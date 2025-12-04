# LizardNova – ESP32-S3 Waveshare 7B (ESP-IDF 6.1, LVGL 9.4)

Assistant Administratif Reptiles pour la carte Waveshare ESP32-S3 Touch LCD 7B (1024×600, GT911). Code prêt pour ESP-IDF 6.1 et LVGL 9.4.

## Prérequis
- ESP-IDF 6.1 installé et initialisé (`. $IDF_PATH/export.sh`)
- Cible : `esp32s3` (16 MB flash, 8 MB PSRAM)
- Python de l’IDF et CMake/Ninja disponibles

## Construction et flash
```bash
idf.py set-target esp32s3
idf.py fullclean
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

## Tests rapides
- `idf.py fullclean build`
- `idf.py -p COMx flash monitor`

## Points matériels
- Écran RGB 1024×600 : fréquence PCLK fixée à 27 MHz pour une stabilité conforme aux spécifications Waveshare.
- Tactile GT911 (I2C) : géré via `esp_lcd_touch_gt911`.
- Rétroéclairage : réglable 0–100 % via `board_set_backlight_percent()` et le slider UI Paramètres.
- SD SPI : désactivée (instabilité de la CS sur l'IO expander Waveshare).
- Batterie : lecture 0–100 % via l’expandeur CH32V003, affichée dans l’en-tête du tableau de bord.

## Rétroéclairage : options Kconfig
- `CONFIG_BOARD_BACKLIGHT_MAX_DUTY` (par défaut 5000) : rapport cyclique PWM correspondant à 100 % de luminosité. À ajuster si la fréquence PWM ou le driver changent.
- `CONFIG_BOARD_BACKLIGHT_ACTIVE_LOW` (par défaut `n`) : inverser le signal PWM si la carte coupe le rétroéclairage quand le niveau est haut (active-low). Laisser à `n` pour le câblage Waveshare actif-haut.
- `CONFIG_BOARD_BACKLIGHT_RAMP_TEST` (par défaut `n`) : effectue un balayage 0→100→0 % au démarrage pour valider la plage de luminosité.

Pour changer ces valeurs : `idf.py menuconfig` > Board > Backlight.

### Ajuster la luminosité à l’exécution
L’API `board_set_backlight_percent(uint8_t percent)` règle la luminosité de 0 à 100 %. Exemple :

```c
ESP_ERROR_CHECK(board_set_backlight_percent(80)); // 80 % pour réduire la consommation
```

## Dépannage
- Si la compilation échoue après mise à jour d’ESP-IDF, relancer `idf.py fullclean` puis `idf.py build`.
- Vérifier que les sous-modules/dépendances gérés (`idf_component.yml`) se téléchargent correctement (LVGL 9.4, GT911, drivers IDF).

## Licence
MIT
# Assistant Reptiles
