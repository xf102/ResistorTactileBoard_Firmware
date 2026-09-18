#include "bsp_init.h"

#if defined(BSP_USE_ADS8681) || defined(BSP_USE_TPC5120S16)
  #include "pin_def.h"   /* ADC_CS_PORT / ADC_RST_PORT / ADC_RVS_PORT etc. */
  #include "spi.h"       /* extern SPI_HandleTypeDef hspi1; */
#endif

/* ============================================================
 *  ADS8681 global handle
 * ============================================================ */
#ifdef BSP_USE_ADS8681
ADS8681_HandleTypeDef g_bsp_adc;
#endif

/* ==========================================================================
 *  Change: 新增
 *  Editor: 谢峰
 *  Time: 2026-09-17
 *  Range: 增加条件编译的 TPC5120S16 全局句柄
 * ========================================================================== */
#ifdef BSP_USE_TPC5120S16
TPC5120S16_HandleTypeDef g_bsp_tpc5120;
tpc5120s16_status_t g_bsp_tpc5120_init_status = TPC5120S16_STATUS_INVALID_ARG;
#endif

/*
 * Initialize all board-specific drivers/subsystems.
 * Add new BSP module init calls here as they are introduced.
 */
void bsp_init(void)
{
#ifdef BSP_USE_STATUS_LED
    /* ==========================================================================
     *  Change: 修改
     *  Editor: 谢峰
     *  Time: 2026-09-18
     *  Range: 状态灯仅执行直接 GPIO 初始化，不启动任何 RTOS 后台任务
     * ========================================================================== */
    bsp_led_init();
#endif

#ifdef BSP_USE_74HC595
    /* 74HC595 row drivers: 4 cascaded shift registers via SPI2 (32 outputs) */
    HC595_Init();
#endif

#ifdef BSP_USE_ADS8681
    /* ADS8681 ADC: populate handle -> hardware reset -> set default range */
    g_bsp_adc.hspi     = &hspi1;
    g_bsp_adc.cs_port  = ADC_CS_PORT;
    g_bsp_adc.cs_pin   = ADC_CS_PIN;
    g_bsp_adc.rst_port = ADC_RST_PORT;
    g_bsp_adc.rst_pin  = ADC_RST_PIN;
    g_bsp_adc.rvs_port = ADC_RVS_PORT;
    g_bsp_adc.rvs_pin  = ADC_RVS_PIN;

    ADS8681_Init(&g_bsp_adc);
    ADS8681_SetRange(&g_bsp_adc, BSP_ADS8681_DEFAULT_RANGE);
#endif

    /* ======================================================================
     *  Change: 新增
     *  Editor: 谢峰
     *  Time: 2026-09-17
     *  Range: 初始化 TPC5120S16 并配置 Auto-2 CH0 至默认末通道扫描
     * ====================================================================== */
#ifdef BSP_USE_TPC5120S16
    {
        tpc5120s16_status_t status;

        g_bsp_tpc5120.hspi = &hspi1;
        g_bsp_tpc5120.cs_port = TPC5120S16_CS_PORT;
        g_bsp_tpc5120.cs_pin = TPC5120S16_CS_PIN;
        g_bsp_tpc5120.alarm_port = TPC5120S16_ALARM_PORT;
        g_bsp_tpc5120.alarm_pin = TPC5120S16_ALARM_PIN;
        g_bsp_tpc5120.low_alarm_port = TPC5120S16_LOW_ALARM_PORT;
        g_bsp_tpc5120.low_alarm_pin = TPC5120S16_LOW_ALARM_PIN;

        status = TPC5120S16_InitHandle(&g_bsp_tpc5120);
        if (status == TPC5120S16_STATUS_OK) {
            status = TPC5120S16_TransferFrame(&g_bsp_tpc5120,
                                               TPC5120S16_CMD_CONTINUE,
                                               NULL);
        }
        if (status == TPC5120S16_STATUS_OK) {
            status = TPC5120S16_ConfigureAuto2(
                &g_bsp_tpc5120,
                (uint8_t)BSP_TPC5120S16_DEFAULT_LAST_CHANNEL,
                BSP_TPC5120S16_DEFAULT_RANGE);
        }
        if (status == TPC5120S16_STATUS_OK) {
            status = TPC5120S16_SynchronizeToChannel0(
                &g_bsp_tpc5120, BSP_TPC5120S16_SYNC_ATTEMPTS);
        }

        /* ==================================================================
         *  Change: 新增
         *  Editor: 谢峰
         *  Time: 2026-09-17
         *  Range: 保存初始化结果，供正常采集及矩阵诊断入口阻止故障前端继续运行
         * ================================================================== */
        g_bsp_tpc5120_init_status = status;

        if (status == TPC5120S16_STATUS_OK) {
            LOG_I("[BSP] TPC5120S16 Auto-2 CH0..CH%u synchronized",
                  (unsigned int)BSP_TPC5120S16_DEFAULT_LAST_CHANNEL);
        } else {
            LOG_E("[BSP] TPC5120S16 initialization failed, status=%u",
                  (unsigned int)status);
        }
    }
#endif

#ifdef BSP_USE_CD74HC4067_MUX
    /* CD74HC4067 column mux: both EN off, address lines cleared */
    CD74HC4067_Init();
#endif

#ifdef BSP_USE_PRODUCT
    /* Product serial number: read chip UID + generate product SN */
    // extern const product_serial_t *product_get_sn(void);
    const product_serial_t *sn = product_get_sn();
    LOG_I("Product Serial Number: %s", sn->text);
#endif
}
