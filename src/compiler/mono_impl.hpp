// Logos project — https://github.com/victor-smirnov/logos
//
// mono_impl.hpp — Mono class definition shared across mono_*.cpp TUs.

#pragma once

#include <logos/compiler/mono.hpp>
#include <logos/compiler/lir.hpp>
#include <logos/compiler/lir_mirror.hpp>
#include <logos/compiler/lir_view.hpp>
#include <logos/compiler/sema.hpp>
#include <logos/compiler/str_map.hpp>

#include "trait_engine.hpp"

#include <cstdlib>
#include <format>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace logos::compiler {

using SubstMap = StrMap<TypeRef>;
using PackMap  = StrMap<std::vector<TypeRef>>;

static inline std::string make_pack_arg_name(std::string_view base, size_t idx) {
    return std::string("$pack_arg$") + std::string(base) + "$" + std::to_string(idx);
}

class Mono {
public:
    explicit Mono(int max_depth) : max_depth_(max_depth) {
        // L1.6: lazy method codegen is the default. `LOGOS_LAZY_METHODS=0`
        // restores eager mode for bisecting / regression isolation.
        if (const char* e = std::getenv("LOGOS_LAZY_METHODS"); e && e[0] == '0')
            lazy_methods_ = false;
        if (const char* e = std::getenv("LOGOS_MONO_STATS"); e && e[0] != '0' && e[0] != '\0')
            stats_enabled_ = true;
    }
    // Opt-in: restrict non-generic free fn processing to those reachable
    // from the given names. Empty → eager (all non-generic fns processed).
    void set_entry_points(StrSet ep) { entry_points_ = std::move(ep); }
    // M6.2: when set, run() seeds out_ with the previous iter's mono
    // output (preserving cloned generic instances + passed-through
    // non-generics) and seeds done_/struct_done_/enum_done_ from it so
    // those items aren't re-cloned. The two LPrograms must share the
    // same TypePool / pools (via SemaCache).
    void set_prev_out(lir::LProgram&& p) { prev_out_ = std::move(p); has_prev_out_ = true; }
    // M3 step 3: non-owning pointer to the stdlib template catalog decoded
    // from .writ0 v3 trailers (see mono.hpp). Stored only for now; future
    // M3 steps use it to skip in_-walks for stdlib content.
    void set_stdlib_exports(const StdlibExports* e) { stdlib_exports_ = e; }

    lir::LProgram run(lir::LProgram&& in, int max_depth);

    // Cumulative counters for `LOGOS_MONO_STATS=1`. Dumped to stderr at the
    // end of run(). Cheap (always-on increments); guard formatting/IO behind
    // stats_enabled_ so production builds aren't taxed.
    struct Stats {
        uint64_t fn_clones        = 0;  // non-generic free fn copied to out_
        uint64_t fn_instances     = 0;  // generic fn instance (worklist drain)
        uint64_t method_instances = 0;  // lazy struct-method drain
        uint64_t struct_instances = 0;  // generic struct instance
        uint64_t enum_instances   = 0;  // generic enum instance
        uint64_t dispatch_entries = 0;  // generic-trait dispatch entries
        uint64_t enqueue_calls    = 0;  // enqueue_method_inst entries
        uint64_t defer_pushes     = 0;  // enqueue deferred (struct not emitted)
        uint64_t defer_rescans    = 0;  // deferred entries re-examined in drains
        uint64_t fixpoint_iters   = 0;  // run() lazy-drain loop iterations
        uint64_t emitted_checks   = 0;  // struct_emitted() calls
        size_t   peak_deferred    = 0;  // deferred_method_enqueues_ high-water
        size_t   peak_fn_worklist     = 0;
        size_t   peak_method_worklist = 0;
        int      peak_depth           = 0;
    };
    mutable Stats stats_;
    bool  stats_enabled_ = false;

    void note_fn_worklist_size(size_t n) {
        if (n > stats_.peak_fn_worklist) stats_.peak_fn_worklist = n;
    }
    void note_method_worklist_size(size_t n) {
        if (n > stats_.peak_method_worklist) stats_.peak_method_worklist = n;
    }
    void note_depth(int d) {
        if (d > stats_.peak_depth) stats_.peak_depth = d;
    }

private:
    lir::LProgram  in_;
    lir::LProgram  out_;
    // M6.2: optional previous-iter mono output (set via set_prev_out).
    // When has_prev_out_ is true, run() seeds out_ from it and primes
    // done_/struct_done_/enum_done_ so the previously-cloned instances
    // and passthroughs aren't redone.
    lir::LProgram  prev_out_;
    bool           has_prev_out_ = false;
    // M3 step 3: non-owning pointer to the stdlib template catalog merged
    // from .writ0 v3 trailers. Caller (main.cpp) keeps the value alive
    // for the duration of run(). Null = no exports available.
    const StdlibExports* stdlib_exports_ = nullptr;
    // Mirror of in_'s L-IR. Stage 3g.1: in_.mirror_table is the canonical
    // home — pre-populated by sema's LirBuilder for LExprs and topped up
    // by lir_mirror_emit_into() in run() for stmts/blocks/patterns.
    int            max_depth_;
    int            depth_ = 0;
    PackMap        cur_packs_;

    // Module system: module-qualify a package for synthesised symbols. Returns
    // `<module_id>.<pkg>` when pkg belongs to a non-global module, else pkg
    // unchanged — mirroring function_symbol_name's definition-side mangle so a
    // method-call symbol synthesised from a type's package matches the emitted
    // definition. Reads the package→module map handed over by sema.
    std::string mq(const std::string& pkg) const {
        if (pkg.empty()) return pkg;
        std::string_view mid = in_.pkg_module_ids.get_str(pkg);
        if (!mid.empty())
            return std::string(mid) + "." + pkg;
        return pkg;
    }

    // Archive-membership gate for a mono-synthesised symbol. binary_symbols
    // carries the archives' LINK names (nm output): method-shaped symbols are
    // `<mid>..<pkg>.Owner__m__sig` while mono synthesises the bare
    // `<pkg>.Owner__m__sig` — so after the raw-name test (free fns already
    // carry their module; global-module symbols have no prefix), retry the
    // `<mid>..`-prefixed form, mirroring sym::link_name and sema_decl's
    // skel_skip gate. All three body-skip gates (sema skip, mono stub,
    // mlir-gen forward-declare) then key on one name algebra. The qualified
    // retry can't resurrect the static-assoc-fn false-skip class (gap #2):
    // it only hits when pkg's OWN module defines the exact symbol.
    // ADR 0021 §3: is this struct type the metaclass demand marker's home?
    // Pilot scope: the one built-in marker (logos.lcm.canon.metaclass). A
    // registry (sema-fed, per metaclass) replaces this when a second
    // metaclass appears.
    static bool struct_pkg_is_metaclass(TypeRef tr) {
        return tr.pkg_name() == "logos.lcm.canon.metaclass";
    }
    std::unordered_set<uint64_t> factory_demand_hashes_;  // demand dedup (raw CFG hashes)

    bool binary_has_link(const std::string& name, std::string_view pkg) const {
        if (in_.binary_symbols.empty()) return false;
        if (in_.binary_symbols.has(name)) return true;
        if (pkg.empty()) return false;
        std::string_view mid = in_.pkg_module_ids.get_str(pkg);
        if (mid.empty()) return false;
        if (name.size() <= pkg.size() || name.compare(0, pkg.size(), pkg) != 0 ||
            name[pkg.size()] != '.')
            return false;
        std::string q;
        q.reserve(mid.size() + 2 + name.size());
        q.append(mid); q.append(".."); q.append(name);
        return in_.binary_symbols.has(q);
    }

protected:
    // Phase 5.A: source arena for refs constructed from the IN_-side variants.
    // mono moves in_.type_pool into out_ at run() start so historically these
    // refs were over out_.type_pool.arena() (which equals in_'s former arena).
    // When clone_fn reads a body that lives in a FOREIGN arena (Phase 5+B:
    // body_external_ref points into stdlib's published arena), it sets this
    // to that arena's pointer for the duration of the body walk.
    // nullptr → "use out_.type_pool.arena()" (legacy default).
    const writ::Arena* src_arena_ = nullptr;

