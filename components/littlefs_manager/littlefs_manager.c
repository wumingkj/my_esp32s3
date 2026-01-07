#include "littlefs_manager.h"
#include "esp_littlefs.h"
#include <errno.h>

static const char *TAG = "littlefs_manager";

// 默认文件系统配置
static filesystem_info_t fs_info = {
    .mount_point = "/littlefs",
    .partition_label = "littlefs",
    .format_if_mount_failed = true
};

static bool fs_mounted = false;

// 初始化文件系统（参考官方demo实现）
esp_err_t littlefs_manager_init(void) {
    esp_err_t ret = ESP_FAIL;
    
    ESP_LOGI(TAG, "Initializing LittleFS filesystem...");

    // 验证配置参数
    if (fs_info.mount_point == NULL || fs_info.partition_label == NULL) {
        ESP_LOGE(TAG, "Invalid filesystem configuration");
        return ESP_ERR_INVALID_ARG;
    }

    // LittleFS 配置（参考官方demo）
    esp_vfs_littlefs_conf_t conf = {
        .base_path = fs_info.mount_point,
        .partition_label = fs_info.partition_label,
        .format_if_mount_failed = fs_info.format_if_mount_failed,
        .dont_mount = false
    };

    ESP_LOGI(TAG, "Mounting LittleFS: mount_point=%s, partition_label=%s", 
             conf.base_path, conf.partition_label);

    // 使用esp_vfs_littlefs_register注册文件系统（参考官方demo）
    ret = esp_vfs_littlefs_register(&conf);
    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            ESP_LOGE(TAG, "Failed to mount or format filesystem");
        } else if (ret == ESP_ERR_NOT_FOUND) {
            ESP_LOGE(TAG, "Failed to find LittleFS partition with label: %s", conf.partition_label);
        } else {
            ESP_LOGE(TAG, "Failed to initialize LittleFS (%s)", esp_err_to_name(ret));
        }
        return ret;
    }

    // 获取文件系统信息（参考官方demo）
    size_t total = 0, used = 0;
    ret = esp_littlefs_info(fs_info.partition_label, &total, &used);

    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to get filesystem info");
    } else {
        ESP_LOGI(TAG, "Partition size: total: %d, used: %d", total, used);
    }

    fs_mounted = true;
    ESP_LOGI(TAG, "LittleFS filesystem initialized successfully");
    return ESP_OK;
}

// 反初始化文件系统
esp_err_t littlefs_manager_deinit(void) {
    esp_err_t ret = esp_vfs_littlefs_unregister(fs_info.partition_label);

    if (ret == ESP_OK) {
        fs_mounted = false;
        ESP_LOGI(TAG, "LittleFS filesystem uninitialized");
    } else {
        ESP_LOGE(TAG, "Failed to uninitialize filesystem");
    }

    return ret;
}

// 检查文件系统是否已挂载
bool littlefs_manager_is_mounted(void) {
    return fs_mounted;
}

// 检查文件是否存在
bool littlefs_manager_file_exists(const char* path) {
    if (!fs_mounted || path == NULL) {
        return false;
    }

    char full_path[256];
    snprintf(full_path, sizeof(full_path), "%s%s", fs_info.mount_point, path);

    struct stat st;
    return (stat(full_path, &st) == 0);
}

// 创建目录
bool littlefs_manager_create_dir(const char* path) {
    if (!fs_mounted || path == NULL) {
        return false;
    }

    char full_path[256];
    snprintf(full_path, sizeof(full_path), "%s%s", fs_info.mount_point, path);

    int ret = mkdir(full_path, 0755);
    if (ret != 0 && errno != EEXIST) {
        ESP_LOGE(TAG, "Failed to create directory: %s", path);
        return false;
    }

    return true;
}

// 删除文件
bool littlefs_manager_delete_file(const char* path) {
    if (!fs_mounted || path == NULL) {
        return false;
    }

    char full_path[256];
    snprintf(full_path, sizeof(full_path), "%s%s", fs_info.mount_point, path);

    int ret = remove(full_path);
    if (ret != 0) {
        ESP_LOGE(TAG, "Failed to delete file: %s", path);
        return false;
    }

    return true;
}

