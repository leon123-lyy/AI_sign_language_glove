#include "potentiometer.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"
#include "hal/adc_types.h"

static const char *TAG = "Potentiometer";

#define POT_R1_CHAN     ADC_CHANNEL_0  /*!< 电位器R1 (GPIO1,  ADC1_CH0) */
#define POT_R2_CHAN     ADC_CHANNEL_1  /*!< 电位器R2 (GPIO2,  ADC1_CH1) */
#define POT_R3_CHAN     ADC_CHANNEL_2  /*!< 电位器R3 (GPIO3,  ADC1_CH2) */
#define POT_R4_CHAN     ADC_CHANNEL_3  /*!< 电位器R4 (GPIO4,  ADC1_CH3) */
#define POT_R5_CHAN     ADC_CHANNEL_4  /*!< 电位器R5 (GPIO5,  ADC1_CH4) */
#define POT_R6_CHAN     ADC_CHANNEL_5  /*!< 电位器R6 (GPIO6,  ADC1_CH5) */
#define POT_R7_CHAN     ADC_CHANNEL_6  /*!< 电位器R7 (GPIO7,  ADC1_CH6) */
#define POT_R8_CHAN     ADC_CHANNEL_7  /*!< 电位器R8 (GPIO8,  ADC1_CH7) */
#define POT_R9_CHAN     ADC_CHANNEL_8  /*!< 电位器R9 (GPIO9,  ADC1_CH8) */
#define POT_R10_CHAN    ADC_CHANNEL_9  /*!< 电位器R10 (GPIO10, ADC1_CH9) */

static adc_oneshot_unit_handle_t adc1_handle = NULL;

static const adc_channel_t pot_channels[] = {
    POT_R1_CHAN,
    POT_R2_CHAN,
    POT_R3_CHAN,
    POT_R4_CHAN,
    POT_R5_CHAN,
    POT_R6_CHAN,
    POT_R7_CHAN,
    POT_R8_CHAN,
    POT_R9_CHAN,
    POT_R10_CHAN
};

static const char *finger_names[] = {
    "拇指", "食指", "中指", "无名指", "小指",
    "R6", "R7", "R8", "R9", "R10"
};

esp_err_t potentiometer_init(void)
{
    adc_oneshot_unit_init_cfg_t adc1_config = {
        .unit_id = ADC_UNIT_1,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&adc1_config, &adc1_handle));

    adc_oneshot_chan_cfg_t chan_config = {
        .atten = ADC_ATTEN_DB_11,
        .bitwidth = ADC_BITWIDTH_12,
    };

    for (int i = 0; i < POTENTIOMETER_COUNT; i++) {
        ESP_ERROR_CHECK(adc_oneshot_config_channel(adc1_handle, pot_channels[i], &chan_config));
    }

    ESP_LOGI(TAG, "Potentiometer ADC initialized successfully (10 channels)");
    ESP_LOGI(TAG, "Pinout: R1=GPIO1 R2=GPIO2 R3=GPIO3 R4=GPIO4 R5=GPIO5 R6=GPIO6 R7=GPIO7 R8=GPIO8 R9=GPIO9 R10=GPIO10");

    return ESP_OK;
}

void potentiometer_read_all(int *values)
{
    for (int i = 0; i < POTENTIOMETER_COUNT; i++) {
        ESP_ERROR_CHECK(adc_oneshot_read(adc1_handle, pot_channels[i], &values[i]));
    }
}

int potentiometer_read_single(int index)
{
    if (index < 0 || index >= POTENTIOMETER_COUNT) {
        ESP_LOGE(TAG, "Invalid potentiometer index: %d", index);
        return -1;
    }

    int value = 0;
    ESP_ERROR_CHECK(adc_oneshot_read(adc1_handle, pot_channels[index], &value));
    return value;
}

void potentiometer_print_all(void)
{
    int values[POTENTIOMETER_COUNT] = {0};
    potentiometer_read_all(values);

    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "Real-time Potentiometer Values (12-bit ADC):");
    for (int i = 0; i < POTENTIOMETER_COUNT; i++) {
        ESP_LOGI(TAG, "%s: %d", finger_names[i], values[i]);
    }
    ESP_LOGI(TAG, "========================================");
}