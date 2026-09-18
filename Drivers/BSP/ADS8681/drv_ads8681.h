/**
 * @file    drv_ads8681.h
 * @brief   ADS8681/ADS8685/ADS8689 16 位 SAR ADC 驱动（支持 STM32 / GD32 平台）。
 *
 * @details
 * 本驱动实现 TI ADS868x 系列 16 位 SAR ADC 的完整寄存器级控制，包括：
 *   - 硬件复位与上电初始化
 *   - 可编程输入量程（双极性 ±12.288 V ~ ±2.56 V / 单极性 0 ~ 12.288 V）
 *   - 阻塞式单帧采样读取
 *   - DMA 非阻塞连续采样
 *   - 内部寄存器读写（含 WKEY 保护机制）
 *
 * 平台选择（在包含本头文件前定义，默认 STM32）：
 * @code
 *   #define ADS8681_PLATFORM   ADS8681_PLATFORM_STM32   // STM32 HAL
 *   #define ADS8681_PLATFORM   ADS8681_PLATFORM_GD32    // GD32 SPL
 * @endcode
 *
 * 硬件连接（本项目 STM32F103C8T6）：
 * @code
 *   MCU 引脚           ADS8681 引脚       说明
 *   ─────────────────────────────────────────────────
 *   PA5  (SPI1_SCK)  → SCLK             SPI 时钟
 *   PA7  (SPI1_MOSI) → SDI              SPI 数据输入（MCU→ADC）
 *   PA6  (SPI1_MISO) → SDO              SPI 数据输出（ADC→MCU）
 *   PA4  (GPIO)      → CS/CONVST        片选 / 转换启动（低有效）
 *   PA3  (EXTI3)     → RVS              转换完成指示（上升沿）
 *   (GPIO)           → RST              硬件复位（低有效）
 * @endcode
 *
 * @note    参考文档: TI ADS8681/85/89 Datasheet (SBAS633E, 2024)
 * @author  Firmware Team
 * @version 1.1
 * @date    2026-07-21
 */

#ifndef DRV_ADS8681_H_
#define DRV_ADS8681_H_

#include <stdint.h>
#include "bsp_config.h"   /* RTT header + module switches */
/* ==========================================================================
 *  平台抽象层
 *  ========================================================================== */

#ifndef ADS8681_PLATFORM
#define ADS8681_PLATFORM    ADS8681_PLATFORM_STM32
#endif

#define ADS8681_PLATFORM_STM32    1   /**< STM32 HAL 平台 */
#define ADS8681_PLATFORM_GD32     2   /**< GD32 SPL 平台 */

#if ADS8681_PLATFORM == ADS8681_PLATFORM_STM32

  #include "drv_ads8681_port_stm32.h"

#elif ADS8681_PLATFORM == ADS8681_PLATFORM_GD32

  /**
   * GD32 平台适配层。用户需提供 drv_ads8681_port_gd32.h，定义以下接口：
   *   - ads8681_spi_t           SPI 句柄类型
   *   - ads8681_gpio_port_t     GPIO 端口类型
   *   - ads8681_gpio_pin_t      GPIO 引脚类型
   *   - ads8681_status_t        状态码枚举 (OK/ERROR/BUSY/TIMEOUT)
   *   - ADS8681_GPIO_SET / ADS8681_GPIO_RESET
   *   - ADS8681_OK / ADS8681_ERROR / ADS8681_BUSY / ADS8681_TIMEOUT
   *   - ADS8681_SPI_TIMEOUT_FOREVER
   *   - ads8681_delay_ms(ms) / ads8681_get_tick()
   *   - ads8681_gpio_write(port, pin, state) / ads8681_gpio_read(port, pin)
   *   - ads8681_spi_trx(hspi, tx, rx, len, timeout)
   *   - ads8681_spi_trx_dma(hspi, tx, rx, len)
   */
  #include "drv_ads8681_port_gd32.h"

