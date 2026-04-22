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
- 边界注意：当前 `initRTL8125()` 的 fallback 分支没有再次校验 `fallbackMAC` 本身是否有效。

## 3. 当前代码是否读取 OpenCore `DeviceProperties`

结论：**当前代码没有读取 OC 注入到 `IOPCIDevice` 节点的属性。**

- 现有实现读取的是驱动服务自身属性：`getProperty("Driver Parameters")`。
- 未见 `pciDevice->getProperty(...)` 这类从 provider（`IOPCIDevice`）读取注入属性的逻辑。
- 因此按现状，`DeviceProperties -> Add -> PciRoot(...)/Pci(...)` 的自定义键默认不会被驱动消费。

## 4. 读取 OC 注入属性的正确方式

根据 OpenCore 文档与社区实践，`DeviceProperties` 会进入 IORegistry，驱动应从对应设备节点读取：

- 在驱动中优先从 `IOPCIDevice`（provider）读取，例如 `pciDevice->getProperty("fallbackMAC")`。
- 读不到时再回退到 `Info.plist` 的 `Driver Parameters`（兼容历史配置）。
- 建议兼容多类型：`OSString` / `OSData` / `OSNumber`，避免因注入类型不一致导致读取失败。

推荐优先级：

1. `pciDevice->getProperty("...")`（OpenCore 注入）
2. `Driver Parameters`（Info.plist）
3. 默认值

## 5. 是否可用 `DeviceProperties Delete` 直接改 `IOMACAddress`

结论：**不建议把它当作稳定方案，通常不能保证链路层 MAC 最终生效。**

- `DeviceProperties Add/Delete` 修改的是指定设备节点属性，不等同于驱动最终采用该值写入硬件。
- 本驱动会在初始化阶段按自身流程读硬件并调用 `rtl8125_rar_set()`，随后由 `currMacAddr` 对外提供地址。
- 即便某节点里能看到 `IOMACAddress` 变化，也可能被驱动初始化流程覆盖，出现“属性变了但实际 MAC 未变”。

## 6. 推荐落地方案

- 最稳妥方案：在驱动中新增或复用注入键（如 `overrideMAC` / `fallbackMAC`），显式读取 `pciDevice` 属性并执行 `rtl8125_rar_set()`。
- 兼容要求：保留 `Info.plist` 参数回退路径，避免破坏现有用户配置。
- 如需强制固定 MAC：使用“注入优先于硬件值”的逻辑，而不是依赖“硬件值非法才 fallback”。

## 7. 编译环境建议（本工程）

结论：优先使用 **Xcode 15.2**（以及对应 Command Line Tools 15.2）。

依据（来自工程配置）：

- `LastUpgradeCheck = 1520`（对应 Xcode 15.2）。
- `CreatedOnToolsVersion = 11.4`，说明工程起源较早，但后续升级到较新工具链。
- `compatibilityVersion = "Xcode 12.0"`，表示可向下兼容，不代表当前最稳编译版本。
- 目标类型为传统 `kext`（`com.apple.product-type.kernel-extension`），对工具链变化更敏感，建议优先使用最后一次升级对应版本。

建议组合：

1. 首选：Xcode 15.2 + CLT 15.2
2. 可尝试：Xcode 15.3 / 15.4
3. 不优先：Xcode 16.x（kext 项目潜在兼容差异更大）

## 8. 环境验证步骤

在终端执行以下命令确认当前工具链：

```bash
xcodebuild -version
xcode-select -p
clang --version
pkgutil --pkg-info=com.apple.pkg.CLTools_Executables
```

期望结果（目标环境）：

- `xcodebuild -version` 显示 `Xcode 15.2`（Build version 通常为 `15C500b`）。
- `xcode-select -p` 指向你要使用的 Xcode（如 `/Applications/Xcode_15.2.app/Contents/Developer`）。
- `pkgutil --pkg-info=com.apple.pkg.CLTools_Executables` 版本为 15.2 对应范围。

可选工程验证（仅编译，不签名安装）：

```bash
cd /Users/tzraeq/workspace/kext/LucyRTL8125Ethernet
xcodebuild -project LucyRTL8125Ethernet.xcodeproj -target LucyRTL8125Ethernet -configuration Release CODE_SIGNING_ALLOWED=NO build
```

## 9. 安装指定版本教程（Xcode 15.2 + CLT 15.2）

### 9.1 安装 Xcode 15.2

推荐方式（官方）：

1. 登录 [Apple Developer Downloads](https://developer.apple.com/download/all/)。
2. 搜索并下载 `Xcode 15.2.xip`。
3. 解压后将应用重命名为 `Xcode_15.2.app` 并放到 `/Applications`。

### 9.2 切换到指定 Xcode

```bash
sudo xcode-select -s /Applications/Xcode_15.2.app/Contents/Developer
sudo xcodebuild -license accept
```

再次验证：

```bash
xcodebuild -version
xcode-select -p
```

### 9.3 安装匹配的 Command Line Tools

方式 A（推荐，随 Xcode 组件匹配）：

- 打开 `Xcode_15.2` 一次，等待必要组件安装完成。

方式 B（手动）：

1. 在 Apple Developer Downloads 下载对应版本 `Command Line Tools for Xcode 15.2` 的 `.dmg`。
2. 双击安装后执行验证命令：

```bash
pkgutil --pkg-info=com.apple.pkg.CLTools_Executables
```

### 9.4 常见问题排查

- 若 `xcodebuild` 仍显示旧版本：重新执行 `sudo xcode-select -s ...`。
- 若提示许可协议未接受：执行 `sudo xcodebuild -license accept`。
- 若编译仍调用旧工具链：重启终端后再执行一次 `xcodebuild -version` 与工程编译命令确认。
