# Compact Codegen IR (JCIR)

Status: design plus reference implementation. This is the first lane 2
deliverable of the zero-bytecode endgame (epic #8201): it fixes the Step 4
shim's contract and is the proof that the intermediate annotated-state
materializer is skippable at all. The format module, the Python reference
shim, and the round-trip suite land together with this document; the sealed
native producer and the generated native transcriber come later and must
conform to the bytes specified here.

Note on location: the task brief suggested `docs/community/internals/`; the
corpus's actual home for internal design docs is `docs/internals/` (beside
`compiler_architecture.md`, `interop.md`, `ownership-checker-spec.md`), so
this document lives there.

## 1. What problem this format solves

Today codegen is two passes at the end of the pipeline:

- `jac0core/passes/pyast_gen_pass.jac` (+impl, about 4,500 lines, 169
  methods; census 2026-08-14: 137 of 169 methods are `ast3`-bound) walks the
  annotated unitree and builds a CPython `ast` tree per module.
- `jac0core/passes/pybc_gen_pass.jac` (3 methods) calls `compile()` on each
  `ast.Module` and `marshal.dumps` the code object.

`pyast_gen` interleaves two very different kinds of work:

- **Decisions**: which ast shape each unitree node lowers to, symbol
  mangling (`_py_name`, `KW_INIT` to `__init__`), scope handling (self/cls
  injection, global/nonlocal directives), temporaries (`__jac_temp_N`,
  `_jac_lambda_N`), lambda hoisting and frame placement, preamble assembly
  (jaclib/builtin/typing import dedup), switch lowering to a while/if
  chain, jsx lowering via `PyJsxProcessor`, semstr decorators, docstring
  placement, `TYPE_CHECKING` wrapping, the `TOKEN_AST_MAP` operator table
  (45 token kinds to ast classes), and every emitted diagnostic.
- **Construction**: actually instantiating `ast3.*` objects, stamping
  lineno/col via `sync()`, and handing the tree to `compile()`.

In the sealed pipeline the decisions run natively, but construction is
CPython's business by definition. The compact codegen IR is the boundary
between them: **decisions happen in the sealed pipeline and are encoded;
the shim only transcribes.** Every instruction has exactly one
transcription. The shim makes no choices, so it can stay small, dumb, and
version-tracking, exactly as the Step 4 shim principle from #8139 demands:
the shim tracks CPython internals (builds `ast` objects and calls
`compile()`) rather than owning `co_linetable`. CPython owns line tables.

The production crossing then collapses to: IR bytes in, code objects plus
diagnostics out. Full annotated trees never cross in production.

## 2. The crossing, by analogy to the parser materializer

The proven pattern is the `mat_parse` crossing
(`jac0core/parser/materialize.jac` on the Python side,
`compiler/native_materialize.jac` generated at seal time by
`utils/gen_native_materialize.jac` on the native side):

- one GIL-held `PYFUNCTYPE` entry per crossing, returning a fully
  Python-owned result;
- handle tables registered at bind time (`mat_reset`, `mat_set_class`,
  `mat_set_key`, `mat_set_enum`) so the native side can construct Python
  objects by index, never by name lookup at runtime;
- string transport through `jac_str_new`;
- `Py_DecRef` ownership discipline on the returned handle;
- a per-crossing memo flush (`_mat_flush`) for the identity-preserving
  object graph.

JCIR points this machinery at `ast` construction instead of unitree
construction, with two deliberate simplifications:

1. **No memo table.** The materializer needs `_mat_memo` because unitree
   is a mutation-aliased object graph where identity matters (decl-impl
   matching deliberately leaves two scopes sharing one dict). An ast tree
   handed to `compile()` has no identity semantics: where `pyast_gen`
   reuses one ast object in two places (for example `executed_assign` in
   `resolve_switch_stmt`), duplicating the construction is
   semantics-preserving. JCIR is therefore strictly a tree, shared
   subtrees are emitted twice, and the whole aliasing apparatus that makes
   the annotated-state materializer hard is simply absent. This is the
   concrete sense in which the intermediate materializer of #8139 Step 3
   is skippable.
