"""The kernel's adjacency is indexed by (node, direction, edge type) and keeps
insertion order (K1 of the substrate clean break on #8748).

A typed hop walks only the rows of that type (and, through MRO tags, its
subclasses); a reverse hop reads the in-chain, never the out-chain; a
one-sided row is reachable from its source and invisible from its target;
insert-before keeps both the untyped and the typed chains in source order;
keyed rows answer (node, type, key) directly; region death retires every row
of every node registered in the region."""

import time;
import jaclang.runtime.osp_graph as _g;
import jaclang.runtime.osp_graph_sv as _sv;
import from jaclang.runtime.osp_graph_sv { sv_conn, sv_live_edges, sv_refs_handles }

node TNode {
    has a: int = 0;
}

edge Role {
    has in_kid: bool = True;
}

edge BodyRole(Role) {}

edge NameRole(Role) {}

edge InScope {}

edge Fan {}

def _anchor(n: TNode) -> any {
    return n.__jac__;
}

def _conn(src: TNode, tgt: TNode, e: any, before: any = None) -> any {
    import from jaclang.runtime.archetype { EdgeAnchor }
    ea = EdgeAnchor(
        archetype=e, source=_anchor(src), target=_anchor(tgt), is_undirected=False
    );
    e.__jac__ = ea;
    sv_conn(_anchor(src), _anchor(tgt), ea, False, None, before);
    return ea;
}

def _ends(origin: TNode, dir_int: int, et: any) -> list[int] {
    out: list[int] = [];
    for a in sv_live_edges(_anchor(origin), dir_int, et, False) {
        arch: any = a.archetype;
        out.append(arch.a);
    }
    return out;
}

test "typed hop walks only that type and its subclasses, in insertion order" {
    p = TNode(a=0);
    kids = [TNode(a=i) for i in range(1, 6)];
    _conn(p, kids[0], NameRole());
    _conn(p, kids[1], BodyRole());
    _conn(p, kids[2], InScope());
    _conn(p, kids[3], BodyRole());
    _conn(p, kids[4], BodyRole(in_kid=False));
    assert _ends(p, 2, Role) == [1, 2, 4, 5];
    assert _ends(p, 2, BodyRole) == [2, 4, 5];
    assert _ends(p, 2, NameRole) == [1];
    assert _ends(p, 2, InScope) == [3];
    assert _ends(p, 2, None) == [1, 2, 3, 4, 5];
    hs = sv_refs_handles(_anchor(p), 2, Role, False, _g.FLAG_IN_KID);
    assert _handles_a(hs) == [1, 2, 4];
}

def _handles_a(hs: list[int]) -> list[int] {
    out: list[int] = [];
    for h in hs {
        a: any = _sv.obj_of(h);
        out.append(a.archetype.a);
    }
    return out;
}

test "reverse hop reads the in-chain only" {
    p = TNode(a=0);
    c = TNode(a=1);
    _conn(p, c, BodyRole());
    i = 0;
    while i < 500 {
        _conn(p, TNode(a=100 + i), InScope());
        i += 1;
    }
    assert _ends(c, 1, Role) == [0];
    assert _ends(c, 1, None) == [0];
    assert _ends(p, 1, Role) == [];
    assert _g.osp_degree(_sv.handle_of(_anchor(p)), 2) == 501;
}

test "insert-before keeps the typed and untyped chains in source order" {
    p = TNode(a=0);
    a = TNode(a=1);
    b = TNode(a=2);
    c = TNode(a=3);
    _conn(p, a, BodyRole());
    eb = _conn(p, b, InScope());
    _conn(p, c, BodyRole());
    d = TNode(a=4);
    _conn(p, d, BodyRole(), eb);
    assert _ends(p, 2, None) == [1, 4, 2, 3];
    assert _ends(p, 2, BodyRole) == [1, 4, 3];
    assert _ends(p, 2, Role) == [1, 4, 3];
}

test "one-sided rows are reachable from the source only" {
    n = TNode(a=0);
    t = TNode(a=9);
    e = Fan();
    e.__dict__['__jac_one_sided__'] = True;
    _conn(n, t, e);
    assert _ends(n, 2, Fan) == [9];
    assert _ends(t, 1, Fan) == [];
    assert _ends(t, 1, None) == [];
    assert _g.osp_degree(_sv.handle_of(_anchor(t)), 3) == 0;
}

test "disconnect retires the row from every chain" {
    p = TNode(a=0);
    a = TNode(a=1);
    b = TNode(a=2);
    ea = _conn(p, a, BodyRole());
    _conn(p, b, BodyRole());
    _g.osp_disconnect(_sv.handle_of(ea));
    assert _ends(p, 2, BodyRole) == [2];
    assert _ends(p, 2, Role) == [2];
    assert _ends(p, 2, None) == [2];
    assert _ends(a, 1, None) == [];
}

test "keyed rows answer (node, type, key) directly and keep overloads" {
    p = TNode(a=0);
    a = TNode(a=1);
    b = TNode(a=2);
    c = TNode(a=3);
    ea = _conn(p, a, InScope());
    eb = _conn(p, b, InScope());
    ec = _conn(p, c, InScope());
    _sv.sv_key_put(ea, "foo");
    _sv.sv_key_put(eb, "foo");
    _sv.sv_key_put(ec, "bar");
    assert _keyed(p, "foo") == [1, 2];
    assert _keyed(p, "bar") == [3];
    assert _keyed(p, "nope") == [];
    _g.osp_disconnect(_sv.handle_of(ea));
    assert _keyed(p, "foo") == [2];
}

def _keyed(p: TNode, key: str) -> list[int] {
    out: list[int] = [];
    for e in _sv.sv_key_edges(_anchor(p), InScope, key) {
        tgt: any = e.target.archetype;
        out.append(tgt.a);
    }
    return out;
}

test "a reverse hop on a high-degree node costs what a low-degree one costs" {
    small_p = TNode(a=0);
    small_c = TNode(a=1);
    _conn(small_p, small_c, BodyRole());
    big_p = TNode(a=0);
    big_c = TNode(a=1);
    _conn(big_p, big_c, BodyRole());
    i = 0;
    while i < 4000 {
        _conn(big_p, TNode(a=100 + i), InScope());
        i += 1;
    }
    reps = 3000;
    t0 = time.perf_counter();
    j = 0;
    while j < reps {
        sv_live_edges(_anchor(small_c), 1, Role, False);
        j += 1;
    }
    t_small = time.perf_counter() - t0;
    t0 = time.perf_counter();
    j = 0;
    while j < reps {
        sv_live_edges(_anchor(big_c), 1, Role, False);
        j += 1;
    }
    t_big = time.perf_counter() - t0;
    ratio = t_big / t_small if t_small > 0.0 else 1.0;
    assert ratio < 4.0 , f"parent hop scaled with sibling degree: ratio {ratio:.1f}";
    t0 = time.perf_counter();
    j = 0;
    while j < reps {
        sv_live_edges(_anchor(big_p), 2, BodyRole, False);
        j += 1;
    }
    t_typed = time.perf_counter() - t0;
    ratio2 = t_typed / t_small if t_small > 0.0 else 1.0;
    assert ratio2 < 4.0 , f"typed hop scanned other types: ratio {ratio2:.1f}";
}
