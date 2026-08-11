// ── D1 round 3 / F3: per-function BORROW-FLOW SUMMARIES ────────────────────
//
// THE DEFECT. `fn stash2(c: &C, v: &mut Vec<B>) { v.push(c.mk()); }` followed
// by `stash2(&c, &mut vs); c.bump(); *vs.get(0).p` compiled. Nothing at the
// CALL SITE is by-value borrow-carrying — the B is MANUFACTURED inside the
// callee out of its own `&`-param and stored through its `&mut` out-param — so
// none of round 1/2's rules (door 8b's by-value arg, door F's &mut-arg mirror)
// can fire. Every one of those rules reads only the callee's SIGNATURE, and the
// signature does not say where a borrow goes.
//
// THE SUMMARY. Per function, for each parameter i, which SINKS the loans rooted
// at i can reach: the RESULT, and/or each `&mut` parameter j. That is a finite
// lattice (params × (1 + params)) and the transfer functions only ever ADD, so
// a round-robin fixpoint over the whole program terminates — no "in progress =>
// bottom" default is needed for ordinary recursion or mutual recursion, which
// is the point (see the ABUSE-DIRECTION note below).
//
// WHY A FIXPOINT AND NOT A ONE-PASS BOTTOM-UP WALK. Both terminate. They differ
// in what they do at a CYCLE. A bottom-up walk must answer "what does this
// callee do?" while that callee is still being computed, and the only two
// answers are `bottom` (no flow — PERMISSIVE, a silent hole) and `top` (flows
// everywhere — OVER-REFUSING). Checking the exemption in the abuse direction:
// a permissive default is invisible to a green corpus by construction, so
// choosing it for ordinary recursion would write an unmeasurable hole into the
// design. The fixpoint needs no default there at all, which confines the
// permissive answer to the four genuinely-unavailable cases below, where it can
// be written down and pointed at.
//
// THE DOCUMENTED RESIDUAL HOLE. A callee whose BODY is not in this LProgram
// gets `available=false`, and every consumer then falls back to its pre-F3
// behaviour (signature elision) rather than tying everything to everything:
//   (a) is_extern()          — no body exists at all;
//   (b) from_binary_module() — a precompiled .writ0 in a `-L` archive;
//   (c) from_lazy_module() / a non-empty body_external_ref() — the body is a
//       Writ ExternalRef that was never materialized;
//   (d) INDIRECT calls — dyn dispatch, closures, fn-pointers, and operator-
//       desugared calls whose resolved_symbol is empty and whose by_base
//       fallback disagrees. Same conservative route result_borrows_self takes.
// Constraint (a) of the brief — over-refusing the ecosystem is worse than a
// documented residual hole — is what picks the fallback over a `top` default.
//
// PRE-MONO. Summaries are computed and consumed in the POST-mono pass only:
// the pre-mono `generic_templates_only` pass runs exclusivity-only over TypeVar
// bodies where neither is_borrow_carrying_type nor the callee resolution is
// meaningful.

struct FlowSummary {
    bool                  available = false;
    // Bit i set ⇒ loans rooted at param i can reach the sink.
    uint64_t              to_result = 0;
    std::vector<uint64_t> to_outparam;   // indexed by the &mut param j
    std::vector<uint8_t>  is_outparam;
    uint32_t              nparams = 0;
};
using FlowSummaryMap = std::unordered_map<std::string, FlowSummary>;

// The most parameters a summary can discriminate (one bit each). A function
// with more falls back to available=false — the documented (d)-class route,
// not a wrong answer.
static constexpr uint32_t kFlowMaxParams = 64;

