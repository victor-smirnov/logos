// ── ADR 0028 S4 (#423): BIR, a MIR-like linearisation of L-IR bodies ────────
//
// Included into borrow_check.cpp (like borrow_flow_summary.inc) so it can use
// TypeSets, is_move_type, needs_drop, FnIndex and the flow summaries without
// duplicating them.
//
// BIR exists to produce Polonius facts. It makes explicit what L-IR leaves
// implicit and what the counter-based checker reconstructs per door:
//   * every evaluation step is a statement with its own points, so the
//     reservation, the argument evaluation and the activation of a call are
//     three different points (#316 is a single L-IR statement);
//   * places carry projections (`*v`, `x.f`, `a[i]`, `t.0`), not names;
//   * every intermediate value is a temporary local with a type;
//   * scope ends are StorageDead statements, so a loan of a dying local is
//     invalidated where the local dies.
//
// A construct the lowering does not handle yet is NOT guessed: the body records
// the reason in `unsupported` and the shadow comparison skips the function.
// The census of those reasons is the coverage measure (LOGOS_DL_SHADOW=bc).

namespace bir {

// TEMPORARY (#438 pricing): how each callee is resolved. Reverted with the probe.
struct CalleeCensus {
    size_t call_name = 0, call_bare = 0, call_miss = 0;
    size_t m_symbol = 0, m_recv = 0, m_miss = 0;
    size_t ifr_needle = 0, ifr_selftype = 0;
    size_t rm_symbol = 0, rm_needle = 0, rm_agree_bare = 0, rm_agree_base = 0, rm_miss = 0;
};
inline CalleeCensus& callee_census() { static CalleeCensus c; return c; }

using LocalId = uint32_t;
using BlockId = uint32_t;
constexpr LocalId kNoLocal = ~0u;

enum class LocalKind : uint8_t { Return, Param, User, Temp };

struct Local {
    std::string name;
    LocalKind   kind   = LocalKind::Temp;
    TypeRef     type;
    // A reference the lowering introduced (an implicit autoref) whose `&T`
    // has no pooled type: the local is a reference to `synth_pointee`.
    TypeRef     synth_pointee;
    bool        synth_ref = false;
    bool        synth_mut = false;   // the synthesised reference is `&mut`: its pointee is invariant
    bool        is_mut = false;
    bool        static_addr = false;   // holds the address of a `static` item
    TypeRef     decl_type;             // Param / Return: the type as DECLARED (before mono)
    bool        annot_lifetime = false;   // a `let` whose WRITTEN annotation names a lifetime
    // A CLOSURE CAPTURE. It is a Param of the closure body, but its region is
    // the PARENT frame's free region, not one bound by the call. The two are
    // the whole of E0521, so they may not be one kind.
    bool        is_capture = false;
    uint32_t    slot   = ~0u;
    uint32_t    line   = 0;
};

struct Proj {
    // ADR 0028: Subslice is MIR's, and it exists because a slice pattern's REST
    // (`[a, rest @ ..]`) names a RANGE of elements, not one. `from_end` is
    // MIR's too: a suffix element (`[.., b]`) is at an offset counted from the
    // end, which is a DIFFERENT element from the same offset counted from the
    // start unless the length is known.
    enum class K : uint8_t { Deref, Field, Index, Subslice };
    Proj() = default;
    explicit Proj(K kind, std::string f = {}) : k(kind), field(std::move(f)) {}
    K           k = K::Deref;
    std::string field;              // Field: name, tuple index or `Variant#i`
    LocalId     index = kNoLocal;   // Index: the index operand's local, if any
    int64_t     const_index = -1;   // Index into an ARRAY by a literal: MIR's ConstantIndex
    bool        from_end = false;   // Index / Subslice: the offset counts from the END
    int64_t     sub_to = -1;        // Subslice: the END offset (from the end when from_end)
    TypeRef     ty;                 // type of the place AFTER this projection, when known
};

struct Place {
    LocalId           local = kNoLocal;
    std::vector<Proj> proj;
    bool valid() const { return local != kNoLocal; }
    bool has_deref() const {
        for (auto& p : proj) if (p.k == Proj::K::Deref) return true;
        return false;
    }
    Place with(Proj p) const { Place q = *this; q.proj.push_back(std::move(p)); return q; }
};

struct Operand {
    enum class K : uint8_t { Const, Copy, Move };
    K     k = K::Const;
    Place place;
};

struct Rvalue {
    enum class K : uint8_t {
        Use,        // ops[0]
        Ref,        // &place / &mut place
        RawAddr,    // address of a place as a raw pointer: no loan
        Aggregate,  // struct / tuple / array / enum / slice literal
        Op,         // pure operation over ops (binop, unary, cast, len, ...)
    };
    Rvalue() = default;
    explicit Rvalue(K kind) : k(kind) {}
    K                    k = K::Use;
    std::vector<Operand> ops;
    Place                place;           // Ref, RawAddr
    bool                 mut_ = false;    // Ref
    bool                 two_phase = false;
    std::string          what;            // for dumps
    std::vector<std::string> field_names; // Aggregate of a struct literal: ops[i] initialises field_names[i]
};

struct Stmt {
    enum class K : uint8_t { Assign, StorageDead, Drop, Read };
    Stmt() = default;
    explicit Stmt(K kind) : k(kind) {}
    K        k = K::Assign;
    Place    dest;      // Assign; Drop: the dropped place
    Rvalue   rv;        // Assign; Read: rv.ops are read for effect
    LocalId  local = kNoLocal;   // StorageDead
    uint32_t line = 0;
};

struct Term {
    enum class K : uint8_t { Goto, Branch, Call, Return, Unreachable };
    Term() = default;
    explicit Term(K kind) : k(kind) {}
    K                    k = K::Unreachable;
    std::vector<BlockId> targets;     // Goto: 1; Branch: n; Call: continuation
    Operand              discr;       // Branch
    std::string          callee;      // Call (for dumps)
    std::vector<Operand> args;        // Call
    Place                dest;        // Call
    const FlowSummary*   flow = nullptr;   // Call: argument -> result reach
    // From the callee's SIGNATURE (lifetime names and elision): bit i set when
    // the result may hold what argument i holds. Authoritative when present;
    // the body-derived `flow` is only a fallback for unresolved callees.
    bool                 has_sig = false;
    uint64_t             sig_to_result = 0;
    // Bit i set when argument i's DECLARED type can be WRITTEN THROUGH (a
    // `&mut`, or a value holding one). Read only for an INDIRECT call, where
    // there is no body to summarise: see the out-param fallback.
    uint64_t             sig_mut_params = 0;
    // The resolved callee, whose DECLARED signature (decl_ret_type /
    // decl_type) says which argument positions the result borrows from.
    lir_view::FunctionView callee_fn;
    TypeRef              callee_fty;   // indirect call: the fn-pointer's TYPE is its contract
    int8_t               closure_recv = -1;  // ClosureCall: leading origins of args[0]
                                             // that are the AUTOREF, not the captures
    uint32_t             line = 0;
};

struct Block {
    std::vector<Stmt> stmts;
    Term              term;
    bool              terminated = false;
};

struct Body {
    std::string              name;
    std::vector<Local>       locals;
    std::vector<Block>       blocks;
    uint32_t                 n_params = 0;   // locals 1..n_params are params
    bool                     is_closure = false;
    uint32_t                 n_captures = 0;   // a closure: locals 1..n_captures are its captures
    std::set<std::string>    lifetime_params;   // the function's declared `'a`s
    std::vector<std::pair<std::string, std::string>> outlives;   // declared `'long: 'short`
    std::vector<std::string> unsupported;
    // Two-phase loans: (block, stmt) of the Ref and (block) of the Call that
    // activates it, recorded while lowering the receiver.
    struct TwoPhase { BlockId ref_block; uint32_t ref_stmt; BlockId act_block; };
    std::vector<TwoPhase>    two_phase;
};

} // namespace bir

// ── Loan-carrying types ──────────────────────────────────────────────────────
//
// A type whose values may hold a loan: a reference, a struct that holds a
// reference or is `#[borrow_carrying]` (the loan channel's closure, which
// includes arena handles), an enum with such a payload, or an aggregate of
// them. A struct or enum like that with no declared lifetime parameter still
// needs one origin, or the loan it carries is lost at the first copy.
static bool bir_type_carries_loans(TypeRef t, const lir::LProgram& prog, const TypeSets& ts,
                                   int depth = 0);

// A struct's fields, read from its definition (the instance, else the base).
static std::optional<std::vector<TypeRef>> bir_struct_fields(TypeRef t, const lir::LProgram& prog,
                                                             const TypeSets& ts) {
    std::string want = concrete_struct_name(t);
    auto look = [&](const auto& map) -> std::optional<std::vector<TypeRef>> {
        auto it = map.find(want);
        if (it == map.end()) return std::nullopt;
        std::vector<TypeRef> out;
        for (auto& f : it->second.fields()) out.push_back(f.type(prog.type_pool.impl()));
        return out;
    };
    if (auto f = look(ts.struct_by_name)) return f;
    return look(ts.spec_by_name);
}

// Whether dropping a value of type `t` may READ a borrow it holds — rustc's
// dropck. A destructor observes what it can name through `self`; `Vec<B>` and
// `Box<&T>` reach their elements too, but their drops are the `#[may_dangle]`
// kind, and holding those loans to the drop would refuse every
// `v.push(c.mk()); <last use of v>; c.bump()`. Logos has no `may_dangle`
// spelling yet, so the stand-in is the counter-based checker's
// (BorrowChecker::drop_can_observe_borrow): DECLARED FIELDS only, never type
// arguments. The two must answer alike until the attribute exists.
static bool bir_drop_observes_borrow(TypeRef t, const lir::LProgram& prog, const TypeSets& ts,
                                     int depth = 0) {
    if (!t || depth > 4 || t.kind() != LogosType::Kind::Struct) return false;
    if (!needs_drop(t, prog, ts)) return false;
    std::string want = concrete_struct_name(t);
    auto reaches_ref = [&](lir_view::StructView sd) -> bool {
        if (!sd) return false;
        for (auto& f : sd.fields()) {
            TypeRef ft = f.type(prog.type_pool.impl());
            if (!ft) continue;
            if (ft.kind() == LogosType::Kind::Ref || ft.kind() == LogosType::Kind::MutRef) return true;
            if (bir_drop_observes_borrow(ft, prog, ts, depth + 1)) return true;
        }
        return false;
    };
    if (auto it = ts.struct_by_name.find(want); it != ts.struct_by_name.end() && reaches_ref(it->second)) return true;
    if (auto it = ts.spec_by_name.find(want); it != ts.spec_by_name.end() && reaches_ref(it->second)) return true;
    return false;
}

// The same, with the field names (declaration order).
static std::optional<std::vector<std::pair<std::string, TypeRef>>>
bir_struct_named_fields(TypeRef t, const lir::LProgram& prog, const TypeSets& ts) {
    std::string want = concrete_struct_name(t);
    auto look = [&](const auto& map) -> std::optional<std::vector<std::pair<std::string, TypeRef>>> {
        auto it = map.find(want);
        if (it == map.end()) return std::nullopt;
        std::vector<std::pair<std::string, TypeRef>> out;
        for (auto& f : it->second.fields())
            out.emplace_back(std::string(f.name()), f.type(prog.type_pool.impl()));
        return out;
    };
    if (auto f = look(ts.struct_by_name)) return f;
    return look(ts.spec_by_name);
}

// An enum's payload slots as (`Variant#i`, type), in declaration order: the
// names BIR's Field projections use for a variant's payload.
static std::optional<std::vector<std::pair<std::string, TypeRef>>>
bir_enum_named_payloads(TypeRef t, const lir::LProgram& prog, const TypeSets& ts) {
    auto it = ts.enum_by_name.end();
    if (!t.type_args().empty()) it = ts.enum_by_name.find(Mono::enum_instance_name(t));
    if (it == ts.enum_by_name.end()) it = ts.enum_by_name.find(std::string(t.enum_name()));
    if (it == ts.enum_by_name.end()) return std::nullopt;
    std::vector<std::pair<std::string, TypeRef>> out;
    it->second.each_variant([&](lir_view::EnumVariantView v) {
        uint32_t i = 0;
        v.each_payload_type(prog.type_pool.impl(), [&](TypeRef pt) {
            out.emplace_back(std::string(v.name()) + "#" + std::to_string(i++), pt);
        });
    });
    return out;
}

static bool bir_type_carries_loans(TypeRef t, const lir::LProgram& prog, const TypeSets& ts,
                                   int depth) {
    if (!t || depth > 16) return false;
    switch (t.kind()) {
        case LogosType::Kind::Ref: case LogosType::Kind::MutRef: case LogosType::Kind::Closure:
            return true;
        case LogosType::Kind::Ptr: {
            // A raw pointer carries a loan only when it points AT a reference
            // (`*const &'a T`). `*mut dyn Trait` is `*mut (dyn Trait + 'static)`
            // in Rust: no loan.
            TypeRef pt = t.pointee();
            if (!pt) return false;
            auto pk = pt.kind();
            if (pk == LogosType::Kind::Ref || pk == LogosType::Kind::MutRef) return true;
            if (pk == LogosType::Kind::TraitObject || pk == LogosType::Kind::UnsizedDyn ||
                pk == LogosType::Kind::Slice || pk == LogosType::Kind::UnsizedSlice ||
                pk == LogosType::Kind::DstRef)
                return false;
            return bir_type_carries_loans(pt, prog, ts, depth + 1);
        }
        case LogosType::Kind::Slice:
            if (t.raw_fat()) return false;
            return !t.owning_slice() || bir_type_carries_loans(t.elem(), prog, ts, depth + 1);
        case LogosType::Kind::TraitObject: return !t.raw_fat();
        case LogosType::Kind::DstRef: return !t.owning_dst() && !t.raw_fat();
        case LogosType::Kind::Array: return bir_type_carries_loans(t.elem(), prog, ts, depth + 1);
        case LogosType::Kind::Tuple:
            for (auto& e : t.tuple_elems()) if (bir_type_carries_loans(e, prog, ts, depth + 1)) return true;
            return false;
        case LogosType::Kind::Struct: {
            if (!t.lifetime_args().empty()) return true;
            if (bc_loan_carrying_type(ts, t)) return true;   // #[borrow_carrying]
            for (auto& a : t.type_args()) if (bir_type_carries_loans(a, prog, ts, depth + 1)) return true;
            if (auto fields = bir_struct_fields(t, prog, ts)) {
                for (auto& ft : *fields) if (bir_type_carries_loans(ft, prog, ts, depth + 1)) return true;
                return false;
            }
            return bc_holds_any_ref_type(ts, t);   // no definition: the name-keyed set
        }
        case LogosType::Kind::Enum: {
            if (!t.lifetime_args().empty()) return true;
            // `#[borrow_carrying]` IS READ ON AN ENUM TOO. The arm above reads
            // it and this one did not, so the attribute worked on a struct and
            // was invisible on an enum — and the stdlib's own `WAny` is an
            // enum. Its payloads are `*const u8` and `u64`, neither of which
            // carries a loan by structure, which is exactly why the attribute
            // is written on it: the Ref variant points INTO a Writ arena. With
            // the attribute unread, every `WAny` carrying a borrow of a local
            // arena was loan-free to the new checker, and four escapes rode out
            // of their functions unrefused.
            if (bc_loan_carrying_type(ts, t)) return true;   // #[borrow_carrying]
            for (auto& a : t.type_args()) if (bir_type_carries_loans(a, prog, ts, depth + 1)) return true;
            auto eit = ts.enum_by_name.end();
            if (!t.type_args().empty()) eit = ts.enum_by_name.find(Mono::enum_instance_name(t));
            if (eit == ts.enum_by_name.end()) eit = ts.enum_by_name.find(std::string(t.enum_name()));
            if (eit == ts.enum_by_name.end()) return false;
            bool carries = false;
            eit->second.each_variant([&](lir_view::EnumVariantView v) {
                v.each_payload_type(prog.type_pool.impl(), [&](TypeRef pt) {
                    if (!carries && bir_type_carries_loans(pt, prog, ts, depth + 1)) carries = true;
                });
            });
            return carries;
        }
        default:
            return false;
    }
}

// ── Lowering ─────────────────────────────────────────────────────────────────

class BirBuilder {
public:
    BirBuilder(const lir::LProgram& prog, const TypeSets& ts, const FnIndex& idx,
               const FlowSummaryMap* flows)
        : prog_(prog), ts_(ts), idx_(idx), flows_(flows),
          pool_(prog.type_pool.impl()) {}

    bir::Body build(lir_view::FunctionView fn) {
        body_ = {};
        body_.name = std::string(fn.name());
        // #434: a generic TEMPLATE's `T` moves unless the function bounds it
        // by the `Copy` lang item, as rustc checks generic MIR. Same walk and
        // same identity test as the counter-based checker's copy_tvs_.
        copy_tvs_.clear();
        aliases_.clear();
        closures_.clear();
        for (auto lp : fn.lifetime_params()) {
            std::string n(lp);
            if (!n.empty() && n[0] != '\'') n = "'" + n;
            body_.lifetime_params.insert(std::move(n));
        }
        fn.each_type_param([&](lir_view::FnTParamView tp) {
            tp.each_bound([&](lir_view::FnTraitBoundView b) {
                if (lir_is_copy_lang_item(b.identity_trait()))
                    copy_tvs_.insert(std::string(tp.name()));
            });
        });
        for (auto& [lo, sh] : fn.lifetime_outlives()) {
            auto q = [](std::string_view n) { std::string r(n); if (!r.empty() && r[0] != '\'') r = "'" + r; return r; };
            body_.outlives.emplace_back(q(lo), q(sh));
        }
        new_local("_ret", bir::LocalKind::Return, fn.ret_type(pool_), true, ~0u, 0);
        body_.locals[0].decl_type = fn.decl_ret_type(pool_);
        cur_ = new_block();
        push_scope();
        fn.each_param([&](lir_view::LParamView p) {
            // A parameter is immutable: sema rewrites `mut x: T` into a
            // hidden parameter plus `let mut x`, so a Param local that is
            // written or `&mut`-borrowed directly was never declared `mut`.
            bir::LocalId l = new_local(std::string(p.name()), bir::LocalKind::Param,
                                       p.type(pool_), false, p.slot(), 0);
            body_.locals[l].decl_type = p.decl_type(pool_) ? p.decl_type(pool_) : p.type(pool_);
            declare(l);
            ++body_.n_params;
        });
        if (auto b = fn.body()) lower_block_stmts(b);
        if (!body_.blocks[cur_].terminated) {
            // Falling off the end: every scope dies, then return.
            exit_scopes(0, 0);
            terminate(bir::Term(bir::Term::K::Return));
        }
        return std::move(body_);
    }

    // ADR 0029 S5b — A CLOSURE BODY IS ITS OWN `bir::Body`, as MIR gives a
    // closure one. Everything it needs is already in the L-IR — CL_BLOCK,
    // CL_PARAM_*, CL_CAPTURE_* and CL_RET_TYPE — so the body is LOWERED rather
    // than approximated, and a violation visible ONLY inside the closure (a
    // returned reference to a body local, two conflicting borrows of one
    // capture) is found. 29 of the corpus's 61 remaining misses are this shape.
    //
    // ⚠ A CAPTURE IS A PARAMETER HERE, AND ITS LOAN IS NOT RE-ISSUED. The loan a
    // by-reference capture takes is issued in the ENCLOSING body at the
    // construction point (the `ClosureBox` arm), which is where rustc issues it
    // too; taking it again here would count one borrow twice and refuse programs
    // that only ever borrow once. What this body checks is what the body DOES
    // with what it was given.
    bir::Body build_closure(lir_view::ExprRef e) {
        lir_view::EClosureBoxView v{e};
        body_ = {};
        body_.name = "closure " + std::string(v.closure_id());
        body_.is_closure = true;
        copy_tvs_.clear();
        aliases_.clear();
        closures_.clear();
        new_local("_ret", bir::LocalKind::Return, v.ret_type(pool_), true, ~0u, 0);
        cur_ = new_block();
        push_scope();
        uint64_t ci = 0;
        v.each_capture(pool_, [&](std::string_view n, TypeRef t) {
            // CAPTURE_MODES: 0 ImmBorrow, 1 MutBorrow, 2 ByValue. Only the first
            // cannot be written through, so it is the only one that is not `mut`.
            // ⚠ THE CAPTURE IS `mut` HERE, AND THE MODE IS NOT RE-DERIVED.
            // Whether a capture may be mutated is decided OUTSIDE, at the
            // construction point, and the enclosing body acts on that decision:
            // `CAPTURE_MODES` is what it borrows by. Re-deriving it inside meant
            // answering the same question twice from less — and sema records
            // ImmBorrow for a capture the body only CALLS, so `|v| f(v)` with
            // `F: FnMut` was refused with "cannot borrow 'f' as mutable, as it
            // is not declared as mutable" although `mut f` was written. Four
            // legal fixtures. What this body checks is what it DOES with what it
            // was given; by what right it was given is the caller's question.
            bir::LocalId l = new_local(std::string(n), bir::LocalKind::Param, t,
                                       /*is_mut=*/true, ~0u, 0);
            body_.locals[l].is_capture = true;
            ++body_.n_captures;
            (void)ci;
            // ⚠ NOT `declare`d — ON PURPOSE. `declare` puts a local in a SCOPE,
            // and leaving the scope emits its StorageDead. A capture's storage
            // is the ENCLOSING frame's, not this body's: in MIR a by-reference
            // capture is reached as `(*_1.0)` and nothing about it dies when the
            // closure returns. Declaring it made a borrow of a capture look
            // like a borrow of a dying local, so every closure that RETURNS
            // something derived from what it captured read as letting borrowed
            // data escape — eighteen pass fixtures, all of them legal.
            ++body_.n_params;
            ++ci;
        });
        v.each_param(pool_, [&](std::string_view n, TypeRef t) {
            bir::LocalId l = new_local(std::string(n), bir::LocalKind::Param, t,
                                       false, ~0u, 0);
            declare(l);
            ++body_.n_params;
        });
        if (auto b = v.body()) lower_block_stmts(b);
        if (!body_.blocks[cur_].terminated) {
            exit_scopes(0, 0);
            terminate(bir::Term(bir::Term::K::Return));
        }
        return std::move(body_);
    }
    // The closure literals THIS body mentioned — a closure inside a closure is
    // found by asking the sub-body the same question.
    const std::vector<lir_view::ExprRef>& closures() const { return closures_; }

private:
    using ECode = lir_schema::expr::Code;
    using SCode = lir_schema::stmt::Code;
    using PCode = lir_schema::pat::Code;

    // ── construction helpers ──
    bir::LocalId new_local(std::string name, bir::LocalKind k, TypeRef t, bool is_mut,
                           uint32_t slot, uint32_t line) {
        bir::Local l;
        l.name = std::move(name); l.kind = k; l.type = t; l.is_mut = is_mut;
        l.slot = slot; l.line = line;
        body_.locals.push_back(std::move(l));
        return static_cast<bir::LocalId>(body_.locals.size() - 1);
    }
    bir::LocalId temp(TypeRef t) {
        bir::LocalId l = new_local("_t" + std::to_string(body_.locals.size()),
                                   bir::LocalKind::Temp, t, true, ~0u, line_);
        if (extend_place_temps_) declare_outer(l); else declare(l);
        return l;
    }
    // Building the PLACE under an extending `&`: `let f = &mk().n;` extends the
    // temporary `mk()` the place is rooted in, not only a temporary that is the
    // whole operand.
    bool extend_place_temps_ = false;
    bir::LocalId synth_ref_temp(TypeRef pointee, bool is_mut) {
        bir::LocalId l = temp({});
        body_.locals[l].synth_ref = true;
        body_.locals[l].synth_mut = is_mut;
        body_.locals[l].synth_pointee = pointee;
        return l;
    }
    bir::BlockId new_block() {
        body_.blocks.emplace_back();
        return static_cast<bir::BlockId>(body_.blocks.size() - 1);
    }
    void emit(bir::Stmt s) {
        s.line = line_;
        if (body_.blocks[cur_].terminated) {
            // Code after a diverging statement: give it an unreachable block
            // so it is still lowered (its loans cannot matter).
            cur_ = new_block();
        }
        body_.blocks[cur_].stmts.push_back(std::move(s));
    }
    void terminate(bir::Term t) {
        if (body_.blocks[cur_].terminated) cur_ = new_block();
        t.line = line_;
        body_.blocks[cur_].term = std::move(t);
        body_.blocks[cur_].terminated = true;
    }
    void goto_(bir::BlockId b) {
        bir::Term t(bir::Term::K::Goto);
        t.targets = {b};
        terminate(std::move(t));
    }
    // `[a; 2]` reads `a` TWICE inside ONE rvalue, and both reads land at the
    // SAME point — so the initialisation dataflow, which asks whether a path is
    // uninitialised ON ENTRY to a point, cannot see the second. MIR gives each
    // operand its own statement (`_3 = move _1; _4 = move _1;`) and the second
    // is then a use of a moved value; the counter-based checker said exactly
    // that, and this one compiled the program CLEAN — a double move of a value
    // with a `Drop` impl, so a double drop.
    //
    // Only the DUPLICATE is materialised: the first read keeps its place, so
    // every aggregate that mentions a local once is untouched.
    void split_repeated_moves(bir::Rvalue& rv) {
        std::set<bir::LocalId> seen;
        for (auto& op : rv.ops) {
            if (op.k != bir::Operand::K::Move || !op.place.valid() || !op.place.proj.empty())
                continue;
            if (seen.insert(op.place.local).second) continue;
            bir::LocalId t = temp(body_.locals[op.place.local].type);
            bir::Rvalue u(bir::Rvalue::K::Use);
            u.ops.push_back(op);
            assign(bir::Place{t, {}}, std::move(u));
            op.place = bir::Place{t, {}};
        }
    }

    void assign(bir::Place dest, bir::Rvalue rv) {
        // ⚠ THE SHAPE IS "AN RVALUE THAT MOVES ONE PLACE TWICE", not "an array
        // literal". Found on `[a; 2]`, then measured on `Pair { a: d, b: d }`,
        // which had the same hole — rustc answers E0382 for both. Placing the
        // split HERE covers every aggregate at once instead of one literal kind
        // at a time. It is a no-op unless a place is moved twice, and the
        // statement it emits has a single operand, so it cannot recurse.
        split_repeated_moves(rv);
        // `x = h(move x)` as one statement would move and initialise x at the
        // same point; route the value through a temporary so the move comes
        // first.
        // A READ THROUGH A REFERENCE IS ITS OWN STATEMENT, for the same reason
        // the self-move above is: otherwise the read and the write of the
        // destination land on ONE program point, and a write that invalidates
        // a loan the same point still uses is an error the program does not
        // have. MIR does exactly this — `x = x + *pick(&x)` lowers to
        // `_4 = (*_3); x = Add(x, move _4);` — and `acc = acc + *pick(&acc);`
        // was refused here while rustc 1.98.1 accepts it (MEASURED).
        //
        // ⚠ ONLY FOR A NON-TEMP DESTINATION, and only for an Op rvalue. A temp
        // destination cannot be the place a loan is held on, so materialising
        // there would add statements and facts for nothing. ⚠ AND THE TEMP
        // TAKES THE PLACE'S TYPE, not a null one: an untyped temp gets no
        // origins, the loan would not flow through it, and the materialisation
        // would turn an over-refusal into a silent hole.
        if (rv.k == bir::Rvalue::K::Op &&
            body_.locals[dest.local].kind != bir::LocalKind::Temp) {
            for (auto& op : rv.ops) {
                if (op.k == bir::Operand::K::Const || !op.place.valid()) continue;
                bool derefs = false;
                for (auto& pj : op.place.proj)
                    if (pj.k == bir::Proj::K::Deref) { derefs = true; break; }
                if (!derefs) continue;
                TypeRef pt = op.place.proj.empty() ? body_.locals[op.place.local].type
                                                   : op.place.proj.back().ty;
                bir::LocalId t = temp(pt);
                bir::Stmt sm(bir::Stmt::K::Assign);
                sm.dest = {t, {}};
                bir::Rvalue use(bir::Rvalue::K::Use);
                use.ops.push_back(op);
                sm.rv = std::move(use);
                emit(std::move(sm));
                op.k = bir::Operand::K::Copy;
                op.place = bir::Place{t, {}};
            }
        }
        bool self_move = false;
        for (auto& op : rv.ops)
            if (op.k == bir::Operand::K::Move && op.place.local == dest.local) self_move = true;
        if (self_move) {
            bir::LocalId t = temp(dest.proj.empty() ? body_.locals[dest.local].type : TypeRef{});
            bir::Stmt s1(bir::Stmt::K::Assign);
            s1.dest = {t, {}};
            s1.rv   = std::move(rv);
            emit(std::move(s1));
            bir::Rvalue mv(bir::Rvalue::K::Use);
            bir::Operand o;
            o.k = bir::Operand::K::Move;
            o.place = {t, {}};
            mv.ops.push_back(std::move(o));
            rv = std::move(mv);
        }
        bir::Stmt s(bir::Stmt::K::Assign);
        s.dest = std::move(dest);
        s.rv   = std::move(rv);
        emit(std::move(s));
    }
    void unsupported(std::string why) { body_.unsupported.push_back(std::move(why)); }

    // ── scopes ──
    struct Scope { std::vector<bir::LocalId> locals; };
    std::vector<Scope> scopes_;
    void push_scope() { scopes_.emplace_back(); }
    void declare(bir::LocalId l) { if (!scopes_.empty()) scopes_.back().locals.push_back(l); }
    // StorageDead for every local of scopes [depth, top), innermost first.
    void exit_scopes(size_t depth, int) {
        for (size_t i = scopes_.size(); i-- > depth;)
            for (auto it = scopes_[i].locals.rbegin(); it != scopes_[i].locals.rend(); ++it) {
                if (body_.locals[*it].kind == bir::LocalKind::Return) continue;
                // Rust drops a scope's locals in REVERSE DECLARATION ORDER, each
                // one dropped and then dead. Sema places its SDrop for a
                // droppable local wherever its own glue wants it (ahead of the
                // scope's StorageDeads), so `let h: H; let x = 7; h = H { r: &x
                // };` had `drop h` run while `x` was still alive. The drop is
                // put where Rust has it: `x` is dead by the time `h`'s
                // destructor reads `*self.r` (E0597, dropck).
                if (body_.locals[*it].kind != bir::LocalKind::Temp &&
                    bir_drop_observes_borrow(body_.locals[*it].type, prog_, ts_)) {
                    bir::Stmt d(bir::Stmt::K::Drop);
                    d.dest = {*it, {}};
                    emit(std::move(d));
                }
                bir::Stmt s(bir::Stmt::K::StorageDead);
                s.local = *it;
                emit(std::move(s));
            }
    }
    void pop_scope() {
        if (!body_.blocks[cur_].terminated) exit_scopes(scopes_.size() - 1, 0);
        scopes_.pop_back();
    }

    // ── name resolution ──
    // A binding is found by slot when sema assigned one, else by the
    // innermost declaration of that name.
    bir::LocalId find_local(std::string_view name, uint32_t slot) const {
        if (slot != ~0u)
            for (size_t i = body_.locals.size(); i-- > 0;)
                if (body_.locals[i].slot == slot && body_.locals[i].kind != bir::LocalKind::Temp)
                    return static_cast<bir::LocalId>(i);
        for (size_t s = scopes_.size(); s-- > 0;)
            for (auto it = scopes_[s].locals.rbegin(); it != scopes_[s].locals.rend(); ++it)
                if (body_.locals[*it].name == name) return *it;
        // ⚠ A CLOSURE'S CAPTURE IS A PARAM WITH NO SCOPE. It is deliberately not
        // `declare`d — its storage belongs to the enclosing frame, so it must
        // not receive a StorageDead — and the scope walk above therefore cannot
        // see it. Without this the body's every mention of a capture missed and
        // became a FRESH const temp (`_t3 = const; r = &mut _t3.a;`), so the
        // body was lowered and said nothing: two conflicting borrows of one
        // capture touched two different temps.
        for (size_t i = 0; i < body_.locals.size(); ++i)
            if (body_.locals[i].kind == bir::LocalKind::Param && body_.locals[i].name == name)
                return static_cast<bir::LocalId>(i);
        return bir::kNoLocal;
    }

    // ── types ──
    static bool is_pointer_kind(TypeRef t) {
        if (!t) return false;
        switch (t.kind()) {
            case LogosType::Kind::Ref: case LogosType::Kind::MutRef:
            case LogosType::Kind::Ptr: return true;
            case LogosType::Kind::DstRef: return true;
            default: return false;
        }
    }
    bool is_copy(TypeRef t) const {
        if (!t) return true;
        return !is_move_type(t, prog_, ts_, &copy_tvs_);
    }
    TypeRef field_type(TypeRef st, std::string_view field) const {
        if (!st) return {};
        if (st.kind() == LogosType::Kind::Tuple) {
            auto elems = st.tuple_elems();
            uint32_t i = 0;
            for (char c : field) { if (c < '0' || c > '9') return {}; i = i * 10 + uint32_t(c - '0'); }
            return i < elems.size() ? elems[i] : TypeRef{};
        }
        if (st.kind() != LogosType::Kind::Struct) return {};
        std::string want = concrete_struct_name(st);
        auto look = [&](const auto& map) -> TypeRef {
            auto it = map.find(want);
            if (it == map.end()) return {};
            for (auto& f : it->second.fields())
                if (f.name() == field) return f.type(pool_);
            return {};
        };
        if (TypeRef t = look(ts_.struct_by_name)) return t;
        return look(ts_.spec_by_name);
    }
    TypeRef local_type(bir::LocalId l) const { return body_.locals[l].type; }

