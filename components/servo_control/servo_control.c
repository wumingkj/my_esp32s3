#include "servo_control.h"

#include <math.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char* TAG = "servo_control";

// 舵机控制状态
static servo_status_t servo_status = {.state = SERVO_STATE_UNINITIALIZED,
                                      .current_angle = 90,
                                      .target_angle = 90,
                                      .last_update_time = 0,
                                      .last_error = ESP_OK};

static bool is_initialized = false;
static servo_config_t current_config;

// 默认配置 - 使用标准脉宽范围
static const servo_config_t default_config = {
    .pin = SERVO_PIN,
    .channel = SERVO_CHANNEL,
    .timer = SERVO_TIMER,
    .speed_mode = SERVO_SPEED_MODE,
    .frequency = SERVO_FREQUENCY,
    .resolution = SERVO_RESOLUTION,
    .min_pulsewidth = SERVO_MIN_PULSEWIDTH,  // 使用头文件定义
    .max_pulsewidth = SERVO_MAX_PULSEWIDTH   // 使用头文件定义
};

/**
 * @brief S型曲线函数（Sigmoid函数变体）
 */
static float s_curve(float t, float k) {
    if (t <= 0.0f)
        return 0.0f;
    if (t >= 1.0f)
        return 1.0f;

    return 0.5f * (tanhf(k * (t - 0.5f)) / tanhf(k * 0.5f) + 1.0f);
}

/**
 * @brief 角度转脉宽（微秒）
 */
static int angle_to_pulsewidth(int angle, int min_pulse, int max_pulse) {
    if (angle < SERVO_MIN_ANGLE) {
        angle = SERVO_MIN_ANGLE;
    } else if (angle > SERVO_MAX_ANGLE) {
        angle = SERVO_MAX_ANGLE;
    }

    return min_pulse + (angle * (max_pulse - min_pulse)) / SERVO_MAX_ANGLE;
}

/**
 * @brief 脉宽转占空比
 */
static uint32_t pulsewidth_to_duty(int pulsewidth_us, uint32_t frequency, ledc_timer_bit_t resolution) {
    uint32_t period_us = 1000000 / frequency;
    uint64_t duty = ((uint64_t)pulsewidth_us * ((1ULL << resolution) - 1)) / period_us;
    return (uint32_t)duty;
}

/**
 * @brief 验证GPIO引脚是否可用
 */
static esp_err_t validate_gpio_pin(int pin) {
    // 检查引脚是否在有效范围内
    if (pin < 0 || pin > 48) {
        return ESP_ERR_INVALID_ARG;
    }

    // 检查引脚是否支持PWM输出
    if (!GPIO_IS_VALID_OUTPUT_GPIO(pin)) {
        return ESP_ERR_INVALID_ARG;
    }

    return ESP_OK;
}

/**
 * @brief 内部角度设置函数（不检查初始化状态）
 */
static esp_err_t servo_control_set_angle_internal(int angle) {
    // 约束角度范围
    if (angle < SERVO_MIN_ANGLE) {
        angle = SERVO_MIN_ANGLE;
        ESP_LOGW(TAG, "Angle clamped to minimum: %d", angle);
    } else if (angle > SERVO_MAX_ANGLE) {
        angle = SERVO_MAX_ANGLE;
        ESP_LOGW(TAG, "Angle clamped to maximum: %d", angle);
    }

    // 计算脉宽
    int pulsewidth = angle_to_pulsewidth(angle, current_config.min_pulsewidth, current_config.max_pulsewidth);

    // 计算占空比
    uint32_t duty = pulsewidth_to_duty(pulsewidth, current_config.frequency, current_config.resolution);

    // 设置PWM输出
    esp_err_t ret = ledc_set_duty(current_config.speed_mode, current_config.channel, duty);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set LEDC duty: %s", esp_err_to_name(ret));
        servo_status.last_error = ret;
        servo_status.state = SERVO_STATE_ERROR;
        return ret;
    }

    // 更新PWM输出
    ret = ledc_update_duty(current_config.speed_mode, current_config.channel);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to update LEDC duty: %s", esp_err_to_name(ret));
        servo_status.last_error = ret;
        servo_status.state = SERVO_STATE_ERROR;
        return ret;
    }

    servo_status.current_angle = angle;
    servo_status.last_update_time = xTaskGetTickCount() * portTICK_PERIOD_MS;

    ESP_LOGD(TAG, "Servo angle set to %d° (pulsewidth: %dus, duty: %lu)", angle, pulsewidth, duty);
    return ESP_OK;
}

