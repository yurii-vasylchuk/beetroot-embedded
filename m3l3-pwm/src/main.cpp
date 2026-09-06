
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include <algorithm>
#include <atomic>
#include <cmath>

static const char *TAG = "M3L3";

enum class AdjustmentTarget {
  MOTOR_SPEED,
  LED_BRIGHTNESS,
};

static constexpr gpio_num_t MOTOR_CONTROL_GPIO = GPIO_NUM_4;
static constexpr gpio_num_t LED_CONTROL_GPIO = GPIO_NUM_15;
static constexpr gpio_num_t ADJUSTMENT_KNOB_GPIO = GPIO_NUM_5;
static constexpr gpio_num_t SWITCH_MODE_BTN_GPIO = GPIO_NUM_16;

static constexpr uint16_t ADC_CALI_MAX_VALUE = 3300;
static constexpr ledc_timer_bit_t LEDC_RESOLUTION = LEDC_TIMER_12_BIT;
static constexpr uint16_t LEDC_MAX_VALUE = (1 << LEDC_RESOLUTION) - 1;
static constexpr ledc_mode_t LEDC_MODE = LEDC_LOW_SPEED_MODE;
static constexpr ledc_channel_t MOTOR_CHANNEL = LEDC_CHANNEL_0;
static constexpr ledc_channel_t LED_CHANNEL = LEDC_CHANNEL_1;
static constexpr double MOTOR_CHANGE_SPEED_THRESHOLD = 0.01;
static constexpr double LED_CHANGE_BRIGHTNESS_THRESHOLD = 0.01;

static double read_adjustment();
static void read_adjustment_task(void *);
static adc_oneshot_unit_handle_t adc_handle;
static adc_cali_handle_t calibration_handle;
static adc_unit_t adc_unit;
static adc_channel_t adc_channel;

static void motor_control_task(void *arg);
static void led_control_task(void *arg);

static void switch_mode_isr_handler(void *);
static TaskHandle_t switch_mode_task_handle;
static void switch_mode_task(void *);

static void init_adc();
static void init_tasks();
static void init_gpio();
static void init_ledc();

static std::atomic<double> speed{0.0};
static std::atomic<double> brightness{0.0};
static std::atomic<AdjustmentTarget> mode{AdjustmentTarget::MOTOR_SPEED};
static constexpr float alpha = 0.1f;

extern "C" void app_main() {
  init_gpio();
  init_adc();
  init_ledc();
  init_tasks();
}

void init_gpio() {
  gpio_config_t config = {
      .pin_bit_mask = BIT64(SWITCH_MODE_BTN_GPIO),
      .mode = GPIO_MODE_INPUT,
      .pull_up_en = GPIO_PULLUP_ENABLE,
      .pull_down_en = GPIO_PULLDOWN_DISABLE,
      .intr_type = GPIO_INTR_NEGEDGE,
  };

  ESP_ERROR_CHECK(gpio_config(&config));

  ESP_ERROR_CHECK(gpio_install_isr_service(0));
  ESP_ERROR_CHECK(gpio_isr_handler_add(SWITCH_MODE_BTN_GPIO,
                                       switch_mode_isr_handler, nullptr));
}

void init_ledc() {
  ledc_timer_config_t timer_config = {.speed_mode = LEDC_MODE,
                                      .duty_resolution = LEDC_RESOLUTION,
                                      .timer_num = LEDC_TIMER_0,
                                      .freq_hz = 10000,
                                      .clk_cfg = LEDC_AUTO_CLK};

  ESP_ERROR_CHECK(ledc_timer_config(&timer_config));

  ledc_channel_config_t channel_config = {.gpio_num = MOTOR_CONTROL_GPIO,
                                          .speed_mode = LEDC_MODE,
                                          .channel = MOTOR_CHANNEL,
                                          .intr_type = LEDC_INTR_DISABLE,
                                          .timer_sel = LEDC_TIMER_0,
                                          .duty = 0,
                                          .hpoint = 0};

  ESP_ERROR_CHECK(ledc_channel_config(&channel_config));

  channel_config.gpio_num = LED_CONTROL_GPIO;
  channel_config.channel = LED_CHANNEL;
  ESP_ERROR_CHECK(ledc_channel_config(&channel_config));
}

