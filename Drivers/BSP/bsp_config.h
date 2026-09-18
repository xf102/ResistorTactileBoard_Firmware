/**
 * @file    bsp_config.h
 * @brief   BSP global config -- bare-metal peripheral module switches.
 *
 * @details
 * All BSP peripheral module enable/disable switches are centralized in this
 * file so the application does not depend on a concrete ADC implementation.
 *
 * Usage:
 *   - Define / comment out macros below to enable / disable modules.
 *   - Any driver / application file only needs #include "bsp_config.h"
 *     to get all enabled module headers.
 *
 * @author  Firmware Team
 * @version 1.1
 * @date    2026-07-23
 */

#ifndef __BSP_CONFIG_H__
#define __BSP_CONFIG_H__

#ifdef __cplusplus
extern "C" {
#endif
/* ==========================================================================
 *  Change: 新增
 *  Editor: 谢峰
 *  Time: 2026-09-17
 *  Range: 增加 74HC595 两片/四片级联选择，默认使用两片
 * ========================================================================== */
#define BSP_HC595_CASCADE_2       2U
#define BSP_HC595_CASCADE_4       4U

#ifndef BSP_HC595_CASCADE_NUM
#define BSP_HC595_CASCADE_NUM     BSP_HC595_CASCADE_2
#endif

#if (BSP_HC595_CASCADE_NUM != BSP_HC595_CASCADE_2) && \
    (BSP_HC595_CASCADE_NUM != BSP_HC595_CASCADE_4)
  #error "BSP_HC595_CASCADE_NUM must be BSP_HC595_CASCADE_2 or BSP_HC595_CASCADE_4"
#endif

#define HC595_CASCADE_NUM         BSP_HC595_CASCADE_NUM
#define HC595_TX_BYTES            HC595_CASCADE_NUM
#define HC595_OUTPUT_BITS         (HC595_CASCADE_NUM * 8U)

#if BSP_HC595_CASCADE_NUM == BSP_HC595_CASCADE_2
  #define HC595_OUTPUT_MASK       0x0000FFFFUL
#else
  #define HC595_OUTPUT_MASK       0xFFFFFFFFUL
#endif

#include "pin_def.h"
/* ============================================================
 *  BSP peripheral module enable switches  (define = enable, comment out = disable)
 * ============================================================ */

/** @brief Status LED module (deferred until a bare-metal pattern service exists) */
/* #define BSP_USE_STATUS_LED */

/** @brief 74HC595 row-driver module (2/4 cascaded, SPI2, 16/32 outputs) */
#define BSP_USE_74HC595

/* ==========================================================================
 *  Change: 修改
 *  Editor: 谢峰
 *  Time: 2026-09-18
 *  Range: 裸机第一阶段选择已完成硬件验证的 ADS8681 + CD74HC4067 前端
 * ========================================================================== */
#define BSP_ADC_FRONTEND_ADS8681_MUX  1U
#define BSP_ADC_FRONTEND_TPC5120S16   2U

#define BSP_ADC_FRONTEND BSP_ADC_FRONTEND_ADS8681_MUX

#if BSP_ADC_FRONTEND == BSP_ADC_FRONTEND_ADS8681_MUX
  #define BSP_USE_ADS8681
  #define BSP_USE_CD74HC4067_MUX
#elif BSP_ADC_FRONTEND == BSP_ADC_FRONTEND_TPC5120S16
  #define BSP_USE_TPC5120S16
#else
  #error "Unsupported BSP_ADC_FRONTEND"
#endif

/** @brief Product serial number module (UID read + SN generation, cross-series) */
#define BSP_USE_PRODUCT

/* ==========================================================================
 *  Change: 新增
 *  Editor: 谢峰
 *  Time: 2026-09-18
 *  Range: 增加裸机串口服务与静态双缓冲编译配置
 * ========================================================================== */
#define BSP_USE_USART1_CONSOLE
#define BSP_USE_USART2_DATAPORT
#define BSP_FRAME_BUFFER_COUNT  2U

/* ==========================================================================
 *  Change: 新增
 *  Editor: 谢峰
 *  Time: 2026-09-17
 *  Range: 统一定义默认 8x8 矩阵规格，供前端和采集应用共用
 * ========================================================================== */
#define BSP_MATRIX_8X8

#ifdef BSP_MATRIX_8X8
  #define BSP_MATRIX_ROWS  8U
  #define BSP_MATRIX_COLS  8U
#else
  #define BSP_MATRIX_ROWS  16U
  #define BSP_MATRIX_COLS  16U
#endif

/* ============================================================
 *  全局 BSP 驱动库选择（LL vs HAL）
 *
 *  BSP_USE_LL_DRIVER  0 = HAL（默认，功能完整，调试友好）
 *  BSP_USE_LL_DRIVER  1 = LL （内联展开，高性能，适合生产环境）
 *
 *  所有 BSP 驱动（ADS8681 / 74HC595 / LED 等）共享此开关，
 *  各驱动的 port_stm32.h 中通过此宏自动选择 HAL 或 LL API。
 * ============================================================ */

// #define BSP_USE_LL_DRIVER


/* ============================================================
 *  LL 库头文件（仅当 BSP_USE_LL_DRIVER=1 时引入）
 *
 *  所有 BSP 驱动在 LL 模式下共享同一套 LL 头文件，
 *  各驱动的 port_stm32.h 不再重复包含。
 * ============================================================ */
#ifdef BSP_USE_LL_DRIVER
  #if defined(STM32F1xx)
    #include "stm32f1xx_ll_gpio.h"
    #include "stm32f1xx_ll_spi.h"
  #elif defined(STM32F4xx)
    #include "stm32f4xx_ll_gpio.h"
    #include "stm32f4xx_ll_spi.h"
  #elif defined(STM32F7xx)
    #include "stm32f7xx_ll_gpio.h"
    #include "stm32f7xx_ll_spi.h"
  #elif defined(STM32H7xx)
    #include "stm32h7xx_ll_gpio.h"
    #include "stm32h7xx_ll_spi.h"
  #else
    #include "stm32f1xx_ll_gpio.h"
    #include "stm32f1xx_ll_spi.h"
  #endif
#endif

#include "debug_log.h"


/* ============================================================
 *  ADS8681 default input range
 *  See ADS8681_RANGE_* macros in drv_ads8681.h for options.
 * ============================================================ */
#ifdef BSP_USE_ADS8681
  /** @brief Default ADC range applied in bsp_init() */
  #define BSP_ADS8681_DEFAULT_RANGE   ADS8681_RANGE_UNIPOLAR_1_25VREF
#endif

/* ==========================================================================
 *  Change: 新增
 *  Editor: 谢峰
 *  Time: 2026-09-17
 *  Range: 定义 TPC5120S16 正式采集、同步与可选矩阵诊断参数
 * ========================================================================== */
#ifdef BSP_USE_TPC5120S16
  #define BSP_TPC5120S16_DEFAULT_RANGE         TPC5120S16_RANGE_0_TO_VREF
  #define BSP_TPC5120S16_DEFAULT_LAST_CHANNEL  (BSP_MATRIX_COLS - 1U)
  #define BSP_TPC5120S16_SYNC_ATTEMPTS          32U
  #ifndef BSP_TPC5120S16_MATRIX_DIAGNOSTIC
    #define BSP_TPC5120S16_MATRIX_DIAGNOSTIC 0U
  #endif
#endif

/* ============================================================
 *  Conditional driver header includes
 *  (Disabled modules will not be compiled in)
 *
 * ============================================================ */

#ifdef BSP_USE_STATUS_LED
  #include "bsp_led.h"
#endif

#ifdef BSP_USE_74HC595
  #include "drv_74hc595.h"
  // #define SHIFT_REG_DBG_ENABLE
#endif
#ifdef BSP_USE_ADS8681
  #include "drv_ads8681.h"
  // #define ADS8681_DBG_ENABLE
#endif

/* ==========================================================================
 *  Change: 新增
 *  Editor: 谢峰
 *  Time: 2026-09-17
 *  Range: 仅在选择 TPC 前端时引入 TPC5120S16 驱动接口
 * ========================================================================== */
#ifdef BSP_USE_TPC5120S16
  #include "drv_tpc5120s16.h"
  // #define TPC5120S16_DBG_ENABLE
#endif

#ifdef BSP_USE_CD74HC4067_MUX
  #include "drv_cd74hc4067.h"
  // #define MUX_DBG_ENABLE
#endif

#ifdef BSP_USE_PRODUCT
  #include "product.h"
#endif

// Disable debug macros if global dbg is not enabled
#ifndef DBG_ENABLE
  #undef SHIFT_REG_DBG_ENABLE
  #undef ADS8681_DBG_ENABLE
  #undef TPC5120S16_DBG_ENABLE
  #undef MUX_DBG_ENABLE
#endif



#ifdef __cplusplus
}
#endif

#endif /* __BSP_CONFIG_H__ */
