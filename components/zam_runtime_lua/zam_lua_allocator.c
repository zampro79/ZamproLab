#include <stdlib.h>
#include "zam_lua_internal.h"
#ifdef ESP_PLATFORM
#include "sdkconfig.h"
#endif
#if CONFIG_ZAM_LUA_PSRAM_EXPERIMENT
#include <stdatomic.h>
#include "esp_heap_caps.h"
#include "esp_memory_utils.h"
static atomic_uint diagnostic_live, diagnostic_peak, diagnostic_nonexternal;
void zam_lua_psram_diagnostics(unsigned *live, unsigned *peak, unsigned *nonexternal)
{
    *live=atomic_load(&diagnostic_live);
    *peak=atomic_load(&diagnostic_peak);
    *nonexternal=atomic_load(&diagnostic_nonexternal);
}
#endif
void *zam_lua_allocator(void *context, void *pointer, size_t old_size, size_t new_size)
{
    zam_lua_memory_t *memory=context;
    if (!pointer) old_size=0;
    if (old_size>memory->live) return NULL;
    if (!new_size) {
#if CONFIG_ZAM_LUA_PSRAM_EXPERIMENT
        heap_caps_free(pointer);
#else
        free(pointer);
#endif
        memory->live-=old_size;
#if CONFIG_ZAM_LUA_PSRAM_EXPERIMENT
        atomic_store(&diagnostic_live,(unsigned)memory->live);
#endif
        return NULL;
    }
    if (new_size>memory->limit || memory->live-old_size>memory->limit-new_size) return NULL;
#if CONFIG_ZAM_LUA_PSRAM_EXPERIMENT
    void *next=pointer ? heap_caps_realloc(pointer,new_size,MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
                       : heap_caps_malloc(new_size,MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
    void *next=realloc(pointer,new_size);
#endif
    if (next) {
        memory->live=memory->live-old_size+new_size;
        if(memory->live>memory->peak)memory->peak=memory->live;
#if CONFIG_ZAM_LUA_PSRAM_EXPERIMENT
        if (!esp_ptr_external_ram(next)) atomic_fetch_add(&diagnostic_nonexternal,1);
        atomic_store(&diagnostic_live,(unsigned)memory->live);
        atomic_store(&diagnostic_peak,(unsigned)memory->peak);
#endif
    }
    return next;
}
