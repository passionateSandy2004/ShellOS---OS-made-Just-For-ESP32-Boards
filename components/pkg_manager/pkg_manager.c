#include "pkg_manager.h"
#include "shpkg.h"
#include "lua_runtime.h"
#include "shell_io.h"
#include "shell_fs.h"

#include "esp_log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <dirent.h>
#include <errno.h>
#include <unistd.h>

static const char *TAG = "pkg_mgr";

/* ── Path helpers ── */
#define PKG_ROOT   SHELL_FS_ROOT "/packages"
#define DATA_ROOT  SHELL_FS_ROOT "/data"

static void pkg_dir(const char *name, char *out, size_t n)
{ snprintf(out, n, PKG_ROOT "/%s", name); }

static void data_dir(const char *name, char *out, size_t n)
{ snprintf(out, n, DATA_ROOT "/%s", name); }

static void manifest_path(const char *name, char *out, size_t n)
{ snprintf(out, n, PKG_ROOT "/%s/manifest.json", name); }

static void entry_path(const char *name, char *out, size_t n)
{ snprintf(out, n, PKG_ROOT "/%s/main.lua", name); }

/* ── mkdir -p (up to 3 levels) ── */
static void ensure_dir(const char *path)
{
    struct stat st;
    if (stat(path, &st) == 0) return;
    /* Walk segments */
    char tmp[SHELL_FS_PATH_MAX];
    strncpy(tmp, path, sizeof(tmp) - 1);
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            mkdir(tmp, 0777);
            *p = '/';
        }
    }
    mkdir(tmp, 0777);
}

/* ── Simple JSON field reader (no library needed for our small manifests) ── */
static int json_string_field(const char *json, const char *key,
                              char *out, size_t outsz)
{
    char needle[64];
    snprintf(needle, sizeof(needle), "\"%s\"", key);
    const char *p = strstr(json, needle);
    if (!p) return -1;
    p += strlen(needle);
    while (*p == ' ' || *p == ':' || *p == ' ') p++;
    if (*p != '"') return -1;
    p++;
    size_t i = 0;
    while (*p && *p != '"' && i < outsz - 1) out[i++] = *p++;
    out[i] = '\0';
    return (int)i;
}

static int json_bool_field(const char *json, const char *key)
    __attribute__((unused));
static int json_bool_field(const char *json, const char *key)
{
    char needle[64];
    snprintf(needle, sizeof(needle), "\"%s\"", key);
    const char *p = strstr(json, needle);
    if (!p) return -1;
    p += strlen(needle);
    while (*p == ' ' || *p == ':' || *p == ' ') p++;
    if (strncmp(p, "true", 4) == 0) return 1;
    if (strncmp(p, "false", 5) == 0) return 0;
    return -1;
}

/* ── Read whole file ── */
static char *read_file_alloc(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    if (sz <= 0 || sz > 8192) { fclose(f); return NULL; }
    rewind(f);
    char *buf = malloc((size_t)sz + 1);
    if (!buf) { fclose(f); return NULL; }
    size_t got = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    buf[got] = '\0';
    return buf;
}

/* ── Recursive directory removal ── */
static void rmdir_recursive(const char *path)
{
    DIR *d = opendir(path);
    if (!d) { remove(path); return; }
    struct dirent *e;
    char child[SHELL_FS_PATH_MAX * 2];
    while ((e = readdir(d))) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        snprintf(child, sizeof(child), "%s/%s", path, e->d_name);
        if (e->d_type == DT_DIR) rmdir_recursive(child);
        else remove(child);
    }
    closedir(d);
    rmdir(path);
}

/* ──────────────────────────────────────────────────────── */

void pkg_manager_init(void)
{
    ensure_dir(PKG_ROOT);
    ensure_dir(DATA_ROOT);
    ESP_LOGI(TAG, "pkg_manager ready");
}

