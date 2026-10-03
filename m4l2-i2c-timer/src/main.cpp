
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include <iterator>

static const char *TAG = "M4L2";

static constexpr gpio_num_t I2C_SDA_GPIO = GPIO_NUM_10;
static constexpr gpio_num_t I2C_SCL_GPIO = GPIO_NUM_9;

static constexpr uint8_t SECONDS_MASK = 0b01111111;
static constexpr uint8_t MINUTES_MASK = 0b01111111;

static constexpr uint8_t CLOCK_ADDR = 0x68;

i2c_master_bus_handle_t i2c_bus;
i2c_master_dev_handle_t i2c_clock;

uint8_t binary_to_bcd(uint8_t);
uint8_t bcd_to_binary(uint8_t);

void print_time(const uint8_t buffer[7]);

extern "C" void app_main() {
  i2c_master_bus_config_t i2c_config = {
      .i2c_port = I2C_NUM_0,
      .sda_io_num = I2C_SDA_GPIO,
      .scl_io_num = I2C_SCL_GPIO,
      .clk_source = I2C_CLK_SRC_DEFAULT,
      .glitch_ignore_cnt = 7,
      .flags =
          {
              .enable_internal_pullup = true,
          },
  };

  ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_config, &i2c_bus));

  i2c_device_config_t clock_config = {
      .dev_addr_length = I2C_ADDR_BIT_LEN_7,
      .device_address = CLOCK_ADDR,
      .scl_speed_hz = 100'000,
  };

  ESP_ERROR_CHECK(
      i2c_master_bus_add_device(i2c_bus, &clock_config, &i2c_clock));

  uint8_t buffer[8];
  // Just  for test, init with time: 1999-12-31T23:59:30, Tuesday
  uint8_t command[8] = {0x00, 0x30, 0x59, 0x23, 0x02, 0x31, 0x12, 0x99};
  // Init device
  ESP_ERROR_CHECK(i2c_master_transmit(i2c_clock, command, 8, 30));

  command[0] = 0x00;
  while (true) {
    auto err =
        i2c_master_transmit_receive(i2c_clock, command, 1, buffer, 7, 100);

    if (err != ESP_OK) {
      ESP_LOGW(TAG, "Error while reading time: %s", esp_err_to_name(err));
    }

    print_time(buffer);

    vTaskDelay(pdMS_TO_TICKS(500));
  }
}

void print_time(const uint8_t buffer[7]) {
  uint16_t year = bcd_to_binary(buffer[6]);
  uint8_t month = bcd_to_binary(buffer[5] & ~(7 << 5));
  uint8_t day_of_month = bcd_to_binary(buffer[4] & ~(3 << 6));
  uint8_t day_of_week_num = bcd_to_binary(buffer[3] & 7);
  uint8_t hour = bcd_to_binary(buffer[2] & ~(3 << 6));
  uint8_t minute = bcd_to_binary(buffer[1] & ~(1u << 7));
  uint8_t second = bcd_to_binary(buffer[0] & ~(1u << 7));

  const char *day_of_week;

  switch (day_of_week_num) {
  case 1:
    day_of_week = "Mon";
    break;
  case 2:
    day_of_week = "Tue";
    break;
  case 3:
    day_of_week = "Wed";
    break;
  case 4:
    day_of_week = "Thu";
    break;
  case 5:
    day_of_week = "Fri";
    break;
  case 6:
    day_of_week = "Sat";
    break;
  case 7:
    day_of_week = "Sun";
    break;
  }

  ESP_LOGI(TAG, "%02d-%02d-%02dT%02d:%02d:%02d; Day of week: %s", year, month,
           day_of_month, hour, minute, second, day_of_week);
}

uint8_t binary_to_bcd(const uint8_t value) {
  if (value > 99) {
    return 0;
  }

  uint8_t result = 0;

  for (int bit = 7; bit >= 0; bit--) {

    if ((result & 0x0F) >= 5) {
      result += 3;
    }

    if (((result & 0xF0) >> 4) >= 5) {
      result += (3 << 4);
    }

    result <<= 1;
    result |= (value >> bit) & 1u;
  }

  return result;
}

uint8_t bcd_to_binary(const uint8_t bcd) {
  uint8_t result = (bcd & 0x0Fu) + ((bcd & 0xF0u) >> 4) * 10;
  return result;
}
