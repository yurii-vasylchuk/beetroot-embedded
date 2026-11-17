#include "spi_flash_driver.h"

#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <algorithm>
#include <array>
#include <cstring>

namespace spi_flash {

static constexpr const char *TAG = "SPI_NOR";

enum class command_t : uint8_t {
  COMMAND_READ = 0x03,
  COMMAND_PAGE_PROGRAM = 0x02,
  COMMAND_WRITE_ENABLE = 0x06,
  COMMAND_READ_STATUS_1 = 0x05,
  COMMAND_SECTOR_ERASE = 0x20,
  COMMAND_JEDEC_ID = 0x9F,
};

static constexpr uint8_t STATUS_BUSY = 1U << 0;
static constexpr uint8_t STATUS_WRITE_ENABLE_LATCH = 1U << 1;

static constexpr size_t ADDRESS_SIZE = 3;
static constexpr size_t COMMAND_AND_ADDRESS_SIZE = 1 + ADDRESS_SIZE;
static constexpr size_t MAX_DATA_PER_TRANSACTION = 60;
static constexpr size_t MAX_TRANSACTION_SIZE =
    MAX_DATA_PER_TRANSACTION + COMMAND_AND_ADDRESS_SIZE;
static constexpr uint32_t PROGRAM_TIMEOUT_MS = 500;
static constexpr uint32_t ERASE_TIMEOUT_MS = 5'000;

struct spi_flash {
  spi_host_device_t host;
  spi_device_handle_t device;
  uint32_t capacity_bytes;
  uint8_t jedec_manufacturer;
  uint8_t jedec_memory_type;
  uint8_t jedec_capacity;
};

static esp_err_t transmit(handle_t handle, const void *tx, void *rx,
                          size_t size);
static esp_err_t read_status(handle_t handle, uint8_t *status);
static esp_err_t wait_until_ready(handle_t handle, uint32_t timeout_ms);
static esp_err_t enable_write(handle_t handle);
static esp_err_t validate_range(handle_t handle, uint32_t address, size_t size);
static esp_err_t validate_jedec(uint8_t capacity);
static void destroy(handle_t handle);
static uint8_t u8(command_t command);

esp_err_t init(const config_t &config, handle_t *out_handle) {
  if (out_handle == nullptr) {
    ESP_LOGW(TAG, "Unable to initialize: out_handle is null");
    return ESP_ERR_INVALID_ARG;
  }

  if (!GPIO_IS_VALID_OUTPUT_GPIO(config.mosi_gpio)) {
    ESP_LOGW(TAG, "Unable to initialize: invalid MOSI GPIO%d",
             config.mosi_gpio);
    return ESP_ERR_INVALID_ARG;
  }

  if (!GPIO_IS_VALID_GPIO(config.miso_gpio)) {
    ESP_LOGW(TAG, "Unable to initialize: invalid MISO GPIO%d",
             config.miso_gpio);
    return ESP_ERR_INVALID_ARG;
  }

  if (!GPIO_IS_VALID_OUTPUT_GPIO(config.sck_gpio)) {
    ESP_LOGW(TAG, "Unable to initialize: invalid SCK GPIO%d", config.sck_gpio);
    return ESP_ERR_INVALID_ARG;
  }

  if (!GPIO_IS_VALID_OUTPUT_GPIO(config.cs_gpio)) {
    ESP_LOGW(TAG, "Unable to initialize: invalid CS GPIO%d", config.cs_gpio);
    return ESP_ERR_INVALID_ARG;
  }

  if (config.clock_speed_hz <= 0) {
    ESP_LOGW(TAG, "Unable to initialize: clock speed must be positive");
    return ESP_ERR_INVALID_ARG;
  }

  spi_bus_config_t bus_config = {
      .mosi_io_num = config.mosi_gpio,
      .miso_io_num = config.miso_gpio,
      .sclk_io_num = config.sck_gpio,
      .quadwp_io_num = -1,
      .quadhd_io_num = -1,
      .data4_io_num = -1,
      .data5_io_num = -1,
      .data6_io_num = -1,
      .data7_io_num = -1,
      .max_transfer_sz = MAX_TRANSACTION_SIZE,
  };

  esp_err_t err =
      spi_bus_initialize(config.host, &bus_config, SPI_DMA_DISABLED);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Unable to initialize SPI bus: %s", esp_err_to_name(err));
    return err;
  }

