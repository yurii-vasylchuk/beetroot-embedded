#include "button_fsm.hpp"
#include <inttypes.h>

namespace btn_fsm {

enum state_t {
  BTN_FSM_STATE_IDLE,
  BTN_FSM_STATE_DETECTED,
  BTN_FSM_STATE_PRESSED,
  BTN_FSM_STATE_LONGPRESSED,
};

struct handle_t {
  log_function_t log;
  callback_t press_callback;
  callback_t longpress_callback;
  now_us_t &now;

  std::uint16_t debounce_ms;
  std::uint16_t longpress_ms;

  std::uint64_t last_press;

  state_t state;
};

handle_t *create(const config_t &config) {
  auto handle = new handle_t{
      .log = config.log,
      .press_callback = config.press_callback,
      .longpress_callback = config.longpress_callback,
      .now = config.now,

      .debounce_ms = config.debounce_ms,
      .longpress_ms = config.longpress_ms,

      .state = BTN_FSM_STATE_IDLE,
  };

  return handle;
}

void register_callback(handle_t *handle, callback_t callback,
                       event_type_t event_type) {
  if (handle == nullptr)
    return;

  switch (event_type) {
  case BTN_FSM_PRESS:
    handle->press_callback = callback;
    break;

  case BTN_FSM_LONGPRESS:
    handle->longpress_callback = callback;
    break;
  }
}

void register_btn_up(handle_t *handle) {
  uint64_t now_us = handle->now();
  uint32_t dt_ms = (now_us - handle->last_press) / 1000;
  handle->log(BTN_FSM_INFO, "Got UP event on %" PRIu64, now_us);

  switch (handle->state) {
  case BTN_FSM_STATE_IDLE:
    // Just ignore
    break;

  case BTN_FSM_STATE_DETECTED:
    if (dt_ms > handle->debounce_ms && dt_ms < handle->longpress_ms) {
      // click detected
      handle->last_press = now_us;
      handle->state = BTN_FSM_STATE_IDLE;
      if (handle->press_callback != nullptr)
        handle->press_callback();
    } else if (dt_ms > handle->longpress_ms) {
      handle->log(BTN_FSM_WARN, "Looks like tick call missed.");
      handle->state = BTN_FSM_STATE_IDLE;
      handle->last_press = now_us;
      if (handle->longpress_callback != nullptr)
        handle->longpress_callback();
    }
    break;

  case BTN_FSM_STATE_PRESSED:
    if (dt_ms < handle->longpress_ms) {
      handle->last_press = now_us;
      handle->state = BTN_FSM_STATE_IDLE;
      if (handle->press_callback != nullptr)
        handle->press_callback();
    } else if (dt_ms > handle->longpress_ms) {
      handle->last_press = now_us;
      handle->state = BTN_FSM_STATE_IDLE;
      if (handle->longpress_callback != nullptr)
        handle->longpress_callback();
    }
    break;
  case BTN_FSM_STATE_LONGPRESSED:
    handle->state = BTN_FSM_STATE_IDLE;
    handle->last_press = 0;
    break;
  }
}

void register_btn_down(handle_t *handle) {
  uint64_t now_us = handle->now();
  uint32_t dt_ms = (now_us - handle->last_press) / 1000;
  handle->log(BTN_FSM_INFO, "Got DOWN event on %" PRIu64, now_us);

  switch (handle->state) {
  case BTN_FSM_STATE_IDLE:
    handle->state = BTN_FSM_STATE_DETECTED;
    handle->last_press = now_us;
    break;

  case BTN_FSM_STATE_DETECTED:
    if (dt_ms < handle->longpress_ms) {
      // Just skip
    } else {
      handle->log(BTN_FSM_WARN, "Looks loke tick call missed.");
      handle->last_press = now_us;
    }
    break;

  case BTN_FSM_STATE_PRESSED:
    // Just ignore
    break;

  case BTN_FSM_STATE_LONGPRESSED:
    // Just ignore
    break;
  }
}

void tick(handle_t *handle) {
  uint64_t now_us = handle->now();
  uint32_t dt_ms = (now_us - handle->last_press) / 1000;

  switch (handle->state) {
  case BTN_FSM_STATE_IDLE:
    // Just skip;
    break;
  case BTN_FSM_STATE_DETECTED:
    if (dt_ms > handle->debounce_ms && dt_ms < handle->longpress_ms) {
      handle->state = BTN_FSM_STATE_PRESSED;
    } else if (dt_ms > handle->longpress_ms) {
      handle->state = BTN_FSM_STATE_LONGPRESSED;
      if (handle->longpress_callback != nullptr) {
        handle->longpress_callback();
      }
    }
    break;
  case BTN_FSM_STATE_PRESSED:
    if (dt_ms > handle->longpress_ms) {
      handle->state = BTN_FSM_STATE_LONGPRESSED;
      if (handle->longpress_callback != nullptr) {
        handle->longpress_callback();
      }
    }
    break;
  case BTN_FSM_STATE_LONGPRESSED:
    // Just ignore
    break;
  }
}

void destroy(handle_t *handle) { delete handle; }
} // namespace btn_fsm
