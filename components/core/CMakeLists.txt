idf_component_register(SRCS "src/core_service.c" "src/core_export.c"
                       INCLUDE_DIRS "include"
                       REQUIRES reptile_storage cjson)