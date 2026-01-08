/*
 * SPDX-FileCopyrightText: 2010-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 * 
 */

#include <stdio.h>
#include <inttypes.h>
#include <dirent.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "esp_chip_info.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_task_wdt.h"

#include "partition_manager.h"
#include "frequency_manager.h"
#include "wifi_manager.h"
#include "littlefs_manager.h"
#include "session_manager.h"
#include "user_manager.h"
#include "servo_control.h"
#include "ws2812_led.h"

static const char *TAG = "Main";

// 全局CPU使用率数组 [core0, core1]
static float cpu_usage[2] = {0.0f, 0.0f};
static SemaphoreHandle_t cpu_usage_mutex;

// 任务统计相关变量
static uint32_t task_runtime_core0 = 0;
static uint32_t task_runtime_core1 = 0;
static uint32_t last_stat_time = 0;

// 舵机配置
static servo_config_t servo_config = {
    .pin = SERVO_PIN,
    .channel = SERVO_CHANNEL,
    .timer = SERVO_TIMER,
    .speed_mode = SERVO_SPEED_MODE,
    .frequency = SERVO_FREQUENCY,
    .resolution = SERVO_RESOLUTION,
    .min_pulsewidth = SERVO_MIN_PULSEWIDTH,
    .max_pulsewidth = SERVO_MAX_PULSEWIDTH
};

// 频率管理器配置
static frequency_manager_config_t freq_config = {
    .current_mode = FREQ_MODE_BALANCED,
    .performance_freq = 240,
    .balanced_freq = 160,
    .power_save_freq = 80,
    .custom_freq = 240};

// WiFi配置
static wifi_manager_config_t wifi_config = {
    .ap_ssid = "ESP32-S3-AP",
    .ap_password = "12345678",
    .sta_ssid = "",
    .sta_password = "",
    .enable_nat = true,
    .enable_dhcp_server = true};

// 获取任务运行时间统计
static void get_task_runtime_stats(void)
{
    TaskStatus_t *pxTaskStatusArray;
    volatile UBaseType_t uxArraySize, x;
    uint32_t ulTotalRunTime;
    
    // 获取当前任务数量
    uxArraySize = uxTaskGetNumberOfTasks();
    
    // 分配内存存储任务状态
    pxTaskStatusArray = pvPortMalloc(uxArraySize * sizeof(TaskStatus_t));
    
    if (pxTaskStatusArray != NULL) {
        // 获取任务状态信息
        uxArraySize = uxTaskGetSystemState(pxTaskStatusArray, uxArraySize, &ulTotalRunTime);
        
        // 重置核心运行时间
        task_runtime_core0 = 0;
        task_runtime_core1 = 0;
        
        // 遍历所有任务，统计各核心的运行时间
        for (x = 0; x < uxArraySize; x++) {
            // 根据任务名称判断运行在哪个核心
            if (strstr(pxTaskStatusArray[x].pcTaskName, "sys_mgmt") != NULL || 
                strstr(pxTaskStatusArray[x].pcTaskName, "cpu_monitor") != NULL) {
                task_runtime_core0 += pxTaskStatusArray[x].ulRunTimeCounter;
            } else if (strstr(pxTaskStatusArray[x].pcTaskName, "hw_ctrl") != NULL) {
                task_runtime_core1 += pxTaskStatusArray[x].ulRunTimeCounter;
            }
        }
        
        vPortFree(pxTaskStatusArray);
    }
}

