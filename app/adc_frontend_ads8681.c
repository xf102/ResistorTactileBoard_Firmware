/**
 * @file adc_frontend_ads8681.c
 * @brief ADS8681 + CD74HC4067 implementation of the generic ADC frontend.
 */

#include "adc_frontend.h"

#include <string.h>

#include "bsp_init.h"
#include "drv_cd74hc4067.h"

#if BSP_ADC_FRONTEND != BSP_ADC_FRONTEND_ADS8681_MUX
#error "adc_frontend_ads8681.c compiled while ADS8681 frontend is not selected"
#endif

#define ADC_FRONTEND_MUX_SETTLE_US  10U

static uint8_t s_rx_buffer[4];
static uint16_t s_columns;
static uint16_t s_expected_step;
static uint16_t s_active_step;
static bool s_row_prepared;
static bool s_transfer_active;

/* ==========================================================================
 *  Change: 新增
 *  Editor: 谢峰
 *  Time: 2026-09-18
 *  Range: 提供72MHz Cortex-M3下短时MUX稳定等待，仅在采样定时中断中使用
 * ========================================================================== */
static void adc_frontend_delay_us(uint32_t delay_us)
{
    volatile uint32_t loops = delay_us * 18U;
    while (loops > 0U) {
        --loops;
        __NOP();
    }
}

/* ==========================================================================
 *  Change: 新增
 *  Editor: 谢峰
 *  Time: 2026-09-18
 *  Range: 初始化 ADS8681 前端的软件流水状态，不重复执行 BSP 硬件初始化
 * ========================================================================== */
bool adc_frontend_init(void)
{
    memset(s_rx_buffer, 0, sizeof(s_rx_buffer));
    s_columns = BSP_MATRIX_COLS;
    s_expected_step = 0U;
    s_active_step = 0U;
    s_row_prepared = false;
    s_transfer_active = false;

    return (g_bsp_adc.hspi != NULL) && (g_bsp_adc.hspi->Instance == SPI1);
}

/* ==========================================================================
 *  Change: 新增
 *  Editor: 谢峰
 *  Time: 2026-09-18
 *  Range: 在扫描停止状态下设置本行有效列数，限制到产品矩阵范围
 * ========================================================================== */
bool adc_frontend_configure(uint16_t columns)
{
    if ((columns == 0U) || (columns > BSP_MATRIX_COLS) || s_transfer_active) {
        return false;
    }
    s_columns = columns;
    return true;
}

/* ==========================================================================
 *  Change: 新增
 *  Editor: 谢峰
 *  Time: 2026-09-18
 *  Range: 在行激励稳定后复位 ADS8681 一拍流水步骤
 * ========================================================================== */
bool adc_frontend_prepare_row(uint16_t row)
{
    if ((row >= BSP_MATRIX_ROWS) || s_transfer_active || (s_columns == 0U)) {
        return false;
    }

    s_expected_step = 0U;
    s_active_step = 0U;
    s_row_prepared = true;
    return true;
}

/* ==========================================================================
 *  Change: 新增
 *  Editor: 谢峰
 *  Time: 2026-09-18
 *  Range: 选择MUX列并启动4字节NOP DMA，末步骤保持末列用于冲刷结果
 * ========================================================================== */
bool adc_frontend_start_step(uint16_t step)
{
    uint16_t mux_column;

    if (!s_row_prepared || s_transfer_active || (step != s_expected_step) ||
        (step > s_columns)) {
        return false;
    }

    mux_column = (step < s_columns) ? step : (uint16_t)(s_columns - 1U);
    CD74HC4067_Select((uint8_t)mux_column);
    adc_frontend_delay_us(ADC_FRONTEND_MUX_SETTLE_US);

    s_active_step = step;
    s_transfer_active = true;
    if (ADS8681_ReadRaw_DMA(&g_bsp_adc, s_rx_buffer) != ADS8681_OK) {
        s_transfer_active = false;
        return false;
    }

    return true;
}

/* ==========================================================================
 *  Change: 新增
 *  Editor: 谢峰
 *  Time: 2026-09-18
 *  Range: 结束DMA并把步骤N返回值映射到N-1列，步骤0仅预充流水线
 * ========================================================================== */
adc_frontend_result_t adc_frontend_on_dma_complete(void)
{
    adc_frontend_result_t result = {
        .code = ADC_FRONTEND_RESULT_ERROR,
        .column = 0U,
        .sample = 0U,
        .row_complete = false,
    };

    if (!s_transfer_active) {
        return result;
    }

    ADS8681_DMA_RxCpltCallback(&g_bsp_adc);
    s_transfer_active = false;

    if (s_active_step == 0U) {
        result.code = ADC_FRONTEND_RESULT_PENDING;
        s_expected_step = 1U;
        return result;
    }

    result.code = ADC_FRONTEND_RESULT_SAMPLE;
    result.column = (uint16_t)(s_active_step - 1U);
    result.sample = ((uint16_t)s_rx_buffer[0] << 8U) | s_rx_buffer[1];
    result.row_complete = (s_active_step == s_columns);

    if (result.row_complete) {
        s_row_prepared = false;
        s_expected_step = 0U;
    } else {
        s_expected_step = (uint16_t)(s_active_step + 1U);
    }
    return result;
}

/* ==========================================================================
 *  Change: 新增
 *  Editor: 谢峰
 *  Time: 2026-09-18
 *  Range: 故障恢复时终止SPI DMA、释放CS并清空半行流水状态
 * ========================================================================== */
void adc_frontend_abort(void)
{
    if ((g_bsp_adc.hspi != NULL) && s_transfer_active) {
        (void)HAL_SPI_Abort(g_bsp_adc.hspi);
    }
    HAL_GPIO_WritePin(g_bsp_adc.cs_port, g_bsp_adc.cs_pin, GPIO_PIN_SET);
    g_bsp_adc.dma_busy = 0U;
    s_transfer_active = false;
    s_row_prepared = false;
    s_expected_step = 0U;
}

/* ==========================================================================
 *  Change: 新增
 *  Editor: 谢峰
 *  Time: 2026-09-18
 *  Range: 为扫描服务提供无阻塞前端空闲状态查询
 * ========================================================================== */
bool adc_frontend_is_ready(void)
{
    return !s_transfer_active && (g_bsp_adc.dma_busy == 0U);
}
