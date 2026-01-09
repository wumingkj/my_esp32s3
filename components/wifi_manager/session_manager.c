#include "session_manager.h"

#include <string.h>
#include <time.h>

#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"

static const char* TAG = "SessionManager";

static session_manager_t g_session_manager = {0};

// 函数声明
static bool session_exists(const char* session_id);
static char* generate_session_id(void);
static bool add_session(const char* session_id, const char* username);
static char* get_username_by_session(const char* session_id);

// Base64编码表
static const char base64_table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

// Base64编码函数
static void base64_encode(const uint8_t* data, size_t input_length, char* output, size_t output_length) {
    size_t i = 0;
    size_t j = 0;

    if (output_length < ((input_length + 2) / 3) * 4 + 1) {
        ESP_LOGE(TAG, "Output buffer too small for base64 encoding");
        return;
    }

    for (i = 0; i < input_length;) {
        uint32_t octet_a = i < input_length ? data[i++] : 0;
        uint32_t octet_b = i < input_length ? data[i++] : 0;
        uint32_t octet_c = i < input_length ? data[i++] : 0;

        uint32_t triple = (octet_a << 16) + (octet_b << 8) + octet_c;

        output[j++] = base64_table[(triple >> 18) & 0x3F];
        output[j++] = base64_table[(triple >> 12) & 0x3F];
        output[j++] = base64_table[(triple >> 6) & 0x3F];
        output[j++] = base64_table[triple & 0x3F];
    }

    // 添加填充字符
    switch (input_length % 3) {
        case 1:
            output[j - 2] = '=';
            output[j - 1] = '=';
            break;
        case 2:
            output[j - 1] = '=';
            break;
    }

    output[j] = '\0';
}

esp_err_t session_manager_init(void) {
    memset(&g_session_manager, 0, sizeof(g_session_manager));
    ESP_LOGI(TAG, "Session manager initialized");
    return ESP_OK;
}

char* generate_session_id(void) {
    static char session_id[SESSION_ID_LENGTH];
    uint8_t random_bytes[16];

    // 生成随机字节
    for (int i = 0; i < sizeof(random_bytes); i++) {
        random_bytes[i] = (uint8_t)esp_random();
    }

    // 使用Base64编码生成会话ID
    base64_encode(random_bytes, sizeof(random_bytes), session_id, sizeof(session_id));

    // 确保会话ID唯一
    while (session_exists(session_id)) {
        // 重新生成随机字节
        for (int i = 0; i < sizeof(random_bytes); i++) {
            random_bytes[i] = (uint8_t)esp_random();
        }
        base64_encode(random_bytes, sizeof(random_bytes), session_id, sizeof(session_id));
    }

    ESP_LOGI(TAG, "Generated session ID: %s", session_id);
    return session_id;
}

bool session_exists(const char* session_id) {
    for (int i = 0; i < g_session_manager.session_count; i++) {
        if (strcmp(g_session_manager.sessions[i].session_id, session_id) == 0) {
            return true;
        }
    }
    return false;
}

bool add_session(const char* session_id, const char* username) {
    if (g_session_manager.session_count >= MAX_SESSIONS) {
        ESP_LOGE(TAG, "Session limit reached, cannot add new session");
        return false;
    }

    if (session_exists(session_id)) {
        ESP_LOGE(TAG, "Session ID already exists");
        return false;
    }

    session_t* session = &g_session_manager.sessions[g_session_manager.session_count];
    strncpy(session->session_id, session_id, SESSION_ID_LENGTH - 1);
    session->session_id[SESSION_ID_LENGTH - 1] = '\0';

    strncpy(session->username, username, sizeof(session->username) - 1);
    session->username[sizeof(session->username) - 1] = '\0';

    uint64_t current_time = esp_timer_get_time() / 1000;  // 转换为毫秒
    session->created_time = current_time;
    session->last_accessed = current_time;

    g_session_manager.session_count++;

    ESP_LOGI(TAG, "Session added for user: %s, session ID: %s", username, session_id);
    return true;
}

esp_err_t remove_session(const char* session_id) {
    for (int i = 0; i < g_session_manager.session_count; i++) {
        if (strcmp(g_session_manager.sessions[i].session_id, session_id) == 0) {
            // 将最后一个会话移动到当前位置
            if (i < g_session_manager.session_count - 1) {
                memcpy(&g_session_manager.sessions[i], &g_session_manager.sessions[g_session_manager.session_count - 1],
                       sizeof(session_t));
            }

            g_session_manager.session_count--;
            ESP_LOGI(TAG, "Session removed: %s", session_id);
            return ESP_OK;
        }
    }

    ESP_LOGW(TAG, "Session not found for removal: %s", session_id);
    return ESP_FAIL;
}

