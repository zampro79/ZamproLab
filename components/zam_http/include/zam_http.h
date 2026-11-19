#pragma once
#include <stdbool.h>
#include "esp_err.h"
/* Initialize once at startup; start/stop are serialized and idempotent. */
esp_err_t zam_http_init(void);
esp_err_t zam_http_start(void);
esp_err_t zam_http_stop(void);
bool zam_http_is_running(void);
