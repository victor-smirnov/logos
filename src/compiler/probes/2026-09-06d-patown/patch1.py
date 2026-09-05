import io, re, sys
def rd(p): return io.open(p).read()
def wr(p, s): io.open(p, "w").write(s)
def rep(s, old, new, n=1, tag=""):
    c = s.count(old); assert c == n, (tag, c, n, old[:80]); return s.replace(old, new)
def span(s, start, end, new, tag=""):
    i = s.find(start); assert i >= 0, (tag, "start", start[:60])
    j = s.find(end, i); assert j >= 0, (tag, "end", end[:60])
    assert s.find(start, i + 1) < 0, (tag, "start not unique")
    return s[:i] + new + s[j:]

# ── sema_impl.hpp ────────────────────────────────────────────────────────────
H = "src/compiler/sema_impl.hpp"; h = rd(H)
h = rep(h, "    std::vector<writ::Writ> blob_docs_;\n",
"""    std::vector<writ::Writ> blob_docs_;

    // ── SYNTHESIZED AST — a desugaring lowers BY DELEGATION ─────────────
    // `if let` is `match VALUE { PAT [if GUARD] => THEN, _ => ELSE }`,
    // `while let` is `loop { match … { … , _ => break } }`, a let-chain is
    // that nested once per segment. Each builds the node the ONE lowering
    // that owns the ownership protocol reads (lower_match / lower_match_expr
    // / lower_loop) and hands it over; the second copy of the arm shape had
    // no temp hoist, no scrutinee move and no arm drop (PROBES.md 2026-09-06c
    // §1c) and is gone. Nodes live in this never-move arena and reference
    // the ORIGINAL subtrees by AnyVal — a self-relative ref resolves across
    // arenas, so no source node is copied and a side table keyed on a source
    // node's address (extending_borrow_nodes_) stays valid.
    writ::Writ synth_doc_;
    writ::AnyVal synth_node(int32_t code, uint32_t line,
                            std::initializer_list<std::pair<uint8_t, writ::AnyVal>> keys);
    writ::AnyVal synth_array(const std::vector<writ::AnyVal>& items);
    writ::AnyVal synth_block(const std::vector<writ::AnyVal>& stmts, uint32_t line);
    writ::AnyVal synth_match(writ::AnyVal scrut, writ::AnyVal pat, writ::AnyVal guard,
                             writ::AnyVal then_body, writ::AnyVal else_body, uint32_t line);
    writ::AnyVal synth_let_chain(writ::TinyMapView node, writ::AnyVal then_body,
                                 writ::AnyVal else_body, uint32_t line);
    // An arm body / else branch position that must hold a BLOCK.
    writ::AnyVal synth_as_block(writ::AnyVal body, uint32_t line);
""", 1, "hpp blob_docs")
h = rep(h, """    // G156-2: mark a by-value move-type match scrutinee var as moved when an
    // unguarded arm binds+moves it (whole-binding / struct / tuple / variant
    // payload). Shared by the statement and expression match paths so the
    // enum's scope-exit Drop doesn't double-free a value a binding/result owns.
    void mark_match_scrutinee_moved(const lir::LExprPtr& scrut, TypeRef scrut_type,
                                    writ::TinyMapView node);
""", """    // G156-2: mark a by-value move-type match scrutinee (var or place) moved
    // when THIS arm's pattern binds+moves out of it (whole-binding / struct /
    // tuple / variant payload). Called PER ARM, inside the arm's own move
    // state, by the statement and expression match paths (so an arm that binds
    // nothing leaves the scrutinee a flagged drop — the `_ => {}` arm over an
    // unmatched payload used to leak it) and once by let-else for its pattern.
    void mark_match_scrutinee_moved(const lir::LExprPtr& scrut, TypeRef scrut_type,
                                    writ::TinyMapView arm_lhs);
""", 1, "hpp mark decl")
wr(H, h)

