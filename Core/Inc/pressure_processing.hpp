#pragma once

#include <cstdint>
#include <optional>

class PressureProcessor
{
public:
    bool Process(int32_t adcValue) noexcept
    {
        if (adcValue < 0 or adcValue > kAdcMaximum) {
            InvalidateMeasurement();
            return false;
        }

        const uint32_t voltageMillivolts = AdcToMillivolts(static_cast<uint32_t>(adcValue));
        const uint32_t pressurePascals   = MillivoltsToPascals(voltageMillivolts);

        if (not atmosphericPressurePascals_.has_value()) {
            atmosphericPressurePascals_ = pressurePascals;
        }

        voltageMillivolts_ = voltageMillivolts;
        pressurePascals_   = pressurePascals;
        depthMillimeters_  = PascalsToDepthMillimeters(pressurePascals, atmosphericPressurePascals_.value());
        return true;
    }

    [[nodiscard]] std::optional<uint32_t> DepthMillimeters() const noexcept
    {
        return depthMillimeters_;
    }

    [[nodiscard]] std::optional<uint32_t> PressurePascals() const noexcept
    {
        return pressurePascals_;
    }

    [[nodiscard]] std::optional<uint32_t> VoltageMillivolts() const noexcept
    {
        return voltageMillivolts_;
    }

private:
    // Transfer function for a 0.5...4.5 V, 0...300 PSI pressure sensor
    static constexpr uint32_t kReferenceVoltageMillivolts = 5000;
    static constexpr int32_t kAdcMaximum                  = 8388607;
    static constexpr uint32_t kSensorMinimumMillivolts    = 500;
    static constexpr uint32_t kSensorMaximumMillivolts    = 4500;
    static constexpr uint32_t kSensorSpanMillivolts       = kSensorMaximumMillivolts - kSensorMinimumMillivolts;
    static constexpr uint32_t kMaximumPressurePsi         = 300;
    static constexpr uint32_t kPascalsPerPsi              = 6895;
    static constexpr uint32_t kMaximumPressurePascals     = kMaximumPressurePsi * kPascalsPerPsi;
    static constexpr uint32_t kWaterDensityTimesGravity   = 9800;

    std::optional<uint32_t> atmosphericPressurePascals_;
    std::optional<uint32_t> voltageMillivolts_;
    std::optional<uint32_t> pressurePascals_;
    std::optional<uint32_t> depthMillimeters_;

    void InvalidateMeasurement() noexcept
    {
        voltageMillivolts_.reset();
        pressurePascals_.reset();
        depthMillimeters_.reset();
    }

    static uint32_t AdcToMillivolts(uint32_t adcValue) noexcept
    {
        const uint64_t scaledVoltage = static_cast<uint64_t>(adcValue) * kReferenceVoltageMillivolts;
        return static_cast<uint32_t>(scaledVoltage / static_cast<uint32_t>(kAdcMaximum));
    }

    static uint32_t MillivoltsToPascals(uint32_t voltageMillivolts) noexcept
    {
        if (voltageMillivolts <= kSensorMinimumMillivolts) {
            return 0;
        }

        if (voltageMillivolts >= kSensorMaximumMillivolts) {
            return kMaximumPressurePascals;
        }

        const uint32_t activeVoltage  = voltageMillivolts - kSensorMinimumMillivolts;
        const uint64_t scaledPressure = static_cast<uint64_t>(activeVoltage) * kMaximumPressurePascals;
        return static_cast<uint32_t>(scaledPressure / kSensorSpanMillivolts);
    }

    static uint32_t PascalsToDepthMillimeters(uint32_t pressurePascals, uint32_t atmosphericPressurePascals) noexcept
    {
        if (pressurePascals <= atmosphericPressurePascals) {
            return 0;
        }

        const uint32_t waterPressurePascals = pressurePascals - atmosphericPressurePascals;
        return static_cast<uint32_t>(static_cast<uint64_t>(waterPressurePascals) * 1000 / kWaterDensityTimesGravity);
    }
};
