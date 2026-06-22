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
 *     Raw 1600 floats → Spectral Analysis (FFT) → 208 features → INT8 NN → 4 classes
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

// Flex 传感器基线校准值（开机时静止状态 ADC 读数）
static float s_flex_baseline[POTENTIOMETER_COUNT] = {0};
static bool  s_flex_calibrated = false;

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

    bool imu_available = mpu6050_is_ready();
    printf("\n========== 推理开始 ==========\n");
    printf("[诊断] MPU6050 状态: %s\n", imu_available ? "就绪" : "未就绪(将尝试读取)");
    printf("[诊断] 模型期望: %d 帧 x %d 轴 = %d 特征\n",
           EI_CLASSIFIER_RAW_SAMPLE_COUNT,
           EI_CLASSIFIER_RAW_SAMPLES_PER_FRAME,
           EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE);
    printf("[诊断] 轴顺序: %s\n", EI_CLASSIFIER_FUSION_AXES_STRING);

    uint32_t start_ms = esp_timer_get_time() / 1000;

    for (int frame = 0; frame < EI_CLASSIFIER_RAW_SAMPLE_COUNT; frame++) {
        uint32_t target_ms = start_ms + (uint32_t)frame * EI_CLASSIFIER_INTERVAL_MS;
        uint32_t current_ms = esp_timer_get_time() / 1000;

        if (target_ms > current_ms) {
            vTaskDelay(pdMS_TO_TICKS(target_ms - current_ms));
        }

        int16_t acc_x = 0, acc_y = 0, acc_z = 0;
        int16_t gyro_x = 0, gyro_y = 0, gyro_z = 0;
        // 先读 I2C（此时电源轨最干净，ADC 尚未产生瞬态电流）
        mpu6050_read_accel_gyro(&acc_x, &acc_y, &acc_z, &gyro_x, &gyro_y, &gyro_z);
        // 再读 ADC（会产生电源毛刺，但不影响已完成 I2C 通信）
        int flex_values[POTENTIOMETER_COUNT] = {0};
        potentiometer_read_all(flex_values);

        int base = frame * EI_CLASSIFIER_RAW_SAMPLES_PER_FRAME;

        // === Flex 传感器 (ADC 原始值减去基线，消除漂移) ===
        for (int i = 0; i < POTENTIOMETER_COUNT; i++) {
            float corrected = (float)flex_values[i] - s_flex_baseline[i];
            s_features[base + i] = corrected;
        }

        // === IMU 数据 (原始 int16 → 物理单位) ===
        // MPU6050 寄存器配置: ACCEL_CONFIG=0x18 (±16g), GYRO_CONFIG=0x18 (±2000°/s)
        // 加速度灵敏度: 2048 LSB/g → 转换为 m/s²: raw / 2048.0 * 9.80665
        // 陀螺仪灵敏度: 16.4 LSB/(°/s) → 转换为 °/s: raw / 16.4
        const float ACCEL_SCALE = 2048.0f / 9.80665f;  // LSB → m/s² (灵敏度2048LSB/g, 除以g→m/s²换算系数)
        const float GYRO_SCALE  = 16.4f;                 // LSB → °/s
        s_features[base + 10] = (float)acc_x  / ACCEL_SCALE;
        s_features[base + 11] = (float)acc_y  / ACCEL_SCALE;
        s_features[base + 12] = (float)acc_z  / ACCEL_SCALE;
        s_features[base + 13] = (float)gyro_x / GYRO_SCALE;
        s_features[base + 14] = (float)gyro_y / GYRO_SCALE;
        s_features[base + 15] = (float)gyro_z / GYRO_SCALE;

        // 打印前3帧和后3帧的数据（含 IMU 物理单位和 flex 校准值）
        if (frame < 3 || frame >= EI_CLASSIFIER_RAW_SAMPLE_COUNT - 3) {
            printf("[帧%3d] flex_raw:%4d %4d %4d %4d %4d %4d %4d %4d %4d %4d",
                   frame,
                   flex_values[0], flex_values[1], flex_values[2], flex_values[3], flex_values[4],
                   flex_values[5], flex_values[6], flex_values[7], flex_values[8], flex_values[9]);
            printf(" | acc:%6.1f %6.1f %6.1f m/s² | gyro:%6.1f %6.1f %6.1f °/s\n",
                   (float)acc_x / ACCEL_SCALE, (float)acc_y / ACCEL_SCALE, (float)acc_z / ACCEL_SCALE,
                   (float)gyro_x / GYRO_SCALE, (float)gyro_y / GYRO_SCALE, (float)gyro_z / GYRO_SCALE);
        }
    }

    uint32_t elapsed_ms = esp_timer_get_time() / 1000 - start_ms;
    printf("[诊断] 采集耗时: %lu ms (期望 2000ms)\n", (unsigned long)elapsed_ms);

    // 打印每个轴的数据范围（min/max），帮助判断数据是否合理
    printf("[诊断] 各轴数据范围 (min / max):\n");
    const char* axis_names[] = {"flex1","flex2","flex3","flex4","flex5",
                                "flex6","flex7","flex8","flex9","flex10",
                                "accX","accY","accZ","gyroX","gyroY","gyroZ"};
    for (int ax = 0; ax < EI_CLASSIFIER_RAW_SAMPLES_PER_FRAME; ax++) {
        float min_val = s_features[ax];
        float max_val = s_features[ax];
        for (int f = 1; f < EI_CLASSIFIER_RAW_SAMPLE_COUNT; f++) {
            float v = s_features[f * EI_CLASSIFIER_RAW_SAMPLES_PER_FRAME + ax];
            if (v < min_val) min_val = v;
            if (v > max_val) max_val = v;
        }
        printf("  %6s: [%8.0f, %8.0f]  range=%.0f\n",
               axis_names[ax], min_val, max_val, max_val - min_val);
    }

    signal_t signal;
    signal.total_length = EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE;
    signal.get_data = &get_signal_data;

    ei_impulse_result_t ei_result = {};
    printf("[诊断] 调用 run_classifier (debug=true 查看DSP特征)...\n");
    EI_IMPULSE_ERROR ei_error = run_classifier(&signal, &ei_result, true);

    if (ei_error != EI_IMPULSE_OK) {
        ESP_LOGE(TAG, "run_classifier failed: %d", ei_error);
        printf("========== 推理失败 ==========\n\n");
        return ESP_FAIL;
    }

    printf("\n[诊断] 推理耗时: DSP=%dms, 推理=%dms, 后处理=%dms\n",
           ei_result.timing.dsp, ei_result.timing.classification, ei_result.timing.postprocessing);

    // 打印所有分类结果
    printf("[诊断] 全部分类结果:\n");
    float max_confidence = 0.0f;
    int max_index = 0;

    for (size_t i = 0; i < EI_CLASSIFIER_LABEL_COUNT; i++) {
        printf("  [%d] %-16s : %.4f (%.1f%%)\n",
               (int)i,
               ei_result.classification[i].label,
               ei_result.classification[i].value,
               ei_result.classification[i].value * 100.0f);
        if (ei_result.classification[i].value > max_confidence) {
            max_confidence = ei_result.classification[i].value;
            max_index = i;
        }
    }

    result->label = ei_result.classification[max_index].label;
    result->confidence = max_confidence;
    result->label_index = max_index;

    printf("========== 推理完成: %s (%.1f%%) ==========\n\n",
           result->label, result->confidence * 100.0f);
    return ESP_OK;
}

