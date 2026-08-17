"""Fingerprint the CPython AST surface a mod_ty transcriber is pinned to.

The whole version-pin question for M3 reduces to one measurable thing: what
in ``Include/internal/pycore_ast.h`` can change under a transcriber compiled
against it. Three things can, and all three are fatal if unnoticed:

* the ``*_kind`` enumerator VALUES (a node inserted in the middle renumbers
  every node after it -- CPython 3.14 did exactly that with
  ``Interpolation_kind``);
* the per-node struct FIELD LISTS (3.13 gave ``type_param`` a
  ``default_value``; 3.15 gives ``Import`` an ``is_lazy``);
* the constructor ARGUMENT ORDERS, which a generator reads to place operands.

This script parses one or more ``pycore_ast.h`` files and prints a stable
fingerprint plus a per-minor diff, so "which minors is this artifact valid
for" is an answer with evidence behind it rather than a hope. It is the
audit a real M3 build step runs before it emits the transcriber.

Usage:
    python3 audit_layouts.py 3.12=/path/pycore_ast.h 3.14=/path/pycore_ast.h
    python3 audit_layouts.py --fetch 3.12 3.13 3.14
"""

from __future__ import annotations

import argparse
import hashlib
import re
import sys
import urllib.request
from pathlib import Path

RAW = "https://raw.githubusercontent.com/python/cpython/{ref}/Include/internal/pycore_ast.h"

# Two shapes carry enumerators: the node-kind tags (`enum _expr_kind {...};`,
# the ones a renumbering breaks) and the ASDL sums that lower to plain ints
# (`typedef enum _expr_context {...} expr_context_ty;`).
_ENUM_RE = re.compile(
    r"(?:typedef\s+)?enum\s+_(\w+)\s*\{(.*?)\}\s*(?:\w+_ty)?\s*;", re.S
)
_CTOR_RE = re.compile(r"^(\w+_ty)\s+_PyAST_(\w+)\(([^;]*?)\);", re.M)


def fetch(minor: str) -> str:
    ref = "main" if minor in ("main", "3.15") else minor
    with urllib.request.urlopen(RAW.format(ref=ref), timeout=60) as fh:
        return fh.read().decode("utf-8")


def parse(text: str) -> dict:
    """Extract the three things that can move under a pinned transcriber."""
    kinds: dict[str, dict[str, int]] = {}
    for name, body in _ENUM_RE.findall(text):
        members: dict[str, int] = {}
        nxt = 0
        for raw in body.replace("\n", " ").split(","):
            raw = raw.strip()
            if not raw:
                continue
            if "=" in raw:
                mem, val = raw.split("=", 1)
                nxt = int(val.strip())
            else:
                mem = raw
            members[mem.strip()] = nxt
            nxt += 1
        if members:
            kinds[name] = members

    ctors: dict[str, list[str]] = {}
    for _ret, cls, sig in _CTOR_RE.findall(text):
        raw: list[str] = []
        for chunk in sig.replace("\n", " ").split(","):
            chunk = " ".join(chunk.split())
            if not chunk:
                continue
            raw.append(chunk.replace("*", " ").split()[-1])
        # Location attributes always arrive as the same trailing quadruple.
        # `lineno` alone is NOT a location: `TypeIgnore(int lineno, string
        # tag)` has it as a real data field, and a generator that strips the
        # name unconditionally drops a field CPython requires.
        loc = ("lineno", "col_offset", "end_lineno", "end_col_offset")
        if raw[-5:-1] == list(loc):
            raw = raw[:-5]
        elif raw[-1:] == ["arena"]:
            raw = raw[:-1]
        ctors[cls] = raw
    return {"kinds": kinds, "ctors": ctors}


def fingerprint(surface: dict) -> str:
    h = hashlib.sha256()
    for enum in sorted(surface["kinds"]):
        h.update(enum.encode())
        for mem, val in sorted(surface["kinds"][enum].items()):
            h.update(f"{mem}={val};".encode())
    for cls in sorted(surface["ctors"]):
        h.update(f"{cls}({','.join(surface['ctors'][cls])});".encode())
    return h.hexdigest()[:16]


