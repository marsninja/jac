/* Public Python signatures, readonly settings, and GC registration for JSON. */
#include <Python.h>
#include <stdint.h>
#include <stddef.h>
#define H(p) ((uint64_t)(uintptr_t)(p))
#define O(h) ((PyObject *)(uintptr_t)(h))
extern uint64_t jacpy_json_escape(uint64_t, int64_t);
extern uint64_t jacpy_json_scanstring(uint64_t, int64_t, int64_t);
extern uint64_t jacpy_json_scan(uint64_t, int64_t, int64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);
static int check_string(PyObject *value) {
    if (PyUnicode_Check(value)) return 1;
    PyErr_Format(PyExc_TypeError, "first argument must be a string, not %.80s", Py_TYPE(value)->tp_name);
    return 0;
}
static PyObject *escape(PyObject *module, PyObject *value) { return check_string(value) ? O(jacpy_json_escape(H(value), 0)) : NULL; }
static PyObject *escape_ascii(PyObject *module, PyObject *value) { return check_string(value) ? O(jacpy_json_escape(H(value), 1)) : NULL; }
static PyObject *scanstring(PyObject *module, PyObject *args) {
    PyObject *source; Py_ssize_t end; int strict = 1;
    if (!PyArg_ParseTuple(args, "On|p:scanstring", &source, &end, &strict) || !check_string(source)) return NULL;
    return O(jacpy_json_scanstring(H(source), end, strict));
}
typedef struct {
    PyObject_HEAD
    signed char strict;
    PyObject *object_hook, *object_pairs_hook, *parse_float, *parse_int, *parse_constant;
} Scanner;
static PyObject *scanner_new(PyTypeObject *type, PyObject *args, PyObject *kwargs) {
    static char *keywords[] = {"context", NULL};
    PyObject *context;
    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "O:make_scanner", keywords, &context)) return NULL;
    Scanner *self = (Scanner *)type->tp_alloc(type, 0);
    if (!self) return NULL;
    PyObject *strict = PyObject_GetAttrString(context, "strict");
    if (!strict) goto error;
    int is_strict = PyObject_IsTrue(strict); Py_DECREF(strict);
    if (is_strict < 0) goto error;
    self->strict = (signed char)is_strict;
#define READ_SETTING(name) self->name = PyObject_GetAttrString(context, #name); if (!self->name) goto error;
    READ_SETTING(object_hook)
    READ_SETTING(object_pairs_hook)
    READ_SETTING(parse_float)
    READ_SETTING(parse_int)
    READ_SETTING(parse_constant)
#undef READ_SETTING
    return (PyObject *)self;
