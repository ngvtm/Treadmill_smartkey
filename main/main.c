#include "esp_log.h"
#include "esp_task_wdt.h"
#include "esp_idf_version.h"
#include "nvs_flash.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "console/console.h"
#include "services/gap/ble_svc_gap.h"
#include "blecent.h"
#include "host/ble_sm.h"
#include <string.h>

#include "ble_client/ble_client.h"
#include "treadmill_ctrl/treadmill_ctrl.h"
#include "button/button.h"
#include "lcd/lcd_display.h"

static const char *tag = "NimBLE_BLE_CENT";
static int blecent_gap_event(struct ble_gap_event *event, void *arg);

void ble_store_config_init(void);

#if MYNEWT_VAL(BLE_GATTC)
static const ble_uuid_t *remote_svc_uuid =
    BLE_UUID128_DECLARE(0x2d, 0x71, 0xa2, 0x59, 0xb4, 0x58, 0xc8, 0x12,
                        0x99, 0x99, 0x43, 0x95, 0x12, 0x2f, 0x46, 0x59);

static const ble_uuid_t *remote_chr_uuid =
    BLE_UUID128_DECLARE(0x00, 0x00, 0x00, 0x00, 0x11, 0x11, 0x11, 0x11,
                        0x22, 0x22, 0x22, 0x22, 0x33, 0x33, 0x33, 0x33);
#endif

/* Phase 1, S1.3: a quick press can only stop the machine; starting it
 * needs the HOLD event (toggle held >= 1.5 s). Speed buttons act on
 * PRESS only — a held speed button does not repeat steps. */
static void on_button_event(button_id_t id, button_event_t event)
{
    switch (id) {
    case BUTTON_ID_TOGGLE:
        if (event == BTN_EV_PRESS) {
            if (treadmill_ctrl_is_control_active() ||
                treadmill_ctrl_is_running()) {
                treadmill_ctrl_request_off();
            } else {
                ESP_LOGI(tag, "Quick toggle press while stopped: hold 1.5 s to start");
            }
        } else if (event == BTN_EV_HOLD) {
            treadmill_ctrl_request_on();
        }
        break;
    case BUTTON_ID_SPEED_UP:
        if (event == BTN_EV_PRESS) {
            treadmill_ctrl_speed_up();
        }
        break;
    case BUTTON_ID_SPEED_DOWN:
        if (event == BTN_EV_PRESS) {
            treadmill_ctrl_speed_down();
        }
        break;
    default:
        break;
    }
}

#if MYNEWT_VAL(BLE_GATTC)
static int
blecent_on_custom_subscribe(uint16_t conn_handle,
                            const struct ble_gatt_error *error,
                            struct ble_gatt_attr *attr,
                            void *arg)
{
    const struct peer_chr *chr;
    const struct peer     *peer;

    MODLOG_DFLT(INFO, "Subscribe complete; status=%d conn_handle=%d\n",
                error->status, conn_handle);

    if (error->status != 0) {
        MODLOG_DFLT(ERROR, "Subscribe failed, status=%d\n", error->status);
        return 0;
    }

    peer = peer_find(conn_handle);
    if (peer == NULL) {
        MODLOG_DFLT(ERROR, "peer_find failed\n");
        return 0;
    }

    chr = peer_chr_find_uuid(peer, remote_svc_uuid, remote_chr_uuid);
    if (chr == NULL) {
        MODLOG_DFLT(ERROR, "Cannot find treadmill characteristic\n");
        ble_gap_terminate(conn_handle, BLE_ERR_REM_USER_CONN_TERM);
        return 0;
    }

    ble_client_set_connection(conn_handle, chr->chr.val_handle);
    MODLOG_DFLT(INFO, "★ Treadmill BLE ready! val_handle=%d\n",
                chr->chr.val_handle);
    return 0;
}

static void
blecent_subscribe_treadmill(const struct peer *peer)
{
    const struct peer_dsc *dsc;
    const struct peer_chr *chr;
    uint8_t value[2] = {1, 0};
    int rc;

    dsc = peer_dsc_find_uuid(peer,
                             remote_svc_uuid,
                             remote_chr_uuid,
                             BLE_UUID16_DECLARE(BLE_GATT_DSC_CLT_CFG_UUID16));
    if (dsc == NULL) {
        /* Không có CCCD — bỏ qua subscribe, vẫn set handle để write được */
        MODLOG_DFLT(WARN, "No CCCD found, skip subscribe\n");
        chr = peer_chr_find_uuid(peer, remote_svc_uuid, remote_chr_uuid);
        if (chr) {
            ble_client_set_connection(peer->conn_handle, chr->chr.val_handle);
            MODLOG_DFLT(INFO, "★ Treadmill BLE ready (no notify)! val_handle=%d\n",
                        chr->chr.val_handle);
        }
        return;
    }

    rc = ble_gattc_write_flat(peer->conn_handle, dsc->dsc.handle,
                              value, sizeof(value),
                              blecent_on_custom_subscribe, NULL);
    if (rc != 0) {
        MODLOG_DFLT(ERROR, "Failed to subscribe; rc=%d\n", rc);
        ble_gap_terminate(peer->conn_handle, BLE_ERR_REM_USER_CONN_TERM);
    }
}

