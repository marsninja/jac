"""JCIR container plumbing for the M3 mod_ty transcription spike.

Three things live here, all of them stand-ins so the spike can run without
the jac toolchain in the loop:

* ``CodegenIrWriter`` / ``read_container`` -- a faithful transliteration of
  ``jac/jaclang/jac0core/codegen_ir.jac`` (magic, varints, zigzag, tables,
  the fifteen opcodes). Byte-compatible with the real writer by
  construction; the format is small enough that this is verifiable by
  reading the two side by side.
* ``emit_from_ast`` -- a fixture producer. The real producer
  (``jcir_gen_pass``) never touches ``ast``; a differential fixture may, and
  deriving the bytes from a parsed tree is what makes the three-way
  comparison in ``test_modty_spike.py`` exact down to the location fields.
* ``shim_transcribe`` / ``code_diffs`` -- the reference shim seat
  (``jac0core/codegen_shim.jac``) reduced to the opcodes the spike emits,
  and the field-by-field code-object comparison that
  ``codegen-ir.md`` section 10 requires instead of comparing marshal bytes.
"""

from __future__ import annotations

import ast
import struct
import sys
import types

CIR_MAGIC = b"JCIR"
CIR_FORMAT_VERSION = 1
CIR_TERMINATOR = 254

OP_NONE = 1
OP_TRUE = 2
OP_FALSE = 3
OP_ELLIPSIS = 4
OP_INT = 5
OP_INT_BIG = 6
OP_FLOAT = 7
OP_STR = 8
OP_BYTES = 9
OP_LIST = 10
OP_TUPLE = 11
OP_NODE = 12
OP_LOC = 13
OP_PARSE_SPLICE = 14
OP_END = 15

_I64_MIN = -(2**63)
_I64_MAX = 2**63 - 1


class CodegenIrError(RuntimeError):
    pass


class CodegenIrVersionError(CodegenIrError):
    pass


class CodegenIrFormatError(CodegenIrError):
    pass


def cir_python_version() -> int:
    return (sys.version_info[0] << 8) | sys.version_info[1]


def _w_varint(buf: bytearray, v: int) -> None:
    if v < 0:
        raise CodegenIrFormatError(f"varint cannot encode negative value {v}")
    while True:
        b = v & 127
        v >>= 7
        if v:
            buf.append(b | 128)
        else:
            buf.append(b)
            return


def _w_svarint(buf: bytearray, v: int) -> None:
    _w_varint(buf, (v << 1) if v >= 0 else (((-v) << 1) - 1))


def _r_varint(data: bytes, pos: int, what: str) -> tuple[int, int]:
    value = 0
    shift = 0
    while True:
        if pos >= len(data):
            raise CodegenIrFormatError(f"truncated container: varint for {what}")
        b = data[pos]
        pos += 1
        value |= (b & 127) << shift
        if not (b & 128):
            return value, pos
        shift += 7


def _w_str_entry(buf: bytearray, s: str) -> None:
    raw = s.encode("utf-8")
    _w_varint(buf, len(raw))
    buf.extend(raw)


def _r_str_entry(data: bytes, pos: int, what: str) -> tuple[str, int]:
    n, pos = _r_varint(data, pos, what)
    if pos + n > len(data):
        raise CodegenIrFormatError(f"truncated container: {what} body")
    return data[pos : pos + n].decode("utf-8"), pos + n