# ── sema_stmt.cpp ────────────────────────────────────────────────────────────
P = "src/compiler/sema_stmt.cpp"; s = rd(P)
# 1. lower_stmt_inner: the chain statement
s = rep(s, """    if (c == la::IF_LET_CHAIN) {
        // §6.4: route to the expression-form desugar; wrap result
        // as a stmt-expr so the chain works in statement position
        // (the canonical port shape).
        auto e = lower_if_let_chain(stmt);
        return builder().stmt_expr(std::move(e), node_line_);
    }
""", """    if (c == la::IF_LET_CHAIN) {
        // §6.4: `if let P1 = e1 && … { THEN } else { ELSE }` is the nested
        // MATCH/IF tree synth_let_chain builds; the first segment is a let,
        // so the root is a MATCH statement.
        writ::AnyVal else_body = stmt.has_key(la::ELSE)
            ? stmt.get(la::ELSE.code) : synth_block({}, node_line_);
        return lower_match(map_of(synth_let_chain(stmt, stmt.get(la::THEN.code),
                                                  else_body, node_line_)));
    }
""", 1, "chain stmt")
# 2. lower_if PAT branch
s = span(s, "    // ── if let pattern = expr { ... } ─────────────────────────────\n    if (node.has_key(la::PAT)) {\n        auto scrut = node.has_key(la::VALUE)\n",
            "    // ── regular if cond { ... } ",
"""    // ── if let PAT = VALUE [&& GUARD] THEN [else ELSE] ────────────
    //   ≡ match VALUE { PAT [if GUARD] => THEN, _ => ELSE }
    // Lowered BY DELEGATION to lower_match (see synth_doc_).
    if (node.has_key(la::PAT)) {
        writ::AnyVal else_body = node.has_key(la::ELSE)
            ? node.get(la::ELSE.code) : synth_block({}, if_line);
        auto m = synth_match(node.get(la::VALUE.code), node.get(la::PAT.code),
                             node.get(la::GUARD.code), node.get(la::THEN.code),
                             else_body, if_line);
        return lower_match(map_of(m));
    }

""", "lower_if pat")
# 3. lower_while chain + PAT branches
s = span(s, "    // §6.4: while-let CHAIN (multi-seg). Desugar source-text to\n",
            "    // ── regular while cond { ... } ",
"""    // ── while let PAT = VALUE [&& GUARD] BODY ───────────────────────
    //   ≡ loop { match VALUE { PAT [if GUARD] => BODY, _ => break } }
    // and the chain form (`while let P1 = e1 && … BODY`, ITEMS = the
    // segments) nests one MATCH / IF per segment with `break` at every
    // fall-through. Lowered BY DELEGATION to lower_loop → lower_match (see
    // synth_doc_); the scrutinee is re-evaluated per iteration because the
    // match statement lives inside the loop body, and the label is taken by
    // lower_loop from pending_loop_label_ exactly as a written loop's is.
    if (node.has_key(la::PAT) || (node.has_key(la::ITEMS) && node.has_key(la::BODY))) {
        auto brk = synth_block({synth_node(la::BREAK.code, while_line, {})}, while_line);
        writ::AnyVal m = node.has_key(la::PAT)
            ? synth_match(node.get(la::VALUE.code), node.get(la::PAT.code),
                          node.get(la::GUARD.code), node.get(la::BODY.code), brk, while_line)
            : synth_let_chain(node, node.get(la::BODY.code), brk, while_line);
        auto lp = synth_node(la::LOOP.code, while_line,
                             {{la::BODY.code, synth_block({m}, while_line)}});
        return lower_loop(map_of(lp));
    }

""", "lower_while")
# 4. synth helpers, defined right before lower_if
s = rep(s, "lir_view::StmtRef SemaChecker::lower_if(TinyMapView node) {\n",
"""// ── SYNTHESIZED AST (see synth_doc_ in sema_impl.hpp) ─────────────────────
writ::AnyVal SemaChecker::synth_node(int32_t code, uint32_t line,
        std::initializer_list<std::pair<uint8_t, writ::AnyVal>> keys) {
    if (synth_doc_.is_null()) synth_doc_ = writ::make_doc(1u << 20).get();  // MultiChunk: never moves
    auto* m = synth_doc_.make_tiny_map(keys.size() + 2).get();
    auto& ar = synth_doc_.arena();
    m->put(la::CODE.code, writ::AnyVal::from_value(code), ar).get();
    if (line) m->put(la::SRC_LINE.code, writ::AnyVal::from_value(line), ar).get();
    for (const auto& kv : keys)
        if (!kv.second.is_null()) m->put(kv.first, kv.second, ar).get();
    writ::AnyVal a; a.set_ref(m); return a;
}

writ::AnyVal SemaChecker::synth_array(const std::vector<writ::AnyVal>& items) {
    if (synth_doc_.is_null()) synth_doc_ = writ::make_doc(1u << 20).get();
    auto arr = synth_doc_.make_array(items.empty() ? 1 : items.size()).get();
    for (const auto& it : items) arr.push_back(it).get();
    return arr.to_anyval();
}

writ::AnyVal SemaChecker::synth_block(const std::vector<writ::AnyVal>& stmts, uint32_t line) {
    return synth_node(la::BLOCK.code, line, {{la::ITEMS.code, synth_array(stmts)}});
}

writ::AnyVal SemaChecker::synth_as_block(writ::AnyVal body, uint32_t line) {
    return code_of(map_of(body)) == la::BLOCK ? body : synth_block({body}, line);
}

// match SCRUT { PAT [if GUARD] => THEN, _ => ELSE }
writ::AnyVal SemaChecker::synth_match(writ::AnyVal scrut, writ::AnyVal pat, writ::AnyVal guard,
                                      writ::AnyVal then_body, writ::AnyVal else_body,
                                      uint32_t line) {
    auto arm1 = synth_node(la::MATCH_ARM.code, line,
                           {{la::LHS.code, pat}, {la::GUARD.code, guard}, {la::BODY.code, then_body}});
    auto arm2 = synth_node(la::MATCH_ARM.code, line,
                           {{la::LHS.code, synth_node(la::PAT_WILD.code, 0, {})},
                            {la::BODY.code, else_body}});
    return synth_node(la::MATCH.code, line,
                      {{la::VALUE.code, scrut}, {la::ITEMS.code, synth_array({arm1, arm2})}});
}

// §6.4 let-chain: `let P1 = e1 && c && let P2 = e2 …` → one MATCH per let
// segment, one IF per bool segment, nested inside-out, ELSE at every
// fall-through (the ELSE subtree is REFERENCED from each site and lowered at
// each; a diverging ELSE is the port shape). The grammar guarantees the first
// segment is a let, so the root is a MATCH.
writ::AnyVal SemaChecker::synth_let_chain(TinyMapView node, writ::AnyVal then_body,
                                          writ::AnyVal else_body, uint32_t line) {
    auto wrapper = map_of(node.get(la::ITEMS.code));
    if (wrapper.is_null() || !wrapper.has_key(la::ITEMS)) {
        error("let-chain: wrapper has no ITEMS array");
        return synth_block({}, line);
    }
    auto segs = arr_of(wrapper.get(la::ITEMS.code));
    else_body = synth_as_block(else_body, line);
    writ::AnyVal cur = synth_as_block(then_body, line);
    for (uint64_t i = segs.size(); i-- > 0; ) {
        auto seg = map_of(segs.get(i));
        const uint32_t sl = get_line(seg) ? get_line(seg) : line;
        if (code_of(seg) == la::LET_CHAIN_LET) {
            cur = synth_match(seg.get(la::VALUE.code), seg.get(la::PAT.code), {},
                              cur, else_body, sl);
        } else if (code_of(seg) == la::LET_CHAIN_COND) {
            cur = synth_node(la::IF.code, sl, {{la::COND.code, seg.get(la::VALUE.code)},
                                               {la::THEN.code, cur},
                                               {la::ELSE.code, else_body}});
        } else {
            error(std::format("let-chain: unexpected seg CODE {}", code_of(seg)));
            return synth_block({}, line);
        }
        if (i > 0) cur = synth_block({cur}, sl);
    }
    return cur;
}

lir_view::StmtRef SemaChecker::lower_if(TinyMapView node) {
""", 1, "synth defs")
# 5. mark_match_scrutinee_moved: one arm's LHS
s = rep(s, """void SemaChecker::mark_match_scrutinee_moved(const lir::LExprPtr& scrut,
                                              TypeRef scrut_type,
                                              writ::TinyMapView node) {
""", """void SemaChecker::mark_match_scrutinee_moved(const lir::LExprPtr& scrut,
                                              TypeRef scrut_type,
                                              writ::TinyMapView arm_lhs) {
""", 1, "mark def head")
s = rep(s, """    if (scrut && scrut_type && is_move_type(scrut_type) &&
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
""", """    if (scrut && scrut_type && is_move_type(scrut_type) &&
        scrut_is_place && !arm_lhs.is_null()) {
        {
            {
                auto lhs = arm_lhs;
""", 1, "mark head")
s = rep(s, "mark_moved_target(); break;", "mark_moved_target(); return;", 4, "mark breaks")
# 6. lower_match / lower_match_expr: the pre-arm mark becomes a per-arm mark
s = rep(s, """    // A whole-value binding arm (`x => …` — an UNGUARDED `PAT_WILD` that
    // carries a real name, not `_`) moves an owned move-type scrutinee into
    // the binding: Rust's by-value match move, `match v { x => … }` ≡
    // `{ let x = v; … }`. Mark the scrutinee var moved so it is not dropped a
    // SECOND time after the match — the binding's own drop already fires at arm
    // end (match-arm bindings drop, a6a04330). Without this an owned Vec/String
    // scrutinee is double-freed (SIGSEGV). Restricted to an unguarded binding
    // arm (which always matches → unconditional move); guarded binding arms
    // leave the scrutinee conditionally live for later arms.
    mark_match_scrutinee_moved(scrut, scrut_type, node);

""", """    // A binding arm (`x => …`, `Some(r) => …`, `(a, b) => …`) moves an owned
    // move-type scrutinee (or its payload) into the binding: Rust's by-value
    // match move. The scrutinee is marked moved INSIDE each such arm (see the
    // arm loop) so the binding's own arm-end drop is the only one on that path
    // and an arm that binds nothing leaves a flagged scope-exit drop.

""", 1, "pre-arm mark stmt")
s = rep(s, """    // G156-2: a match-EXPRESSION that binds+moves a payload out of a by-value
    // move-type enum/struct/tuple scrutinee (`let x = match body { Ok(s) => s }`)
    // must mark the scrutinee moved so its scope-exit Drop doesn't double-free a
    // value the result already owns. Same per-arm analysis as the statement
    // match path (shared helper). (Was: lforge read_manifest / graph_cas
    // double-free.)
    mark_match_scrutinee_moved(scrut, scrut_type, node);

""", """    // G156-2: a match-EXPRESSION that binds+moves a payload out of a by-value
    // move-type enum/struct/tuple scrutinee (`let x = match body { Ok(s) => s }`)
    // marks the scrutinee moved inside each binding arm (see the arm loop) so
    // its scope-exit Drop doesn't double-free a value the result already owns.
    // (Was: lforge read_manifest / graph_cas double-free.)

""", 1, "pre-arm mark expr")
s = rep(s, """            std::vector<lir_view::StmtRef> body;
            if (arm.has_key(la::BODY)) {
                auto body_node = map_of(arm.get(la::BODY.code));
                if (code_of(body_node) == la::BLOCK) {
                    lower_block(body_node).each_stmt([&](lir_view::StmtRef s){ body.push_back(s); });
""", """            // This arm OWNS what its pattern binds by value — on THIS arm's
            // path only (the arm is one CondMoveBranch; an arm that binds
            // nothing leaves the scrutinee a flagged drop). After the guard,
            // which may still read the scrutinee.
            if (arm.has_key(la::LHS))
                mark_match_scrutinee_moved(smatch.scrut, scrut_type, effective_lhs(arm, alt_idx));

            std::vector<lir_view::StmtRef> body;
            if (arm.has_key(la::BODY)) {
                auto body_node = map_of(arm.get(la::BODY.code));
                if (code_of(body_node) == la::BLOCK) {
                    lower_block(body_node).each_stmt([&](lir_view::StmtRef s){ body.push_back(s); });
""", 1, "per-arm mark stmt")
s = rep(s, """            lir::LExprPtr val = nullptr;
            bool arm_diverges = false;   // move/uninit merge below
            if (arm.has_key(la::EXPR)) {
""", """            // The arm owns what its pattern binds by value — see lower_match.
            if (arm.has_key(la::LHS))
                mark_match_scrutinee_moved(me.scrut, scrut_type, effective_lhs(arm, alt_idx));

            lir::LExprPtr val = nullptr;
            bool arm_diverges = false;   // move/uninit merge below
            if (arm.has_key(la::EXPR)) {
""", 1, "per-arm mark expr")
# 7. let-else: the pattern owns what it binds by value
s = rep(s, """    pop_scope();

    // 4. Add pattern bindings to outer scope
""", """    pop_scope();

    // The pattern's bindings OWN what they bind by value: the scrutinee (var or
    // place) is marked moved so its scope-exit drop does not fire a second time
    // over a payload a binding already dropped. After the else block, which
    // may still read the scrutinee (no move happened on that path).
    mark_match_scrutinee_moved(scrut, scrut_type, pat_node);

    // 4. Add pattern bindings to outer scope
""", 1, "let-else mark")
# 8. nested-struct sub-pattern prologue: the binding takes the field OUT of the synth
s = rep(s, """            auto fr = builder().field_read(std::move(sref), fname, ftype);
            lir::SLet sl;
            sl.name = bind; sl.type = ftype; sl.is_mut = bmut_;
""", """            auto fr = builder().field_read(std::move(sref), fname, ftype);
            // The binding moves the field OUT of the synth — mark synth.<f>
            // moved so the synth's scope-exit Drop skips it (the tuple branch
            // above does the same for its elements; without it: double drop).
            if (is_move_type(ftype)) mark_moved_expr(expr_ref_of(fr));
            lir::SLet sl;
            sl.name = bind; sl.type = ftype; sl.is_mut = bmut_;
""", 1, "substruct mark")
wr(P, s)

