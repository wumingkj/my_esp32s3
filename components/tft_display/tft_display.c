#include "tft_display.h"

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_lcd_ili9341.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "touch.h"

static const char* TAG = "TFT_Display";

static esp_lcd_panel_io_handle_t lcd_io = NULL;
static esp_lcd_panel_handle_t lcd_panel = NULL;
static lv_display_t* lvgl_disp = NULL;
static lv_indev_t* touch_indev = NULL;

// 定义兼容的宏（修复宏定义不匹配问题）
#define TFT_MOSI TFT_SPI_MOSI
#define TFT_MISO TFT_SPI_MISO
#define TFT_SCLK TFT_SPI_SCLK
#define TFT_CS TFT_SPI_CS
#define TFT_DC TFT_SPI_DC
#define TFT_RST TFT_SPI_RST
#define TFT_BL TFT_SPI_BL

#define TOUCH_CS TOUCH_SPI_CS
#define TOUCH_CLK TOUCH_SPI_CLK
#define TOUCH_DIN TOUCH_SPI_DIN
#define TOUCH_DO TOUCH_SPI_DOUT
#define TOUCH_IRQ TOUCH_SPI_IRQ

// SPI总线配置
static spi_bus_config_t buscfg = {
    .mosi_io_num = TFT_MOSI,
    .miso_io_num = TFT_MISO,
    .sclk_io_num = TFT_SCLK,
    .quadwp_io_num = -1,
    .quadhd_io_num = -1,
    .max_transfer_sz = TFT_WIDTH * TFT_HEIGHT * 2,
};

// LCD IO配置
static esp_lcd_panel_io_spi_config_t io_config = {
    .dc_gpio_num = TFT_DC,
    .cs_gpio_num = TFT_CS,
    .pclk_hz = 40 * 1000 * 1000,
    .lcd_cmd_bits = 8,
    .lcd_param_bits = 8,
    .spi_mode = 0,
    .trans_queue_depth = 10,
};

// ILI9341厂商特定配置（使用默认初始化命令）
static ili9341_vendor_config_t vendor_config = {
    .init_cmds = NULL,  // 使用默认初始化命令
    .init_cmds_size = 0,
};

// LCD面板配置 (ILI9341)
static esp_lcd_panel_dev_config_t panel_config = {
    .reset_gpio_num = TFT_RST,
    .color_space = ESP_LCD_COLOR_SPACE_RGB,
    .bits_per_pixel = 16,
    .vendor_config = &vendor_config,  // 添加厂商特定配置
};

