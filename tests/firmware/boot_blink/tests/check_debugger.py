#!/usr/bin/env python3
"""Check a compiled debugger ELF, its C startup, LED transitions and reset.

This needs arm-none-eabi-gcc only at build time. It does not manufacture an ELF
or silently substitute the Python assembly fixtures for the C application.
"""
import argparse
from pathlib import Path
import struct
import subprocess


def check_elf_layout(path):
    image = path.read_bytes()
    header = struct.unpack_from("<16sHHIIIIIHHHHHH", image)
    ident, kind, machine, _, entry, phoff, _, _, _, phsize, phcount, _, _, _ = header
    assert ident[:7] == b"\x7fELF\x01\x01\x01", "Expected ELF32 little-endian"
    assert kind == 2 and machine == 40 and entry & 1, "Expected linked Thumb ARM executable"
    assert phsize == 32
    segments = [struct.unpack_from("<8I", image, phoff + index * phsize)
                for index in range(phcount)]
    assert any(kind == 1 and size and 0x20000000 <= virtual < 0x20082000
               and 0x10000000 <= physical < 0x10400000
               for kind, _, virtual, physical, size, _, _, _ in segments), \
        "Expected .data in SRAM with a Flash load address (startup must copy it)"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runner", required=True, type=Path,
                        help="Path to the picodbg_elf_tests integration test executable")
    parser.add_argument("--elf", required=True, type=Path)
    parser.add_argument("--board", choices=("pico", "pico2"), required=True)
    args = parser.parse_args()
    check_elf_layout(args.elf)
    chip = "rp2350" if args.board == "pico2" else "rp2040"
    subprocess.run([str(args.runner.resolve()), "boot_blink", chip, str(args.elf.resolve())],
                   timeout=30, check=True)


if __name__ == "__main__":
    main()