    const writ::Arena* effective_src_arena() const noexcept {
        return src_arena_ ? src_arena_ : out_.type_pool.arena();
    }

    // Resolve an input Pattern* to its mirror PatRef. Returns null PatRef
    // when the mirror has no entry (caller falls back to variant access).
    lir_view::PatRef pat_ref_of(const lir::Pattern& p) const noexcept {
        auto& tbl = *in_.mirror_table;
        auto it = tbl.pat.find(&p);
        if (it == tbl.pat.end()) return {};
        return lir_view::PatRef(effective_src_arena(), it->second);
    }
    lir_view::StmtRef stmt_ref_of(const lir_view::StmtRef& s) const noexcept {
        return s;
    }
    lir_view::WritValRef hv_ref_of(const lir::WritVal& v) const noexcept {
        if (v.mirror_ptr_ == nullptr) return {};
        return lir_view::WritValRef(effective_src_arena(), v.mirror_ptr_);
    }
private:

    StrMap<lir_view::FunctionView>  templates_;
    StrMap<std::vector<lir_view::FunctionView>> specs_;
    StrMap<lir_view::StructView> struct_templates_;
    // const-length-overhaul: ctfe'd assoc-const values by "<target>::<name>",
    // built from impls at index time so subst_type can fold a `C::CONST`
    // projection in a length / const-arg once C binds. Sema is the only place
    // that can compute these, so they ride the impl LIR (impl_keys::ASSOC_CONSTS).
    StrMap<int64_t> assoc_const_values_;
    // ALL structs (generic templates AND non-generic), by bare + pkg-qualified
    // name. struct_templates_ holds GENERICS ONLY, so the `*mut DstStruct`→DstRef
    // canonicalisation in subst_type missed non-generic custom-DSTs (`*mut Foo`
    // where Foo has a `[u8]` tail), leaving them thin Ptr while sema resolved
    // them to DstRef — a representation divergence. This map closes it.
    StrMap<lir_view::StructView> all_structs_;
    lir_view::StructView find_any_struct(std::string_view pkg,
                                         std::string_view base) const noexcept {
        if (auto it = all_structs_.find(std::string(base)); it != all_structs_.end())
            return it->second;
        if (!pkg.empty()) {
            std::string q; q.reserve(pkg.size()+1+base.size());
            q.append(pkg).append(".").append(base);
            if (auto it = all_structs_.find(q); it != all_structs_.end()) return it->second;
        }
        return {};
    }
    // ── M2: centralized struct_templates_ lookup helpers ─────────────
    //
    // Today: inserts at mono.cpp:135-137 register BOTH `pkg.base` and
    // bare `base` keys. Lookups have three semantics across call sites:
    //   (B) pkg-first-then-bare: most common — try `pkg.base`, fall back
    //       to bare. Used when caller knows the pkg and wants to prefer
    //       it but tolerates the bare alias.
    //   (C) bare-first-then-pkg: mono_subst.cpp:81 only — used in a DST
    //       check where any same-named struct's is_dst flag is acceptable.
    //   (A) existence-of-pkg-qualified: existence-check only, no fallback.
    //
    // Helpers below capture (A)/(B)/(C) so a future M3 can swap the
    // backing map for a .writ0-loaded exports table without rewriting
    // every lookup. Composite-key sites (mono_scan.cpp:487, mono_clone.cpp
    // :2454) take a single string and split internally — they stay on
    // direct .find for now.
    lir_view::StructView
    find_struct_template_pkg_first(std::string_view pkg, std::string_view base) const noexcept {
        if (!pkg.empty()) {
            std::string qkey;
            qkey.reserve(pkg.size() + 1 + base.size());
            qkey.append(pkg).append(".").append(base);
            if (auto it = struct_templates_.find(qkey); it != struct_templates_.end())
                return it->second;
        }
        if (auto it = struct_templates_.find(std::string(base));
            it != struct_templates_.end())
            return it->second;
        return {};
    }
    // Stage 2 (B): ABI size of a (substituted) type and the byte offset of a
    // PREFIX field inside a custom-DST struct instance. Used to RE-LOWER a
    // generic method's `self.<dstfield>.<prefix>` access per instantiation
    // (the access shape was baked thin at sema-lower time, before T was known
    // to be unsized). Mirrors sema_abi_byte_size / sema_expr's DstRef field
    // projection, at mono time. Returns false (no re-lower) for the unsized
    // tail field — that is only ever projected at concrete use sites.
    uint64_t mono_abi_size(TypeRef t);
    bool mono_dst_prefix_field(TypeRef dstref, std::string_view field,
                               uint64_t& off_out, TypeRef& ftype_out);
    // True when the let-binding `var`'s initializer is an owned DST-tail dyn
    // projection (`self.inner.val` where the receiver substitutes to a fat
    // DstRef and the projected field is the unsized `dyn` tail). Used to gate
    // drop-in-place for a moved-out tail typed as TraitObject(Borrow) — telling
    // it apart from a genuine borrowed `&dyn` local of the same type.
    bool let_init_is_owned_dyn_tail(const std::string& var, const SubstMap& s);
    // Resolve a concrete struct TypeRef to its definition + the substitution that
    // binds the chosen def's type-vars to `t`'s type-args. Prefers the best-matching
    // partial SPECIALISATION (find_best_struct_spec) over the generic base — the same
    // selection instantiate_struct_templates uses — so layout/size computed here
    // matches the struct that is actually emitted. Falls back to the base template
    // (positional binding) when no spec matches. Returns nullptr if no def is found.
    lir_view::StructView resolve_struct_layout(TypeRef t, SubstMap& m_out);

    lir_view::StructView
    find_struct_template_bare_first(std::string_view pkg, std::string_view base) const noexcept {
        if (auto it = struct_templates_.find(std::string(base));
            it != struct_templates_.end())
            return it->second;
        if (!pkg.empty()) {
            std::string qkey;
            qkey.reserve(pkg.size() + 1 + base.size());
            qkey.append(pkg).append(".").append(base);
            if (auto it = struct_templates_.find(qkey); it != struct_templates_.end())
                return it->second;
        }
        return {};
    }
    // M2: composite-key variant. Caller passes a possibly-pkg-qualified
    // string in one argument (e.g. mono_scan's `struct_part`). Tries the
    // full string first; on miss with a dot present, retries with the
    // tail after the last dot. Mirrors find_struct_method_templates_
    // unguarded's semantics for the per-struct map.
    lir_view::StructView
    find_struct_template_unguarded(std::string_view qkey) const noexcept {
        if (auto it = struct_templates_.find(std::string(qkey));
            it != struct_templates_.end())
            return it->second;
        auto dot = qkey.rfind('.');
        if (dot != std::string_view::npos) {
            // B-mv-02: the bare-name fallback must NOT cross package
            // boundaries. `struct_templates_` keys every generic struct under
            // both `pkg.Name` and the bare `Name` (last-wins). When `qkey` is
            // package-qualified but has no qualified entry (e.g. a NON-generic
            // user `struct Box` whose package owns the name), the bare slot may
            // hold a same-name GENERIC struct from a DIFFERENT package (e.g.
            // `logos.mem.boxed.Box<T>`). Returning that makes mono treat the
            // user struct as generic and splice a method's type-arg into a
            // bogus `$G1$..` struct slot. Only accept the bare hit when its
            // owning package matches `qkey`'s.
            auto bare = qkey.substr(dot + 1);
            auto pkg  = qkey.substr(0, dot);
            if (auto it = struct_templates_.find(std::string(bare));
                it != struct_templates_.end() &&
                it->second.valid() && it->second.pkg() == pkg)
                return it->second;
            return {};
        }
        return {};
    }
    bool has_struct_template_pkg(std::string_view pkg, std::string_view base) const noexcept {
        if (pkg.empty()) return false;
        std::string qkey;
        qkey.reserve(pkg.size() + 1 + base.size());
        qkey.append(pkg).append(".").append(base);
        return struct_templates_.find(qkey) != struct_templates_.end();
    }
    StrMap<std::vector<lir_view::StructView>> struct_specs_;
    StrMap<std::pair<TypeRef, int>> needed_struct_insts_;
    StrSet struct_done_;
    StrMap<lir_view::EnumView>     enum_templates_;   // Stage E: decl mirrors
    // M2: enum_templates_ is currently bare-keyed only (mono.cpp:192).
    // The single lookup site (mono_clone.cpp:4048) takes a bare base
    // extracted from a mangled cname. Helper wraps the direct .find for
    // call-site symmetry with struct_template helpers; M3 can swap the
    // backing store without touching the call site.
    std::optional<lir_view::EnumView>
    find_enum_template_bare(std::string_view base) const noexcept {
        if (auto it = enum_templates_.find(std::string(base));
            it != enum_templates_.end())
            return it->second;
        return std::nullopt;
    }
    StrMap<std::pair<std::vector<TypeRef>, int>> needed_enum_insts_;
    StrSet enum_done_;
    StrSet done_;
    StrMap<TypeRef> assoc_impls_;

