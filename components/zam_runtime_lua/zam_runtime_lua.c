#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "lauxlib.h"
#include "sdkconfig.h"
#include "zam_files.h"
#include "zam_lua_internal.h"
#define HOOK_COUNT 1000
/* Copy while holding the file manager lock, then execute without a file handle. */
static int snapshot(const char *path,char **source,size_t *length)
{
    if(zam_files_lock())return -1;
    struct stat st;int fd=zam_files_open_read(path,&st);int result=-1;char *data=NULL;
    if(fd<0)goto out;
    if(st.st_size<0 || st.st_size>CONFIG_ZAM_LUA_SOURCE_LIMIT){errno=EFBIG;goto close_file;}
    data=malloc((size_t)st.st_size+1);if(!data){errno=ENOMEM;goto close_file;}
    size_t count=0;
    while(count<(size_t)st.st_size){
        size_t remaining=(size_t)st.st_size-count;
        ssize_t n=read(fd,data+count,remaining);if(n<=0){errno=EIO;goto close_file;}count+=(size_t)n;
    }
    data[count]=0;*source=data;*length=count;data=NULL;result=0;
close_file:free(data);close(fd);
out:zam_files_unlock();return result;
}
zam_lua_result_t zam_runtime_lua_execute(const char *path,const zam_lua_callbacks_t *cb,zam_lua_metrics_t *m)
{
    if(!path||!cb||!cb->stop_requested||!cb->running||!cb->cleanup||!cb->output||!m)return ZAM_LUA_INTERNAL;
    memset(m,0,sizeof(*m));
    int64_t start=esp_timer_get_time(),loaded=start,ended=start;
    m->heap_before=heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
    zam_lua_memory_t memory={.limit=CONFIG_ZAM_LUA_HEAP_LIMIT};
    zam_lua_context_t context={.callbacks=cb};
    char *source=NULL;size_t size=0;lua_State *L=NULL;zam_lua_result_t result=ZAM_LUA_INTERNAL;
    if(snapshot(path,&source,&size)!=0){result=ZAM_LUA_LOAD;goto done;}
    loaded=esp_timer_get_time();
    if(cb->stop_requested(cb->context)){result=ZAM_LUA_STOPPED;goto done;}
    L=lua_newstate(zam_lua_allocator,&memory,0);if(!L){result=ZAM_LUA_MEMORY;goto done;}
    *(zam_lua_context_t **)lua_getextraspace(L)=&context;
    lua_pushcfunction(L,zam_lua_open_bindings);
    int code=lua_pcall(L,0,0,0);
    if(code==LUA_OK){lua_sethook(L,zam_lua_hook,LUA_MASKCOUNT,HOOK_COUNT);code=luaL_loadbufferx(L,source,size,path,"t");}
    free(source);source=NULL;
    if(code==LUA_OK && !cb->stop_requested(cb->context)){
        cb->running(cb->context);
        if(!cb->stop_requested(cb->context))code=lua_pcall(L,0,0,0);
    }
    ended=esp_timer_get_time();
    /* Recognize only the private stop object. A different error, including
     * one that replaces STOP during unwinding, is conservatively preserved. */
    if(code==LUA_ERRMEM)result=ZAM_LUA_MEMORY;
    else if(code==LUA_ERRSYNTAX)result=ZAM_LUA_SYNTAX;
    else if(code!=LUA_OK)result=zam_lua_is_stop_error(L)?ZAM_LUA_STOPPED:ZAM_LUA_RUNTIME;
    else if(cb->stop_requested(cb->context))result=ZAM_LUA_STOPPED;
    else result=ZAM_LUA_OK;
    if(code!=LUA_OK && result!=ZAM_LUA_STOPPED){
        const char *message=lua_type(L,-1)==LUA_TSTRING ? lua_tostring(L,-1) : NULL;
        if(message){cb->output(cb->context,message,strlen(message));cb->output(cb->context,"\n",1);}
    }
done:
    cb->cleanup(cb->context);
    /* Lua finalizers may run in lua_close: prohibit reacquisition after release. */
    context.gpio_closing=true;
    if(!zam_lua_i2c_cleanup(&context)){
        static const char warning[]="Cleanup I2C fallito\n";
        cb->output(cb->context,warning,sizeof(warning)-1);result=ZAM_LUA_INTERNAL;
    }
    if(!zam_lua_gpio_cleanup(&context)){
        static const char warning[]="Cleanup GPIO fallito\n";
        cb->output(cb->context,warning,sizeof(warning)-1);result=ZAM_LUA_INTERNAL;
    }
    if(ended==start)ended=esp_timer_get_time();
    free(source);if(L){lua_sethook(L,NULL,0,0);lua_close(L);}
    int64_t finished=esp_timer_get_time();m->lua_peak=memory.peak;m->lua_live=memory.live;
    m->heap_after=heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
    m->heap_minimum=heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
    m->heap_largest=heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
    m->load_us=loaded-start;m->run_us=ended-loaded;m->cleanup_us=finished-ended;
    return result;
}
