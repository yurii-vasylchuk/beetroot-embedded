#include "ssd1306_driver.h"
#include "bitmap_symbols.h"
#include "esp_log.h"
#include <iterator>

using namespace bitmap_symbols;

namespace ssd1306 {

static constexpr uint16_t FRAMEBUFFER_SIZE = 1024u;
static constexpr uint8_t COMMAND_MODE = 0x00;
static constexpr uint8_t DATA_MODE = 0x40;

enum class command_t : uint8_t {
  DISPLAY_OFF = 0xAE,
  DISPLAY_ON = 0xAF,

  DISPLAY_CLOCK = 0xD5,

  MULTIPLEX_RATIO = 0xA8,

  DISPLAY_OFFSET = 0xD3,

  START_LINE_0 = 0x40,

  CHARGE_PUMP = 0x8D,
  CHARGE_PUMP_ENABLE = 0x14,
  CHARGE_PUMP_DISABLE = 0x10,

  MEMORY_ADDRESING_MODE = 0x20,
  MEMORY_ADDRESING_MODE_HORIZONTAL = 0x00,
  MEMORY_ADDRESING_MODE_VERTICAL = 0x01,
  MEMORY_ADDRESING_MODE_PAGE = 0x02,

  SEGMENT_MAPPING_NORMAL = 0xA0,
  SEGMENT_MAPPING_REMAPPED = 0xA1,

  COM_SCAN_DIRECTION_NORMAL = 0xC0,
  COM_SCAN_DIRECTION_REMAPPED = 0xC8,

  COM_PINS_CONFIGURATION = 0xDA,

  CONTRAST = 0x81,

  PRE_CHARGE_PERIOD = 0xD9,

  VCOMH_DESELECT_LEVEL = 0xDB,

  ENTIRE_DISPLAY_OFF = 0xA4,
  ENTIRE_DISPLAY_ON = 0xA5,

  NORMAL_DISPLAY = 0xA6,
  INVERSE_DISPLAY = 0xA7,

  SET_COLUMN_ADDRESS = 0x21,
  SET_PAGE_ADDRESS = 0x22,
};

static constexpr const char *TAG = "SSD1306";
static constexpr uint8_t SSD1306_I2C_ADDR = 0x3C;
static constexpr uint16_t WIDTH = 128;
static constexpr uint16_t HEIGHT = 64;
static constexpr uint8_t LETTER_WIDTH = 8;
static constexpr uint8_t LETTER_HEIGHT = 8;

struct ssd1306 {
  i2c_master_dev_handle_t dev;
  uint8_t *framebuffer;
};

struct frame_coord_t {
  uint16_t page;
  uint8_t bit;
};

uint8_t u8(command_t);
frame_coord_t get_coord(uint16_t x, uint16_t y);
const uint8_t *find_char_bitmap(const char c);

handle_t init(config_t &config) {
  esp_err_t err;

  const uint8_t init_commands[] = {
      u8(command_t::DISPLAY_OFF),

      u8(command_t::DISPLAY_CLOCK),
      0x80,

      u8(command_t::MULTIPLEX_RATIO),
      0x3F,

      u8(command_t::DISPLAY_OFFSET),
      0x00,

      u8(command_t::START_LINE_0),

      u8(command_t::CHARGE_PUMP),
      u8(command_t::CHARGE_PUMP_ENABLE),

      u8(command_t::MEMORY_ADDRESING_MODE),
      u8(command_t::MEMORY_ADDRESING_MODE_HORIZONTAL),

      u8(command_t::SEGMENT_MAPPING_REMAPPED),

      u8(command_t::COM_SCAN_DIRECTION_REMAPPED),

      u8(command_t::COM_PINS_CONFIGURATION),
      0x12,

      u8(command_t::CONTRAST),
      0x7F,

      u8(command_t::PRE_CHARGE_PERIOD),
      0xF1,

      u8(command_t::VCOMH_DESELECT_LEVEL),
      0x40,

      u8(command_t::ENTIRE_DISPLAY_OFF),
      // u8(command_t::ENTIRE_DISPLAY_ON),

      u8(command_t::NORMAL_DISPLAY),

      u8(command_t::DISPLAY_ON),
  };

  err = i2c_master_probe(config.bus, SSD1306_I2C_ADDR, 30);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Unable to init SSD1306: device is not responding to probe");
    return nullptr;
  }

  i2c_device_config_t device_config = {
      .dev_addr_length = I2C_ADDR_BIT_LEN_7,
      .device_address = SSD1306_I2C_ADDR,
      .scl_speed_hz = 400'000,
  };

