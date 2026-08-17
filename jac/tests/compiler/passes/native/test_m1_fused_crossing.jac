"""The M1 fused crossing's contract, written before the crossing (epic #8288).

`internals/fused-pipeline.md` specifies a single native call that parses,
runs the sealed passes, and emits the JCIR container, with no tree ever
surfacing as a Python object. This file is that design's falsifier. It
holds three claims, and each one is calibrated to fail the day the thing
it describes changes, so none of them can quietly go stale.

  1. **The byte contract.** The fused entry runs a *fixed* pass list,
     baked at seal time, and its container has to be the container the
     staged schedule produces -- otherwise fusing changes the program.
     The list is not the whole ir-gen schedule: the fused entry can only
     contain passes the seal ships, and today that is a prefix
     (ASTValidation, SymTabBuild, DeclImplMatch, SemanticAnalysis,
     SemDefMatch) plus the emitter at the tail, with CFGBuild, MTIRGen
     and PlacementApply unsealed in between. This test measures which
     prefix is byte-sufficient on a corpus of real modules. It is green
     today, which is the point: it says the fusion is *allowed* to skip
     the unsealed middle for these inputs, and it will name the module
     and the byte offset the moment that stops being true.

  2. **The census, as an assertion.** Thirteen of the fourteen sealed
     roots are bound and never called; the pass-serving binder retains a
     `DylibEngine` and no callable, and the only `ctypes` entry bindings
     into a sealed compiler artifact live in `parser/materialize.jac`.
     That is the crossing surface M1 empties, and it is asserted here so
     that the first sealed pass to actually execute breaks this test
     rather than passing unnoticed.

  3. **The blocker, pinned self-clearing.** A sealed pass artifact
     carries the pass's own method bodies and not its base classes:
     `jac0core/passes/transform.jac` is in no root's closure. It cannot
     be, because `BaseTransform[T_in, T_out]` is a generic archetype and
     the native lane does not lower generic archetype construction or
     field access. Measured on `bd272af79` by reading the emitted LLVM:
     a `UniPass` subclass constructed inside a native root lowers to
     `__jac_pyb_bridge("_pyb_instantiate", ...)`, whose result is
     immediately `Py_DecRef`ed and discarded, after which the field read
     returns the field's *declared default* -- the pass does not run and
     nothing says so. A plain local archetype, an imported unitree
     archetype, and an archetype with an `ABC` base all lower to real
     native code with no bridge, which is how genericity was isolated.
     This test pins the closure fact; when rungs 1 and 2 of the ladder
     put `transform.jac` in the closure, this fails and the pin comes
     out.
"""

import jaclang;
import from pathlib { Path }
import from jaclang.jac0core.compile_options { CompileOptions }
import from jaclang.jac0core.compiler { get_ir_gen_sched, run_pass }
import from jaclang.jac0core.passes.ast_validation_pass { ASTValidationPass }
import from jaclang.jac0core.passes.decl_impl_match_pass { DeclImplMatchPass }
import from jaclang.jac0core.passes.jcir_gen_pass { JcirGenPass }
import from jaclang.jac0core.passes.semantic_analysis_pass { SemanticAnalysisPass }
import from jaclang.jac0core.passes.sym_tab_build_pass { SymTabBuildPass }
import from jaclang.jac0core.program { JacProgram }
import from jaclang.jac0core.runtime { JacRuntime as Jac }
import from jaclang.utils.precompile_bytecode {
    NATIVE_SEAL_CLOSURE,
    NATIVE_SEAL_ROOTS
}

glob PKG_PATH = Path(jaclang.__file__).parent,
     FUSED_PASSES: tuple = (
         ASTValidationPass,
         SymTabBuildPass,
         DeclImplMatchPass,
         SemanticAnalysisPass
     ),
     TRANSFORM_SRC: str = "jac0core/passes/transform.jac",
     CORPUS: tuple = (
         (
             "a self-contained module",
             "\"\"\"Doc.\"\"\"\nglob seeded: int = 1;\n\nobj Gadget {\n"
                 + "    has n: int = 1;\n\n    def bump -> int {\n"
                 + "        self.n += seeded;\n        return self.n;\n    }\n}\n\n"
                 + "def widen(n: int) -> int {\n    total = n + seeded;\n"
                 + "    for i in range(3) {\n        total += i;\n    }\n"
                 + "    return total;\n}\n\nwith entry {\n    g = Gadget();\n"
                 + "    widened = widen(g.bump());\n}\n",
             "m1_unit.jac"
         ),
         (
             "comments and a string-named test",
             "# leading\nglob base: int = 2;\n\ndef triple(x: int) -> int {\n"
                 + "    return x * base;  # tail\n}\n\n"
                 + "test \"a named test with 'quotes'\" { assert triple(3) == 6; }\n",
             "m1_comments.jac"
         ),
         (
             "a real compiler module",
             "",
             "jac0core/srcloc.jac"
         ),
         (
             "a real compiler module of baked facts",
             "",
             "jac0core/jcir_facts.jac"
         )
     );