    // ── places ──
    // The place an expression denotes, if it is a place expression. The type
    // of the place is `e.type()`. Auto-deref: a field or index read through a
    // pointer-typed receiver goes through `*`.
    std::optional<bir::Place> place_of(lir_view::ExprRef e) {
        auto p = place_of_(e);
        if (p && !p->proj.empty() && e) p->proj.back().ty = e.type(pool_);
        return p;
    }
    std::optional<bir::Place> place_of_(lir_view::ExprRef e) {
        if (!e) return std::nullopt;
        switch (e.kind()) {
            case ECode::VarRef: {
                lir_view::EVarRefView v{e};
                if (!aliases_.empty())
                    if (auto a = aliases_.find(std::string(v.name())); a != aliases_.end())
                        return a->second;
                bir::LocalId l = find_local(v.name(), v.var_slot());
                if (l == bir::kNoLocal) return std::nullopt;   // a global / const
                return bir::Place{l, {}};
            }
            case ECode::Deref: {
                auto inner = lir_view::EDerefView{e}.operand();
                bir::Place p = place_or_temp(inner);
                // Sema spells a static's address as the reserved VarRef
                // `__static_addr:<link symbol>` (not an identifier a program
                // can write). A static is never moved out of (E0507).
                if (inner && inner.kind() == ECode::VarRef && p.proj.empty() &&
                    lir_view::EVarRefView{inner}.name().starts_with("__static_addr:"))
                    body_.locals[p.local].static_addr = true;
                return p.with(bir::Proj(bir::Proj::K::Deref));
            }
            case ECode::FieldRead: {
                lir_view::EFieldReadView v{e};
                bir::Place p = place_or_temp(v.receiver());
                if (TypeRef rt = v.receiver().type(pool_); is_pointer_kind(rt)) {
                    bir::Proj d(bir::Proj::K::Deref); d.ty = rt.pointee();
                    p = p.with(std::move(d));
                }
                {   // ⚠ THE FIELD'S TYPE IS RECORDED. The reborrow walk gives up
                    // the moment a step has no type ("unknown: no further
                    // reborrow edges"), so a place like `(*t.0).a` lost its
                    // deref edge at the FIELD step and the loan the pointer
                    // carried never flowed into the reborrow.
                    bir::Proj pr(bir::Proj::K::Field, std::string(v.field()));
                    TypeRef rt2 = v.receiver().type(pool_);
                    if (rt2 && is_pointer_kind(rt2)) rt2 = rt2.pointee();
                    pr.ty = field_type(rt2, v.field());
                    return p.with(std::move(pr));
                }
            }
            case ECode::TupleIndex: {
                lir_view::ETupleIndexView v{e};
                bir::Place p = place_or_temp(v.receiver());
                if (TypeRef rt = v.receiver().type(pool_); is_pointer_kind(rt)) {
                    bir::Proj d(bir::Proj::K::Deref); d.ty = rt.pointee();
                    p = p.with(std::move(d));
                }
                {   // Same as the FieldRead arm above: record the element type.
                    bir::Proj pr(bir::Proj::K::Field, std::to_string(v.index()));
                    TypeRef rt2 = v.receiver().type(pool_);
                    if (rt2 && is_pointer_kind(rt2)) rt2 = rt2.pointee();
                    if (rt2 && rt2.kind() == LogosType::Kind::Tuple) {
                        auto el = rt2.tuple_elems();
                        if (v.index() < el.size()) pr.ty = el[v.index()];
                    }
                    return p.with(std::move(pr));
                }
            }
            case ECode::IndexRead: {
                lir_view::EIndexReadView v{e};
                TypeRef rt = v.receiver().type(pool_);
                bir::Place p = place_or_temp(v.receiver());
                if (is_pointer_kind(rt) || (rt && rt.kind() == LogosType::Kind::Slice)) {
                    bir::Proj d(bir::Proj::K::Deref);
                    d.ty = is_pointer_kind(rt) ? rt.pointee() : TypeRef{};
                    p = p.with(std::move(d));
                }
                // THE BOUNDS CHECK IS A BORROW OF THE BASE, AND IT SPANS THE
                // INDEX EXPRESSION. MIR takes a shallow FAKE borrow of the
                // indexed place for the length read, so
                // `arr[{ arr = [4,5,6]; 1 }]` has a live loan of `arr` when the
                // index expression assigns to it — upstream's E0510, which this
                // checker missed because nothing here borrowed the base at all.
                // A real shared loan, read once after the index is computed, is
                // the same shape the match-guard fake borrows above already use.
                // ⚠ ONLY WHEN THE INDEX CAN WRITE. A literal or a plain place
                // cannot, and issuing a loan for every `xs[i]` would add a fact
                // per index in the corpus for nothing.
                bir::LocalId fake = bir::kNoLocal;
                if (v.index() && index_may_write(v.index())) {
                    fake = synth_ref_temp({}, false);
                    assign({fake, {}}, ref_of(p, false, /*two_phase=*/false));
                }
                bir::Proj ix(bir::Proj::K::Index);
                ix.index = operand_local(v.index());
                if (fake != bir::kNoLocal) {
                    bir::Stmt rd(bir::Stmt::K::Read);
                    bir::Operand fo; fo.k = bir::Operand::K::Copy; fo.place = {fake, {}};
                    rd.rv.ops.push_back(std::move(fo));
                    emit(std::move(rd));
                }
                // `let [a, b] = xs;` reaches L-IR as `xs[0]`, `xs[1]`. Over an
                // array a literal index is its own move path, as MIR's
                // ConstantIndex is, so the elements move out one by one.
                if (rt && rt.kind() == LogosType::Kind::Array && v.index() &&
                    v.index().kind() == ECode::LitInt)
                    ix.const_index = lir_view::ELitIntView{v.index()}.value();
                return p.with(std::move(ix));
            }
            case ECode::SliceIndex: {
                lir_view::ESliceIndexView v{e};
                bir::Place p = place_or_temp(v.slice()).with(bir::Proj(bir::Proj::K::Deref));
                bir::Proj ix(bir::Proj::K::Index);
                ix.index = operand_local(v.index());
                return p.with(std::move(ix));
            }
            default:
                return std::nullopt;
        }
    }
    // Can this expression WRITE anything? A constant or a plain place cannot;
    // anything with a block, a call or an assignment in it can. Used to decide
    // whether an index expression needs the bounds-check fake borrow above.
    static bool index_may_write(lir_view::ExprRef e) {
        if (!e) return false;
        switch (e.kind()) {
            case ECode::LitInt: case ECode::LitBool: case ECode::VarRef:
            case ECode::FieldRead: case ECode::TupleIndex: case ECode::Cast:
                return false;
            default:
                return true;
        }
    }
    // A place for `e`: the place itself, or a temporary holding its value.
    bir::Place place_or_temp(lir_view::ExprRef e) {
        if (auto p = place_of(e)) return *p;
        bir::LocalId t = temp(e ? e.type(pool_) : TypeRef{});
        lower_into(bir::Place{t, {}}, e);
        return bir::Place{t, {}};
    }
    // The local an index operand is evaluated into (constants need none).
    bir::LocalId operand_local(lir_view::ExprRef e) {
        bir::Operand o = operand(e);
        if (o.k == bir::Operand::K::Const) return bir::kNoLocal;
        if (o.place.proj.empty()) return o.place.local;
        bir::LocalId t = temp(e.type(pool_));
        bir::Rvalue rv(bir::Rvalue::K::Use);
        rv.ops.push_back(std::move(o));
        assign({t, {}}, std::move(rv));
        return t;
    }

    // ── operands ──
    bir::Operand read_place(bir::Place p, TypeRef t) {
        bir::Operand o;
        o.k     = is_copy(t) ? bir::Operand::K::Copy : bir::Operand::K::Move;
        o.place = std::move(p);
        return o;
    }
    bir::Operand operand(lir_view::ExprRef e) {
        if (!e) return {};
        if (is_constant(e)) return {};
        if (auto p = place_of(e)) return read_place(*p, e.type(pool_));
        bir::LocalId t = temp(e.type(pool_));
        lower_into(bir::Place{t, {}}, e);
        return read_place({t, {}}, e.type(pool_));
    }
    // A call argument, evaluated into its own temporary BEFORE the call, as
    // MIR building does (`_5 = copy c.n; call f(move _4, move _5)`): the
    // argument's read happens at its own point, ahead of the call where the
    // two-phase reservations activate.
    bir::Operand call_operand(lir_view::ExprRef e) {
        if (!e || is_constant(e)) return {};
        auto p = place_of(e);
        if (!p) return operand(e);
        bir::LocalId t = temp(e.type(pool_));
        bir::Rvalue rv(bir::Rvalue::K::Use);
        rv.ops.push_back(read_place(*p, e.type(pool_)));
        assign({t, {}}, std::move(rv));
        return read_place({t, {}}, e.type(pool_));
    }
    // A literal, or an array of them (`&[]`, `&[1, 2]`): what Rust promotes to
    // a `'static` place under a shared borrow.
    bool is_promotable_literal(lir_view::ExprRef e, int depth = 0) const {
        if (!e || depth > 4) return false;
        switch (e.kind()) {
            case ECode::LitInt: case ECode::LitFloat: case ECode::LitBool: case ECode::LitStr:
                return true;
            case ECode::ArrLit: {
                lir_view::EArrLitView a{e};
                for (uint64_t i = 0; i < a.count(); ++i)
                    if (!is_promotable_literal(a.elem(i), depth + 1)) return false;
                return true;
            }
            default:
                return false;
        }
    }
    bool is_constant(lir_view::ExprRef e) const {
        switch (e.kind()) {
            case ECode::LitInt: case ECode::LitFloat: case ECode::LitBool:
            case ECode::LitStr: case ECode::EnumLit: case ECode::SizeOf:
            case ECode::AlignOf: case ECode::TypeCodeOf: case ECode::GenericRef:
                return true;
            case ECode::VarRef: {
                lir_view::EVarRefView v{e};
                return find_local(v.name(), v.var_slot()) == bir::kNoLocal;
            }
            default:
                return false;
        }
    }

    // ── borrows ──
    // `&place` or `&mut place`. The loan's place is the place itself; its
    // mutability is the reference type's.
    bir::Rvalue ref_of(bir::Place p, bool is_mut, bool two_phase) {
        bir::Rvalue rv(bir::Rvalue::K::Ref);
        rv.place     = std::move(p);
        rv.mut_      = is_mut;
        rv.two_phase = two_phase;
        rv.what      = is_mut ? "&mut" : "&";
        return rv;
    }

    // ── expressions into a destination ──
    void lower_into(bir::Place dest, lir_view::ExprRef e) {
        if (!e) return;
        // Extending positions are `&E` itself and the operands of a braced
        // struct / tuple / array literal; anything else (a call's arguments,
        // an operator's operands) ends the extension.
        struct ExtGuard { bool& f; bool v; ~ExtGuard() { f = v; } } ext_guard{extending_, extending_};
        switch (e.kind()) {
            case ECode::AddrOf: case ECode::AddrOfTemp: case ECode::StructLit:
            case ECode::TupleLit: case ECode::ArrLit:
            // `&[&t]` coerced to `&[&dyn Tc]` is a SliceLit / a cast around the
            // borrow: the operand of a cast is an extending position too.
            case ECode::SliceLit: case ECode::Cast: break;
            default: extending_ = false; break;
        }
        switch (e.kind()) {
            case ECode::AddrOf: {
                lir_view::EAddrOfView v{e};
                bir::LocalId l = find_local(v.var_name(), ~0u);
                TypeRef t = e.type(pool_);
                if (l == bir::kNoLocal) { assign(dest, bir::Rvalue(bir::Rvalue::K::Use)); return; }
                if (t && t.kind() == LogosType::Kind::Ptr) {
                    bir::Rvalue rv(bir::Rvalue::K::RawAddr);
                    rv.place = {l, {}};
                    assign(dest, std::move(rv));
                    return;
                }
                bool m = t && (t.kind() == LogosType::Kind::MutRef || t.mut_ptr());
                assign(dest, ref_of({l, {}}, m, false));
                return;
            }
            case ECode::AddrOfTemp: {
                lir_view::EAddrOfTempView v{e};
                TypeRef t = e.type(pool_);
                // `&*fat` where the inner expression already IS the fat
                // reference (a borrow of an unsized place: `&self.inner.val`
                // for `T = dyn`, `&v[..]`): same type in and out, no new loan.
                if (TypeRef it = v.inner() ? v.inner().type(pool_) : TypeRef{};
                    it && t && it.kind() == t.kind() &&
                    ((it.kind() == LogosType::Kind::Slice && !it.owning_slice()) ||
                     (it.kind() == LogosType::Kind::TraitObject && !it.owning_trait_object()) ||
                     (it.kind() == LogosType::Kind::DstRef && !it.owning_dst()))) {
                    lower_into(std::move(dest), v.inner());
                    return;
                }
                // Rvalue static promotion (Rust reference, "constant
                // promotion"): a SHARED borrow of a literal is a `&'static` to
                // a promoted constant, not a loan of a temporary — `return
                // &5i32;` is legal. Literals only; a named const or a constant
                // call is not claimed here.
                if (!v.is_mut() && v.inner() && t && t.kind() != LogosType::Kind::Ptr &&
                    is_promotable_literal(v.inner())) {
                    assign(std::move(dest), bir::Rvalue(bir::Rvalue::K::Use));
                    return;
                }
                // `&mut *x.deref()`: the deref feeding a mutable borrow is a
                // `deref_mut`, whatever the L-IR spelled (sema may emit the
                // shared one for a mutable receiver).
                bool saved = mut_deref_ctx_;
                mut_deref_ctx_ = v.is_mut();
                bir::Place p;
                bool saved_ept = extend_place_temps_;
                extend_place_temps_ = extending_;
                auto known = place_of(v.inner());
                extend_place_temps_ = saved_ept;
                if (known) p = *known;
                else {
                    // The borrowed temporary. In an extending position of a
                    // `let` it lives as long as the binding; its own operands
                    // do not.
                    bir::LocalId tl = new_local("_t" + std::to_string(body_.locals.size()),
                                                bir::LocalKind::Temp, v.inner().type(pool_), true, ~0u, line_);
                    if (extending_) declare_outer(tl); else declare(tl);
                    // The operand of an extending `&` is itself in an extending
                    // position (`&&&x`); lower_into ends the extension at the
                    // first expression that is not one.
                    lower_into(bir::Place{tl, {}}, v.inner());
                    p = bir::Place{tl, {}};
                }
                mut_deref_ctx_ = saved;
                if (t && t.kind() == LogosType::Kind::Ptr) {
                    bir::Rvalue rv(bir::Rvalue::K::RawAddr);
                    rv.place = std::move(p);
                    assign(dest, std::move(rv));
                    return;
                }
                assign(dest, ref_of(std::move(p), v.is_mut(), false));
                return;
            }
            case ECode::BinOp: {
                lir_view::EBinOpView v{e};
                bir::Rvalue rv(bir::Rvalue::K::Op);
                rv.what = std::string(v.op());
                // Operands are read, not consumed: comparison operators take
                // references and arithmetic is on Copy scalars. (A by-value
                // Add on a move type is a known gap of v1.)
                rv.ops.push_back(as_read(operand(v.lhs())));
                rv.ops.push_back(as_read(operand(v.rhs())));
                assign(dest, std::move(rv));
                return;
            }
            case ECode::Unary: {
                lir_view::EUnaryView v{e};
                bir::Rvalue rv(bir::Rvalue::K::Op);
                rv.what = std::string(v.op());
                rv.ops.push_back(as_read(operand(v.operand())));
                assign(dest, std::move(rv));
                return;
            }
            case ECode::Cast: {
                bir::Rvalue rv(bir::Rvalue::K::Op);
                rv.what = "cast";
                // `r as *mut T` with r: &mut T reborrows r (Rust); r stays usable.
                lir_view::ExprRef op = lir_view::ECastView{e}.operand();
                bir::Operand o = operand(op);
                if (op && op.type(pool_) && op.type(pool_).kind() == LogosType::Kind::MutRef)
                    o = as_read(std::move(o));
                rv.ops.push_back(std::move(o));
                assign(dest, std::move(rv));
                return;
            }
            case ECode::SliceLen: case ECode::SlicePtr: {
                auto s = e.kind() == ECode::SliceLen ? lir_view::ESliceLenView{e}.slice()
                                                     : lir_view::ESlicePtrView{e}.slice();
                bir::Rvalue rv(bir::Rvalue::K::Op);
                rv.what = e.kind() == ECode::SliceLen ? "len" : "ptr";
                rv.ops.push_back(as_read(operand(s)));
                assign(dest, std::move(rv));
                return;
            }
            case ECode::PtrArith: case ECode::PtrDiff: {
                bir::Rvalue rv(bir::Rvalue::K::Op);
                rv.what = "ptr";
                if (e.kind() == ECode::PtrArith) {
                    lir_view::EPtrArithView v{e};
                    rv.ops.push_back(as_read(operand(v.ptr())));
                    rv.ops.push_back(as_read(operand(v.offset())));
                } else {
                    lir_view::EPtrDiffView v{e};
                    rv.ops.push_back(as_read(operand(v.lhs())));
                    rv.ops.push_back(as_read(operand(v.rhs())));
                }
                assign(dest, std::move(rv));
                return;
            }
            case ECode::StructLit: {
                bir::Rvalue rv(bir::Rvalue::K::Aggregate);
                rv.what = std::string(lir_view::EStructLitView{e}.name());
                lir_view::EStructLitView{e}.each_field([&](std::string_view fname, lir_view::ExprRef fv) {
                    rv.ops.push_back(operand(fv));
                    rv.field_names.emplace_back(fname);
                });
                assign(dest, std::move(rv));
                return;
            }
            case ECode::TupleLit: {
                bir::Rvalue rv(bir::Rvalue::K::Aggregate);
                rv.what = "tuple";
                lir_view::ETupleLitView{e}.each_elem([&](lir_view::ExprRef el) {
                    rv.ops.push_back(operand(el));
                });
                assign(dest, std::move(rv));
                return;
            }
            case ECode::ArrLit: {
                bir::Rvalue rv(bir::Rvalue::K::Aggregate);
                rv.what = "array";
                lir_view::EArrLitView{e}.each_elem([&](lir_view::ExprRef el) {
                    rv.ops.push_back(operand(el));
                });
                assign(dest, std::move(rv));
                return;
            }
            case ECode::EnumLitData: {
                bir::Rvalue rv(bir::Rvalue::K::Aggregate);
                rv.what = std::string(lir_view::EEnumLitDataView{e}.variant());
                lir_view::EEnumLitDataView{e}.each_payload([&](lir_view::ExprRef pl) {
                    rv.ops.push_back(operand(pl));
                });
                assign(dest, std::move(rv));
                return;
            }
            case ECode::SliceLit: {
                lir_view::ESliceLitView v{e};
                bir::Rvalue rv(bir::Rvalue::K::Aggregate);
                rv.what = "slice";
                rv.ops.push_back(operand(v.base()));
                rv.ops.push_back(operand(v.len()));
                assign(dest, std::move(rv));
                return;
            }
            case ECode::IfExpr: {
                lir_view::EIfExprView v{e};
                bir::Operand c = operand(v.cond());
                bir::BlockId bt = new_block(), bf = new_block(), join = new_block();
                branch(std::move(c), {bt, bf});
                cur_ = bt; lower_into(dest, v.then_val()); goto_(join);
                cur_ = bf; lower_into(dest, v.else_val()); goto_(join);
                cur_ = join;
                return;
            }
            case ECode::BlockExpr: {
                lir_view::EBlockExprView v{e};
                push_scope();
                if (auto b = v.block()) lower_block_stmts(b);
                if (v.result()) lower_into(dest, v.result());
                pop_scope();
                return;
            }
            case ECode::MatchExpr: {
                lir_view::EMatchExprView v{e};
                lower_match(v.scrut(), [&](auto&& f) { v.each_arm(f); }, &dest);
                return;
            }
            case ECode::Try: {
                lir_view::ETryView v{e};
                bir::Place src = place_or_temp(v.inner());
                bir::BlockId ok = new_block(), err = new_block();
                branch(read_place(src, v.inner().type(pool_)), {ok, err});
                cur_ = err;
                // The error path returns early: every scope dies.
                exit_scopes(0, 0);
                terminate(bir::Term(bir::Term::K::Return));
                cur_ = ok;
                bir::Rvalue rv(bir::Rvalue::K::Use);
                rv.ops.push_back(read_place(src.with(bir::Proj(bir::Proj::K::Field, "Ok#0")), e.type(pool_)));
                assign(dest, std::move(rv));
                return;
            }
            case ECode::Call: case ECode::MethodCall: case ECode::ClosureCall:
            case ECode::FnPtrCall: case ECode::FormatCall:
                lower_call(dest, e);
                return;
            case ECode::ClosureBox: {
                // A closure VALUE is an aggregate of its captures, as MIR builds
                // Rvalue::Aggregate(AggregateKind::Closure, ..): a by-reference
                // capture issues its loan HERE, at the construction point, and a
                // by-value one moves. The mode is READ (closure_keys::
                // CAPTURE_MODES), never derived from {IS_MOVE, MUT_CAPTURES} —
                // that pair cannot separate ByValue from ImmBorrow and loses the
                // mutation whenever sema widened the path to an LCA.
                // ⚠ THE BODY IS NOT LOWERED BY THIS ARM. A violation visible only
                // inside the closure stays with the old checker and shows up as
                // an `old_only` row; the body becomes its own bir::Body later.
                lir_view::EClosureBoxView v{e};
                // ADR 0029 S5b: the BODY becomes its own `bir::Body`, as MIR
                // gives a closure one. Recorded here, lowered after this body.
                closures_.push_back(e);
                bir::Rvalue rv(bir::Rvalue::K::Aggregate);
                rv.what = std::string(v.closure_id());
                std::vector<std::pair<std::string, TypeRef>> caps;
                v.each_capture(pool_, [&](std::string_view n, TypeRef t) {
                    caps.emplace_back(std::string(n), t);
                });
                for (uint64_t i = 0; i < caps.size(); ++i) {
                    bir::LocalId root = find_local(caps[i].first, ~0u);
                    if (root == bir::kNoLocal) {
                        unsupported("closure: a capture names no local in scope");
                        assign(dest, bir::Rvalue(bir::Rvalue::K::Use));
                        return;
                    }
                    bir::Place p{root, {}};
                    // CAPTURE_PATHS is `root.f.g`; the tail after the root is a
                    // chain of field projections on the place.
                    std::string_view path = v.capture_path(i);
                    TypeRef cur = caps[i].second;
                    if (path.size() > caps[i].first.size() &&
                        path.substr(0, caps[i].first.size()) == caps[i].first) {
                        std::string_view tail = path.substr(caps[i].first.size() + 1);
                        while (!tail.empty()) {
                            auto dot = tail.find('.');
                            std::string_view f = dot == std::string_view::npos ? tail : tail.substr(0, dot);
                            // A FIELD REACHED THROUGH A REFERENCE IS REACHED BY
                            // DEREFERENCING IT, and the place has to say so. The
                            // capture path is a DOTTED STRING (`this.x`), which
                            // spells no deref even when the root is a `&mut`, so
                            // the place read `this.x` with a bare Field step —
                            // and `write_place`'s `!has_borrowing_deref` guard
                            // then took the write for a write to the BINDING,
                            // demanding `let mut this`. rustc demands nothing:
                            // writing THROUGH a `&mut` is not reassigning it
                            // (MEASURED, `let this = &mut f; let mut r = ||
                            // { this.x = this.x + 10; };` compiles). The DIRECT
                            // spelling of the same write was already right —
                            // only the capture path, which builds its place
                            // from the string instead of from the type, was not.
                            while (cur && (cur.kind() == LogosType::Kind::Ref ||
                                           cur.kind() == LogosType::Kind::MutRef)) {
                                bir::Proj dr(bir::Proj::K::Deref);
                                cur = cur.pointee();
                                dr.ty = cur;
                                p = p.with(std::move(dr));
                            }
                            bir::Proj pr(bir::Proj::K::Field, std::string(f));
                            cur = field_type(cur, f);
                            pr.ty = cur;
                            p = p.with(std::move(pr));
                            tail = dot == std::string_view::npos ? std::string_view{} : tail.substr(dot + 1);
                        }
                    }
                    TypeRef leaf = v.capture_field_type(pool_, i);
                    if (leaf) cur = leaf;
                    const uint8_t mode = v.capture_mode(i);
                    // (The guard that used to stand here — refusing a move-type
                    // capture in a non-`move` closure — is gone: sema now
                    // records ByValue when the BODY CONSUMES the capture, which
                    // is rustc's rule, so an ImmBorrow on a move type is a
                    // known fact and no longer an unknown.)
                    // A SHARED REFERENCE IS Copy, SO AN ImmBorrow OF ONE IS A
                    // COPY OF IT, NOT A LOAN OF THE LOCAL THAT HOLDS IT. rustc
                    // captures the PLACE the body touches (RFC 2229): for
                    // `|| *r` with `r: &'a i64` that is `*r`, so the env holds a
                    // `&'a i64` and the closure borrows nothing of the enclosing
                    // frame. The capture PATH is a dotted string and a deref is
                    // not spellable in it, so the path names the root `r` and
                    // this arm issued `& r` — a loan of a LOCAL, which then
                    // escaped with the closure and made `fn mk<'a>(r: &'a i64)
                    // -> Box<dyn Fn() -> i64 + 'a>` refuse (rustc accepts it,
                    // MEASURED). Copying the reference gives the same env value
                    // with the same region and no loan at all.
                    // ⚠ SHARED ONLY. A `&mut` is not Copy: rustc REBORROWS it,
                    // and the reborrow is a real loan of the place that holds it.
                    const bool copy_shared_ref =
                        mode == 0 && cur && cur.kind() == LogosType::Kind::Ref;
                    if (mode == 2 || copy_shared_ref) {    // ByValue, or a Copy of a `&T`
                        rv.ops.push_back(read_place(std::move(p), cur));
                    } else {
                        // A MutBorrow recorded for a WIDENED path is known only
                        // for a place wider than the one the body touches, so it
                        // is taken as a shared loan — which is exactly what the
                        // folded `mut_captures` bit used to encode, now stated
                        // rather than implied.
                        const bool m = mode == 1 && !v.capture_widened(i);
                        bir::LocalId tmp = synth_ref_temp(cur, m);
                        assign({tmp, {}}, ref_of(std::move(p), m, /*two_phase=*/false));
                        bir::Operand o;
                        o.k = bir::Operand::K::Move;
                        o.place = {tmp, {}};
                        rv.ops.push_back(std::move(o));
                    }
                    rv.field_names.push_back(caps[i].first);
                }
                assign(dest, std::move(rv));
                return;
            }
            case ECode::PackExpand:
                unsupported("expr:PackExpand");
                assign(dest, bir::Rvalue(bir::Rvalue::K::Use));
                return;
            case ECode::WritLit: {
                // A Writ literal has two shapes and the L-IR already says which.
                // CLOSED (`@{ a: 1 }`, `quote_item!`): a pre-serialised blob
                // destined for rodata, no sub-expressions — a constant, which is
                // what the empty `Use` already was. The function was being
                // dropped from the comparison for a node that borrows nothing.
                // WITH CAPTURES (`@{ a: $x }`): the captures are ordinary
                // expressions evaluated here and patched into the template, so
                // the value is an AGGREGATE over them and may hold what they
                // hold. Reading them as operands is conservative in the right
                // direction: a capture that carries a loan reaches the result.
                // ⚠ NOT MODELLED: the call to `writ_build_from_template` the
                // capture shape lowers to. Sema resolves that callee and keeps
                // only its return type (lower_writ_lit), so the L-IR does not
                // name it and its SIGNATURE cannot be read — the result's
                // regions come from the aggregate instead of from the callee.
                lir_view::EWritLitView v{e};
                if (!v.has_captures()) {
                    assign(dest, bir::Rvalue(bir::Rvalue::K::Use));
                    return;
                }
                bir::Rvalue rv(bir::Rvalue::K::Aggregate);
                rv.what = "writ";
                v.each_capture_expr([&](lir_view::ExprRef c) {
                    rv.ops.push_back(operand(c));
                });
                assign(dest, std::move(rv));
                return;
            }
            case ECode::ReflectOf:
                // `EReflectOf{T}` has no sub-expressions at all — codegen lowers
                // it to the ADDRESS OF A STATIC (`__logos_reflect__<hash>` plus
                // an offset), so it borrows nothing from the function and is a
                // constant here. The empty `Use` the refusal already wrote was
                // the right lowering; only the refusal beside it was wrong, and
                // it cost the whole enclosing function.
                assign(dest, bir::Rvalue(bir::Rvalue::K::Use));
                return;
            default: {
                // Constants and place expressions: a plain use.
                bir::Rvalue rv(bir::Rvalue::K::Use);
                rv.ops.push_back(operand(e));
                assign(dest, std::move(rv));
                return;
            }
        }
    }
    static bir::Operand as_read(bir::Operand o) {
        if (o.k == bir::Operand::K::Move) o.k = bir::Operand::K::Copy;
        return o;
    }
    void branch(bir::Operand d, std::vector<bir::BlockId> targets) {
        bir::Term t(bir::Term::K::Branch);
        t.discr   = std::move(d);
        t.targets = std::move(targets);
        terminate(std::move(t));
    }

    // rustc's AllowTwoPhase::Yes borrows: a method-receiver autoref, an
    // implicit reborrow at a coercion site, an overloaded `x op= y`. Sema
    // records which one it built (BorrowOrigin); nothing here infers it.
    static bool two_phase_origin(lir_schema::expr::BorrowOrigin o) {
        using BO = lir_schema::expr::BorrowOrigin;
        return o == BO::Autoref || o == BO::Reborrow || o == BO::CompoundAssign;
    }
    static lir_schema::expr::BorrowOrigin borrow_origin(lir_view::ExprRef a) {
        if (a && a.kind() == ECode::AddrOfTemp) return lir_view::EAddrOfTempView{a}.origin();
        if (a && a.kind() == ECode::AddrOf)     return lir_view::EAddrOfView{a}.origin();
        return lir_schema::expr::BorrowOrigin::Unknown;
    }
    // The `&mut` loan lowering `a` just issued into `op`'s temporary becomes a
    // two-phase reservation, activated by the call being built.
    void reserve_if_two_phase(lir_view::ExprRef a, const bir::Operand& op,
                              std::vector<std::pair<bir::BlockId, uint32_t>>& reservations) {
        if (!two_phase_origin(borrow_origin(a)) || !op.place.valid() || !op.place.proj.empty()) return;
        auto& stmts = body_.blocks[cur_].stmts;
        for (size_t i = stmts.size(); i-- > 0;) {
            auto& st = stmts[i];
            if (st.k != bir::Stmt::K::Assign || st.dest.local != op.place.local || !st.dest.proj.empty()) continue;
            if (st.rv.k == bir::Rvalue::K::Ref && st.rv.mut_) {
                st.rv.two_phase = true;
                reservations.emplace_back(cur_, uint32_t(i));
            }
            return;
        }
    }