// 改进的平滑移动函数 - 使用简单的缓动函数
esp_err_t servo_control_smooth_move(int target_angle, int duration_ms, float acceleration) {
    if (!is_initialized) {
        ESP_LOGE(TAG, "Servo control not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (target_angle == servo_status.current_angle) {
        return ESP_OK;
    }

    // 约束加速度参数
    if (acceleration < 0.1f)
        acceleration = 0.1f;
    if (acceleration > 0.9f)
        acceleration = 0.9f;

    // 计算角度差（保留方向信息）
    int angle_diff = target_angle - servo_status.current_angle;
    int start_angle = servo_status.current_angle;

    // 计算步数和步长时间
    int step_count = duration_ms / 10;  // 每10ms更新一次，提高平滑度
    if (step_count < 5)
        step_count = 5;  // 至少5步

    float step_time_ms = (float)duration_ms / step_count;

    ESP_LOGI(TAG, "Smooth move: from %d to %d, duration: %dms, steps: %d", start_angle, target_angle, duration_ms,
             step_count);

    // 使用改进的缓动函数
    for (int step = 0; step <= step_count; step++) {
        // 计算时间比例（0.0-1.0）
        float t = (float)step / step_count;

        // 改进的缓动函数：缓入缓出效果
        float eased_t;
        if (t < 0.5f) {
            // 缓入阶段
            eased_t = 2.0f * t * t;
        } else {
            // 缓出阶段
            t = t - 0.5f;
            eased_t = 1.0f - 2.0f * t * t;
            eased_t = 0.5f + eased_t * 0.5f;
        }

        // 应用加速度调整
        eased_t = eased_t * (1.0f - acceleration) + t * acceleration;

        // 计算目标角度
        int actual_angle = start_angle + (int)(angle_diff * eased_t);

        // 约束角度范围
        if (actual_angle < SERVO_MIN_ANGLE)
            actual_angle = SERVO_MIN_ANGLE;
        if (actual_angle > SERVO_MAX_ANGLE)
            actual_angle = SERVO_MAX_ANGLE;

        // 设置舵机角度
        esp_err_t ret = servo_control_set_angle_internal(actual_angle);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to set angle during smooth move: %s", esp_err_to_name(ret));
            return ret;
        }

        // 延时
        if (step < step_count) {
            vTaskDelay(pdMS_TO_TICKS((int)step_time_ms));
        }
    }

    // 确保最终位置准确
    esp_err_t ret = servo_control_set_angle_internal(target_angle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set final angle: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "Smooth move completed");
    return ESP_OK;
}

esp_err_t servo_control_smooth_test(void) {
    if (!is_initialized) {
        ESP_LOGE(TAG, "Servo control not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Starting smooth servo test...");

    // 使用S型曲线平滑移动到0度（缓启动，缓停止）
    ESP_LOGI(TAG, "Smooth moving to 0 degrees...");
    esp_err_t ret = servo_control_smooth_move(0, 2000, 0.3f);  // 2秒，中等平滑度
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to move to 0 degrees: %s", esp_err_to_name(ret));
        return ret;
    }

    vTaskDelay(pdMS_TO_TICKS(500));  // 暂停500ms

    // 使用S型曲线平滑移动到180度
    ESP_LOGI(TAG, "Smooth moving to 180 degrees...");
    ret = servo_control_smooth_move(180, 3000, 0.2f);  // 3秒，更平滑
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to move to 180 degrees: %s", esp_err_to_name(ret));
        return ret;
    }

    vTaskDelay(pdMS_TO_TICKS(500));  // 暂停500ms

    // 使用S型曲线平滑移动回90度
    ESP_LOGI(TAG, "Smooth moving to 90 degrees...");
    ret = servo_control_smooth_move(90, 2500, 0.4f);  // 2.5秒，较陡峭
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to move to 90 degrees: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "Smooth servo test completed");
    return ESP_OK;
}