#else
  #error "Unknown ADS8681_PLATFORM. Use ADS8681_PLATFORM_STM32 or ADS8681_PLATFORM_GD32."
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* ==========================================================================
 *  SPI 命令操作码（32 位字，MSB 先出）
 *
 *  帧格式: [31:25] 操作码 | [24:16] 9-bit 地址 | [15:0] 数据域
 *  命令在 CS 上升沿生效，响应数据在下一帧 SDO 输出。
 *  ========================================================================== */

/** @brief 由 8 位寄存器地址构建 9-bit 地址域（放置于 bits[23:16]） */
#define ADS8681_ADDR_9BIT(reg8)          (((uint32_t)(reg8) & 0xFFU) << 16)

/** @brief NOP 空操作命令（读取上一次转换结果） */
#define ADS8681_CMD_NOP                  0x00000000U

/** @brief CLEAR_HWORD: 将数据域中为 '1' 的对应寄存器位清零 */
#define ADS8681_CMD_CLEAR_HWORD(reg, dat) \
    (0xC0000000U | ADS8681_ADDR_9BIT(reg) | ((uint32_t)(dat) & 0xFFFFU))

/** @brief READ_HWORD: 16 位寄存器读（数据在下一帧 D[31:16] 返回） */
#define ADS8681_CMD_READ_HWORD(reg) \
    (0xC8000000U | ADS8681_ADDR_9BIT(reg))

/** @brief READ: 8 位寄存器读（数据在下一帧 D[31:24] 返回） */
#define ADS8681_CMD_READ(reg) \
    (0x48000000U | ADS8681_ADDR_9BIT(reg))

/** @brief WRITE_HWORD: 16 位寄存器全写 */
#define ADS8681_CMD_WRITE_HWORD(reg, dat) \
    (0xD0000000U | ADS8681_ADDR_9BIT(reg) | ((uint32_t)(dat) & 0xFFFFU))

/** @brief WRITE_MSB: 仅写寄存器高字节 D[15:8] */
#define ADS8681_CMD_WRITE_MSB(reg, dat) \
    (0xD2000000U | ADS8681_ADDR_9BIT(reg) | (((dat) >> 8) & 0xFFU))

/** @brief WRITE_LSB: 仅写寄存器低字节 D[7:0] */
#define ADS8681_CMD_WRITE_LSB(reg, dat) \
    (0xD4000000U | ADS8681_ADDR_9BIT(reg) | ((dat) & 0xFFU))

/** @brief SET_HWORD: 将数据域中为 '1' 的对应寄存器位置位 */
#define ADS8681_CMD_SET_HWORD(reg, dat) \
    (0xD8000000U | ADS8681_ADDR_9BIT(reg) | ((uint32_t)(dat) & 0xFFFFU))

/* ==========================================================================
 *  寄存器地址映射
 *  ========================================================================== */

#define ADS8681_REG_DEVICE_ID            0x00U   /**< 设备 ID（只读） */
#define ADS8681_REG_RST_PWRCTL           0x04U   /**< 复位 / 电源控制 */
#define ADS8681_REG_SDI_CTL              0x08U   /**< SPI 输入模式配置 */
#define ADS8681_REG_SDO_CTL              0x0CU   /**< SDO 输出格式配置 */
#define ADS8681_REG_DATAOUT_CTL          0x10U   /**< 数据输出内容配置 */
#define ADS8681_REG_RANGE_SEL            0x14U   /**< 输入量程选择 */
#define ADS8681_REG_ALARM                0x20U   /**< 报警标志（只读） */
#define ADS8681_REG_ALARM_H_TH           0x24U   /**< 报警上限阈值 */
#define ADS8681_REG_ALARM_L_TH           0x28U   /**< 报警下限阈值 */

/* --------------------------------------------------------------------------
 *  RST_PWRCTL_REG (0x04) 位定义
 *  注意: 写入 bits[5:0] 前必须在 D[15:8] 填入 WKEY=0x69
 * -------------------------------------------------------------------------- */
