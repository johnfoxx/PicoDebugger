"""Supply an ELF file to the hardware-protocol tests, without a physical probe."""
from pathlib import Path
import os
import subprocess
import struct
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'examples'))
from make_demo import make_elf

with tempfile.TemporaryDirectory(prefix='picodbg-probe-tests-') as directory:
    root = Path(directory)
    elf = root / 'démo avec espaces.elf'
    elf.write_bytes(make_elf())
    # Relocate the fixture's flash addresses into SRAM, including vectors,
    # program/section headers and symbols. Ordinary RAM data stays unchanged.
    data = bytearray(make_elf())
    def relocate(offset):
        value, = struct.unpack_from('<I', data, offset)
        if 0x10000000 <= value < 0x10010000:
            struct.pack_into('<I', data, offset, value + 0x10000000)
    for offset in (24, 60, 64, 0x104, 0x10c, 0x12c, 0x13c):
        relocate(offset)
    shoff, = struct.unpack_from('<I', data, 32)
    for index in range(6):
        section = shoff + index * 40
        relocate(section + 12)
        kind, = struct.unpack_from('<I', data, section + 4)
        if kind == 2:
            offset, size = struct.unpack_from('<II', data, section + 16)
            for symbol in range(offset, offset + size, 16):
                relocate(symbol + 4)
    ram = root / 'ram vectors.elf'
    ram.write_bytes(data)
    struct.pack_into('<I', data, 0x100, 0x30000000)
    invalid = root / 'bad ram vectors.elf'
    invalid.write_bytes(data)
    env = dict(os.environ, PICODBG_TEST_GDB_LOG=str(root / 'commands.log'),
               PICODBG_TEST_SERVER_LOG=str(root / 'server.json'))
    subprocess.run([sys.argv[1], str(Path(__file__).with_name('fake_gdb.py')), str(elf), str(ram), str(invalid),
                    str(Path(__file__).with_name('fake_openocd.py'))], env=env, check=True, timeout=55)
