/**
 * @file console.h
 * @brief USART1 text console with interrupt-driven RX and bounded TX waits.
 */

#ifndef CONSOLE_H
#define CONSOLE_H

#include <stdbool.h>
#include <stdint.h>

/* ==========================================================================
 *  Change: 新增
 *  Editor: 谢峰
 *  Time: 2026-09-18
 *  Range: 定义USART1控制台初始化、轮询、输出、接收回调和溢出统计接口
 * ========================================================================== */
bool console_init(void);
void console_process(void);
bool console_write(const char *text);
bool console_printf(const char *format, ...);
void console_on_rx_complete_irq(void);
void console_on_error_irq(void);
uint32_t console_get_rx_overflows(void);

/* ==========================================================================
 *  Change: 新增
 *  Editor: 谢峰
 *  Time: 2026-09-18
 *  Range: 提供USART1硬件错误与接收重挂接失败计数查询接口
 * ========================================================================== */
uint32_t console_get_rx_errors(void);
uint32_t console_get_rx_rearm_failures(void);

#endif /* CONSOLE_H */
