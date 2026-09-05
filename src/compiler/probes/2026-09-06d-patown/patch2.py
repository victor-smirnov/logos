import io
def rd(p): return io.open(p).read()
def wr(p, s): io.open(p, "w").write(s)
def rep(s, old, new, n=1, tag=""):
    c = s.count(old); assert c == n, (tag, c, n, old[:80]); return s.replace(old, new)

H = "src/compiler/sema_impl.hpp"; h = rd(H)
h = rep(h, """    void mark_match_scrutinee_moved(const lir::LExprPtr& scrut, TypeRef scrut_type,
                                    writ::TinyMapView arm_lhs);
""", """    void mark_match_scrutinee_moved(const lir::LExprPtr& scrut, TypeRef scrut_type,
                                    lir_view::PatRef pat);
    // Does `pat`, matched against a value of type `ty`, bind a MOVE-TYPE part
    // of it BY VALUE (mode 0, a real name)? Read off the LIR pattern — the one
    // place the binding modes live — never re-derived from the AST: the AST
    // re-derivation this replaces missed the shorthand `ref` field, the nested
    // variant under a tuple, and the nested variant payload itself.
    bool pattern_moves_out(lir_view::PatRef pat, TypeRef ty);
""", 1, "hpp mark decl")
h = rep(h, "    std::vector<lir_view::StmtRef> collect_drops_to_loop() const;\n",
           "    // `cross` = how many loop-body frames a LABELED break/continue passes\n"
           "    // through before the one it leaves (0 = the innermost loop).\n"
           "    std::vector<lir_view::StmtRef> collect_drops_to_loop(size_t cross = 0) const;\n", 1, "hpp drops")
wr(H, h)

