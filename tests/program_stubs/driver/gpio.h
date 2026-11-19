#pragma once
#include <stdint.h>
#include "esp_err.h"
typedef int gpio_num_t;
typedef enum {GPIO_MODE_DISABLE,GPIO_MODE_INPUT,GPIO_MODE_OUTPUT} gpio_mode_t;
typedef struct {uint64_t pin_bit_mask;gpio_mode_t mode;int pull_up_en,pull_down_en,intr_type;} gpio_config_t;
#define GPIO_PULLUP_DISABLE 0
#define GPIO_PULLDOWN_DISABLE 0
#define GPIO_INTR_DISABLE 0
esp_err_t gpio_config(const gpio_config_t *config);
esp_err_t gpio_set_level(gpio_num_t pin,uint32_t level);
int gpio_get_level(gpio_num_t pin);
