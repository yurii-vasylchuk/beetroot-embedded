#pragma once
#include "driver/i2c_master.h"

namespace ssd1306 {

struct ssd1306;

using handle_t = ssd1306 *;

struct config_t {
  i2c_master_bus_handle_t bus;
};

handle_t init(config_t &config);

void update(handle_t handle);

void clear(handle_t handle);

void text(handle_t handle, uint16_t x_offset, uint16_t y_offset, char *text);

void dot(handle_t handle, uint16_t x, uint16_t y);
void clear_dot(handle_t handle, uint16_t x, uint16_t y);

} // namespace ssd1306