P = "src/compiler/sema_stmt.cpp"; s = rd(P)
i = s.find("void SemaChecker::mark_match_scrutinee_moved(const lir::LExprPtr& scrut,\n")
j = s.find("\nvoid SemaChecker::emit_nested_variant_lets(", i)
assert i > 0 and j > i
new_fn = r'''bool SemaChecker::pattern_moves_out(lir_view::PatRef pr, TypeRef ty) {
    namespace ps = lir_schema::pat;
    if (!pr) return false;
    const auto* pool = cur_prog_->type_pool.impl();
    auto named = [](std::string_view n) { return !n.empty() && n != "_"; };
    switch (pr.kind()) {
        case ps::Code::Wild:
            return named(lir_view::PatWildView{pr}.name()) && ty && is_move_type(ty);
        case ps::Code::VariantData: {
            lir_view::PatVariantDataView v{pr};
            std::vector<std::string> ns; std::vector<TypeRef> tys;
            v.each_binding([&](std::string_view n){ ns.emplace_back(n); });
            v.each_binding_type(pool, [&](TypeRef t){ tys.push_back(t); });
            auto ms = v.bind_ref_modes();   // absent = all by value (measured, see the E0507 arm)
            for (size_t i = 0; i < ns.size(); ++i) {
                uint32_t m = i < ms.size() ? ms[i] : 0u;
                TypeRef bt = i < tys.size() ? tys[i] : TypeRef(nullptr);
                if (m == 0 && named(ns[i]) && bt && is_move_type(bt)) return true;
            }
            return false;
        }
        case ps::Code::Tuple: {
            lir_view::PatTupleView v{pr};
            std::vector<TypeRef> elems;
            if (ty && TypeRef(ty).kind() == LogosType::Kind::Tuple) elems = TypeRef(ty).tuple_elems();
            bool any = false; size_t i = 0;
            v.each_sub([&](lir_view::PatRef sp) {
                TypeRef et = i < elems.size() ? elems[i] : TypeRef(nullptr);
                if (!any && pattern_moves_out(sp, et)) any = true;
                ++i;
            });
            return any;
        }
        case ps::Code::Struct: {
            lir_view::PatStructView v{pr};
            bool any = false;
            v.each_field([&](lir_view::PatFieldBindingView f) {
                if (any) return;
                TypeRef ft = ty ? field_type_of_for_type(ty, f.field_name()) : TypeRef(nullptr);
                auto sub = f.sub();
                if (!sub) { any = ft && is_move_type(ft); return; }   // shorthand `{ f }` = by value
                any = pattern_moves_out(sub, ft);
            });
            return any;
        }
        case ps::Code::Slice: {
            lir_view::PatSliceView v{pr};
            TypeRef et = ty && (TypeRef(ty).kind() == LogosType::Kind::Array ||
                                TypeRef(ty).kind() == LogosType::Kind::Slice)
                         ? TypeRef(ty).elem() : TypeRef(nullptr);
            bool any = false;
            v.each_prefix([&](lir_view::PatRef sp){ if (!any && pattern_moves_out(sp, et)) any = true; });
            v.each_suffix([&](lir_view::PatRef sp){ if (!any && pattern_moves_out(sp, et)) any = true; });
            if (!any && v.rest() && v.rest().kind() == ps::Code::Wild)
                any = named(lir_view::PatWildView{v.rest()}.name()) && et && is_move_type(et);
            return any;
        }
        case ps::Code::At: {
            lir_view::PatAtView v{pr};
            if (named(v.name()) && ty && is_move_type(ty)) return true;   // the whole value, by value
            return pattern_moves_out(v.sub(), ty);
        }
        case ps::Code::Or: {
            bool any = false;
            lir_view::PatOrView{pr}.each_alt([&](lir_view::PatRef a){
                if (!any && pattern_moves_out(a, ty)) any = true; });
            return any;
        }
        default:
            // RefBind / RefPat bind through a reference; Variant / Int / Bool /
            // Range bind nothing.
            return false;
    }
}

void SemaChecker::mark_match_scrutinee_moved(const lir::LExprPtr& scrut,
                                              TypeRef scrut_type,
                                              lir_view::PatRef pat) {
    namespace ec = lir_schema::expr;
    // The scrutinee may be a plain VAR (`match o`) or a PLACE — a struct field
    // (`match s.o`) / tuple element (`match a.1`) / an element behind an index
    // or a deref. Enum value-repr makes the payload INLINE in the parent's
    // storage, so moving a payload out of a place scrutinee must mark THAT
    // place moved (mark_moved_expr records `s.o`/`a.1` in moved_fields, and
    // REFUSES an array element the way every other move spelling does) — else
    // the parent's scope-exit Drop double-frees the moved-out payload. A bare
    // VarRef marks the var. A temporary has no owner to mark (lower_match hoists
    // it into a synth local first, so it arrives here as a VarRef).
    if (!(scrut && scrut_type && is_move_type(scrut_type) &&
          lir_view::is_place_expr(expr_ref_of(scrut)) && pattern_moves_out(pat, scrut_type)))
        return;
    if (expr_ref_of(scrut).kind() == ec::Code::VarRef)
        mark_moved(std::string(lir_view::EVarRefView{expr_ref_of(scrut)}.name()));
    else
        mark_moved_expr(expr_ref_of(scrut));
}
'''
s = s[:i] + new_fn + s[j:]
s = rep(s, "                mark_match_scrutinee_moved(smatch.scrut, scrut_type, effective_lhs(arm, alt_idx));",
           "                mark_match_scrutinee_moved(smatch.scrut, scrut_type, pat_ref_of(pat));", 1, "call stmt")
s = rep(s, "                mark_match_scrutinee_moved(me.scrut, scrut_type, effective_lhs(arm, alt_idx));",
           "                mark_match_scrutinee_moved(me.scrut, scrut_type, pat_ref_of(pat));", 1, "call expr")
s = rep(s, "    mark_match_scrutinee_moved(scrut, scrut_type, pat_node);\n",
           "    mark_match_scrutinee_moved(scrut, scrut_type, pat_ref_of(pat));\n", 1, "call let-else")
