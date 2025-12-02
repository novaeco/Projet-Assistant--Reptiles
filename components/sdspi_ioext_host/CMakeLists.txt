idf_component_register(SRCS "sdspi_ioext_host.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_driver_spi sdmmc
                       PRIV_REQUIRES log)