class CodegenIrWriter:
    """Transliteration of ``codegen_ir.jac``'s writer, opcode for opcode."""

    def __init__(self) -> None:
        self.class_names: list[str] = []
        self.class_idx: dict[str, int] = {}
        self.key_names: list[str] = []
        self.key_idx: dict[str, int] = {}
        self.strings: list[str] = []
        self.string_idx: dict[str, int] = {}
        self.modules: list[tuple[str, bytes]] = []
        self._code: bytearray | None = None
        self._mod_path = ""
        self._depth = 0
        self._loc_set = False
        self._loc = (0, 0, 0, 0)

    def class_ref(self, name: str) -> int:
        if name not in self.class_idx:
            self.class_idx[name] = len(self.class_names)
            self.class_names.append(name)
        return self.class_idx[name]

    def key_ref(self, name: str) -> int:
        if name not in self.key_idx:
            self.key_idx[name] = len(self.key_names)
            self.key_names.append(name)
        return self.key_idx[name]

    def str_ref(self, s: str) -> int:
        if s not in self.string_idx:
            self.string_idx[s] = len(self.strings)
            self.strings.append(s)
        return self.string_idx[s]

    def _require_open(self) -> bytearray:
        if self._code is None:
            raise CodegenIrFormatError("no module is open")
        return self._code

    def begin_module(self, mod_path: str) -> None:
        if self._code is not None:
            raise CodegenIrFormatError(f"module {self._mod_path} is still open")
        self._code = bytearray()
        self._mod_path = mod_path
        self._depth = 0
        self._loc_set = False
        self._loc = (0, 0, 0, 0)

    def emit_none(self) -> None:
        self._require_open().append(OP_NONE)
        self._depth += 1

    def emit_true(self) -> None:
        self._require_open().append(OP_TRUE)
        self._depth += 1

    def emit_false(self) -> None:
        self._require_open().append(OP_FALSE)
        self._depth += 1

    def emit_ellipsis(self) -> None:
        self._require_open().append(OP_ELLIPSIS)
        self._depth += 1

    def emit_int(self, v: int) -> None:
        code = self._require_open()
        if _I64_MIN <= v <= _I64_MAX:
            code.append(OP_INT)
            _w_svarint(code, v)
        else:
            code.append(OP_INT_BIG)
            mag = -v if v < 0 else v
            nbytes = (mag.bit_length() + 7) // 8
            code.append(1 if v < 0 else 0)
            _w_varint(code, nbytes)
            code.extend(mag.to_bytes(nbytes, "little"))
        self._depth += 1

    def emit_float(self, v: float) -> None:
        code = self._require_open()
        code.append(OP_FLOAT)
        code.extend(struct.pack("<d", v))
        self._depth += 1

    def emit_str(self, s: str) -> None:
        code = self._require_open()
        code.append(OP_STR)
        _w_varint(code, self.str_ref(s))
        self._depth += 1

    def emit_bytes(self, b: bytes) -> None:
        code = self._require_open()
        code.append(OP_BYTES)
        _w_varint(code, len(b))
        code.extend(b)
        self._depth += 1

    def emit_list(self, n: int) -> None:
        code = self._require_open()
        if n > self._depth:
            raise CodegenIrFormatError(f"emit_list({n}) with depth {self._depth}")
        code.append(OP_LIST)
        _w_varint(code, n)
        self._depth -= n - 1

    def emit_loc(self, first_line: int, col_start: int, last_line: int, col_end: int) -> None:
        code = self._require_open()
        code.append(OP_LOC)
        cur = (first_line, col_start, last_line, col_end)
        for new, old in zip(cur, self._loc):
            _w_svarint(code, new - old)
        self._loc = cur
        self._loc_set = True

    def emit_loc_needed(self, first_line: int, col_start: int, last_line: int, col_end: int) -> None:
        self._require_open()
        if self._loc_set and self._loc == (first_line, col_start, last_line, col_end):
            return
        self.emit_loc(first_line, col_start, last_line, col_end)

    def emit_node(self, class_name: str, field_names: list[str]) -> None:
        code = self._require_open()
        if not self._loc_set:
            raise CodegenIrFormatError(
                f"emit_node({class_name}) before any emit_loc in {self._mod_path}"
            )
        n = len(field_names)
        if n > self._depth:
            raise CodegenIrFormatError(
                f"emit_node({class_name}) pops {n} but depth is {self._depth}"
            )
        code.append(OP_NODE)
        _w_varint(code, self.class_ref(class_name))
        _w_varint(code, n)
        for fname in field_names:
            _w_varint(code, self.key_ref(fname))
        self._depth -= n - 1

    def end_module(self) -> None:
        code = self._require_open()
        if self._depth != 1:
            raise CodegenIrFormatError(
                f"end_module on {self._mod_path} with stack depth {self._depth}"
            )
        code.append(OP_END)
        self.modules.append((self._mod_path, bytes(code)))
        self._code = None
        self._mod_path = ""

    def to_bytes(self) -> bytes:
        if self._code is not None:
            raise CodegenIrFormatError(f"module {self._mod_path} is still open")
        for path, _ in self.modules:
            self.str_ref(path)
        buf = bytearray()
        buf.extend(CIR_MAGIC)
        buf.extend(struct.pack("<H", CIR_FORMAT_VERSION))
        buf.extend(struct.pack("<H", cir_python_version()))
        _w_varint(buf, len(self.class_names))
        for cn in self.class_names:
            _w_str_entry(buf, cn)
        _w_varint(buf, len(self.key_names))
        for kn in self.key_names:
            _w_str_entry(buf, kn)
        _w_varint(buf, len(self.strings))
        for s in self.strings:
            _w_str_entry(buf, s)
        _w_varint(buf, len(self.modules))
        for path, code in self.modules:
            _w_varint(buf, self.str_ref(path))
            _w_varint(buf, len(code))
            buf.extend(code)
        _w_varint(buf, 0)  # no diagnostics in the spike fixtures
        buf.append(CIR_TERMINATOR)
        return bytes(buf)


