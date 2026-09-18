/**
 * @file    drv_ads8681.c
 * @brief   ADS8681/ADS8685/ADS8689 16 位 SAR ADC 驱动实现。
 *
 * @details
 * 平台无关的驱动逻辑，所有硬件操作通过平台抽象层宏完成
 * （见 drv_ads8681_port_stm32.h / drv_ads8681_port_gd32.h）。
 *
 * 通信协议要点（SBAS633E, Section 7）：
 *   - 每帧固定 32 位（4 字节），MSB 先出
 *   - 命令在 CS↑（上升沿）时被器件锁存并执行
 *   - SDO 输出的是上一帧命令对应的响应数据（一帧延迟）
 *   - CS↑ 同时触发下一次 ADC 转换
 *   - 寄存器读需要两帧：Frame F 发 READ 命令，Frame F+1 发 NOP 取数据
 *
 * @author  Firmware Team
 * @version 1.1
 * @date    2026-07-21
 */

#include "drv_ads8681.h"
#include <string.h>

/* ==========================================================================
 *  私有辅助函数
 *  ========================================================================== */

/**
 * @brief  拉低 CS 引脚（激活 ADC，准备 SPI 通信）。
 *
 * @param  hadc  驱动句柄指针
 * @return 无
 */
static void ADS8681_CS_Low(ADS8681_HandleTypeDef* hadc)
{
    ads8681_gpio_write(hadc->cs_port, hadc->cs_pin, ADS8681_GPIO_RESET);
}

/**
 * @brief  拉高 CS 引脚（命令生效 + 触发下一次转换）。
 *
 * @param  hadc  驱动句柄指针
 * @return 无
 */
static void ADS8681_CS_High(ADS8681_HandleTypeDef* hadc)
{
    ads8681_gpio_write(hadc->cs_port, hadc->cs_pin, ADS8681_GPIO_SET);
}

/**
 * @brief  将 4 字节大端序 SPI 接收缓冲区转换为 32 位整数。
 *
 * @param  buf  4 字节输入缓冲区（buf[0] 为 MSB）
 * @return 32 位无符号整数
 */
static uint32_t ADS8681_BytesToU32(const uint8_t buf[4])
{
    return (((uint32_t)buf[0]) << 24) |
           (((uint32_t)buf[1]) << 16) |
           (((uint32_t)buf[2]) <<  8) |
           ((uint32_t)buf[3]);
}

/**
 * @brief  将 32 位命令字拆分为 4 字节大端序 SPI 发送缓冲区。
 *
 * @param  cmd  32 位命令字
 * @param  buf  [out] 4 字节输出缓冲区（buf[0] 为 MSB）
 * @return 无
 */
static void ADS8681_U32ToBytes(uint32_t cmd, uint8_t buf[4])
{
    buf[0] = (uint8_t)(cmd >> 24);
    buf[1] = (uint8_t)(cmd >> 16);
    buf[2] = (uint8_t)(cmd >>  8);
    buf[3] = (uint8_t)(cmd);
}

/* ==========================================================================
 *  公共 API 实现
 *  ========================================================================== */

/**
 * @brief  初始化 ADS8681 驱动并执行硬件复位。
 *
 * @details
 * 初始化序列：
 *   -# CS 拉高（非激活）、RST 拉高（非复位态）
 *   -# 清除 DMA 相关状态（pRxBuf=NULL, dma_busy=0）
 *   -# 执行硬件复位（RST 脉冲 + 25 ms POR 等待）
 *   -# 回读 DEVICE_ID 寄存器验证 SPI 链路正常
 *
 * @note   复位后默认配置：SPI Mode 0、量程 ±3×Vref、内部基准使能。
 *
 * @param  hadc  驱动句柄指针（需预先填充 hspi/cs/rst/rvs 字段）
 * @return 无
 */
