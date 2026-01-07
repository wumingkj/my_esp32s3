#include "wifi_manager.h"
#include "user_manager.h"
#include "session_manager.h"
#include "whitelist_manager.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_http_server.h"
#include "lwip/lwip_napt.h"
#include "lwip/ip4_addr.h"
#include "lwip/inet.h"
#include "nvs_flash.h"
#include "cJSON.h"
#include <string.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "device_mapping.h"

static const char *TAG = "WiFiManager";

static wifi_manager_t g_wifi_manager = {0};

// 在文件开头添加header_too_large_handler声明
// 函数声明 - 修复编译错误
static bool get_client_mac_address(httpd_req_t *req, char *mac_str, size_t mac_str_len);
static esp_err_t validate_api_session(httpd_req_t *req, char *username, size_t username_size);
static esp_err_t send_json_response(httpd_req_t *req, cJSON *json_obj);
static esp_err_t send_error_response(httpd_req_t *req, const char *error_message);
static esp_err_t send_success_response(httpd_req_t *req, const char *message);
static esp_err_t serve_file(httpd_req_t *req, const char *file_path, const char *content_type);
static esp_err_t login_get_handler(httpd_req_t *req);
static esp_err_t login_post_handler(httpd_req_t *req);
static esp_err_t dashboard_get_handler(httpd_req_t *req);
static esp_err_t network_get_handler(httpd_req_t *req);
static esp_err_t controls_get_handler(httpd_req_t *req);
static esp_err_t account_get_handler(httpd_req_t *req);
static esp_err_t backup_restore_get_handler(httpd_req_t *req);
static esp_err_t css_get_handler(httpd_req_t *req);
static esp_err_t js_get_handler(httpd_req_t *req);
static esp_err_t users_api_get_handler(httpd_req_t *req);
static esp_err_t users_api_post_handler(httpd_req_t *req);
static esp_err_t whitelist_api_get_handler(httpd_req_t *req);
static esp_err_t whitelist_api_post_handler(httpd_req_t *req);
static esp_err_t devices_api_get_handler(httpd_req_t *req);
static esp_err_t logout_get_handler(httpd_req_t *req);
static esp_err_t wifi_manager_try_get_device_name(const char *mac, char *hostname, size_t hostname_size);
static esp_err_t header_too_large_handler(httpd_req_t *req); // 添加431错误处理函数声明

/**
 * @brief 获取指定AP下连接的设备列表
 */
device_mapping_t **wifi_manager_get_connected_devices(const char *ap_name, int *count)
{
    // 暂时使用全局设备列表，后续需要修改设备映射表以支持多AP
    return device_mapping_get_all_devices(count);
}

/**
 * @brief 通过主机名查找设备（在指定AP中）
 */
esp_err_t wifi_manager_find_device_by_hostname(const char *ap_name, const char *hostname, device_lookup_result_t *result)
{
    // 暂时忽略AP名称，使用全局查找
    return device_mapping_find_by_hostname(hostname, result);
}

/**
 * @brief 通过IP地址查找设备（在指定AP中）
 */
esp_err_t wifi_manager_find_device_by_ip(const char *ap_name, const char *ip, device_lookup_result_t *result)
{
    // 暂时忽略AP名称，使用全局查找
    return device_mapping_find_by_ip(ip, result);
}

/**
 * @brief 通过MAC地址查找设备（在指定AP中）
 */
esp_err_t wifi_manager_find_device_by_mac(const char *ap_name, const char *mac, device_lookup_result_t *result)
{
    // 暂时忽略AP名称，使用全局查找
    return device_mapping_find_by_mac(mac, result);
}

// 在文件开头添加设备名称解析函数
/**
 * @brief 尝试获取设备的实际名称
 * @param mac MAC地址
 * @param hostname 主机名缓冲区
 * @param hostname_size 缓冲区大小
 * @return esp_err_t 错误码
 */
static esp_err_t wifi_manager_try_get_device_name(const char *mac, char *hostname, size_t hostname_size)
{
    if (!mac || !hostname || hostname_size < 2)
    {
        return ESP_ERR_INVALID_ARG;
    }

    // 方法1: 尝试通过mDNS查询设备名称
    // 在ESP-IDF中，可以使用mdns_query_ptr来查询设备名称
    // 这里简化处理，实际应该实现mDNS查询逻辑

    // 方法2: 尝试通过NetBIOS名称查询
    // 在局域网中，可以通过NetBIOS协议查询设备名称

    // 方法3: 尝试通过DHCP主机名查询
    // 在DHCP分配过程中，设备可能会发送主机名

    // 由于获取设备实际名称需要复杂的网络协议支持，
    // 这里先返回"unknown"，后续可以逐步实现具体的名称解析功能
    strncpy(hostname, "unknown", hostname_size - 1);
    hostname[hostname_size - 1] = '\0';

    return ESP_OK;
}

/**
 * @brief 扫描并获取连接到指定AP的设备列表
 * @param ap_name AP名称
 * @return esp_err_t 错误码
 */