// Callee → summary resolution, shared by the summary pass and the checker.
//
// Post-mono a method call still carries the GENERIC template symbol
// (`…Vec__push__g__refmut_Vec$G1$T__T`), while the summaries are keyed by the
// SPECIALIZATION names that actually exist in the LProgram
// (`…Vec$G1$B__push__g__…`). Measured on f3_stash_leak: the direct
// resolved_symbol lookup misses every generic container method. So after the
// direct hit fails, narrow `by_base` by the RECEIVER'S CONCRETE TYPE — that is
// the same information mono used to pick the specialization — and only then
// fall back to "every same-named method agrees", the conservative route
// result_borrows_self already takes. Ambiguity ⇒ nullptr ⇒ the documented
// (d) hole, never a guess.
static const FlowSummary* resolve_call_flow(const FlowSummaryMap& flows,
                                            std::string_view symbol) {
    if (symbol.empty()) return nullptr;
    auto it = flows.find(std::string(symbol));
    if (it == flows.end() || !it->second.available) return nullptr;
    return &it->second;
}

static bool flow_eq(const FlowSummary& a, const FlowSummary& b) {
    return a.nparams == b.nparams && a.to_result == b.to_result &&
           a.to_outparam == b.to_outparam;
}

static const FlowSummary* resolve_method_flow(const FlowSummaryMap& flows,
                                              const FnIndex& idx,
                                              const TypePoolImpl* pool,
                                              lir_view::EMethodCallView v) {
    if (auto* s = resolve_call_flow(flows, v.resolved_symbol())) return s;
    auto it = idx.by_base.find(std::string(v.method()));
    if (it == idx.by_base.end() || it->second.empty()) return nullptr;
    TypeRef recv_t = v.receiver() ? v.receiver().type(pool) : TypeRef(nullptr);

    auto agree = [&](const std::vector<lir_view::FunctionView>& cands)
            -> const FlowSummary* {
        const FlowSummary* first = nullptr;
        for (lir_view::FunctionView f : cands) {
            auto* s = resolve_call_flow(flows, f.name());
            if (!s) return nullptr;
            if (!first) { first = s; continue; }
            if (!flow_eq(*s, *first)) return nullptr;
        }
        return first;
    };

    // D1 round 4 / N0 — THE PEEL, AND WHY IT IS A SECOND TIER RATHER THAN A
    // WIDENING. `fn stash3(c: &C, k: &mut Keep) { k.v.push(c.mk()); }` compiled.
    // The receiver is a FIELD PROJECTION, so `recv.type(pool)` is the
    // expression's VALUE type `Vec<B>`, while `ps[0].type(pool)` of the callee
    // is the self parameter's type `&mut Vec<B>`. The comparison below is
    // pointer identity over interned types, so it narrowed to ZERO candidates
    // for EVERY field-projection receiver of a generic method, and control fell
    // to `agree(it->second)` — where the program held both
    // `Vec$G1$B__push` (result<-0 out0<-0x2) and `Vec$G1$i32__push`
    // (UNAVAILABLE). agree() returns nullptr on the first unavailable sibling,
    // so this was not even a value disagreement: narrowing WOULD have found the
    // right summary, and the permissive (d) route was taken for want of a peel.
    //
    // The exact tier runs FIRST and still decides whenever it can. That is what
    // keeps a `&self` overload distinguishable from a `&mut self` overload of
    // the same name on the same base type, and `Vec<B>` from `&Vec<B>` — and it
    // is what the param-root spelling (`fn stash2(c: &C, v: &mut Vec<B>)`,
    // receiver `v` a VarRef of type `&mut Vec<B>`) already matches on today, so
    // the peel must not disturb it. Hence: peel BOTH sides, and only when the
    // exact tier came back empty.
    //
    // The peeled tier can only ever SHRINK the candidate set relative to the
    // `agree(it->second)` fallback it replaces, never grow it, so it cannot
    // admit a summary that the fallback would have rejected on disagreement
    // grounds alone — and a peeled tie between two genuinely different
    // specializations still goes through agree(): ambiguity ⇒ nullptr ⇒ the
    // documented (d) hole, never a guess.
    //
    // NOT a consumer concern: ps[0].type is read ONLY inside this loop (all
    // five consumers of the resolved summary — summary_of_method's two callers,
    // the two flow_of_method sites in prov, and apply_flow_outparams — read the
    // receiver-first OPERAND indexing convention, which the peel does not
    // touch).
    auto peel = [](TypeRef t) {
        while (t && (t.kind() == LogosType::Kind::Ref ||
                     t.kind() == LogosType::Kind::MutRef))
            t = t.pointee();
        return t;
    };
    if (recv_t) {
        std::vector<lir_view::FunctionView> narrowed;
        for (lir_view::FunctionView f : it->second) {
            auto ps = f.params();
            if (ps.empty()) continue;
            if (ps[0].type(pool) == recv_t) narrowed.push_back(f);
        }
        if (!narrowed.empty()) return agree(narrowed);
        if (TypeRef rb = peel(recv_t)) {
            for (lir_view::FunctionView f : it->second) {
                auto ps = f.params();
                if (ps.empty()) continue;
                if (peel(ps[0].type(pool)) == rb) narrowed.push_back(f);
            }
            if (!narrowed.empty()) return agree(narrowed);
        }
    }
    return agree(it->second);
}

