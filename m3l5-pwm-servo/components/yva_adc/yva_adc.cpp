#include "yva_adc.hpp"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include <algorithm>
#include <atomic>
#include <vector>
namespace yva_adc {

static const char *TAG = "YVA_ADC";
static constexpr uint16_t ADC_OPERATION_TIMEOUT_MS = 100;

struct callback_to_call_t {
  change_callback_t callback;
  float arg;
};

struct module_handle_t {
  TaskHandle_t task;
  esp_timer_handle_t timer;

  uint16_t meter_period_ms;

  std::vector<handle_t> units;
  SemaphoreHandle_t units_mutex;
};

struct handle {
  adc_oneshot_unit_handle_t adc_handle;
  adc_cali_handle_t calibration_handle;
  adc_unit_t adc_unit;
  adc_channel_t adc_channel;

  bool on_change_enabled = false;
  change_callback_t on_change;
  float change_threshold;
  uint16_t max_calibrated_value;

  float value;
  SemaphoreHandle_t unit_mutex;

  gpio_num_t gpio;
};

std::atomic<module_handle_t *> module_handle;

void meter_adc_task(void *);
void alarm_handler(void *);

void init_module(module_config_t &config) {
  esp_err_t err;
  auto module = module_handle.load();

  if (module != nullptr) {
    ESP_LOGW(TAG, "Yva ADC module is already initialized");
    return; // TODO: Return ERROR
  }

  if (config.meter_period_ms <= 0) {
    ESP_LOGE(TAG,
             "Invalid configuration meter_period_ms should be greater then 0");
    return;
  }

  TaskHandle_t task;
  if (xTaskCreate(meter_adc_task, "meter-adc", 4096, nullptr,
                  config.task_priority, &task) != pdPASS) {
    ESP_LOGE(TAG, "Unable to create ADC Meter task");
    return; // TODO: return ERROR
  }

  const esp_timer_create_args_t timer_conf = {
      .callback = alarm_handler,
      .arg = task,
      .dispatch_method = ESP_TIMER_TASK,
      .name = "yva adc timer",
      .skip_unhandled_events = true,
  };

  esp_timer_handle_t timer;
  err = esp_timer_create(&timer_conf, &timer);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Unable to initialize timer, Error: %s",
             esp_err_to_name(err));
    vTaskDelete(task);
    return; // TODO: return ERROR
  }

  SemaphoreHandle_t units_mutex = xSemaphoreCreateMutex();
  if (units_mutex == nullptr) {
    ESP_LOGE(TAG, "Unable to create units mutex");
    vTaskDelete(task);
    esp_timer_delete(timer);
    return; // TODO: return ERROR
  }

  module_handle.store(new module_handle_t{
      .task = task,
      .timer = timer,
      .meter_period_ms = config.meter_period_ms,
      .units = {},
      .units_mutex = units_mutex,
  });

  err = esp_timer_start_periodic(timer, config.meter_period_ms * 1000);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Unable to start periodic timer; Error %s",
             esp_err_to_name(err));
    vTaskDelete(task);
    esp_timer_delete(timer);
    delete module_handle.load();
    module_handle.store(nullptr);
    vSemaphoreDelete(units_mutex);
  }
}

void alarm_handler(void *arg) { xTaskNotifyGive((TaskHandle_t)arg); }

