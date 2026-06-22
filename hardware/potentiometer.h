#ifndef _POTENTIOMETER_H_
#define _POTENTIOMETER_H_

#include <stdint.h>
#include "esp_err.h"

#define POTENTIOMETER_COUNT 10

/**
 * @brief 初始化电位器ADC
 * @return 初始化结果，ESP_OK表示成功
 */
esp_err_t potentiometer_init(void);

/**
 * @brief 读取所有电位器值
 * @param values 存储10个电位器值的数组指针
 */
void potentiometer_read_all(int *values);

/**
 * @brief 读取单个电位器值
 * @param index 电位器索引 (0-9)
 * @return 电位器的ADC原始值 (0-4095)
 */
int potentiometer_read_single(int index);

/**
 * @brief 打印所有电位器值到串口
 */
void potentiometer_print_all(void);

#endif /* _POTENTIOMETER_H_ */