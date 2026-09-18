/**
 * @file    drv_TEMPLATE.c
 * @brief   TEMPLATE peripheral driver implementation.
 *
 * @author  Firmware Team
 * @version 1.0
 * @date    2026-xx-xx
 */

#include "drv_TEMPLATE.h"
#include "spi.h"          /* extern SPI_HandleTypeDef hspiX; */

/* ==========================================================================
 *  Private data
 *  ========================================================================== */

/* (shadow registers, TX/RX buffers, etc.) */

/* ==========================================================================
 *  Private helpers
 *  ========================================================================== */

/* static inline void template_pulse_cs(void) { ... } */

/* ==========================================================================
 *  Public API
 *  ========================================================================== */

void TEMPLATE_Init(TEMPLATE_HandleTypeDef* h)
{
    /* 1. Set idle GPIO states */
    /* 2. Hardware reset if applicable */
    /* 3. Clear internal state */
    /* 4. Verify communication (e.g. read DEVICE_ID) */
}

template_status_t TEMPLATE_Read(TEMPLATE_HandleTypeDef* h, uint16_t* pData)
{
    /* HAL_SPI_TransmitReceive(...) */
    return TEMPLATE_OK;
}

/* ==========================================================================
 *  Optional: RT-Thread background thread / IPC
 *  Only compiled when BSP_USE_RTTHREAD is defined.
 *  To enable: change #if 0 below to #ifdef BSP_USE_RTTHREAD
 *  (requires #include "bsp_config.h" in the .h file)
 *  ========================================================================== */
#if 0  /* <-- change to: #ifdef BSP_USE_RTTHREAD */

#define TEMPLATE_THREAD_STACK_SIZE  512
#define TEMPLATE_THREAD_PRIORITY    (RT_THREAD_PRIORITY_MAX - 3)

static struct rt_thread  template_thread;
static uint8_t           template_stack[TEMPLATE_THREAD_STACK_SIZE];

static void template_thread_entry(void *param)
{
    (void)param;
    while (1) {
        /* periodic work */
        rt_thread_delay(rt_tick_from_millisecond(10));
    }
}

void TEMPLATE_ThreadInit(void)
{
    rt_err_t ret = rt_thread_init(&template_thread,
                                  "template",
                                  template_thread_entry,
                                  RT_NULL,
                                  template_stack,
                                  sizeof(template_stack),
                                  TEMPLATE_THREAD_PRIORITY,
                                  10);
    RT_ASSERT(ret == RT_EOK);
    rt_thread_startup(&template_thread);
}

#endif /* BSP_USE_RTTHREAD */