void ADS8681_Init(ADS8681_HandleTypeDef* hadc)
{
    /* 确保 CS 和 RST 起始为非激活态 */
    #ifdef ADS8681_DBG_ENABLE
    LOG_D("ADS8681 Init: CS↑, RST↑.");
    #endif
    ADS8681_CS_High(hadc);
    ads8681_gpio_write(hadc->rst_port, hadc->rst_pin, ADS8681_GPIO_SET);
    #ifdef ADS8681_DBG_ENABLE
    LOG_D("Clear DMA status.");
    #endif
    /* 清除 DMA 状态 */
    hadc->pRxBuf   = NULL;
    hadc->dma_busy = 0U;

    /* 执行硬件复位 */
    #ifdef ADS8681_DBG_ENABLE
    LOG_D("ADS8681 HW Reset.");
    #endif
    ADS8681_Reset(hadc);

    /* 回读 DEVICE_ID 验证 SPI 通信正常（可选断言） */
    uint16_t reg_val;
    if (ADS8681_ReadReg(hadc, ADS8681_REG_DEVICE_ID, &reg_val) == ADS8681_OK)
    {
        uint8_t device_id = (uint8_t)(reg_val & 0x000FU);  // 提取 bits[3:0]
        /* ==========================================================================
         *  Change: 修改
         *  Editor: 谢峰
         *  Time: 2026-09-18
         *  Range: 调试日志关闭时显式保留 DEVICE_ID 回读，避免未使用变量警告
         * ========================================================================== */
        (void)device_id;
        #ifdef ADS8681_DBG_ENABLE
        LOG_D("DEVICE_ID: %d", device_id);
        #endif
    }

}

/**
 * @brief  执行硬件复位（RST 引脚脉冲 + POR 等待）。
 *
 * @details
 * 时序要求（数据手册 Section 6.7）：
 *   - RST 低脉冲宽度: ≥ 100 ns（实际拉低 1 ms，远超最小值）
 *   - POR 恢复时间 tD_RST_POR: 最大 20 ms（实际等待 25 ms）
 *
 * @param  hadc  驱动句柄指针
 * @return 无
 */
void ADS8681_Reset(ADS8681_HandleTypeDef* hadc)
{
    /* RST 拉低（有效），保持 1 ms >> 100 ns 最小要求 */
    ads8681_gpio_write(hadc->rst_port, hadc->rst_pin, ADS8681_GPIO_RESET);
    ads8681_delay_ms(1);

    /* RST 释放（拉高），等待 POR 完成 */
    ads8681_gpio_write(hadc->rst_port, hadc->rst_pin, ADS8681_GPIO_SET);
    ads8681_delay_ms(25);   /* > tD_RST_POR(max) = 20 ms */
}

/**
 * @brief  查询转换是否完成（非阻塞，读 RVS 引脚）。
 *
 * @param  hadc  驱动句柄指针
 * @retval 1  转换完成（RVS = HIGH），器件空闲可接收下一帧
 * @retval 0  转换进行中（RVS = LOW）
 */
uint8_t ADS8681_IsConvReady(ADS8681_HandleTypeDef* hadc)
{
    return (ads8681_gpio_read(hadc->rvs_port, hadc->rvs_pin) == ADS8681_GPIO_SET) ? 1U : 0U;
}

/**
 * @brief  阻塞等待转换完成（轮询 RVS 引脚）。
 *
 * @details
 * 持续读取 RVS 直到高电平或超过 timeout_ms。
 * 计时基于平台 tick（STM32 为 HAL_GetTick，分辨率 1 ms）。
 *
 * @param  hadc        驱动句柄指针
 * @param  timeout_ms  超时时间（毫秒）
 *
 * @retval ADS8681_OK       转换完成
 * @retval ADS8681_TIMEOUT  超时未就绪
 */
ads8681_status_t ADS8681_WaitConvReady(ADS8681_HandleTypeDef* hadc, uint32_t timeout_ms)
{
    uint32_t start = ads8681_get_tick();

    while (!ADS8681_IsConvReady(hadc))
    {
        if ((ads8681_get_tick() - start) > timeout_ms)
        {
            return ADS8681_TIMEOUT;
        }
    }
    return ADS8681_OK;
}