class CirContainer:
    def __init__(self) -> None:
        self.class_names: list[str] = []
        self.key_names: list[str] = []
        self.strings: list[str] = []
        self.modules: list[tuple[str, bytes]] = []


def read_container(data: bytes) -> CirContainer:
    if len(data) < 8 or data[0:4] != CIR_MAGIC:
        raise CodegenIrFormatError("not a JCIR container: bad magic")
    fmt_ver, py_ver = struct.unpack("<HH", data[4:8])
    if fmt_ver != CIR_FORMAT_VERSION:
        raise CodegenIrVersionError(f"JCIR format version {fmt_ver} is not {CIR_FORMAT_VERSION}")
    if py_ver != cir_python_version():
        want = cir_python_version()
        raise CodegenIrVersionError(
            f"JCIR container targets Python {py_ver >> 8}.{py_ver & 255} but this "
            f"runtime is {want >> 8}.{want & 255}; refusing to read"
        )
    out = CirContainer()
    pos = 8
    for table in ("class_names", "key_names", "strings"):
        n, pos = _r_varint(data, pos, table)
        vals: list[str] = []
        for _ in range(n):
            s, pos = _r_str_entry(data, pos, table)
            vals.append(s)
        setattr(out, table, vals)
    n_mods, pos = _r_varint(data, pos, "module count")
    for _ in range(n_mods):
        path_ref, pos = _r_varint(data, pos, "module path ref")
        code_len, pos = _r_varint(data, pos, "module code length")
        out.modules.append((out.strings[path_ref], data[pos : pos + code_len]))
        pos += code_len
    n_diags, pos = _r_varint(data, pos, "diagnostic count")
    if n_diags:
        raise CodegenIrFormatError("the spike's reader carries no diagnostic records")
    if data[pos] != CIR_TERMINATOR:
        raise CodegenIrFormatError("missing container terminator")
    return out


# --------------------------------------------------------------------------
# Fixture producer: a parsed tree in, JCIR bytes out.
# --------------------------------------------------------------------------

_NO_LOC = (ast.Module, ast.arguments, ast.expr_context, ast.operator, ast.boolop,
           ast.unaryop, ast.cmpop)


