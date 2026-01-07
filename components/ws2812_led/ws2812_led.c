#include "ws2812_led.h"
#include "string.h"
#include "esp_check.h"

static const char *TAG = "WS2812";

// 默认配置
static ws2812_config_t default_config = {
    .pin = WS2812_PIN,
    .num_leds = WS2812_NUM_LEDS,
    .strip = NULL,
    .current_mode = LED_MODE_OFF,
    .effect_task = NULL,
    .is_running = false
};

static ws2812_config_t *ws2812_config = NULL;

/**
 * @brief HSV转RGB颜色转换
 */
void hsv_to_rgb(hsv_color_t hsv, rgb_color_t *rgb) {
    float h = hsv.h;
    float s = hsv.s;
    float v = hsv.v;
    
    int i;
    float f, p, q, t;
    
    if (s == 0) {
        // 灰色
        rgb->r = rgb->g = rgb->b = (uint8_t)(v * 255);
        return;
    }
    
    h /= 60;            // 扇形分割
    i = (int)h;
    f = h - i;          // 小数部分
    p = v * (1 - s);
    q = v * (1 - s * f);
    t = v * (1 - s * (1 - f));
    
    switch (i) {
        case 0:
            rgb->r = (uint8_t)(v * 255);
            rgb->g = (uint8_t)(t * 255);
            rgb->b = (uint8_t)(p * 255);
            break;
        case 1:
            rgb->r = (uint8_t)(q * 255);
            rgb->g = (uint8_t)(v * 255);
            rgb->b = (uint8_t)(p * 255);
            break;
        case 2:
            rgb->r = (uint8_t)(p * 255);
            rgb->g = (uint8_t)(v * 255);
            rgb->b = (uint8_t)(t * 255);
            break;
        case 3:
            rgb->r = (uint8_t)(p * 255);
            rgb->g = (uint8_t)(q * 255);
            rgb->b = (uint8_t)(v * 255);
            break;
        case 4:
            rgb->r = (uint8_t)(t * 255);
            rgb->g = (uint8_t)(p * 255);
            rgb->b = (uint8_t)(v * 255);
            break;
        default:
            rgb->r = (uint8_t)(v * 255);
            rgb->g = (uint8_t)(p * 255);
            rgb->b = (uint8_t)(q * 255);
            break;
    }
}

/**
 * @brief RGB转HSV颜色转换
 */
void rgb_to_hsv(rgb_color_t rgb, hsv_color_t *hsv) {
    float r = rgb.r / 255.0f;
    float g = rgb.g / 255.0f;
    float b = rgb.b / 255.0f;
    
    float max = (r > g) ? ((r > b) ? r : b) : ((g > b) ? g : b);
    float min = (r < g) ? ((r < b) ? r : b) : ((g < b) ? g : b);
    float delta = max - min;
    
    hsv->v = max;
    
    if (delta == 0) {
        hsv->h = 0;
        hsv->s = 0;
    } else {
        hsv->s = delta / max;
        
        if (r == max) {
            hsv->h = (g - b) / delta;
        } else if (g == max) {
            hsv->h = 2 + (b - r) / delta;
        } else {
            hsv->h = 4 + (r - g) / delta;
        }
        
        hsv->h *= 60;
        if (hsv->h < 0) {
            hsv->h += 360;
        }
    }
}

/**
 * @brief 初始化WS2812 LED控制
 */
esp_err_t ws2812_led_init(const ws2812_config_t *config) {
    esp_err_t ret = ESP_OK;
    
    // 分配配置内存
    ws2812_config = calloc(1, sizeof(ws2812_config_t));
    ESP_RETURN_ON_FALSE(ws2812_config, ESP_ERR_NO_MEM, TAG, "no mem for ws2812 config");
    
    // 复制配置或使用默认值
    if (config) {
        memcpy(ws2812_config, config, sizeof(ws2812_config_t));
    } else {
        memcpy(ws2812_config, &default_config, sizeof(ws2812_config_t));
    }
    
    // 配置LED灯带
    led_strip_config_t strip_config = {
        .strip_gpio_num = ws2812_config->pin,
        .max_leds = ws2812_config->num_leds,
        .led_pixel_format = LED_PIXEL_FORMAT_GRB,
        .led_model = LED_MODEL_WS2812,
    };
    
    led_strip_rmt_config_t rmt_config = {
        .resolution_hz = 10 * 1000 * 1000, // 10MHz
    };
    
    // 创建LED灯带
    ret = led_strip_new_rmt_device(&strip_config, &rmt_config, &ws2812_config->strip);
    ESP_RETURN_ON_ERROR(ret, TAG, "create LED strip failed");
    
    // 设置运行标志为true，以便清除函数可以工作
    ws2812_config->is_running = true;
    
    // 清除所有LED
    ret = ws2812_clear_all();
    ESP_RETURN_ON_ERROR(ret, TAG, "initial clear failed");
    
    ESP_LOGI(TAG, "WS2812 LED initialized successfully, pin: %d, leds: %d", 
             ws2812_config->pin, ws2812_config->num_leds);
    
    return ESP_OK;
}

