name: ifletown
file: src/compiler/sema_impl.hpp
---
    void mark_match_scrutinee_moved(const lir::LExprPtr& scrut, TypeRef scrut_type,
                                    writ::TinyMapView node);
---
    void mark_match_scrutinee_moved(const lir::LExprPtr& scrut, TypeRef scrut_type,
                                    writ::TinyMapView node,
                                    writ::TinyMapView only_lhs = {});  // PROBE ifletown/matchcondmv: one arm's LHS
===
name: ifletown
file: src/compiler/sema_stmt.cpp
---
void SemaChecker::mark_match_scrutinee_moved(const lir::LExprPtr& scrut,
                                              TypeRef scrut_type,
                                              writ::TinyMapView node) {
---
void SemaChecker::mark_match_scrutinee_moved(const lir::LExprPtr& scrut,
                                              TypeRef scrut_type,
                                              writ::TinyMapView node,
                                              writ::TinyMapView only_lhs) {
===
name: ifletown
file: src/compiler/sema_stmt.cpp
---
    if (scrut && scrut_type && is_move_type(scrut_type) &&
        scrut_is_place && node.has_key(la::ITEMS)) {
        std::string scrut_var =
            expr_ref_of(scrut).kind() == ec::Code::VarRef
                ? std::string(lir_view::EVarRefView{expr_ref_of(scrut)}.name())
                : std::string("<place>");
        if (!scrut_var.empty()) {
            auto arms_mv = arr_of(node.get(la::ITEMS.code));
            for (uint64_t i = 0; i < arms_mv.size(); ++i) {
                auto arm = map_of(arms_mv.get(i));
                if (code_of(arm) != la::MATCH_ARM) continue;
                if (arm.has_key(la::GUARD)) continue;
                if (!arm.has_key(la::LHS)) continue;
                auto lhs = map_of(arm.get(la::LHS.code));
---
    if (scrut && scrut_type && is_move_type(scrut_type) &&
        scrut_is_place && (node.has_key(la::ITEMS) || node.has_key(la::PAT) || !only_lhs.is_null())) {
        std::string scrut_var =
            expr_ref_of(scrut).kind() == ec::Code::VarRef
                ? std::string(lir_view::EVarRefView{expr_ref_of(scrut)}.name())
                : std::string("<place>");
        if (!scrut_var.empty()) {
            // PROBE ifletown/matchcondmv: the arm LHS list — every unguarded MATCH_ARM, or ONE arm, or the if-let's PAT
            std::vector<writ::TinyMapView> lhs_list_;
            if (!only_lhs.is_null()) {
                lhs_list_.push_back(only_lhs);
            } else if (node.has_key(la::ITEMS)) {
                auto arms_mv = arr_of(node.get(la::ITEMS.code));
                for (uint64_t i = 0; i < arms_mv.size(); ++i) {
                    auto arm = map_of(arms_mv.get(i));
                    if (code_of(arm) != la::MATCH_ARM) continue;
                    if (arm.has_key(la::GUARD)) continue;
                    if (!arm.has_key(la::LHS)) continue;
                    lhs_list_.push_back(map_of(arm.get(la::LHS.code)));
                }
            } else if (node.has_key(la::PAT)) {
                lhs_list_.push_back(map_of(node.get(la::PAT.code)));
            }
            for (auto lhs : lhs_list_) {
===
name: ifletown
file: src/compiler/sema_stmt.cpp
---
    const uint32_t if_line = node_line_;
    // ── if let pattern = expr { ... } ─────────────────────────────
    if (node.has_key(la::PAT)) {
        auto scrut = node.has_key(la::VALUE)
            ? lower_expr(map_of(node.get(la::VALUE.code))) : error_expr();
        TypeRef scrut_type = expr_type(scrut);
---
    const uint32_t if_line = node_line_;
    // ── if let pattern = expr { ... } ─────────────────────────────
    if (node.has_key(la::PAT)) {
        auto scrut = node.has_key(la::VALUE)
            ? lower_expr(map_of(node.get(la::VALUE.code))) : error_expr();
        TypeRef scrut_type = expr_type(scrut);
        // PROBE ifletown (PROBES.md 2026-09-06c): the if-let takes lower_match's ownership protocol.
        // ifletmv = hoist + mark + flags only; ifletdr = arm drops only; ifletown = both.
        const bool il_mv_ = logos::probe::on("ifletown") || logos::probe::on("ifletmv");
        const bool il_dr_ = logos::probe::on("ifletown") || logos::probe::on("ifletdr");
        bool il_hoist_ = false; std::string il_var_; lir_view::StmtRef il_let_;
        if (il_mv_ && scrut && scrut_type && is_move_type(scrut_type) &&
            !lir_view::is_place_expr(expr_ref_of(scrut))) {
            il_var_ = "__match_scrut_" + std::to_string(tmp_var_count_++);
            push_scope();
            define(il_var_, scrut_type);
            lir::SLet il_sl_; il_sl_.name = il_var_; il_sl_.type = scrut_type; il_sl_.is_mut = false;
            il_sl_.value = std::move(scrut);
            il_let_ = make_stmt_emit(node_line_, std::move(il_sl_));
            scrut = builder().var_ref(il_var_, scrut_type);
            il_hoist_ = true;
        }
        auto il_pre_moves_ = moved_vars_;
        const size_t il_then_mark_ = flag_clear_log_.size();
===
name: ifletown
file: src/compiler/sema_stmt.cpp
---
        // Then arm: pattern → then block. Define + emit the nested-payload
        // destructures BEFORE the body so its bindings are in scope.
        push_scope();
        bind_pattern(pat, scrut_type);
        std::vector<lir_view::StmtRef> nested_destructure;
---
        // Then arm: pattern → then block. Define + emit the nested-payload
        // destructures BEFORE the body so its bindings are in scope.
        push_scope();
        bind_pattern(pat, scrut_type);
        if (il_mv_ && !node.has_key(la::GUARD))  // PROBE ifletown: the binding arm owns the payload it binds by value
            mark_match_scrutinee_moved(scrut, scrut_type, node);
        std::vector<lir_view::StmtRef> nested_destructure;
===
name: ifletown
file: src/compiler/sema_stmt.cpp
---
        std::vector<lir_view::StmtRef> then_body;
        if (node.has_key(la::THEN))
            lower_block(map_of(node.get(la::THEN.code))).each_stmt([&](lir_view::StmtRef s){ then_body.push_back(s); });
        if (!nested_destructure.empty()) {
            std::vector<lir_view::StmtRef> merged = std::move(nested_destructure);
            merged.insert(merged.end(),
                          std::make_move_iterator(then_body.begin()),
                          std::make_move_iterator(then_body.end()));
            then_body = std::move(merged);
        }
        pop_scope();

        // Else arm: wildcard → else block (or empty)
        std::vector<lir_view::StmtRef> else_body;
        if (node.has_key(la::ELSE)) {
            auto else_node = map_of(node.get(la::ELSE.code));
            if (code_of(else_node) == la::BLOCK) {
                lower_block(else_node).each_stmt([&](lir_view::StmtRef s){ else_body.push_back(s); });
            } else {
                // else if: wrap in block
                else_body.push_back(lower_if(else_node));
            }
        }
---
        std::vector<lir_view::StmtRef> then_body;
        if (node.has_key(la::THEN))
            lower_block(map_of(node.get(la::THEN.code))).each_stmt([&](lir_view::StmtRef s){ then_body.push_back(s); });
        if (!nested_destructure.empty()) {
            std::vector<lir_view::StmtRef> merged = std::move(nested_destructure);
            merged.insert(merged.end(),
                          std::make_move_iterator(then_body.begin()),
                          std::make_move_iterator(then_body.end()));
            then_body = std::move(merged);
        }
        if (il_dr_) {  // PROBE ifletown: the arm's own bindings drop on fall-through (lower_match's [[baghunt]] shape)
            bool il_ret_ = false;
            lir_view::StmtRef il_last_;
            if (!then_body.empty()) {
                il_last_ = stmt_ref_of(then_body.back());
                if (il_last_ && il_last_.kind() == lir_schema::stmt::Code::Return) il_ret_ = true;
            }
            if (!il_ret_) {
                if (il_last_ && il_last_.kind() == lir_schema::stmt::Code::ExprStmt)
                    mark_moved_in_expr_recursive(lir_view::SExprStmtView{il_last_}.expr());
                for (auto& d : collect_drops()) then_body.push_back(std::move(d));
            }
        }
        auto il_then_moves_ = moved_vars_;
        const size_t il_then_end_ = flag_clear_log_.size();
        pop_scope();
        if (il_mv_) moved_vars_ = il_pre_moves_;
        const size_t il_else_mark_ = flag_clear_log_.size();

        // Else arm: wildcard → else block (or empty)
        std::vector<lir_view::StmtRef> else_body;
        if (node.has_key(la::ELSE)) {
            auto else_node = map_of(node.get(la::ELSE.code));
            if (code_of(else_node) == la::BLOCK) {
                lower_block(else_node).each_stmt([&](lir_view::StmtRef s){ else_body.push_back(s); });
            } else {
                // else if: wrap in block
                else_body.push_back(lower_if(else_node));
            }
        }
        if (il_mv_) {  // PROBE ifletown: the two arms are two paths to the frame's drops (lower_if's plain-if shape)
            auto il_else_moves_ = moved_vars_;
            const size_t il_else_end_ = flag_clear_log_.size();
            auto il_div_ = [&](const std::vector<lir_view::StmtRef>& b) -> int {
                if (b.empty()) return 0;
                auto br = stmt_ref_of(b.back());
                if (!br) return 0;
                auto k = br.kind();
                if (k == lir_schema::stmt::Code::Return) return 1;
                if (k == lir_schema::stmt::Code::Break || k == lir_schema::stmt::Code::Continue) return 2;
                return 0;
            };
            std::set<std::string> il_post_;
            bool il_any_ = false;
            if (il_div_(then_body) == 0) { il_any_ = true; for (auto& m : il_then_moves_) il_post_.insert(m); }
            if (il_div_(else_body) == 0) { il_any_ = true; for (auto& m : il_else_moves_) il_post_.insert(m); }
            moved_vars_ = il_any_ ? il_post_ : il_pre_moves_;
            std::vector<CondMoveBranch> il_reach_;
            if (il_div_(then_body) != 1) il_reach_.push_back({&then_body, nullptr, il_then_moves_, il_then_mark_, il_then_end_});
            if (il_div_(else_body) != 1) il_reach_.push_back({&else_body, nullptr, il_else_moves_, il_else_mark_, il_else_end_});
            elaborate_cond_moves(il_pre_moves_, il_reach_);
        }
===
name: ifletown
file: src/compiler/sema_stmt.cpp
---
        sm.arms.push_back({make_pat_wild("_"), lir_mirror_block(*cur_prog_, else_body), std::nullopt});
        return make_stmt_emit(node_line_, std::move(sm));
    }
---
        sm.arms.push_back({make_pat_wild("_"), lir_mirror_block(*cur_prog_, else_body), std::nullopt});
        if (il_hoist_) {  // PROBE ifletown: the hoisted temporary is owned by a block around the match (lower_match's finalize)
            auto il_stmt_ = make_stmt_emit(node_line_, std::move(sm));
            auto il_drops_ = collect_drops();
            pop_scope();
            std::vector<lir_view::StmtRef> il_blk_;
            il_blk_.push_back(std::move(il_let_));
            il_blk_.push_back(std::move(il_stmt_));
            for (auto& d : il_drops_) il_blk_.push_back(std::move(d));
            return make_stmt_emit(node_line_, lir::SBlock{lir_mirror_block(*cur_prog_, il_blk_), /*transparent=*/true});
        }
        return make_stmt_emit(node_line_, std::move(sm));
    }
===
name: ifletown
file: src/compiler/sema_stmt.cpp
---
        auto scrut = node.has_key(la::VALUE)
            ? lower_expr_temp_scoped(map_of(node.get(la::VALUE.code))) : error_expr();
        TypeRef scrut_type = expr_type(scrut);
---
        auto scrut = node.has_key(la::VALUE)
            ? lower_expr_temp_scoped(map_of(node.get(la::VALUE.code))) : error_expr();
        TypeRef scrut_type = expr_type(scrut);
        // PROBE ifletown (PROBES.md 2026-09-06c): the while-let takes lower_match's ownership protocol (see lower_if).
        const bool wl_mv_ = logos::probe::on("ifletown") || logos::probe::on("ifletmv");
        const bool wl_dr_ = logos::probe::on("ifletown") || logos::probe::on("ifletdr");
        bool wl_hoist_ = false; std::string wl_var_; lir_view::StmtRef wl_let_;
        if (wl_mv_ && scrut && scrut_type && is_move_type(scrut_type) &&
            !lir_view::is_place_expr(expr_ref_of(scrut))) {
            wl_var_ = "__match_scrut_" + std::to_string(tmp_var_count_++);
            push_scope();
            define(wl_var_, scrut_type);
            lir::SLet wl_sl_; wl_sl_.name = wl_var_; wl_sl_.type = scrut_type; wl_sl_.is_mut = false;
            wl_sl_.value = std::move(scrut);
            wl_let_ = make_stmt_emit(node_line_, std::move(wl_sl_));
            scrut = builder().var_ref(wl_var_, scrut_type);
            wl_hoist_ = true;
        }
        auto wl_pre_moves_ = moved_vars_;
        const size_t wl_then_mark_ = flag_clear_log_.size();
===
name: ifletown
file: src/compiler/sema_stmt.cpp
---
        // Then arm: pattern → loop body
        push_scope();
        bind_pattern(pat, scrut_type);
        std::vector<lir_view::StmtRef> nested_destructure;
---
        // Then arm: pattern → loop body
        push_scope();
        bind_pattern(pat, scrut_type);
        if (wl_mv_ && !node.has_key(la::GUARD))  // PROBE ifletown: the binding arm owns the payload it binds by value
            mark_match_scrutinee_moved(scrut, scrut_type, node);
        std::vector<lir_view::StmtRef> nested_destructure;
===
name: ifletown
file: src/compiler/sema_stmt.cpp
---
        pending_loop_body_scope_ = true;  // G167-4: tag the body frame
            lower_block(map_of(node.get(la::BODY.code))).each_stmt([&](lir_view::StmtRef s){ then_body.push_back(s); });
            loop_break_frames_.pop_back();
            if (!my_label.empty()) active_loop_labels_.pop_back();
            --loop_depth_;
        }
        if (!nested_destructure.empty()) {
            std::vector<lir_view::StmtRef> merged = std::move(nested_destructure);
            merged.insert(merged.end(),
                          std::make_move_iterator(then_body.begin()),
                          std::make_move_iterator(then_body.end()));
            then_body = std::move(merged);
        }
        pop_scope();

        // Else arm: wildcard → break
        std::vector<lir_view::StmtRef> else_body;
        else_body.push_back(builder().stmt_break(nullptr, "", node_line_));
---
        pending_loop_body_scope_ = true;  // G167-4: tag the body frame
            lower_block(map_of(node.get(la::BODY.code))).each_stmt([&](lir_view::StmtRef s){ then_body.push_back(s); });
            loop_break_frames_.pop_back();
            if (!my_label.empty()) active_loop_labels_.pop_back();
            --loop_depth_;
        }
        if (!nested_destructure.empty()) {
            std::vector<lir_view::StmtRef> merged = std::move(nested_destructure);
            merged.insert(merged.end(),
                          std::make_move_iterator(then_body.begin()),
                          std::make_move_iterator(then_body.end()));
            then_body = std::move(merged);
        }
        if (wl_dr_) {  // PROBE ifletown: the arm's own bindings drop at the iteration's end
            bool wl_ret_ = false;
            lir_view::StmtRef wl_last_;
            if (!then_body.empty()) {
                wl_last_ = stmt_ref_of(then_body.back());
                if (wl_last_ && (wl_last_.kind() == lir_schema::stmt::Code::Return ||
                                 wl_last_.kind() == lir_schema::stmt::Code::Break ||
                                 wl_last_.kind() == lir_schema::stmt::Code::Continue)) wl_ret_ = true;
            }
            if (!wl_ret_) {
                if (wl_last_ && wl_last_.kind() == lir_schema::stmt::Code::ExprStmt)
                    mark_moved_in_expr_recursive(lir_view::SExprStmtView{wl_last_}.expr());
                for (auto& d : collect_drops()) then_body.push_back(std::move(d));
            }
        }
        auto wl_then_moves_ = moved_vars_;
        const size_t wl_then_end_ = flag_clear_log_.size();
        pop_scope();
        if (wl_mv_) moved_vars_ = wl_pre_moves_;
        const size_t wl_else_mark_ = flag_clear_log_.size();

        // Else arm: wildcard → break
        std::vector<lir_view::StmtRef> else_body;
        else_body.push_back(builder().stmt_break(nullptr, "", node_line_));
        if (wl_mv_) {  // PROBE ifletown: the binding arm and the break arm are two paths to the frame's drops
            std::vector<CondMoveBranch> wl_reach_;
            wl_reach_.push_back({&then_body, nullptr, wl_then_moves_, wl_then_mark_, wl_then_end_});
            wl_reach_.push_back({&else_body, nullptr, wl_pre_moves_, wl_else_mark_, wl_else_mark_});
            elaborate_cond_moves(wl_pre_moves_, wl_reach_);
            moved_vars_ = wl_pre_moves_;
        }
===
name: ifletown
file: src/compiler/sema_stmt.cpp
---
        std::vector<lir_view::StmtRef> loop_body;
        loop_body.push_back(make_stmt_emit(node_line_, std::move(sm)));
        lir::SLoop sl; sl.body = lir_mirror_block(*cur_prog_, loop_body);
        sl.label = std::move(my_label);
---
        std::vector<lir_view::StmtRef> loop_body;
        if (wl_hoist_) {  // PROBE ifletown: the per-iteration temporary is owned by the loop body
            loop_body.push_back(std::move(wl_let_));
            loop_body.push_back(make_stmt_emit(node_line_, std::move(sm)));
            for (auto& d : collect_drops()) loop_body.push_back(std::move(d));
            pop_scope();
        } else {
            loop_body.push_back(make_stmt_emit(node_line_, std::move(sm)));
        }
        lir::SLoop sl; sl.body = lir_mirror_block(*cur_prog_, loop_body);
        sl.label = std::move(my_label);
===
name: matchcondmv
file: src/compiler/sema_stmt.cpp
---
    // leave the scrutinee conditionally live for later arms.
    mark_match_scrutinee_moved(scrut, scrut_type, node);
---
    // leave the scrutinee conditionally live for later arms.
    if (!logos::probe::on("matchcondmv"))  // PROBE matchcondmv: the mark moves INSIDE each binding arm (a flagged drop)
        mark_match_scrutinee_moved(scrut, scrut_type, node);
===
name: matchcondmv
file: src/compiler/sema_stmt.cpp
---
            bind_pattern(pat, scrut_type);
            current_pat_mut_names_ = saved_pat_muts;
            // Register Writ @-pattern bindings in scope (visible in body + guard).
            for (const auto& b : body_binds) {
                define(b.name, anyval_t, /*is_mut=*/false);
            }
            // P4-pm-02: for each nested struct sub-pat, emit field-by-
---
            bind_pattern(pat, scrut_type);
            current_pat_mut_names_ = saved_pat_muts;
            if (logos::probe::on("matchcondmv") && !arm.has_key(la::GUARD) && arm.has_key(la::LHS))  // PROBE matchcondmv
                mark_match_scrutinee_moved(smatch.scrut, scrut_type, node, map_of(arm.get(la::LHS.code)));
            // Register Writ @-pattern bindings in scope (visible in body + guard).
            for (const auto& b : body_binds) {
                define(b.name, anyval_t, /*is_mut=*/false);
            }
            // P4-pm-02: for each nested struct sub-pat, emit field-by-
===
name: tuplemut
file: src/compiler/sema_stmt.cpp
---
                pt.bindings.push_back(nm);
                pt.subs.push_back(make_pat_wild(nm));
            } else if (sc == la::PAT_INT.code || sc == la::PAT_NEG_INT.code ||
---
                pt.bindings.push_back(nm);
                {   // PROBE tuplemut: the element sub carries the written by-value `mut`
                    lir::Pattern tw_;
                    tw_.mirror_ptr_ = lir_mirror_emit_pat_wild(*cur_prog_, nm, 0xFFFFFFFFu,
                        logos::probe::on("tuplemut") && nm != "_" && pat_byval_mut(sub));
                    pt.subs.push_back(std::move(tw_));
                }
            } else if (sc == la::PAT_INT.code || sc == la::PAT_NEG_INT.code ||
===
name: tuplemut
file: src/compiler/sema_stmt.cpp
---
                            pt.bindings.push_back(nm);
                            pt.subs.push_back(make_pat_wild(nm));
                            single = true;
---
                            pt.bindings.push_back(nm);
                            {   // PROBE tuplemut: the single-alt element sub carries the written by-value `mut`
                                lir::Pattern tw_;
                                tw_.mirror_ptr_ = lir_mirror_emit_pat_wild(*cur_prog_, nm, 0xFFFFFFFFu,
                                    logos::probe::on("tuplemut") && nm != "_" && pat_byval_mut(inner));
                                pt.subs.push_back(std::move(tw_));
                            }
                            single = true;
===
name: tuplemut
file: src/compiler/sema_stmt.cpp
---
        auto _tp_slots = v.bind_slots();  // Phase-1: reuse reserved slots
        for (size_t i = 0; i < names.size() && i < types.size(); ++i)
            if (names[i] != "_")
                define(std::string(names[i]), types[i], false,
                       i < _tp_slots.size() ? _tp_slots[i] : 0xFFFFFFFFu);
---
        auto _tp_slots = v.bind_slots();  // Phase-1: reuse reserved slots
        std::vector<uint32_t> _tp_muts;  // PROBE tuplemut: the element sub's by-value `mut`
        v.each_sub([&](lir_view::PatRef sp) {
            _tp_muts.push_back(sp && sp.kind() == ps::Code::Wild && lir_view::PatWildView{sp}.is_mut() ? 1u : 0u); });
        for (size_t i = 0; i < names.size() && i < types.size(); ++i)
            if (names[i] != "_")
                define(std::string(names[i]), types[i],
                       logos::probe::on("tuplemut") && i < _tp_muts.size() && _tp_muts[i] != 0u,
                       i < _tp_slots.size() ? _tp_slots[i] : 0xFFFFFFFFu);
===
name: tuplemut
file: src/compiler/borrow_check.cpp
---
            case Code::RefPat:
                declare_pat_bindings(lir_view::PatRefPatView{pr}.inner());
                break;
            default: break;
---
            case Code::RefPat:
                declare_pat_bindings(lir_view::PatRefPatView{pr}.inner());
                break;
            case Code::Tuple: {  // PROBE tuplemut / tupledecl: a tuple pattern's bindings are tracked locals
                if (!(logos::probe::on("tuplemut") || logos::probe::on("tupledecl"))) break;
                lir_view::PatTupleView tv{pr};
                auto tslots = tv.bind_slots();
                std::vector<uint32_t> tmuts;
                tv.each_sub([&](lir_view::PatRef sp) {
                    tmuts.push_back(sp && sp.kind() == Code::Wild && lir_view::PatWildView{sp}.is_mut() ? 1u : 0u); });
                size_t ti = 0;
                tv.each_binding([&](std::string_view b) {
                    if (!b.empty() && b != "_") {
                        const uint32_t sl_ = ti < tslots.size() ? tslots[ti] : NO_SLOT;
                        declare_var(std::string(b), sl_);
                        if (ti < tmuts.size() && tmuts[ti] != 0u)
                            var_at(sl_, b).is_mut_binding = true;
                    }
                    ++ti;
                });
                break;
            }
            default: break;
===
