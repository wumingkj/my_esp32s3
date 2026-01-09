#include "esp_log.h"
#include "littlefs_manager.h"

static const char* TAG = "littlefs_example";

void littlefs_example_task(void* pvParameters) {
    // 初始化文件系统
    esp_err_t ret = littlefs_manager_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize filesystem");
        vTaskDelete(NULL);
        return;
    }

    // 检查文件系统是否挂载成功
    if (!littlefs_manager_is_mounted()) {
        ESP_LOGE(TAG, "Filesystem not mounted");
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "Filesystem initialized successfully");

    // 创建测试目录
    if (littlefs_manager_create_dir("/test")) {
        ESP_LOGI(TAG, "Directory /test created successfully");
    }

    // 写入测试文件
    const char* test_content = "Hello LittleFS from ESP-IDF!";
    if (littlefs_manager_write_file("/test/hello.txt", test_content)) {
        ESP_LOGI(TAG, "File /test/hello.txt written successfully");
    }

    // 检查文件是否存在
    if (littlefs_manager_file_exists("/test/hello.txt")) {
        ESP_LOGI(TAG, "File /test/hello.txt exists");
    }

    // 读取文件内容
    char* content = littlefs_manager_read_file("/test/hello.txt");
    if (content != NULL) {
        ESP_LOGI(TAG, "File content: %s", content);
        free(content);
    }

    // 获取文件大小
    size_t file_size = littlefs_manager_get_file_size("/test/hello.txt");
    ESP_LOGI(TAG, "File size: %d bytes", file_size);

    // 追加内容到文件
    const char* append_content = "\nThis is appended content!";
    if (littlefs_manager_append_file("/test/hello.txt", append_content)) {
        ESP_LOGI(TAG, "Content appended to file successfully");
    }

    // 再次读取文件内容
    content = littlefs_manager_read_file("/test/hello.txt");
    if (content != NULL) {
        ESP_LOGI(TAG, "Updated file content: %s", content);
        free(content);
    }

    // 列出目录内容
    littlefs_manager_list_files("/test");

    // 获取文件系统信息
    size_t total_size = littlefs_manager_get_total_size();
    size_t used_size = littlefs_manager_get_used_size();
    ESP_LOGI(TAG, "Filesystem info - Total: %d bytes, Used: %d bytes", total_size, used_size);

    // 保持任务运行，定期检查文件系统状态
    while (1) {
        if (littlefs_manager_is_mounted()) {
            ESP_LOGI(TAG, "Filesystem is still mounted");
        } else {
            ESP_LOGE(TAG, "Filesystem is not mounted!");
        }
        vTaskDelay(pdMS_TO_TICKS(10000));  // 每10秒检查一次
    }

    vTaskDelete(NULL);
}

// 启动示例任务
void start_littlefs_example(void) {
    xTaskCreate(littlefs_example_task, "littlefs_example", 4096, NULL, 5, NULL);
}