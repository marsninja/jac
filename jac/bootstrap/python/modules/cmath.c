/* Public signatures and constants for native Jac complex mathematics. */
#include <Python.h>
#include <stdint.h>
#include <math.h>
#define H(p) ((uint64_t)(uintptr_t)(p))
#define O(h) ((PyObject *)(uintptr_t)(h))
extern uint64_t jacpy_cmath_unary(int64_t, uint64_t);
extern uint64_t jacpy_cmath_log(uint64_t, uint64_t);
extern uint64_t jacpy_cmath_rect(uint64_t, uint64_t);
extern uint64_t jacpy_cmath_isclose(uint64_t, uint64_t, uint64_t, uint64_t);
#define UNARY(name, index) static PyObject *cmath_##name(PyObject *module, PyObject *value) { return O(jacpy_cmath_unary(index, H(value))); }
UNARY(acos, 0) UNARY(acosh, 1) UNARY(asin, 2) UNARY(asinh, 3)
UNARY(atan, 4) UNARY(atanh, 5) UNARY(cos, 6) UNARY(cosh, 7)
UNARY(exp, 8) UNARY(sin, 9) UNARY(sinh, 10) UNARY(sqrt, 11)
UNARY(tan, 12) UNARY(tanh, 13) UNARY(log10, 14) UNARY(phase, 15)
UNARY(polar, 16) UNARY(isfinite, 17) UNARY(isinf, 18) UNARY(isnan, 19)
static PyObject *cmath_log(PyObject *module, PyObject *args) {
    PyObject *value, *base = NULL;
    if (!PyArg_UnpackTuple(args, "log", 1, 2, &value, &base)) return NULL;
    return O(jacpy_cmath_log(H(value), H(base)));
}
static PyObject *cmath_rect(PyObject *module, PyObject *args) {
    PyObject *radius, *angle;
    if (!PyArg_UnpackTuple(args, "rect", 2, 2, &radius, &angle)) return NULL;
    return O(jacpy_cmath_rect(H(radius), H(angle)));
}
static PyObject *cmath_isclose(PyObject *module, PyObject *args, PyObject *kwargs) {
    static char *names[] = {"a", "b", "rel_tol", "abs_tol", NULL};
    PyObject *left, *right, *relative = NULL, *absolute = NULL;
    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "OO|$OO:isclose", names, &left, &right, &relative, &absolute)) return NULL;
    return O(jacpy_cmath_isclose(H(left), H(right), H(relative), H(absolute)));
}
#define METHOD(name) {#name, cmath_##name, METH_O, #name "($module, z, /)\n--\n\nComplex " #name "."}
static PyMethodDef methods[] = {
    METHOD(acos), METHOD(acosh), METHOD(asin), METHOD(asinh), METHOD(atan), METHOD(atanh),
    METHOD(cos), METHOD(cosh), METHOD(exp), METHOD(sin), METHOD(sinh), METHOD(sqrt),
    METHOD(tan), METHOD(tanh), METHOD(log10), METHOD(phase), METHOD(polar), METHOD(isfinite), METHOD(isinf), METHOD(isnan),
    {"log", cmath_log, METH_VARARGS, "log($module, z, base=None, /)\n--\n\nComplex logarithm."},
    {"rect", cmath_rect, METH_VARARGS, "rect($module, r, phi, /)\n--\n\nConvert polar to rectangular coordinates."},
    {"isclose", (PyCFunction)(void(*)(void))cmath_isclose, METH_VARARGS | METH_KEYWORDS, "isclose($module, /, a, b, *, rel_tol=1e-09, abs_tol=0.0)\n--\n\nCompare complex values with tolerances."}, {NULL}
};
static int add_owned(PyObject *module, const char *name, PyObject *value) {
    if (!value) return -1;
    int result = PyModule_AddObjectRef(module, name, value); Py_DECREF(value); return result;
}
static int module_exec(PyObject *module) {
    const char *names[] = {"pi", "e", "tau", "inf", "nan"};
    const double values[] = {3.14159265358979323846, 2.71828182845904523536, 6.28318530717958647693, INFINITY, NAN};
    for (int i = 0; i < 5; i++) if (add_owned(module, names[i], PyFloat_FromDouble(values[i])) < 0) return -1;
    if (add_owned(module, "infj", PyComplex_FromDoubles(0.0, INFINITY)) < 0) return -1;
    if (add_owned(module, "nanj", PyComplex_FromDoubles(0.0, NAN)) < 0) return -1;
    return 0;
}
static PyModuleDef_Slot slots[] = {{Py_mod_exec, module_exec}, {Py_mod_multiple_interpreters, Py_MOD_PER_INTERPRETER_GIL_SUPPORTED}, {Py_mod_gil, Py_MOD_GIL_USED}, {0, NULL}};
static PyModuleDef definition = {PyModuleDef_HEAD_INIT, "cmath", "Native Jac complex mathematics.", 0, methods, slots};
PyMODINIT_FUNC PyInit_cmath(void) { return PyModuleDef_Init(&definition); }