/**
 * @brief 设置单个LED颜色
 */
esp_err_t ws2812_set_led_color(int led_index, rgb_color_t color) {
    ESP_RETURN_ON_FALSE(ws2812_config && ws2812_config->is_running, ESP_ERR_INVALID_STATE, 
                       TAG, "WS2812 not initialized");
    ESP_RETURN_ON_FALSE(led_index >= 0 && led_index < ws2812_config->num_leds, 
                       ESP_ERR_INVALID_ARG, TAG, "invalid LED index");
    
    return led_strip_set_pixel(ws2812_config->strip, led_index, color.r, color.g, color.b);
}

/**
 * @brief 设置所有LED颜色
 */
esp_err_t ws2812_set_all_color(rgb_color_t color) {
    ESP_RETURN_ON_FALSE(ws2812_config && ws2812_config->is_running, ESP_ERR_INVALID_STATE, 
                       TAG, "WS2812 not initialized");
    
    for (int i = 0; i < ws2812_config->num_leds; i++) {
        esp_err_t ret = ws2812_set_led_color(i, color);
        if (ret != ESP_OK) {
            return ret;
        }
    }
    
    return ws2812_update();
}

/**
 * @brief 清除所有LED（关闭）
 */
esp_err_t ws2812_clear_all(void) {
    rgb_color_t black = {0, 0, 0};
    return ws2812_set_all_color(black);
}

/**
 * @brief 更新LED显示
 */
esp_err_t ws2812_update(void) {
    ESP_RETURN_ON_FALSE(ws2812_config && ws2812_config->is_running, ESP_ERR_INVALID_STATE, 
                       TAG, "WS2812 not initialized");
    
    return led_strip_refresh(ws2812_config->strip);
}

/**
 * @brief 停止当前特效任务
 */
static esp_err_t stop_current_effect_simple(void) {
    if (!ws2812_config) return ESP_OK;
    
    if (ws2812_config->effect_task) {
        // 设置停止标志
        ws2812_config->is_running = false;
        
        // 等待任务自然退出（增加等待时间）
        vTaskDelay(pdMS_TO_TICKS(300));
        
        // 如果任务仍然存在，强制删除
        if (ws2812_config->effect_task) {
            TaskHandle_t task_to_delete = ws2812_config->effect_task;
            ws2812_config->effect_task = NULL;
            vTaskDelete(task_to_delete);
        }
        
        // 重置运行标志
        ws2812_config->is_running = true;
        
        // 清除LED
        ws2812_clear_all();
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    return ESP_OK;
}

/**
 * @brief 简化的彩虹渐变效果任务
 */
static void rainbow_effect_task_simple(void *pvParameters) {
    int speed = *(int *)pvParameters;
    int hue = 0;
    
    // 立即释放参数内存
    free(pvParameters);
    
    // 检查WS2812是否已初始化
    if (!ws2812_config || !ws2812_config->is_running) {
        vTaskDelete(NULL);
        return;
    }
    
    while (ws2812_config && ws2812_config->is_running) {
        // 简化的彩虹效果，只设置一个LED（因为我们只有1个LED）
        hsv_color_t hsv = {
            .h = hue % 360,
            .s = 1.0,
            .v = 1.0
        };
        rgb_color_t rgb;
        hsv_to_rgb(hsv, &rgb);
        
        esp_err_t ret = ws2812_set_all_color(rgb);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "彩虹效果设置颜色失败: %s", esp_err_to_name(ret));
            break;
        }
        
        hue = (hue + speed) % 360;
        vTaskDelay(pdMS_TO_TICKS(100)); // 减慢速度，减少CPU负载
    }
    
    // 任务退出时清除LED
    if (ws2812_config && ws2812_config->is_running) {
        ws2812_clear_all();
    }
    
    // 清除任务句柄
    if (ws2812_config && ws2812_config->effect_task == xTaskGetCurrentTaskHandle()) {
        ws2812_config->effect_task = NULL;
    }
    
    vTaskDelete(NULL);
}

