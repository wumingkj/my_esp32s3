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
#include "tft_display.h"  // 添加TFT显示屏头文件
#include "key_manager.h"
#include "device_mapping.h"  // 添加设备映射表头文件

static const char *TAG = "Main";

// 全局CPU使用率数组 [core0, core1]
static float cpu_usage[2] = {0.0f, 0.0f};

// 全局按键管理器句柄
static keymanager_handle_t g_keymanager_handle = NULL;


#define PERF_COUNTER_CORE0 0                            // 性能计数器0监控Core0
#define PERF_COUNTER_CORE1 1                            // 性能计数器1监控Core1
uint32_t perf_counter_value_core0 = 0;
uint32_t perf_counter_value_core1 = 0;
static const uint16_t select_value = XTPERF_CNT_CYCLES; // 关键：监控CPU时钟周期总数
static const uint16_t mask_value = XTPERF_MASK_CYCLES;  // 掩码值
static const int kernelcnt_value = 0;                   // 内核计数（改为int类型）
static const int tracelevel_value = 0;                  // 跟踪级别（改为int类型）

TaskHandle_t runtime_0_task_handle = NULL;
TaskHandle_t runtime_1_task_handle = NULL;

uint32_t millis()
{
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
    .max_pulsewidth = SERVO_MAX_PULSEWIDTH};

