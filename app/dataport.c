/**
 * @file dataport.c
 * @brief Pack complete ADC frames and transmit them through USART2 DMA.
 */

#include "dataport.h"

#include <stddef.h>
#include <string.h>

#include "app_events.h"
#include "frame_protocol.h"
#include "frame_service.h"
#include "usart.h"

#define DATAPORT_TX_TIMEOUT_MS  100U

static frame_buffer_t *s_tx_frame;
static uint16_t s_frame_counter;
static uint32_t s_tx_started_tick;
static dataport_stats_t s_stats;

/* ==========================================================================
 *  Change: 新增
 *  Editor: 谢峰
 *  Time: 2026-09-18
 *  Range: 初始化USART2发送所有权、帧计数器和运行统计
 * ========================================================================== */
void dataport_init(void)
{
    s_tx_frame = NULL;
    s_frame_counter = 0U;
    s_tx_started_tick = 0U;
    memset(&s_stats, 0, sizeof(s_stats));
}

/* ==========================================================================
 *  Change: 新增
 *  Editor: 谢峰
 *  Time: 2026-09-18
 *  Range: 释放已完成或失败的USART2 DMA帧，保证缓冲所有权回到FREE
 * ========================================================================== */
static void dataport_finish_tx(bool success)
{
    if (s_tx_frame == NULL) {
        return;
    }

    if (success) {
        ++s_stats.frames_transmitted;
    }
    (void)frame_release(s_tx_frame);
    s_tx_frame = NULL;
    s_stats.tx_busy = false;
}

/* ==========================================================================
 *  Change: 新增
 *  Editor: 谢峰
 *  Time: 2026-09-18
 *  Range: 主循环完成原地打包、DMA启动、完成消费和超时恢复
 * ========================================================================== */
void dataport_process(void)
{
    const uint32_t events = app_events_take(APP_EVENT_UART2_TX_DONE |
                                             APP_EVENT_UART2_TX_ERROR);

    if ((events & APP_EVENT_UART2_TX_ERROR) != 0U) {
        (void)HAL_UART_AbortTransmit(&huart2);
        ++s_stats.uart2_dma_errors;
        dataport_finish_tx(false);
    } else if ((events & APP_EVENT_UART2_TX_DONE) != 0U) {
        dataport_finish_tx(true);
    }

    if (s_stats.tx_busy &&
        ((uint32_t)(HAL_GetTick() - s_tx_started_tick) >
         DATAPORT_TX_TIMEOUT_MS)) {
        (void)HAL_UART_AbortTransmit(&huart2);
        ++s_stats.uart2_timeouts;
        dataport_finish_tx(false);
    }

    frame_buffer_t *raw_frame = frame_take_raw();
    if (raw_frame != NULL) {
        uint16_t packed_len = 0U;
        if (!frame_protocol_pack_inplace(raw_frame->storage,
                                         frame_storage_capacity_bytes(),
                                         raw_frame->rows,
                                         raw_frame->cols,
                                         s_frame_counter,
                                         &packed_len) ||
            !frame_publish_packed(raw_frame, packed_len)) {
            ++s_stats.frame_pack_errors;
            (void)frame_discard_ready(raw_frame);
        } else {
            ++s_frame_counter;
        }
    }

    if (!s_stats.tx_busy) {
        frame_buffer_t *packed_frame = frame_take_packed();
        if (packed_frame == NULL) {
            return;
        }
        if (!frame_mark_transmitting(packed_frame)) {
            return;
        }

        s_tx_frame = packed_frame;
        s_stats.tx_busy = true;
        s_tx_started_tick = HAL_GetTick();
        if (HAL_UART_Transmit_DMA(&huart2,
                                  (uint8_t *)packed_frame->storage,
                                  packed_frame->packed_len) != HAL_OK) {
            ++s_stats.uart2_dma_errors;
            dataport_finish_tx(false);
        }
    }
}

/* ==========================================================================
 *  Change: 新增
 *  Editor: 谢峰
 *  Time: 2026-09-18
 *  Range: HAL USART2发送完成回调仅发布事件，不在ISR释放缓冲或继续发送
 * ========================================================================== */
void dataport_on_tx_complete_irq(void)
{
    app_events_set(APP_EVENT_UART2_TX_DONE);
}

/* ==========================================================================
 *  Change: 新增
 *  Editor: 谢峰
 *  Time: 2026-09-18
 *  Range: HAL USART2错误回调仅发布事件，由主循环执行恢复
 * ========================================================================== */
void dataport_on_error_irq(void)
{
    app_events_set(APP_EVENT_UART2_TX_ERROR);
}

/* ==========================================================================
 *  Change: 新增
 *  Editor: 谢峰
 *  Time: 2026-09-18
 *  Range: 提供USART2发送统计快照和清零接口
 * ========================================================================== */
void dataport_get_stats(dataport_stats_t *stats)
{
    if (stats != NULL) {
        memcpy(stats, &s_stats, sizeof(*stats));
    }
}

void dataport_clear_stats(void)
{
    const bool busy = s_stats.tx_busy;
    memset(&s_stats, 0, sizeof(s_stats));
    s_stats.tx_busy = busy;
}
