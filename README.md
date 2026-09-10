# MYFOC 固件升级上位机

这是 STM32F407IGT6 Bootloader 的 Qt 5.14.2 / WinUSB 升级工具。设备同时枚举 CDC 和 Vendor-specific WinUSB Bulk；上位机的升级通道只使用 WinUSB。

## 功能

- Application 与 Bootloader 使用完全相同的 USB 身份（VID/PID CAFE:4070）和接口布局，避免模式切换后重新安装驱动。
- “刷新设备”会实际打开每个 WinUSB 接口并发送 `GET_MODE`，根据响应显示 Application 或 Bootloader，不根据 PID 猜测模式。
- 使用 Windows 文件管理器选择 .hex / .ihx 文件。
- 校验 Intel HEX checksum、地址范围、重叠记录和 Cortex-M4 向量表。
- Application 在线切换到 Bootloader，并按相同 STM32 UID 序列号重新匹配设备。
- 支持断点恢复、超时重传、取消升级、整镜像 CRC32 和向量表复核。
- 显示阶段、已确认字节、百分比、速度、预计剩余时间和事件记录。
- 进度仅根据 Bootloader 的 BL_DATA 确认偏移推进。

## 直接运行

发布程序位于 deploy\MYFOCFirmwareUpdater.exe。

运行前先分别烧录最新 Bootloader 和 Application。Windows 10/11 会根据固件提供的 Microsoft OS 2.0 描述符为接口 2 自动绑定系统自带 WinUSB 驱动；CDC 接口使用系统自带串口驱动，不需要自定义 INF。

操作顺序：

1. USB 连接控制器，确保电机处于 IDLE、功率输出关闭。
2. 启动上位机，点击“刷新设备”，选择目标设备。
3. 点击“浏览...”选择链接地址为 0x08020000 的 HEX。
4. 检查文件大小、起始地址和 CRC32 后点击“开始升级”。
5. 等待进度到 100%，并看到“Firmware upgrade completed”。

## 构建

使用 Qt 5.14.2 MinGW 64-bit，在 build-release 中运行：

    E:\Qt\Qt5.14.2\5.14.2\mingw73_64\bin\qmake.exe ..\bootloaderQtStepbyStep.pro
    E:\Qt\Qt5.14.2\Tools\mingw730_64\bin\mingw32-make.exe -j4

## 自动化测试

在 build-tests 中运行：

    E:\Qt\Qt5.14.2\5.14.2\mingw73_64\bin\qmake.exe ..\tests\tests.pro
    E:\Qt\Qt5.14.2\Tools\mingw730_64\bin\mingw32-make.exe -j4
    $env:QT_QPA_PLATFORM='offscreen'
    .\release\firmware_core_tests.exe

测试覆盖 HEX 解析、APP1 CRC/跨 USB 包重组、设备身份解析、ACK 驱动进度以及必要界面控件。

## 固定协议参数

- Interface GUID：{6E15414D-B3E8-4B08-9B73-73DB7E6A0F40}
- USB 身份：VID/PID CAFE:4070，Application 与 Bootloader 一致
- 模式查询：`GET_MODE = 0x0002`，响应携带协议版本、模式和能力位
- Bulk OUT / IN：0x01 / 0x81
- Application 地址：0x08020000..0x080DFFFF
- APP1 数据块：最大 240 字节
- 普通请求超时：1 秒；BL_BEGIN 擦除阶段超时：20 秒；同一帧最多重传 3 次
- USB 重枚举等待：10 秒

完整字节协议见 I:\MYFOC\protocal\Ident\README.md 的“Bootloader 固件升级协议”章节。
"# bootloaderStepbyStepQt" 
