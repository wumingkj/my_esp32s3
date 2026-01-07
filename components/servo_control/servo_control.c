#include "servo_control.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "servo_control";

// 舵机控制状态
static int current_angle = 90;  // 默认初始角度为90度
static bool is_initialized = false;
static servo_config_t current_config;

// 默认配置
static const servo_config_t default_config = {
    .pin = SERVO_PIN,
    .channel = SERVO_CHANNEL,
    .timer = SERVO_TIMER,
    .speed_mode = SERVO_SPEED_MODE,
    .frequency = SERVO_FREQUENCY,
    .resolution = SERVO_RESOLUTION,
    .min_pulsewidth = SERVO_MIN_PULSEWIDTH,
    .max_pulsewidth = SERVO_MAX_PULSEWIDTH
};

/**
 * @brief 角度转脉宽（微秒）
 */
static int angle_to_pulsewidth(int angle, int min_pulse, int max_pulse)
{
    // 约束角度范围
    if (angle < SERVO_MIN_ANGLE) {
        angle = SERVO_MIN_ANGLE;
    } else if (angle > SERVO_MAX_ANGLE) {
        angle = SERVO_MAX_ANGLE;
    }

    // 线性映射角度到脉宽
    return min_pulse + (angle * (max_pulse - min_pulse)) / SERVO_MAX_ANGLE;
}

/**
 * @brief 脉宽转占空比
 */
static uint32_t pulsewidth_to_duty(int pulsewidth_us, uint32_t frequency, ledc_timer_bit_t resolution)
{
    // 计算周期（微秒）
    uint32_t period_us = 1000000 / frequency;
    
    // 计算占空比：duty = (pulsewidth / period) * (2^resolution - 1)
    uint64_t duty = ((uint64_t)pulsewidth_us * ((1ULL << resolution) - 1)) / period_us;
    
    return (uint32_t)duty;
}

/**
 * @brief 内部角度设置函数（不检查初始化状态）
 */
static esp_err_t servo_control_set_angle_internal(int angle)
{
    // 约束角度范围
    if (angle < SERVO_MIN_ANGLE) {
        angle = SERVO_MIN_ANGLE;
        ESP_LOGW(TAG, "Angle clamped to minimum: %d", angle);
    } else if (angle > SERVO_MAX_ANGLE) {
        angle = SERVO_MAX_ANGLE;
        ESP_LOGW(TAG, "Angle clamped to maximum: %d", angle);
    }

    // 计算脉宽
    int pulsewidth = angle_to_pulsewidth(angle, 
                                        current_config.min_pulsewidth, 
                                        current_config.max_pulsewidth);
    
    // 计算占空比
    uint32_t duty = pulsewidth_to_duty(pulsewidth, 
                                      current_config.frequency, 
                                      current_config.resolution);

    // 设置PWM输出
    esp_err_t ret = ledc_set_duty(current_config.speed_mode, current_config.channel, duty);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set LEDC duty: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // 更新PWM输出
    ret = ledc_update_duty(current_config.speed_mode, current_config.channel);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to update LEDC duty: %s", esp_err_to_name(ret));
        return ret;
    }
    
    current_angle = angle;
    // ESP_LOGI(TAG, "Servo angle set to %d degrees (pulsewidth: %dus, duty: %lu)", 
    //          angle, pulsewidth, duty);
    return ESP_OK;
}

esp_err_t servo_control_init(const servo_config_t *config)
{
    if (is_initialized) {
        ESP_LOGW(TAG, "Servo control already initialized");
        return ESP_OK;
    }

    // 使用默认配置如果没有提供配置
    if (config == NULL) {
        current_config = default_config;
    } else {
        current_config = *config;
    }

    ESP_LOGI(TAG, "Initializing servo control on GPIO%d", current_config.pin);

    // 配置LEDC定时器
    ledc_timer_config_t ledc_timer = {
        .speed_mode = current_config.speed_mode,
        .timer_num = current_config.timer,
        .duty_resolution = current_config.resolution,
        .freq_hz = current_config.frequency,
        .clk_cfg = LEDC_AUTO_CLK
    };
    
    esp_err_t ret = ledc_timer_config(&ledc_timer);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure LEDC timer: %s", esp_err_to_name(ret));
        return ret;
    }

    // 配置LEDC通道
    ledc_channel_config_t ledc_channel = {
        .channel = current_config.channel,
        .duty = 0,
        .gpio_num = current_config.pin,
        .speed_mode = current_config.speed_mode,
        .hpoint = 0,
        .timer_sel = current_config.timer
    };
    
    ret = ledc_channel_config(&ledc_channel);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure LEDC channel: %s", esp_err_to_name(ret));
        return ret;
    }

    // 设置初始角度（使用内部函数，不检查初始化状态）
    ESP_LOGI(TAG, "Setting initial angle to %d degrees", current_angle);
    
    // 计算脉宽
    int pulsewidth = angle_to_pulsewidth(current_angle, 
                                        current_config.min_pulsewidth, 
                                        current_config.max_pulsewidth);
    
    // 计算占空比
    uint32_t duty = pulsewidth_to_duty(pulsewidth, 
                                      current_config.frequency, 
                                      current_config.resolution);

    // 设置PWM输出
    ret = ledc_set_duty(current_config.speed_mode, current_config.channel, duty);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set LEDC duty: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // 更新PWM输出
    ret = ledc_update_duty(current_config.speed_mode, current_config.channel);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to update LEDC duty: %s", esp_err_to_name(ret));
        return ret;
    }

    is_initialized = true;
    ESP_LOGI(TAG, "Servo control initialized successfully on GPIO%d", current_config.pin);
    return ESP_OK;
}

esp_err_t servo_control_set_angle(int angle)
{
    if (!is_initialized) {
        ESP_LOGE(TAG, "Servo control not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    return servo_control_set_angle_internal(angle);
}

int servo_control_get_angle(void)
{
    return current_angle;
}

esp_err_t servo_control_test(void)
{
    if (!is_initialized) {
        ESP_LOGE(TAG, "Servo control not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Starting servo test...");
    
    // 从当前角度转到0度
    ESP_LOGI(TAG, "Moving to 0 degrees...");
    for (int angle = current_angle; angle >= SERVO_MIN_ANGLE; angle -= 10) {
        esp_err_t ret = servo_control_set_angle(angle);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to set angle during test: %s", esp_err_to_name(ret));
            return ret;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    
    // 从0度转到180度
    ESP_LOGI(TAG, "Moving to 180 degrees...");
    for (int angle = SERVO_MIN_ANGLE; angle <= SERVO_MAX_ANGLE; angle += 10) {
        esp_err_t ret = servo_control_set_angle(angle);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to set angle during test: %s", esp_err_to_name(ret));
            return ret;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    
    // 从180度转回90度（中间位置）
    ESP_LOGI(TAG, "Moving to 90 degrees...");
    for (int angle = SERVO_MAX_ANGLE; angle >= 90; angle -= 10) {
        esp_err_t ret = servo_control_set_angle(angle);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to set angle during test: %s", esp_err_to_name(ret));
            return ret;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    
    ESP_LOGI(TAG, "Servo test completed");
    return ESP_OK;
}

esp_err_t servo_control_deinit(void)
{
    if (!is_initialized) {
        return ESP_OK;
    }

    // 停止PWM输出
    esp_err_t ret = ledc_stop(current_config.speed_mode, current_config.channel, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to stop LEDC: %s", esp_err_to_name(ret));
        return ret;
    }
    
    is_initialized = false;
    current_angle = 90;
    ESP_LOGI(TAG, "Servo control deinitialized");
    return ESP_OK;
}