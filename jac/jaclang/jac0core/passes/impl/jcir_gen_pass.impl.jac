@classmethod
impl JcirGenPass._get_builtin_names(cls: any) -> set[str] {
    if (cls._builtin_names is None) {
        try {
            import jaclang.runtimelib.builtin as builtin_mod;
            cls._builtin_names = set(builtin_mod.__all__);
        } except (ImportError, AttributeError) {
            return set();
        }
    }
    return cls._builtin_names;
}

@classmethod
impl JcirGenPass._get_ambient_typing_names(cls: any) -> set[str] {
    if (cls._ambient_typing_names is not None) {
        return cls._ambient_typing_names;
    }
    import os;
    stub_path = os.path.join(
        os.path.dirname(os.path.dirname(os.path.dirname(__file__))),
        "compiler",
        "type_system",
        "typing_ambient.pyi"
    );
    names: set[str] = set();
    try {
        with open(stub_path, encoding="utf-8") as f {
            text = f.read();
        }
        marker = text.find("__all__");
        open_b = text.find("[", marker) if marker >= 0 else -1;
        close_b = text.find("]", open_b) if open_b >= 0 else -1;
        if (open_b >= 0 and close_b > open_b) {
            for piece in text[open_b + 1:close_b].split(",") {
                entry = piece.strip();
                if (len(entry) >= 2 and entry[0] in "'\"" and entry[-1] == entry[0]) {
                    names.add(entry[1:-1]);
                }
            }
        }
    } except OSError {
        names = set();
    }
    cls._ambient_typing_names = names;
    return names;
}

impl JcirGenPass.postinit -> None {
    self.jaclib_imports = set();
    self.builtin_imports = set();
    self.typing_imports = set();
    self._hoist_frames = [[]];
    self._sink = {};
    self.preamble = [
        self.nod(
            'ImportFrom',
            ['module', 'names', 'level'],
            [
                '__future__',
                [
                    self.nod(
                        'alias',
                        ['name', 'asname'],
                        ['annotations', None],
                        jac_node=self.ir_in
                    )
                ],
                0
            ],
            jac_node=self.ir_in
        )
    ];
    super.postinit();
}

impl JcirGenPass._is_native_ext_module -> bool {
    return self.ir_in is not None and self.ir_in.decided_codespace == 'native';
}

impl JcirGenPass._refuse(nd: (uni.UniNode | None), construct: str) -> None {
    target = nd if nd is not None else self.cur_node;
    where = '';
    if (target is not None and target.loc is not None) {
        where = (
            f" at {target.loc.mod_path}:"
            f"{target.loc.first_line}:{target.loc.col_start}"
        );
    }
    raise NotImplementedError(
        f"JCIR emitter phase 1 does not lower {construct}{where}; "
        "this construct is outside the phase 1 scope"
    );
}

impl JcirGenPass.enter_node(nd: uni.UniNode) -> None {
    if (isinstance(nd, uni.Module) and (nd is not self.ir_in)) {
        self.prune();
        return;
    }
    if (
        isinstance(nd, uni.ContextAwareNode)
        and (nd.code_context == CodeContext.NATIVE)
        and ((nd.parent is None) or isinstance(nd.parent, uni.Module))
        and not isinstance(nd, (uni.Import, uni.Test))
        and not self._is_native_ext_module()
    ) {
        self._refuse(nd, 'native codespace interop stub generation');
    }
    if (
        isinstance(nd, uni.ContextAwareNode)
        and (nd.code_context == CodeContext.CLIENT)
        and not nd.codespace_dual
        and ((nd.parent is None) or isinstance(nd.parent, uni.Module))
    ) {
        self.prune();
        return;
    }
    if (
        isinstance(nd, uni.Import)
        and nd.code_context == CodeContext.NATIVE
        and (not nd.items)
    ) {
        self.prune();
        return;
    }
    super.enter_node(nd);
}

impl JcirGenPass.exit_node(nd: uni.UniNode) -> None {
    if (isinstance(nd, uni.Module) and (nd is not self.ir_in)) {
        return;
    }
    if (
        isinstance(nd, uni.ContextAwareNode)
        and (nd.code_context == CodeContext.CLIENT)
        and not nd.codespace_dual
        and ((nd.parent is None) or isinstance(nd.parent, uni.Module))
    ) {
        return;
    }
    if (
        isinstance(nd, uni.Import)
        and nd.code_context == CodeContext.NATIVE
        and (not nd.items)
    ) {
        return;
    }
    super.exit_node(nd);
}

impl JcirGenPass.nod(
    cls_name: str, keys: list, vals: list, jac_node: (uni.UniNode | None) = None
) -> CgNode {
    src = jac_node if jac_node is not None else self.cur_node;
    loc = src.loc;
    l2 = loc.last_line
        if (loc.last_line and (loc.last_line > loc.first_line))
        else loc.first_line;
    c2 = loc.col_end
        if (loc.col_end and (loc.col_end > loc.col_start))
        else loc.col_start;
    return CgNode(
        cls=cls_name,
        keys=keys,
        vals=vals,
        l1=loc.first_line,
        c1=loc.col_start,
        l2=l2,
        c2=c2,
        jac_src=src
    );
}

impl JcirGenPass.nod_loc(
    cls_name: str, keys: list, vals: list, l1: int, c1: int, l2: int, c2: int
) -> CgNode {
    return CgNode(cls=cls_name, keys=keys, vals=vals, l1=l1, c1=c1, l2=l2, c2=c2);
}

impl JcirGenPass.outs(nd: uni.UniNode) -> list {
    got = self._sink.get(nd);
    return got if got is not None else [];
}

impl JcirGenPass.set_out(nd: uni.UniNode, items: list) -> None {
    self._sink[nd] = items;
}

impl JcirGenPass.val(nd: uni.UniNode) -> CgNode {
    got = self._sink.get(nd);
    if not got {
        loc = nd.loc;
        raise self.ice(
            f"Empty JCIR recipe on {type(nd).__name__} "
            f"at {loc.mod_path}:{loc.first_line}:{loc.col_start}"
        );
    }
    return got[0];
}

impl JcirGenPass.const_of(value: any, jac_node: (uni.UniNode | None) = None) -> CgNode {
    return self.nod('Constant', ['value'], [value], jac_node=jac_node);
}

impl JcirGenPass.name_load(
    name_id: str, jac_node: (uni.UniNode | None) = None
) -> CgNode {
    return self.nod(
        'Name',
        ['id', 'ctx'],
        [name_id, self.nod('Load', [], [], jac_node=jac_node)],
        jac_node=jac_node
    );
}

impl JcirGenPass._with_ctx(cgn: CgNode, ctx_cls: str) -> CgNode {
    twin = CgNode(
        cls=cgn.cls,
        keys=list(cgn.keys),
        vals=list(cgn.vals),
        l1=cgn.l1,
        c1=cgn.c1,
        l2=cgn.l2,
        c2=cgn.c2,
        jac_src=cgn.jac_src
    );
    twin.setf('ctx', self.nod_loc(ctx_cls, [], [], cgn.l1, cgn.c1, cgn.l2, cgn.c2));
    return twin;
}

impl JcirGenPass.jaclib_obj(obj_name: str) -> CgNode {
    self.jaclib_imports.add(obj_name);
    return self.name_load(obj_name);
}

impl JcirGenPass.builtin_name(name: str) -> CgNode {
    if (name not in ['Enum', 'IntEnum', 'StrEnum']) {
        self.builtin_imports.add(name);
    }
    return self.name_load(name);
}

impl JcirGenPass._add_preamble_once(key: str, cgn: CgNode) -> None {
    if (key in self.already_added) {
        return;
    }
    self.preamble.append(cgn);
    self.already_added.append(key);
}

impl JcirGenPass.needs_enum_kind(kind: str) -> None {
    self._add_preamble_once(
        f"needs_enum_kind:{kind}",
        self.nod(
            'ImportFrom',
            ['module', 'names', 'level'],
            [
                'enum',
                [
                    self.nod('alias', ['name', 'asname'], [kind, None]),
                    self.nod('alias', ['name', 'asname'], ['auto', None])
                ],
                0
            ],
            jac_node=self.ir_out
        )
    );
}

impl JcirGenPass._py_name(nd: uni.AstSymbolNode) -> str {
    if (isinstance(nd, uni.SpecialVarRef) and (nd.name == Tok.KW_INIT)) {
        return '__init__';
    }
    if (isinstance(nd, uni.Name) and (nd.name == Tok.KW_INIT)) {
        return '__init__';
    }
    return nd.sym_name;
}

impl JcirGenPass.py_ctx(nd: uni.AstSymbolNode) -> CgNode {
    if (nd.expr_ctx == uni.ExprCtx.STORE) {
        return self.nod('Store', [], []);
    }
    if (nd.expr_ctx == uni.ExprCtx.DEL) {
        return self.nod('Del', [], []);
    }
    return self.nod('Load', [], []);
}

impl JcirGenPass._next_lambda_name -> str {
    self._lambda_counter += 1;
    return f"_jac_lambda_{self._lambda_counter}";
}

impl JcirGenPass._push_hoist_frame -> None {
    self._hoist_frames.append([]);
}

impl JcirGenPass._hoist_lambda(func_def: CgNode) -> None {
    if not self._hoist_frames {
        self._hoist_frames = [[]];
    }
    self._hoist_frames[-1].append(func_def);
}

impl JcirGenPass._ancestor_is_one_of(
    nd: (uni.UniNode | None), roots: list[uni.UniNode]
) -> bool {
    cur: (uni.UniNode | None) = nd;
    seen: set[uni.UniNode] = set();
    while cur {
        if cur in seen {
            return False;
        }
        seen.add(cur);
        for cand_root in roots {
            if cur is cand_root {
                return True;
            }
        }
        cur = cur.parent;
    }
    return False;
}

impl JcirGenPass._split_scope_hoists(
    hoists: list, sig_roots: list[uni.UniNode]
) -> tuple[(list, list)] {
    body_hoists: list = [];
    enclosing_hoists: list = [];
    for func_def in hoists {
        lam = func_def.jac_src;
        if (lam is not None and self._ancestor_is_one_of(lam, sig_roots)) {
            enclosing_hoists.append(func_def);
        } else {
            body_hoists.append(func_def);
        }
    }
    return (body_hoists, enclosing_hoists);
}

impl JcirGenPass._drain_module_hoists -> list {
    if not self._hoist_frames {
        self._hoist_frames = [[]];
    }
    if len(self._hoist_frames) > 1 {
        self._report_leaked_hoisted_lambdas();
    }
    hoists = self._hoist_frames[0][:];
    self._hoist_frames = [[]];
    return hoists;
}

impl JcirGenPass._report_leaked_hoisted_lambdas -> None {
    leftover: list = [];
    for frame in self._hoist_frames[1:] {
        leftover.extend(frame);
    }
    for func_def in leftover {
        jac_node: uni.UniNode = self.ir_out;
        if func_def.jac_src is not None {
            jac_node = func_def.jac_src;
        }
        self.emit(E5098, node_override=jac_node, name=func_def.get('name'));
    }
}

impl JcirGenPass._function_expr_from_def(
    func_def: CgNode, jac_node: uni.UniNode
) -> CgNode {
    self._hoist_lambda(func_def);
    return self.nod(
        'Name',
        ['id', 'ctx'],
        [func_def.get('name'), self.nod('Load', [], [], jac_node=jac_node)],
        jac_node=jac_node
    );
}

