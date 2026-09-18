/**
 * @file app_main.h
 * @brief Bare-metal application initialization and cooperative processing.
 */

#ifndef APP_MAIN_H
#define APP_MAIN_H

#include <stdbool.h>

/* ==========================================================================
 *  Change: 新增
 *  Editor: 谢峰
 *  Time: 2026-09-18
 *  Range: 定义应用初始化和超级循环单步处理入口
 * ========================================================================== */
bool app_init(void);
void app_process(void);

#endif /* APP_MAIN_H */
