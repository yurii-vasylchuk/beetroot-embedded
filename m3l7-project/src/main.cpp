
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "driver/pulse_cnt.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "notes.h"
#include "safe_fsm.h"
#include "sounds.h"
#include <yva_button_fsm.h>

namespace btn = yva_button_fsm;

static const char *TAG = "M3L7";

static constexpr gpio_num_t ENC_BTN_GPIO = GPIO_NUM_15;
static constexpr gpio_num_t ENC_A_GPIO = GPIO_NUM_16;
static constexpr gpio_num_t ENC_B_GPIO = GPIO_NUM_17;

static constexpr ledc_timer_bit_t LEDC_RESOLUTION = LEDC_TIMER_12_BIT;
static constexpr uint16_t LEDC_MAX_VALUE = (1 << LEDC_RESOLUTION) - 1;
static constexpr ledc_mode_t LEDC_MODE = LEDC_LOW_SPEED_MODE;

static constexpr gpio_num_t BUZZER_GPIO = GPIO_NUM_4;
static constexpr ledc_channel_t BUZZER_SERVO_CHANNEL = LEDC_CHANNEL_1;
static constexpr uint16_t BUZZER_DUTY = LEDC_MAX_VALUE / 2;
static constexpr ledc_timer_t BUZZER_TIMER = LEDC_TIMER_1;

static constexpr gpio_num_t LOCK_CTRL_GPIO = GPIO_NUM_5;
static constexpr ledc_channel_t LOCK_SERVO_CHANNEL = LEDC_CHANNEL_0;
static constexpr uint16_t LOCKED_SERVO_DUTY = LEDC_MAX_VALUE / 8;
static constexpr uint16_t UNLOCKED_SERVO_DUTY = LEDC_MAX_VALUE / 40;
static constexpr ledc_timer_t LOCK_TIMER = LEDC_TIMER_0;

static QueueHandle_t encodder_events = nullptr;
static QueueHandle_t play_sounds_tasks_queue = nullptr;
static safe_fsm::handle_t *safe_handle;

void handle_press();
void handle_longpress();
bool handle_enc_changed(pcnt_unit_handle_t, const pcnt_watch_event_data_t *,
                        void *);
void enc_events_handler_task(void *);
void handle_safe_event(safe_fsm::event_t);

bool init_encodder();
bool init_lock();
bool init_buzzer();

void unlock_safe();
void lock_safe();
void play_note(Note);
void play_sound_task(void *);

extern "C" void app_main() {

  if (!init_encodder()) {
    return;
  }

  if (!init_lock()) {
    return;
  }

  if (!init_buzzer()) {
    return;
  }

  safe_handle = safe_fsm::init({
      .on_event = handle_safe_event,
      .password = {0, 0, 0, 0, 0, 0, 0, 0},
      .attempts = 3,
  });

  xTaskCreate(enc_events_handler_task, "enc_event_handler", 4096, nullptr, 5,
              nullptr);
  xTaskCreate(play_sound_task, "play_sound", 4096, nullptr, 5, nullptr);
}

