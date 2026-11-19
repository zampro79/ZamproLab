#pragma once
#include <stddef.h>
#include "esp_err.h"

/* Called from a console command only. Completion runs in the console task;
 * the secret is valid only during the callback and must not be retained. */
typedef void (*zam_console_secret_cb_t)(esp_err_t result, const char *secret, void *context);
esp_err_t zam_console_request_secret(size_t capacity, zam_console_secret_cb_t callback, void *context);
esp_err_t zam_console_start(void);
