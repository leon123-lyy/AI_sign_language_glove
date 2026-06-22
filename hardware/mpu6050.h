#ifndef _MPU6050_H_
#define _MPU6050_H_

#include <stdint.h>
#include "esp_err.h"
#include "driver/i2c.h"
#include "driver/gpio.h"

// MPU6050默认I2C地址
#define MPU6050_ADDR_AD0_LOW     0x68    // AD0引脚接地时的地址
#define MPU6050_ADDR_AD0_HIGH    0x69    // AD0引脚接VCC时的地址

// 传感器数据结构体
typedef struct {
    int16_t accel_x;  // 加速度计X轴原始数据
    int16_t accel_y;  // 加速度计Y轴原始数据
    int16_t accel_z;  // 加速度计Z轴原始数据
    int16_t gyro_x;   // 陀螺仪X轴原始数据
    int16_t gyro_y;   // 陀螺仪Y轴原始数据
    int16_t gyro_z;   // 陀螺仪Z轴原始数据
    int16_t temp;     // 温度原始数据
} mpu6050_data_t;

/**
 * @brief 初始化MPU6050传感器
 * @param i2c_num I2C端口号 (I2C_NUM_0 或 I2C_NUM_1)
 * @param sda_pin SDA引脚 GPIO17
 * @param scl_pin SCL引脚 GPIO18
 * @return 初始化结果，ESP_OK表示成功
 */
esp_err_t mpu6050_init(i2c_port_t i2c_num, gpio_num_t sda_pin, gpio_num_t scl_pin);

/**
 * @brief 读取MPU6050所有传感器数据
 * @param data 存储传感器数据的结构体指针
 * @return 读取结果，ESP_OK表示成功
 */
esp_err_t mpu6050_read_data(mpu6050_data_t *data);

/**
 * @brief 检查MPU6050是否就绪
 * @return true 表示设备已初始化成功并可读取
 */
bool mpu6050_is_ready(void);

/**
 * @brief 读取加速度计数据
 * @param ax, ay, az 存储加速度计数据的指针
 * @return 读取结果，ESP_OK表示成功
 */
esp_err_t mpu6050_read_accel(int16_t *ax, int16_t *ay, int16_t *az);

/**
 * @brief 读取陀螺仪数据
 * @param gx, gy, gz 存储陀螺仪数据的指针
 * @return 读取结果，ESP_OK表示成功
 */
esp_err_t mpu6050_read_gyro(int16_t *gx, int16_t *gy, int16_t *gz);

/**
 * @brief 读取温度数据
 * @param temp 存储温度数据的指针
 * @return 读取结果，ESP_OK表示成功
 */
esp_err_t mpu6050_read_temp(int16_t *temp);

/**
 * @brief 同时读取加速度计和陀螺仪数据
 * @param acc_x, acc_y, acc_z 存储加速度计数据的指针
 * @param gyro_x, gyro_y, gyro_z 存储陀螺仪数据的指针
 * @return 读取结果，ESP_OK表示成功
 */
esp_err_t mpu6050_read_accel_gyro(int16_t *acc_x, int16_t *acc_y, int16_t *acc_z,
                                   int16_t *gyro_x, int16_t *gyro_y, int16_t *gyro_z);

/**
 * @brief 打印传感器数据到串口
 */
void mpu6050_print_data(void);

#endif /* _MPU6050_H_ */