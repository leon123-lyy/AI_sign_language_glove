#include "mpu6050.h"
#include "driver/i2c.h"
#include "driver/gpio.h"
#include "esp_log.h"

static const char *TAG = "MPU6050";

// MPU6050寄存器地址
#define MPU6050_REG_PWR_MGMT_1    0x6B    // 电源管理寄存器1
#define MPU6050_REG_PWR_MGMT_2    0x6C    // 电源管理寄存器2
#define MPU6050_REG_SMPLRT_DIV    0x19    // 采样率分频器
#define MPU6050_REG_CONFIG        0x1A    // 配置寄存器
#define MPU6050_REG_GYRO_CONFIG   0x1B    // 陀螺仪配置
#define MPU6050_REG_ACCEL_CONFIG  0x1C    // 加速度计配置
#define MPU6050_REG_ACCEL_XOUT_H  0x3B    // 加速度计X轴高位
#define MPU6050_REG_ACCEL_XOUT_L  0x3C    // 加速度计X轴低位
#define MPU6050_REG_ACCEL_YOUT_H  0x3D    // 加速度计Y轴高位
#define MPU6050_REG_ACCEL_YOUT_L  0x3E    // 加速度计Y轴低位
#define MPU6050_REG_ACCEL_ZOUT_H  0x3F    // 加速度计Z轴高位
#define MPU6050_REG_ACCEL_ZOUT_L  0x40    // 加速度计Z轴低位
#define MPU6050_REG_TEMP_OUT_H    0x41    // 温度高位
#define MPU6050_REG_TEMP_OUT_L    0x42    // 温度低位
#define MPU6050_REG_GYRO_XOUT_H   0x43    // 陀螺仪X轴高位
#define MPU6050_REG_GYRO_XOUT_L   0x44    // 陀螺仪X轴低位
#define MPU6050_REG_GYRO_YOUT_H   0x45    // 陀螺仪Y轴高位
#define MPU6050_REG_GYRO_YOUT_L   0x46    // 陀螺仪Y轴低位
#define MPU6050_REG_GYRO_ZOUT_H   0x47    // 陀螺仪Z轴高位
#define MPU6050_REG_GYRO_ZOUT_L   0x48    // 陀螺仪Z轴低位

// I2C配置
static i2c_port_t s_i2c_num = I2C_NUM_0;
static gpio_num_t s_sda_pin = GPIO_NUM_17;
static gpio_num_t s_scl_pin = GPIO_NUM_18;
static uint8_t s_mpu6050_addr = MPU6050_ADDR_AD0_LOW;
static bool s_mpu6050_ready = false;  // 设备就绪标志

// 写入MPU6050寄存器
static esp_err_t mpu6050_write_reg(uint8_t reg_addr, uint8_t data)
{
    int ret;
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (s_mpu6050_addr << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, reg_addr, true);
    i2c_master_write_byte(cmd, data, true);
    i2c_master_stop(cmd);
    ret = i2c_master_cmd_begin(s_i2c_num, cmd, pdMS_TO_TICKS(10));
    i2c_cmd_link_delete(cmd);
    return ret;
}

static esp_err_t mpu6050_read_reg(uint8_t reg_addr, uint8_t *data, size_t len)
{
    int ret;

    for (int attempt = 0; attempt < 3; attempt++) {
        i2c_cmd_handle_t cmd = i2c_cmd_link_create();
        i2c_master_start(cmd);
        i2c_master_write_byte(cmd, (s_mpu6050_addr << 1) | I2C_MASTER_WRITE, true);
        i2c_master_write_byte(cmd, reg_addr, true);
        i2c_master_start(cmd);
        i2c_master_write_byte(cmd, (s_mpu6050_addr << 1) | I2C_MASTER_READ, true);
        for (size_t i = 0; i < len - 1; i++) {
            i2c_master_read_byte(cmd, data + i, I2C_MASTER_ACK);
        }
        i2c_master_read_byte(cmd, data + len - 1, I2C_MASTER_NACK);
        i2c_master_stop(cmd);
        ret = i2c_master_cmd_begin(s_i2c_num, cmd, pdMS_TO_TICKS(50));
        i2c_cmd_link_delete(cmd);

        if (ret == ESP_OK) {
            return ESP_OK;
        }

        if (attempt < 2) {
            vTaskDelay(pdMS_TO_TICKS(5));
        }
    }

    static int read_fail_count = 0;
    read_fail_count++;
    if (read_fail_count % 10 == 0) {
        ESP_LOGE(TAG, "I2C read error: addr=0x%02X, len=%d, ret=%d (count=%d)",
                 reg_addr, len, ret, read_fail_count);
    }

    return (ret < 0) ? ESP_ERR_INVALID_RESPONSE : ret;
}

