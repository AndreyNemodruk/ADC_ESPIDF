#pragma once

#include <cstdint>

#include "driver/gpio.h"
#include "esp_err.h"

class MyButton;

typedef void (*ButtonHandler)(MyButton* button);

enum class ButtonPinMode {
  PullUp,
  PullDown,
};

struct ButtonConfig {
  uint32_t debounceMs = 50;
  uint32_t longPressMs = 2000;
  uint32_t repeatMs = 100;

  ButtonPinMode pinMode = ButtonPinMode::PullUp;
};

enum class ButtonEvent { ShortPress, Release, LongPress, Repeat };

class MyButton {
public:
  esp_err_t createButton(gpio_num_t pin, const ButtonConfig& config = ButtonConfig{});

  void poll();

  void on(ButtonEvent event, ButtonHandler handler);

  bool isPressed() const;

private:
  gpio_num_t _pin = GPIO_NUM_NC;
  bool _ready = false;

  int _currentState = 0;
  int _lastState = 0;
  int _activeState = 0;

  uint32_t _lastChangeTime = 0;
  uint32_t _pressTime = 0;
  uint32_t _lastRepeatTime = 0;

  uint32_t _debounceMs = 50;
  uint32_t _longPressMs = 2000;
  uint32_t _repeatMs = 100;

  bool _longPressTriggered = false;
  bool _pressArmed = false;

  ButtonHandler _onShortPress = nullptr;
  ButtonHandler _onRelease = nullptr;
  ButtonHandler _onLongPress = nullptr;
  ButtonHandler _onRepeat = nullptr;

  static uint32_t nowMs();

  void processPress(uint32_t now);
  void processRelease();
  void processHold(uint32_t now);
};
