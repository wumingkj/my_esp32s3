#include "whitelist_manager.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "WhitelistManager";

// 白名单数组
static whitelist_mac_t** g_whitelist_macs = NULL;
static int g_whitelist_count = 0;

// 默认白名单MAC地址（示例）
static const char* DEFAULT_WHITELIST_MACS[] = {
    "AA:BB:CC:11:22:33",
    "DD:EE:FF:44:55:66"
};

static const int DEFAULT_WHITELIST_COUNT = 2;

// NVS命名空间
static const char* NVS_NAMESPACE = "whitelist";
static const char* NVS_KEY_COUNT = "count";
static const char* NVS_KEY_PREFIX = "mac_";

// 初始化白名单管理器
esp_err_t whitelist_manager_init(void) {
    ESP_LOGI(TAG, "Initializing whitelist manager");
    
    // 从NVS加载白名单
    esp_err_t ret = whitelist_manager_load_macs();
    if (ret != ESP_OK) {
        ESP_LOGI(TAG, "Failed to load whitelist from NVS, using default whitelist");
        
        // 使用默认白名单
        g_whitelist_count = DEFAULT_WHITELIST_COUNT;
        g_whitelist_macs = malloc(sizeof(whitelist_mac_t*) * g_whitelist_count);
        
        if (!g_whitelist_macs) {
            ESP_LOGE(TAG, "Failed to allocate memory for whitelist");
            return ESP_ERR_NO_MEM;
        }
        
        for (int i = 0; i < g_whitelist_count; i++) {
            g_whitelist_macs[i] = malloc(sizeof(whitelist_mac_t));
            if (!g_whitelist_macs[i]) {
                ESP_LOGE(TAG, "Failed to allocate memory for whitelist entry");
                // 清理已分配的内存
                for (int j = 0; j < i; j++) {
                    free(g_whitelist_macs[j]);
                }
                free(g_whitelist_macs);
                g_whitelist_macs = NULL;
                return ESP_ERR_NO_MEM;
            }
            
            strncpy(g_whitelist_macs[i]->mac, DEFAULT_WHITELIST_MACS[i], sizeof(g_whitelist_macs[i]->mac) - 1);
            g_whitelist_macs[i]->mac[sizeof(g_whitelist_macs[i]->mac) - 1] = '\0';
            snprintf(g_whitelist_macs[i]->description, sizeof(g_whitelist_macs[i]->description), 
                     "Default whitelist entry %d", i + 1);
        }
        
        // 保存默认白名单到NVS
        whitelist_manager_save_macs();
    }
    
    ESP_LOGI(TAG, "Whitelist manager initialized with %d entries", g_whitelist_count);
    return ESP_OK;
}

// 检查MAC地址是否在白名单中
bool whitelist_manager_check_mac(const char* mac) {
    if (!g_whitelist_macs || !mac) {
        return false;
    }
    
    for (int i = 0; i < g_whitelist_count; i++) {
        if (strcasecmp(g_whitelist_macs[i]->mac, mac) == 0) {
            ESP_LOGI(TAG, "MAC %s found in whitelist", mac);
            return true;
        }
    }
    
    ESP_LOGI(TAG, "MAC %s not found in whitelist", mac);
    return false;
}

