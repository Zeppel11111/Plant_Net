# 扩展板 PINOUT 供电关系图设计

## 目的

将 Plant_Net 的开发板扩展板接线图补充为可直接用于 PCB 接口规划的简约图，明确逻辑信号与供电网络。

## 范围

- STM32F103C8T6 最小系统板、ESP-01S、串口转 RS485 模块、塔石传感器。
- ESP32-CAM 作为独立 Wi-Fi 节点，只在图中标示 Micro-USB 供电/烧录。
- 不新增固件功能、不改现有 IO 分配。

## 供电拓扑

- `+3V3`：STM32、ESP-01S、RS485 模块的逻辑侧共用；ESP-01S 的 VCC、EN/CH_PD 均接 `+3V3`，EN 保持上拉。
- `+12V`：仅供塔石传感器的 `V+`；传感器 GND 与 RS485 模块/STM32 的 GND 共地。
- ESP32-CAM：由独立的 Micro-USB 5 V 供电与烧录，不与扩展板建立电源或数据线连接。

## 信号关系

- PB10 → ESP-01S RXD；PB11 ← ESP-01S TXD。
- PA2 → RS485 DI/TXD；PA3 ← RS485 RO/RXD。
- RS485 A/B → 塔石 A/B。
- PA9/PA10 仅作为可选 UART 日志接口。

## 图形要求

- 使用红色表示正电源、黑色表示 GND、橙色表示 STM32 TX、蓝色表示模块 TX 到 STM32 RX。
- `+12V` 与 `+3V3` 显著分开，避免制造时误接。
- 将 PC13、PD0/PD1、PB6/PB7、PA13/PA14、NRST、BOOT0 标为未从扩展板引出。
