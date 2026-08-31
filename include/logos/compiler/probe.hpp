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
inline bool arm_inst() {
    return on("ltmintinst") || on("ltmintmeet") || on("ltmeetany") ||
           on("ltmintmeetrg");
}
inline bool arm_subst() {
    return on("ltsubstinst") || on("ltmeetco");
}

}  // namespace logos::probe
