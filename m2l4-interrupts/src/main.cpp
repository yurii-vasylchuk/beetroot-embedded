#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

static constexpr gpio_num_t BTN_GPIO = GPIO_NUM_16;
static constexpr uint32_t BTN_DEBOUNCE_TIME = 30000;

static const char *TAG = "M2L4";

void btn_click_registrar_task(void *arg);

void init_gpio() {
  gpio_config_t config = {
      .pin_bit_mask = BIT64(BTN_GPIO),
      .mode = GPIO_MODE_INPUT,
      .pull_up_en = GPIO_PULLUP_DISABLE,
      .pull_down_en = GPIO_PULLDOWN_DISABLE,
  };

  ESP_ERROR_CHECK(gpio_config(&config));
}

void init_tasks() {
  xTaskCreate(btn_click_registrar_task, "btn-click-registrar", 4096, nullptr, 5,
              nullptr);
}

extern "C" void app_main() {
  init_gpio();

  init_tasks();
}

void btn_click_registrar_task(void *arg) {
  int64_t time = -1;
  int32_t count = 0;
  int64_t now;
  bool clicked = false;

  while (true) {
    vTaskDelay(1);

    if (gpio_get_level(BTN_GPIO) != 0) {
      time = -1;
      clicked = false;
      continue;
    }

    if (clicked) {
      continue;
    }

    now = esp_timer_get_time();

    if (time < 0) {
      time = now;
      continue;
    } else if (now - time >= BTN_DEBOUNCE_TIME) {
      ESP_LOGI(TAG, "Click registrerd at %" PRId64 "ms; total count %" PRIu32,
               (time / 1000), ++count);

      time = -1;
      clicked = true;
    }
  }
}
