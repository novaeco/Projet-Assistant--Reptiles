#include "core_service.h"
#include "reptile_storage.h"
#include "esp_log.h"
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <sys/stat.h>
#include <dirent.h>
#include <stdlib.h>
#include <ctype.h>

static const char *TAG = "CORE";
#define ANIMAL_DIR "/sdcard/animals"
#define REPORT_DIR "/sdcard/reports"
#define LOG_FILE   "/sdcard/audit.log"

static void ensure_dirs(void) {
    struct stat st = {0};
    if (stat(ANIMAL_DIR, &st) == -1) mkdir(ANIMAL_DIR, 0700);
    if (stat(REPORT_DIR, &st) == -1) mkdir(REPORT_DIR, 0700);
}

esp_err_t core_init(void) {
    ESP_LOGI(TAG, "Initializing Core Service...");
    ensure_dirs();
    return ESP_OK;
}

void core_free_animal_content(animal_t *animal) {
    if (animal) {
        if (animal->weights) { free(animal->weights); animal->weights = NULL; }
        if (animal->events) { free(animal->events); animal->events = NULL; }
        animal->weight_count = 0;
        animal->event_count = 0;
    }
}

// ... (Previous Animal/History/Doc/Log functions assumed present. I will re-implement the modified ones) ...
// I need to include core_save_animal, core_get_animal, etc. again because I am overwriting the file.
// To save space/tokens, I will assume the previous implementations are correct and just add the new ones + list_animals with search logic if needed?
// Actually, list_animals is used by search, or I can make a helper.
// Let's rewrite the full file to be safe and correct.

esp_err_t core_save_animal(const animal_t *animal) {
    if (!animal || strlen(animal->id) == 0) return ESP_ERR_INVALID_ARG;
    ensure_dirs();
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "id", animal->id);
    cJSON_AddStringToObject(root, "name", animal->name);
    cJSON_AddStringToObject(root, "species", animal->species);
    cJSON_AddNumberToObject(root, "sex", animal->sex);
    cJSON_AddNumberToObject(root, "dob", animal->dob);
    cJSON_AddStringToObject(root, "origin", animal->origin);
    cJSON_AddStringToObject(root, "registry_id", animal->registry_id);
    cJSON_AddBoolToObject(root, "is_deleted", animal->is_deleted);

    if (animal->weight_count > 0 && animal->weights) {
        cJSON *w_array = cJSON_CreateArray();
        for (size_t i = 0; i < animal->weight_count; i++) {
            cJSON *item = cJSON_CreateObject();
            cJSON_AddNumberToObject(item, "date", animal->weights[i].date);
            cJSON_AddNumberToObject(item, "value", animal->weights[i].value);
            cJSON_AddStringToObject(item, "unit", animal->weights[i].unit);
            cJSON_AddItemToArray(w_array, item);
        }
        cJSON_AddItemToObject(root, "weights", w_array);
    }
    if (animal->event_count > 0 && animal->events) {
        cJSON *e_array = cJSON_CreateArray();
        for (size_t i = 0; i < animal->event_count; i++) {
            cJSON *item = cJSON_CreateObject();
            cJSON_AddNumberToObject(item, "date", animal->events[i].date);
            cJSON_AddNumberToObject(item, "type", animal->events[i].type);
            cJSON_AddStringToObject(item, "desc", animal->events[i].description);
            cJSON_AddItemToArray(e_array, item);
        }
        cJSON_AddItemToObject(root, "events", e_array);
    }

    char filepath[512];
    int n = snprintf(filepath, sizeof(filepath), "%s/%s.json", ANIMAL_DIR, animal->id);
    if (n < 0 || n >= (int)sizeof(filepath)) {
        ESP_LOGW(TAG, "Path too long, skipping save: dir=%s id=%s", ANIMAL_DIR, animal->id);
        cJSON_Delete(root);
        return ESP_ERR_INVALID_SIZE;
    }
    esp_err_t ret = storage_json_save(filepath, root);
    cJSON_Delete(root);
    if (ret == ESP_OK) core_log_event(LOG_LEVEL_AUDIT, "CORE", "Animal saved");
    return ret;
}

