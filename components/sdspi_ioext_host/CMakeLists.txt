idf_component_register(SRCS "sdspi_ioext_host.c"
                       INCLUDE_DIRS "include"
                       REQUIRES sdmmc esp_driver_sdmmc esp_driver_sdspi esp_driver_spi
                       PRIV_REQUIRES log freertos core)
