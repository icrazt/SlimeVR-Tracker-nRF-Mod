# E73 Tracker Dev J-Link 调试流程

`e73_tracker/nrf52840/dev` 是 `e73_tracker` 板级配置的 **dev 变体**，给 E73 Tracker 日常开发调试用。它假设开发板处于无 UF2 bootloader 的状态，固件从 flash `0x0` 直接启动，方便 nRF Connect for VSCode 通过 J-Link 做 `Flash`、`Debug` 和 `Attach`。

## 适用场景

- 你一直连接着 J-Link，主要做断点、单步、RTT 日志和快速迭代。
- 你不想在日常调试时反复生成或写入 Adafruit bootloader settings page。
- 你当前不需要验证 UF2 拖拽升级、OTA 或带 SoftDevice 的发布布局。

不适用场景：

- 要给已有 Adafruit UF2 bootloader 的板子更新用户固件。
- 要产出 `.uf2` 或 release hex。
- 要验证 `SoftDevice + app at 0x27000` 的发布布局。

这些场景继续使用 `e73_tracker/nrf52840/uf2`。

## 配置差异

`e73_tracker/nrf52840/dev` 和 `e73_tracker/nrf52840/uf2` 共用同一套 E73 板级目录（`boards/crazt/e73_tracker/`），硬件引脚定义完全相同（来自 `e73_tracker_common.dtsi`），只是启动布局不同：

| 配置 | 用途 | App 起始地址 | UF2 输出 | SoftDevice |
| --- | --- | --- | --- | --- |
| `e73_tracker/nrf52840/dev` | J-Link 日常调试 | `0x0` | 关闭 | 关闭 |
| `e73_tracker/nrf52840/uf2` | UF2/release/已有 bootloader | `0x27000` | 开启 | 按构建参数开启 |

两个变体共享 `board.cmake` 注册的 runner 集合（J-Link + pyocd + nrfutil），日常用哪个由命令行或 IDE 选择决定。

## 首次切换到 dev 布局

如果开发板之前烧过 Adafruit UF2 bootloader，首次切换到 `e73_tracker/nrf52840/dev` 前建议做一次 recover 或整片擦除。这样 reset 后不会再先进 UF2 bootloader。

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

- Board target: `e73_tracker/nrf52840/dev`
- Board root: 仓库根目录
- Build directory: 建议使用独立目录，例如 `build_e73_dev`
- Extra CMake arguments: 通常只需要 `-DBOARD_ROOT=.`

创建后，日常使用：

- `Build`: 编译当前 dev 固件。
- `Flash`: 通过 J-Link 烧录 app（可用 `--runner jlink` 明确指定）。
- `Debug`: 烧录并启动调试会话。
- `Attach`: 只附加到已经在运行的固件，不重新烧录。
- `RTT Terminal`: 查看 RTT 日志。

### 固化 RTT 芯片选择

仓库里的 [board.cmake](../boards/crazt/e73_tracker/board.cmake) 已经给 Zephyr/west 的 J-Link runner 固化了 `--device=nrf52840_xxaa` 和 `--speed=4000`，所以 `west flash`、`west debug`、`west attach` 这条链路不需要手动选择芯片。

nRF Terminal 或独立 SEGGER RTT Viewer 属于另一条链路：它是单独打开 J-Link/RTT 连接，不会稳定继承 west runner 参数，所以仍可能弹出芯片选择。工作区已经提供 VS Code task：

```text
E73: RTT Viewer (nRF52840 fixed)
```

在 VS Code 里运行 `Tasks: Run Task` 并选择这个任务即可。它等价于：

```powershell
JLinkRTTViewer.exe --device NRF52840_XXAA --connection usb --interface swd --speed 4000 --autoconnect --setwindowtitle "E73 Tracker RTT"
```

如果电脑同时连了多个 J-Link，可以在 [.vscode/tasks.json](../.vscode/tasks.json) 的 `args` 里额外加入 `-usb` 和对应序列号，避免再弹出探针选择。

## 命令行等价流程

从仓库根目录构建：

```powershell
west build -d build_e73_dev -p always -b e73_tracker/nrf52840/dev -- -DBOARD_ROOT=.
```

烧录（dev 变体日常用 J-Link）：

```powershell
west flash -d build_e73_dev --runner jlink
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

`e73_tracker/nrf52840/dev` 不产出 `.uf2`，也不保留 bootloader。要回到可 UF2 更新的布局时，使用 `e73_tracker/nrf52840/uf2` 构建：

```powershell
west build -d build_e73_uf2 -p always -b e73_tracker/nrf52840/uf2 -- -DBOARD_ROOT=. -DWITH_SOFTDEVICE=ON
```

如果板上已经没有 UF2 bootloader，需要用 factory 镜像或单独重烧 bootloader。已有 bootloader 且只想用 J-Link 更新应用时，应烧包含 bootloader metadata 的 SWD-ready 镜像，而不是普通 `zephyr_sd.hex`。

## 安全护栏

在 [CMakeLists.txt](../CMakeLists.txt) 里仍有一条显式守卫：

```cmake
if(CONFIG_BOARD_E73_TRACKER_DEV AND WITH_SOFTDEVICE)
  message(FATAL_ERROR ...)
endif()
```

守卫通过 `Kconfig.e73_tracker` 中声明的兼容符号 `BOARD_E73_TRACKER_DEV` 生效（由 `BOARD_E73_TRACKER_NRF52840_DEV` 驱动），继续阻止对 dev 变体传 `-DWITH_SOFTDEVICE=ON` 产生危险的混合布局镜像。
