# Pending Actions for ESP-IDF 6.1 / LVGL 9.4 Readiness

- **Build & hardware validation on ESP-IDF 6.1**: Rebuild the firmware with ESP-IDF 6.1 using `sdkconfig.defaults`, then run on the Waveshare 7B to catch any compilation warnings or runtime regressions (LCD init, touch init, Wi-Fi/web, SD card mount, MQTT).
- **Pin LVGL 9.4 in dependency metadata**: Ensure `idf_component.yml` (or component registry config) explicitly depends on lvgl/lvgl 9.4.0 so the correct major/minor is fetched, matching the v9 APIs already used by the UI.
- **LCD pixel clock verification**: Validate that `BOARD_LCD_PIXEL_CLOCK_HZ` matches the stable range for the 1024x600 panel; if artifacts appear at 30 MHz, lower to ~27 MHz for spec compliance.
- **Battery sensing calibration**: The UI battery label now displays the percentage returned by `board_get_battery_level`; verify on hardware that the CH32V003 ADC scaling (register 0x00) maps accurately to 0–100%, and adjust conversion if needed.
- **Optional peripheral enablement**: Document/enable CAN and RS485 modes (transceiver enable pins via the IO expander) if the project needs full hardware coverage; currently unused by the application logic.
