/*
 * ShellOS Lua log API
 * log("message") — appends timestamped line to /root/data/<pkg>/logs/app.log
 */
#include "lua_api_log.h"
#include "lua_runtime.h"
#include "lauxlib.h"

#include "esp_timer.h"
#include <stdio.h>
#include <string.h>

static int l_log(lua_State *L)
{
    int n = lua_gettop(L);
    char msg[512];
    int pos = 0;

    /* Concatenate all arguments with a space between them */
    for (int i = 1; i <= n; i++) {
        const char *s = lua_tostring(L, i);
        if (!s) s = "(nil)";
        if (i > 1 && pos < (int)sizeof(msg) - 2) msg[pos++] = ' ';
        int rem = (int)sizeof(msg) - pos - 1;
        int len = (int)strlen(s);
        if (len > rem) len = rem;
        memcpy(msg + pos, s, (size_t)len);
        pos += len;
    }
    msg[pos] = '\0';

    /* Prepend ms timestamp */
    char line[560];
    uint32_t ms = (uint32_t)(esp_timer_get_time() / 1000);
    snprintf(line, sizeof(line), "[%lu] %s", (unsigned long)ms, msg);

    lua_rt_append_log(L, line);
    return 0;
}

void lua_api_log_inject(lua_State *L)
{
    lua_pushcfunction(L, l_log);
    lua_setglobal(L, "log");
}
