/**
 * @file frame_protocol.c
 * @brief Encode raw 16-bit ADC samples into the established wire format.
 */

#include "frame_protocol.h"

#include <stddef.h>

/* ==========================================================================
 *  Change: 新增
 *  Editor: 谢峰
 *  Time: 2026-09-18
 *  Range: 实现 CRC-32/ISO-HDLC，保持原工程协议的反射算法和最终取反
 * ========================================================================== */
uint32_t frame_protocol_crc32(const uint8_t *data, uint32_t len)
{
    uint32_t crc = 0xFFFFFFFFUL;

    if (data == NULL) {
        return 0U;
    }

    for (uint32_t i = 0U; i < len; ++i) {
        crc ^= data[i];
        for (uint32_t bit = 0U; bit < 8U; ++bit) {
            if ((crc & 1UL) != 0UL) {
                crc = (crc >> 1U) ^ 0xEDB88320UL;
            } else {
                crc >>= 1U;
            }
        }
    }

    return ~crc;
}

/* ==========================================================================
 *  Change: 新增
 *  Editor: 谢峰
 *  Time: 2026-09-18
 *  Range: 原地生成 FF66 帧头、16位计数、全16位样本和小端 CRC32 帧尾
 * ========================================================================== */
bool frame_protocol_pack_inplace(uint16_t *storage,
                                 uint16_t capacity_bytes,
                                 uint16_t rows,
                                 uint16_t cols,
                                 uint16_t frame_counter,
                                 uint16_t *packed_len)
{
    uint32_t sample_count;
    uint32_t frame_len;
    uint8_t *bytes;

    if ((storage == NULL) || (packed_len == NULL)) {
        return false;
    }
    if ((rows == 0U) || (rows > FRAME_PROTOCOL_ROWS_MAX) ||
        (cols == 0U) || (cols > FRAME_PROTOCOL_COLS_MAX)) {
        return false;
    }

    sample_count = (uint32_t)rows * (uint32_t)cols;
    frame_len = FRAME_PROTOCOL_OVERHEAD + sample_count * sizeof(uint16_t);
    if ((frame_len > capacity_bytes) || (frame_len > UINT16_MAX)) {
        return false;
    }

    bytes = (uint8_t *)storage;
    for (uint32_t i = sample_count; i > 0U; --i) {
        const uint16_t value = storage[i - 1U];
        const uint32_t offset = 4U + (i - 1U) * 2U;
        bytes[offset] = (uint8_t)(value >> 8U);
        bytes[offset + 1U] = (uint8_t)value;
    }

    bytes[0] = FRAME_PROTOCOL_HEADER_0;
    bytes[1] = FRAME_PROTOCOL_HEADER_1;
    bytes[2] = (uint8_t)(frame_counter >> 8U);
    bytes[3] = (uint8_t)frame_counter;

    const uint32_t crc = frame_protocol_crc32(&bytes[2], 2U + sample_count * 2U);
    bytes[frame_len - 4U] = (uint8_t)crc;
    bytes[frame_len - 3U] = (uint8_t)(crc >> 8U);
    bytes[frame_len - 2U] = (uint8_t)(crc >> 16U);
    bytes[frame_len - 1U] = (uint8_t)(crc >> 24U);

    *packed_len = (uint16_t)frame_len;
    return true;
}
