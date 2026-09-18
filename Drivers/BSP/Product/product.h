/**
 * @file    product.h
 * @brief   产品序列号模块头文件：跨系列 STM32 UID 基地址映射 + 序列号 API + 数据帧打包
 *
 * @details
 *   本头文件提供：
 *     - 多系列 STM32 96-bit UID 基地址自动适配
 *     - 产品序列号结构体定义
 *     - 序列号生成、读取、格式化的 API 声明
 *     - 数据帧打包常量与函数声明
 *
 * @author  AgiSense Tactile R&D Team
 * @version 1.1
 * @date    2026-07-24
 */

#ifndef __PRODUCT_H__
#define __PRODUCT_H__



/* ============================================================
 *   产品信息宏定义（编译时常量）
 * ============================================================ */

#define RESISTOR_TACTILE


#ifdef RESISTOR_TACTILE
  #include "resistorTactile.h"
#else
  #error "Include resistorTactile.h instead of product.h."
#endif


#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif
/* ============================================================
 *   多系列 STM32 96-bit UID 基地址映射
 *
 *   所有地址均来自 STM32 参考手册的 "Device electronic
 *   signature" 章节。96-bit UID 即 12 字节，分 3 个 32-bit
 *   字，地址连续。
 *
 *   UID 结构（通用）：
 *     Word 0 [0:3]  = X/Y 晶圆坐标
 *     Word 1 [4:7]  = 晶圆编号 (Wafer Number)
 *     Word 2 [8:11] = 批字号 (Lot Number)
 *
 *   注：不同系列字段含义可能细微不同，但唯一性不变。
 * ============================================================ */

/* -- F0 / F3: 0x1FFFF7AC -- */
#if defined(STM32F0xx) || defined(STM32F030x6) || defined(STM32F030x8) || \
    defined(STM32F031xx) || defined(STM32F038xx) || defined(STM32F042xx) || \
    defined(STM32F048xx) || defined(STM32F051xx) || defined(STM32F058xx) || \
    defined(STM32F070xb) || defined(STM32F071xx) || defined(STM32F072xx) || \
    defined(STM32F078xx) || defined(STM32F091xx) || defined(STM32F098xx) || \
    defined(STM32F3xx)  || defined(STM32F301xx) || defined(STM32F302xx) || \
    defined(STM32F303xx) || defined(STM32F334xx) || defined(STM32F373xx) || \
    defined(STM32F378xx) || defined(STM32F398xx)
  #define STM32_UID_BASE    0x1FFFF7ACU

/* -- F1: 0x1FFFF7E8 -- */
/* ==========================================================================
 *  Change: 修改
 *  Editor: 谢峰
 *  Time: 2026-09-18
 *  Range: 识别当前 CubeMX 工程使用的 STM32F103xB 器件宏
 * ========================================================================== */
#elif defined(STM32F1xx) || defined(STM32F103xB) || defined(STM32F100xx) || defined(STM32F101xx) || \
      defined(STM32F102xx) || defined(STM32F103xx) || defined(STM32F105xx) || \
      defined(STM32F107xx)
  #define STM32_UID_BASE    0x1FFFF7E8U

/* -- F2 / F4 / F7: 0x1FFF7A10 -- */
#elif defined(STM32F2xx) || defined(STM32F205xx) || defined(STM32F207xx) || \
      defined(STM32F215xx) || defined(STM32F217xx) || \
      defined(STM32F4xx) || defined(STM32F405xx) || defined(STM32F407xx) || \
      defined(STM32F410xx) || defined(STM32F411xx) || defined(STM32F412xx) || \
      defined(STM32F413xx) || defined(STM32F415xx) || defined(STM32F417xx) || \
      defined(STM32F423xx) || defined(STM32F427xx) || defined(STM32F429xx) || \
      defined(STM32F437xx) || defined(STM32F439xx) || defined(STM32F446xx) || \
      defined(STM32F469xx) || defined(STM32F479xx) || \
      defined(STM32F7xx) || defined(STM32F722xx) || defined(STM32F723xx) || \
      defined(STM32F730xx) || defined(STM32F732xx) || defined(STM32F733xx) || \
      defined(STM32F745xx) || defined(STM32F746xx) || defined(STM32F750xx) || \
      defined(STM32F756xx) || defined(STM32F765xx) || defined(STM32F767xx) || \
      defined(STM32F769xx) || defined(STM32F777xx) || defined(STM32F778xx) || \
      defined(STM32F779xx)
  #define STM32_UID_BASE    0x1FFF7A10U

