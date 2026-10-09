#include "MyButton.h"

#include "esp_timer.h"

uint32_t MyButton::nowMs() {
  return (uint32_t)(esp_timer_get_time() / 1000ULL);
}

esp_err_t MyButton::createButton(gpio_num_t pin, const ButtonConfig& config) {
  if (!GPIO_IS_VALID_GPIO(pin)) {
    _ready = false;
    return ESP_ERR_INVALID_ARG;
  }

  _pin = pin;
  _ready = false;

  _debounceMs = config.debounceMs;
  _longPressMs = config.longPressMs;
  _repeatMs = config.repeatMs;

  gpio_config_t io = {};
  io.pin_bit_mask = 1ULL << static_cast<int>(_pin);
  io.mode = GPIO_MODE_INPUT;
  io.intr_type = GPIO_INTR_DISABLE;

  switch (config.pinMode) {
  case ButtonPinMode::PullUp:
    io.pull_up_en = GPIO_PULLUP_ENABLE;
    io.pull_down_en = GPIO_PULLDOWN_DISABLE;
    _activeState = 0;
    break;

  case ButtonPinMode::PullDown:
    io.pull_up_en = GPIO_PULLUP_DISABLE;
    io.pull_down_en = GPIO_PULLDOWN_ENABLE;
    _activeState = 1;
    break;
  }

  esp_err_t err = gpio_config(&io);
  if (err != ESP_OK) {
    _pin = GPIO_NUM_NC;
    return err;
  }

  _currentState = gpio_get_level(_pin);
  _lastState = _currentState;
  _lastChangeTime = nowMs();
  _pressTime = 0;
  _lastRepeatTime = 0;
  _longPressTriggered = false;
  _pressArmed = false;
  _ready = true;

  return ESP_OK;
}

void MyButton::on(ButtonEvent event, ButtonHandler handler) {
  switch (event) {
  case ButtonEvent::ShortPress:
    _onShortPress = handler;
    break;

  case ButtonEvent::Release:
    _onRelease = handler;
    break;

  case ButtonEvent::LongPress:
    _onLongPress = handler;
    break;

  case ButtonEvent::Repeat:
    _onRepeat = handler;
    break;
  }
}

void MyButton::poll() {
  if (!_ready) {
    return;
  }

  const int state = gpio_get_level(_pin);
  const uint32_t now = nowMs();

  if (state != _lastState) {
    _lastState = state;
    _lastChangeTime = now;
    return;
  }

  if (now - _lastChangeTime < _debounceMs) {
    return;
  }

  if (state != _currentState) {
    _currentState = state;

    if (state == _activeState) {
      processPress(now);
    } else {
      processRelease();
    }
  }

  if (_pressArmed && _currentState == _activeState) {
    processHold(now);
  }
}

bool MyButton::isPressed() const {
  return _ready && _currentState == _activeState;
}

void MyButton::processPress(uint32_t now) {
  _pressTime = now;
  _longPressTriggered = false;
  _pressArmed = true;
}

void MyButton::processRelease() {
  if (!_pressArmed) {
    return;
  }

  if (!_longPressTriggered) {
    if (_onShortPress) {
      _onShortPress(this);
    }
  }

  if (_onRelease) {
    _onRelease(this);
  }

  _pressArmed = false;
}

void MyButton::processHold(uint32_t now) {
  if (!_longPressTriggered && now - _pressTime >= _longPressMs) {
    _longPressTriggered = true;
    _lastRepeatTime = now;

    if (_onLongPress) {
      _onLongPress(this);
    }
  }

  if (_longPressTriggered && now - _lastRepeatTime >= _repeatMs) {
    _lastRepeatTime = now;

    if (_onRepeat) {
      _onRepeat(this);
    }
  }
}