esp_err_t core_get_animal(const char *id, animal_t *out_animal) {
    char filepath[512];
    int n = snprintf(filepath, sizeof(filepath), "%s/%s.json", ANIMAL_DIR, id);
    if (n < 0 || n >= (int)sizeof(filepath)) {
        ESP_LOGW(TAG, "Path too long when loading animal: dir=%s id=%s", ANIMAL_DIR, id);
        return ESP_ERR_INVALID_SIZE;
    }
    cJSON *root = storage_json_load(filepath);
    if (!root) return ESP_FAIL;
    memset(out_animal, 0, sizeof(animal_t));

    cJSON *item = cJSON_GetObjectItem(root, "id"); if (item) strncpy(out_animal->id, item->valuestring, 36);
    item = cJSON_GetObjectItem(root, "name"); if (item) strncpy(out_animal->name, item->valuestring, 63);
    item = cJSON_GetObjectItem(root, "species"); if (item) strncpy(out_animal->species, item->valuestring, 127);
    item = cJSON_GetObjectItem(root, "sex"); if (item) out_animal->sex = (animal_sex_t)item->valueint;
    item = cJSON_GetObjectItem(root, "dob"); if (item) out_animal->dob = item->valueint;
    item = cJSON_GetObjectItem(root, "origin"); if (item) strncpy(out_animal->origin, item->valuestring, 15);
    item = cJSON_GetObjectItem(root, "registry_id"); if (item) strncpy(out_animal->registry_id, item->valuestring, 31);
    item = cJSON_GetObjectItem(root, "is_deleted"); if (item) out_animal->is_deleted = cJSON_IsTrue(item);

    cJSON *weights = cJSON_GetObjectItem(root, "weights");
    if (weights && cJSON_IsArray(weights)) {
        int count = cJSON_GetArraySize(weights);
        if (count > 0) {
            out_animal->weights = malloc(count * sizeof(weight_record_t));
            out_animal->weight_count = count;
            for (int i = 0; i < count; i++) {
                cJSON *w = cJSON_GetArrayItem(weights, i);
                cJSON *d = cJSON_GetObjectItem(w, "date");
                cJSON *v = cJSON_GetObjectItem(w, "value");
                cJSON *u = cJSON_GetObjectItem(w, "unit");
                if (d) out_animal->weights[i].date = d->valueint;
                if (v) out_animal->weights[i].value = v->valuedouble;
                if (u) strncpy(out_animal->weights[i].unit, u->valuestring, 7);
            }
        }
    }
    cJSON *events = cJSON_GetObjectItem(root, "events");
    if (events && cJSON_IsArray(events)) {
        int count = cJSON_GetArraySize(events);
        if (count > 0) {
            out_animal->events = malloc(count * sizeof(event_record_t));
            out_animal->event_count = count;
            for (int i = 0; i < count; i++) {
                cJSON *e = cJSON_GetArrayItem(events, i);
                cJSON *d = cJSON_GetObjectItem(e, "date");
                cJSON *t = cJSON_GetObjectItem(e, "type");
                cJSON *desc = cJSON_GetObjectItem(e, "desc");
                if (d) out_animal->events[i].date = d->valueint;
                if (t) out_animal->events[i].type = (event_type_t)t->valueint;
                if (desc) strncpy(out_animal->events[i].description, desc->valuestring, 63);
            }
        }
    }
    cJSON_Delete(root);
    return ESP_OK;
}

// Helper for case-insensitive substring search
static int str_contains_ignore_case(const char *haystack, const char *needle) {
    if (!needle || !*needle) return 1;
    if (!haystack || !*haystack) return 0;
    
    char *h = strdup(haystack);
    char *n = strdup(needle);
    for(int i=0; h[i]; i++) h[i] = tolower((unsigned char)h[i]);
    for(int i=0; n[i]; i++) n[i] = tolower((unsigned char)n[i]);
    
    int ret = (strstr(h, n) != NULL);
    free(h); free(n);
    return ret;
}

esp_err_t core_list_animals(animal_summary_t **out_list, size_t *out_count) {
    return core_search_animals(NULL, out_list, out_count);
}

