glob _r_src: list[int] = [],
     _r_tgt: list[int] = [],
     _r_undir: list[int] = [],
     _r_tag: list[int] = [],
     _r_handle: list[int] = [],
     _r_nsrc: list[int] = [],
     _r_ntgt: list[int] = [],
     _r_psrc: list[int] = [],
     _r_ptgt: list[int] = [],
     _r_flags: list[int] = [],
     _r_lo: list[int] = [],
     _r_li: list[int] = [],
     _r_knext: list[int] = [],
     _r_kkey: list[int] = [],
     _row_free: list[int] = [];

glob _nt_cnt: list[int] = [0],
     _nt_key: list[int] = [],
     _nt_ho: list[int] = [],
     _nt_to: list[int] = [],
     _nt_hi: list[int] = [],
     _nt_ti: list[int] = [],
     _nt_rgn: list[int] = [];

glob _rt_cnt: list[int] = [0],
     _rt_key: list[int] = [],
     _rt_head: list[int] = [];

glob _et_cnt: list[int] = [0],
     _et_key: list[int] = [],
     _et_row: list[int] = [];

glob _rk_addr: list[int] = [],
     _rk_next: list[int] = [],
     _rk_free: list[int] = [];

glob _ct_cnt: list[int] = [0],
     _ct_addr2: list[int] = [],
     _ct_tag: list[int] = [],
     _ct_head: list[int] = [],
     _ct_tail: list[int] = [];

glob _cl_row: list[int] = [],
     _cl_next: list[int] = [],
     _cl_prev: list[int] = [],
     _cl_addr2: list[int] = [],
     _cl_tag: list[int] = [],
     _cl_rnext: list[int] = [],
     _cl_free: list[int] = [];

glob _kt_cnt: list[int] = [0],
     _kt_addr: list[int] = [],
     _kt_tag: list[int] = [],
     _kt_key: list[int] = [],
     _kt_head: list[int] = [];

glob _dd_gen: list[int] = [0],
     _dd_cnt: list[int] = [0],
     _dd_key: list[int] = [],
     _dd_stamp: list[int] = [];

glob FLAG_ONE_SIDED: int = 1,
     FLAG_IN_KID: int = 2;

def _hslot(k: int, m: int) -> int {
    return (((k ^ (k >> 4)) & 4294967295) * 40503) & m;
}

def _h2(a: int, b: int) -> int {
    return (((a & 4294967295) * 2654435761) ^ (b * 40503)) & 9007199254740991;
}

def _h3(a: int, b: int, c: int) -> int {
    return _h2(_h2(a, b), c);
}

def _nt_grow {
    ocap = len(_nt_key);
    ok: list[int] = [];
    oho: list[int] = [];
    oto: list[int] = [];
    ohi: list[int] = [];
    oti: list[int] = [];
    orn: list[int] = [];
    live = 0;
    i = 0;
    while i < ocap {
        k = _nt_key[i];
        ok.append(k);
        oho.append(_nt_ho[i]);
        oto.append(_nt_to[i]);
        ohi.append(_nt_hi[i]);
        oti.append(_nt_ti[i]);
        orn.append(_nt_rgn[i]);
        if k != 0 {
            live += 1;
        }
        _nt_key[i] = 0;
        _nt_ho[i] = -1;
        _nt_to[i] = -1;
        _nt_hi[i] = -1;
        _nt_ti[i] = -1;
        _nt_rgn[i] = 0;
        i += 1;
    }
    ncap = ocap * 2;
    if ncap == 0 {
        ncap = 1024;
    }
    i = ocap;
    while i < ncap {
        _nt_key.append(0);
        _nt_ho.append(-1);
        _nt_to.append(-1);
        _nt_hi.append(-1);
        _nt_ti.append(-1);
        _nt_rgn.append(0);
        i += 1;
    }
    m = ncap - 1;
    i = 0;
    while i < ocap {
        k = ok[i];
        if k != 0 {
            h = _hslot(k, m);
            while _nt_key[h] != 0 {
                h = (h + 1) & m;
            }
            _nt_key[h] = k;
            _nt_ho[h] = oho[i];
            _nt_to[h] = oto[i];
            _nt_hi[h] = ohi[i];
            _nt_ti[h] = oti[i];
            _nt_rgn[h] = orn[i];
        }
        i += 1;
    }
    _nt_cnt[0] = live;
}

def _nt_slot(addr: int) -> int {
    if len(_nt_key) == 0 or _nt_cnt[0] * 4 >= len(_nt_key) * 3 {
        _nt_grow();
    }
    m = len(_nt_key) - 1;
    h = _hslot(addr, m);
    while True {
        k = _nt_key[h];
        if k == addr {
            return h;
        }
        if k == 0 {
            _nt_key[h] = addr;
            _nt_rgn[h] = 0;
            _nt_cnt[0] += 1;
            return h;
        }
        h = (h + 1) & m;
    }
    return -1;
}

def _nt_find(addr: int) -> int {
    if len(_nt_key) == 0 {
        return -1;
    }
    m = len(_nt_key) - 1;
    h = _hslot(addr, m);
    while True {
        k = _nt_key[h];
        if k == addr {
            return h;
        }
        if k == 0 {
            return -1;
        }
        h = (h + 1) & m;
    }
    return -1;
}