    // Blanket impls indexed for AssocType resolution at mono time.
    // Entry: { trait, bound_trait, target_typevar, assoc_types_map }.
    struct BlanketImplInfo {
        std::string trait_name;
        std::string bound_trait;                  // primary (first) bound
        std::vector<std::string> extra_bounds;    // bounds[1..] for AND-filter
        std::string target_typevar;
        StrMap<TypeRef> assoc_types;
        // ADR 0008: assoc-type equality clauses on primary/extra bounds.
        std::vector<std::pair<std::string, TypeRef>> primary_assoc_eqs;
        std::vector<std::pair<std::string,
            std::vector<std::pair<std::string, TypeRef>>>> extra_assoc_eqs;
    };
    std::vector<BlanketImplInfo> blanket_impls_;

    // Concrete types coerced to `dyn <Trait>` somewhere in the program that the
    // eager blanket pass does NOT cover: PRIMITIVES (`&i64 as &dyn`) and GENERIC
    // STRUCT INSTANTIATIONS (`&Box<i64> as &dyn` — created post-blanket-loop by
    // instantiate_struct_templates). Collected by scan_expr from `as dyn` casts.
    // Eagerly cloning a blanket for ALL primitives is unsafe (an integer-bodied
    // blanket fails to verify on f32/f64), so after the drain we instantiate each
    // blanket ONLY for these actually-coerced targets. trait → {concrete name →
    // its TypeRef} (the TypeRef drives the blanket substitution directly, so the
    // target struct need not yet be in out_.structs).
    StrMap<StrMap<TypeRef>> dyn_coerced_targets_;
    // Enqueue every `$blanket$…__method` template (matching tmpl_prefix) cloned
    // for `concrete` (candidate_t) into the worklist as `concrete__method`.
    void enqueue_blanket_concrete(const BlanketImplInfo& bi,
                                  const std::string& tmpl_prefix,
                                  const std::string& concrete, TypeRef candidate_t);

    // Prefilter: only `$blanket$…__method` template fns can ever match a
    // tmpl_prefix, yet they are a tiny fraction of in_.functions. The eager
    // blanket pass calls enqueue_blanket_concrete once per (blanket × candidate
    // type) — without a prefilter each call re-scans EVERY function. Build the
    // (pkg-stripped-name, fn) list of `$blanket$` templates ONCE (in_.functions
    // is stable through the whole blanket pass); the scan iterates it instead.
    // string_view points into the fn's stable arena name storage.
    std::vector<std::pair<std::string_view, lir_view::FunctionView>> blanket_tmpl_fns_;
    bool blanket_tmpl_built_ = false;
    void ensure_blanket_tmpl_index();

    // Set of (trait::type) keys: concrete types that implement each trait.
    // Populated from out_.impls (non-blanket) so the blanket fallback can
    // verify a concrete type satisfies the bound.
    StrSet concrete_impls_;

    // Sprint 5: side-by-side trait_engine driving the same queries as
    // mono_has_impl_recursive. Populated lazily from concrete_impls_ +
    // blanket_impls_ on first use; cleared & repopulated when those
    // tables change (drain_method_worklist etc.). Once parity is
    // validated, mono_has_impl_recursive collapses into a thin wrapper.
    trait_engine::TraitEngine trait_engine_;
    bool                      trait_engine_dirty_ = true;

    // Populate trait_engine_ from concrete_impls_ + blanket_impls_.
    // Cheap: a few hundred entries even for medium codebases.
    void populate_trait_engine_();
    // Name → TypeRef for the auto-trait shape predicates (see .cpp).
    TypeRef mono_typeref_by_name_(const std::string& n);

    // Reverse uid → TypeRef table populated when a metaprog `Type` value
    // is emitted. Drives `reify_type` and (later) `quote_ty!` antiquot
    // reification: read the uid out of a const-folded `Type` struct lit,
    // recover the source TypeRef from this map.
    std::unordered_map<uint64_t, TypeRef> uid_to_type_;

    // var name → unsubstituted RHS ExprRef of its `let` binding. Lets
    // __reify_type__/__type_apply__ trace a Type-typed VarRef back to the
    // producer expression at compile time. Populated by subst_stmt's Let
    // case; current MVP keeps a flat map (no scope/shadow handling).
    std::unordered_map<std::string, lir_view::ExprRef> type_let_inits_;

    // T2-24 (B): a compile-time-constant value baked into a const-arg
    // specialization. `is_enum` → emit an EnumLit(enum_name, variant, disc);
    // else an IntLit(ival). `type` is the param's type (the literal's type).
    struct ConstArgVal {
        bool        is_enum = false;
        int64_t     ival = 0;          // int value, or enum discriminant
        std::string enum_name;
        std::string variant;
        TypeRef     type = nullptr;
    };

    struct WorkItem {
        std::string                mangled;
        lir_view::FunctionView     tmpl;
        SubstMap                   subst;
        PackMap                    packs;
        int                        depth;
        // T2-24 (B): param name → baked constant. Empty for ordinary clones.
        // Set into current_const_args_ around clone_fn so subst_expr replaces
        // each VarRef(param) with the literal.
        std::vector<std::pair<std::string, ConstArgVal>> const_args;
    };
    std::vector<WorkItem> worklist_;

    // T2-24 (B): const-arg specialization state.
    //  * const_want_cache_: fn base name → param indices whose value, when a
    //    compile-time literal at a call site, is worth baking (the param
    //    forwards — directly or transitively — to an intrinsic position that
    //    const-evaluates it, e.g. atomic Ordering). Memoized; recursion-guarded
    //    by const_want_inflight_.
    //  * current_const_args_: active during a const-arg-specialised clone_fn.
    StrMap<std::vector<size_t>> const_want_cache_;
    StrSet                      const_want_inflight_;
    StrMap<ConstArgVal>         current_const_args_;

    // L1: lazy struct-method instantiation (default OFF — eager scheme is
    // preserved by clone_struct_def; this infrastructure exists so callers
    // can opt-in via `lazy_methods_`. The full flip is staged across L1.4-L1.6.)
    //
    // Index: base struct/enum name → method name → method template fn (lives
    // in the input struct's sd.methods, heap-stable through unique_ptr).
    StrMap<StrMap<lir_view::FunctionView>> struct_method_templates_;
    // M2: centralized struct_method_templates_ outer lookup. Tries
    // `pkg.base` first; falls back to bare `base` ONLY if the struct
    // isn't registered in this pkg at all (the pkg_struct_exists guard
    // prevents leaking methods from a same-named struct in a different
    // pkg when the in-pkg struct simply has no methods). Returns nullptr
    // on miss; caller does the inner-map method-name lookup. Used at
    // mono.cpp / mono_scan.cpp sites that have the guard; mono_clone.cpp
    // :2338 uses an unguarded pkg-first-then-bare pattern and stays on
    // the direct .find for now.
    // M2: unguarded variant for sites that pass a composite (possibly-
    // pkg-qualified) string in one argument, or that don't have the
    // pkg_struct_exists guard for historical reasons (mono_clone.cpp
    // :2338). Tries the full string first; on miss, if there's a dot
    // present, retries with the portion after the last dot.
    const StrMap<lir_view::FunctionView>*
    find_struct_method_templates_unguarded(std::string_view qkey) const noexcept {
        if (auto it = struct_method_templates_.find(std::string(qkey));
            it != struct_method_templates_.end())
            return &it->second;
        auto dot = qkey.rfind('.');
        if (dot != std::string_view::npos) {
            if (auto it = struct_method_templates_.find(std::string(qkey.substr(dot + 1)));
                it != struct_method_templates_.end())
                return &it->second;
        }
        return nullptr;
    }
    const StrMap<lir_view::FunctionView>*
    find_struct_method_templates_guarded(std::string_view pkg, std::string_view base) const noexcept {
        bool pkg_exists = has_struct_template_pkg(pkg, base);
        if (!pkg.empty()) {
            std::string qkey;
            qkey.reserve(pkg.size() + 1 + base.size());
            qkey.append(pkg).append(".").append(base);
            if (auto it = struct_method_templates_.find(qkey); it != struct_method_templates_.end())
                return &it->second;
        }
        if (!pkg_exists) {
            if (auto it = struct_method_templates_.find(std::string(base));
                it != struct_method_templates_.end())
                return &it->second;
        }
        return nullptr;
    }

