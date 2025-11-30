# Actions en attente pour la préparation ESP-IDF 6.1 / LVGL 9.4

- **Validation matérielle et de compilation sur ESP-IDF 6.1** : Recompilez le firmware avec ESP-IDF 6.1 en utilisant `sdkconfig.defaults` , puis exécutez-le sur le Waveshare 7B pour détecter tout avertissement de compilation ou régression d'exécution (initialisation LCD, initialisation tactile, Wi-Fi/web, montage de carte SD, MQTT) et vérifiez le routage CAN/USB.
 
- **Calibrage de la détection de batterie sur le matériel** : Mesurez les valeurs brutes CH32V003 IO7 à batterie pleine/vide et définissez `CONFIG_BOARD_BATTERY_RAW_FULL` / `CONFIG_BOARD_BATTERY_RAW_EMPTY` en conséquence afin que le pourcentage de l'interface utilisateur corresponde à la réalité.