#pragma once
// B65: outlives reasoning over named lifetime regions.
//
// `outlives(long, short, graph)` answers: does region `long` live at least
// as long as region `short`? Returns true for:
//   - reflexive:  long == short
//   - 'static:    'static outlives every region
//   - elision:    empty short is treated as the most permissive — every region
//                 outlives the unconstrained one
//   - direct:     (long, short) appears in the explicit outlives graph
//   - transitive: there exists c such that long: c and c: short (BFS)
//
// The graph is a sequence of (longer, shorter) pairs as parsed from
// `where 'long: 'short` / `'long: 'short + 'mid` / etc.

#include <logos/compiler/probe.hpp>

#include <queue>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace logos::compiler {

// Both `'a` and the bare `a` forms are accepted; LIFETIME terminals in the
// grammar include the leading apostrophe, while elsewhere we sometimes have
// the bare name. Normalise to with-apostrophe for graph keys.
inline std::string outlives_norm(std::string_view s) {
    if (s.empty()) return {};
    if (s.front() == '\'') return std::string(s);
    return std::string("'") + std::string(s);
}

// THE BINDERS OF THE SCOPE BEING CHECKED. `outlives()` is handed two STRINGS
// and cannot say which BINDER each one denotes; two names that collide by
// spelling are not two names of one scope. This set is that missing fact — the
// lifetime parameters declared by the generic scope whose body is under check
// (the fn's own, plus its `impl` block's) — refilled at each fn in
// sema_decl.cpp beside `current_outlives_`. Empty outside a fn body, which is
// exactly when the rule below must not fire.
//
// ⚠ IT IS NOT SOUND BY CONSTRUCTION AND IT DOES NOT CLAIM TO BE: it works by
// name collision, so a callee's `'a` that happens to be spelled like the
// caller's is caught and the same program with the callee's binder renamed is
// missed. The measured pair is in src/compiler/PROBES.md (u7 / u8) and is
// pinned as a fixture pair. What removes the caveat is SUBSTITUTION, which is
// its own round.
inline std::unordered_set<std::string>& current_lt_binders() {
    static std::unordered_set<std::string> s;
    return s;
}

// A MINTED REGION — the name an elided slot did not have. `""` means both
// "elided here" and "the same region as that other elided slot"; a minted name
// means exactly the first. The prefix is unspellable in the grammar, so a
// minted name can never collide with a user's binder, and every consumer that
// treats an elided slot as ABSENT (diagnostics, the undeclared-lifetime walk,
// borrow_check's elision contract) asks `lt_is_minted` and keeps its old
// answer. Only the comparators (subtype/outlives) see the name.
inline bool lt_is_minted(std::string_view lt) {
    return lt.size() > 1 && lt[0] == '\'' && lt[1] == '%';
}

inline bool outlives_is_static(std::string_view lt) {
    return lt == "'static" || lt == "static";
}

// Build a forward adjacency map (longer → set of shorters it outlives directly).
// Caller passes parsed pairs; this just indexes them. Symmetry: an entry
// `(a, b)` means a: b (a lives at least as long as b).
inline std::unordered_map<std::string, std::unordered_set<std::string>>
outlives_adj(const std::vector<std::pair<std::string, std::string>>& pairs) {
    std::unordered_map<std::string, std::unordered_set<std::string>> adj;
    for (auto& [longer, shorter] : pairs) {
        if (longer.empty() || shorter.empty()) continue;
        adj[outlives_norm(longer)].insert(outlives_norm(shorter));
    }
    return adj;
}