void meter_adc_task(void *arg) {
  std::vector<callback_to_call_t> callbacks_to_call = {};
  while (true) {
    if (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(10'000)) <= 0) {
      // Timeout
      ESP_LOGD(TAG, "Haven't been notified for 10 seconds");
      continue;
    }

    auto module = module_handle.load();
    if (module == nullptr) {
      ESP_LOGW(TAG, "Module were not initialized");
      continue;
    }

    int value;
    int calibrated;

    esp_err_t err;

    if (xSemaphoreTake(module->units_mutex,
                       pdMS_TO_TICKS(module->meter_period_ms * 0.8)) ==
        pdTRUE) {
      for (auto unit : module->units) {
        if (xSemaphoreTake(unit->unit_mutex,
                           pdMS_TO_TICKS(ADC_OPERATION_TIMEOUT_MS)) != pdTRUE) {
          ESP_LOGW(TAG,
                   "Unable to meter ADC value for ADC_CHANNEL=%d ADC_UNIT=%d",
                   unit->adc_channel, unit->adc_unit);
          continue;
        }
        err = adc_oneshot_read(unit->adc_handle, unit->adc_channel, &value);
        if (err != ESP_OK) {
          ESP_LOGW(TAG, "Unable to read raw adjustment, err: %s",
                   esp_err_to_name(err));
          xSemaphoreGive(unit->unit_mutex);
          continue;
        }

        err = adc_cali_raw_to_voltage(unit->calibration_handle, value,
                                      &calibrated);
        if (err != ESP_OK) {
          ESP_LOGW(TAG, "Unable to read raw adjustment, err: %s",
                   esp_err_to_name(err));
          xSemaphoreGive(unit->unit_mutex);
          continue;
        }

        float result = std::clamp(
            (float)calibrated / unit->max_calibrated_value, 0.0f, 1.0f);

        ESP_LOGD(TAG,
                 "Value read from [GPIO: %d] ADC; Raw: %d, Calibrated: %d; "
                 "Result: %f",
                 unit->gpio, value, calibrated, result);

        if (std::abs(unit->value - result) > unit->change_threshold) {
          if (unit->on_change_enabled && unit->on_change != nullptr) {
            callbacks_to_call.push_back(callback_to_call_t{
                .callback = unit->on_change,
                .arg = result,
            });
          }
          unit->value = result;
        }
        xSemaphoreGive(unit->unit_mutex);
      }
      xSemaphoreGive(module->units_mutex);
    }
    for (auto call : callbacks_to_call) {
      call.callback(call.arg);
    }
    callbacks_to_call.clear();
  }
}

handle_t init_unit(config_t &conf) {
  if (module_handle == nullptr) {
    ESP_LOGE(TAG, "Module is not initialized");
    return nullptr;
  }

  if (conf.max_calibrated_value <= 0) {
    ESP_LOGE(TAG, "Invalid unit configuration: max_calibrated_value should be "
                  "greater then 0");
    return nullptr;
  }

  auto module = module_handle.load();
  esp_err_t err;

  adc_oneshot_unit_handle_t adc_handle;
  adc_cali_handle_t calibration_handle;
  adc_unit_t adc_unit;
  adc_channel_t adc_channel;

  err = adc_oneshot_io_to_channel(conf.gpio, &adc_unit, &adc_channel);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Unable to identify ADC UNIT/CHANNEL; Error: %s",
             esp_err_to_name(err));
    return nullptr;
  }

  bool adc_unit_configured = false;
  if (xSemaphoreTake(module->units_mutex,
                     pdMS_TO_TICKS(module->meter_period_ms * 5)) == pdTRUE) {
    for (auto unit : module->units) {
      if (unit->adc_unit == adc_unit && unit->adc_channel == adc_channel) {
        ESP_LOGE(TAG, "GPIO %d is already in use", conf.gpio);
        xSemaphoreGive(module->units_mutex);
        return nullptr;
      } else if (unit->adc_unit == adc_unit) {
        adc_unit_configured = true;
        adc_handle = unit->adc_handle;
      }
    }

    if (!adc_unit_configured) {
      adc_oneshot_unit_init_cfg_t unit_cfg = {
          .unit_id = adc_unit,
          .ulp_mode = ADC_ULP_MODE_DISABLE,
      };
      err = adc_oneshot_new_unit(&unit_cfg, &adc_handle);
      if (err != ESP_OK) {
        ESP_LOGE(TAG, "Unable to configure new adc unit; Error: %s",
                 esp_err_to_name(err));
        xSemaphoreGive(module->units_mutex);
        return nullptr;
      }
    }

    adc_oneshot_chan_cfg_t chan_cfg = {
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };

    err = adc_oneshot_config_channel(adc_handle, adc_channel, &chan_cfg);
    if (err != ESP_OK) {
      ESP_LOGE(TAG, "Unable to configure adc channel; Error: %s",
               esp_err_to_name(err));
      if (!adc_unit_configured) {
        adc_oneshot_del_unit(adc_handle);
      }
      xSemaphoreGive(module->units_mutex);
      return nullptr;
    }

    adc_cali_curve_fitting_config_t calibration_config = {
        .unit_id = adc_unit,
        .chan = adc_channel,
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT};
    err = adc_cali_create_scheme_curve_fitting(&calibration_config,
                                               &calibration_handle);
    if (err != ESP_OK) {
      ESP_LOGE(TAG, "Unable to configure adc calibration; Error: %s",
               esp_err_to_name(err));
      if (!adc_unit_configured) {
        adc_oneshot_del_unit(adc_handle);
      }
      xSemaphoreGive(module->units_mutex);
      return nullptr;
    }

    SemaphoreHandle_t unit_mutex = xSemaphoreCreateMutex();
    if (unit_mutex == nullptr) {
      ESP_LOGE(TAG, "Unable to create unit mutex");
      xSemaphoreGive(module->units_mutex);
      if (!adc_unit_configured) {
        adc_oneshot_del_unit(adc_handle);
      }
      adc_cali_delete_scheme_curve_fitting(calibration_handle);
      return nullptr;
    }

    auto unit = new handle{
        .adc_handle = adc_handle,
        .calibration_handle = calibration_handle,
        .adc_unit = adc_unit,
        .adc_channel = adc_channel,

        .on_change_enabled = conf.on_change_enabled,
        .on_change = conf.on_change,
        .change_threshold = conf.change_threshold,
        .max_calibrated_value = conf.max_calibrated_value,

        .value = -1.0,
        .unit_mutex = unit_mutex,

        .gpio = conf.gpio,
    };

    module->units.push_back(unit);
    xSemaphoreGive(module->units_mutex);
    return unit;
  } else {
    ESP_LOGE(TAG, "Unable to obtain units_mutex");
    return nullptr;
  }
}

