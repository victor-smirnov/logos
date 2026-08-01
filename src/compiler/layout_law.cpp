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
        LedgerEntry{ engine, std::move(key), answer.layout, answer.shape });
}

}  // namespace logos::compiler::layout
