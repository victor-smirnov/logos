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
//   (a) is_extern() / is_metaprog_stub() — no body exists at all.
//       PROBE: any `extern fn`; body_available() returns false at the seed.
//   (c) a non-empty body_external_ref() — the body is a Writ ExternalRef that
//       was never materialized.
//       PROBE: a lazy-module callee; body_available() returns false.
//   (d) INDIRECT calls whose callee cannot be resolved to a summary — dyn
//       dispatch, fn-pointers that are not closures, and operator-desugared
//       calls whose resolved_symbol is empty and whose by_base fallback
//       disagrees (resolve_method_flow returns nullptr). Same conservative
//       route result_borrows_self takes.
//       PROBE: two same-named methods with DIFFERENT summaries and a receiver
//       type that narrows to neither ⇒ agree() returns nullptr.
//       ⚠ CLOSURES ARE NO LONGER IN THIS CLASS on the result path: see H4 in
//       taint_of and note_closure_caps in borrow_check.cpp.
//
//   (e) AN ARCHIVED CALLEE. A function whose declaration arrives from a
//       `-l` module archive reaches the consumer with NO BODY, so
//       body_available()'s final `f.body()` test answers false and the
//       summary is UNAVAILABLE. The door-F shape is therefore NOT caught
//       across an archive boundary: the leak compiles rc=0.
//       PROBE, both halves one command each:
//         logosc --emit-module tests/logos/bc_cross_archive/bcxa.module \
//                -o /tmp/libbcxa.a
//         LOGOS_DUMP_FLOWS=stash2 logosc <mutate-after twin> -l /tmp/libbcxa.a
//       Pinned as far as it honestly can be: fail/bc_d1r5_h6_local_body_twin
//       (the same program with the body in the consumer's file — REFUSES) and
//       pass/bc_d1r5_h6_cross_archive_admits (the archive links and the
//       read-before-mutate program runs). The leak itself is deliberately
//       NOT pinned — an `.expected` asserting that unsound code compiles
//       records the defect as intended.
//
// ⚠ D1 round 5 / H6 — THE CORRECTION, AND IT CUTS BOTH WAYS. Item (b) used to
// read "(b) from_binary_module() — a precompiled .writ0 in a `-L` archive", and
// round 5 deleted it as "false — not a hole", citing a measurement in which the
// cross-archive leak refused. THE PREDICATE CLAIM IS TRUE and stands:
// body_available() never tests from_binary_module, and measured on an archived
// `stash2` the flag reads 0 anyway. THE CONCLUSION WAS WRONG. Re-measured here
// against both the original probe archive and a freshly rebuilt one, the
// summary is UNAVAILABLE and the leak compiles rc=0. The archived function's
// probe reads `extern=0 stub=0 extref=0 body=0 from_binary_module=0` — it is
// not (a), not (c) and not (b); it is the plain null body, entered above as
// (e). Deleting a residual entry because its NAMED PREDICATE was the wrong one
// silently promoted an open hole to a closed one, which is a residual list
// failing in the direction a residual list is dangerous in.
//
// Closing (e) is not a gate tweak: an external body must be walked through the
// FOREIGN arena AND its foreign type pool (mono does this via
// writ::resolve_external_ref + a src_arena_ swap), while this walk reads
// `prog_.type_pool` throughout. Cross-arena summarisation is its own arc.
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

    // ── D1 round 5 / H2: ITERATE TO CONVERGENCE, DO NOT CAP ────────────────
    //
    // THE DEFECT. `kMaxRounds = 8` was documented as "belt-and-braces", but a
    // round-robin fixpoint visits functions in DECLARATION order, so a chain
    // `s0 -> s1 -> ... -> s7` propagates exactly one link per round when the
    // callers are declared BEFORE the callees and the whole chain in ONE round
    // when they are declared after. Measured on the ladder 4/7/8/9/12:
    // caller-first refused at depth 4 and 7 and ADMITTED the leak at 8, 9, 12;
    // callee-first refused at every depth. The cap turned a semantically inert
    // property — the order two functions happen to appear in the file — into
    // the verdict, and in the PERMISSIVE direction.
    //
    // ⚠ AND IT WAS INVISIBLE. `rounds_used()` is read only inside the
    // LOGOS_DUMP_FLOWS block; nothing in the compiler ever saw the cap being
    // hit. It is also not a proxy for a wrong answer: depth 7 caller-first
    // already reported rounds=9 (cap exhausted) with a SOUND answer. So there
    // is no assertion over rounds_used() that could have caught this — the fix
    // has to be the iteration itself.
    //
    // TERMINATION IS A THEOREM, NOT A CAP. The state is, per available
    // function, `to_result` (nparams bits) plus `to_outparam[j]` (nparams bits
    // for each of the nparams sinks): a finite bitmask lattice of exactly
    // `Σ_f nparams_f · (1 + nparams_f)` bits over the whole program. Every
    // write is `|=` (recompute() returns `grew` only when a bit that was 0
    // became 1) and no path ever clears a summary bit, so the state is
    // monotonically increasing in a finite lattice ⇒ the ascending chain is
    // finite ⇒ the round that reports no change is reached.
    //
    // THE BOUND IS DERIVED, NOT CHOSEN. A round that returns `changed` set at
    // least one of those bits, so at most `total_bits` rounds can report a
    // change and the loop exits on round `total_bits + 1` at the latest.
    // `total_bits + 1` is therefore UNREACHABLE BY CONSTRUCTION, and reaching
    // it means monotonicity itself is broken (a summary bit was cleared, or a
    // transfer function is not deterministic) — a compiler bug, not an input.
    // Hence a hard internal error there, never a permissive answer: an
    // exhausted budget that silently returns the current state is exactly the
    // defect being removed.
    void run() {
        seed();
        uint64_t total_bits = 0;
        for (auto& [nm, s] : out_) {
            (void)nm;
            if (!s.available) continue;
            total_bits += static_cast<uint64_t>(s.nparams) * (1ull + s.nparams);
        }
        for (rounds_used_ = 1;; ++rounds_used_) {
            bool changed = false;
            for_each_fn([&](lir_view::FunctionView f) {
                changed |= recompute(f);
            });
            if (!changed) return;
            if (rounds_used_ > total_bits)
                flow_ice("inter-procedural fixpoint exceeded its derived bound",
                         rounds_used_, total_bits);
        }
    }
    uint32_t rounds_used() const { return rounds_used_; }
    uint32_t max_body_passes() const { return max_body_passes_; }

