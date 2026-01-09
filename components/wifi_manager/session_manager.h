#ifndef SESSION_MANAGER_H
#define SESSION_MANAGER_H

#include <stdint.h>

#include "esp_err.h"
#include "esp_http_server.h"

#define SESSION_ID_LENGTH 64
#define MAX_SESSIONS 20
#define SESSION_TIMEOUT_MS (30 * 60 * 1000)  // 30分钟超时

// 会话结构体
typedef struct {
    char session_id[SESSION_ID_LENGTH];
    char username[32];
    uint64_t created_time;
    uint64_t last_accessed;
} session_t;

// 会话管理器结构体
typedef struct {
    session_t sessions[MAX_SESSIONS];
    int session_count;
} session_manager_t;

// 会话管理函数
esp_err_t session_manager_init(void);
bool validate_session_cookie(httpd_req_t* req, char* username, size_t username_size);
esp_err_t create_session(const char* username, char* session_id, size_t session_id_size);
esp_err_t remove_session(const char* session_id);
bool verify_session(const char* session_id, char* username, size_t username_len);

#endif  // SESSION_MANAGER_H