/**
 * @file    drv_cd74hc4067.c
 * @brief   CD74HC4067 驱动实现（BSRR 直接寄存器操作）。
 *
 * @author  Firmware Team
 * @version 1.0
 * @date    2026-07-24
 */

#include "drv_cd74hc4067.h"

#ifdef BSP_USE_CD74HC4067_MUX
/**
 * @brief  初始化：两片 EN 拉高（全关），地址线清零。
 */
void CD74HC4067_Init(void)
{
    /* EN1(PB7) HIGH + EN2(PB8) HIGH → 全部通道关断
     * S0~S3(PB3~PB6) → 复位到 0 */
    GPIOB->BSRR = MUX_EN1_PIN | MUX_EN2_PIN |          /* set: EN1, EN2 high */
                  ((uint32_t)(MUX_S0_PIN | MUX_S1_PIN |
                              MUX_S2_PIN | MUX_S3_PIN) << 16U); /* reset: S0~S3 low */
}

/**
 * @brief  选通第 ch 列 [0,31]，单次 BSRR 原子写入完成全部切换。
 *
 * @param  ch  列索引。0~15 片 1，16~31 片 2。
 */
void CD74HC4067_Select(uint8_t ch)
{
    uint8_t  addr = (ch < 16U) ? ch : (uint8_t)(ch - 16U);
    uint32_t bsrr;

    /* S0~S3 (PB3~PB6): addr bit=1 → set, bit=0 → reset */
    uint32_t s_set   = ((uint32_t)(addr & 0x0FU)) << 3U;
    uint32_t s_reset = ((uint32_t)(~addr & 0x0FU)) << 3U;
    bsrr = s_set | (s_reset << 16U);

    /* EN1(PB7) / EN2(PB8): 低电平有效 */
    #ifdef SHIFT_REG_DBG_ENABLE
    LOG_D("[MUX] Select ch=%u, addr=%u, BSRR=0x%08X", ch, addr, bsrr);
    #endif
    if (ch < 16U) {
        bsrr |= (MUX_EN1_PIN << 16U) | MUX_EN2_PIN;   /* EN1 low, EN2 high */
    } else {
        bsrr |= MUX_EN1_PIN | (MUX_EN2_PIN << 16U);   /* EN1 high, EN2 low */
    }

    GPIOB->BSRR = bsrr;
}
#endif /* BSP_USE_CD74HC4067_MUX */