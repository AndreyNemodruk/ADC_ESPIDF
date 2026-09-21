#include "driver/gpio.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "soc/adc_channel.h"

constexpr gpio_num_t LED_GPIO = GPIO_NUM_16;
constexpr adc_channel_t ADC_CHANNEL = ADC_CHANNEL_3;

static adc_oneshot_unit_handle_t adc_handle;
static adc_cali_handle_t adc_cali_handle;

constexpr int LIGHT_THRESHOLD_MV = 1500;
constexpr int HYSTERESIS_MV = 300;

constexpr int DARK_THRESHOLD_MV = LIGHT_THRESHOLD_MV - HYSTERESIS_MV;
constexpr int BRIGHT_THRESHOLD_MV = LIGHT_THRESHOLD_MV + HYSTERESIS_MV;

struct SMA {
  int* readings;
  int size;
  int index;
  int sum;
  bool initialized;
};

#define SMA_SIZE 5

int readings[SMA_SIZE];

SMA adcFilter = {readings, SMA_SIZE, 0, 0, false};

int calculateSMA(SMA& filter, int newValue) {
  if (!filter.initialized) {

    for (int i = 0; i < filter.size; i++) {
      filter.readings[i] = newValue;
    }

    filter.sum = newValue * filter.size;
    filter.index = 0;
    filter.initialized = true;

    return newValue;
  }

  filter.sum -= filter.readings[filter.index];

  filter.readings[filter.index] = newValue;

  filter.sum += newValue;

  filter.index = (filter.index + 1) % filter.size;

  return filter.sum / filter.size;
}

void adc_init() {
  adc_oneshot_unit_init_cfg_t unit_config = {
      .unit_id = ADC_UNIT_1,
      .clk_src = ADC_RTC_CLK_SRC_DEFAULT,
      .ulp_mode = ADC_ULP_MODE_DISABLE,
  };

  ESP_ERROR_CHECK(adc_oneshot_new_unit(&unit_config, &adc_handle));

  adc_oneshot_chan_cfg_t channel_config = {
      .atten = ADC_ATTEN_DB_12,
      .bitwidth = ADC_BITWIDTH_DEFAULT,
  };

  ESP_ERROR_CHECK(adc_oneshot_config_channel(adc_handle, ADC_CHANNEL, &channel_config));

  adc_cali_curve_fitting_config_t cali_config = {
      .unit_id = ADC_UNIT_1,
      .chan = ADC_CHANNEL,
      .atten = ADC_ATTEN_DB_12,
      .bitwidth = ADC_BITWIDTH_DEFAULT,
  };

  ESP_ERROR_CHECK(adc_cali_create_scheme_curve_fitting(&cali_config, &adc_cali_handle));
}

int adc_read_raw() {
  int raw;

  ESP_ERROR_CHECK(adc_oneshot_read(adc_handle, ADC_CHANNEL, &raw));

  return raw;
}

int adc_read_mv(int raw) {
  int voltage_mv;

  ESP_ERROR_CHECK(adc_cali_raw_to_voltage(adc_cali_handle, raw, &voltage_mv));

  return voltage_mv;
}

void led_init() {
  gpio_config_t config = {};

  config.pin_bit_mask = (1ULL << LED_GPIO);
  config.mode = GPIO_MODE_OUTPUT;
  config.pull_up_en = GPIO_PULLUP_DISABLE;
  config.pull_down_en = GPIO_PULLDOWN_DISABLE;
  config.intr_type = GPIO_INTR_DISABLE;

  ESP_ERROR_CHECK(gpio_config(&config));

  gpio_set_level(LED_GPIO, 0);
}

extern "C" void app_main() {
  adc_init();
  led_init();

  bool led_on = false;

  while (true) {
    int raw = adc_read_raw();
    int mv = adc_read_mv(raw);

    int filtered_mv = calculateSMA(adcFilter, mv);

    if (filtered_mv < DARK_THRESHOLD_MV) {
      gpio_set_level(LED_GPIO, 1);
      led_on = true;
    } else if (filtered_mv > BRIGHT_THRESHOLD_MV) {
      gpio_set_level(LED_GPIO, 0);
      led_on = false;
    }

    printf("ADC: raw = %d, voltage = %d mV, SMA = %d mV, LED = %s\n", raw, mv, filtered_mv,
           led_on ? "ON" : "OFF");

    vTaskDelay(pdMS_TO_TICKS(400));
  }
}
