#include "bme280_driver.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace bme280 {

static constexpr const char *TAG = "BME280";

enum class register_t : uint8_t {
  CALIBRATION_1 = 0x88,
  CHIP_ID = 0xD0,
  RESET = 0xE0,
  CALIBRATION_2 = 0xE1,
  CONTROL_HUMIDITY = 0xF2,
  STATUS = 0xF3,
  CONTROL_MEASUREMENT = 0xF4,
  CONFIG = 0xF5,
  PRESSURE_MSB = 0xF7,
};

static constexpr uint8_t CHIP_ID = 0x60;
static constexpr uint8_t RESET_COMMAND = 0xB6;
static constexpr uint8_t STATUS_NVM_UPDATE = 1U << 0;
static constexpr uint8_t STATUS_MEASURING = 1U << 3;
static constexpr uint8_t HUMIDITY_OVERSAMPLING_X1 = 0x01;
static constexpr uint8_t TEMPERATURE_OVERSAMPLING_X1 = 1U << 5;
static constexpr uint8_t PRESSURE_OVERSAMPLING_X1 = 1U << 2;
static constexpr uint8_t MODE_FORCED = 0x01;
static constexpr uint8_t FORCED_MEASUREMENT =
    TEMPERATURE_OVERSAMPLING_X1 | PRESSURE_OVERSAMPLING_X1 | MODE_FORCED;
static constexpr uint32_t I2C_TIMEOUT_MS = 50;
static constexpr uint32_t MEASUREMENT_TIMEOUT_MS = 100;
static constexpr uint32_t NVM_TIMEOUT_MS = 100;

struct calibration_t {
  uint16_t t1;
  int16_t t2;
  int16_t t3;
  uint16_t p1;
  int16_t p2;
  int16_t p3;
  int16_t p4;
  int16_t p5;
  int16_t p6;
  int16_t p7;
  int16_t p8;
  int16_t p9;
  uint8_t h1;
  int16_t h2;
  uint8_t h3;
  int16_t h4;
  int16_t h5;
  int8_t h6;
};

struct bme280 {
  i2c_master_dev_handle_t device;
  calibration_t calibration;
  int32_t fine_temperature;
};

static esp_err_t read_registers(handle_t handle, register_t reg, void *data,
                                size_t size);
static esp_err_t write_register(handle_t handle, register_t reg, uint8_t value);
static esp_err_t wait_status_clear(handle_t handle, uint8_t mask,
                                   uint32_t timeout_ms);
static esp_err_t read_calibration(handle_t handle);
static double compensate_temperature(handle_t handle, uint32_t raw);
static double compensate_pressure(const handle_t handle, uint32_t raw);
static double compensate_humidity(const handle_t handle, uint16_t raw);
static uint16_t read_u16_le(const uint8_t *data);
static int16_t read_s16_le(const uint8_t *data);
static int16_t sign_extend_12(uint16_t value);
static uint8_t u8(register_t reg);

esp_err_t init(const config_t &config, handle_t *out_handle) {
  if (out_handle == nullptr) {
    ESP_LOGW(TAG, "Unable to initialize: out_handle is null");
    return ESP_ERR_INVALID_ARG;
  }

  if (config.bus == nullptr) {
    ESP_LOGW(TAG, "Unable to initialize: I2C bus is null");
    return ESP_ERR_INVALID_ARG;
  }

  if (config.address != 0x76 && config.address != 0x77) {
    ESP_LOGW(TAG, "Unable to initialize: unsupported address 0x%02X",
             config.address);
    return ESP_ERR_INVALID_ARG;
  }

  if (config.clock_speed_hz == 0) {
    ESP_LOGW(TAG, "Unable to initialize: clock speed is zero");
    return ESP_ERR_INVALID_ARG;
  }

  esp_err_t err = i2c_master_probe(config.bus, config.address, I2C_TIMEOUT_MS);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Device 0x%02X is not responding: %s", config.address,
             esp_err_to_name(err));
    return err;
  }

  i2c_device_config_t device_config = {
      .dev_addr_length = I2C_ADDR_BIT_LEN_7,
      .device_address = config.address,
      .scl_speed_hz = config.clock_speed_hz,
  };

  i2c_master_dev_handle_t device;
  err = i2c_master_bus_add_device(config.bus, &device_config, &device);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Unable to add I2C device: %s", esp_err_to_name(err));
    return err;
  }

  auto handle = new bme280{
      .device = device,
      .calibration = {},
      .fine_temperature = 0,
  };

  uint8_t id;
  err = read_registers(handle, register_t::CHIP_ID, &id, sizeof(id));
  if (err != ESP_OK) {
    i2c_master_bus_rm_device(device);
    delete handle;
    return err;
  }

  if (id != CHIP_ID) {
    ESP_LOGW(TAG, "Unexpected chip ID: 0x%02X", id);
    i2c_master_bus_rm_device(device);
    delete handle;
    return ESP_ERR_NOT_FOUND;
  }

  err = write_register(handle, register_t::RESET, RESET_COMMAND);
  if (err != ESP_OK) {
    i2c_master_bus_rm_device(device);
    delete handle;
    return err;
  }

  vTaskDelay(1);
  err = wait_status_clear(handle, STATUS_NVM_UPDATE, NVM_TIMEOUT_MS);
  if (err != ESP_OK) {
    i2c_master_bus_rm_device(device);
    delete handle;
    return err;
  }

  err = read_calibration(handle);
  if (err != ESP_OK) {
    i2c_master_bus_rm_device(device);
    delete handle;
    return err;
  }

  err = write_register(handle, register_t::CONTROL_HUMIDITY,
                       HUMIDITY_OVERSAMPLING_X1);
  if (err != ESP_OK) {
    i2c_master_bus_rm_device(device);
    delete handle;
    return err;
  }

  err = write_register(handle, register_t::CONFIG, 0);
  if (err != ESP_OK) {
    i2c_master_bus_rm_device(device);
    delete handle;
    return err;
  }

  ESP_LOGI(TAG, "BME280 found at I2C address 0x%02X", config.address);
  *out_handle = handle;
  return ESP_OK;
}

