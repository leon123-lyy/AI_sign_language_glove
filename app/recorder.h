/**
 * @file recorder.h
 * @brief 数据记录器模块接口定义
 * @details 执行手势样本的数据采集并保存到SD卡
 */

#ifndef _RECORDER_H_
#define _RECORDER_H_

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// 采样参数
#define RECORDER_SAMPLING_INTERVAL_MS 20    // 采样间隔（毫秒）
#define RECORDER_SAMPLING_DURATION_MS 2000  // 采样时长（毫秒）
#define RECORDER_SAMPLES_COUNT (RECORDER_SAMPLING_DURATION_MS / RECORDER_SAMPLING_INTERVAL_MS)

// CSV表头
#define RECORDER_CSV_HEADER "timestamp,flex1,flex2,flex3,flex4,flex5,flex6,flex7,flex8,flex9,flex10,accX,accY,accZ,gyroX,gyroY,gyroZ"

/**
 * @brief 初始化记录器
 * @return ESP_OK 表示成功
 */
esp_err_t recorder_init(void);

/**
 * @brief 开始录制手势样本
 * @param label 手势标签名称
 * @return ESP_OK 表示成功，其他表示失败
 */
esp_err_t recorder_start(const char *label);

#ifdef __cplusplus
}
#endif

#endif /* _RECORDER_H_ */