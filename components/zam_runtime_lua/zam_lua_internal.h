#pragma once
#include <stddef.h>
#include "lua.h"
#include "zam_runtime_lua.h"
typedef struct { size_t live, peak, limit; } zam_lua_memory_t;
#define ZAM_GPIO_TRACKED_PINS 64
enum { ZAM_GPIO_FREE, ZAM_GPIO_INPUT, ZAM_GPIO_OUTPUT, ZAM_GPIO_I2C };
typedef struct { const zam_lua_callbacks_t *callbacks; unsigned hook_ticks; bool gpio_closing; unsigned char gpio_modes[ZAM_GPIO_TRACKED_PINS]; void *i2c_bus, *i2c_device; int i2c_sda, i2c_scl; unsigned i2c_frequency; } zam_lua_context_t;
bool zam_lua_gpio_allowed_output(int pin);
bool zam_lua_gpio_quarantined(int pin);
void zam_lua_gpio_quarantine(int pin);
void zam_lua_i2c_register(lua_State *L);
bool zam_lua_i2c_cleanup(zam_lua_context_t *context);
void zam_lua_gpio_register(lua_State *L);
bool zam_lua_gpio_cleanup(zam_lua_context_t *context);
void *zam_lua_allocator(void *context, void *pointer, size_t old_size, size_t new_size);
int zam_lua_open_bindings(lua_State *L);
void zam_lua_modules_register(lua_State *L);
int zam_lua_stop_error(lua_State *L);
bool zam_lua_is_stop_error(lua_State *L);
void zam_lua_hook(lua_State *L, lua_Debug *ar);
