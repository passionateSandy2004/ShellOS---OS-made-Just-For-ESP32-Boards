#ifndef SHELL_FS_H
#define SHELL_FS_H

#include <stddef.h>
#include <stdbool.h>
#include "esp_err.h"

/* VFS mount — LittleFS lives here (like "/" on Linux) */
#define SHELL_FS_ROOT        "/root"
#define SHELL_FS_PARTITION   "storage"
#define SHELL_FS_PATH_MAX    256

bool shell_fs_init(void);
bool shell_fs_ok(void);
/* True after successful mount (for prompt); does not call esp_littlefs_mounted */
bool shell_fs_mounted(void);

void shell_fs_lock(void);
void shell_fs_unlock(void);

/* Resolve path: cwd-relative or absolute; result always under SHELL_FS_ROOT */
esp_err_t shell_fs_resolve(const char *user_path, char *out_abs, size_t out_len);

const char *shell_fs_getcwd(void);
esp_err_t shell_fs_chdir(const char *path);

/* mkdir; if parents, create intermediate dirs (mkdir -p) */
esp_err_t shell_fs_mkdir(const char *path, bool parents);

esp_err_t shell_fs_info(size_t *total_bytes, size_t *used_bytes);

#endif
