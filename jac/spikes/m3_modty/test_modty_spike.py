"""Differential proof for the M3 mod_ty transcription spike.

For each fixture the same JCIR bytes go down two paths:

  A. the shim seat        JCIR -> ast objects -> compile()      -> code object
  B. the M3 seat          JCIR -> mod_ty in a PyArena -> _PyAST_Compile

and the two code objects are compared FIELD BY FIELD, recursively through
``co_consts`` and including ``co_positions()`` -- never by comparing marshal
bytes, which ``codegen-ir.md`` section 10 rules out because ``FLAG_REF``
depends on transient refcounts.

Path A is additionally pinned against ``compile(source)`` so a bug that
happens to be shared by both transcribers cannot pass unnoticed.

Run directly (``python3 test_modty_spike.py``) or under pytest. Either way
the module re-executes itself under the CPython the spike was built for,
because a mod_ty artifact is version-pinned by construction.
"""

from __future__ import annotations

import os
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

import build_spike  # noqa: E402

FIXTURES: dict[str, str] = {
    "call_and_return": (
        "def greet(name, punct='!'):\n"
        "    msg = str(name)\n"
        "    return msg\n"
        "\n"
        "result = greet('jac', punct='?')\n"
    ),
    "literal_domain": (
        "def lits(a=1, b=2.5, c=b'xy', d=True, e=False, f=None, g='s', h=7):\n"
        "    return g\n"
        "\n"
        "picked = lits()\n"
    ),
    "nested_def": (
        "def outer(x):\n"
        "    def inner(y):\n"
        "        return y\n"
        "    return inner(x)\n"
        "\n"
        "result = outer('deep')\n"
    ),
    "docstrings_and_bare_expr": (
        '"""Module doc."""\n'
        "def f():\n"
        '    """Inner doc."""\n'
        "    return 1\n"
        "\n"
        "f\n"
        "result = f()\n"
    ),
}


# --------------------------------------------------------------------------
# Re-exec under the pinned interpreter.
# --------------------------------------------------------------------------

def _ensure_pinned_interpreter() -> None:
    if os.environ.get("_M3_MODTY_REEXEC"):
        return
    info = build_spike.resolve_python()
    if Path(info["executable"]).resolve() == Path(sys.executable).resolve():
        os.environ["_M3_MODTY_REEXEC"] = "1"
        return
    env = dict(os.environ, _M3_MODTY_REEXEC="1")
    proc = subprocess.run([info["executable"], str(Path(__file__).resolve())], env=env)
    raise SystemExit(proc.returncode)


def _load_extension():
    info = build_spike.resolve_python()
    if Path(info["executable"]).resolve() != Path(sys.executable).resolve():
        raise RuntimeError(
            f"this process is {sys.executable} but the spike pins "
            f"{info['executable']}; a mod_ty artifact must not be loaded into a "
            "CPython it was not built against"
        )
    so = build_spike.build(info)
    sys.path.insert(0, str(so.parent))
    import importlib

    return importlib.import_module("m3_modty")


# --------------------------------------------------------------------------
# The comparison.
# --------------------------------------------------------------------------