/**
 * @brief  底层阻塞式 SPI 帧交换（4 字节全双工）。
 *
 * @details
 * 完整时序：
 *   -# CS↓（激活 ADC）
 *   -# SPI 全双工收发 4 字节（32 个 SCK）
 *   -# CS↑（命令生效 + 触发下次转换）
 *   -# 若 rx_data 非 NULL，拷贝接收数据
 *
 * @param  hadc     驱动句柄指针
 * @param  tx_cmd   32 位命令字
 * @param  rx_data  [out] 4 字节接收输出（NULL 表示丢弃接收数据）
 *
 * @retval ADS8681_OK       传输成功
 * @retval ADS8681_ERROR    SPI 返回错误
 * @retval ADS8681_TIMEOUT  SPI 超时
 */
ads8681_status_t ADS8681_TransferFrame(ADS8681_HandleTypeDef* hadc, uint32_t tx_cmd, uint8_t rx_data[4])
{
    uint8_t tx[4];
    uint8_t rx[4];
    ads8681_status_t status;

    /* 将 32 位命令字拆分为 4 字节大端序 */
    ADS8681_U32ToBytes(tx_cmd, tx);

    /* CS↓ → SPI 收发 → CS↑ */
    ADS8681_CS_Low(hadc);
    status = ads8681_spi_trx(hadc->hspi, tx, rx, 4U, ADS8681_SPI_TIMEOUT_FOREVER);
    ADS8681_CS_High(hadc);

    /* 拷贝接收数据（若调用者需要） */
    if ((status == ADS8681_OK) && (rx_data != NULL))
    {
        memcpy(rx_data, rx, 4U);
    }

    return status;
}

/**
 * @brief  底层 DMA 式 SPI 帧发送启动（4 字节全双工，非阻塞）。
 *
 * @details
 * CS↓ 后启动 DMA 传输，CS 保持低电平直到 DMA 完成。
 * 调用者必须在 DMA 完成 ISR 中调用 ADS8681_DMA_RxCpltCallback()
 * 以释放 CS 并清除 busy 标志。
 *
 * @param  hadc    驱动句柄指针
 * @param  tx_cmd  32 位命令字
 *
 * @retval ADS8681_OK       DMA 传输已启动
 * @retval ADS8681_ERROR    pRxBuf 为 NULL 或 SPI/DMA 启动失败
 * @retval ADS8681_BUSY     上一次 DMA 传输尚未完成
 */
ads8681_status_t ADS8681_TransferFrame_DMA(ADS8681_HandleTypeDef* hadc, uint32_t tx_cmd)
{
    ads8681_status_t status;

    /* 检查是否有正在进行的 DMA 传输 */
    if (hadc->dma_busy)
    {
        return ADS8681_BUSY;
    }

    /* 检查接收缓冲区有效性 */
    if (hadc->pRxBuf == NULL)
    {
        return ADS8681_ERROR;
    }

    /* 标记忙、打包命令、拉低 CS、启动 DMA */
    hadc->dma_busy = 1U;
    ADS8681_U32ToBytes(tx_cmd, hadc->txBuf);
    ADS8681_CS_Low(hadc);

    status = ads8681_spi_trx_dma(hadc->hspi, hadc->txBuf, hadc->pRxBuf, 4U);
    if (status != ADS8681_OK)
    {
        /* 启动失败：恢复状态 */
        hadc->dma_busy = 0U;
        ADS8681_CS_High(hadc);
    }

    return status;
}

/**
 * @brief  DMA 接收完成回调。
 *
 * @details
 * 在平台 SPI DMA 完成中断中调用（如 HAL_SPI_TxRxCpltCallback）。
 * 执行：CS↑（命令生效 + 触发下次转换）→ 清除 dma_busy。
 *
 * @param  hadc  驱动句柄指针
 * @return 无
 */
void ADS8681_DMA_RxCpltCallback(ADS8681_HandleTypeDef* hadc)
{
    if (hadc->dma_busy)
    {
        ADS8681_CS_High(hadc);
        hadc->dma_busy = 0U;
    }
}

