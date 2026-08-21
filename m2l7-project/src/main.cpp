
#include "button_fsm.hpp"
#include "driver/gpio.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "traffic_light_fsm.hpp"

static const char *TAG = "M2L7";

static constexpr gpio_num_t RED_RGB_GPIO = GPIO_NUM_15;
static constexpr gpio_num_t YLW_RGB_GPIO = GPIO_NUM_16;
static constexpr gpio_num_t GRN_RGB_GPIO = GPIO_NUM_17;

static constexpr gpio_num_t BTN_GPIO = GPIO_NUM_4;
static constexpr gpio_num_t ALLOW_PERIOD_MODIFIER_GPIO = GPIO_NUM_5;

static constexpr uint64_t READ_PERIOD_MODIFIERS_DELAY = 1000;
static constexpr uint32_t TRAFFIC_LIGHT_PERIOD_MIN = 10000;
static constexpr uint32_t TRAFFIC_LIGHT_PERIOD_DEFAULT = 30000;
static constexpr uint32_t TRAFFIC_LIGHT_PERIOD_MAX = 60000;

static void init_gpio();
static void init_tasks();
static void init_adc_calibration();
static void init_adc();
static void init_timers();

TaskHandle_t btn_task_handle;
void btn_isr_handler(void *arg);
void btn_task(void *arg);

btn_fsm::handle_t *btn_handle;
void press_handler();
void log(const btn_fsm::log_level_t level, const char *format, ...);
uint64_t now();

static adc_oneshot_unit_handle_t adc_handle;
static adc_cali_handle_t calibrationHandle;
static adc_unit_t adcUnit;
static adc_channel_t adcChannel;
static esp_timer_handle_t read_period_modifiers_timer;
static void read_period_modifiers_alarm(void *arg);
static void read_period_modifiers_task(void *arg);
static TaskHandle_t read_period_modifiers_task_handle;

traffic_light_fsm::handle_t *traffic_light_handle;

extern "C" void app_main() {
  btn_fsm::config_t config = {
      .log = log,
      .press_callback = press_handler,
      .longpress_callback = nullptr,
      .now = now,
  };

  init_gpio();
  btn_handle = btn_fsm::create(config);
  traffic_light_handle = traffic_light_fsm::create(
      TRAFFIC_LIGHT_PERIOD_DEFAULT, TRAFFIC_LIGHT_PERIOD_DEFAULT, RED_RGB_GPIO,
      YLW_RGB_GPIO, GRN_RGB_GPIO);

  init_tasks();

  gpio_isr_handler_add(BTN_GPIO, btn_isr_handler, nullptr);
  gpio_intr_enable(BTN_GPIO);

  init_adc();
  init_timers();
}

void init_tasks() {
  xTaskCreate(btn_task, "BTN TASK", 4096, nullptr, 5, &btn_task_handle);
  xTaskCreate(read_period_modifiers_task, "Read period modif.", 4096, nullptr,
              5, &read_period_modifiers_task_handle);
}

void init_gpio() {
  gpio_config_t config = {
      .pin_bit_mask =
          BIT64(RED_RGB_GPIO) | BIT64(YLW_RGB_GPIO) | BIT64(GRN_RGB_GPIO),
      .mode = GPIO_MODE_OUTPUT,
  };

  gpio_config(&config);

  config = {
      .pin_bit_mask = BIT64(BTN_GPIO),
      .mode = GPIO_MODE_INPUT,
      .pull_up_en = GPIO_PULLUP_ENABLE,
      .pull_down_en = GPIO_PULLDOWN_DISABLE,
      .intr_type = GPIO_INTR_ANYEDGE,
  };

  gpio_config(&config);

  gpio_install_isr_service(0);
}

void press_handler() { traffic_light_fsm::switch_mode(traffic_light_handle); }

void IRAM_ATTR btn_isr_handler(void *arg) {
  int level = gpio_get_level(BTN_GPIO);
  BaseType_t woken = pdFALSE;

  xTaskNotifyFromISR(btn_task_handle, static_cast<uint32_t>(level),
                     eSetValueWithOverwrite, &woken);

  portYIELD_FROM_ISR(woken);
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
  asprintf(&log_format, "%c (%" PRId64 ") BTN_FSM: %s\n", level_name,
           esp_timer_get_time() / 1000, format);

  va_list args;
  va_start(args, format);

  esp_log_writev(esp_level, TAG, log_format, args);

  free(log_format);

  va_end(args);
}

std::uint64_t now() { return static_cast<std::uint64_t>(esp_timer_get_time()); }

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

static void init_adc_calibration(void) {
  adc_cali_curve_fitting_config_t calibration_config = {
      .unit_id = adcUnit,
      .chan = adcChannel,
      .atten = ADC_ATTEN_DB_12,
      .bitwidth = ADC_BITWIDTH_DEFAULT};

  esp_err_t err = adc_cali_create_scheme_curve_fitting(&calibration_config,
                                                       &calibrationHandle);

  if (err != ESP_OK) {
    ESP_LOGW(TAG, "ADC calibration not enabled: %s", esp_err_to_name(err));
  }
}

static void init_adc(void) {
  ESP_ERROR_CHECK(adc_oneshot_io_to_channel(ALLOW_PERIOD_MODIFIER_GPIO,
                                            &adcUnit, &adcChannel));
  adc_oneshot_unit_init_cfg_t unit_cfg = {.unit_id = adcUnit,
                                          .ulp_mode = ADC_ULP_MODE_DISABLE};

  ESP_ERROR_CHECK(adc_oneshot_new_unit(&unit_cfg, &adc_handle));

  adc_oneshot_chan_cfg_t chanCfg = {
      .atten = ADC_ATTEN_DB_12,
      .bitwidth = ADC_BITWIDTH_DEFAULT,
  };

  ESP_ERROR_CHECK(adc_oneshot_config_channel(adc_handle, adcChannel, &chanCfg));

  init_adc_calibration();
}

static void read_period_modifiers_alarm(void *arg) {
  xTaskNotifyGive(read_period_modifiers_task_handle);
}

static void read_period_modifiers_task(void *arg) {
  while (true) {
    if (ulTaskNotifyTake(pdFALSE, portMAX_DELAY) == 0) {
      ESP_LOGI(TAG, "Timed out when waiting to read period modifiers");
      continue;
    }

    int value;
    adc_oneshot_read(adc_handle, adcChannel, &value);
    int calibrated;
    adc_cali_raw_to_voltage(calibrationHandle, value, &calibrated);
    uint32_t period = static_cast<uint32_t>(
        TRAFFIC_LIGHT_PERIOD_MIN +
        (((TRAFFIC_LIGHT_PERIOD_MAX - TRAFFIC_LIGHT_PERIOD_MIN) * calibrated) /
         3300));
    ESP_LOGI(TAG, "Read period modification value %d = %dms", calibrated,
             period);
    traffic_light_fsm::set_allow_period_ms(traffic_light_handle, period);
    traffic_light_fsm::set_deny_period_ms(traffic_light_handle, period);
  }
}

static void init_timers() {
  const esp_timer_create_args_t read_period_modifiers_timer_conf = {
      .callback = read_period_modifiers_alarm,
      .arg = nullptr,
      .dispatch_method = ESP_TIMER_TASK,
      .name = "Read periods modif. timer",
      .skip_unhandled_events = false,
  };

  ESP_ERROR_CHECK(esp_timer_create(&read_period_modifiers_timer_conf,
                                   &read_period_modifiers_timer));

  esp_timer_start_periodic(read_period_modifiers_timer,
                           READ_PERIOD_MODIFIERS_DELAY * 1000);
}