static bool mpu6050_wakeup(void)
{
    esp_err_t ret;

    for (int retry = 0; retry < 3; retry++) {
        ret = mpu6050_write_reg(MPU6050_REG_PWR_MGMT_1, 0x00);
        if (ret == ESP_OK) {
            break;
        }
        ESP_LOGW(TAG, "Wakeup PWR_MGMT_1 write failed (retry %d/3), retrying...", retry + 1);
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Wakeup PWR_MGMT_1 failed after 3 attempts");
        return false;
    }

    vTaskDelay(pdMS_TO_TICKS(100));

    ret = mpu6050_write_reg(MPU6050_REG_PWR_MGMT_2, 0x00);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to write PWR_MGMT_2");
        return false;
    }

    vTaskDelay(pdMS_TO_TICKS(20));

    ret = mpu6050_write_reg(MPU6050_REG_SMPLRT_DIV, 0x07);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to write SMPLRT_DIV");
        return false;
    }

    ret = mpu6050_write_reg(MPU6050_REG_CONFIG, 0x04);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to write CONFIG");
        return false;
    }

    ret = mpu6050_write_reg(MPU6050_REG_GYRO_CONFIG, 0x18);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to write GYRO_CONFIG");
        return false;
    }

    ret = mpu6050_write_reg(MPU6050_REG_ACCEL_CONFIG, 0x18);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to write ACCEL_CONFIG");
        return false;
    }

    vTaskDelay(pdMS_TO_TICKS(50));

    uint8_t verify[6];
    ret = mpu6050_read_reg(MPU6050_REG_ACCEL_XOUT_H, verify, 6);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Verification read failed after wakeup");
        return false;
    }

    ESP_LOGI(TAG, "Wakeup + verify OK (accel: %d %d %d)",
             (int16_t)((verify[0] << 8) | verify[1]),
             (int16_t)((verify[2] << 8) | verify[3]),
             (int16_t)((verify[4] << 8) | verify[5]));

    return true;
}

static bool is_known_imu_who_am_i(uint8_t who_am_i)
{
    uint8_t known_values[] = {0x68, 0x69, 0x70, 0x71, 0x72, 0x73};
    for (size_t i = 0; i < sizeof(known_values); i++) {
        if (who_am_i == known_values[i]) {
            return true;
        }
    }
    return false;
}

static const char* get_imu_name(uint8_t who_am_i)
{
    switch (who_am_i) {
        case 0x68: return "MPU6050";
        case 0x69: return "MPU6500";
        case 0x70: return "MPU6000";
        case 0x71: return "MPU6500";
        case 0x72: return "MPU9250";
        case 0x73: return "MPU9150";
        default:   return "Unknown IMU";
    }
}

