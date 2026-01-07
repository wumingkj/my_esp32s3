#ifndef PARTITION_MANAGER_H
#define PARTITION_MANAGER_H

#include "esp_system.h"
#include "nvs_flash.h"
#include "esp_partition.h"
#include "esp_pm.h"
#include "esp_log.h"

#ifdef __cplusplus
extern "C" {
#endif

// 电源管理配置结构体
typedef struct {
    int max_freq_mhz;    // 最大CPU频率
    int min_freq_mhz;    // 最小CPU频率
    bool light_sleep_enable; // 是否启用轻睡眠
} power_management_config_t;

// 分区信息结构体
typedef struct {
    char label[32];      // 分区标签
    esp_partition_type_t type;     // 分区类型
    esp_partition_subtype_t subtype; // 子类型
    uint32_t address;    // 偏移地址
    uint32_t size;       // 分区大小
} partition_info_t;

/**
 * @brief 初始化分区管理
 * @return esp_err_t 错误码
 */
esp_err_t partition_manager_init(void);

/**
 * @brief 配置电源管理
 * @param config 电源管理配置
 * @return esp_err_t 错误码
 */
esp_err_t partition_manager_configure_power(power_management_config_t *config);

/**
 * @brief 列出所有分区信息
 */
void partition_manager_list_partitions(void);

/**
 * @brief 获取CPU频率信息
 * @return double 当前CPU频率(MHz)
 */
double partition_manager_get_cpu_frequency(void);

/**
 * @brief 设置CPU频率
 * @param freq_mhz 目标频率(MHz)
 * @return esp_err_t 错误码
 */
esp_err_t partition_manager_set_cpu_frequency(int freq_mhz);

#ifdef __cplusplus
}
#endif

#endif // PARTITION_MANAGER_H