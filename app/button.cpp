/**
 * @file button.cpp
 * @brief 物理按钮模块实现 (C++)
 * @details
 *   GPIO41: 按下 → 手势推理 → BLE通知
 *   GPIO40: 按下 → 手势推理 → BLE通知 + ESP-NOW发送
 */

#include "button.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <stdio.h>

extern "C" {
#include "inference.h"
#include "ble_notify.h"
#include "espnow_sender.h"
}

static const char *TAG = "Button";

/** 按钮中断信号量 */
static SemaphoreHandle_t s_button_semaphore = NULL;

/** 记录是哪个引脚触发了中断 */
static volatile uint32_t s_triggered_pin = 0;

/** 是否正在处理按钮触发的推理 */
static volatile bool s_is_processing = false;

/**
 * @brief GPIO中断服务函数
 * @details 只做最简单的操作：释放信号量并记录触发引脚
 * @param arg 中断参数（未使用）
 */
static void IRAM_ATTR button_isr_handler(void* arg)
{
    uint32_t gpio_num = (uint32_t)arg;

    BaseType_t xHigherPriorityTaskWoken = pdFALSE;

    /* 记录触发引脚 */
    s_triggered_pin = gpio_num;

    /* 释放信号量，唤醒按钮任务 */
    if (s_button_semaphore != NULL) {
        xSemaphoreGiveFromISR(s_button_semaphore, &xHigherPriorityTaskWoken);
    }

    /* 如果需要，进行上下文切换 */
    if (xHigherPriorityTaskWoken == pdTRUE) {
        portYIELD_FROM_ISR();
    }
}

/**
 * @brief 执行一次性推理，结果通过 BLE 通知推送
 * @details 调用 inference_run，内部自动完成传感器数据采集和模型推理
 * @param send_espnow 是否同时通过 ESP-NOW 发送
 */
static void button_run_single_inference(bool send_espnow)
{
    printf("[按钮] 开始采集数据...\n");

    inference_result_t result = {.label = NULL, .confidence = 0.0f, .label_index = -1};
    esp_err_t ret = inference_run(&result);

    if (ret == ESP_OK && result.label != NULL) {
        printf("[识别结果] %s (%.1f%%)\n\n", result.label, result.confidence * 100);

        /* 通过BLE通知推送到手机 */
        char ble_msg[64];
        snprintf(ble_msg, sizeof(ble_msg), "%s (%.1f%%)", result.label, result.confidence * 100);
        ble_notify_send(ble_msg);

        /* GPIO40 额外通过 ESP-NOW 发送 */
        if (send_espnow) {
            espnow_send_result(result.label, result.confidence);
        }
    } else {
        printf("[识别结果] 识别失败\n\n");
    }
}

extern "C" {

/**
 * @brief 按钮任务主循环
 * @param arg 任务参数（未使用）
 */
void button_task(void* arg)
{
    ESP_LOGI(TAG, "Button task started (GPIO41: 推理+BLE, GPIO40: 推理+BLE+ESP-NOW)");

    while (1) {
        /* 等待按钮中断信号量 */
        if (xSemaphoreTake(s_button_semaphore, portMAX_DELAY) == pdTRUE) {
            uint32_t pin = s_triggered_pin;

            /* 去抖动：等待50ms后再次确认按钮状态 */
            vTaskDelay(pdMS_TO_TICKS(50));

            /* 再次读取GPIO电平确认（按钮按下时为低电平） */
            if (gpio_get_level((gpio_num_t)pin) == 0) {
                ESP_LOGI(TAG, "Button GPIO%d pressed (debounced)", (int)pin);

                /* 检查是否正在处理 */
                if (s_is_processing) {
                    ESP_LOGW(TAG, "Busy - skipping button press");
                    continue;
                }

                /* 设置处理标志 */
                s_is_processing = true;

                /* 执行一次性推理，GPIO40额外发送ESP-NOW */
                bool send_espnow = (pin == BUTTON_PIN_40);
                button_run_single_inference(send_espnow);

                /* 清除处理标志 */
                s_is_processing = false;
            }
        }
    }
}

/**
 * @brief 初始化按钮模块
 * @details 配置 GPIO41 和 GPIO40 为输入，启用内部上拉和下降沿中断
 * @return ESP_OK 表示成功
 */
esp_err_t button_init(void)
{
    ESP_LOGI(TAG, "Initializing buttons on GPIO%d and GPIO%d", BUTTON_PIN_41, BUTTON_PIN_40);

    /* 创建信号量（用于中断与任务间的同步） */
    s_button_semaphore = xSemaphoreCreateBinary();
    if (s_button_semaphore == NULL) {
        ESP_LOGE(TAG, "Failed to create button semaphore");
        return ESP_ERR_NO_MEM;
    }

    /* 配置 GPIO41 和 GPIO40 */
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << BUTTON_PIN_41) | (1ULL << BUTTON_PIN_40),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_NEGEDGE,        /* 下降沿触发中断 */
    };
    gpio_config(&io_conf);

    /* 安装GPIO中断处理服务 */
    gpio_install_isr_service(0);

    /* 注册中断服务函数（用引脚号作为参数传递，ISR中区分来源） */
    gpio_isr_handler_add(BUTTON_PIN_41, button_isr_handler, (void*)BUTTON_PIN_41);
    gpio_isr_handler_add(BUTTON_PIN_40, button_isr_handler, (void*)BUTTON_PIN_40);

    ESP_LOGI(TAG, "Buttons initialized");
    return ESP_OK;
}

/**
 * @brief 获取按钮任务是否正在处理推理
 * @return true 表示正在处理，false 表示空闲
 */
bool button_is_processing(void)
{
    return s_is_processing;
}

} // extern "C"