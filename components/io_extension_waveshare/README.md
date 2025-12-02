# Waveshare IO Extension (CH32V003 firmware)

Minimal ESP-IDF component for the IO extension microcontroller bundled on the Waveshare ESP32-S3 Touch LCD 7B. The MCU exposes 8 GPIOs, a PWM channel (used for LCD backlight), and a simple ADC channel used by Waveshare for battery sense.

This driver is adapted from the official demo (`io_extension` folder) and uses the ESP-IDF v6 I2C master device API. It intentionally delegates bus ownership to the caller so it can share the bus with other peripherals (GT911 touch).

## API
- `io_extension_ws_init(config, &handle)`: Bind the IO extension to an existing I2C master bus (address defaults to 0x24). All IOs are configured as push-pull outputs at startup.
- `io_extension_ws_set_output(handle, pin, level)`: Drive IO0-IO7 high/low.
- `io_extension_ws_set_pwm_percent(handle, percent)`: Set PWM duty cycle (0-100%, clamped to 97% per Waveshare firmware) for the backlight channel.
- `io_extension_ws_read_inputs(handle, &value)`: Read IO input register (IO7 carries the battery-sense byte in Waveshare wiring).
- `io_extension_ws_read_adc(handle, &value)`: Read the 16-bit ADC sample when available.

Pins match the Waveshare demo mapping: IO1=TP_RST, IO2=LCD_BK, IO3=LCD_RST, IO4=SD_CS, IO5=CAN/USB mux, IO6/IO7 general purpose.
