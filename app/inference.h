/**
 * @file inference.h
 * @brief 手势识别推理模块接口定义
 * @details 集成Edge Impulse模型实现单次推理
 */

#ifndef _INFERENCE_H_
#define _INFERENCE_H_

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 推理结果结构体
 */
typedef struct {
    const char *label;
    float confidence;
    int label_index;
} inference_result_t;

/**
 * @brief 初始化推理模块
 * @return ESP_OK 表示成功
 */
esp_err_t inference_init(void);

/**
 * @brief 启动实时推理模式（当前版本不支持）
 * @return ESP_ERR_NOT_SUPPORTED
 */
esp_err_t inference_start(void);

/**
 * @brief 停止实时推理模式
 * @return ESP_OK 表示成功
 */
esp_err_t inference_stop(void);

/**
 * @brief 检查是否正在进行推理
 * @return true 表示正在推理，false 表示未在推理
 */
bool inference_is_running(void);

/**
 * @brief 获取最近一次推理结果
 * @param result 存储推理结果的结构体指针
 * @return ESP_OK 表示成功
 */
esp_err_t inference_get_last_result(inference_result_t *result);

/**
 * @brief 执行单次推理（用于按钮触发）
 * @details
 *   内部自动完成：
 *     1. 以 20ms 间隔采集 100 帧传感器数据（10 电位器 + 6 轴 MPU6050）
 *     2. 按模型要求的交错格式填充特征缓冲区
 *     3. 调用 run_classifier 执行 DSP + 推理
 *     4. 提取最高置信度的分类结果
 * @param result 存储推理结果的结构体指针
 * @return ESP_OK 表示成功
 */
esp_err_t inference_run(inference_result_t *result);

#ifdef __cplusplus
}
#endif

#endif /* _INFERENCE_H_ */