def _nt_del(s: int) {
    m = len(_nt_key) - 1;
    i = s;
    j = s;
    while True {
        j = (j + 1) & m;
        k = _nt_key[j];
        if k == 0 {
            break;
        }
        h = _hslot(k, m);
        move = False;
        if i <= j {
            if h <= i or h > j {
                move = True;
            }
        } else {
            if h <= i and h > j {
                move = True;
            }
        }
        if move {
            _nt_key[i] = k;
            _nt_ho[i] = _nt_ho[j];
            _nt_to[i] = _nt_to[j];
            _nt_hi[i] = _nt_hi[j];
            _nt_ti[i] = _nt_ti[j];
            _nt_rgn[i] = _nt_rgn[j];
            i = j;
        }
    }
    _nt_key[i] = 0;
    _nt_ho[i] = -1;
    _nt_to[i] = -1;
    _nt_hi[i] = -1;
    _nt_ti[i] = -1;
    _nt_rgn[i] = 0;
    _nt_cnt[0] -= 1;
}

def _rt_grow {
    ocap = len(_rt_key);
    ok: list[int] = [];
    oh: list[int] = [];
    live = 0;
    i = 0;
    while i < ocap {
        k = _rt_key[i];
        ok.append(k);
        oh.append(_rt_head[i]);
        if k != 0 {
            live += 1;
        }
        _rt_key[i] = 0;
        _rt_head[i] = -1;
        i += 1;
    }
    ncap = ocap * 2;
    if ncap == 0 {
        ncap = 256;
    }
    i = ocap;
    while i < ncap {
        _rt_key.append(0);
        _rt_head.append(-1);
        i += 1;
    }
    m = ncap - 1;
    i = 0;
    while i < ocap {
        k = ok[i];
        if k != 0 {
            h = _hslot(k, m);
            while _rt_key[h] != 0 {
                h = (h + 1) & m;
            }
            _rt_key[h] = k;
            _rt_head[h] = oh[i];
        }
        i += 1;
    }
    _rt_cnt[0] = live;
}

def _rt_slot(rgn: int) -> int {
    if len(_rt_key) == 0 or _rt_cnt[0] * 4 >= len(_rt_key) * 3 {
        _rt_grow();
    }
    m = len(_rt_key) - 1;
    h = _hslot(rgn, m);
    while True {
        k = _rt_key[h];
        if k == rgn {
            return h;
        }
        if k == 0 {
            _rt_key[h] = rgn;
            _rt_cnt[0] += 1;
            return h;
        }
        h = (h + 1) & m;
    }
    return -1;
}

def _rt_find(rgn: int) -> int {
    if len(_rt_key) == 0 {
        return -1;
    }
    m = len(_rt_key) - 1;
    h = _hslot(rgn, m);
    while True {
        k = _rt_key[h];
        if k == rgn {
            return h;
        }
        if k == 0 {
            return -1;
        }
        h = (h + 1) & m;
    }
    return -1;
}

def _rt_del(s: int) {
    m = len(_rt_key) - 1;
    i = s;
    j = s;
    while True {
        j = (j + 1) & m;
        k = _rt_key[j];
        if k == 0 {
            break;
        }
        h = _hslot(k, m);
        move = False;
        if i <= j {
            if h <= i or h > j {
                move = True;
            }
        } else {
            if h <= i and h > j {
                move = True;
            }
        }
        if move {
            _rt_key[i] = k;
            _rt_head[i] = _rt_head[j];
            i = j;
        }
    }
    _rt_key[i] = 0;
    _rt_head[i] = -1;
    _rt_cnt[0] -= 1;
}

def _et_grow {
    ocap = len(_et_key);
    ok: list[int] = [];
    orow: list[int] = [];
    live = 0;
    i = 0;
    while i < ocap {
        k = _et_key[i];
        ok.append(k);
        orow.append(_et_row[i]);
        if k != 0 {
            live += 1;
        }
        _et_key[i] = 0;
        _et_row[i] = -1;
        i += 1;
    }
    ncap = ocap * 2;
    if ncap == 0 {
        ncap = 1024;
    }
    i = ocap;
    while i < ncap {
        _et_key.append(0);
        _et_row.append(-1);
        i += 1;
    }
    m = ncap - 1;
    i = 0;
    while i < ocap {
        k = ok[i];
        if k != 0 {
            h = _hslot(k, m);
            while _et_key[h] != 0 {
                h = (h + 1) & m;
            }
            _et_key[h] = k;
            _et_row[h] = orow[i];
        }
        i += 1;
    }
    _et_cnt[0] = live;
}

def _et_put(eh: int, row: int) {
    if len(_et_key) == 0 or _et_cnt[0] * 4 >= len(_et_key) * 3 {
        _et_grow();
    }
    m = len(_et_key) - 1;
    h = _hslot(eh, m);
    while True {
        k = _et_key[h];
        if k == eh {
            _et_row[h] = row;
            return;
        }
        if k == 0 {
            _et_key[h] = eh;
            _et_row[h] = row;
            _et_cnt[0] += 1;
            return;
        }
        h = (h + 1) & m;
    }
}

