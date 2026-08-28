"""The object-spatial surface the jac0 seed tier lowers to.

jac0 rewrites `node`/`edge`/`walker` declarations, `+>:T:+>`, `[x ->:T:->]`,
`spawn`, `visit`, `disengage`, `del` and `can f with T entry|exit` into calls
on this module. Structure lives in the kernel's rows (`jaclang.runtime.osp_graph`):
`connect0` mints handles for both endpoints in the seed store and links one
row per edge, indexed by (node, direction, edge type) with the edge class's
MRO tags so a hop on a base edge type sees every subclass row; `refs0` reads
those chains by handle and never touches an anchor's edge list.

Lifetime is regions. `push_region` / `pop_region` bracket one module's build;
a node registered in a region (region-scoped classes only, see
`mark_region_scoped`) dies with it in `region_close`, which retires every row
touching the region's nodes and frees their handles.
"""

from __future__ import annotations

from typing import Any, Callable

from jaclang.runtime.archetype import (
    EdgeArchetype as Edge,
    NodeArchetype as Node,
    WalkerArchetype as Walker,
)

__all__ = [
    "Node",
    "Edge",
    "Walker",
    "connect0",
    "disconnect0",
    "refs0",
    "spawn0",
    "visit0",
    "disengage0",
    "destroy0",
    "on_entry",
    "on_exit",
    "set_trigger",
    "push_region",
    "pop_region",
    "current_region",
    "region_close",
    "mark_region_scoped",
    "clone_subtree",
    "edge_key_put",
    "edge_key_refs",
]

_ARCH = (Node, Edge, Walker)

_G: Any = None
_SV: Any = None
_ST: Any = None
_regions: list[int] = []
_scoped: list[type] = []
_scoped_tuple: tuple = ()
_TAG: dict = {}


def _rt() -> Any:
    from jaclang.runtime.runtime import JacRuntimeInterface

    return JacRuntimeInterface


def _kernel() -> Any:
    global _G, _SV, _ST
    if _G is None:
        import jaclang.runtime.osp_graph as g
        import jaclang.runtime.osp_graph_sv as sv

        _G = g
        _SV = sv
        _ST = sv._SvStore(region=0)
    return _G


def mark_region_scoped(cls: type) -> type:
    """Nodes of `cls` (and subclasses) belong to the region current at their
    first connect and die with it. Everything else is region-free."""
    global _scoped_tuple
    _scoped.append(cls)
    _scoped_tuple = tuple(_scoped)
    return cls


def push_region(rgn: int = 0) -> int:
    """Enter a region: a fresh one when `rgn` is 0, else re-enter `rgn`."""
    _kernel()
    if rgn == 0:
        rgn = _SV._next_region[0]
        _SV._next_region[0] = rgn + 1
    _regions.append(rgn)
    return rgn


def pop_region() -> int:
    if not _regions:
        return 0
    rgn = _regions.pop()
    if _pending_regions:
        _sweep_regions()
    return rgn


def current_region() -> int:
    return _regions[-1] if _regions else 0


def _region_for(arch: Any) -> int:
    if getattr(arch, "_osp_region_scoped", False) or (
        _scoped_tuple and isinstance(arch, _scoped_tuple)
    ):
        return current_region()
    return 0


def _free_handle(h: int) -> None:
    objs = _SV._obj_of_h
    if h <= 0 or h >= len(objs):
        return
    a = objs[h]
    if a is None:
        return
    if a is not _ROW:
        _ST.h_of_key.pop(id(a), None)
        aid = a.id
        if aid is not None:
            _ST.h_of_key.pop(aid, None)
        a.__dict__.pop("_osp_h", None)
    objs[h] = None
    _SV._h_free.append(h)


_pending_regions: set[int] = set()
_backref_tags: list[int] | None = None


def _backref_tag_list() -> list[int]:
    """Edge kinds that point from a dependency back at its users; they never
    keep the target region alive."""
    global _backref_tags
    if _backref_tags is None:
        tags: list[int] = []
        try:
            from jaclang.compiler.frontend.relations import Uses

            tags.append(_SV.tag_of_cls(Uses))
        except Exception:
            pass
        _backref_tags = tags
    return _backref_tags


