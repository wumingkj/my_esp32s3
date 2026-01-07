#include "device_mapping.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "DeviceMapping";

static device_mapping_t g_devices[MAX_DEVICES];
static int g_device_count = 0;
static bool g_initialized = false;

// NVS相关配置
#define NVS_NAMESPACE "device_mapping"
#define NVS_KEY_COUNT "device_count"
#define NVS_KEY_PREFIX "device_"

/**
 * @brief 初始化设备映射表
 */
esp_err_t device_mapping_init(void) {
    if (g_initialized) {
        return ESP_OK;
    }
    
    // 初始化NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    
    // 从NVS加载设备映射
    ret = device_mapping_load_from_nvs();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to load device mapping from NVS: %s", esp_err_to_name(ret));
    }
    
    g_initialized = true;
    ESP_LOGI(TAG, "Device mapping initialized with %d devices", g_device_count);
    return ESP_OK;
}

/**
 * @brief 添加或更新设备映射
 */
esp_err_t device_mapping_add_device(const char* hostname, const char* ip, const char* mac) {
    if (!g_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (!hostname || !ip || !mac) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // 首先检查是否已存在该设备
    device_lookup_result_t result;
    esp_err_t ret = device_mapping_find_by_mac(mac, &result);
    
    if (ret == ESP_OK && result.device) {
        // 更新现有设备
        strncpy(result.device->hostname, hostname, MAX_HOSTNAME_LEN - 1);
        strncpy(result.device->ip, ip, MAX_IP_LEN - 1);
        result.device->last_seen = xTaskGetTickCount() * portTICK_PERIOD_MS / 1000;
        result.device->is_active = true;
        
        ESP_LOGD(TAG, "Updated device mapping: %s -> %s (%s)", hostname, ip, mac);
    } else {
        // 添加新设备
        if (g_device_count >= MAX_DEVICES) {
            ESP_LOGW(TAG, "Device mapping table is full, cannot add new device");
            return ESP_ERR_NO_MEM;
        }
        
        device_mapping_t* device = &g_devices[g_device_count];
        strncpy(device->hostname, hostname, MAX_HOSTNAME_LEN - 1);
        strncpy(device->ip, ip, MAX_IP_LEN - 1);
        strncpy(device->mac, mac, MAX_MAC_LEN - 1);
        device->last_seen = xTaskGetTickCount() * portTICK_PERIOD_MS / 1000;
        device->is_active = true;
        
        g_device_count++;
        ESP_LOGD(TAG, "Added new device mapping: %s -> %s (%s)", hostname, ip, mac);
    }
    
    // 保存到NVS
    device_mapping_save_to_nvs();
    
    return ESP_OK;
}

/**
 * @brief 通过主机名查找设备（支持unknown设备的特殊处理）
 * @param hostname 主机名
 * @param result 查找结果
 * @return esp_err_t 错误码
 */
esp_err_t device_mapping_find_by_hostname(const char* hostname, device_lookup_result_t* result) {
    if (!hostname || !result) {
        return ESP_ERR_INVALID_ARG;
    }
    
    result->device = NULL;
    result->index = -1;
    
    // 特殊处理：如果查找"unknown"设备，返回第一个unknown设备
    if (strcmp(hostname, "unknown") == 0) {
        for (int i = 0; i < g_device_count; i++) {
            if (strcmp(g_devices[i].hostname, "unknown") == 0) {
                result->device = &g_devices[i];
                result->index = i;
                return ESP_OK;
            }
        }
        return ESP_ERR_NOT_FOUND;
    }
    
    // 正常查找
    for (int i = 0; i < g_device_count; i++) {
        if (strcmp(g_devices[i].hostname, hostname) == 0) {
            result->device = &g_devices[i];
            result->index = i;
            return ESP_OK;
        }
    }
    
    return ESP_ERR_NOT_FOUND;
}

/**
 * @brief 通过IP地址查找设备
 */
esp_err_t device_mapping_find_by_ip(const char* ip, device_lookup_result_t* result) {
    if (!ip || !result) {
        return ESP_ERR_INVALID_ARG;
    }
    
    result->device = NULL;
    result->index = -1;
    
    for (int i = 0; i < g_device_count; i++) {
        if (strcmp(g_devices[i].ip, ip) == 0) {
            result->device = &g_devices[i];
            result->index = i;
            return ESP_OK;
        }
    }
    
    return ESP_ERR_NOT_FOUND;
}

/**
 * @brief 通过MAC地址查找设备
 */
esp_err_t device_mapping_find_by_mac(const char* mac, device_lookup_result_t* result) {
    if (!mac || !result) {
        return ESP_ERR_INVALID_ARG;
    }
    
    result->device = NULL;
    result->index = -1;
    
    for (int i = 0; i < g_device_count; i++) {
        if (strcmp(g_devices[i].mac, mac) == 0) {
            result->device = &g_devices[i];
            result->index = i;
            return ESP_OK;
        }
    }
    
    return ESP_ERR_NOT_FOUND;
}

/**
 * @brief 删除设备映射
 */
esp_err_t device_mapping_remove_device(const char* mac) {
    device_lookup_result_t result;
    esp_err_t ret = device_mapping_find_by_mac(mac, &result);
    
    if (ret != ESP_OK || !result.device) {
        return ESP_ERR_NOT_FOUND;
    }
    
    // 将设备移动到数组末尾
    for (int i = result.index; i < g_device_count - 1; i++) {
        g_devices[i] = g_devices[i + 1];
    }
    
    g_device_count--;
    
    // 保存到NVS
    device_mapping_save_to_nvs();
    
    ESP_LOGD(TAG, "Removed device mapping for MAC: %s", mac);
    return ESP_OK;
}

/**
 * @brief 获取所有设备映射
 * @param count 设备数量指针
 * @return device_mapping_t** 设备数组
 */
device_mapping_t** device_mapping_get_all_devices(int* count) {
    // 调用扩展版本，包含所有设备（不排除unknown设备）
    return device_mapping_get_all_devices_ex(count, false);
}

/**
 * @brief 获取所有设备映射（支持过滤unknown设备）
 * @param count 设备数量指针
 * @param exclude_unknown 是否排除unknown设备
 * @return device_mapping_t** 设备数组
 */
device_mapping_t** device_mapping_get_all_devices_ex(int* count, bool exclude_unknown) {
    static device_mapping_t* device_ptrs[MAX_DEVICES];
    int actual_count = 0;
    
    for (int i = 0; i < g_device_count; i++) {
        if (exclude_unknown && strcmp(g_devices[i].hostname, "unknown") == 0) {
            continue; // 跳过unknown设备
        }
        device_ptrs[actual_count++] = &g_devices[i];
    }
    
    if (count) {
        *count = actual_count;
    }
    
    return device_ptrs;
}

/**
 * @brief 刷新设备状态
 */
esp_err_t device_mapping_refresh_status(uint32_t timeout_seconds) {
    uint32_t current_time = xTaskGetTickCount() * portTICK_PERIOD_MS / 1000;
    
    for (int i = 0; i < g_device_count; i++) {
        if (current_time - g_devices[i].last_seen > timeout_seconds) {
            g_devices[i].is_active = false;
        }
    }
    
    return ESP_OK;
}

/**
 * @brief 保存设备映射到NVS
 */
esp_err_t device_mapping_save_to_nvs(void) {
    nvs_handle_t nvs_handle;
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (ret != ESP_OK) {
        return ret;
    }
    
    // 保存设备数量
    ret = nvs_set_i32(nvs_handle, NVS_KEY_COUNT, g_device_count);
    if (ret != ESP_OK) {
        nvs_close(nvs_handle);
        return ret;
    }
    
    // 保存每个设备
    for (int i = 0; i < g_device_count; i++) {
        char key[32];
        snprintf(key, sizeof(key), "%s%d", NVS_KEY_PREFIX, i);
        
        ret = nvs_set_blob(nvs_handle, key, &g_devices[i], sizeof(device_mapping_t));
        if (ret != ESP_OK) {
            nvs_close(nvs_handle);
            return ret;
        }
    }
    
    nvs_commit(nvs_handle);
    nvs_close(nvs_handle);
    
    ESP_LOGD(TAG, "Saved %d device mappings to NVS", g_device_count);
    return ESP_OK;
}

/**
 * @brief 从NVS加载设备映射
 */
esp_err_t device_mapping_load_from_nvs(void) {
    nvs_handle_t nvs_handle;
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (ret != ESP_OK) {
        return ret;
    }
    
    // 读取设备数量
    int32_t device_count = 0;
    ret = nvs_get_i32(nvs_handle, NVS_KEY_COUNT, &device_count);
    if (ret != ESP_OK) {
        nvs_close(nvs_handle);
        return ret;
    }
    
    if (device_count > MAX_DEVICES) {
        device_count = MAX_DEVICES;
    }
    
    // 读取每个设备
    for (int i = 0; i < device_count; i++) {
        char key[32];
        snprintf(key, sizeof(key), "%s%d", NVS_KEY_PREFIX, i);
        
        size_t required_size = sizeof(device_mapping_t);
        ret = nvs_get_blob(nvs_handle, key, &g_devices[i], &required_size);
        if (ret != ESP_OK) {
            nvs_close(nvs_handle);
            return ret;
        }
    }
    
    g_device_count = device_count;
    nvs_close(nvs_handle);
    
    ESP_LOGD(TAG, "Loaded %d device mappings from NVS", g_device_count);
    return ESP_OK;
}

/**
 * @brief 获取设备数量
 */
int device_mapping_get_count(void) {
    return g_device_count;
}

/**
 * @brief 清空设备映射表
 */
esp_err_t device_mapping_clear_all(void) {
    g_device_count = 0;
    
    // 清除NVS中的设备映射
    nvs_handle_t nvs_handle;
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (ret == ESP_OK) {
        nvs_erase_all(nvs_handle);
        nvs_commit(nvs_handle);
        nvs_close(nvs_handle);
    }
    
    ESP_LOGI(TAG, "Cleared all device mappings");
    return ESP_OK;
}