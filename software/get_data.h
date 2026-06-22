/**
 * @file get_data.h
 * @brief 数据获取模块接口定义
 * @details 读取传感器数据并格式化为CSV行
 */

#ifndef _GET_DATA_H_
#define _GET_DATA_H_

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 获取传感器数据并格式化为CSV行
 * @param elapsed_ms 相对时间戳（毫秒）
 * @param buffer 输出缓冲区
 * @param buffer_size 缓冲区大小
 * @return 实际写入的字符数（不含null终止符），失败返回-1
 */
int get_data_line(uint32_t elapsed_ms, char* buffer, size_t buffer_size);

#ifdef __cplusplus
}
#endif

#endif /* _GET_DATA_H_ */