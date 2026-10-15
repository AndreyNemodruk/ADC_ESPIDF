#include "oled.h"

#include <string.h>

#include "esp_log.h"
#include "ssd1306.h"

static const char* TAG = "oled";
static const uint8_t kAddr = 0x3C;

static SSD1306_t s_dev;
static char s_last_time[17];
static char s_last_date[17];

static void center_line(char dst[17], const char* src) {
  memset(dst, ' ', 16);
  dst[16] = '\0';
  if (src == nullptr) {
    return;
  }

  size_t n = strlen(src);
  if (n > 16) {
    n = 16;
  }
  memcpy(dst + (16 - n) / 2, src, n);
}

esp_err_t oled_init(i2c_master_bus_handle_t bus) {
  if (bus == nullptr) {
    return ESP_ERR_INVALID_ARG;
  }

  memset(&s_dev, 0, sizeof(s_dev));
  s_dev._i2c_bus_handle = bus;
  i2c_device_add(&s_dev, I2C_NUM_0, -1, kAddr);

  esp_err_t err = ssd1306_init(&s_dev, 128, 64);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "ssd1306_init failed: %s", esp_err_to_name(err));
    return err;
  }

  ssd1306_clear_screen(&s_dev, false);
  ssd1306_contrast(&s_dev, 0xFF);
  s_last_time[0] = '\0';
  s_last_date[0] = '\0';
  ESP_LOGI(TAG, "SSD1306 ready");
  return ESP_OK;
}

void oled_show_datetime(const char* time_hhmmss, const char* date_line) {
  char time_line[17];
  char date_buf[17];
  center_line(time_line, time_hhmmss);
  center_line(date_buf, date_line);

  if (strcmp(time_line, s_last_time) != 0) {
    ssd1306_display_text(&s_dev, 2, time_line, 16, false);
    memcpy(s_last_time, time_line, sizeof(s_last_time));
  }
  if (strcmp(date_buf, s_last_date) != 0) {
    ssd1306_display_text(&s_dev, 5, date_buf, 16, false);
    memcpy(s_last_date, date_buf, sizeof(s_last_date));
  }
}
