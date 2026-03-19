/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *  http://www.apache.org/licenses/LICENSE-2.0
 */

#include "esp_log.h"
#include "nvs_flash.h"
/* BLE */
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "console/console.h"
#include "services/gap/ble_svc_gap.h"
#include "blecent.h"
#if MYNEWT_VAL(BLE_GATT_CACHING)
#include "host/ble_esp_gattc_cache.h"
#endif

/* ---- New modules ---- */
#include "ble_client/ble_client.h"
#include "treadmill_ctrl/treadmill_ctrl.h"
#include "button/button.h"

#if CONFIG_EXAMPLE_USE_CI_ADDRESS
#ifdef CONFIG_IDF_TARGET_ESP32
#define TEST_CI_ADDRESS_CHIP_OFFSET (0)
#elif CONFIG_IDF_TARGET_ESP32C2
#define TEST_CI_ADDRESS_CHIP_OFFSET (1)
#elif CONFIG_IDF_TARGET_ESP32C3
#define TEST_CI_ADDRESS_CHIP_OFFSET (2)
#elif CONFIG_IDF_TARGET_ESP32C6
#define TEST_CI_ADDRESS_CHIP_OFFSET (3)
#elif CONFIG_IDF_TARGET_ESP32C5
#define TEST_CI_ADDRESS_CHIP_OFFSET (4)
#elif CONFIG_IDF_TARGET_ESP32H2
#define TEST_CI_ADDRESS_CHIP_OFFSET (5)
#elif CONFIG_IDF_TARGET_ESP32P4
#define TEST_CI_ADDRESS_CHIP_OFFSET (6)
#elif CONFIG_IDF_TARGET_ESP32S3
#define TEST_CI_ADDRESS_CHIP_OFFSET (7)
#elif CONFIG_IDF_TARGET_ESP32C61
#define TEST_CI_ADDRESS_CHIP_OFFSET (8)
#endif
#endif /* CONFIG_EXAMPLE_USE_CI_ADDRESS */

#if MYNEWT_VAL(BLE_GATTC)
/*** UUID của service chứa custom characteristic ***/
static const ble_uuid_t *remote_svc_uuid =
    BLE_UUID128_DECLARE(0x2d, 0x71, 0xa2, 0x59, 0xb4, 0x58, 0xc8, 0x12,
                        0x99, 0x99, 0x43, 0x95, 0x12, 0x2f, 0x46, 0x59);

/*** UUID của custom characteristic ***/
static const ble_uuid_t *remote_chr_uuid =
    BLE_UUID128_DECLARE(0x00, 0x00, 0x00, 0x00, 0x11, 0x11, 0x11, 0x11,
                        0x22, 0x22, 0x22, 0x22, 0x33, 0x33, 0x33, 0x33);
#endif

static const char *tag = "NimBLE_BLE_CENT";
static int blecent_gap_event(struct ble_gap_event *event, void *arg);

#if MYNEWT_VAL(BLE_EATT_CHAN_NUM) > 0
static uint16_t cids[MYNEWT_VAL(BLE_EATT_CHAN_NUM)];
static uint16_t bearers;
#endif

void ble_store_config_init(void);

/* ------------------------------------------------------------------ */
/*  Button callback — bridge button -> treadmill_ctrl                  */
/* ------------------------------------------------------------------ */
static void on_button_pressed(button_id_t id)
{
    switch (id) {
    case BUTTON_ID_TOGGLE:     treadmill_ctrl_toggle();     break;
    case BUTTON_ID_SPEED_UP:   treadmill_ctrl_speed_up();   break;
    case BUTTON_ID_SPEED_DOWN: treadmill_ctrl_speed_down(); break;
    default:                   break;
    }
}

/* ------------------------------------------------------------------ */
/*  GATT Callbacks                                                      */
/* ------------------------------------------------------------------ */
#if MYNEWT_VAL(BLE_GATTC)

