# FirmwareInfoV1 固件信息与升级兼容性设计

日期：2026-09-10

## 1. 目标与范围

本设计为以下三个工程定义统一的固件身份、地址布局、查询协议和升级前兼容性校验：

- APP：`I:\MYFOC\YuanziHardwareWithMyFirmware\FOCTEST`
- Bootloader：`I:\MYFOC\bootloader\bootloaderStepbyStep`
- Qt 升级工具：`I:\MYFOC\bootloader\bootloaderQtStepbyStep`

目标如下：

1. HEX 自带版本、厂商、设备名称、产品型号、硬件兼容范围和构建来源。
2. Qt 选择 HEX 后无需连接设备即可读取并显示固件信息。
3. APP 和 Bootloader 通过同一套 APP1 命令返回设备与固件信息。
4. Qt 和 Bootloader 都阻止产品或硬件版本不兼容的固件升级。
5. 固件信息、向量表和 APP 正文使用固定且可验证的地址。
6. 保持现有 APP1 USB 传输模型，升级数据块大小不变。

本阶段不实现数字签名、固件加密、安全防回滚或外部 Flash 暂存。这些功能后续使用发布包 Manifest 承载镜像长度、SHA-256、签名和加密参数。

## 2. 已确认决策

- APP 镜像基址为 `0x08020000`。
- 固件信息位于镜像最前方，向量表后移。
- 仅支持新地址布局，不兼容向量表位于 `0x08020000` 的旧 APP。
- 固件信息使用 256 字节 `FirmwareInfoV1`，随后保留 256 字节扩展区。
- Keil 编译前自动生成固件信息，不在链接后修改 HEX。
- APP 和 Bootloader 均支持 `GET_DEVICE_INFO` 与 `GET_FIRMWARE_INFO`。
- `BL_BEGIN V2` 携带完整 `FirmwareInfoV1`，Bootloader 在擦除前完成兼容性校验。
- Qt 默认阻止版本降级，但允许用户明确勾选后强制降级。
- 正式防回滚留待签名发布包和受保护单调计数器阶段实现。

## 3. Flash 地址布局

| 地址范围 | 大小 | 用途 |
|---|---:|---|
| `0x08000000–0x0801FFFF` | 128 KiB | Bootloader |
| `0x08020000–0x080200FF` | 256 B | `FirmwareInfoV1` |
| `0x08020100–0x080201FF` | 256 B | 保留，擦除态为 `0xFF` |
| `0x08020200–0x080203FF` | 512 B | APP 中断向量表保留区 |
| `0x08020400–0x080DFFFF` | 767 KiB | APP 代码、常量和初始化数据装载区 |
| `0x080E0000–0x080FFFFF` | 128 KiB | Bootloader 升级状态区，保持现有用途 |

APP 可用 Flash 总范围为 `0x08020000–0x080DFFFF`，大小固定为 `0x000C0000`。Keil Target 的 IROM 大小不得继续使用会覆盖状态区的 `0x000E0000`。

向量表固定为 `0x08020200`。该地址满足 Cortex-M4 VTOR 对齐要求，并使固件头与其后预留区合计占用对齐的 512 字节。

## 4. FirmwareInfoV1 二进制格式

所有多字节整数使用小端序。字符串使用 UTF-8，必须在字段范围内包含 `\0`，终止符之后的字节必须为零。兼容性判断只使用数字字段，不使用显示字符串。

