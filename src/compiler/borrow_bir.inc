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
    bool        is_mut = false;
    uint32_t    slot   = ~0u;
    uint32_t    line   = 0;
};

struct Proj {
    enum class K : uint8_t { Deref, Field, Index };
    Proj() = default;
    explicit Proj(K kind, std::string f = {}) : k(kind), field(std::move(f)) {}
    K           k = K::Deref;
    std::string field;              // Field: name, tuple index or `Variant#i`
    LocalId     index = kNoLocal;   // Index: the index operand's local, if any
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
    // The resolved callee, whose DECLARED signature (decl_ret_type /
    // decl_type) says which argument positions the result borrows from.
    lir_view::FunctionView callee_fn;
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
                                   int depth = 0) {
    if (!t || depth > 16) return false;
    switch (t.kind()) {
        case LogosType::Kind::Ref: case LogosType::Kind::MutRef: case LogosType::Kind::Closure:
            return true;
        case LogosType::Kind::Slice: return !t.owning_slice() || bir_type_carries_loans(t.elem(), prog, ts, depth + 1);
        case LogosType::Kind::TraitObject: return !t.owning_trait_object();
        case LogosType::Kind::DstRef: return !t.owning_dst();
        case LogosType::Kind::Array: return bir_type_carries_loans(t.elem(), prog, ts, depth + 1);
        case LogosType::Kind::Tuple:
            for (auto& e : t.tuple_elems()) if (bir_type_carries_loans(e, prog, ts, depth + 1)) return true;
            return false;
        case LogosType::Kind::Struct:
            if (!t.lifetime_args().empty()) return true;
            if (bc_holds_any_ref_type(ts, t) || bc_loan_carrying_type(ts, t)) return true;
            for (auto& a : t.type_args()) if (bir_type_carries_loans(a, prog, ts, depth + 1)) return true;
            return false;
        case LogosType::Kind::Enum: {
            if (!t.lifetime_args().empty()) return true;
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
        new_local("_ret", bir::LocalKind::Return, fn.ret_type(pool_), true, ~0u, 0);
        cur_ = new_block();
        push_scope();
        fn.each_param([&](lir_view::LParamView p) {
            bir::LocalId l = new_local(std::string(p.name()), bir::LocalKind::Param,
                                       p.type(pool_), true, p.slot(), 0);
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
        declare(l);
        return l;
    }
    bir::LocalId synth_ref_temp(TypeRef pointee) {
        bir::LocalId l = temp({});
        body_.locals[l].synth_ref = true;
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
    void assign(bir::Place dest, bir::Rvalue rv) {
        // `x = h(move x)` as one statement would move and initialise x at the
        // same point; route the value through a temporary so the move comes
        // first.
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
        return !is_move_type(t, prog_, ts_);
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
        if (!e) return std::nullopt;
        switch (e.kind()) {
            case ECode::VarRef: {
                lir_view::EVarRefView v{e};
                bir::LocalId l = find_local(v.name(), v.var_slot());
                if (l == bir::kNoLocal) return std::nullopt;   // a global / const
                return bir::Place{l, {}};
            }
            case ECode::Deref: {
                auto inner = lir_view::EDerefView{e}.operand();
                bir::Place p = place_or_temp(inner);
                return p.with(bir::Proj(bir::Proj::K::Deref));
            }
            case ECode::FieldRead: {
                lir_view::EFieldReadView v{e};
                bir::Place p = place_or_temp(v.receiver());
                if (is_pointer_kind(v.receiver().type(pool_))) p = p.with(bir::Proj(bir::Proj::K::Deref));
                return p.with(bir::Proj(bir::Proj::K::Field, std::string(v.field())));
            }
            case ECode::TupleIndex: {
                lir_view::ETupleIndexView v{e};
                bir::Place p = place_or_temp(v.receiver());
                if (is_pointer_kind(v.receiver().type(pool_))) p = p.with(bir::Proj(bir::Proj::K::Deref));
                return p.with(bir::Proj(bir::Proj::K::Field, std::to_string(v.index())));
            }
            case ECode::IndexRead: {
                lir_view::EIndexReadView v{e};
                TypeRef rt = v.receiver().type(pool_);
                bir::Place p = place_or_temp(v.receiver());
                if (is_pointer_kind(rt) || (rt && rt.kind() == LogosType::Kind::Slice))
                    p = p.with(bir::Proj(bir::Proj::K::Deref));
                bir::Proj ix(bir::Proj::K::Index);
                ix.index = operand_local(v.index());
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
                // `&mut *x.deref()`: the deref feeding a mutable borrow is a
                // `deref_mut`, whatever the L-IR spelled (sema may emit the
                // shared one for a mutable receiver).
                bool saved = mut_deref_ctx_;
                mut_deref_ctx_ = v.is_mut();
                bir::Place p = place_or_temp(v.inner());
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
                rv.ops.push_back(operand(lir_view::ECastView{e}.operand()));
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
                lir_view::EStructLitView{e}.each_field_value([&](lir_view::ExprRef fv) {
                    rv.ops.push_back(operand(fv));
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
            case ECode::ClosureBox:
                unsupported("expr:ClosureBox");
                assign(dest, bir::Rvalue(bir::Rvalue::K::Use));
                return;
            case ECode::PackExpand:
                unsupported("expr:PackExpand");
                assign(dest, bir::Rvalue(bir::Rvalue::K::Use));
                return;
            case ECode::WritLit:
                unsupported("expr:WritLit");
                assign(dest, bir::Rvalue(bir::Rvalue::K::Use));
                return;
            case ECode::ReflectOf:
                unsupported("expr:ReflectOf");
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

    // A `&mut T` place handed to a reference parameter is reborrowed
    // (`&*p` / `&mut *p`), never moved: Rust's implicit reborrow at a
    // coercion site. Returns false when `a` is not such a place.
    bool reborrow_arg(lir_view::ExprRef a, TypeRef param, bir::Term& t,
                      std::vector<std::pair<bir::BlockId, uint32_t>>& reservations,
                      int kind_hint = 0) {
        if (!a) return false;
        TypeRef at = a.type(pool_);
        if (!at || at.kind() != LogosType::Kind::MutRef) return false;
        bool to_ref = !param || param.kind() == LogosType::Kind::Ref ||
                      param.kind() == LogosType::Kind::MutRef;
        if (!to_ref) return false;
        auto p = place_of(a);
        if (!p) return false;
        // Unknown parameter: the receiver kind from the key when there is one,
        // else a mutable reborrow (what a `&mut` argument most often meets).
        bool m = param ? param.kind() == LogosType::Kind::MutRef : kind_hint != 1;
        bir::LocalId tmp = synth_ref_temp(at.pointee());
        assign({tmp, {}}, ref_of(p->with(bir::Proj(bir::Proj::K::Deref)), m, m));
        if (m) reservations.emplace_back(cur_, uint32_t(body_.blocks[cur_].stmts.size() - 1));
        t.args.push_back(read_place({tmp, {}}, {}));
        return true;
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
            // An implicit reborrow of a `&mut` argument (AddrOfTemp(Deref r))
            // is an adjustment, which rustc allows as two-phase; an explicit
            // `&mut x` argument is not.
            if (a && a.kind() == ECode::AddrOfTemp && lir_view::EAddrOfTempView{a}.is_mut()) {
                lir_view::EAddrOfTempView v{a};
                if (v.inner() && v.inner().kind() == ECode::Deref) {
                    bool saved = mut_deref_ctx_;
                    mut_deref_ctx_ = true;
                    bir::Place p = place_or_temp(v.inner());
                    mut_deref_ctx_ = saved;
                    bir::LocalId tmp = temp(a.type(pool_));
                    assign({tmp, {}}, ref_of(std::move(p), true, true));
                    reservations.emplace_back(cur_, uint32_t(body_.blocks[cur_].stmts.size() - 1));
                    t.args.push_back(read_place({tmp, {}}, a.type(pool_)));
                    return;
                }
            }
            t.args.push_back(operand(a));
        };
        switch (e.kind()) {
            case ECode::Call: {
                lir_view::ECallView v{e};
                t.callee = std::string(v.callee());
                if (flows_) t.flow = resolve_call_flow(*flows_, v.callee(), &idx_);
                std::vector<TypeRef> ptypes;
                if (auto it = idx_.by_name.find(std::string(v.callee())); it != idx_.by_name.end()) {
                    for (auto& pv : it->second.params()) ptypes.push_back(pv.type(pool_));
                    t.has_sig = true;
                    t.sig_to_result = signature_mask(it->second);
                    t.callee_fn = it->second;
                }
                size_t ai = 0;
                v.each_arg([&](lir_view::ExprRef a) {
                    TypeRef pt = ai < ptypes.size() ? ptypes[ai] : TypeRef{};
                    ++ai;
                    if (reborrow_arg(a, pt, t, reservations)) return;
                    if (raw_addr_arg(a, pt, t)) return;
                    // A reference parameter given a non-reference place: the
                    // autoref codegen inserts is a borrow of that place.
                    if (pt && (pt.kind() == LogosType::Kind::Ref || pt.kind() == LogosType::Kind::MutRef) &&
                        a && !is_pointer_kind(a.type(pool_)) &&
                        a.type(pool_) && a.type(pool_).kind() != LogosType::Kind::Slice &&
                        a.kind() != ECode::AddrOf && a.kind() != ECode::AddrOfTemp) {
                        if (auto p = place_of(a)) {
                            bir::LocalId tmp = temp(pt);
                            assign({tmp, {}}, ref_of(*p, pt.kind() == LogosType::Kind::MutRef, false));
                            t.args.push_back(read_place({tmp, {}}, pt));
                            return;
                        }
                    }
                    arg_value(a);
                });
                break;
            }
            case ECode::MethodCall: {
                lir_view::EMethodCallView v{e};
                t.callee = std::string(v.resolved_symbol().empty() ? v.method() : v.resolved_symbol());
                if (flows_) t.flow = resolve_method_flow(*flows_, idx_, pool_, v);
                if (auto f = resolve_method(v)) {
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
                    bir::LocalId tmp = synth_ref_temp(a.inner().type(pool_));
                    assign({tmp, {}}, ref_of(std::move(p), m, m));
                    if (m)
                        reservations.emplace_back(cur_, uint32_t(body_.blocks[cur_].stmts.size() - 1));
                    t.args.push_back(read_place({tmp, {}}, rcv.type(pool_)));
                } else if (rcv && sk == 3) {
                    auto f = resolve_method(v);
                    TypeRef p0 = f && !f.params_empty() ? f.params()[0].type(pool_) : TypeRef{};
                    if (p0 ? !raw_addr_arg(rcv, p0, t) : !raw_addr_place(rcv, t)) arg_value(rcv);
                } else if (rcv && sk >= 1 && !is_pointer_kind(rcv.type(pool_))) {
                    // A bare place passed to `&self` / `&mut self`.
                    bir::Place p = place_or_temp(rcv);
                    bir::LocalId tmp = synth_ref_temp(rcv.type(pool_));
                    assign({tmp, {}}, ref_of(std::move(p), sk == 2, sk == 2));
                    if (sk == 2)
                        reservations.emplace_back(cur_, uint32_t(body_.blocks[cur_].stmts.size() - 1));
                    t.args.push_back(read_place({tmp, {}}, {}));
                } else if (rcv) {
                    TypeRef p0;
                    if (auto f = resolve_method(v); f && !f.params_empty()) p0 = f.params()[0].type(pool_);
                    if (!reborrow_arg(rcv, p0, t, reservations, sk)) arg_value(rcv);
                }
                {
                    std::vector<TypeRef> ptypes;
                    if (auto f = resolve_method(v))
                        for (auto& pv : f.params()) ptypes.push_back(pv.type(pool_));
                    size_t ai = 1;
                    v.each_arg([&](lir_view::ExprRef a) {
                        TypeRef pt = ai < ptypes.size() ? ptypes[ai] : TypeRef{};
                        ++ai;
                        if (reborrow_arg(a, pt, t, reservations)) return;
                        if (raw_addr_arg(a, pt, t)) return;
                        arg_value(a);
                    });
                }
                break;
            }
            case ECode::ClosureCall: {
                lir_view::EClosureCallView v{e};
                t.callee = "<closure>";
                t.args.push_back(as_read(operand(v.callee())));
                v.each_arg(arg_value);
                break;
            }
            case ECode::FnPtrCall: {
                lir_view::EFnPtrCallView v{e};
                t.callee = "<fnptr>";
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
        t.targets = {next};
        terminate(std::move(t));
        bir::BlockId call_block = cur_;   // terminate() acted on cur_
        for (auto [b, st] : reservations) body_.two_phase.push_back({b, st, call_block});
        cur_ = next;
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
                if (!t.owning_slice()) elided = true;
                lifetimes_in(t.elem(), names, elided, depth + 1);
                return;
            case LogosType::Kind::TraitObject:
                if (!t.owning_trait_object()) elided = true;
                return;
            case LogosType::Kind::DstRef:
                if (!t.owning_dst()) elided = true;
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
    // The callee of a method call: by its resolved symbol, else by the mono
    // template key's bare spelling, else by the method's base name when every
    // candidate agrees on the receiver's reference kind (the same fallbacks
    // the counter-based checker's method_self_kind uses).
    lir_view::FunctionView resolve_method(lir_view::EMethodCallView v) const {
        if (auto it = idx_.by_name.find(std::string(v.resolved_symbol())); it != idx_.by_name.end())
            return it->second;
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
                        if (std::string(f.name()).find(needle) != std::string::npos) return f;
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
                if (auto f = agree(it->second)) return f;
        if (auto it = idx_.by_base.find(std::string(v.method())); it != idx_.by_base.end())
            return agree(it->second);
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
        bir::Operand v = operand(value);
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
            bir::Stmt d(bir::Stmt::K::Drop);
            d.dest = dest;
            emit(std::move(d));
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
        if (is_pointer_kind(t)) { p = p.with(bir::Proj(bir::Proj::K::Deref)); t = t.pointee(); }
        p = p.with(bir::Proj(bir::Proj::K::Field, std::string(field)));
        t = field_type(t, field);
        return p;
    }

    void lower_stmt(lir_view::StmtRef s) {
        if (!s) return;
        if (uint32_t ln = lir_view::stmt_line(s)) line_ = ln;
        switch (s.kind()) {
            case SCode::Let: {
                lir_view::SLetView v{s};
                // Evaluate the initializer BEFORE the binding exists, so a
                // shadowing `let x = f(x)` reads the outer x.
                TypeRef t = v.type(pool_);
                if (!t && v.value()) t = v.value().type(pool_);
                bir::LocalId l = new_local(std::string(v.name()), bir::LocalKind::User, t,
                                           v.is_mut() || v.name().starts_with("__") || v.compiler_glue(),
                                           v.var_slot(), line_);
                if (v.value()) lower_into({l, {}}, v.value());
                declare(l);
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
                    return place_or_temp(v.ptr()).with(bir::Proj(bir::Proj::K::Deref));
                });
                return;
            }
            case SCode::Drop: {
                lir_view::SDropView v{s};
                bir::LocalId l = find_local(v.var_name(), v.var_slot());
                if (l == bir::kNoLocal) return;
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
                // The step below reassigns the counter; in Rust each iteration
                // binds a fresh `i`, so the step is not a user assignment.
                bir::LocalId var = new_local(std::string(v.var()), bir::LocalKind::User, it,
                                             true, v.var_slot(), line_);
                declare(var);
                bir::Rvalue init(bir::Rvalue::K::Use);
                init.ops.push_back(lo);
                assign({var, {}}, std::move(init));
                bir::BlockId head = new_block(), body = new_block(), step = new_block(),
                             exit = new_block();
                goto_(head);
                cur_ = head;
                bir::LocalId c = temp({});
                bir::Rvalue cmp(bir::Rvalue::K::Op);
                cmp.what = "<";
                cmp.ops.push_back(read_place({var, {}}, it));
                cmp.ops.push_back(as_read(hi));
                assign({c, {}}, std::move(cmp));
                branch(read_place({c, {}}, {}), {body, exit});
                loops_.push_back({std::string(v.label()), step, exit, scopes_.size()});
                cur_ = body; lower_scoped_block(v.body()); goto_(step);
                loops_.pop_back();
                cur_ = step;
                bir::Rvalue inc(bir::Rvalue::K::Op);
                inc.what = "+1";
                inc.ops.push_back(read_place({var, {}}, it));
                assign({var, {}}, std::move(inc));
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
                    body_.locals[var].synth_pointee = et;
                    assign({var, {}}, ref_of(std::move(elem), mut_ref, false));
                } else {
                    bir::Rvalue get(bir::Rvalue::K::Use);
                    get.ops.push_back(read_place(std::move(elem), et));
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
            assign({l, {}}, ref_of(std::move(src), ref_mode == 2, false));
        else {
            bir::Rvalue rv(bir::Rvalue::K::Use);
            rv.ops.push_back(read_place(std::move(src), bind_type));
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
                    else bind_to(fp, ft, f.field_name(), f.bind_slot(), false, default_ref);
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
            case PCode::Slice:
                if (pattern_binds(pat)) unsupported("pat:Slice with bindings");
                return;
            default:
                return;   // Variant, Int, Bool, Range: no bindings
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
        pattern_test(sp, stype, {});
        std::vector<bir::BlockId> arm_blocks;
        for (size_t i = 0; i < arms.size(); ++i) arm_blocks.push_back(new_block());
        // "No arm matched": a match is exhaustive, so this edge is never
        // taken. It must not reach `join`, where the result is assumed set.
        bir::BlockId nomatch = new_block();
        body_.blocks[nomatch].term = bir::Term(bir::Term::K::Unreachable);
        body_.blocks[nomatch].terminated = true;
        arm_blocks.push_back(nomatch);
        branch(read_place(sp, {}), arm_blocks);
        for (size_t i = 0; i < arms.size(); ++i) {
            cur_ = arm_blocks[i];
            push_scope();
            bind_pattern(sp, stype, arms[i].pat(), 0);
            if (auto g = arms[i].guard()) {
                bir::Operand c = operand(g);
                bir::BlockId yes = new_block();
                branch(std::move(c), {yes, i + 1 < arms.size() ? arm_blocks[i + 1] : nomatch});
                cur_ = yes;
            }
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
    const TypePoolImpl*   pool_;
    bool                  mut_deref_ctx_ = false;
    bir::Body             body_;
    bir::BlockId          cur_ = 0;
    uint32_t              line_ = 0;
};

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

    BirFacts(const bir::Body& b, const lir::LProgram& prog, const TypeSets& ts)
        : b_(b), prog_(prog), ts_(ts), pool_(prog.type_pool.impl()) {}

    // Fills `db` (whose program is logos_bc.dl) and keeps the loan table for
    // explanations.
    void emit(dl::Database& db, dl::Symbols& syms);

    const std::vector<Loan>& loans() const { return loans_; }
    std::string point_name(uint32_t p) const { return "p" + std::to_string(p); }
    uint32_t point_line(uint32_t p) const { return p < point_lines_.size() ? point_lines_[p] : 0; }
    std::string place_str(const bir::Place& p) const {
        std::string s = b_.locals[p.local].name;
        for (auto& pj : p.proj) {
            if (pj.k == bir::Proj::K::Deref) s = "(*" + s + ")";
            else if (pj.k == bir::Proj::K::Field) s += "." + pj.field;
            else s += "[_]";
        }
        return s;
    }
    std::string path_str(uint32_t id) const { return paths_[id].first; }

private:
    // ── origins ──
    struct OList { std::vector<uint32_t> os; std::vector<bool> inv; bool exact = true; };
    uint32_t fresh_origin() { return n_origins_++; }
    void walk_type(TypeRef t, bool invariant, OList& out, int depth = 0) {
        if (!t || depth > 16) return;
        switch (t.kind()) {
            case LogosType::Kind::Ref: case LogosType::Kind::MutRef: {
                out.os.push_back(fresh_origin()); out.inv.push_back(invariant);
                walk_type(t.pointee(), invariant || t.kind() == LogosType::Kind::MutRef, out, depth + 1);
                return;
            }
            case LogosType::Kind::Slice:
                if (!t.owning_slice()) { out.os.push_back(fresh_origin()); out.inv.push_back(invariant); }
                walk_type(t.elem(), invariant, out, depth + 1);
                return;
            case LogosType::Kind::TraitObject:
                if (!t.owning_trait_object()) { out.os.push_back(fresh_origin()); out.inv.push_back(invariant); }
                return;
            case LogosType::Kind::DstRef:
                if (!t.owning_dst()) { out.os.push_back(fresh_origin()); out.inv.push_back(invariant); }
                return;
            case LogosType::Kind::Array:
                walk_type(t.elem(), invariant, out, depth + 1);
                return;
            case LogosType::Kind::Tuple:
                for (auto& e : t.tuple_elems()) walk_type(e, invariant, out, depth + 1);
                return;
            case LogosType::Kind::Struct: case LogosType::Kind::Enum: {
                size_t before = out.os.size();
                for (size_t i = 0; i < t.lifetime_args().size(); ++i) {
                    out.os.push_back(fresh_origin()); out.inv.push_back(invariant);
                }
                for (auto& a : t.type_args()) walk_type(a, invariant, out, depth + 1);
                if (out.os.size() == before && bir_type_carries_loans(t, prog_, ts_)) {
                    out.os.push_back(fresh_origin()); out.inv.push_back(invariant);
                }
                return;
            }
            case LogosType::Kind::Closure:
                // Captures are not visible in the type: one origin stands for
                // whatever the closure may hold.
                out.os.push_back(fresh_origin()); out.inv.push_back(invariant);
                return;
            default:
                return;
        }
    }
    const OList& local_origins(bir::LocalId l) {
        auto it = local_os_.find(l);
        if (it != local_os_.end()) return it->second;
        OList o;
        if (b_.locals[l].synth_ref) {
            o.os.push_back(fresh_origin()); o.inv.push_back(false);
            walk_type(b_.locals[l].synth_pointee, false, o);
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
                } else if (k == LogosType::Kind::Slice && !t.owning_slice()) {
                    cur.os.erase(cur.os.begin()); cur.inv.erase(cur.inv.begin());
                    t = TypeRef{};   // the unsized [T]: its element list follows
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
                    cur.exact = false;   // struct / enum payload: whole list
                }
            }
        }
        return cur;
    }
    // Loans in `src` flow into `dst` at point p. Pairwise when both shapes
    // are known and agree; every-to-every otherwise.
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
                if (!actual.owning_slice()) out.push_back("");
                labels(decl.elem(), actual.elem(), out, depth + 1);
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
                for (size_t i = 0; i < alts.size(); ++i) {
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
        if (!t.callee_fn || !dst.exact) return false;
        std::vector<std::string> lret;
        labels(t.callee_fn.decl_ret_type(pool_), t.callee_fn.ret_type(pool_), lret);
        if (lret.size() != dst.os.size()) return false;
        auto ps = t.callee_fn.params();
        struct Src { uint32_t origin; std::string label; bool outer_self; };
        std::vector<Src> srcs;
        std::vector<size_t> elided_positions;
        bool self_ref = false;
        for (size_t i = 0; i < t.args.size(); ++i) {
            const auto& a = t.args[i];
            if (a.k == bir::Operand::K::Const || !a.place.valid()) continue;
            OList ao = place_origins(a.place);
            std::vector<std::string> pl;
            if (i < ps.size()) labels(ps[i].decl_type(pool_), ps[i].type(pool_), pl);
            if (!ao.exact || pl.size() != ao.os.size()) pl.assign(ao.os.size(), "?");
            bool is_self = i == 0 && !ps.empty() && ps[0].name() == "self";
            TypeRef d0 = is_self ? ps[0].decl_type(pool_) : TypeRef{};
            if (is_self && d0 && (d0.kind() == LogosType::Kind::Ref || d0.kind() == LogosType::Kind::MutRef ||
                                  (d0.kind() == LogosType::Kind::DstRef && !d0.owning_dst())))
                self_ref = true;
            for (size_t k = 0; k < ao.os.size(); ++k) {
                srcs.push_back({ao.os[k], pl[k], is_self && k == 0});
                if (pl[k].empty() || (pl[k][0] == '\'' && pl[k] != "'static")) elided_positions.push_back(srcs.size() - 1);
            }
        }
        for (size_t j = 0; j < dst.os.size(); ++j) {
            const std::string& L = lret[j];
            if (L == "'static") continue;
            for (size_t si = 0; si < srcs.size(); ++si) {
                const Src& sr = srcs[si];
                bool from = false;
                if (L == "?" || sr.label == "?" || L == "~") from = true;
                else if (L.empty()) {
                    // Elided result lifetime: from `&self`'s own lifetime,
                    // else from the one input lifetime, else from all.
                    if (self_ref) from = sr.outer_self;
                    else from = std::find(elided_positions.begin(), elided_positions.end(), si) !=
                                elided_positions.end();
                } else {
                    from = sr.label == L;
                }
                if (from) subset(sr.origin, dst.os[j], p);
            }
        }
        return true;
    }

    // ── paths (moves / initialisation) ──
    // A move path is a local plus field projections, up to the first deref
    // or index. Returns ~0u when the place has no path (a temp-free const).
    uint32_t path_of(const bir::Place& p) {
        std::string key = "v" + std::to_string(p.local);
        uint32_t id = intern_path(key, ~0u, p.local);
        for (auto& pj : p.proj) {
            if (pj.k != bir::Proj::K::Field) break;
            std::string child = key + "." + pj.field;
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
        if (op.k == bir::Operand::K::Move && !op.place.has_deref()) {
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
        if (!dest.has_deref()) {
            row("path_assigned_at_base", {pa(path_of(dest)), pt(p)});
            if (!dest.proj.empty() && !b_.locals[dest.local].is_mut &&
                b_.locals[dest.local].kind != bir::LocalKind::Temp)
                row("immutable_write", {var(dest.local), pt(p)});
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
        return a.k == b.k && (a.k != bir::Proj::K::Field || a.field == b.field);
    }
    // Two places may denote overlapping memory.
    static bool overlap(const bir::Place& a, const bir::Place& b) {
        if (a.local != b.local) return false;
        size_t n = std::min(a.proj.size(), b.proj.size());
        for (size_t k = 0; k < n; ++k) {
            const auto& x = a.proj[k];
            const auto& y = b.proj[k];
            if (x.k == bir::Proj::K::Field && y.k == bir::Proj::K::Field && x.field != y.field)
                return false;
        }
        return true;
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
                        hit = true;
                        break;
                    case Access::StorageDead:
                        hit = extends_without_deref(L.place, a.place);
                        break;
                }
                if (hit) row("loan_invalidated_at", {l(i), pt(a.point)});
            }
        }
    }

    // ── row helpers ──
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
        if (L.kind == bir::LocalKind::Param)
            row("path_assigned_at_base", {pa(path_of({li, {}})), pt(entry)});
        else
            row("path_moved_at_base", {pa(path_of({li, {}})), pt(entry)});
        if (L.is_mut) row("var_mut", {var(li)});
        if (needs_drop(L.type, prog_, ts_))
            for (uint32_t oo : os.os) row("drop_of_var_derefs_origin", {var(li), o(oo)});
    }

    // Loans first (the kill and invalidation passes need all of them).
    std::vector<std::vector<int32_t>> loan_at(b_.blocks.size());
    for (size_t bi = 0; bi < b_.blocks.size(); ++bi) {
        loan_at[bi].assign(b_.blocks[bi].stmts.size(), -1);
        for (size_t si = 0; si < b_.blocks[bi].stmts.size(); ++si) {
            const auto& s = b_.blocks[bi].stmts[si];
            if (s.k != bir::Stmt::K::Assign || s.rv.k != bir::Rvalue::K::Ref) continue;
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
                    if (s.rv.k == bir::Rvalue::K::Ref) {
                        const Loan& L = loans_[loan_at[bi][si]];
                        row("loan_issued_at", {o(L.origin), l(uint32_t(loan_at[bi][si])), pt(p)});
                        use_place(s.rv.place, p);
                        row("path_accessed_at_base", {pa(path_of(s.rv.place)), pt(p)});
                        // Creating a shared borrow reads the place; a `&mut`
                        // writes it (a two-phase one reads until activation).
                        access(p, s.rv.place,
                               s.rv.mut_ && !s.rv.two_phase ? Access::WriteDeep : Access::Read,
                               loan_at[bi][si]);
                        if (s.rv.mut_ && !s.rv.place.has_deref() &&
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
                                else if (pj.k == bir::Proj::K::Field) pt = TypeRef{};   // unknown: no further reborrow edges
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
                        for (auto& op : s.rv.ops) {
                            read_operand(op, p);
                            if (op.k != bir::Operand::K::Const && op.place.valid()) {
                                OList src = place_origins(op.place);
                                if (s.rv.k == bir::Rvalue::K::Use) relate(src, dst, p);
                                else { src.exact = false; relate(src, dst, p); }
                            }
                        }
                        if (s.rv.k == bir::Rvalue::K::RawAddr) use_place(s.rv.place, p);
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
                    // A dead local is uninitialised again (a loop body's
                    // `let` is a first assignment on every iteration).
                    row("path_moved_at_base", {pa(path_of({s.local, {}})), pt(p)});
                    break;
                case bir::Stmt::K::Drop:
                    access(p, s.dest, Access::WriteDeep);
                    if (s.dest.proj.empty()) row("var_dropped_at", {var(s.dest.local), pt(p)});
                    else use_place(s.dest, p);
                    if (!s.dest.has_deref())
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
                    // Out-params: argument i stored behind `&mut` argument j.
                    if (t.flow && t.flow->available)
                        for (size_t j = 0; j < t.flow->to_outparam.size() && j < t.args.size(); ++j) {
                            uint64_t mask = t.flow->to_outparam[j];
                            if (!mask || !t.args[j].place.valid()) continue;
                            OList into = place_origins(t.args[j].place.with(bir::Proj(bir::Proj::K::Deref)));
                            into.exact = false;
                            for (size_t ai = 0; ai < t.args.size() && ai < 64; ++ai) {
                                if (!((mask >> ai) & 1u) || !t.args[ai].place.valid()) continue;
                                OList src = place_origins(t.args[ai].place); src.exact = false;
                                relate(src, into, p);
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
        out += std::format("  {} {}{}: {}\n", k, l.is_mut ? "mut " : "", l.name,
                           l.type ? std::string(type_str(l.type)) : std::string("?"));
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
                out += std::format("    {} = call {}({}) ->{}    @{}\n", place(t.dest), t.callee, as, tg, t.line);
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

static BirVerdict bir_check(lir_view::FunctionView fn, const lir::LProgram& prog,
                            const TypeSets& ts, const FnIndex& idx,
                            const FlowSummaryMap* flows) {
    BirVerdict v;
    BirBuilder builder(prog, ts, idx, flows);
    bir::Body body = builder.build(fn);
    const char* dump = std::getenv("LOGOS_BIR_DUMP");
    bool want_dump = dump && body.name.find(dump) != std::string::npos;
    if (want_dump) std::fputs(bir_dump(body).c_str(), stderr);
    v.unsupported = body.unsupported;
    if (!body.unsupported.empty()) return v;

    dl::Symbols* syms = nullptr;
    const dl::Program& rules = bc_rules(syms);
    dl::Database db(rules, *syms, /*provenance=*/want_dump);
    BirFacts facts(body, prog, ts);
    facts.emit(db, *syms);

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
    auto sym = [&](dl::Value x) { return std::string(syms->name(x)); };
    auto num = [](const std::string& s) { return uint32_t(std::stoul(s.substr(1))); };
    {
        const dl::Relation& R = rel("errors");
        for (size_t i = 0; i < R.size(); ++i) {
            auto row = R.row(i);
            uint32_t L = num(sym(row[0])), P = num(sym(row[1]));
            const auto& loan = facts.loans()[L];
            v.errors.push_back(std::format(
                "{} borrow of '{}' (line {}) is used after an access at line {} invalidated it",
                loan.mut_ ? "mutable" : "shared", facts.place_str(loan.place), loan.line,
                facts.point_line(P)));
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
            v.errors.push_back(std::format("'{}' is not declared mutable (line {})",
                                           body.locals[num(sym(row[0]))].name,
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
    return v;
}

// ── Shadow mode (LOGOS_DL_SHADOW=bc) ─────────────────────────────────────────
//
// Both checkers run on every post-mono function; the old checker's verdict is
// the one the compile acts on. Per process, disagreements are logged with both
// sides' messages, and a census line records how many functions were
// compared, skipped (and why), agreed and disagreed.

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