static int
blecent_on_custom_subscribe(uint16_t conn_handle,
                            const struct ble_gatt_error *error,
                            struct ble_gatt_attr *attr,
                            void *arg)
{
    const struct peer_chr *chr;
    const struct peer     *peer;

    MODLOG_DFLT(INFO,
                "Subscribe to the custom subscribable characteristic complete; "
                "status=%d conn_handle=%d", error->status, conn_handle);
    if (error->status == 0) {
        MODLOG_DFLT(INFO, " attr_handle=%d value=", attr->handle);
        print_mbuf(attr->om);
    }
    MODLOG_DFLT(INFO, "\n");

    peer = peer_find(conn_handle);
    chr  = peer_chr_find_uuid(peer, remote_svc_uuid, remote_chr_uuid);
    if (chr == NULL) {
        MODLOG_DFLT(ERROR,
                    "Error: Peer doesn't have the subscribable characteristic\n");
        goto err;
    }

    /* Lưu connection info để button task có thể gửi lệnh */
    ble_client_set_connection(conn_handle, chr->chr.val_handle);
    return 0;

err:
    return ble_gap_terminate(peer->conn_handle, BLE_ERR_REM_USER_CONN_TERM);
}

static void
blecent_custom_gatt_operations(const struct peer *peer)
{
    const struct peer_dsc *dsc;
    int     rc;
    uint8_t value[2];

    dsc = peer_dsc_find_uuid(peer,
                             remote_svc_uuid,
                             remote_chr_uuid,
                             BLE_UUID16_DECLARE(BLE_GATT_DSC_CLT_CFG_UUID16));
    if (dsc == NULL) {
        MODLOG_DFLT(ERROR,
                    "Error: Peer lacks a CCCD for the subscribable characteristic\n");
        goto err;
    }

    value[0] = 1;
    value[1] = 0;
    rc = ble_gattc_write_flat(peer->conn_handle, dsc->dsc.handle,
                              value, sizeof(value),
                              blecent_on_custom_subscribe, NULL);
    if (rc != 0) {
        MODLOG_DFLT(ERROR,
                    "Error: Failed to subscribe to the subscribable characteristic; "
                    "rc=%d\n", rc);
        goto err;
    }
    return;

err:
    ble_gap_terminate(peer->conn_handle, BLE_ERR_REM_USER_CONN_TERM);
}

static int
blecent_on_subscribe(uint16_t conn_handle,
                     const struct ble_gatt_error *error,
                     struct ble_gatt_attr *attr,
                     void *arg)
{
    struct peer *peer;

    MODLOG_DFLT(INFO, "Subscribe complete; status=%d conn_handle=%d "
                      "attr_handle=%d\n",
                error->status, conn_handle, attr->handle);

    peer = peer_find(conn_handle);
    if (peer == NULL) {
        MODLOG_DFLT(ERROR, "Error in finding peer, aborting...");
        ble_gap_terminate(conn_handle, BLE_ERR_REM_USER_CONN_TERM);
        return 0;
    }

    blecent_custom_gatt_operations(peer);
    return 0;
}

static int
blecent_on_write(uint16_t conn_handle,
                 const struct ble_gatt_error *error,
                 struct ble_gatt_attr *attr,
                 void *arg)
{
    MODLOG_DFLT(INFO,
                "Write complete; status=%d conn_handle=%d attr_handle=%d\n",
                error->status, conn_handle, attr->handle);

    const struct peer_dsc *dsc;
    uint8_t value[2];
    int rc;
    const struct peer *peer = peer_find(conn_handle);

    dsc = peer_dsc_find_uuid(peer,
                             BLE_UUID16_DECLARE(BLECENT_SVC_ALERT_UUID),
                             BLE_UUID16_DECLARE(BLECENT_CHR_UNR_ALERT_STAT_UUID),
                             BLE_UUID16_DECLARE(BLE_GATT_DSC_CLT_CFG_UUID16));
    if (dsc == NULL) {
        MODLOG_DFLT(ERROR, "Error: Peer lacks a CCCD for the Unread Alert "
                           "Status characteristic\n");
        goto err;
    }

    value[0] = 1;
    value[1] = 0;
    rc = ble_gattc_write_flat(conn_handle, dsc->dsc.handle,
                              value, sizeof value,
                              blecent_on_subscribe, NULL);
    if (rc != 0) {
        MODLOG_DFLT(ERROR, "Error: Failed to subscribe to characteristic; "
                           "rc=%d\n", rc);
        goto err;
    }
    return 0;

err:
    return ble_gap_terminate(peer->conn_handle, BLE_ERR_REM_USER_CONN_TERM);
}

