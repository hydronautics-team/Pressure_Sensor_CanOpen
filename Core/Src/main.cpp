#include <optional>
#ifdef __cplusplus
extern "C"
{
#endif

#include "main.h"

#ifdef __cplusplus
}
#endif

#include "ADS1220.hpp"
#include "hydrolib_bus_datalink_stream.hpp"
#include "hydrolib_bus_application_master.hpp"
#include "hydrv_gpio_low.hpp"
#include "hydrv_rs_485.hpp"
#include "pressure_processing.hpp"

constinit hydrv::GPIO::GPIOLow rx_pin(hydrv::GPIO::GPIOLow::GPIOA_port, 10, hydrv::GPIO::GPIOLow::GPIO_UART_RX);

constinit hydrv::GPIO::GPIOLow tx_pin(hydrv::GPIO::GPIOLow::GPIOA_port, 9, hydrv::GPIO::GPIOLow::GPIO_UART_TX);

constinit hydrv::GPIO::GPIOLow direction_pin(hydrv::GPIO::GPIOLow::GPIOA_port, 8, hydrv::GPIO::GPIOLow::GPIO_Output);

static constexpr hydrv::UART::UARTLow::UARTPreset USART1_115200_LOW{
    USART1_BASE, 7, RCC_APB2ENR_USART1EN, RCC_BASE + offsetof(RCC_TypeDef, APB2ENR), USART1_IRQn, 39, 1
};

constinit hydrv::RS485::RS485<255, 255> RS(USART1_115200_LOW, rx_pin, tx_pin, direction_pin, 7);
class Logger
{
public:
    Logger() = default;
};
Logger loger;
hydrolib::bus::datalink::StreamManager manager(1, RS, loger);
hydrolib::bus::datalink::Stream stream(manager, 2);
hydrolib::bus::application::Master master(stream, loger);

CAN_HandleTypeDef hcan;
ADS1220 external_adc;
PressureProcess pressure{ external_adc };

void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_SPI1_Init(void);
static void MX_CAN_Init(void);
std::optional<int32_t> adc_value = 0;
bool isPressure                  = true;

int main(void)
{
    HAL_Init();

    SystemClock_Config();

    MX_GPIO_Init();
    MX_SPI1_Init();
    MX_CAN_Init();

    RS.Init();
    external_adc.Init();

    HAL_GPIO_WritePin(STM_ALIVE_GPIO_Port, STM_ALIVE_Pin, GPIO_PIN_SET);

    while (1) {
        manager.Process();

        if (isPressure) {
            // получаем данные с ацп
            external_adc.PressureMeasurementProcess();
            // обрабатываем значение с ацп
            pressure.process();
            // isPressure = false;
        }
        if (!isPressure) {
            adc_value = external_adc.TemperatureMeasurementProcess();
            // дописать мат обработку датчика температуры
            // isPressure = true;
        }
        auto depthMm = pressure.getDepthMm();
        if (depthMm == std::nullopt) {
            // отправлять чето что ошибка или просто забить
        }
        else {
            master.Write(static_cast<void*>(&depthMm), 0, sizeof(depthMm));
        }
    }
}

void SystemClock_Config(void)
{
    RCC_OscInitTypeDef RCC_OscInitStruct = { 0 };
    RCC_ClkInitTypeDef RCC_ClkInitStruct = { 0 };

    RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    RCC_OscInitStruct.HSEState       = RCC_HSE_ON;
    RCC_OscInitStruct.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
    RCC_OscInitStruct.HSIState       = RCC_HSI_ON;
    RCC_OscInitStruct.PLL.PLLState   = RCC_PLL_ON;
    RCC_OscInitStruct.PLL.PLLSource  = RCC_PLLSOURCE_HSE;
    RCC_OscInitStruct.PLL.PLLMUL     = RCC_PLL_MUL9;
    if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK) {
        Error_Handler();
    }

    RCC_ClkInitStruct.ClockType      = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    RCC_ClkInitStruct.SYSCLKSource   = RCC_SYSCLKSOURCE_PLLCLK;
    RCC_ClkInitStruct.AHBCLKDivider  = RCC_SYSCLK_DIV1;
    RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
    RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

    if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK) {
        Error_Handler();
    }
}

static void MX_CAN_Init(void)
{
    hcan.Instance                  = CAN1;
    hcan.Init.Prescaler            = 16;
    hcan.Init.Mode                 = CAN_MODE_NORMAL;
    hcan.Init.SyncJumpWidth        = CAN_SJW_1TQ;
    hcan.Init.TimeSeg1             = CAN_BS1_1TQ;
    hcan.Init.TimeSeg2             = CAN_BS2_1TQ;
    hcan.Init.TimeTriggeredMode    = DISABLE;
    hcan.Init.AutoBusOff           = DISABLE;
    hcan.Init.AutoWakeUp           = DISABLE;
    hcan.Init.AutoRetransmission   = DISABLE;
    hcan.Init.ReceiveFifoLocked    = DISABLE;
    hcan.Init.TransmitFifoPriority = DISABLE;
    if (HAL_CAN_Init(&hcan) != HAL_OK) {
        Error_Handler();
    }
}

static void MX_GPIO_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = { 0 };

    __HAL_RCC_GPIOD_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();

    /*Configure GPIO pin Output Level */
    HAL_GPIO_WritePin(GPIOA, STM_ALIVE_Pin | SPI1_NSS_Pin, GPIO_PIN_SET);

    /*Configure GPIO pins : STM_ALIVE_Pin SPI1_NSS_Pin */
    GPIO_InitStruct.Pin   = STM_ALIVE_Pin | SPI1_NSS_Pin;
    GPIO_InitStruct.Mode  = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull  = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    /*Configure GPIO pin : DATA_READY_Pin */
    GPIO_InitStruct.Pin  = DATA_READY_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(DATA_READY_GPIO_Port, &GPIO_InitStruct);
}

void USART1_IRQHandler(void)
{
    RS.IRQCallback();
}

void Error_Handler(void)
{
    /* USER CODE BEGIN Error_Handler_Debug */
    /* User can add his own implementation to report the HAL error return state */
    __disable_irq();
    while (1) {
    }
    /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
 * @brief  Reports the name of the source file and the source line number
 *         where the assert_param error has occurred.
 * @param  file: pointer to the source file name
 * @param  line: assert_param error line source number
 * @retval None
 */
void assert_failed(uint8_t* file, uint32_t line)
{
    /* USER CODE BEGIN 6 */
    /* User can add his own implementation to report the file name and line
       number, ex: printf("Wrong parameters value: file %s on line %d\r\n", file,
       line) */
    /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