impl JcirGenPass._check_sem_decorator(nd: uni.UniNode) -> None {
    semstr: str = '';
    inner_semstr: dict = {};
    if isinstance(nd, uni.Archetype) {
        semstr = nd.semstr;
        arch_ast_body: list = [];
        if isinstance(nd.body, list) {
            arch_ast_body = nd.body;
        } elif (isinstance(nd.body, uni.ImplDef) and isinstance(nd.body.body, list)) {
            arch_ast_body = nd.body.body;
        }
        for stmt in arch_ast_body {
            if isinstance(stmt, uni.ArchHas) {
                for has_var in stmt.vars {
                    if has_var.semstr {
                        inner_semstr[has_var.sym_name] = has_var.semstr;
                    }
                }
            }
        }
    } elif isinstance(nd, uni.Enum) {
        semstr = nd.semstr;
        enum_ast_body: list = [];
        if isinstance(nd.body, list) {
            enum_ast_body = nd.body;
        } elif (isinstance(nd.body, uni.ImplDef) and isinstance(nd.body.body, list)) {
            enum_ast_body = nd.body.body;
        }
        for stmt in enum_ast_body {
            if (
                isinstance(stmt, uni.Assignment)
                and isinstance(stmt.target[0], uni.AstSymbolNode)
            ) {
                inner_semstr[stmt.target[0].sym_name] = stmt.target[0].semstr;
            }
        }
    } elif isinstance(nd, uni.Ability) {
        semstr = nd.semstr;
        if isinstance(nd.signature, uni.FuncSignature) {
            for param in nd.signature.params {
                if param.semstr {
                    inner_semstr[param.sym_name] = param.semstr;
                }
            }
        }
    }
    if (
        not semstr and (not inner_semstr or all(not v for v in inner_semstr.values()))
    ) {
        return;
    }
    self._refuse(nd, 'sem-string decorator emission');
}

impl JcirGenPass.resolve_switch_pattern(
    pattern: uni.MatchPattern, target: uni.Expr
) -> CgNode {
    if isinstance(pattern, uni.MatchValue) {
        return self.nod(
            'Compare',
            ['left', 'ops', 'comparators'],
            [self.val(target), [self.nod('Eq', [], [])], [self.val(pattern.value)]]
        );
    } elif isinstance(pattern, uni.MatchOr) {
        return self.nod(
            'BoolOp',
            ['op', 'values'],
            [
                self.nod('Or', [], []),
                [self.resolve_switch_pattern(pat, target) for pat in pattern.patterns]
            ]
        );
    } elif isinstance(pattern, uni.MatchSingleton) {
        return self.nod(
            'Compare',
            ['left', 'ops', 'comparators'],
            [self.val(target), [self.nod('Is', [], [])], [self.val(pattern.value)]]
        );
    } else {
        raise self.ice('Unsupported switch pattern type');
    }
}

impl JcirGenPass.resolve_switch_stmt(nd: uni.SwitchStmt) -> list {
    var_executed = self.nod(
        'Name', ['id', 'ctx'], ['__executed', self.nod('Store', [], [])]
    );
    assign_var = self.nod(
        'Assign', ['targets', 'value'], [[var_executed], self.const_of(False)]
    );
    ast_nodes: list = [];
    executed_assign = self.nod(
        'Assign', ['targets', 'value'], [[var_executed], self.const_of(True)]
    );
    for cs in nd.cases {
        if (cs.pattern is None) {
            ast_nodes.extend((self.resolve_stmt_block(cs.body) + [executed_assign]));
        } else {
            test_expr = self.nod(
                'BoolOp',
                ['op', 'values'],
                [
                    self.nod('Or', [], []),
                    [
                        self.resolve_switch_pattern(cs.pattern, nd.target),
                        self.name_load('__executed')
                    ]
                ]
            );
            if_stmt = self.nod(
                'If',
                ['test', 'body', 'orelse'],
                [test_expr, (self.resolve_stmt_block(cs.body) + [executed_assign]), []]
            );
            ast_nodes.append(if_stmt);
        }
    }
    while_cond = self.nod(
        'UnaryOp',
        ['op', 'operand'],
        [self.nod('Not', [], []), self.name_load('__executed')]
    );
    return [
        assign_var,
        self.nod('While', ['test', 'body', 'orelse'], [while_cond, ast_nodes, []])
    ];
}

impl JcirGenPass.resolve_stmt_block(
    nd: Sequence[uni.CodeBlockStmt] | Sequence[uni.EnumBlockStmt] | None,
    doc: (uni.String | None) = None
) -> list {
    items = list(nd) if nd else [];
    valid_stmts = [
        i
        for i in items
        if not isinstance(i, uni.Semi)
    ];
    ret: list = [self.nod('Pass', [], [])]
        if (isinstance(nd, Sequence) and not valid_stmts)
        else self._flatten_ast_list(
            [
                self.outs(x)
                    if not isinstance(x, uni.SwitchStmt)
                    else self.resolve_switch_stmt(x)
                for x in valid_stmts
                if not isinstance(x, uni.ImplDef)
            ]
        )
            if (nd is not None)
            else [self.nod('Pass', [], [])];
    if doc {
        ret = [self.nod('Expr', ['value'], [self.val(doc)], jac_node=doc), *ret];
    }
    return ret;
}

impl JcirGenPass.scope_directive_stmts(scopes: list) -> list {
    global_names: list = [];
    nonlocal_names: list = [];
    seen: set = set();
    for scope in scopes {
        if scope is None {
            continue;
        }
        for name in scope.implicit_rebinds {
            if name in seen {
                continue;
            }
            seen.add(name);
            sym = scope.implicit_rebinds[name];
            if isinstance(sym.parent_tab, uni.Module) {
                global_names.append(name);
            } else {
                nonlocal_names.append(name);
            }
        }
    }
    stmts: list = [];
    if global_names {
        stmts.append(self.nod('Global', ['names'], [sorted(global_names)]));
    }
    if nonlocal_names {
        stmts.append(self.nod('Nonlocal', ['names'], [sorted(nonlocal_names)]));
    }
    return stmts;
}

impl JcirGenPass.ir_bytes -> bytes {
    if self._module_root is None {
        raise self.ice('JCIR emitter produced no module root');
    }
    return module_container_bytes(self._module_path, self._module_root);
}

impl JcirGenPass.exit_sub_tag(nd: uni.SubTag[uni.T]) -> None {
    self.set_out(nd, self.outs(nd.tag));
}

impl JcirGenPass.exit_module(nd: uni.Module) -> None {
    is_builtin_module = (
        nd.loc.mod_path.endswith(('builtin.jac', 'builtin.py'))
        and ('runtimelib' in nd.loc.mod_path)
    );
    if (self.builtin_imports and not is_builtin_module) {
        self.preamble.append(
            self.nod(
                'ImportFrom',
                ['module', 'names', 'level'],
                [
                    'jaclang.runtimelib.builtin',
                    [
                        self.nod(
                            'alias',
                            ['name', 'asname'],
                            [name, None],
                            jac_node=self.ir_out
                        ) for name in sorted(self.builtin_imports)
                    ],
                    0
                ],
                jac_node=self.ir_out
            )
        );
    }
    if self.typing_imports {
        self.preamble.append(
            self.nod(
                'ImportFrom',
                ['module', 'names', 'level'],
                [
                    'typing',
                    [
                        self.nod(
                            'alias',
                            ['name', 'asname'],
                            [name, None],
                            jac_node=self.ir_out
                        ) for name in sorted(self.typing_imports)
                    ],
                    0
                ],
                jac_node=self.ir_out
            )
        );
    }
    mod_path_norm = nd.loc.mod_path.replace('\\', '/');
    is_jaclib_module = mod_path_norm.endswith(('/jaclang/lib.jac', '/jaclang/lib.py'));
    if (self.jaclib_imports and not is_jaclib_module) {
        self.preamble.append(
            self.nod(
                'ImportFrom',
                ['module', 'names', 'level'],
                [
                    'jaclang.jac0core.jaclib',
                    [
                        self.nod(
                            'alias',
                            ['name', 'asname'],
                            [name, None],
                            jac_node=self.ir_out
                        ) for name in sorted(self.jaclib_imports)
                    ],
                    0
                ],
                jac_node=self.ir_out
            )
        );
    }
    manifest = self.ir_in.gen.interop_manifest;
    if (manifest and manifest.bindings and manifest.native_imports) {
        self._refuse(nd, 'native interop import registration map');
    }
    merged_body = self._merge_module_bodies(nd);
    body_items: list = [];
    if nd.doc {
        body_items.append(
            self.nod('Expr', ['value'], [self.val(nd.doc)], jac_node=nd.doc)
        );
    }
    body_items.extend(self.preamble);
    body_items.extend(self._drain_module_hoists());
    for item in merged_body {
        body_items.extend(self.outs(item));
    }
    new_body = self._flatten_ast_list(body_items);
    module = self.nod('Module', ['body', 'type_ignores'], [new_body, []]);
    self.set_out(nd, [module]);
    self._module_root = module;
    self._module_path = nd.loc.mod_path;
}

impl JcirGenPass.exit_type_param(nd: uni.TypeParam) -> None {
    bound_ast = self.val(nd.bound) if nd.bound else None;
    self.set_out(
        nd, [self.nod('TypeVar', ['name', 'bound'], [nd.name.value, bound_ast])]
    );
}

impl JcirGenPass.exit_type_alias(nd: uni.TypeAlias) -> None {
    type_param_asts: list = [];
    for tp in nd.type_params {
        if self.outs(tp) {
            type_param_asts.append(self.val(tp));
        }
    }
    value_ast = self.val(nd.value);
    if nd.is_distinct {
        assign = self.nod(
            'Assign',
            ['targets', 'value'],
            [
                [
                    self.nod(
                        'Name',
                        ['id', 'ctx'],
                        [nd.name.sym_name, self.nod('Store', [], [])]
                    )
                ],
                value_ast
            ]
        );
        self.set_out(nd, [assign]);
        return;
    }
    type_alias = self.nod(
        'TypeAlias',
        ['name', 'type_params', 'value'],
        [
            self.nod(
                'Name', ['id', 'ctx'], [nd.name.sym_name, self.nod('Store', [], [])]
            ),
            type_param_asts,
            value_ast
        ]
    );
    self.set_out(nd, [type_alias]);
}

impl JcirGenPass.exit_global_vars(nd: uni.GlobalVars) -> None {
    assigns_ast = self._flatten_ast_list([self.outs(a) for a in nd.assignments]);
    if nd.doc {
        doc = self.nod('Expr', ['value'], [self.val(nd.doc)], jac_node=nd.doc);
        self.set_out(nd, [doc] + assigns_ast);
    } else {
        self.set_out(nd, assigns_ast);
    }
}

impl JcirGenPass.enter_test(nd: uni.Test) -> None {
    self._push_hoist_frame();
}