static int
blecent_on_read(uint16_t conn_handle,
                const struct ble_gatt_error *error,
                struct ble_gatt_attr *attr,
                void *arg)
{
    MODLOG_DFLT(INFO, "Read complete; status=%d conn_handle=%d",
                error->status, conn_handle);
    if (error->status == 0) {
        MODLOG_DFLT(INFO, " attr_handle=%d value=", attr->handle);
        print_mbuf(attr->om);
    }
    MODLOG_DFLT(INFO, "\n");

    const struct peer_chr *chr;
    uint8_t value[2];
    int rc;
    const struct peer *peer = peer_find(conn_handle);

    chr = peer_chr_find_uuid(peer,
                             BLE_UUID16_DECLARE(BLECENT_SVC_ALERT_UUID),
                             BLE_UUID16_DECLARE(BLECENT_CHR_ALERT_NOT_CTRL_PT));
    if (chr == NULL) {
        MODLOG_DFLT(ERROR, "Error: Peer doesn't support the Alert "
                           "Notification Control Point characteristic\n");
        goto err;
    }

    value[0] = 99;
    value[1] = 100;
    rc = ble_gattc_write_flat(conn_handle, chr->chr.val_handle,
                              value, sizeof value,
                              blecent_on_write, NULL);
    if (rc != 0) {
        MODLOG_DFLT(ERROR, "Error: Failed to write characteristic; rc=%d\n", rc);
        goto err;
    }
    return 0;

err:
    return ble_gap_terminate(peer->conn_handle, BLE_ERR_REM_USER_CONN_TERM);
}

static void
blecent_read_write_subscribe(const struct peer *peer)
{
    const struct peer_chr *chr;
    int rc;

    chr = peer_chr_find_uuid(peer,
                             BLE_UUID16_DECLARE(BLECENT_SVC_ALERT_UUID),
                             BLE_UUID16_DECLARE(BLECENT_CHR_SUP_NEW_ALERT_CAT_UUID));
    if (chr == NULL) {
        MODLOG_DFLT(ERROR, "Error: Peer doesn't support the Supported New "
                           "Alert Category characteristic\n");
        goto err;
    }

    rc = ble_gattc_read(peer->conn_handle, chr->chr.val_handle,
                        blecent_on_read, NULL);
    if (rc != 0) {
        MODLOG_DFLT(ERROR, "Error: Failed to read characteristic; rc=%d\n", rc);
        goto err;
    }
    return;

err:
    ble_gap_terminate(peer->conn_handle, BLE_ERR_REM_USER_CONN_TERM);
}

static void
blecent_on_disc_complete(const struct peer *peer, int status, void *arg)
{
    if (status != 0) {
        MODLOG_DFLT(ERROR, "Error: Service discovery failed; status=%d "
                           "conn_handle=%d\n", status, peer->conn_handle);
        ble_gap_terminate(peer->conn_handle, BLE_ERR_REM_USER_CONN_TERM);
        return;
    }

    MODLOG_DFLT(INFO, "Service discovery complete; status=%d "
                      "conn_handle=%d\n", status, peer->conn_handle);

    blecent_read_write_subscribe(peer);
}

#endif /* MYNEWT_VAL(BLE_GATTC) */

/* ------------------------------------------------------------------ */
/*  GAP — Scan & Connect                                               */
/* ------------------------------------------------------------------ */
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
    disc_params.passive           = 1;
    disc_params.itvl              = 0;
    disc_params.window            = 0;
    disc_params.filter_policy     = 0;
    disc_params.limited           = 0;

    rc = ble_gap_disc(own_addr_type, BLE_HS_FOREVER, &disc_params,
                      blecent_gap_event, NULL);
    if (rc != 0) {
        MODLOG_DFLT(ERROR,
                    "Error initiating GAP discovery procedure; rc=%d\n", rc);
    }
}

