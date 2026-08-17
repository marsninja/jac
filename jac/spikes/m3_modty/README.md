# M3 mod_ty transcription spike

A working prototype for epic #8288 M3: replacing the Python shim seat
(`jac0core/passes/jcir_bc_gen_pass.jac` + `jac0core/codegen_shim.jac`) with
native construction of a CPython `mod_ty` in a `PyArena`, handed to
`_PyAST_Compile`.

The design this measures is `jac/jaclang/cli/docs/internals/mod-ty-transcription.md`.
Nothing here is wired into the compiler, imported by it, or built by it.

## Running it

```sh
# any CPython that ships Include/internal/pycore_*.h
python3 test_modty_spike.py

# or point it at one explicitly
JAC_MODTY_PYTHON=/path/to/python3 python3 test_modty_spike.py
```

The runner resolves an interpreter (in order: `$JAC_MODTY_PYTHON`, the
python-build-standalone tree under `jac/.pbs-build/<osarch>/python/install`,
then the running interpreter), re-executes itself under it, compiles
`modty_transcribe.c` against that CPython's internal headers, and runs the
differential comparison. If no candidate has internal headers it fails and
says which ones it tried and how to get them; it never degrades to a
partial run.

Expected output:

```
m3_modty built for CPython 3.14, running under 3.14
  [OK ] call_and_return
  [OK ] literal_domain
  [OK ] nested_def
  [OK ] docstrings_and_bare_expr
  [OK ] refusals are loud and named
  [note] co_flags: shim(compile)=0x1000000 m3(merge)=0x1000000 m3(pinned)=0x0
  [note] 200-function module: shim seat 15.35 ms, mod_ty seat 1.26 ms (12.2x); ...

field-by-field identity holds for every fixture
```

## The layout auditor

`audit_layouts.py` is the version-pin mechanism, standalone. It parses
`pycore_ast.h` from one or more CPython minors and reports what a compiled
transcriber is pinned to: node-kind enumerator values, per-node field
lists, and constructor argument orders, plus a fingerprint and a per-minor
diff.

```sh
python3 audit_layouts.py --fetch 3.12 3.13 3.14 3.15

# and the property that lets a generator read field order from `ast`:
python3 audit_layouts.py --fetch 3.14 --check-running 3.14
```

## Files

| File | What it is |
|---|---|
| `modty_transcribe.c` | the transcriber: JCIR bytes -> `mod_ty` -> `_PyAST_Compile`. Version-pinned by `#error` guards and a load-time minor check. Reaches libpython through five exported internal symbols; every AST and asdl-sequence constructor is inlined from the pinned headers, because none of them are in libpython's dynamic symbol table. |
| `jcir_spike.py` | a faithful transliteration of `codegen_ir.jac`'s writer and container reader, a fixture producer that derives JCIR bytes from a parsed tree, the reference shim reduced to the opcodes in play, and the field-by-field code-object comparison. |
| `build_spike.py` | interpreter resolution and the compile step. |
| `test_modty_spike.py` | the differential runner: same bytes, two seats, field-by-field comparison plus `exec` equality, the refusal checks, and two measured notes. Also a pytest entry point. |
| `audit_layouts.py` | the cross-minor AST surface audit. |

## Scope

Deliberately partial, and section 11 of the design doc says exactly how:
13 of ~90 ast classes, one CPython minor built, one module stream per
call, and no `OP_PARSE_SPLICE` / `OP_TUPLE` / `OP_INT_BIG`. The point is
the path, not the coverage -- the remaining classes are four mechanical
lines each, which is what M3's generator emits.