impl JcirGenPass.exit_test(nd: uni.Test) -> None {
    import from jaclang.jac0core.ext_registry { is_test }
    test_name = nd.name.sym_name;
    if nd.code_context == CodeContext.NATIVE {
        self._refuse(nd, 'native codespace test shim');
    }
    body_stmts = self.resolve_stmt_block(nd.body, doc=nd.doc);
    test_directives = self.scope_directive_stmts([nd]);
    if test_directives {
        insert_at = 1
            if (
                len(body_stmts) > 0
                and isinstance(body_stmts[0], CgNode)
                and body_stmts[0].cls == 'Expr'
                and isinstance(body_stmts[0].get('value'), CgNode)
                and body_stmts[0].get('value').cls == 'Constant'
                and isinstance(body_stmts[0].get('value').get('value'), str)
            )
            else 0;
        body_stmts = [
            *body_stmts[:insert_at],
            *test_directives,
            *body_stmts[insert_at:]
        ];
    }
    scope_frame = self._hoist_frames.pop() if self._hoist_frames else [];
    if not self._hoist_frames {
        self._hoist_frames = [[]];
    }
    test_sig_roots: list[uni.UniNode] = [];
    if nd.decorators {
        for d in nd.decorators {
            if d {
                test_sig_roots.append(d);
            }
        }
    }
    (test_body_hoists, test_enclosing_hoists) = self._split_scope_hoists(
        scope_frame, test_sig_roots
    );
    if test_enclosing_hoists {
        self._hoist_frames[-1].extend(test_enclosing_hoists);
    }
    if test_body_hoists {
        body_stmts = [*test_body_hoists, *body_stmts];
    }
    func = self.nod(
        'FunctionDef',
        [
            'name',
            'args',
            'body',
            'decorator_list',
            'returns',
            'type_comment',
            'type_params'
        ],
        [
            test_name,
            self.nod(
                'arguments',
                [
                    'posonlyargs',
                    'args',
                    'kwonlyargs',
                    'vararg',
                    'kwarg',
                    'kw_defaults',
                    'defaults'
                ],
                [[], [], [], None, None, [], []]
            ),
            body_stmts,
            [self.val(i) for i in nd.decorators] if nd.decorators else [],
            self.const_of(None),
            None,
            []
        ]
    );
    jac_test_keywords: list = [];
    if nd.description {
        jac_test_keywords.append(
            self.nod(
                'keyword',
                ['arg', 'value'],
                ['description', self.const_of(nd.description.lit_value)]
            )
        );
    }
    func.get('decorator_list').append(
        self.nod(
            'Call',
            ['func', 'args', 'keywords'],
            [self.jaclib_obj('jac_test'), [], jac_test_keywords]
        )
    );
    if is_test(nd.loc.mod_path) {
        func.get('decorator_list').append(
            self.nod(
                'Call',
                ['func', 'args', 'keywords'],
                [
                    self.jaclib_obj('impl_patch_filename'),
                    [],
                    [
                        self.nod(
                            'keyword',
                            ['arg', 'value'],
                            ['file_loc', self.const_of(nd.loc.mod_path)]
                        )
                    ]
                ]
            )
        );
    }
    self.set_out(nd, [func]);
}

impl JcirGenPass.exit_module_code(nd: uni.ModuleCode) -> None {
    stmts = self.resolve_stmt_block(nd.body, doc=nd.doc);
    if nd.name {
        stmts = [
            self.nod(
                'If',
                ['test', 'body', 'orelse'],
                [
                    self.nod(
                        'Compare',
                        ['left', 'ops', 'comparators'],
                        [
                            self.name_load('__name__'),
                            [self.nod('Eq', [], [])],
                            [self.const_of(nd.name.sym_name)]
                        ]
                    ),
                    stmts,
                    []
                ]
            )
        ];
    }
    self.set_out(nd, stmts);
}

impl JcirGenPass.exit_py_inline_code(nd: uni.PyInlineCode) -> None {
    if nd.doc {
        doc = self.nod('Expr', ['value'], [self.val(nd.doc)], jac_node=nd.doc);
        shift = nd.loc.first_line;
        inner = doc.get('value');
        doc.l1 += shift;
        doc.l2 += shift;
        inner.l1 += shift;
        inner.l2 += shift;
        self.set_out(nd, [doc, CgSplice(src=nd.code.value, offset=shift)]);
    } else {
        self.set_out(
            nd, [CgSplice(src=textwrap.dedent(nd.code.value), offset=nd.loc.first_line)]
        );
    }
}

impl JcirGenPass.exit_import(nd: uni.Import) -> None {
    import from jaclang.jac0core.compiler {
        import_binds_client_native,
        native_only_import_target
    }
    drop_native_target = False;
    native_target = native_only_import_target(nd, self.prog);
    if native_target is not None {
        import os as _os;
        drop_native_target = (_os.sep + "jaclang" + _os.sep)
            not in _os.path.abspath(native_target);
    }
    if (
        import_binds_client_native(nd, self.ir_in.gen.interop_manifest)
        or drop_native_target
    ) {
        self.set_out(nd, []);
        return;
    }
    if (nd.from_loc and nd.from_loc.path) {
        module_parts = [
            p.value
            for p in nd.from_loc.path
            if p?.value
        ];
        if (module_parts == ['__future__']) {
            self.set_out(nd, []);
            return;
        }
    }
    if (
        nd.from_loc
        and nd.items
        and nd.is_service_import
        and nd.code_context != CodeContext.CLIENT
    ) {
        manifest = self.ir_in.gen.interop_manifest;
        sv_module_name = nd.from_loc.base_module_name
            if (manifest and nd.from_loc.path)
            else None;
        if (manifest and sv_module_name) {
            for b in manifest.server_exports_to_server {
                if b.source_module == sv_module_name {
                    self._refuse(nd, 'service-to-service import stubs');
                }
            }
        }
    }
    py_nodes: list = [];
    if nd.doc {
        py_nodes.append(
            self.nod('Expr', ['value'], [self.val(nd.doc)], jac_node=nd.doc)
        );
    }
    if nd.is_absorb {
        source = nd.items[0];
        if not isinstance(source, uni.ModulePath) {
            raise self.ice();
        }
        module_name_parts = [p.value for p in source.path] if source.path else [];
        module_name = '.'.join(module_name_parts) if module_name_parts else None;
        py_nodes.append(
            self.nod(
                'ImportFrom',
                ['module', 'names', 'level'],
                [
                    module_name,
                    [self.nod('alias', ['name'], ['*'], jac_node=nd)],
                    source.level
                ],
                jac_node=nd
            )
        );
    } elif not nd.from_loc {
        mod_paths = [
            k
            for k in nd.kid
            if isinstance(k, uni.ModulePath)
        ];
        names_list = [x for p in mod_paths for x in self.outs(p)]
            if mod_paths
            else [x for item in nd.items for x in self.outs(item)];
        py_nodes.append(self.nod('Import', ['names'], [names_list]));
    } else {
        module_name_parts = [p.value for p in nd.from_loc.path]
            if nd.from_loc.path
            else [];
        module_name = '.'.join(module_name_parts) if module_name_parts else None;
        py_nodes.append(
            self.nod(
                'ImportFrom',
                ['module', 'names', 'level'],
                [
                    module_name,
                    [i for item in nd.items for i in self.outs(item)],
                    nd.from_loc.level
                ]
            )
        );
    }
    if nd.is_typed {
        self.typing_imports.add('TYPE_CHECKING');
        body_stmts = [
            stmt
            for stmt in py_nodes
            if isinstance(stmt, CgNode) and stmt.cls in ('Import', 'ImportFrom', 'Expr')
        ];
        if body_stmts {
            wrapped = self.nod(
                'If',
                ['test', 'body', 'orelse'],
                [self.name_load('TYPE_CHECKING'), body_stmts, []]
            );
            py_nodes = [wrapped];
        }
    }
    self.set_out(nd, py_nodes);
}

impl JcirGenPass.exit_module_path(nd: uni.ModulePath) -> None {
    if nd.string_path_value is not None {
        import_node = nd.parent_of_type(uni.Import);
        if (import_node and (import_node.code_context != CodeContext.CLIENT)) {
            self.emit(E5001, node_override=nd, path=str(nd.string_path_value));
        }
    }
    self.set_out(
        nd,
        [
            self.nod(
                'alias',
                ['name', 'asname'],
                [str(nd.dot_path_str), nd.alias.sym_name if nd.alias else None]
            )
        ]
    );
}

impl JcirGenPass.exit_module_item(nd: uni.ModuleItem) -> None {
    if (isinstance(nd.name, uni.Token) and (nd.name.value in ['default', '*'])) {
        import_node = nd.from_parent;
        if (import_node.code_context != CodeContext.CLIENT) {
            import_type = 'Default' if (nd.name.value == 'default') else 'Namespace';
            self.emit(
                E5002,
                node_override=nd,
                import_type=import_type,
                name=str(nd.name.value)
            );
            self.set_out(nd, []);
            return;
        }
    }
    self.set_out(
        nd,
        [
            self.nod(
                'alias',
                ['name', 'asname'],
                [
                    (
                        nd.name.sym_name
                            if isinstance(nd.name, uni.Name)
                            else nd.name.value
                    ),
                    nd.alias.sym_name if nd.alias else None
                ]
            )
        ]
    );
}

impl JcirGenPass.enter_archetype(nd: uni.Archetype) -> None {
    if isinstance(nd.body, uni.ImplDef) {
        self.traverse(nd.body);
    }
}

impl JcirGenPass.exit_archetype(nd: uni.Archetype) -> None {
    if nd.is_async {
        self._refuse(nd, 'async archetype');
    }
    self._check_sem_decorator(nd);
    inner: Sequence[uni.CodeBlockStmt] | Sequence[uni.EnumBlockStmt] | None = None;
    if isinstance(nd.body, uni.ImplDef) {
        inner = nd.body.body if not isinstance(nd.body.body, uni.Expr) else None;
    } elif not isinstance(nd.body, uni.Expr) {
        inner = nd.body;
    }
    decorators = [self.val(i) for i in nd.decorators] if nd.decorators else [];
    body: list = self.resolve_stmt_block(inner, doc=nd.doc);
    if (not body and not isinstance(nd.body, uni.Expr)) {
        self.emit(E5003, node_override=nd);
        body = [self.nod('Pass', [], [], jac_node=nd)];
    }
    base_classes = [self.val(i) for i in nd.base_classes];
    if (nd.arch_type.name != Tok.KW_CLASS) {
        base_classes.append(self.jaclib_obj(nd.arch_type.value.capitalize()));
    }
    type_param_asts: list = [];
    if nd.type_params {
        for tp in nd.type_params {
            if self.outs(tp) {
                type_param_asts.append(self.val(tp));
            }
        }
    }
    keywords = [self.val(k) for k in nd.keywords] if nd.keywords else [];
    class_def = self.nod(
        'ClassDef',
        ['name', 'bases', 'keywords', 'body', 'decorator_list', 'type_params'],
        [nd.name.sym_name, base_classes, keywords, body, decorators, type_param_asts]
    );
    self.set_out(nd, [class_def]);
}

impl JcirGenPass.enter_enum(nd: uni.Enum) -> None {
    if isinstance(nd.body, uni.ImplDef) {
        self.traverse(nd.body);
    }
}

