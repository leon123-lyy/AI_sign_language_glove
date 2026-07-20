/**
 * @file inference.cpp
 * @brief 手势识别推理模块实现 (C++)
 * @details
 *   模型输入格式（来自 model_metadata.h）：
 *     - 16 轴/帧：flex1-10, accX, accY, accZ, gyroX, gyroY, gyroZ（不含 timestamp）
 *     - 100 帧/窗口，20ms 间隔，总计 2000ms
 *     - 特征总数：EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE = 100 × 16 = 1600
 *     - 数据布局：按帧交错 [f0_ax0, f0_ax1, ..., f0_ax15, f1_ax0, ..., f99_ax15]
 *   DSP 处理链：
 *     Raw 1600 floats → Raw Features → 1600 features → INT8 NN → 6 classes
 */

#include "inference.h"
#include <string.h>
#include <stdio.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

extern "C" {
#include "../hardware/mpu6050.h"
#include "../hardware/potentiometer.h"
}

#include "edge-impulse-sdk/classifier/ei_run_classifier.h"

static const char *TAG = "Inference";

static float s_features[EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE];

static int get_signal_data(size_t offset, size_t length, float *out_ptr)
{
    for (size_t i = 0; i < length; i++) {
        out_ptr[i] = s_features[offset + i];
    }
    return EIDSP_OK;
}

extern "C" {

esp_err_t inference_run(inference_result_t *result)
{
    if (!result) {
        ESP_LOGE(TAG, "Invalid result pointer");
        return ESP_ERR_INVALID_ARG;
    }

    result->label = NULL;
    result->confidence = 0.0f;
    result->label_index = -1;

    uint32_t start_ms = esp_timer_get_time() / 1000;

    for (int frame = 0; frame < EI_CLASSIFIER_RAW_SAMPLE_COUNT; frame++) {
        uint32_t target_ms = start_ms + (uint32_t)frame * EI_CLASSIFIER_INTERVAL_MS;
        uint32_t current_ms = esp_timer_get_time() / 1000;

        if (target_ms > current_ms) {
            vTaskDelay(pdMS_TO_TICKS(target_ms - current_ms));
        }

        int16_t acc_x = 0, acc_y = 0, acc_z = 0;
        int16_t gyro_x = 0, gyro_y = 0, gyro_z = 0;
        mpu6050_read_accel_gyro(&acc_x, &acc_y, &acc_z, &gyro_x, &gyro_y, &gyro_z);
        int flex_values[POTENTIOMETER_COUNT] = {0};
        potentiometer_read_all(flex_values);

        int base = frame * EI_CLASSIFIER_RAW_SAMPLES_PER_FRAME;

        for (int i = 0; i < POTENTIOMETER_COUNT; i++) {
            s_features[base + i] = (float)flex_values[i];
        }

        s_features[base + 10] = (float)acc_x;
        s_features[base + 11] = (float)acc_y;
        s_features[base + 12] = (float)acc_z;
        s_features[base + 13] = (float)gyro_x;
        s_features[base + 14] = (float)gyro_y;
        s_features[base + 15] = (float)gyro_z;
    }

    signal_t signal;
    signal.total_length = EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE;
    signal.get_data = &get_signal_data;

    ei_impulse_result_t ei_result = {};
    EI_IMPULSE_ERROR ei_error = run_classifier(&signal, &ei_result, false);

    if (ei_error != EI_IMPULSE_OK) {
        ESP_LOGE(TAG, "run_classifier failed: %d", ei_error);
        return ESP_FAIL;
    }

    float max_confidence = 0.0f;
    int max_index = 0;

    for (size_t i = 0; i < EI_CLASSIFIER_LABEL_COUNT; i++) {
        if (ei_result.classification[i].value > max_confidence) {
            max_confidence = ei_result.classification[i].value;
            max_index = i;
        }
    }

    result->label = ei_result.classification[max_index].label;
    result->confidence = max_confidence;
    result->label_index = max_index;

    printf("[推理] %s (%.1f%%)\n", result->label, result->confidence * 100.0f);
    return ESP_OK;
}

esp_err_t inference_init(void)
{
    run_classifier_init();
    memset(s_features, 0, sizeof(s_features));
    return ESP_OK;
}

esp_err_t inference_start(void)
{
    ESP_LOGW(TAG, "Continuous inference not supported in this version");
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t inference_stop(void)
{
    return ESP_OK;
}

bool inference_is_running(void)
{
    return false;
}

esp_err_t inference_get_last_result(inference_result_t *result)
{
    if (!result) {
        return ESP_ERR_INVALID_ARG;
    }
    result->label = NULL;
    result->confidence = 0.0f;
    result->label_index = -1;
    return ESP_OK;
}

} // extern "C"