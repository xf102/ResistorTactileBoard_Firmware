/**
 * @file    product.c
 * @brief   产品序列号模块：跨系列 STM32 芯片 UID 读取 + 序列号生成
 *
 * @details
 *   本模块提供以下功能：
 *     - 读取不同 STM32 系列的 96-bit 唯一 ID（自动适配基地址）
 *     - 软件 CRC32 计算（不依赖硬件 CRC 模块）
 *     - 基于 UID + 产品命名规则的产品序列号生成
 *     - 单例模式，全局访问，惰性初始化
 *
 *   支持的 STM32 系列：
 *     F0, F1, F2, F3, F4, F7, G0, G4, H5, H7,
 *     L0, L1, L4, L5, WB, WL, MP1
 *
 *   序列号格式：
 *     {PREFIX}-{YY}{MM}{NNNN}-{CRC32_HEX}
 *     示例：RTSB-26070001-A3B7C9EF
 *
 * @author  AgiSense Tactile R&D Team
 * @version 1.1
 * @date    2026-07-24
 */

#include "product.h"
#include <stdio.h>
#include <string.h>


/* ============================================================
 *   默认序列号参数（编译时可覆写）
 *
 *   生产环境中，year/month/sequence 应通过上位机工具
 *   写入 Flash/EEPROM。此处仅提供编译时默认值用于原型阶段。
 * ============================================================ */
#ifndef PRODUCT_SN_DEFAULT_YEAR
  #define PRODUCT_SN_DEFAULT_YEAR     26U       /**< 默认生产年份 (20xx) */
#endif
#ifndef PRODUCT_SN_DEFAULT_MONTH
  #define PRODUCT_SN_DEFAULT_MONTH    1U        /**< 默认生产月份 (1-12) */
#endif
#ifndef PRODUCT_SN_DEFAULT_SEQ
  #define PRODUCT_SN_DEFAULT_SEQ      1U        /**< 默认流水号起始值 */
#endif

/* 产品序列号前缀：从产品型号中取前几个字符作为缩写 */
// #ifndef PRODUCT_SN_PREFIX
//   #define PRODUCT_SN_PREFIX           "AG"
// #endif

/* ============================================================
 *   序列号单例
 * ============================================================ */
static product_serial_t s_sn_instance;
static uint8_t         s_sn_initialized = 0;

/* 可覆写的运行时参数（0 表示使用默认值） */
static uint32_t s_custom_year     = 0;
static uint32_t s_custom_month    = 0;
static uint32_t s_custom_sequence = 0;

/* ============================================================
 *   软件 CRC32 实现
 *
 *   多项式：0xEDB88320（反射形式，等价于 0x04C11DB7）
 *   初始值：0xFFFFFFFF
 *   结果异或：0xFFFFFFFF
 *
 *   与 Python binascii.crc32() 及 PKZIP/ethernet CRC32 一致。
 * ============================================================ */
/* ==========================================================================
 *  Change: 新增
 *  Editor: 谢峰
 *  Time: 2026-09-16
 *  Range: 抽取任意长度 CRC32 实现，供 UID 与串口帧协议共同使用
 * ========================================================================== */
uint32_t product_calc_crc32(const uint8_t *data, uint32_t len)
{
    uint32_t crc = 0xFFFFFFFFUL;
    uint32_t i, j;

    if (data == NULL) {
        return 0;
    }

    for (i = 0; i < len; i++) {
        crc ^= data[i];
        for (j = 0; j < 8; j++) {
            if (crc & 1UL) {
                crc = (crc >> 1) ^ 0xEDB88320UL;
            } else {
                crc >>= 1;
            }
        }
    }

    return ~crc;
}

/* ==========================================================================
 *  Change: 修改
 *  Editor: 谢峰
 *  Time: 2026-09-16
 *  Range: UID CRC32 改为复用通用 CRC32 实现，保持原有 API 行为
 * ========================================================================== */
uint32_t product_calc_uid_crc32(const uint8_t *data)
{
    return product_calc_crc32(data, STM32_UID_LEN);
}

/* ============================================================
 *   读取芯片唯一 ID
 *
 *   直接通过基地址指针访问系统 ROM 区域。
 *   Arm Cortex-M 架构下该地址可读，无需额外权限。
 * ============================================================ */
void product_read_uid(uint8_t *buf)
{
    const uint8_t *p_uid = (const uint8_t *)STM32_UID_BASE;
    uint32_t i;

    if (buf == NULL) {
        return;
    }

    for (i = 0; i < STM32_UID_LEN; i++) {
        buf[i] = p_uid[i];
    }
}

/* ============================================================
 *   UID → 十六进制字符串
 * ============================================================ */