// 读取文件内容（通用函数，参考官方demo）
char* littlefs_manager_read_file(const char* path) {
    if (!fs_mounted || path == NULL) {
        ESP_LOGE(TAG, "Filesystem not mounted or path is NULL");
        return NULL;
    }

    char full_path[256];
    snprintf(full_path, sizeof(full_path), "%s%s", fs_info.mount_point, path);

    // 使用fopen和fread读取文件（参考官方demo）
    FILE* file = fopen(full_path, "r");
    if (file == NULL) {
        ESP_LOGE(TAG, "Failed to open file for reading: %s", full_path);
        return NULL;
    }

    // 获取文件大小（参考官方demo的文件读取方式）
    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);

    if (file_size <= 0) {
        ESP_LOGW(TAG, "File is empty or error getting file size: %s", full_path);
        fclose(file);
        return NULL;
    }

    // 分配内存并读取文件内容
    char* content = malloc(file_size + 1);
    if (content == NULL) {
        ESP_LOGE(TAG, "Failed to allocate memory for file content: %s", full_path);
        fclose(file);
        return NULL;
    }

    size_t bytes_read = fread(content, 1, file_size, file);
    if (bytes_read != (size_t)file_size) {
        ESP_LOGE(TAG, "Failed to read complete file: %s (read %zu of %ld bytes)", 
                 full_path, bytes_read, file_size);
        free(content);
        fclose(file);
        return NULL;
    }
    
    content[bytes_read] = '\0';
    fclose(file);
    
    ESP_LOGI(TAG, "File read successfully: %s (%ld bytes)", full_path, file_size);
    return content;
}

// 写入文件内容（通用函数，参考官方demo）
bool littlefs_manager_write_file(const char* path, const char* content) {
    if (!fs_mounted || path == NULL || content == NULL) {
        return false;
    }

    char full_path[256];
    snprintf(full_path, sizeof(full_path), "%s%s", fs_info.mount_point, path);

    // 使用fopen和fwrite写入文件（参考官方demo）
    FILE* file = fopen(full_path, "w");
    if (file == NULL) {
        ESP_LOGE(TAG, "Failed to open file for writing: %s", full_path);
        return false;
    }

    size_t bytes_written = fwrite(content, 1, strlen(content), file);
    fclose(file);

    if (bytes_written != strlen(content)) {
        ESP_LOGE(TAG, "Failed to write complete file content: %s (wrote %zu of %zu bytes)", 
                 full_path, bytes_written, strlen(content));
        return false;
    }

    ESP_LOGI(TAG, "File written successfully: %s", full_path);
    return true;
}

// 追加文件内容（通用函数）
bool littlefs_manager_append_file(const char* path, const char* content) {
    if (!fs_mounted || path == NULL || content == NULL) {
        return false;
    }

    char full_path[256];
    snprintf(full_path, sizeof(full_path), "%s%s", fs_info.mount_point, path);

    FILE* file = fopen(full_path, "a");
    if (file == NULL) {
        ESP_LOGE(TAG, "Failed to open file for appending: %s", path);
        return false;
    }

    size_t bytes_written = fwrite(content, 1, strlen(content), file);
    fclose(file);

    if (bytes_written != strlen(content)) {
        ESP_LOGE(TAG, "Failed to append complete file content: %s", path);
        return false;
    }

    return true;
}

// 列出目录中的文件
bool littlefs_manager_list_files(const char* path) {
    if (!fs_mounted || path == NULL) {
        return false;
    }

    char full_path[256];
    snprintf(full_path, sizeof(full_path), "%s%s", fs_info.mount_point, path);

    DIR* dir = opendir(full_path);
    if (dir == NULL) {
        ESP_LOGE(TAG, "Failed to open directory: %s", path);
        return false;
    }

    struct dirent* entry;
    ESP_LOGI(TAG, "Files in directory %s:", path);
    while ((entry = readdir(dir)) != NULL) {
        ESP_LOGI(TAG, "  %s", entry->d_name);
    }

    closedir(dir);
    return true;
}

