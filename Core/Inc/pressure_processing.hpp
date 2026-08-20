#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"
#include <optional>

#ifdef __cplusplus
}
#endif

class PressureProcess {
public:
  void process(int32_t adc_value_) {
    adcVoltage_ = adcToVoltageMv(adc_value_);
    adcPascals_ = voltageToPascals(adcVoltage_);
    if (isFirst) {
      atmospheric_pascals_ = adcPascals_;
      isFirst = false;
    }
    depthMm_ = pascalsToDepthMm(adcPascals_, atmospheric_pascals_);
  }

  std::optional<uint32_t> getDepthMm() const noexcept {
    if (dataIsValidate) {
      return depthMm_;
    }
    return std::nullopt;
  }
  std::optional<uint32_t> getPressurePascals() const noexcept {
    if (dataIsValidate) {
      return adcPascals_;
    }
    return std::nullopt;
  }
  std::optional<uint32_t> getVoltageADC() const noexcept {
    if (dataIsValidate) {
      return adcVoltage_;
    }
    return std::nullopt;
  }

private:
  bool dataIsValidate = true;
  bool isFirst = true;
  uint32_t atmospheric_pascals_ = 0;
  uint32_t adcVoltage_ = 0;
  uint32_t adcPascals_ = 0;
  uint32_t depthMm_ = 0;

  // константы преобразований для датчика 300 PSI с внешним 24-битным АЦП

  // VREF = 5V (5000 мВ)
  uint32_t VREF_MV = 5000u;

  // максимальное значение 24-битного АЦП (2^23 - 1) с учетом его биполярности
  uint32_t ADC_MAX_24BIT = 8388607u;

  // диапазон измерения: 0...300 PSI
  // 1 PSI = 6894.757 Па
  uint64_t PSI_TO_PASCAL = 6895u;

  uint64_t MAX_PRESSURE_PSI = 300u;
  uint32_t MAX_PRESSURE_PASCAL =
      MAX_PRESSURE_PSI * PSI_TO_PASCAL; // 2,068,500 Па

  // выходной сигнал датчика: 0.5...4.5V
  // 0.5V = 0 PSI, 4.5V = 300 PSI
  uint32_t V_MIN_MV = 500u;                 // 0.5V
  uint32_t V_MAX_MV = 4500u;                // 4.5V
  uint32_t V_SPAN_MV = V_MAX_MV - V_MIN_MV; // 4000 мВ

  // перевод показаний АЦП в милливольты (мВ) без учета атмосферного adc_zero
  uint32_t adcToVoltageMv(int32_t adc_value) noexcept {
    return static_cast<uint32_t>((static_cast<uint64_t>(adc_value) * VREF_MV) /
                                 ADC_MAX_24BIT);
  }

  // перевод напряжения (мВ) в Паскали (Па)
  uint32_t voltageToPascals(uint32_t voltage_mv) noexcept {
    if (voltage_mv >= V_MAX_MV) {
      return MAX_PRESSURE_PASCAL;
    }

    // чистый полезный сигнал (0...4000 мВ)
    uint32_t active_voltage = voltage_mv - V_MIN_MV;

    // паскали = (active_voltage * 300 PSI * 6895 Па) / 4000 мВ
    uint64_t numerator = static_cast<uint64_t>(active_voltage) *
                         MAX_PRESSURE_PSI * PSI_TO_PASCAL;
    uint32_t pascals = static_cast<uint32_t>(numerator / V_SPAN_MV);

    return pascals;
  }

  // перевод паскалей (Па) в глубину (в миллиметрах водного столба)
  // формула: глубина_мм = паскали / (плотность_воды * g)
  uint32_t pascalsToDepthMm(uint32_t current_pascals,
                            uint32_t atmospheric_pascals) noexcept {
    if (current_pascals <= atmospheric_pascals) {
      return 0u;
    }
    // давление воды
    uint32_t water_pascals = current_pascals - atmospheric_pascals;

    uint64_t numerator = static_cast<uint64_t>(water_pascals) * 1000u;
    uint32_t depthMm = static_cast<uint32_t>(numerator / 9800u);
    return depthMm;
  }
};