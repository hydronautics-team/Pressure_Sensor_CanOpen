#pragma once

#include "main.h"
#include <optional>

SPI_HandleTypeDef hspi1;
static void MX_SPI1_Init(void) {
  /* SPI1 parameter configuration*/
  hspi1.Instance = SPI1;
  hspi1.Init.Mode = SPI_MODE_MASTER;
  hspi1.Init.Direction = SPI_DIRECTION_2LINES;
  hspi1.Init.DataSize = SPI_DATASIZE_8BIT;
  hspi1.Init.CLKPolarity = SPI_POLARITY_LOW;
  hspi1.Init.CLKPhase = SPI_PHASE_2EDGE;
  hspi1.Init.NSS = SPI_NSS_SOFT;
  hspi1.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_32;
  hspi1.Init.FirstBit = SPI_FIRSTBIT_MSB;
  hspi1.Init.TIMode = SPI_TIMODE_DISABLE;
  hspi1.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
  hspi1.Init.CRCPolynomial = 10;
  if (HAL_SPI_Init(&hspi1) != HAL_OK) {
    Error_Handler();
  }
}

class ADS1220 {
private:
  void SendCommand(uint8_t command) {
    HAL_GPIO_WritePin(SPI1_NSS_GPIO_Port, SPI1_NSS_Pin, GPIO_PIN_RESET);
    status = HAL_SPI_Transmit(&hspi1, &command, 1, HAL_MAX_DELAY);
    HAL_GPIO_WritePin(SPI1_NSS_GPIO_Port, SPI1_NSS_Pin, GPIO_PIN_SET);

    if (status != HAL_OK) {
      dataIsValidate = false;
    } else {
      dataIsValidate = true;
    }
  }
  void WriteRegisters(uint8_t reg_start, const uint8_t *p_data, uint8_t count) {
    uint8_t command = ADS1220_WREG | (reg_start << 2) | (count - 1);
    HAL_GPIO_WritePin(SPI1_NSS_GPIO_Port, SPI1_NSS_Pin, GPIO_PIN_RESET);
    HAL_SPI_Transmit(&hspi1, &command, 1, HAL_MAX_DELAY);
    status = HAL_SPI_Transmit(&hspi1, p_data, count, HAL_MAX_DELAY);
    HAL_GPIO_WritePin(SPI1_NSS_GPIO_Port, SPI1_NSS_Pin, GPIO_PIN_SET);

    if (status != HAL_OK) {
      dataIsValidate = false;
    } else {
      dataIsValidate = true;
    }
  }

  // константы команд ADS1220
  uint8_t ADS1220_RESET = 0b00000110;     // Reset the device (0x06)
  uint8_t ADS1220_START = 0b00001000;     // Start or restart conversions (0x08)
  uint8_t ADS1220_POWERDOWN = 0b00000010; // Enter power-down mode (0x02)
  uint8_t ADS1220_RDATA = 0b00010000;     // Read data by command (0x10)
  uint8_t ADS1220_RREG =
      0b00100000; // Read nn registers starting at address rr - 0010 rrnn
  uint8_t ADS1220_WREG =
      0b01000000; // Write nn registers starting at address rr - 0100 rrnn
  uint8_t ADS1220_REG0_PRESS =
      0b10110000 | 0b01; // конфиг измерения канала датчика давления
  uint8_t ADS1220_REG0_TEMP =
      0b10100000 | 0b01; // конфиг измерения канала датчика температуры

  HAL_StatusTypeDef status = HAL_OK;
  uint8_t spi_buffer[3] = {0, 0, 0};
  int32_t adc_value_ = 0;
  bool dataIsValidate = true;

public:
  void Init() {

    SendCommand(ADS1220_RESET);
    HAL_Delay(10);

    uint8_t config[4] = {0};

    // Configuration Register 0 (offset = 00h)
    config[0] = ADS1220_REG0_PRESS; // измеряем по дефолту AIN3
                                    // относительно AVSS (датчик
                                    // давления) без усилений

    // Configuration Register 1 (offset = 01h)
    config[1] = 0; // normal mode 20 SPS

    // Configuration Register 2 (offset = 02h)
    config[2] = 0b01 << 6;

    // Configuration Register 3 (offset = 03h)
    config[3] = 0; // data ready pin enabled

    WriteRegisters(0, config, 4);
  }

