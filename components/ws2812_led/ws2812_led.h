#ifndef WS2812_LED_H
#define WS2812_LED_H

#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "led_strip.h"

#ifdef __cplusplus
extern "C" {
#endif

// 默认配置
#define WS2812_PIN             48          /*!< WS2812控制引脚 GPIO48 */
#define WS2812_NUM_LEDS        0          /*!< LED灯珠总数 1个 */

// 颜色结构体
typedef struct {
    uint8_t r;  /*!< 红色分量 (0-255) */
    uint8_t g;  /*!< 绿色分量 (0-255) */
    uint8_t b;  /*!< 蓝色分量 (0-255) */
} rgb_color_t;

// HSV颜色结构体
typedef struct {
    float h;    /*!< 色相 (0-360) */
    float s;    /*!< 饱和度 (0-1) */
    float v;    /*!< 明度 (0-1) */
} hsv_color_t;

// LED模式枚举
typedef enum {
    LED_MODE_OFF = 0,          /*!< 关闭模式 */
    LED_MODE_SOLID,            /*!< 单色模式 */
    LED_MODE_RAINBOW,          /*!< 彩虹模式 */
    LED_MODE_BREATHING,        /*!< 呼吸模式 */
    LED_MODE_WATER_FLOW        /*!< 流水灯模式 */
} led_mode_t;

// WS2812配置结构体
typedef struct {
    int pin;                    /*!< GPIO引脚 */
    int num_leds;               /*!< LED灯珠数量 */
    led_strip_handle_t strip;   /*!< LED灯带句柄 */
    led_mode_t current_mode;    /*!< 当前模式 */
    TaskHandle_t effect_task;   /*!< 特效任务句柄 */
    bool is_running;            /*!< 是否运行中 */
} ws2812_config_t;

/**
 * @brief 初始化WS2812 LED控制
 * 
 * @param config 配置参数，如果为NULL则使用默认配置
 * @return esp_err_t ESP_OK表示成功，其他表示失败
 */
esp_err_t ws2812_led_init(const ws2812_config_t *config);

/**
 * @brief 设置单个LED颜色
 * 
 * @param led_index LED索引 (0-60)
 * @param color 颜色值
 * @return esp_err_t ESP_OK表示成功，其他表示失败
 */
esp_err_t ws2812_set_led_color(int led_index, rgb_color_t color);

/**
 * @brief 设置所有LED颜色
 * 
 * @param color 颜色值
 * @return esp_err_t ESP_OK表示成功，其他表示失败
 */
esp_err_t ws2812_set_all_color(rgb_color_t color);

/**
 * @brief 清除所有LED（关闭）
 * 
 * @return esp_err_t ESP_OK表示成功，其他表示失败
 */
esp_err_t ws2812_clear_all(void);

/**
 * @brief 更新LED显示
 * 
 * @return esp_err_t ESP_OK表示成功，其他表示失败
 */
esp_err_t ws2812_update(void);

/**
 * @brief HSV转RGB颜色转换
 * 
 * @param hsv HSV颜色
 * @param rgb RGB颜色输出
 */
void hsv_to_rgb(hsv_color_t hsv, rgb_color_t *rgb);

/**
 * @brief RGB转HSV颜色转换
 * 
 * @param rgb RGB颜色
 * @param hsv HSV颜色输出
 */
void rgb_to_hsv(rgb_color_t rgb, hsv_color_t *hsv);

/**
 * @brief 彩虹渐变效果
 * 
 * @param speed 速度参数 (1-10)
 * @return esp_err_t ESP_OK表示成功，其他表示失败
 */
esp_err_t ws2812_rainbow_effect(int speed);

/**
 * @brief 呼吸灯效果
 * 
 * @param color 基础颜色
 * @param speed 速度参数 (1-10)
 * @return esp_err_t ESP_OK表示成功，其他表示失败
 */
esp_err_t ws2812_breathing_effect(rgb_color_t color, int speed);

/**
 * @brief 七彩流水灯效果
 * 
 * @param speed 速度参数 (1-10)
 * @return esp_err_t ESP_OK表示成功，其他表示失败
 */
esp_err_t ws2812_water_flow_effect(int speed);

/**
 * @brief 停止WS2812控制并释放资源
 * 
 * @return esp_err_t ESP_OK表示成功，其他表示失败
 */
esp_err_t ws2812_led_deinit(void);

#ifdef __cplusplus
}
#endif

#endif /* WS2812_LED_H */