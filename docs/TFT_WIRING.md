# Đấu nối TFT 2,4" cho smartkey (GĐ3.1) — thay e-paper

Giữ nguyên vị trí dây của e-paper cũ; chỉ đổi vai trò chân GPIO4 và tháo bo DESPI-C02/e-paper ra.

| Chân module TFT | Nối vào | Ghi chú |
|---|---|---|
| VCC | 3V3 | Module 2,4" SPI chạy 3,3 V (bản có ổn áp kèm theo vẫn cấp 3,3 V cho an toàn logic) |
| GND | GND | |
| CS | GPIO27 | như cũ |
| RESET | GPIO25 | như cũ |
| DC / RS | GPIO26 | như cũ |
| SDI / MOSI (có thể in "SDA") | GPIO33 | như cũ |
| SCK | GPIO32 | như cũ |
| LED / BL | GPIO4 | chân BUSY cũ của e-paper — nay là đèn nền, firmware bật sau khi init xong |
| SDO / MISO | — | không nối (chỉ ghi, không đọc panel) |
| T_CLK, T_CS, T_DIN, T_DO, T_IRQ (touch XPT2046) | — | **không nối** — có touch nhưng không dùng theo chốt của user |

## Cấu hình firmware
- Driver IC: menuconfig → **Smartkey TFT Display → TFT panel driver IC**: mặc định **ILI9341** (module v1.3 phổ biến). Nếu flash xong màn trắng/đen bất thường hoặc màu sai hẳn, đổi sang **ST7789** — không sửa code.
- Tốc độ SPI mặc định 40 MHz; dây dài/nhiễu thì hạ `TFT SPI clock` xuống 20 MHz trong cùng menu.

## Kiểm tra sau khi lắp (trước khi chạy máy)
1. Bật smartkey: đèn nền sáng, hiện khung SMARTKEY + "MAT KET NOI". Màn trắng tinh = sai driver IC hoặc lỏng RESET/CS — thử đổi driver trong menuconfig trước khi nghi phần cứng.
2. Màu bị đảo (nền đen chữ trắng ngược hẳn): panel cần INVON — báo lại để thêm vào chuỗi init của driver đang chọn.
3. Ảnh bị lệch/dồn một phía: khả năng module là biến thể có offset RAM — báo lại kèm ảnh chụp màn hình.
4. Kết nối được server: ô trạng thái góc phải chuyển nền **xanh lá** "DA KET NOI"; bấm chạy thử, cảnh báo nghiêm trọng (ví dụ rút khoá reed) phải hiện **nền đỏ** "KHOA AN TOAN / DA BI BUT RA".
5. Gửi kèm serial log dòng `TFT: San sang: TFT ILI9341 (hoac ST7789) 240x320...` để đối chiếu cấu hình đã build.
