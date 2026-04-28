# MAC 地址与 OpenCore 注入结论

## 1. 当前驱动的 MAC 读取与生效路径

- 驱动启动时在 `start()` 调用 `initRTL8125()` 完成 MAC 初始化。
- `initRTL8125()` 会先从硬件寄存器 `MAC0..MAC5` 读取 MAC；部分芯片配置会优先使用 `BACKUP_ADDR0_8125/BACKUP_ADDR1_8125`。
- 读取后若地址合法，直接写入硬件接收地址寄存器（`rtl8125_rar_set()` -> `MAC0/MAC4`）。
- 若读取结果不合法，当前代码分支会使用 `fallbackMAC`。
- 对外上报 MAC 由 `getHardwareAddress()` 返回 `currMacAddr`；运行时可通过 `setHardwareAddress()` 改写 `currMacAddr` 并调用 `rtl8125_rar_set()`。

## 2. `fallbackMAC` 的真实行为

- `fallbackMAC` 来源于 `Info.plist` 的 `Driver Parameters` 字典（当前代码路径）。
- 判定“合法 MAC”使用 `is_valid_ether_addr()`：
  - 非全 0；
  - 非组播地址（首字节最低位不能是 1）；
  - `FF:FF:FF:FF:FF:FF` 也会因组播规则被判无效。
- 关键点：`fallbackMAC` 仅在硬件读到的 MAC 不合法时才参与当前分支；默认不是“强制覆盖合法原始 MAC”。
- 关键点：当前代码**会再次校验** `fallbackMAC`（对应 `fallBackMacAddr`）是否有效；只有在 `fallbackMAC` 也合法时才会采用，否则继续尝试 `BACKUP_ADDR`，最后生成随机 MAC。

## 3. 当前代码是否读取 OpenCore `DeviceProperties`

结论：**当前代码没有读取 OC 注入到 `IOPCIDevice` 节点的属性。**

- 现有实现读取的是驱动服务自身属性：`getProperty("Driver Parameters")`。
- 未见 `pciDevice->getProperty(...)` 这类从 provider（`IOPCIDevice`）读取注入属性的逻辑。
- 因此按现状，`DeviceProperties -> Add -> PciRoot(...)/Pci(...)` 的自定义键默认不会被驱动消费。

## 4. 读取 OC 注入属性的正确方式

根据 OpenCore 文档与社区实践，`DeviceProperties` 会进入 IORegistry，驱动应从对应设备节点读取：

- 在驱动中优先从 `IOPCIDevice`（provider）读取，例如 `pciDevice->getProperty("mac-address")`。
- 读不到时再回退到 `Info.plist` 的 `Driver Parameters`（兼容历史配置）。
- 为保持与现有 `fallbackMAC` 行为一致，`mac-address` **仅支持 `OSString`**，格式为 `xx:xx:xx:xx:xx:xx`（十六进制，不区分大小写）。不支持 `OSData` / `OSNumber`。

推荐优先级：

1. `pciDevice->getProperty("mac-address")`（OpenCore 注入，强制覆盖）
2. 硬件 MAC（若合法）
3. `Driver Parameters -> fallbackMAC`（若合法）
4. `BACKUP_ADDR0_8125/BACKUP_ADDR1_8125`（若合法）
5. 随机 MAC（本地管理地址）

## 5. 是否可用 `DeviceProperties Delete` 直接改 `IOMACAddress`

结论：**不采用该方案**（不建议把它当作稳定方案，通常不能保证链路层 MAC 最终生效）。

- `DeviceProperties Add/Delete` 修改的是指定设备节点属性，不等同于驱动最终采用该值写入硬件。
- 本驱动会在初始化阶段按自身流程读硬件并调用 `rtl8125_rar_set()`，随后由 `currMacAddr` 对外提供地址。
- 即便某节点里能看到 `IOMACAddress` 变化，也可能被驱动初始化流程覆盖，出现“属性变了但实际 MAC 未变”。

## 6. 推荐落地方案

- 最稳妥方案：在驱动中新增注入键 `mac-address`（`OSString`），显式读取 `pciDevice` 属性并执行 `rtl8125_rar_set()`。
- 兼容要求：保留 `Info.plist` 的 `Driver Parameters -> fallbackMAC` 回退路径，避免破坏现有用户配置。
- 强制固定 MAC：`mac-address` 采用“注入优先于硬件值”的逻辑；`fallbackMAC` 保持原本语义（仅在硬件 MAC 不合法时参与）。

