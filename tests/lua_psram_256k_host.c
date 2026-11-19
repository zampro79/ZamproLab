#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "lauxlib.h"
#include "lualib.h"
#include "zam_lua_internal.h"

int main(void)
{
    zam_lua_memory_t memory={.limit=262144};
    unsigned char *p=zam_lua_allocator(&memory,NULL,0,140000);
    assert(p && memory.live==140000);memset(p,0x5a,140000);
    unsigned char *q=zam_lua_allocator(&memory,p,140000,262144);
    assert(q && q[0]==0x5a && q[139999]==0x5a && memory.live==262144);p=q;
    assert(!zam_lua_allocator(&memory,p,262144,262145));
    assert(memory.live==262144 && p[0]==0x5a);
    q=zam_lua_allocator(&memory,p,262144,64000);
    assert(q && q[0]==0x5a && memory.live==64000);p=q;
    assert(!zam_lua_allocator(&memory,p,64000,0) && memory.live==0);

    zam_lua_memory_t vm={.limit=262144};
    lua_State *L=lua_newstate(zam_lua_allocator,&vm,0);assert(L);
    luaL_openlibs(L);
    assert(luaL_dostring(L,"keep={} for i=1,200 do keep[i]=string.rep('x',700) end")==LUA_OK);
    assert(vm.live>131072 && vm.live<vm.limit);
    size_t before=vm.live;
    assert(luaL_loadstring(L,"while true do keep[#keep+1]=string.rep('y',2048) end")==LUA_OK);
    int result=lua_pcall(L,0,0,0);
    assert(result==LUA_ERRMEM && vm.peak<=vm.limit && vm.live<=vm.limit && vm.peak>before && vm.peak>240000);
    lua_settop(L,0);lua_pushnil(L);lua_setglobal(L,"keep");lua_gc(L,LUA_GCCOLLECT);
    assert(luaL_dostring(L,"assert(6*7 == 42)")==LUA_OK);
    lua_close(L);assert(vm.live==0);
    printf("256 KiB: exact limit, realloc, over-limit, Lua >128 KiB, OOM and recovery PASS; peak=%zu\n",vm.peak);
    return 0;
}
