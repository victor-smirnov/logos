name: consumex
file: src/compiler/sema_impl.hpp
---
    void mark_moved_expr(lir_view::ExprRef er) {
---
    // PROBE 2026-09-15e-consume (census tag for arrmove / arrmovetv / opmove / unmove / casmove / casplmove / consumex) — see src/compiler/PROBES.md.
    static const char* consume_probe_tag(lir_view::ExprRef r) {
        using C = lir_schema::expr::Code;
        if (!r) return "N";
        switch (r.kind()) {
            case C::VarRef: return "V";
            case C::FieldRead: return "F";
            case C::TupleIndex: return "T";
            case C::IndexRead: return "I";
            default: return "O";
        }
    }

    void mark_moved_expr(lir_view::ExprRef er) {
===
name: arrmove
file: src/compiler/sema_expr.cpp
---
    for (uint64_t i = 0; i < items.size(); ++i)
        elems.push_back(lower_expr(map_of(items.get(i))));
---
    for (uint64_t i = 0; i < items.size(); ++i) {
        elems.push_back(lower_expr(map_of(items.get(i))));
        // PROBE 2026-09-15e-consume (census + arrmove / arrmovetv / consumex) — see src/compiler/PROBES.md.
        TypeRef consume_et = expr_type(elems.back());
        const bool consume_tv = consume_et && TypeRef(consume_et).kind() == LogosType::Kind::TypeVar;
        if (consume_tv)
            logos::probe::census(std::string("consume.arrlit.tv.") + consume_probe_tag(expr_ref_of(elems.back())));
        else if (consume_et && is_move_type(consume_et))
            logos::probe::census(std::string("consume.arrlit.move.") + consume_probe_tag(expr_ref_of(elems.back())));
        if (logos::probe::on("arrmovetv") ||
            (!consume_tv && (logos::probe::on("arrmove") || logos::probe::on("consumex"))))
            mark_moved_expr(expr_ref_of(elems.back()));
    }
===
name: opmove
file: src/compiler/sema_expr.cpp
---
                    args.push_back(std::move(e));
                };
                push_operand(std::move(lhs), lt, 0);
---
                    // PROBE 2026-09-15e-consume (census + opmove / consumex) — see src/compiler/PROBES.md.
                    if (vty && is_move_type(vty)) {
                        logos::probe::census(std::string("consume.binop.byval.move.") + consume_probe_tag(expr_ref_of(e)));
                        if (logos::probe::on("opmove") || logos::probe::on("consumex"))
                            mark_moved_expr(expr_ref_of(e));
                    }
                    args.push_back(std::move(e));
                };
                push_operand(std::move(lhs), lt, 0);
===
name: unmove
file: src/compiler/sema_expr.cpp
---
                args.push_back(std::move(operand));
---
                // PROBE 2026-09-15e-consume (census + unmove / consumex) — see src/compiler/PROBES.md.
                if (is_move_type(vt) && !(fit->param_types.size() == 1 && fit->param_types[0] &&
                                          is_ref_like(TypeRef(fit->param_types[0]).kind()))) {
                    logos::probe::census(std::string("consume.unary.byval.move.") + consume_probe_tag(expr_ref_of(operand)));
                    if (logos::probe::on("unmove") || logos::probe::on("consumex"))
                        mark_moved_expr(expr_ref_of(operand));
                }
                args.push_back(std::move(operand));
===
name: casmove
file: src/compiler/sema_stmt.cpp
---
                    args.push_back(std::move(recv));
                    args.push_back(std::move(rhs));
---
                    // PROBE 2026-09-15e-consume (census + casmove / consumex) — see src/compiler/PROBES.md.
                    if (rhs && is_move_type(expr_type(rhs)) &&
                        !(fit->param_types.size() == 2 && fit->param_types[1] &&
                          is_ref_like(TypeRef(fit->param_types[1]).kind()))) {
                        logos::probe::census(std::string("consume.opassign.var.move.") + consume_probe_tag(expr_ref_of(rhs)));
                        if (logos::probe::on("casmove") || logos::probe::on("consumex"))
                            mark_moved_expr(expr_ref_of(rhs));
                    }
                    args.push_back(std::move(recv));
                    args.push_back(std::move(rhs));
===
name: casplmove
file: src/compiler/sema_stmt.cpp
---
                    args.push_back(std::move(addr));
                    args.push_back(std::move(rhs));
---
                    // PROBE 2026-09-15e-consume (census + casplmove / consumex) — see src/compiler/PROBES.md.
                    if (rhs && is_move_type(expr_type(rhs)) &&
                        !(fit->param_types.size() == 2 && fit->param_types[1] &&
                          is_ref_like(TypeRef(fit->param_types[1]).kind()))) {
                        logos::probe::census(std::string("consume.opassign.place.move.") + consume_probe_tag(expr_ref_of(rhs)));
                        if (logos::probe::on("casplmove") || logos::probe::on("consumex"))
                            mark_moved_expr(expr_ref_of(rhs));
                    }
                    args.push_back(std::move(addr));
                    args.push_back(std::move(rhs));
===
name: arrmovetv
file: src/compiler/sema_expr.cpp
---
    TypeRef elem_type = expr_type(elems[0]);
---
    // PROBE 2026-09-15e-consume: arrmovetv (TypeVar elements marked too) is armed in the element loop above.
    TypeRef elem_type = expr_type(elems[0]);
===
