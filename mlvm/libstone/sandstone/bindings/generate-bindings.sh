#!/usr/bin/env bash
# Generate Sandstone language bindings with SWIG 4.0+.
#
# Usage:
#   bindings/generate-bindings.sh [--language LANG]... [--all]
#       [--out DIR] [--swig PATH] [--build] [-- python/setup args...]
#
# Languages: python (default), lua, java. --all generates all three.
# Output: $OUT/<lang>/ contains the SWIG wrapper plus a small smoke test.
# --build additionally compiles the Python extension in place (needs the
# Sandstone static library and Python development headers).
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
INTERFACE="$REPO/bindings/sandstone.i"
OUT="$REPO/build/bindings"
SWIG="${SWIG:-swig}"
BUILD=0
declare -a LANGS=()
declare -a EXTRA=()

while [[ $# -gt 0 ]]; do
    case "$1" in
        --language) LANGS+=("$2"); shift 2 ;;
        --all) LANGS=(python lua java); shift ;;
        --out) OUT="$2"; shift 2 ;;
        --swig) SWIG="$2"; shift 2 ;;
        --build) BUILD=1; shift ;;
        "--") shift; EXTRA+=("$@"); break ;;
        -h|--help)
            sed -n '2,12p' "$0"; exit 0 ;;
        *) echo "unknown option: $1" >&2; exit 2 ;;
    esac
done
[[ ${#LANGS[@]} -eq 0 ]] && LANGS=(python)

if ! command -v "$SWIG" >/dev/null 2>&1; then
    echo "error: swig not found (set SWIG=path or install SWIG >= 4.0)" >&2
    exit 1
fi
SWIG_VERSION="$("$SWIG" -version 2>/dev/null | awk '/SWIG Version/ {print $3}')"
echo "swig: $SWIG ($SWIG_VERSION) interface: $INTERFACE"

gen_python() {
    local dir="$OUT/python"
    mkdir -p "$dir"
    "$SWIG" -python -I"$REPO/include" -I"$REPO/assembler" -o "$dir/sandstone_wrap.c" \
        -outdir "$dir" "$INTERFACE"
    cat > "$dir/smoke.py" <<'EOF'
"""Smoke-test the generated bindings against the built library."""
import sandstone as s

print("magic: 0x%06x" % s.domsymas_magic())
print("version:", s.domsymas_version())
probe = bytes([0xCE, 0xEB, 0x09]) + bytes(64)
assert s.domsymas_probe_magic(probe) == 1
arr = s.new_sandstone_u32array(8)
assert arr is not None
s.delete_sandstone_u32array(arr)
print("BINDINGS_OK")
EOF
    echo "python bindings: $dir"
}

gen_lua() {
    local dir="$OUT/lua"
    mkdir -p "$dir"
    "$SWIG" -lua -I"$REPO/include" -I"$REPO/assembler" -o "$dir/sandstone_wrap.c" \
        -outdir "$dir" "$INTERFACE"
    echo "lua bindings: $dir"
}

gen_java() {
    local dir="$OUT/java"
    mkdir -p "$dir"
    "$SWIG" -java -package org.sandstone -I"$REPO/include" -I"$REPO/assembler" \
        -o "$dir/sandstone_wrap.c" -outdir "$dir" "$INTERFACE"
    echo "java bindings: $dir"
}

for lang in "${LANGS[@]}"; do
    case "$lang" in
        python) gen_python ;;
        lua) gen_lua ;;
        java) gen_java ;;
        *) echo "unknown language: $lang (python, lua, java)" >&2; exit 2 ;;
    esac
done

if [[ $BUILD -eq 1 ]]; then
    if [[ ! " ${LANGS[*]} " == *" python "* ]]; then
        echo "error: --build currently supports the python language" >&2
        exit 2
    fi
    if ! command -v python3 >/dev/null 2>&1; then
        echo "error: python3 not found" >&2; exit 1
    fi
    # Locate built static libraries next to this script's build tree.
    LIB="$(find "$REPO/build" -name 'libsandstone.a' 2>/dev/null | head -n 1 || true)"
    ALIB="$(find "$REPO/build" -name 'libsandstone_assembler.a' 2>/dev/null | head -n 1 || true)"
    if [[ -z "$LIB" ]]; then
        echo "error: libsandstone.a not found under $REPO/build;" >&2
        echo "build the project first (cmake --build <dir>)" >&2
        exit 1
    fi
    PYINC="$(python3 -c 'import sysconfig; print(sysconfig.get_paths()["include"])')"
    PYEXT="$(python3 -c 'import sysconfig; print(sysconfig.get_config_var("EXT_SUFFIX"))')"
    ZSTDDEP=""
    if nm "$LIB" 2>/dev/null | grep "ZSTD_decompress" >/dev/null; then
        ZSTDDEP="-lzstd"
    fi
    # shellcheck disable=SC2068
    "${CXX:-c++}" -O2 -fPIC -shared -I"$REPO/include" -I"$REPO/assembler" \
        -I"$PYINC" \
        "$OUT/python/sandstone_wrap.c" $LIB ${ALIB:+"$ALIB"} -lz $ZSTDDEP -o \
        "$OUT/python/_sandstone$PYEXT" ${EXTRA[@]+"${EXTRA[@]}"}
    echo "built: $OUT/python/_sandstone$PYEXT"
fi
