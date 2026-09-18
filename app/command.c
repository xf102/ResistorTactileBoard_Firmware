/**
 * @file command.c
 * @brief Clear, range-checked control commands for acquisition and diagnostics.
 */

#include "command.h"

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "adc_frontend.h"
#include "bsp_config.h"
#include "console.h"
#include "dataport.h"
#include "scan_service.h"

#define COMMAND_MAX_ARGS  6

typedef int (*command_handler_t)(int argc, char **argv);

typedef struct {
    const char *name;
    command_handler_t handler;
    const char *help;
} command_entry_t;

static int command_help(int argc, char **argv);
static int command_status(int argc, char **argv);
static int command_scan_start(int argc, char **argv);
static int command_scan_stop(int argc, char **argv);
static int command_scan_rows(int argc, char **argv);
static int command_scan_cols(int argc, char **argv);
static int command_scan_fps(int argc, char **argv);
static int command_scan_period(int argc, char **argv);
static int command_adc_read(int argc, char **argv);
static int command_adc_reset(int argc, char **argv);
static int command_spi_status(int argc, char **argv);
static int command_stats_show(int argc, char **argv);
static int command_stats_clear(int argc, char **argv);

/* ==========================================================================
 *  Change: 新增
 *  Editor: 谢峰
 *  Time: 2026-09-18
 *  Range: 注册第一阶段13条可读命令名称、处理函数和参数帮助
 * ========================================================================== */
static const command_entry_t s_commands[] = {
    {"help", command_help, "help - show all commands"},
    {"status", command_status, "status - show acquisition status"},
    {"scan_start", command_scan_start, "scan_start - start ADC acquisition"},
    {"scan_stop", command_scan_stop, "scan_stop - stop ADC acquisition"},
    {"scan_rows", command_scan_rows, "scan_rows <1..max> - set row count"},
    {"scan_cols", command_scan_cols, "scan_cols <1..max> - set column count"},
    {"scan_fps", command_scan_fps, "scan_fps <fps> - set target frame rate"},
    {"scan_period_us", command_scan_period, "scan_period_us <100..65535> - set step period"},
    {"adc_read", command_adc_read, "adc_read - read channel zero while stopped"},
    {"adc_reset", command_adc_reset, "adc_reset - reset frontend while stopped"},
    {"spi_status", command_spi_status, "spi_status - show ADC frontend readiness"},
    {"stats_show", command_stats_show, "stats_show - show all runtime counters"},
    {"stats_clear", command_stats_clear, "stats_clear - clear all runtime counters"},
};

/* ==========================================================================
 *  Change: 新增
 *  Editor: 谢峰
 *  Time: 2026-09-18
 *  Range: 严格解析无符号十进制参数并拒绝空值、尾随字符和范围溢出
 * ========================================================================== */
static bool command_parse_u16(const char *text, uint16_t min_value,
                              uint16_t max_value, uint16_t *value)
{
    char *end;
    unsigned long parsed;

    if ((text == NULL) || (value == NULL) || (*text == '\0')) {
        return false;
    }
    errno = 0;
    parsed = strtoul(text, &end, 10);
    if ((errno != 0) || (*end != '\0') || (parsed < min_value) ||
        (parsed > max_value)) {
        return false;
    }
    *value = (uint16_t)parsed;
    return true;
}

/* ==========================================================================
 *  Change: 新增
 *  Editor: 谢峰
 *  Time: 2026-09-18
 *  Range: 使用原地空白分隔替代newlib strtok，避免其内部malloc依赖
 * ========================================================================== */
static int command_tokenize(char *line, char **argv, int argv_capacity)
{
    int argc = 0;
    char *cursor = line;

    while (*cursor != '\0') {
        while ((*cursor == ' ') || (*cursor == '\t')) {
            ++cursor;
        }
        if (*cursor == '\0') {
            break;
        }
        if (argc >= argv_capacity) {
            return -1;
        }

        argv[argc++] = cursor;
        while ((*cursor != '\0') && (*cursor != ' ') && (*cursor != '\t')) {
            ++cursor;
        }
        if (*cursor != '\0') {
            *cursor++ = '\0';
        }
    }
    return argc;
}

