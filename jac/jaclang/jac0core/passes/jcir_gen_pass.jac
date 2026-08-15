import textwrap;
import from typing { ClassVar }
import from collections.abc { Sequence }
import jaclang.jac0core.unitree as uni;
import from jaclang.jac0core.constant { CodeContext, is_borrow_kind, OwnershipKind }
import from jaclang.jac0core.constant { Tokens as Tok }
import from jaclang.jac0core.codegen_ir { CodegenIrWriter }
import from jaclang.jac0core.passes.ast_gen { BaseAstGenPass }
import from jaclang.jac0core.diagnostics {
    E5001,
    E5002,
    E5003,
    E5004,
    E5005,
    E5006,
    E5007,
    E5008,
    E5098
}

glob TOKEN_CLS_MAP: dict[(Tok, str)] = {
         Tok.KW_AND: 'And',
         Tok.KW_OR: 'Or',
         Tok.PLUS: 'Add',
         Tok.ADD_EQ: 'Add',
         Tok.BW_AND: 'BitAnd',
         Tok.BW_AND_EQ: 'BitAnd',
         Tok.BW_OR: 'BitOr',
         Tok.BW_OR_EQ: 'BitOr',
         Tok.BW_XOR: 'BitXor',
         Tok.BW_XOR_EQ: 'BitXor',
         Tok.DIV: 'Div',
         Tok.DIV_EQ: 'Div',
         Tok.FLOOR_DIV: 'FloorDiv',
         Tok.FLOOR_DIV_EQ: 'FloorDiv',
         Tok.LSHIFT: 'LShift',
         Tok.LSHIFT_EQ: 'LShift',
         Tok.MOD: 'Mod',
         Tok.MOD_EQ: 'Mod',
         Tok.STAR_MUL: 'Mult',
         Tok.MUL_EQ: 'Mult',
         Tok.DECOR_OP: 'MatMult',
         Tok.MATMUL_EQ: 'MatMult',
         Tok.STAR_POW: 'Pow',
         Tok.STAR_POW_EQ: 'Pow',
         Tok.RSHIFT: 'RShift',
         Tok.RSHIFT_EQ: 'RShift',
         Tok.MINUS: 'Sub',
         Tok.SUB_EQ: 'Sub',
         Tok.BW_NOT: 'Invert',
         Tok.NOT: 'Not',
         Tok.EQ: 'NotEq',
         Tok.EE: 'Eq',
         Tok.GT: 'Gt',
         Tok.GTE: 'GtE',
         Tok.KW_IN: 'In',
         Tok.KW_IS: 'Is',
         Tok.KW_ISN: 'IsNot',
         Tok.LT: 'Lt',
         Tok.LTE: 'LtE',
         Tok.NE: 'NotEq',
         Tok.KW_NIN: 'NotIn'
     },
     UNARY_CLS_MAP: dict[(Tok, str)] = {
         Tok.NOT: 'Not',
         Tok.BW_NOT: 'Invert',
         Tok.PLUS: 'UAdd',
         Tok.MINUS: 'USub'
     };

obj CgNode {
    has cls: str,
        keys: list,
        vals: list,
        l1: int = 0,
        c1: int = 0,
        l2: int = 0,
        c2: int = 0,
        jac_src: any = None;

    def get(key: str) -> any {
        i = 0;
        while i < len(self.keys) {
            if self.keys[i] == key {
                return self.vals[i];
            }
            i += 1;
        }
        raise KeyError(f"CgNode {self.cls} has no field {key}");
    }

    def setf(key: str, value: any) -> None {
        i = 0;
        while i < len(self.keys) {
            if self.keys[i] == key {
                self.vals[i] = value;
                return;
            }
            i += 1;
        }
        self.keys.append(key);
        self.vals.append(value);
    }

    def hasf(key: str) -> bool {
        return key in self.keys;
    }
}

obj CgSplice {
    has src: str,
        offset: int = 0;
}

