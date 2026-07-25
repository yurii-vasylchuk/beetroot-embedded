#include "driver/gpio.h"
#include "driver/gptimer.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "soc/gpio_num.h"

#define BTN_GPIO GPIO_NUM_17
#define LIGHT_GPIO GPIO_NUM_16
#define AUTO_INDICATOR_GPIO GPIO_NUM_15
#define SENSOR_GPIO GPIO_NUM_4

#define TIMER_RESOLUTION_HZ 1000000
#define TIMER_PERIOD_US 100000

#define QUEUE_LENGTH 10
#define ILLUMINATION_LEVEL_TRESHOLD 1400
#define BTN_DEBOUNCE_TRESHOLD 50

typedef enum {
  WORK_MODE_ON,
  WORK_MODE_OFF,
  WORK_MODE_AUTO,
} WorkMode;

typedef enum {
  ILLUMINATION_LEVEL_HIGH,
  ILLUMINATION_LEVEL_LOW,
} IlluminationLevel;

static const char *TAG = "M1L5";

static QueueHandle_t btnPressQueueHandle = NULL;
static QueueHandle_t alarmQueueHandle = NULL;
static QueueHandle_t illuminationLevelChangedQueueHandle = NULL;

static gptimer_handle_t timer;
static adc_oneshot_unit_handle_t adcHandle;

static adc_unit_t adcUnit;
static adc_channel_t adcChannel;

static void adcInit(void);
static void timerInit(void);
static void gpioInit(void);
static WorkMode nextWorkMode(WorkMode prev);

bool timerAlarmIsrHandler(gptimer_handle_t timer,
                          const gptimer_alarm_event_data_t *alarm, void *ctx);
void btnIsrHandler(void *arg);

static void handleIlluminationLevelValueTask(void *arg);
static void handleIlluminationLevelTask(void *arg);
static void handleBtnPressTask(void *arg);

static volatile WorkMode workMode = WORK_MODE_ON;

void app_main() {
  gpioInit();
  ESP_LOGI(TAG, "GPIO initialized");
  adcInit();
  ESP_LOGI(TAG, "ADC Initialized");
  timerInit();
  ESP_LOGI(TAG, "Timer initialized");

  btnPressQueueHandle = xQueueCreate(QUEUE_LENGTH, sizeof(uint32_t));
  alarmQueueHandle = xQueueCreate(QUEUE_LENGTH, sizeof(uint32_t));
  illuminationLevelChangedQueueHandle =
      xQueueCreate(QUEUE_LENGTH, sizeof(IlluminationLevel));

  xTaskCreate(handleBtnPressTask, "btn-press-task", 2048, NULL, 5, NULL);
  xTaskCreate(handleIlluminationLevelTask, "handle-illumination-level-task",
              2048, NULL, 5, NULL);
  xTaskCreate(handleIlluminationLevelValueTask,
              "handle-illumination-value-task", 2048, NULL, 5, NULL);

  gpio_install_isr_service(0);
  gpio_isr_handler_add(BTN_GPIO, btnIsrHandler, NULL);

  ESP_ERROR_CHECK(gptimer_start(timer));

  ESP_LOGI(TAG, "INITIALIZATION FINISHED");
}

static void gpioInit(void) {
  gpio_set_direction(LIGHT_GPIO, GPIO_MODE_OUTPUT);
  gpio_set_level(LIGHT_GPIO, 1);

  gpio_set_direction(AUTO_INDICATOR_GPIO, GPIO_MODE_OUTPUT);

  gpio_set_direction(BTN_GPIO, GPIO_MODE_INPUT);
  gpio_set_pull_mode(BTN_GPIO, GPIO_PULLUP_ONLY);
  gpio_set_intr_type(BTN_GPIO, GPIO_INTR_NEGEDGE);
}

static void adcInit(void) {
  ESP_ERROR_CHECK(
      adc_oneshot_io_to_channel(SENSOR_GPIO, &adcUnit, &adcChannel));

  adc_oneshot_unit_init_cfg_t unit_cfg = {.unit_id = adcUnit,
                                          .ulp_mode = ADC_ULP_MODE_DISABLE};

  ESP_ERROR_CHECK(adc_oneshot_new_unit(&unit_cfg, &adcHandle));

  adc_oneshot_chan_cfg_t chanCfg = {
      .bitwidth = ADC_BITWIDTH_DEFAULT,
      .atten = ADC_ATTEN_DB_12,
  };

  ESP_ERROR_CHECK(adc_oneshot_config_channel(adcHandle, adcChannel, &chanCfg));
}

