// константы команд ADS1220
#define ADS1220_RESET 0b00000110     // Reset the device (0x06)
#define ADS1220_START 0b00001000     // Start or restart conversions (0x08)
#define ADS1220_POWERDOWN 0b00000010 // Enter power-down mode (0x02)
#define ADS1220_RDATA 0b00010000     // Read data by command (0x10)
#define ADS1220_RREG                                                           \
  0b00100000 // Read nn registers starting at address rr - 0010 rrnn
#define ADS1220_WREG                                                           \
  0b01000000 // Write nn registers starting at address rr - 0100 rrnn
#define ADS1220_REG0_PRESS (0b10110000 | 0b01)
#define ADS1220_REG0_TEMP (0b10100000 | 0b01)

#include "main.h"

SPI_HandleTypeDef hspi1;

void ADS1220_SendCommand(uint8_t command) {
  HAL_GPIO_WritePin(SPI1_NSS_GPIO_Port, SPI1_NSS_Pin, GPIO_PIN_RESET);
  HAL_SPI_Transmit(&hspi1, &command, 1, HAL_MAX_DELAY);
  HAL_GPIO_WritePin(SPI1_NSS_GPIO_Port, SPI1_NSS_Pin, GPIO_PIN_SET);
}

void ADS1220_WriteRegisters(uint8_t reg_start, uint8_t *p_data, uint8_t count) {
  uint8_t command = ADS1220_WREG | (reg_start << 2) | (count - 1);
  HAL_GPIO_WritePin(SPI1_NSS_GPIO_Port, SPI1_NSS_Pin, GPIO_PIN_RESET);
  HAL_SPI_Transmit(&hspi1, &command, 1, HAL_MAX_DELAY);
  HAL_SPI_Transmit(&hspi1, p_data, count, HAL_MAX_DELAY);
  HAL_GPIO_WritePin(SPI1_NSS_GPIO_Port, SPI1_NSS_Pin, GPIO_PIN_SET);
}

void ADS1220_Init() {

  ADS1220_SendCommand(ADS1220_RESET);
  HAL_Delay(10);

  uint8_t config[4] = {0};

  // Configuration Register 0 (offset = 00h)
  config[0] = ADS1220_REG0_PRESS; // измеряем по дефолту AIN3 относительно AVSS
                                 // (датчик давления) без усилений

  // Configuration Register 1 (offset = 01h)
  config[1] = 0; // normal mode 20 SPS

  // Configuration Register 2 (offset = 02h)
  config[2] = 0b01 << 6;

  // Configuration Register 3 (offset = 03h)
  config[3] = 0; // data ready pin enabled

  ADS1220_WriteRegisters(0, config, 4);
}