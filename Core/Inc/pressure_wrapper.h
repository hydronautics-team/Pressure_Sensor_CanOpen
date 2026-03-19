#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

#ifdef __cplusplus
}
#endif

#include <array>
#include <stdint.h>


#define LENGHT_OF_THE_ARRAY 200
#define VOLTAGE_WITHOUT_ATM_(adc_for_pressure_middle, adc_z)                   \
  ((adc_for_pressure_middle - adc_z) * 16575 / 2048)
#define VOLTAGE_CONV_PASCALES_(adc_conv_volt)                                  \
  (adc_conv_volt * 6895 / 4 * 37 / 9)
#define PASCALES_CONV_DEPTH_MM_(voltage_conv_pascales)                         \
  (voltage_conv_pascales / (10 * 981))

#define ADC_CONV_VOLTAGE_(adc_for_temperature_middle)                          \
  (adc_for_temperature_middle * 33120 / 4096)
#define VOLTAGE_CONV_TEMPERATURE_(adc_conv_voltage_for_temperature)            \
  ((adc_conv_voltage_for_temperature - 5000) * 10)


class PressureWrapper {
private:

public:
    PressureWrapper(ADC_HandleTypeDef &hadc, TIM_HandleTypeDef &htim) noexcept
        : hadc_{hadc}, htim_{htim} {}
    
    void init() noexcept {
        
      HAL_ADCEx_Calibration_Start(&hadc_);
      HAL_Delay(1000);
      HAL_TIM_Base_Start(&htim_);
      HAL_ADC_Start_DMA(&hadc_, adcRaw_.data(), LenAdcBuf_ * 2);
    }
    void poll() {
        if (fullConvDone_) {
            if(halfConvDone_)
                adcProcess(true);
            else
                adcProcess(false);
            if(isFirst_) {
                adc_z_ = pressSum;
                isFirst_ = false;
            }
            
            adcVoltage_ = VOLTAGE_WITHOUT_ATM_(pressSum, adc_z_);
            adcPasc_ = VOLTAGE_CONV_PASCALES_(adcVoltage_);
            depthMm_ = PASCALES_CONV_DEPTH_MM_(adcPasc_);
        }
      
    }
    
    void setHalfConvFlag() { 
        halfConvDone_ = 1;
        fullConvDone_ = 1; 
    }
    void setFullConvFlag() { fullConvDone_ = 1; }
  
    ~PressureWrapper() = default;
  
private:

    void adcProcess(bool isHalf) {
        uint8_t start = 0;
        uint8_t end = 0;
        if(isHalf) {
            start = 0;
            end = LenAdcBuf_;
            halfConvDone_ = 0;
            
        } else {
            start = LenAdcBuf_;
            end = LenAdcBuf_*2;
            fullConvDone_ = 0;
        }
        for (uint8_t i = start; i < end; i++) {
              if (i % 2) {
                  tempSum += adcRaw_[i];
              } else {
                  pressSum += adcRaw_[i];
              }
          }
          pressSum /= LenAdcBuf_/2;
          tempSum /= LenAdcBuf_/2;
        
    }
    ADC_HandleTypeDef &hadc_;
    TIM_HandleTypeDef &htim_;
    
    static constexpr uint8_t LenAdcBuf_{200};
    std::array<uint32_t, LenAdcBuf_ * 2> adcRaw_{0};
    
    uint32_t pressSum = 0;
    uint32_t tempSum = 0;
    uint32_t adcVoltage_ = 0;
    uint32_t adcPasc_ = 0;
    
    uint32_t adc_z_ = 0;
    //флаги прерываний
    volatile bool halfConvDone_ = false;
    volatile bool fullConvDone_ = false;
    bool isFirst_ = true;
    
    uint32_t depthMm_ = 0;
};
