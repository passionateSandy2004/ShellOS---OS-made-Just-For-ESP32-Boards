#ifndef PKG_MANAGER_H
#define PKG_MANAGER_H

/*
 * ShellOS package manager
 * Manages installation, lifecycle, and removal of .shpkg packages.
 * Each installed package has:
 *   /root/packages/<name>/manifest.json  — metadata
 *   /root/packages/<name>/main.lua       — entry script
 *   /root/data/<name>/                   — private storage
 *   /root/data/<name>/logs/app.log       — runtime log
 */

#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>

#define PKG_NAME_MAX    32
#define PKG_VERSION_MAX 24
#define PKG_MAX_LISTED  8   /* must match LUA_RT_MAX_INSTANCES */

/* Must be called once at boot, after lua_runtime_init(). */
void pkg_manager_init(void);

/*
 * Install a package from a .shpkg archive already present on the filesystem.
 * shpkg_abs_path: absolute path under /root (e.g. "/root/.cache/pkg/upload.tmp")
 */
esp_err_t pkg_install(const char *shpkg_abs_path);

/* Start the named package as a FreeRTOS/Lua task. */
esp_err_t pkg_run(const char *name);

/* Request a graceful stop for the named package. */
esp_err_t pkg_stop(const char *name);

/* Stop (if running) and delete all package files. */
esp_err_t pkg_remove(const char *name);

/*
 * Write a human-readable package list into out[0..len-1].
 * Format: one "name  version  status" per line.
 */
esp_err_t pkg_list(char *out, size_t len);

/* Read the last max_lines of /root/data/<name>/logs/app.log to shell_io. */
void pkg_logs(const char *name, int max_lines);

/* Print /root/packages/<name>/manifest.json to shell_io. */
void pkg_info(const char *name);

/* Toggle autorun flag in manifest (for future kernel_boot walk). */
esp_err_t pkg_autorun(const char *name, bool enable);

#endif /* PKG_MANAGER_H */
