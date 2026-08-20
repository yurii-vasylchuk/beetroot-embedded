#pragma once

#include <cstdint>

namespace btn_fsm {

constexpr std::uint16_t DEBOUNCE_DEFAULT_MS = 30;
constexpr std::uint16_t LONGPRESS_DEFAULT_MS = 1000;

enum event_type_t {
  BTN_FSM_PRESS,
  BTN_FSM_LONGPRESS,
};

enum log_level_t {
  BTN_FSM_DEBUG,
  BTN_FSM_INFO,
  BTN_FSM_WARN,
  BTN_FSM_ERROR,
};

using log_function_t = void (*)(log_level_t, const char *format, ...);

using callback_t = void (*)(void);

using now_us_t = std::uint64_t();

struct handle_t;

struct config_t {
  log_function_t log = nullptr;
  callback_t press_callback = nullptr;
  callback_t longpress_callback = nullptr;
  now_us_t &now;

  std::uint16_t debounce_ms = DEBOUNCE_DEFAULT_MS;
  std::uint16_t longpress_ms = LONGPRESS_DEFAULT_MS;
};

handle_t *create(const config_t &config);

void register_callback(handle_t *, callback_t, event_type_t);

void register_btn_up(handle_t *);
void register_btn_down(handle_t *);
void tick(handle_t *);
void destroy(handle_t *);
} // namespace btn_fsm
