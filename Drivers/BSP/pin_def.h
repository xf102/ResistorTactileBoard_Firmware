/**
 * @file    pin_def.h
 * @brief   全局引脚定义集中管理。
 *
 * @details
 * 所有 BSP 驱动共用的引脚映射统一定义在此文件中，
 * 各驱动通过 #include "pin_def.h" 引用，降低文件间耦合。
 *
 * 引脚分配总览 (STM32F103C8T6, LQFP48):
 * @code
 *   PA3        ADC RVS (EXTI3, 上升沿)     转换完成指示
 *   PA4        ADC CS/CONVST (低有效)      片选/转换启动
 *   PA5        SPI1_SCK                    ADC 时钟
 *   PA6        SPI1_MISO                   ADC 数据输出
 *   PA7        SPI1_MOSI                   ADC 数据输入
 *   PA8        ADC RST (低有效)            硬件复位
 *   PB0        Status LED 0 (高有效)
 *   PB1        Status LED 1 (高有效)
 *   PB12       74HC595 ST_CP              锁存时钟
 *   PB13       SPI2_SCK                    595 移位时钟
 *   PB15       SPI2_MOSI                   595 串行数据
 * @endcode
 *
 * @author  Firmware Team
 * @version 1.1
 * @date    2026-07-21
 */

#ifndef __PIN_DEF_H__
#define __PIN_DEF_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

/* ============================================================
 * 状态 LED（高电平点亮）
 * ============================================================ */
#define STATUS_LED_0_PIN        GPIO_PIN_0
#define STATUS_LED_0_PORT       GPIOB
#define STATUS_LED_0_ACTIVE_LVL GPIO_PIN_SET
#define STATUS_LED_0_INACT_LVL  GPIO_PIN_RESET

#define STATUS_LED_1_PIN        GPIO_PIN_1
#define STATUS_LED_1_PORT       GPIOB
#define STATUS_LED_1_ACTIVE_LVL GPIO_PIN_SET
#define STATUS_LED_1_INACT_LVL  GPIO_PIN_RESET

/* ==========================================================================
 *  Change: 修改
 *  Editor: 谢峰
 *  Time: 2026-09-17
 *  Range: 74HC595 级联数量移至 bsp_config.h 统一配置
 * ========================================================================== */
/* ============================================================
 * 74HC595 行激励驱动（2/4 片级联，SPI2）
 * ============================================================ */

/** @brief ST_CP 锁存时钟 GPIO 端口 */
#define HC595_STCP_PORT         GPIOB
/** @brief ST_CP 锁存时钟 GPIO 引脚 (PB12) */
#define HC595_STCP_PIN          GPIO_PIN_12

/* ============================================================
 * ADS8681 ADC（SPI1 全双工）
 * ============================================================ */

/** @brief CS/CONVST 片选 GPIO 端口（低有效） */
#define ADC_CS_PORT             GPIOA
/** @brief CS/CONVST 片选 GPIO 引脚 (PA4) */
#define ADC_CS_PIN              GPIO_PIN_4

/** @brief RST 硬件复位 GPIO 端口（低有效） */
#define ADC_RST_PORT            GPIOA
/** @brief RST 硬件复位 GPIO 引脚 (PA8) */
#define ADC_RST_PIN             GPIO_PIN_8

/** @brief RVS 转换完成指示 GPIO 端口（高电平 = 就绪） */
#define ADC_RVS_PORT            GPIOA
/** @brief RVS 转换完成指示 GPIO 引脚 (PA3) */
#define ADC_RVS_PIN             GPIO_PIN_3

/* ==========================================================================
 *  Change: 新增
 *  Editor: 谢峰
 *  Time: 2026-09-17
 *  Range: 复用现有 ADC 物理引脚并增加 TPC5120S16 语义别名
 * ========================================================================== */
#define TPC5120S16_CS_PORT          ADC_CS_PORT
#define TPC5120S16_CS_PIN           ADC_CS_PIN
#define TPC5120S16_ALARM_PORT       ADC_RVS_PORT
#define TPC5120S16_ALARM_PIN        ADC_RVS_PIN
#define TPC5120S16_LOW_ALARM_PORT   ADC_RST_PORT
#define TPC5120S16_LOW_ALARM_PIN    ADC_RST_PIN


/* ============================================================
 * CD74HC4067 列选通（2 片级联，32 通道）
 * 地址线 S0~S3 共享，EN 各自独立（低电平有效）
 * ============================================================ */

/** @brief S0 地址位 GPIO 引脚 (PB3) */
#define MUX_S0_PIN              GPIO_PIN_3
/** @brief S1 地址位 GPIO 引脚 (PB4) */
#define MUX_S1_PIN              GPIO_PIN_4
/** @brief S2 地址位 GPIO 引脚 (PB5) */
#define MUX_S2_PIN              GPIO_PIN_5
/** @brief S3 地址位 GPIO 引脚 (PB6) */
#define MUX_S3_PIN              GPIO_PIN_6
/** @brief 片 1 使能 GPIO 引脚 (PB7, 低有效, col 0~15) */
#define MUX_EN1_PIN             GPIO_PIN_7
/** @brief 片 2 使能 GPIO 引脚 (PB8, 低有效, col 16~31) */
#define MUX_EN2_PIN             GPIO_PIN_8
#ifdef __cplusplus
}
#endif

#endif /* __PIN_DEF_H__ */
