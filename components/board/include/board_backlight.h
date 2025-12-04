#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint16_t max_duty;   /*!< Duty cycle upper bound (0..CONFIG_BOARD_BACKLIGHT_MAX_DUTY) */
} board_backlight_config_t;

/**
 * @brief Initialise le rétroéclairage à l'aide du périphérique LEDC.
 *
 * @param cfg Configuration du rétroéclairage (NULL pour utiliser les valeurs Kconfig).
 * @return ESP_OK si l'initialisation s'est déroulée correctement, sinon un code d'erreur ESP-IDF.
 */
esp_err_t board_backlight_init(const board_backlight_config_t *cfg);

/**
 * @brief Régler la luminosité du rétroéclairage.
 *
 * @param percent Valeur comprise entre 0 et 100 représentant la luminosité souhaitée.
 * @return ESP_OK en cas de succès, sinon un code d'erreur LEDC/IO expander.
 */
esp_err_t board_backlight_set_percent(uint8_t percent);

#ifdef __cplusplus
}
#endif
