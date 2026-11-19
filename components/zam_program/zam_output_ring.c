#include "zam_output_ring.h"
#include <string.h>

size_t zam_output_begin(zam_output_ring_t *r)
{
    size_t invalidated=r->used;
    r->head=0;r->used=0;r->run_id++;
    return invalidated;
}
size_t zam_output_write(zam_output_ring_t *r,const void *data,size_t length)
{
    const unsigned char *src=data;
    uint64_t remaining=UINT64_MAX-r->next;
    size_t accepted=length;
    if(remaining<(uint64_t)accepted)accepted=(size_t)remaining;
    size_t rejected=length-accepted;
    if(rejected)r->exhausted=true;
    size_t displaced=accepted>ZAM_OUTPUT_CAPACITY-r->used?accepted-(ZAM_OUTPUT_CAPACITY-r->used):0;
    if(accepted>=ZAM_OUTPUT_CAPACITY){
        memcpy(r->bytes,src+accepted-ZAM_OUTPUT_CAPACITY,ZAM_OUTPUT_CAPACITY);
        r->head=0;r->used=ZAM_OUTPUT_CAPACITY;
    }else if(accepted){
        size_t drop=displaced;r->head=(r->head+drop)%ZAM_OUTPUT_CAPACITY;r->used-=drop;
        size_t tail=(r->head+r->used)%ZAM_OUTPUT_CAPACITY;
        size_t first=accepted<ZAM_OUTPUT_CAPACITY-tail?accepted:ZAM_OUTPUT_CAPACITY-tail;
        memcpy(r->bytes+tail,src,first);if(accepted>first)memcpy(r->bytes,src+first,accepted-first);
        r->used+=accepted;
    }
    r->next+=(uint64_t)accepted;
    return SIZE_MAX-displaced<rejected?SIZE_MAX:displaced+rejected;
}
bool zam_output_copy(const zam_output_ring_t *r,uint64_t cursor,void *destination,size_t capacity,zam_output_snapshot_t *out)
{
    if(cursor>r->next)return false;
    uint64_t oldest=r->next-r->used,start=cursor<oldest?oldest:cursor;
    size_t n=(size_t)(r->next-start);if(n>capacity)n=capacity;
    size_t pos=(r->head+(size_t)(start-oldest))%ZAM_OUTPUT_CAPACITY;
    size_t first=n<ZAM_OUTPUT_CAPACITY-pos?n:ZAM_OUTPUT_CAPACITY-pos;
    if(first)memcpy(destination,r->bytes+pos,first);
    if(n>first)memcpy((char *)destination+first,r->bytes,n-first);
    *out=(zam_output_snapshot_t){.run_id=r->run_id,.oldest=oldest,.end=r->next,.next=start+n,.lost=start-cursor,.copied=n,.exhausted=r->exhausted};
    return true;
}
