#!/usr/bin/env bash
set -euo pipefail
cd -- "$(dirname -- "${BASH_SOURCE[0]}")"
cmake --preset linux
cmake --build --preset linux --parallel 2
ctest --preset linux
