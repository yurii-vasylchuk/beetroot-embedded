#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"

static const char *TAG = "M2L3";

static constexpr gpio_num_t RED_GPIO = GPIO_NUM_15;
static constexpr gpio_num_t YLW_GPIO = GPIO_NUM_16;
static constexpr gpio_num_t GRN_GPIO = GPIO_NUM_17;

static constexpr uint32_t RED_DELAY_MS = 200;
static constexpr uint32_t YLW_DELAY_MS = 500;
static constexpr uint32_t GRN_DELAY_MS = 1000;

struct State {
  uint8_t red : 1;
  uint8_t ylw : 1;
  uint8_t grn : 1;
};

void gpio_init() {
  gpio_config_t config = {
      .pin_bit_mask = BIT64(RED_GPIO) | BIT64(YLW_GPIO) | BIT64(GRN_GPIO),
      .mode = GPIO_MODE_OUTPUT,
  };

  ESP_ERROR_CHECK(gpio_config(&config));
}

extern "C" void app_main() {
  gpio_init();

  uint32_t time = esp_timer_get_time() / 1000;

  uint32_t red_last_change = time;
  uint32_t ylw_last_change = time;
  uint32_t grn_last_change = time;

  State state = {
      .red = 0,
      .ylw = 0,
      .grn = 0,
  };

  ESP_LOGI(TAG, "Size of state is %d bytes", sizeof(State));

  while (true) {
    time = esp_timer_get_time() / 1000;

    if (time - red_last_change >= RED_DELAY_MS) {
      ESP_LOGI(TAG, "Switching RED state; current = %d", state.red);
      red_last_change += RED_DELAY_MS;
      state.red = state.red == 1 ? 0 : 1;
      gpio_set_level(RED_GPIO, state.red);
    }

    if (time - ylw_last_change >= YLW_DELAY_MS) {
      ESP_LOGI(TAG, "Switching YLW state; current = %d", state.ylw);
      ylw_last_change += YLW_DELAY_MS;
      state.ylw = state.ylw == 1 ? 0 : 1;
      gpio_set_level(YLW_GPIO, state.ylw);
    }

    if (time - grn_last_change >= GRN_DELAY_MS) {
      ESP_LOGI(TAG, "Switching GRN state; current = %d", state.grn);
      grn_last_change += GRN_DELAY_MS;
      state.grn = state.grn == 1 ? 0 : 1;
      gpio_set_level(GRN_GPIO, state.grn);
    }

    vTaskDelay(1);
  }
}