/* -- G0 / G4 / L4+ / L5 / WB / WL: 0x1FFF7590 -- */
#elif defined(STM32G0xx) || defined(STM32G030xx) || defined(STM32G031xx) || \
      defined(STM32G041xx) || defined(STM32G050xx) || defined(STM32G051xx) || \
      defined(STM32G061xx) || defined(STM32G070xx) || defined(STM32G071xx) || \
      defined(STM32G081xx) || defined(STM32G0B0xx) || defined(STM32G0B1xx) || \
      defined(STM32G0C1xx) || \
      defined(STM32G4xx) || defined(STM32G431xx) || defined(STM32G441xx) || \
      defined(STM32G471xx) || defined(STM32G473xx) || defined(STM32G474xx) || \
      defined(STM32G483xx) || defined(STM32G484xx) || defined(STM32G491xx) || \
      defined(STM32G4A1xx) || \
      defined(STM32L4xx) || defined(STM32L4P5xx) || defined(STM32L4Q5xx) || \
      defined(STM32L4R5xx) || defined(STM32L4R7xx) || defined(STM32L4R9xx) || \
      defined(STM32L4S5xx) || defined(STM32L4S7xx) || defined(STM32L4S9xx) || \
      defined(STM32L5xx) || \
      defined(STM32WBxx) || defined(STM32WB15xx) || defined(STM32WB1Mxx) || \
      defined(STM32WB30xx) || defined(STM32WB35xx) || defined(STM32WB50xx) || \
      defined(STM32WB55xx) || \
      defined(STM32WLxx) || defined(STM32WLE4xx) || defined(STM32WLE5xx) || \
      defined(STM32WL3x)  || defined(STM32WL5x)
  #define STM32_UID_BASE    0x1FFF7590U

/* -- H5 / H7: 0x1FF1E800 -- */
#elif defined(STM32H5xx) || defined(STM32H503xx) || defined(STM32H523xx) || \
      defined(STM32H533xx) || defined(STM32H562xx) || defined(STM32H563xx) || \
      defined(STM32H573xx) || \
      defined(STM32H7xx) || defined(STM32H723xx) || defined(STM32H725xx) || \
      defined(STM32H730xx) || defined(STM32H730xxQ) || defined(STM32H733xx) || \
      defined(STM32H735xx) || defined(STM32H742xx) || defined(STM32H743xx) || \
      defined(STM32H745xx) || defined(STM32H747xx) || defined(STM32H750xx) || \
      defined(STM32H753xx) || defined(STM32H755xx) || defined(STM32H757xx) || \
      defined(STM32H7A3xx) || defined(STM32H7A3xxQ) || defined(STM32H7B0xx) || \
      defined(STM32H7B3xx) || defined(STM32H7B3xxQ) || defined(STM32H7R3xx) || \
      defined(STM32H7R7xx) || defined(STM32H7S3xx) || defined(STM32H7S7xx)
  #define STM32_UID_BASE    0x1FF1E800U

/* -- L0: 0x1FF80050 -- */
#elif defined(STM32L0xx) || defined(STM32L010xx) || defined(STM32L011xx) || \
      defined(STM32L021xx) || defined(STM32L031xx) || defined(STM32L041xx) || \
      defined(STM32L051xx) || defined(STM32L052xx) || defined(STM32L053xx) || \
      defined(STM32L062xx) || defined(STM32L063xx) || defined(STM32L071xx) || \
      defined(STM32L072xx) || defined(STM32L073xx) || defined(STM32L081xx) || \
      defined(STM32L082xx) || defined(STM32L083xx)
  #define STM32_UID_BASE    0x1FF80050U

