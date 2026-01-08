idf_component_register(SRCS "main.c"
                    INCLUDE_DIRS "."
                    REQUIRES perfmon partition_manager frequency_manager wifi_manager littlefs_manager servo_control ws2812_led esp_driver_ledc esp_driver_gpio led_strip lvgl esp_lvgl_port esp_lcd tft_display)