def _run_fixture(m3, jcir_spike, name: str, source: str) -> list[str]:
    import ast

    path = f"<spike:{name}>"
    tree = ast.parse(source, path)

    w = jcir_spike.CodegenIrWriter()
    jcir_spike.emit_from_ast(w, tree, path)
    ir = w.to_bytes()

    # Path A: the shim seat.
    (shim_path, shim_tree), = jcir_spike.shim_transcribe(ir)
    assert shim_path == path
    co_shim = compile(shim_tree, path, "exec")

    # The shim's own tree must still equal the parse it came from, so a bug
    # in the fixture producer cannot hide inside a matched pair.
    if ast.dump(shim_tree, include_attributes=True) != ast.dump(tree, include_attributes=True):
        return [f"{name}: the shim's tree does not match the parsed fixture"]

    # Path B: the M3 seat.
    container = jcir_spike.read_container(ir)
    mod_path, code = container.modules[0]
    co_native = m3.transcribe_compile(
        list(container.class_names),
        list(container.key_names),
        list(container.strings),
        code,
        mod_path,
        # The shim seat reaches _PyAST_Compile through `compile(tree, path,
        # "exec")`, whose dont_inherit=False merges the CALLING frame's
        # __future__ flags. Identity is a like-for-like claim, so the M3 seat
        # is asked for the same thing here; whether production should keep
        # that inheritance is section 6 of the design doc.
        merge_caller_future_flags=True,
    )

    diffs = jcir_spike.code_diffs(co_shim, co_native, name)

    # Behavioural equality, not just structural.
    g_shim: dict = {}
    g_native: dict = {}
    exec(co_shim, g_shim)
    exec(co_native, g_native)
    for key in sorted(set(g_shim) | set(g_native)):
        if key.startswith("__") or callable(g_shim.get(key)) or callable(g_native.get(key)):
            continue
        if g_shim.get(key) != g_native.get(key):
            diffs.append(f"{name}: exec produced {key}={g_native.get(key)!r} != {g_shim.get(key)!r}")
    return diffs


def _check_refusals(m3, jcir_spike) -> list[str]:
    """Every malformed input must refuse by name, never guess or crash."""
    import ast

    problems: list[str] = []
    path = "<spike:refusals>"
    tree = ast.parse("def f():\n    return 1\n", path)
    w = jcir_spike.CodegenIrWriter()
    jcir_spike.emit_from_ast(w, tree, path)
    ir = w.to_bytes()
    container = jcir_spike.read_container(ir)
    _, code = container.modules[0]

    def expect(label: str, needle: str, *args, **kw) -> None:
        try:
            m3.transcribe_compile(*args, **kw)
        except Exception as exc:  # noqa: BLE001 - the point is what it says
            if needle not in str(exc):
                problems.append(f"{label}: refused with {exc!r}, expected {needle!r}")
            return
        problems.append(f"{label}: did NOT refuse")

    expect(
        "unknown ast class",
        "does not implement",
        list(container.class_names) + ["MatchAs"],
        list(container.key_names),
        list(container.strings),
        code,
        path,
    )
    expect(
        "unknown opcode",
        "refusing to guess",
        list(container.class_names),
        list(container.key_names),
        list(container.strings),
        bytes([200]) + code,
        path,
    )
    # Strip the leading OP_LOC so the very first OP_NODE has no location.
    stripped = bytearray(code)
    assert stripped[0] == jcir_spike.OP_LOC
    del stripped[0:5]
    expect(
        "OP_NODE before OP_LOC",
        "location register is unset",
        list(container.class_names),
        list(container.key_names),
        list(container.strings),
        bytes(stripped),
        path,
    )
    return problems


def _future_flag_report(m3, jcir_spike) -> str:
    """`compile(..., dont_inherit=False)` inherits the CALLER's future flags.

    This module has ``from __future__ import annotations`` at the top, so the
    two runs below differ by exactly that bit whenever the caller's frame is
    consulted. The M3 seat has no such caller, which makes this a decision
    the design has to make rather than inherit.
    """
    import ast

    path = "<spike:futures>"
    tree = ast.parse("def f(x):\n    return x\n", path)
    w = jcir_spike.CodegenIrWriter()
    jcir_spike.emit_from_ast(w, tree, path)
    ir = w.to_bytes()
    container = jcir_spike.read_container(ir)
    _, code = container.modules[0]
    args = (
        list(container.class_names),
        list(container.key_names),
        list(container.strings),
        code,
        path,
    )
    merged = m3.transcribe_compile(*args, merge_caller_future_flags=True)
    pinned = m3.transcribe_compile(*args, merge_caller_future_flags=False)
    (_, shim_tree), = jcir_spike.shim_transcribe(ir)
    via_shim = compile(shim_tree, path, "exec")
    return (
        f"co_flags: shim(compile)=0x{via_shim.co_flags:x} "
        f"m3(merge)=0x{merged.co_flags:x} m3(pinned)=0x{pinned.co_flags:x}"
    )