impl JcirGenPass.exit_enum(nd: uni.Enum) -> None {
    self._check_sem_decorator(nd);
    inner: Sequence[uni.CodeBlockStmt] | Sequence[uni.EnumBlockStmt] | None = None;
    if isinstance(nd.body, uni.ImplDef) {
        inner = nd.body.body if not isinstance(nd.body.body, uni.Expr) else None;
    } elif not isinstance(nd.body, uni.Expr) {
        inner = nd.body;
    }
    decorators = [self.val(i) for i in nd.decorators] if nd.decorators else [];
    body = self.resolve_stmt_block(inner, doc=nd.doc);
    trailing_kind: str = "Enum";
    extra_bases: list = [];
    if nd.value_type {
        if (isinstance(nd.value_type, uni.Name) and nd.value_type.value == "int") {
            trailing_kind = "IntEnum";
        } elif (isinstance(nd.value_type, uni.Name) and nd.value_type.value == "str") {
            trailing_kind = "StrEnum";
        } else {
            extra_bases.append(self.val(nd.value_type));
        }
    }
    self.needs_enum_kind(trailing_kind);
    base_classes = extra_bases + [self.val(i) for i in nd.base_classes];
    base_classes.append(self.builtin_name(trailing_kind));
    class_def = self.nod(
        'ClassDef',
        ['name', 'bases', 'keywords', 'body', 'decorator_list', 'type_params'],
        [nd.name.sym_name, base_classes, [], body, decorators, []]
    );
    self.set_out(nd, [class_def]);
}

impl JcirGenPass.enter_ability(nd: uni.Ability) -> None {
    self._push_hoist_frame();
    if isinstance(nd.body, uni.ImplDef) {
        self.traverse(nd.body);
    }
}

impl JcirGenPass.exit_ability(nd: uni.Ability) -> None {
    if nd.is_async {
        self._refuse(nd, 'async ability');
    }
    if isinstance(nd.signature, uni.EventSignature) {
        self._refuse(nd, 'walker event signature ability');
    }
    self._check_sem_decorator(nd);
    has_by_in_return = (
        isinstance(nd.signature, uni.FuncSignature)
        and nd.signature.return_type
        and isinstance(nd.signature.return_type, uni.BinaryExpr)
        and isinstance(nd.signature.return_type.op, uni.Token)
        and (nd.signature.return_type.op.name == Tok.KW_BY)
    );
    if (
        isinstance(nd.body, uni.Expr)
        or (isinstance(nd.body, uni.ImplDef) and isinstance(nd.body.body, uni.Expr))
        or has_by_in_return
    ) {
        self._refuse(nd, 'llm-backed ability body');
    }
    scope_frame = self._hoist_frames.pop() if self._hoist_frames else [];
    if not self._hoist_frames {
        self._hoist_frames = [[]];
    }
    sig_roots: list[uni.UniNode] = [];
    if nd.signature {
        sig_roots.append(nd.signature);
    }
    if nd.decorators {
        for d in nd.decorators {
            if d {
                sig_roots.append(d);
            }
        }
    }
    (body_hoists, enclosing_hoists) = self._split_scope_hoists(scope_frame, sig_roots);
    if enclosing_hoists {
        self._hoist_frames[-1].extend(enclosing_hoists);
    }
    is_prop_acc = nd.accessor_kind != "";
    prop_owner_name: str | None = None;
    if is_prop_acc {
        hv = nd.find_parent_of_type(uni.HasVar);
        if (hv and isinstance(hv.name, uni.Name)) {
            prop_owner_name = hv.name.value;
        }
    }
    body = [
        self.nod('Expr', ['value'], [self.val(nd.doc)], jac_node=nd.doc),
        self.nod('Pass', [], [], jac_node=nd.body)
    ]
        if (nd.doc and nd.is_abstract)
        else [self.nod('Pass', [], [], jac_node=nd.body)]
            if nd.is_abstract
            else self.resolve_stmt_block(
                nd.body.body
                    if (
                        isinstance(nd.body, uni.ImplDef)
                        and not isinstance(nd.body.body, uni.Expr)
                    )
                    else nd.body if not isinstance(nd.body, uni.Expr) else None,
                doc=nd.doc
            );
    if (nd.is_abstract and nd.body) {
        self.emit(E5004, node_override=nd, name=str(nd.sym_name));
    }
    decorator_list = [self.val(i) for i in nd.decorators] if nd.decorators else [];
    if isinstance(nd.body, uni.ImplDef) {
        decorator_list.append(
            self.nod(
                'Call',
                ['func', 'args', 'keywords'],
                [
                    self.jaclib_obj('impl_patch_filename'),
                    [self.const_of(nd.body.loc.mod_path)],
                    []
                ]
            )
        );
    }
    if nd.is_abstract {
        decorator_list.append(self.builtin_name('abstractmethod'));
    }
    if nd.is_override {
        decorator_list.append(self.builtin_name('override'));
    }
    if nd.is_static {
        decorator_list.insert(0, self.name_load('staticmethod'));
    }
    if nd.is_classmethod {
        decorator_list.insert(0, self.name_load('classmethod'));
    }
    if (is_prop_acc and (prop_owner_name is not None)) {
        if nd.accessor_kind == "property" {
            decorator_list.insert(0, self.name_load('property'));
        } elif nd.accessor_kind == "setter" {
            decorator_list.insert(
                0,
                self.nod(
                    'Attribute',
                    ['value', 'attr', 'ctx'],
                    [
                        self.name_load(prop_owner_name),
                        'setter',
                        self.nod('Load', [], [])
                    ]
                )
            );
        } elif nd.accessor_kind == "deleter" {
            decorator_list.insert(
                0,
                self.nod(
                    'Attribute',
                    ['value', 'attr', 'ctx'],
                    [
                        self.name_load(prop_owner_name),
                        'deleter',
                        self.nod('Load', [], [])
                    ]
                )
            );
        }
    }
    if (not body and not isinstance(nd.body, uni.Expr)) {
        self.emit(E5005, node_override=nd);
        body = [self.nod('Pass', [], [], jac_node=nd)];
    }
    if body_hoists {
        body = [*body_hoists, *body];
    }
    directive_stmts = self.scope_directive_stmts(
        [nd, nd.body if isinstance(nd.body, uni.ImplDef) else None]
    );
    if directive_stmts {
        insert_at = 1
            if (
                len(body) > 0
                and isinstance(body[0], CgNode)
                and body[0].cls == 'Expr'
                and isinstance(body[0].get('value'), CgNode)
                and body[0].get('value').cls == 'Constant'
                and isinstance(body[0].get('value').get('value'), str)
            )
            else 0;
        body = [*body[:insert_at], *directive_stmts, *body[insert_at:]];
    }
    ast_returns: CgNode = self.const_of(None);
    if (isinstance(nd.signature, uni.FuncSignature) and nd.signature.return_type) {
        ast_returns = self.val(nd.signature.return_type);
    }
    if (
        is_prop_acc
        and (nd.accessor_kind == "property")
        and (nd.signature is None)
        and (prop_owner_name is not None)
    ) {
        hv2 = nd.find_parent_of_type(uni.HasVar);
        if (hv2 and hv2.type_tag) {
            ast_returns = self.val(hv2.type_tag);
        }
    }
    func_name_out = (
        prop_owner_name
            if (is_prop_acc and (prop_owner_name is not None))
            else self._py_name(nd.name_ref)
    );
    func_args_out: CgNode;
    if (is_prop_acc and (nd.signature is None)) {
        acc_args = [self.nod('arg', ['arg', 'annotation'], ['self', None])];
        if nd.accessor_kind == "setter" {
            acc_args.append(self.nod('arg', ['arg', 'annotation'], ['value', None]));
        }
        func_args_out = self.nod(
            'arguments',
            [
                'posonlyargs',
                'args',
                'vararg',
                'kwonlyargs',
                'kw_defaults',
                'kwarg',
                'defaults'
            ],
            [[], acc_args, None, [], [], None, []]
        );
    } elif nd.signature {
        func_args_out = self.val(nd.signature);
    } else {
        func_args_out = self.nod(
            'arguments',
            [
                'posonlyargs',
                'args',
                'vararg',
                'kwonlyargs',
                'kw_defaults',
                'kwarg',
                'defaults'
            ],
            [
                [],
                [self.nod('arg', ['arg', 'annotation'], ['self', None])]
                    if nd.is_method and not nd.is_static and not nd.is_classmethod
                    else [],
                None,
                [],
                [],
                None,
                []
            ]
        );
    }
    func_def = self.nod(
        'FunctionDef',
        ['name', 'args', 'body', 'decorator_list', 'returns', 'type_params'],
        [func_name_out, func_args_out, body, decorator_list, ast_returns, []]
    );
    self.set_out(nd, [func_def]);
}

impl JcirGenPass.exit_impl_def(nd: uni.ImplDef) -> None {
    ;
}

impl JcirGenPass.exit_sem_def(nd: uni.SemDef) -> None {
    ;
}

impl JcirGenPass.exit_func_signature(nd: uni.FuncSignature) -> None {
    posonlyargs = [self.val(i) for i in nd.posonly_params];
    vararg = self.val(nd.varargs) if nd.varargs else None;
    kwarg = self.val(nd.kwargs) if nd.kwargs else None;
    params = [self.nod('arg', ['arg', 'annotation'], ['cls', None])]
        if (
            (abl := nd.parent)
            and isinstance(abl, uni.Ability)
            and abl.is_method
            and abl.is_classmethod
            and not nd.is_in_py_class
        )
        else [self.nod('arg', ['arg', 'annotation'], ['self', None])]
            if (
                (abl := nd.parent)
                and isinstance(abl, uni.Ability)
                and abl.is_method
                and not nd.is_static
                and not abl.is_cls_method
                and not nd.is_in_py_class
            )
            else [];
    if posonlyargs {
        posonlyargs = params + posonlyargs;
        params = [self.val(i) for i in nd.params];
    } else {
        params = params + [self.val(i) for i in nd.params];
    }
    defaults: list = [];
    for i in [*nd.posonly_params, *nd.params] {
        if i.value {
            defaults.append(self.val(i.value));
        }
    }
    kwonly_args = [self.val(i) for i in nd.kwonlyargs];
    kw_defaults: list = [];
    for i in nd.kwonlyargs {
        if i.value {
            kw_defaults.append(self.val(i.value));
        } else {
            kw_defaults.append(None);
        }
    }
    self.set_out(
        nd,
        [
            self.nod(
                'arguments',
                [
                    'posonlyargs',
                    'args',
                    'kwonlyargs',
                    'vararg',
                    'kwarg',
                    'kw_defaults',
                    'defaults'
                ],
                [posonlyargs, params, kwonly_args, vararg, kwarg, kw_defaults, defaults]
            )
        ]
    );
}

impl JcirGenPass.exit_event_signature(nd: uni.EventSignature) -> None {
    self._refuse(nd, 'walker event signature');
}

impl JcirGenPass.exit_param_var(nd: uni.ParamVar) -> None {
    if self.val(nd.name).cls == 'Name' {
        name = self.val(nd.name).get('id');
        self.set_out(
            nd,
            [
                self.nod(
                    'arg',
                    ['arg', 'annotation'],
                    [name, self.val(nd.type_tag) if nd.type_tag else None]
                )
            ]
        );
    }
}

impl JcirGenPass.exit_arch_has(nd: uni.ArchHas) -> None {
    vars_py = self._flatten_ast_list([self.outs(v) for v in nd.vars]);
    if nd.doc {
        doc = self.nod('Expr', ['value'], [self.val(nd.doc)], jac_node=nd.doc);
        self.set_out(nd, [doc] + vars_py);
    } else {
        self.set_out(nd, vars_py);
    }
}

