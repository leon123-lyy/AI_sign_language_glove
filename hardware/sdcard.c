/**
 * @file sdcard.c
 * @brief SD卡模块实现
 */

#include "sdcard.h"
#include "esp_log.h"
#include "driver/spi_master.h"
#include "driver/sdspi_host.h"
#include "sdmmc_cmd.h"
#include "esp_vfs_fat.h"
#include "driver/gpio.h"
#include "errno.h"

static const char *TAG = "SDCard";

#define MOUNT_POINT "/sdcard"

static bool s_mounted = false;
static sdmmc_card_t* s_card = NULL;

bool sdcard_init(void)
{
    ESP_LOGI(TAG, "Initializing SD card...");

    if (s_mounted) {
        ESP_LOGI(TAG, "SD card already mounted");
        return true;
    }

    spi_bus_config_t bus_cfg = {
        .mosi_io_num = SDCARD_MOSI_PIN,
        .miso_io_num = SDCARD_MISO_PIN,
        .sclk_io_num = SDCARD_SCLK_PIN,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4096,
    };

    esp_err_t ret = spi_bus_initialize(SPI3_HOST, &bus_cfg, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize SPI bus: %s", esp_err_to_name(ret));
        return false;
    }

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.max_freq_khz = 10000;
    
    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs = SDCARD_CS_PIN;
    slot_config.host_id = SPI3_HOST;

    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 512,
    };
    
    ret = esp_vfs_fat_sdspi_mount(MOUNT_POINT, &host, &slot_config, &mount_config, &s_card);
    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            ESP_LOGE(TAG, "Failed to mount filesystem. Card may not be formatted.");
        } else {
            ESP_LOGE(TAG, "Failed to initialize SD card: %s", esp_err_to_name(ret));
        }
        spi_bus_free(SPI3_HOST);
        return false;
    }

    sdmmc_card_print_info(stdout, s_card);
    
    FILE* test = fopen(MOUNT_POINT "/test.txt", "w");
    if (test) {
        fprintf(test, "test\n");
        fclose(test);
        remove(MOUNT_POINT "/test.txt");
        ESP_LOGI(TAG, "Filesystem write test passed");
    } else {
        ESP_LOGE(TAG, "Filesystem write test failed, errno=%d", errno);
        spi_bus_free(SPI3_HOST);
        return false;
    }

    s_mounted = true;
    ESP_LOGI(TAG, "SD card initialized successfully");
    return true;
}

void* sdcard_csv_open(const char* filename, const char* header)
    {
    if (!s_mounted) {
        ESP_LOGE(TAG, "SD card not mounted");
        return NULL;
    }

    char filepath[64];
    snprintf(filepath, sizeof(filepath), "%s/%s", MOUNT_POINT, filename);

    FILE* file = fopen(filepath, "w");
    if (!file) {
        ESP_LOGE(TAG, "Failed to open file: %s, errno=%d (%s)", filepath, errno, strerror(errno));
        return NULL;
    }

    if (header && fprintf(file, "%s\n", header) < 0) {
        ESP_LOGE(TAG, "Failed to write header");
        fclose(file);
        return NULL;
    }

    fflush(file);

    ESP_LOGI(TAG, "Opened CSV file: %s", filepath);
    return (void*)file;
}

bool sdcard_csv_write_line(void* handle, const char* line)
{
    if (!handle || !line) {
        ESP_LOGE(TAG, "Invalid parameters");
        return false;
    }

    FILE* file = (FILE*)handle;
    
    if (fprintf(file, "%s\n", line) < 0) {
        ESP_LOGE(TAG, "Failed to write line to file");
        return false;
    }

    fflush(file);
    return true;
}

void sdcard_csv_close(void* handle)
{
    if (!handle) {
        return;
    }

    FILE* file = (FILE*)handle;
    fflush(file);
    fclose(file);
    ESP_LOGI(TAG, "CSV file closed");
}

bool sdcard_is_mounted(void)
{
    return s_mounted;
}