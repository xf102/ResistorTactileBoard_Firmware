/**
 * @file adc_frontend.h
 * @brief ADC-independent sampling boundary used by the matrix scan service.
 */

#ifndef ADC_FRONTEND_H
#define ADC_FRONTEND_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    ADC_FRONTEND_RESULT_PENDING = 0,
    ADC_FRONTEND_RESULT_SAMPLE,
    ADC_FRONTEND_RESULT_ERROR
} adc_frontend_result_code_t;

typedef struct {
    adc_frontend_result_code_t code;
    uint16_t column;
    uint16_t sample;
    bool row_complete;
} adc_frontend_result_t;

/* ==========================================================================
 *  Change: 新增
 *  Editor: 谢峰
 *  Time: 2026-09-18
 *  Range: 定义与 ADC 型号无关的初始化、行准备、DMA步骤和故障恢复接口
 * ========================================================================== */
bool adc_frontend_init(void);
bool adc_frontend_configure(uint16_t columns);
bool adc_frontend_prepare_row(uint16_t row);
bool adc_frontend_start_step(uint16_t step);
adc_frontend_result_t adc_frontend_on_dma_complete(void);
void adc_frontend_abort(void);
bool adc_frontend_is_ready(void);
bool adc_frontend_read_once(uint16_t *sample);
bool adc_frontend_reset(void);

#ifdef __cplusplus
}
#endif

#endif /* ADC_FRONTEND_H */
