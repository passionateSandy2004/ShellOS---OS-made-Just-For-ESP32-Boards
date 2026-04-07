#include "http_upload.h"
#include "pkg_manager.h"
#include "shell_fs.h"

#include "esp_http_server.h"
#include "esp_log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <errno.h>

static const char *TAG = "http_upload";

static httpd_handle_t _server = NULL;

#define UPLOAD_TMP   SHELL_FS_ROOT "/.cache/pkg/upload.tmp"
#define CHUNK_SZ     4096
#define MAX_PKG_SIZE (512 * 1024)   /* 512 KB */

/* ─────────────────────────────────────────
   Helpers
   ───────────────────────────────────────── */

static esp_err_t send_json(httpd_req_t *req, int status,
                            const char *body, size_t body_len)
{
    char status_line[16];
    snprintf(status_line, sizeof(status_line), "%d", status);
    httpd_resp_set_status(req, status == 200 ? "200 OK" :
                               status == 201 ? "201 Created" :
                               status == 400 ? "400 Bad Request" :
                               status == 404 ? "404 Not Found" :
                                               "500 Internal Server Error");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    return httpd_resp_send(req, body, (ssize_t)body_len);
}

static esp_err_t send_ok(httpd_req_t *req, const char *msg)
{
    char buf[256];
    int n = snprintf(buf, sizeof(buf), "{\"ok\":true,\"message\":\"%s\"}", msg);
    return send_json(req, 200, buf, (size_t)n);
}

static esp_err_t send_err(httpd_req_t *req, int code, const char *msg)
{
    char buf[256];
    int n = snprintf(buf, sizeof(buf), "{\"ok\":false,\"error\":\"%s\"}", msg);
    return send_json(req, code, buf, (size_t)n);
}

/* Extract last path segment after the 4th '/' → "name" from /pkg/run/<name> */
static const char *last_segment(const char *uri)
{
    const char *p = uri;
    int slashes = 0;
    while (*p) { if (*p++ == '/') slashes++; }
    if (slashes < 3) return NULL;
    p = uri;
    int s = 0;
    while (*p) { if (*p == '/') { if (++s == 3) return p + 1; } p++; }
    return NULL;
}

/* ─────────────────────────────────────────
   POST /pkg/upload
   Receives raw .shpkg body, writes to temp file, calls pkg_install()
   ───────────────────────────────────────── */
static esp_err_t handler_upload(httpd_req_t *req)
{
    int content_len = (int)req->content_len;
    if (content_len <= 0 || content_len > MAX_PKG_SIZE) {
        return send_err(req, 400, "invalid or too-large body");
    }

    /* Ensure cache directory */
    struct stat st;
    if (stat(SHELL_FS_ROOT "/.cache/pkg", &st) != 0) {
        mkdir(SHELL_FS_ROOT "/.cache", 0777);
        mkdir(SHELL_FS_ROOT "/.cache/pkg", 0777);
    }

    FILE *f = fopen(UPLOAD_TMP, "wb");
    if (!f) return send_err(req, 500, "cannot create temp file");

    char *chunk = malloc(CHUNK_SZ);
    if (!chunk) { fclose(f); return send_err(req, 500, "OOM"); }

    int received = 0;
    bool ok = true;
    while (received < content_len) {
        int to_recv = content_len - received;
        if (to_recv > CHUNK_SZ) to_recv = CHUNK_SZ;
        int ret = httpd_req_recv(req, chunk, (size_t)to_recv);
        if (ret <= 0) { ok = false; break; }
        if (fwrite(chunk, 1, (size_t)ret, f) != (size_t)ret) { ok = false; break; }
        received += ret;
    }
    free(chunk);
    fclose(f);

    if (!ok) {
        remove(UPLOAD_TMP);
        return send_err(req, 500, "receive error");
    }

    ESP_LOGI(TAG, "received %d bytes, installing...", received);
    esp_err_t err = pkg_install(UPLOAD_TMP);
    remove(UPLOAD_TMP);

    if (err != ESP_OK) {
        return send_err(req, 400, esp_err_to_name(err));
    }
    return send_ok(req, "package installed");
}

/* ─────────────────────────────────────────
   GET /pkg/list
   Returns JSON array of installed packages with running status
   ───────────────────────────────────────── */