// 更新CPU使用率（基于真实的任务运行时间统计）
static void update_cpu_usage(void)
{
    static uint32_t last_runtime_core0 = 0;
    static uint32_t last_runtime_core1 = 0;
    static uint32_t last_total_time = 0;
    
    uint32_t current_time = xTaskGetTickCount();
    uint32_t elapsed_ticks = current_time - last_total_time;
    
    // 每1秒更新一次
    if (elapsed_ticks >= pdMS_TO_TICKS(1000)) {
        get_task_runtime_stats();
        
        if (last_total_time > 0) {
            // 计算每个核心的CPU使用率
            uint32_t runtime_diff_core0 = task_runtime_core0 - last_runtime_core0;
            uint32_t runtime_diff_core1 = task_runtime_core1 - last_runtime_core1;
            
            // 转换为百分比（假设每个tick为1ms）
            float usage_core0 = (runtime_diff_core0 * 100.0f) / (elapsed_ticks * portTICK_PERIOD_MS);
            float usage_core1 = (runtime_diff_core1 * 100.0f) / (elapsed_ticks * portTICK_PERIOD_MS);
            
            if (xSemaphoreTake(cpu_usage_mutex, portMAX_DELAY) == pdTRUE) {
                cpu_usage[0] = usage_core0 > 100.0f ? 100.0f : usage_core0;
                cpu_usage[1] = usage_core1 > 100.0f ? 100.0f : usage_core1;
                xSemaphoreGive(cpu_usage_mutex);
            }
        }
        
        last_runtime_core0 = task_runtime_core0;
        last_runtime_core1 = task_runtime_core1;
        last_total_time = current_time;
    }
}

// 获取CPU使用率
void get_cpu_usage(float *core0, float *core1)
{
    if (xSemaphoreTake(cpu_usage_mutex, portMAX_DELAY) == pdTRUE) {
        *core0 = cpu_usage[0];
        *core1 = cpu_usage[1];
        xSemaphoreGive(cpu_usage_mutex);
    }
}

