/**
 * @file label_counter.h
 * @brief 标签计数器模块接口定义
 * @details 管理手势标签的采集序号，程序重启后从1开始计数，不持久化
 */

#ifndef _LABEL_COUNTER_H_
#define _LABEL_COUNTER_H_

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化标签计数器
 */
void label_counter_init(void);

/**
 * @brief 获取标签的下一个序号
 * @param label 标签名称
 * @return 标签的下一个序号（从1开始自增），失败返回-1
 */
int label_counter_get_next(const char *label);

/**
 * @brief 获取标签的当前计数（不递增）
 * @param label 标签名称
 * @return 标签的当前计数，不存在返回0
 */
int label_counter_get_current(const char *label);

/**
 * @brief 设置标签的起始计数值（下次get_next从此值开始）
 * @param label 标签名称
 * @param start_count 起始计数值（如设为99，则下次从100开始）
 */
void label_counter_set(const char *label, int start_count);

/**
 * @brief 重置指定标签的计数
 * @param label 标签名称
 */
void label_counter_reset(const char *label);

/**
 * @brief 重置所有标签的计数
 */
void label_counter_reset_all(void);

#ifdef __cplusplus
}
#endif

#endif /* _LABEL_COUNTER_H_ */