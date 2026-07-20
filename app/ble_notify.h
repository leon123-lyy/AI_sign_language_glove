/**
 * @file ble_notify.h
 * @brief BLE通知模块接口 - 通过BLE Notification推送手势信息到手机
 * @details 使用NimBLE协议栈，创建GATT服务并通过Notification推送文本消息
 */

#ifndef _BLE_NOTIFY_H_
#define _BLE_NOTIFY_H_

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化BLE通知模块
 * @details 初始化NimBLE协议栈、注册GATT服务、启动广播
 *          - 设备名称: GestureGlove
 *          - Service UUID: 4fafc201-1fb5-459e-8fcc-c5c9c331914b
 *          - Characteristic UUID: beb5483e-36e1-4688-b7f5-ea07361b26a8
 * @return ESP_OK 表示成功
 */
esp_err_t ble_notify_init(void);

/**
 * @brief 通过BLE Notification发送消息到已连接的手机
 * @details 如果无手机连接或通知未启用，静默失败（仅打印日志）
 * @param message 待发送的文本消息（以'\0'结尾）
 * @return ESP_OK 表示发送成功
 */
esp_err_t ble_notify_send(const char *message);

#ifdef __cplusplus
}
#endif
#endif /* _BLE_NOTIFY_H_ */
    