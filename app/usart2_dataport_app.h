/**
 * @file    usart2_dataport_app.h
 * @brief   USART2 data port application — replaces USB CDC for data upload.
 *
 * @details
 * Creates a thread that waits for frame buffers from the shared mailbox,
 * transmits them via USART2 using DMA (2 Mbps, CTS flow control),
 * then returns buffers to the pool.
 *
 * @author  Firmware Team
 * @version 1.0
 * @date    2026-08-03
 */

#ifndef USART2_DATAPORT_APP_H_
#define USART2_DATAPORT_APP_H_

#include <stdint.h>
#include "bsp_init.h"   /* bsp_config.h -> RTT + BSP drivers + BasicService (frame_buf_t, frame_mb) */

#ifdef __cplusplus
extern "C" {
#endif

/* Thread configuration */
#define USART2_DATA_THREAD_NAME       "uart2_data"
#define USART2_DATA_THREAD_STACK_SIZE 1024
#define USART2_DATA_THREAD_PRIORITY   10

/* Initialize and start the USART2 data port thread.
 * Must be called after bsp_frame_pool_init() and MX_USART2_UART_Init(). */
void USART2_DataPort_Init(void);

/**
 * @brief Transmit data via USART2 DMA directly (blocking).
 *
 * Sends data over USART2 using DMA and blocks until transmission completes.
 * This is a direct API for use by threads that need to send data without
 * going through the mailbox (e.g. data acquisition thread).
 *
 * @param data  Pointer to data buffer (must remain valid until callback)
 * @param len   Data length in bytes
 * @return 0 on success, -1 on failure
 */
int USART2_DataPort_Transmit_DMA(const uint8_t *data, uint16_t len);

#ifdef __cplusplus
}
#endif

#endif /* USART2_DATAPORT_APP_H_ */
