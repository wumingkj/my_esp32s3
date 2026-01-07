idf_component_register(SRCS "partition_manager.c"
                       INCLUDE_DIRS "."
                       REQUIRES nvs_flash esp_pm)