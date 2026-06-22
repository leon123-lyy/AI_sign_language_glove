/**
 * @file get_data.c
 * @brief 数据获取模块实现
 */

#include "get_data.h"
#include "esp_log.h"
#include "../hardware/mpu6050.h"
#include "../hardware/potentiometer.h"

static const char *TAG = "GetData";

int get_data_line(uint32_t elapsed_ms, char* buffer, size_t buffer_size)
{
    if (!buffer || buffer_size < 256) {
        ESP_LOGE(TAG, "Invalid buffer parameters");
        return -1;
    }

    int pot_values[POTENTIOMETER_COUNT] = {0};

    int16_t acc_x = 0, acc_y = 0, acc_z = 0;
    int16_t gyro_x = 0, gyro_y = 0, gyro_z = 0;

    // 先读 I2C（电源轨最干净时通信），再读 ADC（产生瞬态电流不影响已完成的 I2C）
    mpu6050_read_accel_gyro(&acc_x, &acc_y, &acc_z, &gyro_x, &gyro_y, &gyro_z);
    potentiometer_read_all(pot_values);

    int len = snprintf(buffer, buffer_size,
                      "%lu,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d",
                      (unsigned long)elapsed_ms,
                      pot_values[0], pot_values[1], pot_values[2], pot_values[3], pot_values[4],
                      pot_values[5], pot_values[6], pot_values[7], pot_values[8], pot_values[9],
                      acc_x, acc_y, acc_z,
                      gyro_x, gyro_y, gyro_z);

    if (len < 0 || len >= buffer_size) {
        ESP_LOGE(TAG, "Buffer too small or formatting error");
        return -1;
    }

    return len;
}