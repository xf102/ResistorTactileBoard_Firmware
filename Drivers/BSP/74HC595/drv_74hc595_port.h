/**
 * @file    drv_74hc595_port.h
 * @brief   74HC595 驱动平台抽象层（STM32 HAL/LL 双模式）。
 *
 * @details
 * 将驱动所需的平台相关接口映射到 STM32 API，支持两套驱动库：
 *   - HAL 模式（默认）：HAL_GPIO_WritePin / HAL_SPI_Transmit
 *   - LL  模式（高性能）：LL_GPIO_SetOutputPin / LL_SPI_TransmitData8（内联展开）
 *   通过编译宏 BSP_USE_LL_DRIVER 在两者间切换（该宏定义在 bsp_config.h）。
 *
 * 若需移植到 GD32 或其他平台，复制本文件并替换宏实现即可，
 * 驱动逻辑文件 (drv_74hc595.c) 无需修改。
 *
 * @author  Firmware Team
 * @version 1.1
 * @date    2026-07-24
 */

#ifndef DRV_74HC595_PORT_H_
#define DRV_74HC595_PORT_H_

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
  #include "stm32f7xx_hal.h"
#else
  #include "stm32f1xx_hal.h"   /* 默认回退到 F1 */
#endif

#include "spi.h"   /* extern SPI_HandleTypeDef hspi2; */

/* ==========================================================================
 *  硬件配置宏
 *  引脚映射见 pin_def.h（HC595_STCP_PORT / HC595_STCP_PIN 等）
 *  SPI 句柄 / 超时见 drv_74hc595_port.h（HC595_SPI_HANDLE / HC595_SPI_TIMEOUT_MS）
 * ========================================================================== */

/* ==========================================================================
 *  可选: MR（主复位）和 OE（输出使能）GPIO 控制
 *
 *  若板上 MR 硬接 VCC、OE 硬接 GND，保持为 0 即可。
 *  若由 GPIO 控制，改为 1 并填写对应端口/引脚宏。
 * ========================================================================== */

/** @brief 是否使用 GPIO 控制 MR 引脚 (0=硬连线, 1=GPIO) */

// #define HC595_USE_MR_PIN

/** @brief 是否使用 GPIO 控制 OE 引脚 (0=硬连线, 1=GPIO) */

// #define HC595_USE_OE_PIN

#ifdef HC595_USE_MR_PIN
  /** @brief MR 引脚 GPIO 端口 */
  #define HC595_MR_PORT         GPIOB
  /** @brief MR 引脚 GPIO Pin（TODO: 填入实际引脚） */
  #define HC595_MR_PIN          GPIO_PIN_x
#endif

#ifdef HC595_USE_OE_PIN
  /** @brief OE 引脚 GPIO 端口 */
  #define HC595_OE_PORT         GPIOB
  /** @brief OE 引脚 GPIO Pin（TODO: 填入实际引脚） */
  #define HC595_OE_PIN          GPIO_PIN_x
#endif

/* --------------------------------------------------------------------------
 *  SPI 句柄与超时
 * -------------------------------------------------------------------------- */

/** @brief 移位数据使用的 SPI 句柄 */
#define HC595_SPI_HANDLE            (&hspi2)

/** @brief SPI 阻塞发送超时时间 (ms) */
#define HC595_SPI_TIMEOUT_MS        10U

/* ==========================================================================
 *  Change: 新增
 *  Editor: 谢峰
 *  Time: 2026-09-18
 *  Range: 定义中断上下文 SPI 状态轮询的最大次数，避免依赖 SysTick 超时
 * ========================================================================== */
/**
 * @brief 每次等待 SPI 标志的最大轮询次数。
 * @note  4096 次在 72 MHz MCU 上为异常路径保留充足余量，同时保证中断必定退出。
 */
#define HC595_ISR_POLL_LIMIT        4096U

/* --------------------------------------------------------------------------
 *  GPIO 电平常量
 * -------------------------------------------------------------------------- */

