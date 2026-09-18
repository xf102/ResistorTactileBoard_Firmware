# Bare-Metal ADC Data Pipeline Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 在根工程中实现 ADS8681 + CD74HC4067 裸机矩阵采集、MCU 原协议打包、USART2 DMA 输出和 USART1 双向命令控制台。

**Architecture:** 使用中断驱动采样和事件驱动超级循环；ISR 只推进 SPI/扫描状态并发布事件，主循环负责帧打包、串口发送、命令解析和故障恢复。帧数据由两个静态缓冲区承载，具体 ADC 差异被限制在 `adc_frontend` 适配层。

**Tech Stack:** C11、STM32F103xB、STM32 HAL、CMake/Ninja、ARM GCC、Python 3 标准库测试。

**Spec:** `docs/superpowers/specs/2026-09-18-bare-metal-adc-pipeline-design.md`

## Global Constraints

- `ResistorTactileBoard-1` 仅可读取，不得修改，也不得加入源码、头文件、库、构建路径或生成文件依赖。
- 这是纯本地工程，不执行 Git、分支、提交或 PR 操作。
- 开始修改 C 代码前，必须按 `code-edit-tracking` 规范询问用户使用的编辑器名称。
- 根工程不得包含 `<rtthread.h>`，不得调用任何 `rt_*` API。
- 运行期禁止动态内存；帧、命令和串口缓冲区全部静态分配。
- ISR 中禁止日志、CRC、Flash写入、命令解析和阻塞等待。
- USART2 仅输出二进制采集帧；USART1 仅承载文本命令、响应和日志。
- 保持帧头 `FF 66`、16位帧计数、样本字节序和 CRC32 覆盖范围与现有协议一致。
- 所有新增或修改的 C 代码必须注释模块职责、公共 API、状态转换、硬件时序和并发所有权；避免无意义逐行注释。
- 每个任务完成后建立一个本地备份点：复制本任务修改清单和验证输出到 `build/checkpoints/task-N/`，不复制整个工程。

---

## File Structure

### 新建文件

- `app/app_main.h/.c`：应用初始化、超级循环服务编排和 HAL 回调分发。
- `app/app_events.h/.c`：ISR 与主循环之间的事件位管理。
- `app/frame_protocol.h/.c`：与硬件无关的原数据协议打包和 CRC32。
- `app/frame_service.h/.c`：两个静态帧缓冲及所有权状态机。
- `app/adc_frontend.h`：通用 ADC 前端接口和结果类型。
- `app/adc_frontend_ads8681.c`：ADS8681 + CD74HC4067 前端适配。
- `app/scan_service.h/.c`：矩阵扫描状态机、行列推进和安全重配置。
- `app/dataport.h/.c`：USART2 DMA 非阻塞发送状态机。
- `app/console.h/.c`：USART1 RX 环形缓冲、行解析和文本输出。
- `app/command.h/.c`：命令注册表、参数校验和命令处理函数。
- `tests/test_source_boundaries.py`：引用边界、RTOS残留和构建路径静态检查。
- `tests/test_frame_protocol.py`：协议参考向量测试。
- `tests/test_frame_state_model.py`：双缓冲状态模型测试。
- `tests/test_command_contract.py`：命令名称、帮助和参数边界检查。

### 修改文件

- `CMakeLists.txt`：加入根工程应用和 BSP 源文件、包含路径及检查目标。
- `Drivers/BSP/bsp_config.h`：删除 RTOS入口，集中定义前端、矩阵和模块配置。
- `Drivers/BSP/bsp_init.h/.c`：只初始化裸机核心 BSP。
- `Drivers/BSP/debug_log.h`：日志输出切换到 USART1 console，无 RTOS依赖。
- `Drivers/BSP/ADS8681/drv_ads8681.h/.c`：保留芯片驱动，整理 DMA/状态返回接口。
- `Drivers/BSP/BasicService/bsp_basicService.h/.c`：第一阶段从构建中移除；后续只迁移参数存储，不保留 RTOS帧池和 console。
- `Core/Src/main.c`：调用 `app_init()` 和 `app_process()`。
- `Core/Src/stm32f1xx_it.c`、`Core/Inc/stm32f1xx_it.h`：补齐 USART1、USART2、DMA1 Channel7 中断入口。
- `Core/Src/usart.c`：启用 USART1 RX 中断和 USART2 TX DMA所需 NVIC/DMA配置。
- `Core/Src/dma.c`：明确 SPI1 RX/TX 与 USART2 TX DMA中断优先级。

