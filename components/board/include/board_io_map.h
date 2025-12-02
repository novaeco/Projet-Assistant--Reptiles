#pragma once

// IO expander mapping for Waveshare ESP32-S3 Touch LCD 7B (CH422G or CH32V003)
// The lines are actively driven high/low by the IO expander unless stated.
//
// IO0 : Unused (kept high)
// IO1 : Touch reset (GT911 RST, active-low)
// IO2 : Backlight enable (panel PWM enable)
// IO3 : LCD reset (active-low)
// IO4 : SD card chip-select (active-low)
// IO5 : USB/CAN selector (high = CAN transceiver, low = USB bridge)
// IO6 : LCD_VDD / VCOM enable (active-high)
// IO7 : Battery sense input (when available on the Waveshare MCU backend)
#define IO_EXP_PIN_TOUCH_RST 1
#define IO_EXP_PIN_BK        2
#define IO_EXP_PIN_LCD_RST   3
#define IO_EXP_PIN_SD_CS     4
#define IO_EXP_PIN_CAN_USB   5
#define IO_EXP_PIN_LCD_VDD   6

#define IO_EXP_ACTIVE_LOW_SD_CS true
