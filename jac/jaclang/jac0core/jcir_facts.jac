"""Seal-time-baked name facts for the JCIR emitter.

The emitter needs two name sets while lowering: the runtime builtin
surface (`jaclang.runtimelib.builtin.__all__`) and the ambient typing
names (`compiler/type_system/typing_ambient.pyi`'s `__all__`). Reading
either at emit time means a Python module import or a filesystem text
scan, neither of which a native unit can host, so both sets are baked
here as data. `test_jcir_facts_sync.jac` pins each against its source of
truth, so growing either surface without re-baking fails loudly.
"""

glob JCIR_BUILTIN_NAMES: frozenset = frozenset(
         {
             "APIProtocol",
             "AccessLevel",
             "JacListView",
             "Region",
             "ScheduleTrigger",
             "_jac_chunks",
             "_jac_region_close",
             "_jac_region_instrument",
             "_jac_region_open",
             "_jac_region_register",
             "allroots",
             "archetype_alias",
             "commit",
             "grant",
             "jid",
             "jobj",
             "jref",
             "llm",
             "managed",
             "new",
             "on_commit",
             "printgraph",
             "restspec",
             "revoke",
             "save",
             "schedule",
             "schema_alias",
             "schema_drop",
             "schema_upgrade",
             "schema_was",
             "store",
             "unsafe_html"
         }
     );

glob JCIR_AMBIENT_TYPING_NAMES: frozenset = frozenset(
         {
             "Annotated",
             "AsyncIterable",
             "AsyncIterator",
             "Awaitable",
             "Callable",
             "ClassVar",
             "Coroutine",
             "Generic",
             "Iterable",
             "Iterator",
             "Literal",
             "Mapping",
             "MutableMapping",
             "MutableSequence",
             "Protocol",
             "Sequence",
             "TypeVar"
         }
     );

def jcir_is_test_path(path: str) -> bool {
    return path.endswith(".test.jac");
}
