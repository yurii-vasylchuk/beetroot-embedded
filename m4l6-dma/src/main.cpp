
#include "driver/gpio.h"
#include "driver/uart.h"
#include "driver/uhci.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_continuous.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

static const char *TAG = "M4L6";
static constexpr uint32_t RESTART_DELAY = 1000;

// ADC

static constexpr gpio_num_t ANALOG_INPUT_GPIO = GPIO_NUM_4;
static constexpr adc_atten_t ADC_ATTEN = ADC_ATTEN_DB_12;
static constexpr adc_bitwidth_t ADC_BITWIDTH = ADC_BITWIDTH_12;

static adc_channel_t adc_channel;
static adc_unit_t adc_unit;
static adc_continuous_handle_t adc_handle;
static adc_cali_handle_t adc_calibration;

// Buffers
static constexpr size_t BUFFERS_COUNT = 2;
static constexpr size_t BUFFER_SIZE = 256;
static QueueHandle_t free_buffers;

// UART
static constexpr uart_port_t UART_PORT = UART_NUM_1;
static constexpr gpio_num_t UART_TX_GPIO = GPIO_NUM_17;
static constexpr int UART_BAUD_RATE = 921600;

static uhci_controller_handle_t uhci_handle;

static void init_adc();
static void init_uart();
static void init_buffers();
static void init_tasks();

static void adc_to_uart_task(void *);
static bool uart_tx_done_callback(uhci_controller_handle_t,
                                  const uhci_tx_done_event_data_t *, void *);

extern "C" void app_main() {
  init_buffers();
  init_uart();
  init_adc();
  init_tasks();
}

void init_adc() {
  esp_err_t err = ESP_OK;
  adc_continuous_handle_cfg_t handle_cnf = {
      .max_store_buf_size = 4096,
      .conv_frame_size = BUFFER_SIZE,
  };

  err = adc_continuous_new_handle(&handle_cnf, &adc_handle);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Unable to configure continious ADC, ERR: %s",
             esp_err_to_name(err));
    vTaskDelay(pdMS_TO_TICKS(RESTART_DELAY));
    esp_restart();
  }

  err =
      adc_continuous_io_to_channel(ANALOG_INPUT_GPIO, &adc_unit, &adc_channel);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Unable to identify continious ADC unit/channel, ERR: %s",
             esp_err_to_name(err));
    vTaskDelay(pdMS_TO_TICKS(RESTART_DELAY));
    esp_restart();
  }

  adc_digi_pattern_config_t pattern_config = {
      .atten = ADC_ATTEN,
      .channel = static_cast<uint8_t>(adc_channel),
      .unit = static_cast<uint8_t>(adc_unit),
      .bit_width = ADC_BITWIDTH,
  };

  adc_continuous_config_t config = {
      .pattern_num = 1,
      .adc_pattern = &pattern_config,
      .sample_freq_hz = 20000,
      .conv_mode = ADC_CONV_SINGLE_UNIT_1,
  };

  err = adc_continuous_config(adc_handle, &config);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Unable to configure continuous adc, ERR: %s",
             esp_err_to_name(err));
    vTaskDelay(pdMS_TO_TICKS(RESTART_DELAY));
    esp_restart();
  }

  adc_cali_curve_fitting_config_t cali_config = {
      .unit_id = adc_unit,
      .chan = adc_channel,
      .atten = ADC_ATTEN,
      .bitwidth = ADC_BITWIDTH,
  };

  err = adc_cali_create_scheme_curve_fitting(&cali_config, &adc_calibration);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Unable to configure adc calibration, ERR: %s",
             esp_err_to_name(err));
    vTaskDelay(pdMS_TO_TICKS(RESTART_DELAY));
    esp_restart();
  }

  err = adc_continuous_start(adc_handle);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Unable to start continuous ADC, ERR: %s",
             esp_err_to_name(err));
    vTaskDelay(pdMS_TO_TICKS(RESTART_DELAY));
    esp_restart();
  }
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

  err = uart_set_pin(UART_PORT, UART_TX_GPIO, UART_PIN_NO_CHANGE,
                     UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Unable to configure UART pins, ERR: %s",
             esp_err_to_name(err));
    vTaskDelay(pdMS_TO_TICKS(RESTART_DELAY));
    esp_restart();
  }

  uhci_controller_config_t uhci_config = {
      .uart_port = UART_PORT,
      .tx_trans_queue_depth = BUFFERS_COUNT,
      .max_transmit_size = BUFFER_SIZE,
      .max_receive_internal_mem = BUFFER_SIZE,
      .dma_burst_size = 32,
      .max_packet_receive = 0,
      .rx_eof_flags = {},
  };

  err = uhci_new_controller(&uhci_config, &uhci_handle);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Unable to initialize UART DMA controller, ERR: %s",
             esp_err_to_name(err));
    vTaskDelay(pdMS_TO_TICKS(RESTART_DELAY));
    esp_restart();
  }

  uhci_event_callbacks_t callbacks = {
      .on_rx_trans_event = nullptr,
      .on_tx_trans_done = uart_tx_done_callback,
  };

  err = uhci_register_event_callbacks(uhci_handle, &callbacks, free_buffers);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Unable to register UART DMA callbacks, ERR: %s",
             esp_err_to_name(err));
    vTaskDelay(pdMS_TO_TICKS(RESTART_DELAY));
    esp_restart();
  }
}

