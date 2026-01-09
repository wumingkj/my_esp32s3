#ifndef DEVICE_MAPPING_H
#define DEVICE_MAPPING_H

#include <stdbool.h>
#include <string.h>

#include "esp_err.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "esp_wifi.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MAX_DEVICES 50
#define MAX_HOSTNAME_LEN 128
#define MAX_IP_LEN 16
#define MAX_MAC_LEN 18

// 设备信息结构体
typedef struct {
    char hostname[MAX_HOSTNAME_LEN];  // 主机名
    char ip[MAX_IP_LEN];              // IP地址
    char mac[MAX_MAC_LEN];            // MAC地址
    uint32_t last_seen;               // 最后发现时间
    bool is_active;                   // 是否活跃
} device_mapping_t;

// 查找结果结构体
typedef struct {
    device_mapping_t* device;  // 找到的设备
    int index;                 // 设备索引
} device_lookup_result_t;

/**
 * @brief 初始化设备映射表
 * @return esp_err_t 错误码
 */
esp_err_t device_mapping_init(void);

/**
 * @brief 添加或更新设备映射
 * @param hostname 主机名
 * @param ip IP地址
 * @param mac MAC地址
 * @return esp_err_t 错误码
 */
esp_err_t device_mapping_add_device(const char* hostname, const char* ip, const char* mac);

/**
 * @brief 通过主机名查找设备
 * @param hostname 主机名
 * @param result 查找结果
 * @return esp_err_t 错误码
 */
esp_err_t device_mapping_find_by_hostname(const char* hostname, device_lookup_result_t* result);

/**
 * @brief 通过IP地址查找设备
 * @param ip IP地址
 * @param result 查找结果
 * @return esp_err_t 错误码
 */
esp_err_t device_mapping_find_by_ip(const char* ip, device_lookup_result_t* result);

/**
 * @brief 通过MAC地址查找设备
 * @param mac MAC地址
 * @param result 查找结果
 * @return esp_err_t 错误码
 */
esp_err_t device_mapping_find_by_mac(const char* mac, device_lookup_result_t* result);

/**
 * @brief 删除设备映射
 * @param mac MAC地址
 * @return esp_err_t 错误码
 */
esp_err_t device_mapping_remove_device(const char* mac);

/**
 * @brief 获取所有设备映射
 * @param count 设备数量指针
 * @return device_mapping_t** 设备数组
 */
device_mapping_t** device_mapping_get_all_devices(int* count);

/**
 * @brief 刷新设备状态（标记不活跃设备）
 * @param timeout_seconds 超时时间（秒）
 * @return esp_err_t 错误码
 */
esp_err_t device_mapping_refresh_status(uint32_t timeout_seconds);

/**
 * @brief 保存设备映射到NVS
 * @return esp_err_t 错误码
 */
esp_err_t device_mapping_save_to_nvs(void);

/**
 * @brief 从NVS加载设备映射
 * @return esp_err_t 错误码
 */
esp_err_t device_mapping_load_from_nvs(void);

/**
 * @brief 获取设备数量
 * @return int 设备数量
 */
int device_mapping_get_count(void);

/**
 * @brief 清空设备映射表
 * @return esp_err_t 错误码
 */
esp_err_t device_mapping_clear_all(void);

/**
 * @brief 获取所有设备映射（支持过滤unknown设备）
 * @param count 设备数量指针
 * @param exclude_unknown 是否排除unknown设备
 * @return device_mapping_t** 设备数组
 */
device_mapping_t** device_mapping_get_all_devices_ex(int* count, bool exclude_unknown);

#ifdef __cplusplus
}
#endif

#endif  // DEVICE_MAPPING_H