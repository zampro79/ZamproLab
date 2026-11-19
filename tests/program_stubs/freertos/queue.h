#pragma once
#include <stdint.h>
typedef struct fake_queue *QueueHandle_t;
QueueHandle_t xQueueCreate(unsigned count,unsigned item_size);
int xQueueSend(QueueHandle_t q,const void *item,uint32_t ticks);
int xQueueReceive(QueueHandle_t q,void *item,uint32_t ticks);
void vQueueDelete(QueueHandle_t q);