/**
 * @brief  向内部寄存器写入 16 位数据。
 *
 * @details
 * 使用 WRITE_HWORD 命令。若目标为 RST_PWRCTL (0x04)，
 * 自动注入 WKEY=0x69 到 D[15:8]，并屏蔽 reserved bits[7:6]，
 * 调用者只需提供 bits[5:0] 有效控制位。
 *
 * @param  hadc  驱动句柄指针
 * @param  reg   目标寄存器地址
 * @param  data  待写入数据（对 RST_PWRCTL 仅 bits[5:0] 有效）
 *
 * @retval ADS8681_OK       写入成功
 * @retval ADS8681_ERROR    SPI 通信错误
 * @retval ADS8681_TIMEOUT  SPI 通信超时
 */
ads8681_status_t ADS8681_WriteReg(ADS8681_HandleTypeDef* hadc, uint8_t reg, uint16_t data)
{
    uint32_t cmd;

    if (reg == ADS8681_REG_RST_PWRCTL)
    {
        /* WKEY[15:8]=0x69 为写保护密钥；bits[7:6] 为 reserved 必须为 0 */
        data = (data & 0x003FU) | ADS8681_PWRCTL_WKEY_VALUE;
    }

    cmd = ADS8681_CMD_WRITE_HWORD(reg, data);
    return ADS8681_TransferFrame(hadc, cmd, NULL);
}

/**
 * @brief  从内部寄存器读取 16 位数据（两帧协议）。
 *
 * @details
 * ADS868x 的寄存器读需要两帧 SPI 通信：
 *   - Frame F:   发送 READ_HWORD 命令（CS↑ 时命令生效）
 *   - Frame F+1: 发送 NOP，SDO 在 D[31:16] 返回寄存器内容
 *
 * @param  hadc   驱动句柄指针
 * @param  reg    目标寄存器地址
 * @param  pData  [out] 16 位读取结果输出指针
 *
 * @retval ADS8681_OK       读取成功，*pData 有效
 * @retval ADS8681_ERROR    SPI 通信错误
 * @retval ADS8681_TIMEOUT  SPI 通信超时
 */
ads8681_status_t ADS8681_ReadReg(ADS8681_HandleTypeDef* hadc, uint8_t reg, uint16_t* pData)
{
    uint8_t rx[4];
    uint32_t cmd;
    ads8681_status_t status;

    /* Frame F: 发送 READ_HWORD 命令（输出为上次转换结果，丢弃） */
    cmd = ADS8681_CMD_READ_HWORD(reg);
    status = ADS8681_TransferFrame(hadc, cmd, NULL);
    if (status != ADS8681_OK)
    {
        return status;
    }

    /* Frame F+1: 发送 NOP，SDO 返回寄存器数据在 D[31:16] */
    status = ADS8681_TransferFrame(hadc, ADS8681_CMD_NOP, rx);
    if (status != ADS8681_OK)
    {
        return status;
    }

    *pData = (uint16_t)(ADS8681_BytesToU32(rx) >> 16U);
    return ADS8681_OK;
}

/**
 * @brief  设置 ADC 输入量程。
 *
 * @details
 * 写入 RANGE_SEL_REG (0x14)，有效掩码 0x4F（bits[6] INTREF_DIS + bits[3:0] 量程）。
 *
 * @param  hadc   驱动句柄指针
 * @param  range  量程编码（见 ADS8681_RANGE_* 宏）
 *
 * @retval ADS8681_OK       设置成功
 * @retval ADS8681_ERROR    SPI 通信错误
 * @retval ADS8681_TIMEOUT  SPI 通信超时
 */
ads8681_status_t ADS8681_SetRange(ADS8681_HandleTypeDef* hadc, uint8_t range)
{
    /* 0x4F = bit6(INTREF_DIS) + bits[3:0](range code)，屏蔽无效位 */
    uint16_t val = (uint16_t)(range & 0x4FU);
    return ADS8681_WriteReg(hadc, ADS8681_REG_RANGE_SEL, val);
}

/**
 * @brief  阻塞式读取一帧 ADC 转换结果。
 *
 * @details
 * 发送 NOP 命令读取 SDO 32 位输出字，提取 D[31:16] 为 16 位转换码。
 * CS↑ 同时触发下一次转换（连续采样模式）。
 *
 * @note   Init 后第一次调用返回陈旧数据（复位期间采样），应丢弃。
 *
 * @param  hadc    驱动句柄指针
 * @param  pResult [out] 16 位转换结果输出指针
 *
 * @retval ADS8681_OK       读取成功，*pResult 有效
 * @retval ADS8681_ERROR    SPI 通信错误
 * @retval ADS8681_TIMEOUT  SPI 通信超时
 */
