"""Read-only adapter to upstream Optilisp's source-located S-expression reader."""

import importlib.util
from pathlib import Path
import sys
import types

ROOT = Path(__file__).resolve().parents[2]
UPSTREAM = ROOT / "libstone/blackstone"
PACKAGE = "_mlvm_blackstone_reader"


def reader():
    # Load only the reader and diagnostics, without upstream __init__ side effects,
    # path mutation, bytecode writes, or copying dependency sources.
    if PACKAGE + ".sexpr" in sys.modules:
        return sys.modules[PACKAGE + ".sexpr"]
    package = types.ModuleType(PACKAGE)
    package.__path__ = []
    sys.modules[PACKAGE] = package
    for name in ("errors", "sexpr"):
        path = UPSTREAM / "optilisp/src/optilisp" / (name + ".py")
        spec = importlib.util.spec_from_file_location(PACKAGE + "." + name, path)
        module = importlib.util.module_from_spec(spec)
        sys.modules[spec.name] = module
        # compile/exec bypasses importlib bytecode caching in the read-only tree.
        exec(compile(path.read_text(encoding="utf-8"), str(path), "exec"), module.__dict__)
    return sys.modules[PACKAGE + ".sexpr"]