bool init_encodder() {
  encodder_events = xQueueCreate(5, sizeof(pcnt_watch_event_data_t));
  if (encodder_events == nullptr) {
    ESP_LOGE(TAG,
             "Unable to init encodder: failed to create encodder events queue");
    return false;
  }

  esp_err_t err;

  btn::init_module({});
  btn::init({
      .gpio = ENC_BTN_GPIO,
      .active_level = btn::active_level_t::HIGH,
      .pull_enabled = true,
      .press_callback = handle_press,
      .longpress_callback = handle_longpress,
  });

  pcnt_unit_handle_t unit_handle = nullptr;
  pcnt_channel_handle_t channel_a_handle = nullptr;
  pcnt_channel_handle_t channel_b_handle = nullptr;
  pcnt_unit_config_t unit_config = {
      .low_limit = -4,
      .high_limit = 4,
  };

  err = pcnt_new_unit(&unit_config, &unit_handle);
  if (err != ESP_OK) {
    ESP_LOGE(
        TAG,
        "Unable to init encodder: got error while configuring unit; err = %s",
        esp_err_to_name(err));
    return false;
  }

  pcnt_chan_config_t channel_a_conf = {
      .edge_gpio_num = ENC_A_GPIO,
      .level_gpio_num = ENC_B_GPIO,
  };
  pcnt_chan_config_t channel_b_conf = {
      .edge_gpio_num = ENC_B_GPIO,
      .level_gpio_num = ENC_A_GPIO,
  };

  err = pcnt_new_channel(unit_handle, &channel_a_conf, &channel_a_handle);
  if (err != ESP_OK) {
    ESP_LOGE(TAG,
             "Unable to init encodder: got error while configuring channel A; "
             "err = %s",
             esp_err_to_name(err));
    return false;
  }

  err = pcnt_new_channel(unit_handle, &channel_b_conf, &channel_b_handle);
  if (err != ESP_OK) {
    ESP_LOGE(TAG,
             "Unable to init encodder: got error while configuring channel B; "
             "err = %s",
             esp_err_to_name(err));
    return false;
  }

  err = pcnt_channel_set_edge_action(channel_a_handle,
                                     PCNT_CHANNEL_EDGE_ACTION_DECREASE,
                                     PCNT_CHANNEL_EDGE_ACTION_INCREASE);
  if (err != ESP_OK) {
    ESP_LOGE(TAG,
             "Unable to init encodder: got error while configuring edge "
             "actions for channel A; "
             "err = %s",
             esp_err_to_name(err));
    return false;
  }

  err = pcnt_channel_set_level_action(channel_a_handle,
                                      PCNT_CHANNEL_LEVEL_ACTION_KEEP,
                                      PCNT_CHANNEL_LEVEL_ACTION_INVERSE);
  if (err != ESP_OK) {
    ESP_LOGE(TAG,
             "Unable to init encodder: got error while configuring level "
             "actions for channel A; "
             "err = %s",
             esp_err_to_name(err));
    return false;
  }

  err = pcnt_channel_set_edge_action(channel_b_handle,
                                     PCNT_CHANNEL_EDGE_ACTION_INCREASE,
                                     PCNT_CHANNEL_EDGE_ACTION_DECREASE);
  if (err != ESP_OK) {
    ESP_LOGE(TAG,
             "Unable to init encodder: got error while configuring edge "
             "actions for channel B; "
             "err = %s",
             esp_err_to_name(err));
    return false;
  }

  err = pcnt_channel_set_level_action(channel_b_handle,
                                      PCNT_CHANNEL_LEVEL_ACTION_KEEP,
                                      PCNT_CHANNEL_LEVEL_ACTION_INVERSE);
  if (err != ESP_OK) {
    ESP_LOGE(TAG,
             "Unable to init encodder: got error while configuring level "
             "actions for channel B; "
             "err = %s",
             esp_err_to_name(err));
    return false;
  }

  pcnt_glitch_filter_config_t glitch_filter_config = {
      .max_glitch_ns = 1000,
  };

  err = pcnt_unit_set_glitch_filter(unit_handle, &glitch_filter_config);
  if (err != ESP_OK) {
    ESP_LOGE(TAG,
             "Unable to init encodder: got error while configuring glitch "
             "filter; err = %s",
             esp_err_to_name(err));
    return false;
  }

  err = pcnt_unit_add_watch_point(unit_handle, 4);
  if (err != ESP_OK) {
    ESP_LOGE(TAG,
             "Unable to init encodder: got error while configuring +1 watch "
             "point; err = %s",
             esp_err_to_name(err));
    return false;
  }

  err = pcnt_unit_add_watch_point(unit_handle, -4);
  if (err != ESP_OK) {
    ESP_LOGE(TAG,
             "Unable to init encodder: got error while configuring -1 watch "
             "point; err = %s",
             esp_err_to_name(err));
    return false;
  }

  pcnt_event_callbacks_t callbacks = {
      .on_reach = handle_enc_changed,
  };

  err = pcnt_unit_register_event_callbacks(unit_handle, &callbacks, nullptr);
  if (err != ESP_OK) {
    ESP_LOGE(TAG,
             "Unable to init encodder: got error while configuring events "
             "callback; err = %s",
             esp_err_to_name(err));
    return false;
  }

  err = pcnt_unit_enable(unit_handle);
  if (err != ESP_OK) {
    ESP_LOGE(TAG,
             "Unable to init encodder: got error while enabling unit; err = %s",
             esp_err_to_name(err));
    return false;
  }

  err = pcnt_unit_clear_count(unit_handle);
  if (err != ESP_OK) {
    ESP_LOGE(TAG,
             "Unable to init encodder: got error while clearing unit count; "
             "err = %s",
             esp_err_to_name(err));
    return false;
  }

  err = pcnt_unit_start(unit_handle);
  if (err != ESP_OK) {
    ESP_LOGE(TAG,
             "Unable to init encodder: got error while starting unit; err = %s",
             esp_err_to_name(err));
    return false;
  }

  return true;
}

