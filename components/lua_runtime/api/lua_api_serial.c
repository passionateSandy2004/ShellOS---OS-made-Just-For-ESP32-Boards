/*
 * ShellOS Lua serial API
 * serial.print(x), serial.println(x) — routes to UART (not shell_io so it
 * doesn't interfere with the interactive TCP/UART shell session).
 */
#include "lua_api_serial.h"
#include "lauxlib.h"
#include "lualib.h"

#include "uart_driver.h"
#include <string.h>

static void uart_send(const char *s)
{
    if (!s) {
        return;
    }
    uart_driver_send_bytes(s, strlen(s));
}

static int l_serial_print(lua_State *L)
{
    int n = lua_gettop(L);
    for (int i = 1; i <= n; i++) {
        const char *s = lua_tostring(L, i);
        if (s) uart_send(s);
    }
    return 0;
}

static int l_serial_println(lua_State *L)
{
    l_serial_print(L);
    uart_send("\r\n");
    return 0;
}

static int l_serial_begin(lua_State *L)
{
    /* Arduino compat: Serial.begin(baud) — UART already initialised, ignore */
    (void)L;
    return 0;
}

static const luaL_Reg serial_funcs[] = {
    {"print",    l_serial_print},
    {"println",  l_serial_println},
    {"begin",    l_serial_begin},
    {NULL, NULL}
};

int lua_api_serial_open(lua_State *L)
{
    luaL_newlib(L, serial_funcs);
    /* Also expose as global Serial for direct Arduino-style access */
    lua_pushvalue(L, -1);
    lua_setglobal(L, "Serial");
    return 1;
}