esp_err_t wifi_manager_scan_connected_devices(const char *ap_name)
{
    wifi_sta_list_t sta_list;
    esp_err_t ret = esp_wifi_ap_get_sta_list(&sta_list);

    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to get station list: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "Found %d connected stations in AP %s", sta_list.num, ap_name);

    // 获取每个设备的详细信息
    for (int i = 0; i < sta_list.num; i++)
    {
        wifi_sta_info_t *sta = &sta_list.sta[i];
        char mac_str[18];
        snprintf(mac_str, sizeof(mac_str), "%02X:%02X:%02X:%02X:%02X:%02X",
                 sta->mac[0], sta->mac[1], sta->mac[2],
                 sta->mac[3], sta->mac[4], sta->mac[5]);

        // 在ESP-IDF 5.4中，获取设备IP地址的正确方法
        char ip_str[16] = "unknown";

        // 方法1：通过DHCP服务器获取分配的IP（推荐）
        // 在ESP-IDF 5.4中，可以通过esp_netif_dhcps_get_clients_by_mac()获取
        // 这里简化处理，实际应该查询DHCP服务器

        // 方法2：使用IP事件记录（如果设备已经分配了IP）
        // 在IP_EVENT_AP_STAIPASSIGNED事件中记录IP地址

        // 方法3：使用默认IP模式（192.168.4.x）
        snprintf(ip_str, sizeof(ip_str), "192.168.0.%d", i + 2);

        // 尝试获取设备的实际名称
        char hostname[MAX_HOSTNAME_LEN];
        esp_err_t name_ret = wifi_manager_try_get_device_name(mac_str, hostname, sizeof(hostname));

        if (name_ret != ESP_OK || strcmp(hostname, "unknown") == 0)
        {
            // 如果无法获取设备名称或名称为"unknown"，使用MAC地址后几位作为标识
            snprintf(hostname, sizeof(hostname), "device_%02X%02X%02X",
                     sta->mac[3], sta->mac[4], sta->mac[5]);
        }

        // 添加到设备映射表
        device_mapping_add_device(hostname, ip_str, mac_str);
        ESP_LOGI(TAG, "Device found in AP %s: MAC=%s, IP=%s, Hostname=%s", ap_name, mac_str, ip_str, hostname);
    }

    return ESP_OK;
}
// 在WiFi事件处理函数中修改设备发现逻辑，支持多AP
static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT)
    {
        switch (event_id)
        {
        case WIFI_EVENT_AP_STACONNECTED:
        {
            wifi_event_ap_staconnected_t *event = (wifi_event_ap_staconnected_t *)event_data;
            char mac_str[18];
            snprintf(mac_str, sizeof(mac_str), "%02X:%02X:%02X:%02X:%02X:%02X",
                     event->mac[0], event->mac[1], event->mac[2],
                     event->mac[3], event->mac[4], event->mac[5]);
            // 获取当前AP的名称（默认为ap1）
            char ap_name[32] = "ap1";
            if (g_wifi_manager.ap_count > 0)
            {
                strncpy(ap_name, g_wifi_manager.aps[0].ap_name, sizeof(ap_name) - 1);
            }

            // 尝试获取设备的实际名称
            char hostname[MAX_HOSTNAME_LEN];
            esp_err_t name_ret = wifi_manager_try_get_device_name(mac_str, hostname, sizeof(hostname));

            if (name_ret != ESP_OK || strcmp(hostname, "unknown") == 0)
            {
                // 如果无法获取设备名称或名称为"unknown"，使用MAC地址后几位作为标识
                snprintf(hostname, sizeof(hostname), "device_%02X%02X%02X",
                         event->mac[3], event->mac[4], event->mac[5]);
            }

            // 暂时忽略AP名称，使用全局添加
            device_mapping_add_device(hostname, "unknown", mac_str);

            // 更新AP连接设备计数
            for (int i = 0; i < g_wifi_manager.ap_count; i++)
            {
                if (strcmp(g_wifi_manager.aps[i].ap_name, ap_name) == 0)
                {
                    g_wifi_manager.aps[i].connected_devices++;
                    break;
                }
            }

            ESP_LOGI(TAG, "Station connected to AP %s: MAC=%s, Hostname=%s", ap_name, mac_str, hostname);
        }
        break;
        case WIFI_EVENT_AP_STADISCONNECTED:
        {
            wifi_event_ap_stadisconnected_t *event = (wifi_event_ap_stadisconnected_t *)event_data;
            char mac_str[18];
            snprintf(mac_str, sizeof(mac_str), "%02X:%02X:%02X:%02X:%02X:%02X",
                     event->mac[0], event->mac[1], event->mac[2],
                     event->mac[3], event->mac[4], event->mac[5]);

            // 获取当前AP的名称（默认为ap1）
            char ap_name[32] = "ap1";
            if (g_wifi_manager.ap_count > 0)
            {
                strncpy(ap_name, g_wifi_manager.aps[0].ap_name, sizeof(ap_name) - 1);
            }

            // 更新AP连接设备计数
            for (int i = 0; i < g_wifi_manager.ap_count; i++)
            {
                if (strcmp(g_wifi_manager.aps[i].ap_name, ap_name) == 0)
                {
                    if (g_wifi_manager.aps[i].connected_devices > 0)
                    {
                        g_wifi_manager.aps[i].connected_devices--;
                    }
                    break;
                }
            }

            ESP_LOGI(TAG, "Station disconnected from AP %s: MAC=%s", ap_name, mac_str);
        }
        break;
        default:
            break;
        }
    }
    else if (event_base == IP_EVENT)
    {
        switch (event_id)
        {
        case IP_EVENT_AP_STAIPASSIGNED:
        {
            ip_event_ap_staipassigned_t *event = (ip_event_ap_staipassigned_t *)event_data;
            char ip_str[16];
            char mac_str[18];

            snprintf(ip_str, sizeof(ip_str), IPSTR, IP2STR(&event->ip));
            snprintf(mac_str, sizeof(mac_str), "%02X:%02X:%02X:%02X:%02X:%02X",
                     event->mac[0], event->mac[1], event->mac[2],
                     event->mac[3], event->mac[4], event->mac[5]);

            // 获取当前AP的名称（默认为ap1）
            char ap_name[32] = "ap1";
            if (g_wifi_manager.ap_count > 0)
            {
                strncpy(ap_name, g_wifi_manager.aps[0].ap_name, sizeof(ap_name) - 1);
            }

            // 更新设备映射表中的IP地址
            device_lookup_result_t result;
            // 暂时忽略AP名称，使用全局查找
            if (device_mapping_find_by_mac(mac_str, &result) == ESP_OK && result.device)
            {
                strncpy(result.device->ip, ip_str, MAX_IP_LEN - 1);
                device_mapping_save_to_nvs();
                ESP_LOGI(TAG, "Assigned IP %s to device %s in AP %s", ip_str, mac_str, ap_name);
            }
        }
        break;
        default:
            break;
        }
    }
}

// 修改根目录处理函数 - 移除STA相关逻辑
static esp_err_t root_get_handler(httpd_req_t *req)
{
    // 检查是否有有效的会话
    char username[32] = {0};
    if (validate_session_cookie(req, username, sizeof(username)))
    {
        // 如果有有效会话，直接跳转到仪表盘
        ESP_LOGI(TAG, "Valid session found for user %s, redirecting to dashboard", username);
        httpd_resp_set_status(req, "302 Found");
        httpd_resp_set_hdr(req, "Location", "/dashboard");
        httpd_resp_send(req, NULL, 0);
        return ESP_OK;
    }

    // 直接重定向到登录页面
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "/login");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

