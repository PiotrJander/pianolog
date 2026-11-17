#pragma once
#include "FreeRTOS.h"
static inline void vTaskDelay(int t) { (void)t; }
static inline uint32_t ulTaskNotifyTake(int a, int b) { (void)a; (void)b; return 0; }
static inline void xTaskNotifyGive(TaskHandle_t t) { (void)t; }
static inline int xTaskCreate(void (*f)(void *), const char *n, int s, void *a, int p, TaskHandle_t *h) { (void)f;(void)n;(void)s;(void)a;(void)p;(void)h; return 1; }
