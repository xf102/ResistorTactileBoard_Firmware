/**
 * @file    data_acquisition_debug.c
 * @brief   数据采集调试实现：msh 命令手动控制硬件。
 */

#include "data_acquisition_debug.h"
#include "data_acquisition_app.h"  /* SCAN_ROWS, SCAN_COLS, SCAN_DEAD_TIME_US */
#include "bsp_init.h"
#include "rtthread.h"
#include "rthw.h"                 /* rt_hw_us_delay */
#include "drv_74hc595.h"
#include "drv_ads8681.h"
#include "drv_cd74hc4067.h"
#include "finsh.h"
#include <stdio.h>
#include <stdlib.h>               /* atoi */
#include <string.h>
/* ========================================================================== 
 *  Change: 修改
 *  Editor: 谢峰
 *  Time: 2026-09-14
 *  Range: 现有 ADS8681/MUX 手动调试命令仅在旧前端下参与编译
 * ========================================================================== */
#if defined(DBG_CMD_ENABLE) && defined(BSP_USE_ADS8681)
/* ==========================================================================
 *  私有变量
 * ========================================================================== */

/** @brief 当前选中的行 [0, 31] */
static uint8_t dbg_row = 0;

/** @brief 当前选中的列 [0, 31] */
static uint8_t dbg_col = 0;

/** @brief 采样周期 (us) */
static uint16_t dbg_period_us = 100;

/** @brief 硬件初始化标志 */
static uint8_t hw_initialized = 0;

/* ==========================================================================
 *  私有变量
 * ========================================================================== */

 extern sys_params_t g_params;

/* ==========================================================================
 *  调试命令实现
 * ========================================================================== */

/**
 * @brief  初始化所有硬件模块。
 *
 * 用法: dbg_init
 */
static void cmd_dbg_init(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    LOG_D("Initializing hardware modules...");

    /* 74HC595 行驱动 */
    HC595_Init();
    LOG_D("   74HC595  OK");

    /* ADS8681 ADC */
    ADS8681_Init(&g_bsp_adc);
    ADS8681_SetRange(&g_bsp_adc, ADS8681_RANGE_UNIPOLAR_1_25VREF);
    LOG_D("   ADS8681  OK");

    /* CD74HC4067 列选择 */
    CD74HC4067_Init();
    LOG_D("   CD74HC4067 OK");

    hw_initialized = 1;
    LOG_D("Hardware initialization complete.");
}
MSH_CMD_EXPORT(cmd_dbg_init, init hardware modules);

/**
 * @brief  显示硬件状态。
 *
 * 用法: dbg_status
 */
static void cmd_dbg_status(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    LOG_I("=== Hardware Status ===");
    LOG_I("  Initialized : %s", hw_initialized ? "YES" : "NO");
    LOG_I("  Current Row : %u", dbg_row);
    LOG_I("  Current Col : %u", dbg_col);
    LOG_I("  Period (us) : %u", dbg_period_us);
    LOG_I("=======================");
}
MSH_CMD_EXPORT(cmd_dbg_status, show hardware status);

/**
 * @brief  选择行。
 *
 * 用法: dbg_row <0-31>
 */
static void cmd_dbg_row(int argc, char **argv)
{
    if (argc < 2) {
        LOG_I("Usage: dbg_row <0-%u>", SCAN_ROWS - 1);
        return;
    }

    uint8_t row = (uint8_t)atoi(argv[1]);
    if (row >= SCAN_ROWS) {
        LOG_E("Row out of range [0, %u]", SCAN_ROWS - 1);
        return;
    }

    dbg_row = row;

    /* 切换行：生成行掩码并写入 74HC595 */
    uint32_t mask = (1UL << row);
    HC595_Write32(mask);

    LOG_D("Row selected: %u (mask: 0x%08X)", row, mask);
}
MSH_CMD_EXPORT(cmd_dbg_row, select row [0-31]);

/**
 * @brief  选择列。
 *
 * 用法: dbg_col <0-31>
 */