---

### Task 1: 建立独立裸机构建边界

**Files:**
- Create: `tests/test_source_boundaries.py`
- Modify: `CMakeLists.txt`
- Modify: `Drivers/BSP/bsp_config.h`
- Modify: `Drivers/BSP/debug_log.h`

**Interfaces:**
- Consumes: CubeMX生成的 `stm32cubemx` CMake目标。
- Produces: 不含参考目录和 RT-Thread 的根工程构建；集中配置宏 `BSP_ADC_FRONTEND`、`BSP_MATRIX_ROWS`、`BSP_MATRIX_COLS`。

- [ ] **Step 1: 询问用户编辑器名称并记录修改注释署名**

在任何 C/H 文件编辑前，只询问一次：“本次代码修改注释中的 Editor 名称使用什么？”得到答案后用于所有标准修改注释块。

- [ ] **Step 2: 写边界检查测试**

创建脚本，扫描根工程构建文件和非参考源码：

```python
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
REFERENCE = ROOT / "ResistorTactileBoard-1"

class SourceBoundaryTests(unittest.TestCase):
    def test_build_files_do_not_reference_reference_project(self):
        for rel in ("CMakeLists.txt", "cmake/stm32cubemx/CMakeLists.txt"):
            text = (ROOT / rel).read_text(encoding="utf-8")
            self.assertNotIn("ResistorTactileBoard-1", text)

    def test_firmware_target_has_no_rtthread_dependency(self):
        cmake_text = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
        forbidden = ("rt-thread-nano", "<rtthread.h>", '"rtthread.h"')
        for token in forbidden:
            self.assertNotIn(token, cmake_text)

        compile_db = ROOT / "build" / "Debug" / "compile_commands.json"
        if compile_db.exists():
            self.assertNotIn("rt-thread-nano",
                             compile_db.read_text(encoding="utf-8"))

if __name__ == "__main__":
    unittest.main()
```

- [ ] **Step 3: 运行测试并确认现有 RTOS 依赖导致失败**

Run: `python -m unittest tests.test_source_boundaries -v`

Expected: 边界检查或随后的固件链接失败，暴露当前构建尚未登记裸机应用模块；不得通过加入参考工程或 RT-Thread 路径修复。

- [ ] **Step 4: 收敛第一阶段编译配置**

在 `bsp_config.h` 中保留/定义：

```c
#define BSP_ADC_FRONTEND_ADS8681_MUX  1U
#define BSP_ADC_FRONTEND_TPC5120S16   2U
#define BSP_ADC_FRONTEND              BSP_ADC_FRONTEND_ADS8681_MUX

#define BSP_MATRIX_ROWS               8U
#define BSP_MATRIX_COLS               8U
#define BSP_FRAME_BUFFER_COUNT        2U

#define BSP_USE_74HC595
#define BSP_USE_ADS8681
#define BSP_USE_CD74HC4067_MUX
#define BSP_USE_USART1_CONSOLE
#define BSP_USE_USART2_DATAPORT
```

删除 `BSP_USE_RTTHREAD` 和 RT-Thread头文件。第一阶段不编译旧 `data_acquisition_app.c`、`usart2_dataport_app.c`、`data_acquisition_debug.c`、`tpc5120_matrix_diag.c`、`bsp_basicService.c` 和 RTOS LED逻辑。

- [ ] **Step 5: 在根 CMake 中只登记第一阶段所需文件**

加入明确的源文件和 include目录，不使用递归 glob，不出现参考工程路径。此时仅加入可独立编译的 BSP 驱动；后续任务创建的新模块随任务逐项加入。

- [ ] **Step 6: 运行静态检查和交叉编译**

Run: `python -m unittest tests.test_source_boundaries -v`

