// ADR 0030 — the core layer (HIR). See hir_lower.hpp.

#include "hir_lower.hpp"

#include <logos/compiler/ast.hpp>

#include "logos_parser.hpp"
#include "sema_fmt.hpp"

#include <format>

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

uint32_t line_of(TinyMapView n) {
    AnyVal v = n.is_null() ? AnyVal{} : n.get(la::SRC_LINE.code);
    return v.is_null() || !v.is_value() ? 0 : v.as_value<uint32_t>();
}
std::string_view text_of(TinyMapView n, uint8_t key) {
    AnyVal v = n.is_null() ? AnyVal{} : n.get(key);
    if (v.is_null() || !v.is_pointer()) return {};
    return writ::StringView(v, nullptr).view();
}
Origin origin_of(TinyMapView n) noexcept {
    AnyVal o = n.is_null() ? AnyVal{} : n.get(la::ORIGIN.code);
    return o.is_null() || !o.is_value() ? Origin::User : static_cast<Origin>(o.as_value<int64_t>());
}
bool ends_in_tail(TinyMapView blk) noexcept {
    if (blk.is_null() || !blk.has_key(la::ITEMS)) return false;
    writ::ArrayView items(blk.get(la::ITEMS.code), nullptr);
    return items.size() > 0 && code_of(map_of(items.get(items.size() - 1))) == la::TAIL_EXPR.code;
}

// Subtrees the pass does not enter: quote bodies and meta blocks are TEMPLATES
// (their antiquote placeholders are substituted before the result is lowered,
// and the spliced result goes through this pass then), not code. A metacall
// is compile-time code run in its own thunk module (whose body goes through
// this pass when sema lowers it), and the driver PATCHES the metacall node in
// the AST document by its offset — so it must keep its identity, never be
// copied into this pass's document.
bool opaque(int32_t c) noexcept {
    return c == la::QUOTE_ITEM.code || c == la::QUOTE_EXPR.code || c == la::QUOTE_TY.code ||
           c == la::META_BLOCK.code || c == la::METACALL.code || c == la::METACALL_ITEM.code;
}

} // namespace

Lowering::Lowering() : doc_(writ::make_doc(1u << 20).get()) {}   // MultiChunk: never moves

bool Lowering::is_surface(TinyMapView n) noexcept {
    const int32_t c = code_of(n);
    if (c == la::IF_LET_CHAIN.code) return true;
    if (c == la::IF.code && n.has_key(la::PAT)) return true;
    if (c == la::WHILE.code) return true;   // every form: `while c`, `while let`, chains
    if (c == la::RETURN_EXPR.code || c == la::BREAK_EXPR.code || c == la::CONTINUE_EXPR.code)
        return true;
    if (c == la::DESTRUCTURE_ASSIGN.code) return true;
    if (c == la::FIELD_SHORTHAND.code) return true;
    if (c == la::LABELED_BLOCK.code) return true;
    return false;
}

