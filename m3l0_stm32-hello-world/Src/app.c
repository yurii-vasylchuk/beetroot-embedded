
#include "app.h"
#include "stm32f4xx_hal.h"

void setup() {}

void loop() {
  uint32_t delay;
  if (HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_0)) {
    delay = 200;
  } else {
    delay = 50;
  }
  HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13);
  HAL_Delay(delay);
  HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13);
  HAL_Delay(delay);
}