def _et_find(eh: int) -> int {
    if len(_et_key) == 0 {
        return -1;
    }
    m = len(_et_key) - 1;
    h = _hslot(eh, m);
    while True {
        k = _et_key[h];
        if k == eh {
            return _et_row[h];
        }
        if k == 0 {
            return -1;
        }
        h = (h + 1) & m;
    }
    return -1;
}

def osp_has_edge(eh: int) -> int {
    return 1 if _et_find(eh) >= 0 else 0;
}

def _et_del(eh: int) {
    if len(_et_key) == 0 {
        return;
    }
    m = len(_et_key) - 1;
    h = _hslot(eh, m);
    s = -1;
    while True {
        k = _et_key[h];
        if k == eh {
            s = h;
            break;
        }
        if k == 0 {
            return;
        }
        h = (h + 1) & m;
    }
    i = s;
    j = s;
    while True {
        j = (j + 1) & m;
        k = _et_key[j];
        if k == 0 {
            break;
        }
        hh = _hslot(k, m);
        move = False;
        if i <= j {
            if hh <= i or hh > j {
                move = True;
            }
        } else {
            if hh <= i and hh > j {
                move = True;
            }
        }
        if move {
            _et_key[i] = k;
            _et_row[i] = _et_row[j];
            i = j;
        }
    }
    _et_key[i] = 0;
    _et_row[i] = -1;
    _et_cnt[0] -= 1;
}

def _ct_grow {
    ocap = len(_ct_addr2);
    oa: list[int] = [];
    ot: list[int] = [];
    oh: list[int] = [];
    ol: list[int] = [];
    live = 0;
    i = 0;
    while i < ocap {
        a = _ct_addr2[i];
        oa.append(a);
        ot.append(_ct_tag[i]);
        oh.append(_ct_head[i]);
        ol.append(_ct_tail[i]);
        if a != 0 {
            live += 1;
        }
        _ct_addr2[i] = 0;
        _ct_tag[i] = 0;
        _ct_head[i] = -1;
        _ct_tail[i] = -1;
        i += 1;
    }
    ncap = ocap * 2;
    if ncap == 0 {
        ncap = 1024;
    }
    i = ocap;
    while i < ncap {
        _ct_addr2.append(0);
        _ct_tag.append(0);
        _ct_head.append(-1);
        _ct_tail.append(-1);
        i += 1;
    }
    m = ncap - 1;
    i = 0;
    while i < ocap {
        a = oa[i];
        if a != 0 {
            h = _hslot(_h2(a, ot[i]), m);
            while _ct_addr2[h] != 0 {
                h = (h + 1) & m;
            }
            _ct_addr2[h] = a;
            _ct_tag[h] = ot[i];
            _ct_head[h] = oh[i];
            _ct_tail[h] = ol[i];
        }
        i += 1;
    }
    _ct_cnt[0] = live;
}

def _ct_slot(addr2: int, tag: int) -> int {
    if len(_ct_addr2) == 0 or _ct_cnt[0] * 4 >= len(_ct_addr2) * 3 {
        _ct_grow();
    }
    m = len(_ct_addr2) - 1;
    h = _hslot(_h2(addr2, tag), m);
    while True {
        a = _ct_addr2[h];
        if a == addr2 and _ct_tag[h] == tag {
            return h;
        }
        if a == 0 {
            _ct_addr2[h] = addr2;
            _ct_tag[h] = tag;
            _ct_head[h] = -1;
            _ct_tail[h] = -1;
            _ct_cnt[0] += 1;
            return h;
        }
        h = (h + 1) & m;
    }
    return -1;
}

def _ct_find(addr2: int, tag: int) -> int {
    if len(_ct_addr2) == 0 {
        return -1;
    }
    m = len(_ct_addr2) - 1;
    h = _hslot(_h2(addr2, tag), m);
    while True {
        a = _ct_addr2[h];
        if a == addr2 and _ct_tag[h] == tag {
            return h;
        }
        if a == 0 {
            return -1;
        }
        h = (h + 1) & m;
    }
    return -1;
}

def _ct_del(s: int) {
    m = len(_ct_addr2) - 1;
    i = s;
    j = s;
    while True {
        j = (j + 1) & m;
        a = _ct_addr2[j];
        if a == 0 {
            break;
        }
        h = _hslot(_h2(a, _ct_tag[j]), m);
        move = False;
        if i <= j {
            if h <= i or h > j {
                move = True;
            }
        } else {
            if h <= i and h > j {
                move = True;
            }
        }
        if move {
            _ct_addr2[i] = a;
            _ct_tag[i] = _ct_tag[j];
            _ct_head[i] = _ct_head[j];
            _ct_tail[i] = _ct_tail[j];
            i = j;
        }
    }
    _ct_addr2[i] = 0;
    _ct_tag[i] = 0;
    _ct_head[i] = -1;
    _ct_tail[i] = -1;
    _ct_cnt[0] -= 1;
}

