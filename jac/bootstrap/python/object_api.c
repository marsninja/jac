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

#include "internal/pycore_long.h"
#include "internal/pycore_pylifecycle.h"
#include <unistd.h>

int64_t jacpy_is_long(uint64_t handle) { return PyLong_Check(OBJECT(handle)); }
uint64_t jacpy_long_absolute(uint64_t handle) {
    return HANDLE(PyLong_Type.tp_as_number->nb_absolute(OBJECT(handle)));
}
uint64_t jacpy_hash_unsigned(uint64_t handle) {
    Py_hash_t value = PyObject_Hash(OBJECT(handle));
    return value == -1 ? 0 : HANDLE(PyLong_FromSize_t((size_t)value));
}
int64_t jacpy_long_bits(uint64_t handle) { return _PyLong_NumBits(OBJECT(handle)); }
uint64_t jacpy_long_bytes(uint64_t handle, int64_t size) {
    PyObject *result = PyBytes_FromStringAndSize(NULL, size);
    if (!result) return 0;
    if (_PyLong_AsByteArray((PyLongObject *)OBJECT(handle),
            (unsigned char *)PyBytes_AS_STRING(result), size, 1, 0, 1) < 0) {
        Py_DECREF(result);
        return 0;
    }
    return HANDLE(result);
}
uint64_t jacpy_long_from_bytes(uint64_t handle) {
    return HANDLE(_PyLong_FromByteArray((const unsigned char *)PyBytes_AS_STRING(OBJECT(handle)),
                                      PyBytes_GET_SIZE(OBJECT(handle)), 1, 0));
}
uint64_t jacpy_unsigned_long(uint64_t handle) { return PyLong_AsUnsignedLong(OBJECT(handle)); }
uint64_t jacpy_entropy(int64_t size) {
    PyObject *result = PyBytes_FromStringAndSize(NULL, size);
    if (!result) return 0;
    if (_PyOS_URandomNonblock(PyBytes_AS_STRING(result), size) < 0) {
        Py_DECREF(result);
        return 0;
    }
    return HANDLE(result);
}
void jacpy_clear_error(void) { PyErr_Clear(); }
int64_t jacpy_wall_time(void) {
    PyTime_t value;
    return PyTime_Time(&value) < 0 ? -1 : value;
}
int64_t jacpy_monotonic_time(void) {
    PyTime_t value;
    return PyTime_Monotonic(&value) < 0 ? -1 : value;
}
int64_t jacpy_process_id(void) { return getpid(); }
int64_t jacpy_is_tuple(uint64_t handle) { return PyTuple_Check(OBJECT(handle)); }
int64_t jacpy_tuple_size(uint64_t handle) { return PyTuple_Size(OBJECT(handle)); }
uint64_t jacpy_tuple_new(int64_t size) { return HANDLE(PyTuple_New(size)); }
uint64_t jacpy_tuple_item(uint64_t handle, int64_t index) {
    return HANDLE(Py_XNewRef(PyTuple_GetItem(OBJECT(handle), index)));
}
int64_t jacpy_tuple_set_owned(uint64_t handle, int64_t index, uint64_t value) {
    if (!value) return -1;
    return PyTuple_SetItem(OBJECT(handle), index, OBJECT(value));
}

/* Argument converters keep an exported buffer alive through argument parsing
 * and the native call. This preserves resize guards and callback ordering. */
int jacpy_binary_buffer(PyObject *value, void *output) {
    Py_buffer *view = output;
    if (!value) { PyBuffer_Release(view); return 1; }
    if (PyObject_GetBuffer(value, view, PyBUF_SIMPLE) < 0) return 0;
    return Py_CLEANUP_SUPPORTED;
}
int jacpy_ascii_buffer(PyObject *value, void *output) {
    Py_buffer *view = output;
    if (!value) { PyBuffer_Release(view); return 1; }
    if (PyUnicode_Check(value)) {
        if (!PyUnicode_IS_ASCII(value)) {
            PyErr_SetString(PyExc_ValueError, "string argument should contain only ASCII characters");
            return 0;
        }
        if (PyBuffer_FillInfo(view, value, PyUnicode_DATA(value),
                             PyUnicode_GET_LENGTH(value), 1, PyBUF_SIMPLE) < 0) return 0;
    } else if (PyObject_GetBuffer(value, view, PyBUF_SIMPLE) < 0) {
        if (!PyObject_CheckBuffer(value))
            PyErr_Format(PyExc_TypeError, "argument should be bytes, buffer or ASCII string, not '%.100s'", Py_TYPE(value)->tp_name);
        return 0;
    }
    return Py_CLEANUP_SUPPORTED;
}
uint64_t jacpy_buffer_bytes(const Py_buffer *view) {
    if (PyBytes_CheckExact(view->obj) && view->buf == PyBytes_AS_STRING(view->obj)
        && view->len == PyBytes_GET_SIZE(view->obj)) return HANDLE(Py_NewRef(view->obj));
    return HANDLE(PyBytes_FromStringAndSize(view->buf, view->len));
}
void jacpy_set_exception(uint64_t type, const char *message, int64_t size) {
    PyObject *text = PyUnicode_DecodeUTF8(message, size, "surrogatepass");
    if (text) { PyErr_SetObject(OBJECT(type), text); Py_DECREF(text); }
}
