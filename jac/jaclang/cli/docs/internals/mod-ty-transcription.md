# Native mod_ty Transcription (M3)

Status: design, with a working prototype and a measured feasibility map.
This document specifies what replaces the Python shim seat under epic
#8288 M3 -- `jac0core/passes/jcir_bc_gen_pass.jac` and the reference shim
it drives (`jac0core/codegen_shim.jac`) give way to native construction of
a CPython `mod_ty` in a `PyArena`, handed to `_PyAST_Compile`.

It is the third sibling of `codegen-ir.md` and `fused-pipeline.md`.
`codegen-ir.md` specified the bytes that cross at the codegen tail;
`fused-pipeline.md` specified the crossing those bytes ride out of; this
document specifies the seat that consumes them, and the deletion that
follows.

The alternative of building `ast` objects natively and calling `compile()`
on them was evaluated and rejected in the epic. It is not revisited here.
What is recorded instead is the reason the rejected path was tempting --
`compile()` on an `ast` object runs `PyAST_obj2mod` to reach exactly the
entry this design calls directly -- and the measurement that shows the
direct call is both faster and structurally smaller (section 11).

Everything numbered in this document was measured on the prototype in
`jac/spikes/m3_modty/`, against CPython 3.14.6 (the python-build-standalone
distribution the shipped binary embeds) and CPython 3.12.3, on
2026-08-17.

## 1. The seat being replaced

`JcirBytecodeGenPass.transcribe_module` does four things per module today:

1. `transcribe(mod.gen.jcir)` -- decode the container, bind `ast` classes
   by name, walk the instruction stream building `ast.*` objects, stamp
   four location fields per node;
2. `ast3.unparse` the tree into `mod.gen.py` for the tooling lane;
3. `compile_ir(mod.gen.jcir)` -- decode a *second* time, build the tree a
   *second* time, and `compile(built_root, m.mod_path, "exec")`;
4. `marshal.dumps` the result onto `mod.gen.py_bytecode`.

Steps 1 and 3 are Python bytecode executing during a compile, which is the
thing #8139 set out to end. Step 3 is also where the double work lives:
the seat transcribes the same bytes twice per module, once for the tooling
view and once for the code object.

M3 replaces steps 1, 3 and 4. Step 2 is tooling-lane only and section 8
says where it goes.

## 2. The seam signature

One entry, one direction, GIL held for its whole extent.

```
jac_jcir_compile(const uint8_t *ir, size_t ir_len) -> PyObject *
```

- **Inbound**: the JCIR container bytes and nothing else. No Python
  object, no tree, no handle table lookup at call time. The class table,
  key table and string pool are inside the container, exactly as
  `codegen-ir.md` section 3 specifies; the class table is resolved to
  internal constructor ids once per container, which is the
  `cir_bind_classes` analog with no `getattr(ast, name)` in it.
- **Outbound**: a Python-owned list of `(mod_path, code_object)` pairs
  plus the diagnostics list, built with the same
  `Py_DecRef`-on-the-returned-handle discipline `mat_parse` uses. The
  code objects are ordinary `PyCodeObject`s; nothing the caller receives
  points into native memory.
- **Never crosses**: `mod_ty`, `PyArena *`, `asdl_*_seq *`, `expr_ty`.
  These are arena-scoped C types with no Python identity, and the arena
  outlives none of them (section 3).

In M1's fused world the natural placement is one step further in: the
crossing that already ends by emitting the container can call the
transcriber before returning, so the bytes never surface as a Python
`bytes` object at all and the outbound alphabet becomes "code objects and
diagnostics". The seam signature above is what makes both placements the
same code -- it takes a pointer and a length, not a Python buffer.

## 3. Memory: who owns what, and for how long

Three lifetimes, and they nest.

1. **The container bytes** are owned by the caller (the fused crossing's
   own output buffer, or a Python `bytes` in the interim lane). The
   transcriber reads them and retains nothing.
