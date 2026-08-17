# The Fused Compile Crossing (M1)

Status: design, with a measured census and a named blocker. This document
specifies the crossing `compile_module` becomes under epic #8288 M1 --
parse, the sealed passes, and codegen inside one native call, with the
tree never surfacing as a Python object mid-pipeline -- and records what
was measured on `bd272af79` (post-#8291: JCIR is the only codegen, 14
sealed roots, the pooled seal from #8280).

It is the sibling of `codegen-ir.md`. That document specified what
crosses at the codegen tail; this one specifies the crossing itself.
Sections 1 through 4 are the census the design answers to; sections 5
onward are the design; section 11 is the measured blocker, stated before
the ladder that clears it so nobody reads the ladder as a schedule.

## 1. What the crossing surface actually is today

The headline number is one, and it is not the one the naming suggests.

**Per compile of one module, on a warm process, the sealed pipeline makes
exactly one Python-to-native execution crossing per source file, and that
crossing is the parser.** Thirteen of the fourteen sealed roots are
built, hash-verified, `dlopen`ed, symbol-checked and registered -- and
then never called. The pass-serving binder does not serve passes. It
attests them.

The mechanism, in full:

- `jac0core/passes/pass_serve.jac` `_bind_one` resolves an artifact,
  opens it (`DylibEngine`), calls `__jac_shared_init`, checks that every
  advertised export resolves by `dlsym`, probes `jac_str_new` for
  liveness, and adds the fullname to `_NATIVE_SERVED_PASSES`. It retains
  the engine (`_BIND_KEEP`) and **no function pointer**.
- `jac0core/passes/impl/transform.impl.jac` `_require_native_pass_tier`
  runs from `BaseTransform.postinit` on every pass construction. It
  raises when the image claims a module the binder could not bind. On
  the success path the very next statement is
  `self.ir_out = self.timed_transform(ir_in=self.ir_in)` -- the pass's
  **bytecode body**.
- The only `ctypes.PYFUNCTYPE` bindings that reach a sealed compiler
  artifact live in `jac0core/parser/materialize.jac`: `mat_parse`,
  `mat_parse_diags`, `mat_merge_probe`, plus the four bind-time table
  setters. `frontend.jac` is the only caller of the first two;
  `pass_serve.jac`'s annex canary is the only caller of the third.

So the tripwire's message -- "the pass's bytecode body was reached
instead of the artifact serving it" -- describes, on the success path,
exactly what happens. The invariant it enforces is *the artifact loads
and is healthy*, which is a real invariant and was worth building; it is
not *the artifact runs*.

This is also the honest reading of the epic's line that "the sealed
binary compiles no faster than dev today". It does not compile faster
because, apart from the parse, it does not run any of the native code it
ships.

### 1.1 The consequence for `test_jcir_sealed_bytes.jac`

That suite asserts sealed-lane JCIR bytes equal source-lane JCIR bytes,
and guards against a vacuous pass by asserting the binder registered
`jaclang.jac0core.passes.jcir_gen_pass`. The guard is the strongest one
available at the time it was written, and it is not sufficient: binder
registration proves the artifact bound, and `JcirGenPass(...).ir_bytes()`
still executes the Python body. Both sides of that comparison are the
source lane. The suite is not wrong about anything it checks; it cannot
today check the thing its name promises. M1's parity canary
(section 10.1) is what makes that comparison real, and this document is
where the limitation is recorded until then.

## 2. The numbers

Measured on this tree, warm process, dependency modules already cached.

### 2.1 Crossings

Let **A** = annex files (`.impl.jac` / `.test.jac`) for the module,
**D** = dependency source files parsed into the hub, **R** = sealed
artifacts bound (14), **C/K/E** = the materializer's class / interned-key
/ enum-member handle counts (158 / 219 / 46).

```
crossings_steady(compile_module)  = 1 + A + D          # one mat_parse per source file
crossings_per_pass                = 0
crossings_per_node                = 0
crossings_bind (once per process) = 2R + 1 + 1 + C + K + E + 2  ~= 454
```

The steady-state count is **O(#files)**. It is independent of the number
of passes and independent of the number of nodes. Of those crossings,
100% carry a tree, all of it outbound; nothing but `(char*, len)` source
strings and `int64` scalars ever crosses inbound.

### 2.2 Dispatch, against which those crossings are measured

`jac0core/passes/impl/uni_pass.impl.jac` `traverse` recurses in Python,
calling `enter_node`/`exit_node`, which double-dispatch through the
generated `accept_enter`/`accept_exit` on each node class
(`jac0core/unitree.impl/dispatch.impl.jac`). The visitor surface is 118
`enter_*` plus 125 `exit_*` methods over 155 node classes. Not one of
those files references `ctypes`.

| Module | Nodes | Pass runs on it | Node visits | enter+exit dispatches | Native crossings |
|---|---|---|---|---|---|
| a 25-line probe module | 121 | 27 | 1,096 | **2,192** | **1** |
| `jac0core/srcloc.jac` | 611 | 23 | 5,860 | **11,720** | **1** |

Whole-compile totals for the same two runs, including dependencies and
typeshed stubs already in cache: 77 pass runs / 126,500 dispatches, and
76 pass runs / 306,912 dispatches respectively.

The ratio M1 attacks is therefore **thousands of Python visitor
invocations per native crossing**, and the crossing count is already
minimal. M1 does not reduce a large number of crossings to one. **M1
moves work across the one crossing that exists.**

### 2.3 The materializer, and what a tree costs to hand back

`jac/jaclang/compiler/native_materialize.jac` is generated at seal time
by `utils/gen_native_materialize.jac` (931 LOC of generator) and is not
in the tree (`.gitignore`). A measured artifact: **20,579 LOC**, 161
per-class emitters, 7 enum emitters, one `isinstance` dispatch chain, 3
crossing entries, 4 bind-time setters. The Python-side binder is
`parser/materialize.jac` (309 LOC); the dispatch site is
`parser/frontend.jac` (191 LOC).

Every crossing that returns a tree also pays `materialize_fixups`, an
O(#nodes) Python re-walk that rebuilds the slots the emitter erased
(`name_spec` self-links, `AstSymbolStubNode` stubs, seven `_SYM_REBIND`
categories). That walk is not a boundary crossing; it is the boundary's
tax.

There is **no zero-objects-materialized counter** today. `_mat_memo`'s
length is that number per crossing and is never read; `na_census_*`
counts native-compile demotions, which is a different thing. The counter
M4 asserts in CI is new instrumentation, and section 7.3 places the seam
where it belongs.

### 2.4 The schedule, in three places

- **A, the executed schedule**: the `get_*_sched` family in
  `jac0core/compiler.jac`, ordered by `PROVIDES`/`REQUIRES` tags and
  checked by `validate_schedule` / `resolve_schedule`.
- **B, the sealed set**: `NATIVE_SEAL_ROOTS` (14) and
  `NATIVE_SEAL_CLOSURE` (35) in `utils/precompile_bytecode.jac`, ordered,
  with `compiler/native_materialize.jac` last because it is generated
  from unitree's layout.
- **C, the dispatch surface and the tier registry**: `DISPATCH_PASSES` in
  `jac0core/gen_uni_dispatch.jac` (which passes get generated `accept_*`
  arms) and `_NATIVE_PASS_TIER_MEMO` / `_NATIVE_SERVED_PASSES` in
  `jac0core/passes/transform.jac`.

M1 adds no fourth site. The fused entry's pass order **is** site A, read
at seal time and emitted into the generated crossing; the fused root
joins site B; site C is where the tripwire's replacement lands
(section 8.2).

### 2.5 Driver-stamped facts, the inbound half of the problem

Ten facts are stamped by the Python driver onto the tree before a sealed
pass reads them. They exist precisely because the pass could not reach
the driver: `test_pass_driver_seam_census.jac` fails any sealed pass
whose native seam names `prog`, `resolve_relative_path`, `compile`,
`load_dependency_module`, `discover_annex_files`, `discover_base_file`,
`rd_parse`, or `read_file_with_encoding`.

| Fact | Stamped in | Shape |
|---|---|---|
| `Module.annex_mods` | `parse_str` | **list of Modules (trees)** |
| `Import.absorbed_mod` | `resolve_absorb_imports` | **a Module (a tree)** |
| `Import.jac_detected` | `resolve_absorb_imports` | bool |
| `Import.boundary_facts` (9 fields) | `stamp_boundary_facts` | strings + bools |
| `Module.access_enforced` | `stamp_access_check_facts` | bool |
| `Module.jac_project_root` | `stamp_access_check_facts` | str |
| `Module.mobui_enforced_root` | `stamp_client_kind_facts` | str or None |
| `Module.decided_codespace` + per-node `code_context` | `_coerce_module` | enum |
| `Module.parse_failed` | `parse_str` | bool |
| `_analyses_run[id(mod)]` | `run_schedule` | set of tags |

Eight of the ten are scalars and cross trivially. **Two are trees**, and
they are the reason section 6 exists.

## 3. What the fused crossing is

One GIL-held entry, generated at seal time beside the materializer's
entries, taking sources and facts and returning a JCIR container plus
diagnostics plus the module facts the driver needs back. No tree in, no
tree out.

```jac
def jc_compile_unit(sources: str, paths: str, facts: str) -> int
```

Three `jacstr` operands, one `PyObject*` result, exactly the `mat_parse`
ABI (`ctypes.PYFUNCTYPE(c_uint64, ...)` over `jacstr3`/`jacstr2`
triples), because that ABI is proven, GIL-correct, and already
generated. The operands are not three scalars but three *containers*:

- **`sources` / `paths`**: the unit's source texts and their paths --
  the module itself, its annex files, and every module it absorbs, in
  dependency order. The driver already resolves this set; it is the same
  walk `parse_str` and `resolve_absorb_imports` do today.
- **`facts`**: the compile-facts container (section 6).

The result is a Python list, built inside the artifact through the same
libpython externs the materializer uses:

| Slot | Value |
|---|---|
| 0 | JCIR container bytes for every module in the unit (`PyBytes`) |
| 1 | diagnostics, as flat records, one list (section 7) |
| 2 | error count |
| 3 | the unit's outbound facts, as a container (section 6.3) |

Slot 0 is already multi-module by construction: the JCIR container's
`modules` section is `varint count, then per module: path ref + code
length + stream`. A unit with a module and two absorbed modules is one
container with three module records. Nothing in the format changes.

**The unit, not the module, is the crossing's granularity.** That is the
one shape change M1 makes to the driver's mental model, and it is forced:
a fused crossing cannot call back into Python to resolve an absorb, and
an AOT artifact with a Python callback is refused at seal time
(`seal_native_roots` rejects any root whose `interop_manifest` carries
`native_imports`). Either the absorbed module's tree crosses inbound --
which is the thing M1 exists to delete -- or its *source* crosses inbound
and the crossing compiles it. The second is the only option that is not
self-defeating.

## 4. What stays Python: the driver rim

The rim is everything that is not a decision about a tree.

- **Module and import resolution, and the filesystem.** `modresolver`,
  `discover_annex_files`, `discover_base_file`, `read_file_with_encoding`,
  `resolve_relative_path`, `jac.toml` and project config. These are the
  eight names `test_pass_driver_seam_census.jac` already forbids inside a
  sealed pass; M1 does not move them, it feeds the crossing from them.
- **The caches.** JIR read/write, the sealed-image tiers, `get_bytecode`,
  `module_cache_key`. Section 9.
- **The whole-program placement solver.** Its verdict crosses in as a
  fact; the solver itself needs the program graph, not a tree.
- **The unsealed passes.** Until M2 seals the analysis cluster, the
  type-check and inference passes are Python and need a Python tree.
  Section 11.2 is where that collides with this design; it is the reason
  M1's first landing is a *prefix* fusion and not the whole pipeline.
- **The shim seat.** `jcir_bc_gen_pass` transcribes the container into
  code objects and stays Python until M3, by the design already recorded
  in `codegen-ir.md` section 11.
- **Diagnostic policy and delivery.** `build_diagnostic_policy`,
  `deliver_alerts`, `Alert` rendering, `pretty_print`. Suppression
  decisions that depend on source text and inline `jac:ignore` comments
  stay rim-side; the crossing emits every diagnostic it decides and the
  rim filters. That is what `run_pass` already does with its two
  caller-owned lists.

## 5. The entry, and what does *not* cross

Three properties are load-bearing and each one is a deletion.

1. **No tree crosses inbound, ever.** The inbound alphabet is source
   text, paths, and flat facts. This is what makes a "dematerializer"
   (Python tree to native tree) unnecessary -- a component that would be
   strictly harder than the materializer, because the Python tree is
   mutation-aliased and the native side would have to rebuild the
   aliasing.
2. **No tree crosses outbound on the production path.** The container is
   bytes. `mat_parse` keeps existing for the tooling lane (LSP, `jac
   tool`, formatter, tests) exactly as `codegen-ir.md` section 7 says the
   materializer would; it stops being on the compile path.
3. **No per-node crossing is introduced.** The traversal happens
   natively, inside the call, through the same generated
   `accept_enter`/`accept_exit` double dispatch -- which is already
   compiled into the sealed artifacts. M1 does not "retire per-node
   dispatch" by deleting the visitor pattern; it retires per-node
   *Python* dispatch by running the visitor natively. The 2,192-to-1 and
   11,720-to-1 ratios of section 2.2 become 0-to-1.

## 6. Getting the facts in

### 6.1 The scalar eight: a compile-facts container

A versioned byte container in the discipline `jir.jac` and
`codegen_ir.jac` already share: magic, format version, string pool,
varint payload, terminator, exact-match refusal with no migration
attempt.

```
magic          4 bytes  b"JCFX"
format_version u16 LE   exact match required
string pool    varint count, then per entry: varint length + utf8
unit           varint module count, then per module:
                 varint path ref
                 u8  flags (access_enforced, parse_failed, is_annex, is_absorbed)
                 varint decided_codespace enum
                 varint jac_project_root ref
                 varint mobui_enforced_root ref (0 = None)
                 varint import count, then per import (source order):
                   u8  flags (jac_detected, is_service, pinned_server,
                              client_native_edge, from_loc_resolved)
                   varint native_from_path / native_absorb_path /
                          sv_source_mod refs
                   varint native_item_paths count + refs
                   varint bridged_decls count + refs
terminator     1 byte 0xFE
```

Imports are keyed by **source order**, not by node identity: both sides
parse the same bytes with the same parser, so the *n*-th `Import` node is
the same node on both sides. That is the same identity discipline the
materializer's handle tables use -- agree on an index, never on a name at
runtime.

The container's producer is the driver rim; its consumer is generated
into the fused root beside the crossing entry, so the fact struct and the
decoder are derived from one description and cannot drift. A version
mismatch is a `CompileFactsVersionError`, the same failure class as
`CodegenIrVersionError`: the artifact and the runtime disagree, the
answer is rebuild, never a best-effort read.

### 6.2 The two trees: annex and absorb

`Module.annex_mods` and `Import.absorbed_mod` are the only facts whose
value is a tree, and both dissolve the same way: **the driver hands the
crossing the source, and the crossing does the work.**

- **Annex.** The native side already weaves: `merge_annex_into` is
  compiled into the annex artifact and `mat_merge_probe` exercises it on
  natively-parsed trees at every bind. The fused entry does what the
  probe does, for real, on the unit's annex sources. The driver keeps
  annex *discovery* (a filesystem walk) and stops doing annex *parsing*.
- **Absorb.** `resolve_absorb_imports` today calls
  `target_program.compile(resolved, no_cgen=True, type_check=False)` --
  a recursive compile. In the fused world the driver resolves the absorb
  *path* (rim work it already does) and adds the resolved source to the
  unit. The crossing compiles it in the same call, keeps its tree native,
  and emits its module record into the same container. The recursion
  moves from "Python driver re-enters itself" to "one crossing over a
  dependency-ordered source list", which is the shape that lets a tree
  die inside the call.

The one behavior this changes is *cache reuse for absorbed modules*: the
driver must decide, before the crossing, whether an absorbed module is
already compiled. It has that information (`target_program.mod.hub`, the
JIR tier) and the decision is rim work. What it must not do is hand the
crossing a half-compiled Python tree.

### 6.3 Facts out

The rim needs three things back that are not diagnostics and not code:
the unit's dependency edges (for the program graph and cache
invalidation), the interop manifest (for the native and client lanes),
and the placement/codespace verdicts the emitter reached. All three are
flat records today and all three ride slot 3 as a second JCFX-shaped
container. Nothing about them requires a tree.

## 7. Diagnostics, in one boundary, as data

The wire shape exists. `codegen_ir.jac`'s `CirDiag` (severity, code
string, message, help, module path, four location ints) is already the
JCIR container's diagnostics section, and `frontend.jac`'s `_replay_diags`
already rebuilds `Alert` objects from the parser's `SrcDiag` records on
the far side. M1 unifies them: **one diagnostics list, one shape, one
boundary.**

- Producer-side decisions -- every `Transform.emit` call inside a fused
  pass -- become records. Suppression that depends only on the diagnostic
  code is a producer decision and can happen natively; suppression that
  depends on the source's inline `jac:ignore` comments stays rim-side,
  because the rim owns `Source.inline_suppressions`. Emitting a
  suppressed diagnostic and dropping it at the rim is correct and costs
  one record.
- `Transform.ice` (E9001) is a record, not an exception across the
  boundary. A native pass that must abort sets the unit's error count and
  returns; the rim raises.
- Hard errors stay hard. Version skew, malformed containers, a stack
  imbalance -- these mean the artifact and the runtime disagree and raise
  a bind-class error, exactly as `materialize.jac`'s `_bind_error` and
  `codegen_shim`'s format errors do.

`Alert` construction, `DiagnosticInfo` lookup, severity policy, related
spans and `pretty_print` all stay rim-side, which is where they are
today. The crossing carries the *decision*; the rim carries the
*presentation*. That split is exactly `codegen-ir.md` section 6, applied
to the whole pipeline instead of the tail.

## 8. Lifetime, and the refusal story

### 8.1 The tree dies inside the call

Today a native tree cannot die inside a compile, because it never lives
inside one: the parse crossing materializes and returns, and the native
allocation is dropped on the floor. The measured consequence is the
seal's peak RSS -- about 1.6 GiB per root -- and #8280's answer was a
worker process per root, so the OS reclaims at the process boundary. The
release note for that change says so plainly: the crossings "strand their
native trees in the process that built them".

A fused crossing changes the shape of the question. The native tree is
created, walked, lowered to a container, and unreachable, all inside one
GIL-held call. The runtime is refcounted with a cycle collector
(`na_ir_gen_pass.impl/refcount.impl.jac`,
`na_ir_gen_pass.impl/cycle_collector.impl.jac`); a unitree is cyclic
(`parent` back-links), so refcounting alone will not reclaim it, and the
honest options are two:

1. **Collect at the end of the crossing.** Run the cycle collector over
   the unit's allocations before returning. Correct, and its cost is
   proportional to the tree, which is the same order as building it.
2. **Give the unit a region.** The native lane already has a
   user-facing region arena (`na_ir_gen_pass.impl/arena.impl.jac`). A
   unit-scoped region, freed wholesale at the end of the crossing, is the
   cheaper answer and is what "in-process tree reclaim" should mean.

M1 must not pick by assertion. The crossing is the thing that makes
either possible; the measurement that picks between them is peak RSS over
a full seal, which is exactly the number #8280 made visible. Whichever
wins, the #8280 worker pool stays: it also bounds LLVM's own footprint,
which no tree reclaim touches.

### 8.2 Refusal: verdict before emission, extended

Cross-module trust already has a rule: a caller may only emit a direct
native call to an imported method after the import's verdict is known --
`_forward_declare_imported_ability` consults the imported module's
layout, or `_imported_function_demotions`, before it emits. Verdict
before emission.

The fused crossing extends that rule from a method to a pipeline stage:

- **The unit's pass list is fixed at seal time**, from schedule site A.
  A pass that is not sealed is not in the fused entry. There is no
  runtime choice.
- **A module whose compile needs an unsealed path is not a mid-fusion
  discovery.** It is a rim verdict, taken before the crossing, from facts
  the rim already has: the module's decided codespace, whether it needs
  the native or client lane, whether type checking is requested. The rim
  either enters the fused crossing or runs the staged path; it never
  enters the crossing and then finds out.
- **A refusal inside the crossing is a diagnostic, not a fallback.** The
  emitter already works this way after #8291: a construct it refuses
  fails the compile loudly, because there is no second lane. A fused pass
  that meets a construct it cannot handle emits its diagnostic and the
  unit produces no code object for that module. The rim reports it.
- **A demoted method reached inside the artifact aborts.** That is
  today's rule (`seal_abort_stubs`) and M1 does not soften it. What M1
  changes is the reachability graph the seal audits: the fused entry
  makes many methods reachable that are advertised-but-unreached today,
  so `test_sealed_seam_reachability.jac` and the demotion audit get
  strictly stronger, and some current waivers will stop being waivable.
  That is the point.

## 9. Incremental compatibility: the key law is untouched

`jir.jac` splits the two keys on purpose and M1 must not blur them:

- `module_cache_key` (`SEC_MODKEY`) = toolchain fingerprint + project
  fingerprint + content sha + related-file digests. **No compiler
  digest.** This is *identity*: which source, compiled under which
  project settings.
- `module_env_fingerprint` (`SEC_ENVKEY`) = the same plus
  `running_compiler_digest()`. This is *validity*: whether the compiler
  that produced this artifact is the compiler running now.

**Compiler identity gates validity, never identity.** A fused crossing is
a change to the compiler, so it moves ENVKEY and invalidates caches once,
which is correct and is what ENVKEY is for. It must not enter MODKEY: a
cache-warm run and a cold run must key a given source identically, which
is the invariant #8239 restored and which a "was this compiled through
the fused crossing" bit in MODKEY would break again.

The unit granularity of section 3 does not change the key granularity.
Keys stay per source file. A unit's cache decision is the conjunction of
its members' decisions, taken at the rim before the crossing; a hit on
every member skips the crossing entirely, and a miss on any member
compiles the unit. That is the same rule the driver applies today when it
decides whether to recompile a module whose annex changed --
`_related_files` already folds annex digests into MODKEY.

## 10. The ladder, with no two-implementations era

The repo's law is that main carries no dormant-by-flag code. A fused
crossing beside a staged one is exactly that, so the sequencing has to be
one of *replacement in slices*, not *addition then removal*. Each rung
below deletes what the previous rung made redundant, and each is
independently reviewable.

**Rung 0 -- the parity canary, first. Landed.**
`tests/compiler/passes/native/test_m1_fused_crossing.jac`: the fused pass
list produces the container the staged schedule produces, for a corpus of
real modules, compared as container bytes and, on mismatch, as code
objects field by field (never marshal bytes -- `codegen-ir.md` section
10). It also pins the census of section 1 as an executable assertion, so
the day a sealed pass actually executes, the test that says the binder
retains no callable fails and forces its own update; and it pins the
section-11.1 blocker as a self-clearing refusal keyed on
`transform.jac`'s absence from the sealed closure. Four tests, green on
`bd272af79`. This is what makes every later rung falsifiable, which is
why it is rung 0 rather than a deliverable of rung 3.

**Rung 1 -- the pass base chain stops being generic.**
`BaseTransform[T_in, T_out]` and `Transform[T, R]` are generic
archetypes, and genericity is what makes a pass unconstructible natively
(measured: section 11.1). Either the base chain is monomorphized -- every
compiler pass is `Module -> Module`, so the parameters carry no
information the driver uses -- or the native lane learns generic
archetype construction and field access. The first is a compiler-side
change and is small; the second is native-lane work with a much wider
blast radius. Measure both before choosing; do not do both.

Two smaller deletions ride along, because they block the same door.
`BaseTransform.prog` is a `JacProgram`-typed required field and is the
first error message you meet; exactly one sealed pass reads it --
`jcir_gen_pass.impl.jac`'s `native_only_import_target(nd, self.prog)` --
and that read is an import-resolution question, which is the
`absorbed_mod` pattern's home ground. Retire the read into a stamped
fact, then move `prog` off `BaseTransform` onto the rim-facing subtype
the unsealed passes use.

**Rung 2 -- `transform.jac` joins the sealed closure.** Today the sealed
pass artifacts contain the passes' own method bodies and **not** their
base classes: `jcir_gen_pass`'s measured closure is eight modules and
contains neither `uni_pass.jac` nor `module_codegen_pass.jac` nor
`transform.jac`. Once rung 1 lands the chain can lower, and this rung
finishes the job: `time.time()` leaves `timed_transform` (timing is a rim
measurement), and the tier check leaves `postinit` for the rim, where it
belongs -- it is a statement about the *image*, not about a pass. Until
`BaseTransform.postinit` exists natively, a natively constructed pass is
a hollow object whose transform never ran.

**Rung 3 -- the fused root, with two passes.** Generate
`jc_compile_unit` into a fused root, sealed with the parser, `uni_pass`,
`transform`, two sealed passes and the emitter in its closure. The
crossing takes one source and no facts, and returns the container. Rung
0's canary goes green on its sealed half. This is the rung that proves
the shape; it does not yet replace anything, and it is the one rung that
adds before it deletes -- which is acceptable exactly because the thing
it adds is not reachable from the driver yet.

**Rung 4 -- the rim switches, with a verdict.** The driver decides, per
module, whether the module is in the fusible class (section 11.3) and
either enters the crossing or takes the staged path. Writing that
predicate is this rung's first deliverable, not an afterthought: without
it the class is a corpus, and a corpus goes stale in silence. For a
fusible module the crossing spans parse to container and the materializer
never runs; for the rest, the crossing spans the prefix and the
materializer runs once at its end to hand the tree to the unsealed
middle. The Python bodies of the fused passes leave the compile path in
the same change -- they stay for the tooling lane only where the tooling
lane still needs them, and where it does not, they go. Peak RSS is
re-measured here; the crossing is now on the hot path.

**Rung 5 -- the facts container.** Annex sources and the scalar eight
cross in; `parse_str`'s annex parsing and `JacAnnexPass`'s Python body
leave the compile path. The absorb rewrite (section 6.2) lands with it,
because `absorbed_mod` and `annex_mods` are the same problem.

**Rung 6 -- the fusible class grows to everything.** The middle
materialization dies when no module needs the staged path, which is when
M2 has sealed the analysis cluster. Rung 4 already spans parse to
container for the fusible class; this rung deletes the *other* branch,
and with it the last compile-path materialization. It cannot land before
M2, and pretending otherwise is the failure mode this document exists to
prevent. See sections 11.2 and 11.3.

**Rung 7 -- deletion accounting.** The zero-objects-materialized counter
(section 2.3) becomes a real counter, asserted in CI, and the
materializer fleet's compile-path callers are gone. That is M4's trigger
and it fires here.

### 10.1 What the canary compares

Container bytes, primarily: the container is the artifact the pipeline
ships on, and byte equality across lanes is the cheapest honest signal.
On mismatch, the canary compiles both transcribed trees and diffs code
objects field by field, recursively through `co_consts`, because
`marshal.dumps` sets a per-object `FLAG_REF` bit from the object's
transient refcount and is therefore not a stable parity token. That
comparison helper already exists twice in the tree
(`test_jcir_sealed_bytes.jac`, `precompile_bytecode.jac`); the canary
uses it rather than inventing a third.

## 11. The measured blockers

### 11.1 A natively constructed pass is hollow, and it does not say so

Measured on this tree, by native-lane census (`na_census_begin` /
`na_census_end`) over a probe root compiled exactly the way
`seal_native_roots` compiles a root, with the emitted LLVM read back:

- `parse_program(src, path)` **lowers**. A native root can parse.
- Constructing a `UniPass` subclass **demotes**, with the reason
  `constructor argument 'LocalCountPass(prog=...)'`. `prog` is typed
  `JacProgram`, which arrives through `import type from` and therefore
  has no native layout.
- With `prog` loosened to `any`, constructing a **locally defined**
  `UniPass` subclass stops demoting -- and what it lowers to is the
  problem. The emitted body is:

  ```llvm
  %r.i62 = call ptr @PyTuple_New(i64 6)              ; the ctor's kwargs
  %pyb._pyb_instantiate = call ptr @__jac_pyb_bridge(ptr @.pyb.str.9, ptr %r.i62)
  call void @Py_DecRef(ptr nonnull %pyb._pyb_instantiate)
  %field.seen = load i64, ptr %default.seen, align 8 ; the *declared default*
  ret i64 %field.seen
  ```

  The construction is a libpython round trip to `_pyb_instantiate`; the
  resulting object is immediately released and discarded; and the
  subsequent field read comes from the field's declared default, not from
  the object. The pass does not run, the read is wrong, and nothing
  diagnoses either. `ir_in` even crosses as `PyLong_FromLongLong` of the
  raw native address.
- Constructing an **imported** sealed pass (`ASTValidationPass`,
  `JcirGenPass`) still demotes -- the pass module never joins the closure
  at all.

The obvious suspects were then eliminated one at a time, each by reading
the emitted LLVM rather than the census verdict:

| Probe | pyb bridges in the emitted body | Verdict |
|---|---|---|
| a plain local `obj`, constructed and mutated | 0 | ordinary archetype construction is real native code |
| an imported *unitree* archetype (`Name`), constructed | 0 | cross-module construction of a closure member is real native code |
| a local `obj` with an `ABC` base | 0 | an `ABC` base is not the problem |
| a local `obj` parameterized on a generic archetype | -- | **demoted**, on the *field read*: "Native lowering failed for expression 'BinaryExpr'" over `g.marker + g.extra` |
| a `UniPass` subclass | 1, discarded | the hollow construction above |

So it is not cross-module-ness, not `ABC`, and not closure membership.
**It is genericity.** `BaseTransform[T_in, T_out]` and `Transform[T, R]`
are generic archetypes; the native lane demotes field access on a
generic-parameterized archetype -- shipping an `abort()` stub, which is
the correct loud behavior -- and falls back to a discarded
`_pyb_instantiate` for construction of a subclass of one, which is not.
Every pass in the compiler inherits from that chain, which is why
`transform.jac` is in no closure: it cannot be, and the absence is a
symptom rather than the disease.

Two things follow. The ladder's rung 1 is not "`prog` loses its type" --
that is only the first error message you meet -- it is **"the pass base
classes stop being generic, or the native lane learns generic
archetypes"**, and which of those two is the right fix is a native-lane
question, not a compiler-driver one. And the pyb fallback is a bug worth
filing on its own account: a lowering that silently returns a field's
declared default instead of the constructed object's value is a wrong
answer, not a gap. It is the same failure family as the silent no-ops
#8250 fixed, and the native lane should demote it to an `abort()` stub --
exactly as it already does for the generic field read one row above --
rather than emit it.

### 11.2 The pipeline is not sealed-then-unsealed; it is interleaved

This is the structural fact that bounds M1, and it does not have a
clever way out.

The ir-gen schedule is `ASTValidationPass, SymTabBuildPass,
DeclImplMatchPass, SemanticAnalysisPass, SemDefMatchPass, CFGBuildPass,
MTIRGenPass, JsxIntrinsicGuardPass, PlacementApplyPass`, then inference,
then the type-check cluster, then boundary analysis, then codegen. Of
these, the sealed set covers a **prefix** (through `SemDefMatchPass`),
skips `CFGBuildPass`, `MTIRGenPass` and `PlacementApplyPass`, skips
inference and the entire type-check cluster, and then picks up again at
the **tail** (`JcirGenPass`).

A single fused call can therefore contain the prefix, or the tail, but
not both -- unless it can re-enter Python in the middle, which an AOT
artifact may not do (`seal_native_roots` refuses any root with
`native_imports`). Fusing the prefix and the tail separately requires
handing the tail a Python tree, which needs a Python-to-native
dematerializer that does not exist and that section 5 argues should never
exist.

The alternative -- reordering the schedule so every sealed pass is
contiguous -- is not available: `CFGBuildPass` sits where it does because
`MTIRGenPass` and the type-check cluster require it, and `JcirGenPass`
reads `FuncCall.chunks_recv_type`, stamped by `type_checker_pass`. Those
are real data dependencies, not accidents of ordering.

But the interleaving is a fact about the *schedule*, not necessarily
about any given *module*, and that distinction is measurable. It was
measured.

### 11.3 The unsealed middle is byte-irrelevant for a real class of modules

`tests/compiler/passes/native/test_m1_fused_crossing.jac` runs the
truncated pipeline the fused crossing would perform -- `parse_str`, then
the sealed prefix, then the emitter, with **nothing** from the unsealed
middle and no whole-program stage -- and compares its container against
the full staged pipeline's. On a corpus of four modules, two synthetic
(including one with comments and a string-named `test`) and two real
compiler sources (`jac0core/srcloc.jac`, `jac0core/jcir_facts.jac`):

| Fused pass list | Result |
|---|---|
| ASTValidation, SymTabBuild | **differs** on 2 of 4; the shorter container does not even `compile()` ("expression must have Store context but has Load") |
| + DeclImplMatch | **differs** on 2 of 4 |
| **+ SemanticAnalysis** | **byte-identical on all 4** |
| + SemDefMatch | byte-identical on all 4 |

So for these modules, `CFGBuildPass`, `MTIRGenPass`,
`JsxIntrinsicGuardPass`, `PlacementApplyPass`, inference, the whole
type-check cluster and boundary analysis contribute **nothing the emitter
reads**. A fused call spanning parse to codegen is byte-correct for them
today.

That converts section 11.2 from a wall into a **verdict**. The fused
crossing does not have to wait for M2 to span prefix and tail; it has to
know, before it is entered, whether *this module* is in the fusible
class. That is section 8.2's rule at module granularity, and the rim
already holds the inputs: a module needs the staged path when it uses
`by` abilities (MTIRGen), jsx intrinsics (the guard pass), a non-server
codespace or a client boundary (placement), or any construct whose
emission reads a type-checker stamp (`chunks_recv_type` is the only one
today). Everything else fuses end to end.

Therefore:

**M1's acceptance is "one call from source to container for the fusible
class, with a rim verdict that routes the rest to the staged path", and
M2 is what grows the fusible class to everything.** That is a stronger
claim than "prefix fusion" and a weaker one than "the whole pipeline,
always". It is the one the measurement supports.

The two honest caveats: the fusible predicate is not yet written, and
until it is, the class is defined by a corpus rather than by a rule --
which is exactly the shape that goes stale silently, so the predicate is
rung 4's first deliverable, not an afterthought. And the corpus is four
modules; widening it is cheap and is where the next surprise will come
from.

## 12. Unresolved questions

- **Which reclaim wins.** Section 8.1 names two and refuses to pick
  without the peak-RSS measurement over a full seal. The measurement
  needs rung 3.
- **Bytes across the `jacstr` boundary.** The facts container is bytes,
  and `_native_str_args` encodes operands as UTF-8. Either the container
  is made UTF-8-safe or `_entry_argtypes` grows a raw-bytes operand kind.
  The second is smaller and honest; it is a change to the generated
  crossing header, not to the format.
- **The unit's cache decision.** Section 9 says a miss on any member
  compiles the unit. Whether that over-compiles in practice -- a module
  whose absorbed dependency is unchanged -- is a measurement nobody has
  taken, because absorb-heavy trees are rare in the corpus.
- **Inline suppression at the rim.** Emitting a diagnostic natively and
  dropping it at the rim is correct but changes *which* diagnostics get
  constructed. If a suppressed diagnostic is expensive to format, the
  cost moves. No such diagnostic is known; the question is open.
- **What happens to `mat_parse` when the prefix fuses.** It stays for
  tooling, but the tooling lane and the compile lane then run different
  code for the same job, which is exactly the two-implementations shape
  the ladder avoids elsewhere. The honest answer is probably that
  `mat_parse` becomes the fused entry with the pass list empty, so there
  is one implementation with a parameter rather than two entries.
- **The tier tripwire's replacement.** Once the rim enters the crossing,
  "sealed means served" becomes checkable directly: the rim either took
  the crossing or it did not, and it knows which. What that check looks
  like, and where it lives, is rung 2 work and is not designed here.
- **The fusible predicate.** Section 11.3 measures a class and does not
  define it. The candidate inputs are known (`by` abilities, jsx
  intrinsics, a non-server codespace, a client boundary, and any emission
  that reads a type-checker stamp) but "any emission that reads a
  type-checker stamp" is today a one-element set that nobody has proven
  stays small. The predicate has to be conservative and it has to be
  *checked*: a module wrongly judged fusible produces a wrong container
  silently, which is the one failure mode this whole design is arranged
  to avoid.
