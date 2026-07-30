#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"

constexpr gpio_num_t LED_G_GPIO = GPIO_NUM_16;
constexpr gpio_num_t LED_Y_GPIO = GPIO_NUM_17;
constexpr gpio_num_t LED_R_GPIO = GPIO_NUM_18;

constexpr gpio_num_t BTN_GPIO = GPIO_NUM_6;

constexpr uint8_t blink_delays_count = 3;
constexpr uint16_t blink_delays[blink_delays_count] = {250, 500, 1000};

constexpr uint32_t btn_debounce_time_ms = 50;

static SemaphoreHandle_t button_semaphore = NULL;
static const char *TAG = "M2L1";

typedef enum {
  LED_STATE_OFF = 0,
  LED_STATE_ON = 1,
} LedState;

typedef enum {
  LED_MODE_ON,
  LED_MODE_OFF,
  LED_MODE_BLINK,
} LedMode;

class Led {
private:
  LedState state;
  gpio_num_t gpio;
  uint32_t delay;
  uint32_t last_tick;
  LedMode mode;

  void setState(LedState state) {
    this->state = state;
    switch (state) {
    case LED_STATE_ON:
      gpio_set_level(gpio, 1);
      break;
    case LED_STATE_OFF:
      gpio_set_level(gpio, 0);
      break;
    }
  }

public:
  Led(gpio_num_t gpio, uint32_t delay) {
    this->gpio = gpio;
    this->delay = delay;

    this->last_tick = 0;
    this->state = LED_STATE_OFF;
    this->mode = LED_MODE_OFF;
  }

  void init() {
    gpio_config_t config = {
        .pin_bit_mask = BIT64(gpio),
        .mode = GPIO_MODE_OUTPUT,
    };

    ESP_ERROR_CHECK(gpio_config(&config));
    this->setState(this->state);
  }

  void nextMode() {
    switch (this->mode) {
    case LED_MODE_ON:
      ESP_LOGI(TAG, "MODE SET TO OFF");
      this->mode = LED_MODE_OFF;
      this->setState(LED_STATE_OFF);
      break;
    case LED_MODE_OFF:
      ESP_LOGI(TAG, "MODE SET TO BLINK");
      this->mode = LED_MODE_BLINK;
      this->last_tick = esp_timer_get_time() / 1000;
      this->setState(LED_STATE_OFF);
      break;
    case LED_MODE_BLINK:
      ESP_LOGI(TAG, "MODE SET TO ON");
      this->mode = LED_MODE_ON;
      this->setState(LED_STATE_ON);
      break;
    }
  }

  void blink() {
    this->setState(this->state == LED_STATE_ON ? LED_STATE_OFF : LED_STATE_ON);
  }

  void tick() {
    if (this->mode != LED_MODE_BLINK) {
      return;
    }

    uint32_t now = esp_timer_get_time() / 1000;
    if ((now - this->last_tick) > this->delay) {
      this->last_tick += this->delay;
      this->blink();
    }
  }
};

typedef struct {
  Led *green;
  Led *yellow;
  Led *red;
} Leds;

static void btn_isr_handler(void *arg) {
  BaseType_t higher_priority_task_woken = pdFALSE;

  xSemaphoreGiveFromISR(button_semaphore, &higher_priority_task_woken);

  portYIELD_FROM_ISR(higher_priority_task_woken);
}

static void button_processing_task(void *arg) {
  Leds *leds = (Leds *)arg;
  while (true) {
    if (xSemaphoreTake(button_semaphore, portMAX_DELAY) != pdTRUE) {
      continue;
    }

    vTaskDelay(pdMS_TO_TICKS(btn_debounce_time_ms));
    xSemaphoreTake(button_semaphore, 1);
    ESP_LOGI(TAG, "Button press identified");
    leds->red->nextMode();
    leds->yellow->nextMode();
    leds->green->nextMode();
  }
}

extern "C" void app_main() {

  Led red = Led(LED_R_GPIO, blink_delays[0]);
  Led yellow = Led(LED_Y_GPIO, blink_delays[1]);
  Led green = Led(LED_G_GPIO, blink_delays[2]);

  Leds leds = {
      .green = &green,
      .yellow = &yellow,
      .red = &red,
  };

  button_semaphore = xSemaphoreCreateBinary();

  gpio_config_t btn_config = {
      .pin_bit_mask = BIT64(BTN_GPIO),
      .mode = GPIO_MODE_INPUT,
      .pull_up_en = GPIO_PULLUP_ENABLE,
      .pull_down_en = GPIO_PULLDOWN_DISABLE,
      .intr_type = GPIO_INTR_NEGEDGE,
  };

  gpio_config(&btn_config);

  gpio_install_isr_service(0);
  gpio_isr_handler_add(BTN_GPIO, btn_isr_handler, nullptr);

  xTaskCreate(button_processing_task, "button_processing_task", 2048,
              (void *)&leds, 5, NULL);

  gpio_intr_enable(BTN_GPIO);

  red.init();
  yellow.init();
  green.init();

  uint16_t loops_count = 0;
  uint64_t start_time = esp_timer_get_time();
  while (true) {
    red.tick();
    yellow.tick();
    green.tick();

    if (loops_count < 1000) {
      loops_count++;
    } else {
      loops_count = 0;
      uint32_t time = (esp_timer_get_time() - start_time) / 1000;
      ESP_LOGI(TAG, "Time for 1000 loops = %dms; Average loop time = %dms",
               time, time / 1000);
      start_time = esp_timer_get_time(); // Get current time again as log may
                                         // take some time
    }

    vTaskDelay(1);
  }
}
