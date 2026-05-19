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
    // from .hermes0 v3 trailers (see mono.hpp). Stored only for now; future
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
        size_t   peak_fn_worklist     = 0;
        size_t   peak_method_worklist = 0;
        int      peak_depth           = 0;
    };
    Stats stats_;
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
    // from .hermes0 v3 trailers. Caller (main.cpp) keeps the value alive
    // for the duration of run(). Null = no exports available.
    const StdlibExports* stdlib_exports_ = nullptr;
    // Mirror of in_'s L-IR. Stage 3g.1: in_.mirror_table is the canonical
    // home — pre-populated by sema's LirBuilder for LExprs and topped up
    // by lir_mirror_emit_into() in run() for stmts/blocks/patterns.
    int            max_depth_;
    int            depth_ = 0;
    PackMap        cur_packs_;

protected:
    // Phase 5.A: source arena for refs constructed from the IN_-side variants.
    // mono moves in_.type_pool into out_ at run() start so historically these
    // refs were over out_.type_pool.arena() (which equals in_'s former arena).
    // When clone_fn reads a body that lives in a FOREIGN arena (Phase 5+B:
    // body_external_ref points into stdlib's published arena), it sets this
    // to that arena's pointer for the duration of the body walk.
    // nullptr → "use out_.type_pool.arena()" (legacy default).
    const hermes::Arena* src_arena_ = nullptr;

    const hermes::Arena* effective_src_arena() const noexcept {
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
    lir_view::ExprRef expr_ref_of(const lir::LExpr& e) const noexcept {
        if (e.mirror_offset_ == hermes::arena_offset_t{}) return {};
        return lir_view::ExprRef(effective_src_arena(), e.mirror_offset_);
    }
    lir_view::StmtRef stmt_ref_of(const lir::LStmt& s) const noexcept {
        if (s.mirror_offset_ == hermes::arena_offset_t{}) return {};
        return lir_view::StmtRef(effective_src_arena(), s.mirror_offset_);
    }
    lir_view::BlockRef block_ref_of(const lir::LBlock& b) const noexcept {
        if (b.mirror_offset_ == hermes::arena_offset_t{}) return {};
        return lir_view::BlockRef(effective_src_arena(), b.mirror_offset_);
    }
    lir_view::HermesValRef hv_ref_of(const lir::HermesVal& v) const noexcept {
        if (v.mirror_offset_ == hermes::arena_offset_t{}) return {};
        return lir_view::HermesValRef(effective_src_arena(), v.mirror_offset_);
    }
    // Reverse maps: ref → variant pointer. Used by subst_* to look up the
    // input variant whose kind is being substituted while reading sub-refs
    // through views. The input mirror_table lives on `in_`.
    const lir::LExpr* lexpr_of(lir_view::ExprRef r) const noexcept {
        if (!r || !in_.mirror_table) return nullptr;
        auto it = in_.mirror_table->expr_by_offset.find(uint32_t(r.offset()));
        return it == in_.mirror_table->expr_by_offset.end() ? nullptr : it->second;
    }
    const lir::LStmt* lstmt_of(lir_view::StmtRef r) const noexcept {
        if (!r || !in_.mirror_table) return nullptr;
        auto it = in_.mirror_table->stmt_by_offset.find(uint32_t(r.offset()));
        return it == in_.mirror_table->stmt_by_offset.end() ? nullptr : it->second;
    }
    const lir::LBlock* lblock_of(lir_view::BlockRef r) const noexcept {
        if (!r || !in_.mirror_table) return nullptr;
        auto it = in_.mirror_table->block_by_offset.find(uint32_t(r.offset()));
        return it == in_.mirror_table->block_by_offset.end() ? nullptr : it->second;
    }
    const lir::HermesVal* hermes_val_of(lir_view::HermesValRef r) const noexcept {
        if (!r || !in_.mirror_table) return nullptr;
        auto it = in_.mirror_table->hermes_val_by_offset.find(uint32_t(r.offset()));
        return it == in_.mirror_table->hermes_val_by_offset.end() ? nullptr : it->second;
    }
private:

    StrMap<const lir::LFunction*>  templates_;
    StrMap<std::vector<const lir::LFunction*>> specs_;
    StrMap<const lir::LStructDef*> struct_templates_;
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
    // backing map for a .hermes0-loaded exports table without rewriting
    // every lookup. Composite-key sites (mono_scan.cpp:487, mono_clone.cpp
    // :2454) take a single string and split internally — they stay on
    // direct .find for now.
    const lir::LStructDef*
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
        return nullptr;
    }
    const lir::LStructDef*
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
        return nullptr;
    }
    // M2: composite-key variant. Caller passes a possibly-pkg-qualified
    // string in one argument (e.g. mono_scan's `struct_part`). Tries the
    // full string first; on miss with a dot present, retries with the
    // tail after the last dot. Mirrors find_struct_method_templates_
    // unguarded's semantics for the per-struct map.
    const lir::LStructDef*
    find_struct_template_unguarded(std::string_view qkey) const noexcept {
        if (auto it = struct_templates_.find(std::string(qkey));
            it != struct_templates_.end())
            return it->second;
        auto dot = qkey.rfind('.');
        if (dot != std::string_view::npos) {
            if (auto it = struct_templates_.find(std::string(qkey.substr(dot + 1)));
                it != struct_templates_.end())
                return it->second;
        }
        return nullptr;
    }
    bool has_struct_template_pkg(std::string_view pkg, std::string_view base) const noexcept {
        if (pkg.empty()) return false;
        std::string qkey;
        qkey.reserve(pkg.size() + 1 + base.size());
        qkey.append(pkg).append(".").append(base);
        return struct_templates_.find(qkey) != struct_templates_.end();
    }
    StrMap<std::vector<const lir::LStructDef*>> struct_specs_;
    StrMap<std::pair<TypeRef, int>> needed_struct_insts_;
    StrSet struct_done_;
    StrMap<const lir::LEnumDef*>   enum_templates_;
    // M2: enum_templates_ is currently bare-keyed only (mono.cpp:192).
    // The single lookup site (mono_clone.cpp:4048) takes a bare base
    // extracted from a mangled cname. Helper wraps the direct .find for
    // call-site symmetry with struct_template helpers; M3 can swap the
    // backing store without touching the call site.
    const lir::LEnumDef*
    find_enum_template_bare(std::string_view base) const noexcept {
        if (auto it = enum_templates_.find(std::string(base));
            it != enum_templates_.end())
            return it->second;
        return nullptr;
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

    struct WorkItem {
        std::string                mangled;
        const lir::LFunction*      tmpl;
        SubstMap                   subst;
        PackMap                    packs;
        int                        depth;
    };
    std::vector<WorkItem> worklist_;

    // L1: lazy struct-method instantiation (default OFF — eager scheme is
    // preserved by clone_struct_def; this infrastructure exists so callers
    // can opt-in via `lazy_methods_`. The full flip is staged across L1.4-L1.6.)
    //
    // Index: base struct/enum name → method name → method template fn (lives
    // in the input struct's sd.methods, heap-stable through unique_ptr).
    StrMap<StrMap<const lir::LFunction*>> struct_method_templates_;
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
    const StrMap<const lir::LFunction*>*
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
    const StrMap<const lir::LFunction*>*
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
        const lir::LFunction*   tmpl;            // resolved template (overload-aware)
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

    // Opt-in reachability filter. Stored on the Mono instance for future
    // use (currently not consulted — the mlir_gen-side binary-skip path
    // is preferred, since mono needs the full LProgram for struct-method
    // instantiation to converge). MonoOpts API kept stable.
    StrSet entry_points_;

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
    TypeRef build_concrete_typeref(const std::string& name) {
        // Struct (incl. ZonedStruct).
        for (auto& sd : out_.structs)
            if (sd.name == name) {
                LogosTypeBuilder st;
                st.kind = sd.is_zoned ? LogosType::Kind::ZonedStruct
                                      : LogosType::Kind::Struct;
                st.struct_name = name;
                st.pkg_name    = sd.pkg;
                return out_.type_pool.alloc(std::move(st));
            }
        // Enum.
        for (auto& ed : out_.enums)
            if (ed.name == name) {
                LogosTypeBuilder et;
                et.kind = LogosType::Kind::Enum;
                et.enum_name = name;
                et.pkg_name  = ed.pkg;
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
            for (auto a : tr.type_args()) if (has_tv(a)) return std::string(tr.enum_name());
            std::string r = std::string(tr.enum_name());
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
    lir::LExprPtr subst_expr(lir_view::ExprRef eref, const SubstMap& s,
                              const PackMap& /*unused*/ = {});
    lir::LStmt    subst_stmt(lir_view::StmtRef sref, const SubstMap& s);

    lir::LBlock subst_block(lir_view::BlockRef bref, const SubstMap& s,
                             const PackMap& /*unused*/ = {}) {
        lir::LBlock nb;
        if (!bref) return nb;
        bref.each_stmt([&](lir_view::StmtRef sref) {
            nb.stmts.push_back(subst_stmt(sref, s));
        });
        return nb;
    }

    lir::LFunction clone_fn(const lir::LFunction& fn, const SubstMap& s,
                             const PackMap& packs = {});

    // Signature-only clone for binary-symbol fast path: copies name/flags
    // and substitutes param/return types, but leaves body empty (no deep
    // body walk). The result is suitable for mlir_gen's forward_declare —
    // mlir_gen skips body emission for binary_symbols fns anyway. Caller
    // must NOT call lir_mirror_emit_function / scan_fn on the result;
    // body.mirror_offset_ stays default, and scan_fn early-returns on that.
    lir::LFunction clone_fn_signature(const lir::LFunction& fn, const SubstMap& s,
                                       const PackMap& packs = {});

    // Auto-trait structural check.  Mirrors sema_auto_trait.cpp::is_auto_trait_satisfied
    // but operates on mono-side state (out_.structs/in_.structs, out_.enums, out_.traits,
    // concrete_impls_).  Used by clone_struct_def's bound gate and (future) auto-trait
    // verification at instantiation time.
    bool is_auto_satisfied(TypeRef tv, std::string_view trait_name, StrSet& visited);

    // L1.4: bound gate, factored out of clone_struct_def for re-use in
    // drain_method_worklist. Returns false when method `m`'s impl_type_params
    // bounds are not satisfied under substitution `s`.
    bool method_bound_ok(const lir::LFunction& m, const SubstMap& s);

    // Recursive trait-satisfaction at mono-time: does `concrete_name`
    // implement `trait_name` directly via concrete_impls_, or transitively
    // via any chain of blanket_impls_? Per-attempt copy of `seen` keeps
    // sibling blanket candidates from poisoning each other (see
    // method_bound_ok's local has_impl, factored here for reuse).
    bool mono_has_impl_recursive(const std::string& trait_name,
                                 const std::string& concrete_name,
                                 StrSet& seen);

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
    lir::LStructDef clone_struct_def(const lir::LStructDef& tmpl,
                                      const SubstMap& s,
                                      const PackMap& packs,
                                      const std::string& new_name);
    lir::LEnumDef   clone_enum_def(const lir::LEnumDef& tmpl,
                                    const SubstMap& s,
                                    const PackMap& packs,
                                    const std::string& new_name);

    // ── Scan (defined in mono_scan.cpp) ───────────────────────────────────
    // Mirror-dispatched: scan_fn looks up the function's body offset in
    // out_.mirror_table and walks through view types from there. This
    // requires lir_mirror_emit_function to have been called for `fn`
    // before scan_fn — see clone+push_back sites in mono.cpp.
    void scan_fn(const lir::LFunction& fn);
    void scan_block(lir_view::BlockRef b);
    void scan_stmt(lir_view::StmtRef s);
    void scan_expr(lir_view::ExprRef e);

    // ── Pattern matching (static — inline) ───────────────────────────────
    static bool match_type(TypeRef c, TypeRef p,
                           SubstMap& bindings) noexcept {
        if (!c || !p) return false;
        if (p.kind() == LogosType::Kind::TypeVar) {
            auto tvn = p.type_var_name();
            auto it = bindings.find(tvn);
            if (it != bindings.end())
                return types_equal(c, TypeRef(it->second));
            bindings[std::string(tvn)] = c;
            return true;
        }
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
        if (p.kind() == LogosType::Kind::TypeVar) {
            auto tvn = p.type_var_name();
            auto it = bindings.find(tvn);
            if (it != bindings.end())
                return types_equal(c, TypeRef(it->second));
            bindings[std::string(tvn)] = c;
            return true;
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
        case LogosType::Kind::Slice:
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

    static int type_specificity(TypeRef tr) noexcept {
        if (!tr || tr.kind() == LogosType::Kind::TypeVar) return 0;
        if (tr.kind() == LogosType::Kind::Ptr)   return 1 + type_specificity(tr.pointee());
        if (tr.kind() == LogosType::Kind::Array)  return 1 + type_specificity(tr.elem());
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
    const lir::LFunction*  find_best_spec(const std::string& base_name,
                                          const std::vector<TypeRef>& type_args);
    const lir::LStructDef* find_best_struct_spec(const std::string& base_name,
                                                  const std::vector<TypeRef>& type_args);

    // ── Enqueue / instantiate (defined in mono_scan.cpp) ─────────────────
    void enqueue_if_needed(const std::string& mangled_callee,
                           const std::vector<TypeRef>& type_args);

    // L1: enqueue a single struct-method instance for lazy codegen. Looks up
    // the method template via `struct_method_templates_`, builds a SubstMap
    // from the concrete struct's type_args, and pushes a MethodWorkItem if
    // the (concrete_struct, method) pair isn't already done. No-op when the
    // method or struct is unknown or already pinned.
    void enqueue_method_inst(TypeRef concrete_struct_t,
                             const std::string& method_name);

    // Drain method_worklist_: clone each pending method under its struct's
    // substitution, rename to "<concrete>__<method>", append to the matching
    // LStructDef in out_.structs (heap-stable through shared_ptr<LFunction>),
    // mirror-emit, and scan for further calls.
    void drain_method_worklist();

    lir::LFunction instantiate_fn(const lir::LFunction& tmpl,
                                   const std::string& mangled_name,
                                   const SubstMap& subst,
                                   const PackMap& packs = {}) {
        ++depth_;
        auto fn = clone_fn(tmpl, subst, packs);
        fn.name = mangled_name;
        // Phase 2: clear type_params on the instance. Without this the
        // cloned instance still claims to be generic, which can confuse
        // downstream passes (mlir-gen treats it as a template, dispatch
        // resolution miscategorises it, etc.). Mirror of the eager
        // blanket-instantiation path which already does this clear at
        // mono.cpp:491,565.
        fn.type_params.clear();
        --depth_;
        return fn;
    }

    // ── Struct/class/enum type collection (inline) ────────────────────────
    void collect_type_for_structs(TypeRef tr) {
        if (!tr) return;
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
