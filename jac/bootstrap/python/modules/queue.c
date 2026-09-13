/* Object layout, signatures, GC visitation and lifetime for native SimpleQueue. */
#include <Python.h>
#include "internal/pycore_modsupport.h"
#include <stddef.h>
#include <stdint.h>
#define H(p) ((uint64_t)(uintptr_t)(p))
#define O(h) ((PyObject *)(uintptr_t)(h))
typedef struct { PyObject *type, *empty; } QueueModule;
typedef struct {
    PyObject_HEAD
    void *native;
    uint64_t lock;
    PyObject *weakrefs;
} Queue;
static PyModuleDef definition;
extern uint64_t jacpy_lock_new(void);
extern void jacpy_lock_free(uint64_t);
extern void *jacpy_queue_new(uint64_t);
extern void jac_release(void *);
extern void jacpy_queue_clear(void *);
extern void jacpy_queue_put(void *, uint64_t);
extern uint64_t jacpy_queue_get(void *, uint64_t, int64_t, uint64_t);
extern int64_t jacpy_queue_size(void *);
extern uint64_t jacpy_queue_borrow(void *, int64_t);
static QueueModule *queue_module(PyTypeObject *type) {
    PyObject *module = PyType_GetModuleByDef(type, &definition);
    return module ? PyModule_GetState(module) : NULL;
}
static PyObject *queue_new(PyTypeObject *type, PyObject *args, PyObject *kwargs) {
    QueueModule *state = queue_module(type);
    if (!state) return NULL;
    PyTypeObject *base = (PyTypeObject *)state->type;
    if (type == base || type->tp_init == base->tp_init) {
        if (!_PyArg_NoPositional("SimpleQueue", args) || !_PyArg_NoKeywords("SimpleQueue", kwargs)) return NULL;
    }
    Queue *self = (Queue *)type->tp_alloc(type, 0);
    if (!self) return NULL;
    self->lock = jacpy_lock_new();
    if (!self->lock) { Py_DECREF(self); return NULL; }
    self->native = jacpy_queue_new(self->lock);
    if (!self->native) { Py_DECREF(self); return PyErr_NoMemory(); }
    return (PyObject *)self;
}
static int queue_clear(PyObject *self) {
    Queue *queue = (Queue *)self;
    if (queue->native) jacpy_queue_clear(queue->native);
    return 0;
}
static void queue_dealloc(PyObject *self) {
    Queue *queue = (Queue *)self;
    PyTypeObject *type = Py_TYPE(self);
    PyObject_GC_UnTrack(self);
    queue_clear(self);
    if (queue->weakrefs) PyObject_ClearWeakRefs(self);
    if (queue->native) jac_release(queue->native);
    if (queue->lock) jacpy_lock_free(queue->lock);
    type->tp_free(self);
    Py_DECREF(type);
}
static int queue_traverse(PyObject *self, visitproc visit, void *arg) {
    Queue *queue = (Queue *)self;
    Py_VISIT(Py_TYPE(self));
    if (queue->native) {
        int64_t count = jacpy_queue_size(queue->native);
        for (int64_t i = 0; i < count; i++) Py_VISIT(O(jacpy_queue_borrow(queue->native, i)));
    }
    return 0;
}
static PyObject *queue_put(PyObject *self, PyObject *args, PyObject *kwargs) {
    static char *keywords[] = {"item", "block", "timeout", NULL};
    PyObject *item, *timeout = Py_None; int block = 1;
    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "O|pO:put", keywords, &item, &block, &timeout)) return NULL;
    jacpy_queue_put(((Queue *)self)->native, H(item));
    Py_RETURN_NONE;
}
static PyObject *queue_put_nowait(PyObject *self, PyObject *args, PyObject *kwargs) {
    static char *keywords[] = {"item", NULL};
    PyObject *item;
    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "O:put_nowait", keywords, &item)) return NULL;
    jacpy_queue_put(((Queue *)self)->native, H(item));
    Py_RETURN_NONE;
}
static PyObject *queue_get(PyObject *self, PyObject *args, PyObject *kwargs) {
    static char *keywords[] = {"block", "timeout", NULL};
    PyObject *timeout = Py_None; int block = 1;
    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "|pO:get", keywords, &block, &timeout)) return NULL;
    QueueModule *state = queue_module(Py_TYPE(self));
    return state ? O(jacpy_queue_get(((Queue *)self)->native, H(state->empty), block, H(timeout))) : NULL;
}
static PyObject *queue_get_nowait(PyObject *self, PyObject *unused) {
    QueueModule *state = queue_module(Py_TYPE(self));
    return state ? O(jacpy_queue_get(((Queue *)self)->native, H(state->empty), 0, H(Py_None))) : NULL;
}
static PyObject *queue_empty(PyObject *self, PyObject *unused) { return PyBool_FromLong(jacpy_queue_size(((Queue *)self)->native) == 0); }
static PyObject *queue_size(PyObject *self, PyObject *unused) { return PyLong_FromLongLong(jacpy_queue_size(((Queue *)self)->native)); }
#define KEYMETHOD(name, signature) {#name, (PyCFunction)(void(*)(void))queue_##name, METH_VARARGS | METH_KEYWORDS, #name signature "\n--\n\nOperate on the FIFO queue."}
static PyMethodDef methods[] = {
    KEYMETHOD(put, "($self, /, item, block=True, timeout=None)"),
    KEYMETHOD(put_nowait, "($self, /, item)"),
    KEYMETHOD(get, "($self, /, block=True, timeout=None)"),
    {"get_nowait", queue_get_nowait, METH_NOARGS, "get_nowait($self, /)\n--\n\nGet an item or raise Empty."},
    {"empty", queue_empty, METH_NOARGS, "empty($self, /)\n--\n\nReturn whether the queue is empty."},
    {"qsize", queue_size, METH_NOARGS, "qsize($self, /)\n--\n\nReturn the approximate queue size."},
    {"__class_getitem__", Py_GenericAlias, METH_O | METH_CLASS, "Create a generic queue alias."},
    {NULL, NULL, 0, NULL}
};
static PyMemberDef members[] = {{"__weaklistoffset__", Py_T_PYSSIZET, offsetof(Queue, weakrefs), Py_READONLY}, {NULL}};
static PyType_Slot type_slots[] = {
    {Py_tp_new, queue_new}, {Py_tp_dealloc, queue_dealloc}, {Py_tp_traverse, queue_traverse}, {Py_tp_clear, queue_clear},
    {Py_tp_members, members}, {Py_tp_methods, methods},
    {Py_tp_doc, "SimpleQueue()\n--\n\nSimple, unbounded, reentrant FIFO queue."}, {0, NULL}
};
static PyType_Spec type_spec = {"_queue.SimpleQueue", sizeof(Queue), 0, Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE | Py_TPFLAGS_HAVE_GC | Py_TPFLAGS_IMMUTABLETYPE, type_slots};
static int module_exec(PyObject *module) {
    QueueModule *state = PyModule_GetState(module);
    state->empty = PyErr_NewExceptionWithDoc("_queue.Empty", "Exception raised by Queue.get(block=0)/get_nowait().", NULL, NULL);
    if (!state->empty || PyModule_AddObjectRef(module, "Empty", state->empty) < 0) return -1;
    state->type = PyType_FromModuleAndSpec(module, &type_spec, NULL);
    return state->type ? PyModule_AddType(module, (PyTypeObject *)state->type) : -1;
}
static int module_traverse(PyObject *module, visitproc visit, void *arg) {
    QueueModule *state = PyModule_GetState(module);
    Py_VISIT(state->type); Py_VISIT(state->empty);
    return 0;
}
static int module_clear(PyObject *module) {
    QueueModule *state = PyModule_GetState(module);
    Py_CLEAR(state->type); Py_CLEAR(state->empty);
    return 0;
}
static void module_free(void *module) { module_clear(module); }
static PyModuleDef_Slot module_slots[] = {{Py_mod_exec, module_exec}, {Py_mod_multiple_interpreters, Py_MOD_PER_INTERPRETER_GIL_SUPPORTED}, {0, NULL}};
static PyModuleDef definition = {PyModuleDef_HEAD_INIT, "_queue", "Native Jac FIFO queue.", sizeof(QueueModule), NULL, module_slots, module_traverse, module_clear, module_free};
PyMODINIT_FUNC PyInit__queue(void) { return PyModuleDef_Init(&definition); }