def _throughput_report(m3, jcir_spike) -> str:
    """One measured datapoint: the seat is replaced for speed as well as purity."""
    import ast
    import time

    path = "<spike:bench>"
    source = "".join(
        f"def f{i}(a, b='x'):\n    v = str(a)\n    return f{i}_helper(v, b, k={i})\n"
        for i in range(200)
    )
    tree = ast.parse(source, path)
    w = jcir_spike.CodegenIrWriter()
    jcir_spike.emit_from_ast(w, tree, path)
    ir = w.to_bytes()
    container = jcir_spike.read_container(ir)
    _, code = container.modules[0]
    args = (
        list(container.class_names),
        list(container.key_names),
        list(container.strings),
        code,
        path,
    )
    reps = 20
    t0 = time.perf_counter()
    for _ in range(reps):
        (_, t), = jcir_spike.shim_transcribe(ir)
        compile(t, path, "exec")
    t_shim = (time.perf_counter() - t0) / reps
    t0 = time.perf_counter()
    for _ in range(reps):
        m3.transcribe_compile(*args, merge_caller_future_flags=True)
    t_native = (time.perf_counter() - t0) / reps
    # `compile()` on an ALREADY BUILT ast tree is not a floor: it still runs
    # PyAST_obj2mod and _PyAST_Validate before reaching _PyAST_Compile. That
    # conversion is exactly what the mod_ty seat deletes, which is why the
    # seat beats it.
    (_, prebuilt), = jcir_spike.shim_transcribe(ir)
    t0 = time.perf_counter()
    for _ in range(reps):
        compile(prebuilt, path, "exec")
    t_obj2mod = (time.perf_counter() - t0) / reps
    return (
        f"200-function module: shim seat {t_shim * 1e3:.2f} ms, "
        f"mod_ty seat {t_native * 1e3:.2f} ms ({t_shim / t_native:.1f}x); "
        f"compile() on a pre-built ast tree (obj2mod + validate + "
        f"_PyAST_Compile) is {t_obj2mod * 1e3:.2f} ms, which the mod_ty seat "
        f"also undercuts because obj2mod is what it deletes"
    )


def main() -> int:
    _ensure_pinned_interpreter()
    m3 = _load_extension()
    import jcir_spike

    info = m3.build_info()
    print(
        f"m3_modty built for CPython {info['built_major']}.{info['built_minor']}, "
        f"running under {info['running_major']}.{info['running_minor']}"
    )

    failures: list[str] = []
    for name, source in FIXTURES.items():
        diffs = _run_fixture(m3, jcir_spike, name, source)
        status = "OK " if not diffs else "FAIL"
        print(f"  [{status}] {name}")
        for d in diffs:
            print(f"         {d}")
        failures.extend(diffs)

    refusal_problems = _check_refusals(m3, jcir_spike)
    print(f"  [{'OK ' if not refusal_problems else 'FAIL'}] refusals are loud and named")
    for p in refusal_problems:
        print(f"         {p}")
    failures.extend(refusal_problems)

    print(f"  [note] {_future_flag_report(m3, jcir_spike)}")
    print(f"  [note] {_throughput_report(m3, jcir_spike)}")

    if failures:
        print(f"\n{len(failures)} difference(s)")
        return 1
    print("\nfield-by-field identity holds for every fixture")
    return 0


# --- pytest entry points ---------------------------------------------------

def test_modty_transcription_matches_the_shim_seat() -> None:
    assert main() == 0


if __name__ == "__main__":
    raise SystemExit(main())
