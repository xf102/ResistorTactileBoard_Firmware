/**
 * @file scan_service.c
 * @brief TIM2/SPI1 driven matrix scan with frame-boundary buffer switching.
 */

#include "scan_service.h"

#include <string.h>

#include "adc_frontend.h"
#include "app_events.h"
#include "bsp_config.h"
#include "drv_74hc595.h"
#include "frame_service.h"
#include "tim.h"

#define SCAN_PERIOD_US_MIN  100U
#define SCAN_PERIOD_US_MAX  65535U
#define SCAN_PERIOD_US_DEFAULT  100U
#define SCAN_ROW_SETTLE_US  15U

static volatile scan_state_t s_state;
static volatile uint16_t s_row;
static volatile uint16_t s_step;
static uint16_t s_rows;
static uint16_t s_cols;
static uint16_t s_period_us;
static volatile bool s_discarding;
static frame_buffer_t *s_active_frame;
static volatile scan_stats_t s_stats;

/* ==========================================================================
 *  Change: 新增
 *  Editor: 谢峰
 *  Time: 2026-09-18
 *  Range: 提供72MHz Cortex-M3下行激励稳定等待，仅在每行首个定时中断执行
 * ========================================================================== */
static void scan_delay_us(uint32_t delay_us)
{
    volatile uint32_t loops = delay_us * 18U;
    while (loops > 0U) {
        --loops;
        __NOP();
    }
}

/* ==========================================================================
 *  Change: 新增
 *  Editor: 谢峰
 *  Time: 2026-09-18
 *  Range: 在完整帧边界发布当前帧并决定下一帧写入或整帧丢弃
 * ========================================================================== */
static void scan_finish_frame(void)
{
    if (s_discarding) {
        ++s_stats.frames_dropped;
    } else if ((s_active_frame == NULL) ||
               !frame_publish_raw(s_active_frame, s_rows, s_cols)) {
        ++s_stats.frontend_sync_errors;
        app_events_set(APP_EVENT_SCAN_FAULT);
        return;
    } else {
        ++s_stats.frames_acquired;
    }

    s_active_frame = frame_acquire_for_scan();
    s_discarding = (s_active_frame == NULL);
    s_row = 0U;
    s_step = 0U;
}

/* ==========================================================================
 *  Change: 新增
 *  Editor: 谢峰
 *  Time: 2026-09-18
 *  Range: 初始化扫描参数和前端软件状态，尚不启动TIM2
 * ========================================================================== */
bool scan_init(void)
{
    s_state = SCAN_STATE_STOPPED;
    s_rows = BSP_MATRIX_ROWS;
    s_cols = BSP_MATRIX_COLS;
    s_period_us = SCAN_PERIOD_US_DEFAULT;
    s_row = 0U;
    s_step = 0U;
    s_discarding = false;
    s_active_frame = NULL;
    memset((void *)&s_stats, 0, sizeof(s_stats));
    return adc_frontend_init() && adc_frontend_configure(s_cols);
}

/* ==========================================================================
 *  Change: 新增
 *  Editor: 谢峰
 *  Time: 2026-09-18
 *  Range: 从完整帧边界获取缓冲并启动TIM2周期采样
 * ========================================================================== */
bool scan_start(void)
{
    if (s_state != SCAN_STATE_STOPPED) {
        return false;
    }

    s_state = SCAN_STATE_STARTING;
    s_active_frame = frame_acquire_for_scan();
    if (s_active_frame == NULL) {
        s_state = SCAN_STATE_FAULT;
        return false;
    }

    s_row = 0U;
    s_step = 0U;
    s_discarding = false;
    __HAL_TIM_SET_AUTORELOAD(&htim2, (uint32_t)s_period_us - 1U);
    __HAL_TIM_SET_COUNTER(&htim2, 0U);
    if (HAL_TIM_Base_Start_IT(&htim2) != HAL_OK) {
        (void)frame_discard_acquisition(s_active_frame);
        s_active_frame = NULL;
        s_state = SCAN_STATE_FAULT;
        return false;
    }

    s_state = SCAN_STATE_RUNNING;
    return true;
}

/* ==========================================================================
 *  Change: 新增
 *  Editor: 谢峰
 *  Time: 2026-09-18
 *  Range: 停止定时器和SPI事务，丢弃半帧并关闭全部行激励
 * ========================================================================== */
void scan_stop(void)
{
    if (s_state == SCAN_STATE_STOPPED) {
        return;
    }

    s_state = SCAN_STATE_STOPPING;
    (void)HAL_TIM_Base_Stop_IT(&htim2);
    adc_frontend_abort();
    if (s_active_frame != NULL) {
        (void)frame_discard_acquisition(s_active_frame);
        s_active_frame = NULL;
    }
    (void)HC595_Write32(0U);
    s_row = 0U;
    s_step = 0U;
    s_discarding = false;
    s_state = SCAN_STATE_STOPPED;
}

/* ==========================================================================
 *  Change: 新增
 *  Editor: 谢峰
 *  Time: 2026-09-18
 *  Range: 在主循环处理ISR上报的扫描故障，保证不发送半帧
 * ========================================================================== */
void scan_process(void)
{
    if (app_events_take(APP_EVENT_SCAN_FAULT) != 0U) {
        scan_stop();
        s_state = SCAN_STATE_FAULT;
    }
}

