/* Python layouts, slots, reference visitation and signatures for native collections. */
#include <Python.h>
#include "internal/pycore_object.h"
#include <stddef.h>
#include <stdint.h>
#define H(p) ((uint64_t)(uintptr_t)(p))
#define O(h) ((PyObject *)(uintptr_t)(h))
typedef struct { PyObject *deque, *forward, *reverse, *defaults, *getter; } Module;
typedef struct { PyObject_HEAD void *native; PyObject *weakrefs; } Deque;
typedef struct { PyObject_HEAD void *cursor; PyObject *owner; } Iterator;
typedef struct { PyDictObject dict; PyObject *factory; } DefaultDict;
typedef struct { PyObject_HEAD Py_ssize_t index; PyObject *doc; } Getter;
static PyModuleDef definition;
extern void jac_release(void *);
extern void *jacpy_deque_new(void);
extern int64_t jacpy_deque_size(void *), jacpy_deque_limit(void *), jacpy_deque_memory(void *);
extern void jacpy_deque_set_limit(void *, int64_t), jacpy_deque_clear(void *);
extern uint64_t jacpy_deque_borrow(void *, int64_t), jacpy_deque_pop(void *, int64_t);
extern void jacpy_deque_append(void *, uint64_t, int64_t);
extern int64_t jacpy_deque_extend(void *, uint64_t, uint64_t, int64_t);
extern uint64_t jacpy_deque_item(void *, int64_t);
extern int64_t jacpy_deque_assign(void *, int64_t, uint64_t), jacpy_deque_insert(void *, int64_t, uint64_t);
extern void jacpy_deque_rotate(void *, int64_t), jacpy_deque_reverse(void *);
extern int64_t jacpy_deque_search(void *, uint64_t, int64_t, int64_t, int64_t);
extern void *jacpy_deque_cursor(void *, int64_t, int64_t);
extern uint64_t jacpy_deque_next(void *);
extern int64_t jacpy_deque_remaining(void *), jacpy_deque_consumed(void *);
extern uint64_t jacpy_deque_compare(void *, void *, uint64_t, uint64_t, int64_t), jacpy_deque_repr(void *, uint64_t);
extern int64_t jacpy_deque_repeat(void *, int64_t), jacpy_count_elements(uint64_t, uint64_t);
extern uint64_t jacpy_default_missing(uint64_t, uint64_t, uint64_t);
static Module *module_for(PyTypeObject *type) {
    PyObject *module = PyType_GetModuleByDef(type, &definition);
    return module ? PyModule_GetState(module) : NULL;
}
static PyObject *deque_new(PyTypeObject *type, PyObject *args, PyObject *kw) {
    Deque *self = (Deque *)type->tp_alloc(type, 0);
    if (self) self->native = jacpy_deque_new();
    return (PyObject *)self;
}
static int deque_init(PyObject *op, PyObject *args, PyObject *kw) {
    static char *names[] = {"iterable", "maxlen", NULL};
    PyObject *iterable = NULL, *maximum = Py_None;
    if (!PyArg_ParseTupleAndKeywords(args, kw, "|OO:deque", names, &iterable, &maximum)) return -1;
    Py_ssize_t limit = -1;
    if (maximum != Py_None) {
        limit = PyLong_AsSsize_t(maximum);
        if (limit == -1 && PyErr_Occurred()) return -1;
        if (limit < 0) { PyErr_SetString(PyExc_ValueError, "maxlen must be non-negative"); return -1; }
    }
    void *state = ((Deque *)op)->native;
    jacpy_deque_set_limit(state, limit); jacpy_deque_clear(state);
    return iterable ? (int)jacpy_deque_extend(state, H(op), H(iterable), 0) : 0;
}
static int deque_traverse(PyObject *op, visitproc visit, void *arg) {
    Py_VISIT(Py_TYPE(op)); void *state = ((Deque *)op)->native;
    if (state) for (int64_t i=0, n=jacpy_deque_size(state); i<n; i++) Py_VISIT(O(jacpy_deque_borrow(state, i)));
    return 0;
}
static int deque_clear(PyObject *op) { if (((Deque *)op)->native) jacpy_deque_clear(((Deque *)op)->native); return 0; }
static void deque_dealloc(PyObject *op) {
    PyTypeObject *type = Py_TYPE(op); Deque *self = (Deque *)op;
    PyObject_GC_UnTrack(op);
    if (self->weakrefs) PyObject_ClearWeakRefs(op);
    deque_clear(op); if (self->native) jac_release(self->native);
    type->tp_free(op); Py_DECREF(type);
}
static Py_ssize_t deque_len(PyObject *op) { return (Py_ssize_t)jacpy_deque_size(((Deque *)op)->native); }
static PyObject *deque_item(PyObject *op, Py_ssize_t i) { return O(jacpy_deque_item(((Deque *)op)->native, i)); }
static int deque_assign(PyObject *op, Py_ssize_t i, PyObject *value) { return (int)jacpy_deque_assign(((Deque *)op)->native, i, H(value)); }
static PyObject *deque_append(PyObject *op, PyObject *item) { jacpy_deque_append(((Deque *)op)->native, H(item), 0); Py_RETURN_NONE; }
static PyObject *deque_appendleft(PyObject *op, PyObject *item) { jacpy_deque_append(((Deque *)op)->native, H(item), 1); Py_RETURN_NONE; }
static PyObject *deque_pop(PyObject *op, PyObject *unused) { return O(jacpy_deque_pop(((Deque *)op)->native, 0)); }
static PyObject *deque_popleft(PyObject *op, PyObject *unused) { return O(jacpy_deque_pop(((Deque *)op)->native, 1)); }
static PyObject *deque_extend(PyObject *op, PyObject *value) {
    if (jacpy_deque_extend(((Deque *)op)->native, H(op), H(value), 0) < 0) return NULL; Py_RETURN_NONE;
}
static PyObject *deque_extendleft(PyObject *op, PyObject *value) {
    if (jacpy_deque_extend(((Deque *)op)->native, H(op), H(value), 1) < 0) return NULL; Py_RETURN_NONE;
}
static PyObject *deque_clear_method(PyObject *op, PyObject *unused) { deque_clear(op); Py_RETURN_NONE; }
static PyObject *deque_reverse(PyObject *op, PyObject *unused) { jacpy_deque_reverse(((Deque *)op)->native); Py_RETURN_NONE; }
static PyObject *deque_rotate(PyObject *op, PyObject *args) {
    Py_ssize_t amount = 1; if (!PyArg_ParseTuple(args, "|n:rotate", &amount)) return NULL;
    jacpy_deque_rotate(((Deque *)op)->native, amount); Py_RETURN_NONE;
}
static PyObject *deque_insert(PyObject *op, PyObject *args) {
    Py_ssize_t index; PyObject *value;
    if (!PyArg_ParseTuple(args, "nO:insert", &index, &value)) return NULL;
    if (jacpy_deque_insert(((Deque *)op)->native, index, H(value)) < 0) return NULL; Py_RETURN_NONE;
}
static PyObject *deque_count(PyObject *op, PyObject *value) {
    int64_t n=jacpy_deque_search(((Deque *)op)->native, H(value), 0, PY_SSIZE_T_MAX, 0);
    return n < 0 ? NULL : PyLong_FromLongLong(n);
}
static int deque_contains(PyObject *op, PyObject *value) { return (int)jacpy_deque_search(((Deque *)op)->native,H(value),0,PY_SSIZE_T_MAX,1); }
static PyObject *deque_index(PyObject *op, PyObject *args) {
    PyObject *value, *first=NULL, *last=NULL; Py_ssize_t start=0, stop=PY_SSIZE_T_MAX;
    if (!PyArg_ParseTuple(args,"O|OO:index",&value,&first,&last)) return NULL;
    if (first) { start=PyNumber_AsSsize_t(first,NULL); if(start==-1 && PyErr_Occurred()) return NULL; }
    if (last) { stop=PyNumber_AsSsize_t(last,NULL); if(stop==-1 && PyErr_Occurred()) return NULL; }
    int64_t result=jacpy_deque_search(((Deque *)op)->native,H(value),start,stop,2);
    return result<0 ? NULL : PyLong_FromLongLong(result);
}
static PyObject *deque_remove(PyObject *op, PyObject *value) {
    if(jacpy_deque_search(((Deque *)op)->native,H(value),0,PY_SSIZE_T_MAX,3)<0) return NULL; Py_RETURN_NONE;
}
static PyObject *deque_maxlen(PyObject *op, void *unused) { int64_t limit=jacpy_deque_limit(((Deque *)op)->native); if(limit<0) Py_RETURN_NONE; return PyLong_FromLongLong(limit); }
static PyObject *deque_sizeof(PyObject *op, PyObject *unused) { return PyLong_FromSsize_t(Py_TYPE(op)->tp_basicsize+(Py_ssize_t)jacpy_deque_memory(((Deque *)op)->native)); }
static PyObject *deque_repr(PyObject *op) { return O(jacpy_deque_repr(((Deque *)op)->native,H(op))); }
static PyObject *deque_compare(PyObject *a, PyObject *b, int operation) {
    Module *m=module_for(Py_TYPE(a)); if(!m) return NULL;
    if(!PyObject_TypeCheck(b,(PyTypeObject *)m->deque)) Py_RETURN_NOTIMPLEMENTED;
    return O(jacpy_deque_compare(((Deque *)a)->native,((Deque *)b)->native,H(a),H(b),operation));
}
static PyObject *deque_copy(PyObject *op, PyObject *unused) {
    Module *m=module_for(Py_TYPE(op)); if(!m) return NULL;
    int64_t limit=jacpy_deque_limit(((Deque *)op)->native);
    PyObject *result;
    if(limit<0) result=PyObject_CallOneArg((PyObject *)Py_TYPE(op),op);
    else {
        PyObject *maximum=PyLong_FromLongLong(limit); if(!maximum) return NULL;
        result=PyObject_CallFunctionObjArgs((PyObject *)Py_TYPE(op),op,maximum,NULL); Py_DECREF(maximum);
    }
    if(result && !PyObject_TypeCheck(result,(PyTypeObject *)m->deque)) {
        PyErr_Format(PyExc_TypeError,"%.200s() must return a deque, not %.200s",Py_TYPE(op)->tp_name,Py_TYPE(result)->tp_name);
        Py_CLEAR(result);
    }
    return result;
}
static PyObject *deque_concat(PyObject *a, PyObject *b) {
    Module *m=module_for(Py_TYPE(a)); if(!m) return NULL;
    if(!PyObject_TypeCheck(b,(PyTypeObject *)m->deque)) { PyErr_Format(PyExc_TypeError,"can only concatenate deque (not \"%.200s\") to deque",Py_TYPE(b)->tp_name); return NULL; }
    PyObject *result=deque_copy(a,NULL); if(!result) return NULL;
    if(jacpy_deque_extend(((Deque *)result)->native,H(result),H(b),0)<0) Py_CLEAR(result); return result;
}
static PyObject *deque_iconcat(PyObject *op, PyObject *value) {
    if(jacpy_deque_extend(((Deque *)op)->native,H(op),H(value),0)<0) return NULL; return Py_NewRef(op);
}
static PyObject *deque_irepeat(PyObject *op, Py_ssize_t n) { if(jacpy_deque_repeat(((Deque *)op)->native,n)<0) return NULL; return Py_NewRef(op); }
static PyObject *deque_repeat(PyObject *op, Py_ssize_t n) {
    PyObject *result=deque_copy(op,NULL); if(!result) return NULL;
    if(jacpy_deque_repeat(((Deque *)result)->native,n)<0) Py_CLEAR(result); return result;
}
static PyObject *iterator_create(PyTypeObject *type, PyObject *owner, int reverse, Py_ssize_t skip) {
    Iterator *it=(Iterator *)type->tp_alloc(type,0); if(!it) return NULL;
    it->owner=Py_NewRef(owner); it->cursor=jacpy_deque_cursor(((Deque *)owner)->native,reverse,skip); return (PyObject *)it;
}
static PyObject *deque_iter(PyObject *op) { Module *m=module_for(Py_TYPE(op)); return m ? iterator_create((PyTypeObject *)m->forward,op,0,0) : NULL; }
static PyObject *deque_reversed(PyObject *op, PyObject *unused) { Module *m=module_for(Py_TYPE(op)); return m ? iterator_create((PyTypeObject *)m->reverse,op,1,0) : NULL; }
static int iterator_clear(PyObject *op) { Iterator *it=(Iterator *)op; void *cursor=it->cursor; it->cursor=NULL; Py_CLEAR(it->owner); if(cursor) jac_release(cursor); return 0; }
static int iterator_traverse(PyObject *op, visitproc visit, void *arg) { Py_VISIT(Py_TYPE(op)); Py_VISIT(((Iterator *)op)->owner); return 0; }
static void iterator_dealloc(PyObject *op) { PyTypeObject *type=Py_TYPE(op); PyObject_GC_UnTrack(op); iterator_clear(op); type->tp_free(op); Py_DECREF(type); }
static PyObject *iterator_next(PyObject *op) { return ((Iterator *)op)->cursor ? O(jacpy_deque_next(((Iterator *)op)->cursor)) : NULL; }
static PyObject *iterator_hint(PyObject *op, PyObject *unused) { return PyLong_FromLongLong(((Iterator *)op)->cursor ? jacpy_deque_remaining(((Iterator *)op)->cursor) : 0); }
static PyObject *iterator_new(PyTypeObject *type, PyObject *args, PyObject *kw) {
    PyObject *owner; Py_ssize_t skip=0; Module *m=module_for(type); if(!m) return NULL;
    if(kw && PyDict_GET_SIZE(kw)) { PyErr_SetString(PyExc_TypeError,"deque iterator takes no keyword arguments"); return NULL; }
    if(!PyArg_ParseTuple(args,"O|n",&owner,&skip)) return NULL;
    if(!PyObject_TypeCheck(owner,(PyTypeObject *)m->deque)) { PyErr_SetString(PyExc_TypeError,"argument must be a deque"); return NULL; }
    return iterator_create(type,owner,type==(PyTypeObject *)m->reverse,skip);
}
static PyObject *iterator_reduce(PyObject *op, PyObject *unused) {
    Iterator *it=(Iterator *)op;
    return Py_BuildValue("O(On)",Py_TYPE(op),it->owner,(Py_ssize_t)jacpy_deque_consumed(it->cursor));
}
static PyObject *deque_reduce(PyObject *op, PyObject *unused) {
    PyObject *state=_PyObject_GetState(op); if(!state) return NULL;
    PyObject *it=PyObject_GetIter(op); if(!it) { Py_DECREF(state); return NULL; }
    int64_t limit=jacpy_deque_limit(((Deque *)op)->native);
    return limit<0 ? Py_BuildValue("O()NN",Py_TYPE(op),state,it) : Py_BuildValue("O(()n)NN",Py_TYPE(op),(Py_ssize_t)limit,state,it);
}
static PyMethodDef deque_methods[] = {
    {"append",deque_append,METH_O,"Add an element to the right side of the deque."},{"appendleft",deque_appendleft,METH_O,"Add an element to the left side of the deque."},
    {"pop",deque_pop,METH_NOARGS,"Remove and return the rightmost element."},{"popleft",deque_popleft,METH_NOARGS,"Remove and return the leftmost element."},
    {"extend",deque_extend,METH_O,"Extend the right side of the deque with elements from the iterable."},{"extendleft",deque_extendleft,METH_O,"Extend the left side of the deque with elements from the iterable."},
    {"clear",deque_clear_method,METH_NOARGS,"Remove all elements from the deque."},{"reverse",deque_reverse,METH_NOARGS,"Reverse *IN PLACE*."},
    {"rotate",deque_rotate,METH_VARARGS,"Rotate the deque n steps to the right.  If n is negative, rotates left."},{"insert",deque_insert,METH_VARARGS,"Insert value before index."},
    {"count",deque_count,METH_O,"Return number of occurrences of value."},{"index",deque_index,METH_VARARGS,"Return first index of value.\n\nRaises ValueError if the value is not present."},{"remove",deque_remove,METH_O,"Remove first occurrence of value."},
    {"copy",deque_copy,METH_NOARGS,"Return a shallow copy of a deque."},{"__copy__",deque_copy,METH_NOARGS,"Return a shallow copy of a deque."},
    {"__reduce__",deque_reduce,METH_NOARGS,"Return state information for pickling."},{"__reversed__",deque_reversed,METH_NOARGS,"Return a reverse iterator over the deque."},
    {"__sizeof__",deque_sizeof,METH_NOARGS,"Return the size of the deque in memory, in bytes."},{"__class_getitem__",Py_GenericAlias,METH_O|METH_CLASS,"deques are generic over the type of their contents"},{NULL}
};
static PyGetSetDef deque_getsets[]={{"maxlen",deque_maxlen,NULL,NULL,NULL},{NULL}};
static PyMemberDef deque_members[]={{"__weaklistoffset__",Py_T_PYSSIZET,offsetof(Deque,weakrefs),Py_READONLY},{NULL}};
static PyType_Slot deque_slots[]={
    {Py_tp_doc,"A list-like sequence optimized for data accesses near its endpoints."},{Py_tp_new,deque_new},{Py_tp_init,deque_init},{Py_tp_dealloc,deque_dealloc},{Py_tp_traverse,deque_traverse},{Py_tp_clear,deque_clear},
    {Py_tp_iter,deque_iter},{Py_tp_repr,deque_repr},{Py_tp_richcompare,deque_compare},{Py_tp_hash,PyObject_HashNotImplemented},
    {Py_tp_methods,deque_methods},{Py_tp_getset,deque_getsets},{Py_tp_members,deque_members},
    {Py_sq_length,deque_len},{Py_sq_item,deque_item},{Py_sq_ass_item,deque_assign},{Py_sq_contains,deque_contains},
    {Py_sq_concat,deque_concat},{Py_sq_inplace_concat,deque_iconcat},{Py_sq_repeat,deque_repeat},{Py_sq_inplace_repeat,deque_irepeat},{0,NULL}
};
static PyMethodDef iterator_methods[]={{"__length_hint__",iterator_hint,METH_NOARGS,"Private method returning an estimate of len(list(it))."},{"__reduce__",iterator_reduce,METH_NOARGS,"Return state information for pickling."},{NULL}};
static PyType_Slot iterator_slots[]={{Py_tp_new,iterator_new},{Py_tp_dealloc,iterator_dealloc},{Py_tp_traverse,iterator_traverse},{Py_tp_clear,iterator_clear},{Py_tp_iter,PyObject_SelfIter},{Py_tp_iternext,iterator_next},{Py_tp_methods,iterator_methods},{0,NULL}};
#define FLAGS (Py_TPFLAGS_DEFAULT|Py_TPFLAGS_HAVE_GC|Py_TPFLAGS_IMMUTABLETYPE)
static PyType_Spec deque_spec={"collections.deque",sizeof(Deque),0,FLAGS|Py_TPFLAGS_BASETYPE|Py_TPFLAGS_SEQUENCE,deque_slots};
static PyType_Spec forward_spec={"_collections._deque_iterator",sizeof(Iterator),0,FLAGS,iterator_slots};
static PyType_Spec reverse_spec={"_collections._deque_reverse_iterator",sizeof(Iterator),0,FLAGS,iterator_slots};