static void cmd_dbg_col(int argc, char **argv)
{
    if (argc < 2) {
        LOG_I("Usage: dbg_col <0-%u>", SCAN_COLS - 1);
        return;
    }

    uint8_t col = (uint8_t)atoi(argv[1]);
    if (col >= SCAN_COLS) {
        LOG_E("Col out of range [0, %u]", SCAN_COLS - 1);
        return;
    }

    dbg_col = col;

    /* 切换列选择 */
    CD74HC4067_Select(col);

    LOG_D("Col selected: %u", col);
}
MSH_CMD_EXPORT(cmd_dbg_col, select column [0-31]);

/**
 * @brief  读取当前选中通道的 ADC 值。
 *
 * 用法: dbg_read
 */
static void cmd_dbg_read(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    if (!hw_initialized) {
        LOG_E("Hardware not initialized. Run 'dbg_init' first.");
        return;
    }

    uint16_t adc_raw = 0;
    ads8681_status_t ret = ADS8681_ReadRaw(&g_bsp_adc, &adc_raw);

    if (ret == ADS8681_OK) {
        LOG_I("[ADC] Row=%u Col=%u Raw=%u (0x%04X)",
               dbg_row, dbg_col, adc_raw, adc_raw);
    } else {
        LOG_E("[ERR] ADS8681 read failed: %d", ret);
    }
}
MSH_CMD_EXPORT(cmd_dbg_read, read ADC value at current row/col);

/**
 * @brief  读取整行数据（32 列）。
 *
 * 用法: dbg_read_row
 */
static void cmd_dbg_read_row(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    if (!hw_initialized) {
        LOG_E("Hardware not initialized. Run 'dbg_init' first.");
        return;
    }

    LOG_I("[ROW %u] Reading 32 columns...", dbg_row);

    for (uint8_t col = 0; col < SCAN_COLS; col++) {
        /* 选择列 */
        CD74HC4067_Select(col);

        /* 等待模拟稳定（短延时） */
        rt_hw_us_delay(10);

        /* 读取 ADC */
        uint16_t adc_raw = 0;
        ads8681_status_t ret = ADS8681_ReadRaw(&g_bsp_adc, &adc_raw);

        if (ret == ADS8681_OK) {
            LOG_I("  [%2u] %5u", col, adc_raw);
            /* 每 8 列换行 */
            if ((col + 1) % 8 == 0) {
                LOG_I("\n");
            }
        } else {
            LOG_E("  [%2u] ERR(%d)", col, ret);
        }
    }
    LOG_I("\n[ROW %u] Done.", dbg_row);
}
MSH_CMD_EXPORT(cmd_dbg_read_row, read all 32 columns of current row);

/**
 * @brief  设置采样周期。
 *
 * 用法: dbg_period <us>
 */
static void cmd_dbg_period(int argc, char **argv)
{
    if (argc < 2) {
        LOG_I("Usage: dbg_period <10-65535>");
        return;
    }

    uint16_t period = (uint16_t)atoi(argv[1]);
    if (period < 10) {
        period = 10;
    }

    dbg_period_us = period;

    /* 更新 TIM2 ARR（如果定时器已配置） */
    /* Note: TIM2 未在调试模式下启动，仅记录参数 */
    LOG_D("Period set to %u us", dbg_period_us);
}
MSH_CMD_EXPORT(cmd_dbg_period, set sample period in us);

/**
 * @brief  显示当前参数。
 *
 * 用法: dbg_params
 */
static void cmd_dbg_params(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    const sys_params_t *params = params_get();
    LOG_I("=== Acquisition Parameters ===");
    LOG_I("  Row Start  : %u", params->scan_row_start);
    LOG_I("  Col Start  : %u", params->scan_col_start);
    LOG_I("  Rows        : %u", params->scan_row_end - params->scan_row_start + 1);
    LOG_I("  Cols        : %u", params->scan_col_end - params->scan_col_start + 1);
    LOG_I("  Period (us) : %u", dbg_period_us);
    LOG_I("  Dead Time   : %u us", SCAN_DEAD_TIME_US);
    LOG_I("  Frame Time  : ~%u ms (32*33*%u/1000)",
           (SCAN_ROWS * (SCAN_COLS + 1) * dbg_period_us) / 1000,
           dbg_period_us);
    LOG_I("==============================");
}
MSH_CMD_EXPORT(cmd_dbg_params, show acquisition parameters);

