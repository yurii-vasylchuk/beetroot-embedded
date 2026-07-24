#include "driver/gptimer.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "soc/gpio_num.h"

#define ANALOG_GPIO GPIO_NUM_17

#define TIMER_RESOLUTION_HZ 1000000
#define TIMER_PERIOD_US 100000
#define TIMER_QUEUE_LENGTH 10

static const char *TAG = "M1L6";

static QueueHandle_t alarmQueueHandle = NULL;
static gptimer_handle_t timer;
static adc_oneshot_unit_handle_t adc_handle;
static adc_cali_handle_t calibrationHandle;

adc_unit_t adcUnit;
adc_channel_t adcChannel;

static void adc_calibration_init(void) {
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

static void adc_init(void) {
  adc_oneshot_unit_init_cfg_t unit_cfg = {.unit_id = adcUnit,
                                          .ulp_mode = ADC_ULP_MODE_DISABLE};

  ESP_ERROR_CHECK(adc_oneshot_new_unit(&unit_cfg, &adc_handle));

  adc_oneshot_chan_cfg_t chanCfg = {
      .bitwidth = ADC_BITWIDTH_DEFAULT,
      .atten = ADC_ATTEN_DB_12,
  };

  ESP_ERROR_CHECK(adc_oneshot_config_channel(adc_handle, adcChannel, &chanCfg));

  adc_calibration_init();
}

static bool IRAM_ATTR timerAlarmHandler(gptimer_handle_t timer,
                                        const gptimer_alarm_event_data_t *alarm,
                                        void *ctx) {

  uint32_t time = (uint32_t)(esp_timer_get_time() / 1000);

  xQueueSendFromISR(alarmQueueHandle, &time, NULL);
  return true;
}

static void logAnalogValueTask(void *arg) {
  uint32_t time;
  while (true) {
    if (xQueueReceive(alarmQueueHandle, &time, portMAX_DELAY)) {
      int value;
      adc_oneshot_read(adc_handle, adcChannel, &value);

      int calculated = (value * 3300) / 4095;

      int calibrated;
      adc_cali_raw_to_voltage(calibrationHandle, value, &calibrated);

      int error = 100 - ((calculated * 100) / calibrated);
      ESP_LOGI(TAG,
               "Read value RAW: %d, calculated: %d, calibrated: %d. Error = "
               "%d%% at %dms",
               value, calculated, calibrated, error, time);
    }
  }
}

void app_main() {

  ESP_ERROR_CHECK(adc_oneshot_io_to_channel(GPIO_NUM_4, &adcUnit, &adcChannel));
  adc_init();

  alarmQueueHandle = xQueueCreate(TIMER_QUEUE_LENGTH, sizeof(uint32_t));

  xTaskCreate(logAnalogValueTask, "log_analog_value", 2048, NULL, 10, NULL);

  gptimer_handle_t timer = NULL;

  const gptimer_config_t timerConfig = {.clk_src = GPTIMER_CLK_SRC_DEFAULT,
                                        .direction = GPTIMER_COUNT_UP,
                                        .resolution_hz = TIMER_RESOLUTION_HZ};
  ESP_ERROR_CHECK(gptimer_new_timer(&timerConfig, &timer));

  const gptimer_event_callbacks_t callbacks = {.on_alarm = timerAlarmHandler};
  ESP_ERROR_CHECK(gptimer_register_event_callbacks(timer, &callbacks, NULL));

  const gptimer_alarm_config_t alarmConfig = {.alarm_count = TIMER_PERIOD_US,
                                              .reload_count = 0,
                                              .flags.auto_reload_on_alarm =
                                                  true};
  ESP_ERROR_CHECK(gptimer_set_alarm_action(timer, &alarmConfig));

  ESP_ERROR_CHECK(gptimer_enable(timer));
  ESP_ERROR_CHECK(gptimer_start(timer));

  ESP_LOGI(TAG, "Timer started");
}