static esp_err_t handler_list(httpd_req_t *req)
{
    /* Use pkg_list to get text, then format as JSON */
    char text[1024];
    pkg_list(text, sizeof(text));

    /* Build JSON array by parsing the text lines */
    char json[2048];
    int jpos = 0;
    jpos += snprintf(json + jpos, sizeof(json) - (size_t)jpos, "[");

    char *line = strtok(text, "\n");
    bool first = true;
    while (line) {
        /* Skip leading spaces */
        while (*line == ' ') line++;
        if (*line == '\0' || *line == '(') { line = strtok(NULL, "\n"); continue; }

        /* Parse: name  version  [status] */
        char pname[PKG_NAME_MAX] = {0}, pver[24] = {0}, pstat[16] = {0};
        sscanf(line, "%31s v%23s %15s", pname, pver, pstat);

        bool running = (strstr(pstat, "running") != NULL);

        /* Read description from manifest */
        char pdesc[64] = {0};
        char mpath[SHELL_FS_PATH_MAX + 32];
        snprintf(mpath, sizeof(mpath), SHELL_FS_ROOT "/packages/%s/manifest.json", pname);
        FILE *mf = fopen(mpath, "r");
        if (mf) {
            char mbuf[256];
            size_t mr = fread(mbuf, 1, sizeof(mbuf) - 1, mf);
            fclose(mf);
            mbuf[mr] = '\0';
            char *dp = strstr(mbuf, "\"description\"");
            if (dp) {
                dp = strchr(dp, ':');
                if (dp) {
                    dp++;
                    while (*dp == ' ' || *dp == '"') dp++;
                    char *ep = strchr(dp, '"');
                    if (ep) { size_t dl = (size_t)(ep - dp); if (dl >= sizeof(pdesc)) dl = sizeof(pdesc)-1; memcpy(pdesc, dp, dl); pdesc[dl] = '\0'; }
                }
            }
        }

        if (!first) jpos += snprintf(json + jpos, sizeof(json) - (size_t)jpos, ",");
        jpos += snprintf(json + jpos, sizeof(json) - (size_t)jpos,
                         "{\"name\":\"%s\",\"version\":\"%s\",\"running\":%s,\"description\":\"%s\"}",
                         pname, pver, running ? "true" : "false", pdesc);
        first = false;
        line = strtok(NULL, "\n");
    }
    jpos += snprintf(json + jpos, sizeof(json) - (size_t)jpos, "]");

    return send_json(req, 200, json, (size_t)jpos);
}

/* ─────────────────────────────────────────
   POST /pkg/run/<name>
   ───────────────────────────────────────── */
static esp_err_t handler_run(httpd_req_t *req)
{
    const char *name = last_segment(req->uri);
    if (!name || name[0] == '\0') return send_err(req, 400, "missing package name");

    esp_err_t err = pkg_run(name);
    if (err == ESP_OK) return send_ok(req, "started");
    if (err == ESP_ERR_INVALID_STATE) return send_err(req, 409, "already running");
    if (err == ESP_ERR_NOT_FOUND) return send_err(req, 404, "package not found");
    return send_err(req, 500, esp_err_to_name(err));
}

/* ─────────────────────────────────────────
   POST /pkg/stop/<name>
   ───────────────────────────────────────── */
static esp_err_t handler_stop(httpd_req_t *req)
{
    const char *name = last_segment(req->uri);
    if (!name || name[0] == '\0') return send_err(req, 400, "missing package name");

    esp_err_t err = pkg_stop(name);
    if (err == ESP_OK) return send_ok(req, "stopped");
    if (err == ESP_ERR_NOT_FOUND) return send_err(req, 404, "not running");
    return send_err(req, 500, esp_err_to_name(err));
}

/* ─────────────────────────────────────────
   POST /pkg/remove/<name>
   ───────────────────────────────────────── */
static esp_err_t handler_remove(httpd_req_t *req)
{
    const char *name = last_segment(req->uri);
    if (!name || name[0] == '\0') return send_err(req, 400, "missing package name");

    esp_err_t err = pkg_remove(name);
    if (err == ESP_OK) return send_ok(req, "removed");
    if (err == ESP_ERR_NOT_FOUND) return send_err(req, 404, "package not found");
    if (err == ESP_ERR_INVALID_STATE) return send_err(req, 409, "stop package before removing");
    return send_err(req, 500, esp_err_to_name(err));
}

/* ─────────────────────────────────────────
   CORS preflight handler
   ───────────────────────────────────────── */
static esp_err_t handler_options(httpd_req_t *req)
{
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Methods", "GET, POST, OPTIONS");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", "Content-Type");
    return httpd_resp_send(req, NULL, 0);
}

/* ─────────────────────────────────────────
   Server start / stop
   ───────────────────────────────────────── */
esp_err_t http_upload_server_start(void)
{
    if (_server) return ESP_ERR_INVALID_STATE;  /* already running */

    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.server_port         = HTTP_UPLOAD_PORT;
    cfg.max_uri_handlers    = 10;
    cfg.stack_size          = 8192;
    cfg.recv_wait_timeout   = 30;
    cfg.send_wait_timeout   = 30;
    cfg.uri_match_fn        = httpd_uri_match_wildcard;

    esp_err_t err = httpd_start(&_server, &cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "httpd_start failed: %s", esp_err_to_name(err));
        return err;
    }

    static const httpd_uri_t routes[] = {
        { .uri="/pkg/upload",   .method=HTTP_POST,    .handler=handler_upload  },
        { .uri="/pkg/list",     .method=HTTP_GET,     .handler=handler_list    },
        { .uri="/pkg/run/*",    .method=HTTP_POST,    .handler=handler_run     },
        { .uri="/pkg/stop/*",   .method=HTTP_POST,    .handler=handler_stop    },
        { .uri="/pkg/remove/*", .method=HTTP_POST,    .handler=handler_remove  },
        { .uri="/pkg/*",        .method=HTTP_OPTIONS, .handler=handler_options },
    };

    for (size_t i = 0; i < sizeof(routes)/sizeof(routes[0]); i++) {
        httpd_register_uri_handler(_server, &routes[i]);
    }

    ESP_LOGI(TAG, "HTTP upload server started on port %d", HTTP_UPLOAD_PORT);
    return ESP_OK;
}

void http_upload_server_stop(void)
{
    if (_server) {
        httpd_stop(_server);
        _server = NULL;
        ESP_LOGI(TAG, "HTTP upload server stopped");
    }
}
