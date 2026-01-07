#include "frequency_manager.h"
#include "partition_manager.h"
#include "esp_system.h"
#include "esp_err.h"

static const char *TAG = "FrequencyManager";
static frequency_manager_config_t *g_config = NULL;

esp_err_t frequency_manager_init(frequency_manager_config_t *config) {
    if (config == NULL) {
        ESP_LOGE(TAG, "配置参数不能为NULL");
        return ESP_ERR_INVALID_ARG;
    }
    
    // 验证频率值
    if (config->performance_freq < 80 || config->performance_freq > 240 ||
        config->balanced_freq < 80 || config->balanced_freq > 240 ||
        config->power_save_freq < 80 || config->power_save_freq > 240) {
        ESP_LOGE(TAG, "频率值无效 (有效范围: 80-240MHz)");
        return ESP_ERR_INVALID_ARG;
    }
    
    g_config = config;
    ESP_LOGI(TAG, "频率管理器初始化完成");
    
    // 设置初始模式
    return frequency_manager_set_mode(config->current_mode);
}

esp_err_t frequency_manager_set_mode(frequency_mode_t mode) {
    if (g_config == NULL) {
        ESP_LOGE(TAG, "频率管理器未初始化");
        return ESP_ERR_INVALID_STATE;
    }
    
    int target_freq;
    
    switch (mode) {
        case FREQ_MODE_PERFORMANCE:
            target_freq = g_config->performance_freq;
            ESP_LOGI(TAG, "切换到性能模式");
            break;
        case FREQ_MODE_BALANCED:
            target_freq = g_config->balanced_freq;
            ESP_LOGI(TAG, "切换到平衡模式");
            break;
        case FREQ_MODE_POWER_SAVE:
            target_freq = g_config->power_save_freq;
            ESP_LOGI(TAG, "切换到省电模式");
            break;
        case FREQ_MODE_CUSTOM:
            target_freq = g_config->custom_freq;
            ESP_LOGI(TAG, "切换到自定义模式");
            break;
        default:
            ESP_LOGE(TAG, "未知的频率模式: %d", mode);
            return ESP_ERR_INVALID_ARG;
    }
    
    g_config->current_mode = mode;
    return partition_manager_set_cpu_frequency(target_freq);
}

esp_err_t frequency_manager_set_custom_frequency(int freq_mhz) {
    if (g_config == NULL) {
        ESP_LOGE(TAG, "频率管理器未初始化");
        return ESP_ERR_INVALID_STATE;
    }
    
    g_config->custom_freq = freq_mhz;
    return frequency_manager_set_mode(FREQ_MODE_CUSTOM);
}

frequency_mode_t frequency_manager_get_current_mode(void) {
    if (g_config == NULL) {
        return FREQ_MODE_BALANCED;
    }
    return g_config->current_mode;
}

double frequency_manager_get_current_frequency(void) {
    return partition_manager_get_cpu_frequency();
}