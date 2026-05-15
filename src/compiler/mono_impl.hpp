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
    // Mirror of in_'s L-IR. Stage 3g.1: in_.mirror_table is the canonical
    // home — pre-populated by sema's LirBuilder for LExprs and topped up
    // by lir_mirror_emit_into() in run() for stmts/blocks/patterns.
    int            max_depth_;
    int            depth_ = 0;
    PackMap        cur_packs_;

protected:
    // Resolve an input Pattern* to its mirror PatRef. Returns null PatRef
    // when the mirror has no entry (caller falls back to variant access).
    lir_view::PatRef pat_ref_of(const lir::Pattern& p) const noexcept {
        auto& tbl = *in_.mirror_table;
        auto it = tbl.pat.find(&p);
        if (it == tbl.pat.end()) return {};
        return lir_view::PatRef(out_.type_pool.arena(), it->second);
    }
    lir_view::ExprRef expr_ref_of(const lir::LExpr& e) const noexcept {
        if (e.mirror_offset_ == hermes::arena_offset_t{}) return {};
        return lir_view::ExprRef(out_.type_pool.arena(), e.mirror_offset_);
    }
    lir_view::StmtRef stmt_ref_of(const lir::LStmt& s) const noexcept {
        if (s.mirror_offset_ == hermes::arena_offset_t{}) return {};
        return lir_view::StmtRef(out_.type_pool.arena(), s.mirror_offset_);
    }
    lir_view::BlockRef block_ref_of(const lir::LBlock& b) const noexcept {
        if (b.mirror_offset_ == hermes::arena_offset_t{}) return {};
        return lir_view::BlockRef(out_.type_pool.arena(), b.mirror_offset_);
    }
    lir_view::HermesValRef hv_ref_of(const lir::HermesVal& v) const noexcept {
        if (v.mirror_offset_ == hermes::arena_offset_t{}) return {};
        return lir_view::HermesValRef(out_.type_pool.arena(), v.mirror_offset_);
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
    StrMap<std::vector<const lir::LStructDef*>> struct_specs_;
    StrMap<std::pair<TypeRef, int>> needed_struct_insts_;
    StrSet struct_done_;
    StrMap<const lir::LEnumDef*>   enum_templates_;
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

    // Structural FNV-1a-64 hash of T (mini-Memoria block_type_hash). Layout-
    // stable: ignores struct/field names, recurses into field types using
    // the same SubstMap shape as __field_types_of__. Cycle-guarded via the
    // caller-provided seen set. Defined in mono_clone.cpp.
    uint64_t compute_type_hash(TypeRef t, StrSet& seen) noexcept;

    // Pattern substitution — view-based walk over the input mirror.
    lir::Pattern subst_pattern(const lir::Pattern& pat, const SubstMap& s);
    lir::Pattern subst_pattern(lir_view::PatRef pref, const SubstMap& s);

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

    // ── Record needed instantiations (small — inline) ────────────────────
    void record_needed_struct(TypeRef tr) {
        if (!tr || (tr.kind() != LogosType::Kind::Struct &&
                    tr.kind() != LogosType::Kind::ZonedStruct) ||
            tr.type_args().empty()) return;
        for (auto a : tr.type_args())
            if (TypeRef(a).kind() == LogosType::Kind::TypeVar) return;
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
        for (auto a : tr.type_args())
            if (TypeRef(a).kind() == LogosType::Kind::TypeVar) return;
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
    lir::LExprPtr subst_expr(const lir::LExpr& e, const SubstMap& s,
                              const PackMap& /*unused*/ = {});
    lir::LStmt    subst_stmt(const lir::LStmt& st, const SubstMap& s);

    lir::LBlock subst_block(const lir::LBlock& b, const SubstMap& s,
                             const PackMap& /*unused*/ = {}) {
        lir::LBlock nb;
        for (auto& st : b.stmts)
            nb.stmts.push_back(subst_stmt(st, s));
        return nb;
    }

    lir::LFunction clone_fn(const lir::LFunction& fn, const SubstMap& s,
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
    // LStructDef in out_.structs (heap-stable through unique_ptr<LFunction>),
    // mirror-emit, and scan for further calls.
    void drain_method_worklist();

    lir::LFunction instantiate_fn(const lir::LFunction& tmpl,
                                   const std::string& mangled_name,
                                   const SubstMap& subst,
                                   const PackMap& packs = {}) {
        ++depth_;
        auto fn = clone_fn(tmpl, subst, packs);
        fn.name = mangled_name;
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