  spi_device_interface_config_t device_config = {
      .mode = 0,
      .clock_speed_hz = config.clock_speed_hz,
      .spics_io_num = config.cs_gpio,
      .queue_size = 1,
  };

  spi_device_handle_t device;
  err = spi_bus_add_device(config.host, &device_config, &device);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Unable to add SPI flash device: %s", esp_err_to_name(err));
    spi_bus_free(config.host);
    return err;
  }

  auto handle = new spi_flash{
      .host = config.host,
      .device = device,
      .capacity_bytes = 0,
      .jedec_manufacturer = 0,
      .jedec_memory_type = 0,
      .jedec_capacity = 0,
  };

  // A reset can happen while the external chip is still finishing an erase.
  // Status polling is valid in that state and avoids misreading the JEDEC ID.
  err = wait_until_ready(handle, ERASE_TIMEOUT_MS);
  if (err != ESP_OK) {
    destroy(handle);
    return err;
  }

  const std::array<uint8_t, 4> tx = {u8(command_t::COMMAND_JEDEC_ID), 0, 0, 0};
  std::array<uint8_t, 4> rx{};
  err = transmit(handle, tx.data(), rx.data(), tx.size());
  if (err != ESP_OK) {
    destroy(handle);
    return err;
  }

  handle->jedec_manufacturer = rx[1];
  handle->jedec_memory_type = rx[2];
  handle->jedec_capacity = rx[3];

  err = validate_jedec(handle->jedec_capacity);
  if (err != ESP_OK) {
    destroy(handle);
    return err;
  }

  handle->capacity_bytes = 1UL << rx[3];
  ESP_LOGI(TAG, "JEDEC ID: %02X %02X %02X, capacity: %lu bytes", rx[1], rx[2],
           rx[3], static_cast<unsigned long>(handle->capacity_bytes));

  *out_handle = handle;
  return ESP_OK;
}

esp_err_t read(handle_t handle, uint32_t address, void *data, size_t size) {
  if (handle == nullptr) {
    ESP_LOGW(TAG, "Unable to read: handle is null");
    return ESP_ERR_INVALID_ARG;
  }

  if (data == nullptr) {
    ESP_LOGW(TAG, "Unable to read: data is null");
    return ESP_ERR_INVALID_ARG;
  }

  if (size == 0) {
    ESP_LOGW(TAG, "Unable to read: size is zero");
    return ESP_ERR_INVALID_ARG;
  }

  if (size > MAX_DATA_PER_TRANSACTION) {
    ESP_LOGW(TAG, "Unable to read: size %u exceeds transaction limit %u",
             static_cast<unsigned>(size),
             static_cast<unsigned>(MAX_DATA_PER_TRANSACTION));
    return ESP_ERR_INVALID_ARG;
  }

  esp_err_t err = validate_range(handle, address, size);
  if (err != ESP_OK) {
    return err;
  }

  std::array<uint8_t, MAX_TRANSACTION_SIZE> tx{};
  std::array<uint8_t, MAX_TRANSACTION_SIZE> rx{};

  tx[0] = u8(command_t::COMMAND_READ);
  tx[1] = static_cast<uint8_t>(address >> 16);
  tx[2] = static_cast<uint8_t>(address >> 8);
  tx[3] = static_cast<uint8_t>(address);

  const size_t transaction_size = size + COMMAND_AND_ADDRESS_SIZE;
  err = transmit(handle, tx.data(), rx.data(), transaction_size);
  if (err == ESP_OK) {
    std::memcpy(data, rx.data() + COMMAND_AND_ADDRESS_SIZE, size);
  }
  return err;
}

