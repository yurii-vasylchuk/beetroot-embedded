#include "traffic_light_fsm.hpp"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace traffic_light_fsm {

static const char *TAG = "TLFSM";
static constexpr std::uint64_t YELLOW_BLINK_PERIOD_MS = 500;
static constexpr std::uint64_t YELLOW_PERIOD_MS = 3000;

enum state_t {
  RED,
  RED_YELLOW,
  GREEN,
  YELLOW,
  NONE,
};

struct handle_t {
  mode_t mode;
  state_t state;
  esp_timer_handle_t timer;
  std::uint64_t allow_period_ms;
  std::uint64_t deny_period_ms;

  gpio_num_t red;
  gpio_num_t ylw;
  gpio_num_t grn;
};

static void init_timer(handle_t *handle);
static void timer_callback(void *);
static state_t next_state(handle_t *);
static void handle_state(handle_t *);
static void set_traffic_light(handle_t *handle, bool red, bool yellow,
                              bool green);

void set_mode(handle_t *handle, mode_t mode);
void switch_mode(handle_t *handle);
void set_allow_period_ms(handle_t *handle, std::uint32_t period);
void set_deny_period_ms(handle_t *handle, std::uint32_t period);

handle_t *create(std::uint32_t allow_period_ms, std::uint32_t deny_period_ms,
                 gpio_num_t red, gpio_num_t yellow, gpio_num_t green) {

  handle_t *handle = new handle_t{
      .mode = TRAFFIC_LIGHT_MODE_AUTO,
      .state = RED,
      .timer = nullptr,
      .allow_period_ms = static_cast<std::uint64_t>(allow_period_ms),
      .deny_period_ms = static_cast<std::uint64_t>(deny_period_ms),

      .red = red,
      .ylw = yellow,
      .grn = green,
  };

  init_timer(handle);
  handle_state(handle);

  return handle;
}

void set_mode(handle_t *handle, mode_t mode) {
  if (handle == nullptr) {
    ESP_LOGE(TAG, "NULL traffic light fsm handle");
    return;
  }

  handle->mode = mode;

  if (mode == TRAFFIC_LIGHT_MODE_AUTO) {
    handle->state = RED;
  } else {
    handle->state = YELLOW;
  }

  ESP_ERROR_CHECK(esp_timer_stop(handle->timer));
  handle_state(handle);
}

void set_allow_period_ms(handle_t *handle, std::uint32_t period) {
  if (handle == nullptr) {
    ESP_LOGE(TAG, "NULL traffic light fsm handle");
    return;
  }

  handle->allow_period_ms = static_cast<std::uint64_t>(period);
}

void set_deny_period_ms(handle_t *handle, std::uint32_t period) {
  if (handle == nullptr) {
    ESP_LOGE(TAG, "NULL traffic light fsm handle");
    return;
  }

  handle->deny_period_ms = static_cast<std::uint64_t>(period);
}

void destroy(handle_t *handle) {
  ESP_ERROR_CHECK(esp_timer_stop(handle->timer));
  ESP_ERROR_CHECK(esp_timer_delete(handle->timer));
  delete handle;
}

static void init_timer(handle_t *handle) {
  esp_timer_create_args_t config = {
      .callback = timer_callback,
      .arg = handle,
      .dispatch_method = ESP_TIMER_TASK,
      .name = "traffic_light_timer",
      .skip_unhandled_events = true,
  };

  esp_timer_handle_t timer;
  ESP_ERROR_CHECK(esp_timer_create(&config, &timer));
  handle->timer = timer;
}

static void timer_callback(void *arg) {
  handle_t *handle = (handle_t *)arg;
  handle->state = next_state(handle);
  handle_state(handle);
}

static state_t next_state(handle_t *handle) {
  if (handle == nullptr) {
    ESP_LOGE(TAG, "NULL traffic light fsm handle");
    return RED;
  }

  if (handle->mode == TRAFFIC_LIGHT_MODE_MANUAL) {
    switch (handle->state) {
    case YELLOW:
      return NONE;
    case NONE:
      return YELLOW;
    default:
      ESP_LOGE(TAG, "Illegal state [%d] for manual mode ", handle->state);
      return YELLOW;
    }
  } else {
    switch (handle->state) {
    case RED:
      return RED_YELLOW;
    case RED_YELLOW:
      return GREEN;
    case GREEN:
      return YELLOW;
    case YELLOW:
      return RED;
    default:
      ESP_LOGE(TAG, "Illegal state [%d] for auto mode", handle->state);
      return RED;
    }
  }
}

static void handle_state(handle_t *handle) {
  if (handle == nullptr) {
    ESP_LOGE(TAG, "NULL traffic light fsm handle");
    return;
  }

  if (handle->mode == TRAFFIC_LIGHT_MODE_MANUAL) {
    switch (handle->state) {
    case YELLOW:
      set_traffic_light(handle, false, true, false);
      ESP_ERROR_CHECK(
          esp_timer_start_once(handle->timer, YELLOW_BLINK_PERIOD_MS * 1000));
      break;
    case NONE:
      set_traffic_light(handle, false, false, false);
      ESP_ERROR_CHECK(
          esp_timer_start_once(handle->timer, YELLOW_BLINK_PERIOD_MS * 1000));
      break;
    default:
      set_traffic_light(handle, false, false, false);
      ESP_LOGE(TAG, "Illegal state [%d] for manual mode ", handle->state);
      break;
    }
  } else {
    switch (handle->state) {
    case RED:
      set_traffic_light(handle, true, false, false);
      ESP_ERROR_CHECK(esp_timer_start_once(
          handle->timer, (handle->deny_period_ms - YELLOW_PERIOD_MS) * 1000));
      break;
    case RED_YELLOW:
      set_traffic_light(handle, true, true, false);
      ESP_ERROR_CHECK(
          esp_timer_start_once(handle->timer, YELLOW_PERIOD_MS * 1000));
      break;
    case GREEN:
      set_traffic_light(handle, false, false, true);
      ESP_ERROR_CHECK(esp_timer_start_once(
          handle->timer, (handle->allow_period_ms - YELLOW_PERIOD_MS) * 1000));
      break;
    case YELLOW:
      set_traffic_light(handle, false, true, false);
      ESP_ERROR_CHECK(
          esp_timer_start_once(handle->timer, YELLOW_PERIOD_MS * 1000));
      break;
    default:
      ESP_LOGE(TAG, "Illegal state [%d] for auto mode", handle->state);
      break;
    }
  }
}

void set_traffic_light(handle_t *handle, bool red, bool yellow, bool green) {
  gpio_set_level(handle->red, red ? 1 : 0);
  gpio_set_level(handle->ylw, yellow ? 1 : 0);
  gpio_set_level(handle->grn, green ? 1 : 0);
}

void switch_mode(handle_t *handle) {
  if (handle->mode == TRAFFIC_LIGHT_MODE_MANUAL) {
    set_mode(handle, TRAFFIC_LIGHT_MODE_AUTO);
  } else {
    set_mode(handle, TRAFFIC_LIGHT_MODE_MANUAL);
  }
}
} // namespace traffic_light_fsm
