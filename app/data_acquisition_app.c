/**
 * @file    data_acquisition_app.c
 * @brief   数据采集应用实现：TIM2 + SPI1 DMA 全中断驱动 32×32 矩阵扫描。
 *
 * @details
 * 架构概述（方案 B：零 CPU 占用采样）：
 *
 *   TIM2 ISR (可配置周期)
 *     ├─ 行首: HC595 切换行激励 + 死区延时
 *     ├─ column_select(col) 列选通（CD74HC4067 占位）
 *     └─ CS↓ + 启动 SPI1 TX+RX DMA（发送 NOP，接收 4 字节）
 *
 *   SPI1 DMA 完成 ISR (~14µs 后)
 *     ├─ CS↑（触发 ADS8681 下次转换）
 *     ├─ 将上一次转换结果写入前一列（首传输丢弃、末传输冲刷）
 *     └─ 推进流水线步骤 / 行号 / 帧完成信号量
 *
 *   扫描线程 (每帧醒一次)
 *     ├─ frame_pool_get() 获取空帧
 *     ├─ 等待扫描完成
 *     └─ rt_mb_send() 投递给 USART2 线程
 *
 * 参数管理：
 *   - 默认参数通过宏定义配置
 *   - 运行时参数从 Flash 加载（params_init()）
 *   - 参数验证失败时自动回退到默认值并输出 LOG_E
 *
 * @author  Firmware Team
 * @version 2.3
 * @date    2026-07-30
 */

#include "data_acquisition_app.h"
#include "data_acquisition_pipeline.h"
#include "bsp_basicService.h"
#include "debug_log.h"
#include "spi.h"
#include "tim.h"
#include "dma.h"
/* <! USB→USART2 migration 2026-08-06: replaced usbd_cdc_if.h with usart2_dataport_app.h */
#include "usart2_dataport_app.h"
#include "product.h"

/* ==========================================================================
 *  私有常量
 * ========================================================================== */

/** @brief TIM2 预分频：72 MHz / (71+1) = 1 MHz 计数频率 */
#define SCAN_TIM_PSC           71U

/** @brief TIM2 自动重装值：1 MHz / 100 µs = 100 计数 → ARR = 99 */
#define SCAN_TIM_ARR_DEFAULT   (SCAN_CHANNEL_PERIOD_US_DEFAULT - 1U)

/* ========================================================================== 
 *  Change: 新增
 *  Editor: 谢峰
 *  Time: 2026-09-14
 *  Range: 按前端区分每行 SPI 传输次数，ADS 保留冲刷帧，TPC 每通道一帧
 * ========================================================================== */
#ifdef BSP_USE_ADS8681
#define SCAN_TRANSFERS_PER_ROW(cols) SCAN_PIPELINE_TRANSFER_COUNT(cols)
#else
/* ==========================================================================
 *  Change: 修改
 *  Editor: 谢峰
 *  Time: 2026-09-16
 *  Range: TPC5120每行计算一轮丢弃和一轮有效采集的总传输次数
 * ========================================================================== */
#define SCAN_TRANSFERS_PER_ROW(cols) SCAN_TPC_TRANSFER_COUNT(cols)
#endif

/* ==========================================================================
 *  私有变量：动态扫描参数
 * ========================================================================== */

/** @brief 当前扫描行数（从 Flash 加载或使用默认值） */
static uint16_t s_scan_rows = SCAN_ROWS_DEFAULT;

/** @brief 当前扫描列数（从 Flash 加载或使用默认值） */
static uint16_t s_scan_cols = SCAN_COLS_DEFAULT;

/** @brief 当前每通道采样周期 µs（从帧率计算或使用默认值） */
static uint16_t s_scan_period_us = SCAN_CHANNEL_PERIOD_US_DEFAULT;

/** @brief 当前扫描起始行 */
static uint16_t s_scan_row_start = 0U;

/** @brief 当前扫描起始列 */
static uint16_t s_scan_col_start = 0U;

/* ==========================================================================
 *  私有变量：硬件操作
 * ========================================================================== */

/* ========================================================================== 
 *  Change: 修改
 *  Editor: 谢峰
 *  Time: 2026-09-14
 *  Range: ADS 使用原 4 字节缓冲区，TPC 使用驱动句柄内置 2 字节 DMA 缓冲区
 * ========================================================================== */
#ifdef BSP_USE_ADS8681
/* --- SPI1 DMA 发送缓冲区（NOP 命令，全零） --- */
static uint8_t s_tx_nop[4] = {0x00, 0x00, 0x00, 0x00};

/* --- SPI1 DMA 接收缓冲区（4 字节，ISR 中读取） --- */
static volatile uint8_t s_rx_buf[4];
#else
static volatile uint8_t s_tpc_sync_discard_count;
static volatile uint8_t s_scan_fault;
/* ==========================================================================
 *  Change: 新增
 *  Editor: 谢峰
 *  Time: 2026-09-16
 *  Range: 记录TPC5120当前处于行切换后的丢弃轮或有效采集轮
 * ========================================================================== */
static volatile uint8_t s_tpc_phase = SCAN_TPC_PHASE_DISCARD;
#endif

/* --- SPI1 TX DMA 句柄（DMA1_Channel3，软件配置） --- */
DMA_HandleTypeDef hdma_spi1_tx;

