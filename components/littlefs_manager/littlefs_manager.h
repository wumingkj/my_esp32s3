#ifndef LITTLEFS_MANAGER_H
#define LITTLEFS_MANAGER_H

#include <stdio.h>
#include <string.h>

#include "dirent.h"
#include "esp_littlefs.h"
#include "esp_log.h"
#include "esp_partition.h"
#include "esp_system.h"
#include "esp_vfs.h"
#include "sys/stat.h"

#ifdef __cplusplus
extern "C" {
#endif

// 文件系统信息结构体
typedef struct {
    const char* mount_point;      // 挂载点
    const char* partition_label;  // 分区标签
    bool format_if_mount_failed;  // 挂载失败时是否格式化
} filesystem_info_t;

// 文件系统操作函数声明
esp_err_t littlefs_manager_init(void);
esp_err_t littlefs_manager_deinit(void);
bool littlefs_manager_is_mounted(void);

// 通用文件操作函数（基础层）
bool littlefs_manager_file_exists(const char* path);
bool littlefs_manager_create_dir(const char* path);
bool littlefs_manager_delete_file(const char* path);
char* littlefs_manager_read_file(const char* path);
bool littlefs_manager_write_file(const char* path, const char* content);
bool littlefs_manager_append_file(const char* path, const char* content);

// 目录操作函数
bool littlefs_manager_list_files(const char* path);
size_t littlefs_manager_get_file_size(const char* path);

// 文件重命名函数（参考官方demo）
bool littlefs_manager_rename_file(const char* old_path, const char* new_path);

// 新增：详细文件列表函数
bool littlefs_manager_list_files_detailed(const char* path);
bool littlefs_manager_find_file(const char* filename, char* found_path, size_t path_size);

// 文件系统信息函数
size_t littlefs_manager_get_total_size(void);
size_t littlefs_manager_get_used_size(void);

// Web专用文件服务函数（专用层）
esp_err_t littlefs_manager_serve_web_file(const char* filepath, const char** content_type, char** file_content,
                                          size_t* file_size);

#ifdef __cplusplus
}
#endif

#endif  // LITTLEFS_MANAGER_H