/**
 * @brief 简化的呼吸灯效果任务
 */
static void breathing_effect_task_simple(void *pvParameters) {
    rgb_color_t base_color = *(rgb_color_t *)pvParameters;
    
    // 立即释放参数内存
    free(pvParameters);
    
    // 检查WS2812是否已初始化
    if (!ws2812_config || !ws2812_config->is_running) {
        vTaskDelete(NULL);
        return;
    }
    
    float brightness = 0.0;
    bool increasing = true;
    
    while (ws2812_config && ws2812_config->is_running) {
        rgb_color_t color = {
            .r = (uint8_t)(base_color.r * brightness),
            .g = (uint8_t)(base_color.g * brightness),
            .b = (uint8_t)(base_color.b * brightness)
        };
        
        esp_err_t ret = ws2812_set_all_color(color);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "呼吸灯效果设置颜色失败: %s", esp_err_to_name(ret));
            break;
        }
        
        if (increasing) {
            brightness += 0.02;
            if (brightness >= 1.0) {
                brightness = 1.0;
                increasing = false;
            }
        } else {
            brightness -= 0.02;
            if (brightness <= 0.0) {
                brightness = 0.0;
                increasing = true;
            }
        }
        
        vTaskDelay(pdMS_TO_TICKS(100)); // 减慢速度，减少CPU负载
    }
    
    // 任务退出时清除LED
    if (ws2812_config && ws2812_config->is_running) {
        ws2812_clear_all();
    }
    
    // 清除任务句柄
    if (ws2812_config && ws2812_config->effect_task == xTaskGetCurrentTaskHandle()) {
        ws2812_config->effect_task = NULL;
    }
    
    vTaskDelete(NULL);
}

/**
 * @brief 简化的彩虹渐变效果
 */
esp_err_t ws2812_rainbow_effect(int speed) {
    ESP_RETURN_ON_FALSE(ws2812_config && ws2812_config->is_running, ESP_ERR_INVALID_STATE, 
                       TAG, "WS2812 not initialized");
    
    // 停止之前的特效任务
    stop_current_effect_simple();
    
    int *param = malloc(sizeof(int));
    ESP_RETURN_ON_FALSE(param, ESP_ERR_NO_MEM, TAG, "no mem for effect param");
    *param = speed;
    
    esp_err_t ret = xTaskCreate(rainbow_effect_task_simple, "rainbow_effect", 4096, param, 3, &ws2812_config->effect_task);
    if (ret != pdPASS) {
        free(param);
        ESP_LOGE(TAG, "创建彩虹效果任务失败");
        return ESP_FAIL;
    }
    
    return ESP_OK;
}

/**
 * @brief 简化的呼吸灯效果
 */
esp_err_t ws2812_breathing_effect(rgb_color_t color, int speed) {
    ESP_RETURN_ON_FALSE(ws2812_config && ws2812_config->is_running, ESP_ERR_INVALID_STATE, 
                       TAG, "WS2812 not initialized");
    
    // 停止之前的特效任务
    stop_current_effect_simple();
    
    rgb_color_t *param = malloc(sizeof(rgb_color_t));
    ESP_RETURN_ON_FALSE(param, ESP_ERR_NO_MEM, TAG, "no mem for effect param");
    *param = color;
    
    esp_err_t ret = xTaskCreate(breathing_effect_task_simple, "breathing_effect", 4096, param, 3, &ws2812_config->effect_task);
    if (ret != pdPASS) {
        free(param);
        ESP_LOGE(TAG, "创建呼吸灯效果任务失败");
        return ESP_FAIL;
    }
    
    return ESP_OK;
}

/**
 * @brief 停止WS2812控制并释放资源
 */
esp_err_t ws2812_led_deinit(void) {
    if (!ws2812_config) {
        return ESP_OK;
    }
    
    // 停止特效任务
    if (ws2812_config->effect_task) {
        ws2812_config->is_running = false;
        vTaskDelay(pdMS_TO_TICKS(200));
        
        // 强制删除任务
        TaskHandle_t task_to_delete = ws2812_config->effect_task;
        ws2812_config->effect_task = NULL;
        vTaskDelete(task_to_delete);
    }
    
    // 关闭所有LED
    ws2812_clear_all();
    vTaskDelay(pdMS_TO_TICKS(50));
    
    // 释放资源
    if (ws2812_config->strip) {
        led_strip_del(ws2812_config->strip);
    }
    
    free(ws2812_config);
    ws2812_config = NULL;
    
    ESP_LOGI(TAG, "WS2812 LED deinitialized");
    return ESP_OK;
}