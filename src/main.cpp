#include "driver/gpio.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "soc/adc_channel.h"

constexpr gpio_num_t ADC_GPIO = GPIO_NUM_4;
constexpr adc_channel_t ADC_CHANNEL = ADC_CHANNEL_3;

static adc_oneshot_unit_handle_t adc_handle;
static adc_cali_handle_t adc_cali_handle;

// Вноси сюда напряжение, измеренное мультиметром
int U_manual = 1000;

#define SMA_SIZE 5

// int readings[SMA_SIZE];
// long sum = 0;
// bool sma_initialized = false;
// static int index = 0;

// int calculateSMA(int newValue) {
//   if (!sma_initialized) {
//     for (int i = 0; i < SMA_SIZE; i++) {
//       readings[i] = newValue;
//     }

//     sum = newValue * SMA_SIZE;
//     sma_initialized = true;

//     return newValue;
//   }

//   sum -= readings[index];
//   readings[index] = newValue;
//   sum += newValue;

//   index = (index + 1) % SMA_SIZE;

//   return sum / SMA_SIZE;
// }

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

extern "C" void app_main() {
  adc_init();

  printf("\n");
  printf("===============================================================\n");
  printf("                 ESP32-S3 ADC TEST\n");
  printf("===============================================================\n");
  printf("%8s | %14s | %12s | %10s\n", "RAW", "U_manual(mV)", "U_cali(mV)", "Error(%)");
  printf("---------+----------------+--------------+-----------\n");

  while (true) {
    int raw = adc_read_raw();
    int mv = adc_read_mv(raw);

    // int filtered_mv = calculateSMA(mv);

    float error = 0.0f;

    if (U_manual != 0) {
      error = ((static_cast<float>(mv) - U_manual) / U_manual) * 100.0f;
    }

    printf("%8d | %14d | %12d | %+9.2f\n", raw, U_manual, mv, error);

    vTaskDelay(pdMS_TO_TICKS(400));
  }
}
