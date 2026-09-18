/**
 * @file  drv_ads8681_port_gd32.h
 * @brief Example GD32 port layer for the ADS8681 driver.
 *
 * This file is a **template**.  It shows what the driver expects from a
 * non-STM32 platform.  Copy it into your GD32 project, adapt the includes
 * and the inline functions to your actual SPL/HAL, then make sure it is
 * found by the compiler when ADS8681_PLATFORM == ADS8681_PLATFORM_GD32.
 */

#ifndef DRV_ADS8681_PORT_GD32_H_
#define DRV_ADS8681_PORT_GD32_H_

#include "gd32f30x.h"   /* change to your GD32 device header, e.g. gd32f10x.h */

/* --------------------------------------------------------------------------
 *  Type definitions
 *  -------------------------------------------------------------------------- */

/**
 * @brief SPI handle / peripheral type.
 *        If you use the GD32 SPL, this can be SPI_TypeDef* or a custom struct.
 *        If you pass the SPI peripheral directly, it is the base address.
 */
typedef SPI_TypeDef*   ads8681_spi_t;

/**
 * @brief GPIO port type.
 *        The driver stores a pointer to this type, so define it as the
 *        plain port struct type (e.g. GPIO_TypeDef).  The handle field will
 *        then become GPIO_TypeDef*.
 */
typedef GPIO_TypeDef   ads8681_gpio_port_t;

typedef uint16_t       ads8681_gpio_pin_t;

/* --------------------------------------------------------------------------
 *  Status codes (must match the values used by the driver)
 *  -------------------------------------------------------------------------- */
typedef enum
{
    ADS8681_OK      = 0,
    ADS8681_ERROR   = 1,
    ADS8681_BUSY    = 2,
    ADS8681_TIMEOUT = 3
} ads8681_status_t;

/* --------------------------------------------------------------------------
 *  GPIO helpers
 *  -------------------------------------------------------------------------- */
#define ADS8681_GPIO_SET     1
#define ADS8681_GPIO_RESET   0

#define ADS8681_SPI_TIMEOUT_FOREVER    0xFFFFFFFFU

/* Replace with your actual delay/tick functions. */
#define ads8681_delay_ms(ms)    delay_1ms((ms))
#define ads8681_get_tick()      get_tick()      /* or sysTickValue, etc. */

static inline void ads8681_gpio_write(ads8681_gpio_port_t* port,
                                      ads8681_gpio_pin_t pin,
                                      uint8_t state)
{
    if (state)
    {
        GPIO_BOP(port) = pin;   /* set   */
    }
    else
    {
        GPIO_BC(port) = pin;    /* reset */
    }
}

static inline uint8_t ads8681_gpio_read(ads8681_gpio_port_t* port,
                                          ads8681_gpio_pin_t pin)
{
    return (GPIO_ISTAT(port) & pin) ? 1U : 0U;
}

/* --------------------------------------------------------------------------
 *  SPI helpers
 *  -------------------------------------------------------------------------- */

/**
 * @brief Blocking SPI transfer: send tx[len], receive into rx[len].
 * @note  len is in bytes; the ADS8681 driver always calls with len == 4.
 */
static inline ads8681_status_t ads8681_spi_trx(ads8681_spi_t hspi,
                                               const uint8_t* tx,
                                               uint8_t* rx,
                                               uint16_t len,
                                               uint32_t timeout)
{
    (void)timeout;  /* or use it if your SPL supports timeout */

    for (uint16_t i = 0; i < len; ++i)
    {
        /* Wait until TX FIFO/empty flag, then send. */
        while (RESET == spi_i2s_flag_get(hspi, SPI_FLAG_TBE));
        spi_i2s_data_transmit(hspi, tx[i]);

        /* Wait until RX not empty, then receive. */
        while (RESET == spi_i2s_flag_get(hspi, SPI_FLAG_RBNE));
        rx[i] = (uint8_t)spi_i2s_data_receive(hspi);
    }

    return ADS8681_OK;
}

/**
 * @brief Start an SPI DMA transfer.
 * @note  Implement this to match your GD32 DMA/SPI setup.  The driver will
 *        call this, then in the DMA complete ISR you must call
 *        ADS8681_DMA_RxCpltCallback().
 */
static inline ads8681_status_t ads8681_spi_trx_dma(ads8681_spi_t hspi,
                                                   const uint8_t* tx,
                                                   uint8_t* rx,
                                                   uint16_t len)
{
    /* Example skeleton (GD32 SPL): configure DMA channels, enable, etc. */
    (void)hspi; (void)tx; (void)rx; (void)len;

    /* TODO: implement with your DMA engine. */

    return ADS8681_ERROR;
}

#endif /* DRV_ADS8681_PORT_GD32_H_ */