esp_err_t mpu6050_init(i2c_port_t i2c_num, gpio_num_t sda_pin, gpio_num_t scl_pin)
{
    s_i2c_num = i2c_num;
    s_sda_pin = sda_pin;
    s_scl_pin = scl_pin;
    
    vTaskDelay(pdMS_TO_TICKS(200));
    
    ESP_LOGI(TAG, "Scanning I2C bus for all devices...");
    int found_count = 0;
    
    for (uint8_t addr = 0x08; addr < 0x78; addr++) {
        i2c_cmd_handle_t cmd = i2c_cmd_link_create();
        i2c_master_start(cmd);
        i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_WRITE, true);
        i2c_master_stop(cmd);
        esp_err_t err = i2c_master_cmd_begin(s_i2c_num, cmd, pdMS_TO_TICKS(50));
        i2c_cmd_link_delete(cmd);

        if (err == ESP_OK) {
            found_count++;
            ESP_LOGI(TAG, "I2C device found at address: 0x%02X", addr);

            uint8_t who_am_i = 0;
            uint8_t reg_addr = 0x75;
            i2c_cmd_handle_t read_cmd = i2c_cmd_link_create();
            i2c_master_start(read_cmd);
            i2c_master_write_byte(read_cmd, (addr << 1) | I2C_MASTER_WRITE, true);
            i2c_master_write_byte(read_cmd, reg_addr, true);
            i2c_master_start(read_cmd);
            i2c_master_write_byte(read_cmd, (addr << 1) | I2C_MASTER_READ, true);
            i2c_master_read_byte(read_cmd, &who_am_i, I2C_MASTER_NACK);
            i2c_master_stop(read_cmd);
            esp_err_t who_err = i2c_master_cmd_begin(s_i2c_num, read_cmd, pdMS_TO_TICKS(50));
            i2c_cmd_link_delete(read_cmd);

            if (who_err == ESP_OK) {
                ESP_LOGI(TAG, "  -> WHO_AM_I=0x%02X (%s)", who_am_i, get_imu_name(who_am_i));
            } else {
                ESP_LOGI(TAG, "  -> WHO_AM_I read failed (may not be IMU sensor)");
            }
        }
    }
    ESP_LOGI(TAG, "I2C scan complete, found %d device(s)", found_count);
    
    if (found_count == 0) {
        ESP_LOGE(TAG, "No I2C devices found! Check wiring and power.");
    }
    
    uint8_t addrs[] = {MPU6050_ADDR_AD0_LOW, MPU6050_ADDR_AD0_HIGH};
    bool found = false;
    esp_err_t ret;

    for (int i = 0; i < 2; i++) {
        s_mpu6050_addr = addrs[i];

        uint8_t test_data = 0;
        ret = mpu6050_read_reg(0x75, &test_data, 1);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "No response at I2C addr 0x%02X", s_mpu6050_addr);
            continue;
        }

        if (is_known_imu_who_am_i(test_data)) {
            ESP_LOGI(TAG, "%s found at I2C addr 0x%02X (WHO_AM_I=0x%02X)",
                     get_imu_name(test_data), s_mpu6050_addr, test_data);
            found = true;
            break;
        } else {
            ESP_LOGW(TAG, "Unexpected WHO_AM_I at addr 0x%02X: 0x%02X", s_mpu6050_addr, test_data);
            ESP_LOGW(TAG, "Expected one of: 0x68(MPU6050), 0x69/0x71(MPU6500), 0x70(MPU6000), 0x72(MPU9250), 0x73(MPU9150)");
        }
    }
    
    if (!found) {
        ESP_LOGE(TAG, "MPU6050 not found on I2C bus (SDA: GPIO%d, SCL: GPIO%d)", sda_pin, scl_pin);
        ESP_LOGE(TAG, "Please check: 1) Wiring 2) Pull-up resistors (4.7kΩ) 3) Power supply");
        return ESP_ERR_NOT_FOUND;
    }
    
    if (!mpu6050_wakeup()) {
        ESP_LOGE(TAG, "Failed to wake up MPU6050");
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "MPU6050 initialized successfully");
    ESP_LOGI(TAG, "I2C Address: 0x%02X", s_mpu6050_addr);
    ESP_LOGI(TAG, "I2C Port: %d, SDA: GPIO%d, SCL: GPIO%d", i2c_num, sda_pin, scl_pin);
    
    s_mpu6050_ready = true;
    
    return ESP_OK;
}

