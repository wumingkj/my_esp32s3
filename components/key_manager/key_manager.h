#ifndef KEY_MANAGER_H
#define KEY_MANAGER_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "driver/gpio.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 按键事件类型定义
 */
typedef enum {
    KEY_EVENT_PRESSED = 0,      // 按键按下
    KEY_EVENT_RELEASED,         // 按键释放
    KEY_EVENT_SINGLE_CLICK,     // 单击
    KEY_EVENT_DOUBLE_CLICK,     // 双击
    KEY_EVENT_LONG_PRESS,       // 长按
    KEY_EVENT_HOLD,             // 保持按下
    KEY_EVENT_REPEAT            // 重复按键
} key_event_type_t;

/**
 * @brief 按键事件结构体
 */
typedef struct {
    int pin;                    // 按键引脚
    key_event_type_t type;      // 事件类型
    uint32_t duration;          // 持续时间（毫秒）
    uint32_t timestamp;         // 时间戳
} key_event_t;

/**
 * @brief 按键配置结构体
 */
typedef struct {
    gpio_num_t pin;             // GPIO引脚
    bool active_low;            // 是否低电平有效
    uint32_t debounce_time;     // 去抖动时间（毫秒）
    uint32_t long_press_time;   // 长按时间（毫秒）
    uint32_t repeat_time;       // 重复按键时间（毫秒）
    bool enable_double_click;   // 是否启用双击检测
    uint32_t double_click_time; // 双击时间间隔（毫秒）
} key_config_t;

/**
 * @brief 按键管理器句柄
 */
typedef void* keymanager_handle_t;

/**
 * @brief 按键事件回调函数类型
 */
typedef void (*key_event_callback_t)(key_event_t event, void* user_data);

/**
 * @brief 初始化按键管理器
 * 
 * @param[out] handle 返回的按键管理器句柄
 * @return esp_err_t 
 */
esp_err_t keymanager_init(keymanager_handle_t* handle);

/**
 * @brief 释放按键管理器资源
 * 
 * @param handle 按键管理器句柄
 * @return esp_err_t 
 */
esp_err_t keymanager_deinit(keymanager_handle_t handle);

/**
 * @brief 添加按键到管理器
 * 
 * @param handle 按键管理器句柄
 * @param config 按键配置
 * @return esp_err_t 
 */
esp_err_t keymanager_add_key(keymanager_handle_t handle, const key_config_t* config);

/**
 * @brief 移除按键
 * 
 * @param handle 按键管理器句柄
 * @param pin 按键引脚
 * @return esp_err_t 
 */
esp_err_t keymanager_remove_key(keymanager_handle_t handle, gpio_num_t pin);

/**
 * @brief 注册按键事件回调
 * 
 * @param handle 按键管理器句柄
 * @param callback 回调函数
 * @param user_data 用户数据
 * @return esp_err_t 
 */
esp_err_t keymanager_register_callback(keymanager_handle_t handle, 
                                      key_event_callback_t callback, 
                                      void* user_data);

/**
 * @brief 获取按键事件队列
 * 
 * @param handle 按键管理器句柄
 * @return QueueHandle_t 事件队列句柄
 */
QueueHandle_t keymanager_get_event_queue(keymanager_handle_t handle);

/**
 * @brief 设置按键使能状态
 * 
 * @param handle 按键管理器句柄
 * @param pin 按键引脚
 * @param enabled 使能状态
 * @return esp_err_t 
 */
esp_err_t keymanager_set_enabled(keymanager_handle_t handle, gpio_num_t pin, bool enabled);

/**
 * @brief 获取按键状态
 * 
 * @param handle 按键管理器句柄
 * @param pin 按键引脚
 * @param[out] state 返回的按键状态
 * @return esp_err_t 
 */
esp_err_t keymanager_get_state(keymanager_handle_t handle, gpio_num_t pin, bool* state);

/**
 * @brief 按键管理器任务处理函数
 * 
 * @param pvParameters 参数
 */
void keymanager_task(void* pvParameters);

#ifdef __cplusplus
}
#endif

#endif // KEYMANAGER_H