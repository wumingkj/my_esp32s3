idf_component_register(
    SRCS "ws2812_led.c"
    INCLUDE_DIRS "."
    REQUIRES led_strip freertos
)