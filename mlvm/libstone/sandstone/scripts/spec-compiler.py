#!/usr/bin/env python3
"""Compile Sandstone `.dsymspec` specifications into SymAS (`.symas`) banks.

Pipeline (see compiler/stdspec/README.md):

    .dsymspec (Lua DSL)  ->  JSON document  ->  Builder  ->  .symas
    (stock Lua + dsl.lua)    (this script)     (sandstone-spec-compiler)

Each `.dsymspec` file requires the `dsl` module, describes one
architecture's registers, features, encodings, syntaxes, sections,
relocations and (optionally) optimization records, and prints a JSON
document on stdout. This script collects those documents and hands them to
the `sandstone-spec-compiler` binary, which maps them onto the databank
builder and writes the compressed `.symas` file. Several inputs may be
folded into one bank.

Usage:
    python3 scripts/spec-compiler.py [--compiler PATH] [-o OUT.symas]
        [--compression none|zlib|zstd] [--validate] [--emit-spec [FILE]]
        [--spec-dir DIR] [--lua LUA] inputs...

    python3 scripts/spec-compiler.py --list-arches [--spec-dir DIR]
"""

import argparse
import glob
import os
import shutil
import subprocess
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_STDSPEC = os.path.join(REPO, "compiler", "stdspec")


def find_compiler(explicit):
    """Locate the sandstone-spec-compiler binary."""
    if explicit:
        return explicit
    found = shutil.which("sandstone-spec-compiler")
    if found:
        return found
    # Adjacent build trees: <repo>/build/.../sandstone-spec-compiler.
    for root, _dirs, files in os.walk(os.path.join(REPO, "build")):
        if "sandstone-spec-compiler" in files:
            return os.path.join(root, "sandstone-spec-compiler")
    return "sandstone-spec-compiler"  # let the exec fail with a clear error


def find_lua(explicit):
    if explicit:
        return explicit
    for candidate in ("lua", "lua5.4", "lua5.3"):
        if shutil.which(candidate):
            return candidate
    return os.environ.get("LUA", "lua")


def run_spec(lua, spec, spec_dir):
    """Run one .dsymspec under stock Lua; return its JSON document."""
    env = dict(os.environ)
    # The DSL module lives next to the specs (dsl.lua) and beside the
    # spec file itself; both are added to LUA_PATH.
    path_entries = []
    parent = os.path.dirname(os.path.abspath(spec))
    if parent:
        path_entries.append(os.path.join(parent, "?.lua"))
    if spec_dir:
        path_entries.append(os.path.join(os.path.abspath(spec_dir), "?.lua"))
    if env.get("LUA_PATH"):
        path_entries.append(env["LUA_PATH"])
    else:
        path_entries.append(";;")
    env["LUA_PATH"] = ";".join(path_entries)
    proc = subprocess.run([lua, os.path.abspath(spec)], cwd=spec_dir or parent,
                          capture_output=True, text=True, env=env)
    if proc.returncode != 0:
        sys.stderr.write("error: %s: %s\n"
                         % (spec, proc.stderr.strip() or "lua failed"))
        sys.exit(1)
    doc = proc.stdout.strip()
    if not doc:
        sys.stderr.write("error: %s: spec produced no output "
                         "(expected a JSON document on stdout)\n" % spec)
        sys.exit(1)
    if ')JSON"' in doc:
        sys.stderr.write("error: %s: JSON contains raw-string delimiter\n"
                         % spec)
        sys.exit(1)
    return doc


def list_arches(spec_dir):
    specs = sorted(glob.glob(os.path.join(spec_dir, "*.dsymspec")))
    if not specs:
        sys.stderr.write("error: no .dsymspec files in %s\n" % spec_dir)
        return 1
    for spec in specs:
        print(os.path.splitext(os.path.basename(spec))[0])
    return 0


def main(argv=None):
    parser = argparse.ArgumentParser(
        description="Compile .dsymspec specifications into a .symas databank.")
    parser.add_argument("inputs", nargs="*", help=".dsymspec files to compile")
    parser.add_argument("-o", "--output", default=None,
                        help="output .symas path (required with several inputs)")
    parser.add_argument("--compiler", default=None,
                        help="path to the sandstone-spec-compiler binary")
    parser.add_argument("--lua", default=None,
                        help="Lua interpreter for the DSL step")
    parser.add_argument("--spec-dir", default=None,
                        help="directory containing dsl.lua and standard specs")
    parser.add_argument("--compression", default="zlib",
                        choices=("none", "zlib", "zstd"),
                        help="payload compression (default: zlib)")
    parser.add_argument("--validate", action="store_true",
                        help="validate the output bank")
    parser.add_argument("--emit-spec", nargs="?", const="-", default=None,
                        metavar="FILE",
                        help="write the merged JSON document to FILE "
                             "('-' = stdout) instead of building a bank")
    parser.add_argument("--list-arches", action="store_true",
                        help="list available .dsymspec files and exit")
    args = parser.parse_args(argv)

    spec_dir = args.spec_dir or DEFAULT_STDSPEC

    if args.list_arches:
        return list_arches(spec_dir)

    if not args.inputs:
        parser.error("at least one .dsymspec input is required")

    lua = find_lua(args.lua)
    documents = [run_spec(lua, spec, spec_dir) for spec in args.inputs]

    if args.emit_spec is not None:
        joined = "\n".join(documents) + "\n"
        if args.emit_spec == "-":
            sys.stdout.write(joined)
        else:
            with open(args.emit_spec, "w", encoding="utf-8") as f:
                f.write(joined)
        return 0

    compiler = find_compiler(args.compiler)
    cmd = [compiler, "--compression", args.compression]
    if args.validate:
        cmd.append("--validate")
    if args.output:
        cmd += ["-o", args.output]
    elif len(args.inputs) == 1:
        src = args.inputs[0]
        if src.endswith(".dsymspec"):
            cmd += ["-o", src[: -len(".dsymspec")] + ".symas"]
        else:
            cmd += ["-o", src + ".symas"]

    # Hand the JSON documents to the compiler as temporary files so that
    # banks built from several specs keep document boundaries intact.
    import tempfile
    tmpdir = tempfile.mkdtemp(prefix="sandstone-spec-")
    try:
        for i, doc in enumerate(documents):
            path = os.path.join(tmpdir, "spec%d.json" % i)
            with open(path, "w", encoding="utf-8") as f:
                f.write(doc)
            cmd.append(path)
        proc = subprocess.run(cmd)
        return proc.returncode
    finally:
        for i in range(len(documents)):
            try:
                os.unlink(os.path.join(tmpdir, "spec%d.json" % i))
            except OSError:
                pass
        try:
            os.rmdir(tmpdir)
        except OSError:
            pass


if __name__ == "__main__":
    sys.exit(main())
