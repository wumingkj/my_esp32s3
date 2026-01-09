#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include "cJSON.h"
#include "device_mapping.h"  // 添加设备映射表头文件
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_mac.h"  // 添加esp_mac.h头文件
#include "esp_netif.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "lwip/ip4_addr.h"
#include "lwip/lwip_napt.h"
#include "nvs_flash.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MAX_APS 5  // 最大AP数量

// WiFi配置结构体 - 包含AP和STA配置
typedef struct {
    char ap_ssid[32];
    char ap_password[64];
    char sta_ssid[32];
    char sta_password[64];
    bool enable_nat;
    bool enable_dhcp_server;
} wifi_manager_config_t;

// 网络信息结构体 - 简化，只保留AP相关信息
typedef struct {
    char ap_ip[16];
    char netmask[16];
} network_info_t;

// AP信息结构体
typedef struct {
    char ap_name[32];  // AP名称（如"ap1", "ap2", "ap3"）
    char ssid[32];
    char password[64];
    char ip[16];
    int connected_devices;  // 连接的设备数量
} ap_info_t;

// WiFi管理器配置
typedef struct {
    wifi_manager_config_t wifi_config;
    network_info_t network_info;
    esp_netif_t* ap_netif;
    httpd_handle_t server;
    ap_info_t aps[MAX_APS];  // 多AP信息
    int ap_count;            // 当前AP数量
} wifi_manager_t;

/**
 * @brief 初始化WiFi管理器
 * @param config WiFi配置
 * @return esp_err_t 错误码
 */
esp_err_t wifi_manager_init(wifi_manager_config_t* config);

/**
 * @brief 启动AP模式
 * @param ap_name AP名称（如"ap1", "ap2", "ap3"）
 * @param ssid AP的SSID
 * @param password AP的密码
 * @return esp_err_t 错误码
 */
esp_err_t wifi_manager_start_ap(const char* ap_name, const char* ssid, const char* password);

/**
 * @brief 获取网络信息
 * @param info 网络信息结构体指针
 * @return esp_err_t 错误码
 */
esp_err_t wifi_manager_get_network_info(network_info_t* info);

/**
 * @brief 启动Web服务器
 * @return esp_err_t 错误码
 */
esp_err_t wifi_manager_start_web_server(void);

/**
 * @brief 保存WiFi配置到NVS
 * @param config WiFi配置
 * @return esp_err_t 错误码
 */
esp_err_t wifi_manager_save_config(wifi_manager_config_t* config);

/**
 * @brief 从NVS加载WiFi配置
 * @param config WiFi配置指针
 * @return esp_err_t 错误码
 */
esp_err_t wifi_manager_load_config(wifi_manager_config_t* config);

/**
 * @brief 获取指定AP下连接的设备列表
 * @param ap_name AP名称
 * @param count 设备数量指针
 * @return device_mapping_t** 设备数组
 */
device_mapping_t** wifi_manager_get_connected_devices(const char* ap_name, int* count);

/**
 * @brief 通过主机名查找设备（在指定AP中）
 * @param ap_name AP名称
 * @param hostname 主机名
 * @param result 查找结果
 * @return esp_err_t 错误码
 */
esp_err_t wifi_manager_find_device_by_hostname(const char* ap_name, const char* hostname,
                                               device_lookup_result_t* result);

/**
 * @brief 通过IP地址查找设备（在指定AP中）
 * @param ap_name AP名称
 * @param ip IP地址
 * @param result 查找结果
 * @return esp_err_t 错误码
 */
esp_err_t wifi_manager_find_device_by_ip(const char* ap_name, const char* ip, device_lookup_result_t* result);

/**
 * @brief 通过MAC地址查找设备（在指定AP中）
 * @param ap_name AP名称
 * @param mac MAC地址
 * @param result 查找结果
 * @return esp_err_t 错误码
 */
esp_err_t wifi_manager_find_device_by_mac(const char* ap_name, const char* mac, device_lookup_result_t* result);

/**
 * @brief 扫描并获取连接到指定AP的设备列表
 * @param ap_name AP名称
 * @return esp_err_t 错误码
 */
esp_err_t wifi_manager_scan_connected_devices(const char* ap_name);

/**
 * @brief 获取AP信息列表
 * @param count AP数量指针
 * @return ap_info_t** AP信息数组
 */
ap_info_t** wifi_manager_get_ap_list(int* count);

// 移除这行，因为函数声明应该在device_mapping.h中
// device_mapping_t** device_mapping_get_all_devices_ex(int* count, bool exclude_unknown);

#ifdef __cplusplus
}
#endif

#endif  // WIFI_MANAGER_H