/*
 * ShellOS Lua timer API
 * Injects as globals:  delay(ms)  millis()
 * delay() checks the package stop-request flag every 10 ms
 * and raises a Lua error if stop is requested — cleanly unwinds the stack.
 */
#include "lua_api_timer.h"
#include "lua_runtime.h"
#include "lauxlib.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"

/* ── delay(ms) ── */
static int l_delay(lua_State *L)
{
    int ms = (int)luaL_checkinteger(L, 1);
    if (ms <= 0) return 0;

    while (ms > 0) {
        int chunk = (ms > 10) ? 10 : ms;
        vTaskDelay(pdMS_TO_TICKS(chunk));
        ms -= chunk;

        if (lua_rt_check_stop(L)) {
            lua_pushliteral(L, "__shellos_stop__");
            lua_error(L);   /* unwinds cleanly */
        }
    }
    return 0;
}

/* ── millis() ── */
static int l_millis(lua_State *L)
{
    lua_pushinteger(L, (lua_Integer)(esp_timer_get_time() / 1000));
    return 1;
}

/* ── micros() ── */
static int l_micros(lua_State *L)
{
    lua_pushinteger(L, (lua_Integer)esp_timer_get_time());
    return 1;
}

void lua_api_timer_inject(lua_State *L)
{
    lua_pushcfunction(L, l_delay);  lua_setglobal(L, "delay");
    lua_pushcfunction(L, l_millis); lua_setglobal(L, "millis");
    lua_pushcfunction(L, l_micros); lua_setglobal(L, "micros");
}
