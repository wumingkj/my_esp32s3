idf_component_register(SRCS "tft_display.c" "touch.c"
                    INCLUDE_DIRS "."
                    REQUIRES lvgl esp_lvgl_port driver esp_lcd_ili9341)