
#include "driver/gpio.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include <algorithm>

static constexpr gpio_num_t ANALOG_GPIO = GPIO_NUM_16;
static constexpr gpio_num_t MOTOR_GPIO = GPIO_NUM_15;
static constexpr uint32_t ANALOG_READ_DELAY = pdMS_TO_TICKS(50);

static const char *TAG = "M2L2M";

static adc_oneshot_unit_handle_t adc_handle;
static adc_cali_handle_t calibration_handle;
adc_unit_t adc_unit;
adc_channel_t adc_channel;

volatile float duty_cycle = 0;
static constexpr uint8_t pwm_full_cycle = 5;

static void init_adc(void) {
  ESP_ERROR_CHECK(
      adc_oneshot_io_to_channel(ANALOG_GPIO, &adc_unit, &adc_channel));

  adc_oneshot_unit_init_cfg_t unit_cfg = {.unit_id = adc_unit,
                                          .ulp_mode = ADC_ULP_MODE_DISABLE};

  ESP_ERROR_CHECK(adc_oneshot_new_unit(&unit_cfg, &adc_handle));

  adc_oneshot_chan_cfg_t chan_cfg = {
      .atten = ADC_ATTEN_DB_12,
      .bitwidth = ADC_BITWIDTH_DEFAULT,
  };

  ESP_ERROR_CHECK(
      adc_oneshot_config_channel(adc_handle, adc_channel, &chan_cfg));
  adc_cali_curve_fitting_config_t calibration_config = {
      .unit_id = adc_unit,
      .chan = adc_channel,
      .atten = ADC_ATTEN_DB_12,
      .bitwidth = ADC_BITWIDTH_DEFAULT};

  esp_err_t err = adc_cali_create_scheme_curve_fitting(&calibration_config,
                                                       &calibration_handle);

  if (err != ESP_OK) {
    ESP_LOGW(TAG, "ADC calibration not enabled: %s", esp_err_to_name(err));
  }
}

void init_gpio() {
  gpio_config_t config = {
      .pin_bit_mask = BIT64(MOTOR_GPIO),
      .mode = GPIO_MODE_OUTPUT,
  };

  gpio_config(&config);
}

void motor_control_task(void *arg) {
  uint32_t high_delay;
  uint32_t low_delay;
  uint8_t iteration = 0;
  while (true) {
    high_delay = pwm_full_cycle * duty_cycle;
    low_delay = pwm_full_cycle - high_delay;

    if (high_delay > 0) {
      gpio_set_level(MOTOR_GPIO, 1);
      vTaskDelay(high_delay);
    }

    if (low_delay > 0) {
      gpio_set_level(MOTOR_GPIO, 0);
      vTaskDelay(low_delay);
    }

    if (iteration >= 20) {
      iteration = 0;
      ESP_LOGI(TAG, "Duty cycle = %.4f; %d ticks/%d ticks", duty_cycle,
               high_delay, low_delay);
    } else {
      iteration++;
    }
  }
}

void read_analog_task(void *arg) {
  int value;
  int calibrated;

  while (true) {
    ESP_ERROR_CHECK(adc_oneshot_read(adc_handle, adc_channel, &value));
    ESP_ERROR_CHECK(
        adc_cali_raw_to_voltage(calibration_handle, value, &calibrated));

    duty_cycle = std::clamp(calibrated / 3300.f, 0.0f, 1.0f);
    vTaskDelay(ANALOG_READ_DELAY);
  }
}

extern "C" void app_main() {
  init_gpio();
  init_adc();

  xTaskCreate(read_analog_task, "read", 4096, nullptr, 5, nullptr);
  xTaskCreate(motor_control_task, "motor", 4096, nullptr, 5, nullptr);
}