    struct MethodWorkItem {
        std::string             concrete_struct; // e.g. "Foo$G1$i32"
        std::string             struct_pkg;      // pkg of receiver struct (for cross-pkg disambig)
        std::string             base_struct;     // e.g. "Foo"
        std::string             method_name;     // short name (e.g. "bar"), used as dest suffix
        lir_view::FunctionView  tmpl;            // resolved template (overload-aware)
        SubstMap                subst;
        PackMap                 packs;
        int                     depth;
    };
    std::vector<MethodWorkItem> method_worklist_;
    StrSet done_methods_;              // "ConcreteStruct__method" markers

    // L1.2: concrete-struct-name → its TypeRef. Populated during
    // instantiate_struct_templates so dispatch-emission can re-derive subst
    // for trait-method root-pinning without re-parsing mangled names.
    StrMap<TypeRef> concrete_struct_types_;

    // L1.5: deferred method-instance enqueues from ECall hook. When the
    // hook fires before the receiver struct has been instantiated, we park
    // (concrete-cname, method-name) here; the L1.1 fixpoint resolves them
    // after each instantiate_struct_templates pass.
    std::vector<std::pair<std::string, std::string>> deferred_method_enqueues_;

    // Pinned method roots: "ConcreteStruct__method" set populated by
    // L1.2 (trait dispatch entries) and L1.3 (is_root_pin annotations).
    // In lazy mode any method in this set is force-instantiated even if
    // no call site references it.
    StrSet pinned_method_roots_;

    // L1.6: default true (lazy method cloning). `LOGOS_LAZY_METHODS=0`
    // restores eager codegen — only used for bisecting regressions.
    bool lazy_methods_ = true;

    // Opt-in reachability filter (set via MonoOpts.entry_points). When
    // non-empty, run() switches the non-generic free-fn loop from "clone
    // every fn" to "clone only fns transitively reachable from these names":
    // entry points seed free_fn_queue_, scan_fn discovers callees, and the
    // existing generic-fn / lazy-method drains pull in the rest of the
    // closure. Used by the metacall-thunk JIT compile (entry_points = the
    // `__metacall_thunk_*` names) so it stops cloning the whole user program
    // (test fns + their iterator monomorphizations) it never executes.
    StrSet entry_points_;

    // Deferred-emission poison guard. A generic call whose substituted type
    // args still contain Error (an unresolved name — typically a type a
    // metaprog derive EMITS later, referenced by same-build code at dispatch
    // iter 0, before the emission exists) must not be instantiated: the clone
    // would carry error-typed exprs into mlir_gen, which half-lowers it and
    // crashes the LLVM pipeline (this killed the trigger hooks that would
    // have EMITTED the missing type). Instead the instantiation is skipped
    // and the SCANNING fn is recorded here; run()'s tail publishes these via
    // LProgram::poisoned_fns and mlir_gen gives each a TRAP body (defined —
    // so a stale same-named symbol in a prior build's archive can't satisfy
    // it — but loud if ever executed). Later passes (post-emission re-sema)
    // resolve the name and produce the real bodies.
    std::string scanning_fn_link_;   // link name of the fn scan_fn is walking
    StrSet      poisoned_fns_;       // link names to demote to trap stubs
    bool type_contains_error(TypeRef t, int depth = 0) const;

    // Struct instantiations whose TEMPLATE was missing (same deferred-emission
    // class: the template is a macro-emitted struct that doesn't exist yet at
    // dispatch iter 0). instantiate_struct_templates records them; run()'s
    // tail re-walks fn bodies in probe mode (missing_probe_ set) and demotes
    // every fn referencing one — those bodies would otherwise half-lower.
    StrSet missing_struct_insts_;
    const StrSet* missing_probe_ = nullptr;  // non-null → record_needed_struct probes
    bool missing_hit_ = false;               // probe result for the current fn

    // Prune-mode (entry_points_ non-empty) free-fn reachability worklist.
    // free_fn_index_ maps every non-generic free fn's name to its template;
    // enqueue_free_fn pushes a name once; drain_free_fn_queue clones each.
    std::unordered_map<std::string, lir_view::FunctionView> free_fn_index_;
    std::vector<std::string> free_fn_queue_;
    StrSet free_fn_queued_;
    void enqueue_free_fn(const std::string& name);
    void drain_free_fn_queue();

    // ── Type substitution (large — defined in mono_subst.cpp) ────────────
    TypeRef subst_type(TypeRef tv, const SubstMap& s) noexcept;

    // Phase 5.B step 3: when a TypeRef points into a foreign arena (cross-
    // arena body walk), re-intern it into out_.type_pool so its offset is
    // meaningful when stored in local LIR mirrors. Recurses through every
    // child type. No-op for local TypeRefs.
    TypeRef localize_type(TypeRef tv) noexcept;

    // Structural FNV-1a-64 hash of T (mini-Memoria block_type_hash). Layout-
    // stable: ignores struct/field names, recurses into field types using
    // the same SubstMap shape as __field_types_of__. Cycle-guarded via the
    // caller-provided seen set. Defined in mono_clone.cpp.
    uint64_t compute_type_hash(TypeRef t, StrSet& seen) noexcept;

    // Pattern substitution — view-based walk over the input mirror.
    lir::Pattern subst_pattern(const lir::Pattern& pat, const SubstMap& s);
    lir::Pattern subst_pattern(lir_view::PatRef pref, const SubstMap& s);

    // Build a TypeRef for a concrete struct/enum/primitive named
    // `name`. Walks out_.structs / out_.enums to find the pkg-aware
    // entry; falls back to primitive Kind if `name` is a known scalar.
    // Returns null TypeRef if `name` doesn't match anything. Centralises
    // the per-candidate TypeRef construction that previously got
    // duplicated at every eager-instantiation / deep-bound-check site.
    // Generic-struct TypeRef with pkg threaded from an explicit pkg or (when
    // empty) the template definition in out_.structs. THE mono-side analog of
    // sema's make_generic_struct — use this, never an inline LogosTypeBuilder
    // (antipat_inline_typebuilder: each inline site risks dropping pkg_name).
    TypeRef build_generic_struct_typeref(const std::string& name,
                                         std::vector<TypeRef> args,
                                         std::string pkg = {}) {
        LogosTypeBuilder sb;
        sb.kind = LogosType::Kind::Struct;
        sb.struct_name = name;
        if (pkg.empty())
            for (auto& s : out_.structs)
                if (s.name() == name) { pkg = std::string(s.pkg()); break; }
        sb.pkg_name  = std::move(pkg);
        sb.type_args = std::move(args);
        return out_.type_pool.alloc(std::move(sb));
    }

