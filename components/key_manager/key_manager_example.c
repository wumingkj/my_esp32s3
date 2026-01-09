#include "esp_log.h"
#include "keymanager.h"

static const char* TAG = "KeyManagerExample";

// 按键事件回调函数
static void key_event_handler(key_event_t event, void* user_data) {
    const char* event_name = "";

    switch (event.type) {
        case KEY_EVENT_PRESSED:
            event_name = "PRESSED";
            break;
        case KEY_EVENT_RELEASED:
            event_name = "RELEASED";
            break;
        case KEY_EVENT_SINGLE_CLICK:
            event_name = "SINGLE_CLICK";
            break;
        case KEY_EVENT_DOUBLE_CLICK:
            event_name = "DOUBLE_CLICK";
            break;
        case KEY_EVENT_LONG_PRESS:
            event_name = "LONG_PRESS";
            break;
        case KEY_EVENT_HOLD:
            event_name = "HOLD";
            break;
        case KEY_EVENT_REPEAT:
            event_name = "REPEAT";
            break;
        default:
            event_name = "UNKNOWN";
            break;
    }

    ESP_LOGI(TAG, "Key event: pin=%d, type=%s, duration=%lums", event.pin, event_name, event.duration);
}

void keymanager_example(void) {
    keymanager_handle_t keymanager;
    esp_err_t ret;

    // 初始化按键管理器
    ret = keymanager_init(&keymanager);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize keymanager: %s", esp_err_to_name(ret));
        return;
    }

    // 注册事件回调
    keymanager_register_callback(keymanager, key_event_handler, NULL);

    // 配置按键（示例：GPIO0，低电平有效）
    key_config_t key_config = {
        .pin = GPIO_NUM_0,
        .active_low = true,           // 低电平有效
        .debounce_time = 20,          // 20ms去抖动
        .long_press_time = 1000,      // 1秒长按
        .repeat_time = 200,           // 200ms重复间隔
        .enable_double_click = true,  // 启用双击检测
        .double_click_time = 500      // 500ms双击间隔
    };

    // 添加按键
    ret = keymanager_add_key(keymanager, &key_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add key: %s", esp_err_to_name(ret));
        keymanager_deinit(keymanager);
        return;
    }

    ESP_LOGI(TAG, "KeyManager example started. Press GPIO0 button to test.");

    // 也可以使用事件队列方式处理
    QueueHandle_t event_queue = keymanager_get_event_queue(keymanager);
    key_event_t event;

    while (1) {
        if (xQueueReceive(event_queue, &event, portMAX_DELAY) == pdTRUE) {
            // 这里可以处理队列中的事件
            // 如果已经注册了回调，这里可以不需要额外处理
        }
    }

    // 清理资源（通常不会执行到这里）
    keymanager_deinit(keymanager);
}