/* --- 扫描状态机 --- */
/* Current frame being filled (acquired from the pool by the scan thread) */
static frame_buf_t *s_active_frame = RT_NULL;
static volatile uint16_t  s_row;                     /**< 当前行索引 [0, rows-1] */
/* ==========================================================================
 *  Change: 修改
 *  Editor: 谢峰
 *  Time: 2026-09-11
 *  Range: s_col 改为 ADS8681 流水线步骤 [0, cols]，删除无法表达逐列延迟的阶段变量
 * ========================================================================== */
static volatile uint16_t  s_col;                     /**< ADS 流水线步骤或 TPC 行内采样序号 */
static volatile uint8_t   s_scanning;                /**< 扫描进行中标志 */

/* --- RT-Thread IPC --- */
static rt_sem_t s_frame_done_sem = RT_NULL;          /**< 帧完成信号量 */
static uint16_t s_frame_counter = 0;                 /**< 帧计数器（打包用） */

/* ==========================================================================
 *  Change: 修改
 *  Editor: Thompson
 *  Time: 2026-08-26
 *  Range: 新增帧率统计（由 DBG_ENABLE 宏控制）：扫描线程每 2 秒统计实测帧率，
 *         从 USART1 console 打印，含生效 rows/cols/采样周期/TIM2 ARR
 * ========================================================================== */
#ifdef DBG_ENABLE
#define ACQ_FPS_STAT_PERIOD_TICKS   (RT_TICK_PER_SECOND * 2U)  /**< 帧率统计打印周期 (2 s) */
static uint32_t s_fps_frame_cnt;    /**< 当前统计窗口内完成的帧数 */
static uint32_t s_fps_tick_prev;    /**< 当前统计窗口起始 tick */
#endif /* DBG_ENABLE */

/* --- 扫描线程 --- */
static struct rt_thread s_scan_thread_obj;
static rt_uint8_t       s_scan_thread_stack[DATA_ACQ_THREAD_STACK_SIZE];

/* ==========================================================================
 *  [2026-07-30 新增] 静态缓冲区（替代 frame_pool）
 * ========================================================================== */

/** @brief 原始数据缓冲区（内部使用） */
static uint16_t s_frame_data[SCAN_ROWS_MAX * SCAN_COLS_MAX];

/* The mailbox path uses frame_pool buffers, so s_packed_buf is no longer used. */
/* static uint8_t s_packed_buf[FRAME_OVERHEAD + SCAN_ROWS_MAX * SCAN_COLS_MAX * 2]; */

/* ==========================================================================
 *  私有函数：列多路器选通（占位）
 * ========================================================================== */

/**
 * @brief  列多路器选通（CD74HC4067 × 2 级联）。
 *
 * @details
 * 根据列索引设置两片 CD74HC4067 的 S0~S3 地址线，
 * 将指定列电极连接到 ADS8681 模拟输入。
 *
 * @param  col  列索引 [0, 31]。
 *              0~15  → 第一片 4067
 *              16~31 → 第二片 4067
 *
 * @note   当前为空实现，下一轮填入 GPIO 驱动。
 */
/* ========================================================================== 
 *  Change: 修改
 *  Editor: 谢峰
 *  Time: 2026-09-14
 *  Range: 外部列多路器仅属于 ADS8681 旧前端
 * ========================================================================== */
#ifdef BSP_USE_ADS8681
static void column_select(uint16_t col)
{
    /* 驱动 CD74HC4067 地址线 */
    CD74HC4067_Select(col);
}
#endif

/* ==========================================================================
 *  私有函数：微秒级忙等待
 * ========================================================================== */

/**
 * @brief  微秒级忙等待（基于 SysTick 或 NOP 循环）。
 *
 * @param  us  等待时间 (µs)
 *
 * @note   在 ISR 中调用，不可使用 rt_thread_mdelay。
 *         72 MHz 主频下每次循环约 4 个时钟周期 ≈ 55 ns。
 */
static inline void busy_wait_us(uint32_t us)
{
    /* 72 MHz / 4 cycles per loop ≈ 18 次循环/µs */
    volatile uint32_t count = us * 18U;
    while (count--) {
        __asm volatile ("nop");
    }
}

/* ==========================================================================
 *  私有函数：SPI1 TX DMA 初始化
 * ========================================================================== */

/**
 * @brief  配置 SPI1 TX DMA（DMA1_Channel3，软件初始化）。
 *
 * @details
 * CubeMX 仅初始化了 SPI1 RX DMA（DMA1_Channel2），TX DMA 需要软件配置。
 * 用于发送 4 字节 NOP 命令，触发 ADS8681 转换并读取结果。
 */
static void scan_spi1_tx_dma_init(void)
{
    __HAL_RCC_DMA1_CLK_ENABLE();

    hdma_spi1_tx.Instance                 = DMA1_Channel3;
    hdma_spi1_tx.Init.Direction           = DMA_MEMORY_TO_PERIPH;
    hdma_spi1_tx.Init.PeriphInc           = DMA_PINC_DISABLE;
    hdma_spi1_tx.Init.MemInc              = DMA_MINC_ENABLE;
    hdma_spi1_tx.Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE;
    hdma_spi1_tx.Init.MemDataAlignment    = DMA_MDATAALIGN_BYTE;
    hdma_spi1_tx.Init.Mode                = DMA_NORMAL;
    hdma_spi1_tx.Init.Priority            = DMA_PRIORITY_HIGH;
    HAL_DMA_Init(&hdma_spi1_tx);

    __HAL_LINKDMA(&hspi1, hdmatx, hdma_spi1_tx);

    HAL_NVIC_SetPriority(DMA1_Channel3_IRQn, 1, 0);
    HAL_NVIC_EnableIRQ(DMA1_Channel3_IRQn);
}

