#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "host/ble_hs.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Module quản lý trạng thái kết nối BLE phía Central.
 * Lưu conn_handle và chr_val_handle sau khi service discovery
 * hoàn tất, cung cấp API ghi command tới Peripheral.
 */

void ble_client_init(void);

/** Lưu thông tin sau khi service discovery thành công */
void ble_client_set_connection(uint16_t conn_handle, uint16_t chr_val_handle);

/** Xóa thông tin khi disconnect */
void ble_client_clear_connection(void);

/** Kiểm tra đã kết nối và sẵn sàng ghi chưa */
bool ble_client_is_ready(void);

/** Ghi 1 byte command tới characteristic của Peripheral */
int  ble_client_write_command(uint8_t cmd);

#ifdef __cplusplus
}
#endif
