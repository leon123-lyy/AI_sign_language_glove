/**
 * @file espnow_sender_wrapper.cpp
 * @brief ESP-NOW 发送模块 C++ 包装（用于与 C++ 项目集成）
 */

extern "C" {
#include "espnow_sender.h"
#include "ble_notify.h"
#include "inference.h"
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
#define WIFI_SSID     "000"     // 替换为你的WiFi名称
#define WIFI_PASSWORD "123456789" // 替换为你的WiFi密码
#define MAX_RETRY     5

/*===========================================================================
 * 接收端 MAC 地址配置（占位符，请替换为实际接收端MAC地址）
 *===========================================================================*/
static uint8_t s_receiver_mac[] = {0xA0, 0xB7, 0x65, 0x2D, 0x42, 0x00};

/*===========================================================================
 * GPIO40 按钮配置
 *===========================================================================*/
#define ESPNOW_BUTTON_PIN GPIO_NUM_40

/** BLE通知标签带前缀 */
#define ESPNOW_BLE_PREFIX "[result] "

/** 是否正在处理推理（避免按钮长按/抖动导致重复触发） */
static volatile bool s_is_processing = false;

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
        ESP_LOGD(TAG, "发送成功");
    } else {
        ESP_LOGW(TAG, "发送失败");
    }
}

/*===========================================================================
 * 公共接口
 *===========================================================================*/

static bool s_log_suppressed = false;

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
        /* 首次断开时禁用 WiFi 底层调试日志，避免重复刷屏 */
        if (!s_log_suppressed) {
            esp_log_level_set("wifi", ESP_LOG_ERROR);
            s_log_suppressed = true;
        }
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        /* 仅在首次获取IP时打印 */
        static bool got_ip_once = false;
        if (!got_ip_once) {
            ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
            ESP_LOGI(TAG, "WiFi connected, IP: " IPSTR, IP2STR(&event->ip_info.ip));
            got_ip_once = true;
        }
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

/**
 * @brief 通过 ESP-NOW 发送推理结果
 * @param label 识别标签
 * @param confidence 置信度 (0.0 ~ 1.0)
 * @return ESP_OK 表示成功
 */
esp_err_t espnow_send_result(const char *label, float confidence)
{
    if (!label) {
        return ESP_ERR_INVALID_ARG;
    }

    espnow_message_t msg = {0};
    snprintf(msg.text, sizeof(msg.text), "%s", label);

    esp_err_t send_ret = esp_now_send(s_receiver_mac, (uint8_t *)&msg, sizeof(msg));
    if (send_ret == ESP_OK) {
        printf("[ESP-NOW] 已发送推理结果: \"%s\"\n", msg.text);
    } else {
        printf("[ESP-NOW] 发送失败: \"%s\" (错误: %s)\n", msg.text, esp_err_to_name(send_ret));
    }

    return send_ret;
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
    ESP_LOGI(TAG, "行为: 按下按钮 -> 等待 2 秒 -> 运行手势推理 -> 通过 ESP-NOW 发送结果");

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
                /* 避免与上一次推理重叠 */
                if (s_is_processing) {
                    ESP_LOGW(TAG, "正在处理上一次推理，按下忽略");
                } else {
                    s_is_processing = true;

                    /* 延迟2秒后开始采集（给用户准备手势的时间） */
                    printf("[ESP-NOW] 按下按钮，2秒后开始采集手势数据...\n");
                    vTaskDelay(pdMS_TO_TICKS(2000));

                    /* 执行一次手势推理（内部完成 100 帧采集 + 模型推理） */
                    inference_result_t result = {
                        .label = NULL,
                        .confidence = 0.0f,
                        .label_index = -1,
                    };
                    esp_err_t run_ret = inference_run(&result);

                    if (run_ret == ESP_OK && result.label != NULL) {
                        espnow_message_t msg = {0};
                        snprintf(msg.text, sizeof(msg.text), "%s", result.label);

                        /* 发送 ESP-NOW 消息 */
                        esp_err_t send_ret = esp_now_send(s_receiver_mac, (uint8_t *)&msg, sizeof(msg));
                        if (send_ret == ESP_OK) {
                            printf("[ESP-NOW] 已发送推理结果: \"%s\"\n", msg.text);

                            /* 通过BLE通知同时推送到手机，格式与 button.cpp 保持一致 */
                            char ble_msg[64];
                            snprintf(ble_msg, sizeof(ble_msg), ESPNOW_BLE_PREFIX "%s (%.1f%%)",
                                     result.label, result.confidence * 100.0f);
                            ble_notify_send(ble_msg);
                        } else {
                            printf("[ESP-NOW] 发送失败: \"%s\" (错误: %s)\n",
                                   msg.text, esp_err_to_name(send_ret));
                        }
                    } else {
                        printf("[ESP-NOW] 推理失败，未发送结果\n");
                    }

                    s_is_processing = false;
                }
            }
        }

        last_state = current_state;
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

} // extern "C"