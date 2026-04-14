#!/usr/bin/env python3
"""
Generate Adafruit nRF52 bootloader support pages for direct SWD flashing.

This writes:
- a bootloader settings page that marks bank 0 as a valid application
- an erased MBR params page to clear stale update state

The settings page intentionally uses a compatibility layout:
- word 0 = 0x00000001
- word 1 = 0x00000000
- word 2 = app size in bytes

This keeps the image bootable with both the older 32-bit bank field layout and
the newer 16-bit bank field layout used by Adafruit's SDK11-based bootloader.
CRC checking is disabled by storing a zero CRC.
"""

from __future__ import annotations

import argparse
from pathlib import Path


def _parse_int(value: str) -> int:
    return int(value, 0)


def parse_ihex(path: Path) -> dict[int, int]:
    memory: dict[int, int] = {}
    base = 0

    for line_no, raw_line in enumerate(path.read_text().splitlines(), start=1):
        line = raw_line.strip()
        if not line:
            continue
        if not line.startswith(":"):
            raise ValueError(f"{path}:{line_no}: invalid Intel HEX record")

        length = int(line[1:3], 16)
        address = int(line[3:7], 16)
        record_type = int(line[7:9], 16)
        data = bytes.fromhex(line[9 : 9 + length * 2])

        if record_type == 0x00:
            absolute = base + address
            for offset, value in enumerate(data):
                memory[absolute + offset] = value
        elif record_type == 0x01:
            break
        elif record_type == 0x02:
            base = int.from_bytes(data, "big") << 4
        elif record_type == 0x04:
            base = int.from_bytes(data, "big") << 16
        elif record_type in (0x03, 0x05):
            continue
        else:
            raise ValueError(f"{path}:{line_no}: unsupported record type 0x{record_type:02X}")

    return memory


def ihex_record(record_type: int, address: int, data: bytes) -> str:
    payload = bytes([len(data), (address >> 8) & 0xFF, address & 0xFF, record_type]) + data
    checksum = ((~sum(payload) + 1) & 0xFF)
    return ":" + payload.hex().upper() + f"{checksum:02X}"


def write_ihex(path: Path, chunks: list[tuple[int, bytes]]) -> None:
    lines: list[str] = []
    current_upper: int | None = None

    for address, data in sorted(chunks, key=lambda item: item[0]):
        start = 0
        while start < len(data):
            absolute = address + start
            upper = absolute >> 16
            if upper != current_upper:
                current_upper = upper
                lines.append(ihex_record(0x04, 0x0000, upper.to_bytes(2, "big")))

            low = absolute & 0xFFFF
            remaining_in_segment = 0x10000 - low
            chunk_len = min(16, len(data) - start, remaining_in_segment)
            chunk = data[start : start + chunk_len]
            lines.append(ihex_record(0x00, low, chunk))
            start += chunk_len

    lines.append(":00000001FF")
    path.write_text("\n".join(lines) + "\n")


def build_settings_page(page_size: int, app_size: int) -> bytes:
    page = bytearray([0xFF] * page_size)

    # Compatibility format:
    #   0x00: 0x00000001  -> BANK_VALID_APP in both known layouts
    #   0x04: 0x00000000  -> zero CRC, which disables CRC enforcement
    #   0x08: app size    -> useful for the newer short-field layout
    settings = bytearray(28)
    settings[0:4] = (1).to_bytes(4, "little")
    settings[4:8] = (0).to_bytes(4, "little")
    settings[8:12] = app_size.to_bytes(4, "little")

    page[: len(settings)] = settings
    return bytes(page)


def collect_app_image(path: Path, app_start: int) -> tuple[int, int]:
    memory = parse_ihex(path)
    if not memory:
        raise ValueError(f"{path} contains no data records")

    addresses = sorted(addr for addr in memory if addr >= app_start)
    if not addresses:
        raise ValueError(f"{path} contains no application bytes at or above 0x{app_start:X}")

    first = addresses[0]
    if first != app_start:
        raise ValueError(
            f"{path} starts at 0x{first:X}, expected application start 0x{app_start:X}"
        )

    end = addresses[-1] + 1
    return first, end


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--app-hex", required=True, type=Path)
    parser.add_argument("--output-hex", required=True, type=Path)
    parser.add_argument("--app-start", required=True, type=_parse_int)
    parser.add_argument("--settings-address", required=True, type=_parse_int)
    parser.add_argument("--mbr-params-address", type=_parse_int)
    parser.add_argument("--page-size", type=_parse_int, default=0x1000)
    args = parser.parse_args()

    _, app_end = collect_app_image(args.app_hex, args.app_start)
    app_size = app_end - args.app_start

    chunks = [(args.settings_address, build_settings_page(args.page_size, app_size))]
    if args.mbr_params_address is not None:
        chunks.append((args.mbr_params_address, bytes([0xFF] * args.page_size)))

    write_ihex(args.output_hex, chunks)

    print(
        f"Generated {args.output_hex} "
        f"(app_start=0x{args.app_start:X}, app_size=0x{app_size:X}, "
        f"settings=0x{args.settings_address:X})"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