void product_uid_to_hexstr(const uint8_t *uid, char *dst)
{
    static const char hex_chars[] = "0123456789ABCDEF";
    uint32_t i;

    if (uid == NULL || dst == NULL) {
        return;
    }

    for (i = 0; i < STM32_UID_LEN; i++) {
        dst[i * 2]     = hex_chars[uid[i] >> 4];
        dst[i * 2 + 1] = hex_chars[uid[i] & 0x0F];
    }
    dst[i * 2] = '\0';
}

/* ============================================================
 *   生成产品序列号
 * ============================================================ */
void product_generate_sn(product_serial_t *sn,
                         const char *prefix,
                         uint32_t year,
                         uint32_t month,
                         uint32_t sequence)
{
    if (sn == NULL) {
        return;
    }

    /* 使用默认值填充空参数 */
    if (prefix == NULL) {
        prefix = PRODUCT_SN_PREFIX;
    }
    if (year == 0) {
        year = PRODUCT_SN_DEFAULT_YEAR;
    }
    if (month == 0) {
        month = PRODUCT_SN_DEFAULT_MONTH;
    }
    if (sequence == 0) {
        sequence = PRODUCT_SN_DEFAULT_SEQ;
    }

    /* 读取芯片 UID */
    product_read_uid(sn->raw_uid);

    /* 计算 UID 的 CRC32 指纹 */
    sn->uid_crc32 = product_calc_uid_crc32(sn->raw_uid);

    /* 组装序列号字符串 */
    snprintf(sn->text, sizeof(sn->text),
             "%s-%02lu%02lu%04lu-%08lX",
             prefix,
             (unsigned long)(year % 100),       /* 取年份后两位 */
             (unsigned long)(month % 100),       /* 月份，最多两位 */
             (unsigned long)(sequence % 10000U), /* 流水号，最多 4 位 */
             (unsigned long)sn->uid_crc32);
}

/* ============================================================
 *   序列号单例初始化（内部函数）
 * ============================================================ */
static void product_sn_lazy_init(void)
{
    uint32_t year, month, seq;

    /* 优先使用运行时覆写参数，否则用默认值 */
    year  = s_custom_year     ? s_custom_year     : PRODUCT_SN_DEFAULT_YEAR;
    month = s_custom_month    ? s_custom_month    : PRODUCT_SN_DEFAULT_MONTH;
    seq   = s_custom_sequence ? s_custom_sequence : PRODUCT_SN_DEFAULT_SEQ;

    product_generate_sn(&s_sn_instance, PRODUCT_SN_PREFIX, year, month, seq);
    s_sn_initialized = 1;
}

/* ============================================================
 *   获取产品序列号（公共 API）
 * ============================================================ */
const product_serial_t *product_get_sn(void)
{
    if (!s_sn_initialized) {
        product_sn_lazy_init();
    }
    return &s_sn_instance;
}

/* ============================================================
 *   重新初始化序列号参数
 * ============================================================ */
void product_sn_reinit(uint32_t year, uint32_t month, uint32_t sequence)
{
    s_custom_year     = year;
    s_custom_month    = month;
    s_custom_sequence = sequence;

    /* 使用新参数重新生成 */
    product_generate_sn(&s_sn_instance,
                        PRODUCT_SN_PREFIX,
                        year  ? year  : PRODUCT_SN_DEFAULT_YEAR,
                        month ? month : PRODUCT_SN_DEFAULT_MONTH,
                        sequence ? sequence : PRODUCT_SN_DEFAULT_SEQ);
    s_sn_initialized = 1;
}

/* ==========================================================================
 *  Change: 修改
 *  Editor: Thompson
 *  Time: 2026-08-25
 *  Range: 数据帧打包改为按 16 位传输：去掉 (val & 0x0FFF) 12 位掩码，完整发送 16 位值；
 *         校验和相应改为数据区完整值累加（上位机协议已同步更新）
 * ========================================================================== */
/* ============================================================
 *   数据帧打包
 *
 *   将16位数据矩阵打包为串行传输帧格式。
 *   帧结构：
 *     [0-1]   帧头: 0xFF 0x66
 *     [2-3]   计数器: CNT_H + CNT_L (MSB first)
 *     [4-N]   数据区: rows × cols × 2 字节 (MSB first)
 *     [N+1-4] CRC32:  小端序 CRC-32/ISO-HDLC（覆盖计数器 + 数据区）
 *
 * 数据按 16 位传输：每个 16 位值完整发送（大端序，高字节在前）。
 * ============================================================ */
