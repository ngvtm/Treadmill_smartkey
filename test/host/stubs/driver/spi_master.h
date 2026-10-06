#pragma once

#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

typedef enum { SPI1_HOST = 0, SPI2_HOST = 1, SPI3_HOST = 2 } spi_host_device_t;

#define SPI_DMA_CH_AUTO 3

typedef struct {
    int    sclk_io_num;
    int    mosi_io_num;
    int    miso_io_num;
    int    quadwp_io_num;
    int    quadhd_io_num;
    size_t max_transfer_sz;
} spi_bus_config_t;

extern spi_bus_config_t fake_spi_bus_cfg;

esp_err_t spi_bus_initialize(spi_host_device_t host,
                             const spi_bus_config_t *cfg, int dma);
