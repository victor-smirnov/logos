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

void record(const char* engine, std::string key, L answer) noexcept {
    if (!recording_enabled()) return;
    if (key.empty()) return;
    ledger().push_back(LedgerEntry{ engine, std::move(key), answer });
}

}  // namespace logos::compiler::layout