/* ==========================================================================
 *  Change: 新增
 *  Editor: 谢峰
 *  Time: 2026-09-18
 *  Range: 停止扫描后原子应用矩阵尺寸和TIM2周期，再从新帧恢复
 * ========================================================================== */
bool scan_reconfigure(uint16_t rows, uint16_t cols, uint16_t period_us)
{
    const bool was_running = (s_state == SCAN_STATE_RUNNING);

    if ((rows == 0U) || (rows > BSP_MATRIX_ROWS) ||
        (cols == 0U) || (cols > BSP_MATRIX_COLS) ||
        (period_us < SCAN_PERIOD_US_MIN) ||
        (period_us > SCAN_PERIOD_US_MAX)) {
        return false;
    }

    if (was_running) {
        scan_stop();
    } else if (s_state != SCAN_STATE_STOPPED) {
        return false;
    }

    s_rows = rows;
    s_cols = cols;
    s_period_us = period_us;
    if (!adc_frontend_configure(cols)) {
        s_state = SCAN_STATE_FAULT;
        return false;
    }

    return !was_running || scan_start();
}

/* ==========================================================================
 *  Change: 新增
 *  Editor: 谢峰
 *  Time: 2026-09-18
 *  Range: TIM2 ISR中切换行激励、等待模拟稳定并启动一个ADC DMA步骤
 * ========================================================================== */
void scan_on_timer_irq(void)
{
    if (s_state != SCAN_STATE_RUNNING) {
        return;
    }
    if (!adc_frontend_is_ready()) {
        ++s_stats.spi_busy_errors;
        return;
    }

    if (s_step == 0U) {
        /* ==========================================================================
         *  Change: 修改
         *  Editor: 谢峰
         *  Time: 2026-09-18
         *  Range: TIM2 ISR 使用有限轮询接口，避免 HAL 超时等待低优先级 SysTick
         * ========================================================================== */
        if (HC595_Write32_ISR(1UL << s_row) != HC595_OK) {
            ++s_stats.spi_dma_errors;
            app_events_set(APP_EVENT_SCAN_FAULT);
            return;
        }
        scan_delay_us(SCAN_ROW_SETTLE_US);
        if (!adc_frontend_prepare_row(s_row)) {
            ++s_stats.frontend_sync_errors;
            app_events_set(APP_EVENT_SCAN_FAULT);
            return;
        }
    }

    if (!adc_frontend_start_step(s_step)) {
        ++s_stats.spi_dma_errors;
        app_events_set(APP_EVENT_SCAN_FAULT);
    }
}

/* ==========================================================================
 *  Change: 新增
 *  Editor: 谢峰
 *  Time: 2026-09-18
 *  Range: SPI1 DMA完成ISR中保存样本并推进列、行和完整帧边界
 * ========================================================================== */
void scan_on_spi_complete_irq(void)
{
    const adc_frontend_result_t result = adc_frontend_on_dma_complete();

    if (s_state != SCAN_STATE_RUNNING) {
        return;
    }
    if (result.code == ADC_FRONTEND_RESULT_ERROR) {
        ++s_stats.spi_dma_errors;
        app_events_set(APP_EVENT_SCAN_FAULT);
        return;
    }
    if (result.code == ADC_FRONTEND_RESULT_PENDING) {
        s_step = 1U;
        return;
    }
    if (result.column >= s_cols) {
        ++s_stats.frontend_sync_errors;
        app_events_set(APP_EVENT_SCAN_FAULT);
        return;
    }

    if (!s_discarding && (s_active_frame != NULL)) {
        s_active_frame->storage[(uint32_t)s_row * s_cols + result.column] =
            result.sample;
    }

    if (result.row_complete) {
        s_step = 0U;
        ++s_row;
        if (s_row >= s_rows) {
            scan_finish_frame();
        }
    } else {
        ++s_step;
    }
}

/* ==========================================================================
 *  Change: 新增
 *  Editor: 谢峰
 *  Time: 2026-09-18
 *  Range: SPI HAL错误回调中仅记录计数并发布主循环恢复事件
 * ========================================================================== */
void scan_on_spi_error_irq(void)
{
    ++s_stats.spi_dma_errors;
    app_events_set(APP_EVENT_SCAN_FAULT);
}

/* ==========================================================================
 *  Change: 新增
 *  Editor: 谢峰
 *  Time: 2026-09-18
 *  Range: 在短临界区复制一致的扫描配置、进度和统计快照
 * ========================================================================== */
void scan_get_status(scan_status_t *status)
{
    uint32_t primask;

    if (status == NULL) {
        return;
    }

    primask = __get_PRIMASK();
    __disable_irq();
    status->state = s_state;
    status->rows = s_rows;
    status->cols = s_cols;
    status->period_us = s_period_us;
    status->row = s_row;
    status->step = s_step;
    status->discarding = s_discarding;
    memcpy(&status->stats, (const void *)&s_stats, sizeof(status->stats));
    if (primask == 0U) {
        __enable_irq();
    }
}

/* ==========================================================================
 *  Change: 新增
 *  Editor: 谢峰
 *  Time: 2026-09-18
 *  Range: 清除运行统计但不改变扫描状态和当前帧所有权
 * ========================================================================== */
void scan_clear_stats(void)
{
    const uint32_t primask = __get_PRIMASK();
    __disable_irq();
    memset((void *)&s_stats, 0, sizeof(s_stats));
    if (primask == 0U) {
        __enable_irq();
    }
}