// 修改静态文件服务函数，添加更好的错误处理
static esp_err_t serve_file(httpd_req_t *req, const char *file_path, const char *content_type)
{
    // 检查请求头大小
    size_t header_len = httpd_req_get_hdr_value_len(req, "Cookie");
    if (header_len > 1024)
    { // 如果Cookie头超过1KB，可能有问题
        ESP_LOGW(TAG, "Large header detected: %d bytes", header_len);
    }

    // 拼接LittleFS挂载点路径
    char full_path[512];
    snprintf(full_path, sizeof(full_path), "%s/%s", "/littlefs", file_path);

    ESP_LOGI(TAG, "Attempting to open file: %s", full_path);

    FILE *file = fopen(full_path, "r");
    if (!file)
    {
        ESP_LOGE(TAG, "Failed to open file: %s (errno: %d)", full_path, errno);
        httpd_resp_send_404(req);
        return ESP_FAIL;
    }

    char buffer[512];
    size_t read_size;
    esp_err_t ret = ESP_OK;

    httpd_resp_set_type(req, content_type);

    while ((read_size = fread(buffer, 1, sizeof(buffer), file)) > 0)
    {
        if (httpd_resp_send_chunk(req, buffer, read_size) != ESP_OK)
        {
            ret = ESP_FAIL;
            break;
        }
    }

    fclose(file);
    httpd_resp_send_chunk(req, NULL, 0);
    ESP_LOGI(TAG, "File served successfully: %s", full_path);
    return ret;
}

// 登录页面处理函数
static esp_err_t login_get_handler(httpd_req_t *req)
{
    // 检查MAC白名单
    char client_mac[18] = {0};
    if (get_client_mac_address(req, client_mac, sizeof(client_mac)))
    {
        // 检查是否在白名单中
        if (whitelist_manager_check_mac(client_mac))
        {
            ESP_LOGI(TAG, "Client MAC %s is in whitelist, redirecting to dashboard", client_mac);
            httpd_resp_set_status(req, "302 Found");
            httpd_resp_set_hdr(req, "Location", "/dashboard");
            httpd_resp_send(req, NULL, 0);
            return ESP_OK;
        }
    }

    // 如果不在白名单中，显示登录页面
    return serve_file(req, "web_pages/html/login.html", "text/html");
}

// 仪表盘页面处理函数
static esp_err_t dashboard_get_handler(httpd_req_t *req)
{
    // 验证会话
    char username[32] = {0};
    if (!validate_session_cookie(req, username, sizeof(username)))
    {
        // 如果没有有效会话，重定向到登录页面
        httpd_resp_set_status(req, "302 Found");
        httpd_resp_set_hdr(req, "Location", "/login");
        httpd_resp_send(req, NULL, 0);
        return ESP_OK;
    }

    ESP_LOGI(TAG, "User %s accessing dashboard", username);
    return serve_file(req, "web_pages/html/dashboard.html", "text/html");
}

// 网络设置页面处理函数
static esp_err_t network_get_handler(httpd_req_t *req)
{
    char username[32] = {0};

    // 验证会话
    if (!validate_session_cookie(req, username, sizeof(username)))
    {
        ESP_LOGI(TAG, "Unauthorized access to network settings, redirecting to login");
        httpd_resp_set_status(req, "302 Found");
        httpd_resp_set_hdr(req, "Location", "/login");
        httpd_resp_send(req, NULL, 0);
        return ESP_OK;
    }

    ESP_LOGI(TAG, "User %s accessing network settings", username);
    return serve_file(req, "web_pages/html/network.html", "text/html");
}

// 控制页面处理函数
static esp_err_t controls_get_handler(httpd_req_t *req)
{
    char username[32] = {0};

    // 验证会话
    if (!validate_session_cookie(req, username, sizeof(username)))
    {
        ESP_LOGI(TAG, "Unauthorized access to controls, redirecting to login");
        httpd_resp_set_status(req, "302 Found");
        httpd_resp_set_hdr(req, "Location", "/login");
        httpd_resp_send(req, NULL, 0);
        return ESP_OK;
    }

    ESP_LOGI(TAG, "User %s accessing controls", username);
    return serve_file(req, "web_pages/html/controls.html", "text/html");
}

// 账户管理页面处理函数
static esp_err_t account_get_handler(httpd_req_t *req)
{
    char username[32] = {0};

    // 验证会话
    if (!validate_session_cookie(req, username, sizeof(username)))
    {
        ESP_LOGI(TAG, "Unauthorized access to account management, redirecting to login");
        httpd_resp_set_status(req, "302 Found");
        httpd_resp_set_hdr(req, "Location", "/login");
        httpd_resp_send(req, NULL, 0);
        return ESP_OK;
    }

    ESP_LOGI(TAG, "User %s accessing account management", username);
    return serve_file(req, "web_pages/html/account.html", "text/html");
}

// 备份恢复页面处理函数
static esp_err_t backup_restore_get_handler(httpd_req_t *req)
{
    char username[32] = {0};

    // 验证会话
    if (!validate_session_cookie(req, username, sizeof(username)))
    {
        ESP_LOGI(TAG, "Unauthorized access to backup/restore, redirecting to login");
        httpd_resp_set_status(req, "302 Found");
        httpd_resp_set_hdr(req, "Location", "/login");
        httpd_resp_send(req, NULL, 0);
        return ESP_OK;
    }

    ESP_LOGI(TAG, "User %s accessing backup/restore", username);
    return serve_file(req, "web_pages/html/backup-restore.html", "text/html");
}

// CSS样式文件处理函数
static esp_err_t css_get_handler(httpd_req_t *req)
{
    return serve_file(req, "web_pages/css/style.css", "text/css");
}

// JavaScript文件处理函数
static esp_err_t js_get_handler(httpd_req_t *req)
{
    return serve_file(req, "web_pages/js/common.js", "application/javascript");
}

// 删除第一个错误的get_client_mac_address函数定义
// 保留第二个正确的函数定义（在1139行附近）

