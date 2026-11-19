#pragma once
#include <stddef.h>
#define MALLOC_CAP_INTERNAL 1
#define MALLOC_CAP_8BIT 2
static inline size_t heap_caps_get_free_size(int flags){(void)flags;return 100000;}
static inline size_t heap_caps_get_minimum_free_size(int flags){(void)flags;return 90000;}
static inline size_t heap_caps_get_largest_free_block(int flags){(void)flags;return 80000;}
