#pragma once

#include "esp_err.h"

/** Initialize power management */
esp_err_t power_init(void);

/** Switch CPU to high performance mode */
void power_high_performance(void);

/** Switch CPU to low power mode */
void power_low_power(void);

/** Register a user activity event */
void power_register_activity(void);