// 登录POST处理函数
static esp_err_t login_post_handler(httpd_req_t *req)
{
    char content[256];
    int ret = httpd_req_recv(req, content, sizeof(content) - 1);
    if (ret <= 0)
    {
        return ESP_FAIL;
    }
    content[ret] = '\0';

    // 解析用户名和密码
    char username[32] = {0};
    char password[64] = {0};

    // 简单的表单解析
    char *username_start = strstr(content, "username=");
    char *password_start = strstr(content, "password=");

    if (username_start && password_start)
    {
        username_start += 9; // "username="的长度
        char *username_end = strchr(username_start, '&');
        if (username_end)
        {
            strncpy(username, username_start, username_end - username_start);
        }
        else
        {
            strncpy(username, username_start, sizeof(username) - 1);
        }

        password_start += 9; // "password="的长度
        char *password_end = strchr(password_start, '\0');
        if (password_end)
        {
            strncpy(password, password_start, password_end - password_start);
        }
    }
    // 验证用户凭据
    if (user_manager_authenticate(username, password))
    {
        char session_id[64] = {0};
        // 使用session_manager.h中的create_session函数
        if (create_session(username, session_id, sizeof(session_id)) == ESP_OK)
        {
            // 设置会话cookie
            char cookie[128];
            snprintf(cookie, sizeof(cookie), "session_id=%s; Path=/; HttpOnly", session_id);
            httpd_resp_set_hdr(req, "Set-Cookie", cookie);
            // 重定向到仪表盘
            httpd_resp_set_status(req, "302 Found");
            httpd_resp_set_hdr(req, "Location", "/dashboard");
            httpd_resp_send(req, NULL, 0);
            ESP_LOGI(TAG, "User %s logged in successfully", username);
            return ESP_OK;
        }
    }

    // 登录失败，重定向回登录页面
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "/login?error=1");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

// HTTP URI配置
static const httpd_uri_t root = {
    .uri = "/",
    .method = HTTP_GET,
    .handler = root_get_handler,
    .user_ctx = NULL};

static const httpd_uri_t login_get = {
    .uri = "/login",
    .method = HTTP_GET,
    .handler = login_get_handler,
    .user_ctx = NULL};

static const httpd_uri_t login_post = {
    .uri = "/login",
    .method = HTTP_POST,
    .handler = login_post_handler,
    .user_ctx = NULL};
static const httpd_uri_t dashboard = {
    .uri = "/dashboard",
    .method = HTTP_GET,
    .handler = dashboard_get_handler,
    .user_ctx = NULL};

static const httpd_uri_t network = {
    .uri = "/network",
    .method = HTTP_GET,
    .handler = network_get_handler,
    .user_ctx = NULL};

static const httpd_uri_t controls = {
    .uri = "/controls",
    .method = HTTP_GET,
    .handler = controls_get_handler,
    .user_ctx = NULL};

static const httpd_uri_t account = {
    .uri = "/account",
    .method = HTTP_GET,
    .handler = account_get_handler,
    .user_ctx = NULL};

static const httpd_uri_t backup_restore = {
    .uri = "/backup-restore",
    .method = HTTP_GET,
    .handler = backup_restore_get_handler,
    .user_ctx = NULL};

static const httpd_uri_t css = {
    .uri = "/style.css",
    .method = HTTP_GET,
    .handler = css_get_handler,
    .user_ctx = NULL};

static const httpd_uri_t js = {
    .uri = "/common.js",
    .method = HTTP_GET,
    .handler = js_get_handler,
    .user_ctx = NULL};

// 用户管理API处理函数
static esp_err_t users_api_get_handler(httpd_req_t *req)
{
    char username[32] = {0};
    // 使用统一的会话验证
    if (validate_api_session(req, username, sizeof(username)) != ESP_OK)
    {
        return ESP_OK;
    }

    cJSON *root = cJSON_CreateObject();
    cJSON *users_array = cJSON_CreateArray();

    int user_count = 0;
    user_t **users = user_manager_get_all_users(&user_count);

    for (int i = 0; i < user_count; i++)
    {
        cJSON *user_obj = cJSON_CreateObject();
        cJSON_AddStringToObject(user_obj, "username", users[i]->username);
        cJSON_AddNumberToObject(user_obj, "role", users[i]->role);
        cJSON_AddItemToArray(users_array, user_obj);
    }

    cJSON_AddItemToObject(root, "users", users_array);

    // 使用统一的JSON响应发送
    esp_err_t result = send_json_response(req, root);
    cJSON_Delete(root);
    return result;
}

static esp_err_t users_api_post_handler(httpd_req_t *req)
{
    char username[32] = {0};

    // 使用统一的会话验证
    if (validate_api_session(req, username, sizeof(username)) != ESP_OK)
    {
        return ESP_OK;
    }

    char content[512];
    int ret, remaining = req->content_len;

    if (remaining >= sizeof(content))
    {
        remaining = sizeof(content) - 1;
    }

    ret = httpd_req_recv(req, content, remaining);
    if (ret <= 0)
    {
        return send_error_response(req, "Failed to receive request content");
    }

    content[ret] = '\0';

    cJSON *root = cJSON_Parse(content);
    if (root == NULL)
    {
        return send_error_response(req, "Invalid JSON");
    }

    cJSON *action = cJSON_GetObjectItem(root, "action");
    cJSON *username_json = cJSON_GetObjectItem(root, "username");
    cJSON *password = cJSON_GetObjectItem(root, "password");
    cJSON *role = cJSON_GetObjectItem(root, "role");

    if (!action || !username_json)
    {
        cJSON_Delete(root);
        return send_error_response(req, "Missing required fields");
    }

    esp_err_t result = ESP_FAIL;

    if (strcmp(action->valuestring, "add") == 0)
    {
        if (!password || !role)
        {
            cJSON_Delete(root);
            return send_error_response(req, "Missing password or role for add action");
        }
        result = user_manager_add_user(username_json->valuestring, password->valuestring, role->valueint);
    }
    else if (strcmp(action->valuestring, "delete") == 0)
    {
        result = user_manager_delete_user(username_json->valuestring);
    }
    else if (strcmp(action->valuestring, "update") == 0)
    {
        result = user_manager_update_user(username_json->valuestring,
                                          password ? password->valuestring : NULL,
                                          role ? role->valueint : 0);
    }

    cJSON_Delete(root);

    if (result == ESP_OK)
    {
        user_manager_save_users();
        return send_success_response(req, "Operation completed successfully");
    }
    else
    {
        return send_error_response(req, "Operation failed");
    }
}

