/*
 * m3_modty -- native JCIR -> mod_ty transcription spike (epic #8288, M3).
 *
 * This translation unit is the shape the generated native transcriber takes:
 * it consumes a decoded JCIR container (class table, key table, string pool,
 * one module instruction stream), builds a CPython `mod_ty` directly in a
 * `PyArena`, and hands it to `_PyAST_Compile`. No `ast` module objects are
 * created anywhere on this path.
 *
 * It is a SPIKE. It covers a deliberate handful of the fifteen JCIR opcodes
 * and thirteen ast classes -- exactly enough to build a module with a
 * function definition, a return, literals and a call -- so that the output
 * can be compared field by field against the Python shim seat's output for
 * the same bytes. Nothing here is wired into the compiler.
 *
 * Build: see build_spike.py. Requires a CPython that ships its internal
 * headers (Include/internal/pycore_*.h), compiled with -DPy_BUILD_CORE, and
 * linked against that same CPython.
 */

#include <Python.h>

#include <stddef.h>
#include <string.h>

#include "pycore_asdl.h"
#include "pycore_ast.h"
#include "pycore_compile.h"
#include "pycore_pyarena.h"

/* ------------------------------------------------------------------ *
 * 1. The version pin.
 *
 * The AST struct layouts, the `_expr_kind` / `_stmt_kind` enumerator
 * VALUES, and the constructor argument orders are all generated per
 * CPython minor from Parser/Python.asdl. They are stable WITHIN a minor
 * and change BETWEEN minors (measured 3.12 -> 3.15: `type_param` gained
 * `default_value`; `expr_kind` was renumbered when `Interpolation_kind`
 * was inserted; `Import` gained `is_lazy`). This TU therefore compiles
 * against exactly one CPython's headers and refuses at BUILD time on any
 * minor it has not been audited against. It never guesses.
 * ------------------------------------------------------------------ */

#if PY_VERSION_HEX < 0x030C0000
#error "m3_modty: CPython 3.12 is the oldest audited AST layout; refusing to build."
#endif
#if PY_VERSION_HEX >= 0x030F0000
#error "m3_modty: CPython 3.15+ has an unaudited AST layout (Import.is_lazy et al); \
add a layout audit for this minor before building. Refusing to guess."
#endif

/* The second half of the pin: the headers this TU compiled against must be
 * the headers of the interpreter it is loaded into. A shared object built
 * against 3.13 and dlopened into 3.14 would read the wrong union arm at the
 * wrong offset and produce silently wrong code, so the check is fatal at
 * module init, not a warning. */
#define MODTY_BUILT_MAJOR ((PY_VERSION_HEX >> 24) & 0xFF)
#define MODTY_BUILT_MINOR ((PY_VERSION_HEX >> 16) & 0xFF)

/* ------------------------------------------------------------------ *
 * 2. JCIR opcodes (jac0core/codegen_ir.jac).
 * ------------------------------------------------------------------ */

enum {
    OP_NONE = 1,
    OP_TRUE = 2,
    OP_FALSE = 3,
    OP_ELLIPSIS = 4,
    OP_INT = 5,
    OP_INT_BIG = 6,
    OP_FLOAT = 7,
    OP_STR = 8,
    OP_BYTES = 9,
    OP_LIST = 10,
    OP_TUPLE = 11,
    OP_NODE = 12,
    OP_LOC = 13,
    OP_PARSE_SPLICE = 14,
    OP_END = 15
};

/* ------------------------------------------------------------------ *
 * 3. The class table this spike knows.
 *
 * A generated transcriber emits one enumerator per ast class named by the
 * seal-time class table, plus its kind: NODE classes carry a constructor,
 * ENUM classes (expr_context, operator, boolop, unaryop, cmpop -- ASDL sums
 * whose constructors have no fields) carry an integer. That distinction is
 * invisible on the Python shim path, where `ast.Load()` is an object like
 * any other; on the mod_ty path `Load` is the integer 1 and has no location.
 * ------------------------------------------------------------------ */

typedef enum {
    C_UNKNOWN = 0,
    C_Module,
    C_FunctionDef,
    C_arguments,
    C_arg,
    C_Return,
    C_Expr,
    C_Assign,
    C_Constant,
    C_Call,
    C_keyword,
    C_Name,
    C_Load,
    C_Store
} class_id;

static class_id
class_id_of(const char *name)
{
    /* A generator emits a perfect hash here; strcmp is fine for a spike and
     * runs once per container, not once per node. */
    if (strcmp(name, "Module") == 0) return C_Module;
    if (strcmp(name, "FunctionDef") == 0) return C_FunctionDef;
    if (strcmp(name, "arguments") == 0) return C_arguments;
    if (strcmp(name, "arg") == 0) return C_arg;
    if (strcmp(name, "Return") == 0) return C_Return;
    if (strcmp(name, "Expr") == 0) return C_Expr;
    if (strcmp(name, "Assign") == 0) return C_Assign;
    if (strcmp(name, "Constant") == 0) return C_Constant;
    if (strcmp(name, "Call") == 0) return C_Call;
    if (strcmp(name, "keyword") == 0) return C_keyword;
    if (strcmp(name, "Name") == 0) return C_Name;
    if (strcmp(name, "Load") == 0) return C_Load;
    if (strcmp(name, "Store") == 0) return C_Store;
    return C_UNKNOWN;
}

