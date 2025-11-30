#include "core_export.h"
#include "core_service.h"
#include "core_models.h"
#include <stdio.h>

esp_err_t core_export_csv(const char *filename)
{
    FILE *f = fopen(filename, "w");
    if (!f) return ESP_FAIL;

    // Header
    fprintf(f, "ID,Name,Species,Sex,Origin,RegistryID\n");

    animal_summary_t *list = NULL;
    size_t count = 0;
    if (core_list_animals(&list, &count) == ESP_OK) {
        for (size_t i = 0; i < count; i++) {
            animal_t a;
            if (core_get_animal(list[i].id, &a) == ESP_OK) {
                fprintf(f, "%s,%s,%s,%d,%s,%s\n", 
                    a.id, a.name, a.species, a.sex, a.origin, a.registry_id);
                core_free_animal_content(&a);
            }
        }
        core_free_animal_list(list);
    }

    fclose(f);
    return ESP_OK;
}