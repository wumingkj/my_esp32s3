/*
 * SPDX-FileCopyrightText: 2010-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#include <stdio.h>
#include <inttypes.h>
#include <dirent.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_chip_info.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_task_wdt.h"  // 添加看门狗头文件
#include "partition_manager.h"
#include "frequency_manager.h"
#include "wifi_manager.h"
#include "littlefs_manager.h"  // 使用重构后的文件系统管理器
#include "session_manager.h"
#include "user_manager.h"
#include "servo_control.h"  // 添加舵机控制头文件
#include "ws2812_led.h"     // 添加WS2812 LED控制头文件

static const char *TAG = "Main";

// 频率管理器配置
static frequency_manager_config_t freq_config = {
    .current_mode = FREQ_MODE_BALANCED,
    .performance_freq = 240,
    .balanced_freq = 160,
    .power_save_freq = 80,
    .custom_freq = 240
};

// WiFi配置 - 使用新的类型名（简化，只保留AP相关配置）
static wifi_manager_config_t wifi_config = {
    .ap_ssid = "ESP32-S3-AP",
    .ap_password = "12345678",
    .enable_dhcp_server = true
};

// WiFi管理任务
static void wifi_manager_task(void *pvParameters) {
    ESP_LOGI(TAG, "WiFi manager task started");
    
    // 初始化WiFi管理器
    esp_err_t ret = wifi_manager_init(&wifi_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WiFi manager initialization failed: %s", esp_err_to_name(ret));
        vTaskDelete(NULL);
        return;
    }
    
    // 启动AP模式（使用默认AP名称）
    ret = wifi_manager_start_ap("ap_map", wifi_config.ap_ssid, wifi_config.ap_password);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start AP: %s", esp_err_to_name(ret));
    }
    
    // 启动Web服务器
    ret = wifi_manager_start_web_server();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start web server: %s", esp_err_to_name(ret));
    }
    
    // 获取并显示网络信息
    network_info_t net_info;
    wifi_manager_get_network_info(&net_info);
    
    ESP_LOGI(TAG, "Network Information:");
    ESP_LOGI(TAG, "  AP IP: %s", net_info.ap_ip);
    ESP_LOGI(TAG, "  Netmask: %s", net_info.netmask);
    
    // 主循环 - 监控网络状态
    while (1) {
        // 每30秒扫描一次连接的设备
        static uint32_t last_scan_time = 0;
        uint32_t current_time = xTaskGetTickCount() * portTICK_PERIOD_MS / 1000;
        
        if (current_time - last_scan_time >= 30) {
            wifi_manager_scan_connected_devices("ap_map");
            last_scan_time = current_time;
            ESP_LOGI(TAG, "Device scan completed");
        }
        
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
    
    vTaskDelete(NULL);
}

// 系统初始化任务
static void system_init_task(void *arg) {
    ESP_LOGI(TAG, "System initialization started");
    
    // 初始化文件系统
    esp_err_t ret = littlefs_manager_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Filesystem initialization failed: %s", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "Filesystem initialized successfully");
    
        // 详细列出LittleFS根目录下的所有文件和文件夹（类似ls -lh）
        ESP_LOGI(TAG, "=== LittleFS文件系统详细内容 ===");
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
    
    // 显示当前系统信息
    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);
    
    ESP_LOGI(TAG, "Chip information:");
    ESP_LOGI(TAG, "  Model: %s", 
             (chip_info.model == CHIP_ESP32S3) ? "ESP32-S3" : "Unknown");
    ESP_LOGI(TAG, "  Cores: %d", chip_info.cores);
    ESP_LOGI(TAG, "  Features: %s%s%s%s", 
             (chip_info.features & CHIP_FEATURE_WIFI_BGN) ? "WiFi " : "",
             (chip_info.features & CHIP_FEATURE_BLE) ? "BLE " : "",
             (chip_info.features & CHIP_FEATURE_BT) ? "BT " : "",
             (chip_info.features & CHIP_FEATURE_EMB_FLASH) ? "Embedded-Flash" : "External-Flash");
    
    ESP_LOGI(TAG, "System initialization completed");
    
    // 创建WiFi管理任务
    xTaskCreate(wifi_manager_task, "wifi_manager", 8192, NULL, 4, NULL);
    
    vTaskDelete(NULL);
}

// WS2812自定义配置测试任务
static void ws2812_custom_test_task(void *pvParameters) {
    ws2812_config_t custom_config = {
        .pin = 48,
        .num_leds = 1
    };
    
    while (1) {
        // 每次循环开始时初始化WS2812
        esp_err_t ret = ws2812_led_init(&custom_config);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "WS2812 initialization failed: %s", esp_err_to_name(ret));
            vTaskDelay(pdMS_TO_TICKS(5000)); // 等待5秒后重试
            continue;
        }
        
        ESP_LOGI(TAG, "=== 自定义WS2812测试开始 ===");
        
        ESP_LOGI(TAG, "阶段1: 单色循环测试");
        
        // 简化的单色测试，只测试几种基本颜色
        ret = ws2812_set_all_color((rgb_color_t){255, 0, 0}); // 红色
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "设置红色失败: %s", esp_err_to_name(ret));
            goto cleanup;
        }
        vTaskDelay(pdMS_TO_TICKS(2000));
        
        ret = ws2812_set_all_color((rgb_color_t){0, 255, 0}); // 绿色
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "设置绿色失败: %s", esp_err_to_name(ret));
            goto cleanup;
        }
        vTaskDelay(pdMS_TO_TICKS(2000));
        
        ret = ws2812_set_all_color((rgb_color_t){0, 0, 255}); // 蓝色
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "设置蓝色失败: %s", esp_err_to_name(ret));
            goto cleanup;
        }
        vTaskDelay(pdMS_TO_TICKS(2000));
        
        ESP_LOGI(TAG, "阶段2: 呼吸灯效果测试");
        ret = ws2812_breathing_effect((rgb_color_t){255, 0, 0}, 5);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "呼吸灯效果失败: %s", esp_err_to_name(ret));
            goto cleanup;
        }
        vTaskDelay(pdMS_TO_TICKS(3000)); // 缩短测试时间
        
        ESP_LOGI(TAG, "阶段4: 关闭LED");
        ret = ws2812_clear_all();
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "关闭LED失败: %s", esp_err_to_name(ret));
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
        
cleanup:
        // 每次循环结束时清理WS2812资源
        ws2812_led_deinit();
        
        ESP_LOGI(TAG, "=== 自定义WS2812测试完成，开始下一轮 ===");
        vTaskDelay(pdMS_TO_TICKS(2000)); // 等待2秒后开始下一轮
    }
}

void app_main(void) {
    ESP_LOGI(TAG, "ESP32-S3 application started");
    
    // 统一初始化NVS（只调用一次）
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
        
        // 设置频率模式
        ret = frequency_manager_set_mode(FREQ_MODE_PERFORMANCE);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to set frequency mode: %s", esp_err_to_name(ret));
        } else {
            ESP_LOGI(TAG, "Frequency mode set to PERFORMANCE");
        }
    }
    
    // 初始化舵机控制
    ret = servo_control_init(NULL);  // 使用默认配置
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Servo control initialization failed: %s", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "Servo control initialized successfully");
        
        // 运行舵机测试
        ret = servo_control_test();
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Servo control test failed: %s", esp_err_to_name(ret));
        } else {
            ESP_LOGI(TAG, "Servo control test completed");
        }
    }
    
    // 创建WS2812自定义配置测试任务
    xTaskCreate(ws2812_custom_test_task, "ws2812_custom", 4096, NULL, 3, NULL);
    
    // 创建系统初始化任务
    xTaskCreate(system_init_task, "system_init", 4096, NULL, 5, NULL);
    
    // 等待系统初始化完成
    vTaskDelay(pdMS_TO_TICKS(2000));

    // 初始化设备映射表
    ret = device_mapping_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize device mapping: %s", esp_err_to_name(ret));
        return;
    }
    ESP_LOGI(TAG, "Device mapping initialized successfully");
    
    // 设备映射相关变量
    static uint32_t last_device_check_time = 0;
    
    // 舵机雷达扫描相关变量 - 缓急缓效果
    static int current_angle = 90;           // 当前角度
    static int direction = 1;               // 扫描方向：1表示增加，-1表示减少
    static uint32_t last_servo_move_time = 0; // 上次舵机移动时间
    static float current_speed = 0.0;       // 当前速度（度/次）
    static float acceleration = 0.1;        // 加速度（度/次²）
    
    // 速度参数
    const float max_speed = 1.0;            // 最大速度（度/次）
    const float min_speed = 0.1;            // 最小速度（度/次）
    const uint32_t base_interval = 25;      // 基础移动间隔(毫秒)
    
    while (true)
    {
        // 主循环 - 保持系统运行
        uint32_t current_time = xTaskGetTickCount() * portTICK_PERIOD_MS;

        // 舵机雷达扫描逻辑 - 缓急缓效果
        if (current_time - last_servo_move_time > base_interval) {
            // 计算目标速度（基于当前位置的缓急缓曲线）
            float target_speed;
            
            if (direction == 1) {  // 向右扫描（0度 → 180度）
                if (current_angle <= 90) {
                    // 0-90度：加速阶段
                    target_speed = min_speed + (max_speed - min_speed) * (current_angle / 90.0);
                } else {
                    // 90-180度：减速阶段
                    target_speed = max_speed - (max_speed - min_speed) * ((current_angle - 90) / 90.0);
                }
            } else {  // 向左扫描（180度 → 0度）
                if (current_angle >= 90) {
                    // 180-90度：加速阶段
                    target_speed = min_speed + (max_speed - min_speed) * ((180 - current_angle) / 90.0);
                } else {
                    // 90-0度：减速阶段
                    target_speed = max_speed - (max_speed - min_speed) * ((90 - current_angle) / 90.0);
                }
            }
            
            // 平滑调整当前速度
            if (current_speed < target_speed) {
                current_speed += acceleration;
                if (current_speed > target_speed) current_speed = target_speed;
            } else if (current_speed > target_speed) {
                current_speed -= acceleration;
                if (current_speed < target_speed) current_speed = target_speed;
            }
            
            // 确保速度在合理范围内
            if (current_speed < min_speed) current_speed = min_speed;
            if (current_speed > max_speed) current_speed = max_speed;
            
            // 计算角度增量（整数部分和小数部分）
            int angle_increment = (int)current_speed;
            float fractional_part = current_speed - angle_increment;
            
            // 处理小数部分（概率性增加1度）
            if ((float)rand() / RAND_MAX < fractional_part) {
                angle_increment += 1;
            }
            
            // 更新舵机角度
            current_angle += direction * angle_increment;
            
            // 边界检查和方向反转
            if (current_angle >= 180) {
                current_angle = 180;
                direction = -1;  // 改变方向为向左
                current_speed = min_speed;  // 重置速度
                //ESP_LOGI(TAG, "舵机扫描到达右边界180度，开始向左扫描");
            } else if (current_angle <= 0) {
                current_angle = 0;
                direction = 1;   // 改变方向为向右
                current_speed = min_speed;  // 重置速度
                //ESP_LOGI(TAG, "舵机扫描到达左边界0度，开始向右扫描");
            }
            
            // 设置舵机角度
            ret = servo_control_set_angle(current_angle);
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "Failed to set servo angle: %s", esp_err_to_name(ret));
            } else {
                // 调试信息（可选）
                // ESP_LOGI(TAG, "舵机角度: %d度, 速度: %.2f度/次", current_angle, current_speed);
            }
            
            last_servo_move_time = current_time;
        }

        // 每60秒检查一次设备状态
        if (current_time - last_device_check_time > 60000) {
            // 刷新设备状态（标记30秒内不活跃的设备）
            device_mapping_refresh_status(30);
            
            // 显示当前设备映射表
            int device_count = device_mapping_get_count();
            if (device_count > 0) {
                ESP_LOGI(TAG, "=== Device Mapping Table (%d devices) ===", device_count);
                
                device_mapping_t** devices = device_mapping_get_all_devices(NULL);
                for (int i = 0; i < device_count; i++) {
                    ESP_LOGI(TAG, "  %d. Hostname: %s, IP: %s, MAC: %s, Active: %s", 
                        i + 1, 
                        devices[i]->hostname,
                        devices[i]->ip,
                        devices[i]->mac,
                        devices[i]->is_active ? "Yes" : "No");
                }
            } else {
                ESP_LOGI(TAG, "No devices in mapping table");
            }
            
            last_device_check_time = current_time;
        }

        vTaskDelay(pdMS_TO_TICKS(10)); // 保持系统响应性
    }
}