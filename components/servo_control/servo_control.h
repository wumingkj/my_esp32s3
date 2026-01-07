#ifndef SERVO_CONTROL_H
#define SERVO_CONTROL_H

#include "esp_err.h"
#include "driver/ledc.h"

#ifdef __cplusplus
extern "C" {
#endif

// 舵机默认配置
#define SERVO_PIN           38          /*!< 舵机控制引脚 GPIO38 */
#define SERVO_CHANNEL       LEDC_CHANNEL_0  /*!< LEDC通道0 */
#define SERVO_TIMER         LEDC_TIMER_0    /*!< LEDC定时器0 */
#define SERVO_SPEED_MODE    LEDC_LOW_SPEED_MODE  /*!< LEDC速度模式 */
#define SERVO_FREQUENCY     50          /*!< PWM频率50Hz */
#define SERVO_RESOLUTION    LEDC_TIMER_12_BIT  /*!< PWM分辨率12位 */

// 舵机角度范围
#define SERVO_MIN_ANGLE     0           /*!< 最小角度0度 */
#define SERVO_MAX_ANGLE     180         /*!< 最大角度180度 */

// 脉宽范围（微秒）
#define SERVO_MIN_PULSEWIDTH 500        /*!< 0度对应脉宽500us */
#define SERVO_MAX_PULSEWIDTH 2500       /*!< 180度对应脉宽2500us */

/**
 * @brief 舵机控制配置结构体
 */
typedef struct {
    int pin;                    /*!< GPIO引脚 */
    ledc_channel_t channel;     /*!< LEDC通道 */
    ledc_timer_t timer;         /*!< LEDC定时器 */
    ledc_mode_t speed_mode;     /*!< LEDC速度模式 */
    uint32_t frequency;         /*!< PWM频率 */
    ledc_timer_bit_t resolution; /*!< PWM分辨率 */
    int min_pulsewidth;         /*!< 最小脉宽(微秒) */
    int max_pulsewidth;         /*!< 最大脉宽(微秒) */
} servo_config_t;

/**
 * @brief 初始化舵机控制
 * 
 * @param config 舵机配置参数，如果为NULL则使用默认配置
 * @return esp_err_t ESP_OK表示成功，其他表示失败
 */
esp_err_t servo_control_init(const servo_config_t *config);

/**
 * @brief 设置舵机角度
 * 
 * @param angle 目标角度(0-180度)
 * @return esp_err_t ESP_OK表示成功，其他表示失败
 */
esp_err_t servo_control_set_angle(int angle);

/**
 * @brief 获取当前舵机角度
 * 
 * @return int 当前角度值
 */
int servo_control_get_angle(void);

/**
 * @brief 舵机测试函数，从0度转到180度再转回0度
 * 
 * @return esp_err_t ESP_OK表示成功，其他表示失败
 */
esp_err_t servo_control_test(void);

/**
 * @brief 停止舵机控制并释放资源
 * 
 * @return esp_err_t ESP_OK表示成功，其他表示失败
 */
esp_err_t servo_control_deinit(void);

/**
 * @brief 平滑移动舵机到指定角度（使用S型曲线加速/减速）
 * 
 * @param target_angle 目标角度(0-180度)
 * @param duration_ms 移动总时间（毫秒）
 * @param acceleration 加速度因子（0.0-1.0，值越小越平滑）
 * @return esp_err_t ESP_OK表示成功，其他表示失败
 */
esp_err_t servo_control_smooth_move(int target_angle, int duration_ms, float acceleration);

/**
 * @brief 平滑舵机测试函数，使用S型曲线实现缓启动和缓停止
 * 
 * @return esp_err_t ESP_OK表示成功，其他表示失败
 */
esp_err_t servo_control_smooth_test(void);

/**
 * @brief 舵机硬件测试函数，直接测试基本角度设置
 * 
 * @return esp_err_t ESP_OK表示成功，其他表示失败
 */
esp_err_t servo_control_hardware_test(void);

#ifdef __cplusplus
}
#endif

#endif /* SERVO_CONTROL_H */