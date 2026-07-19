#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"

#define LED_1 GPIO_NUM_15
#define LED_2 GPIO_NUM_16
#define BTN_1 GPIO_NUM_17
#define BTN_2 GPIO_NUM_0

static const char *TAG = "M1L4";

typedef enum {
  LED_MODE_FAST_BLINK,
  LED_MODE_SLOW_BLINK,
  LED_MODE_SOLID,
  LED_MODE_UNDEF,
} LedMode;

LedMode identifyMode();
bool readBtnDebounced(gpio_num_t pin);
const char *ledModeToChar(LedMode mode) {
  switch (mode) {
  case LED_MODE_SLOW_BLINK:
    return "LED_MODE_SLOW_BLINK";
  case LED_MODE_FAST_BLINK:
    return "LED_MODE_FAST_BLINK";
  case LED_MODE_SOLID:
    return "LED_MODE_SOLID";
  case LED_MODE_UNDEF:
    return "LED_MODE_UNDEF";
  }

  ESP_LOGW(TAG, "UNKNOWN LedMode: %d", mode);
  return "UNKNOWN";
}

static LedMode ledMode = LED_MODE_SLOW_BLINK;

void app_main() {
  gpio_reset_pin(LED_1);
  gpio_reset_pin(LED_2);
  gpio_reset_pin(BTN_1);
  gpio_reset_pin(BTN_2);

  gpio_set_direction(LED_1, GPIO_MODE_OUTPUT);
  gpio_set_direction(LED_2, GPIO_MODE_OUTPUT);
  gpio_set_direction(BTN_1, GPIO_MODE_INPUT);
  gpio_set_direction(BTN_2, GPIO_MODE_INPUT);
  gpio_set_pull_mode(BTN_1, GPIO_PULLUP_ONLY);
  gpio_set_pull_mode(BTN_2, GPIO_PULLUP_ONLY);

  while (true) {
    LedMode mode = identifyMode();
    if (mode != LED_MODE_UNDEF) {
      ESP_LOGI(TAG, "Mode set to %s", ledModeToChar(mode));
      ledMode = mode;
    }

    uint32_t start = esp_timer_get_time();
    while (esp_timer_get_time() < start + (300000)) {
      switch (ledMode) {
      case LED_MODE_FAST_BLINK:
        gpio_set_level(LED_1, 1);
        gpio_set_level(LED_2, 0);
        vTaskDelay(pdMS_TO_TICKS(50));
        gpio_set_level(LED_1, 0);
        gpio_set_level(LED_2, 1);
        vTaskDelay(pdMS_TO_TICKS(50));
        gpio_set_level(LED_1, 0);
        gpio_set_level(LED_2, 0);
        break;
      case LED_MODE_SLOW_BLINK:
        gpio_set_level(LED_1, 1);
        gpio_set_level(LED_2, 0);
        vTaskDelay(pdMS_TO_TICKS(150));
        gpio_set_level(LED_1, 0);
        gpio_set_level(LED_2, 1);
        vTaskDelay(pdMS_TO_TICKS(150));
        gpio_set_level(LED_1, 0);
        gpio_set_level(LED_2, 0);
        break;
      case LED_MODE_SOLID:
        gpio_set_level(LED_1, 1);
        gpio_set_level(LED_2, 1);
        vTaskDelay(pdMS_TO_TICKS(150));
        break;
      case LED_MODE_UNDEF:
        ESP_LOGW(TAG, "LED_MODE_UNDEF is unexpected here");
      }
    }

    ESP_LOGI(TAG, "End Frame");
  }
}

LedMode identifyMode() {
  bool btn1 = readBtnDebounced(BTN_1);
  bool btn2 = readBtnDebounced(BTN_2);

  if (!btn1 && !btn2) {
    return LED_MODE_UNDEF;
  }

  if (btn1 && btn2) {
    return LED_MODE_SOLID;
  }

  if (btn1) {
    return LED_MODE_FAST_BLINK;
  }

  return LED_MODE_SLOW_BLINK;
}

bool readBtnDebounced(gpio_num_t pin) {

  if (gpio_get_level(pin) == 0) {
    vTaskDelay(pdMS_TO_TICKS(50));

    return gpio_get_level(pin) == 0;
  }

  return false;
}
