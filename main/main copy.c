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
#include "perfmon.h"

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

// 性能监控相关变量 - 每个核心独立
static uint32_t last_cycle_count_core0 = 0;
static uint32_t last_cycle_count_core1 = 0;

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

// 初始化Core 0的性能监控计数器
static esp_err_t init_perfmon_core0(void)
{
    esp_err_t ret = xtensa_perfmon_reset(0); // Core 0 计数器
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to reset perfmon counter for core 0: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // 初始化Core 0的性能计数器（监控指令周期）
    ret = xtensa_perfmon_init(0, XTPERF_CNT_INSN, 0, 1, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init perfmon counter for core 0: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // 立即读取一次初始值
    last_cycle_count_core0 = xtensa_perfmon_value(0);
    
    ESP_LOGI(TAG, "Core0 perfmon counter initialized successfully");
    ESP_LOGI(TAG, "Core0 initial cycles: %"PRIu32, last_cycle_count_core0);
    return ESP_OK;
}

// 初始化Core 1的性能监控计数器
static esp_err_t init_perfmon_core1(void)
{
    esp_err_t ret = xtensa_perfmon_reset(1); // Core 1 计数器
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to reset perfmon counter for core 1: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // 初始化Core 1的性能计数器
    ret = xtensa_perfmon_init(1, XTPERF_CNT_INSN, 0, 1, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init perfmon counter for core 1: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // 立即读取一次初始值
    last_cycle_count_core1 = xtensa_perfmon_value(1);
    
    ESP_LOGI(TAG, "Core1 perfmon counter initialized successfully");
    ESP_LOGI(TAG, "Core1 initial cycles: %"PRIu32, last_cycle_count_core1);
    return ESP_OK;
}

// Core 0的CPU使用率更新
static void update_cpu_usage_core0(void)
{
    static uint32_t last_update_time_core0 = 0;
    uint32_t current_time = xTaskGetTickCount();
    
    // 每1秒更新一次
    if (current_time - last_update_time_core0 >= pdMS_TO_TICKS(1000)) {
        // 获取当前周期计数
        uint32_t current_cycles_core0 = xtensa_perfmon_value(0);
        
        // 调试信息：显示当前周期计数
        ESP_LOGI(TAG, "Core0 current cycles: %"PRIu32, current_cycles_core0);
        
        // 检查计数器是否溢出
        if (current_cycles_core0 < last_cycle_count_core0) {
            ESP_LOGW(TAG, "Core0 counter overflow detected");
            last_cycle_count_core0 = current_cycles_core0;
            ESP_LOGI(TAG, "Core0 counter overflow, reset history value");
        } else if (last_cycle_count_core0 > 0) {
            // 计算周期增量
            uint32_t cycles_diff_core0 = current_cycles_core0 - last_cycle_count_core0;
            
            // 计算CPU使用率（基于时间比例）
            float usage_core0 = 0.0f;
            if (cycles_diff_core0 > 0) {
                // 假设最大频率为240MHz，每秒最大周期数为240,000,000
                usage_core0 = (cycles_diff_core0 * 100.0f) / 240000000.0f;
                
                // 调试信息：显示增量和使用率
                ESP_LOGI(TAG, "Core0 cycle diff: %"PRIu32, cycles_diff_core0);
                ESP_LOGI(TAG, "Core0 calculated usage: %.1f%%", usage_core0);
            } else {
                ESP_LOGI(TAG, "Core0 no cycle difference detected");
            }
            
            // 限制在0-100%范围内
            usage_core0 = (usage_core0 < 0.0f) ? 0.0f : (usage_core0 > 100.0f) ? 100.0f : usage_core0;
            
            if (xSemaphoreTake(cpu_usage_mutex, portMAX_DELAY) == pdTRUE) {
                cpu_usage[0] = usage_core0;
                xSemaphoreGive(cpu_usage_mutex);
            }
        } else {
            // 第一次运行，只更新历史值
            ESP_LOGI(TAG, "Core0 first run, updating history value only");
        }
        
        // 更新历史数据
        last_cycle_count_core0 = current_cycles_core0;
        last_update_time_core0 = current_time;
    }
}

// Core 1的CPU使用率更新
static void update_cpu_usage_core1(void)
{
    static uint32_t last_update_time_core1 = 0;
    uint32_t current_time = xTaskGetTickCount();
    
    // 每1秒更新一次
    if (current_time - last_update_time_core1 >= pdMS_TO_TICKS(1000)) {
        // 获取当前周期计数
        uint32_t current_cycles_core1 = xtensa_perfmon_value(1);
        
        // 调试信息：显示当前周期计数
        ESP_LOGI(TAG, "Core1 current cycles: %"PRIu32, current_cycles_core1);
        
        // 检查计数器是否溢出
        if (current_cycles_core1 < last_cycle_count_core1) {
            ESP_LOGW(TAG, "Core1 counter overflow detected");
            last_cycle_count_core1 = current_cycles_core1;
            ESP_LOGI(TAG, "Core1 counter overflow, reset history value");
        } else if (last_cycle_count_core1 > 0) {
            // 计算周期增量
            uint32_t cycles_diff_core1 = current_cycles_core1 - last_cycle_count_core1;
            
            // 计算CPU使用率（基于时间比例）
            float usage_core1 = 0.0f;
            if (cycles_diff_core1 > 0) {
                // 假设最大频率为240MHz，每秒最大周期数为240,000,000
                usage_core1 = (cycles_diff_core1 * 100.0f) / 240000000.0f;
                
                // 调试信息：显示增量和使用率
                ESP_LOGI(TAG, "Core1 cycle diff: %"PRIu32, cycles_diff_core1);
                ESP_LOGI(TAG, "Core1 calculated usage: %.1f%%", usage_core1);
            } else {
                ESP_LOGI(TAG, "Core1 no cycle difference detected");
            }
            
            // 限制在0-100%范围内
            usage_core1 = (usage_core1 < 0.0f) ? 0.0f : (usage_core1 > 100.0f) ? 100.0f : usage_core1;
            
            if (xSemaphoreTake(cpu_usage_mutex, portMAX_DELAY) == pdTRUE) {
                cpu_usage[1] = usage_core1;
                xSemaphoreGive(cpu_usage_mutex);
            }
        } else {
            // 第一次运行，只更新历史值
            ESP_LOGI(TAG, "Core1 first run, updating history value only");
        }
        
        // 更新历史数据
        last_cycle_count_core1 = current_cycles_core1;
        last_update_time_core1 = current_time;
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

// 系统管理任务（运行在Core 0）
static void runtime_core0(void *arg)
{
    ESP_LOGI(TAG, "System management task started on core %d", xPortGetCoreID());

    // 初始化Core 0的性能监控
    esp_err_t ret = init_perfmon_core0();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize Core0 perfmon");
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

    // WiFi管理功能
    ESP_LOGI(TAG, "WiFi manager task started");

    ret = wifi_manager_init(&wifi_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WiFi manager initialization failed: %s", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "WiFi manager initialized successfully");
    }

    // 设备映射功能
    ESP_LOGI(TAG, "Device mapping task started");

    while (1) {
        // 更新Core 0的CPU使用率
        update_cpu_usage_core0();
        
        // 显示CPU使用率
        float core0, core1;
        get_cpu_usage(&core0, &core1);
        ESP_LOGI(TAG, "CPU Usage - Core0: %.1f%%, Core1: %.1f%%", core0, core1);
        
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

// 硬件控制任务（运行在Core 1）
static void hardware_control_task(void *arg)
{
    ESP_LOGI(TAG, "Hardware control task started on core %d", xPortGetCoreID());

    // 初始化Core 1的性能监控
    esp_err_t ret = init_perfmon_core1();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize Core1 perfmon");
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

    // 舵机角度控制变量
    int target_angle = 0;
    // 移除未使用的变量
    // int current_angle = 0;
    int angle_step = 10;
    bool increasing = true;

    while (1) {
        // 更新Core 1的CPU使用率
        update_cpu_usage_core1();
        
        // 舵机角度控制
        if (increasing) {
            target_angle += angle_step;
            if (target_angle >= 180) {
                target_angle = 180;
                increasing = false;
            }
        } else {
            target_angle -= angle_step;
            if (target_angle <= 0) {
                target_angle = 0;
                increasing = true;
            }
        }

        // 设置舵机角度
        ret = servo_control_set_angle(target_angle);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to set servo angle: %s", esp_err_to_name(ret));
        } else {
            // 移除未使用的变量赋值
            // current_angle = target_angle;
            ESP_LOGI(TAG, "Servo angle set to %d degrees", target_angle);
        }

        // WS2812 LED控制
        static uint32_t color_index = 0;
        // 使用rgb_color_t结构体定义颜色
        rgb_color_t colors[] = {
            {255, 0, 0},     // 红色
            {0, 255, 0},     // 绿色
            {0, 0, 255},     // 蓝色
            {255, 255, 0},   // 黄色
            {255, 0, 255},   // 紫色
            {0, 255, 255}    // 青色
        };
        
        // 使用正确的函数调用，设置第一个LED的颜色
        ret = ws2812_set_led_color(0, colors[color_index % 6]);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to set WS2812 LED color: %s", esp_err_to_name(ret));
        } else {
            // 修复格式化字符串，使用正确的格式说明符
            rgb_color_t current_color = colors[color_index % 6];
            ESP_LOGI(TAG, "WS2812 LED color set to R:%d G:%d B:%d", 
                     current_color.r, current_color.g, current_color.b);
        }
        
        color_index++;

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

// 保留app_main函数
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
    }

    // 创建互斥锁
    cpu_usage_mutex = xSemaphoreCreateMutex();
    if (cpu_usage_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create CPU usage mutex");
        return;
    }

    // 创建core0任务（运行在Core 0）
    if (xTaskCreatePinnedToCore(runtime_core0, "runtime_core0", 8192, NULL, 5, NULL, 0) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create runtime_core0 task");
        return;
    }

    // 创建core1任务（运行在Core 1）
    if (xTaskCreatePinnedToCore(runtime_core1, "runtime_core1", 8192, NULL, 5, NULL, 1) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create runtime_core1 task");
        return;
    }

    ESP_LOGI(TAG, "All tasks created successfully");
}