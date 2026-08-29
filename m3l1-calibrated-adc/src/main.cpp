#include "driver/gpio.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "M3L1";

static constexpr gpio_num_t INPUT_GPIO = GPIO_NUM_4;

static void init_adc();
static void init_tasks();

static void read_input_task(void *);
static adc_oneshot_unit_handle_t adc_handle;
static adc_cali_handle_t calibrationHandle;
static adc_unit_t adcUnit;
static adc_channel_t adcChannel;

extern "C" void app_main() {
  init_adc();
  init_tasks();
}

void init_adc() {
  ESP_ERROR_CHECK(adc_oneshot_io_to_channel(INPUT_GPIO, &adcUnit, &adcChannel));
  adc_oneshot_unit_init_cfg_t unit_cfg = {.unit_id = adcUnit,
                                          .ulp_mode = ADC_ULP_MODE_DISABLE};

  ESP_ERROR_CHECK(adc_oneshot_new_unit(&unit_cfg, &adc_handle));

  adc_oneshot_chan_cfg_t chanCfg = {
      .atten = ADC_ATTEN_DB_12,
      .bitwidth = ADC_BITWIDTH_DEFAULT,
  };

  ESP_ERROR_CHECK(adc_oneshot_config_channel(adc_handle, adcChannel, &chanCfg));

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

void init_tasks() {
  BaseType_t err =
      xTaskCreate(read_input_task, "read_input", 4096, nullptr, 5, nullptr);
  if (err != pdPASS) {
    ESP_LOGE(TAG, "Unable to start 'read_input_task'");
  }
}

void read_input_task(void *arg) {
  int value;
  int calculated;
  int calibrated;
  int error;

  while (true) {
    UBaseType_t uxHighWaterMark = uxTaskGetStackHighWaterMark(NULL);
    ESP_LOGI(TAG, "STACK HIGH WATER MARK: %" PRIu32, (uint32_t)uxHighWaterMark);

    adc_oneshot_read(adc_handle, adcChannel, &value);
    calculated = (value * 3300) / 4095;
    adc_cali_raw_to_voltage(calibrationHandle, value, &calibrated);

    error = 100 - ((calculated * 100) / calibrated);

    ESP_LOGI(TAG, "RAW: %d\tCALC: %d\tCALI: %d\t ERROR: %d%%", value,
             calculated, calibrated, error);

    vTaskDelay(pdMS_TO_TICKS(100));
  }
}