    TypeRef build_concrete_typeref(const std::string& name) {
        // Struct (incl. ZonedStruct).
        for (auto& sd : out_.structs)
            if (sd.name() == name) {
                LogosTypeBuilder st;
                st.kind = sd.is_zoned() ? LogosType::Kind::ZonedStruct
                                      : LogosType::Kind::Struct;
                st.struct_name = name;
                st.pkg_name    = std::string(sd.pkg());
                return out_.type_pool.alloc(std::move(st));
            }
        // Enum.
        for (auto& ed : out_.enums)
            if (ed.name() == name) {
                LogosTypeBuilder et;
                et.kind = LogosType::Kind::Enum;
                et.enum_name = name;
                et.pkg_name  = std::string(ed.pkg());
                return out_.type_pool.alloc(std::move(et));
            }
        // Primitive scalar.
        LogosType::Kind sk = LogosType::Kind::Error;
        if      (name == "u8")    sk = LogosType::Kind::U8;
        else if (name == "u16")   sk = LogosType::Kind::U16;
        else if (name == "u32")   sk = LogosType::Kind::U32;
        else if (name == "u64")   sk = LogosType::Kind::U64;
        else if (name == "i8")    sk = LogosType::Kind::I8;
        else if (name == "i16")   sk = LogosType::Kind::I16;
        else if (name == "i32")   sk = LogosType::Kind::I32;
        else if (name == "i64")   sk = LogosType::Kind::I64;
        else if (name == "f32")   sk = LogosType::Kind::F32;
        else if (name == "f64")   sk = LogosType::Kind::F64;
        else if (name == "bool")  sk = LogosType::Kind::Bool;
        else if (name == "usize") sk = LogosType::Kind::Usize;
        else if (name == "isize") sk = LogosType::Kind::Isize;
        else if (name == "char")  sk = LogosType::Kind::Char;
        if (sk != LogosType::Kind::Error) {
            LogosTypeBuilder pt; pt.kind = sk;
            return out_.type_pool.alloc(std::move(pt));
        }
        return nullptr;
    }

    // Pkg-qualified workspace key. When tr.pkg_name() is set, the key
    // is "pkg.bare"; otherwise just "bare". Two same-named structs from
    // different pkgs (user's `Box<i64>` vs `std.mem.box.Box<i64>`) get
    // distinct keys so both can be cloned in one compilation.
    static std::string qualified_cname(TypeRef tr) {
        auto bare = concrete_struct_name(tr);
        auto pkg = tr.pkg_name();
        if (pkg.empty()) return bare;
        std::string r;
        r.reserve(pkg.size() + 1 + bare.size());
        r.append(pkg); r.push_back('.'); r.append(bare);
        return r;
    }

    // ── Substitution-complete gate (Phase 1) ──────────────────────────────
    //
    // True iff `tv` contains a TypeVar at any depth (own kind, nested
    // pointee, array element, tuple element, generic type-args, fn
    // params/return). Used to gate trait-method clones and the
    // record_needed_* instantiation queue so that nested-generic
    // shapes like `Option<(A, B)>` or `Result<Option<T>, E>` aren't
    // eagerly cloned while their inner TypeVars are still unresolved
    // (the eager-clone path used to produce `OptionIter$G1$<error>`
    // wrappers mlir-gen couldn't lower —
    // [[baghunt-mono-eager-typevar-default-clone]]).
    //
    // The pre-existing shallow check `t.kind() == Kind::TypeVar` only
    // caught immediate top-level TypeVars; this recurses through
    // every kind so nested forms get blocked too.
    static bool contains_typevar(TypeRef tv) noexcept {
        if (!tv) return false;
        if (tv.kind() == LogosType::Kind::TypeVar) return true;
        // Phase 1: Error-kind types are sema's placeholder for unresolved
        // references; mangling them produces `<error>` in spec names which
        // the mlir-gen layer can't lower. Treat them as "unresolved" for
        // gating purposes — same hazard class as TypeVar.
        if (tv.kind() == LogosType::Kind::Error) return true;
        for (auto a : tv.type_args()) if (contains_typevar(a)) return true;
        if (tv.pointee() && contains_typevar(tv.pointee())) return true;
        if (tv.elem()    && contains_typevar(tv.elem()))    return true;
        for (auto e : tv.tuple_elems())    if (contains_typevar(e)) return true;
        for (auto p : tv.closure_params()) if (contains_typevar(p)) return true;
        if (tv.closure_ret() && contains_typevar(tv.closure_ret())) return true;
        return false;
    }

    // A pattern arg carrying an associated-type projection (`D::Resources`
    // in `impl<D: Device> Tr for Foo<D, D::Resources>`) can't be structurally
    // unified against a concrete arg — its resolution needs the trait impl
    // table. Such positions must DEFER (bind nothing), not report a mismatch.
    static bool contains_assoc_type(TypeRef tv) noexcept {
        if (!tv) return false;
        if (tv.kind() == LogosType::Kind::AssocType) return true;
        for (auto a : tv.type_args()) if (contains_assoc_type(a)) return true;
        if (tv.pointee() && contains_assoc_type(tv.pointee())) return true;
        if (tv.elem()    && contains_assoc_type(tv.elem()))    return true;
        for (auto e : tv.tuple_elems()) if (contains_assoc_type(e)) return true;
        return false;
    }

    // ── Record needed instantiations (small — inline) ────────────────────
    void record_needed_struct(TypeRef tr) {
        if (!tr || (tr.kind() != LogosType::Kind::Struct &&
                    tr.kind() != LogosType::Kind::ZonedStruct) ||
            tr.type_args().empty()) return;
        // Phase 1: recursive TypeVar check (was shallow `kind() == TypeVar`
        // per-arg; missed nested forms like `Foo<(A, B)>` where the tuple
        // wraps the TypeVars).
        for (auto a : tr.type_args())
            if (contains_typevar(a)) return;
        if (missing_probe_) {           // probe mode: no recording, just a hit test
            if (missing_probe_->count(qualified_cname(tr))) missing_hit_ = true;
            return;
        }
        auto cname = qualified_cname(tr);
        if (!struct_done_.count(cname)) {
            if (depth_ >= max_depth_) {
                in_.diags.diags.push_back({Diag::Level::Error, "mono",
                    std::format("struct instantiation depth limit ({}) exceeded for '{}'",
                                max_depth_, cname), {}, 0});
                return;
            }
            needed_struct_insts_[cname] = {tr, depth_ + 1};
        }
    }

    void record_needed_enum(TypeRef tr) {
        if (!tr || tr.kind() != LogosType::Kind::Enum || tr.type_args().empty()) return;
        // Phase 1: recursive TypeVar check (was shallow per-arg).
        for (auto a : tr.type_args())
            if (contains_typevar(a)) return;
        std::string cname = std::string(tr.enum_name());
        for (auto a : tr.type_args()) { cname += "__"; cname += mangle_type(a); }
        if (!enum_done_.count(cname)) {
            if (depth_ >= max_depth_) {
                in_.diags.diags.push_back({Diag::Level::Error, "mono",
                    std::format("enum instantiation depth limit ({}) exceeded for '{}'",
                                max_depth_, cname), {}, 0});
                return;
            }
            needed_enum_insts_[cname] = {tr.type_args(), depth_ + 1};
        }
    }

    // FQN-checked stdlib `logos.mem.boxed.Box<T>` (not a user struct named Box).
    static bool is_stdlib_box(TypeRef t) {
        if (!t) return false;
        auto k = TypeRef(t).kind();
        if (k != LogosType::Kind::Struct && k != LogosType::Kind::ZonedStruct)
            return false;
        if (TypeRef(t).struct_name() != "Box") return false;
        auto pkg = TypeRef(t).pkg_name();
        return pkg.empty() || pkg == "logos.mem.boxed";
    }

