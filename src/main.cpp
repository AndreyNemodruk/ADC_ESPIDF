#include "driver/i2c_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "oled.h"
#include "watch.h"

static constexpr gpio_num_t kI2cSda = GPIO_NUM_8;
static constexpr gpio_num_t kI2cScl = GPIO_NUM_9;

static i2c_master_bus_handle_t i2c_bus_init() {
  i2c_master_bus_config_t cfg = {};
  cfg.i2c_port = I2C_NUM_0;
  cfg.sda_io_num = kI2cSda;
  cfg.scl_io_num = kI2cScl;
  cfg.clk_source = I2C_CLK_SRC_DEFAULT;
  cfg.glitch_ignore_cnt = 7;
  cfg.flags.enable_internal_pullup = true;

  i2c_master_bus_handle_t bus = nullptr;
  ESP_ERROR_CHECK(i2c_new_master_bus(&cfg, &bus));
  return bus;
}

extern "C" void app_main() {
  i2c_master_bus_handle_t bus = i2c_bus_init();
  ESP_ERROR_CHECK(oled_init(bus));
  ESP_ERROR_CHECK(watch_init(bus));

  char time_buf[16];
  char date_buf[20];

  while (true) {
    WatchTime now = {};
    if (watch_get(&now) == ESP_OK) {
      watch_format_time(&now, time_buf, sizeof(time_buf));
      watch_format_date(&now, date_buf, sizeof(date_buf));
      oled_show_datetime(time_buf, date_buf);
    }
    vTaskDelay(pdMS_TO_TICKS(200));
  }
}
