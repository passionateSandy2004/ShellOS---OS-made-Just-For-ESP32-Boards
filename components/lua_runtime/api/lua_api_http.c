/*
 * ShellOS Lua HTTP API
 * http.get(url)             → body_string | nil, err_string
 * http.post(url, body)      → body_string | nil, err_string
 * Thin wrapper over ESP-IDF esp_http_client.
 */
#include "lua_api_http.h"
#include "lauxlib.h"
#include "lualib.h"

#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_log.h"

#include <stdlib.h>
#include <string.h>

static const char *TAG = "lua_http";

#define HTTP_RESP_MAX (32 * 1024)
#define HTTP_BUF_SIZE 1024

typedef struct {
    char   *buf;
    size_t  len;
    size_t  cap;
    bool    overflow;
} resp_ctx_t;

static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    resp_ctx_t *ctx = (resp_ctx_t *)evt->user_data;
    if (!ctx) return ESP_OK;

    if (evt->event_id == HTTP_EVENT_ON_DATA && evt->data_len > 0) {
        if (ctx->overflow) return ESP_OK;
        size_t need = ctx->len + (size_t)evt->data_len + 1;
        if (need > HTTP_RESP_MAX) { ctx->overflow = true; return ESP_OK; }
        if (need > ctx->cap) {
            size_t nc = need * 2;
            if (nc > HTTP_RESP_MAX) nc = HTTP_RESP_MAX;
            char *nb = realloc(ctx->buf, nc);
            if (!nb) { ctx->overflow = true; return ESP_OK; }
            ctx->buf = nb;
            ctx->cap = nc;
        }
        memcpy(ctx->buf + ctx->len, evt->data, (size_t)evt->data_len);
        ctx->len += (size_t)evt->data_len;
        ctx->buf[ctx->len] = '\0';
    }
    return ESP_OK;
}

static int do_request(lua_State *L, const char *url, const char *method,
                      const char *body, size_t body_len)
{
    resp_ctx_t ctx = { .buf = malloc(HTTP_BUF_SIZE), .cap = HTTP_BUF_SIZE };
    if (!ctx.buf) { lua_pushnil(L); lua_pushliteral(L, "OOM"); return 2; }
    ctx.buf[0] = '\0';

    esp_http_client_config_t cfg = {
        .url                   = url,
        .timeout_ms            = 30000,
        .buffer_size           = HTTP_BUF_SIZE,
        .crt_bundle_attach     = esp_crt_bundle_attach,
        .max_redirection_count = 5,
        .event_handler         = http_event_handler,
        .user_data             = &ctx,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) {
        free(ctx.buf);
        lua_pushnil(L); lua_pushliteral(L, "client init failed");
        return 2;
    }

    if (body && body_len > 0) {
        esp_http_client_set_method(client, HTTP_METHOD_POST);
        esp_http_client_set_post_field(client, body, (int)body_len);
        esp_http_client_set_header(client, "Content-Type", "application/octet-stream");
    }

    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    if (err != ESP_OK) {
        free(ctx.buf);
        lua_pushnil(L);
        lua_pushstring(L, esp_err_to_name(err));
        return 2;
    }
    if (status < 200 || status >= 300) {
        free(ctx.buf);
        lua_pushnil(L);
        lua_pushfstring(L, "HTTP %d", status);
        return 2;
    }
    if (ctx.overflow) {
        ESP_LOGW(TAG, "response truncated at %d bytes", HTTP_RESP_MAX);
    }

    lua_pushlstring(L, ctx.buf, ctx.len);
    free(ctx.buf);
    lua_pushnil(L);  /* no error */
    return 2;
}

static int l_http_get(lua_State *L)
{
    const char *url = luaL_checkstring(L, 1);
    return do_request(L, url, "GET", NULL, 0);
}

static int l_http_post(lua_State *L)
{
    const char *url = luaL_checkstring(L, 1);
    size_t blen;
    const char *body = luaL_optlstring(L, 2, "", &blen);
    return do_request(L, url, "POST", body, blen);
}

static const luaL_Reg http_funcs[] = {
    {"get",  l_http_get},
    {"post", l_http_post},
    {NULL, NULL}
};

int lua_api_http_open(lua_State *L)
{
    luaL_newlib(L, http_funcs);
    return 1;
}