bool mpu6050_is_ready(void)
{
    return s_mpu6050_ready;
}

esp_err_t mpu6050_read_data(mpu6050_data_t *data)
{
    uint8_t buffer[14];
    
    esp_err_t ret = mpu6050_read_reg(MPU6050_REG_ACCEL_XOUT_H, buffer, 14);
    if (ret != ESP_OK) {
        return ret;
    }
    
    data->accel_x = (buffer[0] << 8) | buffer[1];
    data->accel_y = (buffer[2] << 8) | buffer[3];
    data->accel_z = (buffer[4] << 8) | buffer[5];
    data->temp = (buffer[6] << 8) | buffer[7];
    data->gyro_x = (buffer[8] << 8) | buffer[9];
    data->gyro_y = (buffer[10] << 8) | buffer[11];
    data->gyro_z = (buffer[12] << 8) | buffer[13];
    
    return ESP_OK;
}

esp_err_t mpu6050_read_accel(int16_t *ax, int16_t *ay, int16_t *az)
{
    uint8_t buffer[6];

    esp_err_t ret = mpu6050_read_reg(MPU6050_REG_ACCEL_XOUT_H, buffer, 6);
    if (ret != ESP_OK) {
        *ax = 0;
        *ay = 0;
        *az = 0;
        s_mpu6050_ready = false;
        return ret;
    }

    s_mpu6050_ready = true;

    *ax = (buffer[0] << 8) | buffer[1];
    *ay = (buffer[2] << 8) | buffer[3];
    *az = (buffer[4] << 8) | buffer[5];

    return ESP_OK;
}

esp_err_t mpu6050_read_gyro(int16_t *gx, int16_t *gy, int16_t *gz)
{
    uint8_t buffer[6];

    esp_err_t ret = mpu6050_read_reg(MPU6050_REG_GYRO_XOUT_H, buffer, 6);
    if (ret != ESP_OK) {
        *gx = 0;
        *gy = 0;
        *gz = 0;
        s_mpu6050_ready = false;
        return ret;
    }

    s_mpu6050_ready = true;

    *gx = (buffer[0] << 8) | buffer[1];
    *gy = (buffer[2] << 8) | buffer[3];
    *gz = (buffer[4] << 8) | buffer[5];

    return ESP_OK;
}

esp_err_t mpu6050_read_temp(int16_t *temp)
{
    uint8_t buffer[2];
    
    esp_err_t ret = mpu6050_read_reg(MPU6050_REG_TEMP_OUT_H, buffer, 2);
    if (ret != ESP_OK) {
        *temp = 0;
        return ret;
    }
    
    *temp = (buffer[0] << 8) | buffer[1];
    
    return ESP_OK;
}

static int s_fail_count = 0;

void mpu6050_reset_fail_count(void)
{
    s_fail_count = 0;
    s_mpu6050_ready = true;
}

bool mpu6050_health_check(void)
{
    uint8_t who_am_i = 0;
    esp_err_t ret = mpu6050_read_reg(0x75, &who_am_i, 1);
    
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Health check: I2C communication failed");
        return false;
    }
    
    if (!is_known_imu_who_am_i(who_am_i)) {
        ESP_LOGE(TAG, "Health check: Unknown WHO_AM_I value 0x%02X", who_am_i);
        return false;
    }
    
    // 尝试读取一次数据，确保设备正常工作
    uint8_t buffer[6];
    ret = mpu6050_read_reg(MPU6050_REG_ACCEL_XOUT_H, buffer, 6);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Health check: Data read failed");
        return false;
    }
    
    ESP_LOGI(TAG, "Health check passed (WHO_AM_I=0x%02X)", who_am_i);
    return true;
}