def write_ir_value(w: CodegenIrWriter, v: object) {
    if v is None {
        w.emit_none();
    } elif v is Ellipsis {
        w.emit_ellipsis();
    } elif isinstance(v, CgNode) {
        for item in v.vals {
            write_ir_value(w, item);
        }
        w.emit_loc_needed(v.l1, v.c1, v.l2, v.c2);
        w.emit_node(v.cls, v.keys);
    } elif isinstance(v, CgSplice) {
        w.emit_parse_splice(v.src, v.offset);
    } elif isinstance(v, bool) {
        if v {
            w.emit_true();
        } else {
            w.emit_false();
        }
    } elif isinstance(v, int) {
        w.emit_int(v);
    } elif isinstance(v, float) {
        w.emit_float(v);
    } elif isinstance(v, str) {
        w.emit_str(v);
    } elif isinstance(v, bytes) {
        w.emit_bytes(v);
    } elif isinstance(v, list) {
        for item in v {
            write_ir_value(w, item);
        }
        w.emit_list(len(v));
    } else {
        raise ValueError(
            "JCIR emitter produced a value outside the v1 constant "
                + f"domain: {type(v).__name__}"
        );
    }
}

def module_container_bytes(mod_path: str, root_node: CgNode) -> bytes {
    w = CodegenIrWriter();
    w.begin_module(mod_path);
    write_ir_value(w, root_node);
    w.end_module();
    return w.to_bytes();
}

