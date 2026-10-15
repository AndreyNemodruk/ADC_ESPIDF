#pragma once

#include "driver/i2c_master.h"
#include "esp_err.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  int hour;
  int minute;
  int second;
  int day;
  int month;
  int year;
  int weekday; // 0 = Sun ... 6 = Sat
} WatchTime;

esp_err_t watch_init(i2c_master_bus_handle_t bus);
esp_err_t watch_get(WatchTime* out);
esp_err_t watch_set(const WatchTime* in);
void watch_format_time(const WatchTime* t, char* buf, size_t n);
void watch_format_date(const WatchTime* t, char* buf, size_t n);

#ifdef __cplusplus
}
#endif