    // ── calls ──
    // Receiver first, then arguments left to right, then the call. An
    // implicit `&mut` receiver (sema's AddrOfTemp autoref, or a bare place
    // passed to `&mut self`) is a TWO-PHASE borrow: reserved here, activated
    // at the call.
    void lower_call(bir::Place dest, lir_view::ExprRef e) {
        bir::Term t(bir::Term::K::Call);
        std::vector<std::pair<bir::BlockId, uint32_t>> reservations;
        auto arg_value = [&](lir_view::ExprRef a) {
            bir::Operand op = call_operand(a);
            reserve_if_two_phase(a, op, reservations);
            t.args.push_back(std::move(op));
        };
        // A reference parameter given a value that is neither a reference nor
        // a borrow node: sema left the autoref to codegen, so the loan is not
        // in the L-IR. Refuse rather than guess it.
        auto ref_param_gap = [&](lir_view::ExprRef a, TypeRef pt) {
            if (!a || !pt) return false;
            bool ref_param = pt.kind() == LogosType::Kind::Ref || pt.kind() == LogosType::Kind::MutRef;
            if (ref_param && !is_pointer_kind(a.type(pool_)) && a.type(pool_) &&
                a.type(pool_).kind() != LogosType::Kind::Slice && place_of(a)) {
                unsupported("call: a place for a reference parameter without a borrow node");
                return true;
            }
            return false;
        };
        switch (e.kind()) {
            case ECode::Call: {
                lir_view::ECallView v{e};
                t.callee = std::string(v.callee());
                if (flows_) t.flow = resolve_call_flow(*flows_, v.callee(), &idx_);
                std::vector<TypeRef> ptypes;
                if (auto cf = resolve_call(v.callee())) {
                    for (auto& pv : cf.params()) ptypes.push_back(pv.type(pool_));
                    t.has_sig = true;
                    t.sig_to_result = signature_mask(cf);
                    t.callee_fn = cf;
                }
                size_t ai = 0;
                v.each_arg([&](lir_view::ExprRef a) {
                    TypeRef pt = ai < ptypes.size() ? ptypes[ai] : TypeRef{};
                    ++ai;
                    if (raw_addr_arg(a, pt, t)) return;
                    ref_param_gap(a, pt);
                    arg_value(a);
                });
                break;
            }
            case ECode::MethodCall: {
                lir_view::EMethodCallView v{e};
                t.callee = std::string(v.resolved_symbol().empty() ? v.method() : v.resolved_symbol());
                if (flows_) t.flow = resolve_method_flow(*flows_, idx_, pool_, v);
                if (auto f = resolve_method_exact(v)) {
                    t.has_sig = true;
                    t.sig_to_result = signature_mask(f);
                    t.callee_fn = f;
                } else if (is_deref_method(v)) {
                    // Deref::deref(&'a self) -> &'a Target.
                    t.has_sig = true;
                    t.sig_to_result = 1;
                }
                lir_view::ExprRef rcv = v.receiver();
                int sk = self_kind(v);
                bool place_op_mut = mut_deref_ctx_ && is_deref_method(v);
                if (sk == 1 && place_op_mut) sk = 2;
                mut_deref_ctx_ = false;   // applies to this call's receiver only
                if (rcv && rcv.kind() == ECode::AddrOfTemp) {
                    lir_view::EAddrOfTempView a{rcv};
                    bool m = a.is_mut() || place_op_mut;
                    bool saved = mut_deref_ctx_;
                    mut_deref_ctx_ = m;
                    bir::Place p = place_or_temp(a.inner());
                    mut_deref_ctx_ = saved;
                    bir::LocalId tmp = synth_ref_temp(a.inner().type(pool_), m);
                    bool tp = m && two_phase_origin(a.origin());
                    assign({tmp, {}}, ref_of(std::move(p), m, tp));
                    if (tp)
                        reservations.emplace_back(cur_, uint32_t(body_.blocks[cur_].stmts.size() - 1));
                    t.args.push_back(read_place({tmp, {}}, rcv.type(pool_)));
                } else if (rcv && sk == 3) {
                    auto f = resolve_method(v);
                    TypeRef p0 = f && !f.params_empty() ? f.params()[0].type(pool_) : TypeRef{};
                    if (p0 ? !raw_addr_arg(rcv, p0, t) : !raw_addr_place(rcv, t)) arg_value(rcv);
                } else if (rcv && sk >= 1 && !is_pointer_kind(rcv.type(pool_))) {
                    TypeRef rty = rcv.type(pool_);
                    bool fat_ref = rty && ((rty.kind() == LogosType::Kind::TraitObject && !rty.owning_trait_object()) ||
                                           (rty.kind() == LogosType::Kind::Slice && !rty.owning_slice()) ||
                                           (rty.kind() == LogosType::Kind::DstRef && !rty.owning_dst()));
                    if (fat_ref) {
                        // `&dyn Tr`, `&mut [T]`, `&Dst`: the receiver IS a
                        // reference, and a method call REBORROWS it (`&*x`, a
                        // two-phase `&mut *x`) as it does a thin one — sema
                        // spells that reborrow for thin references only. Without
                        // the loan, `let y = x.f1(); x.f2(); use(y);` over `x:
                        // &mut dyn Foo` went unrefused.
                        bool m = sk == 2;
                        bir::Place p = place_or_temp(rcv).with(bir::Proj(bir::Proj::K::Deref));
                        bir::LocalId tmp = synth_ref_temp({}, m);
                        assign({tmp, {}}, ref_of(std::move(p), m, m));
                        if (m)
                            reservations.emplace_back(cur_, uint32_t(body_.blocks[cur_].stmts.size() - 1));
                        bir::Operand o;
                        o.k = m ? bir::Operand::K::Move : bir::Operand::K::Copy;
                        o.place = {tmp, {}};
                        t.args.push_back(std::move(o));
                    } else {
                        // A value receiver of a `&self` / `&mut self` method with
                        // no borrow node in the L-IR (sema leaves the address-of
                        // to codegen for a `Box<dyn Tr>` handle, a zoned struct, a
                        // primitive rvalue). What the language means is not in
                        // doubt: `x.m()` borrows `x` — shared for `&self`, a
                        // two-phase `&mut` for `&mut self` — and an rvalue is
                        // borrowed through a temporary. The self kind comes from
                        // the callee's signature, not from the receiver's shape.
                        bool m = sk == 2;
                        bir::Place p = place_or_temp(rcv);
                        bir::LocalId tmp = synth_ref_temp(rty, m);
                        assign({tmp, {}}, ref_of(std::move(p), m, m));
                        if (m)
                            reservations.emplace_back(cur_, uint32_t(body_.blocks[cur_].stmts.size() - 1));
                        bir::Operand o;
                        o.k = m ? bir::Operand::K::Move : bir::Operand::K::Copy;
                        o.place = {tmp, {}};
                        t.args.push_back(std::move(o));
                    }
                } else if (rcv) {
                    arg_value(rcv);
                }
                {
                    std::vector<TypeRef> ptypes;
                    if (auto f = resolve_method(v))
                        for (auto& pv : f.params()) ptypes.push_back(pv.type(pool_));
                    size_t ai = 1;
                    v.each_arg([&](lir_view::ExprRef a) {
                        TypeRef pt = ai < ptypes.size() ? ptypes[ai] : TypeRef{};
                        ++ai;
                        if (raw_addr_arg(a, pt, t)) return;
                        ref_param_gap(a, pt);
                        arg_value(a);
                    });
                }
                break;
            }
            case ECode::ClosureCall: {
                lir_view::EClosureCallView v{e};
                t.callee = "<closure>";
                // #434: `f(args)` is `Fn::call(&f, ..)`, `FnMut::call_mut(&mut
                // f, ..)` or `FnOnce::call_once(f, ..)` — as MIR has it, with
                // the autoref explicit and, for `&mut`, two-phase. Sema says
                // which (CallMode); Unknown keeps the plain read.
                using CM = lir_schema::expr::CallMode;
                lir_view::ExprRef c = v.callee();
                switch (v.call_mode()) {
                    case CM::Shared: case CM::Mut: {
                        bool m = v.call_mode() == CM::Mut;
                        TypeRef ct = c ? c.type(pool_) : TypeRef{};
                        bir::Place p = place_or_temp(c);
                        TypeRef pointee = ct;
                        if (ct && (ct.kind() == LogosType::Kind::Ref ||
                                   ct.kind() == LogosType::Kind::MutRef) && ct.pointee()) {
                            // Called through `&F` / `&mut F`: a reborrow of `*f`.
                            pointee = ct.pointee();
                            bir::Proj d(bir::Proj::K::Deref);
                            d.ty = pointee;
                            p = p.with(std::move(d));
                        } else if (ct && ct.borrowed_dyn_callable()) {
                            // `&dyn Fn*` / `&mut dyn Fn*`: the value IS the fat
                            // borrow, so the autoref is a reborrow of `*f` and
                            // not a borrow of the binding `f`. Its pointee is
                            // the callable itself.
                            bir::Proj d(bir::Proj::K::Deref);
                            d.ty = ct;
                            p = p.with(std::move(d));
                        }
                        bir::LocalId tmp = synth_ref_temp(pointee, m);
                        assign({tmp, {}}, ref_of(std::move(p), m, /*two_phase=*/m));
                        if (m)
                            reservations.emplace_back(cur_, uint32_t(body_.blocks[cur_].stmts.size() - 1));
                        bir::Operand o;
                        o.k = m ? bir::Operand::K::Move : bir::Operand::K::Copy;
                        o.place = {tmp, {}};
                        t.args.push_back(std::move(o));
                        // `Fn::call(&self, Args) -> Self::Output`: Output is
                        // fixed by `F` and `Args`, so the result cannot hold
                        // the autoref's loan — the declared signature, as for
                        // any other callee. Every other argument may reach it.
                        t.has_sig = true;
                        t.sig_to_result = ~uint64_t(1);
                        // ⚠ THE MASK EXCLUDES THE WHOLE ARGUMENT, AND THE
                        // CAPTURES ARE INSIDE IT. `Self::Output` cannot hold the
                        // AUTOREF's loan — that is what the mask says and it is
                        // right — but it may hold what `F` itself holds: `|| &l`
                        // returns `&'a i64` with `'a` from the capture. Dropping
                        // the argument dropped both, so `fn get() -> &i64 { let
                        // l = 5; (|| &l)() }` compiled clean. One origin is the
                        // autoref; the rest are the closure's, and they relate.
                        t.closure_recv = 1;
                        break;
                    }
                    case CM::Once:
                        t.args.push_back(call_operand(c));
                        break;
                    case CM::Unknown:
                        t.args.push_back(as_read(operand(c)));
                        break;
                }
                v.each_arg(arg_value);
                break;
            }
            case ECode::FnPtrCall: {
                lir_view::EFnPtrCallView v{e};
                t.callee = "<fnptr>";
                t.has_sig = true;
                t.callee_fty = v.callee() ? v.callee().type(pool_) : TypeRef{};
                t.sig_to_result = signature_mask_of_type(
                    v.callee() ? v.callee().type(pool_) : TypeRef{}, /*arg_off=*/1);
                t.sig_mut_params = mut_param_mask_of_type(
                    v.callee() ? v.callee().type(pool_) : TypeRef{}, /*arg_off=*/1);
                t.args.push_back(as_read(operand(v.callee())));
                v.each_arg(arg_value);
                break;
            }
            case ECode::FormatCall: {
                lir_view::EFormatCallView v{e};
                t.callee = "<format>";
                t.args.push_back(as_read(operand(v.fmt())));
                // Format arguments are taken by reference.
                v.each_arg([&](lir_view::ExprRef a) { t.args.push_back(as_read(operand(a))); });
                break;
            }
            default:
                break;
        }
        // The call writes a fresh temporary; the continuation block stores it
        // into `dest`. Arguments leave at the call, the result arrives after,
        // so `x = f(x)` moves x and then initialises it at DIFFERENT points.
        bir::LocalId res = temp({});
        body_.locals[res].type = dest.valid() && dest.proj.empty() ? body_.locals[dest.local].type
                                                                   : e.type(pool_);
        t.dest = {res, {}};
        bir::BlockId next = new_block();
        // A call of type `!` (`panic(..)`) does not return: MIR gives it no
        // successor. With one, the path that never assigns `x` in `let x = if
        // c { 10 } else { panic("..") };` reached the join and `x` was read
        // uninitialised.
        bool diverges = e.type(pool_) && e.type(pool_).kind() == LogosType::Kind::Never;
        if (!diverges) t.targets = {next};
        terminate(std::move(t));
        bir::BlockId call_block = cur_;   // terminate() acted on cur_
        for (auto [b, st] : reservations) body_.two_phase.push_back({b, st, call_block});
        cur_ = next;
        if (diverges) { terminate(bir::Term(bir::Term::K::Unreachable)); cur_ = new_block(); return; }
        if (dest.valid()) {
            bir::Rvalue rv(bir::Rvalue::K::Use);
            rv.ops.push_back(read_place({res, {}}, body_.locals[res].type));
            assign(std::move(dest), std::move(rv));
        }
    }
    // Lifetime names a type mentions, and whether it holds an elided one.
    void lifetimes_in(TypeRef t, std::set<std::string>& names, bool& elided, int depth = 0) const {
        if (!t || depth > 16) return;
        switch (t.kind()) {
            case LogosType::Kind::Ref: case LogosType::Kind::MutRef: {
                std::string lt(t.lifetime());
                if (lt.empty() || lt == "'_") elided = true;
                else if (lt != "'static") names.insert(lt);
                lifetimes_in(t.pointee(), names, elided, depth + 1);
                return;
            }
            case LogosType::Kind::Slice:
                if (t.raw_fat()) return;
                if (!t.owning_slice()) {
                    std::string lt(t.lifetime());
                    if (lt.empty() || lt == "'_") elided = true;
                    else if (lt != "'static") names.insert(lt);
                }
                lifetimes_in(t.elem(), names, elided, depth + 1);
                return;
            case LogosType::Kind::TraitObject:
                if (!t.owning_trait_object() && !t.raw_fat()) elided = true;
                return;
            case LogosType::Kind::DstRef:
                if (!t.owning_dst() && !t.raw_fat()) elided = true;
                return;
            case LogosType::Kind::Array:
                lifetimes_in(t.elem(), names, elided, depth + 1);
                return;
            case LogosType::Kind::Tuple:
                for (auto& e : t.tuple_elems()) lifetimes_in(e, names, elided, depth + 1);
                return;
            case LogosType::Kind::Struct: case LogosType::Kind::Enum: {
                auto lts = t.lifetime_args();
                for (auto& lt : lts) {
                    if (lt.empty() || lt == "'_" || lt == "_") elided = true;
                    else if (lt != "'static" && lt != "static") names.insert(lt[0] == '\'' ? lt : "'" + lt);
                }
                for (auto& a : t.type_args()) lifetimes_in(a, names, elided, depth + 1);
                if (lts.empty() && names.empty() && bir_type_carries_loans(t, prog_, ts_))
                    elided = true;
                return;
            }
            case LogosType::Kind::Closure:
                elided = true;
                return;
            default:
                return;
        }
    }
    // Which arguments the result may borrow from, by the signature of `f`.
    // THE SAME QUESTION ASKED OF A FN-POINTER'S TYPE. A `fn(A, B) -> R` value
    // carries its regions in the type itself, so which parameters share one with
    // the result is readable without a FunctionView — and without it
    // `relate_call` bails on a fn-pointer call (`t.callee_fn` is null) and the
    // fallback ties the result to EVERY borrowing argument. `arg_off` is 1 for a
    // FnPtrCall, whose first operand is the pointer itself, so the bits line up
    // with `t.args`.
    // Which argument positions can be WRITTEN THROUGH, read off a fn-pointer's
    // type. An indirect call has no body to summarise, so this is what the
    // out-param fallback has instead.
    uint64_t mut_param_mask_of_type(TypeRef fty, size_t arg_off) const {
        if (!fty) return 0;
        uint64_t mask = 0;
        auto ps = fty.closure_params();
        for (size_t i = 0; i < ps.size() && i + arg_off < 64; ++i) {
            TypeRef pt = ps[i];
            if (!pt) continue;
            const bool writable =
                pt.kind() == LogosType::Kind::MutRef ||
                ((pt.kind() == LogosType::Kind::Slice || pt.kind() == LogosType::Kind::DstRef) &&
                 pt.mut_ptr() && !pt.raw_fat()) ||
                bc_holds_mut_ref_type(ts_, pt);
            if (writable) mask |= uint64_t(1) << (i + arg_off);
        }
        return mask;
    }
    uint64_t signature_mask_of_type(TypeRef fty, size_t arg_off) const {
        if (!fty) return ~uint64_t(0);
        auto ps = fty.closure_params();
        TypeRef ret = fty.closure_ret();
        std::set<std::string> rnames;
        bool relided = false;
        lifetimes_in(ret, rnames, relided);
        if (rnames.empty() && !relided) return 0;
        uint64_t mask = 0;
        std::vector<size_t> ref_params;
        for (size_t i = 0; i < ps.size() && i + arg_off < 64; ++i) {
            std::set<std::string> pn;
            bool pe = false;
            lifetimes_in(ps[i], pn, pe);
            for (auto& n : pn) if (rnames.count(n)) mask |= uint64_t(1) << (i + arg_off);
            if (pe || !pn.empty()) ref_params.push_back(i + arg_off);
        }
        // An elided result with no `self` takes every input region — Rust
        // refuses that signature (E0106) and Logos accepts it, so the
        // conservative reading is the one that keeps every candidate.
        if (relided) for (size_t i : ref_params) mask |= uint64_t(1) << i;
        return mask;
    }
    uint64_t signature_mask(lir_view::FunctionView f) const {
        std::set<std::string> rnames;
        bool relided = false;
        lifetimes_in(f.ret_type(pool_), rnames, relided);
        if (rnames.empty() && !relided) return 0;
        auto ps = f.params();
        uint64_t mask = 0;
        std::vector<size_t> ref_params;
        for (size_t i = 0; i < ps.size() && i < 64; ++i) {
            std::set<std::string> pn;
            bool pe = false;
            lifetimes_in(ps[i].type(pool_), pn, pe);
            for (auto& n : pn) if (rnames.count(n)) mask |= uint64_t(1) << i;
            if (pe || !pn.empty()) ref_params.push_back(i);
        }
        if (relided) {
            TypeRef p0 = ps.empty() ? TypeRef{} : ps[0].type(pool_);
            bool self_ref = !ps.empty() && ps[0].name() == "self" && p0 &&
                            (p0.kind() == LogosType::Kind::Ref || p0.kind() == LogosType::Kind::MutRef ||
                             (p0.kind() == LogosType::Kind::DstRef && !p0.owning_dst()));
            if (self_ref) mask |= 1;
            else for (size_t i : ref_params) mask |= uint64_t(1) << i;
        }
        return mask;
    }

    // The overloaded place operators: `*x` (Deref / DerefMut) and `x[i]`
    // (Index / IndexMut). Their result borrows the receiver, and in a mutable
    // context the step is the `_mut` one whatever the L-IR spelled.
    static bool is_deref_method(lir_view::EMethodCallView v) {
        auto m = v.method();
        return m == "deref" || m == "deref_mut" || m == "index" || m == "index_mut";
    }
    // The callee of a plain call: by name, else by the mono worklist key's
    // bare spelling (#83: a devirtualised generic method call names
    // `<Type>__<method>`, not any function's full name).
    lir_view::FunctionView resolve_call(std::string_view callee) const {
        if (auto it = idx_.by_name.find(std::string(callee)); it != idx_.by_name.end()) {
            ++bir::callee_census().call_name;
            return it->second;
        }
        if (auto it = idx_.by_bare.find(std::string(bare_fn_name(callee))); it != idx_.by_bare.end() &&
            it->second.size() == 1) {
            ++bir::callee_census().call_bare;
            return it->second.front();
        }
        ++bir::callee_census().call_miss;
        return {};
    }
    // The callee of a method call: by its resolved symbol, else by the mono
    // template key's bare spelling, else by the method's base name when every
    // candidate agrees on the receiver's reference kind (the same fallbacks
    // the counter-based checker's method_self_kind uses).
    // Exact resolution only: the resolved symbol, or the instance for this
    // receiver's concrete type. What the declared signature is read from.
    lir_view::FunctionView resolve_method_exact(lir_view::EMethodCallView v) const {
        if (auto it = idx_.by_name.find(std::string(v.resolved_symbol())); it != idx_.by_name.end()) {
            ++bir::callee_census().m_symbol;
            return it->second;
        }
        auto r = instance_for_receiver(v);
        ++(r ? bir::callee_census().m_recv : bir::callee_census().m_miss);
        return r;
    }
    lir_view::FunctionView instance_for_receiver(lir_view::EMethodCallView v) const {
        auto rcv = v.receiver();
        if (!rcv) return {};
        TypeRef rt = rcv.type(pool_);
        while (rt && (rt.kind() == LogosType::Kind::Ref || rt.kind() == LogosType::Kind::MutRef ||
                      rt.kind() == LogosType::Kind::Ptr))
            rt = rt.pointee();
        std::string cname;
        if (rt && rt.kind() == LogosType::Kind::Struct) cname = concrete_struct_name(rt);
        else if (rt && rt.kind() == LogosType::Kind::Enum)
            cname = rt.type_args().empty() ? std::string(rt.enum_name()) : Mono::enum_instance_name(rt);
        if (cname.empty()) return {};
        auto it = idx_.by_base.find(std::string(v.method()));
        if (it == idx_.by_base.end()) return {};
        std::string needle = "." + cname + "__" + std::string(v.method()) + "__";
        std::string needle2 = cname + "__" + std::string(v.method()) + "__";
        for (auto& f : it->second) {
            std::string n(f.name());
            if (n.find(needle) != std::string::npos || n.starts_with(needle2)) { ++bir::callee_census().ifr_needle; return f; }
        }
        // Instance names can carry a module hash the type's name does not
        // (`Buffer$M<hash>$G1$…`): match the receiver TYPE against each
        // candidate's `self` type instead.
        std::string want = type_str(rt, true);
        for (auto& f : it->second) {
            auto ps = f.params();
            if (ps.empty() || ps[0].name() != "self") continue;
            TypeRef p0 = ps[0].type(pool_);
            while (p0 && (p0.kind() == LogosType::Kind::Ref || p0.kind() == LogosType::Kind::MutRef ||
                          p0.kind() == LogosType::Kind::Ptr))
                p0 = p0.pointee();
            if (p0 && type_str(p0, true) == want) { ++bir::callee_census().ifr_selftype; return f; }
        }
        if (std::getenv("LOGOS_BIR_RESOLVE_DEBUG"))
            std::fprintf(stderr, "[bir-resolve] no instance for %s among %zu (%s)\n", needle.c_str(),
                         it->second.size(), std::string(it->second.front().name()).c_str());
        return {};
    }
    lir_view::FunctionView resolve_method(lir_view::EMethodCallView v) const {
        if (auto it = idx_.by_name.find(std::string(v.resolved_symbol())); it != idx_.by_name.end()) {
            ++bir::callee_census().rm_symbol;
            return it->second;
        }
        // The instance for THIS receiver: mono names a method instance
        // `<pkg>.<ConcreteType>__<method>__…`, so the receiver's concrete type
        // picks it out of the same-named candidates.
        if (auto rcv = v.receiver()) {
            TypeRef rt = rcv.type(pool_);
            while (rt && (rt.kind() == LogosType::Kind::Ref || rt.kind() == LogosType::Kind::MutRef ||
                          rt.kind() == LogosType::Kind::Ptr))
                rt = rt.pointee();
            std::string cname;
            if (rt && rt.kind() == LogosType::Kind::Struct) cname = concrete_struct_name(rt);
            else if (rt && rt.kind() == LogosType::Kind::Enum)
                cname = rt.type_args().empty() ? std::string(rt.enum_name()) : Mono::enum_instance_name(rt);
            if (!cname.empty())
                if (auto it = idx_.by_base.find(std::string(v.method())); it != idx_.by_base.end()) {
                    std::string needle = "." + cname + "__" + std::string(v.method()) + "__";
                    for (auto& f : it->second)
                        if (std::string(f.name()).find(needle) != std::string::npos) {
                            ++bir::callee_census().rm_needle; return f;
                        }
                }
        }
        auto agree = [&](const std::vector<lir_view::FunctionView>& cands) -> lir_view::FunctionView {
            if (cands.empty()) return {};
            auto kind0 = [&](lir_view::FunctionView f) {
                auto ps = f.params();
                return ps.empty() ? LogosType::Kind::Void : ps[0].type(pool_).kind();
            };
            auto k = kind0(cands.front());
            for (auto& c : cands) if (kind0(c) != k) return {};
            return cands.front();
        };
        if (!v.resolved_symbol().empty())
            if (auto it = idx_.by_bare.find(std::string(bare_fn_name(v.resolved_symbol())));
                it != idx_.by_bare.end())
                if (auto f = agree(it->second)) { ++bir::callee_census().rm_agree_bare; return f; }
        if (auto it = idx_.by_base.find(std::string(v.method())); it != idx_.by_base.end())
            if (auto f = agree(it->second)) { ++bir::callee_census().rm_agree_base; return f; }
        ++bir::callee_census().rm_miss;
        return {};
    }
    // The receiver form spelled in a mono template key: the first parameter
    // after `__g__` / `__f__` is `ref_…`, `refmut_…`, `pmut_…` / `pconst_…`,
    // or a by-value type. -1 when the key carries no signature.
    static int self_kind_from_key(std::string_view key) {
        size_t at = key.rfind("__g__");
        size_t af = key.rfind("__f__");
        size_t pos = at == std::string_view::npos ? af
                   : af == std::string_view::npos ? at : std::max(at, af);
        if (pos == std::string_view::npos) return -1;
        std::string_view sig = key.substr(pos + 5);
        if (sig.starts_with("refmut_")) return 2;
        if (sig.starts_with("ref_"))    return 1;
        if (sig.starts_with("pmut_") || sig.starts_with("pconst_")) return 3;
        return 0;
    }
    int self_kind(lir_view::EMethodCallView v) const {
        lir_view::FunctionView f = resolve_method(v);
        if (!f && !v.resolved_symbol().empty() && !is_deref_method(v)) {
            int k = self_kind_from_key(v.resolved_symbol());
            if (k >= 0) return k;
        }
        // Sema's overloaded-deref step (`*box`, method autoderef) is a call of
        // Deref::deref(&self) / DerefMut::deref_mut(&mut self) that mono may
        // leave unresolved: the receiver is borrowed, never moved.
        if (!f && is_deref_method(v)) return v.method().ends_with("_mut") ? 2 : 1;
        if (!f) return 0;
        auto ps = f.params();
        if (ps.empty()) return 0;
        TypeRef p0 = ps[0].type(pool_);
        if (!p0) return 0;
        if (p0.kind() == LogosType::Kind::MutRef) return 2;
        if (p0.kind() == LogosType::Kind::Ref) return 1;
        if (p0.kind() == LogosType::Kind::DstRef && !p0.owning_dst()) return p0.mut_ptr() ? 2 : 1;
        if (p0.kind() == LogosType::Kind::Ptr) return 3;   // `self: *mut Self`
        return 0;
    }
    // A non-pointer place handed to a raw-pointer parameter: codegen takes its
    // address. That is neither a move nor a tracked loan.
    bool raw_addr_place(lir_view::ExprRef a, bir::Term& t) {
        if (!a || is_pointer_kind(a.type(pool_))) return false;
        auto p = place_of(a);
        if (!p) return false;
        bir::LocalId tmp = temp({});
        bir::Rvalue rv(bir::Rvalue::K::RawAddr);
        rv.place = *p;
        assign({tmp, {}}, std::move(rv));
        t.args.push_back(read_place({tmp, {}}, {}));
        return true;
    }
    bool raw_addr_arg(lir_view::ExprRef a, TypeRef param, bir::Term& t) {
        if (!a || !param || param.kind() != LogosType::Kind::Ptr) return false;
        if (is_pointer_kind(a.type(pool_))) return false;
        auto p = place_of(a);
        if (!p) return false;
        bir::LocalId tmp = temp(param);
        bir::Rvalue rv(bir::Rvalue::K::RawAddr);
        rv.place = *p;
        assign({tmp, {}}, std::move(rv));
        t.args.push_back(read_place({tmp, {}}, param));
        return true;
    }

    // ── statements ──
    void lower_block_stmts(lir_view::BlockRef b) {
        b.each_stmt([&](lir_view::StmtRef s) { lower_stmt(s); });
    }
    void lower_scoped_block(lir_view::BlockRef b) {
        push_scope();
        if (b) lower_block_stmts(b);
        pop_scope();
    }

    struct Loop {
        std::string   label;
        bir::BlockId  cont, brk;
        size_t        depth;      // scopes_ size at the loop statement
        bir::LocalId  break_slot = bir::kNoLocal;
    };
    std::vector<Loop> loops_;
    const Loop* find_loop(std::string_view label) const {
        for (size_t i = loops_.size(); i-- > 0;)
            if (label.empty() || loops_[i].label == label) return &loops_[i];
        return nullptr;
    }

    // `place = value`: the value is evaluated FIRST, then the place (its
    // derefs and index operands), then the store. `make_dest` builds the place
    // after the value exists.
    template <class MakeDest>
    void store(lir_view::ExprRef value, bool drop_old, MakeDest make_dest) {
        // Into its own temporary, as MIR does: a value that is a PLACE
        // (`s[0] = s[1]`) must be READ before the destination's `&mut s[0]`
        // exists, not merely named before it.
        bir::Operand v = call_operand(value);
        bir::Place dest = make_dest();
        if (drop_old) {
            bir::Stmt d(bir::Stmt::K::Drop);
            d.dest = dest;
            emit(std::move(d));
        }
        bir::Rvalue rv(bir::Rvalue::K::Use);
        rv.ops.push_back(std::move(v));
        assign(std::move(dest), std::move(rv));
    }
    void write_place(bir::Place dest, lir_view::ExprRef value, bool drop_old) {
        if (drop_old) {
            // THE OLD VALUE DIES AFTER THE NEW ONE IS BUILT, NOT BEFORE — the
            // order `store()` above already keeps, and for its reason: the
            // right-hand side may READ the destination. `a = format!("{}{}", a,
            // a);` dropped `a` and then evaluated a format block that borrows
            // it, so the checker reported `a` used while moved or
            // uninitialised. Rust evaluates the rvalue first and the assignment
            // drops what was there afterwards; the imported fixture
            // `self/string-self-append` is exactly that loop and rustc accepts
            // it (MEASURED). The temporary is BIR-only — this lowering feeds
            // the checker, not codegen — so it costs facts, not a copy.
            bir::LocalId t = temp(value ? value.type(pool_) : TypeRef{});
            lower_into(bir::Place{t, {}}, value);
            bir::Stmt d(bir::Stmt::K::Drop);
            d.dest = dest;
            emit(std::move(d));
            bir::Rvalue rv(bir::Rvalue::K::Use);
            bir::Operand o;
            o.k = bir::Operand::K::Move;
            o.place = bir::Place{t, {}};
            rv.ops.push_back(std::move(o));
            assign(std::move(dest), std::move(rv));
            return;
        }
        lower_into(std::move(dest), value);
    }
    bir::Place named_place(std::string_view name) {
        bir::LocalId l = find_local(name, ~0u);
        if (l == bir::kNoLocal) { unsupported("write to unknown name"); l = temp({}); }
        return {l, {}};
    }
    // `recv.field` for a statement that names its receiver, auto-derefing
    // through a pointer-typed receiver.
    bir::Place field_of(bir::Place p, TypeRef& t, std::string_view field) {
        if (is_pointer_kind(t)) {
            bir::Proj d(bir::Proj::K::Deref); d.ty = t.pointee();
            p = p.with(std::move(d)); t = t.pointee();
        }
        bir::Proj f(bir::Proj::K::Field, std::string(field));
        t = field_type(t, field);
        f.ty = t;
        return p.with(std::move(f));
    }