static int default_init(PyObject *op, PyObject *args, PyObject *kw) {
    DefaultDict *self=(DefaultDict *)op; Py_ssize_t n=PyTuple_GET_SIZE(args);
    PyObject *factory=n ? PyTuple_GET_ITEM(args,0) : NULL;
    if(factory && factory!=Py_None && !PyCallable_Check(factory)) { PyErr_SetString(PyExc_TypeError,"first argument must be callable or None"); return -1; }
    Py_XINCREF(factory); Py_XSETREF(self->factory,factory);
    PyObject *rest=PyTuple_GetSlice(args,n ? 1 : 0,n); if(!rest) return -1;
    int result=PyDict_Type.tp_init(op,rest,kw); Py_DECREF(rest); return result;
}
static int default_traverse(PyObject *op, visitproc visit, void *arg) {
    Py_VISIT(Py_TYPE(op)); Py_VISIT(((DefaultDict *)op)->factory);
    return PyDict_Type.tp_traverse(op,visit,arg);
}
static int default_clear(PyObject *op) { Py_CLEAR(((DefaultDict *)op)->factory); return PyDict_Type.tp_clear(op); }
static void default_dealloc(PyObject *op) {
    PyTypeObject *type=Py_TYPE(op); PyObject_GC_UnTrack(op); Py_CLEAR(((DefaultDict *)op)->factory);
    PyDict_Type.tp_dealloc(op); Py_DECREF(type);
}
static PyObject *default_missing(PyObject *op, PyObject *key) {
    PyObject *factory=Py_XNewRef(((DefaultDict *)op)->factory);
    PyObject *result=O(jacpy_default_missing(H(op),H(factory),H(key))); Py_XDECREF(factory); return result;
}
static PyObject *default_construct(PyObject *op, PyObject *mapping) {
    PyObject *factory=((DefaultDict *)op)->factory;
    return PyObject_CallFunctionObjArgs((PyObject *)Py_TYPE(op),factory ? factory : Py_None,mapping,NULL);
}
static PyObject *default_copy(PyObject *op, PyObject *unused) { return default_construct(op,op); }
static PyObject *default_reduce(PyObject *op, PyObject *unused) {
    PyObject *factory=((DefaultDict *)op)->factory;
    PyObject *args=(!factory || factory==Py_None) ? PyTuple_New(0) : PyTuple_Pack(1,factory); if(!args) return NULL;
    PyObject *items=PyObject_CallMethod(op,"items",NULL); if(!items) { Py_DECREF(args); return NULL; }
    PyObject *it=PyObject_GetIter(items); Py_DECREF(items); if(!it) { Py_DECREF(args); return NULL; }
    PyObject *result=PyTuple_Pack(5,Py_TYPE(op),args,Py_None,Py_None,it); Py_DECREF(args); Py_DECREF(it); return result;
}
extern uint64_t jacpy_default_repr(uint64_t, uint64_t);
static PyObject *default_repr(PyObject *op) {
    PyObject *factory=Py_XNewRef(((DefaultDict *)op)->factory);
    PyObject *result=O(jacpy_default_repr(H(op),H(factory))); Py_XDECREF(factory); return result;
}
static PyObject *default_or(PyObject *a, PyObject *b) {
    Module *m=NULL; PyObject *owner=a;
    if(Py_TYPE(a)->tp_as_number && Py_TYPE(a)->tp_as_number->nb_or==default_or) m=module_for(Py_TYPE(a));
    else { owner=b; m=module_for(Py_TYPE(b)); }
    if(!m) return NULL;
    if(!PyDict_Check(a) || !PyDict_Check(b)) Py_RETURN_NOTIMPLEMENTED;
    PyObject *result=default_construct(owner,a); if(!result) return NULL;
    if(PyDict_Update(result,b)<0) Py_CLEAR(result); return result;
}
static PyMethodDef default_methods[]={{"__missing__",default_missing,METH_O,"__missing__(key) # Called by __getitem__ for missing key; pseudo-code:\n  if self.default_factory is None: raise KeyError((key,))\n  self[key] = value = self.default_factory()\n  return value\n"},{"copy",default_copy,METH_NOARGS,"D.copy() -> a shallow copy of D."},{"__copy__",default_copy,METH_NOARGS,"D.copy() -> a shallow copy of D."},{"__reduce__",default_reduce,METH_NOARGS,"Return state information for pickling."},{"__class_getitem__",Py_GenericAlias,METH_O|METH_CLASS,"defaultdicts are generic over two types, signifying (respectively) the types of the dictionary's keys and values"},{NULL}};
static PyMemberDef default_members[]={{"default_factory",_Py_T_OBJECT,offsetof(DefaultDict,factory),0,NULL},{NULL}};
static PyType_Slot default_slots[]={{Py_tp_doc,"defaultdict(default_factory=None, /, [...]) --> dict with default factory\n\nThe default factory is called without arguments to produce\na new value when a key is not present, in __getitem__ only.\nA defaultdict compares equal to a dict with the same items.\nAll remaining arguments are treated the same as if they were\npassed to the dict constructor, including keyword arguments.\n"},{Py_tp_init,default_init},{Py_tp_dealloc,default_dealloc},{Py_tp_traverse,default_traverse},{Py_tp_clear,default_clear},{Py_tp_repr,default_repr},{Py_nb_or,default_or},{Py_tp_methods,default_methods},{Py_tp_members,default_members},{0,NULL}};
static PyType_Spec default_spec={"collections.defaultdict",sizeof(DefaultDict),0,FLAGS|Py_TPFLAGS_BASETYPE,default_slots};

