# LizardNova

LizardNova is a sample project for the ESP32-C6 family that demonstrates how to drive a 1.47" LCD display. The project includes example code for two reference boards:

- **ESP32-C6-LCD-1.47-M**
- **ESP32-C6-Touch-LCD-1.47-M**

This repository shows how to set up and build an application using [ESP-IDF](https://github.com/espressif/esp-idf) v5.x.

## Building with ESP-IDF

1. Install ESP-IDF v5.x following the official [getting started guide](https://docs.espressif.com/projects/esp-idf/en/v5.0/esp32c6/get-started/).
2. Set up the environment variables for ESP-IDF:
   ```bash
   . $IDF_PATH/export.sh
   ```
3. Configure the project:
   ```bash
   idf.py menuconfig
   ```
4. Build and flash:
   ```bash
   idf.py -p PORT flash monitor
   ```
   Replace `PORT` with the serial port of your development board.

## Usage

After flashing, the application brings up the ST7789 LCD with LVGL and starts scanning the keyboard matrix. Wi-Fi 6 and BLE are initialized and their status is shown on screen. LVGL assets can be loaded from a microSD card and the backlight brightness is controlled by PWM. Touch input is optional and power modes can be switched between high-performance and low-power cores.

## GPIO Assignments

| Signal | ESP32-C6-LCD-1.47-M | ESP32-C6-Touch-LCD-1.47-M |
| ------ | ------------------ | -------------------------- |
| LCD CS | GPIO10             | GPIO10                     |
| LCD DC | GPIO9              | GPIO9                      |
| LCD RES| GPIO8              | GPIO8                      |
| LCD MOSI| GPIO7             | GPIO7                      |
| LCD SCLK| GPIO6             | GPIO6                      |
| Backlight | GPIO5           | GPIO5                      |
| Touch INT | —               | GPIO4                      |
| Touch SDA | —               | GPIO3                      |
| Touch SCL | —               | GPIO2                      |

```
             LCD
            ------
    SCLK  -| GPIO6
    MOSI  -| GPIO7
    CS    -| GPIO10
    DC    -| GPIO9
    RES   -| GPIO8
  Backlight| GPIO5
            ------
```

## Credits

This project is maintained by the LizardNova developers and uses ESP-IDF under the Apache 2.0 license. Contributions are welcome!