def _kt_grow {
    ocap = len(_kt_addr);
    oa: list[int] = [];
    ot: list[int] = [];
    ok: list[int] = [];
    oh: list[int] = [];
    live = 0;
    i = 0;
    while i < ocap {
        a = _kt_addr[i];
        oa.append(a);
        ot.append(_kt_tag[i]);
        ok.append(_kt_key[i]);
        oh.append(_kt_head[i]);
        if a != 0 {
            live += 1;
        }
        _kt_addr[i] = 0;
        _kt_tag[i] = 0;
        _kt_key[i] = 0;
        _kt_head[i] = -1;
        i += 1;
    }
    ncap = ocap * 2;
    if ncap == 0 {
        ncap = 1024;
    }
    i = ocap;
    while i < ncap {
        _kt_addr.append(0);
        _kt_tag.append(0);
        _kt_key.append(0);
        _kt_head.append(-1);
        i += 1;
    }
    m = ncap - 1;
    i = 0;
    while i < ocap {
        a = oa[i];
        if a != 0 {
            h = _hslot(_h3(a, ot[i], ok[i]), m);
            while _kt_addr[h] != 0 {
                h = (h + 1) & m;
            }
            _kt_addr[h] = a;
            _kt_tag[h] = ot[i];
            _kt_key[h] = ok[i];
            _kt_head[h] = oh[i];
        }
        i += 1;
    }
    _kt_cnt[0] = live;
}

def _kt_slot(addr: int, tag: int, key: int) -> int {
    if len(_kt_addr) == 0 or _kt_cnt[0] * 4 >= len(_kt_addr) * 3 {
        _kt_grow();
    }
    m = len(_kt_addr) - 1;
    h = _hslot(_h3(addr, tag, key), m);
    while True {
        a = _kt_addr[h];
        if a == addr and _kt_tag[h] == tag and _kt_key[h] == key {
            return h;
        }
        if a == 0 {
            _kt_addr[h] = addr;
            _kt_tag[h] = tag;
            _kt_key[h] = key;
            _kt_head[h] = -1;
            _kt_cnt[0] += 1;
            return h;
        }
        h = (h + 1) & m;
    }
    return -1;
}

def _kt_find(addr: int, tag: int, key: int) -> int {
    if len(_kt_addr) == 0 {
        return -1;
    }
    m = len(_kt_addr) - 1;
    h = _hslot(_h3(addr, tag, key), m);
    while True {
        a = _kt_addr[h];
        if a == addr and _kt_tag[h] == tag and _kt_key[h] == key {
            return h;
        }
        if a == 0 {
            return -1;
        }
        h = (h + 1) & m;
    }
    return -1;
}

def _kt_del(s: int) {
    m = len(_kt_addr) - 1;
    i = s;
    j = s;
    while True {
        j = (j + 1) & m;
        a = _kt_addr[j];
        if a == 0 {
            break;
        }
        h = _hslot(_h3(a, _kt_tag[j], _kt_key[j]), m);
        move = False;
        if i <= j {
            if h <= i or h > j {
                move = True;
            }
        } else {
            if h <= i and h > j {
                move = True;
            }
        }
        if move {
            _kt_addr[i] = a;
            _kt_tag[i] = _kt_tag[j];
            _kt_key[i] = _kt_key[j];
            _kt_head[i] = _kt_head[j];
            i = j;
        }
    }
    _kt_addr[i] = 0;
    _kt_tag[i] = 0;
    _kt_key[i] = 0;
    _kt_head[i] = -1;
    _kt_cnt[0] -= 1;
}

def _dd_grow {
    ocap = len(_dd_key);
    gen = _dd_gen[0];
    ok: list[int] = [];
    i = 0;
    while i < ocap {
        if _dd_stamp[i] == gen {
            ok.append(_dd_key[i]);
        }
        _dd_stamp[i] = 0;
        i += 1;
    }
    ncap = ocap * 2;
    if ncap == 0 {
        ncap = 1024;
    }
    i = ocap;
    while i < ncap {
        _dd_key.append(0);
        _dd_stamp.append(0);
        i += 1;
    }
    m = ncap - 1;
    i = 0;
    while i < len(ok) {
        k = ok[i];
        h = _hslot(k, m);
        while _dd_stamp[h] == gen {
            h = (h + 1) & m;
        }
        _dd_key[h] = k;
        _dd_stamp[h] = gen;
        i += 1;
    }
}

def _dd_begin {
    _dd_gen[0] += 1;
    _dd_cnt[0] = 0;
    if len(_dd_key) == 0 {
        _dd_grow();
    }
}

def _dd_add(k: int) -> int {
    if _dd_cnt[0] * 4 >= len(_dd_key) * 3 {
        _dd_grow();
    }
    gen = _dd_gen[0];
    m = len(_dd_key) - 1;
    h = _hslot(k, m);
    while True {
        if _dd_stamp[h] != gen {
            _dd_stamp[h] = gen;
            _dd_key[h] = k;
            _dd_cnt[0] += 1;
            return 1;
        }
        if _dd_key[h] == k {
            return 0;
        }
        h = (h + 1) & m;
    }
    return 0;
}

def _cl_new(row: int, addr2: int, tag: int) -> int {
    l = -1;
    if _cl_free {
        l = _cl_free.pop();
        _cl_row[l] = row;
        _cl_next[l] = -1;
        _cl_prev[l] = -1;
        _cl_addr2[l] = addr2;
        _cl_tag[l] = tag;
        _cl_rnext[l] = -1;
    } else {
        l = len(_cl_row);
        _cl_row.append(row);
        _cl_next.append(-1);
        _cl_prev.append(-1);
        _cl_addr2.append(addr2);
        _cl_tag.append(tag);
        _cl_rnext.append(-1);
    }
    return l;
}