/* ==========================================================================
 *  私有函数：TIM2 初始化（使用动态周期）
 * ========================================================================== */

/**
 * @brief  配置并启动 TIM2（可配置周期中断）。
 *
 * @details
 * PSC = 71 → 72 MHz / 72 = 1 MHz 计数频率
 * ARR = s_scan_period_us - 1 → 周期 = s_scan_period_us µs
 *
 * 例如：s_scan_period_us = 100 → ARR = 99 → 100 µs 周期
 */
static void scan_timer_init(void)
{
    TIM_ClockConfigTypeDef sClockSourceConfig = {0};
    TIM_MasterConfigTypeDef sMasterConfig = {0};

    htim2.Instance               = TIM2;
    htim2.Init.Prescaler         = SCAN_TIM_PSC;//预分频器值为71
    htim2.Init.CounterMode       = TIM_COUNTERMODE_UP;//向上计数
    htim2.Init.Period            = (uint32_t)(s_scan_period_us - 1U);//自动重装载值为周期减1
    htim2.Init.ClockDivision     = TIM_CLOCKDIVISION_DIV1;//时钟分频器值为1
    htim2.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;//使能自动重装载预加载
    HAL_TIM_Base_Init(&htim2);

    sClockSourceConfig.ClockSource = TIM_CLOCKSOURCE_INTERNAL;//时钟源选择内部时钟
    HAL_TIM_ConfigClockSource(&htim2, &sClockSourceConfig);

    sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
    sMasterConfig.MasterSlaveMode     = TIM_MASTERSLAVEMODE_DISABLE;
    HAL_TIMEx_MasterConfigSynchronization(&htim2, &sMasterConfig);

    HAL_NVIC_SetPriority(TIM2_IRQn, 0, 0);
    HAL_NVIC_EnableIRQ(TIM2_IRQn);

    HAL_TIM_Base_Start_IT(&htim2);
}

/* ==========================================================================
 *  TIM2 中断处理
 * ========================================================================== */

/**
 * @brief  TIM2 更新中断回调。
 *
 * @details
 * 每个定时周期触发一次，驱动扫描状态机：
 *   1. 行首：HC595 切换行激励 + 死区延时
 *   2. 列选通：CD74HC4067 选择列
 *   3. CS↓ + 启动 SPI1 DMA（发送 NOP，接收 4 字节）
 *
 * @param  htim  TIM 句柄（由 HAL 传入）
 */
/* ==========================================================================
 *  Change: 修改
 *  Editor: 谢峰
 *  Time: 2026-09-11
 *  Range: 按 ADS8681 单级流水线选择当前列，首传输预充、末传输保持最后一列冲刷
 * ========================================================================== */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
    if (htim->Instance != TIM2) {
        return;
    }

    if (!s_scanning) {//如果未扫描
        return;
    }

    /* ===== 行首: 切换行激励（只在每行第一次进入时执行一次）=====
     * 原理: 74HC595 的 32 路并行输出, 选通第 s_row 行通电, 其余 31 行接地
     * mask 计算: bit[n]=1 表示第 n 行通电
     * s_row + s_scan_row_start 支持扫描子区域 (比如跳过前 8 行) */
    /* ==========================================================================
     *  Change: 修改
     *  Editor: 谢峰
     *  Time: 2026-09-16
     *  Range: TPC仅在每行丢弃轮开始时切换行，并检查74HC595写入结果
     * ========================================================================== */
#ifdef BSP_USE_ADS8681
    if (s_col == 0U) {//每行第 0 步为预充传输
#else
    if ((s_col == 0U) && (s_tpc_phase == SCAN_TPC_PHASE_DISCARD)) {
#endif
        uint32_t mask = (1UL << (s_row + s_scan_row_start));
#ifdef BSP_USE_ADS8681
        HC595_Write32(mask);//写入HC595寄存器，使第s_row行通电，其余31行接地
#else
        if (HC595_Write32(mask) != HC595_OK) {
            s_scan_fault = 1U;
            s_scanning = 0U;
            LOG_E("[ACQ] HC595 row select failed, row=%u", (unsigned int)s_row);
            rt_sem_release(s_frame_done_sem);
            return;
        }
#endif
        /* 等待分压网络稳定: 行通电后, 32 个分压网络需要 ~8µs 建立稳定电压 */
        busy_wait_us(SCAN_DEAD_TIME_US);
    }

    /* ====================================================================== 
     *  Change: 修改
     *  Editor: 谢峰
     *  Time: 2026-09-14
     *  Range: ADS 保留外部 MUX 四字节流水线，TPC 改为 Auto-2 两字节连续操作帧
     * ====================================================================== */
#ifdef BSP_USE_ADS8681
    /* ===== 列选通: 步骤 0..cols-1 依次选列，步骤 cols 保持最后一列 =====
     * ADS8681 本次 SPI 返回上一次 CS 上升沿启动的转换结果。
     * 最后一步不能选择范围外通道，必须保持末列以读回末列结果。 */
    uint16_t mux_col = SCAN_PIPELINE_MUX_COL(s_col, s_scan_cols);
    column_select(mux_col + s_scan_col_start);

    /* ===== 列切换后等待 4067 导通电阻稳定 + ADC 内部采样保持稳定 =====
     * 之前这里没有等待导致 col0→col1 串扰, 加 100µs 解决 */
    busy_wait_us(10);

    /* ===== CS↓: 拉低 ADS8681 片选, 准备 SPI 通信 =====
     * 注意: CS↓ 标志帧开始, 但 ADS8681 真正开始转换是在 CS↑ 之后 */
    HAL_GPIO_WritePin(ADC_CS_PORT, ADC_CS_PIN, GPIO_PIN_RESET);

    /* ===== 启动 SPI1 TX+RX DMA =====
     * SPI1 全双工: MOSI 发 4 字节 NOP (触发流水线),
     *             MISO 同时收 4 字节 (上一次转换的结果)
     * DMA 硬件自动搬运, CPU 完全空闲, ~14µs 后完成中断 */
    HAL_SPI_TransmitReceive_DMA(&hspi1, s_tx_nop, (uint8_t *)s_rx_buf, 4);
#else
    if (TPC5120S16_TransferFrameDMA(&g_bsp_tpc5120,
                                    TPC5120S16_CMD_CONTINUE)
        != TPC5120S16_STATUS_OK) {
        s_scan_fault = 1U;
        s_scanning = 0U;
        LOG_E("[ACQ] TPC5120S16 DMA start failed");
        rt_sem_release(s_frame_done_sem);
    }
#endif
}

