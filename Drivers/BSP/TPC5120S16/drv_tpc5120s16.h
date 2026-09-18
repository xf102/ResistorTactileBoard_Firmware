/* ==========================================================================
 *  Change: 新增
 *  Editor: 谢峰
 *  Time: 2026-09-17
 *  Range: 定义 TPC5120S16 协议、驱动句柄与公共接口
 * ========================================================================== */
#ifndef DRV_TPC5120S16_H_
#define DRV_TPC5120S16_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>
#include "drv_tpc5120s16_port_stm32.h"

#define TPC5120S16_CHANNEL_COUNT              16U
#define TPC5120S16_FRAME_BYTES                2U
#define TPC5120S16_INVALID_WORD               0xFFFFU

#define TPC5120S16_CMD_CONTINUE               0x0000U
#define TPC5120S16_CMD_MANUAL                 0x1000U
#define TPC5120S16_CMD_AUTO1                  0x2000U
#define TPC5120S16_CMD_AUTO2                  0x3000U
#define TPC5120S16_CMD_GPIO_PROGRAM           0x4000U
#define TPC5120S16_CMD_AUTO1_PROGRAM          0x8000U
#define TPC5120S16_CMD_AUTO2_PROGRAM          0x9000U

#define TPC5120S16_CONTROL_PROGRAM_ENABLE     0x0800U
#define TPC5120S16_CONTROL_COUNTER_RESET      0x0400U
#define TPC5120S16_CONTROL_RANGE_2            0x0040U
#define TPC5120S16_CONTROL_POWER_DOWN         0x0020U
#define TPC5120S16_CONTROL_GPIO_ENABLE        0x0010U
#define TPC5120S16_GPIO_ALARM_WORD            0x4060U

#define TPC5120S16_AUTO2_PROGRAM_CONST(channel) \
    (((uint16_t)(channel) < TPC5120S16_CHANNEL_COUNT) \
         ? (uint16_t)(TPC5120S16_CMD_AUTO2_PROGRAM | ((uint16_t)(channel) << 6U)) \
         : TPC5120S16_INVALID_WORD)

#define TPC5120S16_AUTO2_CONTROL_CONST(range, programming_enable, reset_counter) \
    ((uint16_t)(TPC5120S16_CMD_AUTO2 \
      | ((programming_enable) ? TPC5120S16_CONTROL_PROGRAM_ENABLE : 0U) \
      | ((reset_counter) ? TPC5120S16_CONTROL_COUNTER_RESET : 0U) \
      | (((range) == TPC5120S16_RANGE_0_TO_2VREF) ? TPC5120S16_CONTROL_RANGE_2 : 0U)))

#define TPC5120S16_FRAME_CHANNEL_CONST(frame) \
    ((uint8_t)(((uint16_t)(frame) >> 12U) & 0x0FU))
#define TPC5120S16_FRAME_CODE_CONST(frame) \
    ((uint16_t)((uint16_t)(frame) & 0x0FFFU))

typedef enum {
    TPC5120S16_RANGE_0_TO_VREF = 0,
    TPC5120S16_RANGE_0_TO_2VREF = 1
} tpc5120s16_range_t;

typedef enum {
    TPC5120S16_STATUS_OK = 0,
    TPC5120S16_STATUS_INVALID_ARG,
    TPC5120S16_STATUS_BUSY,
    TPC5120S16_STATUS_HAL_ERROR,
    TPC5120S16_STATUS_SYNC_ERROR
} tpc5120s16_status_t;

typedef struct {
    tpc5120s16_spi_t *hspi;
    tpc5120s16_gpio_port_t *cs_port;
    tpc5120s16_gpio_pin_t cs_pin;
    tpc5120s16_gpio_port_t *alarm_port;
    tpc5120s16_gpio_pin_t alarm_pin;
    tpc5120s16_gpio_port_t *low_alarm_port;
    tpc5120s16_gpio_pin_t low_alarm_pin;
    uint8_t tx_buf[TPC5120S16_FRAME_BYTES];
    volatile uint8_t rx_buf[TPC5120S16_FRAME_BYTES];
    volatile uint8_t dma_busy;
    uint8_t last_channel;
    tpc5120s16_range_t range;
} TPC5120S16_HandleTypeDef;

uint16_t TPC5120S16_BuildManualControl(uint8_t channel,
                                       tpc5120s16_range_t range,
                                       bool power_down);
uint16_t TPC5120S16_BuildAuto1Control(tpc5120s16_range_t range,
                                      bool programming_enable,
                                      bool reset_counter,
                                      bool power_down);
uint16_t TPC5120S16_BuildAuto2Program(uint8_t last_channel);
uint16_t TPC5120S16_BuildAuto2Control(tpc5120s16_range_t range,
                                      bool programming_enable,
                                      bool reset_counter,
                                      bool power_down);
uint16_t TPC5120S16_BuildGpioProgram(bool gpio3_power_down,
                                     bool gpio2_range,
                                     uint8_t gpio10_function,
                                     uint8_t gpio_direction_mask);
uint8_t TPC5120S16_ParseChannel(uint16_t frame);
uint16_t TPC5120S16_ParseCode(uint16_t frame);
tpc5120s16_status_t TPC5120S16_InitHandle(TPC5120S16_HandleTypeDef *hadc);
tpc5120s16_status_t TPC5120S16_TransferFrame(TPC5120S16_HandleTypeDef *hadc,
                                             uint16_t tx_word,
                                             uint16_t *rx_word);
tpc5120s16_status_t TPC5120S16_TransferFrameDMA(TPC5120S16_HandleTypeDef *hadc,
                                                uint16_t tx_word);
tpc5120s16_status_t TPC5120S16_DMAComplete(TPC5120S16_HandleTypeDef *hadc,
                                           uint16_t *rx_word);
void TPC5120S16_DMAAbort(TPC5120S16_HandleTypeDef *hadc);
tpc5120s16_status_t TPC5120S16_ConfigureAuto1(TPC5120S16_HandleTypeDef *hadc,
                                              uint16_t channel_mask,
                                              tpc5120s16_range_t range);
tpc5120s16_status_t TPC5120S16_ConfigureAuto2(TPC5120S16_HandleTypeDef *hadc,
                                              uint8_t last_channel,
                                              tpc5120s16_range_t range);
tpc5120s16_status_t TPC5120S16_SetPowerDown(TPC5120S16_HandleTypeDef *hadc,
                                            bool power_down,
                                            tpc5120s16_range_t range);
tpc5120s16_status_t TPC5120S16_SynchronizeToChannel0(TPC5120S16_HandleTypeDef *hadc,
                                                     uint8_t max_attempts);
bool TPC5120S16_IsAlarmActive(const TPC5120S16_HandleTypeDef *hadc);
bool TPC5120S16_IsLowAlarmActive(const TPC5120S16_HandleTypeDef *hadc);

#ifdef __cplusplus
}
#endif

#endif /* DRV_TPC5120S16_H_ */