2. **The construction recipe crosses as data.** The native side does not
   hardcode ast shapes; it emits a byte stream. The transcriber that walks
   the stream and constructs `ast.*` objects exists twice, by design:
   - the **Python reference shim** (`jac0core/codegen_shim.jac`, this
     change): consumes IR bytes with plain `import ast`, used by the dev
     lane, tooling, and tests, and serves as the executable specification;
   - the **generated native transcriber** (future): the evolution of
     `native_materialize.jac`, generated at seal time, walking the same
     bytes inside the sealed artifact and constructing `ast.*` PyObjects
     through libpython externs, then calling `compile()` and
     `marshal.dumps` at the end of the same GIL-held crossing.

   Both consumers bind the container's class and key tables up front:
   `cir_bind_classes` / `cir_bind_keys` are the `mat_set_class` /
   `mat_set_key` analogs. In the sealed artifact the tables are fixed at
   seal time and registered once at bind; per crossing, instructions refer
   to them by index only.

Why serialize at all if the native side could construct directly? Four
reasons: the sealed producer stays free of libpython and CPython version
churn (only the transcriber tracks CPython); the dev lane and the sealed
lane produce the same bytes, so cross-lane byte equality is a cheap parity
canary; the bytes are cacheable and dumpable for debugging; and the
transcriber stays a bounded, generated, mechanically verifiable component
instead of 4,500 lines of hand-written crossing code.

## 3. Container format

The container is versioned like the JIR container (`jac0core/jir.jac`):
magic plus format version, refuse mismatched, no migration attempts.

```
magic          4 bytes  b"JCIR"
format_version u16 LE   CIR_FORMAT_VERSION (currently 1); exact match required
python_version u16 LE   (major << 8) | minor of the producer's CPython;
                        exact match against the running interpreter required
class table    varint count, then per entry: varint byte length + utf8
key table      varint count, then per entry: varint byte length + utf8
string pool    varint count, then per entry: varint byte length + utf8
modules        varint count, then per module:
                 varint path ref (string pool index)
                 varint code length, then the instruction stream bytes
diagnostics    varint count, then per record:
                 u8 severity (0 warning, 1 error)
                 varint refs: code, message, help, mod_path (string pool)
                 varint first_line, col_start, last_line, col_end
terminator     1 byte 0xFE; trailing bytes after it are an error
```

- The **class table** holds `ast` class names (`"Module"`,
  `"FunctionDef"`, `"Load"`, ...). The consumer resolves each against the
  running CPython's `ast` module once per container; an unresolvable name
  is a hard `CodegenIrVersionError` (the producer and the runtime disagree
  about CPython), never a skip.
- The **key table** holds field names (`"body"`, `"targets"`, ...),
  interned once at bind.
- The **string pool** deduplicates every string operand: identifiers,
  string constants, module paths, spliced Python source, diagnostic text.
- Varints are LEB128; signed operands use zigzag.

Version discipline: any change to the opcode set, operand encodings, or
container layout bumps `CIR_FORMAT_VERSION`. A reader that sees a
different version refuses with `CodegenIrVersionError`. There is no
best-effort path. The `python_version` check is exact-minor for now, the
same stance the JIR header takes; marshaled bytecode is per-minor anyway.

## 4. Instruction vocabulary

The instruction stream is a postfix stack machine that builds exactly one
`ast.Module` per module record. Fifteen opcodes in six categories:

