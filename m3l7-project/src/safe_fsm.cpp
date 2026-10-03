#include "safe_fsm.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include <algorithm>
#include <cstring>
#include <iterator>
#include <optional>

namespace safe_fsm {

static const char *TAG = "SAFE";
static constexpr auto DEFAULT_BLOCK_TIME_TICKS = pdMS_TO_TICKS(100);
enum class state_t {
  LOCKED,
  UNLOCKED,
  BLOCKED,
};

struct handle_t {
  config_t config;

  state_t state;
  uint8_t current_attempt;
  uint8_t input[PASSWORD_LENGTH];
  uint8_t current_symbol_idx;
  SemaphoreHandle_t mutex;
};

handle_t *init(const config_t &config) {
  if (config.on_event == nullptr) {
    ESP_LOGE(TAG, "Unable to initialize safe FSM: invalid config - on_event is "
                  "not defined");
    return nullptr;
  }

  if (config.attempts == 0) {
    ESP_LOGE(TAG, "Unable to initialize safe FSM: invalid config - attempts "
                  "should be at least 1");
    return nullptr;
  }

  for (uint8_t i = 0; i < PASSWORD_LENGTH; i++) {
    if (config.password[i] < 0 || config.password[i] > 9) {
      ESP_LOGE(TAG,
               "Unable to initialize safe FSM: invalid config - password "
               "contains illeagal number '%d' at position #%d",
               config.password[i], i);
      return nullptr;
    }
  }

  auto mutex = xSemaphoreCreateMutex();
  if (mutex == nullptr) {
    ESP_LOGE(TAG, "Unable to initialize safe FSM: can't create mutex");
    return nullptr;
  }

  return new handle_t{
      .config = config,
      .state = state_t::LOCKED,
      .current_attempt = 0,
      .input = {0, 0, 0, 0, 0, 0, 0, 0},
      .current_symbol_idx = 0,
      .mutex = mutex,
  };
}

void increment(handle_t *handle) {
  if (handle == nullptr) {
    ESP_LOGE(TAG, "Unable to increment: passed handle is NULL");
    return;
  }

  if (xSemaphoreTake(handle->mutex, DEFAULT_BLOCK_TIME_TICKS) != pdTRUE) {
    ESP_LOGW(TAG, "Unable to increment: can't take mutex");
    return;
  }

  if (handle->state == state_t::BLOCKED) {
    ESP_LOGW(TAG, "Unable to increment: illegal state (BLOCKED)");
    xSemaphoreGive(handle->mutex);
    return;
  }

  handle->input[handle->current_symbol_idx] =
      handle->input[handle->current_symbol_idx] >= 9
          ? 0
          : handle->input[handle->current_symbol_idx] + 1;

  event_t event = {.type = event_type_t::INPUT,
                   .input = {
                       .input = {},
                   }};
  std::memcpy(event.input.input, handle->input, std::size(event.input.input));

  auto callback = handle->config.on_event;

  if (xSemaphoreGive(handle->mutex) != pdTRUE) {
    ESP_LOGW(TAG, "Faile while incrementing: Unable to give mutex");
  }

  callback(event);
}

void decrement(handle_t *handle) {
  if (handle == nullptr) {
    ESP_LOGE(TAG, "Unable to decrement: passed handle is NULL");
    return;
  }

  if (xSemaphoreTake(handle->mutex, DEFAULT_BLOCK_TIME_TICKS) != pdTRUE) {
    ESP_LOGW(TAG, "Unable to decrement: can't take mutex");
    return;
  }

  if (handle->state == state_t::BLOCKED) {
    ESP_LOGW(TAG, "Unable to decrement: illegal state (BLOCKED)");
    xSemaphoreGive(handle->mutex);
    return;
  }

  handle->input[handle->current_symbol_idx] =
      handle->input[handle->current_symbol_idx] <= 0
          ? 9
          : handle->input[handle->current_symbol_idx] - 1;

  event_t event = {.type = event_type_t::INPUT,
                   .input = {
                       .input = {},
                   }};
  std::memcpy(event.input.input, handle->input, std::size(event.input.input));

  auto callback = handle->config.on_event;

  if (xSemaphoreGive(handle->mutex) != pdTRUE) {
    ESP_LOGW(TAG, "Faile while decrement: Unable to give mutex");
  }

  callback(event);
}

void click(handle_t *handle) {
  if (handle == nullptr) {
    ESP_LOGE(TAG, "Unable to handle click: passed handle is NULL");
    return;
  }

  if (xSemaphoreTake(handle->mutex, DEFAULT_BLOCK_TIME_TICKS) != pdTRUE) {
    ESP_LOGW(TAG, "Unable to handle click: can't take mutex");
    return;
  }

  if (handle->state == state_t::BLOCKED) {
    ESP_LOGW(TAG, "Unable to handle click: illegal state (BLOCKED)");
    xSemaphoreGive(handle->mutex);
    return;
  }

  auto callback = handle->config.on_event;
  event_t event = {
      .type = event_type_t::NEXT_DIGIT,
  };

  handle->current_symbol_idx =
      handle->current_symbol_idx >= (PASSWORD_LENGTH - 1)
          ? 0
          : handle->current_symbol_idx + 1;

  if (xSemaphoreGive(handle->mutex) != pdTRUE) {
    ESP_LOGW(TAG, "Faile while handle click: Unable to give mutex");
  }

  callback(event);
}

void longclick(handle_t *handle) {
  if (handle == nullptr) {
    ESP_LOGE(TAG, "Unable to handle longclick: passed handle is NULL");
    return;
  }

  if (xSemaphoreTake(handle->mutex, DEFAULT_BLOCK_TIME_TICKS) != pdTRUE) {
    ESP_LOGW(TAG, "Unable to handle longclick: can't take mutex");
    return;
  }

  if (handle->state == state_t::BLOCKED) {
    ESP_LOGW(TAG, "Unable to handle longclick: illegal state (BLOCKED)");
    xSemaphoreGive(handle->mutex);
    return;
  }

  bool has_event = false;
  event_t event;

  switch (handle->state) {
  case state_t::LOCKED: {
    bool password_matches =
        std::equal(std::begin(handle->input), std::end(handle->input),
                   std::begin(handle->config.password));

    handle->current_attempt++;

    if (password_matches) {
      handle->current_symbol_idx = 0;
      handle->state = state_t::UNLOCKED;
      event = event_t{.type = event_type_t::UNLOCKED};
      has_event = true;
    } else if (handle->current_attempt < handle->config.attempts) {
      event = event_t{.type = event_type_t::WRONG_INPUT,
                      .wrong_input{
                          .attempts = handle->current_attempt,
                      }};
      has_event = true;
    } else {
      handle->state = state_t::BLOCKED;
      event = event_t{.type = event_type_t::BLOCKED};
      has_event = true;
    }
    break;
  }
  case state_t::UNLOCKED: {
    memcpy(handle->config.password, handle->input,
           sizeof(handle->config.password));
    memset(handle->input, 0, sizeof(handle->input));

    handle->current_attempt = 0;
    handle->current_symbol_idx = 0;
    handle->state = state_t::LOCKED;

    event = event_t{.type = event_type_t::LOCKED};
    has_event = true;
    break;
  }
  default: {
    ESP_LOGW(TAG, "Unable to handle longclick: illegal state - %d",
             static_cast<int>(handle->state));
    break;
  }
  }

  const auto callback = handle->config.on_event;

  if (xSemaphoreGive(handle->mutex) != pdTRUE) {
    ESP_LOGW(TAG, "Faile while handle longclick: Unable to give mutex");
  }

  if (has_event) {
    callback(event);
  }
}

} // namespace safe_fsm