bool init_lock() {
  esp_err_t err;
  ledc_timer_config_t timer_config = {.speed_mode = LEDC_MODE,
                                      .duty_resolution = LEDC_RESOLUTION,
                                      .timer_num = LOCK_TIMER,
                                      .freq_hz = 50,
                                      .clk_cfg = LEDC_AUTO_CLK};

  err = ledc_timer_config(&timer_config);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Unable to init lock: can't init LEDC Timer, err: '%s'",
             esp_err_to_name(err));
    return false;
  }

  ledc_channel_config_t channel_config = {.gpio_num = LOCK_CTRL_GPIO,
                                          .speed_mode = LEDC_MODE,
                                          .channel = LOCK_SERVO_CHANNEL,
                                          .intr_type = LEDC_INTR_DISABLE,
                                          .timer_sel = LOCK_TIMER,
                                          .duty = 0,
                                          .hpoint = 0};

  err = ledc_channel_config(&channel_config);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Unable to init lock: can't init LEDC Channel, err: '%s'",
             esp_err_to_name(err));
    return false;
  }

  lock_safe();

  return true;
}

bool init_buzzer() {
  play_sounds_tasks_queue = xQueueCreate(10, sizeof(const Riff *));
  if (play_sounds_tasks_queue == nullptr) {
    ESP_LOGE(TAG, "Unable to init buzzer: can't create queue");
    return false;
  }

  esp_err_t err;
  ledc_timer_config_t timer_config = {.speed_mode = LEDC_MODE,
                                      .duty_resolution = LEDC_RESOLUTION,
                                      .timer_num = BUZZER_TIMER,
                                      .freq_hz = 10000,
                                      .clk_cfg = LEDC_AUTO_CLK};

  err = ledc_timer_config(&timer_config);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Unable to init buzzer: can't init LEDC Timer, err: '%s'",
             esp_err_to_name(err));
    return false;
  }

  ledc_channel_config_t channel_config = {.gpio_num = BUZZER_GPIO,
                                          .speed_mode = LEDC_MODE,
                                          .channel = BUZZER_SERVO_CHANNEL,
                                          .intr_type = LEDC_INTR_DISABLE,
                                          .timer_sel = BUZZER_TIMER,
                                          .duty = 0,
                                          .hpoint = 0};

  err = ledc_channel_config(&channel_config);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Unable to init buzzer: can't init LEDC Channel, err: '%s'",
             esp_err_to_name(err));
    return false;
  }

  return true;
}

void handle_press() { safe_fsm::click(safe_handle); }

void handle_longpress() { safe_fsm::longclick(safe_handle); }

bool IRAM_ATTR handle_enc_changed(pcnt_unit_handle_t unit,
                                  const pcnt_watch_event_data_t *event,
                                  void *arg) {
  BaseType_t woken = pdFALSE;
  xQueueSendFromISR(encodder_events, event, &woken);

  return woken == pdTRUE;
}

void enc_events_handler_task(void *arg) {
  pcnt_watch_event_data_t event;
  while (true) {
    xQueueReceive(encodder_events, &event, portMAX_DELAY);
    if (event.watch_point_value == 4) {
      safe_fsm::increment(safe_handle);
    } else if (event.watch_point_value == -4) {
      safe_fsm::decrement(safe_handle);
    }
  }
}

