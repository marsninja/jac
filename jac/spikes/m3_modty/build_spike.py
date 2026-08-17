"""Locate a CPython that ships its internal headers, and build the spike.

The M3 seat is a version-pinned C translation unit: it compiles against ONE
CPython's ``Include/internal`` headers and refuses to load into any other
minor. That makes "which CPython" a build input, not a runtime discovery, so
this script resolves it explicitly and says out loud which one it picked.

Search order (first hit wins):

1. ``$JAC_MODTY_PYTHON`` -- an explicit interpreter path.
2. ``jac/.pbs-build/<osarch>/python/install`` -- the python-build-standalone
   distribution the shipped ``jac`` binary embeds. This is the CPython that
   actually matters for M3, and it ships the full internal header set.
3. The running interpreter, if its own include directory has
   ``internal/pycore_ast.h``.

If none of those has internal headers the script FAILS, naming what is
missing and how to get it. It never silently degrades to a partial run.
"""

from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
import sysconfig
from pathlib import Path

HERE = Path(__file__).resolve().parent
BUILD_DIR = HERE / "build"
SOURCE = HERE / "modty_transcribe.c"


class NoUsableCPython(RuntimeError):
    pass


def _internal_headers_ok(include_dir: Path) -> bool:
    need = ("pycore_ast.h", "pycore_asdl.h", "pycore_compile.h", "pycore_pyarena.h")
    return all((include_dir / "internal" / h).is_file() for h in need)


def _probe(python: Path) -> dict | None:
    """Ask an interpreter for its include dir, libdir, soabi and version."""
    if not python.is_file():
        return None
    script = (
        "import json, sys, sysconfig;"
        "print(json.dumps({"
        "'include': sysconfig.get_path('include'),"
        "'libdir': sysconfig.get_config_var('LIBDIR') or '',"
        "'ldlibrary': sysconfig.get_config_var('LDLIBRARY') or '',"
        "'ext_suffix': sysconfig.get_config_var('EXT_SUFFIX') or '.so',"
        "'version': '%d.%d' % sys.version_info[:2],"
        "'executable': sys.executable}))"
    )
    try:
        out = subprocess.run(
            [str(python), "-c", script], capture_output=True, text=True, timeout=60
        )
    except (OSError, subprocess.SubprocessError):
        return None
    if out.returncode != 0:
        return None
    import json

    info = json.loads(out.stdout.strip())
    info["python"] = str(python)
    return info


def _repo_root() -> Path:
    # .../jac/spikes/m3_modty/build_spike.py -> .../jac
    return HERE.parent.parent


def candidates() -> list[Path]:
    out: list[Path] = []
    env = os.environ.get("JAC_MODTY_PYTHON")
    if env:
        out.append(Path(env))
    pbs_root = _repo_root() / ".pbs-build"
    if pbs_root.is_dir():
        for osarch in sorted(p.name for p in pbs_root.iterdir() if p.is_dir()):
            for name in ("python3", "python"):
                out.append(pbs_root / osarch / "python" / "install" / "bin" / name)
    # A PBS tree fetched into a sibling checkout is still the right CPython;
    # honour it only when pointed at explicitly, never by scanning the disk.
    out.append(Path(sys.executable))
    return out


def resolve_python() -> dict:
    tried: list[str] = []
    for cand in candidates():
        info = _probe(cand)
        if info is None:
            tried.append(f"{cand}: not runnable")
            continue
        include = Path(info["include"])
        if not _internal_headers_ok(include):
            tried.append(f"{info['executable']} ({info['version']}): no {include}/internal/pycore_ast.h")
            continue
        info["include_dir"] = str(include)
        return info
    raise NoUsableCPython(
        "m3_modty needs a CPython that ships Include/internal/pycore_*.h.\n"
        "Tried:\n  " + "\n  ".join(tried) + "\n"
        "Fixes: run `zig build fetch-pbs` under jac/ to populate "
        ".pbs-build/<osarch>/python/install, install the matching "
        "python3.X-dev package, or set JAC_MODTY_PYTHON to an interpreter that "
        "has them."
    )


def build(info: dict, verbose: bool = False) -> Path:
    BUILD_DIR.mkdir(exist_ok=True)
    so = BUILD_DIR / f"m3_modty{info['ext_suffix']}"
    include = Path(info["include_dir"])
    cc = os.environ.get("CC") or shutil.which("cc") or shutil.which("gcc")
    if cc is None:
        raise RuntimeError("no C compiler on PATH (set CC)")
    cmd = [
        cc,
        "-shared",
        "-fPIC",
        "-O2",
        "-std=c11",
        "-Wall",
        "-DPy_BUILD_CORE=1",
        f"-I{include}",
        f"-I{include / 'internal'}",
        str(SOURCE),
        "-o",
        str(so),
    ]
    proc = subprocess.run(cmd, capture_output=True, text=True)
    if verbose or proc.returncode != 0:
        sys.stderr.write(proc.stdout)
        sys.stderr.write(proc.stderr)
    if proc.returncode != 0:
        raise RuntimeError(f"compiling {SOURCE.name} failed: {' '.join(cmd)}")
    return so


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--verbose", action="store_true")
    ap.add_argument("--print-python", action="store_true",
                    help="only report the interpreter that would be used")
    args = ap.parse_args()
    info = resolve_python()
    if args.print_python:
        print(info["executable"])
        return 0
    so = build(info, verbose=args.verbose)
    print(f"built {so} against CPython {info['version']} ({info['executable']})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
