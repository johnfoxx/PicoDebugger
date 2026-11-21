#!/usr/bin/env python3
"""Install selected SDK tools into an application-owned directory."""

import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import signal
import stat
import subprocess
import sys
import tarfile
import tempfile
from urllib.parse import urlsplit
from urllib.request import Request, urlopen
import zipfile


def component(value):
    if not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9._+-]{0,95}", value):
        raise ValueError(f"Identifiant de paquet invalide : {value}")
    return value


def https(url):
    parsed = urlsplit(url)
    if parsed.scheme != "https" or not parsed.hostname or parsed.username is not None:
        raise ValueError(f"Le catalogue doit indiquer une URL HTTPS : {url}")
    return url


def request(url):
    response = urlopen(Request(https(url), headers={"User-Agent": "PicoDebugger"}), timeout=45)
    try:
        https(response.geturl())
    except Exception:
        response.close()
        raise
    return response


def archive_digest(asset):
    expected = asset.get("sha256", "").lower()
    if not expected and asset.get("checksumUrl"):
        with request(asset["checksumUrl"]) as response:
            checksum = response.read(16385).decode("ascii")
        match = re.search(r"(?m)^\s*([0-9a-fA-F]{64})(?:\s|$)", checksum[:16384])
        expected = match.group(1).lower() if match else ""
    if not re.fullmatch(r"[0-9a-f]{64}", expected):
        raise ValueError("Le catalogue doit fournir une empreinte SHA-256 valide.")
    return expected


def download(url, archive, expected):
    digest = hashlib.sha256()
    total = 0
    with request(url) as response, archive.open("wb") as output:
        while True:
            chunk = response.read(1024 * 1024)
            if not chunk:
                break
            total += len(chunk)
            if total > 2 * 1024 ** 3:
                raise ValueError("Archive de plus de 2 Gio.")
            output.write(chunk)
            digest.update(chunk)
            if total // (32 * 1024 ** 2) != (total - len(chunk)) // (32 * 1024 ** 2):
                print(f"  {total // (1024 ** 2)} Mio téléchargés", flush=True)
    if digest.hexdigest() != expected:
        raise ValueError("L'empreinte SHA-256 de l'archive ne correspond pas au catalogue.")


def relative(path):
    value = PurePosixPath(path)
    if not path or "\\" in path or value.is_absolute() or ".." in value.parts:
        raise ValueError(f"Chemin d'archive invalide : {path}")
    return value


def extract(archive, destination):
    print("  Extraction de l'archive vérifiée", flush=True)
    if zipfile.is_zipfile(archive):
        with zipfile.ZipFile(archive) as compressed:
            if sum(item.file_size for item in compressed.infolist()) > 8 * 1024 ** 3:
                raise ValueError("Contenu ZIP de plus de 8 Gio.")
            for item in compressed.infolist():
                relative(item.filename)
                mode = item.external_attr >> 16
                if stat.S_ISLNK(mode):
                    raise ValueError("Lien symbolique inattendu dans l'archive ZIP.")
                path = Path(compressed.extract(item, destination))
                if mode & 0o111 and path.is_file():
                    path.chmod(0o755)
    else:
        if not hasattr(tarfile, "data_filter"):
            raise ValueError("Un Python disposant du filtre d'extraction tarfile.data_filter est requis.")
        with tarfile.open(archive) as compressed:
            members = compressed.getmembers()
            if sum(member.size for member in members) > 8 * 1024 ** 3:
                raise ValueError("Contenu TAR de plus de 8 Gio.")
            compressed.extractall(destination, members, filter="data")


def git_checkout(repository, version, commit, destination, git):
    if not re.fullmatch(r"[0-9a-f]{40}", commit):
        raise ValueError("Le catalogue doit fixer le commit du Pico SDK.")
    base = [git, "-c", "protocol.file.allow=never", "-c", "protocol.ext.allow=never"]
    environment = {**os.environ, "GIT_TERMINAL_PROMPT": "0"}
    subprocess.run(base + ["clone", "--depth=1", "--branch", version, "--", https(repository), str(destination)],
                   check=True, env=environment)
    actual = subprocess.check_output(base + ["rev-parse", "HEAD"], cwd=destination, text=True).strip()
    if actual != commit:
        raise ValueError("Le tag du SDK ne correspond pas au commit du catalogue.")
    subprocess.run(base + ["submodule", "update", "--init", "--recursive", "--depth=1"],
                   cwd=destination, check=True, env=environment)


