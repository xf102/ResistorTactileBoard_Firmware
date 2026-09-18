#ifndef __BSP_INIT_H__
#define __BSP_INIT_H__

#ifdef __cplusplus
extern "C" {
#endif

/* ==========================================================================
 *  Change: 修改
 *  Editor: 谢峰
 *  Time: 2026-09-18
 *  Range: 明确 BSP 配置入口仅提供裸机模块开关和驱动接口
 * ========================================================================== */
#include "bsp_config.h"

/*
 * BSP initialization entry point.
 * Call this once after low-level hardware (GPIO clocks/pins) is ready.
 */
void bsp_init(void);

/* ============================================================
 *  ADS8681 global handle (defined in bsp_init.c)
 *  Application code uses this handle to access the ADC;
 *  no need to create or initialize one privately.
 * ============================================================ */
#ifdef BSP_USE_ADS8681
extern ADS8681_HandleTypeDef g_bsp_adc;
#endif

/* ==========================================================================
 *  Change: 新增
 *  Editor: 谢峰
 *  Time: 2026-09-17
 *  Range: 导出条件编译的 TPC5120S16 全局驱动句柄
 * ========================================================================== */
#ifdef BSP_USE_TPC5120S16
extern TPC5120S16_HandleTypeDef g_bsp_tpc5120;
extern tpc5120s16_status_t g_bsp_tpc5120_init_status;
#endif

#ifdef __cplusplus
}
#endif

#endif /* __BSP_INIT_H__ */
