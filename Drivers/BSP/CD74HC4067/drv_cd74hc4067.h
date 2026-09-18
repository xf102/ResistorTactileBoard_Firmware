/**
 * @file    drv_cd74hc4067.h
 * @brief   CD74HC4067 16 通道模拟多路复用器驱动（2 片级联，32 列选通）。
 *
 * @details
 * 两片 CD74HC4067 共享 S0~S3 地址线，各自独立 EN 使能：
 *   - 片 1 (EN1, PB7): 列 0~15
 *   - 片 2 (EN2, PB8): 列 16~31
 * 所有 GPIO 操作采用 BSRR 寄存器直接写入，单次原子操作完成切换。
 *
 * @note    参考文档: TI CD74HC4067 Datasheet (SCHS366)
 * @author  Firmware Team
 * @version 1.0
 * @date    2026-07-24
 */

#ifndef DRV_CD74HC4067_H_
#define DRV_CD74HC4067_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include "bsp_config.h"   /* RTT header + module switches */
/**
 * @brief  初始化：关闭所有通道（两片 EN 拉高）。
 */
void CD74HC4067_Init(void);

/**
 * @brief  选通指定列通道。
 *
 * @param  ch  列索引 [0, 31]。
 *             0~15  → 片 1 导通，片 2 关断
 *             16~31 → 片 2 导通，片 1 关断
 */
void CD74HC4067_Select(uint8_t ch);

#ifdef __cplusplus
}
#endif

#endif /* DRV_CD74HC4067_H_ */