    // ── temporaries die with their statement (Rust reference, "temporary
    // scopes") ──
    // A temporary lives to the end of the enclosing STATEMENT, except the
    // operand of a `&` in an extending position of a `let` initializer, which
    // lives to the end of the block: `let x = &mk();` is fine, `let r =
    // keep(&mk().v);` and `x = &id(3);` leave `r` / `x` dangling (E0716).
    // Statements with their own blocks keep the scopes they already open.
    void lower_stmt(lir_view::StmtRef s) {
        if (!s) return;
        bool simple = false;
        switch (s.kind()) {
            case SCode::Let: case SCode::Assign: case SCode::ExprStmt:
            case SCode::FieldWrite: case SCode::DerefFieldWrite: case SCode::DerefWrite:
                simple = true; break;
            default: break;
        }
        if (!simple) { lower_stmt_(s); return; }
        push_scope();
        bool saved_scoped = stmt_scoped_, saved_ext = extending_;
        stmt_scoped_ = true;
        extending_   = s.kind() == SCode::Let;
        lower_stmt_(s);
        stmt_scoped_ = saved_scoped;
        extending_   = saved_ext;
        pop_scope();
    }
    bool stmt_scoped_ = false;   // the innermost scope is a statement's
    bool extending_   = false;   // lowering a `let` initializer in an extending position
    // A local that outlives the statement being lowered: the `let`'s own
    // binding, and an extended temporary.
    void declare_outer(bir::LocalId l) {
        if (stmt_scoped_ && scopes_.size() >= 2) scopes_[scopes_.size() - 2].locals.push_back(l);
        else declare(l);
    }
    void lower_stmt_(lir_view::StmtRef s) {
        if (uint32_t ln = lir_view::stmt_line(s)) line_ = ln;
        switch (s.kind()) {
            case SCode::Let: {
                lir_view::SLetView v{s};
                // Evaluate the initializer BEFORE the binding exists, so a
                // shadowing `let x = f(x)` reads the outer x.
                TypeRef t = v.type(pool_);
                if (!t && v.value()) t = v.value().type(pool_);
                // `let P = place;` — sema materialises the scrutinee into a
                // temp and reads the bindings off it, but in Rust (and in MIR)
                // the pattern binds straight off the PLACE and the scrutinee is
                // not moved: `let Fd { n } = *self;` is legal because only a
                // Copy field leaves. The temp is that place under another name.
                // Sema says which `let` it is (DESTRUCTURE_TMP); the name is
                // not consulted.
                // Sema's conditional-drop glue (`if __df_N { let __cmfd = h.p;
                // drop(__cmfd); }`, stamped COMPILER_GLUE): drop ELABORATION,
                // which in rustc runs AFTER borrowck and is not part of the
                // program being checked. Read as a user move it is a second
                // move of a maybe-moved path.
                if (v.compiler_glue()) return;
                if (v.destructure_tmp() && v.value())
                    if (auto sp = place_of(v.value())) {
                        aliases_[std::string(v.name())] = *sp;
                        return;
                    }
                bir::LocalId l = new_local(std::string(v.name()), bir::LocalKind::User, t,
                                           v.is_mut() || v.name().starts_with("__") || v.compiler_glue(),
                                           v.var_slot(), line_);
                body_.locals[l].annot_lifetime = v.annot_lifetime();
                if (v.value()) lower_into({l, {}}, v.value());
                declare_outer(l);
                return;
            }
            case SCode::Assign: {
                lir_view::SAssignView v{s};
                write_place(named_place(v.name()), v.value(), v.drop_old());
                return;
            }
            case SCode::Return: {
                lir_view::SReturnView v{s};
                if (v.value()) lower_into({0, {}}, v.value());
                exit_scopes(0, 0);
                terminate(bir::Term(bir::Term::K::Return));
                return;
            }
            case SCode::ExprStmt: {
                lir_view::ExprRef e = lir_view::SExprStmtView{s}.expr();
                if (!e) return;
                if (auto p = place_of(e)) {
                    bir::Stmt r(bir::Stmt::K::Read);
                    r.rv.ops.push_back(as_read(read_place(*p, e.type(pool_))));
                    emit(std::move(r));
                    return;
                }
                bir::LocalId t = temp(e.type(pool_));
                lower_into({t, {}}, e);
                return;
            }
            case SCode::FieldWrite: {
                lir_view::SFieldWriteView v{s};
                bir::Place p = named_place(v.receiver());
                TypeRef t = local_type(p.local);
                write_place(field_of(std::move(p), t, v.field()), v.value(), false);
                return;
            }
            case SCode::TupleWrite: {
                lir_view::STupleWriteView v{s};
                bir::Place p = named_place(v.receiver());
                TypeRef t = local_type(p.local);
                write_place(field_of(std::move(p), t, std::to_string(v.index())), v.value(), false);
                return;
            }
            case SCode::ChainFieldWrite: {
                lir_view::SChainFieldWriteView v{s};
                bir::Place p = named_place(v.receiver());
                TypeRef t = local_type(p.local);
                p = field_of(std::move(p), t, v.mid_field());
                v.each_extra([&](std::string_view f) { p = field_of(std::move(p), t, f); });
                write_place(field_of(std::move(p), t, v.field()), v.value(), false);
                return;
            }
            case SCode::DerefFieldWrite: {
                lir_view::SDerefFieldWriteView v{s};
                bir::Place p = named_place(v.receiver()).with(bir::Proj(bir::Proj::K::Deref));
                write_place(p.with(bir::Proj(bir::Proj::K::Field, std::string(v.field()))), v.value(), false);
                return;
            }
            case SCode::IndexWrite: {
                lir_view::SIndexWriteView v{s};
                store(v.value(), false, [&] {
                    bir::Place p = named_place(v.arr());
                    TypeRef t = local_type(p.local);
                    if (is_pointer_kind(t) || (t && t.kind() == LogosType::Kind::Slice))
                        p = p.with(bir::Proj(bir::Proj::K::Deref));
                    bir::Proj ix(bir::Proj::K::Index);
                    ix.index = operand_local(v.index());
                    return p.with(std::move(ix));
                });
                return;
            }
            case SCode::FieldIndexWrite: {
                lir_view::SFieldIndexWriteView v{s};
                store(v.value(), false, [&] {
                    bir::Place p = named_place(v.receiver());
                    TypeRef t = local_type(p.local);
                    p = field_of(std::move(p), t, v.field());
                    if (is_pointer_kind(t) || (t && t.kind() == LogosType::Kind::Slice))
                        p = p.with(bir::Proj(bir::Proj::K::Deref));
                    bir::Proj ix(bir::Proj::K::Index);
                    ix.index = operand_local(v.index());
                    return p.with(std::move(ix));
                });
                return;
            }
            case SCode::DerefWrite: {
                lir_view::SDerefWriteView v{s};
                store(v.value(), v.drop_old(), [&] {
                    // `place = v` for a field / index place is SPELLED by sema
                    // as `*(&mut place) = v`, with the borrow stamped Desugar.
                    // It is an assignment to the place: no loan, and the place
                    // may be (re)initialised after a move (`h.a = Pay{..}` in
                    // a loop that moved `h.a`). A `&mut` the program wrote
                    // keeps its borrow.
                    if (auto pe = v.ptr(); pe && pe.kind() == ECode::AddrOfTemp) {
                        lir_view::EAddrOfTempView a{pe};
                        if (a.is_mut() && a.origin() == lir_schema::expr::BorrowOrigin::Desugar)
                            if (auto direct = place_of(a.inner())) return *direct;
                    }
                    return place_or_temp(v.ptr()).with(bir::Proj(bir::Proj::K::Deref));
                });
                return;
            }
            case SCode::Drop: {
                lir_view::SDropView v{s};
                bir::LocalId l = find_local(v.var_name(), v.var_slot());
                if (l == bir::kNoLocal) return;
                // A local whose destructor can read a borrow is dropped where
                // Rust drops it, at its scope's exit in reverse declaration
                // order (exit_scopes); sema's placement is its own glue order.
                if (body_.locals[l].kind != bir::LocalKind::Temp &&
                    bir_drop_observes_borrow(body_.locals[l].type, prog_, ts_))
                    return;
                bir::Stmt d(bir::Stmt::K::Drop);
                d.dest = {l, {}};
                emit(std::move(d));
                return;
            }
            case SCode::Block: {
                lir_view::SBlockView v{s};
                if (v.transparent()) { if (auto b = v.body()) lower_block_stmts(b); }
                else lower_scoped_block(v.body());
                return;
            }
            case SCode::If: {
                lir_view::SIfView v{s};
                bir::Operand c = operand(v.cond());
                bir::BlockId bt = new_block(), bf = new_block(), join = new_block();
                branch(std::move(c), {bt, bf});
                cur_ = bt; lower_scoped_block(v.then_block()); goto_(join);
                cur_ = bf; lower_scoped_block(v.else_block()); goto_(join);
                cur_ = join;
                return;
            }
            case SCode::While: {
                lir_view::SWhileView v{s};
                bir::BlockId head = new_block(), body = new_block(), exit = new_block();
                goto_(head);
                cur_ = head;
                bir::Operand c = operand(v.cond());
                branch(std::move(c), {body, exit});
                loops_.push_back({std::string(v.label()), head, exit, scopes_.size()});
                cur_ = body; lower_scoped_block(v.body()); goto_(head);
                loops_.pop_back();
                cur_ = exit;
                return;
            }
            case SCode::Loop: {
                lir_view::SLoopView v{s};
                bir::BlockId body = new_block(), exit = new_block();
                bir::LocalId slot = v.break_slot().empty() ? bir::kNoLocal
                                                           : find_local(v.break_slot(), ~0u);
                goto_(body);
                loops_.push_back({std::string(v.label()), body, exit, scopes_.size(), slot});
                cur_ = body; lower_scoped_block(v.body()); goto_(body);
                loops_.pop_back();
                cur_ = exit;
                return;
            }
            case SCode::For: {
                lir_view::SForView v{s};
                bir::Operand lo = operand(v.lo());
                bir::Operand hi = operand(v.hi());
                push_scope();   // the counter lives across iterations
                TypeRef it = v.lo() ? v.lo().type(pool_) : TypeRef{};
                // As Rust has it: a hidden counter the desugaring steps, and a
                // FRESH `i` bound from it on every iteration, mutable only when
                // declared `for mut i`. With the user's `i` doubling as the
                // counter it had to be `mut`, and `&mut i` on a plain binder
                // went unrefused.
                bir::LocalId ctr = temp(it);
                bir::Rvalue init(bir::Rvalue::K::Use);
                init.ops.push_back(lo);
                assign({ctr, {}}, std::move(init));
                bir::BlockId head = new_block(), body = new_block(), step = new_block(),
                             exit = new_block();
                goto_(head);
                cur_ = head;
                bir::LocalId c = temp({});
                bir::Rvalue cmp(bir::Rvalue::K::Op);
                cmp.what = "<";
                cmp.ops.push_back(read_place({ctr, {}}, it));
                cmp.ops.push_back(as_read(hi));
                assign({c, {}}, std::move(cmp));
                branch(read_place({c, {}}, {}), {body, exit});
                // `continue` leaves through `step` and must end this
                // iteration's `i`, so the loop records the depth BELOW it.
                loops_.push_back({std::string(v.label()), step, exit, scopes_.size()});
                cur_ = body;
                push_scope();
                bir::LocalId var = new_local(std::string(v.var()), bir::LocalKind::User, it,
                                             v.var_mut(), v.var_slot(), line_);
                declare(var);
                bir::Rvalue bind(bir::Rvalue::K::Use);
                bind.ops.push_back(read_place({ctr, {}}, it));
                assign({var, {}}, std::move(bind));
                lower_scoped_block(v.body());
                pop_scope();
                goto_(step);
                loops_.pop_back();
                cur_ = step;
                bir::Rvalue inc(bir::Rvalue::K::Op);
                inc.what = "+1";
                inc.ops.push_back(read_place({ctr, {}}, it));
                assign({ctr, {}}, std::move(inc));
                goto_(head);
                cur_ = exit;
                pop_scope();
                return;
            }
            case SCode::ForEach: {
                lir_view::SForEachView v{s};
                // Iterating an array or slice. The sequence is evaluated once
                // into a place; the index starts at 0 and steps each
                // iteration. Over a borrowed sequence (`&xs`, `&mut xs`, a
                // slice) the variable is a reference to the element, as in
                // Rust's IntoIterator for &[T] / &mut [T]; otherwise the
                // element itself.
                TypeRef st = v.iter().type(pool_);
                bool by_ref = st && (st.kind() == LogosType::Kind::Ref ||
                                     st.kind() == LogosType::Kind::MutRef ||
                                     (st.kind() == LogosType::Kind::Slice && !st.owning_slice()));
                bool mut_ref = st && (st.kind() == LogosType::Kind::MutRef ||
                                      (st.kind() == LogosType::Kind::Slice && st.mut_ptr()));
                bir::Place seq = place_or_temp(v.iter());
                if (by_ref || is_pointer_kind(st)) seq = seq.with(bir::Proj(bir::Proj::K::Deref));
                push_scope();
                // By value, `for d in xs` is `IntoIterator::into_iter(xs)`: the
                // sequence is moved into the iterator ONCE, and each `next()`
                // hands an element out of it. Reading `xs[i]` as a move per
                // iteration moved the whole array again on the second pass
                // ("use of moved '_t5'" over `for d in [a, b]`).
                bool by_value_iter = !by_ref && !is_pointer_kind(st);
                if (by_value_iter) {
                    bir::LocalId it = temp(st);
                    bir::Rvalue into(bir::Rvalue::K::Use);
                    into.ops.push_back(read_place(seq, st));
                    assign({it, {}}, std::move(into));
                    seq = bir::Place{it, {}};
                }
                bir::LocalId ix = temp({});
                assign({ix, {}}, bir::Rvalue(bir::Rvalue::K::Use));   // ix = 0
                bir::BlockId head = new_block(), body = new_block(), step = new_block(),
                             exit = new_block();
                goto_(head);
                cur_ = head;
                bir::Rvalue more(bir::Rvalue::K::Op);
                more.what = "has_next";
                more.ops.push_back(read_place({ix, {}}, {}));
                bir::LocalId c = temp({});
                assign({c, {}}, std::move(more));
                branch(read_place({c, {}}, {}), {body, exit});
                loops_.push_back({"", step, exit, scopes_.size()});
                cur_ = body;
                push_scope();
                TypeRef et = v.elem_type(pool_);
                bir::LocalId var = new_local(std::string(v.var()), bir::LocalKind::User,
                                             by_ref ? TypeRef{} : et, v.var_mut(), v.var_slot(), line_);
                bir::Proj pj(bir::Proj::K::Index);
                pj.index = ix;
                bir::Place elem = seq.with(std::move(pj));
                if (by_ref) {
                    body_.locals[var].synth_ref = true;
                    body_.locals[var].synth_mut = mut_ref;
                    body_.locals[var].synth_pointee = et;
                    assign({var, {}}, ref_of(std::move(elem), mut_ref, false));
                } else {
                    // `next()`: the element comes out of the iterator, which
                    // stays whole (and keeps whatever loans the elements hold).
                    bir::Rvalue get(bir::Rvalue::K::Op);
                    get.what = "next";
                    get.ops.push_back(as_read(read_place(std::move(elem), et)));
                    assign({var, {}}, std::move(get));
                }
                declare(var);
                if (auto b = v.body()) lower_block_stmts(b);
                pop_scope();
                goto_(step);
                loops_.pop_back();
                cur_ = step;
                bir::Rvalue inc(bir::Rvalue::K::Op);
                inc.what = "+1";
                inc.ops.push_back(read_place({ix, {}}, {}));
                assign({ix, {}}, std::move(inc));
                goto_(head);
                cur_ = exit;
                pop_scope();
                return;
            }
            case SCode::Break: {
                lir_view::SBreakView v{s};
                const Loop* lp = find_loop(v.label());
                if (!lp) { unsupported("break outside a loop"); return; }
                Loop l = *lp;
                if (v.value()) {
                    if (l.break_slot != bir::kNoLocal) lower_into({l.break_slot, {}}, v.value());
                    else { bir::LocalId t = temp(v.value().type(pool_)); lower_into({t, {}}, v.value()); }
                }
                exit_scopes(l.depth, 0);
                goto_(l.brk);
                return;
            }
            case SCode::Continue: {
                lir_view::SContinueView v{s};
                const Loop* lp = find_loop(v.label());
                if (!lp) { unsupported("continue outside a loop"); return; }
                Loop l = *lp;
                exit_scopes(l.depth, 0);
                goto_(l.cont);
                return;
            }
            case SCode::Match: {
                lir_view::SMatchView v{s};
                lower_match(v.scrut(), [&](auto&& f) { v.each_arm(f); }, nullptr);
                return;
            }
            case SCode::LetElse: {
                lir_view::SLetElseView v{s};
                bir::Place sp = place_or_temp(v.scrut());
                TypeRef stype = v.scrut().type(pool_);
                bir::BlockId ok = new_block(), no = new_block();
                pattern_test(sp, stype, v.pat());
                branch(read_place(sp, {}), {ok, no});
                cur_ = no;
                lower_scoped_block(v.else_block());
                if (!body_.blocks[cur_].terminated) terminate(bir::Term(bir::Term::K::Unreachable));
                cur_ = ok;
                // The bindings live in the enclosing scope.
                bind_pattern(sp, stype, v.pat(), 0);
                return;
            }
            default:
                unsupported("stmt:" + std::to_string(int(s.kind())));
                return;
        }
    }

    // ── patterns ──
    // The test itself reads the scrutinee (a discriminant read); bindings then
    // copy, move or borrow from projections of the scrutinee place.
    void pattern_test(const bir::Place& p, TypeRef, lir_view::PatRef) {
        bir::Stmt r(bir::Stmt::K::Read);
        bir::Operand o;
        o.k = bir::Operand::K::Copy;
        o.place = p;
        r.rv.ops.push_back(std::move(o));
        emit(std::move(r));
    }
    void bind_to(bir::Place src, TypeRef bind_type, std::string_view name, uint32_t slot,
                 bool is_mut, int ref_mode) {
        if (name.empty() || name == "_") return;
        bir::LocalId l = new_local(std::string(name), bir::LocalKind::User, bind_type, is_mut,
                                   slot, line_);
        if (ref_mode == 1 || ref_mode == 2)
            // In a guard every binding is a SHARED reference, a `ref mut` one
            // included (rustc: the guard sees `&&mut`): the unique borrow is
            // taken when the arm is entered.
            assign({l, {}}, ref_of(std::move(src), ref_mode == 2 && !in_guard_, false));
        else {
            bir::Rvalue rv(bir::Rvalue::K::Use);
            bir::Operand o = read_place(std::move(src), bind_type);
            rv.ops.push_back(in_guard_ ? as_read(std::move(o)) : std::move(o));
            assign({l, {}}, std::move(rv));
        }
        declare(l);
    }
    // `default_ref`: 0 by value, 1 / 2 when matching through a `&` / `&mut`
    // scrutinee without an explicit `&` pattern (default binding modes).
    void bind_pattern(bir::Place p, TypeRef t, lir_view::PatRef pat, int default_ref) {
        if (!pat) return;
        if (is_pointer_kind(t) && t.kind() != LogosType::Kind::Ptr &&
            pat.kind() != PCode::RefPat && pat.kind() != PCode::Wild &&
            pat.kind() != PCode::RefBind && pat.kind() != PCode::At) {
            default_ref = t.kind() == LogosType::Kind::MutRef ? 2 : 1;
            p = p.with(bir::Proj(bir::Proj::K::Deref));
            t = t.pointee();
        }
        switch (pat.kind()) {
            case PCode::Wild: {
                lir_view::PatWildView v{pat};
                bind_to(p, t, v.name(), v.bind_slot(), v.is_mut(), default_ref);
                return;
            }
            case PCode::RefBind: {
                lir_view::PatRefBindView v{pat};
                bind_to(p, v.bind_type(pool_), v.name(), v.bind_slot(), v.is_mut(),
                        v.bind_type(pool_) && v.bind_type(pool_).kind() == LogosType::Kind::MutRef ? 2 : 1);
                return;
            }
            case PCode::RefPat: {
                lir_view::PatRefPatView v{pat};
                bind_pattern(p.with(bir::Proj(bir::Proj::K::Deref)), t ? t.pointee() : TypeRef{}, v.inner(), 0);
                return;
            }
            case PCode::At: {
                lir_view::PatAtView v{pat};
                bind_pattern(p, t, v.sub(), default_ref);
                bind_to(p, v.type(pool_), v.name(), v.bind_slot(), v.is_mut(), default_ref);
                return;
            }
            case PCode::VariantData: {
                lir_view::PatVariantDataView v{pat};
                auto modes = v.bind_ref_modes();
                auto muts  = v.bind_byval_muts();
                auto slots = v.bind_slots();
                std::vector<TypeRef> types;
                v.each_binding_type(pool_, [&](TypeRef bt) { types.push_back(bt); });
                uint32_t i = 0;
                std::string variant(v.variant());
                v.each_binding([&](std::string_view name) {
                    int mode = i < modes.size() ? int(modes[i]) : 0;
                    // 3 / 4 are sema's DEFAULT-MODE shared / mut bindings (RFC
                    // 2005); the place already sits under the scrutinee's
                    // deref here, so they borrow it like a written `ref`.
                    if (mode == 3) mode = 1;
                    else if (mode == 4) mode = 2;
                    if (mode == 0) mode = default_ref;
                    bind_to(p.with(bir::Proj(bir::Proj::K::Field, variant + "#" + std::to_string(i))),
                            i < types.size() ? types[i] : TypeRef{}, name,
                            i < slots.size() ? slots[i] : ~0u,
                            i < muts.size() && muts[i], mode);
                    ++i;
                });
                return;
            }
            case PCode::Tuple: {
                lir_view::PatTupleView v{pat};
                std::vector<std::string_view> names;
                v.each_binding([&](std::string_view n) { names.push_back(n); });
                std::vector<TypeRef> types;
                v.each_binding_type(pool_, [&](TypeRef bt) { types.push_back(bt); });
                auto slots = v.bind_slots();
                std::vector<lir_view::PatRef> subs;
                v.each_sub([&](lir_view::PatRef sp) { subs.push_back(sp); });
                size_t n = std::max(names.size(), subs.size());
                auto elems = t && t.kind() == LogosType::Kind::Tuple ? t.tuple_elems()
                                                                     : std::vector<TypeRef>{};
                for (size_t i = 0; i < n; ++i) {
                    bir::Place ep = p.with(bir::Proj(bir::Proj::K::Field, std::to_string(i)));
                    TypeRef et = i < elems.size() ? elems[i] : (i < types.size() ? types[i] : TypeRef{});
                    if (i < subs.size() && subs[i]) bind_pattern(ep, et, subs[i], default_ref);
                    else if (i < names.size())
                        bind_to(ep, i < types.size() ? types[i] : et, names[i],
                                i < slots.size() ? slots[i] : ~0u, false, default_ref);
                }
                return;
            }
            case PCode::Struct: {
                lir_view::PatStructView v{pat};
                v.each_field([&](lir_view::PatFieldBindingView f) {
                    TypeRef ft = field_type(t, f.field_name());
                    bir::Place fp = p.with(bir::Proj(bir::Proj::K::Field, std::string(f.field_name())));
                    if (auto sub = f.sub()) bind_pattern(fp, ft, sub, default_ref);
                    else bind_to(fp, ft, f.field_name(), f.bind_slot(), f.is_mut(), default_ref);
                });
                return;
            }
            case PCode::Or: {
                bool binds = false;
                lir_view::PatOrView{pat}.each_alt([&](lir_view::PatRef a) {
                    if (pattern_binds(a)) binds = true;
                });
                if (binds) unsupported("pat:Or with bindings");
                return;
            }
            case PCode::Slice: {
                // A FIXED-LENGTH slice pattern binds each element at a constant
                // index, which is MIR's ConstantIndex and the projection BIR
                // already has. `let [a, b] = xs;` never reaches here — sema
                // desugars it to `xs[0]`, `xs[1]` — so what does is a match arm.
                // ⚠ A REST OR A SUFFIX IS REFUSED, NOT APPROXIMATED. `[a, rest
                // @ ..]` needs MIR's Subslice and `[.., b]` needs an index FROM
                // THE END; `bir::Proj` has neither, so the place cannot be
                // spelled and a coarse stand-in (the whole container) is exactly
                // the over-approximation the old checker's own comment calls
                // "coarse". Refused with its own reason so the census counts it.
                lir_view::PatSliceView sv{pat};
                TypeRef et = t ? (t.kind() == LogosType::Kind::Array ? t.elem()
                                                                     : TypeRef{}) : TypeRef{};
                int64_t i = 0;
                sv.each_prefix([&](lir_view::PatRef sub) {
                    bir::Proj ix(bir::Proj::K::Index);
                    ix.const_index = i++;
                    ix.ty = et;
                    if (sub) bind_pattern(p.with(std::move(ix)), et, sub, default_ref);
                });
                // The REST binds a RANGE — MIR's Subslice — and the SUFFIX
                // elements sit at offsets counted FROM THE END, because the
                // length is not known here for a slice. Neither has a single
                // element's move path, and `overlap` cannot separate them from
                // anything, so both are conservative by construction.
                // ⚠ AN ARRAY'S LENGTH IS KNOWN, SO THE OFFSET FROM THE END IS
                // RESOLVED HERE AND THE PRECISE MACHINERY KEEPS WORKING.
                // Leaving it `from_end` would truncate its move path to the
                // CONTAINER (path_of cannot name it), and a WIDER move path is
                // the REFUSING direction: `match arr { [a, .., b] => .. }` was
                // refused as "use of moved or uninitialised 'arr.[0]'" because
                // binding `b` moved the whole array. "Conservative" has a
                // direction, and for moves it is not the wide one.
                const uint64_t sfx = sv.suffix_count();
                const int64_t  alen = (t && t.kind() == LogosType::Kind::Array)
                                          ? int64_t(t.arr_size()) : -1;
                sv.each_rest([&](lir_view::PatRef sub) {
                    if (!sub) return;
                    // BY VALUE OVER A KNOWN LENGTH: the rest moves exactly the
                    // elements it covers, so it is bound to an AGGREGATE of
                    // those places and each one moves by its own path. A single
                    // Subslice path would be a sibling of `a[2]`, not its
                    // parent — the initialisation dataflow builds its tree from
                    // the parent FIELD, not from the `child_path` relation — so
                    // moving the rest would not mark the elements moved. That
                    // was MEASURED: four missed moves.
                    const int64_t lo = i, hi = alen >= 0 ? alen - int64_t(sfx) : -1;
                    if (default_ref == 0 && hi >= lo && sub.kind() == PCode::Wild) {
                        auto wv = lir_view::PatWildView{sub};
                        if (!wv.name().empty() && wv.name() != "_") {
                            bir::LocalId l = new_local(std::string(wv.name()), bir::LocalKind::User,
                                                       t, wv.is_mut(), wv.bind_slot(), line_);
                            bir::Rvalue rv(bir::Rvalue::K::Aggregate);
                            rv.what = "subslice";
                            for (int64_t k = lo; k < hi; ++k) {
                                bir::Proj ix(bir::Proj::K::Index);
                                ix.const_index = k; ix.ty = et;
                                rv.ops.push_back(read_place(p.with(std::move(ix)), et));
                            }
                            assign({l, {}}, std::move(rv));
                            declare(l);
                            return;
                        }
                    }
                    bir::Proj ss(bir::Proj::K::Subslice);
                    ss.const_index = i;
                    ss.ty          = t;
                    if (alen >= 0) { ss.sub_to = alen - int64_t(sfx); }
                    else           { ss.sub_to = int64_t(sfx); ss.from_end = true; }
                    bind_pattern(p.with(std::move(ss)), t, sub, default_ref);
                });
                int64_t j = int64_t(sfx);
                sv.each_suffix([&](lir_view::PatRef sub) {
                    bir::Proj ix(bir::Proj::K::Index);
                    if (alen >= 0) ix.const_index = alen - j;
                    else         { ix.const_index = j; ix.from_end = true; }
                    --j;
                    ix.ty = et;
                    if (sub) bind_pattern(p.with(std::move(ix)), et, sub, default_ref);
                });
                return;
            }
            default:
                return;   // Variant, Int, Bool, Range: no bindings
        }
    }
    // The places a pattern TESTS (a discriminant or a value compared), as
    // opposed to the ones it only binds or ignores.
    void tested_places(bir::Place p, TypeRef t, lir_view::PatRef pat, std::vector<bir::Place>& out,
                       int depth = 0) {
        if (!pat || depth > 8) return;
        if (is_pointer_kind(t) && t.kind() != LogosType::Kind::Ptr && pat.kind() != PCode::RefPat &&
            pat.kind() != PCode::Wild && pat.kind() != PCode::RefBind && pat.kind() != PCode::At) {
            p = p.with(bir::Proj(bir::Proj::K::Deref));
            t = t.pointee();
        }
        switch (pat.kind()) {
            case PCode::Wild: case PCode::RefBind: return;
            case PCode::RefPat:
                tested_places(p.with(bir::Proj(bir::Proj::K::Deref)), t ? t.pointee() : TypeRef{},
                              lir_view::PatRefPatView{pat}.inner(), out, depth + 1);
                return;
            case PCode::At:
                tested_places(p, t, lir_view::PatAtView{pat}.sub(), out, depth + 1);
                return;
            case PCode::Tuple: {
                lir_view::PatTupleView v{pat};
                auto elems = t && t.kind() == LogosType::Kind::Tuple ? t.tuple_elems() : std::vector<TypeRef>{};
                size_t i = 0;
                v.each_sub([&](lir_view::PatRef sp2) {
                    if (sp2) tested_places(p.with(bir::Proj(bir::Proj::K::Field, std::to_string(i))),
                                           i < elems.size() ? elems[i] : TypeRef{}, sp2, out, depth + 1);
                    ++i;
                });
                return;
            }
            case PCode::Struct: {
                lir_view::PatStructView v{pat};
                v.each_field([&](lir_view::PatFieldBindingView f) {
                    if (auto sub = f.sub())
                        tested_places(p.with(bir::Proj(bir::Proj::K::Field, std::string(f.field_name()))),
                                      field_type(t, f.field_name()), sub, out, depth + 1);
                });
                return;
            }
            case PCode::Or:
                lir_view::PatOrView{pat}.each_alt([&](lir_view::PatRef a) { tested_places(p, t, a, out, depth + 1); });
                return;
            case PCode::Slice: {
                // ⚠ AN ARRAY'S LENGTH IS STATIC, SO A SHAPE TEST READS NOTHING.
                // Falling to the default below marked the WHOLE array examined,
                // and a second `match arr` after a first had moved an element
                // out was then refused as a use of a partially moved value —
                // rustc accepts it (twin measured), and so does the old checker.
                // Only the sub-patterns test anything. A SLICE is different: its
                // length is a runtime word, so the shape test does read it, and
                // that falls through to the default.
                if (!t || t.kind() != LogosType::Kind::Array) break;
                lir_view::PatSliceView sv{pat};
                TypeRef et = t.elem();
                int64_t i = 0;
                sv.each_prefix([&](lir_view::PatRef sub) {
                    bir::Proj ix(bir::Proj::K::Index);
                    ix.const_index = i++; ix.ty = et;
                    tested_places(p.with(std::move(ix)), et, sub, out, depth + 1);
                });
                int64_t j = int64_t(sv.suffix_count());
                sv.each_suffix([&](lir_view::PatRef sub) {
                    bir::Proj ix(bir::Proj::K::Index);
                    ix.const_index = int64_t(t.arr_size()) - j--; ix.ty = et;
                    tested_places(p.with(std::move(ix)), et, sub, out, depth + 1);
                });
                return;
            }
            default:
                out.push_back(p);   // a variant, a literal, a range, a slice shape: the place is examined
                return;
        }
    }
    bool pattern_binds(lir_view::PatRef pat) const {
        if (!pat) return false;
        switch (pat.kind()) {
            case PCode::Wild: { auto n = lir_view::PatWildView{pat}.name(); return !n.empty() && n != "_"; }
            case PCode::RefBind: case PCode::At: case PCode::VariantData:
            case PCode::Tuple: case PCode::Struct: case PCode::Slice:
                return true;   // conservatively
            case PCode::RefPat: return pattern_binds(lir_view::PatRefPatView{pat}.inner());
            case PCode::Or: {
                bool b = false;
                lir_view::PatOrView{pat}.each_alt([&](lir_view::PatRef a) { b = b || pattern_binds(a); });
                return b;
            }
            default: return false;
        }
    }

    template <class EachArm>
    void lower_match(lir_view::ExprRef scrut, EachArm each_arm, const bir::Place* dest) {
        bir::Place sp = place_or_temp(scrut);
        TypeRef stype = scrut.type(pool_);
        std::vector<lir_view::EMatchArmRef> arms;
        each_arm([&](lir_view::EMatchArmRef a) { arms.push_back(a); });
        bir::BlockId join = new_block();
        // ⚠ AN ARRAY MATCHED ONLY BY SHAPE READS NOTHING. The length is static,
        // so `match arr { [.., z] => .. }` examines no storage — it only moves
        // what it binds. Reading the whole scrutinee here (and again in the
        // branch below) refused a SECOND `match arr` after a first had moved an
        // element out, as a use of a partially moved value; rustc accepts that
        // program (twin measured) and so does the old checker. This is the same
        // argument the guard's fake borrows below already make, in the comment
        // that says borrowing the whole scrutinee "refused both" — made once
        // for guards and not for the read.
        bool shape_only = stype && stype.kind() == LogosType::Kind::Array && !arms.empty();
        if (shape_only)
            for (auto& a2 : arms) {
                auto k = a2.pat() ? a2.pat().kind() : PCode::Wild;
                if (k != PCode::Slice && k != PCode::Wild) { shape_only = false; break; }
            }
        if (!shape_only) pattern_test(sp, stype, {});
        std::vector<bir::BlockId> arm_blocks;
        for (size_t i = 0; i < arms.size(); ++i) arm_blocks.push_back(new_block());
        // "No arm matched": a match is exhaustive, so this edge is never
        // taken. It must not reach `join`, where the result is assumed set.
        bir::BlockId nomatch = new_block();
        body_.blocks[nomatch].term = bir::Term(bir::Term::K::Unreachable);
        body_.blocks[nomatch].terminated = true;
        arm_blocks.push_back(nomatch);
        branch(shape_only ? bir::Operand{} : read_place(sp, {}), arm_blocks);
        for (size_t i = 0; i < arms.size(); ++i) {
            cur_ = arm_blocks[i];
            push_scope();
            if (auto g = arms[i].guard()) {
                // As MIR has it: the guard sees the arm's bindings BY
                // REFERENCE, and the scrutinee is moved out of only once the
                // guard has passed. Binding by move first left the scrutinee
                // moved on the guard's FALSE edge (the next arm then read a
                // moved value), and sema's nested-variant synthesis — an inner
                // `match` inside the guard, repeated in the body — moved the
                // same payload twice.
                ++in_guard_;
                bind_pattern(sp, stype, arms[i].pat(), 0);
                // E0510: the guard may not change what is being matched. MIR
                // holds a FAKE shared borrow of the scrutinee across the guard;
                // here it is a real one that is read once the guard is done, so
                // `3 if { x = 9; false } => ..` invalidates a live loan.
                // ONLY the places the match TESTS (rustc's fake borrows are of
                // the examined places): `_ if { x = 9; .. }` tests nothing, and
                // `W { e: E::A, n: _ } if { w.n = 5; .. }` tests `w.e`, not
                // `w.n`. Borrowing the whole scrutinee refused both.
                std::vector<bir::Place> tested;
                for (auto& a2 : arms) tested_places(sp, stype, a2.pat(), tested);
                std::vector<bir::LocalId> fakes;
                for (auto& tp : tested) {
                    bir::LocalId fake = synth_ref_temp({}, false);
                    assign({fake, {}}, ref_of(tp, false, false));
                    fakes.push_back(fake);
                }
                bir::Operand c = operand(g);
                for (bir::LocalId fake : fakes) {
                    bir::Stmt rd(bir::Stmt::K::Read);
                    bir::Operand fo; fo.k = bir::Operand::K::Copy; fo.place = {fake, {}};
                    rd.rv.ops.push_back(std::move(fo));
                    emit(std::move(rd));
                }
                --in_guard_;
                bir::BlockId yes = new_block();
                branch(std::move(c), {yes, i + 1 < arms.size() ? arm_blocks[i + 1] : nomatch});
                cur_ = yes;
            }
            bind_pattern(sp, stype, arms[i].pat(), 0);
            if (dest && arms[i].value()) lower_into(*dest, arms[i].value());
            if (auto b = arms[i].body()) lower_block_stmts(b);
            pop_scope();
            if (!body_.blocks[cur_].terminated) goto_(join);
        }
        cur_ = join;
    }