private:
    [[noreturn]] static void flow_ice(const char* what, uint64_t got,
                                      uint64_t bound) {
        fprintf(stderr,
                "internal compiler error: borrow-flow summary: %s "
                "(iteration %llu > bound %llu). The summary lattice is finite "
                "and the transfer functions are monotone, so this is "
                "unreachable by construction; refusing to answer.\n",
                what, (unsigned long long)got, (unsigned long long)bound);
        std::abort();
    }

    const lir::LProgram& prog_;
    const TypeSets&      ts_;
    const FnIndex&       idx_;
    FlowSummaryMap&      out_;
    uint32_t             rounds_used_ = 0;
    uint32_t             max_body_passes_ = 0;

    // per-function working state
    std::unordered_map<std::string, uint64_t> taint_;
    // D1 round 12 / A1: the enclosing loops' (label, break_slot) pairs, so a
    // `break v` can name the slot it deposits into. The checker keeps the same
    // fact on its LoopFrame; this is the summarizer's half of the SAME rule,
    // added in the same step because the two walkers may not drift again
    // (round 8's invariant, re-charged by A0 in this very round).
    std::vector<std::pair<std::string, std::string>> loop_slots_;
    // H0: local -> the places it reborrows (see note_alias). Monotone within a
    // body; cleared per function alongside taint_.
    // D1 round 8: the SAME store and the SAME resolve as the checker's `refs_`
    // and the pre-pass's `prescan_refs_` — this instance's policy is MONOTONE
    // (see RefGraph). What used to be a local `std::unordered_map` + a local
    // `each_root` is now the shared one.
    RefGraph alias_;
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
            // ── D1 round 13 / P3: AN OUT-PARAM NEED NOT BE A `&mut` ────────
            //
            // THE DEFECT (one-variable mask pair, measured):
            //   fn stashv(h: H,      c: &C) { h.r.push(c.mk()); }
            //       → `stashv__f__H__ref_C: result<-0`            no out bit
            //   fn stashv(h: &mut H, c: &C) { h.r.push(c.mk()); }
            //       → `stashv__f__refmut_H__ref_C: result<-0 out0<-0x2`
            // Same body, only the parameter MODE differs. A by-value aggregate
            // that transitively holds a `&mut` is an out-param in the only
            // sense this mask means: a write through that field reaches the
            // CALLER's object, because the reference was copied, not the
            // referent. `bc_holds_mut_ref_type` is round 10's predicate for
            // exactly this question and lived 176 lines from this loop without
            // ever being consulted here.
            //
            // It cannot widen the mask on its own: `to_outparam[j]` is still
            // computed by the body walk, so a param that is never written
            // through keeps a zero row and every consumer skips it
            // (`if (!fs->is_outparam[j] || !fs->to_outparam[j]) continue`).
            for (uint32_t i = 0; i < s.nparams; ++i)
                if (is_mut_ref(params[i].type(pool)) ||
                    bc_holds_mut_ref_type(ts_, params[i].type(pool)))
                    s.is_outparam[i] = 1;
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
        // ── D1 round 11 / X2: AN AGGREGATE THAT HOLDS A `&mut` CAN CARRY ───
        //
        // THE DEFECT, measured at the SUMMARY and not at a verdict.
        // `fn peel3(h: Inner) -> &mut Vec<B> { return h.r; }` summarised
        // `result<-0` while its by-REF twin (`h: &mut Inner`) summarised
        // `result<-0x1` — the same body, the same projection, two answers.
        // `Inner { r: &mut Vec<B> }` is a plain struct: not a ref, not
        // `#[borrow_carrying]` (that attribute is about a `&` FIELD), not
        // loan-carrying — so a BY-VALUE aggregate param was never seeded in
        // `recompute`, and `taint_of`'s VarRef arm answered 0 for it as well.
        // Both gates read THIS predicate, which is why the fix is one line
        // here and not two at the sites: the question they ask is the same
        // one, "could a borrow travel in this value?".
        //
        // Round 10 / SP1 gave the STORE path the `bc_holds_mut_ref_type`
        // treatment for the same reason ("a deposit could be written through
        // this value"); the RESULT path is that question read the other way —
        // a borrow can be EXTRACTED from anything that transitively holds a
        // `&mut`. Saying yes only ever ADDS flows, exactly like the Ptr and
        // Closure arms above.
        //
        // MEASURED over the whole stdlib (lang/mem/lcm/std, ~21.9k summaries,
        // all four compile rc=0): 204 functions gain a flow line they did not
        // have, ZERO lose one, and the only masks that CHANGE rather than
        // appear are `Option<&mut i32>::unwrap_or`'s (result<-0x2 → 0x3 — the
        // receiver holds the `&mut`, which is true and was missing). Compile
        // time within noise (mem 85.2s on / 85.9s off).
        return is_ref_kind(t) || bc_is_borrow_carrying_type(ts_, t) ||
               bc_loan_carrying_type(ts_, t) ||
               bc_holds_mut_ref_type(ts_, t);
    }

    const FlowSummary* summary_of_call(std::string_view symbol) const {
        return resolve_call_flow(out_, symbol);
    }
    const FlowSummary* summary_of_method(lir_view::EMethodCallView v) const {
        return resolve_method_flow(out_, idx_, prog_.type_pool.impl(), v);
    }

    // ── D1 round 5 / H0: A LOCAL HOLDING A &mut REBORROW *IS* ITS REFERENT ──
    //
    // THE DEFECT. One inert line — a reborrow local between the `&mut`
    // parameter (or the `&mut self` field) and the store — erased the whole
    // out-param flow:
    //     fn stash(c: &C, v: &mut Vec<B>) { let r: &mut Vec<B> = v;
    //                                       r.push(c.mk()); }
    // summarised `result<-0` (measured) while the one-line-less twin
    // summarised `result<-0 out1<-0x1`. call_effects deposits the source bits
    // on `root_of(args[j])` — which is the ALIAS local `r` — while recompute()
    // reads back `taint_of_name(pnames_[j])`, the PARAM name, and nothing ever
    // propagates from one to the other.
    //
    // ONE CONCEPT, NOT A PATCH AT EACH END. `r` is not a second place that
    // happens to be correlated with `v`; through a `&mut` reborrow it IS `v`'s
    // place. So the two ends are made the SAME SLOT rather than the deposit
    // being mirrored: `each_root` is the single choke point both the read
    // (taint_of_name) and the write (add_taint) go through. The checker's
    // mirror of this same concept is `rehome_reborrow` (borrow_check.cpp), and
    // it is the same sentence about the same fact.
    //
    // WHY A VECTOR AND NOT A SINGLE NAME. The map must be MONOTONE: the body
    // is walked repeatedly to a fixpoint (H3), so a later `r = w;` may not
    // RETARGET what an earlier pass already deposited. Unioning the roots is
    // the conservative resolution — more places co-hold, never fewer.
    //
    // ⚠ THE ADMIT DIRECTION. A `&mut` that is a FRESH borrow of a
    // materialized statement-temporary (`__rtmp_N`, which dies at the end of
    // the statement) has no referent to re-home to; tying it would make every
    // rvalue-receiver method call retain. Those are excluded by name, using
    // the same `is_materialized_temp_name` predicate prov_of already uses.
    // Control: h0_alias_admit / h0b_field_admit must stay admitted.
    void note_alias(const std::string& name, lir_view::ExprRef val) {
        using namespace lir_view;
        using Code = lir_schema::expr::Code;
        // ⚠ NOT THE DISCARD BINDING. MEASURED ON THE FULL BUILD: `fn
        // emit_lexer(cg: &mut Codegen, w: &mut CodeWriter) { let _ = cg;
        // let _ = w; }` (tools/peg_gen_logos) recorded BOTH params as roots of
        // the single name `_`, and since a deposit goes to every root, `cg` and
        // `w` swapped bits — the summary grew `out0<-0x2 out1<-0x1` out of a
        // function with an EMPTY body, and emit_all was refused twice over
        // ("cannot borrow 'cg' as mutable: already mutably borrowed"). `_` is a
        // discard, never a place, so it can never be a reborrow of anything.
        // D1 round 8: the SHAPE enumeration is no longer written here. The old
        // three-kind gate (VarRef/AddrOf/AddrOfTemp) is exactly U1's root, and
        // it was written out three times in two files; `ref_source_places` is
        // the one walker. This side is keyed by NAME (taint_ is), so a dotted
        // source place is charged to its ROOT — `let r = h.r;` makes a deposit
        // on `r` a deposit on `h`, which is the conservative direction.
        // ── D1 round 9 / S1: THE FEEDING IS THE SAME, THE POLICY IS NOT ────
        //
        // THE DEFECT (measured with LOGOS_DUMP_FLOWS):
        //   fn stash(c: &C, v: &mut Vec<B>) { let mut h: Inner = Inner { r: v };
        //                                     h.r.push(c.mk()); }
        // printed NO summary line for `stash` at all — `out1` lost entirely —
        // while the local-alias twin (`let r: &mut Vec<B> = v;`) prints
        // `result<-0 out1<-0x1`. Round 8 unified the shape walker, the store
        // and the resolve, but note_alias was still reached from exactly two
        // call sites (walk_stmt's Let and Assign) behind a gate on the whole
        // VALUE's type, and a struct literal is not a `&mut`: the field store
        // never entered `alias_`, so `call_effects`' deposit on `h` never
        // reached the parameter `v`.
        //
        // THE FIX is the enumeration, not a third door: `each_ref_store` is
        // the one walk (shared with the checker's note_struct_lit_reborrows
        // and the pre-pass's prescan_reborrow), and what stays here is this
        // instance's POLICY — both ends charged to their ROOT, because this
        // map is keyed by NAME (`taint_` is), and MONOTONE `add`, because the
        // body is walked to a fixpoint. Charging `h.r -> v` as `h -> v` is the
        // conservative direction: a deposit reaches MORE places, never fewer.
        //
        // U2 — THE `&mut`-ONLY GATE STAYS, and it is now a MEASURED decision
        // rather than an un-widened hedge. It is applied per STORED VALUE, not
        // to the whole `let`'s value — that was the part that made a struct
        // literal invisible. Widening it to `Ref|MutRef` (the checker's rule
        // since R7a) was BUILT and measured against the narrow gate:
        //   • bc_d1 + ctr_family corpus: 733155 `[flow]` lines, byte-identical
        //     under both gates; ctest 118/118 under both;
        //   • stdlib `mem` package: 6706 wide-only edges, 14 function
        //     summaries gain a bit (e.g. plan_stamp_src out0 0x10 -> 0x14),
        //     the package still compiles, no verdict moves;
        //   • the direct witness (`struct Hs { s: &C }`, a SHARED param ref
        //     stored into a field and laundered into an out-param) is admitted
        //     by BOTH gates — the widening does not close it. That leak is a
        //     separate hole in `taint_of`'s read path, not in this gate.
        // The structural reason there is no consumer: this map's edges are
        // only consumed as deposit destinations (`add_taint` walks them), and
        // a DEPOSIT needs a write — an out-param, a field/index write — which
        // needs a `&mut` path. A shared reborrow can never be written through,
        // so the extra edges can only ever strengthen a summary that no
        // witness asked to be stronger. A widening with no consumer is a
        // hedge; it is not taken, and the measurement above is the pair.
        if (name.empty() || name == "_" || !val) return;
        const auto* pool = prog_.type_pool.impl();
        each_ref_store(name, val, [&](const std::string& place,
                                      lir_view::ExprRef v) {
            // U2 — D1 round 10 / SP0+SP1: the gate now also admits a stored
            // AGGREGATE that transitively holds a `&mut`. r9's own measurement
            // retired a DIFFERENT and narrower widening (Ref|MutRef, whose
            // extra edges are shared reborrows that can never be written
            // through, hence no consumer); this one has its consumer measured:
            //   h.i = inn;        // gate=REJECT, edge h -> inn never added
            //   let inn = h.i;    // gate=REJECT, edge inn -> h never added
            // In both, `inn.r -> v` WAS recorded and the deposit landed on the
            // OTHER root, so each_root never crossed and the summary was lost
            // WHOLE (LOGOS_DUMP_FLOWS printed no line at all for the callee).
            // An aggregate holding a `&mut` is a write path, which is exactly
            // what an alias edge is consumed for.
            if (!v) return;
            TypeRef vt_u2 = v.type(pool);
            if (!is_mut_ref(vt_u2) && !bc_holds_mut_ref_type(ts_, vt_u2)) return;   // U2
            // ── D1 round 11 / X3: THE EDGE IS BETWEEN PLACES, NOT ROOTS ──
            //
            // THE DEFECT (over-refusal). `fn wire3(dst: &mut Two, a: &mut
            // Vec<B>, z: &mut Vec<B>) { dst.x = a; dst.y = z; }` summarised
            // `out1<-0x4 out2<-0x2` beside the true `out0<-0x6`: rooting the
            // destination recorded `dst -> a` AND `dst -> z`, so a deposit
            // into ONE field spread to the other field's referent and the
            // summary manufactured a `a <-> z` edge that the body never
            // writes. Keyed by the stored PLACE (`dst.x -> a`, `dst.y -> z`)
            // the two stop meeting. The out-param channel still answers about
            // the WHOLE parameter — recompute folds every sub-place back to
            // the root at the boundary, which is where the coarsening belongs.
            //
            // The SOURCE side is a place for the same reason: rooting it turns
            // `inn -> h.i` into `inn -> h`, and `inn.r` then resolves to `h.r`
            // instead of `h.i.r`. Admissibility (`_`, `__rtmp_N`) is still
            // asked of the ROOT — it is a fact about the binding.
            std::string droot = ref_place_root(place);
            if (droot.empty() || droot == "_" || is_materialized_temp_name(droot)) return;
            const std::string& dst = place;
            std::vector<std::string> srcs;
            ref_source_places(v, pool, srcs);
            for (auto& p : srcs) {
                std::string rroot = ref_place_root(p);
                if (rroot.empty() || is_materialized_temp_name(rroot)) continue;
                if (p == dst) continue;
                alias_.add(dst, p);
            }
        });
    }

    // `name` and every place it reborrows, transitively — RefGraph::each_root,
    // the one resolve shared with the checker.
    template <class F>
    void each_root(const std::string& name, F&& f) const {
        // X3: place-keyed edges need the PREFIX-AWARE walk (RefGraph::
        // each_root_place). One choke point still, as H0 requires — the read
        // (taint_of_name) and the write (add_taint) both come through here.
        alias_.each_root_place(name, std::forward<F>(f));
    }

    // Total popcount of the intra-procedural state — the monotone measure the
    // body fixpoint's termination argument is written over.
    uint64_t taint_weight() const {
        uint64_t w = static_cast<uint64_t>(__builtin_popcountll(result_taint_));
        for (auto& [n, m] : taint_) { (void)n; w += __builtin_popcountll(m); }
        return w;
    }

    uint64_t taint_of_name(const std::string& n) const {
        uint64_t m = 0;
        each_root(n, [&](const std::string& r) {
            auto it = taint_.find(r);
            if (it != taint_.end()) m |= it->second;
        });
        return m;
    }
    void add_taint(const std::string& n, uint64_t bits) {
        if (n.empty() || !bits) return;
        each_root(n, [&](const std::string& r) {
            uint64_t& slot = taint_[r];
            if ((slot | bits) != slot) { slot |= bits; body_changed_ = true; }
        });
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

    // X3: the DOTTED destination place a write names ("h.i.r"), falling back
    // to the root whenever the shape walker cannot name one (an index or a
    // tuple step makes the rest unnameable, and `ref_source_places` says so by
    // handing back the bare root). One walker, as round 8 requires.
    std::string dest_place_of(lir_view::ExprRef e) const {
        std::string root = root_of(e);
        if (root.empty()) return root;
        std::vector<std::string> ps;
        ref_source_places(e, prog_.type_pool.impl(), ps);
        if (ps.size() == 1 && ref_place_root(ps[0]) == root) return ps[0];
        return root;
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
            // ── D1 round 5 / H4: A CLOSURE CALL'S RESULT ───────────────
            //
            // THE DEFECT. `let f = || -> B { return c.mk(); }; return f();`
            // summarised as carrying NOTHING (measured rc=0 against rc=1 for
            // the inlined `return c.mk();`), and so did the out-param sink
            // `vs.push(f())`. taint_of's switch had arms for Call and
            // MethodCall only, so a ClosureCall fell to `default: return 0` —
            // it never even reached call_result_taint, which means the
            // documented (d) fallback ("tie the result to the ref /
            // by-value-bc OPERANDS") was never executed here. And had it been,
            // it would still have been VACUOUS: a closure's provenance lives
            // in its CAPTURES, and captures are not operands of the call —
            // the only operands are the closure value and the explicit args,
            // and in the leak shape there are no explicit args at all.
            //
            // THE FIX. The result inherits from the CLOSURE VALUE operand.
            // taint_of already has a ClosureBox arm that unions the captures'
            // taint into the closure local, so `taint_of(callee)` is exactly
            // "what this closure can hand back" and no new state is needed;
            // can_carry() already answers YES for Kind::Closure.
            //
            // A GENUINE FN POINTER IS NOT A CLOSURE and keeps the current
            // behaviour: the discriminator is the CALLEE EXPRESSION'S TYPE
            // KIND (Closure), not the expression code — FnPtrCall is the
            // spelling a closure local also gets in some lowerings, so
            // switching on the code alone would decide the wrong question.
            // Control: h4_closure_scalar_admit / h4_closure_out_admit stay
            // admitted (an i64-returning closure carries nothing).
            case Code::ClosureCall:
            case Code::FnPtrCall: {
                ExprRef callee = e.kind() == Code::ClosureCall
                    ? EClosureCallView{e}.callee() : EFnPtrCallView{e}.callee();
                if (!callee) return 0;
                TypeRef ct = callee.type(pool);
                // ── D1 round 6 / G1: A GENUINE FN POINTER IS NOT SILENT ───
                // The `return 0` below was written for the CLOSURE rule, but
                // it also swallowed every real fn-pointer call — so a function
                // whose ONLY borrow-flow edge is an indirect call summarised as
                // carrying NOTHING. Measured: `fn via(g: fn(&C) -> B, c: &C,
                // vs: &mut Vec<B>) { vs.push(g(c)); }` produced an empty
                // summary, so `via(mkd, &c, &mut vs); c.bump();` leaked in the
                // CALLER even though the caller's own call is an ordinary Call.
                // Fixing only the call sites in borrow_check.cpp cannot reach
                // this one: the fact is missing from the summary itself.
                //
                // A fn pointer inside a body being summarised is opaque here
                // (typically a PARAMETER — the very shape that motivates this),
                // so it takes the documented (d) route: `call_result_taint`
                // with a null summary ties the result to every borrowing
                // operand. That is the same conservatism a summary-less direct
                // Call already gets, and it can only EXTEND a loan.
                if (ct && ct.kind() == LogosType::Kind::FnPtr) {
                    if (!can_carry(t)) return 0;
                    std::vector<ExprRef> args;
                    EFnPtrCallView{e}.each_arg([&](ExprRef a){ args.push_back(a); });
                    return call_result_taint(/*s=*/nullptr, args,
                                             /*has_recv=*/false, t);
                }
                if (!ct || ct.kind() != LogosType::Kind::Closure) return 0;
                if (!can_carry(t)) return 0;
                uint64_t m = taint_of(callee);
                auto add_arg = [&](ExprRef a) {
                    if (!a) return;
                    TypeRef at = a.type(pool);
                    if (is_plain_ref_kind(at) ||
                        bc_is_borrow_carrying_type(ts_, at) ||
                        bc_loan_carrying_type(ts_, at))
                        m |= taint_of(a);
                };
                if (e.kind() == Code::ClosureCall) EClosureCallView{e}.each_arg(add_arg);
                else                               EFnPtrCallView{e}.each_arg(add_arg);
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
                // X3: the DEPOSIT side is keyed the way the alias edge is,
                // or the migration is half-done — and a half-migration turns
                // the over-approximation into an UNDER-approximation
                // (measured: r9_s1 / r10_sp0 / r10_sp1 all go permissive).
                add_taint(dest_place_of(args[j]), src);
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

    // ── D1 round 5 / H5: EVERY PATTERN KIND THAT BINDS ────────────────────
    //
    // THE DEFECT. This was a two-branch if/else-if over VariantData and Wild
    // that SILENTLY RETURNED for the other eleven kinds of
    // lir_schema::pat::Code — a permissive default, invisible to a green
    // corpus by construction. Measured: a callee that destructures a STRUCT
    // (`match w { Wrap { b: inner } => { v.push(inner); } }`) summarised
    // `result<-0` and admitted the leak, while the ENUM twin refused; the
    // TUPLE pattern was the same hole. Both are now covered.
    //
    // ⚠ THE PROBE HAD TO BE RE-SHAPED. Passing the `Wrap` by value AT THE
    // CALL SITE came back rc=1 — but that is door 8b firing on the by-value
    // borrow-carrying ARGUMENT, a different rule, while the summary
    // underneath was already wrong. The isolating shape manufactures the
    // `Wrap` INSIDE the callee.
    //
    // ENUMERATED FROM THE SCHEMA (pat::Code::Count == 13), not guessed, and
    // written as an exhaustive switch WITHOUT a default: the four kinds that
    // legitimately bind nothing are explicit no-op arms, so a future 14th kind
    // is a compile-time hole rather than another silent permissive default.
    // Three carrier mechanisms: a BINDINGS string array (VariantData, Tuple),
    // a NAME (Wild, At, RefBind, PatFieldBinding's shorthand), and SUB-PATTERNS
    // recursed into with the same bits.
    // Control that a handled arm really was handled: h5b_at_pat_leak (bare-name
    // Wild binding) refused before this change and must still refuse.
    void bind_pat(lir_view::PatRef pr, uint64_t bits) {
        using namespace lir_view;
        using PC = lir_schema::pat::Code;
        if (!pr || !bits) return;
        auto bind_name = [&](std::string_view n) {
            if (!n.empty() && n != "_") add_taint(std::string(n), bits);
        };
        switch (pr.kind()) {
            // Bind nothing — the only four, stated rather than defaulted.
            case PC::Variant: case PC::Int: case PC::Bool: case PC::Range:
                return;
            case PC::Wild:
                bind_name(PatWildView{pr}.name());
                return;
            case PC::VariantData:
                PatVariantDataView{pr}.each_binding(bind_name);
                return;
            case PC::Tuple: {
                PatTupleView pv{pr};
                pv.each_binding(bind_name);
                pv.each_sub([&](PatRef sub){ bind_pat(sub, bits); });
                return;
            }
            case PC::Struct: {
                PatStructView pv{pr};
                pv.each_field([&](PatFieldBindingView fb) {
                    if (auto sub = fb.sub()) bind_pat(sub, bits);
                    else bind_name(fb.field_name());   // shorthand `Wrap { b }`
                });
                return;
            }
            case PC::Or:
                PatOrView{pr}.each_alt([&](PatRef alt){ bind_pat(alt, bits); });
                return;
            case PC::Slice: {
                PatSliceView pv{pr};
                pv.each_prefix([&](PatRef sub){ bind_pat(sub, bits); });
                pv.each_rest  ([&](PatRef sub){ bind_pat(sub, bits); });
                pv.each_suffix([&](PatRef sub){ bind_pat(sub, bits); });
                return;
            }
            case PC::At: {
                PatAtView pv{pr};
                bind_name(pv.name());
                if (auto sub = pv.sub()) bind_pat(sub, bits);
                return;
            }
            case PC::RefBind:
                bind_name(PatRefBindView{pr}.name());
                return;
            case PC::RefPat:
                if (auto in = PatRefPatView{pr}.inner()) bind_pat(in, bits);
                return;
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
                note_alias(std::string(v.name()), v.value());   // H0, before the deposit
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
                note_alias(std::string(v.name()), v.value());   // H0, before the deposit
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
                // S1, the other half of the split feeding: the checker has a
                // FIELD-WRITE door into the graph (note_reborrow_place) and
                // this side had none, so `h.r = v;` recorded no alias at all.
                // Charged to the receiver's ROOT, as everything on this side
                // is (see note_alias).
                // X3 NOT TAKEN HERE, MEASURED: dotting this destination
                // (`v.receiver() + "." + v.field()`) fired ZERO times — over
                // stdlib mem and lcm, and over X3's own two witnesses. The
                // FieldWrite STATEMENT kind does not occur in what reaches this
                // walker (r10 retired another door for the same reason); a
                // `h.i = inn` on a local arrives as DerefWrite. A widening on a
                // branch that never executes is a hedge, and worse, a green
                // branch that never ran. The door stays as it was.
                note_alias(std::string(v.receiver()), v.value());
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
                // ── D1 round 10 / SP2, and the END of "a door per position" ──
                // Seven place-write statement kinds fed `add_taint`; only two
                // (Assign, FieldWrite) also fed `alias_`. Measured on SP2's
                // witness: `h.r = w` (a reference FIELD store, h.r: &mut) does
                // NOT lower to FieldWrite — FieldWrite occurs ZERO times in
                // that program — it lowers to DerefWrite, six rows of it, and
                // this door had no note_alias, so the edge h -> w was never
                // created and the deposit stayed on the ORIGINAL referent
                // (`out1<-0x5`, no out2). The lowering was SETTLED BY
                // EXECUTION, not assumed: `struct IV { r: &mut i64 }; h.r = w;
                // *h.r = 9;` over a=1,b=2 exits 19 (b written), so it is a
                // reference REBIND, not `*h.r = *w` — the missing out2 is a
                // real defect and not an artifact of the spelling.
                // THE OTHER FIVE DOORS STAY add_taint-ONLY, and that is a
                // MEASURED decision, not an oversight. note_alias was written
                // into ChainFieldWrite / TupleWrite / DerefFieldWrite /
                // IndexWrite / FieldIndexWrite as well, and then removed: over
                // stdlib `mem` + `std` those five fired ZERO times (13855 +
                // 2481 rows, ALL DerefWrite), and every nested-place spelling
                // tried for a witness — `h.m.i = Inner{…}`, `t.0 = Inner{…}` —
                // ALSO lowers to DerefWrite. A door that never executes cannot
                // be measured green; a widening with no consumer is a hedge.
                // (Re-open this with a program that reaches one of them.)
                //
                // The alias instance is MONOTONE by policy, so after this the
                // summary carries BOTH h -> v and h -> w and sets out1 and
                // out2: over-approximate, and that is the direction this
                // analysis is allowed to err in. Generations / a flow-
                // sensitive view are the price of the remaining precision and
                // are not paid until a witness shows OVER-refusal.
                SDerefWriteView v{sr};
                std::string dw_dst = dest_place_of(v.ptr());        // X3
                note_alias(dw_dst, v.value());                     // r10 / 1c
                add_taint(dw_dst, taint_of(v.value()));
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
            // D1 round 12 / A1: a loop-as-expression deposits into its BREAK
            // SLOT, and `return loop { break v; }` therefore carries v's taint
            // to the result. Measured (LOGOS_DUMP_FLOWS): `pickl` summarised
            // `result<-0` while the unwrapped `pickd` beside it gave the true
            // `result<-0x1`. The slot's VarRef is read by the BlockExpr wrapper
            // sema builds around the loop, so once the Break arm below writes
            // the slot, `taint_of` picks it up with no further change.
            case Code::Loop: {
                SLoopView v{sr};
                loop_slots_.emplace_back(std::string(v.label()),
                                         std::string(v.break_slot()));
                if (auto b = v.body()) walk_block(b);
                loop_slots_.pop_back();
                break;
            }
            case Code::Break: {
                SBreakView v{sr};
                if (!loop_slots_.empty() && v.value()) {
                    std::string_view lbl = v.label();
                    const std::string* slot = &loop_slots_.back().second;
                    if (!lbl.empty())
                        for (auto it = loop_slots_.rbegin(); it != loop_slots_.rend(); ++it)
                            if (it->first == lbl) { slot = &it->second; break; }
                    if (!slot->empty()) add_taint(*slot, taint_of(v.value()));
                }
                break;
            }
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
        alias_.clear();
        loop_slots_.clear();          // A1: per-function, like taint_
        result_taint_ = 0;
        pnames_.clear();
        ptypes_.clear();
        for (uint32_t i = 0; i < s.nparams; ++i) {
            pnames_.emplace_back(params[i].name());
            ptypes_.push_back(params[i].type(pool));
            // ── D1 round 11 / X2: THE SEED IS THE STORE DOOR'S TWIN ────────
            //
            // THE DEFECT (measured at the SUMMARY, not at a verdict).
            // `fn peel3(h: Inner) -> &mut Vec<B> { return h.r; }` summarises
            // `result<-0` while its by-REF twin (`h: &mut Inner`) summarises
            // `result<-0x1` — the same body, the same projection, two answers.
            // The seed is why: `can_carry(Inner)` is false (a plain struct is
            // neither a ref nor `#[borrow_carrying]` nor loan-carrying), so a
            // BY-VALUE aggregate param was never seeded and `h.r`'s FieldRead
            // had no taint to project. Round 10 / SP1 gave the STORE path the
            // `bc_holds_mut_ref_type` treatment for exactly this reason ("a
            // deposit could be written through this value"); the RESULT path
            // is the same question read the other way — a borrow can be
            // EXTRACTED from any value that transitively holds a `&mut`.
            // Adding it only ever ADDS flows, never removes one.
            if (can_carry(ptypes_[i])) taint_[pnames_[i]] = 1ull << i;
        }
        // ── D1 round 5 / H3: THE BODY PASS IS A FIXPOINT TOO ───────────────
        //
        // THE DEFECT. `kBodyPasses = 3` truncated the INTRA-procedural walk,
        // and `taint_.clear()` above makes the truncation unrecoverable: the
        // next outer round starts from parameters again, so the outer fixpoint
        // converges on the truncated answer and reports "no change". Measured
        // with a reverse-order assignment chain in the callee
        // (`v.push(aN); aN = aN-1; …; a1 = c.mk();`): hops 1 and 2 refuse,
        // hops 3, 4 and 5 ADMIT, boundary exactly at kBodyPasses — i.e. the
        // ORDER OF STATEMENTS inside the callee decided soundness, in the
        // permissive direction, and the outer round counter still read 3.
        //
        // TERMINATION. Within one body the state is `taint_` (a bitmask per
        // NAME, over the ≤64 parameter bits) plus `result_taint_`, and every
        // write is `|=` guarded by "did a 0-bit become 1" — that guard is
        // exactly what sets `body_changed_`. So a pass that reports a change
        // strictly increases the total popcount `taint_weight()`, which is
        // bounded by 64 · (number of distinct names the walk ever touches), a
        // finite set fixed by the body. Strictly-increasing and bounded ⇒ the
        // pass that reports no change is reached.
        //
        // The two checks below are the theorem's two halves, both hard errors:
        // a "changed" pass that did NOT increase the weight falsifies the
        // monotone-growth invariant, and the derived pass bound
        // 64·|names| + 1 is unreachable while that invariant holds.
        uint64_t prev_w = taint_weight();
        for (uint32_t pass = 1;; ++pass) {
            body_changed_ = false;
            walk_block(f.body());
            if (pass > max_body_passes_) max_body_passes_ = pass;
            if (!body_changed_) break;
            uint64_t w = taint_weight();
            if (w <= prev_w)
                flow_ice("body pass reported a change without growing the "
                         "taint lattice", w, prev_w);
            prev_w = w;
            if (pass > 64ull * (taint_.size() + 1))
                flow_ice("intra-procedural body fixpoint exceeded its derived "
                         "bound", pass, 64ull * (taint_.size() + 1));
        }

        bool grew = false;
        if ((s.to_result | result_taint_) != s.to_result) {
            s.to_result |= result_taint_;
            grew = true;
        }
        for (uint32_t j = 0; j < s.nparams; ++j) {
            if (!s.is_outparam[j]) continue;
            // X3: FOLD SUB-PLACE MASKS TO THE ROOT AT THE BOUNDARY. Inside the
            // body the deposits are keyed by place (`dst.x`, `dst.y`); the
            // out-param channel answers about the WHOLE parameter, so every
            // place under it contributes. The precision the keying buys is not
            // lost by this: what a SIBLING place holds no longer reaches the
            // other's referent, which is the false `out1<-0x4` X3 names.
            uint64_t held = taint_of_name(pnames_[j]);
            {
                std::string pfx = pnames_[j] + ".";
                for (auto& [n, m] : taint_)
                    if (n.size() > pfx.size() && n.compare(0, pfx.size(), pfx) == 0)
                        held |= m;
            }
            held &= ~(1ull << j);
            if ((s.to_outparam[j] | held) != s.to_outparam[j]) {
                s.to_outparam[j] |= held;
                grew = true;
            }
        }
        return grew;
    }
};