// 频率管理器配置
static frequency_manager_config_t freq_config = {
    .current_mode = FREQ_MODE_PERFORMANCE, // FREQ_MODE_BALANCED
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


// 按键事件回调函数
static void key_event_callback(key_event_t event, void* user_data)
{
    const char* event_name = "";
    switch (event.type) {
        case KEY_EVENT_PRESSED:
            event_name = "按下";break;
        case KEY_EVENT_RELEASED:
            event_name = "释放";break;
        case KEY_EVENT_SINGLE_CLICK:
            event_name = "单击";break;
        case KEY_EVENT_DOUBLE_CLICK:
            event_name = "双击";break;
        case KEY_EVENT_LONG_PRESS:
            event_name = "长按";break;
        case KEY_EVENT_HOLD:
            event_name = "保持";break;
        case KEY_EVENT_REPEAT:
            event_name = "重复";break;
        default:
            event_name = "未知";break;
    }
    
    //ESP_LOGI(TAG, "GPIO%d 按键事件: %s, 持续时间: %"PRIu32"ms", event.pin, event_name, event.duration);
    
    // 根据按键事件类型执行相应操作
    switch (event.type) {
        case KEY_EVENT_SINGLE_CLICK:
            // 单击事件处理
            ESP_LOGI(TAG, "GPIO%d 单击事件触发", event.pin);
            break;
            
        case KEY_EVENT_LONG_PRESS:
            // 长按事件处理
            ESP_LOGI(TAG, "GPIO%d 长按事件触发", event.pin);
            
            //// 如果是GPIO0的长按事件，清空MAC和IP缓存表
            //if (event.pin == 0) {
            //    ESP_LOGI(TAG, "GPIO0 长按事件 - 清空MAC和IP缓存表");
            //    
            //    // 清空设备映射表（MAC和IP缓存表）
            //    esp_err_t ret = device_mapping_clear_all();
            //    if (ret == ESP_OK) {
            //        ESP_LOGI(TAG, "MAC和IP缓存表已清空");
            //    } else {
            //        ESP_LOGE(TAG, "清空缓存表失败: %s", esp_err_to_name(ret));
            //    }
            //}
            break;
            
        case KEY_EVENT_DOUBLE_CLICK:
            // 双击事件处理
            ESP_LOGI(TAG, "GPIO%d 双击事件触发", event.pin);
            break;
            
        default:
            // 其他事件
            break;
    }
}



// Core 0性能监控任务
static void core0_perfmon_task(void *arg) {
    ESP_LOGI(TAG, "Core0 performance monitoring task started");
    // 初始化性能监视 - Core 0（使用与Arduino代码相同的参数类型）
    int xtensa_perfmon_result = xtensa_perfmon_init(PERF_COUNTER_CORE0, select_value, mask_value, kernelcnt_value, tracelevel_value);
    if (xtensa_perfmon_result == 0) {
        ESP_LOGI(TAG, "Core0 perfmon counter initialized successfully");
    } else {
        ESP_LOGE(TAG, "Failed to init perfmon counter for core 0: %d", xtensa_perfmon_result);
        return;
    }
    
    // 定时器变量 - 统一使用millis()
    unsigned long previousMillis_1 = millis();  // 1秒间隔
    unsigned long previousMillis_2 = millis(); // 50ms间隔（网页处理）
    const long interval_1 = 1 * 1000;     // 1秒间隔时间（毫秒）
    const long interval_2 = 50;          // 50ms间隔时间（毫秒）

    while (1) {
        unsigned long currentMillis = millis();  // 使用millis()获取当前时间
        
        // 1秒间隔：更新Core 0的CPU使用率
        if (currentMillis - previousMillis_1 >= interval_1) {
            previousMillis_1 = currentMillis;

            // 实时获取当前CPU频率（单位：MHz）
            uint32_t cpu_frequency_mhz = (uint32_t)esp_rom_get_cpu_ticks_per_us();
            uint32_t cpu_frequency_hz = cpu_frequency_mhz * 1000000;
            xtensa_perfmon_stop();    // 停止性能监控并读取计数器值
            perf_counter_value_core0 = xtensa_perfmon_value(PERF_COUNTER_CORE0); // 读取核心0的性能计数器的值
            // 使用和demo代码相同的正确计算方式
            cpu_usage[0] = (float)perf_counter_value_core0 / (float)cpu_frequency_hz * 100.0f;
            ESP_LOGI(TAG, "核心0的CPU占用率：%.2f%% (频率：%"PRIu32"MHz)", cpu_usage[0], cpu_frequency_mhz);
            xtensa_perfmon_reset(PERF_COUNTER_CORE0);    // 重置性能计数器，为下一次计算做准备
            xtensa_perfmon_start();    // 重新启动性能监控
        }
        
        // 50ms间隔：网页处理
        if (currentMillis - previousMillis_2 >= interval_2) {
            previousMillis_2 = currentMillis;
            

        }
        
        taskYIELD();
        vTaskDelay(pdMS_TO_TICKS(1)); // 1ms延迟，减少CPU占用
    }
}

// Core 1性能监控任务
static void core1_perfmon_task(void *arg) {
    ESP_LOGI(TAG, "Core1 performance monitoring task started");
    int xtensa_perfmon_result = xtensa_perfmon_init(PERF_COUNTER_CORE1, select_value, mask_value, kernelcnt_value, tracelevel_value);
    if (xtensa_perfmon_result == 0) {
        ESP_LOGI(TAG, "Core1 perfmon counter initialized successfully");
    } else {
        ESP_LOGE(TAG, "Failed to init perfmon counter for core 1: %d", xtensa_perfmon_result);
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

            // 实时获取当前CPU频率（单位：MHz）
            uint32_t cpu_frequency_mhz = (uint32_t)esp_rom_get_cpu_ticks_per_us();
            uint32_t cpu_frequency_hz = cpu_frequency_mhz * 1000000;
            xtensa_perfmon_stop();    // 停止性能监控并读取计数器值
            perf_counter_value_core1 = xtensa_perfmon_value(PERF_COUNTER_CORE1); // 读取核心1的性能计数器的值
            // 使用和demo代码相同的正确计算方式
            cpu_usage[1] = (float)perf_counter_value_core1 / (float)cpu_frequency_hz * 100.0f;     
            ESP_LOGI(TAG, "核心1的CPU占用率：%.2f%% (频率：%"PRIu32"MHz)", cpu_usage[1], cpu_frequency_mhz);
            xtensa_perfmon_reset(PERF_COUNTER_CORE1);    // 重置性能计数器，为下一次计算做准备
            xtensa_perfmon_start();    // 重新启动性能监控
        }
        
        taskYIELD();
        vTaskDelay(pdMS_TO_TICKS(1)); // 1ms延迟，减少CPU占用
    }
}