_closed_regions: set[int] = set()


def _close_now(rgn: int) -> int:
    g = _kernel()
    nodes = g.osp_region_handles(rgn)
    edges = g.osp_region_edges(rgn)
    n = g.osp_region_died(rgn)
    for h in edges:
        _free_handle(h)
    for h in nodes:
        _free_handle(h)
    _closed_regions.add(rgn)
    return n


def region_is_closed(rgn: int) -> bool:
    return rgn in _closed_regions


_inflight: dict[int, int] = {}
_inflight_stack: list[int] = []


def mark_inflight(rgn: int) -> None:
    """A module whose compile is in progress: its region is never closed,
    whatever the hub does, until the mark is released. Marks form a stack so a
    compile scope releases exactly what was pushed inside it, crash or not."""
    if rgn:
        _inflight[rgn] = _inflight.get(rgn, 0) + 1
        _inflight_stack.append(rgn)


def unmark_inflight(rgn: int) -> int:
    if not rgn:
        return 0
    n = _inflight.get(rgn, 0) - 1
    if n <= 0:
        _inflight.pop(rgn, None)
    else:
        _inflight[rgn] = n
    if _pending_regions:
        return _sweep_regions()
    return 0


def inflight_depth() -> int:
    return len(_inflight_stack)


def release_inflight_to(depth: int) -> int:
    total = 0
    while len(_inflight_stack) > depth:
        total += unmark_inflight(_inflight_stack.pop())
    return total


def _held(rgn: int) -> bool:
    return rgn in _inflight or rgn in _regions


def _sweep_regions() -> int:
    g = _kernel()
    total = 0
    progress = True
    skip = _backref_tag_list()
    while progress and _pending_regions:
        progress = False
        for r in list(_pending_regions):
            if _held(r):
                continue
            blockers = g.osp_region_blockers(r, skip)
            if all(b in _pending_regions and not _held(b) for b in blockers):
                _pending_regions.discard(r)
                total += _close_now(r)
                progress = True
    return total


def region_close(rgn: int) -> int:
    """Retire every row touching the region's nodes and free their handles
    (nodes and edges), as soon as no row from another live region points into
    it; until then the region stays pending and closes when its referrers do
    (a cycle of pending regions closes together). Returns rows retired now."""
    if rgn == 0:
        return 0
    _kernel()
    _pending_regions.add(rgn)
    import os

    if os.environ.get("JAC_OSP_NO_REGION_CLOSE"):
        return 0
    return _sweep_regions()


def pending_regions() -> list[int]:
    return sorted(_pending_regions)


def _mint(anchor: Any) -> int:
    d = anchor.__dict__
    h = d.get("_osp_h", 0)
    if h:
        return h
    h = _SV._mint(_ST, anchor)
    d["_osp_h"] = h
    return h


def _handle(anchor: Any) -> int:
    h = anchor.__dict__.get("_osp_h", 0)
    if h:
        return h
    return _SV._handle_of(_ST, anchor)


_ROW = object()
_INFRA_FIELDS = frozenset(
    ("_jac_entry_funcs_", "_jac_exit_funcs_", "_subclass_hooks", "in_kid", "in_kids")
)
_objless_cache: dict[type, bool] = {}


def _objectless(cls: type) -> bool:
    """An edge class with no fields beyond the row flags needs no object per edge."""
    v = _objless_cache.get(cls)
    if v is None:
        names = getattr(cls, "__dataclass_fields__", None)
        v = names is not None and all(n in _INFRA_FIELDS for n in names)
        _objless_cache[cls] = v
    return v


def _alloc_row_handle() -> int:
    objs = _SV._obj_of_h
    free = _SV._h_free
    if free:
        h = free.pop()
        objs[h] = _ROW
        return h
    objs.append(_ROW)
    return len(objs) - 1