esp_err_t inference_init(void)
{
    ESP_LOGI(TAG, "Initializing inference module");

    run_classifier_init();

    memset(s_features, 0, sizeof(s_features));

    // === Flex 传感器基线校准 ===
    // 开机时读取 10 次取平均，作为静止状态的基线参考
    // 目的：消除电源电压、温度等环境因素导致的 ADC 漂移
    printf("[校准] Flex 传感器基线采集中，请保持手套静止放松...\n");
    float sum[POTENTIOMETER_COUNT] = {0};
    const int calib_samples = 10;
    for (int i = 0; i < calib_samples; i++) {
        int values[POTENTIOMETER_COUNT] = {0};
        potentiometer_read_all(values);
        for (int j = 0; j < POTENTIOMETER_COUNT; j++) {
            sum[j] += (float)values[j];
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    for (int j = 0; j < POTENTIOMETER_COUNT; j++) {
        s_flex_baseline[j] = sum[j] / calib_samples;
    }
    s_flex_calibrated = true;
    printf("[校准] Flex 基线值: ");
    for (int j = 0; j < POTENTIOMETER_COUNT; j++) {
        printf("%.0f ", s_flex_baseline[j]);
    }
    printf("\n[校准] 完成\n");

    ESP_LOGI(TAG, "Inference module initialized");
    ESP_LOGI(TAG, "  DSP input: %d features (%d frames x %d axes)",
             EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE,
             EI_CLASSIFIER_RAW_SAMPLE_COUNT,
             EI_CLASSIFIER_RAW_SAMPLES_PER_FRAME);
    ESP_LOGI(TAG, "  NN input:  %d features", EI_CLASSIFIER_NN_INPUT_FRAME_SIZE);
    ESP_LOGI(TAG, "  Labels:    %d classes", EI_CLASSIFIER_LABEL_COUNT);
    ESP_LOGI(TAG, "  Interval:  %d ms", EI_CLASSIFIER_INTERVAL_MS);
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