impl JcirGenPass.exit_has_var(nd: uni.HasVar) -> None {
    annotation = self.val(nd.type_tag) if nd.type_tag else None;
    if ((nd.accessors is not None) and (len(nd.accessors) > 0)) {
        result_stmts: list = [];
        for acc in nd.accessors {
            for stmt in self.outs(acc) {
                result_stmts.append(stmt);
            }
        }
        self.set_out(nd, result_stmts);
        return;
    }
    is_static_var = (
        (haspar := nd.find_parent_of_type(uni.ArchHas)) and haspar and haspar.is_static
    );
    is_in_class = (
        (archpar := nd.find_parent_of_type(uni.Archetype))
        and archpar
        and (archpar.arch_type.name == Tok.KW_CLASS)
    );
    value = None;
    if is_in_class {
        value = self.val(nd.value) if nd.value else None;
    } elif is_static_var {
        annotation = self.nod(
            'Subscript',
            ['value', 'slice', 'ctx'],
            [self.builtin_name('ClassVar'), annotation, self.nod('Load', [], [])]
        );
        value = self.val(nd.value) if nd.value else None;
    } elif nd.defer {
        value = self.nod(
            'Call',
            ['func', 'args', 'keywords'],
            [
                self.jaclib_obj('field'),
                [],
                [self.nod('keyword', ['arg', 'value'], ['init', self.const_of(False)])]
            ]
        );
    } elif nd.value {
        value_expr = self.val(nd.value);
        if value_expr.cls == 'Constant' {
            value = value_expr;
        } else {
            value = self.nod(
                'Call',
                ['func', 'args', 'keywords'],
                [
                    self.jaclib_obj('field'),
                    [],
                    [
                        self.nod(
                            'keyword',
                            ['arg', 'value'],
                            [
                                'factory',
                                self.nod(
                                    'Lambda',
                                    ['args', 'body'],
                                    [
                                        self.nod(
                                            'arguments',
                                            [
                                                'posonlyargs',
                                                'args',
                                                'kwonlyargs',
                                                'vararg',
                                                'kwarg',
                                                'kw_defaults',
                                                'defaults'
                                            ],
                                            [[], [], [], None, None, [], []]
                                        ),
                                        value_expr
                                    ]
                                )
                            ]
                        )
                    ]
                ]
            );
        }
    }
    ann_assign = self.nod(
        'AnnAssign',
        ['target', 'annotation', 'value', 'simple'],
        [
            self.val(nd.name),
            annotation or self.const_of(None),
            value,
            int(isinstance(nd.name, uni.Name))
        ]
    );
    self.set_out(nd, [ann_assign]);
}

impl JcirGenPass.exit_typed_ctx_block(nd: uni.TypedCtxBlock) -> None {
    self._refuse(nd, 'typed context block');
}

impl JcirGenPass.exit_open_stmt(nd: uni.OpenStmt) -> None {
    self.builtin_imports.add("_jac_region_open");
    self.builtin_imports.add("_jac_region_close");
    _tmp = f"_jac_region_{nd.loc.first_line}_{nd.loc.col_start}";
    assign_stmt = self.nod(
        'Assign',
        ['targets', 'value'],
        [
            [
                self.nod(
                    'Name',
                    ['id', 'ctx'],
                    [_tmp, self.nod('Store', [], [], jac_node=nd)],
                    jac_node=nd
                )
            ],
            self.val(nd.target)
        ],
        jac_node=nd.target
    );
    open_stmt = self.nod(
        'Expr',
        ['value'],
        [
            self.nod(
                'Call',
                ['func', 'args', 'keywords'],
                [
                    self.name_load('_jac_region_open', jac_node=nd),
                    [self.name_load(_tmp, jac_node=nd)],
                    []
                ],
                jac_node=nd
            )
        ],
        jac_node=nd
    );
    close_stmt = self.nod(
        'Expr',
        ['value'],
        [
            self.nod(
                'Call',
                ['func', 'args', 'keywords'],
                [
                    self.name_load('_jac_region_close', jac_node=nd),
                    [self.name_load(_tmp, jac_node=nd)],
                    []
                ],
                jac_node=nd
            )
        ],
        jac_node=nd
    );
    clear_stmt = self.nod(
        'Assign',
        ['targets', 'value'],
        [
            [
                self.nod(
                    'Name',
                    ['id', 'ctx'],
                    [_tmp, self.nod('Store', [], [], jac_node=nd)],
                    jac_node=nd
                )
            ],
            self.const_of(None, jac_node=nd)
        ],
        jac_node=nd
    );
    body_stmts = self._flatten_ast_list([self.outs(item) for item in nd.body]);
    if not body_stmts {
        body_stmts = [self.nod('Pass', [], [], jac_node=nd)];
    }
    try_stmt = self.nod(
        'Try',
        ['body', 'handlers', 'orelse', 'finalbody'],
        [body_stmts, [], [], [close_stmt, clear_stmt]],
        jac_node=nd
    );
    self.set_out(nd, [assign_stmt, open_stmt, try_stmt]);
}

impl JcirGenPass.exit_if_stmt(nd: uni.IfStmt) -> None {
    self.set_out(
        nd,
        [
            self.nod(
                'If',
                ['test', 'body', 'orelse'],
                [
                    self.val(nd.condition),
                    self.resolve_stmt_block(nd.body),
                    self.outs(nd.else_body) if nd.else_body else []
                ]
            )
        ]
    );
}

impl JcirGenPass.exit_else_if(nd: uni.ElseIf) -> None {
    self.set_out(
        nd,
        [
            self.nod(
                'If',
                ['test', 'body', 'orelse'],
                [
                    self.val(nd.condition),
                    self.resolve_stmt_block(nd.body),
                    self.outs(nd.else_body) if nd.else_body else []
                ]
            )
        ]
    );
}

impl JcirGenPass.exit_else_stmt(nd: uni.ElseStmt) -> None {
    self.set_out(nd, self.resolve_stmt_block(nd.body));
}

impl JcirGenPass.exit_expr_stmt(nd: uni.ExprStmt) -> None {
    expr_stmt = self.nod('Expr', ['value'], [self.val(nd.expr)])
        if not nd.in_fstring
        else self.nod(
            'FormattedValue',
            ['value', 'conversion', 'format_spec'],
            [self.val(nd.expr), -1, None]
        );
    self.set_out(nd, [expr_stmt]);
}

impl JcirGenPass.exit_concurrent_expr(nd: uni.ConcurrentExpr) -> None {
    self._refuse(nd, 'concurrent flow/wait expression');
}

impl JcirGenPass.exit_try_stmt(nd: uni.TryStmt) -> None {
    self.set_out(
        nd,
        [
            self.nod(
                'Try',
                ['body', 'handlers', 'orelse', 'finalbody'],
                [
                    self.resolve_stmt_block(nd.body),
                    [self.val(i) for i in nd.excepts],
                    self.outs(nd.else_body) if nd.else_body else [],
                    self.outs(nd.finally_body) if nd.finally_body else []
                ]
            )
        ]
    );
}

impl JcirGenPass.exit_except(nd: uni.Except) -> None {
    self.set_out(
        nd,
        [
            self.nod(
                'ExceptHandler',
                ['type', 'name', 'body'],
                [
                    self.val(nd.ex_type) if nd.ex_type else None,
                    nd.name.sym_name if nd.name else None,
                    self.resolve_stmt_block(nd.body)
                ]
            )
        ]
    );
}

impl JcirGenPass.exit_finally_stmt(nd: uni.FinallyStmt) -> None {
    self.set_out(nd, self.resolve_stmt_block(nd.body));
}

impl JcirGenPass.exit_iter_for_stmt(nd: uni.IterForStmt) -> None {
    py_nodes: list = [];
    body = self.resolve_stmt_block(nd.body);
    body = body + [self.val(nd.count_by)];
    py_nodes.append(self.val(nd.iter));
    py_nodes.append(
        self.nod(
            'While',
            ['test', 'body', 'orelse'],
            [
                self.val(nd.condition),
                body,
                self.outs(nd.else_body) if nd.else_body else []
            ]
        )
    );
    self.set_out(nd, py_nodes);
}

impl JcirGenPass.exit_in_for_stmt(nd: uni.InForStmt) -> None {
    if nd.is_async {
        self._refuse(nd, 'async for statement');
    }
    self.set_out(
        nd,
        [
            self.nod(
                'For',
                ['target', 'iter', 'body', 'orelse'],
                [
                    self.val(nd.target),
                    self.val(nd.collection),
                    self.resolve_stmt_block(nd.body),
                    self.outs(nd.else_body) if nd.else_body else []
                ]
            )
        ]
    );
}

impl JcirGenPass.exit_while_stmt(nd: uni.WhileStmt) -> None {
    self.set_out(
        nd,
        [
            self.nod(
                'While',
                ['test', 'body', 'orelse'],
                [
                    self.val(nd.condition),
                    self.resolve_stmt_block(nd.body),
                    self.outs(nd.else_body) if nd.else_body else []
                ]
            )
        ]
    );
}

impl JcirGenPass.exit_forever_stmt(nd: uni.ForeverStmt) -> None {
    self.set_out(
        nd,
        [
            self.nod(
                'While',
                ['test', 'body', 'orelse'],
                [self.const_of(True), self.resolve_stmt_block(nd.body), []]
            )
        ]
    );
}

impl JcirGenPass.exit_with_stmt(nd: uni.WithStmt) -> None {
    if nd.is_async {
        self._refuse(nd, 'async with statement');
    }
    self.set_out(
        nd,
        [
            self.nod(
                'With',
                ['items', 'body'],
                [
                    [self.val(item) for item in nd.exprs],
                    self.resolve_stmt_block(nd.body)
                ]
            )
        ]
    );
}

impl JcirGenPass.exit_expr_as_item(nd: uni.ExprAsItem) -> None {
    self.set_out(
        nd,
        [
            self.nod(
                'withitem',
                ['context_expr', 'optional_vars'],
                [self.val(nd.expr), self.val(nd.alias) if nd.alias else None]
            )
        ]
    );
}

impl JcirGenPass.exit_raise_stmt(nd: uni.RaiseStmt) -> None {
    self.set_out(
        nd,
        [
            self.nod(
                'Raise',
                ['exc', 'cause'],
                [
                    self.val(nd.cause) if nd.cause else None,
                    self.val(nd.from_target) if nd.from_target else None
                ]
            )
        ]
    );
}

impl JcirGenPass.exit_assert_stmt(nd: uni.AssertStmt) -> None {
    self.set_out(
        nd,
        [
            self.nod(
                'Assert',
                ['test', 'msg'],
                [
                    self.val(nd.condition),
                    self.val(nd.error_msg) if nd.error_msg else None
                ]
            )
        ]
    );
}

impl JcirGenPass.exit_ctrl_stmt(nd: uni.CtrlStmt) -> None {
    if (nd.ctrl.name == Tok.KW_BREAK) {
        self.set_out(nd, [self.nod('Break', [], [])]);
    } elif (nd.ctrl.name == Tok.KW_CONTINUE) {
        if (iter_for_parent := self.find_parent_of_type(nd, uni.IterForStmt)) {
            count_by = iter_for_parent.count_by;
            self.set_out(nd, [self.val(count_by), self.nod('Continue', [], [])]);
        } else {
            self.set_out(nd, [self.nod('Continue', [], [])]);
        }
    } elif (nd.ctrl.name == Tok.KW_SKIP) {
        self._refuse(nd, 'walker skip statement');
    }
}