2. **The arena** is created per module stream and freed before the entry
   returns. It owns every `mod_ty`/`stmt_ty`/`expr_ty`/`asdl_*_seq` and
   every `PyObject` handed to `_PyArena_AddPyObject`. Nothing survives
   it, which is the whole reason a `PyArena` is the right allocator here:
   the tree is write-once, read-once, and dies in the same call.
3. **The code object** is refcounted and outlives the arena.
   `_PyAST_Compile` copies what it needs into `co_consts`, `co_names` and
   `co_varnames`; it does not retain arena pointers. The prototype proves
   this the only way that matters -- it frees the arena before returning
   and the returned code objects still execute correctly.

This is a deliberately *different* lifetime from the one
`fused-pipeline.md` section 8.1 is deciding between. That section is about
the **unitree**: cyclic, native-runtime-allocated, and reclaimed either by
the cycle collector or by a unit-scoped region. The mod_ty arena is
neither of those. It is CPython's own allocator, it holds an acyclic tree,
and its lifetime is a single module stream. **M3 must not fold the mod_ty
arena into the unit region.** Two reasons: `_PyAST_Compile` is entitled to
allocate into the arena it is handed (it does, for its own preprocessing),
and a `PyObject` registered with `_PyArena_AddPyObject` must be dropped
under the GIL by CPython's own teardown. Sharing a region would make the
compiler responsible for CPython's invariants for no gain -- the arena's
peak is one module's tree, which is small next to the unitree the fused
crossing is already holding.

## 4. The libpython surface, measured

This is the load-bearing feasibility result, and it is smaller and harder
than expected in the same breath.

