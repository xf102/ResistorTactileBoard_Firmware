/* ==========================================================================
 *  Change: 新增
 *  Editor: 谢峰
 *  Time: 2026-09-17
 *  Range: 创建 TPC5120S16 STM32 HAL 平台适配层
 * ========================================================================== */
#ifndef DRV_TPC5120S16_PORT_STM32_H_
#define DRV_TPC5120S16_PORT_STM32_H_

#if defined(STM32F4xx)
#include "stm32f4xx_hal.h"
#elif defined(STM32F7xx)
#include "stm32f7xx_hal.h"
#elif defined(STM32H7xx)
#include "stm32h7xx_hal.h"
#else
#include "stm32f1xx_hal.h"
#endif

typedef SPI_HandleTypeDef tpc5120s16_spi_t;
typedef GPIO_TypeDef tpc5120s16_gpio_port_t;
typedef uint16_t tpc5120s16_gpio_pin_t;

#define TPC5120S16_GPIO_HIGH                   GPIO_PIN_SET
#define TPC5120S16_GPIO_LOW                    GPIO_PIN_RESET
#define TPC5120S16_SPI_TIMEOUT_MS              HAL_MAX_DELAY
#define tpc5120s16_gpio_write(port, pin, state) HAL_GPIO_WritePin((port), (pin), (state))
#define tpc5120s16_gpio_read(port, pin)         HAL_GPIO_ReadPin((port), (pin))
#define tpc5120s16_spi_transfer(spi, tx, rx, length, timeout) \
    HAL_SPI_TransmitReceive((spi), (tx), (rx), (length), (timeout))
#define tpc5120s16_spi_transfer_dma(spi, tx, rx, length) \
    HAL_SPI_TransmitReceive_DMA((spi), (tx), (rx), (length))
#define tpc5120s16_delay_ms(delay)              HAL_Delay(delay)

#endif /* DRV_TPC5120S16_PORT_STM32_H_ */