glob _CODE_FIELDS: tuple = (
         "co_name",
         "co_qualname",
         "co_filename",
         "co_firstlineno",
         "co_argcount",
         "co_posonlyargcount",
         "co_kwonlyargcount",
         "co_nlocals",
         "co_stacksize",
         "co_flags",
         "co_code",
         "co_names",
         "co_varnames",
         "co_freevars",
         "co_cellvars",
         "co_linetable",
         "co_exceptiontable"
     );

glob _OPTS: dict = {
         "force_codespace": "server",
         "skip_native_engine": True,
         "no_ir_cache": True,
         "suppress_codes": ["E5092"]
     };

"""Source text for a corpus entry; an empty body means read it from the package."""
def corpus_source(text: str, path: str) -> str {
    if text {
        return text;
    }
    return (PKG_PATH / path).read_text(encoding="utf-8");
}

"""Container bytes from the full staged pipeline."""
def staged_bytes(text: str, path: str) -> bytes {
    prog = JacProgram();
    mod = prog.compile(use_str=text, file_path=path, options=CompileOptions(**_OPTS));
    assert len(prog.errors_had) == 0 , str([str(e) for e in prog.errors_had]);
    return JcirGenPass(ir_in=mod, prog=prog).ir_bytes();
}

"""Container bytes from parse plus the fused pass list alone, emitter at the tail.

This is the truncated pipeline the fused crossing performs: `parse_str`
(read, parse, coerce the codespace, weave the annexes, register the hub),
then the fused passes in schedule order, then the emitter. Nothing from
the unsealed middle runs, and no whole-program stage runs.
"""
def fused_bytes(text: str, path: str) -> bytes {
    prog = JacProgram();
    prog._compile_options = CompileOptions(**_OPTS);
    mod = Jac.get_compiler().parse_str(text, path, prog);
    for pass_cls in FUSED_PASSES {
        run_pass(pass_cls, mod, prog);
    }
    return JcirGenPass(ir_in=mod, prog=prog).ir_bytes();
}

def first_difference(left: bytes, right: bytes) -> int {
    n = min(len(left), len(right));
    i = 0;
    while (i < n and left[i] == right[i]) {
        i += 1;
    }
    return i;
}

"""Field-by-field code comparison, recursive through co_consts.

Never marshal bytes: `marshal.dumps` sets a per-object FLAG_REF bit from
the object's transient refcount, so two identical code objects can
marshal differently (internals/codegen-ir.md section 10).
"""
def code_field_diffs(a: any, b: any, at: str, out: list[str]) -> list[str] {
    import types;
    for attr in _CODE_FIELDS {
        av = getattr(a, attr, None);
        bv = getattr(b, attr, None);
        if av != bv {
            out.append(f"{at}.{attr}: staged {av!r} vs fused {bv!r}");
        }
    }
    if len(a.co_consts) != len(b.co_consts) {
        out.append(
            f"{at}.co_consts length {len(a.co_consts)} vs {len(b.co_consts)}"
        );
        return out;
    }
    for (i, (x, y)) in enumerate(zip(a.co_consts, b.co_consts)) {
        if (isinstance(x, types.CodeType) and isinstance(y, types.CodeType)) {
            code_field_diffs(x, y, f"{at}.consts[{i}]", out);
        } elif (x != y or type(x) is not type(y)) {
            out.append(f"{at}.consts[{i}]: staged {x!r} vs fused {y!r}");
        }
    }
    return out;
}

