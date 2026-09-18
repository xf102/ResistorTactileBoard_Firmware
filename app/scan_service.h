/**
 * @file scan_service.h
 * @brief Non-blocking bare-metal matrix scan state machine.
 */

#ifndef SCAN_SERVICE_H
#define SCAN_SERVICE_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    SCAN_STATE_STOPPED = 0,
    SCAN_STATE_STARTING,
    SCAN_STATE_RUNNING,
    SCAN_STATE_STOPPING,
    SCAN_STATE_FAULT
} scan_state_t;

typedef struct {
    uint32_t frames_acquired;
    uint32_t frames_dropped;
    uint32_t spi_busy_errors;
    uint32_t spi_dma_errors;
    uint32_t frontend_sync_errors;
} scan_stats_t;

typedef struct {
    scan_state_t state;
    uint16_t rows;
    uint16_t cols;
    uint16_t period_us;
    uint16_t row;
    uint16_t step;
    bool discarding;
    scan_stats_t stats;
} scan_status_t;

/* ==========================================================================
 *  Change: 新增
 *  Editor: 谢峰
 *  Time: 2026-09-18
 *  Range: 定义扫描初始化、控制、重配置、ISR入口和状态查询接口
 * ========================================================================== */
bool scan_init(void);
bool scan_start(void);
void scan_stop(void);
void scan_process(void);
bool scan_reconfigure(uint16_t rows, uint16_t cols, uint16_t period_us);
void scan_on_timer_irq(void);
void scan_on_spi_complete_irq(void);
void scan_on_spi_error_irq(void);
void scan_get_status(scan_status_t *status);
void scan_clear_stats(void);

#endif /* SCAN_SERVICE_H */