static bool mpu6050_i2c_bus_recovery(void)
{
    ESP_LOGI(TAG, "Attempting I2C bus recovery...");
    
    // 1. 删除I2C驱动
    i2c_driver_delete(s_i2c_num);
    vTaskDelay(pdMS_TO_TICKS(100));
    
    // 2. 重新初始化I2C驱动
    i2c_config_t i2c_config = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = s_sda_pin,
        .scl_io_num = s_scl_pin,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = 50000,
    };
    
    esp_err_t ret = i2c_param_config(s_i2c_num, &i2c_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C param config failed: %s", esp_err_to_name(ret));
        return false;
    }
    
    ret = i2c_driver_install(s_i2c_num, I2C_MODE_MASTER, 0, 0, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C driver install failed: %s", esp_err_to_name(ret));
        return false;
    }
    
    ESP_LOGI(TAG, "I2C bus recovery successful");
    vTaskDelay(pdMS_TO_TICKS(100));
    return true;
}

bool mpu6050_reinit(void)
{
    ESP_LOGI(TAG, "Attempting MPU6050 re-initialization...");
    
    // 先尝试I2C总线恢复
    if (!mpu6050_i2c_bus_recovery()) {
        ESP_LOGE(TAG, "I2C bus recovery failed");
        return false;
    }
    
    // 重新初始化MPU6050
    if (!mpu6050_wakeup()) {
        ESP_LOGE(TAG, "MPU6050 wakeup failed");
        return false;
    }
    
    s_fail_count = 0;
    s_mpu6050_ready = true;
    ESP_LOGI(TAG, "MPU6050 re-initialization successful");
    return true;
}

esp_err_t mpu6050_read_accel_gyro(int16_t *acc_x, int16_t *acc_y, int16_t *acc_z,
                                   int16_t *gyro_x, int16_t *gyro_y, int16_t *gyro_z)
{
    uint8_t buffer[14];

    esp_err_t ret = mpu6050_read_reg(MPU6050_REG_ACCEL_XOUT_H, buffer, 14);
    if (ret == ESP_OK) {
        *acc_x  = (int16_t)((buffer[0]  << 8) | buffer[1]);
        *acc_y  = (int16_t)((buffer[2]  << 8) | buffer[3]);
        *acc_z  = (int16_t)((buffer[4]  << 8) | buffer[5]);
        *gyro_x = (int16_t)((buffer[8]  << 8) | buffer[9]);
        *gyro_y = (int16_t)((buffer[10] << 8) | buffer[11]);
        *gyro_z = (int16_t)((buffer[12] << 8) | buffer[13]);
        s_mpu6050_ready = true;
        s_fail_count = 0;
        return ESP_OK;
    }

    *acc_x = 0; *acc_y = 0; *acc_z = 0;
    *gyro_x = 0; *gyro_y = 0; *gyro_z = 0;

    s_fail_count++;

    // 立即检查设备是否还在（可能掉电复位）
    if (s_fail_count >= 3) {
        uint8_t who_am_i = 0;
        esp_err_t who_ret = mpu6050_read_reg(0x75, &who_am_i, 1);
        
        if (who_ret == ESP_OK && is_known_imu_who_am_i(who_am_i)) {
            // 设备还在，但可能需要重新初始化
            ESP_LOGW(TAG, "Device still present (WHO_AM_I=0x%02X) but data read failed, re-initializing...", who_am_i);
            if (mpu6050_wakeup()) {
                ESP_LOGI(TAG, "MPU6050 re-initialized successfully");
                s_fail_count = 0;
                s_mpu6050_ready = true;
                return ESP_OK;
            }
        } else {
            ESP_LOGE(TAG, "Device not responding (WHO_AM_I read ret=%d, data=0x%02X)", who_ret, who_am_i);
        }
    }

    if (s_fail_count % 5 == 1) {
        ESP_LOGW(TAG, "MPU6050 read failed (ret=%d, count=%d)", ret, s_fail_count);
    }

    // 10次失败后尝试I2C总线恢复
    if (s_fail_count == 10) {
        ESP_LOGI(TAG, "Attempting I2C bus recovery after 10 consecutive failures...");
        
        if (mpu6050_i2c_bus_recovery()) {
            if (mpu6050_wakeup()) {
                ESP_LOGI(TAG, "MPU6050 re-initialization successful after bus recovery");
                s_fail_count = 0;
                s_mpu6050_ready = true;
            } else {
                ESP_LOGE(TAG, "MPU6050 re-initialization failed after bus recovery");
            }
        } else {
            ESP_LOGE(TAG, "I2C bus recovery failed");
        }
    }

    s_mpu6050_ready = false;
    return ret;
}

