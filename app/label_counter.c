/**
 * @file label_counter.c
 * @brief 标签计数器模块实现
 */

#include "label_counter.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "LabelCounter";

// 最大支持的标签数量
#define MAX_LABELS 10

// 标签计数结构体
typedef struct {
    char label[32];  // 标签名称
    int count ;       // 当前计数
} label_entry_t;

// 标签表
static label_entry_t s_label_table[MAX_LABELS] = {0};
static int s_label_count = 0;

void label_counter_init(void)
{
    // 清空标签表
    memset(s_label_table, 0, sizeof(s_label_table));
    s_label_count = 0;
    ESP_LOGI(TAG, "Label counter initialized");
}

int label_counter_get_next(const char *label)
{
    if (!label || strlen(label) == 0) {
        ESP_LOGE(TAG, "Invalid label");
        return -1;
    }

    // 查找现有标签
    for (int i = 0; i < s_label_count; i++) {
        if (strcmp(s_label_table[i].label, label) == 0) {
            s_label_table[i].count++;
            ESP_LOGD(TAG, "Label '%s' count: %d", label, s_label_table[i].count);
            return s_label_table[i].count;
        }
    }

    // 创建新标签
    if (s_label_count >= MAX_LABELS) {
        ESP_LOGE(TAG, "Maximum labels exceeded (%d)", MAX_LABELS);
        return -1;
    }

    strncpy(s_label_table[s_label_count].label, label, sizeof(s_label_table[s_label_count].label) - 1);
    s_label_table[s_label_count].count = 1;
    s_label_count++;

    ESP_LOGD(TAG, "Created new label '%s', count: 1", label);
    return 1;
}

int label_counter_get_current(const char *label)
{
    if (!label || strlen(label) == 0) {
        return 0;
    }

    for (int i = 0; i < s_label_count; i++) {
        if (strcmp(s_label_table[i].label, label) == 0) {
            return s_label_table[i].count;
        }
    }

    return 0;
}

void label_counter_reset(const char *label)
{
    if (!label || strlen(label) == 0) {
        return;
    }

    for (int i = 0; i < s_label_count; i++) {
        if (strcmp(s_label_table[i].label, label) == 0) {
            s_label_table[i].count = 0;
            ESP_LOGI(TAG, "Reset label '%s'", label);
            return;
        }
    }
}

void label_counter_set(const char *label, int start_count)
{
    if (!label || strlen(label) == 0) {
        return;
    }

    if (start_count < 0) {
        start_count = 0;
    }

    // 查找现有标签并设置
    for (int i = 0; i < s_label_count; i++) {
        if (strcmp(s_label_table[i].label, label) == 0) {
            s_label_table[i].count = start_count;
            ESP_LOGI(TAG, "Set label '%s' to %d", label, start_count);
            return;
        }
    }

    // 标签不存在则创建并设置
    if (s_label_count >= MAX_LABELS) {
        ESP_LOGE(TAG, "Maximum labels exceeded (%d)", MAX_LABELS);
        return;
    }

    strncpy(s_label_table[s_label_count].label, label, sizeof(s_label_table[s_label_count].label) - 1);
    s_label_table[s_label_count].count = start_count;
    s_label_count++;

    ESP_LOGI(TAG, "Created new label '%s' with count %d", label, start_count);
}

void label_counter_reset_all(void)
{
    memset(s_label_table, 0, sizeof(s_label_table));
    s_label_count = 0;
    ESP_LOGI(TAG, "Reset all labels");
}