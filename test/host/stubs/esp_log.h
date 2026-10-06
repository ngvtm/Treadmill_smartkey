#pragma once

#include <stdio.h>

/*
 * Log khong in ra trong test, nhung van giu lai loi goi printf trong
 * nhanh chet de compiler tiep tuc kiem tra format string va khong bao
 * "defined but not used" cho cac helper chi dung trong log.
 */
#define ESP_LOG_STUB(...) do { if (0) { printf(__VA_ARGS__); } } while (0)

#define ESP_LOGE(tag, ...) ESP_LOG_STUB(__VA_ARGS__)
#define ESP_LOGW(tag, ...) ESP_LOG_STUB(__VA_ARGS__)
#define ESP_LOGI(tag, ...) ESP_LOG_STUB(__VA_ARGS__)
#define ESP_LOGD(tag, ...) ESP_LOG_STUB(__VA_ARGS__)
#define ESP_LOGV(tag, ...) ESP_LOG_STUB(__VA_ARGS__)