/* ==========================================================================
 *  SPI DMA 完成回调
 * ========================================================================== */

/**
 * @brief  SPI1 TX+RX DMA 完成回调。
 *
 * @details
 * 在 DMA 完成中断中执行，耗时极短：
 *   -# CS↑（触发 ADS8681 下次转换）
 *   -# 根据当前阶段丢弃或存储数据
 *   -# 推进扫描状态机
 *   -# 帧完成时释放信号量唤醒扫描线程
 *
 * @param  hspi  SPI 句柄（由 HAL 传入）
 *
 * @note   运行在 ISR 上下文（DMA1_CH2/CH3 → HAL_DMA_IRQHandler → HAL）。
 */
void HAL_SPI_TxRxCpltCallback(SPI_HandleTypeDef *hspi)
{
    if (hspi->Instance != SPI1) {
        return;
    }

    /* ====================================================================== 
     *  Change: 修改
     *  Editor: 谢峰
     *  Time: 2026-09-14
     *  Range: 按选定前端解析 ADS 流水线结果或 TPC 通道头+12位采样值
     * ====================================================================== */
#ifdef BSP_USE_ADS8681
    /* ===== CS↑: ADS8681 的核心设计 =====
     * 上升沿同时完成两件事:
     *   1. 锁存并执行 MCU 刚发的命令 (本帧 NOP)
     *   2. 触发下一次 ADC 转换 (模拟前端开始工作)
     * 这样 "读上一个结果 + 转下一个样本" 完全并行, 零等待! */
    HAL_GPIO_WritePin(ADC_CS_PORT, ADC_CS_PIN, GPIO_PIN_SET);

    /* ===== 第 0 步仅预充流水线 =====
     * 当前返回值属于上一行；CS 上升沿才开始转换本行第 0 列。 */
    if (!SCAN_PIPELINE_HAS_SAMPLE(s_col)) {
        s_col = 1U;
        return;
    }

    /* ===== 提取 16 bit 有效数据 =====
     * ADS8681 返回 32 bit 帧: 前 16 bit (D[31:16]) 是有效转换码, 大端序
     * DMA 把 4 字节写入 s_rx_buf, 其中 buf[0]=MSB, buf[1]=LSB */
    uint16_t raw = ((uint16_t)s_rx_buf[0] << 8) | (uint16_t)s_rx_buf[1];

    /* ===== 写入前一列 =====
     * 步骤 N 选中列 N，但读回的是步骤 N-1 启动的转换结果。
     * 步骤 cols 是末列冲刷传输，因此仍写入 cols-1。 */
    uint16_t sample_col = SCAN_PIPELINE_SAMPLE_COL(s_col);
    s_active_frame->data[s_row * s_scan_cols + sample_col] = raw;

    /* ===== 推进扫描状态机 =====
     * 当前列读完 → col++
     *   col 到末列 → col=0, row++ (换到下一行)
     *     row 到末行 → 帧完成, 释放信号量唤醒扫描线程 */
    if (SCAN_PIPELINE_ROW_COMPLETE(s_col, s_scan_cols)) {
        s_col = 0U;
        s_row++;
        if (s_row >= s_scan_rows) {
            /* 帧扫描完成: 复位状态, 释放信号量让扫描线程打包帧 */
            s_row      = 0U;
            s_scanning = 0U;
            rt_sem_release(s_frame_done_sem);
            return;
        }
    } else {
        s_col++;
    }
#else
    {
        uint16_t frame;
        uint8_t channel;

        if (TPC5120S16_DMAComplete(&g_bsp_tpc5120, &frame)
            != TPC5120S16_STATUS_OK) {
            s_scan_fault = 1U;
            s_scanning = 0U;
            LOG_E("[ACQ] TPC5120S16 DMA completion failed");
            rt_sem_release(s_frame_done_sem);
            return;
        }

        /* ==================================================================
         *  Change: 修改
         *  Editor: 谢峰
         *  Time: 2026-09-16
         *  Range: 每次行切换后丢弃完整CH0至末通道扫描，再保存下一轮有效数据
         * ================================================================== */
        channel = TPC5120S16_ParseChannel(frame);
        if (channel >= s_scan_cols) {
            s_scan_fault = 1U;
            s_scanning = 0U;
            LOG_E("[ACQ] TPC5120S16 channel %u outside CH0..CH%u",
                  (unsigned int)channel, (unsigned int)(s_scan_cols - 1U));
            rt_sem_release(s_frame_done_sem);
            return;
        }

        if (channel != (uint8_t)s_col) {
            s_tpc_sync_discard_count++;
            if (s_tpc_sync_discard_count >= BSP_TPC5120S16_SYNC_ATTEMPTS) {
                s_scan_fault = 1U;
                s_scanning = 0U;
                LOG_E("[ACQ] TPC5120S16 sequence failed, expected=%u channel=%u",
                      (unsigned int)s_col, (unsigned int)channel);
                rt_sem_release(s_frame_done_sem);
                return;
            }
            s_col = 0U;
            s_tpc_phase = SCAN_TPC_PHASE_DISCARD;
            if (channel != 0U) {
                return;
            }
        }

        s_tpc_sync_discard_count = 0U;
        if (SCAN_TPC_SHOULD_STORE(s_tpc_phase)) {
            s_active_frame->data[s_row * s_scan_cols + channel] =
                TPC5120S16_ParseCode(frame);
        }

        if (SCAN_TPC_DISCARD_COMPLETE(s_tpc_phase, channel, s_scan_cols)) {
            s_tpc_phase = SCAN_TPC_PHASE_READ;
            s_col = 0U;
            return;
        }

        if (SCAN_TPC_ROW_COMPLETE(s_tpc_phase, channel, s_scan_cols)) {
            s_tpc_phase = SCAN_TPC_PHASE_DISCARD;
            s_col = 0U;
            s_row++;
            if (s_row >= s_scan_rows) {
                s_row = 0U;
                s_scanning = 0U;
                rt_sem_release(s_frame_done_sem);
            }
        } else {
            s_col++;
        }
    }
#endif
}

