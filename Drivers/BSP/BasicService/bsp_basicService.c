/**
 * @file    bsp_basicService.c
 * @brief   片上外设基础服务实现：UART console + 帧缓冲池 + Flash 参数存储。
 *
 * @details
 * 合并来源：
 *   - rt-thread-nano/board.c 中的 UART RX ring buffer + console 输出/输入
 *   - app/basic_service.c 中的帧缓冲池 + 邮箱
 *   - Core/Src/config_storage.c 中的 Flash 读写（整合到本模块）
 *
 * @author  Firmware Team
 * @version 1.1
 * @date    2026-07-24
 */

#include "bsp_basicService.h"
#include "usart.h"        /* extern UART_HandleTypeDef huart1; */
#include "stm32f1xx_hal.h"
#include "finsh.h"
#include <string.h>
#include <stdlib.h>               /* atoi */
#include <stdio.h>

/* ============================================================
 *  UART console 服务
 * ============================================================ */

#ifdef RT_USING_CONSOLE

#define UART_RX_BUF_SIZE 128

static volatile uint8_t  uart_rx_buf[UART_RX_BUF_SIZE];
static volatile uint16_t uart_rx_head = 0;
static volatile uint16_t uart_rx_tail = 0;
static struct rt_semaphore uart_rx_sem;

/**
 * @brief  向 RX ring buffer 写入一个字节。
 *
 * @details
 * 由 USART1_IRQHandler 在 RXNE 中断中调用。
 * 写入成功后释放信号量，唤醒等待输入的 shell 线程。
 *
 * @param  ch  接收到的字节
 */
void uart_rx_put(uint8_t ch)
{
    uint16_t next = (uart_rx_head + 1) % UART_RX_BUF_SIZE;
    if (next != uart_rx_tail)
    {
        uart_rx_buf[uart_rx_head] = ch;
        uart_rx_head = next;
        rt_sem_release(&uart_rx_sem);
    }
}

/**
 * @brief  初始化 UART console 子系统。
 *
 * @details
 * 初始化 RX ring buffer 信号量。USART1 外设已由 MX_USART1_UART_Init()
 * 完成配置（含 RXNE 中断 + NVIC），此处仅做软件层初始化。
 *
 * @note   在 rt_hw_board_init() 中调用，不依赖堆。
 */
void bsp_basic_service_init(void)
{
    rt_sem_init(&uart_rx_sem, "uart_rx", 0, RT_IPC_FLAG_FIFO);
}

/**
 * @brief  RT-Thread console 输出钩子（阻塞式逐字节发送）。
 * @param  str  待输出的字符串
 */
void rt_hw_console_output(const char *str)
{
    while (*str)
    {
        HAL_UART_Transmit(&huart1, (uint8_t *)str, 1, HAL_MAX_DELAY);
        str++;
    }
}

/**
 * @brief  RT-Thread console 输入钩子（阻塞等待 ring buffer 数据）。
 * @return 读取到的字符
 */
char rt_hw_console_getchar(void)
{
    char ch;

    rt_sem_take(&uart_rx_sem, RT_WAITING_FOREVER);

    ch = (char)uart_rx_buf[uart_rx_tail];
    uart_rx_tail = (uart_rx_tail + 1) % UART_RX_BUF_SIZE;
    return ch;
}

#else /* !RT_USING_CONSOLE */

void bsp_basic_service_init(void) { /* no console, nothing to init */ }
void uart_rx_put(uint8_t ch) { (void)ch; }

#endif /* RT_USING_CONSOLE */

/* ============================================================
 *  帧缓冲池服务
 * ============================================================ */

/** @brief 帧邮箱对象 */
rt_mailbox_t frame_mb = RT_NULL;
static struct rt_mailbox frame_mb_obj;

/** @brief 帧缓冲池信号量 */
rt_sem_t frame_pool_sem = RT_NULL;

/** @brief 预分配帧缓冲数组 */
static frame_buf_t frame_pool[FRAME_POOL_COUNT];

/** @brief 邮箱消息池（每条消息 = 1 个指针，4 字节） */
static rt_uint8_t frame_mb_pool[FRAME_POOL_COUNT * 4];

/** @brief 帧尺寸（初始化时设定） */
static uint16_t g_rows = 0;
static uint16_t g_cols = 0;

/**
 * @brief  初始化帧缓冲池 + 邮箱。
 *
 * @details
 * 执行以下操作：
 *   -# 初始化邮箱（FIFO，FRAME_POOL_COUNT 条消息）
 *   -# 创建信号量（初始值 = FRAME_POOL_COUNT）
 *   -# 预分配所有帧缓冲的 data 数组（rt_malloc）
 *
 * @param  rows  扫描行数
 * @param  cols  扫描列数
 *
 * @note   依赖堆，必须在调度器启动后调用。
 */
