// ADR 0030 — the core layer (HIR). See hir_lower.hpp.

#include "hir_lower.hpp"

#include <logos/compiler/ast.hpp>

namespace logos::compiler::hir {

namespace la = logos::compiler::ast;
using writ::AnyVal;
using writ::TinyMapView;

namespace {

bool is_map(AnyVal v) noexcept {
    if (v.is_null() || !v.is_pointer()) return false;
    const uint8_t* p = v.resolve();
    return p && writ::TypeTag::read_before(p).type_code() == writ::type_hash::TinyObjectMap;
}
bool is_array(AnyVal v) noexcept {
    if (v.is_null() || !v.is_pointer()) return false;
    const uint8_t* p = v.resolve();
    return p && writ::TypeTag::read_before(p).type_code() == writ::type_hash::Array;
}
TinyMapView map_of(AnyVal v) noexcept { return is_map(v) ? TinyMapView(v, nullptr) : TinyMapView{}; }
int32_t code_of(TinyMapView n) noexcept {
    if (n.is_null()) return -1;
    AnyVal c = n.get(la::CODE.code);
    return c.is_null() || !c.is_value() ? -1 : c.as_value<int32_t>();
}
// Identity of two slots: a Ref is SELF-RELATIVE and re-anchors when copied,
// so two copies of one reference differ in raw bits — compare what they point
// at; compare an inline value by its bits.
bool same(AnyVal a, AnyVal b) noexcept {
    if (a.is_pointer() != b.is_pointer()) return false;
    return a.is_pointer() ? a.resolve() == b.resolve() : a.raw() == b.raw();
}

// Subtrees the pass does not enter: quote bodies and meta blocks are TEMPLATES
// (their antiquote placeholders are substituted before the result is lowered,
// and the spliced result goes through this pass then), not code.
bool opaque(int32_t c) noexcept {
    return c == la::QUOTE_ITEM.code || c == la::QUOTE_EXPR.code || c == la::QUOTE_TY.code ||
           c == la::META_BLOCK.code;
}

} // namespace

Lowering::Lowering() : doc_(writ::make_doc(1u << 20).get()) {}   // MultiChunk: never moves

bool Lowering::is_surface(TinyMapView n) noexcept {
    const int32_t c = code_of(n);
    if (c == la::IF_LET_CHAIN.code) return true;
    if (c == la::IF.code && n.has_key(la::PAT)) return true;
    if (c == la::WHILE.code && (n.has_key(la::PAT) || (n.has_key(la::ITEMS) && n.has_key(la::BODY))))
        return true;
    return false;
}

AnyVal Lowering::lower_body(AnyVal node, bool stmt) {
    return lower(node, stmt ? Ctx::Stmt : Ctx::Expr);
}

AnyVal Lowering::lower(AnyVal v, Ctx ctx) {
    if (is_map(v)) return lower_map(v, ctx);
    return v;
}

AnyVal Lowering::lower_map(AnyVal v, Ctx ctx) {
    TinyMapView n = map_of(v);
    const int32_t c = code_of(n);
    if (opaque(c)) return v;
    // The context of each child: a block's items are statements, as are a
    // statement `if`'s else branch and a statement `match`'s arm bodies (and
    // the loop a label wraps); everything else is an expression.
    auto child_ctx = [&](uint8_t key) -> Ctx {
        if (c == la::BLOCK.code && key == la::ITEMS.code) return Ctx::Stmt;
        if (c == la::IF.code && ctx == Ctx::Stmt && key == la::ELSE.code) return Ctx::Stmt;
        if (c == la::MATCH_ARM.code && ctx == Ctx::Stmt && key == la::BODY.code) return Ctx::Stmt;
        if (c == la::LABELED_LOOP.code && key == la::BODY.code) return Ctx::Stmt;
        return Ctx::Expr;
    };
    // A MATCH's arms inherit the match's context (their BODY is a statement
    // only for a statement match).
    auto arm_ctx = [&]() { return ctx; };

    AnyVal vals[writ::TinyObjectMap::MAX_KEYS];
    bool changed = false;
    const uint64_t bits = n.bitmap();
    for (uint8_t k = 0; k < writ::TinyObjectMap::MAX_KEYS; ++k) {
        if (!(bits & (1ull << k))) continue;
        AnyVal cv = n.get(k);
        AnyVal nv = cv;
        if (is_map(cv)) {
            nv = lower_map(cv, child_ctx(k));
        } else if (is_array(cv)) {
            writ::ArrayView a(cv, nullptr);
            std::vector<AnyVal> items;
            items.reserve(a.size());
            bool arr_changed = false;
            const Ctx ec = (c == la::MATCH.code && k == la::ITEMS.code) ? arm_ctx() : child_ctx(k);
            for (uint64_t i = 0; i < a.size(); ++i) {
                AnyVal e = a.get(i);
                AnyVal ne = lower(e, ec);
                if (!same(e, ne)) arr_changed = true;
                items.push_back(ne);
            }
            if (arr_changed) nv = array(items);
        }
        if (!same(cv, nv)) changed = true;
        vals[k] = nv;
    }
    AnyVal cur = v;
    if (changed) {
        auto* m = doc_.make_tiny_map(n.size() + 1).get();
        auto& ar = doc_.arena();
        for (uint8_t k = 0; k < writ::TinyObjectMap::MAX_KEYS; ++k)
            if (bits & (1ull << k)) m->put(k, vals[k], ar).get();
        m->set_schema_type_code(n.schema_type_code());
        cur.set_ref(m);
    }
    return is_surface(map_of(cur)) ? desugar(cur, ctx) : cur;
}

AnyVal Lowering::desugar(AnyVal v, Ctx ctx) {
    TinyMapView n = map_of(v);
    const int32_t c = code_of(n);
    // An `else if …` / a non-block else of an EXPRESSION yields its value from
    // a block tail; a missing else of an expression is recorded in ORIGIN so
    // sema refuses it with the construct's own sentence.
    auto expr_else = [&](Origin with, Origin without, Origin& o) -> AnyVal {
        if (!n.has_key(la::ELSE)) { o = without; return block({}, n, without); }
        o = with;
        AnyVal e = n.get(la::ELSE.code);
        if (code_of(map_of(e)) == la::BLOCK.code) return e;
        return block({node(la::TAIL_EXPR.code, n, with, {{la::VALUE.code, e}})}, n, with);
    };
    if (c == la::IF.code) {                      // if let P = e [&& g] { A } [else B]
        Origin o = Origin::IfLet;
        AnyVal else_body = ctx == Ctx::Expr
            ? expr_else(Origin::IfLet, Origin::IfLetNoElse, o)
            : (n.has_key(la::ELSE) ? n.get(la::ELSE.code) : block({}, n, Origin::IfLet));
        return match_of(n.get(la::VALUE.code), n.get(la::PAT.code), n.get(la::GUARD.code),
                        n.get(la::THEN.code), else_body, n, o);
    }
    if (c == la::IF_LET_CHAIN.code) {
        Origin o = Origin::LetChain;
        AnyVal else_body = ctx == Ctx::Expr
            ? expr_else(Origin::LetChain, Origin::LetChainNoElse, o)
            : (n.has_key(la::ELSE) ? n.get(la::ELSE.code) : block({}, n, Origin::LetChain));
        return let_chain(n, n.get(la::THEN.code), else_body, o);
    }
    if (c == la::WHILE.code) {                   // while let … { A }
        const Origin o = n.has_key(la::PAT) ? Origin::WhileLet : Origin::WhileLetChain;
        AnyVal brk = block({node(la::BREAK.code, n, o, {})}, n, o);
        AnyVal m = n.has_key(la::PAT)
            ? match_of(n.get(la::VALUE.code), n.get(la::PAT.code), n.get(la::GUARD.code),
                       n.get(la::BODY.code), brk, n, o)
            : let_chain(n, n.get(la::BODY.code), brk, o);
        return node(la::LOOP.code, n, o, {{la::BODY.code, block({m}, n, o)}});
    }
    return v;
}

// ── builders ────────────────────────────────────────────────────────────────

AnyVal Lowering::node(int32_t code, TinyMapView from, Origin o,
                      std::initializer_list<std::pair<uint8_t, AnyVal>> keys) {
    auto* m = doc_.make_tiny_map(keys.size() + 4).get();
    auto& ar = doc_.arena();
    m->put(la::CODE.code, AnyVal::from_value(code), ar).get();
    for (uint8_t pk : {la::SRC_LINE.code, la::SRC_SPAN.code}) {
        AnyVal pv = from.is_null() ? AnyVal{} : from.get(pk);
        if (!pv.is_null() && pv.is_value()) m->put(pk, pv, ar).get();
    }
    if (o != Origin::User)
        m->put(la::ORIGIN.code, AnyVal::from_value(static_cast<int64_t>(o)), ar).get();
    for (const auto& kv : keys)
        if (!kv.second.is_null()) m->put(kv.first, kv.second, ar).get();
    AnyVal a; a.set_ref(m); return a;
}

AnyVal Lowering::array(const std::vector<AnyVal>& items) {
    auto arr = doc_.make_array(items.empty() ? 1 : items.size()).get();
    for (const auto& it : items) arr.push_back(it).get();
    return arr.to_anyval();
}

AnyVal Lowering::block(const std::vector<AnyVal>& stmts, TinyMapView from, Origin o) {
    return node(la::BLOCK.code, from, o, {{la::ITEMS.code, array(stmts)}});
}

AnyVal Lowering::as_block(AnyVal body, TinyMapView from, Origin o) {
    return code_of(map_of(body)) == la::BLOCK.code ? body : block({body}, from, o);
}

// match SCRUT { PAT [if GUARD] => THEN, _ => ELSE }. PAT on the MATCH node
// records that the match was WRITTEN as a let form: its `_` arm is the else
// branch, not a user arm (the arm-after-catchall lint reads it).
AnyVal Lowering::match_of(AnyVal scrut, AnyVal pat, AnyVal guard, AnyVal then_body,
                          AnyVal else_body, TinyMapView from, Origin o) {
    AnyVal arm1 = node(la::MATCH_ARM.code, from, o,
                       {{la::LHS.code, pat}, {la::GUARD.code, guard}, {la::BODY.code, then_body}});
    AnyVal arm2 = node(la::MATCH_ARM.code, from, o,
                       {{la::LHS.code, node(la::PAT_WILD.code, TinyMapView{}, o, {})},
                        {la::BODY.code, else_body}});
    return node(la::MATCH.code, from, o,
                {{la::VALUE.code, scrut}, {la::ITEMS.code, array({arm1, arm2})}, {la::PAT.code, pat}});
}

// `let P1 = e1 && c && let P2 = e2 …` → one MATCH per let segment, one IF per
// bool segment, nested inside-out, ELSE at every fall-through (the ELSE
// subtree is shared by each site and lowered at each; a diverging ELSE is the
// port shape). The grammar guarantees the first segment is a let.
AnyVal Lowering::let_chain(TinyMapView n, AnyVal then_body, AnyVal else_body, Origin o) {
    TinyMapView wrapper = map_of(n.get(la::ITEMS.code));
    if (wrapper.is_null() || !wrapper.has_key(la::ITEMS)) {
        diags_.push_back({0, "let-chain: wrapper has no ITEMS array"});
        return block({}, n, o);
    }
    writ::ArrayView segs(wrapper.get(la::ITEMS.code), nullptr);
    else_body = as_block(else_body, n, o);
    AnyVal cur = as_block(then_body, n, o);
    for (uint64_t i = segs.size(); i-- > 0; ) {
        TinyMapView seg = map_of(segs.get(i));
        TinyMapView at = seg.has_key(la::SRC_LINE) ? seg : n;
        if (code_of(seg) == la::LET_CHAIN_LET.code) {
            cur = match_of(seg.get(la::VALUE.code), seg.get(la::PAT.code), AnyVal{},
                           cur, else_body, at, o);
        } else if (code_of(seg) == la::LET_CHAIN_COND.code) {
            cur = node(la::IF.code, at, o, {{la::COND.code, seg.get(la::VALUE.code)},
                                            {la::THEN.code, cur}, {la::ELSE.code, else_body}});
        } else {
            diags_.push_back({0, "let-chain: unexpected segment"});
            return block({}, n, o);
        }
        if (i > 0) cur = block({cur}, at, o);
    }
    return cur;
}

} // namespace logos::compiler::hir
