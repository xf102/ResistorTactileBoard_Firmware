/* ==========================================================================
 *  Change: 新增
 *  Editor: 谢峰
 *  Time: 2026-09-17
 *  Range: 实现 TPC5120S16 阻塞/DMA 传输、模式配置、同步与告警读取
 * ========================================================================== */
#include "drv_tpc5120s16.h"

static bool TPC5120S16_HandleIsValid(const TPC5120S16_HandleTypeDef *hadc)
{
    return (hadc != NULL) && (hadc->hspi != NULL) &&
           (hadc->cs_port != NULL) && (hadc->alarm_port != NULL) &&
           (hadc->low_alarm_port != NULL);
}

static void TPC5120S16_Select(TPC5120S16_HandleTypeDef *hadc)
{
    tpc5120s16_gpio_write(hadc->cs_port, hadc->cs_pin, TPC5120S16_GPIO_LOW);
}

static void TPC5120S16_Deselect(TPC5120S16_HandleTypeDef *hadc)
{
    tpc5120s16_gpio_write(hadc->cs_port, hadc->cs_pin, TPC5120S16_GPIO_HIGH);
}

static bool TPC5120S16_RangeIsValid(tpc5120s16_range_t range)
{
    return (range == TPC5120S16_RANGE_0_TO_VREF) ||
           (range == TPC5120S16_RANGE_0_TO_2VREF);
}

uint16_t TPC5120S16_BuildManualControl(uint8_t channel,
                                       tpc5120s16_range_t range,
                                       bool power_down)
{
    if ((channel >= TPC5120S16_CHANNEL_COUNT) || !TPC5120S16_RangeIsValid(range)) {
        return TPC5120S16_INVALID_WORD;
    }

    return (uint16_t)(TPC5120S16_CMD_MANUAL |
                      TPC5120S16_CONTROL_PROGRAM_ENABLE |
                      ((uint16_t)channel << 7U) |
                      ((range == TPC5120S16_RANGE_0_TO_2VREF) ? TPC5120S16_CONTROL_RANGE_2 : 0U) |
                      (power_down ? TPC5120S16_CONTROL_POWER_DOWN : 0U));
}

uint16_t TPC5120S16_BuildAuto1Control(tpc5120s16_range_t range,
                                      bool programming_enable,
                                      bool reset_counter,
                                      bool power_down)
{
    if (!TPC5120S16_RangeIsValid(range)) {
        return TPC5120S16_INVALID_WORD;
    }

    return (uint16_t)(TPC5120S16_CMD_AUTO1 |
                      (programming_enable ? TPC5120S16_CONTROL_PROGRAM_ENABLE : 0U) |
                      (reset_counter ? TPC5120S16_CONTROL_COUNTER_RESET : 0U) |
                      ((range == TPC5120S16_RANGE_0_TO_2VREF) ? TPC5120S16_CONTROL_RANGE_2 : 0U) |
                      (power_down ? TPC5120S16_CONTROL_POWER_DOWN : 0U));
}

uint16_t TPC5120S16_BuildAuto2Program(uint8_t last_channel)
{
    return TPC5120S16_AUTO2_PROGRAM_CONST(last_channel);
}

uint16_t TPC5120S16_BuildAuto2Control(tpc5120s16_range_t range,
                                      bool programming_enable,
                                      bool reset_counter,
                                      bool power_down)
{
    if (!TPC5120S16_RangeIsValid(range)) {
        return TPC5120S16_INVALID_WORD;
    }

    return (uint16_t)(TPC5120S16_AUTO2_CONTROL_CONST(range, programming_enable, reset_counter) |
                      (power_down ? TPC5120S16_CONTROL_POWER_DOWN : 0U));
}

uint16_t TPC5120S16_BuildGpioProgram(bool gpio3_power_down,
                                     bool gpio2_range,
                                     uint8_t gpio10_function,
                                     uint8_t gpio_direction_mask)
{
    if (gpio10_function > 7U) {
        return TPC5120S16_INVALID_WORD;
    }

    return (uint16_t)(TPC5120S16_CMD_GPIO_PROGRAM |
                      (gpio3_power_down ? 0x0100U : 0U) |
                      (gpio2_range ? 0x0080U : 0U) |
                      ((uint16_t)gpio10_function << 4U) |
                      ((uint16_t)gpio_direction_mask & 0x000FU));
}