def emit_from_ast(w: CodegenIrWriter, tree: ast.Module, mod_path: str) -> None:
    """Emit one module stream, children first, parent last (postfix)."""
    w.begin_module(mod_path)
    # codegen-ir.md 5: the register must be live before the first OP_NODE.
    w.emit_loc(1, 0, 1, 0)
    _emit_node(w, tree)
    w.end_module()


def _emit_value(w: CodegenIrWriter, v: object) -> None:
    if isinstance(v, ast.AST):
        _emit_node(w, v)
    elif isinstance(v, list):
        for item in v:
            _emit_value(w, item)
        w.emit_list(len(v))
    elif v is None:
        w.emit_none()
    elif v is True:
        w.emit_true()
    elif v is False:
        w.emit_false()
    elif v is Ellipsis:
        w.emit_ellipsis()
    elif isinstance(v, str):
        w.emit_str(v)
    elif isinstance(v, bytes):
        w.emit_bytes(v)
    elif isinstance(v, int):
        w.emit_int(v)
    elif isinstance(v, float):
        w.emit_float(v)
    else:
        raise CodegenIrFormatError(f"the spike cannot encode operand {v!r}")


def _emit_node(w: CodegenIrWriter, node: ast.AST) -> None:
    fields = list(node._fields)
    for fname in fields:
        _emit_value(w, getattr(node, fname, None))
    if not isinstance(node, _NO_LOC) and hasattr(node, "lineno"):
        first_line = node.lineno
        col_start = node.col_offset
        last_line = node.end_lineno if (node.end_lineno or 0) > first_line else first_line
        col_end = node.end_col_offset if (node.end_col_offset or 0) > col_start else col_start
        w.emit_loc_needed(first_line, col_start, last_line, col_end)
    w.emit_node(type(node).__name__, fields)


# --------------------------------------------------------------------------
# The reference shim seat, reduced to the opcodes the spike emits.
# --------------------------------------------------------------------------


def _decode(code: bytes, container: CirContainer) -> list[tuple]:
    ops: list[tuple] = []
    pos = 0
    while pos < len(code):
        op = code[pos]
        pos += 1
        if op in (OP_NONE, OP_TRUE, OP_FALSE, OP_ELLIPSIS, OP_END):
            ops.append((op,))
        elif op == OP_INT:
            u, pos = _r_varint(code, pos, "OP_INT")
            ops.append((OP_INT, (u >> 1) if not (u & 1) else -((u + 1) >> 1)))
        elif op == OP_FLOAT:
            ops.append((OP_FLOAT, struct.unpack("<d", code[pos : pos + 8])[0]))
            pos += 8
        elif op == OP_STR:
            r, pos = _r_varint(code, pos, "OP_STR")
            ops.append((OP_STR, container.strings[r]))
        elif op == OP_BYTES:
            n, pos = _r_varint(code, pos, "OP_BYTES")
            ops.append((OP_BYTES, code[pos : pos + n]))
            pos += n
        elif op in (OP_LIST, OP_TUPLE):
            n, pos = _r_varint(code, pos, "aggregate arity")
            ops.append((op, n))
        elif op == OP_NODE:
            cref, pos = _r_varint(code, pos, "class ref")
            nk, pos = _r_varint(code, pos, "field count")
            krefs = []
            for _ in range(nk):
                kr, pos = _r_varint(code, pos, "key ref")
                krefs.append(kr)
            ops.append((OP_NODE, cref, krefs))
        elif op == OP_LOC:
            deltas = []
            for _ in range(4):
                u, pos = _r_varint(code, pos, "OP_LOC")
                deltas.append((u >> 1) if not (u & 1) else -((u + 1) >> 1))
            ops.append((OP_LOC, *deltas))
        else:
            raise CodegenIrFormatError(f"opcode {op} at offset {pos - 1}")
    return ops