char* get_username_by_session(const char* session_id) {
    for (int i = 0; i < g_session_manager.session_count; i++) {
        if (strcmp(g_session_manager.sessions[i].session_id, session_id) == 0) {
            // 更新最后访问时间
            g_session_manager.sessions[i].last_accessed = esp_timer_get_time() / 1000;
            return g_session_manager.sessions[i].username;
        }
    }

    return NULL;
}

bool validate_session(const char* session_id) {
    for (int i = 0; i < g_session_manager.session_count; i++) {
        if (strcmp(g_session_manager.sessions[i].session_id, session_id) == 0) {
            uint64_t current_time = esp_timer_get_time() / 1000;
            uint64_t last_accessed = g_session_manager.sessions[i].last_accessed;

            // 检查会话是否超时
            if (current_time - last_accessed > SESSION_TIMEOUT_MS) {
                ESP_LOGI(TAG, "Session expired: %s", session_id);
                remove_session(session_id);
                return false;
            }

            // 更新最后访问时间
            g_session_manager.sessions[i].last_accessed = current_time;
            return true;
        }
    }

    return false;
}

bool verify_session(const char* session_id, char* username, size_t username_len) {
    for (int i = 0; i < g_session_manager.session_count; i++) {
        if (strcmp(g_session_manager.sessions[i].session_id, session_id) == 0) {
            uint64_t current_time = esp_timer_get_time() / 1000;
            uint64_t last_accessed = g_session_manager.sessions[i].last_accessed;

            // 检查会话是否超时
            if (current_time - last_accessed > SESSION_TIMEOUT_MS) {
                ESP_LOGI(TAG, "Session expired: %s", session_id);
                remove_session(session_id);
                return false;
            }

            // 更新最后访问时间
            g_session_manager.sessions[i].last_accessed = current_time;

            // 复制用户名到输出缓冲区
            if (username && username_len > 0) {
                strncpy(username, g_session_manager.sessions[i].username, username_len - 1);
                username[username_len - 1] = '\0';
            }

            return true;
        }
    }

    return false;
}

// 创建新会话
esp_err_t create_session(const char* username, char* session_id, size_t session_id_size) {
    if (!username || !session_id || session_id_size < SESSION_ID_LENGTH) {
        return ESP_ERR_INVALID_ARG;
    }

    char* new_session_id = generate_session_id();
    if (!new_session_id) {
        return ESP_FAIL;
    }

    strncpy(session_id, new_session_id, session_id_size - 1);
    session_id[session_id_size - 1] = '\0';

    if (!add_session(session_id, username)) {
        return ESP_FAIL;
    }

    return ESP_OK;
}

// 验证会话cookie
bool validate_session_cookie(httpd_req_t* req, char* username, size_t username_size) {
    // 获取Cookie头
    size_t cookie_header_len = httpd_req_get_hdr_value_len(req, "Cookie");
    if (cookie_header_len == 0) {
        return false;
    }

    char* cookie_header = malloc(cookie_header_len + 1);
    if (!cookie_header) {
        return false;
    }

    if (httpd_req_get_hdr_value_str(req, "Cookie", cookie_header, cookie_header_len + 1) != ESP_OK) {
        free(cookie_header);
        return false;
    }

    // 查找session_id
    char* session_start = strstr(cookie_header, "session_id=");
    if (!session_start) {
        free(cookie_header);
        return false;
    }

    session_start += 11;  // 跳过"session_id="
    char* session_end = strchr(session_start, ';');
    if (!session_end) {
        session_end = session_start + strlen(session_start);
    }

    size_t session_len = session_end - session_start;
    if (session_len == 0) {
        free(cookie_header);
        return false;
    }

    char session_id[SESSION_ID_LENGTH + 1];
    strncpy(session_id, session_start, session_len);
    session_id[session_len] = '\0';

    free(cookie_header);

    // 验证会话
    return verify_session(session_id, username, username_size);
}

void cleanup_expired_sessions(void) {
    uint64_t current_time = esp_timer_get_time() / 1000;
    int removed_count = 0;

    for (int i = g_session_manager.session_count - 1; i >= 0; i--) {
        if (current_time - g_session_manager.sessions[i].last_accessed > SESSION_TIMEOUT_MS) {
            ESP_LOGI(TAG, "Cleaning up expired session: %s", g_session_manager.sessions[i].session_id);
            remove_session(g_session_manager.sessions[i].session_id);
            removed_count++;
        }
    }

    if (removed_count > 0) {
        ESP_LOGI(TAG, "Cleaned up %d expired sessions", removed_count);
    }
}

int get_session_count(void) {
    return g_session_manager.session_count;
}