/* ========================================================================== 
 *  Change: 新增
 *  Editor: 谢峰
 *  Time: 2026-09-14
 *  Range: SPI1 DMA 异常时释放 TPC 片选并停止当前扫描，防止 CS 长时间保持低电平
 * ========================================================================== */
void HAL_SPI_ErrorCallback(SPI_HandleTypeDef *hspi)
{
    if (hspi->Instance != SPI1) {
        return;
    }

#ifdef BSP_USE_TPC5120S16
    TPC5120S16_DMAAbort(&g_bsp_tpc5120);
    if (s_scanning != 0U) {
        s_scan_fault = 1U;
        s_scanning = 0U;
        LOG_E("[ACQ] TPC5120S16 SPI DMA error");
        rt_sem_release(s_frame_done_sem);
    }
#else
    HAL_GPIO_WritePin(ADC_CS_PORT, ADC_CS_PIN, GPIO_PIN_SET);
#endif
}

/* ==========================================================================
 *  扫描线程入口
 * ========================================================================== */

/**
 * @brief  Data acquisition scan thread.
 *
 * @details
 * - Get an empty frame buffer from the pool (blocks until the USART2
 *   thread returns the previous buffer).
 * - Start TIM2 + SPI1 DMA interrupt-driven scan.
 * - Wait for the frame-done semaphore.
 * - Send the completed frame to the mailbox for the USART2 data-port thread.
 *
 * @param  param  Unused
 */