class FlowSummarizer {
public:
    FlowSummarizer(const lir::LProgram& prog, const TypeSets& ts,
                   const FnIndex& idx, FlowSummaryMap& out)
        : prog_(prog), ts_(ts), idx_(idx), out_(out) {}

    // Round-robin to a fixpoint. Monotone + finite ⇒ terminates; the cap is a
    // belt-and-braces bound, and hitting it is reported by `rounds_used`.
    void run() {
        seed();
        for (rounds_used_ = 1; rounds_used_ <= kMaxRounds; ++rounds_used_) {
            bool changed = false;
            for_each_fn([&](lir_view::FunctionView f) {
                changed |= recompute(f);
            });
            if (!changed) return;
        }
    }
    uint32_t rounds_used() const { return rounds_used_; }

private:
    static constexpr uint32_t kMaxRounds = 8;
    static constexpr uint32_t kBodyPasses = 3;   // loops: walk until stable

    const lir::LProgram& prog_;
    const TypeSets&      ts_;
    const FnIndex&       idx_;
    FlowSummaryMap&      out_;
    uint32_t             rounds_used_ = 0;

    // per-function working state
    std::unordered_map<std::string, uint64_t> taint_;
    std::vector<std::string>                  pnames_;
    std::vector<TypeRef>                      ptypes_;
    uint64_t                                  result_taint_ = 0;
    bool                                      body_changed_ = false;

    template <class F> void for_each_fn(F&& f) {
        for (auto& fn : prog_.functions)       f(lir_view::FunctionView(fn));
        for (auto& fn : prog_.specializations) f(lir_view::FunctionView(fn));
        for (auto& sd : prog_.structs)
            sd.each_method([&](lir_view::FunctionView m) { f(m); });
    }

    // MEASURED, not assumed: `from_binary_module()` is NOT a hole by itself.
    // The driver skips those functions for CHECKING ("already borrow-checked
    // when THEIR layer was built"), but their bodies ARE materialized in this
    // LProgram — and they have to be read here, because a post-mono call site
    // still names the GENERIC template symbol, and every stdlib generic
    // (`Vec::push`, the whole container plane) is exactly a from_binary_module
    // template. Excluding them left f3_stash_leak's summary EMPTY (measured:
    // `stash2: result<-0` with no out-flow) and F3 open for every generic
    // container. The residual hole is therefore only "no body at all":
    // is_extern / metaprog stub / an unmaterialized ExternalRef body / a null
    // body — plus the (d) indirect-call class at the resolution site.
    static bool body_available(lir_view::FunctionView f) {
        if (!f) return false;
        if (f.is_extern() || f.is_metaprog_stub()) return false;       // (a)
        if (f.body_external_ref().aid.value != 0) return false;         // (c)
        return static_cast<bool>(f.body());
    }