| 偏移 | 大小 | 字段 | 规则 |
|---:|---:|---|---|
| `0x00` | 4 | `magic` | ASCII `FWI1`，字节为 `46 57 49 31` |
| `0x04` | 2 | `format_version` | 固定为 1 |
| `0x06` | 2 | `header_size` | 固定为 256 |
| `0x08` | 4 | `product_id` | 初始为 `0x00010001` |
| `0x0C` | 2 | `hw_rev_min` | 初始为 1 |
| `0x0E` | 2 | `hw_rev_max` | 初始为 1，且必须不小于 `hw_rev_min` |
| `0x10` | 2 | `version_major` | 初始为 1 |
| `0x12` | 2 | `version_minor` | 初始为 0 |
| `0x14` | 2 | `version_patch` | 初始为 0 |
| `0x16` | 2 | `flags` | 见下文 |
| `0x18` | 4 | `build_number` | `git rev-list --count HEAD` |
| `0x1C` | 4 | `image_base` | 固定为 `0x08020000` |
| `0x20` | 4 | `vector_base` | 固定为 `0x08020200` |
| `0x24` | 4 | `app_region_size` | 固定为 `0x000C0000` |
| `0x28` | 8 | `build_time_utc` | UTC Unix 时间戳 |
| `0x30` | 32 | `vendor_name` | 初始为 `MYFOC` |
| `0x50` | 32 | `device_name` | 初始为 `MYFOC Motor Controller` |
| `0x70` | 32 | `firmware_name` | 初始为 `FOCTEST Application` |
| `0x90` | 16 | `git_commit` | 12 位 Git 短哈希、`\0` 和补零 |
| `0xA0` | 92 | `reserved` | V1 中必须全部为零 |
| `0xFC` | 4 | `header_crc32` | 前 252 字节的 CRC32 |

`flags` 定义：

- bit 0：`DIRTY`
- bit 1：`DEBUG_BUILD`
- bit 2：`RELEASE_BUILD`
- bit 3–15：保留，必须为零
- `DEBUG_BUILD` 和 `RELEASE_BUILD` 必须且只能设置一个

CRC 使用 CRC-32/ISO-HDLC：反射多项式 `0xEDB88320`，初值 `0xFFFFFFFF`，输入和输出反射，结果异或 `0xFFFFFFFF`。标准检查字符串 `123456789` 的结果必须为 `0xCBF43926`。CRC 覆盖偏移 `0x00–0xFB`，结果以小端序保存于 `0xFC`。

C 端使用自然对齐的定宽整数结构，并通过编译期断言检查总大小和所有关键偏移。Qt 不直接映射 C 结构体，而是按固定偏移解码字节，避免编译器 ABI 差异。

完整镜像长度、SHA-256 和数字签名不放入 `FirmwareInfoV1`，以避免固件头参与自身摘要产生循环依赖。它们属于后续发布包 Manifest。

## 5. 发布配置和编译前生成

APP 仓库保存人工维护的 `firmware_profile.json`，初始内容表达以下配置：

```json
{
  "vendor_name": "MYFOC",
  "device_name": "MYFOC Motor Controller",
  "firmware_name": "FOCTEST Application",
  "product_id": "0x00010001",
  "hardware_revision": 1,
  "hw_rev_min": 1,
  "hw_rev_max": 1,
  "version": {
    "major": 1,
    "minor": 0,
    "patch": 0
  }
}
```

生成器在编译前执行以下步骤：

1. 严格解析配置文件，拒绝未知字段、越界数字、过长或非法 UTF-8 字符串。
2. 检查 `hw_rev_min <= hardware_revision <= hw_rev_max`。
3. 执行 `git rev-list --count HEAD` 获取构建号。
4. 执行 `git rev-parse --short=12 HEAD` 获取提交哈希。
5. 使用 `git status --porcelain` 判断工作区是否 dirty；被 `.gitignore` 忽略的生成文件不影响结果。
6. Debug 构建允许 dirty，并设置 `DIRTY`；Release 构建发现 dirty 时以非零状态退出，阻止 Keil 继续编译。
7. 写入 UTC 构建时间并计算信息头 CRC32。
8. 生成被跟踪源文件包含的、但自身被忽略的初始化数据文件。

Keil 提供 `FOCTEST_Debug` 与 `FOCTEST_Release` 两个目标，分别向生成器传入明确的构建类型。两种目标生成的 AXF 和 HEX 都包含相同的最终固件信息，不采用 HEX 后处理。

