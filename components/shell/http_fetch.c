#include "http_fetch.h"
#include "shell_fs.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define FETCH_BUF_SIZE 1024
#define MANIFEST_MAX   8192

/* Create parent dir of a file under /root (e.g. /root/downloads/LICENSE → mkdir downloads) */
static esp_err_t ensure_parent_dir_for_abs(const char *abs_path)
{
    char tmp[SHELL_FS_PATH_MAX];
    strncpy(tmp, abs_path, sizeof(tmp) - 1);
    tmp[sizeof(tmp) - 1] = '\0';
    char *slash = strrchr(tmp, '/');
    if (!slash || slash == tmp) {
        return ESP_OK;
    }
    *slash = '\0';

    size_t rootlen = strlen(SHELL_FS_ROOT);
    if (strncmp(tmp, SHELL_FS_ROOT, rootlen) != 0) {
        return ESP_ERR_INVALID_ARG;
    }
    const char *rel = tmp + rootlen;
    if (*rel == '/') {
        rel++;
    }
    if (*rel == '\0') {
        return ESP_OK;
    }

    shell_fs_lock();
    esp_err_t e = shell_fs_mkdir(rel, true);
    shell_fs_unlock();
    return e;
}

esp_err_t shell_http_fetch_to_file(const char *url, const char *abs_path)
{
    if (!url || !abs_path) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = ensure_parent_dir_for_abs(abs_path);
    if (err != ESP_OK) {
        return err;
    }

    esp_http_client_config_t cfg = {
        .url                    = url,
        .timeout_ms             = 120000,
        .buffer_size            = FETCH_BUF_SIZE,
        .crt_bundle_attach      = esp_crt_bundle_attach,
        .max_redirection_count  = 10,
    };

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) {
        return ESP_FAIL;
    }

    err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        esp_http_client_cleanup(client);
        return err;
    }

    (void)esp_http_client_fetch_headers(client);

    int status = esp_http_client_get_status_code(client);
    if (status < 200 || status >= 300) {
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return ESP_FAIL;
    }

    shell_fs_lock();
    FILE *f = fopen(abs_path, "wb");
    if (!f) {
        shell_fs_unlock();
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return ESP_FAIL;
    }

    char buf[FETCH_BUF_SIZE];
    while (1) {
        int r = esp_http_client_read(client, buf, sizeof(buf));
        if (r < 0) {
            fclose(f);
            unlink(abs_path);
            shell_fs_unlock();
            esp_http_client_close(client);
            esp_http_client_cleanup(client);
            return ESP_FAIL;
        }
        if (r == 0) {
            break;
        }
        if (fwrite(buf, 1, (size_t)r, f) != (size_t)r) {
            fclose(f);
            unlink(abs_path);
            shell_fs_unlock();
            esp_http_client_close(client);
            esp_http_client_cleanup(client);
            return ESP_FAIL;
        }
    }
    fclose(f);
    shell_fs_unlock();

    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return ESP_OK;
}

static esp_err_t mkdir_parents_for_file(const char *rel_path)
{
    char tmp[SHELL_FS_PATH_MAX];
    if (strlen(rel_path) >= sizeof(tmp)) {
        return ESP_ERR_INVALID_ARG;
    }
    strcpy(tmp, rel_path);
    char *slash = strrchr(tmp, '/');
    if (!slash || slash == tmp) {
        return ESP_OK;
    }
    *slash = '\0';

    shell_fs_lock();
    esp_err_t e = shell_fs_mkdir(tmp, true);
    shell_fs_unlock();
    return e;
}

static void trim_line(char *s)
{
    size_t n = strlen(s);
    while (n > 0 && (s[n - 1] == '\n' || s[n - 1] == '\r')) {
        s[--n] = '\0';
    }
}

esp_err_t shell_pkg_install_manifest(const char *manifest_url)
{
    if (!manifest_url) {
        return ESP_ERR_INVALID_ARG;
    }

    char manifest_abs[SHELL_FS_PATH_MAX];
    if (shell_fs_resolve(".cache/pkg/manifest.tmp", manifest_abs, sizeof(manifest_abs)) != ESP_OK) {
        return ESP_FAIL;
    }

    shell_fs_lock();
    (void)shell_fs_mkdir(".cache", true);
    (void)shell_fs_mkdir(".cache/pkg", true);
    shell_fs_unlock();

    esp_err_t err = shell_http_fetch_to_file(manifest_url, manifest_abs);
    if (err != ESP_OK) {
        return err;
    }

    char *blob = (char *)malloc(MANIFEST_MAX + 1);
    if (!blob) {
        return ESP_ERR_NO_MEM;
    }

    shell_fs_lock();
    FILE *mf = fopen(manifest_abs, "rb");
    if (!mf) {
        shell_fs_unlock();
        free(blob);
        return ESP_FAIL;
    }
    if (fseek(mf, 0, SEEK_END) != 0) {
        fclose(mf);
        shell_fs_unlock();
        free(blob);
        return ESP_FAIL;
    }
    long sz = ftell(mf);
    if (sz < 0 || sz > MANIFEST_MAX) {
        fclose(mf);
        shell_fs_unlock();
        free(blob);
        return ESP_FAIL;
    }
    rewind(mf);
    size_t got = fread(blob, 1, (size_t)sz, mf);
    fclose(mf);
    shell_fs_unlock();

    if (got != (size_t)sz) {
        free(blob);
        return ESP_FAIL;
    }
    blob[got] = '\0';

    for (char *line = strtok(blob, "\n"); line != NULL; line = strtok(NULL, "\n")) {
        trim_line(line);
        while (*line == ' ' || *line == '\t') {
            line++;
        }
        if (*line == '\0' || *line == '#') {
            continue;
        }

        char *sp = strchr(line, ' ');
        if (!sp) {
            continue;
        }
        *sp++ = '\0';
        while (*sp == ' ' || *sp == '\t') {
            sp++;
        }
        if (strncmp(sp, "http://", 7) != 0 && strncmp(sp, "https://", 8) != 0) {
            continue;
        }

        err = mkdir_parents_for_file(line);
        if (err != ESP_OK) {
            free(blob);
            return err;
        }

        char out_abs[SHELL_FS_PATH_MAX];
        err = shell_fs_resolve(line, out_abs, sizeof(out_abs));
        if (err != ESP_OK) {
            free(blob);
            return err;
        }

        err = shell_http_fetch_to_file(sp, out_abs);
        if (err != ESP_OK) {
            free(blob);
            return err;
        }
    }

    free(blob);
    return ESP_OK;
}
