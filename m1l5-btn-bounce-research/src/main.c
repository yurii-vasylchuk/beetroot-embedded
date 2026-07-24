#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#define BTN_GPIO GPIO_NUM_17

static const char *TAG = "M1L5";

static QueueHandle_t btnPressQueueHandle = NULL;

static void IRAM_ATTR btn_isr_handler(void *arg) {
  uint32_t time = (uint32_t)(esp_timer_get_time() / 1000);
  xQueueSendFromISR(btnPressQueueHandle, &time, NULL);
}

static void logBtnPressTask(void *arg) {
  uint32_t time;
  uint32_t prevTime = 0;
  while (true) {
    if (xQueueReceive(btnPressQueueHandle, &time, portMAX_DELAY)) {
      ESP_LOGI(TAG, "Button pressed +%dms (%dms)", time - prevTime, time);
      prevTime = time;
    }
  }
}

void app_main() {
  gpio_set_direction(BTN_GPIO, GPIO_MODE_INPUT);
  gpio_set_pull_mode(BTN_GPIO, GPIO_PULLUP_ONLY);
  gpio_set_intr_type(BTN_GPIO, GPIO_INTR_NEGEDGE);

  btnPressQueueHandle = xQueueCreate(5, sizeof(uint32_t));

  xTaskCreate(logBtnPressTask, "log_btn_press", 2048, NULL, 10, NULL);

  gpio_install_isr_service(0);
  gpio_isr_handler_add(BTN_GPIO, btn_isr_handler, NULL);
}
