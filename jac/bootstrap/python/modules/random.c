/* Retained Python type layout, signatures, and native state lifetime only. */
#include <Python.h>
#include "internal/pycore_modsupport.h"
#include "internal/pycore_long.h"
#include <stdint.h>

typedef struct { PyObject_HEAD void *native; } Random;
extern void *jacpy_random_new(void);
extern void jac_release(void *);
extern int64_t jacpy_random_seed(void *, uint64_t);
extern double jacpy_random_float(void *);
extern uint64_t jacpy_random_getstate(void *);
extern int64_t jacpy_random_setstate(void *, uint64_t);
extern uint64_t jacpy_random_bits(void *, uint64_t);
static struct PyModuleDef module;
#define HANDLE(p) ((uint64_t)(uintptr_t)(p))

static PyObject *random_new(PyTypeObject *type, PyObject *args, PyObject *kwargs) {
    Random *self = (Random *)type->tp_alloc(type, 0);
    if (!self) return NULL;
    self->native = jacpy_random_new();
    if (!self->native) { Py_DECREF(self); return PyErr_NoMemory(); }
    return (PyObject *)self;
}
static void random_dealloc(Random *self) {
    PyTypeObject *type = Py_TYPE(self);
    jac_release(self->native);
    type->tp_free((PyObject *)self);
    Py_DECREF(type);
}
static int random_init(Random *self, PyObject *args, PyObject *kwargs) {
    PyObject *owner = PyType_GetModuleByDef(Py_TYPE(self), &module);
    if (!owner) return -1;
    PyTypeObject *base = *(PyTypeObject **)PyModule_GetState(owner);
    if ((Py_TYPE(self) == base || Py_TYPE(self)->tp_new == base->tp_new)
        && !_PyArg_NoKeywords("Random", kwargs)) return -1;
    PyObject *seed = Py_None;
    if (!PyArg_UnpackTuple(args, "Random", 0, 1, &seed)) return -1;
    return (int)jacpy_random_seed(self->native, seed == Py_None ? 0 : HANDLE(seed));
}
static PyObject *random_seed(Random *self, PyObject *args) {
    PyObject *seed = Py_None;
    if (!PyArg_UnpackTuple(args, "seed", 0, 1, &seed)) return NULL;
    if (jacpy_random_seed(self->native, seed == Py_None ? 0 : HANDLE(seed)) < 0) return NULL;
    Py_RETURN_NONE;
}
static PyObject *random_float(Random *self, PyObject *unused) {
    return PyFloat_FromDouble(jacpy_random_float(self->native));
}
static PyObject *random_getstate(Random *self, PyObject *unused) {
    return (PyObject *)(uintptr_t)jacpy_random_getstate(self->native);
}
static PyObject *random_setstate(Random *self, PyObject *value) {
    if (jacpy_random_setstate(self->native, HANDLE(value)) < 0) return NULL;
    Py_RETURN_NONE;
}
static PyObject *random_bits(Random *self, PyObject *value) {
    uint64_t bits;
    if (!_PyLong_UInt64_Converter(value, &bits)) return NULL;
    return (PyObject *)(uintptr_t)jacpy_random_bits(self->native, bits);
}
static PyMethodDef methods[] = {
    {"seed", (PyCFunction)random_seed, METH_VARARGS, "seed($self, n=None, /)\n--\n\nInitialize generator state."},
    {"random", (PyCFunction)random_float, METH_NOARGS, "random($self, /)\n--\n\nReturn a random float in [0, 1)."},
    {"getstate", (PyCFunction)random_getstate, METH_NOARGS, "getstate($self, /)\n--\n\nReturn generator state."},
    {"setstate", (PyCFunction)random_setstate, METH_O, "setstate($self, state, /)\n--\n\nRestore generator state."},
    {"getrandbits", (PyCFunction)random_bits, METH_O, "getrandbits($self, k, /)\n--\n\nReturn an integer with k random bits."},
    {NULL, NULL, 0, NULL}
};
static PyType_Slot type_slots[] = {
    {Py_tp_new, random_new}, {Py_tp_init, random_init}, {Py_tp_dealloc, random_dealloc},
    {Py_tp_methods, methods}, {0, NULL}
};
static PyType_Spec spec = {
    "_random.Random", sizeof(Random), 0, Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE, type_slots
};
static int module_exec(PyObject *mod) {
    PyObject *type = PyType_FromModuleAndSpec(mod, &spec, NULL);
    if (!type) return -1;
    *(PyObject **)PyModule_GetState(mod) = type;
    return PyModule_AddType(mod, (PyTypeObject *)type);
}
static int module_traverse(PyObject *mod, visitproc visit, void *arg) {
    Py_VISIT(*(PyObject **)PyModule_GetState(mod));
    return 0;
}
static int module_clear(PyObject *mod) {
    Py_CLEAR(*(PyObject **)PyModule_GetState(mod));
    return 0;
}
static PyModuleDef_Slot slots[] = {
    {Py_mod_exec, module_exec},
    {Py_mod_multiple_interpreters, Py_MOD_PER_INTERPRETER_GIL_SUPPORTED},
    {0, NULL}
};
static struct PyModuleDef module = {
    PyModuleDef_HEAD_INIT, "_random", "Mersenne Twister implemented in native Jac.",
    sizeof(PyObject *), NULL, slots, module_traverse, module_clear, NULL
};
PyMODINIT_FUNC PyInit__random(void) { return PyModuleDef_Init(&module); }
