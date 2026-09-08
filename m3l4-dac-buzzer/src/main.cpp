#include "doom.hpp"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "notes.hpp"
#include "super_mario.hpp"

static const char *TAG = "M3L4";

static constexpr gpio_num_t BUZZER_GPIO = GPIO_NUM_4;
static constexpr ledc_timer_bit_t LEDC_RESOLUTION = LEDC_TIMER_12_BIT;
static constexpr uint16_t LEDC_DUTY = ((1 << LEDC_RESOLUTION) - 1) / 2;
static constexpr ledc_mode_t LEDC_MODE = LEDC_LOW_SPEED_MODE;
static constexpr ledc_channel_t BUZZER_CHANNEL = LEDC_CHANNEL_0;
static constexpr uint16_t BPM = 150;

static constexpr Song song = DOOM_SONG;

static void init_ledc();

void play_note(Note);

extern "C" void app_main() {
  init_ledc();

  while (true) {
    for (int r = 0; r < song.length; r++) {
      Riff riff = song.riffs[r];

      for (int n = 0; n < riff.length; n++) {
        play_note(riff.notes[n]);
      }
    }
  }
}

void init_ledc() {
  ledc_timer_config_t timer_config = {.speed_mode = LEDC_MODE,
                                      .duty_resolution = LEDC_RESOLUTION,
                                      .timer_num = LEDC_TIMER_0,
                                      .freq_hz = 10000,
                                      .clk_cfg = LEDC_AUTO_CLK};

  ESP_ERROR_CHECK(ledc_timer_config(&timer_config));

  ledc_channel_config_t channel_config = {.gpio_num = BUZZER_GPIO,
                                          .speed_mode = LEDC_MODE,
                                          .channel = BUZZER_CHANNEL,
                                          .intr_type = LEDC_INTR_DISABLE,
                                          .timer_sel = LEDC_TIMER_0,
                                          .duty = 0,
                                          .hpoint = 0};

  ESP_ERROR_CHECK(ledc_channel_config(&channel_config));
}

void play_note(Note note) {
  uint16_t note_duration_ms =
      (60000 * 4) / (BPM * static_cast<int>(note.duration));
  uint16_t sound_duration_ms = note_duration_ms * 9 / 10;
  uint16_t pause_duration_ms = note_duration_ms - sound_duration_ms;

  if (note.frequency == 0) {
    ledc_set_duty(LEDC_MODE, BUZZER_CHANNEL, 0);
    ledc_update_duty(LEDC_MODE, BUZZER_CHANNEL);
    vTaskDelay(pdMS_TO_TICKS(note_duration_ms));
  } else {
    ledc_set_freq(LEDC_MODE, LEDC_TIMER_0, note.frequency);
    ledc_set_duty(LEDC_MODE, BUZZER_CHANNEL, LEDC_DUTY);
    ledc_update_duty(LEDC_MODE, BUZZER_CHANNEL);
    vTaskDelay(pdMS_TO_TICKS(sound_duration_ms));
    ledc_set_duty(LEDC_MODE, BUZZER_CHANNEL, 0);
    ledc_update_duty(LEDC_MODE, BUZZER_CHANNEL);
    vTaskDelay(pdMS_TO_TICKS(pause_duration_ms));
  }
}
