#include "ds1307_driver.h"
#include "esp_log.h"
#include <iterator>

namespace ds1307 {
static constexpr const char *TAG = "DS1307";
static constexpr int TIMEOUT_MS = 20;

static constexpr const char *MON = "Mon";
static constexpr const char *TUE = "Tue";
static constexpr const char *WED = "Wed";
static constexpr const char *THU = "Thu";
static constexpr const char *FRI = "Fri";
static constexpr const char *SAT = "Sat";
static constexpr const char *SUN = "Sun";
static constexpr const char *UNKNOWN_DAY_OF_WEEK = "UNKNOWN";

static constexpr uint8_t REG_SECOND = 0x00;
static constexpr uint8_t REG_MINUTE = 0x01;
static constexpr uint8_t REG_HOUR = 0x02;
static constexpr uint8_t REG_DAY_OF_WEEK = 0x03;
static constexpr uint8_t REG_DATE = 0x04;
static constexpr uint8_t REG_MONTH = 0x05;
static constexpr uint8_t REG_YEAR = 0x06;
static constexpr uint8_t REG_CONTROL = 0x07;

static constexpr uint8_t CLOCK_ADDR = 0x68;

struct ds1307 {
  i2c_master_dev_handle_t dev;
};

uint8_t binary_to_bcd(const uint8_t);
uint8_t bcd_to_binary(const uint8_t);

handle_t init(i2c_master_bus_handle_t bus, config_t *config) {
  i2c_device_config_t clock_config = {
      .dev_addr_length = I2C_ADDR_BIT_LEN_7,
      .device_address = CLOCK_ADDR,
      .scl_speed_hz = 100'000,
  };

  i2c_master_dev_handle_t device;
  esp_err_t err = i2c_master_bus_add_device(bus, &clock_config, &device);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Unable to init ds1307: %s", esp_err_to_name(err));
    return nullptr;
  }

  auto handle = new ds1307{.dev = device};

  auto d = config->initial_date;
  uint8_t command[8] = {REG_SECOND,
                        binary_to_bcd(d.seconds),
                        binary_to_bcd(d.minutes),
                        binary_to_bcd(d.hours),
                        binary_to_bcd(static_cast<uint8_t>(d.day_of_week)),
                        binary_to_bcd(d.day_of_month),
                        binary_to_bcd(d.month),
                        binary_to_bcd(d.year)};

  err = i2c_master_transmit(device, command, std::size(command), TIMEOUT_MS);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "Unable to init ds1307: can't start timer. Error: %s",
             esp_err_to_name(err));
  }

  return handle;
}

date_t now(handle_t handle) {
  uint8_t buffer[8];
  auto err = i2c_master_transmit_receive(handle->dev, &REG_SECOND, 1, buffer,
                                         std::size(buffer), TIMEOUT_MS);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Unable to read date: %s", esp_err_to_name(err));
    return date_t{};
  }

  return date_t{
      .seconds = bcd_to_binary(buffer[0] & ~BIT7),
      .minutes = bcd_to_binary(buffer[1] & ~BIT7),
      .hours = bcd_to_binary(buffer[2] & ~(3 << 6)),
      .day_of_month = bcd_to_binary(buffer[4] & ~(3 << 6)),
      .month = bcd_to_binary(buffer[5] & ~(7 << 5)),
      .year = bcd_to_binary(buffer[6]),
      .day_of_week = static_cast<day_of_week_t>(buffer[3] & 7),
  };
}

// Utilities
const char *day_of_week_to_string(day_of_week_t day) {
  switch (day) {
  case day_of_week_t::Mon:
    return MON;
  case day_of_week_t::Tue:
    return TUE;
  case day_of_week_t::Wed:
    return WED;
  case day_of_week_t::Thu:
    return THU;
  case day_of_week_t::Fri:
    return FRI;
  case day_of_week_t::Sat:
    return SAT;
  case day_of_week_t::Sun:
    return SUN;
  case day_of_week_t::UNK:
    return UNKNOWN_DAY_OF_WEEK;
  }

  ESP_LOGE(TAG, "Unhandled day_of_week_t");
  return "";
}

// Private
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
} // namespace ds1307
