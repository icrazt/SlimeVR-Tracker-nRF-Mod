# E73 Tracker Dev J-Link 调试流程

`e73_tracker_dev` 是给 E73 Tracker 日常开发调试用的板级配置。它假设开发板处于无 UF2 bootloader 的状态，固件从 flash `0x0` 直接启动，方便 nRF Connect for VSCode 通过 J-Link 做 `Flash`、`Debug` 和 `Attach`。

## 适用场景

- 你一直连接着 J-Link，主要做断点、单步、RTT 日志和快速迭代。
- 你不想在日常调试时反复生成或写入 Adafruit bootloader settings page。
- 你当前不需要验证 UF2 拖拽升级、OTA 或带 SoftDevice 的发布布局。

不适用场景：

- 要给已有 Adafruit UF2 bootloader 的板子更新用户固件。
- 要产出 `.uf2` 或 release hex。
- 要验证 `SoftDevice + app at 0x27000` 的发布布局。

这些场景继续使用 `e73_tracker_uf2/nrf52840`。

## 配置差异

`e73_tracker_dev/nrf52840` 和 `e73_tracker_uf2/nrf52840` 使用同一套 E73 硬件引脚定义思路，但启动布局不同：

| 配置 | 用途 | App 起始地址 | UF2 输出 | SoftDevice |
| --- | --- | --- | --- | --- |
| `e73_tracker_dev/nrf52840` | J-Link 日常调试 | `0x0` | 关闭 | 关闭 |
| `e73_tracker_uf2/nrf52840` | UF2/release/已有 bootloader | `0x27000` | 开启 | 按构建参数开启 |

`e73_tracker_dev` 只注册 J-Link runner，避免日常调试时被其它 runner 路径干扰。

## 首次切换到 dev 布局

如果开发板之前烧过 Adafruit UF2 bootloader，首次切换到 `e73_tracker_dev` 前建议做一次 recover 或整片擦除。这样 reset 后不会再先进 UF2 bootloader。

```powershell
nrfutil device recover
```

旧工具链也可以用：

```powershell
nrfjprog --recover
```

注意：recover 会清空 flash，包括 bootloader、NVS/settings 和配对/校准数据。这个动作只建议在切换到无 bootloader 开发布局时做一次；日常迭代不要反复 recover。

## nRF Connect for VSCode 使用

在 nRF Connect 侧栏添加新的 Build Configuration：

- Board target: `e73_tracker_dev/nrf52840`
- Board root: 仓库根目录
- Build directory: 建议使用独立目录，例如 `build_e73_dev`
- Extra CMake arguments: 通常只需要 `-DBOARD_ROOT=.`

创建后，日常使用：

- `Build`: 编译当前 dev 固件。
- `Flash`: 通过 J-Link 烧录 app。
- `Debug`: 烧录并启动调试会话。
- `Attach`: 只附加到已经在运行的固件，不重新烧录。
- `RTT Terminal`: 查看 RTT 日志。

## 命令行等价流程

从仓库根目录构建：

```powershell
west build -d build_e73_dev -p always -b e73_tracker_dev/nrf52840 -- -DBOARD_ROOT=.
```

烧录：

```powershell
west flash -d build_e73_dev
```

启动调试：

```powershell
west debug -d build_e73_dev
```

只附加，不重新烧录：

```powershell
west attach -d build_e73_dev
```

## 日常擦写建议

首次切换布局时可以 recover。之后日常调试尽量直接用 `Flash` / `Debug`，不要把 `--erase` 固化进 board runner 参数。

需要清掉 NVS/settings 时，再显式做一次整片擦除或 recover。这样能避免每次调试都把校准、配对、bootloader 恢复状态等数据清掉。

## 回到 UF2/release 布局

`e73_tracker_dev` 不产出 `.uf2`，也不保留 bootloader。要回到可 UF2 更新的布局时，使用 `e73_tracker_uf2/nrf52840` 构建：

```powershell
west build -d build_e73_uf2 -p always -b e73_tracker_uf2/nrf52840 -- -DBOARD_ROOT=. -DWITH_SOFTDEVICE=ON
```

如果板上已经没有 UF2 bootloader，需要用 factory 镜像或单独重烧 bootloader。已有 bootloader 且只想用 J-Link 更新应用时，应烧包含 bootloader metadata 的 SWD-ready 镜像，而不是普通 `zephyr_sd.hex`。



---

已加好 `e73_tracker_dev`，专门给无 bootloader 的 J-Link 调试流用。

主要改动：

- 新增板级配置目录：[boards/crazt/e73_tracker_dev](e:/smol/workspace/jiting/SlimeVR-Tracker-nRF-Mod/boards/crazt/e73_tracker_dev)
  - `e73_tracker_dev/nrf52840`
  - app 从 `0x0` 启动
  - 不启用 `CONFIG_BUILD_OUTPUT_UF2`
  - 不走 SoftDevice
  - `board.cmake` 只注册 J-Link runner，日常 `west flash/debug/attach` 更直接
- 新增 CMake 保护：[CMakeLists.txt](e:/smol/workspace/jiting/SlimeVR-Tracker-nRF-Mod/CMakeLists.txt:109)
  - 如果对 `e73_tracker_dev` 传 `-DWITH_SOFTDEVICE=ON`，会直接报错，避免生成危险的混合布局镜像。
- 新增日常使用文档：[doc/e73_tracker_dev_jlink_debug.md](e:/smol/workspace/jiting/SlimeVR-Tracker-nRF-Mod/doc/e73_tracker_dev_jlink_debug.md)
  - 写了 nRF Connect for VSCode Build Configuration 配置方式、命令行等价流程、首次从 UF2 bootloader 切换到 dev 布局时的 recover 注意事项，以及回到 release/UF2 布局时该怎么处理。

验证情况：`git diff --check` 通过；我也尝试用 `west boards` 做板发现检查，但当前 shell 环境里没有 `west` 命令，所以没有做实际 build/board discovery 验证。

你现在日常可以用这个 target：

```powershell
west build -d build_e73_dev -p always -b e73_tracker_dev/nrf52840 -- -DBOARD_ROOT=.
west flash -d build_e73_dev
west debug -d build_e73_dev
```

nRF Connect for VSCode 里对应就是新建 Build Configuration，Board target 选 `e73_tracker_dev/nrf52840`，Extra CMake arguments 填 `-DBOARD_ROOT=.`。

后续比较值得继续做的三件事：

1. 给 release 路径显式产出 `_SWD.hex`，避免误拿普通 `zephyr_sd.hex` 烧已有 bootloader 的板。
2. 需要调 SoftDevice 时，再单独加一个 `e73_tracker_dev_sd`，不要混进当前这个纯 dev target。
3. 你本机有 west 环境时跑一次 `west build`，确认 board discovery 和 DTS/Kconfig 都完整通过。