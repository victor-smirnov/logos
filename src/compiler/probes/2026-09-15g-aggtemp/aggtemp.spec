name: pt-decls
file: src/compiler/sema_impl.hpp
---
    std::unordered_set<const void*> extending_borrow_nodes_;
    void mark_extending_borrows(writ::TinyMapView e);
---
    std::unordered_set<const void*> extending_borrow_nodes_;
    void mark_extending_borrows(writ::TinyMapView e);
    // PROBE 2026-09-15g-aggtemp (census + ptidx / pttup / ptlen / ptext / ptmark / ptnoext / ptown / ptownx / ptownxf) — see src/compiler/PROBES.md.
    bool pt_ext_place_ctx_ = false;   // an extending borrow's PLACE operand chain is being lowered
    bool pt_foreach_iter_ = false;    // a for-each ITERABLE is being lowered (ptownxf only)
    static bool pt_door_arm(const char* part) {
        return logos::probe::on(part) || logos::probe::on("ptnoext") || logos::probe::on("ptown") ||
               logos::probe::on("ptownx") || logos::probe::on("ptownxf");
    }
    static bool pt_ext_arm() {
        return logos::probe::on("ptext") || logos::probe::on("ptown") || logos::probe::on("ptownx") ||
               logos::probe::on("ptownxf");
    }
    static bool pt_mark_arm(bool in_foreach_iter) {
        return logos::probe::on("ptmark") || logos::probe::on("ptownx") ||
               (!in_foreach_iter && logos::probe::on("ptownxf"));
    }
    lir::LExprPtr pt_hoist_ext(lir::LExprPtr v, bool is_mut);
    lir::LExprPtr pt_autoref_ext(lir::LExprPtr v, bool is_mut, TypeRef ref_type);
