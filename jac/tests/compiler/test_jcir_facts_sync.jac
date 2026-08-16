"""The JCIR emitter's baked name facts match their sources of truth.

`jcir_facts.jac` bakes two name sets and one path predicate so the
emitter can lower natively instead of importing a Python module or
scanning a stub file at emit time. Baked data drifts silently, so each
fact is pinned against the thing it bakes: grow the runtime builtin
surface, the ambient typing stub, or the annex suffixes without
re-baking and this file fails naming the delta.
"""

import os;
import jaclang;
import jaclang.runtimelib.builtin as builtin_mod;
import from pathlib { Path }
import from jaclang.jac0core.ext_registry { is_test }
import from jaclang.jac0core.jcir_facts {
    JCIR_AMBIENT_TYPING_NAMES,
    JCIR_BUILTIN_NAMES,
    jcir_is_test_path
}


test "the baked builtin surface matches runtimelib.builtin.__all__" {
    live = set(builtin_mod.__all__);
    baked = set(JCIR_BUILTIN_NAMES);
    assert baked == live , (
        f"jcir_facts.JCIR_BUILTIN_NAMES drifted from builtin.__all__: "
        f"missing {sorted(live - baked)}, stale {sorted(baked - live)} -- "
        "re-bake the frozenset"
    );
}


test "the baked ambient typing names match typing_ambient.pyi" {
    stub = (
        Path(jaclang.__file__).parent
        / "compiler"
        / "type_system"
        / "typing_ambient.pyi"
    );
    text = stub.read_text(encoding="utf-8");
    marker = text.find("__all__ =");
    open_b = text.find("[", marker);
    close_b = text.find("]", open_b);
    live: set[str] = set();
    for piece in text[open_b + 1:close_b].split(",") {
        entry = piece.strip();
        if len(entry) >= 2 and entry[0] in "'\"" and entry[-1] == entry[0] {
            live.add(entry[1:-1]);
        }
    }
    baked = set(JCIR_AMBIENT_TYPING_NAMES);
    assert baked == live , (
        f"jcir_facts.JCIR_AMBIENT_TYPING_NAMES drifted from the stub: "
        f"missing {sorted(live - baked)}, stale {sorted(baked - live)} -- "
        "re-bake the frozenset"
    );
}


test "the baked test-path predicate agrees with ext_registry" {
    for path in (
        "/a/foo.test.jac",
        "/a/foo.impl.jac",
        "/a/foo.jac",
        "foo.test.jac",
        "/a/foo.test.jac.bak"
    ) {
        assert jcir_is_test_path(path) == is_test(path) , (
            f"jcir_is_test_path diverges from ext_registry.is_test on " f"{path!r}"
        );
    }
}
