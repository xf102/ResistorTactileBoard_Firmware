# 数据采集调试指南

## 概述

本调试模块提供基于 msh 命令的逐步调试功能，用于手动调通数据采集硬件。

## 调试流程

### 1. 初始化硬件

```
msh > dbg_init
```

输出示例：
```
[DBG] Initializing hardware modules...
[DBG]   74HC595  OK
[DBG]   ADS8681  OK
[DBG]   CD74HC4067 OK
[DBG] Hardware initialization complete.
```

### 2. 检查状态

```
msh > dbg_status
```

输出示例：
```
=== Hardware Status ===
  Initialized : YES
  Current Row : 0
  Current Col : 0
  Period (us) : 100
=======================
```

### 3. 单点测试

```
msh > dbg_test_single 0 0
```

输出示例：
```
[TEST] Row=0 Col=0 Raw=1234
```

### 4. 行扫描测试

```
msh > dbg_test_row 0
```

输出示例：
```
[TEST ROW 0] Scanning...
  [ 0]  1234  [ 1]  1256  [ 2]  1278  [ 3]  1300
  [ 4]  1322  [ 5]  1344  [ 6]  1366  [ 7]  1388
  ...
[TEST ROW 0] Done.
```

## 命令列表

| 命令 | 说明 |
|------|------|
| `dbg_init` | 初始化硬件模块 |
| `dbg_status` | 显示硬件状态 |
| `dbg_row <0-31>` | 选择行 |
| `dbg_col <0-31>` | 选择列 |
| `dbg_read` | 读取当前选中通道 ADC 值 |
| `dbg_read_row` | 读取整行数据 |
| `dbg_period <us>` | 设置采样周期 |
| `dbg_params` | 显示当前参数 |
| `dbg_test_single [row] [col]` | 单步测试 |
| `dbg_test_row [row]` | 行扫描测试 |

## 调试步骤建议

1. **硬件初始化**：运行 `dbg_init` 初始化所有硬件模块
2. **单点验证**：使用 `dbg_test_single 0 0` 验证第一个点
3. **行扫描**：使用 `dbg_test_row 0` 验证整行数据
4. **多行测试**：依次测试不同行，验证行选择功能
5. **参数调整**：根据需要调整采样周期等参数

## 注意事项

1. 调试模式下不启动自动扫描，需要手动控制
2. 确保硬件连接正确后再运行 `dbg_init`
3. 如果读取失败，检查 SPI 和 GPIO 配置
4. 采样周期最小值为 10us