esp_err_t core_search_animals(const char *query, animal_summary_t **out_list, size_t *out_count) {
    *out_list = NULL; *out_count = 0;
    DIR *dir = opendir(ANIMAL_DIR);
    if (!dir) return ESP_FAIL;
    
    // First pass: count matches
    size_t count = 0; struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strstr(entry->d_name, ".json")) {
            if (query == NULL || strlen(query) == 0) {
                count++;
            } else {
                // Need to load to check name/species
                char filepath[512];
                int path_len = snprintf(filepath, sizeof(filepath), "%s/%s", ANIMAL_DIR, entry->d_name);
                if (path_len < 0 || path_len >= (int)sizeof(filepath)) {
                    ESP_LOGW(TAG, "Path too long, skipping entry: dir=%s name=%s", ANIMAL_DIR, entry->d_name);
                    continue;
                }
                cJSON *root = storage_json_load(filepath);
                if (root) {
                    cJSON *name = cJSON_GetObjectItem(root, "name");
                    cJSON *species = cJSON_GetObjectItem(root, "species");
                    if (name && species) {
                        if (str_contains_ignore_case(name->valuestring, query) || 
                            str_contains_ignore_case(species->valuestring, query)) {
                            count++;
                        }
                    }
                    cJSON_Delete(root);
                }
            }
        }
    }
    rewinddir(dir);

    if (count == 0) { closedir(dir); return ESP_OK; }
    animal_summary_t *list = malloc(count * sizeof(animal_summary_t));
    if (!list) { closedir(dir); return ESP_ERR_NO_MEM; }

    size_t idx = 0;
    while ((entry = readdir(dir)) != NULL) {
        if (strstr(entry->d_name, ".json")) {
            char filepath[512];
            int path_len = snprintf(filepath, sizeof(filepath), "%s/%s", ANIMAL_DIR, entry->d_name);
            if (path_len < 0 || path_len >= (int)sizeof(filepath)) {
                ESP_LOGW(TAG, "Path too long, skipping entry: dir=%s name=%s", ANIMAL_DIR, entry->d_name);
                continue;
            }
            cJSON *root = storage_json_load(filepath);
            if (root) {
                if (!cJSON_IsTrue(cJSON_GetObjectItem(root, "is_deleted"))) {
                    cJSON *id = cJSON_GetObjectItem(root, "id");
                    cJSON *name = cJSON_GetObjectItem(root, "name");
                    cJSON *species = cJSON_GetObjectItem(root, "species");
                    
                    bool match = false;
                    if (query == NULL || strlen(query) == 0) match = true;
                    else if (name && species) {
                        if (str_contains_ignore_case(name->valuestring, query) || 
                            str_contains_ignore_case(species->valuestring, query)) {
                            match = true;
                        }
                    }

                    if (match && id && name && species) {
                        strncpy(list[idx].id, id->valuestring, 36); list[idx].id[36]=0;
                        strncpy(list[idx].name, name->valuestring, 63); list[idx].name[63]=0;
                        strncpy(list[idx].species, species->valuestring, 127); list[idx].species[127]=0;
                        idx++;
                    }
                }
                cJSON_Delete(root);
            }
        }
    }
    closedir(dir);
    *out_list = list; *out_count = idx;
    return ESP_OK;
}

void core_free_animal_list(animal_summary_t *list) { if (list) free(list); }

esp_err_t core_add_weight(const char *animal_id, float weight, const char *unit) {
    animal_t animal;
    if (core_get_animal(animal_id, &animal) != ESP_OK) return ESP_FAIL;
    size_t new_count = animal.weight_count + 1;
    weight_record_t *new_weights = realloc(animal.weights, new_count * sizeof(weight_record_t));
    if (!new_weights) { core_free_animal_content(&animal); return ESP_ERR_NO_MEM; }
    animal.weights = new_weights;
    animal.weights[animal.weight_count].date = time(NULL);
    animal.weights[animal.weight_count].value = weight;
    strncpy(animal.weights[animal.weight_count].unit, unit, 7);
    animal.weight_count = new_count;
    esp_err_t ret = core_save_animal(&animal);
    core_free_animal_content(&animal);
    return ret;
}

esp_err_t core_add_event(const char *animal_id, event_type_t type, const char *description) {
    animal_t animal;
    if (core_get_animal(animal_id, &animal) != ESP_OK) return ESP_FAIL;
    size_t new_count = animal.event_count + 1;
    event_record_t *new_events = realloc(animal.events, new_count * sizeof(event_record_t));
    if (!new_events) { core_free_animal_content(&animal); return ESP_ERR_NO_MEM; }
    animal.events = new_events;
    animal.events[animal.event_count].date = time(NULL);
    animal.events[animal.event_count].type = type;
    strncpy(animal.events[animal.event_count].description, description, 63);
    animal.event_count = new_count;
    esp_err_t ret = core_save_animal(&animal);
    core_free_animal_content(&animal);
    return ret;
}

