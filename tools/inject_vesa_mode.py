#!/usr/bin/env python3
"""Inject a VESA mode into the CL-GD5429 option ROM and regenerate the
embedded header.

The structural offsets (mode list, parameter-table stride, ModeInfoBlock
source) are filled in from the ROM reverse-engineering pass; until then this
tool provides the surrounding machinery: load, checksum-fix, header
regeneration, and a verify pass, all validated against the pristine ROM so
the injection step is the only new variable.
"""

from __future__ import annotations

import argparse
import hashlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_ROM = Path("/opt/pico/PCem-ROMs/5429.vbi")
DEFAULT_HEADER = ROOT / "src/modules/pcem_cl5429_bios_rom.h"
ROM_SIZE = 32768


def load_rom(path: Path) -> bytearray:
    data = bytearray(path.read_bytes())
    if len(data) != ROM_SIZE:
        raise SystemExit(f"expected {ROM_SIZE}-byte ROM, got {len(data)}")
    if data[0] != 0x55 or data[1] != 0xAA:
        raise SystemExit("not an option ROM (missing 55 AA signature)")
    return data


def rom_length(data: bytearray) -> int:
    return data[2] * 512


def checksum(data: bytearray, length: int) -> int:
    return sum(data[:length]) & 0xFF


def fix_checksum(data: bytearray) -> None:
    """Force the option-ROM 8-bit sum to zero by adjusting the last byte of
    the declared image."""
    length = rom_length(data)
    data[length - 1] = 0
    data[length - 1] = (-sum(data[:length])) & 0xFF
    assert checksum(data, length) == 0


def write_header(data: bytearray, header: Path, rom_path: Path) -> None:
    digest = hashlib.sha256(bytes(data)).hexdigest()
    lines = [
        "#pragma once",
        "",
        "#include <stdint.h>",
        "",
        f"// Embedded from {rom_path} for PCem CL-GD5429 option ROM behavior.",
        "// PCem maps this ROM at bus address 0xc0000.",
        f"// Source SHA-256: {digest}",
        f"static constexpr uint8_t kPcemCl5429BiosRom[{ROM_SIZE}] = {{",
    ]
    for i in range(0, ROM_SIZE, 12):
        chunk = ", ".join(f"0x{b:02x}" for b in data[i : i + 12])
        lines.append(f"    {chunk},")
    lines.append("};")
    header.write_text("\n".join(lines) + "\n")


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--rom", type=Path, default=DEFAULT_ROM)
    ap.add_argument("--header", type=Path, default=DEFAULT_HEADER)
    ap.add_argument("--verify", action="store_true",
                    help="regenerate the header from the pristine ROM and confirm it is byte-identical to the committed header")
    args = ap.parse_args()

    data = load_rom(args.rom)
    if args.verify:
        # Reproducibility: re-run the injection from the pristine ROM and
        # confirm it reproduces the committed header byte-for-byte.
        import re
        inject_800x480(data)
        fix_checksum(data)
        want = args.header.read_text()
        body = want[want.index("= {"):]
        committed = bytes(int(x, 16) for x in re.findall(r"0x([0-9a-fA-F]{2})", body))
        ok_rom = committed == bytes(data)
        ok_cksum = checksum(data, rom_length(data)) == 0
        print(f"committed header == injected ROM: {ok_rom}")
        print(f"option-ROM checksum valid: {ok_cksum}")
        raise SystemExit(0 if (ok_rom and ok_cksum) else 1)

    inject_800x480(data)
    fix_checksum(data)
    write_header(data, args.header, args.rom)
    print(f"wrote {args.header}")


# --- Structural offsets, from the ROM reverse-engineering pass -------------
TABLE_A = 0x1442          # mode-descriptor table
REC_STRIDE = 0x21
NULL_SLOT = 36            # spare record already inside the count (37)
TABLE_B = 0x1907          # register-template table, 0x40-byte records
TMPL_640x480 = 0x56EB     # nibble 0 (special)
TMPL_800x600 = 0x19C7     # nibble 5
FREE_TMPL_NIBBLE = 8      # 0x1a87, a real slot no mode record references
# The 640x480x8 descriptor is the donor: its +0x11 gate byte is 0 (so the
# ROM's 0x0F99 config gate always passes and 4F00 enumerates it), and it
# already carries 480-line geometry. Cloning the 800x600x8 record instead
# fails - that record's +0x11 is nonzero and the gate rejects it.
DONOR_640x480x8 = 13      # table-A index of the 640x480x8 record

# Register positions inside a template that carry vertical timing (CRTC
# 6/7/9/0x10/0x11/0x12/0x15/0x16 and the two vertical header fields). Pitch
# (+0x1d) and htotal (+0x0a) track width, so they are NOT transplanted.
VERT_POS = (0x01, 0x04, 0x09, 0x10, 0x11, 0x13, 0x1A, 0x1B, 0x1C, 0x1F, 0x20)

NEW_VESA_MODE = 0x160     # first free OEM VBE number
NEW_INTERNAL = 0x77       # unused internal mode number


def inject_800x480(data: bytearray) -> None:
    tmpl_off = TABLE_B + (FREE_TMPL_NIBBLE - 2) * 0x40
    # 800x480 template = 800x600 template with 480-line vertical registers
    # taken from the known-good 640x480 template.
    tmpl = bytearray(data[TMPL_800x600:TMPL_800x600 + 0x40])
    src480 = data[TMPL_640x480:TMPL_640x480 + 0x40]
    for pos in VERT_POS:
        tmpl[pos] = src480[pos]
    data[tmpl_off:tmpl_off + 0x40] = tmpl

    # New descriptor = 640x480x8 donor (480-line geometry, zero gate byte),
    # widened to 800 and pointed at the new 800-wide template. The internal
    # number (0x77) is larger than every existing record's so the table
    # stays sorted ascending and the last slot remains the match for it.
    donor = TABLE_A + DONOR_640x480x8 * REC_STRIDE
    rec = bytearray(data[donor:donor + REC_STRIDE])
    rec[0x00] = NEW_INTERNAL
    rec[0x01] = NEW_VESA_MODE & 0xFF
    rec[0x02] = NEW_VESA_MODE >> 8
    rec[0x03] = 800 & 0xFF          # XResolution
    rec[0x04] = 800 >> 8
    # YResolution already 480 in the donor.
    rec[0x0C] = (rec[0x0C] & 0xF0) | FREE_TMPL_NIBBLE  # template selector
    slot = TABLE_A + NULL_SLOT * REC_STRIDE
    assert all(b == 0 for b in data[slot:slot + REC_STRIDE]), "null slot not empty"
    data[slot:slot + REC_STRIDE] = rec
    print(f"injected VESA {NEW_VESA_MODE:#05x} 800x480x8 "
          f"(record slot {NULL_SLOT} @ {slot:#06x}, template @ {tmpl_off:#06x})")


if __name__ == "__main__":
    main()
