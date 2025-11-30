idf_component_register(SRCS "src/ui.c" "src/ui_dashboard.c" "src/ui_animals.c" "src/ui_animal_form.c" "src/ui_animal_details.c" "src/ui_settings.c" "src/ui_documents.c" "src/ui_web.c" "src/ui_logs.c" "src/ui_alerts.c" "src/ui_reproduction.c"
                       INCLUDE_DIRS "include"
                       REQUIRES lvgl board esp_timer esp_lcd esp_lcd_touch core net reptile_storage)
