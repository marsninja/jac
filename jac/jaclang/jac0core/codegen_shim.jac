import ast;
import marshal;
import sys;

import from jaclang.jac0core.codegen_ir {
    CirContainer,
    CirDiag,
    CirModule,
    CodegenIrFormatError,
    CodegenIrVersionError,
    decode_ops,
    DIAG_ERROR,
    OP_BYTES,
    OP_ELLIPSIS,
    OP_END,
    OP_FALSE,
    OP_FLOAT,
    OP_INT,
    OP_LIST,
    OP_LOC,
    OP_NODE,
    OP_NONE,
    OP_PARSE_SPLICE,
    OP_STR,
    OP_TRUE,
    OP_TUPLE,
    read_container
}

glob _KW_CALL_CACHE: list = [];

def _call_kw(cls: any, kw: dict) -> any {
    if not _KW_CALL_CACHE {
        _KW_CALL_CACHE.append(eval("lambda cls, kw: cls(**kw)"));
    }
    return _KW_CALL_CACHE[0](cls, kw);
}

obj CirSplice {
    has stmts: list = [];
}

obj CirCompiledModule {
    has mod_path: str,
        code_obj: any = None,
        bytecode: bytes = b"";
}

obj CirResult {
    has modules: list = [],
        diags: list = [];
}

def cir_bind_classes(class_names: list) -> list {
    out: list = [];
    for name in class_names {
        cls = getattr(ast, name, None);
        if cls is None {
            raise CodegenIrVersionError(
                f"container names ast class {name} which this CPython "
                f"({sys.version_info.major}.{sys.version_info.minor}) does not "
                "provide; the producer and this runtime disagree"
            );
        }
        out.append(cls);
    }
    return out;
}

def cir_bind_keys(key_names: list) -> list {
    out: list = [];
    for name in key_names {
        out.append(sys.intern(name));
    }
    return out;
}

def _parse_splice(src: str, line_offset: int) -> CirSplice {
    tree = ast.parse(src);
    if line_offset {
        for w in ast.walk(tree) {
            if hasattr(w, "lineno") {
                w.lineno += line_offset;
            }
            if w?.end_lineno is not None {
                w.end_lineno += line_offset;
            }
        }
    }
    return CirSplice(stmts=list(tree.body));
}

def _pop_values(stack: list, n: int, what: str) -> list {
    if n > len(stack) {
        raise CodegenIrFormatError(
            f"{what} pops {n} values but only {len(stack)} are on the stack"
        );
    }
    vals: list = [];
    i = 0;
    while i < n {
        vals.append(stack.pop());
        i += 1;
    }
    vals.reverse();
    return vals;
}

def transcribe_module(
    mod: CirModule, classes: list, keys: list, container: CirContainer
) -> any {
    ops = decode_ops(mod.code, container);
    stack: list = [];
    loc_set = False;
    l1 = 0;
    c1 = 0;
    l2 = 0;
    c2 = 0;
    for op in ops {
        kind = op[0];
        if kind == OP_NODE {
            if not loc_set {
                raise CodegenIrFormatError(
                    f"OP_NODE before any OP_LOC in {mod.mod_path}: the location "
                    "register is unset"
                );
            }
            cls = classes[op[1]];
            krefs = op[2];
            vals = _pop_values(stack, len(krefs), f"OP_NODE {cls.__name__}");
            kw: dict = {};
            j = 0;
            while j < len(krefs) {
                if isinstance(vals[j], CirSplice) {
                    raise CodegenIrFormatError(
                        f"a splice chunk reached OP_NODE {cls.__name__} as a "
                        "field value; splices may only be spliced into OP_LIST"
                    );
                }
                kw[keys[krefs[j]]] = vals[j];
                j += 1;
            }
            try {
                built = _call_kw(cls, kw);
            } except TypeError as e {
                raise CodegenIrFormatError(
                    f"ast.{cls.__name__} rejected its fields in {mod.mod_path}: {e}"
                );
            }
            built.lineno = l1;
            built.col_offset = c1;
            built.end_lineno = l2;
            built.end_col_offset = c2;
            stack.append(built);
        } elif kind == OP_LOC {
            l1 = op[1];
            c1 = op[2];
            l2 = op[3];
            c2 = op[4];
            loc_set = True;
        } elif kind == OP_STR
            or kind == OP_INT
            or kind == OP_FLOAT
            or kind == OP_BYTES {
            stack.append(op[1]);
        } elif kind == OP_NONE {
            stack.append(None);
        } elif kind == OP_TRUE {
            stack.append(True);
        } elif kind == OP_FALSE {
            stack.append(False);
        } elif kind == OP_ELLIPSIS {
            stack.append(Ellipsis);
        } elif kind == OP_LIST {
            vals = _pop_values(stack, op[1], "OP_LIST");
            out: list = [];
            for v in vals {
                if isinstance(v, CirSplice) {
                    out.extend(v.stmts);
                } else {
                    out.append(v);
                }
            }
            stack.append(out);
        } elif kind == OP_TUPLE {
            vals = _pop_values(stack, op[1], "OP_TUPLE");
            for v in vals {
                if isinstance(v, CirSplice) {
                    raise CodegenIrFormatError(
                        "a splice chunk reached OP_TUPLE; splices may only be spliced into OP_LIST"
                    );
                }
            }
            stack.append(tuple(vals));
        } elif kind == OP_PARSE_SPLICE {
            stack.append(_parse_splice(op[1], op[2]));
        } elif kind == OP_END {
            if len(stack) != 1 {
                raise CodegenIrFormatError(
                    f"module stream for {mod.mod_path} ended with stack depth "
                    f"{len(stack)}; exactly one root is required"
                );
            }
            root = stack[0];
            if not isinstance(root, ast.Module) {
                raise CodegenIrFormatError(
                    f"module stream for {mod.mod_path} left a "
                    f"{type(root).__name__} on the stack; the root must be "
                    "ast.Module"
                );
            }
            return root;
        } else {
            raise CodegenIrFormatError(
                f"decoder produced an op this shim does not interpret: {kind}"
            );
        }
    }
    raise CodegenIrFormatError(f"module stream for {mod.mod_path} has no OP_END");
}

def transcribe(ir_bytes: bytes) -> tuple {
    container = read_container(ir_bytes);
    classes = cir_bind_classes(container.class_names);
    keys = cir_bind_keys(container.key_names);
    mods: list = [];
    for m in container.modules {
        mods.append((m.mod_path, transcribe_module(m, classes, keys, container), ));
    }
    return (mods, list(container.diags), );
}

def compile_ir(ir_bytes: bytes) -> CirResult {
    container = read_container(ir_bytes);
    classes = cir_bind_classes(container.class_names);
    keys = cir_bind_keys(container.key_names);
    diags = list(container.diags);
    modules: list = [];
    for m in container.modules {
        root = transcribe_module(m, classes, keys, container);
        try {
            code_obj = compile(root, m.mod_path, "exec");
        } except (ValueError, SyntaxError, TypeError) as e {
            diags.append(
                CirDiag(
                    message=str(e),
                    code_str="E5043",
                    mod_path=m.mod_path,
                    severity=DIAG_ERROR
                )
            );
            continue;
        }
        modules.append(
            CirCompiledModule(
                mod_path=m.mod_path, code_obj=code_obj, bytecode=marshal.dumps(code_obj)
            )
        );
    }
    return CirResult(modules=modules, diags=diags);
}