void bsp_frame_pool_init(uint16_t rows, uint16_t cols)
{
    rt_err_t ret;

    g_rows = rows;
    g_cols = cols;
    uint16_t total = rows * cols;
    /* ==========================================================================
     *  Change: 修改
     *  Editor: Thompson
     *  Time: 2026-08-14
     *  Range: 帧缓冲尺寸改为 FRAME_OVERHEAD + total*2，容纳打包后帧数据；
     *         扫描线程打包后 frame->len 保存打包字节数，USART2 线程按其发送
     * ========================================================================== */
    uint32_t alloc_size = FRAME_OVERHEAD + total * sizeof(uint16_t);

    /* 初始化邮箱 */
    frame_mb = &frame_mb_obj;
    ret = rt_mb_init(&frame_mb_obj,
                     "frm_mb",
                     frame_mb_pool,
                     FRAME_POOL_COUNT,
                     RT_IPC_FLAG_FIFO);
    RT_ASSERT(ret == RT_EOK);

    /* 初始化信号量：所有缓冲可用 */
    frame_pool_sem = rt_sem_create("frm_pool", FRAME_POOL_COUNT, RT_IPC_FLAG_FIFO);
    RT_ASSERT(frame_pool_sem != RT_NULL);

    /* ==========================================================================
     *  Change: 修改
     *  Editor: Thompson
     *  Time: 2026-08-14
     *  Range: 帧缓冲分配失败防护：堆不足时将对应帧永久标记为不可用并同步扣减
     *         池信号量计数，避免扫描/USART2 线程拿到 NULL data 指针触发 HardFault
     * ========================================================================== */
    /* 预分配所有帧缓冲 */
    for (int i = 0; i < FRAME_POOL_COUNT; i++) {
        frame_pool[i].data = (uint16_t *)rt_malloc(alloc_size);
        if (frame_pool[i].data == RT_NULL) {
            LOG_E("[POOL] frame[%d] malloc %u bytes failed (heap exhausted)!",
                  i, (unsigned int)alloc_size);
            frame_pool[i].len    = 0;
            frame_pool[i].in_use = 1;        /* 永久占用：不再对外分配，防止 NULL 解引用 */
            rt_sem_take(frame_pool_sem, 0U); /* 池可用计数同步减 1 */
            continue;
        }
        frame_pool[i].len  = total;
        frame_pool[i].in_use = 0;   /* 初始为空闲状态 */
    }
}

/**
 * @brief  从池中获取一个空帧缓冲。
 *
 * @details
 * 先获取信号量（阻塞直到有空闲帧），然后扫描池找到空闲缓冲。
 * 取出后将 data 指针置 NULL 标记为"在用"，实际数据由调用者持有。
 *
 * @return 帧缓冲指针（不会返回 NULL）
 */
frame_buf_t *frame_pool_get(void)
{
    rt_sem_take(frame_pool_sem, RT_WAITING_FOREVER);

    for (int i = 0; i < FRAME_POOL_COUNT; i++) {
        if (!frame_pool[i].in_use) {
            frame_pool[i].in_use = 1;   /* 标记为在用 */
            return &frame_pool[i];       /* 返回帧，data 保持有效 */
        }
    }
    return RT_NULL;  /* 不可达 */
}

/**
 * @brief  归还帧缓冲到池中。
 *
 * @details
 * 恢复 data 指针（从未释放，仅在 get 时分离），然后释放信号量。
 *
 * @param  frame  待归还的帧缓冲指针
 */
void frame_pool_put(frame_buf_t *frame)
{
    frame->in_use = 0;  /* 标记为空闲 */
    rt_sem_release(frame_pool_sem);
}

/* ============================================================
 *  Flash 参数存储服务
 * ============================================================ */

/** @brief 当前系统参数（RAM 缓存） */
static sys_params_t g_params;

/** @brief 参数已初始化标志 */
static bool g_params_initialized = false;

/**
 * @brief  擦除 Flash 配置分区。
 *
 * @return true 成功, false 失败
 */