# nested-variant re-extraction: the synth payload is a local of the arm; what
# its let-else binds by value is moved OUT of it
s = rep(s, """    define_binds(pat_ref_of(lpat));
    // Emit the let-else.
    lir::SLetElse sle;
    sle.pat   = std::move(lpat);
    sle.scrut = builder().var_ref(synth_name, synth_t);
""", """    define_binds(pat_ref_of(lpat));
    // Emit the let-else. Its bindings OWN what they take by value: the synth
    // is marked moved so the arm's end does not drop it a second time.
    lir::SLetElse sle;
    sle.scrut = builder().var_ref(synth_name, synth_t);
    mark_match_scrutinee_moved(sle.scrut, synth_t, pat_ref_of(lpat));
    sle.pat   = std::move(lpat);
""", 1, "nested variant synth")
# labeled break / continue: cross the inner loop bodies up to the labeled one
s = rep(s, """    } else if (sref && (sref.kind() == lir_schema::stmt::Code::Break ||
                        sref.kind() == lir_schema::stmt::Code::Continue)) {
        // G167-4: drop every frame down to AND INCLUDING the loop body — a
        // break/continue nested in an `if` exits via the loop edge, bypassing
        // the body block's normal end drops.
        for (auto& d : collect_drops_to_loop())
            out.push_back(std::move(d));
    }
""", """    } else if (sref && (sref.kind() == lir_schema::stmt::Code::Break ||
                        sref.kind() == lir_schema::stmt::Code::Continue)) {
        // G167-4: drop every frame down to AND INCLUDING the loop body — a
        // break/continue nested in an `if` exits via the loop edge, bypassing
        // the body block's normal end drops. A LABELED one leaves an OUTER
        // loop: every inner loop body it passes through is left too, so the
        // walk crosses one loop-body frame per enclosing loop below the target
        // (`'a: loop { let s = …; loop { continue 'a; } }` leaked `s`).
        std::string_view lbl = sref.kind() == lir_schema::stmt::Code::Break
            ? lir_view::SBreakView{sref}.label() : lir_view::SContinueView{sref}.label();
        size_t cross = 0;
        if (!lbl.empty())
            for (size_t k = loop_break_frames_.size(); k-- > 0; ++cross)
                if (loop_break_frames_[k].label == lbl) break;
        if (cross >= loop_break_frames_.size()) cross = 0;   // unknown label: diagnosed elsewhere
        for (auto& d : collect_drops_to_loop(cross))
            out.push_back(std::move(d));
    }
""", 1, "labeled break")
wr(P, s)

C = "src/compiler/sema.cpp"; c = rd(C)
c = rep(c, """std::vector<lir_view::StmtRef> SemaChecker::collect_drops_to_loop() const {
    std::vector<lir_view::StmtRef> drops;
    for (auto fit = scope_.rbegin(); fit != scope_.rend(); ++fit) {
        emit_frame_drops(*fit, drops);
        // Stop AFTER dropping the loop-body frame: break/continue leaves the
        // loop via its edge, so the iteration's locals (incl. this frame's) are
        // released here; outer (enclosing-fn) frames stay live. A closure
        // boundary also stops the walk (a break can't cross it).
        if (fit->loop_boundary || fit->closure_boundary) break;
    }
    return drops;
}
""", """std::vector<lir_view::StmtRef> SemaChecker::collect_drops_to_loop(size_t cross) const {
    std::vector<lir_view::StmtRef> drops;
    for (auto fit = scope_.rbegin(); fit != scope_.rend(); ++fit) {
        emit_frame_drops(*fit, drops);
        // Stop AFTER dropping the loop-body frame: break/continue leaves the
        // loop via its edge, so the iteration's locals (incl. this frame's) are
        // released here; outer (enclosing-fn) frames stay live. A labeled
        // break/continue crosses `cross` inner loop bodies first. A closure
        // boundary also stops the walk (a break can't cross it).
        if (fit->closure_boundary) break;
        if (fit->loop_boundary) { if (cross == 0) break; --cross; }
    }
    return drops;
}
""", 1, "drops to loop")
wr(C, c)
print("patch2 applied")
