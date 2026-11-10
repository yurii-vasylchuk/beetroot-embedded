
#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include <yva_button_fsm.h>

namespace btn = yva_button_fsm;

enum class LedState {
  ON,
  OFF,
};

static const char *TAG = "M4L1";
static constexpr gpio_num_t BTN_GPIO = GPIO_NUM_4;

static constexpr uart_port_t UART_PORT = UART_NUM_1;
static constexpr gpio_num_t UART_TX_PIN = GPIO_NUM_17;
static constexpr gpio_num_t UART_RX_PIN = GPIO_NUM_18;
static constexpr int UART_BUFFER_SIZE = 1024;
static constexpr int UART_BAUD_RATE = 115200;

static constexpr gpio_num_t LED_GPIO = GPIO_NUM_5;

static const char *COMMAND_ON = "ON";
static const char *COMMAND_OFF = "OFF";

void rx_task(void *);
void tx_task(void *);
void handle_press();

TaskHandle_t tx_task_handle = nullptr;

extern "C" void app_main() {
  btn::init_module({});
  btn::init({.gpio = BTN_GPIO,
             .active_level = btn::active_level_t::HIGH,
             .pull_enabled = true,
             .press_callback = handle_press,
             .longpress_callback = nullptr});

  gpio_config_t gpio_configuration = {
      .pin_bit_mask = BIT64(LED_GPIO),
      .mode = GPIO_MODE_OUTPUT,
  };

  gpio_config(&gpio_configuration);

  uart_config_t config = {
      .baud_rate = UART_BAUD_RATE,
      .data_bits = UART_DATA_8_BITS,
      .parity = UART_PARITY_DISABLE,
      .stop_bits = UART_STOP_BITS_1,
      .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
      .rx_flow_ctrl_thresh = 0,
      .source_clk = UART_SCLK_DEFAULT,
  };

  ESP_ERROR_CHECK(uart_param_config(UART_PORT, &config));

  ESP_ERROR_CHECK(uart_set_pin(UART_PORT, UART_TX_PIN, UART_RX_PIN,
                               UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));

  ESP_ERROR_CHECK(uart_driver_install(UART_PORT, UART_BUFFER_SIZE,
                                      UART_BUFFER_SIZE, 0, nullptr, 0));

  xTaskCreate(rx_task, "uart_rx", 4096, nullptr, 5, nullptr);
  xTaskCreate(tx_task, "uart_tx", 4096, nullptr, 5, &tx_task_handle);
}

void rx_task(void *arg) {
  uint8_t commands[64];
  uint8_t end = 0;

  uint8_t rx[64];
  char cmd[10];

  while (true) {
    int received =
        uart_read_bytes(UART_PORT, rx, sizeof(rx), pdMS_TO_TICKS(1000));
    if (received <= 0) {
      continue;
    }
    if (received + end >= sizeof(commands)) {
      ESP_LOGE(TAG, "UART RX Buffer overflow");
      uart_driver_delete(UART_PORT);
      esp_restart();
    }

    memcpy(commands + end, rx, received);
    end += received;

    uint8_t *found = static_cast<uint8_t *>(memchr(commands, '\n', end));

    if (found == nullptr) {
      continue;
    }

    uint8_t cmd_end = found - commands;
    ESP_LOGI(TAG, "Command end found with offset = %d", cmd_end);

    memcpy(cmd, commands, cmd_end);
    cmd[cmd_end] = '\0';

    char *command_str = cmd;

    if (strcmp(command_str, COMMAND_ON) == 0) {
      ESP_LOGI(TAG, "LED_ON Command received");
      gpio_set_level(LED_GPIO, 1);
    } else if (strcmp(command_str, COMMAND_OFF) == 0) {
      ESP_LOGI(TAG, "LED_OFF Command received");
      gpio_set_level(LED_GPIO, 0);
    } else {
      ESP_LOGW(TAG, "Unknown command received: %s", command_str);
    }

    memmove(commands, commands + cmd_end + 1, end - cmd_end - 1);
    end -= cmd_end + 1;
  }
}

void tx_task(void *arg) {
  bool state = false;
  while (true) {
    if (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(500)) == 0) {
      continue;
    }

    const char *command = state ? COMMAND_OFF : COMMAND_ON;
    ESP_LOGI(TAG, "Sending command: %s", command);
    uart_write_bytes(UART_PORT, command, strlen(command));
    uart_write_bytes(UART_PORT, "\n", 1);
    state = !state;
  }
}

void handle_press() { xTaskNotifyGive(tx_task_handle); }
