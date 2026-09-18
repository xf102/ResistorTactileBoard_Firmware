/**
 * @file    drv_ads8681_port_stm32.h
 * @brief   ADS8681 驱动 STM32 HAL/LL 双模式平台适配层。
 *
 * @details
 * 将驱动所需的平台抽象接口映射到 STM32 API，支持两套驱动库：
 *   - HAL 模式（默认）：HAL_GPIO_WritePin / HAL_GPIO_ReadPin / HAL_SPI_TransmitReceive
 *   - LL  模式（高性能）：LL_GPIO_SetOutputPin / LL_SPI_TransmitData8（内联展开）
 *   通过编译宏 BSP_USE_LL_DRIVER 在两者间切换（该宏定义在 bsp_config.h）。
 *
 * 接口清单：
 *   - GPIO 读写   → ads8681_gpio_write / ads8681_gpio_read
 *   - SPI 收发    → ads8681_spi_trx（阻塞）、ads8681_spi_trx_dma（非阻塞）
 *   - 延时/计时   → ads8681_delay_ms / ads8681_get_tick
 *
 * 支持的 STM32 系列：F1 / F4 / F7 / H7（通过编译宏自动选择 HAL/LL 头文件）。
 *
 * @note    本文件由 drv_ads8681.h 在 ADS8681_PLATFORM == STM32 时自动包含。
 * @author  Firmware Team
 * @version 1.2
 * @date    2026-07-24
 */

#ifndef DRV_ADS8681_PORT_STM32_H_
#define DRV_ADS8681_PORT_STM32_H_

/* --------------------------------------------------------------------------
 *  HAL 头文件选择（根据目标 STM32 系列）
 * -------------------------------------------------------------------------- */
#if defined(STM32F1xx)
  #include "stm32f1xx_hal.h"
#elif defined(STM32F4xx)
  #include "stm32f4xx_hal.h"
#elif defined(STM32F7xx)
  #include "stm32f7xx_hal.h"
#elif defined(STM32H7xx)
  #include "stm32h7xx_hal.h"
#else
  #include "stm32f1xx_hal.h"   /* 默认回退到 F1 */
#endif

/* --------------------------------------------------------------------------
 *  类型映射
 * -------------------------------------------------------------------------- */

/** @brief SPI 句柄类型 → STM32 HAL SPI_HandleTypeDef */
typedef SPI_HandleTypeDef    ads8681_spi_t;

/** @brief GPIO 端口类型 → STM32 HAL GPIO_TypeDef */
typedef GPIO_TypeDef         ads8681_gpio_port_t;

/** @brief GPIO 引脚类型 → STM32 HAL uint16_t (GPIO_PIN_x) */
typedef uint16_t             ads8681_gpio_pin_t;

/** @brief 状态码类型 → STM32 HAL HAL_StatusTypeDef */
typedef HAL_StatusTypeDef    ads8681_status_t;

/* --------------------------------------------------------------------------
 *  GPIO 电平常量
 * -------------------------------------------------------------------------- */

/** @brief GPIO 高电平 */
#define ADS8681_GPIO_SET                GPIO_PIN_SET

/** @brief GPIO 低电平 */
#define ADS8681_GPIO_RESET              GPIO_PIN_RESET

/* --------------------------------------------------------------------------
 *  状态码映射
 * -------------------------------------------------------------------------- */

/** @brief 操作成功 */
#define ADS8681_OK                      HAL_OK

/** @brief 操作错误 */
#define ADS8681_ERROR                   HAL_ERROR

/** @brief 设备忙 */
#define ADS8681_BUSY                    HAL_BUSY

/** @brief 操作超时 */
#define ADS8681_TIMEOUT                 HAL_TIMEOUT

/* --------------------------------------------------------------------------
 *  SPI 超时
 * -------------------------------------------------------------------------- */

/** @brief SPI 阻塞传输无限等待 */
#define ADS8681_SPI_TIMEOUT_FOREVER     HAL_MAX_DELAY

/* --------------------------------------------------------------------------
 *  延时与计时
 * -------------------------------------------------------------------------- */

/** @brief 毫秒级阻塞延时 */
#define ads8681_delay_ms(ms)            HAL_Delay(ms)

/** @brief 获取系统 tick（毫秒，用于超时计算） */
#define ads8681_get_tick()              HAL_GetTick()

