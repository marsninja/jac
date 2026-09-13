"""The Python distribution must support Jac's bootstrap and native runtime."""
import bz2
import ctypes
import decimal
import hashlib
import lzma
import multiprocessing
import platform
from pathlib import Path
import sqlite3
import shlex
import ssl
import sys
import sysconfig
import subprocess
import tempfile
import venv
import xml.parsers.expat
import zlib
from compression import zstd

assert sys.version_info[:3] == (3, 14, 6), sys.version
# Jac executes its compiler on this interpreter. Keep the release runtime's
# optimization contract explicit so a source-build change cannot silently
# ship the slower development interpreter.
assert sysconfig.get_config_var("Py_TAIL_CALL_INTERP") == 1, "Tail-call interpreter is required"
if sys.platform != "darwin":  # The pinned Zig Mach-O linker does not support LTO.
    assert "-flto" in sysconfig.get_config_var("PY_CFLAGS_NODIST"), "Release Python requires LTO"
# The last optimization option wins; CPython's default -O3 can be followed by
# dependency CFLAGS that silently downgrade it to -O2.
optimization_flags = [
    flag for flag in shlex.split(sysconfig.get_config_var("PY_CFLAGS"))
    if flag.startswith("-O")
]
assert optimization_flags[-1:] == ["-O3"], optimization_flags
if sys.platform == "darwin":
    import _scproxy
sample = b"Jac source-built runtime" * 100
for codec in (bz2, lzma, zlib, zstd):
    assert codec.decompress(codec.compress(sample)) == sample
assert sqlite3.connect(":memory:").execute("select 6 * 7").fetchone() == (42,)
assert str(decimal.Decimal("0.1") + decimal.Decimal("0.2")) == "0.3"
assert hashlib.sha256(sample).digest()
assert ctypes.pythonapi.PyInitConfig_Create
mode = sys.argv[1] if len(sys.argv) > 1 else "jacpython"
assert mode in ("jacpython", "host"), mode
if mode == "jacpython":
    assert ctypes.pythonapi._PyJac_CompilerBridgeVersion() == 3
try:
    required_compiler = ctypes.pythonapi._PyJac_CompilerRequired
except AttributeError:
    required_compiler = None  # Only the build host retains the C compiler.
if mode == "jacpython":
    assert required_compiler is not None, "JacPython was requested but is missing"
elif mode == "host":
    assert required_compiler is None, "Unexpected JacPython runtime"
    assert getattr(sys, "_jacpython_compile", None) is None
    assert not hasattr(sys, "_jacpython_image"), "Unexpected embedded seed"
    assert not hasattr(ctypes.pythonapi, "_PyJac_CompilerBridgeVersion")
