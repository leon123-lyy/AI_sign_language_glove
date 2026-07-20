/**
 * @file ble_notify.c
 * @brief BLE通知模块 — 通过NimBLE GATT Notification推送手势信息到手机
 * @details 基于 ESP-IDF v5.5.4 bleprph 示例，提供最简化的通知接口：
 *          - ble_notify_init()   — 初始化BLE、注册GATT服务、开始广播
 *          - ble_notify_send()   — 向已连接客户端发送Notification
 */

#include "ble_notify.h"
#include <string.h>
#include <stdio.h>
#include "esp_log.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/* NimBLE 头文件 — 与 ESP-IDF bleprph 示例完全一致 */
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

static const char *TAG = "BLE_Notify";

/*===========================================================================
 * UUID 定义
 *===========================================================================*/

/** 自定义 GATT 服务 128-bit UUID */
static const ble_uuid128_t s_svc_uuid =
    BLE_UUID128_INIT(0x4b, 0x91, 0x31, 0xc3, 0xc9, 0xc5, 0xcc, 0x8f,
                     0x9e, 0x45, 0xb5, 0x1f, 0x01, 0xc2, 0xaf, 0x4f);

/** 自定义 Characteristic 128-bit UUID (NOTIFY) */
static const ble_uuid128_t s_chr_uuid =
    BLE_UUID128_INIT(0xa8, 0x26, 0x1b, 0x36, 0xea, 0x07, 0xf5, 0xb7,
                     0x88, 0x46, 0x36, 0xe1, 0x3e, 0x48, 0xeb, 0xbe);

/*===========================================================================
 * 状态管理
 *===========================================================================*/

/** 当前连接句柄，BLE_HS_CONN_HANDLE_NONE(0xffff) 表示未连接 */
static uint16_t s_conn_handle  = BLE_HS_CONN_HANDLE_NONE;

/** 客户端是否已启用 Notification (CCCD 订阅状态) */
static bool s_notify_enabled    = false;

/** Characteristic 的值属性句柄（服务注册后由 NimBLE 分配） */
static uint16_t s_chr_val_handle = 0;

/*===========================================================================
 * 前向声明
 *===========================================================================*/

static int  ble_gap_event_cb(struct ble_gap_event *event, void *arg);
static int  gatt_svc_access_cb(uint16_t conn_handle, uint16_t attr_handle,
                               struct ble_gatt_access_ctxt *ctxt, void *arg);
static void bleprph_on_reset(int reason);
static void bleprph_on_sync(void);
static void start_advertising(void);
static void nimble_host_task(void *param);

/*===========================================================================
 * GATT 服务列表定义（与官方示例格式完全一致）
 *===========================================================================*/

static const struct ble_gatt_svc_def s_gatt_services[] = {
    {
        /* 主服务 */
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &s_svc_uuid.u,
        .characteristics = (struct ble_gatt_chr_def[]){
            {
                .uuid       = &s_chr_uuid.u,
                .access_cb  = gatt_svc_access_cb,
                .flags      = BLE_GATT_CHR_F_NOTIFY,
                .val_handle = &s_chr_val_handle,
            },
            { 0 }   /* 特征值列表终止 */
        },
    },
    { 0 }   /* 服务列表终止 */
};

/*===========================================================================
 * GATT 访问回调（处理 CCCD 读写）
 *===========================================================================*/

static int gatt_svc_access_cb(uint16_t conn_handle, uint16_t attr_handle,
                               struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    switch (ctxt->op) {
    case BLE_GATT_ACCESS_OP_READ_CHR:
        /* 读取 Characteristic — 返回空（我们不提供 Read 属性，但回调仍需处理） */
        break;

    case BLE_GATT_ACCESS_OP_WRITE_CHR:
        /* 写入 CCCD 时触发，检查是否启用了 Notification */
        if (ctxt->chr->flags & BLE_GATT_CHR_F_NOTIFY) {
            uint8_t data[2] = {0};
            uint16_t om_len = OS_MBUF_PKTLEN(ctxt->om);
            if (om_len >= 2) {
                os_mbuf_copydata(ctxt->om, 0, 2, data);
                s_notify_enabled = (data[0] & 0x01) != 0;
                ESP_LOGD(TAG, "Notification %s", s_notify_enabled ? "ON" : "OFF");
            }
        }
        return 0;

    default:
        break;
    }
    return BLE_ATT_ERR_UNLIKELY;
}

/*===========================================================================
 * GATT 服务注册回调（注册时由 NimBLE 调用）— 完全按官方示例
 *===========================================================================*/

static void gatt_svr_register_cb(struct ble_gatt_register_ctxt *ctxt, void *arg)
{
    char buf[BLE_UUID_STR_LEN];
    switch (ctxt->op) {
    case BLE_GATT_REGISTER_OP_SVC:
        ESP_LOGD(TAG, "registered service %s handle=%d",
                 ble_uuid_to_str(ctxt->svc.svc_def->uuid, buf),
                 ctxt->svc.handle);
        break;
    case BLE_GATT_REGISTER_OP_CHR:
        ESP_LOGD(TAG, "registered characteristic %s def_handle=%d val_handle=%d",
                 ble_uuid_to_str(ctxt->chr.chr_def->uuid, buf),
                 ctxt->chr.def_handle,
                 ctxt->chr.val_handle);
        break;
    default:
        break;
    }
}

/*===========================================================================
 * 广播控制
 *===========================================================================*/