#define ADS8681_PWRCTL_WKEY_VALUE       0x6900U   /**< 写保护密钥，填入 D[15:8] */
#define ADS8681_PWRCTL_VDD_AL_DIS       (1U << 5) /**< 禁止 VDD 报警 */
#define ADS8681_PWRCTL_IN_AL_DIS        (1U << 4) /**< 禁止输入报警 */
#define ADS8681_PWRCTL_RSTN_APP         (1U << 2) /**< 应用层软复位 */
#define ADS8681_PWRCTL_NAP_EN           (1U << 1) /**< 使能 NAP 低功耗模式 */
#define ADS8681_PWRCTL_PWRDN            (1U << 0) /**< 进入 Power-down */

/* --------------------------------------------------------------------------
 *  SDI_CTL_REG (0x08) 位定义 — SPI 时钟模式
 * -------------------------------------------------------------------------- */
#define ADS8681_SDI_MODE_00             0x00U   /**< CPOL=0, CPHA=0（上电默认） */
#define ADS8681_SDI_MODE_01             0x01U   /**< CPOL=0, CPHA=1 */
#define ADS8681_SDI_MODE_10             0x02U   /**< CPOL=1, CPHA=0 */
#define ADS8681_SDI_MODE_11             0x03U   /**< CPOL=1, CPHA=1 */

/* --------------------------------------------------------------------------
 *  SDO_CTL_REG (0x0C) 位定义 — SDO 输出配置
 * -------------------------------------------------------------------------- */
#define ADS8681_SDO1_CONFIG_TRISTATE    (0U << 8) /**< SDO1 三态（仅 SDO0 输出） */
#define ADS8681_SDO1_CONFIG_RANGE       (1U << 8) /**< SDO1 输出量程信息 */
#define ADS8681_SDO1_CONFIG_DEVICE_ADDR (2U << 8) /**< SDO1 输出设备地址 */
#define ADS8681_SDO1_CONFIG_INPUT       (3U << 8) /**< SDO1 输出输入通道 */
#define ADS8681_SDO0_CONFIG_RANGE       (1U << 0) /**< SDO0 输出含量程信息 */

/* --------------------------------------------------------------------------
 *  DATAOUT_CTL_REG (0x10) 位定义 — 输出数据内容
 * -------------------------------------------------------------------------- */
#define ADS8681_DATAOUT_VDD_INCL        (1U << 15) /**< 输出包含 VDD 值 */
#define ADS8681_DATAOUT_DEVICE_ADDR_INCL (1U << 14) /**< 输出包含设备地址 */
#define ADS8681_DATAOUT_RANGE_INCL      (1U << 8)  /**< 输出包含量程 ID */
#define ADS8681_DATAOUT_PAR_EN          (1U << 3)  /**< 使能奇偶校验位 */
#define ADS8681_DATAOUT_DATA_VAL_CONV   0x00U   /**< 输出转换数据（默认） */
#define ADS8681_DATAOUT_DATA_VAL_ZERO   0x04U   /**< 输出全零（测试用） */
#define ADS8681_DATAOUT_DATA_VAL_ONE    0x05U   /**< 输出全一（测试用） */
#define ADS8681_DATAOUT_DATA_VAL_ALT01  0x06U   /**< 输出 0101...（测试用） */
#define ADS8681_DATAOUT_DATA_VAL_ALT00  0x07U   /**< 输出 0011...（测试用） */

/* --------------------------------------------------------------------------
 *  RANGE_SEL_REG (0x14) 位定义 — 输入量程选择
 *  默认内部基准 Vref = 4.096 V
 * -------------------------------------------------------------------------- */