// 修复servo_control_init函数中的格式错误
esp_err_t servo_control_init(const servo_config_t* config) {
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
    // 修复格式错误：使用%lu代替%d来匹配uint32_t类型
    ESP_LOGI(TAG, "PWM配置: 频率=%luHz, 分辨率=%d位", current_config.frequency, current_config.resolution);
    ESP_LOGI(TAG, "脉宽范围: %d-%dus", current_config.min_pulsewidth, current_config.max_pulsewidth);

    // 验证GPIO引脚
    esp_err_t ret = validate_gpio_pin(current_config.pin);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Invalid GPIO pin %d", current_config.pin);
        return ret;
    }

    // 配置LEDC定时器
    ledc_timer_config_t ledc_timer = {.speed_mode = current_config.speed_mode,
                                      .timer_num = current_config.timer,
                                      .duty_resolution = current_config.resolution,
                                      .freq_hz = current_config.frequency,
                                      .clk_cfg = LEDC_AUTO_CLK};

    ret = ledc_timer_config(&ledc_timer);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure LEDC timer: %s", esp_err_to_name(ret));
        return ret;
    }

    // 配置LEDC通道
    ledc_channel_config_t ledc_channel = {.channel = current_config.channel,
                                          .duty = 0,
                                          .gpio_num = current_config.pin,
                                          .speed_mode = current_config.speed_mode,
                                          .hpoint = 0,
                                          .timer_sel = current_config.timer};

    ret = ledc_channel_config(&ledc_channel);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure LEDC channel: %s", esp_err_to_name(ret));
        return ret;
    }

    // 设置初始角度（使用内部函数，不检查初始化状态）
    ESP_LOGI(TAG, "Setting initial angle to %d degrees", servo_status.current_angle);

    // 计算脉宽
    int pulsewidth =
        angle_to_pulsewidth(servo_status.current_angle, current_config.min_pulsewidth, current_config.max_pulsewidth);

    // 计算占空比
    uint32_t duty = pulsewidth_to_duty(pulsewidth, current_config.frequency, current_config.resolution);

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

    // 注释掉PWM输出测试 - 避免重复执行导致日志刷屏
    // 立即测试PWM输出
    ESP_LOGI(TAG, "=== PWM输出测试开始 ===");
    servo_control_set_angle_internal(0);  // 测试0度
    vTaskDelay(pdMS_TO_TICKS(1000));
    servo_control_set_angle_internal(90);  // 测试90度
    vTaskDelay(pdMS_TO_TICKS(1000));
    servo_control_set_angle_internal(180);  // 测试180度
    vTaskDelay(pdMS_TO_TICKS(1000));
    servo_control_set_angle_internal(90);  // 回到90度
    ESP_LOGI(TAG, "=== PWM输出测试完成 ===");

    return ESP_OK;
}
esp_err_t servo_control_set_angle(int angle) {
    if (!is_initialized) {
        ESP_LOGE(TAG, "Servo control not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    return servo_control_set_angle_internal(angle);
}

int servo_control_get_angle(void) {
    if (!is_initialized) {
        ESP_LOGE(TAG, "Servo control not initialized");
        return -1;
    }
    return servo_status.current_angle;
}

esp_err_t servo_control_test(void) {
    if (!is_initialized) {
        ESP_LOGE(TAG, "Servo control not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Starting servo test...");

    // 从当前角度转到0度（使用平滑移动替代线性步进）
    esp_err_t ret = servo_control_smooth_move(0, 1500, 0.5f);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set angle during test: %s", esp_err_to_name(ret));
        return ret;
    }

    // 从0度转到180度
    ret = servo_control_smooth_move(180, 2000, 0.5f);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set angle during test: %s", esp_err_to_name(ret));
        return ret;
    }

    // 从180度转回90度（中间位置）
    ret = servo_control_smooth_move(90, 1500, 0.5f);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set angle during test: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "Servo test completed");
    return ESP_OK;
}

esp_err_t servo_control_deinit(void) {
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
    servo_status.current_angle = 90;
    servo_status.target_angle = 90;
    servo_status.state = SERVO_STATE_UNINITIALIZED;
    ESP_LOGI(TAG, "Servo control deinitialized");
    return ESP_OK;
}

/**
 * @brief 舵机硬件诊断测试
 */
esp_err_t servo_control_diagnostic_test(void) {
    if (!is_initialized) {
        ESP_LOGE(TAG, "Servo control not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "=== 舵机硬件诊断测试开始 ===");

    // 测试各个关键角度
    int test_angles[] = {0, 45, 90, 135, 180};
    int num_tests = sizeof(test_angles) / sizeof(test_angles[0]);

    for (int i = 0; i < num_tests; i++) {
        int angle = test_angles[i];
        ESP_LOGI(TAG, "测试角度: %d度", angle);

        // 计算脉宽和占空比
        int pulsewidth = angle_to_pulsewidth(angle, current_config.min_pulsewidth, current_config.max_pulsewidth);
        uint32_t duty = pulsewidth_to_duty(pulsewidth, current_config.frequency, current_config.resolution);

        ESP_LOGI(TAG, "角度 %d度 -> 脉宽: %dus, 占空比: %lu", angle, pulsewidth, duty);

        esp_err_t ret = servo_control_set_angle_fast(angle);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "设置角度 %d度失败: %s", angle, esp_err_to_name(ret));
            return ret;
        }

        vTaskDelay(pdMS_TO_TICKS(2000));  // 等待2秒观察舵机反应
    }

    // 回到中间位置
    servo_control_set_angle_fast(90);
    ESP_LOGI(TAG, "=== 舵机硬件诊断测试完成 ===");
    return ESP_OK;
}

/**
 * @brief 快速设置舵机角度（跳过平滑移动）
 */
esp_err_t servo_control_set_angle_fast(int angle) {
    if (!is_initialized) {
        ESP_LOGE(TAG, "Servo control not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    servo_status.state = SERVO_STATE_MOVING;
    servo_status.target_angle = angle;

    esp_err_t ret = servo_control_set_angle_internal(angle);

    if (ret == ESP_OK) {
        servo_status.state = SERVO_STATE_READY;
    }

    return ret;
}

/**
 * @brief 获取舵机状态信息
 */
esp_err_t servo_control_get_status(servo_status_t* status) {
    if (status == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    *status = servo_status;
    return ESP_OK;
}