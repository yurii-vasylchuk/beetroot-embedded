#pragma once
#include "driver/i2c_master.h"

namespace ds1307 {

struct ds1307;
using handle_t = ds1307 *;

enum class day_of_week_t : uint8_t {
  UNK = 0u,
  Mon,
  Tue,
  Wed,
  Thu,
  Fri,
  Sat,
  Sun,
};

struct date_t {
  uint8_t seconds;
  uint8_t minutes;
  uint8_t hours;
  uint8_t day_of_month;
  uint8_t month;
  uint8_t year;
  day_of_week_t day_of_week;
};

struct config_t {
  date_t initial_date;
};

handle_t init(i2c_master_bus_handle_t bus, config_t *config);

date_t now(handle_t);

// Utilities
const char *day_of_week_to_string(day_of_week_t);

} // namespace ds1307
