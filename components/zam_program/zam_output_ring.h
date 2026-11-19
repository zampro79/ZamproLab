#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define ZAM_OUTPUT_CAPACITY 2048
/* All operations are called under the Program Manager lock. */
typedef struct {
    unsigned char bytes[ZAM_OUTPUT_CAPACITY];
    size_t head, used;
    uint64_t next, run_id;
    bool exhausted;
} zam_output_ring_t;
typedef struct {
    uint64_t run_id, oldest, end, next, lost;
    size_t copied;
    bool exhausted;
} zam_output_snapshot_t;
size_t zam_output_begin(zam_output_ring_t *ring);
size_t zam_output_write(zam_output_ring_t *ring,const void *data,size_t length);
bool zam_output_copy(const zam_output_ring_t *ring,uint64_t cursor,void *destination,size_t capacity,zam_output_snapshot_t *result);