def _cl_release(l: int) {
    _cl_row[l] = -1;
    _cl_addr2[l] = 0;
    _cl_free.append(l);
}

def _chain_append(l: int) {
    s = _ct_slot(_cl_addr2[l], _cl_tag[l]);
    t = _ct_tail[s];
    if t >= 0 {
        _cl_next[t] = l;
        _cl_prev[l] = t;
    } else {
        _ct_head[s] = l;
    }
    _ct_tail[s] = l;
}

def _chain_insert_before(l: int, before_l: int) {
    s = _ct_slot(_cl_addr2[l], _cl_tag[l]);
    p = _cl_prev[before_l];
    _cl_prev[l] = p;
    _cl_next[l] = before_l;
    _cl_prev[before_l] = l;
    if p >= 0 {
        _cl_next[p] = l;
    } else {
        _ct_head[s] = l;
    }
}

def _chain_unlink(l: int) {
    s = _ct_find(_cl_addr2[l], _cl_tag[l]);
    if s < 0 {
        return;
    }
    p = _cl_prev[l];
    n = _cl_next[l];
    if p >= 0 {
        _cl_next[p] = n;
    } else {
        _ct_head[s] = n;
    }
    if n >= 0 {
        _cl_prev[n] = p;
    } else {
        _ct_tail[s] = p;
    }
    if _ct_head[s] < 0 {
        _ct_del(s);
    }
}

def _row_link_of(row: int, addr2: int, tag: int) -> int {
    l = _r_lo[row] if (addr2 & 1) == 0 else _r_li[row];
    while l >= 0 {
        if _cl_addr2[l] == addr2 and _cl_tag[l] == tag {
            return l;
        }
        l = _cl_rnext[l];
    }
    return -1;
}

def _row_alloc(
    src: int, tgt: int, undirected: int, edge_tag: int, eobj: int, flags: int
) -> int {
    r = -1;
    if _row_free {
        r = _row_free.pop();
        _r_src[r] = src;
        _r_tgt[r] = tgt;
        _r_undir[r] = undirected;
        _r_tag[r] = edge_tag;
        _r_handle[r] = eobj;
        _r_nsrc[r] = -1;
        _r_ntgt[r] = -1;
        _r_psrc[r] = -1;
        _r_ptgt[r] = -1;
        _r_flags[r] = flags;
        _r_lo[r] = -1;
        _r_li[r] = -1;
        _r_knext[r] = -1;
        _r_kkey[r] = 0;
    } else {
        r = len(_r_src);
        _r_src.append(src);
        _r_tgt.append(tgt);
        _r_undir.append(undirected);
        _r_tag.append(edge_tag);
        _r_handle.append(eobj);
        _r_nsrc.append(-1);
        _r_ntgt.append(-1);
        _r_psrc.append(-1);
        _r_ptgt.append(-1);
        _r_flags.append(flags);
        _r_lo.append(-1);
        _r_li.append(-1);
        _r_knext.append(-1);
        _r_kkey.append(0);
    }
    return r;
}

def _out_append(ss: int, r: int) {
    t = _nt_to[ss];
    if t >= 0 {
        _r_nsrc[t] = r;
        _r_psrc[r] = t;
    } else {
        _nt_ho[ss] = r;
    }
    _nt_to[ss] = r;
}

def _out_insert_before(ss: int, r: int, before_row: int) {
    p = _r_psrc[before_row];
    _r_psrc[r] = p;
    _r_nsrc[r] = before_row;
    _r_psrc[before_row] = r;
    if p >= 0 {
        _r_nsrc[p] = r;
    } else {
        _nt_ho[ss] = r;
    }
}

def _in_append(ts: int, r: int) {
    t = _nt_ti[ts];
    if t >= 0 {
        _r_ntgt[t] = r;
        _r_ptgt[r] = t;
    } else {
        _nt_hi[ts] = r;
    }
    _nt_ti[ts] = r;
}

def _unlink_out(src: int, row: int) {
    s = _nt_find(src);
    if s < 0 {
        return;
    }
    p = _r_psrc[row];
    n = _r_nsrc[row];
    if p >= 0 {
        _r_nsrc[p] = n;
    } else {
        if _nt_ho[s] == row {
            _nt_ho[s] = n;
        }
    }
    if n >= 0 {
        _r_psrc[n] = p;
    } else {
        if _nt_to[s] == row {
            _nt_to[s] = p;
        }
    }
    _r_psrc[row] = -1;
    _r_nsrc[row] = -1;
}

def _unlink_in(tgt: int, row: int) {
    s = _nt_find(tgt);
    if s < 0 {
        return;
    }
    p = _r_ptgt[row];
    n = _r_ntgt[row];
    if p >= 0 {
        _r_ntgt[p] = n;
    } else {
        if _nt_hi[s] == row {
            _nt_hi[s] = n;
        }
    }
    if n >= 0 {
        _r_ptgt[n] = p;
    } else {
        if _nt_ti[s] == row {
            _nt_ti[s] = p;
        }
    }
    _r_ptgt[row] = -1;
    _r_ntgt[row] = -1;
}

