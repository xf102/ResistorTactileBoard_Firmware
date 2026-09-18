/**
 * @file    drv_74hc595.c
 * @brief   74HC595 移位寄存器驱动实现（2/4 片级联，16/32 位并行输出）。
 *
 * @details
 * 通过 SPI2 驱动 2/4 片菊花链连接的 74HC595，产生 16/32 路并行
 * 行激励电平，用于压阻触觉矩阵的逐行选通扫描。
 *
 * 平台无关：所有硬件操作通过 drv_74hc595_port.h 宏完成，
 * 本文件不直接调用任何平台 HAL/SPL API。
 *
 * 设计说明：
 *   - 每次切换一行激励后，外置 ADC 完成列采集才切换下一行，
 *     因此阻塞式 SPI 发送的开销可忽略，无需 DMA。
 *   - 内部维护 32 位影子寄存器，支持单 bit 读-改-写操作。
 *
 * 74HC595 关键时序 (VCC ≥ 4.5 V, 最坏情况):
 *   - SH_CP 高/低脉冲宽度:  ≥ 20 ns  → SPI 18 MHz 半周期 27.8 ns ✓
 *   - ST_CP 脉冲宽度:       ≥ 20 ns  → GPIO 翻转 + 函数调用开销 > 55 ns ✓
 *   - ST_CP 对末位 SH_CP 的建立时间: ≥ 20 ns → SPI TXE 标志保证 ✓
 *
 * @author  Firmware Team
 * @version 1.1
 * @date    2026-07-23
 */

#include "drv_74hc595.h"

/* ==========================================================================
 *  私有数据
 * ========================================================================== */

/** @brief 32 位输出影子寄存器，缓存最近一次写入的行激励状态 */
static uint32_t s_output_state = 0U;

/* ==========================================================================
 *  Change: 修改
 *  Editor: 谢峰
 *  Time: 2026-09-17
 *  Range: 发送缓冲区长度跟随两片/四片级联配置
 * ========================================================================== */
/** @brief SPI 发送缓冲区（2/4 字节，按级联移位顺序排列） */
static uint8_t s_tx_buf[HC595_TX_BYTES];

/* ==========================================================================
 *  私有辅助函数
 * ========================================================================== */

/**
 * @brief  在 ST_CP 引脚上产生一个完整的锁存脉冲（低→高→低）。
 *
 * @details
 * 74HC595 在 ST_CP 上升沿将移位寄存器内容传输到存储（输出）寄存器。
 * 本函数产生：LOW → HIGH → LOW 序列。
 *
 * 在 72 MHz 主频下，两次连续 GPIO 写操作耗时约 4~6 个时钟周期
 * （≥ 55 ns），远超 74HC595 要求的最小 20 ns 脉冲宽度。
 * 中间的 NOP + memory barrier 防止编译器优化合并 GPIO 写操作。
 *
 * @return 无
 */
static inline void hc595_pulse_stcp(void)
{
    HC595_GPIO_WRITE(HC595_STCP_PORT, HC595_STCP_PIN, HC595_GPIO_LOW);
    HC595_NOP_BARRIER();
    HC595_GPIO_WRITE(HC595_STCP_PORT, HC595_STCP_PIN, HC595_GPIO_HIGH);
    HC595_NOP_BARRIER();
    HC595_GPIO_WRITE(HC595_STCP_PORT, HC595_STCP_PIN, HC595_GPIO_LOW);
}

/**
 * @brief  将输出掩码按级联移位顺序打包到 2/4 字节发送缓冲区。
 *
 * @details
 * 74HC595 级联链中，最先移出的字节最终落在级联链末端芯片。
 * SPI 配置为 MSB 先出，因此：
 * @code
 *   两片模式: mask[15:8] → U2, mask[7:0] → U1
 *   四片模式: mask[31:24] → U4 ... mask[7:0] → U1
 * @endcode
 * 这样 mask 的 bit0 对应 U1 的 Q0 输出，符合直觉的行编号映射。
 *
 * @param  mask  32 位行激励掩码
 *
 * @return 无（结果写入全局 s_tx_buf）
 */
/* ==========================================================================
 *  Change: 修改
 *  Editor: 谢峰
 *  Time: 2026-09-17
 *  Range: 根据级联数量打包低 16 位或完整 32 位发送数据
 * ========================================================================== */
