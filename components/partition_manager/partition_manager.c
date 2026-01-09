#include "partition_manager.h"
#include <stdio.h>
#include "esp_system.h"
#include "nvs_flash.h"
#include "esp_err.h"
#include "esp_rom_sys.h"  // 使用这个头文件替代esp_clk.h

static const char *TAG = "PartitionManager";

esp_err_t partition_manager_init(void) {
    esp_err_t ret = nvs_flash_init();
    
    // 如果NVS分区需要擦除
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGI(TAG, "NVS partition needs erase, performing erase operation...");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    
    ESP_ERROR_CHECK(ret);
    ESP_LOGI(TAG, "Partition manager initialization completed");
    
    return ESP_OK;
}

esp_err_t partition_manager_configure_power(power_management_config_t *config) {
#ifdef CONFIG_PM_ENABLE
    // 使用正确的ESP32-S3电源管理配置结构体
    esp_pm_config_t pm_config = {
        .max_freq_mhz = config->max_freq_mhz,
        .min_freq_mhz = config->min_freq_mhz,
        .light_sleep_enable = config->light_sleep_enable,
    };
    
    esp_err_t err = esp_pm_configure(&pm_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Power management configuration failed: %s", esp_err_to_name(err));
        return err;
    }
    
    ESP_LOGI(TAG, "Power management configured successfully: max_freq=%dMHz, min_freq=%dMHz", 
             config->max_freq_mhz, config->min_freq_mhz);
    return ESP_OK;
#else
    ESP_LOGW(TAG, "Power management not enabled in sdkconfig");
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

void partition_manager_list_partitions(void) {
    ESP_LOGI(TAG, "Partition list:");
    
    esp_partition_iterator_t it = esp_partition_find(ESP_PARTITION_TYPE_ANY, 
                                                     ESP_PARTITION_SUBTYPE_ANY, NULL);
    const esp_partition_t* partition = NULL;
    
    while (it != NULL) {
        partition = esp_partition_get(it);
        if (partition != NULL) {
            ESP_LOGI(TAG, "Label: %s, Type: %d, Subtype: %d, Offset: 0x%lx, Size: %lu bytes",
                     partition->label, partition->type, partition->subtype,
                     (unsigned long)partition->address, (unsigned long)partition->size);
        }
        it = esp_partition_next(it);
    }
    
    esp_partition_iterator_release(it);
}

double partition_manager_get_cpu_frequency(void) {
    // 使用esp_rom_get_cpu_ticks_per_us()来获取CPU频率
    return (double)esp_rom_get_cpu_ticks_per_us();
}

esp_err_t partition_manager_set_cpu_frequency(int freq_mhz) {
#ifdef CONFIG_PM_ENABLE
    // 验证频率值是否有效
    if (freq_mhz < 80 || freq_mhz > 240) {
        ESP_LOGE(TAG, "Invalid CPU frequency: %dMHz (valid range: 80-240MHz)", freq_mhz);
        return ESP_ERR_INVALID_ARG;
    }
    
    // 使用esp_pm_config_t结构体重新配置
    esp_pm_config_t pm_config = {
        .max_freq_mhz = freq_mhz,
        .min_freq_mhz = freq_mhz,  // 设置相同值确保固定频率
        .light_sleep_enable = false,
    };
    
    esp_err_t err = esp_pm_configure(&pm_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set CPU frequency: %s", esp_err_to_name(err));
        return err;
    }
    
    ESP_LOGI(TAG, "CPU frequency set to: %dMHz", freq_mhz);
    return ESP_OK;
#else
    ESP_LOGW(TAG, "Power management not enabled, cannot set CPU frequency");
    return ESP_ERR_NOT_SUPPORTED;
#endif
}