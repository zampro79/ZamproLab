#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "driver/gpio.h"
#include "lauxlib.h"
#include "sdkconfig.h"
#include "zam_lua_internal.h"

/* A failed cleanup quarantines that pin until reboot; only the single VM task accesses it. */
static uint64_t quarantined_gpio;

/* A SoC allowlist is not a board pinout: the user must verify the module and wiring. */
static bool allowed(int pin)
{
#if CONFIG_IDF_TARGET_ESP32
    switch(pin){case 18:case 19:case 21:case 22:case 23:case 25:case 26:case 27:case 32:case 33:case 34:case 35:case 36:case 39:return true;default:return false;}
#elif CONFIG_IDF_TARGET_ESP32S3
    switch(pin){case 4:case 5:return true;default:return false;}
#else
    (void)pin;return false;
#endif
}
static bool output_capable(int pin)
{
#if CONFIG_IDF_TARGET_ESP32
    return pin<34;
#else
    (void)pin;return true;
#endif
}
bool zam_lua_gpio_allowed_output(int pin){return pin>=0 && pin<ZAM_GPIO_TRACKED_PINS && allowed(pin) && output_capable(pin);}
bool zam_lua_gpio_quarantined(int pin){return (quarantined_gpio & (UINT64_C(1)<<(unsigned)pin))!=0;}
void zam_lua_gpio_quarantine(int pin){quarantined_gpio |= UINT64_C(1)<<(unsigned)pin;}
static zam_lua_context_t *context(lua_State *L){return *(zam_lua_context_t **)lua_getextraspace(L);}
static int pin_arg(lua_State *L,int count)
{
    if(context(L)->gpio_closing)luaL_error(L,"GPIO non disponibile durante cleanup");
    if(lua_gettop(L)!=count)luaL_error(L,"numero argomenti GPIO non valido");
    if(!lua_isinteger(L,1))luaL_argerror(L,1,"pin GPIO deve essere un intero");
    lua_Integer pin=lua_tointeger(L,1);
    if(pin<0 || pin>=ZAM_GPIO_TRACKED_PINS || !allowed((int)pin))luaL_argerror(L,1,"pin GPIO non consentito su questo target");
    if(quarantined_gpio & (UINT64_C(1)<<(int)pin))luaL_error(L,"GPIO %d non disponibile dopo errore cleanup",(int)pin);
    return (int)pin;
}
static int set_mode(lua_State *L)
{
    int pin=pin_arg(L,2);if(context(L)->gpio_modes[pin]==ZAM_GPIO_I2C)return luaL_error(L,"GPIO%d gia utilizzato da I2C",pin);size_t length=0;
    if(lua_type(L,2)!=LUA_TSTRING)return luaL_argerror(L,2,"modalita GPIO deve essere input o output");
    const char *mode=lua_tolstring(L,2,&length);bool input=length==5&&!memcmp(mode,"input",5),output=length==6&&!memcmp(mode,"output",6);
    if(!input&&!output)return luaL_argerror(L,2,"modalita GPIO deve essere input o output");
    if(output&&!output_capable(pin))return luaL_argerror(L,1,"pin GPIO supporta solo input");
    zam_lua_context_t *c=context(L);
    /* An acquired output is driven LOW before changing its mode. */
    if(c->gpio_modes[pin]==ZAM_GPIO_OUTPUT && gpio_set_level(pin,0)!=ESP_OK)return luaL_error(L,"GPIO %d: impossibile impostare LOW",pin);
    /* Claim before first driver call so partial failures are always cleaned up. */
    c->gpio_modes[pin]=output?ZAM_GPIO_OUTPUT:ZAM_GPIO_INPUT;
    if(output && gpio_set_level(pin,0)!=ESP_OK)return luaL_error(L,"GPIO %d: inizializzazione LOW fallita",pin);
    gpio_config_t config={.pin_bit_mask=UINT64_C(1)<<pin,.mode=output?GPIO_MODE_OUTPUT:GPIO_MODE_INPUT,
        .pull_up_en=GPIO_PULLUP_DISABLE,.pull_down_en=GPIO_PULLDOWN_DISABLE,.intr_type=GPIO_INTR_DISABLE};
    if(gpio_config(&config)!=ESP_OK)return luaL_error(L,"GPIO %d: configurazione fallita",pin);
    return 0;
}
static int write_pin(lua_State *L)
{
    int pin=pin_arg(L,2);
    if(lua_type(L,2)!=LUA_TBOOLEAN)return luaL_argerror(L,2,"valore GPIO deve essere booleano");
    if(context(L)->gpio_modes[pin]!=ZAM_GPIO_OUTPUT)return luaL_error(L,"GPIO %d non configurato come output",pin);
    if(gpio_set_level(pin,(uint32_t)lua_toboolean(L,2))!=ESP_OK)return luaL_error(L,"GPIO %d: scrittura fallita",pin);
    return 0;
}
static int read_pin(lua_State *L)
{
    int pin=pin_arg(L,1);
    if(context(L)->gpio_modes[pin]!=ZAM_GPIO_INPUT)return luaL_error(L,"GPIO %d non configurato come input",pin);
    lua_pushboolean(L,gpio_get_level(pin));return 1;
}
void zam_lua_gpio_register(lua_State *L)
{
    lua_newtable(L);lua_pushcfunction(L,set_mode);lua_setfield(L,-2,"mode");lua_pushcfunction(L,write_pin);lua_setfield(L,-2,"write");lua_pushcfunction(L,read_pin);lua_setfield(L,-2,"read");lua_setfield(L,-2,"gpio");
}
bool zam_lua_gpio_cleanup(zam_lua_context_t *context)
{
    bool ok=true;
    for(int pin=0;pin<ZAM_GPIO_TRACKED_PINS;pin++){
        if(context->gpio_modes[pin]==ZAM_GPIO_FREE || context->gpio_modes[pin]==ZAM_GPIO_I2C)continue;
        bool pin_ok=true;
        if(context->gpio_modes[pin]==ZAM_GPIO_OUTPUT && gpio_set_level(pin,0)!=ESP_OK)pin_ok=false;
        gpio_config_t config={.pin_bit_mask=UINT64_C(1)<<pin,.mode=GPIO_MODE_DISABLE,
            .pull_up_en=GPIO_PULLUP_DISABLE,.pull_down_en=GPIO_PULLDOWN_DISABLE,.intr_type=GPIO_INTR_DISABLE};
        if(gpio_config(&config)!=ESP_OK)pin_ok=false;
        if(!pin_ok){quarantined_gpio|=UINT64_C(1)<<pin;ok=false;}
        context->gpio_modes[pin]=ZAM_GPIO_FREE;
    }
    return ok;
}