// 保留app_main函数
void app_main(void)
{
    ESP_LOGI(TAG, "ESP32-S3 dual-core performance monitoring started");

    // 初始化NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_LOGI(TAG, "NVS partition needs erase, performing erase operation...");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    ESP_LOGI(TAG, "NVS initialized successfully");

    // 正确的看门狗初始化方式（ESP-IDF 5.4.0）
    esp_task_wdt_config_t wdt_config = {
        .timeout_ms = 10 * 60 * 1000,  // 10分钟超时（600,000毫秒）
        .idle_core_mask = (1 << 0) | (1 << 1),  // 监控两个核心的IDLE任务
        .trigger_panic = false  // 超时时不触发panic（仅记录日志）
    };

    esp_err_t wdt_status = esp_task_wdt_status(NULL);
    if (wdt_status != ESP_ERR_INVALID_STATE) {
        ESP_LOGW("TWDT", "看门狗已经初始化，跳过重复初始化");
        // 可以尝试重新配置超时时间
        esp_task_wdt_reconfigure(&wdt_config);
        ESP_LOGI("TWDT", "看门狗重新配置成功，超时时间：10分钟");
    } else {
        esp_err_t err = esp_task_wdt_init(&wdt_config);
        if (err != ESP_OK) {
            ESP_LOGE("TWDT", "看门狗初始化失败: %s", esp_err_to_name(err));
        } else {
            ESP_LOGI("TWDT", "看门狗初始化成功，超时时间：10分钟");
        }
    }

    // 初始化频率管理器
    ret = frequency_manager_init(&freq_config);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Frequency manager initialization failed: %s", esp_err_to_name(ret));
    }
    else
    {
        ESP_LOGI(TAG, "Frequency manager initialized successfully");
    }

    // 初始化文件系统
    ret = littlefs_manager_init();
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Filesystem initialization failed: %s", esp_err_to_name(ret));
    }
    else
    {
        ESP_LOGI(TAG, "Filesystem initialized successfully");
        littlefs_manager_list_files_detailed("/");
    }

    // 初始化用户管理器
    ret = user_manager_init();
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "User manager initialization failed: %s", esp_err_to_name(ret));
    }
    else
    {
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
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "WiFi manager initialization failed: %s", esp_err_to_name(ret));
    }
    else
    {
        ESP_LOGI(TAG, "WiFi manager initialized successfully");
    }

        // 启动Web服务器
    esp_err_t web_server_ret = wifi_manager_start_web_server();
    if (web_server_ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start web server: %s", esp_err_to_name(web_server_ret));
    } else {
        ESP_LOGI(TAG, "Web server started successfully");
    }

    // 舵机初始化
    ret = servo_control_init(&servo_config);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Servo initialization failed: %s", esp_err_to_name(ret));
    }
    else
    {
        ESP_LOGI(TAG, "Servo initialized successfully");
    }

    // WS2812 LED初始化
    ret = ws2812_led_init(NULL);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "WS2812 LED initialization failed: %s", esp_err_to_name(ret));
    }
    else
    {
        ESP_LOGI(TAG, "WS2812 LED initialized successfully");
    }

    // TFT显示屏初始化
    ret = tft_display_init();
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "TFT display initialization failed: %s", esp_err_to_name(ret));
    }
    else
    {
        ESP_LOGI(TAG, "TFT display initialized successfully");
    }

    // 触摸屏初始化
    ret = tft_touch_init();
    if (ret != ESP_OK)
    {
        ESP_LOGW(TAG, "TFT touch initialization failed: %s", esp_err_to_name(ret));
    }
    else
    {
        ESP_LOGI(TAG, "TFT touch initialized successfully");
    }

    // 初始化按键管理器
    ret = keymanager_init(&g_keymanager_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Key manager initialization failed: %s", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "Key manager initialized successfully");
    }

    // 添加GPIO0按键到按键管理器
    key_config_t gpio0_config = {
        .pin = GPIO_NUM_0,
        .active_low = true,           // 低电平有效（按下时GPIO0为低电平）
        .debounce_time = 50,          // 去抖动时间50ms
        .long_press_time = 1000,      // 长按时间1秒
        .repeat_time = 0,             // 不启用重复按键
        .enable_double_click = true, // 启用双击检测
        .double_click_time = 300        // 双击时间间隔
    };
    
    ret = keymanager_add_key(g_keymanager_handle, &gpio0_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add GPIO0 to key manager: %s", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "GPIO0 added to key manager successfully");
    }

    // 注册按键事件回调
    ret = keymanager_register_callback(g_keymanager_handle, key_event_callback, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register key event callback: %s", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "Key event callback registered successfully");
    }

    // 创建Core 0性能监控任务（运行在Core 0）
    if (xTaskCreatePinnedToCore(core0_perfmon_task, "core0_perfmon", 4096, NULL, tskIDLE_PRIORITY + 1, &runtime_0_task_handle, 0) != pdPASS)
    {
        ESP_LOGE(TAG, "Failed to create Core0 performance monitoring task");
        return;
    }

    // 创建Core 1性能监控任务（运行在Core 1）
    if (xTaskCreatePinnedToCore(core1_perfmon_task, "core1_perfmon", 4096, NULL, tskIDLE_PRIORITY + 1, &runtime_1_task_handle, 1) != pdPASS)
    {
        ESP_LOGE(TAG, "Failed to create Core1 performance monitoring task");
        return;
    }

    ESP_LOGI(TAG, "All performance monitoring tasks created successfully");
}