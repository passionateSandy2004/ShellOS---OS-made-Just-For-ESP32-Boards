#ifndef LUA_API_TIMER_H
#define LUA_API_TIMER_H
#include "lua.h"
/* Injects delay(ms) and millis() as Lua globals */
void lua_api_timer_inject(lua_State *L);
#endif