// 添加MAC地址到白名单
esp_err_t whitelist_manager_add_mac(const char* mac, const char* description) {
    if (!mac) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // 检查是否已存在
    if (whitelist_manager_check_mac(mac)) {
        ESP_LOGW(TAG, "MAC %s already exists in whitelist", mac);
        return ESP_ERR_INVALID_STATE;
    }
    
    // 重新分配内存
    whitelist_mac_t** new_macs = realloc(g_whitelist_macs, sizeof(whitelist_mac_t*) * (g_whitelist_count + 1));
    if (!new_macs) {
        ESP_LOGE(TAG, "Failed to reallocate memory for whitelist");
        return ESP_ERR_NO_MEM;
    }
    
    g_whitelist_macs = new_macs;
    
    // 分配新条目内存
    g_whitelist_macs[g_whitelist_count] = malloc(sizeof(whitelist_mac_t));
    if (!g_whitelist_macs[g_whitelist_count]) {
        ESP_LOGE(TAG, "Failed to allocate memory for new whitelist entry");
        return ESP_ERR_NO_MEM;
    }
    
    // 设置MAC地址和描述
    strncpy(g_whitelist_macs[g_whitelist_count]->mac, mac, sizeof(g_whitelist_macs[g_whitelist_count]->mac) - 1);
    g_whitelist_macs[g_whitelist_count]->mac[sizeof(g_whitelist_macs[g_whitelist_count]->mac) - 1] = '\0';
    
    if (description) {
        strncpy(g_whitelist_macs[g_whitelist_count]->description, description, 
                sizeof(g_whitelist_macs[g_whitelist_count]->description) - 1);
    } else {
        strncpy(g_whitelist_macs[g_whitelist_count]->description, "Added via API", 
                sizeof(g_whitelist_macs[g_whitelist_count]->description) - 1);
    }
    g_whitelist_macs[g_whitelist_count]->description[sizeof(g_whitelist_macs[g_whitelist_count]->description) - 1] = '\0';
    
    g_whitelist_count++;
    
    ESP_LOGI(TAG, "Added MAC %s to whitelist", mac);
    
    // 保存到NVS
    return whitelist_manager_save_macs();
}

// 从白名单中删除MAC地址
esp_err_t whitelist_manager_remove_mac(const char* mac) {
    if (!g_whitelist_macs || !mac) {
        return ESP_ERR_INVALID_ARG;
    }
    
    for (int i = 0; i < g_whitelist_count; i++) {
        if (strcasecmp(g_whitelist_macs[i]->mac, mac) == 0) {
            // 找到要删除的条目
            free(g_whitelist_macs[i]);
            
            // 移动后续条目
            for (int j = i; j < g_whitelist_count - 1; j++) {
                g_whitelist_macs[j] = g_whitelist_macs[j + 1];
            }
            
            g_whitelist_count--;
            
            // 重新分配内存
            if (g_whitelist_count > 0) {
                whitelist_mac_t** new_macs = realloc(g_whitelist_macs, sizeof(whitelist_mac_t*) * g_whitelist_count);
                if (new_macs) {
                    g_whitelist_macs = new_macs;
                }
            } else {
                free(g_whitelist_macs);
                g_whitelist_macs = NULL;
            }
            
            ESP_LOGI(TAG, "Removed MAC %s from whitelist", mac);
            
            // 保存到NVS
            return whitelist_manager_save_macs();
        }
    }
    
    ESP_LOGW(TAG, "MAC %s not found in whitelist", mac);
    return ESP_ERR_NOT_FOUND;
}

// 获取所有白名单MAC地址
whitelist_mac_t** whitelist_manager_get_all_macs(int* count) {
    if (count) {
        *count = g_whitelist_count;
    }
    return g_whitelist_macs;
}

// 保存白名单到NVS
esp_err_t whitelist_manager_save_macs(void) {
    nvs_handle_t nvs_handle;
    esp_err_t ret;
    
    ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS namespace: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // 保存数量
    ret = nvs_set_i32(nvs_handle, NVS_KEY_COUNT, g_whitelist_count);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save whitelist count: %s", esp_err_to_name(ret));
        nvs_close(nvs_handle);
        return ret;
    }
    
    // 保存每个MAC地址
    for (int i = 0; i < g_whitelist_count; i++) {
        char key[32];
        snprintf(key, sizeof(key), "%s%d", NVS_KEY_PREFIX, i);
        
        // 保存MAC地址和描述（用分号分隔）
        char value[128];
        snprintf(value, sizeof(value), "%s;%s", 
                 g_whitelist_macs[i]->mac, g_whitelist_macs[i]->description);
        
        ret = nvs_set_str(nvs_handle, key, value);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to save whitelist entry %d: %s", i, esp_err_to_name(ret));
            nvs_close(nvs_handle);
            return ret;
        }
    }
    
    ret = nvs_commit(nvs_handle);
    nvs_close(nvs_handle);
    
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Whitelist saved to NVS with %d entries", g_whitelist_count);
    } else {
        ESP_LOGE(TAG, "Failed to commit whitelist to NVS: %s", esp_err_to_name(ret));
    }
    
    return ret;
}

