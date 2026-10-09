#include <stdio.h>
#include <string.h>

#include "MyButton.h"
#include "driver/gpio.h"
#include "driver/uart.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define UART_PORT UART_NUM_1
#define UART_TX_PIN GPIO_NUM_17
#define UART_RX_PIN GPIO_NUM_18
#define UART_BAUD_RATE 115200
#define RX_BUFFER_SIZE 256
#define COMMAND_SIZE 64

#define BUTTON_PIN GPIO_NUM_5
#define LED_PIN GPIO_NUM_4

static char rxBuffer[COMMAND_SIZE];
static uint8_t rxPos = 0;

static MyButton button;
static bool ledRequestedOn = false;

static void uart_init() {
  uart_config_t uartConfig = {};
  uartConfig.baud_rate = UART_BAUD_RATE;
  uartConfig.data_bits = UART_DATA_8_BITS;
  uartConfig.parity = UART_PARITY_DISABLE;
  uartConfig.stop_bits = UART_STOP_BITS_1;
  uartConfig.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
  uartConfig.rx_flow_ctrl_thresh = 122;
  uartConfig.source_clk = UART_SCLK_DEFAULT;

  ESP_ERROR_CHECK(uart_driver_install(UART_PORT, RX_BUFFER_SIZE, 0, 0, NULL, 0));
  ESP_ERROR_CHECK(uart_param_config(UART_PORT, &uartConfig));
  ESP_ERROR_CHECK(
      uart_set_pin(UART_PORT, UART_TX_PIN, UART_RX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
}

static void uart_send(const char* text) {
  uart_write_bytes(UART_PORT, text, strlen(text));
}

static void led_init() {
  gpio_config_t io = {};
  io.pin_bit_mask = 1ULL << LED_PIN;
  io.mode = GPIO_MODE_OUTPUT;
  io.pull_up_en = GPIO_PULLUP_DISABLE;
  io.pull_down_en = GPIO_PULLDOWN_DISABLE;
  io.intr_type = GPIO_INTR_DISABLE;
  gpio_config(&io);
  gpio_set_level(LED_PIN, 0);
}

static void led_set(bool on) {
  gpio_set_level(LED_PIN, on ? 1 : 0);
}

static void onButtonShortPress(MyButton* /*btn*/) {
  ledRequestedOn = !ledRequestedOn;
  uart_send(ledRequestedOn ? "LED_ON\r\n" : "LED_OFF\r\n");
}

static void handle_command(const char* command) {
  if (strcmp(command, "LED_ON") == 0) {
    led_set(true);
  } else if (strcmp(command, "LED_OFF") == 0) {
    led_set(false);
  }
}

static void uart_poll() {
  uint8_t rx;
  int len = uart_read_bytes(UART_PORT, &rx, 1, pdMS_TO_TICKS(10));

  if (len <= 0) {
    return;
  }

  if (rx == '\r' || rx == '\n') {
    if (rxPos > 0) {
      rxBuffer[rxPos] = '\0';
      handle_command(rxBuffer);
      rxPos = 0;
    }
    return;
  }

  if (rxPos < sizeof(rxBuffer) - 1) {
    rxBuffer[rxPos++] = rx;
  }
}

extern "C" void app_main() {
  uart_init();
  led_init();

  ButtonConfig buttonConfig = {};
  buttonConfig.pinMode = ButtonPinMode::PullUp;
  ESP_ERROR_CHECK(button.createButton(BUTTON_PIN, buttonConfig));
  button.on(ButtonEvent::ShortPress, onButtonShortPress);

  while (true) {
    button.poll();
    uart_poll();
  }
}
