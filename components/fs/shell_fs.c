#include "shell_fs.h"
#include "esp_littlefs.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>

static const char *TAG = "shell_fs";

static SemaphoreHandle_t s_fs_mutex;
static char              s_cwd[SHELL_FS_PATH_MAX];
static bool              s_mounted;

static bool path_under_root(const char *abs_path)
{
    size_t n = strlen(SHELL_FS_ROOT);
    if (strncmp(abs_path, SHELL_FS_ROOT, n) != 0) {
        return false;
    }
    return abs_path[n] == '\0' || abs_path[n] == '/';
}

/* Collapse . and .. ; in-place on copy in buf */
static esp_err_t normalize_abs(char *buf, size_t cap)
{
    if (!buf || cap < 4) {
        return ESP_ERR_INVALID_ARG;
    }
    if (buf[0] != '/') {
        return ESP_ERR_INVALID_ARG;
    }

    char stack[24][64];
    int  sp = 0;

    const char *s = buf + 1;
    while (*s) {
        while (*s == '/') {
            s++;
        }
        if (!*s) {
            break;
        }

        char seg[64];
        size_t k = 0;
        while (*s && *s != '/' && k < sizeof(seg) - 1) {
            seg[k++] = *s++;
        }
        seg[k] = '\0';

        if (seg[0] == '\0') {
            continue;
        }
        if (strcmp(seg, ".") == 0) {
            continue;
        }
        if (strcmp(seg, "..") == 0) {
            if (sp > 0) {
                sp--;
            }
            continue;
        }
        if (sp >= 24) {
            return ESP_ERR_NO_MEM;
        }
        strncpy(stack[sp], seg, sizeof(stack[0]) - 1);
        stack[sp][sizeof(stack[0]) - 1] = '\0';
        sp++;
    }

    char *w = buf;
    *w++ = '/';
    *w = '\0';
    for (int i = 0; i < sp; i++) {
        size_t room = cap - (size_t)(w - buf);
        if (room < strlen(stack[i]) + 2) {
            return ESP_ERR_NO_MEM;
        }
        if (i > 0) {
            *w++ = '/';
        }
        strcpy(w, stack[i]);
        w += strlen(stack[i]);
    }
    if (sp == 0) {
        buf[0] = '/';
        buf[1] = '\0';
    }
    return ESP_OK;
}

bool shell_fs_init(void)
{
    if (s_mounted) {
        return true;
    }

    if (!s_fs_mutex) {
        s_fs_mutex = xSemaphoreCreateMutex();
        if (!s_fs_mutex) {
            ESP_LOGE(TAG, "mutex alloc failed");
            return false;
        }
    }

    esp_vfs_littlefs_conf_t conf = {
        .base_path              = SHELL_FS_ROOT,
        .partition_label        = SHELL_FS_PARTITION,
        .format_if_mount_failed = true,
        .dont_mount             = false,
        .grow_on_mount          = true,
    };

    esp_err_t err = esp_vfs_littlefs_register(&conf);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "LittleFS mount failed: %s", esp_err_to_name(err));
        return false;
    }

    strncpy(s_cwd, SHELL_FS_ROOT, sizeof(s_cwd) - 1);
    s_cwd[sizeof(s_cwd) - 1] = '\0';
    s_mounted = true;

    /* Default tree from phases.md */
    (void)shell_fs_mkdir("scripts", true);
    (void)shell_fs_mkdir("logs", true);
    (void)shell_fs_mkdir("config", true);

    const char *sample =
        "# WiFi — edit with: write config/wifi.cfg \"ssid=...\"\n"
        "ssid=\n"
        "pass=\n";
    char cfg_path[SHELL_FS_PATH_MAX];
    if (shell_fs_resolve("config/wifi.cfg", cfg_path, sizeof(cfg_path)) == ESP_OK) {
        struct stat st;
        if (stat(cfg_path, &st) != 0) {
            FILE *f = fopen(cfg_path, "w");
            if (f) {
                fputs(sample, f);
                fclose(f);
            }
        }
    }

    /* Use uart only here so we don't splice ESP_LOG into kernel banner (same UART as console) */
    return true;
}