AnyVal Lowering::lower_body(AnyVal node, bool stmt, bool fragment) {
    fragment_ = fragment;
    loops_.clear(); barriers_.clear(); labeled_body_ = false; valued_loops_.clear();
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
    if (c == la::FN_MACRO_CALL.code) return expand_macro(v);
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

    // Loop scopes. A LABELED_LOOP's scope covers its BODY (the loop it names,
    // or a labeled block); a plain loop's covers every key but its iterated
    // expression / range bounds. A closure or nested fn is a barrier.
    const bool is_loop = c == la::LOOP.code || c == la::WHILE.code || c == la::FOR.code ||
                         c == la::FOR_EACH.code;
    const bool labeled_block = c == la::LABELED_BLOCK.code;
    const bool labeled = c == la::LABELED_LOOP.code || labeled_block;
    const bool barrier = c == la::CLOSURE_EXPR.code || c == la::NESTED_FN.code;
    bool own_scope = false;
    LoopScope mine;
    if (labeled) {
        loops_.push_back({std::string(text_of(n, la::LABEL.code)), labeled_block, false});
        // The statement form's BODY is the loop node it names (the expression
        // form's is the loop's block).
        const int32_t bc = code_of(map_of(n.get(la::BODY.code)));
        labeled_body_ = !labeled_block && (bc == la::LOOP.code || bc == la::WHILE.code ||
                                           bc == la::FOR.code || bc == la::FOR_EACH.code);
    } else if (is_loop) {
        own_scope = !labeled_body_;
        labeled_body_ = false;
    }
    if (barrier) barriers_.push_back({loops_.size(), c == la::CLOSURE_EXPR.code});
    auto inside = [&](uint8_t k) {
        if (c == la::FOR.code) return k != la::LHS.code && k != la::RHS.code;
        if (c == la::FOR_EACH.code) return k != la::ITER.code;
        return true;
    };

    AnyVal vals[writ::TinyObjectMap::MAX_KEYS];
    bool changed = false;
    const uint64_t bits = n.bitmap();
    for (uint8_t k = 0; k < writ::TinyObjectMap::MAX_KEYS; ++k) {
        if (!(bits & (1ull << k))) continue;
        AnyVal cv = n.get(k);
        AnyVal nv = cv;
        const bool scoped = own_scope && inside(k) && (is_map(cv) || is_array(cv));
        if (scoped) loops_.push_back(mine);
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
        if (scoped) { mine = loops_.back(); loops_.pop_back(); }
        if (!same(cv, nv)) changed = true;
        vals[k] = nv;
    }
    if (barrier) barriers_.pop_back();
    bool valued = own_scope && mine.valued;
    if (labeled) {
        valued = loops_.back().valued;
        loops_.pop_back();
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
    if (c == la::BREAK.code || c == la::CONTINUE.code || c == la::BREAK_EXPR.code ||
        c == la::CONTINUE_EXPR.code)
        cur = resolve_exit(cur);
    // A block ending in a `loop` statement whose breaks carry a value: the
    // loop is the block's tail expression.
    if (c == la::BLOCK.code && n.has_key(la::ITEMS)) {
        writ::ArrayView items(map_of(cur).get(la::ITEMS.code), nullptr);
        if (items.size() > 0) {
            AnyVal last = items.get(items.size() - 1);
            if (last.is_pointer() && std::find(valued_loops_.begin(), valued_loops_.end(),
                                               static_cast<const void*>(last.resolve())) != valued_loops_.end()) {
                std::vector<AnyVal> all;
                for (uint64_t i = 0; i + 1 < items.size(); ++i) all.push_back(items.get(i));
                all.push_back(node(la::TAIL_EXPR.code, map_of(last), Origin::User,
                                   {{la::VALUE.code, loop_as_expr(last)}}));
                cur = recoded(map_of(cur), la::BLOCK.code, Origin::User);
                cur = node(la::BLOCK.code, map_of(cur), Origin::User, {{la::ITEMS.code, array(all)}});
            }
        }
    }
    AnyVal out = is_surface(map_of(cur)) ? desugar(cur, ctx) : cur;
    // A labeled block yields its tail through the break the desugaring adds.
    if (labeled_block && ends_in_tail(map_of(n.get(la::BODY.code)))) valued = true;
    if (valued && out.is_pointer()) valued_loops_.push_back(out.resolve());
    return out;
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
    if (c == la::WHILE.code && n.has_key(la::COND)) {   // while c { A }
        AnyVal brk = block({node(la::BREAK.code, n, Origin::While, {})}, n, Origin::While);
        AnyVal iff = node(la::IF.code, n, Origin::While,
                          {{la::COND.code, n.get(la::COND.code)},
                           {la::THEN.code, as_block(n.get(la::BODY.code), n, Origin::While)},
                           {la::ELSE.code, brk}});
        return node(la::LOOP.code, n, Origin::While, {{la::BODY.code, block({iff}, n, Origin::While)}});
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
    if (c == la::LABELED_BLOCK.code) {           // 'a: { B }
        // A loop that runs once: its tail (if any) leaves through the break.
        const AnyVal label = n.get(la::LABEL.code), blk = n.get(la::BODY.code);
        std::vector<AnyVal> body;
        if (ends_in_tail(map_of(blk))) {
            body.push_back(node(la::BREAK.code, n, Origin::LabeledBlock,
                                {{la::LABEL.code, label}, {la::VALUE.code, blk}}));
        } else {
            body.push_back(node(la::BLOCK_STMT.code, n, Origin::LabeledBlock, {{la::BODY.code, blk}}));
            body.push_back(node(la::BREAK.code, n, Origin::LabeledBlock, {{la::LABEL.code, label}}));
        }
        // The statement form names a LOOP node, the expression form its block
        // (the grammar's two shapes).
        AnyVal lb = block(body, n, Origin::LabeledBlock);
        if (ctx == Ctx::Stmt) lb = node(la::LOOP.code, n, Origin::LabeledBlock, {{la::BODY.code, lb}});
        return node(la::LABELED_LOOP.code, n, Origin::LabeledBlock, {{la::LABEL.code, label}, {la::BODY.code, lb}});
    }
    if (c == la::DESTRUCTURE_ASSIGN.code) return destructure(n);
    if (c == la::FIELD_SHORTHAND.code)
        return node(la::FIELD_INIT.code, n, Origin::FieldShorthand,
                    {{la::NAME.code, n.get(la::NAME.code)},
                     {la::VALUE.code, node(la::VAR_REF.code, n, Origin::FieldShorthand,
                                           {{la::NAME.code, n.get(la::NAME.code)}})}});
    if (c == la::RETURN_EXPR.code || c == la::BREAK_EXPR.code || c == la::CONTINUE_EXPR.code) {
        // An exit in expression position is the statement form inside a block;
        // the keys (VALUE, LABEL) are the same.
        const int32_t sc = c == la::RETURN_EXPR.code ? la::RETURN.code
                         : c == la::BREAK_EXPR.code  ? la::BREAK.code : la::CONTINUE.code;
        const Origin o = origin_of(n) == Origin::ExitRefused ? Origin::ExitRefused : Origin::ExprExit;
        return block({recoded(n, sc, o)}, n, Origin::ExprExit);
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

// A loop statement as an expression: the statement form of a labeled loop
// (`'a: loop { B }` → LABELED_LOOP { BODY: LOOP { BODY: B } }) becomes the
// expression form (LABELED_LOOP { BODY: B }); every other loop is both.
AnyVal Lowering::loop_as_expr(AnyVal v) {
    TinyMapView n = map_of(v);
    TinyMapView inner = map_of(n.get(la::BODY.code));
    if (code_of(n) != la::LABELED_LOOP.code || code_of(inner) != la::LOOP.code) return v;
    auto* m = doc_.make_tiny_map(n.size() + 1).get();
    auto& ar = doc_.arena();
    const uint64_t bits = n.bitmap();
    for (uint8_t k = 0; k < writ::TinyObjectMap::MAX_KEYS; ++k)
        if (bits & (1ull << k))
            m->put(k, k == la::BODY.code ? inner.get(la::BODY.code) : n.get(k), ar).get();
    m->set_schema_type_code(n.schema_type_code());
    AnyVal out; out.set_ref(m); return out;
}

AnyVal Lowering::recoded(TinyMapView from, int32_t code, Origin o) {
    auto* m = doc_.make_tiny_map(from.size() + 2).get();
    auto& ar = doc_.arena();
    const uint64_t bits = from.bitmap();
    for (uint8_t k = 0; k < writ::TinyObjectMap::MAX_KEYS; ++k)
        if ((bits & (1ull << k)) && k != la::CODE.code && k != la::ORIGIN.code)
            m->put(k, from.get(k), ar).get();
    m->put(la::CODE.code, AnyVal::from_value(code), ar).get();
    m->put(la::ORIGIN.code, AnyVal::from_value(static_cast<int64_t>(o)), ar).get();
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

// ── destructuring assignment ──────────────────────────────────────────────
// rustc: `(a, b) = rhs` is `{ let (lhs0, lhs1) = rhs; a = lhs0; b = lhs1; }`.
// The places are the binding list's names; `_` and `..` stay as they are.

AnyVal Lowering::list_map(const std::vector<AnyVal>& items) {
    auto* m = doc_.make_tiny_map(1).get();
    m->put(la::ITEMS.code, array(items), doc_.arena()).get();
    AnyVal a; a.set_ref(m); return a;
}

AnyVal Lowering::str(std::string_view s) {
    return doc_.make_string(s).get().to_anyval();
}

namespace {
std::string_view name_of(TinyMapView n) {
    AnyVal v = n.is_null() ? AnyVal{} : n.get(la::NAME.code);
    if (v.is_null() || !v.is_pointer()) return {};
    return writ::StringView(v, nullptr).view();
}
// A binding list is either the `{ITEMS: [...]}` map or a bare array.
std::vector<AnyVal> list_items(AnyVal l) {
    std::vector<AnyVal> out;
    AnyVal arr = l;
    if (is_map(l)) arr = map_of(l).get(la::ITEMS.code);
    if (is_array(arr)) {
        writ::ArrayView a(arr, nullptr);
        for (uint64_t i = 0; i < a.size(); ++i) out.push_back(a.get(i));
    }
    return out;
}
} // namespace

AnyVal Lowering::bind_pattern(AnyVal b, TinyMapView at,
                              std::vector<std::pair<AnyVal, std::string>>& assigns) {
    TinyMapView bn = map_of(b);
    const int32_t bc = code_of(bn);
    if (bc == la::PAT_TUPLE.code) {                         // nested `(b, c)`
        std::vector<AnyVal> subs;
        for (AnyVal s : list_items(bn.has_key(la::NAMES) ? bn.get(la::NAMES.code) : bn.get(la::ITEMS.code)))
            subs.push_back(bind_pattern(s, at, assigns));
        // The pattern grammar's shape: a tuple's items as a bare array, and a
        // non-binding element (a nested tuple) wrapped in a single-alt PAT_OR —
        // the form the tuple door expects of every nested sub-pattern.
        for (auto& sub : subs)
            if (code_of(map_of(sub)) == la::PAT_TUPLE.code)
                sub = node(la::PAT_OR.code, at, Origin::Destructure, {{la::ITEMS.code, array({sub})}});
        return node(la::PAT_TUPLE.code, at, Origin::Destructure, {{la::ITEMS.code, array(subs)}});
    }
    if (bc == la::PAT_WILD.code) {
        std::string_view nm = name_of(bn);
        if (nm.empty() || nm == "_") return b;               // `_` as written (the doors read its NAME)
        std::string t = "__da" + std::to_string(fresh_++);
        assigns.push_back({bn.get(la::NAME.code), t});
        return node(la::PAT_WILD.code, at, Origin::Destructure, {{la::NAME.code, str(t)}});
    }
    return b;                                               // `..`, `()` — as written
}

AnyVal Lowering::destructure(TinyMapView n) {
    int32_t op = 0;
    if (n.has_key(la::OP)) { AnyVal ov = n.get(la::OP.code); if (ov.is_value()) op = ov.as_value<int32_t>(); }
    std::vector<std::pair<AnyVal, std::string>> assigns;
    AnyVal pat;
    if (op == 2) {                                          // S { f, g: b } = e
        std::vector<AnyVal> fields;
        for (AnyVal f : list_items(n.get(la::FIELDS.code))) {
            TinyMapView fn = map_of(f);
            if (code_of(fn) != la::PAT_FIELD.code) { fields.push_back(f); continue; }
            AnyVal sub = fn.has_key(la::VALUE) ? fn.get(la::VALUE.code)
                       : node(la::PAT_WILD.code, fn, Origin::Destructure, {{la::NAME.code, fn.get(la::NAME.code)}});
            fields.push_back(node(la::PAT_FIELD.code, fn, Origin::Destructure,
                                  {{la::NAME.code, fn.get(la::NAME.code)},
                                   {la::VALUE.code, bind_pattern(sub, fn, assigns)}}));
        }
        pat = node(la::PAT_STRUCT.code, n, Origin::Destructure,
                   {{la::NAME.code, n.get(la::NAME.code)}, {la::ITEMS.code, list_map(fields)}});
    } else {                                                // (…) = e / […] = e
        std::vector<AnyVal> items = list_items(n.get(la::NAMES.code));
        // `((a, b)) = e` is `(a, b) = e`: one nested place-tuple is parentheses
        // (the tuple form takes no trailing comma, so it is never a 1-tuple).
        while (op == 0 && items.size() == 1 && code_of(map_of(items[0])) == la::PAT_TUPLE.code) {
            TinyMapView inner = map_of(items[0]);
            items = list_items(inner.has_key(la::NAMES) ? inner.get(la::NAMES.code) : inner.get(la::ITEMS.code));
        }
        // `(x) = e` IS `x = e`: the tuple form takes no trailing comma, so one
        // place is a parenthesized place, never a 1-tuple.
        if (op == 0 && items.size() == 1 && code_of(map_of(items[0])) == la::PAT_WILD.code) {
            std::string_view nm = name_of(map_of(items[0]));
            if (!nm.empty() && nm != "_")
                return node(la::ASSIGN.code, n, Origin::Destructure,
                            {{la::NAME.code, map_of(items[0]).get(la::NAME.code)},
                             {la::VALUE.code, n.get(la::VALUE.code)}});
        }
        std::vector<AnyVal> subs;
        for (AnyVal b : items) {
            AnyVal sub = bind_pattern(b, n, assigns);
            if (code_of(map_of(sub)) == la::PAT_TUPLE.code)     // see bind_pattern
                sub = node(la::PAT_OR.code, n, Origin::Destructure, {{la::ITEMS.code, array({sub})}});
            subs.push_back(sub);
        }
        // A tuple pattern carries its items as the bare array, a slice
        // pattern as the list rule's `{ITEMS}` map (the grammar's shapes).
        pat = op == 1 ? node(la::PAT_SLICE.code, n, Origin::Destructure, {{la::ITEMS.code, list_map(subs)}})
                      : node(la::PAT_TUPLE.code, n, Origin::Destructure, {{la::ITEMS.code, array(subs)}});
    }
    std::vector<AnyVal> stmts;
    stmts.push_back(node(la::LET_PAT.code, n, Origin::Destructure,
                         {{la::PAT.code, pat}, {la::VALUE.code, n.get(la::VALUE.code)}}));
    for (auto& [place, t] : assigns)
        stmts.push_back(node(la::ASSIGN.code, n, Origin::Destructure,
                             {{la::NAME.code, place},
                              {la::VALUE.code, node(la::VAR_REF.code, n, Origin::Destructure,
                                                    {{la::NAME.code, str(t)}})}}));
    return node(la::BLOCK_STMT.code, n, Origin::Destructure, {{la::BODY.code, block(stmts, n, Origin::Destructure)}});
}

// ── built-in macros ─────────────────────────────────────────────────────────

namespace {
// Text the expansion puts INSIDE a format string literal: `"` and `\`
// escaped, `{`/`}` doubled so the format parser reads them as literal braces.
std::string fmt_lit_escape(std::string_view t) {
    std::string out;
    for (char ch : t) {
        if (ch == '"' || ch == '\\') out.push_back('\\');
        else if (ch == '{') { out += "{{"; continue; }
        else if (ch == '}') { out += "}}"; continue; }
        out.push_back(ch);
    }
    return out;
}
bool is_format_family(std::string_view n) {
    return n == "format" || n == "print" || n == "println" || n == "eprint" ||
           n == "eprintln" || n == "panic" || n == "format_args_str";
}
std::string_view unquote(std::string_view lit) {
    if (lit.size() >= 2 && lit.front() == '"' && lit.back() == '"')
        lit = lit.substr(1, lit.size() - 2);
    return lit;
}
} // namespace

AnyVal Lowering::refuse(TinyMapView call, std::string msg) {
    diags_.push_back({line_of(call), std::move(msg)});
    return recoded(call, la::FN_MACRO_CALL.code, Origin::Macro);
}

TinyMapView Lowering::parse_args(TinyMapView call, ArgsEntry entry, bool& ok) {
    ok = false;
    std::string_view raw = text_of(call, la::RAW_TEXT.code);
    if (entry == ArgsEntry::Args && raw.find_first_not_of(" \t\r\n") == std::string_view::npos) {
        ok = true;   // `name!()`
        return {};
    }
    auto text = std::make_shared<std::string>(raw);
    logos::compiler::LogosParser parser(*text);
    AnyVal lv = call.get(la::RAW_LINE.code);
    uint32_t line = !lv.is_null() && lv.is_value() ? lv.as_value<uint32_t>() : line_of(call);
    if (line != 0) parser.set_first_line(line);
    AnyVal ov = call.get(la::RAW_OFF.code);
    if (!ov.is_null() && ov.is_value()) parser.set_first_offset(ov.as_value<uint32_t>());
    auto doc = entry == ArgsEntry::Args ? parser.parse_macro_args() : parser.parse_matches_args();
    if (doc.is_null() || !parser.at_eof()) return {};
    TinyMapView root = doc.root_object().as_tiny_map();
    if (root.is_null()) return {};
    AnyVal rv; rv.set_ref(root.ptr());
    arg_texts_.push_back(std::move(text));
    arg_docs_.push_back(std::move(doc));
    ok = true;
    return map_of(lower(rv, Ctx::Expr));   // the arguments are code in the caller's body
}

AnyVal Lowering::expand_macro(AnyVal v) {
    TinyMapView n = map_of(v);
    const std::string callee(text_of(n, la::CALLEE.code));
    const bool write_family = callee == "write" || callee == "writeln";
    const bool marker = callee == "unreachable" || callee == "todo" || callee == "unimplemented";
    if (!is_format_family(callee) && !write_family && !marker &&
        callee != "matches" && callee != "dbg")
        return v;

    if (callee == "matches") {
        bool ok = false;
        TinyMapView m = parse_args(n, ArgsEntry::Matches, ok);
        if (!ok || m.is_null()) return refuse(n, "matches!: expected `matches!(expr, pattern [if guard])`");
        auto lit_bool = [&](bool b) {
            return block({node(la::TAIL_EXPR.code, n, Origin::Macro,
                               {{la::VALUE.code, node(la::LIT_BOOL.code, n, Origin::Macro,
                                                      {{la::VALUE.code, AnyVal::from_value(b)}})}})},
                         n, Origin::Macro);
        };
        return match_of(m.get(la::VALUE.code), m.get(la::PAT.code), m.get(la::GUARD.code),
                        lit_bool(true), lit_bool(false), n, Origin::Macro);
    }

    bool ok = false;
    TinyMapView root = parse_args(n, ArgsEntry::Args, ok);
    std::vector<AnyVal> args;
    if (ok && !root.is_null() && root.has_key(la::ITEMS)) {
        writ::ArrayView arr(root.get(la::ITEMS.code), nullptr);
        for (uint64_t i = 0; i < arr.size(); ++i) args.push_back(arr.get(i));
    }

    if (marker) {
        // `unreachable!()` panics with the fixed message; `unreachable!("fmt",
        // args…)` with "<message>: <formatted>" (rustc).
        if (!ok) return refuse(n, std::format("{}!: arguments do not parse as an expression list", callee));
        const std::string prefix =
            callee == "unreachable" ? "internal error: entered unreachable code"
          : callee == "todo"        ? "not yet implemented"
          :                           "not implemented";
        std::string fmt = fmt_lit_escape(prefix);
        if (!args.empty()) {
            TinyMapView f0 = map_of(args[0]);
            if (code_of(f0) != la::LIT_STR.code || !f0.has_key(la::VALUE))
                return refuse(n, std::format("{}!: the first argument must be a format string literal", callee));
            fmt += ": ";
            fmt += unquote(text_of(f0, la::VALUE.code));
        } else {
            args.push_back(AnyVal{});   // the format string's slot
        }
        AnyVal blk = format_expansion(n, "panic", fmt, args, 0, false);
        return blk.is_null() ? recoded(n, la::FN_MACRO_CALL.code, Origin::Macro) : blk;
    }

    if (callee == "dbg") {
        // `{ let t = e; eprintln!("[file:line] <e as written> = {:?}", t); t }`
        // — the value passes through (the print borrows it); `dbg!()` prints
        // the marker only.
        if (!ok || args.size() > 1) return refuse(n, "dbg!: expected `dbg!()` or `dbg!(expr)`");
        std::string where = std::format("[{}:{}]", fmt_lit_escape(file_), line_of(n));
        if (args.empty()) {
            AnyVal blk = format_expansion(n, "eprintln", where, {AnyVal{}}, 0, false);
            return blk.is_null() ? recoded(n, la::FN_MACRO_CALL.code, Origin::Macro) : blk;
        }
        std::string_view raw = text_of(n, la::RAW_TEXT.code);
        const size_t b = raw.find_first_not_of(" \t\r\n"), e = raw.find_last_not_of(" \t\r\n");
        raw = b == std::string_view::npos ? std::string_view{} : raw.substr(b, e - b + 1);
        const std::string t = std::format("__dbg_{}", fresh_++);
        auto var = [&] { return node(la::VAR_REF.code, n, Origin::Macro, {{la::NAME.code, str(t)}}); };
        AnyVal print = format_expansion(n, "eprintln",
                                        std::format("{} {} = {{:?}}", where, fmt_lit_escape(raw)),
                                        {AnyVal{}, var()}, 0, false);
        if (print.is_null()) return recoded(n, la::FN_MACRO_CALL.code, Origin::Macro);
        return block({node(la::LET.code, n, Origin::Macro, {{la::NAME.code, str(t)}, {la::VALUE.code, args[0]}}),
                      node(la::EXPR_STMT.code, n, Origin::Macro, {{la::VALUE.code, print}}),
                      node(la::TAIL_EXPR.code, n, Origin::Macro, {{la::VALUE.code, var()}})},
                     n, Origin::Macro);
    }

    // The format family: expanded when the format string is a literal; any
    // other first argument is left to the macro call's own resolution.
    const size_t fmt_pos = write_family ? 1 : 0;
    if (!ok || args.size() <= fmt_pos) return v;
    TinyMapView f = map_of(args[fmt_pos]);
    if (code_of(f) != la::LIT_STR.code || !f.has_key(la::VALUE)) return v;
    AnyVal blk = format_expansion(n, callee, unquote(text_of(f, la::VALUE.code)), args, fmt_pos,
                                  write_family);
    if (blk.is_null()) return recoded(n, la::FN_MACRO_CALL.code, Origin::Macro);
    // The family is declared in logos.std.fmt: sema asks that the name is in
    // scope at the expansion (the block's CALLEE).
    TinyMapView b = map_of(blk);
    return node(la::BLOCK.code, n, Origin::Macro,
                {{la::ITEMS.code, b.get(la::ITEMS.code)}, {la::CALLEE.code, str(callee)}});
}

// rustc's format_args!: the arguments are evaluated ONCE, left to right, each
// borrowed (`&(arg)`), before any formatting; `{0}{0}` formats one value
// twice; a write!/writeln! sink is evaluated first. `body` is the literal's
// contents as written (escapes intact), `args[fmt_pos]` the literal itself.
// Null after a diagnostic (bad format string, arity mismatch).
AnyVal Lowering::format_expansion(TinyMapView at, std::string_view callee, std::string_view body,
                                  const std::vector<AnyVal>& args, size_t fmt_pos,
                                  bool write_family) {
    const uint32_t ln = line_of(at);
    bool refused = false;
    FormatParseResult fr;
    parse_format_string(body, fr, [&](std::string msg) {
        diags_.push_back({ln, std::format("{}!: {}", callee, msg)});
        refused = true;
    });
    if (!fr.ok) return AnyVal{};
    int32_t placeholders = 0;
    for (auto& sg : fr.segments) if (!sg.is_literal) ++placeholders;
    const int32_t provided = static_cast<int32_t>(args.size()) - static_cast<int32_t>(fmt_pos) - 1;
    const int32_t needed = std::max(fr.positional_count, fr.max_explicit_plus_one);
    if (fr.max_explicit_plus_one == 0 ? placeholders != provided : provided < needed) {
        diags_.push_back({ln, fr.max_explicit_plus_one == 0
            ? std::format("{}!: format string has {} placeholder{} but {} argument{} provided",
                          callee, placeholders, placeholders == 1 ? "" : "s",
                          provided, provided == 1 ? "" : "s")
            : std::format("{}!: format string references arg index up to {} but only {} argument{} provided",
                          callee, needed - 1, provided, provided == 1 ? "" : "s")});
        return AnyVal{};
    }
    (void)refused;   // a soft diagnostic keeps the best-effort expansion

    const Origin o = Origin::Macro;
    const std::string pfx = std::format("__fmt{}_", fresh_++);
    auto var = [&](const std::string& nm) { return node(la::VAR_REF.code, at, o, {{la::NAME.code, str(nm)}}); };
    auto lit_int = [&](std::string lit) { return node(la::LIT_INT.code, at, o, {{la::VALUE.code, str(lit)}}); };
    auto lit_bool = [&](bool b) { return node(la::LIT_BOOL.code, at, o, {{la::VALUE.code, AnyVal::from_value(b)}}); };
    auto lit_str = [&](std::string_view t) {
        return node(la::LIT_STR.code, at, o, {{la::VALUE.code, str(std::format("\"{}\"", t))}});
    };
    auto mcall = [&](AnyVal recv, std::string_view m, std::vector<AnyVal> a) {
        return node(la::METHOD_CALL.code, at, o,
                    {{la::RECEIVER.code, recv}, {la::NAME.code, str(m)}, {la::ARGS.code, array(a)}});
    };
    auto call = [&](std::string_view fn, std::vector<AnyVal> a) {
        return node(la::CALL.code, at, o, {{la::CALLEE.code, str(fn)}, {la::ARGS.code, array(a)}});
    };
    auto let = [&](const std::string& nm, AnyVal ty, AnyVal val, bool mut_) {
        return node(la::LET.code, at, o, {{la::NAME.code, str(nm)}, {la::TYPE.code, ty}, {la::VALUE.code, val},
                                          {la::IS_MUT.code, mut_ ? AnyVal::from_value(true) : AnyVal{}}});
    };
    auto type = [&](std::string_view nm) { return node(la::TYPE_REF.code, at, o, {{la::NAME.code, str(nm)}}); };
    auto mutref = [&](AnyVal x) { return node(la::ADDR_OF_MUT.code, at, o, {{la::VALUE.code, x}}); };
    auto stmt = [&](AnyVal x) { return node(la::EXPR_STMT.code, at, o, {{la::VALUE.code, x}}); };
    // A STATIC_CALL's ARGS is the call_arg_list map ({ITEMS}).
    auto static_call = [&](std::string_view ty, std::string_view m, std::vector<AnyVal> a) {
        return node(la::STATIC_CALL.code, at, o,
                    {{la::RECEIVER.code, str(ty)}, {la::NAME.code, str(m)}, {la::ARGS.code, list_map(a)}});
    };
    const std::string f_n = pfx + "f", buf_n = pfx + "buf";
    std::vector<AnyVal> stmts;
    if (write_family)
        stmts.push_back(let(f_n, type("Formatter"), mcall(args[0], "as_formatter", {}), true));
    else
        stmts.push_back(let(buf_n, type("String"), static_call("String", "new", {}), true));
    // The value arguments, each borrowed once, in order; the borrow takes the
    // ARGUMENT's position, so a diagnostic on it points into the argument.
    std::vector<std::string> names;
    for (size_t vi = fmt_pos + 1; vi < args.size(); ++vi) {
        std::string an = std::format("{}a{}", pfx, vi - fmt_pos - 1);
        TinyMapView av = map_of(args[vi]);
        stmts.push_back(let(an, AnyVal{},
                            node(la::UNARY.code, line_of(av) ? av : at, o,
                                 {{la::OP.code, str("&")}, {la::VALUE.code, args[vi]}}),
                            false));
        names.push_back(std::move(an));
    }
    // The Formatter over the buffer is made after the arguments, so an
    // argument cannot observe the buffer.
    if (!write_family)
        stmts.push_back(let(f_n, type("Formatter"), static_call("Formatter", "new", {mutref(var(buf_n))}), true));
    int32_t auto_idx = 0;
    for (auto& sg : fr.segments) {
        if (sg.is_literal) {
            if (sg.lit_text.empty()) continue;
            // Literals go through the Formatter too: two live mutable paths to
            // one String are what format_args! never makes (ADR 0028).
            stmts.push_back(let("_", AnyVal{}, mcall(var(f_n), "write_str", {lit_str(sg.lit_text)}), false));
            continue;
        }
        const int32_t idx = sg.arg_idx >= 0 ? sg.arg_idx : auto_idx++;
        if (idx < 0 || size_t(idx) >= names.size()) continue;
        // align: 0 = unknown → right, 1 = left, 2 = right, 3 = center.
        const int32_t align = sg.spec.align == FormatAlign::Left   ? 1
                            : sg.spec.align == FormatAlign::Right  ? 2
                            : sg.spec.align == FormatAlign::Center ? 3 : 0;
        int32_t fill = static_cast<unsigned char>(sg.spec.fill);
        if (sg.spec.zero && sg.spec.align == FormatAlign::None && sg.spec.fill == ' ') fill = '0';
        stmts.push_back(stmt(mcall(var(f_n), "set_spec", {
            lit_int(std::format("{}u8", fill)), lit_int(std::format("{}u8", align)),
            lit_bool(sg.spec.sign == FormatSign::Plus), lit_bool(sg.spec.alt), lit_bool(sg.spec.zero),
            lit_int(std::format("{}i64", sg.spec.width >= 0 ? sg.spec.width : -1)),
            lit_int(std::format("{}i64", sg.spec.precision >= 0 ? sg.spec.precision : -1))})));
        stmts.push_back(let("_", AnyVal{},
                            call(format_trait_dispatcher(sg.spec.trait_kind),
                                 {var(names[size_t(idx)]), mutref(var(f_n))}),
                            false));
    }
    auto buf_str = [&] { return mcall(var(buf_n), "as_str", {}); };
    AnyVal tail;
    if (callee == "format" || callee == "format_args_str") tail = var(buf_n);
    else if (callee == "println")  tail = call("__fmt_println",  {buf_str()});
    else if (callee == "print")    tail = call("__fmt_print",    {buf_str()});
    else if (callee == "eprintln") tail = call("__fmt_eprintln", {buf_str()});
    else if (callee == "eprint")   tail = call("__fmt_eprint",   {buf_str()});
    else if (callee == "panic")    tail = call("__fmt_panic",    {buf_str()});
    else {
        // write!/writeln!: streamed into the sink through the Formatter;
        // writeln! adds the newline. The value is Ok(()) — per-placeholder
        // errors are discarded, as format! discards them.
        if (callee == "writeln")
            stmts.push_back(let("_", AnyVal{}, mcall(var(f_n), "write_str", {lit_str("\\n")}), false));
        tail = call("ok", {});
    }
    stmts.push_back(node(la::TAIL_EXPR.code, at, o, {{la::VALUE.code, tail}}));
    return block(stmts, at, o);
}

// ── loop exits ──────────────────────────────────────────────────────────────

AnyVal Lowering::resolve_exit(AnyVal v) {
    TinyMapView n = map_of(v);
    const int32_t c = code_of(n);
    const bool is_break = c == la::BREAK.code || c == la::BREAK_EXPR.code;
    const char* kw = is_break ? "break" : "continue";
    const bool with_value = is_break && n.has_key(la::VALUE);
    const std::string label(text_of(n, la::LABEL.code));
    const size_t floor = barriers_.empty() ? 0 : barriers_.back().depth;
    const bool in_closure = !barriers_.empty() && barriers_.back().closure;
    // In a fragment, the loops around its root are sema's to know.
    const bool open_root = fragment_ && barriers_.empty();
    auto refuse_exit = [&](std::string msg) {
        diags_.push_back({line_of(n), std::move(msg)});
        return recoded(n, c, Origin::ExitRefused);
    };
    if (label.empty()) {
        if (loops_.size() > floor) {
            if (loops_.back().is_block)
                return refuse_exit(std::format("unlabeled `{}` inside of a labeled block (E0695)", kw));
            if (with_value) loops_.back().valued = true;
            return v;
        }
        if (open_root) return v;
        return refuse_exit(in_closure ? std::format("'{}' inside of a closure (E0267)", kw)
                                      : std::format("'{}' outside loop (E0268)", kw));
    }
    for (size_t i = loops_.size(); i-- > floor; ) {
        if (loops_[i].label != label) continue;
        if (!is_break && loops_[i].is_block)
            return refuse_exit(std::format("`continue {}` targets a labeled block, not a loop (E0696)", label));
        if (with_value) loops_[i].valued = true;
        return v;
    }
    for (size_t i = floor; i-- > 0; )
        if (loops_[i].label == label)
            return refuse_exit(std::format("'{} {}': the label is outside the enclosing closure (E0767)", kw, label));
    if (open_root) return v;
    return refuse_exit(std::format("'{} {}': label not in scope (E0426)", kw, label));
}

} // namespace logos::compiler::hir