/* --------------------------------------------------------------------------
 *  GPIO 操作
 * -------------------------------------------------------------------------- */

/**
 * @brief  写 GPIO 引脚电平。
 * @param  port   GPIO 端口（如 GPIOA）
 * @param  pin    GPIO 引脚（如 GPIO_PIN_4）
 * @param  state  目标电平（ADS8681_GPIO_SET / ADS8681_GPIO_RESET）
 */
#ifdef BSP_USE_LL_DRIVER
  #define ads8681_gpio_write(port, pin, state) \
      do { \
          if ((state) != ADS8681_GPIO_RESET) \
              LL_GPIO_SetOutputPin((port), (uint32_t)(pin)); \
          else \
              LL_GPIO_ResetOutputPin((port), (uint32_t)(pin)); \
      } while(0)
#else
  #define ads8681_gpio_write(port, pin, state) \
      HAL_GPIO_WritePin((port), (pin), (state))
#endif

/**
 * @brief  读 GPIO 引脚电平。
 * @param  port  GPIO 端口
 * @param  pin   GPIO 引脚
 * @return GPIO_PIN_SET 或 GPIO_PIN_RESET
 */
#ifdef BSP_USE_LL_DRIVER
  #define ads8681_gpio_read(port, pin) \
      (LL_GPIO_IsInputPinSet((port), (uint32_t)(pin)) ? GPIO_PIN_SET : GPIO_PIN_RESET)
#else
  #define ads8681_gpio_read(port, pin) \
      HAL_GPIO_ReadPin((port), (pin))
#endif

/* --------------------------------------------------------------------------
 *  SPI 操作
 * -------------------------------------------------------------------------- */

/**
 * @brief  阻塞式 SPI 全双工收发。
 * @param  hspi  SPI 句柄指针
 * @param  tx    发送缓冲区
 * @param  rx    接收缓冲区
 * @param  len   字节数（ADS8681 固定为 4）
 * @param  to    超时（毫秒），ADS8681_SPI_TIMEOUT_FOREVER 为无限等待
 * @return HAL_StatusTypeDef
 * @note   LL 模式：超时参数用于兼容接口，实际未使用（建议开启看门狗保护）。
 *         HAL 模式：完整超时保护，传 ADS8681_SPI_TIMEOUT_FOREVER 为无限等待。
 */
#ifdef BSP_USE_LL_DRIVER
  #define ads8681_spi_trx(hspi, tx, rx, len, to) \
      ({ \
          HAL_StatusTypeDef _sts = HAL_OK; \
          SPI_TypeDef *_spi = (hspi)->Instance; \
          (void)(to); /* LL 轮询模式不使用超时，建议开启独立看门狗保护 */ \
          for (uint32_t _i = 0U; _i < (uint32_t)(len); _i++) { \
              while (!LL_SPI_IsActiveFlag_TXE(_spi)) {} \
              LL_SPI_TransmitData8(_spi, (tx)[_i]); \
              while (!LL_SPI_IsActiveFlag_RXNE(_spi)) {} \
              (rx)[_i] = LL_SPI_ReceiveData8(_spi); \
          } \
          _sts; \
      })
#else
  #define ads8681_spi_trx(hspi, tx, rx, len, to) \
      HAL_SPI_TransmitReceive((hspi), (tx), (rx), (len), (to))
#endif

/**
 * @brief  DMA 式 SPI 全双工收发（非阻塞）。
 * @param  hspi  SPI 句柄指针
 * @param  tx    发送缓冲区（DMA 完成前不可释放）
 * @param  rx    接收缓冲区（DMA 完成前不可释放）
 * @param  len   字节数（ADS8681 固定为 4）
 * @return HAL_StatusTypeDef
 * @note   完成后 HAL 调用 HAL_SPI_TxRxCpltCallback，用户需在其中
 *         调用 ADS8681_DMA_RxCpltCallback()。
 * @note   DMA 传输始终使用 HAL 库（LL 不做 SPI+DMA 联合封装，需手动配置
 *         DMA 通道）。若需 LL DMA，请在本适配层自行实现。
 */
#define ads8681_spi_trx_dma(hspi, tx, rx, len) \
    HAL_SPI_TransmitReceive_DMA((hspi), (tx), (rx), (len))

#endif /* DRV_ADS8681_PORT_STM32_H_ */