bool shell_fs_ok(void)
{
    return s_mounted && esp_littlefs_mounted(SHELL_FS_PARTITION);
}

bool shell_fs_mounted(void)
{
    return s_mounted;
}

void shell_fs_lock(void)
{
    if (s_fs_mutex) {
        xSemaphoreTake(s_fs_mutex, portMAX_DELAY);
    }
}

void shell_fs_unlock(void)
{
    if (s_fs_mutex) {
        xSemaphoreGive(s_fs_mutex);
    }
}

esp_err_t shell_fs_resolve(const char *user_path, char *out_abs, size_t out_len)
{
    if (!user_path || !out_abs || out_len < strlen(SHELL_FS_ROOT) + 2) {
        return ESP_ERR_INVALID_ARG;
    }

    char tmp[SHELL_FS_PATH_MAX];

    if (user_path[0] == '/') {
        if (strncmp(user_path, SHELL_FS_ROOT, strlen(SHELL_FS_ROOT)) == 0) {
            strncpy(tmp, user_path, sizeof(tmp) - 1);
            tmp[sizeof(tmp) - 1] = '\0';
        } else {
            /* /config/foo -> /data/config/foo */
            int n = snprintf(tmp, sizeof(tmp), "%s%s", SHELL_FS_ROOT, user_path);
            if (n < 0 || (size_t)n >= sizeof(tmp)) {
                return ESP_ERR_INVALID_ARG;
            }
        }
    } else {
        int n = snprintf(tmp, sizeof(tmp), "%s/%s", s_cwd, user_path);
        if (n < 0 || (size_t)n >= sizeof(tmp)) {
            return ESP_ERR_INVALID_ARG;
        }
    }

    esp_err_t e = normalize_abs(tmp, sizeof(tmp));
    if (e != ESP_OK) {
        return e;
    }
    if (!path_under_root(tmp)) {
        return ESP_ERR_INVALID_ARG;
    }
    strncpy(out_abs, tmp, out_len - 1);
    out_abs[out_len - 1] = '\0';
    return ESP_OK;
}

const char *shell_fs_getcwd(void)
{
    return s_cwd;
}

esp_err_t shell_fs_chdir(const char *path)
{
    char abs_path[SHELL_FS_PATH_MAX];
    esp_err_t err = shell_fs_resolve(path, abs_path, sizeof(abs_path));
    if (err != ESP_OK) {
        return err;
    }

    struct stat st;
    if (stat(abs_path, &st) != 0) {
        return ESP_FAIL;
    }
    if (!S_ISDIR(st.st_mode)) {
        return ESP_ERR_INVALID_ARG;
    }
    strncpy(s_cwd, abs_path, sizeof(s_cwd) - 1);
    s_cwd[sizeof(s_cwd) - 1] = '\0';
    return ESP_OK;
}

static esp_err_t mkdir_p(const char *abs_path)
{
    char tmp[SHELL_FS_PATH_MAX];
    strncpy(tmp, abs_path, sizeof(tmp) - 1);
    tmp[sizeof(tmp) - 1] = '\0';
    size_t len = strlen(tmp);
    if (len < 2) {
        return ESP_ERR_INVALID_ARG;
    }
    for (size_t i = 1; i < len; i++) {
        if (tmp[i] == '/') {
            tmp[i] = '\0';
            if (tmp[0] && mkdir(tmp, 0755) != 0 && errno != EEXIST) {
                return ESP_FAIL;
            }
            tmp[i] = '/';
        }
    }
    if (mkdir(tmp, 0755) != 0 && errno != EEXIST) {
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t shell_fs_mkdir(const char *path, bool parents)
{
    char abs_path[SHELL_FS_PATH_MAX];
    esp_err_t err = shell_fs_resolve(path, abs_path, sizeof(abs_path));
    if (err != ESP_OK) {
        return err;
    }
    if (parents) {
        return mkdir_p(abs_path);
    }
    if (mkdir(abs_path, 0755) != 0) {
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t shell_fs_info(size_t *total_bytes, size_t *used_bytes)
{
    if (!shell_fs_ok()) {
        return ESP_ERR_INVALID_STATE;
    }
    return esp_littlefs_info(SHELL_FS_PARTITION, total_bytes, used_bytes);
}