bool ConfigStorage_Erase(void)
{
    LOG_D("[FLASH] Erasing %u pages at 0x%08X...", 
               CONFIG_STORAGE_PAGE_COUNT, CONFIG_STORAGE_BASE_ADDR);
    HAL_FLASH_Unlock();

    FLASH_EraseInitTypeDef erase = {0};
    erase.TypeErase   = FLASH_TYPEERASE_PAGES;
    erase.PageAddress = CONFIG_STORAGE_BASE_ADDR;
    erase.NbPages     = CONFIG_STORAGE_PAGE_COUNT;

    uint32_t error = 0;
    HAL_StatusTypeDef status = HAL_FLASHEx_Erase(&erase, &error);

    HAL_FLASH_Lock();

        LOG_D("[FLASH] Erase result: status=%d error=0x%08X", status, error);
    return (status == HAL_OK && error == 0xFFFFFFFFU);
}

/**
 * @brief  从 Flash 配置分区读取数据。
 *
 * @param  offset  偏移地址（相对于 CONFIG_STORAGE_BASE_ADDR）
 * @param  buf     读取缓冲区
 * @param  len     读取长度（字节）
 * @return true 成功, false 失败
 */
bool ConfigStorage_Read(uint32_t offset, void *buf, uint32_t len)
{
    if (buf == NULL || (offset + len) > CONFIG_STORAGE_SIZE)
        return false;

    const uint8_t *src = (const uint8_t *)(CONFIG_STORAGE_BASE_ADDR + offset);
    memcpy(buf, src, len);
        LOG_P("Load success");
    return true;
}

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
bool ConfigStorage_Write(uint32_t offset, const void *buf, uint32_t len)
{
    LOG_D("[FLASH] Write: offset=%u len=%u", offset, len);
    if (buf == NULL || (offset + len) > CONFIG_STORAGE_SIZE)
        return false;

    /* STM32F1 internal flash programs in 16-bit half-words. */
    if ((offset % 2U) != 0U || (len % 2U) != 0U)
        return false;

    const uint8_t *src = (const uint8_t *)buf;
    uint32_t base = CONFIG_STORAGE_BASE_ADDR + offset;

    HAL_FLASH_Unlock();

    bool ok = true;
    for (uint32_t i = 0; i < len; i += 2U)
    {
        uint16_t halfword = (uint16_t)src[i] | ((uint16_t)src[i + 1U] << 8U);
                if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_HALFWORD, base + i, halfword) != HAL_OK)
        {
            LOG_D("[FLASH] Program failed at offset %u", i);
            ok = false;
            break;
        }
    }

        HAL_FLASH_Lock();
    LOG_D("[FLASH] Write result: %s", ok ? "OK" : "FAIL");
    return ok;
}

/**
 * @brief  计算参数校验和。
 *
 * @param  params  参数结构体指针
 * @return 校验和值
 */
static uint32_t params_calc_checksum(const sys_params_t *params)
{
    /* 校验和 = 所有字段之和（排除 checksum 字段本身） */
    uint32_t sum = 0;
    const uint8_t *p = (const uint8_t *)params;
    size_t len = offsetof(sys_params_t, checksum);

    for (size_t i = 0; i < len; i++) {
        sum += p[i];
    }
    return sum;
}

/**
 * @brief  从 Flash 加载系统参数。
 *
 * @param  params  参数结构体指针
 * @return true 成功（参数有效）, false 失败（使用默认值）
 */
bool params_load(sys_params_t *params)
{
    LOG_P("Loading params from flash...");
    if (params == NULL)
        return false;

    /* 从 Flash 读取参数 */
    if (!ConfigStorage_Read(0, params, sizeof(sys_params_t)))
        return false;

    /* 验证魔数 */
        if (params->magic != PARAMS_MAGIC) {
        LOG_E("Parameter Loading, invalid magic: 0x%08X", params->magic);
        return false;
    }

    /* 验证版本号 */
        if (params->version != PARAMS_VERSION) {
        LOG_E("Parameter Loading, invalid version: %u", params->version);
        return false;
    }

    /* 验证校验和 */
    uint32_t expected_checksum = params_calc_checksum(params);
        if (params->checksum != expected_checksum) {
        LOG_E("Parameter Loading, checksum mismatch: 0x%08X != 0x%08X", params->checksum, expected_checksum);
        return false;
    }

    /* 验证参数范围 */
    if (params->scan_row_start > 31 || params->scan_row_end > 31 ||
        params->scan_row_start > params->scan_row_end)
        return false;

    if (params->scan_col_start > 31 || params->scan_col_end > 31 ||
        params->scan_col_start > params->scan_col_end)
        return false;

    if (params->scan_fps == 0 || params->scan_fps > 100)
        return false;

        LOG_P("Load success");
    return true;
}

/**
 * @brief  保存系统参数到 Flash。
 *
 * @param  params  参数结构体指针
 * @return true 成功, false 失败
 */
