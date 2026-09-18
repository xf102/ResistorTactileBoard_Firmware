/**
 * @file dataport.h
 * @brief Non-blocking binary frame output over USART2 DMA.
 */

#ifndef DATAPORT_H
#define DATAPORT_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint32_t frames_transmitted;
    uint32_t uart2_dma_errors;
    uint32_t uart2_timeouts;
    uint32_t frame_pack_errors;
    bool tx_busy;
} dataport_stats_t;

/* ==========================================================================
 *  Change: 新增
 *  Editor: 谢峰
 *  Time: 2026-09-18
 *  Range: 定义USART2 DMA数据口的初始化、轮询、ISR事件和统计接口
 * ========================================================================== */
void dataport_init(void);
void dataport_process(void);
void dataport_on_tx_complete_irq(void);
void dataport_on_error_irq(void);
void dataport_get_stats(dataport_stats_t *stats);
void dataport_clear_stats(void);

#endif /* DATAPORT_H */
