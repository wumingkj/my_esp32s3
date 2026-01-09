#include "key_manager.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/gpio.h"

#define TAG "KeyManager"

// 按键状态结构体
typedef struct {
    gpio_num_t pin;                 // GPIO引脚
    bool active_low;                // 是否低电平有效
    uint32_t debounce_time;         // 去抖动时间
    uint32_t long_press_time;       // 长按时间
    uint32_t repeat_time;           // 重复按键时间
    bool enable_double_click;       // 是否启用双击检测
    uint32_t double_click_time;     // 双击时间间隔
    
    bool current_state;             // 当前状态
    bool last_state;                // 上次状态
    bool stable_state;              // 稳定状态
    uint32_t last_change_time;      // 上次状态变化时间
    uint32_t press_start_time;      // 按下开始时间
    bool is_pressed;                // 是否按下
    bool long_press_detected;       // 长按已检测
    uint32_t last_repeat_time;      // 上次重复时间
    uint32_t click_count;           // 点击计数
    uint32_t last_click_time;       // 上次点击时间
    bool waiting_for_double_click;  // 等待双击检测
    bool enabled;                   // 按键使能状态
} key_state_t;

// 按键管理器结构体
typedef struct {
    key_state_t* keys;              // 按键状态数组
    uint32_t key_count;             // 按键数量
    uint32_t max_keys;              // 最大按键数量
    QueueHandle_t event_queue;      // 事件队列
    key_event_callback_t callback;  // 事件回调
    void* user_data;                // 用户数据
    TaskHandle_t task_handle;       // 任务句柄
    bool running;                   // 运行状态
} keymanager_t;

static esp_err_t keymanager_process_key(keymanager_t* manager, key_state_t* key);
static void keymanager_send_event(keymanager_t* manager, key_event_t event);

