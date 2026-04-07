/*
 * ShellOS Lua file API
 * All paths are relative to the package's data directory /root/data/<pkg>/
 * so packages cannot escape their sandbox.
 *
 * file.read(path)               → string | nil
 * file.write(path, text, mode?) → bool  (mode "w"=default, "a"=append)
 * file.exists(path)             → bool
 * file.remove(path)             → bool
 */
#include "lua_api_fs.h"
#include "lua_runtime.h"
#include "lauxlib.h"
#include "lualib.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#define MAX_PATH 256
#define MAX_READ (16 * 1024)  /* 16 KB read limit per call */

/* Build absolute path rooted in the package data dir */
static int build_path(lua_State *L, const char *rel, char *out, size_t outsz)
{
    const char *dd = lua_rt_data_dir(L);
    if (!dd) {
        lua_pushliteral(L, "file API: no data dir");
        return -1;
    }
    if (rel[0] == '/') {  /* absolute paths forbidden */
        lua_pushliteral(L, "file API: absolute paths not allowed");
        return -1;
    }
    /* Reject path traversal */
    if (strstr(rel, "..")) {
        lua_pushliteral(L, "file API: path traversal not allowed");
        return -1;
    }
    int n = snprintf(out, outsz, "%s/%s", dd, rel);
    if (n < 0 || (size_t)n >= outsz) {
        lua_pushliteral(L, "file API: path too long");
        return -1;
    }
    return 0;
}

static int l_file_read(lua_State *L)
{
    const char *rel = luaL_checkstring(L, 1);
    char abs[MAX_PATH];
    if (build_path(L, rel, abs, sizeof(abs)) != 0) return lua_error(L);

    FILE *f = fopen(abs, "rb");
    if (!f) { lua_pushnil(L); return 1; }

    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    if (sz < 0 || sz > MAX_READ) { fclose(f); lua_pushnil(L); return 1; }
    rewind(f);

    char *buf = malloc((size_t)sz + 1);
    if (!buf) { fclose(f); lua_pushnil(L); return 1; }
    size_t got = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    buf[got] = '\0';
    lua_pushlstring(L, buf, got);
    free(buf);
    return 1;
}

static int l_file_write(lua_State *L)
{
    const char *rel  = luaL_checkstring(L, 1);
    size_t dlen;
    const char *data = luaL_checklstring(L, 2, &dlen);
    const char *mode = luaL_optstring(L, 3, "w");

    char abs[MAX_PATH];
    if (build_path(L, rel, abs, sizeof(abs)) != 0) return lua_error(L);

    char fmode[4] = {0};
    if (mode[0] == 'a') { fmode[0]='a'; fmode[1]='b'; }
    else                 { fmode[0]='w'; fmode[1]='b'; }

    FILE *f = fopen(abs, fmode);
    if (!f) { lua_pushboolean(L, 0); return 1; }
    size_t w = fwrite(data, 1, dlen, f);
    fclose(f);
    lua_pushboolean(L, w == dlen);
    return 1;
}

static int l_file_exists(lua_State *L)
{
    const char *rel = luaL_checkstring(L, 1);
    char abs[MAX_PATH];
    if (build_path(L, rel, abs, sizeof(abs)) != 0) return lua_error(L);
    struct stat st;
    lua_pushboolean(L, stat(abs, &st) == 0);
    return 1;
}

static int l_file_remove(lua_State *L)
{
    const char *rel = luaL_checkstring(L, 1);
    char abs[MAX_PATH];
    if (build_path(L, rel, abs, sizeof(abs)) != 0) return lua_error(L);
    lua_pushboolean(L, remove(abs) == 0);
    return 1;
}

static const luaL_Reg fs_funcs[] = {
    {"read",   l_file_read},
    {"write",  l_file_write},
    {"exists", l_file_exists},
    {"remove", l_file_remove},
    {NULL, NULL}
};

int lua_api_fs_open(lua_State *L)
{
    luaL_newlib(L, fs_funcs);
    return 1;
}
