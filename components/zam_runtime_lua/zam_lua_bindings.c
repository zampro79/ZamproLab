#include <stdio.h>
#include <stdint.h>
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lauxlib.h"
#include "lualib.h"
#include "zam_lua_internal.h"
#include "zam_rgb.h"
#define SLEEP_MAX_MS 60000
static zam_lua_context_t *context(lua_State *L) { return *(zam_lua_context_t **)lua_getextraspace(L); }
/* Private, non-string identity prevents user errors or a replacement error
 * during unwinding from being mistaken for a cooperative interruption. */
static char stop_token;
int zam_lua_stop_error(lua_State *L)
{
    lua_pushlightuserdata(L,&stop_token);
    return lua_error(L);
}
bool zam_lua_is_stop_error(lua_State *L)
{
    return lua_type(L,-1)==LUA_TLIGHTUSERDATA && lua_touserdata(L,-1)==&stop_token;
}
void zam_lua_hook(lua_State *L,lua_Debug *ar)
{
    (void)ar;zam_lua_context_t *c=context(L);
    if(c->callbacks->stop_requested(c->callbacks->context))zam_lua_stop_error(L);
    if((++c->hook_ticks & 15u)==0)vTaskDelay(1);
}
static int native_sleep(lua_State *L)
{
    lua_Integer ms=luaL_checkinteger(L,1);
    luaL_argcheck(L,ms>=0 && ms<=SLEEP_MAX_MS,1,"milliseconds out of range");
    int64_t end=esp_timer_get_time()+(int64_t)ms*1000;
    do {
        if(context(L)->callbacks->stop_requested(context(L)->callbacks->context))return zam_lua_stop_error(L);
        int64_t remain=(end-esp_timer_get_time()+999)/1000;
        if(remain<=0)break;
        uint32_t slice=(uint32_t)(remain>25?25:remain);
        vTaskDelay(pdMS_TO_TICKS(slice)>0?pdMS_TO_TICKS(slice):1);
    } while(true);
    return 0;
}
static int native_print(lua_State *L)
{
    const zam_lua_callbacks_t *cb=context(L)->callbacks;
    int n=lua_gettop(L);
    for(int i=1;i<=n;i++){
        size_t length;const char *text=luaL_tolstring(L,i,&length);
        if(i>1)cb->output(cb->context,"\t",1);
        cb->output(cb->context,text,length);lua_pop(L,1);
    }
    cb->output(cb->context,"\n",1);return 0;
}
static int rgb_set(lua_State *L)
{
    if(lua_gettop(L)!=3)return luaL_error(L,"expected three RGB values");
    int v[3];for(int i=0;i<3;i++){if(!lua_isinteger(L,i+1))return luaL_argerror(L,i+1,"integer required");lua_Integer n=lua_tointeger(L,i+1);luaL_argcheck(L,n>=0 && n<=255,i+1,"value out of range");v[i]=(int)n;}
    if(zam_rgb_set(v[0],v[1],v[2])!=ESP_OK)return luaL_error(L,"RGB unavailable");
    return 0;
}
int zam_lua_open_bindings(lua_State *L)
{
    luaL_requiref(L,"_G",luaopen_base,1);lua_pop(L,1);
    luaL_requiref(L,LUA_TABLIBNAME,luaopen_table,1);lua_pop(L,1);
    luaL_requiref(L,LUA_STRLIBNAME,luaopen_string,1);lua_pop(L,1);
    luaL_requiref(L,LUA_MATHLIBNAME,luaopen_math,1);lua_pop(L,1);
    luaL_requiref(L,LUA_UTF8LIBNAME,luaopen_utf8,1);lua_pop(L,1);
    const char *const disabled[]={"dofile","loadfile","load","pcall","xpcall","collectgarbage"};
    for(size_t i=0;i<sizeof(disabled)/sizeof(disabled[0]);i++){lua_pushnil(L);lua_setglobal(L,disabled[i]);}
    zam_lua_modules_register(L);
    lua_pushcfunction(L,native_print);lua_setglobal(L,"print");
    lua_newtable(L);lua_pushcfunction(L,native_sleep);lua_setfield(L,-2,"sleep");zam_lua_gpio_register(L);zam_lua_i2c_register(L);if(zam_rgb_available()){lua_newtable(L);lua_pushcfunction(L,rgb_set);lua_setfield(L,-2,"set");lua_setfield(L,-2,"rgb");}lua_setglobal(L,"zam");
    return 0;
}
