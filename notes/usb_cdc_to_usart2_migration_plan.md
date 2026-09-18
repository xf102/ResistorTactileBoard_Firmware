# USB CDC → USART2 迁移方案与实施记录

> 文档版本：1.0
> 最后更新：2026-08-04
> 适用范围：ResistorTactileBoard_Firmware（STM32F103C8T6）

---

## 1. 背景与目标

### 1.1 问题

当前固件使用 USB CDC 虚拟串口上传 32×32 压阻触觉帧数据（每帧 2048 字节）。
USB CDC 占用了过多的 Flash 和 RAM：

| 资源 | 用量 | 占芯片比例 |
|------|:----:|:----------:|
| Flash | ~13 KB | ~20%（64KB 总量）|
| RAM | ~2.5 KB | ~12.5%（20KB 总量） |

对于 STM32F103C8T6（64KB Flash / 20KB RAM），这个占比偏高，后续迭代空间受限。

### 1.2 方案

将数据接口从 USB CDC 改为 USART2：

| 项目 | 原方案 | 新方案 |
|------|--------|--------|
| 接口 | USB FS (PA11/PA12) | USART2 TX=PA2, CTS=PA0 |
| 速率 | 12 Mbps | 2 Mbps |
| 发送方式 | CDC_Transmit_FS_Wait (阻塞) | HAL_UART_Transmit_DMA + 信号量 |
| 流控 | 无 | 硬件 CTS |
| Flash 占用 | ~13 KB | ~1.5 KB |
| RAM 占用 | ~2.5 KB | ~0.5 KB |

### 1.3 保留不变

- USART1（PA9/PA10）：RTT console，921600 baud，DMA TX
- 全部数据采集逻辑（TIM2 + SPI1 DMA 全中断驱动 32×32 扫描）
- 全部 BSP 驱动（74HC595、ADS8681、CD74HC4067、Status LED）
- RT-Thread Nano 内核配置

---

## 2. 引脚分配（无重映射）

| 引脚 | 功能 | 模式 | 备注 |
|------|------|------|------|
| **PA2** | USART2_TX | AF 推挽输出 | ← 原空闲，无冲突 |
| **PA0** | USART2_CTS | 输入浮空 | ← 原空闲，无冲突 |

> USART2_RX 和 USART2_RTS 不使用，因本端口仅单向上传数据。

## 3. DMA 分配

| DMA 通道 | 用途 | 优先级 | 备注 |
|:--------:|------|:------:|------|
| DMA1_CH2 | SPI1_RX（ADC 采集） | HIGH | 不变 |
| DMA1_CH3 | SPI1_TX（软件配置） | -- | 不变（在 data_acquisition_app.c 中配置）|
| DMA1_CH4 | USART1_TX（console） | LOW | 不变 |
| **DMA1_CH7** | **USART2_TX（数据端口）** | **MEDIUM** | **新增** |

---

## 4. 文件变更清单

### 4.1 已完成

| # | 文件 | 操作 | 内容 |
|---|------|:----:|------|
| 1 | Core/Inc/usart.h | 修改 | 新增 huart2 声明、MX_USART2_UART_Init 原型 |
| 2 | Core/Src/usart.c | 修改 | MX_USART2_UART_Init（2 Mbps/CTS）、MSP init/deinit、IRQ handler |
| 3 | Core/Inc/dma.h | 修改 | 新增 hdma_usart2_tx extern |
| 4 | Core/Src/dma.c | 修改 | MX_DMA_Init 中新增 DMA1_Channel7 NVIC 配置 |
| 5 | Core/Inc/stm32f1xx_it.h | 修改 | 新增 USART2_IRQHandler、DMA1_Channel7 声明 |
| 6 | app/usart2_dataport_app.h | 新增 | 线程配置宏、USART2_DataPort_Init 声明 |
| 7 | app/usart2_dataport_app.c | 新增 | 线程实现（有语法错误待修复）|
| 8 | rt-thread-nano/board.c | 修改 | 加入 MX_USART2_UART_Init、移除 USB 相关 |
| B | Core/Src/stm32f1xx_it.c | 修改 | 新增 DMA1_CH7/USART2 ISR，删除 USB ISR |

| C | Core/Src/main.c | 修改 | 替换 USB_CDC_app.h + Init 为 USART2 版 |
| D | Core/Inc/stm32f1xx_hal_conf.h | 修改 | 注释 HAL_PCD_MODULE_ENABLED |
| A | app/usart2_dataport_app.c | 修复 | 3 处语法错误（已修复）|
| G | USB_DEVICE/ 目录 | 删除 | 整个 USB 设备应用层 |
|   | Middlewares/ST/STM32_USB_Device_Library/ | 删除 | 整个 USB 中间件 |

> E、F 不适用（非 cmake 编译），已跳过。

### 4.2 待完成

| # | 文件 | 操作 | 内容 |
|---|------|:----:|------|
> 4.2 所有项已完成，迁移全部结束。

---

## 5. 核心函数设计

### 5.1 MX_USART2_UART_Init()

`c
UART_HandleTypeDef huart2;

void MX_USART2_UART_Init(void)
{
    huart2.Instance = USART2;
    huart2.Init.BaudRate = 2000000;
    huart2.Init.WordLength = UART_WORDLENGTH_8B;
    huart2.Init.StopBits = UART_STOPBITS_1;
    huart2.Init.Parity = UART_PARITY_NONE;
    huart2.Init.Mode = UART_MODE_TX;
    huart2.Init.HwFlowCtl = UART_HWCONTROL_CTS;
    huart2.Init.OverSampling = UART_OVERSAMPLING_16;
    if (HAL_UART_Init(&huart2) != HAL_OK)
        Error_Handler();
}
`

