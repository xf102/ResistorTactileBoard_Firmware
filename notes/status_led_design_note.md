# 状态指示 LED 组件设计说明（bsp_led）

> 适用文件：`Drivers/BSP_Drivers/Status_LED/bsp_led.c` / `bsp_led.h` / `pin_def.h`
> 运行环境：STM32F1 + RT-Thread Nano

## 1. 概述

本组件是一个基于 RT-Thread 的**双层 LED 控制架构**：

- **直接控制层**：提供即时 GPIO 读写（`bsp_led_on/off/set/toggle`），同步、一次性。
- **模式引擎层**：通过一个低优先级后台线程异步执行"闪烁模式（pattern）"，调用方非阻塞。

两层共享同一套硬件抽象（引脚 + 有效电平映射），对上层屏蔽具体接线差异。

```
            ┌──────────────────────────────────────────┐
 应用任务 ──┤ bsp_status_led_set()  →  消息队列(MQ)      │
            │                              │            │
            │                    ┌─────────▼─────────┐  │
            │                    │ status_led 线程    │  │
            │                    │ (10ms 轮询)        │  │
            │                    └─────────┬─────────┘  │
            │                              │            │
            │  bsp_led_on/off/set/toggle ──┤            │
            │                              ▼            │
            │                    ┌──────────────────┐   │
            │                    │ bsp_led_set()    │   │
            │                    │ 逻辑态→物理电平   │   │
            │                    └────────┬─────────┘   │
            └─────────────────────────────┼────────────┘
                                          ▼
                                     HAL_GPIO_WritePin
```

## 2. 硬件抽象层

`pin_def.h` 定义物理接线：

| LED   | 引脚         | 端口  | 有效电平              |
|-------|--------------|-------|-----------------------|
| LED_0 | GPIO_PIN_0   | GPIOB | 高电平有效 (`GPIO_PIN_SET`) |
| LED_1 | GPIO_PIN_1   | GPIOB | 高电平有效 (`GPIO_PIN_SET`) |

`.c` 中用描述符表把枚举 ID 映射到硬件：

```c
typedef struct {
    GPIO_TypeDef *port;
    uint16_t      pin;
    GPIO_PinState active_lvl;   /* "亮"对应的实际电平 */
} bsp_led_desc_t;
```

`bsp_led_set()` 据此做**逻辑状态 → 物理电平**转换：逻辑 `ON` 写 `active_lvl`，`OFF` 写相反电平。
将来硬件改成低电平驱动时，只需改 `pin_def.h`，上层代码无需改动。

> 小瑕疵：`bsp_led_set()` 中先按 `active_lvl` 算了一次电平，又用
> `if (active_lvl == GPIO_PIN_RESET)` 重新算了一次，逻辑冗余（active-low 分支会覆盖前值），
> 但结果正确，可后续简化。

## 3. 状态的定义（三个层次）

### 3.1 层次一：LED 标识（哪颗灯）

```c
typedef enum {
    BSP_STATUS_LED_0 = 0,
    BSP_STATUS_LED_1,
    BSP_STATUS_LED_MAX   /* 哨兵值：数组大小 + 边界检查 */
} bsp_status_led_t;
```

### 3.2 层次二：即时开关状态（亮/灭）

```c
#define BSP_LED_OFF  0
#define BSP_LED_ON   1
```

这是**逻辑值**而非 GPIO 电平，由 `bsp_led_set()` 翻译成实际电平。

### 3.3 层次三：闪烁模式（pattern）——核心

模式定义分两部分。

**(a) 枚举——对外接口：**

```c
typedef enum {
    BSP_LED_PATTERN_OFF = 0,       /* 常灭 */
    BSP_LED_PATTERN_ON,            /* 常亮 */
    BSP_LED_PATTERN_BLINK_SLOW,    /* 1 Hz：500ms亮 / 500ms灭 */
    BSP_LED_PATTERN_BLINK_FAST,    /* 5 Hz：100ms亮 / 100ms灭 */
    BSP_LED_PATTERN_DOUBLE_BLINK,  /* 双闪：亮50→灭50→亮50→灭850 */
    BSP_LED_PATTERN_FAST_FLASH,    /* 快闪：亮100ms，灭1000ms */
    BSP_LED_PATTERN_MAX
} bsp_led_pattern_t;
```

