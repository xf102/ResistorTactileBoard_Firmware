# 系统启动链文档

## 概述

本文档描述了 ResistorTactileBoard 固件的完整启动流程，重点关注 RT-Thread entry() 函数之后的启动顺序。

## 启动链流程图

```
上电/复位
    │
    ▼
Reset_Handler (startup_stm32f103xb.s)
    │
    ├─ 设置初始 SP
    ├─ 设置初始 PC
    ├─ 配置向量表
    ├─ 调用 __libc_init_array (C 库初始化)
    │
    ▼
entry() (rt-thread-nano/components/components.c)
    │
    ▼
rtthread_startup() (RT-Thread 启动主函数)
    │
    ├─ rt_hw_interrupt_disable()          # 关中断
    │
    ├─ rt_hw_board_init()                 # 板级初始化
    │   ├─ HAL_Init()                     # HAL 库初始化
    │   ├─ SystemClock_Config()           # 系统时钟配置 (HSE 8MHz + PLL x9 → 72MHz)
    │   ├─ MX_GPIO_Init()                 # GPIO 初始化
    │   ├─ MX_DMA_Init()                  # DMA 初始化
    │   ├─ MX_SPI1_Init()                 # SPI1 初始化 (ADS8681)
    │   ├─ MX_SPI2_Init()                 # SPI2 初始化 (74HC595)
    │   ├─ MX_USART1_UART_Init()          # UART1 初始化
    │   ├─ MX_TIM2_Init()                 # TIM2 初始化 (停止状态)
    │   ├─ MX_USB_DEVICE_Init()           # USB 设备初始化
    │   ├─ rt_components_board_init()     # 板级组件初始化
    │   ├─ rt_system_heap_init()          # 堆初始化 (8KB)
    │   └─ bsp_basic_service_init()       # 基础服务初始化 (UART 控制台)
    │
    ├─ rt_show_version()                  # 显示 RT-Thread 版本
    │
    ├─ rt_system_timer_init()             # 定时器系统初始化
    │
    ├─ rt_system_scheduler_init()         # 调度器初始化
    │
    ├─ rt_application_init()              # 创建主线程
    │   └─ rt_thread_create("main", main_thread_entry, ...)
    │
    ├─ rt_system_timer_thread_init()      # 定时器线程初始化
    │
    ├─ rt_thread_idle_init()              # 空闲线程初始化
    │
    └─ rt_system_scheduler_start()        # 启动调度器
        │
        ▼
    调度器运行，切换到主线程
        │
        ▼
main_thread_entry() (主线程入口)
    │
    ├─ rt_components_init()               # 组件初始化
    │   ├─ INIT_DEVICE_EXPORT 设备驱动
    │   ├─ INIT_COMPONENT_EXPORT 组件
    │   ├─ INIT_FS_EXPORT 文件系统
    │   ├─ INIT_ENV_EXPORT 环境变量
    │   └─ INIT_APP_EXPORT 应用模块
    │
    └─ main()                             # 用户主函数
        │
        ├─ bsp_init()                     # BSP 驱动初始化
        │   ├─ bsp_led_init()             # LED 初始化
        │   ├─ HC595_Init()               # 74HC595 行驱动初始化
        │   ├─ ADS8681_Init()             # ADS8681 ADC 初始化
        │   └─ CD74HC4067_Init()          # CD74HC4067 列选择初始化
        │
        ├─ bsp_frame_pool_init(32, 32)    # 帧缓冲池初始化
        │
        ├─ // DATA_ACQUISITION_Init()     # 数据采集初始化 (已注释)
        │
        ├─ USB_CDC_app_Init()             # USB CDC 应用初始化
        │   └─ 创建 USB 发送线程
        │
        ├─ DATA_ACQUISITION_DEBUG_Init()  # 调试命令注册
        │   └─ 注册 msh 调试命令
        │
        └─ 进入主循环
            └─ while(1) { rt_thread_mdelay(100); }
```

## 关键启动阶段说明

### 1. Reset_Handler 阶段
- **文件**: `startup_stm32f103xb.s`
- **功能**: 
  - 设置初始栈指针 (SP)
  - 设置初始程序计数器 (PC)
  - 配置中断向量表
  - 调用 C 库初始化 (`__libc_init_array`)
  - 跳转到 `entry()` 函数

### 2. entry() → rtthread_startup() 阶段
- **文件**: `rt-thread-nano/components/components.c`
- **功能**:
  - 这是 RT-Thread 的真正入口点
  - 调用 `rtthread_startup()` 启动系统

### 3. rtthread_startup() 阶段
- **文件**: `rt-thread-nano/components/components.c`
- **功能**: 按顺序完成系统初始化
  1. 关中断
  2. 板级初始化 (`rt_hw_board_init`)
  3. 显示版本信息
  4. 定时器系统初始化
  5. 调度器初始化
  6. 创建主线程
  7. 启动调度器

### 4. rt_hw_board_init() 阶段
- **文件**: `rt-thread-nano/board.c`
- **功能**: 完成硬件初始化
  - HAL 库初始化
  - 系统时钟配置 (72MHz)
  - 外设初始化 (GPIO, DMA, SPI, UART, TIM, USB)
  - 堆内存初始化 (8KB)
  - 基础服务初始化

### 5. main_thread_entry() 阶段
- **文件**: `rt-thread-nano/components/components.c`
- **功能**: 
  - 调用组件初始化 (`rt_components_init`)
  - 调用用户 `main()` 函数

### 6. main() 阶段
- **文件**: `Core/Src/main.c`
- **功能**: 用户应用初始化
  - BSP 驱动初始化
  - 帧缓冲池初始化
  - USB CDC 应用初始化
  - 调试命令注册
  - 进入主循环

## 调试模式下的启动特点

在调试模式下，`DATA_ACQUISITION_Init()` 被注释，系统不会自动启动数据采集扫描。用户需要通过 msh 命令手动控制：

1. 运行 `dbg_init` 初始化硬件模块
2. 使用调试命令逐步调通硬件
3. 验证各模块功能正常后再启用自动扫描

## 配置开关

相关配置在 `rt-thread-nano/include/rtconfig.h` 中：
```c
#define RT_USING_COMPONENTS_INIT    // 启用组件初始化
#define RT_USING_USER_MAIN          // 使用用户 main 函数
#define RT_USING_HEAP               // 启用堆管理
```

## 线程优先级

| 线程 | 优先级 | 说明 |
|------|--------|------|
| main | 10 | 主线程 (RT_MAIN_THREAD_PRIORITY) |
| data_acq | 5 | 数据采集线程 (高优先级) |
| usb_cdc | 15 | USB CDC 发送线程 |
| idle | 31 | 空闲线程 (最低优先级) |
