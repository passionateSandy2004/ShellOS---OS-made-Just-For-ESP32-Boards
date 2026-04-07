# ShellOS - single IDF component
# Includes: shell, fs, drivers, pkg_manager, http_upload, Lua 5.4 runtime + API
# Run  python tools/get_lua.py  once before the first build to fetch Lua source.

if(NOT EXISTS "${CMAKE_CURRENT_LIST_DIR}/lua_runtime/lua/lvm.c")
    message(FATAL_ERROR
        "\n[ShellOS] Lua 5.4 source not found in components/lua_runtime/lua/\n"
        "Run:  python tools/get_lua.py\n"
        "Then rebuild.\n")
endif()

# Lua 5.4 core sources
set(LUA_SRCS
    lua_runtime/lua/lapi.c
    lua_runtime/lua/lcode.c
    lua_runtime/lua/lctype.c
    lua_runtime/lua/ldebug.c
    lua_runtime/lua/ldo.c
    lua_runtime/lua/ldump.c
    lua_runtime/lua/lfunc.c
    lua_runtime/lua/lgc.c
    lua_runtime/lua/linit.c
    lua_runtime/lua/llex.c
    lua_runtime/lua/lmem.c
    lua_runtime/lua/lobject.c
    lua_runtime/lua/lopcodes.c
    lua_runtime/lua/lparser.c
    lua_runtime/lua/lstate.c
    lua_runtime/lua/lstring.c
    lua_runtime/lua/ltable.c
    lua_runtime/lua/ltm.c
    lua_runtime/lua/lundump.c
    lua_runtime/lua/lvm.c
    lua_runtime/lua/lzio.c
    lua_runtime/lua/lauxlib.c
    lua_runtime/lua/lbaselib.c
    lua_runtime/lua/lcorolib.c
    lua_runtime/lua/lmathlib.c
    lua_runtime/lua/lstrlib.c
    lua_runtime/lua/ltablib.c
    lua_runtime/lua/lutf8lib.c
)

# ShellOS Lua runtime + Arduino API bindings
set(LUA_RT_SRCS
    lua_runtime/lua_runtime.c
    lua_runtime/api/lua_api_gpio.c
    lua_runtime/api/lua_api_timer.c
    lua_runtime/api/lua_api_serial.c
    lua_runtime/api/lua_api_fs.c
    lua_runtime/api/lua_api_log.c
    lua_runtime/api/lua_api_http.c
)

# esp32-camera supports ESP32 / S2 / S3 only — omit on esp32c6 et al.
set(SHALLOS_CAM_SRC "drivers/cam_driver_stub.c")
set(SHALLOS_REQUIRES "")
if("${IDF_TARGET}" STREQUAL "esp32")
    set(SHALLOS_CAM_SRC "drivers/cam_driver.c")
    list(APPEND SHALLOS_REQUIRES "espressif__esp32-camera")
endif()

set(COMPONENT_REQUIRES
    driver
    esp_adc
    esp_system
    esp_timer
    esp_wifi
    esp_netif
    esp_event
    joltwallet__littlefs
    vfs
    heap
    freertos
    spi_flash
    nvs_flash
    log
    lwip
    esp_http_client
    esp_http_server
    mbedtls
)
if(SHALLOS_REQUIRES)
    list(APPEND COMPONENT_REQUIRES ${SHALLOS_REQUIRES})
endif()

if("${IDF_TARGET}" STREQUAL "esp32c6")
    list(APPEND COMPONENT_REQUIRES esp_driver_usb_serial_jtag)
endif()

idf_component_register(
    SRCS
        "shell/kernel.c"
        "shell/shell.c"
        "shell/shell_io.c"
        "shell/netsh_server.c"
        "shell/http_fetch.c"
        "shell/http_upload.c"
        "shell/commands.c"
        "fs/shell_fs.c"
        "drivers/uart_driver.c"
        "drivers/wifi_driver.c"
        "${SHALLOS_CAM_SRC}"
        "pkg_manager/pkg_manager.c"
        "pkg_manager/shpkg.c"
        ${LUA_SRCS}
        ${LUA_RT_SRCS}
    INCLUDE_DIRS
        "shell"
        "drivers"
        "fs"
        "pkg_manager"
        "lua_runtime"
        "lua_runtime/lua"
        "lua_runtime/api"
    REQUIRES
        ${COMPONENT_REQUIRES}
)

# Suppress Lua's benign C warnings. -Wno-macro-redefined silences luaconf.h
# internal guards; the other flags suppress Lua's common C idioms.
set_source_files_properties(${LUA_SRCS} PROPERTIES
    COMPILE_FLAGS "-Wno-all -Wno-extra -Wno-implicit-fallthrough -Wno-unused-result -Wno-macro-redefined"
)