esp_err_t read(handle_t handle, data_t *data) {
  if (handle == nullptr) {
    ESP_LOGW(TAG, "Unable to read: handle is null");
    return ESP_ERR_INVALID_ARG;
  }

  if (data == nullptr) {
    ESP_LOGW(TAG, "Unable to read: data is null");
    return ESP_ERR_INVALID_ARG;
  }

  esp_err_t err = write_register(handle, register_t::CONTROL_MEASUREMENT,
                                 FORCED_MEASUREMENT);
  if (err != ESP_OK) {
    return err;
  }

  vTaskDelay(1);
  err = wait_status_clear(handle, STATUS_MEASURING, MEASUREMENT_TIMEOUT_MS);
  if (err != ESP_OK) {
    return err;
  }

  std::array<uint8_t, 8> raw{};
  err =
      read_registers(handle, register_t::PRESSURE_MSB, raw.data(), raw.size());
  if (err != ESP_OK) {
    return err;
  }

  const uint32_t raw_pressure = (static_cast<uint32_t>(raw[0]) << 12) |
                                (static_cast<uint32_t>(raw[1]) << 4) |
                                (raw[2] >> 4);
  const uint32_t raw_temperature = (static_cast<uint32_t>(raw[3]) << 12) |
                                   (static_cast<uint32_t>(raw[4]) << 4) |
                                   (raw[5] >> 4);
  const uint16_t raw_humidity =
      static_cast<uint16_t>((static_cast<uint16_t>(raw[6]) << 8) | raw[7]);

  if (raw_temperature == 0x80000) {
    ESP_LOGW(TAG, "Invalid raw temperature");
    return ESP_ERR_INVALID_RESPONSE;
  }

  if (raw_pressure == 0x80000) {
    ESP_LOGW(TAG, "Invalid raw pressure");
    return ESP_ERR_INVALID_RESPONSE;
  }

  if (raw_humidity == 0x8000) {
    ESP_LOGW(TAG, "Invalid raw humidity");
    return ESP_ERR_INVALID_RESPONSE;
  }

  const double temperature_c = compensate_temperature(handle, raw_temperature);
  const double pressure_pa = compensate_pressure(handle, raw_pressure);
  const double humidity_percent = compensate_humidity(handle, raw_humidity);

  if (!std::isfinite(pressure_pa)) {
    ESP_LOGW(TAG, "Unable to compensate pressure");
    return ESP_ERR_INVALID_RESPONSE;
  }

  *data = {
      .temperature_c = static_cast<float>(temperature_c),
      .pressure_hpa = static_cast<float>(pressure_pa / 100.0),
      .humidity_percent = static_cast<float>(humidity_percent),
  };
  return ESP_OK;
}

esp_err_t read_registers(handle_t handle, register_t reg, void *data,
                         size_t size) {
  if (data == nullptr) {
    ESP_LOGW(TAG, "Unable to read registers: data is null");
    return ESP_ERR_INVALID_ARG;
  }

  if (size == 0) {
    ESP_LOGW(TAG, "Unable to read registers: size is zero");
    return ESP_ERR_INVALID_ARG;
  }

  const uint8_t address = u8(reg);
  return i2c_master_transmit_receive(handle->device, &address, sizeof(address),
                                     static_cast<uint8_t *>(data), size,
                                     I2C_TIMEOUT_MS);
}

esp_err_t write_register(handle_t handle, register_t reg, uint8_t value) {
  const std::array<uint8_t, 2> data = {u8(reg), value};
  return i2c_master_transmit(handle->device, data.data(), data.size(),
                             I2C_TIMEOUT_MS);
}

esp_err_t wait_status_clear(handle_t handle, uint8_t mask,
                            uint32_t timeout_ms) {
  const int64_t deadline_us =
      esp_timer_get_time() + static_cast<int64_t>(timeout_ms) * 1'000;

  while (esp_timer_get_time() < deadline_us) {
    uint8_t status;
    esp_err_t err =
        read_registers(handle, register_t::STATUS, &status, sizeof(status));
    if (err != ESP_OK) {
      return err;
    }

    if ((status & mask) == 0) {
      return ESP_OK;
    }

    vTaskDelay(1);
  }

  ESP_LOGW(TAG, "Status wait timed out, mask 0x%02X", mask);
  return ESP_ERR_TIMEOUT;
}

