# USB CDC 虚拟串口调试笔记

> 适用文件：`app/USB_CDC_app.c` / `USB_DEVICE/App/usbd_cdc_if.c`
> 运行环境：STM32F103 + RT-Thread Nano + STM32 USB Device Library (CDC)

## 1. 背景与目标

为了验证 USB CDC 虚拟串口能否正常工作，在 CDC 发送线程里加入循环发送 `hello USB` 的测试逻辑，
用电脑端串口监视器（VS Code Serial Monitor）观察是否能收到数据。

调试过程中依次暴露并解决了两个问题：

1. **COM 口能被电脑识别，但打不开** —— CDC 类请求未正确响应。
2. **COM 口能打开，但收不到任何数据** —— 主机未就绪时发送导致线程死锁。

## 2. 测试发送逻辑（app/USB_CDC_app.c）

CDC 线程原本阻塞在邮箱接收上，永远等待采集线程投递数据帧。
为做环路测试，把等待改成 **1 秒超时**：超时就发一次测试字符串，收到数据帧则照常转发。

```c
static const uint8_t hello_usb[] = "hello USB\r\n";

while (1) {
    ret = rt_mb_recv(frame_mb, (rt_ubase_t *)&frame,
                     rt_tick_from_millisecond(1000));
    if (ret == -RT_ETIMEOUT) {
        /* 每秒发一次测试字符串 */
        CDC_Transmit_FS_Wait((uint8_t *)hello_usb, sizeof(hello_usb) - 1);
        continue;
    }
    else if (ret != RT_EOK) {
        continue;
    }

    /* 正常数据帧转发 */
    CDC_Transmit_FS_Wait((uint8_t *)frame->data, frame->len * sizeof(uint16_t));
    frame_pool_put(frame);
}
```

## 3. 问题一：COM 口看得见但打不开

### 3.1 现象

设备能正常枚举，电脑设备管理器里出现新的 COM 口、无黄色警告，
但用串口监视器打开时直接失败。

### 3.2 根因

`usbd_cdc_if.c` 里的 `CDC_Control_FS` 对 CDC/ACM 类请求全是空处理：

- `CDC_SET_LINE_CODING`：主机下发线路参数（波特率 / 数据位 / 停止位 / 校验）
- `CDC_GET_LINE_CODING`：主机查询当前线路参数
- `CDC_SET_CONTROL_LINE_STATE`：DTR / RTS 控制

**枚举成功 ≠ 串口可用。** 枚举只看设备 / 配置描述符；而 Windows 的 `usbser.sys`
驱动在 **打开 COM 口** 时会发 `GET_LINE_CODING` 查询线路参数。如果固件不响应或返回全 0 / 无效数据，
驱动会认为设备状态异常，拒绝完成打开流程，于是表现为“看得见、打不开”。

### 3.3 修复

维护一份合法的默认线路参数（115200 8N1），并实际处理 SET / GET 请求：

```c
#include <string.h>

/* 默认线路参数：115200 8N1 */
static USBD_CDC_LineCodingTypeDef line_coding =
{
    115200U, /* bitrate  */
    0x00U,   /* 1 stop bit */
    0x00U,   /* no parity  */
    0x08U    /* 8 data bits */
};

switch (cmd) {
    case CDC_SET_LINE_CODING:
        if (length == sizeof(line_coding)) {
            memcpy(&line_coding, pbuf, sizeof(line_coding));
        }
        break;

    case CDC_GET_LINE_CODING:
        memcpy(pbuf, &line_coding, sizeof(line_coding));
        break;

    case CDC_SET_CONTROL_LINE_STATE:
        /* DTR/RTS 未使用，直接应答 */
        break;
    ...
}
```

补上之后，主机查询时能拿到有效结构体，驱动允许打开，COM 口即可正常打开。

## 4. 问题二：打开后收不到数据

### 4.1 现象

COM 口能打开了，但串口监视器里始终没有 `hello USB`。

### 4.2 根因

CDC 线程一启动就每秒尝试发送。如果此时主机还没完成 `SET_CONFIGURATION`（串口尚未打开），
`CDC_Transmit_FS_Wait` 内部会调用 `USBD_CDC_TransmitPacket`，把 `TxState` 置 1 并启动一次 IN 传输，
然后阻塞等待 ISR 通过信号量通知“发送完成”。

但主机未就绪时这个完成中断永远不会来，线程就 **永久卡死在信号量上**。
等用户再打开串口时，线程已经死了，自然不会再发任何数据。

### 4.3 修复

发送前判断 USB 是否已进入 `CONFIGURED` 状态，未就绪就不发：

```c
extern USBD_HandleTypeDef hUsbDeviceFS;

if (ret == -RT_ETIMEOUT) {
    if (hUsbDeviceFS.dev_state == USBD_STATE_CONFIGURED) {
        CDC_Transmit_FS_Wait((uint8_t *)hello_usb, sizeof(hello_usb) - 1);
    }
    continue;
}
```

## 5. 测试步骤

1. 重新编译、烧录固件。
2. **插拔 USB 或按复位**，让板子在“主机已连接”的状态下重新枚举。
3. 在串口监视器里打开对应 COM 口（波特率 115200），应每秒看到一行 `hello USB`。

## 6. 遗留事项

- 目前只对 `hello USB` 测试发送加了 `CONFIGURED` 保护。
  后续若打开 `DATA_ACQUISITION_Init()`，**帧数据发送路径同样需要在 USB 未就绪时做保护**，否则会重现死锁。
- `hello USB` 循环是调试用，验证通过后应移除，恢复纯数据帧转发逻辑。