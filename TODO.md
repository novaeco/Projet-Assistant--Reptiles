# Pending Actions for ESP-IDF 6.1 / LVGL 9.4 Readiness

- **Build & hardware validation on ESP-IDF 6.1**: Rebuild the firmware with ESP-IDF 6.1 using `sdkconfig.defaults`, then run on the Waveshare 7B to catch any compilation warnings or runtime regressions (LCD init, touch init, Wi-Fi/web, SD card mount, MQTT) and verify CAN/USB routing.
- **Battery sensing calibration on hardware**: Measure the CH32V003 IO7 raw values at full/empty battery and set `CONFIG_BOARD_BATTERY_RAW_FULL` / `CONFIG_BOARD_BATTERY_RAW_EMPTY` accordingly so the UI percentage matches reality.