if required_compiler is not None:
    import _bisect
    import _heapq
    import _random
    import binascii
    import _operator
    import _queue
    import _json
    for replacement in (_bisect, _heapq, _random, binascii, _operator, _queue, _json):
        assert replacement.__name__ in sys.builtin_module_names
        assert replacement.__spec__.origin == "built-in"
    assert ctypes.pythonapi.jacpy_json_encode
    assert ctypes.pythonapi.jacpy_json_scan
    import json
    document = {"unicode": "\U0001f642\ud800", "nested": [None, True, 2 ** 100, 1.25]}
    assert json.loads(json.dumps(document, indent=2, sort_keys=True)) == document
    assert json.loads('{"a":1,"a":2}', object_pairs_hook=tuple) == (("a", 1), ("a", 2))
    try:
        json.loads("[" * 100_000 + "0" + "]" * 100_000)
    except RecursionError:
        pass
    else:
        raise AssertionError("Native JSON did not guard recursive parsing")
    assert ctypes.pythonapi.jacpy_queue_get
    import threading
    fifo = _queue.SimpleQueue()
    consumed = []
    def consume_native_queue():
        while True:
            item = fifo.get(timeout=5)
            if item is None:
                return
            consumed.append(item)
    consumers = [threading.Thread(target=consume_native_queue, daemon=True) for _ in range(8)]
    for consumer in consumers:
        consumer.start()
    for item in range(1000):
        fifo.put(item)
    for consumer in consumers:
        fifo.put(None)
    for consumer in consumers:
        consumer.join(10)
        assert not consumer.is_alive(), "Native queue lost a notification"
    assert sorted(consumed) == list(range(1000))
    assert fifo.empty()
    assert ctypes.pythonapi.jacpy_operator_apply
    assert _operator.itemgetter(2, 0)(["a", "b", "c"]) == ("c", "a")
    assert _operator.methodcaller("replace", "a", "b")("native") == "nbtive"
    assert _operator._compare_digest(b"native", b"native") is True
    assert _operator._compare_digest(b"native", b"Native") is False
    repr_events = []
    class MethodName(str):
        def __repr__(self):
            repr_events.append("name")
            return super().__repr__()
    class Argument:
        def __repr__(self):
            repr_events.append("argument")
            return "argument"
    repr(_operator.methodcaller(MethodName("method"), Argument(), key=Argument()))
    assert repr_events == ["argument", "argument", "name"]
    recursive_items = []
    recursive_getter = _operator.itemgetter(recursive_items)
    recursive_items.append(recursive_getter)
    assert repr(recursive_getter) == "operator.itemgetter([operator.itemgetter(...)])"
    assert ctypes.pythonapi.jacpy_binascii_convert
    assert binascii.a2b_base64(binascii.b2a_base64(b"native codec")) == b"native codec"
    assert binascii.crc32(b"123456789") == 0xcbf43926
    # Private Jac helpers must not interpose on zlib's distinct C ABI.
    assert zlib.crc32(sample) == binascii.crc32(sample)
    buffer = bytearray(b"data")
    class ResizeDuringArgumentConversion:
        def __bool__(self):
            buffer.clear()
            return False
    try:
        binascii.b2a_base64(buffer, newline=ResizeDuringArgumentConversion())
    except BufferError:
        pass
    else:
        raise AssertionError("Native codecs must preserve exported buffers")
    buffer.clear()  # Failed argument conversion must release its export.
    assert ctypes.pythonapi.jacpy_bisect
    assert ctypes.pythonapi.jacpy_random_bits
    generator = _random.Random(42)
    assert generator.random() == 0.6394267984578837
    state = generator.getstate()
    bits = generator.getrandbits(130)
    generator.setstate(state)
    assert generator.getrandbits(130) == bits
    assert ctypes.pythonapi.jacpy_heapify
    assert _bisect.bisect_right([1, 2, 2, 4], 2) == 3
    import builtins
    original_value_error = builtins.ValueError
    try:
        builtins.ValueError = RuntimeError
        try:
            _bisect.bisect_left([], 0, lo=-1)
        except original_value_error:
            pass
        else:
            raise AssertionError("Native exceptions must ignore rebound builtins")
    finally:
        builtins.ValueError = original_value_error
    heap = [4, 1, 3, 2]
    _heapq.heapify(heap)
    assert [_heapq.heappop(heap) for _ in range(4)] == [1, 2, 3, 4]
    heap = [1, 4, 2, 3]
    _heapq.heapify_max(heap)
    assert [_heapq.heappop_max(heap) for _ in range(4)] == [4, 3, 2, 1]
    assert required_compiler() == 1
    assert ctypes.pythonapi._PyJac_CompilerBridgeVersion() == 3
    for retired in ("_jacpython_compile", "_jacpython_symtable", "_jacpython_tokenize", "_jacpython_image", "_jacpython_code"):
        assert not hasattr(sys, retired), retired
    assert not any(name.startswith("_jacpython_seed") for name in sys.modules)
    import io
    import symtable
    import tokenize
    import ast
    # A false comparison argument still evaluates later arguments and calls
    # the function. Exercise eval, assignment, and statement contexts.
    for comparison_value, expected in ((0, False), (2, True), (4, False)):
        for expression in ("capture(1 < value < 3, later())",
                           "capture(later(), 1 < value < 3, flag=later())",
                           "capture(1 < value < 3, 0 < value < 5)"):
            for template in ("{}", "result = {}", "{}\nresult = seen[-1]"):
                events = []
                seen = []
                def later():
                    events.append("later")
                    return 42
                def capture(*args, **kwargs):
                    seen.append((args, kwargs))
                    return seen[-1]
                scope = dict(value=comparison_value, capture=capture, later=later, seen=seen)
                source = template.format(expression)
                if template == "{}":
                    actual = eval(compile(source, "<compare-arguments>", "eval"), scope)
                else:
                    exec(compile(source, "<compare-arguments>", "exec"), scope)
                    actual = scope["result"]
                expected_args = ((expected, 42), {})
                if "flag=" in expression:
                    expected_args = ((42, expected), {"flag": 42})
                elif "0 < value" in expression:
                    expected_args = ((expected, 0 < comparison_value < 5), {})
                assert actual == expected_args and len(seen) == 1
                assert len(events) == expression.count("later()")
        assert eval("(1 < value < 3) == expected", dict(value=comparison_value, expected=expected))
    match_source = "def match_alias(value):\n match value:\n  case str() as text: return text\n  case _: return None\n"
    match_tree = ast.parse(match_source)
    assert isinstance(match_tree.body[0].body[0].cases[0].pattern.pattern, ast.MatchClass)
    for match_input in (match_source, match_tree):
        match_scope = {}
        exec(compile(match_input, "<match-alias>", "exec"), match_scope)
        assert match_scope["match_alias"]("retained") == "retained"
        assert match_scope["match_alias"](42) is None
    try:
        compile("def invalid_match(value):\n match value:\n  case captured: return captured\n  case _: return None\n", "<nested-diagnostic>", "exec")
    except SyntaxError:
        pass
    else:
        raise AssertionError("Nested codegen diagnostic was discarded")
    assert symtable.symtable("x=1", "<smoke>", "exec").lookup("x").is_global()
    assert list(tokenize.generate_tokens(io.StringIO("x=1\n").readline))
    for source in ('f"{value:{width}}"', 't"{value:{width}}"'):
        tokens = [item[:2] for item in tokenize.generate_tokens(io.StringIO(source).readline)]
        rebuilt = tokenize.untokenize(tokens)
        assert [item[:2] for item in tokenize.generate_tokens(io.StringIO(rebuilt).readline)] == tokens
    for prefix in ("r", "R", "rb", "br", "rB", "Rb", "bR", "Br", "RB", "BR"):
        for quote in ("'", '"'):
            for count in (1, 2):
                body = ("\\" + quote) * count
                source = prefix + quote * 3 + body + quote * 3
                expected = body.encode() if "b" in prefix.lower() else body
                assert eval(source) == expected, source
    namespace = {}
    exec(compile("""
def checked(ok, values):
    assert ok, f"{[item * 2 for item in values]}"
    return 42
scalar = lambda: "not a name"
sequence = lambda: ("not a name",)
member = lambda value: value in {("not a name",)}
def set_global():
    global declared, declared
    declared = 42
def outer():
    value = 0
    def inner():
        nonlocal value, value
        value = 42
    inner()
    return value
""", "<compiler-regressions>", "exec", optimize=0), namespace)
    assert namespace["checked"](True, None) == 42
    try:
        namespace["checked"](False, [1, 2])
    except AssertionError as error:
        assert str(error) == "[2, 4]", error
    else:
        raise AssertionError("Assertion message control flow was bypassed")
    assert namespace["member"](("not a name",))
    assert not namespace["member"]("not a name")
    frozen = next(c for c in namespace["member"].__code__.co_consts if isinstance(c, frozenset))
    sequence = next(c for c in namespace["sequence"].__code__.co_consts if isinstance(c, tuple))
    assert next(iter(frozen)) is sequence
    assert sequence[0] is namespace["scalar"]()
    namespace["set_global"]()
    assert namespace["declared"] == namespace["outer"]() == 42
    constants = {}
    # Inserting deferred module annotations must reindex pooled constants,
    # including a folded list which shares the call's keyword-name tuple.
    calls = []
    annotation_scope = {"record": lambda *args, **kwargs: calls.append((args, kwargs)), "flag": False}
    exec(compile("""
items: list[str] = ['first', 'second', 'third']
record(None)
record(first=None, second=None, third=None)
if flag:
    record('unreachable')
""", "<annotation-constant-pool>", "exec"), annotation_scope)
    assert annotation_scope["items"] == ["first", "second", "third"]
    assert calls == [((None,), {}), ((), {"first": None, "second": None, "third": None})]
    exec(compile("nul = '\\x00tail'\nempty = ''\nprefix = '\\x00'\n", "<nul-constants>", "exec"), constants)
    assert (constants["nul"], constants["empty"], constants["prefix"]) == (chr(0) + "tail", "", chr(0))
    for optimize in (0, 1, 2):
        constants = {}
        exec(compile("""
if True:
    truth = 1
else:
    truth = 2
if False:
    falsehood = 1
else:
    falsehood = 2
if __debug__:
    debug = True
else:
    debug = False
""", "<constant-conditions>", "exec", optimize=optimize), constants)
        assert (constants["truth"], constants["falsehood"], constants["debug"]) == (1, 2, optimize == 0)
    import ast
    tree = ast.parse("class Located:\n    value = 42\n")
    tree.body[0].lineno = 10
    tree.body[0].end_lineno = 11
    located_code = compile(tree, "<separate-class-body>", "exec")
    exec(located_code, namespace)
    assert namespace["Located"].value == 42
    assert all(end is None or end >= start for start, end, _, _ in located_code.co_positions())

    # Runtime callbacks cannot redirect the shipped native compiler.
    def unavailable(*args, **kwargs):
        raise AssertionError("Native JacPython called a retired Python adapter")

    for retired in ("_jacpython_compile", "_jacpython_symtable", "_jacpython_tokenize"):
        setattr(sys, retired, unavailable)
    try:
        assert eval("6 * 7") == 42
        assert symtable.symtable("x=1", "<native>", "exec").lookup("x").is_global()
        assert list(tokenize.generate_tokens(io.StringIO("x=1\n").readline))
    finally:
        for retired in ("_jacpython_compile", "_jacpython_symtable", "_jacpython_tokenize"):
            delattr(sys, retired)
    with tempfile.TemporaryDirectory(prefix="jac-python-cold-") as cache:
        for optimization in ([], ["-O"], ["-OO"]):
            subprocess.run(
                [sys.executable, "-I", "-S", "-B", "-X", "pycache_prefix=" + cache]
                + optimization + ["-c", "import ast, ctypes, encodings, sys; "
                                  "ok = eval('6 * 7') == 42 and isinstance(ast.parse('x=1'), ast.Module); "
                                  "ok = ok and encodings.search_function.__code__.co_filename == encodings.__file__; "
                                  "ok = ok and not hasattr(sys, '_jacpython_compile'); "
                                  "sys.exit(0 if ok and ctypes.pythonapi._PyJac_CompilerBridgeVersion() == 3 else 1)"],
                check=True,
            )
    interactive = subprocess.run(
        [sys.executable, "-I", "-q", "-i"],
        input="def twice(value):\n    return value * 2\n\nprint('INTERACTIVE', twice(21))\n"
              "from __future__ import annotations\ndef typed(x: Missing):\n    return x\n\n"
              "print(typed.__annotations__)\n",
        text=True, capture_output=True, check=True,
    )
    assert "INTERACTIVE 42" in interactive.stdout, interactive
    assert "{'x': 'Missing'}" in interactive.stdout, interactive
    assert "Traceback" not in interactive.stderr, interactive.stderr
