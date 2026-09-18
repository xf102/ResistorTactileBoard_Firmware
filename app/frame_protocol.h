/**
 * @file frame_protocol.h
 * @brief Hardware-independent binary frame encoder for ADC samples.
 */

#ifndef FRAME_PROTOCOL_H
#define FRAME_PROTOCOL_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FRAME_PROTOCOL_HEADER_0  0xFFU
#define FRAME_PROTOCOL_HEADER_1  0x66U
#define FRAME_PROTOCOL_OVERHEAD  8U
#define FRAME_PROTOCOL_ROWS_MAX  32U
#define FRAME_PROTOCOL_COLS_MAX  32U

/* ==========================================================================
 *  Change: 新增
 *  Editor: 谢峰
 *  Time: 2026-09-18
 *  Range: 定义裸机采集链路的数据帧 CRC32 与原地打包公共接口
 * ========================================================================== */
uint32_t frame_protocol_crc32(const uint8_t *data, uint32_t len);

bool frame_protocol_pack_inplace(uint16_t *storage,
                                 uint16_t capacity_bytes,
                                 uint16_t rows,
                                 uint16_t cols,
                                 uint16_t frame_counter,
                                 uint16_t *packed_len);

#ifdef __cplusplus
}
#endif

#endif /* FRAME_PROTOCOL_H */
