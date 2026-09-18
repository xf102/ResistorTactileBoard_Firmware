/**
 * @file    data_acquisition_app.h
 * @brief   数据采集应用：32×32 压阻触觉矩阵扫描。
 *
 * @details
 * 采用 TIM2 + SPI1 DMA 全中断驱动架构（方案 B）：
 *   - TIM2 提供可配置的每通道采样时基
 *   - TIM2 ISR 驱动 74HC595 行切换 + 启动 SPI1 DMA 读取 ADS8681
 *   - DMA 完成 ISR 提取数据、推进状态机
 *   - 扫描线程仅管理帧缓冲池和邮箱投递（每帧醒一次）
 *
 * 参数来源：
 *   - Flash 存储的系统参数（通过 params_init() 加载）
 *   - 运行时 msh 命令修改（param_row/param_col/param_fps）
 *   - 参数验证失败时使用默认值
 *
 * @author  Firmware Team
 * @version 2.2
 * @date    2026-07-28
 */

#ifndef DATA_ACQUISITION_APP_H_
#define DATA_ACQUISITION_APP_H_

#include <stdint.h>
#include <stdbool.h>
#include "bsp_init.h"   /* bsp_config.h -> RTT + BSP drivers + BasicService (frame_buf_t, frame_mb) */
#ifdef __cplusplus
extern "C" {
#endif

/* --------------------------------------------------------------------------
 *  扫描线程配置
 * -------------------------------------------------------------------------- */
#define DATA_ACQ_THREAD_NAME       "data_acq"    /**< 线程名称 */
#define DATA_ACQ_THREAD_STACK_SIZE 2048          /**< 栈大小 (字节) */
#define DATA_ACQ_THREAD_PRIORITY   5             /**< 优先级 (数值越小越高) */

/* --------------------------------------------------------------------------
 *  扫描矩阵参数（默认值，用于参数验证失败时回退）
 * -------------------------------------------------------------------------- */
/* ==========================================================================
 *  Change: 修改
 *  Editor: 谢峰
 *  Time: 2026-09-10
 *  Range: 扫描默认尺寸和范围改由 BSP 矩阵规格宏统一控制
 * ========================================================================== */
#define SCAN_ROWS_DEFAULT           BSP_MATRIX_ROWS /**< 默认行数 */
#define SCAN_COLS_DEFAULT           BSP_MATRIX_COLS /**< 默认列数 */
#define SCAN_CHANNELS_PER_FRAME     (SCAN_ROWS_DEFAULT * SCAN_COLS_DEFAULT) /**< 每帧总通道数 */

/* 兼容性宏定义（保留旧名称，指向默认值，debug用，正式固件需屏蔽） */
#define SCAN_ROWS                   SCAN_ROWS_DEFAULT
#define SCAN_COLS                   SCAN_COLS_DEFAULT

/* --------------------------------------------------------------------------
 *  参数范围限制
 * -------------------------------------------------------------------------- */
#define SCAN_ROWS_MIN               1U            /**< 最小行数 */
#define SCAN_ROWS_MAX               BSP_MATRIX_ROWS /**< 当前产品最大行数 */
#define SCAN_COLS_MIN               1U            /**< 最小列数 */
#define SCAN_COLS_MAX               BSP_MATRIX_COLS /**< 当前产品最大列数 */
#define SCAN_CHANNEL_PERIOD_US_MIN  10U           /**< 最小采样周期 (µs) */
#define SCAN_CHANNEL_PERIOD_US_MAX  65535U        /**< 最大采样周期 (µs) */
#define SCAN_FPS_MIN                1U            /**< 最小帧率 */
#define SCAN_FPS_MAX                100U          /**< 最大帧率 */

/* --------------------------------------------------------------------------
 *  时基与死区配置（默认值）
 * -------------------------------------------------------------------------- */
#define SCAN_CHANNEL_PERIOD_US_DEFAULT  100U      /**< 默认每通道采样周期 (µs) */
#define SCAN_FPS_DEFAULT                10U       /**< 默认帧率 */
/* ==========================================================================
 *  Change: 修改
 *  Editor: 谢峰
 *  Time: 2026-09-14
 *  Range: 将行激励切换后的模拟稳定时间由 15 µs 增加到 1000 µs，用于验证换行瞬态串扰
 * ========================================================================== */
#define SCAN_DEAD_TIME_US               15U     /**< 行激励切换后模拟稳定死区 (µs) */

/**
 * @brief  初始化并启动数据采集子系统。
 *
 * @details
 * 执行以下初始化：
 *   -# 从 Flash 加载并验证扫描参数
 *   -# 配置 SPI1 TX DMA（DMA1_Channel3）
 *   -# 创建帧完成信号量
 *   -# 配置并启动 TIM2（使用动态周期）
 *   -# 启动扫描线程
 *
 * @note   必须在 bsp_frame_pool_init() 和 params_init() 之后调用。
 */
void DATA_ACQUISITION_Init(void);

/**
 * @brief  运行时修改每通道采样周期。
 *
 * @details
 * 修改 TIM2 自动重装值，下一个定时周期生效。
 * 计算公式：ARR = period_us - 1（PSC=71 时，计数频率 = 1 MHz）。
 *
 * @param  period_us  新采样周期 (µs)，有效范围 [10, 10000]。
 */



void scan_set_channel_period_us(uint16_t period_us);

/**
 * @brief  从 Flash 加载扫描参数并验证。
 *
 * @details
 * 从 params_get() 获取 Flash 存储的参数，验证其合理性：
 *   - 行/列范围：检查 start <= end 且在 [0, 31] 范围内
 *   - 帧率：检查在 [1, 100] 范围内，并计算对应的采样周期
 *   - 采样周期：确保在 [10, 10000] µs 范围内
 *
 * 验证失败时：
 *   - 使用 LOG_E 输出错误信息（包含错误原因和修改建议）
 *   - 自动回退到默认参数
 *
 * @return true 所有参数有效，false 部分参数使用默认值
 */
bool scan_load_params(void);

/**
 * @brief  获取当前扫描参数。
 *
 * @return 当前扫描行数
 */
uint16_t scan_get_rows(void);

/**
 * @brief  获取当前扫描列数。
 *
 * @return 当前扫描列数
 */
uint16_t scan_get_cols(void);

/**
 * @brief  获取当前每通道采样周期。
 *
 * @return 当前采样周期 (µs)
 */
uint16_t scan_get_period_us(void);

#ifdef __cplusplus
}
#endif

#endif /* DATA_ACQUISITION_APP_H_ */
