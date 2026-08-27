#pragma once

#include <array>
#include <cstdint>
#include <optional>

#include "main.h"

class ADS1220
{
public:
    enum class Channel : uint8_t
    {
        Pressure    = 0,
        Temperature = 1,
    };

    struct Measurement
    {
        Channel channel;
        int32_t adcValue;
    };

    ADS1220(SPI_HandleTypeDef& spi, GPIO_TypeDef* chipSelectPort, uint16_t chipSelectPin) noexcept;

    [[nodiscard]] bool Init() noexcept;
    [[nodiscard]] bool StartMeasurement(Channel channel) noexcept;
    [[nodiscard]] std::optional<Measurement> ReadMeasurement() noexcept;

    [[nodiscard]] bool IsMeasurementInProgress() const noexcept;
    void OnDataReadyInterrupt() noexcept;

private:
    static constexpr uint8_t kResetCommand         = 0x06;
    static constexpr uint8_t kStartCommand         = 0x08;
    static constexpr uint8_t kReadDataCommand      = 0x10;
    static constexpr uint8_t kWriteRegisterCommand = 0x40;

    // AIN3/AVSS and AIN2/AVSS, gain 1, PGA bypassed
    static constexpr uint8_t kPressureRegister0    = 0xB1;
    static constexpr uint8_t kTemperatureRegister0 = 0xA1;

    static constexpr uint8_t kRegisterCount       = 4;
    static constexpr uint8_t kConversionByteCount = 3;

    SPI_HandleTypeDef& spi_;
    GPIO_TypeDef* chipSelectPort_;
    uint16_t chipSelectPin_;

    volatile bool dataReady_    = false;
    bool measurementInProgress_ = false;
    Channel activeChannel_      = Channel::Pressure;

    bool SendCommand(uint8_t command) noexcept;
    bool WriteRegisters(uint8_t firstRegister, const uint8_t* data, uint8_t count) noexcept;
    std::optional<int32_t> ReadConversion() noexcept;

    void Select() noexcept;
    void Deselect() noexcept;
    static uint8_t Register0For(Channel channel) noexcept;
};
