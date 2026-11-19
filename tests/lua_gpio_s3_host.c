#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
#include "driver/gpio.h"
#include "zam_lua_internal.h"
static int mode[64],level[64],calls,fail_disable;
esp_err_t gpio_set_level(gpio_num_t p,uint32_t v){calls++;level[p]=(int)v;return ESP_OK;}
int gpio_get_level(gpio_num_t p){calls++;return level[p];}
esp_err_t gpio_config(const gpio_config_t *c){int p=__builtin_ctzll(c->pin_bit_mask);calls++;if(c->mode==GPIO_MODE_OUTPUT)assert(level[p]==0);if(c->mode==GPIO_MODE_DISABLE&&mode[p]==GPIO_MODE_OUTPUT)assert(level[p]==0);mode[p]=c->mode;if(c->mode==GPIO_MODE_DISABLE && fail_disable){fail_disable=0;return ESP_FAIL;}return ESP_OK;}
static int run(lua_State *L,const char *code){int rc=luaL_dostring(L,code);if(rc!=LUA_OK)lua_pop(L,1);return rc;}
int main(void){lua_State *L=luaL_newstate();assert(L);luaL_openlibs(L);zam_lua_context_t c={0};*(zam_lua_context_t **)lua_getextraspace(L)=&c;lua_newtable(L);zam_lua_gpio_register(L);lua_setglobal(L,"zam");
 assert(run(L,"zam.gpio.mode(4,'output');zam.gpio.write(4,true);zam.gpio.mode(5,'input');assert(zam.gpio.read(5)==false)")==LUA_OK);assert(level[4]==1);assert(zam_lua_gpio_cleanup(&c));assert(level[4]==0&&mode[4]==GPIO_MODE_DISABLE&&mode[5]==GPIO_MODE_DISABLE);
 assert(run(L,"zam.gpio.mode(4,'output')")==LUA_OK);fail_disable=1;assert(!zam_lua_gpio_cleanup(&c));int old=calls;assert(run(L,"zam.gpio.mode(4,'output')")!=LUA_OK&&calls==old);
 const int forbidden[]={0,3,18,19,20,26,27,32,33,34,35,36,37,43,44,45,46,47,48};for(unsigned i=0;i<sizeof(forbidden)/sizeof(forbidden[0]);i++){char code[100];int old=calls;snprintf(code,sizeof(code),"zam.gpio.mode(%d,'output')",forbidden[i]);assert(run(L,code)!=LUA_OK&&calls==old);}lua_close(L);puts("S3 GPIO host: allowlist, reserved pins and cleanup PASS");}
