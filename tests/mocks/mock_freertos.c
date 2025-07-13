#include "mock_freertos.h"

BaseType_t mock_xTaskCreate_ret = pdPASS;
TaskFunction_t mock_last_task_func = NULL;

BaseType_t xTaskCreate(TaskFunction_t pxTaskCode,
                       const char * const pcName,
                       uint32_t usStackDepth,
                       void *pvParameters,
                       UBaseType_t uxPriority,
                       TaskHandle_t *pxCreatedTask)
{
    (void)pcName; (void)usStackDepth; (void)pvParameters; (void)uxPriority; (void)pxCreatedTask;
    mock_last_task_func = pxTaskCode;
    return mock_xTaskCreate_ret;
}
