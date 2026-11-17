#include "data_logger.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "M4L8_MAIN";

extern "C" void app_main() {
  vTaskDelay(pdMS_TO_TICKS(1'000));

  const esp_err_t err = data_logger::start();
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Unable to start data logger: %s", esp_err_to_name(err));
    vTaskDelay(pdMS_TO_TICKS(1'000));
    esp_restart();
  }
}