def diff(older: dict, newer: dict, a: str, b: str) -> list[str]:
    out: list[str] = []
    for enum in sorted(set(older["kinds"]) | set(newer["kinds"])):
        om = older["kinds"].get(enum, {})
        nm = newer["kinds"].get(enum, {})
        renumbered = sorted(k for k in set(om) & set(nm) if om[k] != nm[k])
        if renumbered:
            out.append(
                f"  enum {enum}: {len(renumbered)} enumerator(s) RENUMBERED "
                f"(e.g. {renumbered[0]} {om[renumbered[0]]}->{nm[renumbered[0]]})"
            )
        for k in sorted(set(nm) - set(om)):
            out.append(f"  enum {enum}: + {k}={nm[k]}")
        for k in sorted(set(om) - set(nm)):
            out.append(f"  enum {enum}: - {k}")
    for cls in sorted(set(older["ctors"]) | set(newer["ctors"])):
        op = older["ctors"].get(cls)
        np_ = newer["ctors"].get(cls)
        if op is None:
            out.append(f"  node {cls}: NEW ({', '.join(np_ or [])})")
        elif np_ is None:
            out.append(f"  node {cls}: REMOVED")
        elif op != np_:
            out.append(f"  node {cls}: fields {op} -> {np_}")
    if not out:
        out.append("  (no change)")
    return [f"{a} -> {b}:"] + out


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("specs", nargs="*", help="MINOR=/path/to/pycore_ast.h")
    ap.add_argument("--fetch", nargs="*", metavar="MINOR",
                    help="download the header for these CPython minors")
    ap.add_argument("--check-running", metavar="MINOR",
                    help="assert that ast.<Cls>._fields on THIS interpreter "
                         "equals the constructor argument order in MINOR's "
                         "header -- the property that lets a generator read "
                         "field order from the `ast` module")
    args = ap.parse_args()

    sources: dict[str, str] = {}
    for spec in args.specs:
        minor, _, path = spec.partition("=")
        if not path:
            ap.error(f"expected MINOR=/path, got {spec!r}")
        sources[minor] = Path(path).read_text()
    for minor in args.fetch or []:
        sources[minor] = fetch(minor)
    if not sources:
        ap.error("give at least one header (positional MINOR=path or --fetch)")

    order = sorted(sources, key=lambda m: [int(p) for p in m.split(".")] if m[0].isdigit() else [99])
    surfaces = {m: parse(sources[m]) for m in order}

    print("AST surface fingerprints (kind enumerators + constructor field orders)")
    for m in order:
        s = surfaces[m]
        print(f"  {m:>6}: {fingerprint(s)}  {len(s['ctors'])} constructors, "
              f"{sum(len(v) for v in s['kinds'].values())} enumerators")
    print()
    for a, b in zip(order, order[1:]):
        for line in diff(surfaces[a], surfaces[b], a, b):
            print(line)
        print()
    if args.check_running:
        import ast as ast_mod

        target = surfaces[args.check_running]["ctors"]
        mismatched: list[str] = []
        checked = 0
        for cls, params in sorted(target.items()):
            node = getattr(ast_mod, cls, None)
            if node is None or not hasattr(node, "_fields"):
                mismatched.append(f"  {cls}: absent from this interpreter's ast module")
                continue
            checked += 1
            if list(node._fields) != params:
                mismatched.append(f"  {cls}: ast._fields {list(node._fields)} != header {params}")
        label = f"ast._fields vs {args.check_running} constructor order"
        if mismatched:
            print(f"{label}: {len(mismatched)} MISMATCH of {len(target)}")
            for line in mismatched:
                print(line)
        else:
            print(f"{label}: all {checked} constructors agree")
        print()

    print("A transcriber compiled against one of these fingerprints is valid for")
    print("that minor and no other. Every delta above is a silent-miscompile")
    print("class if the pin is not enforced.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
