#pragma once

#include "esp_err.h"
#include "spi_flash_driver.h"
#include "environment_sample.h"

namespace environment_log {

struct environment_log;

using handle_t = environment_log *;

esp_err_t init(spi_flash::handle_t flash, handle_t *out_handle);
esp_err_t append(handle_t handle, const environment_sample_t &sample);

} // namespace environment_log