    const lir::LProgram&  prog_;
    const TypeSets&       ts_;
    const FnIndex&        idx_;
    const FlowSummaryMap* flows_;
    std::vector<lir_view::ExprRef> closures_;   // ADR 0029 S5b: bodies to lower next
    const TypePoolImpl*   pool_;
    bool                  mut_deref_ctx_ = false;
    int                   in_guard_ = 0;   // lowering a match guard: bindings are reads
    bir::Body             body_;
    std::unordered_set<std::string> copy_tvs_;   // #434: the template's `T: Copy` params
    std::unordered_map<std::string, bir::Place> aliases_;   // destructure temps -> the scrutinee place
    bir::BlockId          cur_ = 0;
    uint32_t              line_ = 0;
};

// ── Closure requirements ─────────────────────────────────────────────────────
//
// A CLOSURE BODY CANNOT ANSWER ITS OWN ESCAPE QUESTION. In
// `{ let y = 22i64; let c = || { p = &y; }; c(); }` the body learns only that a
// loan of capture `y` reaches capture `p`; whether that is legal is the
// PARENT's question, and the parent's alone — `y` dies with the block and `p`
// is read after it. Both captures are free regions of the parent, so a
// body-local check either admits every such closure or refuses every one.
//
// rustc propagates the constraint outward as a closure region requirement.
// Here a requirement is a pair of CAPTURE INDICES, "a loan of capture a reaches
// capture b", and the parent turns it back into a subset between the two
// operands of the construction, where its own liveness then decides. Keyed by
// closure id, because an inner closure's requirement is an outer one's fact.
using ClosureReqs = std::map<std::string, std::set<std::pair<uint32_t, uint32_t>>>;

// ── Facts ────────────────────────────────────────────────────────────────────
//
// BIR -> the input relations of dl/rules/logos_bc.dl (Polonius + E0384 +
// the local mutability errors). Points: every statement and terminator has a
// Start and a Mid point; reads, writes, loan issue and invalidation happen at
// Mid. Origins come from a pre-order walk of each local's type: one origin per
// reference layer, per struct lifetime argument, and one implicit origin for a
// struct that holds references without declaring a lifetime.

class BirFacts {
public:
    struct Loan {
        bir::Place place;
        bool       mut_ = false;
        uint32_t   issue = 0;          // Mid point id
        uint32_t   activation = ~0u;   // two-phase: the Call's Mid point
        uint32_t   origin = 0;
        uint32_t   line = 0;
    };
    struct Out {
        std::vector<std::pair<std::string, std::vector<std::string>>> rows;   // rel, row
    };
    // A universal region's PARAM/RETURN local and the lifetime its declared
    // type wrote there ("" when elided). A subset error between two of them is
    // a sentence about those two, and rustc has two of them — see the report.
    struct Universal { bir::LocalId local; std::string label; };

    BirFacts(const bir::Body& b, const lir::LProgram& prog, const TypeSets& ts,
             const ClosureReqs* creqs = nullptr)
        : b_(b), prog_(prog), ts_(ts), pool_(prog.type_pool.impl()), creqs_(creqs) {}

    // Fills `db` (whose program is logos_bc.dl) and keeps the loan table for
    // explanations.
    void emit(dl::Database& db, dl::Symbols& syms);

    const std::vector<Loan>& loans() const { return loans_; }
    uint32_t n_origins() const { return n_origins_; }
    size_t   n_points()  const { return point_lines_.size(); }
    // WHY this loan died at this point, as a code the message site can switch
    // on without reaching into the access record: 0 unknown, 1 a new SHARED
    // borrow, 2 a new MUTABLE borrow, 3 the place went out of scope, 4 an
    // assignment, 5 a move, 6 a plain read. See why_ and invalidations().
    bool at_return(uint32_t point) const { return return_points_.count(point) != 0; }
    int invalidation_kind(uint32_t loan, uint32_t point) const {
        auto it = why_.find({loan, point});
        if (it == why_.end()) return 0;
        const auto& a = it->second;
        if (a.self_loan >= 0 && size_t(a.self_loan) < loans_.size())
            return loans_[size_t(a.self_loan)].mut_ ? 2 : 1;
        switch (a.kind) {
            case Access::StorageDead:   return 3;
            case Access::WriteShallow:  return 4;
            case Access::WriteDeep:     return 5;
            case Access::Read:          return 6;
        }
        return 0;
    }
    std::string point_name(uint32_t p) const { return "p" + std::to_string(p); }
    uint32_t point_line(uint32_t p) const { return p < point_lines_.size() ? point_lines_[p] : 0; }
    // rustc renders a place in source form: a Deref followed by a field or an
    // index vanishes (`(*r).a` is `r.a`), and only a trailing one is written,
    // as a `*` prefix with no parentheses (`*r`, `**p`, `*child.raw`).
    std::string place_str(const bir::Place& p) const {
        std::string s = b_.locals[p.local].name;
        unsigned pending_deref = 0;
        for (auto& pj : p.proj) {
            if (pj.k == bir::Proj::K::Deref) { ++pending_deref; continue; }
            pending_deref = 0;
            if (pj.k == bir::Proj::K::Field) s += "." + pj.field;
            else if (pj.k == bir::Proj::K::Subslice)
                s += pj.from_end ? "[" + std::to_string(pj.const_index) + "..-" +
                                   std::to_string(pj.sub_to) + "]"
                                 : "[" + std::to_string(pj.const_index) + "..]";
            else if (pj.k == bir::Proj::K::Index && pj.from_end && pj.const_index >= 0)
                s += "[-" + std::to_string(pj.const_index) + "]";
            else s += "[_]";
        }
        return std::string(pending_deref, '*') + s;
    }
    std::string path_str(uint32_t id) const { return paths_[id].first; }

    // Which PARAM or RETURN local a universal region belongs to. A subset error
    // between two of them is a claim about those two locals, and the reader
    // needs their names, not the region numbers.
    const Universal* placeholder_owner(uint32_t origin) const {
        auto it = placeholder_owner_.find(origin);
        return it == placeholder_owner_.end() ? nullptr : &it->second;
    }
private:
    // ── origins ──
    struct OList { std::vector<uint32_t> os; std::vector<bool> inv; bool exact = true; };
    std::map<uint32_t, Universal> placeholder_owner_;
    const ClosureReqs* creqs_ = nullptr;
    uint32_t fresh_origin() { return n_origins_++; }
    void walk_type(TypeRef t, bool invariant, OList& out, int depth = 0) {
        if (!t || depth > 16) return;
        switch (t.kind()) {
            case LogosType::Kind::Ref: case LogosType::Kind::MutRef: {
                out.os.push_back(fresh_origin()); out.inv.push_back(invariant);
                walk_type(t.pointee(), invariant || t.kind() == LogosType::Kind::MutRef, out, depth + 1);
                return;
            }
            case LogosType::Kind::Ptr: {
                // A RAW POINTER HAS NO REGION OF ITS OWN — AND WHAT IT POINTS AT
                // MAY HAVE SEVERAL. `bir_type_carries_loans` already says a
                // `*mut &'a T` carries a loan; this walker minted NO origin for
                // it and the two predicates disagreed about the same type, so a
                // struct field of that shape was loan-free here and
                // loan-carrying there. MEASURED as the one variable in a
                // four-program ladder: with `q: &'a &'a i64` the escape of a
                // local through a struct and a call is refused, and changing
                // that ONE FIELD to `q: *mut &'a i64` let it out — rustc
                // refuses both (E0515).
                //
                // No origin for the pointer itself: `*mut T` is `*mut (T +
                // 'static)`-shaped in Rust, the pointer is not a loan. The
                // pointee kinds skipped here are the ones the carries-loans Ptr
                // arm skips, for its own stated reason (`*mut dyn Trait` is
                // `+ 'static`), so the two arms stay one decision.
                TypeRef pt = t.pointee();
                if (!pt) return;
                auto pk = pt.kind();
                if (pk == LogosType::Kind::TraitObject || pk == LogosType::Kind::UnsizedDyn ||
                    pk == LogosType::Kind::Slice || pk == LogosType::Kind::UnsizedSlice ||
                    pk == LogosType::Kind::DstRef)
                    return;
                walk_type(pt, invariant, out, depth + 1);
                return;
            }
            case LogosType::Kind::Slice:
                if (!t.owning_slice() && !t.raw_fat()) { out.os.push_back(fresh_origin()); out.inv.push_back(invariant); }
                if (!t.raw_fat()) walk_type(t.elem(), invariant, out, depth + 1);
                return;
            case LogosType::Kind::TraitObject:
                // An OWNING trait object (`Box<dyn Get>`) erases what it holds,
                // loans included: `Box::new(c.mk())` keeps `c` borrowed for as
                // long as the box is used. One origin stands for whatever was
                // erased, as it does for a closure's captures. (Rust asks for
                // `+ 'static` here by default and refuses outright; holding the
                // loan is the weaker claim and refuses the same programs that
                // go on to use both.)
                if (!t.raw_fat()) { out.os.push_back(fresh_origin()); out.inv.push_back(invariant); }
                // A BORROWED one is `&'a (dyn Tr + 'b)`: the borrow's region and
                // the bound on what was erased are two regions. With one, `let
                // d: &dyn Tr = &w;` lost the loans `w` itself holds (`w: B`
                // borrowing `c`), and `c.bump(); d.g();` went unrefused.
                if (!t.raw_fat() && !t.owning_trait_object()) { out.os.push_back(fresh_origin()); out.inv.push_back(invariant); }
                return;
            case LogosType::Kind::DstRef:
                if (!t.owning_dst() && !t.raw_fat()) { out.os.push_back(fresh_origin()); out.inv.push_back(invariant); }
                return;
            case LogosType::Kind::Array:
                walk_type(t.elem(), invariant, out, depth + 1);
                return;
            case LogosType::Kind::Tuple:
                for (auto& e : t.tuple_elems()) walk_type(e, invariant, out, depth + 1);
                return;
            case LogosType::Kind::Struct: case LogosType::Kind::Enum: {
                size_t before = out.os.size();
                for (size_t i = 0, nl = struct_lifetime_count(t); i < nl; ++i) {
                    out.os.push_back(fresh_origin()); out.inv.push_back(invariant);
                }
                for (auto& a : t.type_args()) walk_type(a, invariant, out, depth + 1);
                if (out.os.size() == before && bir_type_carries_loans(t, prog_, ts_)) {
                    // A struct that holds references without declaring a
                    // lifetime parameter has HIDDEN ones. Rust would make the
                    // author write `H<'a, 'b> { r: &'a mut Vec<B<'b>> }`; the
                    // regions are laid out the same way here, field by field,
                    // so the borrow's own region ('a) and the invariant region
                    // of what it points at ('b) stay two origins. One origin
                    // for both either lost the write-through (one-way subset)
                    // or made `vs` hold the loan of itself (two-way).
                    if (auto ef = expanded_fields(t)) {
                        visiting_.insert(layout_key(t));
                        for (auto& [fn, ft] : *ef) walk_type(ft, invariant, out, depth + 1);
                        visiting_.erase(layout_key(t));
                    }
                    if (out.os.size() == before) {
                        out.os.push_back(fresh_origin()); out.inv.push_back(invariant);
                    }
                }
                return;
            }
            case LogosType::Kind::Closure: {
                // ADR 0029 S5a — THE CAPTURES ARE VISIBLE IN THE TYPE NOW, and
                // the comment that stood here ("one origin stands for whatever
                // the closure may hold") stopped being true the moment S2
                // landed CLOSURE_CAPTURES. A literal's type carries its env, by
                // capture order, with a by-reference capture already spelled
                // `&[mut] T` — so each one walks like a struct field and gets
                // its own origin, with a `&mut` capture invariant in what it
                // points at exactly as the Ref arm above decides it.
                //
                // THE ERASED FORMS KEEP THE ONE-ORIGIN STAND-IN, and there it is
                // the right answer, not a stand-in for a better one: a written
                // `|T| -> R` or a `dyn Fn*` states no env, so one origin stands
                // for whatever was erased — the same claim the TraitObject arm
                // above makes, for the same reason. It is the literal id, not
                // an empty capture list, that separates the two: a literal that
                // captures NOTHING holds no loans and needs no origin at all.
                if (t.closure_literal_id()) {
                    for (auto c : t.closure_captures())
                        walk_type(c, invariant, out, depth + 1);
                    return;
                }
                out.os.push_back(fresh_origin()); out.inv.push_back(invariant);
                return;
            }
            default:
                return;
        }
    }
    // The fields whose regions stand in for a struct's hidden lifetime
    // parameters (see walk_type). None for an enum, a struct with declared
    // lifetimes or region-carrying type arguments, a struct already being laid
    // out (a recursive type keeps its single origin), or one with no definition.
    // How many lifetime parameters a struct type has. Mono drops lifetime
    // ARGUMENTS from an instantiated type (`SliceIter<i64>` for the declared
    // `SliceIter<'a, T>`), so an empty argument list says nothing: the
    // DEFINITION's parameters do. Without this such a value had no origin at
    // all when its fields are raw pointers, and `next(&mut self) -> Option<&'a
    // T>` had no `'a` source to take its result from.
    size_t struct_lifetime_count(TypeRef t) const {
        size_t n = t.lifetime_args().size();
        if (n || t.kind() != LogosType::Kind::Struct) return n;
        std::string want = concrete_struct_name(t);
        if (auto it = ts_.struct_by_name.find(want); it != ts_.struct_by_name.end())
            return it->second.lifetime_params().size();
        if (auto it = ts_.spec_by_name.find(want); it != ts_.spec_by_name.end())
            return it->second.lifetime_params().size();
        return 0;
    }
    std::set<std::string> visiting_;
    static std::string layout_key(TypeRef t) {
        return t.kind() == LogosType::Kind::Enum ? "enum " + Mono::enum_instance_name(t)
                                                 : concrete_struct_name(t);
    }
    std::optional<std::vector<std::pair<std::string, TypeRef>>> expanded_fields(TypeRef t) {
        if (!t || struct_lifetime_count(t)) return std::nullopt;
        if (t.kind() != LogosType::Kind::Struct && t.kind() != LogosType::Kind::Enum) return std::nullopt;
        for (auto& a : t.type_args()) { size_t n = 0; count_only(a, n); if (n) return std::nullopt; }
        if (visiting_.count(layout_key(t))) return std::nullopt;
        // An enum's hidden regions are its variants' payloads, one after
        // another (`Variant#i`), exactly as a struct's are its fields.
        return t.kind() == LogosType::Kind::Struct ? bir_struct_named_fields(t, prog_, ts_)
                                                   : bir_enum_named_payloads(t, prog_, ts_);
    }
    // [offset, count) of `field`'s origins inside the list walk_type gives `t`.
    std::optional<std::pair<size_t, size_t>> field_span(TypeRef t, std::string_view field, TypeRef* ftype) {
        auto ef = expanded_fields(t);
        if (!ef) return std::nullopt;
        visiting_.insert(layout_key(t));
        size_t off = 0;
        std::optional<std::pair<size_t, size_t>> out;
        for (auto& [fn, ft] : *ef) {
            size_t n = 0; count_only(ft, n);
            if (fn == field) { out = std::make_pair(off, n); if (ftype) *ftype = ft; break; }
            off += n;
        }
        visiting_.erase(layout_key(t));
        return out;
    }
    // ── exact field projection for a type that is NOT laid out from its fields
    // (it declares lifetimes, or its type arguments carry regions) ──
    // walk_type gives such a type [its lifetime params] ++ [each type
    // argument's regions]. A field's regions are found in that list by what
    // the field's type says: a written lifetime is the definition's lifetime
    // parameter of that name; a sub-type that IS one of the type arguments
    // (modulo regions) is that argument's slice. Anything the definition does
    // not pin down (an ambiguous argument, an enum's lifetime, a trait object)
    // gives no answer, and the caller keeps the whole list, inexactly.
    struct ParentLayout {
        std::vector<std::string> lifetimes;                  // names, in order
        std::vector<std::pair<TypeRef, std::pair<size_t, size_t>>> args;   // (arg, [off, n))
    };
    std::optional<ParentLayout> parent_layout(TypeRef t) {
        if (!t || (t.kind() != LogosType::Kind::Struct && t.kind() != LogosType::Kind::Enum)) return std::nullopt;
        ParentLayout pl;
        size_t nl = struct_lifetime_count(t);
        if (nl) {
            if (t.kind() != LogosType::Kind::Struct) return std::nullopt;
            std::string want = concrete_struct_name(t);
            auto names = [&](lir_view::StructView sd) { for (auto n : sd.lifetime_params()) { std::string q(n); if (!q.empty() && q[0] != '\'') q = "'" + q; pl.lifetimes.push_back(q); } };
            if (auto it = ts_.struct_by_name.find(want); it != ts_.struct_by_name.end()) names(it->second);
            else if (auto it2 = ts_.spec_by_name.find(want); it2 != ts_.spec_by_name.end()) names(it2->second);
            if (pl.lifetimes.size() != nl) return std::nullopt;
        }
        size_t off = nl;
        for (auto& a : t.type_args()) {
            size_t n = 0; count_only(a, n);
            pl.args.emplace_back(a, std::make_pair(off, n));
            off += n;
        }
        return pl;
    }
    bool map_field_type(const ParentLayout& pl, TypeRef ft, std::vector<size_t>& out, int depth = 0) {
        if (!ft || depth > 12) return !ft;
        size_t need = 0; count_only(ft, need);
        if (!need) return true;
        // Is this sub-type one of the type arguments?
        const std::pair<size_t, size_t>* hit = nullptr;
        for (auto& [a, span] : pl.args)
            if (span.second == need && same_type_modulo_regions(ft, a)) {
                if (hit) return false;   // two arguments of one type: which one is not recoverable
                hit = &span;
            }
        if (hit) { for (size_t k = 0; k < hit->second; ++k) out.push_back(hit->first + k); return true; }
        auto by_name = [&](std::string lt) -> bool {
            if (!lt.empty() && lt[0] != '\'') lt = "'" + lt;
            for (size_t i = 0; i < pl.lifetimes.size(); ++i)
                if (pl.lifetimes[i] == lt) { out.push_back(i); return true; }
            return false;
        };
        switch (ft.kind()) {
            case LogosType::Kind::Ref: case LogosType::Kind::MutRef:
                return by_name(std::string(ft.lifetime())) && map_field_type(pl, ft.pointee(), out, depth + 1);
            case LogosType::Kind::Slice:
                if (ft.raw_fat()) return true;
                if (!ft.owning_slice() && !by_name(std::string(ft.lifetime()))) return false;
                return map_field_type(pl, ft.elem(), out, depth + 1);
            case LogosType::Kind::Array:
                return map_field_type(pl, ft.elem(), out, depth + 1);
            case LogosType::Kind::Tuple:
                for (auto& e : ft.tuple_elems()) if (!map_field_type(pl, e, out, depth + 1)) return false;
                return true;
            case LogosType::Kind::Struct: case LogosType::Kind::Enum: {
                auto lts = ft.lifetime_args();
                if (lts.size() != struct_lifetime_count(ft)) return false;
                for (auto& lt : lts) if (!by_name(std::string(lt))) return false;
                size_t before = out.size();
                for (auto& a : ft.type_args()) if (!map_field_type(pl, a, out, depth + 1)) return false;
                return out.size() - before + lts.size() == need;   // no hidden-region layout here
            }
            default:
                return false;
        }
    }
    // The indices, in `t`'s own region list, of the regions of field `field`.
    std::optional<std::vector<size_t>> field_slots(TypeRef t, std::string_view field, TypeRef* ftype) {
        if (auto span = field_span(t, field, ftype)) {
            std::vector<size_t> v;
            for (size_t k = 0; k < span->second; ++k) v.push_back(span->first + k);
            return v;
        }
        auto pl = parent_layout(t);
        auto fields = !pl ? std::nullopt
                    : t.kind() == LogosType::Kind::Struct ? bir_struct_named_fields(t, prog_, ts_)
                                                          : bir_enum_named_payloads(t, prog_, ts_);
        if (!fields) {
            if (std::getenv("LOGOS_BIR_LABELS") && t && (t.kind() == LogosType::Kind::Struct || t.kind() == LogosType::Kind::Enum))
            {
                std::fprintf(stderr, "[field-nodef] %s . %.*s layout=%d want=%s\n", type_str(t).c_str(), (int)field.size(),
                             field.data(), (int)bool(pl),
                             t.kind() == LogosType::Kind::Enum ? Mono::enum_instance_name(t).c_str() : concrete_struct_name(t).c_str());
            }
            return std::nullopt;
        }
        for (auto& [fn, ft] : *fields) {
            if (fn != field) continue;
            std::vector<size_t> v;
            if (!map_field_type(*pl, ft, v)) {
                if (std::getenv("LOGOS_BIR_LABELS"))
                    std::fprintf(stderr, "[field-miss] %s . %.*s : %s\n", type_str(t).c_str(), (int)field.size(),
                                 field.data(), type_str(ft).c_str());
                return std::nullopt;
            }
            size_t need = 0; count_only(ft, need);
            if (v.size() != need) return std::nullopt;
            if (ftype) *ftype = ft;
            return v;
        }
        return std::nullopt;
    }
    const OList& local_origins(bir::LocalId l) {
        auto it = local_os_.find(l);
        if (it != local_os_.end()) return it->second;
        OList o;
        if (b_.locals[l].synth_ref) {
            o.os.push_back(fresh_origin()); o.inv.push_back(false);
            // What a synthesised `&mut` points at is invariant, exactly as
            // behind a typed `&mut T`; it was covariant, so a `v.push(x)`
            // through the autoref never tied `x`'s region to `v`'s.
            walk_type(b_.locals[l].synth_pointee, b_.locals[l].synth_mut, o);
        } else {
            walk_type(b_.locals[l].type, false, o);
        }
        return local_os_.emplace(l, std::move(o)).first->second;
    }
    // Origins of the value at `p`: exact through derefs of references,
    // tuple fields and array elements; the whole root list otherwise.
    OList place_origins(const bir::Place& p) {
        OList cur = local_origins(p.local);
        TypeRef t = b_.locals[p.local].type;
        size_t start = 0;
        if (b_.locals[p.local].synth_ref && !p.proj.empty() && p.proj[0].k == bir::Proj::K::Deref) {
            cur.os.erase(cur.os.begin()); cur.inv.erase(cur.inv.begin());
            t = b_.locals[p.local].synth_pointee;
            start = 1;
        }
        for (size_t pi = start; pi < p.proj.size(); ++pi) {
            const auto& pj = p.proj[pi];
            if (!cur.exact || !t) { cur.exact = false; continue; }
            if (pj.k == bir::Proj::K::Deref) {
                auto k = t.kind();
                if (k == LogosType::Kind::Ref || k == LogosType::Kind::MutRef) {
                    cur.os.erase(cur.os.begin()); cur.inv.erase(cur.inv.begin());
                    t = t.pointee();
                    // The PLACE `*r` has the pointee's own variance. Invariance
                    // is a fact about relating two `&mut T`s; a value stored
                    // into `*x` (`*x = *y` with `x: &mut &'a i64`, `y: &mut &'b
                    // i64`, `'b: 'a`) flows one way, and carrying the `&mut`'s
                    // flag onto the place tied `'a` back into `'b`.
                    OList own;
                    uint32_t saved = n_origins_;
                    walk_type(t, false, own);
                    n_origins_ = saved;
                    if (own.inv.size() == cur.inv.size()) cur.inv = own.inv;
                } else if (k == LogosType::Kind::Slice && !t.owning_slice()) {
                    cur.os.erase(cur.os.begin()); cur.inv.erase(cur.inv.begin());
                    t = TypeRef{};   // the unsized [T]: its element list follows
                } else if (is_std_box(t) && !t.type_args().empty()) {
                    t = t.type_args()[0];   // Box<T>'s regions ARE T's
                } else if (k == LogosType::Kind::Ptr) {
                    cur = {}; cur.exact = true; t = t.pointee();   // raw: untracked
                    OList fresh; walk_type(t, false, fresh);         // unconstrained
                    cur = fresh;
                } else { cur.exact = false; }
            } else if (pj.k == bir::Proj::K::Index) {
                if (t.kind() == LogosType::Kind::Array) t = t.elem();
                else cur.exact = false;
            } else {   // Field
                if (t.kind() == LogosType::Kind::Tuple) {
                    auto elems = t.tuple_elems();
                    uint32_t i = 0;
                    bool num = !pj.field.empty();
                    for (char c : pj.field) { if (c < '0' || c > '9') num = false; else i = i * 10 + uint32_t(c - '0'); }
                    if (!num || i >= elems.size()) { cur.exact = false; continue; }
                    size_t off = 0;
                    for (uint32_t k = 0; k < i; ++k) { OList tmp; walk_type(elems[k], false, tmp); off += tmp.os.size(); n_origins_ -= uint32_t(tmp.os.size()); }
                    OList me; walk_type(elems[i], false, me); n_origins_ -= uint32_t(me.os.size());
                    OList sub;
                    for (size_t k = 0; k < me.os.size() && off + k < cur.os.size(); ++k) {
                        sub.os.push_back(cur.os[off + k]); sub.inv.push_back(cur.inv[off + k]);
                    }
                    cur = sub;
                    t = elems[i];
                } else {
                    // A hidden-region struct's field has its own slice of the
                    // list; anything else (declared lifetimes, enum payload)
                    // reads the whole list.
                    TypeRef ft;
                    size_t total = 0; count_only(t, total);
                    auto slots = field_slots(t, pj.field, &ft);
                    bool ok = slots && total == cur.os.size();
                    if (ok) for (size_t ix : *slots) if (ix >= cur.os.size()) ok = false;
                    if (ok) {
                        OList sub;
                        for (size_t ix : *slots) { sub.os.push_back(cur.os[ix]); sub.inv.push_back(cur.inv[ix]); }
                        cur = sub;
                        t = ft;
                    } else {
                        cur.exact = false;
                    }
                }
            }
        }
        return cur;
    }
    // Two types that differ at most in their regions: the unit a value is
    // stored as (`T` in `Vec<T>`) whatever regions the two spellings carry.
    static bool same_type_modulo_regions(TypeRef a, TypeRef b, int depth = 0) {
        if (!a || !b || depth > 16) return a == b;
        if (a.kind() != b.kind()) return false;
        using K = LogosType::Kind;
        switch (a.kind()) {
            case K::Ref: case K::MutRef:
                return same_type_modulo_regions(a.pointee(), b.pointee(), depth + 1);
            case K::Slice:
                return a.mut_ptr() == b.mut_ptr() && a.raw_fat() == b.raw_fat() &&
                       a.owning_slice() == b.owning_slice() &&
                       same_type_modulo_regions(a.elem(), b.elem(), depth + 1);
            case K::Array:
                return a.arr_size() == b.arr_size() && same_type_modulo_regions(a.elem(), b.elem(), depth + 1);
            case K::Tuple: {
                auto ea = a.tuple_elems(), eb = b.tuple_elems();
                if (ea.size() != eb.size()) return false;
                for (size_t i = 0; i < ea.size(); ++i)
                    if (!same_type_modulo_regions(ea[i], eb[i], depth + 1)) return false;
                return true;
            }
            case K::Struct: case K::ZonedStruct: case K::Enum: case K::TraitObject: {
                if (a.kind() == K::Enum ? a.enum_name() != b.enum_name()
                    : a.kind() == K::TraitObject ? a.trait_name() != b.trait_name()
                    : a.struct_name() != b.struct_name()) return false;
                if (a.pkg_name() != b.pkg_name()) return false;
                auto ta = a.type_args(), tb = b.type_args();
                if (ta.size() != tb.size()) return false;
                for (size_t i = 0; i < ta.size(); ++i)
                    if (!same_type_modulo_regions(ta[i], tb[i], depth + 1)) return false;
                return true;
            }
            default:
                return types_equal(a, b);
        }
    }
    // Out-parameter store: argument `src` (of type `st`) is stored behind a
    // `&mut` whose pointee is `pointee` (origins `into`). When the pointee is a
    // generic container one of whose type arguments IS the value's type (a
    // `Vec<T>` receiving a `T`), the value's origins land PAIRWISE on that
    // argument's origins, invariant (behind `&mut`). Returns false when the
    // pointee has no such argument; the caller then relates every-to-every.
    bool relate_into_type_arg(const OList& src, TypeRef st, const OList& into,
                              TypeRef pointee, uint32_t p) {
        if (!pointee || !st || !src.exact || !into.exact) return false;
        auto k = pointee.kind();
        if (k != LogosType::Kind::Struct && k != LogosType::Kind::ZonedStruct &&
            k != LogosType::Kind::Enum)
            return false;
        size_t off = pointee.lifetime_args().size();
        for (auto& ta : pointee.type_args()) {
            OList tmp; walk_type(ta, false, tmp); n_origins_ -= uint32_t(tmp.os.size());
            size_t n = tmp.os.size();
            if (same_type_modulo_regions(ta, st) && n == src.os.size() && off + n <= into.os.size()) {
                for (size_t i = 0; i < n; ++i) {
                    subset(src.os[i], into.os[off + i], p);
                    subset(into.os[off + i], src.os[i], p);
                }
                return true;
            }
            off += n;
        }
        return false;
    }
    // Loans in `src` flow into `dst` at point p. Pairwise when both shapes
    // are known and agree; every-to-every otherwise.
    // The slice of `dst` that operand `i` of a struct or tuple literal
    // initialises, when the destination's regions are laid out per field.
    std::optional<OList> aggregate_slot(const bir::Stmt& s, size_t i, const OList& dst) {
        if (s.rv.k != bir::Rvalue::K::Aggregate || !s.dest.proj.empty() || !dst.exact) return std::nullopt;
        TypeRef t = b_.locals[s.dest.local].type;
        if (!t) return std::nullopt;
        size_t total = 0; count_only(t, total);
        if (total != dst.os.size()) return std::nullopt;
        std::optional<std::pair<size_t, size_t>> span;
        std::optional<std::vector<size_t>> slots;
        if (t.kind() == LogosType::Kind::Struct && i < s.rv.field_names.size())
            slots = field_slots(t, s.rv.field_names[i], nullptr);
        else if (t.kind() == LogosType::Kind::Enum && s.rv.what != "tuple" && s.rv.what != "array" &&
                 s.rv.what != "slice")
            slots = field_slots(t, s.rv.what + "#" + std::to_string(i), nullptr);
        if (slots) {
            OList sub;
            for (size_t ix : *slots) {
                if (ix >= dst.os.size()) return std::nullopt;
                sub.os.push_back(dst.os[ix]); sub.inv.push_back(dst.inv[ix]);
            }
            return sub;
        }
        if (t.kind() == LogosType::Kind::Struct || t.kind() == LogosType::Kind::Enum) {
            return std::nullopt;
        } else if (t.kind() == LogosType::Kind::Array && s.rv.what == "array") {
            span = std::make_pair(size_t(0), total);   // every element has the element type's list
        } else if (t.kind() == LogosType::Kind::Tuple && s.rv.what == "tuple") {
            auto elems = t.tuple_elems();
            if (i >= elems.size()) return std::nullopt;
            size_t off = 0;
            for (size_t k = 0; k < i; ++k) { size_t n = 0; count_only(elems[k], n); off += n; }
            size_t n = 0; count_only(elems[i], n);
            span = std::make_pair(off, n);
        }
        if (!span || span->first + span->second > dst.os.size()) return std::nullopt;
        OList sub;
        for (size_t k = 0; k < span->second; ++k) {
            sub.os.push_back(dst.os[span->first + k]); sub.inv.push_back(dst.inv[span->first + k]);
        }
        return sub;
    }
    void relate(const OList& src, const OList& dst, uint32_t p) {
        if (src.exact && dst.exact && src.os.size() == dst.os.size()) {
            for (size_t i = 0; i < src.os.size(); ++i) {
                subset(src.os[i], dst.os[i], p);
                if (dst.inv[i] || src.inv[i]) subset(dst.os[i], src.os[i], p);
            }
            return;
        }
        for (uint32_t a : src.os) for (uint32_t d : dst.os) subset(a, d, p);
    }
    void subset(uint32_t a, uint32_t d, uint32_t p) {
        if (a == d) return;
        row("subset_base", {o(a), o(d), pt(p)});
    }

