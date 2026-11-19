#pragma once
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
typedef void *rmt_channel_handle_t;
typedef void *rmt_encoder_handle_t;
typedef struct {int level0,duration0,level1,duration1;} rmt_symbol_word_t;
typedef struct {int clk_src,gpio_num,mem_block_symbols,resolution_hz,trans_queue_depth;} rmt_tx_channel_config_t;
typedef struct {size_t (*callback)(const void *,size_t,size_t,size_t,rmt_symbol_word_t *,bool *,void *);} rmt_simple_encoder_config_t;
typedef struct {int loop_count;} rmt_transmit_config_t;
#define RMT_CLK_SRC_DEFAULT 0
extern int mock_error, mock_disabled, mock_deleted;
extern uint8_t mock_pixel[3];
static inline int rmt_new_tx_channel(const rmt_tx_channel_config_t *c,void **h){if(mock_error==1)return ESP_FAIL;if(c->gpio_num!=48)return ESP_FAIL;*h=(void *)1;return ESP_OK;}
static inline int rmt_new_simple_encoder(const rmt_simple_encoder_config_t *c,void **e){(void)c;if(mock_error==2)return ESP_FAIL;*e=(void *)2;return ESP_OK;}
static inline int rmt_enable(void *c){(void)c;return mock_error==3?ESP_FAIL:ESP_OK;}
static inline int rmt_disable(void *c){(void)c;mock_disabled++;return ESP_OK;}
static inline int rmt_del_encoder(void *c){(void)c;mock_deleted++;return ESP_OK;}
static inline int rmt_del_channel(void *c){(void)c;mock_deleted++;return ESP_OK;}
static inline int rmt_transmit(void *c,void *e,const void *d,size_t n,const rmt_transmit_config_t *t){(void)c;(void)e;(void)t;if(n!=3)return ESP_FAIL;const uint8_t *p=d;for(int i=0;i<3;i++)mock_pixel[i]=p[i];return mock_error==4?ESP_FAIL:ESP_OK;}
static inline int rmt_tx_wait_all_done(void *c,int t){(void)c;(void)t;return mock_error==5?ESP_FAIL:ESP_OK;}