static void
blecent_on_disc_complete(const struct peer *peer, int status, void *arg)
{
    if (status != 0) {
        MODLOG_DFLT(ERROR, "Service discovery failed; status=%d\n", status);
        ble_gap_terminate(peer->conn_handle, BLE_ERR_REM_USER_CONN_TERM);
        return;
    }

    MODLOG_DFLT(INFO, "Service discovery complete\n");
    blecent_subscribe_treadmill(peer);
}
#endif /* MYNEWT_VAL(BLE_GATTC) */

static void
blecent_scan(void)
{
    uint8_t own_addr_type;
    struct ble_gap_disc_params disc_params = {0};
    int rc;

    rc = ble_hs_id_infer_auto(0, &own_addr_type);
    if (rc != 0) {
        MODLOG_DFLT(ERROR, "error determining address type; rc=%d\n", rc);
        return;
    }

    disc_params.filter_duplicates = 1;
    disc_params.passive           = 0;   /* active scan */
    disc_params.itvl              = 0;
    disc_params.window            = 0;
    disc_params.filter_policy     = 0;
    disc_params.limited           = 0;

    rc = ble_gap_disc(own_addr_type, BLE_HS_FOREVER, &disc_params,
                      blecent_gap_event, NULL);
    if (rc != 0) {
        MODLOG_DFLT(ERROR, "Error initiating GAP discovery; rc=%d\n", rc);
        lcd_display_set_link(LCD_LINK_DISCONNECTED);
    } else {
        lcd_display_set_link(LCD_LINK_SCANNING);
    }
}

/* Kiểm tra địa chỉ quảng cáo có phải peer đã bond không, và có bond
 * nào đang tồn tại hay không. Store được đọc trực tiếp mỗi lượt quét:
 * số bond rất ít (thường là 1) nên chi phí không đáng kể. */
static int
blecent_bonded_match(const ble_addr_t *addr, int *out_have_bond)
{
    ble_addr_t addrs[8];
    int num = 0;
    int i;

    *out_have_bond = 0;
    if (ble_store_util_bonded_peers(addrs, &num, 8) != 0) {
        return 0;
    }
    *out_have_bond = (num > 0);
    for (i = 0; i < num; i++) {
        if (ble_addr_cmp(addr, &addrs[i]) == 0) {
            return 1;
        }
    }
    return 0;
}

static int
blecent_should_connect(const struct ble_gap_disc_desc *disc)
{
    struct ble_hs_adv_fields fields;
    int rc;
    int have_bond = 0;

    if (disc->event_type != BLE_HCI_ADV_RPT_EVTYPE_ADV_IND &&
        disc->event_type != BLE_HCI_ADV_RPT_EVTYPE_DIR_IND) {
        return 0;
    }

    /* Đã cặp với một server rồi: chỉ kết nối lại đúng server đó,
     * bỏ qua mọi thiết bị khác kể cả thiết bị quảng cáo cùng UUID
     * dịch vụ. Chưa có bond: rơi xuống tiêu chí UUID như trước để
     * thực hiện pairing lần đầu. */
    if (blecent_bonded_match(&disc->addr, &have_bond)) {
        return 1;
    }
    if (have_bond) {
        return 0;
    }

    rc = ble_hs_adv_parse_fields(&fields, disc->data, disc->length_data);
    if (rc != 0) return 0;

    for (int i = 0; i < fields.num_uuids16; i++) {
        if (ble_uuid_u16(&fields.uuids16[i].u) == BLECENT_SVC_ALERT_UUID) {
            return 1;
        }
    }
    return 0;
}