static inline void hc595_pack_tx(uint32_t mask)
{
    uint8_t index;

    for (index = 0U; index < HC595_TX_BYTES; ++index) {
        uint8_t shift = (uint8_t)((HC595_TX_BYTES - 1U - index) * 8U);
        s_tx_buf[index] = (uint8_t)(mask >> shift);
    }
}

/* ==========================================================================
 *  公共 API 实现
 * ========================================================================== */

/**
 * @brief  初始化 74HC595 驱动。
 *
 * @details
 * 执行以下初始化序列：
 *   -# 将 ST_CP 拉低，确保锁存引脚处于空闲态
 *   -# 若启用 MR GPIO，解除主复位（MR 拉高）
 *   -# 若启用 OE GPIO，使能并行输出（OE 拉低）
 *   -# 移位输出全零并锁存，清除上电不确定状态
 *
 * @note   前置条件：MX_SPI2_Init() 和 MX_GPIO_Init() 必须已完成。
 *         本函数已在 bsp_init() 中被调用。
 *
 * @return 无
 */
void HC595_Init(void)
{
    /* 确保 ST_CP 起始为低电平（空闲态） */
    HC595_GPIO_WRITE(HC595_STCP_PORT, HC595_STCP_PIN, HC595_GPIO_LOW);

#if HC595_USE_MR_PIN
    /* 解除主复位：MR 低有效 → 拉高进入正常工作模式 */
    HC595_GPIO_WRITE(HC595_MR_PORT, HC595_MR_PIN, HC595_GPIO_HIGH);
#endif

#if HC595_USE_OE_PIN
    /* 使能输出：OE 低有效 → 拉低使并行输出有效 */
    HC595_GPIO_WRITE(HC595_OE_PORT, HC595_OE_PIN, HC595_GPIO_LOW);
#endif

    /* 清除级联链：移位输出全零并锁存，消除上电不确定态 */
    s_output_state = 0U;
    hc595_pack_tx(0U);
    /* ==========================================================================
     *  Change: 修改
     *  Editor: 谢峰
     *  Time: 2026-09-17
     *  Range: 初始化清零发送长度跟随级联配置
     * ========================================================================== */
    HC595_SPI_TRANSMIT(HC595_SPI_HANDLE, s_tx_buf, HC595_TX_BYTES,
                       HC595_SPI_TIMEOUT_MS);
    hc595_pulse_stcp();
}

/**
 * @brief  向 2/4 片级联 74HC595 写入行激励掩码（阻塞式）。
 *
 * @details
 * 完整操作流程：
 *   -# 更新内部影子寄存器
 *   -# 将有效输出位打包为 2/4 字节发送缓冲区
 *   -# 调用 SPI 阻塞发送配置的字节数
 *   -# 产生 ST_CP 锁存脉冲，数据从移位寄存器传输到输出寄存器
 *
 * @param  mask  32 位行激励模式。
 *               bit[n] = 1 → 第 n 行输出高电平（选通）
 *               bit[n] = 0 → 第 n 行输出低电平（释放）
 *
 * @retval HC595_OK       SPI 发送及锁存均成功
 * @retval HC595_ERROR    SPI 发送返回错误
 * @retval HC595_TIMEOUT  SPI 发送超时（超过 HC595_SPI_TIMEOUT_MS）
 */
hc595_status_t HC595_Write32(uint32_t mask)
{
    hc595_hal_status_t hal_ret;

    /* ==========================================================================
     *  Change: 修改
     *  Editor: 谢峰
     *  Time: 2026-09-17
     *  Range: 两片模式仅保留低 16 位，并按配置发送 2/4 字节
     * ========================================================================== */
    /* 更新影子寄存器，仅保留当前级联芯片实际具备的输出位 */
    s_output_state = mask & HC595_OUTPUT_MASK;

    /* 按级联顺序打包发送缓冲区 */
    hc595_pack_tx(s_output_state);

    /* 阻塞式 SPI 发送 2/4 字节（16/32 个 SCK 时钟） */
    #ifdef SHIFT_REG_DBG_ENABLE
    LOG_D("[HC595] SPI transmit...");
    #endif // DEBUG SHIFT_REG_DBG_ENABLE
    hal_ret = HC595_SPI_TRANSMIT(HC595_SPI_HANDLE, s_tx_buf,
                                 HC595_TX_BYTES, HC595_SPI_TIMEOUT_MS);
    if (hal_ret != HC595_HAL_OK) {
        return (hal_ret == HC595_HAL_TIMEOUT) ? HC595_TIMEOUT : HC595_ERROR;
    }

    /* 锁存：ST_CP 上升沿将移位寄存器 → 输出寄存器 */
    #ifdef SHIFT_REG_DBG_ENABLE
    LOG_D("[HC595] Latching...");
    #endif // DEBUG SHIFT_REG_DBG_ENABLE
    hc595_pulse_stcp();

    return HC595_OK;
}