uint8_t TPC5120S16_ParseChannel(uint16_t frame)
{
    return TPC5120S16_FRAME_CHANNEL_CONST(frame);
}

uint16_t TPC5120S16_ParseCode(uint16_t frame)
{
    return TPC5120S16_FRAME_CODE_CONST(frame);
}

tpc5120s16_status_t TPC5120S16_InitHandle(TPC5120S16_HandleTypeDef *hadc)
{
    if (!TPC5120S16_HandleIsValid(hadc)) {
        return TPC5120S16_STATUS_INVALID_ARG;
    }

    TPC5120S16_Deselect(hadc);
    hadc->tx_buf[0] = 0U;
    hadc->tx_buf[1] = 0U;
    hadc->rx_buf[0] = 0U;
    hadc->rx_buf[1] = 0U;
    hadc->dma_busy = 0U;
    hadc->last_channel = 0U;
    hadc->range = TPC5120S16_RANGE_0_TO_VREF;
    tpc5120s16_delay_ms(1U);
    return TPC5120S16_STATUS_OK;
}

tpc5120s16_status_t TPC5120S16_TransferFrame(TPC5120S16_HandleTypeDef *hadc,
                                             uint16_t tx_word,
                                             uint16_t *rx_word)
{
    uint8_t tx[TPC5120S16_FRAME_BYTES];
    uint8_t rx[TPC5120S16_FRAME_BYTES];
    HAL_StatusTypeDef hal_status;

    if (!TPC5120S16_HandleIsValid(hadc)) {
        return TPC5120S16_STATUS_INVALID_ARG;
    }

    tx[0] = (uint8_t)(tx_word >> 8U);
    tx[1] = (uint8_t)tx_word;
    TPC5120S16_Select(hadc);
    hal_status = tpc5120s16_spi_transfer(hadc->hspi, tx, rx,
                                         TPC5120S16_FRAME_BYTES,
                                         TPC5120S16_SPI_TIMEOUT_MS);
    TPC5120S16_Deselect(hadc);
    if (hal_status != HAL_OK) {
        return TPC5120S16_STATUS_HAL_ERROR;
    }

    if (rx_word != NULL) {
        *rx_word = (uint16_t)(((uint16_t)rx[0] << 8U) | rx[1]);
    }
    return TPC5120S16_STATUS_OK;
}

tpc5120s16_status_t TPC5120S16_TransferFrameDMA(TPC5120S16_HandleTypeDef *hadc,
                                                uint16_t tx_word)
{
    HAL_StatusTypeDef hal_status;

    if (!TPC5120S16_HandleIsValid(hadc)) {
        return TPC5120S16_STATUS_INVALID_ARG;
    }
    if (hadc->dma_busy != 0U) {
        return TPC5120S16_STATUS_BUSY;
    }

    hadc->tx_buf[0] = (uint8_t)(tx_word >> 8U);
    hadc->tx_buf[1] = (uint8_t)tx_word;
    hadc->dma_busy = 1U;
    TPC5120S16_Select(hadc);
    hal_status = tpc5120s16_spi_transfer_dma(hadc->hspi, hadc->tx_buf,
                                             (uint8_t *)hadc->rx_buf,
                                             TPC5120S16_FRAME_BYTES);
    if (hal_status != HAL_OK) {
        TPC5120S16_Deselect(hadc);
        hadc->dma_busy = 0U;
        return TPC5120S16_STATUS_HAL_ERROR;
    }

    return TPC5120S16_STATUS_OK;
}

tpc5120s16_status_t TPC5120S16_DMAComplete(TPC5120S16_HandleTypeDef *hadc,
                                           uint16_t *rx_word)
{
    if (!TPC5120S16_HandleIsValid(hadc) || (rx_word == NULL)) {
        return TPC5120S16_STATUS_INVALID_ARG;
    }
    if (hadc->dma_busy == 0U) {
        return TPC5120S16_STATUS_HAL_ERROR;
    }

    TPC5120S16_Deselect(hadc);
    *rx_word = (uint16_t)(((uint16_t)hadc->rx_buf[0] << 8U) | hadc->rx_buf[1]);
    hadc->dma_busy = 0U;
    return TPC5120S16_STATUS_OK;
}

void TPC5120S16_DMAAbort(TPC5120S16_HandleTypeDef *hadc)
{
    if (TPC5120S16_HandleIsValid(hadc)) {
        TPC5120S16_Deselect(hadc);
        hadc->dma_busy = 0U;
    }
}

