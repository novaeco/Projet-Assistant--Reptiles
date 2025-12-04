# Board Component (Waveshare ESP32-S3 Touch LCD 7B)

This component brings up the Waveshare ESP32-S3 Touch LCD 7B (ESP32-S3-WROOM-1 N16R8) peripherals:
- I2C bus recovery + probing (GT911 touch + IO expander)
- IO expander controlled power/reset/backlight, SD card CS gating, CAN/USB mux
- RGB LCD (esp_lcd RGB panel driver)
- GT911 capacitive touch
- SD card over SDSPI with expander-driven CS
- Battery level sampling via expander input

## Kconfig options

- `CONFIG_BOARD_SD_HOLD_CS_LOW_DIAG` (bool, default `n`): Forces EXIO4/SD_CS low during SDSPI init and transactions to ease signal integrity debugging.
- `CONFIG_BOARD_BATTERY_RAW_EMPTY` (int 0..255): Raw 8-bit value reported by the expander when the battery is considered empty. Used as the lower bound for percentage scaling.
- `CONFIG_BOARD_BATTERY_RAW_FULL` (int 0..255): Raw 8-bit value reported by the expander when the battery is full. Must be greater than `RAW_EMPTY`.
- `CONFIG_BOARD_REQUIRE_IO_EXPANDER` (bool, default `n`): Abort the boot if the IO expander backend (Waveshare CH32V003 or CH422G) is missing or fails initialization. When disabled, the firmware degrades gracefully and disables touch/SD/backlight PWM/battery-sense when the expander is absent.
- `CONFIG_BOARD_IOEXP_DRIVER` choice: pick Waveshare CH32V003 firmware (default) or legacy CH422G, with optional CH422G fallback when Waveshare is selected.
- `CONFIG_BOARD_I2C_PORT/SDA/SCL`: configure the shared I2C master instance (default SDA=8, SCL=9 on I2C0).
- `CONFIG_BOARD_BACKLIGHT_MAX_DUTY` (int 1..8192, default `8192`): valeur maximale appliquée au canal LEDC pour le rétroéclairage (résolution 13 bits).
- `CONFIG_BOARD_BATTERY_ENABLE` (bool, default `n`): enable battery sampling via the expander when a battery is fitted.

Battery calibration can also be updated at runtime via `board_battery_set_calibration()`, which persists values to NVS (keys `battery_raw_empty` and `battery_raw_full`).

## Runtime behaviour

- Targeted I2C probes for GT911 and the configured IO expander are performed at boot. If devices are missing, the firmware logs a single diagnostic message and continues without panicking unless `CONFIG_BOARD_REQUIRE_IO_EXPANDER=y`.
- Optional peripherals (IO expander, touch, SD) degrade gracefully: failures return errors to callers and do not abort the boot.
- SD card CS is controlled through the expander; SDSPI transactions toggle CS per command unless the diagnostic hold-low option is enabled.
- Battery level is read from the expander input register and linearly scaled using the calibrated bounds.
- The LVGL vsync callback requires `CONFIG_ESP_LCD_RGB_ISR_IRAM_SAFE=y` (set in `sdkconfig.defaults`). If disabled, a warning is issued and rendering continues without vsync-driven flush-ready events.

## Backlight control

The LED backlight is driven by the ESP32-S3 LEDC peripheral on `BOARD_LCD_BK_LIGHT_GPIO` (GPIO15 by default in `board_pins.h`). After calling `board_backlight_init()`, brightness can be adjusted at runtime:

```c
// Set maximum brightness
board_backlight_set_percent(100);
```

PWM is configuré en mode « actif haut » : 0 % coupe totalement le rétroéclairage, 100 % applique le duty configuré via `CONFIG_BOARD_BACKLIGHT_MAX_DUTY`.