Bootloader 在自身配置中保存同一设备身份：`product_id = 0x00010001`、`hardware_revision = 1`。跨工程验证脚本必须比较 APP 发布配置和 Bootloader 设备身份，防止两个仓库中的数字配置发生漂移；Qt 不内置这些产品常量，而是分别读取 HEX 和设备响应。

## 6. APP 链接与运行

APP 使用自定义 Scatter 文件：

- 固件信息对象的专用段固定在 `0x08020000`，大小必须恰好为 256 字节。
- `0x08020100–0x080201FF` 不放置任何段。
- startup 的 `RESET` 段固定在 `0x08020200`。
- 其余只读代码和装载内容从 `0x08020400` 开始。
- RAM 执行区保持现有工程配置，但其 Flash 装载内容不得超过 `0x080DFFFF`。

APP 将向量表设置为：

```c
SCB->VTOR = 0x08020200U;
```

若通过 STM32 系统文件配置，则 `VECT_TAB_OFFSET` 为相对于 `FLASH_BASE` 的 `0x00020200U`。

链接后检查工具解析 Map 和 HEX，并强制验证：固件头、向量表、首个普通 RO 段、镜像上界及状态区均符合本设计。任一不符时构建失败。

Keil 直接加载 APP AXF 时仍可设置断点并单步调试。正常上电复位时，持久化状态为 RECEIVING 或 ABORTED 会阻止启动；没有升级状态记录时，Bootloader 允许在新固件头和向量全部有效后启动，从而支持 Keil 直接下载 APP 后复位调试。

## 7. Bootloader 启动验证

Bootloader 区分以下常量：

- `BL_APPLICATION_BASE = 0x08020000`
- `BL_APPLICATION_VECTOR_BASE = 0x08020200`
- `BL_APPLICATION_SIZE = 0x000C0000`

启动 APP 前依次验证：

1. 若存在持久化升级状态，其状态不得为 RECEIVING 或 ABORTED；没有状态记录时继续执行其余验证。
2. `FirmwareInfoV1` 魔数、版本、大小、保留字段和 flags 合法。
3. 信息头 CRC32 正确。
4. `product_id` 等于本机 `0x00010001`。
5. 本机 `hardware_revision = 1` 落在固件声明的范围内。
6. 镜像基址、向量表地址和 APP 区域大小完全匹配固定布局。
7. 从 `0x08020200` 读取初始 MSP，从 `0x08020204` 读取 Reset_Handler。
8. 初始 MSP 为 8 字节对齐，并满足 `0x20000000 < MSP <= 0x20020000`，或在明确启用 CCM 栈时满足 `0x10000000 < MSP <= 0x10010000`；允许初始栈顶等于 RAM 区域的上边界。
9. Reset_Handler 的 Thumb 位为 1，清除 Thumb 位后的地址位于 `0x08020400–0x080DFFFF`。

任何验证失败都停留在 Bootloader，并记录可查询的具体原因。跳转前沿用现有安全清理流程：禁止中断、停止 SysTick、复位已使用外设、清除待处理中断，设置 VTOR 和 MSP 后调用 Reset_Handler。

## 8. 设备与固件信息查询协议

保留现有：

- `0x0001 ENTER_BOOTLOADER`
- `0x0002 GET_MODE`
- `0x0200–0x0206` 现有 Bootloader 升级命令

新增：

### 8.1 `0x0003 GET_DEVICE_INFO`

请求载荷为空。成功响应为 28 字节 `DeviceInfoV1`：

| 偏移 | 大小 | 字段 |
|---:|---:|---|
| `0x00` | 2 | 协议版本，固定为 1 |
| `0x02` | 1 | 当前模式：1=Application，2=Bootloader |
| `0x03` | 1 | 保留，必须为零 |
| `0x04` | 4 | `product_id` |
| `0x08` | 2 | 实际 `hardware_revision` |
| `0x0A` | 2 | 保留，必须为零 |
| `0x0C` | 4 | 能力位 |
| `0x10` | 12 | STM32 96 位唯一 ID |