bool params_save(const sys_params_t *params)
{
    LOG_P("Saving params...");
    LOG_P("Size: %u bytes", sizeof(sys_params_t));
    if (params == NULL)
        return false;

    /* 计算校验和 */
    sys_params_t tmp = *params;
    tmp.checksum = params_calc_checksum(&tmp);

    /* 擦除 Flash */
        if (!ConfigStorage_Erase()) {
        LOG_E("Flash erase failed!");
        return false;
    }

    /* 写入参数 */
        bool result = ConfigStorage_Write(0, &tmp, sizeof(sys_params_t));
    LOG_P("Save %s", result ? "success" : "failed");
    return result;
}

/**
 * @brief  恢复默认参数。
 *
 * @param  params  参数结构体指针（输出）
 */
void params_default(sys_params_t *params)
{
    if (params == NULL)
        return;

    params->magic          = PARAMS_MAGIC;
    params->version        = PARAMS_VERSION;
    /* ==========================================================================
     *  Change: 修改
     *  Editor: 谢峰
     *  Time: 2026-09-16
     *  Range: 默认扫描范围设为 8×8；可通过参数命令切换为 16×16 或 32×32
     * ========================================================================== */
    params->scan_row_start = 0;
    params->scan_row_end   = 7;
    params->scan_col_start = 0;
    params->scan_col_end   = 7;
    params->scan_fps       = 10;
    params->reserved       = 0;
    params->checksum       = 0;  /* 将在 save 时计算 */
}

/**
 * @brief  将参数应用到系统变量。
 *
 * @details
 * 将 g_params 中的配置值应用到数据采集模块。
 * 请求数据采集模块在安全帧边界重新加载参数并重配前端。
 */
void params_apply(void)
{
    LOG_P("Applying: rows[%u-%u] cols[%u-%u] fps=%u",
               g_params.scan_row_start, g_params.scan_row_end,
               g_params.scan_col_start, g_params.scan_col_end,
               g_params.scan_fps);

    /* ======================================================================
     *  Change: 修改
     *  Editor: 谢峰
     *  Time: 2026-09-17
     *  Range: 参数更新改为由采集模块串行化；运行中延迟到帧边界，避免改变活动帧尺寸
     * ====================================================================== */
    extern void scan_request_params_apply(void);

    scan_request_params_apply();
    LOG_P("Scan parameter apply requested.");
}

/**
 * @brief  初始化参数系统。
 *
 * @details
 * 从 Flash 加载参数，如果无效则使用默认值。
 * 加载成功后将参数应用到系统变量。
 *
 * @note   必须在调度器启动后调用。
 */
void params_init(void)
{
    if (params_load(&g_params)) {
        LOG_P("Loaded from flash.");
    } else {
        LOG_P("Invalid, using defaults.");
        params_default(&g_params);
    }

    g_params_initialized = true;
    params_apply();
}

/**
 * @brief  获取当前系统参数指针。
 *
 * @return 当前参数结构体指针
 */
const sys_params_t *params_get(void)
{
    return &g_params;
}

/* ============================================================
 *  msh 命令
 * ============================================================ */
#ifdef DBG_CMD_ENABLE
/**
 * @brief  显示当前系统参数。
 *
 * 用法: param_show
 */
static void cmd_param_show(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    LOG_P("=== System Parameters ===");
    LOG_P("  Row Range   : %u - %u", g_params.scan_row_start, g_params.scan_row_end);
    LOG_P("  Col Range   : %u - %u", g_params.scan_col_start, g_params.scan_col_end);
    LOG_P("  FPS         : %u", g_params.scan_fps);
    LOG_P("  Version     : %u", g_params.version);
    LOG_P("=========================");
}
MSH_CMD_EXPORT(cmd_param_show, show system parameters);

/**
 * @brief  保存当前参数到 Flash。
 *
 * 用法: param_save
 */
static void cmd_param_save(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    if (params_save(&g_params)) {
        LOG_P("Saved to flash.");

        /* ==========================================================================
         *  Change: 修改
         *  Editor: Thompson
         *  Time: 2026-08-25
         *  Range: 参数修改/保存成功后立即调用 params_apply()，使帧率/行/列设置即时生效
         *         （修复：msh 设置 20fps 后运行中的 TIM2 周期不更新、实测仍按旧帧率运行的问题）
         * ========================================================================== */
        params_apply();
    } else {
        LOG_E("Save failed!");
    }
}
MSH_CMD_EXPORT(cmd_param_save, save parameters to flash);

/**
 * @brief  从 Flash 加载参数。
 *
 * 用法: param_load
 */
