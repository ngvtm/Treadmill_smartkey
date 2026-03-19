# set(srcs "main.c")

# idf_component_register(SRCS "${srcs}"
#                        INCLUDE_DIRS ".")

idf_component_register(
    SRCS
        "main.c"
        "treadmill_ctrl/treadmill_ctrl.c"     # <-- thêm dòng này
        "button/button.c"
        "ble_client/ble_client.c"
    INCLUDE_DIRS
        "."
        "treadmill_ctrl"                 # <-- thêm dòng này để #include "treadmill.h" hoạt động
        "button"
        "ble_client"
)