impl JcirGenPass._delete_targets(nd: uni.DeleteStmt) -> list {
    if (isinstance(nd.target, uni.TupleVal) and nd.target.values) {
        return [self.val(i) for i in nd.target.values];
    }
    return self.outs(nd.target);
}

impl JcirGenPass.exit_delete_stmt(nd: uni.DeleteStmt) -> None {
    def set_ctx(targets: any, ctx_cls: str) -> list {
        if not isinstance(targets, list) {
            targets = [targets];
        } elif (
            targets
            and isinstance(targets[0], CgNode)
            and targets[0].cls in ('List', 'Tuple')
        ) {
            targets = [
                i
                for i in targets[0].get('elts')
                if isinstance(i, CgNode)
            ];
        }
        result: list = [];
        for target in targets {
            if (isinstance(target, CgNode) and target.hasf('ctx')) {
                target = self._with_ctx(target, ctx_cls);
            }
            result.append(target);
        }
        return result;
    }
    destroy_expr = self.nod(
        'Expr',
        ['value'],
        [
            self.nod(
                'Call',
                ['func', 'args', 'keywords'],
                [
                    self.jaclib_obj('destroy'),
                    [
                        self.nod(
                            'List',
                            ['elts', 'ctx'],
                            [
                                set_ctx(self._delete_targets(nd), 'Load'),
                                self.nod('Load', [], [])
                            ]
                        )
                    ],
                    []
                ]
            )
        ]
    );
    delete_stmt = self.nod(
        'Delete', ['targets'], [set_ctx(self._delete_targets(nd), 'Del')]
    );
    self.set_out(nd, [destroy_expr, delete_stmt]);
}

impl JcirGenPass.exit_report_stmt(nd: uni.ReportStmt) -> None {
    self._refuse(nd, 'walker report statement');
}

impl JcirGenPass.exit_return_stmt(nd: uni.ReturnStmt) -> None {
    self.set_out(
        nd, [self.nod('Return', ['value'], [self.val(nd.expr) if nd.expr else None])]
    );
}

impl JcirGenPass.exit_yield_expr(nd: uni.YieldExpr) -> None {
    if not nd.with_from {
        self.set_out(
            nd, [self.nod('Yield', ['value'], [self.val(nd.expr) if nd.expr else None])]
        );
    } else {
        self.set_out(
            nd,
            [
                self.nod(
                    'YieldFrom',
                    ['value'],
                    [self.val(nd.expr) if nd.expr else self.const_of(None)]
                )
            ]
        );
    }
}

impl JcirGenPass.exit_visit_stmt(nd: uni.VisitStmt) -> None {
    self._refuse(nd, 'walker visit statement');
}

impl JcirGenPass.exit_disengage_stmt(nd: uni.DisengageStmt) -> None {
    self._refuse(nd, 'walker disengage statement');
}

impl JcirGenPass.exit_await_expr(nd: uni.AwaitExpr) -> None {
    self._refuse(nd, 'await expression');
}

impl JcirGenPass.exit_cast_expr(nd: uni.CastExpr) -> None {
    self.set_out(nd, self.outs(nd.value));
}

impl JcirGenPass.exit_assignment(nd: uni.Assignment) -> None {
    value = self.val(nd.value)
        if nd.value
        else self.nod(
            'Call', ['func', 'args', 'keywords'], [self.name_load('auto'), [], []]
        )
            if nd.is_enum_stmt
            else None if nd.type_tag else self.ice();
    targets_ast = [self.val(t) for t in nd.target];
    if nd.type_tag {
        assignment_stmt = self.nod(
            'AnnAssign',
            ['target', 'annotation', 'value', 'simple'],
            [
                targets_ast[0],
                self.val(nd.type_tag),
                self.val(nd.value) if nd.value else None,
                int(isinstance(targets_ast[0], CgNode) and targets_ast[0].cls == 'Name')
            ]
        );
    } elif nd.aug_op {
        assignment_stmt = self.nod(
            'AugAssign',
            ['target', 'op', 'value'],
            [
                targets_ast[0],
                self.val(nd.aug_op),
                value if isinstance(value, CgNode) else self.const_of(None)
            ]
        );
    } else {
        assignment_stmt = self.nod(
            'Assign',
            ['targets', 'value'],
            [targets_ast, value if isinstance(value, CgNode) else self.const_of(None)]
        );
    }
    self.set_out(nd, [assignment_stmt]);
}

impl JcirGenPass.exit_binary_expr(nd: uni.BinaryExpr) -> None {
    if isinstance(nd.op, (uni.ConnectOp, uni.DisconnectOp)) {
        self._refuse(nd, 'edge connect/disconnect operator');
    } elif (nd.op.name in [Tok.KW_AND.value, Tok.KW_OR.value]) {
        self.set_out(
            nd,
            [
                self.nod(
                    'BoolOp',
                    ['op', 'values'],
                    [self.val(nd.op), [self.val(nd.left), self.val(nd.right)]]
                )
            ]
        );
    } elif ((nd.op.name in [Tok.WALRUS_EQ]) and self.val(nd.left).cls == 'Name') {
        self.val(nd.left).setf('ctx', self.nod('Store', [], []));
        self.set_out(
            nd,
            [
                self.nod(
                    'NamedExpr',
                    ['target', 'value'],
                    [self.val(nd.left), self.val(nd.right)]
                )
            ]
        );
    } elif ((nd.op.name in [Tok.WALRUS_EQ]) and not isinstance(nd.left, uni.Name)) {
        self.set_out(nd, [self.val(nd.right)]);
    } elif self.outs(nd.op) {
        self.set_out(
            nd,
            [
                self.nod(
                    'BinOp',
                    ['left', 'op', 'right'],
                    [self.val(nd.left), self.val(nd.op), self.val(nd.right)]
                )
            ]
        );
    } else {
        self.set_out(nd, self.translate_jac_bin_op(nd));
    }
}

impl JcirGenPass.translate_jac_bin_op(nd: uni.BinaryExpr) -> list {
    if (nd.op.name in [Tok.PIPE_FWD, Tok.A_PIPE_FWD]) {
        func_node = uni.FuncCall(
            target=nd.right,
            params=list(nd.left.values)
                if (isinstance(nd.left, uni.TupleVal) and nd.left.values)
                else [nd.left],
            genai_call=None,
            kid=nd.kid
        );
        func_node.parent = nd.parent;
        self.exit_func_call(func_node);
        return self.outs(func_node);
    } elif (nd.op.name in [Tok.KW_SPAWN]) {
        self._refuse(nd, 'walker spawn operator');
    } elif (nd.op.name in [Tok.PIPE_BKWD, Tok.A_PIPE_BKWD]) {
        func_node = uni.FuncCall(
            target=nd.left,
            params=list(nd.right.values)
                if (isinstance(nd.right, uni.TupleVal) and nd.right.values)
                else [nd.right],
            genai_call=None,
            kid=nd.kid
        );
        func_node.parent = nd.parent;
        self.exit_func_call(func_node);
        return self.outs(func_node);
    } elif (nd.op.name in [Tok.KW_BY]) {
        self._refuse(nd, 'llm by operator');
    } elif ((nd.op.name == Tok.PIPE_FWD) and isinstance(nd.right, uni.TupleVal)) {
        self.emit(E5006);
    } else {
        self.emit(E5007, op=str(nd.op.value));
    }
    return [];
}

impl JcirGenPass.exit_compare_expr(nd: uni.CompareExpr) -> None {
    self.set_out(
        nd,
        [
            self.nod(
                'Compare',
                ['left', 'ops', 'comparators'],
                [
                    self.val(nd.left),
                    [self.val(i) for i in nd.ops],
                    [self.val(i) for i in nd.rights]
                ]
            )
        ]
    );
}

impl JcirGenPass.exit_cfg_expr(nd: uni.CfgExpr) -> None {
    self.set_out(nd, self.outs(nd.expr));
}

impl JcirGenPass.exit_bool_expr(nd: uni.BoolExpr) -> None {
    self.set_out(
        nd,
        [
            self.nod(
                'BoolOp',
                ['op', 'values'],
                [self.val(nd.op), [self.val(i) for i in nd.values]]
            )
        ]
    );
}

impl JcirGenPass.exit_lambda_expr(nd: uni.LambdaExpr) -> None {
    if isinstance(nd.body, list) {
        tail = nd.tail_return_expr();
        if tail is not None {
            self._emit_lambda_expr_body(nd, tail);
            return;
        }
        if (nd.signature and self.outs(nd.signature)) {
            arguments = self.val(nd.signature);
        } else {
            arguments = self.nod(
                'arguments',
                ['posonlyargs', 'args', 'kwonlyargs', 'kw_defaults', 'defaults'],
                [[], [], [], [], []],
                jac_node=nd
            );
        }
        body_stmts = self.resolve_stmt_block(nd.body, doc=None);
        directive_stmts = self.scope_directive_stmts([nd]);
        if directive_stmts {
            body_stmts = [*directive_stmts, *body_stmts];
        }
        if not body_stmts {
            body_stmts = [self.nod('Pass', [], [], jac_node=nd)];
        }
        func_name = self._next_lambda_name();
        returns = self.val(nd.signature.return_type)
            if (
                nd.signature
                and nd.signature.return_type
                and self.outs(nd.signature.return_type)
            )
            else None;
        func_def = self.nod(
            'FunctionDef',
            ['name', 'args', 'body', 'decorator_list', 'returns', 'type_params'],
            [func_name, arguments, body_stmts, [], returns, []],
            jac_node=nd
        );
        self.set_out(nd, [self._function_expr_from_def(func_def, nd)]);
        return;
    }
    self._emit_lambda_expr_body(nd, nd.body);
}

impl JcirGenPass._emit_lambda_expr_body(
    nd: uni.LambdaExpr, body_node: uni.Expr
) -> None {
    if nd.signature {
        self._remove_lambda_param_annotations(nd.signature);
    }
    if (nd.signature and self.outs(nd.signature)) {
        arguments = self.val(nd.signature);
    } else {
        arguments = self.nod(
            'arguments',
            ['posonlyargs', 'args', 'kwonlyargs', 'kw_defaults', 'defaults'],
            [[], [], [], [], []],
            jac_node=nd
        );
    }
    self.set_out(
        nd,
        [
            self.nod(
                'Lambda',
                ['args', 'body'],
                [arguments, self.val(body_node)],
                jac_node=nd
            )
        ]
    );
}

impl JcirGenPass._remove_lambda_param_annotations(
    signature: uni.FuncSignature
) -> None {
    for param in signature.params {
        if (self.outs(param) and self.val(param).cls == 'arg') {
            self.val(param).setf('annotation', None);
        }
    }
}

impl JcirGenPass._emit_jobj_ref(nd: uni.UnaryExpr) -> None {
    self.set_out(
        nd,
        [
            self.nod(
                'Call',
                ['func', 'args', 'keywords'],
                [
                    self.builtin_name('jobj'),
                    [],
                    [
                        self.nod(
                            'keyword', ['arg', 'value'], ['id', self.val(nd.operand)]
                        )
                    ]
                ]
            )
        ]
    );
}