static void cmd_param_load(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    if (params_load(&g_params)) {
        LOG_P("Loaded from flash.");
        params_apply();
    } else {
        LOG_P("Load failed, using defaults.");
        params_default(&g_params);
        params_apply();
    }
}
MSH_CMD_EXPORT(cmd_param_load, load parameters from flash);

/**
 * @brief  恢复默认参数。
 *
 * 用法: param_default
 */
static void cmd_param_default(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    params_default(&g_params);
    LOG_P("Restored defaults.");
    params_apply();
}
MSH_CMD_EXPORT(cmd_param_default, restore default parameters);

/**
 * @brief  设置扫描行范围。
 *
 * 用法: param_row <start> <end>
 */
static void cmd_param_row(int argc, char **argv)
{
    if (argc < 3) {
        LOG_P("Usage: param_row <start> <end>");
        return;
    }

    uint16_t start = (uint16_t)atoi(argv[1]);
    uint16_t end = (uint16_t)atoi(argv[2]);

    if (start > 31 || end > 31 || start > end) {
        LOG_E("Invalid range [0-31], start <= end");
        return;
    }

    g_params.scan_row_start = start;
    g_params.scan_row_end = end;
    LOG_P("Row range: %u - %u", start, end);

    /* ==========================================================================
     *  Change: 修改
     *  Editor: Thompson
     *  Time: 2026-08-25
     *  Range: 参数修改/保存成功后立即调用 params_apply()，使帧率/行/列设置即时生效
     *         （修复：msh 设置 20fps 后运行中的 TIM2 周期不更新、实测仍按旧帧率运行的问题）
     * ========================================================================== */
    params_apply();
}
MSH_CMD_EXPORT(cmd_param_row, set scan row range);

/**
 * @brief  设置扫描列范围。
 *
 * 用法: param_col <start> <end>
 */
static void cmd_param_col(int argc, char **argv)
{
    if (argc < 3) {
        LOG_P("Usage: param_col <start> <end>");
        return;
    }

    uint16_t start = (uint16_t)atoi(argv[1]);
    uint16_t end = (uint16_t)atoi(argv[2]);

    if (start > 31 || end > 31 || start > end) {
        LOG_E("Invalid range [0-31], start <= end");
        return;
    }

    g_params.scan_col_start = start;
    g_params.scan_col_end = end;
    LOG_P("Col range: %u - %u", start, end);

    /* ==========================================================================
     *  Change: 修改
     *  Editor: Thompson
     *  Time: 2026-08-25
     *  Range: 参数修改/保存成功后立即调用 params_apply()，使帧率/行/列设置即时生效
     *         （修复：msh 设置 20fps 后运行中的 TIM2 周期不更新、实测仍按旧帧率运行的问题）
     * ========================================================================== */
    params_apply();
}
MSH_CMD_EXPORT(cmd_param_col, set scan col range);

/**
 * @brief  设置扫描帧率。
 *
 * 用法: param_fps <1-100>
 */
static void cmd_param_fps(int argc, char **argv)
{
    if (argc < 2) {
        LOG_P("Usage: param_fps <1-100>");
        return;
    }

    uint16_t fps = (uint16_t)atoi(argv[1]);

    if (fps == 0 || fps > 100) {
        LOG_E("FPS out of range [1-100]");
        return;
    }

    g_params.scan_fps = fps;
    LOG_P("FPS: %u", fps);

    /* ==========================================================================
     *  Change: 修改
     *  Editor: Thompson
     *  Time: 2026-08-25
     *  Range: 参数修改/保存成功后立即调用 params_apply()，使帧率/行/列设置即时生效
     *         （修复：msh 设置 20fps 后运行中的 TIM2 周期不更新、实测仍按旧帧率运行的问题）
     * ========================================================================== */
    params_apply();
}
MSH_CMD_EXPORT(cmd_param_fps, set scan fps);


/**
 * @brief  读取产品信息。
 *
 * 用法: cmd_product_info
 */
static void cmd_product_info(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    LOG_P("=== Product Information ===");
    LOG_P("  Name       : %s", PRODUCT_NAME);
    LOG_P("  Model      : %s", PRODUCT_MODEL);
    LOG_P("  Version    : %s", PRODUCT_VERSION);
    LOG_P("  Serial No. : %s", PRODUCT_SERIAL);
    LOG_P("  Hardware   : %s", PRODUCT_HARDWARE);
    LOG_P("  Firmware   : %s", PRODUCT_FIRMWARE);
    LOG_P("  Company    : %s", PRODUCT_COMPANY);
    LOG_P("  Date       : %s", PRODUCT_DATE);
    LOG_P("===========================");
}
MSH_CMD_EXPORT(cmd_product_info, show product information);

#endif /* DBG_CMD_ENABLE */