    void seed() {
        for_each_fn([&](lir_view::FunctionView f) {
            if (!f) return;
            FlowSummary s;
            auto params = f.params();
            s.nparams = static_cast<uint32_t>(params.size());
            if (!body_available(f) || s.nparams > kFlowMaxParams) {
                out_[std::string(f.name())] = s;   // available = false
                return;
            }
            s.available = true;
            s.to_outparam.assign(s.nparams, 0);
            s.is_outparam.assign(s.nparams, 0);
            const auto* pool = prog_.type_pool.impl();
            for (uint32_t i = 0; i < s.nparams; ++i)
                if (is_mut_ref(params[i].type(pool))) s.is_outparam[i] = 1;
            out_[std::string(f.name())] = std::move(s);
        });
    }

    // Can a value of this type carry a borrow?
    //
    // A TypeVar / AssocType / ImplTrait answers YES. Post-mono a call site
    // still names the GENERIC template symbol (`Vec__push__g__…`, not
    // `Vec$G1$B__push__g__…` — measured on f3_stash_leak), so the summary that
    // gets consulted is the one computed over the TypeVar body. If `T` were
    // read as "cannot carry", `Vec::push` would summarise as storing NOTHING
    // and F3 would stay open for every generic container — the by_base
    // fallback cannot rescue it either, because `Vec<i64>::push` and
    // `Vec<B>::push` then DISAGREE and disagreement means "unavailable".
    // Saying YES here is a statement about the CALLEE's shape only; whether a
    // borrow is actually present is decided at the call site from the concrete
    // ARGUMENT's type, which is what keeps f3_scalar_control admitted
    // (`v.push(c.v)` — the argument is an i64, so nothing flows).
    bool can_carry(TypeRef t) const {
        if (!t) return false;
        auto k = t.kind();
        if (k == LogosType::Kind::TypeVar || k == LogosType::Kind::AssocType ||
            k == LogosType::Kind::ImplTrait)
            return true;
        // A RAW POINTER carries whatever was stored through it. `box_new` is
        // the load-bearing case: `let p = alloc(…) as *mut T; p[0] = val;
        // return Box { ptr: p };` — the flow val → result goes through a raw
        // store, and without Ptr here `Box::new` summarised as retaining
        // NOTHING and F4's closure/`Box<&i64>` leaks re-opened (measured: both
        // flipped back to rc=0). Saying yes here only ever ADDS flows, i.e.
        // makes the summary more conservative, never more permissive.
        if (k == LogosType::Kind::Ptr) return true;
        // A CLOSURE value carries its captures, and an ERASED trait object
        // carries whatever the concrete value held — both are the F4 shapes,
        // and `is_ref_kind` says no to the OWNING forms (`Box<dyn Fn>`'s
        // payload). Measured: without Closure here, `Box$G1$|| -> i64__new`
        // summarised `result<-0` and f4_closure_leak stayed rc=0.
        if (k == LogosType::Kind::Closure || k == LogosType::Kind::TraitObject ||
            k == LogosType::Kind::UnsizedDyn || k == LogosType::Kind::DstRef)
            return true;
        return is_ref_kind(t) || bc_is_borrow_carrying_type(ts_, t) ||
               bc_loan_carrying_type(ts_, t);
    }

    const FlowSummary* summary_of_call(std::string_view symbol) const {
        return resolve_call_flow(out_, symbol);
    }
    const FlowSummary* summary_of_method(lir_view::EMethodCallView v) const {
        return resolve_method_flow(out_, idx_, prog_.type_pool.impl(), v);
    }

    uint64_t taint_of_name(const std::string& n) const {
        auto it = taint_.find(n);
        return it == taint_.end() ? 0u : it->second;
    }
    void add_taint(const std::string& n, uint64_t bits) {
        if (n.empty() || !bits) return;
        uint64_t& slot = taint_[n];
        if ((slot | bits) != slot) { slot |= bits; body_changed_ = true; }
    }

