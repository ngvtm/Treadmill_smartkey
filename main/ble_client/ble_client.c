#include "ble_client.h"
#include "lcd/lcd_display.h"
#include "host/ble_gatt.h"
#include "host/ble_gap.h"
#include "modlog/modlog.h"

static uint16_t s_conn_handle    = BLE_HS_CONN_HANDLE_NONE;
static uint16_t s_chr_val_handle = 0;

void ble_client_init(void)
{
    s_conn_handle    = BLE_HS_CONN_HANDLE_NONE;
    s_chr_val_handle = 0;
    MODLOG_DFLT(INFO, "ble_client: initialized\n");
}

void ble_client_set_connection(uint16_t conn_handle, uint16_t chr_val_handle)
{
    s_conn_handle    = conn_handle;
    s_chr_val_handle = chr_val_handle;
    lcd_display_set_link(LCD_LINK_CONNECTED);
    MODLOG_DFLT(INFO, "ble_client: ready — conn_handle=%d chr_val_handle=%d\n",
                conn_handle, chr_val_handle);
}

void ble_client_clear_connection(void)
{
    s_conn_handle    = BLE_HS_CONN_HANDLE_NONE;
    s_chr_val_handle = 0;
    lcd_display_set_link(LCD_LINK_DISCONNECTED);
    MODLOG_DFLT(INFO, "ble_client: connection cleared\n");
}

bool ble_client_is_ready(void)
{
    return (s_conn_handle != BLE_HS_CONN_HANDLE_NONE)
        && (s_chr_val_handle != 0);
}

int ble_client_write_feature(uint8_t feature_id, bool enable)
{
    if (!ble_client_is_ready()) {
        MODLOG_DFLT(WARN, "ble_client: not connected, drop SET_FEATURE\n");
        return -1;
    }
    uint8_t bytes[2] = { 0x10,
                         (uint8_t)(((feature_id & 0x0F) << 4) |
                                   (enable ? 1 : 0)) };
    int rc = ble_gattc_write_flat(s_conn_handle, s_chr_val_handle,
                                  bytes, sizeof(bytes), NULL, NULL);
    if (rc != 0) {
        MODLOG_DFLT(ERROR, "ble_client: SET_FEATURE write failed rc=%d\n", rc);
    }
    return rc;
}

int ble_client_get_rssi(int8_t *out_rssi)
{
    if (out_rssi == NULL || !ble_client_is_ready()) {
        return -1;
    }
    return ble_gap_conn_rssi(s_conn_handle, out_rssi);
}

int ble_client_write_command(uint8_t cmd)
{
    if (!ble_client_is_ready()) {
        MODLOG_DFLT(WARN, "ble_client: not connected, drop cmd=0x%02X\n", cmd);
        return -1;
    }

    int rc = ble_gattc_write_flat(s_conn_handle, s_chr_val_handle,
                                  &cmd, sizeof(cmd), NULL, NULL);
    if (rc != 0) {
        MODLOG_DFLT(ERROR, "ble_client: write failed rc=%d\n", rc);
    } else {
        MODLOG_DFLT(INFO, "ble_client: sent cmd=0x%02X\n", cmd);
    }
    return rc;
}
