#pragma once

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_err.h"

#include <cstddef>
#include <cstdint>

namespace spi_flash {

static constexpr size_t PAGE_SIZE = 256;
static constexpr size_t SECTOR_SIZE = 4'096;

struct spi_flash;

using handle_t = spi_flash *;

struct config_t {
  spi_host_device_t host;
  gpio_num_t mosi_gpio;
  gpio_num_t miso_gpio;
  gpio_num_t sck_gpio;
  gpio_num_t cs_gpio;
  int clock_speed_hz;
};

esp_err_t init(const config_t &config, handle_t *out_handle);

esp_err_t read(handle_t handle, uint32_t address, void *data, size_t size);
esp_err_t write(handle_t handle, uint32_t address, const void *data,
                size_t size);
esp_err_t erase_sector(handle_t handle, uint32_t address);

uint32_t capacity(handle_t handle);

} // namespace spi_flash
