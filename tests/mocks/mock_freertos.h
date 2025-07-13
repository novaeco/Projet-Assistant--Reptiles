#pragma once
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

extern BaseType_t mock_xTaskCreate_ret;
extern TaskFunction_t mock_last_task_func;

BaseType_t xTaskCreate(TaskFunction_t pxTaskCode,
                       const char * const pcName,
                       uint32_t usStackDepth,
                       void *pvParameters,
                       UBaseType_t uxPriority,
                       TaskHandle_t *pxCreatedTask);
