/**
 * @file button.h
 * @brief 物理按钮模块接口定义
 * @details 处理GPIO 41按钮的中断检测和一次性推理触发
 */

#ifndef _BUTTON_H_
#define _BUTTON_H_

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 按钮引脚定义
 */
#define BUTTON_PIN    GPIO_NUM_41   /*!< 推理按钮引脚 - GPIO41 */

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