static void data_acquisition_thread_entry(void *param)
{
    (void)param;

    while (1) {
        /* ===== 从帧缓冲池取一个空帧 =====
         * 邮箱路径: frame_pool_get() 会阻塞直到 USART2 线程归还了一个空闲缓冲
         * 这样保证扫描线程不会覆盖还没发完的旧帧 */
        s_active_frame = frame_pool_get();

        /* ===== 启动新的一帧扫描 =====
         * 状态机初始化为 row=0、流水线步骤=0，第一步预充列0并丢弃旧结果。
         * s_scanning=1 → TIM2 ISR 开始响应 */
        /* ==========================================================================
         *  Change: 修改
         *  Editor: 谢峰
         *  Time: 2026-09-11
         *  Range: 新帧从流水线预充步骤 0 开始，不再使用行级 DISCARD/READ 阶段
         * ========================================================================== */
        s_row      = 0U;
        s_col      = 0U;
#ifdef BSP_USE_TPC5120S16
        s_scan_fault = 0U;
        s_tpc_sync_discard_count = 0U;
        /* ==================================================================
         *  Change: 修改
         *  Editor: 谢峰
         *  Time: 2026-09-16
         *  Range: 每个新帧从第0行的TPC完整丢弃轮开始
         * ================================================================== */
        s_tpc_phase = SCAN_TPC_PHASE_DISCARD;
#endif
        s_scanning = 1U;

        /* ===== 阻塞等待帧完成 =====
         * TIM2 ISR + SPI DMA ISR 在后台零 CPU 占用地扫描整个矩阵
         * 扫描完最后一个像素时 DMA ISR 会 rt_sem_release(s_frame_done_sem)
         * 这里才被唤醒 */
        rt_sem_take(s_frame_done_sem, RT_WAITING_FOREVER);

        /* ================================================================== 
         *  Change: 新增
         *  Editor: 谢峰
         *  Time: 2026-09-14
         *  Range: TPC 通道失步或 DMA 故障时停止扫描并归还未完成帧
         * ================================================================== */
#ifdef BSP_USE_TPC5120S16
        if (s_scan_fault != 0U) {
            frame_pool_put(s_active_frame);
            s_active_frame = RT_NULL;
            LOG_E("[ACQ] TPC5120S16 scan stopped after frontend fault");
            return;
        }
#endif

        /* ==========================================================================
         *  Change: 修改
         *  Editor: 谢峰
         *  Time: 2026-09-11
         *  Range: 更新帧格式说明：帧尾由16位累加和升级为小端CRC32
         * ========================================================================== */
        /* ===== 原地打包帧数据 =====
         * 扫描线程把原始 uint16_t[] 帧数据, 原地打包成带协议头的字节流:
         *   [0xFF 0x66] + [计数器MSB] + [计数器LSB] + [rows*cols*2字节大端数据] + [CRC32小端]
         * pack_len 输出打包后的总字节数, 赋给 frame->len 供 USART2 DMA 使用 */
        uint32_t pack_len = 0;
        if (product_pack_frame_inplace(s_active_frame->data, s_scan_rows, s_scan_cols,
                                       s_frame_counter++, &pack_len) == 0) {
            s_active_frame->len = (uint16_t)pack_len;   /* len = 打包后字节数 */
        } else {
            LOG_E("[ACQ] Frame pack failed, sending raw.");
            /* 打包失败则退回发送原始数据 (没有帧头, 上位机会丢帧) */
            s_active_frame->len = (uint16_t)(s_scan_rows * s_scan_cols * sizeof(uint16_t));
        }

        /* ===== 投递帧指针到邮箱 =====
         * rt_mb_send 只传 32 位指针, 零内存拷贝
         * USART2 发送线程被唤醒 → rt_mb_recv 拿到指针 → DMA 发出去 */
        rt_mb_send(frame_mb, (rt_ubase_t)s_active_frame);

#ifdef DBG_ENABLE
        /* 帧率统计: 每 2 秒打印一次到 console (USART1), 用 rt_tick_get() 计时 */
        s_fps_frame_cnt++;
        {
            uint32_t now_tick = rt_tick_get();
            uint32_t elapsed = now_tick - s_fps_tick_prev;
            if (elapsed >= ACQ_FPS_STAT_PERIOD_TICKS) {
                uint32_t fps = (s_fps_frame_cnt * RT_TICK_PER_SECOND) / elapsed;
                LOG_I("[ACQ] measured fps=%u rows=%u cols=%u period=%uus ARR=%u",
                      fps, s_scan_rows, s_scan_cols, s_scan_period_us,
                      (unsigned int)__HAL_TIM_GET_AUTORELOAD(&htim2));
                s_fps_frame_cnt = 0U;
                s_fps_tick_prev = now_tick;
            }
        }
#endif /* DBG_ENABLE */
    }
}

/* ==========================================================================
 *  参数验证和载入
 * ========================================================================== */

/**
 * @brief  从 Flash 加载扫描参数并验证。
 *
 * @details
 * 从 params_get() 获取 Flash 存储的参数，验证其合理性：
 *   - 行范围：检查 start <= end 且在 [0, SCAN_ROWS_MAX-1] 范围内
 *   - 列范围：检查 start <= end 且在 [0, SCAN_COLS_MAX-1] 范围内
 *   - 帧率：检查在 [SCAN_FPS_MIN, SCAN_FPS_MAX] 范围内
 *   - 采样周期：根据帧率计算，确保在 [SCAN_CHANNEL_PERIOD_US_MIN, SCAN_CHANNEL_PERIOD_US_MAX] 范围内
 *
 * 验证失败时：
 *   - 使用 LOG_E 输出错误信息（包含错误原因和修改建议）
 *   - 自动回退到默认参数
 *
 * @return true 所有参数有效，false 部分参数使用默认值
 */
