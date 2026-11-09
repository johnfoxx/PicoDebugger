#!/usr/bin/env python3
"""Cache official references with provenance, without adding vendor files to Git.

The SDK tag is deliberately pinned. Datasheet URLs can change their contents;
SHA-256 records the actual downloaded revision, not a claim of authenticity.
"""
import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import sys
import tempfile
from urllib.request import Request, urlopen

ROOT = Path(__file__).resolve().parent.parent
SDK = "https://raw.githubusercontent.com/raspberrypi/pico-sdk/2.2.0/src"
DOCUMENTS = {
    "rp2040-datasheet": ("rp2040-datasheet.pdf", "https://datasheets.raspberrypi.com/rp2040/rp2040-datasheet.pdf"),
    "rp2350-datasheet": ("rp2350-datasheet.pdf", "https://datasheets.raspberrypi.com/rp2350/rp2350-datasheet.pdf"),
}
for chip in ("rp2040", "rp2350"):
    for header in ("addressmap", "timer", "uart", "spi", "pwm", "sio", "intctrl", "resets", "watchdog", "adc", "i2c", "dma", "pio"):
        DOCUMENTS[chip + "-" + header] = (
            chip + "-" + header + "-sdk-2.2.0.h",
            SDK + "/" + chip + "/hardware_regs/include/hardware/regs/" + header + ".h",
        )


def fetch(directory, filename, url, timeout):
    """Keep any previous cache intact if a download is interrupted or invalid."""
    temporary = None
    try:
        request = Request(url, headers={"User-Agent": "PicoDebugger-reference-cache/1"})
        with urlopen(request, timeout=timeout) as response, tempfile.NamedTemporaryFile(
            dir=str(directory), prefix=".download-", delete=False
        ) as output:
            temporary = Path(output.name)
            digest, length, prefix = hashlib.sha256(), 0, b""
            while True:
                chunk = response.read(64 * 1024)
                if not chunk:
                    break
                if not prefix:
                    prefix = chunk[:1024]
                length += len(chunk)
                if length > 128 * 1024 * 1024:
                    raise ValueError("Réponse supérieure à 128 Mio")
                digest.update(chunk)
                output.write(chunk)
            if filename.endswith(".pdf"):
                valid = prefix.startswith(b"%PDF-")
            else:
                valid = b"SPDX-License-Identifier: BSD-3-Clause" in prefix
            if not valid:
                raise ValueError("Format inattendu : document vide ou page d'erreur")
            resolved_url = response.geturl()
        temporary.replace(directory / filename)
        return {
            "file": filename, "url": url, "resolved_url": resolved_url,
            "sha256": digest.hexdigest(), "bytes": length,
            "retrieved_utc": datetime.now(timezone.utc).isoformat(),
        }
    finally:
        if temporary and temporary.exists():
            temporary.unlink()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("documents", nargs="*", help="Identifiants à récupérer (tous par défaut)")
    parser.add_argument("--list", action="store_true", help="Afficher les sources sans accès réseau")
    parser.add_argument("--output", type=Path, default=ROOT / "docs" / "reference-cache")
    parser.add_argument("--timeout", type=float, default=20, help="Timeout réseau en secondes")
    parser.add_argument("--refresh", action="store_true", help="Remplacer les documents déjà vérifiés")
    args = parser.parse_args()
    if args.timeout <= 0:
        parser.error("Le timeout doit être positif")
    names = args.documents or list(DOCUMENTS)
    for name in names:
        if name not in DOCUMENTS:
            parser.error("Document inconnu : " + name)
    if args.list:
        for name in names:
            print(name + "\n  " + DOCUMENTS[name][1])
        return 0
    args.output.mkdir(parents=True, exist_ok=True)
    manifest_path = args.output / "manifest.json"
    manifest = json.loads(manifest_path.read_text()) if manifest_path.exists() else {}
    failed = False
    for name in names:
        filename, url = DOCUMENTS[name]
        try:
            old = manifest.get(name, {})
            target = args.output / filename
            if not args.refresh and old.get("url") == url and target.exists():
                if hashlib.sha256(target.read_bytes()).hexdigest() == old.get("sha256"):
                    print(name + " : cache vérifié")
                    continue
            manifest[name] = fetch(args.output, filename, url, args.timeout)
            # A manifest entry is created only after a successful, validated
            # download. Failed transfers cannot masquerade as retrieved docs.
            temporary = args.output / ".manifest.tmp"
            temporary.write_text(json.dumps(manifest, indent=2) + "\n")
            temporary.replace(manifest_path)
            print(name + " : récupéré, SHA-256 " + manifest[name]["sha256"])
        except (OSError, ValueError) as error:
            failed = True
            print(name + " : échec — " + str(error), file=sys.stderr)
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
