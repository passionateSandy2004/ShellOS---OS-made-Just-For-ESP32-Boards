#ifndef LUA_RUNTIME_H
#define LUA_RUNTIME_H

/*
 * ShellOS Lua package runtime.
 * Each installed package runs in its own FreeRTOS task with an isolated Lua state.
 * The Lua state is allocated from PSRAM where available to preserve internal SRAM.
 */

#include "esp_err.h"
#include <stdbool.h>

#define LUA_RT_PKG_NAME_MAX  32
#define LUA_RT_DATA_DIR_MAX  80
#define LUA_RT_MAX_INSTANCES  8

/* ─────────────────────────────────────────
   Lifecycle
   ───────────────────────────────────────── */

/* Call once at boot (before any pkg_run). */
void lua_runtime_init(void);

/*
 * Spawn a FreeRTOS task that creates a Lua state and runs main_lua_path.
 * pkg_name is used for log output and scoping the file API to data_dir.
 * Returns ESP_OK immediately if the task was created; the script runs async.
 */
esp_err_t lua_runtime_run(const char *pkg_name,
                           const char *main_lua_path,
                           const char *data_dir);

/* Request a gracefully stop; the task finishes its current delay then exits. */
esp_err_t lua_runtime_stop(const char *pkg_name);

/* True if the package task is alive. */
bool lua_runtime_is_running(const char *pkg_name);

/* ─────────────────────────────────────────
   Internal helpers (used by API bindings)
   ───────────────────────────────────────── */

typedef struct lua_State lua_State;

/* Store / retrieve stop-request flag via the Lua registry. */
void      lua_rt_mark_stop(lua_State *L);
bool      lua_rt_check_stop(lua_State *L);

/* Retrieve the data directory for the running package from the registry. */
const char *lua_rt_data_dir(lua_State *L);

/* Append a message to /data/<pkg>/logs/app.log */
void lua_rt_append_log(lua_State *L, const char *msg);

#endif /* LUA_RUNTIME_H */