Expected: PASS。

Run: `cmake --preset Debug --fresh`

Run: `cmake --build --preset Debug --clean-first`

Expected: 生成 `.elf` 和 `.map`，无 `rtthread`、`ResistorTactileBoard-1` 或未解析 `rt_*` 符号。

- [ ] **Step 7: 保存本地检查点**

将任务修改文件列表、测试输出和构建输出摘要保存到 `build/checkpoints/task-1/README.txt`。

---

### Task 2: 提取并锁定数据帧协议

**Files:**
- Create: `app/frame_protocol.h`
- Create: `app/frame_protocol.c`
- Create: `tests/test_frame_protocol.py`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `uint16_t` ADC样本、行数、列数和16位帧计数器。
- Produces: `uint32_t frame_protocol_crc32(const uint8_t *, uint32_t)`；`bool frame_protocol_pack_inplace(uint16_t *, uint16_t, uint16_t, uint16_t, uint16_t *)`。

- [ ] **Step 1: 写固定参考向量测试**

测试至少覆盖空维度拒绝、2×2固定样本、最大配置长度和计数器回绕。2×2测试向量固定为：

```python
samples = [0x1234, 0xABCD, 0x0001, 0xFFFF]
expected_prefix = bytes.fromhex("FF 66 00 2A 12 34 AB CD 00 01 FF FF")
expected_length = 2 + 2 + len(samples) * 2 + 4
```

CRC期望值由现有根工程 `product_calc_crc32()` 算法独立翻译成 Python参考实现生成，不从待测 C 函数读取结果。

- [ ] **Step 2: 运行协议测试并确认模块缺失**

Run: `python -m unittest tests.test_frame_protocol -v`

Expected: FAIL，提示 `app/frame_protocol.c` 或协议导出尚不存在。

- [ ] **Step 3: 实现纯协议模块**

头文件定义：

```c
#define FRAME_PROTOCOL_HEADER_0  0xFFU
#define FRAME_PROTOCOL_HEADER_1  0x66U
#define FRAME_PROTOCOL_OVERHEAD  8U

uint32_t frame_protocol_crc32(const uint8_t *data, uint32_t len);
bool frame_protocol_pack_inplace(uint16_t *storage,
                                 uint16_t capacity_bytes,
                                 uint16_t rows,
                                 uint16_t cols,
                                 uint16_t frame_counter,
                                 uint16_t *packed_len);
```

实现从缓冲尾部向后写入大端样本，避免原地打包覆盖尚未读取的数据；CRC覆盖帧计数器和样本数据，不包含 `FF 66` 帧头和末尾CRC。

- [ ] **Step 4: 运行参考向量与固件构建**

Run: `python -m unittest tests.test_frame_protocol -v`

Expected: PASS，固定帧长度和逐字节内容一致。

Run: `cmake --build --preset Debug`

Expected: PASS，无重复协议符号。

- [ ] **Step 5: 保存本地检查点**

保存协议向量、CRC结果和 `.map` 中 `frame_protocol_*` 符号摘要到 `build/checkpoints/task-2/`。

---

### Task 3: 实现静态双缓冲所有权状态机

**Files:**
- Create: `app/frame_service.h`
- Create: `app/frame_service.c`
- Create: `tests/test_frame_state_model.py`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: 编译期 `BSP_MATRIX_ROWS`、`BSP_MATRIX_COLS`、`BSP_FRAME_BUFFER_COUNT == 2`。
- Produces: `frame_service_init()`、`frame_acquire_for_scan()`、`frame_publish_raw()`、`frame_take_raw()`、`frame_publish_packed()`、`frame_take_packed()`、`frame_mark_transmitting()`、`frame_release()`。

- [ ] **Step 1: 写状态转换模型测试**

测试以下合法序列：

```text
FREE -> ACQUIRING -> RAW_READY -> PACKED_READY -> TRANSMITTING -> FREE
```

并断言：发送占用两个缓冲时第三帧获取返回空；释放发送帧后只能在下一帧边界重新获取；非法重复发布被拒绝。

- [ ] **Step 2: 运行模型测试并确认失败**

