#include "watch.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "esp_log.h"

static const char* TAG = "watch";
static const uint8_t kAddr = 0x68;
static const int kTimeoutMs = 100;
static const char* kDow[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};

static i2c_master_dev_handle_t s_dev;

static uint8_t bcd2dec(uint8_t v) {
  return static_cast<uint8_t>(((v >> 4) * 10) + (v & 0x0F));
}

static uint8_t dec2bcd(uint8_t v) {
  return static_cast<uint8_t>(((v / 10) << 4) | (v % 10));
}

static int month_from_date(const char* date) {
  static const char kMonths[] = "JanFebMarAprMayJunJulAugSepOctNovDec";
  for (int i = 0; i < 12; ++i) {
    if (memcmp(date, kMonths + i * 3, 3) == 0) {
      return i + 1;
    }
  }
  return 1;
}

static void fill_compile_time(WatchTime* t) {
  // __DATE__ = "Mmm dd yyyy", __TIME__ = "hh:mm:ss"
  t->month = month_from_date(__DATE__);
  t->day = 0;
  t->year = 0;
  sscanf(__DATE__ + 4, "%d %d", &t->day, &t->year);
  sscanf(__TIME__, "%d:%d:%d", &t->hour, &t->minute, &t->second);

  struct tm tm = {};
  tm.tm_year = t->year - 1900;
  tm.tm_mon = t->month - 1;
  tm.tm_mday = t->day;
  tm.tm_hour = t->hour;
  tm.tm_min = t->minute;
  tm.tm_sec = t->second;
  tm.tm_isdst = -1;
  mktime(&tm);
  t->weekday = tm.tm_wday;
}

static bool time_looks_valid(const WatchTime* t) {
  return t->second >= 0 && t->second <= 59 && t->minute >= 0 && t->minute <= 59 && t->hour >= 0 &&
         t->hour <= 23 && t->day >= 1 && t->day <= 31 && t->month >= 1 && t->month <= 12 &&
         t->year >= 2000 && t->year <= 2099;
}

esp_err_t watch_init(i2c_master_bus_handle_t bus) {
  if (bus == nullptr) {
    return ESP_ERR_INVALID_ARG;
  }

  i2c_device_config_t cfg = {};
  cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
  cfg.device_address = kAddr;
  cfg.scl_speed_hz = 100000;
  esp_err_t err = i2c_master_bus_add_device(bus, &cfg, &s_dev);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "add device failed: %s", esp_err_to_name(err));
    return err;
  }

  WatchTime now = {};
  err = watch_get(&now);
  if (err != ESP_OK || !time_looks_valid(&now)) {
    fill_compile_time(&now);
    ESP_LOGW(TAG, "RTC halted/invalid, setting compile time %02d:%02d:%02d %04d-%02d-%02d",
             now.hour, now.minute, now.second, now.year, now.month, now.day);
    err = watch_set(&now);
    if (err != ESP_OK) {
      return err;
    }
  }

  ESP_LOGI(TAG, "DS1307 ready at 0x68");
  return ESP_OK;
}

esp_err_t watch_get(WatchTime* out) {
  if (out == nullptr || s_dev == nullptr) {
    return ESP_ERR_INVALID_STATE;
  }

  uint8_t reg = 0;
  uint8_t data[7] = {};
  esp_err_t err = i2c_master_transmit_receive(s_dev, &reg, 1, data, sizeof(data), kTimeoutMs);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "read failed: %s", esp_err_to_name(err));
    return err;
  }

  const bool halted = (data[0] & 0x80) != 0;
  out->second = bcd2dec(data[0] & 0x7F);
  out->minute = bcd2dec(data[1] & 0x7F);

  if (data[2] & 0x40) {
    int hour12 = bcd2dec(data[2] & 0x1F);
    if (hour12 == 12) {
      hour12 = 0;
    }
    if (data[2] & 0x20) {
      hour12 += 12;
    }
    out->hour = hour12;
  } else {
    out->hour = bcd2dec(data[2] & 0x3F);
  }

  int dow = data[3] & 0x07;
  out->weekday = (dow == 0) ? 0 : (dow - 1) % 7;
  out->day = bcd2dec(data[4] & 0x3F);
  out->month = bcd2dec(data[5] & 0x1F);
  out->year = 2000 + bcd2dec(data[6]);

  if (halted) {
    return ESP_ERR_INVALID_STATE;
  }
  return ESP_OK;
}

esp_err_t watch_set(const WatchTime* in) {
  if (in == nullptr || s_dev == nullptr) {
    return ESP_ERR_INVALID_STATE;
  }

  uint8_t buf[8];
  buf[0] = 0;
  buf[1] = dec2bcd(static_cast<uint8_t>(in->second)) & 0x7F;
  buf[2] = dec2bcd(static_cast<uint8_t>(in->minute)) & 0x7F;
  buf[3] = dec2bcd(static_cast<uint8_t>(in->hour)) & 0x3F;
  buf[4] = static_cast<uint8_t>(((in->weekday % 7) + 1) & 0x07);
  buf[5] = dec2bcd(static_cast<uint8_t>(in->day)) & 0x3F;
  buf[6] = dec2bcd(static_cast<uint8_t>(in->month)) & 0x1F;
  buf[7] = dec2bcd(static_cast<uint8_t>(in->year % 100));

  esp_err_t err = i2c_master_transmit(s_dev, buf, sizeof(buf), kTimeoutMs);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "write failed: %s", esp_err_to_name(err));
  }
  return err;
}

void watch_format_time(const WatchTime* t, char* buf, size_t n) {
  if (t == nullptr || buf == nullptr || n == 0) {
    return;
  }
  snprintf(buf, n, "%02d:%02d:%02d", t->hour, t->minute, t->second);
}

void watch_format_date(const WatchTime* t, char* buf, size_t n) {
  if (t == nullptr || buf == nullptr || n == 0) {
    return;
  }
  snprintf(buf, n, "%s %02d.%02d.%04d", kDow[t->weekday % 7], t->day, t->month, t->year);
}
