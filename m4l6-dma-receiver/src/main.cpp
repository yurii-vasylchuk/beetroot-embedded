#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "hal/adc_types.h"
#include "soc/soc_caps.h"
#include <cstring>

static const char *TAG = "M4L6_RX";
static constexpr uint32_t RESTART_DELAY = 1000;

// UART
static constexpr uart_port_t UART_PORT = UART_NUM_1;
static constexpr gpio_num_t UART_RX_GPIO = GPIO_NUM_18;
static constexpr int UART_BAUD_RATE = 921600;
static constexpr size_t UART_DRIVER_BUFFER_SIZE = 2048;
static constexpr size_t UART_READ_BUFFER_SIZE = 256;

// Moving average
static constexpr size_t AVERAGE_WINDOW_SIZE = 64;
static constexpr size_t PRINT_EVERY_SAMPLES = 2000;

struct moving_average_t {
  uint16_t values[AVERAGE_WINDOW_SIZE] = {};
  uint32_t sum = 0;
  size_t next = 0;
  size_t count = 0;
};

static void init_uart();
static void init_tasks();

static uint16_t add_sample(moving_average_t &, uint16_t);
static void uart_receive_task(void *);

extern "C" void app_main() {
  init_uart();
  init_tasks();
}

void init_uart() {
  esp_err_t err = ESP_OK;

  uart_config_t uart_config = {
      .baud_rate = UART_BAUD_RATE,
      .data_bits = UART_DATA_8_BITS,
      .parity = UART_PARITY_DISABLE,
      .stop_bits = UART_STOP_BITS_1,
      .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
      .rx_flow_ctrl_thresh = 0,
      .source_clk = UART_SCLK_DEFAULT,
  };

  err = uart_param_config(UART_PORT, &uart_config);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Unable to configure UART parameters, ERR: %s",
             esp_err_to_name(err));
    vTaskDelay(pdMS_TO_TICKS(RESTART_DELAY));
    esp_restart();
  }

  err = uart_set_pin(UART_PORT, UART_PIN_NO_CHANGE, UART_RX_GPIO,
                     UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Unable to configure UART pins, ERR: %s",
             esp_err_to_name(err));
    vTaskDelay(pdMS_TO_TICKS(RESTART_DELAY));
    esp_restart();
  }

  err = uart_driver_install(UART_PORT, UART_DRIVER_BUFFER_SIZE, 0, 0, nullptr,
                            0);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Unable to install UART driver, ERR: %s",
             esp_err_to_name(err));
    vTaskDelay(pdMS_TO_TICKS(RESTART_DELAY));
    esp_restart();
  }
}

void init_tasks() {
  BaseType_t result =
      xTaskCreate(uart_receive_task, "uart_receive", 4096, nullptr, 5, nullptr);

  if (result != pdPASS) {
    ESP_LOGE(TAG, "Unable to start 'uart_receive_task'");
    vTaskDelay(pdMS_TO_TICKS(RESTART_DELAY));
    esp_restart();
  }
}

uint16_t add_sample(moving_average_t &average, uint16_t value) {
  if (average.count < AVERAGE_WINDOW_SIZE) {
    average.count++;
  } else {
    average.sum -= average.values[average.next];
  }

  average.values[average.next] = value;
  average.sum += value;
  average.next = (average.next + 1) % AVERAGE_WINDOW_SIZE;

  return static_cast<uint16_t>(average.sum / average.count);
}

void uart_receive_task(void *arg) {
  uint8_t buffer[UART_READ_BUFFER_SIZE + sizeof(adc_digi_output_data_t)];
  size_t pending_bytes = 0;
  size_t samples_since_print = 0;
  uint32_t invalid_samples = 0;
  moving_average_t average;

  while (true) {
    int received = uart_read_bytes(
        UART_PORT, buffer + pending_bytes, UART_READ_BUFFER_SIZE - pending_bytes,
        pdMS_TO_TICKS(1000));

    if (received < 0) {
      ESP_LOGW(TAG, "Unable to read data from UART");
      continue;
    }

    if (received == 0) {
      continue;
    }

    size_t total_bytes = pending_bytes + static_cast<size_t>(received);
    size_t complete_bytes =
        total_bytes - total_bytes % sizeof(adc_digi_output_data_t);
    uint16_t latest_raw = 0;
    uint16_t latest_average = 0;

    for (size_t offset = 0; offset < complete_bytes;
         offset += sizeof(adc_digi_output_data_t)) {
      adc_digi_output_data_t sample;
      std::memcpy(&sample, buffer + offset, sizeof(sample));

      if (sample.type2.unit != 0 ||
          sample.type2.channel >= SOC_ADC_CHANNEL_NUM(0)) {
        invalid_samples++;
        continue;
      }

      latest_raw = static_cast<uint16_t>(sample.type2.data);
      latest_average = add_sample(average, latest_raw);
      samples_since_print++;

      if (samples_since_print >= PRINT_EVERY_SAMPLES) {
        ESP_LOGI(TAG, "RAW: %u; SMA[%u]: %u; INVALID: %" PRIu32,
                 latest_raw, static_cast<unsigned>(average.count),
                 latest_average, invalid_samples);
        samples_since_print = 0;
        invalid_samples = 0;
      }
    }

    pending_bytes = total_bytes - complete_bytes;
    if (pending_bytes > 0) {
      std::memmove(buffer, buffer + complete_bytes, pending_bytes);
    }
  }
}

