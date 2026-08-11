
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "M2L5";

static constexpr gpio_num_t MONITOR_GPIO = GPIO_NUM_47;
static constexpr gpio_num_t CONTROL_GPIO = GPIO_NUM_15;

static constexpr uint8_t MOTOR_WORK_TIME_SECONDS = 2;
static constexpr uint8_t MOTOR_CYCLE_TIME_SECONDS = 4;
// If through 3 cycles coudn't get a change from monitoring gpio - something
// goes wrong
static constexpr uint8_t WATCHDOG_TIME_SECONDS = MOTOR_CYCLE_TIME_SECONDS * 3;

static constexpr uint32_t S_TO_US_MULTIPLIER = 1000000;

static esp_timer_handle_t cycle_timer;
static esp_timer_handle_t work_timer;
static esp_timer_handle_t watchdog_timer;

static TaskHandle_t monitor_task_handle;
static TaskHandle_t motor_task_handle;
static TaskHandle_t watchdog_task_handle;

void monitor_isr_handler(void *arg);

void motor_alarm_handler(void *arg);
void cycle_alarm_handler(void *arg);
void watchdog_alarm_handler(void *arg);

void monitor_task(void *arg);
void motor_run_task(void *arg);
void watchdog_task(void *arg);

void init_gpio();
void init_timers();
void init_tasks();

extern "C" void app_main() {
  init_tasks();
  init_gpio();
  init_timers();
}

void init_tasks() {
  xTaskCreate(monitor_task, "monitor", 4096, nullptr, 5, &monitor_task_handle);

  xTaskCreate(motor_run_task, "Motor run", 4096, nullptr, 5,
              &motor_task_handle);

  xTaskCreate(watchdog_task, "Watchdog task", 4096, nullptr, 10,
              &watchdog_task_handle);
}

void init_gpio() {
  gpio_config_t out_pin_config = {
      .pin_bit_mask = BIT64(CONTROL_GPIO),
      .mode = GPIO_MODE_OUTPUT,
  };

  gpio_config_t in_pin_config = {
      .pin_bit_mask = BIT64(MONITOR_GPIO),
      .mode = GPIO_MODE_INPUT,
      .pull_down_en = GPIO_PULLDOWN_ENABLE,
      .intr_type = GPIO_INTR_POSEDGE,
  };

  gpio_config(&out_pin_config);
  gpio_config(&in_pin_config);

  gpio_install_isr_service(0);

  gpio_isr_handler_add(MONITOR_GPIO, monitor_isr_handler, nullptr);
}

void init_timers() {
  const esp_timer_create_args_t cycle_timer_conf = {
      .callback = cycle_alarm_handler,
      .arg = nullptr,
      .dispatch_method = ESP_TIMER_TASK,
      .name = "Cycle timer",
      .skip_unhandled_events = false,
  };

  ESP_ERROR_CHECK(esp_timer_create(&cycle_timer_conf, &cycle_timer));

  const esp_timer_create_args_t motor_timer_conf = {
      .callback = motor_alarm_handler,
      .arg = nullptr,
      .dispatch_method = ESP_TIMER_TASK,
      .name = "Motor timer",
      .skip_unhandled_events = false,
  };

  ESP_ERROR_CHECK(esp_timer_create(&motor_timer_conf, &work_timer));

  const esp_timer_create_args_t watchdog_timer_conf = {
      .callback = watchdog_alarm_handler,
      .arg = nullptr,
      .dispatch_method = ESP_TIMER_TASK,
      .name = "Watchdog timer",
      .skip_unhandled_events = true,
  };

  ESP_ERROR_CHECK(esp_timer_create(&watchdog_timer_conf, &watchdog_timer));

  esp_timer_start_once(watchdog_timer,
                       WATCHDOG_TIME_SECONDS * S_TO_US_MULTIPLIER);

  esp_timer_start_periodic(cycle_timer,
                           MOTOR_CYCLE_TIME_SECONDS * S_TO_US_MULTIPLIER);
}

void watchdog_alarm_handler(void *arg) {
  xTaskNotifyGive(watchdog_task_handle);
}

void cycle_alarm_handler(void *arg) {
  xTaskNotify(motor_task_handle, 1, eSetValueWithOverwrite);

  esp_timer_start_once(work_timer,
                       MOTOR_WORK_TIME_SECONDS * S_TO_US_MULTIPLIER);
}

void motor_alarm_handler(void *arg) {
  xTaskNotify(motor_task_handle, 0, eSetValueWithOverwrite);
}

void motor_run_task(void *arg) {
  uint32_t value;
  uint32_t prev_value = 0;

  while (true) {
    xTaskNotifyWait(0, 0, &value, portMAX_DELAY);

    if (value == prev_value) {
      ESP_LOGW(TAG,
               "Caught repeated motor command: motor gpio already has value %d",
               value);
      continue;
    }
    prev_value = value;

    if (value == 1) {
      ESP_LOGI(TAG, "Start motor");
      gpio_set_level(CONTROL_GPIO, 1);
    } else if (value == 0) {
      ESP_LOGI(TAG, "Stop motor");
      gpio_set_level(CONTROL_GPIO, 0);
    } else {
      ESP_LOGW(TAG, "Unknown task notification value %d", value);
    }
  }
}

void monitor_task(void *arg) {
  int prev_value = 0;
  while (true) {
    if (ulTaskNotifyTake(pdTRUE, portMAX_DELAY) != pdTRUE) {
      continue;
    }

    vTaskDelay(pdMS_TO_TICKS(30));
    int value = gpio_get_level(MONITOR_GPIO);

    if (value == prev_value) {
      continue;
    }

    xTaskNotifyStateClear(nullptr);
    prev_value = value;
    esp_timer_stop(watchdog_timer);
    esp_timer_start_once(watchdog_timer,
                         WATCHDOG_TIME_SECONDS * S_TO_US_MULTIPLIER);
    ESP_LOGI(TAG, "MONITORING LEVEL CHANGED TO %d", value);
  }
}

void watchdog_task(void *arg) {
  while (true) {
    if (ulTaskNotifyTake(pdTRUE, portMAX_DELAY) == 0)
      continue;

    ESP_LOGE(TAG, "WATCHDOG FIRED, Rebooting...");
    esp_restart();
  }
}

void monitor_isr_handler(void *arg) { xTaskNotifyGive(monitor_task_handle); }