static void
blecent_connect_if_interesting(void *disc)
{
    uint8_t    own_addr_type;
    int        rc;
    ble_addr_t *addr;

    if (!blecent_should_connect((struct ble_gap_disc_desc *)disc)) {
        return;
    }

    rc = ble_gap_disc_cancel();
    if (rc != 0) {
        MODLOG_DFLT(DEBUG, "Failed to cancel scan; rc=%d\n", rc);
        return;
    }

    rc = ble_hs_id_infer_auto(0, &own_addr_type);
    if (rc != 0) {
        MODLOG_DFLT(ERROR, "error determining address type; rc=%d\n", rc);
        return;
    }

    addr = &((struct ble_gap_disc_desc *)disc)->addr;

    /*
     * Service discovery là chuỗi hàng chục lượt hỏi-đáp ATT, mỗi lượt tốn
     * ít nhất một connection interval. Ở 80-100 ms thì toàn bộ quá trình
     * mất hơn 15 giây, vượt xa supervision timeout 4 giây -> link chết
     * giữa chừng và discovery trả về BLE_HS_ENOTCONN (status=7).
     *
     * 15-30 ms rút thời gian discovery xuống còn khoảng một phần năm, và
     * timeout 10 giây cho phép peer chậm vẫn kịp trả lời.
     */
    struct ble_gap_conn_params conn_params = {
        .scan_itvl           = 0x0010,
        .scan_window         = 0x0010,
        .itvl_min            = BLE_GAP_CONN_ITVL_MS(15),
        .itvl_max            = BLE_GAP_CONN_ITVL_MS(30),
        .latency             = 0,
        .supervision_timeout = BLE_GAP_SUPERVISION_TIMEOUT_MS(10000),
        .min_ce_len          = 0,
        .max_ce_len          = 0,
    };

    rc = ble_gap_connect(own_addr_type, addr, 10000,
                         &conn_params,
                         blecent_gap_event, NULL);
    if (rc != 0) {
        MODLOG_DFLT(ERROR, "Failed to connect; rc=%d\n", rc);
    }
}

static int
blecent_gap_event(struct ble_gap_event *event, void *arg)
{
    int rc;
#if NIMBLE_BLE_CONNECT
    struct ble_gap_conn_desc desc;
#endif

    switch (event->type) {

    case BLE_GAP_EVENT_DISC:
        blecent_connect_if_interesting(&event->disc);
        return 0;

#if NIMBLE_BLE_CONNECT
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0) {
            MODLOG_DFLT(INFO, "Connection established!\n");
            rc = ble_gap_conn_find(event->connect.conn_handle, &desc);
            assert(rc == 0);

            rc = peer_add(event->connect.conn_handle);
            if (rc != 0) {
                MODLOG_DFLT(ERROR, "Failed to add peer; rc=%d\n", rc);
                return 0;
            }

            /* Mã hoá trước, discovery sau: characteristic điều khiển
             * bên server yêu cầu link đã mã hoá (WRITE_ENC). Lần đầu
             * kết nối, security_initiate chạy pairing và bond được
             * lưu; lần sau LTK đã lưu được dùng lại. Discovery được
             * bắt đầu ở BLE_GAP_EVENT_ENC_CHANGE khi mã hoá xong. */
            rc = ble_gap_security_initiate(event->connect.conn_handle);
            if (rc != 0) {
                MODLOG_DFLT(ERROR, "Failed to initiate security; rc=%d\n", rc);
                ble_gap_terminate(event->connect.conn_handle,
                                  BLE_ERR_REM_USER_CONN_TERM);
            }
        } else {
            MODLOG_DFLT(ERROR, "Connection failed; status=%d\n",
                        event->connect.status);
            blecent_scan();
        }
        return 0;

    case BLE_GAP_EVENT_DISCONNECT:
        MODLOG_DFLT(INFO, "disconnect; reason=%d\n",
                    event->disconnect.reason);
        ble_client_clear_connection();
        treadmill_ctrl_init();
        peer_delete(event->disconnect.conn.conn_handle);
        /* Tự động scan lại để reconnect */
        blecent_scan();
        return 0;

    case BLE_GAP_EVENT_ENC_CHANGE:
        MODLOG_DFLT(INFO, "encryption change; status=%d\n",
                    event->enc_change.status);
        if (event->enc_change.status != 0) {
            MODLOG_DFLT(ERROR, "encryption failed, terminate connection\n");
            ble_gap_terminate(event->enc_change.conn_handle,
                              BLE_ERR_REM_USER_CONN_TERM);
            return 0;
        }
#if MYNEWT_VAL(BLE_GATTC)
        rc = peer_disc_all(event->enc_change.conn_handle,
                           blecent_on_disc_complete, NULL);
        if (rc != 0) {
            MODLOG_DFLT(ERROR, "Failed to discover services; rc=%d\n", rc);
        }
#endif
        return 0;

    case BLE_GAP_EVENT_DISC_COMPLETE:
        MODLOG_DFLT(INFO, "discovery complete; reason=%d\n",
                    event->disc_complete.reason);
        return 0;

    case BLE_GAP_EVENT_MTU:
        MODLOG_DFLT(INFO, "mtu update; conn_handle=%d mtu=%d\n",
                    event->mtu.conn_handle,
                    event->mtu.value);
        return 0;

    case BLE_GAP_EVENT_NOTIFY_RX: {
        uint8_t buf[sizeof(treadmill_status_msg_t)];
        uint16_t len = 0;

        rc = ble_hs_mbuf_to_flat(event->notify_rx.om,
                                 buf,
                                 sizeof(buf),
                                 &len);
        if (rc != 0) {
            MODLOG_DFLT(ERROR, "notify_rx parse failed rc=%d\n", rc);
            return 0;
        }

        treadmill_ctrl_handle_status(buf, len);
        return 0;
    }
#endif
    }
    return 0;
}