    // ── Mangling (static — inline) ────────────────────────────────────────
public:
    static std::string mangle_type(TypeRef tr) {
        if (!tr) return "null";
        switch (tr.kind()) {
        case LogosType::Kind::Ptr:
            return (tr.mut_ptr() ? "pmut_" : "pcst_") + mangle_type(tr.pointee());
        case LogosType::Kind::Ref:    return "ref_"    + mangle_type(tr.pointee());
        case LogosType::Kind::MutRef: return "refmut_" + mangle_type(tr.pointee());
        case LogosType::Kind::Array:
            return "arr" + std::to_string(tr.arr_size()) + "_" + mangle_type(tr.elem());
        case LogosType::Kind::Struct:
            // G156-1: the package fingerprint is folded into concrete_struct_name's
            // canonical identity (byte-identical to sema's mangle_type_for_name).
            return concrete_struct_name(tr);
        // Nested generic enum: bare `type_str(Option<T>)` returns just "Option",
        // dropping inner type-args. For nested specs like `Option<Option<i32>>`
        // we need the inner instance encoded so `record_needed_enum` and
        // payload-layout lookup agree.
        case LogosType::Kind::Enum: {
            // Skip recursive mangling if any type-arg is unresolved
            // (TypeVar) — emitting "T" into a mangled spec name leaks
            // unresolved symbols. Fall back to the bare enum name in
            // that case (matching the pre-2026-05-14 behaviour).
            std::function<bool(TypeRef)> has_tv = [&](TypeRef t) -> bool {
                if (!t) return false;
                if (TypeRef(t).kind() == LogosType::Kind::TypeVar) return true;
                for (auto a : t.type_args()) if (has_tv(a)) return true;
                if (t.pointee() && has_tv(t.pointee())) return true;
                if (t.elem() && has_tv(t.elem())) return true;
                return false;
            };
            // Coexistence + G156-1: fold module_id (and package, for ambiguous
            // names) into the enum identity — byte-identical to sema.
            std::string esuf = type_module_suffix(tr.enum_name(), tr.pkg_name());
            for (auto a : tr.type_args()) if (has_tv(a)) return std::string(tr.enum_name()) + esuf;
            std::string r = std::string(tr.enum_name()) + esuf;
            for (auto a : tr.type_args()) {
                r += "__";
                r += mangle_type(a);
            }
            return r;
        }
        case LogosType::Kind::IntLit:
        case LogosType::Kind::ConstVar:
            // Const-generic args (scalar or pack element) carry their value in
            // const_val. Mangle as `cN_<v>` (negatives as `cN_n<v>`) so distinct
            // values yield distinct symbol names.
            if (tr.const_val()) {
                int64_t v = *tr.const_val();
                if (v < 0) return "cN_n" + std::to_string(-v);
                return "cN_" + std::to_string(v);
            }
            return type_str(tr);
        case LogosType::Kind::TraitObject:
            // Distinguish an OWNING Box<dyn T> from a borrowed &dyn T so that
            // e.g. Vec<Box<dyn T>> and Vec<&dyn T> mangle to DISTINCT specs —
            // otherwise mono collapses them and may bind the element type-var
            // to the borrow form, dropping the owning bit (→ no element drop,
            // leak). Borrowed &dyn keeps its historical type_str mangling.
            if (tr.owning_trait_object()) {
                std::string r = "owndyn_" + std::string(tr.trait_name());
                for (auto a : tr.type_args()) { r += "__"; r += mangle_type(a); }
                return r;
            }
            return type_str(tr);
        default:
            return type_str(tr);
        }
    }

    static std::string mangle(const std::string& name,
                               const std::vector<TypeRef>& type_args) {
        std::string result = name;
        for (auto t : type_args) {
            result += "__";
            result += mangle_type(t);
        }
        return result;
    }

    // Walk a type tree and append a "<name>$M<module_id>" tag for EVERY nominal
    // (struct/enum) node that belongs to a non-stdlib module. Recurses through
    // every composite child (ptr/ref/array/slice/tuple/generic-args/closure) so
    // a nested module type — e.g. the `Widget` in `Box<pkg::Widget>` — still
    // contributes its module identity even when the outer type is stdlib.
    static void collect_module_tags(TypeRef t, std::string& out) {
        if (!t) return;
        switch (t.kind()) {
        case LogosType::Kind::Struct:
        case LogosType::Kind::ZonedStruct: {
            std::string suf = type_module_suffix(t.struct_name(), t.pkg_name());
            if (!suf.empty()) { out += "|"; out += std::string(t.struct_name()); out += suf; }
            break;
        }
        case LogosType::Kind::Enum: {
            std::string suf = type_module_suffix(t.enum_name(), t.pkg_name());
            if (!suf.empty()) { out += "|"; out += std::string(t.enum_name()); out += suf; }
            break;
        }
        default: break;
        }
        if (t.pointee())     collect_module_tags(t.pointee(), out);
        if (t.elem())        collect_module_tags(t.elem(), out);
        for (auto a : t.type_args())     collect_module_tags(a, out);
        for (auto e : t.tuple_elems())   collect_module_tags(e, out);
        for (auto p : t.closure_params()) collect_module_tags(p, out);
        if (t.closure_ret()) collect_module_tags(t.closure_ret(), out);
    }

    // Canonical type-identity STRING for nominal runtime UID hashing
    // (`type_uid::<T>()` → Any / type_id / quote_ty reification). It is type_str
    // PLUS a module fingerprint: every non-stdlib MODULE type anywhere in the
    // tree contributes a "<name>$M<module_id>" tag, so two modules' same-named
    // `pkg::Type` (incl. when nested, e.g. `Box<pkg::Widget>`) hash to DISTINCT
    // runtime UIDs and cross-module Any/downcast can't confuse them (Cat C
    // coexistence). stdlib (`logos.*`) + no-module compiles ⇒ no tags ⇒
    // byte-identical to type_str (UIDs unchanged).
    static std::string type_id_canon(TypeRef t) {
        std::string s = type_str(t);
        collect_module_tags(t, s);
        return s;
    }
private:

    // ── Expression/statement cloning (large — defined in mono_clone.cpp) ─
    // Phase 5.B: signatures take view refs (ExprRef/StmtRef/BlockRef) instead
    // of C++ LExpr/LStmt/LBlock — so the body walk works both for local
    // bodies AND for cross-arena bodies (BlockRef constructed from a
    // body_external_ref resolution into a foreign arena's mirror). The view-
    // based path doesn't depend on in_.mirror_table for the source side.
    // Requires sema to keep mirror's TYPE field in sync with C++ LExpr.type
    // — see lir_mirror_update_type call sites for the 5 post-construction
    // type modifications.
    lir_view::ExprRef subst_expr(lir_view::ExprRef eref, const SubstMap& s,
                              const PackMap& /*unused*/ = {});
    lir_view::StmtRef subst_stmt(lir_view::StmtRef sref, const SubstMap& s);

    lir_view::BlockRef subst_block(lir_view::BlockRef bref, const SubstMap& s,
                             const PackMap& /*unused*/ = {}) {
        std::vector<lir_view::StmtRef> nb;
        if (!bref) return lir_mirror_block(out_, nb);
        bref.each_stmt([&](lir_view::StmtRef sref) {
            nb.push_back(subst_stmt(sref, s));
        });
        return lir_mirror_block(out_, nb);
    }

    DeclBuilder clone_fn(lir_view::FunctionView fn, const SubstMap& s,
                             const PackMap& packs = {});

    // Signature-only clone for binary-symbol fast path: copies name/flags
    // and substitutes param/return types, but leaves body empty (no deep
    // body walk). The result is suitable for mlir_gen's forward_declare —
    // mlir_gen skips body emission for binary_symbols fns anyway. Caller
    // must NOT call lir_mirror_emit_function / scan_fn on the result;
    // body.mirror_ptr_ stays default, and scan_fn early-returns on that.
    DeclBuilder clone_fn_signature(lir_view::FunctionView fn, const SubstMap& s,
                                       const PackMap& packs = {});

    // Auto-trait structural check.  Mirrors sema_auto_trait.cpp::is_auto_trait_satisfied
    // but operates on mono-side state (out_.structs/in_.structs, out_.enums, out_.traits,
    // concrete_impls_).  Used by clone_struct_def's bound gate and (future) auto-trait
    // verification at instantiation time.
    bool is_auto_satisfied(TypeRef tv, std::string_view trait_name, StrSet& visited);

    // L1.4: bound gate, factored out of clone_struct_def for re-use in
    // drain_method_worklist. Returns false when method `m`'s impl_type_params
    // bounds are not satisfied under substitution `s`.
    bool method_bound_ok(lir_view::FunctionView m, const SubstMap& s);

    // Recursive trait-satisfaction at mono-time: does `concrete_name`
    // implement `trait_name` directly via concrete_impls_, or transitively
    // via any chain of blanket_impls_? Per-attempt copy of `seen` keeps
    // sibling blanket candidates from poisoning each other (see
    // method_bound_ok's local has_impl, factored here for reuse).
    bool mono_has_impl_recursive(const std::string& trait_name,
                                 const std::string& concrete_name,
                                 StrSet& seen);