#if CONFIG_EXAMPLE_EXTENDED_ADV
static int
ext_blecent_should_connect(const struct ble_gap_ext_disc_desc *disc)
{
    int offset       = 0;
    int ad_struct_len = 0;
#if CONFIG_EXAMPLE_USE_CI_ADDRESS
    uint32_t *addr_offset;
#endif
    uint8_t  test_addr[6];
    uint32_t peer_addr[6];

    memset(peer_addr, 0x0, sizeof peer_addr);

    if (disc->legacy_event_type != BLE_HCI_ADV_RPT_EVTYPE_ADV_IND &&
        disc->legacy_event_type != BLE_HCI_ADV_RPT_EVTYPE_DIR_IND) {
        return 0;
    }

    if (strlen(CONFIG_EXAMPLE_PEER_ADDR) &&
        (strncmp(CONFIG_EXAMPLE_PEER_ADDR, "ADDR_ANY",
                 strlen("ADDR_ANY")) != 0)) {
#if !CONFIG_EXAMPLE_USE_CI_ADDRESS
        ESP_LOGI(tag, "Peer address from menuconfig: %s",
                 CONFIG_EXAMPLE_PEER_ADDR);
        sscanf(CONFIG_EXAMPLE_PEER_ADDR, "%lx:%lx:%lx:%lx:%lx:%lx",
               &peer_addr[5], &peer_addr[4], &peer_addr[3],
               &peer_addr[2], &peer_addr[1], &peer_addr[0]);
#endif
        for (int i = 0; i < 6; i++) {
            test_addr[i] = (uint8_t)peer_addr[i];
        }
#if CONFIG_EXAMPLE_USE_CI_ADDRESS
        addr_offset  = (uint32_t *)&test_addr[1];
        *addr_offset = atoi(CONFIG_EXAMPLE_PEER_ADDR);
        test_addr[5] = 0xC3;
        test_addr[0] = TEST_CI_ADDRESS_CHIP_OFFSET;
#endif
        if (memcmp(test_addr, disc->addr.val, sizeof(disc->addr.val)) != 0) {
            return 0;
        }
    }

    do {
        ad_struct_len = disc->data[offset];
        if (!ad_struct_len) break;
        if (disc->data[offset]     == 0x03 &&
            disc->data[offset + 1] == 0x03 &&
            disc->data[offset + 2] == 0x18 &&
            disc->data[offset + 3] == 0x11) {
            return 1;
        }
        offset += ad_struct_len + 1;
    } while (offset < disc->length_data);

    return 0;
}
#else
static int
blecent_should_connect(const struct ble_gap_disc_desc *disc)
{
    struct ble_hs_adv_fields fields;
    int rc;
    int i;
#if CONFIG_EXAMPLE_USE_CI_ADDRESS
    uint32_t *addr_offset;
#endif
    uint8_t  test_addr[6];
    uint32_t peer_addr[6];

    memset(peer_addr, 0x0, sizeof peer_addr);

    if (disc->event_type != BLE_HCI_ADV_RPT_EVTYPE_ADV_IND &&
        disc->event_type != BLE_HCI_ADV_RPT_EVTYPE_DIR_IND) {
        return 0;
    }

    rc = ble_hs_adv_parse_fields(&fields, disc->data, disc->length_data);
    if (rc != 0) {
        return 0;
    }

    if (strlen(CONFIG_EXAMPLE_PEER_ADDR) &&
        (strncmp(CONFIG_EXAMPLE_PEER_ADDR, "ADDR_ANY",
                 strlen("ADDR_ANY")) != 0)) {
        ESP_LOGI(tag, "Peer address from menuconfig: %s",
                 CONFIG_EXAMPLE_PEER_ADDR);
#if !CONFIG_EXAMPLE_USE_CI_ADDRESS
        sscanf(CONFIG_EXAMPLE_PEER_ADDR, "%lx:%lx:%lx:%lx:%lx:%lx",
               &peer_addr[5], &peer_addr[4], &peer_addr[3],
               &peer_addr[2], &peer_addr[1], &peer_addr[0]);
        printf("peer--> %lx %lx %lx %lx %lx %lx \n",
               peer_addr[5], peer_addr[4], peer_addr[3],
               peer_addr[2], peer_addr[1], peer_addr[0]);
#endif
        for (int i = 0; i < 6; i++) {
            test_addr[i] = (uint8_t)peer_addr[i];
        }
#if CONFIG_EXAMPLE_USE_CI_ADDRESS
        addr_offset  = (uint32_t *)&test_addr[1];
        *addr_offset = atoi(CONFIG_EXAMPLE_PEER_ADDR);
        test_addr[5] = 0xC3;
        test_addr[0] = TEST_CI_ADDRESS_CHIP_OFFSET;
#endif
        if (memcmp(test_addr, disc->addr.val, sizeof(disc->addr.val)) != 0) {
            return 0;
        }
    }

    for (i = 0; i < fields.num_uuids16; i++) {
        if (ble_uuid_u16(&fields.uuids16[i].u) == BLECENT_SVC_ALERT_UUID) {
            return 1;
        }
    }
    return 0;
}
#endif /* CONFIG_EXAMPLE_EXTENDED_ADV */

