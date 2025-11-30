idf_component_register(SRCS "src/reptile_storage.c"
                       INCLUDE_DIRS "include"
                       REQUIRES nvs_flash board cjson)
