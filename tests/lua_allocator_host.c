#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "zam_lua_internal.h"
int main(void){zam_lua_memory_t m={.limit=1024};
 void *p=zam_lua_allocator(&m,NULL,99,400);assert(p&&m.live==400&&m.peak==400);
 void *grown=zam_lua_allocator(&m,p,400,800);assert(grown&&m.live==800&&m.peak==800);p=grown;
 assert(!zam_lua_allocator(&m,p,800,1025)&&m.live==800);
 assert(!zam_lua_allocator(&m,p,800,SIZE_MAX)&&m.live==800);
 void *shrunk=zam_lua_allocator(&m,p,800,100);assert(shrunk&&m.live==100&&m.peak==800);p=shrunk;
 assert(!zam_lua_allocator(&m,p,100,0)&&m.live==0);
 p=zam_lua_allocator(&m,NULL,0,1024);assert(p&&m.live==1024);assert(!zam_lua_allocator(&m,NULL,0,1));
 assert(!zam_lua_allocator(&m,p,1024,0)&&m.live==0);
 puts("Lua allocator limits, reallocations and cleanup PASS");return 0;}