唯一 ID 依次读取 `UID_BASE + 0`、`UID_BASE + 4` 和 `UID_BASE + 8` 三个 32 位寄存器，每个寄存器按小端序写入响应，从而得到稳定的 12 字节序列。

能力位新增：

- bit 3：支持 `GET_DEVICE_INFO`
- bit 4：支持 `GET_FIRMWARE_INFO`
- 现有 bit 0–2 含义保持不变

### 8.2 `0x0004 GET_FIRMWARE_INFO`

请求载荷为空。成功响应直接返回 256 字节 `FirmwareInfoV1`：

- APP 模式返回当前运行 APP 的固件头。
- Bootloader 模式返回内部 Flash 中已安装 APP 的固件头。
- 固件头无效时返回 APP1 错误响应及明确的 `FIRMWARE_INFO_INVALID` 详细码。

APP 的 CDC 命令行同时支持文本命令 `GET_DEVICE_INFO` 和 `GET_FIRMWARE_INFO`。输出使用稳定的单行 `key=value` 格式供人工阅读；数据必须由同一二进制结构格式化，不允许维护第二份版本常量。

## 9. BL_BEGIN V2

APP1 最大载荷由 256 字节提高到 320 字节。`BL_DATA` 的数据部分仍保持 246 字节，现有 USB 分片与重组方式不变。

`BL_BEGIN V2` 请求载荷为 272 字节：

| 偏移 | 大小 | 字段 |
|---:|---:|---|
| `0x00` | 2 | `begin_schema = 2` |
| `0x02` | 2 | `info_length = 256` |
| `0x04` | 4 | `image_base = 0x08020000` |
| `0x08` | 4 | `image_size` |
| `0x0C` | 4 | 完整稠密镜像 CRC32 |
| `0x10` | 256 | `FirmwareInfoV1` |

Qt 将 HEX 从最低到最高有效地址转换为稠密镜像，地址洞填充 `0xFF`。`image_size` 和镜像 CRC32 均针对该稠密镜像。

Bootloader 收到 BEGIN 后，在擦除前校验固件头、产品 ID、硬件版本、固定地址和镜像边界。通过后才执行：

1. 将升级状态设为无效/进行中。
2. 擦除 APP 所需扇区。
3. 将 BEGIN 携带的 256 字节固件头写入 `0x08020000`。
4. 将期望的下一数据偏移设置为 256。
5. 接收 Qt 从偏移 256 开始发送的剩余镜像数据。
6. `BL_END` 对 Flash 中从 `0x08020000` 开始的完整 `image_size` 重新计算 CRC32。
7. 再次验证固件头与 `0x08020200` 向量表。
8. 全部成功后才写入 APP 有效状态并复位。

失败详细码至少区分：信息头无效、产品 ID 不匹配、硬件版本不兼容、镜像基址错误、向量表地址错误、镜像越界、Flash 操作失败和最终 CRC 不匹配。

## 10. Qt 工作流

选择 HEX 后，Qt 必须在不连接设备的情况下：

1. 严格解析 Intel HEX。
2. 提取 `0x08020000` 的 256 字节固件头。
3. 校验格式、字段、UTF-8、保留字节和 CRC32。
4. 校验 `0x08020200` 的 MSP 与 Reset_Handler。
5. 显示厂商、设备、固件名称、产品 ID、硬件兼容范围、SemVer、构建号、Git 哈希、构建类型、dirty 状态、时间和固定地址。
6. 计算并显示稠密镜像大小与 CRC32。

连接设备后：

1. 发送 `GET_MODE`。
2. 发送 `GET_DEVICE_INFO`。
3. 发送 `GET_FIRMWARE_INFO` 读取当前已安装版本；无有效 APP 时允许继续显示 Bootloader 状态。
4. 强制检查候选 `product_id == device.product_id`。
5. 强制检查 `hw_rev_min <= device.hardware_revision <= hw_rev_max`。
6. 比较候选与已安装固件的 `major.minor.patch`。

版本策略：