"""Where two containers disagree, named by code-object field where possible."""
def container_diff_detail(staged: bytes, fused: bytes, path: str) -> str {
    import from builtins { compile as pycompile }
    import from jaclang.jac0core.codegen_shim { transcribe }
    detail = (
        f"fused {len(fused)} bytes vs staged {len(staged)} bytes, first "
        f"difference at offset {first_difference(fused, staged)}"
    );
    (staged_mods, _sd) = transcribe(staged);
    (fused_mods, _fd) = transcribe(fused);
    if (len(staged_mods) != len(fused_mods)) {
        return (
            detail
            + f"; the containers carry {len(staged_mods)} and "
            + f"{len(fused_mods)} module records"
        );
    }
    try {
        diffs = code_field_diffs(
            pycompile(staged_mods[0][1], path, "exec"),
            pycompile(fused_mods[0][1], path, "exec"),
            "<module>",
            []
        );
    } except Exception as compile_exc {
        return (
            detail
            + f"; the fused container does not compile ("
            + f"{type(compile_exc).__name__}: {compile_exc}), so the shorter "
            + "pass list did not merely reorder the recipe, it produced an "
            + "invalid one"
        );
    }
    if not diffs {
        return (
            detail
            + " (the code objects agree field for field, so the divergence is "
            + "in the container encoding, not the recipe)"
        );
    }
    return detail + "\n      " + "\n      ".join(diffs[:8]);
}


test "the fused pass list emits the container the staged schedule emits" {
    failures: list[str] = [];
    for (label, text, path) in CORPUS {
        src = corpus_source(text, path);
        staged = staged_bytes(src, path);
        fused = fused_bytes(src, path);
        if fused == staged {
            continue;
        }
        failures.append(
            f"{label} ({path}): " + container_diff_detail(staged, fused, path)
        );
    }
    assert not failures , (
        f"{len(failures)} corpus module(s) get a different JCIR container from "
        "the fused pass list than from the staged schedule. The fused crossing "
        "bakes its pass list at seal time and cannot re-enter Python for the "
        "unsealed middle, so a divergence here means the pass list is wrong or "
        "an unsealed pass stamps something the emitter reads:\n  "
        + "\n  ".join(failures)
    );
}


test "the fused pass list is a real prefix of the executed schedule" {
    sched = [p.__name__ for p in get_ir_gen_sched()];
    fused = [p.__name__ for p in FUSED_PASSES];
    assert sched[:len(fused)] == fused , (
        f"the fused pass list {fused} is not a prefix of the ir-gen schedule "
        f"{sched}. The fused entry must run the schedule's passes in the "
        "schedule's order; a reordering here is a semantic change, not a "
        "refactor"
    );
    unsealed_middle = [
        name
        for name in sched[len(fused):]
        if name in ("CFGBuildPass", "MTIRGenPass", "PlacementApplyPass")
    ];
    assert unsealed_middle , (
        "no unsealed pass remains between the fused prefix and the emitter. "
        "That is the M2 condition for fusing the tail into the same call "
        "(internals/fused-pipeline.md section 11.2); when it holds, this "
        "assertion comes out and the tail joins FUSED_PASSES"
    );
}


test "the pass-serving binder binds sealed artifacts and calls none of them" {
    serve_src = (PKG_PATH / "jac0core/passes/pass_serve.jac").read_text(
        encoding="utf-8"
    );
    assert "PYFUNCTYPE" not in serve_src , (
        "pass_serve.jac now binds a GIL-held crossing entry. That is the M1 "
        "fused crossing arriving, which is good -- and it means the census "
        "this file pins (thirteen sealed roots bound and never called) is "
        "stale. Update the census in internals/fused-pipeline.md section 1 "
        "and this test together"
    );
    assert serve_src.count("CFUNCTYPE") == 1 , (
        "pass_serve.jac binds more than the one `jac_str_new` liveness probe. "
        "Every additional binding is a native entry the binder can call, so "
        "the crossing surface changed; re-measure it"
    );
    assert "_BIND_KEEP.append(eng)" in serve_src , (
        "the binder no longer retains only the DylibEngine. If it now retains "
        "callables, sealed passes are being served for real and this file's "
        "claim 2 has to be rewritten"
    );
}


test "the sealed closure still carries no pass base class" {
    assert TRANSFORM_SRC not in NATIVE_SEAL_CLOSURE , (
        f"{TRANSFORM_SRC} joined the sealed closure. That is rung 2 of the "
        "fused-pipeline ladder: BaseTransform.postinit can now exist "
        "natively, so a natively constructed pass is no longer hollow. Drop "
        "this pin and take the hollow-construction blocker out of "
        "internals/fused-pipeline.md section 11.1"
    );
    assert TRANSFORM_SRC not in NATIVE_SEAL_ROOTS , (
        f"{TRANSFORM_SRC} became a seal root; see the message above"
    );
    for pass_root in NATIVE_SEAL_ROOTS {
        if not pass_root.endswith("_pass.jac") {
            continue;
        }
        assert pass_root in NATIVE_SEAL_CLOSURE , (
            f"{pass_root} is a seal root but is absent from "
            "NATIVE_SEAL_CLOSURE, so no other root can reach it. A fused "
            "root has to reach every pass it runs"
        );
    }
}
