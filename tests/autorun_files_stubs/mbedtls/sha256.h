#pragma once
#include <openssl/sha.h>
typedef SHA256_CTX mbedtls_sha256_context;
static inline void mbedtls_sha256_init(mbedtls_sha256_context *c){(void)c;}
static inline void mbedtls_sha256_free(mbedtls_sha256_context *c){(void)c;}
static inline int mbedtls_sha256_starts(mbedtls_sha256_context *c,int x){(void)x;return SHA256_Init(c)==1?0:-1;}
static inline int mbedtls_sha256_update(mbedtls_sha256_context *c,const unsigned char *p,size_t n){return SHA256_Update(c,p,n)==1?0:-1;}
static inline int mbedtls_sha256_finish(mbedtls_sha256_context *c,unsigned char *p){return SHA256_Final(p,c)==1?0:-1;}