static PyObject *getter_new(PyTypeObject *type, PyObject *args, PyObject *kw) {
    if(kw && PyDict_GET_SIZE(kw)) { PyErr_SetString(PyExc_TypeError,"_tuplegetter takes no keyword arguments"); return NULL; }
    Py_ssize_t index; PyObject *doc; if(!PyArg_ParseTuple(args,"nO:_tuplegetter",&index,&doc)) return NULL;
    Getter *self=(Getter *)type->tp_alloc(type,0); if(!self) return NULL;
    self->index=index; self->doc=Py_NewRef(doc); return (PyObject *)self;
}
static PyObject *getter_get(PyObject *op, PyObject *object, PyObject *owner) {
    if(!object) return Py_NewRef(op);
    if(!PyTuple_Check(object)) { PyErr_Format(PyExc_TypeError,"descriptor for index '%zd' for tuple subclasses doesn't apply to '%s' object",((Getter *)op)->index,Py_TYPE(object)->tp_name); return NULL; }
    PyObject *item=PyTuple_GetItem(object,((Getter *)op)->index); return Py_XNewRef(item);
}
static int getter_set(PyObject *op, PyObject *object, PyObject *value) { PyErr_SetString(PyExc_AttributeError,value ? "can't set attribute" : "can't delete attribute"); return -1; }
static int getter_traverse(PyObject *op, visitproc visit, void *arg) { Py_VISIT(Py_TYPE(op)); Py_VISIT(((Getter *)op)->doc); return 0; }
static int getter_clear(PyObject *op) { Py_CLEAR(((Getter *)op)->doc); return 0; }
static void getter_dealloc(PyObject *op) { PyTypeObject *type=Py_TYPE(op); PyObject_GC_UnTrack(op); getter_clear(op); type->tp_free(op); Py_DECREF(type); }
static PyObject *getter_repr(PyObject *op) { Getter *self=(Getter *)op; return PyUnicode_FromFormat("%s(%zd, %R)",_PyType_Name(Py_TYPE(op)),self->index,self->doc ? self->doc : Py_None); }
static PyObject *getter_reduce(PyObject *op, PyObject *unused) { Getter *self=(Getter *)op; return Py_BuildValue("O(nO)",Py_TYPE(op),self->index,self->doc ? self->doc : Py_None); }
static PyMemberDef getter_members[]={{"__doc__",_Py_T_OBJECT,offsetof(Getter,doc),0,NULL},{NULL}};
static PyMethodDef getter_methods[]={{"__reduce__",getter_reduce,METH_NOARGS,""},{NULL}};
static PyType_Slot getter_slots[]={{Py_tp_new,getter_new},{Py_tp_descr_get,getter_get},{Py_tp_descr_set,getter_set},{Py_tp_traverse,getter_traverse},{Py_tp_clear,getter_clear},{Py_tp_dealloc,getter_dealloc},{Py_tp_repr,getter_repr},{Py_tp_members,getter_members},{Py_tp_methods,getter_methods},{0,NULL}};
static PyType_Spec getter_spec={"collections._tuplegetter",sizeof(Getter),0,FLAGS,getter_slots};

