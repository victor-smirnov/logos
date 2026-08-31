// Logos project — https://github.com/victor-smirnov/logos
//
// Borrow checker — affine ownership + reference exclusivity + dangling detection.
//
// Phase 1 — Linear ownership (use-after-move):
//   Move types  — structs with Drop impl and no Copy impl.
//   Copy types  — everything else (primitives, raw pointers, enums, &T, &mut T).
//   A Move variable is consumed on first EVarRef in value position; re-use is an error.
//
// Phase 2 — Borrow exclusivity:
//   &T   (Ref)    — shared borrow: multiple allowed, blocks moves and &mut.
//   &mut T (MutRef) — exclusive borrow: one at a time, blocks moves and all other borrows.
//   Borrows are scoped lexically; they end when the scope containing the let binding ends.
//   Call-site borrows (&x in function args) are transient (released after the call).
//
// Phase 3 — Dangling reference detection:
//   A function returning &T / &mut T must not return a reference to a local variable.
//   Parameters are safe to borrow from; locals are not.
//
// Branch merging: moves (from Phase 1) are propagated conservatively (union).
//   Borrows are scope-local and released by pop_scope, so they don't survive merges.
//
// Loops: outer variables moved inside the body are dead after the loop.

#include <logos/compiler/borrow_check.hpp>
#include <logos/compiler/lir.hpp>
#include <logos/compiler/lir_mirror.hpp>
#include <logos/compiler/lir_view.hpp>
#include <logos/compiler/probe.hpp>
#include <logos/compiler/sema.hpp>
#include <logos/compiler/outlives.hpp>
#include <logos/compiler/const_promote.hpp>
#include <map>
#include <algorithm>
#include <cassert>
#include <logos/compiler/region_infer.hpp>
#include <logos/compiler/move_classify.hpp>

#include <format>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <variant>
#include <vector>

namespace logos::compiler {

using namespace lir;

// ── PER-CALL-SITE CENSUS OF `type_may_carry_borrow` (see PROBES.md §tmcbsite) ─
// Inert unless LOGOS_TMCB_FLIP names a file. Records the LINE of each caller
// whose answer the erased-payload widening would FLIP.
namespace {
struct TmcbFlip {
    const char* path = std::getenv("LOGOS_TMCB_FLIP");
    std::map<int, std::pair<unsigned long, unsigned long>> hits;  // site -> {arrivals, flips}
    ~TmcbFlip() {
        if (!path || hits.empty()) return;
        if (std::FILE* f = std::fopen(path, "a")) {
            for (auto& kv : hits)
                std::fprintf(f, "%d\t%lu\t%lu\n", kv.first, kv.second.first, kv.second.second);
            std::fclose(f);
        }
    }
};
inline TmcbFlip& tmcb_flip() { static TmcbFlip s; return s; }
inline bool tmcb_flip_armed() { return tmcb_flip().path != nullptr; }
inline void tmcb_flip_seen(int line) { ++tmcb_flip().hits[line].first; }
inline void tmcb_flip_note(int line) { ++tmcb_flip().hits[line].second; }
// LOGOS_PROBE_SITE=<line>[,<line>…] restricts the widening to those CALL SITES,
// so a refusal can be attributed to one consumer instead of to the predicate.
inline bool tmcb_site_allowed(int line) {
    static const char* sel = std::getenv("LOGOS_PROBE_SITE");
    if (!sel || !*sel) return true;
    char buf[16]; int n = std::snprintf(buf, sizeof buf, "%d", line);
    for (const char* p = sel; *p;) {
        if (std::strncmp(p, buf, (size_t)n) == 0 && (p[n] == 0 || p[n] == ','))
            return true;
        while (*p && *p != ',') ++p;
        if (*p) ++p;
    }
    return false;
}
}  // namespace

// ── Copy/Move classification ────────────────────────────────────────────────

struct TypeSets {
    std::unordered_set<std::string> drop_types;
    std::unordered_set<std::string> copy_types;
    // `#[borrow_carrying]` struct names — values of these types may hold a Ref into
    // an arena (WAny); escape-tracked like references.
    std::unordered_set<std::string> borrow_carrying;
    // Residency-holder packages (`Held<T>`, `HeldAny`): a struct with an Rc/Arc
    // field ref-counts the arena alive on its own, so the value is the LAUNDERED
    // escape form — never borrow-carrying, not even via its type-args
    // (`Held<WArray<WAny>>`). Mirror of the holds_residency_holder exemption,
    // consulted by the use-site type walk too.
    std::unordered_set<std::string> residency_exempt;
    // D1 round 2, EXEMPT — the LOAN channel's own closure: `borrow_carrying`
    // computed WITHOUT the residency skip. `residency_exempt` is applied at
    // REGISTRATION (a struct with an Rc/Arc field never enters
    // `borrow_carrying`, so neither do its containers), which is why the
    // use-site exemption check was never the whole story: `H { h: Rc<i64>, b: B
    // }` was invisible to the loan channel too. Escape gates keep asking
    // `borrow_carrying` / `residency_exempt`; only the loan channel asks this.
    std::unordered_set<std::string> loan_carrying;
    // D1 round 10 / SP0+SP1 — the structural closure "this type IS, or
    // transitively CONTAINS, a `&mut`". The summarizer's U2 gate asks it: a
    // store of a whole AGGREGATE that holds a `&mut` (`h.i = inn`, `let inn =
    // h.i`) is an alias-creating store exactly as a bare `&mut` store is, and
    // gating on the stored value being itself a `&mut` made those two shapes
    // invisible — the summary was then lost WHOLE. Narrower than
    // `loan_carrying` on purpose: this set answers "can a deposit be WRITTEN
    // through this value", which is what an alias edge is consumed for.
    std::unordered_set<std::string> holds_mut_ref;
    // #71 SPIKE — "this type IS, or transitively CONTAINS, ANY reference".
    // holds_mut_ref's fixpoint with ONE predicate changed (MutRef ->
    // is_ref_kind) plus tuple/array element seeding, which holds_mut_ref's set
    // builder lacks. Consumed only by type_may_carry_borrow.
    std::unordered_set<std::string> holds_any_ref;
    // Name → def indices (built once in build_type_sets). Replace the per-type
    // linear scans of prog.structs / struct_specializations / enums that made
    // needs_drop / struct_is_dropck_relevant / enum_is_move O(structs) each —
    // and, called per variable across every function, the whole borrow pass
    // O(n²) in program size. First-def-wins, matching the scans' short-circuit.
    std::unordered_map<std::string, lir_view::StructView>  struct_by_name;
    std::unordered_map<std::string, lir_view::StructView>  spec_by_name;
    std::unordered_map<std::string, lir_view::EnumView>     enum_by_name;
    // Names of `const` items — the ones WITHOUT static storage. Logos has no
    // const-eval (project_no_const_eval): a const is name + type + lowered
    // expression, and mlir_gen MATERIALISES that expression INTO THE FRAME at
    // each use, so `&K` borrows frame storage exactly as `&local` does and
    // dangles the moment it is returned (MEASURED: `const K: i64 = 5i64; fn f()
    // -> &i64 { return &K; }` returned the clobber constant, 71 and 9 in two
    // runs, where 5 is correct). A `static` is the ADMIT TWIN — one
    // llvm.mlir.global, stable address — and is deliberately NOT in this set.
    // The distinction is carried on the LIR const mirror as IS_STATIC
    // (lir_view::ConstView::is_static), which is why this set can be built here
    // at all: sema's module_consts_/module_statics_ are not visible to a pass
    // that works on LIR.
    std::unordered_set<std::string> frame_consts;
};

static TypeSets build_type_sets(const lir::LProgram& prog) {
    TypeSets ts;
    // `prog.consts` holds BOTH kinds (sema pushes CONST_DEF and STATIC_DEF into
    // the same vector); IS_STATIC is the one property that separates them.
    // ⚠ THE `is_static` SKIP IS NOT WHAT KEEPS `static` COMPILING TODAY, AND
    // SAYING SO IS THE POINT. Sema rewrites every static READ to
    // `Deref(VarRef("__static_addr:<sym>"))` (sema_expr.cpp, §6.2 S25) and
    // `&STATIC` collapses to that address through the `&*p ≡ p` peephole — so a
    // static's borrow never arrives at the AddrOf arm below at all. MEASURED
    // two ways: with this skip deleted, all 44 `static`-using pass fixtures stay
    // green and pass/bc_static_item_return_ref_admit still passes; and a
    // `LOGOS_DUMP_RETGATE` trace of the twin probes reads `prov{loc=1}` for the
    // const and `prov{loc=0}` for the static. The skip stays because the set
    // MEANS "materialised into the frame", which a static is not — but it is
    // belt-and-braces, and the thing that would catch a lowering change putting
    // statics back on the AddrOf path is the admit fixture, not this line.
    // Bare name only: over all 2368 `tests/logos/pass` fixtures, every AddrOf
    // that reached this set spelled the const BARE (`[consthit]` sweep); the
    // pkg-qualified spelling never appeared, so it is not inserted.
    for (auto& cv : prog.consts) {
        if (cv.is_static()) continue;   // real global storage — borrow is sound
        ts.frame_consts.insert(std::string(cv.name()));
    }
    auto register_drop_symbol = [&](std::string_view sym) {
        // After unification, method names are pkg-qualified
        // (`pkg.Buf__drop__f__sig`). Strip pkg prefix before extracting
        // the bare type name.
        // Pkg may contain inner dots; split at LAST dot for bare name.
        if (auto dot = sym.rfind('.'); dot != std::string_view::npos)
            sym = sym.substr(dot + 1);
        if (auto p = sym.find("__drop"); p != std::string_view::npos) {
            auto base = sym.substr(0, p);
            ts.drop_types.insert(std::string(base));
            // Mono-spec names (`Box$G1$i64__drop`) ALSO register the template
            // base ("Box"): fn-body TypeRefs spell the bare template name, so
            // without the strip NO generic droppable struct (Box/Vec/String-
            // generics) was move-classified — `let c = b;` of a Box while
            // `&*b` was live passed silently (adversarial #2 f13).
            if (auto g = base.find("$G"); g != std::string_view::npos)
                ts.drop_types.insert(std::string(base.substr(0, g)));
        }
    };
    auto scan_fns = [&](const std::vector<LFunctionPtr>& fns) {
        for (auto& fn : fns)
            register_drop_symbol(fn.name());
    };
    scan_fns(prog.functions);
    scan_fns(prog.specializations);
    for (auto& sd : prog.structs)
        sd.each_method([&](lir_view::FunctionView m) {
            register_drop_symbol(m.name());
        });
    for (auto& impl : prog.impls)
        if (impl.trait_name() == "Copy")
            ts.copy_types.insert(std::string(impl.target_type()));
    // strip_generic: ONLY for DIRECT (attribute) marks — the spec's flag is
    // a verbatim copy of the template's (mono_clone), so registering the
    // `$G`-stripped template base is exact. The MAIN borrow check runs
    // POST-mono, where stdlib template defs live only in module arenas (not
    // prog.structs) while fn-body TypeRefs still spell the bare template
    // name — without the strip, `#[borrow_carrying]` on a generic stdlib
    // struct (VecIterMut) was invisible to user code (f12: two live
    // iter_mut() accepted). TRANSITIVE closure marks must NOT strip: a
    // `FilterIter$G…$VecIterMut…` spec is BC because of its ARGS — bare
    // "FilterIter" over a Slice chain is not (false E0716 on
    // closure_bare_param without this split).
    auto reg_bc_name = [&](const std::string& name, bool strip_generic) {
        ts.borrow_carrying.insert(name);
        std::string_view n = name;          // also the bare (pkg-stripped) name
        if (auto dot = n.rfind('.'); dot != std::string_view::npos)
            ts.borrow_carrying.insert(std::string(n.substr(dot + 1)));
        if (!strip_generic) return;
        if (auto g = n.find("$G"); g != std::string_view::npos) {
            std::string_view base = n.substr(0, g);
            ts.borrow_carrying.insert(std::string(base));
            if (auto d2 = base.rfind('.'); d2 != std::string_view::npos)
                ts.borrow_carrying.insert(std::string(base.substr(d2 + 1)));
        }
    };
    auto reg_bc = [&](lir_view::StructView sd) {
        if (sd.borrow_carrying()) reg_bc_name(std::string(sd.name()), /*strip_generic=*/true);
    };
    for (auto& sd : prog.structs) reg_bc(sd);
    for (auto& sd : prog.struct_specializations) reg_bc(sd);
    // `#[borrow_carrying]` enums (WAny) — same escape tracking as the struct form.
    for (auto& ed : prog.enums)
        if (ed.borrow_carrying()) reg_bc_name(std::string(ed.name()), /*strip_generic=*/true);
    // Transitive closure (escape tracking must see the WHOLE aggregate): a struct
    // or enum with an INLINE field / variant payload of a (transitively) borrow-
    // carrying type is itself borrow-carrying — the borrow rides inside the value,
    // so returning the aggregate escapes it exactly as returning the bare WAny
    // would. (Borrow-carrying as a generic CONTAINER element — `Vec<WAny>`, behind
    // an owning pointer — is handled by the container-element rule below.)
    auto type_bc_name = [](TypeRef t) -> std::string {
        if (!t) return {};
        auto k = t.kind();
        if (k == LogosType::Kind::Enum) return std::string(t.enum_name());
        if (k == LogosType::Kind::Struct || k == LogosType::Kind::ZonedStruct)
            return std::string(t.struct_name());
        return {};
    };
    auto type_is_bc = [&](TypeRef t) -> bool {
        // A direct borrow-carrying field/payload, OR a generic type-argument that
        // is borrow-carrying (a container of WAny — `Vec<WAny>`, `Option<WAny>` —
        // carries the borrows of its elements even though the buffer sits behind a
        // raw pointer / the payload is a type-param).
        auto n = type_bc_name(t);
        if (!n.empty() && ts.borrow_carrying.count(n) > 0) return true;
        if (!t) return false;
        for (auto a : t.type_args()) {
            auto an = type_bc_name(a);
            if (!an.empty() && ts.borrow_carrying.count(an) > 0) return true;
        }
        return false;
    };
    // A residency-holder field (`Rc`/`Arc<...>`) marks the LAUNDERED escape package
    // (`HeldAny { holder: Rc<dyn Resident>, val: WAny }`): the holder ref-counts the
    // arena alive independent of any local, so the contained borrow is SAFE to
    // escape. Such a type must NOT be transitively borrow-carrying (else returning
    // the escape hatch — its whole purpose — would be wrongly rejected).
    auto holds_residency_holder = [&](lir_view::StructView sd) -> bool {
        for (auto& f : sd.fields()) {
            auto ft = f.type(prog.type_pool.impl());
            if (!ft) continue;
            auto k = ft.kind();
            if (k != LogosType::Kind::Struct && k != LogosType::Kind::ZonedStruct) continue;
            std::string n(ft.struct_name());
            if (auto d = n.rfind('.'); d != std::string::npos) n = n.substr(d + 1);
            if (n == "Rc" || n == "Arc") return true;
        }
        return false;
    };
    auto reg_exempt_name = [&](const std::string& name) {
        ts.residency_exempt.insert(name);
        std::string_view n = name;          // also the bare (pkg-stripped) name
        if (auto dot = n.rfind('.'); dot != std::string_view::npos)
            n = n.substr(dot + 1);
        ts.residency_exempt.insert(std::string(n));
        // Specs are named `Held$G1$…`; the use-site type walk sees the BASE
        // struct name (`Held`) with type-args — register that form too.
        if (auto g = n.find("$G"); g != std::string_view::npos)
            ts.residency_exempt.insert(std::string(n.substr(0, g)));
    };
    // An EXPLICIT `#[borrow_carrying]` annotation is the author declaring the
    // type enforces a borrow — it WINS over the auto residency-holder heuristic.
    // (A `#[borrow_carrying]` iterator that also holds an Arc share of the nodes
    // it walks — PMapIter — is still a borrow-carrying iterator: the explicit
    // iterator-invalidation guard must not be silently dropped just because the
    // handle happens to be ref-counted. No laundered-escape type is also
    // explicitly borrow-carrying — they are opposites.)
    for (auto& sd : prog.structs)
        if (!sd.borrow_carrying() && holds_residency_holder(sd)) reg_exempt_name(std::string(sd.name()));
    for (auto& sd : prog.struct_specializations)
        if (!sd.borrow_carrying() && holds_residency_holder(sd)) reg_exempt_name(std::string(sd.name()));
    bool bc_changed = true;
    while (bc_changed) {
        bc_changed = false;
        auto consider_struct = [&](lir_view::StructView sd) {
            if (ts.borrow_carrying.count(std::string(sd.name()))) return;
            if (holds_residency_holder(sd)) return;   // laundered escape package — exempt
            for (auto& f : sd.fields())
                if (type_is_bc(f.type(prog.type_pool.impl()))) { reg_bc_name(std::string(sd.name()), /*strip_generic=*/false); bc_changed = true; return; }
        };
        for (auto& sd : prog.structs)                consider_struct(sd);
        for (auto& sd : prog.struct_specializations) consider_struct(sd);
        for (auto& ed : prog.enums) {
            std::string ed_name(ed.name());
            if (ts.borrow_carrying.count(ed_name)) continue;
            bool hit = false;
            ed.each_variant([&](lir_view::EnumVariantView var) {
                if (hit) return;
                var.each_payload_type(prog.type_pool.impl(), [&](TypeRef pt) {
                    if (type_is_bc(pt)) hit = true;
                });
            });
            if (hit) { reg_bc_name(ed_name, /*strip_generic=*/false); bc_changed = true; }
        }
    }
    // ── The loan-channel closure (see TypeSets::loan_carrying) ────────────
    // Same fixpoint, seeded from `borrow_carrying`, with holds_residency_holder
    // NOT skipped: an Rc/Arc share keeps the arena ALIVE, which is an escape
    // fact; it does not stop the contained borrow from reading that arena.
    ts.loan_carrying = ts.borrow_carrying;
    {
        auto reg_lc_name = [&](const std::string& name) {
            ts.loan_carrying.insert(name);
            std::string_view n = name;
            if (auto dot = n.rfind('.'); dot != std::string_view::npos)
                ts.loan_carrying.insert(std::string(n.substr(dot + 1)));
        };
        auto type_is_lc = [&](TypeRef t) -> bool {
            auto n = type_bc_name(t);
            if (!n.empty() && ts.loan_carrying.count(n) > 0) return true;
            if (!t) return false;
            for (auto a : t.type_args()) {
                auto an = type_bc_name(a);
                if (!an.empty() && ts.loan_carrying.count(an) > 0) return true;
            }
            return false;
        };
        bool lc_changed = true;
        while (lc_changed) {
            lc_changed = false;
            auto consider = [&](lir_view::StructView sd) {
                if (ts.loan_carrying.count(std::string(sd.name()))) return;
                for (auto& f : sd.fields())
                    if (type_is_lc(f.type(prog.type_pool.impl()))) {
                        reg_lc_name(std::string(sd.name())); lc_changed = true; return;
                    }
            };
            for (auto& sd : prog.structs)                 consider(sd);
            for (auto& sd : prog.struct_specializations)  consider(sd);
            for (auto& ed : prog.enums) {
                std::string ed_name(ed.name());
                if (ts.loan_carrying.count(ed_name)) continue;
                bool hit = false;
                ed.each_variant([&](lir_view::EnumVariantView var) {
                    if (hit) return;
                    var.each_payload_type(prog.type_pool.impl(),
                                          [&](TypeRef pt) { if (type_is_lc(pt)) hit = true; });
                });
                if (hit) { reg_lc_name(ed_name); lc_changed = true; }
            }
        }
    }
    // ── holds_mut_ref: same fixpoint shape, seeded by a literal `&mut` field ─
    {
        auto has_mut = [&](TypeRef t) -> bool {
            return t && t.kind() == LogosType::Kind::MutRef;
        };
        auto type_is_hm = [&](TypeRef t) -> bool {
            if (has_mut(t)) return true;
            auto n = type_bc_name(t);
            if (!n.empty() && ts.holds_mut_ref.count(n) > 0) return true;
            if (!t) return false;
            for (auto a : t.type_args()) {
                if (has_mut(a)) return true;
                auto an = type_bc_name(a);
                if (!an.empty() && ts.holds_mut_ref.count(an) > 0) return true;
            }
            return false;
        };
        auto reg_hm_name = [&](const std::string& name) {
            ts.holds_mut_ref.insert(name);
            std::string_view n = name;
            if (auto dot = n.rfind('.'); dot != std::string_view::npos)
                ts.holds_mut_ref.insert(std::string(n.substr(dot + 1)));
        };
        bool hm_changed = true;
        while (hm_changed) {
            hm_changed = false;
            auto consider = [&](lir_view::StructView sd) {
                if (ts.holds_mut_ref.count(std::string(sd.name()))) return;
                for (auto& f : sd.fields())
                    if (type_is_hm(f.type(prog.type_pool.impl()))) {
                        reg_hm_name(std::string(sd.name())); hm_changed = true; return;
                    }
            };
            for (auto& sd : prog.structs)                 consider(sd);
            for (auto& sd : prog.struct_specializations)  consider(sd);
            for (auto& ed : prog.enums) {
                std::string ed_name(ed.name());
                if (ts.holds_mut_ref.count(ed_name)) continue;
                bool hit = false;
                ed.each_variant([&](lir_view::EnumVariantView var) {
                    if (hit) return;
                    var.each_payload_type(prog.type_pool.impl(),
                                          [&](TypeRef pt) { if (type_is_hm(pt)) hit = true; });
                });
                if (hit) { reg_hm_name(ed_name); hm_changed = true; }
            }
        }
    }
    // ── #71 SPIKE — holds_any_ref: holds_mut_ref's fixpoint, is_ref_kind ────
    {
        auto has_any = [&](TypeRef t) -> bool {
            return t && (t.kind() == LogosType::Kind::Ref ||
                         t.kind() == LogosType::Kind::MutRef ||
                         t.kind() == LogosType::Kind::Slice ||
                         (t.kind() == LogosType::Kind::DstRef && !t.owning_dst()) ||
                         (t.kind() == LogosType::Kind::TraitObject &&
                          !t.owning_trait_object()));
        };
        std::function<bool(TypeRef)> type_is_ha = [&](TypeRef t) -> bool {
            if (has_any(t)) return true;
            auto n = type_bc_name(t);
            if (!n.empty() && ts.holds_any_ref.count(n) > 0) return true;
            if (!t) return false;
            for (auto a : t.type_args()) {
                if (has_any(a)) return true;
                auto an = type_bc_name(a);
                if (!an.empty() && ts.holds_any_ref.count(an) > 0) return true;
            }
            // holds_mut_ref's set builder inspects ONLY type_args, so
            // `struct H { t: (&i64, i64) }` reopens the hole one level down.
            if (t.kind() == LogosType::Kind::Tuple)
                for (auto e : t.tuple_elems())
                    if (type_is_ha(TypeRef(e))) return true;
            if (t.kind() == LogosType::Kind::Array)
                return type_is_ha(t.elem());
            return false;
        };
        auto reg_ha_name = [&](const std::string& name) {
            ts.holds_any_ref.insert(name);
            std::string_view n = name;
            if (auto dot = n.rfind('.'); dot != std::string_view::npos)
                ts.holds_any_ref.insert(std::string(n.substr(dot + 1)));
        };
        bool ha_changed = true;
        while (ha_changed) {
            ha_changed = false;
            auto consider = [&](lir_view::StructView sd) {
                if (ts.holds_any_ref.count(std::string(sd.name()))) return;
                for (auto& f : sd.fields())
                    if (type_is_ha(f.type(prog.type_pool.impl()))) {
                        reg_ha_name(std::string(sd.name())); ha_changed = true; return;
                    }
            };
            for (auto& sd : prog.structs)                 consider(sd);
            for (auto& sd : prog.struct_specializations)  consider(sd);
            for (auto& ed : prog.enums) {
                std::string ed_name(ed.name());
                if (ts.holds_any_ref.count(ed_name)) continue;
                bool hit = false;
                ed.each_variant([&](lir_view::EnumVariantView var) {
                    if (hit) return;
                    var.each_payload_type(prog.type_pool.impl(),
                                          [&](TypeRef pt) { if (type_is_ha(pt)) hit = true; });
                });
                if (hit) { reg_ha_name(ed_name); ha_changed = true; }
            }
        }
        if (std::getenv("LOGOS_DUMP_HOLDS_ANY_REF"))
            fprintf(stderr, "[holds_any_ref] %zu names (holds_mut_ref: %zu)\n",
                    ts.holds_any_ref.size(), ts.holds_mut_ref.size());
    }
    // Name → def indices for O(1) by-name lookup (first-def-wins).
    for (auto& sd : prog.structs)               ts.struct_by_name.emplace(std::string(sd.name()), sd);
    for (auto& sd : prog.struct_specializations) ts.spec_by_name.emplace(std::string(sd.name()), sd);
    for (auto& ed : prog.enums)                 ts.enum_by_name.emplace(std::string(ed.name()), ed);
    return ts;
}

static bool has_droppable_fields(TypeRef, const lir::LProgram&, const TypeSets&);

static bool needs_drop(TypeRef t, const lir::LProgram& prog, const TypeSets& ts) {
    if (!t || t.kind() != LogosType::Kind::Struct) return false;
    return ts.drop_types.count(std::string(t.struct_name())) || has_droppable_fields(t, prog, ts);
}

static bool has_droppable_fields(TypeRef t, const lir::LProgram& prog,
                                  const TypeSets& ts) {
    if (!t || t.kind() != LogosType::Kind::Struct) return false;
    // Match the struct DEF by its concrete (mono-mangled) name. A generic
    // instantiation `Wrap<i64>` has struct_name()=="Wrap" (base, never mangled)
    // but its def is stored as "Wrap$G1$i64" in prog.structs — matching on the
    // bare base finds nothing, so the droppable `Vec<i64>` field was invisible
    // and the value was mis-classified as non-move (move-while-field-borrowed
    // slipped through for generic containers). concrete_struct_name == base for
    // non-generic structs, so this also covers the plain case.
    std::string want = concrete_struct_name(t);
    auto def_has_drop = [&](lir_view::StructView sd) -> bool {
        if (!sd) return false;
        for (auto& f : sd.fields())
            if (needs_drop(f.type(prog.type_pool.impl()), prog, ts)) return true;
        return false;
    };
    auto sit = ts.struct_by_name.find(want);
    if (sit != ts.struct_by_name.end() && def_has_drop(sit->second)) return true;
    auto pit = ts.spec_by_name.find(want);
    if (pit != ts.spec_by_name.end() && def_has_drop(pit->second)) return true;
    return false;
}

// A value is a "move type" for borrow-check purposes (consuming it invalidates
// the source, and it can't be moved while borrowed) when it owns droppable
// resources and isn't Copy. P2-12: this was Struct-only, so a tuple / array /
// enum carrying a move element (e.g. `(String, i64)`) was treated as Copy →
// move-while-borrowed and consume tracking silently skipped (a dangling-ref
// soundness gap). Now recurses structurally to match the value's real ownership.
static bool is_move_type(TypeRef t, const lir::LProgram& prog, const TypeSets& ts,
                         const std::unordered_set<std::string>* copy_tvs = nullptr) {
    // Shared aggregate-recursion skeleton (moveclass::is_move_type); the
    // callbacks reproduce borrow_check's exact (post-mono) semantics. Tuple /
    // Array recursion is single-sourced in the skeleton.
    auto leaf = [&](TypeRef x) -> std::optional<bool> {
        // Rust: `&mut T` is NOT Copy — passing/binding it MOVES the unique
        // mut-reference. `&T` is Copy. Reborrow-shaped uses (`&mut *r`) are
        // wrapped at sema (try_implicit_reborrow_mut) and surface here as
        // AddrOfTemp(Deref(r)), which the AddrOfTemp handler routes to a
        // borrow on r — they don't pass through the move path.
        if (x && x.kind() == LogosType::Kind::MutRef) return true;
        // A bare type-parameter `T` is MOVE unless it carries an explicit
        // `Copy` bound (Rust checks generic BODIES abstractly: `T` moves unless
        // `T: Copy`). Mirrors sema's bound-aware is_move_type (DIVERGENCES §B1)
        // — without this the borrow checker's PARTIAL-move tracker never marked
        // `s.a: T` moved, so partial moves in generic templates (Tier 1) went
        // undiagnosed. copy_tvs = the fn's Copy-bounded type-param names; null
        // (post-mono / no context) leaves TypeVars conservative-move (sound:
        // Copy ⊥ Drop). Concrete code post-mono has no TypeVars, so Tier 2 is
        // unaffected.
        if (x && x.kind() == LogosType::Kind::TypeVar) {
            if (copy_tvs && copy_tvs->count(std::string(x.type_var_name())))
                return std::optional<bool>(false);
            return std::optional<bool>(true);
        }
        return std::nullopt;
    };
    auto struct_is_move = [&](TypeRef x) {
        return needs_drop(x, prog, ts) &&
               !ts.copy_types.count(std::string(TypeRef(x).struct_name()));
    };
    auto enum_is_move = [&](TypeRef x) -> bool {
        std::string en(TypeRef(x).enum_name());
        if (ts.copy_types.count(en)) return false;     // explicitly Copy enum
        if (ts.drop_types.count(en)) return true;       // has a Drop impl
        auto eit = ts.enum_by_name.find(en);            // any move-typed payload
        if (eit != ts.enum_by_name.end()) {
            bool moved = false;
            eit->second.each_variant([&](lir_view::EnumVariantView v) {
                v.each_payload_type(prog.type_pool.impl(), [&](TypeRef pt) {
                    if (is_move_type(pt, prog, ts, copy_tvs)) moved = true;
                });
            });
            return moved;
        }
        return false;
    };
    return moveclass::is_move_type(t, leaf, struct_is_move, enum_is_move);
}

// ── Variable state ───────────────────────────────────────────────────────────

struct VarState {
    // Phase 1 — ownership
    bool     moved          = false;
    uint32_t moved_line     = 0;
    // Phase 2 — borrow tracking
    int      shared_borrows = 0;     // # active &T borrows on this var
    bool     mut_borrowed   = false; // has an active (activated) &mut borrow
    // B82: two-phase borrows — a `&mut x` taken as a fn-call argument is
    // *reserved* during the rest of the arg evaluation, then *activated*
    // at call entry. Reservations behave like the absence of an exclusive
    // borrow w.r.t. concurrent shared reads, but block other mut borrows.
    int      mut_reservations = 0;
    // Phase 3 — binding mutability (`let mut x` vs `let x`).
    // Required for rejecting `&mut x` and `x = ...` against immutable bindings.
    bool     is_mut_binding = false;
    // Partial moves: name → line where field was moved out. Reading the
    // same field again, or the whole value, after a field move is rejected.
    std::unordered_map<std::string, uint32_t> moved_fields;
    // B83: field-path borrow tracking. Keys are dotted paths into the
    // value (e.g. "a", "i.x"). Disjoint field paths can be borrowed
    // simultaneously even mutably. Two borrows conflict iff one path is
    // a prefix of the other (including equal). The whole-value borrows
    // (mut_borrowed, shared_borrows) act as borrows on path "" — they
    // conflict with every field path.
    std::unordered_map<std::string, int>  shared_field_borrows;  // path → count
    std::unordered_set<std::string>       mut_field_borrows;     // path
};

// Phase-1 string-interning flip: the per-function variable-state store. A var
// that sema assigned a dense SLOT lives in slot_[slot] (declared_[slot]=1), and
// name2slot_ maps its name → slot so a residual by-NAME access still finds it.
// Vars with no slot (synthesised refs, captures with no node) live in name_.
// Copyable, so branch save/restore is a plain struct copy and merge iterates
// declared slots + name_. Replaces the old unordered_map<string,VarState>,
// killing the per-access string hash + node alloc for slotted vars (the bulk).
struct VarStore {
    static constexpr uint32_t NO_SLOT = 0xFFFFFFFFu;
    std::vector<VarState>                      slot_;       // by slot index
    std::vector<char>                          declared_;   // slot live?
    std::unordered_map<std::string, VarState>  name_;       // unslotted vars
    std::unordered_map<std::string, uint32_t>  name2slot_;  // name → slot (slotted)

    void reset(uint32_t n) {
        slot_.assign(n, VarState{}); declared_.assign(n, 0);
        name_.clear(); name2slot_.clear();
    }
    void clear() { reset(static_cast<uint32_t>(slot_.size())); }

    uint32_t resolve(uint32_t slot, std::string_view name) const {
        if (slot != NO_SLOT) return slot;
        auto it = name2slot_.find(std::string(name));
        return it == name2slot_.end() ? NO_SLOT : it->second;
    }
    VarState* find(uint32_t slot, std::string_view name) {
        uint32_t s = resolve(slot, name);
        if (s != NO_SLOT && s < declared_.size() && declared_[s]) return &slot_[s];
        auto it = name_.find(std::string(name));
        return it == name_.end() ? nullptr : &it->second;
    }
    const VarState* find(uint32_t slot, std::string_view name) const {
        uint32_t s = resolve(slot, name);
        if (s != NO_SLOT && s < declared_.size() && declared_[s]) return &slot_[s];
        auto it = name_.find(std::string(name));
        return it == name_.end() ? nullptr : &it->second;
    }
    bool has(uint32_t slot, std::string_view name) const { return find(slot, name) != nullptr; }
    // operator[] equivalent: access-or-declare. A real slot (declare site) or a
    // resolvable name routes to slot_; otherwise a fresh name_ entry.
    VarState& at(uint32_t slot, std::string_view name) {
        uint32_t s = (slot != NO_SLOT) ? slot : resolve(slot, name);
        if (s != NO_SLOT && s < slot_.size()) {
            declared_[s] = 1;
            if (!name.empty()) name2slot_[std::string(name)] = s;
            return slot_[s];
        }
        return name_[std::string(name)];
    }
    void erase(uint32_t slot, std::string_view name) {
        uint32_t s = resolve(slot, name);
        if (s != NO_SLOT && s < declared_.size()) declared_[s] = 0;
        name2slot_.erase(std::string(name));
        name_.erase(std::string(name));
    }
    // Branch-merge support: iterate every live var as (slot, name, state) — slot
    // vars yield an empty name, name_ vars yield NO_SLOT. has_id/at_id match a
    // var across two stores by slot (or name for unslotted).
    template <class F> void for_each(F&& f) {
        for (uint32_t s = 0; s < slot_.size(); ++s) if (declared_[s]) f(s, std::string_view{}, slot_[s]);
        for (auto& [n, st] : name_) f(NO_SLOT, std::string_view(n), st);
    }
    bool has_id(uint32_t slot, std::string_view name) const {
        if (slot != NO_SLOT) return slot < declared_.size() && declared_[slot];
        return name_.count(std::string(name)) != 0;
    }
    VarState& at_id(uint32_t slot, std::string_view name) {
        if (slot != NO_SLOT) { declared_[slot] = 1; return slot_[slot]; }
        return name_[std::string(name)];
    }
};
using StateMap = VarStore;

// Phase 4 — lifetime provenance.
// For each reference-typed variable, tracks which function parameters it
// ultimately dereferences into (params are guaranteed to outlive the call).
// is_local = true  → at least one path originates from a local variable
//                    (returning such a ref is a dangling reference).
// params.empty() && !is_local → provenance unknown / comes from a global or
//                                a function return value; assume safe to return.
struct RefProv {
    std::unordered_set<std::string> params;    // param names this ref may alias
    bool                            is_local = false;
    // is_temp = the ref borrows into a *statement-scoped temporary* (a fresh
    // value with no named storage — a call/method-call result, struct/tuple/
    // array/enum literal, …) which drops at the end of the enclosing statement.
    // Distinct from is_local (a named local that lives for the whole scope): a
    // ref to a temporary is dangling the moment it ESCAPES its statement (e.g.
    // `let v = make().view();` — Rust E0716), whereas a ref to a local is only
    // dangling when RETURNED past the scope.
    bool                            is_temp = false;
};

using ProvMap = std::unordered_map<std::string, RefProv>;

static RefProv merge_prov(const RefProv& a, const RefProv& b) {
    RefProv r;
    for (auto& s : a.params) r.params.insert(s);
    for (auto& s : b.params) r.params.insert(s);
    r.is_local = a.is_local || b.is_local;
    r.is_temp  = a.is_temp  || b.is_temp;
    return r;
}

// A statement-scoped temporary value: a fresh value with no named storage. A
// reference borrowing into one dangles once it leaves the statement. (Mirrors
// the kind-set the AddrOfTemp case below treats as a true temporary.)
// Names of compiler-materialized, statement-scoped temporaries. materialize_recv_ref
// (sema_expr.cpp) hoists a fresh droppable rvalue receiver into a `__rtmp_N` local
// that drops at the END of the enclosing statement. A reference borrowing into one
// is statement-scoped (dangling if it escapes the statement) — exactly Rust's
// temporary-lifetime for an auto-ref'd rvalue receiver.
static bool is_materialized_temp_name(std::string_view n) {
    return n.rfind("__rtmp_", 0) == 0;
}

// The RETURN temp sema mints in `make_return_with_drops` (sema_stmt.cpp:
// `"__ret_tmp_" + tmp_var_count_++`) whenever a returned value has to be bound
// before the frame's drops run. It is a compiler-internal PLACE, never a name
// the programmer can read in a diagnostic — see check_return_value.
static bool is_return_temp_name(std::string_view n) {
    return n.rfind("__ret_tmp_", 0) == 0;
}

// The DESTRUCTURE temp sema mints in every `let <pattern> = rhs;` lowering
// (sema_stmt.cpp: `"__dst_" + destruct_counter_++`, five sites): the scrutinee
// is materialised WHOLE into this local and the pattern's bindings are read off
// it as field/tuple projections. See deref_move_exempt for why that lowering
// makes the E0507 question unanswerable at this position.
static bool is_destructure_temp_name(std::string_view n) {
    return n.rfind("__dst_", 0) == 0;
}

// D1 round 12 / A1. A loop's break slot, minted by sema as
// `"__loop_val_" + tmp_var_count_++` (sema_stmt.cpp, lower_loop) whenever a
// `break v` gives the loop a value. Both walkers key the deposit on this name;
// the prefix is the ONE place that knowledge lives.
static bool is_loop_break_slot_name(std::string_view n) {
    return n.rfind("__loop_val_", 0) == 0;
}

// #121 — CONDITIONAL-MOVE FIELD DROP GLUE. `emit_cond_move_field_drops`
// (sema.cpp) destroys a conditionally-moved field path by moving it into a
// `__cmfd_N` temp inside `if <flag> { … }` and dropping that. The read is by
// construction a read of a place the static analysis has already recorded as
// moved — that is the whole point: the FLAG says it was not moved on THIS
// path. Walking it would report "use of moved field" on compiler-generated
// drop glue for a program that is correct.
//
// ⚠ THE NAME IS NOT THE EVIDENCE, AND KEYING ON IT ALONE WAS A USER-REACHABLE
// HOLE. `__cmfd_` is a spelling any user can write, and the exemption skips
// BOTH `Code::Let` walkers entirely — so `let __cmfd_0: &i64 = &t; return
// __cmfd_0;` compiled, rc 0, returning a dangling reference, while the same
// program with the binding named `zz_0` was correctly refused. MEASURED on the
// landing round's own tree before this predicate existed. The prose above
// asserted "the RHS is always a pure field-read place (no call, no borrow, no
// index), so skipping it hides no check" — true of the glue, and never checked,
// which is exactly the "exemption not checked in the ABUSE direction" class.
//
// So the exemption is now granted on the STRUCTURE the claim is about: the
// value must be a chain of field / tuple-index reads bottoming out in a plain
// variable — what `emit_cond_move_field_drops` emits and nothing else. A
// borrow, a call, a deref, an index, or a bare variable copy is not glue and
// is walked like any other binding, whatever it is called.
static bool is_cond_move_field_drop_place(lir_view::ExprRef e) {
    using EK = lir_schema::expr::Code;
    // At least one projection: a bare `VarRef` is a whole-value move, which the
    // glue never emits and which the walkers must still see.
    bool projected = false;
    while (e) {
        switch (e.kind()) {
            case EK::FieldRead:
                projected = true;
                e = lir_view::EFieldReadView{e}.receiver();
                continue;
            case EK::TupleIndex:
                projected = true;
                e = lir_view::ETupleIndexView{e}.receiver();
                continue;
            case EK::VarRef:
                return projected;
            default:
                return false;
        }
    }
    return false;
}

// ⚠ AND THE STRUCTURE WAS NOT ENOUGH EITHER. The structural test above closed
// the BORROW half — `let __cmfd_0: &i64 = &t; return __cmfd_0;` is refused
// again — but a field-read chain bottoming out in a VarRef is EXACTLY what a
// genuine partial move looks like, so the MOVE half stayed open: MEASURED,
// `let __cmfd_9: Pay = h.p; return eatH(h);` compiled at rc 0 while the same
// program with the binding named `zz_9` was refused with "use of partially
// moved value 'h' (field 'p' moved on line 8)". Two abuse directions, two
// rounds, one root: the exemption was being granted on evidence the ATTACKER
// WRITES. Provenance is not derivable from the text of the binding, so it is
// now carried by the producer — `emit_cond_move_field_drops` stamps
// `stmt_keys::COMPILER_GLUE` on the `let` it synthesises, mono carries it
// through cloning, and this predicate demands it. The name and the shape are
// kept as corroborating conjuncts: they cost nothing and they keep the
// exemption pinned to the ONE emitter that is allowed to use it, so a future
// producer that stamps the bit on something else does not silently inherit it.
static bool is_cond_move_field_drop_temp(lir_view::StmtRef st,
                                         std::string_view n, lir_view::ExprRef v) {
    return lir_view::SLetView{st}.compiler_glue() &&
           n.rfind("__cmfd_", 0) == 0 && is_cond_move_field_drop_place(v);
}

static bool is_temporary_value_expr(lir_view::ExprRef e) {
    if (!e) return false;
    using EK = lir_schema::expr::Code;
    switch (e.kind()) {
        case EK::LitInt: case EK::LitFloat: case EK::LitBool: case EK::LitStr:
        case EK::StructLit: case EK::TupleLit: case EK::ArrLit:
        case EK::Call: case EK::MethodCall: case EK::ClosureCall:
        case EK::EnumLit: case EK::EnumLitData:
        // An OPERATOR produces a fresh value with no place of its own, exactly
        // as a literal or a call does — the omission was by spelling, not by
        // property. `return &(2i64 + 3i64);` fell past this list to
        // `value_local_root`, found no local root, and reached the
        // "unknown — conservative-accept" tail: it compiled and returned a
        // pointer into the dead frame, with the exit code tracking a clobber
        // constant (9->9, 40->40, 71->71 where 5 is correct).
        // ⚠ NOT promoted instead of refused, and that is deliberate: Logos has
        // NO const-eval by design — a `const` is stored as an expression and
        // read by name, and compile-time arithmetic goes through `metacall`
        // (see project_no_const_eval). Folding `2+3` inside this predicate
        // would be the mini-evaluator that stance exists to forbid. A caller
        // who wants a promoted constant writes `metacall { 2 + 3 }`, which
        // splices a LITERAL and is promoted by the arm above.
        case EK::BinOp: case EK::Unary: case EK::Cast:
            return true;
        default:
            return false;
    }
}

static void merge_provs(ProvMap& base, const ProvMap& other) {
    for (auto& [name, p] : other)
        base[name] = merge_prov(base[name], p);
}

static bool is_ref_kind(TypeRef t) {
    // logos-core 2.1 (default trait-object lifetime rule): `&dyn Trait`
    // resolves to Kind::TraitObject (a fat pair {data, vtable}, not
    // Ref<TraitObject>). The DATA half is a borrowed pointer, so it
    // has the same dangling-on-return risk as `&T`. Treat it as a
    // ref-kind here so check_return_value catches `fn bad() -> &dyn T
    // { return &local; }` — the elided lifetime defaults to the
    // outer-scope (caller) but the local doesn't outlive the fn.
    // Borrowing-form only — owning Box<dyn T> doesn't qualify.
    // Fat borrowed forms carry the same provenance duties as `&T`:
    // `&[T]`/`&mut [T]` (Kind::Slice — Logos has no owning slice value; the
    // owning forms are Box<[T]>/DstRef-owning) and a borrowed custom-DST
    // reference (non-owning DstRef). Without these, a method returning a
    // slice VIEW of its receiver (`fn offs(&self) -> &[u32]`) did not extend
    // the receiver's borrow — resize-after-view compiled and dangled.
    return t && (t.kind() == LogosType::Kind::Ref ||
                 t.kind() == LogosType::Kind::MutRef ||
                 t.kind() == LogosType::Kind::Slice ||
                 (t.kind() == LogosType::Kind::DstRef && !t.owning_dst()) ||
                 (t.kind() == LogosType::Kind::TraitObject &&
                  !t.owning_trait_object()));
}

// The `&`/`&mut`/borrowed-dyn subset of is_ref_kind — WITHOUT the fat value
// forms (str/&[T]/borrowed DST). The ESCAPE/provenance ARG-capture heuristics
// use this: a by-value slice argument is a COPY of a borrow whose lifetime is
// its ELEMENT's, so a borrow-returning call must not have its result tied to
// the argument's root (`tv_build(h, name.as_str(), …)` interns into `h`; tying
// the WAny to local `name` falsely rejected the return).
static bool is_plain_ref_kind(TypeRef t) {
    return t && (t.kind() == LogosType::Kind::Ref ||
                 t.kind() == LogosType::Kind::MutRef ||
                 (t.kind() == LogosType::Kind::TraitObject &&
                  !t.owning_trait_object()));
}

static bool is_mut_ref(TypeRef t) {
    return t && t.kind() == LogosType::Kind::MutRef;
}

struct BorrowRecord {
    std::string target;   // the var being borrowed FROM
    bool        is_mut;
    // Phase 9 (NLL): the var being borrowed INTO (the let/assign LHS that
    // holds the resulting reference). Empty when the borrow is transient
    // (call-site `&x` in an arg, not bound). After each stmt, if
    // `holder`'s last_use_line < current stmt line, the borrow is
    // released — making the borrow non-lexical.
    std::string holder;
    uint32_t    target_slot = 0xFFFFFFFFu;  // Phase-1: dense slot of `target`
    // D1: additional holders the loan was INHERITED by (see inherit_loans).
    // A borrow-carrying value hops by value — composed into an enum/struct
    // literal, extracted out with `unwrap()`/a field read, passed through a
    // by-value fn — and every hop makes a NEW binding able to reach the
    // borrow. The loan must then die at the LAST of all its holders' last
    // uses, not the first: co_holders is that set, and release_dead_borrows
    // maxes over it. (Modelled as one record with many holders rather than
    // duplicated records, so the shared_borrows counter stays balanced and a
    // `mut` loan cannot be released twice.)
    std::vector<std::string> co_holders;
    // D1 round 3 / F5 — A LOOKUP KEY IS NOT AN IDENTITY (third instance in
    // this codebase; see also the registry-key and canon-name arcs). `holder`
    // is a NAME, and a name is re-used by shadowing: `let b: B = c.mk(); let
    // z = *b.p; let b: i64 = 5i64; c.bump(); return z + b;` used to refuse,
    // because `note_use("b", <last line>)` from the SHADOWING i64 binding
    // revived the dead B-holder's loan. The BINDING IDENTITY is the dense
    // per-function slot sema already assigns (StateMap slots); `holder` stays
    // a name for every structural comparison (inherit_loans, pop_scope
    // re-homing, is_loan_holder), and only the LIFETIME lookup keys on the
    // slot. NO_SLOT means "no identity available" → the old, conservative
    // name-keyed lookup.
    uint32_t              holder_slot = 0xFFFFFFFFu;
    std::vector<uint32_t> co_holder_slots;   // parallel to co_holders
};

// B83: a tracked field borrow recorded in the current scope. On pop,
// the borrow is released from the target var's field map.
struct FieldBorrow {
    std::string target;     // root var
    std::string path;       // dotted field path ("a.b.c"); empty for whole-value
    bool        is_mut;
    // Phase 9 (NLL) — same contract as BorrowRecord::holder: released once
    // the holder's last use has passed (empty = lexical, released at pop).
    std::string holder;
    uint32_t    target_slot = 0xFFFFFFFFu;  // Phase-1: dense slot of `target`
    std::vector<std::string> co_holders;    // D1 — see BorrowRecord::co_holders
    uint32_t              holder_slot = 0xFFFFFFFFu;  // F5 — see BorrowRecord
    std::vector<uint32_t> co_holder_slots;            // parallel to co_holders
};

// ── §B6 SOURCE IDENTITY — F5, FOURTH INSTANCE ("a lookup KEY is not an
//    IDENTITY"; see BorrowRecord::holder_slot for the third) ──────────────
// A §B6 source was a bare NAME, and a name is re-used by shadowing. The D-b
// tail check and pop_scope's dangling deposit both ask "is this source one of
// the bindings dying here?" by comparing STRINGS, so an inner
//     let p: &i64 = &o.n;  let o: i64 = 1i64;      // outer `o` is a Buf
// made `p` dangle on the INNER `o` — a false E0597, one-property pair against
// the same block with the inner local named `zz` (ADMITTED). The identity is
// the dense per-function slot sema already assigns; it is captured HERE, at
// the point the source is collected, because that is the only point at which
// the name still denotes the binding the borrow was formed from. NO_SLOT
// means "no identity available" → the old, conservative name-keyed match.
struct RefSrc {
    std::string name;
    uint32_t    slot = 0xFFFFFFFFu;
    bool operator==(const RefSrc& o) const {
        return name == o.name && slot == o.slot;
    }
};

struct ScopeFrame {
    std::vector<std::string>  declared;  // vars declared in this scope
    std::vector<uint32_t>     declared_slots;  // F5 — parallel to `declared`
    std::vector<BorrowRecord> borrows;   // borrows held in this scope
    std::vector<FieldBorrow>  field_borrows;  // B83: tracked field-path borrows
    // D3: this frame was pushed by a BARE `{ … }` STATEMENT — not a loop body,
    // not an `if`/`match` arm, not a call-site scope. It is the only frame kind
    // the NLL sweep is allowed to look PAST (see release_dead_borrows).
    bool bare_block = false;
};

// Single foundation for "what is being borrowed when we see an `AddrOfTemp`?".
// Walks `inner` outer→inner accumulating field-path components. Fields ABOVE
// the first IndexRead/SliceIndex describe layout WITHIN the indexed element
// (not the path TO the indexed container), so they're discarded when an
// index is crossed; fields BELOW the index are the path to the container.
// Whole-element granularity (Rust-conservative: we don't prove disjointness
// on the index value).
//
// Examples (with the outermost expression first):
//   `&p.f.g`        → root=p,   path="f.g",  index_in_chain=false
//   `&p.arr[i]`     → root=p,   path="arr",  index_in_chain=true
//   `&p.arr[i].x`   → root=p,   path="arr",  index_in_chain=true  (.x is within-elem)
//   `&self.data[i]` → root=self,path="data", index_in_chain=true  (self.idx disjoint)
//   `&x`            → root=x,   path="",     index_in_chain=false
//   `&*r`           → root="",  path="",     index_in_chain=false (deref outside scope —
//                                            reborrow shape is recognised separately)
//
// Both the RECORD pass (`take_ref_borrows::AddrOfTemp`) and the CHECK pass
// (`visit::AddrOfTemp`) use this so the structural rule cannot drift between
// them. Each call site applies its own POLICY (take vs check) to the result.
// The EXEMPTION half of the reborrow rule, lifted here from the local struct
// it used to be declared inside: a reborrow draws on the REFERENCE's capacity,
// not on the binding's declared mutness, and that is expressed by a TEMPORARY
// param_names_ insertion that must be undone on every exit path. One copy, one
// owner — `record_borrow` arms it, the destructor disarms it.
// The two escapes `record_borrow` still honours, named rather than spelled as
// a pair of bare bools at a call. FILE SCOPE, not a member: a nested class
// with NSDMIs cannot be used as a DEFAULT ARGUMENT of a member function of its
// own enclosing class (the enclosing class is incomplete there) — the compiler
// says so, and it is right.
struct RecordFlags {
    bool skip_mut_binding = false;  // the bare-receiver elision's own escape
                                    // (the DstRef and bare-place method-
                                    // receiver arms)
    bool ref_capacity     = false;  // reborrow: draw on the REFERENCE's
                                    // capacity, not the binding's mutness
    // E0510 — the loan is COMPILER-RAISED, not written by the programmer: a
    // match guard's implicit shared borrow of the scrutinee. It must be
    // recordable over a place the user has ALREADY borrowed, because the two
    // borrows are the same match: `match v { None => …, ref mut foo if k > 0
    // => … }` is legal Rust, and a loan that REPORTS at its own record site
    // refuses it ("cannot borrow 'v' as shared: already mutably borrowed" —
    // measured, hand-written counter-example h19). So an implicit loan that
    // conflicts at the record is dropped SILENTLY: nothing is recorded and
    // nothing is said. The refusals it exists for all happen at a USE inside
    // the guard, never here.
    // ⚠ PERMISSIVE BY CONSTRUCTION, and that is the sound direction — the
    // price is that `match-guards-always-borrow`, whose guard mutates through
    // its own `ref mut` binding, stays admitted (see the guard site).
    bool implicit         = false;
};

struct MutBindBypass {
    std::unordered_set<std::string>* set = nullptr;
    std::string name;
    ~MutBindBypass() { if (set) set->erase(name); }
};

struct BorrowPlace {
    std::string root;             // empty if walker did not reach a VarRef
    std::string path;             // dotted, outermost-inside-root first
    bool        index_in_chain = false;
    TypeRef     root_type = nullptr;   // for raw-ptr / &mut root classification
    uint32_t    root_slot = 0xFFFFFFFFu;  // Phase-1: dense slot of `root`
    // S5-D4 (§B6 provenance): at least one step of the place walk went THROUGH
    // a reference — `b[i]`, `b.f`, `*b`, `b[a..c]` with `b: &T`. The LOAN
    // channel deliberately roots such a place at the reference VARIABLE (see
    // the Deref arm's comment: a reborrow through `r` must lock `r`). The
    // §B6 SOURCE channel must not: the storage the borrow names is the
    // POINTEE, whose life is whatever `b` itself borrows, not `b`'s scope.
    // Recorded here so the two policies can differ without two walkers.
    bool        through_ref = false;
    // ⚠ AND ITS TYPE, WHICH IS THE QUESTION FOUR SPELLINGS WERE ASKING BADLY.
    // `through_ref` says a reference was crossed; the legality of a `&mut`
    // through it is a property of WHICH reference — `&T` can never hand out
    // `&mut` (E0596) and `&mut T` always can, regardless of how the ROOT
    // BINDING was declared. The four exemption spellings this file grew
    // (take_borrow's skip_mut_binding_check, the reborrow arm's fake_param,
    // MutBindBypass, take_field_borrow's root_is_{mut,shared}_ref) all key on
    // the ROOT or on the BINDING, so `&mut *rx` with `rx: &i64` and
    // `&mut *h.r` with `h.r: &i64` were admitted. The innermost reference
    // dereferenced on the way to the place is recorded here — the walk runs
    // outer→inner, so the last assignment wins — and `record_borrow` asks it
    // once for both tails. Null when no reference was crossed.
    TypeRef     through_ref_type = nullptr;
};

static BorrowPlace extract_borrow_place(lir_view::ExprRef inner,
                                         const TypePoolImpl* pool) {
    using namespace lir_view;
    using Code = lir_schema::expr::Code;
    BorrowPlace bp;
    auto cur = inner;
    std::vector<std::string> path_parts;
    // CEILING PROBE `sharedsticky` — MEASURED 2026-08-27: fired 61 796 times
    // across the 447 ledger compiles and closed ZERO rows. NEGATIVE RESULT:
    // the last-assignment-wins policy below is not holding any ledger row
    // open. Making a shared crossing STICKY over the whole chain changes no
    // verdict in the acceptance population — do not re-open without a new
    // mechanism; the site is live, so the zero is an answer and not a silence.
    //
    // The hypothesis was: a shared `&` crossed ANYWHERE in the
    // chain should win over a later, nearer-the-root `&mut`; today the LAST
    // assignment wins (the walk runs outer->inner) and that disarms
    // record_borrow's E0596 gate whenever the root binding is itself `&mut`.
    auto cross = [&](TypeRef t) {
        bp.through_ref = true;
        if (logos::probe::on("sharedsticky") && bp.through_ref_type &&
            bp.through_ref_type.kind() == LogosType::Kind::Ref) return;
        bp.through_ref_type = t;
    };
    while (cur) {
        if (cur.kind() == Code::FieldRead) {
            EFieldReadView fv{cur};
            path_parts.push_back(std::string(fv.field()));
            cur = fv.receiver();
            if (cur && is_ref_kind(cur.type(pool))) cross(cur.type(pool));
        } else if (cur.kind() == Code::TupleIndex) {
            // A tuple element is a FIELD whose name is its index — that is
            // already the spelling everything else uses (`moved_vars_` writes
            // `t.0`, and `emit_cond_move_field_drops` branches on tuple-vs-
            // struct to rebuild the same path). Without this arm the walk fell
            // through to `else break` with an empty root, so NO loan was
            // recorded at all: `let a = &x.0; let b = &mut x.0; *b = 9;`
            // admitted, while the byte-identical program over a named-field
            // struct refused. One arm, not a tuple-shaped copy of the rules.
            ETupleIndexView tv{cur};
            path_parts.push_back(std::to_string(tv.index()));
            cur = tv.receiver();
            if (cur && is_ref_kind(cur.type(pool))) cross(cur.type(pool));
        } else if (cur.kind() == Code::IndexRead) {
            auto recv = EIndexReadView{cur}.receiver();
            // Indexing through a RAW pointer (`p[i]`, p: *mut/*const T) is an
            // unsafe raw deref — like `*p`, it creates NO tracked borrow of the
            // base (Rust parity: the aliasing is the programmer's job inside
            // `unsafe`). `VecIterMut::next` does `&mut self.data[self.idx]` with
            // `data: *mut T`; recording a field borrow on `self.data` there is
            // wrong and would lock all of `self`. Bail with an empty root.
            if (recv && recv.type(pool) &&
                recv.type(pool).kind() == LogosType::Kind::Ptr) {
                // CEILING PROBE `rootkeep` — MEASURED 2026-08-27: fired 427
                // times across the 447 ledger compiles and closed ZERO rows.
                // An empty root makes record_borrow return on its first line,
                // so this bail looked like the permissive half of the class-B
                // gloss and was named "the real subject, upstream of every
                // site". THIS site holds open nothing.
                //
                // ⚠ THE ORIGINAL NOTE OVERCLAIMED, AND THE COVERAGE MAP CAUGHT
                // IT. One probe NAME guarded two bails, and the note read as
                // if the measurement covered both. It does not: over 8060
                // compiler runs (the whole corpus plus four stdlib layers) the
                // map counts 21,299 arrivals HERE and ZERO at the SliceIndex
                // twin below. The negative result is about this line only; the
                // other was never measured, because nothing we compile reaches
                // it. Two sites under one name is the same defect this file is
                // full of, committed in a comment about measuring it.
                if (!logos::probe::on("rootkeep")) { bp.root.clear(); return bp; }
            }
            path_parts.clear();
            bp.index_in_chain = true;
            if (recv && is_ref_kind(recv.type(pool))) cross(recv.type(pool));
            cur = recv;
        } else if (cur.kind() == Code::SliceIndex) {
            auto sl = ESliceIndexView{cur}.slice();
            if (sl && sl.type(pool) &&
                sl.type(pool).kind() == LogosType::Kind::Ptr) {
                // ⚠ NOT MEASURED. The coverage map of 2026-08-27 counts ZERO
                // arrivals here across 8060 compiler runs — the `*mut` bail
                // under SliceIndex rather than under a method receiver. Sharing
                // `rootkeep`'s name with the live site above made its 427 fires
                // read as coverage of both. A ceiling read off this line would
                // be a ceiling off a population of nothing.
                if (!logos::probe::on("rootkeep")) { bp.root.clear(); return bp; }
            }
            path_parts.clear();
            bp.index_in_chain = true;
            if (sl && is_ref_kind(sl.type(pool))) cross(sl.type(pool));
            cur = sl;
        } else if (cur.kind() == Code::Deref) {
            // A borrow through a REFERENCE deref (`*r`, `(*r).f`, `(*r)[i]`) is a
            // borrow of the reference variable `r` — root through it so reborrows
            // through a `&`/`&mut` ref are tracked (recording AND conflict checks).
            // A deref through an OWNING container value (Box / Rc / user Deref
            // struct — `&*b` desugars to Deref::deref(&b)) likewise borrows the
            // container variable itself: `let r = &*b; let c = b;` must reject
            // the move while r is live (Rust parity; adversarial #2 f13).
            // Raw pointers (`*p`) are NOT rooted (unchecked — Rust parity).
            auto op = EDerefView{cur}.operand();
            if (op) {
                auto ok = op.type(pool);
                bool through = is_ref_kind(ok) ||
                    (ok && (ok.kind() == LogosType::Kind::Struct ||
                            ok.kind() == LogosType::Kind::ZonedStruct ||
                            ok.kind() == LogosType::Kind::DstRef));
                if (through) {
                    // S5-D4: only a genuine REFERENCE deref hands the §B6
                    // channel a pointee whose life is the operand's own
                    // provenance. A Box/Rc/user-Deref container OWNS its
                    // content, so the content dies with the container variable
                    // and the root stays the right source.
                    if (is_ref_kind(ok)) cross(ok);
                    // The deref'd CONTENT isn't a sibling-decomposable field
                    // of the container — treat like an index step (whole-
                    // container borrow), dropping any field path collected
                    // below the deref.
                    path_parts.clear();
                    cur = op;
                    continue;
                }
                // CEILING PROBE `ptrderef` — MEASURED 2026-08-27: fired 314
                // times across the 447 ledger compiles and closed ZERO rows.
                // NEGATIVE RESULT over a proven-live site: the coverage map
                // of 2026-08-27 counts 46,887 arrivals here across 8060 runs.
                // ⚠ THE PAIRING CLAIM THIS COMMENT USED TO MAKE IS RETIRED.
                // It said `rootkeep` priced "the sibling raw-ptr bails in
                // IndexRead/SliceIndex" and that "both spellings" hold open
                // nothing. `rootkeep` named TWO sites: the IndexRead bail
                // (21,299 arrivals, genuinely priced at 0) and the SliceIndex
                // bail (ZERO arrivals — never reached by anything we compile,
                // therefore never measured). Only ONE spelling was priced.
                // The raw-pointer axis is refuted for the Deref and IndexRead
                // forms and UNMEASURED for SliceIndex.
                //
                // The hypothesis was: the raw-pointer / non-through
                // deref bail loses the ROOT, so `*p` records nothing and is
                // checked by nothing. Root through it anyway (crude: Rust
                // deliberately leaves raw derefs unchecked).
                if (logos::probe::on("ptrderef")) {
                    path_parts.clear();
                    cur = op;
                    continue;
                }
            }
            break;
        } else if (cur.kind() == Code::MethodCall ||
                   cur.kind() == Code::Call ||
                   cur.kind() == Code::AddrOfTemp) {
            // The place is reached THROUGH A CALL: a user Deref/Index impl, or
            // an autoref'd receiver sema lowered to a plain Call. `callsite` is
            // the observational outer half (rule 9) — the whole population of
            // this arm, armed or not.
            (void)logos::probe::on("callsite");
            // LANDED 2026-08-29, priced as `callidxcallonly` (PROBES.md): the
            // place is reached through a call that RETURNS A REFERENCE, so the
            // reference names a place INSIDE the receiver / first argument.
            // Hop to it and keep walking. `index_in_chain` is the DEPOSIT half
            // — visit()'s AddrOfTemp arm records a whole-root borrow only for a
            // path it already believes in, and a call is opaque exactly the way
            // `[i]` is (the returned reference may name ANY part of the
            // receiver), so the flag is the truth here, not a guess. It is NOT
            // set for AddrOfTemp: that is an autoref of a place still in view,
            // and widening it to the whole root broke liblogos-lang 2026-08-27.
            bool p_hop = is_ref_kind(cur.type(pool));
            bool p_idx = p_hop && cur.kind() != Code::AddrOfTemp;
            // CEILING PROBE `callroot` — the widening that is still NOT landed:
            // hop a call whose result is not a reference. A by-value result
            // names no place in its receiver, so this over-refuses by
            // construction; it is kept as the measured alternative.
            if (!p_hop && logos::probe::on("callroot")) p_hop = true;
            if (!p_hop) break;
            ExprRef nxt;
            if (cur.kind() == Code::AddrOfTemp)
                nxt = EAddrOfTempView{cur}.inner();
            else if (cur.kind() == Code::MethodCall)
                nxt = EMethodCallView{cur}.receiver();
            else
                ECallView{cur}.each_arg([&](ExprRef a) { if (!nxt) nxt = a; });
            while (nxt && (nxt.kind() == Code::AddrOfTemp ||
                           nxt.kind() == Code::SliceLit)) {
                nxt = nxt.kind() == Code::AddrOfTemp
                        ? EAddrOfTempView{nxt}.inner()
                        : ESliceLitView{nxt}.base();
            }
            if (!nxt) break;
            path_parts.clear();
            if (p_idx) bp.index_in_chain = true;
            cur = nxt;
            continue;
        } else {
            break;
        }
    }
    if (cur && cur.kind() == Code::VarRef) {
        bp.root = std::string(EVarRefView{cur}.name());
        bp.root_slot = EVarRefView{cur}.var_slot();  // Phase-1
        bp.root_type = cur.type(pool);
        // CEILING PROBE `refwhole` — a place reached THROUGH a reference is
        // recorded as a FIELD borrow of the REFERENCE BINDING, so two
        // projections of one ref never overlap and take_field_borrow_path_'s
        // ref-root exemption skips the mut-binding check. Collapse to the
        // whole root: crude, and in the refusing direction.
        if (logos::probe::on("refwhole") && bp.through_ref) path_parts.clear();
        for (auto it = path_parts.rbegin(); it != path_parts.rend(); ++it) {
            if (!bp.path.empty()) bp.path.push_back('.');
            bp.path.append(*it);
        }
    }
    return bp;
}

// Merge Phase-1 move state from 'other' into 'base' (union of moved sets).
// Borrows are scope-local and do not survive merges.
static void merge_moves(StateMap& base, StateMap& other) {
    other.for_each([&](uint32_t slot, std::string_view name, VarState& st) {
        if (st.moved) base.at_id(slot, name) = st;
    });
}

// D1 round 2, Door B — the JOIN half of the same defect. A loan RECORD can now
// outlive the frame it was recorded in (pop_scope re-homes it when a holder is
// an outer binding), but the borrow COUNTERS live in the VarState map, which
// `if` and the loop passes save and RESTORE wholesale across a branch. So the
// record survived while the evidence for it was rolled back and `c.bump()`
// still saw zero shared borrows (measured: B1 re-homed target=c holder=b and
// STILL compiled). Carry the accumulators forward: a borrow live on ANY path
// is live after the join.
//
// D1 round 10, J0 — the restriction to `rehomed_slots_`/`rehomed_names_` is
// DELETED. It was wrong-sided: it separated PROVENANCE (was the counter raised
// by Door B's re-homing, or by a CALL SUMMARY?) and not lifetime. Measured, a
// fire print inside this function over the whole stdlib (lang/mem/lcm/std):
// 99 rows arrive with a raised counter — 59 were carried, 40 dropped — and
// `base.has_id(slot,name)` was TRUE for all 99. The region-local population
// the restriction was written to protect is EMPTY at this site: such a loan is
// already released by pop_scope / NLL inside the region and never arrives,
// exactly as the paragraph above says. So `fn stash(c:&C,v:&mut Vec<B>)` called
// inside `while` / `for` / `if`-with-empty-else raised the counter, the join
// threw it away, and `c.bump()` after the region compiled (J0 witnesses
// j0_while_leak / j0_for_leak / j0_if_empty_else_leak, all rc=0 → rc=1 here).
//
// The residency test below is the discriminator that IS real ("the loan's
// target outlives the region" = the target binding existed before it), and it
// is written as a guard rather than assumed: today it holds 99/99, and if a
// future change starts delivering region-local rows they are dropped instead
// of resurrecting a dead binding's slot.
static void merge_loans(StateMap& base, StateMap& other) {
    other.for_each([&](uint32_t slot, std::string_view name, VarState& st) {
        if (!base.has_id(slot, name)) return;
        auto& b = base.at_id(slot, name);
        if (st.shared_borrows   > b.shared_borrows)   b.shared_borrows   = st.shared_borrows;
        if (st.mut_reservations > b.mut_reservations) b.mut_reservations = st.mut_reservations;
        if (st.mut_borrowed) b.mut_borrowed = true;
        for (auto& [p, n] : st.shared_field_borrows) {
            // ── OBSERVATIONAL PROBES, THE `shared_field_borrows` ZERO CENSUS.
            // Nothing here is gated on a probe: the counters change no
            // behaviour, they only say WHO PUTS A ZERO IN THE MAP. This
            // default-insert is the one writer that CAN leave a zero (it
            // inserts 0 and then only raises it when `n > cur`), so it is the
            // standing static explanation for the 144 `c <= 0` reads in
            // take_field_borrow_path_. Coverage 2026-08-27: this loop body 785
            // iterations, the `cur = n` assignment ZERO — so over 8060 runs
            // the raise never once fired.
            (void)logos::probe::on("szw_merge_iter");
            if (n <= 0) (void)logos::probe::on("szw_merge_srczero");
            if (logos::probe::on("szdump_merge") && n <= 0)
                std::fprintf(stderr, "SZDUMP merge path=%s n=%d\n", p.c_str(), n);
            const size_t szw_n0 = b.shared_field_borrows.size();
            auto& cur = b.shared_field_borrows[p];
            if (b.shared_field_borrows.size() != szw_n0)
                (void)logos::probe::on("szw_merge_fresh");
            if (n > cur) cur = n;
            if (cur <= 0) (void)logos::probe::on("szw_merge_leftzero");
        }
        for (auto& p : st.mut_field_borrows) b.mut_field_borrows.insert(p);
    });
}

// ── Function index (escape analysis) ────────────────────────────────────────
// Program-global symbol→callee maps consulted by escape analysis
// (`result_borrows_self` / `method_self_kind`). The index depends ONLY on the
// program, not on the function being checked — so it is built ONCE per compile
// in borrow_check() and shared (const) across every per-function BorrowChecker.
// (It used to be a per-instance `mutable` map rebuilt lazily inside each of the
// N BorrowCheckers → O(N × total_fns) rebuild storm, the top malloc cost.)
static bool bc_is_borrow_carrying_type(const TypeSets& ts_, TypeRef t) {
    if (!t) return false;
    auto k = t.kind();
    std::string nm;
    if (k == LogosType::Kind::Enum)                 // WAny: the niche-enum form (F3)
        nm = std::string(t.enum_name());
    else if (k == LogosType::Kind::Struct || k == LogosType::Kind::ZonedStruct)
        nm = std::string(t.struct_name());
    // Laundered escape package (`Held<T>`/`HeldAny`: holds an Rc/Arc holder
    // that keeps the arena alive) — never borrow-carrying, including via its
    // type-args (`Held<WArray<WAny>>`). Same exemption the definition-side
    // closure applies; without it the container-element rule below would
    // reject returning the escape hatch — its whole purpose.
    //
    // D1 — the exemption checked in the ABUSE direction (exempt type
    // WRAPPING a borrow-carrying type). DECISION: the exemption still wins
    // HERE, unconditionally, and the type-arg recursion below is not
    // reached. This function answers an ESCAPE question ("may this value
    // leave the referent's scope? — yes, the Rc/Arc share keeps the arena
    // alive"), and that answer is unchanged.
    //
    // ROUND 2 — WHAT THE OLD TEXT CLAIMED, AND WHAT IS ACTUALLY TRUE. It
    // claimed the abuse direction was closed "one level up, by the loan
    // channel". MEASURED: that holds ONLY when the wrapper is built in the
    // frame that later mutates — `let hh = H { h: rc_new(7), b: c.mk() };
    // c.bump()` refuses (/tmp/bcm/c2_rc_exempt.logos, X1) because the loan
    // was recorded right there. Built in a CALLEE it did not: `fn make(c:
    // &C) -> H { … }; let hh = make(&c); let b = hh.b; c.bump()` compiled
    // (X2), and `Option<H>` inherited the same false through the type-arg
    // recursion (X3). The reason was that the loan channel gated on THIS
    // function and therefore inherited the exemption too — and, one level
    // deeper, that `residency_exempt` is applied at REGISTRATION, so `H`
    // never entered `borrow_carrying` at all.
    //
    // FIX (and the decision): the loan channel now has its own predicate,
    // `loan_carrying_type` over `TypeSets::loan_carrying` — the same
    // structural closure computed WITHOUT the residency skip. A loan says
    // "the arena is still being READ"; an Rc share says "the arena is still
    // ALIVE". Those are different claims, and only the second is what the
    // hatch exists to assert. So:
    //   /tmp/bcm/c4_exempt_return.logos  rc=0 — return H past the arena
    //                                     (the hatch's purpose: a RETURN
    //                                      gate, which still asks THIS fn)
    //   /tmp/bcm/c2_rc_exempt.logos      rc=1 — in-frame abuse (unchanged)
    //   X2 (exempt built in a callee)    rc=1 — was rc=0, closed by the
    //                                     loan-channel predicate
    //   X3 (Option<H>)                   rc=1 — same, through type-args
    // NO HOLE IS KNOWN TO REMAIN in the abuse direction; if one turns up,
    // it belongs in `loan_carrying`, not here.
    // Consequently EVERY D1 rule gates on this function rather than on a
    // raw name test — a name test would re-capture Held/HeldAny and kill
    // c4. See also: this function was MEASURED non-guilty for D1 and is
    // otherwise unchanged. It already implements "bc if the name is in the
    // set OR any type-arg is bc"; the mangled `Option__B` / `X$G1$Y` forms
    // never reach it (TypeRefs keep their type-args post-mono, and all name
    // munging lives in reg_bc_name at registration), which is why
    // `Option<B>` is bc here by the type-arg recursion and not by name.
    if (!nm.empty() && ts_.residency_exempt.count(nm) > 0) return false;
    if (!nm.empty() && ts_.borrow_carrying.count(nm) > 0) return true;
    // A generic CONTAINER of a borrow-carrying element carries its elements'
    // borrows (`Vec<WAny>`, `Option<WAny>`, `Box<WAny>`) — even though the
    // buffer sits behind an owning pointer / the payload is a type-param, the
    // value transitively holds a Ref into an arena. (A raw `*mut WAny` has no
    // type-args → stays unchecked, like box_leak — Rust parity.)
    for (auto a : t.type_args())
        if (bc_is_borrow_carrying_type(ts_, a)) return true;
    // D1 round 2, Door G — the STRUCTURAL containers. A tuple and an array
    // hold their element types outside `type_args()`: `(B, i64)` came back
    // NOT borrow-carrying (measured — the destructure's `let __destruct_1:
    // (B,i64) = __destruct_0` spill was not routed through
    // take_ref_borrows at all, so the hop chain broke one step before the
    // binding). The container rule above already says a `Vec<B>` / `Box<B>`
    // carries its elements' borrows; a tuple or an array is the same claim
    // with the elements spelled structurally instead of as type-args.
    if (k == LogosType::Kind::Tuple) {
        for (auto e : t.tuple_elems())
            if (bc_is_borrow_carrying_type(ts_, TypeRef(e))) return true;
        return false;
    }
    if (k == LogosType::Kind::Array || k == LogosType::Kind::Slice)
        return bc_is_borrow_carrying_type(ts_, t.elem());
    return false;
}

static bool bc_loan_carrying_type(const TypeSets& ts_, TypeRef t) {
    if (!t) return false;
    auto k = t.kind();
    std::string nm;
    if (k == LogosType::Kind::Enum) nm = std::string(t.enum_name());
    else if (k == LogosType::Kind::Struct || k == LogosType::Kind::ZonedStruct)
        nm = std::string(t.struct_name());
    if (!nm.empty() && ts_.loan_carrying.count(nm) > 0) return true;
    for (auto a : t.type_args())
        if (bc_loan_carrying_type(ts_, a)) return true;
    if (k == LogosType::Kind::Tuple) {
        for (auto e : t.tuple_elems())
            if (bc_loan_carrying_type(ts_, TypeRef(e))) return true;
        return false;
    }
    if (k == LogosType::Kind::Array || k == LogosType::Kind::Slice)
        return bc_loan_carrying_type(ts_, t.elem());
    return false;
}

// D1 round 10 / SP0+SP1 — "a deposit could be written through this value":
// the value IS a `&mut`, or it is an aggregate that transitively holds one.
static bool bc_holds_mut_ref_type(const TypeSets& ts_, TypeRef t) {
    if (!t) return false;
    auto k = t.kind();
    if (k == LogosType::Kind::MutRef) return true;
    std::string nm;
    if (k == LogosType::Kind::Enum) nm = std::string(t.enum_name());
    else if (k == LogosType::Kind::Struct || k == LogosType::Kind::ZonedStruct)
        nm = std::string(t.struct_name());
    if (!nm.empty() && ts_.holds_mut_ref.count(nm) > 0) return true;
    for (auto a : t.type_args())
        if (bc_holds_mut_ref_type(ts_, a)) return true;
    if (k == LogosType::Kind::Tuple) {
        for (auto e : t.tuple_elems())
            if (bc_holds_mut_ref_type(ts_, TypeRef(e))) return true;
        return false;
    }
    if (k == LogosType::Kind::Array || k == LogosType::Kind::Slice)
        return bc_holds_mut_ref_type(ts_, t.elem());
    return false;
}

// ── #77 round 2 / SEED — "a borrow could be EXTRACTED from this value" ──────
//
// THE DEFECT, measured at the SUMMARY (LOGOS_DUMP_FLOWS=pick):
//   `struct H { r: &i64 }  fn pick(h: H) -> &i64 { return h.r; }`
//     → `sp$pick__f__H: result<-0  EXACT  (rounds=2)`
// A by-VALUE struct param whose SHARED-ref field is returned summarised as
// retaining NOTHING — and, worse, said EXACT about it, so #77's new
// return-escape door trusted the empty mask and the caller
// (`let t = 9; let h = H{r:&t}; return pick(h);`) compiled rc=0.
//
// `can_carry` asked four predicates and none of them answers for this shape:
// `H` is not a ref, not `#[borrow_carrying]` (that attribute is DECLARED),
// not loan-carrying (that set only propagates NAMED carriers), and
// `bc_holds_mut_ref_type` is `&mut`-only by construction — round 11 / X2 fixed
// exactly this hole for the `&mut` half and left the `&` half open.
// `ts_.holds_any_ref` is the fixpoint #71 already built for the same question
// on the checker's side (`type_may_carry_borrow` consults it); the summarizer
// simply never asked it, so the two channels disagreed about the same type.
// Adding it only ever ADDS flows — every consumer of a mask bit widens, none
// narrows — which is the direction this analysis is allowed to err in.
static bool bc_holds_any_ref_type(const TypeSets& ts_, TypeRef t) {
    if (!t) return false;
    auto k = t.kind();
    if (is_ref_kind(t)) return true;
    std::string nm;
    if (k == LogosType::Kind::Enum) nm = std::string(t.enum_name());
    else if (k == LogosType::Kind::Struct || k == LogosType::Kind::ZonedStruct)
        nm = std::string(t.struct_name());
    if (!nm.empty() && ts_.holds_any_ref.count(nm) > 0) return true;
    for (auto a : t.type_args())
        if (bc_holds_any_ref_type(ts_, a)) return true;
    if (k == LogosType::Kind::Tuple) {
        for (auto e : t.tuple_elems())
            if (bc_holds_any_ref_type(ts_, TypeRef(e))) return true;
        return false;
    }
    if (k == LogosType::Kind::Array || k == LogosType::Kind::Slice)
        return bc_holds_any_ref_type(ts_, t.elem());
    return false;
}

struct FnIndex {
    std::unordered_map<std::string, lir_view::FunctionView>              by_name;
    std::unordered_map<std::string, std::vector<lir_view::FunctionView>> by_base;
    // #83: the MONO TEMPLATE KEY spelling (`bare_fn_name` of the LIR name —
    // signature tail and package prefix stripped). Mono writes its own
    // worklist key as the callee of a devirtualised generic method call, and
    // that key is not any function's name. See resolve_call_flow.
    std::unordered_map<std::string, std::vector<lir_view::FunctionView>> by_bare;
};

static FnIndex build_fn_index(const lir::LProgram& prog) {
    FnIndex idx;
    {   // post-mono this indexes thousands of fns; reserve to skip the rehashes.
        size_t cnt = prog.functions.size() + prog.specializations.size();
        for (auto& sd : prog.structs) cnt += sd.methods().size();
        // Stage E: LImplBlock.methods was always empty (never populated) — gone.
        idx.by_name.reserve(cnt);
    }
    auto add = [&](const LFunctionPtr& f) {
        if (!f) return;
        idx.by_name.emplace(std::string(f.name()), f);
        if (!f.method_base().empty()) idx.by_base[std::string(f.method_base())].push_back(f);
        idx.by_bare[std::string(bare_fn_name(f.name()))].push_back(f);
    };
    for (auto& f  : prog.functions)       add(f);
    for (auto& f  : prog.specializations) add(f);
    for (auto& sd : prog.structs) sd.each_method([&](lir_view::FunctionView m) { add(m); });
    // Stage E: impl-block methods were never stored on LImplBlock (always empty);
    // trait-impl methods (Index, Deref, …) live on prog.functions / struct methods.
    return idx;
}

// ── D1 round 8 / THE UNIFICATION: ONE REF-PROVENANCE GRAPH ─────────────────
//
// THE DEFECT THE PIECEWISE APPROACH HAS. Rounds 5-7 grew a recorder and a
// chase at each POSITION the leak was witnessed at: `reborrow_referent` for a
// `let`, `note_struct_lit_reborrows` for a struct literal, `prescan_referent`
// for the NLL pre-pass, `note_alias` for the summarizer, `rehome_reborrow` /
// `resolve_place_reborrow` for the two place walks. Each learned exactly the
// shapes its witness spelled, so the machinery leaked at every position it was
// NOT taught — measured twice more in hunt 8:
//   U0  `let rc: &C = &c; let rc2: &C = rc; cur.batch(rc2);` held across a
//       `c.bump()` COMPILES, while the one-hop spelling refuses. The chase was
//       not one hop short: it resolved rc2 → c correctly and handed back the
//       ENDPOINT, discarding `rc` — the node that actually holds c's loan.
//   U1  `let mut r2: &mut Vec<B> = h.r;` records NOTHING, because the shape
//       gate accepted only AddrOf / AddrOfTemp / VarRef; `r2.push(…)` then
//       launders a loan that the inline `h.r.push(…)` refuses.
// Both are one sentence: the graph must be recorded from EVERY shape that
// names a place, and resolved to the TRANSITIVE CLOSURE, not the endpoint.
//
// THE STRUCTURE. `RefGraph` is the single edge store + the single resolve.
// Three INSTANCES survive, and they are three passes, not three graphs — the
// difference between them is a stated RETRACTION POLICY, not a second
// implementation or a second shape enumeration:
//   • the checker's `refs_`        — FLOW-SENSITIVE: `set()` replaces, a
//                                    non-reborrow write erases (the fact must
//                                    not outlive the binding).
//   • the pre-pass's `prescan_refs_` — MONOTONE `add()`: a whole-body union
//                                    that only ever EXTENDS an NLL live range,
//                                    and must not be retracted by a later
//                                    branch it never saw.
//   • the summarizer's `alias_`    — MONOTONE `add()`: the body is walked to a
//                                    fixpoint, so a later re-binding may not
//                                    retarget what an earlier pass deposited.
// `each_root` is the one resolve for all three (it was already the summarizer's
// — this moves it under the store both sides share). `endpoint()` is the
// single-name VIEW for the two consumers that must answer with one name; it is
// the same walk, stopped early, and it replaces `rehome_reborrow`'s ad-hoc
// 8-hop bounded chase (the hop cap was the cycle guard; a visited set is).
struct RefGraph {
    // Keys and values are DOTTED PLACES ("h", "h.r", "h.r.p"), not bindings —
    // the identity `h.r == vs` is not expressible over binding names (round 6).
    // The value is a VECTOR because one reference may name two places
    // (`if c { &a } else { &b }`), and because the monotone instances union.
    std::unordered_map<std::string, std::vector<std::string>> e_;

    bool empty() const { return e_.empty(); }
    void clear() { e_.clear(); }
    const std::vector<std::string>* find(const std::string& p) const {
        auto it = e_.find(p);
        return it == e_.end() ? nullptr : &it->second;
    }
    // FLOW-SENSITIVE write. An empty source list ERASES: a write that is not a
    // reborrow retracts, so the map never outlives the fact.
    void set(const std::string& p, std::vector<std::string> s) {
        s.erase(std::remove(s.begin(), s.end(), p), s.end());
        if (s.empty()) e_.erase(p);
        else           e_[p] = std::move(s);
    }
    // MONOTONE write.
    void add(const std::string& p, const std::string& s) {
        if (p.empty() || s.empty() || s == p) return;
        auto& v = e_[p];
        if (std::find(v.begin(), v.end(), s) == v.end()) v.push_back(s);
    }
    void erase(const std::string& p) { e_.erase(p); }
    // Every recorded place strictly UNDER `root` ("hh" → "hh.r", "hh.i.r").
    // The sub-place view of the same store: a reference to a whole aggregate
    // reaches what its fields reborrow, and only the field places carry those
    // edges (round 6: `h.r == vs` is not expressible over binding names).
    template <class F> void each_under(const std::string& root, F&& f) const {
        if (root.empty()) return;
        std::string pfx = root + ".";
        for (auto& [k, v] : e_) {
            (void)v;
            if (k.size() > pfx.size() && k.compare(0, pfx.size(), pfx) == 0) f(k);
        }
    }
    // Re-binding a root retracts every place recorded UNDER it: the old value
    // is gone, so `root.f` no longer names whatever it used to reborrow.
    void erase_under(const std::string& root) {
        std::string pfx = root + ".";
        for (auto it = e_.begin(); it != e_.end(); )
            if (it->first.size() > pfx.size() &&
                it->first.compare(0, pfx.size(), pfx) == 0) it = e_.erase(it);
            else ++it;
    }
    // THE ONE RESOLVE. `f` is called on `start` and on EVERY place reachable
    // from it, each exactly once. Cycle-safe by the visited set (a self-edge is
    // refused at insertion, but that is then a property of this walk rather
    // than of every caller).
    template <class F> void each_root(const std::string& start, F&& f) const {
        if (start.empty()) return;
        if (e_.empty()) { f(start); return; }
        std::vector<std::string> stack{start}, seen;
        while (!stack.empty()) {
            std::string n = std::move(stack.back());
            stack.pop_back();
            if (std::find(seen.begin(), seen.end(), n) != seen.end()) continue;
            seen.push_back(n);
            f(n);
            if (auto* v = find(n)) for (auto& r : *v) stack.push_back(r);
        }
    }
    // ── D1 round 11 / X3: THE SAME WALK, PREFIX-AWARE ─────────────────────
    //
    // `each_root` is a FLAT map walk: it follows the edges recorded on the
    // exact string it is handed. That is enough while every edge is keyed by a
    // ROOT, and wrong as soon as they are keyed by PLACES — `inn -> h.i` plus
    // `h.i.r -> v` must answer `inn.r` ⇒ {inn.r, h.i.r, v}, and the flat walk
    // finds no edge on "inn.r" at all. So a place also inherits what its
    // PROPER PREFIXES reborrow, with the remaining suffix re-attached: if
    // `inn` names `h.i`, then `inn.r` names `h.i.r`.
    //
    // This lives on the STORE, next to the walk it generalises, because both
    // instances need it for the same reason. Duplicating it into the
    // summarizer was the measurement stand-in of round 11 and is exactly the
    // mistake round 8 charged for (one shape enumeration, one resolve).
    template <class F> void each_root_place(const std::string& start, F&& f) const {
        if (start.empty()) return;
        if (e_.empty()) { f(start); return; }
        std::vector<std::string> stack{start}, seen;
        while (!stack.empty()) {
            std::string n = std::move(stack.back());
            stack.pop_back();
            if (std::find(seen.begin(), seen.end(), n) != seen.end()) continue;
            if (seen.size() > 512) break;          // bound, as elsewhere here
            seen.push_back(n);
            f(n);
            if (auto* v = find(n)) for (auto& r : *v) stack.push_back(r);
            for (size_t d = n.find('.'); d != std::string::npos;
                 d = n.find('.', d + 1)) {
                std::string pre = n.substr(0, d), suf = n.substr(d);
                if (auto* v = find(pre))
                    for (auto& r : *v) stack.push_back(r + suf);
            }
        }
    }
    // The single-name VIEW of the same walk: follow the first edge to a
    // terminal. For the two consumers whose contract is one name (a written
    // place's root, a receiver's root); every consumer that can take a SET
    // takes each_root instead — that is what U0 turns on.
    std::string endpoint(std::string cur) const {
        std::vector<std::string> seen;
        while (!cur.empty()) {
            if (std::find(seen.begin(), seen.end(), cur) != seen.end()) break;
            seen.push_back(cur);
            auto* v = find(cur);
            if (!v || v->empty() || v->front() == cur) break;
            cur = v->front();
        }
        return cur;
    }
};

// The ROOT BINDING of a dotted place ("h.r.p" -> "h"). A dotted place names a
// variable only through its root; the loan channels are keyed by variable.
static std::string ref_place_root(const std::string& p) {
    auto d = p.find('.');
    return d == std::string::npos ? p : p.substr(0, d);
}

// ── THE ONE SHAPE WALKER ───────────────────────────────────────────────────
//
// Every SOURCE PLACE a reference-valued expression names. This is the union of
// what the five old recorders accepted, and it is the half U1 was missing:
//   VarRef / AddrOf / AddrOfTemp          — the three the old gate allowed;
//   FieldRead / TupleIndex / IndexRead /
//   SliceIndex / Deref chains             — U1 (`h.r`, `&mut *h.r`), yielding
//                                           the DOTTED place while every step
//                                           is a field read, the bare root
//                                           once an index/tuple step makes the
//                                           rest unnameable;
//   Cast / IfExpr / BlockExpr             — transparent, both arms.
// A RAW-POINTER projection or deref yields NOTHING, exactly as every other
// walk in this file (Rust parity: the pointee is not tied to the local holding
// the pointer). Call/MethodCall results are deliberately NOT here: what a
// callee returns is the borrow-flow SUMMARY's question, and answering it from
// the shape would be a guess.
static void ref_source_places(lir_view::ExprRef val, const TypePoolImpl* pool,
                              std::vector<std::string>& out, int depth = 0) {
    using namespace lir_view;
    using Code = lir_schema::expr::Code;
    if (!val || depth > 8) return;
    switch (val.kind()) {
        case Code::AddrOf: {
            std::string n(EAddrOfView{val}.var_name());
            if (!n.empty()) out.push_back(std::move(n));
            return;
        }
        case Code::AddrOfTemp:
            ref_source_places(EAddrOfTempView{val}.inner(), pool, out, depth + 1);
            return;
        // ⚠ THE ARRAY→SLICE COERCION IS TRANSPARENT TO PROVENANCE, and this was
        // the ONE walker of four that did not say so. prov_of, taint_of and
        // collect_ref_sources_paths all have the arm; here a SliceLit matched
        // nothing, the projection loop below broke with `cur` still the
        // SliceLit, the terminal-VarRef test failed, and the walk yielded no
        // source place at all.
        // MEASURED as the SECOND of two independent blockers: opening the type
        // gate in is_reborrow_ref_kind closed the `&dyn Tr` twin (rc 0 ->
        // refused) and left `let s: &[B] = &w;` still admitted, which is what
        // separates the two. Only BASE_PTR can carry provenance; the length is
        // a scalar.
        case Code::SliceLit:
            ref_source_places(ESliceLitView{val}.base(), pool, out, depth + 1);
            return;
        case Code::Cast:
            ref_source_places(ECastView{val}.operand(), pool, out, depth + 1);
            return;
        case Code::IfExpr:
            ref_source_places(EIfExprView{val}.then_val(), pool, out, depth + 1);
            ref_source_places(EIfExprView{val}.else_val(), pool, out, depth + 1);
            return;
        case Code::BlockExpr:
            ref_source_places(EBlockExprView{val}.result(), pool, out, depth + 1);
            return;
        // ── D1 round 12 / A0: A MATCH IS A TRANSPARENT MULTI-ARM VALUE ─────
        //
        // `let s: &mut Vec<B> = match k { 1 => &mut vs, _ => &mut ws };`
        // ADMITTED a later `c.bump()` while the `if`-`else` twin one line
        // above REFUSED it. The two walkers over the SAME shape space had
        // drifted: the summarizer's `taint_of` (borrow_flow_summary.inc, the
        // `Code::MatchExpr` arm) already unioned the arms, this one did not.
        //
        // That drift is exactly what round 8 charged for: ONE shape
        // enumeration, not one per walker. Every arm below IfExpr's — Cast,
        // IfExpr, BlockExpr, MatchExpr — must be added to BOTH walkers in the
        // same step, or the next one-shape omission is re-introduced silently
        // (nothing reds: an over-permissive walker never breaks a working
        // program; only a hand-written refusal witness sees it).
        //
        // The scrutinee is deliberately NOT a source: a match yields one of
        // its ARM values, never the thing it discriminated on. Pattern
        // BINDINGS that carry the scrutinee's places are a separate rule and
        // live in `propagate_pat_loans`.
        case Code::MatchExpr:
            EMatchExprView{val}.each_arm([&](EMatchArmRef arm) {
                ref_source_places(arm.value(), pool, out, depth + 1);
            });
            return;
        // D1 round 13 / P0c: `?` IS TRANSPARENT, and this was the one walker
        // of the four that did not say so — `taint_of`, the summarizer's
        // operand walker, `scan_uses_expr` and `BorrowChecker::visit` all
        // carry a `Try` arm. Without it a Try does not even reach the default
        // path as a no-op: it falls INTO the projection loop below, breaks at
        // a non-VarRef and yields nothing, so `let s: &mut Vec<B> =
        // pick(&mut vs)?;` names no source place at all while the direct-
        // return twin `pickd(&mut vs)` refuses.
        case Code::Try:
            ref_source_places(ETryView{val}.inner(), pool, out, depth + 1);
            return;
        // D1 round 13 / P1: an ARRAY LITERAL names what its ELEMENTS name.
        // The read side of the whole-container convention above: the array
        // place is the key every element's edge is recorded on, so the value
        // stored there is the UNION of the elements' sources. `let arr =
        // [&mut vs];` therefore records `arr -> vs`, and `arr[0]` (which the
        // index step answers as `arr`) resolves through it.
        case Code::ArrLit:
            EArrLitView{val}.each_elem([&](ExprRef ev) {
                ref_source_places(ev, pool, out, depth + 1);
            });
            return;
        default: break;
    }
    auto is_rawptr = [&](ExprRef r) {
        return r && r.type(pool) && r.type(pool).kind() == LogosType::Kind::Ptr;
    };
    ExprRef cur = val;
    std::vector<std::string> fields;   // outermost first
    bool path_ok = true;
    while (cur) {
        Code k = cur.kind();
        if (k == Code::FieldRead) {
            auto r = EFieldReadView{cur}.receiver();
            if (is_rawptr(r)) return;
            if (path_ok) fields.emplace_back(EFieldReadView{cur}.field());
            cur = r; continue;
        }
        // ── D1 round 13 / P1, door 2: A TUPLE STEP IS A PATH STEP ──────────
        //
        // `let t = (&mut vs, 0); let s = t.0;` ADMITTED a later `c.bump()`
        // while the struct twin `H { r }` REFUSED it: this walk dropped
        // `path_ok` on a tuple step and answered the bare ROOT `t`, and
        // `each_root("t")` never reaches `t.0`'s target. A tuple index is a
        // CONSTANT and a place segment is a string, so "t.0" is representable
        // with no key-format change at all — `ref_place_root` still answers
        // "t", `erase_under("t")` still retracts it, and the §B6 channel
        // already MINTS exactly this spelling (collect_ref_sources_paths'
        // TupleLit arm). A source field name can never be a numeral, so the
        // numeric segment cannot alias a struct field.
        if (k == Code::TupleIndex) {
            auto r = ETupleIndexView{cur}.receiver();
            if (is_rawptr(r)) return;
            if (path_ok)
                fields.emplace_back(std::to_string(ETupleIndexView{cur}.index()));
            cur = r; continue;
        }
        // ── AN INDEX STEP NAMES THE CONTAINER, WHOLE ───────────────────────
        //
        // An array index is NOT a static path component: `arr[i]` with a
        // dynamic `i` cannot be named, and a per-element key would be unsound
        // in the RETRACTION direction (under the flow-sensitive `set()` a
        // write to `arr[i]` cannot retract one element). So the element edge
        // is keyed at the ARRAY PLACE itself — whole-element granularity,
        // which is this file's own stated convention for arrays
        // (collect_ref_sources_paths' ArrLit arm: "every element lands on
        // `path`"). The step therefore keeps the path and appends NOTHING,
        // instead of collapsing to the root: `h.a[0]` answers `h.a`, the
        // place the element edge is recorded on, and never a place that was
        // not written. A dynamic index takes the same answer as a constant
        // one, so an unnameable index does not silently drop the edge.
        if (k == Code::IndexRead) {
            auto r = EIndexReadView{cur}.receiver();
            if (is_rawptr(r)) return;
            cur = r; continue;
        }
        if (k == Code::SliceIndex) {
            auto s = ESliceIndexView{cur}.slice();
            if (is_rawptr(s)) return;
            cur = s; continue;
        }
        if (k == Code::Deref) {
            auto op = EDerefView{cur}.operand();
            if (is_rawptr(op)) return;      // raw deref — unchecked
            cur = op; continue;
        }
        break;
    }
    if (!cur || cur.kind() != Code::VarRef) return;
    std::string p(EVarRefView{cur}.name());
    if (p.empty()) return;
    if (path_ok)
        for (auto it = fields.rbegin(); it != fields.rend(); ++it) { p += '.'; p += *it; }
    out.push_back(std::move(p));
}

// ── THE ONE STORE ENUMERATION (D1 round 9 / S1) ────────────────────────────
//
// Round 8 unified the SHAPE walker (`ref_source_places`) and the STORE and the
// RESOLVE (`RefGraph`), but the FEEDING stayed split: the checker learned a
// struct-literal door (`note_struct_lit_reborrows`) and a field-write door,
// and the summarizer got neither — its `note_alias` was called only from
// `walk_stmt`'s Let/Assign, behind an `is_mut_ref(VALUE)` gate that a struct
// literal cannot pass. S1 is the bill: `fn stash(c: &C, v: &mut Vec<B>) { let
// mut h: Inner = Inner { r: v }; h.r.push(c.mk()); }` loses `out1` ENTIRELY
// (no summary line at all), while its local-alias twin summarises correctly.
//
// This is that enumeration, written ONCE: `sink(place, value)` for the
// destination itself and, recursively, for every field place a NESTED
// aggregate literal stores into. Both instances of the graph are fed through
// it. What stays per-instance is the POLICY applied to each pair, which is
// what the three instances were always defined to differ by:
//   • the checker resolves each pair to the transitive closure and RETRACTS
//     by PLACE (flow-sensitive `set`);
//   • the summarizer charges both ends to their ROOT (its map is keyed by
//     name, because `taint_` is) and only ever ADDS (monotone, because the
//     body is walked to a fixpoint).
template <class Sink>
static void each_ref_store(const std::string& dest, lir_view::ExprRef val,
                           Sink&& sink, int depth = 0) {
    using Code = lir_schema::expr::Code;
    if (dest.empty() || !val || depth > 8) return;
    sink(dest, val);
    if (val.kind() == Code::StructLit) {
        lir_view::EStructLitView{val}.each_field(
            [&](std::string_view fname, lir_view::ExprRef fv) {
                if (!fname.empty())
                    each_ref_store(dest + "." + std::string(fname), fv, sink, depth + 1);
            });
        return;
    }
    // D1 round 13 / P0-P1: an ENUM PAYLOAD and a TUPLE ELEMENT are stored
    // places exactly like a struct field, spelled with the payload/element
    // INDEX (the numeric segment `collect_ref_sources_paths`' TupleLit arm
    // already mints in the §B6 channel — a source field name can never be a
    // numeral, so the two segment alphabets cannot collide). Without them
    // `E::Some(&mut vs)` and `(&mut vs, 0)` record no place at all, and every
    // consumer downstream — the pattern binding of P0b, the `t.0` read of P1
    // — resolves to nothing.
    //
    // An ARRAY LITERAL is NOT enumerated per element here, and that is the
    // decision the dynamic index forces: `arr[i]` cannot be named, so an
    // element key could never be read back for a dynamic index and could not
    // be retracted element-wise by the flow-sensitive `set()`. Its edge is
    // keyed at the ARRAY PLACE itself — the `sink(dest, val)` above, with the
    // ArrLit as the value — and `ref_source_places`' ArrLit arm unions the
    // elements' sources onto it. Whole-container granularity, in both
    // directions, exactly as the §B6 channel already does.
    if (val.kind() == Code::TupleLit) {
        uint32_t i = 0;
        lir_view::ETupleLitView{val}.each_elem(
            [&](lir_view::ExprRef ev) {
                each_ref_store(dest + "." + std::to_string(i++), ev, sink, depth + 1);
            });
        return;
    }
    if (val.kind() == Code::EnumLitData) {
        uint32_t i = 0;
        lir_view::EEnumLitDataView{val}.each_payload(
            [&](lir_view::ExprRef pv) {
                each_ref_store(dest + "." + std::to_string(i++), pv, sink, depth + 1);
            });
        return;
    }
}
// The same enumeration MINUS the destination itself, for the callers that
// apply a different (declared-type) gate to the binding than to its places.
template <class Sink>
static void each_nested_ref_store(const std::string& dest, lir_view::ExprRef val,
                                  Sink&& sink) {
    if (dest.empty() || !val) return;
    // The SAME enumeration with the destination itself filtered out, rather
    // than a second (StructLit-only) copy of its first step — that copy is
    // what kept every aggregate kind `each_ref_store` learns from reaching
    // this door (D1 round 13 / P0: the enum-payload place).
    each_ref_store(dest, val,
        [&](const std::string& place, lir_view::ExprRef v) {
            if (place != dest) sink(place, v);
        });
}

#include "borrow_flow_summary.inc"

// ── BorrowChecker ───────────────────────────────────────────────────────────

class BorrowChecker {
    SemaResult&          diags_;
    std::string          fn_name_;
    const lir::LProgram& prog_;
    const TypeSets&      ts_;
    const FnIndex&       fn_index_;

    StateMap                 states_;
    // Phase-1 string-interning migration — accessor chokepoint over states_.
    // STEP 1 (this form): everything routes by NAME onto states_ (the `slot`
    // argument is accepted but ignored), so converting access sites to these
    // methods is behaviour-preserving and can proceed incrementally (converted
    // and not-yet-converted sites hit the same map). STEP 3 will replace
    // states_ with a hybrid {vector<VarState> by slot + name fallback} and
    // rewrite ONLY these methods + the branch-merge sites — the call sites stay.
    static constexpr uint32_t NO_SLOT = VarStore::NO_SLOT;
    VarState* var_find(uint32_t slot, std::string_view name) {
        return states_.find(slot, name);
    }
    const VarState* var_find(uint32_t slot, std::string_view name) const {
        return states_.find(slot, name);
    }
    bool var_has(uint32_t slot, std::string_view name) const {
        return states_.has(slot, name);
    }
    VarState& var_at(uint32_t slot, std::string_view name) {
        return states_.at(slot, name);
    }
    void var_erase(uint32_t slot, std::string_view name) {
        states_.erase(slot, name);
    }
    std::vector<ScopeFrame>  scopes_;
    // Phase 4: provenance tracking for reference-typed variables.
    ProvMap                              prov_;
    // #86 MISS 1: the DECLARED type of each local binding, recorded at its
    // `let`. VarState carries ownership/borrow state but no type, and the
    // mutation sites (FieldWrite / TupleWrite / the out-param deposit) know
    // only the receiver's NAME — they need the holder's type to ask
    // type_may_carry_borrow / type_is_residency_exempt. Types do not change
    // over a binding's life, so this map is NOT part of the branch
    // save/restore that `prov_` takes part in.
    std::unordered_map<std::string, TypeRef> holder_ty_;
    // H1 / round 8: THE ref-provenance graph of the checking pass. Keyed by
    // dotted PLACE, FLOW-SENSITIVE (a non-reborrow write retracts). Same store
    // and same resolve as the pre-pass's `prescan_refs_` and the summarizer's
    // `alias_` — see RefGraph.
    RefGraph reborrow_of_;
    // D1 round 13 / P2: which recorded places hold a MUTABLE reborrow. The
    // graph itself is kind-blind on purpose (a shared reborrow is as
    // load-bearing as a mut one for the live-range question — round 7 / R7a
    // rule 2), but a DEPOSIT is not: a callee can only write through a `&mut`.
    // Membership is a hint about the value LAST stored at a place; the live
    // graph still decides whether the place exists at all, so a retraction
    // needs no mirror here.
    std::unordered_set<std::string> reborrow_mut_;
    // H4: closure binding -> its CAPTURE names. A closure's provenance lives in
    // its captures, and the captures are not operands of the call, so the call
    // site cannot reach them without this. See closure_caps_of.
    std::unordered_map<std::string, std::vector<std::string>> closure_caps_;
    std::unordered_set<std::string>      param_names_;
    std::unordered_set<std::string>      param_byval_;   // census: by-VALUE params (not &/&mut/*)
    // Round F/B scaffolding — see src/compiler/PROBES.md.
    std::unordered_set<std::string>      closure_param_names_;
    std::unordered_set<std::string>      closure_body_decls_;
    // F-1: the SCOPE FRAME each closure parameter was declared in. The name
    // alone is not the binding — see names_live_closure_param.
    std::unordered_map<std::string, size_t> closure_param_frame_;
    // Params whose referent OUTLIVES the call — reference params and
    // borrow-carrying value params (their borrow points at caller data). A
    // borrow of such a param is safe to return. A BY-VALUE owned param (not in
    // this set) is dropped at return exactly like a local, so a borrow of it
    // must NOT escape (else use-after-free).
    std::unordered_set<std::string>      outliving_params_;
    // PROBE capretsc's CARRIER. The capture names of the closure body being
    // walked. `outliving_params_` answers "does this name outlive the CALL",
    // and three channels read it (escape/dangling, retained-provenance, the
    // return check); depositing captures there — cause B — un-refuses four
    // pinned `-L bc -L fail` fixtures. The fact the RETURN check needs is
    // narrower: "this name is a capture of the closure whose body we are in".
    // Written unconditionally (a few strings per closure), read only under
    // the probe, and at ONE site.
    std::unordered_set<std::string>      closure_capture_names_;
    // Type-param names with an explicit `Copy` bound (per current fn) — a bare
    // TypeVar not in this set is move-classified (Rust generic-body semantics).
    std::unordered_set<std::string>      copy_tvs_;
    // Set transiently while recording a method-RESULT reborrow (`&mut v[i]` =
    // AddrOfTemp(Deref(MethodCall index_mut))): the OUTER `&mut` is the
    // authoritative borrow mutability. method_self_kind can't always resolve
    // the desugared index_mut (returns 0 ⇒ would record a SHARED borrow ⇒ two
    // `&mut v[i]` alias undetected). The MethodCall recorder ORs this in.
    bool                                 reborrow_force_mut_ = false;
    // THE BASE OF A SLICE VIEW. The array→slice coercion is borrow-forming
    // (take_ref_borrows' SliceLit arm says so and delegates to record it), but
    // its base has two spellings: `&arr` -> AddrOf, which the AddrOf arm always
    // records, and the range desugar `a[0..2]` -> Call(slice_get_range,
    // [SliceLit{AddrOfTemp(VarRef a)}, lo, hi]), whose base is an AddrOfTemp
    // over a WHOLE local — path "", no index — so both guards of the AddrOfTemp
    // decomposition miss and the delegation records nothing. This flag carries
    // the one fact the callee cannot see: the AddrOfTemp about to be walked is
    // the base of a VIEW.
    // ⚠ "RECORD A WHOLE-ROOT BORROW WHENEVER THE PATH IS EMPTY" WAS MEASURED
    // AND RED THE STDLIB (iter_min, iter_max: `it.next()` in a loop conflicted
    // with itself). A plain AddrOfTemp(VarRef) is ALSO every Call-lowered
    // method autoref — sema's finish_generic_call/materialize_recv_ref passes
    // the autoref'd receiver as arg 0 of a plain Call, and the Call arm below
    // feeds every ref-kind arg back into this walker with the caller's holder.
    // A method autoref is never wrapped in a SliceLit, which is exactly why the
    // marker is set there and nowhere else.
    bool                                 slice_view_base_ = false;
    // Escape-analysis callee lookups (`result_borrows_self` / `method_self_kind`)
    // go through the shared, program-global `fn_index_` (built once in
    // borrow_check()).
    // B82: depth of nested call-arg evaluation. While >0, new &mut borrows
    // are taken as reservations (don't conflict with shared reads of the
    // same target during the remaining arg evaluation).
    int                                  in_call_args_ = 0;
    // Set while visiting a PLACE-FORMING sub-expression: the inner of an
    // AddrOfTemp (`&place`) or the receiver/base of a projection (`x.f`, `x[i]`,
    // `recv.method()` — see visit_place_base). A VarRef/FieldRead reached here
    // is part of naming a place, not a value-use, so the field-borrow value-use
    // checks (#2 whole-read, #3 field-read) must not fire — the conflict was
    // already decided at the borrow/projection site (else double-report, or a
    // spurious whole-`w` conflict when walking `w.buf`'s base).
    bool                                 in_addr_source_ = false;
    // Set while visiting the RHS of a `let __dst_N = rhs;` — the temp sema
    // materialises for a pattern-destructuring `let`. Read by
    // deref_move_exempt only; see the exemption there for why the position
    // cannot answer E0507.
    bool                                 in_destructure_temp_ = false;
    // param name → lifetime annotation of that param's type (e.g. "'a", "")
    std::unordered_map<std::string, std::string> param_lifetimes_;
    // B86: per-param inner-struct lifetime_args. Populated for ref-typed
    // params whose pointee is a Struct/Enum carrying explicit lt_args.
    // Used by check_return_value to accept `self: &Self` -> &'a T when 'a
    // is one of Self's struct lt_args (Self<'a> shape).
    std::unordered_map<std::string, std::vector<std::string>> param_inner_lifetimes_;
    // B87 dropck: for each binding whose type is a Drop-having struct with
    // lifetime params, record which local vars it borrows from at
    // construction. On scope-pop, if any source local was declared in the
    // exiting scope while the binding still lives, reject — the binding's
    // Drop will run after the source dies.
    std::unordered_map<std::string, std::vector<RefSrc>>
        dropck_borrow_sources_;
    // B87: line at which each dropck-relevant binding was last bound, for
    // diagnostic reporting.
    std::unordered_map<std::string, uint32_t> dropck_binding_line_;
    // B87 PER-PATH: `dropck_borrow_sources_` is keyed on the ROOT NAME, so a
    // FIELD-wise deposit (`w.a = &x`) can only REPLACE the whole record — and
    // MEASURED, a second write to a DIFFERENT field then erases the first
    // field's source. Two field writes, one frame, opposite order: identical
    // fire counts, opposite verdicts (sandbox mvr_replace_loses /
    // mvr_order_swapped). This map restores the missing dimension — sources
    // keyed by FIELD PATH under the root — so a write REPLACES its own path
    // and MERGES across paths. `dropck_borrow_sources_[root]` stays the flat
    // union that pop_scope reads, rebuilt from here on every field deposit.
    // ⚠ A WHOLE-VALUE write re-owns the binding and must therefore CLEAR the
    // per-path record; the Let and Assign arms do exactly that. Without it
    // `w.a = &inner; w = W{a:&o};` would keep `inner` and refuse legally.
    std::unordered_map<std::string, std::map<std::string, std::vector<RefSrc>>>
        dropck_field_srcs_;
    // §B6 (NLL scope lifetime, rustc E0597): for EVERY reference / borrow-
    // carrying binding, the LOCAL variables it borrows from + the line of the
    // borrow. Generalises dropck_borrow_sources_ (which is gated on a Drop
    // impl) to all borrows. On scope-pop, a binding that OUTLIVES one of its
    // source locals is recorded in `dangling_`; the FIRST subsequent use of it
    // is rejected (E0597) — matching NLL (a stored borrow that is never used
    // after its referent dies is fine; only the use is the error).
    // (D1 round 10, J0: `rehomed_slots_`/`rehomed_names_` lived here to restrict
    //  the branch-join loan merge to Door B's own targets. The restriction was
    //  measured wrong-sided and deleted, and with it the only consumer of the
    //  two sets, so the sets are gone too — see merge_loans.)
    std::string                     pending_esc_holder_;
    // F6 (D1 round 3) — keyed by PLACE, not by binding. Same lesson as F5:
    // a per-binding key standing in for a per-place identity. `let mut w =
    // Wrap { b: B { p: &z } }; w.b = c.mk();` recorded ref_borrow_sources_["w"]
    // = {z} at the `let` and MERGED at the field write, so "the source of w.b
    // was replaced" was structurally inexpressible: `z` stayed a source of `w`,
    // place_write_loans re-rooted the c-loan onto `z` as a co-holder, and a
    // later read of `z` — the loan's TARGET — kept the loan on `c` alive.
    // Keys are now dotted places ("w", "w.b", "w.b.p"); a write to a place
    // erases that place AND everything under it, and every consumer that wants
    // a binding's sources unions over the subtree (ref_sources_under).
    std::unordered_map<std::string, std::vector<RefSrc>>      ref_borrow_sources_;
    std::unordered_map<std::string, uint32_t>                 ref_borrow_line_;
    // S5-D4: fire count of the through-a-reference provenance arm in
    // collect_ref_sources_paths' AddrOfTemp case. A rule whose branch never
    // executes is a green over nothing (MEMORY: "GREEN over a branch that
    // NEVER EXECUTED"), so the number is dumpable: LOGOS_DUMP_BC_THRUREF=1.
    // STATIC: one checker object is constructed per function and thrown away,
    // so a per-instance counter would only ever read 0 or 1. The number that
    // answers "did this branch execute over the corpus" is the program-wide sum.
public:
    static inline uint64_t thru_ref_prov_fired_ = 0;
    // #86 MISS 1 — fire count of the MUTATION-side holder-provenance record
    // (assign / field write / tuple write / container deposit). Read by
    // LOGOS_DUMP_BC_HOLDERPROV; the arm is pinned by
    // tests/logos/fail/bc_esc_holder_assign_*_dangle.
    static inline uint64_t holder_escape_prov_fired_ = 0;
    // …and per DOOR, so a dead door is visible without re-reading the trace:
    // 0=assign 1=derefwrite 2=outparam 3=recvstore.
    static inline uint64_t holder_escape_prov_by_door_[4] = {0, 0, 0, 0};
private:
    struct DanglingRef { std::string source; uint32_t borrow_line; };
    std::unordered_map<std::string, DanglingRef>              dangling_;
    // Declared lifetime parameters of the current function (e.g. ["'a", "'b"]).
    std::vector<std::string>             fn_lifetime_params_;
    // B66: outlives graph from fn.lifetime_outlives — used to accept the
    // return-lifetime check when an explicit `where 'src: 'ret` (or
    // transitive) covers the case.
    std::unordered_map<std::string, std::unordered_set<std::string>> outlives_adj_;
    // Phase 3/4: return type of current function.
    TypeRef         ret_type_ = nullptr;
    // CFG divergence flag. Set true when a stmt diverges (Return/Break/
    // Continue). If/Match merges check it on each branch and skip merging
    // moves from a diverged arm — otherwise an early-return inside one arm
    // would pollute the join with moves that the OTHER arm never sees.
    bool cur_diverged_ = false;
    // logos-core 2.1 (consumer): optional region_infer for declared-
    // lifetime outlives queries. When non-null, the named-region BFS
    // replaces the string-graph BFS at return-value lifetime checks
    // (both share `fn.lifetime_outlives` as the source). Null in
    // exclusivity-only / generic-template mode (region_infer is
    // imprecise on TypeVars).
    const RegionInferer* ri_ = nullptr;
    // ── D1 round 6 / G0: THE ALIAS SET THE NLL PRE-PASS MUST SEE ──────────
    // Flow-INSENSITIVE union of `place -> referent` reborrow edges over the
    // whole body, keyed by dotted place exactly like `reborrow_of_`. It exists
    // only to EXTEND lifetimes: a use of a reborrow (`r`, or a struct holding
    // one, `h`) is a use of the referent, because the referent stays borrowed
    // for as long as the reborrow may be used. Without it, re-homing a loan
    // onto the referent SHORTENS the loan — the referent's own name may never
    // appear again — and `h.r.push(c.mk()); c.bump(); *h.r.get(0).p` flips from
    // refuse to admit. Measured: it did, and the plain-local twin
    // (`let r = &mut vs; r.push(c.mk()); c.bump(); *r.get(0).p`) had that same
    // hole ALREADY, from round 5's binding-keyed re-home. One rule closes both.
    // Union (never retracted) is the safe direction here: it can only extend.
    // Round 8: same store, MONOTONE policy (see RefGraph).
    RefGraph reborrow_prescan_;
    // ── D1 round 6 / G1: WHICH FUNCTION DOES THIS FN POINTER NAME ─────────
    // A fn-pointer local assigned EXACTLY ONCE from a named function resolves
    // statically; anything else (reassigned, or arriving as a parameter) is
    // unresolvable and takes the conservative route. Both maps are built by
    // the same whole-body pre-pass as reborrow_prescan_, so the answer does
    // not depend on where in the body the call sits relative to the binding —
    // and a binding written in two branches is `multi` no matter which branch
    // the walk is in. The stored value is already the MANGLED symbol: sema
    // lowers `let g: fn(&C) -> B = mkd;` to a VarRef whose name IS
    // `pkg$mkd__f__ref_C`, the exact string `flow_of_call` resolves.
    std::unordered_map<std::string, std::string> fnptr_sym_;
    std::unordered_set<std::string>              fnptr_multi_;
    // Phase 9 (NLL): max line at which each local variable is read.
    // Populated by scan_uses_block over the entire fn body before checking.
    // A borrow with non-empty holder is released once cur_line >= last_use_line_[holder].
    // #75: the value is no longer a LINE but a PROGRAM POINT — the pair
    // (line, per-line statement ordinal) packed lexicographically into a
    // uint64 by `stmt_point`. Statements on DISTINCT lines keep ordinal 0 and
    // compare exactly as their lines did (bit-identical); statements SHARING a
    // physical line are what the ordinal separates, which is the whole hole.
    std::unordered_map<std::string, uint64_t> last_use_line_;
    // F5 (identity, not key): the same three facts split by what the use site
    // could tell us about WHICH binding was used.
    //   last_use_slot_      — uses through a VarRef, which carries the dense
    //                         binding slot. Discriminates shadowed bindings.
    //   last_use_unslotted_ — uses through a site with only a NAME (AddrOf,
    //                         ClosureBox captures, the *Write receivers).
    //                         Charged to EVERY binding of that name: we cannot
    //                         tell them apart, so we stay conservative.
    //   last_use_line_      — max over both. Used when the HOLDER itself has
    //                         no slot, i.e. the pre-F5 behaviour, unchanged.
    std::unordered_map<uint32_t,    uint64_t> last_use_slot_;
    std::unordered_map<std::string, uint64_t> last_use_unslotted_;
    // Slot of the CURRENTLY-live binding of each name, so a loan can capture
    // its holder's identity at record time (declare_var runs AFTER the RHS is
    // walked, so the `let` arm pre-seeds this).
    std::unordered_map<std::string, uint32_t> cur_slot_of_;
    // Max statement line visited so far — the NLL release point after a
    // COMPOUND statement (its uses extend past its start line).
    uint64_t max_line_seen_ = 0;   // #75: a PROGRAM POINT, see stmt_point

    // ── #75: (line, ordinal) program points ────────────────────────────────
    //
    // THE DEFECT this closes: liveness was keyed on the SOURCE LINE, and
    // release_dead_borrows released on `lu <= cur_line`, so two statements on
    // ONE physical line released the first one's loans before the second one's
    // conflicting use — every metaprog emitter that pushes a module as a
    // single-line string was thereby exempt from ALL exclusivity checking.
    //
    // The key becomes lexicographic (line, ordinal). The ordinal is assigned
    // LAZILY and MEMOISED BY STATEMENT ADDRESS (RefBase::addr() is the stable
    // node identity — segments never move), so every consumer of a given
    // statement's point gets the SAME value BY CONSTRUCTION: the two
    // scan_uses_block pre-passes (run_fn calls it twice for the G0 alias set)
    // and the checker walk share this one memo. No traversal-order agreement
    // has to be argued — there is only one assignment, and it is the first
    // query's.
    // ⚠ THE ORDINAL FIELD IS 32 BITS, NOT 20, AND THAT IS THE WHOLE CONTENT OF
    // THIS LINE. At 20 bits the ordinal SATURATED at 1,048,575 and every
    // statement past that on one physical line collapsed back to one point —
    // i.e. this fix silently re-opened the exact hole it exists to close, in
    // exactly the channel it exists for: an emitter that pushes a module as ONE
    // line puts its ENTIRE statement count on line 1. Not hypothetical, and not
    // expensive to reach — the round's own verify built the discriminating pair
    // (`sandbox/verify75/big_sub.logos`, 1,000,004 statements on one line,
    // REFUSED; `big_sat.logos`, 1,100,004, ADMITTED), which straddles the cap
    // and so separates saturation from any generic large-function bail-out.
    // A line number is `uint32_t`, so `(uint64(line) << 32) | ord` is EXACT:
    // the point domain is the full cross product and nothing is packed away.
    // The saturation guard is kept below because a guard that cannot fire is
    // still cheaper than a carry into the line field if it ever could.
    static constexpr uint32_t PT_ORD_BITS = 32;
    static constexpr uint32_t PT_ORD_MAX  = 0xFFFFFFFFu;
    std::unordered_map<const void*, uint64_t> stmt_pt_;    // stmt addr -> point
    std::unordered_map<uint32_t, uint32_t>    line_ord_;   // line -> next ordinal
    uint64_t stmt_point(lir_view::StmtRef sr) {
        if (!sr) return 0;
        const void* k = sr.addr();
        if (auto it = stmt_pt_.find(k); it != stmt_pt_.end()) return it->second;
        uint32_t ln  = lir_view::stmt_line(sr);
        uint32_t& nx = line_ord_[ln];
        uint32_t ord = nx;
        // Saturate rather than carry into the line field: an overflowing
        // ordinal must never make a point compare as a LATER LINE.
        if (nx < PT_ORD_MAX) ++nx;
        uint64_t pt = (uint64_t(ln) << PT_ORD_BITS) | uint64_t(ord);
        stmt_pt_.emplace(k, pt);
        return pt;
    }

    // Loop dataflow: per-enclosing-loop capture of the move-state at each
    // `break` (flows to AFTER the loop) and `continue` (flows to the loop BACK
    // EDGE). visit_loop_body consumes these to (a) reject a value moved on one
    // iteration and reused on the next, and (b) keep such moves live in the
    // after-loop state — the pre-fix code forgot moves made on a break/continue
    // path (they were dropped like a `return`). Labeled break/continue target
    // the matching frame; unlabeled ones target the innermost.
    struct LoopFrame {
        std::string           label;
        std::vector<StateMap> continue_states;
        std::vector<StateMap> break_states;
        // D1 round 12 / A1: the loop's BREAK SLOT (`__loop_val_N`, empty unless
        // the loop is used as an expression). `break v` deposits into it, and
        // that deposit is a REBORROW record — see the Break arm in visit_stmt.
        std::string           break_slot;
        // D1 residuals / r11 (task #51): # of scope frames that SURVIVE this
        // loop (set to scopes_.size() before the body's push_scope). The
        // break/continue snapshots must carry the state that flows PAST the
        // exit — i.e. after the loop's own scopes unwind — so loop-local loans
        // are release-simulated against this baseline (loop_exit_snapshot).
        size_t                outer_scope_count = 0;
    };
    std::vector<LoopFrame> loop_stack_;
    // Set during the loop dataflow's dry-run pass (pass 1), which recomputes the
    // back-edge move-state and must not emit duplicate diagnostics — the
    // authoritative pass 2 reports.
    bool suppress_reports_ = false;
    LoopFrame* loop_target(std::string_view label) {
        if (loop_stack_.empty()) return nullptr;
        if (label.empty()) return &loop_stack_.back();
        for (auto it = loop_stack_.rbegin(); it != loop_stack_.rend(); ++it)
            if (it->label == label) return &*it;
        return &loop_stack_.back();
    }
    // Mark in `dst` every outer binding (present in `base`) that is `moved` in
    // `src`, carrying its move line.
    // ── THE THIRD KIND OF STATE AT A JOIN ────────────────────────────────
    //
    // A join in this checker carries THREE kinds of variable state, and until
    // 2026-08-28 it carried two:
    //
    //   kind                     if-stmt / if-expr   match arms      loop edges
    //   whole-value `moved`      merge_moves         st.moved fold   here
    //   loans (counters, field   merge_loans         merge_loans     merge_loans
    //     borrow maps)
    //   PARTIAL move             NOT MERGED          arm 1 only,     HERE, NEW
    //     (`moved_fields`)                           by accident
    //
    // "arm 1 only, by accident" is literal: the match join seeds `merged_s`
    // with a WHOLE VarState copy of the first arm and then folds arms 2..n by
    // `st.moved` alone, so a partial move in arm 1 survives and the same
    // program with the arms swapped compiles — the J0 story, for a third kind
    // of state. That asymmetry is NOT fixed here: priced at CEILING 2 (i.e.
    // exactly what the pattern-site record alone already closes, probe
    // `mfjoinarm`), it buys nothing measurable, and a second mechanism in this
    // change would make the row delta unattributable.
    //
    // THE LOOP EDGE IS THE ONE THAT PAYS. Probe `mfjoinloop`: CEILING 6 / COST
    // 0, against `mfjoinarm`'s 2 and `mfjoinbare`'s 0 — the last of those being
    // the join with NO producer armed, 25,096 fires and not one row, which is
    // how we know the four extra rows are bought by this edge and not by the
    // existing FieldRead producer.
    //
    // ⚠ THE DIRECTION IS UNION, AND IT IS THE REFUSE-MORE DIRECTION. A field
    // moved on iteration 1 is moved on entry to iteration 2 (rustc: "value
    // moved here, in previous iteration of loop"). RE-INITIALISATION needs no
    // subtraction on this path: `erase_reinit` runs at the two WRITE sites
    // inside the body, so a body that refills the field arrives at the bottom
    // with an already-empty map — measured, and the fixture
    // tests/logos/pass/bc_mfjoin_loop_partial_move_legal says which of its ten
    // functions CARRIED and which had nothing to carry, because both answers
    // are evidence and they are different evidence. `field_reinit_in_body` and
    // `disjoint_fields_each_refilled` carry NOTHING; `whole_reassign_at_top`
    // DOES carry `h.a` onto the back edge and still compiles, because the top
    // of the body reassigns `h` and a whole-value reassign resets the VarState
    // outright. The union is `insert`, not `operator[]`: an entry already
    // present keeps its ORIGINAL move line, which is the line a reader wants.
    //
    // ⚠ A LEGAL PROGRAM THAT CARRIES TO THE BACK EDGE AND IS THEN READ DOES
    // NOT EXIST for the pattern producer, and that is a property of the rule
    // rather than of the corpus: the pattern site is itself the reader, so a
    // back-edge carry that reaches a second read IS the upstream error. The
    // legal carries are therefore all loop-EXIT ones (`break_after_move`,
    // `move_then_break_in_arm`) or back-edge carries the body clears before
    // reading (`whole_reassign_at_top`). Stated so nobody reads the shape of
    // the fixture file as a gap in it.
    void loop_propagate_moves(StateMap& dst, StateMap& src, const StateMap& base) {
        src.for_each([&](uint32_t slot, std::string_view name, VarState& st) {
            std::unordered_map<std::string, uint32_t> keep;
            if (dst.has_id(slot, name)) keep = dst.at_id(slot, name).moved_fields;
            if (st.moved && base.has_id(slot, name)) dst.at_id(slot, name) = st;
            if (base.has_id(slot, name) && dst.has_id(slot, name)) {
                auto& b = dst.at_id(slot, name);
                for (auto& kv : keep) b.moved_fields.insert(kv);
                for (auto& kv : st.moved_fields)
                    if (b.moved_fields.insert(kv).second &&
                        std::getenv("LOGOS_MFJ_TRACE"))
                        fprintf(stderr,
                                "[mfj] loop edge carried '%.*s.%s' (moved line %u)\n",
                                (int)name.size(), name.data(),
                                kv.first.c_str(), kv.second);
            }
        });
    }

    // ── D1 residuals / r11 (task #51): the break/continue snapshot must be ──
    // the state AFTER the loop's scopes unwind. `Code::Break` snapshotted
    // `states_` VERBATIM, so a loan whose holder is loop-body-LOCAL (`while …
    // { let r = &mut x; if p { break; } }`) crossed the loop-exit merge with
    // its counter raised — while pop_scope had already destroyed the only
    // BorrowRecord that could ever release it. merge_loans copies raw counters
    // with no holder identity, so the loan became immortal: `let s = &x` after
    // the loop was refused (imported pass/nll/label-borrow-in-labeled*, red
    // since r11; same defect at the back edge re-refused iteration 2's
    // re-borrow). Rust semantics: a `break`/`continue` drops the body's
    // locals before the edge is taken.
    //
    // This is pop_scope's OWN release arithmetic and its OWN `escapes` test
    // (Door B re-home), applied to a COPY of states_ over every loop-local
    // frame at once, with "outer" = names declared in the frames that survive
    // the loop ([0, outer_scope_count)) plus pending_esc_holder_. A loan whose
    // holder (or any co_holder) is OUTER keeps its counter — exactly the r11
    // fail witnesses (`bc_d1r11_x0_*`, holder `vs` declared before the loop) —
    // so the J0 loop-exit/back-edge merges those witnesses need are unchanged.
    // State-only, no reports: runs identically in BOTH passes (unlike the
    // re-home, which is gated on !suppress_reports_ for reporting reasons).
    // Live `states_` and all in-body checking are untouched.
    StateMap loop_exit_snapshot(size_t outer_scope_count) const {
        StateMap snap = states_;
        if (scopes_.size() <= outer_scope_count) return snap;
        std::unordered_set<std::string> outer;
        for (size_t fi = 0; fi < outer_scope_count; ++fi)
            outer.insert(scopes_[fi].declared.begin(),
                         scopes_[fi].declared.end());
        if (!pending_esc_holder_.empty()) outer.insert(pending_esc_holder_);
        auto escapes = [&](const auto& rec) {
            // `loopexit_coholder` hoisted to the lambda ENTRY: inside the
            // co_holders loop it reported NEVER FIRED, which does not
            // distinguish "escapes() is never called in the ledger
            // population" from "co_holders is always empty". Here the
            // count IS the escapes() arrival count.
            bool lec = logos::probe::on("loopexit_coholder");
            if (rec.holder.empty()) return false;   // lexical: dies at pop
            if (outer.count(rec.holder)) return true;
            for (auto& h : rec.co_holders)
                // CEILING PROBE `loopexit_coholder` — the co-holder channel
                // (what place_write_loans' and visit_stmt's reroot write) is
                // consulted here and its `return true` has count 0 in 8060
                // runs: no loan has ever escaped a loop through it. escapes()
                // is called 194 times total, so 194 IS the population, not a
                // sample. `escapes` true means the release is SKIPPED —
                // strictly fewer releases.
                // ⚠ UNMEASURABLE BY THE LEDGER — MEASURED 2026-08-28: with the
                // probe hoisted to the lambda entry (inside the co_holders
                // loop it reported NEVER FIRED, which cannot tell "escapes()
                // is never called" from "co_holders is always empty"), it
                // fired ONCE across the 400 ledger compiles. CEILING 0 over a
                // population of ONE is not a result; COST 0 is not a safety
                // claim. The ledger contains essentially no loop-exit escape
                // decision at all. Funding this needs a hand-written
                // loop-carried co-held borrow first — a program, then a price.
                if (lec || outer.count(h)) return true;
            return false;
        };
        for (size_t fi = outer_scope_count; fi < scopes_.size(); ++fi) {
            auto& frame = scopes_[fi];
            for (auto& br : frame.borrows) {
                if (escapes(br)) continue;
                auto* it = snap.find(br.target_slot, br.target);
                if (it == nullptr) continue;
                if (br.is_mut) {
                    // B82: same order as pop_scope — activated first,
                    // then an outstanding reservation.
                    if (it->mut_borrowed) it->mut_borrowed = false;
                    else if (it->mut_reservations > 0) it->mut_reservations--;
                } else if (it->shared_borrows > 0)
                    --it->shared_borrows;
            }
            for (auto& fb : frame.field_borrows) {
                if (escapes(fb)) continue;
                auto* it = snap.find(fb.target_slot, fb.target);
                if (it == nullptr) continue;
                if (fb.is_mut) it->mut_field_borrows.erase(fb.path);
                else {
                    // Observational zero census, decrement site 1 of 3.
                    // Coverage 2026-08-27: this else-branch ran 0 times in
                    // 8060 runs, so these two are expected NEVER FIRED.
                    auto sit = it->shared_field_borrows.find(fb.path);
                    if (sit != it->shared_field_borrows.end()) {
                        if (sit->second <= 0)
                            (void)logos::probe::on("szw_snap_pre0");
                        if (--sit->second <= 0)
                            it->shared_field_borrows.erase(sit);
                        else (void)logos::probe::on("szw_snap_keep");
                    }
                }
            }
        }
        return snap;
    }

    void report(uint32_t line, std::string msg) {
        // Loop dataflow pass-1 dry run: state only, no diagnostics (pass 2 is
        // authoritative — reporting here would duplicate every in-body error).
        if (suppress_reports_) return;
        // P2-10 exclusivity-only mode (Tier 1, generic templates): drop the
        // WHOLE-VALUE use-after-move diagnostic ("use of moved value") — it is
        // redundant with sema's flow `moved_vars_` check and imprecise on
        // generics. But KEEP the PARTIAL-move diagnostics ("use of partially
        // moved value" / "use of moved field") and move-while-borrowed: with
        // bound-aware is_move_type (TypeVar = move unless Copy) the partial-move
        // tracker is now precise on generic bodies, matching Rust's abstract
        // generic check (DIVERGENCES §B1). sema doesn't track field-granular
        // moves, so dropping these would re-open the Tier-1 partial-move hole.
        if (exclusivity_only_ && msg.find("moved") != std::string::npos &&
            msg.find("partially moved") == std::string::npos &&
            msg.find("moved field") == std::string::npos) return;
        Diag d;
        d.level   = Diag::Level::Error;
        d.context = fn_name_;
        d.message = std::move(msg);
        d.line    = line;
        diags_.diags.push_back(std::move(d));
    }

    // ── Scope management ───────────────────────────────────────────────────

    // D3: the next push_scope() opens a BARE `{ … }` statement frame. Set at the
    // ONE site that knows (the Block stmt arm) and consumed by push_scope, so
    // no other frame kind can acquire the flag by accident.
    // A `return` INSIDE A WALKED CLOSURE BODY returns from the CLOSURE, not
    // from the enclosing fn, so the enclosing fn's return contract must not be
    // asked about it. Without this, `fn foo(x: &i64) -> &i64 { let f = |y:
    // &i64| -> &i64 { return y; }; return x; }` — legal, and the closure is
    // never even CALLED — was refused with "return reference must derive from
    // 'x'": the closure's `return y` was checked against `foo`'s elision rule.
    // ⚠ THIS WAS INVISIBLE TO THE 478-TEST COST CORPUS, which scored the whole
    // body walk at COST 0. It was found by CONSTRUCTING the program, and it
    // was buying two ledger rows (nll/issue-48697--b, --t16) with a refusal
    // that is wrong about WHY — one of them by refusing a `return f(x)` that
    // does derive from `x`. Those two rows are NOT closed by this round.
    // The closure's OWN return contract is simply not modelled here; skipping
    // the check is the UNDER-refusing direction, which is the safe one.
    bool in_closure_body_ = false;

    // ONE body walk, called from BOTH ClosureBox arms. The class here is "a
    // ClosureBox arm", it has exactly two members, and a walk that drifts
    // between them is a bug shape this file has met under other names.
    //
    // ⚠ KNOWN PRECISION REGRESSION, PAID KNOWINGLY AND RECORDED HERE. Because
    // the walk runs BEFORE the capture loop, a conflict between TWO closures
    // over one root is now reported from the SECOND closure's BODY ("cannot
    // assign to 'x' while it is mutably borrowed", E0506-shaped) rather than
    // from its CAPTURE ("cannot borrow 'x' as mutable: already mutably
    // borrowed", E0499-shaped, which is what upstream says). Four imported
    // pins were re-pinned for it: borrowck-closures-two-mut, -two-mut-fail,
    // borrowck-autoref-3261, and closure-access-spans--b-closure-mut-capture-
    // conflict. THE VERDICT IS UNCHANGED — all four still refuse, and both
    // messages name the same real conflict — but the canonical one is the
    // capture-site message and this round does not produce it.
    // THE FIX IS TO SPELL THE EXEMPTION AS A SUPPRESSION RATHER THAN AS AN
    // ORDER: walk AFTER the capture loop with the closure's OWN capture loans
    // exempted, so the capture-site diagnostic fires first and the body still
    // cannot conflict with itself. That needs a holder-keyed filter through
    // the conflict emitters, it is a bigger change than this one, and it must
    // be RE-PRICED — the 12 rows this round closed were measured under the
    // ORDER, and a different exemption is a different mechanism.
    void walk_closure_body(lir_view::EClosureBoxView cbv) {
        // A `move` closure OWNS its captures, so its body operates on env
        // COPIES and has no business being read in the ENCLOSING frame's
        // namespace. MEASURED: walking both arms and walking only the non-move
        // arm price IDENTICALLY, so nothing is bought by walking a `move` body
        // and the narrower rule is the one that lands.
        // PROBE capmovewalk: a `move` closure's body is NEVER WALKED at all,
        // so issue-48238 (`move || -> &i64 { return use_val(&orig); }`) is not
        // an under-refusal by a permissive verdict — it is ZERO ARRIVALS
        // (rule 16). And the cause-B exemption below is INVERTED for a move
        // closure: a MOVED capture is a LOCAL of the closure, not of the
        // enclosing frame. Numbers in src/compiler/PROBES.md.
        bool probe_mv_ = cbv.is_move();
        if (probe_mv_ && !logos::probe::on("capmovewalk")) return;
        auto cbb = cbv.body();
        if (!cbb) return;
        auto saved_params     = param_names_;
        auto saved_outliving  = outliving_params_;
        auto saved_capnames_  = closure_capture_names_;
        auto saved_cpn        = closure_param_names_;
        auto saved_cpf        = closure_param_frame_;
        auto saved_cbd        = closure_body_decls_;
        bool saved_icb        = in_closure_body_;
        TypeRef saved_ret     = ret_type_;
        // ── LANDED 2026-08-31, and the exemption is SCOPED ─────────────────
        // What lands is `capretsc`: the gate on, `ret_type_` and
        // `param_lifetimes_` rebound from the CLOSURE'S OWN signature, and
        // cause B put where ONLY the return check can read it. Measured on
        // build 571876f6ef48a1ed: ceiling 4, cost 0, COST-fail 0 of 1044,
        // stdlib all four layers. `capretcaps` / `capretplt` closed a fifth
        // row (regions-nested-fns-2) and bought it by depositing captures in
        // `outliving_params_`, which un-refused FOUR pinned fail fixtures
        // (escape-argument--b, escape-upvar-nested, escape-upvar-ref,
        // regions-nested-fns). Numbers and the retirements in
        // src/compiler/PROBES.md, round 2026-08-31h.
        TypeRef saved_ret_c = ret_type_;   (void)saved_ret_c;
        auto saved_plt = param_lifetimes_;
        // The closure's OWN return type. `ret_type_` used to be set exactly
        // once, at the enclosing FUNCTION'S entry, so every return inside a
        // closure body was judged against the function's return type.
        if (TypeRef crt = cbv.ret_type(prog_.type_pool.impl())) ret_type_ = crt;
        // CAUSE B, SCOPED. A non-move closure's capture lives in the
        // ENCLOSING frame and outlives the closure by construction — which is
        // what `outliving_params_` describes (#138). But three channels read
        // that set (escape/dangling, retained provenance, the return check),
        // and only the third asked the question. So the fact goes in a set of
        // its own. INVERTED for a `move` closure: a moved capture is the
        // closure's own local (and a move body is not walked at all today —
        // see the `probe_mv_` gate above).
        if (!probe_mv_)
            cbv.each_capture_name([&](std::string_view cn) {
                if (!cn.empty()) closure_capture_names_.insert(std::string(cn));
            });
        {
            // CAUSE A. Clearing the enclosing fn's param lifetimes is only
            // half of it: without the REBIND a closure signature's own
            // contract is checked against an empty map, so
            //   fn  g(x:&i64) -> &'static i64 { return x; }     REFUSED
            //   let c = |x:&i64| -> &'static i64 { return x; }  admitted
            // one node kind apart. The rule exists and lands on fns; the
            // closure never reached it.
            param_lifetimes_.clear();
            cbv.each_param(prog_.type_pool.impl(),
                           [&](std::string_view pn, TypeRef pt) {
                if (pn.empty() || !is_ref_kind(pt)) return;
                param_lifetimes_[std::string(pn)] =
                    lt_is_minted(TypeRef(pt).lifetime())
                        ? std::string{} : std::string(TypeRef(pt).lifetime());
            });
        }
        in_closure_body_ = true;
        // The body was never scanned either: scan_uses_expr's ClosureBox arm
        // stops at the capture names exactly as the loan channel did, so body
        // locals had no last-use line and NLL could not retire their loans.
        scan_uses_block(cbb);
        push_scope();
        cbv.each_param(prog_.type_pool.impl(),
                       [&](std::string_view pn, TypeRef pt) {
            if (pn.empty()) return;
            std::string nm(pn);
            declare_var(nm, NO_SLOT);
            param_names_.insert(nm);
            closure_param_names_.insert(nm);
            // F-1: WHICH FRAME, not just which word. `visit_block` pushes its
            // own scope for the body, so every body `let` — shadow included —
            // lands strictly deeper than this one.
            if (!scopes_.empty()) closure_param_frame_[nm] = scopes_.size() - 1;
            closure_body_decls_.insert(nm);
            if (is_ref_kind(pt) || is_borrow_carrying_type(pt) ||
                TypeRef(pt).kind() == LogosType::Kind::Ptr)
                outliving_params_.insert(nm);
        });
        visit_block(cbb);
        pop_scope();
        in_closure_body_  = saved_icb;
        ret_type_         = saved_ret;
        param_lifetimes_  = std::move(saved_plt);
        param_names_      = std::move(saved_params);
        outliving_params_ = std::move(saved_outliving);
        closure_capture_names_ = std::move(saved_capnames_);
        closure_param_names_ = std::move(saved_cpn);
        closure_param_frame_ = std::move(saved_cpf);
        closure_body_decls_  = std::move(saved_cbd);
    }

    // PROBE capmoveloan's precondition. Deliberately name-keyed and
    // deliberately over-wide: a ceiling probe may be wrong, and this one is
    // asked only "is ANY loan outstanding on this root".
    bool root_has_live_loan(const std::string& n) const {
        for (const auto& f : scopes_) {
            for (const auto& b : f.borrows)        if (b.target == n) return true;
            for (const auto& fb : f.field_borrows) if (fb.target == n) return true;
        }
        return false;
    }
    bool next_scope_is_bare_block_ = false;
    void push_scope() {
        scopes_.push_back({});
        scopes_.back().bare_block = next_scope_is_bare_block_;
        next_scope_is_bare_block_ = false;
    }

    void pop_scope() {
        if (scopes_.empty()) return;
        {
            // ── D1 round 2, Door B: a loan whose HOLDER outlives this frame ──
            // pop_scope released every record recorded in the frame, on the
            // assumption that a loan's holder is always frame-local. With the
            // D1 holder graph that is false: `let b: B; if z > 0 { b = c.mk();
            // } c.bump(); *b.p` records the loan inside the `if` frame while
            // the holder `b` lives in the enclosing one, so the loan died at
            // the inner `}` and the mutation was admitted. Same for while /
            // for-each bodies and for a block used as a VALUE
            // (`let b: B = { let t: B = c.mk(); t };`).
            //
            // DESIGN CHOICE — RE-HOME, not "release only when all holders die".
            // The two differ in what happens to the record afterwards: pure
            // deferral leaves it in a frame that no longer exists, so nothing
            // would ever release it (a loan that never expires refuses every
            // later mutation — over-refusal, and NLL would be dead for it).
            // Re-homing hands the record to the ENCLOSING frame, where the
            // ordinary NLL rule (release_dead_borrows over holders_last_use)
            // still retires it at its holders' last use and the enclosing
            // pop_scope still releases it. So the loan's LIFETIME becomes what
            // the holder graph says, and no other mechanism changes.
            //
            // The test is on the HOLDER SET, not the target: a record survives
            // this pop iff some holder (holder or a co_holder) is not declared
            // in this frame. A wholly frame-local loan releases exactly as
            // before — which is what keeps the twins refusing/admitting:
            //   Bt1 (same statement at fn top level)      rc=1, unchanged
            //   Bt2 (block with no inner binding)         rc=1, unchanged
            //   K1  (loan rooted at an unrelated local)   rc=0, unchanged
            if (scopes_.size() >= 2 && !suppress_reports_) {
                auto& frame = scopes_.back();
                auto& parent = scopes_[scopes_.size() - 2];
                // "Outer" means declared in a frame that SURVIVES this pop —
                // not merely "absent from this frame". A co-holder declared in
                // an already-popped DEEPER frame (`while { let b = s.batch(&c);
                // let kc = b.keys(); … }` inside a block holding `s`) is gone,
                // and treating it as outer re-homed a loan that must die here:
                // measured as pass/ctr_family_batch_then_mut, the admit twin
                // whose whole job is to prove the batch's borrow ENDS.
                bool any_held = false;
                for (auto& br : frame.borrows)       if (!br.holder.empty()) any_held = true;
                for (auto& fb : frame.field_borrows) if (!fb.holder.empty()) any_held = true;
                std::unordered_set<std::string> outer;
                if (any_held) {
                    for (size_t fi = 0; fi + 1 < scopes_.size(); ++fi)
                        outer.insert(scopes_[fi].declared.begin(),
                                     scopes_[fi].declared.end());
                    // The value-block result holder is not declared ANYWHERE
                    // yet — the enclosing `let` declares it after the RHS is
                    // visited — so it cannot be found by scanning frames. It
                    // is, by construction, about to live in the enclosing one.
                    if (!pending_esc_holder_.empty())
                        outer.insert(pending_esc_holder_);
                }
                auto escapes = [&](const auto& rec) {
                    if (rec.holder.empty()) return false;   // lexical: unchanged
                    // CEILING PROBE `rehome_all` — MEASURED 2026-08-27: fired
                    // 4 times across the 447 ledger compiles, ceiling 0.
                    // ⚠ CEILING 0 OVER A POPULATION OF FOUR IS NOT A
                    // REFUTATION. What was NOT measured: this site outside the
                    // ledger. The coverage map of 2026-08-27 counts 573,451
                    // arrivals at this condition over 8060 runs — `escapes` is
                    // one of the busiest probed sites in the file, and the 4
                    // is a property of the acceptance corpus, not of the
                    // compiler. The earlier reading ("it decides almost
                    // never") generalised a ledger count into a claim about
                    // everything we compile. The hypothesis below is UNPRICED
                    // for any population but the ledger; the ledger says 0 and
                    // can say nothing else. (docs/coverage §E lists 2,165,443
                    // here — that is the enclosing `escapes` lambda, not this
                    // condition.)
                    //
                    // The hypothesis was: `escapes` decides by NAME
                    // over `frame.declared`; a projection holder or an
                    // un-deposited co-holder tests FALSE and the loan dies at
                    // the inner `}`. Re-home everything held: strictly fewer
                    // releases, strictly longer loan lifetimes.
                    if (logos::probe::on("rehome_all")) return true;
                    if (outer.count(rec.holder)) return true;
                    for (auto& h : rec.co_holders)
                        if (outer.count(h)) return true;
                    return false;
                };
                auto move_out = [&](auto& vec, auto& dst) {
                    for (auto it = vec.begin(); it != vec.end(); ) {
                        if (escapes(*it)) {
                            dst.push_back(std::move(*it));
                            it = vec.erase(it); }
                        else ++it;
                    }
                };
                move_out(frame.borrows,       parent.borrows);
                move_out(frame.field_borrows, parent.field_borrows);
            }
        }
        auto& frame = scopes_.back();
        // Release borrows held by this scope.
        for (auto& br : frame.borrows) {
            auto it = var_find(br.target_slot, br.target);
            if (it != nullptr) {
                if (br.is_mut) {
                    // B82: release either an activated mut borrow or an
                    // outstanding reservation taken via in_call_args_.
                    if (it->mut_borrowed)
                        it->mut_borrowed = false;
                    else if (it->mut_reservations > 0)
                        it->mut_reservations--;
                }
                else if (it->shared_borrows > 0)
                    --it->shared_borrows;
            }
        }
        // B83: release field-path borrows.
        for (auto& fb : frame.field_borrows) {
            auto it = var_find(fb.target_slot, fb.target);
            if (it == nullptr) continue;
            if (fb.is_mut) it->mut_field_borrows.erase(fb.path);
            else {
                // Observational zero census, decrement site 2 of 3 — the
                // hot one. Coverage 2026-08-27: else-branch 575,653 runs,
                // `--second <= 0` evaluated 575,650 (3 misses), erase 575,649
                // — so exactly ONE decrement in 8060 runs left a live count
                // behind, and none left a zero.
                auto sit = it->shared_field_borrows.find(fb.path);
                if (sit != it->shared_field_borrows.end()) {
                    if (sit->second <= 0)
                        (void)logos::probe::on("szw_pop_pre0");
                    if (--sit->second <= 0)
                        it->shared_field_borrows.erase(sit);
                    else (void)logos::probe::on("szw_pop_keep");
                }
            }
        }
        // B87 dropck: before erasing this scope's declared locals, check
        // whether a dropck-relevant binding holds a borrow of one of them and
        // is dropped AFTER it. If so, the binding's Drop reads a dead local —
        // reject.
        // ⚠ THE BINDING MAY DIE IN THIS SAME FRAME, AND THAT IS NOT AN
        // EXEMPTION. The old spelling skipped it with the comment "its own
        // death coincides with the source, no issue", which is FALSE: drop
        // order inside one frame is REVERSE DECLARATION ORDER, so a binding
        // declared BEFORE its source is dropped AFTER it and its `drop` reads
        // a local that is already dead. `let h: H; let x = 7i64; h = H{r:&x};`
        // — three imported dropck fixtures are exactly that program, and their
        // own headers say so. The rule therefore only ever saw the cross-frame
        // case and was blind to the whole same-frame population. What decides
        // is the POSITION, so skip only the SOUND direction: the binding
        // declared LATER drops FIRST. Cross-frame (bpos < 0) keeps today's
        // behaviour exactly.
        // ⚠ AND THE GATE IS `dropck_borrow_sources_`, NOT `ref_borrow_sources_`.
        // That map is written only under `struct_is_dropck_relevant`, so the
        // Drop requirement is already paid for. The un-gated version of this
        // same position test — the crude `droporder` ceiling probe — refuses
        // `let r: &i64; let x = 7i64; r = &x;`, deferred initialisation of an
        // ordinary reference, which is legal and must stay legal. MEASURED as a
        // one-variable pair; pass/bc_dropck_reverse_order_nodrop_admit pins it.
        if (!frame.declared.empty()) {
            for (auto& [binding, sources] : dropck_borrow_sources_) {
                // Name-keyed map, so no slot for the holder — the same name
                // fallback the pre-F5 `dying.count(binding)` used.
                long bpos = declared_pos(frame, RefSrc{binding, NO_SLOT});
                for (auto& src : sources) {
                    long spos = declared_pos(frame, src);   // F5, not the name
                    if (spos < 0) continue;                 // not dying here
                    if (bpos >= 0 && spos < bpos) continue; // binding drops first
                    uint32_t ln = dropck_binding_line_[binding];
                    report(ln, std::format(
                        "binding '{}' has a `Drop` impl and borrows local '{}', "
                        "but '{}' goes out of scope before '{}' is dropped",
                        binding, src.name, src.name, binding));
                    break;
                }
            }
        }
        // §B6 (E0597): a reference binding in an OUTER scope that borrows a
        // local dying here now dangles. Don't report yet — NLL flags only the
        // first USE past this point (handled in check_live). A binding dying in
        // THIS same scope is fine (it can't outlive its source).
        if (!frame.declared.empty()) {
            std::unordered_set<std::string> dying;
            for (auto& n : frame.declared) dying.insert(n);
            for (auto& [place, sources] : ref_borrow_sources_) {
                // F6: keys are PLACES; the binding that dangles is the root.
                std::string binding = place_root(place);
                // CEILING PROBE `droporder` — both halves of this deposit are
                // deliberately permissive: a never-used dangling binding is
                // never reported, and a same-frame death is skipped even
                // though drop order inside ONE frame is REVERSE declaration
                // order, so a binding declared BEFORE its source outlives it.
                bool dorder = logos::probe::on("droporder");
                if (dorder) {
                    auto didx = [&](std::string_view n) -> long {
                        for (size_t i = 0; i < frame.declared.size(); ++i)
                            if (frame.declared[i] == n) return (long)i;
                        return -1;
                    };
                    long bi = didx(binding);
                    if (bi >= 0)
                        for (auto& src : sources)
                            if (didx(src.name) > bi) {
                                report(ref_borrow_line_[place], std::format(
                                    "ceiling-probe droporder: '{}' does not live "
                                    "long enough: it is borrowed by '{}' (E0597)",
                                    src.name, binding));
                                break;
                            }
                }
                if (dying.count(binding) || dangling_.count(binding)) continue;
                for (auto& src : sources) {
                    if (!dying_binding(frame, src)) continue;   // F5, not the name
                    dangling_[binding] =
                        DanglingRef{ src.name, ref_borrow_line_[place] };
                    if (dorder)
                        report(ref_borrow_line_[place], std::format(
                            "ceiling-probe droporder: '{}' does not live long "
                            "enough: it is borrowed by '{}' (E0597)",
                            src.name, binding));
                    break;
                }
            }
        }
        // Remove variables declared in this scope.
        for (auto& name : frame.declared) {
            var_erase(NO_SLOT, name);
            prov_.erase(name);
            dropck_borrow_sources_.erase(name);
            dropck_binding_line_.erase(name);
            dropck_field_srcs_.erase(name);
            erase_ref_sources_under(name);   // F6: the whole place subtree
            dangling_.erase(name);
        }
        scopes_.pop_back();
    }

    void declare_var(const std::string& name, uint32_t slot = NO_SLOT) {
        var_at(slot, name) = VarState{};  // Phase-1: real slot → dense slot_
        if (in_closure_body_) closure_body_decls_.insert(name);
        if (!scopes_.empty()) {
            scopes_.back().declared.push_back(name);
            scopes_.back().declared_slots.push_back(slot);   // F5
        }
        note_binding_slot(name, slot);    // F5
    }

    // F5: is `s` one of the bindings THIS frame is about to erase? The NAME
    // comparison alone answers YES for a shadowing inner local that merely
    // spells the same word, which is the false E0597 above. When BOTH sides
    // carry a slot the slots decide; when either does not, fall back to the
    // name — the pre-F5 behaviour, conservative in the refusing direction.
    // ⚠ THE POSITION, NOT THE MEMBERSHIP, IS THE ANSWER ONE OF THE READERS
    // NEEDS: drop order inside one frame is REVERSE declaration order, so
    // "both die here" does not mean "they die together". `declared_pos`
    // answers where; `dying_binding` keeps the boolean question for the
    // readers that only ask that, and is now expressed in terms of it so the
    // two can never drift.
    static long declared_pos(const ScopeFrame& fr, const RefSrc& s) {
        for (size_t i = 0; i < fr.declared.size(); ++i) {
            if (fr.declared[i] != s.name) continue;
            if (s.slot != NO_SLOT && i < fr.declared_slots.size() &&
                fr.declared_slots[i] != NO_SLOT &&
                fr.declared_slots[i] != s.slot)
                continue;   // same word, different binding
            return (long)i;
        }
        return -1;
    }
    // ONE predicate for BOTH readers: pop_scope's `dangling_` deposit (the
    // pre-existing sibling defect, same one-property pair) and D-b's tail
    // check. They differ only in where they report, never in what dies.
    static bool dying_binding(const ScopeFrame& fr, const RefSrc& s) {
        return declared_pos(fr, s) >= 0;
    }

    // F5: identity of the binding a name currently denotes. Loans capture it
    // at record time; a later shadowing `let` of the same name overwrites the
    // map but cannot reach loans already recorded.
    void note_binding_slot(const std::string& name, uint32_t slot) {
        if (name.empty()) return;
        cur_slot_of_[name] = slot;
    }
    uint32_t slot_of_binding(const std::string& name) const {
        if (name.empty()) return NO_SLOT;
        auto it = cur_slot_of_.find(name);
        return it == cur_slot_of_.end() ? NO_SLOT : it->second;
    }

    // F-1: does this name, HERE, denote a CLOSURE PARAMETER — or a body local
    // that merely spells the same word? `closure_param_names_` is a set of
    // strings, and a set of strings cannot answer that; the probe that priced
    // this rule asked it anyway and refused a legal program for it (ce5 in
    // PROBES.md). The binding is the innermost frame that declares the name,
    // so the question is whether that frame is the one the parameter was
    // declared in. F5's `declared_slots` cannot decide it: a closure parameter
    // is `declare_var(nm, NO_SLOT)` and NO_SLOT compares equal to everything.
    bool names_live_closure_param(const std::string& n) const {
        if (n.empty()) return false;
        auto it = closure_param_frame_.find(n);
        if (it == closure_param_frame_.end()) return false;
        for (size_t i = scopes_.size(); i-- > 0; )
            for (auto& d : scopes_[i].declared)
                if (d == n) return i == it->second;
        return false;
    }

    // ── B87 dropck helpers ───────────────────────────────────────────────
    //
    // A struct is "dropck-relevant" iff it has a Drop impl AND its declared
    // template had a lifetime parameter (preserved through mono in B87).
    // For such bindings, borrows fed into the construction must outlive
    // the binding's drop point (textual scope-end of the binding).
    bool struct_is_dropck_relevant(TypeRef t) const {
        if (!t || t.kind() != LogosType::Kind::Struct) return false;
        if (!needs_drop(t, prog_, ts_)) return false;
        std::string sname(t.struct_name());
        // Check the post-mono struct (preserves lifetime_params via the
        // B87 clone_struct_def change). O(1) via ts_'s name→def index.
        auto has_lt = [](lir_view::StructView sd) {
            return sd && !sd.lifetime_params().empty();
        };
        auto sit = ts_.struct_by_name.find(sname);
        if (sit != ts_.struct_by_name.end() && has_lt(sit->second)) return true;
        auto pit = ts_.spec_by_name.find(sname);
        if (pit != ts_.spec_by_name.end() && has_lt(pit->second)) return true;
        // Also honor explicit lifetime_args on the TypeRef (paranoia).
        return !t.lifetime_args().empty();
    }

    // ── DROPCK LIVENESS IN THE LOAN CHANNEL ──────────────────────────────
    // A binding whose type has a `Drop` impl is USED once more after its
    // textual last use: at its drop. `release_dead_borrows` reads only
    // `last_use_*`, which knows nothing about drops, so `let w = Wrap { p:
    // &mut x }; x = 1;` retired the loan at the `let` and admitted; rustc
    // says E0506. MEASURED with LOGOS_DUMP_BC_RELEASE: `holder=wrap lu=0`.
    //
    // THE REACH TEST IS DIRECT FIELDS ONLY, AND THAT IS THE `may_dangle`
    // STAND-IN. rustc lets a `Drop` impl observe a borrow it can NAME through
    // `self`; `Vec<B>`/`Box<&T>` reach their elements too but their drops are
    // the `#[may_dangle]` kind, and holding those loans to scope end would
    // over-refuse every `let v: Vec<B> = …; v.push(c.mk()); <last use of v>;
    // c.bump()` the D1 arc made legal. `bc_loan_carrying_type`'s TYPE-ARG
    // recursion is exactly what would capture them, so it is deliberately NOT
    // used here — the walk is over DECLARED FIELDS. Logos has no `may_dangle`
    // spelling; when it gets one, this predicate is where it is consulted.
    // Residual (accepted, stated): `struct W { v: Vec<&i64> } impl Drop for W`
    // stays admitted. That is the status quo, not a regression.
    bool drop_can_observe_borrow(TypeRef t, int depth = 0) const {
        if (!t || depth > 4) return false;
        if (t.kind() != LogosType::Kind::Struct) return false;
        if (!needs_drop(t, prog_, ts_)) return false;
        std::string want = concrete_struct_name(t);
        auto reaches_ref = [&](lir_view::StructView sd) -> bool {
            if (!sd) return false;
            for (auto& f : sd.fields()) {
                TypeRef ft = f.type(prog_.type_pool.impl());
                if (!ft) continue;
                auto fk = ft.kind();
                if (fk == LogosType::Kind::Ref ||
                    fk == LogosType::Kind::MutRef) return true;
                if (drop_can_observe_borrow(ft, depth + 1)) return true;
            }
            return false;
        };
        auto sit = ts_.struct_by_name.find(want);
        if (sit != ts_.struct_by_name.end() && reaches_ref(sit->second)) return true;
        auto pit = ts_.spec_by_name.find(want);
        if (pit != ts_.spec_by_name.end() && reaches_ref(pit->second)) return true;
        return false;
    }
    // A loan held by such a binding is not retired by the NLL cursor. It dies
    // at `pop_scope`, which IS the drop point — no new lifetime concept.
    template <class Rec>
    bool holder_drops_after_last_use(const Rec& r) const {
        if (drop_can_observe_borrow(holder_ty_of(r.holder))) return true;
        for (auto& co : r.co_holders)
            if (drop_can_observe_borrow(holder_ty_of(co))) return true;
        return false;
    }

    // §6.1: `items.union.ref.borrow` — a borrow of one union field
    // implicitly borrows ALL fields (they share common storage). At
    // borrow-recording time we coerce union field-path borrows into
    // whole-root borrows so any other field-path of the same union
    // overlaps. Nested-union (`s.u.a` where `s.u` is a union) is a
    // narrower follow-up.
    bool is_union_root(TypeRef t) const {
        if (!t || t.kind() != LogosType::Kind::Struct) return false;
        std::string sname(t.struct_name());
        auto check = [&](const std::vector<lir_view::StructView>& defs) {
            for (auto& sd : defs)
                if (sd.name() == sname && sd.is_union()) return true;
            return false;
        };
        return check(prog_.structs) || check(prog_.struct_specializations);
    }

    // Walk a struct-lit (or nested aggregate) expression, collecting names
    // of LOCAL variables that are borrowed via AddrOf. Filters out params.
    // ⚠ FILLS RefSrc, NOT std::string, AND THE SLOT IS WHY. The B87 dropck
    // reader below compared these sources to the dying frame BY NAME, so a
    // shadowed inner local produced a false report about an OUTER binding that
    // outlives everything. One-property pair, differing only in the inner
    // local's spelling: with it named `outer` the program is refused, with it
    // named `zz` it compiles. `RefSrc` and `dying_binding` already exist — §B6
    // was converted last round and this was the sibling twenty lines above it,
    // written but not read. Resolving the slot HERE, where the name is still in
    // scope, is the only point at which it is knowable.
    void collect_borrow_locals(lir_view::ExprRef e,
                               std::vector<RefSrc>& out) const {
        if (!e) return;
        using EC = lir_schema::expr::Code;
        switch (e.kind()) {
            case EC::AddrOf: {
                std::string n(lir_view::EAddrOfView{e}.var_name());
                if (var_has(NO_SLOT, n) && !param_names_.count(n)) {
                    uint32_t sl = slot_of_binding(n);
                    out.push_back(RefSrc{std::move(n), sl});
                }
                return;
            }
            case EC::AddrOfTemp:
                collect_borrow_locals(lir_view::EAddrOfTempView{e}.inner(), out);
                return;
            case EC::StructLit:
                lir_view::EStructLitView{e}.each_field_value(
                    [&](lir_view::ExprRef fv) { collect_borrow_locals(fv, out); });
                return;
            case EC::TupleLit:
                lir_view::ETupleLitView{e}.each_elem(
                    [&](lir_view::ExprRef fv) { collect_borrow_locals(fv, out); });
                return;
            case EC::Cast:
                collect_borrow_locals(lir_view::ECastView{e}.operand(), out);
                return;
            default:
                return;
        }
    }

    // §B6 (E0597): record the LOCAL variables that binding `name` borrows from,
    // so pop_scope can flag `name` dangling if it outlives one of them. Clears
    // any stale dangling/sources first (a rebind re-owns). Only records when the
    // value actually borrows a local — pure moves/copies record nothing.
    // ── F6: PLACE keys ────────────────────────────────────────────────────
    static std::string place_of(const std::string& root, const std::string& path) {
        return path.empty() ? root : root + "." + path;
    }
    static bool place_under(const std::string& place, const std::string& root) {
        return place == root ||
               (place.size() > root.size() &&
                place.compare(0, root.size(), root) == 0 &&
                place[root.size()] == '.');
    }
    static std::string place_root(const std::string& place) {
        auto d = place.find('.');
        return d == std::string::npos ? place : place.substr(0, d);
    }
    // Every local a binding (or sub-place) may borrow from, unioned over the
    // whole place subtree. This is what the pre-F6 `ref_borrow_sources_[root]`
    // lookup meant; it now has to be computed rather than read.
    std::vector<RefSrc> ref_sources_under(const std::string& root) const {
        std::vector<RefSrc> out;
        if (root.empty()) return out;
        for (auto& [pl, srcs] : ref_borrow_sources_) {
            if (!place_under(pl, root)) continue;
            for (auto& s : srcs)
                if (std::find(out.begin(), out.end(), s) == out.end())
                    out.push_back(s);
        }
        return out;
    }
    // A write to `place` replaces what that place holds — and everything
    // reachable THROUGH it. This is the erase the pre-F6 merge never did.
    void erase_ref_sources_under(const std::string& place) {
        if (place.empty()) return;
        for (auto it = ref_borrow_sources_.begin(); it != ref_borrow_sources_.end();)
            it = place_under(it->first, place) ? ref_borrow_sources_.erase(it) : std::next(it);
        for (auto it = ref_borrow_line_.begin(); it != ref_borrow_line_.end();)
            it = place_under(it->first, place) ? ref_borrow_line_.erase(it) : std::next(it);
    }
    void store_ref_sources(const std::string& place,
                           std::vector<std::pair<std::string, RefSrc>> pairs,
                           const std::string& self_name, uint32_t ln) {
        for (auto& [sub, src] : pairs) {
            if (src.name == self_name) continue;  // a binding never borrows ITSELF
            auto& dst = ref_borrow_sources_[place_of(place, sub)];
            if (std::find(dst.begin(), dst.end(), src) == dst.end())
                dst.push_back(src);
            ref_borrow_line_[place_of(place, sub)] = ln;
        }
    }

    void record_ref_sources(const std::string& name, lir_view::ExprRef val,
                            uint32_t ln) {
        dangling_.erase(name);
        erase_ref_sources_under(name);
        if (!val) return;
        std::vector<std::pair<std::string, RefSrc>> pairs;
        collect_ref_sources_paths(val, std::string{}, pairs);
        store_ref_sources(name, std::move(pairs), name, ln);
    }

    // §B6: record the borrow sources stored INTO one place of a binding — for
    // a field/tuple write `root.f = &x`. Sibling fields keep their own sources
    // because they live under their own keys; `root.f`'s previous sources are
    // REPLACED, not merged (F6: the merge made an overwritten borrow immortal).
    // Params are filtered by collect_ref_sources.
    void add_ref_sources(const std::string& name, const std::string& path,
                         lir_view::ExprRef val, uint32_t ln) {
        if (name.empty() || !val) return;
        std::string place = place_of(name, path);
        erase_ref_sources_under(place);
        std::vector<std::pair<std::string, RefSrc>> pairs;
        collect_ref_sources_paths(val, std::string{}, pairs);
        store_ref_sources(place, std::move(pairs), name, ln);
    }

    // Like collect_borrow_locals but also follows borrow-returning calls to
    // their borrowed local (receiver / ref args) for §B6 source tracking.
    //
    // F6: the PATH-carrying form. `out` collects (sub-path, source) pairs so
    // `Wrap { b: B { p: &z } }` records the source `z` at sub-path "b.p"
    // instead of at the bare binding — which is what lets a later `w.b = …`
    // erase exactly the sources that write replaces. Only the aggregate
    // LITERAL cases extend the path (they are the only ones that know which
    // field a value lands in); every other case forwards it unchanged.
    // ── D1 residuals / R1 — HOISTED OUT OF collect_ref_sources_paths ─────
    // It was a LOCAL LAMBDA and therefore readable at exactly one site, while
    // `prov_of`'s Call arm — the channel the RETURN gate consults — asked the
    // TYPE filters only. That is why `fn f(x: i64) -> &[i64] { let a: [i64;3]
    // = [x,x,x]; return &a[0..2]; }` COMPILED, LINKED and dangled: measured
    // with an intervening frame-stomping call, the exit code IS the clobber
    // constant (9 -> 9, 40 -> 40, 71 -> 71) where 1 is correct. `[retgate]`
    // named the spread in one run — `prov{loc=0 tmp=0 np=0} srcs=[a,]`: the
    // answer was ALREADY COMPUTED, one channel over. Hoisted rather than
    // re-derived; the rule below is byte-identical to the lambda it replaces.
    // ── D1 residuals / R1 store side: DOES THIS ARG NODE FORM THE BORROW
    // AT THE CALL SITE? The per-arg filters below deliberately exclude fat
    // forms via `is_plain_ref_kind` (a by-value slice COPY — `tv_build(h,
    // name.as_str(), …)` — must not tie), but that exclusion also dropped
    // the arg that CREATES the slice right here: `&arr[0..1]` lowers to
    // `Call(slice_get_range, [SliceLit{AddrOfTemp(arr)}, lo, hi])`, and the
    // SliceLit arg was filtered out by its TYPE before its NODE could
    // speak. A borrow formed at the call site is a borrow of a local by
    // construction — no copy ambiguity exists — so admit it by node kind:
    // SliceLit, AddrOf, AddrOfTemp, through transparent Casts. A by-value
    // fat COPY arrives as VarRef/MethodCall/FieldRead and stays excluded.
    //
    // ── #70(a): THE NESTED BORROW-FORMING CALL ─────────────────────────
    // The residual this closes: `pick1(&arr[0..2])` passes the
    // `slice_get_range` Call node itself (fat-typed `&[i64]`), which no
    // rule above admits — not `is_plain_ref_kind` (fat), not
    // `is_borrow_carrying_type` (a slice of i64 carries nothing by name),
    // not the node-kind list. Nothing was deposited, so the dangle was
    // admitted at rc=0 while the one-level twin `o = &arr[0..2]` refused
    // at rc=1 (pinned by bc_d1res_r2_sliceform_dangle).
    //
    // The rule: a CALL whose result is a reference and one of whose
    // arguments is ITSELF borrow-forming (recursively — the SliceLit /
    // AddrOf / AddrOfTemp base above, or another such call) forms the
    // borrow at this site, exactly like the one-level spelling it wraps.
    // This is the same conservative signature-elision the Call arm below
    // already applies to plain-ref args; the nesting is what was missing.
    //
    // `EC::MethodCall` STAYS EXCLUDED, and that is the load-bearing half.
    // The by-value fat COPY this whole filter exists to keep out is
    // `tv_build(h, name.as_str(), …)` (stdlib/mem/writ/parser.logos:324) —
    // a MethodCall. Excluding the kind, rather than guessing on the
    // fat/plain axis, is what keeps that exemption; it is pinned in the
    // abuse direction by bc_argcomp_tvbuild_byvalue_fat_admit, which
    // before this task NOTHING under tests/ pinned.
    //
    // MEASURED AND REJECTED, the provenance route the residual note asked
    // for (`flow_of_call(callee)->to_result`): slice_get_range's mask is
    // 0, because its body reaches the buffer through `s.as_ptr()`
    // (a SlicePtr node taint_of has no arm for), an i64 address
    // round-trip (no BinOp arm), and `slice_from_raw` (sema-rewritten to
    // the bodyless `str_from_raw`, whose `*const T` argument the (a)-(d)
    // fallback filter drops). Repairing all four DOES produce
    // `to_result=1` and DOES refuse this fixture — and it also makes
    // `string_as_str` correctly report that it retains its argument,
    // which then reds the stdlib itself: `join_order.decide_over_set`
    // stores `string_as_str(&szs[k])` into `rs.w.ssz[k]` and afterwards
    // calls `szs[i4].clear()` while `rs` is live. That is a REAL aliasing
    // hazard rustc would reject too, so the summary route is blocked on a
    // stdlib repair, not on the checker. Recorded, not landed.
    void collect_ref_sources_paths(
            lir_view::ExprRef e, const std::string& path,
            std::vector<std::pair<std::string, RefSrc>>& out) const {
        if (!e) return;
        using EC = lir_schema::expr::Code;
        const auto* pool = prog_.type_pool.impl();
        // F5: a source collected HERE is named by a live binding, so its
        // identity is `slot_of_binding` AT THIS POINT. A source forwarded out
        // of `ref_sources_under` already carries the slot captured when IT was
        // collected, and must keep it — re-resolving would read the shadowing
        // binding's slot, which is exactly the identity loss being fixed.
        auto emit_src = [&](RefSrc src) {
            for (auto& pr : out)
                if (pr.first == path && pr.second == src) return;
            out.emplace_back(path, std::move(src));
        };
        auto emit = [&](std::string src) {
            uint32_t sl = slot_of_binding(src);
            emit_src(RefSrc{std::move(src), sl});
        };
        auto sub = [&](const std::string& p) {
            return path.empty() ? p : path + "." + p;
        };
        // ── #71/#72 round / CLASS B: THE FAT BY-VALUE ARG, TIED BY FACT ────
        //
        // THE DEFECT the per-arg filters below leave open. `my_as_str2(
        // owner.as_str())` where `fn my_as_str2(s: &str) -> str { return s; }`
        // ADMITS at rc=0 — a real dangle — because the argument is a fat
        // `MethodCall` and all three filters answer NO: not `is_plain_ref_kind`
        // (fat), not `is_borrow_carrying_type` (a `str` carries nothing BY
        // NAME), and `forms_borrow_at_call` excludes MethodCall on purpose.
        // The callee's SUMMARY, meanwhile, says `result<-0x1` — "I retain
        // parameter 0" — and NOTHING in this channel ever asked it.
        //
        // ⚠ AND THE EXCLUSION CANNOT SIMPLY BE DROPPED. It exists for
        // `tv_build(h, name.as_str(), …)` (stdlib/mem/writ/parser.logos), a fat
        // by-value COPY that must NOT tie, pinned in the abuse direction by
        // tests/logos/pass/bc_argcomp_tvbuild_byvalue_fat_admit.logos — whose
        // callee returns a `#[borrow_carrying]` value on purpose, so no
        // "results that are scalars are safe" shortcut can pass it.
        //
        // THE RULE, and why it keeps that exemption BY CONSTRUCTION rather
        // than by a fat-versus-plain guess: an argument ties iff the callee's
        // borrow-flow summary says its bit reaches the result. The pin's `tvb`
        // summarises `result<-0` (the Pod arm retains nothing) → no tie, green
        // stays green. `my_as_str2` summarises `result<-0x1` → tie, dangle
        // refused. The two are separated by what the callee DOES, which is the
        // only thing that can separate them; the argument's TYPE is the same
        // fat `str` in both. Falls back to the kind filters whenever the
        // summary is unavailable (extern, cross-package, >kFlowMaxParams), so
        // this can only ADD ties, never remove one.
        //
        // `idx` is param-space: receiver-first for a method call, matching
        // FlowSummary's own convention (call_result_taint's comment).
        auto arg_retained_by_callee = [](const FlowSummary* fs, size_t idx) {
            return fs && fs->available && idx < fs->nparams &&
                   (fs->to_result & (1ull << idx)) != 0;
        };
        // ── D1 residuals / R1 store side: DOES THIS ARG NODE FORM THE BORROW
        // AT THE CALL SITE? The per-arg filters below deliberately exclude fat
        // forms via `is_plain_ref_kind` (a by-value slice COPY — `tv_build(h,
        // name.as_str(), …)` — must not tie), but that exclusion also dropped
        // the arg that CREATES the slice right here: `&arr[0..1]` lowers to
        // `Call(slice_get_range, [SliceLit{AddrOfTemp(arr)}, lo, hi])`, and the
        // SliceLit arg was filtered out by its TYPE before its NODE could
        // speak. A borrow formed at the call site is a borrow of a local by
        // construction — no copy ambiguity exists — so admit it by node kind:
        // SliceLit, AddrOf, AddrOfTemp, through transparent Casts. A by-value
        // fat COPY arrives as VarRef/MethodCall/FieldRead and stays excluded.
        //
        // ── #70(a): THE NESTED BORROW-FORMING CALL ─────────────────────────
        // The residual this closes: `pick1(&arr[0..2])` passes the
        // `slice_get_range` Call node itself (fat-typed `&[i64]`), which no
        // rule above admits — not `is_plain_ref_kind` (fat), not
        // `is_borrow_carrying_type` (a slice of i64 carries nothing by name),
        // not the node-kind list. Nothing was deposited, so the dangle was
        // admitted at rc=0 while the one-level twin `o = &arr[0..2]` refused
        // at rc=1 (pinned by bc_d1res_r2_sliceform_dangle).
        //
        // The rule: a CALL whose result is a reference and one of whose
        // arguments is ITSELF borrow-forming (recursively — the SliceLit /
        // AddrOf / AddrOfTemp base above, or another such call) forms the
        // borrow at this site, exactly like the one-level spelling it wraps.
        // This is the same conservative signature-elision the Call arm below
        // already applies to plain-ref args; the nesting is what was missing.
        //
        // `EC::MethodCall` STAYS EXCLUDED, and that is the load-bearing half.
        // The by-value fat COPY this whole filter exists to keep out is
        // `tv_build(h, name.as_str(), …)` (stdlib/mem/writ/parser.logos:324) —
        // a MethodCall. Excluding the kind, rather than guessing on the
        // fat/plain axis, is what keeps that exemption; it is pinned in the
        // abuse direction by bc_argcomp_tvbuild_byvalue_fat_admit, which
        // before this task NOTHING under tests/ pinned.
        //
        // MEASURED AND REJECTED, the provenance route the residual note asked
        // for (`flow_of_call(callee)->to_result`): slice_get_range's mask is
        // 0, because its body reaches the buffer through `s.as_ptr()`
        // (a SlicePtr node taint_of has no arm for), an i64 address
        // round-trip (no BinOp arm), and `slice_from_raw` (sema-rewritten to
        // the bodyless `str_from_raw`, whose `*const T` argument the (a)-(d)
        // fallback filter drops). Repairing all four DOES produce
        // `to_result=1` and DOES refuse this fixture — and it also makes
        // `string_as_str` correctly report that it retains its argument,
        // which then reds the stdlib itself: `join_order.decide_over_set`
        // stores `string_as_str(&szs[k])` into `rs.w.ssz[k]` and afterwards
        // calls `szs[i4].clear()` while `rs` is live. That is a REAL aliasing
        // hazard rustc would reject too, so the summary route is blocked on a
        // stdlib repair, not on the checker. Recorded, not landed.
        std::function<bool(lir_view::ExprRef)> forms_borrow_at_call =
            [&](lir_view::ExprRef a) -> bool {
            while (a && a.kind() == EC::Cast)
                a = lir_view::ECastView{a}.operand();
            if (!a) return false;
            if (a.kind() == EC::SliceLit || a.kind() == EC::AddrOf ||
                a.kind() == EC::AddrOfTemp)
                return true;
            // An AGGREGATE LITERAL forms a borrow iff one of its members does
            // (`pick((&tmp, 1))`, `pick(H { r: &tmp })`, `pick(Opt::Some(&tmp))`
            // all admitted at rc 0 while their bare spellings refused — the #70
            // verify's m14b/m16/m17). The tv_build exemption is untouched: a
            // by-value fat COPY arrives as VarRef/MethodCall/FieldRead, and a
            // literal with no borrow-forming member still answers false.
            bool tied = false;
            switch (a.kind()) {
            case EC::TupleLit:
                lir_view::ETupleLitView{a}.each_elem([&](lir_view::ExprRef inner) {
                    if (!tied && inner && forms_borrow_at_call(inner)) tied = true;
                });
                return tied;
            case EC::StructLit:
                lir_view::EStructLitView{a}.each_field(
                    [&](std::string_view, lir_view::ExprRef inner) {
                        if (!tied && inner && forms_borrow_at_call(inner)) tied = true;
                    });
                return tied;
            case EC::EnumLitData:
                lir_view::EEnumLitDataView{a}.each_payload([&](lir_view::ExprRef inner) {
                    if (!tied && inner && forms_borrow_at_call(inner)) tied = true;
                });
                return tied;
            case EC::ArrLit:
                lir_view::EArrLitView{a}.each_elem([&](lir_view::ExprRef inner) {
                    if (!tied && inner && forms_borrow_at_call(inner)) tied = true;
                });
                return tied;
            default: break;
            }
            if (a.kind() != EC::Call) return false;
            if (!is_ref_kind(a.type(pool))) return false;
            lir_view::ECallView{a}.each_arg([&](lir_view::ExprRef inner) {
                if (!tied && inner && forms_borrow_at_call(inner)) tied = true;
            });
            return tied;
        };
        switch (e.kind()) {
            // MEASURED 2026-08-28, 379-row ledger: 49 fires, CEILING 0,
            // COST 0. NEGATIVE RESULT, and the COST 0 is itself informative:
            // the named risk was that this crude form refuses
            // tests/spec/pass/borrow_1 and the three scalar-capture rows that
            // `expr.closure.env-capture-binding` makes legal. It refuses
            // NONE of them — and it also closes none of regions-steal-closure,
            // regions-return-ref-to-upvar-issue-17403 or region-bound-on-
            // closure-outlives-call. A closure env carrying a borrow out of a
            // dying scope is blocked by something upstream of the §B6 source
            // walk, not by the missing arm. Population 1-3 rows: RULE 4.
            // PROBE lifereg_closurestore: this function follows a borrow
            // through StructLit/TupleLit/ArrLit and through borrow-returning
            // calls, but has no ClosureBox arm, so a borrow carried by a
            // closure ENV is invisible to the §B6 store-borrow record. ⚠ The
            // capture must not be a plain scalar taken by value — spec rule
            // `expr.closure.env-capture-binding` says a scalar is COPIED into
            // the env, and three ledger rows are legal under it. This CRUDE
            // form does NOT make that distinction; the cost side is what
            // prices the omission.
            case EC::ClosureBox: {
                if (logos::probe::on("lifereg_closurestore") ||
                    logos::probe::on("bxsrc")) {
                    lir_view::EClosureBoxView cbv{e};
                    cbv.each_capture_name([&](std::string_view cap) {
                        std::string cn(cap);
                        if (!var_has(NO_SLOT, cn)) return;
                        if (param_names_.count(cn)) return;
                        emit(std::move(cn));
                    });
                }
                return;
            }
            case EC::AddrOf: {
                std::string n(lir_view::EAddrOfView{e}.var_name());
                if (var_has(NO_SLOT, n) && !param_names_.count(n))
                    emit(std::move(n));
                return;
            }
            case EC::AddrOfTemp: {
                // `&x.f`, `&x[i]`, `&*r` → root local via the shared place walker.
                BorrowPlace bp = extract_borrow_place(
                    lir_view::EAddrOfTempView{e}.inner(), pool);
                if (bp.root.empty() || !var_has(bp.root_slot, bp.root) ||
                    param_names_.count(bp.root)) return;
                // ── S5-D4: THE PLACE WENT THROUGH A REFERENCE ───────────────
                //
                // `let b: &[Row] = rows; ks.push((&b[0]).k);` recorded `b` — a
                // LOCAL — as a §B6 source of `ks`, so `ks` was declared
                // dangling the moment `b`'s block closed and the next USE of
                // `ks` was refused (E0597). The refusal is wrong twice over:
                //   * `b` is a `&[Row]` VALUE copied from a parameter; the
                //     bytes the pushed `str` points at are the CALLER's, and
                //     they outlive the function, never mind the block.
                //   * the same shape one scope up (`b` at fn scope, m6) was
                //     admitted, so the verdict tracked the block boundary
                //     rather than the borrow — a scope coincidence, not a
                //     correctness property.
                // The VarRef arm below already states the right rule for the
                // plain copy (`o = r` emits r's SOURCES, never `r`); this arm
                // is the same question reached through a projection, and it
                // answered with the reference variable itself.
                //
                // NOT a weakening. Where the reference does borrow a local the
                // sources are recorded and travel: `let v = mk(); { let b =
                // &v; ks.push((&b[0]).k); }` emits `v`, and a use of `ks`
                // after `v` dies is refused exactly as before — it is now
                // refused for the RIGHT binding. It also CLOSES a hole in the
                // old rule: with `b` outliving `v`, emitting `b` saw nothing
                // die and admitted the dangle.
                //
                // The LOAN channel keeps rooting at `b` (extract_borrow_place's
                // Deref comment) — that policy is about exclusivity of the
                // reborrow, a different question, and it is untouched.
                if (bp.through_ref) {
                    ++thru_ref_prov_fired_;
                    for (auto& s : ref_sources_under(bp.root)) emit_src(s);
                    return;
                }
                emit(bp.root);
                return;
            }
            case EC::StructLit:
                lir_view::EStructLitView{e}.each_field(
                    [&](std::string_view fname, lir_view::ExprRef fv) {
                        collect_ref_sources_paths(
                            fv, fname.empty() ? path : sub(std::string(fname)), out);
                    });
                return;
            case EC::TupleLit: {
                uint32_t idx = 0;
                lir_view::ETupleLitView{e}.each_elem(
                    [&](lir_view::ExprRef fv) {
                        collect_ref_sources_paths(fv, sub(std::to_string(idx++)), out);
                    });
                return;
            }
            case EC::ArrLit:
                // Whole-element granularity: an array index is not a static
                // path component here, so every element lands on `path`.
                lir_view::EArrLitView{e}.each_elem(
                    [&](lir_view::ExprRef fv) { collect_ref_sources_paths(fv, path, out); });
                return;
            case EC::Cast:
                collect_ref_sources_paths(lir_view::ECastView{e}.operand(), path, out);
                return;
            case EC::MethodCall: {
                lir_view::EMethodCallView v{e};
                // Only a borrow-returning call ties the result to its inputs.
                // A FAT value result (str/&[T]/borrowed DST) ties to the receiver
                // only when the method borrows self (see prov_of MethodCall) —
                // `Vec<str>::get` copies a stored borrow out, no receiver tie.
                TypeRef rt = e.type(pool);
                bool plain = is_plain_ref_kind(rt);
                bool fat   = !plain && is_ref_kind(rt);
                bool bc    = is_borrow_carrying_type(rt);
                const FlowSummary* fs = flow_of_method(v);
                // Class B, method spelling. The ENTRY gate widens too: a
                // method that retains an ARGUMENT (not self) into a fat result
                // is invisible to `result_borrows_self`, which asks only about
                // the receiver. Gated on the result being able to carry a
                // borrow at all, and on the summary saying SOMETHING reaches
                // it — so a summary of `result<-0` opens no gate.
                bool by_flow = fs && fs->available && fs->to_result != 0 &&
                               type_may_carry_borrow_erased(rt);
                if (plain || bc || (fat && result_borrows_self(v)) || by_flow) {
                    // param 0 is the receiver, matching FlowSummary's order.
                    if (plain || bc || (fat && result_borrows_self(v)) ||
                        arg_retained_by_callee(fs, 0))
                        collect_ref_sources_paths(v.receiver(), path, out);
                    size_t idx = 1;
                    v.each_arg([&](lir_view::ExprRef a) {
                        size_t i = idx++;
                        // D1: a BY-VALUE borrow-carrying argument carries its
                        // sources exactly like a plain-ref one — the same rule
                        // the loan channel applies. Reached only under the
                        // "result is ref/bc" gate above, so a consuming call
                        // returning a scalar still ties nothing.
                        if (a && (is_plain_ref_kind(a.type(pool)) ||
                                  is_borrow_carrying_type(a.type(pool)) ||
                                  forms_borrow_at_call(a) ||
                                  arg_retained_by_callee(fs, i)))
                            collect_ref_sources_paths(a, path, out);
                    });
                }
                return;
            }
            case EC::EnumLitData:
                lir_view::EEnumLitDataView{e}.each_payload(
                    [&](lir_view::ExprRef pl) { collect_ref_sources_paths(pl, path, out); });
                return;
            // D1 (door 5b in the §B6 channel): reading a borrow-carrying value
            // OUT of an aggregate unwraps the provenance the construction
            // wrapped — `let b = w.b` makes `b` borrow whatever `w` borrows,
            // so `b` must not outlive w's referent's scope. Gated on the READ's
            // type carrying borrows: reading a scalar field out of a
            // borrow-carrying holder copies a value, not a borrow.
            //
            // ── D1 round 14 / Q7: AND NOW THE FIELDREAD ARM, WITH ITS PAIR ──
            //
            // Round 13 / P1 widened TupleIndex and IndexRead to
            // `type_may_carry_borrow` and DELIBERATELY left this arm on the
            // old gate, with a standing note: "if it is widened later it needs
            // its own probe pair." Here it is.
            //
            // THE PAIR (one variable — the aggregate KIND — nothing else):
            //   `{ let x = 7; o = T { a: &x }.a; } *o`   admitted   rc=0
            //   `{ let x = 7; o = (&x, 0).0;    } *o`    refused    rc=1
            // Same borrow, same scope, same dangling read; the tuple leg went
            // through the arm P1 widened and the struct leg through this one.
            // `is_borrow_carrying_type` names Enum/Struct/ZonedStruct, so it
            // answers NO for the field's own type `&i64` and the read tied to
            // nothing.
            //
            // WHY THE RECEIVER-IS-A-LOCAL CASE HID IT for twelve rounds: `h.r`
            // on a plain variable is covered by the RefGraph channel, which
            // this channel need not duplicate. A LITERAL receiver has no place
            // for that channel to key on, which is why the witness spells one.
            case EC::FieldRead:
                if (type_may_carry_borrow(e.type(pool)))
                    collect_ref_sources_paths(
                        lir_view::EFieldReadView{e}.receiver(), path, out);
                return;
            // D1 round 13 / P1, door 1: THE GATE WAS WRITTEN FOR bc NAMES.
            // `is_borrow_carrying_type` answers NO for a plain `&mut T` — it
            // names Enum/Struct/ZonedStruct — so reading a reference OUT of a
            // tuple or an array (`t.0`, `arr[0]`) tied the result to nothing
            // in the §B6 channel, while the struct-field twin one arm up is
            // covered by the RefGraph channel instead. `type_may_carry_borrow`
            // is the predicate written for exactly this question (rounds 9/11)
            // and it answers YES for a ref kind.
            //
            // ⚠ THE FIELDREAD ARM ABOVE IS DELIBERATELY LEFT ON THE OLD GATE.
            // It is the same asymmetry read the other way: a struct field read
            // has a second channel and these two do not, and widening a
            // predicate under three arms at once is three rules in one control.
            // If it is widened later it needs its own probe pair.
            case EC::TupleIndex:
                if (type_may_carry_borrow(e.type(pool)))
                    collect_ref_sources_paths(
                        lir_view::ETupleIndexView{e}.receiver(), path, out);
                return;
            case EC::IndexRead:
                if (type_may_carry_borrow(e.type(pool)))
                    collect_ref_sources_paths(
                        lir_view::EIndexReadView{e}.receiver(), path, out);
                return;
            // ── D1 RESIDUALS R1/R2 (task #51): THE TWO SLICE SPELLINGS ──────
            //
            // Round 14 measured both arms and REVERTED them because neither
            // moved a verdict; the measurement pointed one step UPSTREAM, and
            // that is where the fix landed. `&arr[0..1]` lowers to a plain
            // `Call(slice_get_range<T>, [SliceLit{AddrOfTemp(arr)}, lo, hi])`
            // (sema's RANGE_EXPR index lowering; the outer `&` is transparent
            // for a slice-typed inner). The Call arm's ENTRY gate passed but
            // its per-arg TYPE filter rejected the fat SliceLit arg, so the
            // binding's `let` deposited nothing — the dry input hunt 14 saw.
            // `forms_borrow_at_call` (above) is the store-side fix; these two
            // arms are the read side, now with a wet input:
            //
            //   * SliceLit: the coercion node `try_coerce_array_ref_to_slice`
            //     builds for `&arr` decay and for range-index receivers. Its
            //     base is `AddrOfTemp(arr)` — the arm above roots it. Round
            //     14's "fired ZERO times" was not unreachability of the node:
            //     the node sat in an ARGUMENT position the arg filter never
            //     recursed into. Pinned: fail/bc_d1res_r2_sliceform_dangle.
            //   * SliceIndex: `sl[0]` on a `&[&i64]` — same question as the
            //     IndexRead arm above, same `type_may_carry_borrow` gate
            //     (reading a scalar OUT of a slice copies a value, not a
            //     borrow). Recurses to the slice operand (VarRef `sl`, whose
            //     `ref_sources_under` now answers `arr` → `x`). Pinned:
            //     fail/bc_d1res_r1_sliceindex_dangle + its admit twin.
            case EC::SliceLit:
                collect_ref_sources_paths(
                    lir_view::ESliceLitView{e}.base(), path, out);
                return;
            case EC::SliceIndex:
                if (type_may_carry_borrow(e.type(pool)))
                    collect_ref_sources_paths(
                        lir_view::ESliceIndexView{e}.slice(), path, out);
                return;

            case EC::IfExpr: {
                lir_view::EIfExprView v{e};
                collect_ref_sources_paths(v.then_val(), path, out);
                collect_ref_sources_paths(v.else_val(), path, out);
                return;
            }
            case EC::BlockExpr:
                // `if c { &x } else { … }` lowers each branch to a block whose
                // TAIL is the borrow — recurse into the result expr.
                collect_ref_sources_paths(
                    lir_view::EBlockExprView{e}.result(), path, out);
                return;
            // ── D1 round 14 / Q1-Q4: THE FOURTH CHANNEL NEVER GOT ROUND 8 ────
            //
            // Round 8 charged for ONE shape enumeration, and round 12 / A0 and
            // round 13 / P0c each paid the bill again — but only across the
            // THREE walkers the coverage table had columns for
            // (`ref_source_places`, `ref_sources_of`, the summarizer's
            // `taint_of`). THIS function is the fourth, and it is not a peer of
            // those three: it answers a DIFFERENT question for a DIFFERENT
            // verdict. `ref_borrow_sources_` is read by `pop_scope` to raise
            // E0597 ("does not live long enough"), so an omission here does not
            // move the `c.bump()` verdict those rounds measured — it loses a
            // DANGLING-REFERENCE refusal, which is why twelve rounds of
            // `c.bump()` witnesses could never see it. Adding the pattern
            // propagators as a fourth column is what made the four arms below
            // visible in one read.
            //
            // Every arm is the SAME transparency the other three already state,
            // and each was measured with its own one-variable twin (the twin is
            // the SAME program with the transparent node removed, and every
            // twin already REFUSED at rc=1 before this change):
            //
            //   Q1 MatchExpr  `o = match k { _ => &x };`   admitted (rc=0)
            //                 vs `o = if k==1 { &x } else { &x };`      rc=1
            //                 — round 12 / A0's defect, exactly, one channel
            //                 over. The IfExpr arm is directly above.
            //   Q2 Try        `o = pick(&x)?;`             admitted (rc=0)
            //                 vs `o = pickd(&x);`                       rc=1
            //                 — round 13 / P0c's defect, one channel over.
            //   Q3 Deref      `o = *rr;`                   admitted (rc=0)
            //                 vs `o = r;`                              rc=1
            //   Q4 Closure/FnPtrCall
            //                 `let g = || -> &i64 { &x }; o = g();`     rc=0
            //                 `let g: fn(&i64)->&i64 = idr; o = g(&x);` rc=0
            //                 vs `o = idr(&x);`                        rc=1
            //
            // The MatchExpr scrutinee is deliberately NOT a source, for the
            // same reason `ref_source_places` gives: a match yields one of its
            // ARM values, never the thing it discriminated on. Pattern BINDINGS
            // that carry the scrutinee's places are a separate rule and already
            // have one (`propagate_pat_sources`, called on every arm).
            case EC::MatchExpr:
                lir_view::EMatchExprView{e}.each_arm(
                    [&](lir_view::EMatchArmRef arm) {
                        collect_ref_sources_paths(arm.value(), path, out);
                    });
                return;
            //
            // ⚠ THIS ARM IS UNEXERCISED, AND SO ARE ROUND 13's TWO — MEASURED.
            // A fire-count print inside all three `Code::Try` arms (this one,
            // `ref_source_places`', `ref_sources_of`') counted ZERO fires over
            // every `?`-using file in the corpus, INCLUDING round 13's own
            // witness fail/bc_d1r13_p0c_try.logos. Control revert: with all
            // three arms DELETED, that witness still refuses (rc=1), its twin
            // still refuses, and its dead_admit control still admits — so
            // round 13's P0c credit belongs to the OTHER half of that round
            // (`ref_source_admissible` admitting a graph-recorded place whose
            // root no `let` declared, i.e. sema's synthesized `__try_ok_N`),
            // not to the Try arms.
            //
            // THE REASON: sema desugars `?` into a MATCH before the borrow
            // checker runs, so a `Code::Try` never reaches any of these
            // walkers. Round 14's `?` witness is fixed by Q6 below (the two
            // missing pattern propagators at the rvalue-match site), which is
            // where the shape actually arrives.
            //
            // KEPT, NOT DELETED, and the reason is the one this file's own
            // rule warns about: the consumer may be on the other side.
            // `lir_mirror.cpp`'s `emit_try_direct` can CONSTRUCT this node, so
            // a round-tripped or metaprog-emitted LIR can carry a Try that the
            // sema path never produces. An arm that agrees with the other
            // three costs nothing and keeps round 8's one-shape-enumeration
            // invariant true by inspection; deleting three arms on a corpus
            // that cannot reach them would be trading a provable invariant for
            // an unprovable absence. It is flagged here rather than pinned by
            // a test because NO fixture can reach it through the front end.
            case EC::Try:
                collect_ref_sources_paths(lir_view::ETryView{e}.inner(), path, out);
                return;
            case EC::Deref:
                // `*rr` names what `rr` names. The projection walk in
                // `ref_source_places` already treats a deref as transparent
                // (its `Code::Deref` step) and so does `taint_of`; this channel
                // fell to `default: return` and tied the result to nothing.
                // A RAW pointer deref is unchecked everywhere else in this
                // file, and stays unchecked here: a `Kind::Ptr` operand short-
                // circuits before the recursion, the same test the projection
                // walk spells as its `is_rawptr` lambda.
                //
                // ⚠ GATED ON THE DEREF'S OWN RESULT TYPE, and an admit-control
                // wrote this half: `{ let x = 5; r = &x; acc = *r; }` with
                // `acc: i64` REFUSED (pass/nll_borrow_not_used_after_scope,
                // the only red in 2088 L2 tests). Dereferencing a `&i64`
                // COPIES the value out — the result is a scalar and borrows
                // nothing — while `*rr` on a `&&i64` yields a reference that
                // does. `taint_of`'s Deref arm already spells exactly this
                // guard (`can_carry(t) ? taint_of(operand) : 0`); the arm
                // without it is not "the same transparency", it is a strictly
                // wider claim. Same predicate as the TupleIndex/IndexRead arms
                // above.
                if (!type_may_carry_borrow(e.type(pool))) return;
                {
                    lir_view::ExprRef op = lir_view::EDerefView{e}.operand();
                    TypeRef ot = op ? op.type(pool) : TypeRef(nullptr);
                    if (ot && ot.kind() == LogosType::Kind::Ptr) return;
                    collect_ref_sources_paths(op, path, out);
                }
                return;
            // A CLOSURE'S PROVENANCE LIVES IN ITS CAPTURES, and the captures
            // are not operands of the call — the same fact round 5 / H4 had to
            // state for the loan channel, restated for this one. The gate and
            // the capture lookup are H4's own (`closure_caps_of`, which answers
            // nullptr for a genuine fn pointer by the callee's TYPE KIND, not
            // by the expression code). A genuine fn pointer takes `Call`'s
            // argument rule instead — its result can only borrow what was
            // passed in — which is G1's answer in the loan channel.
            //
            // ⚠ EVERY CAPTURE, NOT THE ONES THE RESULT DERIVES FROM, AND THAT
            // IS AN OVER-REFUSAL WITH A MEASUREMENT. The Call arm two cases up
            // asks a FlowSummary which operands reach the result; a closure
            // body is never summarised, so this arm names them all. MEASURED
            // 2026-08-28 as a one-variable pair, both refused "cannot borrow
            // 'x' as shared: already mutably borrowed":
            //   |y: &mut i64| -> &mut i64 { set(&mut x); return y; }
            //   |y: &mut i64| -> &mut i64 { x = 2i64;   return y; }
            // legal Rust — the result derives from the PARAM — and the second
            // form is refused on the unpatched tree too, so the AddrOf
            // capture-mutability read that landed the same day WIDENED this
            // population rather than creating it. It is also the channel that
            // buys imported nll/issue-53040 and regions/regions-return-ref-to-
            // upvar-issue-17403, whose upstream reason is E0521 escape and not
            // this. The repair is a flow summary for a closure BODY; it is its
            // own round and it is not priced yet.
            case EC::ClosureCall:
            case EC::FnPtrCall: {
                TypeRef rt = e.type(pool);
                if (!is_ref_kind(rt) && !is_borrow_carrying_type(rt)) return;
                const auto* caps = closure_caps_of(call_callee(e));
                if (caps) {
                    for (auto& c : *caps)
                        if (var_has(NO_SLOT, c) && !param_names_.count(c)) emit(c);
                    return;
                }
                if (e.kind() == EC::FnPtrCall) {
                    // ── #79: THE FN-POINTER TWIN OF CLASS B ────────────────
                    //
                    // The three filters below are the summary-less rule, and
                    // they leave exactly the hole the direct-Call arm had
                    // before `arg_retained_by_callee`: a FAT by-value argument
                    // (`owner.as_str()` — a MethodCall of type `str`) is none
                    // of plain-ref, bc-by-name, or borrow-forming, so nothing
                    // was deposited. MEASURED at f0a60ff3
                    // (sandbox/escchan/r79.logos):
                    //   let f: fn(str) -> str = keep1;  v = f(owner.as_str());
                    // rc 0, while the IDENTICAL direct call `keep1(owner
                    // .as_str())` refuses with E0597. Same callee, same
                    // argument, same use — only the indirection differs.
                    //
                    // `flow_of_fnptr` is G1's own resolver: the initializer
                    // must be a known fn ITEM (`fnptr_sym_`), and a variable
                    // ever assigned two different fns is in `fnptr_multi_` and
                    // resolves to nullptr. So this is not "be conservative for
                    // ref-typed args through an unknown pointer" — that is the
                    // priced-and-not-taken option; it is "when the callee IS
                    // statically known, ask it", which is additive over
                    // today's answer and silent on every genuinely indirect
                    // call. The loan channel (prov_of / prov_of_retained)
                    // already resolved it this way; this arm did not.
                    //
                    // MEASURED by fire-print over one `stdlib/mem` module
                    // build (print then removed): 238 argument ties that ONLY
                    // this term admits (the other three predicates all answer
                    // no), and 0 reds — the arm is live and it costs nothing
                    // there. UNCOVERED: a pointer in `fnptr_multi_` resolves
                    // to nullptr and its dangle still admits
                    // (sandbox/escchan/r79_multi.logos, rc 0), and a
                    // ClosureCall's ARGUMENTS are still never consulted — a
                    // closure body is never summarised at all; its CAPTURES
                    // are read above and always were.
                    const FlowSummary* fs =
                        flow_of_fnptr(lir_view::EFnPtrCallView{e}.callee());
                    // ── #79 round 2: THE PRICED-AND-NOT-TAKEN OPTION, PRICED ─
                    //
                    // The paragraph above says this arm is "silent on every
                    // genuinely indirect call". That silence is the residual,
                    // and it is not a small one: `flow_of_fnptr` resolves ONLY
                    // a local whose initializer is a known fn ITEM, so the two
                    // shapes that dominate real callback code both fall
                    // through it. MEASURED at f0a60ff3, both rc 0:
                    //   pub fn run(f: fn(str) -> str) -> i64 {          // PARAM
                    //       let mut v: str = "";
                    //       { let owner = String::from("hello");
                    //         v = f(owner.as_str()); }
                    //       return v.len(); }
                    //   struct Cb { f: fn(str) -> str }                 // FIELD
                    //   let c = Cb { f: keep1 };  v = (c.f)(owner.as_str());
                    // Neither callee expression is a VarRef naming a
                    // single-assigned local, so `fs` is null and the three
                    // signature filters below never see a fat by-value `str`.
                    //
                    // A PARAMETER is not statically resolvable — and it cannot
                    // be rescued by a whole-program "every call site passes
                    // keep1" agreement either, because `run` is `pub`: a
                    // foreign caller may pass anything, so the agreement is
                    // over an OPEN set. The documented (d) route for an
                    // unresolvable indirect callee is therefore the one this
                    // takes: tie every operand that can carry a borrow, which
                    // is what a summary-less direct `Call` already does one
                    // arm below. It applies ONLY when `fs` is null, so every
                    // resolved pointer keeps its exact mask and this cannot
                    // narrow anything.
                    size_t idx = 0;
                    lir_view::EFnPtrCallView{e}.each_arg([&](lir_view::ExprRef a) {
                        size_t i = idx++;
                        if (a && (is_plain_ref_kind(a.type(pool)) ||
                                  is_borrow_carrying_type(a.type(pool)) ||
                                  forms_borrow_at_call(a) ||
                                  (!fs && is_ref_kind(a.type(pool))) ||
                                  arg_retained_by_callee(fs, i)))
                            collect_ref_sources_paths(a, path, out);
                    });
                }
                return;
            }
            case EC::Call: {
                // Free fn returning a borrow ties it to its ref args (elision).
                lir_view::ECallView v{e};
                // ── D1 round 14 / Q5: THE GATE WAS WRITTEN FOR bc NAMES ─────
                // `is_borrow_carrying_type` names Enum/Struct/ZonedStruct and
                // recurses type-args, but a bare `&i64` is none of those — so
                // `Option<&i64>` answered NO and a call returning an OPTION OF
                // A REFERENCE tied its result to nothing. Measured WITHOUT `?`
                // (so this is not the Try arm's problem): `fn pick(v: &i64) ->
                // Option<&i64>; { let x = 7; o = pick(&x); }` admitted the
                // dangling `o` at rc=0 while the direct-return twin `pickd(&x)
                // -> &i64` refused at rc=1. `type_may_carry_borrow` is round
                // 9/11's predicate for exactly this question (is_ref_kind OR
                // loan_carrying OR any type-arg), and round 13 / P1 already
                // moved TupleIndex and IndexRead onto it.
                //
                // This widens the arm's ENTRY only. What it then ties to is
                // unchanged — still only the args that are themselves plain
                // refs or bc — so a call returning `Option<i64>` from an i64
                // arg still ties nothing.
                if (type_may_carry_borrow(e.type(pool))) {
                    const FlowSummary* fs = flow_of_call(v.callee());
                    size_t idx = 0;
                    v.each_arg([&](lir_view::ExprRef a) {
                        size_t i = idx++;
                        // D1: by-value borrow-carrying args too (`id(c.mk())`).
                        if (a && (is_plain_ref_kind(a.type(pool)) ||
                                  is_borrow_carrying_type(a.type(pool)) ||
                                  forms_borrow_at_call(a) ||
                                  (logos::probe::on("bxsrc") &&
                                   a.kind() == EC::ClosureBox) ||
                                  arg_retained_by_callee(fs, i)))
                            collect_ref_sources_paths(a, path, out);
                    });
                }
                return;
            }
            // Ref-to-ref provenance chaining: `o = r` (r another reference
            // binding) makes o borrow whatever r borrows. Propagates sources so
            // an aliased borrow can't escape a referent's scope via a copy.
            case EC::VarRef: {
                std::string n(lir_view::EVarRefView{e}.name());
                // ── F-1: A CLOSURE PARAMETER IS A BORROW SOURCE ────────────
                // THE MISSING OBSERVATION. §B6 asks this walk "what does this
                // value borrow?", and for a reference bound by a CLOSURE
                // PARAMETER the answer was NOTHING — `ref_sources_under` finds
                // no record because a parameter is not a `let` and never went
                // through `record_ref_sources`. So `x = y` inside a closure
                // body, with `x` in the enclosing frame, deposited no source,
                // and `pop_scope` had nothing to find dying. That is E0521,
                // "borrowed data escapes outside of closure", and it is the
                // whole of what three ledger rows needed.
                //
                // The parameter's referent dies at the closure body's scope
                // exit as far as this checker can see, which is exactly the
                // fact F5/F6 already know how to spend: emit the parameter as
                // a source named by its own binding and the existing
                // `pop_scope` deposit reports at the first use past it, with
                // the local already named in the sentence.
                //
                // PRICED as `fpsrc` before it was written (PROBES.md, build
                // 98f66c0aebc5cc5d, gate-db 75 -> 76): 2 213 384 arrivals,
                // CEILING 3, corpus COST 0 — and `fpboth` proved the escape
                // channel (`prov_of`) adds neither a row nor a different row.
                //
                // ⚠ NARROWER THAN THE PROBE, TWICE, AND BOTH NARROWINGS ARE
                // LEGAL PROGRAMS THE PROBE REFUSED:
                //   · `names_live_closure_param`, not the name set — a body
                //     `let y` that SHADOWS parameter `y` denotes the shadow,
                //     and storing the shadow's own (outer) borrow outward is
                //     legal. ce5 in PROBES.md compiles unarmed and is refused
                //     by the probe.
                //   · recorded sources WIN. A parameter reassigned in the body
                //     (`y = &z;`) borrows what the assignment says, not
                //     itself; reporting `y` there would name the wrong binding
                //     and, when `z` outlives, refuse a legal program.
                auto srcs = ref_sources_under(n);
                if (srcs.empty() && is_ref_kind(e.type(pool)) &&
                    names_live_closure_param(n)) {
                    emit(std::move(n));
                    return;
                }
                for (auto& s : srcs) emit_src(s);
                return;
            }
            default:
                return;
        }
    }

    // Flat form — every source, path discarded. Used by the consumers that
    // only ask "does this expression borrow a local at all?".
    void collect_ref_sources(lir_view::ExprRef e,
                             std::vector<std::string>& out) const {
        std::vector<std::pair<std::string, RefSrc>> pairs;
        collect_ref_sources_paths(e, std::string{}, pairs);
        for (auto& pr : pairs)
            if (std::find(out.begin(), out.end(), pr.second.name) == out.end())
                out.push_back(pr.second.name);
    }

    // ── Field-path borrow operations (B83) ───────────────────────────────
    //
    // A path P is a prefix of path Q iff P == Q OR Q starts with P + ".".
    // Two borrows (a, b) conflict iff one path is a prefix of the other AND
    // at least one is mutable.
    static bool path_prefix_or_eq(const std::string& a, const std::string& b) {
        if (a.empty()) return true;  // whole-value covers everything
        if (b.size() < a.size()) return false;
        if (b.compare(0, a.size(), a) != 0) return false;
        return b.size() == a.size() || b[a.size()] == '.';
    }
    static bool paths_overlap(const std::string& a, const std::string& b) {
        return path_prefix_or_eq(a, b) || path_prefix_or_eq(b, a);
    }
    static std::string fmt_path(const std::string& target,
                                const std::string& path) {
        if (path.empty()) return target;
        return target + "." + path;
    }
    // True (and reports) if ACCESSING `target`(.`path`) collides with a tracked
    // FIELD borrow. The field-borrow records (mut_field_borrows /
    // shared_field_borrows) are consulted by take_field_borrow when a NEW borrow
    // is taken, but the use/move/read checks (consume, VarRef read, FieldRead)
    // used to ignore them — so `let r = &mut s.a; let s2 = s;` (move whole while
    // a field is borrowed) and `let r = &mut s.a; let x = s.a;` (read a field
    // while it is mut-borrowed) both slipped through (rustc E0505 / E0503).
    //   need_exclusive = the access mutates/moves (whole move, partial move);
    //     a plain read only collides with a MUT field borrow.
    //   empty `path`   = whole-value access — collides with ANY field borrow.
    //   `verb`         = shapes the diagnostic ("move", "use").
    bool field_borrow_conflicts(const VarState& st, const std::string& target,
                                const std::string& path, bool need_exclusive,
                                uint32_t line, const char* verb) {
        // THE WHOLE-VALUE BORROWS ARE BORROWS ON PATH "" — the invariant the
        // VarState declaration above states in words and this reader did not
        // implement. `let b = &a;` raises `shared_borrows` on the ROOT and
        // records nothing in either path map, so `let z = a.i;` (E0505, a
        // partial move under a live whole-value loan) answered "no conflict"
        // by looking only at the two maps. The 2026-08-28 clang enumeration
        // checked the OTHER direction — a root reader holding a path — and
        // correctly found 0; this is the inverse and it was never asked.
        // MEASURED as `fldrootbits` (PROBES.md): 5 302 137 arrivals, CEILING 1,
        // COST 0, re-priced unchanged across the 365 -> 337 shrink.
        // The empty borrowed path prints as the bare root, which is what
        // `fmt_path` already does — same wording as the two loops below.
        //
        // ⚠ THE `mut_borrowed` HALF OF THE PROBE IS DELIBERATELY NOT HERE, AND
        // THE GATE IS WHAT SAID SO. Armed, it buys ZERO ledger rows (334/334
        // green without it) and its whole effect on 1860 `bc` tests was to
        // REWORD TEN ALREADY-RED DIAGNOSTICS — check_live's whole-variable
        // reader already refuses every field access under a live `&mut` root
        // loan at every site the corpus reaches, and four of the ten kept
        // emitting its line as a SECOND error. Two names for one question is
        // the defect this file keeps recording. What is left open is a
        // DIAGNOSTICS task with its price named: teach the whole-var reader to
        // print the field path, in ONE reader, worth ten pinned texts.
        if (need_exclusive && st.shared_borrows > 0) {
            report(line, std::format(
                "cannot {} '{}' while '{}' is borrowed",
                verb, fmt_path(target, path), target));
            return true;
        }
        for (auto& p : st.mut_field_borrows) {
            if (path.empty() || paths_overlap(path, p)) {
                report(line, std::format(
                    "cannot {} '{}' while '{}' is mutably borrowed",
                    verb, fmt_path(target, path), fmt_path(target, p)));
                return true;
            }
        }
        if (need_exclusive) {
            for (auto& [p, c] : st.shared_field_borrows) {
                // ONE NAME PER SITE. `sharedzero_site`/`sharedzero_live` used
                // to name THIS loop, take_field_borrow_path_'s and
                // visit/AddrOfTemp's all three at once, so their fire counts
                // were one number over three places and could not be read.
                // Coverage map 2026-08-27 (8060 runs), THIS region:
                // 13 iterations, `c <= 0` true 1.
                (void)logos::probe::on("sharedzero_site_conflict");
                if (c <= 0 && !logos::probe::on("sharedzero_live_conflict")) continue;
                if (path.empty() || paths_overlap(path, p)) {
                    report(line, std::format(
                        "cannot {} '{}' while '{}' is borrowed",
                        verb, fmt_path(target, path), fmt_path(target, p)));
                    return true;
                }
            }
        }
        return false;
    }
    void take_field_borrow_path_(const std::string& target, uint32_t target_slot,
                           std::string path,
                           bool is_mut, uint32_t line,
                           TypeRef root_type = nullptr,
                           const std::string& holder = "",
                           bool implicit = false) {
        auto it = var_find(target_slot, target);
        if (it == nullptr) return;
        std::string self_disp = fmt_path(target, path);
        // Whole-value borrows still block everything.
        if (it->mut_borrowed) {
            if (implicit) return;   // RecordFlags::implicit — drop, do not say
            report(line, std::format(
                "cannot borrow '{}': '{}' is already mutably borrowed",
                self_disp, target));
            return;
        }
        if (is_mut && it->shared_borrows > 0) {
            report(line, std::format(
                "cannot borrow '{}' as mutable: '{}' has shared borrows",
                self_disp, target));
            return;
        }
        // Reference-typed root: `m.b` with m: &mut S projects THROUGH the
        // deref — mutation legality comes from the reference TYPE, not the
        // binding's `mut` (Rust parity; adversarial #2 p02/p10 — mirrors
        // the AddrOfTemp site's root_is_ref skip). A SHARED-ref root can't
        // give out &mut at all (Rust E0596).
        bool root_is_mut_ref = root_type &&
            TypeRef(root_type).kind() == LogosType::Kind::MutRef;
        bool root_is_shared_ref = root_type &&
            TypeRef(root_type).kind() == LogosType::Kind::Ref;
        if (is_mut && root_is_shared_ref) {
            report(line, std::format(
                "cannot borrow '{}' as mutable: '{}' is behind a `&` reference",
                self_disp, target));
            return;
        }
        // Mut binding check — N/A for reference-typed roots.
        if (is_mut && !root_is_mut_ref && !root_is_shared_ref &&
            !it->is_mut_binding) {
            logos::probe::census("mb.f.arrive");
            if (param_names_.count(target)) {
                logos::probe::census("mb.f.hatch");
                logos::probe::census(param_byval_.count(target)
                    ? "mb.f.hatch.byval" : "mb.f.hatch.ref");
            } else logos::probe::census("mb.f.refuse");
        }
        // PROBE mbparamvalf — the by-VALUE half of the hatch, field path.
        const bool byval_f_ = param_names_.count(target) &&
            param_byval_.count(target) &&
            (logos::probe::on("mbparamvalf") ||
             logos::probe::on("mbparamvalall"));
        if (is_mut && !root_is_mut_ref && !root_is_shared_ref &&
            !it->is_mut_binding && (!param_names_.count(target) || byval_f_)) {
            report(line, std::format(
                "cannot borrow '{}' as mutable: '{}' not declared as mut",
                self_disp, target));
            return;
        }
        // Check against tracked field borrows.
        // ── THE 144 ZERO-READS, AND WHY THE `c <= 0` SKIP IS NOT A
        // FUNDABLE DEFECT. History first, because both earlier verdicts here
        // were wrong in opposite directions. (1) The 2026-08-27 ledger
        // measurement (447 compiles: sharedzero_reach 36, sharedzero_site 0)
        // concluded "the loop never iterates" — false; the ledger simply has
        // no shared field borrows and cannot reach this population. (2) The
        // correction then nominated merge_loans' default-insert as the writer
        // that leaves the zeros. MEASURED 2026-08-28 — ALSO FALSE.
        //
        // Population = the coverage map's 8060 runs (corpus + lang/mem/lcm/std),
        // re-run today; `sharedzero_prod` swept as an armed probe over the same
        // population returned 576,021, the map's number to the unit, so the two
        // instruments are reading one population.
        //   THE READS, now one probe name per site:
        //     take_field_borrow_path_ (here)  325 iters, `c<=0` 144  (skip)
        //     field_borrow_conflicts           13 iters, `c<=0`   1  (skip)
        //     visit/AddrOfTemp                 18 iters, `c<=0`   7  (skip)
        //   Every one of the 144 is EXACTLY 0, never negative — dumped and
        //   counted, 144/144, in 38 programs (mem/lcm + tests/logos/pass
        //   data_*, adv_edge_trav_fuzz_vs, dview_*).
        //   THE WRITERS, one probe name per site, observational:
        //     producer `[path]++`                  576,021, always >= 1
        //     pop_scope decrement          575,653 arrivals; found a <=0 entry
        //                                  0 times; left a positive 1 time;
        //                                  every other one ERASED at <= 0
        //     loop_exit_snapshot decrement          0 arrivals
        //     place_write_loans decrement           0 arrivals
        //     merge_loans default-insert   785 iters; `cur = n` raise fires
        //                                  0 times; 264 FRESH inserts; source
        //                                  count `n <= 0` in 426 (all n == 0);
        //                                  leaves a <=0 in 426
        // So merge_loans NEVER raises: it only ever copies a zero that its
        // SOURCE already had, 264 times into a key the destination lacked. It
        // is a PROPAGATOR, not the origin. A single-compile write trace over
        // token_stream_basic.logos confirms it: the first `n == 0` merge
        // precedes every fresh insert and every decrement in the process, and
        // its source map was never written by any of the five sites.
        // ⚠ THE 144 ARE THEREFORE STILL NOT ACCOUNTED FOR. No writer in this
        // file produces the first zero. What is left is untraced whole-STATE
        // value flow (VarStore copy / copy-assign, which cannot CREATE a zero)
        // or a write this census cannot see because it enumerated by the
        // member's SPELLING. That is an open observation, narrowed, not a
        // defect and not a non-defect.
        //
        // WHAT IS SETTLED IS THE FUNDING QUESTION. The skip's only
        // implementable direction is to stop skipping, i.e. REFUSE MORE.
        //   CEILING: 0 over the acceptance ledger, which is structurally
        //     unable to reach it (no shared field borrows in any row).
        //   COST:    scripts/pass-probe.sh sharedzero_live_take --fast,
        //     2026-08-28 — fired 143 times in 38 of 4369 compiles, CHANGED 0
        //     programs, stdlib built clean. Not skipping the zeros changes no
        //     verdict anywhere we can measure: the entries carry 0, which
        //     MEANS "no live shared borrow", which is what the skip already
        //     implements.
        // Nothing to buy on either side, and refusing on a 0-valued entry
        // would be buying a row with a legal-program refusal. NOT FUNDABLE.
        // ⚠ COST 0 IS NOT A SAFETY CLAIM (probe.hpp's own rule): it says no
        // program IN THIS CORPUS moved, not that none could.
        // Also corrected: `shared_field_borrows` has FIVE read sites (this
        // one, field_borrow_conflicts, the `!empty()` test in this file's
        // place-write path, check_recv_conflict, and visit/AddrOfTemp), not
        // three; only three are probed.
        (void)logos::probe::on("sharedzero_reach");
        for (auto& [p, c] : it->shared_field_borrows) {
            // Coverage map 2026-08-27 (8060 runs), THIS region: 325
            // iterations, `c <= 0` true 144 — the 144 zero-reads. The other
            // two loops that used to share these two names are 13/1 and 18/7.
            (void)logos::probe::on("sharedzero_site_take");
            // OBSERVATIONAL, not behavioural: split the 144 by SIGN, and dump
            // the entry. A zero and a negative have different writers, and no
            // writer in this file can produce either (the producer is a `++`,
            // and all three decrements erase at <= 0). ⚠ THESE MUST SIT ABOVE
            // THE `continue` — placed below it they measured the 181
            // iterations that were NOT skipped and read 0/0, which is a
            // never-fired dressed as an answer.
            if (c == 0) (void)logos::probe::on("szr_take_zero");
            if (c <  0) (void)logos::probe::on("szr_take_neg");
            if (logos::probe::on("szdump_take") && c <= 0)
                std::fprintf(stderr, "SZDUMP take target=%s path=%s c=%d line=%u\n",
                             target.c_str(), p.c_str(), c, line);
            if (c <= 0 && !logos::probe::on("sharedzero_live_take")) continue;
            if (paths_overlap(path, p) && is_mut) {
                report(line, std::format(
                    "cannot borrow '{}' as mutable: '{}' is already borrowed",
                    self_disp, fmt_path(target, p)));
                return;
            }
        }
        for (auto& p : it->mut_field_borrows) {
            if (paths_overlap(path, p)) {
                if (implicit) return;   // RecordFlags::implicit — see above
                report(line, std::format(
                    "cannot borrow '{}': '{}' is already mutably borrowed",
                    self_disp, fmt_path(target, p)));
                return;
            }
        }
        // Record.
        if (is_mut) it->mut_field_borrows.insert(path);
        else      { (void)logos::probe::on("sharedzero_prod");
                    it->shared_field_borrows[path]++; }
        if (!scopes_.empty())
            scopes_.back().field_borrows.push_back(
                {target, std::move(path), is_mut, holder, target_slot,
                 {}, slot_of_binding(holder), {}});
    }

    // ── Borrow operations ─────────────────────────────────────────────────

    // Take a borrow of 'target'. Registers it in the current scope for cleanup.
    void take_borrow_whole_(const std::string& target, uint32_t target_slot,
                     bool is_mut, uint32_t line,
                     const std::string& holder = "",
                     bool skip_mut_binding_check = false,
                     bool implicit = false) {
        auto it = var_find(target_slot, target);
        if (it == nullptr) return;  // unknown / extern
        if (it->moved) {
            if (implicit) return;   // RecordFlags::implicit — drop, do not say
            report(line, std::format(
                "cannot borrow moved value '{}'", target));
            return;
        }
        // The branch above asks the WHOLE-variable `moved` flag and never
        // `moved_fields`, so borrowing a value one of whose fields has been
        // moved out was silent here while `recvpartial` refuses the identical
        // shape one route over, at the method-call receiver. Repair by
        // DELEGATION to `report_partial_move` — the reader that already owns
        // this question and its wording — rather than by a second name for it.
        // ⚠ ITS POPULATION IS MADE BY THE STRUCT-PATTERN REPAIR ABOVE. Measured
        // alone this site matched ZERO times, because until a shorthand field
        // got a type nothing ever put a struct pattern's field into
        // `moved_fields`. Neither half closes a row; the pair does.
        if (!implicit && !it->moved_fields.empty() &&
            report_partial_move(*it, target, line))
            return;
        if (is_mut) {
            // Reject &mut on a binding declared without `mut`.
            // Function params don't currently carry a mut bit in LParam,
            // so they're declared with is_mut_binding=false; we whitelist
            // them by checking known_params_ to avoid spurious diagnostics.
            // skip_mut_binding_check: the bare-receiver elision recorder
            // tracks EXCLUSIVITY only — binding-mut legality for bare
            // receivers stays the (permissive) status quo, the stdlib's
            // `arc.deref_mut()` on a non-mut Arc binding relies on it.
            if (!skip_mut_binding_check && !it->is_mut_binding) {
                logos::probe::census("mb.w.arrive");
                if (param_names_.count(target)) {
                    logos::probe::census("mb.w.hatch");
                    logos::probe::census(param_byval_.count(target)
                        ? "mb.w.hatch.byval" : "mb.w.hatch.ref");
                } else logos::probe::census("mb.w.refuse");
                // CEILING PROBES (C) — see PROBES.md. `mbsite` is the arrival
                // population with the mut bit absent; `mbhatch` is the subset
                // the param hatch exempts; `mbrefuse` is what the guard
                // actually refuses today; `mbnoparam` CLOSES the hatch.
                (void)logos::probe::on("mbsite");
                const bool hatched = param_names_.count(target) > 0;
                if (hatched) (void)logos::probe::on("mbhatch");
                // PROBE mbparamvalw — the by-VALUE half of the hatch.
                const bool byval_w_ = hatched && param_byval_.count(target) &&
                    (logos::probe::on("mbparamvalw") ||
                     logos::probe::on("mbparamvalall"));
                if (!hatched || byval_w_ || logos::probe::on("mbnoparam")) {
                    if (!hatched) (void)logos::probe::on("mbrefuse");
                    report(line, std::format(
                        "cannot borrow '{}' as mutable: not declared as mut", target));
                    return;
                }
            }
            // B83: any tracked field-path borrow blocks a whole-value mut.
            if (!it->mut_field_borrows.empty() ||
                !it->shared_field_borrows.empty()) {
                report(line, std::format(
                    "cannot borrow '{}' as mutable: field of '{}' is already borrowed",
                    target, target));
                return;
            }
            if (it->mut_borrowed) {
                report(line, std::format(
                    "cannot borrow '{}' as mutable: already mutably borrowed", target));
                return;
            }
            // B82: another mut reservation in flight is still a conflict —
            // Rust rejects f(&mut x, &mut x) too.
            if (it->mut_reservations > 0) {
                report(line, std::format(
                    "cannot borrow '{}' as mutable: already mutably borrowed", target));
                return;
            }
            // B82: inside fn-call arg evaluation, take the mut borrow as a
            // *reservation*. Shared reads of the same target in subsequent
            // args remain legal; the reservation is activated at call entry
            // (logically — we just leave it as reservation since the scope
            // pops after the call returns).
            if (in_call_args_ > 0) {
                // B82+: TPB reservation is compatible with shared borrows
                // taken *during* the same arg evaluation but NOT with
                // shared borrows pre-existing from outer scope. Detect
                // the latter: the current scope frame is the call-args
                // frame (pushed by visit_args); if shared_borrows > 0
                // and any of them was registered in an OUTER scope (not
                // current frame), reject.
                if (it->shared_borrows > 0) {
                    bool outer_shared = false;
                    if (!scopes_.empty()) {
                        // Count shared borrows recorded in the top frame.
                        int in_top = 0;
                        for (auto& br : scopes_.back().borrows)
                            if (br.target == target && !br.is_mut) ++in_top;
                        if (in_top < it->shared_borrows)
                            outer_shared = true;
                    } else {
                        outer_shared = true;
                    }
                    if (outer_shared) {
                        report(line, std::format(
                            "cannot borrow '{}' as mutable: {} shared borrow(s) active",
                            target, it->shared_borrows));
                        return;
                    }
                }
                it->mut_reservations++;
                if (!scopes_.empty())
                    scopes_.back().borrows.push_back(
                        {target, is_mut, holder, target_slot,
                         {}, slot_of_binding(holder), {}});
                return;
            }
            if (it->shared_borrows > 0) {
                report(line, std::format(
                    "cannot borrow '{}' as mutable: {} shared borrow(s) active",
                    target, it->shared_borrows));
                return;
            }
            it->mut_borrowed = true;
        } else {
            if (it->mut_borrowed) {
                if (implicit) return;   // RecordFlags::implicit — see its note
                report(line, std::format(
                    "cannot borrow '{}' as shared: already mutably borrowed", target));
                return;
            }
            // B83: a mut field borrow blocks whole-value shared borrows.
            if (!it->mut_field_borrows.empty()) {
                if (implicit) return;   // RecordFlags::implicit — see its note
                report(line, std::format(
                    "cannot borrow '{}' as shared: field of '{}' is mutably borrowed",
                    target, target));
                return;
            }
            ++it->shared_borrows;
        }
        if (!scopes_.empty())
            scopes_.back().borrows.push_back(
                {target, is_mut, holder, target_slot,
                 {}, slot_of_binding(holder), {}});
    }

    // ── THE ONE RECORD SITE ───────────────────────────────────────────────
    //
    // Every borrow this pass records goes through here. `take_borrow_whole_`
    // and `take_field_borrow_path_` are its two TAILS — the trailing
    // underscore says so, and `scripts/lint-record-borrow-monopoly.sh` makes
    // it a build failure rather than a review hope. The whole/field decision
    // and the §6.1 union widening are made HERE, once, from the BorrowPlace;
    // the six call sites in `take_ref_borrows` that used to spell
    // `if (!bp.path.empty()) field else whole` by hand no longer can, and the
    // four exemption spellings that used to sit beside them are one flag.
    //
    // ⚠ THE FIELD TAIL MUST NEVER RECEIVE AN EMPTY PATH. `FieldBorrow::path`
    // documents "empty for whole-value", but NO consumer honours it —
    // `mut_field_borrows[""]` is not checked on a bare variable read (the
    // ClosureBox arm's own comment says so verbatim). An empty-path field
    // record is therefore a PERMISSIVE defect by construction, and a
    // permissive defect is invisible to a green corpus. The dispatch below
    // cannot produce one; the assert is the proof, not the hope.
    void record_borrow(const BorrowPlace& bp, bool is_mut, uint32_t line,
                       const std::string& holder, RecordFlags fl = {}) {
        // ⚠ THE CEILING HARNESS'S KNOWN ANSWER, and it lives here on purpose.
        // scripts/ceiling-probe.sh reads closed rows off FAILING ledger tests;
        // a harness that has never SEEN a row close cannot tell "the hypothesis
        // is dead" from "my reader is broken". This probe refuses every borrow
        // the pass records, so its ceiling must be LARGE — if it ever comes
        // back small, the reader is what broke, not the tree.
        // ⚠ THE NULL POLE OF scripts/pass-probe.sh, and it sits at the SAME
        // site as selftest_refuse on purpose: whatever selftest_refuse can
        // break, selftest_inert is proven to reach — and it changes NOTHING.
        // A reader that reports changes for this one is inventing them, the
        // way a reader that reports none for selftest_refuse is blind.
        (void)logos::probe::on("selftest_inert");
        if (logos::probe::on("selftest_refuse")) {
            report(line, std::format("ceiling-probe: refusing borrow of '{}'",
                                     fmt_path(bp.root, bp.path)));
            return;
        }
        if (bp.root.empty()) return;
        // CEILING PROBE `movedborrow` — MEASURED 2026-08-27: fired 86 230
        // times across the 447 ledger compiles and closed ZERO rows. NEGATIVE
        // RESULT: the record side and the read side DO disagree about move
        // state (measured by hand), but no ledger row's admit depends on that
        // disagreement — every row whose conflict is a move-then-borrow is
        // already refused by check_live on the later read. Do not re-open.
        //
        // The hypothesis was: record_borrow is the monopoly entry
        // point and asks nothing about the MOVE state of what is borrowed,
        // while the READ side does (check_live). Two channels, one concept.
        if (logos::probe::on("movedborrow")) {
            if (auto* mst = var_find(bp.root_slot, bp.root); mst != nullptr) {
                if (mst->moved) {
                    report(line, std::format(
                        "ceiling-probe movedborrow: cannot borrow '{}': it is "
                        "moved", bp.root));
                    return;
                }
                if (!bp.path.empty())
                    if (auto* hit = find_moved_overlap(mst->moved_fields,
                                                       bp.path)) {
                        report(line, std::format(
                            "ceiling-probe movedborrow: cannot borrow '{}.{}': "
                            "field '{}' moved on line {}",
                            bp.root, bp.path, hit->first, hit->second));
                        return;
                    }
            }
        }
        // ── ONE EXEMPTION QUESTION, ASKED ONCE, OF THE RIGHT THING ────────
        // A `&mut` through a SHARED reference is E0596 and there is no
        // binding, root type or escape hatch that makes it legal. Asked HERE,
        // before the whole/field dispatch, so both tails answer the same —
        // take_field_borrow already refused this for a REF-TYPED ROOT
        // (`&mut s.a`, s: &S) and take_borrow never asked at all, which is
        // why `&mut *rx` (rx: &i64) and `&mut *h.r` (h.r: &i64) were
        // admitted: the reference crossed was not the root.
        // ⚠ THE REFUSING HALF ONLY. The mirror widening — letting a `&mut T`
        // hop EXEMPT the place from the binding-mutness check at every site —
        // is a PERMISSIVE change and is deliberately not made here; the
        // existing per-site exemptions keep their current reach.
        if (is_mut && bp.through_ref_type &&
            bp.through_ref_type.kind() == LogosType::Kind::Ref) {
            report(line, std::format(
                "cannot borrow '{}' as mutable: '{}' is behind a `&` reference",
                fmt_path(bp.root, bp.path), bp.root));
            return;
        }
        // §6.1 `items.union.ref.borrow`: a field borrow on a UNION root is
        // morally whole-value — all sibling fields alias.
        bool whole = bp.path.empty() || is_union_root(bp.root_type);
        MutBindBypass bypass;
        if (fl.ref_capacity) {
            auto* sit = var_find(bp.root_slot, bp.root);
            if (sit != nullptr && !sit->is_mut_binding &&
                !param_names_.count(bp.root)) {
                param_names_.insert(bp.root);
                bypass.set  = &param_names_;
                bypass.name = bp.root;
            }
        }
        if (whole) {
            take_borrow_whole_(bp.root, bp.root_slot, is_mut, line, holder,
                               fl.skip_mut_binding, fl.implicit);
        } else {
            assert(!bp.path.empty() &&
                   "record_borrow: the field tail may never receive an empty "
                   "path — no consumer checks mut_field_borrows[\"\"]");
            take_field_borrow_path_(bp.root, bp.root_slot, bp.path, is_mut,
                                    line, bp.root_type, holder, fl.implicit);
        }
    }


    // ── D1 round 3 / F3: the loan channel's OUT-PARAM rule ────────────────
    //
    // Round 2 closed two shapes where a borrow travels INTO a binding across a
    // call: door 8b (a by-value borrow-carrying ARGUMENT to a `&mut self`
    // method) and door F (its free-fn mirror, a by-value bc arg alongside a
    // `&mut` arg). Both read the CALL SITE, and both need a bc value to be
    // visible there. F3's residue has none: `fn stash2(c: &C, v: &mut Vec<B>)
    // { v.push(c.mk()); }` MANUFACTURES the B inside the callee out of its own
    // `&`-param, so `stash2(&c, &mut vs); c.bump(); *vs.get(0).p` compiled —
    // nothing at the call site is by-value bc, and no signature-only rule can
    // see it. The callee's BORROW-FLOW SUMMARY can: stash2 summarises as
    // `out1 <- {0}` (measured), i.e. param 0's loans reach out-param 1.
    //
    // The rule: for every (source i → out-param j) the summary reports, the
    // loans of operand i become held by operand j's root binding — recorded
    // through the SAME machinery the two earlier doors use (inherit_loans for a
    // loan the operand already holds, take_ref_borrows/record_only for one the
    // operand expression itself creates), not a fork of it.
    //
    // DIRECTION CONTROLS (both measured): f3_stash_leak must REFUSE, while
    // f3_scalar_control (`fn fill(v: &mut Vec<i64>, c: &C) { v.push(c.v); }`
    // — an i64 is copied, nothing is carried, summary `result<-0` with no
    // out-flow) and f3_stash_admit_twin (read BEFORE the mutation, so NLL
    // retires the loan) must stay ADMITTED.
    std::string flow_operand_root(lir_view::ExprRef a) const {
        using namespace lir_view;
        using Code = lir_schema::expr::Code;
        if (!a) return {};
        if (a.kind() == Code::AddrOf) return std::string(EAddrOfView{a}.var_name());
        bool dummy = false;
        return place_write_root(
            a.kind() == Code::AddrOfTemp ? EAddrOfTempView{a}.inner() : a, dummy);
    }
    // ── D1 round 2 Door F + round 3 F3, as ONE callable unit ──────────────
    // The elision half (a by-value borrow-carrying argument may be STORED
    // through a `&mut` argument) and the body-informed half (the callee's
    // summary says which arg reaches which out-param) were written inline in
    // the Code::Call arm. G1 needs BOTH at Code::FnPtrCall — including for a
    // pointer that does not resolve, which is precisely the case where only
    // the elision half can speak. Extracted verbatim; `ops` is the argument
    // list in order.
    void apply_call_outparam_rules(const std::vector<lir_view::ExprRef>& ops,
                                   const FlowSummary* fs, uint32_t line) {
        using namespace lir_view;
        using Code = lir_schema::expr::Code;
        const auto* pool = prog_.type_pool.impl();
        std::vector<std::string> bc_roots;
        std::vector<std::pair<std::string, uint32_t>> mut_roots;
        for (auto a : ops) {
            if (!a) continue;
            TypeRef at = a.type(pool);
            if (!is_ref_kind(at) && is_borrow_carrying_type(at)) {
                bc_hop_roots(a, bc_roots);
                continue;
            }
            if (at && at.kind() == LogosType::Kind::MutRef) {
                std::string mr;
                if (a.kind() == Code::AddrOf)
                    mr = std::string(EAddrOfView{a}.var_name());
                else {
                    bool dummy = false;
                    mr = place_write_root(
                        a.kind() == Code::AddrOfTemp
                            ? EAddrOfTempView{a}.inner() : a, dummy);
                }
                if (!mr.empty() && var_has(NO_SLOT, mr))
                    mut_roots.emplace_back(mr, 0u);
            }
        }
        for (auto& [mr, ms] : mut_roots) {
            (void)ms;
            for (auto& r : bc_roots) inherit_loans(r, mr, line);
        }
        if (!mut_roots.empty())
            for (auto a : ops) {
                if (!a) continue;
                TypeRef at = a.type(pool);
                if (is_ref_kind(at) || !is_borrow_carrying_type(at)) continue;
                // record_only: visit_args already visited the argument.
                take_ref_borrows(a, line, mut_roots.front().first,
                                 /*record_only=*/true);
            }
        apply_flow_outparams(fs, ops, line);   // D1 round 3 / F3
    }
    void apply_flow_outparams(const FlowSummary* fs,
                              const std::vector<lir_view::ExprRef>& ops,
                              uint32_t line) {
        if (!fs) return;                       // documented (a)-(d) hole
        const auto* pool = prog_.type_pool.impl();
        for (size_t j = 0; j < ops.size() && j < fs->nparams; ++j) {
            if (!fs->is_outparam[j] || !fs->to_outparam[j]) continue;
            std::string dst = flow_operand_root(ops[j]);
            if (dst.empty() || !var_has(NO_SLOT, dst)) continue;
            // ── D1 round 13 / P2: A DEPOSIT MUST FOLLOW THE REBORROW EDGE ──
            //
            // THE DEFECT (measured). `fn stash(h: &mut H, c: &C) { h.r.push(
            // c.mk()); } stash(&mut h, &c); c.bump(); *vs.get(0).p` ADMITS,
            // while all THREE isolating twins refuse: the callee's body
            // INLINED, the direct-param spelling `stash(&mut vs, &c)`, and the
            // same program with `h` used after `c.bump()`. The mask is
            // IDENTICAL on both sides of that split (`out0<-0x2` on the leak
            // and on the refusing direct-param twin), so the summarizer is
            // right and the loss is entirely here.
            //
            // The deposit above is keyed to the out-param's ROOT — `h` — and
            // that is all `h` is: a holder whose last use is the call itself,
            // so NLL retires the loan one line before `c.bump()`. But the
            // callee wrote through `h.r`, and `h.r` REBORROWS `vs`, whose last
            // use is the read after the mutation. The holder set has to say
            // so.
            //
            // THE RULE: the deposit's holders are the out-param root AND every
            // terminal root its sub-places reborrow, resolved through the ONE
            // resolve (`each_root_place`, prefix-aware since round 11 / X3 —
            // this is the checker-side twin of what X3 gave the summarizer).
            // Recorded as CO-HOLDERS through `inherit_loans`, which only ever
            // extends a loan's LIFETIME and never its strength, so it cannot
            // turn an admitted program into a refused one by itself; the
            // refusal comes from the loan still being live at the mutation.
            // The holder SET is resolved BEFORE the operand loop and applied
            // AFTER it, and both halves are load-bearing. Before, because A2's
            // prospective edges are added inside that loop and name the SOURCE
            // operand: resolving after made `c` a co-holder of its own loan,
            // which then never expires — 17 admit fixtures over-refused,
            // measured. After, because `inherit_loans` extends loan records
            // that already exist, and the deposit's record is created by the
            // `take_ref_borrows` inside the loop.
            std::vector<std::string> chased;
            auto chase = [&](const std::string& n) {
                std::string r = ref_place_root(n);
                if (r.empty() || r == dst) return;
                if (param_names_.count(r) || !var_has(NO_SLOT, r)) return;
                if (std::find(chased.begin(), chased.end(), r) == chased.end())
                    chased.push_back(r);
            };
            //
            // ONLY THROUGH A MUTABLE REBORROW, and this is the half a control
            // wrote. Following EVERY edge under the out-param made
            // pass/bc_d1r2_call_out_param_admits refuse: `stash(w: &mut Wrap,
            // x: B) { w.b = x; }` deposits into `w.b`, and `w`'s old value
            // held `w.b.p -> z`, so `z` — a plain `i64` local the deposit
            // cannot reach and whose last use is at the bottom of main —
            // became a co-holder and the loan never expired. A deposit travels
            // through `&mut` and nothing else.
            if (reborrow_mut_.count(dst)) reborrow_of_.each_root_place(dst, chase);
            std::vector<std::string> subs;
            reborrow_of_.each_under(dst, [&](const std::string& s) {
                if (reborrow_mut_.count(s)) subs.push_back(s);
            });
            for (auto& s : subs) reborrow_of_.each_root_place(s, chase);
            for (size_t i = 0; i < ops.size() && i < fs->nparams; ++i) {
                if (i == j || !(fs->to_outparam[j] & (1ull << i))) continue;
                lir_view::ExprRef src = ops[i];
                if (!src) continue;
                TypeRef st = src.type(pool);
                // ── #86 MISS 3: CONTAINER HOLDERS ─────────────────────────
                //
                // THE DEFECT (measured at the #86 landing, rc 0 for both):
                //   fn bad() -> Vec<str> { let o = String::from("hello");
                //     let mut v: Vec<str> = Vec::new();
                //     v.push(o.as_str()); return v; }
                //   fn bad() -> Vec<H>   { … v.push(H { v: o.as_str() }); … }
                // Same root as MISS 1 — the borrow enters the holder by a
                // MUTATION, here through the callee's out-param — and the #86
                // fixture set contained no container holder at all. The gate
                // itself opens (LOGOS_DUMP_RETGATE: mcb=1 for `Vec<str>` and
                // `Vec<H>` alike); `prov_` was simply never written.
                //
                // DELIBERATELY BEFORE THE LOAN FILTER BELOW, and with its own
                // gate. The filter's three predicates are the LOAN channel's
                // (`is_borrow_carrying_type` answers NO for `H { v: str }`,
                // which is why the `Vec<H>` spelling deposited no §B6 source
                // either — srcs=[] measured). Widening THAT filter would widen
                // inherit_loans / take_ref_borrows / the A2 alias edges in one
                // move — three rules in one control. The escape record asks
                // its own question with #71's `type_may_carry_borrow`, and
                // records only the escape fact.
                if (type_may_carry_borrow(st))
                    note_holder_escape_prov(dst, holder_ty_of(dst), src, line,
                                            "outparam");
                // Nothing to move if the operand cannot carry a borrow at all.
                if (!is_ref_kind(st) && !loan_carrying_type(st) &&
                    !is_borrow_carrying_type(st))
                    continue;
                std::vector<std::string> roots;
                bc_hop_roots(src, roots);
                for (auto& r : roots) inherit_loans(r, dst, line);
                // record_only: visit_args already visited the operand.
                take_ref_borrows(src, line, dst, /*record_only=*/true);
                // ── D1 round 12 / A2: THE PROSPECTIVE HALF ────────────────
                //
                // THE DEFECT (measured). `fn wire(t: &mut T, v: &mut Vec<B>)
                // { t.x = v; }` summarises `result<-0 out0<-0x2` — the TRUE
                // mask, so the loss is not in the summarizer. It is here:
                // everything above is RETROSPECTIVE (inherit_loans /
                // take_ref_borrows move loans the argument ALREADY carries),
                // so `wire(&mut t, &mut vs); t.x.push(c.mk()); c.bump();`
                // ADMITS while the same program with the loan raised BEFORE
                // the call refuses, and the direct spelling `t.x = &mut vs`
                // refuses too. The only difference between refusal and
                // admission was whether the borrow crosses the call — which is
                // precisely a missing ALIAS edge, not a missing loan.
                //
                // This is round 11 / X1 read in the other direction: X1 turned
                // a callee's `to_result` into a reborrow edge on the RESULT,
                // this turns its `to_outparam[j]` into one on the OUT-PARAM.
                // Same summary, same `if (!fs) return;` no-summary no-op above,
                // same additive-only argument: today's answer for `dst` is
                // whatever the syntactic walk recorded, the new edges only ADD
                // (RefGraph::add is monotone and refuses a self-edge), and with
                // no summary — pre-mono generics, extern/metaprog/indirect
                // callees — nothing changes at all. Additive edges push toward
                // OVER-refusal, so the sources still go through
                // `ref_sources_of`, i.e. through `ref_source_admissible`.
                //
                // The edge is recorded on the out-param's ROOT rather than on
                // the field the callee actually stored into: the mask names a
                // PARAMETER, not a place inside it (X1 met the same wall and
                // answered it the same way). Holding the whole struct reaches
                // every field of it, so this is a coarsening in the safe
                // direction — and the admit controls beside the witness are
                // what price it.
                for (auto& p : ref_sources_of(src))
                    reborrow_of_.add(dst, p);
                // ── #78: THE SCOPE-ESCAPE HALF OF THE SAME DEPOSIT ────────
                //
                // Everything above this line is the LOAN/EXCLUSIVITY channel:
                // it moves loans and alias edges so a later MUTATION of the
                // source is caught. Nothing in it answers the §B6 question —
                // "does `dst` outlive what it now borrows?" — because that
                // channel is `ref_borrow_sources_`, and only the assignment
                // and let sites ever wrote to it.
                //
                // THE DEFECT (measured, sandbox/escchan/r78.logos):
                //   fn set2(k: &mut K, s: str) { k.f = s; }
                //   { let owner = String::from("hello"); set2(&mut k, owner.as_str()); }
                //   k.f.len()
                // rc 0, while the DIRECT spelling of the same store,
                // `k.f = owner.as_str()`, refuses with E0597 naming `k`. The
                // `&mut self` method spelling admits too. The exclusivity
                // channel cannot see it: nothing MUTATES `owner` afterwards —
                // it is simply gone, and the read of `k.f` is a use of a
                // borrow whose referent has been dropped.
                //
                // The deposit is keyed to the out-param's ROOT for the same
                // reason A2's alias edge above is: the mask names a PARAMETER,
                // not a place inside it. Coarse in the safe direction, and
                // ADDITIVE — an argument that borrows no local contributes
                // nothing, so this cannot refuse a program whose sources are
                // all parameters or statics. MEASURED by fire-print over one
                // `stdlib/mem` module build (print then removed): 68 deposits,
                // 0 reds.
                //
                // ⚠ #77 round 2 — THE CLAIM IS NARROWER THAN THE NAME, and it
                // is re-scoped here rather than left to be read as more. This
                // deposit catches a BLOCK-scope escape: the out-param outlives
                // the block whose local it was handed. It does NOT catch the
                // FRAME escape — the callee storing a borrow of ITS OWN local
                // through the caller's `&mut`:
                //   pub fn set2(k: &mut K, s: str) { k.f = s; }
                //   pub fn wire(k: &mut K) {
                //       let o: String = String::from("hello");
                //       set2(k, o.as_str()); }              // rc 0
                // AND THAT IS NOT A #78 REGRESSION: the DIRECT control, the
                // same store written in `wire` itself with no callee at all
                // (`k.f = o.as_str();`), admits at rc 0 too — measured again
                // after this round's four repairs. The out-param channel is
                // faithfully reproducing what the base channel answers, so the
                // gap is that a write THROUGH a `&mut` parameter is not
                // treated as an escape past the frame, and closing it here
                // would be closing it in the wrong place.
                {
                    std::vector<std::string> escs;
                    collect_ref_sources(src, escs);
                    std::vector<std::pair<std::string, RefSrc>> pairs;
                    for (auto& s2 : escs)
                        pairs.emplace_back(std::string{},
                                           RefSrc{s2, slot_of_binding(s2)});
                    if (!pairs.empty())
                        store_ref_sources(dst, std::move(pairs), dst, line);
                }
            }
            for (auto& h : chased) inherit_loans(dst, h, line);
        }
    }

    // ── D1: loans follow the HOLDER graph ─────────────────────────────────
    //
    // Phase 9 (NLL) releases a loan when its holder's last use has passed.
    // With inheritance a loan may have several holders; it expires only once
    // ALL of them are past. Missing holders count as 0 (never-used binding),
    // exactly as the single-holder lookup did.
    // F5: one holder's last use, keyed by its IDENTITY when it has one.
    uint64_t one_holder_last_use(const std::string& name, uint32_t slot) const {
        if (slot == NO_SLOT) {   // no identity available — pre-F5 behaviour
            auto it = last_use_line_.find(name);
            return it == last_use_line_.end() ? 0u : it->second;
        }
        uint64_t lu = 0;
        if (auto it = last_use_slot_.find(slot); it != last_use_slot_.end())
            lu = it->second;
        // Uses that named the binding without a slot could be THIS binding;
        // they stay charged to it. Only uses that named a DIFFERENT slot of
        // the same name are dropped — which is exactly the shadowing defect.
        if (auto it = last_use_unslotted_.find(name); it != last_use_unslotted_.end())
            lu = std::max(lu, it->second);
        // Fire-count measured on f5_shadow_overrefuse: the discriminating
        // branch (name-keyed lu 15 > identity lu 12) fires twice, once per
        // release_dead_borrows sweep past the shadowing `let`.
        return lu;
    }
    uint64_t holders_last_use(const std::string& holder, uint32_t holder_slot,
                              const std::vector<std::string>& co,
                              const std::vector<uint32_t>& co_slots) const {
        uint64_t lu = one_holder_last_use(holder, holder_slot);
        for (size_t i = 0; i < co.size(); ++i)
            lu = std::max(lu, one_holder_last_use(
                co[i], i < co_slots.size() ? co_slots[i] : NO_SLOT));
        return lu;
    }
    template <class Rec>
    uint64_t holders_last_use(const Rec& r) const {
        return holders_last_use(r.holder, r.holder_slot,
                                r.co_holders, r.co_holder_slots);
    }

    // D1 (the by-value-hop defect): `from` holds one or more loans and a
    // borrow-carrying VALUE has just hopped out of it into `to` — `let b =
    // ob.unwrap()`, `let b = w.b`, `let b = s.get()`, `let b = id(c.mk())`.
    // The borrow is now reachable through `to`, so every loan `from` holds
    // must stay alive at least as long as `to`. This is the one mechanism the
    // loan channel lacked: a loan was always taken on the IMMEDIATE target and
    // never followed the holder graph, so it was NLL-released at the SOURCE
    // holder's last use while the extracted value was still live.
    //
    // Inheritance ADDS a holder to the existing record (it does not duplicate
    // it): the loan's *strength* is unchanged — only its lifetime extends —
    // which is why this can never turn an admitted program into a refused one
    // on its own. The refusal comes from the loan still being live at the
    // later mutation.
    // Is `name` already a holder of some live loan? Used to accept a binding
    // that the loan channel knows about but the VarState map does not (a match
    // ARM binding reached through take_ref_borrows, which does not declare
    // pattern bindings) — without widening the hop-root walk to arbitrary
    // unknown names.
    bool is_loan_holder(const std::string& name) const {
        for (auto& frame : scopes_) {
            for (auto& br : frame.borrows)
                if (br.holder == name ||
                    std::find(br.co_holders.begin(), br.co_holders.end(), name)
                        != br.co_holders.end()) return true;
            for (auto& fb : frame.field_borrows)
                if (fb.holder == name ||
                    std::find(fb.co_holders.begin(), fb.co_holders.end(), name)
                        != fb.co_holders.end()) return true;
        }
        return false;
    }

    void inherit_loans(const std::string& from, const std::string& to,
                       uint64_t /*line*/) {
        if (from.empty() || to.empty() || from == to) return;
        auto holds = [&](const auto& rec) {
            return rec.holder == from ||
                   std::find(rec.co_holders.begin(), rec.co_holders.end(), from)
                       != rec.co_holders.end();
        };
        uint32_t to_slot = slot_of_binding(to);   // F5
        auto add_to = [&](auto& rec) {
            if (rec.holder == to) return;
            if (std::find(rec.co_holders.begin(), rec.co_holders.end(), to)
                != rec.co_holders.end()) return;
            rec.co_holders.push_back(to);
            rec.co_holder_slots.push_back(to_slot);
            ++inherit_fired_;
        };
        for (auto& frame : scopes_) {
            for (auto& br : frame.borrows)       if (holds(br)) add_to(br);
            for (auto& fb : frame.field_borrows) if (holds(fb)) add_to(fb);
        }
    }
    mutable uint64_t inherit_fired_ = 0;   // debug: rule-fire counter

    // ── Ownership operations ───────────────────────────────────────────────

    // T1-10 (B78): dotted-path relation — two paths conflict when equal or
    // one is a dot-prefix of the other ("i" vs "i.s"). Reading a moved leaf,
    // a path inside a moved subtree, or a parent containing a moved leaf are
    // all uses of (partially) moved data; disjoint siblings ("i.t" vs "i.s")
    // do not conflict.
    static bool path_overlaps(const std::string& a, const std::string& b) {
        if (a == b) return true;
        if (a.size() < b.size())
            return b.compare(0, a.size(), a) == 0 && b[a.size()] == '.';
        return a.compare(0, b.size(), b) == 0 && a[b.size()] == '.';
    }
    // First moved entry overlapping `path` (linear scan — the map is tiny).
    static const std::pair<const std::string, uint32_t>*
    find_moved_overlap(const std::unordered_map<std::string, uint32_t>& moved,
                       const std::string& path) {
        for (auto& kv : moved)
            if (path_overlaps(kv.first, path)) return &kv;
        return nullptr;
    }
    // Re-initialisation of `path` clears equal AND deeper entries (writing
    // `o.i` refills `o.i.s`); a SHALLOWER moved entry stays — assigning
    // `o.i.s` does not resurrect the rest of a moved `o.i`.
    static void erase_reinit(std::unordered_map<std::string, uint32_t>& moved,
                             const std::string& path) {
        for (auto it = moved.begin(); it != moved.end(); ) {
            bool covered = it->first == path ||
                (it->first.size() > path.size() &&
                 it->first.compare(0, path.size(), path) == 0 &&
                 it->first[path.size()] == '.');
            it = covered ? moved.erase(it) : ++it;
        }
    }

    // ── THE PARTIAL-MOVE QUESTION, ASKED IN ONE PLACE ────────────────────
    // A whole-value MOVE (`consume`, below) and a whole-value USE through a
    // METHOD-CALL RECEIVER (visit()'s MethodCall arm) are the same fact reached
    // by two routes, and only the first of them ever asked it. Hoisted out of
    // `consume` rather than re-spelled at the second site: two walkers of one
    // fact that drift apart is a cost this file has already paid twice.
    // Returns true when it reported.
    bool report_partial_move(const VarState& vs, const std::string& name,
                             uint32_t line) {
        if (vs.moved_fields.empty()) return false;
        const auto& [fld, ln] = *vs.moved_fields.begin();
        report(line, std::format(
            "use of partially moved value '{}' (field '{}' moved on line {})",
            name, fld, ln));
        return true;
    }

    bool consume(const std::string& name, uint32_t line, uint32_t slot = NO_SLOT) {
        auto it = var_find(slot, name);
        if (it == nullptr) return true;
        if (report_partial_move(*it, name, line)) return false;
        if (it->moved) {
            uint32_t prev = it->moved_line;
            if (prev)
                report(line, std::format(
                    "use of moved value '{}' (moved on line {})", name, prev));
            else
                report(line, std::format("use of moved value '{}'", name));
            return false;
        }
        if (it->mut_borrowed || it->shared_borrows > 0 ||
            it->mut_reservations > 0) {
            report(line, std::format("cannot move '{}' while it is borrowed", name));
            return false;
        }
        // A live borrow of ANY field of `name` also blocks moving the whole
        // value (rustc E0505) — the move would invalidate the field reference.
        if (field_borrow_conflicts((*it), name, /*path=*/"",
                                   /*need_exclusive=*/true, line, "move"))
            return false;
        {
            // Value state resets, but the BINDING property `is_mut_binding`
            // persists across the move — a reinit (`nd = fresh`) of a
            // moved-from `let mut` binding must still admit `&mut nd`
            // (mirrors the Assign re-own; Rust keeps binding mut-ness).
            bool was_mut = it->is_mut_binding;
            (*it) = VarState{};
            it->is_mut_binding = was_mut;
        }
        it->moved = true;
        it->moved_line = line;
        return true;
    }

    // Visit the RECEIVER/BASE sub-expression of a place projection (`x.f`,
    // `x[i]`, `recv.method()`, `t.N`). This is place-FORMING, not a value-use:
    // the precise place was already conflict-checked at the projection site, so
    // the field-borrow VALUE-use checks must be suppressed while walking the
    // base — otherwise reading `w.writer` (whose `writer` field the call just
    // borrowed) would re-report whole-`w` as a conflicting use. Liveness/move
    // checks (check_live, moved_fields) are NOT gated by the flag, so they still
    // run on the base chain.
    void visit_place_base(lir_view::ExprRef e, uint32_t line) {
        bool saved = in_addr_source_;
        in_addr_source_ = true;
        visit(e, /*consuming=*/false, line);
        in_addr_source_ = saved;
    }

    // ── E0507 AT THE DEREF: `let s = *r;` ──────────────────────────────
    // Is a MOVE out of `*op` exempt? The rule itself lives in visit()'s
    // Code::Deref arm and is POSITION-GENERAL: `consuming` is computed once by
    // the caller and propagated, so every consuming position (let RHS, return,
    // call arg, block tail, destructure scrutinee) asks the same question. That
    // is deliberately NOT how sema asks it — `is_unowned_move_source`
    // (sema_impl.hpp) is consulted from five HAND-LISTED positions and its
    // Deref arm requires the operand to be spelled `VarRef`, so `**r`, `*u.a`
    // and `{ *r }` were all admitted for want of a spelling. A position-general
    // rule cannot miss a position.
    //
    // ⚠ TWO EXEMPTIONS, AND BOTH ARE DELIBERATE.
    bool deref_move_exempt(lir_view::ExprRef op) const {
        if (!op) return true;
        using Code = lir_schema::expr::Code;
        const auto* pool = prog_.type_pool.impl();
        // (1) A `static` READ is `Deref(VarRef("__static_addr:<sym>", *T))`
        // (sema_expr.cpp §6.2 S25) — a RAW-pointer operand BY SPELLING, but it
        // is not the unsafe raw-pointer idiom exemption (2) exists for: the
        // programmer never wrote a pointer, and rustc's answer is E0507
        // "cannot move out of static item". Tested BOTH ways: SEVEN
        // static-move ledger rows are this line (fail/bc_move_out_of_static_
        // fail is the core pin), and pass/bc_deref_move_exempt_admit — a Copy
        // static read by value, `&STATIC`, `STATIC.field` — is the control
        // that it does not eat the legal shapes.
        if (op.kind() == Code::VarRef &&
            lir_view::EVarRefView{op}.name().starts_with("__static_addr:"))
            return false;
        auto ot = op.type(pool);
        // (2) RAW-POINTER DEREF-MOVE IS A DOCUMENTED DIVERGENCE, NOT AN
        // OVERSIGHT. rustc rejects `*p` on a `*const T`/`*mut T` (E0507,
        // "behind a raw pointer"), and Logos deliberately admits it: it is how
        // logos.mem's ptr / Vec / Cell primitives legitimately move a value out
        // of memory they own the lifetime of (`let old = *p; *p = new;`).
        // is_unowned_move_source takes the same exemption for the same reason
        // and says so. Removing it here would refuse the stdlib, so the row it
        // costs (borrowck-move-from-unsafe-ptr) stays on the ledger, named.
        if (ot && ot.kind() == LogosType::Kind::Ptr) return true;
        // (3) A PRE-MONO GENERIC BODY CANNOT ANSWER THE QUESTION, and the
        // MONOMORPHISED copy can. `copy_tvs_` is built from the FUNCTION's own
        // type params, so an IMPL- or TRAIT-level `T: Copy` is invisible here
        // and `is_move_type` calls a bare `T` move by default (DIVERGENCES
        // §B1) — which is the sound default for the partial-move TRACKER it
        // was written for, and a refusal when read as a rule.
        // MEASURED: without this, `fn greater_than_one<T: NumExt>(n: &T) -> bool
        // { return *n > one; }` (tests/imported/pass/traits/inheritance-num1-b150)
        // is refused, and it is legal. Every instantiation is checked with a
        // CONCRETE type, so nothing is lost — `take<T>(r: &T) -> T { *r }` over
        // a non-Copy T still refuses, at `take$Own`.
        if (auto dt = deref_type_of_(op); dt && dt.kind() == LogosType::Kind::TypeVar)
            return true;
        // (4) A PATTERN-DESTRUCTURING `let`, WHOSE LOWERING DISCARDED THE
        // ANSWER. `let A { s } = *r;` becomes `let __dst_N: A = *r;` plus field
        // reads off the temp, so at this position the whole `A` is materialised
        // whatever the pattern binds. rustc's answer depends on exactly what
        // the lowering threw away: `let Fd(s) = *self;` over `struct Fd(u32)`
        // with a `Drop` impl is a rustc UI PASS test (nothing MOVES — `u32` is
        // Copy), while the same shape over a `String` field is E0507. Refusing
        // here reds tests/imported/pass/structs/newtype-struct-with-dtor,
        // MEASURED. So the position is left alone and the residual is named:
        // a destructure that binds a NON-Copy field out of a reference stays
        // admitted (tests/imported/admit/nll/move-errors--d keeps its row).
        // Closing it means carrying the pattern's move-ness to the temp's
        // `let`, which is a sema change and its own round.
        // CEILING PROBE `destrmove` — exemption (4)'s own NAMED residual: "a
        // destructure that binds a NON-Copy field out of a reference stays
        // admitted (tests/imported/admit/nll/move-errors--d keeps its row)".
        // ⚠ RULE 4 IN ADVANCE: the coverage map of 2026-08-28 reaches this
        // guard 2944 times over 8060 runs and takes it THREE times. A zero
        // here bounds almost nothing.
        if (in_destructure_temp_ && !logos::probe::on("destrmove")) return true;
        return false;
    }

    // The type `*op` yields — asked off the Deref's own node by the caller, so
    // this helper exists only to keep `deref_move_exempt` readable where the
    // operand is all it was handed.
    TypeRef deref_type_of_(lir_view::ExprRef op) const {
        const auto* pool = prog_.type_pool.impl();
        auto ot = op ? op.type(pool) : TypeRef(nullptr);
        if (!ot) return TypeRef(nullptr);
        if (ot.kind() == LogosType::Kind::Ref ||
            ot.kind() == LogosType::Kind::MutRef ||
            ot.kind() == LogosType::Kind::Ptr)
            return ot.pointee();
        return TypeRef(nullptr);
    }

    // The E0507 wording, taken from the SAME operand `deref_move_exempt`
    // judged — so the diagnostic can never name a different thing than the
    // rule refused. Mirrors rustc's four phrasings ("static item", "behind a
    // shared reference", "behind a mutable reference"); the bare fallback is
    // the user-`Deref`/`Index` call form, whose place has no name to print.
    std::string deref_move_message(lir_view::ExprRef op) const {
        using Code = lir_schema::expr::Code;
        const auto* pool = prog_.type_pool.impl();
        if (op && op.kind() == Code::VarRef) {
            std::string_view n = lir_view::EVarRefView{op}.name();
            constexpr std::string_view kPfx = "__static_addr:";
            if (n.starts_with(kPfx)) {
                // The VarRef carries the LINK symbol ("<pkg>$<NAME>"; an
                // extern-block decl keeps the bare name) — print the half the
                // programmer wrote.
                std::string_view sym = n.substr(kPfx.size());
                if (auto d = sym.rfind('$'); d != std::string_view::npos)
                    sym = sym.substr(d + 1);
                return std::format("cannot move out of static item '{}' (E0507)",
                                   sym);
            }
        }
        auto ot = op ? op.type(pool) : TypeRef(nullptr);
        if (ot && ot.kind() == LogosType::Kind::Ref)
            return "cannot move out of a value behind a shared reference (E0507)";
        if (ot && ot.kind() == LogosType::Kind::MutRef)
            return "cannot move out of a value behind a mutable reference (E0507)";
        return "cannot move out of a dereference (E0507)";
    }

    void check_live(const std::string& name, uint32_t line, uint32_t slot = NO_SLOT) {
        // §B6 (E0597): using a reference whose referent has gone out of scope.
        if (auto dit = dangling_.find(name); dit != dangling_.end()) {
            report(line, std::format(
                "'{}' does not live long enough: it is borrowed by '{}', which is "
                "used here after '{}' goes out of scope (E0597)",
                dit->second.source, name, dit->second.source));
            dangling_.erase(dit);   // report once per binding
        }
        auto it = var_find(slot, name);
        if (it == nullptr) return;
        if (it->moved) {
            uint32_t prev = it->moved_line;
            if (prev)
                report(line, std::format(
                    "use of moved value '{}' (moved on line {})", name, prev));
            else
                report(line, std::format("use of moved value '{}'", name));
        }
        if (it->mut_borrowed) {
            report(line, std::format(
                "cannot use '{}' while it is mutably borrowed", name));
        }
    }

    // ── #74's follow-up, found by ITS verify (MISS 1) ─────────────────────
    //
    // A WHOLE-VALUE READ of a variable conflicts with a live MUT FIELD loan on
    // it. `take_borrow`'s shared arm has said exactly that since B83 ("a mut
    // field borrow blocks whole-value shared borrows"), but a MATCH SCRUTINEE
    // never reaches `take_borrow`: it is visited non-consumingly, so the only
    // guard that runs is `check_live`, and `check_live` reads the
    // WHOLE-VARIABLE flag (`mut_borrowed`) alone.
    //
    // MEASURED on a one-token twin pair (`sandbox/adv_v3/{whole,field}_loan_match.logos`):
    // with the loan on the whole var (`let hp: &mut Dx = &mut d;`) the
    // scrutinee refuses through `check_live`; with the loan on a FIELD
    // (`let hp: &mut Arm = &mut d.a;`) the IDENTICAL `match &d { _ => … }` was
    // ADMITTED. The same program point refuses through `ro(&d)` and through
    // `let p: &Dx = &d`, so it is this spelling and no other. PRE-EXISTING —
    // the explicit `&mut d.a` was already in `mut_field_borrows` before D8 —
    // and invisible to the whole corpus, which contains no instance: the
    // permissive shape, invisible to a green corpus by construction.
    //
    // NARROW BY CONSTRUCTION: only a BARE variable scrutinee (optionally
    // through `&` / `&mut`-temp / transparent casts) is a whole-value read.
    // `match self.w.next_batch()` is a CALL and `match d.a { … }` is a FIELD
    // read — neither arrives here, which is why the direct emitter's own shape
    // (a field pull beside disjoint field uses) is untouched.
    void check_whole_read_vs_field_loans(lir_view::ExprRef e, uint32_t line) {
        using EC = lir_schema::expr::Code;
        lir_view::ExprRef cur = e;
        while (cur && cur.kind() == EC::Cast)
            cur = lir_view::ECastView{cur}.operand();
        if (!cur) return;
        std::string nm;
        uint32_t slot = NO_SLOT;
        if (cur.kind() == EC::AddrOf) {
            nm = std::string(lir_view::EAddrOfView{cur}.var_name());
        } else {
            if (cur.kind() == EC::AddrOfTemp)
                cur = lir_view::EAddrOfTempView{cur}.inner();
            while (cur && cur.kind() == EC::Cast)
                cur = lir_view::ECastView{cur}.operand();
            if (!cur || cur.kind() != EC::VarRef) return;
            nm = std::string(lir_view::EVarRefView{cur}.name());
            slot = lir_view::EVarRefView{cur}.var_slot();
        }
        if (nm.empty()) return;
        auto* it = var_find(slot, nm);
        if (!it || it->mut_field_borrows.empty()) return;
        // Spelling REUSED from `take_borrow`'s B83 arm rather than minted: the
        // two are the same fact reached by two routes, and a second wording
        // would make them look like two rules.
        report(line, std::format(
            "cannot borrow '{}' as shared: field of '{}' is mutably borrowed",
            nm, nm));
    }

    // ── Phase 4: provenance of a reference expression ─────────────────────
    //
    // Returns the set of function parameters the expression borrows from.
    // is_local = true  → at least one source is a local variable (dangling if returned).
    // params.empty() && !is_local → unknown/global — assumed safe (e.g. static data,
    //   or result of a function call where we don't track cross-call lifetimes).

    // LExprPtr is now lir_view::ExprRef — the expression IS its own mirror view.
    lir_view::ExprRef expr_ref(const LExprPtr& e) const {
        return e;
    }

    lir_view::PatRef pat_ref(const Pattern& p) const {
        auto& tbl = *prog_.mirror_table;
        auto it = tbl.pat.find(&p);
        if (it == tbl.pat.end()) return {};
        return lir_view::PatRef(prog_.type_pool.arena(), it->second);
    }

    // Match arm pattern bindings: PatVariantData injects each binding name into
    // scope; PatWild may also bind (when name is non-empty and not "_").
    void declare_pat_bindings(lir_view::PatRef pr) {
        if (!pr) return;
        using Code = lir_schema::pat::Code;
        switch (pr.kind()) {
            case Code::VariantData: {
                lir_view::PatVariantDataView v{pr};
                auto slots = v.bind_slots();  // Phase-1
                size_t i = 0;
                v.each_binding([&](std::string_view b) {
                    declare_var(std::string(b), i < slots.size() ? slots[i] : NO_SLOT);
                    ++i;
                });
                break;
            }
            case Code::Wild: {
                lir_view::PatWildView wv{pr};
                std::string n(wv.name());
                if (!n.empty() && n != "_") declare_var(n, wv.bind_slot());  // Phase-1
                break;
            }
            default: break;
        }
    }
    void declare_pat_bindings(const Pattern& p) {
        declare_pat_bindings(pat_ref(p));
    }

    // ── D1 round 5 / H5 (checker half): EVERY PATTERN KIND THAT BINDS ─────
    //
    // The three §B6 propagation channels below (sources, provenance, loans)
    // each opened with `if (pr.kind() != VariantData) return;` plus, in one
    // case, a Wild arm — the SAME two-of-thirteen shape the summarizer's
    // bind_pat had, and the same permissive default: a struct, tuple, slice,
    // or-, at-, ref- or ref-binding pattern propagated NOTHING and no green
    // program could ever show it. Enumerated from lir_schema::pat::Code
    // (Count == 13), exhaustive switch, no default — a 14th kind must not
    // silently rejoin the hole.
    //
    // `t` is the binding's declared type where the schema records one
    // (VariantData / Tuple binding_types, At::type, RefBind::bind_type) and
    // NULL where it does not (Wild, a struct-pattern shorthand field). The
    // channels gate on the type carrying a borrow; a NULL type is unknown, not
    // "scalar", so it is passed through and each channel decides.
    //
    // ⚠ THE ARMS ARE LOAD-BEARING, NOT DEFENSIVE — and proving it needed a
    // fixture that did not exist. Round 5 registered the H5 doors through a
    // CALLEE (`stash(&c, &mut v)` with the match inside), where the
    // summariser's own bind_pat decides the program: reverting THIS function
    // alone produced ZERO reds across the whole corpus. That is mutual
    // redundancy between two independently-correct rules, and one-at-a-time
    // control reverts cannot see it — each looks inert while the other covers.
    //
    // The discriminator has to (i) be intra-procedural, so no summary is
    // involved, and (ii) make a PATTERN BINDING the sole holder, so no named
    // source place refuses on its own. That is
    // fail/bc_d1r5_h8_match_named_twin: `let w = c.mk_wrap(); match w { Wrap
    // { b: got } => { inner = got; } } c.bump(); *inner.p`. Under this
    // function's pre-H5 shape (Wild + VariantData only) the Struct arm binds
    // nothing, `got` inherits no loan, and the program compiles. It reds under
    // the H5b revert and stays green under the H8 revert — the corpus's only
    // single-rule discriminator for this walk.
    template <class F>
    void each_pat_binding(lir_view::PatRef pr, F&& f) const {
        using namespace lir_view;
        using PC = lir_schema::pat::Code;
        if (!pr) return;
        const auto* pool = prog_.type_pool.impl();
        auto zip = [&](auto&& each_name, auto&& each_type) {
            std::vector<std::string> ns;
            std::vector<TypeRef>     ts;
            each_name([&](std::string_view b){ ns.emplace_back(b); });
            each_type([&](TypeRef t){ ts.push_back(t); });
            for (size_t i = 0; i < ns.size(); ++i)
                f(std::string_view(ns[i]), i < ts.size() ? ts[i] : TypeRef(nullptr));
        };
        switch (pr.kind()) {
            case PC::Variant: case PC::Int: case PC::Bool: case PC::Range:
                return;
            case PC::Wild:
                f(PatWildView{pr}.name(), TypeRef(nullptr));
                return;
            case PC::VariantData: {
                PatVariantDataView v{pr};
                zip([&](auto&& g){ v.each_binding(g); },
                    [&](auto&& g){ v.each_binding_type(pool, g); });
                return;
            }
            case PC::Tuple: {
                PatTupleView v{pr};
                zip([&](auto&& g){ v.each_binding(g); },
                    [&](auto&& g){ v.each_binding_type(pool, g); });
                v.each_sub([&](PatRef sub){ each_pat_binding(sub, f); });
                return;
            }
            case PC::Struct:
                PatStructView{pr}.each_field([&](PatFieldBindingView fb) {
                    if (auto sub = fb.sub()) each_pat_binding(sub, f);
                    else f(fb.field_name(), TypeRef(nullptr));
                });
                return;
            case PC::Or:
                PatOrView{pr}.each_alt([&](PatRef alt){ each_pat_binding(alt, f); });
                return;
            case PC::Slice: {
                PatSliceView v{pr};
                v.each_prefix([&](PatRef sub){ each_pat_binding(sub, f); });
                v.each_rest  ([&](PatRef sub){ each_pat_binding(sub, f); });
                v.each_suffix([&](PatRef sub){ each_pat_binding(sub, f); });
                return;
            }
            case PC::At: {
                PatAtView v{pr};
                f(v.name(), v.type(pool));
                if (auto sub = v.sub()) each_pat_binding(sub, f);
                return;
            }
            case PC::RefBind: {
                PatRefBindView v{pr};
                f(v.name(), v.bind_type(pool));
                return;
            }
            case PC::RefPat:
                if (auto in = PatRefPatView{pr}.inner()) each_pat_binding(in, f);
                return;
        }
    }

    // ── E0510 — THE PLACES A MATCH ACTUALLY TESTS ─────────────────────────
    //
    // A match guard runs with the scrutinee borrowed, so a guard may not
    // assign to it, take it by `&mut`, or move it (rustc E0510 / E0505). The
    // borrow rustc raises is a FAKE borrow of the places the match READS to
    // choose an arm — not of the scrutinee as a whole — and it is SHALLOW: a
    // shallow borrow of `a.b` does not conflict with a mutable borrow of
    // `a.b.c`. Both halves were bought with hand-written counter-examples that
    // the crude `guardscrutloan` probe (a whole-place loan, unconditional)
    // REFUSED although they are legal:
    //   `match p { _ if { p.b = 5; true } => … }`            — nothing tested
    //   `match p { P { a: 1, b: _ } if { p.b = 7; … } => … }` — sibling field
    //   `match t { (1, _) if { t.1 = 7; … } => … }`           — sibling elem
    // So this walk answers the narrow question rustc asks: WHICH PATHS under
    // the scrutinee place does some pattern compare? `out` is that set, as
    // root-relative dotted paths (the spelling `FieldWrite`/`TupleWrite` and
    // `fmt_path` already use), and it is EMPTY for a match that tests nothing
    // — which is the exemption the first counter-example above names.
    //
    // The union over ALL arms is what a guard holds, not the arm's own
    // pattern: `match b { B { n: 0 } => …, _ if eat(b) => … }` is upstream
    // E0382 precisely because the FIRST arm read `b.n`, and the guard that
    // moves `b` is on the second.
    //
    // ⚠ A STRUCT PATTERN WITH A *LITERAL* FIELD NEVER REACHES THE `Struct`
    // ARM AT ALL, and that is why `issue-27282-move-match-input-into-guard`
    // (`match b { B { n: 0 } => …, _ if eat(b) => … }`) STAYS ADMITTED. sema
    // rewrites `B { n: 0 }` into a `_`-binding to a synth name plus a
    // synthesised `__sfld_n_N == 0` GUARD on the arm (sema_stmt.cpp, the
    // `is_lit && current_pat_refutable_guards_` branch — the same idiom as a
    // string tuple element), so by the time this walk runs the pattern
    // compares nothing and the arm carries a guard nobody wrote. MEASURED, not
    // inferred: four hand-written programs over that shape (`b.n = 5`,
    // `&mut b.n`, whole-`b` assign, `eat(b)`) are all admitted, and the
    // collector prints `tested=0` for each. The decision that guard makes is
    // taken on a COPY the lowering made before the guard ran, so the hole is
    // narrower than it looks — but it is a hole, and it belongs to whoever
    // widens this to the places a SYNTHESISED guard reads.
    // The arm is nonetheless LIVE and load-bearing for the sub-patterns that
    // survive lowering (`W { e: E::A, n: _ }`): the loan lands on `w.e`, the
    // guard's `w.e = E::B` is refused, and its sibling `w.n = 5` is ADMITTED
    // under the same loan. Both directions measured by hand.
    // ⚠ SLICE PATTERNS DELIBERATELY TEST NOTHING HERE. Their read is a LENGTH
    // test on the whole array place, and the writes a guard could then make
    // (`a[0] = …`) are deeper projections that our index algebra cannot tell
    // apart from the borrowed place — refusing them would be exactly the
    // legal-program refusal this walk exists to avoid. Under-refusing is the
    // sound direction; no ledger row asks for it.
    static void add_tested_path_(std::vector<std::string>& out,
                                 const std::string& p) {
        if (std::find(out.begin(), out.end(), p) == out.end()) out.push_back(p);
    }
    static std::string join_tested_path_(const std::string& base,
                                         std::string_view seg) {
        if (base.empty()) return std::string(seg);
        return base + "." + std::string(seg);
    }
    void collect_tested_paths(lir_view::PatRef pr, const std::string& base,
                              std::vector<std::string>& out) const {
        using namespace lir_view;
        using PC = lir_schema::pat::Code;
        if (!pr) return;
        switch (pr.kind()) {
            // A binding (`x`, `_`, `ref mut x`) compares nothing.
            case PC::Wild: case PC::RefBind:
                return;
            // A discriminant / literal / range comparison READS `base`.
            case PC::Variant: case PC::VariantData: case PC::Int:
            case PC::Bool:    case PC::Range:
                add_tested_path_(out, base);
                return;
            case PC::Slice:   // see the note above — length test, not recorded
                return;
            case PC::RefPat:  // `&pat`: our place algebra roots at the
                              // reference variable, so the base is unchanged
                collect_tested_paths(PatRefPatView{pr}.inner(), base, out);
                return;
            case PC::At:
                collect_tested_paths(PatAtView{pr}.sub(), base, out);
                return;
            case PC::Or:
                PatOrView{pr}.each_alt([&](PatRef a) {
                    collect_tested_paths(a, base, out);
                });
                return;
            case PC::Struct:
                PatStructView{pr}.each_field([&](PatFieldBindingView fb) {
                    // A field with no sub-pattern is a plain binding.
                    if (auto sub = fb.sub())
                        collect_tested_paths(
                            sub, join_tested_path_(base, fb.field_name()), out);
                });
                return;
            case PC::Tuple: {
                // `subs` is ARITY-ALIGNED (sema pads `..` with `_` entries to
                // keep the fixed layout), so the ordinal IS the tuple index —
                // the same segment `Code::TupleWrite` spells with
                // `std::to_string(index)`.
                size_t i = 0;
                PatTupleView{pr}.each_sub([&](PatRef sub) {
                    collect_tested_paths(
                        sub, join_tested_path_(base, std::to_string(i)), out);
                    ++i;
                });
                return;
            }
        }
    }

    // §B6: a `match scrut { Variant(r) => … }` binds `r` to a piece of `scrut`;
    // if scrut carries borrows (e.g. `Option<&i64>` holding `&x`), the by-REF
    // binding inherits them so `o = r` can't smuggle the borrow past x's scope.
    // Gated on the binding's type being a reference / borrow-carrying — a
    // by-value binding copies out and carries no borrow (no false positive).
    void propagate_pat_sources(lir_view::PatRef pr,
                               const std::vector<std::string>& srcs, uint32_t ln) {
        if (!pr || srcs.empty()) return;
        each_pat_binding(pr, [&](std::string_view b, TypeRef t) {
            if (b.empty() || b == "_") return;
            // A NULL type is unknown, not scalar — the channel it feeds only
            // ever EXTENDS a source set, so passing it through is the
            // conservative reading.
            if (t && !is_ref_kind(t) && !is_borrow_carrying_type(t)) return;
            std::string n(b);
            erase_ref_sources_under(n);
            auto& dst = ref_borrow_sources_[n];
            dst.clear();
            for (auto& s : srcs) dst.push_back(RefSrc{s, slot_of_binding(s)});
            ref_borrow_line_[n] = ln;
        });
    }



    // D1 round 3 — the PROVENANCE counterpart of propagate_pat_sources /
    // propagate_pat_loans. A pattern binding is an EXTRACTION out of the
    // scrutinee, so it borrows exactly what the scrutinee borrows. Without
    // this the binding was a bare declared local with no prov_ entry, and
    // `value_local_root` therefore called it call-local storage — which only
    // started to matter once F0/F1 let a by-value borrow-carrying ARGUMENT
    // carry provenance: `fn scan_of(e: RExpr) -> RScan { match e {
    // RExpr::Filter(f) => { return scan_of(f.input_expr()); } … } }`
    // (stdlib/mem/wql) then reported the recursive call as returning a borrow
    // of the local `f`, for every arm. Measured on the full stdlib build.
    // Recording an EMPTY provenance is meaningful: it says "this binding's
    // provenance is known and it is the scrutinee's", which is what stops the
    // value-local fallback from guessing.
    void propagate_pat_prov(lir_view::PatRef pr, lir_view::ExprRef scrut) {
        using namespace lir_view;
        if (!pr || !scrut) return;
        const auto* pool = prog_.type_pool.impl();
        // GATED exactly like propagate_pat_sources: only a BORROW-CARRYING
        // scrutinee hands its provenance to its bindings. Two measurements
        // fixed this boundary, both on the full stdlib build:
        //   * ungated + propagating is_local reddened
        //     `Option<&[u8]>::unwrap_or_else` — `self` is by-value call-local
        //     storage, but the `&[u8]` pulled out of it is a COPY of a stored
        //     reference and returning it is legal (`Option<&[u8]>` is not
        //     borrow-carrying: a `&T` type-arg is Kind::Ref, not a bc NAME);
        //   * gating on "the scrutinee is not a value-local root" left
        //     `scan_of` red, because a by-value bc param IS such a root.
        // A plain owned local/param scrutinee is therefore left to
        // value_local_root, which keeps `match owned { S(f) => return &f.g }`
        // refusing.
        TypeRef st = scrut.type(pool);
        if (!is_borrow_carrying_type(st) && !loan_carrying_type(st)) return;
        RefProv sp = prov_of(scrut);
        auto bind = [&](std::string_view b) {
            if (b.empty() || b == "_") return;
            prov_[std::string(b)] = sp;
        };
        each_pat_binding(pr, [&](std::string_view b, TypeRef){ bind(b); });
    }

    // D1 (door 4): the LOAN counterpart of propagate_pat_sources. A pattern
    // binding EXTRACTS a piece of the scrutinee; when that piece carries
    // borrows, the binding is a new holder of every loan the scrutinee's own
    // holder bindings hold — otherwise the loan dies at the scrutinee's last
    // use (the `match` line) while the extracted value lives on past it
    // (`let b = match ob { Some(bb) => bb, … }; c.bump(); *b.p`).
    //
    // Gated on the BINDING's type, not the scrutinee's: a by-value binding of
    // a NON-borrow-carrying payload (`Some(n) => n` with n: i64) copies a
    // scalar out and inherits nothing, which is what keeps the payload-to-
    // scalar control admitted. By-REF bindings are covered too: a `&B` into
    // the scrutinee reaches the same arena.
    void propagate_pat_loans(lir_view::PatRef pr,
                             const std::vector<std::string>& roots, uint32_t ln) {
        if (!pr || roots.empty()) return;
        each_pat_binding(pr, [&](std::string_view b, TypeRef t) {
            if (b.empty() || b == "_") return;
            if (t && !is_ref_kind(t) && !is_borrow_carrying_type(t)) return;
            std::string n(b);
            for (auto& r : roots) inherit_loans(r, n, ln);
        });
    }

    // ── D1 round 13 / P0: A PATTERN BINDING NAMES A PLACE ─────────────────
    //
    // The PLACE-carrying twin of `each_pat_binding`: the same enumeration over
    // the same 13 pattern kinds, handing each binding the SUB-PLACE of the
    // scrutinee it is extracted from (`match h { H { r: s } => … }` with the
    // scrutinee at place "h" gives `s` the place "h.r").
    //
    // Where a pattern step names a component, the place is extended with that
    // component's segment — a field NAME for a struct pattern, the payload /
    // element INDEX for a variant or tuple pattern (numeric segments are the
    // spelling both channels already mint for tuples, and a source field name
    // can never be a numeral, so they cannot alias). Where a step does NOT
    // name one — a slice pattern, an `or`-alternative, a `ref`/`@` wrapper —
    // the place stays the container's, which is the whole-element convention
    // the array channel uses everywhere else: coarse, never invented.
    // ⚠ THE FOURTH CALLBACK ARGUMENT IS THE BINDING MODE — 0 by value, 1
    // `ref`, 2 `ref mut` — and it is a CARRIED FACT, read off
    // pat_keys::BINDING_REF_MODES / PatRefBind::is_mut, never inferred from
    // the binding's type. `Opt::Some(ref r)` and `E::V(p)` over `enum E {
    // V(&i64) }` hand the binding the SAME type `&i64`; only sema knows which
    // of the two names a place inside the scrutinee.
    template <class F>
    void each_pat_binding_place(lir_view::PatRef pr, const std::string& base,
                                F&& f, TypeRef cty = TypeRef{},
                                TypeRef wty = TypeRef{}) const {
        using namespace lir_view;
        using PC = lir_schema::pat::Code;
        if (!pr || base.empty()) return;
        const auto* pool = prog_.type_pool.impl();
        auto sub = [&](const std::string& seg) { return base + "." + seg; };
        // `cty` is the CONTAINER type the walk was seeded with, `wty` the type
        // a PC::Wild sub-pattern is to be handed. They are two parameters and
        // not one on purpose: the walk is seeded at its caller for EVERY
        // pattern, so consuming the seed at PC::Wild would give a top-level
        // `match x { n => ... }` binding a non-null type with nothing asking
        // for it -- a behaviour change in the baseline attributed to nothing.
        // Only the PC::Slice arm below sets `wty`, for the sub-patterns whose
        // element type it has just computed.
        auto elem_arr = [&](TypeRef t) -> TypeRef {
            if (t && t.kind() == LogosType::Kind::Array) return t.elem();
            return TypeRef{};
        };
        auto arr_n = [&](TypeRef t) -> uint64_t {
            for (int i = 0; i < 2 && t; ++i) {
                if (t.kind() == LogosType::Kind::Array) return t.arr_size();
                if (is_ref_kind(t)) { t = t.pointee(); continue; }
                break;
            }
            return 0;
        };
        auto zip = [&](auto&& each_name, auto&& each_type,
                       const std::vector<uint32_t>& modes) {
            std::vector<std::string> ns;
            std::vector<TypeRef>     ts;
            each_name([&](std::string_view b){ ns.emplace_back(b); });
            each_type([&](TypeRef t){ ts.push_back(t); });
            for (size_t i = 0; i < ns.size(); ++i)
                f(std::string_view(ns[i]), i < ts.size() ? ts[i] : TypeRef(nullptr),
                  sub(std::to_string(i)),
                  i < modes.size() ? static_cast<uint8_t>(modes[i]) : uint8_t(0));
        };
        switch (pr.kind()) {
            case PC::Variant: case PC::Int: case PC::Bool: case PC::Range:
                return;
            case PC::Wild:
                f(PatWildView{pr}.name(), wty, base, uint8_t(0));
                return;
            case PC::VariantData: {
                PatVariantDataView v{pr};
                zip([&](auto&& g){ v.each_binding(g); },
                    [&](auto&& g){ v.each_binding_type(pool, g); },
                    v.bind_ref_modes());
                return;
            }
            case PC::Tuple: {
                PatTupleView v{pr};
                // ⚠ ZERO, AND NOT BECAUSE A TUPLE ELEMENT CANNOT BE `ref`.
                // build_pattern's PAT_WILD tuple-element arm pushes the bare
                // NAME and a `make_pat_wild`, dropping IS_REF — so `(ref a, b)`
                // reaches the LIR as a by-value binding and no mode survives
                // for this walk to read. Recording 1/2 here would be inventing
                // a fact the tree does not carry; the dropped keyword is a
                // sema defect one door over, with its own paired fixture.
                zip([&](auto&& g){ v.each_binding(g); },
                    [&](auto&& g){ v.each_binding_type(pool, g); },
                    std::vector<uint32_t>{});
                // The sub-patterns' positions are not zipped with the binding
                // list, so they take the container's place (coarse).
                v.each_sub([&](PatRef s){ each_pat_binding_place(s, base, f); });
                return;
            }
            case PC::Struct: {
                PatStructView sv{pr};
                // ── A SHORTHAND FIELD USED TO ARRIVE WITH A NULL TYPE ────────
                // `f(name, TypeRef(nullptr), …)` reaches EVERY consumer of this
                // walk, and every one of them gates on the type: the by-value
                // sub-place move rule asks `is_move_type(t)`, so it skipped
                // `match x { Foo { f } => … }` exactly as it skips an array
                // pattern (`slicepatnull`, still open). A missing line has no
                // spelling, which is why no grep-defined class contained this.
                // The type is recoverable HERE and nowhere downstream: the
                // pattern carries the struct's NAME, and `ts_` already indexes
                // every def by it.
                // ⚠ A LOOKUP MISS MUST LEAVE THE OLD BEHAVIOUR. A generic
                // pattern carries the BASE name (`W`) while the def is stored
                // mono-mangled, so `sdef` stays empty and the field type stays
                // null — the same permissive answer as before, never a wrong one.
                std::optional<lir_view::StructView> sdef;
                {
                    std::string sn(sv.struct_name());
                    auto sit = ts_.struct_by_name.find(sn);
                    if (sit != ts_.struct_by_name.end()) sdef = sit->second;
                    else if (auto pit = ts_.spec_by_name.find(sn);
                             pit != ts_.spec_by_name.end()) sdef = pit->second;
                }
                sv.each_field([&](PatFieldBindingView fb) {
                    std::string fp = fb.field_name().empty()
                                       ? base : sub(std::string(fb.field_name()));
                    if (auto s = fb.sub()) { each_pat_binding_place(s, fp, f); return; }
                    TypeRef ft{};
                    if (sdef)
                        for (auto fd : sdef->fields())
                            if (fd.name() == fb.field_name()) { ft = fd.type(pool); break; }
                    f(fb.field_name(), ft, fp, uint8_t(0));
                });
                return;
            }
            case PC::Or:
                PatOrView{pr}.each_alt([&](PatRef a){ each_pat_binding_place(a, base, f); });
                return;
            case PC::Slice: {
                PatSliceView v{pr};
                // ── AN ARRAY PATTERN'S ELEMENT BINDING USED TO ARRIVE WITH A
                // NULL TYPE AND THE CONTAINER'S PLACE ────────────────────────
                // The sub-patterns are PC::Wild, whose arm passed
                // `TypeRef(nullptr)`, so `is_move_type` was false and the
                // by-value sub-place move rule skipped every element binding.
                // The element type is recoverable HERE and nowhere downstream:
                // it lives in the SCRUTINEE type, which the caller seeds.
                // ⚠ OWNED `[T; N]` ONLY -- no reference peel, no Kind::Slice.
                // Under match ergonomics a `&[T]` scrutinee binds its elements
                // BY REFERENCE, and handing those a move-typed element makes
                // the by-value rule consume the pointee: measured, and it
                // refuses `let s: &[String] = &a[..]; match s { [x,_,_] => ...
                // } use_s(s);`, which is legal. The decline IS the narrowing.
                // ⚠ AND THE PLACE MUST BE REFINED TOO, THOUGH IT SUBTRACTS.
                // With the element type but the CONTAINER's place a binding is
                // a WHOLE-VALUE use of the array, which refuses the plain
                // `match a { [p, q, r] => ... }` over `[String; 3]`. Index
                // segments make it a sub-place: prefix elements 0.., suffix
                // elements N-sc+j. `rest` names no single index, so it keeps
                // the container's place and no type -- coarse, never invented.
                const TypeRef aty = elem_arr(cty);
                if (aty) {
                    uint64_t i = 0;
                    v.each_prefix([&](PatRef s){
                        each_pat_binding_place(s, sub(std::to_string(i)), f, aty, aty);
                        ++i; });
                    v.each_rest  ([&](PatRef s){ each_pat_binding_place(s, base, f); });
                    const uint64_t n = arr_n(cty); const uint64_t sc = v.suffix_count();
                    uint64_t j = 0;
                    v.each_suffix([&](PatRef s){
                        if (n >= sc)
                            each_pat_binding_place(s, sub(std::to_string(n - sc + j)), f, aty, aty);
                        else
                            each_pat_binding_place(s, base, f, aty, aty);
                        ++j; });
                    return;
                }
                v.each_prefix([&](PatRef s){ each_pat_binding_place(s, base, f); });
                v.each_rest  ([&](PatRef s){ each_pat_binding_place(s, base, f); });
                v.each_suffix([&](PatRef s){ each_pat_binding_place(s, base, f); });
                return;
            }
            case PC::At: {
                PatAtView v{pr};
                f(v.name(), v.type(pool), base, uint8_t(0));
                if (auto s = v.sub()) each_pat_binding_place(s, base, f);
                return;
            }
            case PC::RefBind: {
                PatRefBindView v{pr};
                f(v.name(), v.bind_type(pool), base,
                  uint8_t(v.is_mut() ? 2 : 1));
                return;
            }
            case PC::RefPat:
                if (auto in = PatRefPatView{pr}.inner())
                    each_pat_binding_place(in, base, f);
                return;
        }
    }

    // ── A BY-REFERENCE PATTERN BINDING IS A LOAN ON THE SCRUTINEE ────────
    //
    // THE DEFECT (measured as a ceiling probe over the whole 447-row
    // acceptance population: 36 rows moved): the four pattern propagators —
    // sources, prov, loans, reborrows — all COPY facts the scrutinee already
    // carries onto the binding. Not one of them RAISES a loan. So
    // `match x { Opt::Some(ref r) => { x = Opt::Some(1); let _ = *r; } }`
    // compiled: `r` names the place `x.0`, nothing recorded a borrow of it,
    // and the assignment to `x` found no conflict (rustc E0506). The
    // by-value twin `let r = &x;` refuses, one line over.
    //
    // THE RULE: a `ref` / `ref mut` binding borrows the SUB-PLACE of the
    // scrutinee it names, shared or mutable as the keyword says, held by the
    // binding — which is exactly what `&scrut.0` / `&mut scrut.0` records at
    // every other site. `each_pat_binding_place` already computes the
    // sub-place and now carries the MODE beside it, so this consumes existing
    // machinery and mints no new key.
    //
    // ⚠ THE MODE IS THE GATE, NOT THE TYPE. Gating on "the binding's type is
    // a reference" would refuse `match e { E::V(p) => { e = …; *p } }` over
    // `enum E { V(&i64) }`, where `p` is a COPY of a stored reference and `e`
    // is not borrowed at all — the two spellings produce the identical
    // binding type `&i64`. The keyword is a carried fact; the type is not.
    //
    // ⚠ THREE SITES BIND A PATTERN AGAINST A SCRUTINEE AND ONLY TWO GET THIS.
    // stmt::Match and stmt::LetElse both `declare_pat_bindings` into a scope,
    // so a loan raised here has a holder that DIES — pop_scope releases it.
    // The rvalue `MatchExpr` arm inside `take_ref_borrows` declares no
    // bindings at all (its own comment says so), so a loan held by a name no
    // scope owns would never be released and would refuse every later use of
    // the scrutinee. That site needs the declaration first; it is a named
    // omission, not an oversight.
    //
    // The base place comes from `extract_borrow_place`, so the E0596 gate
    // (`ref mut` through a `&`), the root type and the Phase-1 slot are the
    // SAME answers every other record site gets — a scrutinee that is a
    // temporary (a call) yields an empty root and records nothing.
    // The names an arm's VALUE hands out of the arm. `extract_borrow_place`
    // answers the place chain (`a`, `&mut a`, `a.f`, `*a`); the arms above it
    // are the value-producing forms that have no place of their own, so the
    // walk reaches the places INSIDE them. A form not listed yields no name —
    // the conservative direction here, since a name absent from this list only
    // means the loan is not extended past the match.
    void arm_value_roots(lir_view::ExprRef e,
                         std::vector<std::string>& out) const {
        using Code = lir_schema::expr::Code;
        using namespace lir_view;
        if (!e) return;
        switch (e.kind()) {
            case Code::BlockExpr:
                arm_value_roots(EBlockExprView{e}.result(), out);
                return;
            case Code::IfExpr: {
                EIfExprView v{e};
                arm_value_roots(v.then_val(), out);
                arm_value_roots(v.else_val(), out);
                return;
            }
            case Code::MatchExpr:
                EMatchExprView{e}.each_arm([&](EMatchArmRef a) {
                    arm_value_roots(a.value(), out);
                });
                return;
            case Code::Cast:
                arm_value_roots(ECastView{e}.operand(), out);
                return;
            // ⚠ A BARE ARM VALUE IS AN `AddrOfTemp`, AND THAT COST A ROW.
            // `=> a` and `=> { a }` are the same program; the first arrives
            // here as AddrOfTemp(VarRef a) and the second as
            // BlockExpr(result=VarRef a). `extract_borrow_place` peels
            // AddrOfTemp only as the entry node of its own place walk, so
            // without this arm the bare spelling answered NO ROOT and the loan
            // was never raised. MEASURED: with the two spellings as the ONLY
            // variable, `borrowck-anon-fields-struct` stayed admitted bare and
            // closed block-wrapped — the row the ceiling probe (which asked no
            // such question) had predicted.
            case Code::AddrOfTemp:
                arm_value_roots(EAddrOfTempView{e}.inner(), out);
                return;
            case Code::StructLit:
                EStructLitView{e}.each_field_value(
                    [&](ExprRef f) { arm_value_roots(f, out); });
                return;
            case Code::TupleLit:
                ETupleLitView{e}.each_elem(
                    [&](ExprRef el) { arm_value_roots(el, out); });
                return;
            case Code::ArrLit:
                EArrLitView{e}.each_elem(
                    [&](ExprRef el) { arm_value_roots(el, out); });
                return;
            case Code::EnumLitData:
                EEnumLitDataView{e}.each_payload(
                    [&](ExprRef pl) { arm_value_roots(pl, out); });
                return;
            default:
                break;
        }
        BorrowPlace bp = extract_borrow_place(e, prog_.type_pool.impl());
        if (!bp.root.empty()) out.push_back(std::move(bp.root));
    }

    // ── `carried`: WHICH BINDINGS THE ARM VALUE ACTUALLY HANDS OUT ────────
    //
    // Only the rvalue-MatchExpr caller passes it, and only because it is the
    // one caller that redirects the loan onto a holder the BINDING IS NOT.
    // At the two statement sites the loan is held by the binding's own name,
    // whose scope ends with the arm, so a binding the arm value discards
    // releases itself. Redirect that loan to the enclosing `let`'s binding and
    // the release moves with it — to a name that is still live long after the
    // match — so a `ref mut` binding the arm merely READS would keep the
    // scrutinee borrowed for the holder's whole life.
    // MEASURED (hand-written, `sandbox/mepb/cx9_binding_discarded`): with the
    // ungated probe,
    //   let r: &mut i64 = match y { Y { f0: ref mut a, .. } => { *a = *a + 1; &mut z } };
    //   y.f0 = 5; *r = 9;
    // — legal, and admitted at HEAD — refused with "cannot borrow 'y.f0'".
    // Its shared twin (cx10) refused too. Neither shape exists anywhere in the
    // 807-program legal corpus, which is why the probe's COST read 0.
    // The gate is the same question the loan HOP asks one line later
    // (`take_ref_borrows(arm.value(), …)`): a loan needs to outlive the match
    // exactly when the value carries the name out.
    void propagate_pat_borrows(lir_view::PatRef pr, lir_view::ExprRef scrut,
                               uint32_t ln,
                               const std::string& holder_override = {},
                               const std::vector<std::string>* carried =
                                   nullptr) {
        if (!pr || !scrut) return;
        BorrowPlace base = extract_borrow_place(scrut, prog_.type_pool.impl());
        if (base.root.empty()) return;
        const std::string base_place = fmt_path(base.root, base.path);
        each_pat_binding_place(pr, base_place,
            [&](std::string_view b, TypeRef t, const std::string& place,
                uint8_t mode) {
                // ⚠ MODES 1 AND 2 ONLY — the WRITTEN `ref` / `ref mut`.
                // A default-binding-mode by-ref binding (3/4) names a place
                // under the scrutinee's IMPLICIT DEREF, and there is no
                // sub-place of the scrutinee EXPRESSION that spells it: over
                // `cur: &List`, `List::Cons(v, rest)` gives `rest` a borrow of
                // `(*cur).1`, not of `cur.1`. Recording it on `cur` made
                // `cur = &**rest` — a legal list walk, since assigning the
                // LOCAL cannot invalidate a borrow of its POINTEE — refuse,
                // MEASURED as two reds in tests/spec/pass (type_3, type_8) on
                // the run that first landed this rule. The ergonomic half
                // needs the loan keyed on the pointee, which is a read-side
                // change, not a wider gate here.
                // ── CEILING PROBE `patbyvalmove` — the largest permissive
                // early exit in the pattern channel: `mode == 0`, the BY-VALUE
                // binding, takes it 13,499,867 times in 8060 runs (this guard
                // is entered 14,124,171 times and its `mode > 2` disjunct is
                // evaluated only 624,304). The comment above explains why 3/4
                // are excluded and says nothing about 0 — it is not the loan
                // question. But nobody else asks it either: declare_pat_bindings
                // only DECLARES and there is no propagate_pat_moves, so a
                // by-value binding never consumes the sub-place it extracts.
                // ⚠ `probe::on` sits AFTER the `mode == 0` test on purpose: put
                // first it would count all 14.1M bindings and the fire count
                // would stop being the by-value population. `mode == 0` is a
                // side-effect-free integer compare, and a zero here cannot be a
                // dead site — the 13.5M coverage count refutes that
                // independently.
                // ── MEASURED 2026-08-28. 686,954 fires over 400 ledger
                // compiles, CEILING 8, corpus COST 0:
                //   borrowck_bindings-after-at-or-patterns-slice-patterns-box-patterns
                //   borrowck_borrowck-move-error-many-places--move-out-of-ref-in-match
                //   borrowck_borrowck-move-error-many-places--r-runtime
                //   borrowck_issue-41962--r25 · --t25
                //   borrowck_move-in-pattern-mut-in-loop
                //   nll_issue-53807--c-iflet-noloop · --move-in-loop
                // ⚠ AND THE COST 0 IS FALSE. Corpus silence, broken by hand on
                // the FIRST constructed try: a PARTIAL move, legal in Rust —
                //   struct H { a: Foo, b: P }
                //   match h.a { Foo::F1(p) => { o = p.n; } Foo::F2 => {} }
                //   o = o + h.b.n;
                // compiles today and is refused under the probe with "use of
                // moved value 'h'". `place_root(place)` consumes the ROOT, and
                // the pattern moved a SUB-PLACE. Six other hand-written legal
                // shapes (match ergonomics through a `&E`, or-pattern with one
                // binding, fresh value per loop iteration, reassign-then-reuse,
                // tuple destructure, move out of a Box) all stayed admitted, so
                // the over-refusal is precisely the whole-root consume and
                // nothing else. THE READING: the mechanism is not dead and the
                // ceiling is real, but the crude spelling cannot be shipped —
                // the move has to be recorded on the SUB-PLACE, with
                // arm-exclusivity. That is a day of work, and this number says
                // it is worth up to 8 rows.
                // ── CEILING PROBE `patbyvalsubmove` — the SUB-PLACE
                // re-spelling of `patbyvalmove` (whose whole-root consume was
                // its single measured over-refusal). VarState::moved_fields is
                // the existing dotted-path partial-move map; find_moved_overlap
                // and erase_reinit are its existing reader and invalidator, and
                // arm exclusivity is free because states_ is saved/restored per
                // arm at both match sites.
                // ── MEASURED 2026-08-28: 674,934 fires over 393 ledger
                // compiles, CEILING 2, COST 0.
                //   borrowck_bindings-after-at-or-patterns-slice-patterns-box-patterns
                //   nll_issue-53807--c-iflet-noloop
                // Against `patbyvalmove`'s recorded CEILING 8 over the same
                // population. RULE 7, measured in the SHRINKING direction: the
                // correct spelling closed a QUARTER of what the crude one did,
                // and the two it kept are the two the crude one could reach
                // honestly — an `a @ …` binding whose place IS the root (so it
                // takes the same whole-root consume), and a statement-level
                // `if let` pair where the sub-place record survives.
                // THE GATING COUNTER-EXAMPLE PASSES, and this time the test was
                // valid: `struct H { a: Foo, b: P }` / `match h.a { Foo::F1(p)
                // => { o = p.n; } … }` / `o = o + h.b.n;` traces
                // `place=h.a.0 root=h found=1` and is ADMITTED, where the
                // whole-root consume refused it. ⚠ The FIRST attempt at that
                // counter-example fired ZERO times and its silence was
                // meaningless: its payload struct had no `Drop` impl, so
                // `is_move_type` was false and the probe never ran. A
                // counter-example that does not reach the site proves nothing
                // — rule 1, pointed at the counter-example instead of the probe.
                // ⚠ WHY THE OTHER SIX DID NOT CLOSE — THE RECORD DOES NOT
                // SURVIVE CONTROL FLOW. `borrowck_issue-41962--r25` DOES fire
                // (twice, `place=maybe.0 root=maybe found=1`) and still admits:
                // both match sites restore `states_` per arm
                // (`states_ = guard_acc` / `states_ = saved_s`) and
                // `merge_loans` names `moved_fields` NOWHERE, so a partial move
                // recorded inside an arm is discarded at the arm boundary and
                // at the loop back edge. The root `moved` flag the crude
                // spelling set is evidently carried; the dotted-path map is not.
                // ── FOLLOWED UP 2026-08-28, and it was the LOOP EDGE, not the
                // arm boundary. Three probes over the same population:
                //   mfjoinbare (join, NO record)          25,096 fires, 0 rows
                //   mfjoinarm  (record + arm joins)      686,060 fires, 2 rows
                //   mfjoinloop (record + loop edges)     661,448 fires, 6 rows
                // The arm join buys NOTHING over the record alone; all four
                // extra rows cross `loop_propagate_moves`, hand-traced one at a
                // time with LOGOS_MFJ_TRACE=1. The loop half landed; the arm
                // half did not — see loop_propagate_moves for the join table.
                // ── A BY-VALUE PATTERN BINDING MOVES THE SUB-PLACE IT NAMES
                //
                // LANDED 2026-08-28 from the probe above, with ONE change: the
                // diagnostic wording. Every predicate, every branch and the
                // unconditional re-record are BYTE-IDENTICAL to what was
                // priced, because rule 7 says a crude probe and a correct fix
                // do not close the same programs — so the only way the
                // measurement transfers is if nothing but the message moves.
                //
                //   place == root  → the pattern binds the WHOLE local
                //                    (`x @ …`, `match x { y => … }`): a plain
                //                    whole-value consume, which is what
                //                    `patbyvalmove` did for EVERY place and is
                //                    why it over-refused.
                //   place  > root  → the pattern binds a SUB-place
                //                    (`Opt::Some(t)` names `m.0`): record it in
                //                    `moved_fields`, the existing dotted-path
                //                    partial-move map, and refuse a later
                //                    OVERLAPPING read. A disjoint sibling stays
                //                    usable, which is the whole point.
                //
                // ⚠ THE RVALUE-MATCH SPELLING IS UNCHECKED, and that is
                // measured, not assumed. `let r = match m { Opt::Some(t) =>
                // t.n, … };` produces NO record on `m` at all (hand-traced,
                // e1_match_rvalue / e3_match_rvalue_in_loop: zero `[pbsm]`
                // lines for the scrutinee, where the statement spelling one
                // token away emits one). The statement, if-let and let-else
                // spellings are the three that record. A silence, named here
                // so the next round does not read it as coverage.
                // CEILING PROBE `slicepatnull` — A SLICE/ARRAY PATTERN'S
                // ELEMENT BINDING ARRIVES WITH A NULL TYPE. each_pat_binding_
                // place's PC::Slice arm forwards `base` to each sub-pattern,
                // and those sub-patterns are PC::Wild, whose arm passes
                // `TypeRef(nullptr)` as the binding type — so `is_move_type`
                // is false and the landed by-value sub-place move rule
                // (`patbyvalsubmove`) skips every array-pattern binding.
                // Coverage map 2026-08-28: PC::Slice 111 arrivals, PC::Wild
                // 1512. Treat a null binding type as a move: crude, refusing.
                if (mode == 0 &&
                    !b.empty() && b != "_" && !place.empty() &&
                    (!t ? logos::probe::on("slicepatnull")
                        : is_move_type(t, prog_, ts_, &copy_tvs_))) {
                    const std::string mroot = place_root(place);
                    if (std::getenv("LOGOS_PBSM_TRACE"))
                        fprintf(stderr, "[pbsm] ln=%u b=%.*s place=%s root=%s found=%d\n",
                                ln, (int)b.size(), b.data(), place.c_str(),
                                mroot.c_str(), var_find(NO_SLOT, mroot) != nullptr);
                    if (mroot.size() == place.size()) {
                        (void)consume(mroot, ln);
                    } else if (auto* vs = var_find(NO_SLOT, mroot)) {
                        const std::string mpath = place.substr(mroot.size() + 1);
                        if (auto* hit = find_moved_overlap(vs->moved_fields, mpath))
                            report(ln, std::format(
                                "use of moved field '{}.{}' (moved on line {})",
                                mroot, hit->first, hit->second));
                        vs->moved_fields[mpath] = ln;
                    }
                }
                if (mode == 0 || mode > 2 ||
                    b.empty() || b == "_" || place.empty()) return;
                if (place.size() < base_place.size()) return;
                if (carried && std::find(carried->begin(), carried->end(),
                                         std::string(b)) == carried->end())
                    return;
                BorrowPlace bp = base;
                if (place.size() > base_place.size())
                    bp.path = base.path.empty()
                                ? place.substr(base_place.size() + 1)
                                : base.path + place.substr(base_place.size());
                record_borrow(bp, /*is_mut=*/mode == 2, ln,
                              holder_override.empty() ? std::string(b)
                                                      : holder_override);
            }, scrut.type(prog_.type_pool.impl()));
    }

    // ── D1 round 13 / P0: THE REBORROW COUNTERPART OF THE THREE ───────────
    //
    // THE DEFECT (three spellings, all measured rc=0 against a one-variable
    // field-read twin at rc=1): `match h { H { r: s } => { s.push(c.mk()); } }
    // c.bump(); *vs.get(0).p` COMPILES while `let s: &mut Vec<B> = h.r;`
    // REFUSES. A pattern binding fed `ref_borrow_sources_` (propagate_pat_
    // sources), `prov_` (propagate_pat_prov) and the loan channel
    // (propagate_pat_loans) — three of the four stores — and never fed
    // `reborrow_of_`. So the identity `s == h.r == vs` was lost at the arm,
    // and the loan raised through `s` never reached `vs`: a binding-extracted
    // `&mut` LAUNDERS its referent.
    //
    // THE RULE, and it is `note_reborrow`'s, not a new one: a binding that
    // extracts a place the graph ALREADY records as a reborrow IS that
    // reborrow. That is the whole gate — no type test, because a struct
    // pattern's shorthand field carries no declared type (a type gate would
    // have skipped the very witness), and because a place the graph does not
    // record cannot produce an edge here at all. It is additive over KNOWN
    // facts: the binding is a fresh name in a fresh scope, the sources come
    // from `reborrow_of_` itself, so nothing can be invented or strengthened.
    //
    // The sub-place half is `note_place_copy`'s, one door over: binding a
    // whole AGGREGATE out of a place makes the binding's sub-places name what
    // the source's did (`H2 { i: inn }` ⇒ `inn.r` names what `h.i.r` named).
    void propagate_pat_reborrows(lir_view::PatRef pr, lir_view::ExprRef scrut) {
        if (!pr || !scrut) return;
        const auto* pool = prog_.type_pool.impl();
        std::vector<std::string> bases;
        ref_source_places(scrut, pool, bases);
        if (bases.empty()) {
            // ── THE TEMPORARY SCRUTINEE (P0c, and `?` is the spelling) ─────
            //
            // `let s: &mut Vec<B> = pick(&mut vs)?;` desugars to a MATCH over
            // the call's result, so the leak is not the `?` node at all — it
            // is a pattern binding whose scrutinee is a CALL. A call names no
            // PLACE, so there is no sub-place to extend and the walk above
            // yields nothing; what the call result names is the borrow-flow
            // summary's answer, and `ref_sources_of` is where that answer
            // already lives (round 11 / X1).
            //
            // COARSE, deliberately: the binding is recorded naming what the
            // whole scrutinee names, with no segment appended — a `.0` under
            // `vs` would be an INVENTED place (`vs` is the referent, not the
            // Option). Same whole-value convention as the slice/or arms.
            // Gated on the binding's own declared type being a reference: a
            // by-value payload copies out, and an UNKNOWN (null) type is not
            // recorded here at all, because unlike the place branch below
            // there is no "the graph already knows this place" check to keep
            // it honest.
            std::vector<std::string> srcs = ref_sources_of(scrut);
            if (srcs.empty()) return;
            each_pat_binding(pr, [&](std::string_view b, TypeRef t) {
                if (b.empty() || b == "_" || !is_reborrow_ref_kind(t)) return;
                std::string n(b);
                std::vector<std::string> src = srcs;
                src.erase(std::remove(src.begin(), src.end(), n), src.end());
                if (src.empty()) return;
                freeze_ref_closure(n, src);
                reborrow_of_.set(n, std::move(src));
            });
            return;
        }
        // name -> the places it names, unioned over a multi-place scrutinee.
        std::vector<std::pair<std::string, std::vector<std::string>>> rec;
        auto slot = [&](const std::string& n) -> std::vector<std::string>& {
            for (auto& pr2 : rec) if (pr2.first == n) return pr2.second;
            rec.emplace_back(n, std::vector<std::string>{});
            return rec.back().second;
        };
        for (auto& base : bases)
            each_pat_binding_place(pr, base,
                [&](std::string_view b, TypeRef, const std::string& place,
                    uint8_t) {
                    if (b.empty() || b == "_" || place.empty()) return;
                    std::string n(b);
                    if (n == place) return;
                    if (reborrow_of_.find(place)) {
                        auto& v = slot(n);
                        if (std::find(v.begin(), v.end(), place) == v.end())
                            v.push_back(place);
                    }
                    // The aggregate half: copy the recorded SUB-places over.
                    std::string pfx = place + ".";
                    std::vector<std::pair<std::string, std::vector<std::string>>> adds;
                    for (auto& kv : reborrow_of_.e_)
                        if (kv.first.size() > pfx.size() &&
                            kv.first.compare(0, pfx.size(), pfx) == 0)
                            adds.emplace_back(n + kv.first.substr(place.size()),
                                              kv.second);
                    for (auto& a : adds) {
                        auto& v = slot(a.first);
                        for (auto& s : a.second)
                            if (std::find(v.begin(), v.end(), s) == v.end())
                                v.push_back(s);
                    }
                });
        for (auto& [n, s] : rec) {
            if (s.empty()) continue;
            std::vector<std::string> src = s;
            freeze_ref_closure(n, src);        // E0, exactly as note_reborrow
            reborrow_of_.set(n, std::move(src));
        }
    }

    // ── D1 round 5 / H8: a TEMPORARY scrutinee's own loan has NO HOLDER ───
    // `match c.mk_wrap() { Wrap { b: got } => … }`.
    //
    // A scrutinee that is a PLACE hands its bindings the loans it ALREADY
    // holds — that is propagate_pat_loans over bc_hop_roots, and it is the
    // only channel the three match-shaped sites had. A scrutinee that is a
    // TEMPORARY holds nothing yet: the loan its own evaluation takes (`&self`
    // elision on `mk_wrap`) was never taken AT ALL, because take_ref_borrows
    // — the sole site that turns an elided `&self` result into a loan — is
    // never called on a scrutinee. So the extraction out of a temporary
    // carried nothing, while the SAME program with the scrutinee named one
    // line earlier refused.
    //
    // MEASURED as two discriminating pairs on the pre-fix build (probe2.py,
    // /tmp/d1r6/probe2_prefix.json): stmt_assign_tmp rc=0 vs stmt_assign_named
    // rc=1, matchexpr_struct rc=0 vs matchexpr_named rc=1 — the ONLY variable
    // between the members of each pair is whether the scrutinee was named.
    // ATTRIBUTION MATRIX, measured by control revert with a proven restore
    // between the legs (md5 of both sources checked back to the fixed state):
    //
    //   fixture                            fixed   H5b-revert   H8-revert
    //   fail/…h8_match_tmp_scrutinee       refuse   ADMIT        ADMIT
    //   fail/…h8_matchexpr_tmp             refuse   ADMIT        ADMIT
    //   fail/…h8_match_named_twin          refuse   ADMIT        refuse
    //   the other 9 new fixtures            ok      ok           ok
    //
    // Both rules are individually load-bearing for the two temporary-scrutinee
    // doors: this one CREATES the loan, H5b's each_pat_binding hands it to the
    // Struct-pattern binding, and removing either re-opens them. The named twin
    // is green under THIS revert — which is what proves the restore between the
    // legs — and it is the corpus's only single-rule discriminator for H5b.
    //
    // The loan is taken ONCE per match construct, held by a synthetic name no
    // source binding can spell; the pattern bindings then inherit it through
    // the ordinary loan channel (the returned name is appended to the hop
    // roots the caller already computed). The synthetic holder has no
    // recorded use, so one_holder_last_use returns 0 for it and the loan's
    // lifetime is decided ENTIRELY by the real bindings that inherit it —
    // which is what keeps the admit twin (read through, THEN mutate)
    // compiling. Gated on the scrutinee being a temporary AND its type
    // carrying a loan: a scalar scrutinee (`match c.len() { n => … }`) and a
    // named-place scrutinee both return {} and change nothing.
    std::string retain_temp_scrut_loan(lir_view::ExprRef scrut, uint32_t ln) {
        if (!scrut || !is_temporary_value_expr(scrut)) return {};
        const auto* pool = prog_.type_pool.impl();
        // ── THE THIRD SITE, AND THE STRUCTURAL NOTION ────────────────────────
        // A temporary scrutinee's loan (`match heap.peek_mut() { Some(g) … }`)
        // comes from neither of B-10's two `let` gates, so nothing on that
        // path retained it. This gate asked `loan_carrying_type` — the
        // ATTRIBUTE-keyed carrier closure — where the question is structural
        // ("does this VALUE hold a loan"), the same two-notions-of-one-concept
        // split repaired at the other consumers: the narrow notion silently
        // won, and `Option<&mut i64>` is invisible to it because no
        // `#[borrow_carrying]` attribute names it.
        // ⚠ IT IS A PAIR, not one gate. `peek_mut()` returns `Option<&mut i64>`,
        // which `is_borrow_carrying_type` denies, so `is_self_borrowing` said no
        // and take_ref_borrows' MethodCall arm tied no receiver — this gate
        // would fire and record NOTHING. The `is_self_borrowing` result test is
        // the other half; neither closes a row alone.
        if (!loan_carrying_type(scrut.type(pool)) &&
            !type_may_carry_borrow(scrut.type(pool)))
            return {};
        std::string tmp = "__scrut_tmp_" + std::to_string(++scrut_tmp_seq_);
        take_ref_borrows(scrut, ln, tmp, /*record_only=*/true);
        ++scrut_retain_fired_;
        return tmp;
    }
    uint32_t scrut_tmp_seq_ = 0;
    mutable uint64_t scrut_retain_fired_ = 0;   // debug: rule-fire counter

    // Is this callee self-borrowing — reference `self`, reference result, AND no
    // explicit lifetime params (fully elided → Rust ties the output lifetime to
    // `&self`)? A method with explicit lifetimes MAY tie its result to an arg
    // (`fn pick<'a>(&self, x:&'a T)->&'a T`) → NOT self-borrowing (avoids the
    // over-borrow that broke persistent_showcase). See escape-analysis §4(a).
    bool is_self_borrowing(lir_view::FunctionView f) const {
        // Elision: `&self -> &T` borrows self. SO DOES `&self -> <BC type>`
        // (iter()/iter_mut() returning a borrowing iterator, WAny views):
        // the returned VALUE carries the receiver borrow (adversarial #2
        // f12 — two live iter_mut() were accepted, aliasing &mut).
        if (!f) return false;
        auto* pool = prog_.type_pool.impl();
        auto params = f.params();
        if (params.empty() || !is_ref_kind(params[0].type(pool)) ||
            !f.lifetime_params().empty())
            return false;
        TypeRef ret = f.ret_type(pool);
        if (is_borrow_carrying_type(ret)) return true;
        // The RESULT test was ATTRIBUTE-keyed where the question is structural
        // ("does the returned VALUE hold a loan"). `is_borrow_carrying_type`
        // answers only for types an attribute names; a `&self` method returning
        // `Option<&mut T>` or a tuple holding a `&mut` ties the receiver just as
        // surely. Measured ALONE this widening moves NOTHING in either
        // direction (180 arrivals, 0 rows, 0 legal programs) — it is the half
        // that makes the scrutinee gate above and the `if let` route have
        // anything to retain. BLAME IS PER SITE, CREDIT IS PER SET.
        if (!is_ref_kind(ret) && type_may_carry_borrow(ret))
            return true;
        if (is_plain_ref_kind(ret)) return true;
        if (!is_ref_kind(ret)) return false;
        // FAT result (str/&[T]/borrowed DST) with no lifetime syntax to
        // disambiguate: if the method ALSO takes a ref/fat non-self param, the
        // result may slice THAT instead of self (`Token::text(&self, src: str)
        // -> str` returns a piece of src — the token is just offsets). Tie to
        // self only when self is the sole borrow input; ambiguous → no tie
        // (lenient vs Rust elision; revisit with explicit lifetimes).
        for (size_t i = 1; i < params.size(); ++i)
            if (is_ref_kind(params[i].type(pool))) return false;
        return true;
    }

    // Does this method-call's RESULT reference borrow its receiver (by elision)?
    bool result_borrows_self(lir_view::EMethodCallView v) const {
        if (auto it = fn_index_.by_name.find(std::string(v.resolved_symbol()));
            it != fn_index_.by_name.end())
            return is_self_borrowing(it->second);
        // Operator-desugared / trait calls (`v[i]` → index, `*p` → deref) carry an
        // EMPTY resolved_symbol. Fall back to the unmangled method name: if EVERY
        // method with that name is self-borrowing, the result borrows self (the
        // Index/Deref/etc. trait contract). Conservative — any disagreeing
        // same-named method, or no match, → false.
        if (auto it = fn_index_.by_base.find(std::string(v.method()));
            it != fn_index_.by_base.end() && !it->second.empty()) {
            for (lir_view::FunctionView f : it->second)
                if (!is_self_borrowing(f)) return false;
            return true;
        }
        return false;
    }

    // Receiver self-kind of a method call: 0 = none/by-value, 1 = `&self`, 2 =
    // `&mut self`. Resolves via resolved_symbol, falling back to the method name
    // (all same-named methods must agree, else 0). Used to conflict-check bare-
    // place (VarRef) receivers, which the AddrOfTemp path doesn't see.
    int method_self_kind(lir_view::EMethodCallView v) const {
        auto* pool = prog_.type_pool.impl();
        lir_view::FunctionView f;
        if (auto it = fn_index_.by_name.find(std::string(v.resolved_symbol()));
            it != fn_index_.by_name.end())
            f = it->second;
        else if (auto it = fn_index_.by_base.find(std::string(v.method()));
                 it != fn_index_.by_base.end() && !it->second.empty()) {
            f = it->second.front();
            auto f_params = f.params();
            auto kind0 = f_params.empty() ? LogosType::Kind::Void
                                          : f_params[0].type(pool).kind();
            for (lir_view::FunctionView g : it->second) {
                auto g_params = g.params();
                auto k = g_params.empty() ? LogosType::Kind::Void
                                          : g_params[0].type(pool).kind();
                if (k != kind0) return 0;  // ambiguous
            }
        }
        if (!f) return 0;
        auto f_params = f.params();
        if (f_params.empty()) return 0;
        TypeRef p0 = f_params[0].type(pool);
        auto k = p0.kind();
        if (k == LogosType::Kind::MutRef) return 2;
        if (k == LogosType::Kind::Ref)    return 1;
        // A `#[self_describing]` struct's `&self`/`&mut self` canonicalises
        // to a non-owning DstRef (thin) — same borrow semantics as Ref/MutRef.
        // Without this, `&mut self` methods on DST structs (resize_block_at)
        // skipped the receiver conflict check and a live slot view survived
        // the resize (the pkd invalidation contract).
        if (k == LogosType::Kind::DstRef && !p0.owning_dst())
            return p0.mut_ptr() ? 2 : 1;
        return 0;
    }

    // ⚠ `method_self_kind` RETURNS 0 FOR THREE DIFFERENT FACTS, and only one
    // of them is "the receiver is consumed": a genuine by-value `self`, a
    // callee it could NOT RESOLVE, and an AMBIGUOUS overload set. Every other
    // consumer of the 0 uses it as "not a borrow", where conflating them is
    // conservative; a CONSUMING-position rule reads it in the opposite
    // direction, where the conflation refuses legal code. Measured 2026-08-30:
    // reading the bare 0 as by-value refused NINE `logos.mem` functions
    // (`target.set(...)` on a `&mut Vec<u16>`, whose callee this index does
    // not resolve) and broke the stdlib build. Rule 16 — "no fact recorded"
    // and "the fact is absent" are different, and only the minting site tells
    // them apart.
    bool method_self_by_value(lir_view::EMethodCallView v) const {
        lir_view::FunctionView f;
        if (auto it = fn_index_.by_name.find(std::string(v.resolved_symbol()));
            it != fn_index_.by_name.end())
            f = it->second;
        else if (auto it = fn_index_.by_base.find(std::string(v.method()));
                 it != fn_index_.by_base.end() && it->second.size() == 1)
            f = it->second.front();
        if (!f || f.params().empty()) return false;   // UNRESOLVED / ambiguous
        return method_self_kind(v) == 0;              // resolved, and by value
    }

    // Whole-root conflict check for a MUTABLE USE of a whole-var place. Three
    // consumers: a method call's bare-place receiver borrowing `self`, the
    // SD-DST Call arg0 site, and the DerefWrite arm's bare-pointer spelling
    // (`*r = v`, whose place decomposes to root=r with an empty path). Only
    // whole-var places (empty path) are checked (conservative — field places
    // are refused by visit()'s AddrOfTemp arm instead). Raw-ptr roots are
    // unchecked (Rust parity); reference roots ARE checked (a `&mut self` call,
    // or a write, through a `&mut` ref var still conflicts with a live borrow
    // of it). NOT a recorder: it takes no borrow, so widening its call set
    // cannot manufacture a shape a later consumer must recognise.
    void check_recv_conflict(const BorrowPlace& bp, bool is_mut, uint32_t line) {
        if (bp.root.empty()) return;
        if (!bp.path.empty()) {
            // THE ROOT-KEYED GATE NOW ANSWERS A PROJECTION instead of bailing.
            // Isolated on one variable: `let v: Vec<i64>; let e = &v[0];
            // v.push(1);` is refused, and the byte-equivalent over a FIELD
            // (`let e = &t.v[0]; t.v.push(1);`) compiled — this bail was the
            // only difference. The field maps are exactly where a field place's
            // loans live, so the question is a delegation, not a new rule; the
            // whole-var arm below is unchanged and still owns the empty path.
            // MEASURED as `recvfieldpath` (PROBES.md): 91 arrivals, CEILING 1,
            // COST 0, re-priced unchanged on the 337-row ledger.
            if (auto* fst = var_find(bp.root_slot, bp.root))
                field_borrow_conflicts(*fst, bp.root, bp.path,
                                       /*need_exclusive=*/is_mut, line,
                                       "call on");
            return;
        }
        if (bp.root_type && bp.root_type.kind() == LogosType::Kind::Ptr) return;
        auto sit = var_find(bp.root_slot, bp.root);
        if (sit == nullptr) return;
        // ── CEILING PROBE `recvmutbind` — ONE RULE AT TWO SPELLINGS, and only
        // one has it. `let f: Foo = ...; f.bump();` (AddrOfTemp receiver) is
        // refused by take_borrow_whole_'s binding-mut arm; the byte-equivalent
        // over a bare-place receiver (`let v: Vec<i64> = Vec::new(); v.push(1);`)
        // compiles. check_recv_conflict has no binding-mut arm at all.
        // ── MEASURED 2026-08-28 (re-priced on this tree): 205 fires over 389
        // ledger compiles, CEILING 1, COST 0 — exactly the predicted row,
        // borrowck_many-mutable-borrows (E0596).
        // ⚠ THE PREDICTED HAZARD DID NOT MATERIALISE. The aiming report
        // expected a LARGE cost, on the reading that `skip_mut_binding_check`'s
        // comment makes the bare-receiver permissiveness load-bearing for the
        // stdlib (`arc.deref_mut()` on a non-mut Arc binding). Over 807 bc/pass
        // tests plus three spec dirs it costs nothing.
        //
        // ⛔ DECLINED 2026-08-28, AND NOT BY THE CORPUS — BY A HAND-WRITTEN
        // COUNTER-EXAMPLE, which is the only thing a COST 0 has ever been
        // refuted by. `tests/logos/pass/bc_recvmutbind_pattern_mut_binding.logos`
        //     match e { E::A(mut v) => { v.push(1i64); }, E::B => {} }
        // is legal Rust, compiles today, and this arm REFUSES it — the probe
        // fired once and reported "cannot borrow 'v' as mutable: not declared
        // as mut". `mut` written in a PATTERN never reaches `is_mut_binding`:
        // declare_pat_bindings calls declare_var and the LIR pattern schema has
        // no by-value-`mut` key at all (IS_MUT is PatRefBind/PatRefPat only;
        // BIND_MODES 0 means "by value" whether or not `mut` was written). So
        // the binding-mut question cannot be asked correctly at ANY spelling
        // until sema carries that bit.
        //
        // ⚠ AND THE SAME OVER-REFUSAL IS ALREADY LANDED AT THE OTHER SPELLING.
        // Measured beside it: `match e { E::A(mut f) => { f.bump(); } }` over a
        // struct receiver, and `... => { let r = &mut f; }`, are refused TODAY
        // by the AddrOfTemp/AddrOf mut-binding arms. Arming this probe would
        // not introduce a new class of wrongness; it would spread an existing
        // one to the last receiver spelling where such programs still compile.
        // That is still a legal-program refusal, so the row is not bought.
        // THE PREREQUISITE, named so the next round does not re-derive it: a
        // per-binding by-value-`mut` flag on PatVariantData/PatWild, parallel
        // to BINDINGS, set by sema and read by declare_pat_bindings. With that
        // bit this arm is a one-liner again AND the two AddrOf arms stop
        // refusing legal programs — a strictly bigger prize than one row.
        if (logos::probe::on("recvmutbind") && is_mut &&
            !sit->is_mut_binding && !param_names_.count(bp.root) &&
            !(bp.root_type && is_ref_kind(bp.root_type))) {
            report(line, std::format(
                "cannot borrow '{}' as mutable: not declared as mut", bp.root));
            return;
        }
        if (sit->mut_borrowed) {
            // D1 residuals / P2 (task #51): DO NOT report here. Both callers
            // (the MethodCall bare-place-receiver check and the SD-DST Call
            // arg0 site) go on to VISIT the same operand, where the identical
            // mut_borrowed fact is refused by check_live's "cannot use '{}'
            // while it is mutably borrowed" (or the AddrOfTemp arm's own
            // report) — so this line was a guaranteed duplicate: every method
            // call on a mut-borrowed receiver emitted BOTH spellings
            // (bc_d1r9_f0_retarget_held pinned the check_live one and counted
            // 2 error lines; the P2/P3 fixtures counted 3). Verdict unchanged:
            // rc stays 1 through check_live. The branch is kept (empty) so a
            // mut-borrowed receiver cannot fall into the field-borrow arms
            // below and mint a different spelling. Measured over the full
            // 710-fixture fail corpus: no fixture's error count rose or
            // reached zero after this deletion.
        // ── THERE IS NO mut_reservations ARM HERE, AND ITS ABSENCE IS A
        // MEASUREMENT, NOT AN OVERSIGHT. `recvresvbare` was priced as two
        // halves: the receiver-loan producer in the MethodCall arm, and a
        // consumer arm at this spot refusing a mutable use while a reservation
        // of the same root is live. Split into two probe names and priced
        // separately 2026-08-28: the PRODUCER ALONE closes BOTH rows (342
        // fires, ceiling 2); producer+consumer closes the SAME TWO (343 fires,
        // ceiling 2) — the consumer fired ONCE in 387 whole-program compiles
        // and closed nothing.
        //
        // ⚠ AND IT IS NOT MERELY UNPROFITABLE, IT IS UNREACHABLE AT THIS
        // SPELLING. Traced: `self.set(self.set(1i64))` and its param twin
        // `s.set(s.set(1i64))` — two `&mut self` calls on one root, rustc
        // E0499 — never arrive at this function at all; sema wraps a
        // user-struct method receiver in an AddrOfTemp, so they are handled by
        // visit()'s AddrOfTemp arm. Landing the arm would have been a branch
        // no program executes, green for the reason a never-executed branch is
        // always green. The E0499 hole those two programs name is REAL and
        // stays OPEN; it lives at the AddrOfTemp arm, not here.
        } else if (is_mut && sit->shared_borrows > 0)
            report(line, std::format(
                "cannot borrow '{}' as mutable: '{}' has shared borrows",
                bp.root, bp.root));
        else if (is_mut && (!sit->shared_field_borrows.empty() ||
                            !sit->mut_field_borrows.empty()))
            report(line, std::format(
                "cannot borrow '{}' as mutable: field of '{}' is already borrowed",
                bp.root, bp.root));
        // V1-M1: THE SHARED ARM WAS MISSING. A `&self` method call on the
        // WHOLE variable while one of its FIELDS carries a live mut loan
        // aliases that field — `self.wat()` reads `self.w.at`, the very field
        // `self.w.next_batch()` holds `&mut` and mutates. Only the `is_mut`
        // arm above consulted the field table, so every `x.shared_method()`
        // after a live `&mut x.f` was admitted here.
        //
        // It stayed invisible while the receiver loan lived in the
        // WHOLE-VARIABLE flag (`mut_borrowed`), which check_live refuses for
        // any use: D8 moved that loan into the field table — correctly — and
        // this arm was then the only guard left on the path, with no shared
        // branch. The LOCAL-HOLDER twin (`let mut d = …; d.w.next_batch();
        // d.peek();`) refused throughout, because a value local's receiver is
        // wrapped in an AddrOfTemp and reaches take_borrow, whose shared arm
        // (§B83) has had this check all along. `self` is a reference param, so
        // sema never wraps it, so only THIS site guarded it — the whole spread
        // between the two spellings.
        //
        // Spelling reused verbatim from that take_borrow arm — not minted.
        // Reached only with `bp.path.empty()` (the guard at the top), so a
        // DISJOINT-field receiver (`self.sc.clear()`) never arrives here and
        // the D8 field-split admits are untouched.
        // PAIR: fail/bc_d8_shared_use_while_field_mut_fail (refuse) +
        //       pass/bc_d8_disjoint_field_use_admit (admit).
        else if (!is_mut && !sit->mut_field_borrows.empty())
            report(line, std::format(
                "cannot borrow '{}' as shared: field of '{}' is mutably borrowed",
                bp.root, bp.root));
    }

    // A MUTABLE USE of the place `bp` — the SAME question check_recv_conflict
    // answers, asked for a place whose PATH is non-empty. `*P = v` requires
    // exclusive use of P whatever P's spelling, because the borrow model has
    // NO PLACE FOR THE POINTEE: extract_borrow_place's Deref arm roots THROUGH
    // a reference to the reference's own place, and place_write_root's Deref
    // arm calls that step "the identity on places". So P is all there is to
    // ask about, and the only thing the DerefWrite site got wrong was asking
    // the path-EMPTY instantiation of the question.
    //
    // Empty path DELEGATES to check_recv_conflict — verbatim behaviour for its
    // three consumers, and `r.f = v` / `a[i] = v` / `t.0 = v` never arrive here
    // at all (they are FieldWrite / IndexWrite / TupleWrite statements). The
    // non-empty arm asks the field tables through field_borrow_conflicts, the
    // check-only predicate four other consumers already use. No third notion.
    //
    // THE RAW-PTR EXEMPTION MOVES TO THE POINTER'S OWN TYPE, and that is the
    // abuse direction of the widening. `bp.root_type` is the type of the ROOT,
    // which for `*p = v` (p a local `*mut T`) IS the pointer — the coincidence
    // that made check_recv_conflict's guard look right — but for `*h.p = v` it
    // is the STRUCT, so the exemption would stop applying exactly where the
    // path becomes non-empty. Census of the live stdlib spelling: 8 sites, 8
    // of 8 raw (cell.logos `*g.value`, `*a.value`/`*b.value`, `*self.state`,
    // `*orig.state`; handle.logos `*self.id_ctr_p`). Guard on the POINTER
    // expression's type, which reduces to today's test for a bare-VarRef ptr.
    void check_place_mut_use(const BorrowPlace& bp, TypeRef ptr_type,
                             uint32_t line) {
        // THE RAW-PTR EXEMPTION, AND IT IS WHAT THE RULE BELOW RESTS ON.
        // record_borrow gets this for free — the walker's Deref arm refuses to
        // root through a `Ptr` operand, so `&mut *s.p` arrives with an empty
        // root and returns on record_borrow's first line. The WRITE path walks
        // the POINTER expression (`s.p`, a FieldRead), which roots fine, so it
        // reaches the question on a place the borrow path never can and the
        // exemption must be spelled. MEASURED, one variable (the field's type):
        // `struct S { p: *mut i64 } fn a(s:&S){ unsafe{*s.p=7;} }` rc=0 and the
        // site NEVER FIRES — zero arrivals over the whole compile, so the only
        // statement between entry and the rule is this return; the byte-
        // identical program with `p: &mut i64` is rc=1 with one arrival.
        // Abuse direction: entering this hatch costs an `unsafe` block — the
        // same price Rust charges — enforced independently ("write through raw
        // pointer requires unsafe context", measured on the same program with
        // the `unsafe` removed).
        if (ptr_type && ptr_type.kind() == LogosType::Kind::Ptr) return;
        // ── THE QUESTION record_borrow ALREADY ASKS, INHERITED ────────────
        // A `&mut` through a SHARED reference is E0596 and a WRITE through one
        // is E0594; they are the same question about the same BorrowPlace,
        // computed by the same walker (`cross()` records the reference
        // crossed). record_borrow asks it at THE ONE RECORD SITE, before the
        // whole/field dispatch, so both its tails answer alike; this consumer —
        // the only checker of a `*place = v` write — never inherited it, and
        // the spread was directly observable on ONE program in two spellings:
        // `&mut *s.p` refused while `*s.p = 7` compiled. `is_mut` is
        // record_borrow's guard and is constant TRUE here (a DerefWrite is a
        // write), so no predicate is minted — one is delegated.
        //
        // ⚠ ASKED BEFORE THE ROOT-EMPTY RETURN, DELIBERATELY, and that is not
        // a drift from record_borrow. "Behind a shared `&`" is a property of
        // the PATH, not of the loan table: it needs no root to be TRUE, only a
        // name to be PRINTED. `**y = 2` / `***p = 2` over a Box used to break
        // the walk at the user-Deref call and lose the root while the crossing
        // was already recorded — 2 of the 4 rows this closes, and a root-empty
        // guard here would have dropped them silently. Since 2026-08-29 the
        // walk HOPS that call, so those two now print the named form and their
        // `.expected` files were re-pinned to it; the anonymous branch stays
        // for the crossings that still have no root.
        //
        // ⚠ WHAT THIS DOES NOT COVER, so it is not mistaken for coverage:
        // `cross()` is LAST-ASSIGNMENT-WINS (the walk runs outer->inner, so the
        // crossing NEAREST THE ROOT survives), hence `s: &mut S, s.r: &i64,
        // *s.r = 5` records MutRef and is still ADMITTED. That missed refusal
        // is inherited from the borrow path along with the question; probe
        // `sharedsticky` priced it at 0 rows over 61796 fires.
        if (bp.through_ref_type &&
            bp.through_ref_type.kind() == LogosType::Kind::Ref) {
            std::string place = fmt_path(bp.root, bp.path);
            report(line, place.empty()
                ? std::string("cannot assign to a place behind a `&` reference")
                : std::format("cannot assign to '{}': '{}' is behind a "
                              "`&` reference", place, bp.root));
            return;
        }
        if (bp.path.empty()) {
            check_recv_conflict(bp, /*is_mut=*/true, line);
            return;
        }
        if (bp.root.empty()) return;
        auto sit = var_find(bp.root_slot, bp.root);
        if (sit == nullptr) return;
        // mut_borrowed is DELIBERATELY not reported — check_recv_conflict's own
        // reasoning, one level out: the `visit(v.ptr(), ...)` that follows this
        // call reaches the same root through visit()'s place-base walk and
        // check_live refuses it there ("cannot use '{}' while it is mutably
        // borrowed"). Reporting here would be a guaranteed duplicate line.
        if (sit->mut_borrowed) return;
        if (sit->shared_borrows > 0) {
            report(line, std::format(
                "cannot borrow '{}' as mutable: '{}' has shared borrows",
                fmt_path(bp.root, bp.path), bp.root));
            return;
        }
        field_borrow_conflicts(*sit, bp.root, bp.path, /*need_exclusive=*/true,
                               line, "assign through");
    }

    // A `#[borrow_carrying]` type (WAny): a value that may hold a Ref into an arena.
    // Escape-tracked like a reference — see prov_of MethodCall/Call + Let/return gates.
    bool is_borrow_carrying_type(TypeRef t) const {
        return bc_is_borrow_carrying_type(ts_, t);
    }

    // ── D1 round 2, Door E + EXEMPT: the LOAN channel's own predicate ──────
    // is_borrow_carrying_type answers an ESCAPE question ("may this value leave
    // the referent's scope?") and therefore lets ts_.residency_exempt win: an
    // Rc/Arc-holding wrapper may escape, because the share keeps the arena
    // alive. The comment there claimed the ABUSE direction was closed by the
    // loan channel — but the loan channel gated on THE SAME function, so the
    // exemption silenced it too. That claim held only while the wrapper was
    // built in the caller's own frame (X1, rc=1, the loan on `c` is right
    // there); built in a CALLEE it leaked (X2 `let hh = make(&c); let b = hh.b;
    // c.bump()` rc=0) and `Option<H>` inherited the same FALSE (X3).
    //
    // DECISION: exemption-wins stays, but only for what it is ABOUT. Escape
    // gates keep is_borrow_carrying_type; the loan channel asks
    // loan_carrying_type, which is the same structural test with the residency
    // exemption NOT applied. A loan is a statement about the ARENA still being
    // read, and an Rc share does not stop the read — it only guarantees the
    // arena is alive, which is a different claim. Returning an Rc-holding
    // iterator past its arena (c4_exempt_return, the hatch's actual purpose)
    // goes through the return gate and is untouched.
    bool loan_carrying_type(TypeRef t) const {
        return bc_loan_carrying_type(ts_, t);
    }

    // #86: the escape hatch's own name test. `Held<T>`/`HeldAny` carry an
    // Rc/Arc share of the arena, so returning one past the arena is the
    // hatch's PURPOSE, not a dangle. is_borrow_carrying_type applies this at
    // its top; type_may_carry_borrow deliberately does not (it feeds the LOAN
    // channel), so the #86 escape gate has to apply it itself.
    bool type_is_residency_exempt(TypeRef t) const {
        if (!t) return false;
        auto k = t.kind();
        std::string nm;
        if (k == LogosType::Kind::Enum) nm = std::string(t.enum_name());
        else if (k == LogosType::Kind::Struct || k == LogosType::Kind::ZonedStruct)
            nm = std::string(t.struct_name());
        return !nm.empty() && ts_.residency_exempt.count(nm) > 0;
    }

    // ── #86 MISS 2: THE EXEMPTION, CHECKED IN THE ABUSE DIRECTION ──────────
    //
    // THE HOLE. `type_is_residency_exempt` is a NAME test over
    // `ts_.residency_exempt`, auto-populated (this file's `holds_residency_holder`,
    // ~line 192) for ANY struct with a field whose package-stripped name is
    // `Rc`/`Arc`, and registered under the BARE name too. So
    //   pub struct E { pub h: Rc<i64>, pub v: str }
    // switches the whole #86 escape gate off wholesale: returning `E`, or
    // `e.v`, with `v` borrowing a fn-local `String` was rc 0 — a
    // runtime-confirmed use-after-free (the returned `str` stops comparing
    // equal after 256 intervening String allocations). The `Rc` share keeps a
    // DIFFERENT allocation alive and says NOTHING about `v`. Also reachable by
    // BARE-NAME COLLISION: a user struct named `Held` in any package matches.
    //
    // CTRL-D proved the exemption NECESSARY (without it
    // examples/writ_container_showcase.logos:91 `return hold_any(&mut h, e);`
    // reds) — and nothing about what it lets through. The repo's standing rule
    // is that an unchecked hatch is worse than no gate, because the green now
    // vouches for it.
    //
    // WHAT SEPARATES THE TWO, MEASURED. With the exemption forced off, the
    // real user's refusal names `h` — and `h` IS the residency holder:
    //   [retgate] fn=fn make_held_doc line=91 … srcs=[h,]   `h: Rc<Writ>`
    // The abuse's refusal names `o` — a plain `String` the `Rc` has no share
    // of:
    //   [retgate] fn=fn bad line=8 … srcs=[o,]              `o: String`
    // So the exemption's real claim is not "this TYPE is exempt" but "the
    // borrow that escapes is kept alive by the share this value carries". The
    // check is therefore: the escaping expression may name ONLY locals that
    // are themselves residency-backed. Same hatch, same real user, and the
    // abuse no longer fits through it.
    //
    // Absence of a recorded type answers NO (refuse) — the refusing direction,
    // which is the direction this hole is in. Priced by the full-build red
    // list in the ledger.
    // `Rc<T>` / `Arc<T>` itself, anything already registered residency-exempt
    // (`Held`/`HeldAny` — a value that carries a share IS backed by one), or a
    // generic instance of either. `depth` bounds the type-arg walk the same way
    // `holds_residency_holder` bounds itself to direct fields: one hop is what
    // `&Rc<Writ>` / `Option<Rc<T>>` need, and a recursive type cannot diverge.
    bool type_is_residency_backed(TypeRef t, int depth = 1) const {
        if (!t) return false;
        auto k = t.kind();
        std::string nm;
        if (k == LogosType::Kind::Enum) nm = std::string(t.enum_name());
        else if (k == LogosType::Kind::Struct || k == LogosType::Kind::ZonedStruct)
            nm = std::string(t.struct_name());
        if (!nm.empty()) {
            if (ts_.residency_exempt.count(nm)) return true;
            std::string_view bare = nm;
            if (auto d = bare.rfind('.'); d != std::string_view::npos)
                bare = bare.substr(d + 1);
            if (auto g = bare.find("$G"); g != std::string_view::npos)
                bare = bare.substr(0, g);
            if (ts_.residency_exempt.count(std::string(bare))) return true;
            if (bare == "Rc" || bare == "Arc") return true;
        }
        if (depth <= 0) return false;
        for (auto a : t.type_args())
            if (type_is_residency_backed(TypeRef(a), depth - 1)) return true;
        return false;
    }

    bool local_is_residency_backed(const std::string& n) const {
        return type_is_residency_backed(holder_ty_of(n));
    }

    // The residency exemption for ONE escape, rather than for a type. Answers
    // the old blanket YES only when every local the escaping expression names
    // is residency-backed.
    bool residency_exemption_holds(TypeRef t, lir_view::ExprRef e) const {
        if (!type_is_residency_exempt(t)) return false;
        if (!e) return true;
        std::vector<std::string> srcs;
        collect_ref_sources(e, srcs);
        for (auto& n : srcs) {
            if (is_return_temp_name(n) || is_materialized_temp_name(n)) continue;
            if (!local_is_residency_backed(n)) {
                if (std::getenv("LOGOS_86_TRACE"))
                    fprintf(stderr, "[#86trace-exempt-denied] fn=%s src=%s\n",
                            fn_name_.c_str(), n.c_str());
                return false;
            }
        }
        // ── #86 MISS-D: THE TEST IS PER SHARE, NOT PER TYPE ────────────────
        //
        // THE HOLE, and it is the narrowing being WEAKER THAN ITS OWN STATED
        // RULE. The loop above implements "every named local is
        // residency-backed"; the rule the MISS-2 comment STATES is "the borrow
        // that escapes is kept alive by the share THIS VALUE carries". Those
        // differ the moment TWO shares are in scope:
        //   let mut h:  Rc<Writ> = writ_rc(64i64);
        //   let mut h2: Rc<Writ> = writ_rc(64i64);
        //   let doc: &mut WArray<WAny> = h.array(2i64);  …
        //   let e: WAny = WAny::from(&*doc);
        //   return hold_any(&mut h2, e);          // ← MEASURED rc 0
        // `h2`'s share keeps h2's ARENA alive; `e` points into h's, which is
        // freed at the end of the frame. Every named local is Rc-typed, so the
        // implemented test answers YES and the gate does not even open. The
        // one-variable control (`h2` → `h`) IS the admit twin.
        //
        // ⚠ WHY NOT THE OBVIOUS TEST — "chase the escaping value's borrow to
        // its share and compare identities". MEASURED AND REFUTED, twice over,
        // and the measurement is the reason this rule is coarse:
        //   • `collect_ref_sources(hold_any(&mut h2, e))` = `[h2]`. The
        //     BY-VALUE argument `e` — the half that carries the foreign borrow
        //     — contributes nothing to the source channel at all.
        //   • `bc_hop_roots` DOES reach it (`hops=[h2,e]`), and then the chase
        //     dies one hop later: with the instrument dumping both graphs at
        //     the check, `ref_borrow_sources_` holds exactly ONE entry for the
        //     whole function (`__ret_tmp_0 -> [h2]`) and `reborrow_of_` is
        //     EMPTY. Neither `doc -> h` nor `e -> doc` was ever recorded,
        //     because `h.array(…)` and `WAny::from(…)` are PREBUILT stdlib
        //     methods whose flow summaries are unavailable (task #81). The
        //     identity `e ∈ h` is not merely unread here — it does not exist
        //     anywhere in this pass.
        //
        // SO THE RULE IS THE ONE THE AVAILABLE FACTS SUPPORT: the exemption
        // may only speak when the frame leaves the arena's identity
        // unambiguous — at most ONE share handle (`Rc`/`Arc`) among the
        // bindings this function has declared. One share: the escaping value's
        // borrow can be rooted nowhere else, which is exactly the real user
        // (`make_held_doc` / examples/writ_container_showcase.logos, one
        // `h: Rc<Writ>`). Two shares: the pass cannot say WHICH, and "cannot
        // prove" resolves to REFUSE — the direction an unchecked hatch has to
        // be corrected in.
        //
        // THE PRICE, stated: a frame that holds two arenas and legitimately
        // returns a `Held` of one of them is over-refused. Nothing in the
        // 2211-pass + 754-fail corpus, the 53-target build, or the showcase
        // is such a frame (measured — red list in the ledger); the workaround
        // is the one Rust would also need, split the frame. The narrower fix
        // is a real `e ∈ h` provenance edge, which is task #81's summary
        // availability, not this gate's.
        {
            int nshare = 0;
            for (auto& kv : holder_ty_)
                if (type_is_share_handle(kv.second)) ++nshare;
            if (nshare > 1) {
                if (std::getenv("LOGOS_86_TRACE"))
                    fprintf(stderr, "[#86trace-exempt-multishare] fn=%s n=%d\n",
                            fn_name_.c_str(), nshare);
                return false;
            }
        }
        return true;
    }

    // A SHARE HANDLE proper — `Rc<T>` / `Arc<T>` itself, under any package
    // qualification or generic-instance suffix. Deliberately NARROWER than
    // `type_is_residency_backed`, which also answers yes for anything merely
    // REGISTERED residency-exempt (`Held`, `HeldAny`, a struct with an `Rc`
    // field): those say "a share is in here somewhere", which cannot identify
    // WHICH allocation, and identity is the whole question in MISS-D.
    bool type_is_share_handle(TypeRef t) const {
        if (!t) return false;
        auto k = t.kind();
        std::string nm;
        if (k == LogosType::Kind::Struct || k == LogosType::Kind::ZonedStruct)
            nm = std::string(t.struct_name());
        else if (k == LogosType::Kind::Enum) nm = std::string(t.enum_name());
        if (nm.empty()) return false;
        std::string_view bare = nm;
        if (auto d = bare.rfind('.'); d != std::string_view::npos)
            bare = bare.substr(d + 1);
        if (auto g = bare.find("$G"); g != std::string_view::npos)
            bare = bare.substr(0, g);
        return bare == "Rc" || bare == "Arc";
    }

    // ── #86 MISS 1: THE MUTATION AFTER THE LET ─────────────────────────────
    //
    // THE DEFECT the #86 landing left open, one line from its own fixture
    // fail/bc_esc_holder_return_field_dangle and runtime-confirmed (the
    // returned `str` stops comparing equal after 256 intervening String
    // allocations):
    //   let mut w: W = W { v: "" };  w.v = o.as_str();  return w.v;   // rc 0
    // #86's SUB-SITE 2 writes `prov_` ONLY at the `let` INITIALIZER. Every
    // later store into the same holder — whole-value reassign, field write,
    // tuple-element write, and the container deposit `v.push(o.as_str())` —
    // left `prov_` empty, so the return gate (which DID open: mcb=1, and the
    // loan channel even names the source) found no provenance to refuse on.
    //
    // ONE helper, four call sites, and it records the SAME thing SUB-SITE 2
    // records: the ESCAPE fact only (is_local / is_temp), never `params`. A
    // param-rooted store contributes nothing, so this cannot start
    // check_return_value's elision arm on a binding that never fed it.
    //
    // ADDITIVE, NOT REPLACING. A field write touches ONE field of a holder
    // whose OTHER fields may still carry an earlier borrow, so the escape bits
    // are OR-ed in rather than assigned. The cost is the shape
    // `let w = W{v:o.as_str()}; w.v = "static"; return w.v;` — over-refused;
    // the alternative (clear on every store) LOSES the sibling-field borrow,
    // which is the permissive direction and the direction this hole is in.
    //
    // PARAMS ARE SKIPPED DELIBERATELY. Writing a local borrow through a `&mut`
    // PARAMETER and returning it is the FRAME escape — task #78, explicitly
    // still open (see apply_flow_outparams' `⚠ #77 round 2` note) — and
    // marking a parameter `is_local` here would refuse every later return of
    // that parameter, not just the stored borrow. Left open; see the ledger.
    void note_holder_escape_prov(const std::string& name, TypeRef holder_ty,
                                 lir_view::ExprRef val, uint32_t ln,
                                 const char* site) {
        if (name.empty() || !val) return;
        // #138 — same substitution as in `prov_of`: only a param whose referent
        // OUTLIVES the call is exempt from the holder-escape deposit. A by-value
        // owned param is frame-local storage, so a borrow stored into it escapes
        // exactly as one stored into a local does (measured: `fn outer(h: &mut H)
        // { { let mut v = 1i64; h.r = &mut v; } }` admitted; the same with `h` a
        // local of the caller refused with E0597).
        if (param_names_.count(name)) return;          // #78/#138, see task
        if (!holder_ty ||
            !(type_may_carry_borrow(holder_ty) ||
              (logos::probe::on("bxhold") && type_hides_borrow(holder_ty))))
            return;
        if (residency_exemption_holds(holder_ty, val)) return;
        RefProv vp = prov_of(val);
        if (!vp.is_local && !vp.is_temp && vp.params.empty())
            vp = prov_of_retained(val);
        if (!vp.is_local && !vp.is_temp) return;
        ++holder_escape_prov_fired_;
        {   // per-door tally (site is a literal from the four call sites)
            int d = site[0] == 'a' ? 0 : site[0] == 'd' ? 1
                  : site[0] == 'o' ? 2 : 3;
            ++holder_escape_prov_by_door_[d];
        }
        if (std::getenv("LOGOS_86_TRACE"))
            fprintf(stderr, "[#86trace-%s] fn=%s line=%u var=%s loc=%d tmp=%d\n",
                    site, fn_name_.c_str(), ln, name.c_str(),
                    (int)vp.is_local, (int)vp.is_temp);
        auto& slot = prov_[name];
        slot.is_local = slot.is_local || vp.is_local;
        slot.is_temp  = slot.is_temp  || vp.is_temp;
        // ── AND THE §B6 SOURCE, so the DIAGNOSTIC CAN NAME THE LOCAL ───────
        //
        // MEASURED: without this the `Vec<H>` spelling refused with "cannot
        // return reference to local variable '__ret_tmp_0'" — a name that
        // exists in no source file. #77 round 2 already repaired that leak by
        // chasing `ref_sources_under(temp)`, but the chase finds nothing when
        // the §B6 channel never recorded a source for the holder, which is
        // exactly the by-VALUE element case (srcs=[] measured).
        //
        // The two channels are answering the SAME question here — this record
        // fires only where the escape fact is already local/temp — so a §B6
        // source is not a wider claim, it is the same claim written where the
        // diagnostic can read it. Deposited through `store_ref_sources`, which
        // is ADDITIVE (`add_ref_sources` would `erase_ref_sources_under` the
        // place first and lose an earlier push's source — the permissive
        // direction, and the direction this hole is in). Same shape as the #78
        // out-param deposit.
        {
            std::vector<std::string> escs;
            collect_ref_sources(val, escs);
            std::vector<std::pair<std::string, RefSrc>> pairs;
            for (auto& s2 : escs)
                pairs.emplace_back(std::string{}, RefSrc{s2, slot_of_binding(s2)});
            if (!pairs.empty()) store_ref_sources(name, std::move(pairs), name, ln);
        }
    }

    // The holder type of a local binding, as recorded at its `let`.
    TypeRef holder_ty_of(const std::string& name) const {
        auto it = holder_ty_.find(name);
        return it != holder_ty_.end() ? it->second : TypeRef(nullptr);
    }

    // Door E — the CONSTRUCTION/coercion site. `Box::new(c.mk())` erased into
    // `Box<dyn Get>` has a result type that says nothing: type_bc_name only
    // names Enum/Struct/ZonedStruct, and `dyn Get` is none of them, so
    // `Box<dyn Get>` was not borrow-carrying while `Box<B>` was (E1 rc=0 vs
    // Et1 rc=1 — the type-arg-name pair is exact).
    //
    // DESIGN CHOICE, and the wrong easy answer is "treat every dyn type-arg as
    // borrow-carrying": that would refuse every non-bc dyn user, and the
    // Arc<dyn Snapshot> ecosystem is the live consumer. The honest rule is at
    // the site where the erasure HAPPENS: a value that RETAINS a by-value
    // borrow-carrying operand still holds its borrow, whatever its result type
    // now says. So the hop runs on the operand, not on the erased type.
    //
    // "Retains" is read off the RESULT KIND: an aggregate/pointer/dyn result
    // can hold the operand, a scalar result cannot. That is exactly what keeps
    // the consuming control admitted — `fn eat(x: B) -> i64` returns an i64,
    // which retains nothing (Ea1, rc=0 before and after).
    static bool type_retains_values(TypeRef t) {
        if (!t) return false;
        using K = LogosType::Kind;
        switch (t.kind()) {
            case K::Struct: case K::ZonedStruct: case K::Enum: case K::Tuple:
            case K::Array:  case K::Slice:       case K::UnsizedSlice:
            case K::TraitObject: case K::UnsizedDyn: case K::ImplTrait:
            case K::DstRef: case K::TaggedPtr:   case K::Closure:
            case K::Ptr:    case K::Ref:         case K::MutRef:
                return true;
            default: return false;
        }
    }
    bool retains_loan_carrying_operand(lir_view::ExprRef e) const {
        using namespace lir_view;
        using Code = lir_schema::expr::Code;
        if (!e) return false;
        const auto* pool = prog_.type_pool.impl();
        if (!type_retains_values(e.type(pool))) return false;
        bool found = false;
        auto by_value_bc = [&](ExprRef a) {
            if (!a || found) return;
            TypeRef at = a.type(pool);
            if (!is_ref_kind(at) && loan_carrying_type(at)) found = true;
        };
        switch (e.kind()) {
            case Code::Call:       ECallView{e}.each_arg(by_value_bc); break;
            case Code::MethodCall: {
                EMethodCallView v{e};
                by_value_bc(v.receiver());
                v.each_arg(by_value_bc);
                break;
            }
            case Code::StructLit:
                EStructLitView{e}.each_field_value(by_value_bc); break;
            case Code::TupleLit:  ETupleLitView{e}.each_elem(by_value_bc); break;
            case Code::ArrLit:    EArrLitView{e}.each_elem(by_value_bc);  break;
            case Code::EnumLitData:
                EEnumLitDataView{e}.each_payload(by_value_bc); break;
            case Code::Cast:      by_value_bc(ECastView{e}.operand());    break;
            default: break;
        }
        return found;
    }

    // Door E / EXEMPT — the RETENTION record. The hop alone does not close
    // these two: at `let hh = make(&c)` / `let d = Box::new(c.mk())` the root
    // `c` holds no loan YET — the loan is the one this very expression creates
    // — so there is nothing to inherit. What is needed is take_ref_borrows'
    // other effect: record the operand's borrow with the BINDING as holder.
    //
    // Applied to the OPERANDS, one at a time, rather than by routing the whole
    // expression, so that `&mut` arguments can be excluded. Routing everything
    // was measured and over-refuses: `let res: GpRes = gp_build(…, &mut sa, …);
    // … sa.len()` (stdlib/mem/wql/lower.logos) then held `&mut sa` for the rest
    // of the function. A `&mut` argument is an OUT parameter as often as it is
    // a borrow source, and when the callee stores INTO it the loan is recorded
    // by Door F's rule (the &mut root becomes the holder) — which is the right
    // holder for that case anyway. Shared refs and by-value loan-carrying
    // operands are the ones the RESULT can retain.
    void retain_operand_loans(lir_view::ExprRef e, const std::string& holder,
                              uint32_t ln) {
        using namespace lir_view;
        using Code = lir_schema::expr::Code;
        if (!e || holder.empty()) return;
        const auto* pool = prog_.type_pool.impl();
        auto one = [&](ExprRef op) {
            if (!op) return;
            TypeRef ot = op.type(pool);
            if (!ot) return;
            if (ot.kind() == LogosType::Kind::MutRef) return;   // out-param: not ours
            if (!is_ref_kind(ot) && !loan_carrying_type(ot)) return;
            take_ref_borrows(op, ln, holder, /*record_only=*/true);
        };
        switch (e.kind()) {
            case Code::Call:       ECallView{e}.each_arg(one); break;
            case Code::MethodCall: {
                EMethodCallView v{e};
                one(v.receiver());
                v.each_arg(one);
                break;
            }
            case Code::StructLit:  EStructLitView{e}.each_field_value(one); break;
            case Code::TupleLit:   ETupleLitView{e}.each_elem(one);         break;
            case Code::ArrLit:     EArrLitView{e}.each_elem(one);           break;
            case Code::EnumLitData:EEnumLitDataView{e}.each_payload(one);   break;
            case Code::Cast:       one(ECastView{e}.operand());             break;
            default: break;
        }
    }

    // D1 round 2, Door B (for-each): the HOP-ROOT gate, not a bc classification.
    // `for e in vs.iter()` desugars to `let it = vs.iter(); loop { match
    // it.next() { Some(e) => …, None => break } }`, so the iteration binding is
    // a match-arm binding and the scrutinee type is `Option<&B>`. That type is
    // NOT borrow-carrying by is_borrow_carrying_type — `&B` is Kind::Ref, has
    // no bc NAME and exposes no type-args — so the scrutinee-side gate refused
    // to even look for hop roots and the arm binding inherited nothing
    // (measured: bc=0 roots=0 at the desugared match).
    //
    // This predicate is deliberately NOT is_borrow_carrying_type and is used
    // ONLY to decide whether to LOOK for hop roots. It cannot over-refuse on
    // its own: propagate_pat_loans still gates each BINDING on its own type,
    // and inheriting from a binding that holds no loan is a no-op. Keeping it
    // separate is what stops `Option<&i64>` from becoming "borrow-carrying"
    // everywhere else (return gates, escape analysis, the residency exemption).
    // `wide` = the erased-payload widening, THREADED rather than read from the
    // probe, so one process can answer the same type both ways (the site census).
    bool tmcb_walk(TypeRef t, bool wide) const {
        if (!t) return false;
        if (is_ref_kind(t) || loan_carrying_type(t)) return true;
        if (wide) {
            using KD = LogosType::Kind;
            auto kd = t.kind();
            if (kd == KD::Closure || kd == KD::TraitObject ||
                kd == KD::UnsizedDyn || kd == KD::ImplTrait)
                return true;
        }
        {   // #71 SPIKE
            auto k = t.kind();
            std::string n;
            if (k == LogosType::Kind::Enum) n = std::string(t.enum_name());
            else if (k == LogosType::Kind::Struct || k == LogosType::Kind::ZonedStruct)
                n = std::string(t.struct_name());
            if (!n.empty() && ts_.holds_any_ref.count(n) > 0) return true;
        }
        for (auto a : t.type_args())
            if (tmcb_walk(a, wide)) return true;
        // #70 MISS-2: a TUPLE's elements are not type_args, so `(&i64, i64)`
        // answered false and the §B6 Call arm never inspected
        // `held = pick((&tmp, 1))` — the composition escaped at rc 0 while the
        // bare spelling refused. Structural recursion, same shape as
        // bc_loan_carrying_type's own tuple arm. NOT covered here: a plain
        // STRUCT with a raw-ref field (`H { r: &i64 }`) — deciding that from a
        // TypeRef needs a holds-any-ref fixpoint set beside holds_mut_ref
        // (loan_carrying only propagates NAMED carriers); its repro is
        // sandbox/t70verify/m16_dangle_structlit_real.logos, filed as the
        // class's remaining residual.
        if (t.kind() == LogosType::Kind::Tuple) {
            for (auto e : t.tuple_elems())
                if (tmcb_walk(TypeRef(e), wide)) return true;
        }
        if (t.kind() == LogosType::Kind::Array || t.kind() == LogosType::Kind::Slice)
            return tmcb_walk(t.elem(), wide);
        return false;
    }

    // ── THE ERASED-PAYLOAD ENTRY, SPELLED BY ARM (see PROBES.md §tmcbsite) ──
    // `Box<dyn Trait>` / `impl Trait` / a closure can HOLD a `&` that the type
    // does not spell, exactly as `type_hides_borrow` (the return gate's sibling
    // predicate) has always listed. `type_may_carry_borrow` has 28 consumers
    // and the widening OVER-REFUSES legal programs at two of them, so it is not
    // applied to the predicate; the four arms that discharge it call THIS name
    // instead. Adding a fifth caller is a measurement, not an edit: price it.
    bool type_may_carry_borrow_erased(TypeRef t) const { return tmcb_walk(t, true); }

    // The entry every gate in this file asks. `site` is the CALLER's line, so a
    // flip is attributed to the consuming site and never to the walk's recursion.
    bool type_may_carry_borrow(TypeRef t, int site = __builtin_LINE()) const {
        bool armed = logos::probe::on("tmcbdyn") || logos::probe::on("bxsrc");
        bool logging = tmcb_flip_armed();
        if (logging) tmcb_flip_seen(site);
        if (tmcb_walk(t, false)) return true;
        if (!armed && !logging) return false;
        bool wide = tmcb_walk(t, true);
        if (wide) tmcb_flip_note(site);
        return armed && wide && tmcb_site_allowed(site);
    }

    // If `e` (a borrow's inner place, or a method receiver) roots at a VALUE local,
    // return its name; else "". Walk one optional leading AddrOfTemp, then a
    // FieldRead/TupleIndex/IndexRead/Deref chain to the terminal VarRef. A RAW-
    // pointer deref (`*p`, p:`*mut`/`*const`) STOPS the walk — the pointee isn't
    // tied to p's stack lifetime (Rust parity; box_leak's `&mut *into_raw(b)`). The
    // terminal must be a VALUE local: in states_, NOT a param, NOT a tracked ref-
    // binding (ref locals are in prov_) — which keeps `&param.x` / ref-locals safe.
    // A reference rooted at such a local dangles if it escapes the local's scope.
    // temp_root — D-c's NEIGHBOURS dcv2/dcv3, and the same question the walk
    // below already answers for a NAMED root: `&[x][0u64]` and `&S{n:x}.n`
    // both returned a borrow into storage that dies at the semicolon, and both
    // were admitted, because this walk terminates only on a `VarRef` and a
    // temporary has no name. `is_temporary_value_expr` is the existing test for
    // "this expression IS the temporary"; the projections are already peeled
    // above, so the terminal is the only place it was missing.
    // ⚠ `!is_ref_kind` is load-bearing and NARROW BY MEASUREMENT: that
    // predicate answers YES for any `Call`, and `&foo(x).n` where `foo`
    // returns a REFERENCE borrows the caller's storage, not a temporary. The
    // value-returning call (`&mk().n`) is a genuine dangle and is caught; the
    // param-rooted one is answered earlier by `inner_prov` in prov_of's
    // AddrOfTemp arm, before this function is reached at all.
    std::string value_local_root(lir_view::ExprRef e,
                                 const TypePoolImpl* pool,
                                 bool* temp_root = nullptr,
                                 lir_view::ExprRef* terminal = nullptr) const {
        using namespace lir_view;
        using Code = lir_schema::expr::Code;
        ExprRef cur = e;
        if (cur && cur.kind() == Code::AddrOfTemp) cur = EAddrOfTempView{cur}.inner();
        // A projection whose receiver is a RAW POINTER is a raw deref in
        // disguise — `e.key` / `e[i]` with `e: *const Entry` is spelled without
        // a `*` but means exactly `(*e).key`. The explicit-Deref arm below
        // already returns {} for it (Rust parity: the pointee is not tied to
        // the local holding the pointer); F4's return gate is the first caller
        // that reaches these shapes, and without this the whole raw-pointer
        // iterator idiom (`HashMapKeys::next` → `let e: *const Entry = …;
        // return Option::Some(&e.key);`) was reported as returning a borrow of
        // the local `e`. MEASURED: 6 stdlib iterator families.
        auto recv_is_rawptr = [&](ExprRef r) {
            return r && r.type(pool) &&
                   r.type(pool).kind() == LogosType::Kind::Ptr;
        };
        while (cur) {
            Code k = cur.kind();
            if (k == Code::FieldRead)  {
                auto r = EFieldReadView{cur}.receiver();
                if (recv_is_rawptr(r)) return {};
                cur = r; continue;
            }
            if (k == Code::TupleIndex) {
                auto r = ETupleIndexView{cur}.receiver();
                if (recv_is_rawptr(r)) return {};
                cur = r; continue;
            }
            if (k == Code::IndexRead)  {
                auto r = EIndexReadView{cur}.receiver();
                if (recv_is_rawptr(r)) return {};
                cur = r; continue;
            }
            if (k == Code::Deref) {
                auto op = EDerefView{cur}.operand();
                if (op && op.type(pool) &&
                    op.type(pool).kind() == LogosType::Kind::Ptr)
                    return {};   // raw-pointer deref — unchecked
                cur = op; continue;
            }
            // The SLICE family, transparent here for the same reason it is
            // transparent in `prov_of` (SliceLit/SlicePtr/SliceIndex are
            // projections of the underlying place, exactly like IndexRead).
            // Without them this walk stopped ON the projection: `return &[x];`
            // reached the report site with NO root name and NO temp root, and
            // printed `local variable '?'`.
            if (k == Code::SliceLit)  { cur = ESliceLitView{cur}.base();  continue; }
            if (k == Code::SlicePtr)  { cur = ESlicePtrView{cur}.slice(); continue; }
            if (k == Code::SliceIndex){ cur = ESliceIndexView{cur}.slice(); continue; }
            if (k == Code::AddrOfTemp){ cur = EAddrOfTempView{cur}.inner(); continue; }
            break;
        }
        if (terminal) *terminal = cur;
        if (cur && temp_root && is_temporary_value_expr(cur) &&
            !is_ref_kind(cur.type(pool)))
            *temp_root = true;
        if (cur && cur.kind() == Code::VarRef) {
            std::string rn(EVarRefView{cur}.name());
            uint32_t rn_slot = EVarRefView{cur}.var_slot();  // Phase-1
            // A value LOCAL root: a borrow of it dangles if returned.
            if (var_has(rn_slot, rn) && !param_names_.count(rn) &&
                prov_.find(rn) == prov_.end())
                return rn;
            // A `const` ITEM is materialised into the FRAME at each use, so a
            // borrow reaching it through a PROJECTION dangles exactly as one
            // into a value local does. The bare spelling `&K` is caught by
            // `prov_of`'s AddrOf arm with this same set; `&K.a` and `&A[0]`
            // arrive HERE instead and fell through, because a const is not in
            // `var_has`. Measured on the landed build with the clobber constant
            // varied: `return &A[0];` gave exit 71 for stomp 71 where 5 is
            // correct, and `&K.a` the same — the bare form was closed and its
            // projections were not, which the admit twin could not see because
            // it only exercised the bare form.
            // ⚠ A `static` is NOT here and must not be: it has real static
            // storage, and sema rewrites its read to `Deref(VarRef
            // "__static_addr:<sym>")`, so it never reaches this terminal at all.
            if (ts_.frame_consts.count(rn)) return rn;
            // A BY-VALUE OWNED param is call-local storage too — it is dropped at
            // return, so a borrow into it (`&h.v` where `h: Holder`) dangles.
            // Reference / raw-pointer / borrow-carrying params (outliving_params_)
            // point at data that outlives the call and are safe; a raw-pointer
            // deref earlier in the walk already returned {} for the arena-handle
            // idiom (`&*(self.p)`), so only genuinely owned param storage reaches
            // here.
            if (param_names_.count(rn) && !outliving_params_.count(rn))
                return rn;
        }
        return {};
    }

    // D1 — the local BINDINGS a borrow-carrying value hops out of.
    //
    // `value_local_root` answers a different question (does a borrow root at
    // call-local storage, for dangling detection) and therefore excludes
    // params and tracked ref-bindings. Here the question is "which live
    // binding's loans does this value inherit", so the walk is deliberately
    // unfiltered: any tracked local is a candidate. Inheriting from a binding
    // that holds no loans is a no-op, so widening the walk cannot over-refuse.
    //
    // Structure: composition (aggregate literals) and pass-through (a call
    // whose RESULT carries borrows) recurse into the borrow-carrying operands;
    // extraction (`ob.unwrap()`, `w.b`, `s.get()`) walks the place chain to
    // its terminal VarRef. A raw-pointer deref stops the walk, exactly as in
    // value_local_root (Rust parity — the pointee isn't tied to the local).
    void bc_hop_roots(lir_view::ExprRef e,
                      std::vector<std::string>& out) const {
        using namespace lir_view;
        using Code = lir_schema::expr::Code;
        if (!e) return;
        const auto* pool = prog_.type_pool.impl();
        switch (e.kind()) {
            // D1 round 2, RESIDUE — a PLAIN-REF argument rooted at a loan
            // holder. The hop already followed a method's RECEIVER and every
            // BY-VALUE borrow-carrying argument, but a `&B` argument was
            // skipped because `&B` is Kind::Ref and is_borrow_carrying_type
            // says no. So `fn thru(b: &B) -> B { B { p: b.p } }; let b1 =
            // thru(&b0)` re-exported nothing and only recorded a borrow of
            // `b0` ITSELF — the loan `b0` held died at its last use, while the
            // method spelling `b0.thru2()` refused (Rt1, the exact twin).
            //
            // Gated twice over, which is why it cannot over-refuse: the caller
            // runs this walk only when the RESULT carries borrows, and
            // inheriting from a binding that holds no loan is a no-op. A
            // consuming call (`fn eat(x: B) -> i64`) has a scalar result and
            // never gets here.
            case Code::MethodCall: {
                EMethodCallView v{e};
                bc_hop_roots(v.receiver(), out);
                v.each_arg([&](ExprRef a) {
                    if (a && type_may_carry_borrow(a.type(pool)))
                        bc_hop_roots(a, out);
                });
                return;
            }
            case Code::Call: {
                ECallView v{e};
                v.each_arg([&](ExprRef a) {
                    if (a && type_may_carry_borrow(a.type(pool)))
                        bc_hop_roots(a, out);
                });
                return;
            }
            case Code::ClosureCall:
            case Code::FnPtrCall: {   // H4 — see note_closure_caps
                ExprRef callee = call_callee(e);
                if (const auto* caps = closure_caps_of(callee)) {
                    // The closure VALUE is the holder of the capture borrows,
                    // and the captures are the roots those borrows name; both
                    // have to co-hold whatever the result is stored into.
                    bc_hop_roots(callee, out);
                    for (auto& c : *caps) out.push_back(c);
                }
                auto arg = [&](ExprRef a) {
                    if (a && type_may_carry_borrow(a.type(pool)))
                        bc_hop_roots(a, out);
                };
                if (e.kind() == Code::ClosureCall) EClosureCallView{e}.each_arg(arg);
                else                               EFnPtrCallView{e}.each_arg(arg);
                return;
            }
            // ── D1 round 9 / P12: AN AGGREGATE MEMBER GETS THE ARGUMENT GATE ──
            //
            // THE DEFECT (measured, and a REGRESSION the unification introduced
            // — HEAD refused it, r8 admitted it):
            //   let rc: &C = &c; let rc2: &C = rc;
            //   let b: B = B { p: &rc2.v }; c.bump(); *b.p
            // compiled and returned the POST-mutation value, while the one-hop
            // spelling `&rc.v` and the call spelling `rc2.mk()` both refuse.
            //
            // THE ROOT is this gate, and it is the SAME defect the MethodCall
            // arm above already paid for (see its Door-B note): a `&i64` /
            // `&B` member is Kind::Ref, has no bc NAME and exposes no type-arg,
            // so `is_borrow_carrying_type` says NO and the walk never reaches
            // the member at all — hence never reaches `resolve_ref_places`,
            // and `b` never inherits the loan `rc` holds on `c`. One hop only
            // refused by coincidence: there the borrow's raw root and the
            // loan's holder are the same name (`rc`), which is U0's
            // coincidence one projection deeper.
            //
            // The fix is the gate the ARGUMENT positions already use —
            // `type_may_carry_borrow`, the predicate written for exactly this
            // "should I LOOK for hop roots" question — so the four aggregate
            // arms stop being the one family still on the narrow escape
            // classification. It cannot over-refuse on its own by this file's
            // standing argument: bc_hop_roots only ADDS names, inherit_loans
            // only ADDS a co-holder to an EXISTING record (never creates one,
            // never strengthens a borrow), and inheriting from a binding that
            // holds no loan is a no-op.
            case Code::EnumLitData:
                EEnumLitDataView{e}.each_payload([&](ExprRef pl) {
                    if (pl && type_may_carry_borrow(pl.type(pool)))
                        bc_hop_roots(pl, out);
                });
                return;
            case Code::StructLit:
                EStructLitView{e}.each_field_value([&](ExprRef fv) {
                    if (fv && type_may_carry_borrow(fv.type(pool)))
                        bc_hop_roots(fv, out);
                });
                return;
            case Code::TupleLit:
                ETupleLitView{e}.each_elem([&](ExprRef el) {
                    if (el && type_may_carry_borrow(el.type(pool)))
                        bc_hop_roots(el, out);
                });
                return;
            case Code::ArrLit:
                EArrLitView{e}.each_elem([&](ExprRef el) {
                    if (el && type_may_carry_borrow(el.type(pool)))
                        bc_hop_roots(el, out);
                });
                return;
            case Code::Cast:
                bc_hop_roots(ECastView{e}.operand(), out);
                return;
            case Code::IfExpr:
                bc_hop_roots(EIfExprView{e}.then_val(), out);
                bc_hop_roots(EIfExprView{e}.else_val(), out);
                return;
            case Code::BlockExpr:
                bc_hop_roots(EBlockExprView{e}.result(), out);
                return;
            default: break;
        }
        // Place chain → terminal VarRef.
        ExprRef cur = e;
        if (cur && cur.kind() == Code::AddrOfTemp) {
            cur = EAddrOfTempView{cur}.inner();
            // ── #86 MISS-E: AddrOfTemp OF A NON-PLACE RE-ENTERS THE SWITCH ─
            //
            // THE GAP (measured). `return mk(o.as_str()).get();` — a `&self`
            // method whose receiver is a CALL — spells that receiver
            // `AddrOfTemp(Call)`. The peel above lands on a `Call`, the place
            // loop below has no arm for one, the terminal-VarRef test fails,
            // and the whole walk answers `[]`. The FREE-FUNCTION spelling of
            // the identical program (`get2(mk(o.as_str()))`) answers `[o]` —
            // the one-variable control, run on this tree: it names `o`, the
            // method spelling named `__ret_tmp_0`.
            //
            // The switch above already handles every one of those inner kinds
            // (Call / MethodCall / aggregate literal / IfExpr / BlockExpr);
            // this arm just stops throwing the expression away. Strictly
            // ADDITIVE — a walk that returned nothing now returns names — and
            // the caller-side argument for why adding names is safe is the one
            // written at the top of this function (inherit_loans only ever ADDS
            // a co-holder to a record that already exists).
            using K = Code;
            // SliceIndex added with the loop below, for the same reason and
            // in the same commit: a guard that lists place kinds must not drift
            // from the loop it guards.
            if (cur && cur.kind() != K::VarRef  && cur.kind() != K::AddrOf &&
                cur.kind() != K::AddrOfTemp     && cur.kind() != K::FieldRead &&
                cur.kind() != K::TupleIndex     && cur.kind() != K::IndexRead &&
                cur.kind() != K::SliceIndex     && cur.kind() != K::Deref) {
                bc_hop_roots(cur, out);
                return;
            }
        }
        if (cur && cur.kind() == Code::AddrOf) {
            std::string n(EAddrOfView{cur}.var_name());
            if (var_has(NO_SLOT, n)) out.push_back(std::move(n));
            return;
        }
        // A projection whose receiver is a RAW POINTER is a raw deref in
        // disguise — `e.key` / `e[i]` with `e: *const Entry` is spelled without
        // a `*` but means exactly `(*e).key`. The explicit-Deref arm below
        // already returns {} for it (Rust parity: the pointee is not tied to
        // the local holding the pointer); F4's return gate is the first caller
        // that reaches these shapes, and without this the whole raw-pointer
        // iterator idiom (`HashMapKeys::next` → `let e: *const Entry = …;
        // return Option::Some(&e.key);`) was reported as returning a borrow of
        // the local `e`. MEASURED: 6 stdlib iterator families.
        auto recv_is_rawptr = [&](ExprRef r) {
            return r && r.type(pool) &&
                   r.type(pool).kind() == LogosType::Kind::Ptr;
        };
        // ── D1 round 7 / R7a: THE HOP MUST LOOK THROUGH A NAMED REBORROW ───
        //
        // THE DEFECT (measured, LOGOS_R7A_TRACE at note_reborrow / its alias
        // chase / release_dead_borrows, then reverted). For
        //   let mut s: S = c.mk_s();        // s holds c's loan
        //   let mut r: &mut S = &mut s;
        //   let b: B = r.pull(); c.bump(); *b.p
        // the reborrow IS recorded and DOES extend s's NLL live range
        // (`alias_use r -> s`), so the brief's "extend the live range" shape is
        // already in place. What never happens is LOAN INHERITANCE across the
        // hop: this walk hands back the terminal name `r`, so `b` co-holds only
        // the loan OF s, and the record `target=c holder=s co=[]` is RELEASED
        // at the pull — one line before the mutation. Verbatim:
        //   line=20 target=c holder=s co=[]  lu=20 RELEASE   (leak)
        //   line=19 target=c holder=s co=[b] lu=21 keep      (alias-free twin)
        // Same signature in every spelling: `(*r).pull()`, `&mut *r` two-level,
        // and `h.r.pull()` (where `reborrow_of_["h.r"]=s` is recorded but never
        // consulted). The alias-free twin (a0) and the inline twin
        // `(&mut s).pull()` (a6) both refuse — this rule converges the named
        // spellings onto the verdict they already give.
        //
        // WHY IT CANNOT OVER-REFUSE ON ITS OWN. It only ADDS a name to the
        // roots; the caller runs the walk only when the result carries borrows,
        // inherit_loans ADDS a co-holder to an existing record (never creates,
        // never strengthens), and inheriting from a binding that holds no loan
        // is a no-op (ac1). Retraction is untouched, so scope exit still
        // releases (ac2).
        //
        // The dotted FIELD path is collected exactly as place_write_root does
        // it (outermost first, invalidated by an index/tuple step, NOT by a
        // reference deref) so `h.r.pull()` resolves through the place map.
        std::vector<std::string> path_fields;
        bool path_ok = true;
        while (cur) {
            Code k = cur.kind();
            if (k == Code::FieldRead)  {
                auto r = EFieldReadView{cur}.receiver();
                if (recv_is_rawptr(r)) return;
                if (path_ok) path_fields.emplace_back(EFieldReadView{cur}.field());
                cur = r; continue;
            }
            if (k == Code::TupleIndex) {
                auto r = ETupleIndexView{cur}.receiver();
                if (recv_is_rawptr(r)) return;
                path_ok = false;
                cur = r; continue;
            }
            // ⚠ ONE ARM FOR BOTH INDEXING STEPS. An array index is IndexRead,
            // a slice index is SliceIndex; the same step, and this loop peeled
            // only the first — so a hop whose receiver is a SLICE ELEMENT never
            // reached its terminal VarRef, the walk answered [], and the result
            // inherited none of the slice binding's loans.
            // ⚠ TWO EARLIER ATTEMPTS "REFUTED" THIS, both by building the slice
            // as `&arr`. That spelling loses the loan on the binding ENTIRELY
            // (a separate defect in the `&arr -> &[T]` coercion), so there was
            // nothing to inherit and the arm changed nothing observable — a
            // mask, not an absence. Taking the slice from a CALL (`bx.all()`)
            // makes the loan record identical on both sides and exposes it.
            // MEASURED: `let hs: &[B] = bx.all(); let b = hs[0].thru();
            // bx.touch(); *b.p` compiled rc 0 while its `&[B; 1]` twin refused.
            if (k == Code::IndexRead || k == Code::SliceIndex) {
                auto r = (k == Code::IndexRead) ? EIndexReadView{cur}.receiver()
                                                : ESliceIndexView{cur}.slice();
                if (recv_is_rawptr(r)) return;
                path_ok = false;
                cur = r; continue;
            }
            if (k == Code::Deref) {
                auto op = EDerefView{cur}.operand();
                if (op && op.type(pool) &&
                    op.type(pool).kind() == LogosType::Kind::Ptr)
                    return;   // raw-pointer deref — unchecked (Rust parity)
                cur = op; continue;
            }
            break;
        }
        if (cur && cur.kind() == Code::VarRef) {
            std::string n(EVarRefView{cur}.name());
            bool tracked = var_has(EVarRefView{cur}.var_slot(), n) || is_loan_holder(n);
            if (tracked) out.push_back(n);
            // R7a + round 8 / U0: EVERY node the place names, not the endpoint.
            std::vector<std::string> nodes;
            resolve_ref_places(n, path_fields, path_ok, nodes);
            auto add = [&](const std::string& c) {
                if (c.empty() || c == n) return;
                if (!var_has(NO_SLOT, c) && !is_loan_holder(c)) return;
                if (std::find(out.begin(), out.end(), c) == out.end()) out.push_back(c);
            };
            for (auto& m : nodes) {
                // A dotted place names a variable only through its ROOT — the
                // loan channels are keyed by variable. Try the place first so
                // `&mut s.f` still answers `s` exactly as it used to.
                if (var_has(NO_SLOT, m) || is_loan_holder(m)) add(m);
                else                                          add(ref_place_root(m));
            }
        }
    }

    // ── #86 SUB-SITE C: the borrow a receiver VALUE CARRIES ────────────────
    //
    // NOT prov_of(receiver). A `&self` call spells its receiver `&w`, and
    // prov_of's AddrOf arm answers "is_local" for ANY local `w` — that is the
    // provenance of a borrow OF `w`, which is exactly the F2 over-refusal the
    // recv_contributes guard exists to prevent. MEASURED false positive when
    // the carry clause read that channel:
    //   `fn ok(s: str) -> str { let w = W{v:s}; return w.get(); }` → rc 1,
    //   "cannot return reference to local variable 'w'" — the ELISION case,
    //   which must compile (sandbox C1p.logos, both spellings).
    // What the clause needs is the provenance recorded FOR THE VALUE, i.e.
    // prov_'s entry for the binding (or the value expression itself when the
    // receiver is a fresh rvalue), so a param-rooted carry answers `params`
    // and a fn-local one answers `is_local`.
    RefProv carried_prov_of_recv(lir_view::ExprRef r) const {
        using namespace lir_view;
        using Code = lir_schema::expr::Code;
        if (!r) return {};
        if (r.kind() == Code::AddrOf) {
            std::string nm(EAddrOfView{r}.var_name());
            if (param_names_.count(nm)) return {{nm}, false};
            auto it = prov_.find(nm);
            return it != prov_.end() ? it->second : RefProv{};
        }
        if (r.kind() == Code::AddrOfTemp)
            return carried_prov_of_recv(EAddrOfTempView{r}.inner());
        if (r.kind() == Code::VarRef) {
            std::string nm(EVarRefView{r}.name());
            // #138 — "is a PARAMETER" is not "OUTLIVES THE CALL". A `&T`/`&mut T`
            // / borrow-carrying / raw-pointer param points at caller storage, so
            // a borrow of it is safe to return; a BY-VALUE OWNED param dies with
            // the frame exactly like a local. `outliving_params_` already draws
            // that line at fn entry, with the reason written there — it was just
            // consulted at ONE of the eleven sites that ask this question, and
            // this was not one of them. Result: `fn f(x: i64) -> &i64 { return
            // &x; }` compiled, while the same body over a LOCAL was refused.
            if (param_names_.count(nm))
                return outliving_params_.count(nm) ? RefProv{{nm}, false}
                                                   : RefProv{{}, /*is_local=*/true};
            auto it = prov_.find(nm);
            return it != prov_.end() ? it->second : RefProv{};
        }
        RefProv p = prov_of(r);
        if (!p.is_local && !p.is_temp && p.params.empty())
            p = prov_of_retained(r);
        return p;
    }

    RefProv prov_of(lir_view::ExprRef e) const {
        if (!e) return {};
        using namespace lir_view;
        using Code = lir_schema::expr::Code;
        const auto* pool = prog_.type_pool.impl();

        switch (e.kind()) {
            case Code::VarRef: {
                EVarRefView v{e};
                std::string name(v.name());
                if ((logos::probe::on("fpprov") || logos::probe::on("fpboth")) &&
                    closure_param_names_.count(name) && is_ref_kind(e.type(pool)))
                    return {{}, /*is_local=*/true};
                if (param_names_.count(name) && is_ref_kind(e.type(pool)))
                    return {{name}, false};
                auto it = prov_.find(name);
                if (it != prov_.end()) return it->second;
                return {};
            }
            case Code::AddrOf: {
                EAddrOfView v{e};
                std::string name(v.var_name());
                if (is_materialized_temp_name(name))
                    return {{}, /*is_local=*/false, /*is_temp=*/true};
                // #138 — see the VarRef arm: a BY-VALUE OWNED param is frame
                // storage, so `&param` is as local as `&local`. This is the arm
                // `return &x;` actually reaches (an AddrOf, not a VarRef), which
                // the `[retgate]` trace settled in one run after the VarRef edit
                // alone left `prov{loc=0 np=1}` unchanged.
                if (param_names_.count(name))
                    return outliving_params_.count(name)
                             ? RefProv{{name}, false}
                             : RefProv{{}, /*is_local=*/true};
                if (var_has(NO_SLOT, name))      return {{},     true};
                // A `const` item is materialised into THIS frame at each use
                // (no const-eval by design), so `&K` is frame-rooted and must
                // answer is_local — the same verdict `&local` gets, which is
                // what makes the RETURN gate refuse and the in-scope `let`
                // keep admitting. Reached only after the param and local arms
                // above, so a local or param SHADOWING a const still wins.
                // A `static` is never in this set: its borrow stays admitted.
                if (ts_.frame_consts.count(name))
                    return {{}, /*is_local=*/true};
                return {};
            }
            case Code::AddrOfTemp: {
                // ── #92 CONST PROMOTION ────────────────────────────────────
                // Rust promotes `&<const expr>` to `&'static`: the referent
                // is materialised in READ-ONLY STATIC STORAGE, never in the
                // frame, so there is nothing to dangle into and nothing may
                // be refused. NOT a relaxation of this check — mlir_gen's
                // EAddrOfTemp arm reads the SAME predicate and emits the
                // global, and it fails closed if it cannot (see
                // include/logos/compiler/const_promote.hpp for why the two
                // halves must be one function). Answering `{}` here — no
                // provenance at all, not "local but tolerated" — is what
                // makes the LET, ASSIGN and RETURN gates all agree, because
                // there is nothing for any of them to be about.
                // Imported witnesses: pass/regions/regions-bot (`&0i64`),
                // pass/array-slice-vec/empty-slice-return-b172 (`&[]`).
                if (const_promote::is_promoted_borrow(e, pool)) return {};
                // B74 gap: `&literal` / `&<temp_expr>` whose inner expr
                // is rooted in a temporary (literal, fresh struct lit,
                // call result, etc.) yields a dangling reference when
                // returned. If the inner traces back to a parameter or
                // a long-lived var via field/deref chains, prov_of of
                // the inner already returns that — propagate it.
                EAddrOfTempView v{e};
                // A borrow of a *materialized statement-temporary* (the `__rtmp_N`
                // local that materialize_recv_ref hoists for a fresh rvalue
                // receiver, e.g. `make().view()` → `(&__rtmp_0).view()`) is
                // statement-scoped: __rtmp_N drops at the end of the statement, so
                // a ref into it dangles the moment it escapes. Mark is_temp.
                if (auto in = v.inner();
                    in && in.kind() == Code::VarRef &&
                    is_materialized_temp_name(EVarRefView{in}.name()))
                    return {{}, /*is_local=*/false, /*is_temp=*/true};
                auto inner_prov = prov_of(v.inner());
                if (!inner_prov.params.empty() || inner_prov.is_local ||
                    inner_prov.is_temp)
                    return inner_prov;
                // A DIRECT `&<literal/struct-lit/call>` bound to a `let` is
                // lifetime-EXTENDED in Rust (`let r = &mut 5;` keeps the temporary
                // alive as long as `r`) → NOT dangling at the binding; mark
                // is_local so it is caught only when RETURNED past the scope
                // (check_return_value). This is DISTINCT from the materialized
                // `__rtmp` case above, which is statement-scoped + NOT extended.
                if (is_temporary_value_expr(v.inner()))
                    return {{}, /*is_local=*/true, /*is_temp=*/false};
                // Front (c): a borrow rooted at a VALUE local (`&c.x`, `&c.a[i]`, or
                // a deref of a value-local ref/smart-ptr like `&*h`) is dangling if
                // returned. `value_local_root` does the walk + value-local check.
                bool temp_root = false;
                if (!value_local_root(e, pool, &temp_root).empty() || temp_root)
                    return {{}, /*is_local=*/true, /*is_temp=*/false};
                return {};  // unknown — conservative-accept
            }
            case Code::FieldRead: {
                // A projection THROUGH a raw pointer is a raw deref (see
                // value_local_root's identical rule): the pointee is not tied
                // to the local holding the pointer. Rust parity.
                auto r = EFieldReadView{e}.receiver();
                if (r && r.type(pool) && r.type(pool).kind() == LogosType::Kind::Ptr)
                    return {};
                return prov_of(r);
            }
            case Code::Deref: {
                // Same rule for an explicit `*p`. MEASURED on the full stdlib
                // build (NOT on ctest, which links prebuilt archives):
                // `Iterator::reduce`'s deliberate launder
                // `let init: Item = *((&v) as *const Item);` propagated `v`'s
                // is_local through Deref→Cast→AddrOf, and once F4's return gate
                // started asking about `Option<&T>` returns at all, every
                // `SliceIter::reduce` instantiation was refused.
                auto op = EDerefView{e}.operand();
                if (op && op.type(pool) && op.type(pool).kind() == LogosType::Kind::Ptr)
                    return {};
                return prov_of(op);
            }
            case Code::TupleIndex:
                return prov_of(ETupleIndexView{e}.receiver());
            case Code::Cast:
                return prov_of(ECastView{e}.operand());
            case Code::IndexRead:
                return prov_of(EIndexReadView{e}.receiver());
            // D-c: THE SLICE SPELLINGS WERE MISSING FROM *THIS* WALK ONLY.
            // `fn f(x: i64) -> &[i64] { return &[x]; }` compiled, linked, and
            // returned garbage (measured: 81 where 1 is correct) — and so did
            // the NAMED-local twin `let a: [i64;2] = [x,x]; return &a;`, which
            // is not a temporary at all. `[retgate]` says the gate is REACHED
            // (`retkind=26 typed=1`) and answers `prov{loc=0 tmp=0 np=0}`,
            // while the SAME trace line prints `srcs=[a,]` — because
            // `collect_ref_sources_paths` has had the SliceLit/SliceIndex pair
            // since D1 residuals R1/R2 and this switch never got them. The
            // answer was already computed at the decision site, in the string
            // the diagnostic would have printed.
            // SlicePtr is the node an array→slice coercion actually returns
            // (retkind 26, not 23); all three are transparent projections of
            // the underlying place, exactly like IndexRead one arm up.
            case Code::SliceLit:
                return prov_of(ESliceLitView{e}.base());
            case Code::SlicePtr:
                return prov_of(ESlicePtrView{e}.slice());
            // ⚠ THE FAMILY HAS THREE MEMBERS, NOT TWO. The comment above
            // names the pair `collect_ref_sources_paths` has had since D1
            // residuals R1/R2 — SliceLit/SLICE_INDEX — and then added
            // SlicePtr as the second member instead. `fn f() -> &i64 { let
            // a: [i64;2] = [1i64,2i64]; let s: &[i64] = &a; return
            // &s[0u64]; }` therefore still compiled and still dangled:
            // measured with an intervening frame-stomping call, the exit
            // code IS the clobber constant (9 -> 9, 40 -> 40, 71 -> 71)
            // where 1 is correct, and `[retgate]` printed `prov{loc=0 tmp=0
            // np=0}` beside its own `srcs=[a,]`. An element borrow is a
            // transparent projection of the slice's place, exactly like
            // IndexRead two arms up.
            case Code::SliceIndex:
                return prov_of(ESliceIndexView{e}.slice());
            case Code::IfExpr: {
                EIfExprView v{e};
                return merge_prov(prov_of(v.then_val()), prov_of(v.else_val()));
            }
            case Code::BlockExpr:
                return prov_of(EBlockExprView{e}.result());
            case Code::MatchExpr: {
                RefProv merged = {};
                EMatchExprView{e}.each_arm([&](EMatchArmRef arm) {
                    merged = merge_prov(merged, prov_of(arm.value()));
                });
                return merged;
            }
            case Code::ClosureCall:
            case Code::FnPtrCall: {   // H4 — see note_closure_caps
                TypeRef rt = e.type(pool);
                if (!is_ref_kind(rt) && !is_borrow_carrying_type(rt)) return {};
                const std::vector<std::string>* caps = closure_caps_of(call_callee(e));
                // ── H4-e: A CAPTURE-LESS CLOSURE'S RESULT TIES TO ITS SOLE
                //          REFERENCE ARGUMENT, BY THE ELISION RULE ──────────
                // THE ARGUMENTS OF A CLOSURE CALL WERE NEVER READ HERE. The
                // FnPtrCall branch below walks them (summary first, then the
                // plain-ref/by-value-bc fallback); neither ClosureCall exit
                // did — `if (!caps) return {}` answered NOTHING for a
                // CAPTURE-LESS closure, and the caps loop that follows merges
                // captures ONLY. MEASURED BY HAND on f41cb31ce, multi-line
                // sources, one token apart:
                //   fn id(x:&i64)->&i64{return x;}
                //   fn get()->&i64{let l:i64=5; return id(&l);}      REFUSED
                //   fn get()->&i64{let l:i64=5;
                //       let c=|x:&i64|->&i64{return x;}; return c(&l);}  ADMITTED
                //   let r=id(&l); l=6;                               REFUSED
                //   let c=|x:&i64|->&i64{...}; let r=c(&l); l=6;     ADMITTED
                //   let p=(|x:&i64|->&i64{...},); return p.0(&z);    ADMITTED
                // and a fn-POINTER local in the same slot is REFUSED, so the
                // discriminator is neither indirection nor a region: it is
                // this arm's two ClosureCall exits.
                // ⚠ THE FIRST SITE PRICED FOR THIS WAS THE WRONG ONE.
                // `capargtie` armed the same idea in collect_ref_sources_paths
                // and measured 21 fires / CEILING 0 / COST 2: the dangling-
                // RETURN gate reads `prov_of`, not the §B6 source walk, so the
                // repair there could not reach the shape it was written for.
                // See PROBES.md.
                // PRICED as three probes over the whole 349-row ledger, one
                // build (PROBES.md, gate-db 69 -> 70/71/72):
                //   capprovarg   11420 fires  CEILING 6  COST 0  both exits
                //   capprovnocap 11420 fires  CEILING 6  COST 0  this exit alone
                //   capprovcaps  11428 fires  CEILING 0  COST 0  the caps loop
                // 2 and 3 partition 1, so the narrow half is the whole of it
                // and the caps loop buys nothing: it already answers is_local
                // for every captured local, so a CAPTURING closure returning
                // its own param is refused on the unpatched tree today and no
                // argument tie can move it.
                //
                // ⚠ WHAT LANDED IS NARROWER THAN THE PROBE, AND THE PROBE IS
                // WHY. `capprovnocap` merged EVERY reference argument into the
                // result, and that refuses this, which nothing in the corpus
                // contains (ce7, hand-written):
                //   fn get(p:&i64)->&i64 {
                //       let l:i64=5;
                //       let c=|x:&i64,y:&i64|->&i64{return y;};
                //       return c(&l,p); }
                // The result derives from `y` alone; the tie to `&l` is a
                // legal-program refusal, and a ledger row may not be bought
                // with one. So the rule landed here is the LANGUAGE'S OWN
                // elision rule and not "merge the arguments": with exactly ONE
                // reference-typed argument the result can only borrow THAT
                // one, and the answer is exact. With two or more there is no
                // elision rule to apply — the tree refuses to let a FN even be
                // WRITTEN in that shape (E0106, "more than one input lifetime
                // and no `&self`"), and a closure has no syntax to annotate
                // the tie: `|x:&i64,y:&'b i64|->&'b i64` parses and is still
                // read blanket-wise. Those stay ADMITTED, i.e. still a hole,
                // and it is the honest one — see PROBES.md, group A residue.
                //
                // The six rows this closes all pass exactly one reference.
                auto sole_ref_arg = [&]() -> ExprRef {
                    // Only a ClosureCall reaches here: the `!caps` FnPtrCall
                    // branch above returns unconditionally.
                    ExprRef sole{};
                    unsigned n = 0;
                    EClosureCallView{e}.each_arg([&](ExprRef a) {
                        if (!a) return;
                        TypeRef at = a.type(pool);
                        if (is_plain_ref_kind(at) ||
                            (!is_ref_kind(at) && is_borrow_carrying_type(at))) {
                            ++n; sole = a;
                        }
                    });
                    return n == 1 ? sole : ExprRef{};
                };
                // G1: a GENUINE fn pointer has no captures — this is where the
                // result half leaked. Consult the resolved callee's summary,
                // and fall back to Call's summary-less rule (plain-ref args +
                // by-value borrow-carrying args) when it does not resolve.
                if (!caps && e.kind() == Code::FnPtrCall) {
                    EFnPtrCallView fv{e};
                    RefProv merged = {};
                    if (const FlowSummary* fs = flow_of_fnptr(fv.callee())) {
                        size_t i = 0;
                        fv.each_arg([&](ExprRef a) {
                            if (a && i < fs->nparams && (fs->to_result & (1ull << i)))
                                merged = merge_prov(merged, prov_of(a));
                            ++i;
                        });
                        return merged;
                    }
                    fv.each_arg([&](ExprRef a) {
                        if (!a) return;
                        TypeRef at = a.type(pool);
                        if (is_plain_ref_kind(at) ||
                            (!is_ref_kind(at) && is_borrow_carrying_type(at)))
                            merged = merge_prov(merged, prov_of(a));
                    });
                    return merged;
                }
                // THE CAPTURE-LESS EXIT. `note_closure_caps` ERASES the
                // entry when the list is empty, so a closure that captures
                // nothing is indistinguishable here from an unresolvable
                // callee, and both used to take the permissive answer. A
                // capture-less closure has no provenance of its own, so its
                // result's provenance is its argument's or it is nothing.
                if (!caps) {
                    ExprRef a = sole_ref_arg();
                    return a ? prov_of(a) : RefProv{};
                }
                RefProv merged = {};
                for (auto& cap : *caps) {
                    if (auto it = prov_.find(cap); it != prov_.end()) {
                        merged = merge_prov(merged, it->second);
                        continue;
                    }
                    // A captured VALUE-local is the closure's own frame data —
                    // a borrow-carrying result built from it dangles exactly as
                    // `c.mk()` on a value-local receiver does (same rule, one
                    // indirection further out). A captured PARAM outlives the
                    // call and contributes nothing.
                    if (var_has(NO_SLOT, cap) && !param_names_.count(cap))
                        merged.is_local = true;
                }
                // THE CAPTURING EXIT IS LEFT ALONE, AND THAT IS MEASURED,
                // not an omission: a closure with captures still returns a
                // reference that may name an ARGUMENT and the loop above
                // cannot say so, but `capprovcaps` priced that widening at
                // CEILING 0 over the whole ledger.
                return merged;
            }
            case Code::MethodCall: {
                // Lifetime elision: a method returning `&T` borrows its receiver
                // (`fn view(&self) -> &T` → the output ties to `&self`). So the
                // result's provenance IS the receiver's. Crucially, if the
                // receiver is a *temporary* (`make().view()` — a fresh value with
                // no named storage), the returned borrow points into that
                // temporary, which drops at the end of the statement → is_temp.
                // (A non-ref result is a plain owned value — no provenance.)
                EMethodCallView v{e};
                // A `&T` result borrows the receiver; SO DOES a `#[borrow_carrying]`
                // value result (WAny) — its value may be a Ref into the receiver's
                // arena. Both tie the result's provenance to the receiver.
                // A FAT value result (str/&[T]/borrowed DST) ties only when the
                // method's own signature borrows self: `Vec<str>::get -> T` copies
                // a STORED borrow out — its lifetime is the element's, not the
                // receiver's (returning it past the receiver is fine, Rust parity).
                //
                // ── #77 round 2 / ONE ARM OVER: THE SAME DOOR DEFECT ───────
                //
                // Round 1 found the Code::Call arm returning {} on its ENTRY
                // GATE, before the summary consult that sits right below it
                // and answers correctly. The MethodCall arm's fat gate is the
                // identical mistake: `!result_borrows_self(v)` is a fact about
                // the callee's SIGNATURE, and the signature does not say where
                // a borrow goes — which is the sentence the whole summary
                // plane exists to write.
                //
                // MEASURED TWIN, one variable (method vs free fn), at f0a60ff3:
                //   impl K { pub fn thru(&self, s: str) -> str { return s; } }
                //   pub fn bad(k: &K) -> str {
                //       let o: String = String::from("hello");
                //       return k.thru(o.as_str()); }        // rc 0
                //   pub fn thru2(s: str) -> str { return s; }
                //   pub fn bad() -> str {
                //       let o: String = String::from("hello");
                //       return thru2(o.as_str()); }         // rc 1
                // Same callee body, same argument, same use.
                //
                // The repair is the Call arm's, term for term: the gate no
                // longer RETURNS, it records that elision alone would have
                // said nothing, and the summary is asked. When the summary is
                // absent or over-approximate the pre-#77 answer stands
                // (`return {}`) — an over-approximate mask on a fat result is
                // exactly the eval_sexpr false-refusal the Call arm measured
                // and declined. `Vec<str>::get -> T` — the shape the gate was
                // written for — keeps admitting for the RIGHT reason now: its
                // summary says the borrow comes from the ELEMENT, not from
                // self, so nothing local is merged.
                TypeRef m_rt = e.type(pool);
                bool m_bc = is_borrow_carrying_type(m_rt);
                bool m_fat_gate_shut = false;
                {
                    bool plain = is_plain_ref_kind(m_rt);
                    bool fat   = !plain && is_ref_kind(m_rt);
                    if (!plain && !fat && !m_bc) return {};
                    m_fat_gate_shut = fat && !m_bc && !result_borrows_self(v);
                }
                // D1 round 3 / F1 + F2 — the receiver is an OPERAND, not a
                // privileged one. Two defects, one site:
                //   F1 (under-refusal): this case read ONLY the receiver, so a
                //     by-value borrow-carrying ARGUMENT rooted at a fn-local
                //     escaped — `fn leak(i: &Id) -> B { let c = C{v:1};
                //     return i.thru(c.mk()); }` compiled. The LOAN channel
                //     (collect_ref_sources' MethodCall arm) already took
                //     by-value bc args; the two channels disagreed at the same
                //     expression kind and the loan channel was right.
                //   F2 (over-refusal): the value-local fallback below adopted
                //     the receiver's locality UNCONDITIONALLY once the result
                //     carried borrows, so an unrelated value-local receiver
                //     made the result look local — `fn thru(c: &C) -> B { let
                //     i = Id{z:0}; return i.th(c.mk()); }` refused while the
                //     identical program in FREE-FN spelling admitted.
                // Both are "the operand that actually contributes", and the
                // callee's BORROW-FLOW SUMMARY is what says which operand does
                // (`Id::thru(&self, b: B) -> B` summarises as result<-{b},
                // measured). Elision cannot answer it: `&self -> B` ties the
                // result to self by the declared contract even when the body
                // returns its argument. With no summary (the documented (a)-(d)
                // hole) every clause below keeps its pre-F3 behaviour.
                const FlowSummary* fs = flow_of_method(v);
                // #77 round 2: elision said nothing here, so only an EXACT
                // summary may speak — and if there is none, the arm keeps its
                // pre-#77 silence rather than falling through to the elision
                // fallback below (which would tie the result to the receiver,
                // the very over-refusal the fat gate exists to prevent).
                if (m_fat_gate_shut && (!fs || fs->over_approx)) return {};
                RefProv rp = {};
                bool recv_contributes = true;
                if (fs) {
                    recv_contributes = (fs->to_result & 1ull) != 0;
                    std::vector<ExprRef> ops;
                    ops.push_back(v.receiver());
                    v.each_arg([&](ExprRef a){ ops.push_back(a); });
                    for (size_t i = 0; i < ops.size() && i < fs->nparams; ++i)
                        if (fs->to_result & (1ull << i))
                            rp = merge_prov(rp, prov_of(ops[i]));
                } else {
                    rp = prov_of(v.receiver());
                    // F1's half that does NOT depend on a summary: a by-VALUE
                    // borrow-carrying argument carries its provenance exactly
                    // like a plain-ref one.
                    v.each_arg([&](ExprRef a) {
                        if (!a) return;
                        TypeRef at = a.type(pool);
                        if (!is_ref_kind(at) && is_borrow_carrying_type(at))
                            rp = merge_prov(rp, prov_of(a));
                    });
                }
                // A by-VALUE-self adapter (`.enumerate()` / `.filter()` —
                // `self: Self`) CONSUMES the receiver: a temporary receiver
                // is moved INTO the result, not dropped at stmt end. Its
                // carried borrow (v.iter()'s borrow of v) flows through via
                // the recursive prov, but no E0716 temp applies. Only a
                // ref-self method's result points INTO the temporary.
                if (recv_contributes && is_temporary_value_expr(v.receiver()) &&
                    method_self_kind(v) != 0)
                    rp.is_temp = true;
                // The receiver may be a BARE VarRef value-local (e.g. `Rc::deref`'s
                // `self` is `h` directly, not `&h`) — prov_of(VarRef) doesn't flag
                // value-locals, so catch it here: a `&T`/borrow-carrying result of a
                // method on a value-local receiver borrows that local. F2: only
                // when the receiver actually reaches the result.
                if (recv_contributes && rp.params.empty() && !rp.is_local &&
                    !rp.is_temp &&
                    !value_local_root(v.receiver(), pool).empty())
                    rp.is_local = true;
                // ── #86 SUB-SITE C: THE BORROW THE RECEIVER *CARRIES* ──────
                //
                // `struct W { v: str }  impl W { fn get(&self) -> str { self.v } }`
                // summarises `result<-0 EXACT` — and that is CORRECT: it is
                // `stored_shared_extract`, the Rust-parity rule that a `str`
                // copied out of a `&W` has the FIELD's lifetime, not `&self`'s
                // (see borrow_flow_summary.inc). The bit says "self does not
                // reach the result", so `recv_contributes` is false and both
                // clauses above are (rightly) skipped.
                //
                // What nothing then asks is where the FIELD's borrow came
                // from. Measured, both at rc=0 before this clause:
                //   `let w = W{v:o.as_str()};   return w.get();`   (C1)
                //   `return mk(o.as_str()).get();`                 (RB3b/B3)
                // — the extracted `str` has the lifetime of the borrow the
                // receiver VALUE holds, and that borrow is of a fn-local.
                //
                // NOT the F2 over-refusal this is easy to confuse with. F2 is
                // "an unrelated value-local RECEIVER made the result look
                // local" — `Id{z:0}` carries no borrow at all, so
                // type_may_carry_borrow is false for it and this clause never
                // opens. Only the receiver's CARRIED escape fact is adopted
                // (is_local/is_temp); a param-rooted carry contributes
                // nothing, which is what keeps `walk_program_params`'
                // `prm.relc_ty[i]` (str views of the query text stored in a
                // `&MacroParams`) admitting.
                if (!recv_contributes && !rp.is_local && !rp.is_temp) {
                    TypeRef rvt = v.receiver().type(pool);
                    if (rvt && type_may_carry_borrow(rvt) &&
                        !residency_exemption_holds(rvt, v.receiver())) {
                        RefProv cp = carried_prov_of_recv(v.receiver());
                        if (cp.is_local || cp.is_temp) {
                            if (std::getenv("LOGOS_86_TRACE"))
                                fprintf(stderr, "[#86trace-carry] fn=%s loc=%d tmp=%d\n",
                                        fn_name_.c_str(), (int)cp.is_local,
                                        (int)cp.is_temp);
                            rp.is_local = rp.is_local || cp.is_local;
                            rp.is_temp  = rp.is_temp  || cp.is_temp;
                        }
                    }
                }
                return rp;
            }
            case Code::Call: {
                // A free fn / ctor returning a `#[borrow_carrying]` value
                // (`WAny::from(&x)`) may alias one of its REFERENCE args — merge the
                // provenance of each ref arg. (`WAny::from(7i64)` has no ref arg →
                // empty → freely returnable.) Non-borrow-carrying = caller-owned.
                // ── #77: THE ENTRY GATE WAS KEYED ON THE DECLARED ATTRIBUTE ──
                //
                // THE DEFECT (measured, sandbox/escchan/r77.logos):
                //   `pub fn keepr(x: &i64) -> &i64 { return x; }`
                //   `pub fn bad() -> &i64 { let t = 9i64; return keepr(&t); }`
                // admitted at rc=0 — a plain thin `&i64` escaping its frame
                // through a one-line identity call — while the direct twin
                // `return &t;` refused at rc=1. `is_borrow_carrying_type`
                // names Enum/Struct/ZonedStruct (the `#[borrow_carrying]`
                // fixpoint) and recurses type-args; a bare `&i64` is NONE of
                // those, so this arm returned {} before it ever reached the
                // summary consult below — which is right there and correct.
                // The channel was not summary-blind by its rule, it was
                // summary-blind by its DOOR.
                //
                // `type_may_carry_borrow` is the predicate the §B6 deposit arm
                // (EC::Call, round 14 / Q5) already moved to for exactly this
                // question, and the two channels disagreed at the same
                // expression kind. What the arm then MERGES is unchanged —
                // with a summary, the args whose bit reaches the result; with
                // none, plain-ref + by-value bc args — so a call returning a
                // scalar still contributes nothing, and a call returning
                // `&i64` built from no borrowing arg still merges {}.
                //
                // ── AND THE NEW DOOR TAKES ONLY EXACT SUMMARIES ────────────
                //
                // MEASURED RED LIST of the un-narrowed widening on a full
                // stdlib rebuild: 3, all in `eval_sexpr` (stdlib/mem/deem/
                // tpl.logos:329/420/424, `return RtVal::S(intern(scratch,&t))`),
                // all FALSE — `intern` retains only its ARENA, and its
                // `result<-0x3` is the (a)-(d) fallback firing on the
                // cross-package `Writ::wstring` (see FlowSummary::approx).
                // Refusing on a GUESSED bit is exactly the "moves in the
                // refusing direction over code no gate has ever checked"
                // hazard, so the new door asks for an EXACT summary. The
                // OLD door — a `#[borrow_carrying]` result — is untouched and
                // still takes approx summaries, because that is the behaviour
                // every bc_* pin was measured against.
                //
                // UNCOVERED, and stated rather than hidden: a plain/fat-ref
                // result whose callee summary is approx keeps admitting.
                // MEASURED by fire-print over one `stdlib/mem` module build
                // (the print was then removed): the new door is TAKEN 408
                // times on EXACT summaries and SHUT 238 times on approx ones.
                // Neither number is zero, so this is neither a dead arm nor a
                // narrowing that closed the door it opened.
                bool bc_result = is_borrow_carrying_type(e.type(pool));
                if (!bc_result && !type_may_carry_borrow_erased(e.type(pool))) return {};
                RefProv merged = {};
                // D1 round 3 / F0 — this arm merged provenance only from
                // `is_plain_ref_kind` args, so a by-VALUE borrow-carrying
                // argument contributed NOTHING: `fn idb(b: B) -> B { return b;
                // } fn leak() -> B { let c = C{v:1}; let b = c.mk();
                // return idb(b); }` compiled, while `return b;` directly
                // refused and both wrapper twins (Option::Some(b),
                // Wrap{b:..}) refused. `Box::new` is such a Call. The LOAN
                // channel's Call arm already took by-value bc args — the two
                // channels disagreed at the same expression kind.
                //
                // With a summary, merge exactly the args the callee's body
                // lets reach the result; without one (the documented (a)-(d)
                // hole) keep the plain-ref rule AND add the by-value bc args,
                // which is the loan channel's rule.
                ECallView cv{e};
                // ── MISS 2 / RUST'S ONE-INPUT LIFETIME ELISION ─────────────
                // `fn f(x: i64) -> &[i64] { let a: [i64;3] = [x,x,x]; return
                // &a[0..2]; }` COMPILED, LINKED and DANGLED — measured with an
                // intervening frame-stomping call, the exit code IS the
                // clobber constant (9 -> 9, 40 -> 40, 71 -> 71) where 1 is
                // correct. The spelling lowers to `Call(slice_get_range,
                // [SliceLit{AddrOfTemp(a)}, lo, hi])` and BOTH halves of this
                // arm said nothing: the fallback filters drop the SliceLit by
                // its TYPE (`is_plain_ref_kind` is Ref/MutRef/TraitObject, not
                // Slice), and the summary route answers mask 0 — for the four
                // missing taint arms recorded on `forms_borrow_at_call`, NOT
                // because the callee does not retain its argument. `[retgate]`
                // printed the spread in one run: `prov{loc=0 tmp=0 np=0}`
                // beside its own `srcs=[a,]`.
                //
                // ⚠ AND THE SUMMARY MAY NOT SIMPLY BE OVERRIDDEN — MEASURED
                // TWICE, EACH TIME AS A RED STDLIB BUILD, WHICH IS THE ONLY
                // ORACLE THAT SEES IT (`ctest` links PREBUILT archives):
                //   • "any borrow-forming arg ties"  -> reds
                //     `writ/parser.parse` (`&mut p as *mut P`),
                //     `writ/wbs.wbs_read` (`&mut pos`, `&mut err`),
                //     `wql/join_sel.decide_join_step` (`&st.tl`);
                //   • "…and the result is ref-kind" -> still reds
                //     `deem/tpl.eval_sexpr`: `intern(scratch, &u2)` returns a
                //     `str` borrowed from SCRATCH while `&u2` is a local
                //     String. The callee's summary says which; the call site
                //     cannot.
                // THE RULE THAT SURVIVES BOTH IS RUST'S OWN: elision assigns
                // the output lifetime to an input ONLY when there is EXACTLY
                // ONE reference input. `slice_get_range(s: &[T], lo: i64, hi:
                // i64) -> &[T]` has one and elides; `intern(&Writ, &String)`,
                // `parse(&Writ, str)` and `step_equi_key(&_, str, &_)` have
                // two or three, so Rust would demand an annotation — and here
                // the SUMMARY is that annotation, still deciding exactly as it
                // did before this round. A RAW POINTER is not a reference
                // input in Rust and is not counted, which is also what keeps
                // the three out-param cursors above off this path.
                // NOT a new deposit door: nothing is written, and this is the
                // arm that already merged plain-ref args.
                int elided_to = -1;
                {
                    int nref = 0, idx0 = 0;
                    cv.each_arg([&](ExprRef a) {
                        int k = idx0++;
                        TypeRef at0 = a ? a.type(pool) : TypeRef(nullptr);
                        if (at0 && at0.kind() != LogosType::Kind::Ptr &&
                            is_ref_kind(at0)) { ++nref; elided_to = k; }
                    });
                    if (nref != 1 || !is_ref_kind(e.type(pool))) elided_to = -1;
                    // ⚠ AN **EXACT** SUMMARY OUTRANKS ELISION — MEASURED, and
                    // the corpus named the price in seven fixtures. `pub fn
                    // stored_str(c: &Cfg) -> str { return c.name; }` has one
                    // reference input and a ref-kind result, so elision would
                    // tie its result to `&c` — and #77 round 2 already pinned
                    // the opposite in pass/bc_esc_summary_seed_field_admit,
                    // whose whole subject is that the `str` handed back is the
                    // LITERAL's and its mask reads `result<-0  EXACT`. Six
                    // memoria fixtures and `metaclass_str_generic`
                    // (`bt_descend_get`) red the same way.
                    // Elision is a rule about a SIGNATURE; an exact summary is
                    // a measurement of the BODY, and the body wins. It is only
                    // where the summary admits it is a guess (`over_approx`,
                    // which is what `slice_get_range`'s mask-0 is) that Rust's
                    // rule is the better guess.
                    if (const FlowSummary* fs0 = flow_of_call(cv.callee());
                        fs0 && fs0->available && !fs0->over_approx)
                        elided_to = -1;
                }
                // ── D-a: AN ANONYMOUS RVALUE TEMPORARY, ONCE IT PASSES ─────
                //          THROUGH A CALL
                // `let x: It = iter(&mk());` admitted; `{ let t = mk(); x =
                // iter(&t); }` refused. The isolating control had to be
                // REBUILT because the filed one moved TWO properties at once
                // (temporary->named AND let->assign): `da_iso_tmp_assign`
                // (temporary + assign) admits and `da_iso_let_named` (named +
                // let) refuses, so it is the temporary and not the `let`.
                // MECHANISM: prov_of's AddrOfTemp arm answers
                // `{is_local=true, is_temp=false}` for a direct `&<rvalue>` on
                // the RVALUE-LIFETIME-EXTENSION rule — correct for
                // `let r = &mk();`, which rustc extends — and this arm merged
                // that answer through the call UNCHANGED. The Let/Assign gate
                // reports on `is_temp` ALONE, so the whole shape walked past
                // it. Extension is a property of the CONSUMING SITE, not of
                // the expression: `let r = &mk();` extends, `let r =
                // thru(&mk());` is E0716 in Rust, and the two were
                // indistinguishable here.
                // THE RULE IS NOT NEW — IT IS THE MethodCall ARM'S, ONE ARM
                // UP: "a temporary receiver + a ref-self method => is_temp"
                // (`recv_contributes && is_temporary_value_expr(v.receiver())
                // && method_self_kind(v) != 0`). This is the same statement
                // for a temporary ARGUMENT, applied where the argument is
                // merged so it cannot fire for an argument the callee does not
                // let reach the result. NOT a re-siting of prov_of's
                // AddrOfTemp arm (the diagnosis's recommendation): that would
                // change the answer for every consumer, including the direct
                // `let` the extension rule exists for, and this does not touch
                // it at all.
                auto merge_arg_prov = [&](ExprRef a) {
                    RefProv ap = prov_of(a);
                    if (a.kind() == Code::AddrOfTemp &&
                        is_temporary_value_expr(EAddrOfTempView{a}.inner()))
                        ap.is_temp = true;
                    merged = merge_prov(merged, ap);
                };
                if (const FlowSummary* fs = flow_of_call(cv.callee())) {
                    // ── #77 round 2: RE-MEASURED, AND THE DOOR STAYS SHUT ──
                    //
                    // Round 1 shut this on a scalar `approx` flag that only
                    // ever recorded bits GUESSED IN; round 2 gave the flag its
                    // missing half — `taint_of` and `walk_stmt` now STATE the
                    // kinds that carry nothing instead of defaulting them, so
                    // a mask that is simply incomplete can no longer wear the
                    // EXACT label (see FlowSummary's note and its fire-print
                    // measurement) — and corrected the seed, so the question
                    // was asked again on a mask that no longer over-claims.
                    //
                    // CONTROL, one variable (`if (false && ...)` here, full
                    // stdlib rebuild): the red list is 3, unchanged from round
                    // 1 — `eval_sexpr` at stdlib/mem/deem/tpl.logos:329/420/424,
                    // all three `return RtVal::S(intern(scratch,&t))`, all
                    // FALSE. `intern`'s `result<-0x3` is still the (a)-(d)
                    // fallback firing on the cross-package `Writ::wstring`, and
                    // nothing in this round makes the guessed bit and a real
                    // one distinguishable: both are "a bodyless callee's result
                    // tied to every borrowing operand". Closing it needs the
                    // CALLEE's body, i.e. cross-arena summarisation (#81).
                    //
                    // THE RESIDUE, stated where the door is rather than in a
                    // report. A borrow-returning call whose callee summary is
                    // over-approximate keeps admitting, and it admits a REAL
                    // dangle off the stdlib alone — 12 lines, no attributes:
                    //   pub fn f(a: &String, b: &String) -> str {
                    //       let x: i64 = a.len();
                    //       if x > 100000i64 { return a.as_str(); }
                    //       return b.as_str(); }
                    //   pub fn bad(a: &String) -> str {
                    //       let b: String = String::from("hello");
                    //       return f(a, &b); }      // rc 0; f is result<-0x3 OVER
                    // (`LOGOS_DUMP_FLOWS=f` prints the mask and the tag.)
                    // ⚠ `&& elided_to < 0` — MEASURED, AND IT IS THIS LINE
                    // THAT ACTUALLY HELD MISS 2 OPEN. `slice_get_range`'s
                    // result is `&[i64]`, which is ref-kind but NOT
                    // `is_borrow_carrying_type` ("a slice of i64 carries
                    // nothing BY NAME"), so `!bc_result && over_approx`
                    // returned {} BEFORE the arg loop ran at all — proved with
                    // a one-run trace, not inferred: the arm was entered
                    // (`elided_to=0 fs=1 retref=1`) and the loop body printed
                    // nothing. The bail is about an over-approximate MASK; an
                    // elided lifetime is not a mask reading, so it is not what
                    // this exit is about.
                    if (!bc_result && fs->over_approx && elided_to < 0) return {};
                    size_t i = 0;
                    // ⚠ MEASURED AND REVERTED: `|| forms_borrow_at_call(a)`
                    // ALSO HERE. A summary is the callee's own measured fact
                    // and OUTRANKS a call-site shape; overriding it red the
                    // STDLIB BUILD at `wql/join_sel.decide_join_step` —
                    // `step_equi_key(&st.tl, …)`, whose summary says the
                    // argument does not reach the result, and whose `&st.tl`
                    // is a plain ref this loop would then have tied. The
                    // node-kind rule belongs where there is NO summary to
                    // consult, and that is the fallback below.
                    // ⚠ THE SUMMARY DOES NOT OUTRANK ELISION WHEN THE RESULT
                    // IS ITSELF A REFERENCE. `slice_get_range`'s mask is 0 —
                    // for the four reasons recorded on `forms_borrow_at_call`
                    // above, none of which is "it does not retain its
                    // argument" — so `&a[0..2]` over a LOCAL array was
                    // admitted by this loop and dangled (exit code == the
                    // clobber constant). `ret_is_ref` is Rust's own elision
                    // rule and the predicate's OWN rule one level down (its
                    // Call recursion already tests `is_ref_kind(a.type)`):
                    // a fn returning a REFERENCE ties that reference to an
                    // input, a fn returning a STRUCT elides nothing.
                    // MEASURED — this conjunct is what keeps the summary
                    // authoritative for the three stdlib functions the
                    // un-gated version red: `writ/parser.parse` -> WAny,
                    // `writ/wbs.wbs_read` -> WAny, `wql/join_sel.
                    // decide_join_step` -> StepSel. None is ref-kind; all
                    // three stay on their summaries.
                    cv.each_arg([&](ExprRef a) {
                        if (a && ((i < fs->nparams && (fs->to_result & (1ull << i))) ||
                                  (elided_to >= 0 && (size_t)elided_to == i)))
                            merge_arg_prov(a);
                        ++i;
                    });
                    return merged;
                }
                // No summary at all and a non-bc result: the fallback below is
                // the same signature elision the summarizer's own (a)-(d) arm
                // makes, i.e. a GUESS — the new door does not refuse on it.
                if (!bc_result) return {};
                // Plain `&`/`&mut` args — a by-value slice arg
                // (`tv_build(h, name.as_str(), …)`) is a copied borrow with the
                // element's lifetime; it is not a capture channel for the result.
                size_t fb_i = 0;
                cv.each_arg([&](ExprRef a) {
                    size_t fb_here = fb_i++;
                    if (!a) return;
                    TypeRef at = a.type(pool);
                    if (is_plain_ref_kind(at) ||
                        (!is_ref_kind(at) && is_borrow_carrying_type(at)) ||
                        (elided_to >= 0 && (size_t)elided_to == fb_here))
                        merge_arg_prov(a);
                });
                return merged;
            }
            case Code::StructLit: {
                // An aggregate LITERAL borrows through its borrow-carrying field
                // initialisers: `Wrap { a: WAny::from(&local) }` ties Wrap to the
                // local, so returning the Wrap escapes the borrow. Merge each
                // field-value's provenance (a Pod / owned field contributes {}).
                RefProv merged = {};
                EStructLitView{e}.each_field_value([&](ExprRef fv){
                    merged = merge_prov(merged, prov_of(fv));
                });
                return merged;
            }
            case Code::TupleLit: {
                RefProv merged = {};
                ETupleLitView{e}.each_elem([&](ExprRef el){
                    merged = merge_prov(merged, prov_of(el));
                });
                return merged;
            }
            case Code::EnumLitData: {
                RefProv merged = {};
                EEnumLitDataView{e}.each_payload([&](ExprRef pl){
                    merged = merge_prov(merged, prov_of(pl));
                });
                return merged;
            }
            default:
                // Other rvalues / literals — caller-owned, no borrowed provenance.
                return {};
        }
    }

    // ── Phase 3 + 4: dangling / lifetime check on return ──────────────────

    // ── D1 round 3 / F4: the return gate's mirror of door E ────────────────
    //
    // `is_borrow_carrying_type` answers an ESCAPE question about a TYPE, and an
    // OWNED ERASED WRAPPER's type says nothing: `Box<dyn Fn() -> i64>` is
    // neither a ref kind nor a bc NAME, and neither is `Box<&i64>` (its arg is
    // Kind::Ref, not a `#[borrow_carrying]` name). So check_return_value
    // early-returned and a closure capturing a fn-local borrow — or a plain
    // `Box::new(&c.v)` — walked straight out of the function (measured rc=0 on
    // both; `&dyn` returns were pinned all along by spec/fail/region_diag_1).
    //
    // Round 2 met the same class at the CONSTRUCTION site (door E) and answered
    // it the same way: read the RESULT KIND, not the type name — a value that
    // retains a borrowing operand still holds its borrow whatever its erased
    // type now claims. This is that rule on the return channel. The wrong easy
    // answer, rejected for the same reason as at door E, is "any dyn type-arg
    // is borrow-carrying": that refuses the whole Arc<dyn Snapshot> ecosystem.
    //
    // What counts as a retained BORROWING operand here is wider than door E's
    // by-value-bc test, because a plain `&T` operand is exactly what
    // `Box<&i64>` retains.
    // The RETURN TYPE half of F4's gate, and the half that keeps it narrow.
    // MEASURED: gating on the returned EXPRESSION alone (does it retain a
    // borrowing operand?) over-refused 66 L2 tests — every `impl Debug::fmt`
    // doing `return s.finish();` where `s: DebugStruct` holds a `&mut
    // Formatter`, every `Result<(), Error>` built off a `&self` receiver. The
    // returned VALUE retaining a reference is not the question; the question is
    // whether the return TYPE HIDES one. `Box<&i64>` and `Box<dyn Fn() -> i64>`
    // do — a Ref or an erased dyn/closure sits inside an owned wrapper, and
    // is_borrow_carrying_type answers "no" because neither is a bc NAME.
    // `Result<(), Error>` does not: its type-args are a unit tuple and a plain
    // struct, so nothing about it can outlive the callee's frame.
    //
    // D1 round 4 / N2 + N3 — TWO PLACES THIS GATE NEVER LOOKED. It asked
    // `hides()` of the type's TYPE-ARGS, tuple elements and array element, and
    // of nothing else. Two consequences, both measured:
    //   N2 — a BARE closure return type. `fn leak() -> || -> i64 { let c = …;
    //        let b = c.mk(); return move || -> i64 { return *b.p; }; }` has a
    //        top-level Kind::Closure return type with no type-args at all, so
    //        the gate answered false, the retention gate never opened, and the
    //        walk that would have caught it (retains_borrowing_operand's
    //        `case ClosureBox: return true`) was never reached. The identical
    //        body wrapped in `Box<dyn Fn() -> i64>` refused — only because the
    //        Closure then sat in a type-ARG. The hole is TYPE-shaped, not
    //        provenance-shaped: the same return capturing a plain `&i64` of a
    //        local, with no borrow-carrying struct anywhere, compiled too.
    //   N3 — a closure in a plain struct FIELD of a returned struct.
    //        `struct H { f: || -> i64 }` has no type-args either, and struct
    //        fields were not walked at all, so `return H { f: move || …*b.p };`
    //        compiled while the same struct holding the bc value DIRECTLY
    //        refused. The closure payload, not the struct, lost provenance.
    // Both are fixed HERE and only here: once the gate opens, the existing
    // machinery already resolves the rest (prov_of_retained's `one` recurses
    // into prov_of_retained for a non-ref field value, and prov_of_retained
    // has the ClosureBox arm that reads the capture's provenance out of prov_ —
    // the same bridge the loan channel's round-2 door D uses).
    //
    // The struct walk needs a CYCLE guard that the type-arg/tuple/array
    // recursion never did: a struct may reach itself through a field
    // (`struct N { next: Box<N> }`), and the type-arg step would otherwise
    // recurse forever. `seen` is by struct NAME, which is also what
    // struct_by_name / spec_by_name are keyed by.
    bool type_hides_borrow(TypeRef t) const {
        std::unordered_set<std::string> seen;
        return type_hides_borrow_(t, seen);
    }
    bool type_hides_borrow_(TypeRef t, std::unordered_set<std::string>& seen) const {
        if (!t) return false;
        using K = LogosType::Kind;
        auto hides = [](TypeRef a) {
            if (!a) return false;
            switch (a.kind()) {
                // ERASED payloads only. A bare `&T` type-arg (`Option<&T>`,
                // `Result<&T,E>`) is deliberately NOT here — see
                // `retains_direct_local_borrow` below for why, and for the
                // narrower gate that does cover `Box<&i64>`.
                case K::TraitObject: case K::UnsizedDyn:
                case K::Closure:     case K::ImplTrait:
                    return true;
                default: return false;
            }
        };
        // N2: the type ITSELF. A `hides()` kind reaching this function as the
        // whole return type is the same fact as one reaching it as a type-arg.
        if (hides(t)) return true;
        for (auto a : t.type_args())
            if (hides(a) || type_hides_borrow_(a, seen)) return true;
        if (t.kind() == K::Tuple) {
            for (auto e : t.tuple_elems())
                if (hides(TypeRef(e)) || type_hides_borrow_(TypeRef(e), seen)) return true;
            return false;
        }
        if (t.kind() == K::Array)
            return hides(t.elem()) || type_hides_borrow_(t.elem(), seen);
        // N3: struct FIELDS. Same claim as the type-arg step, spelled
        // structurally — `struct H { f: || -> i64 }` hides exactly what
        // `Box<dyn Fn() -> i64>` hides.
        if (t.kind() == K::Struct || t.kind() == K::ZonedStruct) {
            std::string sname(t.struct_name());
            if (sname.empty() || !seen.insert(sname).second) return false;
            auto walk = [&](lir_view::StructView sd) {
                if (!sd) return false;
                for (auto& f : sd.fields()) {
                    TypeRef ft = f.type(prog_.type_pool.impl());
                    if (hides(ft) || type_hides_borrow_(ft, seen)) return true;
                }
                return false;
            };
            auto sit = ts_.struct_by_name.find(sname);
            if (sit != ts_.struct_by_name.end() && walk(sit->second)) return true;
            auto pit = ts_.spec_by_name.find(sname);
            if (pit != ts_.spec_by_name.end() && walk(pit->second)) return true;
        }
        return false;
    }
    // ── F4: THE DOCUMENTED RESIDUAL HOLE ──────────────────────────────────
    //
    // `fn leak(n: i64) -> Box<&i64> { let c = C{v:n}; return Box::new(&c.v); }`
    // STILL COMPILES, and that is a recorded decision, not an oversight.
    //
    // The gate above fires on ERASED payloads (dyn / closure / impl-Trait),
    // which is F4 as reported: the return type says nothing, so the type-driven
    // early-out never asked. A bare `&T` type-arg (`Box<&i64>`, `Option<&T>`,
    // `Result<&T,E>`) is a different animal — the type DOES name a reference,
    // and the reason those returns are not checked is that prov_of's answer for
    // them rests on `value_local_root`, which is far too coarse for the class.
    // Two attempts were built and MEASURED on the full `cmake --build` (ctest
    // links prebuilt archives and sees none of this):
    //   1. adding Ref/Slice to the erased list → `Iterator::reduce` red for
    //      EVERY SliceIter instantiation: it returns `Option<Item>` with
    //      `Item = &T` off a by-VALUE local iterator, and the value-local
    //      fallback calls that local. It is not — the pointee is the caller's
    //      slice.
    //   2. a narrower "the construction retained a DIRECT `&local`" gate →
    //      `stdlib/mem/wql::scan_of` red on all eleven arms (a `&self` autoref
    //      receiver reads as a direct borrow), then, once receivers were
    //      excluded, `PdtBuf::select_fw_op` / `select_bw_op` /
    //      `decide_join_step` in stdlib/mem/pkd.
    // Each attempt traded a real leak for a wave of refusals of correct code.
    // The brief's constraint (a) settles it: over-refusing the ecosystem is
    // worse than a documented residual hole. Closing this one properly needs a
    // PRECISE answer to "does this reference point into call-local storage?",
    // i.e. value_local_root replaced by real region provenance — a separate
    // arc, not a gate tweak.
    //
    // D1 round 5 / H7 — THE RESIDUAL IS A CLASS, NOT A NAMED SET. It used to
    // read "the leak is `Box<&local>` and its Option/Result siblings", which
    // named three types and invited the reader to believe the rest were
    // closed. MEASURED, all admitting on 285227fe:
    //     fn f() -> (&i64, i64) { let z = …; return (&z, 0); }   // TUPLE
    //     fn f() -> [&i64; 1]   { …; return [&z]; }              // ARRAY
    //     struct S { p: &i64 }  fn f() -> S { …; return S{p:&z}; }// STRUCT
    // alongside the two named siblings, while the BARE `fn f() -> &i64 {
    // return &z; }` refuses. They are one class: a COMPOUND whose
    // borrow-carrying is INFERRED from a type-arg / element / field position
    // rather than DECLARED. The declared form is already closed — the SAME
    // struct marked `#[borrow_carrying]` refuses (measured) — and that is the
    // boundary the residual names. Named probes for the class:
    // h7_tuple_local_leak, h7_array_local_leak, h7_struct_local_leak,
    // h7_box_twin, h7_option_twin; the refusing side is h7_bare_ref_twin and
    // h7_bcstruct_local_leak; the admit control is h7_tuple_param_admit (a
    // tuple of a borrow of a PARAM, which must stay admitted).
    //
    // THE BOUNDARY IS NOW PINNED, and only the boundary — the leaks themselves
    // are NOT registered, because an `.expected` asserting that unsound code
    // compiles records the defect as intended. The two registered fixtures are
    // the two edges of the line the residual runs along:
    //   fail/bc_d1r5_h7_declared_bc_struct_held — the DECLARED
    //     `#[borrow_carrying]` struct return refuses. The residual is exactly
    //     the INFERRED side of this; a red here means the line has moved.
    //   pass/bc_d1r5_h7_param_root_admits — the same type SHAPE as the tuple
    //     leak with the reference rooted at a PARAM. It must compile and run:
    //     it is precisely what both measured close-attempts broke, so a red
    //     here is not progress on the residual but the over-refusal the
    //     residual was accepted to avoid.
    // The erased-wrapper half (fail/bc_d1r3_f4_closure_local) is closed and
    // pinned.
    // What the returned expression RETAINS. Wider than door E's by-value-bc
    // test at the construction site, because an erased wrapper can retain a
    // plain `&T` or a closure just as well as a bc value.
    bool retains_borrowing_operand(lir_view::ExprRef e) const {
        using namespace lir_view;
        using Code = lir_schema::expr::Code;
        if (!e) return false;
        const auto* pool = prog_.type_pool.impl();
        if (!type_retains_values(e.type(pool))) return false;
        bool found = false;
        auto one = [&](ExprRef a) {
            if (!a || found) return;
            TypeRef at = a.type(pool);
            if (is_ref_kind(at) || loan_carrying_type(at) ||
                is_borrow_carrying_type(at)) { found = true; return; }
            if (a.kind() == Code::ClosureBox || retains_borrowing_operand(a))
                found = true;
        };
        switch (e.kind()) {
            case Code::Call:       ECallView{e}.each_arg(one); break;
            case Code::MethodCall: {
                EMethodCallView v{e};
                one(v.receiver());
                v.each_arg(one);
                break;
            }
            case Code::StructLit:   EStructLitView{e}.each_field_value(one); break;
            case Code::TupleLit:    ETupleLitView{e}.each_elem(one); break;
            case Code::ArrLit:      EArrLitView{e}.each_elem(one); break;
            case Code::EnumLitData: EEnumLitDataView{e}.each_payload(one); break;
            case Code::Cast:        one(ECastView{e}.operand()); break;
            case Code::ClosureBox:  return true;
            default: break;
        }
        return found;
    }

    // Provenance THROUGH an erased wrapper: prov_of's own arms are gated on
    // the RESULT being ref/bc, which is precisely what an erased wrapper is
    // not, so they return {}. Walk the retained operands instead.
    RefProv prov_of_retained(lir_view::ExprRef e) const {
        using namespace lir_view;
        using Code = lir_schema::expr::Code;
        RefProv merged = {};
        if (!e) return merged;
        const auto* pool = prog_.type_pool.impl();
        auto one = [&](ExprRef a) {
            if (!a) return;
            TypeRef at = a.type(pool);
            if (is_ref_kind(at) || loan_carrying_type(at) ||
                is_borrow_carrying_type(at)) {
                merged = merge_prov(merged, prov_of(a));
                return;
            }
            merged = merge_prov(merged, prov_of_retained(a));
        };
        switch (e.kind()) {
            // A CALL's retained operands are exactly the ones its borrow-flow
            // summary says reach the result. MEASURED: without this, the
            // metaprog-emitted `fn ss_grp(a: &[SS]) -> Result<Vec<(str,i64)>,
            // ElError> { let __pl = …; return ss_grp_run(&__pl, a); }` was
            // refused because the plain `&__pl` argument — a borrow of a local
            // that the result never touches — looked like retained provenance.
            // No summary ⇒ every borrowing operand, the pre-F4 conservatism.
            case Code::Call: {
                ECallView cv{e};
                const FlowSummary* fs = flow_of_call(cv.callee());
                if (fs) {
                    size_t i = 0;
                    cv.each_arg([&](ExprRef a) {
                        if (a && i < fs->nparams && (fs->to_result & (1ull << i)))
                            one(a);
                        ++i;
                    });
                } else {
                    cv.each_arg(one);
                }
                break;
            }
            // G1: the fn-pointer twin of the arm above. A resolved pointer uses
            // the real summary; an unresolvable one takes `each_arg(one)` —
            // exactly what a summary-less direct Call takes.
            case Code::FnPtrCall: {
                EFnPtrCallView fv{e};
                const FlowSummary* fs = flow_of_fnptr(fv.callee());
                if (fs) {
                    size_t i = 0;
                    fv.each_arg([&](ExprRef a) {
                        if (a && i < fs->nparams && (fs->to_result & (1ull << i)))
                            one(a);
                        ++i;
                    });
                } else {
                    fv.each_arg(one);
                }
                break;
            }
            case Code::MethodCall: {
                EMethodCallView v{e};
                if (const FlowSummary* fs = flow_of_method(v)) {
                    std::vector<ExprRef> ops;
                    ops.push_back(v.receiver());
                    v.each_arg([&](ExprRef a){ ops.push_back(a); });
                    for (size_t i = 0; i < ops.size() && i < fs->nparams; ++i)
                        if (fs->to_result & (1ull << i)) one(ops[i]);
                } else {
                    one(v.receiver());
                    v.each_arg(one);
                }
                break;
            }
            case Code::StructLit:   EStructLitView{e}.each_field_value(one); break;
            case Code::TupleLit:    ETupleLitView{e}.each_elem(one); break;
            case Code::ArrLit:      EArrLitView{e}.each_elem(one); break;
            case Code::EnumLitData: EEnumLitDataView{e}.each_payload(one); break;
            case Code::Cast:        one(ECastView{e}.operand()); break;
            case Code::ClosureBox: {
                // A closure that CAPTURES a borrow of a local carries it out.
                // PROBE capescape: a capture of a plain OWNED local
                // contributes no provenance, so the escape gate sees nothing.
                // MEASURED 2026-08-27: 5 fires, CEILING 4 vs COST 3 —
                // borrowck-escaping-closure-error-2,
                // borrow-immutable-upvar-mutation-impl-trait,
                // suggest-lt-on-ty-alias-w-generics, regions-proc-bound-capture.
                // Its 4 rows are DISJOINT from all five other closure probes:
                // the escape channel is a separate C mechanism, confirmed. The
                // 3 costs are the predicted is_move exemption — a returned
                // `move` closure OWNS its captures and this form does not
                // consult cb.is_move(). Ceiling barely exceeds cost on a
                // near-dead site; not the round to fund.
                // PROBE capescmove: capescape was DECLINED at CEILING 4 vs
                // COST 3, and its three costs are ONE predicted shape — a
                // RETURNED `move` CLOSURE OWNS ITS CAPTURES, so a move-capture
                // of a plain owned local carries nothing out. That exemption
                // stated as a precondition. It also DROPS one of capescape's
                // rows on purpose (borrow-immutable-upvar-mutation-impl-trait
                // is `Box::new(move || x += 1)`, whose upstream reason is the
                // Fn/FnMut kind, not escape) — rule 7: a crude probe and a
                // correct rule do not close the same programs.
                // ⚠ RULE 4: this arm is reached TEN times in 8060 runs. A
                // number read here bounds a near-dead population.
                // MEASURED 2026-08-28, 371-row population: 5 fires,
                // CEILING 2 vs COST 0 — ✓, where capescape is ⛔ 4 vs 3:
                //   borrowck/borrowck-escaping-closure-error-2
                //   borrowck/suggest-lt-on-ty-alias-w-generics
                // Predicted those two plus regions/regions-proc-bound-capture;
                // the third did NOT close — it is a `move` closure too, so the
                // precondition excludes it, and it had been predicted for the
                // wrong reason. ⚠ FIVE FIRES: rule 4 governs anything read
                // here, and 2 is not a number to fund a round on.
                bool escmove = logos::probe::on("capescmove");
                bool force_local = logos::probe::on("capescape") ||
                                   (escmove && !EClosureBoxView{e}.is_move());
                EClosureBoxView{e}.each_capture_name([&](std::string_view cap) {
                    std::string n(cap);
                    if (auto it = prov_.find(n); it != prov_.end())
                        merged = merge_prov(merged, it->second);
                    else if (force_local)
                        merged.is_local = true;   // capture of a plain local
                });
                break;
            }
            default: break;
        }
        return merged;
    }

    void check_return_value(lir_view::ExprRef er, uint32_t line) {
        if (!ret_type_) return;
        bool typed_gate = is_ref_kind(ret_type_) || is_borrow_carrying_type(ret_type_);
        bool retention_gate = !typed_gate && type_hides_borrow(ret_type_) &&
                              retains_borrowing_operand(er);
        // ── #86: the gate asked "is this a REFERENCE", not "does this VALUE
        // HOLD a borrow". `struct W { v: str }` is neither ref-kind nor
        // `#[borrow_carrying]` nor a `hides()` kind, so `-> W`, `-> (str,i64)`
        // and `-> Option<str>` never opened the gate at all. #71 already built
        // the predicate that answers the right question (holds_any_ref, read
        // by type_may_carry_borrow).
        // The residency exemption (`Held<T>`/`HeldAny`: the value carries an
        // Rc/Arc share that keeps the arena ALIVE) is an ESCAPE answer, and
        // this is an escape gate — so it wins here exactly as it wins in
        // is_borrow_carrying_type. MEASURED: without this,
        // examples/writ_container_showcase.logos:91
        // `return hold_any(&mut h, e);` — the hatch's whole purpose — reds.
        bool holds_gate = !typed_gate && !retention_gate &&
                          !residency_exemption_holds(ret_type_, er) &&
                          type_may_carry_borrow_erased(ret_type_);
        if (std::getenv("LOGOS_DUMP_RETGATE")) {
            RefProv p0 = prov_of(er);
            std::vector<std::string> srcs0;
            collect_ref_sources(er, srcs0);
            std::string j; for (auto& s : srcs0) { j += s; j += ","; }
            fprintf(stderr,
                "[retgate] fn=%s line=%u retkind=%d typed=%d retention=%d "
                "mcb=%d prov{loc=%d tmp=%d np=%zu} srcs=[%s]\n",
                fn_name_.c_str(), line, (int)ret_type_.kind(), (int)typed_gate,
                (int)retention_gate, (int)type_may_carry_borrow(ret_type_),
                (int)p0.is_local, (int)p0.is_temp, p0.params.size(), j.c_str());
        }
        if (holds_gate && std::getenv("LOGOS_86_TRACE"))
            fprintf(stderr, "[#86trace-gate] fn=%s line=%u\n", fn_name_.c_str(), line);
        if (!typed_gate && !retention_gate && !holds_gate) return;

        RefProv prov = prov_of(er);
        if ((retention_gate || holds_gate) &&
            !prov.is_local && !prov.is_temp && prov.params.empty())
            prov = prov_of_retained(er);   // F4 / #86
        // 1. Definitely local / temporary → always dangling.
        if (prov.is_local || prov.is_temp) {
            std::string src;
            bool is_temp = prov.is_temp;
            if (er) {
                using Code = lir_schema::expr::Code;
                if (er.kind() == Code::AddrOf)
                    src = std::string(lir_view::EAddrOfView{er}.var_name());
                else if (er.kind() == Code::VarRef)
                    src = std::string(lir_view::EVarRefView{er}.name());
                else if (er.kind() == Code::AddrOfTemp)
                    is_temp = true;
                // #77: the returned expression can now be a CALL whose result
                // the callee's summary ties to a local argument — none of the
                // three node kinds above, so the message read "local variable
                // '?'". The §B6 collector already answers exactly "which
                // locals does this expression borrow"; ask it rather than
                // print a placeholder. Falls back to '?' when it, too, has no
                // name (the prov came from a value-local root, not a source).
                if (src.empty() && !is_temp) {
                    std::vector<std::string> srcs;
                    collect_ref_sources(er, srcs);
                    for (auto& n : srcs)
                        if (!is_return_temp_name(n) &&
                            !is_materialized_temp_name(n)) { src = n; break; }
                    if (src.empty() && !srcs.empty()) src = srcs.front();
                }
                // ── H4-e / RULE 7: A LANDING OWES THE NAME ─────────────────
                // MESSAGE ONLY. `prov_of`'s ClosureCall arm now ties a
                // capture-less closure's result to its sole reference
                // argument, so `return c(&l);` is refused — and printed
                // "local variable '?'" under the probe, because §B6's
                // `collect_ref_sources` has no ClosureCall arm and answers
                // nothing for the whole expression. Ask it about the
                // ARGUMENTS instead, which is where the tie was made.
                // ⚠ NOT A REPAIR OF §B6 ITSELF. `capargtie` armed that idea
                // inside `collect_ref_sources_paths` and priced CEILING 0 /
                // COST 2 — two legal programs — because that walk feeds
                // verdicts other than this one. Here nothing but the string
                // is read, so the two costs cannot be re-incurred.
                if (src.empty() && !is_temp && er &&
                    er.kind() == lir_schema::expr::Code::ClosureCall) {
                    lir_view::EClosureCallView{er}.each_arg(
                        [&](lir_view::ExprRef a) {
                            if (!src.empty() || !a) return;
                            std::vector<std::string> asrcs;
                            collect_ref_sources(a, asrcs);
                            for (auto& n : asrcs)
                                if (!is_return_temp_name(n) &&
                                    !is_materialized_temp_name(n)) {
                                    src = n; break;
                                }
                        });
                }
                // ── RULE 7: THE THIRD ROW OWED A NAME (PROBES.md §tmcbsite) ─
                // MESSAGE ONLY, same shape as the ClosureCall arm above.
                // `return erase(h);` dangles on what `h` holds, but §B6's Call
                // arm is gated on `type_may_carry_borrow`, which does not know
                // an ERASED result carries a borrow — so it answered nothing
                // and this printed '?'. The widening that WOULD answer it is
                // the one that over-refuses (site 3951 / adv7894), so the name
                // is recovered here, where only the string is read.
                if (src.empty() && !is_temp && er &&
                    er.kind() == lir_schema::expr::Code::Call) {
                    lir_view::ECallView{er}.each_arg(
                        [&](lir_view::ExprRef a) {
                            if (!src.empty() || !a) return;
                            std::vector<std::string> asrcs;
                            collect_ref_sources(a, asrcs);
                            for (auto& n : asrcs)
                                if (!is_return_temp_name(n) &&
                                    !is_materialized_temp_name(n)) {
                                    src = n; break;
                                }
                            if (src.empty() &&
                                a.kind() == lir_schema::expr::Code::VarRef) {
                                std::string an(lir_view::EVarRefView{a}.name());
                                if (!is_return_temp_name(an) &&
                                    !is_materialized_temp_name(an)) src = an;
                            }
                        });
                }
                // ── #77 round 2 / THE DIAGNOSTIC LEAKED A COMPILER TEMP ────
                //
                // MEASURED, and it is the FAT arm of the very repair round 1
                // landed: `pub fn bad() -> str { let o = String::from("hello");
                // return thru2(o.as_str()); }` printed "cannot return
                // reference to local variable '__ret_tmp_0'" while the thin
                // twin (`let t = 9i64; return keepr(&t);`) correctly named
                // `t`. Nothing about the borrow differs — what differs is that
                // `o` NEEDS A DROP, so sema rewrites the return through
                // `make_return_with_drops`: `let __ret_tmp_0 = thru2(...);
                // <drops>; return __ret_tmp_0;`. The returned expression is
                // then a plain VarRef and the branch above reads its name
                // straight out, so every borrow-returning function with a
                // droppable local reports a name that exists in no source
                // file.
                //
                // `ref_sources_under` is the §B6 answer for exactly this
                // question — "which locals does this PLACE borrow from" — and
                // the temp is a place like any other, so the fix is to ask it
                // rather than to special-case the message. Falls back to the
                // temp's own name only if the graph has nothing, which keeps
                // this strictly an improvement on the string.
                if (is_return_temp_name(src)) {
                    for (auto& n : ref_sources_under(src))
                        if (!is_return_temp_name(n.name) &&
                            !is_materialized_temp_name(n.name)) { src = n.name; break; }
                }
                // #86 MISS-E: §B6 has no arm for a chained call, and none for
                // a Ref built out of an Rc-held arena — both measured, both
                // pinned on the leaked string. The hop roots recorded at the
                // temp's own `let` do have the name. Message only.
                if (is_return_temp_name(src)) {
                    auto it = ret_temp_roots_.find(src);
                    if (it != ret_temp_roots_.end())
                        for (auto& n : it->second)
                            if (!n.empty() && !is_return_temp_name(n) &&
                                !is_materialized_temp_name(n)) { src = n; break; }
                }
                // ⚠ '?' IS NOT A MESSAGE, AND ITS `.expected` WAS WRITTEN TO
                // ACCOMMODATE THAT. D-c's array-literal spelling (`return
                // &[x];`) reaches here with `prov.is_local` set by
                // `value_local_root`'s TEMP-ROOT arm — a root that has no
                // NAME, because a temporary has none — and printed "local
                // variable '?'". Its fixture was pinned on the prefix
                // `cannot return reference to`, which matches all THREE
                // messages this site can print, the two wrong ones included.
                // Ask the same walk what it actually found: a temp root is a
                // TEMPORARY, and that is the message the `&0i64` sibling
                // already prints.
                // MESSAGE ONLY — `prov` is untouched, so the `let`
                // rvalue-EXTENSION rule (`let r: &[i64] = &[x];`, which Rust
                // extends and which reports off `is_temp` alone) keeps
                // admitting. That is precisely why the verdict could not
                // carry this distinction and the report site must.
                // ⚠ A **LITERAL** TERMINAL ONLY — MEASURED. `is_temporary_
                // value_expr` also answers YES for a Call / MethodCall /
                // ClosureBox, and taking those would have re-messaged two
                // PINNED fail fixtures whose referent really is a named local:
                // fail/bc_d1r3_f4_closure_local (`Box::new(move || *b)` over
                // `&x`) and fail/bc_d1r4_n3_closure_struct_field_held. Both
                // still refuse — only the STRING moved — which is exactly the
                // kind of drift a prefix-only `.expected` cannot see, and the
                // reason this one is now pinned in full. A retained borrow
                // whose name this site cannot recover keeps its old message.
                // `StructLit`/`TupleLit` are out for the same reason, measured
                // on fail/bc_d1r4_n3_closure_struct_field_held: the returned
                // `H { f: move || *b.p }` IS a struct literal, but what it
                // dangles on is the local `c`, not the literal. The shape this
                // repair is for is the ARRAY temporary reached through the
                // slice family — `return &[x];` — and the aggregate spellings
                // that really are temporaries arrive as `AddrOfTemp` and are
                // answered before this block runs.
                if (src.empty() && !is_temp) {
                    bool temp_root_msg = false;
                    lir_view::ExprRef term;
                    const auto* pl = prog_.type_pool.impl();
                    bool lit_term = false;
                    if (value_local_root(er, pl, &temp_root_msg, &term).empty() &&
                        temp_root_msg && term) {
                        switch (term.kind()) {
                            case Code::ArrLit:   case Code::LitInt:
                            case Code::LitFloat: case Code::LitBool:
                                lit_term = true; break;
                            default: break;
                        }
                    }
                    if (lit_term) is_temp = true;
                }
            }
            // ⚠ MISSING LANGUAGE FEATURE, MEASURED, NOT A DEFECT OF THIS CHECK
            // (#69 class B — rvalue static promotion). Rust promotes `&<const
            // literal>` to `'static`; Logos treats the literal's temp as an
            // ordinary local, so all three of
            //     fn foo() -> &i64 { let zero: &i64 = &0i64; return zero; }
            //     fn foo() -> &i64 { return &0i64; }
            //     fn foo() -> &'static i64 { let z: &'static i64 = &0i64; … }
            // are refused here — the annotated spelling included, so there is
            // today no way to write the shape at all.
            // ⚠ #92 IS REFUTED AS WRITTEN, AND THE LIST ABOVE IS WHY. It
            // enumerates BY SPELLING — `&0i64` under three ANNOTATIONS — and
            // the property is "return a borrow of a temporary", whose FOUR
            // spellings are `&<scalar lit>`, `&<tuple lit>`, `&<struct lit>`
            // and `&<ARRAY lit>`. The first three were refused; the fourth,
            // `fn foo() -> &[i64] { return &[0i64]; }`, COMPILED, LINKED and
            // returned garbage (measured 81 where 1 is correct, and 55 where 3
            // is correct for the NAMED-local twin `let a: [i64;2] = [x,x];
            // return &a;`). It did not promote — it dangled. #92's stated
            // repair site is wrong for it too: `&[0i64]` never reaches an
            // `AddrOfTemp`, it is a `SliceLit`/`SlicePtr`, which is why the
            // fix is two `case` labels in `prov_of` and not a change here.
            // #92 should be re-filed as TWO things: a promotion feature for
            // the three, and the dangling defect for the fourth — the latter
            // CLOSED, pinned by fail/bc_slice_return_temp_array_fail +
            // fail/bc_slice_return_local_array_fail with their admit twin
            // pass/bc_slice_return_param_admit.
            // Imported witness:
            // tests/imported/pass/regions/regions-bot (its sibling
            // regions/regions-bot-b147 is green and does not need promotion).
            // The repair belongs at the AddrOfTemp RECORDING path — a temp whose
            // initializer is a constant expression must be marked promoted so it
            // never enters the dangling channel — and is a language feature with
            // its own design surface, not a patch to this report site.
            // MESSAGE ONLY, and it is not cosmetic: `K` is a const ITEM, so
            // "local variable 'K'" sends the reader looking for a `let` that
            // is not in the function. Name what it is and name the fix — the
            // admit twin `static` is a one-word edit away.
            // CAUSE B, AND THIS IS THE ONLY SITE THAT READS IT. A non-move
            // closure's capture lives in the enclosing frame and outlives the
            // closure, so returning a reference to it out of the closure body
            // is not dangling. Read HERE and nowhere else — the escape /
            // dangling channel keeps its verdict, which is the whole
            // difference between this and `capretcaps`.
            if (!is_temp && !src.empty() && closure_capture_names_.count(src))
                return;
            if (!is_temp && !src.empty() && ts_.frame_consts.count(src) &&
                !var_has(NO_SLOT, src))
                report(line, std::format(
                    "cannot return reference to const item '{}': a const is "
                    "materialised into the frame at each use and has no static "
                    "storage (declare it `static` instead): dangling reference",
                    src));
            else if (is_temp)
                report(line,
                    "cannot return reference to temporary value: dangling reference");
            else
                report(line, std::format(
                    "cannot return reference to local variable '{}': dangling reference",
                    src.empty() ? "?" : src));
            return;
        }

        // 2. Explicit lifetime on return type — check sources match.
        // A MINTED return region is not a WRITTEN one: this check is about the
        // contract the user spelled ("return reference must derive from 'x'"),
        // so a minted name reads as elided here. outlives.hpp::lt_is_minted.
        std::string ret_lt(TypeRef(ret_type_).lifetime());
        if (lt_is_minted(ret_lt)) ret_lt.clear();
        if (!ret_lt.empty() && ret_lt != "'_") {
            // L4: When provenance traces to a param that's NOT a ref
            // (e.g. a struct param `x: Foo<'a, 'b>` whose field is
            // returned: `x.y` with `y: &'b u8`), `prov.params` is
            // empty because the param-source is the struct itself,
            // not a ref. The type checker has already verified the
            // returned expression's type matches `&'b u8` against
            // the declared return — trust it. We only error here on
            // detectable mismatches: prov.params non-empty AND each
            // traced param's declared lifetime ≠ ret_lt.
            if (prov.params.empty()) return;
            for (auto& src : prov.params) {
                auto it = param_lifetimes_.find(src);
                // L4: param is NOT a ref (e.g. struct holding refs that
                // we returned a field of) — not in param_lifetimes_ at
                // all. Trust the type checker; it has already verified
                // the FieldRead's lifetime matches the return type.
                if (it == param_lifetimes_.end()) continue;
                const std::string& src_lt = it->second;
                // B66: equality OR src lt outlives ret lt (an explicit
                // `where 'src: 'ret` covers the case, as does 'static src
                // for any named ret).
                // MEASURED 2026-08-28, 379-row ledger: 22 fires, CEILING 0,
                // COST 0. NEGATIVE RESULT, recorded with its population so it
                // is not re-proposed. Predicted 4 rows BY NAME (regions-assoc-
                // type-in-supertrait-outlives-container, regions-outlives-
                // projection-container, -wc, regions-normalize-in-where-
                // clause-list) — the only ledger programs in the block that
                // spell an `'a: 'b` AND produce a nonzero Outlives constraint.
                // None closed. ⚠ RULE 4: 22 arrivals over 379 compiles is a
                // TINY population, so this kills the SPELLING, not the nesting
                // rule: `param_inner_lifetimes_` is populated from the PARAM's
                // declared type, and the obligation Rust wants is on the
                // RETURNED EXPRESSION's type. A correct nesting rule needs the
                // latter and would not read this site at all.
                // PROBE lifereg_retinner: the outer param lifetime matching
                // ret_lt ends the check, so an INNER lifetime carried by the
                // same param (`&'a W<'b>` returning a `&'b` projection) is
                // never asked to outlive ret_lt. `param_inner_lifetimes_`
                // already holds the answer and is consulted only on the
                // src_lt-EMPTY branch below. CRUDE: require every inner
                // lifetime to equal ret_lt or outlive it; the correct rule
                // needs the RETURNED expression's own type, not the param's.
                if (src_lt == ret_lt) {
                    if (!logos::probe::on("lifereg_retinner")) continue;
                    auto pin = param_inner_lifetimes_.find(src);
                    if (pin == param_inner_lifetimes_.end()) continue;
                    bool inner_ok = true;
                    for (auto& ilt : pin->second) {
                        if (ilt.empty() || ilt == ret_lt) continue;
                        bool o = ri_ ? ri_->outlives_named(ilt, ret_lt)
                                     : outlives(ilt, ret_lt, outlives_adj_,
                                                /*permissive_empty=*/false);
                        if (!o) { inner_ok = false; break; }
                    }
                    if (inner_ok) continue;
                }
                // Strict mode (no permissive-empty) — an elided source
                // lifetime cannot silently match a declared return lifetime.
                // logos-core 2.1 (consumer): prefer region_infer's
                // named-region BFS over the local string-graph BFS when
                // available — both consume `fn.lifetime_outlives` as the
                // source, so the two paths agree on every input, but the
                // region_infer view is the canonical one downstream
                // (HRTB, dropck). Falls back to the string graph when
                // ri_ is null (exclusivity-only mode).
                bool ok = ri_ ? ri_->outlives_named(src_lt, ret_lt)
                              : outlives(src_lt, ret_lt, outlives_adj_,
                                         /*permissive_empty=*/false);
                if (ok) continue;
                // B86: if outer ref lt is elided AND the param points to an
                // aggregate, defer to type-checker. The lt structure of
                // fields is verified there; we'd otherwise reject valid
                // `self: &Self` -> &'a T where Self<'a> shapes (Self type
                // resolution doesn't carry impl-level lt_args forward yet).
                if (src_lt.empty()) {
                    auto inner = param_inner_lifetimes_.find(src);
                    if (inner != param_inner_lifetimes_.end()) {
                        // MEASURED 2026-08-27: 4 fires, CEILING 3 vs COST 0
                        // (ex1-return-one-existing-name-self-is-anon and its
                        // --c14b sibling, mir-check-cast-unsize). A near-dead
                        // site whose ceiling is nearly its whole arrival
                        // count. ⚠ COST 0 IS NOT A SAFETY CLAIM: the hatch's
                        // stated justification (`self: &Self` -> &'a T where
                        // Self<'a>, impl-level lt_args not carried forward)
                        // names a REAL missing capability that this corpus
                        // simply does not contain.
                        // PROBE lifereg_aggtrust: a struct with NO lifetime
                        // args carries no region and can justify nothing — the
                        // hatch is checked only in the permissive direction.
                        bool found = logos::probe::on("lifereg_aggtrust")
                                         ? false
                                         : inner->second.empty();
                        for (auto& ilt : inner->second)
                            if (ilt == ret_lt) { found = true; break; }
                        if (found) continue;
                    }
                }
                report(line, std::format(
                    "lifetime mismatch: return type has lifetime {} "
                    "but '{}' has lifetime {}",
                    ret_lt, src, src_lt.empty() ? "(elided)" : src_lt));
            }
            return;
        }

        // 3. Elided / '_ return lifetime — apply Rust elision rules.
        //    Rule: if exactly one ref-typed parameter (with any lifetime) exists,
        //    the return must derive from that parameter.
        //    With multiple ref params: ambiguous — only check non-local provenance.
        if (!prov.params.empty()) {
            // Has param source(s). Apply elision rule if applicable.
            if (param_lifetimes_.size() == 1) {
                const std::string& sole_param = param_lifetimes_.begin()->first;
                if (!prov.params.count(sole_param)) {
                    report(line, std::format(
                        "lifetime elision: return reference must derive from '{}' "
                        "(the only reference parameter)", sole_param));
                }
            }
            // else: multiple ref params — can't disambiguate, allow any param source.
            return;
        }
        // prov.params empty && !is_local: call result, global, or untracked.
        // If we have ref params but return doesn't trace to any: suspicious,
        // but only report when we can clearly see an EVarRef or EAddrOf with no match.
        if (!param_lifetimes_.empty()) {
            // There are ref params but we can't trace the return to any of them.
            // This can happen for complex expressions — be conservative, don't error.
        }
        // Empty params and no local: unknown provenance (e.g. function return) — safe.
    }

    // ── Expression visitor ─────────────────────────────────────────────────

    void visit(lir_view::ExprRef e, bool consuming, uint32_t line);

    // Take scoped borrows for all EAddrOf nodes reachable through a ref
    // expression.  Handles the case where the ref is formed conditionally:
    //   let r = if c { &mut x } else { &mut y };   ← both x and y must be
    //   let r = match tag { A => &x, _ => &y };      borrowed for the scope.
    // For non-borrow sub-expressions (condition of if, scrutinee of match,
    // function calls, etc.) we fall through to a regular visit().
    // record_only: RECORD loans without re-running the consuming visit. Used
    // by the capture-flow site (R7), where visit_args has ALREADY visited the
    // argument — a second consuming visit would report a spurious double move.
    // #70: record_only is a property of the WHOLE SUBTREE, not of this frame.
    // The caller that sets it has already visited `e` *and everything under
    // it*; therefore every internal recursion must FORWARD the flag and every
    // internal consuming visit must be gated on it. Dropping it on the
    // aggregate arms is what made stdlib `Option::replace` — whose body is
    // `return replace_ref(self, Option::Some(value))` — report "use of moved
    // value 'value'": visit_args consumed the payload once (the legitimate
    // move), then apply_call_outparam_rules' record_only re-walk reached the
    // EnumLitData arm, lost the flag, and the payload VarRef fell into
    // `default:` with record_only == false ⇒ a SECOND consuming visit.
    // The FnPtrCall arm below already carried this reasoning for one site
    // (measured on `Result::unwrap_or_else`); it is the same rule everywhere.
    // Gating cannot lose a check: the caller's own visit performed it.
    void take_ref_borrows(lir_view::ExprRef e, uint32_t line,
                           const std::string& holder = "",
                           bool record_only = false) {
        if (!e) return;
        using namespace lir_view;
        using Code = lir_schema::expr::Code;
        const auto* pool = prog_.type_pool.impl();

        // ── D1: provenance survives every BY-VALUE hop ────────────────────
        // If this expression PRODUCES a borrow-carrying value, then whatever
        // local bindings that value hopped out of (`ob.unwrap()`, `w.b`,
        // `s.get()`, `id(c.mk())`, a match/if joining them) may already hold
        // loans of the arena the borrow points into. Those loans are now
        // reachable through `holder`, so `holder` joins them.
        //
        // This is one rule, not a per-shape patch: the shape enumeration lives
        // entirely in bc_hop_roots, and the gate is the value's TYPE carrying
        // borrows — which routes through is_borrow_carrying_type and therefore
        // keeps honouring ts_.residency_exempt. A value whose type does NOT
        // carry borrows ties nothing (the consuming-fn control `fn eat(B) ->
        // i64` stays admitted), and inheriting from a binding that holds no
        // loan is a no-op, so the rule can only extend a REAL loan's life.
        // Gate: NOT is_borrow_carrying_type — the loan channel does not honour
        // the residency exemption (see loan_carrying_type), and
        // type_may_carry_borrow deliberately does not either (its own comment
        // at type_is_residency_exempt says so). The ERASURE case is caught one
        // level up, at the Let/Assign routing gate, because the erased type
        // only appears on the BINDING: `Box::new(c.mk())` still has type
        // `Box<B>` here.
        //
        // ⚠ TWO PREDICATES FOR ONE QUESTION, AND THE NARROW ONE WAS HERE. This
        // gate asked `loan_carrying_type`, the NAMED-CARRIER closure, which has
        // no Ref/MutRef/Slice arm — so a value whose type is a PLAIN `&T` /
        // `&[T]` never reached bc_hop_roots at all. Every OTHER site in this
        // file that asks "should I look for hop roots" asks
        // `type_may_carry_borrow` (= `is_ref_kind(t) || loan_carrying_type(t)`
        // plus the same structural recursion): the six bc_hop_roots ARGUMENT
        // positions all use the wide one while this OUTER gate stayed narrow.
        // Repaired by DELEGATION to the predicate that already owns the
        // question; `loan_carrying_type` is untouched for its other consumers.
        //
        // MEASURED, one property apart — whether the borrow passes through an
        // intermediate slice local:
        //     let v = &a[0..4]; let r = &v[0..2]; a[0] = 9;  -> was rc 0
        //     let r = &a[0..2];                   a[0] = 9;  -> rc 1, always
        // The re-slice spelling produces NO fall-through arrival in the
        // AddrOfTemp census at all: it builds no SliceLit and no AddrOfTemp,
        // take_ref_borrows sees `Call(slice_get_range, [VarRef v, lo, hi])`,
        // and the loss was here, one gate earlier than four landed fixes were
        // looking.
        if (!holder.empty() && type_may_carry_borrow(e.type(pool))) {
            std::vector<std::string> roots;
            bc_hop_roots(e, roots);
            for (auto& r : roots) inherit_loans(r, holder, line);
        }

        switch (e.kind()) {
            // H4 — the CLOSURE-CALL half of the loan channel. A SHARED
            // whole-root capture registers no borrow at the ClosureBox (see
            // the Door D block: `rel.empty() && !is_mut` takes the check_live
            // path only), so `let f = || c.mk(); vs.push(f()); c.bump();` had
            // no loan on `c` for anyone to inherit — while the inlined
            // `vs.push(c.mk())` created one through the AddrOf arm below. The
            // borrow the closure's RESULT carries is a borrow of the captures,
            // taken here with the storing place as holder. Gated on the result
            // type carrying a borrow, so calling an i64-returning closure ties
            // nothing (control: h4_closure_scalar_admit).
            case Code::ClosureCall:
            case Code::FnPtrCall: {
                TypeRef rt = e.type(pool);
                const auto* caps = closure_caps_of(call_callee(e));
                if (is_ref_kind(rt) || is_borrow_carrying_type(rt))
                    if (caps)
                        for (auto& c : *caps)
                            if (var_has(NO_SLOT, c) && !param_names_.count(c)) {
                                BorrowPlace cbp;
                                cbp.root = c;
                                cbp.root_slot = NO_SLOT;
                                record_borrow(cbp, /*is_mut=*/false, line, holder);
                            }
                // G1 — the LOAN half. A genuine fn pointer (no captures) takes
                // Code::Call's argument rule: an argument that can reach the
                // result contributes its borrows to the holder. `vs.push(g(&c))`
                // recorded NOTHING here, which is the leak. Gated the same way
                // (`res_bc`), so a scalar-returning pointer still ties nothing.
                if (!caps && e.kind() == Code::FnPtrCall) {
                    EFnPtrCallView fv{e};
                    const FlowSummary* fs = flow_of_fnptr(fv.callee());
                    bool res_bc = is_borrow_carrying_type(rt);
                    size_t i = 0;
                    fv.each_arg([&](ExprRef a) {
                        size_t ix = i++;
                        if (!a) return;
                        if (fs && ix < fs->nparams && !(fs->to_result & (1ull << ix)))
                            return;
                        if (is_ref_kind(a.type(pool)) ||
                            (res_bc && is_borrow_carrying_type(a.type(pool))))
                            // record_only: the trailing `visit(e, consuming)`
                            // below already visits every argument. Letting this
                            // one visit too reported a DOUBLE MOVE — measured on
                            // stdlib `Result::unwrap_or_else`, whose body is
                            // `return f(e);`: "use of moved value 'e'". The
                            // Code::Call arm has no trailing visit, which is why
                            // it can pass record_only through unchanged.
                            take_ref_borrows(a, line, holder, /*record_only=*/true);
                    });
                }
                if (!record_only) visit(e, /*consuming=*/true, line);
                break;
            }
            // ⚠ THE ARRAY→SLICE COERCION IS BORROW-FORMING, and this switch had
            // no arm for it: `let hs: &[B] = &holders;` lowers to
            // SliceLit{BASE_PTR=AddrOfTemp(holders)} and recorded NO loan on
            // `holders` at all. MEASURED, one-variable pair (only the annotated
            // type differs, `&[B]` vs `&[B; 1]`):
            //     let hs: &[B; 1] = &holders; … holders = holders;  -> REFUSED
            //     let hs: &[B]    = &holders; … holders = holders;  -> rc 0
            // and the loan dump shows `target=holders holder=hs` present for the
            // array spelling and absent for the slice.
            // ⚠ THIS ARM WAS WRITTEN ONCE BEFORE AND REVERTED as "changed
            // nothing". It changed nothing on THAT probe because the program
            // was refused through the reborrow-alias channel instead; the arm
            // was right and the oracle was wrong. Only BASE_PTR can carry
            // provenance — the length is a scalar.
            case Code::SliceLit: {
                bool saved_view = slice_view_base_;
                slice_view_base_ = true;
                take_ref_borrows(ESliceLitView{e}.base(), line, holder, record_only);
                slice_view_base_ = saved_view;
                break;
            }
            case Code::AddrOf: {
                EAddrOfView v{e};
                BorrowPlace abp;
                abp.root = std::string(v.var_name());
                abp.root_slot = NO_SLOT;
                record_borrow(abp, is_mut_ref(e.type(pool)), line, holder);
                break;
            }
            // B81/B83: `&o.field.chain` lowers to AddrOfTemp(FieldRead*).
            // Walk down the FieldRead chain to extract the root var and
            // dotted path; check moved_fields and take a path-aware borrow.
            case Code::AddrOfTemp: {
                EAddrOfTempView v{e};
                auto inner = v.inner();
                // Consume the view-base marker; reset immediately so the nested
                // walks below (index expressions, the reborrow/MethodCall
                // routers) do not inherit it — same discipline as
                // reborrow_force_mut_ at the MethodCall arm.
                bool view_base = slice_view_base_;
                slice_view_base_ = false;
                // Reborrow shape `AddrOfTemp(Deref(VarRef r))` where r is
                // ref-typed — register a borrow on r (NOT on what r points
                // to). NLL releases on the holder's last use, restoring r's
                // usability — this is what makes implicit-reborrow at call
                // args work: r is "frozen" only for the call's scope.
                // ⚠ is_reborrow_shape MATCHES `AddrOfTemp(Deref(VarRef))` AND
                // NOTHING ELSE — one deref exactly. `&mut **p` is the same
                // reborrow one level deeper, and it was rescued by nothing:
                // extract_borrow_place below CLEARS the path on every Deref, so
                // the walk reaches the root with an empty path and no index,
                // both of that decomposition's guards miss, and NO borrow of any
                // kind is recorded. MEASURED, one-property pair — same `p` of
                // the same type `&mut &mut i64` on both sides, same aliasing
                // crime, only the depth under the outer `&mut` differing:
                //     let m = &mut *p;  let n = &mut *p;   -> REFUSED
                //     let m = &mut **p; let n = &mut **p;  -> rc 0, and the loan
                //       dump holds NO record whose holder is m or n.
                // Two simultaneously live `&mut i64` onto one storage; E0499.
                //
                // ⚠ THE FIRST FIX WAS AT THE WRONG SITE AND THE STDLIB SAID SO.
                // Recording a whole-root borrow whenever the path came back
                // empty also fires for a plain `AddrOfTemp(VarRef)` — every
                // method autoref — so `it.next()` in a loop conflicted with
                // itself and liblogos-lang stopped building (iter_min, iter_max).
                // The property is not "empty path", it is "the place is reached
                // through derefs alone". Peel here, and the existing arm keeps
                // its fake_param bypass, which the fallthrough did not have and
                // which is why that attempt also produced the WRONG diagnostic
                // ("not declared as mut") on the very program it fixed.
                //
                // is_reborrow_shape itself is left alone deliberately:
                // mlir_gen_dyn also consumes it, and widening a shared
                // RECOGNISER to serve one CONSUMER is how the narrow/wide pairs
                // in this file were born in the first place.
                ExprRef reborrow_root;
                // â  A REBORROW THROUGH A PROJECTION IS STILL A REBORROW. The
                // peel terminated at a bare VarRef and nothing else, so
                // `&mut *t.0` / `&mut *h.r` / `&mut *arr[0]` â the shapes
                // try_implicit_reborrow_mut PRODUCES for a ref-typed element â
                // fell to the decomposition below, which records them as a
                // FRESH borrow of the root local and then demands the root be
                // declared `mut`. It is not the root that is mutated.
                ExprRef reborrow_place;
                if (inner) {
                    ExprRef cur = inner;
                    while (cur && cur.kind() == Code::Deref)
                        cur = EDerefView{cur}.operand();
                    if (cur && cur != inner && lir_view::is_place_expr(cur)) {
                        if (cur.kind() == Code::VarRef) reborrow_root = cur;
                        else if (is_ref_kind(cur.type(pool))) reborrow_place = cur;
                    }
                }
                if (ExprRef inner_var = reborrow_root;
                    inner_var && is_ref_kind(inner_var.type(pool))) {
                    std::string rname(EVarRefView{inner_var}.name());
                    uint32_t rname_slot = EVarRefView{inner_var}.var_slot();  // Phase-1
                    if (auto sit = var_find(rname_slot, rname); sit != nullptr) {
                        // Route through take_borrow so two-phase reservation
                        // (B82) and prefix-aware diagnostics kick in. Bypass
                        // the is_mut_binding check (reborrow draws from r's
                        // borrow capacity, not its binding mutness; the
                        // `&mut`-ness comes from r's type, which sema has
                        // already verified) via a temporary param_names_
                        // insertion.
                        (void)sit;
                        BorrowPlace rbp;
                        rbp.root = rname;
                        rbp.root_slot = rname_slot;
                        rbp.root_type = inner_var.type(pool);
                        // `&mut *r` / `&mut **r`: the reference crossed IS r.
                        rbp.through_ref_type = inner_var.type(pool);
                        record_borrow(rbp, v.is_mut(), line, holder,
                                      {/*skip_mut_binding=*/false,
                                       /*ref_capacity=*/true});
                        break;
                    }
                }
                // Reborrow of a METHOD RESULT: `&*(c.get_ref())`, and crucially
                // `&v[i]` / `&mut v[i]` which lower to `&*(Vec::index(&v, i))`. The
                // place decomposition below can't root through the MethodCall, so
                // route to it — Front (a)'s `result_borrows_self` then records a
                // borrow of the method's receiver (closing collection iterator-
                // invalidation: `let r=&v[i]; v.push(); use r`).
                if (inner && inner.kind() == Code::Deref) {
                    if (auto op = EDerefView{inner}.operand();
                        op && op.kind() == Code::MethodCall) {
                        // `&mut v[i]` (= AddrOfTemp(Deref(index_mut(...)))): the
                        // outer `&mut` makes the receiver borrow MUTABLE even
                        // when method_self_kind can't resolve the desugared
                        // index_mut. Without this the receiver records SHARED
                        // and two live `&mut v[i]` (even same index) alias
                        // undetected (rustc E0499).
                        bool saved = reborrow_force_mut_;
                        reborrow_force_mut_ = v.is_mut();
                        take_ref_borrows(op, line, holder, record_only);  // #70
                        reborrow_force_mut_ = saved;
                        break;
                    }
                }
                // Structural decomposition of the borrowed PLACE — single
                // foundation shared with the check pass (`visit::AddrOfTemp`).
                // `index_in_chain` flips on if any `[i]` was crossed; the path
                // is the field-chain to the indexed container (whole-element
                // borrow rule). See `extract_borrow_place`.
                BorrowPlace bp = extract_borrow_place(inner, pool);
                std::string root = bp.root;
                std::string path = bp.path;
                bool index_in_chain = bp.index_in_chain;
                if (!root.empty()) {
                    auto sit = var_find(NO_SLOT, root);
                    // The EXEMPTION half of the reborrow rule, and the same one
                    // record_borrow's `ref_capacity` flag spells for the bare
                    // VarRef branch above: a reborrow draws on the REFERENCE's
                    // capacity, not on the binding's declared mutness. One
                    // type, declared at file scope, used by both.
                    MutBindBypass bypass;
                    if (reborrow_place && sit != nullptr &&
                        !sit->is_mut_binding && !param_names_.count(root)) {
                        param_names_.insert(root);
                        bypass.set = &param_names_;
                        bypass.name = root;
                    }
                    if (sit != nullptr && !path.empty()) {
                        // T1-10/B78: full dotted-path overlap (equal /
                        // either-prefix) — disjoint siblings borrow fine.
                        if (auto* hit = find_moved_overlap(
                                sit->moved_fields, path)) {
                            report(line, std::format(
                                "use of moved field '{}.{}' (moved on line {})",
                                root, hit->first, hit->second));
                            break;
                        }
                    }
                    if (index_in_chain) {
                        // Visit inner FIRST (sub-checks on the index expr etc.)
                        // BEFORE registering the borrow, else the recursive
                        // VarRef visit hits check_live on the root we just
                        // borrowed → spurious self-conflict.
                        if (inner) visit(inner, /*consuming=*/false, line);
                        record_borrow(bp, v.is_mut(), line, holder);
                        break;
                    }
                    if (!path.empty()) {
                        bool is_mut = v.is_mut();
                        // §6.1 P40: when union-root redirects a field path
                        // borrow into a WHOLE-VALUE take_borrow, visiting
                        // `inner` AFTER the take would trigger check_live
                        // on the root VarRef and see the freshly-set
                        // mut_borrowed=true as a spurious self-conflict
                        // ("cannot use 'u' while it is mutably borrowed").
                        // The non-union path takes a field-path borrow
                        // (which doesn't set the root's mut_borrowed) so
                        // it's order-insensitive. Mirror the index_in_chain
                        // shape: visit inner FIRST for both branches.
                        if (inner) visit(inner, /*consuming=*/false, line);
                        record_borrow(bp, is_mut, line, holder);
                        break;
                    }
                    // ── THE WHOLE-LOCAL VIEW BASE ────────────────────────────
                    // path is empty and no index was crossed: this is
                    // `&<whole local>`, the same borrow the AddrOf arm records
                    // one arm up — and under a SliceLit it is the coercion's
                    // own borrow, which the SliceLit arm delegated here to have
                    // recorded. MEASURED, one token apart:
                    //   let r: &i64   = &a[0u64];            -> REFUSED
                    //   let r: &[i64] = a[0u64..2u64];       -> rc 0, no loan
                    // Visit `inner` FIRST for the reason both sibling branches
                    // do: the recursive VarRef visit would otherwise hit
                    // check_live on the root just borrowed and report a
                    // spurious self-conflict.
                    if (view_base) {
                        if (inner) visit(inner, /*consuming=*/false, line);
                        record_borrow(bp, v.is_mut(), line, holder);
                        break;
                    }
                }
                if (!record_only) visit(e, /*consuming=*/true, line);  // #70
                break;
            }
            case Code::IfExpr: {
                EIfExprView v{e};
                if (!record_only) visit(v.cond(), /*consuming=*/true, line);  // #70
                // â  ARMS ARE ALTERNATIVES, AND THIS PASS DID NOT KNOW IT. The
                // CHECK pass's IfExpr arm saves the state, walks each arm from
                // the same baseline and joins with merge_loans; this one walked
                // them in sequence, so `if c { &mut a } else { &mut a }` took a
                // second mutable loan on top of the first and refused itself.
                auto saved_s = states_;
                take_ref_borrows(v.then_val(), line, holder, record_only);    // #70
                auto then_s = states_;
                states_ = saved_s;
                take_ref_borrows(v.else_val(), line, holder, record_only);    // #70
                merge_loans(states_, then_s);
                break;
            }
            // Cross-fn provenance (conservative): when a function-call result is
            // bound to a ref (`let r = f(&a, &b)`), the returned ref MAY alias
            // any of the ref-typed input arguments — without per-fn signature
            // analysis, register a borrow on each ref-arg held by `holder` (the
            // let var). NLL releases at the holder's last use; `a = …` /
            // `&mut a` while r is live is now rejected. Sound (matches Rust's
            // elision conservative upper bound); may overshoot when the callee
            // tighter-binds the return to a specific input — refining that
            // needs per-fn signature provenance (future work).
            case Code::Call: {
                ECallView v{e};
                // A `#[self_describing]` DST method call lowers to a PLAIN
                // Call with the receiver as arg0 (typed non-owning DstRef) —
                // the MethodCall elision tie below never sees it. Same rule:
                // when the callee's signature borrows its DstRef receiver and
                // returns a borrow, hold the receiver root's borrow for the
                // holder's lifetime with the formal's mutability
                // (`let v = alc.data(i); alc.resize(…)` must reject).
                bool tied_recv = false;
                if (auto it = fn_index_.by_name.find(std::string(v.callee()));
                    it != fn_index_.by_name.end() && it->second &&
                    !it->second.params().empty() && is_self_borrowing(it->second)) {
                    TypeRef p0 = it->second.params()[0].type(pool);
                    // ⚠ THE FIRST FORMAL IS `Ref`/`MutRef` HERE, NOT JUST
                    // `DstRef`, AND THAT WIDENING IS THE POINT. mono rewrites a
                    // METHOD-GENERIC call into a plain `Code::Call` with the
                    // receiver autoref'd into arg0. Until 2026-08-27 this arm
                    // tied arg0 only for a FAT (`DstRef`) receiver, so a
                    // receiver typed plain `&mut C` deposited NOTHING and the
                    // MethodCall arm — which does tie, via
                    // method_result_borrows_self — no longer saw the node,
                    // there being no MethodCall left after mono. CONSTRUCTED
                    // demonstrator, one variable, byte-identical bodies:
                    //     c.plain();      c.plain();        refused
                    //     c.get::<i64>(); c.get::<i64>();   ADMITTED
                    // pinned as fail/bc_genrecv_two_mut_conflict_fail and its
                    // two siblings.
                    //
                    // `is_self_borrowing` (the enclosing condition) is what
                    // keeps this precise rather than conservative: it is asked
                    // whether the RESULT borrows the receiver at all, so a
                    // generic method returning a scalar ties nothing, and one
                    // whose result may slice a ref PARAMETER instead of self
                    // ties nothing either. Both exemptions are RUN in
                    // pass/bc_genrecv_two_mut_sequential_admit.
                    //
                    // ⚠ MEASURED BEFORE LANDING, and the two numbers are not
                    // interchangeable. `scripts/ceiling-probe.sh genrecvtie`
                    // over the 423-row acceptance ledger: ONE fire, ceiling 0 —
                    // which is a NEAR-DEAD SITE for that population, not a
                    // refutation. `scripts/pass-probe.sh genrecvtie` over 8518
                    // pass/fail tests plus a real stdlib rebuild: 14075 fires
                    // in 700 compiles, CHANGED = 0. Hot, and free. The crude
                    // alternative at the same site (genautorefx below, no
                    // callee resolution, no self-borrowing test) was priced on
                    // the same population at CHANGED = 7, every one of them
                    // COST. That difference is the whole argument for asking
                    // the signature instead of the shape.
                    //
                    // ⚠ THE FINDING COLUMN WAS ZERO AND THE DEFECT IS REAL.
                    // All 400 rustc-rejected `tests/imported/admit` rows were
                    // in the priced population and none moved — a channel
                    // proven live, since the sabotage pole moves all 400. The
                    // corpus simply contains no program of this shape. Every
                    // demonstrator here had to be CONSTRUCTED by hand.
                    // ⚠ BOTH NUMBERS ABOVE ARE HISTORICAL AND NO LONGER
                    // RE-RUNNABLE: the `genrecvtie` probe was RETIRED at this
                    // landing (that is what "made unconditional" means), so
                    // both scripts now report NEVER FIRED for that name. They
                    // were re-run and reproduced to the digit on 2026-08-27
                    // before the gate was removed; a future round must bring a
                    // new probe rather than re-read these.
                    //
                    // ⚠ AND COST 0 WAS NOT WHY THIS LANDED. 18 legal programs
                    // were written by hand against the widened tie — sequential
                    // loans, shadowed holders, inner blocks, both if-arms, a
                    // loop body, a field-path receiver, a receiver reached
                    // through an existing `&mut`, distinct receivers, a
                    // by-value result, a discarded result, a nested call
                    // argument, free generic fns on both `&T` and `&mut T`.
                    // ZERO flipped. The one over-refusal this arm could have
                    // had is closed by SEMA, not by luck: `is_self_borrowing`
                    // returns true on a plain-ref result WITHOUT asking whether
                    // another ref parameter could be the real source, so
                    // `fn pick<T>(a: &mut i64, b: &i64) -> &i64` would be tied
                    // to `a` — and that signature does not compile, E0106
                    // missing lifetime specifier. Spell the lifetimes and
                    // `lifetime_params()` is non-empty, so is_self_borrowing
                    // declines. There is no third spelling.
                    bool p0_ref = p0 && (p0.kind() == LogosType::Kind::Ref ||
                                         p0.kind() == LogosType::Kind::MutRef);
                    if ((p0 && p0.kind() == LogosType::Kind::DstRef &&
                        !p0.owning_dst()) || p0_ref) {
                        ExprRef a0; uint64_t ai0 = 0;
                        v.each_arg([&](ExprRef a){ if (ai0++ == 0) a0 = a; });
                        // extract_borrow_place does NOT peel a top-level
                        // AddrOfTemp: without this an autoref'd arg0
                        // decomposes to an EMPTY root and record_borrow
                        // returns on its first line — a silent no-tie that
                        // reads exactly like a deliberate exemption.
                        if (a0 && a0.kind() == Code::AddrOfTemp)
                            a0 = EAddrOfTempView{a0}.inner();
                        if (a0) {
                            BorrowPlace bp = extract_borrow_place(a0, pool);
                            bool rawptr = bp.root_type &&
                                bp.root_type.kind() == LogosType::Kind::Ptr;
                            if (!bp.root.empty() && !rawptr &&
                                var_has(bp.root_slot, bp.root)) {
                                bool m = p0.mut_ptr() ||
                                    p0.kind() == LogosType::Kind::MutRef;
                                record_borrow(bp, m, line, holder,
                                              {/*skip_mut_binding=*/true});
                                tied_recv = true;
                            }
                        }
                    }
                }
                // MEASURED 2026-08-27: 27 fires — i.e. the ENTIRE Code::Call
                // population reaching take_ref_borrows across all 423 ledger
                // compiles is 27 arrivals, of which exactly 1 is resolved AND
                // self-borrowing. That reading was CORRECT AND MISLEADING, and
                // it is why the tie above is now unconditional: 27 arrivals is
                // what the ACCEPTANCE LEDGER contains, and the same site over
                // the pass corpus plus the stdlib is 14075 fires in 700 of
                // 8518 compiles. The insurance did its job — it said the zero
                // was a near-dead SITE and not a broken callee lookup — but
                // "no widening at this site can reach the hole" was a claim
                // about a POPULATION, and the hole lives in another one.
                // PROBE genarg0blind: INSURANCE AGAINST A FALSE ZERO on
                // the receiver tie and genrecvconflict. Both depend on fn_index_
                // resolving the MANGLED specialization name mono minted. This
                // fires exactly when the lookup MISSED, so the fire count says
                // how large the unresolved-callee population is.
                bool blind = logos::probe::on("genarg0blind");
                if (blind && !tied_recv && !holder.empty() &&
                    fn_index_.by_name.find(std::string(v.callee())) ==
                        fn_index_.by_name.end()) {
                    ExprRef b0; uint64_t bi = 0;
                    v.each_arg([&](ExprRef a){ if (bi++ == 0) b0 = a; });
                    if (b0 && b0.kind() == Code::AddrOfTemp) {
                        BorrowPlace bp = extract_borrow_place(
                            EAddrOfTempView{b0}.inner(), pool);
                        bool rawptr = bp.root_type &&
                            bp.root_type.kind() == LogosType::Kind::Ptr;
                        if (!bp.root.empty() && !rawptr &&
                            var_has(bp.root_slot, bp.root))
                            record_borrow(bp, /*is_mut=*/false, line, holder,
                                          {/*skip_mut_binding=*/true});
                    }
                }
                uint64_t ai = 0;
                // D1: a BY-VALUE borrow-carrying argument contributes its
                // borrows exactly like a plain-ref argument — `id(c.mk())`
                // passes the loan through. Gated on the CALL'S RESULT also
                // carrying borrows: a fn that CONSUMES the value and returns a
                // scalar (`fn eat(x: B) -> i64`) ties nothing, which is what
                // keeps the consuming control admitted.
                bool res_bc = is_borrow_carrying_type(e.type(pool));
                v.each_arg([&](ExprRef a) {
                    bool is_recv = tied_recv && ai == 0;
                    ai++;
                    if (is_recv) return;   // receiver borrow recorded above
                    if (!a) return;
                    // PROBE genautoref: PURE OBSERVER, changes nothing.
                    // The generic-autoref hole's census class twelve, spelled
                    // as a predicate: a plain Call (mono rewrote the generic
                    // method into one), slot arg0, the receiver autoref'd into
                    // an AddrOfTemp, a holder to hang a loan on, a LIVE root —
                    // and NOT rescued by the self-borrowing receiver tie above
                    // (`tied_recv`). Existence only: it answered "is this site
                    // reached at all outside the acceptance ledger", which the
                    // ledger reader could not, the tie having fired ONCE across
                    // 423 ledger compiles.
                    // ⚠ ITS POPULATION SHRANK WHEN THE TIE ABOVE BECAME
                    // UNCONDITIONAL: `!tied_recv` now excludes exactly the
                    // calls the tie covers, so the 43-in-482 and 3463-in-8518
                    // fire counts recorded for genautoref/genautorefx were
                    // taken against the OLD, narrower `tied_recv` and are not
                    // re-measurable as they stand. What remains under them is
                    // the RESIDUE: arg0 autorefs whose callee does not resolve,
                    // or resolves to a signature whose result does not borrow
                    // the receiver.
                    // ⚠ CORRECTION 2026-08-27: the residue IS measurable after
                    // all. The coverage map was taken on the
                    // post-unconditional-tie source and counts 2,397 arrivals
                    // at this guard across 8060 runs.
                    if (ai == 1 && !tied_recv && !holder.empty() &&
                        a.kind() == Code::AddrOfTemp) {
                        BorrowPlace pbp = extract_borrow_place(
                            EAddrOfTempView{a}.inner(), pool);
                        if (!pbp.root.empty() && var_has(pbp.root_slot, pbp.root)) {
                            (void)logos::probe::on("genautoref");
                            // PROBE genautorefx: the SAME site, ARMED. The
                            // observer above cannot be priced — it changes
                            // nothing by construction — so this is the
                            // hypothesis actually spelled out: tie the
                            // autoref'd receiver's borrow to the holder, which
                            // is what the branch above now does for any
                            // Ref/MutRef/DstRef receiver. Deliberately crude:
                            // no callee resolution, no signature analysis, the
                            // mutability read straight off the autoref — and
                            // that is what it cost. PRICED on 8518 pass/fail
                            // tests plus the stdlib: CHANGED = 7, of which
                            // FINDING 0 and COST 7 (borrow_trait,
                            // bc_argcomp_replace_admit, core_6_10_derive_debug,
                            // fmt_debug_builders, fmt_pretty_print,
                            // writ_typed_placement,
                            // zone_mut_thin_source_admits_generic). The
                            // signature-asking version landed above changed 0
                            // of the same 8518. Kept as the standing control
                            // for "why ask the callee at all".
                            if (logos::probe::on("genautorefx")) {
                                bool rawptr = pbp.root_type &&
                                    pbp.root_type.kind() == LogosType::Kind::Ptr;
                                if (!rawptr)
                                    record_borrow(pbp,
                                        EAddrOfTempView{a}.is_mut(), line,
                                        holder, {/*skip_mut_binding=*/true});
                            }
                        }
                    }
                    // ⚠ TWO PREDICATES FOR "DOES THIS OPERAND NEED RECURSION".
                    // The aggregate-literal arms (StructLit / TupleLit / ArrLit)
                    // recurse UNCONDITIONALLY; these two Call arms filter by a
                    // narrow TYPE test written later. A `Kind::Closure` argument
                    // passes NEITHER disjunct, so the capture-deposit code at the
                    // ClosureBox arm was never reached for a closure handed to a
                    // call — MEASURED: `let c = ||{x=4;}; let n = x;` refuses,
                    // and the same program with `id(||{x=4;})` for a generic
                    // `fn id<F>(f:F)->F` admits. Delegated to the predicate this
                    // file already owns for "what does this expression retain".
                    // PROBE capargclos, site 1 of 2 (the free-call arm). A
                    // closure LITERAL handed to a call passes NEITHER disjunct
                    // unless the call's RESULT is borrow-carrying, so the
                    // ClosureBox arm never runs and NO capture is deposited.
                    // MEASURED BY HAND with LOGOS_DUMP_BC_CAPTURE, one token
                    // apart: `b.bar(|| { let n: i64 = x; });` deposits NOTHING;
                    // `let c = || { let n: i64 = x; }; b.bar(c);` deposits
                    // root=x holder=c. Rule 16: "no fact recorded" and "the
                    // fact is absent" are different, and only the MINTING SITE
                    // distinguishes them. The fire count IS the arrival census.
                    bool closarg = a.kind() == Code::ClosureBox &&
                                   logos::probe::on("capargclos");
                    if (is_ref_kind(a.type(pool)) ||
                        (res_bc && is_borrow_carrying_type(a.type(pool))) ||
                        retains_borrowing_operand(a) || closarg)
                        take_ref_borrows(a, line, holder, record_only);  // #70
                });
                break;
            }
            case Code::MethodCall: {
                EMethodCallView v{e};
                auto recv = v.receiver();
                // Consume the reborrow-mut floor set by the `&mut v[i]` router;
                // reset immediately so nested arg processing doesn't inherit it.
                bool force_mut = reborrow_force_mut_;
                reborrow_force_mut_ = false;
                // A method whose result does NOT borrow self (`Vec<str>::get`
                // returns T by value — a COPY of the stored borrow, whose
                // lifetime is the element's, not `&self`'s) produces a plain
                // value: no receiver tie. Route through the exec visitor —
                // receiver conflict checks + call-scope arg borrows — instead
                // of falling into the ref-receiver forwarding below, whose
                // `default:` consuming visit would MOVE a `&mut`-typed
                // receiver (`let t: str = v.get(i)` must not move `v`).
                if (!result_borrows_self(v)) {
                    // CEILING PROBE `mcallrefrecv` — the arm written to walk
                    // INTO a reference receiver (`else if (recv &&
                    // is_ref_kind(...)) take_ref_borrows(recv, …)  // #70`
                    // below) is unreachable by construction: everything past
                    // this exit has result_borrows_self true, so that arm
                    // needs recv == null and then fails its own `recv &&`.
                    // Purely additive: delegate here instead. Records only.
                    // ⛔ DECLINED ON COST — MEASURED 2026-08-28: CEILING 0 vs
                    // COST 9 legal programs refused (bc_argcomp_replace_admit,
                    // bc_d1_match_scalar_admits, bc_d1r2_let_else_admits,
                    // bc_d1r7_b1_destructure_deferred, bc_d1_unrelated_local,
                    // bc_d8_disjoint_field_use/quote_field_split,
                    // 03_ownership borrow_trait + drop_glue_struct_homonym).
                    // ⚠ THE CEILING IS THE WEAK HALF: the probe fired only 3
                    // times across the 400 ledger compiles, so 0 there is an
                    // absence of population, not a refutation. The COST is the
                    // measurement that decides: delegating on every
                    // ref-receiver call locks the referent for the holder's
                    // lifetime and `let n = r.len()` must not lock anything.
                    // The unreachable #70 arm below stays unreachable; if it
                    // is ever revived it needs an exemption analysis first.
                    if (logos::probe::on("mcallrefrecv") && recv &&
                        is_ref_kind(recv.type(pool)))
                        take_ref_borrows(recv, line, holder, record_only);
                    if (!record_only) visit(e, /*consuming=*/false, line);
                    break;
                }
                // Front (a): a method whose result borrows `self` (elision) ties
                // the returned reference to the receiver — hold a borrow of the
                // receiver's root place for the result's lifetime (`holder`), of
                // the receiver's mutability. So `let v=c.get_ref(); c.set(…)` while
                // `v` is live is rejected. Conservative (result_borrows_self only
                // fires for fully-elided &self→&ret methods). Borrows THROUGH a
                // reference/pointer root are skipped (B93.2 — same as elsewhere).
                if (recv && recv.kind() == Code::AddrOfTemp && result_borrows_self(v)) {
                    EAddrOfTempView av{recv};
                    BorrowPlace bp = extract_borrow_place(av.inner(), pool);
                    // Record on value AND reference roots (so a reborrow through a
                    // `&`/`&mut` ref — `&(*a)[i]`, now rooted at `a` by extract —
                    // is tracked); only RAW pointers are unchecked (Rust parity).
                    bool root_is_rawptr = bp.root_type &&
                        bp.root_type.kind() == LogosType::Kind::Ptr;
                    if (!bp.root.empty() && !root_is_rawptr && var_has(bp.root_slot, bp.root)) {
                        bool m = av.is_mut() || force_mut;
                        // D8: field-precise when the receiver is a field chain.
                        // `self.w.next_batch()` (sema wrapped the place in an
                        // AddrOfTemp because the method takes `&mut self`)
                        // borrows self.w — NOT all of self; whole-root here
                        // falsely locked every sibling-field use for the
                        // holder's lifetime (`self.sc.clear()` after). Mirrors
                        // the two siblings that already split on the path: the
                        // bare-place receiver arm below and the explicit
                        // `&mut place` AddrOf arm.
                        record_borrow(bp, m, line, holder);
                    }
                } else if (recv && result_borrows_self(v)) {
                    // Bare VarRef / place receiver — sema didn't wrap it in
                    // AddrOfTemp (`v.iter_mut()` with v a value local). Same
                    // elision rule: a `&T` / borrow-carrying result holds the
                    // receiver borrow for the holder's lifetime, with the
                    // METHOD's self mutability (adversarial #2 f12 — two live
                    // iter_mut() aliased &mut without this; f13 — `&*b` then
                    // move of the Box). EXCEPT Rc/Arc roots: shared-ownership
                    // handles are the blessed interior-mutability domain
                    // (mutable Writ = Rc root owner; `h.array()` then
                    // `hold(&mut h, root)` is the residency escape pattern —
                    // the holder machinery launders the alias).
                    BorrowPlace bp = extract_borrow_place(recv, pool);
                    bool root_is_rawptr = bp.root_type &&
                        bp.root_type.kind() == LogosType::Kind::Ptr;
                    bool root_is_rc = false;
                    if (bp.root_type &&
                        (bp.root_type.kind() == LogosType::Kind::Struct ||
                         bp.root_type.kind() == LogosType::Kind::ZonedStruct)) {
                        std::string rn(bp.root_type.struct_name());
                        if (auto d = rn.rfind('.'); d != std::string::npos)
                            rn = rn.substr(d + 1);
                        if (auto g = rn.find("$G"); g != std::string::npos)
                            rn = rn.substr(0, g);
                        // CEILING PROBE `rcexempt` — this exemption has never
                        // been checked in the ABUSE direction. It is a STRING
                        // match on the struct name after stripping the module
                        // prefix and the `$G` suffix: a lookup key, not an
                        // identity. Force it off — calls that recorded NO
                        // borrow now record one; nothing is admitted.
                        // MEASURED 2026-08-28 in the ABUSE direction, which is
                        // what this slot bought: CEILING 0, COST 0, over 3
                        // fires in the 400 ledger compiles (2,347 arrivals at
                        // the enclosing struct-root test over the 8060-run
                        // coverage population). The hatch holds open no ledger
                        // row, so keeping it costs nothing THAT THE LEDGER CAN
                        // SEE. ⚠ COST 0 IS NOT A SAFETY CLAIM and CEILING 0
                        // over 3 fires is not a refutation: the string-keyed
                        // identity (any user type named `Rc`/`Arc` inherits
                        // the exemption) is still unpriced. Closing it needs a
                        // hand-written counter-example, not another ledger run.
                        root_is_rc = !logos::probe::on("rcexempt") &&
                                     (rn == "Rc" || rn == "Arc");
                    }
                    if (!bp.root.empty() && !root_is_rawptr && !root_is_rc &&
                        var_has(bp.root_slot, bp.root)) {
                        bool m = method_self_kind(v) == 2 || force_mut;
                        // Field-precise when the receiver is a field chain
                        // (`self.arc.deref_mut()` borrows self.arc, not all
                        // of self) — whole-root would falsely lock sibling
                        // field uses for the holder's lifetime.
                        record_borrow(bp, m, line, holder,
                                      {/*skip_mut_binding=*/true});
                    }
                } else if (recv && is_ref_kind(recv.type(pool))) {
                    take_ref_borrows(recv, line, holder, record_only);  // #70
                }
                // D1: same by-value rule as the free-call arm above.
                bool res_bc_m = is_borrow_carrying_type(e.type(pool));
                v.each_arg([&](ExprRef a) {
                    if (!a) return;
                    // Same delegation as the free-call arm above, same reason.
                    // PROBE capargclos, site 2 of 2 (the METHOD-call arm) —
                    // and this is the site issue-51268 arrives at
                    // (`self.thing.bar(|| { let _n = self.number; })`). ONE
                    // name, TWO sites, and they are named here because the
                    // fire count is their SUM (the `rootkeep` defect).
                    bool closarg_m = a.kind() == Code::ClosureBox &&
                                     logos::probe::on("capargclos");
                    if (is_ref_kind(a.type(pool)) ||
                        (res_bc_m && is_borrow_carrying_type(a.type(pool))) ||
                        retains_borrowing_operand(a) || closarg_m)
                        take_ref_borrows(a, line, holder, record_only);  // #70
                });
                break;
            }
            case Code::MatchExpr: {
                EMatchExprView v{e};
                visit(v.scrut(), /*consuming=*/false, line);
                // D1 (door 4): a match used as a VALUE (`let b = match ob {
                // Some(bb) => bb, … }`) extracts through its arm BINDINGS.
                // `visit`'s MatchExpr arm does this via propagate_pat_loans;
                // this arm never declared pattern bindings at all, so the
                // binding was an unknown name and the hop was lost. Give the
                // bindings the scrutinee's loans here too — the arm value then
                // carries them on to `holder` through the top-of-function hop
                // rule, exactly as `ob.unwrap()` does.
                std::vector<std::string> scrut_roots;
                if (v.scrut() && type_may_carry_borrow(v.scrut().type(pool)))
                    bc_hop_roots(v.scrut(), scrut_roots);
                // H8 — spelling 2 of 3: the match used as a VALUE.
                if (auto st = retain_temp_scrut_loan(v.scrut(), line); !st.empty())
                    scrut_roots.push_back(std::move(st));
                // ── D1 round 14 / Q6: TWO OF THE FOUR PROPAGATORS ─────────
                //
                // Adding the pattern propagators as the coverage table's
                // FOURTH COLUMN is what showed this: there are three sites that
                // bind a pattern against a scrutinee, and only two of them run
                // all four propagators.
                //
                //   site                       sources prov loans reborrows
                //   stmt::Match      (§7428)      X      X     X       X
                //   stmt Let/LetElse (§7228)      X      X     X       X
                //   MatchExpr as an RVALUE (here) .      .     X       X
                //
                // So a match used as a VALUE never fed §B6 (`ref_borrow_
                // sources_`) or `prov_`, and §B6 is the channel `pop_scope`
                // reads to raise E0597. This is the residual half of Q2: `?`
                // is ALREADY desugared to an rvalue match by the time the
                // checker runs (measured — the new Try arm above fires ZERO
                // times on `pick(&x)?` and the MatchExpr arm fires once), so
                // the payload binding `__try_ok_N` named no source, and
                // `{ let x = 7; o = pick(&x)?; } *o` admitted at rc=0 while
                // the direct-return twin refused at rc=1.
                //
                // Both propagators are the SAME calls the other two sites
                // make, with the same gates: `propagate_pat_sources` records
                // only bindings whose own type is ref/bc and only from a
                // NON-EMPTY source set, and `propagate_pat_prov` gates on the
                // scrutinee being borrow- or loan-carrying. Neither can invent
                // a source: both copy from what the scrutinee already names.
                std::vector<std::string> scrut_sources;
                collect_ref_sources(v.scrut(), scrut_sources);
                // ⚠ ARMS ARE ALTERNATIVES — the same omission the IfExpr arm
                // above had, in the same pass. The CHECK pass's MatchExpr arm
                // walks every arm from ONE baseline and joins with merge_loans;
                // this one walked them in sequence, so
                // `match c { true => &mut a, false => &mut a }` took a second
                // mutable loan on top of the first and refused itself. Only the
                // LOAN state is rebased here: the pattern propagators and the
                // §B6 / prov_ deposits are accumulative by design and are left
                // exactly as they were.
                std::optional<StateMap> merged_arm_s;
                auto saved_arm_s = states_;
                v.each_arm([&](EMatchArmRef arm) {
                    states_ = saved_arm_s;
                    if (auto g = arm.guard())
                        if (!record_only) visit(g, /*consuming=*/true, line);  // #70
                    propagate_pat_sources(arm.pat(), scrut_sources, line);  // §B6
                    propagate_pat_prov(arm.pat(), v.scrut());               // r3
                    propagate_pat_loans(arm.pat(), scrut_roots, line);
                    propagate_pat_reborrows(arm.pat(), v.scrut());  // D1 r13
                    // ── THE FIFTH PATTERN PROPAGATOR, AT THE THIRD SITE ──
                    //
                    // `propagate_pat_borrows` is the only one of the five that
                    // RAISES a loan rather than copying one, and it ran at
                    // stmt::Match and stmt::LetElse and not here. So a match
                    // used as a VALUE — `let r: &mut i64 = match y { Y { f0:
                    // ref mut a, .. } => a };` — extracted a reference to the
                    // scrutinee and recorded no borrow of it at all, and every
                    // later conflicting use of `y` was admitted.
                    //
                    // THE BLOCKER ITS OWN COMMENT NAMED IS SIDESTEPPED, NOT
                    // SOLVED: "a loan held by a name no scope owns would never
                    // be released" is true of the pattern binding here, which
                    // this arm does not declare. `holder` — the enclosing
                    // `let`'s binding — has a scope and an NLL last use, so
                    // the release comes by construction. `record_only` is not
                    // consulted: this is a RECORD, which is what that flag
                    // permits.
                    //
                    // MEASURED 2026-08-28. Ceiling probe `mexprpatborrow`: 8
                    // fires over 393 ledger compiles, CEILING 4, corpus COST 0
                    // — closing exactly the four rows `mexprpatloan` was aimed
                    // at and fired zero on. The raw spelling (loan held by the
                    // undeclared binding name, never released) priced
                    // IDENTICALLY, 4 and 0: this corpus contains no
                    // `let x = match … { ref … }` with a later scrutinee use,
                    // so it cannot see release timing at all. The `holder`
                    // spelling is shipped because its release is a property of
                    // the code, not of corpus silence.
                    //
                    // ⚠ AND THE COST 0 WAS FALSE, broken by hand on the second
                    // shape tried — see `carried` at propagate_pat_borrows.
                    std::vector<std::string> arm_out;
                    arm_value_roots(arm.value(), arm_out);
                    if (std::getenv("LOGOS_MEPB_TRACE")) {
                        std::string j;
                        for (auto& r : arm_out) { j += r; j += ' '; }
                        fprintf(stderr, "[mepb] ln=%u holder='%s' val_kind=%d roots=[%s]\n",
                                line, holder.c_str(),
                                arm.value() ? (int)arm.value().kind() : -1, j.c_str());
                    }
                    if (!holder.empty() && !arm_out.empty())
                        propagate_pat_borrows(arm.pat(), v.scrut(), line,
                                              holder, &arm_out);
                    take_ref_borrows(arm.value(), line, holder, record_only);  // #70
                    if (!merged_arm_s) merged_arm_s = states_;
                    else merge_loans(*merged_arm_s, states_);
                });
                if (merged_arm_s) states_ = std::move(*merged_arm_s);
                break;
            }
            case Code::BlockExpr: {
                EBlockExprView v{e};
                // Door B: hand the escaping result+holder to visit_block so the
                // hop runs before the frame pops (see visit_block).
                // #70 residual, deliberate: visit_block is NOT gated on
                // record_only. It both re-visits statements (the double-move
                // hazard) and RE-HOMES escaping loans onto `holder` (Door B).
                // Gating it would trade a false red for a lost loan — the
                // permissive direction — so the block-valued-argument shape
                // keeps HEAD's behaviour until the two effects are separated.
                if (auto br = v.block()) visit_block(br, v.result(), holder);
                take_ref_borrows(v.result(), line, holder, record_only);  // #70
                break;
            }
            // P2-13: a non-`move` closure that MUTATES a capture holds a `&mut`
            // borrow of it for the closure's lifetime — registering that borrow
            // with the closure's holder ties release to its last use (NLL), so
            // the exclusivity machinery rejects e.g. reading/`&mut`-ing `x` while
            // a `x`-mutating closure is still live. A SHARED (read) capture is
            // left as a liveness check only: Logos captures a whole variable
            // (not a precise field path), so a whole-var shared borrow would
            // wrongly block RFC-2229 disjoint sibling mutation (`|| p.x` next to
            // `&mut p.y`). A `move` closure takes ownership — no borrow.
            case Code::ClosureBox: {
                // RFC-2229 phase-1: register a FIELD-PATH borrow per capture
                // (precise `p.x` instead of whole-var `p`). Disjoint sibling
                // access is sound — `&mut p.y` next to `||p.x` is allowed;
                // conflicting `&mut p.x` is rejected. Shared captures register
                // a shared field-path borrow (NLL-released at the holder's last
                // use); a `move` closure takes ownership — no borrow.
                EClosureBoxView cb{e};
                // THE CLOSURE BODY IS WALKED. `EClosureBoxView::body()` had ZERO
                // call sites in this file: mono_scan, mono_clone and mlir_gen
                // all walk it, and the loan machinery alone stopped at the
                // capture NAMES, so nothing a closure body DID was ever
                // checked. Three parts, and the first is the whole mechanism:
                //   1. IT RUNS BEFORE THE CAPTURE LOOP. All NINE of crude
                //      capbody's costs are ONE cause, and it is not the one
                //      the crude comment predicted: the body MUTATES exactly
                //      the place the closure CAPTURED, so walking it after
                //      the capture arm recorded a mut loan held by the
                //      closure binding made the closure conflict WITH
                //      ITSELF (`let mut c = || { s.a = s.a + 1; }`). The
                //      body is the JUSTIFICATION for the capture loan, not a
                //      second access competing with it. Walking first is
                //      that exemption, spelled as an ORDER rather than as a
                //      suppression flag: conflicts with loans held OUTSIDE
                //      the closure still fire, and so do conflicts BETWEEN
                //      two accesses inside one body — both are walked
                //      (fail/bc_capbody_intra_body_conflict_refuse).
                //   0. A `move` CLOSURE'S BODY IS NOT WALKED. It owns its
                //      captures, so its body operates on env copies and has
                //      no business being read in the ENCLOSING frame's
                //      namespace. MEASURED: walking both arms and walking
                //      only the non-move arm price IDENTICALLY — same 13
                //      rows, same cost 0 — so the three move-shaped rows
                //      (arc-consumed-in-looped-closure, closure-move-spans,
                //      borrowck-in-static--move-captured-out-of-fn-closure)
                //      close for a NON-move reason. Same benefit, smaller
                //      blast radius: the narrower rule is the one that lands.
                //   2. A CHILD FRAME WITH THE PARAMS DECLARED. `|v: i64| {
                //      a[0] = v; }` (spec expr_6) reads an undeclared name
                //      without this.
                //   3. THE BODY IS SCANNED FOR USES. `scan_uses_expr`'s
                //      ClosureBox arm stops at the capture names exactly as
                //      the loan channel does, so body locals had no
                //      last-use line and NLL could not retire their loans.
                walk_closure_body(cb);
                // D6 follow-on: the field arm needs the capture ROOT's TYPE.
                // It passed `nullptr`, so take_field_borrow's mut-binding check
                // could not see a `&mut`-REFERENCE root and refused
                // `|| { this.x = … }` with "'this' not declared as mut" for
                // `let this: &mut Foo` — legal in Rust, mutability comes from
                // the reference TYPE, not the binding. Invisible while every
                // field capture was recorded SHARED (the check is gated on
                // is_mut); measured the moment D6 made it mut, as the imported
                // ledger row borrowck-closures-unique-imm.
                std::vector<TypeRef> cap_types;
                cb.each_capture(prog_.type_pool.impl(),
                                [&](std::string_view, TypeRef t) {
                                    cap_types.push_back(t);
                                });
                uint64_t i = 0;
                cb.each_capture_name([&](std::string_view cap) {
                    if (cb.is_move()) {
                        // PROBE capmove: a `move` closure deposits NOTHING —
                        // no borrow, no move, no check_live, no inherit_loans.
                        // MEASURED 2026-08-27, 423-row acceptance population:
                        // 7 FIRES in the WHOLE corpus — a near-dead site, and
                        // the fire count is itself the finding. CEILING 2
                        // (borrowck-loan-blocks-move-cc--r10, issue-101119)
                        // vs COST 3 (bc_clsC_b1_closure_arg_move_admit,
                        // move_closure_copy_capture, spec borrow_1) — ⛔ STOP
                        // SIGN. consume() does not ask about Copy-ness, and
                        // `let n: i64 = 1; let f = move || n;` is legal. Its
                        // 2 rows are DISJOINT from every other closure probe,
                        // so `move`-capture is its own mechanism — just a
                        // 2-row one. DECLINED: cost >= ceiling.
                        if (logos::probe::on("capmove"))
                            consume(std::string(cap), line);
                        // PROBE capmoveloan: capmove was DECLINED at CEILING 2
                        // vs COST 3 — ⛔ — and all three of its costs are one
                        // shape, `let n: i64 = 1i64; let f = move || n;`, a
                        // move-capture of a value NOTHING has borrowed.
                        // consume() does not ask about Copy-ness and cannot,
                        // so the exemption is stated on the other side: refuse
                        // only when the root ALREADY carries a live loan,
                        // which is the upstream reason for
                        // borrowck-loan-blocks-move-cc and
                        // borrowck-multiple-captures both ("cannot move out of
                        // X because it is borrowed").
                        // MEASURED 2026-08-28, 371-row population: 8 fires,
                        // CEILING 1 vs COST 0 — ✓, where capmove is ⛔ 2 vs 3.
                        // The precondition removes all three costs and one of
                        // the two rows; only
                        //   borrowck/borrowck-loan-blocks-move-cc--r10
                        // closes. Predicted FOUR (--r10, --t10,
                        // borrowck-multiple-captures, issue-101119), got one:
                        // the other three hold a loan this record does not see
                        // at the capture point, so their blocker is a SECOND
                        // mechanism and they retire only when both go. A
                        // 1-row mechanism, honestly priced.
                        else if (logos::probe::on("capmoveloan") &&
                                 root_has_live_loan(std::string(cap)))
                            consume(std::string(cap), line);
                        ++i; return;
                    }
                    std::string root(cap);
                    std::string_view fpath = cb.capture_path(i);
                    std::string rel;
                    if (fpath.size() > root.size() + 1 &&
                        fpath.compare(0, root.size(), root) == 0 &&
                        fpath[root.size()] == '.')
                        rel = std::string(fpath.substr(root.size() + 1));
                    bool is_mut = cb.capture_is_mut(i);
                    // PROBE capmut: deposit strength comes from sema's
                    // per-capture mutability; nothing asks what the BODY does.
                    // MEASURED 2026-08-27: 49 fires, CEILING 18 vs COST 17 —
                    // ⛔ STOP SIGN, and the DELTA is what it was priced for.
                    // capshared's 4 rows are a strict SUBSET of these 18
                    // (overlap 4/4), so 'any use vs live capture' buys 14 rows
                    // over 'mutation vs live shared capture' and pays 17 legal
                    // refusals for them — `let f = || read(x); let n = x;` is
                    // legal Rust. 'just make every capture exclusive' is
                    // RETIRED for the price of one 70 s run; capshared stands.
                    if (logos::probe::on("capmut")) is_mut = true;
                    if (std::getenv("LOGOS_DUMP_BC_CAPTURE"))
                        std::fprintf(stderr,
                            "[bc-capture] line=%u root=%s fpath=%s rel=%s "
                            "is_mut=%d is_move=%d holder=%s\n",
                            line, root.c_str(), std::string(fpath).c_str(),
                            rel.c_str(), (int)is_mut, (int)cb.is_move(),
                            holder.c_str());
                    // RFC-2229 POLICY, NOT A PLACE DECISION: a SHARED
                    // whole-var capture stays liveness-only, so `|| p.x`
                    // beside `&mut p.y` is not falsely blocked. That is the
                    // only thing left in the caller; the whole/field decision
                    // moved to record_borrow with the other fifteen.
                    bool shared_whole = rel.empty() && !is_mut;
                    // PROBE capshared: turn the liveness-only branch into a
                    // recorded shared loan held by the closure binding.
                    // MEASURED 2026-08-27: 49 fires, CEILING 4 vs COST 0 —
                    // borrowck-closures-mut-and-imm, closure-borrow-spans--a
                    // and --b, region-bound-on-closure-outlives-call. ⚠ COST 0
                    // IS NOT A SAFETY CLAIM: the RFC-2229 exemption named
                    // above is REAL and this crude form ignores it, so a
                    // careful version must still construct its own
                    // counter-examples (`|| p.x` beside `&mut p.y`;
                    // `let f=||x; let g=||x;` shared+shared). Its 4 rows are a
                    // strict subset of capmut's 18 and DISJOINT from capbody.
                    // RE-PRICED 2026-08-28 (rule 8) on the 371-row ledger: 31
                    // fires, CEILING 4 — the same four, named — but COST 1,
                    // not 0. The corpus widened 487 -> 807 and the new cost is
                    // tests/imported/pass/closures/
                    // capture-disjoint-field-tuple-b156 — the very RFC-2229
                    // shape this comment cites as the reason for the branch.
                    // ⚠ AND THE JUSTIFICATION AS WRITTEN IS STALE. The
                    // comment's own example — `|| p.x` beside `&mut p.y` — is
                    // ADMITTED with this probe armed, MEASURED by hand: a
                    // STRUCT-field capture gets `fpath=p.x rel=x`, never
                    // reaches `shared_whole`, and so was never what this
                    // branch protected. The single cost is a TUPLE, and it is
                    // here for a different defect: the capture SCANNER's
                    // TupleIndex arm never asks `try_path`, which WAS taught
                    // tuple indices. The policy is real; this branch is not
                    // what implements it — capture-path precision is.
                    // ⚠ THAT PRECISION LANDED 2026-08-28 (sema_expr.cpp's
                    // TupleIndex arm now asks `try_path`), so b156 no longer
                    // reaches `shared_whole` and capshared's only measured cost
                    // is retired. RE-PRICED on the 368-row ledger after the
                    // landing: 28 fires, CEILING 4 vs COST 0 — the same
                    // four rows named above, and b156 no longer among the
                    // costs, which is the prediction confirmed by measurement
                    // rather than by argument. Rule 8 says re-price again before
                    // funding it; this number is a measurement with a date.
                    if (logos::probe::on("capshared") && shared_whole)
                        shared_whole = false;   // fall to record_borrow
                    if (shared_whole) {
                        check_live(root, line);
                    } else {
                        // The FIELD arm must be keyed on the same holder as
                        // the whole-root arm eleven lines above: without it
                        // the record is `holder.empty()`, and
                        // release_dead_borrows' field loop skips exactly
                        // those, so a `|| s.a` capture was retired only
                        // lexically by pop_scope and `s.a = 5` after the
                        // closure's last use refused in the SAME frame.
                        BorrowPlace cbp;
                        cbp.root = root;
                        cbp.root_slot = NO_SLOT;
                        cbp.path = rel;
                        cbp.root_type = i < cap_types.size() ? cap_types[i]
                                                             : TypeRef(nullptr);
                        // PROBE capscope: an EMPTY holder is skipped by both
                        // release_dead_borrows loops, so the loan becomes
                        // LEXICAL and survives the loop back edge.
                        // MEASURED 2026-08-27: 14 fires, CEILING 2 vs COST 9 —
                        // ⛔ STOP SIGN, and worse: BOTH its rows
                        // (issue-42574-…--b, regions-nested-fns) are already
                        // in capbody's 19, so it contributes NOTHING capbody
                        // does not. The pre-stated fork ("if capscope scores 0
                        // while capmove scores on the same loop fixtures, the
                        // loop rows are a MOVE story") resolved as NEITHER:
                        // capmove's 2 rows are disjoint from capscope's, so
                        // the loop rows are a BODY story. DECLINED.
                        record_borrow(cbp, is_mut, line,
                                      logos::probe::on("capscope")
                                          ? std::string() : holder);
                        if (!rel.empty()) check_live(root, line);
                    }
                    // D1 round 2, Door D: the capture is a HOP. Registering a
                    // borrow of the capture root says "the closure reads b";
                    // it says nothing about the loans `b` itself holds, and a
                    // closure's type is Kind::Closure so the bc gate at the top
                    // of take_ref_borrows never fires for it either. So `let b
                    // = c.mk(); let g = || *b.p; c.bump(); g()` released the
                    // loan on `c` at `b`'s last use — which is the closure's
                    // construction line — while the closure could still deref
                    // it. The closure binding joins the holder set, exactly as
                    // a struct field or a match binding does. A `move` closure
                    // returns above, before this: it OWNS the value, and the
                    // ownership hop is the by-value rule, not the capture one.
                    if (!holder.empty()) inherit_loans(root, holder, line);
                    ++i;
                });
                // The crude `capbody` probe that priced this site (CEILING 19 vs
                // COST 9) is GONE with its subject: its nine costs were one
                // cause, named in part 1 above, and the careful walk that
                // replaced it prices 13 vs 0. The two row sets are not nested
                // — 5 of the 13 are rows the crude form never reached — so
                // the crude ceiling was never an upper bound on this fix, only
                // on itself.
                break;
            }
            // A borrow captured into an aggregate LITERAL — `let g = Guard { r: &mut f }`,
            // `let t = (&a, &b)`, `let arr = [&x]` — is held for the lifetime of the
            // binding the aggregate flows into. Recurse into each field/element with
            // the SAME holder so the loan persists (NLL-released at the holder's last
            // use). Non-borrow field values hit the default case = the same consuming
            // visit as before (move tracking preserved). Without this, a struct-/tuple-
            // held borrow was transient (released at end of the constructing stmt) and a
            // later mutation/move/commit of the borrowed place while the holder was live
            // went undetected (Rust E0502/E0505/E0505-on-move).
            case Code::StructLit: {
                EStructLitView{e}.each_field_value([&](ExprRef fv) {
                    take_ref_borrows(fv, line, holder, record_only);  // #70
                });
                break;
            }
            case Code::TupleLit: {
                ETupleLitView{e}.each_elem([&](ExprRef el) {
                    take_ref_borrows(el, line, holder, record_only);  // #70
                });
                break;
            }
            case Code::ArrLit: {
                EArrLitView{e}.each_elem([&](ExprRef el) {
                    take_ref_borrows(el, line, holder, record_only);  // #70
                });
                break;
            }
            // D1: an ENUM literal is an aggregate literal like the three above
            // — `let ob = Option::Some(c.mk())` captures the payload's borrow
            // into `ob` for `ob`'s lifetime. The loan channel was the only one
            // missing this case (collect_ref_sources and prov_of both already
            // recurse into EnumLitData payloads), so a borrow wrapped in an
            // enum was TRANSIENT: released at the end of the constructing
            // statement, and the later mutation of the borrowed place went
            // undetected (door 2a, `Option::Some(c.mk()); c.bump()`).
            case Code::EnumLitData: {
                EEnumLitDataView{e}.each_payload([&](ExprRef pl) {
                    take_ref_borrows(pl, line, holder, record_only);  // #70
                });
                break;
            }
            default:
                // EVarRef (ref param forwarded), ECall, EMethodCall, etc.
                if (!record_only) visit(e, /*consuming=*/true, line);
                break;
        }
    }
    // ── Statement visitor ─────────────────────────────────────────────────

    // Phase 9 (NLL): pre-pass over fn body computing the max line at which
    // each named local is read. The borrow checker uses this to release
    // borrows whose holder's last use has passed — making borrows non-lexical.
    // G0 — the pre-pass's own referent extractor. `reborrow_referent` cannot be
    // reused here: it consults `var_has` / `param_names_`, which are populated
    // by the MAIN pass and are empty while this one runs. The pre-pass only
    // ever extends a lifetime, so it can afford to be looser about which names
    // are locals; a name that is not one simply never holds a loan.
    // Round 8: `prescan_referent` — the THIRD copy of the three-kind shape gate
    // — is deleted. The shapes come from `ref_source_places`; what this pass
    // keeps is its own POLICY, which is the absence of the checker's two
    // filters (var_has / param_names_ are empty while this pre-pass runs), plus
    // the materialized-temp exclusion. Writes are MONOTONE (`add`).
    void prescan_note(const std::string& place, lir_view::ExprRef val) {
        std::vector<std::string> srcs;
        ref_source_places(val, prog_.type_pool.impl(), srcs);
        for (auto& p : srcs)
            if (!is_materialized_temp_name(ref_place_root(p)))
                reborrow_prescan_.add(place, p);
    }
    // The pre-pass's POLICY for ONE pair. N0's nested recursion is NOT written
    // here either — it is `each_ref_store`, shared with the checker and the
    // summarizer. This instance is MONOTONE (`add`) and has no retraction to
    // mirror: that difference is the stated policy, not an omission.
    void prescan_reborrow_place(const std::string& place, lir_view::ExprRef val) {
        if (place.empty() || !val) return;
        if (!is_reborrow_ref_kind(val.type(prog_.type_pool.impl()))) return;
        prescan_note(place, val);
    }
    void prescan_reborrow(const std::string& name, TypeRef t, lir_view::ExprRef val) {
        if (name.empty() || !val) return;
        TypeRef vt = val.type(prog_.type_pool.impl());
        if (is_reborrow_ref_kind(t) || is_reborrow_ref_kind(vt))     // R7a rule 2
            prescan_note(name, val);
        each_nested_ref_store(name, val,
            [&](const std::string& place, lir_view::ExprRef v) {
                prescan_reborrow_place(place, v);
            });
    }
    // A use of `n` is a use of everything `n` reborrows — `n` itself when it IS
    // a reborrow, and every place recorded UNDER it (a struct that holds a
    // `&mut` keeps the referent borrowed while the struct is live). Chasing is
    // hop-bounded, matching rehome_reborrow.
    void note_reborrow_alias_uses(const std::string& n, uint64_t line) {
        if (n.empty() || reborrow_prescan_.empty()) return;
        // Round 8: the 8-hop chase is RefGraph::each_root (it already did the
        // transitive thing correctly — it is the one channel that did).
        auto chase = [&](const std::string& start) {
            reborrow_prescan_.each_root(start, [&](const std::string& m) {
                if (m != start) note_use(ref_place_root(m), line);
            });
        };
        chase(n);
        std::string pfx = n + ".";
        for (auto& kv : reborrow_prescan_.e_)
            if (kv.first.size() > pfx.size() &&
                kv.first.compare(0, pfx.size(), pfx) == 0)
                for (auto& r : kv.second) {
                    note_use(ref_place_root(r), line);
                    chase(r);
                }
    }
    // ── D1 round 6 / G1 ───────────────────────────────────────────────────
    //
    // THE DEFECT. `flow_of_call` was reached ONLY from the Code::Call and
    // Code::MethodCall arms, so an indirect call through a fn POINTER consulted
    // no summary at all — and, unlike the closure case, got no elision fallback
    // either: it received NOTHING. Measured: `let g: fn(&C) -> B = mkd;
    // vs.push(g(&c)); c.bump(); *vs.get(0).p` rc=0 against rc=1 for the direct
    // `vs.push(mkd(&c))`, with mkd's summary IDENTICAL (`result<-0x1`) in both
    // programs. Same for the out-param half through `stash2`.
    //
    // Resolution rule (the task's, and Rust's): a fn-pointer local assigned
    // exactly ONCE from a named function IS that function — resolve it and use
    // its real summary. Reassigned or opaque (a parameter) ⇒ unresolvable ⇒ the
    // documented (d) conservative route, which for a fn pointer means the SAME
    // elision the summary-less Call already takes (every borrowing operand can
    // reach the result / the `&mut` out-params), not silence.
    void prescan_fnptr(const std::string& name, TypeRef t, lir_view::ExprRef val) {
        using Code = lir_schema::expr::Code;
        if (name.empty() || !val) return;
        TypeRef vt = val.type(prog_.type_pool.impl());
        // Kind::FnItem is the per-instantiation fn-ITEM type (logos-core 1.4):
        // `let g: fn(&C) -> B = mkd;` has declared type FnPtr but VALUE type
        // FnItem, and a later `g = mkd2;` has NO declared type at all — so
        // testing FnPtr alone silently skipped every REASSIGNMENT and left the
        // binding looking single-assigned. Measured: `g` resolved to `mkd`
        // twice and `mkd2` was never seen, i.e. the multi-callee guard could
        // not fire. Both kinds count.
        auto is_fnish = [](TypeRef k) {
            return k && (k.kind() == LogosType::Kind::FnPtr ||
                         k.kind() == LogosType::Kind::FnItem);
        };
        if (!is_fnish(t) && !is_fnish(vt)) return;
        std::string sym;
        if (val.kind() == Code::VarRef) sym = std::string(lir_view::EVarRefView{val}.name());
        if (sym.empty()) { fnptr_multi_.insert(name); return; }   // unresolvable value
        auto it = fnptr_sym_.find(name);
        if (it == fnptr_sym_.end()) fnptr_sym_[name] = std::move(sym);
        else if (it->second != sym) fnptr_multi_.insert(name);    // two callees
    }
    // The callee's summary for a FnPtrCall, or null when the pointer does not
    // resolve to exactly one named function.
    const FlowSummary* flow_of_fnptr(lir_view::ExprRef callee) const {
        using Code = lir_schema::expr::Code;
        if (!callee || callee.kind() != Code::VarRef) return nullptr;
        std::string n(lir_view::EVarRefView{callee}.name());
        if (fnptr_multi_.count(n)) return nullptr;
        auto it = fnptr_sym_.find(n);
        if (it == fnptr_sym_.end()) return nullptr;
        return flow_of_call(it->second);
    }
    void note_use(std::string name, uint64_t line) {
        note_use_slot(NO_SLOT, std::move(name), line);
    }
    // F5: `slot` is the dense binding slot when the use site knows it
    // (VarRef), NO_SLOT otherwise.
    void note_use_slot(uint32_t slot, std::string name, uint64_t line) {
        if (name.empty()) return;
        auto& any = last_use_line_[name];
        if (line > any) any = line;
        if (slot == NO_SLOT) {
            auto& u = last_use_unslotted_[name];
            if (line > u) u = line;
        } else {
            auto& s = last_use_slot_[slot];
            if (line > s) s = line;
        }
    }

    void scan_uses_expr(lir_view::ExprRef e, uint64_t line) {
        if (!e) return;
        using namespace lir_view;
        using Code = lir_schema::expr::Code;
        switch (e.kind()) {
            case Code::VarRef:
                note_use_slot(EVarRefView{e}.var_slot(),
                              std::string(EVarRefView{e}.name()), line);
                note_reborrow_alias_uses(std::string(EVarRefView{e}.name()), line);  // G0
                break;
            case Code::AddrOf:
                note_use(std::string(EAddrOfView{e}.var_name()), line);
                note_reborrow_alias_uses(std::string(EAddrOfView{e}.var_name()), line);  // G0
                break;
            case Code::AddrOfTemp:
                scan_uses_expr(EAddrOfTempView{e}.inner(), line);
                break;
            case Code::Unary:
                scan_uses_expr(EUnaryView{e}.operand(), line);
                break;
            case Code::Deref:
                scan_uses_expr(EDerefView{e}.operand(), line);
                break;
            case Code::Cast:
                scan_uses_expr(ECastView{e}.operand(), line);
                break;
            case Code::Try:
                scan_uses_expr(ETryView{e}.inner(), line);
                break;
            case Code::FieldRead:
                scan_uses_expr(EFieldReadView{e}.receiver(), line);
                break;
            case Code::TupleIndex:
                scan_uses_expr(ETupleIndexView{e}.receiver(), line);
                break;
            case Code::SliceLen:
                scan_uses_expr(ESliceLenView{e}.slice(), line);
                break;
            case Code::SlicePtr:
                scan_uses_expr(ESlicePtrView{e}.slice(), line);
                break;
            case Code::BinOp: {
                EBinOpView v{e};
                scan_uses_expr(v.lhs(), line);
                scan_uses_expr(v.rhs(), line);
                break;
            }
            case Code::IndexRead: {
                EIndexReadView v{e};
                scan_uses_expr(v.receiver(), line);
                scan_uses_expr(v.index(), line);
                break;
            }
            case Code::SliceLit: {
                ESliceLitView v{e};
                scan_uses_expr(v.base(), line);
                scan_uses_expr(v.len(),  line);
                break;
            }
            case Code::SliceIndex: {
                ESliceIndexView v{e};
                scan_uses_expr(v.slice(), line);
                scan_uses_expr(v.index(), line);
                break;
            }
            case Code::IfExpr: {
                EIfExprView v{e};
                scan_uses_expr(v.cond(),     line);
                scan_uses_expr(v.then_val(), line);
                scan_uses_expr(v.else_val(), line);
                break;
            }
            case Code::MatchExpr: {
                EMatchExprView v{e};
                scan_uses_expr(v.scrut(), line);
                v.each_arm([&](EMatchArmRef arm) {
                    if (auto g = arm.guard()) scan_uses_expr(g, line);
                    scan_uses_expr(arm.value(), line);
                });
                break;
            }
            case Code::BlockExpr: {
                EBlockExprView v{e};
                if (auto br = v.block()) scan_uses_block(br);
                if (auto r = v.result()) scan_uses_expr(r, line);
                break;
            }
            case Code::Call:
                ECallView{e}.each_arg([&](ExprRef a){ scan_uses_expr(a, line); });
                break;
            case Code::MethodCall: {
                EMethodCallView v{e};
                scan_uses_expr(v.receiver(), line);
                v.each_arg([&](ExprRef a){ scan_uses_expr(a, line); });
                break;
            }
            case Code::ClosureCall: {
                EClosureCallView v{e};
                scan_uses_expr(v.callee(), line);
                v.each_arg([&](ExprRef a){ scan_uses_expr(a, line); });
                break;
            }
            case Code::FnPtrCall: {
                EFnPtrCallView v{e};
                scan_uses_expr(v.callee(), line);
                v.each_arg([&](ExprRef a){ scan_uses_expr(a, line); });
                break;
            }
            case Code::FormatCall: {
                EFormatCallView v{e};
                scan_uses_expr(v.fmt(), line);
                v.each_arg([&](ExprRef a){ scan_uses_expr(a, line); });
                break;
            }
            case Code::StructLit:
                EStructLitView{e}.each_field_value([&](ExprRef fv){ scan_uses_expr(fv, line); });
                break;
            case Code::ArrLit:
                EArrLitView{e}.each_elem([&](ExprRef el){ scan_uses_expr(el, line); });
                break;
            case Code::TupleLit:
                ETupleLitView{e}.each_elem([&](ExprRef el){ scan_uses_expr(el, line); });
                break;
            case Code::EnumLitData:
                EEnumLitDataView{e}.each_payload([&](ExprRef pl){ scan_uses_expr(pl, line); });
                break;
            case Code::ClosureBox:
                EClosureBoxView{e}.each_capture_name([&](std::string_view cap){
                    note_use(std::string(cap), line);
                });
                break;
            case Code::PtrArith: {
                EPtrArithView v{e};
                scan_uses_expr(v.ptr(),    line);
                scan_uses_expr(v.offset(), line);
                break;
            }
            case Code::PtrDiff: {
                EPtrDiffView v{e};
                scan_uses_expr(v.lhs(), line);
                scan_uses_expr(v.rhs(), line);
                break;
            }
            default:
                break;
        }
    }

    void scan_uses_stmt(lir_view::StmtRef sr) {
        using namespace lir_view;
        using Code = lir_schema::stmt::Code;
        if (!sr) return;
        uint64_t ln = stmt_point(sr);   // #75: (line, ordinal), not the line
        switch (sr.kind()) {
            case Code::Let: {
                SLetView lv{sr};
                if (is_cond_move_field_drop_temp(sr, lv.name(), lv.value())) break;  // #121
                prescan_reborrow(std::string(lv.name()), lv.type(prog_.type_pool.impl()),
                                 lv.value());                        // G0
                prescan_fnptr(std::string(lv.name()), lv.type(prog_.type_pool.impl()),
                              lv.value());                           // G1
                scan_uses_expr(lv.value(), ln);
                break;
            }
            case Code::Assign: {
                SAssignView av{sr};
                prescan_reborrow(std::string(av.name()), TypeRef(nullptr),
                                 av.value());                        // G0
                prescan_fnptr(std::string(av.name()), TypeRef(nullptr),
                              av.value());                           // G1
                scan_uses_expr(av.value(), ln);
                break;
            }
            case Code::Return:
                scan_uses_expr(SReturnView{sr}.value(), ln);
                break;
            case Code::ExprStmt:
                scan_uses_expr(SExprStmtView{sr}.expr(), ln);
                break;
            case Code::FieldWrite: {
                SFieldWriteView v{sr};
                note_use(std::string(v.receiver()), ln);
                if (!std::string(v.receiver()).empty() && !std::string(v.field()).empty())
                    prescan_reborrow_place(std::string(v.receiver()) + "." +
                                           std::string(v.field()), v.value());   // G0
                scan_uses_expr(v.value(), ln);
                break;
            }
            case Code::IndexWrite: {
                SIndexWriteView v{sr};
                note_use(std::string(v.arr()), ln);
                scan_uses_expr(v.index(), ln);
                scan_uses_expr(v.value(), ln);
                break;
            }
            case Code::FieldIndexWrite: {
                SFieldIndexWriteView v{sr};
                note_use(std::string(v.receiver()), ln);
                scan_uses_expr(v.index(), ln);
                scan_uses_expr(v.value(), ln);
                break;
            }
            case Code::ChainFieldWrite: {
                SChainFieldWriteView v{sr};
                note_use(std::string(v.receiver()), ln);
                scan_uses_expr(v.value(), ln);
                break;
            }
            case Code::DerefFieldWrite: {
                SDerefFieldWriteView v{sr};
                note_use(std::string(v.receiver()), ln);
                scan_uses_expr(v.value(), ln);
                break;
            }
            case Code::DerefWrite: {
                SDerefWriteView v{sr};
                scan_uses_expr(v.ptr(),   ln);
                scan_uses_expr(v.value(), ln);
                break;
            }
            case Code::TupleWrite: {
                STupleWriteView v{sr};
                note_use(std::string(v.receiver()), ln);
                scan_uses_expr(v.value(), ln);
                break;
            }
            case Code::If: {
                SIfView v{sr};
                scan_uses_expr(v.cond(), ln);
                if (auto b = v.then_block()) scan_uses_block(b);
                if (auto b = v.else_block()) scan_uses_block(b);
                break;
            }
            case Code::While: {
                SWhileView v{sr};
                scan_uses_expr(v.cond(), ln);
                if (auto b = v.body()) scan_uses_block(b);
                break;
            }
            case Code::For: {
                SForView v{sr};
                scan_uses_expr(v.lo(), ln);
                scan_uses_expr(v.hi(), ln);
                if (auto b = v.body()) scan_uses_block(b);
                break;
            }
            case Code::Loop:
                if (auto b = SLoopView{sr}.body()) scan_uses_block(b);
                break;
            case Code::Block:
                if (auto b = SBlockView{sr}.body()) scan_uses_block(b);
                break;
            case Code::ForEach: {
                SForEachView v{sr};
                scan_uses_expr(v.iter(), ln);
                if (auto b = v.body()) scan_uses_block(b);
                break;
            }
            case Code::Match: {
                SMatchView v{sr};
                scan_uses_expr(v.scrut(), ln);
                v.each_arm([&](EMatchArmRef arm) {
                    if (auto g = arm.guard()) scan_uses_expr(g, ln);
                    if (auto b = arm.body()) scan_uses_block(b);
                });
                break;
            }
            case Code::LetElse: {
                SLetElseView v{sr};
                scan_uses_expr(v.scrut(), ln);
                if (auto b = v.else_block()) scan_uses_block(b);
                break;
            }
            case Code::Break:
                scan_uses_expr(SBreakView{sr}.value(), ln);
                break;
            default:
                break;
        }
    }

    void scan_uses_block(lir_view::BlockRef br) {
        br.each_stmt([&](lir_view::StmtRef sr) { scan_uses_stmt(sr); });
    }

    // Phase 9 (NLL): release borrows whose holder is no longer live.
    // Called after each statement in a block: if holder's max-use line is at or
    // before the current statement, the borrow has expired textually.
    // ⚠ A LOAN IS RELEASED ONLY BY release_dead_borrows, KEYED ON THE
    // HOLDER'S LAST USE. An assignment to the holder is a DIFFERENT event: the
    // reference is destroyed, not merely dead. Nothing killed the loan there,
    // so `let mut sl: &mut [i64] = &mut a; sl = &mut a;` conflicted with the
    // loan `sl` itself was holding.
    //
    // MUTABLE LOANS ONLY, and that bound is MEASURED, not chosen: a shared
    // loan is a COUNTER, merge_loans raises counters across a loop back edge
    // with no BorrowRecord that can release them (its own comment says so), so
    // erasing the record while the merged counter survives strands the count
    // — pass/memoria_ctr_cursor_nav went red on exactly that. Releasing
    // strictly less is the safe half; the shared half is a separate arc.
    void release_borrows_held_by(const std::string& holder_name) {
        uint32_t hslot = slot_of_binding(holder_name);
        auto same = [&](const std::string& h, uint32_t hs) {
            if (h != holder_name) return false;
            if (hslot != NO_SLOT && hs != NO_SLOT) return hs == hslot;
            return true;
        };
        // ⚠ THE HOLDER FIELD IS NOT THE SET OF BINDINGS THAT NAME THE LOAN.
        // `let t: &mut [i64] = sl;` copies the reference and records NO
        // co-holder, so a kill keyed on the holder alone releases a loan `t`
        // still relies on — a LEAK, measured and then closed by this guard.
        // ref_borrow_sources_ is the map that DOES know.
        auto named_elsewhere = [&](const std::string& target) {
            for (auto& [pl, srcs] : ref_borrow_sources_) {
                if (place_under(pl, holder_name)) continue;
                for (auto& s : srcs) if (s.name == target) return true;
            }
            return false;
        };
        for (auto& frame : scopes_) {
            auto it = frame.borrows.begin();
            while (it != frame.borrows.end()) {
                // CEILING PROBE `holderkill_keep` — MEASURED 2026-08-27: the
                // suppression fired 3 times across the 447 ledger compiles,
                // ceiling 0. THE FIRE COUNT IS THE ANSWER, and nothing in the
                // tree measured it before: this event-keyed releaser actually
                // releases a loan THREE times in the whole acceptance
                // population. Its name-equality guards cannot be class B's
                // home — there is no population behind them.
                //
                // The 2026-08-27 coverage map agrees and sharpens: 1,138
                // iterations of this loop, and the LAST conjunct below (the
                // probe itself) is reached 5 times in 8060 runs.
                // (docs/coverage §E prints 1,138 for this probe; that is the
                // loop, not the site.)
                //
                // The hypothesis was: every guard above is a
                // NAME test; a surviving user that reaches the target through
                // a PROJECTION passes `named_elsewhere` and the loan is
                // dropped while live. Last conjunct: a fire is one SUPPRESSED
                // release, so the count is this releaser's whole population.
                if (it->is_mut && same(it->holder, it->holder_slot) &&
                    it->co_holders.empty() && !named_elsewhere(it->target) &&
                    !logos::probe::on("holderkill_keep")) {
                    if (auto sit = var_find(it->target_slot, it->target); sit != nullptr)
                        sit->mut_borrowed = false;
                    it = frame.borrows.erase(it);
                } else { ++it; }
            }
            auto fit = frame.field_borrows.begin();
            while (fit != frame.field_borrows.end()) {
                if (fit->is_mut && same(fit->holder, fit->holder_slot) &&
                    fit->co_holders.empty() && !named_elsewhere(fit->target)) {
                    if (auto sit = var_find(fit->target_slot, fit->target); sit != nullptr)
                        sit->mut_field_borrows.erase(fit->path);
                    fit = frame.field_borrows.erase(fit);
                } else { ++fit; }
            }
        }
    }

    void release_dead_borrows(uint64_t cur_line) {
        if (scopes_.empty()) return;
        if (std::getenv("LOGOS_DUMP_BC_RELEASE")) {
            std::fprintf(stderr, "[bc-release] cur_line=%llu frames=%zu\n",
                         (unsigned long long)cur_line, scopes_.size());
            for (size_t fi = 0; fi < scopes_.size(); ++fi)
                for (auto& b : scopes_[fi].borrows)
                    std::fprintf(stderr,
                        "[bc-release]   frame=%zu%s target=%s holder=%s "
                        "is_mut=%d lu=%llu\n", fi,
                        fi + 1 == scopes_.size() ? "(back)" : "",
                        b.target.c_str(), b.holder.c_str(), (int)b.is_mut,
                        (unsigned long long)holders_last_use(b));
        }
        // ⚠ D3: THIS SWEPT `scopes_.back()` ONLY, and a loan's frame is where it
        // was RECORDED, not where it dies. `let r = &mut n; { *r = 1; let z = n; }`
        // records on the fn frame, enters the block, and every release inside the
        // block sweeps the INNER frame — so the loan survived its own holder's
        // last use and the read refused, while the byte-identical statements with
        // NO nested block compiled. MEASURED with LOGOS_DUMP_BC_RELEASE:
        //     cur_line=…82 frames=3   frame=1 target=n holder=r is_mut=1 lu=…81
        // lu <= cur_line and the record is one frame short of the sweep.
        //
        // Sweeping OUTWARD is sound because the release predicate is not
        // frame-local: `holders_last_use` reads the SLOT-keyed last-use map, so a
        // record answers the same question from any frame, and shadowing is
        // carried by the record's own holder_slot. It releases strictly MORE, so
        // the risk is all in the over-release direction; the guard set is
        // bc_d3_*_refuse — a holder used after the block, two overlapping loans in
        // different frames, and an outer loan conflicting inside the block.
        // ⚠ BOUNDED BY THE LOOP, and the bound already had a name: a LoopFrame
        // records `outer_scope_count` = "frames that survive the loop". Inside a
        // loop body, a holder's TEXTUAL last use says nothing about the BACK
        // EDGE — `prm` is used at line 3060 and the body ends at 3065, but
        // iteration 2 uses it again. Sweeping past `outer_scope_count` retired
        // the loans `srcb`/`dsrcb`/`ssrcb`/`szatb` held by `prm` at the body's
        // last statement, and liblogos-mem then failed to build with five
        // over-refusals in `register_native_rels` (MEASURED). Frames the loop
        // survives are the loop's own business; frames the loop OWNS are swept.
        // The outward walk crosses BARE `{ … }` STATEMENT frames ONLY, and stops
        // one frame past the outermost of them. Two measurements bound it:
        //   · loop bodies — `prm`'s TEXTUAL last use is line 3060 and the body
        //     ends at 3065, but iteration 2 uses it again. Sweeping past the
        //     loop retired the loans held by `prm` and liblogos-mem failed to
        //     build with five over-refusals in `register_native_rels`.
        //   · `if`/`match`-arm frames — same shape one level down; sweeping
        //     those broke `bf_leaf_absorb` and eight more with "'a' has shared
        //     borrows".
        // Both were MEASURED, both are the over-refusal direction, and both are
        // outside D3's property, which is a BARE nested block and nothing else.
        size_t lo = scopes_.size() - 1;
        while (lo > 0 && scopes_[lo].bare_block) --lo;
        for (size_t _fi = lo; _fi < scopes_.size(); ++_fi) {
        auto& frame = scopes_[_fi];
        auto it = frame.borrows.begin();
        while (it != frame.borrows.end()) {
            if (it->holder.empty()) { ++it; continue; }
            uint64_t lu = holders_last_use(*it);
            // ⚠ TWO SITES, ONE NAME. `nll_lu_zero` guards this whole-borrow
            // loop AND the field-borrow loop below; `nll_lu_strict` likewise.
            // probe::on aggregates by NAME, so a fire count cannot separate
            // them. Per the 2026-08-27 map over 8060 runs: nll_lu_zero 84,202
            // here / 1,655 there (`lu == 0` true 1,414 / 12); nll_lu_strict
            // 83,909 here / 1,655 there. Rename per site BEFORE pricing either.
            // CEILING PROBE `nll_lu_zero` — one_holder_last_use returns 0 for
            // a holder note_use_slot never recorded (a projection place, a
            // synthesised temp), and `0 <= cur_line` retires the loan at the
            // very first sweep. Treat lu==0 as "never expires".
            if (lu == 0 && logos::probe::on("nll_lu_zero")) { ++it; continue; }
            // Dropck liveness: the holder is used once more, at its drop.
            if (holder_drops_after_last_use(*it)) { ++it; continue; }
            // CEILING PROBE `nll_lu_strict` — `<=` retires a loan whose holder
            // died ON the statement just visited, before the rest of that same
            // statement (or line) can conflict with it.
            if (logos::probe::on("nll_lu_strict") ? (lu != 0 && lu < cur_line)
                                                  : (lu <= cur_line)) {
                auto sit = var_find(it->target_slot, it->target);
                if (sit != nullptr) {
                    if (it->is_mut) sit->mut_borrowed = false;
                    else if (sit->shared_borrows > 0)
                        --sit->shared_borrows;
                }
                it = frame.borrows.erase(it);
            } else {
                ++it;
            }
        }
        // Field-path borrows with a holder release the same way (NLL).
        auto fit2 = frame.field_borrows.begin();
        while (fit2 != frame.field_borrows.end()) {
            if (fit2->holder.empty()) { ++fit2; continue; }
            uint64_t lu = holders_last_use(*fit2);
            if (lu == 0 && logos::probe::on("nll_lu_zero")) { ++fit2; continue; }
            // CEILING PROBE `fldnlldrop` — the byte-identical guard in the
            // whole-borrow loop above fires 293 of 84,202; here it fires 0 of
            // 1,655. No field borrow in the population is held by a
            // drop-observing holder, so the field loop's dropck-liveness rule
            // is untested. Force it: field loans survive to pop_scope instead
            // of retiring at the holder's last use — strictly fewer releases.
            // ⛔ STOP SIGN — MEASURED 2026-08-28: 19 fires across the 400
            // ledger compiles, CEILING 0 vs COST 13 legal programs refused
            // (bc_nll_d2_field_holder{,_mut}, nll-disjoint-fields,
            // nll-borrow-of-field-disjoint, bc_d6_mut_field_capture_nll,
            // bc_d8_match_scrutinee_disjoint, zone_mut_thin_source, spec
            // borrow_2, …). The WHOLE "field NLL retires too early" axis is
            // priced and declined in one run: field-precise loans that outlive
            // their holder's last use break the disjoint-field corpus wholesale.
            // What is still unexplained, and is NOT bought by this: why
            // drop-observability is structurally unreachable for field holders
            // (293 of 84,202 in the whole-borrow loop, 0 of 1,655 here).
            if (logos::probe::on("fldnlldrop") ||
                holder_drops_after_last_use(*fit2)) { ++fit2; continue; }
            if (logos::probe::on("nll_lu_strict") ? (lu != 0 && lu < cur_line)
                                                  : (lu <= cur_line)) {
                if (auto sit = var_find(fit2->target_slot, fit2->target); sit != nullptr) {
                    if (fit2->is_mut)
                        sit->mut_field_borrows.erase(fit2->path);
                    else if (auto sb = sit->shared_field_borrows.find(fit2->path);
                             sb != sit->shared_field_borrows.end() && sb->second > 0)
                        --sb->second;
                }
                fit2 = frame.field_borrows.erase(fit2);
            } else {
                ++fit2;
            }
        }
        }
    }

    // ── THE NLL WALK ────────────────────────────────────────────────────────
    // One statement walk with the release cursor, shared by every block-shaped
    // site. A COMPOUND statement (while/if/block) spans past its start line — a
    // holder whose last use sits INSIDE the body (`while … { o[k] = …; }` then
    // `self.mutate()`) must count as expired once the whole statement has been
    // visited. Release against the max line inside the just-visited statement's
    // SUBTREE (tracked via max_line_seen_, reset per statement), folded
    // monotonically across this block. NOT a global max: sema emits
    // out-of-line-order shapes (`unsafe { …; return x; }` becomes a Return stmt
    // whose line is the LAST line, wrapping a BlockExpr of the earlier ones) —
    // a global max would pre-release every borrow inside such a block.
    //
    // defer_release — the caller wants NO per-statement release, only the fold:
    // a value block with an escaping holder (see visit_block) and the
    // transparent-destructuring wrapper both release once, at the end.
    //
    // ⚠ D1: visit_loop_body walked its body with a bare `each_stmt` and no
    // cursor at all, so NO loop body in the language had intra-body NLL — a
    // loan raised in the body survived to the body's `}` and was retired only
    // lexically by pop_scope. `while … { let r = &mut n; *r = *r + 1; acc = acc
    // + n; }` refused while THE IDENTICAL BODY under `if` compiled. Both passes
    // walk through here now; pass 1 is the dry run, and releasing there too is
    // what keeps post1_s/post2_s agreeing about which counters reach the back
    // edge.
    uint64_t walk_stmts_releasing(lir_view::BlockRef br, bool defer_release) {
        uint64_t cursor = 0;
        br.each_stmt([&](lir_view::StmtRef sr) {
            uint64_t saved = max_line_seen_;
            max_line_seen_ = stmt_point(sr);   // #75
            visit_stmt(sr);
            cursor = std::max(cursor, max_line_seen_);
            max_line_seen_ = std::max(saved, max_line_seen_);
            if (!defer_release) release_dead_borrows(cursor);
        });
        return cursor;
    }

    // esc_result/esc_holder — Door B, the block-as-VALUE shape. `let b: B = {
    // let t: B = c.mk(); t };` records the loan with holder `t`, declared in
    // THIS frame; the result then escapes to `b` in the enclosing one. The
    // BlockExpr arm of take_ref_borrows ran its hop AFTER visit_block returned,
    // i.e. after pop_scope had already released the loan, so nothing was left
    // to inherit. Doing the hop before the pop lets `b` join the holder set,
    // which is exactly what makes the record escape the frame.
    void visit_block(lir_view::BlockRef br, lir_view::ExprRef esc_result = {},
                     const std::string& esc_holder = {}) {
        push_scope();
        // Door B, value-block: with an escaping result the per-statement NLL
        // release is what kills the loan first — `let b: B = { let t: B =
        // c.mk(); t };` is ONE line, so `t`'s last use is not past the `let t`
        // statement and the loan is released before the result can hand it on.
        // Inside a value block with a holder, defer release to the pop; the pop
        // then releases the frame-local loans exactly as the lexical rule would
        // and re-homes the escaping ones.
        uint64_t cursor = walk_stmts_releasing(br, /*defer_release=*/!esc_holder.empty());
        // ── D-b: THE TAIL EXPRESSION IS READ AFTER THE FRAME RETIRED ────────
        // `let r: &i64 = { let t = mk(); &t.n };` admitted while the SAME
        // borrow ASSIGNED to an outer variable inside the block refused — one
        // property apart (`db_iso_tail_assign`, tail + assign, also admitted,
        // so it is the tail POSITION and not the `let`). §B6's own record for
        // the binding is made by `record_ref_sources` at the enclosing `let`,
        // i.e. after this frame popped: `collect_ref_sources_paths`' AddrOf /
        // AddrOfTemp arms are guarded by `var_has`, `t` is no longer a live
        // variable, and NOTHING is recorded — proved, not inferred, by the n7
        // pair (`[retgate] … srcs=[]` for the tail vs `srcs=[t,]` for the
        // fn-scope twin, both channels flipping together on one property).
        //
        // THIS IS DOOR B'S OWN BUG IN A SECOND CHANNEL, and the comment above
        // says so in the loan channel's words: "the hop ran AFTER visit_block
        // returned, i.e. after pop_scope had already released the loan". The
        // §B6 answer has to be taken at the same point, so it is taken here,
        // beside the hop, over the frame this pop is about to erase. NOT a new
        // deposit door: no map is written, the diagnostic is the one
        // `check_live` already prints for E0597, and the population is
        // `collect_ref_sources`, unchanged.
        //
        // Reported at the block rather than deferred to a later USE (which is
        // what `dangling_` buys elsewhere): a value block's result escapes by
        // construction, and rustc reports this shape whether or not the binding
        // is ever read. Gated on a holder, so the plain statement-block arm of
        // `visit` — which passes none — is untouched.
        //
        // ⚠ KEYED ON THE SLOT, NOT THE NAME (F5). The first cut of this check
        // compared `tail_srcs` against `scopes_.back().declared` as STRINGS,
        // and a shadowed name in the inner block produced a false E0597 —
        // measured on a one-property pair whose only difference is the inner
        // local's name (`o` vs `zz`). `dying_binding` is the shared predicate;
        // pop_scope's `dangling_` deposit had the same defect and now reads it
        // too, so there is one rule and not two.
        if (esc_result && !esc_holder.empty()) {
            std::vector<std::pair<std::string, RefSrc>> tail_srcs;
            collect_ref_sources_paths(esc_result, std::string{}, tail_srcs);
            for (auto& [sub, s] : tail_srcs)
                if (dying_binding(scopes_.back(), s)) {
                    report(cursor ? (uint32_t)cursor : 0, std::format(
                        "'{}' does not live long enough: it is borrowed by '{}', "
                        "which is used here after '{}' goes out of scope (E0597)",
                        s.name, esc_holder, s.name));
                    break;
                }
        }
        if (esc_result && !esc_holder.empty() &&
            is_borrow_carrying_type(esc_result.type(prog_.type_pool.impl()))) {
            std::vector<std::string> roots;
            bc_hop_roots(esc_result, roots);
            for (auto& r : roots) inherit_loans(r, esc_holder, cursor);
        }
        pending_esc_holder_ = esc_holder;
        pop_scope();
        pending_esc_holder_.clear();
    }

    // Analyse a loop body: outer variables moved/borrowed inside are propagated.
    // loop_vars are local to the loop iteration.
    // var_loan_roots — Door B, the for-each shape. The ITERATION BINDING is an
    // extraction from the iterated container (`for e in vs.iter()` with
    // vs: Vec<B>): whatever loans the container's holder bindings hold are
    // reachable through `e`, exactly as for a match arm binding
    // (propagate_pat_loans) or a field read. Without it `b = *e` inside the
    // body hopped from a binding that held nothing and the loan died with the
    // container's last use — the `for` line — while `b` lived on.
    void visit_loop_body(lir_view::BlockRef body,
                         const std::vector<std::string>& loop_vars = {},
                         std::string_view label = {},
                         const std::vector<std::string>& var_loan_roots = {},
                         std::string_view break_slot = {}) {
        auto seed_loop_var_loans = [&]() {
            if (loop_vars.empty()) return;
            for (auto& r : var_loan_roots) inherit_loans(r, loop_vars.front(), 0);
        };
        auto pre_s = states_;
        auto pre_p = prov_;

        auto pre_rbs  = ref_borrow_sources_;
        auto pre_rbl  = ref_borrow_line_;
        auto pre_dang = dangling_;

        // ── Pass 1 (dry run, diagnostics suppressed): recompute the move-state
        //    that reaches the loop's back edge so pass 2 can seed iteration 2+
        //    correctly. Reporting here would duplicate every in-body error.
        loop_stack_.push_back(LoopFrame{std::string(label), {}, {}, std::string(break_slot)});
        loop_stack_.back().outer_scope_count = scopes_.size();   // r11: frames that survive the loop
        bool saved_sup = suppress_reports_;
        suppress_reports_ = true;
        push_scope();
        for (auto& v : loop_vars) declare_var(v);
        seed_loop_var_loans();
        bool saved_div = cur_diverged_;
        cur_diverged_ = false;
        walk_stmts_releasing(body, /*defer_release=*/false);
        bool bottom_reachable = !cur_diverged_;  // fall-through reaches the back edge?
        cur_diverged_ = saved_div;
        pop_scope();
        suppress_reports_ = saved_sup;
        auto post1_s = states_;
        LoopFrame frame1 = std::move(loop_stack_.back());
        loop_stack_.pop_back();

        // Back-edge entry state = pre-loop state + moves of OUTER bindings that
        // reach the back edge (fall-through bottom or a `continue` targeting
        // this loop). loop_propagate_moves keys off `pre_s`, so loop-locals
        // (absent from pre_s) are never seeded moved, and an outer binding
        // re-declared in the body clears naturally when pass 2 re-declares it.
        // J0 at the BACK EDGE: a loan raised on iteration 1 — at the body's
        // bottom or on a `continue` path — is still held when iteration 2
        // re-enters the body, so a use of the referent ABOVE the raise must be
        // refused. Only the move channel crossed here; the loan counters did
        // not, so `while … { c.bump(); if p { vs.push(c.mk()); continue; } }`
        // was admitted. merge_loans is residency-guarded by construction (it
        // skips any binding absent from the base), so a loop-local holder —
        // declared inside the body, absent from pre_s — contributes nothing.
        //
        // The FALL-THROUGH arm crosses whatever `post1_s` carries, which today
        // is less than the truth: pop_scope's re-home step is gated on
        // `!suppress_reports_`, so in pass 1 (the dry run) a loan whose holder
        // outlives the body is RELEASED at the body's `}` and post1_s comes
        // back with the counters down. `while … { c.bump(); vs.push(c.mk()); }`
        // is therefore still admitted. Ungating the re-home (measured) reds
        // that witness and keeps the corpus at 2048/2048, but stdlib `mem`
        // stops compiling — `plan_walker.walk_program_params` keeps a `str`
        // into `src_bufs[i]` from iteration 1 while iteration 2 pushes into
        // `src_bufs[i+1]`, which the element-insensitive model cannot tell
        // apart. That is a separate rule (pass-1 loan lifetime) with a real
        // consumer to fix first, not a hedge to fold in here.
        //
        // ⚠ MEASURED, and NOT what an earlier draft of this comment claimed.
        // The claim was "the `continue` arm here and the one at the loop-exit
        // collector below are MUTUALLY REDUNDANT for every witness: reverting
        // either ALONE leaves all four continue fixtures refusing". A
        // one-at-a-time control revert (two builds, restored and green between)
        // REFUTES that for three of the four. The joins are distinguished by
        // WHERE the offending use sits, not by which keyword reached them:
        //
        //   frame1 arm only reverted (X0a)  → x0_backedge_continue ADMITS.
        //       Its `c.bump()` is INSIDE the loop, above the raise, so only the
        //       back edge can carry iteration 1's loan to it.
        //   frame2 arms only reverted (X0b) → x0_nested_if_break and
        //       x0_nested_if_break_nocallee ADMIT. Their use is AFTER the loop
        //       and the loan leaves via `break`, which post2_s never holds.
        //   BOTH reverted (X0)              → the above three PLUS
        //       x0_nested_if_continue, which is the ONLY genuinely redundant
        //       witness: a `continue` path reaches the after-loop state through
        //       either join, so it needs the pair to go permissive.
        //
        // So each arm has its OWN sole witness and neither is a hedge; the
        // redundancy is real but confined to one fixture, not to "all four".
        StateMap back_edge = pre_s;
        if (bottom_reachable) {
            loop_propagate_moves(back_edge, post1_s, pre_s);
            merge_loans(back_edge, post1_s);
        }
        for (auto& cs : frame1.continue_states) {
            loop_propagate_moves(back_edge, cs, pre_s);
            merge_loans(back_edge, cs);
        }

        // ── Pass 2 (authoritative): analyse the body from the state that holds
        //    on entry to EVERY iteration (pre-loop joined with the back edge —
        //    `back_edge` already subsumes pre_s's moves). A value an outer
        //    binding was moved into on an earlier iteration is now moved on
        //    entry, so its reuse fires the normal use-after-move diagnostic
        //    (Rust E0382, "value moved here, in previous iteration of loop").
        //    Restore the pre-loop borrow accumulators so pass 1 doesn't leak in.
        states_ = back_edge;
        prov_   = pre_p;
        ref_borrow_sources_ = pre_rbs;
        ref_borrow_line_    = pre_rbl;
        dangling_           = pre_dang;
        loop_stack_.push_back(LoopFrame{std::string(label), {}, {}, std::string(break_slot)});
        loop_stack_.back().outer_scope_count = scopes_.size();   // r11: frames that survive the loop
        cur_diverged_ = false;
        push_scope();
        for (auto& v : loop_vars) declare_var(v);
        seed_loop_var_loans();
        walk_stmts_releasing(body, /*defer_release=*/false);
        cur_diverged_ = saved_div;
        pop_scope();
        auto post2_s = states_;
        auto post2_p = prov_;
        LoopFrame frame2 = std::move(loop_stack_.back());
        loop_stack_.pop_back();

        // ── After-loop state: an outer binding moved on ANY path that leaves
        //    the loop body — fall-through / continue to the back edge, or a
        //    `break` out — is dead after the loop. The pre-fix code only
        //    propagated fall-through moves, so a move on a break/continue path
        //    was forgotten and the value looked live again after the loop.
        //    Provenance merges conservatively (the loop may run 0+ times).
        states_ = pre_s;
        prov_   = pre_p;
        merge_loans(states_, post2_s);   // J0: every raised counter crosses
        if (bottom_reachable) loop_propagate_moves(states_, post2_s, pre_s);
        // J0 at the LOOP-EXIT collectors: `post2_s` is the fall-through state
        // only. A loan raised on a path that leaves via `break`/`continue` is
        // absent from it — twice over when the exit sits in a nested `if`,
        // because the if-join is skipped on a diverging arm. These two
        // collectors carried the move channel and not the loan channel, so
        // `while … { if p { vs.push(c.mk()); break; } }` was admitted.
        for (auto& cs : frame2.continue_states) {
            loop_propagate_moves(states_, cs, pre_s);
            merge_loans(states_, cs);
        }
        for (auto& bs : frame2.break_states) {
            loop_propagate_moves(states_, bs, pre_s);
            merge_loans(states_, bs);
        }
        merge_provs(prov_, post2_p);
    }

    // ── D1 round 2, Door A: a PLACE WRITE records the loan ─────────────────
    // `w.b = c.mk()` STORES a borrow-carrying value into `w` exactly as
    // `b = c.mk()` stores it into `b`. Code::Assign routes its RHS through
    // take_ref_borrows with holder = the assigned binding; the seven other
    // place-write statement kinds (FieldWrite / TupleWrite / IndexWrite /
    // FieldIndexWrite / ChainFieldWrite / DerefFieldWrite / DerefWrite) only
    // ran a consuming visit, so no loan was ever recorded and the referent
    // could be mutated on the next line (`w.b = c.mk(); c.bump(); *w.b.p`).
    //
    // The rule: the destination place's ROOT binding becomes the holder. A
    // write to `w.b` / `t.0` / `a[i]` / `o.i.b` makes the stored borrow
    // reachable through the whole root, so the root's lifetime is the loan's.
    // This is Assign's routing, one level of place indirection out.
    //
    // Through a REFERENCE root (`(*r).b = …`, r: &mut Wrap) the root binding
    // is the reference itself, whose own last use IS the write statement — NLL
    // would retire the loan immediately. The §B6 source map already records
    // what `r` was formed from, so the referent joins as a co-holder
    // (through_ref); inheritance only ever EXTENDS a loan's life.
    //
    // record_only: every caller has already run its own consuming visit of the
    // value (move tracking); a second visit would report a double move.
    // Root of a written PLACE, for Door A's DerefWrite spelling. Deliberately
    // NOT extract_borrow_place: that one is shared with the conflict-check
    // pass and has no TupleIndex step, so `t.0 = …` came back with an empty
    // root (measured — A2 recorded nothing while A1/A3/A4 did). This walk is
    // write-destination-only and crosses field / tuple / index / ref-deref
    // steps; a RAW-pointer deref stops it (Rust parity, as everywhere else).
    // `through_ref` reports whether a reference was crossed, so the referent
    // can co-hold (see place_write_loans).
    std::string place_write_root(lir_view::ExprRef e, bool& through_ref,
                                 bool resolve = true) const {
        using namespace lir_view;
        using Code = lir_schema::expr::Code;
        const auto* pool = prog_.type_pool.impl();
        through_ref = false;
        ExprRef cur = e;
        // G0: the dotted path of FIELD steps, outermost first, collected only
        // while EVERY step so far is a field read — a field hanging off an
        // index or a deref has no name this map could ever have recorded.
        std::vector<std::string> path_fields;
        bool path_ok = true;
        while (cur) {
            Code k = cur.kind();
            if (k == Code::FieldRead)       { if (path_ok) path_fields.emplace_back(EFieldReadView{cur}.field());
                                              cur = EFieldReadView{cur}.receiver();  continue; }
            if (k == Code::TupleIndex)      { path_ok = false; cur = ETupleIndexView{cur}.receiver(); continue; }
            if (k == Code::IndexRead)       { path_ok = false; cur = EIndexReadView{cur}.receiver();  continue; }
            if (k == Code::SliceIndex)      { path_ok = false; cur = ESliceIndexView{cur}.slice();    continue; }
            if (k == Code::Deref) {
                auto op = EDerefView{cur}.operand();
                if (!op) break;
                auto ot = op.type(pool);
                if (ot && ot.kind() == LogosType::Kind::Ptr) return {};  // raw: unchecked
                // G0: a REFERENCE deref is the identity on places — `(*h.r).x`
                // and `h.r.x` name the same slot, and the auto-deref inserted
                // for `h.r.push(…)` (receiver `&mut Vec<B>`, self `&mut Vec<B>`)
                // is exactly this step. It must NOT invalidate the dotted path,
                // or the one spelling the defect actually uses is skipped.
                // Index / tuple-index steps above DO invalidate it: those are
                // not names this map could have recorded.
                through_ref = true;
                cur = op;
                continue;
            }
            break;
        }
        if (cur && cur.kind() == Code::VarRef) {
            // Round 8: the graph's nodes are dotted PLACES, and this consumer's
            // contract is one VARIABLE (the loan channels key on one). A dotted
            // endpoint names its root — which is what `&mut s.f` answered
            // before the walker learned to spell the place.
            // F0: a RETARGET of a reference-typed place writes the STORAGE
            // that holds the reference, not the referent — the caller says so
            // by asking for the unresolved root.
            std::string ep = resolve
                ? resolve_place_reborrow(
                      std::string(EVarRefView{cur}.name()), path_fields, path_ok)
                : std::string(EVarRefView{cur}.name());
            if (!ep.empty() && !var_has(NO_SLOT, ep)) ep = ref_place_root(ep);
            return ep;
        }
        return {};
    }

    // ── D1 round 5 / H1: LOOK THROUGH A REBORROW LOCAL TO ITS REFERENT ─────
    //
    // THE DEFECT. The walk above terminates at a VarRef and hands back that
    // NAME. For `let r: &mut Vec<B> = &mut vs; r.push(c.mk()); c.bump();
    // *vs.get(0).p` the name is `r`, so the stored loan was recorded as held
    // by `r` — whose own last use is that very statement — and `vs` was left
    // holding nothing. Measured rc=0 (leak admitted) against rc=1 for the
    // one-line-less twin. Independent of any summary: there is no callee at
    // all. Same via the struct spelling `let r: &mut L3 = &mut k; r.v.push(…)`.
    //
    // THE CONCEPT (shared with the summarizer's note_alias/each_root). A local
    // holding a `&mut` REBORROW is not a second place correlated with its
    // referent — it IS the referent's place, so a write through it is a write
    // to the referent and the referent is the holder.
    //
    // ⚠ WHY NOT prov_ / ref_borrow_sources_, WHICH ALREADY EXIST. MEASURED AND
    // REJECTED. Both answer a DIFFERENT question — "which locals may this
    // binding's value have borrowed from", the §B6 escape channel — and they
    // answer it for every borrow-carrying binding, not only for `&mut`
    // reborrows. Routing the re-home through them fired 134 times on a plain
    // stdlib compile and broke the verdict in BOTH directions at once:
    //   • PERMISSIVE — fail/bc_d1r2_place_write_field_held and
    //     fail/bc_d1r3_f6_place_write_use_after_mut started COMPILING, because
    //     `let mut w: Wrap = Wrap { b: B { p: &z } };` has the single source
    //     `z`, so `w.b = c.mk()`'s loan was re-homed onto `z` — a plain i64
    //     local whose last use is long past — and retired on the spot;
    //   • OVER-REFUSING — pass/bc_d1_container_last_use_admits and
    //     pass/bc_d1r4_n1_field_container_admits started refusing.
    // A `&mut` reborrow is a narrower fact than "borrowed from", so it gets its
    // own recorder, written at the one place the fact exists (the `let`/assign
    // whose DECLARED TYPE is `&mut`) — the exact mirror of the summarizer's
    // note_alias. Guarded controls: h1_callsite_admit stays admitted, and
    // h1b_struct_admit converges onto the verdict its ALIAS-FREE spelling
    // already gives (both refuse — measured on h1b_direct_admit; the alias was
    // the only reason it used to compile).
    // ── D1 round 8: the SHAPE GATE IS GONE ────────────────────────────────
    //
    // What stood here was `reborrow_referent`: a three-kind switch (AddrOf /
    // AddrOfTemp / VarRef, `default: return {}`) written once here, once as
    // `prescan_referent`, and once as the summarizer's `note_alias` gate. U1 is
    // the bill for that: `let mut r2: &mut Vec<B> = h.r;` is a FieldRead, hits
    // the default arm, records nothing, and launders the loan that the inline
    // `h.r.push(…)` refuses. The shapes now come from the one walker
    // (`ref_source_places`); what remains here is this pass's ADMISSIBILITY
    // POLICY, which is the part that was never shared:
    //   • a materialized statement-temporary (`__rtmp_N`) drops at the end of
    //     the statement and has no referent to hold the loan (admit direction);
    //   • the root must be a TRACKED LOCAL of this pass and NOT a param — the
    //     pre-pass deliberately drops both tests (its maps are empty when it
    //     runs), which is why it cannot share this function, only the walker.
    // Applied to the ROOT of a dotted place: `h.r` is admissible iff `h` is.
    bool ref_source_admissible(const std::string& p) const {
        std::string root = ref_place_root(p);
        if (root.empty() || is_materialized_temp_name(root)) return false;
        // D1 round 12 / A1, the READ half of the break-slot deposit. A loop's
        // `__loop_val_N` is synthesized by sema and never appears in a `let`,
        // so `var_has` says no and the slot was dropped AT THE SEED — the
        // deposit recorded at `break` would have been unreachable. It is the
        // exact OPPOSITE of an `__rtmp_N` above: an `__rtmp_N` dies at the end
        // of its statement and has no storage to hold a loan, while the break
        // slot is a real entry alloca (mlir_gen_stmt's gen_loop) that outlives
        // the loop statement and is READ after it, by construction — the
        // BlockExpr wrapper's result is a VarRef to it.
        if (!is_loop_break_slot_name(root)) {
            if (param_names_.count(root)) return false;
            // D1 round 13 / P0c: a place THE GRAPH ITSELF RECORDS is nameable
            // even when no `let` declared its root. `?` desugars to a match
            // whose payload binding (`__try_ok_N`) is synthesized by sema and
            // never declared, so `var_has` says no and the source was dropped
            // — the same shape as the `__loop_val_N` break slot above, and the
            // same answer. This cannot admit an `__rtmp_N` (refused above) nor
            // a param (refused just above), and it cannot invent an edge: the
            // only way into `reborrow_of_` is a recorder that already applied
            // its own gate.
            if (!var_has(NO_SLOT, root) && !reborrow_of_.find(p)) return false;
        }
        return true;
    }
    // ── D1 round 11 / X1: A CALL RESULT IS A PROSPECTIVE REBORROW ──────────
    //
    // THE DEFECT (measured). `let s: &mut Vec<B> = pick(&mut vs); s.push(c.mk());
    // c.bump(); *vs.get(0).p` ADMITS, while the alias-free twin `vs.push(c.mk())`
    // REFUSES. The summary is RIGHT in every witness (`pick` summarises
    // result<-0x1); the loss is here, at the call site: `ref_source_places` is a
    // syntactic walk with no Call arm, so `s` was recorded reborrowing NOTHING
    // and the loan `s` later raised never reached `vs`.
    //
    // The sentence this retires is the one above `ref_source_places` — "what a
    // callee returns is the borrow-flow SUMMARY's question, and answering it
    // from the shape would be a guess". `flows_` now holds the answer, so it
    // stops being a guess: the sources of a call result are the sources of
    // exactly the arguments the callee's `to_result` mask selects, recursively
    // (an argument may itself be a call). Same index convention as the three
    // existing `to_result` call sites — receiver first for a method.
    //
    // STRICTLY ADDITIVE, and that is the whole safety argument: today's answer
    // for a Call is `{}` (the walker has no Call arm), and with no summary —
    // the pre-mono generic pass where `flows_` is null, an extern/metaprog/
    // ExternalRef/indirect callee — the new arm contributes nothing and the
    // syntactic answer stands unchanged. Additive edges push toward
    // OVER-refusal, so every seeded place still goes through
    // `ref_source_admissible` (params and `__rtmp_N` temps dropped AT the seed).
    //
    // ── POPULATION, MEASURED (fire counters inside the arm, removed after; a
    //    tree-wide green means nothing over a branch that never executed) ────
    //   stdlib lang/mem/lcm/std : 675 calls reach the mask, 470 have a summary,
    //       79 of those carry a non-zero `to_result`, and 22 places are SEEDED.
    //       All four packages still compile — so the arm is exercised on real
    //       code and costs no over-refusal there. This is the load-bearing
    //       measurement: without it the corpus green would be vacuous.
    //   deem/wql (251 compilations, 4957 arm entries, 496 calls, 481 summaries)
    //       : `to_result` is non-zero ZERO times, so nothing is ever seeded.
    //       The arc's widest-surface worry is answered by absence, not by luck
    //       — deem/wql simply has no ref-returning callee whose result carries
    //       a parameter's borrow. Do not read those 3404 green tests as
    //       evidence ABOUT this arm; they are silent on it.
    //   `each_under` (the sub-place half below): 0 fires outside its own
    //       witness fail/bc_d1r11_x1_result_subplace, which does red under a
    //       control revert. A rule with a witness and an empty real-code
    //       population — kept because the witness refuses, but the population
    //       is the reason to re-measure it before building anything on it.
    std::vector<std::string> ref_sources_of(lir_view::ExprRef val, int depth = 0) const {
        std::vector<std::string> raw, ok;
        if (!val) return ok;
        ref_source_places(val, prog_.type_pool.impl(), raw);
        for (auto& p : raw)
            if (ref_source_admissible(p)) ok.push_back(std::move(p));
        if (!raw.empty() || depth > 4) return ok;
        using namespace lir_view;
        using Code = lir_schema::expr::Code;
        auto add = [&](std::string p) {
            if (std::find(ok.begin(), ok.end(), p) == ok.end())
                ok.push_back(std::move(p));
        };
        auto seed = [&](ExprRef a) {
            if (!a) return;
            for (auto& p : ref_sources_of(a, depth + 1)) {
                // A mask bit names a PARAMETER, not a field of it: `peel3r(h:
                // &mut Inner) -> &mut Vec<B> { return h.r; }` summarises
                // result<-0x1 and the argument's place is `hh`. Seeding `hh`
                // alone is where the edge dies — `each_root("hh")` never
                // reaches `hh.r`'s targets, so the loan `s` raises stops at
                // `hh` and `vs` looks unborrowed. Holding the whole struct
                // mutably reaches every field of it, so every place recorded
                // UNDER the seeded one is seeded with it. Measured: without
                // this, the by-ref twin x2_byref_twin still admits with a
                // CORRECT summary — the coarsening, not the mask, is the leak.
                reborrow_of_.each_under(p, [&](const std::string& sub) { add(sub); });
                add(std::move(p));
            }
        };
        auto by_mask = [&](const FlowSummary* fs, const std::vector<ExprRef>& ops) {
            if (!fs) return;
            for (size_t i = 0; i < ops.size() && i < fs->nparams; ++i)
                if (fs->to_result & (1ull << i)) seed(ops[i]);
        };
        std::vector<ExprRef> ops;
        switch (val.kind()) {
            case Code::Call: {
                ECallView cv{val};
                cv.each_arg([&](ExprRef a) { ops.push_back(a); });
                by_mask(flow_of_call(cv.callee()), ops);
                break;
            }
            case Code::FnPtrCall: {           // G1's twin: a resolved pointer
                EFnPtrCallView fv{val};       // has a real summary, else nothing
                fv.each_arg([&](ExprRef a) { ops.push_back(a); });
                by_mask(flow_of_fnptr(fv.callee()), ops);
                break;
            }
            case Code::MethodCall: {
                EMethodCallView mv{val};
                ops.push_back(mv.receiver());
                mv.each_arg([&](ExprRef a) { ops.push_back(a); });
                by_mask(flow_of_method(mv), ops);
                break;
            }
            // D1 round 13 / P0c: `?` is TRANSPARENT here too. The shape walker
            // above answers nothing for a Try because its inner is a CALL, and
            // a call result is this function's question, not the shape's — so
            // the unwrap has to happen on THIS side of the split or the mask
            // is never consulted at all. Measured: `let s: &mut Vec<B> =
            // pick(&mut vs)?;` admitted a later `c.bump()` while the direct-
            // return twin `pickd(&mut vs)` refused it.
            case Code::Try:
                for (auto& p : ref_sources_of(ETryView{val}.inner(), depth + 1))
                    add(std::move(p));
                break;
            default: break;
        }
        return ok;
    }
    // Record (or retract) the reborrow edge for a binding. Called from `let`
    // and from assignment; a write that is NOT a reborrow retracts, so the map
    // never outlives the fact.
    // ── D1 round 7 / R7a, SECOND RULE: A SHARED REBORROW IS A REBORROW ────
    //
    // THE DEFECT (measured). `let mut s: S = c.mk_s(); let r: &S = &s; let b:
    // B = r.peek(); c.bump(); *b.p` ADMITS while its alias-free twin
    // `s.peek()` REFUSES. Traced: `note_reborrow name=r referent=<empty>` and
    // NO alias_use at all — both this gate and prescan_reborrow's admitted
    // only Kind::MutRef, so a shared reborrow was recorded NOWHERE. `s`'s last
    // use stayed at the `&s` line, so c's loan was released one line EARLIER
    // than in the `&mut` leak.
    //
    // The `&mut`/`&` distinction is about what may be DONE through the
    // reference, not about whether the referent stays borrowed — for the
    // live-range and provenance questions this file asks, a shared reborrow is
    // exactly as load-bearing. Measured as its OWN rule (separate build, own
    // probe pair) because it touches the NLL live range of every `let r = &x;`
    // in the corpus, which the `&mut` rule does not.
    // ⚠ A SECOND, NARROWER NOTION OF "IS A REFERENCE" — and the narrow one
    // silently won. `is_ref_kind` (above) already admits Ref, MutRef, Slice,
    // non-owning DstRef and non-owning TraitObject, and says why in its own
    // comment: "Fat borrowed forms carry the same provenance duties as `&T`".
    // This predicate never got that update, so every FAT borrowed form fell out
    // of the reborrow-alias channel and its holder's NLL last use was never
    // extended. MEASURED, strict one-variable pairs (one character apart, same
    // binding, same final expression), with LOGOS_DUMP_BC_RELEASE reading out
    // the holder's last use:
    //   let r: &[B; 1] = &w;  -> Kind::Ref    lu(w)=14 (the return)  REFUSED
    //   let r: &[B]    = &w;  -> Kind::Slice  lu(w)=12 (the `&w`)    rc 0
    //   let d: &B      = &w; let d2 = d;  -> lu(w)=17  REFUSED
    //   let d: &dyn Tr = &w; let d2 = d;  -> lu(w)=15  rc 0
    // Two live admits, one class. Delegating removes the second enumeration
    // rather than extending it — a `|| Kind::Slice` would have closed the slice
    // half and left the `&dyn` half exactly as open as it was.
    static bool is_reborrow_ref_kind(TypeRef t) { return is_ref_kind(t); }
    // D1 round 13 / P1: THE VALUE STORED IS AN ARRAY OF REFERENCES.
    //
    // The container-granular half of the array convention on the WRITE side.
    // `let arr: [&mut Vec<B>; 1] = [&mut vs];` has an ARRAY type, so the
    // ref-kind gate above left the source list empty and `set()` erased the
    // key — the same shape N0 met one projection deeper with a nested struct
    // literal. The array place names every element's referent (the read side
    // is `ref_source_places`' ArrLit arm), so an array LITERAL of references
    // is recorded like a reborrow, at the container.
    //
    // Deliberately narrow: only a literal, and only a DIRECT array of
    // references. An array of AGGREGATES that hold references records
    // nothing, because nothing reads such a place back either — the element
    // places do not exist, and inventing them is the unsound direction the
    // dynamic index rules out.
    static bool is_reborrow_store_value(TypeRef t, lir_view::ExprRef val) {
        if (is_reborrow_ref_kind(t)) return true;
        if (!val || val.kind() != lir_schema::expr::Code::ArrLit || !t) return false;
        auto k = t.kind();
        if (k != LogosType::Kind::Array && k != LogosType::Kind::Slice) return false;
        return is_reborrow_ref_kind(t.elem());
    }
    // ── D1 round 10 / E0: RESOLVE-THEN-FREEZE, FOR A BINDING TOO ───────────
    //
    // A recorded source list is snapshotted to the TRANSITIVE CLOSURE of what
    // it names AT RECORD TIME. `note_reborrow_place` already did this, with the
    // argument that a stored reference keeps naming what it named at the store
    // even if the intermediate binding is later retargeted. E0 is that same
    // sentence for a `let` binding, and the shape that measured it:
    //   let s: &mut Vec<B> = h.m.i.r;               // s -> [h.m.i.r]
    //   h = Outer { m: Mid { i: Inner { r: &mut ws } } };   // erase_under("h")
    //   s.push(c.mk()); c.bump();                   // COMPILED (rc=0)
    // `note_reborrow` recorded the one-hop place and nothing else, so the root
    // rebind's retraction — which is CORRECT for `h.m.i.r` itself, that place
    // now names `ws` — took the only edge by which `s` still reached `vs`, and
    // each_root(s) died at an unmapped place.
    //
    // WHY FREEZE AND NOT RE-HOME. Re-homing dependents inside `erase_under`
    // would have to run at every retraction and would re-introduce the stale
    // referent r2/r3 close: a place that IS retargeted must stop naming its old
    // target, and only a node that captured the target BEFORE the retarget may
    // keep it. Freezing at record time says exactly that and says it once, in
    // the same place for a binding and for a stored place.
    void freeze_ref_closure(const std::string& key, std::vector<std::string>& s) {
        if (s.empty()) return;
        std::vector<std::string> cl;
        for (auto& src : s)
            reborrow_of_.each_root(src, [&](const std::string& n) {
                if (n != key && std::find(cl.begin(), cl.end(), n) == cl.end())
                    cl.push_back(n);
            });
        // TERMINAL FIRST — and this ordering is the half that closes E0. The
        // closure is a SET for every consumer that takes one (hop roots,
        // co-holders: U0's lesson that the INTERMEDIATE is what holds the
        // loan), but `endpoint()` is a single-name VIEW and reads the FRONT.
        // Measured: with the closure recorded source-order, `let s = h.m.i.r`
        // froze `s -> [h.m.i.r, vs]`, and after `h = Outer{…&mut ws}` the front
        // edge `h.m.i.r` RE-RESOLVED into the fresh value — the loan came out
        // `target=c holder=ws`, and ws's last use is the push line, so NLL
        // released it before `c.bump()` (dump: `holder=ws lu=21 cur=21
        // RELEASE`, against the no-rebind control's `holder=vs lu=22 keep`).
        // A member that was TERMINAL when the snapshot was taken cannot be
        // re-resolved by a later retarget, so it is the honest answer for the
        // one-name view; the retargetable intermediates stay in the set.
        std::stable_partition(cl.begin(), cl.end(), [&](const std::string& n) {
            auto* v = reborrow_of_.find(n);
            return !v || v->empty();
        });
        s.swap(cl);
    }
    void note_reborrow(const std::string& name, TypeRef t, lir_view::ExprRef val) {
        if (name.empty()) return;
        std::vector<std::string> s;
        // ── D1 round 8 / U3: A PATTERN BINDING IS A BINDING ────────────────
        //
        // MEASURED AND NOT TAKEN: falling back to the VALUE's type when the
        // `let` carries no declared type (which is what the Assign door does)
        // looked like the missing half of U3 — a destructuring `let` binds
        // names the programmer never annotated. It is NOT: instrumented over
        // the whole bc_d1 corpus plus U3's own witness, that arm fired ZERO
        // times, because sema gives every desugared field binding a declared
        // type. A widening with no consumer is a hedge, so the gate still
        // reads only `t`; U3's actual missing half is note_place_copy below.
        if (is_reborrow_store_value(t, val)) s = ref_sources_of(val);   // P1: arrays too
        if (t && t.kind() == LogosType::Kind::MutRef) reborrow_mut_.insert(name);
        else                                          reborrow_mut_.erase(name);
        freeze_ref_closure(name, s);            // E0
        reborrow_of_.set(name, std::move(s));   // empty ⇒ retract
        // G0 — SAME CONCEPT, ONE PROJECTION DEEPER. Re-binding `name` retracts
        // every place recorded UNDER it: the old struct value is gone, so
        // `name.f` no longer names whatever it used to reborrow. Then re-record
        // from the new value if it is a struct literal.
        retract_reborrow_places(name);
        note_struct_lit_reborrows(name, val);
        note_place_copy(name, val);
    }
    // ── D1 round 8 / U3, second half: A COPIED AGGREGATE CARRIES ITS PLACES ─
    //
    // THE DEFECT (measured). `let mut h: RB = RB { r: &mut vs };
    // let RB { r } = h; r.push(c.mk()); c.bump(); *vs.get(0).p` still compiled
    // after the gate above learned to read the VALUE's type: the trace shows
    // the binding IS recorded, as `r -> __dst_0.r`. Sema materialises the
    // destructured scrutinee into a temporary (`let __dst_0 = h;`) and binds
    // the fields off THAT, so the chain ends one place short — `__dst_0.r` is
    // a place nothing ever recorded, because the struct-literal recorder wrote
    // `h.r`.
    //
    // THE RULE, and it is not about patterns. Binding a whole AGGREGATE from
    // another place makes the destination's sub-places name the same things
    // the source's did: `d = h` ⇒ `d.f` names whatever `h.f` named. Recording
    // that at the one binding door covers the compiler's own destructure
    // temporary, an explicit `let d = h;`, and any future desugaring that
    // routes through a copy — none of which needs its own arm. It only ever
    // ADDS edges to places that did not exist a statement earlier (the name is
    // fresh, and re-binding retracted everything under it first), so it cannot
    // strengthen or invent a loan.
    void note_place_copy(const std::string& name, lir_view::ExprRef val) {
        if (!val) return;
        const auto* pool = prog_.type_pool.impl();
        TypeRef vt = val.type(pool);
        if (is_reborrow_ref_kind(vt)) return;   // a reborrow, handled above
        std::vector<std::string> raw;
        ref_source_places(val, pool, raw);
        if (raw.size() != 1) return;            // not ONE named source place
        const std::string& src = raw[0];
        if (src.empty() || src == name) return;
        std::string pfx = src + ".";
        std::vector<std::pair<std::string, std::vector<std::string>>> adds;
        for (auto& kv : reborrow_of_.e_)
            if (kv.first.size() > pfx.size() &&
                kv.first.compare(0, pfx.size(), pfx) == 0)
                adds.emplace_back(name + kv.first.substr(src.size()), kv.second);
        for (auto& a : adds) reborrow_of_.set(a.first, std::move(a.second));
    }
    // ── D1 round 6 / G0: A REBORROW STORED IN A FIELD IS STILL A REBORROW ──
    //
    // THE DEFECT. `reborrow_of_` (and the summarizer's `alias_`) are keyed by
    // binding NAME, so they cannot represent "h.r IS vs's place". Measured:
    //   struct RB { r: &mut Vec<B> }
    //   let mut h = RB { r: &mut vs }; h.r.push(c.mk()); c.bump(); *vs.get(0).p
    // compiled (rc=0) while the plain-local spelling `let r: &mut Vec<B> =
    // &mut vs; r.push(c.mk()); c.bump();` refused. The sharp half: reading back
    // through the SAME holder (`*h.r.get(0).p`) DOES refuse, so the loan lands
    // on `h` correctly — the ONLY thing lost is the identity `h.r == vs`.
    //
    // THE FIX. Key the map by PLACE, not by binding: record `h.r -> vs` at the
    // struct literal / field write that STORES the `&mut`, and let the place
    // walk resolve the dotted path. Retraction is the load-bearing half — a
    // field overwrite must retract EXACTLY that place's entry (and re-binding
    // the whole root retracts everything under it), or the map outlives the
    // fact and re-homes a later loan onto a stale referent.
    void retract_reborrow_places(const std::string& root) {
        reborrow_of_.erase_under(root);
    }
    // Record (or retract) ONE place. The discriminator is the stored VALUE's
    // type being `&mut` — the same test note_reborrow applies to a binding.
    void note_reborrow_place(const std::string& place, lir_view::ExprRef val) {
        if (place.empty()) return;
        const auto* pool = prog_.type_pool.impl();
        TypeRef vt = val ? val.type(pool) : TypeRef(nullptr);
        std::vector<std::string> s;
        if (is_reborrow_store_value(vt, val)) s = ref_sources_of(val);   // R7a rule 2 (+P1)
        if (vt && vt.kind() == LogosType::Kind::MutRef) reborrow_mut_.insert(place);
        else                                            reborrow_mut_.erase(place);
        // A STORED place is resolved EAGERLY, and now to the whole closure
        // rather than to the endpoint. Eagerly, because the stored reference
        // keeps naming what it named at the store even if the intermediate
        // binding is later retargeted (`let r = &mut vs; let h = RB{r:r};
        // r = &mut other;` must not move `h.r` onto `other`) — that was
        // rehome_reborrow's job here. To the CLOSURE, because the endpoint
        // alone is U0's defect one projection deeper: `r` holds the loan.
        freeze_ref_closure(place, s);
        reborrow_of_.set(place, std::move(s));
        // ── D1 round 9 / N0: A PLACE HOLDS WHAT A BINDING HOLDS ────────────
        //
        // THE DEFECT (measured, rc=0 on r8 AND at HEAD, program returns the
        // POST-mutation value):
        //   struct Inner { r: &mut Vec<B> }  struct Outer { i: Inner }
        //   let mut h: Outer = Outer { i: Inner { r: &mut vs } };
        //   h.i.r.push(c.mk()); c.bump(); *vs.get(0).p
        // A NESTED literal's type is a STRUCT, so the ref-kind gate above
        // leaves `s` empty and `set()` ERASES `h.i` — it does not merely skip
        // it — and nothing ever records `h.i.r == vs`. The one-level twin
        // refuses. Two more spellings leak for the same reason: the inner
        // literal bound to its own name and then nested (`Outer { i: inn }`),
        // and the aggregate copied back OUT (`let mut inner: Inner = h.i;`) —
        // the latter only because `note_place_copy` correctly requires the
        // source's SUB-PLACE keys to exist, and they never did.
        //
        // THE RULE, and it is the one `note_reborrow` already applies to a
        // BINDING: what is stored into a place is recorded the same way
        // whatever the place's spelling — retract what was under it, then
        // re-record from the new value, recursively for a nested literal and
        // by sub-place copy for a whole-aggregate copy. The recursion itself
        // is NOT written here: it is `each_ref_store`, the one enumeration
        // both instances of the graph are fed through (S1). This function is
        // the checker's POLICY for ONE pair, and nothing else.
        retract_reborrow_places(place);
        note_place_copy(place, val);
    }
    // The checker's feeding path: every place a value stores into, in
    // PRE-ORDER (a parent's retraction must run before its children record).
    void note_struct_lit_reborrows(const std::string& name, lir_view::ExprRef val) {
        each_nested_ref_store(name, val,
            [&](const std::string& place, lir_view::ExprRef v) {
                note_reborrow_place(place, v);
            });
    }
    // Walk the dotted path root.f1.f2… through the place map. Each hop that
    // resolves REPLACES the accumulated name, so the remaining steps apply to
    // the referent (`h.r == vs` ⇒ a write to `h.r.x` is a write to `vs.x`).
    // The first unrecorded step stops the walk: below it the field names are
    // the referent's own, not another alias.
    // ── D1 round 8 / U0: THE RESOLVE RETURNS THE CLOSURE, NOT THE ENDPOINT ──
    //
    // THE DEFECT. `let rc: &C = &c; let rc2: &C = rc; cur.batch(rc2);` held
    // across `c.bump()` COMPILED while the one-hop spelling refuses. The chase
    // was not short — a decisive differential (`let _keep: &C = rc;` AFTER the
    // bump ⇒ rc=1) showed the loan record is `target=c holder=rc`, i.e. the
    // chase resolved rc2 → c and handed back exactly the one name that does NOT
    // hold the loan. One hop worked only because there the intermediate and the
    // start name coincide. So the answer to "what does this place name" is a
    // SET — every node on the chain — and the endpoint form is a lossy view of
    // it, not the primitive.
    //
    // WHY IT CANNOT OVER-REFUSE ON ITS OWN. Same argument as R7a: it only ADDS
    // names to the hop roots, inherit_loans ADDS a co-holder to an existing
    // record (never creates one, never strengthens a borrow), and inheriting
    // from a binding that holds no loan is a no-op. Retraction is untouched.
    void resolve_ref_places(const std::string& base,
                            const std::vector<std::string>& fields_outer_first,
                            bool path_ok,
                            std::vector<std::string>& out) const {
        if (base.empty()) return;
        std::vector<std::string> frontier;
        auto close = [&](const std::string& n, std::vector<std::string>& into) {
            reborrow_of_.each_root(n, [&](const std::string& m) {
                if (std::find(out.begin(), out.end(), m) == out.end()) out.push_back(m);
                into.push_back(m);
            });
        };
        close(base, frontier);
        if (!path_ok) return;
        // Walk the dotted path root.f1.f2… . Each step that RESOLVES replaces
        // the frontier with what it names (`h.r == vs` ⇒ `h.r.x` is `vs.x`).
        // The first unrecorded step stops the walk: below it the field names
        // are the referent's own, not another alias.
        for (auto it = fields_outer_first.rbegin(); it != fields_outer_first.rend(); ++it) {
            std::vector<std::string> next;
            for (auto& n : frontier) {
                std::string key = n + "." + *it;
                if (reborrow_of_.find(key)) { close(key, next); continue; }
                // ── D1 round 9 / N0, the WALK half ────────────────────────
                // An unrecorded step is not the end of the path: `h` is a
                // plain local, so `h.i` names nothing — but `h.i.r` IS a
                // recorded place (that is what a NESTED literal deposits).
                // Stopping here is what left `h.i.r.push(c.mk())` unresolved
                // after the recorder was fixed. Carry the literal place
                // forward WITHOUT publishing it as a root: it names no
                // referent of its own, so it may not contribute a hop root —
                // only a deeper step that actually resolves may.
                next.push_back(key);   // frontier only, not `out`
            }
            if (next.empty()) break;
            frontier.swap(next);
        }
    }
    // The single-name VIEW of the walk above, for the consumers whose contract
    // is one name (place_write_root's destination, the *Write receiver root).
    std::string resolve_place_reborrow(std::string base,
                                       const std::vector<std::string>& fields_outer_first,
                                       bool path_ok) const {
        base = reborrow_of_.endpoint(std::move(base));
        if (!path_ok) return base;
        // N0, the SINGLE-NAME half of the same walk. An unrecorded step is not
        // the end of the path — `h` is a plain local so `h.i` names nothing,
        // while `h.i.r` IS a recorded place. Carry the literal place forward
        // and keep the LAST step that actually resolved as the answer, so the
        // destination of `h.i.r.push(…)` is `vs` and not `h`.
        std::string cur = base;
        for (auto it = fields_outer_first.rbegin(); it != fields_outer_first.rend(); ++it) {
            cur += "." + *it;
            auto* f = reborrow_of_.find(cur);
            if (!f || f->empty()) continue;
            cur  = reborrow_of_.endpoint(f->front());
            base = cur;
        }
        return base;
    }
    // ── D1 round 5 / H4: A CLOSURE CALL'S RESULT HAS THE CAPTURES' PROV ────
    //
    // THE DEFECT. `let f = || -> B { return c.mk(); }; return f();` compiled
    // where the inlined `return c.mk();` refused, and `vs.push(f())` leaked
    // where `vs.push(c.mk())` refused. Both channels stopped at the same wall:
    // every provenance walk in this file is written over OPERANDS, and a
    // ClosureCall's only operands are the closure value and the explicit args
    // — the CAPTURES, where a closure's provenance actually lives, are not
    // operands at all. So the documented (d) "unresolvable indirect call"
    // fallback was not merely conservative here, it was STRUCTURALLY VACUOUS:
    // there was nothing for it to be conservative about.
    //
    // THE FIX. Record the capture list at the binding — the one place the
    // ClosureBox and the name meet — and let the call site read it. The
    // existing ClosureBox arms (prov_of_retained's, the loan channel's) then
    // apply unchanged at the call.
    //
    // A GENUINE FN POINTER IS NOT A CLOSURE. The discriminator is the callee
    // expression's TYPE KIND (Kind::Closure), not the expression code — both
    // ClosureCall and FnPtrCall spell a closure local in different lowerings,
    // and a real fn pointer has no captures and keeps its current behaviour
    // (closure_caps_of returns nullptr and every arm below falls through).
    void note_closure_caps(const std::string& name, lir_view::ExprRef val) {
        using Code = lir_schema::expr::Code;
        if (name.empty()) return;
        if (!val || val.kind() != Code::ClosureBox) { closure_caps_.erase(name); return; }
        std::vector<std::string> caps;
        lir_view::EClosureBoxView{val}.each_capture_name(
            [&](std::string_view c){ caps.emplace_back(c); });
        if (caps.empty()) closure_caps_.erase(name);
        else              closure_caps_[name] = std::move(caps);
    }
    const std::vector<std::string>* closure_caps_of(lir_view::ExprRef callee) const {
        using Code = lir_schema::expr::Code;
        if (!callee) return nullptr;
        const auto* pool = prog_.type_pool.impl();
        TypeRef ct = callee.type(pool);
        if (!ct || ct.kind() != LogosType::Kind::Closure) return nullptr;
        if (callee.kind() != Code::VarRef) return nullptr;
        auto it = closure_caps_.find(std::string(lir_view::EVarRefView{callee}.name()));
        return it == closure_caps_.end() ? nullptr : &it->second;
    }
    static lir_view::ExprRef call_callee(lir_view::ExprRef e) {
        using Code = lir_schema::expr::Code;
        if (e.kind() == Code::ClosureCall) return lir_view::EClosureCallView{e}.callee();
        if (e.kind() == Code::FnPtrCall)   return lir_view::EFnPtrCallView{e}.callee();
        return {};
    }
    // `rehome_reborrow`'s 8-hop bounded chase is DELETED; `RefGraph::endpoint`
    // is the same walk with a visited set instead of a hop cap for the cycle
    // guard. This name survives only as the one-word call site spelling.
    std::string rehome_reborrow(std::string n) const {
        return reborrow_of_.endpoint(std::move(n));
    }

    // ── D1 round 9 / F0, the RETRACTION half ───────────────────────────────
    //
    // Re-pointing a reference-typed PLACE drops the reference that lived
    // there, so the loan that reference held on its OLD referent ends here.
    // Without this the field spelling of the overlap case (`h.r = &mut vs;`
    // while `h.r` already names `vs`) reports "already mutably borrowed"
    // against a loan nothing can use any more, while the plain-local twin
    // (`r = &mut vs;`) admits — measured on both trees.
    //
    // ⚠ THE ABUSE DIRECTION. This releases ONLY records that (a) name a target
    // this exact place used to reborrow, (b) are held by this place's root as
    // the SOLE holder — a record with co-holders was inherited by something
    // else that may still use it, and (c) at most one record per target. So it
    // cannot retire a loan another binding still owns. The pins are
    // f0_refuse_held (use of the new referent while the field is still live)
    // and f0_refuse_leak (a `c.mk()` stashed through the retargeted field),
    // both of which must stay REFUSED.
    void release_place_retarget(const std::string& place, const std::string& root) {
        const auto* old = reborrow_of_.find(place);
        if (!old || root.empty()) return;
        // CEILING PROBE `retarget_keep` — MEASURED 2026-08-27: fired 2 times
        // across the 447 ledger compiles, ceiling 0.
        // ⚠ CEILING 0 OVER A POPULATION OF TWO IS NOT A REFUTATION, AND
        // "PRICED AND CLOSED" WAS THE WRONG WORD. The coverage map of
        // 2026-08-27 counts, over 8060 runs, 194 entries to
        // release_place_retarget and 30 arrivals at this condition
        // (docs/coverage §E prints 194; that is the function, not the site).
        // The missing `frame.field_borrows` loop noted below is a real
        // asymmetry that the ledger has no program to exercise. Not refuted —
        // unpopulated. Re-opening it needs a hand-written counter-example or a
        // pass-corpus measurement, not another ledger run.
        //
        // The hypothesis was: this F0 retraction walks
        // `frame.borrows` only, with NO `frame.field_borrows` loop, while its
        // caller reaches it ONLY for a place with a non-empty path. Suppress
        // the retraction entirely: strictly fewer releases.
        if (logos::probe::on("retarget_keep")) return;
        std::vector<std::string> targets(*old);
        auto take_one = [&](const std::string& t) {
            auto it = std::find(targets.begin(), targets.end(), t);
            if (it == targets.end()) return false;
            targets.erase(it);
            return true;
        };
        for (auto& frame : scopes_) {
            for (size_t i = frame.borrows.size(); i > 0; --i) {
                auto& br = frame.borrows[i - 1];
                if (br.holder != root || !br.co_holders.empty()) continue;
                if (!take_one(br.target)) continue;
                if (auto sit = var_find(br.target_slot, br.target); sit != nullptr) {
                    if (br.is_mut) sit->mut_borrowed = false;
                    else if (sit->shared_borrows > 0) --sit->shared_borrows;
                }
                frame.borrows.erase(frame.borrows.begin() + (i - 1));
            }
        }
    }
    void place_write_loans(const std::string& root, lir_view::ExprRef val,
                           uint32_t ln, bool through_ref) {
        if (root.empty() || !val) return;
        // Mirror Assign's GATE as well as its routing. Assign only routes a RHS
        // that is a ref, an aggregate literal, or (via prov_) a ref binding;
        // routing UNCONDITIONALLY ties every `&`/`&mut` argument of an ordinary
        // call to the destination, and the destination outlives the statement.
        // Measured on stdlib: `st_hasres[i] = emit_step_texts(…, &mut
        // st_build[i], …)` inside a `while` — the arg borrows became loans held
        // by `st_hasres`, so iteration 2 reported "already mutably borrowed"
        // four times. A call whose RESULT carries no borrow stores no borrow.
        using EC = lir_schema::expr::Code;
        TypeRef vt = val.type(prog_.type_pool.impl());
        bool agg_lit = val.kind() == EC::StructLit || val.kind() == EC::TupleLit ||
                       val.kind() == EC::ArrLit    || val.kind() == EC::EnumLitData;
        if (!agg_lit && !is_ref_kind(vt) && !is_borrow_carrying_type(vt)) return;
        size_t nb = scopes_.empty() ? 0 : scopes_.back().borrows.size();
        size_t nf = scopes_.empty() ? 0 : scopes_.back().field_borrows.size();
        take_ref_borrows(val, ln, root, /*record_only=*/true);
        // A SELF-REFERENTIAL place write borrows out of the very root it stores
        // into: `self.cu = cp.next(&self.cu)` — the cursor is replaced by one
        // derived from the old cursor. Holding that loan against `root` makes
        // it immortal (the root outlives every statement), so the NEXT write to
        // the same place reports "already borrowed": measured as 16 L2 reds in
        // the ctr_family Walk::next / VecWalk::next iterators. The read of the
        // old value ends when the call returns; only borrows of OTHER roots
        // survive in the stored value. Undo the ones taken against `root`.
        if (!scopes_.empty()) {
            auto& fr = scopes_.back();
            for (size_t i = fr.borrows.size(); i > nb; --i) {
                auto& br = fr.borrows[i - 1];
                if (br.target != root) continue;
                if (auto it = var_find(br.target_slot, br.target); it != nullptr) {
                    if (br.is_mut) it->mut_borrowed = false;
                    else if (it->shared_borrows > 0) --it->shared_borrows;
                }
                fr.borrows.erase(fr.borrows.begin() + (i - 1));
            }
            for (size_t i = fr.field_borrows.size(); i > nf; --i) {
                auto& fb = fr.field_borrows[i - 1];
                if (fb.target != root) continue;
                if (auto it = var_find(fb.target_slot, fb.target); it != nullptr) {
                    // Observational zero census, decrement site 3 of 3.
                    // Coverage 2026-08-27: 0 runs in 8060.
                    if (fb.is_mut) it->mut_field_borrows.erase(fb.path);
                    else if (auto sb = it->shared_field_borrows.find(fb.path);
                             sb != it->shared_field_borrows.end()) {
                        if (sb->second <= 0)
                            (void)logos::probe::on("szw_pwl_pre0");
                        if (--sb->second <= 0)
                            it->shared_field_borrows.erase(sb);
                        else (void)logos::probe::on("szw_pwl_keep");
                    }
                }
                fr.field_borrows.erase(fr.field_borrows.begin() + (i - 1));
            }
        }
        if (!through_ref) return;
        std::vector<RefSrc> src_ids = ref_sources_under(root);
        if (src_ids.empty()) return;
        for (auto& src_id : src_ids) {
            const std::string& src = src_id.name;
            if (src == root) continue;
            // NOT plain inherit_loans: a record whose TARGET is `src` itself is
            // the `&mut src` borrow that `root` was FORMED from. Making src a
            // co-holder of its own borrow makes that borrow immortal and
            // refuses every later use of src — measured on A5
            // ((*r).b = c.mk(); … *w.b.p) as a spurious second diagnostic
            // "cannot use 'w' while it is mutably borrowed". Only loans of
            // OTHER referents travel through the reference.
            auto reroot = [&](auto& rec) {
                if (rec.target == src) return;
                if (rec.holder != root &&
                    std::find(rec.co_holders.begin(), rec.co_holders.end(), root)
                        == rec.co_holders.end()) return;
                if (rec.holder == src) return;
                if (std::find(rec.co_holders.begin(), rec.co_holders.end(), src)
                    != rec.co_holders.end()) return;
                rec.co_holders.push_back(src);
                rec.co_holder_slots.push_back(slot_of_binding(src));
                ++inherit_fired_;
            };
            for (auto& frame : scopes_) {
                for (auto& br : frame.borrows)       reroot(br);
                for (auto& fb : frame.field_borrows) reroot(fb);
            }
        }
        (void)ln;
    }

    void visit_stmt(lir_view::StmtRef sr) {
        if (!sr) return;
        uint32_t ln = lir_view::stmt_line(sr);
        // #75: the NLL cursor advances by PROGRAM POINT, not by line; `ln`
        // stays the raw line for every non-liveness consumer below.
        if (uint64_t pt = stmt_point(sr); pt > max_line_seen_) max_line_seen_ = pt;
        using namespace lir_view;
        using Code = lir_schema::stmt::Code;
        const auto* pool = prog_.type_pool.impl();
        // #86 MISS 1 — THE INSTRUMENT THAT PROVED THE TWO DEAD DOORS. The
        // Code::FieldWrite / Code::TupleWrite arms below carry no holder-
        // provenance hook because they are never reached from the front end;
        // this print is what measured it (zero lines over all 2203 corpus
        // fixtures and the whole 53-target build). Kept so the claim can be
        // re-checked rather than believed.
        if (std::getenv("LOGOS_DUMP_BC_PLACEWRITE_DOOR") &&
            (sr.kind() == Code::FieldWrite || sr.kind() == Code::TupleWrite))
            fprintf(stderr, "[bc-placewrite-door] fn=%s ln=%u kind=%d\n",
                    fn_name_.c_str(), ln, (int)sr.kind());

        switch (sr.kind()) {
            // ── Let binding ──────────────────────────────────────────────
            case Code::Let: {
                SLetView v{sr};
                if (is_cond_move_field_drop_temp(sr, v.name(), v.value())) break;  // #121
                auto val = v.value();
                auto t   = v.type(pool);
                std::string name(v.name());
                // P2-13: a closure binding routes through take_ref_borrows too,
                // so its by-ref captures register as borrows held by `name` (the
                // closure var) — released at the closure's last use (NLL).
                bool is_closure_t = t && t.kind() == LogosType::Kind::Closure;
                // F5: the loans recorded while walking the RHS take `name` as
                // their holder, but declare_var runs only AFTER. Publish the
                // binding's identity first so those loans capture the NEW
                // slot, not the shadowed one's.
                note_binding_slot(name, v.var_slot());
                // #86 MISS 1: the mutation sites know only the receiver's NAME
                // (SFieldWriteView / STupleWriteView / the out-param root), and
                // VarState carries no type. Record the declared type here —
                // shadowing overwrites, which is right: a shadowing `let`
                // replaces the binding this name resolves to.
                if (t) holder_ty_[name] = t;
                // An aggregate LITERAL RHS (`let g = Guard { r: &mut snap }`,
                // tuple/array of borrows) may CAPTURE a borrow into the binding
                // even when the binding's nominal type isn't itself flagged
                // borrow-carrying — route it through take_ref_borrows so any
                // `&`/`&mut` field is recorded as a loan held by `name` (NLL).
                // Non-borrow fields fall to the same consuming visit as before.
                // D1: EnumLitData joins the three — `let ob = Option::Some(
                // c.mk())` is the same capture as `Wrap { b: c.mk() }`.
                bool val_is_agg_lit = val &&
                    (val.kind() == lir_schema::expr::Code::StructLit ||
                     val.kind() == lir_schema::expr::Code::TupleLit ||
                     val.kind() == lir_schema::expr::Code::ArrLit ||
                     val.kind() == lir_schema::expr::Code::EnumLitData);
                // A borrow-carrying VALUE binding (`let it = v.iter_mut()`)
                // holds the receiver's borrow for the binding's lifetime —
                // route through take_ref_borrows so its MethodCall case
                // records the borrow with holder=name (NLL release at last
                // use). Non-borrow shapes hit take_ref_borrows' default,
                // which is the same consuming visit as before (move
                // tracking for `let h2 = h` preserved).
                // ── CEILING PROBE `mrletann` (consumer half; the producer is
                // the annotated-`let` coercion site in sema_stmt). `&mut T` IS
                // a move type here — is_move_type's MutRef leaf says so — but a
                // ref-kind `t` routes the RHS to take_ref_borrows, which
                // records a loan instead of consuming, so
                // `let moved: &mut S = state;` never moves `state`.
                // ⚠ THIS HALF IS A DIVERGENCE PROBE ON PURPOSE. Rust treats an
                // explicit type ascription as a COERCION SITE and does
                // reborrow there; the ledger row this aims at
                // (reborrow-sugg-move-then-borrow) was ported WITH an
                // annotation the upstream program does not carry. What the
                // price answers is what the divergence would cost, not whether
                // it should be taken.
                // ── MEASURED 2026-08-29: CEILING 0 / COST 0, SITE PROVEN LIVE.
                // 420 fires on reborrow-sugg-move-then-borrow = 419 arrivals at
                // THIS arm plus 1 at the sema producer, and the producer-only
                // name `mrlasema` fires exactly ONCE on a five-line repro that
                // contains exactly one annotated `&mut` let — so the ascription
                // reborrow WAS declined at `let moved: &mut S = state;`. Yet
                // LOGOS_MRAM_TRACE printed only `[mla] ln=75
                // name=__ret_tmp_1514` twice, both prelude temporaries, and the
                // user's binding never matched (t == MutRef && val == VarRef).
                // The RHS therefore reaches borrow_check WRAPPED even with
                // sema_stmt's ascription site declined — a SECOND producer
                // downstream, `expect_type`/`apply_place_coercions` being the
                // named candidate (its own comment says the fold is in
                // progress). Repair by delegation, measured, not argued.
                bool p_mla = logos::probe::on("mrletann") && val && t &&
                             t.kind() == LogosType::Kind::MutRef &&
                             val.kind() == lir_schema::expr::Code::VarRef;
                if (p_mla) {
                    if (std::getenv("LOGOS_MRAM_TRACE"))
                        fprintf(stderr, "[mla] ln=%u name=%s\n", ln,
                                name.c_str());
                    visit(val, /*consuming=*/true, ln);
                // CEILING PROBE `aggwhole` — B-10, the BLUNT half: route a
                // `let` whose type STRUCTURALLY carries a borrow through
                // take_ref_borrows, which both hops and RECORDS. `probe::on`
                // is last so the fire count is the NEW routings only, not the
                // arm's whole population. See src/compiler/PROBES.md.
                } else if (val && (is_ref_kind(t) || is_closure_t ||
                            is_borrow_carrying_type(t) || val_is_agg_lit ||
                            (type_may_carry_borrow(t) &&
                             logos::probe::on("aggwhole")))) {
                    take_ref_borrows(val, ln, name);
                } else if (val) {
                    bool saved_dst = in_destructure_temp_;
                    in_destructure_temp_ = is_destructure_temp_name(name);
                    visit(val, /*consuming=*/true, ln);
                    in_destructure_temp_ = saved_dst;
                    // Door E / EXEMPT — the HOP ONLY, deliberately not the
                    // routing. When the LOAN channel says the value carries a
                    // borrow but the ESCAPE classification does not (an erased
                    // `Box<dyn Get>` built from a bc value; an exempt `H` whose
                    // field is bc), the binding must join the holder set — but
                    // it must NOT acquire take_ref_borrows' other effect, which
                    // is to record a fresh borrow for every `&`/`&mut`
                    // ARGUMENT with this binding as holder. Routing the whole
                    // thing was measured and it over-refuses: `let res: GpRes =
                    // gp_build(…, &mut sa, …); … sa.len()` in
                    // stdlib/mem/wql/lower.logos then held `&mut sa` for the
                    // rest of the function ("cannot borrow 'sa': 'sa' is
                    // already mutably borrowed"). Inheritance can only extend
                    // an EXISTING loan, so the hop alone cannot do that.
                    // CEILING PROBE `aggnarrow` — "a routing that HOPS WITHOUT
                    // RECORDING", spelled at the place that already draws that
                    // split. Same widening as `aggwhole` (structural carry) but
                    // applied to Door E's inherit-only hop instead of to the
                    // routing gate, so take_ref_borrows' fresh-borrow-per-
                    // argument effect — the whole of aggletroute's COST 40 —
                    // is not taken. See src/compiler/PROBES.md.
                    if (loan_carrying_type(t) ||
                        loan_carrying_type(val.type(pool)) ||
                        retains_loan_carrying_operand(val) ||
                        ((type_may_carry_borrow(t) ||
                          type_may_carry_borrow(val.type(pool))) &&
                         logos::probe::on("aggnarrow"))) {
                        std::vector<std::string> roots;
                        bc_hop_roots(val, roots);
                        for (auto& r : roots) inherit_loans(r, name, ln);
                        retain_operand_loans(val, name, ln);
                    }
                }
                declare_var(name, v.var_slot());  // Phase-1
                note_reborrow(name, t, val);      // H1
                note_closure_caps(name, val);     // H4
                if (auto it = var_find(v.var_slot(), name); it != nullptr)
                    it->is_mut_binding = v.is_mut();
                if (is_ref_kind(t) || is_borrow_carrying_type(t)) {
                    RefProv vp = prov_of(val);
                    // E0716: a `let`-bound reference outlives its statement, but
                    // it borrows into a temporary that drops at the end of THIS
                    // statement (`let v = make().view();`) → dangling. The owner
                    // must be bound to a variable first (`let h = make(); let v =
                    // h.view();`) so it lives as long as the borrow.
                    if (vp.is_temp)
                        report(ln,
                            "temporary value dropped while borrowed: this "
                            "reference borrows into a temporary that is dropped "
                            "at the end of the statement; bind the owning value "
                            "to a variable first so it outlives the borrow");
                    prov_[name] = vp;
                } else if (t && !t.lifetime_args().empty() &&
                         (t.kind() == LogosType::Kind::Struct ||
                          t.kind() == LogosType::Kind::ZonedStruct))
                    prov_[name] = prov_of(val);  // struct<'z> borrows through lifetime
                // ── #86 SUB-SITE 2: the LET side of the same wrong question ──
                // `let w: W = W { v: o.as_str() };` — W is neither ref-kind nor
                // #[borrow_carrying], so NOTHING above records provenance for
                // `w`, and `return w.v` then finds prov_of empty even though
                // its own gate fired. #71's holds_any_ref (via
                // type_may_carry_borrow) is the predicate that answers "does
                // this VALUE hold a borrow"; ask it, and read the aggregate
                // literal through prov_of_retained (prov_of has no StructLit /
                // TupleLit / EnumLitData arm — those are exactly the spellings
                // #86 was measured on).
                //
                // DELIBERATELY NARROW: only the ESCAPE fact (is_local /
                // is_temp) is recorded. A param-rooted answer is NOT stored,
                // so this cannot start the elision arm of check_return_value
                // on bindings that never fed it before.
                else if (val && type_may_carry_borrow_erased(t) &&
                         !residency_exemption_holds(t, val) &&
                         prov_.count(name) == 0) {
                    RefProv vp2 = prov_of(val);
                    if (!vp2.is_local && !vp2.is_temp && vp2.params.empty())
                        vp2 = prov_of_retained(val);
                    if (vp2.is_local || vp2.is_temp) {
                        if (std::getenv("LOGOS_86_TRACE"))
                            fprintf(stderr, "[#86trace-let] fn=%s line=%u var=%s "
                                    "loc=%d tmp=%d\n", fn_name_.c_str(), ln,
                                    name.c_str(), (int)vp2.is_local, (int)vp2.is_temp);
                        prov_[name] = RefProv{{}, vp2.is_local, vp2.is_temp};
                    }
                }
                // B87 dropck: record local borrow sources for Drop-lt bindings.
                if (val && struct_is_dropck_relevant(t)) {
                    std::vector<RefSrc> sources;
                    collect_borrow_locals(val, sources);
                    dropck_field_srcs_.erase(name);  // whole-value write re-owns
                    if (!sources.empty()) {
                        dropck_borrow_sources_[name] = std::move(sources);
                        dropck_binding_line_[name] = ln;
                    }
                }
                // §B6 (E0597): record local borrow sources for EVERY binding so
                // pop_scope can detect a stored borrow outliving its referent.
                record_ref_sources(name, val, ln);
                // #86 MISS-E — the sema-rewritten return temp keeps the names
                // its initializer mentioned, for the DIAGNOSTIC only. It is
                // deliberately NOT deposited through `store_ref_sources`: that
                // map is read by `pop_scope` and by the loan channel, and
                // widening it is what produced the 4 false stdlib E0597s
                // measured in this same round (see the recvstore door).
                if (is_return_temp_name(name) && val) {
                    std::vector<std::string> hr;
                    bc_hop_roots(val, hr);
                    if (!hr.empty()) ret_temp_roots_[name] = std::move(hr);
                }
                break;
            }

            // ── Assignment ───────────────────────────────────────────────
            case Code::Assign: {
                SAssignView v{sr};
                auto val = v.value();
                std::string name(v.name());
                // B87 (dropck-light): an assignment that fills a previously-
                // declared Drop-having lifetime-parameterised struct with a
                // freshly-borrowed local is unsafe — the binding survives
                // beyond this scope and its Drop will reference the borrow
                // after the source dies. Detect the pattern syntactically.
                // B74 gap: writing to a variable while it has any active
                // borrow violates exclusivity. mut_borrowed already errored
                // via check_live elsewhere; the missing case was shared
                // borrows. Catch them here.
                if (auto it = var_find(NO_SLOT, name); it != nullptr) {
                    if (it->shared_borrows > 0)
                        report(ln, std::format(
                            "cannot assign to '{}' because it is borrowed", name));
                    if (it->mut_borrowed)
                        report(ln, std::format(
                            "cannot assign to '{}' while it is mutably borrowed", name));
                    // A borrow of a FIELD of this variable is invalidated by the
                    // assignment exactly as a borrow of the whole variable is —
                    // the storage the reference names is overwritten either way.
                    // The two counters above see only path "", so `&v.f` was
                    // invisible here: `let i: &u64 = &v.f; v = Foo{…}; *i` was
                    // rc 0 while the `&v` spelling refused. The prefix query and
                    // the loan itself both already existed (`take_field_borrow`
                    // records at the dotted path); nothing was asking.
                    field_borrow_conflicts(*it, name, /*path=*/"",
                                           /*need_exclusive=*/true, ln, "assign to");
                }
                bool val_is_agg_lit2 = val &&
                    (val.kind() == lir_schema::expr::Code::StructLit ||
                     val.kind() == lir_schema::expr::Code::TupleLit ||
                     val.kind() == lir_schema::expr::Code::ArrLit ||
                     val.kind() == lir_schema::expr::Code::EnumLitData);
                bool is_ref_assign = val &&
                    (prov_.count(name) || is_ref_kind(val.type(pool)));
                if (is_ref_assign || val_is_agg_lit2) {
                    release_borrows_held_by(name);
                    take_ref_borrows(val, ln, name);
                } else if (val) {
                    visit(val, /*consuming=*/true, ln);
                    // Door E / EXEMPT — hop only; see the Let case above.
                    if (loan_carrying_type(val.type(pool)) ||
                        retains_loan_carrying_operand(val)) {
                        std::vector<std::string> roots;
                        bc_hop_roots(val, roots);
                        for (auto& r : roots) inherit_loans(r, name, ln);
                        retain_operand_loans(val, name, ln);
                    }
                }
                if (var_has(NO_SLOT, name)) {
                    // Re-own: value state (moves/borrows) resets, but the
                    // BINDING property `is_mut_binding` persists — `let mut
                    // nd = …; dl = nd; nd = fresh; &mut nd` is legal Rust
                    // (reinit of a moved-from mut binding keeps its mut-ness;
                    // wiping it made every explicit `&mut` after a
                    // reassignment fail "not declared as mut").
                    auto& st = var_at(NO_SLOT, name);
                    bool was_mut = st.is_mut_binding;
                    st = VarState{};
                    st.is_mut_binding = was_mut;
                }
                if (is_ref_assign)
                    prov_[name] = prov_of(val);
                // #86 MISS 1 / SITE a — THE WHOLE-VALUE REASSIGN.
                //   `let mut w: W = W{v:""}; w = W{v:o.as_str()}; return w;`
                // was rc 0: `is_ref_assign` is false (W is not a ref kind and
                // `prov_` held no entry), so the line above never ran, and
                // #86's let arm had already been passed. The holder type is
                // the VALUE's here — an assign carries its own type and needs
                // no `holder_ty_` lookup.
                note_holder_escape_prov(name, val ? val.type(pool) : TypeRef(nullptr),
                                        val, ln, "assign");
                note_reborrow(name, val ? val.type(pool) : TypeRef(nullptr), val);  // H1
                note_closure_caps(name, val);                                       // H4
                // B87 dropck: record on (re-)assign too.
                if (val) {
                    auto vt = val.type(pool);
                    if (struct_is_dropck_relevant(vt)) {
                        std::vector<RefSrc> sources;
                        collect_borrow_locals(val, sources);
                        dropck_field_srcs_.erase(name);  // whole-value write re-owns
                        if (!sources.empty()) {
                            dropck_borrow_sources_[name] = std::move(sources);
                            dropck_binding_line_[name] = ln;
                        }
                    }
                }
                // §B6 (E0597): (re-)record sources on assign — a rebind re-owns
                // (clears any prior dangling), then tracks the new borrow.
                record_ref_sources(name, val, ln);
                if (logos::probe::on("fpwrite") && in_closure_body_ && val &&
                    !closure_body_decls_.count(name)) {
                    std::vector<std::string> fpw_srcs;
                    collect_ref_sources(val, fpw_srcs);
                    for (auto& fpw_s : fpw_srcs)
                        if (closure_param_names_.count(fpw_s)) {
                            report(ln, std::format(
                                "ceiling-probe fpwrite: borrowed data escapes the "
                                "closure: '{}' is stored into '{}', which outlives "
                                "the closure call (E0521)", fpw_s, name));
                            break;
                        }
                }
                break;
            }

            // ── Return ───────────────────────────────────────────────────
            case Code::Return: {
                if (auto val = SReturnView{sr}.value()) {
                    // PROBE capretchk, site 2 of 2 — see walk_closure_body.
                    // `on()` is called UNCONDITIONALLY, never behind the `||`,
                    // so the fire count records arrivals rather than the
                    // short-circuit's leftovers (rule 1).
                    // LANDED 2026-08-31: `check_return_value` used to be
                    // hard-suppressed inside a closure body, so a closure
                    // returning a reference to a BODY LOCAL compiled. The
                    // suppression was the whole of the miss; the exemptions it
                    // was standing in for now live at their own sites (the
                    // closure's own ret type / param lifetimes above, and
                    // `closure_capture_names_` at the report gate).
                    check_return_value(val, ln);
                    visit(val, /*consuming=*/true, ln);
                }
                cur_diverged_ = true;
                break;
            }

            // ── Expression statement ─────────────────────────────────────
            case Code::ExprStmt: {
                auto ex = SExprStmtView{sr}.expr();
                visit(ex, /*consuming=*/true, ln);
                // #69 class A: a statement whose expression has type `!` does
                // not fall through — `my_panic();` ends the block exactly as
                // `return;` does (Code::Return above sets the same flag). Before
                // this arm `cur_diverged_` was set by Return/break/continue ONLY,
                // so visit_loop_body's `bottom_reachable = !cur_diverged_` was
                // true for a body ending in a `-> !` call, the body-bottom state
                // (with the loop's non-Copy binding moved and its re-init having
                // left via `continue`) crossed the back edge, and pass 2 refused
                // a move that no iteration can reach. Witness:
                // tests/logos/pass/bc_loop_bot_divergent_call_admit.logos.
                // ⚠ PERMISSIVE DIRECTION, so the condition is exactly `!` and
                // nothing wider — the two refuse-side pins named in that
                // fixture's header hold the non-diverging tail and the
                // `continue`-without-reinit path refusing.
                if (ex) {
                    auto ety = ex.type(pool);
                    if (ety && ety.kind() == LogosType::Kind::Never)
                        cur_diverged_ = true;
                }
                break;
            }

            // ── Field write: recv.field = value ──────────────────────────
            case Code::FieldWrite: {
                SFieldWriteView v{sr};
                // §2 Wave 9 — assigning to a moved-out field REINITIALIZES it.
                // The previous "partially moved" state on the receiver is
                // cleared for THIS specific field before check_live runs so
                // `let _ = s.v; s.v = …;` re-binds the field without rejecting
                // the receiver as partially-moved. Rust's NLL move analysis
                // does the same — drop-flag tracking is replaced on the new
                // bits.
                std::string recv_nm(v.receiver());
                std::string field_nm(v.field());
                if (!recv_nm.empty() && !field_nm.empty()) {
                    if (auto it = var_find(NO_SLOT, recv_nm); it != nullptr)
                        // T1-10/B78: reinit clears the path AND anything
                        // under it (writing `o.i` refills `o.i.s`).
                        erase_reinit(it->moved_fields, field_nm);
                }
                check_live(recv_nm, ln);
                visit(v.value(), /*consuming=*/true, ln);
                // §B6: `root.f = &x` stores a borrow into root — record so a
                // later use of root after x dies is E0597 (field sibling of the
                // struct-literal case).
                add_ref_sources(recv_nm, field_nm, v.value(), ln);  // F6: at the PLACE
                // #86 MISS 1 / SITE b — THE FIELD WRITE. `w.v = o.as_str()`,
                // the runtime-confirmed UAF one line from #86's own fixture.
                // ⚠ #86 MISS 1 — NO holder-provenance hook here, MEASURED.
                // A hook placed in this arm and in Code::TupleWrite below fired
                // ZERO times over all 2203 corpus fixtures AND the whole 53-
                // target build: sema lowers `s.f = …` and `t.0 = …` to
                // `SDerefWrite(AddrOfTemp(FieldRead/TupleIndex(VarRef s)), val)`
                // (this file's own §2-Wave-9 comment in Code::DerefWrite says
                // so), so these two arms are reachable only from round-tripped
                // or metaprog-emitted LIR. The hook lives in the DerefWrite arm,
                // which is the door the spelling actually takes.
                // G0: `h.r = &mut vs` stores a REBORROW at the place `h.r`.
                // Retracts when the same field is overwritten with anything else.
                if (!recv_nm.empty() && !field_nm.empty())
                    note_reborrow_place(recv_nm + "." + field_nm, v.value());
                // Door A: the loan counterpart of the line above.
                place_write_loans(recv_nm, v.value(), ln,
                                  /*through_ref=*/prov_.count(recv_nm) > 0);
                break;
            }

            // ── Index write: arr[i] = value ──────────────────────────────
            // Element borrow gap: `arr[i] = v` writes through the container,
            // so it must respect existing borrows on the container — mirror
            // the SAssign exclusivity check (any active shared or mut borrow
            // of `arr` blocks the write). This matches Rust: an `&arr[i]`
            // shared borrow blocks any element write; `&mut arr[i]` blocks it.
            case Code::IndexWrite: {
                SIndexWriteView v{sr};
                std::string nm(v.arr());
                if (auto it = var_find(NO_SLOT, nm); it != nullptr) {
                    if (it->shared_borrows > 0)
                        report(ln, std::format(
                            "cannot assign to '{}[..]' because '{}' is borrowed",
                            nm, nm));
                    if (it->mut_borrowed)
                        report(ln, std::format(
                            "cannot assign to '{}[..]' while '{}' is mutably borrowed",
                            nm, nm));
                }
                check_live(nm, ln);
                visit(v.index(), /*consuming=*/true, ln);
                visit(v.value(), /*consuming=*/true, ln);
                // Door A: `a[i] = c.mk()` stores the borrow into `a`.
                place_write_loans(nm, v.value(), ln,
                                  /*through_ref=*/prov_.count(nm) > 0);
                break;
            }

            // ── Field-index write: recv.field[i] = value ─────────────────
            // Same exclusivity check as IndexWrite — writing through the
            // receiver respects borrows on the receiver.
            case Code::FieldIndexWrite: {
                SFieldIndexWriteView v{sr};
                std::string nm(v.receiver());
                if (auto it = var_find(NO_SLOT, nm); it != nullptr) {
                    if (it->shared_borrows > 0)
                        report(ln, std::format(
                            "cannot assign to '{}.{}[..]' because '{}' is borrowed",
                            nm, std::string(v.field()), nm));
                    if (it->mut_borrowed)
                        report(ln, std::format(
                            "cannot assign to '{}.{}[..]' while '{}' is mutably borrowed",
                            nm, std::string(v.field()), nm));
                }
                check_live(nm, ln);
                visit(v.index(), /*consuming=*/true, ln);
                visit(v.value(), /*consuming=*/true, ln);
                // Door A: `o.f[i] = c.mk()` stores the borrow into `o`.
                place_write_loans(nm, v.value(), ln,
                                  /*through_ref=*/prov_.count(nm) > 0);
                break;
            }

            // ── Chain field write: recv.mid.field = value ────────────────
            case Code::ChainFieldWrite: {
                SChainFieldWriteView v{sr};
                std::string cf_nm(v.receiver());
                check_live(cf_nm, ln);
                visit(v.value(), /*consuming=*/true, ln);
                {   // F6: the written place is receiver.mid[.extra…].field
                    std::string cf_path(v.mid_field());
                    v.each_extra([&](std::string_view ex) {
                        if (!ex.empty()) cf_path += "." + std::string(ex);
                    });
                    if (!v.field().empty())
                        cf_path += (cf_path.empty() ? "" : ".") + std::string(v.field());
                    add_ref_sources(cf_nm, cf_path, v.value(), ln);  // §B6
                }
                place_write_loans(cf_nm, v.value(), ln,   // Door A
                                  /*through_ref=*/prov_.count(cf_nm) > 0);
                break;
            }

            // ── Deref-field write: (*recv).field = value ─────────────────
            case Code::DerefFieldWrite: {
                SDerefFieldWriteView v{sr};
                std::string df_nm(v.receiver());
                check_live(df_nm, ln);
                visit(v.value(), /*consuming=*/true, ln);
                // Door A: the destination root is the REFERENCE — its referent
                // (from the §B6 source map) co-holds, or the loan would retire
                // at this very statement.
                place_write_loans(df_nm, v.value(), ln, /*through_ref=*/true);
                break;
            }

            // ── Deref write: *ptr = value ─────────────────────────────────
            // `arr[i] = v` and other place writes lower to SDerefWrite of
            // AddrOfTemp(<place>). When the place is `<root>[i]…` / `<root>.f[i]…`,
            // the write conflicts with any active borrow on the root (Rust:
            // `&arr[0]; arr[1] = …` rejected — shared borrow blocks any element
            // write; `&mut arr[0]; arr[1] = …` also rejected). Walk the AddrOfTemp
            // chain through FieldRead/IndexRead/SliceIndex to extract the root,
            // then check the root's borrow state directly (no lasting borrow:
            // the write is transient at this statement).
            case Code::DerefWrite: {
                SDerefWriteView v{sr};
                auto ptr = v.ptr();
                using EC = lir_schema::expr::Code;
                if (ptr && ptr.kind() == EC::AddrOfTemp) {
                    EAddrOfTempView atv{ptr};
                    auto cur = atv.inner();
                    bool saw_index = false;
                    while (cur) {
                        if (cur.kind() == EC::FieldRead)      cur = EFieldReadView{cur}.receiver();
                        else if (cur.kind() == EC::IndexRead) { saw_index = true; cur = EIndexReadView{cur}.receiver(); }
                        else if (cur.kind() == EC::SliceIndex){ saw_index = true; cur = ESliceIndexView{cur}.slice(); }
                        else if (cur.kind() == EC::TupleIndex){ cur = ETupleIndexView{cur}.receiver(); }
                        else break;
                    }
                    // CEILING PROBE `dwnoidx` — the place-write exclusivity
                    // refusal below is gated on the AddrOfTemp walk having
                    // crossed an IndexRead/SliceIndex, so `s.f = v` / `t.0 = v`
                    // reach the same VarRef root and are EXEMPT. The un-gated
                    // copy of this rule lives on Code::FieldWrite /
                    // FieldIndexWrite, which sema never emits (0 arrivals).
                    // Drop the conjunct: strictly more statements reach the
                    // two reports, nothing is released or recorded less.
                    // ⛔ REFUTED OVER A PROVEN-LIVE SITE — MEASURED 2026-08-28:
                    // fired 189 times across the 400 ledger compiles (19,193
                    // arrivals over the 8060-run coverage population, of which
                    // `saw_index` is true on only 6,518 — so ~12,675 rooted
                    // place writes per pass really are exempt). CEILING 0,
                    // COST 0. The exemption is real, it is reached, and it
                    // holds open NOT ONE ledger row: the un-gated copy on the
                    // dead FieldWrite arms is not a missing refusal anyone is
                    // waiting for. A real negative result, not an absent
                    // population. Do not re-propose without a NEW row class.
                    bool dwnoidx = logos::probe::on("dwnoidx");
                    if ((dwnoidx || saw_index) && cur && cur.kind() == EC::VarRef) {
                        std::string root(EVarRefView{cur}.name());
                        uint32_t root_slot = EVarRefView{cur}.var_slot();  // Phase-1
                        if (auto it = var_find(root_slot, root); it != nullptr) {
                            if (it->shared_borrows > 0)
                                report(ln, std::format(
                                    "cannot assign through '{}[..]' because '{}' is borrowed",
                                    root, root));
                            if (it->mut_borrowed)
                                report(ln, std::format(
                                    "cannot assign through '{}[..]' while '{}' is mutably borrowed",
                                    root, root));
                        }
                    }
                    // §2 Wave 9 — reinit of a moved-out field. The simplest
                    // place shape `s.f = …` lowers to
                    // `SDerefWrite(AddrOfTemp(FieldRead(VarRef s, f)), val)`.
                    // Before visiting the LHS (which walks the FieldRead and
                    // would trip the moved-field check), clear
                    // moved_fields[f] on the root — the assignment
                    // reinitialises the field. Rust's NLL drop-flag analysis
                    // does the same: the new bits replace the old.
                    auto inner = atv.inner();
                    if (inner && inner.kind() == EC::FieldRead) {
                        // T1-10/B78: walk the whole FieldRead chain so a
                        // nested reinit (`o.i.s = …`) clears its full path.
                        std::vector<std::string> segs;
                        ExprRef cur = inner;
                        while (cur && cur.kind() == EC::FieldRead) {
                            EFieldReadView cv{cur};
                            segs.emplace_back(std::string(cv.field()));
                            cur = cv.receiver();
                        }
                        if (cur && cur.kind() == EC::VarRef) {
                            std::string root(EVarRefView{cur}.name());
                            uint32_t root_slot = EVarRefView{cur}.var_slot();  // Phase-1
                            std::string fpath;
                            for (auto it2 = segs.rbegin(); it2 != segs.rend(); ++it2) {
                                if (!fpath.empty()) fpath.push_back('.');
                                fpath += *it2;
                            }
                            if (auto it = var_find(root_slot, root); it != nullptr)
                                erase_reinit(it->moved_fields, fpath);
                        }
                    }
                    // §B6: `root.f = &x` / `root.0 = &x` stores a borrow into
                    // root's OWN storage. Walk a PURE field/tuple chain to the
                    // root local (bail on a deref/index — those write through a
                    // pointer or into an element, not root's storage). Record so
                    // a later use of root after x dies is E0597.
                    ExprRef c = atv.inner();
                    while (c) {
                        if (c.kind() == EC::FieldRead)       c = EFieldReadView{c}.receiver();
                        else if (c.kind() == EC::TupleIndex) c = ETupleIndexView{c}.receiver();
                        // MEASURED 2026-08-28, 379-row ledger: 187 fires,
                        // CEILING 0, COST 0. NEGATIVE RESULT. Predicted
                        // mut-slice-struct-lifetime-transmute--c17 (and
                        // predicted that --t17 would NOT close, since it
                        // writes through a `&mut [&i64]` slice local). Neither
                        // closed: the array half of `lifereg.B` is not blocked
                        // HERE. Population is 2 rows, so RULE 4 applies — this
                        // refutes the spelling, not the observation that an
                        // array element is root's own storage.
                        // PROBE lifereg_indexstore: the bail's own stated
                        // reason is "writes through a pointer, not root's
                        // storage". An element of a fixed-size ARRAY local IS
                        // root's storage, so the exemption over-covers.
                        else if (logos::probe::on("lifereg_indexstore") &&
                                 c.kind() == EC::IndexRead &&
                                 TypeRef(EIndexReadView{c}.receiver().type(pool)).kind()
                                     == LogosType::Kind::Array)
                            c = EIndexReadView{c}.receiver();
                        else break;
                    }
                    if (c && c.kind() == EC::VarRef &&
                        atv.inner().kind() != EC::VarRef) {
                        std::string root(EVarRefView{c}.name());
                        uint32_t root_slot = EVarRefView{c}.var_slot();  // Phase-1
                        if (var_has(root_slot, root) && !param_names_.count(root)) {
                            add_ref_sources(root, std::string{}, v.value(), ln);
                            // B87 AT THE FIELD DOOR. The Let and Assign arms
                            // deposit into dropck_borrow_sources_; this door —
                            // the one `s.f = &x` actually takes — did not, so a
                            // Drop-carrying holder written FIELD-WISE was
                            // invisible to the B87 rule that already fires on
                            // the whole-value assign.
                            //
                            // ⚠ THE RECORD IS PER FIELD PATH, NOT PER ROOT, AND
                            // THAT IS THE WHOLE DIFFERENCE. The probe this
                            // landed from wrote `dropck_borrow_sources_[root] =
                            // …`, and its own author flagged the replace as a
                            // rule-7 caveat. MEASURED before landing, on a
                            // one-variable pair that differs only in the ORDER
                            // OF TWO FIELD WRITES:
                            //   w.a = &inner; w.b = &o;   replace: rc 0 — LOST
                            //   w.b = &o;     w.a = &inner; replace: rc 1
                            // identical fire counts (2 and 2), opposite
                            // verdicts, decided by which field was written
                            // last. Both are the same E0597. Per-path keying
                            // reports BOTH; the pair is pinned in
                            // fail/bc_dropck_field_two_paths_fail and
                            // fail/bc_dropck_field_two_paths_swapped_fail.
                            //
                            // ⚠ AND A NAIVE APPEND WOULD HAVE BEEN WORSE THAN
                            // THE REPLACE. `w.a = &inner; w.a = &o;` rewrites
                            // ONE path, so an appending merge keeps `inner` and
                            // refuses a program the replace admits. A write
                            // therefore REPLACES ITS OWN PATH and MERGES ACROSS
                            // PATHS — measured in both directions, and pinned
                            // in pass/bc_dropck_field_same_path_rewrite_admit.
                            //
                            // PRICED AS THE MERGE, NOT AS THE PROBE: CEILING 2,
                            // COST 0, 16 fires over the 375-row ledger and the
                            // 807-program legal corpus — the same numbers the
                            // replace form scored, so the correctness came free.
                            if (struct_is_dropck_relevant(TypeRef(c.type(pool)))) {
                                std::vector<RefSrc> dsrcs;
                                collect_borrow_locals(v.value(), dsrcs);
                                if (!dsrcs.empty()) {
                                    // The field path from `root` down to the
                                    // place written. An INDEX step is not a
                                    // static path component (this file's own
                                    // convention, take_field_borrow), so it
                                    // names the container whole — spelled here
                                    // as a `[]` segment so `a[i].p` and `a.p`
                                    // cannot collide.
                                    std::vector<std::string> segs2;
                                    for (ExprRef q = atv.inner(); q && q != c;) {
                                        if (q.kind() == EC::FieldRead) {
                                            segs2.emplace_back(EFieldReadView{q}.field());
                                            q = EFieldReadView{q}.receiver();
                                        } else if (q.kind() == EC::TupleIndex) {
                                            segs2.emplace_back(
                                                std::to_string(ETupleIndexView{q}.index()));
                                            q = ETupleIndexView{q}.receiver();
                                        } else if (q.kind() == EC::IndexRead) {
                                            segs2.emplace_back("[]");
                                            q = EIndexReadView{q}.receiver();
                                        } else break;
                                    }
                                    std::string fp2;
                                    for (auto it2 = segs2.rbegin(); it2 != segs2.rend(); ++it2) {
                                        if (!fp2.empty()) fp2.push_back('.');
                                        fp2 += *it2;
                                    }
                                    dropck_field_srcs_[root][fp2] = std::move(dsrcs);
                                    std::vector<RefSrc> flat;
                                    for (auto& [pk, pv] : dropck_field_srcs_[root])
                                        for (auto& sc : pv)
                                            if (std::find(flat.begin(), flat.end(), sc) == flat.end())
                                                flat.push_back(sc);
                                    dropck_borrow_sources_[root] = std::move(flat);
                                    dropck_binding_line_[root] = ln;
                                }
                            }
                            // #86 MISS 1 / SITE b — THE FIELD/TUPLE WRITE, and
                            // THIS is the door the spelling actually takes.
                            // `w.v = o.as_str()` and `t.0 = o.as_str()` both
                            // lower to SDerefWrite(AddrOfTemp(FieldRead/
                            // TupleIndex(VarRef w)), val) — the Code::FieldWrite
                            // / Code::TupleWrite arms below are the OTHER
                            // spelling, and a probe over the whole corpus
                            // counted ZERO fires of a hook placed there (see
                            // the ledger). Both arms carry the call so the
                            // rule is stated once per door, but this is the
                            // one the runtime-confirmed UAF goes through.
                            // ── #86 MISS-A: DEPOSIT ON THE PLACE ROOT ─────
                            //
                            // THE DEFECT (runtime-confirmed UAF, measured rc 0
                            // at 7b72b89c+#86):
                            //   let mut w: W = W { v: "" };
                            //   let r: &mut W = &mut w;
                            //   r.v = o.as_str();  return w.v;
                            // This door DID fire ([#86trace-derefwrite] var=r
                            // loc=1) — it deposited on the NAME BEING WRITTEN
                            // THROUGH. `r` is a `&mut` REBORROW local, so the
                            // storage written is `w`'s, and `return w.v` found
                            // `prov_["w"]` empty. Controls at the same tree:
                            // `w.v = o.as_str()` direct → rc 1; `return r.v`
                            // → rc 1 (so the record landed, on the wrong name).
                            //
                            // The re-home is the one the LOAN channel already
                            // does for the same statement, two dozen lines
                            // below: `place_write_root(atv.inner(), …)` resolves
                            // a reborrow local to its referent. Reusing it
                            // (rather than walking again) is deliberate — a
                            // second walker that drifts from the first is the
                            // defect this file has already paid for twice
                            // (U0/U1, and `reborrow_referent` × 3).
                            //
                            // ⚠ THE BASE, NOT THE DOTTED PLACE — measured, and
                            // the first attempt got it wrong in BOTH
                            // directions. `place_write_root(atv.inner(), …)`
                            // resolves the whole dotted place `r.v`, and
                            //   • with resolve ON, `h.r = &mut vs` (F0's
                            //     retarget) resolves `h.r` to `vs` and would
                            //     deposit the escape on the REFERENT;
                            //   • with resolve OFF (the loan channel's
                            //     `!is_ref_kind(place type)` test) `r.v =
                            //     o.as_str()` is ALSO "a retarget" — `str` is
                            //     a reference kind — so the re-home never ran
                            //     and the trace still read `var=r` (measured:
                            //     rc still 0).
                            // The fact needed here is narrower and is the one
                            // the recvstore door already uses, verbatim: the
                            // BASE binding's reborrow endpoint, then that
                            // endpoint's place root. `r` → `w`; `h` → `h`.
                            std::string eroot86 = rehome_reborrow(root);
                            if (!eroot86.empty() && !var_has(NO_SLOT, eroot86))
                                eroot86 = ref_place_root(eroot86);
                            if (eroot86.empty()) eroot86 = root;
                            note_holder_escape_prov(eroot86, holder_ty_of(eroot86),
                                                    v.value(), ln, "derefwrite");
                        }
                    }
                    // Door A: the loan counterpart. `*<place> = c.mk()` — the
                    // place's root binding holds the stored borrow. Uses the
                    // FULL place walk (field / tuple / index / deref), not the
                    // pure-field one above: `a[i] = …` and `(*r).f = …` both
                    // land here and both store into their root.
                    // ── D1 round 9 / F0: A RETARGET IS NOT A WRITE THROUGH ─
                    //
                    // THE DEFECT (measured, over-refusal, pre-existing at
                    // HEAD): `let mut h: Inner = Inner { r: &mut vs };
                    // h.r = &mut vs2; return vs2.len();` refuses with "cannot
                    // use 'vs2' while it is mutably borrowed", and so does the
                    // named legal control that USES h.r in between — while the
                    // struct-literal, plain-local and plain-local-ASSIGN
                    // spellings of the same fact all admit. The brief's root
                    // ("a loan that is never released") is REFUTED by the
                    // trace: both loans release at the end of this very
                    // statement. Two real causes, both here:
                    //
                    // (a) THE DESTINATION. `place_write_root` resolves `h.r`
                    //     through the reborrow map to the REFERENT `vs`, so
                    //     re-pointing the field was booked as a write THROUGH
                    //     it — which is what made `vs` a co-holder of a borrow
                    //     of `vs2` and produced the overlap case's second
                    //     diagnostic. A write to a place whose own type is a
                    //     REFERENCE replaces that reference: the storage
                    //     written is `h`, and the map entry for `h.r` must be
                    //     re-recorded onto the new referent (the FieldWrite
                    //     door does this; this door — the one the spelling
                    //     actually takes — never did, so the old identity
                    //     `h.r == vs` survived the retarget).
                    //
                    // (b) THE ORDER. This is the only one of the seven
                    //     place_write_loans callers that records BEFORE it
                    //     visits the value, so the `&mut vs2` borrow is
                    //     already registered when `visit`'s AddrOf arm runs
                    //     check_live(vs2) — the diagnostic is reported on the
                    //     flag the same statement just set. That is exactly
                    //     the self-conflict the AddrOfTemp arm documents
                    //     ("visit inner FIRST for both branches"), and the fix
                    //     is the same one: visit, then record.
                    bool wref = false;
                    TypeRef pt = atv.inner() ? atv.inner().type(pool) : TypeRef(nullptr);
                    bool retarget = is_ref_kind(pt);
                    std::string wroot = place_write_root(atv.inner(), wref,
                                                         /*resolve=*/!retarget);
                    if (retarget) {
                        wref = false;
                        BorrowPlace bp = extract_borrow_place(atv.inner(), pool);
                        if (!bp.root.empty() && !bp.path.empty() && !bp.index_in_chain) {
                            std::string wplace = bp.root + "." + bp.path;
                            release_place_retarget(wplace, bp.root);
                            note_reborrow_place(wplace, v.value());
                        }
                    }
                    visit(v.ptr(),   /*consuming=*/false, ln);
                    visit(v.value(), /*consuming=*/true,  ln);
                    if (!wroot.empty())
                        place_write_loans(wroot, v.value(), ln,
                                          wref || prov_.count(wroot) > 0);
                    break;
                }
                // ── #86 MISS-C: THE INDEX-ASSIGN, WHICH WAS NEVER ASKED ──
                //
                // `v[0i64] = o.as_str(); return v;` — rc 0, a
                // runtime-confirmed dangle in the same three lines as MISS-B.
                // MEASURED: it is neither of the two doors that were looked
                // at. It does not reach `Code::IndexWrite` and it is not an
                // `AddrOfTemp` place — sema lowers it to
                //   SDerefWrite(ptr = MethodCall(index_mut, recv=AddrOf(v),
                //                                args=[0i64]),
                //               value = o.as_str())
                // so the whole AddrOfTemp branch above is skipped and the
                // §B6/holder deposits inside it never run. The receiver-store
                // door does see `index_mut`, but its ARGUMENT is the INDEX
                // (measured `m=index_mut atk=I64`); the stored value is not an
                // argument of that call at all.
                //
                // THE RULE, and it needs no element-type test and no summary:
                // a `&mut self` method returning a REFERENCE hands back a
                // borrow OF THE RECEIVER (Rust's elision says exactly this),
                // so a write through that reference stores into the receiver.
                // If the written value may carry a borrow, the receiver's root
                // is now its holder — the same fact `note_holder_escape_prov`
                // records everywhere else, deposited on the same resolved
                // root (`flow_operand_root` + the reborrow re-home).
                if (auto pm = v.ptr();
                    pm && pm.kind() == EC::MethodCall && v.value()) {
                    lir_view::EMethodCallView mv{pm};
                    auto mrecv = mv.receiver();
                    if (mrecv && method_self_kind(mv) == 2 &&
                        type_may_carry_borrow(v.value().type(pool))) {
                        std::string cr0 = mrecv.kind() == EC::VarRef
                            ? std::string(EVarRefView{mrecv}.name())
                            : flow_operand_root(mrecv);
                        std::string cr = rehome_reborrow(cr0);
                        if (!cr.empty() && !var_has(NO_SLOT, cr))
                            cr = ref_place_root(cr);
                        if (!cr.empty() && var_has(NO_SLOT, cr))
                            note_holder_escape_prov(cr, holder_ty_of(cr),
                                                    v.value(), ln, "derefwrite");
                    }
                }
                // ── `*r = v`: THE WRITE QUESTION WAS NEVER ASKED ──────
                // The AddrOfTemp branch above is the ONLY place this arm asks
                // whether the written place is writable; MEASURED, `ptr.kind`
                // is AddrOfTemp for `r.f = v` and VarRef for `*r = v`, so the
                // plainest deref write in the language reaches none of it.
                // What refuses the field spelling is visit()'s own AddrOfTemp
                // arm (its `is_mut && shared_borrows > 0` report), reached from
                // the `visit(v.ptr(), …)` two lines down; the VarRef spelling
                // reaches visit()'s VarRef arm instead, which runs check_live
                // plus field_borrow_conflicts with need_exclusive=FALSE — the
                // READ form. check_live is the wrong check by construction (it
                // tests dangling/moved/mut_borrowed and deliberately NOT
                // shared_borrows, because USING a value under a shared borrow
                // is legal and only WRITING is not), and three attempts to
                // route this through it were measured wrong. The missing half
                // is the shared-borrow / exclusive-field conflict on the
                // WRITTEN PLACE, and the predicate for it already exists:
                // check_recv_conflict, the whole-root mutable-use check.
                // Delegate; do not restate.
                //
                // The place written is `*ptr`, but extract_borrow_place(ptr)
                // and extract_borrow_place(Deref(ptr)) agree on every field
                // this consumer reads: the walker's Deref arm roots THROUGH a
                // reference to the reference variable and clears the path, so
                // root / root_slot / root_type / path are identical and only
                // `through_ref` (unread here) differs. One walker, no drift.
                //
                // NARROWING IS BY THE PREDICATE'S OWN GUARDS, not by a kind
                // test here — which is what keeps this from over-refusing:
                //   • root_type Kind::Ptr  → returns (raw `*p = v` unchecked,
                //     Rust parity; the stdlib's dominant spelling here),
                //   • !bp.path.empty()     → returns, so `r.f = v` / `a[i] = v`
                //     keep exactly today's route and today's diagnostics,
                //   • root empty           → returns, so the index_mut
                //     MethodCall ptr is untouched.
                // is_mut is unconditionally true: a DerefWrite is a write.
                //
                // ── THE POINTER'S PATH IS NOT THE WRITTEN PLACE'S PATH ──
                // The paragraph above is right about `*r = v` and wrong about
                // `*h.r = v` (r: &mut i64 in a FIELD). There the ptr is a
                // FieldRead, so the place comes back root=h path="r" and
                // check_recv_conflict returns at `!bp.path.empty()` — a guard
                // whose stated reason ("field places are refused by visit()'s
                // AddrOfTemp arm instead") is TRUE of `r.f = v`, where the
                // non-empty path describes the WRITTEN place, and FALSE here,
                // where it describes WHERE THE POINTER LIVES. Nothing else
                // sees this write: ptr.kind is FieldRead so the AddrOfTemp
                // branch above is skipped, and the `visit(v.ptr(), ...)` below
                // reaches visit()'s FieldRead arm, whose field_borrow_conflicts
                // runs with need_exclusive=FALSE — the READ form, and reading
                // `h.r` IS legal. MEASURED rc 0 where the LOCAL twin is rc 1.
                // The place was never wrong; the QUESTION was.
                // CEILING PROBE `dwatunwrap` — the place handed to
                // check_place_mut_use is computed from `v.ptr()`, and for the
                // `s.f = v` spelling that is an AddrOfTemp, on which
                // extract_borrow_place BREAKS: root empty, through_ref_type
                // null, so the landed "behind a `&` reference" rule (E0594)
                // cannot run for a plain field write. The `*h.r = v` spelling
                // hands it a bare FieldRead and is checked. Unwrap one hop.
                {
                    auto cpm_ptr = v.ptr();
                    if (logos::probe::on("dwatunwrap") && cpm_ptr &&
                        cpm_ptr.kind() == EC::AddrOfTemp)
                        cpm_ptr = lir_view::EAddrOfTempView{cpm_ptr}.inner();
                    check_place_mut_use(extract_borrow_place(cpm_ptr, pool),
                                        cpm_ptr ? cpm_ptr.type(pool) : TypeRef{},
                                        ln);
                }
                visit(v.ptr(),   /*consuming=*/false, ln);
                visit(v.value(), /*consuming=*/true,  ln);
                break;
            }

            // ── Tuple field write: var.N = value ──────────────────────────
            case Code::TupleWrite: {
                STupleWriteView v{sr};
                std::string tw_nm(v.receiver());
                check_live(tw_nm, ln);
                visit(v.value(), /*consuming=*/true, ln);
                add_ref_sources(tw_nm, std::to_string(v.index()),
                                v.value(), ln);  // §B6 (F6: at the PLACE)
                // ⚠ #86 MISS 1 — no hook here either; see Code::FieldWrite.
                // `t.0 = …` lowers to DerefWrite(AddrOfTemp(TupleIndex(…))).
                place_write_loans(tw_nm, v.value(), ln,   // Door A
                                  /*through_ref=*/prov_.count(tw_nm) > 0);
                break;
            }

            // ── let-else: `let Some(b) = ob else { … };` ──────────────────
            // D1 round 2, Door C. visit_stmt had NO case for this statement at
            // all: the scrutinee was never visited, the bindings never
            // declared, and propagate_pat_loans therefore never ran — so the
            // let-else spelling of the match extraction leaked what the match
            // spelling refuses (Ct1_matchout_twin, the same program written as
            // `match`, rc=1 throughout). Nothing here is new machinery: it is
            // the Match arm's three steps (visit scrutinee, declare bindings,
            // propagate §B6 sources + D1 loans) minus the arm loop.
            //
            // The bindings live in the ENCLOSING scope — that is the whole
            // point of let-else — so they are declared with no push_scope. The
            // else block MUST diverge (sema enforces it), so its state does not
            // reach the following statements: visit it for its own diagnostics,
            // then restore.
            case Code::LetElse: {
                SLetElseView v{sr};
                if (auto sc = v.scrut()) {
                    visit(sc, /*consuming=*/false, ln);
                    std::vector<std::string> srcs;
                    collect_ref_sources(sc, srcs);
                    std::vector<std::string> roots;
                    if (type_may_carry_borrow(sc.type(pool)))
                        bc_hop_roots(sc, roots);
                    // H8 — spelling 3 of 3: `let Some(b) = <temporary> else`.
                    if (auto st = retain_temp_scrut_loan(sc, ln); !st.empty())
                        roots.push_back(std::move(st));
                    declare_pat_bindings(v.pat());
                    propagate_pat_sources(v.pat(), srcs, ln);  // §B6
                    propagate_pat_prov(v.pat(), v.scrut());   // D1 r3
                    propagate_pat_loans(v.pat(), roots, ln);   // D1
                    propagate_pat_reborrows(v.pat(), sc);      // D1 r13
                    propagate_pat_borrows(v.pat(), sc, ln);
                } else {
                    declare_pat_bindings(v.pat());
                }
                v.each_guard([&](ExprRef g) { visit(g, /*consuming=*/true, ln); });
                if (auto eb = v.else_block()) {
                    auto saved_s = states_;
                    auto saved_p = prov_;
                    bool saved_div = cur_diverged_;
                    cur_diverged_ = false;
                    visit_block(eb);
                    states_ = saved_s;
                    prov_   = saved_p;
                    cur_diverged_ = saved_div;
                }
                break;
            }

            // ── SDrop — compiler-generated, no-op in borrow checker ───────
            case Code::Drop:
                break;

            // ── If / else ────────────────────────────────────────────────
            case Code::If: {
                SIfView v{sr};
                visit(v.cond(), /*consuming=*/true, ln);
                auto saved_s = states_;
                auto saved_p = prov_;
                bool saved_div = cur_diverged_;
                cur_diverged_ = false;
                if (auto then_b = v.then_block()) visit_block(then_b);
                auto then_s = states_;
                auto then_p = prov_;
                bool then_div = cur_diverged_;
                states_ = saved_s;
                prov_   = saved_p;
                cur_diverged_ = false;
                if (auto else_b = v.else_block()) visit_block(else_b);
                bool else_div = cur_diverged_;
                // A diverged branch (return/break/continue at the join point)
                // contributes nothing to the post-if move state — only the
                // surviving branch propagates. If BOTH diverged, the whole if
                // diverges. If ELSE diverged, the post-if state IS then's.
                if (then_div && else_div) {
                    cur_diverged_ = true;
                } else if (then_div) {
                    cur_diverged_ = saved_div;  // states_ = else's
                } else if (else_div) {
                    states_ = then_s;
                    prov_   = then_p;
                    cur_diverged_ = saved_div;
                } else {
                    merge_moves(states_, then_s);
                    // A loan raised on EITHER path keeps its record; its
                    // counters must survive the state restore too (J0).
                    merge_loans(states_, then_s);
                    merge_provs(prov_,   then_p);
                    cur_diverged_ = saved_div;
                }
                break;
            }

            // ── While loop ───────────────────────────────────────────────
            case Code::While: {
                SWhileView v{sr};
                visit(v.cond(), /*consuming=*/true, ln);
                if (auto b = v.body()) visit_loop_body(b, {}, v.label());
                break;
            }

            // ── For range loop ───────────────────────────────────────────
            case Code::For: {
                SForView v{sr};
                visit(v.lo(), /*consuming=*/true, ln);
                visit(v.hi(), /*consuming=*/true, ln);
                if (auto b = v.body())
                    visit_loop_body(b, {std::string(v.var())}, v.label());
                break;
            }

            // ── Infinite loop ─────────────────────────────────────────────
            case Code::Loop: {
                SLoopView v{sr};
                if (auto b = v.body())
                    visit_loop_body(b, {}, v.label(), {}, v.break_slot());
                break;
            }

            // ── Scoping block ─────────────────────────────────────────────
            // D1 round 2, Door G. A destructuring `let` has no statement kind
            // of its own: sema's lower_let_pat emits a TRANSPARENT SBlock
            // wrapping `let __destruct_0 = <rhs>; let __destruct_1 =
            // __destruct_0; let b = __destruct_1.0; let n = __destruct_1.1;`
            // (FieldRead / IndexRead accessors for the struct- and
            // array-pattern spellings; the same shape in all four). borrow_check
            // pushed a scope for it anyway, so `b` and `n` were declared in a
            // frame that closed one statement later and their loans died there
            // — while sema had put those very bindings in the OUTER scope. The
            // twins prove the loan itself is recorded: `let t = (c.mk(), 5);
            // let b = t.0;` refuses (Gt1), and so does the array spelling
            // (Gt2); only the destructured form leaked.
            //
            // `transparent` is a CARRIED FACT, not a heuristic: sema sets it
            // exactly on wrappers it synthesised for bindings that belong to
            // the enclosing scope. Honouring it makes the LIR agree with the
            // scope sema already assigned. This is NOT an alternative to Door
            // B's re-homing — B1/B2/B3/B4 are user-written blocks that are
            // genuinely opaque and stay so; each fix closes shapes the other
            // cannot (measured: after B, G1/G2/G3 were still rc=0).
            case Code::Block: {
                SBlockView bv{sr};
                if (auto b = bv.body()) {
                    if (bv.transparent()) {
                        // ONE release at the end, not one per inner statement:
                        // the wrapper is a single SOURCE statement, so all four
                        // lets carry the same line and the NLL cursor would
                        // retire `__destruct_0`'s loan the moment the spill was
                        // recorded — before `b` could inherit it. Measured: the
                        // transparent branch fired on G1/G2/G3 and all three
                        // still compiled until the release moved out here.
                        release_dead_borrows(
                            walk_stmts_releasing(b, /*defer_release=*/true));
                    } else {
                        next_scope_is_bare_block_ = true;   // D3
                        visit_block(b);
                    }
                }
                break;
            }

            // ── For-each loop ─────────────────────────────────────────────
            case Code::ForEach: {
                SForEachView v{sr};
                // ── CEILING PROBE `foreachitermove` — `for n in v` where
                // `v: &mut Vec<T>` MOVES `v` in Rust: `IntoIterator for &mut
                // Vec<T>` takes `self` BY VALUE, and `&mut T` is not Copy. This
                // arm visits the iterable NON-consuming, so two consecutive
                // `for n in v { … }` loops over one `&mut` compile (E0382).
                // ⚠ SMALL POPULATION (rule 4): coverage 2026-08-28 gives this
                // switch arm 104 arrivals over the whole instrumented run, so a
                // zero here is NOT a refutation of the mechanism.
                // ── MEASURED 2026-08-29: CEILING 0 / COST 0, SITE PROVEN LIVE
                // AND THE PREDICATE TRUE. issue-83924 fires the probe TWICE —
                // once per `for n in v` — so the arm is reached and the
                // iterable IS `&mut Vec<i64>`, and the program still compiles
                // rc 0. `visit` consumes a move type only at a bare VarRef, so
                // the iterable is not one: sema has wrapped it, the same
                // implicit-reborrow wrap that defeats `mrgenerictv` and
                // `mrletann`. Confirmed on a 9-line repro with two loops over
                // one `&mut Vec`: 2 fires, rc 0 armed and unarmed. All three
                // `&mut T is not Copy` rows are blocked by ONE upstream fact,
                // not three; the next round belongs at the WRAP, not here.
                bool p_fim = logos::probe::on("foreachitermove") &&
                             v.iter() && v.iter().type(pool) &&
                             v.iter().type(pool).kind() ==
                                 LogosType::Kind::MutRef;
                visit(v.iter(), /*consuming=*/p_fim, ln);
                if (auto b = v.body())
                    // SForEachView carries no label accessor; unlabeled
                    // break/continue (the common case) still target it as the
                    // innermost frame.
                    visit_loop_body(b, {std::string(v.var())});
                break;
            }

            // ── Match statement ───────────────────────────────────────────
            case Code::Match: {
                SMatchView v{sr};
                check_whole_read_vs_field_loans(v.scrut(), ln);
                visit(v.scrut(), /*consuming=*/false, ln);
                std::vector<std::string> scrut_sources;  // §B6: borrows held by scrut
                collect_ref_sources(v.scrut(), scrut_sources);
                // D1: the LOAN channel needs the scrutinee's holder bindings
                // too — a pattern binding is an EXTRACTION out of the
                // scrutinee, the same hop as `ob.unwrap()`.
                std::vector<std::string> scrut_hop_roots;
                if (type_may_carry_borrow(v.scrut().type(pool)))
                    bc_hop_roots(v.scrut(), scrut_hop_roots);
                // H8 — spelling 1 of 3: the match STATEMENT. Taken once here,
                // before the arm loop, so N arms cannot take N loans.
                if (auto st = retain_temp_scrut_loan(v.scrut(), ln); !st.empty())
                    scrut_hop_roots.push_back(std::move(st));
                auto saved_s = states_;
                auto saved_p = prov_;
                // §B6: snapshot the borrow-source / dangling maps so each arm
                // starts from the pre-match state, then UNION the arms' results
                // (a binding borrows X / dangles if ANY arm makes it so — Rust
                // requires a borrow valid on every path).
                auto saved_rbs  = ref_borrow_sources_;
                auto saved_rbl  = ref_borrow_line_;
                auto saved_dang = dangling_;
                decltype(ref_borrow_sources_) acc_rbs;
                decltype(ref_borrow_line_)    acc_rbl;
                decltype(dangling_)           acc_dang;
                std::optional<StateMap> merged_s;
                std::optional<ProvMap>  merged_p;
                bool any_arm = false, all_diverged = true;
                // Guards are evaluated in source order until one matches, so a
                // value a guard moves is moved for every LATER guard/arm too.
                // `guard_acc` carries the moves earlier guards made (outer vars
                // only) into each subsequent arm's start state, so a second arm
                // whose guard re-moves the value is caught (Rust E0382). Bodies
                // stay mutually exclusive — only guard-caused moves accumulate.
                // ── E0510 — A MATCH GUARD RUNS WITH THE SCRUTINEE BORROWED
                // A guard is evaluated AFTER an arm has been provisionally
                // chosen and BEFORE its body runs, so a guard that assigns to
                // the matched place, takes it by `&mut`, or moves it makes the
                // choice already taken a lie. rustc raises a fake shared borrow
                // over the guard for exactly that; logosc raised one only where
                // a `ref`/`ref mut` BINDING existed, so
                // `match x { Some(_) if { x = None; false } => … }` compiled.
                //
                // What is borrowed is `collect_tested_paths` — the places the
                // patterns COMPARE, not the scrutinee as a whole — because the
                // whole-place version refuses legal programs; its note carries
                // the three counter-examples. The union over all arms is taken
                // ONCE here, not per arm: a guard holds the whole match's
                // reads, including the arms above it.
                //
                // ⚠ AN INDEXED SCRUTINEE IS EXEMPT. `match a[i] { … }` borrows
                // an ELEMENT, and our place algebra cannot tell `a[j]` from it:
                // the whole-array loan our loan channel would raise refuses
                // `match a[0] { 1 if { a[1] = 7; true } => … }`, which is legal.
                // Measured by hand, not inferred.
                BorrowPlace guard_bp{};
                std::vector<std::string> guard_tested;
                {
                    bool any_guard = false;
                    v.each_arm([&](EMatchArmRef arm) {
                        if (arm.guard()) any_guard = true;
                    });
                    if (any_guard) {
                        guard_bp = extract_borrow_place(v.scrut(), pool);
                        if (guard_bp.root.empty() || guard_bp.index_in_chain) {
                            guard_bp = BorrowPlace{};
                        } else {
                            v.each_arm([&](EMatchArmRef arm) {
                                collect_tested_paths(arm.pat(), guard_bp.path,
                                                     guard_tested);
                            });
                            if (guard_tested.empty()) guard_bp = BorrowPlace{};
                        }
                    }
                }
                StateMap guard_acc = saved_s;
                v.each_arm([&](EMatchArmRef arm) {
                    any_arm = true;
                    states_ = guard_acc;
                    prov_   = saved_p;
                    ref_borrow_sources_ = saved_rbs;
                    ref_borrow_line_    = saved_rbl;
                    dangling_           = saved_dang;
                    bool saved_div = cur_diverged_;
                    cur_diverged_ = false;
                    push_scope();
                    declare_pat_bindings(arm.pat());
                    // ── CEILING PROBE `patdropdestr` — E0509. A by-value
                    // pattern binding cannot move a field OUT of a value whose
                    // own type impls Drop; the destructor still owes a call on
                    // the whole value. `needs_drop` exists and is consulted for
                    // dropck liveness, never for destructuring, so
                    // `let S { v } = s;` over a Drop-impl S compiles today.
                    // Gated on the SCRUTINEE'S OWN Drop impl (ts_.drop_types),
                    // not has_droppable_fields: a struct that does not impl
                    // Drop but whose FIELD does may be partially moved legally.
                    if (logos::probe::on("patdropdestr")) {
                        TypeRef sct = v.scrut().type(pool);
                        bool own_drop =
                            sct && ((sct.kind() == LogosType::Kind::Struct &&
                                     ts_.drop_types.count(std::string(sct.struct_name()))) ||
                                    (sct.kind() == LogosType::Kind::Enum &&
                                     ts_.drop_types.count(std::string(sct.enum_name()))));
                        if (own_drop) {
                            bool byval = false;
                            each_pat_binding(arm.pat(), [&](std::string_view b, TypeRef t) {
                                if (!b.empty() && b != "_" &&
                                    is_move_type(t, prog_, ts_, &copy_tvs_)) byval = true;
                            });
                            // ── MEASURED 2026-08-28: CEILING 0, COST 1
                            // (logos_02_semantic_core_pass_drop-trait-enum-b154).
                            // A stop sign — but READ WHY. All four target rows
                            // were compiled with the probe armed and DO reach
                            // this site (2692-2693 fires each, rc=0), yet none
                            // closes: their destructuring is not in a match arm
                            // at all. `let S { f: inner } = s;`
                            // (borrowck-move-out-of-tuple-struct-with-dtor--t13)
                            // and `let S { v: inner } = *s;`
                            // (access-mode-in-closures) are LET PATTERNS. The
                            // E0509 predicate is one type test and is probably
                            // right; THE SITE IS WRONG. Whoever funds it next
                            // installs it at the let-destructuring site, not
                            // here, and prices the cost against
                            // drop-trait-enum-b154 first.
                            if (byval)
                                report(ln, "ceiling-probe patdropdestr: cannot move out of "
                                           "a type which implements Drop (E0509)");
                        }
                    }
                    propagate_pat_sources(arm.pat(), scrut_sources, ln);  // §B6
                    propagate_pat_prov(arm.pat(), v.scrut());             // D1 r3
                    propagate_pat_loans(arm.pat(), scrut_hop_roots, ln);  // D1
                    propagate_pat_reborrows(arm.pat(), v.scrut());        // D1 r13
                    propagate_pat_borrows(arm.pat(), v.scrut(), ln);
                    StateMap before_guard = states_;
                    if (auto g = arm.guard()) {
                        // ⚠ THE LOAN LIVES IN ITS OWN SCOPE FRAME, AND THAT IS
                        // THE ONLY RELEASE THAT WORKS. `release_borrows_held_by`
                        // is MUT-ONLY by a measured decision recorded at its
                        // definition (a shared loan is a COUNTER that
                        // merge_loans raises across a loop back edge with no
                        // record to release), so calling it here was a silent
                        // no-op: the guard's loan survived into the arm BODY
                        // and refused `1 if … => { p.a = 3; }` — the arm's own
                        // legal write to the place it matched on. Caught by
                        // pass/bc_guard_loan_released_admit, which exists for
                        // exactly this failure. push/pop_scope releases a
                        // SHARED count correctly; `__guard_scrut` is declared
                        // in no frame, so pop_scope's re-homing cannot keep it
                        // alive either.
                        push_scope();
                        // The loan is IMPLICIT (RecordFlags): it may sit over
                        // a place the arm's own `ref mut` binding already
                        // holds, and a report AT the record would refuse a
                        // legal guard.
                        for (const auto& tp : guard_tested) {
                            BorrowPlace bp = guard_bp;
                            bp.path = tp;
                            record_borrow(bp, /*is_mut=*/false, ln,
                                          "__guard_scrut",
                                          RecordFlags{.implicit = true});
                        }
                        visit(g, /*consuming=*/true, ln);
                        pop_scope();
                    }
                    // Fold this guard's NEW moves of outer bindings into the
                    // accumulator for the following arms' guards.
                    if (arm.guard())
                        states_.for_each([&](uint32_t slot, std::string_view name, VarState& st) {
                            if (!st.moved || !saved_s.has_id(slot, name)) return;
                            const VarState* bg = before_guard.find(slot, name);
                            if (!bg || !bg->moved) guard_acc.at_id(slot, name) = st;
                        });
                    if (auto body = arm.body()) visit_block(body);
                    pop_scope();
                    bool arm_div = cur_diverged_;
                    cur_diverged_ = saved_div;
                    // A DIVERGED arm (return/break/continue tail) contributes
                    // nothing to the post-match move state — its moves never
                    // reach the join (`None => return out` in a collect loop
                    // must not poison `out` for the next iteration; surfaced
                    // when Vec became move-classified, adversarial #2).
                    if (arm_div) return;
                    all_diverged = false;
                    // §B6: union this arm's surviving borrow-sources / danglers.
                    for (auto& [k, v2] : ref_borrow_sources_) {
                        auto& dst = acc_rbs[k];
                        for (auto& s : v2)
                            if (std::find(dst.begin(), dst.end(), s) == dst.end())
                                dst.push_back(s);
                        acc_rbl[k] = ref_borrow_line_.count(k) ? ref_borrow_line_[k] : ln;
                    }
                    for (auto& [k, d] : dangling_) acc_dang.emplace(k, d);
                    if (!merged_s) {
                        merged_s = states_;
                        merged_p = prov_;
                    } else {
                        states_.for_each([&](uint32_t slot, std::string_view name, VarState& st) {
                            if (st.moved && saved_s.has_id(slot, name))
                                merged_s->at_id(slot, name) = st;
                        });
                        // J0 — the match join used to fold only `st.moved` from
                        // arms 2..n, so arm 1's counters survived BY ACCIDENT
                        // (`merged_s = states_` copies whole VarStates) and every
                        // later arm's were discarded. Measured: the same program
                        // refused with `stash` in arm 1 and COMPILED with it in
                        // arm 2. This join never calls merge_loans through the
                        // `if` path, so fixing merge_loans alone left it open.
                        merge_loans(*merged_s, states_);
                        merge_provs(*merged_p, prov_);
                    }
                });
                if (merged_s) {
                    states_ = saved_s;
                    prov_   = saved_p;
                    merged_s->for_each([&](uint32_t slot, std::string_view name, VarState& st) {
                        if (saved_s.has_id(slot, name)) states_.at_id(slot, name) = st;
                    });
                    merge_provs(prov_, *merged_p);
                    ref_borrow_sources_ = std::move(acc_rbs);
                    ref_borrow_line_    = std::move(acc_rbl);
                    dangling_           = std::move(acc_dang);
                } else {
                    states_ = saved_s;
                    prov_   = saved_p;
                    ref_borrow_sources_ = std::move(saved_rbs);
                    ref_borrow_line_    = std::move(saved_rbl);
                    dangling_           = std::move(saved_dang);
                    if (any_arm && all_diverged) cur_diverged_ = true;
                }
                break;
            }

            // SBreak / SContinue: capture the current move-state for the target
            // loop's dataflow (break → after-loop, continue → back-edge), then
            // diverge the current stmt-flow. LetElse has no variable effects
            // here but its `else` also diverges (handled elsewhere).
            // ── D1 round 12 / A1: `break v` IS A STORE INTO THE LOOP'S SLOT ──
            //
            // THE DEFECT (measured, both oracles). `let s: &mut Vec<B> =
            // loop { break &mut vs; }; s.push(c.mk()); c.bump();` ADMITS while
            // the direct twin `let s: &mut Vec<B> = &mut vs;` REFUSES; on the
            // mask oracle `fn pickl(v: &mut Vec<B>) -> &mut Vec<B> { return
            // loop { break v; }; }` summarises `result<-0` beside the unwrapped
            // `pickd`'s TRUE `result<-0x1`.
            //
            // The hunt's shape ("add a loop-expression arm to
            // ref_source_places") is NOT implementable: there is no
            // `expr::Code` for a loop. Sema lowers a loop-as-expression to
            // `BlockExpr{ [Loop stmt], result: VarRef(__loop_val_N) }`
            // (sema_expr.cpp, la::LOOP), so the shape walker only ever sees the
            // slot's VarRef — a shape it already handles. What is missing is
            // the EDGE INTO the slot: this arm recorded only break_states and
            // cur_diverged_, so `__loop_val_N` reborrowed nothing and the loan
            // `s` later raised stopped there.
            //
            // The deposit is MONOTONE (`add`, not `set`): a loop may break from
            // several places and the slot names whatever ANY of them deposited
            // — the same union `IfExpr` gets across its two arms. Flow-
            // sensitive retraction would let the last break erase the others.
            //
            // Strictly additive: today's answer for the slot is no edge at all,
            // and a `break` with no value (or outside any loop frame, or in a
            // loop with no slot because it is not used as an expression)
            // contributes nothing, exactly as before.
            case Code::Break: {
                SBreakView bv{sr};
                if (auto* lf = loop_target(bv.label())) {
                    // r11: the state past a `break` is the state after the
                    // loop's scopes unwind — see loop_exit_snapshot.
                    lf->break_states.push_back(
                        loop_exit_snapshot(lf->outer_scope_count));
                    if (!lf->break_slot.empty() && bv.value())
                        for (auto& src : ref_sources_of(bv.value()))
                            reborrow_of_.add(lf->break_slot, src);
                }
                cur_diverged_ = true;
                break;
            }
            case Code::Continue:
                // r11: same unwind rule at the back edge — a loop-local loan
                // must not re-enter iteration 2 (see loop_exit_snapshot).
                if (auto* lf = loop_target(SContinueView{sr}.label()))
                    lf->continue_states.push_back(
                        loop_exit_snapshot(lf->outer_scope_count));
                cur_diverged_ = true;
                break;
            default:
                break;
        }
    }

public:
    BorrowChecker(SemaResult& diags, std::string fn_name,
                  const lir::LProgram& prog, const TypeSets& ts,
                  const FnIndex& fn_index,
                  bool exclusivity_only = false,
                  const RegionInferer* ri = nullptr,
                  const FlowSummaryMap* flows = nullptr)
        : diags_(diags), fn_name_(std::move(fn_name)), prog_(prog), ts_(ts),
          fn_index_(fn_index), ri_(ri), flows_(flows),
          exclusivity_only_(exclusivity_only) {}
    // D1 round 3 / F3: post-mono borrow-flow summaries (null pre-mono).
    const FlowSummaryMap* flows_ = nullptr;

    // The callee's summary for a Call / MethodCall, or null when the body is
    // not available (the documented (a)-(d) hole — every consumer then keeps
    // its pre-F3 signature-elision behaviour).
    const FlowSummary* flow_of_call(std::string_view symbol) const {
        return flows_ ? resolve_call_flow(*flows_, symbol, &fn_index_) : nullptr;
    }
    const FlowSummary* flow_of_method(lir_view::EMethodCallView v) const {
        return flows_ ? resolve_method_flow(*flows_, fn_index_,
                                           prog_.type_pool.impl(), v)
                      : nullptr;
    }
    // P2-10: when checking GENERIC templates pre-mono, move/use-after-move
    // tracking is imprecise (TypeVar values + generic method-call move
    // semantics → false positives like a spurious "use of moved 'out'"). In
    // that mode we report only borrow-exclusivity conflicts (which are sound
    // without concrete types) and suppress move-related diagnostics — the
    // concrete moves are fully checked on the monomorphized specializations.
    bool exclusivity_only_ = false;

    // ── #86 MISS-E: A NAME THAT APPEARS IN NO SOURCE FILE ──────────────────
    //
    // DIAGNOSTIC ONLY — nothing in this map is read by any verdict. It exists
    // because sema's `make_return_with_drops` rewrites `return <expr>;` into
    // `let __ret_tmp_0 = <expr>; <drops>; return __ret_tmp_0;` whenever the
    // frame owns something droppable, so by the time check_return_value runs,
    // the expression that NAMED the borrow source is gone and the returned
    // node is a bare VarRef to a compiler temp. #77 round 2 repaired the
    // common case by asking §B6 (`ref_sources_under`), and TWO shapes still
    // leaked at that repair's own tree — both pinned on the exact string:
    //   • tests/logos/fail/bc_esc_holder_return_chained_dangle — the CHAIN
    //     `return mk(o.as_str()).get();`, whose §B6 entry is empty because
    //     that channel has no arm for a chained call;
    //   • tests/logos/fail/wany_escapes_rc_container — `return e;` where the
    //     Ref was built from an Rc-held arena.
    // `bc_hop_roots` — the existing walk for "which bindings does this
    // expression's value hop out of" — answers both at the LET, which is the
    // one point where the initializer still exists. Recorded there, read only
    // by the message.
    std::unordered_map<std::string, std::vector<std::string>> ret_temp_roots_;

    void check(lir_view::FunctionView fn) {
        auto* fn_pool = prog_.type_pool.impl();
        // No body mirror ⇒ a body-less function (extern / metaprog stub /
        // from_binary_module — emit_function skips these). Nothing to borrow-check;
        // skip before any block walk so block_ref(fn.body) is never a null view.
        auto fn_body = fn.body();
        if (!fn_body) return;
        states_.reset(fn.local_count());  // Phase-1: size the dense slot vector
        scopes_.clear();
        prov_.clear();
        holder_ty_.clear();          // #86 MISS 1 — per-FUNCTION, like prov_
        ret_temp_roots_.clear();     // #86 MISS-E — diagnostic only
        reborrow_of_.clear();
        reborrow_mut_.clear();
        reborrow_prescan_.clear();   // G0
        fnptr_sym_.clear();          // G1
        fnptr_multi_.clear();        // G1
        closure_caps_.clear();
        param_names_.clear();
        param_byval_.clear();
        param_lifetimes_.clear();
        last_use_line_.clear();
        last_use_slot_.clear();        // F5 — slots are per-FUNCTION dense ids
        last_use_unslotted_.clear();
        stmt_pt_.clear();              // #75 — points are per-FUNCTION
        line_ord_.clear();
        cur_slot_of_.clear();
        max_line_seen_ = 0;   // per-fn: a prior fn's higher lines must not
                              // count as "already visited" for NLL release
        // Per-body ref / dropck / dangling tracking. Borrow-checking is
        // strictly per-function (Rust never crosses fn bodies), so these are
        // per-function state — but they were never reset between functions.
        // That (1) leaked entries across EVERY function in the program, so
        // pop_scope's full-map scans over ref_borrow_sources_ /
        // dropck_borrow_sources_ grew O(n²) in total program size, and (2)
        // was a latent soundness hole: a stale entry from a prior function
        // with a colliding variable name could mis-fire (or suppress) a
        // dangling/dropck diagnostic. Clear them per function.
        ref_borrow_sources_.clear();
        ref_borrow_line_.clear();
        dangling_.clear();
        dropck_borrow_sources_.clear();
        dropck_binding_line_.clear();
        dropck_field_srcs_.clear();
        param_inner_lifetimes_.clear();
        // Type-params carrying an explicit `Copy` bound — a bare TypeVar is
        // move UNLESS it is Copy (Rust generic-body semantics). Drives
        // is_move_type's TypeVar leaf so the partial-move tracker fires on
        // `s.a: T` in generic templates (Tier 1). See DIVERGENCES §B1.
        copy_tvs_.clear();
        fn.each_type_param([&](lir_view::FnTParamView tp) {
            std::string tpname(tp.name());
            tp.each_bound([&](lir_view::FnTraitBoundView b) {
                if (b.trait_name() == "Copy") copy_tvs_.insert(tpname);
            });
        });
        fn_lifetime_params_.clear();
        for (auto lp : fn.lifetime_params()) fn_lifetime_params_.push_back(std::string(lp));
        {
            std::vector<std::pair<std::string, std::string>> lo;
            for (auto& [a, b] : fn.lifetime_outlives())
                lo.emplace_back(std::string(a), std::string(b));
            outlives_adj_ = outlives_adj(lo);
        }
        ret_type_ = fn.ret_type(fn_pool);

        // G0: TWO passes. The first builds `reborrow_prescan_` (and the last-use
        // maps); the second re-notes uses with the alias set COMPLETE, so a
        // reborrow recorded textually after a use still extends the referent.
        // `note_use` is a max, so the repeat is idempotent on everything else.
        scan_uses_block(fn_body);
        scan_uses_block(fn_body);

        push_scope();  // function scope
        for (auto& p : fn.params()) {
            std::string pname(p.name());
            TypeRef ptype = p.type(fn_pool);
            declare_var(pname, p.slot());  // Phase-1
            param_names_.insert(pname);
            if (!(is_ref_kind(ptype) ||
                  TypeRef(ptype).kind() == LogosType::Kind::Ptr))
                param_byval_.insert(pname);
            // A reference, borrow-carrying, or RAW-POINTER param points at data
            // that lives outside this call (a raw pointer is unbounded / caller-
            // managed), so borrows derived from it are safe to return. Only a
            // by-value OWNED param (struct/enum/array/scalar) has call-local
            // storage.
            if (is_ref_kind(ptype) || is_borrow_carrying_type(ptype) ||
                TypeRef(ptype).kind() == LogosType::Kind::Ptr)
                outliving_params_.insert(pname);
            if (is_ref_kind(ptype)) {
                param_lifetimes_[pname] =
                    lt_is_minted(TypeRef(ptype).lifetime())
                        ? std::string{} : std::string(TypeRef(ptype).lifetime());
                // B86: capture inner-struct lifetime_args.
                auto pointee = TypeRef(ptype).pointee();
                if (pointee && (pointee.kind() == LogosType::Kind::Struct ||
                                pointee.kind() == LogosType::Kind::ZonedStruct ||
                                pointee.kind() == LogosType::Kind::Enum)) {
                    // B86: capture the pointee's explicit lt_args if any.
                    // Even when empty (e.g. `&Self` where Self isn't yet
                    // resolved with impl-level lt_args), record an empty
                    // vector — its presence signals "param points to an
                    // aggregate; trust type-checker for inner lifetime
                    // structure" in check_return_value.
                    // ⚠ A MINTED ARG WOULD ORPHAN THIS GUARD. The hatch below
                    // reads "no inner lifetime recorded" as "trust the type
                    // checker"; the mint fills a struct's absent lifetime args,
                    // so `&Self` stops being empty and the hatch stops firing.
                    // Minted names are not WRITTEN ones — see lt_is_minted.
                    std::vector<std::string> lts;
                    for (auto& lt : pointee.lifetime_args())
                        if (!lt_is_minted(lt)) lts.push_back(lt);
                    param_inner_lifetimes_[pname] = std::move(lts);
                }
            }
        }

        visit_block(fn_body);
        pop_scope();
    }
};

// Expression visitor — out-of-line.

void BorrowChecker::visit(lir_view::ExprRef e, bool consuming, uint32_t line) {
    if (!e) return;
    using namespace lir_view;
    using Code = lir_schema::expr::Code;
    const auto* pool = prog_.type_pool.impl();

    // Helper: visit a sequence of call arguments via per-view each_arg.
    // EAddrOf args (including those nested in if/match) create call-site
    // borrows released when the scope pops after the call.
    auto visit_args = [&](auto&& view) {
        push_scope();  // call-site borrow scope
        in_call_args_++;
        view.each_arg([&](ExprRef a) {
            // ── CEILING PROBE `mutrefargmove` — `&mut T` is not Copy: handing
            // one to a BY-VALUE param moves it. sema's
            // try_implicit_reborrow_mut wraps a `&mut` into
            // AddrOfTemp(Deref(VarRef)) at every ref-shaped formal and DECLINES
            // when the formal is a by-value TypeVar — so a surviving bare
            // VarRef of MutRef type in argument position MEANS "sema said this
            // is not a reborrow".
            // ── MEASURED 2026-08-28: CEILING 0, COST 0 — AND IT IS A SITE
            // ARTEFACT, NOT A REFUTATION (the fourth in this family, after
            // `recvargloan`, `mexprpatloan` and `patbyvalmove`). The 335,227
            // "fires" are the `probe::on` count over EVERY argument, which is
            // what rule 1 buys and is NOT the mechanism's population. The
            // LOGOS_MRAM_TRACE below says the inner predicate matched TWICE per
            // compile, both times inside prelude code, and NEVER ONCE in a user
            // function: on the two target rows
            // (moved-value-suggest-reborrow-issue-127285--r32 / --t32) and on
            // hand-written twins, `generic(self)` and `generic(r)` produce no
            // trace line at all, and even `generic(r); generic(r);` compiles
            // clean under the probe. So the argument at a by-value generic
            // formal is NOT a bare `VarRef` of `MutRef` kind at this site — the
            // aiming report's reading of `try_implicit_reborrow_mut`'s decline
            // is right about sema and wrong about the resulting LIR spelling.
            // The mechanism (`&mut T` is not Copy) is UNTESTED by this number.
            // Any re-aim must PRINT the argument's kind first.
            // ⚠ `mrgenerictv` IS ARMED HERE TOO, AND IT IS ONE MECHANISM AT
            // TWO SITES. Its producer half is in sema_expr: the substituted
            // formal of `generic<T>(x: T)` is `&mut X`, so
            // try_implicit_reborrow_mut wraps the argument and this arm never
            // sees a bare VarRef. Declining that wrap when the DECLARED formal
            // is a TypeVar is the producer; consuming here is the consumer.
            // The fire count is the SUM over both sites — LOGOS_MRAM_TRACE
            // below is what separates them.
            // ── MEASURED 2026-08-29: CEILING 0 / COST 0, AND THE SITE IS
            // PROVEN LIVE — 853 fires on moved-value-...--t32, of which 851 are
            // arrivals at THIS arm and 2 at the sema producer. LOGOS_MRAM_TRACE
            // printed exactly two lines, `[mram] ln=149 name=v`, BOTH in
            // prelude code and NEITHER at the user's `generic(self)`. So the
            // argument is STILL not a bare VarRef with the producer's decline
            // armed. Re-run on a 5-line repro (`fn generic<T>(x: T){} fn f(s:
            // &mut X){ generic(s); s.a = s.a + 1u32; }`): the sema site fires
            // ONCE — that fire IS `generic(s)` — and this arm still traces only
            // the two prelude lines. The decline never triggered, which says
            // `fi.param_types[i]` at that call site is ALREADY SUBSTITUTED
            // (`&mut X`, not `T`), so a TypeVar test there can never be true.
            // The mechanism is still untested; the next probe must read the
            // formal from the callee's DECLARATION, not from `fi`.
            if (a && (logos::probe::on("mutrefargmove") ||
                      logos::probe::on("mrgenerictv")) &&
                a.kind() == Code::VarRef && a.type(pool) &&
                a.type(pool).kind() == LogosType::Kind::MutRef) {
                EVarRefView mv{a};
                if (std::getenv("LOGOS_MRAM_TRACE"))
                    fprintf(stderr, "[mram] ln=%u name=%.*s\n", line,
                            (int)mv.name().size(), mv.name().data());
                consume(std::string(mv.name()), line, mv.var_slot());
                return;
            }
            if (a && is_ref_kind(a.type(pool))) take_ref_borrows(a, line);
            else                                visit(a, /*consuming=*/true, line);
        });
        in_call_args_--;
        pop_scope();
    };

    switch (e.kind()) {
        // ── Variable reference ─────────────────────────────────────────
        case Code::VarRef: {
            EVarRefView v{e};
            std::string name(v.name());
            if (consuming && is_move_type(e.type(pool), prog_, ts_, &copy_tvs_))
                consume(name, line, v.var_slot());  // Phase-1
            else {
                check_live(name, line, v.var_slot());  // Phase-1
                // A whole-value READ while one of its fields is MUT-borrowed is
                // E0503 ("cannot use `s` because `s.a` was mutably borrowed").
                // A shared field borrow leaves whole reads legal, so this is a
                // read (need_exclusive=false) — only mut field borrows block.
                // Suppressed in place-base position (`w.f`, `w[i]`, `w.m()`): a
                // bare VarRef reached while walking a projection's receiver is
                // not a whole-value use (that's why `w.buf` as an arg of
                // `w.writer.wr(..)` must not flag whole-`w`).
                if (auto it = var_find(v.var_slot(), name);
                    it != nullptr && !in_addr_source_)
                    field_borrow_conflicts((*it), name, /*path=*/"",
                                           /*need_exclusive=*/false, line, "use");
            }
            break;
        }

        // ── Address-of: &x or &mut x ──────────────────────────────────
        // When EAddrOf appears directly in visit (not as SLet/SAssign RHS),
        // this is a transient borrow — caller handles scope. We verify the
        // source is alive AND, for `&mut x`, that the binding was declared mut.
        case Code::AddrOf: {
            std::string vname(EAddrOfView{e}.var_name());
            check_live(vname, line);
            if (is_mut_ref(e.type(pool))) {
                if (auto it = var_find(NO_SLOT, vname); it != nullptr) {
                    if (!it->is_mut_binding) {
                        logos::probe::census("mb.ao.arrive");
                        if (param_names_.count(vname)) {
                            logos::probe::census("mb.ao.hatch");
                            logos::probe::census(param_byval_.count(vname)
                                ? "mb.ao.hatch.byval" : "mb.ao.hatch.ref");
                        } else logos::probe::census("mb.ao.refuse");
                    }
                    if (!it->is_mut_binding && !param_names_.count(vname))
                        report(line, std::format(
                            "cannot borrow '{}' as mutable: not declared as mut",
                            vname));
                }
            }
            break;
        }
        // B81 + B93.2: method-call receivers (and other implicit
        // borrow-takes via AddrOfTemp) need the same path-aware conflict
        // checks as explicit `&mut x` / `&mut x.f`. Decompose via the same
        // foundation as the RECORD pass so the structural rule cannot drift
        // — pre-foundation this site only walked FieldRead chains and
        // silently mis-handled `&mut self.data[i]` (no Index branch). The
        // POLICY here is check-only: AddrOfTemp in visit() is a transient
        // auto-borrow (method-call receiver, arg materialization, …) — NLL
        // releases at the enclosing scope-pop. Recording for explicit
        // let-bindings `let r = &mut p.f` happens via take_ref_borrows.
        case Code::AddrOfTemp: {
            EAddrOfTempView v{e};
            auto inner = v.inner();
            bool is_mut = is_mut_ref(e.type(pool));
            BorrowPlace bp = extract_borrow_place(inner, pool);
            std::string root = bp.root;
            std::string path = bp.path;
            // A RAW pointer root (`*mut`/`*const`) is unchecked — Rust parity
            // (deref of a raw ptr is unsafe; aliasing is the programmer's job).
            // A REFERENCE root (`&`/`&mut`) IS tracked for borrow CONFLICTS — a
            // reborrow through it (`&*a`) and a mutation through it (`a.push()`)
            // must not alias (closing the through-`&mut` use-after-free, B93.2-fix).
            // Its mut-binding check stays skipped: writing through `&mut r` needs
            // no `mut r` (Rust parity). Field-path borrow tracking on a reference
            // root is still skipped (the ref's binding, not its pointee, is the
            // tracked place) — whole-root conflict checks below suffice.
            bool root_is_rawptr =
                bp.root_type && bp.root_type.kind() == LogosType::Kind::Ptr;
            // Any borrowed-form root (incl. fat: `&mut [T]`, borrowed DST) —
            // writing THROUGH it needs no `mut` on the binding (Rust parity:
            // `let c: &mut [u32] = …; c[0] = n;` is legal); only reassigning
            // the binding itself would.
            bool root_is_ref = bp.root_type && is_ref_kind(bp.root_type);
            if (!root.empty() && !root_is_rawptr && var_has(NO_SLOT, root)) {
                auto sit = var_find(NO_SLOT, root);
                // Mut-binding check (root-level) — skipped for reference roots.
                if (is_mut && !root_is_ref && !sit->is_mut_binding) {
                    logos::probe::census("mb.aot.arrive");
                    if (param_names_.count(root)) {
                        logos::probe::census("mb.aot.hatch");
                        logos::probe::census(param_byval_.count(root)
                            ? "mb.aot.hatch.byval" : "mb.aot.hatch.ref");
                    } else logos::probe::census("mb.aot.refuse");
                }
                // PROBE mbparamvalaot — the by-VALUE half of the hatch, AddrOfTemp.
                const bool byval_aot_ = param_names_.count(root) &&
                    param_byval_.count(root) &&
                    (logos::probe::on("mbparamvalaot") ||
                     logos::probe::on("mbparamvalall"));
                if (is_mut && !root_is_ref && !sit->is_mut_binding
                    && (!param_names_.count(root) || byval_aot_))
                    report(line, std::format(
                        "cannot borrow '{}' as mutable: not declared as mut",
                        root));
                // moved_fields check for FieldRead chains (T1-10/B78:
                // full dotted-path overlap).
                if (!path.empty()) {
                    if (auto* hit = find_moved_overlap(
                            sit->moved_fields, path)) {
                        report(line, std::format(
                            "use of moved field '{}.{}' (moved on line {})",
                            root, hit->first, hit->second));
                        break;
                    }
                }
                // B94: AddrOfTemp in visit() is a transient auto-borrow
                // (method-call receiver, arg materialization, etc.) — NLL
                // releases it at call-end via scope-pop. Only CHECK against
                // existing borrows; don't record (which would conflict with
                // TPB-style nested method calls and field-receiver chains
                // like `c.v.set(c.v.get() + 1)`).
                // Recording for explicit let-bindings `let r = &mut p.f`
                // happens via take_ref_borrows from the Let handler.
                {
                    std::string self_disp = fmt_path(root, path);
                    if (sit->mut_borrowed) {
                        report(line, std::format(
                            "cannot borrow '{}': '{}' is already mutably borrowed",
                            self_disp, root));
                        break;
                    }
                    // ── D-d.2: THE RESERVATION COUNTER WAS NEVER READ HERE ──
                    // `app(v.bm(0), v.pushret(2))` admitted while the SAME
                    // mutation spelled as a free call — `app(v.bm(0),
                    // vpush5(&mut v))` — refused, and so did every block form.
                    // The filed property ("a nested call in argument position")
                    // is WRONG and was refuted by probe: `d2j`, with no block at
                    // all, admits; `d2i`/`d2f`/`d2m` all refuse. The property is
                    // an AUTO-REF'D `&mut self` METHOD RECEIVER in argument
                    // position, i.e. exactly the node this arm handles.
                    // MECHANISM: `visit_args` routes a REF-KIND argument through
                    // `take_ref_borrows`, so the first argument's `&mut self`
                    // lands in `take_borrow`, which under `in_call_args_ > 0`
                    // deposits a B82 mut RESERVATION rather than `mut_borrowed`.
                    // `take_borrow` reads that counter back (its own B82 arm,
                    // "another mut reservation in flight is still a conflict —
                    // Rust rejects f(&mut x, &mut x) too") and refuses; this arm
                    // read `mut_borrowed`, `shared_borrows` and both field
                    // tables and never `mut_reservations`, so the second
                    // argument's auto-ref walked straight past a live one. That
                    // is the whole spread between the two spellings.
                    // Sharpened, one property at a time: `p1`
                    // `two(v.pushret(1), v.pushret(2))` admitted where rustc
                    // says E0499, its explicit twin `p2` refused; `p3`/`p4`
                    // admitted in BOTH orders, which is what says the receiver
                    // neither takes a reservation nor consulted one — it takes
                    // none by construction (this arm is check-only, B94), so
                    // consulting is the whole of the missing half.
                    // Spelling reused verbatim from take_borrow's B82 arm — not
                    // minted. `is_mut` only: B82's reservation is deliberately
                    // compatible with shared reads taken during the same
                    // argument evaluation, which is what keeps `v.push(v.len())`
                    // admitted (its own admit twin).
                    // PAIR: fail/bc_recv_reservation_conflict_fail (refuse)
                    //     + pass/bc_recv_reservation_disjoint_admit (admit).
                    if (is_mut && sit->mut_reservations > 0) {
                        report(line, std::format(
                            "cannot borrow '{}' as mutable: already mutably borrowed",
                            self_disp));
                        break;
                    }
                    if (is_mut && sit->shared_borrows > 0) {
                        report(line, std::format(
                            "cannot borrow '{}' as mutable: '{}' has shared borrows",
                            self_disp, root));
                        break;
                    }
                    for (auto& [p, c] : sit->shared_field_borrows) {
                        // Coverage map 2026-08-27 (8060 runs), THIS region:
                        // 18 iterations, `c <= 0` true 7.
                        (void)logos::probe::on("sharedzero_site_addrof");
                        if (c <= 0 && !logos::probe::on("sharedzero_live_addrof")) continue;
                        if (paths_overlap(path, p) && is_mut) {
                            report(line, std::format(
                                "cannot borrow '{}' as mutable: '{}' is already borrowed",
                                self_disp, fmt_path(root, p)));
                            goto addrof_temp_done;
                        }
                    }
                    for (auto& p : sit->mut_field_borrows) {
                        if (paths_overlap(path, p)) {
                            report(line, std::format(
                                "cannot borrow '{}': '{}' is already mutably borrowed",
                                self_disp, fmt_path(root, p)));
                            goto addrof_temp_done;
                        }
                    }
                    addrof_temp_done:;
                }
            }
            if (inner) {
                bool saved = in_addr_source_;
                in_addr_source_ = true;
                visit(inner, /*consuming=*/false, line);
                in_addr_source_ = saved;
            }
            break;
        }

        // ── Dereference: *ptr ──────────────────────────────────────────
        // E0507 — MOVING OUT OF A DEREF, ASKED WHERE `consuming` IS ALREADY
        // COMPUTED. This arm used to DISCARD `consuming` entirely, so a move
        // whose place is reached through a deref was never refused here at all.
        // sema has its own E0507 (`is_unowned_move_source`, sema_impl.hpp) and
        // that is the point: it is consulted from FIVE HAND-LISTED POSITIONS
        // (let RHS, `return`, tail-expr return, arg coercion, array
        // destructure) and its Deref arm requires the operand to be SPELLED
        // `VarRef`. So `*u.a`, `**r`, `*mut_ref(&mut t)`, `{ *r }` and a
        // struct-destructure `let A { s } = *r;` were each admitted for want of
        // a position or a spelling. `consuming` is a POSITION-GENERAL answer
        // the caller already carries; one question asked here covers every
        // consuming position at once and cannot miss the next one.
        // ⚠ NOT a place base: visit_place_base visits with consuming=false, so
        // `(*r).copy_field` / `(*r).method()` never reach this report.
        // MEASURED 2026-08-27 (probe `derefmove`): 321 fires, ceiling 13 rows,
        // cost 1. The cost was the raw-pointer exemption's absence, not a
        // place-base confusion — see deref_move_exempt.
        case Code::Deref: {
            auto dop = EDerefView{e}.operand();
            if (consuming && !deref_move_exempt(dop) &&
                is_move_type(e.type(pool), prog_, ts_, &copy_tvs_))
                report(line, deref_move_message(dop));
            visit(dop, /*consuming=*/false, line);
            break;
        }

        // ── Field read: recv.field ─────────────────────────────────────
        // ── Tuple index: t.N — THE SAME ARM, and that is the whole fix.
        // `case Code::TupleIndex:` used to be `visit_place_base(receiver);
        // break;` and nothing else: no partial-move record, no moved-overlap
        // question, no field-borrow conflict. CONTROL, ONE VARIABLE — the
        // projection spelling, byte-identical bodies otherwise:
        //     struct W { a: B }   let y = x.a; let z = x.a;  → REFUSED
        //     (B,)                let y = x.0; let z = x.0;  → ADMITTED
        // A tuple element is a FIELD whose name is its index — already the
        // spelling `extract_borrow_place` emits (B83) and `moved_vars_` writes
        // (`t.0`), so the two projections share one segment walk and one set of
        // rules rather than growing a tuple-shaped copy of them.
        // MEASURED as `tupidxmove` (PROBES.md): 8 arrivals, CEILING 1, COST 0.
        // ⚠ RULE 4 IS DECLARED AGAINST IT: 8 fires is the ENTIRE population of
        // "a move-typed tuple projection in a consuming position" over the
        // ledger plus 1385 legal programs, and a ceiling off eight bounds the
        // count and nothing else. What funds it is not the number — it is that
        // the arm was MISSING while its named-field twin is ~140 lines.
        case Code::FieldRead:
        case Code::TupleIndex: {
            // ONE segment reader for both spellings; the index is its name.
            auto seg_of = [&](ExprRef n) {
                return n.kind() == Code::TupleIndex
                           ? std::to_string(ETupleIndexView{n}.index())
                           : std::string(EFieldReadView{n}.field());
            };
            auto recv_of = [&](ExprRef n) {
                return n.kind() == Code::TupleIndex
                           ? ETupleIndexView{n}.receiver()
                           : EFieldReadView{n}.receiver();
            };
            auto recv = recv_of(e);
            // Partial-move tracking (T1-10/B78: full dotted-path
            // granularity): when `o.f` / `o.a.b` is used in a consuming
            // position (moved into a fn arg or let RHS) and the place is a
            // move type, mark the FULL path as moved on the root.
            // Subsequent overlapping uses — the same path, anything inside
            // it, or any parent containing it (incl. whole-value `o`) —
            // error; disjoint siblings (`o.a.t`) stay usable.
            std::string root;
            std::string path;
            {
                std::vector<std::string> segs{seg_of(e)};
                ExprRef cur = recv;
                bool raw_hop = false;
                auto recv_is_raw = [&](ExprRef r) {
                    TypeRef rt = r ? r.type(pool) : TypeRef(nullptr);
                    return rt && rt.kind() == LogosType::Kind::Ptr;
                };
                while (cur && (cur.kind() == Code::FieldRead ||
                               cur.kind() == Code::TupleIndex)) {
                    segs.emplace_back(seg_of(cur));
                    if (recv_is_raw(cur)) raw_hop = true;
                    cur = recv_of(cur);
                }
                // Ownership doesn't flow through a RAW pointer: a move out
                // of `(*p).field` (unsafe, `self.inner.val` in Rc::drop_rc)
                // is not a partial move of any tracked owned root — skip
                // tracking when any hop in the chain (incl. the root var)
                // is `*const/*mut`-typed. `&`/`&mut` hops keep the existing
                // depth-1-compatible tracking.
                if (cur && cur.kind() == Code::VarRef &&
                    !raw_hop && !recv_is_raw(cur)) {
                    root = std::string(EVarRefView{cur}.name());
                    for (auto it2 = segs.rbegin(); it2 != segs.rend(); ++it2) {
                        if (!path.empty()) path.push_back('.');
                        path += *it2;
                    }
                }
            }
            if (!root.empty()) {
                if (auto it = var_find(NO_SLOT, root); it != nullptr) {
                    if (auto* hit = find_moved_overlap(it->moved_fields, path)) {
                        // Two overlap directions: (1) `path` reads INTO moved
                        // data (a moved entry is a prefix of path, e.g. read
                        // `o.i.s.x` after `o.i.s` moved) — always an error; (2)
                        // `path` is a strict PARENT of a moved leaf (read `o.i`
                        // while `o.i.s` is gone) — an error only for a genuine
                        // whole-value read, NOT when `o.i` is just an
                        // intermediate projection to a DISJOINT deeper leaf
                        // (`o.i.t`, which reaches through `o.i`). The latter is
                        // the place-base position (in_addr_source_); rustc
                        // accepts the disjoint sibling read.
                        bool into_moved = path_prefix_or_eq(hit->first, path);
                        if (into_moved || !in_addr_source_) {
                            report(line, std::format(
                                "use of moved field '{}.{}' (moved on line {})",
                                root, hit->first, hit->second));
                            break;
                        }
                    }
                    bool moving = consuming && is_move_type(e.type(pool), prog_, ts_, &copy_tvs_);
                    // Reading/moving `root.path` while it (or an overlapping
                    // path) is borrowed: a read collides with a mut borrow
                    // (E0503); a partial move collides with ANY borrow (E0505).
                    // Skipped in borrow-source position (`&root.path`) — the
                    // AddrOf site already decided the conflict.
                    if (!in_addr_source_ &&
                        field_borrow_conflicts((*it), root, path,
                                               /*need_exclusive=*/moving, line,
                                               moving ? "move" : "use"))
                        break;
                    if (moving) {
                        // ── CEILING PROBE `fldmovedrop` — E0509. `moving`
                        // already answers "this read moves a non-Copy field";
                        // the missing conjunct is "and the RECEIVER's own type
                        // impls Drop". Sema rewrites every `let S{f:x} = s;`
                        // into `let __dst = s; let x = __dst.f;`, so this is
                        // where all three spellings (destructuring let, `s.f`,
                        // `..base` update) actually land.
                        // ── MEASURED 2026-08-28: 11 fires over 393 ledger
                        // compiles, CEILING 6, COST 1. The `if (moving)` branch
                        // is REACHED 140,066 times and TRUE 551 times in 8060
                        // runs, so the site is live and the population is the
                        // whole E0509 domain.
                        // CLOSED: borrowck-move-out-of-struct-with-dtor ·
                        //   borrowck-struct-update-with-dtor--b · --t17 ·
                        //   nll_enum-drop-access ·
                        //   nll_issue-52059-report-when-borrow-and-drop-conflict ·
                        //   nll_issue-53773
                        // ⚠ THE SET IS NOT THE PREDICTED SET (rule 6: a count
                        // near the prediction is not the prediction). THREE
                        // rows nobody nominated closed, and all three are ONE
                        // shape the aiming report never enumerated:
                        // `fn f(x: DropStruct) -> &mut T { return x.field; }` —
                        // a `&mut` field moved out of a Drop owner and returned.
                        // TWO PREDICTED ROWS DID NOT CLOSE, and the trace says
                        // why in one line: borrowck-move-out-of-tuple-struct-
                        // with-dtor--t13/--r13 produce NO fldmovedrop line at
                        // all, because their moved field is
                        // `struct Inner { a: i64 }` and `is_move_type` calls an
                        // all-scalar struct Copy. That is a Copy-inference
                        // question (DIVERGENCES §B1), not a site question, and
                        // no E0509 rule can reach those two until it is settled.
                        // ⚠⚠ THE COST ROW IS A SPEC RULE, NOT AN EXEMPTION.
                        // logos_25_spec_pass_intrinsic_1 line 227 is
                        // `let moved: Noisy = h.inner;` under the heading
                        // `@rule intrinsic.drop.skip-moved-out-paths` —
                        // "Moving a field out of an owner suppresses that
                        // field's drop; siblings still drop." The Logos spec
                        // DELIBERATELY admits what rustc calls E0509. So this
                        // mechanism is not one exemption away from shipping: it
                        // contradicts a written language rule, and funding it
                        // is a DESIGN decision (PAIR), not a checker round.
                        // Recorded, not fixed.
                        if (logos::probe::on("fldmovedrop")) {
                            TypeRef rt = recv ? recv.type(pool) : TypeRef(nullptr);
                            bool own_drop =
                                rt && ((rt.kind() == LogosType::Kind::Struct &&
                                        ts_.drop_types.count(std::string(rt.struct_name()))) ||
                                       (rt.kind() == LogosType::Kind::Enum &&
                                        ts_.drop_types.count(std::string(rt.enum_name()))));
                            if (std::getenv("LOGOS_FLDMOVEDROP_TRACE"))
                                fprintf(stderr, "[fldmovedrop] line=%u place=%s.%s "
                                        "recv_kind=%d drop=%d\n", line,
                                        root.c_str(), path.c_str(),
                                        rt ? (int)rt.kind() : -1, (int)own_drop);
                            if (own_drop)
                                report(line, "ceiling-probe fldmovedrop: cannot move out of "
                                             "a type which implements Drop (E0509)");
                        }
                        it->moved_fields[path] = line;
                        break;
                    }
                }
            }
            visit_place_base(recv, line);
            break;
        }

        // ── Index read: arr[i] ─────────────────────────────────────────
        case Code::IndexRead: {
            EIndexReadView v{e};
            visit_place_base(v.receiver(), line);
            // ── E0510: THE INDEX BASE IS BORROWED WHILE THE INDEX RUNS.
            // The base was visited as a place and then ABANDONED — nothing held
            // it while the INDEX expression was evaluated, so
            // `arr[{ arr = [4,5,6]; 1u64 }]` compiled and read an element of an
            // array that no longer existed at bounds-check time.
            //
            // The loan is SHARED, and that is the whole exemption analysis: an
            // ordinary READ of the base from inside the index (`a[a[0]]`,
            // `a[a.len()-1]`, `s.arr[s.n]`) stays legal, only a WRITE to it
            // conflicts. It is also PATH-KEYED through extract_borrow_place, so
            // a write to a DISJOINT FIELD of the same root
            // (`s.arr[{ s.k = 7u64; s.n }]`, `s.arr[bump(&mut s.n)]`) stays
            // legal too — rustc accepts both.
            //
            // ── PRICED BEFORE IT WAS WRITTEN (scripts/ceiling-probe.sh
            // idxbaseloan, 2026-08-28): 209 fires over 389 ledger compiles,
            // CEILING 1, COST 0. Predicted ONE row and closed exactly that one:
            //   borrowck_slice-index-bounds-check-invalidation--min
            // ⚠ AND THE COST 0 IS NOT WHY IT LANDED. Seventeen hand-written
            // counter-examples were run under the probe first (tests/logos/pass/
            // bc_idxbase_* are the two kept); fifteen fired 1-4 times and stayed
            // green. The one program the probe refused —
            // `a[{ a[0u64] = 9i64; 2u64 }]` — is refused by rustc too (E0510),
            // so it is the fixture pair's fail half, not a cost.
            //
            // ⚠ THE SECOND ROW WAS PREDICTED NOT TO CLOSE, AND DID NOT.
            // `--t35` (`x[1u64][{ x = yr; 2u64 }]`) writes the ROOT of the
            // OUTER base while the loan recorded here is on the inner
            // IndexRead's own receiver. The nested spelling needs the loan keyed
            // on the OUTERMOST place root; this is half the rule, cleanly, and
            // the row stays in the ledger saying so.
            {
                push_scope();
                BorrowPlace ibp = extract_borrow_place(v.receiver(), pool);
                if (!ibp.root.empty())
                    record_borrow(ibp, /*is_mut=*/false, line, "__idx_base");
                visit(v.index(), /*consuming=*/true, line);
                pop_scope();
            }
            break;
        }

        // ── Method call: recv.method(args) ────────────────────────────
        // Receiver is typically &mut self — already wrapped in EAddrOf.
        // B93.2: scope the receiver borrow to the call — otherwise its
        // implicit &mut self leaks past the call site and conflicts with
        // any subsequent method-call (consecutive `b.foo(); b.bar();`).
        case Code::MethodCall: {
            EMethodCallView v{e};
            // A `&self`/`&mut self` method whose receiver is a bare place (VarRef /
            // FieldRead — how generic & stdlib methods like `Vec::push` lower it,
            // NOT an explicit AddrOfTemp) still borrows the receiver for the call.
            // The AddrOfTemp path self-checks; for a bare-place receiver, run the
            // same whole-root conflict check here — so `let r=&v[i]; v.push()`
            // (push = &mut self, receiver VarRef v, while r borrows v) conflicts
            // (collection iterator-invalidation).
            if (auto recv = v.receiver();
                recv && recv.kind() != Code::AddrOfTemp) {
                int sk = method_self_kind(v);
                if (sk >= 1)
                    check_recv_conflict(extract_borrow_place(recv, pool),
                                        /*is_mut=*/sk == 2, line);
            }
            // ── A METHOD-CALL RECEIVER IS A WHOLE-VALUE USE OF ITS PLACE,
            // AND THE PARTIAL-MOVE MAP WAS NEVER ASKED ABOUT IT (E0382) ────
            // `consume` reads `moved_fields`; `check_live` does not, and a
            // receiver in place-base position reaches only `check_live`. So on
            // ONE partially-moved local, three uses one token apart:
            //     eat(line2);        → REFUSED  "use of partially moved value"
            //     let _c = line2;    → REFUSED  same
            //     line2.consume();   → ADMITTED
            // rustc refuses all three. A `self` / `&self` / `&mut self` call
            // uses the WHOLE receiver, whichever field went missing — the self
            // kind does not enter the question, which is why this asks at every
            // spelling and not only where `method_self_kind` resolved.
            //
            // ⚠ ONLY THE WHOLE-VALUE RECEIVER (`path` empty), AND THAT IS THE
            // WHOLE NARROWING. A PROJECTED receiver already gets the
            // dotted-path answer from visit()'s FieldRead arm below — measured
            // by hand: `o.i.look()` after `let _x = o.i.a;` refuses today with
            // "use of moved field 'o.i.a' (moved on line 10)". Asking here too
            // would emit a SECOND diagnostic for one fact. The empty path is
            // the one spelling that never inherited the question.
            //
            // ── PRICED BEFORE IT WAS WRITTEN (scripts/ceiling-probe.sh
            // recvpartial, 2026-08-29; the record is src/compiler/PROBES.md):
            // 10,017 fires, CEILING 2, COST 0.
            // ⚠ AND THE COST 0 IS NOT WHY IT LANDED — COST 0 IS NOT A SAFETY
            // CLAIM. Eight legal programs were written by hand and compiled
            // under the armed probe first (disjoint-field method call, disjoint
            // field read, a Copy field that is no move at all, re-init before
            // the call, another variable of the same type, the call BEFORE the
            // move, a nested disjoint parent, a whole move into a fresh
            // binding); seven fired the site and all eight stayed green. The
            // two the probe DID refuse — a by-value `self` and a `&self` call
            // on a partially-moved local — are refused by rustc too, so they
            // are this change's fixture pair, not a cost.
            {
                auto rp = v.receiver();
                if (rp && rp.kind() == Code::AddrOfTemp)
                    rp = lir_view::EAddrOfTempView{rp}.inner();
                BorrowPlace rbp = extract_borrow_place(rp, pool);
                if (!rbp.root.empty() && rbp.path.empty())
                    if (auto* rst = var_find(rbp.root_slot, rbp.root))
                        report_partial_move(*rst, rbp.root, line);
                // ── A BY-VALUE `self` RECEIVER IS A CONSUMING POSITION,
                // AND THE DEREF-MOVE RULE WAS NEVER ASKED THERE (E0507).
                // LANDED 2026-08-30 (was `recvselfderef`). The residual is
                // named at `deref_move_exempt`'s own arm: "NOT a place base:
                // visit_place_base visits with consuming=false, so `(*r).…`
                // never reaches this report." One token apart, measured:
                //     fn eat(f: F) -> i64        eat(*r)      → REFUSED E0507
                //     fn eat(self: Self) -> i64  (*r).eat()   → ADMITTED
                // Asked NARROWLY — the existing exemptions plus `is_move_type`
                // on the deref result — because the WIDE spelling (visit the
                // receiver with consuming=true) buys the SAME two rows and
                // refuses `m.get(&k)` over a HashMap, where `method_self_kind`
                // resolves 0 for what is really an autoref.
                if (rp && rp.kind() == Code::Deref && method_self_by_value(v)) {
                    auto dop_ = EDerefView{rp}.operand();
                    if (!deref_move_exempt(dop_) &&
                        is_move_type(rp.type(pool), prog_, ts_, &copy_tvs_))
                        report(line, deref_move_message(dop_));
                }
            }
            push_scope();
            visit_place_base(v.receiver(), line);
            // ── THE `AddrOfTemp` RECEIVER IS CHECKED AND NEVER RECORDED, AND
            // THAT IS THE SECOND HALF OF B94. `visit_place_base` above runs
            // visit()'s AddrOfTemp arm, which CHECKS the receiver against every
            // existing loan — including `mut_reservations` (D-d.2) — and, per
            // B94, records nothing. So nothing held the receiver while
            // `visit_args` evaluated its SIBLINGS, and `f.foo(f.bar())` with
            // both `&mut self` compiled (E0499). The check half is landed; this
            // is the DEPOSIT.
            //
            // ── IT IS A DEPOSIT, NOT A BORROW, AND THE DIFFERENCE IS THE WHOLE
            // ROUND. It does NOT go through `take_borrow`, because take_borrow
            // asks the BINDING-MUT question ("cannot borrow 'X' as mutable: not
            // declared as mut") and the AddrOfTemp arm has ALREADY asked it,
            // with the `root_is_ref` exemption this spelling needs. Asking it
            // twice is what declined this mechanism twice before:
            //   `recvresvamut`  (deposit via record_borrow, every AddrOfTemp
            //                    `&mut self` receiver) CEILING 5 / COST 11 ⛔
            //   `recvamutarg`   (same, only when the call has arguments)
            //                    CEILING 4 / COST 9 ⛔
            // Compiling each of those nine cost fixtures under each probe by
            // hand, ALL NINE are the same diagnostic and all nine are rc 0
            // under the record-only deposit:
            //   bc_d1r10_e0_rebind_alias_dead_admit          's'
            //   bc_d1r5_h0_alias_admits                      'r'
            //   bc_d1r5_h1_reborrow_admits                   'r'
            //   bc_d1r8_u1_field_rhs_read_before_mut_admit   'r'
            //   bc_esc_holder_reborrow_container_admit       'r'
            //   bc_esc_holder_residency_backed_admit         'doc'
            //   bc_esc_holder_residency_pershare_admit       'doc'
            //   zone_mut_fat_ref                             'r'
            //   zone_mut_thin_source_admits_wmap             'mp'
            // Every one is a receiver reached THROUGH a `&mut` reference
            // binding, which is legal Rust and which the AddrOfTemp arm exempts
            // by `root_is_ref`. `take_borrow`'s own `skip_mut_binding_check`
            // exists for exactly this at exactly this spelling; the landed
            // bare-place deposit below sidesteps it the other way
            // (`is_mut_binding || param_names_`), which it can afford because
            // sema never wraps a bare-place receiver.
            //
            // ── PRICED BEFORE IT WAS WRITTEN (scripts/ceiling-probe.sh, one
            // build, three armed runs, 2026-08-29). Population: the MethodCall
            // arm takes 413,203 arrivals and its bare-place branch 174,476, so
            // ~238,727 arrivals reach this block — not a small population.
            //   `recvamutraw`   record-only deposit    CEILING 3 / COST 0  ✓
            //   `recvamutarg`   deposit when nargs>0   CEILING 4 / COST 9  ⛔
            //   `recvamuttouch` deposit when an arg
            //                   names the same root    CEILING 2 / COST 0
            // The three rows are named, and the closed set was diffed BOTH ways
            // — nothing predicted stayed open, nothing unpredicted closed:
            //   suggest-local-var-double-mut--d-double-mut-on-local-receiver
            //   suggest-local-var-double-mut--two-mut-borrows-in-call-args
            //   two-phase-multi-mut
            // ⚠ `recvamutarg`'s FOURTH row is a WRONG-REASON closure by the
            // very check this deposit refuses to ask: lifetimes/ex2e-push-
            // inference-variable-3 (`let a: &mut Vec<Ref<'b>> = x; a.push(b);`)
            // closes with "not declared as mut" — a legal program refused, not
            // a hole closed. A ceiling of 4 that contains a cost is worse than
            // a ceiling of 3, and this rule leaves that row admitted.
            // ⚠ `recvamuttouch` misses two-phase-multi-mut for a reason that is
            // the probe's SPELLING and not the rule's — `foo.method(&mut foo)`
            // hands an `EAddrOf` argument its peel did not know. Dominated.
            //
            // ── COST 0 IS NOT A SAFETY CLAIM. Nineteen legal shapes were
            // hand-written and PROVEN TO FIRE (19 arrivals in one file, 16 in a
            // second), and every one still admits: the argument reading the
            // receiver (`s.set(s.get()+1)`), two shared reads, a `&mut` of a
            // DISJOINT FIELD in the argument, a shared read of a disjoint
            // field, a zero-arg `&mut self` call, a `&self` receiver whose
            // argument reads it, `v.push(v.len() as i64)`, two SEQUENTIAL
            // `&mut self` calls (the deposit must be RELEASED between them),
            // a receiver reached through a NON-mut `&mut` binding called twice
            // and then two-phase, the same root as two sibling arguments of a
            // free call, a `&mut self` call whose argument is a `&mut self`
            // call on a DIFFERENT root, a `while` loop calling `s.bump()` three
            // times, a nested `if` block, a FIELD-PATH receiver two ways
            // (`rrp.path.empty()` keeps the D8 field-split admits untouched),
            // and a `&mut` of a disjoint LOCAL in the argument. The refuse twin
            // is `s.set(s.set(1i64))`: rc 0 before, rc 1 after, E0499.
            // PAIR: fail/bc_recv_addroftemp_resv_fail (refuse)
            //     + pass/bc_recv_addroftemp_resv_admit (admit, all 19 shapes).
            //
            // ── ONE DIVERGENCE FROM THE PRICED PROBE, AND IT IS ABOUT RELEASE
            // (rule 7). The probe deposited whenever the root was not moved.
            // `pop_scope` releases a mut entry as "clear `mut_borrowed` if set,
            // ELSE decrement `mut_reservations`", so depositing a reservation
            // on a root that is ALREADY `mut_borrowed` would release the OTHER
            // borrow and strand ours at 1 for the rest of the function. That
            // can only happen in a program the AddrOfTemp arm has already
            // reported on, but a stranded counter is a permissive hole plus a
            // spurious refusal, so the guard is here. Re-measured with it:
            // ceiling 3, cost 0, same set both ways.
            if (auto recv = v.receiver();
                recv && recv.kind() == Code::AddrOfTemp) {
                int sk = method_self_kind(v);
                if (sk >= 1) {
                    BorrowPlace rrp = extract_borrow_place(
                        EAddrOfTempView{recv}.inner(), pool);
                    if (!rrp.root.empty() && rrp.path.empty()) {
                        auto* rit = var_find(rrp.root_slot, rrp.root);
                        if (rit != nullptr && !rit->moved && !rit->mut_borrowed) {
                            // B82 RESERVATION, not an activation: compatible
                            // with SHARED reads taken during the same argument
                            // evaluation, which is what keeps `v.push(v.len())`
                            // and `s.set(s.get()+1)` admitted.
                            if (sk == 2) rit->mut_reservations++;
                            else         rit->shared_borrows++;
                            if (!scopes_.empty())
                                scopes_.back().borrows.push_back(
                                    {rrp.root, sk == 2, "__recv_resv",
                                     rrp.root_slot, {},
                                     slot_of_binding("__recv_resv"), {}});
                        }
                    }
                }
            }
            // ── THE BARE-PLACE RECEIVER IS CHECKED AND NEVER RECORDED (B94).
            // check_recv_conflict above asks the conflict question and takes no
            // loan, so NOTHING held the receiver while visit_args ran its
            // siblings, and a second use of the same root inside an argument
            // was admitted. `self` is a reference param, and stdlib/generic
            // methods lower their receiver as a bare place, so sema never wraps
            // either in an AddrOfTemp — this spelling had no producer at all.
            //
            // The deposit is a B82 RESERVATION (in_call_args_ > 0), not an
            // activation, which is what keeps `v.push(v.len())` — legal Rust
            // two-phase — admitted: a reservation is compatible with SHARED
            // reads taken during the same argument evaluation. Whole-root only
            // (`rrp.path.empty()`), so the D8 field-split admits are untouched,
            // and gated on the binding already being mut or a param so the
            // binding-mut question stays out of this rule entirely — that
            // question is `recvmutbind`, and it is declined for a reason
            // recorded at check_recv_conflict.
            //
            // ── PRICED BEFORE IT WAS WRITTEN (scripts/ceiling-probe.sh,
            // 2026-08-28): 342 fires over 387 ledger compiles, CEILING 2,
            // COST 0. Closed exactly the two rows it priced:
            //   borrowck_suggest-local-var-imm-and-mut   E0502
            //   borrowck_two-phase-sneaky                E0499
            //
            // ⚠ WHAT IT KEYS ON WAS MEASURED, NOT INFERRED FROM ITS NAME. The
            // aiming report partitioned these rows between this probe and
            // `recvresvamut` by RECEIVER SPELLING and got both assignments
            // wrong. So the halves were split into two probe names and priced
            // separately: the producer alone closes BOTH rows, the
            // producer+consumer pair closes the SAME TWO. The rule is the
            // MISSING LOAN, and the consumer arm is not landed — see the
            // paragraph in check_recv_conflict where it would have gone.
            //
            // ⚠ AND THE TWO ROWS CLOSE BY DIFFERENT ARMS, WHICH IS WHY THE
            // SET WAS READ AND NOT THE COUNT. Traced at the deposit:
            //   suggest-local-var-imm-and-mut — ONE arrival, the OUTER
            //     `self.foo(...)` at sk==1, depositing shared_borrows=1; the
            //     inner `&mut self` call is then refused elsewhere by the
            //     existing "'self' has shared borrows" rule.
            //   two-phase-sneaky — TWO arrivals, both `v.push`, and the row
            //     closes because the SECOND deposit is itself refused by
            //     take_borrow_whole_'s own B82 arm while `v.borrow_mut(0)`'s
            //     loan is live. The producer is the consumer here.
            //
            // ⚠ THE SAME DEPOSIT AT THE AddrOfTemp SPELLING IS NOW LANDED TOO
            // — see the block ABOVE. It got there by a different door: routed
            // through `record_borrow` it priced CEILING 5 / COST 11
            // (`recvresvamut`) and was declined twice, and all eleven costs
            // were `take_borrow`'s BINDING-MUT check, not the reservation.
            // Record-only, it is CEILING 3 / COST 0. This spelling can afford
            // `record_borrow` because it gates on `is_mut_binding ||
            // param_names_` first; that spelling cannot, because sema wraps
            // exactly the receivers — `&mut` reference bindings — that the
            // binding-mut question over-refuses.
            if (auto recv = v.receiver();
                recv && recv.kind() != Code::AddrOfTemp) {
                int sk = method_self_kind(v);
                BorrowPlace rrp = extract_borrow_place(recv, pool);
                auto* sit = rrp.root.empty()
                    ? nullptr : var_find(rrp.root_slot, rrp.root);
                if (sk >= 1 && rrp.path.empty() && sit != nullptr &&
                    (sit->is_mut_binding || param_names_.count(rrp.root))) {
                    in_call_args_++;
                    record_borrow(rrp, /*is_mut=*/sk == 2, line,
                                  "__recv_resv");
                    in_call_args_--;
                }
            }
            {
                // ── CEILING PROBE `recvargloan` — the receiver's conflict
                // check runs BEFORE visit_args and CHECKS without RECORDING, so
                // nothing holds the receiver while the arguments are evaluated
                // and a second `&mut` of the same root inside an argument is
                // admitted: `f.a(f.b())` with both `&mut self` compiles (E0499).
                // ⚠ Holding the receiver's `&mut` across the args would refuse
                // `v.push(v.len())`, which is legal Rust two-phase and compiles
                // today — so the reservation must conflict only with a SECOND
                // MUTABLE use. MethodCall arm 413,187 arrivals; the bare-place
                // `sk >= 1` branch 174,460; check_recv_conflict 108,650.
                // ── MEASURED 2026-08-28: CEILING 0, COST 1
                // (logos_02_semantic_core_pass_two-phase-baseline), 342 fires
                // over 400 ledger compiles. A stop sign, and the cost row is
                // the one the comment above predicted: the crude
                // `a.kind() == Code::MethodCall` half treats every nested call
                // as a mutable argument, so `v.push(v.len())` — legal Rust
                // two-phase — is refused. The three target rows DO reach this
                // site (1-3 fires each) and none closes, so the reservation
                // does not reproduce the conflict either. Both halves wrong at
                // once; the correct predicate is
                // `method_self_kind(EMethodCallView{a}) == 2`, and it needs its
                // own round. Declined.
                bool rl = logos::probe::on("recvargloan");
                BorrowPlace rbp{};
                bool mut_arg = false;
                if (rl) {
                    if (auto recv = v.receiver(); recv && method_self_kind(v) == 2)
                        rbp = extract_borrow_place(recv, pool);
                    v.each_arg([&](lir_view::ExprRef a) {
                        if (!a) return;
                        if (a.kind() == Code::AddrOfTemp ||
                            a.kind() == Code::MethodCall) mut_arg = true;
                    });
                    if (mut_arg && !rbp.root.empty())
                        record_borrow(rbp, /*is_mut=*/true, line, "__recv_resv");
                }
                visit_args(v);
                if (rl && mut_arg && !rbp.root.empty())
                    release_borrows_held_by("__recv_resv");
            }
            pop_scope();
            // Capture-flow: a `&mut self` method (push / insert / set) may STORE a
            // by-value borrow-carrying argument INTO the receiver. If the receiver
            // is a tracked local and such an arg borrows a local (`v.push(WAny::
            // from(&n))`), the receiver now transitively holds that borrow — taint
            // its provenance so a later `return v` is caught. Restricted to
            // &mut self + BY-VALUE borrow-carrying args: `&self` reads can't capture
            // and `&x` ref-args aren't moved in, so neither taints (keeps
            // `v.contains(&x)` / `v.len()` clean).
            // D1 round 4 / N1 — THE RECEIVER IS A PLACE, NOT A BINDING. The
            // guard used to be `recv.kind() == Code::VarRef`, so a container
            // reached as a STRUCT FIELD never inherited the loan and no callee
            // was involved at all: `let mut k = Keep{v:e}; k.v.push(c.mk());
            // c.bump(); *k.v.get(0).p` compiled, and so did the nested
            // `k.i.v.push(...)`, while the local-Vec twin refused. The rule
            // above is about which BINDING becomes the holder; a projection has
            // one — its place root — and the VarRef test was reading the
            // spelling instead of asking for it.
            //
            // place_write_root is the existing walk for exactly this (field /
            // tuple / index / ref-deref steps, raw-pointer deref stops it), and
            // NO_SLOT + var_has(NO_SLOT, name) is the existing by-name lookup
            // that apply_flow_outparams' flow_operand_root already uses for the
            // same reason. A VarRef receiver keeps its Phase-1 slot so the
            // currently-working local-Vec path is byte-for-byte unchanged.
            //
            // `elems` below deliberately keeps reading `recv.type(pool)` — the
            // CONTAINER's type (`k.v` → `Vec<B>`), not the root's (`k` →
            // `Keep`). Reading the root's type there would silently change the
            // stored_ref_elem arm to test membership in the wrong type-arg list.
            if (auto recv = v.receiver(); recv && method_self_kind(v) == 2) {
                bool recv_is_var = recv.kind() == Code::VarRef;
                bool rn_thru = false;
                // H1: the VarRef spelling bypasses place_write_root, so the
                // re-home has to be applied here too — `r.push(c.mk())` with
                // `let r: &mut Vec<B> = &mut vs;` is exactly this arm. When it
                // fires the Phase-1 slot no longer names the binding, so the
                // by-name NO_SLOT lookup (what flow_operand_root already uses)
                // takes over.
                std::string rn0 = recv_is_var
                    ? std::string(lir_view::EVarRefView{recv}.name())
                    : place_write_root(recv, rn_thru);
                std::string rn = rehome_reborrow(rn0);
                // Round 8: a dotted endpoint names its root (see place_write_root).
                if (!rn.empty() && !var_has(NO_SLOT, rn)) rn = ref_place_root(rn);
                uint32_t rn_slot = (recv_is_var && rn == rn0)         // Phase-1
                    ? lir_view::EVarRefView{recv}.var_slot() : NO_SLOT;
                if (!rn.empty() && var_has(rn_slot, rn)) {
                    RefProv cap = {};
                    // §B6: element types of the receiver container (`Vec<&T>` →
                    // [&T]). A by-value arg whose type IS an element type is being
                    // STORED as an element (push/insert) — distinct from a `&self`
                    // read or a `&element` arg (`contains(&&T)`: type ≠ element).
                    TypeRef rt = recv.type(pool);
                    std::vector<TypeRef> elems;
                    if (rt) for (auto el : rt.type_args()) elems.push_back(el);
                    v.each_arg([&](lir_view::ExprRef a){
                        if (!a) return;
                        TypeRef at = a.type(pool);
                        bool by_value_bc =
                            !is_ref_kind(at) && is_borrow_carrying_type(at);
                        bool stored_ref_elem = false;
                        if (is_ref_kind(at))
                            for (auto el : elems)
                                if (at == el) { stored_ref_elem = true; break; }
                        if (by_value_bc)
                            cap = merge_prov(cap, prov_of(a));
                        // The receiver now holds the arg's borrow of a local —
                        // record so a later use after that local dies is E0597
                        // (a `Vec<&T>` / view-buffer outliving its sources).
                        if (by_value_bc || stored_ref_elem)
                            add_ref_sources(rn, std::string{}, a, line);
                        // ── #86 MISS 3: THE CONTAINER HOLDER, SUMMARY-FREE ──
                        //
                        // `Vec<str>::push` comes PREBUILT from the stdlib
                        // archive, so its flow summary is unavailable (fs=0,
                        // measured — task #81) and apply_flow_outparams' hook
                        // never runs for it: `fn bad() -> Vec<str> { … 
                        // v.push(o.as_str()); return v; }` was rc 0 with the
                        // out-param half already in. This arm needs no summary
                        // — it reads the receiver's own element types — and it
                        // is the arm that already recorded the §B6 source for
                        // exactly this shape.
                        //
                        // `stored_ref_elem` above is deliberately NOT reused:
                        // it requires `is_ref_kind(at)`, which is what made the
                        // `Vec<H>` spelling (`H { v: str }` — a by-VALUE
                        // element that holds a borrow) deposit nothing at all
                        // (srcs=[] measured). The element-type match is the
                        // part that discriminates a STORE (`push`) from a read
                        // (`contains(&&T)`: type ≠ element); the ref-ness was
                        // never part of that question. Widened here only, for
                        // the escape record; the §B6/loan line above keeps its
                        // own gate.
                        // ⚠ THE DEPOSIT ITSELF NO LONGER LIVES HERE — see
                        // the `rn86` block just past this loop (#86 MISS-B/C).
                        // It needs a WIDER receiver root than `rn`, and `rn`
                        // is shared with the §B6 and loan lines above, whose
                        // widening was MEASURED and REFUSED (4 stdlib E0597s).
                        // D1 door 8b — the LOAN counterpart of the capture-flow
                        // taint above. `vs.push(c.mk())` moves a borrow-carrying
                        // value INTO the receiver, so the receiver becomes a
                        // holder of that value's loans: the loan must now live
                        // as long as `vs`, not die at the end of the push
                        // statement. Two halves, matching the two ways an
                        // argument can carry a borrow: a loan already held by
                        // the binding the arg hops out of (inherit), and a
                        // borrow the arg expression itself CREATES (record,
                        // with the receiver as holder). record_only, because
                        // visit_args above already visited the argument.
                        //
                        // This is the one rule that can over-refuse: Logos
                        // signatures carry no lifetimes, so a `&mut self`
                        // method that takes a borrow-carrying value and does
                        // NOT store it is indistinguishable from one that does.
                        // Rust refuses those too (the lifetime would have to be
                        // written), so the conservatism is Rust-parity.
                        if (by_value_bc) {
                            std::vector<std::string> roots;
                            bc_hop_roots(a, roots);
                            for (auto& r : roots) inherit_loans(r, rn, line);
                            take_ref_borrows(a, line, rn, /*record_only=*/true);
                        }
                    });
                    if (!cap.params.empty() || cap.is_local || cap.is_temp)
                        prov_[rn] = merge_prov(prov_[rn], cap);
                }

                // ── #86 MISS-B / MISS-C: THE ESCAPE CHANNEL'S OWN ROOT ─────
                //
                // TWO runtime-confirmed UAFs, one missing receiver shape:
                //   let r: &mut Vec<str> = &mut v; r.push(o.as_str()); return v;
                //   v.push("x"); v[0i64] = o.as_str(); return v;
                // both rc 0. `rn` above is empty for both: the receiver of a
                // `&mut self` method is a VarRef only in the plain spelling —
                // MEASURED receiver kinds are AddrOfTemp(12) for the reborrow
                // local and AddrOf(11) for the index-assign's lowered
                // `&mut self` call, and `place_write_root` (which `rn0` uses
                // for a non-VarRef receiver) has an arm for NEITHER.
                //
                // POPULATION, with the instrument in place: over the FIRST 300
                // of the 2211 `tests/logos/pass/*.logos` fixtures, 4275 `&mut
                // self` receivers reach this door; 3116 are AddrOfTemp and
                // 3115 of those produced an EMPTY `rn`. The blindness is the
                // MAJORITY of the door's traffic, not an edge case.
                //
                // ⚠ WHY THIS IS A SECOND ROOT AND NOT A FIX TO `place_write_root`.
                // MEASURED AND REFUSED. Teaching the shared walker the two
                // borrow-forming steps is the obviously-right shape — and it
                // also widens `rn`, which the §B6 `add_ref_sources` line and
                // the door-8b loan lines above consume. A full 53-target build
                // on that tree FAILED with 4 stdlib E0597 over-refusals in
                // `stdlib/mem/wql/lower.logos` (`lower_aggr`: 'rg' borrowed by
                // 'ra'; `gp_build` × 2: 'js0'/'js' borrowed by 'starr';
                // `gp_desugar_query`: 'orig2' borrowed by 'sa'), all of them
                // FALSE — every one stores a WRef HANDLE into an arena
                // container living in the same `h: &Writ`, so nothing dies at
                // the loop-body scope end the diagnostic names. `logos-mem`
                // failed to build, so the packages downstream of it were never
                // even measured. The widening therefore applies to the ESCAPE
                // record ONLY, and the §B6/loan channels keep the root they
                // had. The uncovered count that buys: the loan channel stays
                // blind to the same 3115-of-3116 AddrOfTemp receivers — that
                // is a SEPARATE hole, and it is on the bounding list.
                //
                // `flow_operand_root` is the peel, not a new walker: it is the
                // existing helper for exactly "a receiver/argument spelled
                // AddrOf or AddrOfTemp", already used at two other call sites.
                // The re-home + place-root tail is `rn`'s own, verbatim, so a
                // VarRef receiver computes the byte-identical name it did
                // before this block existed.
                {
                    std::string rn086 = recv_is_var
                        ? std::string(lir_view::EVarRefView{recv}.name())
                        : flow_operand_root(recv);
                    std::string rn86 = rehome_reborrow(rn086);
                    if (!rn86.empty() && !var_has(NO_SLOT, rn86))
                        rn86 = ref_place_root(rn86);
                    uint32_t rn86_slot = (recv_is_var && rn86 == rn086)
                        ? lir_view::EVarRefView{recv}.var_slot() : NO_SLOT;
                    if (!rn86.empty() && var_has(rn86_slot, rn86)) {
                        // The CONTAINER's element types. With an AddrOf /
                        // AddrOfTemp receiver the expression type is
                        // `&mut Vec<str>`, whose sole type-arg is the
                        // container — peel the reference or `at == el` never
                        // matches and the widened door deposits nothing
                        // (measured on the `r.push(o.as_str())` repro).
                        // The CONTAINER's element types. A VarRef receiver
                        // carries the container type directly (`Vec<str>`); a
                        // BORROW-FORMING receiver carries `&mut Vec<str>`, and
                        // — MEASURED — `MutRef::elem()` on the AddrOfTemp
                        // sema synthesizes for `r.push(…)` answers NULL, so
                        // peeling alone yields nothing (rawk=20 rtk=-1 nel=0).
                        // The declared type of the RESOLVED HOLDER is the same
                        // fact, recorded at its `let`, and it is already this
                        // door's second argument (`holder_ty_of(rn86)`).
                        TypeRef rt86 = recv.type(pool);
                        for (int pk = 0; pk < 4 && rt86 && is_ref_kind(rt86) &&
                                 rt86.kind() != LogosType::Kind::TraitObject; ++pk)
                            rt86 = rt86.elem();
                        std::vector<TypeRef> el86;
                        if (rt86) for (auto el : rt86.type_args()) el86.push_back(el);
                        if (el86.empty())
                            if (TypeRef ht86 = holder_ty_of(rn86))
                                for (auto el : ht86.type_args()) el86.push_back(el);
                        v.each_arg([&](lir_view::ExprRef a){
                            if (!a) return;
                            TypeRef at = a.type(pool);
                            bool by_value_bc =
                                !is_ref_kind(at) && is_borrow_carrying_type(at);
                            // `stored_ref_elem` in the loop above is NOT
                            // reused: it requires `is_ref_kind(at)`, which is
                            // what made the `Vec<H>` spelling (`H { v: str }`
                            // — a by-VALUE element that holds a borrow)
                            // deposit nothing at all (srcs=[] measured). The
                            // element-type match discriminates a STORE
                            // (`push`) from a read (`contains(&&T)`: type ≠
                            // element); ref-ness was never part of that.
                            bool stored_elem86 = false;
                            for (auto el : el86)
                                if (at == el) { stored_elem86 = true; break; }
                            if ((by_value_bc || stored_elem86) &&
                                type_may_carry_borrow(at))
                                note_holder_escape_prov(rn86, holder_ty_of(rn86),
                                                        a, line, "recvstore");
                        });
                    }
                }
            }
            {   // D1 round 3 / F3 — the method-call spelling. Receiver first,
                // so operand i lines up with param i in the summary.
                std::vector<ExprRef> ops;
                ops.push_back(v.receiver());
                v.each_arg([&](ExprRef a){ ops.push_back(a); });
                apply_flow_outparams(flow_of_method(v), ops, line);
            }
            break;
        }

        // ── Free function call: f(args) ───────────────────────────────
        case Code::Call: {
            ECallView cv{e};
            // SD-DST method dispatch lowers to a plain Call with the receiver
            // as arg0 (non-owning DstRef formal) — run the same receiver
            // conflict check a MethodCall gets, so a `&mut self` DST method
            // (resize_block_at) rejects while a slot view is live.
            if (auto it = fn_index_.by_name.find(std::string(cv.callee()));
                it != fn_index_.by_name.end() && it->second &&
                !it->second.params().empty()) {
                TypeRef p0 = it->second.params()[0].type(pool);
                // ⛔ REFUTED 2026-08-27 OVER A PROVEN-LIVE SITE: 176555 fires
                // across the 423 ledger compiles — by far the hottest of the
                // seventeen probes — CEILING 0 and COST 0. Widening
                // check_recv_conflict from DstRef-only to Ref/MutRef arg0
                // (with the AddrOfTemp peel, so it is not a false zero for the
                // reason the retired `genrecvtie` would have been) changes
                // NOTHING in either direction. A real negative result, not a
                // dead arm: the receiver-conflict check has nothing to say
                // about a mono-lowered generic receiver, so the class-2 hole
                // is NOT in the conflict channel. Do not re-propose this widening.
                // PROBE genrecvconflict: the MethodCall twin runs this check
                // for Ref/MutRef/DstRef alike with NO is_self_borrowing
                // requirement; this arm gates on DstRef only, so a mono'd
                // `&mut self` generic mutator escapes BOTH sides.
                bool gcf = logos::probe::on("genrecvconflict");
                bool p0_ref = p0 && (p0.kind() == LogosType::Kind::Ref ||
                                     p0.kind() == LogosType::Kind::MutRef);
                if ((p0 && p0.kind() == LogosType::Kind::DstRef &&
                    !p0.owning_dst()) || (gcf && p0_ref)) {
                    ExprRef a0; uint64_t ai0 = 0;
                    cv.each_arg([&](ExprRef a){ if (ai0++ == 0) a0 = a; });
                    if (gcf && a0 && a0.kind() == Code::AddrOfTemp)
                        a0 = EAddrOfTempView{a0}.inner();
                    if (a0)
                        check_recv_conflict(extract_borrow_place(a0, pool),
                                            /*is_mut=*/p0.mut_ptr() ||
                                              (gcf && p0.kind() == LogosType::Kind::MutRef),
                                            line);
                }
            }
            visit_args(cv);
            // ── D1 round 2, Door F: the free-call mirror of the capture flow ──
            // The MethodCall arm above has this rule (door 8b): a BY-VALUE
            // borrow-carrying argument passed to a `&mut self` method may be
            // STORED in the receiver, so the receiver joins the value's holder
            // set. A free function retains through a `&mut` ARGUMENT instead —
            // `fn stash(w: &mut Wrap, x: B) { w.b = x; } stash(&mut w, c.mk());
            // c.bump(); *w.b.p` — and no such rule existed, so the loan died at
            // the call statement. Same conservatism, same justification: Logos
            // signatures carry no lifetimes, so a `&mut` parameter that stores
            // the value is indistinguishable from one that does not, and Rust
            // refuses the ambiguous form too.
            //
            // Restricted to `&mut` roots: storing requires mutation, so a
            // shared-ref argument cannot retain (that is what keeps
            // R1/thru-style read-only pass-throughs out of this rule — they are
            // handled by the RESIDUE rule at the by-value arg site instead).
            {
                std::vector<ExprRef> ops;
                cv.each_arg([&](ExprRef a){ ops.push_back(a); });
                apply_call_outparam_rules(ops, flow_of_call(cv.callee()), line);
            }
            break;
        }

        // ── Closure call ───────────────────────────────────────────────
        case Code::ClosureCall: {
            EClosureCallView v{e};
            visit(v.callee(), /*consuming=*/false, line);
            visit_args(v);
            break;
        }

        // ── Fn-pointer call ────────────────────────────────────────────
        case Code::FnPtrCall: {
            EFnPtrCallView v{e};
            visit(v.callee(), /*consuming=*/false, line);
            visit_args(v);
            // G1 — the OUT-PARAM half. `let g: fn(&C, &mut Vec<B>) = stash2;
            // g(&c, &mut vs); c.bump();` recorded nothing: neither the summary
            // (never consulted through a pointer) nor the elision fallback
            // (which lived inside the Code::Call arm). Both now apply, and the
            // elision half fires even when the pointer does NOT resolve —
            // an unresolvable callee is the conservative case, not the silent one.
            {
                std::vector<ExprRef> ops;
                v.each_arg([&](ExprRef a){ ops.push_back(a); });
                apply_call_outparam_rules(ops, flow_of_fnptr(v.callee()), line);
            }
            break;
        }

        // ── Binary / Unary ─────────────────────────────────────────────
        case Code::BinOp: {
            EBinOpView v{e};
            visit(v.lhs(), /*consuming=*/true, line);
            visit(v.rhs(), /*consuming=*/true, line);
            break;
        }
        case Code::Unary:
            visit(EUnaryView{e}.operand(), /*consuming=*/true, line);
            break;

        // ── Cast ───────────────────────────────────────────────────────
        case Code::Cast: {
            ECastView cv{e};
            auto op = cv.operand();
            const auto* pool = prog_.type_pool.impl();
            TypeRef ot = op ? op.type(pool) : TypeRef(nullptr);
            TypeRef tt = e.type(pool);
            // `&T as *const T` / `&mut T as *mut T`: a reference→raw-pointer cast
            // reads the reference's address — it reborrows, it does NOT move the
            // reference (Rust keeps a `&mut` usable after the cast). Consuming
            // the operand would kill the reference; harmless when the cast is
            // its last use, but wrong when the reference is reused afterwards
            // (e.g. on a loop back edge — `root as *mut T` inside a `while`).
            bool ref_to_ptr = ot && (ot.kind() == LogosType::Kind::Ref ||
                                     ot.kind() == LogosType::Kind::MutRef) &&
                              tt && tt.kind() == LogosType::Kind::Ptr;
            visit(op, consuming && !ref_to_ptr, line);
            break;
        }

        // ── Struct literal ─────────────────────────────────────────────
        case Code::StructLit:
            EStructLitView{e}.each_field_value([&](ExprRef fv) {
                visit(fv, /*consuming=*/true, line);
            });
            break;

        // ── Array literal ──────────────────────────────────────────────
        case Code::ArrLit:
            EArrLitView{e}.each_elem([&](ExprRef el) {
                visit(el, /*consuming=*/true, line);
            });
            break;

        // ── Tuple literal ──────────────────────────────────────────────
        case Code::TupleLit:
            ETupleLitView{e}.each_elem([&](ExprRef el) {
                visit(el, /*consuming=*/true, line);
            });
            break;

        // ── Enum literal with payload ──────────────────────────────────
        case Code::EnumLitData:
            EEnumLitDataView{e}.each_payload([&](ExprRef pl) {
                visit(pl, /*consuming=*/true, line);
            });
            break;

        // ── If expression ──────────────────────────────────────────────
        case Code::IfExpr: {
            EIfExprView v{e};
            visit(v.cond(), /*consuming=*/true, line);
            auto saved_s = states_;
            auto saved_p = prov_;
            visit(v.then_val(), consuming, line);
            auto then_s = states_;
            auto then_p = prov_;
            states_ = saved_s;
            prov_   = saved_p;
            visit(v.else_val(), consuming, line);
            merge_moves(states_, then_s);
            merge_loans(states_, then_s);   // J0: same rule as the if STATEMENT
            merge_provs(prov_,   then_p);
            break;
        }

        // ── Match expression ───────────────────────────────────────────
        case Code::MatchExpr: {
            EMatchExprView v{e};
            check_whole_read_vs_field_loans(v.scrut(), line);
            visit(v.scrut(), /*consuming=*/false, line);
            auto saved_s = states_;
            auto saved_p = prov_;
            std::optional<StateMap> merged_s;
            std::optional<ProvMap>  merged_p;
            StateMap guard_acc = saved_s;
            // ── E0510 — SPELLING 2 OF THE GUARD LOAN. The rule is a property
            // of a match GUARD, not of the syntactic position the match is
            // written in, so it is installed at both spellings; the statement
            // form (visit_stmt's Code::Match) carries the full note.
            BorrowPlace guard_bp{};
            std::vector<std::string> guard_tested;
            {
                bool any_guard = false;
                v.each_arm([&](EMatchArmRef arm) {
                    if (arm.guard()) any_guard = true;
                });
                if (any_guard) {
                    guard_bp = extract_borrow_place(v.scrut(), pool);
                    if (guard_bp.root.empty() || guard_bp.index_in_chain) {
                        guard_bp = BorrowPlace{};
                    } else {
                        v.each_arm([&](EMatchArmRef arm) {
                            collect_tested_paths(arm.pat(), guard_bp.path,
                                                 guard_tested);
                        });
                        if (guard_tested.empty()) guard_bp = BorrowPlace{};
                    }
                }
            }
            // Hoisted OUT of the arm loop deliberately: armed once per
            // MatchExpr, so the fire count reads "expression matches seen",
            // not "arms seen".
            bool gacc = logos::probe::on("mexprguardacc");
            v.each_arm([&](EMatchArmRef arm) {
                states_ = gacc ? guard_acc : saved_s;
                prov_   = saved_p;
                bool saved_div = cur_diverged_;
                cur_diverged_ = false;
                push_scope();
                declare_pat_bindings(arm.pat());
                // ── CEILING PROBE `mexprpatloan` — the un-swept half of
                // `patloan`. The pattern-binding loan channel landed at
                // visit_stmt's Code::Match (10,589,215 arrivals) and at
                // NEITHER MatchExpr arm; this lambda (15,310 arrivals in 8060
                // runs) declares bindings and stops, so a `ref`/`ref mut`
                // binding produced by a match used as a VALUE raises no loan
                // on the scrutinee. Differential: the statement spelling
                // refuses `let m = &mut e;` after `E::A(ref y) => r = y`, the
                // `let r = match e { … }` spelling compiles.
                // ── MEASURED 2026-08-28. CEILING 0 over 400 ledger compiles,
                // 5 fires — AND THAT IS NOT A REFUTATION. The four rows it was
                // aimed at (or-patterns--b-or-pattern-borrows-all,
                // or-patterns--c-or-pattern-true-orpat,
                // borrowck-anon-fields-struct, borrowck-vec-pattern-move-tail)
                // were each compiled with the probe armed and its fire log
                // read: ZERO FIRES IN ALL FOUR, rc=0 in all four. Their match
                // in expression position never reaches THIS arm. So the
                // finding is UNMEASURABLE-HERE, not dead: the loan channel's
                // un-swept half is somewhere else, and whoever widens
                // `patloan` next must FIND THE ARM THOSE ROWS TAKE before
                // spending a line here. ⚠ `case Code::MatchExpr:` appears five
                // times in this file; this is one of them.
                if (logos::probe::on("mexprpatloan")) {
                    std::vector<std::string> hop;
                    if (type_may_carry_borrow(v.scrut().type(pool)))
                        bc_hop_roots(v.scrut(), hop);
                    if (auto st = retain_temp_scrut_loan(v.scrut(), line); !st.empty())
                        hop.push_back(std::move(st));
                    propagate_pat_prov(arm.pat(), v.scrut());
                    propagate_pat_loans(arm.pat(), hop, line);
                    propagate_pat_reborrows(arm.pat(), v.scrut());
                    propagate_pat_borrows(arm.pat(), v.scrut(), line);
                }
                StateMap before_guard = states_;
                if (auto g = arm.guard()) {
                    // Own scope frame — see the statement spelling's note.
                    push_scope();
                    for (const auto& tp : guard_tested) {
                        BorrowPlace bp = guard_bp;
                        bp.path = tp;
                        record_borrow(bp, /*is_mut=*/false, line,
                                      "__guard_scrut",
                                      RecordFlags{.implicit = true});
                    }
                    visit(g, /*consuming=*/true, line);
                    pop_scope();
                }
                // ── CEILING PROBE `mexprguardacc` — visit_stmt's Code::Match
                // carries a `guard_acc` that folds each guard's NEW moves of
                // outer bindings into the next arm's start state (guards run
                // in source order until one matches). This lambda resets to
                // `saved_s` at every arm, so a value a guard moves is un-moved
                // for every later guard and arm. An arm WITH a guard is
                // reached 106 times in 8060 runs.
                // MEASURED 2026-08-28: 2 fires over 400 ledger compiles,
                // CEILING 1 — logos_00_bc_admit_borrowck_
                // use-moved-value-in-match-guard-drop, exactly the row
                // predicted — COST 0. And the pre-stated fork held:
                // `borrowck-mutate-in-guard` reaches this site (1 fire) and
                // does NOT close, because it is the OTHER guard defect (an
                // E0510 assignment, not a move) and belongs to
                // `guardscrutloan`. The two guard mechanisms are genuinely
                // two. Not funded on its own — 1 row — and NOT bundled when
                // `guardscrutloan` was funded on 2026-08-28 either, although
                // that round's report said it would "ride free": a second
                // mechanism in the same change makes the row delta
                // unattributable, and this one's row (a MOVE in a guard, not
                // an assignment) is none of the seven that closed. Still 1
                // row, still priced, still unspent.
                if (gacc && arm.guard())
                    states_.for_each([&](uint32_t slot, std::string_view name, VarState& st) {
                        if (!st.moved || !saved_s.has_id(slot, name)) return;
                        const VarState* bg = before_guard.find(slot, name);
                        if (!bg || !bg->moved) guard_acc.at_id(slot, name) = st;
                    });
                visit(arm.value(), consuming, line);
                pop_scope();
                bool arm_div = cur_diverged_;
                cur_diverged_ = saved_div;
                // A DIVERGED arm (its value is a block whose tail returns —
                // `Err(_) => { return d; }`) contributes nothing to the
                // post-match move state: its moves never reach the join.
                // Stmt-form Match parity (see the Code::Match case).
                if (arm_div) return;
                if (!merged_s) {
                    merged_s = states_;
                    merged_p = prov_;
                } else {
                    states_.for_each([&](uint32_t slot, std::string_view name, VarState& st) {
                        if (st.moved && saved_s.has_id(slot, name))
                            merged_s->at_id(slot, name) = st;
                    });
                    merge_loans(*merged_s, states_);   // J0, as the match STMT
                    merge_provs(*merged_p, prov_);
                }
            });
            if (merged_s) {
                states_ = saved_s;
                prov_   = saved_p;
                merged_s->for_each([&](uint32_t slot, std::string_view name, VarState& st) {
                    if (saved_s.has_id(slot, name)) states_.at_id(slot, name) = st;
                });
                merge_provs(prov_, *merged_p);
            } else {
                states_ = saved_s;
                prov_   = saved_p;
            }
            break;
        }

        // ── Try expression: expr? ──────────────────────────────────────
        case Code::Try:
            visit(ETryView{e}.inner(), consuming, line);
            break;

        // ── Slice ──────────────────────────────────────────────────────
        case Code::SliceLit: {
            ESliceLitView v{e};
            visit(v.base(), /*consuming=*/false, line);
            visit(v.len(),  /*consuming=*/true,  line);
            break;
        }
        case Code::SliceIndex: {
            ESliceIndexView v{e};
            visit(v.slice(), /*consuming=*/false, line);
            visit(v.index(), /*consuming=*/true,  line);
            break;
        }
        case Code::SliceLen:
            visit(ESliceLenView{e}.slice(), /*consuming=*/false, line);
            break;
        case Code::SlicePtr:
            visit(ESlicePtrView{e}.slice(), /*consuming=*/false, line);
            break;

        // ── Format call ────────────────────────────────────────────────
        case Code::FormatCall: {
            EFormatCallView v{e};
            visit(v.fmt(), /*consuming=*/false, line);
            visit_args(v);
            break;
        }

        // ── Closure box ────────────────────────────────────────────────
        case Code::ClosureBox:
            EClosureBoxView{e}.each_capture_name([&](std::string_view cap) {
                check_live(std::string(cap), line);
            });
            // THE SECOND ARM, PRICED ON ITS OWN. There are exactly TWO
            // ClosureBox arms in this file and both stopped at the capture
            // names, so the class is mechanically enumerable and both get the
            // same walk. A closure in a NON-`let` position (`run(|| …);` as a
            // statement, a returned closure literal) reaches only this arm; a
            // `let`-bound one — including `let c = apply(|| …)`, whose
            // operand take_ref_borrows now recurses into — reaches only that
            // one. The two were measured SEPARATELY, and that is why the
            // crude probe's single 97-fire number was uninterpretable: it
            // summed two mechanisms. Arm 1 is 69 fires / 13 rows; this arm is
            // the other 28 fires / 2 further rows, both at cost 0.
            //
            // ⚠ THIS ARM IS THE WEAKER OF THE TWO AND KNOWINGLY SO. It has no
            // holder, so an intra-body conflict that arm 1 catches
            // (fail/bc_capbody_intra_body_conflict_refuse) is still ADMITTED
            // through this one — `run(|| { let r = &mut s; s = 9; *r });`
            // compiles. That is a REMAINING hole, not a closed one; it is
            // recorded here rather than in a ledger row because no ledger row
            // exhibits it.
            {
                walk_closure_body(EClosureBoxView{e});
            }
            break;

        // ── Block expression ───────────────────────────────────────────
        case Code::BlockExpr: {
            EBlockExprView v{e};
            if (auto br = v.block()) visit_block(br);
            if (auto r = v.result()) visit(r, consuming, line);
            break;
        }

        // ── Literals / compile-time nodes — no ownership effects ───────
        default:
            break;
    }
}

// ── Pass entry point ────────────────────────────────────────────────────────

lir::LProgram borrow_check(lir::LProgram prog, bool generic_templates_only) {
    const TypeSets ts = build_type_sets(prog);
    // Escape-analysis callee index — built ONCE here, shared (const) by every
    // per-function BorrowChecker below (was a per-instance map rebuilt N times).
    const FnIndex fn_index = build_fn_index(prog);
    // D1 round 3 / F3 — borrow-flow summaries. Computed ONCE here (the whole
    // program is in hand), consumed by every per-function checker below.
    //
    // ── #83: THEY RUN IN THE PRE-MONO PASS TOO, AND THAT IS HALF THE FIX ───
    //
    // They used to be POST-mono only, on the argument that the pre-mono
    // generic-template pass runs exclusivity-only over TypeVar bodies "where
    // neither the bc-type predicates nor callee resolution mean anything".
    // MEASURED FALSE for callee resolution: `resolve_method_flow` narrows by
    // the receiver's type, and when a TypeVar receiver narrows to nothing it
    // falls to `agree()` over every same-named method — which answers exactly
    // when all of them agree, TypeVar or not. What the old wording described
    // is the MOVE/REGION machinery, which stays off (ri_ is still nullptr and
    // exclusivity_only is still set); nothing here turns those on.
    //
    // The half it closes: a generic fn that is NEVER INSTANTIATED is checked
    // ONLY in this pass, so with `flows == {}` every call in its body was
    // summary-blind. `pub fn bad<T: Tr>(h: &H<T>) -> str { let o =
    // String::from("hello"); return h.t.thru(o.as_str()); }` with no call site
    // compiled rc 0 and refuses now (fail/bc_esc_generic_uninst_dangle); the
    // twin whose callee returns `self.s` still admits
    // (pass/bc_esc_generic_uninst_admit), because the summary is consulted,
    // not guessed.
    //
    // PRICED, not assumed: full tree rebuild (51 logos targets, stdlib
    // lang/mem/std + lforge + memoria + examples) is GREEN with this on, and
    // the summarizer's second run is inside the build's noise (2m53s with,
    // 3m09s without, same tree, same box).
    FlowSummaryMap flows;
    {
        FlowSummarizer fs(prog, ts, fn_index, flows);
        fs.run();
        // H2/H3: the iteration counts are now CONVERGENCE counts, not budget
        // usage — they are the evidence that the derived bounds are slack.
        if (std::getenv("LOGOS_DUMP_FLOW_ITERS"))
            fprintf(stderr, "[flow-iters] fns=%zu rounds=%u max_body_passes=%u\n",
                    flows.size(), fs.rounds_used(), fs.max_body_passes());
        if (const char* df = std::getenv("LOGOS_DUMP_FLOWS")) {
            std::string filt(df);
            for (auto& [nm, s] : flows) {
                if (filt != "1" && nm.find(filt) == std::string::npos) continue;
                if (!s.available) { fprintf(stderr, "[flow] %s: UNAVAILABLE\n", nm.c_str()); continue; }
                if (filt == "1" && !s.to_result && [&]{
                        for (auto m : s.to_outparam) if (m) return false;
                        return true; }()) continue;
                fprintf(stderr, "[flow] %s: result<-%#llx", nm.c_str(),
                        (unsigned long long)s.to_result);
                for (uint32_t j = 0; j < s.nparams; ++j)
                    if (s.to_outparam[j])
                        fprintf(stderr, " out%u<-%#llx", j,
                                (unsigned long long)s.to_outparam[j]);
                fprintf(stderr, "%s  (rounds=%u)\n",
                        s.over_approx ? "  OVER" : "  EXACT", fs.rounds_used());
            }
        }
    }

    auto check = [&](lir_view::FunctionView fn) {
        if (fn.is_extern())           return;
        // Skip functions loaded from a precompiled binary module (.writ0 in a
        // `-L` archive): they were already borrow-checked when THEIR layer was
        // built, so re-checking them on every downstream/user compile is pure
        // waste — and the pre-mono generic-template pass re-checking the WHOLE
        // loaded stdlib's generics was the dominant per-compile cost. User code +
        // user-side generic INSTANTIATIONS (from_binary_module=false) still run.
        if (fn.from_binary_module())  return;
        bool is_generic = !fn.type_params_empty();
        // P2-10: a dedicated PRE-mono pass (generic_templates_only) checks generic
        // fn bodies directly — so a generic that is never instantiated (no
        // specialization) is still borrow-checked (Rust parity). It runs in
        // exclusivity-only mode (move tracking is imprecise on TypeVars) and skips
        // region inference (also imprecise on generics). The normal POST-mono pass
        // checks concrete fns + specializations and ignores any leftover generics.
        if (generic_templates_only != is_generic) return;
        // logos-core 2.1 (consumer): run region_infer FIRST so its
        // named-lifetime regions and solved Outlives graph are available
        // to borrow_check. Both passes now consult the SAME source for
        // declared `'a: 'b` outlives, removing the parallel-paths drift
        // risk that the audit's #1 cross-category finding called out
        // (region_infer scaffolding-only). Generic templates skip
        // region inference (imprecise on TypeVars).
        RegionInferer ri;
        if (!generic_templates_only)
            ri.analyze(fn, prog);
        BorrowChecker(prog.diags, "fn " + std::string(bare_fn_name(fn.name())),
                      prog, ts, fn_index, /*exclusivity_only=*/generic_templates_only,
                      generic_templates_only ? nullptr : &ri,
                      &flows).check(fn);
        if (generic_templates_only) return;
        if (std::getenv("LOGOS_DUMP_REGIONS"))
            ri.dump(std::string(bare_fn_name(fn.name())));
        // B72/B73: region-based conflict diagnostics. Phrased in
        // Rust-style so they read naturally alongside B61's lexical
        // messages. The "later" borrow (by source line) is the
        // offending one; the "earlier" is referenced as the still-
        // live one.
        auto conflicts = ri.find_conflicts();
        for (auto& c : conflicts) {
            const BorrowSite* first  = c.a;
            const BorrowSite* second = c.b;
            if (second->origin_line < first->origin_line)
                std::swap(first, second);
            const char* kind_first  = first->is_mut  ? "mutable" : "shared";
            const char* kind_second = second->is_mut ? "mutable" : "shared";
            std::string target_label = second->target;
            if (target_label.starts_with("<temp")) target_label = "temporary";
            Diag d;
            d.level   = Diag::Level::Error;
            d.context = "fn " + std::string(bare_fn_name(fn.name()));
            d.message = std::format(
                "cannot borrow '{}' as {}: {} borrow still in scope here "
                "(first borrowed at line {}, conflicting use at line {})",
                target_label, kind_second, kind_first,
                first->origin_line, second->origin_line);
            d.line = second->origin_line;
            prog.diags.diags.push_back(std::move(d));
        }
    };

    for (auto& fn : prog.functions)       check(fn);
    for (auto& fn : prog.specializations) check(fn);
    for (auto& sd : prog.structs)
        sd.each_method([&](lir_view::FunctionView m) { check(m); });

    // P2-10: a generic template and each of its monomorphizations are checked
    // separately and report the SAME borrow error (same context/line/message —
    // `bare_fn_name` strips the mono suffix). De-duplicate identical diagnostics
    // so the user sees one error, not one per instantiation.
    {
        auto& ds = prog.diags.diags;
        std::vector<Diag> uniq;
        uniq.reserve(ds.size());
        std::unordered_set<std::string> seen;
        for (auto& d : ds) {
            std::string key = std::to_string((int)d.level) + "\x1f" +
                              std::to_string(d.line) + "\x1f" + d.context +
                              "\x1f" + d.message;
            if (seen.insert(key).second) uniq.push_back(std::move(d));
        }
        ds = std::move(uniq);
    }

    // S5-D4: fire count of the through-a-reference §B6 provenance arm.
    if (std::getenv("LOGOS_DUMP_BC_THRUREF"))
        fprintf(stderr, "[bc-thruref] fired=%llu\n",
                (unsigned long long)BorrowChecker::thru_ref_prov_fired_);

    // #86 MISS 1: fire count of the mutation-side holder-provenance record.
    if (std::getenv("LOGOS_DUMP_BC_HOLDERPROV"))
        fprintf(stderr,
                "[bc-holderprov] fired=%llu assign=%llu derefwrite=%llu "
                "outparam=%llu recvstore=%llu\n",
                (unsigned long long)BorrowChecker::holder_escape_prov_fired_,
                (unsigned long long)BorrowChecker::holder_escape_prov_by_door_[0],
                (unsigned long long)BorrowChecker::holder_escape_prov_by_door_[1],
                (unsigned long long)BorrowChecker::holder_escape_prov_by_door_[2],
                (unsigned long long)BorrowChecker::holder_escape_prov_by_door_[3]);

    return prog;
}

} // namespace logos::compiler
