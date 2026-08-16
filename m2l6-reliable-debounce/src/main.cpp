#include "button_fsm.hpp"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <string>

static const char *TAG = "M2L6";
static constexpr gpio_num_t BTN_GPIO = GPIO_NUM_16;
static constexpr gpio_num_t LED_GPIO = GPIO_NUM_15;

TaskHandle_t led_handler = nullptr;
TaskHandle_t btn_task_handle = nullptr;

void btn_isr_handler(void *arg);
void btn_task(void *arg);

void log(const btn_fsm::log_level_t level, const char *format, ...);
std::uint64_t now();

void press_handler();
void longpress_handler();

btn_fsm::handle_t *btn_handle;

void init_gpio();
void init_tasks();

extern "C" void app_main() {
  ESP_LOGI(TAG, "Initialization started");
  btn_fsm::config_t config = {
      .log = log,
      .press_callback = press_handler,
      .longpress_callback = longpress_handler,
      .now = now,
  };

  btn_handle = btn_fsm::create(config);

  init_tasks();
  init_gpio();
}

void init_tasks() {
  xTaskCreate(btn_task, "BTN TASK", 4096, nullptr, 5, &btn_task_handle);
}

void init_gpio() {
  gpio_config_t config = {
      .pin_bit_mask = BIT64(BTN_GPIO),
      .mode = GPIO_MODE_INPUT,
      .pull_up_en = GPIO_PULLUP_ENABLE,
      .pull_down_en = GPIO_PULLDOWN_DISABLE,
      .intr_type = GPIO_INTR_ANYEDGE,
  };

  ESP_ERROR_CHECK(gpio_config(&config));

  gpio_install_isr_service(0);

  gpio_isr_handler_add(BTN_GPIO, btn_isr_handler, nullptr);
}

void log(btn_fsm::log_level_t level, const char *format, ...) {
  esp_log_level_t esp_level;
  char level_name;

  switch (level) {
  case btn_fsm::BTN_FSM_DEBUG:
    esp_level = ESP_LOG_DEBUG;
    level_name = 'D';
    break;
  case btn_fsm::BTN_FSM_INFO:
    esp_level = ESP_LOG_INFO;
    level_name = 'I';
    break;
  case btn_fsm::BTN_FSM_WARN:
    esp_level = ESP_LOG_WARN;
    level_name = 'W';
    break;
  case btn_fsm::BTN_FSM_ERROR:
    esp_level = ESP_LOG_ERROR;
    level_name = 'E';
    break;
  default:
    esp_level = ESP_LOG_INFO;
    level_name = 'I';
    break;
  }

  char *log_format;
  asprintf(&log_format, "%C (%" PRId64 ") BTN_FSM: %s\n", level_name,
           esp_timer_get_time() / 1000, format);

  va_list args;
  va_start(args, format);

  esp_log_writev(esp_level, TAG, log_format, args);

  va_end(args);
}

std::uint64_t now() { return static_cast<std::uint64_t>(esp_timer_get_time()); }

void press_handler() {
  ESP_LOGI(TAG, "\n\n--- Simple press detected at %" PRId64 " ---\n\n",
           esp_timer_get_time());
}

void longpress_handler() {
  ESP_LOGI(TAG, "\n\n--- Long press detected at %" PRId64 " ---\n\n",
           esp_timer_get_time());
}

void IRAM_ATTR btn_isr_handler(void *arg) {
  int level = gpio_get_level(BTN_GPIO);
  BaseType_t woken = pdFALSE;

  xTaskNotifyFromISR(btn_task_handle, static_cast<uint32_t>(level),
                     eSetValueWithOverwrite, &woken);

  portYIELD_FROM_ISR(woken);
}

void btn_task(void *arg) {
  uint32_t value = 0;

  while (true) {
    if (xTaskNotifyWait(0, UINT32_MAX, &value, 2) == pdFALSE) {
      btn_fsm::tick(btn_handle);
      continue;
    }

    if (btn_handle == nullptr) {
      ESP_LOGW(TAG, "BTN FSM HANDLE IS NULL");
      continue;
    }

    if (value == 0) {
      btn_fsm::register_btn_down(btn_handle);
    } else {
      btn_fsm::register_btn_up(btn_handle);
    }
  }
}
