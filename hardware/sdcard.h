/**
 * @file sdcard.h
 * @brief SD卡模块接口定义
 * @details SPI模式SD卡驱动，提供CSV文件操作接口
 */

#ifndef _SDCARD_H_
#define _SDCARD_H_

#include <stddef.h>
#include <stdbool.h>
#include "driver/gpio.h"

#ifdef __cplusplus
extern "C" {
#endif

// SPI引脚定义 (使用直接数字值以确保兼容性)
#define SDCARD_CS_PIN     15   /*!< SD卡片选引脚 - GPIO15 (HSPI CS) */
#define SDCARD_MOSI_PIN   13   /*!< SPI MOSI引脚 - GPIO13 (HSPI MOSI) */
#define SDCARD_SCLK_PIN   14   /*!< SPI时钟引脚 - GPIO14 (HSPI SCLK) */
#define SDCARD_MISO_PIN   12   /*!< SPI MISO引脚 - GPIO12 (HSPI MISO) */

/**
 * @brief 初始化SD卡
 * @return true: 初始化成功, false: 初始化失败
 */
bool sdcard_init(void);

/**
 * @brief 打开CSV文件并写入表头
 * @param filename 文件名（如 "data.csv"）
 * @param header CSV表头行（不含换行符）
 * @return 文件句柄，失败返回NULL
 */
void* sdcard_csv_open(const char* filename, const char* header);

/**
 * @brief 写入一行CSV数据
 * @param handle 文件句柄
 * @param line 数据行（不含换行符）
 * @return true: 写入成功, false: 写入失败
 */
bool sdcard_csv_write_line(void* handle, const char* line);

/**
 * @brief 关闭CSV文件
 * @param handle 文件句柄
 */
void sdcard_csv_close(void* handle);

/**
 * @brief 检查SD卡是否已挂载
 * @return true: 已挂载, false: 未挂载
 */
bool sdcard_is_mounted(void);

#ifdef __cplusplus
}
#endif

#endif /* _SDCARD_H_ */