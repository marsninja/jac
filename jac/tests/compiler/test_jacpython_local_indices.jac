"""Local-slot indexing preserves constructor inputs and table replacement."""
import from jaclang.compiler.backends.py.jacpython.compiler_ir { unverified_cfg }

test "local slot indices are invalidated when names change" {
    unit = unverified_cfg(
        filename="slots.py",
        name="f",
        qualname="f",
        firstlineno=1,
        varnames=["argument", "local", "cell"]
    );
    assert unit.varname_index("local") == 1;
    assert unit.varname_index("absent") == -1;
    unit.set_varnames(["cell", "argument", "renamed"]);
    assert unit.varname_index("cell") == 0;
    assert unit.varname_index("argument") == 1;
    assert unit.varname_index("renamed") == 2;
    assert unit.varname_index("local") == -1;
    unit.set_varnames([]);
    assert unit.varname_index("cell") == -1;
}
