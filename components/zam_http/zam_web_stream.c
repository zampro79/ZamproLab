#include "zam_web_stream.h"
int zam_web_stream(FILE *file,size_t size,zam_web_stream_send_t send,void *ctx)
{
    if (!file || !send) return -1;
    char buffer[ZAM_WEB_STREAM_BUFFER];
    while (size) {
        size_t want=size<sizeof(buffer)?size:sizeof(buffer);
        size_t got=fread(buffer,1,want,file);
        if (!got || ferror(file)) return -1;
        if (send(ctx,buffer,got)) return -1;
        size-=got;
    }
    return 0;
}
