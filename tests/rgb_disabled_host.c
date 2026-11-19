#include <stdatomic.h>
#include "zam_rgb.h"
atomic_int test_rgb_enabled,test_rgb_last,test_rgb_fail;
esp_err_t zam_rgb_init(void){return ESP_ERR_INVALID_STATE;}
bool zam_rgb_available(void){return atomic_load(&test_rgb_enabled);}
esp_err_t zam_rgb_set(uint8_t r,uint8_t g,uint8_t b){atomic_store(&test_rgb_last,(r<<16)|(g<<8)|b);return atomic_load(&test_rgb_fail)?ESP_FAIL:ESP_OK;}
void zam_rgb_shutdown(void){}
