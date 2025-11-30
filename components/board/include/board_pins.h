#pragma once

#include "driver/gpio.h"

// =============================================================================
// LCD RGB Interface (Waveshare ESP32-S3-Touch-LCD-7B)
// =============================================================================
#define BOARD_LCD_PIXEL_CLOCK_HZ (16 * 1000 * 1000) // 16 MHz
#define BOARD_LCD_H_RES          1024
#define BOARD_LCD_V_RES          600

// RGB565 Data Pins (R0-R4, G0-G5, B0-B4) - Example mapping, needs verification with schematic
// Assuming 16-bit interface. 
// Note: On ESP32-S3 RGB interface, pins are flexible but must be configured correctly.
// Based on typical Waveshare 7" S3 board:
#define BOARD_LCD_PCLK           GPIO_NUM_42
#define BOARD_LCD_VSYNC          GPIO_NUM_40
#define BOARD_LCD_HSYNC          GPIO_NUM_39
#define BOARD_LCD_DE             GPIO_NUM_41
#define BOARD_LCD_DISP           GPIO_NUM_NC // Controlled via IO Expander usually

// Data pins (B0-B4, G0-G5, R0-R4) - Adjust based on actual schematic
// This is a placeholder mapping. In a real scenario, exact pinout is critical.
#define BOARD_LCD_DATA_B0        GPIO_NUM_1
#define BOARD_LCD_DATA_B1        GPIO_NUM_2
#define BOARD_LCD_DATA_B2        GPIO_NUM_3
#define BOARD_LCD_DATA_B3        GPIO_NUM_4
#define BOARD_LCD_DATA_B4        GPIO_NUM_5
#define BOARD_LCD_DATA_G0        GPIO_NUM_6
#define BOARD_LCD_DATA_G1        GPIO_NUM_7
#define BOARD_LCD_DATA_G2        GPIO_NUM_15
#define BOARD_LCD_DATA_G3        GPIO_NUM_16
#define BOARD_LCD_DATA_G4        GPIO_NUM_17
#define BOARD_LCD_DATA_G5        GPIO_NUM_18
#define BOARD_LCD_DATA_R0        GPIO_NUM_8  // Conflict with SDA? Check schematic carefully.
#define BOARD_LCD_DATA_R1        GPIO_NUM_19 // S3 has limited pins, RGB takes many.
#define BOARD_LCD_DATA_R2        GPIO_NUM_20
#define BOARD_LCD_DATA_R3        GPIO_NUM_21
#define BOARD_LCD_DATA_R4        GPIO_NUM_48 // High GPIOs

// =============================================================================
// I2C Bus (Touch GT911 & IO Expander CH422G)
// =============================================================================
#define BOARD_I2C_PORT           I2C_NUM_0
#define BOARD_I2C_SDA            GPIO_NUM_8  // Verify if shared or dedicated
#define BOARD_I2C_SCL            GPIO_NUM_9
#define BOARD_I2C_FREQ_HZ        400000

// Touch GT911
#define BOARD_TOUCH_INT          GPIO_NUM_4
#define BOARD_TOUCH_RST          GPIO_NUM_NC // Controlled via IO Expander

// IO Expander CH422G
#define BOARD_IO_EXP_ADDR        0x24

// =============================================================================
// SD Card (SPI)
// =============================================================================
#define BOARD_SD_SPI_HOST        SPI2_HOST
#define BOARD_SD_MISO            GPIO_NUM_13
#define BOARD_SD_MOSI            GPIO_NUM_11
#define BOARD_SD_CLK             GPIO_NUM_12
#define BOARD_SD_CS              GPIO_NUM_10 

// =============================================================================
// Backlight
// =============================================================================
// Usually controlled via IO Expander or PWM pin
#define BOARD_LCD_BK_LIGHT_ON_LEVEL  1