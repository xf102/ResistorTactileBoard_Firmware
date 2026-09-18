/**
 * @file console.c
 * @brief Lightweight USART1 line console for the bare-metal main loop.
 */

#include "console.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "command.h"
#include "usart.h"

#define CONSOLE_RX_BUFFER_SIZE  128U
#define CONSOLE_LINE_SIZE        96U
#define CONSOLE_TX_BUFFER_SIZE  192U
#define CONSOLE_TX_CHUNK_SIZE    32U
#define CONSOLE_TX_TIMEOUT_MS    20U

static volatile uint8_t s_rx_buffer[CONSOLE_RX_BUFFER_SIZE];
static volatile uint16_t s_rx_head;
static volatile uint16_t s_rx_tail;
static volatile uint32_t s_rx_overflows;
static volatile uint32_t s_rx_errors;
static volatile uint32_t s_rx_rearm_failures;
static uint8_t s_rx_byte;
static char s_line[CONSOLE_LINE_SIZE];
static uint16_t s_line_length;
static bool s_discard_line;
static bool s_last_terminator_was_cr;

/* ==========================================================================
 *  Change: 新增
 *  Editor: 谢峰
 *  Time: 2026-09-18
 *  Range: 统一挂接USART1单字节接收并记录无法恢复的HAL启动失败
 * ========================================================================== */
static bool console_arm_receive(void)
{
    if (HAL_UART_Receive_IT(&huart1, &s_rx_byte, 1U) != HAL_OK) {
        ++s_rx_rearm_failures;
        return false;
    }
    return true;
}

/* ==========================================================================
 *  Change: 新增
 *  Editor: 谢峰
 *  Time: 2026-09-18
 *  Range: 在CR、LF或CRLF边界统一结束命令行，保证CRLF只执行一次
 * ========================================================================== */
static void console_finish_line(void)
{
    if (s_discard_line) {
        (void)console_write("ERR input line too long\r\n");
    } else if (s_line_length > 0U) {
        s_line[s_line_length] = '\0';
        command_execute_line(s_line);
    }
    s_line_length = 0U;
    s_discard_line = false;
}

/* ==========================================================================
 *  Change: 新增
 *  Editor: 谢峰
 *  Time: 2026-09-18
 *  Range: 初始化USART1环形缓冲并挂接首个单字节接收中断
 * ========================================================================== */
bool console_init(void)
{
    s_rx_head = 0U;
    s_rx_tail = 0U;
    s_rx_overflows = 0U;
    s_rx_errors = 0U;
    s_rx_rearm_failures = 0U;
    s_rx_byte = 0U;
    s_line_length = 0U;
    s_discard_line = false;
    s_last_terminator_was_cr = false;
    return console_arm_receive();
}

/* ==========================================================================
 *  Change: 新增
 *  Editor: 谢峰
 *  Time: 2026-09-18
 *  Range: 使用有限超时分段发送USART1文本，禁止无限阻塞主循环
 * ========================================================================== */
bool console_write(const char *text)
{
    size_t remaining;

    if (text == NULL) {
        return false;
    }

    remaining = strlen(text);
    while (remaining > 0U) {
        const uint16_t chunk = (remaining > CONSOLE_TX_CHUNK_SIZE)
                                 ? CONSOLE_TX_CHUNK_SIZE
                                 : (uint16_t)remaining;
        if (HAL_UART_Transmit(&huart1, (const uint8_t *)text, chunk,
                              CONSOLE_TX_TIMEOUT_MS) != HAL_OK) {
            return false;
        }
        text += chunk;
        remaining -= chunk;
    }
    return true;
}

/* ==========================================================================
 *  Change: 新增
 *  Editor: 谢峰
 *  Time: 2026-09-18
 *  Range: 格式化短控制台响应并在截断时返回失败
 * ========================================================================== */
bool console_printf(const char *format, ...)
{
    char buffer[CONSOLE_TX_BUFFER_SIZE];
    va_list args;
    int length;

    va_start(args, format);
    length = vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    if ((length < 0) || ((size_t)length >= sizeof(buffer))) {
        return false;
    }
    return console_write(buffer);
}

/* ==========================================================================
 *  Change: 新增
 *  Editor: 谢峰
 *  Time: 2026-09-18
 *  Range: USART1 RX完成ISR只写环形缓冲并立即重新挂接接收
 * ========================================================================== */
void console_on_rx_complete_irq(void)
{
    const uint16_t next = (uint16_t)((s_rx_head + 1U) % CONSOLE_RX_BUFFER_SIZE);

    if (next == s_rx_tail) {
        ++s_rx_overflows;
        s_discard_line = true;
    } else {
        s_rx_buffer[s_rx_head] = s_rx_byte;
        s_rx_head = next;
    }
    (void)console_arm_receive();
}

/* ==========================================================================
 *  Change: 新增
 *  Editor: 谢峰
 *  Time: 2026-09-18
 *  Range: USART1错误后统计丢失输入并重新挂接单字节接收
 * ========================================================================== */
void console_on_error_irq(void)
{
    ++s_rx_errors;
    s_discard_line = true;
    (void)HAL_UART_AbortReceive(&huart1);
    __HAL_UART_CLEAR_OREFLAG(&huart1);
    (void)console_arm_receive();
}

/* ==========================================================================
 *  Change: 新增
 *  Editor: 谢峰
 *  Time: 2026-09-18
 *  Range: 主循环组装完整命令行，超长或溢出输入丢弃到换行边界
 * ========================================================================== */
void console_process(void)
{
    while (s_rx_tail != s_rx_head) {
        const char ch = (char)s_rx_buffer[s_rx_tail];
        s_rx_tail = (uint16_t)((s_rx_tail + 1U) % CONSOLE_RX_BUFFER_SIZE);

        /* ==========================================================================
         *  Change: 修改
         *  Editor: 谢峰
         *  Time: 2026-09-18
         *  Range: 同时接受CR、LF和CRLF，避免串口工具换行配置导致命令拼接或无响应
         * ========================================================================== */
        if (ch == '\r') {
            console_finish_line();
            s_last_terminator_was_cr = true;
            continue;
        }
        if (ch == '\n') {
            if (!s_last_terminator_was_cr) {
                console_finish_line();
            }
            s_last_terminator_was_cr = false;
            continue;
        }
        s_last_terminator_was_cr = false;
        if (s_discard_line) {
            continue;
        }
        if (s_line_length >= (CONSOLE_LINE_SIZE - 1U)) {
            ++s_rx_overflows;
            s_discard_line = true;
            continue;
        }
        s_line[s_line_length++] = ch;
    }
}

/* ==========================================================================
 *  Change: 新增
 *  Editor: 谢峰
 *  Time: 2026-09-18
 *  Range: 返回USART1环形缓冲与命令行累计溢出次数
 * ========================================================================== */
uint32_t console_get_rx_overflows(void)
{
    return s_rx_overflows;
}

/* ==========================================================================
 *  Change: 新增
 *  Editor: 谢峰
 *  Time: 2026-09-18
 *  Range: 返回USART1硬件错误和HAL接收重挂接失败累计值
 * ========================================================================== */
uint32_t console_get_rx_errors(void)
{
    return s_rx_errors;
}

uint32_t console_get_rx_rearm_failures(void)
{
    return s_rx_rearm_failures;
}
