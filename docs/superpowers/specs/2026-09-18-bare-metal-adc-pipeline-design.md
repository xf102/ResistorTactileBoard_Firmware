# ResistorTactileBoard 裸机 ADC 数据链路设计

## 1. 目标与边界

本设计在根工程 `ResistorTactileBoard_Firmware` 中实现纯裸机数据链路：

```text
ADS8681 + CD74HC4067 采集
    -> MCU 帧处理与协议打包
    -> USART2 DMA 二进制输出

USART1 中断接收
    -> 文本命令解析
    -> 控制采集并输出状态、命令结果和调试日志
```

`ResistorTactileBoard-1` 仅作为只读行为参考。根工程不得通过源文件、头文件、库、构建脚本、符号或相对路径依赖该目录，也不得在该目录中产生修改。

第一阶段使用已经完成硬件验证的 ADS8681 + CD74HC4067 前端。软件同时建立精简、稳定的 ADC 前端边界，使 TPC5120S16 等前端以后可以通过编译期配置接入，但第一阶段不实现或联调其他前端。

## 2. 设计原则

1. 采用裸机事件驱动超级循环，不引入 RT-Thread，也不实现线程、信号量或邮箱兼容层。
2. 中断仅执行硬实时、短路径工作；帧打包、命令解析、日志输出和错误恢复放在主循环。
3. 运行期不使用动态内存；帧、命令行和串口缓冲区均静态分配。
4. USART2 仅传输二进制采集帧；USART1 仅传输文本命令、响应和日志。
5. ADC 型号差异收敛在前端适配层，不传播到帧管理、协议和串口模块。
6. 所有服务函数均非阻塞。运行过程中禁止无限等待式 HAL 调用和 `HAL_Delay()`。
7. 保持现有上位机数据帧格式、字节序、帧计数和 CRC32 计算不变。
8. 代码按职责拆分，避免在 `main.c` 或单个应用文件中堆积全部逻辑。

## 3. 总体架构

```text
                           +----------------------+
                           |        main()        |
                           | init -> app_process  |
                           +----------+-----------+
                                      |
                  +-------------------+-------------------+
                  |                   |                   |
                  v                   v                   v
          +---------------+   +---------------+   +---------------+
          | scan_service  |   | frame_service |   | console       |
          | 矩阵采集状态机 |   | 双缓冲/所有权  |   | USART1命令    |
          +-------+-------+   +-------+-------+   +-------+-------+
                  |                   |                   |
                  v                   v                   v
          +---------------+   +---------------+   +---------------+
          | adc_frontend  |   | frame_protocol|   | parameter /   |
          | 公共前端边界   |   | 原格式打包     |   | diagnostics   |
          +-------+-------+   +-------+-------+   +---------------+
                  |                   |
                  v                   v
          +---------------+   +---------------+
          | ADS8681+MUX   |   | USART2 DMA    |
          | 第一阶段实现   |   | 二进制数据口   |
          +---------------+   +---------------+
```

## 4. 模块职责

### 4.1 `main.c`

`main.c` 只负责 CubeMX 外设初始化、应用初始化、超级循环和可选低功耗等待。它不包含采样状态机、协议打包或命令实现。

推荐初始化顺序：

1. HAL、系统时钟和 CubeMX 外设初始化。
2. BSP 初始化：74HC595、CD74HC4067、ADS8681。
3. USART1 console 初始化并启动 RX 中断。
4. 静态帧池、USART2 dataport 和扫描服务初始化。
5. 加载默认或持久化参数。
6. 检查前端状态后启动扫描。

超级循环依次调用：

```text
console_process()
scan_process()
frame_process()
dataport_process()
fault_process()
```

所有服务无待处理工作时，可执行 `__WFI()`；只有在确认没有会因睡眠而错过的软件工作后才能启用。

### 4.2 `adc_frontend`

前端层向采集服务提供与芯片型号无关的语义接口。第一阶段接口只覆盖实际需要，避免提前构造复杂框架：

```c
bool adc_frontend_init(void);
bool adc_frontend_prepare_row(uint16_t row);
bool adc_frontend_start_step(uint16_t step);
adc_frontend_result_t adc_frontend_on_dma_complete(void);
void adc_frontend_abort(void);
bool adc_frontend_is_ready(void);
```

`adc_frontend_result_t` 至少表达：

- 本次结果是否包含有效样本；
- 样本列号和值；
- 当前行是否完成；
- 是否发生同步、SPI 或前端故障。

ADS8681 适配实现负责：

- CD74HC4067 通道选择；
- ADS8681 一拍流水延迟；
- 每行 `cols + 1` 次 SPI 传输；
- 首次无效结果丢弃和末次结果冲刷；
- SPI1 DMA 启动、结果解析和错误复位。

以后接入其他 ADC 时，通过 `BSP_ADC_FRONTEND` 在 `bsp_config.h` 中选择实现。条件编译集中在配置层和适配层，扫描业务不出现芯片型号判断。

