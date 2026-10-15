#pragma once

#include "driver/i2c_master.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t oled_init(i2c_master_bus_handle_t bus);
void oled_show_datetime(const char* time_hhmmss, const char* date_line);

#ifdef __cplusplus
}
#endif