/* ------------------------------------------------------------------ *
 * 4. Stack values.
 *
 * The shim's operand stack holds PyObjects. The mod_ty stack cannot: an
 * `asdl_stmt_seq *`, an `expr_ty` and an `expr_context_ty` are three
 * unrelated C types. Operands are therefore tagged, and coercion happens at
 * CONSUMPTION -- the field being filled decides what an OP_INT or an OP_NONE
 * means. That is the one structural difference between the two transcribers
 * and it is resolvable entirely from the ASDL, which is why the native side
 * is generated rather than hand-written.
 * ------------------------------------------------------------------ */

typedef enum {
    V_ABSENT = 0, /* OP_NONE: NULL for an optional pointer, Py_None for a `constant` */
    V_PYOBJ,      /* str / bytes / float / bool / Ellipsis: already a PyObject */
    V_INT,        /* OP_INT: an int field, or a `constant` to be boxed */
    V_MOD,
    V_STMT,
    V_EXPR,
    V_ARGUMENTS,
    V_ARG,
    V_KEYWORD,
    V_CTX,        /* an ASDL enum member (Load/Store/Del/operators) */
    V_LIST
} val_kind;

typedef struct jval {
    val_kind kind;
    PyObject *obj;
    long long i;
    void *p;
    struct jval *items;
    Py_ssize_t nitems;
} jval;

/* ------------------------------------------------------------------ *
 * 5. Transcription state.
 * ------------------------------------------------------------------ */

typedef struct {
    const unsigned char *code;
    Py_ssize_t code_len;
    Py_ssize_t pos;

    class_id *classes;   /* bound class table, by container index */
    Py_ssize_t n_classes;
    PyObject **keys;     /* interned key table, by container index */
    Py_ssize_t n_keys;
    PyObject **strings;  /* string pool, borrowed from the caller's list */
    Py_ssize_t n_strings;

    PyArena *arena;

    jval *stack;
    Py_ssize_t depth;
    Py_ssize_t cap;

    int loc_set;
    int l1, c1, l2, c2; /* absolute location register */
    int d1, d2, d3, d4; /* delta accumulators for OP_LOC */

    const char *mod_path;
} tstate;

