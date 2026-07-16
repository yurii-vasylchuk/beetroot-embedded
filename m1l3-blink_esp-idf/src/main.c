#include "driver/gpio.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#define LED_R_GPIO GPIO_NUM_15
#define LED_B_GPIO GPIO_NUM_16

void app_main(void) {
  gpio_reset_pin(LED_R_GPIO);
  gpio_reset_pin(LED_B_GPIO);
  gpio_set_direction(LED_R_GPIO, GPIO_MODE_OUTPUT);
  gpio_set_direction(LED_B_GPIO, GPIO_MODE_OUTPUT);

  while (1) {
    gpio_set_level(LED_R_GPIO, 1);
    gpio_set_level(LED_B_GPIO, 0);
    vTaskDelay(500 / portTICK_PERIOD_MS);

    gpio_set_level(LED_R_GPIO, 0);
    gpio_set_level(LED_B_GPIO, 1);
    vTaskDelay(500 / portTICK_PERIOD_MS);
  }
}
