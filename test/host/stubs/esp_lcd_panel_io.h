#pragma once

#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "driver/spi_master.h"

typedef struct esp_lcd_panel_io_t *esp_lcd_panel_io_handle_t;
typedef void                      *esp_lcd_spi_bus_handle_t;

typedef struct {
    int      dc_gpio_num;
    int      cs_gpio_num;
    unsigned pclk_hz;
    int      lcd_cmd_bits;
    int      lcd_param_bits;
    int      spi_mode;
    int      trans_queue_depth;
} esp_lcd_panel_io_spi_config_t;

extern esp_lcd_panel_io_spi_config_t fake_io_cfg;

esp_err_t esp_lcd_new_panel_io_spi(esp_lcd_spi_bus_handle_t bus,
                                   const esp_lcd_panel_io_spi_config_t *cfg,
                                   esp_lcd_panel_io_handle_t *out);

esp_err_t esp_lcd_panel_io_tx_param(esp_lcd_panel_io_handle_t io, int cmd,
                                    const void *param, size_t len);
esp_err_t esp_lcd_panel_io_tx_color(esp_lcd_panel_io_handle_t io, int cmd,
                                    const void *color, size_t len);