def _key_remove(row: int) {
    key = _r_kkey[row];
    if key == 0 {
        return;
    }
    s = _kt_find(_r_src[row], _r_tag[row], key);
    if s >= 0 {
        prev = -1;
        r = _kt_head[s];
        while r >= 0 {
            if r == row {
                if prev < 0 {
                    _kt_head[s] = _r_knext[r];
                } else {
                    _r_knext[prev] = _r_knext[r];
                }
                break;
            }
            prev = r;
            r = _r_knext[r];
        }
        if _kt_head[s] < 0 {
            _kt_del(s);
        }
    }
    _r_knext[row] = -1;
    _r_kkey[row] = 0;
}

def _row_retire(row: int) {
    l = _r_lo[row];
    while l >= 0 {
        nx = _cl_rnext[l];
        _chain_unlink(l);
        _cl_release(l);
        l = nx;
    }
    _r_lo[row] = -1;
    l = _r_li[row];
    while l >= 0 {
        nx = _cl_rnext[l];
        _chain_unlink(l);
        _cl_release(l);
        l = nx;
    }
    _r_li[row] = -1;
    _key_remove(row);
    _unlink_out(_r_src[row], row);
    if (_r_flags[row] & FLAG_ONE_SIDED) == 0 {
        _unlink_in(_r_tgt[row], row);
    }
    _et_del(_r_handle[row]);
    _r_src[row] = -1;
    _r_handle[row] = 0;
    _r_flags[row] = 0;
    _row_free.append(row);
}

def _rk_reg(addr: int, rgn: int) {
    s = _rt_slot(rgn);
    e = -1;
    if _rk_free {
        e = _rk_free.pop();
        _rk_addr[e] = addr;
        _rk_next[e] = _rt_head[s];
    } else {
        e = len(_rk_addr);
        _rk_addr.append(addr);
        _rk_next.append(_rt_head[s]);
    }
    _rt_head[s] = e;
}

def _typed_anchor_link(src: int, before_row: int, tag: int) -> int {
    x = before_row;
    addr2 = src * 2;
    while x >= 0 {
        l = _row_link_of(x, addr2, tag);
        if l >= 0 {
            return l;
        }
        x = _r_nsrc[x];
    }
    return -1;
}

def osp_conn2(
    src: int,
    tgt: int,
    eobj: int,
    undirected: int,
    tags: list[int],
    flags: int,
    rgn_src: int,
    rgn_tgt: int,
    before_eobj: int
) -> int {
    one_sided = (flags & FLAG_ONE_SIDED) != 0;
    ss = _nt_slot(src);
    ts = -1;
    if not one_sided {
        ts = _nt_slot(tgt);
        ss = _nt_find(src);
    }
    if rgn_src != 0 and _nt_rgn[ss] == 0 {
        if ts >= 0 and ts != ss and _nt_rgn[ts] != 0 {
            rgn_src = _nt_rgn[ts];
        }
        _nt_rgn[ss] = rgn_src;
        _rk_reg(src, rgn_src);
    }
    if rgn_tgt != 0 and ts >= 0 and ts != ss and _nt_rgn[ts] == 0 {
        if _nt_rgn[ss] != 0 {
            rgn_tgt = _nt_rgn[ss];
        }
        _nt_rgn[ts] = rgn_tgt;
        _rk_reg(tgt, rgn_tgt);
    }
    edge_tag = tags[0] if len(tags) > 0 else -1;
    r = _row_alloc(src, tgt, undirected, edge_tag, eobj, flags);
    before_row = -1;
    if before_eobj > 0 {
        before_row = _et_find(before_eobj);
        if before_row >= 0 and _r_src[before_row] != src {
            before_row = -1;
        }
    }
    if before_row >= 0 {
        _out_insert_before(ss, r, before_row);
    } else {
        _out_append(ss, r);
    }
    if not one_sided {
        _in_append(ts, r);
    }
    i = 0;
    n = len(tags);
    while i < n {
        tag = tags[i];
        i += 1;
        l = _cl_new(r, src * 2, tag);
        _cl_rnext[l] = _r_lo[r];
        _r_lo[r] = l;
        bl = -1;
        if before_row >= 0 {
            bl = _typed_anchor_link(src, before_row, tag);
        }
        if bl >= 0 {
            _chain_insert_before(l, bl);
        } else {
            _chain_append(l);
        }
        if not one_sided {
            l2 = _cl_new(r, tgt * 2 + 1, tag);
            _cl_rnext[l2] = _r_li[r];
            _r_li[r] = l2;
            _chain_append(l2);
        }
    }
    _et_put(eobj, r);
    return tgt;
}

def osp_conn(
    src: int,
    tgt: int,
    eobj: int,
    undirected: int,
    edge_tag: int,
    rgn_src: int,
    rgn_tgt: int
) -> int {
    tags: list[int] = [];
    tags.append(edge_tag);
    return osp_conn2(src, tgt, eobj, undirected, tags, 0, rgn_src, rgn_tgt, 0);
}

