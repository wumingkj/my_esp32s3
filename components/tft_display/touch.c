#include "touch.h"

#include "driver/spi_master.h"
#include "esp_log.h"

// 移除未使用的变量
// static spi_device_handle_t touch_spi = NULL;  // 注释掉或删除这行

esp_err_t touch_init(int cs_pin, int clk_pin, int din_pin, int dout_pin, int irq_pin) {
    // 移除未使用的ret变量
    // esp_err_t ret;  // 注释掉或删除这行

    ESP_LOGI("TOUCH", "Touch initialized");
    return ESP_OK;
}

// 简化的触摸读取（模拟数据）
esp_err_t touch_read(uint16_t* x, uint16_t* y, uint8_t* pressed) {
    // 这里返回模拟的触摸数据
    // 在实际应用中，你需要根据你的触摸芯片型号实现真正的读取逻辑
    *x = 120;      // 屏幕中心X
    *y = 160;      // 屏幕中心Y
    *pressed = 0;  // 默认未按下

    return ESP_OK;
}