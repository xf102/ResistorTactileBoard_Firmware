/**
 * @file    drv_TEMPLATE.h
 * @brief   TEMPLATE peripheral driver (brief description).
 *
 * @details
 * (Detailed description: what the chip does, how it connects to the MCU,
 *  SPI/GPIO/I2C configuration, timing constraints, etc.)
 *
 * Hardware connection (STM32F103C8T6):
 * @code
 *   MCU Pin           Chip Pin         Function
 *   ---------------------------------------------
 *   PBxx (SPIx_MOSI)  -> DIN          Data in
 *   PBxx (GPIO)        -> CS           Chip select (active low)
 * @endcode
 *
 * @author  Firmware Team
 * @version 1.0
 * @date    2026-xx-xx
 */

#ifndef DRV_TEMPLATE_H_
#define DRV_TEMPLATE_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include "stm32f1xx_hal.h"
#include "pin_def.h"

/* ==========================================================================
 *  Hardware configuration macros
 *  Pin mappings: see pin_def.h
 *  ========================================================================== */

/** @brief SPI handle used by this driver (extern defined in spi.c) */
#define TEMPLATE_SPI_HANDLE     (&hspiX)

/** @brief SPI blocking timeout (ms) */
#define TEMPLATE_SPI_TIMEOUT_MS 10U

/* ==========================================================================
 *  Return codes
 *  ========================================================================== */
typedef enum {
    TEMPLATE_OK = 0,
    TEMPLATE_ERROR,
    TEMPLATE_TIMEOUT
} template_status_t;

/* ==========================================================================
 *  Handle (if driver needs runtime state; delete if stateless)
 *  ========================================================================== */
typedef struct {
    /* SPI_HandleTypeDef* hspi; */
    /* GPIO_TypeDef* cs_port; uint16_t cs_pin; */
    /* ... chip-specific state ... */
} TEMPLATE_HandleTypeDef;

/* ==========================================================================
 *  Public API
 *  ========================================================================== */

/**
 * @brief  Initialize the TEMPLATE driver.
 * @note   Must be called after MX_SPIx_Init() and MX_GPIO_Init().
 *         Called automatically from bsp_init() when BSP_USE_TEMPLATE is defined.
 */
void TEMPLATE_Init(TEMPLATE_HandleTypeDef* h);

/**
 * @brief  Example read/write operation.
 */
template_status_t TEMPLATE_Read(TEMPLATE_HandleTypeDef* h, uint16_t* pData);

/* ==========================================================================
 *  Optional: RT-Thread dependent APIs
 *  Only compiled when BSP_USE_RTTHREAD is defined (via bsp_config.h)
 *  ========================================================================== */
/*
 * If this driver needs RTT types (rt_err_t, rt_sem_t, etc.) in its public
 * API, replace the #include block at the top with:
 *
 *   #include "bsp_config.h"   // instead of "stm32f1xx_hal.h" + <rtthread.h>
 *
 * and guard RTT-dependent declarations:
 *
 *   #ifdef BSP_USE_RTTHREAD
 *   rt_err_t TEMPLATE_DoAsync(...);
 *   #endif
 */

#ifdef __cplusplus
}
#endif

#endif /* DRV_TEMPLATE_H_ */
