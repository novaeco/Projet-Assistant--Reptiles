#pragma once

#include "core_models.h"
#include "esp_err.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the core service (load data, check integrity).
 * 
 * @return esp_err_t 
 */
esp_err_t core_init(void);

// =============================================================================
// Animal Operations
// =============================================================================

esp_err_t core_save_animal(const animal_t *animal);
esp_err_t core_get_animal(const char *id, animal_t *out_animal);
void core_free_animal_content(animal_t *animal);
esp_err_t core_list_animals(animal_summary_t **out_list, size_t *out_count);
void core_free_animal_list(animal_summary_t *list);

/**
 * @brief Search animals by name or species.
 * 
 * @param query Search string.
 * @param out_list Result list.
 * @param out_count Result count.
 * @return esp_err_t 
 */
esp_err_t core_search_animals(const char *query, animal_summary_t **out_list, size_t *out_count);

// =============================================================================
// History Operations
// =============================================================================

esp_err_t core_add_weight(const char *animal_id, float weight, const char *unit);
esp_err_t core_add_event(const char *animal_id, event_type_t type, const char *description);

// =============================================================================
// Alerts Operations
// =============================================================================

/**
 * @brief Generate a list of alerts (e.g. no feeding for > 21 days).
 * 
 * @param out_list List of alert strings.
 * @param out_count Count of alerts.
 * @return esp_err_t 
 */
esp_err_t core_get_alerts(char ***out_list, size_t *out_count);

void core_free_alert_list(char **list, size_t count);

// =============================================================================
// Document Operations
// =============================================================================

esp_err_t core_save_document(const document_t *doc);
esp_err_t core_generate_report(const char *animal_id);
esp_err_t core_list_reports(char ***out_list, size_t *out_count);
void core_free_report_list(char **list, size_t count);

// =============================================================================
// Logging Operations
// =============================================================================

esp_err_t core_log_event(log_level_t level, const char *module, const char *message);
esp_err_t core_get_logs(char ***out_list, size_t *out_count, size_t max_lines);
void core_free_log_list(char **list, size_t count);

#ifdef __cplusplus
}
#endif