static void timerInit(void) {
  const gptimer_config_t timerConfig = {.clk_src = GPTIMER_CLK_SRC_DEFAULT,
                                        .direction = GPTIMER_COUNT_UP,
                                        .resolution_hz = TIMER_RESOLUTION_HZ};
  ESP_ERROR_CHECK(gptimer_new_timer(&timerConfig, &timer));

  const gptimer_event_callbacks_t callbacks = {.on_alarm =
                                                   timerAlarmIsrHandler};
  ESP_ERROR_CHECK(gptimer_register_event_callbacks(timer, &callbacks, NULL));

  const gptimer_alarm_config_t alarmConfig = {.alarm_count = TIMER_PERIOD_US,
                                              .reload_count = 0,
                                              .flags.auto_reload_on_alarm =
                                                  true};
  ESP_ERROR_CHECK(gptimer_set_alarm_action(timer, &alarmConfig));

  ESP_ERROR_CHECK(gptimer_enable(timer));
}

bool IRAM_ATTR timerAlarmIsrHandler(gptimer_handle_t timer,
                                    const gptimer_alarm_event_data_t *alarm,
                                    void *ctx) {
  uint32_t time = (uint32_t)(esp_timer_get_time() / 1000);

  xQueueSendFromISR(alarmQueueHandle, &time, NULL);
  return true;
}

void IRAM_ATTR btnIsrHandler(void *arg) {
  uint32_t time = (uint32_t)(esp_timer_get_time() / 1000);
  xQueueSendFromISR(btnPressQueueHandle, &time, NULL);
}

static void handleIlluminationLevelTask(void *arg) {
  IlluminationLevel level;
  while (true) {
    if (!xQueueReceive(illuminationLevelChangedQueueHandle, &level,
                       portMAX_DELAY)) {
      continue;
    }

    if (workMode != WORK_MODE_AUTO) {
      continue;
    }

    switch (level) {
    case ILLUMINATION_LEVEL_LOW:
      gpio_set_level(LIGHT_GPIO, 1);
      break;
    case ILLUMINATION_LEVEL_HIGH:
      gpio_set_level(LIGHT_GPIO, 0);
      break;

    default:
      ESP_LOGW(TAG, "UNKNOWN ILLUMINATION LEVEL");
    }
  }
}

static void handleIlluminationLevelValueTask(void *arg) {
  uint32_t time;
  while (true) {
    if (!xQueueReceive(alarmQueueHandle, &time, portMAX_DELAY)) {
      continue;
    }

    int value;
    adc_oneshot_read(adcHandle, adcChannel, &value);

    IlluminationLevel level =
        ((value > ILLUMINATION_LEVEL_TRESHOLD) ? ILLUMINATION_LEVEL_HIGH
                                               : ILLUMINATION_LEVEL_LOW);

    xQueueSend(illuminationLevelChangedQueueHandle, &level, portMAX_DELAY);
  }
}

static void handleBtnPressTask(void *arg) {
  uint32_t prevTime = 0;
  uint32_t time = 0;

  while (true) {
    if (!xQueueReceive(btnPressQueueHandle, &time, portMAX_DELAY)) {
      continue;
    }
    ESP_LOGI(TAG, "RECEIVED Btn press event; time=%dms prev=%dms", time,
             prevTime);
    if (time - prevTime < BTN_DEBOUNCE_TRESHOLD) {
      ESP_LOGI(TAG, "  -- Debounced");
      continue;
    }

    prevTime = time;
    workMode = nextWorkMode(workMode);
    ESP_LOGI(TAG, "Work mode set to: %d", workMode);
    switch (workMode) {
    case WORK_MODE_ON:
      gpio_set_level(AUTO_INDICATOR_GPIO, 0);
      gpio_set_level(LIGHT_GPIO, 1);
      break;

    case WORK_MODE_OFF:
      gpio_set_level(AUTO_INDICATOR_GPIO, 0);
      gpio_set_level(LIGHT_GPIO, 0);
      break;

    case WORK_MODE_AUTO:
      gpio_set_level(AUTO_INDICATOR_GPIO, 1);
      break;
    }
  }
}

static WorkMode nextWorkMode(WorkMode prev) {
  switch (prev) {
  case WORK_MODE_ON:
    return WORK_MODE_OFF;

  case WORK_MODE_OFF:
    return WORK_MODE_AUTO;

  case WORK_MODE_AUTO:
    return WORK_MODE_ON;

  default:
    ESP_LOGW(TAG, "UNKNOWN WORK MODE");
    return WORK_MODE_OFF;
  }
}