static void
blecent_connect_if_interesting(void *disc)
{
    uint8_t    own_addr_type;
    int        rc;
    ble_addr_t *addr;

#if CONFIG_EXAMPLE_EXTENDED_ADV
    if (!ext_blecent_should_connect((struct ble_gap_ext_disc_desc *)disc)) {
        return;
    }
#else
    if (!blecent_should_connect((struct ble_gap_disc_desc *)disc)) {
        return;
    }
#endif

#if !(MYNEWT_VAL(BLE_HOST_ALLOW_CONNECT_WITH_SCAN))
    rc = ble_gap_disc_cancel();
    if (rc != 0) {
        MODLOG_DFLT(DEBUG, "Failed to cancel scan; rc=%d\n", rc);
        return;
    }
#endif

    rc = ble_hs_id_infer_auto(0, &own_addr_type);
    if (rc != 0) {
        MODLOG_DFLT(ERROR, "error determining address type; rc=%d\n", rc);
        return;
    }

#if CONFIG_EXAMPLE_EXTENDED_ADV
    addr = &((struct ble_gap_ext_disc_desc *)disc)->addr;
#else
    addr = &((struct ble_gap_disc_desc *)disc)->addr;
#endif

    rc = ble_gap_connect(own_addr_type, addr, 30000, NULL,
                         blecent_gap_event, NULL);
    if (rc != 0) {
        MODLOG_DFLT(ERROR,
                    "Error: Failed to connect to device; addr_type=%d "
                    "addr=%s; rc=%d\n",
                    addr->type, addr_str(addr->val), rc);
    }
}

#if MYNEWT_VAL(BLE_POWER_CONTROL)
static void blecent_power_control(uint16_t conn_handle)
{
    int rc;
    rc = ble_gap_read_remote_transmit_power_level(conn_handle, 0x01);
    assert(rc == 0);
    rc = ble_gap_set_transmit_power_reporting_enable(conn_handle, 0x01, 0x01);
    assert(rc == 0);
    rc = ble_gap_set_path_loss_reporting_param(conn_handle, 60, 10, 30, 10, 2);
    assert(rc == 0);
    rc = ble_gap_set_path_loss_reporting_enable(conn_handle, 0x01);
    assert(rc == 0);
}
#endif