bool scan_load_params(void)
{
    const sys_params_t *params = params_get();
    bool valid = true;

    /* 验证行范围 */
    if (params->scan_row_start > params->scan_row_end) {
        LOG_E("[ACQ] Invalid row range: start(%u) > end(%u). "
              "Use 'param_row <start> <end>' to set valid range. "
              "Using default rows [%u, %u].",
              params->scan_row_start, params->scan_row_end,
              0, SCAN_ROWS_DEFAULT - 1);
        s_scan_row_start = 0U;
        s_scan_rows = SCAN_ROWS_DEFAULT;
        valid = false;
    } else if (params->scan_row_end >= SCAN_ROWS_MAX) {
        LOG_E("[ACQ] Row end(%u) exceeds max(%u). "
              "Use 'param_row <start> <end>' with end <= %u. "
              "Using default rows [%u, %u].",
              params->scan_row_end, SCAN_ROWS_MAX - 1, SCAN_ROWS_MAX - 1,
              0, SCAN_ROWS_DEFAULT - 1);
        s_scan_row_start = 0U;
        s_scan_rows = SCAN_ROWS_DEFAULT;
        valid = false;
    } else {
        s_scan_row_start = params->scan_row_start;
        s_scan_rows = params->scan_row_end - params->scan_row_start + 1;
        if (s_scan_rows < SCAN_ROWS_MIN) {
            LOG_E("[ACQ] Row range too small(%u), minimum is %u. "
                  "Use 'param_row <start> <end>' to set valid range. "
                  "Using default rows [%u, %u].",
                  s_scan_rows, SCAN_ROWS_MIN,
                  0, SCAN_ROWS_DEFAULT - 1);
            s_scan_row_start = 0U;
            s_scan_rows = SCAN_ROWS_DEFAULT;
            valid = false;
        }
    }

    /* 验证列范围 */
    if (params->scan_col_start > params->scan_col_end) {
        LOG_E("[ACQ] Invalid col range: start(%u) > end(%u). "
              "Use 'param_col <start> <end>' to set valid range. "
              "Using default cols [%u, %u].",
              params->scan_col_start, params->scan_col_end,
              0, SCAN_COLS_DEFAULT - 1);
        s_scan_col_start = 0U;
        s_scan_cols = SCAN_COLS_DEFAULT;
        valid = false;
    } else if (params->scan_col_end >= SCAN_COLS_MAX) {
        LOG_E("[ACQ] Col end(%u) exceeds max(%u). "
              "Use 'param_col <start> <end>' with end <= %u. "
              "Using default cols [%u, %u].",
              params->scan_col_end, SCAN_COLS_MAX - 1, SCAN_COLS_MAX - 1,
              0, SCAN_COLS_DEFAULT - 1);
        s_scan_col_start = 0U;
        s_scan_cols = SCAN_COLS_DEFAULT;
        valid = false;
    } else {
        s_scan_col_start = params->scan_col_start;
        s_scan_cols = params->scan_col_end - params->scan_col_start + 1;
        if (s_scan_cols < SCAN_COLS_MIN) {
            LOG_E("[ACQ] Col range too small(%u), minimum is %u. "
                  "Use 'param_col <start> <end>' to set valid range. "
                  "Using default cols [%u, %u].",
                  s_scan_cols, SCAN_COLS_MIN,
                  0, SCAN_COLS_DEFAULT - 1);
            s_scan_col_start = 0U;
            s_scan_cols = SCAN_COLS_DEFAULT;
            valid = false;
        }
    }

    /* ====================================================================== 
     *  Change: 新增
     *  Editor: 谢峰
     *  Time: 2026-09-14
     *  Range: TPC Auto-2 仅支持从 CH0 起始，其他列起点回退为默认范围
     * ====================================================================== */
#ifdef BSP_USE_TPC5120S16
    if ((s_scan_col_start != 0U) || (s_scan_cols > TPC5120S16_CHANNEL_COUNT)) {
        LOG_W("[ACQ] TPC5120S16 Auto-2 requires CH0 start and at most 16 channels; "
              "using cols [0,%u].", (unsigned int)(SCAN_COLS_DEFAULT - 1U));
        s_scan_col_start = 0U;
        s_scan_cols = SCAN_COLS_DEFAULT;
        valid = false;
    }
#endif

    /* 验证帧率并计算采样周期 */
    if (params->scan_fps < SCAN_FPS_MIN || params->scan_fps > SCAN_FPS_MAX) {
        LOG_E("[ACQ] Invalid FPS(%u), valid range [%u, %u]. "
              "Use 'param_fps <fps>' to set valid FPS. "
              "Using default FPS %u.",
              params->scan_fps, SCAN_FPS_MIN, SCAN_FPS_MAX, SCAN_FPS_DEFAULT);
        /* 使用默认帧率计算采样周期 */
        uint32_t period = 1000000U /
            (s_scan_rows * SCAN_TRANSFERS_PER_ROW(s_scan_cols) * SCAN_FPS_DEFAULT);
        if (period < SCAN_CHANNEL_PERIOD_US_MIN) {
            period = SCAN_CHANNEL_PERIOD_US_MIN;
        } else if (period > SCAN_CHANNEL_PERIOD_US_MAX) {
            period = SCAN_CHANNEL_PERIOD_US_MAX;
        }
        s_scan_period_us = (uint16_t)period;
        valid = false;
    } else {
        /* 根据帧率计算每通道采样周期 */
        uint32_t period = 1000000U /
            (s_scan_rows * SCAN_TRANSFERS_PER_ROW(s_scan_cols) * params->scan_fps);
        if (period < SCAN_CHANNEL_PERIOD_US_MIN) {
            LOG_E("[ACQ] Calculated period(%u us) too small for FPS %u with %ux%u matrix. "
                  "Minimum period is %u us. "
                  "Use 'param_fps <fps>' to reduce FPS or 'param_row'/'param_col' to reduce matrix size. "
                  "Using minimum period %u us.",
                  period, params->scan_fps, s_scan_rows, s_scan_cols,
                  SCAN_CHANNEL_PERIOD_US_MIN, SCAN_CHANNEL_PERIOD_US_MIN);
            period = SCAN_CHANNEL_PERIOD_US_MIN;
            valid = false;
        } else if (period > SCAN_CHANNEL_PERIOD_US_MAX) {
            LOG_E("[ACQ] Calculated period(%u us) too large for FPS %u with %ux%u matrix. "
                  "Maximum period is %u us. "
                  "Use 'param_fps <fps>' to increase FPS. "
                  "Using maximum period %u us.",
                  period, params->scan_fps, s_scan_rows, s_scan_cols,
                  SCAN_CHANNEL_PERIOD_US_MAX, SCAN_CHANNEL_PERIOD_US_MAX);
            period = SCAN_CHANNEL_PERIOD_US_MAX;
            valid = false;
        }
        s_scan_period_us = (uint16_t)period;
    }

    if (valid) {
        LOG_I("[ACQ] Parameters loaded: rows[%u,%u]=%u, cols[%u,%u]=%u, fps=%u, period=%u us",
              s_scan_row_start, s_scan_row_start + s_scan_rows - 1, s_scan_rows,
              s_scan_col_start, s_scan_col_start + s_scan_cols - 1, s_scan_cols,
              params->scan_fps, s_scan_period_us);
    }

    return valid;
}