static void start_advertising(void)
{
    struct ble_gap_adv_params adv_params = {0};
    struct ble_hs_adv_fields  fields     = {0};
    int rc;

    /* 广播标志：通用发现 + 仅BLE */
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;

    /* 设备名称 */
    fields.name               = (uint8_t *)"GestureGlove";
    fields.name_len           = strlen("GestureGlove");
    fields.name_is_complete   = 1;

    rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gap_adv_set_fields: %d", rc);
        return;
    }

    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;

    rc = ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, NULL, BLE_HS_FOREVER,
                           &adv_params, ble_gap_event_cb, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gap_adv_start: %d", rc);
        return;
    }

    ESP_LOGD(TAG, "Advertising started as 'GestureGlove'");
}

/*===========================================================================
 * NimBLE 回调 — 完全按官方示例次序
 *===========================================================================*/

static void bleprph_on_reset(int reason)
{
    ESP_LOGW(TAG, "Resetting state; reason=%d", reason);
}

static void bleprph_on_sync(void)
{
    int rc;
    /* 确保身份地址已设置 */
    rc = ble_hs_util_ensure_addr(0);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_hs_util_ensure_addr: %d", rc);
        return;
    }
    start_advertising();
}

/*===========================================================================
 * GAP 事件处理
 *===========================================================================*/

static int ble_gap_event_cb(struct ble_gap_event *event, void *arg)
{
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0) {
            s_conn_handle = event->connect.conn_handle;
            ESP_LOGI(TAG, "Client connected (handle=%d)", s_conn_handle);
        } else {
            ESP_LOGW(TAG, "Connection failed (status=%d)", event->connect.status);
            s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
            s_notify_enabled = false;
            start_advertising();
        }
        return 0;

    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(TAG, "Client disconnected (reason=%d)", event->disconnect.reason);
        s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
        s_notify_enabled = false;
        start_advertising();
        return 0;

    case BLE_GAP_EVENT_ADV_COMPLETE:
        ESP_LOGI(TAG, "Advertising complete, restarting...");
        start_advertising();
        return 0;

    case BLE_GAP_EVENT_SUBSCRIBE:
        /* 处理 CCCD 订阅事件 */
        if (event->subscribe.attr_handle == s_chr_val_handle) {
            s_notify_enabled = (event->subscribe.cur_notify != 0);
            ESP_LOGI(TAG, "Notification subscription: %s",
                     s_notify_enabled ? "ON" : "OFF");
        }
        return 0;

    case BLE_GAP_EVENT_REPEAT_PAIRING:
        return BLE_GAP_REPEAT_PAIRING_RETRY;

    default:
        return 0;
    }
}

/*===========================================================================
 * NimBLE 主机任务（线程入口）
 *===========================================================================*/

static void nimble_host_task(void *param)
{
    ESP_LOGI(TAG, "NimBLE host task started");
    nimble_port_run();
    nimble_port_freertos_deinit();
    ESP_LOGI(TAG, "NimBLE host task exited");
}

/*===========================================================================
 * 公共接口
 *===========================================================================*/

esp_err_t ble_notify_init(void)
{
    int rc;

    ESP_LOGI(TAG, "Initializing BLE notification module...");

    /* 1. 初始化 NimBLE 协议栈 */
    esp_err_t ret = nimble_port_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "nimble_port_init failed: %d", ret);
        return ret;
    }

    /* 2. 注册 NimBLE 回调 — 必须在 nimble_port_init 之后 */
    ble_hs_cfg.reset_cb          = bleprph_on_reset;
    ble_hs_cfg.sync_cb           = bleprph_on_sync;
    ble_hs_cfg.gatts_register_cb = gatt_svr_register_cb;

    /* 3. 注册 GATT 服务 — 必须在配置完回调之后 */
    rc = ble_gatts_count_cfg(s_gatt_services);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gatts_count_cfg: %d", rc);
        return ESP_FAIL;
    }

    rc = ble_gatts_add_svcs(s_gatt_services);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gatts_add_svcs: %d", rc);
        return ESP_FAIL;
    }

    /* 4. 初始化标准 GAP / GATT 服务 */
    ble_svc_gap_init();
    ble_svc_gatt_init();

    /* 5. 设置设备名称 */
    rc = ble_svc_gap_device_name_set("GestureGlove");
    if (rc != 0) {
        ESP_LOGW(TAG, "Device name set failed: %d", rc);
    }

    /* 6. 启动 NimBLE 主机任务 */
    nimble_port_freertos_init(nimble_host_task);

    ESP_LOGI(TAG, "BLE initialized OK");
    return ESP_OK;
}

esp_err_t ble_notify_send(const char *message)
{
    if (message == NULL || strlen(message) == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    if (s_conn_handle == BLE_HS_CONN_HANDLE_NONE) {
        /* 无连接 — 静默跳过 */
        return ESP_OK;
    }

    if (!s_notify_enabled) {
        /* Notification 未启用 — 静默跳过 */
        return ESP_OK;
    }

    int len = strlen(message);
    struct os_mbuf *om = ble_hs_mbuf_from_flat(message, len);
    if (om == NULL) {
        ESP_LOGE(TAG, "mbuf alloc failed");
        return ESP_ERR_NO_MEM;
    }

    int rc = ble_gattc_notify_custom(s_conn_handle, s_chr_val_handle, om);
    if (rc != 0) {
        ESP_LOGE(TAG, "notify failed: %d", rc);
        return ESP_FAIL;
    }

    ESP_LOGD(TAG, "Sent: \"%s\"", message);
    return ESP_OK;
}
