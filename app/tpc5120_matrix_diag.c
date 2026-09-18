/* ==========================================================================
 *  Change: 新增
 *  Editor: 谢峰
 *  Time: 2026-09-16
 *  Range: 增加74HC595固定掩码与TPC5120八通道平均值循环打印诊断线程
 * ========================================================================== */
#include "tpc5120_matrix_diag.h"

#include "bsp_init.h"

#if defined(BSP_USE_TPC5120S16) && (BSP_TPC5120S16_MATRIX_DIAGNOSTIC == 1U)

#define TPC_MATRIX_DIAG_THREAD_NAME       "tpc_diag"
#define TPC_MATRIX_DIAG_THREAD_STACK_SIZE 2048U
#define TPC_MATRIX_DIAG_THREAD_PRIORITY   5U
#define TPC_MATRIX_DIAG_MASK_SETTLE_MS    500U
#define TPC_MATRIX_DIAG_DISCARD_FRAMES    TPC_MATRIX_DIAG_CHANNEL_COUNT
#define TPC_MATRIX_DIAG_MAX_TRANSFERS     \
    (TPC_MATRIX_DIAG_CHANNEL_COUNT * TPC_MATRIX_DIAG_SAMPLES_PER_CHANNEL * 2U)

static struct rt_thread s_tpc_diag_thread;
static rt_uint8_t s_tpc_diag_stack[TPC_MATRIX_DIAG_THREAD_STACK_SIZE];

/**
 * @brief 读取并丢弃一帧TPC5120数据。
 * @return true表示SPI传输成功，false表示传输失败。
 */
static bool tpc_matrix_diag_discard_frame(void)
{
    uint16_t frame;

    return TPC5120S16_TransferFrame(&g_bsp_tpc5120,
                                    TPC5120S16_CMD_CONTINUE,
                                    &frame) == TPC5120S16_STATUS_OK;
}

/**
 * @brief 在当前固定行掩码下采集CH0～CH7，并计算每个通道32次平均值。
 * @param averages 保存八个通道平均值的数组。
 * @return true表示所有通道均完成采样，false表示SPI或通道序列异常。
 */
static bool tpc_matrix_diag_collect(uint16_t averages[TPC_MATRIX_DIAG_CHANNEL_COUNT])
{
    uint32_t sums[TPC_MATRIX_DIAG_CHANNEL_COUNT] = {0U};
    uint16_t counts[TPC_MATRIX_DIAG_CHANNEL_COUNT] = {0U};
    uint16_t completed = 0U;
    uint16_t transfer_count = 0U;
    uint16_t frame;
    uint8_t channel;
    uint8_t index;

    for (index = 0U; index < TPC_MATRIX_DIAG_DISCARD_FRAMES; index++) {
        if (!tpc_matrix_diag_discard_frame()) {
            return false;
        }
    }

    while ((completed < TPC_MATRIX_DIAG_CHANNEL_COUNT) &&
           (transfer_count < TPC_MATRIX_DIAG_MAX_TRANSFERS)) {
        if (TPC5120S16_TransferFrame(&g_bsp_tpc5120,
                                     TPC5120S16_CMD_CONTINUE,
                                     &frame) != TPC5120S16_STATUS_OK) {
            return false;
        }

        transfer_count++;
        channel = TPC5120S16_ParseChannel(frame);
        if ((channel >= TPC_MATRIX_DIAG_CHANNEL_COUNT) ||
            (counts[channel] >= TPC_MATRIX_DIAG_SAMPLES_PER_CHANNEL)) {
            continue;
        }

        sums[channel] += TPC5120S16_ParseCode(frame);
        counts[channel]++;
        if (counts[channel] == TPC_MATRIX_DIAG_SAMPLES_PER_CHANNEL) {
            completed++;
        }
    }

    if (completed != TPC_MATRIX_DIAG_CHANNEL_COUNT) {
        return false;
    }

    for (index = 0U; index < TPC_MATRIX_DIAG_CHANNEL_COUNT; index++) {
        averages[index] = TPC_MATRIX_DIAG_AVERAGE(
            sums[index], TPC_MATRIX_DIAG_SAMPLES_PER_CHANNEL);
    }

    return true;
}

/**
 * @brief 诊断线程入口，循环测试全关、逐行单开和八行全开三类掩码。
 * @param parameter RT-Thread保留参数，当前未使用。
 */
static void tpc_matrix_diag_thread_entry(void *parameter)
{
    uint16_t averages[TPC_MATRIX_DIAG_CHANNEL_COUNT];
    uint32_t cycle = 0U;
    uint32_t mask;
    uint8_t mask_index;

    (void)parameter;

    LOG_I("[TPC-DIAG] Matrix row-isolation diagnostic started");
    LOG_I("[TPC-DIAG] Keep one sensor point pressed during a complete 5-second cycle");
    LOG_I("[TPC-DIAG] Normal acquisition and USART2 upload are disabled");

    while (1) {
        for (mask_index = 0U; mask_index < TPC_MATRIX_DIAG_MASK_COUNT; mask_index++) {
            mask = TPC_MATRIX_DIAG_MASK_AT(mask_index);
            if (HC595_Write32(mask) != HC595_OK) {
                LOG_E("[TPC-DIAG] HC595 write failed, MASK=0x%02X",
                      (unsigned int)mask);
                rt_thread_mdelay(1000U);
                continue;
            }

            rt_thread_mdelay(TPC_MATRIX_DIAG_MASK_SETTLE_MS);
            if (!tpc_matrix_diag_collect(averages)) {
                LOG_E("[TPC-DIAG] TPC5120 collect failed, MASK=0x%02X",
                      (unsigned int)mask);
                continue;
            }

            LOG_I("[TPC-DIAG] CYCLE=%lu MASK=0x%02X "
                  "CH0=%u CH1=%u CH2=%u CH3=%u CH4=%u CH5=%u CH6=%u CH7=%u",
                  (unsigned long)cycle,
                  (unsigned int)mask,
                  (unsigned int)averages[0],
                  (unsigned int)averages[1],
                  (unsigned int)averages[2],
                  (unsigned int)averages[3],
                  (unsigned int)averages[4],
                  (unsigned int)averages[5],
                  (unsigned int)averages[6],
                  (unsigned int)averages[7]);
        }

        cycle++;
    }
}

/**
 * @brief 初始化并启动TPC5120矩阵诊断线程。
 */
void TPC5120_MatrixDiag_Init(void)
{
    rt_err_t result;

    result = rt_thread_init(&s_tpc_diag_thread,
                            TPC_MATRIX_DIAG_THREAD_NAME,
                            tpc_matrix_diag_thread_entry,
                            RT_NULL,
                            s_tpc_diag_stack,
                            sizeof(s_tpc_diag_stack),
                            TPC_MATRIX_DIAG_THREAD_PRIORITY,
                            10U);
    RT_ASSERT(result == RT_EOK);
    if (result == RT_EOK) {
        rt_thread_startup(&s_tpc_diag_thread);
    }
}

#else

void TPC5120_MatrixDiag_Init(void)
{
}

#endif
