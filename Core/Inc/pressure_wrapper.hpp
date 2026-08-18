#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

#ifdef __cplusplus
}
#endif

#include <array>
#include <atomic>
#include <cstdint>

/**
 *  Константы преобразований для датчика 300 PSI с внешним 24-битным АЦП (VREF = 5V)
 */
namespace PressureConversions {

    // VREF = 5V (5000 мВ)
    constexpr uint32_t VREF_MV = 5000u;
    
    // Максимальное значение 24-битного АЦП (2^24 - 1)
    constexpr uint32_t ADC_MAX_24BIT = 16777215u;

    // Диапазон измерения: 0...300 PSI
    // 1 PSI = 6894.757 Па
    constexpr uint32_t PSI_TO_PASCAL = 6895u;
    
    constexpr uint32_t MAX_PRESSURE_PSI = 300u;
    constexpr uint32_t MAX_PRESSURE_PASCAL = MAX_PRESSURE_PSI * PSI_TO_PASCAL; // 2,068,500 Па
    
    // Выходной сигнал датчика: 0.5...4.5V
    // 0.5V = 0 PSI, 4.5V = 300 PSI
    constexpr uint32_t V_MIN_MV = 0u;   // 0.5V
    constexpr uint32_t V_MAX_MV = 4000u;  // 4.5V
    constexpr uint32_t V_SPAN_MV = V_MAX_MV - V_MIN_MV; // 4000 мВ

    /**
     * 1. Перевод "попугаев" 24-битного АЦП в милливольты (мВ) без учета атмосферного adc_zero
     */
    constexpr uint32_t adcToVoltageMv(uint32_t adc_value, uint32_t adc_zero) noexcept {
        if (adc_value <= adc_zero) {
            return 0u;
        }
        return static_cast<uint32_t>((static_cast<uint64_t>(adc_value - adc_zero) * VREF_MV) / ADC_MAX_24BIT);
    }

    /**
     * 2. Перевод напряжения (мВ) в Паскали (Па)
     */
    constexpr uint32_t voltageToPascals(uint32_t voltage_mv) noexcept {
        if (voltage_mv >= V_MAX_MV) {
            return MAX_PRESSURE_PASCAL;
        }

        // Чистый полезный сигнал (0...4000 мВ)
        uint32_t active_voltage = voltage_mv - V_MIN_MV;

        // Идеальная физическая формула: Паскали = (V_active * 300 PSI * 6895 Па) / 4000 мВ
        uint64_t numerator = static_cast<uint64_t>(active_voltage) * MAX_PRESSURE_PSI * PSI_TO_PASCAL;
        uint32_t pascals = numerator / V_SPAN_MV;
        
        return pascals;
    }

    /**
     * 3. Перевод Паскалей (Па) в глубину (в миллиметрах водного столба)
     * Формула: Глубина_мм = Паскали / (Плотность_воды * g)
     */
    constexpr uint32_t pascalsToDepthMm(int32_t pascals) noexcept {
        if (pascals <= 0) {
            return 0u;
        }

        uint64_t numerator = static_cast<uint64_t>(pascals) * 1000u;
        uint32_t depth = static_cast<uint32_t>(numerator / 9800u);
        return depth;
    }
} // namespace PressureConversions

class PressureWrapper {
public:
    // Опрос: вызывать в основном цикле
    void poll() noexcept {
        // Проверяем, завершён ли полный буфер (флаг атомарный)
        if (fullConvDone_.load(std::memory_order_acquire)) {
            // Определяем, какая половина буфера заполнена (если обе, то обрабатываем полную)
            bool half = halfConvDone_.load(std::memory_order_acquire);

            // Обрабатываем данные буфера (используется локальное копирование)
            adcProcess(half);

            // Для первого набора данных сохраняем нулевое давление (атмосферное)
            if (isFirst_) {
                adc_z_ = pressSum_;   // среднее значение АЦП для нулевого давления
                isFirst_ = false;
            }

            // Вычисляем физические величины
            adcVoltage_ = PressureConversions::adcToVoltage(pressSum_, adc_z_);
            adcPascals_ = PressureConversions::voltageToPascals(adcVoltage_);
            depthMm_ = PressureConversions::pascalsToDepthMm(adcPascals_);

            // Если температура используется, вычисляем и её
            tempMilliCelsius_ = PressureConversions::voltageToTempCelsius(
                PressureConversions::tempAdcToVoltage(tempSum_)
            );

            // Сбрасываем флаг полного завершения (после того как данные обработаны)
            fullConvDone_.store(false, std::memory_order_release);
        }
    }

    // Геттеры
    uint32_t getDepthMm() const noexcept { return depthMm_; }
    uint32_t getPressurePascals() const noexcept { return adcPascals_; }
    uint32_t getAdcVoltage() const noexcept { return adcVoltage_; }
    int32_t  getTemperatureMilliCelsius() const noexcept { return tempMilliCelsius_; }

    // Методы, вызываемые из обработчиков прерываний
    void setHalfConvFlag() noexcept {
        halfConvDone_.store(true, std::memory_order_release);
        fullConvDone_.store(true, std::memory_order_release);
    }

    void setFullConvFlag() noexcept {
        fullConvDone_.store(true, std::memory_order_release);
    }

private:
    // Обработка данных из DMA-буфера
    void adcProcess(bool isHalf) noexcept {
        // Определяем диапазон индексов в буфере adcRaw_
        const size_t start = isHalf ? 0 : kAdcBufferSize;
        const size_t end   = isHalf ? kAdcBufferSize : kAdcBufferSize * 2;

        uint32_t pressSumLocal = 0;
        uint32_t tempSumLocal  = 0;

        // Суммируем значения из указанной половины буфера
        for (size_t i = start; i < end; ++i) {
            // Чётные индексы — канал давления, нечётные — температуры
            if ((i & 1) == 0) {
                pressSumLocal += adcRaw_[i];
            } else {
                tempSumLocal += adcRaw_[i];
            }
        }

        // Вычисляем среднее значение для каждой половины буфера
        const size_t halfCount = kAdcBufferSize / 2;
        pressSum_ = pressSumLocal / halfCount;
        tempSum_  = tempSumLocal / halfCount;

        // Сбрасываем флаги (только тот, который был обработан)
        if (isHalf) {
            halfConvDone_.store(false, std::memory_order_release);
        } else {
            fullConvDone_.store(false, std::memory_order_release);
        }
    }

    // Параметры DMA
    static constexpr size_t kAdcBufferSize = 200;          // количество отсчётов на канал
    // Буфер АЦП: 16-битные значения от DMA (обычно для 12-битных АЦП)
    std::array<uint16_t, kAdcBufferSize * 2> adcRaw_{};

    // Атомарные флаги для синхронизации с прерываниями
    std::atomic<bool> halfConvDone_{false};
    std::atomic<bool> fullConvDone_{false};

    // Состояние
    bool isFirst_ = true;

    // Суммы/средние АЦП
    uint32_t pressSum_ = 0;
    uint32_t tempSum_  = 0;

    // Эталонное значение АЦП для атмосферного давления
    uint32_t adc_z_ = 0;

    // Вычисленные физические величины
    uint32_t adcVoltage_   = 0;
    uint32_t adcPascals_   = 0;
    uint32_t depthMm_      = 0;
    int32_t  tempMilliCelsius_ = 0;
};