#!/usr/bin/env python3
"""Check a compiled debugger ELF, its C startup, LED transitions and reset.

This needs arm-none-eabi-gcc only at build time. It does not manufacture an ELF
or silently substitute the Python assembly fixtures for the C application.
"""
import argparse
from pathlib import Path
import re
import struct
import subprocess
import tempfile


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
    parser.add_argument("--cli", required=True, type=Path)
    parser.add_argument("--elf", required=True, type=Path)
    parser.add_argument("--board", choices=("pico", "pico2"), required=True)
    args = parser.parse_args()
    check_elf_layout(args.elf)
    chip = "rp2350" if args.board == "pico2" else "rp2040"
    oe = "0xd0000030" if args.board == "pico2" else "0xd0000020"

    observations = []
    commands = []

    def expect(expression, value):
        commands.append("print " + expression)
        observations.append((expression, value))

    expect("read32(data_cookie)", 0)  # Loader must not pre-copy .data.
    expect("pc ^ (Reset_Handler & ~1)", 0)
    commands.extend(["break main", "run 100000"])
    expect("pc ^ (main & ~1)", 0)
    expect("read32(data_cookie)", 0x5049434f)
    expect("read32(bss_cookie)", 0)
    expect("read32(boot_stage)", 0)  # Stopped before the first main instruction.
    expect("read32(0xe000ed08)", 0x10000200)
    commands.extend(["delete main", "break blink_checkpoint"])
    for transition in range(1, 5):
        commands.append("run 100000")
        expect("pc ^ (blink_checkpoint & ~1)", 0)
        expect("read32(boot_stage)", 3)
        expect("read32(boot_error)", 0)
        expect("read32(half_period_ms)", 1)
        expect("read32(led_state)", transition % 2)
        expect("read32(led_transitions)", transition)
        expect("read32(blink_count)", transition // 2)
        expect("read32(0xd0000010) & 0x02000000", (transition % 2) << 25)
        expect(f"read32({oe}) & 0x02000000", 1 << 25)
    commands.append("reset")
    expect("read32(data_cookie)", 0)
    expect("read32(led_transitions)", 0)
    commands.append("run 100000")  # Breakpoint survives reset.
    expect("pc ^ (blink_checkpoint & ~1)", 0)
    expect("read32(data_cookie)", 0x5049434f)
    expect("read32(led_transitions)", 1)
    commands.append("quit")

    with tempfile.TemporaryDirectory(prefix="picodbg-boot-blink-") as directory:
        script = Path(directory) / "session.txt"
        script.write_text("\n".join(commands) + "\n", encoding="utf-8")
        result = subprocess.run([str(args.cli.resolve()), "--chip", chip,
                                 "--elf", str(args.elf.resolve()), "--batch", str(script)],
                                text=True, capture_output=True, timeout=30)
    assert result.returncode == 0, result.stdout + result.stderr
    actual = re.findall(r"^(.+) = 0x([0-9a-f]+) \([0-9]+\)$", result.stdout, re.MULTILINE)
    actual = [(expression, int(value, 16)) for expression, value in actual]
    assert actual == observations, result.stdout + result.stderr
    print(f"{chip}: C startup, .data, .bss, 4 LED transitions and reset passed")


if __name__ == "__main__":
    main()
