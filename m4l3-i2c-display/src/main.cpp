
#include "driver/gpio.h"
#include "ds1307_driver.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "ssd1306_driver.h"

static const char *TAG = "M4L3";

static constexpr gpio_num_t I2C_SDA_GPIO = GPIO_NUM_10;
static constexpr gpio_num_t I2C_SCL_GPIO = GPIO_NUM_9;

i2c_master_bus_handle_t i2c_bus;

void test_bitmap_symbols(ssd1306::handle_t handle);

extern "C" void app_main() {
  vTaskDelay(pdMS_TO_TICKS(2000));
  i2c_master_bus_config_t i2c_config = {
      .i2c_port = I2C_NUM_0,
      .sda_io_num = I2C_SDA_GPIO,
      .scl_io_num = I2C_SCL_GPIO,
      .clk_source = I2C_CLK_SRC_DEFAULT,
      .glitch_ignore_cnt = 7,
      .flags =
          {
              .enable_internal_pullup = true,
          },
  };

  ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_config, &i2c_bus));

  ds1307::config_t ds1307_config = {
      .initial_date =
          {
              .seconds = 30,
              .minutes = 59,
              .hours = 23,
              .day_of_month = 20,
              .month = 4,
              .year = 93,
              .day_of_week = ds1307::day_of_week_t::Tue,
          },
  };

  auto rtc = ds1307::init(i2c_bus, &ds1307_config);

  ssd1306::config_t ssd1306_config = {
      .bus = i2c_bus,
  };
  auto ssd1306 = ssd1306::init(ssd1306_config);

  test_bitmap_symbols(ssd1306);

  vTaskDelay(pdMS_TO_TICKS(5000));

  ssd1306::clear(ssd1306);

  while (true) {
    auto date = ds1307::now(rtc);

    char date_str[9];
    snprintf(date_str, 9, "%02d-%02d-%02d", date.year, date.month,
             date.day_of_month);
    char time_str[9];
    snprintf(time_str, 9, "%02d:%02d:%02d", date.hours, date.minutes,
             date.seconds);

    ESP_LOGI(TAG, "%sT%s", date_str, time_str);
    ssd1306::text(ssd1306, 5, 5, date_str);
    ssd1306::text(ssd1306, 5, 14, time_str);
    ssd1306::update(ssd1306);

    vTaskDelay(pdMS_TO_TICKS(500));
  }
}

void test_bitmap_symbols(ssd1306::handle_t handle) {
  char uppercase[] = "ABCDEFGHIJKLMNOP";
  char uppercase2[] = "QRSTUVWXYZ";

  char lowercase[] = "abcdefghijklmnop";
  char lowercase2[] = "qrstuvwxyz";

  char digits[] = "0123456789";

  char special1[] = "!? .:-|";
  char special2[] = "\\/#()[]";

  ssd1306::text(handle, 0, 0, uppercase);
  ssd1306::text(handle, 0, 8, uppercase2);

  ssd1306::text(handle, 0, 16, lowercase);
  ssd1306::text(handle, 0, 24, lowercase2);

  ssd1306::text(handle, 0, 32, digits);

  ssd1306::text(handle, 0, 40, special1);
  ssd1306::text(handle, 0, 48, special2);
  ssd1306::update(handle);
}
