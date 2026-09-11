#pragma once

#include "driver/gpio.h"
#include "esp_adc/adc_oneshot.h"
#include <stdint.h>

namespace yva_adc {

constexpr float DEFAULT_CAHNGE_THRESHOLD = 0.01;
constexpr uint8_t DEFAULT_TASK_PRIORITY = 3;
constexpr uint16_t DEFAULT_METER_PERIOD_MS = 50;
constexpr uint16_t DEFAULT_ADC_CALI_MAX_VALUE = 3300;

struct handle;

using handle_t = handle *;
using change_callback_t = void (*)(float);

struct module_config_t {
  uint16_t meter_period_ms = DEFAULT_METER_PERIOD_MS;
  uint8_t task_priority = DEFAULT_TASK_PRIORITY;
};

struct config_t {
  gpio_num_t gpio;
  bool on_change_enabled = false;
  change_callback_t on_change = nullptr;
  float change_threshold = DEFAULT_CAHNGE_THRESHOLD;
  uint16_t max_calibrated_value = DEFAULT_ADC_CALI_MAX_VALUE;
};

void init_module(module_config_t &);

handle_t init_unit(config_t &);

void set_on_change_callback(handle_t, change_callback_t);
void clear_on_change_callback(handle_t);
void enable_on_change(handle_t);
void disable_on_change(handle_t);

float current_value(handle_t);
} // namespace yva_adc
