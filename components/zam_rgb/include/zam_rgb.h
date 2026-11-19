#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
esp_err_t zam_rgb_init(void);
bool zam_rgb_available(void);
esp_err_t zam_rgb_set(uint8_t red, uint8_t green, uint8_t blue);
void zam_rgb_shutdown(void);