void mpu6050_scan_i2c(void)
{
    ESP_LOGI(TAG, "===== I2C Bus Scan =====");
    int found_count = 0;
    
    for (uint8_t addr = 0x08; addr < 0x78; addr++) {
        i2c_cmd_handle_t cmd = i2c_cmd_link_create();
        i2c_master_start(cmd);
        i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_WRITE, true);
        i2c_master_stop(cmd);
        esp_err_t err = i2c_master_cmd_begin(s_i2c_num, cmd, pdMS_TO_TICKS(50));
        i2c_cmd_link_delete(cmd);

        if (err == ESP_OK) {
            found_count++;
            printf("I2C device found at address: 0x%02X\n", addr);

            uint8_t who_am_i = 0;
            uint8_t reg_addr = 0x75;
            i2c_cmd_handle_t read_cmd = i2c_cmd_link_create();
            i2c_master_start(read_cmd);
            i2c_master_write_byte(read_cmd, (addr << 1) | I2C_MASTER_WRITE, true);
            i2c_master_write_byte(read_cmd, reg_addr, true);
            i2c_master_start(read_cmd);
            i2c_master_write_byte(read_cmd, (addr << 1) | I2C_MASTER_READ, true);
            i2c_master_read_byte(read_cmd, &who_am_i, I2C_MASTER_NACK);
            i2c_master_stop(read_cmd);
            esp_err_t who_err = i2c_master_cmd_begin(s_i2c_num, read_cmd, pdMS_TO_TICKS(50));
            i2c_cmd_link_delete(read_cmd);

            if (who_err == ESP_OK) {
                printf("  -> WHO_AM_I=0x%02X (%s)\n", who_am_i, get_imu_name(who_am_i));
            } else {
                printf("  -> WHO_AM_I read failed (may not be IMU sensor)\n");
            }
        }
    }
    
    printf("I2C scan complete, found %d device(s)\n", found_count);
    
    if (found_count == 0) {
        printf("[错误] 没有找到任何I2C设备！请检查：\n");
        printf("  1. 接线是否正确（SDA-GPIO17, SCL-GPIO18）\n");
        printf("  2. 是否连接了4.7kΩ上拉电阻\n");
        printf("  3. MPU6050电源是否正常（3.3V）\n");
        printf("  4. AD0引脚状态（接地=0x68, 接VCC=0x69）\n");
    }
    
    printf("=========================\n");
}

void mpu6050_print_data(void)
{
    mpu6050_data_t data;
    
    if (mpu6050_read_data(&data) == ESP_OK) {
        // 计算温度（单位：摄氏度）
        float temp_c = (data.temp / 340.0) + 36.53;
        
        ESP_LOGI(TAG, "========================================");
        ESP_LOGI(TAG, "MPU6050 Sensor Data:");
        ESP_LOGI(TAG, "Accelerometer (raw):");
        ESP_LOGI(TAG, "  X: %d", data.accel_x);
        ESP_LOGI(TAG, "  Y: %d", data.accel_y);
        ESP_LOGI(TAG, "  Z: %d", data.accel_z);
        ESP_LOGI(TAG, "Gyroscope (raw):");
        ESP_LOGI(TAG, "  X: %d", data.gyro_x);
        ESP_LOGI(TAG, "  Y: %d", data.gyro_y);
        ESP_LOGI(TAG, "  Z: %d", data.gyro_z);
        ESP_LOGI(TAG, "Temperature: %.2f C", temp_c);
        ESP_LOGI(TAG, "========================================");
    } else {
        ESP_LOGE(TAG, "Failed to read MPU6050 data");
    }
}