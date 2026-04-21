# E73 Tracker UF2 发布流程

`e73_tracker/nrf52840/uf2` 是 `e73_tracker` 板级配置的 **uf2 变体**，用于产出可直接通过 Adafruit UF2 bootloader 拖拽升级、nrfutil DFU 或 BLE OTA 升级的发布镜像。它假设板上已经烧好了 Adafruit UF2 bootloader（保留在 `0xf4000-0x100000`），用户固件从 `0x1000`（无 SoftDevice）或 `0x27000`（带 SoftDevice）开始运行。

日常 J-Link 调试请使用 dev 变体，对应文档：[e73_tracker_dev_jlink_debug.md](e73_tracker_dev_jlink_debug.md)。

## 适用场景

- 要产出 `.uf2` 镜像给已有 bootloader 的用户拖拽升级。
- 要产出带 SoftDevice 的 release 镜像（`zephyr_sd.hex` / `zephyr_sd.uf2`）。
- 要产出 BLE OTA 升级包（`*_OTA.zip`）。
- 要在 CI 上出发布产物。

不适用场景：

- 没有 bootloader 的裸板日常调试（用 dev 变体）。
- 要直接跳过 bootloader 验证 `0x0` 起始的镜像（用 dev 变体）。

## 两种发布布局

uf2 变体支持两种 flash 布局，通过 `-DWITH_SOFTDEVICE=ON` 切换。分区定义在 [pm_static/](../pm_static/) 目录下，由 [sysbuild.cmake](../sysbuild.cmake) 在检测到 `SB_CONFIG_BOARD_QUALIFIERS MATCHES "uf2"` 时自动挑选。

### 无 SoftDevice（默认）

使用 [pm_static/nrf52840_uf2.yml](../pm_static/nrf52840_uf2.yml)：

| 分区 | 地址范围 | 大小 | 说明 |
| --- | --- | --- | --- |
| `bootloader` | `0x0` – `0x1000` | 4 KB | MBR，位于 flash 起始 |
| *app* | `0x1000` – `0xf4000` | ~972 KB | 用户固件，由 `CONFIG_FLASH_LOAD_OFFSET=0x1000` 决定起始 |
| `uf2_bootloader` | `0xf4000` – `0x100000` | 48 KB | Adafruit UF2 bootloader，禁止覆盖 |

### 带 SoftDevice

使用 [pm_static/nrf52840_uf2_sd.yml](../pm_static/nrf52840_uf2_sd.yml)：

| 分区 | 地址范围 | 大小 | 说明 |
| --- | --- | --- | --- |
| `bootloader` | `0x0` – `0x1000` | 4 KB | MBR |
| `softdevice` | `0x1000` – `0x27000` | 152 KB | Nordic SoftDevice s140 7.3.0 |
| *app* | `0x27000` – `0xf4000` | ~820 KB | 用户固件，需要传 `-DCONFIG_FLASH_LOAD_OFFSET=0x27000` 让 app 避开 SoftDevice |
| `uf2_bootloader` | `0xf4000` – `0x100000` | 48 KB | Adafruit UF2 bootloader |

带 SoftDevice 时 SoftDevice 会广播 BLE 服务，支持 BLE OTA 升级；不带 SoftDevice 时只能依赖 USB/UF2 和 nrfutil DFU。

## 输出产物

`e73_tracker/nrf52840/uf2` 下开启了 `CONFIG_BUILD_OUTPUT_UF2=y` 和 `CONFIG_BUILD_OUTPUT_UF2_FAMILY_ID="0xADA52840"`（Adafruit nRF52840 family）。不带 SD 时 Zephyr 直接输出 `zephyr.hex` / `zephyr.uf2`。

带 SoftDevice 时 [CMakeLists.txt](../CMakeLists.txt) 里的 `sd_merged_uf2` 目标会额外跑：

