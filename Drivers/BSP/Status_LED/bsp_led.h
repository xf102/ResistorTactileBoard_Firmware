#ifndef __BSP_LED_H__
#define __BSP_LED_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include "bsp_config.h"   /* Centralized RTT header (replaces direct #include <rtthread.h>) */


/* Status LED identifiers */
typedef enum {
    BSP_STATUS_LED_0 = 0,   /**< First status LED (PB0, active high) */
    BSP_STATUS_LED_1,       /**< Second status LED (PB1, active high) */
    BSP_STATUS_LED_MAX      /**< Number of status LEDs, not a valid ID */
} bsp_status_led_t;

/* State values */
#define BSP_LED_OFF  0  /**< Turn LED off */
#define BSP_LED_ON   1  /**< Turn LED on  */

/* ============================================================
 * Status LED patterns (driven by a background thread via IPC)
 * ============================================================ */
typedef enum {
    BSP_LED_PATTERN_OFF = 0,       /**< Steady off */
    BSP_LED_PATTERN_ON,            /**< Steady on */
    BSP_LED_PATTERN_BLINK_SLOW,    /**< 1 Hz blink, 50% duty */
    BSP_LED_PATTERN_BLINK_FAST,    /**< 5 Hz blink, 50% duty */
    BSP_LED_PATTERN_DOUBLE_BLINK,  /**< Two quick flashes, then 850 ms off */
    BSP_LED_PATTERN_FAST_FLASH,    /**< 100 ms on, 1 s off */
    BSP_LED_PATTERN_MAX            /**< Number of patterns, not a valid value */
} bsp_led_pattern_t;

/**
 * @brief Initialize the LED GPIOs and the background status LED subsystem.
 *
 * Resets all status LEDs to OFF and starts the low-priority status LED thread
 * plus its command message queue. Call once after MX_GPIO_Init().
 */
void bsp_led_init(void);

/**
 * @brief Directly set a status LED on or off.
 *
 * This bypasses the background pattern thread and writes the GPIO immediately.
 * Use for immediate, one-shot control.
 *
 * @param led   Target status LED.
 * @param state BSP_LED_ON or BSP_LED_OFF.
 */
void bsp_led_set(bsp_status_led_t led, uint8_t state);

/**
 * @brief Turn a status LED on.
 * @param led Target status LED.
 */
void bsp_led_on(bsp_status_led_t led);

/**
 * @brief Turn a status LED off.
 * @param led Target status LED.
 */
void bsp_led_off(bsp_status_led_t led);

/**
 * @brief Toggle a status LED.
 * @param led Target status LED.
 */
void bsp_led_toggle(bsp_status_led_t led);

/* ============================================================
 *  RT-Thread dependent background pattern engine APIs
 *  Available only when BSP_USE_RTTHREAD is defined
 * ============================================================ */
#ifdef BSP_USE_RTTHREAD

/**
 * @brief Initialize the status LED background thread and IPC.
 *
 * Normally called automatically from bsp_led_init(). Creates the message queue
 * and starts the "status_led" thread that executes patterns asynchronously.
 */
void bsp_status_led_init(void);

/**
 * @brief Request a new pattern for a status LED.
 *
 * Sends a command to the background status LED thread via message queue.
 * The call is non-blocking; the thread will apply the pattern on its next
 * 10 ms poll.
 *
 * @param led     Target status LED (BSP_STATUS_LED_0 or BSP_STATUS_LED_1).
 * @param pattern Pattern to execute (see bsp_led_pattern_t).
 * @return        RT_EOK on success, negative error code otherwise.
 */
rt_err_t bsp_status_led_set(bsp_status_led_t led, bsp_led_pattern_t pattern);

#endif /* BSP_USE_RTTHREAD */

#ifdef __cplusplus
}
#endif

#endif /* __BSP_LED_H__ */
