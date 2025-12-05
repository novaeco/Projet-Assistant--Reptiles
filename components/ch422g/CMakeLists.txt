idf_component_register(SRCS "ch422g.c"
                      INCLUDE_DIRS "."
                      PRIV_INCLUDE_DIRS "../core/include"
                      REQUIRES esp_driver_i2c)