    // Root binding of a place expression, stripping &/&mut/field/index/deref.
    std::string root_of(lir_view::ExprRef e) const {
        using namespace lir_view;
        using Code = lir_schema::expr::Code;
        const auto* pool = prog_.type_pool.impl();
        while (e) {
            switch (e.kind()) {
                case Code::VarRef:     return std::string(EVarRefView{e}.name());
                case Code::AddrOf:     return std::string(EAddrOfView{e}.var_name());
                case Code::AddrOfTemp: e = EAddrOfTempView{e}.inner();      continue;
                case Code::FieldRead:  e = EFieldReadView{e}.receiver();    continue;
                case Code::TupleIndex: e = ETupleIndexView{e}.receiver();   continue;
                case Code::IndexRead:  e = EIndexReadView{e}.receiver();    continue;
                case Code::SliceIndex: e = ESliceIndexView{e}.slice();      continue;
                case Code::Deref:      e = EDerefView{e}.operand();         continue;
                case Code::Cast:       e = ECastView{e}.operand();          continue;
                default: return {};
            }
        }
        (void)pool;
        return {};
    }

    // Which params' loans this expression's VALUE may carry.
    uint64_t taint_of(lir_view::ExprRef e) {
        using namespace lir_view;
        using Code = lir_schema::expr::Code;
        if (!e) return 0;
        const auto* pool = prog_.type_pool.impl();
        TypeRef t = e.type(pool);
        switch (e.kind()) {
            case Code::VarRef:
                return can_carry(t) ? taint_of_name(std::string(EVarRefView{e}.name())) : 0;
            case Code::AddrOf:
                return taint_of_name(std::string(EAddrOfView{e}.var_name()));
            case Code::AddrOfTemp: {
                BorrowPlace bp = extract_borrow_place(EAddrOfTempView{e}.inner(), pool);
                if (!bp.root.empty()) return taint_of_name(bp.root);
                return taint_of(EAddrOfTempView{e}.inner());
            }
            case Code::FieldRead:
                return can_carry(t) ? taint_of(EFieldReadView{e}.receiver()) : 0;
            case Code::TupleIndex:
                return can_carry(t) ? taint_of(ETupleIndexView{e}.receiver()) : 0;
            case Code::IndexRead:
                return can_carry(t) ? taint_of(EIndexReadView{e}.receiver()) : 0;
            case Code::SliceIndex:
                return can_carry(t) ? taint_of(ESliceIndexView{e}.slice()) : 0;
            case Code::Deref:
                return can_carry(t) ? taint_of(EDerefView{e}.operand()) : 0;
            case Code::Cast:
                return can_carry(t) ? taint_of(ECastView{e}.operand()) : 0;
            case Code::Try:
                return can_carry(t) ? taint_of(ETryView{e}.inner()) : 0;
            case Code::StructLit: {
                uint64_t m = 0;
                EStructLitView{e}.each_field_value([&](ExprRef fv){ m |= taint_of(fv); });
                return m;
            }
            case Code::TupleLit: {
                uint64_t m = 0;
                ETupleLitView{e}.each_elem([&](ExprRef el){ m |= taint_of(el); });
                return m;
            }
            case Code::ArrLit: {
                uint64_t m = 0;
                EArrLitView{e}.each_elem([&](ExprRef el){ m |= taint_of(el); });
                return m;
            }
            case Code::EnumLitData: {
                uint64_t m = 0;
                EEnumLitDataView{e}.each_payload([&](ExprRef pl){ m |= taint_of(pl); });
                return m;
            }
            case Code::SliceLit:
                return taint_of(ESliceLitView{e}.base());
            case Code::IfExpr: {
                EIfExprView v{e};
                return taint_of(v.then_val()) | taint_of(v.else_val());
            }
            case Code::BlockExpr: {
                EBlockExprView v{e};
                if (auto b = v.block()) walk_block(b);
                return taint_of(v.result());
            }
            case Code::MatchExpr: {
                EMatchExprView v{e};
                uint64_t scrut = taint_of(v.scrut());
                uint64_t m = 0;
                v.each_arm([&](EMatchArmRef arm) {
                    bind_pat(arm.pat(), scrut);
                    m |= taint_of(arm.value());
                });
                return m;
            }
            case Code::ClosureBox: {
                uint64_t m = 0;
                EClosureBoxView{e}.each_capture_name([&](std::string_view cap){
                    m |= taint_of_name(std::string(cap));
                });
                return m;
            }
            case Code::Call: {
                ECallView v{e};
                std::vector<ExprRef> args;
                v.each_arg([&](ExprRef a){ args.push_back(a); });
                return call_result_taint(summary_of_call(v.callee()), args,
                                         /*has_recv=*/false, t);
            }
            case Code::MethodCall: {
                EMethodCallView v{e};
                std::vector<ExprRef> args;
                args.push_back(v.receiver());
                v.each_arg([&](ExprRef a){ args.push_back(a); });
                return call_result_taint(summary_of_method(v), args,
                                         /*has_recv=*/true, t);
            }
            default:
                return 0;
        }
    }