void set_on_change_callback(handle_t handle, change_callback_t callback) {
  if (handle == nullptr) {
    ESP_LOGE(TAG, "Handle is null");
    return;
  }
  if (xSemaphoreTake(handle->unit_mutex,
                     pdMS_TO_TICKS(ADC_OPERATION_TIMEOUT_MS)) == pdTRUE) {
    handle->on_change = callback;
    xSemaphoreGive(handle->unit_mutex);
  }
}
void clear_on_change_callback(handle_t handle) {
  if (handle == nullptr) {
    ESP_LOGE(TAG, "Handle is null");
    return;
  }
  if (xSemaphoreTake(handle->unit_mutex,
                     pdMS_TO_TICKS(ADC_OPERATION_TIMEOUT_MS)) == pdTRUE) {
    handle->on_change = nullptr;
    handle->on_change_enabled = false;
    xSemaphoreGive(handle->unit_mutex);
  }
}
void enable_on_change(handle_t handle) {
  if (handle == nullptr) {
    ESP_LOGE(TAG, "Handle is null");
    return;
  }

  if (xSemaphoreTake(handle->unit_mutex,
                     pdMS_TO_TICKS(ADC_OPERATION_TIMEOUT_MS)) == pdTRUE) {
    if (handle->on_change == nullptr) {
      ESP_LOGW(
          TAG,
          "Can't enable 'on change' callback - callback function is not set");
      xSemaphoreGive(handle->unit_mutex);
      return;
    }

    handle->on_change_enabled = true;
    xSemaphoreGive(handle->unit_mutex);
  }
}
void disable_on_change(handle_t handle) {
  if (handle == nullptr) {
    ESP_LOGE(TAG, "Handle is null");
    return;
  }
  if (xSemaphoreTake(handle->unit_mutex,
                     pdMS_TO_TICKS(ADC_OPERATION_TIMEOUT_MS)) == pdTRUE) {
    handle->on_change_enabled = false;
    xSemaphoreGive(handle->unit_mutex);
  }
}

float current_value(handle_t handle) {
  if (handle == nullptr) {
    ESP_LOGE(TAG, "Handle is null");
    return -1.0F;
  }
  if (xSemaphoreTake(handle->unit_mutex,
                     pdMS_TO_TICKS(ADC_OPERATION_TIMEOUT_MS)) == pdTRUE) {
    float result = handle->value;
    xSemaphoreGive(handle->unit_mutex);
    return result;
  } else {
    ESP_LOGW(TAG, "Unable to take unit mutex");
    return -1.0F;
  }
}
} // namespace yva_adc