void handle_safe_event(safe_fsm::event_t event) {
  esp_err_t err;
  const Riff *riff;

  switch (event.type) {
  case safe_fsm::event_type_t::LOCKED:
    ESP_LOGI(TAG, "LOCKED");
    lock_safe();
    riff = &sounds::SAFE_LOCKED;
    xQueueSend(play_sounds_tasks_queue, &riff, portMAX_DELAY);
    break;
  case safe_fsm::event_type_t::UNLOCKED:
    ESP_LOGI(TAG, "UNLOCKED");
    unlock_safe();
    riff = &sounds::SAFE_UNLOCKED;
    xQueueSend(play_sounds_tasks_queue, &riff, portMAX_DELAY);
    break;
  case safe_fsm::event_type_t::BLOCKED:
    ESP_LOGI(TAG, "BLOCKED");
    riff = &sounds::SAFE_BLOCKED;
    xQueueSend(play_sounds_tasks_queue, &riff, portMAX_DELAY);
    break;
  case safe_fsm::event_type_t::WRONG_INPUT:
    ESP_LOGI(TAG, "WRONG_INPUT, attempts: %d", event.wrong_input.attempts);
    riff = &sounds::WRONG_INPUT;
    xQueueSend(play_sounds_tasks_queue, &riff, portMAX_DELAY);
    break;
  case safe_fsm::event_type_t::INPUT: {
    const auto &input = event.input.input;
    ESP_LOGI(TAG, "SAFE INPUT: %u %u %u %u %u %u %u %u", input[0], input[1],
             input[2], input[3], input[4], input[5], input[6], input[7]);
    riff = &sounds::DIGIT_INPUT;
    xQueueSend(play_sounds_tasks_queue, &riff, 0);
    break;
  }
  case safe_fsm::event_type_t::NEXT_DIGIT:
    riff = &sounds::NEXT_DIGIT;
    xQueueSend(play_sounds_tasks_queue, &riff, 0);
    break;
  }
}

void unlock_safe() {
  esp_err_t err;
  err = ledc_set_duty(LEDC_MODE, LOCK_SERVO_CHANNEL, UNLOCKED_SERVO_DUTY);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Unable to unlock safe: can't set initial servo duty");
  }
  err = ledc_update_duty(LEDC_MODE, LOCK_SERVO_CHANNEL);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Unable to unlock safe: can't update initial servo duty");
  }
}

void lock_safe() {
  esp_err_t err;
  err = ledc_set_duty(LEDC_MODE, LOCK_SERVO_CHANNEL, LOCKED_SERVO_DUTY);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Unable to lock safe: can't set initial servo duty");
  }
  err = ledc_update_duty(LEDC_MODE, LOCK_SERVO_CHANNEL);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Unable to lock safe: can't update initial servo duty");
  }
}

void play_sound_task(void *arg) {
  Riff *riff;
  while (true) {
    if (xQueueReceive(play_sounds_tasks_queue, &riff, portMAX_DELAY) !=
        pdTRUE) {
      continue;
    }

    for (int i = 0; i < riff->length; i++) {
      play_note(riff->notes[i]);
    }
  }
}

void play_note(Note note) {
  uint16_t note_duration_ms =
      (60000 * 4) / (sounds::BPM * static_cast<int>(note.duration));
  uint16_t sound_duration_ms = note_duration_ms * 9 / 10;
  uint16_t pause_duration_ms = note_duration_ms - sound_duration_ms;

  if (note.frequency == 0) {
    ledc_set_duty(LEDC_MODE, BUZZER_SERVO_CHANNEL, 0);
    ledc_update_duty(LEDC_MODE, BUZZER_SERVO_CHANNEL);
    vTaskDelay(pdMS_TO_TICKS(note_duration_ms));
  } else {
    ledc_set_freq(LEDC_MODE, BUZZER_TIMER, note.frequency);
    ledc_set_duty(LEDC_MODE, BUZZER_SERVO_CHANNEL, BUZZER_DUTY);
    ledc_update_duty(LEDC_MODE, BUZZER_SERVO_CHANNEL);
    vTaskDelay(pdMS_TO_TICKS(sound_duration_ms));
    ledc_set_duty(LEDC_MODE, BUZZER_SERVO_CHANNEL, 0);
    ledc_update_duty(LEDC_MODE, BUZZER_SERVO_CHANNEL);
    vTaskDelay(pdMS_TO_TICKS(pause_duration_ms));
  }
}
