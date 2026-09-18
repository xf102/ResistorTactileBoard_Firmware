/**
 * @file command.h
 * @brief Text command dispatcher for the USART1 console.
 */

#ifndef COMMAND_H
#define COMMAND_H

/* ==========================================================================
 *  Change: 新增
 *  Editor: 谢峰
 *  Time: 2026-09-18
 *  Range: 定义可修改命令行缓冲的同步命令执行入口
 * ========================================================================== */
void command_execute_line(char *line);

#endif /* COMMAND_H */