// Decide whether `longer` outlives `shorter` given the graph.
// Reflexive, static-is-top, with BFS for transitive paths. When
// `permissive_empty` is true, an empty (elided) lifetime on the sub side
// is treated as compatible with any named region — appropriate at
// variance/subtype coercion sites where call-site region inference will
// pick a unification. The borrow-check return path passes `false` to
// keep elided sources from silently satisfying a named return.
inline bool outlives(
    std::string_view longer,
    std::string_view shorter,
    const std::unordered_map<std::string, std::unordered_set<std::string>>& adj,
    bool permissive_empty = true)
{
    auto L = outlives_norm(longer);
    auto S = outlives_norm(shorter);
    // PROBES ltelidesup / ltelidesub / ltelideboth — AN ELIDED REGION HAS NO
    // NAME, so `outlives()` treats it as compatible with everything. That, not
    // region inference, is what admits the lifereg.A elision rows: naming the
    // slots by hand makes three of them REFUSE today, unarmed.
    if (S.empty()) {
        if (logos::probe::on("ltelidesup") ||
            logos::probe::on("ltelideboth") ||
            logos::probe::on("ltmintfresh")) return false;
        return true;                        // unconstrained short side
    }
    if (permissive_empty && L.empty()) {
        if (logos::probe::on("ltelidesub") ||
            logos::probe::on("ltelideboth") ||
            logos::probe::on("ltmintfresh")) return false;
        return true;                        // see comment above
    }
    if (outlives_is_static(L)) return true; // 'static outlives all
    if (L == S) return true;                // reflexive

    std::unordered_set<std::string> seen;
    std::queue<std::string> q;
    q.push(L);
    seen.insert(L);
    while (!q.empty()) {
        auto cur = std::move(q.front());
        q.pop();
        auto it = adj.find(cur);
        if (it == adj.end()) continue;
        for (auto& nb : it->second) {
            if (nb == S) return true;
            if (seen.insert(nb).second) q.push(nb);
        }
    }
    // Permissive default for two named generic lifetimes (neither static)
    // when NEITHER appears anywhere in the explicit outlives graph: assume
    // the caller's region inference will pick a unification. Gated by
    // `permissive_empty` — strict-mode callers (e.g. borrow_check return
    // path) opt out and get conservative-reject.
    if (outlives_is_static(S)) return false;
    if (!permissive_empty) return false;
    auto mentioned = [&](const std::string& lt) {
        if (adj.count(lt)) return true;
        for (auto& [k, v] : adj) {
            (void)k;
            if (v.count(lt)) return true;
        }
        return false;
    };
    // PROBE lifereg_unmentioned: the single permissive default UPSTREAM of
    // lifereg_callargstrict and lifereg_structlitstrict. MENTIONED-NESS, not
    // the constraint, is what flips the answer — adding `where 'a: 'b` turns
    // an admission into a refusal. Rust's rule is the opposite.
    // Numbers live in src/compiler/PROBES.md (re-measured 2026-08-31 against
    // the 310-row ledger; the 2026-08-27 pair that used to sit here was a
    // 423-row measurement and was never recorded outside this comment).
    if (logos::probe::on("lifereg_unmentioned")) return false;
    // LANDED 2026-08-31. MENTIONED-NESS is not a relation: two named generic
    // regions that appear in no `where` clause are UNRELATED, and Rust refuses
    // the coercion between them. The permissive default below admitted them.
    //
    // It is narrowed to the case where BOTH names are binders of the scope
    // actually being checked — see `current_lt_binders()`. The wider rule (the
    // `lifereg_unmentioned` probe directly above) closes two more rows and
    // refuses FIVE legal programs to do it, every one of them comparing a name
    // from ONE binder against a name from ANOTHER: a callee's or a struct's own
    // lifetime parameter that reached here UNSUBSTITUTED. Numbers, the two rows
    // given up and the counter-examples: src/compiler/PROBES.md 2026-08-31h.
    if (current_lt_binders().count(L) && current_lt_binders().count(S))
        return false;
    if (mentioned(L) || mentioned(S)) return false;
    return true;
}

// Convenience: build + query in one shot.
inline bool outlives(
    std::string_view longer,
    std::string_view shorter,
    const std::vector<std::pair<std::string, std::string>>& pairs,
    bool permissive_empty = true)
{
    return outlives(longer, shorter, outlives_adj(pairs), permissive_empty);
}

} // namespace logos::compiler
