/* Public argument signatures and exception registration for native codecs. */
#include <Python.h>
#include <stdint.h>

typedef struct { PyObject *error, *incomplete; } CodecState;
extern int jacpy_binary_buffer(PyObject *, void *);
extern int jacpy_ascii_buffer(PyObject *, void *);
extern uint64_t jacpy_buffer_bytes(const Py_buffer *);
extern void jacpy_release(uint64_t);
extern uint64_t jacpy_binascii_convert(uint64_t, uint64_t, int64_t, int64_t, int64_t, int64_t);
#define HANDLE(p) ((uint64_t)(uintptr_t)(p))

static PyObject *convert(PyObject *module, Py_buffer *view, int operation,
                         int64_t option, int64_t second, int64_t third) {
    uint64_t data = jacpy_buffer_bytes(view);
    if (!data) { PyBuffer_Release(view); return NULL; }
    CodecState *state = PyModule_GetState(module);
    uint64_t result = jacpy_binascii_convert(data, HANDLE(state->error), operation, option, second, third);
    jacpy_release(data);
    PyBuffer_Release(view);
    return (PyObject *)(uintptr_t)result;
}
#define ONE(name, operation) \
    static PyObject *name(PyObject *module, PyObject *data) { \
        Py_buffer view; \
        if (!jacpy_ascii_buffer(data, &view)) return NULL; \
        return convert(module, &view, operation, 0, 0, 0); \
    }
