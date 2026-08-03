#include "driver/gpio.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"

static constexpr gpio_num_t CONTROL_GPIO = GPIO_NUM_15;
static constexpr gpio_num_t CHECK_GPIO = GPIO_NUM_16;

static constexpr uint32_t BLINK_DELAY = 1000;

static SemaphoreHandle_t relay_switch_semaphore = nullptr;
static const char *TAG = "M2L2";

static void IRAM_ATTR register_relay_switch_isr_handler(void *arg) {
  BaseType_t higher_priority_task_woken = pdFALSE;

  xSemaphoreGiveFromISR(relay_switch_semaphore, &higher_priority_task_woken);

  portYIELD_FROM_ISR(higher_priority_task_woken);
}

static void relay_switch_registrar_task(void *arg) {
  uint32_t time;

  while (true) {
    if (xSemaphoreTake(relay_switch_semaphore, portMAX_DELAY) != pdTRUE) {
      continue;
    }

    time = esp_timer_get_time() / 1000;

    ESP_LOGI(TAG, "Registered relay switch at %dms", time);
  }
}

static void control_blinker_task(void *arg) {
  uint32_t time = 0;
  while (true) {
    gpio_set_level(CONTROL_GPIO, 1);
    time = esp_timer_get_time() / 1000;
    ESP_LOGI(TAG, "Control pin set to HIGH at %dms", time);
    vTaskDelay(pdMS_TO_TICKS(BLINK_DELAY));

    gpio_set_level(CONTROL_GPIO, 0);
    time = esp_timer_get_time() / 1000;
    ESP_LOGI(TAG, "Control pin set to LOW at %dms", time);
    vTaskDelay(pdMS_TO_TICKS(BLINK_DELAY));
  }
}

extern "C" void app_main() {
  gpio_config_t control_gpio_config = {
      .pin_bit_mask = BIT64(CONTROL_GPIO),
      .mode = GPIO_MODE_OUTPUT,
  };

  ESP_ERROR_CHECK(gpio_config(&control_gpio_config));

  gpio_config_t check_gpio_config = {
      .pin_bit_mask = BIT64(CHECK_GPIO),
      .mode = GPIO_MODE_INPUT,
      .pull_down_en = GPIO_PULLDOWN_ENABLE,
      .intr_type = GPIO_INTR_POSEDGE,
  };

  ESP_ERROR_CHECK(gpio_config(&check_gpio_config));

  relay_switch_semaphore = xSemaphoreCreateBinary();

  gpio_install_isr_service(0);
  gpio_isr_handler_add(CHECK_GPIO, register_relay_switch_isr_handler, nullptr);

  gpio_intr_enable(CHECK_GPIO);

  xTaskCreate(relay_switch_registrar_task, "relay-switch-registrar", 4096,
              nullptr, 5, nullptr);
  xTaskCreate(control_blinker_task, "control-blinker", 4096, nullptr, 5,
              nullptr);
}
