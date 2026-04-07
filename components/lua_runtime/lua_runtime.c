#include "lua_runtime.h"

#include "lua.h"
#include "lualib.h"
#include "lauxlib.h"

#include "lua_api_gpio.h"
#include "lua_api_timer.h"
#include "lua_api_serial.h"
#include "lua_api_fs.h"
#include "lua_api_log.h"
#include "lua_api_http.h"

#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static const char *TAG = "lua_rt";

/* ─────────────────────────────────────────
   Lua registry keys (light-userdata pointers used as unique keys)
   ───────────────────────────────────────── */
static const char _LRK_STOP     = 's';
static const char _LRK_DATADIR  = 'd';
static const char _LRK_PKGNAME  = 'n';

/* ─────────────────────────────────────────
   Instance registry
   ───────────────────────────────────────── */
typedef struct {
    char         name[LUA_RT_PKG_NAME_MAX];
    lua_State   *L;
    TaskHandle_t task;
    volatile bool running;
    volatile bool stop_requested;
    char         data_dir[LUA_RT_DATA_DIR_MAX];
} pkg_instance_t;

static pkg_instance_t   _insts[LUA_RT_MAX_INSTANCES];
static SemaphoreHandle_t _mutex;

/* ─────────────────────────────────────────
   PSRAM-aware allocator
   ───────────────────────────────────────── */