1. `mergehex.py --overlap=replace -o zephyr_sd.hex s140_nrf52_7.3.0_softdevice.hex zephyr.hex` — 合并 SoftDevice 和用户固件。
2. `uf2conv.py zephyr_sd.hex -c -f 0xADA52840 -o zephyr_sd.uf2` — 转成可拖拽的 UF2。

构建目录 `build/zephyr/` 下能看到的相关文件：

| 文件 | 何时生成 | 用途 |
| --- | --- | --- |
| `zephyr.hex` | 每次构建 | 裸 app hex，不含 SoftDevice |
| `zephyr.uf2` | 无 SD 构建 | 拖拽到 UF2 盘刷 app |
| `zephyr_sd.hex` | 带 SD 构建 | SoftDevice + app 合并 hex，用于 SWD/J-Link 烧写 |
| `zephyr_sd.uf2` | 带 SD 构建 | SoftDevice + app 的 UF2 |

SoftDevice hex 固定在 [softdevice/s140_nrf52_7.3.0_softdevice.hex](../softdevice/s140_nrf52_7.3.0_softdevice.hex)，升级 SoftDevice 版本需要同步更新该文件并重新验证 `CONFIG_FLASH_LOAD_OFFSET`。

## 本地构建

无 SoftDevice：

```powershell
west build -d build_e73_uf2 -p always -b e73_tracker/nrf52840/uf2 -- -DBOARD_ROOT=.
```

带 SoftDevice：

```powershell
west build -d build_e73_uf2_sd -p always -b e73_tracker/nrf52840/uf2 -- `
  -DBOARD_ROOT=. `
  -DWITH_SOFTDEVICE=ON `
  -DCONFIG_FLASH_LOAD_OFFSET=0x27000
```

`-DWITH_SOFTDEVICE=ON` 触发三件事：

- [CMakeLists.txt](../CMakeLists.txt) 生成 `zephyr_sd.hex` / `zephyr_sd.uf2`。
- [sysbuild.cmake](../sysbuild.cmake) 选中 `pm_static/nrf52840_uf2_sd.yml` 作为分区表。
- 必须配合 `-DCONFIG_FLASH_LOAD_OFFSET=0x27000` 把 app 起始地址从 `0x1000` 移到 `0x27000`，否则 app 会覆盖 SoftDevice。

## 烧录方式

### UF2 拖拽（推荐日常升级）

1. 按下板上 reset 按钮两次进入 Adafruit UF2 bootloader，系统会挂载出 U 盘。
2. 把 `zephyr.uf2`（无 SD）或 `zephyr_sd.uf2`（带 SD）拖进 U 盘。
3. 设备自动重启并运行新固件。

### nrfutil DFU（串口 DFU）

使用 [adafruit-nrfutil](https://github.com/adafruit/Adafruit_nRF52_nrfutil) 通过 USB CDC 串口升级。参见 [04_设备操作与配置.md](04_设备操作与配置.md) 的 DFU 章节。

### J-Link / SWD

带 SoftDevice 发布时优先烧 `zephyr_sd.hex`（同时包含 SoftDevice 和 app）。注意这条路径会覆盖 `0x0 - 0x27000`，但不会动 `0xf4000 - 0x100000` 的 UF2 bootloader。

```powershell
west flash -d build_e73_uf2_sd --runner jlink
```

如果用 `zephyr.hex`（只有 app），需要先确保 SoftDevice 已经在 `0x1000-0x27000` 的位置，否则设备直接挂。

### BLE OTA

带 SoftDevice 时，可以把 app hex 打包成 Adafruit 的 DFU zip，走 BLE OTA 升级：

```powershell
adafruit-nrfutil dfu genpkg `
  --dev-type 0x0052 `
  --application build_e73_uf2_sd/zephyr/zephyr.hex `
  Crazt_E73_Tracker_SPI_SD_OTA.zip
```