esp_err_t pkg_install(const char *shpkg_abs_path)
{
    if (!shpkg_abs_path) return ESP_ERR_INVALID_ARG;

    /* Extract to a temp dir first so we can read the manifest */
    char tmp_dir[SHELL_FS_PATH_MAX];
    snprintf(tmp_dir, sizeof(tmp_dir), PKG_ROOT "/.install_tmp");
    ensure_dir(tmp_dir);

    esp_err_t err = shpkg_extract(shpkg_abs_path, tmp_dir);
    if (err != ESP_OK) {
        rmdir_recursive(tmp_dir);
        shell_io_println("[pkg] Extraction failed.");
        return err;
    }

    /* Read manifest */
    char mpath[SHELL_FS_PATH_MAX * 2];
    snprintf(mpath, sizeof(mpath), "%s/manifest.json", tmp_dir);
    char *manifest = read_file_alloc(mpath);
    if (!manifest) {
        rmdir_recursive(tmp_dir);
        shell_io_println("[pkg] No manifest.json in package.");
        return ESP_FAIL;
    }

    char name[PKG_NAME_MAX] = {0};
    char version[PKG_VERSION_MAX] = {0};
    json_string_field(manifest, "name",    name,    sizeof(name));
    json_string_field(manifest, "version", version, sizeof(version));
    free(manifest);

    if (name[0] == '\0') {
        rmdir_recursive(tmp_dir);
        shell_io_println("[pkg] manifest.json missing 'name' field.");
        return ESP_FAIL;
    }

    /* Validate name (alphanumeric + underscore/dash) */
    for (char *c = name; *c; c++) {
        if (!(*c >= 'a' && *c <= 'z') && !(*c >= 'A' && *c <= 'Z') &&
            !(*c >= '0' && *c <= '9') && *c != '_' && *c != '-') {
            rmdir_recursive(tmp_dir);
            shell_io_printf("[pkg] Invalid package name: %s\n", name);
            return ESP_FAIL;
        }
    }

    /* Move from tmp to final location */
    char final_pkg_dir[SHELL_FS_PATH_MAX];
    pkg_dir(name, final_pkg_dir, sizeof(final_pkg_dir));
    rmdir_recursive(final_pkg_dir);   /* remove old version if present */
    if (rename(tmp_dir, final_pkg_dir) != 0) {
        /* rename across mount points can fail on LittleFS — copy then delete */
        ensure_dir(final_pkg_dir);
        DIR *d = opendir(tmp_dir);
        if (d) {
            struct dirent *e;
            char src[SHELL_FS_PATH_MAX * 2], dst[SHELL_FS_PATH_MAX * 2];
            while ((e = readdir(d))) {
                if (!strcmp(e->d_name,".") || !strcmp(e->d_name,"..")) continue;
                snprintf(src, sizeof(src), "%s/%s", tmp_dir, e->d_name);
                snprintf(dst, sizeof(dst), "%s/%s", final_pkg_dir, e->d_name);
                char *fbuf = read_file_alloc(src);
                if (fbuf) {
                    FILE *fo = fopen(dst, "wb");
                    if (fo) { fputs(fbuf, fo); fclose(fo); }
                    free(fbuf);
                }
                remove(src);
            }
            closedir(d);
        }
        rmdir(tmp_dir);
    }

    /* Ensure data & logs directories */
    char dd[SHELL_FS_PATH_MAX];
    data_dir(name, dd, sizeof(dd));
    char logd[SHELL_FS_PATH_MAX * 2];
    snprintf(logd, sizeof(logd), "%s/logs", dd);
    ensure_dir(dd);
    ensure_dir(logd);

    shell_io_printf("[pkg] Installed: %s v%s\n", name, version[0] ? version : "?");
    return ESP_OK;
}

esp_err_t pkg_run(const char *name)
{
    if (!name || name[0] == '\0') return ESP_ERR_INVALID_ARG;

    /* Check package is installed */
    char ep[SHELL_FS_PATH_MAX];
    entry_path(name, ep, sizeof(ep));
    struct stat st;
    if (stat(ep, &st) != 0) {
        shell_io_printf("[pkg] Package '%s' not installed (no main.lua)\n", name);
        return ESP_ERR_NOT_FOUND;
    }

    char dd[SHELL_FS_PATH_MAX];
    data_dir(name, dd, sizeof(dd));

    esp_err_t err = lua_runtime_run(name, ep, dd);
    if (err == ESP_OK) {
        shell_io_printf("[pkg] Started: %s\n", name);
    } else if (err == ESP_ERR_INVALID_STATE) {
        shell_io_printf("[pkg] Already running: %s\n", name);
    } else {
        shell_io_printf("[pkg] Failed to start %s: %s\n", name, esp_err_to_name(err));
    }
    return err;
}

esp_err_t pkg_stop(const char *name)
{
    if (!name || name[0] == '\0') return ESP_ERR_INVALID_ARG;

    if (!lua_runtime_is_running(name)) {
        shell_io_printf("[pkg] Not running: %s\n", name);
        return ESP_ERR_NOT_FOUND;
    }
    shell_io_printf("[pkg] Stopping: %s ...\n", name);
    esp_err_t err = lua_runtime_stop(name);
    if (err == ESP_OK) {
        shell_io_printf("[pkg] Stopped: %s\n", name);
    }
    return err;
}

esp_err_t pkg_remove(const char *name)
{
    if (!name || name[0] == '\0') return ESP_ERR_INVALID_ARG;

    /* Stop first if running */
    if (lua_runtime_is_running(name)) {
        pkg_stop(name);
    }

    char pd[SHELL_FS_PATH_MAX];
    pkg_dir(name, pd, sizeof(pd));
    char dd[SHELL_FS_PATH_MAX];
    data_dir(name, dd, sizeof(dd));

    rmdir_recursive(pd);
    rmdir_recursive(dd);
    shell_io_printf("[pkg] Removed: %s\n", name);
    return ESP_OK;
}

