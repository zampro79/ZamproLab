#pragma once
#include <stddef.h>
typedef void *TaskHandle_t;
int xTaskCreate(void (*fn)(void *),const char *name,unsigned stack,void *arg,unsigned priority,TaskHandle_t *handle);
void vTaskDelay(unsigned ticks);
void vTaskDelete(TaskHandle_t task);
size_t uxTaskGetStackHighWaterMark(TaskHandle_t task);
