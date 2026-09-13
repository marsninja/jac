/* CPython value/slot operations shared by native Jac standard-library modules.
 * The calling native function holds the GIL. Handles are borrowed on input;
 * object results are new references. Algorithms belong in runtime/python/. */
#include <Python.h>
#include <stdint.h>

#define OBJECT(h) ((PyObject *)(uintptr_t)(h))
#define HANDLE(p) ((uint64_t)(uintptr_t)(p))

int64_t jacpy_object_size(uint64_t handle) {
    return PySequence_Size(OBJECT(handle));
}
uint64_t jacpy_sequence_slot(uint64_t handle) {
    PyTypeObject *type = Py_TYPE(OBJECT(handle));
    if (type->tp_as_sequence && type->tp_as_sequence->sq_item)
        return (uint64_t)(uintptr_t)type->tp_as_sequence->sq_item;
    PyErr_Format(PyExc_TypeError,
        type->tp_as_mapping && type->tp_as_mapping->mp_subscript
            ? "%.200s is not a sequence"
            : "'%.200s' object does not support indexing", type->tp_name);
    return 0;
}
uint64_t jacpy_slot_item(uint64_t slot, uint64_t handle, int64_t index) {
    return HANDLE(((ssizeargfunc)(uintptr_t)slot)(OBJECT(handle), (Py_ssize_t)index));
}
uint64_t jacpy_call_one(uint64_t callable, uint64_t argument) {
    return HANDLE(PyObject_CallOneArg(OBJECT(callable), OBJECT(argument)));
}
int64_t jacpy_less(uint64_t left, uint64_t right) {
    return PyObject_RichCompareBool(OBJECT(left), OBJECT(right), Py_LT);
}
int64_t jacpy_insert(uint64_t handle, int64_t index, uint64_t value) {
    if (PyList_CheckExact(OBJECT(handle)))
        return PyList_Insert(OBJECT(handle), (Py_ssize_t)index, OBJECT(value));
    PyObject *result = PyObject_CallMethod(OBJECT(handle), "insert", "nO",
                                         (Py_ssize_t)index, OBJECT(value));
    if (!result) return -1;
    Py_DECREF(result);
    return 0;
}
int64_t jacpy_recursion_enter(const char *context) {
    return Py_EnterRecursiveCall(context);
}
void jacpy_recursion_leave(void) { Py_LeaveRecursiveCall(); }

uint64_t jacpy_retain(uint64_t handle) { return HANDLE(Py_NewRef(OBJECT(handle))); }
uint64_t jacpy_list_item(uint64_t handle, int64_t index) {
    return HANDLE(PyList_GetItemRef(OBJECT(handle), (Py_ssize_t)index));
}
int64_t jacpy_list_size(uint64_t handle) { return PyList_GET_SIZE(OBJECT(handle)); }
int64_t jacpy_list_append(uint64_t handle, uint64_t value) {
    return PyList_Append(OBJECT(handle), OBJECT(value));
}
int64_t jacpy_list_delete(uint64_t handle, int64_t start, int64_t end) {
    return PyList_SetSlice(OBJECT(handle), (Py_ssize_t)start, (Py_ssize_t)end, NULL);
}
/* Exchange transfers the slot's reference to the caller; it cannot invoke a
 * finalizer while a native caller is still adjusting its container. */
uint64_t jacpy_list_exchange(uint64_t handle, int64_t index, uint64_t value) {
    PyObject *old = PyList_GetItem(OBJECT(handle), (Py_ssize_t)index);
    if (!old) return 0;
    PyList_SET_ITEM(OBJECT(handle), index, Py_NewRef(OBJECT(value)));
    return HANDLE(old);
}
void jacpy_list_swap(uint64_t handle, int64_t left, int64_t right) {
    PyObject *a = PyList_GET_ITEM(OBJECT(handle), left);
    PyObject *b = PyList_GET_ITEM(OBJECT(handle), right);
    PyList_SET_ITEM(OBJECT(handle), left, b);
    PyList_SET_ITEM(OBJECT(handle), right, a);
}

uint64_t jacpy_comparison_slot(uint64_t handle) {
    return (uint64_t)(uintptr_t)Py_TYPE(OBJECT(handle))->tp_richcompare;
}
int64_t jacpy_same_type(uint64_t left, uint64_t right) {
    return Py_TYPE(OBJECT(left)) == Py_TYPE(OBJECT(right));
}
/* -2 preserves NotImplemented so Jac can disable a cached fast comparison. */
int64_t jacpy_slot_less(uint64_t slot, uint64_t left, uint64_t right) {
    PyObject *result = ((richcmpfunc)(uintptr_t)slot)(OBJECT(left), OBJECT(right), Py_LT);
    if (!result) return -1;
    int comparison = result == Py_NotImplemented ? -2 : PyObject_IsTrue(result);
    Py_DECREF(result);
    return comparison;
}
