"""Exercise real ELF parsing, vector startup and actual CPU MMIO instructions."""
from pathlib import Path
import re
import subprocess
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "examples"))
from make_peripheral_demo import make_elf

with tempfile.TemporaryDirectory(prefix="picodbg-elf-") as directory:
    directory = Path(directory)
    for chip in ("rp2040", "rp2350"):
        pico2 = chip == "rp2350"
        timer_irq = 0x400B003C if pico2 else 0x40054034
        pwm_irq = 0x400A80F4 if pico2 else 0x400500A4
        image = directory / (chip + ".elf")
        image.write_bytes(make_elf(chip))
        script = directory / "commands.txt"
        script.write_text("run 1000\nprint read32(0x20000000)\nprint read32(0xd0000010)\n"
                          + f"print read32({hex(timer_irq)})\nprint read32({hex(pwm_irq)})\n")
        result = subprocess.run([sys.argv[1], "--chip", chip, "--elf", str(image), "--batch", str(script)],
                                text=True, capture_output=True, timeout=10, check=True)
        output = result.stdout + result.stderr
        assert "BKPT 3" in output, output
        assert "Boot ROM non exécutée" in output, output
        assert "read32(0xd0000010) = 0x02000000" in output, output
        assert f"read32({hex(timer_irq)}) = 0x00000001" in output, output
        assert f"read32({hex(pwm_irq)}) = 0x00000001" in output, output
        time = re.search(r"read32\(0x20000000\) = 0x([0-9a-f]+)", output)
        assert time and 80 <= int(time.group(1), 16) < 100, output
        print(chip + ": ELF/vector startup, UART instructions, GPIO, PWM and timer polling passed")