#define ADS8681_RANGE_INTREF_DIS        (1U << 6) /**< 禁止内部基准（使用外部） */
#define ADS8681_RANGE_BIPOLAR_3VREF     0x00U   /**< 双极性 ±3×Vref = ±12.288 V */
#define ADS8681_RANGE_BIPOLAR_2_5VREF   0x01U   /**< 双极性 ±2.5×Vref = ±10.24 V */
#define ADS8681_RANGE_BIPOLAR_1_5VREF   0x02U   /**< 双极性 ±1.5×Vref = ±6.144 V */
#define ADS8681_RANGE_BIPOLAR_1_25VREF  0x03U   /**< 双极性 ±1.25×Vref = ±5.12 V */
#define ADS8681_RANGE_BIPOLAR_0_625VREF 0x04U   /**< 双极性 ±0.625×Vref = ±2.56 V */
#define ADS8681_RANGE_UNIPOLAR_3VREF    0x08U   /**< 单极性 0 ~ 3×Vref = 0 ~ 12.288 V */
#define ADS8681_RANGE_UNIPOLAR_2_5VREF  0x09U   /**< 单极性 0 ~ 2.5×Vref = 0 ~ 10.24 V */
#define ADS8681_RANGE_UNIPOLAR_1_5VREF  0x0AU   /**< 单极性 0 ~ 1.5×Vref = 0 ~ 6.144 V */
#define ADS8681_RANGE_UNIPOLAR_1_25VREF 0x0BU   /**< 单极性 0 ~ 1.25×Vref = 0 ~ 5.12 V */

/* ==========================================================================
 *  驱动句柄
 *  ========================================================================== */

/**
 * @brief ADS8681 驱动实例句柄
 *
 * @details 每个 ADC 芯片对应一个句柄实例。使用前需填充 SPI 句柄和
 *          GPIO 端口/引脚信息（由 CubeMX 或手动配置）。
 */
typedef struct
{
    ads8681_spi_t*       hspi;      /**< 平台 SPI 句柄指针 */
    ads8681_gpio_port_t* cs_port;   /**< CS/CONVST GPIO 端口 */
    ads8681_gpio_pin_t   cs_pin;    /**< CS/CONVST GPIO 引脚 */
    ads8681_gpio_port_t* rst_port;  /**< RST GPIO 端口 */
    ads8681_gpio_pin_t   rst_pin;   /**< RST GPIO 引脚 */
    ads8681_gpio_port_t* rvs_port;  /**< RVS GPIO 端口 */
    ads8681_gpio_pin_t   rvs_pin;   /**< RVS GPIO 引脚 */
    uint8_t*             pRxBuf;    /**< DMA 接收缓冲区指针（4 字节，用户所有） */
    uint8_t              txBuf[4];  /**< DMA 发送缓冲区（内部使用，MSB 先出） */
    volatile uint8_t     dma_busy;  /**< DMA 传输进行中标志（ISR/线程共享） */
} ADS8681_HandleTypeDef;

/* ==========================================================================
 *  公共 API
 *  ========================================================================== */

/**
 * @brief  初始化 ADS8681 驱动并执行硬件复位。
 *
 * @details
 * 执行以下初始化序列：
 *   -# CS 拉高（非激活态）、RST 拉高（非复位态）
 *   -# 清除 DMA 状态
 *   -# 执行硬件复位（RST 脉冲 + 等待 POR 恢复）
 *   -# 回读 DEVICE_ID 寄存器验证 SPI 通信正常
 *
 * @note   前置条件：SPI 外设和 GPIO 已由平台初始化完成。
 *         上电默认量程为 ±3×Vref，如需更改请调用 ADS8681_SetRange()。
 *
 * @param  hadc  驱动句柄指针（需预先填充 hspi/cs/rst/rvs 字段）
 *
 * @return 无
 */
void ADS8681_Init(ADS8681_HandleTypeDef* hadc);

