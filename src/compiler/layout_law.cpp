// Logos project — https://github.com/victor-smirnov/logos
//
// layout_law.cpp — the cross-engine layout ledger.
//
// The law itself is header-only and pure. This TU holds the one piece of state
// it needs: the record of what the engines that run BEFORE mlir-gen answered,
// so `verify_layout_engines()` — the only place `llvm::DataLayout` is reachable
// — can check them against the layout the object file is actually emitted with.
//
// Recording is off unless `LOGOS_VERIFY_LAYOUT` is set, so a normal compile
// pays nothing; the gate sets it, and asserts a FLOOR on the number of entries
// checked, because a ledger that silently stayed empty is exactly the
// "reported nothing wrong without having looked" failure.

#include "layout_law.hpp"

#include <cstdlib>
#include <cstring>
#include <cstdio>

namespace logos::compiler::layout {

std::vector<LedgerEntry>& ledger() noexcept {
    static std::vector<LedgerEntry> v;
    return v;
}

bool recording_enabled() noexcept {
    static const bool on = [] {
        const char* e = std::getenv("LOGOS_VERIFY_LAYOUT");
        return e && *e && *e != '0';
    }();
    return on;
}

const char* canary_engine() noexcept {
    static const char* const e = [] () -> const char* {
        const char* v = std::getenv("LOGOS_LAYOUT_CANARY");
        return (v && *v) ? v : nullptr;
    }();
    return e;
}

void record(const char* engine, std::string key, Answer answer) noexcept {
    if (!recording_enabled()) return;
    if (key.empty()) return;
    // ⚠ THE CANARY LIES ON THE WAY IN, not at the comparison. Everything the
    // real answer rides — the dedup on (engine, key), the key→`llvm::DataLayout`
    // lookup, the per-engine count, the size/align compare, `note()`,
    // `bad.size()`, the census line — the corrupted answer rides too, because it
    // IS a ledger entry and there is no second path. A canary caught somewhere
    // else than the real defect would prove nothing about the real defect.
    //
    // EVERY answer from the named engine is moved, not one: at record time
    // nothing here knows which keys mlir-gen will end up registering, and a
    // canary that lands on an unmatched key would be silently dropped as
    // `n_unmatched` and read as "instrument dead" — a false red. Corrupting the
    // whole engine is deterministic: if ANY of its answers is checked at all,
    // the canary is caught.
    if (const char* ce = canary_engine())
        if (std::strcmp(engine, ce) == 0) answer.layout.size += 1;
    ledger().push_back(
        LedgerEntry{ engine, std::move(key), answer.layout, answer.shape,
                     gen_round() });
}

std::vector<Decline>& declines() noexcept {
    static std::vector<Decline> v;
    return v;
}

// A decline is recorded under the SAME door as an answer (`recording_enabled`),
// because it is the same kind of fact about the same run: the verifier reads
// both or neither, and a decline recorded into a ledger nobody reads would be
// exactly the silence it exists to break. The engine asked at codegen does not
// rely on this door — it dies at the decline site regardless.
void record_declined(const char* engine, std::string key, std::string why) noexcept {
    if (!recording_enabled()) return;
    if (const char* t = std::getenv("LOGOS_TRACE_DECLINE"); t && *t && *t != '0')
        std::fprintf(stderr, "[decline round=%u] %s: %s (%s)\n",
                     gen_round(), engine, key.c_str(), why.c_str());
    declines().push_back(Decline{ engine, std::move(key), std::move(why),
                                  gen_round() });
}

// #61: the gen ROUND counter. A metaprog compile runs the front end once per
// fixpoint iteration and once per metacall; only the LAST run's program is
// emitted, and `verify_layout_engines()` compares against that emitted layout.
// Stamping declines with the round lets the verifier judge the round it is
// actually about (see the note over `struct Decline`).
static unsigned& gen_round_slot() noexcept { static unsigned r = 0; return r; }
unsigned gen_round() noexcept { return gen_round_slot(); }
void end_gen_round() noexcept { ++gen_round_slot(); }

std::string kind_key(LogosType::Kind k) noexcept {
    return "<kind " + std::to_string(static_cast<int>(k)) + ">";
}

}  // namespace logos::compiler::layout
