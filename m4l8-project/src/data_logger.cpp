#include "data_logger.h"

#include "app_config.h"
#include "bme280_driver.h"
#include "environment_log.h"
#include "environment_sample.h"
#include "spi_flash_driver.h"
#include "ssd1306_driver.h"

#include "driver/i2c_master.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include <cstdio>

namespace data_logger {

static constexpr const char *TAG = "M4L8";

static bme280::handle_t bme_handle;
static ssd1306::handle_t display_handle;
static spi_flash::handle_t flash_handle;
static environment_log::handle_t log_store_handle;
static i2c_master_bus_handle_t i2c_bus;

static QueueHandle_t flash_queue;
static QueueHandle_t display_queue;

static esp_err_t init_i2c_bus();
static esp_err_t init_bme();
static esp_err_t init_display();
static esp_err_t init_flash();
static esp_err_t init_queues();
static esp_err_t init_tasks();

static void sensor_task(void *);
static void flash_task(void *);
static void display_task(void *);

esp_err_t start() {
  ESP_LOGI(TAG, "Starting BME280 data logger");

  esp_err_t err = init_i2c_bus();
  if (err != ESP_OK) {
    return err;
  }

  err = init_bme();
  if (err != ESP_OK) {
    return err;
  }

  err = init_display();
  if (err != ESP_OK) {
    return err;
  }

  err = init_flash();
  if (err != ESP_OK) {
    return err;
  }

  err = init_queues();
  if (err != ESP_OK) {
    return err;
  }

  return init_tasks();
}

esp_err_t init_i2c_bus() {
  i2c_master_bus_config_t i2c_config = {
      .i2c_port = app_config::I2C_PORT,
      .sda_io_num = app_config::I2C_SDA_GPIO,
      .scl_io_num = app_config::I2C_SCL_GPIO,
      .clk_source = I2C_CLK_SRC_DEFAULT,
      .glitch_ignore_cnt = 7,
      .flags =
          {
              .enable_internal_pullup = true,
          },
  };

  esp_err_t err = i2c_new_master_bus(&i2c_config, &i2c_bus);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Unable to initialize I2C bus: %s", esp_err_to_name(err));
  }
  return err;
}

esp_err_t init_bme() {
  const bme280::config_t config = {
      .bus = i2c_bus,
      .address = app_config::BME280_I2C_ADDRESS,
      .clock_speed_hz = app_config::I2C_CLOCK_HZ,
  };

  esp_err_t err = bme280::init(config, &bme_handle);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Unable to initialize BME280: %s", esp_err_to_name(err));
  }
  return err;
}

esp_err_t init_display() {
  ssd1306::config_t display_config = {
      .bus = i2c_bus,
  };
  display_handle = ssd1306::init(display_config);
  if (display_handle == nullptr) {
    return ESP_ERR_NOT_FOUND;
  }

  return ESP_OK;
}

esp_err_t init_flash() {
  const spi_flash::config_t flash_config = {
      .host = app_config::FLASH_SPI_HOST,
      .mosi_gpio = app_config::FLASH_MOSI_GPIO,
      .miso_gpio = app_config::FLASH_MISO_GPIO,
      .sck_gpio = app_config::FLASH_SCK_GPIO,
      .cs_gpio = app_config::FLASH_CS_GPIO,
      .clock_speed_hz = app_config::FLASH_SPI_CLOCK_HZ,
  };

  esp_err_t err = spi_flash::init(flash_config, &flash_handle);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Unable to initialize external flash: %s",
             esp_err_to_name(err));
    return err;
  }

  err = environment_log::init(flash_handle, &log_store_handle);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Unable to initialize environment log: %s",
             esp_err_to_name(err));
  }
  return err;
}

esp_err_t init_queues() {
  flash_queue = xQueueCreate(app_config::FLASH_QUEUE_LENGTH,
                             sizeof(environment_sample_t));
  display_queue = xQueueCreate(app_config::DISPLAY_QUEUE_LENGTH,
                               sizeof(environment_sample_t));

  if (flash_queue == nullptr) {
    ESP_LOGW(TAG, "Unable to create flash queue");
    return ESP_ERR_NO_MEM;
  }

  if (display_queue == nullptr) {
    ESP_LOGW(TAG, "Unable to create display queue");
    return ESP_ERR_NO_MEM;
  }

  return ESP_OK;
}

esp_err_t init_tasks() {
  BaseType_t result =
      xTaskCreate(flash_task, "flash_writer", 4'096, nullptr, 4, nullptr);
  if (result != pdPASS) {
    ESP_LOGE(TAG, "Unable to start flash task");
    return ESP_ERR_NO_MEM;
  }

  result = xTaskCreate(display_task, "display", 4'096, nullptr, 3, nullptr);
  if (result != pdPASS) {
    ESP_LOGE(TAG, "Unable to start display task");
    return ESP_ERR_NO_MEM;
  }

  result = xTaskCreate(sensor_task, "bme280", 4'096, nullptr, 5, nullptr);
  if (result != pdPASS) {
    ESP_LOGE(TAG, "Unable to start BME280 task");
    return ESP_ERR_NO_MEM;
  }

  return ESP_OK;
}

void sensor_task(void *) {
  while (true) {
    bme280::data_t sensor_data{};
    const esp_err_t err = bme280::read(bme_handle, &sensor_data);

    if (err == ESP_OK) {
      environment_sample_t sample{
          .uptime_ms = esp_timer_get_time() / 1'000,
          .temperature_c = sensor_data.temperature_c,
          .pressure_hpa = sensor_data.pressure_hpa,
          .humidity_percent = sensor_data.humidity_percent,
      };

      ESP_LOGI(TAG, "Temperature: %.2f C, pressure: %.2f hPa, humidity: %.2f%%",
               sample.temperature_c, sample.pressure_hpa,
               sample.humidity_percent);

      xQueueSend(flash_queue, &sample, portMAX_DELAY);
      xQueueSend(display_queue, &sample, portMAX_DELAY);
    } else {
      ESP_LOGW(TAG, "Unable to read BME280: %s", esp_err_to_name(err));
    }

    vTaskDelay(pdMS_TO_TICKS(app_config::SENSOR_DELAY_MS));
  }
}

void flash_task(void *) {
  environment_sample_t sample{};

  while (true) {
    if (xQueueReceive(flash_queue, &sample, portMAX_DELAY) != pdTRUE) {
      continue;
    }

    const esp_err_t err = environment_log::append(log_store_handle, sample);
    if (err != ESP_OK) {
      ESP_LOGE(TAG, "Unable to append environment record: %s",
               esp_err_to_name(err));
    }
  }
}

void display_task(void *) {
  environment_sample_t sample{};

  while (true) {
    if (xQueueReceive(display_queue, &sample, portMAX_DELAY) != pdTRUE) {
      continue;
    }

    char temperature_text[17];
    char pressure_text[17];
    char humidity_text[17];

    std::snprintf(temperature_text, sizeof(temperature_text), "T: %.2f C",
                  static_cast<double>(sample.temperature_c));
    std::snprintf(pressure_text, sizeof(pressure_text), "P: %.2f hPa",
                  static_cast<double>(sample.pressure_hpa));
    std::snprintf(humidity_text, sizeof(humidity_text), "H: %.2f %%",
                  static_cast<double>(sample.humidity_percent));

    ssd1306::clear(display_handle);
    ssd1306::text(display_handle, 0, 0, temperature_text);
    ssd1306::text(display_handle, 0, 16, pressure_text);
    ssd1306::text(display_handle, 0, 32, humidity_text);
    ssd1306::update(display_handle);
  }
}

} // namespace data_logger