    // ── signatures ──
    // One label per origin `walk_type(actual)` produces, read off the DECLARED
    // type: a written lifetime's name, "" for an elided one, "T:<name>:<k>"
    // for the k-th origin a type parameter T brought in, "?" when the two
    // types do not line up.
    void count_only(TypeRef t, size_t& n) {
        OList tmp;
        uint32_t saved = n_origins_;
        walk_type(t, false, tmp);
        n_origins_ = saved;
        n = tmp.os.size();
    }
    void labels(TypeRef decl, TypeRef actual, std::vector<std::string>& out, int depth = 0) {
        if (!actual || depth > 16) return;
        size_t n = 0;
        count_only(actual, n);
        if (!n) return;
        if (!decl) { out.insert(out.end(), n, "?"); return; }
        auto dk = decl.kind();
        if (dk == LogosType::Kind::TypeVar || dk == LogosType::Kind::AssocType) {
            std::string nm = dk == LogosType::Kind::TypeVar ? std::string(decl.type_var_name())
                                                            : std::string(decl.assoc_type_name());
            for (size_t k = 0; k < n; ++k) out.push_back("T:" + nm + ":" + std::to_string(k));
            return;
        }
        auto ak = actual.kind();
        auto same = [&](LogosType::Kind a, LogosType::Kind b) { return a == b; };
        switch (ak) {
            case LogosType::Kind::Ref: case LogosType::Kind::MutRef: {
                if (dk != LogosType::Kind::Ref && dk != LogosType::Kind::MutRef) break;
                std::string lt(decl.lifetime());
                out.push_back(lt.empty() || lt == "'_" ? "" : lt);
                labels(decl.pointee(), actual.pointee(), out, depth + 1);
                return;
            }
            case LogosType::Kind::Slice:
                if (!same(dk, ak)) break;
                // A borrowed slice is a reference: `&'a [T]` names its region
                // like `&'a T` does (`Chunks::next -> Option<&'a [T]>` must
                // take its result from `'a`, not from the `&mut self` autoref).
                if (!actual.owning_slice() && !actual.raw_fat()) {
                    std::string lt(decl.lifetime());
                    out.push_back(lt.empty() || lt == "'_" ? "" : lt);
                }
                labels(decl.elem(), actual.elem(), out, depth + 1);
                return;
            case LogosType::Kind::DstRef:
                // `&WString` IS NOT A `Ref`. WString is unsized, so the borrow
                // is a DstRef — and with this case missing the switch fell
                // through, the label count did not match the origin count, and
                // the caller replaced every label with "?", which matches every
                // source. `Writ::wstring<'a>(self: &'a Writ, s: str) -> &'a
                // WString` then gave its result `s`'s region as well as the
                // arena's, so `parse_value` looked like it returned a borrow of
                // the local `String` whose bytes the arena had just COPIED.
                // A written region on a DST borrow is a contract like any other.
                //
                // One label, and no recursion into the pointee: that is exactly
                // what `walk_type`'s DstRef arm counts.
                if (!same(dk, ak)) break;
                {
                    // ⚠ ONLY A WRITTEN REGION. Labelling an ELIDED DST borrow
                    // too was MEASURED and is wrong: the corpus went 146 -> 556
                    // failing, +239 of them pass fixtures newly REFUSED. An
                    // empty label is not "no constraint" — it takes the
                    // elided-result fallback, which relates every elided input,
                    // and behind `&mut` it also arms the invariance back-edge.
                    // Falling through to "?" is what those callees had before
                    // and what they still get; the contract-honouring part is
                    // the WRITTEN `'a`, which is all this case is here for.
                    std::string lt(decl.lifetime());
                    if (lt.empty() || lt == "'_") break;
                    if (actual.owning_dst() || actual.raw_fat()) break;
                    out.push_back(lt);
                }
                return;
            case LogosType::Kind::Ptr:
                // A RAW POINTER HAS NO REGION OF ITS OWN, and `walk_type`'s Ptr
                // arm says so: no origin for the pointer, the pointee's regions
                // laid out behind it. With no case here the switch fell through
                // and the caller made every label "?", which matches EVERY
                // source — so `fn parse_primary(doc: &Writ, lex: &mut Lexer,
                // src: str) -> *const WMap<..>` handed its raw-pointer result
                // the `&mut *lex` reborrow's region, the reborrow outlived its
                // use, and the next `lex.peek()` was a second mutable borrow of
                // a place the first still held.
                //
                // The pointee kinds skipped are the ones `walk_type` skips, for
                // its own stated reason, so the two stay one decision.
                if (!same(dk, ak)) break;
                {
                    TypeRef pt = actual.pointee();
                    if (!pt) return;
                    auto pk = pt.kind();
                    if (pk == LogosType::Kind::TraitObject || pk == LogosType::Kind::UnsizedDyn ||
                        pk == LogosType::Kind::Slice || pk == LogosType::Kind::UnsizedSlice ||
                        pk == LogosType::Kind::DstRef)
                        return;
                    labels(decl.pointee(), pt, out, depth + 1);
                }
                return;
            case LogosType::Kind::Array:
                if (!same(dk, ak)) break;
                labels(decl.elem(), actual.elem(), out, depth + 1);
                return;
            case LogosType::Kind::Tuple: {
                if (!same(dk, ak)) break;
                auto de = decl.tuple_elems(), ae = actual.tuple_elems();
                if (de.size() != ae.size()) break;
                for (size_t i = 0; i < ae.size(); ++i) labels(de[i], ae[i], out, depth + 1);
                return;
            }
            case LogosType::Kind::Struct: case LogosType::Kind::Enum: {
                if (!same(dk, ak)) break;
                auto alts = actual.lifetime_args(), dlts = decl.lifetime_args();
                size_t before = out.size();
                for (size_t i = 0, nl = struct_lifetime_count(actual); i < nl; ++i) {
                    std::string lt = i < dlts.size() ? dlts[i] : std::string();
                    if (!lt.empty() && lt[0] != '\'') lt = "'" + lt;
                    out.push_back(lt == "'_" ? "" : lt);
                }
                auto aa = actual.type_args(), da = decl.type_args();
                for (size_t i = 0; i < aa.size(); ++i)
                    labels(i < da.size() ? da[i] : TypeRef{}, aa[i], out, depth + 1);
                // The implicit origin of a loan-carrying type that declares no
                // lifetime: an unknown lifetime, labelled "~".
                if (out.size() == before && n == 1) { out.push_back("~"); return; }
                // An INSTANCE type carries no lifetime arguments (mono drops
                // them: `SliceIter<i64>` for the declared `SliceIter<'a, T>`),
                // so its regions are laid out from its fields. When the
                // DECLARED type names exactly one lifetime, every one of those
                // field regions is that lifetime — which is what lets
                // `next(&mut self) -> Option<&'a T>` take its result from the
                // iterator's `'a` and not from the `&mut self` autoref. With
                // "?" here every source fed the result, and `iter_max` /
                // `iter_min` held the loan of `it` in `best` (150 records).
                if (out.size() == before && alts.empty() && dlts.size() == 1) {
                    std::string lt = dlts[0];
                    if (!lt.empty() && lt[0] != '\'') lt = "'" + lt;
                    out.insert(out.end(), n, lt == "'_" ? std::string() : lt);
                    return;
                }
                // Declared with no lifetime either: all hidden.
                if (out.size() == before && alts.empty() && dlts.empty()) { out.insert(out.end(), n, "~"); return; }
                if (out.size() - before != n) { out.resize(before); break; }
                return;
            }
            default:
                break;
        }
        // Shapes differ (or a kind with no parts): every origin is unknown.
        size_t have = 0;
        (void)have;
        out.insert(out.end(), n, "?");
    }
    // Loans in the arguments flow into the call's result where the callee's
    // declared signature says so (Rust: written lifetimes by name, elided
    // result lifetime from `&self` or the single input lifetime, type
    // parameters position by position).
    bool relate_call(const bir::Term& t, const OList& dst, uint32_t p) {
        if (!dst.exact) return false;
        // AN INDIRECT CALL HAS NO BODY, BUT IT HAS A TYPE — and the type is
        // written by the author, so it is exactly as authoritative as a named
        // callee's signature. `QFn<'s> = fn(&[RtVal<'s>]) -> RtVal<'s>` says
        // the result comes from `'s`, not from the borrow of the argument
        // BUFFER; the one-bit-per-argument mask below cannot say that, so it
        // set the bit for `'s` and dragged `&argv` in with it. Matching region
        // LABELS is what the direct case already does.
        const bool indirect = !t.callee_fn;
        if (indirect && !t.callee_fty) return false;
        std::vector<TypeRef> pdecl, pact;
        TypeRef rdecl, ract;
        size_t arg_off = 0;   // indirect: args[0] is the callee itself
        auto ps = t.callee_fn ? t.callee_fn.params() : decltype(t.callee_fn.params()){};
        if (!indirect) {
            rdecl = t.callee_fn.decl_ret_type(pool_);
            ract  = t.callee_fn.ret_type(pool_);
            for (auto& pv : ps) { pdecl.push_back(pv.decl_type(pool_)); pact.push_back(pv.type(pool_)); }
        } else {
            rdecl = ract = t.callee_fty.closure_ret();
            for (auto pt : t.callee_fty.closure_params()) { pdecl.push_back(pt); pact.push_back(pt); }
            arg_off = 1;
        }
        std::vector<std::string> lret;
        labels(rdecl, ract, lret);
        if (lret.size() != dst.os.size()) {
            if (std::getenv("LOGOS_BIR_LABELS"))
                std::fprintf(stderr, "[labels-miss] %s lret=%zu dst=%zu ret=%s decl=%s dest=%s\n",
                             t.callee.c_str(), lret.size(), dst.os.size(),
                             type_str(ract).c_str(), type_str(rdecl).c_str(),
                             t.dest.valid() ? type_str(b_.locals[t.dest.local].type).c_str() : "-");
            return false;
        }
        struct Src { uint32_t origin; std::string label; bool outer_self; bool inv; };
        std::vector<Src> srcs;
        std::vector<size_t> elided_positions;
        bool self_ref = false;
        for (size_t i = 0; i < t.args.size(); ++i) {
            const auto& a = t.args[i];
            if (a.k == bir::Operand::K::Const || !a.place.valid()) continue;
            OList ao = place_origins(a.place);
            std::vector<std::string> pl;
            size_t pi = i >= arg_off ? i - arg_off : ~size_t(0);
            if (pi != ~size_t(0) && pi < pdecl.size()) labels(pdecl[pi], pact[pi], pl);
            if (!ao.exact || pl.size() != ao.os.size()) pl.assign(ao.os.size(), "?");
            bool is_self = !indirect && i == 0 && !ps.empty() && ps[0].name() == "self";
            TypeRef d0 = is_self ? pdecl[0] : TypeRef{};
            if (is_self && d0 && (d0.kind() == LogosType::Kind::Ref || d0.kind() == LogosType::Kind::MutRef ||
                                  (d0.kind() == LogosType::Kind::DstRef && !d0.owning_dst())))
                self_ref = true;
            for (size_t k = 0; k < ao.os.size(); ++k) {
                srcs.push_back({ao.os[k], pl[k], is_self && k == 0, bool(ao.inv[k])});
                if (pl[k].empty() || (pl[k][0] == '\'' && pl[k] != "'static")) elided_positions.push_back(srcs.size() - 1);
            }
        }
        // Several input lifetimes, no `&self`, and an elided result: Rust
        // refuses the signature (E0106), Logos accepts it. Narrow by the body
        // by its flow summary when there is one; otherwise every input stays a
        // source. An over-approximate summary only ever adds bits, so it is a
        // safe superset to narrow by. (A divergence to close by enforcing
        // E0106; ADR 0028.)
        // ARGUMENT TO ARGUMENT, by the signature: `push(&mut self: &mut Vec<T>,
        // value: T)` names `T` in both, and the position behind `&mut` is
        // invariant, so whatever `value` holds is held by `*self` after the
        // call. This is how rustc knows a Vec keeps what was pushed into it;
        // the body-derived out-param summary below remains for callees whose
        // signature does not say (hidden regions).
        for (size_t a = 0; a < srcs.size(); ++a)
            for (size_t b = 0; b < srcs.size(); ++b) {
                if (a == b || !srcs[b].inv || srcs[a].label != srcs[b].label) continue;
                const std::string& lb = srcs[a].label;
                bool identity = lb.starts_with("T:") || (lb.size() > 1 && lb[0] == '\'' && lb != "'static" &&
                                                        lb[1] != '%');
                if (identity) subset(srcs[a].origin, srcs[b].origin, p);
            }
        if (std::getenv("LOGOS_BIR_LABELS")) {
            std::string m = "[labels] " + t.callee + " ret:";
            for (auto& x : lret) m += " '" + x + "'";
            m += " srcs:";
            for (auto& x : srcs) m += " '" + x.label + "'" + (x.outer_self ? "(self)" : "");
            std::fprintf(stderr, "%s\n", m.c_str());
        }
        std::set<size_t> ambiguous_args;
        bool narrow = false;
        if (!self_ref && t.flow && t.flow->available) {
            std::set<size_t> args_with_lt;
            for (size_t i = 0, si = 0; i < t.args.size(); ++i) {
                const auto& a = t.args[i];
                if (a.k == bir::Operand::K::Const || !a.place.valid()) continue;
                size_t n = place_origins(a.place).os.size();
                for (size_t k = 0; k < n; ++k, ++si)
                    if (std::find(elided_positions.begin(), elided_positions.end(), si) != elided_positions.end())
                        args_with_lt.insert(i);
            }
            if (args_with_lt.size() > 1) {
                narrow = true;
                for (size_t i : args_with_lt)
                    if (i < 64 && ((t.flow->to_result >> i) & 1u)) ambiguous_args.insert(i);
            }
        }
        // Map each source origin back to its argument index for the narrowing.
        std::vector<size_t> src_arg;
        for (size_t i = 0; i < t.args.size(); ++i) {
            const auto& a = t.args[i];
            if (a.k == bir::Operand::K::Const || !a.place.valid()) continue;
            size_t n = place_origins(a.place).os.size();
            for (size_t k = 0; k < n; ++k) src_arg.push_back(i);
        }
        for (size_t j = 0; j < dst.os.size(); ++j) {
            const std::string& L = lret[j];
            if (L == "'static") continue;
            // A hidden region behind `&mut` in the result, with a hidden region
            // behind `&mut` among the arguments: those are the same position
            // of the same unnameable region, and ONLY they feed it. Letting
            // every source in as well (the "~" default below) put the
            // argument's own `&mut` loan into the region it is then tied back
            // to, so `vs` held the loan of itself (bc_flow_mask/a1_loopbreak).
            bool hidden_identity = false;
            if (L == "~" && dst.inv[j])
                for (auto& sr : srcs) if (sr.inv && sr.label == "~") { hidden_identity = true; break; }
            for (size_t si = 0; si < srcs.size(); ++si) {
                const Src& sr = srcs[si];
                bool from = false;
                if (hidden_identity) from = sr.inv && sr.label == "~";
                else if (L == "?" || sr.label == "?") from = true;
                else if (L.empty() || L == "~") {
                    // A hidden region in the result (`-> W` with `struct W { p:
                    // &mut i64 }`) is an ELIDED one: it follows the elision
                    // rule below like a written `&`, and it also takes any
                    // hidden region among the arguments (`fn id(b: B) -> B`).
                    // Letting every source in ignored the narrowing, and
                    // `keep(&mut b, &mut a) -> W` held `a` as well as `b`.
                    // (So does what a type parameter brought in: nothing in
                    // the signature separates it from the hidden region.)
                    if (L == "~" && (sr.label == "~" || sr.label.starts_with("T:"))) { from = true; }
                    else
                    // Elided result lifetime: from `&self`'s own lifetime,
                    // else from the one input lifetime, else from all.
                    if (self_ref) from = sr.outer_self;
                    else from = std::find(elided_positions.begin(), elided_positions.end(), si) !=
                                elided_positions.end() &&
                                (!narrow || ambiguous_args.count(src_arg[si]));
                    // ⚠ DO NOT NARROW THIS BY THE BODY SUMMARY. Tried and
                    // REFUTED, 2026-09-21, and the refutation is worth more
                    // than the attempt. The idea: a HIDDEN result region (`~`)
                    // is not a written contract, so the body should be the
                    // evidence — and `narrow` above consults the summary only
                    // when TWO OR MORE arguments are candidates, leaving the
                    // one-argument case tied unconditionally (`fn tvb(tag: i64,
                    // s: str) -> MyAny` holds `s` although its body returns
                    // `MyAny::Pod`; pass/bc_argcomp_tvbuild_byvalue_fat_admit is
                    // refused by this checker today because of it).
                    //
                    // MEASURED: clearing `from` when `to_result` has no bit for
                    // the argument closed that one over-refusal and OPENED SIX
                    // MISSES. Adding `!self_ref` recovered five — they were
                    // `&self` methods whose result legitimately holds the
                    // receiver (`fn mk(&self) -> B`, `cr.iter()`). The sixth
                    // survived and is the real answer: `fn thru(b: &B) -> B {
                    // return B { p: b.p }; }` summarises `to_result=0` with
                    // `available=1` — the body plainly stores `b.p` into the
                    // result. The summary's `taint_of` FieldRead arm
                    // short-circuits on `stored_shared_extract` (#77 r2 SEED),
                    // which is the right answer to the CHECKER's question
                    // ("where does a new loan root?") and the wrong answer to
                    // the SUMMARY's ("does a loan rooted at parameter i reach
                    // the result?"). One predicate, two notions.
                    //
                    // SO THE BIT UNDER-REPORTS, AND `narrow` ABOVE ALREADY
                    // TRUSTS IT: the two-argument spelling of that same program
                    // is a LIVE PERMISSIVE HOLE in this checker — `thru2(&b0,
                    // &bx); c.bump(); *b1.p` is admitted here and refused by the
                    // counter-based one. Rowed rather than patched: the repair
                    // is in the summary, which the OLD checker also consumes, so
                    // it moves verdicts on both sides and is priced on its own.
                } else {
                    from = sr.label == L;
                }
                if (from) {
                    subset(sr.origin, dst.os[j], p);
                    // A position behind `&mut` is invariant: the result's region
                    // IS the argument's (`index(&vs) -> &T` with T = `&mut
                    // Vec<&'y C>`: a store through the result lands in vs's 'y).
                    // Only where the match is an IDENTITY (the same type
                    // parameter's position, or the same named lifetime); the
                    // elided-result fallback relates every elided input and
                    // is an approximation, which must stay one-way.
                    if (dst.inv[j] && !L.empty() && L != "?" && L != "~" && sr.label == L)
                        subset(dst.os[j], sr.origin, p);
                    // A HIDDEN region ("~": a loan-carrying type that declares
                    // no lifetime, so the signature cannot name it) behind
                    // `&mut` on BOTH sides: `fn pick(v: &mut Vec<B>) -> &mut
                    // Vec<B>`. Rust would make the author name it and the two
                    // positions would be one region; with nothing written, a
                    // store through the result must be assumed to land in the
                    // argument. Hidden-to-hidden only, so a borrow's own region
                    // is never tied to what it points at.
                    else if (dst.inv[j] && sr.inv && L == "~" && sr.label == "~")
                        subset(dst.os[j], sr.origin, p);
                }
            }
        }
        return true;
    }

    // ── paths (moves / initialisation) ──
    // A move path is a local plus field projections, up to the first deref
    // or index. Returns ~0u when the place has no path (a temp-free const).
    // Move paths follow fields and the owning `*` of a Box (ADR 0028:
    // `(*b).s` is `b.*.s`); any other projection ends the path.
    uint32_t path_of(const bir::Place& p) {
        std::string key = "v" + std::to_string(p.local);
        uint32_t id = intern_path(key, ~0u, p.local);
        TypeRef t = b_.locals[p.local].synth_ref ? TypeRef{} : b_.locals[p.local].type;
        for (auto& pj : p.proj) {
            std::string seg;
            if (pj.k == bir::Proj::K::Field) {
                seg = pj.field;
                t = pj.ty ? pj.ty : field_type_(t, pj.field);
            } else if (pj.k == bir::Proj::K::Deref && is_std_box(t)) {
                seg = "*";
                t = pj.ty ? pj.ty : (t.type_args().empty() ? TypeRef{} : t.type_args()[0]);
            } else if (pj.k == bir::Proj::K::Index && pj.const_index >= 0 && !pj.from_end && t &&
                       t.kind() == LogosType::Kind::Array) {
                // ⚠ `!from_end`: an offset from the END is a different element
                // from the same offset from the start unless the length is
                // known, and it must not be given that element's move path. A
                // Subslice names a RANGE and has no single path either. Both
                // fall to the `break` below, which truncates to the container —
                // the conservative direction for a move.
                seg = "[" + std::to_string(pj.const_index) + "]";
                t = pj.ty ? pj.ty : t.elem();
            // ⚠ A Subslice gets NO path of its own, on purpose. Giving it one
            // made `a[2..3]` and `a[2]` distinct paths and MEASURED as four
            // missed moves — trading an over-refusal for silence, which is the
            // worse direction. A rest binding TAKEN BY VALUE over an array of
            // known length is bound to an AGGREGATE of the elements it covers
            // instead (see the Slice pattern arm), so each element moves by its
            // own precise path and this projection never reaches a move path.
            // Through a REFERENCE it stays here and truncates, which is the
            // conservative direction for a loan.
            } else {
                break;
            }
            std::string child = key + "." + seg;
            id = intern_path(child, id, p.local);
            key = std::move(child);
        }
        return id;
    }
    uint32_t intern_path(const std::string& key, uint32_t parent, bir::LocalId l) {
        auto it = path_ids_.find(key);
        if (it != path_ids_.end()) return it->second;
        uint32_t id = static_cast<uint32_t>(paths_.size());
        paths_.emplace_back(b_.locals[l].name + key.substr(key.find('.') == std::string::npos ? key.size() : key.find('.')), parent);
        path_ids_.emplace(key, id);
        if (parent == ~0u) row("path_is_var", {pa(id), var(l)});
        else row("child_path", {pa(id), pa(parent)});
        return id;
    }

    // ── accesses ──
    enum class Access : uint8_t { Read, WriteShallow, WriteDeep, StorageDead };
    struct Acc { uint32_t point; bir::Place place; Access kind; int32_t self_loan; };
    std::vector<Acc> accesses_;
    // (loan, point) -> the access that invalidated it; see invalidations().
    std::map<std::pair<uint32_t, uint32_t>, Acc> why_;
    // Points inside a block that ends in `return`. A loan killed by a
    // StorageDead THERE was still live when the function returned, which is
    // what "returns a reference to a local" means.
    std::set<uint32_t> return_points_;
    void access(uint32_t p, const bir::Place& pl, Access k, int32_t self_loan = -1) {
        accesses_.push_back({p, pl, k, self_loan});
    }
    void use_var(bir::LocalId l, uint32_t p) { row("var_used_at", {var(l), pt(p)}); }
    // A read of every local a place mentions: its root and its index locals.
    void use_place(const bir::Place& pl, uint32_t p) {
        use_var(pl.local, p);
        for (auto& pj : pl.proj) if (pj.k == bir::Proj::K::Index && pj.index != bir::kNoLocal) use_var(pj.index, p);
    }
    void read_operand(const bir::Operand& op, uint32_t p) {
        if (op.k == bir::Operand::K::Const || !op.place.valid()) return;
        use_place(op.place, p);
        uint32_t path = path_of(op.place);
        row("path_accessed_at_base", {pa(path), pt(p)});
        if (op.k == bir::Operand::K::Move && derefs_through(op.place))
            row("move_out_of_ref", {pa(path), pt(p)});
        if (op.k == bir::Operand::K::Move && field_of_drop_type(op.place))
            row("move_out_of_drop", {pa(path), pt(p)});
        if (op.k == bir::Operand::K::Move && !has_borrowing_deref(op.place)) {
            access(p, op.place, Access::WriteDeep);
            row("path_moved_at_base", {pa(path), pt(p)});
        } else {
            access(p, op.place, Access::Read);
        }
    }
    void write_place(const bir::Place& dest, uint32_t p) {
        for (auto& pj : dest.proj) if (pj.k == bir::Proj::K::Index && pj.index != bir::kNoLocal) use_var(pj.index, p);
        if (dest.proj.empty()) row("var_defined_at", {var(dest.local), pt(p)});
        else use_var(dest.local, p);   // writing through / into it uses it
        access(p, dest, Access::WriteShallow);
        kill_loans(dest, p);
        if (derefs_through(dest) & 1u) row("shared_deref_write", {var(dest.local), pt(p)});
        if (!has_borrowing_deref(dest)) {
            row("path_assigned_at_base", {pa(path_of(dest)), pt(p)});
            if (!dest.proj.empty() && !b_.locals[dest.local].is_mut &&
                b_.locals[dest.local].kind != bir::LocalKind::Temp)
                row("immutable_write", {var(dest.local), pt(p)});
        } else {
            // A write THROUGH a reference reads the reference: `src.0 = 66`
            // after `let moved = src;` is a use of the moved `src`. path_of
            // stops at the borrowing deref, so this is the pointer's own path.
            row("path_accessed_at_base", {pa(path_of(dest)), pt(p)});
        }
    }
    void kill_loans(const bir::Place& dest, uint32_t p) {
        for (uint32_t i = 0; i < loans_.size(); ++i) {
            // A loan issued by this very statement (`r = &mut *r`) is not
            // killed by the assignment that stores it.
            if (loans_[i].issue == p) continue;
            const bir::Place& lp = loans_[i].place;
            if (lp.local != dest.local || lp.proj.size() < dest.proj.size()) continue;
            bool prefix = true;
            for (size_t k = 0; k < dest.proj.size() && prefix; ++k)
                prefix = same_proj(lp.proj[k], dest.proj[k]);
            if (prefix) row("loan_killed_at", {l(i), pt(p)});
        }
    }
    static bool same_proj(const bir::Proj& a, const bir::Proj& b) {
        if (a.k != b.k) return false;
        if (a.k == bir::Proj::K::Field) return a.field == b.field;
        if (a.k == bir::Proj::K::Index && a.const_index >= 0 && b.const_index >= 0)
            return a.const_index == b.const_index;
        return true;
    }
    bool is_union_type(TypeRef t) const {
        if (!t || t.kind() != LogosType::Kind::Struct) return false;
        std::string want = concrete_struct_name(t);
        if (auto it = ts_.struct_by_name.find(want); it != ts_.struct_by_name.end()) return it->second.is_union();
        if (auto it = ts_.spec_by_name.find(want); it != ts_.spec_by_name.end()) return it->second.is_union();
        return false;
    }
    // Two places may denote overlapping memory. Distinct fields are disjoint,
    // except the fields of a UNION, which share their storage: a borrow of
    // `u.s.a` and one of `u.z.c` overlap whatever follows the union's field.
    bool overlap(const bir::Place& a, const bir::Place& b) const {
        if (a.local != b.local) return false;
        size_t n = std::min(a.proj.size(), b.proj.size());
        TypeRef t = b_.locals[a.local].synth_ref ? TypeRef{} : b_.locals[a.local].type;
        for (size_t k = 0; k < n; ++k) {
            const auto& x = a.proj[k];
            const auto& y = b.proj[k];
            if (x.k == bir::Proj::K::Field && y.k == bir::Proj::K::Field && x.field != y.field)
                return is_union_type(t);
            if (x.k == bir::Proj::K::Index && y.k == bir::Proj::K::Index && x.const_index >= 0 &&
                y.const_index >= 0 && x.from_end == y.from_end && x.const_index != y.const_index)
                return false;   // two literal elements of one array
            // ⚠ `x.from_end != y.from_end` FALLS THROUGH TO OVERLAP ON PURPOSE:
            // `a[2]` and the element 1 FROM THE END are the same element of a
            // 3-array and different elements of a 4-array, and the length is
            // not known here. A Subslice names a RANGE and overlaps anything
            // this walk cannot separate, which is the default below.
            if (x.k == bir::Proj::K::Deref)
                t = x.ty ? x.ty : (t && t.pointee() ? TypeRef(t.pointee()) : TypeRef{});
            else if (x.k == bir::Proj::K::Field)
                t = x.ty ? x.ty : (t ? field_type_(t, x.field) : TypeRef{});
            else
                t = x.ty ? x.ty : (t && t.kind() == LogosType::Kind::Array ? t.elem() : TypeRef{});
        }
        return true;
    }
    // Does the extension from `shorter` to `longer` pass through the deref of a
    // SHARED reference? If it does, the two places are NOT in the same borrow
    // tree: what is reached past a `&T` is reachable only by reading that
    // reference, and the holder of `shorter` cannot write it.
    //
    // THE DEFECT THIS CLOSES: `let a = &(*w.v).a;` with `w: W<&P>`, then
    // `w = W { v: &s2, .. };`. The loan's path is `(*w.v).a` and it is rooted at
    // `s1`, not at `w` — overwriting `w` swaps the POINTER and does not touch
    // `*w.v`. The assignment's `drop w` is lowered as a DEEP write, which hit
    // everything overlapping, so the loan died and `*a` on the next line was
    // refused. rustc accepts the program (MEASURED); its own `places_conflict`
    // stops the walk at a shared deref for exactly this reason.
    bool extends_through_shared_deref(const bir::Place& longer, const bir::Place& shorter) const {
        if (longer.local != shorter.local) return false;
        if (longer.proj.size() <= shorter.proj.size()) return false;
        if (b_.locals[longer.local].synth_ref) return false;
        // ⚠ NOT WHEN A DESTRUCTOR CAN READ IT. A deep write is also how a DROP
        // is modelled, and rustc's dropck says a `Drop` impl observes what it
        // can name through `self` — including what a shared field points at. So
        // the exemption is withheld for a type whose drop reaches a reference;
        // `bir_drop_observes_borrow` is the same predicate the checker already
        // uses for that question, so the two cannot drift apart.
        {
            TypeRef at = shorter.proj.empty() ? b_.locals[shorter.local].type
                                              : shorter.proj.back().ty;
            if (bir_drop_observes_borrow(at, prog_, ts_)) return false;
        }
        TypeRef t = b_.locals[longer.local].type;
        for (size_t k = 0; k < longer.proj.size(); ++k) {
            const auto& pj = longer.proj[k];
            if (!t) return false;
            if (pj.k == bir::Proj::K::Deref) {
                const bool shared = t.kind() == LogosType::Kind::Ref ||
                                    (t.borrowed_dyn_callable() && !t.mut_borrowed_dyn_callable());
                if (shared && k >= shorter.proj.size()) return true;
                if (t.kind() == LogosType::Kind::Ptr) return false;   // raw: outside the borrow system
                t = pj.ty ? pj.ty : t.pointee();
            } else if (pj.k == bir::Proj::K::Field) {
                t = pj.ty ? pj.ty : field_type_(t, pj.field);
            } else {
                t = pj.ty ? pj.ty : (t.kind() == LogosType::Kind::Array ? t.elem() : TypeRef{});
            }
        }
        return false;
    }
    // `longer` extends `shorter` with no deref in the extension.
    static bool extends_without_deref(const bir::Place& longer, const bir::Place& shorter) {
        if (longer.proj.size() < shorter.proj.size()) return false;
        for (size_t k = shorter.proj.size(); k < longer.proj.size(); ++k)
            if (longer.proj[k].k == bir::Proj::K::Deref) return false;
        return true;
    }
    void invalidations() {
        for (auto& a : accesses_) {
            for (uint32_t i = 0; i < loans_.size(); ++i) {
                const Loan& L = loans_[i];
                if (int32_t(i) == a.self_loan) continue;
                if (L.issue == a.point) continue;          // the borrow itself
                if (!overlap(a.place, L.place)) continue;
                bool hit = false;
                switch (a.kind) {
                    case Access::Read:
                        if (!L.mut_) break;
                        // Reads between a two-phase reservation and its
                        // activation are what two-phase borrowing allows.
                        if (L.activation != ~0u && point_order_[a.point] > point_order_[L.issue] &&
                            point_order_[a.point] < point_order_[L.activation]) break;
                        hit = true;
                        break;
                    case Access::WriteShallow:
                        // The written place, anything inside it without a
                        // deref, and anything it is inside of.
                        hit = extends_without_deref(L.place, a.place) ||
                              L.place.proj.size() <= a.place.proj.size();
                        break;
                    case Access::WriteDeep:
                        // …but not past a SHARED deref: see
                        // extends_through_shared_deref.
                        hit = !extends_through_shared_deref(L.place, a.place);
                        break;
                    case Access::StorageDead:
                        hit = extends_without_deref(L.place, a.place);
                        break;
                }
                if (hit) {
                    row("loan_invalidated_at", {l(i), pt(a.point)});
                    // ⚠ WHY the loan died is what the MESSAGE is made of, and it
                    // is known only here. The Datalog fact carries (loan,
                    // point); the kind of access, and whether that access was
                    // itself a borrow, decide between "cannot borrow as
                    // mutable", "cannot assign to", "cannot move" and "cannot
                    // use" — the wordings the corpus pins and the ones rustc
                    // uses. Keeping the first one per (loan, point): a later
                    // access at the same point cannot make the diagnosis more
                    // specific than the one that already fired.
                    why_.try_emplace({i, a.point}, a);
                }
            }
        }
    }