def _refs_untyped(
    origin: int, dir: int, edges_only: int, need_flags: int, out: list[int]
) {
    s = _nt_find(origin);
    if s < 0 {
        return;
    }
    r = _nt_ho[s];
    while r >= 0 {
        eff = dir;
        if _r_undir[r] != 0 {
            eff = 3;
        }
        if (eff == 2 or eff == 3) and (_r_flags[r] & need_flags) == need_flags {
            o = _r_handle[r] if edges_only != 0 else _r_tgt[r];
            if _dd_add(o) != 0 {
                out.append(o);
            }
        }
        r = _r_nsrc[r];
    }
    r = _nt_hi[s];
    while r >= 0 {
        eff = dir;
        if _r_undir[r] != 0 {
            eff = 3;
        }
        if (eff == 1 or eff == 3) and (_r_flags[r] & need_flags) == need_flags {
            o = _r_handle[r] if edges_only != 0 else _r_src[r];
            if _dd_add(o) != 0 {
                out.append(o);
            }
        }
        r = _r_ntgt[r];
    }
}

def _refs_typed(
    origin: int,
    dir: int,
    edge_tag: int,
    edges_only: int,
    need_flags: int,
    out: list[int]
) {
    s = _ct_find(origin * 2, edge_tag);
    if s >= 0 {
        l = _ct_head[s];
        while l >= 0 {
            r = _cl_row[l];
            eff = dir;
            if _r_undir[r] != 0 {
                eff = 3;
            }
            if (eff == 2 or eff == 3) and (_r_flags[r] & need_flags) == need_flags {
                o = _r_handle[r] if edges_only != 0 else _r_tgt[r];
                if _dd_add(o) != 0 {
                    out.append(o);
                }
            }
            l = _cl_next[l];
        }
    }
    s = _ct_find(origin * 2 + 1, edge_tag);
    if s >= 0 {
        l = _ct_head[s];
        while l >= 0 {
            r = _cl_row[l];
            eff = dir;
            if _r_undir[r] != 0 {
                eff = 3;
            }
            if (eff == 1 or eff == 3) and (_r_flags[r] & need_flags) == need_flags {
                o = _r_handle[r] if edges_only != 0 else _r_src[r];
                if _dd_add(o) != 0 {
                    out.append(o);
                }
            }
            l = _cl_next[l];
        }
    }
}

def osp_refs_flag(
    origin: int, dir: int, edge_tag: int, edges_only: int, need_flags: int
) -> list[int] {
    out: list[int] = [];
    _dd_begin();
    if edge_tag < 0 {
        _refs_untyped(origin, dir, edges_only, need_flags, out);
    } else {
        _refs_typed(origin, dir, edge_tag, edges_only, need_flags, out);
    }
    return out;
}

def osp_refs(origin: int, dir: int, edge_tag: int, edges_only: int) -> list[int] {
    return osp_refs_flag(origin, dir, edge_tag, edges_only, 0);
}

def osp_refs_many(
    origins: list[int], dir: int, edge_tag: int, edges_only: int
) -> list[int] {
    out: list[int] = [];
    _dd_begin();
    i = 0;
    n = len(origins);
    while i < n {
        if edge_tag < 0 {
            _refs_untyped(origins[i], dir, edges_only, 0, out);
        } else {
            _refs_typed(origins[i], dir, edge_tag, edges_only, 0, out);
        }
        i = i + 1;
    }
    return out;
}

def osp_wrap1(x: int) -> list[int] {
    out: list[int] = [];
    out.append(x);
    return out;
}

def osp_degree(addr: int, dir: int) -> int {
    s = _nt_find(addr);
    if s < 0 {
        return 0;
    }
    n = 0;
    if dir == 2 or dir == 3 {
        r = _nt_ho[s];
        while r >= 0 {
            n += 1;
            r = _r_nsrc[r];
        }
    }
    if dir == 1 or dir == 3 {
        r = _nt_hi[s];
        while r >= 0 {
            n += 1;
            r = _r_ntgt[r];
        }
    }
    return n;
}

def osp_edge_peer(edge_handle: int, origin: int) -> int {
    r = _et_find(edge_handle);
    if r < 0 or _r_src[r] == -1 {
        return -1;
    }
    if _r_src[r] == origin {
        return _r_tgt[r];
    }
    return _r_src[r];
}

def osp_edge_src(edge_handle: int) -> int {
    r = _et_find(edge_handle);
    if r < 0 or _r_src[r] == -1 {
        return -1;
    }
    return _r_src[r];
}

def osp_edge_tgt(edge_handle: int) -> int {
    r = _et_find(edge_handle);
    if r < 0 or _r_src[r] == -1 {
        return -1;
    }
    return _r_tgt[r];
}

def osp_edge_tag(edge_handle: int) -> int {
    r = _et_find(edge_handle);
    if r < 0 or _r_src[r] == -1 {
        return -1;
    }
    return _r_tag[r];
}

def osp_edge_undir(edge_handle: int) -> int {
    r = _et_find(edge_handle);
    if r < 0 or _r_src[r] == -1 {
        return 0;
    }
    return _r_undir[r];
}

def osp_row_flags(edge_handle: int) -> int {
    r = _et_find(edge_handle);
    if r < 0 or _r_src[r] == -1 {
        return 0;
    }
    return _r_flags[r];
}