**(b) 步骤表——内部数据驱动定义：**

每个模式拆解为时间步（step）序列，每步描述"保持什么状态、持续多久"：

```c
typedef struct {
    uint8_t  state;        /* 0=灭, 1=亮 */
    uint16_t duration_ms;  /* 该步持续时间 */
} bsp_led_pattern_step_t;
```

例如双闪：

```c
static const bsp_led_pattern_step_t steps_double_blink[] =
    {{1, 50}, {0, 50}, {1, 50}, {0, 850}};
/*   亮50ms  灭50ms  亮50ms   灭850ms  → 一个周期 1000ms */
```

再用描述符把枚举与步骤表绑定：

```c
typedef struct {
    const bsp_led_pattern_step_t *steps;  /* 指向步骤数组 */
    uint8_t  step_count;                  /* 步数 */
    uint16_t total_period_ms;             /* 周期总时长（用于取模循环） */
} bsp_led_pattern_desc_t;
```

各模式对照表：

| 模式                  | 步骤表               | 步数 | 周期(ms) |
|-----------------------|----------------------|------|----------|
| `OFF`                 | `{0,100}`            | 1    | 100      |
| `ON`                  | `{1,100}`            | 1    | 100      |
| `BLINK_SLOW`          | `{1,500},{0,500}`    | 2    | 1000     |
| `BLINK_FAST`          | `{1,100},{0,100}`    | 2    | 200      |
| `DOUBLE_BLINK`        | `{1,50},{0,50},{1,50},{0,850}` | 4 | 1000 |
| `FAST_FLASH`          | `{1,100},{0,1000}`   | 2    | 1100     |

**数据驱动设计的好处**：新增模式只需加一行步骤表 + 一行描述符，线程逻辑完全不用改。

## 4. 后台线程如何运作

### 4.1 线程与 IPC 配置

```c
#define STATUS_LED_THREAD_STACK_SIZE  512
#define STATUS_LED_THREAD_PRIORITY    (RT_THREAD_PRIORITY_MAX - 2)  /* 低优先级 */
#define STATUS_LED_POLL_PERIOD_MS     10      /* 每 10ms 轮询一次 */
#define STATUS_LED_MQ_POOL_SIZE       4       /* 消息队列深度 */
```

- **低优先级**：RT-Thread 数值越大优先级越低，`MAX-2` 保证不抢占实时任务。
- **消息队列**：命令经 `rt_messagequeue` 传递，实现"调用方非阻塞、线程异步执行"。

命令结构体很小：

```c
typedef struct {
    bsp_status_led_t  led;      /* 哪颗灯 */
    bsp_led_pattern_t pattern;  /* 切到什么模式 */
} bsp_status_led_cmd_t;
```

### 4.2 命令投递 API

```c
rt_err_t bsp_status_led_set(bsp_status_led_t led, bsp_led_pattern_t pattern)
{
    if (led >= BSP_STATUS_LED_MAX || pattern >= BSP_LED_PATTERN_MAX)
        return -RT_EINVAL;                 /* 1. 参数合法性检查 */
    cmd.led = led;  cmd.pattern = pattern;
    return rt_mq_send(&status_led_mq, &cmd, sizeof(cmd));  /* 2. 投递后立即返回 */
}
```

调用方发完命令即走，**不等待**，真正的 GPIO 操作全由后台线程完成。

### 4.3 线程主循环（核心）

线程为每颗灯维护两个独立的运行时变量：

```c
bsp_led_pattern_t current_pattern[BSP_STATUS_LED_MAX];  /* 各灯当前模式 */
uint32_t          phase_ms[BSP_STATUS_LED_MAX];         /* 各灯在周期内的相位 */
```

主循环每 10ms 一轮，分三步：

**① 排空命令队列（非阻塞）**

```c
while (rt_mq_recv(&status_led_mq, &cmd, sizeof(cmd), RT_WAITING_NO) == RT_EOK) {
    current_pattern[cmd.led] = cmd.pattern;  /* 切换模式 */
    phase_ms[cmd.led] = 0;                   /* 相位清零，从头开始闪 */
}
```

`RT_WAITING_NO` 非阻塞接收，`while` 一次取光所有积压命令（后发覆盖先发，
符合"只要最新状态"语义）。切换模式时**相位归零**，保证新模式从第一步开始。