ads8681_status_t ADS8681_ReadRaw(ADS8681_HandleTypeDef* hadc, uint16_t* pResult)
{
    uint8_t rx[4];
    uint32_t result;
    ads8681_status_t status;

    status = ADS8681_TransferFrame(hadc, ADS8681_CMD_NOP, rx);
    if (status != ADS8681_OK)
    {
        return status;
    }

    result = ADS8681_BytesToU32(rx);

    /* 默认 DATAOUT_CTL 配置下，16 位转换结果位于 D[31:16] */
    *pResult = (uint16_t)(result >> 16U);
    return ADS8681_OK;
}

/**
 * @brief  启动 DMA 非阻塞式 ADC 读取。
 *
 * @details
 * 将用户提供的 rx_buf 绑定到句柄，然后以 DMA 方式发送 NOP 命令。
 * DMA 完成后在平台回调中调用 ADS8681_DMA_RxCpltCallback() 释放 CS。
 *
 * @note   转换结果位于 rx_buf[0..1]（大端序：rx_buf[0]=MSB）。
 *         rx_buf 在 DMA 完成前不可释放或复用。
 *
 * @param  hadc    驱动句柄指针
 * @param  rx_buf  用户提供的 4 字节接收缓冲区
 *
 * @retval ADS8681_OK       DMA 传输已启动
 * @retval ADS8681_ERROR    rx_buf 为 NULL 或 DMA 启动失败
 * @retval ADS8681_BUSY     上一次 DMA 传输尚未完成
 */
ads8681_status_t ADS8681_ReadRaw_DMA(ADS8681_HandleTypeDef* hadc, uint8_t rx_buf[4])
{
    if (rx_buf == NULL)
    {
        return ADS8681_ERROR;
    }

    hadc->pRxBuf = rx_buf;
    return ADS8681_TransferFrame_DMA(hadc, ADS8681_CMD_NOP);
}

/* ==========================================================================
 *  使用示例（供应用层参考）
 *  ==========================================================================
 *
 * 1. 硬件配置（CubeMX / STM32）：
 *      - SPI1: 全双工主模式, 8-bit, CPOL=0, CPHA=0, MSB 先出
 *      - GPIO 输出: CS/CONVST (PA4, 低有效), RST (低有效, 空闲高)
 *      - GPIO 输入: RVS (PA3, 转换完成指示)
 *
 * 2. 句柄声明与初始化：
 *
 *      ADS8681_HandleTypeDef hadc = {
 *          .hspi     = &hspi1,
 *          .cs_port  = ADC_CS_GPIO_Port,
 *          .cs_pin   = ADC_CS_Pin,
 *          .rst_port = ADC_RST_GPIO_Port,
 *          .rst_pin  = ADC_RST_Pin,
 *          .rvs_port = ADC_RVS_GPIO_Port,
 *          .rvs_pin  = ADC_RVS_Pin,
 *      };
 *      ADS8681_Init(&hadc);
 *      ADS8681_SetRange(&hadc, ADS8681_RANGE_BIPOLAR_3VREF);
 *
 * 3. 阻塞式单次采样：
 *
 *      uint16_t raw;
 *      ADS8681_WaitConvReady(&hadc, 2);
 *      ADS8681_ReadRaw(&hadc, &raw);     // 读取结果，同时触发下次转换
 *
 * 4. DMA 连续采样（RVS EXTI 驱动）：
 *
 *      // RVS 上升沿 EXTI ISR 中：
 *      ADS8681_ReadRaw_DMA(&hadc, adc_buf);
 *
 *      // SPI DMA 完成回调中：
 *      ADS8681_DMA_RxCpltCallback(&hadc);
 *      uint16_t raw = ((uint16_t)adc_buf[0] << 8) | adc_buf[1];
 *
 * ==========================================================================*/
