/**
 * @file main.c
 * @brief 手语数据采集系统主程序
 * @details 处理UART命令，协调各模块工作
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "driver/i2c.h"
#include "driver/spi_master.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "sdcard.h"
#include "mpu6050.h"
#include "potentiometer.h"
#include "label_counter.h"
#include "recorder.h"
#include "inference.h"
#include "button.h"
#include "espnow_sender.h"
#include "ble_notify.h"

static const char *TAG = "Main";

#define RECORD_BUTTON_PIN  GPIO_NUM_42

static char g_current_label[32] = "gesture";

static void record_button_task(void* arg)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << RECORD_BUTTON_PIN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);

    bool last_state = true;

    while (1) {
        bool current_state = gpio_get_level(RECORD_BUTTON_PIN);

        if (last_state == true && current_state == false) {
            vTaskDelay(pdMS_TO_TICKS(50));
            if (gpio_get_level(RECORD_BUTTON_PIN) == 0) {
                printf("[录制按钮] 开始录制，标签: %s\n", g_current_label);
                esp_err_t ret = recorder_start(g_current_label);
                if (ret == ESP_OK) {
                    printf("[录制] 手势数据已保存到CSV文件\n\n");
                } else {
                    printf("[错误] 录制失败\n\n");
                }
            }
        }

        last_state = current_state;
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

/**
 * @brief UART命令处理任务
 */
static void uart_command_task(void* arg)
{
    char buffer[64];
    int buffer_pos = 0;

    const uart_port_t uart_num = UART_NUM_0;
    uart_config_t uart_config = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    ESP_ERROR_CHECK(uart_param_config(uart_num, &uart_config));
    ESP_ERROR_CHECK(uart_set_pin(uart_num, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    ESP_ERROR_CHECK(uart_driver_install(uart_num, 256, 0, 0, NULL, 0));

    while (1) {
        uint8_t data;
        int len = uart_read_bytes(uart_num, &data, 1, pdMS_TO_TICKS(10));

        if (len > 0) {
            if (data == '\n' || data == '\r') {
                buffer[buffer_pos] = '\0';

                if (buffer_pos > 0) {
                    char* token = strtok(buffer, " ");
                    if (token && strcmp(token, "label") == 0) {
                        char* label = strtok(NULL, " ");
                        if (label && strlen(label) > 0) {
                            strncpy(g_current_label, label, sizeof(g_current_label) - 1);
                            g_current_label[sizeof(g_current_label) - 1] = '\0';
                            printf("[系统] 当前标签已设置为: %s\n\n", g_current_label);
                        } else {
                            printf("[系统] 当前标签: %s\n", g_current_label);
                            printf("[用法] label <名称> - 设置录制标签\n\n");
                        }
                    } else if (token && strcmp(token, "reset") == 0) {
                        label_counter_reset_all();
                        printf("[系统] 标签计数已重置\n\n");
                    } else if (token && strcmp(token, "count") == 0) {
                        char* count_str = strtok(NULL, " ");
                        if (count_str) {
                            int start_count = atoi(count_str);
                            if (start_count >= 0) {
                                label_counter_set(g_current_label, start_count);
                                printf("[系统] 标签 '%s' 起始计数已设置为: %d\n\n", g_current_label, start_count);
                            } else {
                                printf("[错误] 计数值必须 >= 0\n\n");
                            }
                        } else {
                            int current = label_counter_get_current(g_current_label);
                            printf("[系统] 标签 '%s' 当前计数: %d\n", g_current_label, current);
                            printf("[用法] count <数值> - 设置起始计数值（下次录制从该值+1开始）\n\n");
                        }
                    } else if (token && strcmp(token, "scan") == 0) {
                        printf("[系统] 开始扫描I2C总线...\n");
                        mpu6050_scan_i2c();
                        printf("\n");
                    }
                }

                buffer_pos = 0;
            } else if (data == '\b' && buffer_pos > 0) {
                buffer_pos--;
            } else if (buffer_pos < sizeof(buffer) - 1 && data >= 32 && data <= 126) {
                buffer[buffer_pos++] = data;
            }
        }

        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    if (!sdcard_init()) {
        while (1) {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }

    // === I2C 配置 ===
    #define I2C_SDA_PIN    GPIO_NUM_17
    #define I2C_SCL_PIN    GPIO_NUM_18
    
    // GPIO17=TDI, GPIO18=TDO, GPIO19=TCK, GPIO20=TMS
    gpio_reset_pin(GPIO_NUM_17);
    gpio_reset_pin(GPIO_NUM_18);
    
    // 设置为输入模式并启用内部上拉
    gpio_set_direction(I2C_SDA_PIN, GPIO_MODE_INPUT);
    gpio_set_direction(I2C_SCL_PIN, GPIO_MODE_INPUT);
    gpio_set_pull_mode(I2C_SDA_PIN, GPIO_PULLUP_ONLY);
    gpio_set_pull_mode(I2C_SCL_PIN, GPIO_PULLUP_ONLY);
    vTaskDelay(pdMS_TO_TICKS(50));
    
    i2c_config_t i2c_config = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = I2C_SDA_PIN,
        .scl_io_num = I2C_SCL_PIN,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = 50000,
    };
    
    ESP_ERROR_CHECK(i2c_param_config(I2C_NUM_0, &i2c_config));
    ESP_ERROR_CHECK(i2c_driver_install(I2C_NUM_0, I2C_MODE_MASTER, 0, 0, 0));
    
    vTaskDelay(pdMS_TO_TICKS(100));

    esp_err_t mpu_ret = mpu6050_init(I2C_NUM_0, I2C_SDA_PIN, I2C_SCL_PIN);
    if (mpu_ret != ESP_OK) {
        printf("[警告] MPU6050 初始化失败，系统将仅使用电位器数据\n");
        printf("[警告] 检查 I2C 接线: SDA=GPIO%d, SCL=GPIO%d\n\n", I2C_SDA_PIN, I2C_SCL_PIN);
    }

    potentiometer_init();

    label_counter_init();

    recorder_init();

    inference_init();

    // 配置 GPIO39 为输出低电平，作为 GPIO40/41/42 按钮的"虚拟地"
    gpio_config_t vground_conf = {
        .pin_bit_mask = (1ULL << GPIO_NUM_39),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&vground_conf);
    gpio_set_level(GPIO_NUM_39, 0);

    button_init();

    espnow_sender_init();

    ble_notify_init();

    xTaskCreate(uart_command_task, "uart_command", 4096, NULL, 5, NULL);

    xTaskCreate(button_task, "button", 8192, NULL, 6, NULL);

    xTaskCreate(record_button_task, "record_button", 4096, NULL, 5, NULL);

    printf("\n=== 手语数据采集系统 ===\n");
    printf("命令: label <名称>  - 设置录制标签 (默认: gesture)\n");
    printf("命令: reset         - 重置标签计数\n");
    printf("命令: count [数值]  - 查看/设置标签起始计数值\n");
    printf("命令: scan          - 扫描I2C总线，检测MPU6050设备\n");
    printf("按钮: GPIO42       - 按下开始录制CSV文件\n");
    printf("按钮: GPIO41       - 按下开始手势识别 (BLE通知)\n");
    printf("按钮: GPIO40       - 按下开始手势识别 (BLE通知 + ESP-NOW发送)\n");
    printf("========================================\n\n");

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}