/**
 * @brief  获取当前扫描行数。
 *
 * @return 当前扫描行数
 */
uint16_t scan_get_rows(void)
{
    return s_scan_rows;
}

/**
 * @brief  获取当前扫描列数。
 *
 * @return 当前扫描列数
 */
uint16_t scan_get_cols(void)
{
    return s_scan_cols;
}

/**
 * @brief  获取当前每通道采样周期。
 *
 * @return 当前采样周期 (µs)
 */
uint16_t scan_get_period_us(void)
{
    return s_scan_period_us;
}

/* ==========================================================================
 *  公共 API
 * ========================================================================== */

/**
 * @brief  初始化并启动数据采集子系统。
 *
 * @details
 * 初始化顺序：
 *   -# 从 Flash 加载并验证扫描参数
 *   -# 配置 SPI1 TX DMA（DMA1_Channel3）
 *   -# 创建帧完成信号量
 *   -# 配置并启动 TIM2（使用动态周期）
 *   -# 启动扫描线程
 *
 * @note   必须在 params_init() 之后调用。
 *         TIM2 启动后扫描立即开始。
 */
void DATA_ACQUISITION_Init(void)
{
    rt_err_t ret;

    /* 1. 从 Flash 加载并验证扫描参数 */
    if (!scan_load_params()) {
        LOG_W("[ACQ] Some parameters invalid, using defaults where needed.");
    }

    /* ====================================================================== 
     *  Change: 新增
     *  Editor: 谢峰
     *  Time: 2026-09-14
     *  Range: 根据生效列数重新配置 TPC Auto-2 末通道并对齐下一帧到 CH0
     * ====================================================================== */
#ifdef BSP_USE_TPC5120S16
    {
        tpc5120s16_status_t status = TPC5120S16_ConfigureAuto2(
            &g_bsp_tpc5120, (uint8_t)(s_scan_cols - 1U),
            BSP_TPC5120S16_DEFAULT_RANGE);
        if (status == TPC5120S16_STATUS_OK) {
            status = TPC5120S16_SynchronizeToChannel0(
                &g_bsp_tpc5120, BSP_TPC5120S16_SYNC_ATTEMPTS);
        }
        if (status != TPC5120S16_STATUS_OK) {
            LOG_E("[ACQ] TPC5120S16 scan setup failed, status=%u",
                  (unsigned int)status);
            return;
        }
    }
#endif

    /* 2. 配置 SPI1 TX DMA (DMA1_Channel3) */
    scan_spi1_tx_dma_init();

    /* 3. 创建帧完成信号量（初始值 0，DMA ISR 释放） */
    s_frame_done_sem = rt_sem_create("frm_done", 0U, RT_IPC_FLAG_FIFO);
    RT_ASSERT(s_frame_done_sem != RT_NULL);

    /* 4. 配置并启动 TIM2（使用动态周期） */
    scan_timer_init();

    /* 5. 启动扫描线程 */
    ret = rt_thread_init(&s_scan_thread_obj,       //创建扫描线程
                         DATA_ACQ_THREAD_NAME,
                         data_acquisition_thread_entry,
                         RT_NULL,
                         s_scan_thread_stack,
                         DATA_ACQ_THREAD_STACK_SIZE,
                         DATA_ACQ_THREAD_PRIORITY,
                         10);
    RT_ASSERT(ret == RT_EOK);
    if (ret == RT_EOK) {
        rt_thread_startup(&s_scan_thread_obj);//启动扫描线程
    }
}

/**
 * @brief  运行时修改每通道采样周期。
 *
 * @details
 * 修改 TIM2 自动重装寄存器 (ARR)，下一个定时周期生效。
 * PSC 固定为 71（计数频率 1 MHz），因此 ARR = period_us - 1。
 *
 * @param  period_us  新采样周期 (µs)，有效范围 [10, 10000]。
 */
void scan_set_channel_period_us(uint16_t period_us)
{
    if (period_us < SCAN_CHANNEL_PERIOD_US_MIN) {
        period_us = SCAN_CHANNEL_PERIOD_US_MIN;
    } else if (period_us > SCAN_CHANNEL_PERIOD_US_MAX) {
        period_us = SCAN_CHANNEL_PERIOD_US_MAX;
    }
    s_scan_period_us = period_us;
    __HAL_TIM_SET_AUTORELOAD(&htim2, (uint32_t)(period_us - 1U));
}

/**
 * @brief  [2026-07-30 修改] 调试命令：检查数据帧内容
 *
 * @details
 * 使用静态缓冲区 s_frame_data 而不是 s_active_frame
 */
void dbg_check_dataFrame(int argc, char **argv)
{
    LOG_D("Checking data frame...");
    LOG_D("Frame data buffer: %p", (void *)s_frame_data);
    for (uint8_t i = 0; i < 6; i++)
    {
        for (uint8_t j = 0; j < 6; j++)
        {
            LOG_D("%d ", s_frame_data[i * s_scan_cols + j]);
        }
        LOG_D("\n");
    }
}
MSH_CMD_EXPORT(dbg_check_dataFrame, check data frame);