/* ------------------------------------------------------------------ */
/*  GAP Event Handler                                                  */
/* ------------------------------------------------------------------ */
static int
blecent_gap_event(struct ble_gap_event *event, void *arg)
{
#if NIMBLE_BLE_CONNECT
    struct ble_gap_conn_desc desc;
#endif
    struct ble_hs_adv_fields fields;
#if MYNEWT_VAL(BLE_HCI_VS)
#if MYNEWT_VAL(BLE_POWER_CONTROL)
    struct ble_gap_set_auto_pcl_params params;
#endif
#endif
    int rc;

    switch (event->type) {

    case BLE_GAP_EVENT_DISC:
        rc = ble_hs_adv_parse_fields(&fields, event->disc.data,
                                     event->disc.length_data);
        if (rc != 0) {
            return 0;
        }
        print_adv_fields(&fields);
        blecent_connect_if_interesting(&event->disc);
        return 0;

#if NIMBLE_BLE_CONNECT
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0) {
            MODLOG_DFLT(INFO, "Connection established ");
            rc = ble_gap_conn_find(event->connect.conn_handle, &desc);
            assert(rc == 0);
            print_conn_desc(&desc);
            MODLOG_DFLT(INFO, "\n");

            ble_gap_security_initiate(event->connect.conn_handle);

            rc = peer_add(event->connect.conn_handle);
            if (rc != 0) {
                MODLOG_DFLT(ERROR, "Failed to add peer; rc=%d\n", rc);
                return 0;
            }

#if MYNEWT_VAL(BLE_POWER_CONTROL)
            blecent_power_control(event->connect.conn_handle);
#endif

#if MYNEWT_VAL(BLE_HCI_VS)
#if MYNEWT_VAL(BLE_POWER_CONTROL)
            memset(&params, 0x0, sizeof(struct ble_gap_set_auto_pcl_params));
            params.conn_handle = event->connect.conn_handle;
            rc = ble_gap_set_auto_pcl_param(&params);
            if (rc != 0) {
                MODLOG_DFLT(INFO, "Failed to send VSC %x \n", rc);
                return 0;
            } else {
                MODLOG_DFLT(INFO, "Successfully issued VSC , rc = %d \n", rc);
            }
#endif
#endif

#if CONFIG_EXAMPLE_ENCRYPTION
            rc = ble_gap_security_initiate(event->connect.conn_handle);
            if (rc != 0) {
                MODLOG_DFLT(INFO, "Security could not be initiated, rc = %d\n", rc);
                return ble_gap_terminate(event->connect.conn_handle,
                                         BLE_ERR_REM_USER_CONN_TERM);
            } else {
                MODLOG_DFLT(INFO, "Connection secured\n");
            }
#else
#if MYNEWT_VAL(BLE_GATTC)
            rc = peer_disc_all(event->connect.conn_handle,
                               blecent_on_disc_complete, NULL);
            if (rc != 0) {
                MODLOG_DFLT(ERROR, "Failed to discover services; rc=%d\n", rc);
                return 0;
            }
#endif
#endif
        } else {
            MODLOG_DFLT(ERROR, "Error: Connection failed; status=%d\n",
                        event->connect.status);
            blecent_scan();
        }
        return 0;

    case BLE_GAP_EVENT_DISCONNECT:
        MODLOG_DFLT(INFO, "disconnect; reason=%d ", event->disconnect.reason);
        print_conn_desc(&event->disconnect.conn);
        MODLOG_DFLT(INFO, "\n");

        /* Xóa BLE connection info và reset state treadmill */
        ble_client_clear_connection();
        treadmill_ctrl_init();

        peer_delete(event->disconnect.conn.conn_handle);

#if MYNEWT_VAL(BLE_EATT_CHAN_NUM) > 0
        bearers = 0;
        for (int i = 0; i < MYNEWT_VAL(BLE_EATT_CHAN_NUM); i++) {
            cids[i] = 0;
        }
#endif
        blecent_scan();
        return 0;

    case BLE_GAP_EVENT_DISC_COMPLETE:
        MODLOG_DFLT(INFO, "discovery complete; reason=%d\n",
                    event->disc_complete.reason);
        return 0;

    case BLE_GAP_EVENT_ENC_CHANGE:
        MODLOG_DFLT(INFO, "encryption change event; status=%d ",
                    event->enc_change.status);
        rc = ble_gap_conn_find(event->enc_change.conn_handle, &desc);
        assert(rc == 0);
        print_conn_desc(&desc);
        MODLOG_DFLT(INFO, "\n");
#if !MYNEWT_VAL(BLE_EATT_CHAN_NUM)
#if CONFIG_EXAMPLE_ENCRYPTION && MYNEWT_VAL(BLE_GATTC)
        rc = peer_disc_all(event->enc_change.conn_handle,
                           blecent_on_disc_complete, NULL);
        if (rc != 0) {
            MODLOG_DFLT(ERROR, "Failed to discover services; rc=%d\n", rc);
            return 0;
        }
#endif
#endif
        return 0;

    case BLE_GAP_EVENT_NOTIFY_RX:
        MODLOG_DFLT(INFO, "received %s; conn_handle=%d attr_handle=%d "
                          "attr_len=%d\n",
                    event->notify_rx.indication ? "indication" : "notification",
                    event->notify_rx.conn_handle,
                    event->notify_rx.attr_handle,
                    OS_MBUF_PKTLEN(event->notify_rx.om));
        return 0;

    case BLE_GAP_EVENT_MTU:
        MODLOG_DFLT(INFO, "mtu update event; conn_handle=%d cid=%d mtu=%d\n",
                    event->mtu.conn_handle,
                    event->mtu.channel_id,
                    event->mtu.value);
        return 0;

    case BLE_GAP_EVENT_REPEAT_PAIRING:
        rc = ble_gap_conn_find(event->repeat_pairing.conn_handle, &desc);
        assert(rc == 0);
        ble_store_util_delete_peer(&desc.peer_id_addr);
        return BLE_GAP_REPEAT_PAIRING_RETRY;
#endif /* NIMBLE_BLE_CONNECT */

#if CONFIG_EXAMPLE_EXTENDED_ADV
    case BLE_GAP_EVENT_EXT_DISC:
        ext_print_adv_report(&event->ext_disc);
        blecent_connect_if_interesting(&event->ext_disc);
        return 0;
#endif

#if MYNEWT_VAL(BLE_POWER_CONTROL)
    case BLE_GAP_EVENT_TRANSMIT_POWER:
        MODLOG_DFLT(INFO, "Transmit power event : status=%d conn_handle=%d "
                          "reason=%d phy=%d power_level=%d "
                          "power_level_flag=%d delta=%d",
                    event->transmit_power.status,
                    event->transmit_power.conn_handle,
                    event->transmit_power.reason,
                    event->transmit_power.phy,
                    event->transmit_power.transmit_power_level,
                    event->transmit_power.transmit_power_level_flag,
                    event->transmit_power.delta);
        return 0;

    case BLE_GAP_EVENT_PATHLOSS_THRESHOLD:
        MODLOG_DFLT(INFO, "Pathloss threshold event : conn_handle=%d "
                          "current path loss=%d zone_entered=%d",
                    event->pathloss_threshold.conn_handle,
                    event->pathloss_threshold.current_path_loss,
                    event->pathloss_threshold.zone_entered);
        return 0;
#endif

#if MYNEWT_VAL(BLE_EATT_CHAN_NUM) > 0
    case BLE_GAP_EVENT_EATT: {
        int i;
        MODLOG_DFLT(INFO, "EATT %s : conn_handle=%d cid=%d",
                    event->eatt.status ? "disconnected" : "connected",
                    event->eatt.conn_handle,
                    event->eatt.cid);
        if (event->eatt.status) {
            for (i = 0; i < bearers; i++) {
                if (cids[i] == event->eatt.cid) break;
            }
            while (i < (bearers - 1)) {
                cids[i] = cids[i + 1];
                i++;
            }
            cids[i] = 0;
            return 0;
        }
        cids[bearers] = event->eatt.cid;
        bearers++;
        if (bearers != MYNEWT_VAL(BLE_EATT_CHAN_NUM)) {
            return 0;
        }
        rc = ble_att_set_default_bearer_using_cid(event->eatt.conn_handle,
                                                  cids[0]);
        if (rc != 0) {
            MODLOG_DFLT(INFO, "Cannot set default EATT bearer, rc=%d\n", rc);
            return rc;
        }
#if MYNEWT_VAL(BLE_GATTC)
        rc = peer_disc_all(event->eatt.conn_handle,
                           blecent_on_disc_complete, NULL);
        if (rc != 0) {
            MODLOG_DFLT(ERROR, "Failed to discover services; rc=%d\n", rc);
            return 0;
        }
#endif
        return 0;
    }
#endif /* BLE_EATT_CHAN_NUM */

#if MYNEWT_VAL(BLE_CONN_SUBRATING)
    case BLE_GAP_EVENT_SUBRATE_CHANGE:
        MODLOG_DFLT(INFO, "Subrate change event : conn_handle=%d "
                          "status=%d factor=%d",
                    event->subrate_change.conn_handle,
                    event->subrate_change.status,
                    event->subrate_change.subrate_factor);
        return 0;
#endif

    default:
        return 0;
    }
}

