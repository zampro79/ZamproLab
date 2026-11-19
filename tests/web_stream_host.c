#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "zam_web_stream.h"
typedef struct { size_t size,seen,calls,fail_after; } context;
static int send_chunk(void *ptr,const void *data,size_t n) {
    context *c=ptr; const unsigned char *p=data; assert(n<=ZAM_WEB_STREAM_BUFFER);
    if(c->fail_after && c->calls++>=c->fail_after)return -1;
    for(size_t i=0;i<n;i++)assert(p[i]==(unsigned char)(c->seen+i));
    c->seen+=n;return 0;
}
static void *run(void *v) {
    context *c=v;FILE *f=tmpfile();assert(f);
    for(size_t i=0;i<c->size;i++)assert(fputc((unsigned char)i,f)!=EOF);
    assert(!fseek(f,0,SEEK_SET));int r=zam_web_stream(f,c->size,send_chunk,c);
    if(c->fail_after)assert(r);else assert(!r && c->seen==c->size);
    assert(!fclose(f));return NULL;
}
int main(void) {
    const size_t sizes[]={0,1,16384,16385,32768,65535,65536};
    for(size_t i=0;i<sizeof(sizes)/sizeof(*sizes);i++){context c={.size=sizes[i]};run(&c);}
    context stopped={.size=65536,.fail_after=3};run(&stopped);
    FILE *f=tmpfile();assert(f);assert(fputc(0,f)!=EOF);rewind(f);context c={0};assert(zam_web_stream(f,2,send_chunk,&c));assert(!fclose(f));
    pthread_t t[4];context threads[4]={{.size=16385},{.size=32768},{.size=65535},{.size=65536}};
    for(int i=0;i<4;i++)assert(!pthread_create(&t[i],NULL,run,&threads[i]));
    for(int i=0;i<4;i++)assert(!pthread_join(t[i],NULL));
    puts("bounded streaming boundaries, short read, disconnect and four independent requests: PASS");
}
