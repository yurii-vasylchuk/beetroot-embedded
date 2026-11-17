#pragma once

#include "driver/gpio.h"
#include "driver/i2c_types.h"
#include "driver/spi_master.h"

#include <cstddef>
#include <cstdint>

namespace app_config {

static constexpr i2c_port_num_t I2C_PORT = I2C_NUM_0;
static constexpr gpio_num_t I2C_SDA_GPIO = GPIO_NUM_10;
static constexpr gpio_num_t I2C_SCL_GPIO = GPIO_NUM_9;
static constexpr uint32_t I2C_CLOCK_HZ = 400'000;
static constexpr uint8_t BME280_I2C_ADDRESS = 0x76;

static constexpr spi_host_device_t FLASH_SPI_HOST = SPI2_HOST;
static constexpr gpio_num_t FLASH_MOSI_GPIO = GPIO_NUM_11;
static constexpr gpio_num_t FLASH_MISO_GPIO = GPIO_NUM_13;
static constexpr gpio_num_t FLASH_SCK_GPIO = GPIO_NUM_12;
static constexpr gpio_num_t FLASH_CS_GPIO = GPIO_NUM_14;
static constexpr int FLASH_SPI_CLOCK_HZ = 4'000'000;

static constexpr uint32_t SENSOR_DELAY_MS = 1'000;
static constexpr size_t FLASH_QUEUE_LENGTH = 16;
static constexpr size_t DISPLAY_QUEUE_LENGTH = 1;

static_assert(SENSOR_DELAY_MS > 0);

} // namespace app_config