// 白名单管理API处理函数
static esp_err_t whitelist_api_get_handler(httpd_req_t *req)
{
    char username[32] = {0};

    // 使用统一的会话验证
    if (validate_api_session(req, username, sizeof(username)) != ESP_OK)
    {
        return ESP_OK;
    }

    cJSON *root = cJSON_CreateObject();
    cJSON *macs_array = cJSON_CreateArray();

    int mac_count = 0;
    whitelist_mac_t **macs = whitelist_manager_get_all_macs(&mac_count);

    for (int i = 0; i < mac_count; i++)
    {
        cJSON *mac_obj = cJSON_CreateObject();
        cJSON_AddStringToObject(mac_obj, "mac", macs[i]->mac);
        cJSON_AddStringToObject(mac_obj, "description", macs[i]->description);
        cJSON_AddItemToArray(macs_array, mac_obj);
    }

    cJSON_AddItemToObject(root, "macs", macs_array);

    // 使用统一的JSON响应发送
    esp_err_t result = send_json_response(req, root);
    cJSON_Delete(root);
    return result;
}

static esp_err_t whitelist_api_post_handler(httpd_req_t *req)
{
    char username[32] = {0};

    // 使用统一的会话验证
    if (validate_api_session(req, username, sizeof(username)) != ESP_OK)
    {
        return ESP_OK;
    }
    char content[512];
    int ret = httpd_req_recv(req, content, sizeof(content) - 1);
    if (ret <= 0)
    {
        return send_error_response(req, "Failed to receive data");
    }
    content[ret] = '\0';

    cJSON *json = cJSON_Parse(content);
    if (!json)
    {
        return send_error_response(req, "Invalid JSON");
    }

    cJSON *action = cJSON_GetObjectItem(json, "action");
    cJSON *mac = cJSON_GetObjectItem(json, "mac");
    cJSON *description = cJSON_GetObjectItem(json, "description");

    if (!action || !mac)
    {
        cJSON_Delete(json);
        return send_error_response(req, "Missing required fields");
    }

    esp_err_t result = ESP_OK;

    if (strcmp(action->valuestring, "add") == 0)
    {
        result = whitelist_manager_add_mac(mac->valuestring,
                                           description ? description->valuestring : "");
    }
    else if (strcmp(action->valuestring, "delete") == 0)
    {
        // 使用正确的函数名
        result = whitelist_manager_remove_mac(mac->valuestring);
    }
    else
    {
        cJSON_Delete(json);
        return send_error_response(req, "Invalid action");
    }

    cJSON_Delete(json);

    if (result == ESP_OK)
    {
        // 使用正确的保存函数名
        whitelist_manager_save_macs();
        return send_success_response(req, "Operation completed successfully");
    }
    else
    {
        return send_error_response(req, "Operation failed");
    }
}

/**
 * @brief 扫描并获取连接到AP的设备列表
 * @return esp_err_t 错误码
 */

// 设备管理API处理函数
static esp_err_t devices_api_get_handler(httpd_req_t *req)
{
    char username[32] = {0};

    // 使用统一的会话验证
    if (validate_api_session(req, username, sizeof(username)) != ESP_OK)
    {
        return ESP_OK;
    }

    cJSON *root = cJSON_CreateObject();
    cJSON *devices_array = cJSON_CreateArray();

    // 从设备映射表获取设备信息（排除unknown设备）
    int device_count = 0;
    device_mapping_t **devices = device_mapping_get_all_devices_ex(&device_count, true);

    // 获取总设备数量（包含unknown设备）
    int total_device_count = device_mapping_get_count();
    int unknown_count = total_device_count - device_count; // 计算unknown设备数量

    for (int i = 0; i < device_count; i++)
    {
        cJSON *device_obj = cJSON_CreateObject();
        cJSON_AddStringToObject(device_obj, "hostname", devices[i]->hostname);
        cJSON_AddStringToObject(device_obj, "ip", devices[i]->ip);
        cJSON_AddStringToObject(device_obj, "mac", devices[i]->mac);
        cJSON_AddNumberToObject(device_obj, "last_seen", devices[i]->last_seen);
        cJSON_AddBoolToObject(device_obj, "is_active", devices[i]->is_active);
        cJSON_AddBoolToObject(device_obj, "is_unknown", false);

        cJSON_AddItemToArray(devices_array, device_obj);
    }

    // 添加unknown设备信息
    cJSON *unknown_array = cJSON_CreateArray();
    int unknown_device_count = 0;
    device_mapping_t **all_devices = device_mapping_get_all_devices(&unknown_device_count);

    for (int i = 0; i < unknown_device_count; i++)
    {
        if (strcmp(all_devices[i]->hostname, "unknown") == 0)
        {
            cJSON *device_obj = cJSON_CreateObject();
            cJSON_AddStringToObject(device_obj, "hostname", "unknown");
            cJSON_AddStringToObject(device_obj, "ip", all_devices[i]->ip);
            cJSON_AddStringToObject(device_obj, "mac", all_devices[i]->mac);
            cJSON_AddNumberToObject(device_obj, "last_seen", all_devices[i]->last_seen);
            cJSON_AddBoolToObject(device_obj, "is_active", all_devices[i]->is_active);
            cJSON_AddBoolToObject(device_obj, "is_unknown", true);

            cJSON_AddItemToArray(unknown_array, device_obj);
        }
    }

    cJSON_AddItemToObject(root, "devices", devices_array);
    cJSON_AddItemToObject(root, "unknown_devices", unknown_array);
    cJSON_AddNumberToObject(root, "total_count", device_count);
    cJSON_AddNumberToObject(root, "unknown_count", unknown_count);

    // 使用统一的JSON响应发送
    esp_err_t result = send_json_response(req, root);
    cJSON_Delete(root);
    return result;
}

// 登出URI配置
static const httpd_uri_t logout_get = {
    .uri = "/logout",
    .method = HTTP_GET,
    .handler = logout_get_handler,
    .user_ctx = NULL};

// 431错误处理URI配置
static const httpd_uri_t header_too_large = {
    .uri = "/header_too_large",
    .method = HTTP_GET,
    .handler = header_too_large_handler,
    .user_ctx = NULL};

