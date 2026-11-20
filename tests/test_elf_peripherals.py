"""Exercise real ELF parsing, vector startup and actual CPU MMIO instructions."""
from pathlib import Path
import subprocess
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "examples"))
from make_peripheral_demo import make_elf

with tempfile.TemporaryDirectory(prefix="picodbg-elf-") as directory:
    directory = Path(directory)
    for chip in ("rp2040", "rp2350"):
        image = directory / (chip + ".elf")
        image.write_bytes(make_elf(chip))
        subprocess.run([sys.argv[1], "peripherals", chip, str(image)], timeout=10, check=True)
