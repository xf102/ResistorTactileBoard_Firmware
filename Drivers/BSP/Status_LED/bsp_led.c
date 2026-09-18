#include "bsp_led.h"

/* ============================================================
 * Status LED thread configuration (RT-Thread only)
 * ============================================================ */
#ifdef BSP_USE_RTTHREAD
#define STATUS_LED_THREAD_STACK_SIZE    512
#define STATUS_LED_THREAD_PRIORITY      (RT_THREAD_PRIORITY_MAX - 2)  /* low priority */
#define STATUS_LED_POLL_PERIOD_MS       10
#define STATUS_LED_MQ_POOL_SIZE         4
#endif

/* Private: map BSP LED id to port/pin/active level */
typedef struct {
    GPIO_TypeDef *port;
    uint16_t      pin;
    GPIO_PinState active_lvl;
} bsp_led_desc_t;

static const bsp_led_desc_t led_desc[BSP_STATUS_LED_MAX] = {
    [BSP_STATUS_LED_0] = {
        .port       = STATUS_LED_0_PORT,
        .pin        = STATUS_LED_0_PIN,
        .active_lvl = STATUS_LED_0_ACTIVE_LVL,
    },
    [BSP_STATUS_LED_1] = {
        .port       = STATUS_LED_1_PORT,
        .pin        = STATUS_LED_1_PIN,
        .active_lvl = STATUS_LED_1_ACTIVE_LVL,
    },
};

/* ============================================================
 * Direct LED control
 * ============================================================ */

/**
 * @brief Initialize LED GPIOs and start the status LED subsystem.
 *
 * Resets all LEDs to inactive level and calls bsp_status_led_init() to start
 * the background pattern thread.
 */
void bsp_led_init(void)
{
    /* GPIO clocks and pin modes are already configured in MX_GPIO_Init().
     * Here we just ensure LEDs start in OFF state. */
    for (int i = 0; i < BSP_STATUS_LED_MAX; i++) {
        HAL_GPIO_WritePin(led_desc[i].port,
                          led_desc[i].pin,
                          GPIO_PIN_RESET);
    }

#ifdef BSP_USE_RTTHREAD
    /* Start the background status LED thread and IPC */
    bsp_status_led_init();
#endif
}

/**
 * @brief Set a status LED on/off, accounting for active-high/low wiring.
 */
void bsp_led_set(bsp_status_led_t led, uint8_t state)
{
    if (led >= BSP_STATUS_LED_MAX) {
        return;
    }

    GPIO_PinState lvl = (state ? led_desc[led].active_lvl : GPIO_PIN_RESET);
    if (led_desc[led].active_lvl == GPIO_PIN_RESET) {
        /* Active low: invert level */
        lvl = (state ? GPIO_PIN_RESET : GPIO_PIN_SET);
    }

    HAL_GPIO_WritePin(led_desc[led].port, led_desc[led].pin, lvl);
}

void bsp_led_on(bsp_status_led_t led)
{
    bsp_led_set(led, BSP_LED_ON);
}

void bsp_led_off(bsp_status_led_t led)
{
    bsp_led_set(led, BSP_LED_OFF);
}

void bsp_led_toggle(bsp_status_led_t led)
{
    if (led >= BSP_STATUS_LED_MAX) {
        return;
    }
    HAL_GPIO_TogglePin(led_desc[led].port, led_desc[led].pin);
}

/* ============================================================
 * Pattern engine (background thread) -- RT-Thread only
 * ============================================================ */
#ifdef BSP_USE_RTTHREAD

typedef struct {
    uint8_t  state;        /* 0=off, 1=on */
    uint16_t duration_ms;
} bsp_led_pattern_step_t;

typedef struct {
    const bsp_led_pattern_step_t *steps;
    uint8_t  step_count;
    uint16_t total_period_ms;
} bsp_led_pattern_desc_t;

static const bsp_led_pattern_step_t steps_off[]          = {{0, 100}};
static const bsp_led_pattern_step_t steps_on[]           = {{1, 100}};
static const bsp_led_pattern_step_t steps_blink_slow[]   = {{1, 500}, {0, 500}};
static const bsp_led_pattern_step_t steps_blink_fast[]   = {{1, 100}, {0, 100}};
static const bsp_led_pattern_step_t steps_double_blink[] = {{1, 50}, {0, 50}, {1, 50}, {0, 850}};
static const bsp_led_pattern_step_t steps_fast_flash[]   = {{1, 50}, {0, 1200}};

static const bsp_led_pattern_desc_t led_patterns[BSP_LED_PATTERN_MAX] = {
    [BSP_LED_PATTERN_OFF]          = {steps_off,          1, 100},
    [BSP_LED_PATTERN_ON]           = {steps_on,           1, 100},
    [BSP_LED_PATTERN_BLINK_SLOW]   = {steps_blink_slow,   2, 1000},
    [BSP_LED_PATTERN_BLINK_FAST]   = {steps_blink_fast,   2, 200},
    [BSP_LED_PATTERN_DOUBLE_BLINK] = {steps_double_blink, 4, 1000},
    [BSP_LED_PATTERN_FAST_FLASH]   = {steps_fast_flash,   2, 1250},
};