# ── sema_expr.cpp ────────────────────────────────────────────────────────────
E = "src/compiler/sema_expr.cpp"; e = rd(E)
e = span(e, "    // B97: if-let in expression position. The stmt form handles it via\n",
            "    lir::LExprPtr cond = nullptr;\n    if (node.has_key(la::COND)) {\n        cond = lower_expr(map_of(node.get(la::COND.code)));\n        if (TypeRef(expr_type(cond)).kind() != LogosType::Kind::Bool &&\n            TypeRef(expr_type(cond)).kind() != LogosType::Kind::Error &&\n            TypeRef(expr_type(cond)).kind() != LogosType::Kind::Never)  // G160-10\n",
"""    // if-let in expression position ≡ `match VALUE { PAT [if GUARD] => THEN,
    // _ => ELSE }` as an expression — lowered BY DELEGATION to
    // lower_match_expr (see synth_doc_). An `else if …` else branch is the
    // block `{ <if-expr> }`.
    if (node.has_key(la::PAT)) {
        if (!node.has_key(la::ELSE)) {
            error("if-let-as-expression requires an else branch");
            return error_expr();
        }
        const uint32_t line = get_line(node) ? get_line(node) : node_line_;
        writ::AnyVal else_body = node.get(la::ELSE.code);
        if (code_of(map_of(else_body)) != la::BLOCK)
            else_body = synth_block({synth_node(la::TAIL_EXPR.code, line,
                                                {{la::VALUE.code, else_body}})}, line);
        auto m = synth_match(node.get(la::VALUE.code), node.get(la::PAT.code),
                             node.get(la::GUARD.code), node.get(la::THEN.code),
                             else_body, line);
        return lower_match_expr(map_of(m));
    }

""", "if_expr pat")
e = span(e, "lir::LExprPtr SemaChecker::lower_if_let_chain(TinyMapView node) {\n",
            "    return lower_reparsed_tail_expr(wrapped, \"if-let-chain\");\n}\n",
"""lir::LExprPtr SemaChecker::lower_if_let_chain(TinyMapView node) {
    // §6.4: the chain in EXPRESSION position — the nested MATCH/IF tree
    // synth_let_chain builds, lowered as a match expression (its root is the
    // first let segment's MATCH). Every branch must yield the value, so the
    // else branch is required exactly as for `if let … else …`.
    if (!node.has_key(la::ITEMS) || !node.has_key(la::THEN)) {
        error("if-let-chain: missing ITEMS or THEN");
        return error_expr();
    }
    if (!node.has_key(la::ELSE)) {
        error("if-let-as-expression requires an else branch");
        return error_expr();
    }
    const uint32_t line = get_line(node) ? get_line(node) : node_line_;
    writ::AnyVal else_body = node.get(la::ELSE.code);
    if (code_of(map_of(else_body)) != la::BLOCK)
        else_body = synth_block({synth_node(la::TAIL_EXPR.code, line,
                                            {{la::VALUE.code, else_body}})}, line);
    return lower_match_expr(map_of(synth_let_chain(node, node.get(la::THEN.code),
                                                   else_body, line)));
""", "chain expr")
# the old function's closing brace survives the span (end anchor kept the "}\n")
e = rep(e, "                                                   else_body, line)));\n    return lower_reparsed_tail_expr(wrapped, \"if-let-chain\");\n}\n",
           "                                                   else_body, line)));\n}\n", 1, "chain tail")
wr(E, e)
print("patch1 applied")