impl JcirGenPass.exit_unary_expr(nd: uni.UnaryExpr) -> None {
    if nd.ownership in (OwnershipKind.OWN, OwnershipKind.IMM) {
        self.set_out(nd, [self.val(nd.operand)]);
        return;
    }
    if nd.op.name == Tok.BW_AND {
        if is_borrow_kind(nd.ownership) {
            self.set_out(
                nd,
                [
                    self.nod(
                        'Call',
                        ['func', 'args', 'keywords'],
                        [self.builtin_name('jref'), [self.val(nd.operand)], []]
                    )
                ]
            );
        } else {
            self._emit_jobj_ref(nd);
        }
        return;
    }
    op_tok = Tok(nd.op.name) if (nd.op.name in Tok.__members__) else None;
    op_cls = UNARY_CLS_MAP.get(op_tok) if op_tok else None;
    if op_cls {
        self.set_out(
            nd,
            [
                self.nod(
                    'UnaryOp',
                    ['op', 'operand'],
                    [self.nod(op_cls, [], []), self.val(nd.operand)]
                )
            ]
        );
        return;
    } elif (nd.op.name in [Tok.PIPE_FWD, Tok.KW_SPAWN, Tok.A_PIPE_FWD]) {
        self.set_out(
            nd,
            [
                self.nod(
                    'Call', ['func', 'args', 'keywords'], [self.val(nd.operand), [], []]
                )
            ]
        );
    } elif (nd.op.name in [Tok.STAR_MUL]) {
        ctx_val = self.py_ctx(nd.operand)
            if isinstance(nd.operand, uni.AstSymbolNode)
            else self.nod('Load', [], []);
        self.set_out(
            nd, [self.nod('Starred', ['value', 'ctx'], [self.val(nd.operand), ctx_val])]
        );
    } elif (nd.op.name in [Tok.STAR_POW]) {
        self.set_out(nd, self.outs(nd.operand));
    } else {
        self.ice("Unknown Unary operator " + str(nd.op.value));
    }
}

impl JcirGenPass.exit_if_else_expr(nd: uni.IfElseExpr) -> None {
    self.set_out(
        nd,
        [
            self.nod(
                'IfExp',
                ['test', 'body', 'orelse'],
                [self.val(nd.condition), self.val(nd.value), self.val(nd.else_value)]
            )
        ]
    );
}

impl JcirGenPass.exit_multi_string(nd: uni.MultiString) -> None {
    if (
        (len(nd.strings) == 1)
        and isinstance(nd.strings[0], uni.FString)
        and self.outs(nd.strings[0])
    ) {
        self.set_out(nd, self.outs(nd.strings[0]));
        return;
    }
    def get_pieces(str_seq: Sequence) -> list {
        pieces: list = [];
        for i in str_seq {
            if isinstance(i, uni.String) {
                pieces.append(i.lit_value);
            } elif isinstance(i, (uni.FString, uni.ExprStmt)) {
                py_val = self.val(i);
                if py_val.cls == 'JoinedStr' {
                    for v in py_val.get('values') {
                        pieces.append(v);
                    }
                } else {
                    pieces.append(py_val);
                }
            } elif (
                isinstance(i, uni.Token) and (i.name in [Tok.LBRACE, Tok.RBRACE])
            ) {
                continue;
            } else {
                raise self.ice('Multi string made of something weird.');
            }
        }
        return pieces;
    }
    combined_multi: list = [];
    for item in get_pieces(nd.strings) {
        if (
            combined_multi
            and isinstance(item, str)
            and isinstance(combined_multi[-1], str)
        ) {
            combined_multi[-1] += item;
        } elif (
            combined_multi
            and isinstance(item, bytes)
            and isinstance(combined_multi[-1], bytes)
        ) {
            combined_multi[-1] += item;
        } else {
            combined_multi.append(item);
        }
    }
    for (i, v) in enumerate(combined_multi) {
        if isinstance(v, (str, bytes)) {
            combined_multi[i] = self.const_of(v);
        }
    }
    merged: list = [];
    for v in combined_multi {
        if (
            merged
            and isinstance(v, CgNode)
            and v.cls == 'Constant'
            and isinstance(v.get('value'), str)
            and isinstance(merged[-1], CgNode)
            and merged[-1].cls == 'Constant'
            and isinstance(merged[-1].get('value'), str)
        ) {
            merged[-1] = self.const_of(merged[-1].get('value') + v.get('value'));
        } else {
            merged.append(v);
        }
    }
    if not merged {
        self.set_out(nd, [self.const_of('')]);
    } elif (
        len(merged) == 1
        and isinstance(merged[0], CgNode)
        and merged[0].cls == 'Constant'
    ) {
        self.set_out(nd, [merged[0]]);
    } else {
        self.set_out(nd, [self.nod('JoinedStr', ['values'], [merged])]);
    }
}

impl JcirGenPass.exit_f_string(nd: uni.FString) -> None {
    self.set_out(
        nd,
        [
            self.nod(
                'JoinedStr',
                ['values'],
                [
                    [
                        self.val(part)
                        for part in nd.parts
                        if self.outs(part)
                    ]
                ]
            )
        ]
    );
}

impl JcirGenPass.exit_formatted_value(nd: uni.FormattedValue) -> None {
    self.set_out(
        nd,
        [
            self.nod(
                'FormattedValue',
                ['value', 'conversion', 'format_spec'],
                [
                    self.val(nd.format_part),
                    nd.conversion,
                    self.val(nd.format_spec) if nd.format_spec else None
                ]
            )
        ]
    );
}

impl JcirGenPass.exit_list_val(nd: uni.ListVal) -> None {
    self.set_out(
        nd,
        [
            self.nod(
                'List',
                ['elts', 'ctx'],
                [[self.val(v) for v in nd.values], self.py_ctx(nd)]
            )
        ]
    );
}

impl JcirGenPass.exit_set_val(nd: uni.SetVal) -> None {
    self.set_out(nd, [self.nod('Set', ['elts'], [[self.val(i) for i in nd.values]])]);
}

impl JcirGenPass.exit_tuple_val(nd: uni.TupleVal) -> None {
    self.set_out(
        nd,
        [
            self.nod(
                'Tuple',
                ['elts', 'ctx'],
                [[self.val(i) for i in nd.values], self.py_ctx(nd)]
            )
        ]
    );
}

impl JcirGenPass.exit_dict_val(nd: uni.DictVal) -> None {
    self.set_out(
        nd,
        [
            self.nod(
                'Dict',
                ['keys', 'values'],
                [
                    [self.val(x.key) if x.key else None for x in nd.kv_pairs],
                    [self.val(x.value) for x in nd.kv_pairs]
                ]
            )
        ]
    );
}

impl JcirGenPass.exit_k_v_pair(nd: uni.KVPair) -> None {
    ;
}

impl JcirGenPass.exit_k_w_pair(nd: uni.KWPair) -> None {
    self.set_out(
        nd,
        [
            self.nod(
                'keyword',
                ['arg', 'value'],
                [
                    self.val(nd.key).get('id')
                        if (nd.key and self.val(nd.key).cls == 'Name')
                        else None,
                    self.val(nd.value)
                ]
            )
        ]
    );
}

impl JcirGenPass.exit_inner_compr(nd: uni.InnerCompr) -> None {
    self.set_out(
        nd,
        [
            self.nod(
                'comprehension',
                ['target', 'iter', 'ifs', 'is_async'],
                [
                    self.val(nd.target),
                    self.val(nd.collection),
                    [self.val(x) for x in nd.conditional] if nd.conditional else [],
                    0
                ]
            )
        ]
    );
}

impl JcirGenPass.exit_list_compr(nd: uni.ListCompr) -> None {
    self.set_out(
        nd,
        [
            self.nod(
                'ListComp',
                ['elt', 'generators'],
                [self.val(nd.out_expr), [self.val(i) for i in nd.compr]]
            )
        ]
    );
}

impl JcirGenPass.exit_gen_compr(nd: uni.GenCompr) -> None {
    self.set_out(
        nd,
        [
            self.nod(
                'GeneratorExp',
                ['elt', 'generators'],
                [self.val(nd.out_expr), [self.val(i) for i in nd.compr]]
            )
        ]
    );
}

impl JcirGenPass.exit_set_compr(nd: uni.SetCompr) -> None {
    self.set_out(
        nd,
        [
            self.nod(
                'SetComp',
                ['elt', 'generators'],
                [self.val(nd.out_expr), [self.val(i) for i in nd.compr]]
            )
        ]
    );
}

impl JcirGenPass.exit_dict_compr(nd: uni.DictCompr) -> None {
    self.set_out(
        nd,
        [
            self.nod(
                'DictComp',
                ['key', 'value', 'generators'],
                [
                    self.val(nd.kv_pair.key) if nd.kv_pair.key else self.const_of(None),
                    self.val(nd.kv_pair.value),
                    [self.val(i) for i in nd.compr]
                ]
            )
        ]
    );
}

impl JcirGenPass.exit_atom_trailer(nd: uni.AtomTrailer) -> None {
    if nd.is_genai {
        self.set_out(nd, []);
    }
    if nd.is_attr {
        if isinstance(nd.right, uni.AstSymbolNode) {
            self.set_out(
                nd,
                [
                    self.nod(
                        'Attribute',
                        ['value', 'attr', 'ctx'],
                        [
                            self.val(nd.target),
                            self._py_name(nd.right),
                            self.py_ctx(nd.right)
                        ]
                    )
                ]
            );
        } else {
            self.emit(E5008);
        }
    } elif isinstance(nd.right, uni.FilterCompr) {
        self._refuse(nd, 'filter comprehension trailer');
    } elif isinstance(nd.right, uni.AssignCompr) {
        self._refuse(nd, 'assign comprehension trailer');
    } else {
        self.set_out(
            nd,
            [
                self.nod(
                    'Subscript',
                    ['value', 'slice', 'ctx'],
                    [
                        self.val(nd.target),
                        self.val(nd.right),
                        self.py_ctx(nd.right)
                            if isinstance(nd.right, uni.AstSymbolNode)
                            else self.nod('Load', [], [])
                    ]
                )
            ]
        );
        if self.val(nd.right).hasf('ctx') {
            self.val(nd.right).setf('ctx', self.nod('Load', [], []));
        }
    }
    if nd.is_null_ok {
        walrus_assign = self.nod(
            'NamedExpr',
            ['target', 'value'],
            [
                self.nod(
                    'Name', ['id', 'ctx'], ['__jac_tmp', self.nod('Store', [], [])]
                ),
                self.val(nd.target)
            ]
        );
        tmp_ref = self.name_load('__jac_tmp');
        none_const = self.const_of(None);
        cur = self.val(nd);
        body_expr: CgNode;
        if cur.cls == 'Attribute' {
            body_expr = self.nod(
                'Call',
                ['func', 'args', 'keywords'],
                [
                    self.name_load('getattr'),
                    [tmp_ref, self.const_of(cur.get('attr')), none_const],
                    []
                ]
            );
        } elif cur.cls == 'Call' {
            self._refuse(nd, 'null-ok call trailer');
        } elif cur.cls == 'Subscript' {
            body_expr = self.nod(
                'Call',
                ['func', 'args', 'keywords'],
                [self.jaclib_obj('safe_subscript'), [tmp_ref, cur.get('slice')], []]
            );
        } else {
            cur.setf('value', tmp_ref);
            body_expr = cur;
        }
        self.set_out(
            nd,
            [
                self.nod(
                    'IfExp',
                    ['test', 'body', 'orelse'],
                    [
                        self.nod(
                            'Compare',
                            ['left', 'ops', 'comparators'],
                            [walrus_assign, [self.nod('IsNot', [], [])], [none_const]]
                        ),
                        body_expr,
                        none_const
                    ]
                )
            ]
        );
    }
}

