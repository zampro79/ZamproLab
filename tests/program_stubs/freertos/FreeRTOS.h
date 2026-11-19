#pragma once
#include <pthread.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
typedef pthread_mutex_t portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED PTHREAD_MUTEX_INITIALIZER
#define portENTER_CRITICAL(x) pthread_mutex_lock(x)
#define portEXIT_CRITICAL(x) pthread_mutex_unlock(x)
#define portMAX_DELAY UINT32_MAX
#define pdTRUE 1
#define pdPASS 1
#define pdMS_TO_TICKS(x) (x)