#define FAIL(fmt, ...)                                                        \
    do {                                                                      \
        PyErr_Format(PyExc_RuntimeError, "m3_modty[%s]: " fmt, ts->mod_path,  \
                     ##__VA_ARGS__);                                          \
        return -1;                                                            \
    } while (0)

#define FAILP(fmt, ...)                                                       \
    do {                                                                      \
        PyErr_Format(PyExc_RuntimeError, "m3_modty[%s]: " fmt, ts->mod_path,  \
                     ##__VA_ARGS__);                                          \
        return NULL;                                                          \
    } while (0)

/* --- arena helpers (inlined from CPython, which does not export them) --- */

/* `_Py_asdl_*_seq_new` are `extern` in pycore_asdl.h and are NOT in
 * libpython's dynamic symbol table, so a dlopened artifact cannot call them.
 * Their bodies are three lines of arena arithmetic; the generated transcriber
 * inlines them, which is why the reachable libpython surface stays at five
 * exported symbols. */
static void *
seq_new(tstate *ts, Py_ssize_t size, size_t elem_size, size_t head_off)
{
    size_t n;
    asdl_seq *seq;
    if (size < 0) {
        FAILP("negative sequence length %zd", size);
    }
    n = head_off + elem_size * (size_t)(size ? size : 1);
    seq = (asdl_seq *)_PyArena_Malloc(ts->arena, n);
    if (seq == NULL) {
        PyErr_NoMemory();
        return NULL;
    }
    memset(seq, 0, n);
    seq->size = size;
    seq->elements = (void **)((char *)seq + head_off);
    return seq;
}

#define SEQ_NEW(ts, T, n) \
    ((T *)seq_new((ts), (n), sizeof(((T *)0)->typed_elements[0]), \
                  offsetof(T, typed_elements)))

/* Every PyObject stored into the tree must outlive the transcription and die
 * with the arena. `_PyArena_AddPyObject` steals the reference. */
static PyObject *
arena_own(tstate *ts, PyObject *o)
{
    if (o == NULL) {
        return NULL;
    }
    Py_INCREF(o);
    if (_PyArena_AddPyObject(ts->arena, o) < 0) {
        Py_DECREF(o);
        return NULL;
    }
    return o;
}

/* --- coercions: a field's ASDL type decides what an operand means --- */

static PyObject *
as_identifier(tstate *ts, const jval *v, const char *what)
{
    if (v->kind == V_ABSENT) {
        return NULL; /* an optional identifier?/string? field is simply unset */
    }
    if (v->kind != V_PYOBJ || !PyUnicode_Check(v->obj)) {
        PyErr_Format(PyExc_RuntimeError, "m3_modty[%s]: %s wants a string operand",
                     ts->mod_path, what);
        return NULL;
    }
    return arena_own(ts, v->obj);
}

/* Required identifier fields refuse an absent operand by name rather than
 * returning a bare NULL, so a producer bug reads as a producer bug. */
static PyObject *
as_identifier_req(tstate *ts, const jval *v, const char *what)
{
    if (v->kind == V_ABSENT) {
        PyErr_Format(PyExc_RuntimeError, "m3_modty[%s]: %s is required but the "
                     "operand was OP_NONE", ts->mod_path, what);
        return NULL;
    }
    return as_identifier(ts, v, what);
}

/* A `constant` field is the one place OP_NONE means the value None rather
 * than an absent field, and the one place an OP_INT operand must be boxed. */
static PyObject *
as_constant(tstate *ts, const jval *v, int *ok)
{
    PyObject *o;
    *ok = 1;
    if (v->kind == V_ABSENT) {
        return arena_own(ts, Py_None);
    }
    if (v->kind == V_INT) {
        o = PyLong_FromLongLong(v->i);
        if (o == NULL) {
            *ok = 0;
            return NULL;
        }
        if (_PyArena_AddPyObject(ts->arena, o) < 0) {
            Py_DECREF(o);
            *ok = 0;
            return NULL;
        }
        return o;
    }
    if (v->kind == V_PYOBJ) {
        return arena_own(ts, v->obj);
    }
    PyErr_Format(PyExc_RuntimeError,
                 "m3_modty[%s]: a node operand reached a `constant` field",
                 ts->mod_path);
    *ok = 0;
    return NULL;
}

static int
as_ctx(tstate *ts, const jval *v, expr_context_ty *out)
{
    if (v->kind != V_CTX) {
        FAIL("an expression context field wants Load/Store/Del");
    }
    *out = (expr_context_ty)v->i;
    return 0;
}

static int
as_expr(tstate *ts, const jval *v, expr_ty *out)
{
    if (v->kind == V_ABSENT) {
        *out = NULL;
        return 0;
    }
    if (v->kind != V_EXPR) {
        FAIL("an expression field got a non-expression operand (kind %d)", (int)v->kind);
    }
    *out = (expr_ty)v->p;
    return 0;
}

#define DEFINE_SEQ_COERCE(fn, SEQT, ELEMT, WANT, WHAT)                        \
    static SEQT *fn(tstate *ts, const jval *v)                                \
    {                                                                         \
        SEQT *seq;                                                            \
        Py_ssize_t k;                                                         \
        if (v->kind != V_LIST) {                                              \
            FAILP("a %s field got a non-list operand", WHAT);                 \
        }                                                                     \
        seq = SEQ_NEW(ts, SEQT, v->nitems);                                   \
        if (seq == NULL) {                                                    \
            return NULL;                                                      \
        }                                                                     \
        for (k = 0; k < v->nitems; k++) {                                     \
            const jval *e = &v->items[k];                                     \
            if (e->kind == V_ABSENT) {                                        \
                asdl_seq_SET(seq, k, NULL);                                   \
                continue;                                                     \
            }                                                                 \
            if (e->kind != WANT) {                                            \
                FAILP("element %zd of a %s field has the wrong kind", k, WHAT);\
            }                                                                 \
            asdl_seq_SET(seq, k, (ELEMT)e->p);                                \
        }                                                                     \
        return seq;                                                           \
    }

DEFINE_SEQ_COERCE(as_stmt_seq, asdl_stmt_seq, stmt_ty, V_STMT, "statement list")
DEFINE_SEQ_COERCE(as_expr_seq, asdl_expr_seq, expr_ty, V_EXPR, "expression list")
DEFINE_SEQ_COERCE(as_arg_seq, asdl_arg_seq, arg_ty, V_ARG, "arg list")
DEFINE_SEQ_COERCE(as_keyword_seq, asdl_keyword_seq, keyword_ty, V_KEYWORD, "keyword list")

static asdl_type_ignore_seq *
as_type_ignore_seq(tstate *ts, const jval *v)
{
    if (v->kind != V_LIST || v->nitems != 0) {
        FAILP("this spike only transcribes an empty type_ignores field");
    }
    return SEQ_NEW(ts, asdl_type_ignore_seq, 0);
}

static asdl_type_param_seq *
as_type_param_seq(tstate *ts, const jval *v)
{
    if (v->kind == V_ABSENT) {
        return SEQ_NEW(ts, asdl_type_param_seq, 0);
    }
    if (v->kind != V_LIST || v->nitems != 0) {
        FAILP("this spike only transcribes an empty type_params field");
    }
    return SEQ_NEW(ts, asdl_type_param_seq, 0);
}

/* --- node construction: the inlined `_PyAST_*` bodies ------------------- */

#define ARENA_NEW(ts, T)                                                      \
    ((T)_PyArena_Malloc((ts)->arena, sizeof(*(T)NULL)))

#define STAMP(node, ts)                 \
    do {                                \
        (node)->lineno = (ts)->l1;      \
        (node)->col_offset = (ts)->c1;  \
        (node)->end_lineno = (ts)->l2;  \
        (node)->end_col_offset = (ts)->c2; \
    } while (0)

/* --- reading the instruction stream ------------------------------------ */

static int
r_byte(tstate *ts, unsigned char *out)
{
    if (ts->pos >= ts->code_len) {
        FAIL("truncated instruction stream at offset %zd", ts->pos);
    }
    *out = ts->code[ts->pos++];
    return 0;
}

static int
r_varint(tstate *ts, unsigned long long *out)
{
    unsigned long long value = 0;
    int shift = 0;
    for (;;) {
        unsigned char b;
        if (r_byte(ts, &b) < 0) {
            return -1;
        }
        value |= (unsigned long long)(b & 0x7F) << shift;
        if (!(b & 0x80)) {
            *out = value;
            return 0;
        }
        shift += 7;
        if (shift > 63) {
            FAIL("varint at offset %zd overflows 64 bits", ts->pos);
        }
    }
}

static int
r_svarint(tstate *ts, long long *out)
{
    unsigned long long u;
    if (r_varint(ts, &u) < 0) {
        return -1;
    }
    *out = (u & 1) ? -(long long)((u + 1) >> 1) : (long long)(u >> 1);
    return 0;
}

static int
push(tstate *ts, jval v)
{
    if (ts->depth == ts->cap) {
        Py_ssize_t ncap = ts->cap ? ts->cap * 2 : 64;
        jval *ns = (jval *)PyMem_Realloc(ts->stack, (size_t)ncap * sizeof(jval));
        if (ns == NULL) {
            PyErr_NoMemory();
            return -1;
        }
        ts->stack = ns;
        ts->cap = ncap;
    }
    ts->stack[ts->depth++] = v;
    return 0;
}

/* --- OP_NODE ----------------------------------------------------------- */

/* Field lookup by name. The producer emits a complete keyword set per node
 * (codegen-ir.md section 4, "keyword construction only"), so positional
 * `_fields` order never enters this path -- which is precisely what makes
 * the same bytes survive a CPython minor that reorders or extends a node. */
static const jval *
field(tstate *ts, PyObject **fkeys, const jval *fvals, Py_ssize_t nf, const char *name)
{
    Py_ssize_t k;
    for (k = 0; k < nf; k++) {
        if (PyUnicode_CompareWithASCIIString(fkeys[k], name) == 0) {
            return &fvals[k];
        }
    }
    return NULL;
}

static const jval absent_val = {V_ABSENT, NULL, 0, NULL, NULL, 0};

#define FIELD(name) \
    (fv = field(ts, fkeys, fvals, nf, (name)), fv ? fv : &absent_val)

static int
build_node(tstate *ts, class_id cid, const char *cname, PyObject **fkeys,
           const jval *fvals, Py_ssize_t nf, jval *out)
{
    const jval *fv;
    memset(out, 0, sizeof(*out));

    switch (cid) {
    case C_Load:
        out->kind = V_CTX;
        out->i = Load;
        return 0;
    case C_Store:
        out->kind = V_CTX;
        out->i = Store;
        return 0;

    case C_Module: {
        mod_ty p = ARENA_NEW(ts, mod_ty);
        asdl_stmt_seq *body;
        asdl_type_ignore_seq *ti;
        if (p == NULL) {
            PyErr_NoMemory();
            return -1;
        }
        body = as_stmt_seq(ts, FIELD("body"));
        if (body == NULL) return -1;
        ti = as_type_ignore_seq(ts, FIELD("type_ignores"));
        if (ti == NULL) return -1;
        p->kind = Module_kind;
        p->v.Module.body = body;
        p->v.Module.type_ignores = ti;
        out->kind = V_MOD;
        out->p = p;
        return 0;
    }

    case C_FunctionDef: {
        stmt_ty p = ARENA_NEW(ts, stmt_ty);
        PyObject *name;
        const jval *args_v;
        asdl_stmt_seq *body;
        asdl_expr_seq *decs;
        expr_ty returns;
        if (p == NULL) {
            PyErr_NoMemory();
            return -1;
        }
        name = as_identifier_req(ts, FIELD("name"), "FunctionDef.name");
        if (name == NULL) return -1;
        args_v = FIELD("args");
        if (args_v->kind != V_ARGUMENTS) {
            FAIL("FunctionDef.args wants an `arguments` node");
        }
        body = as_stmt_seq(ts, FIELD("body"));
        if (body == NULL) return -1;
        decs = as_expr_seq(ts, FIELD("decorator_list"));
        if (decs == NULL) return -1;
        if (as_expr(ts, FIELD("returns"), &returns) < 0) return -1;
        p->kind = FunctionDef_kind;
        p->v.FunctionDef.name = name;
        p->v.FunctionDef.args = (arguments_ty)args_v->p;
        p->v.FunctionDef.body = body;
        p->v.FunctionDef.decorator_list = decs;
        p->v.FunctionDef.returns = returns;
        p->v.FunctionDef.type_comment =
            as_identifier(ts, FIELD("type_comment"), "FunctionDef.type_comment");
        if (PyErr_Occurred()) return -1;
        p->v.FunctionDef.type_params = as_type_param_seq(ts, FIELD("type_params"));
        if (p->v.FunctionDef.type_params == NULL) return -1;
        STAMP(p, ts);
        out->kind = V_STMT;
        out->p = p;
        return 0;
    }

    case C_arguments: {
        /* `arguments` is an ASDL product type with no attributes: it carries
         * no location at all. The shim stamps four fields onto the Python
         * object and CPython discards them; here there is nowhere to put
         * them, which is the same outcome reached honestly. */
        arguments_ty p = ARENA_NEW(ts, arguments_ty);
        if (p == NULL) {
            PyErr_NoMemory();
            return -1;
        }
        p->posonlyargs = as_arg_seq(ts, FIELD("posonlyargs"));
        if (p->posonlyargs == NULL) return -1;
        p->args = as_arg_seq(ts, FIELD("args"));
        if (p->args == NULL) return -1;
        fv = FIELD("vararg");
        p->vararg = (fv->kind == V_ABSENT) ? NULL : (arg_ty)fv->p;
        p->kwonlyargs = as_arg_seq(ts, FIELD("kwonlyargs"));
        if (p->kwonlyargs == NULL) return -1;
        p->kw_defaults = as_expr_seq(ts, FIELD("kw_defaults"));
        if (p->kw_defaults == NULL) return -1;
        fv = FIELD("kwarg");
        p->kwarg = (fv->kind == V_ABSENT) ? NULL : (arg_ty)fv->p;
        p->defaults = as_expr_seq(ts, FIELD("defaults"));
        if (p->defaults == NULL) return -1;
        out->kind = V_ARGUMENTS;
        out->p = p;
        return 0;
    }

    case C_arg: {
        arg_ty p = ARENA_NEW(ts, arg_ty);
        PyObject *nm;
        expr_ty ann;
        if (p == NULL) {
            PyErr_NoMemory();
            return -1;
        }
        nm = as_identifier_req(ts, FIELD("arg"), "arg.arg");
        if (nm == NULL) return -1;
        if (as_expr(ts, FIELD("annotation"), &ann) < 0) return -1;
        p->arg = nm;
        p->annotation = ann;
        p->type_comment = as_identifier(ts, FIELD("type_comment"), "arg.type_comment");
        if (PyErr_Occurred()) return -1;
        STAMP(p, ts);
        out->kind = V_ARG;
        out->p = p;
        return 0;
    }

    case C_Return: {
        stmt_ty p = ARENA_NEW(ts, stmt_ty);
        expr_ty value;
        if (p == NULL) {
            PyErr_NoMemory();
            return -1;
        }
        if (as_expr(ts, FIELD("value"), &value) < 0) return -1;
        p->kind = Return_kind;
        p->v.Return.value = value;
        STAMP(p, ts);
        out->kind = V_STMT;
        out->p = p;
        return 0;
    }

    case C_Expr: {
        stmt_ty p = ARENA_NEW(ts, stmt_ty);
        expr_ty value;
        if (p == NULL) {
            PyErr_NoMemory();
            return -1;
        }
        if (as_expr(ts, FIELD("value"), &value) < 0) return -1;
        if (value == NULL) {
            FAIL("Expr.value is required");
        }
        p->kind = Expr_kind;
        p->v.Expr.value = value;
        STAMP(p, ts);
        out->kind = V_STMT;
        out->p = p;
        return 0;
    }

    case C_Assign: {
        stmt_ty p = ARENA_NEW(ts, stmt_ty);
        asdl_expr_seq *targets;
        expr_ty value;
        if (p == NULL) {
            PyErr_NoMemory();
            return -1;
        }
        targets = as_expr_seq(ts, FIELD("targets"));
        if (targets == NULL) return -1;
        if (as_expr(ts, FIELD("value"), &value) < 0) return -1;
        if (value == NULL) {
            FAIL("Assign.value is required");
        }
        p->kind = Assign_kind;
        p->v.Assign.targets = targets;
        p->v.Assign.value = value;
        p->v.Assign.type_comment =
            as_identifier(ts, FIELD("type_comment"), "Assign.type_comment");
        if (PyErr_Occurred()) return -1;
        STAMP(p, ts);
        out->kind = V_STMT;
        out->p = p;
        return 0;
    }

    case C_Constant: {
        expr_ty p = ARENA_NEW(ts, expr_ty);
        PyObject *value;
        int ok;
        if (p == NULL) {
            PyErr_NoMemory();
            return -1;
        }
        value = as_constant(ts, FIELD("value"), &ok);
        if (!ok || value == NULL) return -1;
        p->kind = Constant_kind;
        p->v.Constant.value = value;
        p->v.Constant.kind = as_identifier(ts, FIELD("kind"), "Constant.kind");
        if (PyErr_Occurred()) return -1;
        STAMP(p, ts);
        out->kind = V_EXPR;
        out->p = p;
        return 0;
    }

    case C_Call: {
        expr_ty p = ARENA_NEW(ts, expr_ty);
        expr_ty func;
        asdl_expr_seq *args;
        asdl_keyword_seq *kws;
        if (p == NULL) {
            PyErr_NoMemory();
            return -1;
        }
        if (as_expr(ts, FIELD("func"), &func) < 0) return -1;
        if (func == NULL) {
            FAIL("Call.func is required");
        }
        args = as_expr_seq(ts, FIELD("args"));
        if (args == NULL) return -1;
        kws = as_keyword_seq(ts, FIELD("keywords"));
        if (kws == NULL) return -1;
        p->kind = Call_kind;
        p->v.Call.func = func;
        p->v.Call.args = args;
        p->v.Call.keywords = kws;
        STAMP(p, ts);
        out->kind = V_EXPR;
        out->p = p;
        return 0;
    }

    case C_keyword: {
        keyword_ty p = ARENA_NEW(ts, keyword_ty);
        expr_ty value;
        if (p == NULL) {
            PyErr_NoMemory();
            return -1;
        }
        if (as_expr(ts, FIELD("value"), &value) < 0) return -1;
        if (value == NULL) {
            FAIL("keyword.value is required");
        }
        p->arg = as_identifier(ts, FIELD("arg"), "keyword.arg");
        if (PyErr_Occurred()) return -1;
        p->value = value;
        STAMP(p, ts);
        out->kind = V_KEYWORD;
        out->p = p;
        return 0;
    }

    case C_Name: {
        expr_ty p = ARENA_NEW(ts, expr_ty);
        PyObject *id;
        expr_context_ty ctx;
        if (p == NULL) {
            PyErr_NoMemory();
            return -1;
        }
        id = as_identifier_req(ts, FIELD("id"), "Name.id");
        if (id == NULL) return -1;
        if (as_ctx(ts, FIELD("ctx"), &ctx) < 0) return -1;
        p->kind = Name_kind;
        p->v.Name.id = id;
        p->v.Name.ctx = ctx;
        STAMP(p, ts);
        out->kind = V_EXPR;
        out->p = p;
        return 0;
    }

    default:
        break;
    }
    /* The refusal the container contract demands: an ast class this build
     * cannot transcribe is named, with the CPython it was built for. It is
     * never skipped and never approximated. */
    PyErr_Format(PyExc_RuntimeError,
                 "m3_modty[%s]: the container names ast class %s, which this "
                 "transcriber (built for CPython %d.%d) does not implement; "
                 "refusing to guess",
                 ts->mod_path, cname, (int)MODTY_BUILT_MAJOR, (int)MODTY_BUILT_MINOR);
    return -1;
}

/* --- the main loop ------------------------------------------------------ */

static int
run_stream(tstate *ts, mod_ty *out_mod)
{
    unsigned char op;
    while (ts->pos < ts->code_len) {
        if (r_byte(ts, &op) < 0) {
            return -1;
        }
        switch (op) {
        case OP_NONE: {
            jval v = absent_val;
            if (push(ts, v) < 0) return -1;
            break;
        }
        case OP_TRUE:
        case OP_FALSE:
        case OP_ELLIPSIS: {
            jval v = absent_val;
            v.kind = V_PYOBJ;
            v.obj = (op == OP_TRUE) ? Py_True : (op == OP_FALSE) ? Py_False : Py_Ellipsis;
            if (push(ts, v) < 0) return -1;
            break;
        }
        case OP_INT: {
            jval v = absent_val;
            long long i;
            if (r_svarint(ts, &i) < 0) return -1;
            v.kind = V_INT;
            v.i = i;
            if (push(ts, v) < 0) return -1;
            break;
        }
        case OP_FLOAT: {
            jval v = absent_val;
            double d;
            PyObject *o;
            if (ts->pos + 8 > ts->code_len) {
                FAIL("truncated OP_FLOAT operand");
            }
            memcpy(&d, ts->code + ts->pos, 8); /* the container is LE-only */
            ts->pos += 8;
            o = PyFloat_FromDouble(d);
            if (o == NULL) return -1;
            if (_PyArena_AddPyObject(ts->arena, o) < 0) {
                Py_DECREF(o);
                return -1;
            }
            v.kind = V_PYOBJ;
            v.obj = o;
            if (push(ts, v) < 0) return -1;
            break;
        }
        case OP_STR: {
            jval v = absent_val;
            unsigned long long r;
            if (r_varint(ts, &r) < 0) return -1;
            if ((Py_ssize_t)r >= ts->n_strings) {
                FAIL("OP_STR ref %llu is outside the string pool", r);
            }
            v.kind = V_PYOBJ;
            v.obj = ts->strings[r];
            if (push(ts, v) < 0) return -1;
            break;
        }
        case OP_BYTES: {
            jval v = absent_val;
            unsigned long long n;
            PyObject *o;
            if (r_varint(ts, &n) < 0) return -1;
            if (ts->pos + (Py_ssize_t)n > ts->code_len) {
                FAIL("truncated OP_BYTES body");
            }
            o = PyBytes_FromStringAndSize((const char *)ts->code + ts->pos, (Py_ssize_t)n);
            ts->pos += (Py_ssize_t)n;
            if (o == NULL) return -1;
            if (_PyArena_AddPyObject(ts->arena, o) < 0) {
                Py_DECREF(o);
                return -1;
            }
            v.kind = V_PYOBJ;
            v.obj = o;
            if (push(ts, v) < 0) return -1;
            break;
        }
        case OP_LIST: {
            unsigned long long n;
            jval v = absent_val;
            jval *items;
            if (r_varint(ts, &n) < 0) return -1;
            if ((Py_ssize_t)n > ts->depth) {
                FAIL("OP_LIST pops %llu values but only %zd are on the stack", n, ts->depth);
            }
            items = (jval *)_PyArena_Malloc(ts->arena, sizeof(jval) * (n ? (size_t)n : 1));
            if (items == NULL) {
                PyErr_NoMemory();
                return -1;
            }
            memcpy(items, ts->stack + ts->depth - (Py_ssize_t)n, sizeof(jval) * (size_t)n);
            ts->depth -= (Py_ssize_t)n;
            v.kind = V_LIST;
            v.items = items;
            v.nitems = (Py_ssize_t)n;
            if (push(ts, v) < 0) return -1;
            break;
        }
        case OP_LOC: {
            long long a, b, c, d;
            if (r_svarint(ts, &a) < 0) return -1;
            if (r_svarint(ts, &b) < 0) return -1;
            if (r_svarint(ts, &c) < 0) return -1;
            if (r_svarint(ts, &d) < 0) return -1;
            ts->l1 += (int)a;
            ts->c1 += (int)b;
            ts->l2 += (int)c;
            ts->c2 += (int)d;
            ts->loc_set = 1;
            break;
        }
        case OP_NODE: {
            unsigned long long cref, nf, kref;
            PyObject *fkeys[32];
            jval fvals[32];
            jval built;
            class_id cid;
            unsigned long long k;
            if (r_varint(ts, &cref) < 0) return -1;
            if ((Py_ssize_t)cref >= ts->n_classes) {
                FAIL("OP_NODE class ref %llu is outside the class table", cref);
            }
            if (r_varint(ts, &nf) < 0) return -1;
            if (nf > 32) {
                FAIL("OP_NODE with %llu fields exceeds this spike's fixed arity", nf);
            }
            for (k = 0; k < nf; k++) {
                if (r_varint(ts, &kref) < 0) return -1;
                if ((Py_ssize_t)kref >= ts->n_keys) {
                    FAIL("OP_NODE key ref %llu is outside the key table", kref);
                }
                fkeys[k] = ts->keys[kref];
            }
            if ((Py_ssize_t)nf > ts->depth) {
                FAIL("OP_NODE pops %llu values but only %zd are on the stack", nf, ts->depth);
            }
            memcpy(fvals, ts->stack + ts->depth - (Py_ssize_t)nf, sizeof(jval) * (size_t)nf);
            ts->depth -= (Py_ssize_t)nf;
            cid = ts->classes[cref];
            /* codegen-ir.md section 5: the location register must be live
             * before the first OP_NODE. Enforced here as it is in the shim,
             * because a zero location is a producer bug, not a default. */
            if (!ts->loc_set) {
                FAIL("OP_NODE before any OP_LOC: the location register is unset");
            }
            if (build_node(ts, cid, "<bound>", fkeys, fvals, (Py_ssize_t)nf, &built) < 0) {
                return -1;
            }
            if (push(ts, built) < 0) return -1;
            break;
        }
        case OP_END: {
            if (ts->depth != 1) {
                FAIL("stream ended with stack depth %zd; exactly one root is required",
                     ts->depth);
            }
            if (ts->stack[0].kind != V_MOD) {
                FAIL("stream root is not an ast.Module");
            }
            if (ts->pos != ts->code_len) {
                FAIL("%zd bytes follow OP_END", ts->code_len - ts->pos);
            }
            *out_mod = (mod_ty)ts->stack[0].p;
            return 0;
        }
        default:
            FAIL("opcode %d at offset %zd is not one this transcriber implements; "
                 "refusing to guess", (int)op, ts->pos - 1);
        }
    }
    FAIL("instruction stream has no OP_END");
}

/* ------------------------------------------------------------------ *
 * 6. The entry point.
 * ------------------------------------------------------------------ */

PyDoc_STRVAR(transcribe_compile_doc,
"transcribe_compile(class_names, key_names, strings, code, filename, "
"merge_caller_future_flags=True) -> code object\n\n"
"Transcribe one JCIR module instruction stream into a CPython mod_ty in a\n"
"PyArena and compile it with _PyAST_Compile. No ast objects are built.");

static PyObject *
modty_transcribe_compile(PyObject *self, PyObject *args, PyObject *kwargs)
{
    static char *kwlist[] = {"class_names", "key_names", "strings", "code",
                             "filename", "merge_caller_future_flags", NULL};
    PyObject *class_names, *key_names, *strings, *filename;
    Py_buffer code;
    int merge = 1;
    tstate tsv;
    tstate *ts = &tsv;
    mod_ty mod = NULL;
    PyCodeObject *co = NULL;
    PyObject *result = NULL;
    PyCompilerFlags cf = _PyCompilerFlags_INIT;
    Py_ssize_t i;

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "O!O!O!y*U|p:transcribe_compile",
                                     kwlist, &PyList_Type, &class_names,
                                     &PyList_Type, &key_names, &PyList_Type,
                                     &strings, &code, &filename, &merge)) {
        return NULL;
    }

    memset(ts, 0, sizeof(*ts));
    ts->mod_path = PyUnicode_AsUTF8(filename);
    if (ts->mod_path == NULL) {
        goto done;
    }
    ts->code = (const unsigned char *)code.buf;
    ts->code_len = code.len;
    ts->n_classes = PyList_GET_SIZE(class_names);
    ts->n_keys = PyList_GET_SIZE(key_names);
    ts->n_strings = PyList_GET_SIZE(strings);

    ts->arena = _PyArena_New();
    if (ts->arena == NULL) {
        goto done;
    }

    /* Bind the class table once per container -- the `cir_bind_classes`
     * analog. An unresolvable name is fatal here, before a single node is
     * built, exactly as the shim's version error is. */
    ts->classes = (class_id *)PyMem_Calloc((size_t)(ts->n_classes ? ts->n_classes : 1),
                                           sizeof(class_id));
    if (ts->classes == NULL) {
        PyErr_NoMemory();
        goto done;
    }
    for (i = 0; i < ts->n_classes; i++) {
        PyObject *nm = PyList_GET_ITEM(class_names, i);
        const char *s = PyUnicode_AsUTF8(nm);
        if (s == NULL) {
            goto done;
        }
        ts->classes[i] = class_id_of(s);
        if (ts->classes[i] == C_UNKNOWN) {
            PyErr_Format(PyExc_RuntimeError,
                         "m3_modty: the container names ast class %s, which this "
                         "transcriber (built for CPython %d.%d) does not "
                         "implement; refusing to guess",
                         s, (int)MODTY_BUILT_MAJOR, (int)MODTY_BUILT_MINOR);
            goto done;
        }
    }

    ts->keys = (PyObject **)PyMem_Calloc((size_t)(ts->n_keys ? ts->n_keys : 1),
                                         sizeof(PyObject *));
    if (ts->keys == NULL) {
        PyErr_NoMemory();
        goto done;
    }
    for (i = 0; i < ts->n_keys; i++) {
        ts->keys[i] = PyList_GET_ITEM(key_names, i); /* borrowed; the list outlives us */
    }
    ts->strings = (PyObject **)PyMem_Calloc((size_t)(ts->n_strings ? ts->n_strings : 1),
                                            sizeof(PyObject *));
    if (ts->strings == NULL) {
        PyErr_NoMemory();
        goto done;
    }
    for (i = 0; i < ts->n_strings; i++) {
        ts->strings[i] = PyList_GET_ITEM(strings, i); /* borrowed */
    }

    if (run_stream(ts, &mod) < 0) {
        goto done;
    }

    /* `compile(tree, path, "exec")` reaches _PyAST_Compile with exactly
     * these flags. `PyEval_MergeCompilerFlags` is what `dont_inherit=False`
     * means: the CALLER's __future__ flags are inherited by the compiled
     * module. It is exposed here as a parameter because whether the fused
     * crossing should keep that behaviour is a real design question, not an
     * implementation detail (see mod-ty-transcription.md section 6). */
    cf.cf_flags = PyCF_SOURCE_IS_UTF8;
    cf.cf_feature_version = PY_MINOR_VERSION;
    if (merge) {
        PyEval_MergeCompilerFlags(&cf);
    }
    co = _PyAST_Compile(mod, filename, &cf, -1, ts->arena);
    if (co == NULL) {
        goto done;
    }
    result = (PyObject *)co;

done:
    PyMem_Free(ts->classes);
    PyMem_Free(ts->keys);
    PyMem_Free(ts->strings);
    PyMem_Free(ts->stack);
    if (ts->arena != NULL) {
        /* The arena owns every node and every PyObject added to it. The code
         * object does not point into the arena: `_PyAST_Compile` copies what
         * it needs into `co_consts` / `co_names`, so the arena dies here and
         * the code object outlives it. That is the whole memory contract. */
        _PyArena_Free(ts->arena);
    }
    PyBuffer_Release(&code);
    return result;
}