- 候选 SemVer 较高：允许。
- SemVer 相同但 Git 哈希不同：允许，并提示同版本重新构建。
- 候选 SemVer 较低：默认禁用升级；只有用户明确勾选“允许固件降级”后才允许。
- `build_number` 不参与版本高低判断，因为不同 Git 分支的提交总数不保证全局单调。

Debug 或 dirty 候选固件允许用于开发升级，但界面必须显示醒目警告。Release 构建理论上不会产生 dirty HEX；如果输入文件出现 `RELEASE_BUILD + DIRTY` 这一非法组合，Qt 直接拒绝加载。后续正式加密发布包的生成入口只接受 clean Release 固件。

满足所有条件后，Qt 进入 Bootloader，发送 `BL_BEGIN V2`，再从镜像偏移 256 开始发送数据。升级重启并重新枚举后，再读取固件信息，确认 SemVer、构建号和 Git 哈希与所选 HEX 一致。

## 11. 错误处理

- HEX 缺少固件头、固件头跨越未定义地址、CRC 错误、非法 UTF-8、字段未终止、保留字段非零或固定地址不符：拒绝加载。
- 向量 MSP、Reset_Handler 或 Thumb 位无效：拒绝加载或启动。
- 设备协议过旧、不支持新查询能力：禁止使用新升级流程。
- 产品或硬件不兼容：Qt 禁止升级，Bootloader 在擦除前再次拒绝。
- 传输断开、超时或用户取消：保持 APP 无效状态，重启后停留在 Bootloader。
- 最终镜像 CRC 或二次固件头校验失败：不得写入 APP 有效状态。
- 升级后版本确认不一致：Qt 报告升级后确认失败，不显示成功。

错误日志同时显示稳定错误码和中文解释，便于自动测试与现场诊断。

## 12. 验证与测试

### 12.1 生成器

- 固定输入产生逐字节确定的 256 字节头。
- CRC-32 标准检查值和头部黄金向量。
- 字符串边界、UTF-8、版本范围和未知 JSON 字段的失败测试。
- Debug clean、Debug dirty、Release clean、Release dirty 四种 Git 状态测试。

### 12.2 APP

- 结构大小及关键 `offsetof` 编译期断言。
- APP1 两条新命令的请求长度、响应长度和内容测试。
- CDC 输出来自同一数据源的测试。
- Map/HEX 固定地址检查。

### 12.3 Bootloader

- 启动验证的每个失败分支测试。
- `GET_DEVICE_INFO` 与 `GET_FIRMWARE_INFO` 测试。
- `BL_BEGIN V2` 在任何擦除前拒绝错误产品、硬件和地址的测试。
- BEGIN 写入固件头、DATA 从偏移 256 开始及最终完整 CRC 测试。
- 中断传输保持 APP 无效、完整传输才置有效的测试。

### 12.4 Qt

- 合法及损坏 HEX 的固件头解析测试。
- 产品、硬件、升级、同版本重刷和降级策略测试。
- APP1 320 字节载荷跨 USB 包重组测试。
- BEGIN V2 编码及偏移 256 数据发送测试。
- 升级后版本回读一致与不一致测试。

### 12.5 完成判据

1. 三个工程的自动化测试全部通过。
2. APP 与 Bootloader Keil 全量构建无新增警告或错误。
3. Map 和 HEX 检查证明地址布局完全符合第 3 节。
4. Qt 全量构建和测试通过。
5. CMSIS-DAP 下 APP 仍可断点调试。
6. 实机完成：APP 信息查询、进入 Bootloader、升级、复位跳转、版本回读确认。

## 13. 后续安全扩展边界

后续加密发布包仍把 `FirmwareInfoV1` 作为被保护镜像的前 256 字节，并将其纳入加密与签名覆盖范围。Manifest 保存完整镜像长度、SHA-256、AES-GCM 参数、被包装的固件密钥以及 ECDSA P-256 签名。真正的防回滚需要签名保护的版本策略和设备侧受保护单调状态，不能用当前 Qt 勾选框替代。
