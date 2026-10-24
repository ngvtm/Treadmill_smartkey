# Đấu nối TFT 2,4" cho smartkey (GĐ3.1) — thay e-paper

**Sơ đồ chân cắm trực quan (in được): xem [TFT_PINOUT.html](TFT_PINOUT.html)** — module bên trái,
ESP32 bên phải, đường nối theo từng chân; đi theo tên chân in trên module, không theo vị trí trong hình.

Giữ nguyên vị trí dây của e-paper cũ; chỉ đổi vai trò chân GPIO4 và tháo bo DESPI-C02/e-paper ra.

| Chân module TFT | Nối vào | Ghi chú |
|---|---|---|
| VCC | 3V3 (hoặc 5V nếu module có ổn áp) | Nhìn board module trước: có con AMS1117-3.3 thì cấp 5 V vào VCC (lấy từ chân 5V/VIN của board ESP32); không có ổn áp thì cấp 3,3 V. Cấp 3,3 V vào module có ổn áp sẽ chỉ còn ~2,2 V sau ổn áp, màn không lên. Chân logic luôn 3,3 V từ ESP32. (Sửa lại dòng cũ "bản có ổn áp vẫn cấp 3,3 V" — dòng đó sai với module có ổn áp.) |
| GND | GND | |
| CS | GPIO27 | như cũ |
| RESET | GPIO25 | như cũ |
| DC / RS | GPIO26 | như cũ |
| SDI / MOSI (có thể in "SDA") | GPIO33 | như cũ |
| SCK | GPIO32 | như cũ |
| LED / BL | GPIO4 | chân BUSY cũ của e-paper — nay là đèn nền, firmware bật sau khi init xong |
| SDO / MISO | — | không nối (chỉ ghi, không đọc panel) |
| T_CLK, T_CS, T_DIN, T_DO, T_IRQ (touch XPT2046) | — | **không nối** — có touch nhưng không dùng theo chốt của user |

## Phase 2 — Đấu dây touch XPT2046 (TÙY CHỌN — chỉ khi dùng menu cảm ứng; anh đã chốt không dùng cảm ứng thì bỏ qua toàn bộ mục này, không nối 5 chân touch)

Touch chia sẻ bus SPI với màn hình; chỉ thêm 3 chân:

| Chân touch | Nối vào | Ghi chú |
|---|---|---|
| T_CLK | GPIO32 (chung SCK màn hình) | |
| T_DIN | GPIO33 (chung MOSI màn hình) | |
| T_DO | GPIO19 | MISO riêng; bus SPI của màn hình đã khai báo chân này (menuconfig "Smartkey Touch") |
| T_CS | GPIO14 | chip-select riêng, không phải strapping |
| T_IRQ | GPIO39 | chỉ-vào, mức thấp = đang chạm; firmware poll 20 ms, không dùng ngắt |
| VCC / GND | 3.3 V / GND | **không** đưa 5 V vào chân touch |

Clock touch 2 MHz (device SPI riêng trên cùng bus, không dùng cấu
hình 40 MHz của panel). Hầu hết module 2,4\" đưa các chân touch ra
chung ribbon với chân màn hình — kiểm tra tên in trên module trước
khi hàn (một số module in T_DIN là "T_SDA", T_DO là "T_SDO").

Kiểm tra sau khi lắp: vào menu bằng cách **chạm giữ vùng tiêu đề
2 giây khi máy đang dừng và đã kết nối** — lần đầu chưa có hiệu
chuẩn sẽ hiện màn 3 dấu + (chạm từng dấu); sau đó menu hiện 4 hàng
(BAO VE VUNG IR, KHOA AN TOAN, HIEU CHUAN CAM UNG, THOAT). Chạm
công tắc → server đổi + ghi NVS; nếu server từ chối (máy đang
chạy) công tắc tự về trạng thái cũ khi status mới về. Tắt một tính
năng → màn chính luôn hiện dải đỏ "CO TINH NANG / AN TOAN DANG TAT"
cho tới khi bật lại.

## Cấu hình firmware
- Driver IC: menuconfig → **Smartkey TFT Display → TFT panel driver IC**: mặc định **ILI9341** (module v1.3 phổ biến). Nếu flash xong màn trắng/đen bất thường hoặc màu sai hẳn, đổi sang **ST7789** — không sửa code.
- Tốc độ SPI mặc định 40 MHz; dây dài/nhiễu thì hạ `TFT SPI clock` xuống 20 MHz trong cùng menu.

## Kiểm tra sau khi lắp (trước khi chạy máy)
1. Bật smartkey: đèn nền sáng, hiện khung SMARTKEY + "MAT KET NOI". Màn trắng tinh = sai driver IC hoặc lỏng RESET/CS — thử đổi driver trong menuconfig trước khi nghi phần cứng.
2. Màu bị đảo (nền đen chữ trắng ngược hẳn): panel cần INVON — báo lại để thêm vào chuỗi init của driver đang chọn.
3. Ảnh bị lệch/dồn một phía: khả năng module là biến thể có offset RAM — báo lại kèm ảnh chụp màn hình.
4. Kết nối được server: ô trạng thái góc phải chuyển nền **xanh lá** "DA KET NOI"; bấm chạy thử, cảnh báo nghiêm trọng (ví dụ rút khoá reed) phải hiện **nền đỏ** "KHOA AN TOAN / DA BI BUT RA".
5. Gửi kèm serial log dòng `TFT: San sang: TFT ILI9341 (hoac ST7789) 240x320...` để đối chiếu cấu hình đã build.
