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

After flashing, the example application initializes the LCD and displays a simple test pattern. If you are using the Touch variant, touch events are printed to the serial console. You can modify the source code to experiment with different graphics or integrate your own application logic.

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

