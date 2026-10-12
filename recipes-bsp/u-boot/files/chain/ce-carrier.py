#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Package a fixed-address H432B U-Boot payload for the factory NK slot.

Offline only: no device access. Preserves the qualified normal-boot ROMHDR
pointer pair. The two-stage role permits a larger, explicitly bounded carrier.
"""

from __future__ import annotations

import argparse
import struct
import zlib
from pathlib import Path

def build(payload: bytes, load: int, entry: int) -> bytes:
    if not payload or load != CE_BASE or entry != CE_BASE:
        raise ValueError("only the qualified fixed-address carrier is supported")
    return (b"B000FF\n" + struct.pack("<II", load, len(payload))
            + struct.pack("<III", load, len(payload), sum(payload) & 0xffffffff)
            + payload + struct.pack("<III", 0, entry, 0))


def verify(blob: bytes) -> tuple[int, int, int, int]:
    if len(blob) < 39 or blob[:7] != b"B000FF\n":
        raise ValueError("not a complete B000FF carrier")
    start, span, address, length, expected = struct.unpack_from("<IIIII", blob, 7)
    if not span or address != start or length != span or len(blob) != span + 39:
        raise ValueError("expected exactly one bounded image record and trailer")
    payload = blob[27:27 + span]
    if sum(payload) & 0xffffffff != expected:
        raise ValueError("carrier checksum mismatch")
    zero, entry, checksum = struct.unpack_from("<III", blob, 27 + span)
    if zero or checksum:
        raise ValueError("invalid carrier terminator")
    return start, span, entry, 0



CE_BASE = 0x8002_0000
PA_BASE = 0x4002_0000
CODE_OFFSET = 0x1000
ROMHDR_OFFSET = 0x100
EXTENSIONS_OFFSET = 0x200
NAME_OFFSET = 0x220
SHIM_SIZE = CODE_OFFSET
INTEGRITY_OFFSET = 0x300
INTEGRITY_MAGIC = 0x52433255


def make_payload(code: bytes, integrity: bool = False, chain: bool = False,
                 modern_elf: bytes | None = None) -> bytes:
    limit = 0x100000 if chain or modern_elf is not None else 0x20000
    if len(code) < 68 or SHIM_SIZE + len(code) > limit:
        raise ValueError("U-Boot is truncated or exceeds its carrier role limit")
    if code[3] != 0xea:
        raise ValueError("expected ARM branch vector")
    if modern_elf is not None:
        if (len(modern_elf) < 52 or modern_elf[:7] != b"\x7fELF\x01\x01\x01"
                or struct.unpack_from("<HH", modern_elf, 16) != (2, 40)):
            raise ValueError("expected an ARM ELF32 executable")
        linked = struct.unpack_from("<I", modern_elf, 24)[0]
        phoff = struct.unpack_from("<I", modern_elf, 28)[0]
        entsize, count = struct.unpack_from("<HH", modern_elf, 42)
        if entsize != 32 or not count or phoff + count * entsize > len(modern_elf):
            raise ValueError("invalid ELF program headers")
        shoff = struct.unpack_from("<I", modern_elf, 32)[0]
        shsize, shcount = struct.unpack_from("<HH", modern_elf, 46)
        if shsize != 40 or not shcount or shoff + shsize * shcount > len(modern_elf):
            raise ValueError("invalid ELF section headers")
        sections = []
        for i in range(shcount):
            fields = struct.unpack_from("<10I", modern_elf, shoff + i * shsize)
            _, kind, flags, address, offset, size, *_ = fields
            if not flags & 2 or kind == 8 or not size:
                continue
            relative = address - linked
            if (relative < 0 or size > len(code) - relative or
                    offset + size > len(modern_elf) or
                    code[relative:relative + size] != modern_elf[offset:offset + size]):
                raise ValueError("ELF allocated section does not match payload")
            sections.append((relative, relative + size))
        matched = False
        for i in range(count):
            kind, offset, va, pa, size, memsz, flags, align = struct.unpack_from(
                "<8I", modern_elf, phoff + i * entsize)
            if kind != 1 or not size:
                continue
            relative = pa - linked
            if (va != pa or memsz < size or relative < 0 or size > len(code) - relative or
                    offset + size > len(modern_elf)):
                raise ValueError("ELF load segment does not match payload")
            # objcopy fills inter-section holes with FF, unlike ELF's zero
            # padding. Only actual allocated sections must match byte-for-byte.
            cursor = relative
            for start, end in sorted(sections):
                if start < relative or end > relative + size:
                    continue
                if start < cursor or any(byte != 0xff for byte in code[cursor:start]):
                    raise ValueError("invalid ELF section overlap or binary gap fill")
                cursor = end
            if cursor == relative or any(byte != 0xff for byte in code[cursor:relative + size]):
                raise ValueError("uncovered ELF load segment")
            matched |= relative == 0 and size >= 68 and bool(flags & 1)
        if not matched:
            raise ValueError("ELF contains no loadable payload")
    else:
        linked = struct.unpack_from("<I", code, 0x40)[0]
    if linked != PA_BASE + CODE_OFFSET:
        raise ValueError(
            f"U-Boot vector links to 0x{linked:08x}, expected "
            f"0x{PA_BASE + CODE_OFFSET:08x}"
        )

    image = bytearray(SHIM_SIZE + len(code))
    # EBOOT's physical-address OEMLaunch jumps here with MMU disabled.
    # ARM B from +0x0000 to +0x1000: PC is current address + 8.
    branch_immediate = (CODE_OFFSET - 8) // 4
    struct.pack_into("<I", image, 0, 0xEA00_0000 | branch_immediate)

    # CE NK and the normal-boot parser both place the ROM vector at +0x40.
    image[0x40:0x44] = b"ECEC"
    # EBOOT's NAND writer derives the cold load base by
    # subtracting the relative ROMHDR offset at +0x48 from the absolute
    # pointer at +0x44. Omitting +0x48 silently loads the whole image
    # ROMHDR_OFFSET bytes too high on the next NAND boot, even though
    # recovery launches correctly from the B000FF record address.
    struct.pack_into("<II", image, 0x44,
                     CE_BASE + ROMHDR_OFFSET, ROMHDR_OFFSET)

    end = CE_BASE + len(image)
    ram_start = (end + 0xFFFF) & ~0xFFFF
    romhdr = struct.pack(
        "<17IHH3I",
        0x4001C001, 0x4184C0F4,  # dllfirst, dlllast: stock CE6 values
        CE_BASE, end,            # physical image addresses in EBOOT's CE VA
        1,                       # nummods: a single nk.exe module
        ram_start, ram_start, 0x8F900000,
        0, 0,                    # no CE copy entries
        0, 0,                    # no profiling range
        0, 0, 0x40404040,       # numfiles, kernel flags, FS RAM percent
        0, 0,                    # no driver globals
        0x01C2, 2,              # ARMv7 / stock CE misc flags
        CE_BASE + EXTENSIONS_OFFSET, 0, 0,
    )
    if len(romhdr) != 0x54:
        raise AssertionError("ROMHDR must be 0x54 bytes")
    image[ROMHDR_OFFSET:ROMHDR_OFFSET + len(romhdr)] = romhdr

    # CE module TOC entry immediately after ROMHDR. EBOOT's observed cold
    # parser follows name_ptr (+0x10 in this 0x20-byte record) to find nk.exe.
    toc = struct.pack(
        "<8I", 7, 0, 0, len(image), CE_BASE + NAME_OFFSET,
        0, 0, CE_BASE,
    )
    image[ROMHDR_OFFSET + 0x54:ROMHDR_OFFSET + 0x74] = toc
    image[NAME_OFFSET:NAME_OFFSET + 7] = b"nk.exe\0"
    # The pExtensions target at +0x200 is a zero-terminated empty list.
    image[CODE_OFFSET:] = code
    if modern_elf is not None:
        # Host-side role marker. Integrity check, not an authenticity claim.
        struct.pack_into("<4sIII", image, 0x310, b"MUB1",
                         PA_BASE + CODE_OFFSET, len(code), zlib.crc32(code))
    if integrity:
        struct.pack_into(
            "<III", image, INTEGRITY_OFFSET,
            INTEGRITY_MAGIC, zlib.crc32(code), len(code),
        )
    return bytes(image)


def validate_cold_parser(blob: bytes) -> None:
    """Check EBOOT's signature/TOC lookup AND NAND cold-base calculation."""
    start, span, entry, suffix = verify(blob)
    if start != CE_BASE or entry != CE_BASE or suffix:
        raise ValueError("unexpected carrier start, entry, or suffix")
    payload = blob[27:-12]
    if not SHIM_SIZE + 68 <= span <= 0x100000 or payload[0x40:0x44] != b"ECEC":
        raise ValueError("missing ECEC vector or wrong span")
    rom_va, rom_relative = struct.unpack_from("<II", payload, 0x44)
    cold_base = (rom_va - rom_relative) & 0xFFFF_FFFF
    if cold_base != start:
        raise ValueError(
            f"EBOOT cold-load base 0x{cold_base:08x} differs from "
            f"B000FF load/entry 0x{start:08x}; check ROMHDR pointer pair"
        )
    rom_offset = rom_va - CE_BASE
    if rom_offset != ROMHDR_OFFSET or rom_relative != ROMHDR_OFFSET:
        raise ValueError("ROMHDR pointer outside image")
    nummods = struct.unpack_from("<I", payload, rom_offset + 0x10)[0]
    if nummods != 1:
        raise ValueError("expected exactly one module")
    name_va = struct.unpack_from("<I", payload, rom_offset + 0x54 + 0x10)[0]
    name_offset = name_va - CE_BASE
    if name_offset != NAME_OFFSET or payload[name_offset:name_offset + 7] != b"nk.exe\0":
        raise ValueError("EBOOT cannot find nk.exe module")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("code", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--integrity", action="store_true",
                        help="embed early-boot CRC32 and code length at shim +0x300")
    parser.add_argument("--chain", action="store_true", help="allow the bounded two-stage 1 MiB carrier role")
    parser.add_argument("--modern-elf", type=Path,
                        help="validate a modern fixed-address payload against its ELF")
    args = parser.parse_args()
    code = args.code.read_bytes()
    payload = make_payload(code, integrity=args.integrity, chain=args.chain,
                           modern_elf=args.modern_elf.read_bytes() if args.modern_elf else None)
    carrier = build(payload, CE_BASE, CE_BASE)
    validate_cold_parser(carrier)
    args.output.write_bytes(carrier)
    print(f"validated ECEC/ROMHDR/nk.exe carrier: {args.output}")
    print(f"image bytes={len(carrier)}; U-Boot physical entry=0x{PA_BASE + CODE_OFFSET:08x}")
    if args.integrity:
        print(f"early-boot CRC32=0x{zlib.crc32(code):08x}; code bytes={len(code)}")
    print("This utility did not open a device or write NAND.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