ONE(a2b_uu, 0)
ONE(a2b_hex, 4)
ONE(unhexlify, 4)
#define KEYFLAG(name, operation, converter, key, initial) \
    static PyObject *name(PyObject *module, PyObject *args, PyObject *kwargs) { \
        static char *keywords[] = {"", key, NULL}; \
        Py_buffer data; int flag = initial; \
        if (!PyArg_ParseTupleAndKeywords(args, kwargs, "O&|$p:" #name, keywords, converter, &data, &flag)) return NULL; \
        return convert(module, &data, operation, flag, 0, 0); \
    }
KEYFLAG(b2a_uu, 1, jacpy_binary_buffer, "backtick", 0)
KEYFLAG(a2b_base64, 2, jacpy_ascii_buffer, "strict_mode", 0)
KEYFLAG(b2a_base64, 3, jacpy_binary_buffer, "newline", 1)
static PyObject *hex_call(PyObject *module, PyObject *args, PyObject *kwargs, const char *format) {
    static char *keywords[] = {"data", "sep", "bytes_per_sep", NULL};
    Py_buffer data;
    PyObject *separator = NULL;
    int grouping = 1, character = -1;
    if (!PyArg_ParseTupleAndKeywords(args, kwargs, format, keywords, jacpy_binary_buffer, &data, &separator, &grouping)) return NULL;
    if (separator) {
        Py_ssize_t size = PyObject_Length(separator);
        if (size < 0) goto error;
        if (size != 1) { PyErr_SetString(PyExc_ValueError, "sep must be length 1."); goto error; }
        if (PyUnicode_Check(separator)) {
            Py_UCS4 point = PyUnicode_ReadChar(separator, 0);
            if (point > 255) { PyErr_SetString(PyExc_ValueError, "sep must be ASCII."); goto error; }
            character = (int)point;
        } else if (PyBytes_Check(separator)) {
            character = (unsigned char)PyBytes_AS_STRING(separator)[0];
        } else { PyErr_SetString(PyExc_TypeError, "sep must be str or bytes."); goto error; }
    }
    return convert(module, &data, 5, character, grouping, 0);
error:
    PyBuffer_Release(&data);
    return NULL;
}
static PyObject *hexlify(PyObject *module, PyObject *args, PyObject *kwargs) {
    return hex_call(module, args, kwargs, "O&|Oi:hexlify");
}
static PyObject *b2a_hex(PyObject *module, PyObject *args, PyObject *kwargs) {
    return hex_call(module, args, kwargs, "O&|Oi:b2a_hex");
}
static PyObject *a2b_qp(PyObject *module, PyObject *args, PyObject *kwargs) {
    static char *keywords[] = {"data", "header", NULL};
    Py_buffer data; int header = 0;
    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "O&|p:a2b_qp", keywords, jacpy_ascii_buffer, &data, &header)) return NULL;
    return convert(module, &data, 6, header, 0, 0);
}
static PyObject *b2a_qp(PyObject *module, PyObject *args, PyObject *kwargs) {
    static char *keywords[] = {"data", "quotetabs", "istext", "header", NULL};
    Py_buffer data; int quotetabs = 0, istext = 1, header = 0;
    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "O&|ppp:b2a_qp", keywords, jacpy_binary_buffer, &data, &quotetabs, &istext, &header)) return NULL;
    return convert(module, &data, 7, quotetabs, istext, header);
}
static PyObject *crc_hqx(PyObject *module, PyObject *args) {
    Py_buffer data; unsigned int crc;
    if (!PyArg_ParseTuple(args, "O&I:crc_hqx", jacpy_binary_buffer, &data, &crc)) return NULL;
    return convert(module, &data, 8, crc, 0, 0);
}
static PyObject *crc32(PyObject *module, PyObject *args) {
    Py_buffer data; unsigned int crc = 0;
    if (!PyArg_ParseTuple(args, "O&|I:crc32", jacpy_binary_buffer, &data, &crc)) return NULL;
    return convert(module, &data, 9, crc, 0, 0);
}
#define KEYMETHOD(name, signature) \
    {#name, (PyCFunction)(void(*)(void))name, METH_VARARGS | METH_KEYWORDS, #name signature "\n--\n\nConvert binary data."}
static PyMethodDef methods[] = {
    {"a2b_uu", a2b_uu, METH_O, "a2b_uu($module, data, /)\n--\n\nDecode a uuencoded line."},
    {"a2b_hex", a2b_hex, METH_O, "a2b_hex($module, hexstr, /)\n--\n\nDecode hexadecimal data."},
    {"unhexlify", unhexlify, METH_O, "unhexlify($module, hexstr, /)\n--\n\nDecode hexadecimal data."},
    KEYMETHOD(b2a_uu, "($module, data, /, *, backtick=False)"),
    KEYMETHOD(a2b_base64, "($module, data, /, *, strict_mode=False)"),
    KEYMETHOD(b2a_base64, "($module, data, /, *, newline=True)"),
    KEYMETHOD(b2a_hex, "($module, /, data, sep=<unrepresentable>, bytes_per_sep=1)"),
    KEYMETHOD(hexlify, "($module, /, data, sep=<unrepresentable>, bytes_per_sep=1)"),
    KEYMETHOD(a2b_qp, "($module, /, data, header=False)"),
    KEYMETHOD(b2a_qp, "($module, /, data, quotetabs=False, istext=True, header=False)"),
    {"crc_hqx", crc_hqx, METH_VARARGS, "crc_hqx($module, data, crc, /)\n--\n\nCompute CRC-CCITT."},
    {"crc32", crc32, METH_VARARGS, "crc32($module, data, crc=0, /)\n--\n\nCompute CRC-32."},
    {NULL, NULL, 0, NULL}
};
static int module_exec(PyObject *module) {
    CodecState *state = PyModule_GetState(module);
    state->error = PyErr_NewException("binascii.Error", PyExc_ValueError, NULL);
    if (!state->error || PyModule_AddObjectRef(module, "Error", state->error) < 0) return -1;
    state->incomplete = PyErr_NewException("binascii.Incomplete", NULL, NULL);
    if (!state->incomplete || PyModule_AddObjectRef(module, "Incomplete", state->incomplete) < 0) return -1;
    return 0;
}
static int module_traverse(PyObject *module, visitproc visit, void *arg) {
    CodecState *state = PyModule_GetState(module);
    Py_VISIT(state->error); Py_VISIT(state->incomplete);
    return 0;
}
static int module_clear(PyObject *module) {
    CodecState *state = PyModule_GetState(module);
    Py_CLEAR(state->error); Py_CLEAR(state->incomplete);
    return 0;
}
static PyModuleDef_Slot slots[] = {
    {Py_mod_exec, module_exec},
    {Py_mod_multiple_interpreters, Py_MOD_PER_INTERPRETER_GIL_SUPPORTED},
    {0, NULL}
};
static struct PyModuleDef module = {
    PyModuleDef_HEAD_INIT, "binascii", "Binary codecs implemented in native Jac.",
    sizeof(CodecState), methods, slots, module_traverse, module_clear, NULL
};
PyMODINIT_FUNC PyInit_binascii(void) { return PyModuleDef_Init(&module); }