// 登出处理函数
static esp_err_t logout_get_handler(httpd_req_t *req)
{
    // 获取Cookie中的会话ID
    size_t cookie_len = httpd_req_get_hdr_value_len(req, "Cookie") + 1;
    if (cookie_len > 1)
    {
        char *cookie_header = malloc(cookie_len);
        if (cookie_header)
        {
            if (httpd_req_get_hdr_value_str(req, "Cookie", cookie_header, cookie_len) == ESP_OK)
            {
                // 查找session_id
                char *session_start = strstr(cookie_header, "session_id=");
                if (session_start)
                {
                    session_start += 11; // 跳过"session_id="
                    char *session_end = strchr(session_start, ';');
                    if (!session_end)
                    {
                        session_end = session_start + strlen(session_start);
                    }

                    size_t session_len = session_end - session_start;
                    if (session_len > 0)
                    {
                        char session_id[SESSION_ID_LENGTH + 1];
                        strncpy(session_id, session_start, session_len);
                        session_id[session_len] = '\0';

                        // 删除会话
                        remove_session(session_id);
                        ESP_LOGI(TAG, "Session deleted: %s", session_id);
                    }
                }
            }
            free(cookie_header);
        }
    }

    // 清除Cookie并重定向到登录页面
    httpd_resp_set_hdr(req, "Set-Cookie", "session_id=; Path=/; HttpOnly; Max-Age=0");
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "/login");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

// 用户管理API URI配置
static const httpd_uri_t users_api_get = {
    .uri = "/api/users",
    .method = HTTP_GET,
    .handler = users_api_get_handler,
    .user_ctx = NULL};

static const httpd_uri_t users_api_post = {
    .uri = "/api/users",
    .method = HTTP_POST,
    .handler = users_api_post_handler,
    .user_ctx = NULL};

// 白名单管理API URI配置
static const httpd_uri_t whitelist_api_get = {
    .uri = "/api/whitelist",
    .method = HTTP_GET,
    .handler = whitelist_api_get_handler,
    .user_ctx = NULL};

static const httpd_uri_t whitelist_api_post = {
    .uri = "/api/whitelist",
    .method = HTTP_POST,
    .handler = whitelist_api_post_handler,
    .user_ctx = NULL};

// 统一的会话验证辅助函数
static esp_err_t validate_api_session(httpd_req_t *req, char *username, size_t username_size)
{
    if (!validate_session_cookie(req, username, username_size))
    {
        ESP_LOGI(TAG, "Unauthorized API access, redirecting to login");
        httpd_resp_set_status(req, "302 Found");
        httpd_resp_set_hdr(req, "Location", "/login");
        httpd_resp_send(req, NULL, 0);
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "User %s accessing API", username);
    return ESP_OK;
}

// 统一的JSON响应发送辅助函数
static esp_err_t send_json_response(httpd_req_t *req, cJSON *json_obj)
{
    char *json_str = cJSON_PrintUnformatted(json_obj);
    if (!json_str)
    {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "JSON serialization failed");
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "application/json");
    esp_err_t result = httpd_resp_send(req, json_str, strlen(json_str));
    free(json_str);
    return result;
}

// 统一的错误JSON响应辅助函数
static esp_err_t send_error_response(httpd_req_t *req, const char *error_message)
{
    cJSON *error_obj = cJSON_CreateObject();
    cJSON_AddStringToObject(error_obj, "error", error_message);
    esp_err_t result = send_json_response(req, error_obj);
    cJSON_Delete(error_obj);
    return result;
}

// 统一的成功JSON响应辅助函数
static esp_err_t send_success_response(httpd_req_t *req, const char *message)
{
    cJSON *success_obj = cJSON_CreateObject();
    cJSON_AddStringToObject(success_obj, "status", "success");
    if (message)
    {
        cJSON_AddStringToObject(success_obj, "message", message);
    }
    esp_err_t result = send_json_response(req, success_obj);
    cJSON_Delete(success_obj);
    return result;
}

// 设备管理API URI配置
static const httpd_uri_t devices_api_get = {
    .uri = "/api/devices",
    .method = HTTP_GET,
    .handler = devices_api_get_handler,
    .user_ctx = NULL};

esp_err_t wifi_manager_init(wifi_manager_config_t *config)
{
    esp_err_t ret = ESP_OK;

    // 初始化NVS
    ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // 初始化网络接口
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // 只创建AP网络接口，删除STA网络接口
    g_wifi_manager.ap_netif = esp_netif_create_default_wifi_ap();
    // 删除这一行: g_wifi_manager.sta_netif = esp_netif_create_default_wifi_sta();

    // 配置AP网络接口
    esp_netif_ip_info_t ap_ip_info;
    IP4_ADDR(&ap_ip_info.ip, 192, 168, 0, 1);
    IP4_ADDR(&ap_ip_info.gw, 192, 168, 0, 1);
    IP4_ADDR(&ap_ip_info.netmask, 255, 255, 255, 0);
    esp_netif_dhcps_stop(g_wifi_manager.ap_netif);
    esp_netif_set_ip_info(g_wifi_manager.ap_netif, &ap_ip_info);
    esp_netif_dhcps_start(g_wifi_manager.ap_netif);

    // 设置AP IP信息
    snprintf(g_wifi_manager.network_info.ap_ip, sizeof(g_wifi_manager.network_info.ap_ip), "192.168.0.1");

    // 设置自定义MAC地址来解决eFuse MAC_CUSTOM为空的问题
    uint8_t custom_mac[] = {0xAA, 0xBB, 0xCC, 0x11, 0x22, 0xf5};

    // 在WiFi初始化前设置MAC地址
    esp_err_t set_mac_result = esp_base_mac_addr_set(custom_mac);
    if (set_mac_result != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to set base MAC address: %s", esp_err_to_name(set_mac_result));
        // 继续初始化，使用默认MAC地址处理
        ESP_LOGW(TAG, "Continuing with default MAC address handling");
    }
    else
    {
        ESP_LOGI(TAG, "Custom MAC address set successfully");
    }

    // 初始化WiFi
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    // 注册事件处理程序
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        NULL));

    // 设置WiFi模式为AP+STA
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));

    // 配置AP参数 - 修复初始化语法
    wifi_config_t ap_config;
    memset(&ap_config, 0, sizeof(wifi_config_t));
    ap_config.ap.ssid_len = strlen(config->ap_ssid);
    ap_config.ap.channel = 1;
    ap_config.ap.authmode = WIFI_AUTH_WPA2_PSK;
    ap_config.ap.max_connection = 4;
    ap_config.ap.pmf_cfg.required = false;

    strncpy((char *)ap_config.ap.ssid, config->ap_ssid, sizeof(ap_config.ap.ssid));
    strncpy((char *)ap_config.ap.password, config->ap_password, sizeof(ap_config.ap.password));

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));
    // 保存配置
    memcpy(&g_wifi_manager.wifi_config, config, sizeof(wifi_manager_config_t));

    // 启动WiFi
    ESP_ERROR_CHECK(esp_wifi_start());

    // 初始化白名单管理器
    esp_err_t whitelist_ret = whitelist_manager_init();
    if (whitelist_ret != ESP_OK)
    {
        ESP_LOGW(TAG, "Whitelist manager initialization failed: %s", esp_err_to_name(whitelist_ret));
    }
    else
    {
        ESP_LOGI(TAG, "Whitelist manager initialized successfully");
    }

    ESP_LOGI(TAG, "WiFi manager initialized successfully");
    return ESP_OK;
}
// 删除第755行附近的无参数wifi_manager_scan_connected_devices函数
// 保留第68行的有参数版本