esp_err_t read_calibration(handle_t handle) {
  std::array<uint8_t, 26> first{};
  esp_err_t err = read_registers(handle, register_t::CALIBRATION_1,
                                 first.data(), first.size());
  if (err != ESP_OK) {
    return err;
  }

  std::array<uint8_t, 7> second{};
  err = read_registers(handle, register_t::CALIBRATION_2, second.data(),
                       second.size());
  if (err != ESP_OK) {
    return err;
  }

  handle->calibration = {
      .t1 = read_u16_le(&first[0]),
      .t2 = read_s16_le(&first[2]),
      .t3 = read_s16_le(&first[4]),
      .p1 = read_u16_le(&first[6]),
      .p2 = read_s16_le(&first[8]),
      .p3 = read_s16_le(&first[10]),
      .p4 = read_s16_le(&first[12]),
      .p5 = read_s16_le(&first[14]),
      .p6 = read_s16_le(&first[16]),
      .p7 = read_s16_le(&first[18]),
      .p8 = read_s16_le(&first[20]),
      .p9 = read_s16_le(&first[22]),
      .h1 = first[25],
      .h2 = read_s16_le(&second[0]),
      .h3 = second[2],
      .h4 = sign_extend_12(
          static_cast<uint16_t>((second[3] << 4) | (second[4] & 0x0F))),
      .h5 = sign_extend_12(
          static_cast<uint16_t>((second[5] << 4) | (second[4] >> 4))),
      .h6 = static_cast<int8_t>(second[6]),
  };

  if (handle->calibration.t1 == 0 || handle->calibration.p1 == 0) {
    ESP_LOGW(TAG, "Invalid calibration data");
    return ESP_ERR_INVALID_RESPONSE;
  }

  return ESP_OK;
}

double compensate_temperature(handle_t handle, uint32_t raw) {
  const calibration_t &cal = handle->calibration;
  const double var1 =
      (static_cast<double>(raw) / 16'384.0 - cal.t1 / 1'024.0) * cal.t2;
  const double base = static_cast<double>(raw) / 131'072.0 - cal.t1 / 8'192.0;
  const double var2 = base * base * cal.t3;
  const double fine_temperature = var1 + var2;
  handle->fine_temperature = static_cast<int32_t>(fine_temperature);
  return std::clamp(fine_temperature / 5'120.0, -40.0, 85.0);
}

double compensate_pressure(const handle_t handle, uint32_t raw) {
  const calibration_t &cal = handle->calibration;
  double var1 = handle->fine_temperature / 2.0 - 64'000.0;
  double var2 = var1 * var1 * cal.p6 / 32'768.0;
  var2 += var1 * cal.p5 * 2.0;
  var2 = var2 / 4.0 + cal.p4 * 65'536.0;
  var1 = (cal.p3 * var1 * var1 / 524'288.0 + cal.p2 * var1) / 524'288.0;
  var1 = (1.0 + var1 / 32'768.0) * cal.p1;

  if (var1 <= 0.0) {
    return NAN;
  }

  double pressure = 1'048'576.0 - raw;
  pressure = (pressure - var2 / 4'096.0) * 6'250.0 / var1;
  var1 = cal.p9 * pressure * pressure / 2'147'483'648.0;
  var2 = pressure * cal.p8 / 32'768.0;
  pressure += (var1 + var2 + cal.p7) / 16.0;
  return std::clamp(pressure, 30'000.0, 110'000.0);
}

double compensate_humidity(const handle_t handle, uint16_t raw) {
  const calibration_t &cal = handle->calibration;
  const double var1 = handle->fine_temperature - 76'800.0;
  const double var2 = cal.h4 * 64.0 + cal.h5 / 16'384.0 * var1;
  const double var3 = raw - var2;
  const double var4 = cal.h2 / 65'536.0;
  const double var5 = 1.0 + cal.h3 / 67'108'864.0 * var1;
  const double var6 = 1.0 + cal.h6 / 67'108'864.0 * var1 * var5;
  double humidity = var3 * var4 * var5 * var6;
  humidity *= 1.0 - cal.h1 * humidity / 524'288.0;
  return std::clamp(humidity, 0.0, 100.0);
}

uint16_t read_u16_le(const uint8_t *data) {
  return static_cast<uint16_t>(data[0]) | static_cast<uint16_t>(data[1] << 8);
}

int16_t read_s16_le(const uint8_t *data) {
  return static_cast<int16_t>(read_u16_le(data));
}

int16_t sign_extend_12(uint16_t value) {
  if ((value & 0x0800) != 0) {
    value |= 0xF000;
  }
  return static_cast<int16_t>(value);
}

uint8_t u8(register_t reg) { return static_cast<uint8_t>(reg); }

} // namespace bme280
