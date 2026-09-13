/* CPython object/buffer ABI for Jac's native binary layout implementation. */
#include <Python.h>
#include <stdint.h>
#include <stddef.h>
#define H(p) ((uint64_t)(uintptr_t)(p))
#define O(h) ((PyObject *)(uintptr_t)(h))
typedef struct { PyObject *error, *type, *iterator, *cache; } StructModule;
typedef struct { PyObject_HEAD void *plan; PyObject *format, *weakrefs; } Struct;
typedef struct { PyObject_HEAD PyObject *owner; Py_buffer view; void *cursor; } UnpackIterator;
static PyModuleDef definition;
extern void jac_retain(void *), jac_release(void *);
extern void *jacpy_struct_new(void);
extern int64_t jacpy_struct_compile(void *, uint64_t, uint64_t);
extern int64_t jacpy_struct_memory(void *);
extern int64_t jacpy_struct_size(void *), jacpy_struct_items(void *);
extern int64_t jacpy_struct_pack(void *, uint64_t, int64_t, uint64_t, uint64_t);
extern uint64_t jacpy_struct_unpack(void *, uint64_t);
extern int64_t jacpy_struct_offset(void *, int64_t, int64_t, int64_t, uint64_t);
extern void *jacpy_struct_cursor(void *, uint64_t, int64_t);
extern int64_t jacpy_struct_remaining(void *);
extern uint64_t jacpy_struct_next(void *);
extern uint64_t jacpy_struct_cached(uint64_t, uint64_t, uint64_t);
static StructModule *state_for_type(PyTypeObject *type) {
    PyObject *module = PyType_GetModuleByDef(type, &definition);
    return module ? PyModule_GetState(module) : NULL;
}
static int ready(Struct *self) {
    if (self->plan) return 1;
    PyErr_SetString(PyExc_RuntimeError, "Struct object is not initialized"); return 0;
}
static PyObject *struct_new(PyTypeObject *type, PyObject *args, PyObject *kwargs) {
    return type->tp_alloc(type, 0);
}
static int struct_init(PyObject *object, PyObject *args, PyObject *kwargs) {
    static char *keys[] = {"format", NULL};
    PyObject *format;
    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "O:Struct", keys, &format)) return -1;
    if (PyUnicode_Check(format)) format = PyUnicode_AsASCIIString(format);
    else if (PyBytes_Check(format)) Py_INCREF(format);
    else { PyErr_Format(PyExc_TypeError, "Struct() argument 1 must be a str or bytes object, not %.200s", Py_TYPE(format)->tp_name); return -1; }
    if (!format) return -1;
    StructModule *state = state_for_type(Py_TYPE(object));
    if (!state) { Py_DECREF(format); return -1; }
    void *plan = jacpy_struct_new();
    if (!plan) { Py_DECREF(format); PyErr_NoMemory(); return -1; }
    if (jacpy_struct_compile(plan, H(format), H(state->error)) < 0) { jac_release(plan); Py_DECREF(format); return -1; }
    Struct *self = (Struct *)object;
    void *previous = self->plan; self->plan = plan;
    Py_XSETREF(self->format, format);
    if (previous) jac_release(previous);
    return 0;
}
static int struct_traverse(PyObject *object, visitproc visit, void *arg) {
    Py_VISIT(Py_TYPE(object)); Py_VISIT(((Struct *)object)->format); return 0;
}
static int struct_clear(PyObject *object) {
    Struct *self = (Struct *)object;
    void *plan = self->plan; self->plan = NULL;
    PyObject *format = self->format; self->format = NULL;
    if (plan) jac_release(plan);
    Py_XDECREF(format); return 0;
}
static void struct_dealloc(PyObject *object) {
    Struct *self = (Struct *)object; PyTypeObject *type = Py_TYPE(object);
    PyObject_GC_UnTrack(object);
    if (self->weakrefs) PyObject_ClearWeakRefs(object);
    struct_clear(object);
    type->tp_free(object); Py_DECREF(type);
}
static PyObject *struct_format(PyObject *object, void *unused) {
    Struct *self = (Struct *)object;
    if (!ready(self)) return NULL;
    return PyUnicode_FromStringAndSize(PyBytes_AS_STRING(self->format), PyBytes_GET_SIZE(self->format));
}
static PyObject *struct_size(PyObject *object, void *unused) {
    Struct *self = (Struct *)object; return PyLong_FromLongLong(self->plan ? jacpy_struct_size(self->plan) : -1);
}
static PyObject *struct_repr(PyObject *object) {
    PyObject *format = struct_format(object, NULL);
    if (!format) return NULL;
    const char *name = strrchr(Py_TYPE(object)->tp_name, '.');
    PyObject *result = PyUnicode_FromFormat("%s(%R)", name ? name + 1 : Py_TYPE(object)->tp_name, format);
    Py_DECREF(format); return result;
}
static PyObject *struct_sizeof(PyObject *object, PyObject *unused) {
    Struct *self = (Struct *)object;
    if (!ready(self)) return NULL;
    /* Native allocations are measured by the backend that owns their layout. */
    return PyLong_FromSize_t((size_t)Py_TYPE(object)->tp_basicsize + (size_t)jacpy_struct_memory(self->plan));
}
static PyObject *struct_pack(PyObject *object, PyObject *args) {
    Struct *self = (Struct *)object;
    if (!ready(self)) return NULL;
    StructModule *state = state_for_type(Py_TYPE(object)); if (!state) return NULL;
    Py_ssize_t count = PyTuple_GET_SIZE(args), expected = jacpy_struct_items(self->plan);
    if (count != expected) { PyErr_Format(state->error, "pack expected %zd items for packing (got %zd)", expected, count); return NULL; }
    void *plan = self->plan; jac_retain(plan);
    PyObject *result = PyBytes_FromStringAndSize(NULL, jacpy_struct_size(plan));
    if (result && jacpy_struct_pack(plan, H(args), 0, H(PyBytes_AS_STRING(result)), H(state->error)) < 0) Py_CLEAR(result);
    jac_release(plan); return result;
}
static PyObject *struct_pack_into(PyObject *object, PyObject *args) {
    Struct *self = (Struct *)object;
    if (!ready(self)) return NULL;
    StructModule *state = state_for_type(Py_TYPE(object)); if (!state) return NULL;
    Py_ssize_t count = PyTuple_GET_SIZE(args), expected = jacpy_struct_items(self->plan);
    if (count < 2) { PyErr_SetString(state->error, count ? "pack_into expected offset argument" : "pack_into expected buffer argument"); return NULL; }
    if (count - 2 != expected) { PyErr_Format(state->error, "pack_into expected %zd items for packing (got %zd)", expected, count - 2); return NULL; }
    void *plan = self->plan; jac_retain(plan);
    Py_buffer view;
    if (!PyArg_Parse(PyTuple_GET_ITEM(args, 0), "w*", &view)) { jac_release(plan); return NULL; }
    Py_ssize_t offset = PyNumber_AsSsize_t(PyTuple_GET_ITEM(args, 1), PyExc_IndexError);
    PyObject *result = NULL;
    if (!PyErr_Occurred()) {
        offset = jacpy_struct_offset(plan, view.len, offset, 1, H(state->error));
        if (offset >= 0 && jacpy_struct_pack(plan, H(args), 2, H((char *)view.buf + offset), H(state->error)) == 0) result = Py_NewRef(Py_None);
    }
    PyBuffer_Release(&view); jac_release(plan); return result;
}
static PyObject *struct_unpack(PyObject *object, PyObject *args, PyObject *kwargs) {
    Struct *self = (Struct *)object;
    if (!ready(self)) return NULL;
    static char *keys[] = {"", NULL};
    PyObject *buffer;
    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "O:unpack", keys, &buffer)) return NULL;
    StructModule *state = state_for_type(Py_TYPE(object)); if (!state) return NULL;
    void *plan = self->plan; jac_retain(plan);
    Py_buffer view;
    if (PyObject_GetBuffer(buffer, &view, PyBUF_SIMPLE) < 0) { jac_release(plan); return NULL; }
    PyObject *result = NULL;
    if (view.len != jacpy_struct_size(plan)) PyErr_Format(state->error, "unpack requires a buffer of %lld bytes", (long long)jacpy_struct_size(plan));
    else result = O(jacpy_struct_unpack(plan, H(view.buf)));
    PyBuffer_Release(&view); jac_release(plan); return result;
}
static PyObject *struct_unpack_from(PyObject *object, PyObject *args, PyObject *kwargs) {
    Struct *self = (Struct *)object;
    if (!ready(self)) return NULL;
    static char *keys[] = {"buffer", "offset", NULL};
    PyObject *buffer, *offset_object = NULL;
    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "O|O:unpack_from", keys, &buffer, &offset_object)) return NULL;
    StructModule *state = state_for_type(Py_TYPE(object)); if (!state) return NULL;
    void *plan = self->plan; jac_retain(plan);
    Py_buffer view;
    if (PyObject_GetBuffer(buffer, &view, PyBUF_SIMPLE) < 0) { jac_release(plan); return NULL; }
    Py_ssize_t offset = offset_object ? PyNumber_AsSsize_t(offset_object, PyExc_IndexError) : 0;
    PyObject *result = NULL;
    if (!PyErr_Occurred()) {
        offset = jacpy_struct_offset(plan, view.len, offset, 0, H(state->error));
        if (offset >= 0) result = O(jacpy_struct_unpack(plan, H((char *)view.buf + offset)));
    }
    PyBuffer_Release(&view); jac_release(plan); return result;
}
static int iterator_traverse(PyObject *object, visitproc visit, void *arg) {
    UnpackIterator *self = (UnpackIterator *)object;
    Py_VISIT(Py_TYPE(object)); Py_VISIT(self->owner); Py_VISIT(self->view.obj); return 0;
}
static int iterator_clear(PyObject *object) {
    UnpackIterator *self = (UnpackIterator *)object;
    /* Publish exhaustion before buffer or owner finalizers can reenter. */
    void *cursor = self->cursor; self->cursor = NULL;
    PyObject *owner = self->owner; self->owner = NULL;
    Py_buffer view = self->view; self->view.obj = NULL;
    if (cursor) jac_release(cursor);
    if (view.obj) PyBuffer_Release(&view);
    Py_XDECREF(owner); return 0;
}
static void iterator_dealloc(PyObject *object) {
    PyTypeObject *type = Py_TYPE(object);
    PyObject_GC_UnTrack(object); iterator_clear(object); type->tp_free(object); Py_DECREF(type);
}
static PyObject *iterator_next(PyObject *object) {
    UnpackIterator *self = (UnpackIterator *)object;
    if (!self->cursor) return NULL;
    if (!jacpy_struct_remaining(self->cursor)) { iterator_clear(object); return NULL; }
    return O(jacpy_struct_next(self->cursor));
}
static PyObject *iterator_hint(PyObject *object, PyObject *unused) {
    UnpackIterator *self = (UnpackIterator *)object;
    return PyLong_FromLongLong(self->cursor ? jacpy_struct_remaining(self->cursor) : 0);
}
static PyObject *struct_iter_unpack(PyObject *object, PyObject *args, PyObject *kwargs) {
    Struct *self = (Struct *)object;
    if (!ready(self)) return NULL;
    static char *keys[] = {"", NULL}; PyObject *buffer;
    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "O:iter_unpack", keys, &buffer)) return NULL;
    StructModule *state = state_for_type(Py_TYPE(object)); if (!state) return NULL;
    void *plan = self->plan; jac_retain(plan);
    Py_ssize_t size = jacpy_struct_size(plan);
    if (size == 0) { jac_release(plan); PyErr_SetString(state->error, "cannot iteratively unpack with a struct of length 0"); return NULL; }
    UnpackIterator *iterator = (UnpackIterator *)((PyTypeObject *)state->iterator)->tp_alloc((PyTypeObject *)state->iterator, 0);
    if (!iterator) { jac_release(plan); return NULL; }
    if (PyObject_GetBuffer(buffer, &iterator->view, PyBUF_SIMPLE) < 0) { jac_release(plan); Py_DECREF(iterator); return NULL; }
    if (iterator->view.len % size) {
        PyErr_Format(state->error, "iterative unpacking requires a buffer of a multiple of %zd bytes", size);
        jac_release(plan); Py_DECREF(iterator); return NULL;
    }
    iterator->owner = Py_NewRef(object);
    iterator->cursor = jacpy_struct_cursor(plan, H(iterator->view.buf), iterator->view.len);
    jac_release(plan);
    if (!iterator->cursor) { Py_DECREF(iterator); return PyErr_NoMemory(); }
    return (PyObject *)iterator;
}
static PyMethodDef struct_methods[] = {
    {"pack", struct_pack, METH_VARARGS, "Pack values."}, {"pack_into", struct_pack_into, METH_VARARGS, "Pack into a writable buffer."},
    {"unpack", (PyCFunction)struct_unpack, METH_VARARGS | METH_KEYWORDS, "Unpack a buffer."},
    {"unpack_from", (PyCFunction)struct_unpack_from, METH_VARARGS | METH_KEYWORDS, "Unpack from a buffer offset."},
    {"iter_unpack", (PyCFunction)struct_iter_unpack, METH_VARARGS | METH_KEYWORDS, "Iterate over packed records."},
    {"__sizeof__", struct_sizeof, METH_NOARGS, "Memory consumed by this format."}, {NULL}
};
static PyGetSetDef struct_getsets[] = {{"format", struct_format, NULL, "Format string", NULL}, {"size", struct_size, NULL, "Record size", NULL}, {NULL}};
static PyMemberDef struct_members[] = {{"__weaklistoffset__", Py_T_PYSSIZET, offsetof(Struct, weakrefs), Py_READONLY}, {NULL}};
static PyType_Slot struct_slots[] = {
    {Py_tp_new, struct_new}, {Py_tp_init, struct_init}, {Py_tp_dealloc, struct_dealloc}, {Py_tp_traverse, struct_traverse},
    {Py_tp_clear, struct_clear}, {Py_tp_repr, struct_repr}, {Py_tp_methods, struct_methods}, {Py_tp_getset, struct_getsets}, {Py_tp_members, struct_members}, {0, NULL}
};
static PyType_Spec struct_spec = {"_struct.Struct", sizeof(Struct), 0, Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC | Py_TPFLAGS_BASETYPE | Py_TPFLAGS_IMMUTABLETYPE, struct_slots};
static PyMethodDef iterator_methods[] = {{"__length_hint__", iterator_hint, METH_NOARGS, "Remaining records."}, {NULL}};
static PyType_Slot iterator_slots[] = {
    {Py_tp_dealloc, iterator_dealloc}, {Py_tp_traverse, iterator_traverse}, {Py_tp_clear, iterator_clear},
    {Py_tp_iter, PyObject_SelfIter}, {Py_tp_iternext, iterator_next}, {Py_tp_methods, iterator_methods}, {0, NULL}
};
static PyType_Spec iterator_spec = {"_struct.unpack_iterator", sizeof(UnpackIterator), 0, Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC | Py_TPFLAGS_IMMUTABLETYPE | Py_TPFLAGS_DISALLOW_INSTANTIATION, iterator_slots};
static PyObject *module_call(PyObject *module, PyObject *args, PyObject *kwargs, int operation) {
    if (!PyTuple_GET_SIZE(args)) { PyErr_SetString(PyExc_TypeError, "missing format argument"); return NULL; }
    StructModule *state = PyModule_GetState(module);
    PyObject *self = O(jacpy_struct_cached(H(state->cache), H(state->type), H(PyTuple_GET_ITEM(args, 0))));
    if (!self) return NULL;
    PyObject *rest = PyTuple_GetSlice(args, 1, PyTuple_GET_SIZE(args)), *result = NULL;
    if (rest) {
        switch (operation) {
            case 0: result = struct_pack(self, rest); break;
            case 1: result = struct_pack_into(self, rest); break;
            case 2: result = struct_unpack(self, rest, NULL); break;
            case 3: result = struct_unpack_from(self, rest, kwargs); break;
            case 4: result = struct_iter_unpack(self, rest, NULL); break;
        }
        Py_DECREF(rest);
    }
    Py_DECREF(self); return result;
}
#define FORWARD(name, op) static PyObject *module_##name(PyObject *module, PyObject *args) { return module_call(module, args, NULL, op); }
FORWARD(pack, 0) FORWARD(pack_into, 1) FORWARD(unpack, 2) FORWARD(iter_unpack, 4)
static PyObject *module_unpack_from(PyObject *module, PyObject *args, PyObject *kwargs) { return module_call(module, args, kwargs, 3); }
static PyObject *module_calcsize(PyObject *module, PyObject *format) {
    StructModule *state = PyModule_GetState(module);
    PyObject *self = O(jacpy_struct_cached(H(state->cache), H(state->type), H(format)));
    if (!self) return NULL;
    PyObject *result = struct_size(self, NULL); Py_DECREF(self); return result;
}
static PyObject *module_clearcache(PyObject *module, PyObject *unused) { PyDict_Clear(((StructModule *)PyModule_GetState(module))->cache); Py_RETURN_NONE; }
static PyMethodDef module_methods[] = {
    {"calcsize", module_calcsize, METH_O, "Calculate record size."}, {"_clearcache", module_clearcache, METH_NOARGS, "Clear cached formats."},
    {"pack", module_pack, METH_VARARGS, "Pack values."}, {"pack_into", module_pack_into, METH_VARARGS, "Pack into a buffer."},
    {"unpack", module_unpack, METH_VARARGS, "Unpack a buffer."}, {"unpack_from", (PyCFunction)module_unpack_from, METH_VARARGS | METH_KEYWORDS, "Unpack from an offset."},
    {"iter_unpack", module_iter_unpack, METH_VARARGS, "Iterate packed records."}, {NULL}
};
static int module_traverse(PyObject *module, visitproc visit, void *arg) {
    StructModule *state = PyModule_GetState(module);
    Py_VISIT(state->error); Py_VISIT(state->type); Py_VISIT(state->iterator); Py_VISIT(state->cache); return 0;
}
static int module_clear(PyObject *module) {
    StructModule *state = PyModule_GetState(module);
    Py_CLEAR(state->error); Py_CLEAR(state->type); Py_CLEAR(state->iterator); Py_CLEAR(state->cache); return 0;
}
static void module_free(void *module) { module_clear(module); }
static int module_exec(PyObject *module) {
    StructModule *state = PyModule_GetState(module);
    state->error = PyErr_NewException("struct.error", NULL, NULL);
    state->type = PyType_FromModuleAndSpec(module, &struct_spec, NULL);
    state->iterator = PyType_FromModuleAndSpec(module, &iterator_spec, NULL);
    state->cache = PyDict_New();
    if (!state->error || !state->type || !state->iterator || !state->cache) return -1;
    if (PyModule_AddObjectRef(module, "error", state->error) < 0 || PyModule_AddObjectRef(module, "Struct", state->type) < 0) return -1;
    return 0;
}
static PyModuleDef_Slot module_slots[] = {{Py_mod_exec, module_exec}, {Py_mod_multiple_interpreters, Py_MOD_PER_INTERPRETER_GIL_SUPPORTED}, {Py_mod_gil, Py_MOD_GIL_USED}, {0, NULL}};
static PyModuleDef definition = {PyModuleDef_HEAD_INIT, "_struct", "Native Jac binary layouts.", sizeof(StructModule), module_methods, module_slots, module_traverse, module_clear, module_free};
PyMODINIT_FUNC PyInit__struct(void) { return PyModuleDef_Init(&definition); }
