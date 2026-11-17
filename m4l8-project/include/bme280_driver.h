#pragma once

#include "driver/i2c_master.h"
#include "esp_err.h"

#include <cstdint>

namespace bme280 {

struct bme280;

using handle_t = bme280 *;

struct config_t {
  i2c_master_bus_handle_t bus;
  uint8_t address;
  uint32_t clock_speed_hz;
};

struct data_t {
  float temperature_c;
  float pressure_hpa;
  float humidity_percent;
};

esp_err_t init(const config_t &config, handle_t *out_handle);

esp_err_t read(handle_t handle, data_t *data);

} // namespace bme280
