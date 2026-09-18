#ifndef DATA_ACQUISITION_PIPELINE_H_
#define DATA_ACQUISITION_PIPELINE_H_

#include <stdint.h>

/* ==========================================================================
 *  Change: 新增
 *  Editor: 谢峰
 *  Time: 2026-09-11
 *  Range: 定义 ADS8681 单级流水线的选列、结果归属和行结束规则
 * ========================================================================== */
#define SCAN_PIPELINE_TRANSFER_COUNT(cols)       ((uint16_t)(cols) + 1U)
#define SCAN_PIPELINE_HAS_SAMPLE(step)           ((uint16_t)(step) > 0U)
#define SCAN_PIPELINE_MUX_COL(step, cols)        \
    (((uint16_t)(step) < (uint16_t)(cols)) ?     \
        (uint16_t)(step) : (uint16_t)((uint16_t)(cols) - 1U))
#define SCAN_PIPELINE_SAMPLE_COL(step)            ((uint16_t)(step) - 1U)
#define SCAN_PIPELINE_ROW_COMPLETE(step, cols)    \
    ((uint16_t)(step) >= (uint16_t)(cols))

/* ==========================================================================
 *  Change: 新增
 *  Editor: 谢峰
 *  Time: 2026-09-16
 *  Range: 定义TPC5120行切换后的整轮丢弃与整轮有效采集规则
 * ========================================================================== */
#define SCAN_TPC_PHASE_DISCARD                    0U
#define SCAN_TPC_PHASE_READ                       1U
#define SCAN_TPC_TRANSFER_COUNT(cols)             ((uint16_t)(cols) * 2U)
#define SCAN_TPC_SHOULD_STORE(phase)              \
    ((uint8_t)(phase) == (uint8_t)SCAN_TPC_PHASE_READ)
#define SCAN_TPC_DISCARD_COMPLETE(phase, channel, cols) \
    (((uint8_t)(phase) == (uint8_t)SCAN_TPC_PHASE_DISCARD) && \
     ((uint16_t)(channel) + 1U >= (uint16_t)(cols)))
#define SCAN_TPC_ROW_COMPLETE(phase, channel, cols) \
    (((uint8_t)(phase) == (uint8_t)SCAN_TPC_PHASE_READ) && \
     ((uint16_t)(channel) + 1U >= (uint16_t)(cols)))

#endif /* DATA_ACQUISITION_PIPELINE_H_ */
