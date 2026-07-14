#pragma once
#include "FreeRTOS.h"

typedef void* QueueHandle_t;

inline BaseType_t xQueueSend(QueueHandle_t, const void*, TickType_t) { return pdTRUE; }
inline BaseType_t xQueueSendToBack(QueueHandle_t, const void*, TickType_t) { return pdTRUE; }
inline BaseType_t xQueueReceive(QueueHandle_t, void*, TickType_t) { return pdFALSE; }
inline BaseType_t xQueuePeek(QueueHandle_t, void*, TickType_t) { return pdFALSE; }