int product_pack_frame(const uint16_t *data,
                       uint16_t rows,
                       uint16_t cols,
                       uint16_t counter,
                       uint8_t *out_buf,
                       uint32_t *out_len)
{
    /* 参数校验 */
    if (data == NULL || out_buf == NULL || out_len == NULL) {
        return -1;
    }
    if (rows == 0 || rows > FRAME_ROWS_MAX || cols == 0 || cols > FRAME_COLS_MAX) {
        return -1;
    }

    uint32_t data_len = (uint32_t)rows * (uint32_t)cols;
    uint32_t frame_len = FRAME_OVERHEAD + data_len * 2U;
    uint32_t offset = 0;

    /* 帧头 */
    out_buf[offset++] = FRAME_HEADER_0;
    out_buf[offset++] = FRAME_HEADER_1;

    /* 计数器 (MSB first) */
    out_buf[offset++] = (uint8_t)((counter >> 8) & 0xFFU);
    out_buf[offset++] = (uint8_t)(counter & 0xFFU);

    /* ==========================================================================
     *  Change: 修改
     *  Editor: 谢峰
     *  Time: 2026-09-16
     *  Range: 数据帧尾改为与 agr_8.py 一致的小端 CRC32，覆盖计数器与数据区
     * ========================================================================== */
    /* 数据区 (MSB first) */
    for (uint32_t i = 0; i < data_len; i++) {
        uint16_t val = data[i];   /* 按16位传输 */
        out_buf[offset++] = (uint8_t)((val >> 8) & 0xFFU);
        out_buf[offset++] = (uint8_t)(val & 0xFFU);
    }

    /* CRC32 (LSB first) */
    uint32_t crc = product_calc_crc32(&out_buf[FRAME_HEADER_LEN],
                                      FRAME_COUNTER_LEN + data_len * 2U);
    out_buf[offset++] = (uint8_t)(crc & 0xFFU);
    out_buf[offset++] = (uint8_t)((crc >> 8) & 0xFFU);
    out_buf[offset++] = (uint8_t)((crc >> 16) & 0xFFU);
    out_buf[offset++] = (uint8_t)((crc >> 24) & 0xFFU);

    *out_len = frame_len;
    return 0;
}

/**
 * @brief  原地打包：将16位数据矩阵打包为串行传输帧格式（输入输出同一缓冲）。
 *
 * @details
 * 与 product_pack_frame 帧格式完全相同，打包结果写回 data 所在缓冲，
 * 调用者须确保缓冲长度 ≥ FRAME_OVERHEAD + rows × cols × 2。
 * 从尾部逆序写入大端数据，避免覆盖尚未读取的原始样本。
 *
 * @param  data       in/out: 16位数据数组（打包后为帧字节流）
 * @param  rows       行数 [1, 32]
 * @param  cols       列数 [1, 32]
 * @param  counter    帧计数器 (0-65535)
 * @param  out_len    输出：实际打包的字节数
 *
 * @return 0 成功, -1 参数错误
 */
int product_pack_frame_inplace(uint16_t *data,
                               uint16_t rows,
                               uint16_t cols,
                               uint16_t counter,
                               uint32_t *out_len)
{
    /* 参数校验 */
    if (data == NULL || out_len == NULL) {
        return -1;
    }
    if (rows == 0 || rows > FRAME_ROWS_MAX || cols == 0 || cols > FRAME_COLS_MAX) {
        return -1;
    }

    uint32_t data_len = (uint32_t)rows * (uint32_t)cols;
    uint32_t frame_len = FRAME_OVERHEAD + data_len * 2U;
    uint8_t *buf = (uint8_t *)data;
    /* ==========================================================================
     *  Change: 修改
     *  Editor: 谢峰
     *  Time: 2026-09-16
     *  Range: 原地打包帧尾改为与 agr_8.py 一致的小端 CRC32
     * ========================================================================== */
    /* 数据区 (MSB first)：从尾部逆序写，避免覆盖未读取的原始样本 */
    for (uint32_t i = data_len; i > 0U; i--) {
        uint16_t val = data[i - 1U];   /* 按16位传输 */
        buf[FRAME_HEADER_LEN + FRAME_COUNTER_LEN + (i - 1U) * 2U]     = (uint8_t)((val >> 8) & 0xFFU);
        buf[FRAME_HEADER_LEN + FRAME_COUNTER_LEN + (i - 1U) * 2U + 1U] = (uint8_t)(val & 0xFFU);
    }

    /* 计数器 (MSB first) */
    buf[2U] = (uint8_t)((counter >> 8) & 0xFFU);
    buf[3U] = (uint8_t)(counter & 0xFFU);

    /* 帧头 */
    buf[0U] = FRAME_HEADER_0;
    buf[1U] = FRAME_HEADER_1;

    /* CRC32 (LSB first) */
    uint32_t crc = product_calc_crc32(&buf[FRAME_HEADER_LEN],
                                      FRAME_COUNTER_LEN + data_len * 2U);
    buf[frame_len - 4U] = (uint8_t)(crc & 0xFFU);
    buf[frame_len - 3U] = (uint8_t)((crc >> 8) & 0xFFU);
    buf[frame_len - 2U] = (uint8_t)((crc >> 16) & 0xFFU);
    buf[frame_len - 1U] = (uint8_t)((crc >> 24) & 0xFFU);

    *out_len = frame_len;
    return 0;
}