/**
 * @brief  手动产生 ST_CP 锁存脉冲。
 *
 * @details
 * 正常流程中 HC595_Write32() 内部已自动完成锁存。
 * 此接口用于高级场景，例如需要连续多次移位后再统一锁存。
 *
 * @return 无
 */
void HC595_Latch(void)
{
    hc595_pulse_stcp();
}

/**
 * @brief  设置单个输出位（读-改-写），不影响其余有效输出位。
 *
 * @details
 * 修改内部影子寄存器的指定 bit，然后调用 HC595_Write32() 将
 * 当前级联配置对应的完整状态重新移位输出并锁存。
 *
 * @param  bit    目标位索引，两片模式 [0,15]，四片模式 [0,31]。
 *                - 0~7   对应 U1 的 Q0~Q7
 *                - 8~15  对应 U2 的 Q0~Q7
 *                - 16~23 对应 U3 的 Q0~Q7
 *                - 24~31 对应 U4 的 Q0~Q7
 * @param  state  目标电平：
 *                - 1 = 高电平（选通该行）
 *                - 0 = 低电平（释放该行）
 *
 * @retval HC595_OK       设置并锁存成功
 * @retval HC595_ERROR    bit 超出当前配置范围，或 SPI 通信错误
 * @retval HC595_TIMEOUT  SPI 发送超时
 */
hc595_status_t HC595_SetBit(uint8_t bit, uint8_t state)
{
    if (bit >= HC595_OUTPUT_BITS) {
        return HC595_ERROR;
    }

    if (state) {
        s_output_state |= (1U << bit);
    } else {
        s_output_state &= ~(1U << bit);
    }

    return HC595_Write32(s_output_state);
}

/**
 * @brief  获取当前缓存的 32 位输出状态（影子寄存器）。
 *
 * @details
 * 返回最近一次通过 HC595_Write32() 或 HC595_SetBit() 写入的值。
 * 本函数为纯读取操作，不产生任何 SPI 或 GPIO 动作。
 *
 * @return 当前 32 位输出状态。bit[n]=1 表示第 n 行当前为高电平。
 */
uint32_t HC595_GetState(void)
{
    return s_output_state;
}

#if HC595_USE_MR_PIN
/**
 * @brief  通过 MR 引脚硬件复位所有移位寄存器。
 *
 * @warning 此操作仅清除移位寄存器内容，不影响已锁存的输出寄存器。
 *          若需同时清零物理输出，应在调用后执行 HC595_Write32(0)。
 *
 * @return 无
 */
void HC595_MasterReset(void)
{
    HC595_GPIO_WRITE(HC595_MR_PORT, HC595_MR_PIN, HC595_GPIO_LOW);
    HC595_NOP_BARRIER();
    HC595_NOP_BARRIER();
    HC595_GPIO_WRITE(HC595_MR_PORT, HC595_MR_PIN, HC595_GPIO_HIGH);
}
#endif

#if HC595_USE_OE_PIN
/**
 * @brief  使能或禁止 74HC595 并行输出（三态控制）。
 *
 * @details
 * OE 引脚低电平有效：
 *   - enable=1 → OE 拉低 → Q0~Q7 输出有效
 *   - enable=0 → OE 拉高 → Q0~Q7 进入高阻态
 *
 * @param  enable  输出使能控制：
 *                 - 1 = 使能输出（OE = LOW）
 *                 - 0 = 禁止输出（OE = HIGH，高阻态）
 *
 * @return 无
 */
void HC595_OutputEnable(uint8_t enable)
{
    HC595_GPIO_WRITE(HC595_OE_PORT, HC595_OE_PIN,
                     enable ? HC595_GPIO_LOW : HC595_GPIO_HIGH);
}
#endif
