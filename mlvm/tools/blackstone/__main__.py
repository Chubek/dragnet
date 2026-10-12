"""Catalog validation, deterministic querying, and upstream audit commands."""

import argparse
from collections import Counter
import json
import sys

from .catalog import load


def main(argv=None):
    parser = argparse.ArgumentParser(prog="python -m tools.blackstone")
    parser.add_argument("--version", action="version", version="MLVM Blackstone extension 1.0")
    parser.add_argument("command", choices=["validate", "query", "audit"])
    parser.add_argument("--scope", choices=["supplement", "all"], default=None)
    parser.add_argument("--stage", choices=["hir", "mir", "lir"])
    parser.add_argument("--category")
    parser.add_argument("--requires-analysis")
    parser.add_argument("--json", action="store_true")
    args = parser.parse_args(argv)
    scope = args.scope or ("all" if args.command == "audit" else "supplement")
    try:
        catalog = load(scope)
    except (OSError, UnicodeError) as exc:
        print(f"blackstone: input error: {exc}", file=sys.stderr)
        return 3
    records = [k for _, k in sorted(catalog.kernels.items())
               if (not args.stage or k["stage"] == args.stage)
               and (not args.category or args.category in k["category"])
               and (not args.requires_analysis or ("analysis", args.requires_analysis) in k["requirements"])]
    if args.command == "query":
        if catalog.issues:
            for issue in catalog.issues:
                print(issue.render(), file=sys.stderr)
            return 1
        if args.json:
            print(json.dumps(records, sort_keys=True, indent=2))
        else:
            for record in records:
                print(record["id"])
    else:
        report = {"scope": scope, "kernels": len(catalog.kernels),
                  "stages": dict(sorted(Counter(k["stage"] for k in catalog.kernels.values()).items())),
                  "issues_by_code": dict(sorted(Counter(i.code for i in catalog.issues).items())),
                  "issues": [dict(file=i.file, line=i.line, column=i.column, code=i.code, message=i.message) for i in catalog.issues],
                  "evidence": "Static contract checks only; no NodeIR equivalence proof."}
        if args.json:
            print(json.dumps(report, sort_keys=True, indent=2))
        else:
            for issue in catalog.issues:
                print(issue.render(), file=sys.stderr)
            print(f"{scope}: {len(catalog.kernels)} kernels; {len(catalog.issues)} contract errors")
    return int(bool(catalog.issues))


if __name__ == "__main__":
    raise SystemExit(main())