esp_err_t core_get_alerts(char ***out_list, size_t *out_count) {
    *out_list = NULL; *out_count = 0;
    DIR *dir = opendir(ANIMAL_DIR);
    if (!dir) return ESP_FAIL;

    // We can't easily pre-count alerts without loading files.
    // So we'll use a dynamic list approach or just 2 passes. 2 passes is safer for memory on embedded.
    
    size_t count = 0; struct dirent *entry;
    time_t now = time(NULL);
    const double ALERT_SECONDS = 21 * 24 * 3600; // 21 days

    // Pass 1: Count
    while ((entry = readdir(dir)) != NULL) {
        if (strstr(entry->d_name, ".json")) {
            char filepath[512];
            int path_len = snprintf(filepath, sizeof(filepath), "%s/%s", ANIMAL_DIR, entry->d_name);
            if (path_len < 0 || path_len >= (int)sizeof(filepath)) {
                ESP_LOGW(TAG, "Path too long, skipping entry: dir=%s name=%s", ANIMAL_DIR, entry->d_name);
                continue;
            }
            cJSON *root = storage_json_load(filepath);
            if (root) {
                if (!cJSON_IsTrue(cJSON_GetObjectItem(root, "is_deleted"))) {
                    cJSON *events = cJSON_GetObjectItem(root, "events");
                    time_t last_feed = 0;
                    if (events && cJSON_IsArray(events)) {
                        int n = cJSON_GetArraySize(events);
                        for (int i=0; i<n; i++) {
                            cJSON *e = cJSON_GetArrayItem(events, i);
                            cJSON *t = cJSON_GetObjectItem(e, "type");
                            cJSON *d = cJSON_GetObjectItem(e, "date");
                            if (t && t->valueint == EVENT_FEEDING && d) {
                                if (d->valueint > last_feed) last_feed = d->valueint;
                            }
                        }
                    }
                    // If never fed or fed long ago
                    if (last_feed == 0 || difftime(now, last_feed) > ALERT_SECONDS) {
                        count++;
                    }
                }
                cJSON_Delete(root);
            }
        }
    }
    rewinddir(dir);

    if (count == 0) { closedir(dir); return ESP_OK; }
    char **list = malloc(count * sizeof(char*));
    
    size_t idx = 0;
    while ((entry = readdir(dir)) != NULL) {
        if (strstr(entry->d_name, ".json")) {
            char filepath[512];
            int path_len = snprintf(filepath, sizeof(filepath), "%s/%s", ANIMAL_DIR, entry->d_name);
            if (path_len < 0 || path_len >= (int)sizeof(filepath)) {
                ESP_LOGW(TAG, "Path too long, skipping entry: dir=%s name=%s", ANIMAL_DIR, entry->d_name);
                continue;
            }
            cJSON *root = storage_json_load(filepath);
            if (root) {
                if (!cJSON_IsTrue(cJSON_GetObjectItem(root, "is_deleted"))) {
                    cJSON *events = cJSON_GetObjectItem(root, "events");
                    time_t last_feed = 0;
                    if (events && cJSON_IsArray(events)) {
                        int n = cJSON_GetArraySize(events);
                        for (int i=0; i<n; i++) {
                            cJSON *e = cJSON_GetArrayItem(events, i);
                            cJSON *t = cJSON_GetObjectItem(e, "type");
                            cJSON *d = cJSON_GetObjectItem(e, "date");
                            if (t && t->valueint == EVENT_FEEDING && d) {
                                if (d->valueint > last_feed) last_feed = d->valueint;
                            }
                        }
                    }
                    
                    if (last_feed == 0 || difftime(now, last_feed) > ALERT_SECONDS) {
                        cJSON *name = cJSON_GetObjectItem(root, "name");
                        char buf[128];
                        int days = (last_feed == 0) ? -1 : (int)(difftime(now, last_feed) / (24*3600));
                        if (days == -1) snprintf(buf, sizeof(buf), "%s: Jamais nourri", name ? name->valuestring : "?");
                        else snprintf(buf, sizeof(buf), "%s: Jeun de %d jours", name ? name->valuestring : "?", days);
                        
                        list[idx] = strdup(buf);
                        idx++;
                    }
                }
                cJSON_Delete(root);
            }
        }
    }
    closedir(dir);
    *out_list = list; *out_count = idx;
    return ESP_OK;
}

void core_free_alert_list(char **list, size_t count) {
    if (list) {
        for (size_t i = 0; i < count; i++) free(list[i]);
        free(list);
    }
}

esp_err_t core_save_document(const document_t *doc) { return ESP_OK; }