| Category | Opcode | Operands | One transcription |
|---|---|---|---|
| Operand push | `OP_NONE` | none | push `None` |
| Operand push | `OP_TRUE` | none | push `True` |
| Operand push | `OP_FALSE` | none | push `False` |
| Operand push | `OP_ELLIPSIS` | none | push `Ellipsis` |
| Operand push | `OP_INT` | zigzag varint (64-bit range) | push int |
| Operand push | `OP_INT_BIG` | sign byte, varint length, LE magnitude | push arbitrary-precision int (decoder normalizes to the same logical op as `OP_INT`) |
| Operand push | `OP_FLOAT` | 8-byte IEEE754 LE | push float |
| Operand push | `OP_STR` | varint string ref | push str from pool |
| Operand push | `OP_BYTES` | varint length, raw bytes | push bytes |
| Aggregate | `OP_LIST` | varint n | pop n values in push order, build a list; splice chunks (see `OP_PARSE_SPLICE`) are flattened inline |
| Aggregate | `OP_TUPLE` | varint n | pop n values, build a tuple; a splice chunk here is a hard error |
| Construction | `OP_NODE` | varint class ref, varint field count, that many varint key refs | pop the field values, call the bound class with those keyword arguments, stamp the location register onto the node, push it |
| Location | `OP_LOC` | 4 zigzag varints, deltas against the previous `OP_LOC` | set the location register (first_line, col_start, last_line, col_end) |
| CPython splice | `OP_PARSE_SPLICE` | varint source ref, varint line offset | `ast.parse` the source, add the offset to every `lineno`/`end_lineno` present (offset 0 means untouched), push an opaque splice chunk of the parsed statement list |
| Framing | `OP_END` | none | stream ends; the stack must hold exactly one value and it must be an `ast.Module` |

Rules that make the vocabulary total:

- **Unknown opcode is a hard versioned error.** The decoder refuses with
  the opcode value, the byte offset, and the container's claimed format
  version ("refusing to guess"). Never a silent skip.
- **The location register must be set before the first `OP_NODE`** of each
  module stream. The writer enforces this at emission and the shim
  enforces it again at transcription.
- **Keyword construction only.** `OP_NODE` never relies on positional
  `_fields` order, because CPython owns that order and changes it between
  versions (`type_params` appeared in 3.12). The producer emits every
  field it wants set; missing-field defaulting is CPython's business.
- **Splice chunks flow only into `OP_LIST`.** The producer cannot know how
  many statements a spliced Python source parses to, so the list builder
  flattens chunks deterministically; a chunk reaching `OP_NODE` or
  `OP_TUPLE` is a format error.
- **Stack discipline is checked twice.** The writer simulates stack depth
  and refuses unbalanced emission at `end_module`; the reader verifies
  again at `OP_END`.

The constant domain of v1 is `None`/`True`/`False`/`Ellipsis`/int/float/
str/bytes plus lists and tuples of those. Complex numbers, frozensets, and
nested constant tuples produced by hypothetical future constant folding
are not encodable in v1; adding them is a format version bump, not a
special case. (Spliced Python source can still yield any constant CPython
can parse, because CPython constructs those itself.)

## 5. Line mapping

`pyast_gen.sync()` stamps four fields on every constructed node from the
jac node's `loc`: `lineno = first_line`, `col_offset = col_start`,
`end_lineno = last_line if last_line > first_line else first_line`,
`end_col_offset = col_end if col_end > col_start else col_start`.

Under JCIR the producer performs that normalization and emits the four
resulting values as `OP_LOC` operands; the shim writes them onto nodes
verbatim. The register model matches `sync()`'s usage pattern: runs of
nodes sharing a jac location cost one `OP_LOC` (delta-encoded, so usually
two or three bytes), and the emission order is child locations first, the
parent's `OP_LOC` immediately before the parent's `OP_NODE`.

The shim performs **no** `fix_missing_locations` pass: that function is a
decision-hiding crutch that would mask producer bugs. A node constructed
with the register unset is a hard error instead. Downstream of the shim,
`compile()` turns these fields into `co_linetable`; CPython owns that
format entirely, which is the whole point of the shim tier.

Spliced statements keep the line numbers `ast.parse` gave them, shifted by
the operand offset. This matches today's behavior exactly: `pyinline_sync`
adds the jac node's `first_line` to inline Python, and the generated stub
sources (interop stubs, sv-to-sv stubs, native test shims) keep their
parse-relative 1..k line numbers with no offset.

## 6. How diagnostics and the code object cross

The crossing's production result is code objects plus diagnostics; the
container carries both directions of that contract:

- **Producer diagnostics** ride in the diagnostics section as flat records
  (severity, code string, message, help, module path, four location ints).
  Everything `pyast_gen` emits today (E5001..E5098) is a producer decision
  made before the crossing, so those cross as data, exactly like
  `mat_parse_diags` carries `SrcDiag` records. The wire shape `CirDiag`
  mirrors `SrcDiag` (message, code_str, loc, is_error, help) but is
  dependency-free so the format module imports nothing from the parser.
- **Shim diagnostics**: `compile()` failures are caught exactly as
  `pybc_gen` catches them (`ValueError`, `SyntaxError`, `TypeError`) and
  appended as an error diagnostic with code `E5043` and the module path.
  The module simply produces no code object, and the pipeline reports the
  diagnostic as it does today.
- **Hard errors are not diagnostics.** Version skew, malformed bytes,
  unknown opcodes, unresolvable class names, and stack imbalance raise
  `CodegenIrVersionError`/`CodegenIrFormatError`. These mean the artifact
  and the runtime disagree, the same failure class as
  `materialize.jac`'s `_bind_error`, and the correct response is
  "reinstall or rebuild this jac payload", not a source diagnostic.

The Python reference shim exposes both granularities: `transcribe()`
returns `(mod_path, ast.Module)` pairs plus diagnostics for tooling, and
`compile_ir()` returns compiled modules (code object plus
`marshal.dumps` bytes) plus diagnostics for production. Multi-module
programs are one container with one module record per `uni.Module`,
mirroring `pybc_gen.process_modules`.

## 7. What stays tooling-only

Everything below exists today, keeps existing, and never crosses in
production:

- **Full tree materialization**: the `mat_parse` crossing and the
  annotated unitree it rebuilds stay for LSP, `jac tool`, and tests.
- **`gen.py` unparse output**: `exit_module` runs `ast3.unparse` for the
  Python-source view of a module. Tooling lane only.
- **`jac_link` back-references**: `sync()` attaches `jac_link` (ast node
  to jac node) for in-process consumers. The ast tree dies inside the
  crossing after `compile()`, so production never sees them; the Python
  pyast_gen path keeps producing them for tooling.
- **`debuginfo` / `jac_mods`**: pass-internal bookkeeping.
- **`py_ast` caches on unitree nodes**: `nd.gen.py_ast` is scaffolding of
  the current pass structure, not part of the crossing contract.

## 8. pyast_gen behavior census: IR emission vs shim transcription

"IR emission" means the behavior becomes producer logic whose output is
ordinary JCIR instructions. "Shim transcription" means the behavior is
performed by the shim because it intrinsically requires CPython.

| Behavior | Where | Notes |
|---|---|---|
| `TOKEN_AST_MAP` / `UNARY_OP_MAP` operator lowering | IR emission | zero-field `OP_NODE`s (`Add`, `NotEq`, ...) |
| All statement/expression shapes (if/for/while/try/with/match/assign/calls/comprehensions/f-strings, 130+ exit methods) | IR emission | pure construction recipes; the round-trip suite proves the vocabulary in the small |
| `sync()` location stamping | split | producer computes and emits `OP_LOC`; shim writes fields verbatim; `compile()` owns `co_linetable` |
| Symbol mangling (`_py_name`, `__init__`, `py_ctx` Load/Store/Del) | IR emission | string and context-class choices are decisions |
| Preamble assembly (`__future__` import, jaclib/builtin/typing dedup, enum kind imports) | IR emission | producer keeps the same accumulator sets and emits the final import statements |
| Module docstring placement and `__name__ == "..."` guards | IR emission | ordering (docstring before `__future__`) is producer-owned |
| `TYPE_CHECKING` wrapping of typed imports | IR emission | an `If` node plus a typing import |
| Switch lowering, walker visit/disengage/report lowering, edge/connect lowering, `OpenStmt` region try/finally | IR emission | multi-statement recipes, all ordinary nodes |
| Temp and lambda naming (`__jac_temp_N`, `_jac_lambda_N`), hoist frames, leak diagnostics (E5098) | IR emission | placement decisions happen before emission; see fidelity note 3 on `fix_missing_locations` |
| Scope directives (`Global`/`Nonlocal`, sorted) | IR emission | |
| Semstr decorators (`_get_sem_decorator`), `jac_test` decorators, `impl_patch_filename` decoration | IR emission | decorator `Call` nodes with `Constant` operands; the `is_test(mod_path)` predicate moves producer-side |
| Enum lowering (`Enum`/`IntEnum`/`StrEnum` choice, `auto()` values) | IR emission | |
| Has-var lowering (`field(init=False)`, `field(factory=lambda: ...)`, constant fast path) | IR emission | see fidelity note 2 |
| `PyInlineCode` (`::py::` blocks) | shim transcription | `OP_PARSE_SPLICE` with the jac first_line as offset; `textwrap.dedent` is a producer-side string op |
| Native interop stubs, sv-to-sv stubs, boundary stub classes, native test shims, registration map | shim transcription | producer builds the Python source text from the interop manifest (sealed-side data); shim parses via `OP_PARSE_SPLICE` with offset 0 |
| `compile()` + `marshal.dumps` (all of pybc_gen) | shim transcription | end of the same crossing |
| `ast3.unparse` into `gen.py` | tooling only | never crosses |
| Diagnostics E5001..E5098 | IR emission | cross as `CirDiag` records |
| E5040 (missing py_ast) | retired | the failure class becomes a container format error |
| E5043 (compile failure) | shim transcription | same exception tuple, same code |