callback = ctypes.CFUNCTYPE(ctypes.c_int, ctypes.c_int)(lambda value: value + 1)
assert callback(41) == 42
assert sysconfig.get_config_var("Py_ENABLE_SHARED") == 1
assert sysconfig.get_config_var("CC") == "cc"
# configure needs Misc/platform_triplet.c to produce wheel-compatible names.
# An empty platform silently builds a runtime that cannot import tagged wheels.
abi_platform = "darwin" if sys.platform == "darwin" else f"{platform.machine()}-linux-gnu"
assert sysconfig.get_config_var("SOABI") == f"cpython-314-{abi_platform}"
ca = Path(sys.executable).resolve().parents[2] / "build" / "cacert.pem"
assert ssl.create_default_context(cafile=str(ca)).cert_store_stats()["x509_ca"] > 0
for library in ("ssl", "crypto", "sqlite3", "mpdec", "lzma", "bz2", "expat", "z", "zstd", "ffi"):
    archive = ca.parent / "lib" / f"lib{library}.a"
    assert archive.is_file() and archive.stat().st_size > 8, archive
xml.parsers.expat.ParserCreate().Parse(b"<jac/>", True)
with tempfile.TemporaryDirectory(prefix="jac-python-venv-") as directory:
    try:
        venv.EnvBuilder(with_pip=True).create(directory)
    except subprocess.CalledProcessError as error:
        print(error.output.decode(errors="replace") if error.output else str(error), file=sys.stderr)
        raise
    subprocess.run(
        [str(Path(directory) / "bin/python"), "-I", "-c", "import ssl, sqlite3, pip; assert 6 * 7 == 42"],
        check=True,
    )
print(f"CPython {sys.version.split()[0]}: runtime module checks passed ({ssl.OPENSSL_VERSION})")
