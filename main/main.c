/*
 * SPDX-FileCopyrightText: 2010-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 * 
 * 作者：wuming
 * 创建时间：2025-10-1
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
static const uint32_t mask_value = XTPERF_MASK_CYCLES;     // 掩码值
static const uint32_t kernelcnt_value = 0; // 内核计数
static const uint32_t tracelevel_value = 0; // 跟踪级别

TaskHandle_t runtime_0_task_handle = NULL;
TaskHandle_t runtime_1_task_handle = NULL;

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
    .current_mode = FREQ_MODE_PERFORMANCE,     // FREQ_MODE_BALANCED
    .performance_freq = 240,
    .balanced_freq = 160,
    .power_save_freq = 80,
    .custom_freq = 200};

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
    
    // 添加基准值记录
    static uint32_t last_perf_counter_value_core0 = 0;
    bool first_reading = true;
    
    // 定时器变量 - 统一使用millis()
    unsigned long previousMillis_1 = millis();  // 1秒间隔
    const long interval_1 = 1 * 1000;     // 1秒间隔时间（毫秒）

    while (1) {
        unsigned long currentMillis = millis();  // 使用millis()获取当前时间
        
        // 1秒间隔：更新Core 0的CPU使用率
        if (currentMillis - previousMillis_1 >= interval_1) {
            previousMillis_1 = currentMillis;

            // 实时获取当前CPU频率（单位：MHz）
            uint32_t cpu_frequency_mhz = (uint32_t)esp_rom_get_cpu_ticks_per_us();
            uint32_t cpu_frequency_hz = cpu_frequency_mhz * 1000000;
            
            xtensa_perfmon_stop();    // 停止性能监控并读取计数器值
            uint32_t current_perf_counter_value_core0 = xtensa_perfmon_value(PERF_COUNTER_CORE0); // 读取核心0的性能计数器的值
            
            if (!first_reading) {
                // 计算1秒内的实际周期数
                uint32_t cycles_used = current_perf_counter_value_core0 - last_perf_counter_value_core0;
                // 计算总可用周期数（频率 × 时间）
                uint32_t total_available_cycles = cpu_frequency_hz; // 1秒内的总周期数
                
                // 计算CPU使用率
                cpu_usage[0] = (float)cycles_used / (float)total_available_cycles * 100.0f;
                
                // 限制使用率范围在0-100%
                if (cpu_usage[0] < 0.0f) cpu_usage[0] = 0.0f;
                if (cpu_usage[0] > 100.0f) cpu_usage[0] = 100.0f;
                
                ESP_LOGI(TAG, "核心0的CPU占用率：%.2f%% (频率：%"PRIu32"MHz)", cpu_usage[0], cpu_frequency_mhz);
            } else {
                first_reading = false;
                ESP_LOGI(TAG, "核心0：首次读数，跳过计算");
            }
            
            last_perf_counter_value_core0 = current_perf_counter_value_core0;
            xtensa_perfmon_reset(PERF_COUNTER_CORE0);    // 重置性能计数器，为下一次计算做准备
            xtensa_perfmon_start();    // 重新启动性能监控
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
    
    // 添加基准值记录
    static uint32_t last_perf_counter_value_core1 = 0;
    bool first_reading = true;
    
    // 定时器变量 - 统一使用millis()
    unsigned long previousMillis_1 = millis();  // 1秒间隔
    const long interval_1 = 1 * 1000;     // 1秒间隔时间（毫秒）

    while (1) {
        unsigned long currentMillis = millis();  // 使用millis()获取当前时间
        
        // 1秒间隔：更新Core 1的CPU使用率
        if (currentMillis - previousMillis_1 >= interval_1) {
            previousMillis_1 = currentMillis;

            // 实时获取当前CPU频率（单位：MHz）
            uint32_t cpu_frequency_mhz = (uint32_t)esp_rom_get_cpu_ticks_per_us();
            uint32_t cpu_frequency_hz = cpu_frequency_mhz * 1000000;
            
            xtensa_perfmon_stop();    // 停止性能监控并读取计数器值
            uint32_t current_perf_counter_value_core1 = xtensa_perfmon_value(PERF_COUNTER_CORE1); // 读取核心1的性能计数器的值
            
            if (!first_reading) {
                // 计算1秒内的实际周期数
                uint32_t cycles_used = current_perf_counter_value_core1 - last_perf_counter_value_core1;
                // 计算总可用周期数（频率 × 时间）
                uint32_t total_available_cycles = cpu_frequency_hz; // 1秒内的总周期数
                
                // 计算CPU使用率
                cpu_usage[1] = (float)cycles_used / (float)total_available_cycles * 100.0f;
                
                // 限制使用率范围在0-100%
                if (cpu_usage[1] < 0.0f) cpu_usage[1] = 0.0f;
                if (cpu_usage[1] > 100.0f) cpu_usage[1] = 100.0f;
                
                ESP_LOGI(TAG, "核心1的CPU占用率：%.2f%% (频率：%"PRIu32"MHz)", cpu_usage[1], cpu_frequency_mhz);
            } else {
                first_reading = false;
                ESP_LOGI(TAG, "核心1：首次读数，跳过计算");
            }
            
            last_perf_counter_value_core1 = current_perf_counter_value_core1;
            xtensa_perfmon_reset(PERF_COUNTER_CORE1);    // 重置性能计数器，为下一次计算做准备
            xtensa_perfmon_start();    // 重新启动性能监控
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
    if (xTaskCreatePinnedToCore(core0_perfmon_task, "core0_perfmon", 4096, NULL, tskIDLE_PRIORITY+5, &runtime_0_task_handle, 0) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create Core0 performance monitoring task");
        return;
    }

    // 创建Core 1性能监控任务（运行在Core 1）
    if (xTaskCreatePinnedToCore(core1_perfmon_task, "core1_perfmon", 4096, NULL, tskIDLE_PRIORITY+5, &runtime_1_task_handle, 1) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create Core1 performance monitoring task");
        return;
    }

    ESP_LOGI(TAG, "All performance monitoring tasks created successfully");
}