impl JcirGenPass.exit_atom_unit(nd: uni.AtomUnit) -> None {
    if (
        isinstance(nd.value, uni.Ability)
        and self.outs(nd.value)
        and self.val(nd.value).cls in ('FunctionDef', 'AsyncFunctionDef')
    ) {
        self.set_out(nd, [self._function_expr_from_def(self.val(nd.value), nd)]);
        return;
    }
    self.set_out(nd, self.outs(nd.value));
}

impl JcirGenPass.gen_call_args(nd: uni.FuncCall) -> tuple[(list, list)] {
    args: list = [];
    keywords: list = [];
    if nd.params {
        for x in nd.params {
            if (isinstance(x, uni.UnaryExpr) and (x.op.name == Tok.STAR_POW)) {
                keywords.append(
                    self.nod('keyword', ['value'], [self.val(x.operand)], jac_node=x)
                );
            } elif isinstance(x, uni.Expr) {
                args.append(self.val(x));
            } elif (isinstance(x, uni.KWPair) and self.val(x).cls == 'keyword') {
                keywords.append(self.val(x));
            } else {
                self.ice('Invalid Parameter');
            }
        }
    }
    return (args, keywords);
}

impl JcirGenPass.exit_func_call(nd: uni.FuncCall) -> None {
    if nd.genai_call {
        self._refuse(nd, 'genai call expression');
    }
    if (
        isinstance(nd.target, uni.AtomTrailer)
        and nd.target.is_attr
        and isinstance(nd.target.right, uni.Name)
        and nd.target.right.sym_name == "chunks"
        and nd.params is not None
        and len(nd.params) == 1
    ) {
        _ck_m = "";
        try {
            _ck_m = nd.target.target.type.mangle()
                if nd.target.target.type is not None
                else "";
        } except Exception {
            _ck_m = "";
        }
        if (
            _ck_m == "list"
            or _ck_m.startswith("list[")
            or (_ck_m in ("", "Unknown", "None") and nd.target.right.sym is None)
        ) {
            self.set_out(
                nd,
                [
                    self.nod(
                        'Call',
                        ['func', 'args', 'keywords'],
                        [
                            self.builtin_name('_jac_chunks'),
                            [self.val(nd.target.target), self.val(nd.params[0])],
                            []
                        ]
                    )
                ]
            );
            return;
        }
    }
    func = self.val(nd.target);
    (args, keywords) = self.gen_call_args(nd);
    self.set_out(
        nd, [self.nod('Call', ['func', 'args', 'keywords'], [func, args, keywords])]
    );
}

impl JcirGenPass.exit_index_slice(nd: uni.IndexSlice) -> None {
    if nd.is_range {
        if (len(nd.slices) > 1) {
            self.set_out(
                nd,
                [
                    self.nod(
                        'Tuple',
                        ['elts', 'ctx'],
                        [
                            [
                                self.nod(
                                    'Slice',
                                    ['lower', 'upper', 'step'],
                                    [
                                        self.val(slc.start) if slc.start else None,
                                        self.val(slc.stop) if slc.stop else None,
                                        self.val(slc.step) if slc.step else None
                                    ]
                                ) for slc in nd.slices
                            ],
                            self.nod('Load', [], [])
                        ]
                    )
                ]
            );
        } elif (len(nd.slices) == 1) {
            slc = nd.slices[0];
            self.set_out(
                nd,
                [
                    self.nod(
                        'Slice',
                        ['lower', 'upper', 'step'],
                        [
                            self.val(slc.start) if slc.start else None,
                            self.val(slc.stop) if slc.stop else None,
                            self.val(slc.step) if slc.step else None
                        ]
                    )
                ]
            );
        }
    } elif ((len(nd.slices) > 0) and (nd.slices[0].start is not None)) {
        self.set_out(nd, self.outs(nd.slices[0].start));
    } else {
        self.set_out(nd, []);
    }
}

impl JcirGenPass.exit_special_var_ref(nd: uni.SpecialVarRef) -> None {
    if (nd.name == Tok.KW_SUPER) {
        self.set_out(
            nd,
            [
                self.nod(
                    'Call',
                    ['func', 'args', 'keywords'],
                    [
                        self.nod('Name', ['id', 'ctx'], ['super', self.py_ctx(nd)]),
                        [],
                        []
                    ]
                )
            ]
        );
    } elif (nd.name == Tok.KW_ROOT) {
        self._refuse(nd, 'walker root reference');
    } else {
        self.set_out(
            nd, [self.nod('Name', ['id', 'ctx'], [self._py_name(nd), self.py_ctx(nd)])]
        );
    }
}

impl JcirGenPass.exit_edge_ref_trailer(nd: uni.EdgeRefTrailer) -> None {
    self._refuse(nd, 'edge reference trailer');
}

impl JcirGenPass.exit_edge_op_ref(nd: uni.EdgeOpRef) -> None {
    self._refuse(nd, 'edge operator reference');
}

impl JcirGenPass.exit_disconnect_op(nd: uni.DisconnectOp) -> None {
    self._refuse(nd, 'edge disconnect operator');
}

impl JcirGenPass.exit_connect_op(nd: uni.ConnectOp) -> None {
    self._refuse(nd, 'edge connect operator');
}

impl JcirGenPass.exit_filter_compr(nd: uni.FilterCompr) -> None {
    self._refuse(nd, 'filter comprehension');
}

impl JcirGenPass.exit_assign_compr(nd: uni.AssignCompr) -> None {
    self._refuse(nd, 'assign comprehension');
}

impl JcirGenPass.exit_match_stmt(nd: uni.MatchStmt) -> None {
    self._refuse(nd, 'match statement');
}

impl JcirGenPass.exit_token(nd: uni.Token) -> None {
    tok = Tok(nd.name) if (nd.name in Tok.__members__) else None;
    op_cls = TOKEN_CLS_MAP.get(tok) if tok else None;
    if op_cls {
        self.set_out(nd, [self.nod(op_cls, [], [])]);
    }
}

impl JcirGenPass.exit_name(nd: uni.Name) -> None {
    name = nd.sym_name;
    if name == "Self" {
        resolved = self._resolve_self_type(nd);
        if resolved is not None {
            self.set_out(nd, [resolved]);
            return;
        }
    }
    if (name in self._get_builtin_names()) {
        self.builtin_imports.add(name);
    }
    if (name in self._get_ambient_typing_names()) {
        self.typing_imports.add(name);
    }
    self.set_out(nd, [self.nod('Name', ['id', 'ctx'], [name, self.py_ctx(nd)])]);
}

impl JcirGenPass._resolve_self_type(nd: uni.Name) -> (CgNode | None) {
    arch = nd.find_parent_of_type(uni.Archetype);
    enum_parent = nd.find_parent_of_type(uni.Enum) if not arch else None;
    enclosing = arch or enum_parent;
    enclosing_name: str | None = None;
    if enclosing {
        enclosing_name = enclosing.name.sym_name if enclosing.name else None;
    }
    if not enclosing_name {
        impl_def = nd.find_parent_of_type(uni.ImplDef);
        if (impl_def and impl_def.target and len(impl_def.target) > 0) {
            enclosing_name = impl_def.target[0].sym_name
                if isinstance(impl_def.target[0], uni.NameAtom)
                else None;
        }
    }
    if not enclosing_name {
        return None;
    }
    if self._is_in_type_annotation(nd) {
        return self.name_load(enclosing_name);
    }
    ability = nd.find_parent_of_type(uni.Ability);
    impl_def = nd.find_parent_of_type(uni.ImplDef);
    is_in_classmethod = False;
    is_in_method = False;
    if ability {
        is_in_classmethod = ability.is_cls_method;
        is_in_method = ability.is_method
            and not ability.is_static
            and not ability.is_cls_method;
    } elif (
        impl_def and impl_def.decl_link and isinstance(impl_def.decl_link, uni.Ability)
    ) {
        is_in_classmethod = impl_def.decl_link.is_cls_method;
        is_in_method = (
            impl_def.decl_link.is_method
            and not impl_def.decl_link.is_static
            and not impl_def.decl_link.is_cls_method
        );
    }
    if is_in_classmethod {
        return self.nod('Name', ['id', 'ctx'], ['cls', self.py_ctx(nd)]);
    }
    if is_in_method {
        return self.nod(
            'Call',
            ['func', 'args', 'keywords'],
            [self.name_load('type'), [self.name_load('self')], []]
        );
    }
    return self.nod('Name', ['id', 'ctx'], [enclosing_name, self.py_ctx(nd)]);
}

impl JcirGenPass._is_in_type_annotation(nd: uni.UniNode) -> bool {
    parent = nd.parent;
    while parent {
        if isinstance(parent, uni.FuncSignature) {
            if parent.return_type {
                return self._is_descendant_of(nd, parent.return_type);
            }
        }
        if isinstance(parent, uni.ParamVar) {
            if parent.type_tag {
                return True;
            }
        }
        if isinstance(parent, uni.HasVar) {
            if parent.type_tag {
                return True;
            }
        }
        if isinstance(parent, uni.SubTag) {
            return True;
        }
        parent = parent.parent;
    }
    return False;
}

impl JcirGenPass._is_descendant_of(nd: uni.UniNode, ancestor: uni.UniNode) -> bool {
    if nd is ancestor {
        return True;
    }
    current = nd.parent;
    while current {
        if current is ancestor {
            return True;
        }
        current = current.parent;
    }
    return False;
}

impl JcirGenPass.exit_float(nd: uni.Float) -> None {
    self.set_out(nd, [self.const_of(float(nd.value))]);
}

impl JcirGenPass.exit_int(nd: uni.Int) -> None {
    self.set_out(nd, [self.const_of(int(nd.value, 0))]);
}

impl JcirGenPass.exit_string(nd: uni.String) -> None {
    self.set_out(nd, [self.const_of(nd.lit_value)]);
}

impl JcirGenPass.exit_bool(nd: uni.Bool) -> None {
    self.set_out(nd, [self.const_of(nd.value == 'True')]);
}

impl JcirGenPass.exit_builtin_type(nd: uni.BuiltinType) -> None {
    if nd.sym_name == "any" {
        parent = nd.parent;
        is_call_func = (
            parent is not None
            and isinstance(parent, uni.FuncCall)
            and parent.target is nd
        );
        if not is_call_func {
            self.typing_imports.add("Any");
            self.set_out(
                nd, [self.nod('Name', ['id', 'ctx'], ["Any", self.py_ctx(nd)])]
            );
            return;
        }
    }
    self.set_out(nd, [self.nod('Name', ['id', 'ctx'], [nd.sym_name, self.py_ctx(nd)])]);
}

impl JcirGenPass.exit_null(nd: uni.Null) -> None {
    self.set_out(nd, [self.const_of(None)]);
}

impl JcirGenPass.exit_ellipsis(nd: uni.Ellipsis) -> None {
    self.set_out(nd, [self.const_of(Ellipsis)]);
}

impl JcirGenPass.exit_jsx_element(nd: uni.JsxElement) -> None {
    self._refuse(nd, 'jsx element');
}

impl JcirGenPass.exit_semi(nd: uni.Semi) -> None {
    ;
}

impl JcirGenPass.exit_comment_token(nd: uni.CommentToken) -> None {
    ;
}
