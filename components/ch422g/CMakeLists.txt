idf_component_register(SRCS "ch422g.c"
                      INCLUDE_DIRS "."
                      REQUIRES esp_driver_i2c
                      PRIV_REQUIRES core)
