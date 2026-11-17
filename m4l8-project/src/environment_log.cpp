#include "environment_log.h"

#include "esp_log.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace environment_log {

static constexpr const char *TAG = "ENV_LOG";

struct record_t {
  int64_t uptime_ms;
  float temperature_c;
  float pressure_hpa;
  float humidity_percent;
  uint8_t padding[12];
};

static_assert(sizeof(record_t) == 32);
static_assert(spi_flash::SECTOR_SIZE % sizeof(record_t) == 0);

struct environment_log {
  spi_flash::handle_t flash;
  uint32_t capacity_bytes;
  uint32_t write_address;
};

static bool is_erased(const void *data, size_t size);
static esp_err_t find_write_address(handle_t handle);

esp_err_t init(spi_flash::handle_t flash, handle_t *out_handle) {
  if (flash == nullptr) {
    ESP_LOGW(TAG, "Unable to initialize: flash handle is null");
    return ESP_ERR_INVALID_ARG;
  }

  if (out_handle == nullptr) {
    ESP_LOGW(TAG, "Unable to initialize: out_handle is null");
    return ESP_ERR_INVALID_ARG;
  }

  const uint32_t capacity_bytes = spi_flash::capacity(flash);
  if (capacity_bytes < spi_flash::SECTOR_SIZE) {
    ESP_LOGW(TAG,
             "Unable to initialize: flash capacity is less than one sector");
    return ESP_ERR_INVALID_ARG;
  }

  auto handle = new environment_log{
      .flash = flash,
      .capacity_bytes = capacity_bytes,
      .write_address = 0,
  };

  esp_err_t err = find_write_address(handle);
  if (err != ESP_OK) {
    delete handle;
    return err;
  }

  ESP_LOGI(TAG, "Next record address: 0x%06lX",
           static_cast<unsigned long>(handle->write_address));
  *out_handle = handle;
  return ESP_OK;
}

esp_err_t append(handle_t handle, const environment_sample_t &sample) {
  if (handle == nullptr) {
    ESP_LOGW(TAG, "Unable to append: handle is null");
    return ESP_ERR_INVALID_ARG;
  }

  if (handle->write_address >= handle->capacity_bytes) {
    handle->write_address = 0;
  }

  const uint32_t address = handle->write_address;
  if (address % spi_flash::SECTOR_SIZE == 0) {
    esp_err_t err = spi_flash::erase_sector(handle->flash, address);
    if (err != ESP_OK) {
      return err;
    }
  }

  const record_t record{
      .uptime_ms = sample.uptime_ms,
      .temperature_c = sample.temperature_c,
      .pressure_hpa = sample.pressure_hpa,
      .humidity_percent = sample.humidity_percent,
      .padding = {},
  };

  handle->write_address += sizeof(record_t);

  esp_err_t err =
      spi_flash::write(handle->flash, address, &record, sizeof(record));
  if (err != ESP_OK) {
    return err;
  }

  return ESP_OK;
}

bool is_erased(const void *data, size_t size) {
  const auto *bytes = static_cast<const uint8_t *>(data);
  for (size_t index = 0; index < size; ++index) {
    if (bytes[index] != 0xFF) {
      return false;
    }
  }
  return true;
}

esp_err_t find_write_address(handle_t handle) {
  for (uint32_t sector_address = 0; sector_address < handle->capacity_bytes;
       sector_address += spi_flash::SECTOR_SIZE) {
    const uint32_t last_record_address =
        sector_address + spi_flash::SECTOR_SIZE - sizeof(record_t);
    record_t last_record{};
    esp_err_t err = spi_flash::read(handle->flash, last_record_address,
                                    &last_record, sizeof(last_record));
    if (err != ESP_OK) {
      return err;
    }

    if (!is_erased(&last_record, sizeof(last_record))) {
      continue;
    }

    for (uint32_t address = sector_address; address <= last_record_address;
         address += sizeof(record_t)) {
      record_t record{};
      err = spi_flash::read(handle->flash, address, &record, sizeof(record));
      if (err != ESP_OK) {
        return err;
      }

      if (is_erased(&record, sizeof(record))) {
        handle->write_address = address;
        return ESP_OK;
      }
    }
  }

  handle->write_address = 0;
  return ESP_OK;
}

} // namespace environment_log
