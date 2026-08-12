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
#include <logos/compiler/sema.hpp>
#include <logos/compiler/outlives.hpp>
#include <algorithm>
#include <logos/compiler/region_infer.hpp>
#include <logos/compiler/move_classify.hpp>

#include <format>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <variant>
#include <vector>

namespace logos::compiler {

using namespace lir;

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
    // Name → def indices (built once in build_type_sets). Replace the per-type
    // linear scans of prog.structs / struct_specializations / enums that made
    // needs_drop / struct_is_dropck_relevant / enum_is_move O(structs) each —
    // and, called per variable across every function, the whole borrow pass
    // O(n²) in program size. First-def-wins, matching the scans' short-circuit.
    std::unordered_map<std::string, lir_view::StructView>  struct_by_name;
    std::unordered_map<std::string, lir_view::StructView>  spec_by_name;
    std::unordered_map<std::string, lir_view::EnumView>     enum_by_name;
};

static TypeSets build_type_sets(const lir::LProgram& prog) {
    TypeSets ts;
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

static bool is_temporary_value_expr(lir_view::ExprRef e) {
    if (!e) return false;
    using EK = lir_schema::expr::Code;
    switch (e.kind()) {
        case EK::LitInt: case EK::LitFloat: case EK::LitBool: case EK::LitStr:
        case EK::StructLit: case EK::TupleLit: case EK::ArrLit:
        case EK::Call: case EK::MethodCall: case EK::ClosureCall:
        case EK::EnumLit: case EK::EnumLitData:
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

struct ScopeFrame {
    std::vector<std::string>  declared;  // vars declared in this scope
    std::vector<BorrowRecord> borrows;   // borrows held in this scope
    std::vector<FieldBorrow>  field_borrows;  // B83: tracked field-path borrows
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
struct BorrowPlace {
    std::string root;             // empty if walker did not reach a VarRef
    std::string path;             // dotted, outermost-inside-root first
    bool        index_in_chain = false;
    TypeRef     root_type = nullptr;   // for raw-ptr / &mut root classification
    uint32_t    root_slot = 0xFFFFFFFFu;  // Phase-1: dense slot of `root`
};

static BorrowPlace extract_borrow_place(lir_view::ExprRef inner,
                                         const TypePoolImpl* pool) {
    using namespace lir_view;
    using Code = lir_schema::expr::Code;
    BorrowPlace bp;
    auto cur = inner;
    std::vector<std::string> path_parts;
    while (cur) {
        if (cur.kind() == Code::FieldRead) {
            EFieldReadView fv{cur};
            path_parts.push_back(std::string(fv.field()));
            cur = fv.receiver();
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
                bp.root.clear();
                return bp;
            }
            path_parts.clear();
            bp.index_in_chain = true;
            cur = recv;
        } else if (cur.kind() == Code::SliceIndex) {
            auto sl = ESliceIndexView{cur}.slice();
            if (sl && sl.type(pool) &&
                sl.type(pool).kind() == LogosType::Kind::Ptr) {
                bp.root.clear();
                return bp;
            }
            path_parts.clear();
            bp.index_in_chain = true;
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
                    // The deref'd CONTENT isn't a sibling-decomposable field
                    // of the container — treat like an index step (whole-
                    // container borrow), dropping any field path collected
                    // below the deref.
                    path_parts.clear();
                    cur = op;
                    continue;
                }
            }
            break;
        } else {
            break;
        }
    }
    if (cur && cur.kind() == Code::VarRef) {
        bp.root = std::string(EVarRefView{cur}.name());
        bp.root_slot = EVarRefView{cur}.var_slot();  // Phase-1
        bp.root_type = cur.type(pool);
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
// Restricted to `targets` — the vars some re-homed record actually borrows.
// A loan that dies inside the branch was already released there (pop_scope /
// NLL) and never reaches this set, so no branch-local borrow is prolonged and
// no program outside Door B's shape changes behaviour.
static void merge_loans(StateMap& base, StateMap& other,
                        const std::unordered_set<uint32_t>& slots,
                        const std::unordered_set<std::string>& names) {
    if (slots.empty() && names.empty()) return;
    other.for_each([&](uint32_t slot, std::string_view name, VarState& st) {
        bool wanted = (slot != StateMap::NO_SLOT) ? slots.count(slot) > 0
                                                  : names.count(std::string(name)) > 0;
        if (!wanted) return;
        auto& b = base.at_id(slot, name);
        if (st.shared_borrows   > b.shared_borrows)   b.shared_borrows   = st.shared_borrows;
        if (st.mut_reservations > b.mut_reservations) b.mut_reservations = st.mut_reservations;
        if (st.mut_borrowed) b.mut_borrowed = true;
        for (auto& [p, n] : st.shared_field_borrows) {
            auto& cur = b.shared_field_borrows[p];
            if (n > cur) cur = n;
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

struct FnIndex {
    std::unordered_map<std::string, lir_view::FunctionView>              by_name;
    std::unordered_map<std::string, std::vector<lir_view::FunctionView>> by_base;
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
    };
    for (auto& f  : prog.functions)       add(f);
    for (auto& f  : prog.specializations) add(f);
    for (auto& sd : prog.structs) sd.each_method([&](lir_view::FunctionView m) { add(m); });
    // Stage E: impl-block methods were never stored on LImplBlock (always empty);
    // trait-impl methods (Index, Deref, …) live on prog.functions / struct methods.
    return idx;
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
    // H1: binding -> the place its `&mut` REBORROW names. See rehome_reborrow.
    std::unordered_map<std::string, std::string> reborrow_of_;
    // H4: closure binding -> its CAPTURE names. A closure's provenance lives in
    // its captures, and the captures are not operands of the call, so the call
    // site cannot reach them without this. See closure_caps_of.
    std::unordered_map<std::string, std::vector<std::string>> closure_caps_;
    std::unordered_set<std::string>      param_names_;
    // Params whose referent OUTLIVES the call — reference params and
    // borrow-carrying value params (their borrow points at caller data). A
    // borrow of such a param is safe to return. A BY-VALUE owned param (not in
    // this set) is dropped at return exactly like a local, so a borrow of it
    // must NOT escape (else use-after-free).
    std::unordered_set<std::string>      outliving_params_;
    // Type-param names with an explicit `Copy` bound (per current fn) — a bare
    // TypeVar not in this set is move-classified (Rust generic-body semantics).
    std::unordered_set<std::string>      copy_tvs_;
    // Set transiently while recording a method-RESULT reborrow (`&mut v[i]` =
    // AddrOfTemp(Deref(MethodCall index_mut))): the OUTER `&mut` is the
    // authoritative borrow mutability. method_self_kind can't always resolve
    // the desugared index_mut (returns 0 ⇒ would record a SHARED borrow ⇒ two
    // `&mut v[i]` alias undetected). The MethodCall recorder ORs this in.
    bool                                 reborrow_force_mut_ = false;
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
    std::unordered_map<std::string, std::vector<std::string>>
        dropck_borrow_sources_;
    // B87: line at which each dropck-relevant binding was last bound, for
    // diagnostic reporting.
    std::unordered_map<std::string, uint32_t> dropck_binding_line_;
    // §B6 (NLL scope lifetime, rustc E0597): for EVERY reference / borrow-
    // carrying binding, the LOCAL variables it borrows from + the line of the
    // borrow. Generalises dropck_borrow_sources_ (which is gated on a Drop
    // impl) to all borrows. On scope-pop, a binding that OUTLIVES one of its
    // source locals is recorded in `dangling_`; the FIRST subsequent use of it
    // is rejected (E0597) — matching NLL (a stored borrow that is never used
    // after its referent dies is fine; only the use is the error).
    // Door B: vars targeted by a loan that outlived its recording frame. The
    // branch-join loan merge is restricted to these (see merge_loans).
    std::string                     pending_esc_holder_;
    std::unordered_set<uint32_t>    rehomed_slots_;
    std::unordered_set<std::string> rehomed_names_;
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
    std::unordered_map<std::string, std::vector<std::string>> ref_borrow_sources_;
    std::unordered_map<std::string, uint32_t>                 ref_borrow_line_;
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
    std::unordered_map<std::string, std::string> reborrow_prescan_;
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
    std::unordered_map<std::string, uint32_t> last_use_line_;
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
    std::unordered_map<uint32_t,    uint32_t> last_use_slot_;
    std::unordered_map<std::string, uint32_t> last_use_unslotted_;
    // Slot of the CURRENTLY-live binding of each name, so a loan can capture
    // its holder's identity at record time (declare_var runs AFTER the RHS is
    // walked, so the `let` arm pre-seeds this).
    std::unordered_map<std::string, uint32_t> cur_slot_of_;
    // Max statement line visited so far — the NLL release point after a
    // COMPOUND statement (its uses extend past its start line).
    uint32_t max_line_seen_ = 0;

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
    void loop_propagate_moves(StateMap& dst, StateMap& src, const StateMap& base) {
        src.for_each([&](uint32_t slot, std::string_view name, VarState& st) {
            if (st.moved && base.has_id(slot, name)) dst.at_id(slot, name) = st;
        });
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

    void push_scope() { scopes_.push_back({}); }

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
                    if (outer.count(rec.holder)) return true;
                    for (auto& h : rec.co_holders)
                        if (outer.count(h)) return true;
                    return false;
                };
                auto move_out = [&](auto& vec, auto& dst) {
                    for (auto it = vec.begin(); it != vec.end(); ) {
                        if (escapes(*it)) {
                            if (it->target_slot != StateMap::NO_SLOT)
                                rehomed_slots_.insert(it->target_slot);
                            else rehomed_names_.insert(it->target);
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
                auto sit = it->shared_field_borrows.find(fb.path);
                if (sit != it->shared_field_borrows.end() && --sit->second <= 0)
                    it->shared_field_borrows.erase(sit);
            }
        }
        // B87 dropck: before erasing this scope's declared locals, check
        // whether any outer-scope dropck-relevant binding holds a borrow
        // of one of them. If so, the binding's Drop runs after the local
        // dies — reject.
        if (!frame.declared.empty()) {
            std::unordered_set<std::string> dying;
            for (auto& n : frame.declared) dying.insert(n);
            for (auto& [binding, sources] : dropck_borrow_sources_) {
                // Skip if the binding itself is being declared/erased here —
                // its own death coincides with the source, no issue.
                if (dying.count(binding)) continue;
                for (auto& src : sources) {
                    if (!dying.count(src)) continue;
                    uint32_t ln = dropck_binding_line_[binding];
                    report(ln, std::format(
                        "binding '{}' has a `Drop` impl and borrows local '{}', "
                        "but '{}' goes out of scope before '{}' is dropped",
                        binding, src, src, binding));
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
                if (dying.count(binding) || dangling_.count(binding)) continue;
                for (auto& src : sources) {
                    if (!dying.count(src)) continue;
                    dangling_[binding] = DanglingRef{ src, ref_borrow_line_[place] };
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
            erase_ref_sources_under(name);   // F6: the whole place subtree
            dangling_.erase(name);
        }
        scopes_.pop_back();
    }

    void declare_var(const std::string& name, uint32_t slot = NO_SLOT) {
        var_at(slot, name) = VarState{};  // Phase-1: real slot → dense slot_
        if (!scopes_.empty()) scopes_.back().declared.push_back(name);
        note_binding_slot(name, slot);    // F5
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
    void collect_borrow_locals(lir_view::ExprRef e,
                               std::vector<std::string>& out) const {
        if (!e) return;
        using EC = lir_schema::expr::Code;
        switch (e.kind()) {
            case EC::AddrOf: {
                std::string n(lir_view::EAddrOfView{e}.var_name());
                if (var_has(NO_SLOT, n) && !param_names_.count(n))
                    out.push_back(std::move(n));
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
    std::vector<std::string> ref_sources_under(const std::string& root) const {
        std::vector<std::string> out;
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
                           std::vector<std::pair<std::string, std::string>> pairs,
                           const std::string& self_name, uint32_t ln) {
        for (auto& [sub, src] : pairs) {
            if (src == self_name) continue;   // a binding never borrows ITSELF
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
        std::vector<std::pair<std::string, std::string>> pairs;
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
        std::vector<std::pair<std::string, std::string>> pairs;
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
    void collect_ref_sources_paths(
            lir_view::ExprRef e, const std::string& path,
            std::vector<std::pair<std::string, std::string>>& out) const {
        if (!e) return;
        using EC = lir_schema::expr::Code;
        const auto* pool = prog_.type_pool.impl();
        auto emit = [&](std::string src) {
            for (auto& pr : out)
                if (pr.first == path && pr.second == src) return;
            out.emplace_back(path, std::move(src));
        };
        auto sub = [&](const std::string& p) {
            return path.empty() ? p : path + "." + p;
        };
        switch (e.kind()) {
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
                if (!bp.root.empty() && var_has(bp.root_slot, bp.root) &&
                    !param_names_.count(bp.root))
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
                if (plain || bc || (fat && result_borrows_self(v))) {
                    collect_ref_sources_paths(v.receiver(), path, out);
                    v.each_arg([&](lir_view::ExprRef a) {
                        // D1: a BY-VALUE borrow-carrying argument carries its
                        // sources exactly like a plain-ref one — the same rule
                        // the loan channel applies. Reached only under the
                        // "result is ref/bc" gate above, so a consuming call
                        // returning a scalar still ties nothing.
                        if (a && (is_plain_ref_kind(a.type(pool)) ||
                                  is_borrow_carrying_type(a.type(pool))))
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
            case EC::FieldRead:
                if (is_borrow_carrying_type(e.type(pool)))
                    collect_ref_sources_paths(
                        lir_view::EFieldReadView{e}.receiver(), path, out);
                return;
            case EC::TupleIndex:
                if (is_borrow_carrying_type(e.type(pool)))
                    collect_ref_sources_paths(
                        lir_view::ETupleIndexView{e}.receiver(), path, out);
                return;
            case EC::IndexRead:
                if (is_borrow_carrying_type(e.type(pool)))
                    collect_ref_sources_paths(
                        lir_view::EIndexReadView{e}.receiver(), path, out);
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
            case EC::Call: {
                // Free fn returning a borrow ties it to its ref args (elision).
                lir_view::ECallView v{e};
                if (is_ref_kind(e.type(pool)) || is_borrow_carrying_type(e.type(pool)))
                    v.each_arg([&](lir_view::ExprRef a) {
                        // D1: by-value borrow-carrying args too (`id(c.mk())`).
                        if (a && (is_plain_ref_kind(a.type(pool)) ||
                                  is_borrow_carrying_type(a.type(pool))))
                            collect_ref_sources_paths(a, path, out);
                    });
                return;
            }
            // Ref-to-ref provenance chaining: `o = r` (r another reference
            // binding) makes o borrow whatever r borrows. Propagates sources so
            // an aliased borrow can't escape a referent's scope via a copy.
            case EC::VarRef: {
                std::string n(lir_view::EVarRefView{e}.name());
                for (auto& s : ref_sources_under(n)) emit(s);
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
        std::vector<std::pair<std::string, std::string>> pairs;
        collect_ref_sources_paths(e, std::string{}, pairs);
        for (auto& pr : pairs)
            if (std::find(out.begin(), out.end(), pr.second) == out.end())
                out.push_back(pr.second);
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
                if (c <= 0) continue;
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
    void take_field_borrow(const std::string& target, uint32_t target_slot,
                           std::string path,
                           bool is_mut, uint32_t line,
                           TypeRef root_type = nullptr,
                           const std::string& holder = "") {
        auto it = var_find(target_slot, target);
        if (it == nullptr) return;
        std::string self_disp = fmt_path(target, path);
        // Whole-value borrows still block everything.
        if (it->mut_borrowed) {
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
            !it->is_mut_binding && !param_names_.count(target)) {
            report(line, std::format(
                "cannot borrow '{}' as mutable: '{}' not declared as mut",
                self_disp, target));
            return;
        }
        // Check against tracked field borrows.
        for (auto& [p, c] : it->shared_field_borrows) {
            if (c <= 0) continue;
            if (paths_overlap(path, p) && is_mut) {
                report(line, std::format(
                    "cannot borrow '{}' as mutable: '{}' is already borrowed",
                    self_disp, fmt_path(target, p)));
                return;
            }
        }
        for (auto& p : it->mut_field_borrows) {
            if (paths_overlap(path, p)) {
                report(line, std::format(
                    "cannot borrow '{}': '{}' is already mutably borrowed",
                    self_disp, fmt_path(target, p)));
                return;
            }
        }
        // Record.
        if (is_mut) it->mut_field_borrows.insert(path);
        else        it->shared_field_borrows[path]++;
        if (!scopes_.empty())
            scopes_.back().field_borrows.push_back(
                {target, std::move(path), is_mut, holder, target_slot,
                 {}, slot_of_binding(holder), {}});
    }

    // ── Borrow operations ─────────────────────────────────────────────────

    // Take a borrow of 'target'. Registers it in the current scope for cleanup.
    void take_borrow(const std::string& target, uint32_t target_slot,
                     bool is_mut, uint32_t line,
                     const std::string& holder = "",
                     bool skip_mut_binding_check = false) {
        auto it = var_find(target_slot, target);
        if (it == nullptr) return;  // unknown / extern
        if (it->moved) {
            report(line, std::format(
                "cannot borrow moved value '{}'", target));
            return;
        }
        if (is_mut) {
            // Reject &mut on a binding declared without `mut`.
            // Function params don't currently carry a mut bit in LParam,
            // so they're declared with is_mut_binding=false; we whitelist
            // them by checking known_params_ to avoid spurious diagnostics.
            // skip_mut_binding_check: the bare-receiver elision recorder
            // tracks EXCLUSIVITY only — binding-mut legality for bare
            // receivers stays the (permissive) status quo, the stdlib's
            // `arc.deref_mut()` on a non-mut Arc binding relies on it.
            if (!skip_mut_binding_check &&
                !it->is_mut_binding && !param_names_.count(target)) {
                report(line, std::format(
                    "cannot borrow '{}' as mutable: not declared as mut", target));
                return;
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
                report(line, std::format(
                    "cannot borrow '{}' as shared: already mutably borrowed", target));
                return;
            }
            // B83: a mut field borrow blocks whole-value shared borrows.
            if (!it->mut_field_borrows.empty()) {
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
            for (size_t i = 0; i < ops.size() && i < fs->nparams; ++i) {
                if (i == j || !(fs->to_outparam[j] & (1ull << i))) continue;
                lir_view::ExprRef src = ops[i];
                if (!src) continue;
                TypeRef st = src.type(pool);
                // Nothing to move if the operand cannot carry a borrow at all.
                if (!is_ref_kind(st) && !loan_carrying_type(st) &&
                    !is_borrow_carrying_type(st))
                    continue;
                std::vector<std::string> roots;
                bc_hop_roots(src, roots);
                for (auto& r : roots) inherit_loans(r, dst, line);
                // record_only: visit_args already visited the operand.
                take_ref_borrows(src, line, dst, /*record_only=*/true);
            }
        }
    }

    // ── D1: loans follow the HOLDER graph ─────────────────────────────────
    //
    // Phase 9 (NLL) releases a loan when its holder's last use has passed.
    // With inheritance a loan may have several holders; it expires only once
    // ALL of them are past. Missing holders count as 0 (never-used binding),
    // exactly as the single-holder lookup did.
    // F5: one holder's last use, keyed by its IDENTITY when it has one.
    uint32_t one_holder_last_use(const std::string& name, uint32_t slot) const {
        if (slot == NO_SLOT) {   // no identity available — pre-F5 behaviour
            auto it = last_use_line_.find(name);
            return it == last_use_line_.end() ? 0u : it->second;
        }
        uint32_t lu = 0;
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
    uint32_t holders_last_use(const std::string& holder, uint32_t holder_slot,
                              const std::vector<std::string>& co,
                              const std::vector<uint32_t>& co_slots) const {
        uint32_t lu = one_holder_last_use(holder, holder_slot);
        for (size_t i = 0; i < co.size(); ++i)
            lu = std::max(lu, one_holder_last_use(
                co[i], i < co_slots.size() ? co_slots[i] : NO_SLOT));
        return lu;
    }
    template <class Rec>
    uint32_t holders_last_use(const Rec& r) const {
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
                       uint32_t /*line*/) {
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

    bool consume(const std::string& name, uint32_t line, uint32_t slot = NO_SLOT) {
        auto it = var_find(slot, name);
        if (it == nullptr) return true;
        if (!it->moved_fields.empty()) {
            auto& [fld, ln] = *it->moved_fields.begin();
            report(line, std::format(
                "use of partially moved value '{}' (field '{}' moved on line {})",
                name, fld, ln));
            return false;
        }
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
            ref_borrow_sources_[n] = srcs;
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
        if (!loan_carrying_type(scrut.type(pool))) return {};
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

    // Whole-root conflict check for a method call's bare-place receiver borrowing
    // `self`. Only whole-var receivers (empty path) are checked (conservative —
    // field receivers deferred). Raw-ptr roots are unchecked (Rust parity);
    // reference roots ARE checked (a `&mut self` call through a `&mut` ref var
    // still conflicts with a live borrow of it).
    void check_recv_conflict(const BorrowPlace& bp, bool is_mut, uint32_t line) {
        if (bp.root.empty() || !bp.path.empty()) return;
        if (bp.root_type && bp.root_type.kind() == LogosType::Kind::Ptr) return;
        auto sit = var_find(bp.root_slot, bp.root);
        if (sit == nullptr) return;
        if (sit->mut_borrowed)
            report(line, std::format(
                "cannot borrow '{}': '{}' is already mutably borrowed",
                bp.root, bp.root));
        else if (is_mut && sit->shared_borrows > 0)
            report(line, std::format(
                "cannot borrow '{}' as mutable: '{}' has shared borrows",
                bp.root, bp.root));
        else if (is_mut && (!sit->shared_field_borrows.empty() ||
                            !sit->mut_field_borrows.empty()))
            report(line, std::format(
                "cannot borrow '{}' as mutable: field of '{}' is already borrowed",
                bp.root, bp.root));
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
    bool type_may_carry_borrow(TypeRef t) const {
        if (!t) return false;
        if (is_ref_kind(t) || loan_carrying_type(t)) return true;
        for (auto a : t.type_args())
            if (type_may_carry_borrow(a)) return true;
        return false;
    }

    // If `e` (a borrow's inner place, or a method receiver) roots at a VALUE local,
    // return its name; else "". Walk one optional leading AddrOfTemp, then a
    // FieldRead/TupleIndex/IndexRead/Deref chain to the terminal VarRef. A RAW-
    // pointer deref (`*p`, p:`*mut`/`*const`) STOPS the walk — the pointee isn't
    // tied to p's stack lifetime (Rust parity; box_leak's `&mut *into_raw(b)`). The
    // terminal must be a VALUE local: in states_, NOT a param, NOT a tracked ref-
    // binding (ref locals are in prov_) — which keeps `&param.x` / ref-locals safe.
    // A reference rooted at such a local dangles if it escapes the local's scope.
    std::string value_local_root(lir_view::ExprRef e,
                                 const TypePoolImpl* pool) const {
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
            break;
        }
        if (cur && cur.kind() == Code::VarRef) {
            std::string rn(EVarRefView{cur}.name());
            uint32_t rn_slot = EVarRefView{cur}.var_slot();  // Phase-1
            // A value LOCAL root: a borrow of it dangles if returned.
            if (var_has(rn_slot, rn) && !param_names_.count(rn) &&
                prov_.find(rn) == prov_.end())
                return rn;
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
            case Code::EnumLitData:
                EEnumLitDataView{e}.each_payload([&](ExprRef pl) {
                    if (pl && is_borrow_carrying_type(pl.type(pool)))
                        bc_hop_roots(pl, out);
                });
                return;
            case Code::StructLit:
                EStructLitView{e}.each_field_value([&](ExprRef fv) {
                    if (fv && is_borrow_carrying_type(fv.type(pool)))
                        bc_hop_roots(fv, out);
                });
                return;
            case Code::TupleLit:
                ETupleLitView{e}.each_elem([&](ExprRef el) {
                    if (el && is_borrow_carrying_type(el.type(pool)))
                        bc_hop_roots(el, out);
                });
                return;
            case Code::ArrLit:
                EArrLitView{e}.each_elem([&](ExprRef el) {
                    if (el && is_borrow_carrying_type(el.type(pool)))
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
        if (cur && cur.kind() == Code::AddrOfTemp) cur = EAddrOfTempView{cur}.inner();
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
            if (k == Code::IndexRead)  {
                auto r = EIndexReadView{cur}.receiver();
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
            // R7a: the REFERENT, resolved through the binding map (rehome, 8
            // hops — a4's `&mut *r`) and the place map (a3's `h.r`).
            std::string ref = resolve_place_reborrow(n, path_fields, path_ok);
            if (tracked) out.push_back(n);
            if (!ref.empty() && ref != n &&
                (var_has(NO_SLOT, ref) || is_loan_holder(ref)))
                out.push_back(std::move(ref));
        }
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
                if (param_names_.count(name)) return {{name}, false};
                if (var_has(NO_SLOT, name))      return {{},     true};
                return {};
            }
            case Code::AddrOfTemp: {
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
                if (!value_local_root(e, pool).empty())
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
                if (!caps) return {};
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
                {
                    TypeRef rt = e.type(pool);
                    bool plain = is_plain_ref_kind(rt);
                    bool fat   = !plain && is_ref_kind(rt);
                    if (!plain && !fat && !is_borrow_carrying_type(rt)) return {};
                    if (fat && !is_borrow_carrying_type(rt) &&
                        !result_borrows_self(v)) return {};
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
                return rp;
            }
            case Code::Call: {
                // A free fn / ctor returning a `#[borrow_carrying]` value
                // (`WAny::from(&x)`) may alias one of its REFERENCE args — merge the
                // provenance of each ref arg. (`WAny::from(7i64)` has no ref arg →
                // empty → freely returnable.) Non-borrow-carrying = caller-owned.
                if (!is_borrow_carrying_type(e.type(pool))) return {};
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
                if (const FlowSummary* fs = flow_of_call(cv.callee())) {
                    size_t i = 0;
                    cv.each_arg([&](ExprRef a) {
                        if (a && i < fs->nparams && (fs->to_result & (1ull << i)))
                            merged = merge_prov(merged, prov_of(a));
                        ++i;
                    });
                    return merged;
                }
                // Plain `&`/`&mut` args — a by-value slice arg
                // (`tv_build(h, name.as_str(), …)`) is a copied borrow with the
                // element's lifetime; it is not a capture channel for the result.
                cv.each_arg([&](ExprRef a) {
                    if (!a) return;
                    TypeRef at = a.type(pool);
                    if (is_plain_ref_kind(at) ||
                        (!is_ref_kind(at) && is_borrow_carrying_type(at)))
                        merged = merge_prov(merged, prov_of(a));
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
            case Code::ClosureBox:
                // A closure that CAPTURES a borrow of a local carries it out.
                EClosureBoxView{e}.each_capture_name([&](std::string_view cap) {
                    std::string n(cap);
                    if (auto it = prov_.find(n); it != prov_.end())
                        merged = merge_prov(merged, it->second);
                });
                break;
            default: break;
        }
        return merged;
    }

    void check_return_value(lir_view::ExprRef er, uint32_t line) {
        if (!ret_type_) return;
        bool typed_gate = is_ref_kind(ret_type_) || is_borrow_carrying_type(ret_type_);
        bool retention_gate = !typed_gate && type_hides_borrow(ret_type_) &&
                              retains_borrowing_operand(er);
        if (!typed_gate && !retention_gate) return;

        RefProv prov = prov_of(er);
        if (retention_gate && !prov.is_local && !prov.is_temp && prov.params.empty())
            prov = prov_of_retained(er);   // F4
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
            }
            if (is_temp)
                report(line,
                    "cannot return reference to temporary value: dangling reference");
            else
                report(line, std::format(
                    "cannot return reference to local variable '{}': dangling reference",
                    src.empty() ? "?" : src));
            return;
        }

        // 2. Explicit lifetime on return type — check sources match.
        const std::string ret_lt(TypeRef(ret_type_).lifetime());
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
                if (src_lt == ret_lt) continue;
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
                        bool found = inner->second.empty();  // aggregate w/o lt_args: trust
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
        // Gate: loan_carrying_type, NOT is_borrow_carrying_type — the loan
        // channel does not honour the residency exemption (see
        // loan_carrying_type). The ERASURE case is caught one level up, at the
        // Let/Assign routing gate, because the erased type only appears on the
        // BINDING: `Box::new(c.mk())` still has type `Box<B>` here.
        if (!holder.empty() && loan_carrying_type(e.type(pool))) {
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
                            if (var_has(NO_SLOT, c) && !param_names_.count(c))
                                take_borrow(c, NO_SLOT, /*is_mut=*/false, line, holder);
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
            case Code::AddrOf: {
                EAddrOfView v{e};
                take_borrow(std::string(v.var_name()), NO_SLOT, is_mut_ref(e.type(pool)),
                             line, holder);
                break;
            }
            // B81/B83: `&o.field.chain` lowers to AddrOfTemp(FieldRead*).
            // Walk down the FieldRead chain to extract the root var and
            // dotted path; check moved_fields and take a path-aware borrow.
            case Code::AddrOfTemp: {
                EAddrOfTempView v{e};
                auto inner = v.inner();
                // Reborrow shape `AddrOfTemp(Deref(VarRef r))` where r is
                // ref-typed — register a borrow on r (NOT on what r points
                // to). NLL releases on the holder's last use, restoring r's
                // usability — this is what makes implicit-reborrow at call
                // args work: r is "frozen" only for the call's scope.
                if (ExprRef inner_var; lir_view::is_reborrow_shape(e, &inner_var)
                    && is_ref_kind(inner_var.type(pool))) {
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
                        bool fake_param = !sit->is_mut_binding &&
                                          !param_names_.count(rname);
                        if (fake_param) param_names_.insert(rname);
                        take_borrow(rname, rname_slot, v.is_mut(), line, holder);
                        if (fake_param) param_names_.erase(rname);
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
                        take_ref_borrows(op, line, holder);
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
                uint32_t root_slot = bp.root_slot;  // Phase-1
                std::string path = bp.path;
                bool index_in_chain = bp.index_in_chain;
                if (!root.empty()) {
                    auto sit = var_find(NO_SLOT, root);
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
                    // §6.1 `items.union.ref.borrow`: a field-borrow on
                    // a union root is morally a whole-value borrow —
                    // all sibling fields alias.
                    bool root_is_union = is_union_root(bp.root_type);
                    if (index_in_chain) {
                        // Visit inner FIRST (sub-checks on the index expr etc.)
                        // BEFORE registering the borrow, else the recursive
                        // VarRef visit hits check_live on the root we just
                        // borrowed → spurious self-conflict.
                        if (inner) visit(inner, /*consuming=*/false, line);
                        if (!path.empty() && !root_is_union)
                            take_field_borrow(root, root_slot, std::move(path), v.is_mut(), line,
                                              bp.root_type, holder);
                        else
                            take_borrow(root, root_slot, v.is_mut(), line, holder);
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
                        if (root_is_union)
                            take_borrow(root, root_slot, is_mut, line, holder);
                        else
                            take_field_borrow(root, root_slot, std::move(path), is_mut, line,
                                              bp.root_type, holder);
                        break;
                    }
                }
                visit(e, /*consuming=*/true, line);
                break;
            }
            case Code::IfExpr: {
                EIfExprView v{e};
                visit(v.cond(), /*consuming=*/true, line);
                take_ref_borrows(v.then_val(), line, holder);
                take_ref_borrows(v.else_val(), line, holder);
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
                    if (p0 && p0.kind() == LogosType::Kind::DstRef &&
                        !p0.owning_dst()) {
                        ExprRef a0; uint64_t ai0 = 0;
                        v.each_arg([&](ExprRef a){ if (ai0++ == 0) a0 = a; });
                        if (a0) {
                            BorrowPlace bp = extract_borrow_place(a0, pool);
                            bool rawptr = bp.root_type &&
                                bp.root_type.kind() == LogosType::Kind::Ptr;
                            if (!bp.root.empty() && !rawptr &&
                                var_has(bp.root_slot, bp.root)) {
                                bool m = p0.mut_ptr();
                                if (!bp.path.empty())
                                    take_field_borrow(bp.root, bp.root_slot, bp.path,
                                                      m, line, bp.root_type, holder);
                                else
                                    take_borrow(bp.root, bp.root_slot, m, line, holder,
                                                /*skip_mut_binding_check=*/true);
                                tied_recv = true;
                            }
                        }
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
                    if (is_ref_kind(a.type(pool)) ||
                        (res_bc && is_borrow_carrying_type(a.type(pool))))
                        take_ref_borrows(a, line, holder);
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
                    if (!bp.root.empty() && !root_is_rawptr && var_has(bp.root_slot, bp.root))
                        take_borrow(bp.root, bp.root_slot, av.is_mut() || force_mut, line, holder);
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
                        root_is_rc = rn == "Rc" || rn == "Arc";
                    }
                    if (!bp.root.empty() && !root_is_rawptr && !root_is_rc &&
                        var_has(bp.root_slot, bp.root)) {
                        bool m = method_self_kind(v) == 2 || force_mut;
                        // Field-precise when the receiver is a field chain
                        // (`self.arc.deref_mut()` borrows self.arc, not all
                        // of self) — whole-root would falsely lock sibling
                        // field uses for the holder's lifetime.
                        if (!bp.path.empty())
                            take_field_borrow(bp.root, bp.root_slot, bp.path, m, line,
                                              bp.root_type, holder);
                        else
                            take_borrow(bp.root, bp.root_slot, m, line, holder,
                                        /*skip_mut_binding_check=*/true);
                    }
                } else if (recv && is_ref_kind(recv.type(pool))) {
                    take_ref_borrows(recv, line, holder);
                }
                // D1: same by-value rule as the free-call arm above.
                bool res_bc_m = is_borrow_carrying_type(e.type(pool));
                v.each_arg([&](ExprRef a) {
                    if (!a) return;
                    if (is_ref_kind(a.type(pool)) ||
                        (res_bc_m && is_borrow_carrying_type(a.type(pool))))
                        take_ref_borrows(a, line, holder);
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
                v.each_arm([&](EMatchArmRef arm) {
                    if (auto g = arm.guard()) visit(g, /*consuming=*/true, line);
                    propagate_pat_loans(arm.pat(), scrut_roots, line);
                    take_ref_borrows(arm.value(), line, holder);
                });
                break;
            }
            case Code::BlockExpr: {
                EBlockExprView v{e};
                // Door B: hand the escaping result+holder to visit_block so the
                // hop runs before the frame pops (see visit_block).
                if (auto br = v.block()) visit_block(br, v.result(), holder);
                take_ref_borrows(v.result(), line, holder);
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
                uint64_t i = 0;
                cb.each_capture_name([&](std::string_view cap) {
                    if (cb.is_move()) { ++i; return; }
                    std::string root(cap);
                    std::string_view fpath = cb.capture_path(i);
                    std::string rel;
                    if (fpath.size() > root.size() + 1 &&
                        fpath.compare(0, root.size(), root) == 0 &&
                        fpath[root.size()] == '.')
                        rel = std::string(fpath.substr(root.size() + 1));
                    bool is_mut = cb.capture_is_mut(i);
                    if (rel.empty()) {
                        // Whole-root capture: use the original whole-value
                        // borrow so plain reads of the root still see the
                        // borrow (mut_field_borrows on path="" is not checked
                        // on a bare variable read).
                        if (is_mut)
                            take_borrow(root, NO_SLOT, /*is_mut=*/true, line, holder);
                        else
                            check_live(root, line);
                    } else {
                        take_field_borrow(root, NO_SLOT, rel, is_mut, line);
                        check_live(root, line);
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
                    take_ref_borrows(fv, line, holder);
                });
                break;
            }
            case Code::TupleLit: {
                ETupleLitView{e}.each_elem([&](ExprRef el) {
                    take_ref_borrows(el, line, holder);
                });
                break;
            }
            case Code::ArrLit: {
                EArrLitView{e}.each_elem([&](ExprRef el) {
                    take_ref_borrows(el, line, holder);
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
                    take_ref_borrows(pl, line, holder);
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
    std::string prescan_referent(lir_view::ExprRef val) const {
        using namespace lir_view;
        using Code = lir_schema::expr::Code;
        if (!val) return {};
        std::string r;
        if (val.kind() == Code::AddrOf)          r = std::string(EAddrOfView{val}.var_name());
        else if (val.kind() == Code::AddrOfTemp) r = extract_borrow_place(
                                                         EAddrOfTempView{val}.inner(),
                                                         prog_.type_pool.impl()).root;
        else if (val.kind() == Code::VarRef)     r = std::string(EVarRefView{val}.name());
        else return {};
        if (r.empty() || is_materialized_temp_name(r)) return {};
        return r;
    }
    void prescan_reborrow_place(const std::string& place, lir_view::ExprRef val) {
        if (place.empty() || !val) return;
        TypeRef vt = val.type(prog_.type_pool.impl());
        if (!is_reborrow_ref_kind(vt)) return;   // R7a rule 2 (was MutRef only)
        std::string r = prescan_referent(val);
        if (!r.empty() && r != place) reborrow_prescan_[place] = std::move(r);
    }
    void prescan_reborrow(const std::string& name, TypeRef t, lir_view::ExprRef val) {
        using Code = lir_schema::expr::Code;
        if (name.empty() || !val) return;
        TypeRef vt = val.type(prog_.type_pool.impl());
        if (is_reborrow_ref_kind(t) || is_reborrow_ref_kind(vt)) {   // R7a rule 2
            std::string r = prescan_referent(val);
            if (!r.empty() && r != name) reborrow_prescan_[name] = std::move(r);
        }
        if (val.kind() == Code::StructLit)
            lir_view::EStructLitView{val}.each_field(
                [&](std::string_view fname, lir_view::ExprRef fv) {
                    if (!fname.empty())
                        prescan_reborrow_place(name + "." + std::string(fname), fv);
                });
    }
    // A use of `n` is a use of everything `n` reborrows — `n` itself when it IS
    // a reborrow, and every place recorded UNDER it (a struct that holds a
    // `&mut` keeps the referent borrowed while the struct is live). Chasing is
    // hop-bounded, matching rehome_reborrow.
    void note_reborrow_alias_uses(const std::string& n, uint32_t line) {
        if (n.empty() || reborrow_prescan_.empty()) return;
        auto chase = [&](std::string cur) {
            for (int hop = 0; hop < 8; ++hop) {
                auto it = reborrow_prescan_.find(cur);
                if (it == reborrow_prescan_.end() || it->second == cur) break;
                cur = it->second;
                note_use(cur, line);
            }
        };
        chase(n);
        std::string pfx = n + ".";
        for (auto& kv : reborrow_prescan_)
            if (kv.first.size() > pfx.size() &&
                kv.first.compare(0, pfx.size(), pfx) == 0) {
                note_use(kv.second, line);
                chase(kv.second);
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
    void note_use(std::string name, uint32_t line) {
        note_use_slot(NO_SLOT, std::move(name), line);
    }
    // F5: `slot` is the dense binding slot when the use site knows it
    // (VarRef), NO_SLOT otherwise.
    void note_use_slot(uint32_t slot, std::string name, uint32_t line) {
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

    void scan_uses_expr(lir_view::ExprRef e, uint32_t line) {
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
        uint32_t ln = stmt_line(sr);
        switch (sr.kind()) {
            case Code::Let: {
                SLetView lv{sr};
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
    void release_dead_borrows(uint32_t cur_line) {
        if (scopes_.empty()) return;
        auto& frame = scopes_.back();
        auto it = frame.borrows.begin();
        while (it != frame.borrows.end()) {
            if (it->holder.empty()) { ++it; continue; }
            uint32_t lu = holders_last_use(*it);
            if (lu <= cur_line) {
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
            uint32_t lu = holders_last_use(*fit2);
            if (lu <= cur_line) {
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
        // NLL release cursor: a COMPOUND statement (while/if/block) spans past
        // its start line — a holder whose last use sits INSIDE the body
        // (`while … { o[k] = …; }` then `self.mutate()`) must count as expired
        // once the whole statement has been visited. Release against the max
        // line inside the just-visited statement's SUBTREE (tracked via
        // max_line_seen_, reset per statement), folded monotonically across
        // this block. NOT a global max: sema emits out-of-line-order shapes
        // (`unsafe { …; return x; }` becomes a Return stmt whose line is the
        // LAST line, wrapping a BlockExpr of the earlier ones) — a global max
        // would pre-release every borrow inside such a block.
        uint32_t cursor = 0;
        br.each_stmt([&](lir_view::StmtRef sr) {
            uint32_t saved = max_line_seen_;
            max_line_seen_ = lir_view::stmt_line(sr);
            visit_stmt(sr);
            cursor = std::max(cursor, max_line_seen_);
            max_line_seen_ = std::max(saved, max_line_seen_);
            // Door B, value-block: with an escaping result the per-statement
            // NLL release is what kills the loan first — `let b: B = { let t: B
            // = c.mk(); t };` is ONE line, so `t`'s last use is not past the
            // `let t` statement and the loan is released before the result can
            // hand it on. Inside a value block with a holder, defer release to
            // the pop; the pop then releases the frame-local loans exactly as
            // the lexical rule would and re-homes the escaping ones.
            if (esc_holder.empty()) release_dead_borrows(cursor);
        });
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
                         const std::vector<std::string>& var_loan_roots = {}) {
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
        loop_stack_.push_back(LoopFrame{std::string(label), {}, {}});
        bool saved_sup = suppress_reports_;
        suppress_reports_ = true;
        push_scope();
        for (auto& v : loop_vars) declare_var(v);
        seed_loop_var_loans();
        bool saved_div = cur_diverged_;
        cur_diverged_ = false;
        body.each_stmt([&](lir_view::StmtRef sr) { visit_stmt(sr); });
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
        StateMap back_edge = pre_s;
        if (bottom_reachable) loop_propagate_moves(back_edge, post1_s, pre_s);
        for (auto& cs : frame1.continue_states) loop_propagate_moves(back_edge, cs, pre_s);

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
        loop_stack_.push_back(LoopFrame{std::string(label), {}, {}});
        cur_diverged_ = false;
        push_scope();
        for (auto& v : loop_vars) declare_var(v);
        seed_loop_var_loans();
        body.each_stmt([&](lir_view::StmtRef sr) { visit_stmt(sr); });
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
        merge_loans(states_, post2_s, rehomed_slots_, rehomed_names_);  // Door B
        if (bottom_reachable) loop_propagate_moves(states_, post2_s, pre_s);
        for (auto& cs : frame2.continue_states) loop_propagate_moves(states_, cs, pre_s);
        for (auto& bs : frame2.break_states)    loop_propagate_moves(states_, bs, pre_s);
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
    std::string place_write_root(lir_view::ExprRef e, bool& through_ref) const {
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
        if (cur && cur.kind() == Code::VarRef)
            return resolve_place_reborrow(std::string(EVarRefView{cur}.name()),
                                          path_fields, path_ok);
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
    std::string reborrow_referent(lir_view::ExprRef val) const {
        using namespace lir_view;
        using Code = lir_schema::expr::Code;
        if (!val) return {};
        const auto* pool = prog_.type_pool.impl();
        std::string r;
        if (val.kind() == Code::AddrOf) {
            r = std::string(EAddrOfView{val}.var_name());
        } else if (val.kind() == Code::AddrOfTemp) {
            BorrowPlace bp = extract_borrow_place(EAddrOfTempView{val}.inner(), pool);
            r = bp.root;
        } else if (val.kind() == Code::VarRef) {
            r = std::string(EVarRefView{val}.name());
        } else {
            return {};
        }
        // A materialized statement-temporary drops at the end of the statement
        // and has no referent to hold the loan (the admit direction).
        if (r.empty() || is_materialized_temp_name(r)) return {};
        if (!var_has(NO_SLOT, r) || param_names_.count(r)) return {};
        return r;
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
    static bool is_reborrow_ref_kind(TypeRef t) {
        return t && (t.kind() == LogosType::Kind::MutRef ||
                     t.kind() == LogosType::Kind::Ref);
    }
    void note_reborrow(const std::string& name, TypeRef t, lir_view::ExprRef val) {
        if (name.empty()) return;
        std::string r;
        if (is_reborrow_ref_kind(t)) r = reborrow_referent(val);
        if (!r.empty() && r != name) reborrow_of_[name] = std::move(r);
        else                          reborrow_of_.erase(name);
        // G0 — SAME CONCEPT, ONE PROJECTION DEEPER. Re-binding `name` retracts
        // every place recorded UNDER it: the old struct value is gone, so
        // `name.f` no longer names whatever it used to reborrow. Then re-record
        // from the new value if it is a struct literal.
        retract_reborrow_places(name);
        note_struct_lit_reborrows(name, val);
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
        std::string pfx = root + ".";
        for (auto it = reborrow_of_.begin(); it != reborrow_of_.end(); )
            if (it->first.size() > pfx.size() && it->first.compare(0, pfx.size(), pfx) == 0)
                it = reborrow_of_.erase(it);
            else ++it;
    }
    // Record (or retract) ONE place. The discriminator is the stored VALUE's
    // type being `&mut` — the same test note_reborrow applies to a binding.
    void note_reborrow_place(const std::string& place, lir_view::ExprRef val) {
        if (place.empty()) return;
        const auto* pool = prog_.type_pool.impl();
        std::string r;
        TypeRef vt = val ? val.type(pool) : TypeRef(nullptr);
        if (is_reborrow_ref_kind(vt)) r = reborrow_referent(val);   // R7a rule 2
        if (!r.empty() && r != place) reborrow_of_[place] = rehome_reborrow(std::move(r));
        else                           reborrow_of_.erase(place);
    }
    void note_struct_lit_reborrows(const std::string& name, lir_view::ExprRef val) {
        using Code = lir_schema::expr::Code;
        if (!val || val.kind() != Code::StructLit) return;
        lir_view::EStructLitView{val}.each_field(
            [&](std::string_view fname, lir_view::ExprRef fv) {
                if (fname.empty()) return;
                note_reborrow_place(name + "." + std::string(fname), fv);
            });
    }
    // Walk the dotted path root.f1.f2… through the place map. Each hop that
    // resolves REPLACES the accumulated name, so the remaining steps apply to
    // the referent (`h.r == vs` ⇒ a write to `h.r.x` is a write to `vs.x`).
    // The first unrecorded step stops the walk: below it the field names are
    // the referent's own, not another alias.
    std::string resolve_place_reborrow(std::string base,
                                       const std::vector<std::string>& fields_outer_first,
                                       bool path_ok) const {
        base = rehome_reborrow(std::move(base));
        if (!path_ok) return base;
        for (auto it = fields_outer_first.rbegin(); it != fields_outer_first.rend(); ++it) {
            auto f = reborrow_of_.find(base + "." + *it);
            if (f == reborrow_of_.end()) break;
            base = rehome_reborrow(f->second);
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
    std::string rehome_reborrow(std::string n) const {
        for (int hop = 0; hop < 8 && !n.empty(); ++hop) {
            auto it = reborrow_of_.find(n);
            if (it == reborrow_of_.end() || it->second == n) break;
            n = it->second;
        }
        return n;
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
                    if (fb.is_mut) it->mut_field_borrows.erase(fb.path);
                    else if (auto sb = it->shared_field_borrows.find(fb.path);
                             sb != it->shared_field_borrows.end() && --sb->second <= 0)
                        it->shared_field_borrows.erase(sb);
                }
                fr.field_borrows.erase(fr.field_borrows.begin() + (i - 1));
            }
        }
        if (!through_ref) return;
        std::vector<std::string> srcs = ref_sources_under(root);
        if (srcs.empty()) return;
        for (auto& src : srcs) {
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
        if (ln > max_line_seen_) max_line_seen_ = ln;
        using namespace lir_view;
        using Code = lir_schema::stmt::Code;
        const auto* pool = prog_.type_pool.impl();

        switch (sr.kind()) {
            // ── Let binding ──────────────────────────────────────────────
            case Code::Let: {
                SLetView v{sr};
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
                if (val && (is_ref_kind(t) || is_closure_t ||
                            is_borrow_carrying_type(t) || val_is_agg_lit)) {
                    take_ref_borrows(val, ln, name);
                } else if (val) {
                    visit(val, /*consuming=*/true, ln);
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
                    if (loan_carrying_type(t) ||
                        loan_carrying_type(val.type(pool)) ||
                        retains_loan_carrying_operand(val)) {
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
                // B87 dropck: record local borrow sources for Drop-lt bindings.
                if (val && struct_is_dropck_relevant(t)) {
                    std::vector<std::string> sources;
                    collect_borrow_locals(val, sources);
                    if (!sources.empty()) {
                        dropck_borrow_sources_[name] = std::move(sources);
                        dropck_binding_line_[name] = ln;
                    }
                }
                // §B6 (E0597): record local borrow sources for EVERY binding so
                // pop_scope can detect a stored borrow outliving its referent.
                record_ref_sources(name, val, ln);
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
                }
                bool val_is_agg_lit2 = val &&
                    (val.kind() == lir_schema::expr::Code::StructLit ||
                     val.kind() == lir_schema::expr::Code::TupleLit ||
                     val.kind() == lir_schema::expr::Code::ArrLit ||
                     val.kind() == lir_schema::expr::Code::EnumLitData);
                bool is_ref_assign = val &&
                    (prov_.count(name) || is_ref_kind(val.type(pool)));
                if (is_ref_assign || val_is_agg_lit2) {
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
                note_reborrow(name, val ? val.type(pool) : TypeRef(nullptr), val);  // H1
                note_closure_caps(name, val);                                       // H4
                // B87 dropck: record on (re-)assign too.
                if (val) {
                    auto vt = val.type(pool);
                    if (struct_is_dropck_relevant(vt)) {
                        std::vector<std::string> sources;
                        collect_borrow_locals(val, sources);
                        if (!sources.empty()) {
                            dropck_borrow_sources_[name] = std::move(sources);
                            dropck_binding_line_[name] = ln;
                        }
                    }
                }
                // §B6 (E0597): (re-)record sources on assign — a rebind re-owns
                // (clears any prior dangling), then tracks the new borrow.
                record_ref_sources(name, val, ln);
                break;
            }

            // ── Return ───────────────────────────────────────────────────
            case Code::Return: {
                if (auto val = SReturnView{sr}.value()) {
                    check_return_value(val, ln);
                    visit(val, /*consuming=*/true, ln);
                }
                cur_diverged_ = true;
                break;
            }

            // ── Expression statement ─────────────────────────────────────
            case Code::ExprStmt:
                visit(SExprStmtView{sr}.expr(), /*consuming=*/true, ln);
                break;

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
                    if (saw_index && cur && cur.kind() == EC::VarRef) {
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
                        else break;
                    }
                    if (c && c.kind() == EC::VarRef &&
                        atv.inner().kind() != EC::VarRef) {
                        std::string root(EVarRefView{c}.name());
                        uint32_t root_slot = EVarRefView{c}.var_slot();  // Phase-1
                        if (var_has(root_slot, root) && !param_names_.count(root))
                            add_ref_sources(root, std::string{}, v.value(), ln);
                    }
                    // Door A: the loan counterpart. `*<place> = c.mk()` — the
                    // place's root binding holds the stored borrow. Uses the
                    // FULL place walk (field / tuple / index / deref), not the
                    // pure-field one above: `a[i] = …` and `(*r).f = …` both
                    // land here and both store into their root.
                    bool wref = false;
                    std::string wroot = place_write_root(atv.inner(), wref);
                    if (!wroot.empty())
                        place_write_loans(wroot, v.value(), ln,
                                          wref || prov_.count(wroot) > 0);
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
                    // Door B: a loan re-homed out of a branch frame keeps its
                    // record; its counters must survive the state restore too.
                    merge_loans(states_, then_s, rehomed_slots_, rehomed_names_);
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
                if (auto b = v.body()) visit_loop_body(b, {}, v.label());
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
                        uint32_t cursor = 0;
                        b.each_stmt([&](lir_view::StmtRef s2) {
                            uint32_t saved = max_line_seen_;
                            max_line_seen_ = lir_view::stmt_line(s2);
                            visit_stmt(s2);
                            cursor = std::max(cursor, max_line_seen_);
                            max_line_seen_ = std::max(saved, max_line_seen_);
                        });
                        release_dead_borrows(cursor);
                    } else {
                        visit_block(b);
                    }
                }
                break;
            }

            // ── For-each loop ─────────────────────────────────────────────
            case Code::ForEach: {
                SForEachView v{sr};
                visit(v.iter(), /*consuming=*/false, ln);
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
                    propagate_pat_sources(arm.pat(), scrut_sources, ln);  // §B6
                    propagate_pat_prov(arm.pat(), v.scrut());             // D1 r3
                    propagate_pat_loans(arm.pat(), scrut_hop_roots, ln);  // D1
                    StateMap before_guard = states_;
                    if (auto g = arm.guard()) visit(g, /*consuming=*/true, ln);
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
            case Code::Break:
                if (auto* lf = loop_target(SBreakView{sr}.label()))
                    lf->break_states.push_back(states_);
                cur_diverged_ = true;
                break;
            case Code::Continue:
                if (auto* lf = loop_target(SContinueView{sr}.label()))
                    lf->continue_states.push_back(states_);
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
        return flows_ ? resolve_call_flow(*flows_, symbol) : nullptr;
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
        reborrow_of_.clear();
        reborrow_prescan_.clear();   // G0
        fnptr_sym_.clear();          // G1
        fnptr_multi_.clear();        // G1
        closure_caps_.clear();
        param_names_.clear();
        param_lifetimes_.clear();
        last_use_line_.clear();
        last_use_slot_.clear();        // F5 — slots are per-FUNCTION dense ids
        last_use_unslotted_.clear();
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
            // A reference, borrow-carrying, or RAW-POINTER param points at data
            // that lives outside this call (a raw pointer is unbounded / caller-
            // managed), so borrows derived from it are safe to return. Only a
            // by-value OWNED param (struct/enum/array/scalar) has call-local
            // storage.
            if (is_ref_kind(ptype) || is_borrow_carrying_type(ptype) ||
                TypeRef(ptype).kind() == LogosType::Kind::Ptr)
                outliving_params_.insert(pname);
            if (is_ref_kind(ptype)) {
                param_lifetimes_[pname] = std::string(TypeRef(ptype).lifetime());
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
                    std::vector<std::string> lts;
                    for (auto& lt : pointee.lifetime_args()) lts.push_back(lt);
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
                if (is_mut && !root_is_ref && !sit->is_mut_binding
                    && !param_names_.count(root))
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
                    if (is_mut && sit->shared_borrows > 0) {
                        report(line, std::format(
                            "cannot borrow '{}' as mutable: '{}' has shared borrows",
                            self_disp, root));
                        break;
                    }
                    for (auto& [p, c] : sit->shared_field_borrows) {
                        if (c <= 0) continue;
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
        case Code::Deref:
            visit(EDerefView{e}.operand(), /*consuming=*/false, line);
            break;

        // ── Field read: recv.field ─────────────────────────────────────
        case Code::FieldRead: {
            EFieldReadView v{e};
            auto recv = v.receiver();
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
                std::vector<std::string> segs{std::string(v.field())};
                ExprRef cur = recv;
                bool raw_hop = false;
                auto recv_is_raw = [&](ExprRef r) {
                    TypeRef rt = r ? r.type(pool) : TypeRef(nullptr);
                    return rt && rt.kind() == LogosType::Kind::Ptr;
                };
                while (cur && cur.kind() == Code::FieldRead) {
                    EFieldReadView cv{cur};
                    segs.emplace_back(std::string(cv.field()));
                    if (recv_is_raw(cur)) raw_hop = true;
                    cur = cv.receiver();
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
            visit(v.index(),    /*consuming=*/true,  line);
            break;
        }

        // ── Tuple index: t.N ──────────────────────────────────────────
        case Code::TupleIndex:
            visit_place_base(ETupleIndexView{e}.receiver(), line);
            break;

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
            push_scope();
            visit_place_base(v.receiver(), line);
            visit_args(v);
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
                if (p0 && p0.kind() == LogosType::Kind::DstRef &&
                    !p0.owning_dst()) {
                    ExprRef a0; uint64_t ai0 = 0;
                    cv.each_arg([&](ExprRef a){ if (ai0++ == 0) a0 = a; });
                    if (a0)
                        check_recv_conflict(extract_borrow_place(a0, pool),
                                            /*is_mut=*/p0.mut_ptr(), line);
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
            merge_provs(prov_,   then_p);
            break;
        }

        // ── Match expression ───────────────────────────────────────────
        case Code::MatchExpr: {
            EMatchExprView v{e};
            visit(v.scrut(), /*consuming=*/false, line);
            auto saved_s = states_;
            auto saved_p = prov_;
            std::optional<StateMap> merged_s;
            std::optional<ProvMap>  merged_p;
            v.each_arm([&](EMatchArmRef arm) {
                states_ = saved_s;
                prov_   = saved_p;
                bool saved_div = cur_diverged_;
                cur_diverged_ = false;
                push_scope();
                declare_pat_bindings(arm.pat());
                if (auto g = arm.guard()) visit(g, /*consuming=*/true, line);
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
    // D1 round 3 / F3 — borrow-flow summaries. POST-mono only: the pre-mono
    // generic-template pass runs exclusivity-only over TypeVar bodies, where
    // neither the bc-type predicates nor callee resolution mean anything.
    // Computed ONCE here (the whole program is in hand), consumed by every
    // per-function checker below.
    FlowSummaryMap flows;
    if (!generic_templates_only) {
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
                fprintf(stderr, "  (rounds=%u)\n", fs.rounds_used());
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
                      generic_templates_only ? nullptr : &flows).check(fn);
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

    return prog;
}

} // namespace logos::compiler
