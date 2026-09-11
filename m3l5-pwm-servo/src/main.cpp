
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "yva_adc.hpp"

static const char *TAG = "M4L5";

static constexpr gpio_num_t POTENTIOMETR_GPIO = GPIO_NUM_4;
static constexpr gpio_num_t SERVO_GPIO = GPIO_NUM_5;

static constexpr ledc_timer_bit_t LEDC_RESOLUTION = LEDC_TIMER_12_BIT;
static constexpr uint16_t LEDC_MAX_VALUE = (1 << LEDC_RESOLUTION) - 1;
static constexpr ledc_mode_t LEDC_MODE = LEDC_LOW_SPEED_MODE;
static constexpr ledc_channel_t SERVO_CHANNEL = LEDC_CHANNEL_0;
static constexpr uint16_t SERVO_MAX_DUTY = LEDC_MAX_VALUE / 8;
static constexpr uint16_t SERVO_MIN_DUTY = LEDC_MAX_VALUE / 40;

static bool init_pwm();
static bool init_rtos();
static bool init_adc();

static void potentiometer_change_handler(float value);

static QueueHandle_t servo_angle_changes;
static void servo_control_task(void *);

extern "C" void app_main() {
  if (!init_pwm()) {
    ESP_LOGE(TAG, "Unable to init PWM");
    return;
  }

  if (!init_rtos()) {
    ESP_LOGE(TAG, "Unable to init RTOS");
    return;
  }

  if (!init_adc()) {
    ESP_LOGE(TAG, "Unable to init ADC module");
    return;
  }
}

bool init_adc() {
  yva_adc::module_config_t adc_module_config = {};
  yva_adc::init_module(adc_module_config);

  yva_adc::config_t unit_config = {
      .gpio = POTENTIOMETR_GPIO,
      .on_change_enabled = true,
      .on_change = potentiometer_change_handler,
      .max_calibrated_value = 3173,
  };
  yva_adc::init_unit(unit_config);

  return true;
}

bool init_rtos() {
  servo_angle_changes = xQueueCreate(5, sizeof(float));

  if (servo_angle_changes == nullptr) {
    ESP_LOGE(TAG, "Unable to init queue for angle changes");
    return false;
  }

  if (xTaskCreate(servo_control_task, "Servo control", 4096, nullptr, 5,
                  nullptr) != pdPASS) {
    ESP_LOGE(TAG, "Unable to start task 'servo_control_task'");
    return false;
  };

  return true;
}

void potentiometer_change_handler(float value) {
  if (xQueueSend(servo_angle_changes, &value, 1) != pdTRUE) {
    ESP_LOGW(TAG, "Unable to handle angle change (%f)", value);
  }
}

void servo_control_task(void *arg) {
  float angle;
  uint16_t duty;
  esp_err_t err;

  err = ledc_set_duty(LEDC_MODE, SERVO_CHANNEL, SERVO_MIN_DUTY);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Unable to set initial servo duty");
  }
  err = ledc_update_duty(LEDC_MODE, SERVO_CHANNEL);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Unable to update initial servo duty");
  }

  while (true) {
    if (xQueueReceive(servo_angle_changes, &angle, portMAX_DELAY) != pdTRUE) {
      continue;
    }

    if (angle < 0.0F || angle > 1.0F) {
      ESP_LOGW(TAG, "Invalid servo angle %f, should be between 0.0 and 1.0",
               angle);
      continue;
    }

    duty = SERVO_MIN_DUTY +
           static_cast<uint16_t>((SERVO_MAX_DUTY - SERVO_MIN_DUTY) * angle);

    ESP_LOGI(TAG, "Got angle = %f; calculated duty = %d/%d = %d(us)", angle,
             duty, LEDC_MAX_VALUE,
             ((1'000'000 * duty) / (50 * LEDC_MAX_VALUE)));

    err = ledc_set_duty(LEDC_MODE, SERVO_CHANNEL, duty);
    if (err != ESP_OK) {
      ESP_LOGE(TAG, "Unable to set servo duty");
    }
    err = ledc_update_duty(LEDC_MODE, SERVO_CHANNEL);
    if (err != ESP_OK) {
      ESP_LOGE(TAG, "Unable to update servo duty");
    }
  }
}

bool init_pwm() {
  esp_err_t err;
  ledc_timer_config_t timer_config = {.speed_mode = LEDC_MODE,
                                      .duty_resolution = LEDC_RESOLUTION,
                                      .timer_num = LEDC_TIMER_0,
                                      .freq_hz = 50,
                                      .clk_cfg = LEDC_AUTO_CLK};

  err = ledc_timer_config(&timer_config);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Unable to init LEDC Timer, err: '%s'", esp_err_to_name(err));
    return false;
  }

  ledc_channel_config_t channel_config = {.gpio_num = SERVO_GPIO,
                                          .speed_mode = LEDC_MODE,
                                          .channel = SERVO_CHANNEL,
                                          .intr_type = LEDC_INTR_DISABLE,
                                          .timer_sel = LEDC_TIMER_0,
                                          .duty = 0,
                                          .hpoint = 0};

  err = ledc_channel_config(&channel_config);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Unable to init LEDC Channel, err: '%s'",
             esp_err_to_name(err));
    return false;
  }

  return true;
}
