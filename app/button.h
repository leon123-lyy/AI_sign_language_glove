/**
 * @file button.h
 * @brief 物理按钮模块接口定义
 * @details
 *   GPIO41: 按下 → 手势推理 (2秒采集) → BLE通知
 *   GPIO40: 按下 → 手势推理 (2秒采集) → BLE通知 + ESP-NOW 发送
 */

#ifndef _BUTTON_H_
#define _BUTTON_H_

#include "esp_err.h"
#include "driver/gpio.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 按钮引脚定义
 */
#define BUTTON_PIN_41    GPIO_NUM_41   /*!< 推理按钮 - GPIO41: 推理+BLE */
#define BUTTON_PIN_40    GPIO_NUM_40   /*!< 推理按钮 - GPIO40: 推理+BLE+ESP-NOW */

/** @brief 兼容旧的单按钮引用（指向GPIO41） */
#define BUTTON_PIN       BUTTON_PIN_41

/**
 * @brief 初始化按钮模块
 * @details 配置GPIO为输入，启用内部上拉和下降沿中断
 * @return ESP_OK 表示成功
 */
esp_err_t button_init(void);

/**
 * @brief 按钮任务入口函数
 * @param arg 任务参数（未使用）
 */
void button_task(void* arg);

/**
 * @brief 获取按钮任务是否正在处理推理
 * @return true 表示正在处理，false 表示空闲
 */
bool button_is_processing(void);

#ifdef __cplusplus
}
#endif

#endif /* _BUTTON_H_ */