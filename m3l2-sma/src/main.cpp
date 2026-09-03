
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include <atomic>
#include <cmath>

static const char *TAG = "M3L2";

static constexpr gpio_num_t ILLUMINANCE_INPUT_GPIO = GPIO_NUM_4;
static constexpr gpio_num_t LED_GPIO = GPIO_NUM_15;

static uint16_t read_illuminance();
static void read_illuminance_task(void *);
static adc_oneshot_unit_handle_t adc_handle;
static adc_cali_handle_t calibration_handle;
static adc_unit_t adc_unit;
static adc_channel_t adc_channel;

static constexpr uint16_t illuminance_treshold = 1800;
static constexpr float illuminance_hysteresis_rate = 0.07f;
static void led_control_task(void *arg);

static void init_adc();
static void init_tasks();
static void init_gpio();

static std::atomic<uint16_t> illuminance{0};
static constexpr float alpha = 0.15f;

extern "C" void app_main() {
  init_gpio();
  init_adc();
  init_tasks();
}

void init_adc() {
  ESP_ERROR_CHECK(adc_oneshot_io_to_channel(ILLUMINANCE_INPUT_GPIO, &adc_unit,
                                            &adc_channel));
  adc_oneshot_unit_init_cfg_t unit_cfg = {.unit_id = adc_unit,
                                          .ulp_mode = ADC_ULP_MODE_DISABLE};

  ESP_ERROR_CHECK(adc_oneshot_new_unit(&unit_cfg, &adc_handle));

  adc_oneshot_chan_cfg_t chanCfg = {
      .atten = ADC_ATTEN_DB_12,
      .bitwidth = ADC_BITWIDTH_DEFAULT,
  };

  ESP_ERROR_CHECK(
      adc_oneshot_config_channel(adc_handle, adc_channel, &chanCfg));

  adc_cali_curve_fitting_config_t calibration_config = {
      .unit_id = adc_unit,
      .chan = adc_channel,
      .atten = ADC_ATTEN_DB_12,
      .bitwidth = ADC_BITWIDTH_DEFAULT};

  ESP_ERROR_CHECK(adc_cali_create_scheme_curve_fitting(&calibration_config,
                                                       &calibration_handle));
}

void init_tasks() {
  BaseType_t err;

  err = xTaskCreate(read_illuminance_task, "read_illuminance", 4096, nullptr, 5,
                    nullptr);
  if (err != pdPASS) {
    ESP_LOGE(TAG, "Unable to start 'read_illuminance_task'");
  }

  err = xTaskCreate(led_control_task, "led_control_task", 4096, nullptr, 5,
                    nullptr);
  if (err != pdPASS) {
    ESP_LOGE(TAG, "Unable to start 'led_control_task'");
  }
}

void init_gpio() {
  gpio_config_t config = {
      .pin_bit_mask = BIT64(LED_GPIO),
      .mode = GPIO_MODE_OUTPUT,
  };

  ESP_ERROR_CHECK(gpio_config(&config));
}

void read_illuminance_task(void *arg) {
  float internal_illuminance;

  internal_illuminance = static_cast<float>(read_illuminance());

  while (true) {
    internal_illuminance =
        (alpha * read_illuminance() + (1.0f - alpha) * internal_illuminance);
    illuminance.store(
        static_cast<uint16_t>(std::lroundf(internal_illuminance)));

    vTaskDelay(pdMS_TO_TICKS(100));
  }
}

uint16_t read_illuminance() {
  int value;
  int calibrated;

  adc_oneshot_read(adc_handle, adc_channel, &value);
  adc_cali_raw_to_voltage(calibration_handle, value, &calibrated);
  return static_cast<uint16_t>(calibrated);
}

void led_control_task(void *arg) {
  bool isEnabled = false;
  uint16_t upper_threshold =
      static_cast<uint16_t>(illuminance_treshold +
                            illuminance_treshold * illuminance_hysteresis_rate);
  uint16_t lower_threshold =
      static_cast<uint16_t>(illuminance_treshold -
                            illuminance_treshold * illuminance_hysteresis_rate);
  gpio_set_level(LED_GPIO, 0);

  ESP_LOGI(TAG, "lower_threshold: %d; upper_threshold: %d", lower_threshold,
           upper_threshold);

  vTaskDelay(pdMS_TO_TICKS(100));

  while (true) {
    uint16_t local_illuminance = illuminance.load();
    ESP_LOGI(TAG, "Current illuminance = %d", local_illuminance);
    if (isEnabled && local_illuminance > upper_threshold) {
      gpio_set_level(LED_GPIO, 0);
      isEnabled = false;
    } else if (!isEnabled && illuminance < lower_threshold) {
      gpio_set_level(LED_GPIO, 1);
      isEnabled = true;
    }

    vTaskDelay(pdMS_TO_TICKS(100));
  }
}