static PyObject *count_elements(PyObject *module, PyObject *args) { PyObject *mapping,*iterable; if(!PyArg_ParseTuple(args,"OO:_count_elements",&mapping,&iterable)) return NULL; if(jacpy_count_elements(H(mapping),H(iterable))<0) return NULL; Py_RETURN_NONE; }
static PyMethodDef methods[]={{"_count_elements",count_elements,METH_VARARGS,NULL},{NULL}};
static int module_traverse(PyObject *module, visitproc visit, void *arg) { Module *m=PyModule_GetState(module); Py_VISIT(m->deque); Py_VISIT(m->forward); Py_VISIT(m->reverse); Py_VISIT(m->defaults); Py_VISIT(m->getter); return 0; }
static int module_clear(PyObject *module) { Module *m=PyModule_GetState(module); Py_CLEAR(m->deque); Py_CLEAR(m->forward); Py_CLEAR(m->reverse); Py_CLEAR(m->defaults); Py_CLEAR(m->getter); return 0; }
static void module_free(void *module) { module_clear(module); }
static int add_type(PyObject *module, PyObject **out, PyType_Spec *spec, PyObject *bases) { *out=PyType_FromModuleAndSpec(module,spec,bases); return *out ? PyModule_AddType(module,(PyTypeObject *)*out) : -1; }
static int module_exec(PyObject *module) {
    Module *m=PyModule_GetState(module);
    if(add_type(module,&m->deque,&deque_spec,NULL)<0 || add_type(module,&m->forward,&forward_spec,NULL)<0 || add_type(module,&m->reverse,&reverse_spec,NULL)<0 || add_type(module,&m->getter,&getter_spec,NULL)<0) return -1;
    PyObject *bases=PyTuple_Pack(1,&PyDict_Type); if(!bases) return -1;
    int result=add_type(module,&m->defaults,&default_spec,bases); Py_DECREF(bases); if(result<0) return -1;
    return PyModule_AddType(module,&PyODict_Type);
}
static PyModuleDef_Slot slots[]={{Py_mod_exec,module_exec},{Py_mod_multiple_interpreters,Py_MOD_PER_INTERPRETER_GIL_SUPPORTED},{0,NULL}};
static PyModuleDef definition={PyModuleDef_HEAD_INIT,"_collections","Native Jac collections.",sizeof(Module),methods,slots,module_traverse,module_clear,module_free};
PyMODINIT_FUNC PyInit__collections(void) { return PyModuleDef_Init(&definition); }