obj JcirGenPass(BaseAstGenPass[CgNode]) {
    has already_added: list[str] = [],
        jaclib_imports: set[str] postinit,
        builtin_imports: set[str] postinit,
        typing_imports: set[str] postinit,
        _lambda_counter: int = 0,
        _hoist_frames: list postinit,
        preamble: list postinit,
        _sink: dict postinit,
        _module_root: (CgNode | None) = None,
        _module_path: str = "";

    with entry {
        _builtin_names: ClassVar[(set[str] | None)] = None;
        _ambient_typing_names: ClassVar[(set[str] | None)] = None;
    }

    @classmethod
    def _get_builtin_names(cls: any) -> set[str];

    @classmethod
    def _get_ambient_typing_names(cls: any) -> set[str];

    def postinit -> None;
    def enter_node(nd: uni.UniNode) -> None;
    def exit_node(nd: uni.UniNode) -> None;
    def _is_native_ext_module -> bool;
    def _refuse(nd: (uni.UniNode | None), construct: str) -> None;
    def nod(
        cls_name: str, keys: list, vals: list, jac_node: (uni.UniNode | None) = None
    ) -> CgNode;

    def nod_loc(
        cls_name: str, keys: list, vals: list, l1: int, c1: int, l2: int, c2: int
    ) -> CgNode;

    def outs(nd: uni.UniNode) -> list;
    def set_out(nd: uni.UniNode, items: list) -> None;
    def val(nd: uni.UniNode) -> CgNode;
    def const_of(value: any, jac_node: (uni.UniNode | None) = None) -> CgNode;
    def name_load(name_id: str, jac_node: (uni.UniNode | None) = None) -> CgNode;
    def _with_ctx(cgn: CgNode, ctx_cls: str) -> CgNode;
    def jaclib_obj(obj_name: str) -> CgNode;
    def builtin_name(name: str) -> CgNode;
    def _add_preamble_once(key: str, cgn: CgNode) -> None;
    def needs_enum_kind(kind: str) -> None;
    def _py_name(nd: uni.AstSymbolNode) -> str;
    def py_ctx(nd: uni.AstSymbolNode) -> CgNode;
    def _next_lambda_name -> str;
    def _push_hoist_frame -> None;
    def _hoist_lambda(func_def: CgNode) -> None;
    def _ancestor_is_one_of(
        nd: (uni.UniNode | None), roots: list[uni.UniNode]
    ) -> bool;

    def _split_scope_hoists(
        hoists: list, sig_roots: list[uni.UniNode]
    ) -> tuple[(list, list)];

    def _drain_module_hoists -> list;
    def _report_leaked_hoisted_lambdas -> None;
    def _function_expr_from_def(func_def: CgNode, jac_node: uni.UniNode) -> CgNode;
    def _check_sem_decorator(nd: uni.UniNode) -> None;
    def resolve_switch_pattern(pattern: uni.MatchPattern, target: uni.Expr) -> CgNode;
    def resolve_switch_stmt(nd: uni.SwitchStmt) -> list;
    def resolve_stmt_block(
        nd: Sequence[uni.CodeBlockStmt] | Sequence[uni.EnumBlockStmt] | None,
        doc: (uni.String | None) = None
    ) -> list;

    def scope_directive_stmts(scopes: list) -> list;
    def _resolve_self_type(nd: uni.Name) -> (CgNode | None);
    def _is_in_type_annotation(nd: uni.UniNode) -> bool;
    def _is_descendant_of(nd: uni.UniNode, ancestor: uni.UniNode) -> bool;
    def gen_call_args(nd: uni.FuncCall) -> tuple[(list, list)];
    def _delete_targets(nd: uni.DeleteStmt) -> list;
    def ir_bytes -> bytes;
    def exit_sub_tag(nd: uni.SubTag[uni.T]) -> None;
    def exit_module(nd: uni.Module) -> None;
    def exit_type_param(nd: uni.TypeParam) -> None;
    def exit_type_alias(nd: uni.TypeAlias) -> None;
    def exit_global_vars(nd: uni.GlobalVars) -> None;
    def enter_test(nd: uni.Test) -> None;
    def exit_test(nd: uni.Test) -> None;
    def exit_module_code(nd: uni.ModuleCode) -> None;
    def exit_py_inline_code(nd: uni.PyInlineCode) -> None;
    def exit_import(nd: uni.Import) -> None;
    def exit_module_path(nd: uni.ModulePath) -> None;
    def exit_module_item(nd: uni.ModuleItem) -> None;
    def enter_archetype(nd: uni.Archetype) -> None;
    def exit_archetype(nd: uni.Archetype) -> None;
    def enter_enum(nd: uni.Enum) -> None;
    def exit_enum(nd: uni.Enum) -> None;
    def enter_ability(nd: uni.Ability) -> None;
    def exit_ability(nd: uni.Ability) -> None;
    def exit_impl_def(nd: uni.ImplDef) -> None;
    def exit_sem_def(nd: uni.SemDef) -> None;
    def exit_func_signature(nd: uni.FuncSignature) -> None;
    def exit_event_signature(nd: uni.EventSignature) -> None;
    def exit_param_var(nd: uni.ParamVar) -> None;
    def exit_arch_has(nd: uni.ArchHas) -> None;
    def exit_has_var(nd: uni.HasVar) -> None;
    def exit_typed_ctx_block(nd: uni.TypedCtxBlock) -> None;
    def exit_open_stmt(nd: uni.OpenStmt) -> None;
    def exit_if_stmt(nd: uni.IfStmt) -> None;
    def exit_else_if(nd: uni.ElseIf) -> None;
    def exit_else_stmt(nd: uni.ElseStmt) -> None;
    def exit_expr_stmt(nd: uni.ExprStmt) -> None;
    def exit_concurrent_expr(nd: uni.ConcurrentExpr) -> None;
    def exit_try_stmt(nd: uni.TryStmt) -> None;
    def exit_except(nd: uni.Except) -> None;
    def exit_finally_stmt(nd: uni.FinallyStmt) -> None;
    def exit_iter_for_stmt(nd: uni.IterForStmt) -> None;
    def exit_in_for_stmt(nd: uni.InForStmt) -> None;
    def exit_while_stmt(nd: uni.WhileStmt) -> None;
    def exit_forever_stmt(nd: uni.ForeverStmt) -> None;
    def exit_with_stmt(nd: uni.WithStmt) -> None;
    def exit_expr_as_item(nd: uni.ExprAsItem) -> None;
    def exit_raise_stmt(nd: uni.RaiseStmt) -> None;
    def exit_assert_stmt(nd: uni.AssertStmt) -> None;
    def exit_ctrl_stmt(nd: uni.CtrlStmt) -> None;
    def exit_delete_stmt(nd: uni.DeleteStmt) -> None;
    def exit_report_stmt(nd: uni.ReportStmt) -> None;
    def exit_return_stmt(nd: uni.ReturnStmt) -> None;
    def exit_yield_expr(nd: uni.YieldExpr) -> None;
    def exit_visit_stmt(nd: uni.VisitStmt) -> None;
    def exit_disengage_stmt(nd: uni.DisengageStmt) -> None;
    def exit_await_expr(nd: uni.AwaitExpr) -> None;
    def exit_cast_expr(nd: uni.CastExpr) -> None;
    def exit_assignment(nd: uni.Assignment) -> None;
    def exit_binary_expr(nd: uni.BinaryExpr) -> None;
    def translate_jac_bin_op(nd: uni.BinaryExpr) -> list;
    def exit_compare_expr(nd: uni.CompareExpr) -> None;
    def exit_cfg_expr(nd: uni.CfgExpr) -> None;
    def exit_bool_expr(nd: uni.BoolExpr) -> None;
    def exit_lambda_expr(nd: uni.LambdaExpr) -> None;
    def _emit_lambda_expr_body(nd: uni.LambdaExpr, body_node: uni.Expr) -> None;
    def _remove_lambda_param_annotations(signature: uni.FuncSignature) -> None;
    def exit_unary_expr(nd: uni.UnaryExpr) -> None;
    def _emit_jobj_ref(nd: uni.UnaryExpr) -> None;
    def exit_if_else_expr(nd: uni.IfElseExpr) -> None;
    def exit_multi_string(nd: uni.MultiString) -> None;
    def exit_f_string(nd: uni.FString) -> None;
    def exit_formatted_value(nd: uni.FormattedValue) -> None;
    def exit_list_val(nd: uni.ListVal) -> None;
    def exit_set_val(nd: uni.SetVal) -> None;
    def exit_tuple_val(nd: uni.TupleVal) -> None;
    def exit_dict_val(nd: uni.DictVal) -> None;
    def exit_k_v_pair(nd: uni.KVPair) -> None;
    def exit_k_w_pair(nd: uni.KWPair) -> None;
    def exit_inner_compr(nd: uni.InnerCompr) -> None;
    def exit_list_compr(nd: uni.ListCompr) -> None;
    def exit_gen_compr(nd: uni.GenCompr) -> None;
    def exit_set_compr(nd: uni.SetCompr) -> None;
    def exit_dict_compr(nd: uni.DictCompr) -> None;
    def exit_atom_trailer(nd: uni.AtomTrailer) -> None;
    def exit_atom_unit(nd: uni.AtomUnit) -> None;
    def exit_func_call(nd: uni.FuncCall) -> None;
    def exit_index_slice(nd: uni.IndexSlice) -> None;
    def exit_special_var_ref(nd: uni.SpecialVarRef) -> None;
    def exit_edge_ref_trailer(nd: uni.EdgeRefTrailer) -> None;
    def exit_edge_op_ref(nd: uni.EdgeOpRef) -> None;
    def exit_disconnect_op(nd: uni.DisconnectOp) -> None;
    def exit_connect_op(nd: uni.ConnectOp) -> None;
    def exit_filter_compr(nd: uni.FilterCompr) -> None;
    def exit_assign_compr(nd: uni.AssignCompr) -> None;
    def exit_match_stmt(nd: uni.MatchStmt) -> None;
    def exit_token(nd: uni.Token) -> None;
    def exit_name(nd: uni.Name) -> None;
    def exit_float(nd: uni.Float) -> None;
    def exit_int(nd: uni.Int) -> None;
    def exit_string(nd: uni.String) -> None;
    def exit_bool(nd: uni.Bool) -> None;
    def exit_builtin_type(nd: uni.BuiltinType) -> None;
    def exit_null(nd: uni.Null) -> None;
    def exit_ellipsis(nd: uni.Ellipsis) -> None;
    def exit_jsx_element(nd: uni.JsxElement) -> None;
    def exit_semi(nd: uni.Semi) -> None;
    def exit_comment_token(nd: uni.CommentToken) -> None;
}
