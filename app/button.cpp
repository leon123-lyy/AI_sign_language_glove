/**
 * @file button.c
 * @brief 物理按钮模块实现
 * @details 处理GPIO 41按钮的中断检测和一次性推理触发
 */

#include "button.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

extern "C" {
#include "inference.h"
}

static const char *TAG = "Button";

/** 按钮中断信号量 */
static SemaphoreHandle_t s_button_semaphore = NULL;

/** 是否正在处理按钮触发的推理 */
static volatile bool s_is_processing = false;

/**
 * @brief GPIO中断服务函数
 * @details 只做最简单的操作：释放信号量
 * @param arg 中断参数（未使用）
 */
static void IRAM_ATTR button_isr_handler(void* arg)
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    
    // 释放信号量，唤醒按钮任务
    if (s_button_semaphore != NULL) {
        xSemaphoreGiveFromISR(s_button_semaphore, &xHigherPriorityTaskWoken);
    }
    
    // 如果需要，进行上下文切换
    if (xHigherPriorityTaskWoken == pdTRUE) {
        portYIELD_FROM_ISR();
    }
}

/**
 * @brief 执行一次性推理
 * @details 调用 inference_run，内部自动完成传感器数据采集和模型推理
 */
static void button_run_single_inference(void)
{
    printf("[按钮] 开始采集数据...\n");

    inference_result_t result = {.label = NULL, .confidence = 0.0f, .label_index = -1};
    esp_err_t ret = inference_run(&result);

    if (ret == ESP_OK && result.label != NULL) {
        printf("[识别结果] %s (%.1f%%)\n\n", result.label, result.confidence * 100);
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
    ESP_LOGI(TAG, "Button task started");
    
    while (1) {
        // 等待按钮中断信号量
        if (xSemaphoreTake(s_button_semaphore, portMAX_DELAY) == pdTRUE) {
            // 去抖动：等待50ms后再次确认按钮状态
            vTaskDelay(pdMS_TO_TICKS(50));
            
            // 再次读取GPIO电平确认（按钮按下时为低电平）
            if (gpio_get_level(BUTTON_PIN) == 0) {
                ESP_LOGI(TAG, "Button pressed (debounced)");
                
                // 检查是否正在处理
                if (s_is_processing) {
                    ESP_LOGW(TAG, "Busy - skipping button press");
                    continue;
                }
                
                // 设置处理标志
                s_is_processing = true;
                
                // 执行一次性推理
                button_run_single_inference();
                
                // 清除处理标志
                s_is_processing = false;
            }
        }
    }
}

/**
 * @brief 初始化按钮模块
 * @return ESP_OK 表示成功
 */
esp_err_t button_init(void)
{
    ESP_LOGI(TAG, "Initializing button on GPIO%d", BUTTON_PIN);
    
    // 创建信号量（用于中断与任务间的同步）
    s_button_semaphore = xSemaphoreCreateBinary();
    if (s_button_semaphore == NULL) {
        ESP_LOGE(TAG, "Failed to create button semaphore");
        return ESP_ERR_NO_MEM;
    }
    
    // 配置GPIO
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << BUTTON_PIN),  // 选择GPIO41
        .mode = GPIO_MODE_INPUT,               // 输入模式
        .pull_up_en = GPIO_PULLUP_ENABLE,      // 启用内部上拉
        .pull_down_en = GPIO_PULLDOWN_DISABLE, // 禁用下拉
        .intr_type = GPIO_INTR_NEGEDGE,        // 下降沿触发中断
    };
    gpio_config(&io_conf);
    
    // 安装GPIO中断处理服务
    gpio_install_isr_service(0);
    
    // 注册中断服务函数
    gpio_isr_handler_add(BUTTON_PIN, button_isr_handler, NULL);
    
    ESP_LOGI(TAG, "Button initialized");
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