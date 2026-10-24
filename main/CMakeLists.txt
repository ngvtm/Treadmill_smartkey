# set(srcs "main.c")

# idf_component_register(SRCS "${srcs}"
#                        INCLUDE_DIRS ".")

idf_component_register(
    SRCS
        "main.c"
        "treadmill_ctrl/treadmill_ctrl.c"     # <-- thêm dòng này
        "button/button.c"
        "ble_client/ble_client.c"
        "lcd/lcd_gfx.c"
        "lcd/lcd_display.c"
        "touch/touch_xpt2046.c"
        "touch/touch_menu.c"
    INCLUDE_DIRS
        "."
        "treadmill_ctrl"                 # <-- thêm dòng này để #include "treadmill.h" hoạt động
        "button"
        "ble_client"
        "lcd"
        "touch"
    REQUIRES
        # Khai REQUIRES tường minh sẽ tắt phụ thuộc ngầm của "main",
        # nên phải liệt kê cả bt/nvs_flash vốn trước đây tự có.
        bt
        nvs_flash
        esp_lcd
        esp_driver_spi
        esp_driver_gpio
)