esp_err_t wifi_manager_connect_sta(const char *ssid, const char *password)
{
    wifi_config_t sta_config;
    memset(&sta_config, 0, sizeof(wifi_config_t));

    sta_config.sta.scan_method = WIFI_FAST_SCAN;
    sta_config.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
    sta_config.sta.threshold.rssi = -127;
    sta_config.sta.threshold.authmode = WIFI_AUTH_OPEN;

    strncpy((char *)sta_config.sta.ssid, ssid, sizeof(sta_config.sta.ssid));
    strncpy((char *)sta_config.sta.password, password, sizeof(sta_config.sta.password));

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta_config));
    return esp_wifi_connect();
}

esp_err_t wifi_manager_save_config(wifi_manager_config_t *config)
{
    nvs_handle_t nvs_handle;
    esp_err_t ret = nvs_open("wifi_config", NVS_READWRITE, &nvs_handle);
    if (ret != ESP_OK)
    {
        return ret;
    }

    ret = nvs_set_str(nvs_handle, "ap_ssid", config->ap_ssid);
    ret |= nvs_set_str(nvs_handle, "ap_password", config->ap_password);
    ret |= nvs_set_str(nvs_handle, "sta_ssid", config->sta_ssid);
    ret |= nvs_set_str(nvs_handle, "sta_password", config->sta_password);
    ret |= nvs_set_u8(nvs_handle, "enable_nat", config->enable_nat);
    ret |= nvs_set_u8(nvs_handle, "enable_dhcp_server", config->enable_dhcp_server);

    nvs_commit(nvs_handle);
    nvs_close(nvs_handle);

    return ret;
}

esp_err_t wifi_manager_load_config(wifi_manager_config_t *config)
{
    nvs_handle_t nvs_handle;
    esp_err_t ret = nvs_open("wifi_config", NVS_READONLY, &nvs_handle);
    if (ret != ESP_OK)
    {
        return ret;
    }

    size_t required_size = 0;

    // 读取AP SSID
    ret = nvs_get_str(nvs_handle, "ap_ssid", NULL, &required_size);
    if (ret == ESP_OK && required_size > 0)
    {
        nvs_get_str(nvs_handle, "ap_ssid", config->ap_ssid, &required_size);
    }
    // 读取AP密码
    ret = nvs_get_str(nvs_handle, "ap_password", NULL, &required_size);
    if (ret == ESP_OK && required_size > 0)
    {
        nvs_get_str(nvs_handle, "ap_password", config->ap_password, &required_size);
    }

    // 读取STA相关配置
    ret = nvs_get_str(nvs_handle, "sta_ssid", NULL, &required_size);
    if (ret == ESP_OK && required_size > 0)
    {
        nvs_get_str(nvs_handle, "sta_ssid", config->sta_ssid, &required_size);
    }

    ret = nvs_get_str(nvs_handle, "sta_password", NULL, &required_size);
    if (ret == ESP_OK && required_size > 0)
    {
        nvs_get_str(nvs_handle, "sta_password", config->sta_password, &required_size);
    }

    // 读取NAT设置
    uint8_t enable_nat = 1;
    nvs_get_u8(nvs_handle, "enable_nat", &enable_nat);
    config->enable_nat = (enable_nat != 0);

    // 读取DHCP服务器设置
    uint8_t enable_dhcp = 1;
    nvs_get_u8(nvs_handle, "enable_dhcp_server", &enable_dhcp);
    config->enable_dhcp_server = (enable_dhcp != 0);

    nvs_close(nvs_handle);
    return ESP_OK;
}

// 删除wifi_manager_enable_nat函数
// esp_err_t wifi_manager_enable_nat(void) { ... }

