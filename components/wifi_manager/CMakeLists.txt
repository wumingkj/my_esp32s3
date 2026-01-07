idf_component_register(SRCS "wifi_manager.c" "user_manager.c" "session_manager.c" "device_mapping.c" "whitelist_manager.c"
                    REQUIRES esp_wifi esp_netif esp_http_server lwip nvs_flash json esp_timer littlefs_manager
                    INCLUDE_DIRS ".")