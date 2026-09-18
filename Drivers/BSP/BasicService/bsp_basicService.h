/**
 * @file    bsp_basicService.h
 * @brief   片上外设基础服务：UART console（ring buffer 输入 + 阻塞输出）
 *          + 帧缓冲池（mailbox + 预分配帧缓冲）
 *          + Flash 参数存储（系统配置持久化）。
 *
 * @details
 * 本模块从 board.c 和原 app/basic_service.c 合并而来，集中管理：
 *   -# UART1 RX ring buffer + 信号量（中断驱动 console 输入）
 *   -# rt_hw_console_output / rt_hw_console_getchar（RT-Thread console 钩子）
 *   -# 帧缓冲池：预分配 frame_buf_t + mailbox 投递 + 信号量同步
 *   -# Flash 参数存储：系统配置读写 + 参数管理
 *
 * 初始化分两阶段：
 *   - bsp_basic_service_init()  : 调度器启动前（board.c 内），初始化 UART console
 *   - bsp_frame_pool_init(r, c) : 调度器启动后（main 内），初始化帧缓冲池 + 邮箱
 *   - params_init()             : 调度器启动后（main 内），加载系统参数
 *
 * @author  Firmware Team
 * @version 1.1
 * @date    2026-07-24
 */

#ifndef __BSP_BASIC_SERVICE_H__
#define __BSP_BASIC_SERVICE_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "bsp_config.h"   /* RTT header + module switches */

/* ============================================================
 *  UART console 服务
 * ============================================================ */

/**
 * @brief  初始化 UART console 子系统（ring buffer + 信号量）。
 *
 * @note   在 rt_hw_board_init() 中、MX_USART1_UART_Init() 之后调用。
 *         不依赖堆（rt_sem_init 为静态初始化）。
 */
void bsp_basic_service_init(void);

/**
 * @brief  向 RX ring buffer 写入一个字节（由 USART1 IRQ 调用）。
 * @param  ch  接收到的字节
 */
void uart_rx_put(uint8_t ch);

/* ============================================================
 *  帧缓冲池服务
 * ============================================================ */

/** @brief 帧缓冲结构：一帧传感器数据 */
typedef struct {
    uint16_t *data;    /**< 动态分配的 uint16 数组 */
    /* ==========================================================================
     *  Change: 修改
     *  Editor: 谢峰
     *  Time: 2026-09-16
     *  Range: 明确打包后 len 表示发送字节数，避免被误当作 uint16_t 元素数
     * ========================================================================== */
    uint16_t  len;     /**< 打包后待发送的字节数 */
    uint8_t   in_use;  /**< 使用中标记：0=空闲, 1=在用 */
} frame_buf_t;

/** @brief 帧邮箱：采集线程 → USB 线程 */
extern rt_mailbox_t frame_mb;

/** @brief 帧缓冲池信号量：可用缓冲计数 */
extern rt_sem_t frame_pool_sem;

/** @brief 预分配帧缓冲数量 */
#define FRAME_POOL_COUNT 2

/**
 * @brief  初始化帧缓冲池 + 邮箱。
 *
 * @param  rows  扫描行数
 * @param  cols  扫描列数
 *
 * @note   必须在 bsp_init() 之后、调度器启动后调用（依赖 rt_malloc）。
 */
void bsp_frame_pool_init(uint16_t rows, uint16_t cols);

/**
 * @brief  从池中获取一个空帧缓冲（阻塞直到有空闲帧）。
 * @return 帧缓冲指针
 */
frame_buf_t *frame_pool_get(void);

/**
 * @brief  归还帧缓冲到池中。
 * @param  frame  待归还的帧缓冲指针
 */
void frame_pool_put(frame_buf_t *frame);

/* ============================================================
 *  Flash 参数存储服务
 * ============================================================ */

