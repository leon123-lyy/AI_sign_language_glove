/**
 * @file espnow_sender.h
 * @brief ESP-NOW 手势标签发送模块接口定义
 * @details 通过GPIO40按钮依次循环发送手势标签字符串到接收端
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
 * @brief ESP-NOW 发送任务入口函数
 * @details 轮询GPIO40按钮状态，按下后依次循环发送手势标签
 * @param arg 任务参数（未使用）
 */
void espnow_sender_task(void *arg);

#ifdef __cplusplus
}
#endif

#endif /* _ESPNOW_SENDER_H_ */