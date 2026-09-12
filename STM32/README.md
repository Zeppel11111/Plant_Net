# Plant_Net（原名 BSP_demo）

植物生长环境监测 —— STM32F103C8T6 BSP 学习项目（HAL 库 + FreeRTOS）。

## 组成
- **设备端**：F103 + 塔石温湿度光照传感器(RS485/Modbus-RTU) + ESP8266 联网，
  数据 30s 一条经 WiFi TCP 上行
- **服务器端**：`Flask_TCP/`（独立工程，见其 README）——TCP 收数据 → MySQL →
  网页曲线（可 cpolar 内网穿透上公网）

## 架构
- 插槽式模块架构：`app_config.h` 总开关，Driver(器件卡) + Middleware(算法卡) + App 零 #if
- 分层：App → Driver → BSP → HAL（只有 BSP 持有 HAL 句柄）

## 构建
- STM32CubeIDE 的 CMake 工程（TargetToolchain=CMake），VSCode + CMake/Ninja 亦可
- 本目录由 **BSP_demo 文件夹改名**而来：若从别处 clone/拷贝，务必先删除
  `build/` 缓存目录再重新 configure（CMakeCache 锁着旧绝对路径）
