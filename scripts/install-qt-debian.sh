#!/usr/bin/env bash
# Run as the desktop user; sudo is used only for package installation.
set -euo pipefail
project_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
if ! command -v apt-get >/dev/null; then
    printf 'Ce script nécessite Debian/Ubuntu et apt-get.\n' >&2
    exit 1
fi
privilege=()
if [[ "$(id -u)" != 0 ]]; then
    privilege=(sudo)
fi
"${privilege[@]}" apt-get update
# Qt 5 is sufficient for Widgets and preserves the C++11 desktop build.
"${privilege[@]}" apt-get install -y qtbase5-dev qtbase5-dev-tools cmake g++ make
exec bash "$project_dir/scripts/build-gui.sh" "$@"