/* ------------------------------------------------------------------ */
/*  NimBLE Host Callbacks                                              */
/* ------------------------------------------------------------------ */
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

#if !CONFIG_EXAMPLE_INIT_DEINIT_LOOP
    blecent_scan();
#endif
}

void blecent_host_task(void *param)
{
    ESP_LOGI(tag, "BLE Host Task Started");
    nimble_port_run();
    nimble_port_freertos_deinit();
}

#if CONFIG_EXAMPLE_INIT_DEINIT_LOOP
static void stack_init_deinit(void)
{
    int rc;
    while (1) {
        vTaskDelay(1000);
        ESP_LOGI(tag, "Deinit host");
        rc = nimble_port_stop();
        if (rc == 0) {
            nimble_port_deinit();
        } else {
            ESP_LOGI(tag, "Nimble port stop failed, rc = %d", rc);
            break;
        }
        vTaskDelay(1000);
        ESP_LOGI(tag, "Init host");
        rc = nimble_port_init();
        if (rc != ESP_OK) {
            ESP_LOGI(tag, "Failed to init nimble %d ", rc);
            break;
        }
        nimble_port_freertos_init(blecent_host_task);
        ESP_LOGI(tag, "Waiting for 1 second");
    }
}
#endif

static void btn_simulate_task(void *arg)
{
    /* Chờ BLE connect và service discovery hoàn tất */
    ESP_LOGI(tag, "[SIM] Chờ kết nối BLE...");
    while (!ble_client_is_ready()) {
        vTaskDelay(pdMS_TO_TICKS(500));
    }
    ESP_LOGI(tag, "[SIM] Đã kết nối, bắt đầu giả lập nút bấm...");
    vTaskDelay(pdMS_TO_TICKS(1000));

    /* ---- Kịch bản test ---- */

    ESP_LOGI(tag, "[SIM] >>> Nhấn nút BẬT/TẮT lần 1 (BẬT máy)");
    on_button_pressed(BUTTON_ID_TOGGLE);
    vTaskDelay(pdMS_TO_TICKS(2000));

    ESP_LOGI(tag, "[SIM] >>> Nhấn nút TĂNG TỐC lần 1");
    on_button_pressed(BUTTON_ID_SPEED_UP);
    vTaskDelay(pdMS_TO_TICKS(1500));

    ESP_LOGI(tag, "[SIM] >>> Nhấn nút TĂNG TỐC lần 2");
    on_button_pressed(BUTTON_ID_SPEED_UP);
    vTaskDelay(pdMS_TO_TICKS(1500));

    ESP_LOGI(tag, "[SIM] >>> Nhấn nút TĂNG TỐC lần 3");
    on_button_pressed(BUTTON_ID_SPEED_UP);
    vTaskDelay(pdMS_TO_TICKS(2000));

    ESP_LOGI(tag, "[SIM] >>> Nhấn nút GIẢM TỐC lần 1");
    on_button_pressed(BUTTON_ID_SPEED_DOWN);
    vTaskDelay(pdMS_TO_TICKS(1500));

    ESP_LOGI(tag, "[SIM] >>> Nhấn nút GIẢM TỐC lần 2");
    on_button_pressed(BUTTON_ID_SPEED_DOWN);
    vTaskDelay(pdMS_TO_TICKS(2000));

    ESP_LOGI(tag, "[SIM] >>> Nhấn nút BẬT/TẮT lần 2 (TẮT máy)");
    on_button_pressed(BUTTON_ID_TOGGLE);
    vTaskDelay(pdMS_TO_TICKS(2000));

    ESP_LOGI(tag, "[SIM] Hoàn thành kịch bản test.");
    vTaskDelete(NULL);
}