### 5.2 USART2_DataPort_Init()

`c
void USART2_DataPort_Init(void)
{
    s_usart2_tx_sem = rt_sem_create("u2_tx", 0, RT_IPC_FLAG_FIFO);
    RT_ASSERT(s_usart2_tx_sem != RT_NULL);
    rt_thread_init(&s_usart2_data_thread_obj,
                   USART2_DATA_THREAD_NAME,
                   usart2_dataport_thread_entry,
                   RT_NULL,
                   s_usart2_data_stack,
                   USART2_DATA_THREAD_STACK_SIZE,
                   USART2_DATA_THREAD_PRIORITY, 10);
    rt_thread_startup(&s_usart2_data_thread_obj);
}
`

### 5.3 数据发送线程流程

`
while (1):
    rt_mb_recv(frame_mb, &frame, RT_WAITING_FOREVER)   # 等待采集帧
    HAL_UART_Transmit_DMA(&huart2, frame->data, len)    # DMA 发送
    rt_sem_take(s_usart2_tx_sem, RT_WAITING_FOREVER)    # 等待 DMA 完成
    frame_pool_put(frame)                                # 归还缓冲
`

### 5.4 DMA 完成回调

`c
void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART2)
        rt_sem_release(s_usart2_tx_sem);
}
`

---

## 6. 中断向量变更

| 操作 | 中断向量 | 说明 |
|:----:|----------|------|
| 新增 | USART2_IRQHandler | HAL_UART_IRQHandler(&huart2) |
| 新增 | DMA1_Channel7_IRQHandler | HAL_DMA_IRQHandler(&hdma_usart2_tx) |
| 删除 | USB_HP_CAN1_TX_IRQHandler | USB 不再使用 |
| 删除 | USB_LP_CAN1_RX0_IRQHandler | USB 不再使用 |

---

## 7. 数据流变化

### 原（USB CDC）
`
TIM2 ISR -> SPI1 DMA -> DMA ISR -> sem -> scan_thread
  -> frame_mb -> USB_CDC_thread -> CDC_Transmit_FS_Wait() -> USB
`

### 新（USART2）
`
TIM2 ISR -> SPI1 DMA -> DMA ISR -> sem -> scan_thread
  -> frame_mb -> USART2_dataport_thread -> HAL_UART_Transmit_DMA()
  -> DMA1_CH7 ISR -> sem -> frame_pool_put()
`

---

## 8. 启动链变化

### board.c (rt_hw_board_init)
`
HAL_Init()
SystemClock_Config()          # HSE 8MHz -> PLL x9 -> 72MHz
MX_GPIO_Init()
MX_DMA_Init()
MX_SPI1_Init()
MX_SPI2_Init()
MX_USART1_UART_Init()         # console
MX_TIM2_Init()                # stopped, configured later
MX_USART2_UART_Init()         # <-- 新增：数据端口
# MX_USB_DEVICE_Init()        # <-- 删除：USB CDC
`

### main()
`
bsp_init()                    # BSP 驱动
bsp_basic_service_init()      # UART console
bsp_frame_pool_init(32, 32)   # 帧缓冲池
params_init()                 # Flash 参数
DATA_ACQUISITION_Init()       # 数据采集
# USB_CDC_app_Init()          # <-- 删除
USART2_DataPort_Init()        # <-- 新增：USART2 数据端口
`

---

## 9. 已知问题（usart2_dataport_app.c）

文件中有 3 处语法错误：

**问题 1：L28 - 变量名错误**
`c
// 错误：
static rt_thread_t s.usart2_data_thread = RT_NULL;
// 正确：
static rt_thread_t s_usart2_data_thread = RT_NULL;
`

**问题 2：L84-85 - 宏名错误**
`c
// 错误：
ST_ASSERT(s_usart2_tx_sem != RT_NULL);
// 正确：
RT_ASSERT(s_usart2_tx_sem != RT_NULL);
`

**问题 3：L68-71 - DMA 调用排版和括号错误**
`c
// 错误：
if (HAL_UART_Transmit_DMA(&huart2, (uint8_t *)frame->data,
                                  frame->len*
sizeof(uint16_t)) != HAL_OK)) {
// 正确：
if (HAL_UART_Transmit_DMA(&huart2, (uint8_t *)frame->data,
                          frame->len * sizeof(uint16_t)) != HAL_OK) {
`

---

## 10. 硬件流控说明

- **CTS 引脚**：PA0，由主机（上位机）驱动
- **原理**：USART 硬件在发送每个字节前采样 CTS，无效时暂停
- **主机要求**：上位机须驱动 CTS 信号
- **注意事项**：若主机不驱动 CTS，PA0 需外部下拉或配置 GPIO_PULLDOWN

---

## 11. 测试要点

1. 物理连接验证：示波器检查 PA2(TX) 和 PA0(CTS) 波形
2. 波特率验证：2 Mbps 下单字节时长 = 10 bit / 2 Mbps = 5 us
3. DMA 传输验证：监控 DMA1_Channel7 传输完成中断
4. CTS 流控验证：拉低 PA0 确认发送暂停
5. 数据完整性：上位机接收 32x32 帧数据并校验
6. 联调：启动 DATA_ACQUISITION_Init() 验证完整链路