    // ── places behind raw pointers ──
    TypeRef field_type_(TypeRef st, std::string_view field) const {
        if (!st) return {};
        if (st.kind() == LogosType::Kind::Tuple) {
            auto elems = st.tuple_elems();
            uint32_t i = 0;
            for (char c : field) { if (c < '0' || c > '9') return {}; i = i * 10 + uint32_t(c - '0'); }
            return i < elems.size() ? elems[i] : TypeRef{};
        }
        if (st.kind() != LogosType::Kind::Struct) return {};
        std::string want = concrete_struct_name(st);
        auto look = [&](const auto& map) -> TypeRef {
            auto it = map.find(want);
            if (it == map.end()) return {};
            for (auto& f : it->second.fields())
                if (f.name() == field) return f.type(pool_);
            return {};
        };
        if (TypeRef t = look(ts_.struct_by_name)) return t;
        return look(ts_.spec_by_name);
    }
    // The stdlib `Box<T>`. Its built-in `*b` is an OWNING step (ADR 0028): a
    // place under it is part of `b` for moves, initialisation and writes.
    static bool is_std_box(TypeRef t) { return is_stdlib_box_type(t); }
    // Does the place go through a deref that does not own its target (a
    // reference or a raw pointer), as opposed to only through boxes?
    // The references a place is reached through, read off its types: bit 0 a
    // `&T` deref, bit 1 a `&mut T` deref. A Box deref is a move path and sets
    // nothing; a raw pointer, a synthesised autoref or an unknown type ends the
    // walk without an answer (nothing is guessed past it).
    unsigned derefs_through(const bir::Place& p) const {
        TypeRef t = b_.locals[p.local].type;
        if (b_.locals[p.local].synth_ref) return 0;
        unsigned out = 0;
        // bit 2: the place is inside a `static` item.
        if (b_.locals[p.local].static_addr && !p.proj.empty() && p.proj[0].k == bir::Proj::K::Deref)
            out |= 4u;
        for (auto& pj : p.proj) {
            if (!t) return out;
            if (pj.k == bir::Proj::K::Deref) {
                if (t.kind() == LogosType::Kind::Ref) out |= 1u;
                else if (t.kind() == LogosType::Kind::MutRef) out |= 2u;
                // A `dyn Fn*` written as a reference is a reference: the fat
                // borrow IS the value, so its deref is a borrowing one and its
                // strength is the borrow's own.
                else if (t.borrowed_dyn_callable()) out |= t.mut_borrowed_dyn_callable() ? 2u : 1u;
                else if (t.kind() == LogosType::Kind::Ptr) return out & 4u;   // raw: outside the borrow system (a static's address is spelled as one)
                else if (!is_std_box(t)) return out;
                t = pj.ty ? pj.ty : (t.kind() == LogosType::Kind::Struct
                                         ? (t.type_args().empty() ? TypeRef{} : t.type_args()[0])
                                         : t.pointee());
            } else if (pj.k == bir::Proj::K::Field) {
                t = pj.ty ? pj.ty : field_type_(t, pj.field);
            } else {
                if (t.kind() == LogosType::Kind::Ptr) return out & 4u;   // `p[i]` on a raw pointer
                t = pj.ty ? pj.ty : (t.kind() == LogosType::Kind::Array ? t.elem() : TypeRef{});
            }
        }
        return out;
    }
    // E0509: the place is a FIELD of a value whose own type implements Drop,
    // reached without leaving that value (a deref ends the walk: what lies
    // behind a pointer is not part of the owner). The destructor must see the
    // whole value, so nothing may be moved out of it first.
    bool field_of_drop_type(const bir::Place& p) const {
        TypeRef t = b_.locals[p.local].type;
        if (b_.locals[p.local].synth_ref) return false;
        bool hit = false;
        for (auto& pj : p.proj) {
            if (!t) return hit;
            if (pj.k == bir::Proj::K::Deref) {
                if (!is_std_box(t)) return hit;
                t = pj.ty ? pj.ty : (t.type_args().empty() ? TypeRef{} : t.type_args()[0]);
                hit = false;   // the Box's content is its own owner
            } else if (pj.k == bir::Proj::K::Field) {
                std::string owner = t.kind() == LogosType::Kind::Struct ? std::string(t.struct_name())
                                  : t.kind() == LogosType::Kind::Enum   ? std::string(t.enum_name())
                                                                        : std::string();
                if (!owner.empty() && ts_.drop_types.count(owner)) hit = true;
                t = pj.ty ? pj.ty : field_type_(t, pj.field);
            } else {
                t = pj.ty ? pj.ty : (t.kind() == LogosType::Kind::Array ? t.elem() : TypeRef{});
            }
        }
        return hit;
    }
    bool has_borrowing_deref(const bir::Place& p) const {
        TypeRef t = b_.locals[p.local].type;
        bool synth = b_.locals[p.local].synth_ref;
        for (auto& pj : p.proj) {
            if (pj.k == bir::Proj::K::Deref) {
                if (synth || !is_std_box(t)) return true;
                t = pj.ty ? pj.ty : (t.type_args().empty() ? TypeRef{} : t.type_args()[0]);
            } else if (pj.k == bir::Proj::K::Field) {
                t = pj.ty ? pj.ty : field_type_(t, pj.field);
            } else {
                t = pj.ty ? pj.ty : (t && t.kind() == LogosType::Kind::Array ? t.elem() : TypeRef{});
            }
            synth = false;
        }
        return false;
    }
    // A place reached through `*p` with p a raw pointer: rustc does not check
    // loans there (the raw pointer took the place out of the borrow system).
    bool through_raw_pointer(const bir::Place& p) const {
        TypeRef t = b_.locals[p.local].type;
        bool synth = b_.locals[p.local].synth_ref;
        for (auto& pj : p.proj) {
            if (pj.k == bir::Proj::K::Deref) {
                if (synth) { synth = false; t = pj.ty ? pj.ty : b_.locals[p.local].synth_pointee; continue; }
                if (t && (t.kind() == LogosType::Kind::Ptr || t.raw_fat())) return true;
                t = pj.ty ? pj.ty : (t && t.kind() != LogosType::Kind::Slice ? t.pointee() : TypeRef{});
            } else if (pj.k == bir::Proj::K::Field) {
                t = pj.ty ? pj.ty : field_type_(t, pj.field);
            } else {
                t = pj.ty ? pj.ty : (t && t.kind() == LogosType::Kind::Array ? t.elem() : TypeRef{});
            }
        }
        return false;
    }

    // ── row helpers ──
    // ── initialisation as bitset dataflow (ADR 0028) ──────────────────────
    //
    // Maybe-initialised and maybe-uninitialised paths are forward "may"
    // dataflows over points x paths. As Datalog relations they cost a row per
    // (path, point): 14.8 M + 8.2 M rows on one 48 k-point body. rustc computes
    // them as bitset dataflow (MaybeInitializedPlaces / MaybeUninitializedPlaces)
    // and runs Polonius for loans only; this does the same, and hands the rules
    // exactly the three restrictions they consume:
    //   path_maybe_uninitialized_on_entry(X, Q)   X accessed at Q (move_errors)
    //   path_maybe_initialized_on_entry(X, P)     a variable's root path assigned
    //                                             at P (assign_twice, E0384)
    //   var_maybe_partly_initialized_on_exit(V, P) V's drop touches an origin
    //                                             (drop-liveness)
    // Transfer at a point q:
    //   init_exit   = assigned(q) U (init_in - moved(q) - dead(q))
    //   uninit_exit = moved(q)    U (uninit_in - assigned(q) - dead(q))
    // where assigned / moved reach every descendant path, a StorageDead every
    // path of its variable, and *_in(q) is the union over q's predecessors.
    void initialization() {
        auto num = [](const std::string& s) { return uint32_t(std::stoul(s.substr(1))); };
        const uint32_t npath = uint32_t(paths_.size());
        uint32_t npt = 0;
        std::vector<std::pair<uint32_t, uint32_t>> edges;
        std::vector<std::pair<uint32_t, uint32_t>> assigned, moved, accessed;
        std::vector<std::pair<uint32_t, uint32_t>> dead_var;   // (var, point)
        std::set<uint32_t> drop_vars;
        std::vector<uint32_t> var_of_root;                      // root path -> var
        var_of_root.assign(npath, ~0u);
        for (auto& [rel, vs] : rows_) {
            if (rel == "cfg_edge") {
                uint32_t a = num(vs[0]), b = num(vs[1]);
                edges.emplace_back(a, b);
                npt = std::max(npt, std::max(a, b) + 1);
            } else if (rel == "path_assigned_at_base") assigned.emplace_back(num(vs[0]), num(vs[1]));
            else if (rel == "path_moved_at_base") moved.emplace_back(num(vs[0]), num(vs[1]));
            else if (rel == "path_accessed_at_base") accessed.emplace_back(num(vs[0]), num(vs[1]));
            else if (rel == "var_dead_at") dead_var.emplace_back(num(vs[0]), num(vs[1]));
            else if (rel == "drop_of_var_derefs_origin") drop_vars.insert(num(vs[0]));
            else if (rel == "path_is_var") var_of_root[num(vs[0])] = num(vs[1]);
        }
        if (npath == 0 || npt == 0) return;
        // Path tree: descendants of each path, paths of each variable.
        std::vector<std::vector<uint32_t>> kids(npath);
        std::vector<uint32_t> root(npath);
        for (uint32_t i = 0; i < npath; ++i) {
            if (paths_[i].second != ~0u) kids[paths_[i].second].push_back(i);
        }
        for (uint32_t i = 0; i < npath; ++i) {
            uint32_t r = i;
            while (paths_[r].second != ~0u) r = paths_[r].second;
            root[i] = r;
        }
        std::map<uint32_t, std::vector<uint32_t>> var_paths;   // var -> its paths
        for (uint32_t i = 0; i < npath; ++i)
            if (var_of_root[root[i]] != ~0u) var_paths[var_of_root[root[i]]].push_back(i);
        auto subtree = [&](uint32_t x, auto&& f) {
            std::vector<uint32_t> st{x};
            while (!st.empty()) {
                uint32_t y = st.back(); st.pop_back();
                f(y);
                for (uint32_t k : kids[y]) st.push_back(k);
            }
        };
        const size_t W = (npath + 63) / 64;
        auto bit_set = [&](std::vector<uint64_t>& v, size_t pt, uint32_t x) { v[pt * W + x / 64] |= 1ull << (x % 64); };
        auto bit_get = [&](const std::vector<uint64_t>& v, size_t pt, uint32_t x) {
            return (v[pt * W + x / 64] >> (x % 64)) & 1ull; };
        std::vector<uint64_t> g_asg(npt * W, 0), g_mov(npt * W, 0), g_dead(npt * W, 0);
        for (auto& [x, q] : assigned) subtree(x, [&](uint32_t y) { bit_set(g_asg, q, y); });
        for (auto& [x, q] : moved)    subtree(x, [&](uint32_t y) { bit_set(g_mov, q, y); });
        for (auto& [v, q] : dead_var)
            if (auto it = var_paths.find(v); it != var_paths.end())
                for (uint32_t y : it->second) bit_set(g_dead, q, y);
        std::vector<std::vector<uint32_t>> preds(npt), succs(npt);
        for (auto& [a, b] : edges) { preds[b].push_back(a); succs[a].push_back(b); }
        // Two forward may-dataflows, one worklist each.
        auto solve = [&](const std::vector<uint64_t>& gen, const std::vector<uint64_t>& kill1,
                         const std::vector<uint64_t>& kill2, std::vector<uint64_t>& exit) {
            exit.assign(npt * W, 0);
            std::vector<uint64_t> in(W);
            std::vector<char> queued(npt, 1);
            std::vector<uint32_t> work(npt);
            for (uint32_t i = 0; i < npt; ++i) work[i] = npt - 1 - i;
            while (!work.empty()) {
                uint32_t q = work.back(); work.pop_back(); queued[q] = 0;
                std::fill(in.begin(), in.end(), 0);
                for (uint32_t p : preds[q])
                    for (size_t w = 0; w < W; ++w) in[w] |= exit[p * W + w];
                bool changed = false;
                for (size_t w = 0; w < W; ++w) {
                    uint64_t nv = gen[q * W + w] | (in[w] & ~kill1[q * W + w] & ~kill2[q * W + w]);
                    if (nv != exit[q * W + w]) { exit[q * W + w] = nv; changed = true; }
                }
                if (changed)
                    for (uint32_t s2 : succs[q]) if (!queued[s2]) { queued[s2] = 1; work.push_back(s2); }
            }
        };
        std::vector<uint64_t> init_exit, uninit_exit;
        solve(g_asg, g_mov, g_dead, init_exit);
        solve(g_mov, g_asg, g_dead, uninit_exit);
        auto on_entry = [&](const std::vector<uint64_t>& exit, uint32_t q, uint32_t x) {
            for (uint32_t p : preds[q]) if (bit_get(exit, p, x)) return true;
            return false;
        };
        std::set<std::pair<uint32_t, uint32_t>> seen;
        for (auto& [xb, q] : accessed)
            subtree(xb, [&](uint32_t y) {
                if (on_entry(uninit_exit, q, y) && seen.emplace(y, q).second)
                    row("path_maybe_uninitialized_on_entry", {pa(y), pt(q)});
            });
        for (auto& [x, q] : assigned)
            if (paths_[x].second == ~0u && on_entry(init_exit, q, x))
                row("path_maybe_initialized_on_entry", {pa(x), pt(q)});
        for (auto& [x, q] : assigned)
            for (uint32_t a = paths_[x].second; a != ~0u; a = paths_[a].second)
                if (on_entry(uninit_exit, q, a)) { row("partial_reinit", {pa(x), pt(q)}); break; }
        for (uint32_t v : drop_vars) {
            auto it = var_paths.find(v);
            if (it == var_paths.end()) continue;
            for (uint32_t q = 0; q < npt; ++q)
                for (uint32_t y : it->second)
                    if (bit_get(init_exit, q, y)) { row("var_maybe_partly_initialized_on_exit", {var(v), pt(q)}); break; }
        }
    }

    void row(const char* rel, std::initializer_list<std::string> vals) {
        rows_.emplace_back(rel, std::vector<std::string>(vals));
    }
    static std::string o(uint32_t x)  { return "o" + std::to_string(x); }
    static std::string l(uint32_t x)  { return "l" + std::to_string(x); }
    static std::string pt(uint32_t x) { return "p" + std::to_string(x); }
    static std::string pa(uint32_t x) { return "x" + std::to_string(x); }
    static std::string var(bir::LocalId x) { return "v" + std::to_string(x); }

    const bir::Body&                   b_;
    const lir::LProgram&               prog_;
    const TypeSets&                    ts_;
    const TypePoolImpl*                pool_;
    uint32_t                           n_origins_ = 0;
    std::unordered_map<bir::LocalId, OList> local_os_;
    std::vector<Loan>                  loans_;
    std::vector<std::pair<std::string, uint32_t>> paths_;   // display, parent
    std::unordered_map<std::string, uint32_t>     path_ids_;
    std::vector<std::pair<std::string, std::vector<std::string>>> rows_;
    std::vector<uint32_t>              point_order_;   // point id -> linear order
    std::vector<uint32_t>              point_lines_;

public:
    const std::vector<std::pair<std::string, std::vector<std::string>>>& rows() const { return rows_; }
};

void BirFacts::emit(dl::Database& db, dl::Symbols& syms) {
    // Points: block by block, statement by statement, Start then Mid.
    struct BP { uint32_t first = 0; uint32_t n = 0; };
    std::vector<BP> bp(b_.blocks.size());
    uint32_t np = 0;
    for (size_t bi = 0; bi < b_.blocks.size(); ++bi) {
        bp[bi].first = np;
        bp[bi].n = uint32_t(b_.blocks[bi].stmts.size() + 1) * 2;
        np += bp[bi].n;
    }
    point_order_.resize(np);
    point_lines_.resize(np);
    for (uint32_t i = 0; i < np; ++i) point_order_[i] = i;
    auto S = [&](size_t bi, size_t si) { return bp[bi].first + uint32_t(si) * 2; };
    auto M = [&](size_t bi, size_t si) { return bp[bi].first + uint32_t(si) * 2 + 1; };
    for (size_t bi = 0; bi < b_.blocks.size(); ++bi) {
        const auto& blk = b_.blocks[bi];
        size_t n = blk.stmts.size();
        for (size_t si = 0; si <= n; ++si) {
            uint32_t line = si < n ? blk.stmts[si].line : blk.term.line;
            point_lines_[S(bi, si)] = point_lines_[M(bi, si)] = line;
            row("cfg_edge", {pt(S(bi, si)), pt(M(bi, si))});
            if (si < n) row("cfg_edge", {pt(M(bi, si)), pt(S(bi, si + 1))});
        }
        if (blk.terminated)
            for (bir::BlockId t : blk.term.targets) row("cfg_edge", {pt(M(bi, n)), pt(S(t, 0))});
        if (blk.terminated && blk.term.k == bir::Term::K::Return)
            for (size_t si = 0; si <= n; ++si) {
                return_points_.insert(S(bi, si));
                return_points_.insert(M(bi, si));
            }
    }

    // Parameters are initialised at entry; every other local starts
    // uninitialised. Parameter and return-type origins are universal.
    uint32_t entry = S(0, 0);
    for (bir::LocalId li = 0; li < b_.locals.size(); ++li) {
        const auto& L = b_.locals[li];
        const OList& os = local_origins(li);
        for (uint32_t oo : os.os) row("use_of_var_derefs_origin", {var(li), o(oo)});
        if (L.kind == bir::LocalKind::Param || L.kind == bir::LocalKind::Return)
            for (uint32_t oo : os.os) row("universal_region", {o(oo)});
        // `let x: &'a i64 = &v;` with `'a` a lifetime PARAMETER of the
        // function: the annotation names a region that outlives the body, so
        // that origin is universal exactly as a parameter's is, and a loan of a
        // local that reaches it is refused when the local dies (E0597).
        // USER bindings only: a temporary's type is inferred, not declared, and
        // an unresolved callee's result (`g1.get()` through a bound, returning
        // `&'a i64`) would make the receiver's autoref loan universal.
        // ... and only when the program WROTE the lifetime (sema's
        // ANNOT_LIFETIME): `let m: &mut i32 = v;` gets `&'a mut i32` by
        // inference, and a reborrow that ends at `m`'s last use is not a loan
        // for all of `'a` (nll/poll-reborrow-then-remut).
        else if (L.kind == bir::LocalKind::User && L.annot_lifetime && !b_.lifetime_params.empty() &&
                 L.type && !L.synth_ref) {
            std::vector<std::string> ls;
            labels(L.type, L.type, ls);
            if (ls.size() == os.os.size())
                for (size_t k = 0; k < ls.size(); ++k)
                    if (b_.lifetime_params.count(ls[k])) row("universal_region", {o(os.os[k])});
        }
        if (L.kind == bir::LocalKind::Param)
            row("path_assigned_at_base", {pa(path_of({li, {}})), pt(entry)});
        else
            row("path_moved_at_base", {pa(path_of({li, {}})), pt(entry)});
        if (L.is_mut) row("var_mut", {var(li)});
        if (bir_drop_observes_borrow(L.type, prog_, ts_))
            for (uint32_t oo : os.os) row("drop_of_var_derefs_origin", {var(li), o(oo)});
    }

    // ── placeholders: what the SIGNATURE says about the universal regions ──
    // Each universal origin holds a placeholder loan; one flowing into another
    // universal origin is an error unless the signature relates them
    // (subset_errors in polonius.dl): the same written or elided-and-minted
    // lifetime, the same type parameter's position, a declared `'a: 'b`, or
    // `'static`, which outlives everything. "return type has lifetime 'a but
    // 'y' has lifetime 'b" is this rule; without the facts it never fired.
    {
        struct U { uint32_t origin; std::string label; bool claim; bool capture; };
        std::vector<U> us;
        // Which regions the check may make a CLAIM about: the regions of plain
        // references (through tuples and arrays). A region that is an ADT's
        // lifetime or sits inside an ADT's type argument has the variance that
        // ADT's definition gives it — `Cb<&'a i32>` with `f: fn(T)` is
        // contravariant, `Marker<'a>` with `'a` unused is bivariant — and the
        // L-IR does not carry variance. No claim there is a miss at worst,
        // never a refusal of a legal program.
        std::function<void(TypeRef, bool, std::vector<bool>&, int)> claim_mask =
            [&](TypeRef t, bool claim, std::vector<bool>& out, int depth) {
                if (!t || depth > 16) return;
                switch (t.kind()) {
                    case LogosType::Kind::Ref: case LogosType::Kind::MutRef:
                        out.push_back(claim); claim_mask(t.pointee(), claim, out, depth + 1); return;
                    case LogosType::Kind::Slice:
                        if (!t.owning_slice() && !t.raw_fat()) out.push_back(claim);
                        if (!t.raw_fat()) claim_mask(t.elem(), claim, out, depth + 1);
                        return;
                    case LogosType::Kind::Array: claim_mask(t.elem(), claim, out, depth + 1); return;
                    case LogosType::Kind::Tuple:
                        for (auto& e : t.tuple_elems()) claim_mask(e, claim, out, depth + 1);
                        return;
                    default: { size_t n = 0; count_only(t, n); out.insert(out.end(), n, false); return; }
                }
            };
        // `'a: 'b, 'b: 'c` — closed over the NAMES, since `'b` may appear in no
        // parameter and so have no origin to carry the chain.
        std::set<std::pair<std::string, std::string>> outl(b_.outlives.begin(), b_.outlives.end());
        for (bool grew = true; grew;) {
            grew = false;
            for (auto& x : std::vector<std::pair<std::string, std::string>>(outl.begin(), outl.end()))
                for (auto& y : std::vector<std::pair<std::string, std::string>>(outl.begin(), outl.end()))
                    if (x.second == y.first && outl.emplace(x.first, y.second).second) grew = true;
        }
        for (bir::LocalId li = 0; li < b_.locals.size(); ++li) {
            const auto& L = b_.locals[li];
            if (L.kind != bir::LocalKind::Param && L.kind != bir::LocalKind::Return) continue;
            const OList& os = local_origins(li);
            std::vector<std::string> ls;
            labels(L.decl_type ? L.decl_type : L.type, L.type, ls);
            if (ls.size() != os.os.size()) ls.assign(os.os.size(), "?");
            std::vector<bool> cm;
            claim_mask(L.type, true, cm, 0);
            if (cm.size() != os.os.size()) cm.assign(os.os.size(), false);
            for (size_t k = 0; k < os.os.size(); ++k) {
                us.push_back({os.os[k], ls[k], bool(cm[k]), L.is_capture});
                placeholder_owner_.emplace(os.os[k], Universal{li, ls[k]});
            }
        }
        uint32_t next_loan = 1000000;   // placeholder loans never meet a real loan's id
        for (auto& u : us) row("placeholder", {o(u.origin), l(next_loan++)});
        auto elided = [](const std::string& l) { return l.empty() || l.starts_with("'%"); };
        auto related = [&](const U& a, const U& b) {   // may a's data flow into b?
            // ⚠ A CLOSURE PARAMETER'S REGION IS BOUND BY THE CALL; A CAPTURE'S
            // IS THE PARENT FRAME'S. That asymmetry IS E0521: the parent chose
            // the capture's region before any call existed, so nothing the call
            // binds can be known to outlive it, while the reverse — a capture's
            // data reaching a parameter — is always fine. Without this the two
            // were both elided placeholders, `related` took the elided-target
            // branch ("elision decides on the caller's side"), and every
            // `give(|y| { x = y; })` was admitted: ten fixtures, all E0521/E0597
            // upstream. A parameter whose region is WRITTEN (`|y: &'a i64|`) is
            // early-bound from the parent and falls through to the label rules,
            // exactly as rustc treats it.
            if (b_.is_closure && b.capture && !a.capture && elided(a.label)) return false;
            if (!a.claim || !b.claim) return true;
            if (a.label == "?" || b.label == "?" || a.label == "~" || b.label == "~") return true;   // unknown: no claim
            // An ELIDED target (`-> &i64`, which sema mints a fresh name for):
            // elision decides what may reach it, and that is relate_call's
            // rule on the caller's side, not a named relation to check here.
            if (elided(b.label)) return true;
            // A region a TYPE PARAMETER brought in: in an instance `iter_max<I
            // = SliceIter<'x, i32>, T = &'x i32>` the two are one region, but
            // the declared signature relates `I` and `T` only through a trait
            // bound the instance no longer shows. rustc decides this on the
            // generic body, and so does the template pass (#434); an instance
            // makes no claim.
            if (a.label.starts_with("T:") || b.label.starts_with("T:")) return true;
            if (a.label.empty()) return false;
            if (a.label == b.label) return true;
            if (a.label == "'static") return true;
            if (outl.count({a.label, b.label})) return true;
            return false;
        };
        for (auto& a : us) for (auto& b : us)
            if (a.origin != b.origin && related(a, b))
                row("known_placeholder_subset", {o(a.origin), o(b.origin)});
    }

    // Loans first (the kill and invalidation passes need all of them).
    std::vector<std::vector<int32_t>> loan_at(b_.blocks.size());
    for (size_t bi = 0; bi < b_.blocks.size(); ++bi) {
        loan_at[bi].assign(b_.blocks[bi].stmts.size(), -1);
        for (size_t si = 0; si < b_.blocks[bi].stmts.size(); ++si) {
            const auto& s = b_.blocks[bi].stmts[si];
            if (s.k != bir::Stmt::K::Assign || s.rv.k != bir::Rvalue::K::Ref) continue;
            if (through_raw_pointer(s.rv.place)) continue;
            Loan L;
            L.place = s.rv.place;
            L.mut_ = s.rv.mut_;
            L.issue = M(bi, si);
            L.origin = fresh_origin();
            L.line = s.line;
            loan_at[bi][si] = int32_t(loans_.size());
            loans_.push_back(std::move(L));
        }
    }
    for (auto& tp : b_.two_phase) {
        int32_t li = loan_at[tp.ref_block][tp.ref_stmt];
        if (li < 0) continue;
        loans_[li].activation = M(tp.act_block, b_.blocks[tp.act_block].stmts.size());
    }

    for (size_t bi = 0; bi < b_.blocks.size(); ++bi) {
        const auto& blk = b_.blocks[bi];
        for (size_t si = 0; si < blk.stmts.size(); ++si) {
            const auto& s = blk.stmts[si];
            uint32_t p = M(bi, si);
            switch (s.k) {
                case bir::Stmt::K::Assign: {
                    OList dst = place_origins(s.dest);
                    if (s.rv.k == bir::Rvalue::K::Ref && loan_at[bi][si] < 0) {
                        // A borrow through a raw pointer: no loan, but still a
                        // use of the pointer and a value flowing into dest.
                        use_place(s.rv.place, p);
                    } else if (s.rv.k == bir::Rvalue::K::Ref) {
                        const Loan& L = loans_[loan_at[bi][si]];
                        row("loan_issued_at", {o(L.origin), l(uint32_t(loan_at[bi][si])), pt(p)});
                        use_place(s.rv.place, p);
                        row("path_accessed_at_base", {pa(path_of(s.rv.place)), pt(p)});
                        // Creating a shared borrow reads the place; a `&mut`
                        // writes it (a two-phase one reads until activation).
                        access(p, s.rv.place,
                               s.rv.mut_ && !s.rv.two_phase ? Access::WriteDeep : Access::Read,
                               loan_at[bi][si]);
                        if (s.rv.mut_ && (derefs_through(s.rv.place) & 1u))
                            row("shared_deref_write", {var(s.rv.place.local), pt(p)});
                        if (s.rv.mut_ && !has_borrowing_deref(s.rv.place) &&
                            !b_.locals[s.rv.place.local].is_mut &&
                            b_.locals[s.rv.place.local].kind != bir::LocalKind::Temp)
                            row("immutable_write", {var(s.rv.place.local), pt(p)});
                        // Reborrow: borrowing through `*r` where r: &'p T
                        // keeps what r holds alive as long as the new loan
                        // ('p: 'loan), for every reference deref'd on the way.
                        {
                            bir::Place prefix{s.rv.place.local, {}};
                            const bir::Local& root = b_.locals[prefix.local];
                            TypeRef pt = root.type;
                            bool synth = root.synth_ref;
                            for (auto& pj : s.rv.place.proj) {
                                bool ref_deref = pj.k == bir::Proj::K::Deref &&
                                    (synth || (pt && (pt.kind() == LogosType::Kind::Ref ||
                                                      pt.kind() == LogosType::Kind::MutRef ||
                                                      (pt.kind() == LogosType::Kind::Slice && !pt.owning_slice()))));
                                if (ref_deref) {
                                    OList po = place_origins(prefix);
                                    if (po.exact && !po.os.empty()) subset(po.os[0], L.origin, p);
                                    else for (uint32_t oo : po.os) subset(oo, L.origin, p);
                                }
                                if (pj.k == bir::Proj::K::Deref && synth) { pt = root.synth_pointee; synth = false; }
                                else if (pj.k == bir::Proj::K::Deref) pt = pt ? pt.pointee() : TypeRef{};
                                else if (pj.k == bir::Proj::K::Field) pt = pj.ty;   // the field's type, when the place recorded it
                                else pt = pt && pt.kind() == LogosType::Kind::Array ? pt.elem() : TypeRef{};
                                prefix = prefix.with(pj);
                            }
                        }
                        // The loan's origin flows into the reference's outer
                        // origin; the referent's origins into the inner ones.
                        if (!dst.os.empty()) {
                            subset(L.origin, dst.os[0], p);
                            OList inner = dst;
                            inner.os.erase(inner.os.begin()); inner.inv.erase(inner.inv.begin());
                            OList referent = place_origins(s.rv.place);
                            if (s.rv.mut_) {
                                for (auto&& f : referent.inv) f = true;
                            }
                            relate(referent, inner, p);
                        }
                    } else {
                        // A CAST TO A RAW POINTER ERASES PROVENANCE, which is
                        // this file's own established rule one screen up: a
                        // DEREF of a raw pointer drops the tracked origins and
                        // mints fresh ones ("raw: untracked"). The write side
                        // had no such rule, so `let p: *const F = &d.f.value as
                        // *const F;` related the REFERENCE's own loan into the
                        // pointer and kept it live for as long as `p` was used
                        // — `read::<F>(p)` on the next line then read "through"
                        // a borrow that Rust ends at the cast. Three pass
                        // fixtures were refused this way. The operand is still
                        // READ here, so the borrow must be live AT the cast,
                        // which is exactly Rust's requirement and no more.
                        //
                        // ⚠ ONLY THE CAST. A raw-pointer FIELD still takes what
                        // is stored into it (`MH { q: &r }` with
                        // `q: *mut &'a mut i64`), because there the region 'a
                        // lives INSIDE the pointee type and is what the callee's
                        // signature names.
                        const bool cast_to_raw =
                            s.rv.k == bir::Rvalue::K::Op && s.rv.what == "cast" &&
                            s.dest.proj.empty() && b_.locals[s.dest.local].type &&
                            TypeRef(b_.locals[s.dest.local].type).kind() == LogosType::Kind::Ptr;
                        for (auto& op : s.rv.ops) {
                            read_operand(op, p);
                            if (cast_to_raw) continue;
                            if (op.k != bir::Operand::K::Const && op.place.valid()) {
                                OList src = place_origins(op.place);
                                if (s.rv.k == bir::Rvalue::K::Use) relate(src, dst, p);
                                else if (auto sub = aggregate_slot(s, size_t(&op - s.rv.ops.data()), dst)) relate(src, *sub, p);
                                else { src.exact = false; relate(src, dst, p); }
                            }
                        }
                        if (s.rv.k == bir::Rvalue::K::RawAddr) use_place(s.rv.place, p);
                    }
                    // WHAT THE CLOSURE BODY REQUIRES OF THIS FRAME. The
                    // construction is where the parent hands the body its
                    // captures, so it is where the body's answer comes back:
                    // capture a's loan must end up inside capture b's region.
                    if (s.rv.k == bir::Rvalue::K::Aggregate && creqs_ && !s.rv.what.empty()) {
                        auto it = creqs_->find(s.rv.what);
                        if (it != creqs_->end())
                            for (auto& [ia, ib] : it->second) {
                                if (ia >= s.rv.ops.size() || ib >= s.rv.ops.size()) continue;
                                const bir::Place& pa = s.rv.ops[ia].place;
                                const bir::Place& pb = s.rv.ops[ib].place;
                                if (!pa.valid() || !pb.valid()) continue;
                                OList oa = place_origins(pa), ob = place_origins(pb);
                                // A by-reference capture's operand IS the
                                // reference; the body writes into its POINTEE,
                                // so the target regions start one past the
                                // reference's own.
                                const size_t skip = pb.proj.empty() &&
                                                    b_.locals[pb.local].synth_ref ? 1u : 0u;
                                if (oa.os.empty() || ob.os.size() <= skip) continue;
                                for (size_t k = skip; k < ob.os.size(); ++k)
                                    subset(oa.os[0], ob.os[k], p);
                            }
                    }
                    write_place(s.dest, p);
                    break;
                }
                case bir::Stmt::K::Read:
                    for (auto& op : s.rv.ops) read_operand(op, p);
                    break;
                case bir::Stmt::K::StorageDead:
                    access(p, {s.local, {}}, Access::StorageDead);
                    row("var_defined_at", {var(s.local), pt(p)});
                    // A dead local stops existing: its initialisation state ends
                    // here (a loop body's `let` is a first assignment on every
                    // iteration). See var_dead_at in polonius.dl.
                    row("var_dead_at", {var(s.local), pt(p)});
                    break;
                case bir::Stmt::K::Drop:
                    access(p, s.dest, Access::WriteDeep);
                    if (s.dest.proj.empty()) row("var_dropped_at", {var(s.dest.local), pt(p)});
                    else use_place(s.dest, p);
                    if (!has_borrowing_deref(s.dest))
                        row("path_moved_at_base", {pa(path_of(s.dest)), pt(p)});
                    break;
            }
        }
        if (!blk.terminated) continue;
        uint32_t p = M(bi, blk.stmts.size());
        const auto& t = blk.term;
        switch (t.k) {
            case bir::Term::K::Branch:
                read_operand(t.discr, p);
                break;
            case bir::Term::K::Return:
                use_var(0, p);
                break;
            case bir::Term::K::Call: {
                for (auto& a : t.args) read_operand(a, p);
                // Two-phase activations happen here.
                for (uint32_t i = 0; i < loans_.size(); ++i)
                    if (loans_[i].activation == p) access(p, loans_[i].place, Access::WriteDeep, int32_t(i));
                if (t.dest.valid()) {
                    OList dst = place_origins(t.dest);
                    if (!relate_call(t, dst, p))
                    for (size_t ai = 0; ai < t.args.size(); ++ai) {
                        const auto& a = t.args[ai];
                        if (a.k == bir::Operand::K::Const || !a.place.valid()) continue;
                        bool reaches = t.has_sig ? (ai < 64 && (t.sig_to_result >> ai) & 1u)
                                     : (!t.flow || !t.flow->available ||
                                        (ai < 64 && (t.flow->to_result >> ai) & 1u));
                        if (reaches) { OList src = place_origins(a.place); src.exact = false; relate(src, dst, p); }
                    }
                    // A CLOSURE CALL CARRIES WHAT THE CLOSURE HOLDS. The
                    // per-argument mask above can only keep or drop the whole
                    // receiver, and dropping it drops the captures with the
                    // autoref. Relate the receiver's origins PAST the autoref:
                    // those are the closure's own, and `Self::Output` may hold
                    // them.
                    if (t.closure_recv >= 0 && !t.args.empty() &&
                        t.args[0].k != bir::Operand::K::Const && t.args[0].place.valid()) {
                        OList rec = place_origins(t.args[0].place);
                        OList inner;
                        for (size_t k = size_t(t.closure_recv); k < rec.os.size(); ++k) {
                            inner.os.push_back(rec.os[k]);
                            inner.inv.push_back(rec.inv[k]);
                        }
                        if (!inner.os.empty()) { inner.exact = false; relate(inner, dst, p); }
                    }
                    // ⚠ AN INDIRECT CALL HAS NO BODY TO SUMMARISE, so its
                    // out-param flow is the CONSERVATIVE one: anything handed
                    // to it may be stored through any argument it can write
                    // through. `let g: fn(&C, &mut Vec<B>) = stash2; g(&c, &mut
                    // vs); c.bump();` held a loan of `c` inside `vs` and went
                    // unrefused, because this loop is gated on a summary a fn
                    // POINTER cannot have — the pointer could be any function
                    // with that signature. The mask comes from the TYPE, the
                    // only thing the call site knows.
                    if (!t.flow && t.has_sig && t.sig_mut_params) {
                        for (size_t j = 0; j < t.args.size() && j < 64; ++j) {
                            if (!((t.sig_mut_params >> j) & 1u)) continue;
                            if (!t.args[j].place.valid()) continue;
                            OList dstj = place_origins(t.args[j].place);
                            if (dstj.os.empty()) continue;
                            for (size_t i2 = 0; i2 < t.args.size(); ++i2) {
                                if (i2 == j) continue;
                                const auto& ai = t.args[i2];
                                if (ai.k == bir::Operand::K::Const || !ai.place.valid()) continue;
                                OList src = place_origins(ai.place);
                                if (src.os.empty()) continue;
                                src.exact = false;
                                relate(src, dstj, p);
                            }
                        }
                    }
                    // Out-params: argument i stored behind `&mut` argument j.
                    if (t.flow && t.flow->available)
                        for (size_t j = 0; j < t.flow->to_outparam.size() && j < t.args.size(); ++j) {
                            uint64_t mask = t.flow->to_outparam[j];
                            if (!mask || !t.args[j].place.valid()) continue;
                            // Only a parameter whose DECLARED type can be written
                            // through is an out-parameter (a `&mut`, or a value
                            // holding one). A generic `value: T` is moved in, and
                            // the generic body cannot write through it, whatever
                            // `T` is at this call (Vec::push with T = &mut X).
                            if (t.callee_fn) {
                                auto ps = t.callee_fn.params();
                                if (j < ps.size()) {
                                    TypeRef dt = ps[j].decl_type(pool_) ? ps[j].decl_type(pool_) : ps[j].type(pool_);
                                    bool writable = dt && !dt.raw_fat() &&
                                        (((dt.kind() == LogosType::Kind::Slice || dt.kind() == LogosType::Kind::DstRef) &&
                                          dt.mut_ptr()) || bc_holds_mut_ref_type(ts_, dt));
                                    if (!writable) continue;
                                }
                            }
                            const bir::Place& jp = t.args[j].place;
                            OList into = place_origins(jp.with(bir::Proj(bir::Proj::K::Deref)));
                            TypeRef pointee;
                            if (jp.proj.empty()) {
                                if (b_.locals[jp.local].synth_ref) pointee = b_.locals[jp.local].synth_pointee;
                                else if (TypeRef jt = b_.locals[jp.local].type;
                                         jt && (jt.kind() == LogosType::Kind::MutRef || jt.kind() == LogosType::Kind::Ref))
                                    pointee = jt.pointee();
                            }
                            for (size_t ai = 0; ai < t.args.size() && ai < 64; ++ai) {
                                if (!((mask >> ai) & 1u) || !t.args[ai].place.valid()) continue;
                                // An argument is never stored behind ITS OWN
                                // reference: `&'s mut v` flowing into `*v` would
                                // make `v` hold a loan of itself, which Rust's
                                // region check rejects in the callee.
                                if (ai == j) continue;
                                const bir::Place& ap = t.args[ai].place;
                                OList src = place_origins(ap);
                                TypeRef st;
                                if (ap.proj.empty()) {
                                    if (b_.locals[ap.local].synth_ref) {
                                        auto sp = b_.locals[ap.local].synth_pointee;
                                        (void)sp;   // a synthetic reference: its own type is not recorded
                                    } else {
                                        st = b_.locals[ap.local].type;
                                    }
                                }
                                if (relate_into_type_arg(src, st, into, pointee, p)) continue;
                                OList srcx = src; srcx.exact = false;
                                OList intox = into; intox.exact = false;
                                relate(srcx, intox, p);
                            }
                        }
                    write_place(t.dest, p);
                }
                break;
            }
            default:
                break;
        }
    }
    invalidations();
    initialization();

    // Into the database.
    const dl::Program& prog = db.program();
    std::vector<dl::Value> vals;
    for (auto& [rel, vs] : rows_) {
        auto r = prog.relation(rel);
        if (!r) continue;
        vals.clear();
        for (auto& v : vs) vals.push_back(syms.intern(v));
        db.insert(*r, vals);
    }
}