    // Structure-aware `$ref_`/`$mut_ref_` impl key for a reference target
    // (`impl Trait for &T`). "" for non-ref types. See definition.
    std::string ref_target_key(TypeRef t);

    // Stronger sibling of mono_has_impl_recursive that takes a full
    // TypeRef instead of a stripped name. For a blanket impl
    // `impl<T: Foo> Bar for Vec<T>`, mono_has_impl_recursive on
    // "Bar" + "Vec" returns true unconditionally — even when
    // T=NoFoo. mono_concrete_satisfies_bound additionally
    // pattern-unifies the impl's `target_typeref` against the
    // concrete TypeRef and recurses into the impl's own
    // impl_type_params bounds with the unified substitution. So
    // `concrete_satisfies("Bar", Vec<NoFoo>)` correctly returns
    // false. Used by method_bound_ok to close the
    // `Option<Vec<NoFoo>>` cascade (see
    // [[baghunt-mono-blanket-bound-recursion]]).
    bool mono_concrete_satisfies_bound(const std::string& trait_name,
                                       TypeRef concrete,
                                       StrSet& seen);

    // ── Struct/enum cloning (large — defined in mono_clone.cpp) ─────
    DeclBuilder clone_struct_def(lir_view::StructView tmpl,
                                      const SubstMap& s,
                                      const PackMap& packs,
                                      const std::string& new_name);
    lir_view::EnumView clone_enum_def(lir_view::EnumView tmpl,
                                    const SubstMap& s,
                                    const PackMap& packs,
                                    const std::string& new_name);

    // ── Scan (defined in mono_scan.cpp) ───────────────────────────────────
    // Mirror-dispatched: scan_fn looks up the function's body offset in
    // out_.mirror_table and walks through view types from there. This
    // requires lir_mirror_emit_function to have been called for `fn`
    // before scan_fn — see clone+push_back sites in mono.cpp.
    void scan_fn(lir_view::FunctionView fn);
    void scan_block(lir_view::BlockRef b);
    void scan_stmt(lir_view::StmtRef s);
    void scan_expr(lir_view::ExprRef e);

    // ── Pattern matching (static — inline) ───────────────────────────────
    static bool match_type(TypeRef c, TypeRef p,
                           SubstMap& bindings) noexcept {
        if (!c || !p) return false;
        // ConstVar (`impl<const CFG: WritStatic> … for Node<CFG>`) binds by
        // name exactly like a TypeVar — falling through to the kind-equality
        // check below would report a false mismatch against the concrete arg.
        if (p.kind() == LogosType::Kind::TypeVar ||
            p.kind() == LogosType::Kind::ConstVar) {
            auto tvn = p.type_var_name();
            auto it = bindings.find(tvn);
            if (it != bindings.end())
                return types_equal(c, TypeRef(it->second));
            bindings[std::string(tvn)] = c;
            return true;
        }
        // Slice patterns ([E]) match either slice spelling of the concrete —
        // bare-[T]-as-type-arg canonicalises to UnsizedSlice, but Slice
        // reaches here from some paths; one type at this level (mirrors
        // sema's match_type_sema).
        if ((p.kind() == LogosType::Kind::Slice ||
             p.kind() == LogosType::Kind::UnsizedSlice) &&
            (c.kind() == LogosType::Kind::Slice ||
             c.kind() == LogosType::Kind::UnsizedSlice))
            return match_type(c.elem(), p.elem(), bindings);
        if (p.kind() != c.kind()) return false;
        switch (p.kind()) {
        case LogosType::Kind::Ptr:
            return p.mut_ptr() == c.mut_ptr() &&
                   match_type(c.pointee(), p.pointee(), bindings);
        case LogosType::Kind::Ref:
        case LogosType::Kind::MutRef:
            return match_type(c.pointee(), p.pointee(), bindings);
        case LogosType::Kind::Array:
            return p.arr_size() == c.arr_size() &&
                   match_type(c.elem(), p.elem(), bindings);
        case LogosType::Kind::Struct:
            return p.struct_name() == c.struct_name();
        default:
            return types_equal(c, p);
        }
    }

    // CP-cm-16 follow-up: deep unification used by instantiate_enum_templates
    // to bind impl-level type-params from a partial-spec impl-target pattern
    // (`Result<Vec<T>, E>`) against the concrete receiver (`Result<Vec<i32>, i32>`).
    // Walks Struct / Enum / Tuple / Slice / Array / Ptr / Ref type-args
    // (which `match_type` deliberately doesn't, since several spec-pattern
    // callers rely on shallow name-only matching). Returns false on
    // structural mismatch or conflicting TypeVar bindings; succeeds with
    // partial binding when the concrete side has TypeVars (binds pattern's
    // TypeVar to concrete's TypeVar).
    static bool unify_impl_target(TypeRef c, TypeRef p,
                                  SubstMap& bindings) noexcept {
        if (!c || !p) return false;
        // ConstVar (`impl<const CFG: WritStatic> … for Node<CFG>`) binds by
        // name exactly like a TypeVar — falling through to the kind-equality
        // check below would report a false mismatch against the concrete arg.
        if (p.kind() == LogosType::Kind::TypeVar ||
            p.kind() == LogosType::Kind::ConstVar) {
            auto tvn = p.type_var_name();
            auto it = bindings.find(tvn);
            if (it != bindings.end())
                return types_equal(c, TypeRef(it->second));
            bindings[std::string(tvn)] = c;
            return true;
        }
        // Slice patterns match either slice spelling (Slice / UnsizedSlice —
        // bare-[T] type-args canonicalise to the latter; one type here).
        {
            auto is_sl = [](TypeRef t) {
                return t.kind() == LogosType::Kind::Slice ||
                       t.kind() == LogosType::Kind::UnsizedSlice;
            };
            if (is_sl(p) && is_sl(c))
                return unify_impl_target(c.elem(), p.elem(), bindings);
        }
        if (p.kind() != c.kind()) return false;
        switch (p.kind()) {
        case LogosType::Kind::Ptr:
            if (p.mut_ptr() != c.mut_ptr()) return false;
            return unify_impl_target(c.pointee(), p.pointee(), bindings);
        case LogosType::Kind::Ref:
        case LogosType::Kind::MutRef:
            return unify_impl_target(c.pointee(), p.pointee(), bindings);
        case LogosType::Kind::Array:
            if (p.arr_size() != c.arr_size()) return false;
            return unify_impl_target(c.elem(), p.elem(), bindings);
        case LogosType::Kind::Struct:
        case LogosType::Kind::ZonedStruct: {
            if (p.struct_name() != c.struct_name()) return false;
            auto pa = p.type_args();
            auto ca = c.type_args();
            if (pa.size() != ca.size()) return false;
            for (size_t i = 0; i < pa.size(); ++i)
                if (!unify_impl_target(ca[i], pa[i], bindings)) return false;
            return true;
        }
        case LogosType::Kind::Enum: {
            if (p.enum_name() != c.enum_name()) return false;
            auto pa = p.type_args();
            auto ca = c.type_args();
            if (pa.size() != ca.size()) return false;
            for (size_t i = 0; i < pa.size(); ++i)
                if (!unify_impl_target(ca[i], pa[i], bindings)) return false;
            return true;
        }
        case LogosType::Kind::Tuple: {
            auto pe = p.tuple_elems();
            auto ce = c.tuple_elems();
            if (pe.size() != ce.size()) return false;
            for (size_t i = 0; i < pe.size(); ++i)
                if (!unify_impl_target(ce[i], pe[i], bindings)) return false;
            return true;
        }
        default:
            return types_equal(c, p);
        }
    }