tpc5120s16_status_t TPC5120S16_ConfigureAuto1(TPC5120S16_HandleTypeDef *hadc,
                                              uint16_t channel_mask,
                                              tpc5120s16_range_t range)
{
    tpc5120s16_status_t status;

    if (!TPC5120S16_HandleIsValid(hadc) || (channel_mask == 0U) ||
        !TPC5120S16_RangeIsValid(range)) {
        return TPC5120S16_STATUS_INVALID_ARG;
    }

    status = TPC5120S16_TransferFrame(hadc, TPC5120S16_CMD_AUTO1_PROGRAM, NULL);
    if (status == TPC5120S16_STATUS_OK) {
        status = TPC5120S16_TransferFrame(hadc, channel_mask, NULL);
    }
    if (status == TPC5120S16_STATUS_OK) {
        status = TPC5120S16_TransferFrame(
            hadc, TPC5120S16_BuildAuto1Control(range, true, true, false), NULL);
    }
    return status;
}

tpc5120s16_status_t TPC5120S16_ConfigureAuto2(TPC5120S16_HandleTypeDef *hadc,
                                              uint8_t last_channel,
                                              tpc5120s16_range_t range)
{
    tpc5120s16_status_t status;

    if (!TPC5120S16_HandleIsValid(hadc) ||
        (last_channel >= TPC5120S16_CHANNEL_COUNT) ||
        !TPC5120S16_RangeIsValid(range)) {
        return TPC5120S16_STATUS_INVALID_ARG;
    }

    status = TPC5120S16_TransferFrame(hadc, TPC5120S16_GPIO_ALARM_WORD, NULL);
    if (status == TPC5120S16_STATUS_OK) {
        status = TPC5120S16_TransferFrame(
            hadc, TPC5120S16_BuildAuto2Program(last_channel), NULL);
    }
    if (status == TPC5120S16_STATUS_OK) {
        status = TPC5120S16_TransferFrame(
            hadc, TPC5120S16_BuildAuto2Control(range, true, true, false), NULL);
    }
    if (status == TPC5120S16_STATUS_OK) {
        hadc->last_channel = last_channel;
        hadc->range = range;
    }
    return status;
}

tpc5120s16_status_t TPC5120S16_SetPowerDown(TPC5120S16_HandleTypeDef *hadc,
                                            bool power_down,
                                            tpc5120s16_range_t range)
{
    uint16_t word;

    if (!TPC5120S16_HandleIsValid(hadc) || !TPC5120S16_RangeIsValid(range)) {
        return TPC5120S16_STATUS_INVALID_ARG;
    }
    word = TPC5120S16_BuildAuto2Control(range, true, false, power_down);
    return TPC5120S16_TransferFrame(hadc, word, NULL);
}

tpc5120s16_status_t TPC5120S16_SynchronizeToChannel0(TPC5120S16_HandleTypeDef *hadc,
                                                     uint8_t max_attempts)
{
    uint16_t frame;
    uint8_t attempt;
    tpc5120s16_status_t status;

    if (!TPC5120S16_HandleIsValid(hadc) || (max_attempts == 0U)) {
        return TPC5120S16_STATUS_INVALID_ARG;
    }

    for (attempt = 0U; attempt < max_attempts; ++attempt) {
        status = TPC5120S16_TransferFrame(
            hadc,
            TPC5120S16_BuildAuto2Control(hadc->range, true, true, false),
            &frame);
        if (status != TPC5120S16_STATUS_OK) {
            return status;
        }
        if (TPC5120S16_ParseChannel(frame) == 0U) {
            return TPC5120S16_STATUS_OK;
        }
    }
    return TPC5120S16_STATUS_SYNC_ERROR;
}

bool TPC5120S16_IsAlarmActive(const TPC5120S16_HandleTypeDef *hadc)
{
    return TPC5120S16_HandleIsValid(hadc) &&
           (tpc5120s16_gpio_read(hadc->alarm_port, hadc->alarm_pin) == TPC5120S16_GPIO_HIGH);
}

bool TPC5120S16_IsLowAlarmActive(const TPC5120S16_HandleTypeDef *hadc)
{
    return TPC5120S16_HandleIsValid(hadc) &&
           (tpc5120s16_gpio_read(hadc->low_alarm_port, hadc->low_alarm_pin) == TPC5120S16_GPIO_HIGH);
}
