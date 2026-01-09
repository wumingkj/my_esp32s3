#include "user_manager.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "esp_err.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "littlefs_manager.h"
#include "whitelist_manager.h"

static const char* TAG = "UserManager";

static user_t g_users[MAX_USERS];
static int g_user_count = 0;
static whitelist_mac_t g_whitelist_macs[MAX_WHITELIST_MACS];
static int g_whitelist_count = 0;
static device_info_t* g_devices = NULL;
static int g_device_count = 0;

// 初始化用户管理器
esp_err_t user_manager_init(void) {
    ESP_LOGI(TAG, "Initializing user manager...");

    // 确保文件系统已挂载
    if (!littlefs_manager_is_mounted()) {
        ESP_LOGE(TAG, "LittleFS not mounted");
        return ESP_FAIL;
    }

    // 加载用户数据
    esp_err_t ret = user_manager_load_users();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to load users, creating default admin user");
        // 创建默认管理员用户
        user_manager_add_user("admin", "admin", 1);
        user_manager_save_users();
    }

    // 加载白名单
    ret = whitelist_manager_load_macs();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to load whitelist, creating empty whitelist");
        whitelist_manager_save_macs();
    }

    // 初始化设备管理器
    ret = device_manager_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize device manager");
    }

    ESP_LOGI(TAG, "User manager initialized successfully");
    return ESP_OK;
}

// 加载用户数据
esp_err_t user_manager_load_users(void) {
    char* content = littlefs_manager_read_file("/config/users.json");
    if (content == NULL) {
        ESP_LOGW(TAG, "Users file not found, will create default");
        return ESP_FAIL;
    }

    cJSON* root = cJSON_Parse(content);
    if (root == NULL) {
        ESP_LOGE(TAG, "Failed to parse users JSON");
        free(content);
        return ESP_FAIL;
    }

    cJSON* users_array = cJSON_GetObjectItem(root, "users");
    if (users_array == NULL || !cJSON_IsArray(users_array)) {
        ESP_LOGE(TAG, "Invalid users JSON format");
        cJSON_Delete(root);
        free(content);
        return ESP_FAIL;
    }

    g_user_count = 0;
    cJSON* user_item = NULL;
    cJSON_ArrayForEach(user_item, users_array) {
        if (g_user_count >= MAX_USERS) {
            ESP_LOGW(TAG, "Maximum user count reached");
            break;
        }

        cJSON* username = cJSON_GetObjectItem(user_item, "username");
        cJSON* password = cJSON_GetObjectItem(user_item, "password");
        cJSON* role = cJSON_GetObjectItem(user_item, "role");

        if (username && password && role) {
            strncpy(g_users[g_user_count].username, username->valuestring, MAX_USERNAME_LEN - 1);
            strncpy(g_users[g_user_count].password, password->valuestring, MAX_PASSWORD_LEN - 1);
            g_users[g_user_count].role = role->valueint;
            g_user_count++;
        }
    }

    cJSON_Delete(root);
    free(content);

    ESP_LOGI(TAG, "Loaded %d users", g_user_count);
    return ESP_OK;
}