### 4.3 `scan_service`

扫描服务管理行、列、帧和前端步骤，不负责协议打包或串口发送。

建议状态：

```text
STOPPED -> STARTING -> RUNNING -> STOPPING
                 \-> FAULT
```

TIM2 周期中断：

1. 确认扫描处于 `RUNNING`。
2. 确认 SPI1 DMA 没有未完成事务。
3. 调用 `adc_frontend_start_step()`。
4. 若启动失败，仅记录错误事件，不在 ISR 中打印。

SPI1 DMA完成中断：

1. 调用前端完成接口解析结果。
2. 将有效样本写入当前 `ACQUIRING` 帧。
3. 推进行、列和前端流水步骤。
4. 整帧完成后，将缓冲区变为 `RAW_READY` 并设置事件。
5. 为下一帧取得空闲缓冲；无空闲缓冲则进入整帧丢弃模式。

运行中修改行列、周期或量程时，采用以下原子重配置流程：

```text
停止 TIM2
-> 等待当前 SPI DMA 完成或安全终止
-> 丢弃未完成帧
-> 应用并校验新参数
-> 重新初始化前端流水状态
-> 从完整新帧边界恢复扫描
```

### 4.4 `frame_service`

帧服务维护两个静态缓冲及其所有权：

```text
FREE -> ACQUIRING -> RAW_READY -> PACKED_READY
                                      |
                                      v
                              TRANSMITTING -> FREE
```

只有状态所有者可以访问缓冲区：

- ISR 只写 `ACQUIRING` 缓冲；
- 主循环只打包 `RAW_READY` 缓冲；
- USART2 DMA 只读 `TRANSMITTING` 缓冲。

状态切换在短临界区内完成，临界区中不执行拷贝、CRC、HAL 发送或日志输出。

若一帧结束时没有 `FREE` 缓冲，下一整帧进入 `DISCARDING`。扫描硬件仍按正常时序运行，但不保存样本；同时增加 `frames_dropped`。禁止覆盖 `RAW_READY`、`PACKED_READY` 或 `TRANSMITTING` 缓冲。

缓冲大小由编译期最大矩阵尺寸计算：

```text
FRAME_BUFFER_BYTES = FRAME_OVERHEAD
                   + BSP_MATRIX_ROWS * BSP_MATRIX_COLS * sizeof(uint16_t)
```

不得默认按 32x32 分配，除非当前产品配置确实要求 32x32。

### 4.5 `frame_protocol`

复用根工程中已经存在的产品协议算法，但消除它对 RTOS 的任何间接包含。打包在主循环中原地完成，保持以下格式：

```text
0xFF 0x66                 2 bytes
frame counter             2 bytes
rows * cols ADC samples   2 bytes each
CRC32                     4 bytes
```

帧固定开销为 8 字节。帧计数器、采样字节序和 CRC 覆盖范围必须通过参考向量测试确认与原工程完全一致。

第一阶段 MCU 数据处理为无损处理：保持原始 ADC 样本顺序和值，只完成帧计数、字节序转换和 CRC32 打包。后续算法应放在独立处理模块，不修改采集 ISR 和传输驱动。

### 4.6 `dataport`（USART2）

USART2 只发送已打包的二进制帧。主循环在 USART2 空闲且存在 `PACKED_READY` 帧时调用 `HAL_UART_Transmit_DMA()`。

`HAL_UART_TxCpltCallback()` 只设置完成事件。主循环消费事件后释放对应帧。错误回调同样只记录错误和事件，由主循环执行 DMA 停止、USART2 恢复和缓冲释放。

必须提供发送超时监测，防止丢失 DMA 完成中断后永久占有缓冲区。

### 4.7 `console`（USART1）

USART1 是双向文本控制台：

- RX：中断接收字符，写入静态环形缓冲；
- 解析：主循环按换行符取出完整命令；
- TX：输出命令结果、状态和调试日志。

ISR 不解析命令。命令行长度、参数个数和数值范围均有固定上限；缓冲区溢出时丢弃当前行并返回明确错误。

第一阶段命令：

```text
help
status
scan_start
scan_stop
scan_rows <count>
scan_cols <count>
scan_fps <fps>
scan_period_us <period>
adc_read
adc_reset
spi_status
stats_show
stats_clear
```

每条命令附带简短帮助文字和参数范围。修改扫描配置的命令统一调用扫描服务的安全重配置接口，不能直接修改 ISR 正在访问的变量。

USART1 输出应避免长时间阻塞。第一阶段可使用短消息阻塞发送，但必须设置有限超时；稳定阶段改为 TX 环形缓冲或 DMA 队列。

## 5. 中断与并发规则

