#ifndef WHITELIST_MANAGER_H
#define WHITELIST_MANAGER_H

#include <stdbool.h>

#include "esp_system.h"

#ifdef __cplusplus
extern "C" {
#endif

// 白名单MAC地址结构体
typedef struct {
    char mac[18];          // MAC地址格式：AA:BB:CC:DD:EE:FF
    char description[64];  // 描述信息
} whitelist_mac_t;

/**
 * @brief 初始化白名单管理器
 * @return esp_err_t 错误码
 */
esp_err_t whitelist_manager_init(void);

/**
 * @brief 检查MAC地址是否在白名单中
 * @param mac MAC地址字符串
 * @return bool 是否在白名单中
 */
bool whitelist_manager_check_mac(const char* mac);

/**
 * @brief 添加MAC地址到白名单
 * @param mac MAC地址
 * @param description 描述信息
 * @return esp_err_t 错误码
 */
esp_err_t whitelist_manager_add_mac(const char* mac, const char* description);

/**
 * @brief 从白名单中删除MAC地址
 * @param mac MAC地址
 * @return esp_err_t 错误码
 */
esp_err_t whitelist_manager_remove_mac(const char* mac);

/**
 * @brief 获取所有白名单MAC地址
 * @param count MAC地址数量指针
 * @return whitelist_mac_t** MAC地址数组
 */
whitelist_mac_t** whitelist_manager_get_all_macs(int* count);

/**
 * @brief 保存白名单到NVS
 * @return esp_err_t 错误码
 */
esp_err_t whitelist_manager_save_macs(void);

/**
 * @brief 从NVS加载白名单
 * @return esp_err_t 错误码
 */
esp_err_t whitelist_manager_load_macs(void);

#ifdef __cplusplus
}
#endif

#endif  // WHITELIST_MANAGER_H