# E73 Tracker J-Link 调试与 UF2 发布流程

`e73_tracker` 是 E73 Tracker 的日常 J-Link 调试 target。`e73_tracker/nrf52840/uf2` 是同一硬件的 UF2 发布变种。两者放在同一个 board 目录下，共用同一份硬件 DTS；区别只在启动布局和构建输出。

默认 `e73_tracker` 假设开发板处于无 UF2 bootloader 的状态，固件从 flash `0x0` 直接启动，方便 nRF Connect for VSCode 通过 J-Link 做 `Flash`、`Debug` 和 `Attach`。

## 适用场景

- 你一直连接着 J-Link，主要做断点、单步、RTT 日志和快速迭代。
- 你希望调试和 release 共用 E73 硬件定义，避免两个 board 文件夹逐渐漂移。
- 你不想在日常调试时反复生成或写入 Adafruit bootloader settings page。
- 你当前不需要验证 UF2 拖拽升级、OTA 或带 SoftDevice 的发布布局。

不适用场景：

- 要给已有 Adafruit UF2 bootloader 的板子更新用户固件。
- 要产出 `.uf2` 或 release hex。
- 要验证 `SoftDevice + app` 的发布布局。

这些场景使用 `e73_tracker/nrf52840/uf2`。

## 配置差异

| Target | 用途 | App 起始地址 | UF2 输出 | SoftDevice |
| --- | --- | --- | --- | --- |
| `e73_tracker` | J-Link 日常调试 | `0x0` | 关闭 | 关闭 |
| `e73_tracker/nrf52840/uf2` | UF2/release/已有 bootloader | `0x1000` | 开启 | 按构建参数开启 |

`uf2` 变种的 DTS 只 include `e73_tracker.dts` 并改了 model/compatible。以后修改 E73 引脚、SPI、LED、电池分压、电源 regulator 时，只需要维护 `e73_tracker.dts`。

## 首次切换到调试布局

如果开发板之前烧过 Adafruit UF2 bootloader，首次切换到 `e73_tracker` 前建议做一次 recover 或整片擦除。这样 reset 后不会再先进 UF2 bootloader。

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

- Board target: `e73_tracker`
- Board root: 仓库根目录
- Build directory: 建议使用独立目录，例如 `build_e73_dev`
- Extra CMake arguments: 通常只需要 `-DBOARD_ROOT=.`

创建后，日常使用：

- `Build`: 编译当前调试固件。
- `Flash`: 通过 J-Link 烧录 app。
- `Debug`: 烧录并启动调试会话。
- `Attach`: 只附加到已经在运行的固件，不重新烧录。
- `RTT Terminal`: 查看 RTT 日志。

如果之前创建过旧的 `e73_tracker_dev/nrf52840` 或 `e73_tracker_uf2/nrf52840/dev` Build Configuration，需要删除旧配置或新建一个 build directory。nRF Connect 会在 build 目录里缓存 board 名，旧目录继续构建时会看到类似下面的错误：

```text
No board named 'e73_tracker_dev' found.
```

这种情况下不要复用旧 `build` 目录；新建 `build_e73_dev`，或者在 nRF Connect 里删除旧配置后重新添加 `e73_tracker`。

## 命令行等价流程

从仓库根目录构建：

```powershell
west build -d build_e73_dev -p always -b e73_tracker -- -DBOARD_ROOT=.
```

烧录：

```powershell
west flash -d build_e73_dev -r jlink
```

启动调试：

```powershell
west debug -d build_e73_dev -r jlink
```

只附加，不重新烧录：

```powershell
west attach -d build_e73_dev -r jlink
```

## 日常擦写建议

首次切换布局时可以 recover。之后日常调试尽量直接用 `Flash` / `Debug`，不要把 `--erase` 固化进 board runner 参数。

需要清掉 NVS/settings 时，再显式做一次整片擦除或 recover。这样能避免每次调试都把校准、配对、bootloader 恢复状态等数据清掉。

## 回到 UF2/release 布局

默认 `e73_tracker` 不产出 `.uf2`，也不保留 bootloader。要回到可 UF2 更新的布局时，使用 `uf2` 变种：

```powershell
west build -d build_e73_uf2 -p always -b e73_tracker/nrf52840/uf2 -- -DBOARD_ROOT=. -DWITH_SOFTDEVICE=ON
```

这个 target 仍然走当前项目的 UF2 构建流程：`e73_tracker_uf2_defconfig` 打开 `CONFIG_BUILD_OUTPUT_UF2=y` 并设置 `CONFIG_FLASH_LOAD_OFFSET=0x1000`；`sysbuild.cmake` 因为 qualifiers 中包含 `uf2`，会选择 `pm_static/nrf52840_uf2.yml` 或 `pm_static/nrf52840_uf2_sd.yml`；加 `-DWITH_SOFTDEVICE=ON` 时，顶层 CMake 会额外生成 `zephyr/zephyr_sd.hex` 和 `zephyr/zephyr_sd.uf2`。

如果板上已经没有 UF2 bootloader，需要用 factory 镜像或单独重烧 bootloader。已有 bootloader 且只想用 J-Link 更新应用时，应烧包含 bootloader metadata 的 SWD-ready 镜像，而不是普通 `zephyr_sd.hex`。
