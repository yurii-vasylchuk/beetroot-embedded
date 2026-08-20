#pragma once

#include "driver/gpio.h"
#include <cstdint>
namespace traffic_light_fsm {

enum mode_t {
  TRAFFIC_LIGHT_MODE_AUTO,
  TRAFFIC_LIGHT_MODE_MANUAL,
};

struct handle_t;

void set_mode(handle_t *handle, mode_t mode);
void switch_mode(handle_t *handle);
void set_allow_period_ms(handle_t *handle, std::uint32_t period);
void set_deny_period_ms(handle_t *handle, std::uint32_t period);

handle_t *create(std::uint32_t allow_period_ms, std::uint32_t deny_period_ms,
                 gpio_num_t red, gpio_num_t yellow, gpio_num_t green);
void destroy(handle_t *);

} // namespace traffic_light_fsm
