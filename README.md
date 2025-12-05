# LizardNova – ESP32-S3 Waveshare 7B (ESP-IDF 6.1, LVGL 9.4)

Assistant Administratif Reptiles pour la carte Waveshare ESP32-S3 Touch LCD 7B (1024×600, GT911). Code prêt pour ESP-IDF 6.1 et LVGL 9.4.

## Prérequis build
- Utiliser **exclusivement ESP-IDF v6.1.0 release** (tag officiel). Toute autre version (`-dev`, `-dirty`, ou autre tag) est refusée dès la configuration CMake avec un message explicite. Vérifiez votre environnement avec `idf.py --version` et la variable `IDF_PATH` avant de lancer le build.
- Initialiser l’environnement : `. $IDF_PATH/export.sh` (Linux/macOS) ou `export.bat` (Windows).
- Cible : `esp32s3` (16 MB flash, 8 MB PSRAM)
- Outils IDF (Python, CMake, Ninja) installés via l’installeur ou `install.sh`

### Commandes recommandées
```bash
idf.py set-target esp32s3
idf.py fullclean
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

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
- Écran RGB 1024×600 : fréquence PCLK par défaut **51,2 MHz** (calculée pour ~60 fps avec htotal=1344, vtotal=635). Ajustable via `CONFIG_BOARD_LCD_PCLK_HZ` si un compromis bande passante/stabilité est nécessaire.
- Polarités configurables (Kconfig) pour HSYNC/VSYNC/DE et front d’échantillonnage PCLK afin d’aligner précisément le panneau Waveshare si une autre révision impose des niveaux différents.
- Redémarrage automatique du DMA en VSYNC activé (`CONFIG_LCD_RGB_RESTART_IN_VSYNC`) pour rattraper tout drift résiduel.
- Tactile GT911 (I2C) : géré via `esp_lcd_touch_gt911`.
- Rétroéclairage : réglable 0–100 % via `board_set_backlight_percent()` et le slider UI Paramètres.
- SD SPI : désactivée (instabilité de la CS sur l'IO expander Waveshare).
- Batterie : lecture 0–100 % via l’expandeur CH32V003, affichée dans l’en-tête du tableau de bord.

## Stabilité affichage RGB
- Double framebuffer en PSRAM et redémarrage automatique en VSYNC (`CONFIG_LCD_RGB_RESTART_IN_VSYNC`) pour éviter le drift quand la bande passante est saturée.
- PCLK ajustable : `idf.py menuconfig` → **Board Support → LCD Timing → LCD pixel clock (Hz)**. Laisser 51,2 MHz pour rester verrouillé à ~60 fps ; descendre progressivement (40 MHz, puis 30 MHz) uniquement si des underflows DMA apparaissent.
- Optimisations mémoires appliquées via `sdkconfig.defaults` : instructions/RODATA fetch en PSRAM, ligne de cache 32 B, optimisation compilateur "Performance" pour réduire les underflows DMA.

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

## Gestion des assets (zéro binaire dans Git)
- Aucun fichier binaire (bin, elf, jpg/png, polices, etc.) n’est commité dans ce dépôt pour éviter le rejet des demandes d’extraction.
- Les éventuels visuels LVGL ou assets UI doivent être placés localement dans `public/images/` sans les ajouter à Git.
- Documentez dans le code ou dans `public/images/README.md` le nom et le format attendus, et chargez-les de manière optionnelle pour que `idf.py build` fonctionne même sans asset.