esp_err_t pkg_list(char *out, size_t len)
{
    DIR *d = opendir(PKG_ROOT);
    if (!d) {
        snprintf(out, len, "(no packages installed)\n");
        return ESP_OK;
    }

    int pos = 0;
    struct dirent *e;
    while ((e = readdir(d))) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        if (e->d_type != DT_DIR) continue;
        if (e->d_name[0] == '.') continue; /* skip .install_tmp */

        const char *name = e->d_name;
        bool running = lua_runtime_is_running(name);

        /* Read version from manifest */
        char version[PKG_VERSION_MAX] = "?";
        char mp[SHELL_FS_PATH_MAX];
        manifest_path(name, mp, sizeof(mp));
        char *mdata = read_file_alloc(mp);
        if (mdata) {
            json_string_field(mdata, "version", version, sizeof(version));
            free(mdata);
        }

        int n = snprintf(out + pos, len - (size_t)pos,
                         "  %-20s  v%-12s  %s\n",
                         name, version, running ? "[running]" : "[stopped]");
        if (n > 0) pos += n;
        if ((size_t)pos >= len - 4) break;
    }
    closedir(d);

    if (pos == 0) snprintf(out, len, "(no packages installed)\n");
    return ESP_OK;
}

void pkg_logs(const char *name, int max_lines)
{
    if (!name) return;
    char logpath[SHELL_FS_PATH_MAX];
    snprintf(logpath, sizeof(logpath), DATA_ROOT "/%s/logs/app.log", name);

    FILE *f = fopen(logpath, "r");
    if (!f) {
        shell_io_printf("[pkg] No log for '%s'\n", name);
        return;
    }

    /* Collect lines into a ring buffer then print last max_lines */
    char **lines = malloc((size_t)max_lines * sizeof(char *));
    if (!lines) { fclose(f); return; }
    memset(lines, 0, (size_t)max_lines * sizeof(char *));

    char lbuf[512];
    int idx = 0, total = 0;
    while (fgets(lbuf, sizeof(lbuf), f)) {
        free(lines[idx % max_lines]);
        lines[idx % max_lines] = strdup(lbuf);
        idx++;
        total++;
    }
    fclose(f);

    int start = (total >= max_lines) ? (idx % max_lines) : 0;
    int count = (total >= max_lines) ? max_lines : total;
    shell_io_printf("── Log: %s (last %d lines) ──\n", name, count);
    for (int i = 0; i < count; i++) {
        char *l = lines[(start + i) % max_lines];
        if (l) { shell_io_print(l); }
    }
    for (int i = 0; i < max_lines; i++) free(lines[i]);
    free(lines);
}

void pkg_info(const char *name)
{
    if (!name) return;
    char mp[SHELL_FS_PATH_MAX];
    manifest_path(name, mp, sizeof(mp));
    char *mdata = read_file_alloc(mp);
    if (!mdata) {
        shell_io_printf("[pkg] Package '%s' not found.\n", name);
        return;
    }
    bool running = lua_runtime_is_running(name);
    shell_io_printf("── Package: %s ──\n%s\nStatus: %s\n",
                    name, mdata, running ? "running" : "stopped");
    free(mdata);
}

esp_err_t pkg_autorun(const char *name, bool enable)
{
    if (!name) return ESP_ERR_INVALID_ARG;
    char mp[SHELL_FS_PATH_MAX];
    manifest_path(name, mp, sizeof(mp));
    char *mdata = read_file_alloc(mp);
    if (!mdata) return ESP_ERR_NOT_FOUND;

    /* Simple string replacement of autorun field */
    char *old_val = enable ? "false" : "true";
    char *new_val = enable ? "true" : "false";
    char *pos = strstr(mdata, old_val);
    if (pos) {
        memcpy(pos, new_val, strlen(new_val));
    } else {
        /* Field not present — append before closing brace */
        char *rbrace = strrchr(mdata, '}');
        if (rbrace) {
            size_t olen   = strlen(mdata);
            size_t add    = 32;
            ptrdiff_t off = rbrace - mdata;   /* save offset before realloc */
            char *nm = realloc(mdata, olen + add);
            if (nm) {
                mdata  = nm;
                rbrace = mdata + off;          /* recompute from new base */
                char insert[32];
                snprintf(insert, sizeof(insert), ",\"autorun\":%s}",
                         enable ? "true" : "false");
                strcpy(rbrace, insert);
            }
        }
    }

    FILE *f = fopen(mp, "w");
    if (f) { fputs(mdata, f); fclose(f); }
    free(mdata);

    shell_io_printf("[pkg] autorun %s: %s\n", name, enable ? "enabled" : "disabled");
    return ESP_OK;
}