## 7. 构建流程与环境约束（统一说明）

本节用于统一回答“在不同主机系统上，如何按工程预期构建并保持兼容范围”。

### 7.1 工程约束（决定兼容范围的核心）

- target 构建设置中，`MACOSX_DEPLOYMENT_TARGET = 10.15`，这决定了当前工程的最低目标版本语义为 `macOS 10.15+`。
- `SDKROOT = macosx`，表示使用当前所选 Xcode 自带的 macOS SDK。
- 工程使用 `MacKernelSDK/Headers` 与对应 `libkmod.a`，用于保持旧内核头与构建兼容能力。
- 因此：主机系统版本不会直接改变“最低兼容目标”，除非你修改 deployment target 或引入超出目标版本的符号。

### 7.2 主机系统与 Xcode 约束

- 在 macOS 15（Sequoia）主机上，应使用该系统可正常运行的 Xcode 版本（通常为 Xcode 16.x）。
- Xcode 15.2 属于 macOS 14 时代工具链，其内置 macOS SDK 最高为 14.2 SDK。
- 对本工程而言，若你在 macOS 15 上构建，使用 Xcode 16.x 是正常组合；兼容边界仍由工程 deployment target 控制。

### 7.3 标准构建步骤（建议固定）

1. 选择并切换目标 Xcode：

```bash
sudo xcode-select -s /Applications/Xcode.app/Contents/Developer
sudo xcodebuild -license accept
```

2. 验证当前工具链：

```bash
xcodebuild -version
xcode-select -p
clang --version
```

3. 使用 Release 配置构建（仅编译，不处理安装）：

```bash
cd /Users/tzraeq/workspace/kext/LucyRTL8125Ethernet
xcodebuild -project LucyRTL8125Ethernet.xcodeproj -target LucyRTL8125Ethernet -configuration Release -sdk macosx CODE_SIGNING_ALLOWED=NO build
```

### 7.4 产物一致性校验（必须做）

- 优先以构建设置校验目标版本（对本工程最可靠）：

```bash
xcodebuild -project LucyRTL8125Ethernet.xcodeproj -target LucyRTL8125Ethernet -configuration Release -showBuildSettings | rg "^\s*(MACOSX_DEPLOYMENT_TARGET|SDKROOT|ARCHS|KERNEL_EXTENSION_HEADER_SEARCH_PATHS|KERNEL_FRAMEWORK_HEADERS|WRAPPER_EXTENSION)\s*="
```

- 校验架构（当前工程为 `x86_64`）：

```bash
lipo -info build/Release/LucyRTL8125Ethernet.kext/Contents/MacOS/LucyRTL8125Ethernet
```

### 7.5 排查原则

- 若 `xcodebuild -version` 与预期不一致，先修正 `xcode-select`。
- 若命令行工具版本混乱，优先保证 Xcode 与 CLT 来自同一版本体系。
- 若产物校验不满足预期，先检查是否误改了 `MACOSX_DEPLOYMENT_TARGET`、`SDKROOT`、`ARCHS`。

### 7.6 环境搭建步骤（安装到可用）

1. 按主机系统选择可用 Xcode 主版本：
   - macOS 14：可用 Xcode 15.2/15.4（15.2 对应 14.2 SDK）。
   - macOS 15：优先 Xcode 16.x。

2. 下载并安装 Xcode：
   - 从 [Apple Developer Downloads](https://developer.apple.com/download/all/) 下载对应 `.xip`。
   - 解压后放到 `/Applications`（可命名为 `Xcode_16.x.app` 或 `Xcode_15.2.app` 便于并存）。

3. 切换命令行开发目录并接受许可：

```bash
sudo xcode-select -s /Applications/Xcode.app/Contents/Developer
sudo xcodebuild -license accept
```

4. 安装或对齐 Command Line Tools（CLT）：
   - 方式 A（推荐）：首次打开目标 Xcode，等待组件安装完成。
   - 方式 B（手动）：安装对应版本 `Command Line Tools for Xcode` 安装包。

5. 最终验证（必须通过）：

```bash
xcodebuild -version
xcode-select -p
pkgutil --pkg-info=com.apple.pkg.CLTools_Executables
```

6. 进入“标准构建步骤”执行编译与产物校验（见 7.3 与 7.4）。