class _RowAnchor:
    """Anchor view over a row-only edge: enough for the readers of
    `e.__jac__` (source, target, persistent) and for `del e`."""

    __slots__ = ("__dict__",)

    def __init__(self, h: int) -> None:
        self.__dict__["_osp_h"] = h

    persistent = False
    id = None
    is_undirected = False

    @property
    def source(self) -> Any:
        objs = _SV._obj_of_h
        sh = _G.osp_edge_src(self.__dict__["_osp_h"])
        return objs[sh] if 0 < sh < len(objs) else None

    @property
    def target(self) -> Any:
        objs = _SV._obj_of_h
        th = _G.osp_edge_tgt(self.__dict__["_osp_h"])
        return objs[th] if 0 < th < len(objs) else None

    @property
    def archetype(self) -> Any:
        return _materialize(self.__dict__["_osp_h"])

    def is_populated(self) -> bool:
        return True


def _materialize(h: int) -> Any:
    g = _G
    cls = _SV.cls_of_tag(g.osp_edge_tag(h))
    if cls is None:
        return None
    e = cls.__new__(cls)
    d = e.__dict__
    flags = g.osp_row_flags(h)
    names = cls.__dataclass_fields__
    if "in_kid" in names:
        d["in_kid"] = (flags & 2) != 0
    if "in_kids" in names:
        d["in_kids"] = (flags & 2) != 0
    d["_osp_h"] = h
    d["__jac__"] = _RowAnchor(h)
    return e


def _arch_of(h: int) -> Any:
    objs = _SV._obj_of_h
    if h <= 0 or h >= len(objs):
        return None
    a = objs[h]
    if a is None:
        return None
    if a is _ROW:
        return _materialize(h)
    return a.archetype


def _link(l_arch: Any, r_arch: Any, e: Any, one_sided: bool, before: Any) -> Any:
    from jaclang.runtime.archetype import EdgeAnchor

    g = _kernel()
    src = l_arch.__jac__
    tgt = r_arch.__jac__
    cls = type(e)
    before_h = 0
    if before is not None:
        bj = getattr(before, "__jac__", None)
        if bj is not None:
            before_h = _handle(bj)
    if _objectless(cls):
        d = e.__dict__
        v = d.get("in_kid")
        if v is None:
            v = d.get("in_kids")
        flags = 2 if v else 0
        if one_sided:
            flags |= g.FLAG_ONE_SIDED
        eh = _alloc_row_handle()
        g.osp_conn2(
            _mint(src),
            _mint(tgt),
            eh,
            0,
            _SV.tags_for_cls(cls),
            flags,
            _region_for(l_arch),
            0 if one_sided else _region_for(r_arch),
            before_h,
        )
        return eh
    ea = EdgeAnchor(archetype=e, source=src, target=tgt, is_undirected=False)
    e.__jac__ = ea
    flags = _SV.flags_for_edge(ea)
    if one_sided:
        flags |= g.FLAG_ONE_SIDED
    g.osp_conn2(
        _mint(src),
        _mint(tgt),
        _mint(ea),
        0,
        _SV.tags_for_cls(cls),
        flags,
        _region_for(l_arch),
        0 if one_sided else _region_for(r_arch),
        before_h,
    )
    return ea


def connect0(
    left: Any,
    right: Any,
    edge: Any = None,
    conn_assign: tuple[tuple, tuple] | None = None,
    one_sided: bool = False,
    before: Any = None,
) -> Any:
    """Connect as kernel rows: no anchor list, no context, no persistence.

    `one_sided` rows are reachable from the source only and never register or
    pin the target (fan-in to shared nodes such as interned types). `before`
    names an existing child whose position the new row takes in the source's
    chains (ordered insert).
    """
    from jaclang.runtime.archetype import GenericEdge

    lefts = left if isinstance(left, list) else [left]
    rights = right if isinstance(right, list) else [right]
    ct = edge or GenericEdge
    for l_arch in lefts:
        for r_arch in rights:
            e = ct() if isinstance(ct, type) else ct
            if conn_assign:
                for fld, val in zip(conn_assign[0], conn_assign[1], strict=False):
                    setattr(e, fld, val)
            _link(l_arch, r_arch, e, one_sided, before)
    return right