`_PyAST_Compile` is declared `PyAPI_FUNC` in `pycore_compile.h` (comment
in CPython's own source: *"Export for 'test_peg_generator' shared
extension"*), and the four `_PyArena_*` entries are exported for the same
reason. **Nothing else in the AST construction surface is.** Measured with
`nm -D` on two independent builds:

| Symbol | Ubuntu `libpython3.12.so.1.0` | PBS `libpython3.14.so.1.0` |
|---|---|---|
| `_PyAST_Compile` | exported | exported |
| `_PyArena_New` / `_Malloc` / `_Free` / `_AddPyObject` | exported | exported |
| `_PyAST_Module`, `_PyAST_FunctionDef`, ... (79-81 constructors) | **absent** | **absent** |
| `_Py_asdl_generic_seq_new` and friends | **absent** | **absent** |
| `_PyAST_Validate` | **absent** | **absent** |

The count of `_PyAST_*` symbols in each library's dynamic symbol table is
exactly **one**.

Three consequences, each of which shapes the design:

1. **The constructors must be inlined, not called.** The shipped binary
   `dlopen`s libpython (`build.zig`: "libpython is dlopened at boot"), so
   a symbol absent from the dynamic table is unreachable, full stop.
   Fortunately every `_PyAST_X` body is the same four lines --
   `_PyArena_Malloc`, set `kind`, set the union arm's fields, stamp the
   location -- and every `_Py_asdl_*_seq_new` body is arena arithmetic
   plus a `memset`. The generated transcriber emits those bodies itself
   against the pinned headers. The prototype does exactly this; its
   undefined-symbol list is those 5 internal entries plus 27 public
   C-API / stable-ABI ones, and nothing else.
2. **There is no validator.** `_PyAST_Validate` is what `builtin_compile`
   runs between `PyAST_obj2mod` and `_PyAST_Compile`, and it is not
   reachable. A malformed `mod_ty` -- a NULL in a non-optional field, a
   `Load` context where a `Store` belongs, an empty `body` -- is
   undefined behaviour inside the compiler, not an exception. The shim
   seat has this safety net and the native seat does not, which is the
   one place M3 is strictly less forgiving than what it replaces. Three
   things stand in for it, and they have to be built rather than assumed:
   the transcriber is *generated* from the same ASDL that defines
   validity, so a required field cannot be silently omitted by a typo;
   the required-vs-optional distinction is enforced at coercion time and
   refuses by field name (section 9); and the differential lane
   (section 13, rung 3) is what catches a shape the generator got wrong,
   because a tree the shim rejects and the native seat accepts is a
   difference the comparison sees. Re-implementing the validator is a
   fourth option and is not proposed: it is 900 lines of CPython that
   would have to be re-pinned every minor, to catch bugs a generator
   should not be able to have.
3. **The surface is a stable, deliberate export set, not an accident.**
   Both symbols groups are exported *because CPython's own test
   infrastructure needs them from a shared extension*. That is a much
   better stability story than "these happen to be visible": it means
   un-exporting them would break CPython's own build.

## 5. The version pin

### 5.1 What can move

Everything that can change under a compiled transcriber lives in
`Include/internal/pycore_ast.h`, which `Parser/asdl_c.py` generates from
`Parser/Python.asdl`. Three things move, and `jac/spikes/m3_modty/audit_layouts.py`
measures all three:

```
AST surface fingerprints (kind enumerators + constructor field orders)
    3.12: 85fd617ab5bdb226  79 constructors, 104 enumerators
    3.13: 9a42e21eb3174991  79 constructors, 104 enumerators
    3.14: a59ed763c89802fe  81 constructors, 106 enumerators
    3.15: f8c31aaa70c18111  81 constructors, 106 enumerators

3.12 -> 3.13:
  node ParamSpec:    fields [name] -> [name, default_value]
  node TypeVar:      fields [name, bound] -> [name, bound, default_value]
  node TypeVarTuple: fields [name] -> [name, default_value]

3.13 -> 3.14:
  enum expr_kind: 9 enumerator(s) RENUMBERED (Attribute_kind 21->23, ...)
  enum expr_kind: + Interpolation_kind=19
  enum expr_kind: + TemplateStr_kind=21
  node Interpolation: NEW (value, str, conversion, format_spec)
  node TemplateStr:   NEW (values)

3.14 -> 3.15:
  node Import:     fields [names] -> [names, is_lazy]
  node ImportFrom: fields [module, names, level] -> [..., is_lazy]
```

The `expr_kind` renumbering is the dangerous one and the reason the pin
cannot be a runtime probe. A transcriber that hardcoded `Attribute_kind =
21` would, on 3.14, silently write a `Subscript` tag onto an `Attribute`
node and produce a code object that is wrong rather than absent. The three
field additions are the merely-fatal kind: a missing field is a NULL where
the compiler expects a value.

### 5.2 How the pin works

The pin is a **build-time header pin**, in three layers, and the prototype
carries all three:

1. **Compile against one CPython's internal headers.** The struct layouts
   and enumerator values are then resolved by the C compiler, not by the
   transcriber. This is the layer that makes the whole thing tractable:
   there are no offset tables to maintain, because the header *is* the
   table. python-build-standalone ships the complete internal header set
   (`include/python3.14/internal/pycore_ast.h` and friends), so the
   CPython the shipped binary embeds is also the CPython the transcriber
   is pinned to, with no extra fetch.
2. **Refuse unaudited minors at build.** `#if PY_VERSION_HEX` guards with
   an `#error` naming the audit that is missing. A new CPython does not
   produce a best-effort transcriber; it produces a build failure with a
   named next step. This is the "refuse loudly at build, never silently"
   requirement, and it is one preprocessor conditional.
3. **Refuse a mismatched runtime at load.** The artifact records the
   minor it compiled against and compares it to `Py_Version` in its
   module-exec slot. A `.so` built against 3.13 that finds itself inside
   3.14 raises `ImportError` before transcribing a single node. This
   catches the case layer 1 cannot: a correctly built artifact shipped
   next to the wrong runtime.

The seal's existing discipline supplies the fourth layer for free: the
JCIR container already stamps `python_version` and refuses an exact-minor
mismatch (`codegen-ir.md` section 3), and `jcir_facts.JCIR_PYTHON_MAJOR/MINOR`
already pin the producer's target against the interpreter running the
build. M3 adds no new version concept; it adds a second consumer of the
one that exists.

### 5.3 One TU per CPython, or one TU?

**One TU, compiled once per supported CPython.** Not one hand-written TU
per minor. The transcriber is generated -- from the ASDL surface, which
`audit_layouts.py` already parses -- and the generator's output is
compiled against whichever headers the build targets. A minor that changes
nothing the generator reads produces an identical source file with a
different object file.

A capsule-style runtime probe was considered and rejected: it can discover
that a symbol exists but not that a struct field moved, and the
`expr_kind` renumbering above is precisely a change no probe can see.

### 5.4 The property that makes generation cheap

Measured on two interpreters (3.12.3 and 3.14.6), against their own
headers:

```
ast._fields vs 3.12 constructor order: all 79 constructors agree
ast._fields vs 3.14 constructor order: all 81 constructors agree
```

`ast.<Cls>._fields` is in the same order as `_PyAST_<Cls>()`'s arguments,
for every class, because `asdl_c.py` generates both from one description.
The generator can therefore take field order from the `ast` module of the
CPython it targets and never parse C. One caveat found while proving it:
`TypeIgnore(int lineno, string tag)` has `lineno` as a *data* field, so a
generator that strips location names unconditionally drops a required
argument. Location attributes always arrive as the trailing quadruple
`lineno, col_offset, end_lineno, end_col_offset`; that is the shape to
match, not the name.

## 6. Compiler flags, and a latent divergence found

The shim seat reaches `_PyAST_Compile` through `compile(tree, path,
"exec")`. That path is not flag-neutral. `builtin_compile` sets
`cf.cf_flags = flags | PyCF_SOURCE_IS_UTF8`, sets
`cf.cf_feature_version = PY_MINOR_VERSION`, and -- because `dont_inherit`
defaults to false -- calls `PyEval_MergeCompilerFlags(&cf)`, which folds
in **the calling frame's `__future__` flags**.

Measured on the prototype, compiling a fixture that contains no
`__future__` statement, from a caller that does:

```
co_flags: shim(compile)=0x1000000  m3(merge)=0x1000000  m3(pinned)=0x0
```

`0x1000000` is `CO_FUTURE_ANNOTATIONS`. The shim seat's output for that
module depends on how `codegen_shim.compile_ir` itself was compiled.

Today this is invisible, because the emitter's preamble unconditionally
prepends `from __future__ import annotations` to every module it emits
(`jcir_gen_pass.impl.jac`, `JcirGenPass.postinit`), so the flag would be
set from the tree anyway. It stops being invisible the moment any module
reaches the seat without that preamble.

**The design decision M3 must make explicitly: the native seat pins its
flags and does not merge.** `cf_flags = PyCF_SOURCE_IS_UTF8`,
`cf_feature_version = PY_MINOR_VERSION`, `optimize = -1`, no
`PyEval_MergeCompilerFlags`. Three reasons: inside a native crossing the
"calling frame" is an accident of who invoked the crossing; the emitted
tree already declares its own `__future__` state and `_PyAST_Compile`
reads it from the tree; and a compile whose output depends on ambient
interpreter state is not reproducible, which is the property the whole
container discipline exists to protect.

The prototype exposes both behaviours behind a parameter so the two can be
compared, and its identity fixtures ask for the merging one -- identity is
a like-for-like claim and the shim merges. **Pinning is therefore a
deliberate, named behaviour change at the cutover**, not a silent one, and
section 10 gives it an acceptance test.

## 7. The loc-fill obligation, carried over

`codegen-ir.md` section 5.1's rule is unchanged and gains teeth.

- The location register must be live before the first `OP_NODE` of every
  module stream. The prototype enforces it exactly as the shim does and
  refuses by name (`the location register is unset`) rather than
  defaulting to zero.
- Every location-bearing node is stamped from the register at
  construction. No fill pass exists, on either seat.
- Two node categories in `mod_ty` have **nowhere to put a location**, and
  this is a real difference from the shim rather than a nuance:
  - **ASDL enums** (`Load`, `Store`, `Del`, and every operator: 21 of the
    ~90 ast class names the emitter uses) are plain C integers. The shim
    creates `ast.Load()` objects and stamps four fields on them, which
    CPython then ignores. The native seat stamps nothing because there is
    no object. Same outcome, reached honestly.
  - **Product types without attributes** (`arguments`, `comprehension`,
    `withitem`) likewise carry no location fields.

  The consequence for the differential suite: a per-node location
  assertion cannot walk the native seat's output, because there are no
  nodes to walk. The property survives as an assertion on the *code
  object* -- `co_positions()` -- which the prototype already compares and
  which is strictly stronger, because it is what the location fields were
  for.

The two measured front-end exceptions of `codegen-ir.md` section 5.1 (the
f-string format-spec `Constant`, the annex-reached implicit `self`/`cls`
`arg`) are producer-side and unaffected by which seat consumes the bytes.

## 8. What the deletion actually is

M3 deletes, on the production path:

- `jac0core/passes/jcir_bc_gen_pass.jac` and its impl -- the shim seat.
- `jac0core/codegen_shim.jac`'s `compile_ir`, `transcribe_module`,
  `decode_ops`, `cir_bind_classes`, `cir_bind_keys`, `_call_kw` (with it,
  the `eval`-built keyword trampoline the bootstrap dialect forced), and
  `_parse_splice`.
- The double transcription described in section 1: one decode, one tree,
  per module.
- `marshal.dumps` at the tail, if the JIR tier takes code objects rather
  than bytes; if it does not, `marshal` stays as a serializer and stops
  being a codegen step.

M3 does **not** delete:

- `ast3.unparse` into `mod.gen.py`. That is the tooling view
  (`codegen-ir.md` section 7) and it needs an `ast` tree. It moves to the
  tooling lane and runs only when something asks for `gen.py` -- which is
  the correct shape anyway, since production has been building a tree
  solely to unparse it and throw it away. The transcription that feeds it
  is the existing Python shim, retained as *tooling* rather than as a
  *seat*.
- `OP_PARSE_SPLICE`'s dependence on CPython's parser. `ast.parse` has no
  mod_ty-level equivalent that avoids building objects; the native seat
  calls `PyRun_String`-family parsing (`Py_CompileStringObject` with
  `PyCF_ONLY_AST`, or `PyAST_obj2mod` on the result) and splices the
  resulting statements into the arena tree. This is the one place the
  rejected ast-objects path survives, scoped to spliced Python source
  only, and it is honest: the producer does not know what that source
  parses to, which is why the opcode exists. Section 12 lists it as the
  largest remaining prototype gap.

## 9. The refusal story

Every failure this seat can have is one of three kinds, and none of them
is a silent skip.

| Kind | Example | Response |
|---|---|---|
| Build-time skew | unaudited CPython minor | `#error` naming the missing audit; no artifact is produced |
| Load-time skew | artifact built for 3.13, runtime is 3.14 | `ImportError` in the module-exec slot, before any transcription |
| Container skew | class name this build cannot transcribe; unknown opcode; stack imbalance; `OP_NODE` before `OP_LOC` | hard error naming the class/opcode/offset **and the CPython the transcriber was built for** -- the same class as `CodegenIrVersionError`, meaning "rebuild", not a source diagnostic |
| Source-level | `_PyAST_Compile` returns NULL | the E5043 diagnostic the shim already emits, with the module path; the module produces no code object and the pipeline reports it |

The prototype implements and tests the middle two rows. Its refusal
messages name the offending class or opcode, the byte offset, and the
built-for CPython minor, so a container/runtime disagreement reads as one.

## 10. Acceptance tests M3 needs

Six, and the first four are mechanisms rather than assertions.

1. **Field-by-field code-object identity, per fixture, per CPython
   minor.** The shim seat and the native seat compile the same container
   to code objects that agree on every field, recursively through
   `co_consts`, including `co_linetable` and `co_positions()`. Never
   marshal bytes -- `codegen-ir.md` section 10's `FLAG_REF` caveat rules
   them out and this suite is where that rule earns its keep. The
   prototype's `code_diffs` is the reference comparison and this suite is
   the differential suite of `test_jcir_gen_pass.jac` with a third lane
   added.
2. **The shim seat is not merely unused but absent.** After the cutover
   the seat's module is deleted, so the test that would prove "the native
   seat served this compile" is a deletion-accounting test, not a
   counter. This is the same standard `fused-pipeline.md` section 1.1
   sets for M1: registration is not service. Until the deletion lands,
   the honest guard is that the code object's provenance is observable --
   a build flag that makes the native seat the only path and fails the
   suite if the Python one is reached.
3. **The layout fingerprint is pinned.** `audit_layouts.py`'s fingerprint
   for the target minor is checked into the build and compared at
   generation time. A CPython bump that changes it fails the build with
   the diff, which is the same shape as `test_jcir_facts_sync.jac` pinning
   the baked python version.
4. **Every refusal is exercised.** Unknown opcode, unknown class, stack
   imbalance, `OP_NODE` before `OP_LOC`, truncated stream, and a
   deliberately mismatched artifact/runtime pair. The prototype covers
   four of the six.
5. **The compiler-flag pin is asserted.** A module whose tree contains no
   `__future__` statement compiles to `co_flags` without
   `CO_FUTURE_ANNOTATIONS`, regardless of what the caller's frame carries.
   This is the test that makes section 6's behaviour change deliberate.
6. **Arena hygiene under failure.** A container that fails mid-stream (a
   malformed node after 200 good ones) must free its arena and leak
   nothing; run under `PYTHONMALLOC=debug` and, in CI, under a refcount
   check. The failure path is the one a fuzzer finds and a suite usually
   does not.

## 11. What the prototype proved

`jac/spikes/m3_modty/` -- a version-pinned C translation unit, a faithful
transliteration of the JCIR writer and reference shim, a layout auditor,
and a differential runner.

Proven, on CPython 3.14.6:

- **Field-by-field identity** on four fixtures (a call with a keyword
  argument and defaults; the literal domain including `None`, `True`,
  `False`, bytes, float and str constants; nested function definitions;
  module and function docstrings with a bare-expression statement).
  Identity covers `co_code`, `co_consts` recursively, `co_names`,
  `co_varnames`, `co_flags`, `co_linetable` and `co_positions()`, plus
  behavioural equality under `exec`.
- **The five-symbol surface.** The artifact's undefined internal symbols
  are `_PyAST_Compile`, `_PyArena_New`, `_PyArena_Malloc`,
  `_PyArena_Free`, `_PyArena_AddPyObject`, and nothing else; every
  constructor and sequence allocator is inlined from the pinned headers.
- **The arena contract.** The arena is freed before the entry returns and
  the code objects execute correctly afterwards.
- **The refusals.** Unknown class, unknown opcode and `OP_NODE`-before-
  `OP_LOC` all refuse by name.
- **Throughput, as a secondary result.** A 200-function module: the shim
  seat 15.4 ms, the native seat 1.3 ms (12.2x). The interesting number is
  the third one: `compile()` on an *already built* `ast` tree is 1.7 ms,
  which the native seat also beats, because `PyAST_obj2mod` is precisely
  what it deletes. That is the quantitative form of why the rejected
  alternative was rejected.

Deliberately not proven, and honestly so:

- **Only 13 of ~90 ast classes** are implemented (Module, FunctionDef,
  arguments, arg, Return, Expr, Assign, Constant, Call, keyword, Name,
  Load, Store). The remainder are mechanical -- the same four lines per
  class -- but "mechanical" is a claim the generator has to cash, not the
  spike.
- **Only 3.14 was built.** The 3.12/3.13/3.15 analysis is header-level
  (section 5.1) and no artifact was built against them, because no
  interpreter with internal headers for those minors was available on the
  measuring host. The Ubuntu 3.12 measurement is symbol-level only.
- **`OP_PARSE_SPLICE`, `OP_TUPLE`, `OP_INT_BIG`, `OP_FLOAT` in a
  `constant` position beyond the fixtures, and diagnostics transport** are
  not implemented. `OP_INT_BIG` in particular needs a large-integer
  constructor from bytes and the public C API for it (`PyLong_FromNativeBytes`)
  only exists from 3.13, so the pinned-per-minor answer differs across the
  supported range -- a small but genuine per-minor divergence to design
  for.
- **Multi-module containers.** The prototype transcribes one module
  stream per call; the container carries many and the real seat loops.
- **The generator itself.** The spike hand-writes what M3 generates.

## 12. Open questions

- **Where the seat sits relative to M1's crossing.** If the fused crossing
  calls the transcriber before returning, the container never becomes a
  Python object and the outbound alphabet is code objects plus
  diagnostics. If M3 lands before M1's crossing does, the seat is a
  separate entry taking `bytes`. The seam signature in section 2 is
  chosen to make these the same code, but the ordering decides which one
  ships first and it is not decided here.
- **`OP_PARSE_SPLICE`'s residue.** Section 8 keeps a CPython-parser call
  for spliced source. Whether the splice result is converted through
  `PyAST_obj2mod` into the same arena (simple, keeps one tree) or spliced
  as a pre-compiled code object (avoids objects entirely, but changes the
  container's semantics) is open. The first is the conservative default.
- **Whether `marshal` survives.** If the JIR tier can hold code objects
  directly for the in-process case, the tail loses a serializer. If not,
  `marshal.dumps` stays -- as storage, not as codegen. Either way section
  10's identity test compares code objects, never bytes.
- **The `gen.py` tooling path's cost.** Production currently builds a
  full `ast` tree per module solely to unparse it. Moving that to
  on-demand is a straight win, but something has to measure how often
  tooling actually asks -- if `jac tool ir py` is the only caller, the
  answer is "almost never" and the win is the whole tree.
- **A second supported minor.** The pin mechanism is designed for a set,
  but the shipped binary embeds one CPython. Whether M3 ever builds more
  than one transcriber is a packaging question, and until it is answered
  the honest posture is: one, refusing loudly on any other.

## 13. The sub-PR ladder

Six rungs into `zb-endgame`, each independently reviewable, each with its
own acceptance mechanism. Rungs 1-3 land before anything is deleted.

1. **The layout audit and the pin.** `audit_layouts.py` promoted out of
   the spike, the target minor's fingerprint checked in, and a build step
   that fails on a fingerprint change with the diff. No transcriber yet.
   *Acceptance*: bumping the pinned CPython fails the build with a named
   diff.
2. **The generator, and the generated TU for every ast class the emitter
   names.** Output only -- compiled, symbol-checked, not called. Reuses
   `gen_native_materialize.jac`'s shape: read a description at seal time,
   emit one arm per class. *Acceptance*: the artifact builds, its
   undefined-symbol set is exactly the five internal entries plus public
   API, and a load canary confirms the runtime-minor check fires on a
   deliberate mismatch.
3. **The differential lane.** The native seat runs *beside* the shim seat
   on every compile behind a build flag, and the two code objects are
   compared field by field, failing the build on any difference. This is
   the rung that earns the deletion, and it is where the whole existing
   fixture corpus (`test_jcir_gen_pass.jac`, the nineteen canary suites of
   `codegen-ir.md` section 11.2) gets pointed at the new seat for free.
   *Acceptance*: a full from-clean build of the dev CLI toolchain with the
   comparison on, green.
4. **The compiler-flag pin.** Switch the native seat to non-merging flags
   and land section 10's test 5. Separate rung because it is a deliberate
   behaviour change and deserves its own review and its own release note.
5. **Splices, diagnostics, and multi-module containers.** The remaining
   opcodes and the container loop, plus the E5043 path. *Acceptance*: the
   differential lane of rung 3 goes from "the fixtures agree" to "every
   suite agrees", including `::py::` blocks and the interop stubs.
6. **The cutover and the deletion.** The build flag inverts, the shim
   seat's pass and `codegen_shim`'s production half are deleted, and
   `ast3.unparse` moves to the tooling lane. *Acceptance*: deletion
   accounting -- the fallback path does not exist to regress onto, which
   is the epic's stated standard.

Rungs 1 and 2 can proceed in parallel with M1; rung 3 is where M3 and M1
have to agree on section 12's first question, because the differential
lane needs a place to stand.
