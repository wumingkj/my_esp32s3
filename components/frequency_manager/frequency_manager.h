#ifndef FREQUENCY_MANAGER_H
#define FREQUENCY_MANAGER_H

#include "esp_system.h"
#include "esp_pm.h"
#include "esp_log.h"

#ifdef __cplusplus
extern "C" {
#endif

// 频率模式定义
typedef enum {
    FREQ_MODE_PERFORMANCE = 0,  // 性能模式 - 最高频率
    FREQ_MODE_BALANCED,         // 平衡模式 - 中等频率  
    FREQ_MODE_POWER_SAVE,       // 省电模式 - 最低频率
    FREQ_MODE_CUSTOM            // 自定义模式
} frequency_mode_t;

// 频率管理器配置
typedef struct {
    frequency_mode_t current_mode;
    int performance_freq;   // 性能模式频率 (MHz)
    int balanced_freq;      // 平衡模式频率 (MHz)
    int power_save_freq;    // 省电模式频率 (MHz)
    int custom_freq;        // 自定义模式频率 (MHz)
} frequency_manager_config_t;

/**
 * @brief 初始化频率管理器
 * @param config 频率管理器配置
 * @return esp_err_t 错误码
 */
esp_err_t frequency_manager_init(frequency_manager_config_t *config);

/**
 * @brief 设置频率模式
 * @param mode 频率模式
 * @return esp_err_t 错误码
 */
esp_err_t frequency_manager_set_mode(frequency_mode_t mode);

/**
 * @brief 设置自定义频率
 * @param freq_mhz 频率值 (MHz)
 * @return esp_err_t 错误码
 */
esp_err_t frequency_manager_set_custom_frequency(int freq_mhz);

/**
 * @brief 获取当前频率模式
 * @return frequency_mode_t 当前模式
 */
frequency_mode_t frequency_manager_get_current_mode(void);

/**
 * @brief 获取当前CPU频率
 * @return double 当前频率 (MHz)
 */
double frequency_manager_get_current_frequency(void);

#ifdef __cplusplus
}
#endif

#endif // FREQUENCY_MANAGER_H