  int32_t PressureMeasurementProcess() {
    /*
     * ПОЛУЧЕНИЕ ЗНАЧЕНИЙ С ДАТЧИКА
     */
    // выбираем преобразование канала датчика давления
    WriteRegisters(0, &ADS1220_REG0_PRESS, 1);
    // запускаем одиночное преобразование
    SendCommand(ADS1220_START);
    // ждем, пока DRDY упадет в 0 (преобразование готово)
    while (HAL_GPIO_ReadPin(DATA_READY_GPIO_Port, DATA_READY_Pin) ==
           GPIO_PIN_SET) {
    }

    // очищаем буфер для чтения даты
    spi_buffer[0] = 0;
    spi_buffer[1] = 0;
    spi_buffer[2] = 0;

    // читаем дату
    HAL_GPIO_WritePin(SPI1_NSS_GPIO_Port, SPI1_NSS_Pin, GPIO_PIN_RESET);
    HAL_SPI_Transmit(&hspi1, &ADS1220_RDATA, 1, HAL_MAX_DELAY);
    status = HAL_SPI_Receive(&hspi1, spi_buffer, 3, HAL_MAX_DELAY);
    HAL_GPIO_WritePin(SPI1_NSS_GPIO_Port, SPI1_NSS_Pin, GPIO_PIN_SET);

    if (status != HAL_OK) {
      dataIsValidate = false;
    } else {
      dataIsValidate = true;
    }

    adc_value_ = ((int32_t)spi_buffer[0] << 16) |
                 ((int32_t)spi_buffer[1] << 8) | spi_buffer[2];

    // проверка на отрицательное число на всякий случай
    if (adc_value_ & 0x00800000) {
      adc_value_ = 0;
      dataIsValidate = false;
    }

    return adc_value_;
  }

  int32_t TemperatureMeasurementProcess() {
    // выбираем преобразование канала датчика давления
    WriteRegisters(0, &ADS1220_REG0_TEMP, 1);
    // запускаем одиночное преобразование
    SendCommand(ADS1220_START);
    // ждем, пока DRDY упадет в 0 (преобразование готово)
    while (HAL_GPIO_ReadPin(DATA_READY_GPIO_Port, DATA_READY_Pin) ==
           GPIO_PIN_SET) {
    }

    // очищаем буфер для чтения даты
    spi_buffer[0] = 0;
    spi_buffer[1] = 0;
    spi_buffer[2] = 0;

    // читаем дату
    HAL_GPIO_WritePin(SPI1_NSS_GPIO_Port, SPI1_NSS_Pin, GPIO_PIN_RESET);
    HAL_SPI_Transmit(&hspi1, &ADS1220_RDATA, 1, HAL_MAX_DELAY);
    HAL_SPI_Receive(&hspi1, spi_buffer, 3, HAL_MAX_DELAY);
    HAL_GPIO_WritePin(SPI1_NSS_GPIO_Port, SPI1_NSS_Pin, GPIO_PIN_SET);

    adc_value_ = ((int32_t)spi_buffer[0] << 16) |
                 ((int32_t)spi_buffer[1] << 8) | spi_buffer[2];

    // проверка на отрицательное число на всякий случай
    if (adc_value_ & 0x00800000) {
      adc_value_ = 0;
    }

    return adc_value_;
  }

  std::optional<int32_t> getADCvalue() const noexcept {
    if (dataIsValidate) {
      return adc_value_;
    }
    return std::nullopt;
  };
};