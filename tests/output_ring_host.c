#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "zam_output_ring.h"
static zam_output_ring_t r;
static zam_output_snapshot_t read_at(uint64_t cursor, size_t cap, char *buf){zam_output_snapshot_t out;assert(zam_output_copy(&r,cursor,buf,cap,&out));return out;}
int main(void){
 char in[7000],out[2050],again[2050];for(size_t i=0;i<sizeof(in);i++)in[i]=(char)('A'+i%26);
 zam_output_snapshot_t a=read_at(0,sizeof(out),out);assert(a.run_id==0 && a.end==0 && a.copied==0);
 assert(zam_output_begin(&r)==0);assert(zam_output_write(&r,in,3)==0);a=read_at(0,2,out);assert(a.run_id==1&&a.next==2&&a.copied==2&&!memcmp(out,in,2));
 zam_output_snapshot_t b=read_at(0,3,again);assert(b.next==3&&!memcmp(again,in,3));a=read_at(a.next,3,out);assert(a.copied==1&&out[0]==in[2]);
 assert(zam_output_begin(&r)==3);assert(zam_output_write(&r,in,2048)==0);a=read_at(3,2048,out);assert(a.copied==2048&&!memcmp(out,in,2048));
 assert(zam_output_write(&r,in+2048,4952)==4952);a=read_at(3,2048,out);assert(a.lost==4952&&a.copied==2048&&!memcmp(out,in+sizeof(in)-2048,2048));assert(!zam_output_copy(&r,a.end+1,out,sizeof(out),&b));
 b=read_at(a.end,0,NULL);assert(b.copied==0&&b.next==a.end);assert(zam_output_begin(&r)==2048);a=read_at(3,4,out);assert(a.run_id==3&&a.lost==7000&&a.copied==0);
 const unsigned char utf[]={0xe2,0x82,0xac};assert(zam_output_write(&r,utf,sizeof(utf))==0);a=read_at(7003,1,out);assert((unsigned char)out[0]==0xe2);a=read_at(a.next,2,out);assert((unsigned char)out[0]==0x82&&(unsigned char)out[1]==0xac);
 memset(&r,0,sizeof(r));r.next=UINT64_MAX-2;assert(zam_output_write(&r,"abcd",4)==2);a=read_at(UINT64_MAX-2,4,out);assert(a.end==UINT64_MAX&&a.copied==2&&a.exhausted&&!memcmp(out,"ab",2));assert(zam_output_write(&r,"x",1)==1);a=read_at(UINT64_MAX,1,out);assert(a.copied==0&&a.exhausted);
 puts("Output ring: cursors, overwrite, independent readers, RUN, UTF-8 and overflow PASS");return 0;
}
