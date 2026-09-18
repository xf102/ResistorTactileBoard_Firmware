/**
 * @file    resistorTactile.h
 * @brief   电阻式触觉传感器产品定义及芯片 UID/序列号模块
 *
 * @details
 *          本模块定义电阻式触觉传感器产品的基础信息，包括：
 *          - 产品名称 / 型号 / 版本
 *          - 多系列 STM32 芯片唯一 ID (96-bit) 读取
 *          - 基于 UID + 产品命名规则的序列号生成
 *          - 固件 / 硬件版本信息
 *
 *          跨系列兼容性：
 *            STM32F0 / F1 / F2 / F3 / F4 / F7 / G0 / G4 /
 *            H5 / H7 / L0 / L1 / L4 / L5 / WB / WL / MP1
 *
 * @author  AgiSense Tactile R&D Team
 * @version 1.1
 * @date    2026-07-24
 */

#ifndef __RESISTOR_TACTILE_H__
#define __RESISTOR_TACTILE_H__

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================
 *  产品基本信息（编译时常量）
 * ============================================================ */
#define PRODUCT_NAME        "ResistorTactile"
#define PRODUCT_MODEL       "AG-RT-001-Prototype"
#define PRODUCT_VERSION     "1.0"
#define PRODUCT_SERIAL      "00000000000000000000000000000000"
#define PRODUCT_FIRMWARE     "1.0"
#define PRODUCT_HARDWARE     "1.0"
#define PRODUCT_COMPANY     "AgiSense Robotics Co., Ltd."
#define PRODUCT_DATE        "2026-07-24"
#define PRODUCT_SN_PREFIX           "AGR"












#ifdef __cplusplus
}
#endif
#endif /* __RESISTOR_TACTILE_H__ */