// ── Running the rules ────────────────────────────────────────────────────────

static std::string bir_dump(const bir::Body& b) {
    auto place = [&](const bir::Place& p) {
        if (!p.valid()) return std::string("_");
        std::string s = b.locals[p.local].name;
        for (auto& pj : p.proj) {
            if (pj.k == bir::Proj::K::Deref) s = "(*" + s + ")";
            else if (pj.k == bir::Proj::K::Field) s += "." + pj.field;
            else s += "[" + (pj.index == bir::kNoLocal ? std::string("c") : b.locals[pj.index].name) + "]";
        }
        return s;
    };
    auto opnd = [&](const bir::Operand& o) {
        switch (o.k) {
            case bir::Operand::K::Const: return std::string("const");
            case bir::Operand::K::Copy:  return "copy " + place(o.place);
            default:                     return "move " + place(o.place);
        }
    };
    std::string out = "bir " + b.name + "\n";
    for (size_t i = 0; i < b.locals.size(); ++i) {
        const auto& l = b.locals[i];
        const char* k = l.kind == bir::LocalKind::Return ? "ret" : l.kind == bir::LocalKind::Param ? "param"
                      : l.kind == bir::LocalKind::User ? "let" : "tmp";
        std::string extra;
        if (l.type && (l.type.kind() == LogosType::Kind::Enum || l.type.kind() == LogosType::Kind::Struct))
            extra = std::format("  [targs={} lts={} name={}]", l.type.type_args().size(),
                                l.type.lifetime_args().size(),
                                l.type.kind() == LogosType::Kind::Enum ? std::string(l.type.enum_name())
                                                                       : std::string(l.type.struct_name()));
        out += std::format("  {} {}{}: {}{}\n", k, l.is_mut ? "mut " : "", l.name,
                           l.type ? std::string(type_str(l.type)) : std::string("?"), extra);
    }
    for (size_t bi = 0; bi < b.blocks.size(); ++bi) {
        out += std::format("  bb{}:\n", bi);
        for (auto& s : b.blocks[bi].stmts) {
            switch (s.k) {
                case bir::Stmt::K::Assign: {
                    std::string rv;
                    if (s.rv.k == bir::Rvalue::K::Ref)
                        rv = std::format("{}{} {}", s.rv.what, s.rv.two_phase ? "(2ph)" : "", place(s.rv.place));
                    else if (s.rv.k == bir::Rvalue::K::RawAddr) rv = "raw " + place(s.rv.place);
                    else {
                        rv = s.rv.k == bir::Rvalue::K::Use ? "" : s.rv.what + "(";
                        for (size_t i = 0; i < s.rv.ops.size(); ++i) rv += (i ? ", " : "") + opnd(s.rv.ops[i]);
                        if (s.rv.k != bir::Rvalue::K::Use) rv += ")";
                    }
                    out += std::format("    {} = {}    @{}\n", place(s.dest), rv, s.line);
                    break;
                }
                case bir::Stmt::K::StorageDead: out += "    dead " + b.locals[s.local].name + "\n"; break;
                case bir::Stmt::K::Drop:        out += "    drop " + place(s.dest) + "\n"; break;
                case bir::Stmt::K::Read: {
                    out += "    read";
                    for (auto& o : s.rv.ops) out += " " + opnd(o);
                    out += "\n";
                    break;
                }
            }
        }
        const auto& t = b.blocks[bi].term;
        std::string tg;
        for (auto x : t.targets) tg += std::format(" bb{}", x);
        switch (t.k) {
            case bir::Term::K::Goto:   out += "    goto" + tg + "\n"; break;
            case bir::Term::K::Branch: out += "    branch " + opnd(t.discr) + " ->" + tg + "\n"; break;
            case bir::Term::K::Return: out += "    return\n"; break;
            case bir::Term::K::Unreachable: out += "    unreachable\n"; break;
            case bir::Term::K::Call: {
                std::string as;
                for (size_t i = 0; i < t.args.size(); ++i) as += (i ? ", " : "") + opnd(t.args[i]);
                std::string rs = t.callee_fn ? std::string(t.callee_fn.name()) : std::string("-");
                bool decl = t.callee_fn && t.callee_fn.decl_ret_type(nullptr).addr() !=
                                               t.callee_fn.ret_type(nullptr).addr();
                out += std::format("    {} = call {}({}) ->{}    @{}   [resolved={} decl={}]\n", place(t.dest),
                                   t.callee, as, tg, t.line, rs, decl ? "yes" : "no");
                break;
            }
        }
    }
    for (auto& u : b.unsupported) out += "  unsupported: " + u + "\n";
    return out;
}

struct BirVerdict {
    std::vector<std::string> unsupported;
    std::vector<std::string> errors;
    bool ran = false;
};

static const dl::Program& bc_rules(dl::Symbols*& syms_out) {
    static dl::Symbols syms;
    static const dl::Program prog = [] {
        dl::IncludeResolver inc = [](std::string_view n) -> std::optional<std::string> {
            if (auto t = dl::embedded_rule_file(n)) return std::string(*t);
            return std::nullopt;
        };
        auto text = dl::embedded_rule_file("logos_bc.dl");
        if (!text) { std::fprintf(stderr, "logosc: internal: logos_bc.dl is not embedded\n"); std::abort(); }
        auto p = dl::Program::parse(*text, "logos_bc.dl", syms, inc);
        if (!p) { std::fprintf(stderr, "logosc: internal: %s\n", p.error().str().c_str()); std::abort(); }
        return std::move(*p);
    }();
    syms_out = &syms;
    return prog;
}

// `LOGOS_BC_SLOW=<ms>` names every function whose check costs more than that,
// with the size of the body it cost it on. A whole-module time says the checker
// is slow; only a per-function one says WHERE, and the shape of the blow-up
// (locals, loans, points) is the first thing a cubic rule set shows.
struct BirSlowTimer {
    const bir::Body& b; std::chrono::steady_clock::time_point t0; long thresh;
    // What actually drives the rules' cost: `locals` and `stmts` say how big the
    // SOURCE is, and two sibling functions of the same size can differ 200x.
    size_t loans = 0, origins = 0, points = 0, subset = 0, ocl = 0, rounds = 0, derived = 0;
    explicit BirSlowTimer(const bir::Body& body) : b(body), thresh(threshold()) {
        if (thresh >= 0) t0 = std::chrono::steady_clock::now();
    }
    // Read ONCE: this runs per function, and the probe must not be a cost of
    // its own in the path it is there to price.
    static long threshold() {
        static const long t = [] {
            const char* e = std::getenv("LOGOS_BC_SLOW");
            return e ? std::atol(e) : -1L;
        }();
        return t;
    }
    ~BirSlowTimer() {
        if (thresh < 0) return;
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                      std::chrono::steady_clock::now() - t0).count();
        if (ms >= thresh) {
            size_t stmts = 0;
            for (auto& blk : b.blocks) stmts += blk.stmts.size();
            std::fprintf(stderr, "bc-slow: %7lld ms  loans=%-5zu origins=%-6zu points=%-6zu "
                                 "SUBSET=%-8zu ocl=%-8zu ROUNDS=%-6zu DERIVED=%-10zu stmts=%-5zu %s\n",
                         (long long)ms, loans, origins, points, subset, ocl,
                         rounds, derived, stmts, b.name.c_str());
            std::fflush(stderr);  // redirected stderr is BLOCK-buffered: without
                                  // this the list only appears at exit, which is
                                  // useless for watching a run that takes 20 min
        }
    }
};

// Check ONE body and append what it says to `v`. A function and a closure are
// the same question asked of different bodies, so they run the same rules.
// What one closure body requires of the frame that built it: pairs of CAPTURE
// INDICES (a, b) meaning "a loan of capture a's place reaches capture b". Read
// off the body's own statements — a small flow over locals, not a second
// analysis — and closed over an INNER closure's requirements, whose operands
// name this body's captures.
static void closure_requirements(const bir::Body& b, ClosureReqs& out) {
    if (!b.is_closure || !b.n_captures || b.name.size() <= 8) return;
    auto cap = [&](bir::LocalId l) { return l >= 1 && l <= b.n_captures ? int(l) - 1 : -1; };
    std::map<bir::LocalId, std::set<uint32_t>> carry;   // local -> captures it holds a loan of
    std::map<bir::LocalId, int> desig;                  // local -> the capture it points AT
    auto& reqs = out[b.name.substr(8)];
    auto note = [&](const std::set<uint32_t>& from, int to) {
        if (to < 0) return;
        for (uint32_t f : from) if (f != uint32_t(to)) reqs.emplace(f, uint32_t(to));
    };
    // A loop's back edge can make a carry visible only on a second reading, so
    // this repeats until nothing new is learned.
    for (int round = 0; round < 4; ++round) {
        const size_t before = reqs.size();
        size_t carried = 0;
        for (auto& [l, cs] : carry) carried += cs.size();
        for (auto& blk : b.blocks) for (auto& st : blk.stmts) {
            if (st.k != bir::Stmt::K::Assign) continue;
            std::set<uint32_t> src;
            if (st.rv.k == bir::Rvalue::K::Ref) {
                if (int c = cap(st.rv.place.local); c >= 0) src.insert(uint32_t(c));
                if (auto it = carry.find(st.rv.place.local); it != carry.end())
                    src.insert(it->second.begin(), it->second.end());
                if (st.dest.proj.empty()) desig[st.dest.local] = cap(st.rv.place.local);
            } else {
                for (auto& op : st.rv.ops)
                    if (op.place.valid())
                        if (auto it = carry.find(op.place.local); it != carry.end())
                            src.insert(it->second.begin(), it->second.end());
                if (st.rv.k == bir::Rvalue::K::Aggregate && !st.rv.what.empty())
                    if (auto ir = out.find(st.rv.what); ir != out.end())
                        for (auto& [ia, ib] : ir->second) {
                            if (ia >= st.rv.ops.size() || ib >= st.rv.ops.size()) continue;
                            const bir::Place& pa = st.rv.ops[ia].place;
                            const bir::Place& pb = st.rv.ops[ib].place;
                            if (!pa.valid() || !pb.valid()) continue;
                            std::set<uint32_t> from;
                            if (auto it = carry.find(pa.local); it != carry.end()) from = it->second;
                            if (int c = cap(pa.local); c >= 0) from.insert(uint32_t(c));
                            auto d = desig.find(pb.local);
                            note(from, d != desig.end() ? d->second : cap(pb.local));
                        }
            }
            if (src.empty()) continue;
            note(src, cap(st.dest.local));
            carry[st.dest.local].insert(src.begin(), src.end());
        }
        size_t after = 0;
        for (auto& [l, cs] : carry) after += cs.size();
        if (reqs.size() == before && after == carried) break;
    }
    if (reqs.empty()) out.erase(b.name.substr(8));
}

static void bir_check_body(bir::Body& body, const lir::LProgram& prog,
                           const TypeSets& ts, BirVerdict& v,
                           const ClosureReqs* creqs = nullptr) {
    BirSlowTimer slow_timer_(body);
    const char* dump = std::getenv("LOGOS_BIR_DUMP");
    bool want_dump = dump && body.name.find(dump) != std::string::npos;
    if (want_dump) std::fputs(bir_dump(body).c_str(), stderr);

    dl::Symbols* syms = nullptr;
    const dl::Program& rules = bc_rules(syms);
    // One database per thread, cleared between functions: the rules are
    // planned and the relations' memory allocated once, not per function.
    thread_local std::unique_ptr<dl::Database> tl_db;
    if (!tl_db || &tl_db->program() != &rules)
        tl_db = std::make_unique<dl::Database>(rules, *syms, /*provenance=*/want_dump);
    else
        tl_db->clear(/*provenance=*/want_dump);
    dl::Database& db = *tl_db;
    BirFacts facts(body, prog, ts, creqs);
    facts.emit(db, *syms);
    slow_timer_.loans   = facts.loans().size();
    slow_timer_.origins = facts.n_origins();
    slow_timer_.points  = facts.n_points();

    if (const char* dir = std::getenv("LOGOS_DL_DUMP"); dir && (!dump || want_dump)) {
        std::string sub = body.name;
        for (char& c : sub) if (!std::isalnum(static_cast<unsigned char>(c))) c = '_';
        std::filesystem::path d = std::filesystem::path(dir) / sub.substr(0, 120);
        std::error_code ec;
        std::filesystem::create_directories(d, ec);
        std::map<std::string, std::string> files;
        for (uint32_t r = 0; r < rules.relations().size(); ++r)
            if (rules.relations()[r].input) files[rules.relations()[r].name];
        for (auto& [rel, vals] : facts.rows()) {
            std::string line;
            for (size_t i = 0; i < vals.size(); ++i) line += (i ? "\t" : "") + vals[i];
            files[rel] += line + "\n";
        }
        for (auto& [rel, text] : files) {
            if (std::FILE* f = std::fopen((d / (rel + ".facts")).c_str(), "w")) {
                std::fputs(text.c_str(), f);
                std::fclose(f);
            }
        }
    }

    db.run();
    v.ran = true;
    auto rel = [&](const char* n) -> const dl::Relation& { return db.relation(*rules.relation(n)); };
    slow_timer_.subset = rel("subset").size();
    slow_timer_.ocl    = rel("origin_contains_loan_on_entry").size();
    slow_timer_.rounds  = db.stats().rounds;
    slow_timer_.derived = db.stats().derived;
    auto sym = [&](dl::Value x) { return std::string(syms->name(x)); };
    auto num = [](const std::string& s) { return uint32_t(std::stoul(s.substr(1))); };
    {
        const dl::Relation& R = rel("errors");
        for (size_t i = 0; i < R.size(); ++i) {
            auto row = R.row(i);
            uint32_t L = num(sym(row[0])), P = num(sym(row[1]));
            const auto& loan = facts.loans()[L];
            // ⚠ THE WORDING IS RUSTC'S, NOT POLONIUS'S. "borrow ... is used
            // after an access ... invalidated it" describes the RULE that
            // fired; a reader needs to know what they did — borrowed again,
            // assigned, moved, or read — and that is exactly what the
            // invalidating access says. 1029 pinned diagnostics across the
            // corpus spell these, and rustc spells them the same way.
            const std::string nm = facts.place_str(loan.place);
            const uint32_t at = facts.point_line(P);
            std::string msg;
            switch (facts.invalidation_kind(L, P)) {
                case 1:
                    // A new SHARED borrow only conflicts with a mutable loan.
                    msg = std::format("cannot borrow '{}' as immutable because it is also "
                                      "borrowed as mutable", nm);
                    break;
                case 2:
                    msg = loan.mut_
                        ? std::format("cannot borrow '{}' as mutable more than once at a time", nm)
                        : std::format("cannot borrow '{}' as mutable because it is also "
                                      "borrowed as immutable", nm);
                    break;
                case 3: {
                    // A loan killed by a StorageDead in a block that RETURNS was
                    // still live at the return, and rustc splits that three ways
                    // by WHOSE datum escapes and HOW — the reader's mistake is a
                    // different one in each, and answering all three with
                    // "cannot return reference to local variable" was wrong on a
                    // function that returns nothing and borrows a PARAMETER.
                    //   param            → E0521 borrowed data escapes (out-param)
                    //   local, ret carries a region → E0515 cannot return reference
                    //   otherwise        → E0597 does not live long enough
                    const bir::LocalKind lk = body.locals[loan.place.local].kind;
                    const bool ret_carries =
                        bir_type_carries_loans(body.locals[0].type, prog, ts);
                    if (facts.at_return(P) && lk == bir::LocalKind::Param)
                        msg = std::format("borrowed data escapes outside of function: '{}'", nm);
                    else if (facts.at_return(P) && ret_carries)
                        msg = loan.place.proj.empty() && lk == bir::LocalKind::Temp
                            ? std::string("cannot return reference to temporary value: dangling reference")
                            : std::format("cannot return reference to local variable '{}': "
                                          "dangling reference", nm);
                    // A TEMPORARY HAS NO NAME THE READER WROTE. `_t3` is this
                    // lowering's own bookkeeping, and printing it asks the
                    // reader to find a local that does not exist in the source;
                    // rustc calls the same thing a temporary value (E0716).
                    else if (lk == bir::LocalKind::Temp && loan.place.proj.empty())
                        msg = "temporary value dropped while borrowed";
                    else
                        msg = std::format("'{}' does not live long enough: it is borrowed here and "
                                          "used after '{}' goes out of scope", nm, nm);
                    break;
                }
                case 4:  msg = std::format("cannot assign to '{}' because it is borrowed", nm); break;
                case 5:  msg = std::format("cannot move out of '{}' because it is borrowed", nm); break;
                case 6:  msg = std::format("cannot use '{}' because it was mutably borrowed", nm); break;
                default:
                    msg = loan.mut_
                        ? std::format("cannot use '{}' because it was mutably borrowed", nm)
                        : std::format("cannot borrow '{}' as mutable because it is also "
                                      "borrowed as immutable", nm);
            }
            v.errors.push_back(std::format("line {}: {}", at, msg));
        }
    }
    {
        const dl::Relation& R = rel("move_errors");
        for (size_t i = 0; i < R.size(); ++i) {
            auto row = R.row(i);
            v.errors.push_back(std::format("use of moved or uninitialised '{}' at line {}",
                                           facts.path_str(num(sym(row[0]))),
                                           facts.point_line(num(sym(row[1])))));
        }
    }
    {
        const dl::Relation& R = rel("assign_twice");
        for (size_t i = 0; i < R.size(); ++i) {
            auto row = R.row(i);
            v.errors.push_back(std::format("cannot assign twice to immutable '{}' at line {}",
                                           facts.path_str(num(sym(row[0]))),
                                           facts.point_line(num(sym(row[1])))));
        }
    }
    {
        const dl::Relation& R = rel("mut_errors");
        for (size_t i = 0; i < R.size(); ++i) {
            auto row = R.row(i);
            // rustc's wording, and the corpus's: the reader wants "you cannot
            // borrow this as mutable, because it was not declared `mut`".
            v.errors.push_back(std::format("line {}: cannot borrow '{}' as mutable, as it is "
                                           "not declared as mutable",
                                           facts.point_line(num(sym(row[1]))),
                                           body.locals[num(sym(row[0]))].name));
        }
    }
    {
        const dl::Relation& R = rel("deref_write_errors");
        for (size_t i = 0; i < R.size(); ++i) {
            auto row = R.row(i);
            v.errors.push_back(std::format("'{}' is written or mutably borrowed behind a `&` reference (line {})",
                                           body.locals[num(sym(row[0]))].name,
                                           facts.point_line(num(sym(row[1])))));
        }
    }
    {
        const dl::Relation& R = rel("move_ref_errors");
        for (size_t i = 0; i < R.size(); ++i) {
            auto row = R.row(i);
            v.errors.push_back(std::format("cannot move out of '{}', which is behind a reference (line {})",
                                           facts.path_str(num(sym(row[0]))),
                                           facts.point_line(num(sym(row[1])))));
        }
    }
    {
        const dl::Relation& R = rel("subset_errors");
        std::set<std::pair<std::string, std::string>> seen;
        for (size_t i = 0; i < R.size(); ++i) {
            auto row = R.row(i);
            if (!seen.emplace(sym(row[0]), sym(row[1])).second) continue;
            const uint32_t line = facts.point_line(num(sym(row[2])));
            // E0521 IS A SUBSET ERROR WITH TWO NAMES. Inside a closure the two
            // regions belong to a PARAMETER (bound by the call) and a CAPTURE
            // (the parent frame's), and rustc's headline for that pair is
            // "borrowed data escapes outside of closure" — the twin of the
            // function case two hundred lines up. Without the names the reader
            // got region numbers, which name nothing they wrote.
            const BirFacts::Universal* ua = facts.placeholder_owner(num(sym(row[0])));
            const BirFacts::Universal* ub = facts.placeholder_owner(num(sym(row[1])));
            if (ua && ub && body.is_closure &&
                !body.locals[ua->local].is_capture && body.locals[ub->local].is_capture) {
                v.errors.push_back(std::format(
                    "borrowed data escapes outside of closure: '{}' escapes the closure body "
                    "here, and '{}' is declared outside of it (line {})",
                    body.locals[ua->local].name, body.locals[ub->local].name, line));
                continue;
            }
            // RUSTC HAS TWO SENTENCES FOR A SUBSET BETWEEN UNIVERSAL REGIONS,
            // and which one it uses is decided by whether the SOURCE region was
            // written. Elided (`y: &i64` feeding a `-> &'a i64`): E0621,
            // "explicit lifetime required in the type of `y`", because the
            // repair is to write `'a` on `y`. Both written (`'b` feeding `'a`):
            // "lifetime may not live long enough", because the repair is a
            // BOUND, not an annotation. The region numbers this used to print
            // named nothing the programmer wrote.
            // A region sema MINTED for an elision (`'%1`) is not a name the
            // programmer wrote, so it may not appear in a suggested bound —
            // `consider adding the bound `'%1: 'a`` is unactionable. It counts
            // as elided, which is the branch that suggests an ANNOTATION.
            auto written = [](const std::string& l) {
                return l.size() > 1 && l[0] == '\'' && l[1] != '%';
            };
            const bool named_a = ua && written(ua->label);
            const bool named_b = ub && written(ub->label);
            if (named_a && named_b && ua->label != ub->label) {
                v.errors.push_back(std::format(
                    "lifetime may not live long enough: consider adding the bound `{}: {}` (line {})",
                    ua->label, ub->label, line));
                continue;
            }
            if (ua && !named_a && named_b && ua->local < body.locals.size() &&
                body.locals[ua->local].kind == bir::LocalKind::Param) {
                v.errors.push_back(std::format(
                    "explicit lifetime required in the type of '{}': lifetime {} required (line {})",
                    body.locals[ua->local].name, ub->label, line));
                continue;
            }
            v.errors.push_back(std::format("lifetime mismatch: data with region {} flows into region {}, "
                                           "which the signature does not allow (line {})",
                                           sym(row[0]), sym(row[1]), line));
        }
    }
    {
        const dl::Relation& R = rel("partial_reinit_errors");
        for (size_t i = 0; i < R.size(); ++i) {
            auto row = R.row(i);
            v.errors.push_back(std::format("assign to part of moved value '{}' at line {}",
                                           facts.path_str(num(sym(row[0]))),
                                           facts.point_line(num(sym(row[1])))));
        }
    }
    {
        const dl::Relation& R = rel("move_drop_errors");
        for (size_t i = 0; i < R.size(); ++i) {
            auto row = R.row(i);
            v.errors.push_back(std::format("cannot move out of '{}': its owner implements Drop (E0509, line {})",
                                           facts.path_str(num(sym(row[0]))),
                                           facts.point_line(num(sym(row[1])))));
        }
    }
    std::sort(v.errors.begin(), v.errors.end());
    v.errors.erase(std::unique(v.errors.begin(), v.errors.end()), v.errors.end());
    if (want_dump) {
        for (auto& e : v.errors) std::fprintf(stderr, "  bir-error: %s\n", e.c_str());
        const dl::Relation& R = rel("errors");
        for (size_t i = 0; i < R.size(); ++i)
            if (auto d = db.explain(*rules.relation("errors"), R.row(i), 64))
                std::fputs(db.render(*d).c_str(), stderr);
    }
}

static BirVerdict bir_check(lir_view::FunctionView fn, const lir::LProgram& prog,
                            const TypeSets& ts, const FnIndex& idx,
                            const FlowSummaryMap* flows) {
    BirVerdict v;
    BirBuilder builder(prog, ts, idx, flows);
    bir::Body body = builder.build(fn);
    v.unsupported = body.unsupported;
    if (!body.unsupported.empty()) return v;
    // ADR 0029 S5b — every closure the body mentions, and every closure THOSE
    // mention. A worklist rather than recursion so a closure inside a closure
    // is reached without the builder re-entering itself.
    // ⚠ LOWERED BEFORE ANYTHING IS CHECKED, AND THE PARENT IS CHECKED LAST.
    // A closure body's escape question is the parent's to answer
    // (closure_requirements), so the parent's facts need the bodies' answers
    // before they are emitted — and an inner closure's answer is an outer
    // one's input, which is why the requirements are read deepest-first.
    std::vector<lir_view::ExprRef> queue(builder.closures().begin(), builder.closures().end());
    std::vector<bir::Body> cbodies;
    for (size_t qi = 0; qi < queue.size(); ++qi) {
        BirBuilder cb(prog, ts, idx, flows);
        bir::Body cbody = cb.build_closure(queue[qi]);
        // ⚠ «CONSERVATIVE» HAS A DIRECTION. A closure body this lowering cannot
        // express makes the ENCLOSING function's verdict incomplete too, so the
        // whole function falls back to the old checker rather than reporting a
        // partial answer as a complete one.
        if (!cbody.unsupported.empty()) {
            v.unsupported = cbody.unsupported;
            v.errors.clear();
            return v;
        }
        for (auto x : cb.closures()) queue.push_back(x);
        cbodies.push_back(std::move(cbody));
    }
    ClosureReqs reqs;
    for (size_t i = cbodies.size(); i-- > 0;) closure_requirements(cbodies[i], reqs);
    bir_check_body(body, prog, ts, v, &reqs);
    for (auto& cbody : cbodies) bir_check_body(cbody, prog, ts, v, &reqs);
    std::sort(v.errors.begin(), v.errors.end());
    v.errors.erase(std::unique(v.errors.begin(), v.errors.end()), v.errors.end());
    return v;
}

// ── Shadow mode (LOGOS_DL_SHADOW=bc) ─────────────────────────────────────────
//
// Both checkers run on every post-mono function; the old checker's verdict is
// the one the compile acts on. Per process, disagreements are logged with both
// sides' messages, and a census line records how many functions were
// compared, skipped (and why), agreed and disagreed.

// ── LIVE mode (LOGOS_DL_BC=1) — ADR 0028 S6 ─────────────────────────────────
//
// The NEW checker's verdict is the one the compile acts on, and the old one
// does not run at all. A function the new checker cannot lower yet
// (`v.unsupported`, 13 of them corpus-wide) falls back to the old checker
// rather than going unchecked — the only thing that would be worse than either
// verdict is none.
// ADR 0028 S7: the new checker is the DEFAULT. `LOGOS_DL_BC=0` puts the old
// one back, which is an escape hatch for a bisect, not a supported mode — the
// corpus's diagnostics are pinned to the new checker's wording, so the old one
// now fails ~800 of them by text while agreeing about the verdict.
static bool dl_bc_live() {
    static const bool on = [] {
        const char* v = std::getenv("LOGOS_DL_BC");
        return !(v && std::string_view(v) == "0");
    }();
    return on;
}

static bool dl_shadow_bc() {
    static const bool on = [] {
        const char* v = std::getenv("LOGOS_DL_SHADOW");
        return v && std::string_view(v).find("bc") != std::string_view::npos;
    }();
    return on;
}

struct BcShadowCensus {
    size_t compared = 0, agree = 0, old_only = 0, new_only = 0;
    std::map<std::string, size_t> skipped;
};

static std::string shadow_input_name() {
    static const std::string input = [] {
        std::string cmd, arg;
        if (FILE* f = std::fopen("/proc/self/cmdline", "rb")) {
            int ch;
            while ((ch = std::fgetc(f)) != EOF) {
                if (ch == 0) {
                    if (arg.ends_with(".logos") || arg.ends_with(".module")) cmd = arg;
                    arg.clear();
                } else arg.push_back(static_cast<char>(ch));
            }
            std::fclose(f);
        }
        return cmd.empty() ? std::string("?") : cmd;
    }();
    return input;
}

static void shadow_log(const std::string& text) {
    const char* path = std::getenv("LOGOS_DL_SHADOW_LOG");
    FILE* out = path ? std::fopen(path, "a") : nullptr;
    std::fputs(text.c_str(), out ? out : stderr);
    if (out) std::fclose(out);
}