static void
blecent_on_reset(int reason)
{
    MODLOG_DFLT(ERROR, "Resetting state; reason=%d\n", reason);
}

static void
blecent_on_sync(void)
{
    int rc;

    rc = ble_hs_util_ensure_addr(0);
    assert(rc == 0);

    /* ★ Gọi SAU ensure_addr.
     * KHÔNG gọi ble_store_clear() ở đây nữa: hàm đó xoá toàn bộ bond
     * đã lưu mỗi lần boot — gốc rễ của lỗi "pairing không lưu được
     * key". Store phải sống qua khởi động lại; chỉ xoá khi unpair
     * chủ động. */

    blecent_scan();
}

void blecent_host_task(void *param)
{
    ESP_LOGI(tag, "BLE Host Task Started");
    nimble_port_run();
    nimble_port_freertos_deinit();
}

void
app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Phase 1, S1.5: task watchdog for the LCD, button and ctrl tick
    // tasks (2 s: all three loop in <= 1 s even when idle). A hung
    // render or button task resets the key instead of freezing it
    // with OFF unreachable. Same boundary as the server: this does
    // not replace the server-side hardware interlock.
#ifndef CONFIG_TREADMILL_SMARTKEY_WDT_TIMEOUT_S
#define CONFIG_TREADMILL_SMARTKEY_WDT_TIMEOUT_S 2
#endif
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
    {
        esp_task_wdt_config_t wdt_cfg = {
            .timeout_ms = CONFIG_TREADMILL_SMARTKEY_WDT_TIMEOUT_S * 1000,
            .idle_core_mask = 0,
            .trigger_panic = true,
        };
        ESP_ERROR_CHECK(esp_task_wdt_init(&wdt_cfg));
    }
#else
    ESP_ERROR_CHECK(esp_task_wdt_init(CONFIG_TREADMILL_SMARTKEY_WDT_TIMEOUT_S, true));
#endif
    ESP_LOGI(tag, "task watchdog armed: %d s (lcd, button, ctrl_tick)",
             CONFIG_TREADMILL_SMARTKEY_WDT_TIMEOUT_S);

    /* Bật màn hình trước BLE để có phản hồi ngay khi cấp nguồn.
     * Lỗi màn hình không được chặn phần điều khiển: smartkey vẫn phải
     * bấm nút gửi lệnh được. */
    ret = lcd_display_init();
    if (ret != ESP_OK) {
        ESP_LOGE(tag, "LCD init failed (%s), chay tiep khong co man hinh",
                 esp_err_to_name(ret));
    }

    ret = nimble_port_init();
    if (ret != ESP_OK) {
        ESP_LOGE(tag, "Failed to init nimble %d ", ret);
        return;
    }

    ble_hs_cfg.reset_cb        = blecent_on_reset;
    ble_hs_cfg.sync_cb         = blecent_on_sync;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;

    int m;
#if MYNEWT_VAL(BLE_INCL_SVC_DISCOVERY) || \
    MYNEWT_VAL(BLE_GATT_CACHING_INCLUDE_SERVICES)
    m = peer_init(MYNEWT_VAL(BLE_MAX_CONNECTIONS), 64, 64, 64, 64);
#else
    m = peer_init(MYNEWT_VAL(BLE_MAX_CONNECTIONS), 64, 64, 64);
#endif
    assert(m == 0);

    /* Bảo mật: bật bonding + LE Secure Connections và phân phối khoá
     * ENC + ID ở cả 2 phía (phải khớp cấu hình bên server). Chỉ khi
     * key_dist != 0 thì LTK mới được trao và lưu vào store. MITM = 0
     * (Just Works) cho phase này. */
    ble_hs_cfg.sm_bonding        = 1;
    ble_hs_cfg.sm_mitm           = 0;
    ble_hs_cfg.sm_sc             = 1;
    ble_hs_cfg.sm_io_cap         = BLE_HS_IO_NO_INPUT_OUTPUT;
    ble_hs_cfg.sm_our_key_dist   = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.sm_their_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;

#if CONFIG_BT_NIMBLE_GAP_SERVICE
    m = ble_svc_gap_device_name_set("treadmill-smartkey");
    assert(m == 0);
#endif

    ble_store_config_init();

    ble_client_init();
    treadmill_ctrl_init();
    button_init(on_button_event);

    nimble_port_freertos_init(blecent_host_task);
}
