#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "lauxlib.h"
#include "sdkconfig.h"
#include "zam_files.h"
#include "zam_lua_internal.h"

typedef struct { int fd; size_t remaining; bool failed, stopped; char buffer[256]; } module_reader_t;

static const char *module_reader(lua_State *L, void *data, size_t *size)
{
    module_reader_t *reader=data;
    zam_lua_context_t *context=*(zam_lua_context_t **)lua_getextraspace(L);
    if(context->callbacks->stop_requested(context->callbacks->context)) { reader->stopped=true;*size=0;return NULL; }
    if(!reader->remaining) {*size=0;return NULL;}
    size_t want=reader->remaining<sizeof(reader->buffer)?reader->remaining:sizeof(reader->buffer);
    ssize_t got=read(reader->fd,reader->buffer,want);
    if(got<=0) {reader->failed=true;*size=0;return NULL;}
    reader->remaining-=(size_t)got;*size=(size_t)got;return reader->buffer;
}

/* Called protected: even a Lua allocation failure must release fd and lock. */
static int compile_module(lua_State *L)
{
    module_reader_t *reader=lua_touserdata(L,1);
    const char *path=lua_tostring(L,2);
    int status=lua_load(L,module_reader,reader,path,"t");
    if(status!=LUA_OK)return lua_error(L);
    return 1;
}

static int load_module(lua_State *L)
{
    size_t len;const char *name=luaL_checklstring(L,1,&len);
    char path[ZAM_FILES_PATH_MAX+1];path[0]='/';
    for(size_t i=0;i<len;i++)path[i+1]=name[i]=='.'?'/':name[i];
    memcpy(path+len+1,".lua",5);
    /* Push all potentially allocating Lua values before acquiring the lock. */
    lua_pushcfunction(L,compile_module);
    module_reader_t reader={.fd=-1};lua_pushlightuserdata(L,&reader);
    lua_pushfstring(L,"@%s",path);
    if(zam_files_lock()!=0)return luaL_error(L,"module '%s': UserFS busy",name);
    struct stat st;reader.fd=zam_files_open_read(path,&st);
    if(reader.fd<0) {zam_files_unlock();return luaL_error(L,"module '%s' not found in UserFS",name);}
    if(st.st_size<0 || st.st_size>CONFIG_ZAM_LUA_SOURCE_LIMIT) {
        close(reader.fd);zam_files_unlock();return luaL_error(L,"module '%s' exceeds source limit",name);
    }
    reader.remaining=(size_t)st.st_size;
    int status=lua_pcall(L,2,1,0);
    close(reader.fd);zam_files_unlock();
    /* Preserve a real allocation failure instead of treating it as a stop. */
    if(status==LUA_ERRMEM)return lua_error(L);
    if(reader.stopped)return zam_lua_stop_error(L);
    if(reader.failed)return luaL_error(L,"module '%s': read failed",name);
    if(status!=LUA_OK)return lua_error(L);
    if(reader.remaining)return luaL_error(L,"module '%s': read incomplete",name);
    lua_pushvalue(L,1);lua_pushstring(L,path);lua_call(L,2,1);
    lua_pushstring(L,path);
    return 2;
}

static int require_userfs(lua_State *L)
{
    size_t len;const char *name=luaL_checklstring(L,1,&len);
    if(!len || len+5>ZAM_FILES_PATH_MAX)return luaL_error(L,"invalid module name");
    bool first=true;
    for(size_t i=0;i<len;i++) {
        unsigned char c=(unsigned char)name[i];
        if(c=='.') {if(first)return luaL_error(L,"invalid module name");first=true;continue;}
        bool letter=(c>='a'&&c<='z')||(c>='A'&&c<='Z')||c=='_';
        if(!(letter || (!first && c>='0'&&c<='9')))return luaL_error(L,"invalid module name");
        first=false;
    }
    if(first)return luaL_error(L,"invalid module name");
    lua_getfield(L,LUA_REGISTRYINDEX,LUA_LOADED_TABLE);
    lua_getfield(L,-1,name);
    if(lua_toboolean(L,-1))return 1;
    lua_pop(L,1);
    lua_getfield(L,lua_upvalueindex(1),name);
    if(lua_toboolean(L,-1))return luaL_error(L,"circular module dependency: %s",name);
    lua_pop(L,1);lua_pushboolean(L,1);lua_setfield(L,lua_upvalueindex(1),name);
    lua_pushcfunction(L,load_module);lua_pushvalue(L,1);
    int status=lua_pcall(L,1,2,0);
    lua_pushnil(L);lua_setfield(L,lua_upvalueindex(1),name);
    if(status!=LUA_OK)return lua_error(L);
    if(lua_isnil(L,-2)) {lua_pushboolean(L,1);lua_replace(L,-3);}
    lua_pushvalue(L,-2);lua_setfield(L,-4,name);
    return 2;
}

void zam_lua_modules_register(lua_State *L)
{
    lua_newtable(L);lua_pushcclosure(L,require_userfs,1);lua_setglobal(L,"require");
}
