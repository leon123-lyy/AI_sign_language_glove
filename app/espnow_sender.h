/**
 * @file espnow_sender.h
 * @brief ESP-NOW 手势推理结果发送模块接口定义
 * @details 提供ESP-NOW发送功能，支持通过函数接口发送推理结果（标签+置信度）
 */

#ifndef _ESPNOW_SENDER_H_
#define _ESPNOW_SENDER_H_

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief ESP-NOW 消息结构体
 */
typedef struct {
    char text[32];
} espnow_message_t;

/**
 * @brief 初始化 ESP-NOW 发送模块
 * @details 初始化WiFi STA模式、ESP-NOW协议、注册接收端MAC地址
 * @return ESP_OK 表示成功
 */
esp_err_t espnow_sender_init(void);

/**
 * @brief 通过 ESP-NOW 发送推理结果
 * @param label 识别标签字符串
 * @param confidence 置信度 (0.0 ~ 1.0)
 * @return ESP_OK 表示成功
 */
esp_err_t espnow_send_result(const char *label, float confidence);

#ifdef __cplusplus
}
#endif

#endif /* _ESPNOW_SENDER_H_ */