    // Collect the free TypeVar/ConstVar names of an impl-target pattern in
    // first-appearance order, mirroring unify_impl_target's traversal. Used to
    // recover the impl-level type-param names (e.g. T in `impl<T> Pin<&T>`) for
    // a cross-package impl method whose own FunctionDraft.type_params is empty —
    // the method-instantiation enqueue binds the call's type_args to these.
    static void collect_pattern_typevars(TypeRef p, std::vector<std::string>& out) {
        if (!p) return;
        auto k = p.kind();
        if (k == LogosType::Kind::TypeVar || k == LogosType::Kind::ConstVar) {
            std::string n(p.type_var_name());
            for (auto& e : out) if (e == n) return;  // dedup, keep first position
            out.push_back(std::move(n));
            return;
        }
        if (p.pointee()) collect_pattern_typevars(p.pointee(), out);
        if (p.elem())    collect_pattern_typevars(p.elem(), out);
        for (auto a : p.type_args())    collect_pattern_typevars(a, out);
        for (auto e : p.tuple_elems())  collect_pattern_typevars(e, out);
    }

    static int type_specificity(TypeRef tr) noexcept {
        if (!tr || tr.kind() == LogosType::Kind::TypeVar) return 0;
        if (tr.kind() == LogosType::Kind::Ptr)   return 1 + type_specificity(tr.pointee());
        if (tr.kind() == LogosType::Kind::Array)  return 1 + type_specificity(tr.elem());
        if (tr.kind() == LogosType::Kind::Slice ||
            tr.kind() == LogosType::Kind::UnsizedSlice)
            return 1 + type_specificity(tr.elem());
        return 100;
    }

    static int specificity_score(const std::vector<TypeRef>& patterns) noexcept {
        int s = 0;
        for (auto p : patterns) s += type_specificity(p);
        return s;
    }

    // Per-position specificity vector for lexicographic comparison.
    // Enables correct disambiguation of partial specs like Map<Bitmap,V> vs Map<K,AnyVal>
    // when both score equally by summed specificity but differ positionally.
    static std::vector<int> specificity_vec(const std::vector<TypeRef>& patterns) noexcept {
        std::vector<int> v;
        v.reserve(patterns.size());
        for (auto p : patterns) v.push_back(type_specificity(p));
        return v;
    }

    // ── Spec selection (defined in mono_scan.cpp) ─────────────────────────
    lir_view::FunctionView find_best_spec(const std::string& base_name,
                                          const std::vector<TypeRef>& type_args);
    lir_view::StructView find_best_struct_spec(const std::string& base_name,
                                                  const std::vector<TypeRef>& type_args);

    // ── Enqueue / instantiate (defined in mono_scan.cpp) ─────────────────
    void enqueue_if_needed(const std::string& mangled_callee,
                           const std::vector<TypeRef>& type_args);

    // T2-24 (B): const-arg specialization helpers (impl in mono_const_arg.cpp).
    //  * const_intrinsic_positions: the seed registry — for a known intrinsic
    //    (atomic `*_ord`) returns the arg positions that mlir-gen const-reads
    //    (mirrors read_ordering_at). Empty for everything else.
    //  * compute_const_want: memoized const-want positions of `base` — registry
    //    positions for an intrinsic, else the param indices `base` forwards to a
    //    const-want position of a callee. Recursion-guarded.
    //  * find_fn_def_by_base: locate the (unmangled-base) FunctionDraft for the
    //    forwarding analysis / spec clone — free fns + struct methods in in_.
    static std::vector<size_t> const_intrinsic_positions(const std::string& name);
    const std::vector<size_t>& compute_const_want(const std::string& base);
    lir_view::FunctionView find_fn_def_by_base(const std::string& base);
    // Try to read a compile-time constant out of a (cloned) call argument.
    bool try_read_const_arg(lir_view::ExprRef arg, ConstArgVal& out);
    // At a finalized call: if the callee has const-want params filled with
    // compile-time literals, redirect `nc.callee` to a per-value spec and
    // enqueue it (clone with the literals baked). No-op otherwise.
    void maybe_const_specialize(lir::ECall& nc);
    // Core helper (also used at method→call rewrite sites): returns the callee
    // to emit — `callee` unchanged or a per-value spec name (spec enqueued).
    std::string const_specialize_callee(const std::string& callee,
                                        const std::vector<lir::LExprPtr>& args);

    // L1: enqueue a single struct-method instance for lazy codegen. Looks up
    // the method template via `struct_method_templates_`, builds a SubstMap
    // from the concrete struct's type_args, and pushes a MethodWorkItem if
    // the (concrete_struct, method) pair isn't already done. No-op when the
    // method or struct is unknown or already pinned.
    void enqueue_method_inst(TypeRef concrete_struct_t,
                             const std::string& method_name);

    // True once a generic struct instance named `cname` (bare concrete name,
    // e.g. "PkdArray$G1$uslice_u8") in package `pkg` has been materialized in
    // out_.structs. concrete_struct_types_ is populated EARLIER (before the
    // struct is pushed), so a method enqueue that keys off it alone can fire
    // while drain_method_worklist still has no target StructDraft to attach to —
    // the method then drops and its done_methods_ marker blocks any retry.
    // Method enqueue / deferred resolution gates on THIS, so a clone lands only
    // when emittable. MUST be pkg-aware: two coexisting same-name structs from
    // different pkgs (user `test.Box$G1$i64` vs stdlib `…box.Box$G1$i64`) both
    // exist, and drain matches BOTH name and pkg — a bare-name check would let an
    // enqueue through against the wrong-pkg twin's emission. Amortized O(1): the
    // key index grows incrementally (mono only ever appends structs).
    bool struct_emitted(std::string_view cname, std::string_view pkg) const {
        ++stats_.emitted_checks;
        for (; emitted_indexed_ < out_.structs.size(); ++emitted_indexed_) {
            auto& sd = out_.structs[emitted_indexed_];
            emitted_names_.insert(emitted_key(sd.name(), sd.pkg()));
        }
        return emitted_names_.find(emitted_key(cname, pkg)) != emitted_names_.end();
    }
    static std::string emitted_key(std::string_view name, std::string_view pkg) {
        std::string k(pkg);
        k += '\x1f';   // unit separator — absent from names/pkgs
        k += name;
        return k;
    }
    mutable size_t emitted_indexed_ = 0;
    mutable StrSet emitted_names_;

    // Drain method_worklist_: clone each pending method under its struct's
    // substitution, rename to "<concrete>__<method>", emit its decl mirror and
    // append the FunctionView to the matching StructDraft in out_.structs
    // (arena-stable mirror), and scan for further calls.
    void drain_method_worklist();

    DeclBuilder instantiate_fn(lir_view::FunctionView tmpl,
                                   const std::string& mangled_name,
                                   const SubstMap& subst,
                                   const PackMap& packs = {}) {
        ++depth_;
        auto fn = clone_fn(tmpl, subst, packs);
        fn.str_always(lir_schema::decl_keys::NAME, mangled_name);
        // Phase 2: instances are monomorphic — clone_fn already omits
        // TYPE_PARAMS (it never emits the rich fn type_params for clones),
        // so there is nothing to clear here. Mirror of the eager
        // blanket-instantiation path.
        --depth_;
        return fn;
    }

    // ── Struct/enum type collection (inline) ────────────────────────
    void collect_type_for_structs(TypeRef tr) {
        if (!tr) return;
        // Probe mode (deferred-emission guard): any Error / unresolved
        // residue in the type marks the probed fn for demotion.
        if (missing_probe_ && type_contains_error(tr)) {
            missing_hit_ = true;
            return;
        }
        switch (tr.kind()) {
        case LogosType::Kind::Ptr:
        case LogosType::Kind::Ref:
        case LogosType::Kind::MutRef:   collect_type_for_structs(tr.pointee()); break;
        case LogosType::Kind::Array: collect_type_for_structs(tr.elem());    break;
        case LogosType::Kind::Struct:
        case LogosType::Kind::ZonedStruct:
            record_needed_struct(tr);
            for (auto a : tr.type_args()) collect_type_for_structs(a);
            break;
        default: break;
        }
    }

    // ── Struct/enum needs collection (defined in mono_clone.cpp) ────
    void collect_struct_needs_from_output();
    void collect_struct_needs_from_block(lir_view::BlockRef b);
    void collect_struct_needs_from_stmt(lir_view::StmtRef s);
    void collect_struct_needs_from_expr(lir_view::ExprRef e);

    // ── Instantiation (defined in mono_clone.cpp) ─────────────────────────
    void instantiate_struct_templates();
    void instantiate_enum_templates();
};

} // namespace logos::compiler
