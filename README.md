# MYFOC 固件升级上位机

这是 STM32F407IGT6 的 Qt 5.14.2 / WinUSB 固件升级工具。它读取 Intel HEX 内的固件身份，连接设备后再次读取设备身份与已安装固件，在任何擦除操作之前完成兼容性判断。

## 操作流程

1. 点击“刷新设备”。工具枚举统一 Interface GUID，并发送 `GET_MODE`，不依赖 PID 判断 Application/Bootloader。
2. 选择 `.hex`。工具严格校验 Intel HEX、完整 256 字节 `FirmwareInfoV1`、CRC32、UTF-8/补零、固定地址和位于 `0x08020200` 的向量表。
3. 查看候选固件：厂商、设备/固件名、产品 ID、硬件范围、SemVer、build、Git、Debug/Release、dirty、UTC 时间、镜像/向量地址、镜像大小和 CRC32。
4. 点击“开始升级”。工具依次发送 `GET_MODE → GET_DEVICE_INFO → GET_FIRMWARE_INFO`。
5. 工具强制核对产品 ID 与硬件版本；SemVer 较低时默认停止，并显示“允许固件降级（仅本次升级）”。勾选后再次点击开始才能降级。
6. Application 模式先发送 `ENTER_BOOTLOADER`，按同一 STM32 UID 等待 USB 重枚举；Bootloader 模式直接继续。
7. 工具发送 `BL_HELLO / BL_STATUS / BL_BEGIN V2`，从偏移 256 开始逐块传输；固件头由 BEGIN 携带并由 Bootloader 先校验、后擦除。
8. `BL_END` 成功后等待 Application，重新查询固件信息；只有 SemVer、build number 和 Git commit 与 HEX 完全一致才显示成功。

Debug 或 dirty 固件可用于开发，但界面会显示警告。`Release + dirty` 是非法固件头，加载时直接拒绝。当前 CRC32 仅保证传输完整性，不提供来源认证；正式发布加密/签名包是后续独立阶段。

## 固定参数

- USB：VID/PID `CAFE:4070`，Application 与 Bootloader 相同。
- Interface GUID：`{6E15414D-B3E8-4B08-9B73-73DB7E6A0F40}`。
- WinUSB Interface 2：Bulk OUT `0x01`，Bulk IN `0x81`。
- APP1：24 字节头，最大 payload 320 字节。
- Application：`0x08020000..0x080DFFFF`。
- 固件头：`0x08020000..0x080200FF`。
- 保留擦除区：`0x08020100..0x080201FF`。
- 向量表：`0x08020200`；普通代码不得低于 `0x08020400`。
- `BL_DATA` 数据最多 240 字节；BEGIN V2 固定 272 字节。
- 普通请求超时 1 秒；BEGIN 20 秒；每帧最多重传 3 次；重枚举等待 10 秒。

## 构建与测试

Qt 与 MinGW 必须配套使用：

```powershell
$env:Path='E:\Qt\Qt5.14.2\Tools\mingw730_64\bin;'+$env:Path
cd build-release
E:\Qt\Qt5.14.2\5.14.2\mingw73_64\bin\qmake.exe ..\bootloaderQtStepbyStep.pro
mingw32-make -j4
```

自动化测试：

```powershell
cd build-tests
E:\Qt\Qt5.14.2\5.14.2\mingw73_64\bin\qmake.exe ..\tests\tests.pro
mingw32-make -j4
$env:QT_QPA_PLATFORM='offscreen'
.\release\firmware_core_tests.exe
```

格式编解码独立测试使用 `tests\firmwareinfo_tests.pro`。实机无人值守冒烟工具使用 `tests\hardware_upgrade_smoke.pro`，参数必须是准备安装的真实 HEX；该程序会实际升级硬件，不能用于损坏样本。

发布程序位于 `deploy\MYFOCFirmwareUpdater.exe`。完整字节协议见 `I:\MYFOC\protocal\Ident\README.md` 第 17 节。
