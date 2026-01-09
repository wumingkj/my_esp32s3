#ifndef TFT_DISPLAY_H
#define TFT_DISPLAY_H

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_err.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lvgl_port.h"

// TFT屏幕尺寸
#define TFT_WIDTH 240
#define TFT_HEIGHT 320

// SPI引脚定义
#define TFT_SPI_MOSI 45
#define TFT_SPI_MISO 46
#define TFT_SPI_SCLK 3
#define TFT_SPI_CS 14
#define TFT_SPI_DC 47
#define TFT_SPI_RST 21
#define TFT_SPI_BL 0

// 触摸屏引脚定义
#define TOUCH_SPI_CS 1
#define TOUCH_SPI_CLK 42
#define TOUCH_SPI_DIN 2
#define TOUCH_SPI_DOUT 41
#define TOUCH_SPI_IRQ -1  // 未使用中断

// 函数声明
esp_err_t tft_display_init(void);
esp_err_t tft_touch_init(void);
esp_err_t tft_set_backlight(bool on);
esp_err_t tft_display_deinit(void);

#endif  // TFT_DISPLAY_H