esp_err_t core_generate_report(const char *animal_id) {
    animal_t animal;
    if (core_get_animal(animal_id, &animal) != ESP_OK) return ESP_FAIL;
    ensure_dirs();
    char filepath[512];
    int path_len = snprintf(filepath, sizeof(filepath), "%s/Report_%s.txt", REPORT_DIR, animal.name);
    if (path_len < 0 || path_len >= (int)sizeof(filepath)) {
        ESP_LOGW(TAG, "Path too long for report: dir=%s name=%s", REPORT_DIR, animal.name);
        core_free_animal_content(&animal);
        return ESP_ERR_INVALID_SIZE;
    }
    FILE *f = fopen(filepath, "w");
    if (!f) { core_free_animal_content(&animal); return ESP_FAIL; }
    fprintf(f, "FICHE D'IDENTIFICATION\n======================\n\nNom: %s\nEspece: %s\n", animal.name, animal.species);
    fprintf(f, "Sexe: %s\nOrigine: %s\nI-FAP: %s\n", (animal.sex==SEX_MALE?"Male":(animal.sex==SEX_FEMALE?"Femelle":"Inconnu")), animal.origin, animal.registry_id);
    fprintf(f, "\n--- Historique Poids ---\n");
    if (animal.weights) for(size_t i=0; i<animal.weight_count; i++) fprintf(f, "- %.1f %s (ts: %lu)\n", animal.weights[i].value, animal.weights[i].unit, (unsigned long)animal.weights[i].date);
    fprintf(f, "\n--- Evenements ---\n");
    if (animal.events) for(size_t i=0; i<animal.event_count; i++) fprintf(f, "- [%d] %s (ts: %lu)\n", animal.events[i].type, animal.events[i].description, (unsigned long)animal.events[i].date);
    fprintf(f, "\nGenere le: %lu\n", (unsigned long)time(NULL));
    fclose(f);
    core_free_animal_content(&animal);
    core_log_event(LOG_LEVEL_INFO, "CORE", "Report generated");
    return ESP_OK;
}

esp_err_t core_list_reports(char ***out_list, size_t *out_count) {
    *out_list = NULL; *out_count = 0;
    DIR *dir = opendir(REPORT_DIR);
    if (!dir) return ESP_FAIL;
    size_t count = 0; struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) { if (entry->d_type == DT_REG || entry->d_type == DT_UNKNOWN) count++; }
    rewinddir(dir);
    if (count == 0) { closedir(dir); return ESP_OK; }
    char **list = malloc(count * sizeof(char*));
    size_t idx = 0;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_type == DT_REG || entry->d_type == DT_UNKNOWN) { list[idx] = strdup(entry->d_name); idx++; }
    }
    closedir(dir);
    *out_list = list; *out_count = idx;
    return ESP_OK;
}
void core_free_report_list(char **list, size_t count) { if(list){ for(size_t i=0; i<count; i++) free(list[i]); free(list); } }

esp_err_t core_log_event(log_level_t level, const char *module, const char *message) {
    log_entry_t entry; entry.timestamp = time(NULL); entry.level = level;
    strncpy(entry.module, module, 15); strncpy(entry.message, message, 127);
    char log_line[256]; snprintf(log_line, sizeof(log_line), "%lu|%d|%s|%s\n", (unsigned long)entry.timestamp, entry.level, entry.module, entry.message);
    ESP_LOGI(TAG, "AUDIT: %s", entry.message);
    FILE *f = fopen(LOG_FILE, "a"); if (f) { fprintf(f, "%s", log_line); fclose(f); }
    return ESP_OK;
}

esp_err_t core_get_logs(char ***out_list, size_t *out_count, size_t max_lines) {
    *out_list = NULL; *out_count = 0;
    FILE *f = fopen(LOG_FILE, "r"); if (!f) return ESP_OK;
    size_t lines = 0; char buffer[256]; while (fgets(buffer, sizeof(buffer), f)) lines++;
    size_t start_line = (lines > max_lines) ? (lines - max_lines) : 0;
    size_t count_to_read = (lines > max_lines) ? max_lines : lines;
    if (count_to_read == 0) { fclose(f); return ESP_OK; }
    char **list = malloc(count_to_read * sizeof(char*));
    rewind(f); size_t current_line = 0; size_t idx = 0;
    while (fgets(buffer, sizeof(buffer), f)) {
        if (current_line >= start_line) {
            size_t len = strlen(buffer); if (len > 0 && buffer[len-1] == '\n') buffer[len-1] = 0;
            list[idx] = strdup(buffer); idx++;
        }
        current_line++;
    }
    fclose(f); *out_list = list; *out_count = idx;
    return ESP_OK;
}
void core_free_log_list(char **list, size_t count) { if(list){ for(size_t i=0; i<count; i++) free(list[i]); free(list); } }