    // `args` is receiver-first for a method call, so index i lines up with
    // param i in both spellings.
    uint64_t call_result_taint(const FlowSummary* s,
                               const std::vector<lir_view::ExprRef>& args,
                               bool has_recv, TypeRef result_type) {
        const auto* pool = prog_.type_pool.impl();
        if (s) {
            uint64_t m = 0;
            for (size_t i = 0; i < args.size() && i < s->nparams; ++i)
                if (s->to_result & (1ull << i)) m |= taint_of(args[i]);
            return m;
        }
        // Unavailable body — the (a)–(d) fallback: the pre-F3 signature
        // elision, i.e. exactly what collect_ref_sources already does. A
        // borrow-returning call ties its result to its ref / by-value-bc
        // operands; a call whose result carries nothing ties nothing.
        (void)has_recv;
        if (!can_carry(result_type)) return 0;
        uint64_t m = 0;
        for (auto a : args) {
            if (!a) continue;
            TypeRef at = a.type(pool);
            if (is_plain_ref_kind(at) || bc_is_borrow_carrying_type(ts_, at) ||
                bc_loan_carrying_type(ts_, at))
                m |= taint_of(a);
        }
        return m;
    }

    // A call's OUT-PARAM effect: what it stores through its `&mut` arguments.
    void call_effects(lir_view::ExprRef e) {
        using namespace lir_view;
        using Code = lir_schema::expr::Code;
        if (!e) return;
        const FlowSummary* s = nullptr;
        std::vector<ExprRef> args;
        if (e.kind() == Code::Call) {
            ECallView v{e};
            s = summary_of_call(v.callee());
            v.each_arg([&](ExprRef a){ args.push_back(a); });
        } else if (e.kind() == Code::MethodCall) {
            EMethodCallView v{e};
            s = summary_of_method(v);
            args.push_back(v.receiver());
            v.each_arg([&](ExprRef a){ args.push_back(a); });
        }
        if (s) {
            for (size_t j = 0; j < args.size() && j < s->nparams; ++j) {
                if (!s->is_outparam[j] || !s->to_outparam[j]) continue;
                uint64_t src = 0;
                for (size_t i = 0; i < args.size() && i < s->nparams; ++i)
                    if (s->to_outparam[j] & (1ull << i)) src |= taint_of(args[i]);
                add_taint(root_of(args[j]), src);
            }
        }
        // Nested calls in operand position still have their own effects.
        each_sub_expr(e, [&](ExprRef sub){ call_effects(sub); });
    }