def osp_set_row_flags(edge_handle: int, flags: int) -> int {
    r = _et_find(edge_handle);
    if r < 0 or _r_src[r] == -1 {
        return 0;
    }
    keep = _r_flags[r] & FLAG_ONE_SIDED;
    _r_flags[r] = (flags & (~FLAG_ONE_SIDED)) | keep;
    return 1;
}

def osp_key_put(edge_handle: int, key: int) -> int {
    r = _et_find(edge_handle);
    if r < 0 or _r_src[r] == -1 {
        return 0;
    }
    if _r_kkey[r] != 0 {
        _key_remove(r);
    }
    if key == 0 {
        return 1;
    }
    s = _kt_slot(_r_src[r], _r_tag[r], key);
    _r_knext[r] = _kt_head[s];
    _kt_head[s] = r;
    _r_kkey[r] = key;
    return 1;
}

def osp_key_rows(addr: int, edge_tag: int, key: int) -> list[int] {
    out: list[int] = [];
    s = _kt_find(addr, edge_tag, key);
    if s < 0 {
        return out;
    }
    r = _kt_head[s];
    while r >= 0 {
        out.append(_r_handle[r]);
        r = _r_knext[r];
    }
    out.reverse();
    return out;
}

def osp_edge_key(edge_handle: int) -> int {
    r = _et_find(edge_handle);
    if r < 0 or _r_src[r] == -1 {
        return 0;
    }
    return _r_kkey[r];
}

def osp_disconn_between(src: int, tgt: int, dir: int, edge_tag: int) -> int {
    s = _nt_find(src);
    if s < 0 {
        return 0;
    }
    hits: list[int] = [];
    _dd_begin();
    r = _nt_ho[s];
    while r >= 0 {
        eff = dir;
        if _r_undir[r] != 0 {
            eff = 3;
        }
        if (eff == 2 or eff == 3)
            and _r_tgt[r] == tgt
            and (edge_tag < 0 or _r_tag[r] == edge_tag) {
            if _dd_add(r + 1) != 0 {
                hits.append(r);
            }
        }
        r = _r_nsrc[r];
    }
    r = _nt_hi[s];
    while r >= 0 {
        eff = dir;
        if _r_undir[r] != 0 {
            eff = 3;
        }
        if (eff == 1 or eff == 3)
            and _r_src[r] == tgt
            and (edge_tag < 0 or _r_tag[r] == edge_tag) {
            if _dd_add(r + 1) != 0 {
                hits.append(r);
            }
        }
        r = _r_ntgt[r];
    }
    i = 0;
    n = len(hits);
    while i < n {
        _row_retire(hits[i]);
        i = i + 1;
    }
    return n;
}

def osp_disconnect(edge_handle: int) -> int {
    r = _et_find(edge_handle);
    if r < 0 or _r_src[r] == -1 {
        return 0;
    }
    _row_retire(r);
    return 1;
}

def osp_region_handles(rgn: int) -> list[int] {
    out: list[int] = [];
    s = _rt_find(rgn);
    if s < 0 {
        return out;
    }
    e = _rt_head[s];
    while e >= 0 {
        out.append(_rk_addr[e]);
        e = _rk_next[e];
    }
    return out;
}

def osp_region_edges(rgn: int) -> list[int] {
    out: list[int] = [];
    s = _rt_find(rgn);
    if s < 0 {
        return out;
    }
    _dd_begin();
    e = _rt_head[s];
    while e >= 0 {
        ns = _nt_find(_rk_addr[e]);
        if ns >= 0 {
            r = _nt_ho[ns];
            while r >= 0 {
                if _dd_add(_r_handle[r]) != 0 {
                    out.append(_r_handle[r]);
                }
                r = _r_nsrc[r];
            }
            r = _nt_hi[ns];
            while r >= 0 {
                if _dd_add(_r_handle[r]) != 0 {
                    out.append(_r_handle[r]);
                }
                r = _r_ntgt[r];
            }
        }
        e = _rk_next[e];
    }
    return out;
}

def osp_region_died(rgn: int) -> int {
    n = 0;
    s = _rt_find(rgn);
    if s < 0 {
        return 0;
    }
    addrs: list[int] = [];
    e = _rt_head[s];
    while e >= 0 {
        addrs.append(_rk_addr[e]);
        nx2 = _rk_next[e];
        _rk_free.append(e);
        e = nx2;
    }
    _rt_del(s);
    i = 0;
    while i < len(addrs) {
        ns = _nt_find(addrs[i]);
        if ns >= 0 {
            rows: list[int] = [];
            r = _nt_ho[ns];
            while r >= 0 {
                rows.append(r);
                r = _r_nsrc[r];
            }
            r = _nt_hi[ns];
            while r >= 0 {
                rows.append(r);
                r = _r_ntgt[r];
            }
            j = 0;
            while j < len(rows) {
                if _r_src[rows[j]] != -1 {
                    _row_retire(rows[j]);
                    n += 1;
                }
                j += 1;
            }
            ns = _nt_find(addrs[i]);
            if ns >= 0 {
                _nt_del(ns);
            }
        }
        i += 1;
    }
    return n;
}
