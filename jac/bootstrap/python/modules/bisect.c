/* Registration and argument ABI only; bisection executes in native Jac. */
#include <Python.h>
#include <stdint.h>

extern uint64_t jacpy_bisect(uint64_t, uint64_t, int64_t, int64_t,
                            uint64_t, int64_t, int64_t);

static PyObject *bisect_call(PyObject *args, PyObject *kwargs,
                            const char *format, int right, int insert) {
    static char *keywords[] = {"a", "x", "lo", "hi", "key", NULL};
    PyObject *sequence, *item, *high = Py_None, *key = Py_None;
    Py_ssize_t low = 0, end = -1;
    if (!PyArg_ParseTupleAndKeywords(args, kwargs, format, keywords,
                                    &sequence, &item, &low, &high, &key)) return NULL;
    if (high != Py_None) {
        PyObject *index = PyNumber_Index(high);
        if (!index) return NULL;
        end = PyLong_AsSsize_t(index);
        Py_DECREF(index);
        if (end == -1 && PyErr_Occurred()) return NULL;
    }
    return (PyObject *)(uintptr_t)jacpy_bisect(
        (uint64_t)(uintptr_t)sequence, (uint64_t)(uintptr_t)item, low, end,
        key == Py_None ? 0 : (uint64_t)(uintptr_t)key, right, insert);
}
#define BISECT(name, right, insert) \
    static PyObject *name(PyObject *self, PyObject *args, PyObject *kwargs) { \
        return bisect_call(args, kwargs, "OO|nO$O:" #name, right, insert); \
    }
BISECT(bisect_left, 0, 0)
BISECT(bisect_right, 1, 0)
BISECT(insort_left, 0, 1)
BISECT(insort_right, 1, 1)
#define METHOD(name, description) \
    {#name, (PyCFunction)(void(*)(void))name, METH_VARARGS | METH_KEYWORDS, \
     #name "($module, /, a, x, lo=0, hi=None, *, key=None)\n--\n\n" description}
static PyMethodDef methods[] = {
    METHOD(bisect_left, "Return the insertion index before existing equal items."),
    METHOD(bisect_right, "Return the insertion index after existing equal items."),
    METHOD(insort_left, "Insert x before existing equal items in sorted sequence a."),
    METHOD(insort_right, "Insert x after existing equal items in sorted sequence a."),
    {NULL, NULL, 0, NULL}
};
static PyModuleDef_Slot slots[] = {
    {Py_mod_multiple_interpreters, Py_MOD_PER_INTERPRETER_GIL_SUPPORTED},
    {0, NULL}
};
static struct PyModuleDef module = {
    PyModuleDef_HEAD_INIT, "_bisect", "Bisection algorithms implemented in native Jac.",
    0, methods, slots, NULL, NULL, NULL
};
PyMODINIT_FUNC PyInit__bisect(void) { return PyModuleDef_Init(&module); }
