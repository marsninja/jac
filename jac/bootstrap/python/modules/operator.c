/* Python signatures, GC-visible references and type registration only.
 * Accessor algorithms and operator dispatch are implemented in native Jac. */
#include <Python.h>
#include <stdint.h>
#include <stddef.h>
#define H(p) ((uint64_t)(uintptr_t)(p))
#define O(h) ((PyObject *)(uintptr_t)(h))
#define UNARY(name) \
    extern uint64_t jacpy_operator_##name(uint64_t); \
    static PyObject *op_##name(PyObject *module, PyObject *a) { return O(jacpy_operator_##name(H(a))); }
#define BINARY(name) \
    extern uint64_t jacpy_operator_##name(uint64_t, uint64_t); \
    static PyObject *op_##name(PyObject *module, PyObject *args) { \
        PyObject *a, *b; \
        if (!PyArg_UnpackTuple(args, #name, 2, 2, &a, &b)) return NULL; \
        return O(jacpy_operator_##name(H(a), H(b))); \
    }
UNARY(neg)
UNARY(pos)
UNARY(abs)
UNARY(invert)
UNARY(index)
UNARY(truth)
UNARY(not_)
UNARY(is_none)
UNARY(is_not_none)
BINARY(add)
BINARY(sub)
BINARY(mul)
BINARY(matmul)
BINARY(floordiv)
BINARY(truediv)
BINARY(mod)
BINARY(lshift)
BINARY(rshift)
BINARY(and_)
BINARY(xor)
BINARY(or_)
BINARY(iadd)
BINARY(isub)
BINARY(imul)
BINARY(imatmul)
BINARY(ifloordiv)
BINARY(itruediv)
BINARY(imod)
BINARY(ilshift)
BINARY(irshift)
BINARY(iand)
BINARY(ixor)
BINARY(ior)
BINARY(concat)
BINARY(iconcat)
BINARY(getitem)
BINARY(is_)
BINARY(is_not)
BINARY(pow)
BINARY(ipow)
BINARY(contains)
BINARY(indexOf)
BINARY(countOf)
BINARY(delitem)
extern uint64_t jacpy_operator_compare(uint64_t, uint64_t, int64_t);
#define COMPARE(name, code) \
    static PyObject *op_##name(PyObject *module, PyObject *args) { \
        PyObject *a, *b; \
        if (!PyArg_UnpackTuple(args, #name, 2, 2, &a, &b)) return NULL; \
        return O(jacpy_operator_compare(H(a), H(b), code)); \
    }
COMPARE(lt, Py_LT)
COMPARE(le, Py_LE)
COMPARE(eq, Py_EQ)
COMPARE(ne, Py_NE)
COMPARE(gt, Py_GT)
COMPARE(ge, Py_GE)
extern uint64_t jacpy_operator_setitem(uint64_t, uint64_t, uint64_t);
extern uint64_t jacpy_operator_length_hint(uint64_t, int64_t);
extern uint64_t jacpy_operator_call(uint64_t, uint64_t);
extern uint64_t jacpy_operator_digest(uint64_t, uint64_t, int64_t, int64_t);
static PyObject *op_setitem(PyObject *module, PyObject *args) {
    PyObject *a, *b, *value;
    if (!PyArg_UnpackTuple(args, "setitem", 3, 3, &a, &b, &value)) return NULL;
    return O(jacpy_operator_setitem(H(a), H(b), H(value)));
}
static PyObject *op_length_hint(PyObject *module, PyObject *args) {
    PyObject *a; Py_ssize_t fallback = 0;
    if (!PyArg_ParseTuple(args, "O|n:length_hint", &a, &fallback)) return NULL;
    return O(jacpy_operator_length_hint(H(a), fallback));
}
static PyObject *op_call(PyObject *module, PyObject *args, PyObject *kwargs) {
    if (!PyTuple_GET_SIZE(args)) { PyErr_SetString(PyExc_TypeError, "call expected at least 1 argument, got 0"); return NULL; }
    return O(jacpy_operator_call(H(args), H(kwargs)));
}
static PyObject *op_compare_digest(PyObject *module, PyObject *args) {
    PyObject *a, *b;
    if (!PyArg_UnpackTuple(args, "_compare_digest", 2, 2, &a, &b)) return NULL;
    if (PyUnicode_Check(a) && PyUnicode_Check(b)) {
        if (!PyUnicode_IS_ASCII(a) || !PyUnicode_IS_ASCII(b)) {
            PyErr_SetString(PyExc_TypeError, "comparing strings with non-ASCII characters is not supported"); return NULL;
        }
        return O(jacpy_operator_digest(H(PyUnicode_DATA(a)), H(PyUnicode_DATA(b)), PyUnicode_GET_LENGTH(a), PyUnicode_GET_LENGTH(b)));
    }
    if (!PyObject_CheckBuffer(a) && !PyObject_CheckBuffer(b)) {
        PyErr_Format(PyExc_TypeError, "unsupported operand types(s) or combination of types: '%.100s' and '%.100s'", Py_TYPE(a)->tp_name, Py_TYPE(b)->tp_name);
        return NULL;
    }
    Py_buffer av, bv;
    if (PyObject_GetBuffer(a, &av, PyBUF_SIMPLE) < 0) return NULL;
    if (av.ndim > 1) { PyErr_SetString(PyExc_BufferError, "Buffer must be single dimension"); PyBuffer_Release(&av); return NULL; }
    if (PyObject_GetBuffer(b, &bv, PyBUF_SIMPLE) < 0) { PyBuffer_Release(&av); return NULL; }
    PyObject *result = NULL;
    if (bv.ndim > 1) PyErr_SetString(PyExc_BufferError, "Buffer must be single dimension");
    else result = O(jacpy_operator_digest(H(av.buf), H(bv.buf), av.len, bv.len));
    PyBuffer_Release(&av); PyBuffer_Release(&bv);
    return result;
}
#define METHOD1(name) {#name, op_##name, METH_O, #name "($module, a, /)\n--\n\nApply the Python operator."}
#define METHOD2(name) {#name, op_##name, METH_VARARGS, #name "($module, a, b, /)\n--\n\nApply the Python operator."}
static PyMethodDef methods[] = {
    METHOD1(neg),
    METHOD1(pos),
    METHOD1(abs),
    METHOD1(invert),
    METHOD1(index),
    METHOD1(truth),
    METHOD1(not_),
    METHOD1(is_none),
    METHOD1(is_not_none),
    {"inv", op_invert, METH_O, "inv($module, a, /)\n--\n\nSame as ~a."},
    METHOD2(add),
    METHOD2(sub),
    METHOD2(mul),
    METHOD2(matmul),
    METHOD2(floordiv),
    METHOD2(truediv),
    METHOD2(mod),
    METHOD2(lshift),
    METHOD2(rshift),
    METHOD2(and_),
    METHOD2(xor),
    METHOD2(or_),
    METHOD2(iadd),
    METHOD2(isub),
    METHOD2(imul),
    METHOD2(imatmul),
    METHOD2(ifloordiv),
    METHOD2(itruediv),
    METHOD2(imod),
    METHOD2(ilshift),
    METHOD2(irshift),
    METHOD2(iand),
    METHOD2(ixor),
    METHOD2(ior),
    METHOD2(concat),
    METHOD2(iconcat),
    METHOD2(getitem),
    METHOD2(is_),
    METHOD2(is_not),
    METHOD2(pow),
    METHOD2(ipow),
    METHOD2(contains),
    METHOD2(indexOf),
    METHOD2(countOf),
    METHOD2(delitem),
    METHOD2(lt),
    METHOD2(le),
    METHOD2(eq),
    METHOD2(ne),
    METHOD2(gt),
    METHOD2(ge),
    {"setitem", op_setitem, METH_VARARGS, "setitem($module, a, b, c, /)\n--\n\nSame as a[b] = c."},
    {"length_hint", op_length_hint, METH_VARARGS, "length_hint($module, obj, default=0, /)\n--\n\nEstimate an iterable's length."},
    {"call", (PyCFunction)(void(*)(void))op_call, METH_VARARGS|METH_KEYWORDS, "call($module, obj, /, *args, **kwargs)\n--\n\nCall obj."},
    {"_compare_digest", op_compare_digest, METH_VARARGS, "_compare_digest($module, a, b, /)\n--\n\nCompare without content-dependent timing."},
    {NULL, NULL, 0, NULL}
};

typedef struct { PyObject *types[3]; } OperatorState;
typedef struct {
    PyObject_HEAD
    PyObject *arguments;
    PyObject *keywords;
    vectorcallfunc vectorcall;
    int kind;
} Accessor;
extern uint64_t jacpy_operator_prepare(int64_t, uint64_t);
extern uint64_t jacpy_operator_apply(int64_t, uint64_t, uint64_t, uint64_t);
extern uint64_t jacpy_operator_repr(int64_t, uint64_t, uint64_t, uint64_t);
extern uint64_t jacpy_operator_reduce(int64_t, uint64_t, uint64_t, uint64_t);
static PyObject *accessor_vectorcall(PyObject *self, PyObject *const *args, size_t count, PyObject *names) {
    if (names && PyTuple_GET_SIZE(names)) { PyErr_Format(PyExc_TypeError, "%s() takes no keyword arguments", Py_TYPE(self)->tp_name); return NULL; }
    if (PyVectorcall_NARGS(count) != 1) { PyErr_Format(PyExc_TypeError, "%s expected 1 argument, got %zd", Py_TYPE(self)->tp_name, PyVectorcall_NARGS(count)); return NULL; }
    Accessor *accessor = (Accessor *)self;
    return O(jacpy_operator_apply(accessor->kind, H(args[0]), H(accessor->arguments), H(accessor->keywords)));
}
static PyObject *accessor_new(PyTypeObject *type, PyObject *args, PyObject *kwargs) {
    OperatorState *state = PyType_GetModuleState(type);
    int kind = (PyObject *)type == state->types[0] ? 0 : ((PyObject *)type == state->types[1] ? 1 : 2);
    if (kind != 2 && kwargs && PyDict_GET_SIZE(kwargs)) { PyErr_Format(PyExc_TypeError, "%s() takes no keyword arguments", type->tp_name); return NULL; }
    if (PyTuple_GET_SIZE(args) < 1) { PyErr_Format(PyExc_TypeError, "%s expected at least 1 argument, got 0", type->tp_name); return NULL; }
    PyObject *prepared = O(jacpy_operator_prepare(kind, H(args)));
    if (!prepared) return NULL;
    Accessor *self = (Accessor *)type->tp_alloc(type, 0);
    if (!self) { Py_DECREF(prepared); return NULL; }
    self->kind = kind;
    self->arguments = prepared;
    self->keywords = Py_XNewRef(kwargs);
    self->vectorcall = accessor_vectorcall;
    return (PyObject *)self;
}
static int accessor_clear(PyObject *self) {
    Accessor *accessor = (Accessor *)self;
    Py_CLEAR(accessor->arguments); Py_CLEAR(accessor->keywords);
    return 0;
}
static int accessor_traverse(PyObject *self, visitproc visit, void *arg) {
    Accessor *accessor = (Accessor *)self;
    Py_VISIT(Py_TYPE(self)); Py_VISIT(accessor->arguments); Py_VISIT(accessor->keywords);
    return 0;
}
static void accessor_dealloc(PyObject *self) {
    PyTypeObject *type = Py_TYPE(self);
    PyObject_GC_UnTrack(self);
    accessor_clear(self);
    type->tp_free(self);
    Py_DECREF(type);
}
static PyObject *accessor_repr(PyObject *self) {
    Accessor *accessor = (Accessor *)self;
    return O(jacpy_operator_repr(accessor->kind, H(self), H(accessor->arguments), H(accessor->keywords)));
}
static PyObject *accessor_reduce(PyObject *self, PyObject *unused) {
    Accessor *accessor = (Accessor *)self;
    return O(jacpy_operator_reduce(accessor->kind, H(Py_TYPE(self)), H(accessor->arguments), H(accessor->keywords)));
}
static PyObject *accessor_signature(PyObject *self, void *closure) { return PyUnicode_FromString("(obj, /)"); }
static PyGetSetDef accessor_getset[] = {{"__text_signature__", accessor_signature, NULL, NULL, NULL}, {NULL}};
static PyMemberDef accessor_members[] = {{"__vectorcalloffset__", Py_T_PYSSIZET, offsetof(Accessor, vectorcall), Py_READONLY}, {NULL}};
static PyMethodDef accessor_methods[] = {{"__reduce__", accessor_reduce, METH_NOARGS, "Return state for pickling."}, {NULL}};
#define ACCESSOR_SLOTS(name, doc) \
    static PyType_Slot name##_slots[] = { \
        {Py_tp_doc, doc}, {Py_tp_dealloc, accessor_dealloc}, \
        {Py_tp_call, PyVectorcall_Call}, {Py_tp_traverse, accessor_traverse}, \
        {Py_tp_clear, accessor_clear}, {Py_tp_methods, accessor_methods}, \
        {Py_tp_members, accessor_members}, {Py_tp_getset, accessor_getset}, \
        {Py_tp_new, accessor_new}, {Py_tp_repr, accessor_repr}, {0, NULL} \
    }; \
    static PyType_Spec name##_spec = {"operator." #name, sizeof(Accessor), 0, \
        Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC | Py_TPFLAGS_IMMUTABLETYPE | Py_TPFLAGS_HAVE_VECTORCALL, name##_slots};
ACCESSOR_SLOTS(itemgetter, "itemgetter(item, /, *items)\n--\n\nFetch items from an operand.")
ACCESSOR_SLOTS(attrgetter, "attrgetter(attr, /, *attrs)\n--\n\nFetch attributes from an operand.")
ACCESSOR_SLOTS(methodcaller, "methodcaller(name, /, *args, **kwargs)\n--\n\nCall a method on an operand.")
static int operator_exec(PyObject *module) {
    OperatorState *state = PyModule_GetState(module);
    PyType_Spec *specs[] = {&itemgetter_spec, &attrgetter_spec, &methodcaller_spec};
    for (int i = 0; i < 3; i++) {
        state->types[i] = PyType_FromModuleAndSpec(module, specs[i], NULL);
        if (!state->types[i] || PyModule_AddType(module, (PyTypeObject *)state->types[i]) < 0) return -1;
    }
    return 0;
}
static int operator_traverse(PyObject *module, visitproc visit, void *arg) {
    OperatorState *state = PyModule_GetState(module);
    for (int i = 0; i < 3; i++) Py_VISIT(state->types[i]);
    return 0;
}
static int operator_clear(PyObject *module) {
    OperatorState *state = PyModule_GetState(module);
    for (int i = 0; i < 3; i++) Py_CLEAR(state->types[i]);
    return 0;
}
static void operator_free(void *module) { operator_clear(module); }
static PyModuleDef_Slot slots[] = {
    {Py_mod_exec, operator_exec}, {Py_mod_multiple_interpreters, Py_MOD_PER_INTERPRETER_GIL_SUPPORTED}, {0, NULL}
};
static PyModuleDef definition = {
    PyModuleDef_HEAD_INIT, "_operator", "Native Jac implementations of Python operators.",
    sizeof(OperatorState), methods, slots, operator_traverse, operator_clear, operator_free
};
PyMODINIT_FUNC PyInit__operator(void) { return PyModuleDef_Init(&definition); }