/**
 * @brief  单步测试：选择行列 + 读取。
 *
 * 用法: dbg_test_single [row] [col]
 */
static void cmd_dbg_test_single(int argc, char **argv)
{
    if (!hw_initialized) {
        LOG_E("Hardware not initialized. Run 'dbg_init' first.");
        return;
    }

    uint8_t row = dbg_row;
    uint8_t col = dbg_col;

    if (argc >= 3) {
        row = (uint8_t)atoi(argv[1]);
        col = (uint8_t)atoi(argv[2]);
    }

    if (row >= SCAN_ROWS || col >= SCAN_COLS) {
        LOG_E("Row/Col out of range.");
        return;
    }

    /* 选择行 */
    uint32_t mask = (1UL << row);
    HC595_Write32(mask);
    dbg_row = row;

    /* 等待激励稳定 */
    rt_hw_us_delay(50);

    /* 选择列 */
    CD74HC4067_Select(col);
    dbg_col = col;

    /* 等待模拟稳定 */
    rt_hw_us_delay(10);

    /* 读取 ADC */
    uint16_t adc_raw = 0;
    ads8681_status_t ret = ADS8681_ReadRaw(&g_bsp_adc, &adc_raw);

    if (ret == ADS8681_OK) {
        LOG_I("[TEST] Row=%u Col=%u Raw=%u", row, col, adc_raw);
    } else {
        LOG_E("Read failed: %d", ret);
    }
}
MSH_CMD_EXPORT(cmd_dbg_test_single, single step test [row col]);

/**
 * @brief  行扫描测试：遍历整行。
 *
 * 用法: dbg_test_row [row]
 */
static void cmd_dbg_test_row(int argc, char **argv)
{
    if (!hw_initialized) {
        LOG_E("Hardware not initialized. Run 'dbg_init' first.");
        return;
    }

    uint8_t row = dbg_row;
    if (argc >= 2) {
        row = (uint8_t)atoi(argv[1]);
    }

    if (row >= SCAN_ROWS) {
        LOG_E("Row out of range.");
        return;
    }

    /* 选择行 */
    uint32_t mask = (1UL << row);
    HC595_Write32(mask);
    dbg_row = row;

    /* 等待激励稳定 */
    rt_hw_us_delay(50);

    LOG_I("[TEST ROW %u] Scanning...", row);

    for (uint8_t col = 0; col < SCAN_COLS; col++) {
        CD74HC4067_Select(col);
        rt_hw_us_delay(10);

        uint16_t adc_raw = 0;
        ads8681_status_t ret = ADS8681_ReadRaw(&g_bsp_adc, &adc_raw);

        if (ret == ADS8681_OK) {
            LOG_I("  [%2u] %5u", col, adc_raw);
        } else {
            LOG_E("  [%2u] ERR", col);
        }

        if ((col + 1) % 8 == 0) {
            LOG_I("\n");
        }
    }
    LOG_I("\n[TEST ROW %u] Done.", row);
}
MSH_CMD_EXPORT(cmd_dbg_test_row, row scan test [row]);

/**
 * @brief  初始化扫描线程。
 *
 * 用法: dbg_init_daq
 */
static void cmd_dbg_init_daq(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    if (!hw_initialized) {
        LOG_E("Hardware not initialized. Run 'dbg_init' first.");
        return;
    }

    DATA_ACQUISITION_Init();
    LOG_D("Data acquisition thread initialized.");
}
MSH_CMD_EXPORT(cmd_dbg_init_daq, init data acquisition thread);

/* ==========================================================================
 *  公共 API
 * ========================================================================== */

void DATA_ACQUISITION_DEBUG_Init(void)
{
    LOG_D("Debug commands registered.");
    LOG_D("Use 'dbg_init' to initialize hardware.");
    LOG_D("Use 'dbg_status' to check status.");
}

#endif /* DBG_CMD_ENABLE && BSP_USE_ADS8681 */