static void *lua_psram_alloc(void *ud, void *ptr, size_t osize, size_t nsize)
{
    (void)ud; (void)osize;
    if (nsize == 0) {
        heap_caps_free(ptr);
        return NULL;
    }
    /* prefer PSRAM for large allocations if PSRAM is present */
    if (nsize > 512) {
        void *np = heap_caps_realloc(ptr, nsize,
                       MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (np) return np;
    }
    return heap_caps_realloc(ptr, nsize, MALLOC_CAP_DEFAULT);
}

/* ─────────────────────────────────────────
   Open only the libraries a package script needs
   ───────────────────────────────────────── */
static const luaL_Reg _shellos_libs[] = {
    {LUA_GNAME,       luaopen_base},
    {LUA_TABLIBNAME,  luaopen_table},
    {LUA_STRLIBNAME,  luaopen_string},
    {LUA_MATHLIBNAME, luaopen_math},
    {LUA_COLIBNAME,   luaopen_coroutine},
    {LUA_UTF8LIBNAME, luaopen_utf8},
    /* ShellOS API */
    {"gpio",    lua_api_gpio_open},
    {"serial",  lua_api_serial_open},
    {"file",    lua_api_fs_open},
    {"http",    lua_api_http_open},
    {NULL, NULL}
};

static void open_shellos_libs(lua_State *L)
{
    for (const luaL_Reg *lib = _shellos_libs; lib->func; lib++) {
        luaL_requiref(L, lib->name, lib->func, 1);
        lua_pop(L, 1);
    }
    /* Inject delay() and millis() as plain globals */
    lua_api_timer_inject(L);
    /* Inject log() as a plain global */
    lua_api_log_inject(L);
}

/* ─────────────────────────────────────────
   Registry helpers
   ───────────────────────────────────────── */
void lua_rt_mark_stop(lua_State *L)
{
    lua_pushlightuserdata(L, (void *)&_LRK_STOP);
    lua_pushboolean(L, 1);
    lua_rawset(L, LUA_REGISTRYINDEX);
}

bool lua_rt_check_stop(lua_State *L)
{
    lua_pushlightuserdata(L, (void *)&_LRK_STOP);
    lua_rawget(L, LUA_REGISTRYINDEX);
    bool v = lua_toboolean(L, -1);
    lua_pop(L, 1);
    return v;
}

const char *lua_rt_data_dir(lua_State *L)
{
    lua_pushlightuserdata(L, (void *)&_LRK_DATADIR);
    lua_rawget(L, LUA_REGISTRYINDEX);
    const char *s = lua_tostring(L, -1);
    lua_pop(L, 1);
    return s;
}

void lua_rt_append_log(lua_State *L, const char *msg)
{
    const char *dd = lua_rt_data_dir(L);
    if (!dd || !msg) return;
    char logpath[LUA_RT_DATA_DIR_MAX + 32];
    snprintf(logpath, sizeof(logpath), "%s/logs/app.log", dd);
    FILE *f = fopen(logpath, "a");
    if (f) { fprintf(f, "%s\n", msg); fclose(f); }
}

/* ─────────────────────────────────────────
   Instance lookup helpers (call with mutex held)
   ───────────────────────────────────────── */
static pkg_instance_t *find_inst_by_name(const char *name)
{
    for (int i = 0; i < LUA_RT_MAX_INSTANCES; i++) {
        if (_insts[i].running && strcmp(_insts[i].name, name) == 0)
            return &_insts[i];
    }
    return NULL;
}

static pkg_instance_t *alloc_inst(const char *name)
{
    for (int i = 0; i < LUA_RT_MAX_INSTANCES; i++) {
        if (!_insts[i].running && _insts[i].task == NULL) {
            memset(&_insts[i], 0, sizeof(_insts[i]));
            strncpy(_insts[i].name, name, LUA_RT_PKG_NAME_MAX - 1);
            return &_insts[i];
        }
    }
    return NULL;
}

/* ─────────────────────────────────────────
   Package task
   ───────────────────────────────────────── */
typedef struct {
    int   inst_idx;
    char  main_lua_path[256];
} task_arg_t;

static void package_task(void *arg)
{
    task_arg_t *targ = (task_arg_t *)arg;
    int idx = targ->inst_idx;
    char main_path[256];
    strncpy(main_path, targ->main_lua_path, sizeof(main_path) - 1);
    free(targ);

    pkg_instance_t *inst = &_insts[idx];

    lua_State *L = lua_newstate(lua_psram_alloc, NULL);
    if (!L) {
        ESP_LOGE(TAG, "pkg %s: lua_newstate failed (OOM?)", inst->name);
        inst->running = false;
        inst->task = NULL;
        vTaskDelete(NULL);
        return;
    }

    xSemaphoreTake(_mutex, portMAX_DELAY);
    inst->L = L;
    xSemaphoreGive(_mutex);

    /* Store data_dir and pkg_name in the Lua registry for API bindings */
    lua_pushlightuserdata(L, (void *)&_LRK_DATADIR);
    lua_pushstring(L, inst->data_dir);
    lua_rawset(L, LUA_REGISTRYINDEX);

    lua_pushlightuserdata(L, (void *)&_LRK_PKGNAME);
    lua_pushstring(L, inst->name);
    lua_rawset(L, LUA_REGISTRYINDEX);

    /* Open standard + ShellOS libraries */
    open_shellos_libs(L);

    /* Run the package entry script */
    int err = luaL_dofile(L, main_path);
    if (err != LUA_OK) {
        const char *msg = lua_tostring(L, -1);
        ESP_LOGE(TAG, "pkg %s: %s", inst->name, msg ? msg : "(no message)");
        lua_rt_append_log(L, msg ? msg : "script error");
    }

    lua_close(L);

    xSemaphoreTake(_mutex, portMAX_DELAY);
    inst->L       = NULL;
    inst->running = false;
    inst->task    = NULL;
    xSemaphoreGive(_mutex);

    vTaskDelete(NULL);
}

/* ─────────────────────────────────────────
   Public API
   ───────────────────────────────────────── */
void lua_runtime_init(void)
{
    _mutex = xSemaphoreCreateMutex();
    memset(_insts, 0, sizeof(_insts));
}

esp_err_t lua_runtime_run(const char *pkg_name,
                           const char *main_lua_path,
                           const char *data_dir)
{
    if (!pkg_name || !main_lua_path || !data_dir) return ESP_ERR_INVALID_ARG;

    xSemaphoreTake(_mutex, portMAX_DELAY);

    if (find_inst_by_name(pkg_name)) {
        xSemaphoreGive(_mutex);
        ESP_LOGW(TAG, "pkg %s already running", pkg_name);
        return ESP_ERR_INVALID_STATE;
    }

    pkg_instance_t *inst = alloc_inst(pkg_name);
    if (!inst) {
        xSemaphoreGive(_mutex);
        ESP_LOGE(TAG, "no free package slots");
        return ESP_ERR_NO_MEM;
    }

    strncpy(inst->data_dir, data_dir, LUA_RT_DATA_DIR_MAX - 1);
    inst->running        = true;
    inst->stop_requested = false;

    /* Build task arg (freed inside the task) */
    task_arg_t *targ = malloc(sizeof(task_arg_t));
    if (!targ) {
        memset(inst, 0, sizeof(*inst));
        xSemaphoreGive(_mutex);
        return ESP_ERR_NO_MEM;
    }
    targ->inst_idx = (int)(inst - _insts);
    strncpy(targ->main_lua_path, main_lua_path, sizeof(targ->main_lua_path) - 1);

    xSemaphoreGive(_mutex);

    /* Ensure logs directory exists */
    char logdir[LUA_RT_DATA_DIR_MAX + 8];
    snprintf(logdir, sizeof(logdir), "%s/logs", data_dir);
    struct stat st;
    if (stat(logdir, &st) != 0) {
        /* best-effort mkdir via standard C */
        char cmd[sizeof(logdir) + 8];
        snprintf(cmd, sizeof(cmd), "%s", logdir);
        mkdir(cmd, 0777);
    }

    BaseType_t rc = xTaskCreate(package_task, pkg_name, 8192, targ, 2, &inst->task);
    if (rc != pdPASS) {
        free(targ);
        xSemaphoreTake(_mutex, portMAX_DELAY);
        memset(inst, 0, sizeof(*inst));
        xSemaphoreGive(_mutex);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "pkg %s started", pkg_name);
    return ESP_OK;
}

esp_err_t lua_runtime_stop(const char *pkg_name)
{
    if (!pkg_name) return ESP_ERR_INVALID_ARG;

    xSemaphoreTake(_mutex, portMAX_DELAY);
    pkg_instance_t *inst = find_inst_by_name(pkg_name);
    if (!inst) {
        xSemaphoreGive(_mutex);
        return ESP_ERR_NOT_FOUND;
    }

    inst->stop_requested = true;
    if (inst->L) lua_rt_mark_stop(inst->L);
    xSemaphoreGive(_mutex);

    /* Give the task up to 3 seconds to exit cleanly */
    for (int i = 0; i < 30; i++) {
        vTaskDelay(pdMS_TO_TICKS(100));
        xSemaphoreTake(_mutex, portMAX_DELAY);
        bool still = inst->running;
        xSemaphoreGive(_mutex);
        if (!still) break;
    }

    /* Force-kill if still alive (last resort) */
    xSemaphoreTake(_mutex, portMAX_DELAY);
    if (inst->running && inst->task) {
        vTaskDelete(inst->task);
        if (inst->L) { lua_close(inst->L); inst->L = NULL; }
        inst->running = false;
        inst->task    = NULL;
    }
    xSemaphoreGive(_mutex);

    ESP_LOGI(TAG, "pkg %s stopped", pkg_name);
    return ESP_OK;
}

bool lua_runtime_is_running(const char *pkg_name)
{
    if (!pkg_name) return false;
    xSemaphoreTake(_mutex, portMAX_DELAY);
    pkg_instance_t *inst = find_inst_by_name(pkg_name);
    bool r = inst && inst->running;
    xSemaphoreGive(_mutex);
    return r;
}