// 使用迭代方式搜索文件的辅助函数（避免递归导致的栈溢出）
static bool littlefs_manager_find_file_iterative(const char* filename, 
                                                char* found_path, 
                                                size_t path_size) {
    if (filename == NULL || found_path == NULL || path_size == 0) {
        ESP_LOGE(TAG, "Invalid parameters for file search");
        return false;
    }
    
    // 使用队列进行广度优先搜索，避免递归深度过大
    const size_t queue_size = 50;  // 减少队列大小，避免内存消耗过大
    char** dir_queue = malloc(sizeof(char*) * queue_size);
    size_t queue_front = 0;
    size_t queue_rear = 0;
    
    if (dir_queue == NULL) {
        ESP_LOGE(TAG, "Failed to allocate memory for directory queue");
        return false;
    }
    
    // 初始化队列，从根目录开始
    dir_queue[queue_rear] = strdup("/");
    if (dir_queue[queue_rear] == NULL) {
        ESP_LOGE(TAG, "Failed to allocate memory for root directory");
        free(dir_queue);
        return false;
    }
    queue_rear = (queue_rear + 1) % queue_size;
    
    bool found = false;
    size_t search_depth = 0;
    const size_t max_depth = 10;  // 限制搜索深度
    
    while (queue_front != queue_rear && !found && search_depth < max_depth) {
        search_depth++;
        
        // 从队列中取出当前目录
        char* current_dir = dir_queue[queue_front];
        queue_front = (queue_front + 1) % queue_size;
        
        // 构建完整路径
        char full_path[256];
        int needed = snprintf(full_path, sizeof(full_path), "%s%s", fs_info.mount_point, current_dir);
        if (needed < 0 || needed >= (int)sizeof(full_path)) {
            ESP_LOGE(TAG, "Path too long: %s%s", fs_info.mount_point, current_dir);
            free(current_dir);
            continue;
        }
        
        DIR* dir = opendir(full_path);
        if (dir == NULL) {
            ESP_LOGW(TAG, "Failed to open directory: %s", full_path);
            free(current_dir);
            continue;
        }
        
        struct dirent* entry;
        while ((entry = readdir(dir)) != NULL && !found) {
            // 安全检查：确保entry指针有效
            if (entry == NULL) {
                continue;
            }
            
            // 安全复制文件名
            char entry_filename[256];
            memset(entry_filename, 0, sizeof(entry_filename));
            size_t max_copy_len = sizeof(entry_filename) - 1;
            strncpy(entry_filename, entry->d_name, max_copy_len);
            entry_filename[max_copy_len] = '\0';
            
            // 跳过当前目录和上级目录
            if (strcmp(entry_filename, ".") == 0 || strcmp(entry_filename, "..") == 0) {
                continue;
            }
            
            // 构建相对路径
            char item_path[512];  // 减少缓冲区大小
            needed = snprintf(item_path, sizeof(item_path), "%s/%s", current_dir, entry_filename);
            if (needed < 0 || needed >= (int)sizeof(item_path)) {
                ESP_LOGW(TAG, "Item path too long, skipping: %s/%s", current_dir, entry_filename);
                continue;
            }
            
            // 构建完整路径
            char full_item_path[512];  // 减少缓冲区大小
            needed = snprintf(full_item_path, sizeof(full_item_path), "%s%s", fs_info.mount_point, item_path);
            if (needed < 0 || needed >= (int)sizeof(full_item_path)) {
                ESP_LOGW(TAG, "Full item path too long, skipping: %s%s", fs_info.mount_point, item_path);
                continue;
            }
            
            struct stat st;
            if (stat(full_item_path, &st) != 0) {
                ESP_LOGW(TAG, "Failed to stat file: %s", full_item_path);
                continue;
            }
            
            // 如果是目录，加入队列
            if (S_ISDIR(st.st_mode)) {
                size_t next_rear = (queue_rear + 1) % queue_size;
                if (next_rear != queue_front) {  // 队列未满
                    dir_queue[queue_rear] = strdup(item_path);
                    if (dir_queue[queue_rear] != NULL) {
                        queue_rear = next_rear;
                    } else {
                        ESP_LOGW(TAG, "Failed to allocate memory for directory path: %s", item_path);
                    }
                } else {
                    ESP_LOGW(TAG, "Directory queue is full, skipping: %s", item_path);
                }
            }
            // 如果是文件，检查是否匹配目标文件名
            else if (S_ISREG(st.st_mode)) {
                if (strcmp(entry_filename, filename) == 0) {
                    // 找到文件，复制路径到输出缓冲区
                    strncpy(found_path, item_path, path_size - 1);
                    found_path[path_size - 1] = '\0';
                    found = true;
                    ESP_LOGI(TAG, "Found file: %s at path: %s", filename, item_path);
                }
            }
        }
        
        closedir(dir);
        free(current_dir);
    }
    
    // 清理队列中剩余的项目
    while (queue_front != queue_rear) {
        free(dir_queue[queue_front]);
        queue_front = (queue_front + 1) % queue_size;
    }
    
    free(dir_queue);
    
    if (!found) {
        ESP_LOGI(TAG, "File not found: %s", filename);
    }
    
    return found;
}