===
name: pt-helpers
file: src/compiler/sema_expr.cpp
---
lir::LExprPtr SemaChecker::materialize_recv_ref(lir::LExprPtr recv, bool is_mut,
                                                TypeRef ref_type) {
---
// PROBE 2026-09-15g-aggtemp (ptext) — see src/compiler/PROBES.md. An EXTENDED temporary: the owner of
// hoist_stmt_temp / autoref_operand, named `__lit_temp_N` (lower_let's extended-temporary spelling) so that
// SemaChecker::lower_stmt neither drops it at the end of the statement nor erases it from the frame.
lir::LExprPtr SemaChecker::pt_hoist_ext(lir::LExprPtr v, bool is_mut) {
    std::string nm = std::format("__lit_temp_{}", destruct_counter_++);
    TypeRef rt = expr_type(v);
    register_stmt_temp(nm, rt, std::move(v), is_mut);
    return builder().var_ref(nm, rt);
}
lir::LExprPtr SemaChecker::pt_autoref_ext(lir::LExprPtr v, bool is_mut, TypeRef ref_type) {
    std::string nm = std::format("__lit_temp_{}", destruct_counter_++);
    TypeRef rt = expr_type(v);
    register_stmt_temp(nm, rt, nullptr, is_mut);
    std::vector<lir_view::StmtRef> blk;
    blk.push_back(builder().stmt_assign(nm, std::move(v), node_line_));
    auto addr = builder().addr_of_temp(builder().var_ref(nm, rt), is_mut, ref_type);
    return builder().block_expr(lir_mirror_block(*cur_prog_, blk), std::move(addr), ref_type);
}

lir::LExprPtr SemaChecker::materialize_recv_ref(lir::LExprPtr recv, bool is_mut,
                                                TypeRef ref_type) {
===
name: pt-field-carrier
file: src/compiler/sema_expr.cpp
---
    auto recv_node = map_of(node.get(la::RECEIVER.code));
    bool mut_ctx = mut_place_ctx_;
    auto recv = mut_ctx ? lower_mut_place(recv_node) : lower_expr(recv_node);
    mut_place_ctx_ = false;
---
    auto recv_node = map_of(node.get(la::RECEIVER.code));
    bool mut_ctx = mut_place_ctx_;
    // PROBE 2026-09-15g-aggtemp (ext carrier) — see src/compiler/PROBES.md.
    const bool pt_ext_here = pt_ext_place_ctx_;
    pt_ext_place_ctx_ = pt_ext_here && is_place_node(recv_node);
    auto recv = mut_ctx ? lower_mut_place(recv_node) : lower_expr(recv_node);
    pt_ext_place_ctx_ = false;
    mut_place_ctx_ = false;
===
name: pt-field-owner
file: src/compiler/sema_expr.cpp
---
    if (cur_stmt_temp_hoist_ && recv && expr_type(recv) &&
        is_move_type(expr_type(recv)) && is_hoistable_temp_rvalue(recv)) {
        recv = hoist_stmt_temp(std::move(recv), false);  // field recv = &self
    }
---
    if (cur_stmt_temp_hoist_ && recv && expr_type(recv) &&
        is_move_type(expr_type(recv)) && is_hoistable_temp_rvalue(recv)) {
        // PROBE 2026-09-15g-aggtemp (census + ptext) — see src/compiler/PROBES.md.
        logos::probe::census(pt_ext_here ? "ptown.field.ext" : "ptown.field.stmt");
        if (pt_ext_here && pt_ext_arm())
            recv = pt_hoist_ext(std::move(recv), false);
        else
        recv = hoist_stmt_temp(std::move(recv), false);  // field recv = &self
    }
===
name: pt-addr-carrier
file: src/compiler/sema_expr.cpp
---
        // &<expr> — temporary materialization: spill rvalue to stack
        auto inner = lower_expr(child);
---
        // &<expr> — temporary materialization: spill rvalue to stack
        // PROBE 2026-09-15g-aggtemp (ext carrier) — see src/compiler/PROBES.md.
        pt_ext_place_ctx_ = extending_borrow_nodes_.count(node.ptr()) && is_place_node(child);
        auto inner = lower_expr(child);
        pt_ext_place_ctx_ = false;
===
name: pt-addr-owner
file: src/compiler/sema_expr.cpp
---
        if (extending_borrow_nodes_.count(node.ptr()))
            return builder().addr_of_temp(std::move(inner), false, __ty_inner);
        return materialize_recv_ref(std::move(inner), false, __ty_inner);
---
        if (extending_borrow_nodes_.count(node.ptr())) {
            // PROBE 2026-09-15g-aggtemp (census + ptext) — see src/compiler/PROBES.md.
            if (cur_stmt_temp_hoist_ && inner && expr_type(inner) &&
                needs_drop(expr_type(inner)) && is_hoistable_temp_rvalue(inner)) {
                logos::probe::census("ptown.addr.ext");
                if (pt_ext_arm()) return pt_autoref_ext(std::move(inner), false, __ty_inner);
            }
            return builder().addr_of_temp(std::move(inner), false, __ty_inner);
        }
        return materialize_recv_ref(std::move(inner), false, __ty_inner);
===
name: pt-addrmut-carrier
file: src/compiler/sema_expr.cpp
---
        bool saved_mut_place = mut_place_ctx_;
        auto inner = lower_mut_place(child);
        mut_place_ctx_ = saved_mut_place;
---
        bool saved_mut_place = mut_place_ctx_;
        // PROBE 2026-09-15g-aggtemp (ext carrier) — see src/compiler/PROBES.md.
        pt_ext_place_ctx_ = extending_borrow_nodes_.count(expr.ptr()) && is_place_node(child);
        auto inner = lower_mut_place(child);
        pt_ext_place_ctx_ = false;
        mut_place_ctx_ = saved_mut_place;
===
name: pt-addrmut-owner
file: src/compiler/sema_expr.cpp
---
        if (extending_borrow_nodes_.count(expr.ptr()))
            return builder().addr_of_temp(std::move(inner), true, __ty_inner);
        return materialize_recv_ref(std::move(inner), true, __ty_inner);
---
        if (extending_borrow_nodes_.count(expr.ptr())) {
            // PROBE 2026-09-15g-aggtemp (census + ptext) — see src/compiler/PROBES.md.
            if (cur_stmt_temp_hoist_ && inner && expr_type(inner) &&
                needs_drop(expr_type(inner)) && is_hoistable_temp_rvalue(inner)) {
                logos::probe::census("ptown.addrmut.ext");
                if (pt_ext_arm()) return pt_autoref_ext(std::move(inner), true, __ty_inner);
            }
            return builder().addr_of_temp(std::move(inner), true, __ty_inner);
        }
        return materialize_recv_ref(std::move(inner), true, __ty_inner);
===
name: pt-tup
file: src/compiler/sema_expr.cpp
---
        bool tmut_ctx = mut_place_ctx_;
        mut_place_ctx_ = false;
        auto recv = expr.has_key(la::RECEIVER)
            ? (tmut_ctx ? lower_mut_place(map_of(expr.get(la::RECEIVER.code)))
                        : lower_expr(map_of(expr.get(la::RECEIVER.code))))
            : error_expr();
---
        bool tmut_ctx = mut_place_ctx_;
        mut_place_ctx_ = false;
        // PROBE 2026-09-15g-aggtemp (ext carrier + census + pttup / ptext) — see src/compiler/PROBES.md.
        const bool pt_ext_here = pt_ext_place_ctx_;
        pt_ext_place_ctx_ = pt_ext_here && expr.has_key(la::RECEIVER) &&
                            is_place_node(map_of(expr.get(la::RECEIVER.code)));
        auto recv = expr.has_key(la::RECEIVER)
            ? (tmut_ctx ? lower_mut_place(map_of(expr.get(la::RECEIVER.code)))
                        : lower_expr(map_of(expr.get(la::RECEIVER.code))))
            : error_expr();
        pt_ext_place_ctx_ = false;
        if (cur_stmt_temp_hoist_ && recv && expr_type(recv) &&
            is_move_type(expr_type(recv)) && is_hoistable_temp_rvalue(recv)) {
            logos::probe::census(pt_ext_here ? "ptown.tup.ext" : "ptown.tup.stmt");
            if (pt_ext_here && pt_ext_arm())
                recv = pt_hoist_ext(std::move(recv), tmut_ctx);
            else if (pt_door_arm("pttup"))
                recv = hoist_stmt_temp(std::move(recv), tmut_ctx);
        }
===
name: pt-idx
file: src/compiler/sema_expr.cpp
---
    bool mut_ctx = mut_place_ctx_;
    auto recv_node = map_of(node.get(la::RECEIVER.code));
    auto recv = mut_ctx ? lower_mut_place(recv_node) : lower_expr(recv_node);
    mut_place_ctx_ = false;
    auto arr_type = expr_type(recv);
---
    bool mut_ctx = mut_place_ctx_;
    auto recv_node = map_of(node.get(la::RECEIVER.code));
    // PROBE 2026-09-15g-aggtemp (ext carrier + census + ptidx / ptext) — see src/compiler/PROBES.md.
    const bool pt_ext_here = pt_ext_place_ctx_;
    pt_ext_place_ctx_ = pt_ext_here && is_place_node(recv_node);
    auto recv = mut_ctx ? lower_mut_place(recv_node) : lower_expr(recv_node);
    pt_ext_place_ctx_ = false;
    mut_place_ctx_ = false;
    if (cur_stmt_temp_hoist_ && recv && expr_type(recv) &&
        is_move_type(expr_type(recv)) && is_hoistable_temp_rvalue(recv)) {
        logos::probe::census(pt_ext_here ? "ptown.idx.ext" : "ptown.idx.stmt");
        if (pt_ext_here && pt_ext_arm())
            recv = pt_hoist_ext(std::move(recv), mut_ctx);
        else if (pt_door_arm("ptidx"))
            recv = hoist_stmt_temp(std::move(recv), mut_ctx);
    }
    auto arr_type = expr_type(recv);
===
name: pt-len
file: src/compiler/sema_expr.cpp
---
    if (TypeRef(expr_type(recv)).kind() == LogosType::Kind::Array &&
        method_name == "len") {
        int64_t sz = TypeRef(expr_type(recv)).arr_size();
        return builder().lit_int(sz, prim(LogosType::Kind::I64));
    }
---
    if (TypeRef(expr_type(recv)).kind() == LogosType::Kind::Array &&
        method_name == "len") {
        int64_t sz = TypeRef(expr_type(recv)).arr_size();
        // PROBE 2026-09-15g-aggtemp (census + ptlen) — see src/compiler/PROBES.md.
        if (cur_stmt_temp_hoist_ && recv && is_move_type(expr_type(recv)) &&
            is_hoistable_temp_rvalue(recv)) {
            logos::probe::census("ptown.len.stmt");
            if (pt_door_arm("ptlen")) (void)hoist_stmt_temp(std::move(recv), false);
        }
        return builder().lit_int(sz, prim(LogosType::Kind::I64));
    }
===
name: pt-arrlit
file: src/compiler/sema_expr.cpp
---
    auto items = arr_of(node.get(la::ITEMS.code));
    std::vector<lir::LExprPtr> elems;
    for (uint64_t i = 0; i < items.size(); ++i)
        elems.push_back(lower_expr(map_of(items.get(i))));
---
    auto items = arr_of(node.get(la::ITEMS.code));
    std::vector<lir::LExprPtr> elems;
    // PROBE 2026-09-15g-aggtemp (census + ptmark / ptownx / ptownxf) — see src/compiler/PROBES.md.
    const bool pt_in_foreach = pt_foreach_iter_;
    pt_foreach_iter_ = false;
    for (uint64_t i = 0; i < items.size(); ++i) {
        elems.push_back(lower_expr(map_of(items.get(i))));
        TypeRef pt_et = expr_type(elems.back());
        const bool pt_tv = pt_et && TypeRef(pt_et).kind() == LogosType::Kind::TypeVar;
        if (pt_et && (pt_tv || is_move_type(pt_et)))
            logos::probe::census(pt_tv ? "ptown.arrlit.tv"
                                 : is_hoistable_temp_rvalue(elems.back()) ? "ptown.arrlit.rvalue"
                                                                           : "ptown.arrlit.place");
        if (pt_mark_arm(pt_in_foreach)) mark_moved_expr(expr_ref_of(elems.back()));
    }
===
name: pt-fill-entry
file: src/compiler/sema_expr.cpp
---
    auto val_node = map_of(node.get(la::VALUE.code));
    auto fill_val = lower_expr(val_node);
---
    auto val_node = map_of(node.get(la::VALUE.code));
    // PROBE 2026-09-15g-aggtemp (ptownxf) — see src/compiler/PROBES.md.
    const bool pt_in_foreach = pt_foreach_iter_;
    pt_foreach_iter_ = false;
    auto fill_val = lower_expr(val_node);
===
name: pt-fill-mark
file: src/compiler/sema_expr.cpp
---
    for (int64_t i = 1; i < n; ++i)
        elems.push_back(lower_expr(val_node));  // re-lower for each slot (simple literals)
    return builder().arr_lit(std::move(elems), make_array(elem_type, (size_t)n));
---
    for (int64_t i = 1; i < n; ++i)
        elems.push_back(lower_expr(val_node));  // re-lower for each slot (simple literals)
    // PROBE 2026-09-15g-aggtemp (census + ptmark / ptownx / ptownxf) — see src/compiler/PROBES.md.
    if (n >= 1 && elem_type && is_move_type(elem_type)) {
        logos::probe::census("ptown.fill.move");
        if (pt_mark_arm(pt_in_foreach)) mark_moved_expr(expr_ref_of(elems[0]));
    }
    return builder().arr_lit(std::move(elems), make_array(elem_type, (size_t)n));
===
name: pt-stmt-drops
file: src/compiler/sema_stmt.cpp
---
        if (!moved_vars_.count(nm))
            if (auto d = make_drop_stmt(nm, VarInfo{ty, false}))
                drops.push_back(std::move(*d));
---
        // PROBE 2026-09-15g-aggtemp (ptext: an extended temporary is the block's) — see src/compiler/PROBES.md.
        if (nm.rfind("__lit_temp_", 0) == 0) continue;
        if (!moved_vars_.count(nm))
            if (auto d = make_drop_stmt(nm, VarInfo{ty, false}))
                drops.push_back(std::move(*d));
===
name: pt-stmt-erase
file: src/compiler/sema_stmt.cpp
---
        for (auto& h : hoisted) {
            const std::string& nm = std::get<0>(h);
            fr.vars.erase(nm);
            std::erase(fr.var_order, nm);
        }
---
        for (auto& h : hoisted) {
            const std::string& nm = std::get<0>(h);
            if (nm.rfind("__lit_temp_", 0) == 0) continue;  // PROBE 2026-09-15g-aggtemp (ptext) — see src/compiler/PROBES.md.
            fr.vars.erase(nm);
            std::erase(fr.var_order, nm);
        }
===
name: pt-foreach
file: src/compiler/sema_stmt.cpp
---
    if (node.has_key(la::ITER)) {
        iter = lower_expr(map_of(node.get(la::ITER.code)));
    } else {
---
    if (node.has_key(la::ITER)) {
        pt_foreach_iter_ = true;   // PROBE 2026-09-15g-aggtemp (ptownxf) — see src/compiler/PROBES.md.
        iter = lower_expr(map_of(node.get(la::ITER.code)));
        pt_foreach_iter_ = false;
    } else {
===