error:
    Py_DECREF(self); return NULL;
}
static int scanner_clear(PyObject *object) {
    Scanner *self = (Scanner *)object;
    Py_CLEAR(self->object_hook); Py_CLEAR(self->object_pairs_hook); Py_CLEAR(self->parse_float); Py_CLEAR(self->parse_int); Py_CLEAR(self->parse_constant);
    return 0;
}
static int scanner_traverse(PyObject *object, visitproc visit, void *arg) {
    Scanner *self = (Scanner *)object;
    Py_VISIT(Py_TYPE(object)); Py_VISIT(self->object_hook); Py_VISIT(self->object_pairs_hook); Py_VISIT(self->parse_float); Py_VISIT(self->parse_int); Py_VISIT(self->parse_constant);
    return 0;
}
static void scanner_dealloc(PyObject *self) {
    PyTypeObject *type = Py_TYPE(self);
    PyObject_GC_UnTrack(self); scanner_clear(self); type->tp_free(self); Py_DECREF(type);
}
static PyObject *scanner_call(PyObject *object, PyObject *args, PyObject *kwargs) {
    static char *keywords[] = {"string", "idx", NULL};
    PyObject *source; Py_ssize_t start;
    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "On:scan_once", keywords, &source, &start) || !check_string(source)) return NULL;
    Scanner *self = (Scanner *)object;
    return O(jacpy_json_scan(H(source), start, self->strict, H(self->object_hook), H(self->object_pairs_hook), H(self->parse_float), H(self->parse_int), H(self->parse_constant)));
}
#define SCANNER_MEMBER(name) {#name, Py_T_OBJECT_EX, offsetof(Scanner, name), Py_READONLY, #name}
static PyMemberDef scanner_members[] = {
    {"strict", Py_T_BOOL, offsetof(Scanner, strict), Py_READONLY, "strict"},
    SCANNER_MEMBER(object_hook), SCANNER_MEMBER(object_pairs_hook), SCANNER_MEMBER(parse_float), SCANNER_MEMBER(parse_int), SCANNER_MEMBER(parse_constant), {NULL}
};
static PyType_Slot scanner_slots[] = {
    {Py_tp_new, scanner_new}, {Py_tp_call, scanner_call}, {Py_tp_clear, scanner_clear}, {Py_tp_traverse, scanner_traverse},
    {Py_tp_dealloc, scanner_dealloc}, {Py_tp_members, scanner_members}, {Py_tp_doc, "JSON scanner object"}, {0, NULL}
};
static PyType_Spec scanner_spec = {"_json.Scanner", sizeof(Scanner), 0, Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC | Py_TPFLAGS_IMMUTABLETYPE, scanner_slots};
extern uint64_t jacpy_json_encode(uint64_t, int64_t, uint64_t, int64_t);
typedef struct {
    PyObject_HEAD
    PyObject *markers, *defaultfn, *encoder, *indent, *key_separator, *item_separator;
    signed char sort_keys, skipkeys, allow_nan;
} Encoder;
static int encoder_clear(PyObject *object) {
    Encoder *self = (Encoder *)object;
    Py_CLEAR(self->markers); Py_CLEAR(self->defaultfn); Py_CLEAR(self->encoder);
    Py_CLEAR(self->indent); Py_CLEAR(self->key_separator); Py_CLEAR(self->item_separator);
    return 0;
}
static int encoder_traverse(PyObject *object, visitproc visit, void *arg) {
    Encoder *self = (Encoder *)object;
    Py_VISIT(Py_TYPE(object)); Py_VISIT(self->markers); Py_VISIT(self->defaultfn); Py_VISIT(self->encoder);
    Py_VISIT(self->indent); Py_VISIT(self->key_separator); Py_VISIT(self->item_separator);
    return 0;
}
static void encoder_dealloc(PyObject *object) {
    PyTypeObject *type = Py_TYPE(object);
    PyObject_GC_UnTrack(object); encoder_clear(object); type->tp_free(object); Py_DECREF(type);
}
static PyObject *encoder_new(PyTypeObject *type, PyObject *args, PyObject *kwargs) {
    static char *keywords[] = {"markers", "default", "encoder", "indent", "key_separator", "item_separator", "sort_keys", "skipkeys", "allow_nan", NULL};
    PyObject *markers, *defaultfn, *encoder, *indent, *key_separator, *item_separator;
    int sort_keys, skipkeys, allow_nan;
    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "OOOOUUppp:make_encoder", keywords,
            &markers, &defaultfn, &encoder, &indent, &key_separator, &item_separator, &sort_keys, &skipkeys, &allow_nan)) return NULL;
    if (markers != Py_None && !PyDict_Check(markers)) {
        PyErr_Format(PyExc_TypeError, "make_encoder() argument 1 must be dict or None, not %.200s", Py_TYPE(markers)->tp_name);
        return NULL;
    }
    Encoder *self = (Encoder *)type->tp_alloc(type, 0);
    if (!self) return NULL;
    self->markers = Py_NewRef(markers); self->defaultfn = Py_NewRef(defaultfn); self->encoder = Py_NewRef(encoder);
    self->indent = Py_NewRef(indent); self->key_separator = Py_NewRef(key_separator); self->item_separator = Py_NewRef(item_separator);
    self->sort_keys = sort_keys; self->skipkeys = skipkeys; self->allow_nan = allow_nan;
    return (PyObject *)self;
}
static PyObject *encoder_call(PyObject *object, PyObject *args, PyObject *kwargs) {
    static char *keywords[] = {"obj", "_current_indent_level", NULL};
    PyObject *value; Py_ssize_t level;
    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "On:_iterencode", keywords, &value, &level)) return NULL;
    Encoder *self = (Encoder *)object;
    PyObject *settings = PyTuple_Pack(6, self->markers, self->defaultfn, self->encoder, self->indent, self->key_separator, self->item_separator);
    if (!settings) return NULL;
    PyObject *result = O(jacpy_json_encode(H(value), level, H(settings), self->sort_keys | (self->skipkeys << 1) | (self->allow_nan << 2)));
    Py_DECREF(settings); return result;
}
#define ENCODER_MEMBER(name, field) {name, Py_T_OBJECT_EX, offsetof(Encoder, field), Py_READONLY, name}
static PyMemberDef encoder_members[] = {
    ENCODER_MEMBER("markers", markers), ENCODER_MEMBER("default", defaultfn), ENCODER_MEMBER("encoder", encoder),
    ENCODER_MEMBER("indent", indent), ENCODER_MEMBER("key_separator", key_separator), ENCODER_MEMBER("item_separator", item_separator),
    {"sort_keys", Py_T_BOOL, offsetof(Encoder, sort_keys), Py_READONLY, "sort_keys"},
    {"skipkeys", Py_T_BOOL, offsetof(Encoder, skipkeys), Py_READONLY, "skipkeys"}, {NULL}
};
static PyType_Slot encoder_slots[] = {
    {Py_tp_new, encoder_new}, {Py_tp_call, encoder_call}, {Py_tp_clear, encoder_clear}, {Py_tp_traverse, encoder_traverse},
    {Py_tp_dealloc, encoder_dealloc}, {Py_tp_members, encoder_members}, {Py_tp_doc, "_iterencode(obj, _current_indent_level) -> iterable"}, {0, NULL}
};
static PyType_Spec encoder_spec = {"_json.Encoder", sizeof(Encoder), 0, Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC | Py_TPFLAGS_IMMUTABLETYPE, encoder_slots};
static PyMethodDef methods[] = {
    {"encode_basestring", escape, METH_O, "Return a JSON string representation."},
    {"encode_basestring_ascii", escape_ascii, METH_O, "Return an ASCII-only JSON string representation."},
    {"scanstring", scanstring, METH_VARARGS, "Scan and unescape a JSON string."},
    {NULL, NULL, 0, NULL}
};
static int module_exec(PyObject *module) {
    PyObject *scanner = PyType_FromModuleAndSpec(module, &scanner_spec, NULL);
    if (!scanner) return -1;
    int status = PyModule_AddObjectRef(module, "make_scanner", scanner);
    Py_DECREF(scanner);
    if (status < 0) return -1;
    PyObject *encoder = PyType_FromModuleAndSpec(module, &encoder_spec, NULL);
    if (!encoder) return -1;
    status = PyModule_AddObjectRef(module, "make_encoder", encoder);
    Py_DECREF(encoder); return status;
}
static PyModuleDef_Slot slots[] = {{Py_mod_exec, module_exec}, {Py_mod_multiple_interpreters, Py_MOD_PER_INTERPRETER_GIL_SUPPORTED}, {0, NULL}};
static PyModuleDef module = {PyModuleDef_HEAD_INIT, "_json", "JSON algorithms implemented in native Jac.", 0, methods, slots};
PyMODINIT_FUNC PyInit__json(void) { return PyModuleDef_Init(&module); }
