/*
 * @file  usart2_dataport_app.c
 * @brief  USART2 data port — DMA-based frame upload with CTS flow control.
 *
 * @details
 * Replaces USB CDC for data transmission. Uses USART2 at 2 Mbps with
 * hardware CTS flow control. DMA1_Channel7 handles TX. A semaphore
 * synchronizes DMA completion with the thread.
 *
 * Data flow:
 *   mailbox (from data_acq) -> USART2 Dataport Thread
 *     -> HAL_UART_Transmit_DMA() -> DMA1_CH7 ISR
 *     -> HAL_UART_TxCpltCallback() -> rt_sem_release()
 *     -> frame_pool_put()
 *
 * @author  Firmware Team
 * @version 1.0
 * @date    2026-08-03
 */

#include "usart2_dataport_app.h"
#include "usart.h"       /* huart2 */
#include "dma.h"         /* hdma_usart2_tx */
#include "debug_log.h"

/* Thread control block and stack */
 static rt_thread_t s_usart2_data_thread = RT_NULL;
static struct rt_thread s_usart2_data_thread_obj;
static rt_uint8_t s_usart2_data_stack[USART2_DATA_THREAD_STACK_SIZE];

/* DMA TX completion semaphore */
static rt_sem_t s_usart2_tx_sem = RT_NULL;

/**
 * @brief HAL UART TX Complete callback (override weak default).
 *
 * Called by HAL from DMA ISR context when USART2 DMA TX finishes.
 * Releases the semaphore to wake the data port thread.
 *
 * @note  USART1 (console) does NOT use DMA TX, so this only fires for USART2.
 */
void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART2) {
        rt_sem_release(s_usart2_tx_sem);
    }
}

/**
 * @brief USART2 data port thread entry.
 *
 * Waits for a frame buffer from the shared mailbox (filled by data_acq thread),
 * transmits it over USART2 via DMA, waits for DMA completion, then returns
 * the buffer to the pool.
 */
static void usart2_dataport_thread_entry(void *param)
{
    /* ==========================================================================
     *  Change: 修改
     *  Editor: Thompson
     *  Time: 2026-08-14
     *  Range: [定型] 通讯链路测试完成（CTS 已关闭），恢复原 DMA 帧发送逻辑，
     *         移除临时测试字符串循环
     * ========================================================================== */
    frame_buf_t *frame;
    rt_err_t     ret;
    
    (void)param;

    while (1) {
        /* Wait for a complete frame from the acquisition thread */
        ret = rt_mb_recv(frame_mb, (rt_ubase_t *)&frame, RT_WAITING_FOREVER);//等待数据采集线程发送的帧
        if (ret != RT_EOK) {
            continue;//等待数据采集线程发送的帧
        }

        /* Transmit packed frame data via USART2 DMA (frame->len = packed byte count) */
         if (HAL_UART_Transmit_DMA(&huart2, (uint8_t *)frame->data,
                                   frame->len) != HAL_OK) {
            LOG_E("[UART2] DMA transmit failed, dropping frame.");
            frame_pool_put(frame);
            continue;
        }

        /* Wait for DMA TX completion (released in HAL_UART_TxCpltCallback) */
        rt_sem_take(s_usart2_tx_sem, RT_WAITING_FOREVER);

        /* Return the buffer to the pre-allocated pool */
        frame_pool_put(frame);
    }
}

/**
 * @brief Initialize and start the USART2 data port thread.
 *
 * Creates the DMA-completion semaphore and spawns the thread.
 * Must be called after bsp_frame_pool_init() and MX_USART2_UART_Init().
 */
void USART2_DataPort_Init(void)
{
    rt_err_t ret;

    /* Create DMA TX completion semaphore (binary, initial 0) */
    s_usart2_tx_sem = rt_sem_create("u2_tx", 0, RT_IPC_FLAG_FIFO);//创建DMA TX完成信号量
     RT_ASSERT(s_usart2_tx_sem != RT_NULL);
    if (s_usart2_tx_sem == RT_NULL) {
        LOG_E("[UART2] Failed to create TX semaphore.");
        return;
    }

    /* Initialize and start the data port thread */
    s_usart2_data_thread = &s_usart2_data_thread_obj;//初始化数据端口线程
    ret = rt_thread_init(&s_usart2_data_thread_obj,//初始化数据端口线程
                         USART2_DATA_THREAD_NAME,
                         usart2_dataport_thread_entry,
                         RT_NULL,
                         s_usart2_data_stack,
                         USART2_DATA_THREAD_STACK_SIZE,
                         USART2_DATA_THREAD_PRIORITY,
                        10);
    if (ret != RT_EOK) {
        LOG_E("[UART2] Thread init failed: %d", ret);
        return;
    }

    rt_thread_startup(s_usart2_data_thread);
    LOG_I("[UART2] Data port thread started.");
}

/**
 * @brief Transmit data via USART2 DMA directly (blocking).
 *
 * Direct API for sending data over USART2 via DMA. Blocks until
 * the DMA transfer completes (signaled by HAL_UART_TxCpltCallback).
 * Suitable for use by the data acquisition thread.
 *
 * @param data  Pointer to data buffer
 * @param len   Data length in bytes
 * @return 0 on success, -1 on failure
 */
int USART2_DataPort_Transmit_DMA(const uint8_t *data, uint16_t len)
{
    /* <! USB→USART2 migration 2026-08-06: direct DMA transmit API */
    if (HAL_UART_Transmit_DMA(&huart2, (uint8_t *)data, len) != HAL_OK) {
        LOG_E("[UART2] Direct DMA transmit failed.");
        return -1;
    }
    /* Block until DMA transfer completes (callback releases semaphore) */
    rt_sem_take(s_usart2_tx_sem, RT_WAITING_FOREVER);
    return 0;
}
