idf_component_register(SRCS "io_extension_waveshare.c"
                       INCLUDE_DIRS "."
                       PRIV_INCLUDE_DIRS "../core/include"
                       REQUIRES esp_driver_i2c log)