Run: `python -m unittest tests.test_frame_state_model -v`

Expected: FAIL，状态定义或源文件尚不存在。

- [ ] **Step 3: 定义帧对象和状态接口**

```c
typedef enum {
    FRAME_STATE_FREE = 0,
    FRAME_STATE_ACQUIRING,
    FRAME_STATE_RAW_READY,
    FRAME_STATE_PACKED_READY,
    FRAME_STATE_TRANSMITTING
} frame_state_t;

typedef struct {
    uint16_t storage[(FRAME_PROTOCOL_OVERHEAD +
        BSP_MATRIX_ROWS * BSP_MATRIX_COLS * 2U + 1U) / 2U];
    uint16_t packed_len;
    volatile frame_state_t state;
} frame_buffer_t;
```

所有状态切换返回 `bool`，非法所有权转换不修改状态。ISR/主循环共享转换使用保存并恢复 PRIMASK 的短临界区。

- [ ] **Step 4: 实现并验证状态机**

Run: `python -m unittest tests.test_frame_state_model -v`

Expected: PASS。

Run: `cmake --build --preset Debug`

Expected: PASS；`.map` 显示两个帧缓冲为静态 `.bss`，无 `malloc/calloc/free` 引用。

- [ ] **Step 5: 保存本地检查点**

保存状态转换测试输出和 RAM占用变化到 `build/checkpoints/task-3/`。

---

### Task 4: 建立 ADS8681 通用前端适配层

**Files:**
- Create: `app/adc_frontend.h`
- Create: `app/adc_frontend_ads8681.c`
- Modify: `Drivers/BSP/ADS8681/drv_ads8681.h`
- Modify: `Drivers/BSP/ADS8681/drv_ads8681.c`
- Modify: `Drivers/BSP/bsp_init.h`
- Modify: `Drivers/BSP/bsp_init.c`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `g_bsp_adc`、SPI1、CD74HC4067、ADS8681寄存器和DMA驱动。
- Produces: 设计文档规定的 `adc_frontend_*` API和 `adc_frontend_result_t`。

- [ ] **Step 1: 定义前端契约并添加编译契约测试**

```c
typedef enum {
    ADC_FRONTEND_RESULT_PENDING = 0,
    ADC_FRONTEND_RESULT_SAMPLE,
    ADC_FRONTEND_RESULT_ROW_COMPLETE,
    ADC_FRONTEND_RESULT_ERROR
} adc_frontend_result_code_t;

typedef struct {
    adc_frontend_result_code_t code;
    uint16_t column;
    uint16_t sample;
} adc_frontend_result_t;
```

静态检查必须确认 `scan_service` 不包含 `drv_ads8681.h` 或 `drv_cd74hc4067.h`。

- [ ] **Step 2: 确认契约检查先失败**

Run: `python -m unittest tests.test_source_boundaries -v`

Expected: FAIL，前端接口文件或约束尚未满足。

- [ ] **Step 3: 实现 ADS8681 流水线适配**

对每行执行：

```text
step 0      选择列0，启动传输，结果丢弃
step 1      选择列1，启动传输，保存列0
...
step cols-1 选择末列，启动传输，保存前一列
step cols   保持末列并冲刷，保存末列，报告行完成
```

`adc_frontend_on_dma_complete()` 只解析驱动RX缓冲并返回结果，不写帧、不推进全局行号、不打印日志。

- [ ] **Step 4: 编译并进行板级单次读验证**

Run: `cmake --build --preset Debug`

Expected: PASS。

板级验证：复位后通过临时 `adc_read` 入口连续读取固定输入100次，确认 SPI无 `HAL_BUSY/HAL_ERROR`，数值范围与参考工程同一硬件输入一致。记录逻辑分析仪的 CS、SCLK和DMA完成时序。

- [ ] **Step 5: 保存本地检查点**

保存单次采样数据、SPI时序截图文件名和错误计数到 `build/checkpoints/task-4/`。

---

### Task 5: 实现非阻塞矩阵扫描服务