    template <class F>
    void each_sub_expr(lir_view::ExprRef e, F&& f) {
        using namespace lir_view;
        using Code = lir_schema::expr::Code;
        if (!e) return;
        switch (e.kind()) {
            case Code::Call:       ECallView{e}.each_arg(f); break;
            case Code::MethodCall: {
                EMethodCallView v{e};
                f(v.receiver());
                v.each_arg(f);
                break;
            }
            case Code::ClosureCall: {
                EClosureCallView v{e}; f(v.callee()); v.each_arg(f); break;
            }
            case Code::FnPtrCall: {
                EFnPtrCallView v{e}; f(v.callee()); v.each_arg(f); break;
            }
            case Code::FormatCall: {
                EFormatCallView v{e}; f(v.fmt()); v.each_arg(f); break;
            }
            case Code::StructLit:   EStructLitView{e}.each_field_value(f); break;
            case Code::TupleLit:    ETupleLitView{e}.each_elem(f); break;
            case Code::ArrLit:      EArrLitView{e}.each_elem(f); break;
            case Code::EnumLitData: EEnumLitDataView{e}.each_payload(f); break;
            case Code::FieldRead:   f(EFieldReadView{e}.receiver()); break;
            case Code::TupleIndex:  f(ETupleIndexView{e}.receiver()); break;
            case Code::Deref:       f(EDerefView{e}.operand()); break;
            case Code::Cast:        f(ECastView{e}.operand()); break;
            case Code::Try:         f(ETryView{e}.inner()); break;
            case Code::AddrOfTemp:  f(EAddrOfTempView{e}.inner()); break;
            case Code::Unary:       f(EUnaryView{e}.operand()); break;
            case Code::BinOp: {
                EBinOpView v{e}; f(v.lhs()); f(v.rhs()); break;
            }
            case Code::IndexRead: {
                EIndexReadView v{e}; f(v.receiver()); f(v.index()); break;
            }
            case Code::SliceIndex: {
                ESliceIndexView v{e}; f(v.slice()); f(v.index()); break;
            }
            case Code::IfExpr: {
                EIfExprView v{e}; f(v.cond()); f(v.then_val()); f(v.else_val()); break;
            }
            case Code::MatchExpr: {
                EMatchExprView v{e};
                f(v.scrut());
                v.each_arm([&](EMatchArmRef arm){
                    if (auto g = arm.guard()) f(g);
                    f(arm.value());
                });
                break;
            }
            case Code::BlockExpr:   f(EBlockExprView{e}.result()); break;
            default: break;
        }
    }

    void bind_pat(lir_view::PatRef pr, uint64_t bits) {
        using namespace lir_view;
        if (!pr || !bits) return;
        if (pr.kind() == lir_schema::pat::Code::VariantData) {
            PatVariantDataView pv{pr};
            pv.each_binding([&](std::string_view b){ add_taint(std::string(b), bits); });
        } else if (pr.kind() == lir_schema::pat::Code::Wild) {
            std::string n(PatWildView{pr}.name());
            if (!n.empty() && n != "_") add_taint(n, bits);
        }
    }

    void walk_block(lir_view::BlockRef br) {
        if (!br) return;
        br.each_stmt([&](lir_view::StmtRef sr){ walk_stmt(sr); });
    }

