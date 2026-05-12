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
// Reflexive, static-is-top, with BFS for transitive paths.
inline bool outlives(
    std::string_view longer,
    std::string_view shorter,
    const std::unordered_map<std::string, std::unordered_set<std::string>>& adj)
{
    auto L = outlives_norm(longer);
    auto S = outlives_norm(shorter);
    if (S.empty()) return true;             // unconstrained short side
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
    return false;
}

// Convenience: build + query in one shot.
inline bool outlives(
    std::string_view longer,
    std::string_view shorter,
    const std::vector<std::pair<std::string, std::string>>& pairs)
{
    return outlives(longer, shorter, outlives_adj(pairs));
}

} // namespace logos::compiler
