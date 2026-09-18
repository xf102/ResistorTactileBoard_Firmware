/**
 * @file    drv_74hc595.h
 * @brief   74HC595 移位寄存器驱动（2/4 片级联，16/32 位并行输出），通过 SPI2 驱动。
 *
 * @details
 * 本驱动用于压阻触觉采集板的行激励切换。级联数量由 bsp_config.h 选择，
 * 默认 2 片并提供 16 路输出，也可切换为 4 片并提供 32 路输出。
 *
 * 平台无关：所有硬件操作通过 drv_74hc595_port.h 抽象层完成，
 * 驱动逻辑本身不直接依赖任何平台 HAL/SPL API。
 *
 * 硬件连接 (STM32F103C8T6):
 * @code
 *   MCU 引脚          74HC595 引脚         说明
 *   ─────────────────────────────────────────────────────
 *   PB15 (SPI2_MOSI) → DS (U1)            串行数据输入（首片）
 *   PB13 (SPI2_SCK)  → SH_CP (所有芯片)   移位时钟（共享）
 *   PB12 (GPIO)      → ST_CP (所有芯片)   锁存时钟（共享）
 *   U1 Q7S           → DS (U2)            级联串行输出 → 下片输入
 *   U2 Q7S           → DS (U3)
 *   U3 Q7S           → DS (U4)
 *   MR  (所有芯片)    → VCC               主复位无效（硬连线）
 *   OE  (所有芯片)    → GND               输出始终使能（硬连线）
 * @endcode
 *
 * SPI2 配置（由 CubeMX / MX_SPI2_Init 完成）:
 *   - 主模式、单工发送、8 位数据帧、MSB 先出
 *   - CPOL=0, CPHA=0（SCK 上升沿采样 → 匹配 SH_CP 上升沿移位）
 *   - BaudRatePrescaler=2 → APB1(36 MHz) / 2 = 18 MHz SCK
 *
 * 使用步骤:
 *   1. 确保 MX_SPI2_Init() 和 MX_GPIO_Init() 已调用。
 *   2. 调用 HC595_Init() 完成驱动初始化（已集成在 bsp_init 中）。
 *   3. 在扫描线程中调用 HC595_Write32(mask) 更新 16/32 路行激励。
 *
 * @note    参考文档: NXP 74HC595 / 74HCT595 Datasheet (Rev. 14, 2024)
 * @author  Firmware Team
 * @version 1.1
 * @date    2026-07-23
 */

#ifndef DRV_74HC595_H_
#define DRV_74HC595_H_

