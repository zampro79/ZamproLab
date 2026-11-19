#pragma once
#include <stddef.h>
#define MALLOC_CAP_SPIRAM 0x1
#define MALLOC_CAP_8BIT 0x2
#define MALLOC_CAP_INTERNAL 0x4
static inline size_t heap_caps_get_free_size(int flags){(void)flags;return 100000;}
static inline size_t heap_caps_get_minimum_free_size(int flags){(void)flags;return 90000;}
static inline size_t heap_caps_get_largest_free_block(int flags){(void)flags;return 80000;}
void *heap_caps_malloc(size_t size, unsigned caps);
void *heap_caps_realloc(void *ptr, size_t size, unsigned caps);
void heap_caps_free(void *ptr);
