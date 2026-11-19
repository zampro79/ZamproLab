#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include "esp_heap_caps.h"

typedef union block block;
union block { struct { block *next, *prev; } links; max_align_t align; };
static block *head;
size_t psram_mock_live_blocks, psram_mock_allocations;
static block *find(const void *p) {
    for (block *b=head;b;b=b->links.next) if ((void *)(b+1)==p) return b;
    return NULL;
}
void *heap_caps_malloc(size_t n,unsigned caps) {
    assert(caps==(MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
    block *b=malloc(sizeof(*b)+n); if (!b) return NULL;
    b->links.prev=NULL; b->links.next=head; if (head) head->links.prev=b; head=b;
    psram_mock_live_blocks++; psram_mock_allocations++; return b+1;
}
void *heap_caps_realloc(void *p,size_t n,unsigned caps) {
    assert(caps==(MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
    block *b=find(p); assert(b); block *prev=b->links.prev,*next=b->links.next;
    block *replacement=realloc(b,sizeof(*b)+n); if (!replacement) return NULL;
    replacement->links.prev=prev; replacement->links.next=next;
    if (prev) prev->links.next=replacement; else head=replacement;
    if (next) next->links.prev=replacement;
    psram_mock_allocations++; return replacement+1;
}
void heap_caps_free(void *p) {
    if (!p) return;
    block *b=find(p); assert(b); if (b->links.prev) b->links.prev->links.next=b->links.next; else head=b->links.next;
    if (b->links.next) b->links.next->links.prev=b->links.prev;
    assert(psram_mock_live_blocks); psram_mock_live_blocks--; free(b);
}
bool esp_ptr_external_ram(const void *p) { return find(p)!=NULL; }