esp_err_t wifi_manager_get_network_info(network_info_t *info)
{
    if (info == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    memcpy(info, &g_wifi_manager.network_info, sizeof(network_info_t));
    return ESP_OK;
}

// 在wifi_manager_start_web_server函数中注册错误处理器
esp_err_t wifi_manager_start_web_server(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;

    // 增加缓冲区大小以解决HTTP 431错误（请求头字段过大）
    config.recv_wait_timeout = 10; // 接收超时时间（秒）
    config.send_wait_timeout = 10; // 发送超时时间（秒）
    config.max_uri_handlers = 20;  // 最大URI处理器数量
    config.max_resp_headers = 20;  // 最大响应头数量
    config.stack_size = 8192;      // 堆栈大小

    // 设置更大的缓冲区来处理大请求头
    config.max_open_sockets = 7;    // 最大打开套接字数
    config.lru_purge_enable = true; // 启用LRU清理

    esp_err_t ret = httpd_start(&g_wifi_manager.server, &config);
    if (ret == ESP_OK)
    {
        // 注册所有URI处理器
        httpd_register_uri_handler(g_wifi_manager.server, &root);
        httpd_register_uri_handler(g_wifi_manager.server, &login_get);
        httpd_register_uri_handler(g_wifi_manager.server, &login_post);
        httpd_register_uri_handler(g_wifi_manager.server, &dashboard);
        httpd_register_uri_handler(g_wifi_manager.server, &network);
        httpd_register_uri_handler(g_wifi_manager.server, &controls);
        httpd_register_uri_handler(g_wifi_manager.server, &account);
        httpd_register_uri_handler(g_wifi_manager.server, &backup_restore);
        httpd_register_uri_handler(g_wifi_manager.server, &css);
        httpd_register_uri_handler(g_wifi_manager.server, &js);

        // 注册新的API URI处理器
        httpd_register_uri_handler(g_wifi_manager.server, &users_api_get);
        httpd_register_uri_handler(g_wifi_manager.server, &users_api_post);
        httpd_register_uri_handler(g_wifi_manager.server, &whitelist_api_get);
        httpd_register_uri_handler(g_wifi_manager.server, &whitelist_api_post);
        httpd_register_uri_handler(g_wifi_manager.server, &devices_api_get);

        // 注册登出URI处理器
        httpd_register_uri_handler(g_wifi_manager.server, &logout_get);

        // 注册431错误处理URI处理器
        httpd_register_uri_handler(g_wifi_manager.server, &header_too_large);

        ESP_LOGI(TAG, "Web server started on port 80 with full backend support");
    }

    return ret;
}

static bool get_client_mac_address(httpd_req_t *req, char *mac_str, size_t mac_str_len)
{
    // 使用设备映射表查找MAC地址，不依赖网络API

    // 获取设备映射表中的所有设备
    int device_count = 0;
    device_mapping_t **devices = device_mapping_get_all_devices(&device_count);

    if (device_count > 0)
    {
        // 查找活跃设备
        for (int i = 0; i < device_count; i++)
        {
            if (devices[i] && devices[i]->is_active)
            {
                strncpy(mac_str, devices[i]->mac, mac_str_len - 1);
                mac_str[mac_str_len - 1] = '\0';
                ESP_LOGI(TAG, "Using active device MAC address: %s", mac_str);
                return true;
            }
        }

        // 使用最近连接的设备
        uint32_t latest_time = 0;
        int latest_index = -1;

        for (int i = 0; i < device_count; i++)
        {
            if (devices[i] && devices[i]->last_seen > latest_time)
            {
                latest_time = devices[i]->last_seen;
                latest_index = i;
            }
        }

        if (latest_index >= 0)
        {
            strncpy(mac_str, devices[latest_index]->mac, mac_str_len - 1);
            mac_str[mac_str_len - 1] = '\0';
            ESP_LOGI(TAG, "Using most recent device MAC address: %s", mac_str);
            return true;
        }

        // 使用第一个设备作为备用
        strncpy(mac_str, devices[0]->mac, mac_str_len - 1);
        mac_str[mac_str_len - 1] = '\0';
        ESP_LOGI(TAG, "Using first device MAC address: %s", mac_str);
        return true;
    }

    // 如果设备映射表为空，使用伪MAC地址
    static int counter = 0;
    counter++;
    snprintf(mac_str, mac_str_len, "00:11:22:33:44:%02X", counter % 256);
    ESP_LOGI(TAG, "Using fallback pseudo MAC address: %s", mac_str);
    return true;
}
/**
 * @brief 扫描并获取连接到指定AP的设备列表
 * @param ap_name AP名称
 * @return esp_err_t 错误码
 */

/**
 * @brief 启动AP模式
 * @param ap_name AP名称（如"ap1", "ap2", "ap3"）
 * @param ssid AP的SSID
 * @param password AP的密码
 * @return esp_err_t 错误码
 */
esp_err_t wifi_manager_start_ap(const char *ap_name, const char *ssid, const char *password)
{
    ESP_LOGI(TAG, "Starting AP %s with SSID: %s", ap_name, ssid);

    // 检查是否已达到最大AP数量
    if (g_wifi_manager.ap_count >= MAX_APS)
    {
        ESP_LOGE(TAG, "Maximum AP count reached (%d)", MAX_APS);
        return ESP_ERR_NO_MEM;
    }

    // 添加新的AP信息
    ap_info_t *new_ap = &g_wifi_manager.aps[g_wifi_manager.ap_count];
    strncpy(new_ap->ap_name, ap_name, sizeof(new_ap->ap_name) - 1);
    strncpy(new_ap->ssid, ssid, sizeof(new_ap->ssid) - 1);
    strncpy(new_ap->password, password, sizeof(new_ap->password) - 1);

    // 修复格式截断问题：使用更安全的IP地址生成方式
    // 限制ap_count+1的范围在1-254之间，确保IP地址不会超过15个字符
    int subnet = (g_wifi_manager.ap_count + 1) % 254;
    if (subnet == 0)
        subnet = 1; // 避免使用0和255
    snprintf(new_ap->ip, sizeof(new_ap->ip), "192.168.%d.1", subnet);

    new_ap->connected_devices = 0;

    g_wifi_manager.ap_count++;

    ESP_LOGI(TAG, "AP %s started successfully", ap_name);
    return ESP_OK;
}

static esp_err_t header_too_large_handler(httpd_req_t *req)
{
    const char *error_html =
        "<html>"
        "<head><title>请求头过大</title></head>"
        "<body style='font-family: Arial, sans-serif; text-align: center; margin-top: 50px;'>"
        "<h1>HTTP 431 - 请求头字段过大</h1>"
        "<p>您的浏览器发送的请求头过大，无法处理。</p>"
        "<p>请尝试以下解决方案：</p>"
        "<ul style='text-align: left; max-width: 500px; margin: 0 auto;'>"
        "<li>清除浏览器缓存和Cookie</li>"
        "<li>使用隐私/无痕模式访问</li>"
        "<li>尝试使用其他浏览器</li>"
        "<li>重启浏览器</li>"
        "</ul>"
        "<p><a href='/login'>返回登录页面</a></p>"
        "</body>"
        "</html>";

    httpd_resp_set_type(req, "text/html");
    httpd_resp_set_status(req, "431 Request Header Fields Too Large");
    httpd_resp_send(req, error_html, strlen(error_html));
    return ESP_OK;
}