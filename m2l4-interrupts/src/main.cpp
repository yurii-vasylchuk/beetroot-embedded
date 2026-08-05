#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

static constexpr gpio_num_t BTN_GPIO = GPIO_NUM_16;

static const char *TAG = "M2L4";

static QueueHandle_t btn_click_queue;

void register_btn_click_isr(void *arg);
void btn_click_registrar_task(void *arg);

void init_gpio() {
  gpio_config_t config = {
      .pin_bit_mask = BIT64(BTN_GPIO),
      .mode = GPIO_MODE_INPUT,
      .pull_up_en = GPIO_PULLUP_DISABLE,
      .pull_down_en = GPIO_PULLDOWN_DISABLE,
      .intr_type = GPIO_INTR_NEGEDGE,
  };

  ESP_ERROR_CHECK(gpio_config(&config));

  gpio_install_isr_service(0);

  gpio_isr_handler_add(BTN_GPIO, register_btn_click_isr, nullptr);
}

void init_tasks() {
  btn_click_queue = xQueueCreate(5, sizeof(int64_t));
  xTaskCreate(btn_click_registrar_task, "btn-click-registrar", 4096, nullptr, 5,
              nullptr);
}

extern "C" void app_main() {
  init_gpio();

  init_tasks();

  gpio_intr_enable(BTN_GPIO);
}

void IRAM_ATTR register_btn_click_isr(void *arg) {
  int64_t time = esp_timer_get_time();

  BaseType_t higher_priority_task_woken = pdFALSE;

  xQueueSendFromISR(btn_click_queue, &time, &higher_priority_task_woken);

  portYIELD_FROM_ISR(higher_priority_task_woken);
}

void btn_click_registrar_task(void *arg) {
  int64_t time;
  int32_t count = 0;

  while (true) {
    if (xQueueReceive(btn_click_queue, &time, portMAX_DELAY) != pdTRUE) {
      continue;
    }

    ESP_LOGI(TAG, "Click registrerd at %" PRId64 "ms; total count %" PRIu32,
             (time / 1000), ++count);
  }
}