其中 `0x0052` 是本项目固定的 `BLE_OTA_DEV_TYPE`，定义在 [.github/workflows/workflow.yml](../.github/workflows/workflow.yml) 的 `env` 里。OTA 升级的客户端侧流程参见 [04_设备操作与配置.md](04_设备操作与配置.md)。

## CI 发布流程

[.github/workflows/workflow.yml](../.github/workflows/workflow.yml) 的 `build` job 跑一个 matrix，对 `e73_tracker/nrf52840/uf2` 产出两条配置：

| `filename` | `with_softdevice` | 构建参数 |
| --- | --- | --- |
| `Crazt_E73_Tracker_SPI` | 否 | 默认 |
| `Crazt_E73_Tracker_SPI_SD` | 是 | `-DWITH_SOFTDEVICE=ON -DCONFIG_FLASH_LOAD_OFFSET=0x27000` |

每个 matrix 项完成后上传的 artifact 命名规则：

| 源文件 | 无 SD 产物名 | 带 SD 产物名 |
| --- | --- | --- |
| `zephyr.uf2` | `Crazt_E73_Tracker_SPI.uf2` | — |
| `zephyr.hex` | `Crazt_E73_Tracker_SPI.hex` | `Crazt_E73_Tracker_SPI_SD_OTA.hex` |
| `zephyr_sd.hex` | — | `Crazt_E73_Tracker_SPI_SD.hex` |
| `zephyr_sd.uf2` | — | `Crazt_E73_Tracker_SPI_SD.uf2` |
| BLE OTA zip | — | `Crazt_E73_Tracker_SPI_SD_OTA.zip` |

带 SD 那条额外跑 `adafruit-nrfutil dfu genpkg --dev-type 0x0052 --application zephyr.hex <...>_OTA.zip` 生成 BLE OTA 升级包。

## 安全护栏

`WITH_SOFTDEVICE=ON` 只在 uf2 变体下合法。[CMakeLists.txt](../CMakeLists.txt) 里显式拒绝对 dev 变体传 `-DWITH_SOFTDEVICE=ON`，避免产出 app 从 `0x0` 起始但又带 SoftDevice 的半残镜像：

```cmake
if(CONFIG_BOARD_E73_TRACKER_DEV AND WITH_SOFTDEVICE)
  message(FATAL_ERROR ...)
endif()
```

`CONFIG_BOARD_E73_TRACKER_DEV` 是 [boards/crazt/e73_tracker/Kconfig.e73_tracker](../boards/crazt/e73_tracker/Kconfig.e73_tracker) 里声明的兼容符号，由 HWMv2 自动生成的 `BOARD_E73_TRACKER_NRF52840_DEV` 驱动。uf2 变体下这个符号是 `n`，守卫不触发。

## 相关文件索引

- [boards/crazt/e73_tracker/e73_tracker_uf2_defconfig](../boards/crazt/e73_tracker/e73_tracker_uf2_defconfig) — uf2 变体的默认 Kconfig（启用 UF2 输出、FLASH_LOAD_OFFSET=0x1000）
- [boards/crazt/e73_tracker/e73_tracker_common.dtsi](../boards/crazt/e73_tracker/e73_tracker_common.dtsi) — 硬件引脚定义，dev/uf2 共用
- [pm_static/nrf52840_uf2.yml](../pm_static/nrf52840_uf2.yml) / [pm_static/nrf52840_uf2_sd.yml](../pm_static/nrf52840_uf2_sd.yml) — 两种 flash 布局
- [softdevice/s140_nrf52_7.3.0_softdevice.hex](../softdevice/s140_nrf52_7.3.0_softdevice.hex) — Nordic SoftDevice 固件
- [sysbuild.cmake](../sysbuild.cmake) — 用 `MATCHES "uf2"` 激活分区表选择
- [CMakeLists.txt](../CMakeLists.txt) — SoftDevice 合并 + UF2 转换逻辑
- [.github/workflows/workflow.yml](../.github/workflows/workflow.yml) — CI 发布 matrix
