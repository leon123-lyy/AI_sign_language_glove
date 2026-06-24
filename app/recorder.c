/**
 * @file recorder.c
 * @brief 数据记录器模块实现
 */

#include "recorder.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "../hardware/sdcard.h"
#include "../hardware/mpu6050.h"
#include "../software/get_data.h"
#include "label_counter.h"

static const char *TAG = "Recorder";

esp_err_t recorder_init(void)
{
    ESP_LOGI(TAG, "Recorder initialized");
    return ESP_OK;
}

esp_err_t recorder_start(const char *label)
{
    if (!label || strlen(label) == 0) {
        ESP_LOGE(TAG, "Invalid label");
        return ESP_ERR_INVALID_ARG;
    }

    // 重置MPU6050失败计数器，避免累积错误
    mpu6050_reset_fail_count();

    // 健康检查MPU6050设备
    if (!mpu6050_health_check()) {
        ESP_LOGW(TAG, "MPU6050 health check failed, attempting re-initialization...");
        // 尝试重新初始化MPU6050
        if (!mpu6050_reinit()) {
            ESP_LOGE(TAG, "MPU6050 re-initialization failed, continuing without IMU data");
        } else {
            ESP_LOGI(TAG, "MPU6050 re-initialized successfully");
        }
    }

    // 检查SD卡是否已挂载
    if (!sdcard_is_mounted()) {
        ESP_LOGE(TAG, "SD card not mounted");
        return ESP_ERR_NOT_FOUND;
    }

    // 获取标签序号
    int seq = label_counter_get_next(label);
    if (seq <= 0) {
        ESP_LOGE(TAG, "Failed to get label sequence");
        return ESP_FAIL;
    }

    // 生成文件名
    char filename[64];
    snprintf(filename, sizeof(filename), "%s.%d.csv", label, seq);

    ESP_LOGI(TAG, "Starting recording: %s", filename);

    // 打开CSV文件
    void *csv_handle = sdcard_csv_open(filename, RECORDER_CSV_HEADER);
    if (!csv_handle) {
        ESP_LOGE(TAG, "Failed to open CSV file: %s", filename);
        return ESP_FAIL;
    }

    // 精确定时采集
    uint32_t start_ms = esp_timer_get_time() / 1000;
    int success_count = 0;

    for (int i = 0; i < RECORDER_SAMPLES_COUNT; i++) {
        // 硬超时检查：如果已超过2.5秒，强制结束
        uint32_t current_ms = esp_timer_get_time() / 1000;
        if (current_ms - start_ms > 2500) {
            ESP_LOGW(TAG, "Recording timeout, stopping at sample %d/%d", i, RECORDER_SAMPLES_COUNT);
            break;
        }

        // 计算目标时间和理论时间戳
        uint32_t target_ms = start_ms + (uint32_t)i * RECORDER_SAMPLING_INTERVAL_MS;
        uint32_t elapsed_ms = (uint32_t)i * RECORDER_SAMPLING_INTERVAL_MS;

        // 如果已经落后于目标时间，跳过等待直接采样
        if (target_ms > current_ms) {
            vTaskDelay(pdMS_TO_TICKS(target_ms - current_ms));
        }

        // 获取数据行
        char line[256];
        int len = get_data_line(elapsed_ms, line, sizeof(line));

        if (len <= 0) {
            // 数据获取失败不中断，写入空数据行继续
            snprintf(line, sizeof(line), "%lu,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0", (unsigned long)elapsed_ms);
        }

        // 写入数据行
        sdcard_csv_write_line(csv_handle, line);
        success_count++;
    }

    // 关闭文件
    sdcard_csv_close(csv_handle);

    ESP_LOGI(TAG, "Recording done: %s (%d samples)", filename, success_count);
    return ESP_OK;
}