def shim_transcribe(ir_bytes: bytes) -> list[tuple[str, ast.Module]]:
    """Build ``ast`` objects, exactly as ``codegen_shim.jac`` does."""
    container = read_container(ir_bytes)
    classes = []
    for name in container.class_names:
        cls = getattr(ast, name, None)
        if cls is None:
            raise CodegenIrVersionError(f"container names ast class {name}")
        classes.append(cls)
    keys = [sys.intern(k) for k in container.key_names]
    out: list[tuple[str, ast.Module]] = []
    for mod_path, code in container.modules:
        stack: list = []
        loc_set = False
        loc = [0, 0, 0, 0]
        for op in _decode(code, container):
            kind = op[0]
            if kind == OP_NODE:
                if not loc_set:
                    raise CodegenIrFormatError(f"OP_NODE before any OP_LOC in {mod_path}")
                cls = classes[op[1]]
                krefs = op[2]
                vals = stack[len(stack) - len(krefs) :]
                del stack[len(stack) - len(krefs) :]
                built = cls(**{keys[kr]: v for kr, v in zip(krefs, vals)})
                built.lineno, built.col_offset, built.end_lineno, built.end_col_offset = loc
                stack.append(built)
            elif kind == OP_LOC:
                loc = [a + b for a, b in zip(loc, op[1:])]
                loc_set = True
            elif kind in (OP_STR, OP_INT, OP_FLOAT, OP_BYTES):
                stack.append(op[1])
            elif kind == OP_NONE:
                stack.append(None)
            elif kind == OP_TRUE:
                stack.append(True)
            elif kind == OP_FALSE:
                stack.append(False)
            elif kind == OP_ELLIPSIS:
                stack.append(Ellipsis)
            elif kind == OP_LIST:
                n = op[1]
                vals = stack[len(stack) - n :]
                del stack[len(stack) - n :]
                stack.append(list(vals))
            elif kind == OP_END:
                if len(stack) != 1 or not isinstance(stack[0], ast.Module):
                    raise CodegenIrFormatError(f"bad module root in {mod_path}")
                out.append((mod_path, stack[0]))
                break
        else:
            raise CodegenIrFormatError(f"module stream for {mod_path} has no OP_END")
    return out


# --------------------------------------------------------------------------
# Field-by-field code object comparison (codegen-ir.md 10: marshal bytes are
# not a stable parity token, because FLAG_REF depends on transient refcounts).
# --------------------------------------------------------------------------

_CODE_FIELDS = (
    "co_name",
    "co_qualname",
    "co_argcount",
    "co_posonlyargcount",
    "co_kwonlyargcount",
    "co_nlocals",
    "co_stacksize",
    "co_flags",
    "co_code",
    "co_names",
    "co_varnames",
    "co_freevars",
    "co_cellvars",
    "co_filename",
    "co_firstlineno",
    "co_linetable",
    "co_exceptiontable",
)


def code_diffs(a: types.CodeType, b: types.CodeType, path: str = "<module>") -> list[str]:
    """Return every field-level difference between two code objects."""
    diffs: list[str] = []
    for f in _CODE_FIELDS:
        av = getattr(a, f, None)
        bv = getattr(b, f, None)
        if av != bv:
            diffs.append(f"{path}.{f}: {av!r} != {bv!r}")
    if len(a.co_consts) != len(b.co_consts):
        diffs.append(f"{path}.co_consts length {len(a.co_consts)} != {len(b.co_consts)}")
        return diffs
    for i, (ac, bc) in enumerate(zip(a.co_consts, b.co_consts)):
        if isinstance(ac, types.CodeType) and isinstance(bc, types.CodeType):
            diffs.extend(code_diffs(ac, bc, f"{path}.co_consts[{i}]"))
        elif type(ac) is not type(bc) or ac != bc:
            diffs.append(f"{path}.co_consts[{i}]: {ac!r} != {bc!r}")
    if list(a.co_positions()) != list(b.co_positions()):
        diffs.append(f"{path}.co_positions() differ")
    return diffs