**Files:**
- Create: `app/app_events.h`
- Create: `app/app_events.c`
- Create: `app/scan_service.h`
- Create: `app/scan_service.c`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `adc_frontend_*`、`frame_acquire_for_scan()`、74HC595行选择、TIM2。
- Produces: `scan_init()`、`scan_start()`、`scan_stop()`、`scan_process()`、`scan_on_timer_irq()`、`scan_on_spi_complete_irq()`、`scan_on_spi_error_irq()`、`scan_reconfigure()`、`scan_get_status()`。

- [ ] **Step 1: 写扫描转换测试表**

使用 Python模型验证 2×2 时的预期存储顺序：

```text
row0: discard, col0, col1
row1: discard, col0, col1
frame complete exactly once
```

测试无空闲帧时完整下一帧进入 `DISCARDING`，但行列步骤继续推进且只增加一次 `frames_dropped`。

- [ ] **Step 2: 运行测试并确认失败**

Run: `python -m unittest discover -s tests -p "test_*model.py" -v`

Expected: FAIL，扫描模型尚未实现。

- [ ] **Step 3: 实现扫描状态与统计结构**

```c
typedef enum {
    SCAN_STATE_STOPPED = 0,
    SCAN_STATE_STARTING,
    SCAN_STATE_RUNNING,
    SCAN_STATE_STOPPING,
    SCAN_STATE_FAULT
} scan_state_t;

typedef struct {
    uint32_t frames_acquired;
    uint32_t frames_dropped;
    uint32_t spi_busy_errors;
    uint32_t spi_dma_errors;
    uint32_t frontend_sync_errors;
} scan_stats_t;
```

`scan_on_timer_irq()` 只启动一次前端步骤；`scan_on_spi_complete_irq()` 只保存样本、推进状态并发布帧；`scan_process()` 处理停止、重启和故障恢复事件。

- [ ] **Step 4: 实现安全重配置**

`scan_reconfigure(rows, cols, period_us)` 校验：

```text
1 <= rows <= BSP_MATRIX_ROWS
1 <= cols <= BSP_MATRIX_COLS
SCAN_CHANNEL_PERIOD_US_MIN <= period_us <= 65535
```

调用期间执行停止TIM2、完成或终止DMA、释放半帧、重置前端步骤、更新ARR并从新帧启动。

- [ ] **Step 5: 运行模型、编译和板级扫描验证**

Run: `python -m unittest discover -s tests -v`

Run: `cmake --build --preset Debug`

Expected: 全部PASS。

板级验证：先运行1×1、2×2、8×8，每种连续1000帧；确认帧完成次数、行列顺序和 SPI错误计数。

- [ ] **Step 6: 保存本地检查点**

保存三种矩阵配置的计数、错误统计和时序结果到 `build/checkpoints/task-5/`。

---

### Task 6: 实现帧打包与 USART2 DMA 数据口

**Files:**
- Create: `app/dataport.h`
- Create: `app/dataport.c`
- Modify: `app/frame_service.c`
- Modify: `Core/Src/dma.c`
- Modify: `Core/Src/usart.c`
- Modify: `Core/Src/stm32f1xx_it.c`
- Modify: `Core/Inc/stm32f1xx_it.h`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `RAW_READY` 帧、`frame_protocol_pack_inplace()`、`huart2`、`hdma_usart2_tx`。
- Produces: `dataport_init()`、`dataport_process()`、`dataport_on_tx_complete_irq()`、`dataport_on_error_irq()`、`dataport_get_stats()`。

- [ ] **Step 1: 写发送所有权和超时测试模型**

覆盖：正常DMA完成释放、DMA启动失败释放、超时终止释放、发送期间不可重新打包、帧计数在成功打包时递增并自然16位回绕。

- [ ] **Step 2: 运行测试并确认失败**

Run: `python -m unittest tests.test_frame_state_model -v`

Expected: FAIL，发送转换尚未满足。

- [ ] **Step 3: 实现主循环打包与发送服务**

`dataport_process()` 按顺序：

```text
消费 TX_DONE/ERROR/TIMEOUT 事件并释放 transmitting 帧
-> 取得一个 RAW_READY 帧并原地打包为 PACKED_READY
-> USART2空闲时取得 PACKED_READY 帧
-> 标为 TRANSMITTING
-> HAL_UART_Transmit_DMA()
```