1. TIM2、SPI1 DMA、USART2 DMA和USART1 RX之间不共享无所有权约束的可写缓冲区。
2. ISR 与主循环共享的标志和简单计数使用 `volatile`，多字段状态切换使用短临界区。
3. `volatile` 不代替原子性；超过 MCU 原生访问宽度的数据不能无保护地跨上下文读写。
4. ISR 中禁止 `printf`、CRC计算、Flash写入、命令解析和阻塞等待。
5. 回调必须先判断外设实例，不能把 USART1、USART2或其他 SPI 的回调混为同一事件。
6. SPI1 专用于 ADC 前端；SPI2 专用于74HC595。两者分别维护忙状态和错误状态。

## 6. 错误处理与统计

系统至少维护：

```text
frames_acquired
frames_transmitted
frames_dropped
spi_busy_errors
spi_dma_errors
uart2_dma_errors
uart2_timeouts
uart1_rx_overflows
frontend_sync_errors
```

错误分为：

- 可恢复错误：单次 SPI/DMA启动失败、USART2超时、USART1行溢出；记录后由主循环复位相关外设或丢弃当前帧。
- 前端失步：停止扫描、复位 ADS8681 流水线并从新帧边界恢复。
- 不可恢复初始化错误：保持扫描停止，通过 USART1 返回故障原因，禁止发送无效采集数据。

任何错误恢复都不得把半帧作为完整帧发送。

## 7. 配置和依赖约束

`Drivers/BSP/bsp_config.h` 是 BSP 编译配置的唯一入口。模块开关使用 `BSP_USE_*`；前端类型使用单一 `BSP_ADC_FRONTEND` 选择值。

根工程应满足：

- 不包含 `<rtthread.h>`；
- 不调用 `rt_*` API；
- CMake、IDE配置和编译数据库不包含 `ResistorTactileBoard-1` 路径；
- 应用层不直接包含具体 ADC 驱动头文件；
- 关闭某一可选模块后，其源码和符号不会进入构建。

## 8. 注释和代码组织规范

所有新增或修改的 C 代码必须具备有意义的注释，但避免逐行复述代码。

必须注释：

- 模块职责和依赖；
- 公共 API 的参数、返回值和调用上下文；
- 状态机状态及转换条件；
- ADS8681 一拍流水、冲刷传输和通道映射；
- ISR 与主循环之间的所有权和并发约束；
- 硬件时序、超时值和非直观常量的来源；
- 错误恢复会丢弃哪些数据以及从何处重新同步。

命名使用明确的模块前缀，例如 `scan_`、`frame_`、`console_`、`dataport_` 和 `adc_frontend_`。私有符号使用 `static`，公共头文件只暴露调用方真正需要的接口。

## 9. 分阶段实施

### 阶段 A：工程与依赖清理

1. 检查根工程构建文件和包含链。
2. 保证根工程独立构建且不引用参考目录。
3. 将根工程中需要保留的驱动与协议模块解除 RTOS 依赖。

### 阶段 B：ADS8681 裸机采集

1. 初始化74HC595、CD74HC4067和ADS8681。
2. 验证单次阻塞寄存器读写和原始采样。
3. 建立 TIM2 + SPI1 DMA 非阻塞扫描。
4. 验证行列顺序和 ADS8681 流水线冲刷。

### 阶段 C：帧处理与 USART2

1. 建立静态双缓冲和所有权状态。
2. 接入原格式原地打包。
3. 接入 USART2 DMA 状态机、完成事件和超时恢复。
4. 使用现有上位机验证帧头、长度、计数器和 CRC32。

### 阶段 D：USART1 命令控制台

1. 建立 RX 环形缓冲和行解析。
2. 实现命令注册表及第一阶段命令。
3. 接入安全停止、重配置和恢复流程。
4. 验证连续命令输入不影响采样时序。

### 阶段 E：稳健性和后续扩展

1. 加入统计、错误注入和长时间压力测试。
2. 恢复参数持久化、产品信息、LED和看门狗等非核心功能。
3. 在不改变上层接口的前提下增加 TPC5120S16 前端适配。

## 10. 验收标准

第一阶段完成需同时满足：

1. 根工程可独立完整编译，构建输出不出现 RT-Thread 或参考目录路径。
2. ADS8681 + CD74HC4067 可连续采集，矩阵行列顺序正确。
3. 连续至少1000帧无 SPI 状态机失步和缓冲区所有权错误。
4. USART2输出与原协议逐字节兼容，上位机无需修改即可解析。
5. 人为阻塞 USART2 时采样时序继续运行，只增加整帧丢弃计数。
6. USART1可同时接收命令并输出明确响应，不污染 USART2 数据流。
7. 运行中修改扫描参数不会发送半帧，也不会在 DMA 期间破坏共享状态。
8. SPI和USART2故障能够超时退出并在完整帧边界恢复。
9. 静态 RAM、Flash、栈余量和最坏中断执行时间有可检查的构建或测量结果。

