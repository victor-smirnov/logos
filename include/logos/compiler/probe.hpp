#pragma once
// probe.hpp — CEILING PROBES: measure what a hypothesis COULD close, before
// paying to make it correct.
//
// ── WHY ─────────────────────────────────────────────────────────────────────
// Measured 2026-08-27 on this tree: a one-TU rebuild is 150 s, the WHOLE
// acceptance population (447 `bc_admits.ledger` rows, one ctest test each) is
// 32 s, and L2 + L4 are ~20 minutes. So for two thirds of a minute you can ask
// the only question that matters at the start of a round — DOES THIS MECHANISM
// MOVE ANY ROWS — and we were instead paying twenty minutes and a fixture
// authorship per hypothesis to find out.
//
// A ceiling probe is DELIBERATELY WRONG. It ignores exemptions, over-refuses,
// and would break the stdlib if it were ever on by default. That is legitimate
// because it is never landed: the only thing read off it is the ledger delta,
// which is an UPPER BOUND on what the mechanism could ever close. Ceiling 0
// kills a hypothesis in three minutes instead of ninety; ceiling 40 says where
// to spend a careful round.
//
// ⚠ IT IS ENV-GATED, AND THAT IS LOAD-BEARING TWICE. (1) The build is
// unaffected, so a crude probe cannot break the stdlib compile and cost you the
// measurement you came for. (2) N independent hypotheses fit in ONE build, each
// under its own name, so N probes cost one 150 s build plus N x 32 s instead of
// N x 182 s.
//
// ── THE TRAP THIS EXISTS TO CLOSE ───────────────────────────────────────────
// A probe that never EXECUTES reports ceiling 0, which is indistinguishable
// from a refuted hypothesis and reads as an answer. That is the same defect as
// a green test over a branch that never ran. So `on()` does not merely answer:
// it RECORDS that the site was reached, and scripts/ceiling-probe.sh REFUSES a
// run whose fire count is zero, reporting NEVER FIRED rather than a ceiling.
// A zero is only an answer once the site is proven live.
//
// ── USE ─────────────────────────────────────────────────────────────────────
//     if (logos::probe::on("pathclear")) path_parts.clear();   // suppress it
//     if (logos::probe::on("pathkeep"))  { /* the aggressive alternative */ }
//
//     $ scripts/ceiling-probe.sh pathclear
//
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace logos::probe {

// One armed probe per process, named by LOGOS_PROBE. Counting is per name so a
// mis-typed name reads as NEVER FIRED instead of silently arming nothing.
struct Counter {
    const char* name = nullptr;
    unsigned long hits = 0;
    ~Counter() {
        if (!name || !hits) return;
        const char* log = std::getenv("LOGOS_PROBE_FIRE");
        if (!log) return;
        // Append, never truncate: every ledger row is its own logosc process
        // and the run's total fire count is the sum over all of them.
        if (std::FILE* f = std::fopen(log, "a")) {
            std::fprintf(f, "%s\t%lu\n", name, hits);
            std::fclose(f);
        }
    }
};

// SITE CENSUS. `on()` answers a hypothesis; `census()` answers WHERE. Gated on
// LOGOS_CENSUS, one `bucket<TAB>count` line per bucket appended to
// $LOGOS_CENSUS at exit — every logosc process appends, the run's total is the
// sum. Rides in the same build as the gate it checks (PROBES.md, rule 17).
struct CensusTable {
    std::unordered_map<std::string, unsigned long> c;
    ~CensusTable() {
        const char* log = std::getenv("LOGOS_CENSUS");
        if (!log || c.empty()) return;
        if (std::FILE* f = std::fopen(log, "a")) {
            for (auto& [k, v] : c) std::fprintf(f, "%s\t%lu\n", k.c_str(), v);
            std::fclose(f);
        }
    }
};
inline CensusTable& census_table() { static CensusTable t; return t; }
inline bool census_armed() {
    static const bool armed = std::getenv("LOGOS_CENSUS") != nullptr;
    return armed;
}
inline void census(const char* bucket, unsigned long n = 1) {
    if (!census_armed()) return;
    census_table().c[bucket] += n;
}
inline void census(const std::string& bucket, unsigned long n = 1) {
    if (!census_armed()) return;
    census_table().c[bucket] += n;
}

inline bool on(const char* name) {
    static const char* armed = std::getenv("LOGOS_PROBE");
    if (!armed || std::strcmp(armed, name) != 0) return false;
    static Counter c{name, 0};
    ++c.hits;
    return true;
}

// ── THE ELISION ENGINE'S ARMS, NAMED ONCE ───────────────────────────────────
// One process arms ONE name (`on()` compares against a single LOGOS_PROBE), so
// a compound arm must answer at every gate its component arms use. See
// src/compiler/PROBES.md 2026-08-31i/j/k.
//   arm_inst   — the mint + the substitution + the four repairs (ltmintinst),
//                and every arm built on top of it.
//   arm_subst  — the substitution half alone, and every arm built on it.
// ⚠ LANDED 2026-08-31n — `arm_inst()` IS NO LONGER A PROBE. The mint, the
// substitution and the four repairs are the compiler's behaviour; the name is
// kept because it is what every one of its ~20 sites reads as, and because the
// CONTROL REVERT of this round is `git revert` of the landing commit, not the
// removal of an env var. The arms it used to answer for — ltmintinst,
// ltmintmeet, ltregmeet, ltcallmeet, ltregall — are retired with it. The arms
// that still WIDEN something (ltmeetany, ltmintmeetrg, ltmintmeetamb,
// ltcallmeetany, ltregallany) stay probes and are asked separately below.
inline bool arm_inst() { return true; }
inline bool arm_subst() {
    return on("ltsubstinst") || on("ltmeetco");
}

// ── THE REGION SLOT (2026-08-31m) ───────────────────────────────────────────
// `&'a [T]` / `&'a str` / `&'a dyn` / `&'a Dst` canonicalise to Kind::Slice /
// TraitObject / TaggedPtr / DstRef and the region is DROPPED at resolve_type —
// 1 358 164 arrivals (PROBES.md 2026-08-31i). The AST already CARRIES the
// lifetime (logos.peg `slice_type` / `dyn_type` capture it into a LIFETIME
// slot); only the type it resolves to has nowhere to put it. `arm_regslot()`
// gates the WRITE side: every canonicalisation that today throws the region
// away instead records it. The READ side (variance_in_type, the pool's
// byte-strict equality) needs no gate — it reads a field that is empty unless
// this arm wrote it, so unarmed behaviour is unchanged by construction.
//   ltregslot  — the slot ALONE (rule 13: price the increment separately).
//   ltregmeet  — the slot + the engine (ltmintinst) + the meet under the
//                COVARIANCE GUARD ALONE: with the region recorded the variance
//                fixpoint no longer needs the ambient stand-in of 2026-08-31l.
// ⚠ LANDED 2026-08-31n. `&'a [T]` / `&'a str` / `&'a dyn` / `&'a Dst` RECORD
// their region. ltregslot is retired.
inline bool arm_regslot() { return true; }

// ── THE CALL'S OWN BINDER (2026-08-31n) ─────────────────────────────────────
// The meet exists only at the STRUCT LITERAL: `build_call_lt_subst_` censuses
// its multi-candidate binders and then keeps first-occurrence-wins, because
// `binder_variance_` is asked with an EMPTY key — nothing in this tree computes
// a variance for a fn's own binder. MEASURED at that site, one hand program:
//     fn pick<'a>(x:&'a i64, y:&'a i64) -> &'a i64
//     fn use2<'p,'q>(p:&'p i64, q:&'q i64) -> i64 { let r = pick(p,q); ... }
// `meet.call.multi.novariance 2`, and the program — ordinary legal Rust — is
// REFUSED by every arm built on the engine. `arm_callmeet()` gates the meet at
// the CALL under a variance computed for the callee's binder over its parameter
// types AND its return type; `ltcallmeetany` is the same site with the guard
// removed (rule 9's second name for the inner predicate, the abuse direction).
// ⚠ LANDED 2026-08-31n. `ltcallmeetany` — the SAME site with the variance
// guard removed — stays a probe: it is the only thing that separates the guard
// from its absence, and NO population in the harness can (measured: ceiling 23
// and cost 3 for both, identical row for row; only the hand program
// tests/logos/fail/bc_ltcallmeet_invariant_binder_no_meet refuses under one
// and compiles under the other).
inline bool arm_callmeet() { return true; }
inline bool callmeet_unguarded() { return on("ltcallmeetany"); }

// ── A MINTED REGION IS AN INFERENCE VARIABLE (2026-08-31n) ──────────────────
// The mint gave every elided slot a name, and the comparators' ONLY permissive
// spelling is the empty one — so `swap_ref<T>(&mut t0, &mut t1)` binds T from
// argument 1 with `'%1` in it and refuses argument 2's `'%2` at the invariant
// position, printing "expected &mut &mut i64, got &mut &mut i64" because
// neither name was written by anyone. `lt_is_minted` already carries this fact
// for borrow_check ("a minted name reads as elided here", borrow_check.cpp);
// the comparators never asked. Two names for the inner predicate (rule 9):
//   ltregall     — both sides minted, AND ONLY WHERE `permissive_empty` IS
//                  ALREADY TRUE: a CALL-SITE coercion, where the caller's
//                  region inference is the thing that fills unresolved
//                  lifetimes in. That is exactly where the unarmed comparator
//                  saw "" on both sides and answered true.
//   ltregallany  — the SAME predicate with the scope removed (and either side
//                  minted enough), the ABUSE DIRECTION: it then also erases
//                  the comparison inside a fn BODY — an assignment through a
//                  `&mut`, a return type — where two elided slots are two
//                  different regions and the mint's whole point is to say so.
//                  MEASURED: four ledger rows are the price of the scope.
// ⚠ LANDED 2026-08-31n, SCOPED. `ltregallany` — the same predicate with the
// `permissive_empty` scope removed — stays a probe, and the scope is worth 4
// ledger rows plus one CORRECT refusal (bc_genrecv's `pick<T>(&self, other:
// &i64) -> &i64`, which rustc rejects by elision rule 3).
inline bool arm_mintiv() { return true; }
inline bool mintiv_any() { return on("ltregallany"); }

// slit* (PROBES.md 2026-09-02v) — a bare struct literal's lifetime args are
// read off its VALUES: the fat-pointer kinds in the two walks (slitkinds), the
// generic path (slitgenlt), both (slitwhole).
inline bool slit_kinds() { return on("slitkinds") || on("slitwhole") || on("slitall"); }
inline bool slit_gen()   { return on("slitgenlt") || on("slitwhole") || on("slitall"); }
inline bool slit_mint()  { return on("slitmint")  || on("slitall"); }

}  // namespace logos::probe
