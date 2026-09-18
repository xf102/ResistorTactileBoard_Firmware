/**
 * @file app_main.c
 * @brief Composition root and HAL callback routing for the bare-metal firmware.
 */

#include "app_main.h"

#include "app_events.h"
#include "bsp_init.h"
#include "console.h"
#include "dataport.h"
#include "frame_service.h"
#include "scan_service.h"
#include "spi.h"
#include "tim.h"
#include "usart.h"

/* ==========================================================================
 *  Change: 新增
 *  Editor: 谢峰
 *  Time: 2026-09-18
 *  Range: 定义USART1周期运行状态输出间隔及帧率统计基线
 * ========================================================================== */
#define APP_STATUS_PERIOD_MS  2000U

static uint32_t s_status_last_tick;
static uint32_t s_status_last_frames;

/* ==========================================================================
 *  Change: 新增
 *  Editor: 谢峰
 *  Time: 2026-09-18
 *  Range: 每2秒通过USART1打印矩阵帧率、扫描错误和数据口健康状态
 * ========================================================================== */
static void app_report_periodic_status(void)
{
    const uint32_t now = HAL_GetTick();
    const uint32_t elapsed = now - s_status_last_tick;
    scan_status_t scan;
    dataport_stats_t data;
    uint32_t total_frames;
    uint32_t fps;

    if (elapsed < APP_STATUS_PERIOD_MS) {
        return;
    }

    scan_get_status(&scan);
    dataport_get_stats(&data);
    total_frames = scan.stats.frames_acquired + scan.stats.frames_dropped;
    fps = ((total_frames - s_status_last_frames) * 1000U) / elapsed;

    (void)console_printf(
        "[ACQ] fps=%lu rows=%u cols=%u period=%uus acquired=%lu dropped=%lu "
        "spi_busy=%lu spi_err=%lu sync_err=%lu sent=%lu uart_err=%lu rx_err=%lu\r\n",
        (unsigned long)fps, scan.rows, scan.cols, scan.period_us,
        (unsigned long)scan.stats.frames_acquired,
        (unsigned long)scan.stats.frames_dropped,
        (unsigned long)scan.stats.spi_busy_errors,
        (unsigned long)scan.stats.spi_dma_errors,
        (unsigned long)scan.stats.frontend_sync_errors,
        (unsigned long)data.frames_transmitted,
        (unsigned long)(data.uart2_dma_errors + data.uart2_timeouts),
        (unsigned long)(console_get_rx_errors() +
                        console_get_rx_rearm_failures()));

    s_status_last_tick = now;
    s_status_last_frames = total_frames;
}

/* ==========================================================================
 *  Change: 新增
 *  Editor: 谢峰
 *  Time: 2026-09-18
 *  Range: 按BSP、缓冲、控制台、数据口、扫描服务顺序初始化裸机应用
 * ========================================================================== */
bool app_init(void)
{
    app_events_reset();
    bsp_init();
    frame_service_init();
    dataport_init();

    if (!console_init()) {
        return false;
    }
    if (!scan_init()) {
        (void)console_write("ERR ADC frontend initialization failed\r\n");
        return false;
    }

    (void)console_write("ResistorTactileBoard bare-metal ready; use help\r\n");
    if (!scan_start()) {
        (void)console_write("ERR automatic scan start failed\r\n");
        return false;
    }
    s_status_last_tick = HAL_GetTick();
    s_status_last_frames = 0U;
    return true;
}

/* ==========================================================================
 *  Change: 新增
 *  Editor: 谢峰
 *  Time: 2026-09-18
 *  Range: 超级循环依次处理命令、扫描恢复、帧打包和USART2发送
 * ========================================================================== */
void app_process(void)
{
    console_process();
    scan_process();
    dataport_process();
    app_report_periodic_status();
}

/* ==========================================================================
 *  Change: 新增
 *  Editor: 谢峰
 *  Time: 2026-09-18
 *  Range: 将TIM2周期完成回调路由到矩阵扫描状态机
 * ========================================================================== */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
    if (htim->Instance == TIM2) {
        scan_on_timer_irq();
    }
}

/* ==========================================================================
 *  Change: 新增
 *  Editor: 谢峰
 *  Time: 2026-09-18
 *  Range: 将SPI1 DMA完成和错误回调路由到扫描服务
 * ========================================================================== */
void HAL_SPI_TxRxCpltCallback(SPI_HandleTypeDef *hspi)
{
    if (hspi->Instance == SPI1) {
        scan_on_spi_complete_irq();
    }
}

void HAL_SPI_ErrorCallback(SPI_HandleTypeDef *hspi)
{
    if (hspi->Instance == SPI1) {
        scan_on_spi_error_irq();
    }
}

/* ==========================================================================
 *  Change: 新增
 *  Editor: 谢峰
 *  Time: 2026-09-18
 *  Range: 将USART1接收和USART2发送完成事件分别路由到独立服务
 * ========================================================================== */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART1) {
        console_on_rx_complete_irq();
    }
}

void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART2) {
        dataport_on_tx_complete_irq();
    }
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART1) {
        console_on_error_irq();
    } else if (huart->Instance == USART2) {
        dataport_on_error_irq();
    }
}