/**
 * @brief  执行硬件复位（RST 引脚脉冲）。
 *
 * @details
 * 将 RST 拉低 > 100 ns（数据手册 6.7 节），然后释放并等待
 * POR 恢复时间（tD_RST_POR 最大 20 ms，实际等待 25 ms）。
 * 复位后所有寄存器恢复默认值。
 *
 * @param  hadc  驱动句柄指针
 *
 * @return 无
 */
void ADS8681_Reset(ADS8681_HandleTypeDef* hadc);

/**
 * @brief  查询转换是否完成（非阻塞）。
 *
 * @details 读取 RVS 引脚电平。RVS 高电平表示转换完成 / 器件空闲。
 *
 * @param  hadc  驱动句柄指针
 *
 * @retval 1  转换完成（RVS = HIGH）
 * @retval 0  转换进行中（RVS = LOW）
 */
uint8_t ADS8681_IsConvReady(ADS8681_HandleTypeDef* hadc);

/**
 * @brief  等待转换完成（阻塞轮询 RVS）。
 *
 * @details 持续读取 RVS 引脚直到高电平或超时。
 *
 * @param  hadc        驱动句柄指针
 * @param  timeout_ms  超时时间（毫秒）。基于平台 tick 计时。
 *
 * @retval ADS8681_OK       转换完成（RVS 已拉高）
 * @retval ADS8681_TIMEOUT  超时未就绪
 */
ads8681_status_t ADS8681_WaitConvReady(ADS8681_HandleTypeDef* hadc, uint32_t timeout_ms);

/**
 * @brief  向内部寄存器写入 16 位数据。
 *
 * @details
 * 使用 WRITE_HWORD 命令写入指定寄存器。
 * 若目标为 RST_PWRCTL_REG (0x04)，自动在 D[15:8] 注入 WKEY=0x69，
 * 调用者只需提供 bits[5:0] 的有效控制位。
 *
 * @param  hadc  驱动句柄指针
 * @param  reg   目标寄存器地址（如 ADS8681_REG_RANGE_SEL）
 * @param  data  待写入的 16 位数据（对 RST_PWRCTL 仅 bits[5:0] 有效）
 *
 * @retval ADS8681_OK       写入成功
 * @retval ADS8681_ERROR    SPI 通信错误
 * @retval ADS8681_TIMEOUT  SPI 通信超时
 */
ads8681_status_t ADS8681_WriteReg(ADS8681_HandleTypeDef* hadc, uint8_t reg, uint16_t data);

/**
 * @brief  从内部寄存器读取 16 位数据。
 *
 * @details
 * 采用两帧协议：
 *   - Frame F:   发送 READ_HWORD 命令（CS↑ 时生效）
 *   - Frame F+1: 发送 NOP，SDO 返回 D[31:16] 为寄存器数据
 *
 * @param  hadc   驱动句柄指针
 * @param  reg    目标寄存器地址
 * @param  pData  [out] 读取结果输出指针（16 位）
 *
 * @retval ADS8681_OK       读取成功，*pData 有效
 * @retval ADS8681_ERROR    SPI 通信错误
 * @retval ADS8681_TIMEOUT  SPI 通信超时
 */
ads8681_status_t ADS8681_ReadReg(ADS8681_HandleTypeDef* hadc, uint8_t reg, uint16_t* pData);

/**
 * @brief  设置 ADC 输入量程。
 *
 * @details 写入 RANGE_SEL_REG (0x14)。可选值见 ADS8681_RANGE_* 宏。
 *
 * @param  hadc   驱动句柄指针
 * @param  range  量程编码（如 ADS8681_RANGE_BIPOLAR_3VREF）
 *
 * @retval ADS8681_OK       设置成功
 * @retval ADS8681_ERROR    SPI 通信错误
 * @retval ADS8681_TIMEOUT  SPI 通信超时
 */
ads8681_status_t ADS8681_SetRange(ADS8681_HandleTypeDef* hadc, uint8_t range);

