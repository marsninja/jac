/* CPython signatures, object registration and GC visibility for native CSV. */
#include <Python.h>
#include <stdint.h>
#include <stddef.h>
#define H(p) ((uint64_t)(uintptr_t)(p))
#define O(h) ((PyObject *)(uintptr_t)(h))
typedef struct { PyObject *error, *registry, *dialect, *reader, *writer; Py_ssize_t limit; } CsvModule;
typedef struct { PyObject_HEAD PyObject *options; } Dialect;
typedef struct { PyObject_HEAD PyObject *dialect, *input; void *native; } Reader;
typedef struct { PyObject_HEAD PyObject *dialect, *output; void *native; } Writer;
static PyModuleDef definition;
extern void jac_release(void *);
extern uint64_t jacpy_csv_dialect(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);
extern void *jacpy_csv_reader_new(void);
extern void jacpy_csv_reader_clear(void *);
extern uint64_t jacpy_csv_reader_fields(void *);
extern int64_t jacpy_csv_reader_line(void *);
extern uint64_t jacpy_csv_read(void *, uint64_t, uint64_t, uint64_t, uint64_t);
extern void *jacpy_csv_writer_new(void);
extern uint64_t jacpy_csv_write(void *, uint64_t, uint64_t, uint64_t, uint64_t, int64_t);
extern uint64_t jacpy_csv_registry(uint64_t, uint64_t, uint64_t, uint64_t, int64_t);
static CsvModule *module_for_type(PyTypeObject *type) {
    PyObject *module = PyType_GetModuleByDef(type, &definition);
    return module ? PyModule_GetState(module) : NULL;
}
int64_t jacpy_csv_limit(uint64_t state) { return ((CsvModule *)(uintptr_t)state)->limit; }
static PyObject *dialect_new(PyTypeObject *type, PyObject *args, PyObject *kwargs) {
    static char *keys[] = {"dialect", "delimiter", "doublequote", "escapechar", "lineterminator", "quotechar", "quoting", "skipinitialspace", "strict", NULL};
    PyObject *base = NULL, *values[8] = {NULL};
    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "|OOOOOOOOO", keys, &base,
            &values[0], &values[1], &values[2], &values[3], &values[4], &values[5], &values[6], &values[7])) return NULL;
    CsvModule *state = module_for_type(type);
    if (!state) return NULL;
    /* Normalize supplied positional/keyword arguments, retaining the distinction
     * between omission and an explicitly supplied None. */
    PyObject *overrides = PyDict_New();
    if (!overrides) return NULL;
    for (int i = 0; i < 8; ++i) {
        if (values[i] && PyDict_SetItemString(overrides, keys[i + 1], values[i]) < 0) { Py_DECREF(overrides); return NULL; }
    }
    PyObject *settings = O(jacpy_csv_dialect(H(base), H(overrides), H(state->registry), H(state->dialect), H(state->error)));
    Py_DECREF(overrides);
    if (!settings || PyObject_TypeCheck(settings, (PyTypeObject *)state->dialect)) return settings;
    Dialect *self = (Dialect *)type->tp_alloc(type, 0);
    if (!self) { Py_DECREF(settings); return NULL; }
    self->options = settings;
    return (PyObject *)self;
}
static int dialect_clear(PyObject *object) { Py_CLEAR(((Dialect *)object)->options); return 0; }
static int dialect_traverse(PyObject *object, visitproc visit, void *arg) { Py_VISIT(Py_TYPE(object)); Py_VISIT(((Dialect *)object)->options); return 0; }
static void dialect_dealloc(PyObject *object) {
    PyTypeObject *type = Py_TYPE(object);
    PyObject_GC_UnTrack(object); dialect_clear(object); type->tp_free(object); Py_DECREF(type);
}
static PyObject *dialect_option(PyObject *object, void *closure) {
    Py_ssize_t index = (Py_ssize_t)(uintptr_t)closure;
    PyObject *options = ((Dialect *)object)->options;
    if (!options) Py_RETURN_NONE;
    PyObject *value = PyTuple_GET_ITEM(options, index);
    if (index == 0 || index == 2 || index == 4) {
        long character = PyLong_AsLong(value);
        if (character < 0) Py_RETURN_NONE;
        return PyUnicode_FromOrdinal((int)character);
    }
    return Py_NewRef(value);
}
static PyObject *dialect_reduce(PyObject *object, PyObject *args) {
    PyErr_Format(PyExc_TypeError, "cannot pickle '%.100s' instances", Py_TYPE(object)->tp_name);
    return NULL;
}
#define OPTION(name, index) {name, dialect_option, NULL, name, (void *)(uintptr_t)index}
static PyGetSetDef dialect_getsets[] = {
    OPTION("delimiter", 0), OPTION("doublequote", 1), OPTION("escapechar", 2), OPTION("lineterminator", 3),
    OPTION("quotechar", 4), OPTION("quoting", 5), OPTION("skipinitialspace", 6), OPTION("strict", 7), {NULL}
};
static PyMethodDef dialect_methods[] = {
    {"__reduce__", dialect_reduce, METH_VARARGS, "CSV dialects cannot be pickled."},
    {"__reduce_ex__", dialect_reduce, METH_VARARGS, "CSV dialects cannot be pickled."}, {NULL}
};
static PyType_Slot dialect_slots[] = {
    {Py_tp_new, dialect_new}, {Py_tp_getset, dialect_getsets}, {Py_tp_methods, dialect_methods},
    {Py_tp_dealloc, dialect_dealloc}, {Py_tp_clear, dialect_clear}, {Py_tp_traverse, dialect_traverse},
    {Py_tp_doc, "CSV dialect"}, {0, NULL}
};
static PyType_Spec dialect_spec = {"_csv.Dialect", sizeof(Dialect), 0, Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE | Py_TPFLAGS_HAVE_GC | Py_TPFLAGS_IMMUTABLETYPE, dialect_slots};
static PyObject *call_dialect(CsvModule *state, PyObject *base, PyObject *kwargs) {
    return PyObject_VectorcallDict(state->dialect, base ? &base : NULL, base ? 1 : 0, kwargs);
}
static int reader_clear(PyObject *object) {
    Reader *self = (Reader *)object;
    Py_CLEAR(self->dialect); Py_CLEAR(self->input);
    if (self->native) jacpy_csv_reader_clear(self->native);
    return 0;
}
static int reader_traverse(PyObject *object, visitproc visit, void *arg) {
    Reader *self = (Reader *)object;
    Py_VISIT(Py_TYPE(object)); Py_VISIT(self->dialect); Py_VISIT(self->input);
    if (self->native) Py_VISIT(O(jacpy_csv_reader_fields(self->native)));
    return 0;
}
static void reader_dealloc(PyObject *object) {
    Reader *self = (Reader *)object; PyTypeObject *type = Py_TYPE(object);
    PyObject_GC_UnTrack(object); reader_clear(object);
    if (self->native) jac_release(self->native);
    type->tp_free(object); Py_DECREF(type);
}
static PyObject *reader_next(PyObject *object) {
    Reader *self = (Reader *)object;
    CsvModule *state = module_for_type(Py_TYPE(object));
    if (!state) return NULL;
    return O(jacpy_csv_read(self->native, H(self->input), H(((Dialect *)self->dialect)->options), H(state), H(state->error)));
}
static PyObject *reader_line(PyObject *object, void *unused) { return PyLong_FromLongLong(jacpy_csv_reader_line(((Reader *)object)->native)); }
static PyGetSetDef reader_getsets[] = {{"line_num", reader_line, NULL, "Source line number", NULL}, {NULL}};
static PyMemberDef reader_members[] = {{"dialect", Py_T_OBJECT_EX, offsetof(Reader, dialect), Py_READONLY, "Parsing dialect"}, {NULL}};
static PyType_Slot reader_slots[] = {
    {Py_tp_iter, PyObject_SelfIter}, {Py_tp_iternext, reader_next}, {Py_tp_dealloc, reader_dealloc},
    {Py_tp_clear, reader_clear}, {Py_tp_traverse, reader_traverse}, {Py_tp_getset, reader_getsets},
    {Py_tp_members, reader_members}, {Py_tp_doc, "CSV reader"}, {0, NULL}
};
static PyType_Spec reader_spec = {"_csv.reader", sizeof(Reader), 0, Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE | Py_TPFLAGS_HAVE_GC | Py_TPFLAGS_IMMUTABLETYPE | Py_TPFLAGS_DISALLOW_INSTANTIATION, reader_slots};
static PyObject *reader_new(PyObject *module, PyObject *args, PyObject *kwargs) {
    PyObject *input, *base = NULL;
    if (!PyArg_UnpackTuple(args, "reader", 1, 2, &input, &base)) return NULL;
    CsvModule *state = PyModule_GetState(module);
    Reader *self = (Reader *)((PyTypeObject *)state->reader)->tp_alloc((PyTypeObject *)state->reader, 0);
    if (!self) return NULL;
    self->native = jacpy_csv_reader_new();
    if (!self->native) { Py_DECREF(self); return PyErr_NoMemory(); }
    self->input = PyObject_GetIter(input);
    if (!self->input) { Py_DECREF(self); return NULL; }
    self->dialect = call_dialect(state, base, kwargs);
    if (!self->dialect) { Py_DECREF(self); return NULL; }
    return (PyObject *)self;
}
static int writer_clear(PyObject *object) { Writer *self = (Writer *)object; Py_CLEAR(self->dialect); Py_CLEAR(self->output); return 0; }
static int writer_traverse(PyObject *object, visitproc visit, void *arg) {
    Writer *self = (Writer *)object;
    Py_VISIT(Py_TYPE(object)); Py_VISIT(self->dialect); Py_VISIT(self->output); return 0;
}
static void writer_dealloc(PyObject *object) {
    Writer *self = (Writer *)object; PyTypeObject *type = Py_TYPE(object);
    PyObject_GC_UnTrack(object); writer_clear(object);
    if (self->native) jac_release(self->native);
    type->tp_free(object); Py_DECREF(type);
}
static PyObject *writer_write(PyObject *object, PyObject *row, int many) {
    Writer *self = (Writer *)object;
    CsvModule *state = module_for_type(Py_TYPE(object));
    return state ? O(jacpy_csv_write(self->native, H(row), H(self->output), H(((Dialect *)self->dialect)->options), H(state->error), many)) : NULL;
}
static PyObject *writer_row(PyObject *object, PyObject *row) { return writer_write(object, row, 0); }
static PyObject *writer_rows(PyObject *object, PyObject *rows) { return writer_write(object, rows, 1); }
static PyMethodDef writer_methods[] = {
    {"writerow", writer_row, METH_O, "writerow($self, row, /)\n--\n\nWrite a CSV record."},
    {"writerows", writer_rows, METH_O, "writerows($self, rows, /)\n--\n\nWrite CSV records."}, {NULL}
};
static PyMemberDef writer_members[] = {{"dialect", Py_T_OBJECT_EX, offsetof(Writer, dialect), Py_READONLY, "Writing dialect"}, {NULL}};
static PyType_Slot writer_slots[] = {
    {Py_tp_dealloc, writer_dealloc}, {Py_tp_clear, writer_clear}, {Py_tp_traverse, writer_traverse},
    {Py_tp_methods, writer_methods}, {Py_tp_members, writer_members}, {Py_tp_doc, "CSV writer"}, {0, NULL}
};
static PyType_Spec writer_spec = {"_csv.writer", sizeof(Writer), 0, Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE | Py_TPFLAGS_HAVE_GC | Py_TPFLAGS_IMMUTABLETYPE | Py_TPFLAGS_DISALLOW_INSTANTIATION, writer_slots};
static PyObject *writer_new(PyObject *module, PyObject *args, PyObject *kwargs) {
    PyObject *output, *base = NULL;
    if (!PyArg_UnpackTuple(args, "writer", 1, 2, &output, &base)) return NULL;
    CsvModule *state = PyModule_GetState(module);
    Writer *self = (Writer *)((PyTypeObject *)state->writer)->tp_alloc((PyTypeObject *)state->writer, 0);
    if (!self) return NULL;
    self->native = jacpy_csv_writer_new();
    if (!self->native) { Py_DECREF(self); return PyErr_NoMemory(); }
    if (PyObject_GetOptionalAttrString(output, "write", &self->output) < 0) { Py_DECREF(self); return NULL; }
    if (!self->output || !PyCallable_Check(self->output)) {
        Py_DECREF(self); PyErr_SetString(PyExc_TypeError, "argument 1 must have a \"write\" method"); return NULL;
    }
    self->dialect = call_dialect(state, base, kwargs);
    if (!self->dialect) { Py_DECREF(self); return NULL; }
    return (PyObject *)self;
}
static PyObject *registry(PyObject *module, PyObject *key, PyObject *value, int operation) {
    CsvModule *state = PyModule_GetState(module);
    return O(jacpy_csv_registry(H(state->registry), H(key), H(value), H(state->error), operation));
}
static PyObject *list_dialects(PyObject *module, PyObject *unused) { return registry(module, NULL, NULL, 0); }
static PyObject *get_dialect(PyObject *module, PyObject *args, PyObject *kwargs) {
    static char *keys[] = {"name", NULL}; PyObject *name;
    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "O:get_dialect", keys, &name)) return NULL;
    return registry(module, name, NULL, 1);
}
static PyObject *unregister_dialect(PyObject *module, PyObject *args, PyObject *kwargs) {
    static char *keys[] = {"name", NULL}; PyObject *name;
    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "O:unregister_dialect", keys, &name)) return NULL;
    return registry(module, name, NULL, 3);
}
static PyObject *register_dialect(PyObject *module, PyObject *args, PyObject *kwargs) {
    PyObject *name, *base = NULL;
    if (!PyArg_UnpackTuple(args, "register_dialect", 1, 2, &name, &base)) return NULL;
    if (!PyUnicode_Check(name)) { PyErr_SetString(PyExc_TypeError, "dialect name must be a string"); return NULL; }
    CsvModule *state = PyModule_GetState(module);
    PyObject *dialect = call_dialect(state, base, kwargs);
    if (!dialect) return NULL;
    PyObject *result = registry(module, name, dialect, 2);
    Py_DECREF(dialect); return result;
}
static PyObject *field_size_limit(PyObject *module, PyObject *args, PyObject *kwargs) {
    static char *keys[] = {"new_limit", NULL}; PyObject *limit = NULL;
    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "|O:field_size_limit", keys, &limit)) return NULL;
    CsvModule *state = PyModule_GetState(module);
    Py_ssize_t old = state->limit;
    if (limit) {
        if (!PyLong_CheckExact(limit)) { PyErr_SetString(PyExc_TypeError, "limit must be an integer"); return NULL; }
        Py_ssize_t value = PyLong_AsSsize_t(limit);
        if (value == -1 && PyErr_Occurred()) return NULL;
        state->limit = value;
    }
    return PyLong_FromSsize_t(old);
}
#define KW(name, fn, signature) {name, (PyCFunction)(void(*)(void))fn, METH_VARARGS | METH_KEYWORDS, name signature "\n--\n\nCSV operation."}
static PyMethodDef methods[] = {
    KW("reader", reader_new, "($module, iterable, /, dialect='excel', **fmtparams)"),
    KW("writer", writer_new, "($module, fileobj, /, dialect='excel', **fmtparams)"),
    KW("register_dialect", register_dialect, "($module, name, /, dialect='excel', **fmtparams)"),
    KW("get_dialect", get_dialect, "($module, /, name)"),
    KW("unregister_dialect", unregister_dialect, "($module, /, name)"),
    KW("field_size_limit", field_size_limit, "($module, /, new_limit=<unrepresentable>)"),
    {"list_dialects", list_dialects, METH_NOARGS, "list_dialects($module, /)\n--\n\nList registered dialects."}, {NULL}
};
static int module_clear(PyObject *module) {
    CsvModule *state = PyModule_GetState(module);
    Py_CLEAR(state->error); Py_CLEAR(state->registry); Py_CLEAR(state->dialect); Py_CLEAR(state->reader); Py_CLEAR(state->writer); return 0;
}
static int module_traverse(PyObject *module, visitproc visit, void *arg) {
    CsvModule *state = PyModule_GetState(module);
    Py_VISIT(state->error); Py_VISIT(state->registry); Py_VISIT(state->dialect); Py_VISIT(state->reader); Py_VISIT(state->writer); return 0;
}
static void module_free(void *module) { module_clear(module); }
static int module_exec(PyObject *module) {
    CsvModule *state = PyModule_GetState(module);
    state->limit = 128 * 1024;
    state->registry = PyDict_New(); state->error = PyErr_NewException("_csv.Error", NULL, NULL);
    state->dialect = PyType_FromModuleAndSpec(module, &dialect_spec, NULL);
    state->reader = PyType_FromModuleAndSpec(module, &reader_spec, NULL);
    state->writer = PyType_FromModuleAndSpec(module, &writer_spec, NULL);
    if (!state->registry || !state->error || !state->dialect || !state->reader || !state->writer) return -1;
    if (PyModule_AddObjectRef(module, "_dialects", state->registry) < 0 || PyModule_AddObjectRef(module, "Error", state->error) < 0 ||
        PyModule_AddObjectRef(module, "Dialect", state->dialect) < 0 || PyModule_AddObjectRef(module, "Reader", state->reader) < 0 ||
        PyModule_AddObjectRef(module, "Writer", state->writer) < 0) return -1;
    const char *styles[] = {"QUOTE_MINIMAL", "QUOTE_ALL", "QUOTE_NONNUMERIC", "QUOTE_NONE", "QUOTE_STRINGS", "QUOTE_NOTNULL"};
    for (int i = 0; i < 6; ++i) if (PyModule_AddIntConstant(module, styles[i], i) < 0) return -1;
    return 0;
}
static PyModuleDef_Slot slots[] = {{Py_mod_exec, module_exec}, {Py_mod_multiple_interpreters, Py_MOD_PER_INTERPRETER_GIL_SUPPORTED}, {0, NULL}};
static PyModuleDef definition = {PyModuleDef_HEAD_INIT, "_csv", "CSV algorithms implemented in native Jac.", sizeof(CsvModule), methods, slots, module_traverse, module_clear, module_free};
PyMODINIT_FUNC PyInit__csv(void) { return PyModuleDef_Init(&definition); }
