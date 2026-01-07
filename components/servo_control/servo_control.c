#include "servo_control.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <math.h>

static const char *TAG = "servo_control";

// 舵机控制状态
static int current_angle = 90;  // 默认初始角度为90度
static bool is_initialized = false;
static servo_config_t current_config;

// 默认配置 - 使用更通用的脉宽范围
static const servo_config_t default_config = {
    .pin = SERVO_PIN,
    .channel = SERVO_CHANNEL,
    .timer = SERVO_TIMER,
    .speed_mode = SERVO_SPEED_MODE,
    .frequency = SERVO_FREQUENCY,
    .resolution = SERVO_RESOLUTION,
    .min_pulsewidth = 1000,  // 改为1000us，更通用
    .max_pulsewidth = 2000   // 改为2000us，更通用
};

/**
 * @brief S型曲线函数（Sigmoid函数变体）
 * @param t 时间比例（0.0-1.0）
 * @param k 曲线陡峭度（值越大曲线越陡峭）
 * @return float 位置比例（0.0-1.0）
 */
static float s_curve(float t, float k) {
    // 使用改进的S型曲线，确保在0和1处有平滑的边界
    if (t <= 0.0f) return 0.0f;
    if (t >= 1.0f) return 1.0f;
    
    // 使用双曲正切函数实现S型曲线
    return 0.5f * (tanhf(k * (t - 0.5f)) / tanhf(k * 0.5f) + 1.0f);
}

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
    ESP_LOGI(TAG, "Servo angle set to %d° (pulsewidth: %dus, duty: %lu)", 
             angle, pulsewidth, duty);
    return ESP_OK;
}

// 修复servo_control_smooth_move函数中的未使用变量警告
esp_err_t servo_control_smooth_move(int target_angle, int duration_ms, float acceleration)
{
    if (!is_initialized) {
        ESP_LOGE(TAG, "Servo control not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (target_angle == current_angle) {
        return ESP_OK;
    }

    // 约束加速度参数
    if (acceleration < 0.01f) acceleration = 0.01f;
    if (acceleration > 1.0f) acceleration = 1.0f;

    // 计算角度差（移除未使用的direction变量）
    int angle_diff = target_angle - current_angle;
    angle_diff = abs(angle_diff);

    // 计算步数和步长时间
    int step_count = duration_ms / 20;  // 每20ms更新一次
    if (step_count < 2) step_count = 2;  // 至少2步
    
    float step_time_ms = (float)duration_ms / step_count;
    
    ESP_LOGI(TAG, "Smooth move: from %d to %d, duration: %dms, steps: %d", 
             current_angle, target_angle, duration_ms, step_count);

    // 使用S型曲线进行平滑移动
    float k = 6.0f * acceleration;  // 曲线陡峭度
    float accumulated_fraction = 0.0f;  // 累积的小数部分
    
    for (int step = 0; step <= step_count; step++) {
        // 计算时间比例（0.0-1.0）
        float t = (float)step / step_count;
        
        // 使用S型曲线计算位置比例
        float position_ratio = s_curve(t, k);
        
        // 计算目标位置（考虑小数累积）
        float target_position = current_angle + angle_diff * position_ratio + accumulated_fraction;
        int actual_angle = (int)target_position;
        
        // 处理小数累积
        accumulated_fraction = target_position - actual_angle;
        
        // 约束角度范围
        if (actual_angle < SERVO_MIN_ANGLE) actual_angle = SERVO_MIN_ANGLE;
        if (actual_angle > SERVO_MAX_ANGLE) actual_angle = SERVO_MAX_ANGLE;
        
        // 设置舵机角度
        esp_err_t ret = servo_control_set_angle_internal(actual_angle);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to set angle during smooth move: %s", esp_err_to_name(ret));
            return ret;
        }
        
        // 延时（最后一步不需要延时）
        if (step < step_count) {
            vTaskDelay(pdMS_TO_TICKS((int)step_time_ms));
        }
    }
    
    ESP_LOGI(TAG, "Smooth move completed");
    return ESP_OK;
}

esp_err_t servo_control_smooth_test(void)
{
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
    // 修复格式错误：使用%lu代替%d来匹配uint32_t类型
    ESP_LOGI(TAG, "PWM配置: 频率=%luHz, 分辨率=%d位", 
             current_config.frequency, current_config.resolution);
    ESP_LOGI(TAG, "脉宽范围: %d-%dus", 
             current_config.min_pulsewidth, current_config.max_pulsewidth);

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
    
    // 注释掉PWM输出测试 - 避免重复执行导致日志刷屏
    // 立即测试PWM输出
    ESP_LOGI(TAG, "=== PWM输出测试开始 ===");
    servo_control_set_angle_internal(0);   // 测试0度
    vTaskDelay(pdMS_TO_TICKS(1000));
    servo_control_set_angle_internal(90);  // 测试90度
    vTaskDelay(pdMS_TO_TICKS(1000));
    servo_control_set_angle_internal(180); // 测试180度
    vTaskDelay(pdMS_TO_TICKS(1000));
    servo_control_set_angle_internal(90);  // 回到90度
    ESP_LOGI(TAG, "=== PWM输出测试完成 ===");
    
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