/* ==========================================================================
 *  Change: 新增
 *  Editor: 谢峰
 *  Time: 2026-09-18
 *  Range: 将空格分隔命令行分派到静态命令表并输出统一结果
 * ========================================================================== */
void command_execute_line(char *line)
{
    char *argv[COMMAND_MAX_ARGS];
    const int argc = command_tokenize(line, argv, COMMAND_MAX_ARGS);

    if (argc < 0) {
        (void)console_write("ERR too many arguments\r\n");
        return;
    }
    if (argc == 0) {
        return;
    }

    for (uint32_t i = 0U; i < (sizeof(s_commands) / sizeof(s_commands[0])); ++i) {
        if (strcmp(argv[0], s_commands[i].name) == 0) {
            if (s_commands[i].handler(argc, argv) == 0) {
                (void)console_write("OK\r\n");
            }
            return;
        }
    }
    (void)console_write("ERR unknown command; use help\r\n");
}

/* ==========================================================================
 *  Change: 新增
 *  Editor: 谢峰
 *  Time: 2026-09-18
 *  Range: 实现命令帮助、状态、扫描配置、ADC诊断和统计处理函数
 * ========================================================================== */
static int command_help(int argc, char **argv)
{
    (void)argv;
    if (argc != 1) return console_write("ERR usage: help\r\n") ? -1 : -1;
    for (uint32_t i = 0U; i < (sizeof(s_commands) / sizeof(s_commands[0])); ++i) {
        (void)console_printf("%s\r\n", s_commands[i].help);
    }
    return 0;
}

static int command_status(int argc, char **argv)
{
    scan_status_t status;
    (void)argv;
    if (argc != 1) return console_write("ERR usage: status\r\n") ? -1 : -1;
    scan_get_status(&status);
    (void)console_printf("state=%u rows=%u cols=%u period_us=%u row=%u step=%u discard=%u\r\n",
                         (unsigned)status.state, status.rows, status.cols,
                         status.period_us, status.row, status.step,
                         status.discarding ? 1U : 0U);
    return 0;
}

static int command_scan_start(int argc, char **argv)
{
    (void)argv;
    if (argc != 1) return console_write("ERR usage: scan_start\r\n") ? -1 : -1;
    if (!scan_start()) return console_write("ERR scan did not start\r\n") ? -1 : -1;
    return 0;
}

static int command_scan_stop(int argc, char **argv)
{
    (void)argv;
    if (argc != 1) return console_write("ERR usage: scan_stop\r\n") ? -1 : -1;
    scan_stop();
    return 0;
}

static int command_apply_count(int argc, char **argv, bool rows)
{
    uint16_t value;
    scan_status_t status;
    const uint16_t maximum = rows ? BSP_MATRIX_ROWS : BSP_MATRIX_COLS;

    if ((argc != 2) || !command_parse_u16(argv[1], 1U, maximum, &value)) {
        (void)console_printf("ERR expected 1..%u\r\n", maximum);
        return -1;
    }
    scan_get_status(&status);
    if (!scan_reconfigure(rows ? value : status.rows,
                          rows ? status.cols : value,
                          status.period_us)) {
        return console_write("ERR reconfiguration failed\r\n") ? -1 : -1;
    }
    return 0;
}

static int command_scan_rows(int argc, char **argv)
{
    return command_apply_count(argc, argv, true);
}

static int command_scan_cols(int argc, char **argv)
{
    return command_apply_count(argc, argv, false);
}

static int command_scan_fps(int argc, char **argv)
{
    uint16_t fps;
    uint32_t denominator;
    uint32_t period;
    scan_status_t status;

    if ((argc != 2) || !command_parse_u16(argv[1], 1U, 100U, &fps)) {
        return console_write("ERR expected fps 1..100\r\n") ? -1 : -1;
    }
    scan_get_status(&status);
    denominator = (uint32_t)fps * status.rows * ((uint32_t)status.cols + 1U);
    period = 1000000UL / denominator;
    if ((period < 100U) || (period > 65535U)) {
        return console_write("ERR fps is outside timer limits\r\n") ? -1 : -1;
    }
    if (!scan_reconfigure(status.rows, status.cols, (uint16_t)period)) {
        return console_write("ERR reconfiguration failed\r\n") ? -1 : -1;
    }
    return 0;
}