    void walk_stmt(lir_view::StmtRef sr) {
        using namespace lir_view;
        using Code = lir_schema::stmt::Code;
        if (!sr) return;
        switch (sr.kind()) {
            case Code::Let: {
                SLetView v{sr};
                add_taint(std::string(v.name()), taint_of(v.value()));
                call_effects(v.value());
                break;
            }
            case Code::LetElse: {
                SLetElseView v{sr};
                uint64_t m = taint_of(v.scrut());
                bind_pat(v.pat(), m);
                call_effects(v.scrut());
                if (auto b = v.else_block()) walk_block(b);
                break;
            }
            case Code::Assign: {
                SAssignView v{sr};
                add_taint(std::string(v.name()), taint_of(v.value()));
                call_effects(v.value());
                break;
            }
            case Code::Return: {
                SReturnView v{sr};
                uint64_t m = taint_of(v.value());
                if ((result_taint_ | m) != result_taint_) {
                    result_taint_ |= m;
                    body_changed_ = true;
                }
                call_effects(v.value());
                break;
            }
            case Code::ExprStmt:
                call_effects(SExprStmtView{sr}.expr());
                break;
            case Code::FieldWrite: {
                SFieldWriteView v{sr};
                add_taint(std::string(v.receiver()), taint_of(v.value()));
                call_effects(v.value());
                break;
            }
            case Code::ChainFieldWrite: {
                SChainFieldWriteView v{sr};
                add_taint(std::string(v.receiver()), taint_of(v.value()));
                call_effects(v.value());
                break;
            }
            case Code::TupleWrite: {
                STupleWriteView v{sr};
                add_taint(std::string(v.receiver()), taint_of(v.value()));
                call_effects(v.value());
                break;
            }
            case Code::DerefFieldWrite: {
                SDerefFieldWriteView v{sr};
                add_taint(std::string(v.receiver()), taint_of(v.value()));
                call_effects(v.value());
                break;
            }
            case Code::IndexWrite: {
                SIndexWriteView v{sr};
                add_taint(std::string(v.arr()), taint_of(v.value()));
                call_effects(v.value());
                break;
            }
            case Code::FieldIndexWrite: {
                SFieldIndexWriteView v{sr};
                add_taint(std::string(v.receiver()), taint_of(v.value()));
                call_effects(v.value());
                break;
            }
            case Code::DerefWrite: {
                SDerefWriteView v{sr};
                add_taint(root_of(v.ptr()), taint_of(v.value()));
                call_effects(v.value());
                break;
            }
            case Code::If: {
                SIfView v{sr};
                call_effects(v.cond());
                if (auto b = v.then_block()) walk_block(b);
                if (auto b = v.else_block()) walk_block(b);
                break;
            }
            case Code::While: {
                SWhileView v{sr};
                call_effects(v.cond());
                if (auto b = v.body()) walk_block(b);
                break;
            }
            case Code::Loop:
                if (auto b = SLoopView{sr}.body()) walk_block(b);
                break;
            case Code::For:
                if (auto b = SForView{sr}.body()) walk_block(b);
                break;
            case Code::ForEach: {
                SForEachView v{sr};
                add_taint(std::string(v.var()), taint_of(v.iter()));
                call_effects(v.iter());
                if (auto b = v.body()) walk_block(b);
                break;
            }
            case Code::Match: {
                SMatchView v{sr};
                uint64_t scrut = taint_of(v.scrut());
                v.each_arm([&](EMatchArmRef arm){
                    bind_pat(arm.pat(), scrut);
                    if (auto b = arm.body()) walk_block(b);
                });
                break;
            }
            case Code::Block:
                if (auto b = SBlockView{sr}.body()) walk_block(b);
                break;
            default:
                break;
        }
    }

    // Recompute one function's summary; returns true if it grew.
    bool recompute(lir_view::FunctionView f) {
        if (!f) return false;
        auto it = out_.find(std::string(f.name()));
        if (it == out_.end() || !it->second.available) return false;
        FlowSummary& s = it->second;

        const auto* pool = prog_.type_pool.impl();
        auto params = f.params();
        taint_.clear();
        result_taint_ = 0;
        pnames_.clear();
        ptypes_.clear();
        for (uint32_t i = 0; i < s.nparams; ++i) {
            pnames_.emplace_back(params[i].name());
            ptypes_.push_back(params[i].type(pool));
            if (can_carry(ptypes_[i])) taint_[pnames_[i]] = 1ull << i;
        }
        // Loops / back edges: the transfer functions only add, so re-walking
        // until nothing changes is the intra-procedural fixpoint.
        for (uint32_t pass = 0; pass < kBodyPasses; ++pass) {
            body_changed_ = false;
            walk_block(f.body());
            if (!body_changed_) break;
        }

        bool grew = false;
        if ((s.to_result | result_taint_) != s.to_result) {
            s.to_result |= result_taint_;
            grew = true;
        }
        for (uint32_t j = 0; j < s.nparams; ++j) {
            if (!s.is_outparam[j]) continue;
            uint64_t held = taint_of_name(pnames_[j]) & ~(1ull << j);
            if ((s.to_outparam[j] | held) != s.to_outparam[j]) {
                s.to_outparam[j] |= held;
                grew = true;
            }
        }
        return grew;
    }
};
