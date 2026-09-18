/**
 * @file app_events.c
 * @brief Short critical-section implementation of bare-metal event bits.
 */

#include "app_events.h"

#include "stm32f1xx.h"

static volatile uint32_t s_events;

/* ==========================================================================
 *  Change: 新增
 *  Editor: 谢峰
 *  Time: 2026-09-18
 *  Range: 初始化、发布和消费跨ISR事件，临界区保持在单次32位操作范围内
 * ========================================================================== */
void app_events_reset(void)
{
    const uint32_t primask = __get_PRIMASK();
    __disable_irq();
    s_events = 0U;
    if (primask == 0U) {
        __enable_irq();
    }
}

void app_events_set(uint32_t events)
{
    const uint32_t primask = __get_PRIMASK();
    __disable_irq();
    s_events |= events;
    if (primask == 0U) {
        __enable_irq();
    }
}

uint32_t app_events_take(uint32_t events)
{
    uint32_t taken;
    const uint32_t primask = __get_PRIMASK();

    __disable_irq();
    taken = s_events & events;
    s_events &= ~events;
    if (primask == 0U) {
        __enable_irq();
    }
    return taken;
}