def disconnect0(left: Any, right: Any, dir: int = 2) -> bool:
    from jaclang.compiler.frontend.constant import EdgeDir

    return _rt().disconnect(left=left, right=right, dir=EdgeDir(dir))


def _pred_ok(arch: Any, preds: tuple | None) -> bool:
    if not preds:
        return True
    for name, op, value in preds:
        cur = getattr(arch, name, None)
        if op == "==":
            if not (cur == value):
                return False
        elif op == "!=":
            if not (cur != value):
                return False
        elif op == "<":
            if not (cur < value):
                return False
        elif op == "<=":
            if not (cur <= value):
                return False
        elif op == ">":
            if not (cur > value):
                return False
        elif op == ">=":
            if not (cur >= value):
                return False
        else:
            raise ValueError(f"unsupported edge predicate operator {op!r}")
    return True


def _split_preds(preds: tuple | None) -> tuple[int, tuple | None]:
    """Fold `in_kid == True` / `in_kids == True` into the row flag filter."""
    if not preds:
        return 0, None
    flags = 0
    rest = []
    for p in preds:
        name, op, value = p
        if name in ("in_kid", "in_kids") and op == "==" and value is True:
            flags |= _kernel().FLAG_IN_KID
        else:
            rest.append(p)
    return flags, (tuple(rest) if rest else None)


def refs0(
    origin: Any,
    dir: int,
    edge: Any = None,
    edges_only: bool = False,
    preds: tuple | None = None,
    target: Any = None,
) -> list:
    """One hop over the kernel's typed chains.

    dir: 1 = in, 2 = out, 3 = any. `edge` selects the (node, direction, type)
    chain (subclass rows included through MRO tags), `preds` are (attr, op,
    value) edge-attribute predicates and `target` restricts the far end to one
    node. Results keep chain order, deduplicated. Origins may be a node or a
    list.
    """
    g = _G if _G is not None else _kernel()
    if edge is None:
        tag = -1
    else:
        tag = _TAG.get(edge)
        if tag is None:
            tag = _SV.tag_of_cls(edge)
            _TAG[edge] = tag
    if preds is None:
        need_flags = 0
        rest = None
    else:
        need_flags, rest = _split_preds(preds)
    want_edges = edges_only or rest is not None or target is not None
    if not want_edges and not isinstance(origin, list):
        oj = origin.__dict__.get("__jac__")
        if oj is None:
            return []
        oh = oj.__dict__.get("_osp_h", 0)
        if not oh:
            oh = _SV._handle_of(_ST, oj)
            if oh <= 0:
                return []
        hs = g.osp_refs_flag(oh, dir, tag, 0, need_flags)
        if not hs:
            return []
        objs = _SV._obj_of_h
        return [a.archetype for a in [objs[h] for h in hs] if a is not None]
    origins = origin if isinstance(origin, list) else [origin]
    out: list = []
    seen: set = set()
    for o in origins:
        oj = getattr(o, "__jac__", None)
        if oj is None:
            continue
        oh = _handle(oj)
        if oh <= 0:
            continue
        hs = g.osp_refs_flag(oh, dir, tag, 1 if want_edges else 0, need_flags)
        if not want_edges:
            for h in hs:
                item = _arch_of(h)
                if item is None:
                    continue
                k = id(item)
                if k in seen:
                    continue
                seen.add(k)
                out.append(item)
            continue
        for eh in hs:
            earch = _arch_of(eh)
            if earch is None:
                continue
            if rest is not None and not _pred_ok(earch, rest):
                continue
            if edges_only:
                item = earch
            else:
                item = _arch_of(g.osp_edge_peer(eh, oh))
                if item is None:
                    continue
            if target is not None and (edges_only or item is not target):
                if edges_only:
                    peer = _arch_of(g.osp_edge_peer(eh, oh))
                    if peer is not target:
                        continue
                else:
                    continue
            k = id(item)
            if k in seen:
                continue
            seen.add(k)
            out.append(item)
    return out