/**
 * Flash 分区配置（必须与链接脚本 STM32F103xx_FLASH.ld 保持一致）：
 *   FLASH (rx)       : ORIGIN = 0x08000000, LENGTH = 60K
 *   FLASH_CONFIG (rx): ORIGIN = 0x0800F000, LENGTH = 4K
 *
 * STM32F103C8 flash page size = 1 KB.
 */
#define CONFIG_STORAGE_BASE_ADDR    0x0800F000U
#define CONFIG_STORAGE_SIZE         0x00001000U  /* 4 KB */
#define CONFIG_STORAGE_PAGE_SIZE    0x00000400U  /* 1 KB */
#define CONFIG_STORAGE_PAGE_COUNT   (CONFIG_STORAGE_SIZE / CONFIG_STORAGE_PAGE_SIZE)

/**
 * @brief  系统参数结构体。
 *
 * @details
 * 存储在 Flash 配置分区，用于持久化系统配置。
 * 使用魔数和校验和确保数据有效性。
 */
typedef struct {
    uint32_t magic;              /**< 魔数校验 (0x5041524D = "PARM") */
    uint32_t version;            /**< 版本号 (1) */
    uint16_t scan_row_start;     /**< 扫描起始行 [0, 31] */
    uint16_t scan_row_end;       /**< 扫描结束行 [0, 31] */
    uint16_t scan_col_start;     /**< 扫描起始列 [0, 31] */
    uint16_t scan_col_end;       /**< 扫描结束列 [0, 31] */
    uint16_t scan_fps;           /**< 扫描帧率 [1, 100] */
    uint16_t reserved;           /**< 保留对齐 */
    uint32_t checksum;           /**< 校验和 */
} sys_params_t;

/** @brief 参数魔数 "PARM" */
#define PARAMS_MAGIC    0x5041524DU

/** @brief 参数版本号 */
#define PARAMS_VERSION  1U

/**
 * @brief  擦除 Flash 配置分区。
 *
 * @return true 成功, false 失败
 */
bool ConfigStorage_Erase(void);

/**
 * @brief  从 Flash 配置分区读取数据。
 *
 * @param  offset  偏移地址（相对于 CONFIG_STORAGE_BASE_ADDR）
 * @param  buf     读取缓冲区
 * @param  len     读取长度（字节）
 * @return true 成功, false 失败
 */
bool ConfigStorage_Read(uint32_t offset, void *buf, uint32_t len);

/**
 * @brief  写入数据到 Flash 配置分区。
 *
 * @note   写入前必须先擦除。数据必须半字对齐（2 字节）。
 *
 * @param  offset  偏移地址（相对于 CONFIG_STORAGE_BASE_ADDR）
 * @param  buf     写入缓冲区
 * @param  len     写入长度（字节，必须为 2 的倍数）
 * @return true 成功, false 失败
 */
bool ConfigStorage_Write(uint32_t offset, const void *buf, uint32_t len);

/**
 * @brief  从 Flash 加载系统参数。
 *
 * @param  params  参数结构体指针
 * @return true 成功（参数有效）, false 失败（使用默认值）
 */
bool params_load(sys_params_t *params);

/**
 * @brief  保存系统参数到 Flash。
 *
 * @param  params  参数结构体指针
 * @return true 成功, false 失败
 */
bool params_save(const sys_params_t *params);

/**
 * @brief  恢复默认参数。
 *
 * @param  params  参数结构体指针（输出）
 */
void params_default(sys_params_t *params);

/**
 * @brief  初始化参数系统。
 *
 * @details
 * 从 Flash 加载参数，如果无效则使用默认值。
 * 加载成功后将参数应用到系统变量。
 *
 * @note   必须在调度器启动后调用。
 */
void params_init(void);

/**
 * @brief  获取当前系统参数指针。
 *
 * @return 当前参数结构体指针
 */
const sys_params_t *params_get(void);

/**
 * @brief  将参数应用到系统变量。
 *
 * @details
 * 将 params 中的配置值应用到数据采集模块等系统变量。
 */
void params_apply(void);

#ifdef __cplusplus
}
#endif

#endif /* __BSP_BASIC_SERVICE_H__ */