// 从NVS加载白名单
esp_err_t whitelist_manager_load_macs(void) {
    nvs_handle_t nvs_handle;
    esp_err_t ret;
    
    ret = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (ret != ESP_OK) {
        ESP_LOGI(TAG, "NVS namespace not found, using default whitelist");
        return ret;
    }
    
    // 获取数量
    int32_t count = 0;
    ret = nvs_get_i32(nvs_handle, NVS_KEY_COUNT, &count);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get whitelist count: %s", esp_err_to_name(ret));
        nvs_close(nvs_handle);
        return ret;
    }
    
    if (count <= 0) {
        ESP_LOGI(TAG, "No whitelist entries found in NVS");
        nvs_close(nvs_handle);
        return ESP_ERR_NOT_FOUND;
    }
    
    // 分配内存
    g_whitelist_macs = malloc(sizeof(whitelist_mac_t*) * count);
    if (!g_whitelist_macs) {
        ESP_LOGE(TAG, "Failed to allocate memory for whitelist");
        nvs_close(nvs_handle);
        return ESP_ERR_NO_MEM;
    }
    
    g_whitelist_count = count;
    
    // 加载每个MAC地址
    for (int i = 0; i < count; i++) {
        char key[32];
        snprintf(key, sizeof(key), "%s%d", NVS_KEY_PREFIX, i);
        
        size_t required_size = 0;
        ret = nvs_get_str(nvs_handle, key, NULL, &required_size);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to get size for whitelist entry %d: %s", i, esp_err_to_name(ret));
            break;
        }
        
        char* value = malloc(required_size);
        if (!value) {
            ESP_LOGE(TAG, "Failed to allocate memory for whitelist value");
            ret = ESP_ERR_NO_MEM;
            break;
        }
        
        ret = nvs_get_str(nvs_handle, key, value, &required_size);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to get whitelist entry %d: %s", i, esp_err_to_name(ret));
            free(value);
            break;
        }
        
        // 解析MAC地址和描述
        g_whitelist_macs[i] = malloc(sizeof(whitelist_mac_t));
        if (!g_whitelist_macs[i]) {
            ESP_LOGE(TAG, "Failed to allocate memory for whitelist entry");
            free(value);
            ret = ESP_ERR_NO_MEM;
            break;
        }
        
        char* mac_part = strtok(value, ";");
        char* desc_part = strtok(NULL, ";");
        
        if (mac_part) {
            strncpy(g_whitelist_macs[i]->mac, mac_part, sizeof(g_whitelist_macs[i]->mac) - 1);
            g_whitelist_macs[i]->mac[sizeof(g_whitelist_macs[i]->mac) - 1] = '\0';
        }
        
        if (desc_part) {
            strncpy(g_whitelist_macs[i]->description, desc_part, sizeof(g_whitelist_macs[i]->description) - 1);
        } else {
            strncpy(g_whitelist_macs[i]->description, "Loaded from NVS", sizeof(g_whitelist_macs[i]->description) - 1);
        }
        g_whitelist_macs[i]->description[sizeof(g_whitelist_macs[i]->description) - 1] = '\0';
        
        free(value);
    }
    
    nvs_close(nvs_handle);
    
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Whitelist loaded from NVS with %d entries", g_whitelist_count);
    } else {
        // 清理已加载的条目
        for (int i = 0; i < g_whitelist_count; i++) {
            if (g_whitelist_macs[i]) {
                free(g_whitelist_macs[i]);
            }
        }
        free(g_whitelist_macs);
        g_whitelist_macs = NULL;
        g_whitelist_count = 0;
    }
    
    return ret;
}