static int command_scan_period(int argc, char **argv)
{
    uint16_t period;
    scan_status_t status;
    if ((argc != 2) || !command_parse_u16(argv[1], 100U, 65535U, &period)) {
        return console_write("ERR expected period_us 100..65535\r\n") ? -1 : -1;
    }
    scan_get_status(&status);
    if (!scan_reconfigure(status.rows, status.cols, period)) {
        return console_write("ERR reconfiguration failed\r\n") ? -1 : -1;
    }
    return 0;
}

static int command_require_stopped(scan_status_t *status)
{
    scan_get_status(status);
    if (status->state != SCAN_STATE_STOPPED) {
        (void)console_write("ERR run scan_stop first\r\n");
        return -1;
    }
    return 0;
}

static int command_adc_read(int argc, char **argv)
{
    uint16_t sample;
    scan_status_t status;
    (void)argv;
    if (argc != 1) return console_write("ERR usage: adc_read\r\n") ? -1 : -1;
    if (command_require_stopped(&status) != 0) return -1;
    if (!adc_frontend_read_once(&sample)) return console_write("ERR ADC read failed\r\n") ? -1 : -1;
    (void)console_printf("adc_raw=%u (0x%04X)\r\n", sample, sample);
    return 0;
}

static int command_adc_reset(int argc, char **argv)
{
    scan_status_t status;
    (void)argv;
    if (argc != 1) return console_write("ERR usage: adc_reset\r\n") ? -1 : -1;
    if (command_require_stopped(&status) != 0) return -1;
    if (!adc_frontend_reset()) return console_write("ERR ADC reset failed\r\n") ? -1 : -1;
    return 0;
}

static int command_spi_status(int argc, char **argv)
{
    (void)argv;
    if (argc != 1) return console_write("ERR usage: spi_status\r\n") ? -1 : -1;
    (void)console_printf("adc_frontend_ready=%u\r\n", adc_frontend_is_ready() ? 1U : 0U);
    return 0;
}

static int command_stats_show(int argc, char **argv)
{
    scan_status_t scan;
    dataport_stats_t data;
    (void)argv;
    if (argc != 1) return console_write("ERR usage: stats_show\r\n") ? -1 : -1;
    scan_get_status(&scan);
    dataport_get_stats(&data);
    (void)console_printf("acquired=%lu dropped=%lu spi_busy=%lu spi_err=%lu sync_err=%lu\r\n",
                         (unsigned long)scan.stats.frames_acquired,
                         (unsigned long)scan.stats.frames_dropped,
                         (unsigned long)scan.stats.spi_busy_errors,
                         (unsigned long)scan.stats.spi_dma_errors,
                         (unsigned long)scan.stats.frontend_sync_errors);
    /* ==========================================================================
     *  Change: 修改
     *  Editor: 谢峰
     *  Time: 2026-09-18
     *  Range: stats_show增加USART1硬件错误和接收重挂接失败计数
     * ========================================================================== */
    (void)console_printf("sent=%lu uart_err=%lu uart_timeout=%lu pack_err=%lu rx_overflow=%lu rx_err=%lu rearm_err=%lu\r\n",
                         (unsigned long)data.frames_transmitted,
                         (unsigned long)data.uart2_dma_errors,
                         (unsigned long)data.uart2_timeouts,
                         (unsigned long)data.frame_pack_errors,
                         (unsigned long)console_get_rx_overflows(),
                         (unsigned long)console_get_rx_errors(),
                         (unsigned long)console_get_rx_rearm_failures());
    return 0;
}

static int command_stats_clear(int argc, char **argv)
{
    (void)argv;
    if (argc != 1) return console_write("ERR usage: stats_clear\r\n") ? -1 : -1;
    scan_clear_stats();
    dataport_clear_stats();
    return 0;
}
