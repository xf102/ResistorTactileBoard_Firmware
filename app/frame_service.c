/**
 * @file frame_service.c
 * @brief Static frame storage with explicit ISR/main-loop ownership states.
 */

#include "frame_service.h"

#include <stddef.h>
#include <string.h>

#include "stm32f1xx.h"

#if BSP_FRAME_BUFFER_COUNT != 2U
#error "The first bare-metal pipeline requires exactly two frame buffers"
#endif

static frame_buffer_t s_frames[BSP_FRAME_BUFFER_COUNT];

/* ==========================================================================
 *  Change: 新增
 *  Editor: 谢峰
 *  Time: 2026-09-18
 *  Range: 使用保存 PRIMASK 的短临界区实现帧所有权原子转换
 * ========================================================================== */
static bool frame_transition(frame_buffer_t *frame,
                             frame_state_t expected,
                             frame_state_t next)
{
    bool changed = false;
    const uint32_t primask = __get_PRIMASK();

    __disable_irq();
    if ((frame != NULL) && (frame->state == expected)) {
        frame->state = next;
        changed = true;
    }
    if (primask == 0U) {
        __enable_irq();
    }

    return changed;
}

/* ==========================================================================
 *  Change: 新增
 *  Editor: 谢峰
 *  Time: 2026-09-18
 *  Range: 初始化两个静态帧缓冲并清除历史长度和矩阵信息
 * ========================================================================== */
void frame_service_init(void)
{
    memset(s_frames, 0, sizeof(s_frames));
    for (uint32_t i = 0U; i < BSP_FRAME_BUFFER_COUNT; ++i) {
        s_frames[i].state = FRAME_STATE_FREE;
    }
}

/* ==========================================================================
 *  Change: 新增
 *  Editor: 谢峰
 *  Time: 2026-09-18
 *  Range: 在帧边界获取唯一 FREE 缓冲作为 ADC ISR 的写入目标
 * ========================================================================== */
frame_buffer_t *frame_acquire_for_scan(void)
{
    for (uint32_t i = 0U; i < BSP_FRAME_BUFFER_COUNT; ++i) {
        if (frame_transition(&s_frames[i], FRAME_STATE_FREE,
                             FRAME_STATE_ACQUIRING)) {
            s_frames[i].packed_len = 0U;
            s_frames[i].rows = 0U;
            s_frames[i].cols = 0U;
            return &s_frames[i];
        }
    }

    return NULL;
}

/* ==========================================================================
 *  Change: 新增
 *  Editor: 谢峰
 *  Time: 2026-09-18
 *  Range: 发布完整原始帧，半帧不得进入打包队列
 * ========================================================================== */
bool frame_publish_raw(frame_buffer_t *frame, uint16_t rows, uint16_t cols)
{
    if ((frame == NULL) || (rows == 0U) || (rows > BSP_MATRIX_ROWS) ||
        (cols == 0U) || (cols > BSP_MATRIX_COLS)) {
        return false;
    }

    frame->rows = rows;
    frame->cols = cols;
    if (!frame_transition(frame, FRAME_STATE_ACQUIRING,
                          FRAME_STATE_RAW_READY)) {
        frame->rows = 0U;
        frame->cols = 0U;
        return false;
    }
    return true;
}

/* ==========================================================================
 *  Change: 新增
 *  Editor: 谢峰
 *  Time: 2026-09-18
 *  Range: 为主循环查找等待协议打包的完整原始帧
 * ========================================================================== */
frame_buffer_t *frame_take_raw(void)
{
    for (uint32_t i = 0U; i < BSP_FRAME_BUFFER_COUNT; ++i) {
        if (s_frames[i].state == FRAME_STATE_RAW_READY) {
            return &s_frames[i];
        }
    }
    return NULL;
}

/* ==========================================================================
 *  Change: 新增
 *  Editor: 谢峰
 *  Time: 2026-09-18
 *  Range: 校验打包长度后将原始帧所有权交给 USART2 发送侧
 * ========================================================================== */
bool frame_publish_packed(frame_buffer_t *frame, uint16_t packed_len)
{
    if ((frame == NULL) || (packed_len < FRAME_PROTOCOL_OVERHEAD) ||
        (packed_len > FRAME_STORAGE_BYTES)) {
        return false;
    }

    frame->packed_len = packed_len;
    if (!frame_transition(frame, FRAME_STATE_RAW_READY,
                          FRAME_STATE_PACKED_READY)) {
        frame->packed_len = 0U;
        return false;
    }
    return true;
}

/* ==========================================================================
 *  Change: 新增
 *  Editor: 谢峰
 *  Time: 2026-09-18
 *  Range: 为 USART2 服务查找等待发送的已打包帧
 * ========================================================================== */
frame_buffer_t *frame_take_packed(void)
{
    for (uint32_t i = 0U; i < BSP_FRAME_BUFFER_COUNT; ++i) {
        if (s_frames[i].state == FRAME_STATE_PACKED_READY) {
            return &s_frames[i];
        }
    }
    return NULL;
}

/* ==========================================================================
 *  Change: 新增
 *  Editor: 谢峰
 *  Time: 2026-09-18
 *  Range: 在启动 USART2 DMA 前锁定缓冲区为只读发送所有权
 * ========================================================================== */
bool frame_mark_transmitting(frame_buffer_t *frame)
{
    return frame_transition(frame, FRAME_STATE_PACKED_READY,
                            FRAME_STATE_TRANSMITTING);
}

/* ==========================================================================
 *  Change: 新增
 *  Editor: 谢峰
 *  Time: 2026-09-18
 *  Range: USART2 DMA 完成或恢复结束后释放发送缓冲区
 * ========================================================================== */
bool frame_release(frame_buffer_t *frame)
{
    return frame_transition(frame, FRAME_STATE_TRANSMITTING,
                            FRAME_STATE_FREE);
}

/* ==========================================================================
 *  Change: 新增
 *  Editor: 谢峰
 *  Time: 2026-09-18
 *  Range: 停止或重配置扫描时仅释放尚未完成的采集缓冲区
 * ========================================================================== */
bool frame_discard_acquisition(frame_buffer_t *frame)
{
    return frame_transition(frame, FRAME_STATE_ACQUIRING,
                            FRAME_STATE_FREE);
}

/* ==========================================================================
 *  Change: 新增
 *  Editor: 谢峰
 *  Time: 2026-09-18
 *  Range: 打包或发送准备失败时释放尚未进入DMA所有权的完整帧
 * ========================================================================== */
bool frame_discard_ready(frame_buffer_t *frame)
{
    if (frame_transition(frame, FRAME_STATE_RAW_READY, FRAME_STATE_FREE)) {
        return true;
    }
    return frame_transition(frame, FRAME_STATE_PACKED_READY, FRAME_STATE_FREE);
}

/* ==========================================================================
 *  Change: 新增
 *  Editor: 谢峰
 *  Time: 2026-09-18
 *  Range: 向协议打包层提供静态帧缓冲字节容量
 * ========================================================================== */
uint16_t frame_storage_capacity_bytes(void)
{
    return (uint16_t)FRAME_STORAGE_BYTES;
}