/* -- L1: 0x1FF80050 -- */
#elif defined(STM32L1xx) || defined(STM32L100xx) || defined(STM32L151xx) || \
      defined(STM32L152xx) || defined(STM32L162xx)
  #define STM32_UID_BASE    0x1FF80050U

/* -- MP1: 0x5C005234 -- */
#elif defined(STM32MP1xx) || defined(STM32MP151xx) || defined(STM32MP153xx) || \
      defined(STM32MP157xx) || defined(STM32MP135xx) || defined(STM32MP137xx) || \
      defined(STM32MP25xxx)
  #define STM32_UID_BASE    0x5C005234U

/* -- 回退：默认 STM32F1 地址 -- */
#else
  #pragma message("resistorTactile.h: Unknown STM32 series, using F1 UID address (0x1FFFF7E8) as fallback")
  #define STM32_UID_BASE    0x1FFFF7E8U
#endif

/* ============================================================
 *   UID 长度 & 序列号字符串长度
 * ============================================================ */
#define STM32_UID_LEN        12U             /**< 96-bit UID = 12 bytes */
#define PRODUCT_SN_STR_LEN   32U             /**< 序列号字符串缓冲区长度 */

/* ============================================================
 *   产品序列号结构体
 * ============================================================ */
typedef struct {
    char     text[PRODUCT_SN_STR_LEN];       /**< 字符串形式序列号，如 RTSB-26070001-A3B7C9EF */
    uint8_t  raw_uid[STM32_UID_LEN];         /**< 原始 12 字节芯片 UID */
    uint32_t uid_crc32;                      /**< UID 的 CRC32 校验值（用于唯一性指纹） */
} product_serial_t;

/* ============================================================
 *   API 函数声明
 * ============================================================ */

/**
 * @brief  读取芯片 96-bit 唯一 ID。
 *
 * 直接从系统 ROM 地址读取 12 字节到用户缓冲区。
 * 跨系列自动适配正确的 UID 基地址。
 *
 * @param  buf  输出缓冲区，至少 STM32_UID_LEN 字节。
 */
void product_read_uid(uint8_t *buf);

/**
 * @brief  计算 12 字节 UID 的 CRC32 校验值。
 *
 * 纯软件 CRC32（多项式 0xEDB88320 / 0x04C11DB7），
 * 不依赖硬件 CRC 模块，跨系列一致。
 *
 * @param  uid  指向 UID 数据的指针。
 * @return CRC32 校验值。
 */
uint32_t product_calc_uid_crc32(const uint8_t *uid);

/* ==========================================================================
 *  Change: 新增
 *  Editor: 谢峰
 *  Time: 2026-09-16
 *  Range: 增加任意长度 CRC-32/ISO-HDLC 接口，供串口数据帧与 UID 共用
 * ========================================================================== */
/**
 * @brief  计算任意字节流的 CRC-32/ISO-HDLC。
 * @param  data  输入字节流。
 * @param  len   输入长度（字节）。
 * @return CRC32 校验值；data 为 NULL 时返回 0。
 */
uint32_t product_calc_crc32(const uint8_t *data, uint32_t len);

/**
 * @brief  生成产品序列号字符串。
 *
 * 格式：{prefix}-{年份}{月份}{流水号}-{UID_CRC32_HEX}
 * 示例：RTSB-26070001-A3B7C9EF
 *
 * @param  sn        输出序列号结构体指针。
 * @param  prefix    产品型号前缀（如 "RTSB"），NULL 则用 PRODUCT_SN_PREFIX。
 * @param  year      生产年份（如 26 表示 2026）。
 * @param  month     生产月份（1-12）。
 * @param  sequence  当月流水号（1-9999）。
 */
void product_generate_sn(product_serial_t *sn,
                         const char *prefix,
                         uint32_t year,
                         uint32_t month,
                         uint32_t sequence);