// 保存用户数据
esp_err_t user_manager_save_users(void) {
    cJSON* root = cJSON_CreateObject();
    cJSON* users_array = cJSON_CreateArray();

    for (int i = 0; i < g_user_count; i++) {
        cJSON* user_obj = cJSON_CreateObject();
        cJSON_AddStringToObject(user_obj, "username", g_users[i].username);
        cJSON_AddStringToObject(user_obj, "password", g_users[i].password);
        cJSON_AddNumberToObject(user_obj, "role", g_users[i].role);
        cJSON_AddItemToArray(users_array, user_obj);
    }

    cJSON_AddItemToObject(root, "users", users_array);

    char* json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    if (json_str == NULL) {
        ESP_LOGE(TAG, "Failed to create JSON string");
        return ESP_FAIL;
    }

    // 确保config目录存在
    littlefs_manager_create_dir("/config");

    bool success = littlefs_manager_write_file("/config/users.json", json_str);
    free(json_str);

    if (!success) {
        ESP_LOGE(TAG, "Failed to save users file");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Saved %d users", g_user_count);
    return ESP_OK;
}

// 用户认证
bool user_manager_authenticate(const char* username, const char* password) {
    for (int i = 0; i < g_user_count; i++) {
        if (strcmp(g_users[i].username, username) == 0 && strcmp(g_users[i].password, password) == 0) {
            return true;
        }
    }
    return false;
}

// 添加用户
esp_err_t user_manager_add_user(const char* username, const char* password, int role) {
    if (g_user_count >= MAX_USERS) {
        ESP_LOGE(TAG, "Maximum user count reached");
        return ESP_FAIL;
    }

    // 检查用户名是否已存在
    for (int i = 0; i < g_user_count; i++) {
        if (strcmp(g_users[i].username, username) == 0) {
            ESP_LOGE(TAG, "Username already exists");
            return ESP_FAIL;
        }
    }

    strncpy(g_users[g_user_count].username, username, MAX_USERNAME_LEN - 1);
    strncpy(g_users[g_user_count].password, password, MAX_PASSWORD_LEN - 1);
    g_users[g_user_count].role = role;
    g_user_count++;

    ESP_LOGI(TAG, "User added: %s", username);
    return ESP_OK;
}

// 删除用户
esp_err_t user_manager_delete_user(const char* username) {
    for (int i = 0; i < g_user_count; i++) {
        if (strcmp(g_users[i].username, username) == 0) {
            // 移动后续用户
            for (int j = i; j < g_user_count - 1; j++) {
                memcpy(&g_users[j], &g_users[j + 1], sizeof(user_t));
            }
            g_user_count--;
            ESP_LOGI(TAG, "User deleted: %s", username);
            return ESP_OK;
        }
    }

    ESP_LOGE(TAG, "User not found: %s", username);
    return ESP_FAIL;
}

// 更新用户
esp_err_t user_manager_update_user(const char* username, const char* password, int role) {
    for (int i = 0; i < g_user_count; i++) {
        if (strcmp(g_users[i].username, username) == 0) {
            if (password != NULL) {
                strncpy(g_users[i].password, password, MAX_PASSWORD_LEN - 1);
            }
            g_users[i].role = role;
            ESP_LOGI(TAG, "User updated: %s", username);
            return ESP_OK;
        }
    }

    ESP_LOGE(TAG, "User not found: %s", username);
    return ESP_FAIL;
}

// 获取用户
user_t* user_manager_get_user(const char* username) {
    for (int i = 0; i < g_user_count; i++) {
        if (strcmp(g_users[i].username, username) == 0) {
            return &g_users[i];
        }
    }
    return NULL;
}

// 获取所有用户
user_t** user_manager_get_all_users(int* count) {
    *count = g_user_count;
    return g_user_count > 0 ? (user_t**)g_users : NULL;
}

// 设备管理函数

// 初始化设备管理器
esp_err_t device_manager_init(void) {
    ESP_LOGI(TAG, "Initializing device manager...");

    // 刷新设备列表
    return device_manager_refresh_devices();
}

// 刷新设备列表
esp_err_t device_manager_refresh_devices(void) {
    // 释放之前的设备列表
    if (g_devices != NULL) {
        free(g_devices);
        g_devices = NULL;
        g_device_count = 0;
    }

    // 这里应该实现获取AP下连接设备的功能
    // 由于ESP-IDF的限制，获取AP下设备信息比较复杂
    // 这里先返回空列表，后续可以扩展实现

    ESP_LOGI(TAG, "Device list refreshed (currently empty)");
    return ESP_OK;
}

// 获取设备列表
device_info_t** device_manager_get_devices(int* count) {
    *count = g_device_count;
    return &g_devices;
}