## 9. Fidelity analysis

Behaviors with subtle semantics, and where each one lands. Honesty over
completeness: anything unresolved is listed in section 10, not silently
assumed away.

1. **`impl_patch_filename`.** Applied to abilities whose body is an
   `ImplDef` (operand: the impl file's `mod_path`) and to tests in
   `is_test` files (keyword operand `file_loc`). Both are `Call` decorator
   nodes with string constants: fully IR-encodable. The decision inputs
   (impl file paths, the `is_test` predicate from `ext_registry`) are
   producer-side facts.
2. **Has-var `field()` wrapping.** Today the constant-vs-factory choice
   tests `isinstance(value_expr, ast3.Constant)` on the *constructed* ast.
   The producer must re-express this predicate on unitree facts (literal
   Int/Float/String/Bool/Null/Ellipsis, and MultiString that merges to a
   single constant). Behavior-preserving but not mechanical: flagged for
   the emitter port checklist, covered by the end-to-end parity canary.
3. **Hoisted lambdas and `fix_missing_locations`.** `pyast_gen` calls
   `ast3.fix_missing_locations(func_def)` when hoisting a lambda-derived
   def. JCIR has no such crutch; the producer must emit real `OP_LOC`s for
   every node of the hoisted def. The deterministic replacement rule
   (children inherit the enclosing jac node's location unless they have
   their own) must be written down in the emitter and verified against the
   parity corpus. This is the one place the line-mapping story requires
   new producer logic rather than transport of existing values.
4. **Jsx lowering.** `PyJsxProcessor` makes real decisions (element
   lowering, attribute handling, text/expression children) but its output
   is ordinary `ast3` nodes built through `pass_ref.sync`, so the
   vocabulary covers it. The processor has not been audited method by
   method in this pass; it is on the emitter port checklist. The
   `EsJsxProcessor` targets the ECMAScript lane, which does not cross this
   boundary at all.
5. **`__jac_dirty_fields__`-adjacent emissions.** Verified: `pyast_gen`
   emits nothing dirty-field-related; that tracking lives at runtime in
   `Archetype.__setattr__`. The adjacent codegen behaviors are the
   `field()` wrappers (note 2) and `__jac_async__` class markers, both
   ordinary IR emission. Nothing crosses.
6. **Module docstrings.** `nd.doc` becomes the first `Expr(Constant)`
   before the preamble; the ordering decision is the producer's and the
   round-trip suite asserts `__doc__` survives compilation.
7. **`TYPE_CHECKING` blocks.** Producer wraps typed-import statements in
   `If(Name("TYPE_CHECKING"))` and adds the typing import to the preamble.
   Encodable; no shim involvement.
8. **Generated-source stubs and `jac_link` hygiene.** Today the stub
   generators walk their parsed output setting `jac_link = []` so
   downstream tooling does not confuse them with jac-linked nodes. The
   shim's splice sets no `jac_link` at all, which is the production-correct
   behavior (nothing downstream of the crossing reads it); the tooling
   lane keeps the old path.
9. **Shared ast subtrees.** `resolve_switch_stmt` reuses one
   `executed_assign` node across case bodies. JCIR duplicates the
   construction; `compile()` treats a shared node and an equal copy
   identically. No memo table exists on purpose (section 2).
10. **Interned keys and classes.** Binding once per container mirrors
    `mat_set_key`'s `sys.intern` discipline, keeping per-node kwarg
    construction allocation-free in the native transcriber.
11. **Kwargs construction on future CPythons.** The reference shim calls
    the bound class with the complete keyword set the producer emitted, so
    the CPython 3.13+ deprecation of constructing ast nodes with missing
    required fields never fires as long as producers emit full field sets,
    which `pyast_gen` already does everywhere. The suite passes on the
    bundled 3.14.

## 10. Unresolved questions

- **The `fix_missing_locations` replacement rule** (fidelity note 3) needs
  a written spec in the emitter and a dedicated parity fixture with nested
  lambdas in default arguments and decorators, where today's behavior is
  the trickiest.
- **Producer-side constant predicate** for has-var lowering (fidelity
  note 2): re-derive from unitree, then prove equal on the corpus.
- **`gen.py` consumers audit.** Believed tooling-only; the cutover PR must
  verify nothing in the production serve path reads `mod.gen.py` or
  `mod.gen.py_ast` after the crossing lands.
- **Structured diagnostics.** `CirDiag` carries flat strings; the
  diagnostics registry objects (severity policies, fix-its) stay
  Python-side. If the sealed pipeline ever needs to emit a diagnostic kind
  the flat record cannot express, that is a format version bump.
- **Exact-minor `python_version` refusal** may be relaxable to a range
  once the native transcriber exists and the parity canary runs per
  CPython minor; until proven, exact match stands.
- **Field-set completeness is not validated at bind time.** A producer
  that omits a field CPython requires surfaces as a `compile()` error
  (E5043) rather than a bind error. Acceptable for now because the parity
  canary compares final code objects per CPython minor; revisit if the
  failure mode is ever observed in the wild.
- **`OP_TUPLE` currently has no producer.** It exists because tuple-valued
  ast fields are a plausible near-term need and its transcription is
  unambiguous; if v2 arrives without a use, drop it.

## 11. Module placement

- `jac0core/codegen_ir.jac`: the format module sits in jac0core beside
  `jir.jac`, its container sibling, because it is a leaf (imports only
  `struct`/`sys`), both lanes need it (the seal-time emitter generator
  consumes the same constants), and the consumer must load in the runtime
  core without the full compiler.
- `jac0core/codegen_shim.jac`: the Python-side consumer, beside the format
  it consumes, mirroring how `parser/materialize.jac` sits beside the
  parser it binds. It uses `import ast` freely: the shim IS the Python
  side, and this module is never sealed.
- One bootstrap-dialect note: jac0core is compiled by the jac0 bootstrap,
  which has no `**kwargs` call splat, so the shim builds its single
  keyword-apply trampoline through one `eval` of a two-argument lambda at
  first use. The generated native transcriber has no such constraint (it
  builds a kwargs dict through the C API).
- Tests: `tests/compiler/test_codegen_ir.jac`, string-named, covering the
  full round trip (functions with args and defaults, assignments, calls, a
  class, if/for, f-string), `ast.dump` equality against the source tree,
  hand emission without `ast` on the producer seat, splice offsets,
  version refusal, unknown-opcode refusal, diagnostics transport, and
  writer stack discipline.

## 12. Cutover fit

JCIR is stable across sealed and dev lanes by construction: the codegen
decision pass emits the same bytes whether it runs natively or in Python,
so lane parity is byte equality on the container, and end-to-end parity is
code object plus diagnostics equality against the bytecode pipeline, per
the epic's canary. The pass census, waiver discipline, and payload-lane
proofs of #8139 carry forward unchanged; this format is the piece that
lets Steps 3 and 4 execute as one program with nothing throwaway in
between.
