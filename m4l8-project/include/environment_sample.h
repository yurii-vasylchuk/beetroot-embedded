#pragma once

#include <cstdint>

struct environment_sample_t {
  int64_t uptime_ms;
  float temperature_c;
  float pressure_hpa;
  float humidity_percent;
};
