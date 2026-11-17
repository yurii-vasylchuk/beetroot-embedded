# M4L8 environment logger

This ESP32-S3 project periodically reads temperature, pressure, and relative
humidity from a BME280. Every successful sample is:

- printed to the serial console;
- displayed on an SSD1306 OLED;
- appended to an external W25Q64 SPI flash.

The project does not calculate moving averages, altitude, dew point, or other
derived values. The BME280 driver only applies the factory calibration needed
to convert the sensor's raw readings into degrees Celsius, hPa, and percent.

## Runtime structure

```text
                     +--> console
                     |
BME280 -- sensor_task+--> flash_queue --> flash_task --> W25Q64 / SPI
    |                |
    |                +--> display_queue --> display_task --> SSD1306 / I2C
    |
    +---------------- shared I2C bus ----------------------+
```

There are three application tasks:

- `sensor_task` starts a forced BME280 measurement, reads all three values,
  prints them, and sends the sample to both queues;
- `flash_task` appends samples to the external flash;
- `display_task` redraws the temperature, pressure, and humidity for every
  received sample.

`sensor_task` waits for `SENSOR_DELAY_MS` after each read cycle using
`vTaskDelay`. Therefore the interval between measurements is the configured
delay plus the time spent reading the sensor and sending the sample.

## Wiring

The BME280 and SSD1306 share the same I2C bus.

| Peripheral | Signal | ESP32-S3 GPIO | Notes |
| --- | --- | ---: | --- |
| BME280 | SDA | 10 | I2C address `0x76` |
| BME280 | SCL | 9 | 400 kHz |
| SSD1306 | SDA | 10 | I2C address `0x3C` |
| SSD1306 | SCL | 9 | 400 kHz |
| W25Q64 | MOSI / DI | 11 | SPI2 |
| W25Q64 | MISO / DO | 13 | SPI2 |
| W25Q64 | SCK / CLK | 12 | 4 MHz |
| W25Q64 | CS | 14 | Active low |

Power the BME280, SSD1306, and W25Q64JV from 3.3 V and connect all grounds.
Use external I2C pull-ups when they are not already present on the sensor and
display modules. If the BME280 SDO pin selects address `0x77`, update
`BME280_I2C_ADDRESS` in `include/app_config.h`.

## Configuration

Hardware pins and runtime constants are defined in `include/app_config.h`:

| Constant | Current value | Purpose |
| --- | ---: | --- |
| `I2C_CLOCK_HZ` | 400000 | BME280 I2C clock |
| `BME280_I2C_ADDRESS` | `0x76` | BME280 address |
| `FLASH_SPI_CLOCK_HZ` | 4000000 | External flash SPI clock |
| `SENSOR_DELAY_MS` | 1000 | Delay after every sensor cycle |
| `FLASH_QUEUE_LENGTH` | 16 | Pending samples for Flash |
| `DISPLAY_QUEUE_LENGTH` | 1 | Pending samples for the display |

Both queue sends wait indefinitely when their consumer is busy. This ensures
that a successful sample is not intentionally dropped.

## Flash storage

The external flash driver uses standard 3-byte SPI commands and validates the
JEDEC capacity reported by the chip. W25Q64 provides 8 MiB and is supported.
SPI DMA is disabled, so transfers are split into chunks of at most 60 data
bytes plus the command and 3-byte address.

Each log record occupies 32 bytes:

| Field | Type | Meaning |
| --- | --- | --- |
| `uptime_ms` | `int64_t` | Milliseconds since the current boot |
| `temperature_c` | `float` | Temperature in degrees Celsius |
| `pressure_hpa` | `float` | Pressure in hPa |
| `humidity_percent` | `float` | Relative humidity in percent |
| `padding` | 12 bytes | Makes the record size divide 4 KiB exactly |

A physical 4 KiB erase sector holds 128 records. With one record per second,
the complete 8 MiB W25Q64 holds 262144 records, or about 72.8 hours of data.

The storage format is intentionally simple:

- there are no magic values, format versions, record headers, or CRC fields;
- startup searches for the first 32-byte slot containing only `0xFF`;
- a physical 4 KiB sector is erased before its first slot is reused;
- after reaching the end of the chip, writing continues from address zero.

This simplicity has trade-offs. The firmware cannot distinguish old formats,
partially written records, or corrupted data from valid records. If no erased
slot exists at startup, it resumes from address zero. Erase the external flash
once when switching from an older record format. The project currently has no
command for exporting or decoding stored records.

## Build and run

```sh
pio run
pio run -t upload
pio device monitor
```

The serial monitor runs at 115200 baud. If initialization fails, `app_main`
logs the error and restarts the ESP32-S3 after one second.