esp_err_t write(handle_t handle, uint32_t address, const void *data,
                size_t size) {
  if (handle == nullptr) {
    ESP_LOGW(TAG, "Unable to write: handle is null");
    return ESP_ERR_INVALID_ARG;
  }

  if (data == nullptr) {
    ESP_LOGW(TAG, "Unable to write: data is null");
    return ESP_ERR_INVALID_ARG;
  }

  if (size == 0) {
    ESP_LOGW(TAG, "Unable to write: size is zero");
    return ESP_ERR_INVALID_ARG;
  }

  esp_err_t err = validate_range(handle, address, size);
  if (err != ESP_OK) {
    return err;
  }

  const auto *source = static_cast<const uint8_t *>(data);
  size_t remaining = size;

  while (remaining > 0) {
    const size_t page_space = PAGE_SIZE - (address % PAGE_SIZE);
    const size_t chunk_size =
        std::min({remaining, page_space, MAX_DATA_PER_TRANSACTION});

    ESP_RETURN_ON_ERROR(enable_write(handle), TAG,
                        "Unable to enable flash write");

    std::array<uint8_t, MAX_TRANSACTION_SIZE> tx{};
    tx[0] = u8(command_t::COMMAND_PAGE_PROGRAM);
    tx[1] = static_cast<uint8_t>(address >> 16);
    tx[2] = static_cast<uint8_t>(address >> 8);
    tx[3] = static_cast<uint8_t>(address);
    std::memcpy(tx.data() + COMMAND_AND_ADDRESS_SIZE, source, chunk_size);

    ESP_RETURN_ON_ERROR(transmit(handle, tx.data(), nullptr,
                                 chunk_size + COMMAND_AND_ADDRESS_SIZE),
                        TAG, "Unable to program flash page");
    ESP_RETURN_ON_ERROR(wait_until_ready(handle, PROGRAM_TIMEOUT_MS), TAG,
                        "Flash page program timeout");

    address += chunk_size;
    source += chunk_size;
    remaining -= chunk_size;
  }

  return ESP_OK;
}

esp_err_t erase_sector(handle_t handle, uint32_t address) {
  if (handle == nullptr) {
    ESP_LOGW(TAG, "Unable to erase sector: handle is null");
    return ESP_ERR_INVALID_ARG;
  }

  if (address % SECTOR_SIZE != 0) {
    ESP_LOGW(TAG, "Unable to erase sector: address 0x%06lX is not aligned",
             static_cast<unsigned long>(address));
    return ESP_ERR_INVALID_ARG;
  }

  esp_err_t err = validate_range(handle, address, SECTOR_SIZE);
  if (err != ESP_OK) {
    return err;
  }

  ESP_RETURN_ON_ERROR(enable_write(handle), TAG,
                      "Unable to enable sector erase");

  const std::array<uint8_t, COMMAND_AND_ADDRESS_SIZE> command = {
      u8(command_t::COMMAND_SECTOR_ERASE),
      static_cast<uint8_t>(address >> 16),
      static_cast<uint8_t>(address >> 8),
      static_cast<uint8_t>(address),
  };

  ESP_RETURN_ON_ERROR(transmit(handle, command.data(), nullptr, command.size()),
                      TAG, "Unable to send sector erase command");
  return wait_until_ready(handle, ERASE_TIMEOUT_MS);
}

uint32_t capacity(handle_t handle) {
  if (handle == nullptr) {
    ESP_LOGW(TAG, "Unable to get capacity: handle is null");
    return 0;
  }

  return handle->capacity_bytes;
}

esp_err_t transmit(handle_t handle, const void *tx, void *rx, size_t size) {
  if (handle == nullptr) {
    ESP_LOGW(TAG, "Unable to transmit: handle is null");
    return ESP_ERR_INVALID_ARG;
  }

  if (tx == nullptr) {
    ESP_LOGW(TAG, "Unable to transmit: tx is null");
    return ESP_ERR_INVALID_ARG;
  }

  if (size == 0) {
    ESP_LOGW(TAG, "Unable to transmit: size is zero");
    return ESP_ERR_INVALID_ARG;
  }

  spi_transaction_t transaction = {
      .length = size * 8,
      .tx_buffer = tx,
      .rx_buffer = rx,
  };

  return spi_device_polling_transmit(handle->device, &transaction);
}