DMA启动失败必须立即恢复 `FREE`，增加 `uart2_dma_errors`；超时阈值使用命名宏并基于帧长度、波特率计算出有裕量的固定上限。

- [ ] **Step 4: 补齐 USART2 DMA IRQ**

确保 DMA1 Channel7 IRQ调用 `HAL_DMA_IRQHandler(&hdma_usart2_tx)`；HAL TX完成/错误回调只发布 dataport事件，并先判断 `huart->Instance == USART2`。

- [ ] **Step 5: 运行测试、构建与协议实测**

Run: `python -m unittest discover -s tests -v`

Run: `cmake --build --preset Debug`

Expected: PASS。

板级验证：上位机连续接收1000帧，逐帧验证 `FF 66`、长度、计数器连续性和 CRC32；人为阻塞接收端，确认扫描继续且 `frames_dropped` 增长。

- [ ] **Step 6: 保存本地检查点**

保存1000帧校验摘要、丢帧测试结果和 USART2错误统计到 `build/checkpoints/task-6/`。

---

### Task 7: 实现 USART1 双向命令控制台

**Files:**
- Create: `app/console.h`
- Create: `app/console.c`
- Create: `app/command.h`
- Create: `app/command.c`
- Create: `tests/test_command_contract.py`
- Modify: `Drivers/BSP/debug_log.h`
- Modify: `Core/Src/usart.c`
- Modify: `Core/Src/stm32f1xx_it.c`
- Modify: `Core/Inc/stm32f1xx_it.h`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: USART1、`scan_*`、前端状态、扫描和发送统计。
- Produces: `console_init()`、`console_process()`、`console_write()`、`console_printf()`、`console_on_rx_byte_irq()`、`command_execute_line()`。

- [ ] **Step 1: 写命令契约测试**

测试命令表必须包含：

```text
help status scan_start scan_stop scan_rows scan_cols
scan_fps scan_period_us adc_read adc_reset spi_status
stats_show stats_clear
```

并验证 `scan_rows 0`、超最大值、非数字、参数缺失和多余参数返回明确错误；所有命令有非空帮助文本。

- [ ] **Step 2: 运行测试并确认失败**

Run: `python -m unittest tests.test_command_contract -v`

Expected: FAIL，命令表尚不存在。

- [ ] **Step 3: 实现 USART1 RX 环形缓冲**

```c
#define CONSOLE_RX_BUFFER_SIZE  128U
#define CONSOLE_LINE_SIZE        96U
#define COMMAND_MAX_ARGS          6U
```

使用 `HAL_UART_Receive_IT(&huart1, &rx_byte, 1)` 连续接收。RX完成回调只写环形缓冲并重新挂接下一字节；缓冲满时增加 `uart1_rx_overflows`，丢弃当前输入行直到换行。

- [ ] **Step 4: 实现静态命令表和处理函数**

```c
typedef int (*command_handler_t)(int argc, char **argv);

typedef struct {
    const char *name;
    command_handler_t handler;
    const char *help;
} command_entry_t;
```

数值解析使用 `strtoul` 并检查结束指针、范围和溢出。配置命令只调用 `scan_reconfigure()`，不能直接写扫描服务私有变量。

- [ ] **Step 5: 实现有限超时文本输出**

第一阶段 `console_write()` 使用分段 `HAL_UART_Transmit()`，每段设置有限超时；任何超时返回错误，不得永久阻塞采集主循环。禁止 USART1 文本走 USART2。

- [ ] **Step 6: 运行测试、构建和并发压力验证**

Run: `python -m unittest discover -s tests -v`

Run: `cmake --build --preset Debug`

Expected: PASS。

板级验证：采集过程中连续发送合法、非法和超长命令；USART1返回明确结果，USART2帧格式不受污染，SPI错误计数不增长。

- [ ] **Step 7: 保存本地检查点**

保存命令会话日志、溢出测试和并发采集统计到 `build/checkpoints/task-7/`。

---

### Task 8: 集成应用入口并完成系统验收

