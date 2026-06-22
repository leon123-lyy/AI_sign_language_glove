/**
 * @file espnow_sender_wrapper.cpp
 * @brief ESP-NOW 发送模块 C++ 包装（用于与 C++ 项目集成）
 */

extern "C" {
#include "espnow_sender.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_now.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
}

static const char *TAG = "ESPNOW";

/*===========================================================================
 * WiFi配置（连接到同一WiFi网络以确保通信信道一致）
 *===========================================================================*/
#define WIFI_SSID     "OKO"     // 替换为你的WiFi名称
#define WIFI_PASSWORD "1234567890" // 替换为你的WiFi密码
#define MAX_RETRY     5

/*===========================================================================
 * 接收端 MAC 地址配置（占位符，请替换为实际接收端MAC地址）
 *===========================================================================*/
static uint8_t s_receiver_mac[] = {0xA0, 0xB7, 0x65, 0x2D, 0x42, 0x00};

/*===========================================================================
 * 手势标签循环发送列表
 *===========================================================================*/
static const char *s_labels[] = {"this", "our", "stuff", "bye"};
static const int s_label_count = sizeof(s_labels) / sizeof(s_labels[0]);
static int s_label_index = 0;

/*===========================================================================
 * GPIO40 按钮配置
 *===========================================================================*/
#define ESPNOW_BUTTON_PIN GPIO_NUM_40

/* WiFi断开提示标志（只在启动时显示一次） */
static bool s_wifi_disconnect_first = true;

/*===========================================================================
 * ESP-NOW 发送回调（C 链接）
 *===========================================================================*/

extern "C" {

/**
 * @brief ESP-NOW 数据发送完成回调
 * @param tx_info 发送信息
 * @param status 发送状态
 */
static void IRAM_ATTR espnow_send_cb(const wifi_tx_info_t *tx_info, esp_now_send_status_t status)
{
    if (status == ESP_NOW_SEND_SUCCESS) {
        ESP_LOGI(TAG, "发送成功");
    } else {
        ESP_LOGW(TAG, "发送失败");
    }
}

/*===========================================================================
 * 公共接口
 *===========================================================================*/

/**
 * @brief WiFi事件处理函数
 * @param arg 用户参数
 * @param event_base 事件基础
 * @param event_id 事件ID
 * @param event_data 事件数据
 */
static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_wifi_disconnect_first) {
            ESP_LOGI(TAG, "WiFi断开，尝试重新连接...");
            s_wifi_disconnect_first = false;
        }
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "WiFi连接成功，IP: " IPSTR, IP2STR(&event->ip_info.ip));
    }
}

/**
 * @brief 连接WiFi网络
 * @return ESP_OK 表示成功
 */
static esp_err_t wifi_connect(void)
{
    ESP_LOGI(TAG, "连接WiFi: %s", WIFI_SSID);

    /* 注册WiFi事件 */
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL));

    /* 配置WiFi连接参数 */
    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASSWORD,
            .threshold = {
                .authmode = WIFI_AUTH_WPA2_PSK,
            },
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));

    /* 启动WiFi连接 */
    ESP_ERROR_CHECK(esp_wifi_start());

    return ESP_OK;
}

esp_err_t espnow_sender_init(void)
{
    ESP_LOGI(TAG, "初始化 ESP-NOW 发送模块...");

    /* 初始化 TCP/IP 协议栈（WiFi依赖） */
    ESP_ERROR_CHECK(esp_netif_init());

    /* 创建默认事件循环 */
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    /* 创建默认 WiFi STA 网络接口 */
    esp_netif_create_default_wifi_sta();

    /* 初始化 WiFi（STA模式） */
    wifi_init_config_t wifi_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&wifi_cfg));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));

    /* 连接WiFi网络 */
    ESP_ERROR_CHECK(wifi_connect());

    /* 等待WiFi连接（最多5秒） */
    ESP_LOGI(TAG, "等待WiFi连接...");
    vTaskDelay(pdMS_TO_TICKS(5000));

    /* 初始化 ESP-NOW */
    ESP_ERROR_CHECK(esp_now_init());
    ESP_ERROR_CHECK(esp_now_register_send_cb(espnow_send_cb));

    /* 注册接收端（配对） */
    esp_now_peer_info_t peer_info = {};
    memcpy(peer_info.peer_addr, s_receiver_mac, 6);
    peer_info.channel = 0;
    peer_info.ifidx = WIFI_IF_STA;
    peer_info.encrypt = false;

    esp_err_t ret = esp_now_add_peer(&peer_info);
    if (ret == ESP_ERR_ESPNOW_EXIST) {
        ESP_LOGW(TAG, "接收端已注册，跳过");
    } else if (ret != ESP_OK) {
        ESP_LOGE(TAG, "注册接收端失败: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "ESP-NOW 初始化完成");
    ESP_LOGI(TAG, "接收端 MAC: " MACSTR, MAC2STR(s_receiver_mac));
    ESP_LOGI(TAG, "按钮引脚: GPIO%d", ESPNOW_BUTTON_PIN);
    ESP_LOGI(TAG, "标签列表: this -> our -> stuff -> bye (循环)");

    return ESP_OK;
}

void espnow_sender_task(void *arg)
{
    ESP_LOGI(TAG, "ESP-NOW 发送任务启动");

    /* 配置 GPIO40 为输入，启用内部上拉 */
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << ESPNOW_BUTTON_PIN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);

    bool last_state = true;

    while (1) {
        bool current_state = gpio_get_level(ESPNOW_BUTTON_PIN);

        /* 下降沿检测（按下：高→低） */
        if (last_state == true && current_state == false) {
            /* 去抖动：等待50ms后再次确认 */
            vTaskDelay(pdMS_TO_TICKS(50));

            if (gpio_get_level(ESPNOW_BUTTON_PIN) == 0) {
                /* 延迟2秒后发送 */
                vTaskDelay(pdMS_TO_TICKS(2000));

                /* 获取当前要发送的标签 */
                const char *label = s_labels[s_label_index];

                /* 构造消息 */
                espnow_message_t msg = {0};
                strncpy(msg.text, label, sizeof(msg.text) - 1);

                /* 发送 ESP-NOW 消息 */
                esp_err_t ret = esp_now_send(s_receiver_mac, (uint8_t *)&msg, sizeof(msg));
                if (ret == ESP_OK) {
                    printf("[ESP-NOW] 已发送: \"%s\" (第%d次)\n", label, s_label_index + 1);
                } else {
                    printf("[ESP-NOW] 发送失败: \"%s\" (错误: %s)\n", label, esp_err_to_name(ret));
                }

                /* 切换到下一个标签（循环） */
                s_label_index = (s_label_index + 1) % s_label_count;
            }
        }

        last_state = current_state;
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

} // extern "C"