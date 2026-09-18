/**
 * @file    data_acquisition_debug.h
 * @brief   数据采集调试接口：基于 msh 命令逐步调通硬件。
 *
 * @details
 * 本模块提供手动控制数据采集各环节的调试命令，用于：
 *   - 单独初始化/测试各硬件模块（74HC595、ADS8681、CD74HC4067）
 *   - 手动选择行/列，单次读取 ADC 值
 *   - 逐步调通扫描流程，不启动自动扫描
 *
 * msh 命令列表：
 *   dbg_init          - 初始化所有硬件模块
 *   dbg_status        - 显示硬件状态
 *   dbg_row <0-31>    - 选择行
 *   dbg_col <0-31>    - 选择列
 *   dbg_read          - 读取当前选中通道 ADC 值
 *   dbg_read_row      - 读取整行（32 列）数据
 *   dbg_period <us>   - 设置采样周期
 *   dbg_params        - 显示当前参数
 *   dbg_test_single   - 单步测试（选行列 + 读取）
 *   dbg_test_row      - 行扫描测试
 *
 * @author  Firmware Team
 * @version 1.0
 * @date    2026-07-24
 */

#ifndef DATA_ACQUISITION_DEBUG_H_
#define DATA_ACQUISITION_DEBUG_H_

#include <stdint.h>
#include "bsp_basicService.h"

#ifdef __cplusplus
extern "C" {
#endif
#ifdef DBG_CMD_ENABLE
/* --------------------------------------------------------------------------
 *  公共 API
 * -------------------------------------------------------------------------- */

/**
 * @brief  初始化调试模块（注册 msh 命令）。
 *
 * @note   在 main() 中调用，必须在 bsp_init() 之后。
 */
void DATA_ACQUISITION_DEBUG_Init(void);
#endif /* DBG_CMD_ENABLE */
#ifdef __cplusplus
}
#endif

#endif /* DATA_ACQUISITION_DEBUG_H_ */
