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
#include "esp_timer.h"
#include "esp_task_wdt.h"
#include "perfmon.h"
#include "esp_system.h"

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

#define PERF_COUNTER_CORE0 0  // 性能计数器0监控Core0
#define PERF_COUNTER_CORE1 1  // 性能计数器1监控Core1
static const uint16_t select_value = XTPERF_CNT_CYCLES;  // 关键：监控CPU时钟周期总数
static const uint32_t mask_value = 0;     // 掩码值
static const uint32_t kernelcnt_value = 1; // 内核计数
static const uint32_t tracelevel_value = 0; // 跟踪级别
// 性能监控相关变量 - 每个核心独立
static uint32_t last_cycle_count_core0 = 0;
static uint32_t last_cycle_count_core1 = 0;

uint32_t millis() {
    return esp_timer_get_time() / 1000;
}

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

// Core 0性能监控任务
static void core0_perfmon_task(void *arg) {
    ESP_LOGI(TAG, "Core0 performance monitoring task started");

    // 初始化性能监视 - Core 0（使用变量而非硬编码）
    esp_err_t xtensa_perfmon_result = xtensa_perfmon_init(PERF_COUNTER_CORE0, select_value, mask_value, kernelcnt_value, tracelevel_value);
    if (xtensa_perfmon_result == ESP_OK) {
        ESP_LOGI(TAG, "Core0 perfmon counter initialized successfully");
    } else {
        ESP_LOGE(TAG, "Failed to init perfmon counter for core 0: %s", esp_err_to_name(xtensa_perfmon_result));
        return;
    }

    // 定时器变量 - 统一使用millis()
    unsigned long previousMillis_1 = millis();  // 1秒间隔
    const long interval_1 = 1 * 1000;     // 1秒间隔时间（毫秒）

    while (1) {
        unsigned long currentMillis = millis();  // 使用millis()获取当前时间
        
        // 1秒间隔：更新Core 0的CPU使用率
        if (currentMillis - previousMillis_1 >= interval_1) {
            previousMillis_1 = currentMillis;

            // 直接更新CPU使用率 - Core 0
            uint32_t current_cycle_count = xtensa_perfmon_value(PERF_COUNTER_CORE0);
            if (last_cycle_count_core0 > 0) {
                // 计算CPU使用率：当前周期数 - 上次周期数 = 实际使用的周期数
                uint32_t cycles_used = current_cycle_count - last_cycle_count_core0;
                // 假设最大周期数为240MHz * 1秒 = 240,000,000
                cpu_usage[0] = (float)cycles_used / 240000000.0f * 100.0f;
                ESP_LOGI(TAG, "Core0 CPU usage: %.2f%%", cpu_usage[0]);
            }
            last_cycle_count_core0 = current_cycle_count;
        }
        
        vTaskDelay(pdMS_TO_TICKS(10)); // 10ms延迟，减少CPU占用
    }
}

// Core 1性能监控任务
static void core1_perfmon_task(void *arg) {
    ESP_LOGI(TAG, "Core1 performance monitoring task started");
    
    esp_err_t xtensa_perfmon_result = xtensa_perfmon_init(PERF_COUNTER_CORE1, select_value, mask_value, kernelcnt_value, tracelevel_value);
    if (xtensa_perfmon_result == ESP_OK) {
        ESP_LOGI(TAG, "Core1 perfmon counter initialized successfully");
    } else {
        ESP_LOGE(TAG, "Failed to init perfmon counter for core 1: %s", esp_err_to_name(xtensa_perfmon_result));
        return;
    }
    // 定时器变量 - 统一使用millis()
    unsigned long previousMillis_1 = millis();  // 1秒间隔
    const long interval_1 = 1 * 1000;     // 1秒间隔时间（毫秒）

    while (1) {
        unsigned long currentMillis = millis();  // 使用millis()获取当前时间
        
        // 1秒间隔：更新Core 1的CPU使用率
        if (currentMillis - previousMillis_1 >= interval_1) {
            previousMillis_1 = currentMillis;

            // 直接更新CPU使用率 - Core 1
            uint32_t current_cycle_count = xtensa_perfmon_value(PERF_COUNTER_CORE1);
            if (last_cycle_count_core1 > 0) {
                // 计算CPU使用率：当前周期数 - 上次周期数 = 实际使用的周期数
                uint32_t cycles_used = current_cycle_count - last_cycle_count_core1;
                // 假设最大周期数为240MHz * 1秒 = 240,000,000
                cpu_usage[1] = (float)cycles_used / 240000000.0f * 100.0f;
                ESP_LOGI(TAG, "Core1 CPU usage: %.2f%%", cpu_usage[1]);
            }
            last_cycle_count_core1 = current_cycle_count;
        }
        
        vTaskDelay(pdMS_TO_TICKS(10)); // 10ms延迟，减少CPU占用
    }
}

// 保留app_main函数
void app_main(void) {
    ESP_LOGI(TAG, "ESP32-S3 dual-core performance monitoring started");

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
    }

    // 初始化文件系统
    ret = littlefs_manager_init();
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

    // WiFi管理功能初始化
    ESP_LOGI(TAG, "WiFi manager initialization started");
    ret = wifi_manager_init(&wifi_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WiFi manager initialization failed: %s", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "WiFi manager initialized successfully");
    }

    // 舵机初始化
    ret = servo_control_init(&servo_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Servo initialization failed: %s", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "Servo initialized successfully");
    }

    // WS2812 LED初始化
    ret = ws2812_led_init(NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WS2812 LED initialization failed: %s", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "WS2812 LED initialized successfully");
    }

    // 创建Core 0性能监控任务（运行在Core 0）
    if (xTaskCreatePinnedToCore(core0_perfmon_task, "core0_perfmon", 4096, NULL, 5, NULL, 0) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create Core0 performance monitoring task");
        return;
    }

    // 创建Core 1性能监控任务（运行在Core 1）
    if (xTaskCreatePinnedToCore(core1_perfmon_task, "core1_perfmon", 4096, NULL, 5, NULL, 1) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create Core1 performance monitoring task");
        return;
    }

    ESP_LOGI(TAG, "All performance monitoring tasks created successfully");
}