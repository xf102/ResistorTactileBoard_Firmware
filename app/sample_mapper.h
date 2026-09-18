/**
 * @file sample_mapper.h
 * @brief Per-point idle calibration and ADC-to-viewer value mapping.
 */

#ifndef SAMPLE_MAPPER_H
#define SAMPLE_MAPPER_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    bool calibrated;
    uint16_t frames_collected;
    uint16_t frames_required;
    uint16_t deadband_raw;
} sample_mapper_status_t;

/* ==========================================================================
 *  Change: 新增
 *  Editor: 谢峰
 *  Time: 2026-09-18
 *  Range: 定义逐点零点校准、0至1019映射、重新校准和状态查询接口
 * ========================================================================== */
void sample_mapper_init(void);
void sample_mapper_restart_calibration(void);
void sample_mapper_process_frame(uint16_t *samples, uint16_t rows,
                                 uint16_t cols);
void sample_mapper_get_status(sample_mapper_status_t *status);

#endif /* SAMPLE_MAPPER_H */
