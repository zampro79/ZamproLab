#include <assert.h>
#include <stdio.h>
#include "zam_rgb.h"
int mock_error,mock_disabled,mock_deleted;uint8_t mock_pixel[3];
int main(void){
#if CONFIG_ZAM_RGB_VERIFIED_S3
 assert(!zam_rgb_available());assert(zam_rgb_init()==ESP_OK);assert(zam_rgb_available());
 assert(zam_rgb_set(11,22,33)==ESP_OK);assert(mock_pixel[0]==22&&mock_pixel[1]==11&&mock_pixel[2]==33);
 mock_error=5;assert(zam_rgb_set(1,2,3)!=ESP_OK);assert(!zam_rgb_available());assert(mock_disabled==1&&mock_deleted==2);
 mock_error=2;assert(zam_rgb_init()!=ESP_OK);assert(!zam_rgb_available());
 mock_error=0;assert(zam_rgb_init()==ESP_OK);zam_rgb_shutdown();assert(!zam_rgb_available());
#else
 assert(!zam_rgb_available());assert(zam_rgb_init()==ESP_ERR_NOT_SUPPORTED);assert(zam_rgb_set(1,2,3)==ESP_ERR_NOT_SUPPORTED);
#endif
 puts("RGB host: configuration, GRB and error cleanup PASS");return 0;
}