// 新增：详细列出目录中的文件（类似ls -lh，优化内存使用）
bool littlefs_manager_list_files_detailed(const char* path) {
    if (!fs_mounted || path == NULL) {
        ESP_LOGE(TAG, "Filesystem not mounted or path is NULL");
        return false;
    } else if (strcmp(path, "/") == 0 || strcmp(path, "\\") == 0) {
        path = "";
    }

    char full_path[256];
    snprintf(full_path, sizeof(full_path), "%s%s", fs_info.mount_point, path);

    DIR* dir = opendir(full_path);
    if (dir == NULL) {
        ESP_LOGE(TAG, "Failed to open directory: %s", full_path);
        return false;
    }

    struct dirent* entry;
    ESP_LOGI(TAG, "=== 目录 %s 的详细内容 ===", path);
    ESP_LOGI(TAG, "权限\t大小\t名称");
    ESP_LOGI(TAG, "------\t------\t------");

    // 限制最大文件数量，避免内存耗尽
    const size_t max_files = 100;
    size_t file_count = 0;

    while ((entry = readdir(dir)) != NULL && file_count < max_files) {
        // 安全检查：确保entry指针有效
        if (entry == NULL) {
            ESP_LOGW(TAG, "NULL entry pointer, skipping");
            continue;
        }
        
        // 安全复制文件名
        char filename[256];
        memset(filename, 0, sizeof(filename));
        size_t max_copy_len = sizeof(filename) - 1;
        strncpy(filename, entry->d_name, max_copy_len);
        filename[max_copy_len] = '\0';
        
        // 安全检查：文件名长度和有效性
        size_t name_len = strlen(filename);
        if (name_len == 0 || name_len > 255) {
            ESP_LOGW(TAG, "Invalid filename length: %s", filename);
            continue;
        }

        // 使用固定大小的缓冲区，避免栈溢出
        char item_path[512];  // 减少缓冲区大小
        int needed = snprintf(NULL, 0, "%s/%s", full_path, filename);
        if (needed < 0 || needed >= (int)sizeof(item_path)) {
            ESP_LOGE(TAG, "路径过长，跳过文件: %s", filename);
            continue;
        }
        snprintf(item_path, sizeof(item_path), "%s/%s", full_path, filename);

        struct stat st;
        if (stat(item_path, &st) != 0) {
            ESP_LOGW(TAG, "Failed to get file info: %s", filename);
            continue;
        }

        // 格式化文件权限
        char permissions[11];
        snprintf(permissions, sizeof(permissions), "%c%c%c%c%c%c%c%c%c%c",
                 S_ISDIR(st.st_mode) ? 'd' : '-',
                 (st.st_mode & S_IRUSR) ? 'r' : '-',
                 (st.st_mode & S_IWUSR) ? 'w' : '-',
                 (st.st_mode & S_IXUSR) ? 'x' : '-',
                 (st.st_mode & S_IRGRP) ? 'r' : '-',
                 (st.st_mode & S_IWGRP) ? 'w' : '-',
                 (st.st_mode & S_IXGRP) ? 'x' : '-',
                 (st.st_mode & S_IROTH) ? 'r' : '-',
                 (st.st_mode & S_IWOTH) ? 'w' : '-',
                 (st.st_mode & S_IXOTH) ? 'x' : '-');

        // 格式化文件大小
        char size_str[16];
        if (S_ISDIR(st.st_mode)) {
            snprintf(size_str, sizeof(size_str), "-");
        } else {
            if (st.st_size < 1024) {
                snprintf(size_str, sizeof(size_str), "%ldB", st.st_size);
            } else if (st.st_size < 1024 * 1024) {
                snprintf(size_str, sizeof(size_str), "%.1fK", st.st_size / 1024.0);
            } else {
                snprintf(size_str, sizeof(size_str), "%.1fM", st.st_size / (1024.0 * 1024.0));
            }
        }

        ESP_LOGI(TAG, "%s\t%s\t%s", permissions, size_str, filename);
        file_count++;
    }

    if (file_count >= max_files) {
        ESP_LOGW(TAG, "文件数量超过限制(%d)，已停止列出", max_files);
    }

    closedir(dir);

    // 显示文件系统使用情况
    size_t total_size = 0, used_size = 0;
    esp_err_t ret = esp_littlefs_info(fs_info.partition_label, &total_size, &used_size);
    
    if (ret == ESP_OK) {
        double total_mb = total_size / (1024.0 * 1024.0);
        double used_mb = used_size / (1024.0 * 1024.0);
        double free_mb = total_mb - used_mb;
        double used_percent = (used_size * 100.0) / total_size;

        ESP_LOGI(TAG, "=== 文件系统统计 ===");
        ESP_LOGI(TAG, "总空间: %.2f MB", total_mb);
        ESP_LOGI(TAG, "已使用: %.2f MB (%.1f%%)", used_mb, used_percent);
        ESP_LOGI(TAG, "可用空间: %.2f MB", free_mb);
    } else {
        ESP_LOGW(TAG, "无法获取文件系统统计信息");
    }

    return true;
}

