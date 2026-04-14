# Adafruit UF2 Bootloader And SWD Flashing

## Symptom

On `e73_tracker_uf2` with the matching SoftDevice version:

- Updating through the UF2 drive works.
- RTT logs are visible after a UF2 update.
- Flashing the same firmware directly from VSCode or J-Link leaves the board in UF2 mode after reset or power cycle.
- Flash readback still shows data in the `MBR`, `SoftDevice`, `app`, and `uf2_bootloader` regions.

## Root Cause

The problem is not just `CONFIG_FLASH_LOAD_OFFSET`, and it is not that the app
image itself is broken.

Adafruit's nRF52 bootloader does not only check whether the app region contains
code. It also checks bootloader metadata before it decides that an application
is valid and safe to boot.

For the SoftDevice layout used by this project:

- The app starts at `0x27000`.
- The bootloader also expects a valid bootloader settings page.
- A UF2 update writes those metadata pages as part of the DFU flow.
- A direct SWD/J-Link flash usually writes only `app` or `SoftDevice + app`.
- The settings page is therefore missing or stale, so the bootloader stays in
  UF2/DFU mode.

## Project Layout

For `nrf52840` with `WITH_SOFTDEVICE=ON`:

- `MBR`: `0x0000 - 0x0FFF`
- `SoftDevice`: `0x1000 - 0x26FFF`
- `Application`: `0x27000 - ...`
- `MBR params page`: `0xFE000`
- `Bootloader settings page`: `0xFF000`
- `UF2 bootloader`: `0xF4000 - 0xFFFFF`

That means this board should use:

- `CONFIG_FLASH_LOAD_OFFSET=0x27000`
- `-DWITH_SOFTDEVICE=ON`

## Repository Fix

This repository now generates an extra SWD-ready image for UF2 boards.

Build flow:

1. Read the app start address and size from `zephyr.hex`.
2. Generate `adafruit_bootloader_pages.hex` with:
   - a bootloader settings page
   - an erased MBR params page
3. Merge that with the base firmware image:
   - `zephyr.hex` without SoftDevice
   - `zephyr_sd.hex` with SoftDevice
4. Produce `zephyr_swd.hex`.
5. Register `zephyr_swd.hex` as `BYPRODUCT_KERNEL_SIGNED_HEX_NAME` so sysbuild
   and partition manager use it as the preferred image for the top-level
   `merged.hex`.

This is important because VSCode/J-Link normally uses the top-level
`merged.hex`. By making sysbuild build `merged.hex` from `zephyr_swd.hex`, the
default flash path becomes bootable.

## Relevant Files

- [CMakeLists.txt](../CMakeLists.txt)
- [sysbuild.cmake](../sysbuild.cmake)
- [scripts/build/gen_adafruit_bootloader_pages.py](../scripts/build/gen_adafruit_bootloader_pages.py)

## Settings Format Used Here

The helper script currently writes a compatibility layout:

- `word 0 = 0x00000001`
- `word 1 = 0x00000000`
- `word 2 = app size`

This marks bank 0 as a valid application and disables CRC enforcement by using
zero as the stored CRC value.

## Output Files

- `zephyr.hex`
  - App-only image.
  - Useful for OTA-like semantics, but not sufficient for direct Adafruit
    bootloader validation.
- `zephyr_sd.hex`
  - SoftDevice + app merged image.
  - Good for UF2 packaging, but still missing the bootloader metadata pages.
- `zephyr_swd.hex`
  - SWD-ready image.
  - Contains app, SoftDevice when enabled, and the metadata pages needed by the
    bootloader.
- `merged.hex`
  - Top-level sysbuild image.
  - After this change it should be built from `zephyr_swd.hex`, so the default
    runner path also becomes bootable.
- `zephyr_factory.hex`
  - Only generated when `-DADAFRUIT_BOOTLOADER_HEX=...` is provided.
  - Intended for one-shot recovery programming of bootloader + SoftDevice +
    app + metadata pages.

## Build Commands

Recommended SD build:

```powershell
west build -d build_e73 -p always -b e73_tracker_uf2/nrf52840 -- -DBOARD_ROOT=. -DWITH_SOFTDEVICE=ON
```

Factory image build:

```powershell
west build -d build_e73 -p always -b e73_tracker_uf2/nrf52840 -- -DBOARD_ROOT=. -DWITH_SOFTDEVICE=ON -DADAFRUIT_BOOTLOADER_HEX=E:/path/to/bootloader.hex
```

## Troubleshooting

If the device still stays in UF2 mode after flashing:

- Confirm `-DWITH_SOFTDEVICE=ON` is present.
- Confirm `CONFIG_FLASH_LOAD_OFFSET=0x27000`.
- Confirm the runner is flashing the top-level `merged.hex` or the child
  `zephyr_swd.hex`.
- Compare `0xFE000` and `0xFF000` against a known-good board.
- If the bootloader or SoftDevice layout changed, update both
  `pm_static/nrf52840_uf2_sd.yml` and the page addresses used by the helper
  script.
