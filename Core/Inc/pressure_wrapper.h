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
 *  Константы преобразований (замена макросов)
 */
namespace PressureConversions {
    // Преобразование среднего значения АЦП (без учёта атмосферного давления) в напряжение (мВ)
    constexpr uint32_t adcToVoltage(uint32_t adc_press, uint32_t adc_z) noexcept {
        return (adc_press - adc_z) * 16575u / 2048u;
    }

    // Преобразование напряжения (мВ) в давление (Па)
    constexpr uint32_t voltageToPascals(uint32_t voltage_mv) noexcept {
        return voltage_mv * 6895u / 4u * 37u / 9u;
    }

    // Преобразование давления (Па) в глубину (мм)
    constexpr uint32_t pascalsToDepthMm(uint32_t pascals) noexcept {
        return pascals / (10u * 981u);   // 10 * 981 = 9810
    }

    // Преобразование среднего значения АЦП температуры в напряжение (мВ)
    constexpr uint32_t tempAdcToVoltage(uint32_t adc_temp) noexcept {
        return adc_temp * 33120u / 4096u;
    }

    // Преобразование напряжения температуры (мВ) в температуру (десятые доли градуса Цельсия)
    constexpr int32_t voltageToTempCelsius(uint32_t voltage_mv) noexcept {
        // формула: (напряжение - 5000) * 10
        return (static_cast<int32_t>(voltage_mv) - 5000) * 10;
    }
} // namespace PressureConversions

class PressureWrapper {
public:
    // Конструктор
    PressureWrapper(ADC_HandleTypeDef& hadc, TIM_HandleTypeDef& htim) noexcept
        : hadc_(hadc), htim_(htim) {}

    // Инициализация: калибровка АЦП, запуск таймера и DMA
    bool init() noexcept {
        // Калибровка АЦП (однополярный режим для большинства STM32)
        if (HAL_ADCEx_Calibration_Start(&hadc_) != HAL_OK) {
            return false;
        }

        HAL_Delay(100);

        if (HAL_TIM_Base_Start(&htim_) != HAL_OK) {
            return false;
        }

        // Запуск DMA: буфер adcRaw_ объявлен как uint16_t, приводим к uint32_t*
        if (HAL_ADC_Start_DMA(&hadc_, reinterpret_cast<uint32_t*>(adcRaw_.data()), kAdcBufferSize * 2) != HAL_OK) {
            return false;
        }

        return true;
    }

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

    // Доступ к дескриптору АЦП для идентификации в обработчиках (опционально)
    ADC_HandleTypeDef& getHadc() noexcept { return hadc_; }

    ~PressureWrapper() = default;

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

    // Дескрипторы периферии
    ADC_HandleTypeDef& hadc_;
    TIM_HandleTypeDef& htim_;

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