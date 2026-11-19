#pragma once
#include <stddef.h>
#include <stdio.h>
#define ZAM_WEB_STREAM_BUFFER 2048U
typedef int (*zam_web_stream_send_t)(void *ctx,const void *data,size_t size);
/* Returns failure on short read or transport error. Does not finish the HTTP
 * response: caller closes the file before sending the final chunk. */
int zam_web_stream(FILE *file,size_t size,zam_web_stream_send_t send,void *ctx);
