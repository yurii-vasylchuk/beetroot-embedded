#pragma once

#include <cstdint>
namespace safe_fsm {

static constexpr uint8_t PASSWORD_LENGTH = 8;

struct handle_t;

enum class event_type_t {
  UNLOCKED,
  LOCKED,
  BLOCKED,
  INPUT,
  WRONG_INPUT,
  NEXT_DIGIT,
};

struct event_t {
  event_type_t type;

  union {
    struct {
      uint8_t attempts;
    } wrong_input;

    struct {
      uint8_t input[PASSWORD_LENGTH];
    } input;
  };
};

using event_handler_t = void (*)(event_t);

struct config_t {
  event_handler_t on_event;
  uint8_t password[PASSWORD_LENGTH];
  uint8_t attempts;
};

handle_t *init(const config_t &);

void increment(handle_t *);
void decrement(handle_t *);
void click(handle_t *);
void longclick(handle_t *);
} // namespace safe_fsm
