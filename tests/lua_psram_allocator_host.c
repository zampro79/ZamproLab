#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_heap_caps.h"
#include "zam_lua_internal.h"
static int fail_next, allocations, reallocations, frees;
void *heap_caps_malloc(size_t n,unsigned caps) { assert(caps==(MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT)); allocations++; if(fail_next){fail_next=0;return NULL;} return malloc(n); }
void *heap_caps_realloc(void *p,size_t n,unsigned caps) { assert(caps==(MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT)); reallocations++; if(fail_next){fail_next=0;return NULL;} return realloc(p,n); }
void heap_caps_free(void *p) { if(p)frees++; free(p); }
bool esp_ptr_external_ram(const void *p) { return p!=NULL; }
int main(void) {
    zam_lua_memory_t m={.limit=1024}; unsigned live,peak,foreign;
    fail_next=1;assert(!zam_lua_allocator(&m,NULL,0,40));assert(m.live==0);
    unsigned char *p=zam_lua_allocator(&m,NULL,0,400);assert(p);memset(p,0x5a,400);
    fail_next=1;assert(!zam_lua_allocator(&m,p,400,800));assert(m.live==400&&m.peak==400&&p[0]==0x5a&&p[399]==0x5a);
    unsigned char *q=zam_lua_allocator(&m,p,400,800);assert(q&&q[0]==0x5a&&q[399]==0x5a);p=q;
    assert(!zam_lua_allocator(&m,p,800,1025));assert(!zam_lua_allocator(&m,p,800,SIZE_MAX));assert(m.live==800);
    q=zam_lua_allocator(&m,p,800,100);assert(q&&q[0]==0x5a&&m.live==100&&m.peak==800);p=q;
    zam_lua_psram_diagnostics(&live,&peak,&foreign);assert(live==100&&peak==800&&foreign==0);
    assert(!zam_lua_allocator(&m,p,100,0)&&m.live==0);zam_lua_psram_diagnostics(&live,&peak,&foreign);assert(!live&&peak==800&&!foreign);
    assert(allocations==2&&reallocations==3&&frees==1);puts("PSRAM allocator paths and failure accounting PASS");
}