#ifdef __cplusplus
extern "C" {
#endif

/* ==========================================================================
 *  Change: 修改
 *  Editor: 谢峰
 *  Time: 2026-09-17
 *  Range: 在解析平台抽象层前载入 BSP 编译配置
 * ========================================================================== */
#include <stdint.h>
#include "bsp_config.h"          /* BSP compile-time configuration */
#include "drv_74hc595_port.h"    /* 平台抽象层（GPIO / SPI / 时序宏） */



/* ==========================================================================
 *  返回值定义
 * ========================================================================== */

/**
 * @brief 74HC595 驱动操作返回状态码
 */
typedef enum {
    HC595_OK = 0,       /**< 操作成功 */
    HC595_ERROR,        /**< SPI 通信错误 */
    HC595_TIMEOUT       /**< SPI 通信超时 */
} hc595_status_t;

/* ==========================================================================
 *  公共 API
 * ========================================================================== */

/**
 * @brief  初始化 74HC595 驱动。
 *
 * @details
 * 执行以下操作：
 *   -# 将 ST_CP 拉低（空闲态）
 *   -# 若启用 MR/OE GPIO，解除主复位并使能输出
 *   -# 移位输出全零并锁存，确保上电后所有行为低电平
 *
 * @note   必须在 MX_SPI2_Init() 和 MX_GPIO_Init() 之后调用。
 *         已在 bsp_init() 中自动调用，通常无需手动调用。
 *
 * @return 无
 */
void HC595_Init(void);

/* ==========================================================================
 *  Change: 修改
 *  Editor: 谢峰
 *  Time: 2026-09-17
 *  Range: 公共接口文档兼容两片和四片 74HC595 级联配置
 * ========================================================================== */
/**
 * @brief  向 2/4 片级联 74HC595 写入行激励掩码（阻塞式）。
 *
 * @details
 * 通过 SPI2 移位输出 2/4 字节数据（MSB 先出），随后在 ST_CP 上产生
 * 一个上升沿脉冲将移位寄存器内容锁存到输出寄存器。
 *
 * 位映射关系：
 * @code
 *   mask[7:0]   → U1 (Q0~Q7)   最靠近 MCU 的芯片
 *   mask[15:8]  → U2 (Q8~Q15)
 *   mask[23:16] → U3 (Q16~Q23，仅四片模式)
 *   mask[31:24] → U4 (Q24~Q31，仅四片模式)
 * @endcode
 *
 * @param  mask  行激励模式。两片模式仅使用低 16 位，四片模式使用全部 32 位。
 *
 * @retval HC595_OK       写入并锁存成功
 * @retval HC595_ERROR    SPI 发送返回错误
 * @retval HC595_TIMEOUT  SPI 发送超时
 */
hc595_status_t HC595_Write32(uint32_t mask);

/* ==========================================================================
 *  Change: 新增
 *  Editor: 谢峰
 *  Time: 2026-09-18
 *  Range: 增加不依赖系统时基的中断安全行激励写入接口
 * ========================================================================== */
/**
 * @brief  在中断上下文中以有限轮询方式写入并锁存行激励掩码。
 *
 * @details
 * 此接口直接轮询 SPI 状态寄存器，所有等待均受 HC595_ISR_POLL_LIMIT
 * 限制，不调用依赖 HAL_GetTick/SysTick 的阻塞式 HAL SPI 接口。
 * 适用于优先级高于 SysTick 的矩阵扫描定时器中断。
 *
 * @param  mask  行激励模式；有效位数由级联数量配置决定。
 *
 * @retval HC595_OK       写入并锁存成功
 * @retval HC595_ERROR    SPI 未使能或硬件句柄无效
 * @retval HC595_TIMEOUT  SPI 标志未在有限轮询次数内到达预期状态
 */
hc595_status_t HC595_Write32_ISR(uint32_t mask);

/**
 * @brief  手动产生 ST_CP 锁存脉冲。
 *
 * @details
 * 正常流程中 HC595_Write32() 内部会自动锁存。此接口暴露给高级用途，
 * 例如需要多次移位后再统一锁存的场景。
 *
 * @return 无
 */
void HC595_Latch(void);

/**
 * @brief  设置单个输出位（读-改-写），不影响其余位。
 *
 * @details
 * 内部维护 32 位影子寄存器，修改有效位后调用 HC595_Write32() 刷新。
 *
 * @param  bit    目标位索引。两片模式为 [0,15]，四片模式为 [0,31]。
 *                0 对应 U1 的 Q0。
 * @param  state  目标电平：1 = 高电平，0 = 低电平。
 *
 * @retval HC595_OK       设置成功
 * @retval HC595_ERROR    bit 超出范围或 SPI 通信错误
 * @retval HC595_TIMEOUT  SPI 发送超时
 */
hc595_status_t HC595_SetBit(uint8_t bit, uint8_t state);

/**
 * @brief  获取当前缓存的 32 位输出状态。
 *
 * @details
 * 返回最近一次通过 HC595_Write32() 或 HC595_SetBit() 写入的值，
 * 即影子寄存器内容。不产生任何 SPI/GPIO 操作。
 *
 * @return 当前 32 位输出状态（bit[n]=1 表示第 n 行为高）。
 */
uint32_t HC595_GetState(void);

#ifdef HC595_USE_MR_PIN
/**
 * @brief  通过 MR 引脚硬件复位所有移位寄存器。
 * @return 无
 */
void HC595_MasterReset(void);
#endif

#ifdef HC595_USE_OE_PIN
/**
 * @brief  使能或禁止 74HC595 并行输出。
 * @param  enable  1 = 使能输出（OE 拉低），0 = 禁止输出（OE 拉高，高阻）。
 * @return 无
 */
void HC595_OutputEnable(uint8_t enable);
#endif

#ifdef __cplusplus
}
#endif

#endif /* DRV_74HC595_H_ */