  i2c_master_dev_handle_t device;
  err = i2c_master_bus_add_device(config.bus, &device_config, &device);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Unable to init SSD1306: %s", esp_err_to_name(err));
    return nullptr;
  }

  i2c_master_transmit_multi_buffer_info_t multi_buffer[] = {
      {
          .write_buffer = &COMMAND_MODE,
          .buffer_size = 1,
      },
      i2c_master_transmit_multi_buffer_info_t{
          .write_buffer = init_commands,
          .buffer_size = std::size(init_commands),
      }};

  err = i2c_master_multi_buffer_transmit(device, multi_buffer,
                                         std::size(multi_buffer), 50);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Unable to init SSD1306: %s", esp_err_to_name(err));
    i2c_master_bus_rm_device(device);
    return nullptr;
  }

  uint8_t *framebuffer = new uint8_t[FRAMEBUFFER_SIZE]{};
  return new ssd1306{
      .dev = device,
      .framebuffer = framebuffer,
  };
}

void update(handle_t handle) {
  if (handle == nullptr) {
    ESP_LOGE(TAG, "Unable to update: handle arg is null");
    return;
  }

  esp_err_t err;
  static const uint8_t address_commands[] = {
      COMMAND_MODE,

      u8(command_t::SET_COLUMN_ADDRESS),
      0x00,
      WIDTH - 1,

      u8(command_t::SET_PAGE_ADDRESS),
      0x00,
      HEIGHT / 8 - 1,
  };

  err = i2c_master_transmit(handle->dev, address_commands,
                            sizeof(address_commands), 100);

  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Unable to update: sending address commands failed; %s",
             esp_err_to_name(err));
    return;
  }

  i2c_master_transmit_multi_buffer_info_t multi_buffer[] = {
      {
          .write_buffer = &DATA_MODE,
          .buffer_size = 1,
      },
      i2c_master_transmit_multi_buffer_info_t{
          .write_buffer = handle->framebuffer,
          .buffer_size = FRAMEBUFFER_SIZE,
      }};

  err = i2c_master_multi_buffer_transmit(handle->dev, multi_buffer,
                                         std::size(multi_buffer), 50);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Unable to update: sending frame failed; %s",
             esp_err_to_name(err));
  }
}

void dot(handle_t handle, uint16_t x, uint16_t y) {
  if (handle == nullptr) {
    ESP_LOGE(TAG, "Unable to draw dot: handle arg is null");
    return;
  }

  if (x >= WIDTH || y >= HEIGHT) {
    ESP_LOGE(TAG, "Unable to draw dot: coordinates out of bounds: x=%u, y=%u",
             x, y);
    return;
  }

  auto coord = get_coord(x, y);

  handle->framebuffer[coord.page] |= 1u << coord.bit;
}

void clear_dot(handle_t handle, uint16_t x, uint16_t y) {
  if (handle == nullptr) {
    ESP_LOGE(TAG, "Unable to clear dot: handle arg is null");
    return;
  }

  auto coord = get_coord(x, y);

  handle->framebuffer[coord.page] &= ~(1u << coord.bit);
}

void text(handle_t handle, uint16_t x_offset, uint16_t y_offset, char *text) {
  if (handle == nullptr) {
    ESP_LOGE(TAG, "Unable to draw text: handle arg is null");
    return;
  }
  auto text_length = strlen(text);

  if (x_offset + text_length * LETTER_WIDTH > WIDTH ||
      y_offset + LETTER_HEIGHT > HEIGHT) {
    ESP_LOGE(TAG, "Unable to draw text: text is too long");
    return;
  }

  for (int i = 0; i < text_length; i++) {
    const uint8_t *bitmap = find_char_bitmap(text[i]);

    for (uint8_t x = 0; x < 8; x++) {
      uint8_t column = bitmap[x];

      for (uint8_t y = 0; y < 8; y++) {
        if ((column & BIT(y)) == 0) {
          clear_dot(handle, x_offset + x + i * 8, y_offset + y);
        } else {
          dot(handle, x_offset + x + i * 8, y_offset + y);
        }
      }
    }
  }
}

void clear(handle_t handle) {
  if (handle == nullptr) {
    ESP_LOGE(TAG, "Unable to clear: handle arg is null");
    return;
  }

  memset(handle->framebuffer, 0, FRAMEBUFFER_SIZE);
}

// PRIVATE
const uint8_t *find_char_bitmap(const char c) {
  for (int i = 0; i < std::size(CHARS); i++) {
    if (c == SUPPORTED_CHARS[i]) {
      return CHARS[i];
    }
  }

  return UNKNOWN_CHARACTER;
}

frame_coord_t get_coord(uint16_t x, uint16_t y) {
  return {
      .page = static_cast<uint16_t>((y / 8) * 128 + x),
      .bit = static_cast<uint8_t>(y % 8),
  };
}

uint8_t u8(command_t cmd) { return static_cast<uint8_t>(cmd); }

} // namespace ssd1306
