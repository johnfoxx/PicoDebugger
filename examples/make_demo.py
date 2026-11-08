#!/usr/bin/env python3
"""ELF32 ARM fixture with real Thumb code, vectors and symbols.

No cross compiler required. This fixture is not a bootable UF2/Pico SDK firmware.
"""
import pathlib
import struct
import sys


def make_elf():
    base = 0x10000100
    payload = bytearray(0x160)
    for offset, value in [(0, 0x20041000), (4, 0x10000201),
                          (12, 0x10000241), (44, 0x10000241), (60, 0x10000241)]:
        struct.pack_into("<I", payload, offset, value)
    for offset, increment, target, bkpt in [(0x100, 1, 5, 0), (0x120, 2, 10, 1)]:
        code = [0x2000, 0x4903, 0x3000 | increment, 0x6008,
                0x2800 | target, 0xD1FB, 0xBE00 | bkpt, 0xE7FE]
        struct.pack_into("<8H", payload, offset, *code)
        struct.pack_into("<I", payload, offset + 16, 0x20000000 + bkpt * 4)
    struct.pack_into("<2H", payload, 0x140, 0xBE02, 0x4770)
    strings = bytearray(b"\0")
    symbols = bytearray(16)
    for name, value, info, section in [
        ("Reset_Handler", 0x10000201, 0x12, 2),
        ("core1_entry", 0x10000221, 0x12, 2),
        ("Fault_Handler", 0x10000241, 0x12, 2),
        ("counter", 0x20000000, 0x11, 0xFFF1),
        ("counter1", 0x20000004, 0x11, 0xFFF1),
        ("__vectors", base, 0x11, 1),
    ]:
        symbols.extend(struct.pack("<IIIBBH", len(strings), value, 0, info, 0, section))
        strings.extend(name.encode("ascii") + b"\0")
    section_names = bytearray(b"\0")
    name_offsets = {}
    for name in [".vectors", ".text", ".symtab", ".strtab", ".shstrtab"]:
        name_offsets[name] = len(section_names)
        section_names.extend(name.encode("ascii") + b"\0")
    image = bytearray(0x100) + payload
    sym_offset = len(image)
    image.extend(symbols)
    str_offset = len(image)
    image.extend(strings)
    names_offset = len(image)
    image.extend(section_names)
    image.extend(b"\0" * (-len(image) % 4))
    sh_offset = len(image)
    image.extend(bytes(40))

    def section(name, kind, flags, address, offset, size, link=0, info=0, alignment=4, entry_size=0):
        image.extend(struct.pack("<10I", name_offsets[name], kind, flags, address,
                                 offset, size, link, info, alignment, entry_size))

    section(".vectors", 1, 2, base, 0x100, 0x100)
    section(".text", 1, 6, base + 0x100, 0x200, 0x60)
    section(".symtab", 2, 0, 0, sym_offset, len(symbols), 4, 1, 4, 16)
    section(".strtab", 3, 0, 0, str_offset, len(strings), alignment=1)
    section(".shstrtab", 3, 0, 0, names_offset, len(section_names), alignment=1)
    ident = b"\x7fELF\x01\x01\x01" + bytes(9)
    struct.pack_into("<16sHHIIIIIHHHHHH", image, 0, ident, 2, 40, 1,
                     0x10000201, 52, sh_offset, 0x05000000, 52, 32, 1, 40, 6, 5)
    struct.pack_into("<8I", image, 52, 1, 0x100, base, base, len(payload), len(payload), 5, 4)
    return image


if __name__ == "__main__":
    if len(sys.argv) != 2:
        raise SystemExit("usage: python3 examples/make_demo.py chemin/demo.elf")
    output = pathlib.Path(sys.argv[1])
    output.write_bytes(make_elf())
    print(f"ELF ARM/Thumb : {output} ({output.stat().st_size} octets)")