/**
 * @brief  阻塞式读取一帧 ADC 转换结果。
 *
 * @details
 * 发送 NOP 命令，读取 SDO 返回的 32 位输出字，提取 D[31:16]
 * 作为 16 位转换结果。CS↑ 同时触发下一次转换。
 *
 * @note   Init 后的第一次读取返回的是陈旧数据，应丢弃。
 *
 * @param  hadc    驱动句柄指针
 * @param  pResult [out] 转换结果输出指针（16 位原始码）
 *
 * @retval ADS8681_OK       读取成功，*pResult 有效
 * @retval ADS8681_ERROR    SPI 通信错误
 * @retval ADS8681_TIMEOUT  SPI 通信超时
 */
ads8681_status_t ADS8681_ReadRaw(ADS8681_HandleTypeDef* hadc, uint16_t* pResult);

/**
 * @brief  启动 DMA 非阻塞式 ADC 读取。
 *
 * @details
 * 发送 NOP 命令（DMA 方式），CS↑ 触发下一次转换。
 * DMA 完成后需在平台回调中调用 ADS8681_DMA_RxCpltCallback()。
 *
 * @note   rx_buf 在 DMA 完成前必须保持有效，不可释放或复用。
 *         转换结果位于 rx_buf[0..1]（MSB 先出，大端序）。
 *
 * @param  hadc    驱动句柄指针
 * @param  rx_buf  用户提供的 4 字节接收缓冲区
 *
 * @retval ADS8681_OK       DMA 传输已启动
 * @retval ADS8681_ERROR    参数错误或 SPI 启动失败
 * @retval ADS8681_BUSY     上一次 DMA 传输尚未完成
 */
ads8681_status_t ADS8681_ReadRaw_DMA(ADS8681_HandleTypeDef* hadc, uint8_t rx_buf[4]);

/**
 * @brief  底层阻塞式 SPI 帧交换（4 字节，MSB 先出）。
 *
 * @details
 * 完整流程：CS↓ → SPI 全双工收发 4 字节 → CS↑。
 * CS↑ 使当前命令生效并触发下一次转换。
 *
 * @param  hadc     驱动句柄指针
 * @param  tx_cmd   32 位命令字（如 ADS8681_CMD_NOP）
 * @param  rx_data  [out] 4 字节接收数据输出（可为 NULL 表示丢弃）
 *
 * @retval ADS8681_OK       传输成功
 * @retval ADS8681_ERROR    SPI 通信错误
 * @retval ADS8681_TIMEOUT  SPI 通信超时
 */
ads8681_status_t ADS8681_TransferFrame(ADS8681_HandleTypeDef* hadc, uint32_t tx_cmd, uint8_t rx_data[4]);

/**
 * @brief  底层 DMA 式 SPI 帧发送启动（4 字节，MSB 先出）。
 *
 * @details
 * CS↓ 后启动 DMA 全双工传输。传输完成前 CS 保持低电平。
 * 必须在 DMA 完成回调中调用 ADS8681_DMA_RxCpltCallback() 释放 CS。
 *
 * @param  hadc    驱动句柄指针
 * @param  tx_cmd  32 位命令字
 *
 * @retval ADS8681_OK       DMA 传输已启动
 * @retval ADS8681_ERROR    pRxBuf 为 NULL 或 SPI 启动失败
 * @retval ADS8681_BUSY     上一次 DMA 传输尚未完成
 */
ads8681_status_t ADS8681_TransferFrame_DMA(ADS8681_HandleTypeDef* hadc, uint32_t tx_cmd);

/**
 * @brief  DMA 接收完成回调（在平台 SPI DMA 完成 ISR 中调用）。
 *
 * @details
 * 拉高 CS（命令生效 + 触发下次转换），清除 dma_busy 标志。
 *
 * @param  hadc  驱动句柄指针
 *
 * @return 无
 */
void ADS8681_DMA_RxCpltCallback(ADS8681_HandleTypeDef* hadc);

#ifdef __cplusplus
}
#endif

#endif /* DRV_ADS8681_H_ */