/**
 * @brief Compute the desired LED state for a given pattern and phase.
 */
static uint8_t led_pattern_get_state(bsp_led_pattern_t pattern, uint32_t phase_ms)
{
    if (pattern >= BSP_LED_PATTERN_MAX) {
        return BSP_LED_OFF;
    }

    const bsp_led_pattern_desc_t *desc = &led_patterns[pattern];
    uint32_t t = phase_ms % desc->total_period_ms;
    uint32_t accum = 0;

    for (int i = 0; i < desc->step_count; i++) {
        accum += desc->steps[i].duration_ms;
        if (t < accum) {
            return desc->steps[i].state;
        }
    }
    return BSP_LED_OFF;
}

/* IPC: command message queue */
typedef struct {
    bsp_status_led_t  led;
    bsp_led_pattern_t pattern;
} bsp_status_led_cmd_t;

static struct rt_messagequeue status_led_mq;
static uint8_t status_led_mq_pool[sizeof(bsp_status_led_cmd_t) * STATUS_LED_MQ_POOL_SIZE];

/* Status LED thread */
static struct rt_thread status_led_thread;
static uint8_t status_led_stack[STATUS_LED_THREAD_STACK_SIZE];

/**
 * @brief Background status LED thread entry.
 *
 * Polls the command message queue every STATUS_LED_POLL_PERIOD_MS, drains any
 * pending pattern commands, and updates each status LED by sampling its current
 * pattern step table.
 */
static void status_led_thread_entry(void *param)
{
    bsp_status_led_cmd_t cmd;
    bsp_led_pattern_t current_pattern[BSP_STATUS_LED_MAX];
    uint32_t phase_ms[BSP_STATUS_LED_MAX];

    (void)param;

    /* Default all LEDs to OFF */
    for (int i = 0; i < BSP_STATUS_LED_MAX; i++) {
        current_pattern[i] = BSP_LED_PATTERN_OFF;
        phase_ms[i] = 0;
    }

    while (1) {
        /* Drain pending commands (non-blocking).
         * RT_WAITING_NO: non-blocking receive; the while loop drains all
         * backlogged commands in one pass (later commands override earlier
         * ones, matching "only latest state" semantics).
         * Phase is reset on pattern switch so the new pattern starts from
         * its first step. */
        while (rt_mq_recv(&status_led_mq, &cmd, sizeof(cmd), RT_WAITING_NO) == RT_EOK) {
            if (cmd.led < BSP_STATUS_LED_MAX && cmd.pattern < BSP_LED_PATTERN_MAX) {
                current_pattern[cmd.led] = cmd.pattern;   /* switch pattern */
                phase_ms[cmd.led] = 0;                    /* reset phase    */
            }
        }

        /* Drive each LED according to its current pattern */
        for (int i = 0; i < BSP_STATUS_LED_MAX; i++) {
            uint8_t state = led_pattern_get_state(current_pattern[i], phase_ms[i]);
            bsp_led_set((bsp_status_led_t)i, state);

            phase_ms[i] += STATUS_LED_POLL_PERIOD_MS;
            if (phase_ms[i] >= led_patterns[current_pattern[i]].total_period_ms) {
                phase_ms[i] = 0;   /* wrap around at period boundary */
            }
        }

        rt_thread_delay(rt_tick_from_millisecond(STATUS_LED_POLL_PERIOD_MS));
    }
}

/**
 * @brief Create the status LED message queue and thread.
 */
void bsp_status_led_init(void)
{
    rt_err_t ret;

    ret = rt_mq_init(&status_led_mq,
                     "status_led",
                     status_led_mq_pool,
                     sizeof(bsp_status_led_cmd_t),
                     sizeof(status_led_mq_pool),
                     RT_IPC_FLAG_FIFO);
    RT_ASSERT(ret == RT_EOK);

    ret = rt_thread_init(&status_led_thread,
                         "status_led",
                         status_led_thread_entry,
                         RT_NULL,
                         status_led_stack,
                         sizeof(status_led_stack),
                         STATUS_LED_THREAD_PRIORITY,
                         10);
    RT_ASSERT(ret == RT_EOK);

    rt_thread_startup(&status_led_thread);
}

/**
 * @brief Post a pattern command to the status LED thread.
 */
rt_err_t bsp_status_led_set(bsp_status_led_t led, bsp_led_pattern_t pattern)
{
    bsp_status_led_cmd_t cmd;

    if (led >= BSP_STATUS_LED_MAX || pattern >= BSP_LED_PATTERN_MAX) {
        return -RT_EINVAL;
    }

    cmd.led = led;
    cmd.pattern = pattern;
    return rt_mq_send(&status_led_mq, &cmd, sizeof(cmd));
}

#endif /* BSP_USE_RTTHREAD */
