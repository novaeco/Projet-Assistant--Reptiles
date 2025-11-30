#pragma once

#include "driver/gpio.h"

// =============================================================================
// LCD RGB Interface (Waveshare ESP32-S3-Touch-LCD-7B)
// =============================================================================
#define BOARD_LCD_PIXEL_CLOCK_HZ (30 * 1000 * 1000) // 30 MHz
#define BOARD_LCD_H_RES          1024
#define BOARD_LCD_V_RES          600

// RGB565 Data Pins (R0-R4, G0-G5, B0-B4) mapped to the high-order bits of the
// Waveshare 24-bit bus (R3-R7, G2-G7, B3-B7)
#define BOARD_LCD_PCLK           GPIO_NUM_7
#define BOARD_LCD_VSYNC          GPIO_NUM_3
#define BOARD_LCD_HSYNC          GPIO_NUM_46
#define BOARD_LCD_DE             GPIO_NUM_5
#define BOARD_LCD_DISP           GPIO_NUM_NC // Controlled via IO Expander

// Blue data (B3-B7 -> B0-B4)
#define BOARD_LCD_DATA_B0        GPIO_NUM_14 // B3
#define BOARD_LCD_DATA_B1        GPIO_NUM_38 // B4
#define BOARD_LCD_DATA_B2        GPIO_NUM_18 // B5
#define BOARD_LCD_DATA_B3        GPIO_NUM_17 // B6
#define BOARD_LCD_DATA_B4        GPIO_NUM_10 // B7

// Green data (G2-G7 -> G0-G5)
#define BOARD_LCD_DATA_G0        GPIO_NUM_39 // G2
#define BOARD_LCD_DATA_G1        GPIO_NUM_0  // G3
#define BOARD_LCD_DATA_G2        GPIO_NUM_45 // G4
#define BOARD_LCD_DATA_G3        GPIO_NUM_48 // G5
#define BOARD_LCD_DATA_G4        GPIO_NUM_47 // G6
#define BOARD_LCD_DATA_G5        GPIO_NUM_21 // G7

// Red data (R3-R7 -> R0-R4)
#define BOARD_LCD_DATA_R0        GPIO_NUM_1  // R3
#define BOARD_LCD_DATA_R1        GPIO_NUM_2  // R4
#define BOARD_LCD_DATA_R2        GPIO_NUM_42 // R5
#define BOARD_LCD_DATA_R3        GPIO_NUM_41 // R6
#define BOARD_LCD_DATA_R4        GPIO_NUM_40 // R7

// =============================================================================
// I2C Bus (Touch GT911 & IO Expander CH32V003)
// =============================================================================
#define BOARD_I2C_PORT           I2C_NUM_0
#define BOARD_I2C_SDA            GPIO_NUM_8
#define BOARD_I2C_SCL            GPIO_NUM_9
#define BOARD_I2C_FREQ_HZ        400000

// Touch GT911
#define BOARD_TOUCH_INT          GPIO_NUM_4
#define BOARD_TOUCH_RST          GPIO_NUM_NC // Reset controlled via IO Expander

// IO Expander CH32V003
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
#define BOARD_LCD_BK_LIGHT_ON_LEVEL  1

