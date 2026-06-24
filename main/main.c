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
    // ESP32-S3 的 GPIO17/18 默认被 JTAG 占用，需要先释放
    #define I2C_SDA_PIN    GPIO_NUM_17
    #define I2C_SCL_PIN    GPIO_NUM_18
    
    // 释放JTAG引脚，将其用于普通GPIO
    // GPIO17=TDI, GPIO18=TDO, GPIO19=TCK, GPIO20=TMS
    gpio_reset_pin(GPIO_NUM_17);
    gpio_reset_pin(GPIO_NUM_18);
    
    // 设置为输入模式并启用内部上拉
    gpio_set_direction(I2C_SDA_PIN, GPIO_MODE_INPUT);
    gpio_set_direction(I2C_SCL_PIN, GPIO_MODE_INPUT);
    gpio_set_pull_mode(I2C_SDA_PIN, GPIO_PULLUP_ONLY);
    gpio_set_pull_mode(I2C_SCL_PIN, GPIO_PULLUP_ONLY);
    vTaskDelay(pdMS_TO_TICKS(50));
    
    int sda_level = gpio_get_level(I2C_SDA_PIN);
    int scl_level = gpio_get_level(I2C_SCL_PIN);
    ESP_LOGI(TAG, "=== I2C Hardware Pre-Check ===");
    ESP_LOGI(TAG, "SDA=GPIO%d level=%d, SCL=GPIO%d level=%d (expected: 1,1)", 
             I2C_SDA_PIN, sda_level, I2C_SCL_PIN, scl_level);
    
    if (sda_level == 0 && scl_level == 0) {
        ESP_LOGE(TAG, "I2C LINES ARE BOTH LOW! Check wiring and pull-up resistors.");
    } else if (sda_level == 0 || scl_level == 0) {
        ESP_LOGE(TAG, "One I2C line is LOW! Check wiring.");
    } else {
        ESP_LOGI(TAG, "I2C lines are HIGH - pull-up resistors working");
    }

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
    ESP_LOGI(TAG, "I2C driver installed: Port=I2C_NUM_0, SDA=GPIO%d, SCL=GPIO%d, speed=50kHz", 
             I2C_SDA_PIN, I2C_SCL_PIN);
    
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

    button_init();

    espnow_sender_init();

    xTaskCreate(uart_command_task, "uart_command", 4096, NULL, 5, NULL);

    xTaskCreate(button_task, "button", 8192, NULL, 6, NULL);

    xTaskCreate(record_button_task, "record_button", 4096, NULL, 5, NULL);

    xTaskCreate(espnow_sender_task, "espnow_sender", 4096, NULL, 5, NULL);

    printf("\n=== 手语数据采集系统 ===\n");
    printf("命令: label <名称>  - 设置录制标签 (默认: gesture)\n");
    printf("命令: reset         - 重置标签计数\n");
    printf("命令: scan          - 扫描I2C总线，检测MPU6050设备\n");
    printf("按钮: GPIO42       - 按下开始录制CSV文件\n");
    printf("按钮: GPIO41       - 按下开始2秒手势识别\n");
    printf("按钮: GPIO40       - 按下发送ESP-NOW手势标签\n");
    printf("========================================\n\n");

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}