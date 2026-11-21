#!/usr/bin/env bash
# Build and verify the Qt desktop application explicitly.
set -euo pipefail
project_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
cmake -S "$project_dir" -B "$project_dir/build-gui" \
    -DCMAKE_BUILD_TYPE=Debug -DPICODBG_GUI=ON -DBUILD_TESTING=ON
cmake --build "$project_dir/build-gui" --parallel 2
ctest --test-dir "$project_dir/build-gui" --output-on-failure
if [[ ! -x "$project_dir/build-gui/pico-debugger" ]]; then
    printf 'Erreur : aucun exécutable graphique produit.\n' >&2
    exit 1
fi
printf '\nInterface prête : %s/build-gui/pico-debugger\n' "$project_dir"
if [[ "${1:-}" == "--launch" ]]; then
    exec "$project_dir/build-gui/pico-debugger"
fi