def tool_paths(folder, provides):
    paths = {}
    for key, pattern in provides.items():
        relative(pattern)
        matches = [folder] if pattern == "." else sorted(folder.glob(pattern))
        matches = [path for path in matches if path.exists() and path.resolve().is_relative_to(folder.resolve())]
        if len(matches) != 1:
            raise ValueError(f"Élément attendu introuvable ou ambigu : {pattern}")
        path = matches[0]
        if key not in ("sdk", "openocdScripts"):
            if not path.is_file():
                raise ValueError(f"Le fichier attendu est absent : {pattern}")
            if not os.access(path, os.X_OK):
                path.chmod(0o755)
        paths[key] = str(path.absolute())
    return paths


def local_directory(root, name):
    folder = root / name
    folder.mkdir(exist_ok=True)
    if folder.is_symlink() or folder.resolve().parent != root.resolve():
        raise ValueError(f"Répertoire local invalide : {folder}")
    return folder


def register(root, package_id, paths):
    folder = local_directory(root, "registered")
    with tempfile.NamedTemporaryFile("w", encoding="utf-8", dir=folder, delete=False) as output:
        temp = Path(output.name)
        try:
            json.dump(paths, output, ensure_ascii=False)
            output.flush()
            os.replace(temp, folder / f"{package_id}.json")
        finally:
            temp.unlink(missing_ok=True)


def install(root, package, platform, git):
    name = component(package["id"])
    version = component(package["version"])
    asset = package.get("platforms", {}).get(platform, {})
    repository = package.get("repository")
    print(f"{package['name']} {version}", flush=True)
    fingerprint = package["commit"] if repository else archive_digest(asset)
    if not re.fullmatch(r"[0-9a-f]{40,64}", fingerprint):
        raise ValueError("Empreinte de version invalide.")
    parent = local_directory(root, name)
    target = parent / f"{version}-{fingerprint[:12]}"
    if target.is_symlink():
        raise ValueError(f"Répertoire de version invalide : {target}")
    if not target.exists():
        with tempfile.TemporaryDirectory(prefix=".install-", dir=parent) as temp:
            temp = Path(temp)
            staged = temp / "package"
            if repository:
                git_checkout(repository, version, fingerprint, staged, git)
            else:
                staged.mkdir()
                archive = temp / "download"
                download(asset["url"], archive, fingerprint)
                extract(archive, staged)
            tool_paths(staged, package["provides"])
            try:
                staged.rename(target)
            except OSError:
                if not target.is_dir():
                    raise
    register(root, name, tool_paths(target, package["provides"]))
    print(f"  Installé : {target}", flush=True)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--catalog", required=True, type=Path)
    parser.add_argument("--prefix", required=True, type=Path)
    parser.add_argument("--platform", required=True)
    parser.add_argument("--git", default="git")
    parser.add_argument("packages", nargs="+")
    args = parser.parse_args()
    config = json.loads(args.catalog.read_text(encoding="utf-8"))
    if config.get("schema") != 1:
        raise ValueError("Version de catalogue incompatible.")
    packages = {package["id"]: package for package in config["packages"]}
    args.prefix.mkdir(parents=True, exist_ok=True)
    failed = False
    for name in dict.fromkeys(args.packages):
        try:
            install(args.prefix, packages[name], args.platform, args.git)
        except (OSError, ValueError, KeyError, subprocess.CalledProcessError, tarfile.TarError, zipfile.BadZipFile) as error:
            failed = True
            print(f"{name} · {error}", file=sys.stderr, flush=True)
    return int(failed)


def cancel(_signal, _frame):
    raise KeyboardInterrupt()


if __name__ == "__main__":
    if sys.version_info < (3, 9):
        sys.exit("L'installation locale nécessite Python 3.9 ou supérieur.")
    signal.signal(signal.SIGTERM, cancel)
    try:
        sys.exit(main())
    except KeyboardInterrupt:
        print("Installation annulée.", file=sys.stderr, flush=True)
        sys.exit(2)
    except (OSError, ValueError, KeyError) as error:
        print(error, file=sys.stderr, flush=True)
        sys.exit(1)
