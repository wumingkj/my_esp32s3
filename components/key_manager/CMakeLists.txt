# Component CMakeLists.txt
idf_component_register(SRCS "key_manager.c"
                    INCLUDE_DIRS "."
                    REQUIRES freertos driver esp_timer)