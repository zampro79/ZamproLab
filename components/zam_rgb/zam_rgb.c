#include "zam_rgb.h"
#include "sdkconfig.h"
#if CONFIG_ZAM_RGB_VERIFIED_S3
#include "driver/rmt_tx.h"
#include "esp_log.h"
static rmt_channel_handle_t channel;
static rmt_encoder_handle_t encoder;
static bool enabled, available;
static uint8_t pixel[3];
static const rmt_symbol_word_t zero={.level0=1,.duration0=4,.level1=0,.duration1=8};
static const rmt_symbol_word_t one={.level0=1,.duration0=8,.level1=0,.duration1=4};
static const rmt_symbol_word_t reset={.level0=0,.duration0=1500,.level1=0,.duration1=1500};
static size_t encode(const void *data,size_t size,size_t written,size_t space,rmt_symbol_word_t *out,bool *done,void *arg)
{
    (void)arg;if(space<8)return 0;size_t n=written/8;
    if(n<size){const uint8_t *p=data;for(unsigned i=0;i<8;i++)out[i]=(p[n]&(0x80U>>i))?one:zero;return 8;}
    out[0]=reset;*done=true;return 1;
}
void zam_rgb_shutdown(void)
{
    available=false;
    if(enabled){rmt_disable(channel);enabled=false;}
    if(encoder){rmt_del_encoder(encoder);encoder=NULL;}
    if(channel){rmt_del_channel(channel);channel=NULL;}
}
esp_err_t zam_rgb_init(void)
{
    if(available)return ESP_OK;
    rmt_tx_channel_config_t cfg={.clk_src=RMT_CLK_SRC_DEFAULT,.gpio_num=48,.mem_block_symbols=64,.resolution_hz=10000000,.trans_queue_depth=1};
    esp_err_t e=rmt_new_tx_channel(&cfg,&channel);if(e!=ESP_OK)goto fail;
    rmt_simple_encoder_config_t ecfg={.callback=encode};e=rmt_new_simple_encoder(&ecfg,&encoder);if(e!=ESP_OK)goto fail;
    e=rmt_enable(channel);if(e!=ESP_OK)goto fail;enabled=true;available=true;return ESP_OK;
fail:zam_rgb_shutdown();return e;
}
bool zam_rgb_available(void){return available;}
/* The program worker is the sole caller after startup. Never call under a spinlock.
 * If completion is uncertain, disable and release the driver before reusing pixel. */
esp_err_t zam_rgb_set(uint8_t r,uint8_t g,uint8_t b)
{
    if(!available)return ESP_ERR_INVALID_STATE;
    pixel[0]=g;pixel[1]=r;pixel[2]=b;
    rmt_transmit_config_t tx={.loop_count=0};
    esp_err_t e=rmt_transmit(channel,encoder,pixel,sizeof(pixel),&tx);
    if(e==ESP_OK)e=rmt_tx_wait_all_done(channel,20);
    if(e!=ESP_OK){ESP_LOGW("zam_rgb","RGB transmission failed: %d",(int)e);zam_rgb_shutdown();}
    return e;
}
#else
esp_err_t zam_rgb_init(void){return ESP_ERR_NOT_SUPPORTED;}
bool zam_rgb_available(void){return false;}
esp_err_t zam_rgb_set(uint8_t r,uint8_t g,uint8_t b){(void)r;(void)g;(void)b;return ESP_ERR_NOT_SUPPORTED;}
void zam_rgb_shutdown(void){}
#endif
