/* ==========================================================================
 *  Change: 新增
 *  Editor: 谢峰
 *  Time: 2026-09-16
 *  Range: 定义TPC5120矩阵行选通诊断的掩码序列、平均值计算和启动接口
 * ========================================================================== */
#ifndef TPC5120_MATRIX_DIAG_H_
#define TPC5120_MATRIX_DIAG_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TPC_MATRIX_DIAG_CHANNEL_COUNT       8U
#define TPC_MATRIX_DIAG_SAMPLES_PER_CHANNEL 32U
#define TPC_MATRIX_DIAG_MASK_COUNT          10U
#define TPC_MATRIX_DIAG_MASK_AT(index)                                      \
    ((uint32_t)(((uint32_t)(index) == 0U) ? 0x00U :                         \
                (((uint32_t)(index) <= TPC_MATRIX_DIAG_CHANNEL_COUNT)       \
                     ? (1UL << ((uint32_t)(index) - 1U))                    \
                     : 0xFFU)))
#define TPC_MATRIX_DIAG_AVERAGE(sum, count)                                 \
    ((uint16_t)((uint32_t)(sum) / (uint32_t)(count)))

/**
 * @brief 创建并启动TPC5120矩阵行选通诊断线程。
 *
 * @note 诊断线程独占SPI1和74HC595；启用诊断时不得启动正常采集线程。
 */
void TPC5120_MatrixDiag_Init(void);

#ifdef __cplusplus
}
#endif

#endif /* TPC5120_MATRIX_DIAG_H_ */