esp_err_t read_status(handle_t handle, uint8_t *status) {
  if (handle == nullptr) {
    ESP_LOGW(TAG, "Unable to read status: handle is null");
    return ESP_ERR_INVALID_ARG;
  }

  if (status == nullptr) {
    ESP_LOGW(TAG, "Unable to read status: status is null");
    return ESP_ERR_INVALID_ARG;
  }

  const std::array<uint8_t, 2> tx = {u8(command_t::COMMAND_READ_STATUS_1), 0};
  std::array<uint8_t, 2> rx{};
  ESP_RETURN_ON_ERROR(transmit(handle, tx.data(), rx.data(), tx.size()), TAG,
                      "Unable to read flash status");

  *status = rx[1];
  return ESP_OK;
}

esp_err_t wait_until_ready(handle_t handle, uint32_t timeout_ms) {
  if (handle == nullptr) {
    ESP_LOGW(TAG, "Unable to wait until ready: handle is null");
    return ESP_ERR_INVALID_ARG;
  }

  if (timeout_ms == 0) {
    ESP_LOGW(TAG, "Unable to wait until ready: timeout is zero");
    return ESP_ERR_INVALID_ARG;
  }

  const int64_t deadline_us =
      esp_timer_get_time() + static_cast<int64_t>(timeout_ms) * 1'000;

  while (esp_timer_get_time() < deadline_us) {
    uint8_t status;
    ESP_RETURN_ON_ERROR(read_status(handle, &status), TAG,
                        "Unable to wait for flash");
    if ((status & STATUS_BUSY) == 0) {
      return ESP_OK;
    }
    vTaskDelay(pdMS_TO_TICKS(1));
  }

  return ESP_ERR_TIMEOUT;
}

esp_err_t enable_write(handle_t handle) {
  if (handle == nullptr) {
    ESP_LOGW(TAG, "Unable to enable write: handle is null");
    return ESP_ERR_INVALID_ARG;
  }

  const uint8_t command = u8(command_t::COMMAND_WRITE_ENABLE);
  ESP_RETURN_ON_ERROR(transmit(handle, &command, nullptr, sizeof(command)), TAG,
                      "Unable to send write-enable command");

  uint8_t status;
  ESP_RETURN_ON_ERROR(read_status(handle, &status), TAG,
                      "Unable to confirm write-enable latch");
  if ((status & STATUS_WRITE_ENABLE_LATCH) == 0) {
    ESP_LOGW(TAG, "Flash did not set the write-enable latch");
    return ESP_ERR_INVALID_STATE;
  }

  return ESP_OK;
}

esp_err_t validate_range(handle_t handle, uint32_t address, size_t size) {
  const uint64_t end = static_cast<uint64_t>(address) + size;

  if (end > handle->capacity_bytes) {
    ESP_LOGW(TAG,
             "Invalid range: address 0x%06lX and size %u exceed capacity %lu",
             static_cast<unsigned long>(address), static_cast<unsigned>(size),
             static_cast<unsigned long>(handle->capacity_bytes));
    return ESP_ERR_INVALID_ARG;
  }

  return ESP_OK;
}

esp_err_t validate_jedec(uint8_t capacity) {
  if (capacity < 12) {
    ESP_LOGW(TAG, "Unsupported JEDEC capacity code %02X", capacity);
    return ESP_ERR_NOT_SUPPORTED;
  }

  if (capacity > 24) {
    ESP_LOGW(TAG, "JEDEC capacity code %02X requires 4-byte addressing",
             capacity);
    return ESP_ERR_NOT_SUPPORTED;
  }

  return ESP_OK;
}

void destroy(handle_t handle) {
  spi_bus_remove_device(handle->device);
  spi_bus_free(handle->host);
  delete handle;
}

uint8_t u8(command_t command) { return static_cast<uint8_t>(command); }

} // namespace spi_flash
