/**
 * @file app_events.h
 * @brief Atomic event bits shared between interrupt callbacks and main loop.
 */

#ifndef APP_EVENTS_H
#define APP_EVENTS_H

#include <stdint.h>

#define APP_EVENT_SCAN_FAULT      (1UL << 0)
#define APP_EVENT_UART2_TX_DONE   (1UL << 1)
#define APP_EVENT_UART2_TX_ERROR  (1UL << 2)
#define APP_EVENT_UART1_RX_READY  (1UL << 3)

/* ==========================================================================
 *  Change: 新增
 *  Editor: 谢峰
 *  Time: 2026-09-18
 *  Range: 定义ISR发布、主循环一次性消费的裸机事件位接口
 * ========================================================================== */
void app_events_reset(void);
void app_events_set(uint32_t events);
uint32_t app_events_take(uint32_t events);

#endif /* APP_EVENTS_H */