def edge_key_put(e: Any, key: str) -> None:
    """Index an edge under (source, edge type, key) for keyed lookups."""
    ea = getattr(e, "__jac__", None)
    if ea is None:
        return
    h = _handle(ea)
    if h > 0:
        _kernel().osp_key_put(h, _SV.key_of_str(key) if key else 0)


def edge_key_refs(origin: Any, edge: Any, key: str, edges_only: bool = False) -> list:
    """Rows of `origin` of type `edge` indexed under `key`, in insertion order."""
    g = _kernel()
    oj = getattr(origin, "__jac__", None)
    if oj is None:
        return []
    oh = _handle(oj)
    if oh <= 0:
        return []
    k = _SV._key_of_str.get(key, 0)
    if k == 0:
        return []
    out: list = []
    for eh in g.osp_key_rows(oh, _SV.tag_of_cls(edge), k):
        item = _arch_of(eh) if edges_only else _arch_of(g.osp_edge_peer(eh, oh))
        if item is not None:
            out.append(item)
    return out


def spawn0(op1: Any, op2: Any) -> Any:
    return _rt().spawn(op1, op2)


def visit0(walker: Any, expr: Any, insert_loc: int = -1) -> bool:
    return _rt().visit(walker, expr, insert_loc)


def disengage0(walker: Any) -> bool:
    return _rt().disengage(walker)


def _drop_edge(anchor: Any) -> bool:
    g = _kernel()
    h = _handle(anchor)
    if h <= 0:
        return False
    g.osp_disconnect(h)
    _free_handle(h)
    return True


def destroy0(obj: Any) -> None:
    if isinstance(obj, Edge):
        anchor = obj.__jac__
        if not anchor.persistent:
            _drop_edge(anchor)
            return
    if isinstance(obj, _ARCH):
        _rt().destroy(obj)


def _default_scalar_copy(src: Any) -> Any:
    import copy

    dup = copy.copy(src)
    dup.__dict__.pop("__jac__", None)
    after = getattr(dup, "_after_clone", None)
    if after is not None:
        after()
    return dup


def clone_subtree(
    nd: Any, role_base: type, copy_scalars: Callable[[Any], Any] | None = None
) -> Any:
    """Deep-copy a node and its outgoing `role_base` structure as fresh rows.

    `copy_scalars(node)` returns a scalar-only copy of one node (no anchor);
    by default that is a shallow copy without its anchor, followed by the
    node's own `_after_clone` hook. The clone re-links each out edge of the
    role family, in chain order, with an edge of the same class and
    attributes."""
    g = _kernel()
    memo: dict[int, Any] = {}
    scalars = copy_scalars if copy_scalars is not None else _default_scalar_copy

    def _clone(src: Any) -> Any:
        k = id(src)
        if k in memo:
            return memo[k]
        dup = scalars(src)
        memo[k] = dup
        sj = getattr(src, "__jac__", None)
        if sj is None:
            return dup
        sh = _handle(sj)
        if sh <= 0:
            return dup
        for eh in g.osp_refs_flag(sh, 2, _SV.tag_of_cls(role_base), 1, 0):
            earch = _arch_of(eh)
            child = _arch_of(g.osp_edge_peer(eh, sh))
            if earch is None or child is None:
                continue
            e2 = type(earch)()
            for fld, val in earch.__dict__.items():
                if fld != "__jac__" and fld != "_osp_h":
                    setattr(e2, fld, val)
            one_sided = (g.osp_row_flags(eh) & g.FLAG_ONE_SIDED) != 0
            _link(dup, _clone(child), e2, one_sided, None)
        return dup

    return _clone(nd)


def on_entry(func: Callable) -> Callable:
    setattr(func, "__jac_entry", True)
    return func


def on_exit(func: Callable) -> Callable:
    setattr(func, "__jac_exit", True)
    return func


def set_trigger(trigger_thunk: Callable) -> Callable[[Callable], Callable]:
    def deco(func: Callable) -> Callable:
        setattr(func, "__jac_trigger__", trigger_thunk)
        return func

    return deco
