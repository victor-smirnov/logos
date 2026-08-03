// unit_graph — see include/logos/compiler/unit_graph.hpp for the model.
#include <logos/compiler/unit_graph.hpp>
#include <logos/compiler/lir_view.hpp>
#include <logos/compiler/lir_schema.hpp>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <fstream>
#include <functional>
#include <unordered_set>

namespace logos::compiler {

namespace {

// A unit key that came from an emitter is Generated; one that is a filesystem
// path is Source; the single reserved key is Common. The kind is a property of
// the KEY, derived once here, so nothing else has to guess.
UnitId::Kind kind_of_key(const std::string& key,
                         const std::unordered_set<std::string>& source_paths) {
    if (key == kCommonKey) return UnitId::Kind::Common;
    return source_paths.count(key) ? UnitId::Kind::Source : UnitId::Kind::Generated;
}

// SDN string escaping — same inverse of parse_writ the doc-facts sidecar uses.
void sdn_quote(std::string_view s, std::string& out) {
    out += '"';
    for (char c : s) {
        switch (c) {
        case '"':  out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n";  break;
        case '\t': out += "\\t";  break;
        case '\r': out += "\\r";  break;
        default:
            if (static_cast<unsigned char>(c) < 0x20) {
                char buf[8]; std::snprintf(buf, sizeof(buf), "\\u%04x", c); out += buf;
            } else out += c;
        }
    }
    out += '"';
}

bool is_hex16(std::string_view s) {
    if (s.size() != 16) return false;
    for (char c : s)
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')))
            return false;
    return true;
}

// The family hash tag as it appears in a mangled link name, in BOTH spellings.
// Missing the second one undercounts by 32% (measured), which is exactly why
// this is the canary and not the mechanism — a canary that silently agrees
// because it looked in one place is worse than no canary.
std::string family_tag_of(std::string_view link) {
    for (size_t i = 0; i + 18 <= link.size(); ++i) {
        if (link[i] == 'H' && link[i + 1] == 's' && is_hex16(link.substr(i + 2, 16)))
            return std::string(link.substr(i, 18));
        if (link[i] == 'h' && link[i + 1] == 's' && link[i + 2] == '_' &&
            i + 19 <= link.size() && is_hex16(link.substr(i + 3, 16)))
            return "Hs" + std::string(link.substr(i + 3, 16));
    }
    return {};
}

// Every non-extern function of a program, free fns and methods alike, with its
// qualified link name. ONE definition of "the functions the backend would
// emit", so the partition's totality means the same thing everywhere.
template <class F>
void each_emitted_fn(const lir::LProgram& prog, F&& f) {
    for (auto& fn : prog.functions) {
        if (!fn || fn.is_extern()) continue;
        f(fn, sym::link_name(fn, prog.pkg_module_ids));
    }
    for (auto& sd : prog.structs)
        sd.each_method([&](lir_view::FunctionView m) {
            if (!m || m.is_extern()) return;
            f(m, sym::link_name(m, prog.pkg_module_ids));
        });
}

} // namespace

// ── The edge SOURCE, accumulated ───────────────────────────────────────────
// See unit_graph.hpp: metacall_sites is a worklist the dispatch loop drains,
// so the only program in which a case-1 site is visible is the ROUND that
// dispatched it. note_program is called once per round.
void UnitOrderFacts::note(UnitOrderFact f) {
    for (const auto& e : facts_)
        if (e.consumer_ast == f.consumer_ast && e.site_off == f.site_off &&
            e.cause == f.cause && e.provider_file == f.provider_file)
            return;
    facts_.push_back(std::move(f));
}

void UnitOrderFacts::note_program(const lir::LProgram& prog) {
    // ── GATE SEAM, and the only way this file's safety property is testable ──
    // `LOGOS_UNITS_NO_ORDER_FACTS=1` makes the accumulator record nothing. It
    // exists because two claims in this file are otherwise unfalsifiable:
    //
    //   1. the edges come FROM the accumulation — with it off, a case-1 module
    //      must report edges=0, so a green "edges=1" is sensitive to the
    //      mechanism and not to some other coincidence;
    //   2. the total-order degradation is LIVE code — with it off, levels()
    //      must become one unit per level and order_established false.
    //
    // Same shape as LOGOS_WQL_FORCE_ORDER: a compile-time knob whose only
    // consumer is the gate that proves the check can fail. It can only make the
    // schedule MORE conservative, never less.
    static const bool suppressed = [] {
        const char* v = std::getenv("LOGOS_UNITS_NO_ORDER_FACTS");
        return v && v[0] && v[0] != '0';
    }();
    if (suppressed) return;

    ++rounds_;
    for (auto& site : prog.metacall_sites)
        note(UnitOrderFact{(size_t)site.ast_idx(), (uint32_t)site.expr_offset(),
                           std::string(site.def_source_file()),
                           std::string(site.callee_name()),
                           UnitEdge::Cause::Metacall});
    for (auto& tgt : prog.metaprog_targets)
        for (auto& h : prog.metaprog_handlers) {
            if (h.trigger() != tgt.trigger()) continue;
            note(UnitOrderFact{(size_t)tgt.ast_idx(), (uint32_t)tgt.item_offset(),
                               std::string(h.def_source_file()),
                               std::string(tgt.trigger()),
                               UnitEdge::Cause::Trigger});
        }
}

// ── Phase A: nodes + order edges ───────────────────────────────────────────
UnitGraph UnitGraph::build(const lir::LProgram&            prog,
                           const std::vector<std::string>& filenames,
                           const std::vector<std::string>& ast_unit_key,
                           const std::vector<bool>&        from_binary,
                           const UnitOrderFacts&           facts) {
    UnitGraph g;

    // Unit 0 is ALWAYS Common. It exists whether or not anything lands in it,
    // because the ownership rule needs a top element and the schedule needs a
    // node to put join functions in.
    g.units_.push_back(UnitId{UnitId::Kind::Common, std::string(kCommonKey)});
    g.by_key_[std::string(kCommonKey)] = kCommon;

    // Which keys are real source paths — decided from the FILENAMES array, not
    // by sniffing the string for a '/'.
    std::unordered_set<std::string> source_paths;
    for (size_t i = 0; i < filenames.size(); ++i) {
        bool dep = i < from_binary.size() && from_binary[i];
        if (dep) continue;
        const std::string& f = filenames[i];
        if (!f.empty() && f.front() != '<') source_paths.insert(f);
    }

    auto intern = [&](const std::string& key) -> uint32_t {
        auto it = g.by_key_.find(key);
        if (it != g.by_key_.end()) return it->second;
        uint32_t id = (uint32_t)g.units_.size();
        g.units_.push_back(UnitId{kind_of_key(key, source_paths), key});
        g.by_key_[key] = id;
        return id;
    };

    g.ast_unit_.assign(filenames.size(), kNone);
    for (size_t i = 0; i < filenames.size(); ++i) {
        // A dependency AST is not a unit OF THIS MODULE. Its bodies come from
        // the dependency's own archive; scheduling them here would recompile
        // code that is already object code.
        if (i < from_binary.size() && from_binary[i]) continue;
        g.ast_unit_[i] = intern(unit_key_for_ast(filenames, ast_unit_key, i));
    }

    // ── Edges ──────────────────────────────────────────────────────────────
    // Provider resolution keys on the DEFINITION FILE the schema carries
    // (§1.4). THREE outcomes, and keeping them apart is the whole point:
    //
    //   a unit of THIS module  → a real intra-module order edge;
    //   a file this module does not own → the provider is already object code
    //                            in a dependency archive; no order to impose;
    //   EMPTY                  → the provider is UNKNOWN. There may be an edge
    //                            here and we cannot see it, so the graph must
    //                            not go on claiming independence — see
    //                            compute_levels()'s total-order degradation.
    //
    // The pre-accumulator code collapsed the last two into "no edge", which is
    // how "provider unknown" came to read exactly like "provider is external".
    auto provider_of_file = [&](std::string_view file) -> uint32_t {
        if (file.empty()) return kNone;
        auto it = g.by_key_.find(std::string(file));
        return it == g.by_key_.end() ? kNone : it->second;
    };

    g.order_rounds_ = facts.rounds();
    g.order_facts_  = facts.facts().size();
    for (const auto& f : facts.facts()) {
        if (f.provider_file.empty()) { ++g.unresolved_providers_; continue; }
        uint32_t consumer = g.unit_of_ast(f.consumer_ast);
        uint32_t provider = provider_of_file(f.provider_file);
        if (provider == kNone) { ++g.external_providers_; continue; }
        if (consumer == kNone) { ++g.unresolved_providers_; continue; }
        g.edges_.push_back(UnitEdge{consumer, provider,
                                    (uint32_t)f.consumer_ast, f.site_off, f.cause});
    }

    // THE discriminator, stated once. levels() may only claim independence
    // from a COMPLETE edge set; anything less falls back to the total order.
    g.order_established_ = facts.consulted() && g.unresolved_providers_ == 0;

    // Deduplicate: many sites in one file calling one metafunction are ONE
    // ordering constraint. The first occurrence keeps its provenance.
    std::sort(g.edges_.begin(), g.edges_.end(), [](const UnitEdge& a, const UnitEdge& b) {
        if (a.from != b.from) return a.from < b.from;
        if (a.to != b.to) return a.to < b.to;
        return a.site_ast < b.site_ast;
    });
    g.edges_.erase(std::unique(g.edges_.begin(), g.edges_.end(),
                               [](const UnitEdge& a, const UnitEdge& b) {
                                   return a.from == b.from && a.to == b.to;
                               }),
                   g.edges_.end());

    g.compute_levels();
    return g;
}

// ── SCC condensation + topological levels ──────────────────────────────────
// A cycle in "needs machine code from" IS the bootstrap cycle (§2.1); the
// condensation finds it exactly, so no attribute has to be kept in sync with
// reality. Iterative Tarjan — a module can have thousands of units and a
// recursive walk would be a stack-depth question nobody wants to answer.
void UnitGraph::compute_levels() {
    const uint32_t n = (uint32_t)units_.size();

    // ── The unsafe direction, closed ───────────────────────────────────────
    // A level holding k units says "these k may be built CONCURRENTLY, in any
    // order". That is a positive claim, and an empty/incomplete edge set is no
    // evidence for it — yet it is exactly what an empty edge set produced:
    // every unit at level 0. The degradation has to go the OTHER way. With no
    // trustworthy edges the only defensible schedule is the sequential one, so
    // emit it literally: one unit per level, index order. Every consumer of
    // levels() then does the right thing without knowing this rule exists,
    // and order_established() tells the ones that care.
    if (!order_established_) {
        scc_.assign(n, 0);
        levels_.assign(n, {});
        for (uint32_t v = 0; v < n; ++v) {
            scc_[v] = v;
            levels_[v].push_back(UnitGroup{v, {v}});
        }
        return;
    }

    std::vector<std::vector<uint32_t>> adj(n);   // consumer → providers
    for (auto& e : edges_) adj[e.from].push_back(e.to);

    std::vector<uint32_t> index(n, UINT32_MAX), low(n, 0), stack;
    std::vector<char> on_stack(n, 0);
    scc_.assign(n, UINT32_MAX);
    uint32_t next_index = 0, next_scc = 0;

    struct Frame { uint32_t v; size_t i; };
    for (uint32_t root = 0; root < n; ++root) {
        if (index[root] != UINT32_MAX) continue;
        std::vector<Frame> call;
        call.push_back({root, 0});
        index[root] = low[root] = next_index++;
        stack.push_back(root); on_stack[root] = 1;
        while (!call.empty()) {
            auto& fr = call.back();
            if (fr.i < adj[fr.v].size()) {
                uint32_t w = adj[fr.v][fr.i++];
                if (index[w] == UINT32_MAX) {
                    index[w] = low[w] = next_index++;
                    stack.push_back(w); on_stack[w] = 1;
                    call.push_back({w, 0});
                } else if (on_stack[w]) {
                    low[fr.v] = std::min(low[fr.v], index[w]);
                }
            } else {
                uint32_t v = fr.v;
                call.pop_back();
                if (!call.empty()) low[call.back().v] = std::min(low[call.back().v], low[v]);
                if (low[v] == index[v]) {
                    for (;;) {
                        uint32_t w = stack.back(); stack.pop_back(); on_stack[w] = 0;
                        scc_[w] = next_scc;
                        if (w == v) break;
                    }
                    ++next_scc;
                }
            }
        }
    }

    // Level of an SCC = 1 + max level of the SCCs it needs code from. Tarjan
    // numbers SCCs in reverse topological order, so a single pass in that
    // order already sees every provider settled.
    std::vector<uint32_t> scc_level(next_scc, 0);
    for (uint32_t s = 0; s < next_scc; ++s) {
        uint32_t lv = 0;
        for (uint32_t v = 0; v < n; ++v) {
            if (scc_[v] != s) continue;
            for (uint32_t w : adj[v])
                if (scc_[w] != s) lv = std::max(lv, scc_level[scc_[w]] + 1);
        }
        scc_level[s] = lv;
    }

    uint32_t max_level = 0;
    for (uint32_t s = 0; s < next_scc; ++s) max_level = std::max(max_level, scc_level[s]);
    levels_.assign(max_level + 1, {});
    // One GROUP per SCC, not one entry per unit: the units of an SCC need each
    // other's machine code and go to ONE worker together (case 2). Emitting
    // them as separate entries of one level would state the opposite.
    std::vector<std::vector<uint32_t>> scc_members(next_scc);
    for (uint32_t v = 0; v < n; ++v) scc_members[scc_[v]].push_back(v);
    for (uint32_t s = 0; s < next_scc; ++s) {
        if (scc_members[s].empty()) continue;
        std::sort(scc_members[s].begin(), scc_members[s].end());
        levels_[scc_level[s]].push_back(UnitGroup{s, std::move(scc_members[s])});
    }
    // Deterministic within a level: lowest unit index first. The parallel
    // driver may REORDER the work it hands to the pool (LPT), but the ORDER OF
    // RECORD — what the archive members are appended in, what the sidecar says
    // — is this one, computed before any worker starts.
    for (auto& lv : levels_)
        std::sort(lv.begin(), lv.end(), [](const UnitGroup& a, const UnitGroup& b) {
            return a.units.front() < b.units.front();
        });
}

// ── Phase B: ownership ─────────────────────────────────────────────────────
void UnitGraph::assign_ownership(const lir::LProgram& post_mono) {
    owner_.clear();
    unit_fn_count_.assign(units_.size(), 0);

    // name() → link name, and name() → the fn view. mlir_gen's own reach
    // analysis already assumes fn.name() is unique across a program (its
    // by_name map is built the same way); the call-graph edges in the LIR are
    // spelled in terms of name(), so the walk has to be too.
    std::unordered_map<std::string, std::string> link_of;
    std::unordered_map<std::string, lir_view::FunctionView> by_name;
    std::unordered_map<std::string, uint32_t> declared;   // name → unit

    std::unordered_set<std::string> dependency;   // body owned by ANOTHER module
    each_emitted_fn(post_mono, [&](lir_view::FunctionView fn, std::string link) {
        std::string nm(fn.name());
        by_name[nm] = fn;
        // A call site spells its callee EITHER bare or module-qualified —
        // mlir_gen keeps a "bare-miss → link_name → qualified-hit" cache for
        // exactly this. Indexing both spellings up front is the same fact with
        // no lookup order to get wrong. Without it the §1.3 walk resolved 14%
        // of call edges (measured 1857 hits / 11716 misses) and the ownership
        // rule silently derived almost nothing.
        if (link != nm) by_name.emplace(link, fn);
        link_of[nm] = link;
        std::string_view uk = fn.unit_key();
        if (!uk.empty()) {
            uint32_t u = find_unit(uk);
            // A key naming an AST this module does not own is a DEPENDENCY
            // body: build() deliberately makes no unit for those. It still has
            // to be somewhere, and Common is always emitted — but it is not
            // evidence that THIS module failed to partition itself, so the
            // census counts it apart. Without that split a module that
            // attributed every function it owns reports 3%.
            if (u == kNone) { declared[nm] = kCommon; dependency.insert(nm); }
            else            { declared[nm] = u; }
        }
    });

    // Callee sets, computed ONCE. The walk is deliberately an
    // over-approximation in the same direction mlir_gen's is: a missed edge
    // would put a function in a unit its only caller cannot see, which is a
    // link failure; a spurious edge only widens an owner toward Common, which
    // costs parallelism and never correctness.
    std::unordered_map<std::string, std::vector<std::string>> callees;
    // How much of the program the derivation could actually SEE. A rule that
    // silently reaches nothing looks identical to a rule that reached
    // everything and found nothing to attribute; these counters tell them
    // apart, and they are printed.
    //
    // READ callee_misses CORRECTLY: the walk calls note() on every VarRef and
    // AddrOf name too (a fn used as a value is indistinguishable from a local
    // at this level, and mlir_gen's reach walker has the same shape), so most
    // misses are ordinary local variables, NOT unresolved calls. The counter
    // bounds the walk's coverage; it is not a defect rate.
    size_t bodies_walked = 0, callee_hits = 0, callee_misses = 0;
    {
        std::function<void(lir_view::BlockRef, std::vector<std::string>&)> walk_block;
        std::function<void(lir_view::StmtRef, std::vector<std::string>&)>  walk_stmt;
        std::function<void(lir_view::ExprRef, std::vector<std::string>&)>  walk_expr;

        auto note = [&](std::string_view name, std::vector<std::string>& out) {
            if (name.empty()) return;
            std::string s(name);
            auto it = by_name.find(s);
            if (it == by_name.end()) { ++callee_misses; return; }
            ++callee_hits;
            // Normalise to the fn's OWN name key so the propagation map has one
            // entry per function, not one per spelling.
            out.push_back(std::string(it->second.name()));
        };

        walk_expr = [&](lir_view::ExprRef e, std::vector<std::string>& out) {
            if (!e) return;
            using C = lir_schema::expr::Code;
            switch (e.kind()) {
            case C::Call: {
                lir_view::ECallView v{e};
                note(v.callee(), out);
                v.each_arg([&](lir_view::ExprRef a) { walk_expr(a, out); });
                break;
            }
            case C::MethodCall: {
                lir_view::EMethodCallView v{e};
                note(v.resolved_symbol(), out);
                walk_expr(v.receiver(), out);
                v.each_arg([&](lir_view::ExprRef a) { walk_expr(a, out); });
                break;
            }
            case C::ClosureCall: {
                lir_view::EClosureCallView v{e};
                walk_expr(v.callee(), out);
                v.each_arg([&](lir_view::ExprRef a) { walk_expr(a, out); });
                break;
            }
            case C::FnPtrCall: {
                lir_view::EFnPtrCallView v{e};
                walk_expr(v.callee(), out);
                v.each_arg([&](lir_view::ExprRef a) { walk_expr(a, out); });
                break;
            }
            case C::AddrOf:
                note(lir_view::EAddrOfView{e}.var_name(), out);
                break;
            case C::GenericRef:
                note(lir_view::EGenericRefView{e}.name(), out);
                break;
            case C::VarRef:
                note(lir_view::EVarRefView{e}.name(), out);
                break;
            default: {
                auto recurse_arr = [&](uint8_t key) {
                    auto av = e.mirror()->get(key);
                    if (av.is_null()) return;
                    auto* arr = av.as_ptr<const writ::ObjectArray>();
                    for (uint64_t i = 0; i < arr->size(); ++i) {
                        auto el = arr->get(i);
                        if (!el.is_null())
                            walk_expr(lir_view::detail::make_sub_ref<lir_view::ExprRef>(e, el), out);
                    }
                };
                auto recurse_sub = [&](uint8_t key) { walk_expr(e.sub_expr(key), out); };
                namespace ek = lir_schema::expr_keys;
                recurse_sub(ek::LHS.code);      recurse_sub(ek::RHS.code);
                recurse_sub(ek::OPERAND.code);  recurse_sub(ek::RECEIVER.code);
                recurse_sub(ek::SCRUT.code);    recurse_sub(ek::INDEX.code);
                recurse_sub(ek::CALLEE.code);   recurse_sub(ek::FMT.code);
                recurse_arr(ek::ARGS.code);     recurse_arr(ek::ELEMS.code);
                recurse_arr(ek::FIELD_VALUES.code); recurse_arr(ek::PAYLOAD.code);
                {
                    auto av = e.mirror()->get(ek::ARMS.code);
                    if (!av.is_null()) {
                        auto* arr = av.as_ptr<const writ::ObjectArray>();
                        for (uint64_t i = 0; i < arr->size(); ++i) {
                            auto el = arr->get(i);
                            if (el.is_null()) continue;
                            auto arm = lir_view::detail::make_sub_ref<lir_view::EMatchArmRef>(e, el);
                            walk_expr(arm.guard(), out);
                            walk_expr(arm.value(), out);
                            walk_block(arm.body(), out);
                        }
                    }
                }
                break;
            }
            }
        };

        walk_stmt = [&](lir_view::StmtRef s, std::vector<std::string>& out) {
            if (!s) return;
            using SC = lir_schema::stmt::Code;
            switch (s.kind()) {
            case SC::Let:      walk_expr(lir_view::SLetView{s}.value(), out); break;
            case SC::Assign:   walk_expr(lir_view::SAssignView{s}.value(), out); break;
            case SC::Return:   walk_expr(lir_view::SReturnView{s}.value(), out); break;
            case SC::ExprStmt: walk_expr(lir_view::SExprStmtView{s}.expr(), out); break;
            case SC::If: {
                lir_view::SIfView v{s};
                walk_expr(v.cond(), out); walk_block(v.then_block(), out);
                walk_block(v.else_block(), out);
                break;
            }
            case SC::While: {
                lir_view::SWhileView v{s};
                walk_expr(v.cond(), out); walk_block(v.body(), out);
                break;
            }
            case SC::For: {
                lir_view::SForView v{s};
                walk_expr(v.lo(), out); walk_expr(v.hi(), out); walk_block(v.body(), out);
                break;
            }
            case SC::Loop:  walk_block(lir_view::SLoopView{s}.body(), out); break;
            case SC::Block: walk_block(lir_view::SBlockView{s}.body(), out); break;
            case SC::Match: {
                lir_view::SMatchView v{s};
                walk_expr(v.scrut(), out);
                v.each_arm([&](lir_view::EMatchArmRef arm) {
                    walk_expr(arm.guard(), out);
                    walk_expr(arm.value(), out);
                    walk_block(arm.body(), out);
                });
                break;
            }
            case SC::FieldWrite:      walk_expr(lir_view::SFieldWriteView{s}.value(), out); break;
            case SC::DerefFieldWrite: walk_expr(lir_view::SDerefFieldWriteView{s}.value(), out); break;
            case SC::IndexWrite: {
                lir_view::SIndexWriteView v{s};
                walk_expr(v.index(), out); walk_expr(v.value(), out);
                break;
            }
            default: break;
            }
        };

        walk_block = [&](lir_view::BlockRef b, std::vector<std::string>& out) {
            if (!b) return;
            b.each_stmt([&](lir_view::StmtRef s) { walk_stmt(s, out); });
        };

        if (post_mono.type_pool.arena()) {
            // Iterate link_of, not by_name: by_name holds TWO keys per function
            // (bare + module-qualified) so a walk over it would visit every
            // body twice and inflate every counter derived from it.
            for (auto& [nm, _link] : link_of) {
                auto bit = by_name.find(nm);
                if (bit == by_name.end()) continue;
                auto fn = bit->second;
                auto fb = fn.body();
                if (!fb) continue;
                ++bodies_walked;
                std::vector<std::string> out;
                walk_block(fb, out);
                std::sort(out.begin(), out.end());
                out.erase(std::unique(out.begin(), out.end()), out.end());
                if (!out.empty()) callees[nm] = std::move(out);
            }
        }
    }

    // The §1.3 rule as a lattice propagation. UNSET → u → Common; every node
    // changes at most twice, so this terminates without a visit cap.
    constexpr uint32_t UNSET = UINT32_MAX;
    std::unordered_map<std::string, uint32_t> derived;
    std::deque<std::string> work;
    for (auto& [nm, u] : declared) work.push_back(nm);

    auto owner_now = [&](const std::string& nm) -> uint32_t {
        auto d = declared.find(nm);
        if (d != declared.end()) return d->second;
        auto v = derived.find(nm);
        return v == derived.end() ? UNSET : v->second;
    };

    while (!work.empty()) {
        std::string f = std::move(work.front());
        work.pop_front();
        uint32_t u = owner_now(f);
        if (u == UNSET) continue;
        auto it = callees.find(f);
        if (it == callees.end()) continue;
        for (const auto& c : it->second) {
            if (declared.count(c)) continue;   // declared owners are FIXED
            auto d = derived.find(c);
            if (d == derived.end()) { derived[c] = u; work.push_back(c); }
            else if (d->second != u && d->second != kCommon) {
                d->second = kCommon; work.push_back(c);
            }
        }
    }

    stats_ = Census{};
    std::set<std::string> unref_files;
    stats_.bodies_walked = bodies_walked;
    stats_.callee_hits = callee_hits;
    stats_.callee_misses = callee_misses;
    for (auto& [nm, link] : link_of) {
        uint32_t u = owner_now(nm);
        if (declared.count(nm)) {
            if (dependency.count(nm)) ++stats_.fns_dependency;
            else                      ++stats_.fns_declared;
        } else if (u == UNSET) {
            ++stats_.fns_unreferenced;   // undeclared AND never referenced
            // Would `source_file` give this function an owner? Counted over
            // exactly the population that Common swallows today.
            auto bit = by_name.find(nm);
            std::string_view sf = (bit != by_name.end()) ? bit->second.source_file()
                                                         : std::string_view{};
            if (sf.empty()) ++stats_.unref_src_empty;
            else {
                std::string key(sf);
                bool is_module_src = false;
                for (const auto& un : units_)
                    if (un.kind == UnitId::Kind::Source && un.key == key) {
                        is_module_src = true; break;
                    }
                if (is_module_src) { ++stats_.unref_src_module; unref_files.insert(key); }
                else               ++stats_.unref_src_other;
            }
        } else {
            ++stats_.fns_derived;
        }
        // Undeclared AND unreferenced → Common. It still has to be emitted.
        if (u == UNSET) u = kCommon;
        owner_[link] = u;
        if (u < unit_fn_count_.size()) ++unit_fn_count_[u];
    }
    stats_.unref_src_files = unref_files.size();
}

UnitGraph::Census UnitGraph::census() const {
    Census c = stats_;
    c.units = units_.size();
    c.edges = edges_.size();
    c.levels = levels_.size();
    c.order_established    = order_established_;
    c.order_rounds         = order_rounds_;
    c.order_facts          = order_facts_;
    c.unresolved_providers = unresolved_providers_;
    c.external_providers   = external_providers_;
    for (auto& lv : levels_) {
        c.max_level_width = std::max(c.max_level_width, lv.size());
        for (auto& gr : lv) if (gr.units.size() > 1) ++c.bootstrap_cycles;
    }
    c.fns_total = owner_.size();
    for (auto& [link, u] : owner_) if (u != kCommon) ++c.fns_non_common;
    for (size_t i = 0; i < unit_fn_count_.size(); ++i)
        c.largest_unit_fns = std::max(c.largest_unit_fns, unit_fn_count_[i]);
    return c;
}

UnitGraph::VerifyResult
UnitGraph::verify_against_mangled_tags(const lir::LProgram& post_mono,
                                       std::vector<std::string>& out_msgs) const {
    VerifyResult r;
    // tag → the units its DECLARED functions landed in, and the reverse.
    std::unordered_map<std::string, std::unordered_set<uint32_t>> tag_units;
    std::unordered_map<uint32_t, std::unordered_set<std::string>> unit_tags;

    each_emitted_fn(post_mono, [&](lir_view::FunctionView fn, std::string link) {
        std::string tag = family_tag_of(link);
        if (tag.empty()) return;
        ++r.fns_tagged;
        // Only DECLARED functions carry a promise. A mono clone that still
        // shows a family tag in its name may legitimately be shared between
        // families and derived into Common — that is the ownership rule
        // working, not a drift.
        if (fn.unit_key().empty()) return;
        uint32_t u = owner_of(link);
        // Common is the residue node; a tagged function there is unclaimed,
        // not mis-claimed. Counted and reported, excluded from the verdict —
        // see VerifyResult::tagged_in_common for why the tag cannot decide it.
        if (u == kCommon) { ++r.tagged_in_common; return; }
        tag_units[tag].insert(u);
        unit_tags[u].insert(tag);
    });
    r.tags_seen = tag_units.size();

    for (auto& [tag, us] : tag_units) {
        if (us.size() <= 1) continue;
        ++r.split_families;
        if (out_msgs.size() < 40) {
            std::string m = "unit-verify: family " + tag + " is SPLIT across " +
                            std::to_string(us.size()) + " units:";
            for (uint32_t u : us) m += " [" + units_[u].key + "]";
            out_msgs.push_back(std::move(m));
        }
    }
    for (auto& [u, tags] : unit_tags) {
        if (tags.size() <= 1) continue;
        ++r.merged_units;
        if (out_msgs.size() < 40) {
            std::string m = "unit-verify: unit [" + units_[u].key + "] MERGES " +
                            std::to_string(tags.size()) + " families:";
            for (auto& t : tags) m += " " + t;
            out_msgs.push_back(std::move(m));
        }
    }
    return r;
}

void UnitGraph::write_sidecar(const std::string& path,
                              const std::vector<uint32_t>& used_order) const {
    std::string out;
    out += "{\n  units: [\n";
    for (size_t i = 0; i < units_.size(); ++i) {
        out += "    { idx: " + std::to_string(i) + ", kind: ";
        switch (units_[i].kind) {
        case UnitId::Kind::Common:    out += "\"common\""; break;
        case UnitId::Kind::Source:    out += "\"source\""; break;
        case UnitId::Kind::Generated: out += "\"generated\""; break;
        }
        out += ", scc: " + std::to_string(scc_of((uint32_t)i));
        out += ", fns: " + std::to_string(i < unit_fn_count_.size() ? unit_fn_count_[i] : 0);
        out += ", key: ";
        sdn_quote(units_[i].key, out);
        out += " }";
        if (i + 1 < units_.size()) out += ",";
        out += "\n";
    }
    out += "  ],\n  edges: [\n";
    for (size_t i = 0; i < edges_.size(); ++i) {
        const auto& e = edges_[i];
        out += "    { from: " + std::to_string(e.from) + ", to: " + std::to_string(e.to) +
               ", site_ast: " + std::to_string(e.site_ast) +
               ", site_off: " + std::to_string(e.site_off) +
               ", cause: " + (e.cause == UnitEdge::Cause::Metacall ? "\"metacall\"" : "\"trigger\"") +
               ", same_scc: " + (scc_of(e.from) == scc_of(e.to) ? "true" : "false") + " }";
        if (i + 1 < edges_.size()) out += ",";
        out += "\n";
    }
    // A level is a list of GROUPS. `units` with more than one entry is a
    // bootstrap cycle (case 2) and is spelled that way so a reader of the
    // sidecar cannot mistake it for two independent units.
    out += "  ],\n  levels: [\n";
    for (size_t l = 0; l < levels_.size(); ++l) {
        out += "    [";
        for (size_t j = 0; j < levels_[l].size(); ++j) {
            if (j) out += ", ";
            out += "{ scc: " + std::to_string(levels_[l][j].scc) + ", units: [";
            for (size_t k = 0; k < levels_[l][j].units.size(); ++k) {
                if (k) out += ", ";
                out += std::to_string(levels_[l][j].units[k]);
            }
            out += "], cycle: " +
                   std::string(levels_[l][j].units.size() > 1 ? "true" : "false") + " }";
        }
        out += "]";
        if (l + 1 < levels_.size()) out += ",";
        out += "\n";
    }
    out += "  ],\n";
    // The order the DRIVER actually walked. The gate compares this against
    // flatten(levels()); if a future consumer re-derives an order of its own,
    // the sidecar disagrees and the gate fails. That is the mechanization of
    // "one computation, carried" — not a comment asking for it.
    out += "  used_order: [";
    for (size_t i = 0; i < used_order.size(); ++i) {
        if (i) out += ", ";
        out += std::to_string(used_order[i]);
    }
    out += "],\n";
    auto c = census();
    char pb[32];
    std::snprintf(pb, sizeof(pb), "%.3f", c.parallel_bound());
    out += "  census: { units: " + std::to_string(c.units) +
           ", edges: " + std::to_string(c.edges) +
           ", levels: " + std::to_string(c.levels) +
           ", max_level_width: " + std::to_string(c.max_level_width) +
           ", bootstrap_cycles: " + std::to_string(c.bootstrap_cycles) +
           // The order's PROVENANCE travels with the order. A consumer that
           // parallelises reads order_established; a reader who sees
           // `false` knows the levels are the sequential fallback and not a
           // discovery that the module has no dependencies.
           ", order_established: " + (c.order_established ? "true" : "false") +
           ", order_rounds: " + std::to_string(c.order_rounds) +
           ", order_facts: " + std::to_string(c.order_facts) +
           ", unresolved_providers: " + std::to_string(c.unresolved_providers) +
           ", external_providers: " + std::to_string(c.external_providers) +
           ", fns_total: " + std::to_string(c.fns_total) +
           ", fns_dependency: " + std::to_string(c.fns_dependency) +
           ", fns_module: " + std::to_string(c.fns_module()) +
           ", fns_declared: " + std::to_string(c.fns_declared) +
           ", fns_derived: " + std::to_string(c.fns_derived) +
           ", fns_unreferenced: " + std::to_string(c.fns_unreferenced) +
           ", fns_non_common: " + std::to_string(c.fns_non_common) +
           // Could source_file own the population Common swallows today?
           // Written next to fns_unreferenced so the two are read together.
           ", unref_src_module: " + std::to_string(c.unref_src_module) +
           ", unref_src_other: "  + std::to_string(c.unref_src_other) +
           ", unref_src_empty: "  + std::to_string(c.unref_src_empty) +
           ", unref_src_files: "  + std::to_string(c.unref_src_files) +
           // The complement, and what it COSTS. Written next to the
           // attribution rate so the two can never be read apart.
           ", fns_common: " + std::to_string(c.fns_common()) +
           ", largest_unit_fns: " + std::to_string(c.largest_unit_fns) +
           ", parallel_bound: " + pb + " }\n}\n";

    std::ofstream f(path);
    f << out;
}

} // namespace logos::compiler
