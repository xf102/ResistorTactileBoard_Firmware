/**
 * @file frame_service.h
 * @brief Static two-buffer ownership service for acquisition and UART DMA.
 */

#ifndef FRAME_SERVICE_H
#define FRAME_SERVICE_H

#include <stdbool.h>
#include <stdint.h>

#include "bsp_config.h"
#include "frame_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

#define FRAME_STORAGE_BYTES \
    (FRAME_PROTOCOL_OVERHEAD + BSP_MATRIX_ROWS * BSP_MATRIX_COLS * 2U)
#define FRAME_STORAGE_WORDS ((FRAME_STORAGE_BYTES + 1U) / 2U)

typedef enum {
    FRAME_STATE_FREE = 0,
    FRAME_STATE_ACQUIRING,
    FRAME_STATE_RAW_READY,
    FRAME_STATE_PACKED_READY,
    FRAME_STATE_TRANSMITTING
} frame_state_t;

typedef struct {
    uint16_t storage[FRAME_STORAGE_WORDS];
    uint16_t packed_len;
    uint16_t rows;
    uint16_t cols;
    volatile frame_state_t state;
} frame_buffer_t;

/* ==========================================================================
 *  Change: 新增
 *  Editor: 谢峰
 *  Time: 2026-09-18
 *  Range: 定义静态双缓冲的所有权获取、发布、发送和释放接口
 * ========================================================================== */
void frame_service_init(void);
frame_buffer_t *frame_acquire_for_scan(void);
bool frame_publish_raw(frame_buffer_t *frame, uint16_t rows, uint16_t cols);
frame_buffer_t *frame_take_raw(void);
bool frame_publish_packed(frame_buffer_t *frame, uint16_t packed_len);
frame_buffer_t *frame_take_packed(void);
bool frame_mark_transmitting(frame_buffer_t *frame);
bool frame_release(frame_buffer_t *frame);
bool frame_discard_acquisition(frame_buffer_t *frame);
bool frame_discard_ready(frame_buffer_t *frame);
uint16_t frame_storage_capacity_bytes(void);

#ifdef __cplusplus
}
#endif

#endif /* FRAME_SERVICE_H */
