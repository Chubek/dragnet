#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
build=${1:-"$root/build/python-bindings"}
if [ "$#" -gt 0 ]; then shift; fi
cmake -S "$root" -B "$build" -DLIMESTONE_BUILD_PYTHON_BINDINGS=ON "$@"
cmake --build "$build" --target limestone_python --parallel