/* ------------------------------------------------------------------ */
/*  app_main                                                           */
/* ------------------------------------------------------------------ */
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

    ret = nimble_port_init();
    if (ret != ESP_OK) {
        ESP_LOGE(tag, "Failed to init nimble %d ", ret);
        return;
    }

    /* Cấu hình NimBLE host */
    ble_hs_cfg.reset_cb        = blecent_on_reset;
    ble_hs_cfg.sync_cb         = blecent_on_sync;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;

#if NIMBLE_BLE_CONNECT
    int rc;
#if MYNEWT_VAL(BLE_INCL_SVC_DISCOVERY) || \
    MYNEWT_VAL(BLE_GATT_CACHING_INCLUDE_SERVICES)
    rc = peer_init(MYNEWT_VAL(BLE_MAX_CONNECTIONS), 64, 64, 64, 64);
#else
    rc = peer_init(MYNEWT_VAL(BLE_MAX_CONNECTIONS), 64, 64, 64);
#endif
    assert(rc == 0);
#endif

#if CONFIG_BT_NIMBLE_GAP_SERVICE
    int m = ble_svc_gap_device_name_set("nimble-blecent");
    assert(m == 0);
#endif

    ble_hs_cfg.sm_bonding          = 1;
    ble_hs_cfg.sm_mitm             = 0;
    ble_hs_cfg.sm_sc               = 0;
    ble_hs_cfg.sm_io_cap           = BLE_HS_IO_NO_INPUT_OUTPUT;
    ble_hs_cfg.sm_our_key_dist     = BLE_SM_PAIR_KEY_DIST_ENC;
    ble_hs_cfg.sm_their_key_dist   = BLE_SM_PAIR_KEY_DIST_ENC;

    ble_store_config_init();

    /* Khởi tạo các module mới */
    ble_client_init();
    treadmill_ctrl_init();
    button_init(on_button_pressed);
    // xTaskCreate(btn_simulate_task, "btn_sim", 4096, NULL, 5, NULL);

    nimble_port_freertos_init(blecent_host_task);

#if CONFIG_EXAMPLE_INIT_DEINIT_LOOP
    stack_init_deinit();
#endif

#if MYNEWT_VAL(BLE_EATT_CHAN_NUM) > 0
    bearers = 0;
    for (int i = 0; i < MYNEWT_VAL(BLE_EATT_CHAN_NUM); i++) {
        cids[i] = 0;
    }
#endif
}
