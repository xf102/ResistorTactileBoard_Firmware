# RT-Thread 自动初始化与 GCC `--gc-sections` 兼容经验

## 1. 问题现象

- RT-Thread 配置中已开启 FinSH / MSH，但启动后没有 `msh />` 提示符。
- 串口只打印 RT-Thread 基础启动信息，`finsh_system_init()` 似乎从未被调用。
- 错误原因往往不在 FinSH 配置本身，而在链接脚本没有正确收集 `.rti_fn.*` 段。

## 2. 根因分析

### 2.1 RT-Thread 用 `INIT_EXPORT` 注册初始化函数

例如 `INIT_APP_EXPORT(finsh_system_init)` 展开后类似：

```c
static const init_fn_t __rt_init_finsh_system_init
    __attribute__((section(".rti_fn.6"))) = finsh_system_init;
```

RT-Thread 把初始化函数指针放进名为 `.rti_fn.<level>` 的只读段中。

段名与初始化阶段对应关系如下：

| 宏 | 段名 | 说明 |
|---|---|---|
| `INIT_BOARD_EXPORT(fn)` | `.rti_fn.1` | 板级初始化 |
| `INIT_DEVICE_EXPORT(fn)` | `.rti_fn.3` | 设备初始化 |
| `INIT_COMPONENT_EXPORT(fn)` | `.rti_fn.4` | 组件初始化 |
| `INIT_APP_EXPORT(fn)` | `.rti_fn.6` | 应用初始化 |

边界标记符号：

| 符号 | 段名 | 作用 |
|---|---|---|
| `__rt_init_rti_board_start` | `.rti_fn.0.end` | board 段起始边界 |
| `__rt_init_rti_board_end` | `.rti_fn.1.end` | board 段结束 / app 段起始 |
| `__rt_init_rti_end` | `.rti_fn.6.end` | app 段结束边界 |

### 2.2 运行时遍历原理

`rt_components_board_init()` 遍历 board 段：

```c
for (fn_ptr = &__rt_init_rti_board_start;
     fn_ptr < &__rt_init_rti_board_end;
     fn_ptr++)
{
    (*fn_ptr)();
}
```

`rt_components_init()` 遍历 app 段：

```c
for (fn_ptr = &__rt_init_rti_board_end;
     fn_ptr < &__rt_init_rti_end;
     fn_ptr++)
{
    (*fn_ptr)();
}
```

### 2.3 `--gc-sections` 会回收函数指针段

GCC 链接时如果启用了 `-Wl,--gc-sections`，会丢弃没有被直接按名引用的段。

`.rti_fn.1`、`.rti_fn.6` 等真正存放初始化函数指针的段，只被间接遍历使用，没有代码按名引用 `__rt_init_xxx` 符号，因此会被当成死代码回收。

只有边界标记符号（如 `__rt_init_rti_board_start`）因为被 `components.c` 直接引用而保留，但边界符号的排序会被链接器随意排放，可能出现 `start > end`，导致遍历循环一次都不执行。

### 2.4 STM32CubeMX 生成的 linker script 缺少收集规则

默认 CubeMX 链接脚本通常没有专门收集 `.rti_fn.*` 的输出段，于是：

- 真正的初始化函数指针段被 GC 回收；
- 只剩边界标记符号，且位置可能错乱；
- 最终 FinSH / 各组件初始化函数都不会被执行。

## 3. 修复方法

在 linker script 的 FLASH 区域（建议 `.text` 之后、`.rodata` 之前）增加 `.init_fn` 输出段：

```ld
  /* RT-Thread components initialization table.
   * KEEP prevents --gc-sections from discarding the function pointers;
   * SORT orders them by level (.rti_fn.0, .rti_fn.1, ..., .rti_fn.6).
   */
  .init_fn :
  {
    . = ALIGN(4);
    __rt_init_start = .;
    KEEP(*(SORT(.rti_fn*)))
    __rt_init_end = .;
    . = ALIGN(4);
  } >FLASH
```

关键点：

- `KEEP(...)`：强制保留 `.rti_fn.*` 段，防止 `--gc-sections` 误删。
- `SORT(.rti_fn*)`：按段名字母顺序排序，保证 board → device → component → app 的遍历顺序正确。
- `__rt_init_start` / `__rt_init_end`：可选，可用于调试或未来扩展。

## 4. 为什么不推荐直接去掉 `--gc-sections`

- 去掉 `--gc-sections` 会保留所有未使用代码，固件体积明显增大。
- 对小 Flash MCU（如 STM32F103C8T6 仅 64KB Flash）不友好。
- 没有解决根本问题：后续新增 `INIT_EXPORT` 组件仍可能踩坑。

推荐做法：保留 `--gc-sections`，同时用 `KEEP(*(SORT(.rti_fn*)))` 显式保护初始化表。

## 5. 验证方法

重编后查看 `.map` 文件，确认：

1. 存在 `.init_fn` 输出段；
2. 段内包含 `.rti_fn.0` 到 `.rti_fn.6.end` 的符号；
3. `__rt_init_uart_init`、`__rt_init_finsh_system_init` 等符号有最终链接地址，而不仅仅出现在对象文件中。

也可以在线验证：启动后如果看到 `msh />` 提示符，说明 `finsh_system_init()` 已被成功调用。

## 6. 适用范围

- STM32CubeMX + RT-Thread Nano / 标准版
- GCC 工具链 + `--gc-sections`
- 任何使用 `INIT_EXPORT` 注册初始化函数的项目

## 7. 经验总结

1. `INIT_EXPORT` 依赖链接脚本显式收集 `.rti_fn.*` 段；
2. `--gc-sections` 与隐式符号表是天然冲突的，必须用 `KEEP()` 显式保护；
3. 遇到“开了 FinSH 没 shell”的问题，优先检查 map 文件中的 `.rti_fn` 段，而不是怀疑配置；
4. 小 Flash 项目应保留 `--gc-sections`，通过精确的 `KEEP` 规则平衡体积与功能；
5. FinSH 的命令符号表 `FSymTab` 同样需要链接脚本显式收集；若只修复了 `.rti_fn.*` 而遗漏 `.fsymtab`，会出现 `undefined reference to __fsymtab_start/__fsymtab_end`。

## 8. 补充：FinSH 符号表段

### 8.1 现象

链接阶段报错：

```text
undefined reference to `__fsymtab_start'
undefined reference to `__fsymtab_end'
```

### 8.2 原因

`MSH_CMD_EXPORT(cmd, desc)` 在 GCC 下会展开为类似：

```c
RT_USED const struct finsh_syscall __fsym_##cmd RT_SECTION("FSymTab") = { ... };
```

`shell.c` 中的 `finsh_system_init()` 在 GCC 分支里使用 `__fsymtab_start` 和 `__fsymtab_end` 取得符号表范围。这两个符号必须由链接脚本定义，否则链接失败。

### 8.3 修复

在 linker script 的 `.init_fn` 之后、`.rodata` 之前增加：

```ld
  /* FinSH function symbol table.
   * KEEP prevents --gc-sections from discarding exported shell commands.
   */
  .fsymtab :
  {
    . = ALIGN(4);
    __fsymtab_start = .;
    KEEP(*(FSymTab))
    __fsymtab_end = .;
    . = ALIGN(4);
  } >FLASH
```

### 8.4 验证

编译成功后查看 `.map` 文件，确认：

1. 存在 `.fsymtab` 输出段；
2. 段内包含通过 `MSH_CMD_EXPORT` 导出的命令符号（如 `__fsym_xxx`）。

同时在线验证：进入 shell 后输入 `help` 或已导出的命令，确认命令列表不为空。