**② 驱动每颗灯**

```c
for (int i = 0; i < BSP_STATUS_LED_MAX; i++) {
    uint8_t state = led_pattern_get_state(current_pattern[i], phase_ms[i]);
    bsp_led_set((bsp_status_led_t)i, state);   /* 写 GPIO */

    phase_ms[i] += STATUS_LED_POLL_PERIOD_MS;  /* 相位推进 10ms */
    if (phase_ms[i] >= led_patterns[current_pattern[i]].total_period_ms)
        phase_ms[i] = 0;                        /* 到周期末尾 → 回卷 */
}
```

**③ 睡眠 10ms**，进入下一轮。

### 4.4 相位采样算法 led_pattern_get_state()

把"相位时间"翻译成"当前该亮还是灭"：

```c
uint32_t t = phase_ms % desc->total_period_ms;  /* 定位在周期内的位置 */
uint32_t accum = 0;
for (int i = 0; i < desc->step_count; i++) {
    accum += desc->steps[i].duration_ms;        /* 累加步长形成时间轴 */
    if (t < accum)                              /* 找到 t 落在哪个步区间 */
        return desc->steps[i].state;
}
```

以双闪为例（周期 1000ms），时间轴被切成：

```
[0,50)亮   [50,100)灭   [100,150)亮   [150,1000)灭
```

给定 `phase_ms`，累加步长找到对应区间并返回该步 `state`。
线程每 10ms 采样一次，相当于以 10ms 时间分辨率"播放"这张时间表。

## 5. 两套 API 的关系

| API                            | 路径                       | 特点                     |
|--------------------------------|----------------------------|--------------------------|
| `bsp_led_on/off/set/toggle`    | 直接写 GPIO                | 同步、即时、一次性，绕过线程 |
| `bsp_status_led_set`           | 发消息队列 → 线程执行       | 异步、持续闪烁、非阻塞     |

> ⚠️ **耦合点**：直接控制 API 与模式线程都会写同一颗灯的 GPIO。
> 若某灯正在线程里跑闪烁模式，此时调用 `bsp_led_off()` 直接关掉，
> 线程下一个 10ms 轮询又会按模式写回——两者会"打架"。
> **正确用法**：想停灯就发 `BSP_LED_PATTERN_OFF`，而不是直接 `bsp_led_off()`。

## 6. 初始化流程

```
bsp_led_init()
  ├─ 把所有灯 GPIO 写为 RESET（确保上电默认熄灭）
  └─ bsp_status_led_init()
        ├─ rt_mq_init()       创建消息队列（FIFO，深度 4）
        ├─ rt_thread_init()   创建线程（栈 512B，低优先级）
        └─ rt_thread_startup() 启动线程，进入无限循环
```

前置条件：GPIO 时钟与引脚模式已由 CubeMX 生成的 `MX_GPIO_Init()` 配置好，
本组件只负责逻辑控制。

## 7. 典型用法

```c
/* 初始化（在 MX_GPIO_Init() 之后调用一次） */
bsp_led_init();

/* 让 LED0 慢闪、LED1 双闪（异步，立即返回） */
bsp_status_led_set(BSP_STATUS_LED_0, BSP_LED_PATTERN_BLINK_SLOW);
bsp_status_led_set(BSP_STATUS_LED_1, BSP_LED_PATTERN_DOUBLE_BLINK);

/* 停止某灯：发送 OFF 模式，而非直接 bsp_led_off() */
bsp_status_led_set(BSP_STATUS_LED_0, BSP_LED_PATTERN_OFF);
```

## 8. 设计要点小结

1. **状态定义**采用"枚举对外 + 步骤表对内"的数据驱动方式，把闪烁模式描述为「状态-时长」时间轴。
2. **线程**以 10ms 为节拍，循环执行「排空命令队列切换模式 → 按相位采样步骤表 → 写 GPIO → 推进相位并回卷」。
3. 用低优先级后台任务把异步闪烁与业务代码彻底解耦，调用方零阻塞。
4. 硬件差异（有效电平）封装在 `bsp_led_set()`，便于移植。
5. 注意直接控制与模式控制会争用同一 GPIO，停灯应使用 `BSP_LED_PATTERN_OFF`。