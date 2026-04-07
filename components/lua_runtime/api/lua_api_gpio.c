/*
 * ShellOS Lua GPIO API
 * Exposes: gpio.mode(), gpio.write(), gpio.read(), gpio.analog_read()
 * Constants: gpio.OUTPUT, gpio.INPUT, gpio.HIGH, gpio.LOW
 * Arduino mapping: pinMode → gpio.mode, digitalWrite → gpio.write, etc.
 */
#include "lua_api_gpio.h"
#include "lauxlib.h"
#include "lualib.h"

#include "driver/gpio.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"

static const char *TAG = "lua_gpio";

/* ── gpio.mode(pin, gpio.OUTPUT | gpio.INPUT) ── */
static int l_gpio_mode(lua_State *L)
{
    int pin  = (int)luaL_checkinteger(L, 1);
    int mode = (int)luaL_checkinteger(L, 2);

    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << pin),
        .mode         = (mode == GPIO_MODE_OUTPUT) ? GPIO_MODE_OUTPUT : GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&cfg);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "gpio.mode(%d) error: %d", pin, err);
        lua_pushboolean(L, 0);
    } else {
        lua_pushboolean(L, 1);
    }
    return 1;
}

/* ── gpio.write(pin, gpio.HIGH | gpio.LOW) ── */
static int l_gpio_write(lua_State *L)
{
    int pin   = (int)luaL_checkinteger(L, 1);
    int level = (int)luaL_checkinteger(L, 2);
    gpio_set_level((gpio_num_t)pin, level ? 1 : 0);
    return 0;
}

/* ── gpio.read(pin) → 0 or 1 ── */
static int l_gpio_read(lua_State *L)
{
    int pin = (int)luaL_checkinteger(L, 1);
    lua_pushinteger(L, gpio_get_level((gpio_num_t)pin));
    return 1;
}

/* ── gpio.analog_read(channel) → raw ADC value (0-4095) ── */
static int l_gpio_analog_read(lua_State *L)
{
    int channel = (int)luaL_checkinteger(L, 1);

    adc_oneshot_unit_handle_t adc_handle = NULL;
    adc_oneshot_unit_init_cfg_t init_cfg = { .unit_id = ADC_UNIT_1 };
    if (adc_oneshot_new_unit(&init_cfg, &adc_handle) != ESP_OK) {
        lua_pushinteger(L, -1);
        return 1;
    }

    adc_oneshot_chan_cfg_t chan_cfg = {
        .atten    = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    int raw = 0;
    if (adc_oneshot_config_channel(adc_handle, (adc_channel_t)channel, &chan_cfg) == ESP_OK) {
        adc_oneshot_read(adc_handle, (adc_channel_t)channel, &raw);
    }
    adc_oneshot_del_unit(adc_handle);

    lua_pushinteger(L, raw);
    return 1;
}

static const luaL_Reg gpio_funcs[] = {
    {"mode",         l_gpio_mode},
    {"write",        l_gpio_write},
    {"read",         l_gpio_read},
    {"analog_read",  l_gpio_analog_read},
    {NULL, NULL}
};

int lua_api_gpio_open(lua_State *L)
{
    luaL_newlib(L, gpio_funcs);

    /* Constants */
    lua_pushinteger(L, GPIO_MODE_OUTPUT); lua_setfield(L, -2, "OUTPUT");
    lua_pushinteger(L, GPIO_MODE_INPUT);  lua_setfield(L, -2, "INPUT");
    lua_pushinteger(L, 1);                lua_setfield(L, -2, "HIGH");
    lua_pushinteger(L, 0);                lua_setfield(L, -2, "LOW");

    return 1;
}