// CPU使用率监控任务
static void cpu_monitor_task(void *pvParameters)
{
    ESP_LOGI(TAG, "CPU monitor task started on core %d", xPortGetCoreID());
    
    // 初始化统计时间
    last_stat_time = xTaskGetTickCount();
    
    while (1) {
        update_cpu_usage();
        
        // 每1秒更新一次
        float core0, core1;
        get_cpu_usage(&core0, &core1);
        
        ESP_LOGI(TAG, "CPU Usage - Core0: %.1f%%, Core1: %.1f%%", core0, core1);
        
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

// 系统管理任务（运行在Core 0）
static void system_management_task(void *arg)
{
    ESP_LOGI(TAG, "System management task started on core %d", xPortGetCoreID());

    // 初始化文件系统
    esp_err_t ret = littlefs_manager_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Filesystem initialization failed: %s", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "Filesystem initialized successfully");
        littlefs_manager_list_files_detailed("/");
    }

    // 初始化用户管理器
    ret = user_manager_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "User manager initialization failed: %s", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "User manager initialized successfully");
    }

    // 初始化会话管理器
    session_manager_init();
    ESP_LOGI(TAG, "Session manager initialized successfully");

    // 显示系统信息
    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);

    ESP_LOGI(TAG, "Chip information:");
    ESP_LOGI(TAG, "  Model: %s", (chip_info.model == CHIP_ESP32S3) ? "ESP32-S3" : "Unknown");
    ESP_LOGI(TAG, "  Cores: %d", chip_info.cores);

    // WiFi管理功能
    ESP_LOGI(TAG, "WiFi manager task started");

    ret = wifi_manager_init(&wifi_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WiFi manager initialization failed: %s", esp_err_to_name(ret));
        vTaskDelete(NULL);
        return;
    }

    ret = wifi_manager_start_ap("ap_map", wifi_config.ap_ssid, wifi_config.ap_password);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start AP: %s", esp_err_to_name(ret));
    }

    ret = wifi_manager_start_web_server();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start web server: %s", esp_err_to_name(ret));
    }

    network_info_t net_info;
    wifi_manager_get_network_info(&net_info);
    ESP_LOGI(TAG, "Network Information:");
    ESP_LOGI(TAG, "  AP IP: %s", net_info.ap_ip);

    // 设备映射初始化
    ret = device_mapping_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize device mapping: %s", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "Device mapping initialized successfully");
    }

    // 设备监控循环
    static uint32_t last_device_check_time = 0;
    
    while (1) {
        uint32_t current_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
        
        // 每60秒检查一次设备状态
        if (current_time - last_device_check_time > 60000) {
            device_mapping_refresh_status(30);
            
            int device_count = device_mapping_get_count();
            if (device_count > 0) {
                ESP_LOGI(TAG, "=== Device Mapping Table (%d devices) ===", device_count);
                device_mapping_t **devices = device_mapping_get_all_devices(NULL);
                for (int i = 0; i < device_count; i++) {
                    ESP_LOGI(TAG, "  %d. Hostname: %s, IP: %s, MAC: %s, Active: %s",
                             i + 1, devices[i]->hostname, devices[i]->ip, 
                             devices[i]->mac, devices[i]->is_active ? "Yes" : "No");
                }
            }
            
            // 每30秒扫描一次连接的设备
            wifi_manager_scan_connected_devices("ap_map");
            ESP_LOGI(TAG, "Device scan completed");
            
            last_device_check_time = current_time;
        }

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

// 硬件控制任务（运行在Core 1）
static void hardware_control_task(void *arg)
{
    ESP_LOGI(TAG, "Hardware control task started on core %d", xPortGetCoreID());

    // 初始化舵机控制
    esp_err_t ret = servo_control_init(&servo_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Servo control initialization failed: %s", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "Servo control initialized successfully");
    }

    // 舵机雷达扫描参数
    static int current_angle = 90;
    static int target_angle = 90;
    static int direction = 1;
    static uint32_t last_servo_move_time = 0;
    static bool is_moving = false;
    static uint32_t move_duration = 0;

    const uint32_t min_move_duration = 2000;
    const uint32_t max_move_duration = 5000;

    // WS2812配置
    ws2812_config_t custom_config = {
        .pin = 48,
        .num_leds = 1
    };

    // LED测试循环
    while (1) {
        // WS2812 LED测试
        ret = ws2812_led_init(&custom_config);
        if (ret == ESP_OK) {
            // 简化的LED测试
            ws2812_set_all_color((rgb_color_t){255, 0, 0}); // 红色
            vTaskDelay(pdMS_TO_TICKS(1000));
            ws2812_set_all_color((rgb_color_t){0, 255, 0}); // 绿色
            vTaskDelay(pdMS_TO_TICKS(1000));
            ws2812_set_all_color((rgb_color_t){0, 0, 255}); // 蓝色
            vTaskDelay(pdMS_TO_TICKS(1000));
            ws2812_clear_all();
            ws2812_led_deinit();
        }

        // 舵机控制（简化版）
        if (!is_moving) {
            // 设置新的目标角度
            target_angle = (direction == 1) ? 180 : 0;
            direction = -direction;
            is_moving = true;
            last_servo_move_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
            move_duration = (min_move_duration + max_move_duration) / 2;
            
            // 使用现有的舵机控制函数
            servo_control_set_angle(target_angle);
        } else {
            // 检查移动是否完成
            uint32_t current_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
            if (current_time - last_servo_move_time > move_duration) {
                is_moving = false;
                current_angle = target_angle;
            }
        }

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "ESP32-S3 dual-core application started");

    // 初始化NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGI(TAG, "NVS partition needs erase, performing erase operation...");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    ESP_LOGI(TAG, "NVS initialized successfully");

    // 初始化频率管理器
    ret = frequency_manager_init(&freq_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Frequency manager initialization failed: %s", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "Frequency manager initialized successfully");
        frequency_manager_set_mode(FREQ_MODE_PERFORMANCE);
    }

    // 创建CPU使用率互斥锁
    cpu_usage_mutex = xSemaphoreCreateMutex();
    if (cpu_usage_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create CPU usage mutex");
        return;
    }

    // 创建两个核心任务（优先级相同）
    xTaskCreatePinnedToCore(system_management_task, "sys_mgmt", 8192, NULL, 4, NULL, 0);
    xTaskCreatePinnedToCore(hardware_control_task, "hw_ctrl", 8192, NULL, 4, NULL, 1);
    
    // 创建CPU监控任务（运行在Core 0）
    xTaskCreatePinnedToCore(cpu_monitor_task, "cpu_monitor", 4096, NULL, 3, NULL, 0);

    ESP_LOGI(TAG, "All tasks created successfully");
    
    // 主循环保持空闲
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}