/* Python method signatures and registration; heap algorithms are native Jac. */
#include <Python.h>
#include <stdint.h>
extern uint64_t jacpy_heap_push(uint64_t, uint64_t, int64_t);
extern uint64_t jacpy_heap_pop(uint64_t, int64_t);
extern uint64_t jacpy_heap_replace(uint64_t, uint64_t, int64_t);
extern uint64_t jacpy_heap_pushpop(uint64_t, uint64_t, int64_t);
extern uint64_t jacpy_heapify(uint64_t, int64_t);
#define HANDLE(p) ((uint64_t)(uintptr_t)(p))
#define TWO(name, function, maximum) \
    static PyObject *name(PyObject *self, PyObject *args) { \
        PyObject *heap, *item; \
        if (!PyArg_ParseTuple(args, "O!O:" #name, &PyList_Type, &heap, &item)) return NULL; \
        return (PyObject *)(uintptr_t)function(HANDLE(heap), HANDLE(item), maximum); \
    }
#define ONE(name, function, maximum) \
    static PyObject *name(PyObject *self, PyObject *heap) { \
        if (!PyList_Check(heap)) { \
            PyErr_Format(PyExc_TypeError, #name "() argument must be list, not %.200s", Py_TYPE(heap)->tp_name); \
            return NULL; \
        } \
        return (PyObject *)(uintptr_t)function(HANDLE(heap), maximum); \
    }
TWO(heappush, jacpy_heap_push, 0)
TWO(heappush_max, jacpy_heap_push, 1)
TWO(heapreplace, jacpy_heap_replace, 0)
TWO(heapreplace_max, jacpy_heap_replace, 1)
TWO(heappushpop, jacpy_heap_pushpop, 0)
TWO(heappushpop_max, jacpy_heap_pushpop, 1)
ONE(heappop, jacpy_heap_pop, 0)
ONE(heappop_max, jacpy_heap_pop, 1)
ONE(heapify, jacpy_heapify, 0)
ONE(heapify_max, jacpy_heapify, 1)
#define METHOD2(name) {#name, name, METH_VARARGS, #name "($module, heap, item, /)\n--\n\nMaintain a heap in place."}
#define METHOD1(name) {#name, name, METH_O, #name "($module, heap, /)\n--\n\nMaintain a heap in place."}
static PyMethodDef methods[] = {
    METHOD2(heappush), METHOD2(heappush_max),
    METHOD2(heapreplace), METHOD2(heapreplace_max),
    METHOD2(heappushpop), METHOD2(heappushpop_max),
    METHOD1(heappop), METHOD1(heappop_max), METHOD1(heapify), METHOD1(heapify_max),
    {NULL, NULL, 0, NULL}
};
static PyModuleDef_Slot slots[] = {
    {Py_mod_multiple_interpreters, Py_MOD_PER_INTERPRETER_GIL_SUPPORTED},
    {0, NULL}
};
static struct PyModuleDef module = {
    PyModuleDef_HEAD_INIT, "_heapq", "Heap algorithms implemented in native Jac.",
    0, methods, slots, NULL, NULL, NULL
};
PyMODINIT_FUNC PyInit__heapq(void) { return PyModuleDef_Init(&module); }