static PyObject *
modty_build_info(PyObject *self, PyObject *noargs)
{
    return Py_BuildValue("{s:i,s:i,s:i,s:i}",
                         "built_major", (int)MODTY_BUILT_MAJOR,
                         "built_minor", (int)MODTY_BUILT_MINOR,
                         "running_major", (int)((Py_Version >> 24) & 0xFF),
                         "running_minor", (int)((Py_Version >> 16) & 0xFF));
}

static PyMethodDef modty_methods[] = {
    {"transcribe_compile", (PyCFunction)(void (*)(void))modty_transcribe_compile,
     METH_VARARGS | METH_KEYWORDS, transcribe_compile_doc},
    {"build_info", modty_build_info, METH_NOARGS,
     "Report the CPython minor this TU was compiled against and the one running it."},
    {NULL, NULL, 0, NULL}
};

static int
modty_exec(PyObject *module)
{
    unsigned long running = Py_Version >> 16;
    unsigned long built = (unsigned long)(PY_VERSION_HEX >> 16);
    if (running != built) {
        PyErr_Format(PyExc_ImportError,
                     "m3_modty was built against CPython %d.%d but is loading into "
                     "%d.%d; the AST struct layout and node-kind enumerators differ "
                     "between minors, so this artifact refuses to run rather than "
                     "read the wrong union arm",
                     (int)MODTY_BUILT_MAJOR, (int)MODTY_BUILT_MINOR,
                     (int)((Py_Version >> 24) & 0xFF), (int)((Py_Version >> 16) & 0xFF));
        return -1;
    }
    return 0;
}

static PyModuleDef_Slot modty_slots[] = {
    {Py_mod_exec, modty_exec},
    {0, NULL}
};

static struct PyModuleDef modty_module = {
    PyModuleDef_HEAD_INIT, "m3_modty", "JCIR -> mod_ty transcription spike",
    0, modty_methods, modty_slots, NULL, NULL, NULL
};

PyMODINIT_FUNC
PyInit_m3_modty(void)
{
    return PyModuleDef_Init(&modty_module);
}