**Files:**
- Create: `app/app_main.h`
- Create: `app/app_main.c`
- Modify: `Core/Src/main.c`
- Modify: `Drivers/BSP/bsp_init.c`
- Modify: `CMakeLists.txt`
- Modify: `docs/superpowers/specs/2026-09-18-bare-metal-adc-pipeline-design.md`（仅记录已验证的最终接口或偏差）

**Interfaces:**
- Consumes: 所有前述模块。
- Produces: `bool app_init(void)`、`void app_process(void)` 和可烧录的完整裸机固件。

- [ ] **Step 1: 实现应用编排接口**

```c
bool app_init(void)
{
    if (!bsp_init()) return false;
    frame_service_init();
    console_init();
    dataport_init();
    if (!scan_init()) return false;
    return scan_start();
}

void app_process(void)
{
    console_process();
    scan_process();
    dataport_process();
}
```

实际返回类型需同步调整 `bsp_init()`；初始化失败时保持采集停止，通过 USART1输出故障原因。

- [ ] **Step 2: 将 `main.c` 收敛为初始化和超级循环**

CubeMX外设初始化之后调用 `app_init()`；无限循环只调用 `app_process()`。暂不启用 `__WFI()`，直到硬件压力测试证明所有事件均能可靠唤醒。

- [ ] **Step 3: 执行完整静态检查**

Run: `python -m unittest discover -s tests -v`

Run: `rg -n "rt-thread-nano|ResistorTactileBoard-1" CMakeLists.txt cmake build/Debug/compile_commands.json`

Expected: 测试PASS；第二条命令无输出。根目录中保留但未参与构建的旧RTOS源文件不构成固件依赖，待裸机功能验收后由用户另行决定是否归档或删除。

- [ ] **Step 4: 执行干净交叉编译和资源审计**

Run: `cmake --preset Debug --fresh`

Run: `cmake --build --preset Debug --clean-first`

Expected: PASS并生成 ELF、HEX和MAP。

检查 `.map`：

- 双帧缓冲大小符合当前矩阵配置；
- RAM总使用量留有中断栈和后续扩展余量；
- 没有 `rt_*`、`malloc`、`calloc`、`free`；
- 没有来自参考目录的对象文件。

- [ ] **Step 5: 执行板级功能验收**

按顺序验证：

1. 上电后 USART1报告初始化状态。
2. `status`、`scan_stop`、`scan_start` 正常。
3. 1×1、2×2、8×8采集矩阵顺序正确。
4. USART2连续1000帧全部满足协议格式和 CRC校验。
5. 阻塞 USART2 后系统继续扫描并按整帧增加丢弃数。
6. 连续输入 USART1命令不污染 USART2、不导致 SPI失步。
7. 注入 SPI DMA错误后不发送半帧，并能从完整帧边界恢复。
8. 修改周期和矩阵参数时不出现越界、半帧或死锁。

- [ ] **Step 6: 执行长时间稳定性验收**

默认8×8配置连续运行至少2小时。每10分钟采集一次 `stats_show`，结束时要求：

- 无无法恢复故障；
- `spi_dma_errors == 0`；
- 正常接收条件下 `frames_dropped == 0`；
- USART2 CRC错误为0；
- USART1仍可响应命令。

- [ ] **Step 7: 保存最终本地交付检查点**

在 `build/checkpoints/task-8/` 保存：构建命令及结果、ELF/HEX/MAP文件名、RAM/Flash摘要、1000帧协议报告、2小时统计和已知限制。不得复制或修改参考工程。

---

## Completion Criteria

只有以下条件全部成立才可宣称第一阶段完成：

- 根工程独立构建，参考目录零依赖，RT-Thread零依赖。
- ADS8681真实硬件矩阵采集稳定。
- MCU原地打包结果与旧数据协议逐字节兼容。
- USART2 DMA持续输出，阻塞时只丢整帧且不阻塞扫描。
- USART1可双向交互，命令参数清晰且边界校验完整。
- 静态双缓冲无所有权冲突，无运行期动态内存。
- 自动检查、交叉编译、1000帧测试及2小时板级测试均通过。