void init_adc() {
  ESP_ERROR_CHECK(
      adc_oneshot_io_to_channel(ADJUSTMENT_KNOB_GPIO, &adc_unit, &adc_channel));
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

  err = xTaskCreate(read_adjustment_task, "read_adjustment", 4096, nullptr, 5,
                    nullptr);
  if (err != pdPASS) {
    ESP_LOGE(TAG, "Unable to start 'read_adjustment_task'");
  }

  err = xTaskCreate(led_control_task, "led_control_task", 4096, nullptr, 5,
                    nullptr);
  if (err != pdPASS) {
    ESP_LOGE(TAG, "Unable to start 'led_control_task'");
  }

  err = xTaskCreate(motor_control_task, "motor_control_task", 4096, nullptr, 5,
                    nullptr);
  if (err != pdPASS) {
    ESP_LOGE(TAG, "Unable to start 'motor_control_task'");
  }

  err = xTaskCreate(switch_mode_task, "switch_mode_task", 4096, nullptr, 5,
                    &switch_mode_task_handle);
  if (err != pdPASS) {
    ESP_LOGE(TAG, "Unable to start 'switch_mode_task'");
  }
}

void read_adjustment_task(void *arg) {
  double internal_speed;
  double internal_brightness;
  double adjustment;

  internal_speed = 0.0;
  internal_brightness = 0.0;

  while (true) {
    adjustment = read_adjustment();
    ESP_LOGI(TAG,
             "Read adjustment; Speed: %.3f; Brightness: %.3f; Adjustment: %.3f",
             internal_speed, internal_brightness, adjustment);
    if (adjustment < 0.0) {
      vTaskDelay(pdMS_TO_TICKS(50));
      continue;
    }

    if (mode.load() == AdjustmentTarget::MOTOR_SPEED) {

      internal_speed = alpha * adjustment + (1.0f - alpha) * internal_speed;
      speed.store(internal_speed);
    } else {
      internal_brightness =
          alpha * adjustment + (1.0f - alpha) * internal_brightness;
      brightness.store(internal_brightness);
    }

    vTaskDelay(pdMS_TO_TICKS(50));
  }
}

double read_adjustment() {
  int value;
  int calibrated;
  esp_err_t err;

  err = adc_oneshot_read(adc_handle, adc_channel, &value);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "Unable to read raw adjustment, err: %s",
             esp_err_to_name(err));
    return -1.0;
  }

  err = adc_cali_raw_to_voltage(calibration_handle, value, &calibrated);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "Unable to read raw adjustment, err: %s",
             esp_err_to_name(err));
    return -1.0;
  }

  double result = std::clamp((double)calibrated / ADC_CALI_MAX_VALUE, 0.0, 1.0);
  ESP_LOGI(TAG, "value: %d; calibrated: %d", value, calibrated, result);

  return result;
}

void switch_mode_isr_handler(void *arg) {
  if (switch_mode_task_handle != nullptr) {
    BaseType_t woken = pdFALSE;

    vTaskNotifyGiveFromISR(switch_mode_task_handle, &woken);

    portYIELD_FROM_ISR(woken);
  }
}

void switch_mode_task(void *arg) {
  while (true) {
    if (ulTaskNotifyTake(pdTRUE, portMAX_DELAY) == 0) {
      continue;
    }
    vTaskDelay(pdMS_TO_TICKS(50));
    ulTaskNotifyTake(pdTRUE, 0);
    if (gpio_get_level(SWITCH_MODE_BTN_GPIO) != 0) {
      continue;
    }

    mode.store(mode.load() == AdjustmentTarget::MOTOR_SPEED
                   ? AdjustmentTarget::LED_BRIGHTNESS
                   : AdjustmentTarget::MOTOR_SPEED);
  }
}

void motor_control_task(void *arg) {
  double prev = 0.0;
  esp_err_t err = ESP_OK;

  while (true) {
    double current = speed.load();
    if (std::abs(prev - current) > MOTOR_CHANGE_SPEED_THRESHOLD) {
      // TODO: change by 3% at max per step;
      err = ledc_set_duty(LEDC_MODE, MOTOR_CHANNEL,
                          std::round(LEDC_MAX_VALUE * current));
      if (err == ESP_OK) {
        err = ledc_update_duty(LEDC_MODE, MOTOR_CHANNEL);
      }

      if (err != ESP_OK) {
        ESP_LOGW(TAG, "Unable to update motor speed, err: %s",
                 esp_err_to_name(err));
      } else {
        prev = current;
      }
    }

    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

void led_control_task(void *arg) {
  double prev = 0.0;
  esp_err_t err = ESP_OK;

  while (true) {
    double current = brightness.load();
    if (std::abs(prev - current) > LED_CHANGE_BRIGHTNESS_THRESHOLD) {
      // TODO: change by 3% at max per step;
      err = ledc_set_duty(LEDC_MODE, LED_CHANNEL,
                          std::round(LEDC_MAX_VALUE * current));
      if (err == ESP_OK) {
        err = ledc_update_duty(LEDC_MODE, LED_CHANNEL);
      }

      if (err != ESP_OK) {
        ESP_LOGW(TAG, "Unable to update led brightness, err: %s",
                 esp_err_to_name(err));
      } else {
        prev = current;
      }
    }
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}