/**
 * @brief  获取全局产品序列号实例（单例，惰性初始化）。
 *
 * 首次调用时自动从芯片 UID 生成序列号。
 * 后续调用返回同一实例指针。
 *
 * @return 产品序列号指针，永不为 NULL。
 */
const product_serial_t *product_get_sn(void);

/**
 * @brief  使用预定义参数重新初始化序列号。
 *
 * 调用此函数可强制覆盖默认的序列号参数（年份/月份/流水号）。
 * 参数为 0 时使用编译时默认值。
 *
 * @param  year      生产年份，0=使用默认值。
 * @param  month     生产月份，0=使用默认值。
 * @param  sequence  流水号，0=使用默认值。
 */
void product_sn_reinit(uint32_t year, uint32_t month, uint32_t sequence);

/**
 * @brief  格式化 UID 为十六进制字符串。
 *
 * 输出格式："0123456789AB0123456789AB"
 *
 * @param  uid  指向 12 字节 UID 的指针。
 * @param  dst  输出缓冲区，至少 25 字节 (24 hex + '\0')。
 */
void product_uid_to_hexstr(const uint8_t *uid, char *dst);

#ifdef __cplusplus
}
#endif


/* ============================================================
 *   数据帧打包常量
 * ============================================================ */

#define FRAME_HEADER_0      0xFFU       /**< 帧头第1字节 */
#define FRAME_HEADER_1      0x66U       /**< 帧头第2字节 */
#define FRAME_HEADER_LEN    2U          /**< 帧头长度 */
#define FRAME_COUNTER_LEN   2U          /**< 计数器长度 */
/* ==========================================================================
 *  Change: 修改
 *  Editor: 谢峰
 *  Time: 2026-09-16
 *  Range: 帧尾校验由 2 字节累加和升级为 4 字节 CRC-32/ISO-HDLC
 * ========================================================================== */
#define FRAME_CHECKSUM_LEN  4U          /**< CRC32 长度（兼容原宏名称） */
#define FRAME_OVERHEAD      (FRAME_HEADER_LEN + FRAME_COUNTER_LEN + FRAME_CHECKSUM_LEN)  /**< 帧开销 = 8字节 */

#define FRAME_ROWS_MAX      32U         /**< 最大行数 */
#define FRAME_COLS_MAX      32U         /**< 最大列数 */

/**
 * @brief  将16位数据矩阵打包为串行传输帧格式。
 *
 * @details
 * 帧结构 (总长度 = 8 + rows × cols × 2):
 *   [0-1]   帧头:       0xFF 0x66
 *   [2-3]   计数器:     CNT_H(高8位) + CNT_L(低8位)
 *   [4-N]   数据区:     rows × cols × 2 字节 (MSB first)
 *   [N+1-4] CRC32:      小端序 CRC-32/ISO-HDLC
 *
 * CRC32 覆盖计数器和数据区，不包含帧头及 CRC32 本身。
 *
 * @param  data       16位数据数组，长度 = rows × cols
 * @param  rows       行数 [1, 32]
 * @param  cols       列数 [1, 32]
 * @param  counter    帧计数器 (0-65535)
 * @param  out_buf    输出缓冲区，调用者需确保足够大
 *                    最小长度 = 8 + rows × cols × 2
 * @param  out_len    输出：实际打包的字节数
 *
 * @return 0 成功, -1 参数错误
 */
int product_pack_frame(const uint16_t *data,
                       uint16_t rows,
                       uint16_t cols,
                       uint16_t counter,
                       uint8_t *out_buf,
                       uint32_t *out_len);

/**
 * @brief  原地打包：将16位数据矩阵打包为串行传输帧格式（输入输出同一缓冲）。
 *
 * @details
 * 与 product_pack_frame 帧格式完全相同，打包结果写回 data 所在缓冲，
 * 调用者须确保缓冲长度 ≥ FRAME_OVERHEAD + rows × cols × 2。
 * 采用从尾部逆序写入，避免覆盖未读取的原始数据。
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
                               uint32_t *out_len);
#endif /* __PRODUCT_H__ */
