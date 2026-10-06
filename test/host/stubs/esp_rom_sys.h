#pragma once

#include <stdint.h>

/* Test host khong can tre thuc; chi de compile duong MIN-TEST. */
static inline void esp_rom_delay_us(uint32_t us) { (void)us; }
