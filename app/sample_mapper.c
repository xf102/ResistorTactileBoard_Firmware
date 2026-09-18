/**
 * @file sample_mapper.c
 * @brief Per-point baseline calibration and saturated 0..1019 conversion.
 */

#include "sample_mapper.h"

#include <stddef.h>
#include <string.h>

#include "bsp_config.h"

#define SAMPLE_MAPPER_CALIBRATION_FRAMES  50U
#define SAMPLE_MAPPER_DEADBAND_RAW         50U
#define SAMPLE_MAPPER_VIEWER_MAX         1019U
#define SAMPLE_MAPPER_VIEWER_SPAN        1018U
#define SAMPLE_MAPPER_ADC_MAX           65535U
#define SAMPLE_MAPPER_POINT_CAPACITY \
    ((uint16_t)(BSP_MATRIX_ROWS * BSP_MATRIX_COLS))

static uint32_t s_baseline_sum[SAMPLE_MAPPER_POINT_CAPACITY];
static uint16_t s_baseline[SAMPLE_MAPPER_POINT_CAPACITY];
static uint16_t s_frames_collected;
static uint16_t s_point_count;
static bool s_calibrated;

/* ==========================================================================
 *  Change: 新增
 *  Editor: 谢峰
 *  Time: 2026-09-18
 *  Range: 清除逐点累计值并进入50帧未按压零点校准状态
 * ========================================================================== */
void sample_mapper_restart_calibration(void)
{
    memset(s_baseline_sum, 0, sizeof(s_baseline_sum));
    memset(s_baseline, 0, sizeof(s_baseline));
    s_frames_collected = 0U;
    s_point_count = 0U;
    s_calibrated = false;
}

void sample_mapper_init(void)
{
    sample_mapper_restart_calibration();
}

/* ==========================================================================
 *  Change: 新增
 *  Editor: 谢峰
 *  Time: 2026-09-18
 *  Range: 将单点原始值按独立基线、50码死区和ADC满量程映射到0至1019
 * ========================================================================== */
static uint16_t sample_mapper_map_value(uint16_t raw, uint16_t baseline)
{
    uint32_t threshold = (uint32_t)baseline + SAMPLE_MAPPER_DEADBAND_RAW;
    uint32_t mapped;

    if (threshold > SAMPLE_MAPPER_ADC_MAX) {
        threshold = SAMPLE_MAPPER_ADC_MAX;
    }
    if (((uint32_t)raw <= threshold) || (threshold >= SAMPLE_MAPPER_ADC_MAX)) {
        return 0U;
    }

    mapped = 1U + ((((uint32_t)raw - threshold) * SAMPLE_MAPPER_VIEWER_SPAN) /
                    (SAMPLE_MAPPER_ADC_MAX - threshold));
    return (mapped > SAMPLE_MAPPER_VIEWER_MAX)
             ? SAMPLE_MAPPER_VIEWER_MAX
             : (uint16_t)mapped;
}

/* ==========================================================================
 *  Change: 新增
 *  Editor: 谢峰
 *  Time: 2026-09-18
 *  Range: 前50帧逐点求平均并输出全零，之后原地转换为上位机0至1019显示值
 * ========================================================================== */
void sample_mapper_process_frame(uint16_t *samples, uint16_t rows,
                                 uint16_t cols)
{
    const uint32_t requested_points = (uint32_t)rows * cols;
    uint16_t point_count;
    uint16_t index;

    if ((samples == NULL) || (requested_points == 0U) ||
        (requested_points > SAMPLE_MAPPER_POINT_CAPACITY)) {
        return;
    }
    point_count = (uint16_t)requested_points;

    if ((s_point_count != 0U) && (s_point_count != point_count)) {
        sample_mapper_restart_calibration();
    }
    s_point_count = point_count;

    if (!s_calibrated) {
        for (index = 0U; index < point_count; ++index) {
            s_baseline_sum[index] += samples[index];
            samples[index] = 0U;
        }
        ++s_frames_collected;

        if (s_frames_collected >= SAMPLE_MAPPER_CALIBRATION_FRAMES) {
            for (index = 0U; index < point_count; ++index) {
                s_baseline[index] = (uint16_t)(
                    (s_baseline_sum[index] +
                     (SAMPLE_MAPPER_CALIBRATION_FRAMES / 2U)) /
                    SAMPLE_MAPPER_CALIBRATION_FRAMES);
            }
            s_calibrated = true;
        }
        return;
    }

    for (index = 0U; index < point_count; ++index) {
        samples[index] = sample_mapper_map_value(samples[index],
                                                 s_baseline[index]);
    }
}

/* ==========================================================================
 *  Change: 新增
 *  Editor: 谢峰
 *  Time: 2026-09-18
 *  Range: 提供零点校准完成状态、帧进度和原始码死区参数快照
 * ========================================================================== */
void sample_mapper_get_status(sample_mapper_status_t *status)
{
    if (status == NULL) {
        return;
    }
    status->calibrated = s_calibrated;
    status->frames_collected = s_frames_collected;
    status->frames_required = SAMPLE_MAPPER_CALIBRATION_FRAMES;
    status->deadband_raw = SAMPLE_MAPPER_DEADBAND_RAW;
}