void init_buffers() {
  free_buffers = xQueueCreate(BUFFERS_COUNT, sizeof(uint8_t *));
  if (free_buffers == nullptr) {
    ESP_LOGE(TAG, "Unbale to initialize free_buffers queue");
    vTaskDelay(pdMS_TO_TICKS(RESTART_DELAY));
    esp_restart();
  }

  for (size_t i = 0; i < BUFFERS_COUNT; i++) {
    uint8_t *buffer = static_cast<uint8_t *>(
        heap_caps_malloc(BUFFER_SIZE, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
    if (buffer == NULL) {
      ESP_LOGE(TAG, "Unable to allocate buffer");
      vTaskDelay(pdMS_TO_TICKS(RESTART_DELAY));
      esp_restart();
    }
    BaseType_t res = xQueueSend(free_buffers, &buffer, 0);

    if (res != pdTRUE) {
      ESP_LOGE(TAG, "Unable to fill free_buffers queue");
      vTaskDelay(pdMS_TO_TICKS(RESTART_DELAY));
      esp_restart();
    }
  }
}

void init_tasks() {
  BaseType_t result =
      xTaskCreate(adc_to_uart_task, "adc_to_uart", 4096, nullptr, 5, nullptr);

  if (result != pdPASS) {
    ESP_LOGE(TAG, "Unable to start 'adc_to_uart_task'");
    vTaskDelay(pdMS_TO_TICKS(RESTART_DELAY));
    esp_restart();
  }
}

void adc_to_uart_task(void *arg) {
  uint8_t *buffer;

  while (true) {
    if (xQueueReceive(free_buffers, &buffer, portMAX_DELAY) != pdTRUE) {
      ESP_LOGE(TAG, "Unable to obtain free buffer: consider to speedup UART");
      continue;
    }

    uint32_t bytes_read = 0;

    esp_err_t err = adc_continuous_read(adc_handle, buffer, BUFFER_SIZE,
                                        &bytes_read, ADC_MAX_DELAY);
    if (err != ESP_OK) {
      ESP_LOGW(TAG, "Unable to read data from ADC, ERR: %s",
               esp_err_to_name(err));
      xQueueSend(free_buffers, &buffer, portMAX_DELAY);
      continue;
    }

    if (bytes_read == 0) {
      xQueueSend(free_buffers, &buffer, portMAX_DELAY);
      continue;
    }

    err = uhci_transmit(uhci_handle, buffer, bytes_read);
    if (err != ESP_OK) {
      ESP_LOGW(TAG, "Unable to send data to UART, ERR: %s",
               esp_err_to_name(err));
      xQueueSend(free_buffers, &buffer, portMAX_DELAY);
      continue;
    }
  }
}

bool uart_tx_done_callback(uhci_controller_handle_t controller,
                           const uhci_tx_done_event_data_t *event,
                           void *user_data) {
  BaseType_t task_woken = pdFALSE;

  auto queue = static_cast<QueueHandle_t>(user_data);
  uint8_t *buffer = event->buffer;

  xQueueSendFromISR(queue, &buffer, &task_woken);

  return task_woken == pdTRUE;
}