/** @brief GPIO 高电平 */
#define HC595_GPIO_HIGH             GPIO_PIN_SET

/** @brief GPIO 低电平 */
#define HC595_GPIO_LOW              GPIO_PIN_RESET

/* --------------------------------------------------------------------------
 *  GPIO 操作
 * -------------------------------------------------------------------------- */

/**
 * @brief  写 GPIO 引脚电平。
 * @param  port   GPIO 端口（如 GPIOB）
 * @param  pin    GPIO 引脚（如 GPIO_PIN_12）
 * @param  state  目标电平（HC595_GPIO_HIGH / HC595_GPIO_LOW）
 */
#ifdef BSP_USE_LL_DRIVER
  #define HC595_GPIO_WRITE(port, pin, state) \
      do { \
          if ((state) != HC595_GPIO_LOW) \
              LL_GPIO_SetOutputPin((port), (uint32_t)(pin)); \
          else \
              LL_GPIO_ResetOutputPin((port), (uint32_t)(pin)); \
      } while(0)
#else
  #define HC595_GPIO_WRITE(port, pin, state) \
      HAL_GPIO_WritePin((port), (pin), (state))
#endif

/* --------------------------------------------------------------------------
 *  SPI 操作
 * -------------------------------------------------------------------------- */

/**
 * @brief  阻塞式 SPI 发送。
 * @param  hspi     SPI 句柄指针
 * @param  buf      发送缓冲区
 * @param  len      字节数
 * @param  timeout  超时（毫秒）
 * @return HAL_StatusTypeDef
 * @note   LL 模式：超时参数用于兼容接口，实际未使用（建议开启看门狗保护）。
 *         发送完成后等待 TXE + BSY=0，确保最后一个 SCK 已结束再返回，
 *         满足 74HC595 ST_CP 锁存脉冲需在 SH_CP 结束后产生的时序要求。
 */
#ifdef BSP_USE_LL_DRIVER
  #define HC595_SPI_TRANSMIT(hspi, buf, len, timeout) \
      ({ \
          HAL_StatusTypeDef _sts = HAL_OK; \
          SPI_TypeDef *_spi = (hspi)->Instance; \
          (void)(timeout); /* LL 轮询模式不使用超时，建议开启独立看门狗保护 */ \
          for (uint32_t _i = 0U; _i < (uint32_t)(len); _i++) { \
              while (!LL_SPI_IsActiveFlag_TXE(_spi)) {} \
              LL_SPI_TransmitData8(_spi, (buf)[_i]); \
          } \
          /* 等待最后一字节从移位寄存器完全移出，确保 ST_CP 锁存时序正确 */ \
          while (!LL_SPI_IsActiveFlag_TXE(_spi)) {} \
          while (LL_SPI_IsActiveFlag_BSY(_spi)) {} \
          _sts; \
      })
#else
  #define HC595_SPI_TRANSMIT(hspi, buf, len, timeout) \
      HAL_SPI_Transmit((hspi), (buf), (len), (timeout))
#endif

/* --------------------------------------------------------------------------
 *  状态码映射
 * -------------------------------------------------------------------------- */

/** @brief HAL 状态码类型 */
typedef HAL_StatusTypeDef hc595_hal_status_t;

/** @brief 操作成功 */
#define HC595_HAL_OK                HAL_OK

/** @brief 操作超时 */
#define HC595_HAL_TIMEOUT           HAL_TIMEOUT

/* --------------------------------------------------------------------------
 *  时序屏障
 * -------------------------------------------------------------------------- */

/**
 * @brief  NOP + 编译器屏障，防止 GPIO 写操作被优化合并。
 *
 * @details
 * 在 72 MHz 主频下，一次 NOP + memory barrier 确保相邻 GPIO
 * 写操作之间至少间隔数个时钟周期，满足 74HC595 最小脉冲宽度要求。
 */
#define HC595_NOP_BARRIER()         __asm volatile ("nop" ::: "memory")

#endif /* DRV_74HC595_PORT_H_ */
