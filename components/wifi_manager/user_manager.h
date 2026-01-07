#ifndef USER_MANAGER_H
#define USER_MANAGER_H

#include <stdbool.h>
#include "esp_err.h"
#include "cJSON.h"

#define MAX_USERNAME_LEN 32
#define MAX_PASSWORD_LEN 64
#define MAX_USERS 10
#define MAX_WHITELIST_MACS 20

typedef struct {
    char username[MAX_USERNAME_LEN];
    char password[MAX_PASSWORD_LEN];
    int role; // 0: 普通用户, 1: 管理员
} user_t;

// 使用whitelist_manager.h中的完整定义
// typedef struct whitelist_mac_s whitelist_mac_t;

// 用户管理函数
esp_err_t user_manager_init(void);
esp_err_t user_manager_load_users(void);
esp_err_t user_manager_save_users(void);
bool user_manager_authenticate(const char* username, const char* password);
esp_err_t user_manager_add_user(const char* username, const char* password, int role);
esp_err_t user_manager_delete_user(const char* username);
esp_err_t user_manager_update_user(const char* username, const char* password, int role);
user_t* user_manager_get_user(const char* username);
user_t** user_manager_get_all_users(int* count);

// 设备信息结构
typedef struct {
    char hostname[64];
    char ip[16];
    char mac[18];
} device_info_t;

// 设备管理函数
esp_err_t device_manager_init(void);
esp_err_t device_manager_refresh_devices(void);
device_info_t** device_manager_get_devices(int* count);

#endif // USER_MANAGER_H