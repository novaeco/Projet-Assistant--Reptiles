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
3. Select the default configuration for your board:
   ```bash
   export SDKCONFIG_DEFAULTS=sdkconfig.lcd   # LCD board
   # export SDKCONFIG_DEFAULTS=sdkconfig.touch # Touch board
   ```
4. Configure the project:
   ```bash
   idf.py menuconfig
   ```
5. Build and flash:
   ```bash
   idf.py -p PORT flash monitor
   ```
   Replace `PORT` with the serial port of your development board.

## Running Unit Tests

Unity tests are located in the `tests/` directory. Once the ESP-IDF environment is set up, run:
```bash
idf.py test
```
Run the command from the project root to execute all unit tests using the provided mocks.


## Usage

After flashing, the application brings up the ST7789 LCD with LVGL and starts scanning the keyboard matrix. Wi-Fi 6 and BLE are initialized and their connection state is shown on the status label. The label now updates when a BLE device connects or disconnects. LVGL assets can be loaded from a microSD card and the backlight brightness is controlled by PWM. Touch input is optional and power modes can be switched between high-performance and low-power cores.

### SD Card Usage

Use `storage_sd_init()` to mount the card before loading assets. Call
`storage_sd_unmount()` prior to removal and invoke `storage_sd_update()` in the
main loop to detect if the card is unexpectedly removed. When removal is
detected, an error is logged and displayed on the LVGL interface.
Use `storage_sd_free()` to release memory obtained from `storage_sd_load()`.

`storage_sd_load()` can read a file directly into memory. The caller is
responsible for freeing the returned buffer using `storage_sd_free()` (or
`free()`). For example:

```c
size_t size;
void *data = storage_sd_load("images/picture.bin", &size);
lv_img_set_src(img, data);
storage_sd_free(data);
```

### Language Selection

The user interface supports multiple languages. Call `ui_set_language()` to
switch between the built‑in tables or load your own translations at runtime with
`ui_load_language()`. For example:

```c
static const char *es_table[] = {
    "Inicio", "Ajustes", "Red", "Energia", "Ingl\xC3\xA9s", "Franc\xC3\xA9s",
    "Brillo", "SSID: %s\nIP: %s\nBLE: %s", "Conectado", "Anuncio"
};
ui_load_language(UI_LANG_FR + 1, es_table);
ui_set_language(UI_LANG_FR + 1);
```

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

## Directory Structure

The repository follows a typical ESP-IDF layout. The `main/` directory holds
`app_main.c`, which initializes every subsystem. Reusable modules live under
`components/` and are built as independent ESP-IDF components:

| Component     | Purpose                                                |
|---------------|--------------------------------------------------------|
| `backlight`   | Controls LCD brightness via PWM.                       |
| `buttons`     | Handles the physical button and triggers activity.     |
| `display`     | Sets up the ST7789 display driver and LVGL.            |
| `keyboard`    | Scans the 4x4 key matrix.                              |
| `network`     | Manages Wi‑Fi 6 and BLE connectivity.                  |
| `power`       | Implements power management and sleep handling.        |
| `storage_sd`  | Loads LVGL assets from a microSD card.                 |
| `touch`       | Optional capacitive touch controller support.          |

Unit tests reside in `tests/` alongside mocked drivers, while project
configuration files such as `CMakeLists.txt` and the `sdkconfig.*` defaults live in the repository root.

## Power‑Saving Strategies

The firmware creates an ESP‑IDF power‑management lock in `power_init()` and
switches between high‑performance and low‑power modes using
`power_high_performance()` and `power_low_power()`. An inactivity task monitors
user input and after 30 seconds releases the lock, enables GPIO and touch
wake‑ups and enters light sleep. Any key press, button interrupt or touch event
calls `power_register_activity()` to reset the timer and restore
high‑performance mode when the device wakes.

## LVGL Interface

The LVGL display presents a clean status bar with network information followed
by a main content area. A simplified layout is shown below:

```
┌───────────────────────────────┐
│  Wi‑Fi/BLE Status Label       │
├───────────────────────────────┤
│                               │
│       Application Views       │
│                               │
└───────────────────────────────┘
```

## Credits

This project is maintained by the LizardNova developers and uses ESP-IDF under the Apache 2.0 license. Contributions are welcome!