// 新增：动态查找文件（迭代搜索，避免栈溢出）
bool littlefs_manager_find_file(const char* filename, char* found_path, size_t path_size) {
    if (!fs_mounted || filename == NULL || found_path == NULL || path_size == 0) {
        return false;
    }

    // 从根目录开始搜索，使用迭代方式避免栈溢出
    return littlefs_manager_find_file_iterative(filename, found_path, path_size);
}

// 获取文件大小
size_t littlefs_manager_get_file_size(const char* path) {
    if (!fs_mounted || path == NULL) {
        return 0;
    }

    char full_path[256];
    snprintf(full_path, sizeof(full_path), "%s%s", fs_info.mount_point, path);

    struct stat st;
    if (stat(full_path, &st) != 0) {
        return 0;
    }

    return st.st_size;
}

// 获取文件系统总大小
size_t littlefs_manager_get_total_size(void) {
    if (!fs_mounted) {
        return 0;
    }

    size_t total = 0, used = 0;
    esp_err_t ret = esp_littlefs_info(fs_info.partition_label, &total, &used);
    
    if (ret != ESP_OK) {
        return 0;
    }

    return total;
}

// 获取文件系统已用大小
size_t littlefs_manager_get_used_size(void) {
    if (!fs_mounted) {
        return 0;
    }

    size_t total = 0, used = 0;
    esp_err_t ret = esp_littlefs_info(fs_info.partition_label, &total, &used);
    
    if (ret != ESP_OK) {
        return 0;
    }

    return used;
}

// 重命名文件（参考官方demo的rename功能）
bool littlefs_manager_rename_file(const char* old_path, const char* new_path) {
    if (!fs_mounted || old_path == NULL || new_path == NULL) {
        return false;
    }

    char full_old_path[256];
    char full_new_path[256];
    snprintf(full_old_path, sizeof(full_old_path), "%s%s", fs_info.mount_point, old_path);
    snprintf(full_new_path, sizeof(full_new_path), "%s%s", fs_info.mount_point, new_path);

    // 使用标准rename函数（参考官方demo）
    int ret = rename(full_old_path, full_new_path);
    if (ret != 0) {
        ESP_LOGE(TAG, "Failed to rename file from %s to %s", old_path, new_path);
        return false;
    }

    ESP_LOGI(TAG, "File renamed successfully: %s -> %s", old_path, new_path);
    return true;
}

// Web专用文件服务函数（专用层）
esp_err_t littlefs_manager_serve_web_file(const char* filepath, 
                                         const char** content_type, 
                                         char** file_content, 
                                         size_t* file_size) {
    if (!fs_mounted || filepath == NULL || content_type == NULL || 
        file_content == NULL || file_size == NULL) {
        ESP_LOGE(TAG, "Invalid parameters to serve_web_file");
        return ESP_FAIL;
    }

    // 根据文件扩展名设置内容类型
    const char* ext = strrchr(filepath, '.');
    if (ext != NULL) {
        if (strcmp(ext, ".html") == 0) {
            *content_type = "text/html";
        } else if (strcmp(ext, ".css") == 0) {
            *content_type = "text/css";
        } else if (strcmp(ext, ".js") == 0) {
            *content_type = "application/javascript";
        } else if (strcmp(ext, ".json") == 0) {
            *content_type = "application/json";
        } else if (strcmp(ext, ".png") == 0) {
            *content_type = "image/png";
        } else if (strcmp(ext, ".jpg") == 0 || strcmp(ext, ".jpeg") == 0) {
            *content_type = "image/jpeg";
        } else {
            *content_type = "text/plain";
        }
    } else {
        *content_type = "text/plain";
    }

    // 读取文件内容
    *file_content = littlefs_manager_read_file(filepath);
    if (*file_content == NULL) {
        ESP_LOGE(TAG, "Failed to read file: %s", filepath);
        return ESP_FAIL;
    }

    *file_size = strlen(*file_content);
    return ESP_OK;
}