// 触摸读取回调函数 (LVGL v9版本)
static void touch_read_cb(lv_indev_t* indev, lv_indev_data_t* data) {
    uint16_t touch_x, touch_y;
    uint8_t touch_pressed = 0;

    // 读取触摸数据
    if (touch_read(&touch_x, &touch_y, &touch_pressed) == ESP_OK) {
        data->point.x = touch_x;
        data->point.y = touch_y;
        data->state = touch_pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

// 初始化TFT显示
esp_err_t tft_display_init(void) {
    esp_err_t ret;

    ESP_LOGI(TAG, "Initializing TFT display...");
    ESP_LOGI(TAG, "SPI Config: MOSI=%d, MISO=%d, SCLK=%d, CS=%d, DC=%d, RST=%d", TFT_MOSI, TFT_MISO, TFT_SCLK, TFT_CS,
             TFT_DC, TFT_RST);

    // 初始化SPI总线
    ret = spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SPI bus initialization failed: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "SPI bus initialized successfully");

    // 初始化LCD IO
    ret = esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI2_HOST, &io_config, &lcd_io);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "LCD IO initialization failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // 初始化LCD面板（使用ILI9341驱动）
    ret = esp_lcd_new_panel_ili9341(lcd_io, &panel_config, &lcd_panel);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "LCD panel initialization failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // 初始化LCD面板
    esp_lcd_panel_init(lcd_panel);
    esp_lcd_panel_invert_color(lcd_panel, true);
    esp_lcd_panel_mirror(lcd_panel, true, false);
    esp_lcd_panel_disp_on_off(lcd_panel, true);

    // 测试LCD面板 - 绘制一个简单的测试图案
    uint16_t test_color = 0xF800;  // 红色
    uint16_t test_buffer[TFT_WIDTH];
    for (int i = 0; i < TFT_WIDTH; i++) {
        test_buffer[i] = test_color;
    }

    // 绘制红色横条
    esp_lcd_panel_draw_bitmap(lcd_panel, 0, 0, TFT_WIDTH, 10, test_buffer);
    ESP_LOGI(TAG, "LCD test pattern drawn");

    // 初始化LVGL端口
    const lvgl_port_cfg_t lvgl_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    ret = lvgl_port_init(&lvgl_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "LVGL port initialization failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // 添加LCD显示设备到LVGL端口（使用LVGL v9原生接口）
    const lvgl_port_display_cfg_t disp_cfg = {
        .io_handle = lcd_io,
        .panel_handle = lcd_panel,
        .buffer_size = TFT_WIDTH * 50,  // 缓冲区大小
        .double_buffer = true,          // 双缓冲
        .hres = TFT_WIDTH,
        .vres = TFT_HEIGHT,
        .monochrome = false,
        .rotation =
            {
                .swap_xy = false,
                .mirror_x = true,
                .mirror_y = false,
            },
        .flags =
            {
                .buff_dma = true,
            },
    };

    lvgl_disp = lvgl_port_add_disp(&disp_cfg);
    if (lvgl_disp == NULL) {
        ESP_LOGE(TAG, "Failed to add display to LVGL port");
        return ESP_FAIL;
    }

    // 设置默认屏幕为白色背景
    lv_obj_t* screen = lv_disp_get_scr_act(lvgl_disp);
    lv_obj_set_style_bg_color(screen, lv_color_white(), LV_PART_MAIN);

    // 初始化背光
    gpio_config_t backlight_config = {
        .pin_bit_mask = (1ULL << TFT_BL),
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_config(&backlight_config);
    tft_set_backlight(true);

    ESP_LOGI(TAG, "TFT display initialized successfully with esp_lvgl_port");
    return ESP_OK;
}

// 初始化触摸屏
esp_err_t tft_touch_init(void) {
    ESP_LOGI(TAG, "Initializing touch screen...");

    // 初始化触摸驱动
    esp_err_t ret = touch_init(TOUCH_CS, TOUCH_CLK, TOUCH_DIN, TOUCH_DO, TOUCH_IRQ);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Touch initialization failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // 使用LVGL v9原生接口添加触摸输入设备
    touch_indev = lv_indev_create();
    if (touch_indev == NULL) {
        ESP_LOGE(TAG, "Failed to create touch input device");
        return ESP_FAIL;
    }

    lv_indev_set_type(touch_indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(touch_indev, touch_read_cb);

    ESP_LOGI(TAG, "Touch screen initialized successfully");
    return ESP_OK;
}

// 设置背光
esp_err_t tft_set_backlight(bool on) {
    gpio_set_level(TFT_BL, on ? 1 : 0);
    return ESP_OK;
}

// 释放资源
esp_err_t tft_display_deinit(void) {
    if (touch_indev) {
        lv_indev_delete(touch_indev);
        touch_indev = NULL;
    }

    if (lvgl_disp) {
        lvgl_port_remove_disp(lvgl_disp);
        lvgl_disp = NULL;
    }

    lvgl_port_deinit();

    if (lcd_panel) {
        esp_lcd_panel_del(lcd_panel);
        lcd_panel = NULL;
    }

    if (lcd_io) {
        esp_lcd_panel_io_del(lcd_io);
        lcd_io = NULL;
    }

    spi_bus_free(SPI2_HOST);

    ESP_LOGI(TAG, "TFT display deinitialized");
    return ESP_OK;
}