esp_err_t keymanager_init(keymanager_handle_t* handle) {
    if (handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    keymanager_t* manager = calloc(1, sizeof(keymanager_t));
    if (manager == NULL) {
        return ESP_ERR_NO_MEM;
    }
    
    // 初始化事件队列
    manager->event_queue = xQueueCreate(20, sizeof(key_event_t));
    if (manager->event_queue == NULL) {
        free(manager);
        return ESP_ERR_NO_MEM;
    }
    
    // 初始化按键数组
    manager->max_keys = 10;
    manager->keys = calloc(manager->max_keys, sizeof(key_state_t));
    if (manager->keys == NULL) {
        vQueueDelete(manager->event_queue);
        free(manager);
        return ESP_ERR_NO_MEM;
    }
    
    manager->running = true;
    
    // 创建按键处理任务
    BaseType_t result = xTaskCreate(keymanager_task, 
                                   "keymanager_task", 
                                   4096, 
                                   manager, 
                                   5, 
                                   &manager->task_handle);
    
    if (result != pdPASS) {
        free(manager->keys);
        vQueueDelete(manager->event_queue);
        free(manager);
        return ESP_FAIL;
    }
    
    *handle = manager;
    ESP_LOGI(TAG, "KeyManager initialized successfully");
    return ESP_OK;
}

esp_err_t keymanager_deinit(keymanager_handle_t handle) {
    if (handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    keymanager_t* manager = (keymanager_t*)handle;
    
    // 停止任务
    manager->running = false;
    if (manager->task_handle != NULL) {
        vTaskDelete(manager->task_handle);
    }
    
    // 释放资源
    if (manager->keys != NULL) {
        free(manager->keys);
    }
    if (manager->event_queue != NULL) {
        vQueueDelete(manager->event_queue);
    }
    free(manager);
    
    ESP_LOGI(TAG, "KeyManager deinitialized");
    return ESP_OK;
}

esp_err_t keymanager_add_key(keymanager_handle_t handle, const key_config_t* config) {
    if (handle == NULL || config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    keymanager_t* manager = (keymanager_t*)handle;
    
    // 检查是否已存在
    for (uint32_t i = 0; i < manager->key_count; i++) {
        if (manager->keys[i].pin == config->pin) {
            return ESP_ERR_INVALID_STATE;
        }
    }
    
    // 检查是否需要扩容
    if (manager->key_count >= manager->max_keys) {
        uint32_t new_max = manager->max_keys * 2;
        key_state_t* new_keys = realloc(manager->keys, new_max * sizeof(key_state_t));
        if (new_keys == NULL) {
            return ESP_ERR_NO_MEM;
        }
        manager->keys = new_keys;
        manager->max_keys = new_max;
    }
    
    // 配置GPIO
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << config->pin),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = config->active_low ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE,
        .pull_down_en = config->active_low ? GPIO_PULLDOWN_DISABLE : GPIO_PULLDOWN_ENABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    esp_err_t ret = gpio_config(&io_conf);
    if (ret != ESP_OK) {
        return ret;
    }
    
    // 初始化按键状态
    key_state_t* key = &manager->keys[manager->key_count];
    key->pin = config->pin;
    key->active_low = config->active_low;
    key->debounce_time = config->debounce_time;
    key->long_press_time = config->long_press_time;
    key->repeat_time = config->repeat_time;
    key->enable_double_click = config->enable_double_click;
    key->double_click_time = config->double_click_time;
    
    // 读取初始状态
    bool level = gpio_get_level(config->pin);
    key->current_state = key->active_low ? !level : level;
    key->last_state = key->current_state;
    key->stable_state = key->current_state;
    key->last_change_time = esp_timer_get_time() / 1000;
    key->click_count = 0;
    key->waiting_for_double_click = false;
    key->enabled = true;
    
    manager->key_count++;
    ESP_LOGI(TAG, "Key added on pin %d", config->pin);
    return ESP_OK;
}

esp_err_t keymanager_remove_key(keymanager_handle_t handle, gpio_num_t pin) {
    if (handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    keymanager_t* manager = (keymanager_t*)handle;
    
    for (uint32_t i = 0; i < manager->key_count; i++) {
        if (manager->keys[i].pin == pin) {
            // 移动后续元素
            for (uint32_t j = i; j < manager->key_count - 1; j++) {
                manager->keys[j] = manager->keys[j + 1];
            }
            manager->key_count--;
            ESP_LOGI(TAG, "Key removed from pin %d", pin);
            return ESP_OK;
        }
    }
    
    return ESP_ERR_NOT_FOUND;
}

esp_err_t keymanager_register_callback(keymanager_handle_t handle, 
                                      key_event_callback_t callback, 
                                      void* user_data) {
    if (handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    keymanager_t* manager = (keymanager_t*)handle;
    manager->callback = callback;
    manager->user_data = user_data;
    
    return ESP_OK;
}

QueueHandle_t keymanager_get_event_queue(keymanager_handle_t handle) {
    if (handle == NULL) {
        return NULL;
    }
    
    keymanager_t* manager = (keymanager_t*)handle;
    return manager->event_queue;
}

esp_err_t keymanager_set_enabled(keymanager_handle_t handle, gpio_num_t pin, bool enabled) {
    if (handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    keymanager_t* manager = (keymanager_t*)handle;
    
    for (uint32_t i = 0; i < manager->key_count; i++) {
        if (manager->keys[i].pin == pin) {
            manager->keys[i].enabled = enabled;
            return ESP_OK;
        }
    }
    
    return ESP_ERR_NOT_FOUND;
}

esp_err_t keymanager_get_state(keymanager_handle_t handle, gpio_num_t pin, bool* state) {
    if (handle == NULL || state == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    keymanager_t* manager = (keymanager_t*)handle;
    
    for (uint32_t i = 0; i < manager->key_count; i++) {
        if (manager->keys[i].pin == pin) {
            *state = manager->keys[i].stable_state;
            return ESP_OK;
        }
    }
    
    return ESP_ERR_NOT_FOUND;
}

void keymanager_task(void* pvParameters) {
    keymanager_t* manager = (keymanager_t*)pvParameters;
    
    while (manager->running) {
        uint32_t current_time = esp_timer_get_time() / 1000; // 转换为毫秒
        
        // 处理所有按键
        for (uint32_t i = 0; i < manager->key_count; i++) {
            if (manager->keys[i].enabled) {
                keymanager_process_key(manager, &manager->keys[i]);
            }
        }
        
        vTaskDelay(pdMS_TO_TICKS(10)); // 10ms扫描间隔
    }
    
    vTaskDelete(NULL);
}

static esp_err_t keymanager_process_key(keymanager_t* manager, key_state_t* key) {
    uint32_t current_time = esp_timer_get_time() / 1000;
    
    // 读取当前电平状态
    bool level = gpio_get_level(key->pin);
    key->current_state = key->active_low ? !level : level;
    
    // 状态变化检测
    if (key->current_state != key->last_state) {
        key->last_change_time = current_time;
        key->last_state = key->current_state;
    }
    
    // 去抖动处理
    if ((current_time - key->last_change_time) >= key->debounce_time) {
        if (key->current_state != key->stable_state) {
            key->stable_state = key->current_state;
            
            // 生成按下/释放事件
            key_event_t event = {
                .pin = key->pin,
                .timestamp = current_time,
                .duration = 0
            };
            
            if (key->stable_state) {
                // 按键按下
                event.type = KEY_EVENT_PRESSED;
                key->press_start_time = current_time;
                key->is_pressed = true;
                key->long_press_detected = false;
                key->last_repeat_time = current_time;
            } else {
                // 按键释放
                event.type = KEY_EVENT_RELEASED;
                uint32_t press_duration = current_time - key->press_start_time;
                event.duration = press_duration;
                key->is_pressed = false;
                
                // 判断事件类型
                if (press_duration < key->long_press_time) {
                    // 短按 - 记录点击时间
                    key->last_click_time = current_time;
                    
                    // 如果启用双击检测，等待可能的第二次点击
                    if (key->enable_double_click) {
                        // 增加点击计数
                        key->click_count++;
                        
                        // 设置双击检测标志
                        key->waiting_for_double_click = true;
                        
                        // 如果是第一次点击，不立即发送单击事件
                        if (key->click_count == 1) {
                            // 等待可能的第二次点击
                        } else if (key->click_count == 2) {
                            // 检测到双击，立即发送双击事件
                            key_event_t double_click_event = {
                                .pin = key->pin,
                                .type = KEY_EVENT_DOUBLE_CLICK,
                                .duration = press_duration,
                                .timestamp = current_time
                            };
                            keymanager_send_event(manager, double_click_event);
                            key->click_count = 0;
                            key->waiting_for_double_click = false;
                        }
                    } else {
                        // 直接发送单击事件
                        key_event_t click_event = {
                            .pin = key->pin,
                            .type = KEY_EVENT_SINGLE_CLICK,
                            .duration = press_duration,
                            .timestamp = current_time
                        };
                        keymanager_send_event(manager, click_event);
                    }
                } else {
                    // 长按事件 - 在释放时触发
                    key_event_t long_press_event = {
                        .pin = key->pin,
                        .type = KEY_EVENT_LONG_PRESS,
                        .duration = press_duration,
                        .timestamp = current_time
                    };
                    keymanager_send_event(manager, long_press_event);
                    key->click_count = 0; // 长按后重置点击计数
                    key->waiting_for_double_click = false;
                }
            }
            
            keymanager_send_event(manager, event);
        }
    }
    
    // 处理双击检测超时
    if (key->enable_double_click && key->waiting_for_double_click) {
        if ((current_time - key->last_click_time) > key->double_click_time) {
            // 双击超时 - 触发相应的单击事件
            for (uint32_t i = 0; i < key->click_count; i++) {
                key_event_t click_event = {
                    .pin = key->pin,
                    .type = KEY_EVENT_SINGLE_CLICK,
                    .duration = 0,
                    .timestamp = current_time
                };
                keymanager_send_event(manager, click_event);
            }
            key->click_count = 0;
            key->waiting_for_double_click = false;
        }
    }
    
    // 保持按下检测（仅用于实时状态，不触发长按）
    if (key->is_pressed) {
        key_event_t event = {
            .pin = key->pin,
            .type = KEY_EVENT_HOLD,
            .duration = current_time - key->press_start_time,
            .timestamp = current_time
        };
        keymanager_send_event(manager, event);
        
        // 重复按键检测
        if (key->repeat_time > 0 && (current_time - key->last_repeat_time) >= key->repeat_time) {
            key_event_t repeat_event = {
                .pin = key->pin,
                .type = KEY_EVENT_REPEAT,
                .duration = current_time - key->press_start_time,
                .timestamp = current_time
            };
            keymanager_send_event(manager, repeat_event);
            key->last_repeat_time = current_time;
        }
    }
    
    return ESP_OK;
}

static void keymanager_send_event(keymanager_t* manager, key_event_t event) {
    // 发送到事件队列
    if (manager->event_queue != NULL) {
        xQueueSend(manager->event_queue, &event, 0);
    }
    
    // 调用回调函数
    if (manager->callback != NULL) {
        manager->callback(event, manager->user_data);
    }
}