// Logos project — https://github.com/victor-smirnov/logos
//
// SemaChecker class definition — included by all sema_*.cpp translation units.
//
// All method bodies for large methods are defined in:
//   sema.cpp        — run(), primitives, helpers, type subst, resolve_type, lower_program
//   sema_collect.cpp — collect_*, collect_fn
//   sema_expr.cpp   — lower_expr, lower_*_expr, lower_call, lower_method_call, lower_*_lit
//   sema_stmt.cpp   — lower_stmt, lower_let, lower_assign, lower_*, build_pattern
//   sema_decl.cpp   — lower_fn, lower_struct_def, lower_enum_def, lower_*_def

#pragma once

#include <logos/compiler/lir.hpp>
#include <logos/compiler/lir_builder.hpp>
#include <logos/compiler/lir_view.hpp>
#include <logos/compiler/lir_mirror.hpp>
#include <logos/compiler/ast.hpp>
#include <logos/compiler/variance.hpp>
#include <logos/compiler/outlives.hpp>
#include <logos/compiler/subtype.hpp>
#include <logos/compiler/sha256.hpp>
#include <logos/compiler/str_map.hpp>
#include <logos/hermes/view.hpp>
#include <logos/hermes/tiny_object_map.hpp>
#include <logos/hermes/object_array.hpp>
#include <logos/hermes/arena_string.hpp>
#include <logos/hermes/any_val.hpp>

#include <cctype>
#include <cstdint>
#include <cstdio>
#include <format>
#include <functional>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace logos::compiler::ctfe { struct CtfeValue; }

namespace logos::compiler {

// ── Forward declarations of free helpers used in inline class methods ─────
// (Full definitions are in sema.cpp / sema_impl.hpp bottom section.)

// Defined in sema.cpp (non-inline, single definition):
bool types_equal(TypeRef a, TypeRef b) noexcept;
std::string type_str(TypeRef t);

// Diagnostic helper: when two types have the same bare struct/enum name but
// different packages (B-mv-02 / B-mv-09), prepend `pkg.` so the user can see
// which is which.  For all other cases (different bare names, no pkg, etc.)
// falls back to plain type_str.  Returns {expected_str, got_str}.
inline std::pair<std::string, std::string>
type_str_pair(TypeRef expected, TypeRef got) {
    if (!expected || !got) return {type_str(expected), type_str(got)};
    auto need_qual = [&](TypeRef a, TypeRef b) -> bool {
        auto ka = TypeRef(a).kind();
        auto kb = TypeRef(b).kind();
        if (ka != kb) return false;
        if (ka == LogosType::Kind::Struct || ka == LogosType::Kind::ZonedStruct) {
            return TypeRef(a).struct_name() == TypeRef(b).struct_name() &&
                   TypeRef(a).pkg_name()    != TypeRef(b).pkg_name();
        }
        if (ka == LogosType::Kind::Enum) {
            return TypeRef(a).enum_name() == TypeRef(b).enum_name() &&
                   TypeRef(a).pkg_name()  != TypeRef(b).pkg_name();
        }
        return false;
    };
    if (!need_qual(expected, got)) return {type_str(expected), type_str(got)};
    auto qualify = [](TypeRef t) {
        std::string r;
        auto pkg = TypeRef(t).pkg_name();
        if (!pkg.empty()) { r.append(pkg); r += '.'; }
        r += type_str(t);
        return r;
    };
    return {qualify(expected), qualify(got)};
}
std::string concrete_struct_name(TypeRef t);
bool types_compatible(TypeRef from, TypeRef to) noexcept;

// Inline (defined below, after class, so visible in all TUs):
inline bool is_integer_kind(LogosType::Kind k) noexcept;
inline int64_t parse_int_literal(std::string_view sv) noexcept;
inline std::optional<int64_t> get_intlit_value(lir_view::ExprRef e) noexcept;
inline bool intlit_fits(int64_t v, LogosType::Kind k) noexcept;
inline bool can_widen_int(LogosType::Kind from, LogosType::Kind to) noexcept;

// Aliases used throughout SemaChecker method implementations.
// Placed here so all sema_*.cpp files get them automatically.
namespace sema_detail {
    namespace la = logos::compiler::ast;
    using hermes::TinyMapView;
    using hermes::ArrayView;
    using hermes::StringView;
    using hermes::AnyVal;
    using hermes::MemHolder;
}

// M5 step 3b: snapshot of all collect()-mutated symbol tables + the
// "already collected" holder set. Held by SemaCacheImpl across the
// 5+ sema_lower invocations per compile session. Defined in
// sema_impl.hpp where SemaChecker's private nested types are visible;
// friended so the fields below can spell `SemaChecker::SemaStructInfo`
// etc. directly. PIMPL-style: SemaChecker exposes only take/install
// methods, never the field types.
class SemaCheckerSnapshot;

class SemaChecker {
public:
    friend class SemaCheckerSnapshot;

    lir::LProgram run(const std::vector<hermes::Hermes>& asts,
                      const std::vector<std::string>& filenames,
                      const std::vector<bool>& from_binary = {});

    // M5: move-out all collect-mutated tables + the cached holder set
    // into an opaque snapshot. Called at the end of run() when cache is
    // bound; the cache stores the result and feeds it back via
    // install_snapshot on the next sema_lower invocation.
    std::unique_ptr<SemaCheckerSnapshot> take_snapshot();
    // M5: move-in the snapshot — replaces this checker's symbol tables
    // wholesale. Called at start of run() when cache is bound and
    // already has a snapshot from a prior invocation.
    void install_snapshot(std::unique_ptr<SemaCheckerSnapshot> snap);

    void set_metaprog_options(bool mode, size_t entry_ast_idx) {
        metaprog_mode_ = mode;
        metaprog_entry_ast_idx_ = entry_ast_idx;
    }
    // Skeleton-skip gate: names already compiled into a linked archive's .o.
    // A from_binary fn whose symbol is here has its body in that .o, so sema
    // skips lowering it and the linker resolves the symbol (codegen
    // forward-declares it — same predicate as mlir_gen's is_binary_skip).
    void set_binary_symbols(const logos::compiler::StrSet* s) { binary_symbols_ = s; }
    void set_metaprog_keep_fns(std::vector<std::string> names) {
        metaprog_keep_fns_ = std::move(names);
    }
    // M5: bind to a cache shared across multiple sema_lower invocations.
    // When set, run() seeds prog.type_pool from cache->shared_pool() instead
    // of allocating a fresh pool, so cached TypeRefs (Step 3+) stay valid.
    void set_cache(SemaCache* c) { cache_ = c; }
    // M6.1: when > 0, collect() and lower_program() skip asts[0..idx).
    // Their state is expected to be in the cache (which must be in
    // metaprog_delta mode — see SemaCache).
    void set_delta_start_idx(size_t idx) { delta_start_idx_ = idx; }
    // Phase 6 (multi-arena IR) item-level lazy: per-AST flag. Lifetime-
    // borrowed; caller (sema_lower) keeps the vector alive across run().
    // Null or empty → all asts treated as non-lazy (back-compat).
    void set_is_lazy(const std::vector<bool>* v) { is_lazy_ = v; }
    bool fn_is_metaprog_keep(std::string_view name) const {
        // metacall_sites store the raw callee token (bare base name);
        // compare against the bare form of `name` (which may carry
        // `pkg$base__f__sig` mangling at this point).
        auto base = bare_fn_name(name);
        for (const auto& n : metaprog_keep_fns_) if (n == base) return true;
        return false;
    }

private:
    // ── Type pool and primitives ─────────────────────────────────

    // Bound to the TypePool living inside the LProgram constructed in run()
    // so eager mirror emits, type interning, and downstream stages share a
    // single arena identity (no late std::move out of the SemaChecker).
    TypePool* pool_ = nullptr;

    // prims_[int(Kind)] for primitive kinds.  TypeVar is not a primitive.
    // Size = int(Kind::Error) + 1 to cover all Kind values.
    std::array<TypeRef, int(LogosType::Kind::Error) + 1> prims_{};

    void init_primitives();

    TypeRef prim(LogosType::Kind k)  { return prims_[int(k)]; }
    TypeRef void_t()    { return prim(LogosType::Kind::Void); }
    TypeRef i32_t()     { return prim(LogosType::Kind::I32); }
    TypeRef bool_t()    { return prim(LogosType::Kind::Bool); }
    TypeRef u8_t()      { return prim(LogosType::Kind::U8); }
    TypeRef usize_t()   { return prim(LogosType::Kind::Usize); }
    TypeRef isize_t()   { return prim(LogosType::Kind::Isize); }
    TypeRef intlit_t()  { return prim(LogosType::Kind::IntLit); }
    TypeRef error_t()   { return prim(LogosType::Kind::Error); }

    // Single point of truth for the target's pointer size. Logos ships
    // 64-bit only today; if we ever target 32-bit, change this constant
    // and every Usize/Isize-sensitive site downstream will pick it up via
    // `target_pointer_bits()` rather than hardcoded U64/I64.
    static constexpr int target_pointer_bits() { return 64; }
    LogosType::Kind usize_underlying() const {
        return target_pointer_bits() == 32 ? LogosType::Kind::U32
                                            : LogosType::Kind::U64;
    }
    LogosType::Kind isize_underlying() const {
        return target_pointer_bits() == 32 ? LogosType::Kind::I32
                                            : LogosType::Kind::I64;
    }

    TypeRef make_ptr(bool mut, TypeRef pointee) {
        LogosTypeBuilder t; t.kind = LogosType::Kind::Ptr;
        t.mut_ptr = mut; t.pointee = pointee;
        return pool_->alloc(t);
    }
    TypeRef make_ref(bool mut, TypeRef pointee, std::string lifetime = "") {
        // Phase 1B-11 canonicalisation: `&UnsizedSlice<T>` ↔ `Slice<T>`
        // and `&UnsizedDyn<Trait>` ↔ `TraitObject<Trait>` — done at
        // resolve_type (sema.cpp:3098) for explicit `&Type` syntax;
        // also done here so implicit `&self` for impl-on-str (Self =
        // UnsizedSlice<u8>) and impl-on-dyn collapse to the canonical
        // fat-ptr kind. Without this, param0 mangling diverges between
        // `&self` and `other: &Self` for the same impl (CP-cm-08b).
        if (pointee && pointee.kind() == LogosType::Kind::UnsizedSlice)
            return make_slice_type(pointee.elem());
        if (pointee && pointee.kind() == LogosType::Kind::UnsizedDyn) {
            std::vector<TypeRef> args_vec = pointee.type_args();
            return make_trait_object(pointee.trait_name(), std::move(args_vec));
        }
        LogosTypeBuilder t;
        t.kind = mut ? LogosType::Kind::MutRef : LogosType::Kind::Ref;
        t.pointee = pointee;
        t.lifetime = std::move(lifetime);
        return pool_->alloc(std::move(t));
    }
    TypeRef make_array(TypeRef elem, uint64_t n, std::string_view symbolic = "") {
        LogosTypeBuilder t; t.kind = LogosType::Kind::Array;
        t.elem = elem; t.arr_size = n;
        t.arr_size_var = std::string(symbolic);
        return pool_->alloc(std::move(t));
    }
    // Resolve a struct/datatype/enum's home package by name. Used by the
    // helpers below to auto-fill pkg_name when the caller doesn't supply it,
    // so types built away from a find_*_by_name call site still hash with
    // package identity in compute_type_uid.
    std::string resolve_struct_pkg_(std::string_view name) {
        auto [spkg, si] = find_struct_by_name(name);
        if (si) return spkg;
        auto [dpkg, di] = find_datatype_by_name(name);
        if (di) return dpkg;
        return {};
    }
    std::string resolve_enum_pkg_(std::string_view name) {
        auto [epkg, ei] = find_enum_by_name(name);
        if (ei) return epkg;
        return {};
    }
    TypeRef make_struct_type(std::string_view name, std::string_view pkg = {}) {
        LogosTypeBuilder t; t.kind = LogosType::Kind::Struct; t.struct_name = name;
        if (!pkg.empty()) t.pkg_name = std::string(pkg);
        else if (auto rp = resolve_struct_pkg_(name); !rp.empty()) t.pkg_name = std::move(rp);
        return pool_->alloc(std::move(t));
    }
    TypeRef make_datatype_type(std::string_view name, std::string_view pkg = {}) {
        LogosTypeBuilder t; t.kind = LogosType::Kind::ZonedStruct; t.struct_name = std::string(name);
        if (!pkg.empty()) t.pkg_name = std::string(pkg);
        else if (auto rp = resolve_struct_pkg_(name); !rp.empty()) t.pkg_name = std::move(rp);
        return pool_->alloc(std::move(t));
    }
    TypeRef make_generic_datatype(std::string_view name,
                                   std::vector<TypeRef> args,
                                   std::vector<std::string> lt_args = {},
                                   std::string_view pkg = {}) {
        LogosTypeBuilder t; t.kind = LogosType::Kind::ZonedStruct;
        t.struct_name   = std::string(name);
        t.type_args     = std::move(args);
        t.lifetime_args = std::move(lt_args);
        if (!pkg.empty()) t.pkg_name = std::string(pkg);
        else if (auto rp = resolve_struct_pkg_(name); !rp.empty()) t.pkg_name = std::move(rp);
        return pool_->alloc(std::move(t));
    }
    TypeRef make_generic_struct(std::string_view name,
                                 std::vector<TypeRef> args,
                                 std::vector<std::string> lt_args = {},
                                 std::string_view pkg = {}) {
        LogosTypeBuilder t; t.kind = LogosType::Kind::Struct;
        t.struct_name   = std::string(name);
        t.type_args     = std::move(args);
        t.lifetime_args = std::move(lt_args);
        if (!pkg.empty()) t.pkg_name = std::string(pkg);
        else if (auto rp = resolve_struct_pkg_(name); !rp.empty()) t.pkg_name = std::move(rp);
        return pool_->alloc(std::move(t));
    }
    TypeRef make_enum_type(std::string_view name, std::string_view pkg = {}) {
        LogosTypeBuilder t; t.kind = LogosType::Kind::Enum; t.enum_name = name;
        if (!pkg.empty()) t.pkg_name = std::string(pkg);
        else if (auto rp = resolve_enum_pkg_(name); !rp.empty()) t.pkg_name = std::move(rp);
        return pool_->alloc(std::move(t));
    }
    TypeRef make_generic_enum(std::string_view name,
                               std::vector<TypeRef> args,
                               std::vector<std::string> lt_args = {},
                               std::string_view pkg = {}) {
        LogosTypeBuilder t; t.kind = LogosType::Kind::Enum;
        t.enum_name = std::string(name);
        t.type_args = std::move(args);
        t.lifetime_args = std::move(lt_args);
        if (!pkg.empty()) t.pkg_name = std::string(pkg);
        else if (auto rp = resolve_enum_pkg_(name); !rp.empty()) t.pkg_name = std::move(rp);
        return pool_->alloc(std::move(t));
    }
    TypeRef make_tuple_type(std::vector<TypeRef> elems) {
        LogosTypeBuilder t; t.kind = LogosType::Kind::Tuple;
        t.tuple_elems = std::move(elems);
        return pool_->alloc(std::move(t));
    }
    TypeRef make_closure_type(std::vector<TypeRef> params, TypeRef ret) {
        LogosTypeBuilder t; t.kind = LogosType::Kind::Closure;
        t.closure_params = std::move(params);
        t.closure_ret = ret;
        return pool_->alloc(std::move(t));
    }
    TypeRef make_fn_ptr_type(std::vector<TypeRef> params, TypeRef ret) {
        LogosTypeBuilder t; t.kind = LogosType::Kind::FnPtr;
        t.closure_params = std::move(params);
        t.closure_ret = ret ? ret : void_t();
        return pool_->alloc(std::move(t));
    }
    // Phase 1B-12: unsize coercion `&[T; N]` / `&mut [T; N]` / `*const [T; N]`
    // / `*mut [T; N]` → `&[T]` (Kind::Slice). Builds the fat-pointer payload
    // from the array reference's data ptr and the compile-time-known length N.
    // Returns true if coercion was applied.
    bool try_coerce_array_ref_to_slice(lir::LExprPtr& arg, TypeRef expected) {
        if (!arg || !expected) return false;
        TypeRef et(expected);
        if (et.kind() != LogosType::Kind::Slice) return false;
        TypeRef at(arg->type);
        if (!at) return false;
        bool is_ref_or_ptr =
            at.kind() == LogosType::Kind::Ref ||
            at.kind() == LogosType::Kind::MutRef ||
            at.kind() == LogosType::Kind::Ptr;
        if (!is_ref_or_ptr) return false;
        TypeRef pointee = at.pointee();
        if (!pointee || pointee.kind() != LogosType::Kind::Array) return false;
        if (!types_compatible(pointee.elem(), et.elem())) return false;
        int64_t n = static_cast<int64_t>(pointee.arr_size());
        // Build a slice_lit using the ref/ptr value as the data ptr and N as
        // the length. arg holds the address expression; reuse it directly.
        auto len = builder().lit_int(n, prim(LogosType::Kind::I64));
        arg = builder().slice_lit(std::move(arg), std::move(len),
                                  make_slice_type(et.elem()));
        return true;
    }
    // Coerce a non-capturing closure to fn ptr when target type is FnPtr.
    // Returns true if coercion was applied (arg's type is changed to FnPtr).
    bool try_coerce_closure_to_fnptr(lir::LExprPtr& arg, TypeRef expected) {
        TypeRef er(expected);
        if (!arg || !er || er.kind() != LogosType::Kind::FnPtr) return false;
        TypeRef at(arg->type);
        if (!at || at.kind() != LogosType::Kind::Closure) return false;
        auto xref = expr_ref_of(*arg);
        if (!xref || xref.kind() != lir_schema::expr::Code::ClosureBox) return false;
        lir_view::EClosureBoxView box{xref};
        if (box.capture_count() != 0) return false;
        if (at.closure_params().size() != er.closure_params().size()) return false;
        for (size_t i = 0; i < at.closure_params().size(); ++i)
            if (!types_compatible(at.closure_params()[i], er.closure_params()[i])) return false;
        TypeRef fp_ty = make_fn_ptr_type(at.closure_params(), at.closure_ret());
        arg = builder().closure_to_fnptr(arg, fp_ty);
        return true;
    }
    TypeRef make_slice_type(TypeRef elem) {
        LogosTypeBuilder t; t.kind = LogosType::Kind::Slice;
        t.elem = elem;
        return pool_->alloc(std::move(t));
    }
    // Phase 1B: bare `[T]` — the unsized form, distinct from `&[T]` (Slice).
    // Cannot appear as a value type; only behind a reference (where it
    // canonicalises back to Kind::Slice) or as a `T: ?Sized` substitution.
    TypeRef make_unsized_slice_type(TypeRef elem) {
        LogosTypeBuilder t; t.kind = LogosType::Kind::UnsizedSlice;
        t.elem = elem;
        return pool_->alloc(std::move(t));
    }
    // Phase 1B-4: bare `dyn Trait` — the unsized trait-object form. Mirror
    // of make_unsized_slice_type for dyn. Args may be empty.
    TypeRef make_unsized_dyn_type(std::string_view tname,
                                  std::vector<TypeRef> args = {}) {
        LogosTypeBuilder t; t.kind = LogosType::Kind::UnsizedDyn;
        t.trait_name = std::string(tname);
        t.type_args = std::move(args);
        return pool_->alloc(std::move(t));
    }
    // Phase 1B-14: `&DstStruct` / `&mut DstStruct` / `*const DstStruct` —
    // fat pointer to a custom-DST struct. Stored as {data_ptr, tail_len}
    // and passed by pointer (same ABI as Kind::Slice). is_mut differentiates
    // `&` vs `&mut`/`*mut` for borrow-checker purposes.
    TypeRef make_dst_ref(std::string_view struct_name,
                         std::string_view pkg_name,
                         bool is_mut,
                         std::vector<TypeRef> type_args = {}) {
        LogosTypeBuilder t; t.kind = LogosType::Kind::DstRef;
        t.struct_name = std::string(struct_name);
        t.pkg_name = std::string(pkg_name);
        t.mut_ptr = is_mut;
        t.type_args = std::move(type_args);
        return pool_->alloc(std::move(t));
    }
    // Phase 1B-15: recursive sema-side ABI byte-size computation. Mirrors
    // mlir_gen_types.cpp::logos_abi_byte_size but operates over SemaStructInfo /
    // SemaEnumInfo / SemaDatatypeInfo. Used by tail-field-access codegen
    // (Phase 1B-14) to compute the prefix offset for ANY field-shape mix,
    // not just primitives. Returns 8 for unknown/unresolvable types (a
    // conservative pointer-size guess; later layout passes catch real
    // mismatches). Cycle-guarded via `seen` set keyed by struct name.
    uint64_t sema_abi_byte_size(TypeRef t,
                                logos::compiler::StrSet& seen);
    // Phase 2-1: cfg!() compile-time predicate evaluation. Walks the
    // ARGS items of a FN_MACRO_CALL with callee="cfg" and returns the
    // boolean truth value. Recursive on `all(...)`/`any(...)`/`not(...)`
    // combinators. Built-in keys (target_pointer_width, target_arch,
    // target_os, target_endian, target_family) resolved against
    // compile-target metadata. `feature = "name"` resolved against
    // `cfg_features_` (populated from --cfg flags / lforge manifest).
    bool evaluate_cfg_predicate(hermes::TinyMapView fn_macro_call_node);
    // The single ARG of the cfg!() call is a predicate AST node — could
    // be a parenthesised IDENT (`target_os`), a key=value (e.g.
    // `target_os = "linux"`), or a combinator call (`all(...)`).
    // This helper interprets one predicate node.
    bool evaluate_cfg_node(hermes::TinyMapView pred_node);
    // Phase 2-2: evaluate `#[cfg(...)]` attached to an item. Annotation
    // args parsed by the annotation grammar (ANNOT_KV / bare IDENT only
    // for MVP — combinators like `all(...)` not supported in attribute
    // form yet). Multi-arg list treats as conjunction.
    bool evaluate_cfg_annotation(hermes::TinyMapView annotation_node);
    // Phase 2-3: predicate match against the active cfg-key set + features.
    // Lightweight wrappers around the file-static match_cfg_key_value /
    // match_cfg_flag so sema_collect's cfg_attr handling can call them
    // without exposing the file-static functions directly.
    bool match_cfg_predicate_kv(std::string_view key, std::string_view val);
    bool match_cfg_predicate_flag(std::string_view name);

    // Phase 2-1: feature flag set populated from `--cfg feature=foo`
    // CLI args. Initialised in SemaChecker ctor / main flag parsing.
    logos::compiler::StrSet cfg_features_;
    // Public mutator for cfg features (called by sema_lower entry).
public:
    void add_cfg_feature(const std::string& name) { cfg_features_.insert(name); }
    // Three-layer split Phase 3.4: set the implicit-prelude package name
    // before collect() runs. Empty means "no implicit prelude" (legacy).
    // build_import_scope appends this to wildcard_packages for every
    // NON-binary AST that doesn't carry `#![no_implicit_prelude]`.
    void set_implicit_prelude(std::string p) { implicit_prelude_ = std::move(p); }
private:
    std::string implicit_prelude_;

    // Phase 1B-15: returns true when the struct type `t` is custom-DST
    // either directly (template flagged is_dst at decl time) or after
    // generic instantiation (template's `?Sized` last-field TypeVar
    // bound to an unsized type via t.type_args()). Used for `&S` →
    // DstRef canonicalisation at REF/PTR resolve time and for the
    // dst_from_raw_parts intrinsic check.
    bool is_effective_dst(TypeRef t);
    TypeRef make_trait_object(std::string_view tname,
                              std::vector<TypeRef> args = {}) {
        LogosTypeBuilder t; t.kind = LogosType::Kind::TraitObject;
        t.trait_name = std::string(tname);
        t.type_args = std::move(args);
        return pool_->alloc(std::move(t));
    }
    TypeRef make_typevar(std::string_view name) {
        LogosTypeBuilder t; t.kind = LogosType::Kind::TypeVar;
        t.type_var_name = std::string(name);
        return pool_->alloc(t);
    }

    TypeRef lookup_type_by_name(std::string_view name);

    lir::LExprPtr error_expr() {
        return builder().lit_int(0, error_t());
    }

    // Stage 3f L-IR builder. Bound to cur_prog_ when sema is lowering a
    // module. ADR 0005: variant write-path verbatim today; Stage 3g
    // re-implementation flips this to direct Hermes-zone emission with no
    // caller change.
    LirBuilder builder() {
        return LirBuilder(*cur_prog_);
    }

    // ── Mirror ref accessors (read-only view of just-built L-IR nodes) ──
    // Every LirBuilder-constructed node has mirror_offset_ set, so these
    // never return a null Ref unless `e` was built outside the builder.
    lir_view::ExprRef expr_ref_of(const lir::LExpr& e) const noexcept {
        if (e.mirror_offset_ == hermes::arena_offset_t{}) return {};
        return lir_view::ExprRef(cur_prog_->type_pool.arena(), e.mirror_offset_);
    }
    lir_view::StmtRef stmt_ref_of(const lir::LStmt& s) const noexcept {
        if (s.mirror_offset_ == hermes::arena_offset_t{}) return {};
        return lir_view::StmtRef(cur_prog_->type_pool.arena(), s.mirror_offset_);
    }
    lir_view::PatRef pat_ref_of(const lir::Pattern& p) const noexcept {
        if (p.mirror_offset_ == hermes::arena_offset_t{}) return {};
        return lir_view::PatRef(cur_prog_->type_pool.arena(), p.mirror_offset_);
    }
    // Reverse lookup ExprRef → owning LExpr* via the program's mirror table.
    // Returns nullptr if the ref is null or its offset isn't in the table
    // (e.g. mono-cloned exprs not registered in cur_prog_).
    lir::LExpr* lexpr_of(lir_view::ExprRef r) const noexcept {
        if (!r || !cur_prog_->mirror_table) return nullptr;
        auto it = cur_prog_->mirror_table->expr_by_offset.find(uint32_t(r.offset()));
        if (it == cur_prog_->mirror_table->expr_by_offset.end()) return nullptr;
        return const_cast<lir::LExpr*>(it->second);
    }

    template<typename K>
    lir::LStmt make_stmt_emit(uint32_t line, K&& k) {
        lir::LStmt s; s.line = line;
        if (cur_prog_) {
            using KT = std::decay_t<K>;
            auto& p = *cur_prog_;
            if constexpr (std::is_same_v<KT, lir::SLet>) {
                s.mirror_offset_ = lir_mirror_emit_let(p, line, k.name, k.type, k.value, k.is_mut);
            } else if constexpr (std::is_same_v<KT, lir::SAssign>) {
                s.mirror_offset_ = lir_mirror_emit_assign(p, line, k.name, k.value);
            } else if constexpr (std::is_same_v<KT, lir::SReturn>) {
                s.mirror_offset_ = lir_mirror_emit_return(p, line, k.value);
            } else if constexpr (std::is_same_v<KT, lir::SIf>) {
                const lir::LBlock* eb = k.else_.has_value() ? *k.else_ : nullptr;
                s.mirror_offset_ = lir_mirror_emit_if_stmt(p, line, k.cond, k.then_, eb);
            } else if constexpr (std::is_same_v<KT, lir::SWhile>) {
                s.mirror_offset_ = lir_mirror_emit_while(p, line, k.cond, k.body, k.label);
            } else if constexpr (std::is_same_v<KT, lir::SFor>) {
                s.mirror_offset_ = lir_mirror_emit_for(p, line, k.var, k.lo, k.hi, k.inclusive, k.body, k.label);
            } else if constexpr (std::is_same_v<KT, lir::SLoop>) {
                s.mirror_offset_ = lir_mirror_emit_loop(p, line, k.body, k.label, k.break_slot, k.result_type);
            } else if constexpr (std::is_same_v<KT, lir::SBreak>) {
                s.mirror_offset_ = lir_mirror_emit_break(p, line, k.value, k.label);
            } else if constexpr (std::is_same_v<KT, lir::SContinue>) {
                s.mirror_offset_ = lir_mirror_emit_continue(p, line, k.label);
            } else if constexpr (std::is_same_v<KT, lir::SBlock>) {
                s.mirror_offset_ = lir_mirror_emit_block_stmt(p, line, k.body);
            } else if constexpr (std::is_same_v<KT, lir::SFieldWrite>) {
                s.mirror_offset_ = lir_mirror_emit_field_write(p, line, k.receiver, k.field, k.value);
            } else if constexpr (std::is_same_v<KT, lir::SIndexWrite>) {
                s.mirror_offset_ = lir_mirror_emit_index_write(p, line, k.arr, k.index, k.value);
            } else if constexpr (std::is_same_v<KT, lir::SFieldIndexWrite>) {
                s.mirror_offset_ = lir_mirror_emit_field_index_write(p, line, k.receiver, k.field, k.index, k.value);
            } else if constexpr (std::is_same_v<KT, lir::SExprStmt>) {
                s.mirror_offset_ = lir_mirror_emit_expr_stmt(p, line, k.expr);
            } else if constexpr (std::is_same_v<KT, lir::SMatch>) {
                s.mirror_offset_ = lir_mirror_emit_match_stmt(p, line, k.scrut, k.arms);
            } else if constexpr (std::is_same_v<KT, lir::SDelete>) {
                s.mirror_offset_ = lir_mirror_emit_delete(p, line, k.expr);
            } else if constexpr (std::is_same_v<KT, lir::SForEach>) {
                s.mirror_offset_ = lir_mirror_emit_for_each(p, line, k.var, k.iter, k.elem_type, k.arr_size, k.is_slice, k.body);
            } else if constexpr (std::is_same_v<KT, lir::SDerefWrite>) {
                s.mirror_offset_ = lir_mirror_emit_deref_write(p, line, k.ptr, k.value);
            } else if constexpr (std::is_same_v<KT, lir::SDrop>) {
                s.mirror_offset_ = lir_mirror_emit_drop(p, line, k.var_name, k.drop_fn, k.type, k.drop_fields, k.moved_fields);
            } else if constexpr (std::is_same_v<KT, lir::SDerefFieldWrite>) {
                s.mirror_offset_ = lir_mirror_emit_deref_field_write(p, line, k.receiver, k.type_name, k.field, k.value);
            } else if constexpr (std::is_same_v<KT, lir::STupleWrite>) {
                s.mirror_offset_ = lir_mirror_emit_tuple_write(p, line, k.receiver, k.index, k.value, k.recv_type);
            } else if constexpr (std::is_same_v<KT, lir::SLetElse>) {
                s.mirror_offset_ = lir_mirror_emit_let_else(p, line, k.pat, k.scrut, k.else_block);
            } else if constexpr (std::is_same_v<KT, lir::SChainFieldWrite>) {
                s.mirror_offset_ = lir_mirror_emit_chain_field_write(p, line, k.receiver, k.mid_field, k.extras, k.field, k.value);
            } else {
                static_assert(sizeof(K) == 0, "make_stmt_emit: unknown stmt kind");
            }
        }
        (void)k;  // payload swallowed — only mirror_offset_ matters now
        return s;
    }

    template<typename K>
    lir::HermesValPtr alloc_hv_emit(K&& k) {
        using KT = std::decay_t<K>;
        auto& p = *cur_prog_;
        ::logos::hermes::arena_offset_t mo;
        if constexpr (std::is_same_v<KT, lir::HVNull>) {
            mo = lir_mirror_emit_hv_null(p);
        } else if constexpr (std::is_same_v<KT, lir::HVBool>) {
            mo = lir_mirror_emit_hv_bool(p, k.value);
        } else if constexpr (std::is_same_v<KT, lir::HVInt>) {
            mo = lir_mirror_emit_hv_int(p, k.value);
        } else if constexpr (std::is_same_v<KT, lir::HVFloat>) {
            mo = lir_mirror_emit_hv_float(p, k.value);
        } else if constexpr (std::is_same_v<KT, lir::HVStr>) {
            mo = lir_mirror_emit_hv_str(p, k.value);
        } else if constexpr (std::is_same_v<KT, lir::HVMap>) {
            mo = lir_mirror_emit_hv_map(p, k.entries, k.key_type);
        } else if constexpr (std::is_same_v<KT, lir::HVArray>) {
            mo = lir_mirror_emit_hv_array(p, k.elements, k.elem_type);
        } else if constexpr (std::is_same_v<KT, lir::HVCapture>) {
            mo = lir_mirror_emit_hv_capture(p, k.param_index, k.value_index);
        } else if constexpr (std::is_same_v<KT, lir::HVType>) {
            mo = lir_mirror_emit_hv_type(p, k.kind, k.uid, k.name);
        } else {
            static_assert(sizeof(K) == 0, "alloc_hv_emit: unknown HermesVal payload");
        }
        (void)k;  // payload consumed by per-kind dispatch above
        auto* hv = lir::alloc_hermes_val(*cur_prog_);
        hv->mirror_offset_ = mo;
        return hv;
    }

    // ── File / line tracking ─────────────────────────────────────

    const std::vector<std::string>* filenames_    = nullptr;
    const std::vector<bool>*        from_binary_  = nullptr;
    // Phase 6 (multi-arena IR) item-level lazy. Parallel to from_binary_;
    // per-AST flag indicating the source archive shipped only parsed AST
    // (no .o, no LIR blob — see ModuleManifest::lazy). Stamped onto
    // LFunction.from_lazy_module; consumed by post-mono reach analysis to
    // skip mlir-gen for lazy fns not reached from any non-lazy caller.
    const std::vector<bool>*        is_lazy_      = nullptr;
    std::string  file_;
    std::string  cur_package_;
    lir::LProgram* cur_prog_ = nullptr;  // set during lower_module_items, used by lower_generic_call
    bool         cur_from_binary_ = false;   // current file is from a binary module
    bool         cur_from_lazy_   = false;   // current file is from a lazy archive
    uint32_t     node_line_ = 0;

    // Per-file import scope (wildcard: `use foo.bar;` makes all pub symbols of foo.bar visible)
    struct ImportScope {
        std::vector<std::string> wildcard_packages;
        // CP-cm-02: `use pkg.Path.Type.{V1, V2, …};` brings enum variants
        // into bare scope. Map keyed by the bare variant name → the dotted
        // enum-type qualifier so lookup paths can resolve `V1` as if it
        // were written `Type::V1`. Last-write-wins on name collision.
        std::unordered_map<std::string, std::string> variant_aliases;
    };
    ImportScope cur_imports_;

    // Qualified key: "pkg::name" or "name" if pkg empty
    static std::string sema_key(std::string_view pkg, std::string_view name) {
        if (pkg.empty()) return std::string(name);
        std::string r;
        r.reserve(pkg.size() + 2 + name.size());
        r.append(pkg); r.append("::"); r.append(name);
        return r;
    }

    uint32_t     tmp_var_count_ = 0;   // for generating unique internal names

    uint32_t get_line(hermes::TinyMapView node) noexcept {
        using namespace sema_detail;
        if (node.is_null()) return 0;
        AnyVal av = node.get(la::SRC_LINE.code);
        if (av.is_null() || !av.is_value()) return 0;
        return av.as_value<uint32_t>();
    }

    // ── meta @{} helpers ─────────────────────────────────────────


    // ── Hermes helpers ───────────────────────────────────────────

    hermes::MemHolder* holder_ = nullptr;

    // Slice 7 of metaprog-quote: lower_hermes_blob may build a Hermes doc
    // from blob bytes (when the blob carries an AST fragment) and recurse
    // into lower_expr while pointing holder_ at the new doc's holder.
    // The Hermes objects must outlive the recursion AND any LIR mirror
    // back-fill that runs later — keep them alive for the SemaChecker's
    // lifetime by stashing here.
    std::vector<hermes::Hermes> blob_docs_;

    int32_t code_of(hermes::TinyMapView node) noexcept {
        using namespace sema_detail;
        if (node.is_null()) return -1;
        AnyVal av = node.get(la::CODE.code);
        return av.is_null() ? -1 : av.as_value<int32_t>();
    }

    std::string_view str_of(hermes::AnyVal av) noexcept {
        using namespace sema_detail;
        if (av.is_null()) return {};
        return StringView(av.to_offset(), holder_).view();
    }

    hermes::TinyMapView map_of(hermes::AnyVal av) noexcept {
        using namespace sema_detail;
        if (av.is_null()) return TinyMapView{};
        return TinyMapView(av.to_offset(), holder_);
    }

    hermes::ArrayView arr_of(hermes::AnyVal av) noexcept {
        using namespace sema_detail;
        return ArrayView(av.to_offset(), holder_);
    }

    // ── Call/method argument parsing ──────────────────────────────
    // A call/method-call node's ARGS appears in one of three shapes, which
    // were re-parsed inline at every dispatch site in lower_method_call /
    // lower_call / lower_generic_call:
    //   - absent / null            → no args
    //   - turbofish form `{ITEMS:[…]}` (a map carrying the arg list under
    //     ITEMS, emitted by the turbofish-bearing CALL/METHOD_CALL alt)
    //   - legacy flat ObjectArray  → the arg nodes directly
    // collect_arg_asts returns the arg AST nodes in source order; callers
    // apply their own lowering policy (lower_expr vs lower_arg_with_hint).
    // The shape probe order (ITEMS first, else pointer-array) matches the
    // historical inline logic exactly.
    std::vector<hermes::TinyMapView> collect_arg_asts(hermes::TinyMapView node) noexcept {
        using namespace sema_detail;
        std::vector<hermes::TinyMapView> out;
        if (!node.has_key(la::ARGS)) return out;
        auto args_av = node.get(la::ARGS.code);
        if (args_av.is_null()) return out;
        auto args_list = map_of(args_av);
        if (args_list.has_key(la::ITEMS)) {
            auto items = arr_of(args_list.get(la::ITEMS.code));
            for (uint64_t i = 0; i < items.size(); ++i)
                out.push_back(map_of(items.get(i)));
        } else if (args_av.is_pointer()) {
            auto arr = arr_of(args_av);
            for (uint64_t i = 0; i < arr.size(); ++i)
                out.push_back(map_of(arr.get(i)));
        }
        return out;
    }

    // collect_arg_asts + lower each via lower_expr. Replaces the common
    // "parse the args and lower them" idiom (no per-arg type hint).
    std::vector<lir::LExprPtr> lower_call_args(hermes::TinyMapView node) {
        std::vector<lir::LExprPtr> args;
        for (auto an : collect_arg_asts(node)) args.push_back(lower_expr(an));
        return args;
    }

    // ── Diagnostics ──────────────────────────────────────────────

    SemaResult result_;
    std::string ctx_;
    int         closure_counter_ = 0;

    // ── Stdlib-intrinsic type predicates (catalog-cleanup Step 5) ──────
    // Replaces ad-hoc `t.struct_name() == "X"` patterns scattered across
    // sema.  Centralising helps when the stdlib renames a type or when
    // pkg-qualification gets stricter (today these checks are pkg-blind,
    // matching any struct with the bare name; that's intentional for
    // stdlib intrinsics that have a single canonical type).
    static bool is_named_struct(TypeRef t, std::string_view name) {
        if (!t) return false;
        auto k = TypeRef(t).kind();
        return (k == LogosType::Kind::Struct ||
                k == LogosType::Kind::ZonedStruct) &&
               TypeRef(t).struct_name() == name;
    }
    static bool is_named_enum(TypeRef t, std::string_view name) {
        if (!t) return false;
        return TypeRef(t).kind() == LogosType::Kind::Enum &&
               TypeRef(t).enum_name() == name;
    }
    static bool is_anyval(TypeRef t)         { return is_named_struct(t, "AnyVal"); }
    static bool is_hermes_static(TypeRef t)  { return is_named_struct(t, "HermesStatic"); }
    static bool is_hermes(TypeRef t)         { return is_named_struct(t, "Hermes"); }
    static bool is_string_view(TypeRef t)    { return is_named_struct(t, "StringView"); }
    static bool is_ident(TypeRef t)          { return is_named_struct(t, "Ident"); }
    static bool is_exprblob(TypeRef t)       { return is_named_struct(t, "ExprBlob"); }
    static bool is_dataref(TypeRef t)        { return is_named_struct(t, "DataRef"); }
    static bool is_quote_item_blob(TypeRef t){ return is_named_struct(t, "QuoteItemBlob"); }
    static bool is_item_list(TypeRef t)      { return is_named_struct(t, "ItemList"); }

    // Sema goes through 3 phases:
    //   Init     — primitives populated, no user ASTs touched
    //   Collect  — collect_module phase 1+2; struct/enum/fn declarations
    //              registered. type_params_ may be incomplete during
    //              forward-decl prepass.
    //   Lower    — lower_module_items; full type-info available.
    // Validation that requires fully-populated type-params (e.g. detecting
    // type-args on a non-generic) must check phase_ == Lower.  Earlier
    // checks would wrongly fire on prepass forward-references where
    // type_params haven't been read yet.
    enum class SemaPhase { Init, Collect, Lower };
    SemaPhase phase_ = SemaPhase::Init;

    void error(std::string msg) {
        result_.diags.push_back({Diag::Level::Error, ctx_, std::move(msg), file_, node_line_});
    }
    void warn(std::string msg) {
        // Dedup happens at Diags::print time (B-li-01) — multi-phase sema
        // legitimately revisits the same source site.
        result_.diags.push_back({Diag::Level::Warning, ctx_, std::move(msg), file_, node_line_});
    }

    // ── Centralized validation primitives (Meta-Sprint M0.1) ────────────
    // See ~/.claude/projects/-home-victor-devel-logos/memory/cluster_index.md
    // and antipat_list_no_dup_check.md / invariants.md (I-6, I-7).

    // Emit "duplicate <kind> '<name>' in <container>" for any name that appears
    // twice in the parallel-named-items list.  Empty names are skipped (anonymous
    // bindings such as `_` are deliberately allowed to repeat).
    template <class Items, class GetName>
    void check_unique_names(const Items& items, GetName get_name,
                            std::string_view kind_label,
                            std::string_view container) {
        logos::compiler::StrSet seen;
        for (auto& item : items) {
            std::string name(get_name(item));
            if (name.empty() || name == "_") continue;
            if (!seen.insert(name).second) {
                error(std::format("duplicate {} '{}' in {}",
                                  kind_label, name, container));
            }
        }
    }

    // Validate that a generic instantiation supplies exactly one type-arg per
    // type parameter.  Variadic packs (`T...`) match ≥0 args at the trailing
    // slot; otherwise arity must be exact.
    //
    // Defensive: when params is empty we *don't* error on extra args, because
    // SemaStructInfo lookups during the prepass / forward-decl phase may
    // legitimately see an empty type_params list before the real definition
    // populates it. The spec/canonical lookups elsewhere will catch the
    // genuine "type args on non-generic" case (B-ty-05). Phase 5 fact-base
    // will replace this defensive carve-out with a proper graph query.
    void check_type_arg_arity(std::string_view template_name,
                              const std::vector<TypeParam>& params,
                              const std::vector<TypeRef>& args,
                              std::string_view context) {
        if (params.empty()) {
            // B-ty-05: non-generic target receiving type-args.  Only error
            // in Lower phase; in Collect phase forward-decl/prepass lookups
            // may legitimately see empty type_params before the real def
            // populates them.
            if (phase_ == SemaPhase::Lower && !args.empty()) {
                error(std::format(
                    "{} '{}': not generic — cannot accept {} type arg(s)",
                    context, template_name, args.size()));
            }
            return;
        }
        bool last_variadic = params.back().is_variadic;
        if (last_variadic) {
            // Variadic pack absorbs trailing args.  Need at least (params.size()-1) concrete args.
            if (args.size() + 1 < params.size()) {
                error(std::format("{} '{}': expected at least {} type arg(s), got {}",
                                  context, template_name, params.size() - 1, args.size()));
            }
            return;
        }
        if (params.size() != args.size()) {
            error(std::format("{} '{}': expected {} type arg(s), got {}",
                              context, template_name, params.size(), args.size()));
        }
    }

    // Read the VALUE of an annotation as an integer.  Accepts two AST shapes:
    //   #[name = 123]           → VALUE: { CODE: LIT_INT, VALUE: "123" }
    //   #[name = Enum::Variant] → VALUE: { CODE: ENUM_LIT, NAME: "Enum", FIELD: "Variant" }
    // Caller must have verified ann.has_key(la::VALUE).
    uint64_t read_annotation_u64(hermes::TinyMapView ann) {
        using namespace sema_detail;
        auto vmap = map_of(ann.get(la::VALUE.code));
        int32_t vc = code_of(vmap);
        if (vc == la::LIT_INT) {
            auto sv = str_of(vmap.get(la::VALUE.code));
            return (uint64_t)parse_int_literal(sv);
        }
        if (vc == la::ENUM_LIT) {
            auto ename = std::string(str_of(vmap.get(la::NAME.code)));
            auto vname = std::string(str_of(vmap.get(la::FIELD.code)));
            auto [epkg, esi] = find_enum_by_name(ename);
            auto it = esi ? enums_.find(sema_key(epkg, ename)) : enums_.end();
            if (it == enums_.end()) it = enums_.find(ename);
            if (it == enums_.end()) {
                error(std::format("enum '{}' not found in annotation value", ename));
                return 0;
            }
            for (auto& var : it->second.variants) {
                if (var.name == vname) return (uint64_t)(int64_t)var.value;
            }
            error(std::format("enum '{}' has no variant '{}'", ename, vname));
            return 0;
        }
        error("annotation value must be an integer literal or enum variant");
        return 0;
    }

    // ── Attribute spec registry (Meta-Sprint M0.3) ──────────────────────
    // Closes B-at-01 (unknown attr), B-at-02 (#[type_code] on generic),
    // B-at-04 (#[tag_dispatch] on non-trait), B-at-05 (#[zoned] on enum),
    // B-at-07 (#[type_code] in reserved system range).
    //
    // Phase 5 successor: a single fact-base query "annotation X applied to
    // target of kind Y where (X.targets ∌ Y)" replaces this in-tree pass.
    enum class AttrTarget { Struct, Datatype, Enum, Trait, Fn, Const };

    static const char* attr_target_name(AttrTarget t) {
        switch (t) {
            case AttrTarget::Struct:   return "struct";
            case AttrTarget::Datatype: return "datatype";
            case AttrTarget::Enum:     return "enum";
            case AttrTarget::Trait:    return "trait";
            case AttrTarget::Fn:       return "fn";
            case AttrTarget::Const:    return "const";
        }
        return "?";
    }

    // Built-in compiler-recognised attributes.  Each entry: name → bitset of
    // valid AttrTargets (1<<int(AttrTarget)).  Anything not here is treated
    // as a user `#[annotation]` lookup (and warned if unresolved).
    static unsigned attr_builtin_targets(std::string_view name) {
        auto bit = [](AttrTarget t) { return 1u << unsigned(t); };
        if (name == "type_code")
            return bit(AttrTarget::Struct) | bit(AttrTarget::Datatype) |
                   bit(AttrTarget::Enum)   | bit(AttrTarget::Trait);
        if (name == "zoned")           return bit(AttrTarget::Struct);
        if (name == "no_auto_drop")    return bit(AttrTarget::Struct);
        if (name == "annotation")      return bit(AttrTarget::Struct) | bit(AttrTarget::Datatype);
        if (name == "tag_dispatch")    return bit(AttrTarget::Trait);
        if (name == "metaprog_handler")return bit(AttrTarget::Fn);
        if (name == "no_mangle")       return bit(AttrTarget::Fn);
        if (name == "fn_macro")        return bit(AttrTarget::Fn);
        if (name == "token_macro")     return bit(AttrTarget::Fn);
        // Test harness attrs (Phase #[test]). `#[test]` marks a free fn as a
        // test case; `#[should_panic]` and `#[ignore]` are modifiers (only
        // valid in combination with `#[test]`, enforced downstream).
        if (name == "test")            return bit(AttrTarget::Fn);
        if (name == "should_panic")    return bit(AttrTarget::Fn);
        if (name == "ignore")          return bit(AttrTarget::Fn);
        // Phase 2: conditional compilation. cfg applies to all item kinds
        // (drops the item when predicate is false). cfg_attr applies a
        // wrapped attribute when predicate is true.
        if (name == "cfg" || name == "cfg_attr")
            return bit(AttrTarget::Struct) | bit(AttrTarget::Datatype) |
                   bit(AttrTarget::Enum)   | bit(AttrTarget::Trait) |
                   bit(AttrTarget::Fn)     | bit(AttrTarget::Const);
        return 0u;  // not a builtin
    }

    // Validate `pending_annots` for an item of kind `target` named `target_name`.
    // `has_type_params` controls B-at-02 (no `#[type_code]` on generic templates).
    // ── Recursive by-value cycle detection (Meta-Sprint Sprint 1.2) ──────
    // Closes B-it-01 (recursive struct → SEGFAULT in mlir_gen register_struct)
    // and B-it-02 (recursive enum, latent — silent compile).
    //
    // Walks structs_/enums_ following only by-value field/payload edges
    // (Struct, ZonedStruct, Enum). Pointer/Ref/MutRef break the cycle
    // since they lower to a fixed-size pointer.
    //
    // Phase 5 successor: Datalog
    //   cycle(t) :- by_value_field(t, t).
    //   cycle(t) :- by_value_field(t, u), cycle(u, t).
    // Walks a TypeRef collecting all TypeVar names and lifetime args.
    // Used by unused-type-param / unused-lifetime lints (B-gn-07/09).
    void collect_type_var_uses(TypeRef t,
                                logos::compiler::StrSet& tv_names,
                                logos::compiler::StrSet& lt_names) {
        if (!t) return;
        TypeRef tr = t;
        auto k = tr.kind();
        if (k == LogosType::Kind::TypeVar) {
            tv_names.insert(std::string(tr.type_var_name()));
        }
        if (k == LogosType::Kind::ConstVar) {
            tv_names.insert(std::string(tr.type_var_name()));
        }
        if ((k == LogosType::Kind::Ref || k == LogosType::Kind::MutRef) &&
            !tr.lifetime().empty()) {
            lt_names.insert(std::string(tr.lifetime()));
        }
        for (auto& lt : tr.lifetime_args()) {
            if (!lt.empty()) lt_names.insert(lt);
        }
        for (auto a : tr.type_args())   collect_type_var_uses(a, tv_names, lt_names);
        for (auto e : tr.tuple_elems()) collect_type_var_uses(e, tv_names, lt_names);
        if (tr.elem())     collect_type_var_uses(tr.elem(), tv_names, lt_names);
        if (tr.pointee())  collect_type_var_uses(tr.pointee(), tv_names, lt_names);
        for (auto p : tr.closure_params()) collect_type_var_uses(p, tv_names, lt_names);
        if (tr.closure_ret()) collect_type_var_uses(tr.closure_ret(), tv_names, lt_names);
    }

    // Post-collect: warn about declared type-params / lifetimes that don't
    // appear in the fn signature (closes B-gn-07 / B-gn-09).  Conservative:
    // only fires for fns currently; struct/enum unused-param checks need
    // more thought (phantom-data convention may legitimately exist).
    void check_unused_generics_in_funcs() {
        for (auto& [_k, fi] : generic_funcs_) {
            // Skip synthetic blanket-impl methods — the typeparam is
            // implicit from the impl bound and intentionally absent from
            // the signature when the method only forwards to a trait fn.
            if (fi.base_name.rfind("$blanket$", 0) == 0) continue;
            logos::compiler::StrSet tv_uses;
            logos::compiler::StrSet lt_uses;
            for (auto pt : fi.param_types) collect_type_var_uses(pt, tv_uses, lt_uses);
            collect_type_var_uses(fi.ret_type, tv_uses, lt_uses);
            // Trait bounds count as a use — `fn f<T: Foo>(x: i32)` may
            // legitimately have T not in signature when f only calls
            // T-static methods.  Also walk bound type-args: in
            // `<I: Iterator<T>, T>`, T appears inside I's bound only.
            for (auto& tp : fi.type_params) {
                if (!tp.bounds.empty()) tv_uses.insert(tp.name);
                for (auto& b : tp.bounds)
                    for (auto ba : b.type_args)
                        collect_type_var_uses(ba, tv_uses, lt_uses);
            }
            ctx_ = std::format("fn {}", fi.base_name);
            for (auto& tp : fi.type_params) {
                if (tp.is_variadic) continue;
                // const-generic params (e.g. `const CFG: HermesStatic`) are
                // typically consumed in the body via expression-level uses
                // (`CFG.as_view()`, `f::<CFG>(...)`) which the
                // signature-walking check doesn't track. Skipping them
                // avoids systematic false positives in pmap/CFG-driven code.
                if (tp.is_const) continue;
                // The fix-it suggestion below recommends `_`; honour that as
                // an explicit "intentionally unused" marker. Also covers
                // names that begin with `_` (Rust convention).
                if (tp.name == "_" || (!tp.name.empty() && tp.name[0] == '_'))
                    continue;
                if (tv_uses.count(tp.name) == 0) {
                    warn(std::format(
                        "type parameter '{}' is unused in fn '{}'; "
                        "consider removing or replacing with `_`",
                        tp.name, fi.base_name));
                }
            }
            // B-gn-09: same lint for declared lifetime parameters.
            for (auto& lt : fi.lifetime_params) {
                if (lt_uses.count(lt) == 0) {
                    warn(std::format(
                        "lifetime parameter '{}' is unused in fn '{}'; "
                        "consider removing it",
                        lt, fi.base_name));
                }
            }
        }
    }

    // Post-collect validation of trait bounds (Sprint catalog-sweep).
    // Closes B-gn-03 (unknown trait in bound) and B-gn-04 (bound arity).
    // Walks every recorded TypeParam.bounds across funcs/structs/enums/
    // traits and emits errors at the *definition* site.
    void check_trait_bounds_well_formed() {
        auto check_bounds = [&](const std::vector<TypeParam>& tps,
                                std::string_view ctx) {
            for (auto& tp : tps) {
                for (auto& b : tp.bounds) {
                    // Sprint 5.7: Fn / FnMut / FnOnce are recognised as
                    // compiler-builtin traits — no user-space
                    // `trait Fn { ... }` declaration required. The
                    // parenthesized form carries fn_params / fn_ret
                    // (see read_trait_bound_args).
                    if (b.is_fn_family) continue;
                    // M7-mt-03: `Sized` is a compiler-builtin marker
                    // (auto-implemented for every size-known type). Logos
                    // has no unsized types yet, so `T: Sized` is a no-op
                    // bound — accept and skip the trait-lookup. `?Sized`
                    // (opt-out) isn't grammatically expressible yet.
                    if (b.trait_name == "Sized") continue;
                    auto [_pkg, ti] = find_trait_by_name(b.trait_name);
                    if (!ti) {
                        ctx_ = std::string(ctx);
                        error(std::format(
                            "trait bound '{}: {}': unknown trait",
                            tp.name, b.trait_name));
                        continue;
                    }
                    // Arity check (variadic last param absorbs trailing).
                    auto& tps2 = ti->type_params;
                    if (tps2.empty()) continue;  // no type params → no arity
                    bool last_var = tps2.back().is_variadic;
                    size_t got = b.type_args.size();
                    bool ok;
                    if (last_var) ok = got + 1 >= tps2.size();
                    else          ok = got == tps2.size();
                    if (!ok) {
                        ctx_ = std::string(ctx);
                        error(std::format(
                            "trait bound '{}: {}': expected {} type arg(s), got {}",
                            tp.name, b.trait_name,
                            last_var ? tps2.size() - 1 : tps2.size(), got));
                    }
                }
            }
        };
        for (auto& [_k, fi] : generic_funcs_) check_bounds(fi.type_params,
            std::format("fn {}", fi.base_name));
        for (auto& [_k, si] : structs_)      check_bounds(si.type_params,
            std::format("struct {}", _k));
        for (auto& [_k, ei] : enums_)        check_bounds(ei.type_params,
            std::format("enum {}", _k));
        for (auto& [_k, dt] : datatypes_)    check_bounds(dt.type_params,
            std::format("datatype {}", _k));
    }

    void check_recursive_value_types() {
        enum Color { White, Gray, Black };
        std::unordered_map<std::string, Color> sc;
        std::unordered_map<std::string, Color> ec;
        for (auto& [k, _] : structs_) sc[k] = White;
        for (auto& [k, _] : enums_)   ec[k] = White;

        // Resolve a TypeRef edge to its registry key (struct or enum) so we
        // can look up by name, falling back from pkg-qualified to bare.
        auto find_struct_key = [&](TypeRef t) -> std::string {
            std::string q = sema_key(TypeRef(t).pkg_name(), TypeRef(t).struct_name());
            if (structs_.count(q)) return q;
            std::string b(TypeRef(t).struct_name());
            return structs_.count(b) ? b : std::string{};
        };
        auto find_enum_key = [&](TypeRef t) -> std::string {
            std::string q = sema_key(TypeRef(t).pkg_name(), TypeRef(t).enum_name());
            if (enums_.count(q)) return q;
            std::string b(TypeRef(t).enum_name());
            return enums_.count(b) ? b : std::string{};
        };

        std::function<bool(const std::string&)> visit_struct;
        std::function<bool(const std::string&)> visit_enum;
        std::function<bool(TypeRef)> walk;
        walk = [&](TypeRef t) -> bool {
            if (!t) return false;
            auto k = TypeRef(t).kind();
            if (k == LogosType::Kind::Struct ||
                k == LogosType::Kind::ZonedStruct) {
                auto sk = find_struct_key(t);
                return !sk.empty() && visit_struct(sk);
            }
            if (k == LogosType::Kind::Enum) {
                auto ek = find_enum_key(t);
                return !ek.empty() && visit_enum(ek);
            }
            if (k == LogosType::Kind::Tuple) {
                for (auto e : TypeRef(t).tuple_elems())
                    if (walk(e)) return true;
            }
            return false;
        };
        visit_struct = [&](const std::string& key) -> bool {
            auto& col = sc[key];
            if (col == Black) return false;
            if (col == Gray) {
                // Bare name from key (strip pkg::)
                auto pos = key.rfind("::");
                std::string nm = pos == std::string::npos ? key : key.substr(pos + 2);
                error(std::format("infinite-size type '{}' (cannot contain "
                                  "itself by value); use a pointer or '&{}'",
                                  nm, nm));
                col = Black;
                return true;
            }
            col = Gray;
            auto& sd = structs_[key];
            for (auto& f : sd.fields) {
                if (walk(f.type)) { col = Black; return true; }
            }
            col = Black;
            return false;
        };
        visit_enum = [&](const std::string& key) -> bool {
            auto& col = ec[key];
            if (col == Black) return false;
            if (col == Gray) {
                auto pos = key.rfind("::");
                std::string nm = pos == std::string::npos ? key : key.substr(pos + 2);
                error(std::format("infinite-size enum '{}' (variant payload "
                                  "contains itself by value); box the payload "
                                  "with '*const {}'", nm, nm));
                col = Black;
                return true;
            }
            col = Gray;
            auto& ed = enums_[key];
            for (auto& v : ed.variants) {
                for (auto& pt : v.payload_types)
                    if (walk(pt)) { col = Black; return true; }
            }
            col = Black;
            return false;
        };
        for (auto& [k, _] : structs_) if (sc[k] == White) visit_struct(k);
        for (auto& [k, _] : enums_)   if (ec[k] == White) visit_enum(k);
    }

    void check_annotations(AttrTarget target, std::string_view target_name,
                           bool has_type_params,
                           const std::vector<hermes::TinyMapView>& annots) {
        using namespace sema_detail;
        unsigned target_bit = 1u << unsigned(target);
        for (auto& ann : annots) {
            auto aname = std::string(str_of(ann.get(la::NAME.code)));
            if (aname.empty()) continue;
            // B-at-06: Rust-style `#[derive(Trait, ...)]` is not Logos
            // surface; Logos uses `#[derive_<trait>]` triggers registered by
            // `#[metaprog_handler("derive_<trait>")]`. Detect the Rust shape
            // and surface a specific diagnostic.
            if (aname == "derive" && ann.has_key(la::ARGS)) {
                error(std::format(
                    "'#[derive(...)]' on {} '{}' is not Logos syntax. Logos "
                    "uses one trigger annotation per derive: write "
                    "`#[derive_<trait>]` for each trait you want, and ensure a "
                    "`#[metaprog_handler(\"derive_<trait>\")]` fn is in scope",
                    attr_target_name(target), target_name));
                continue;
            }
            unsigned tgts = attr_builtin_targets(aname);
            if (tgts == 0) {
                // Not a builtin.  Could be:
                //   - a user `#[annotation]` datatype (resolved in apply_annots_to_*)
                //   - a metaprog-handler trigger registered cross-module
                //   - genuinely unknown (typo)
                // Cross-module ordering means we cannot reliably warn here; the
                // unknown-typo diagnostic (B-at-01) is deferred to a Phase 5
                // whole-program fact-base query.
                continue;
            }
            if ((tgts & target_bit) == 0) {
                error(std::format("attribute '#[{}]' is not valid on {} '{}'",
                                  aname, attr_target_name(target), target_name));
                continue;
            }
            // Per-attribute extra checks:
            if (aname == "type_code") {
                if (has_type_params) {
                    error(std::format("attribute '#[type_code]' cannot be applied to "
                                      "generic {} '{}' (apply on each instantiation)",
                                      attr_target_name(target), target_name));
                }
                // B-at-07: codes 1..128 are reserved for the stdlib's
                // runtime tag system (TypeTagSystem).  User code (i.e. not
                // in `std.*` package) should not use them.
                if (ann.has_key(la::VALUE)) {
                    uint64_t tc = read_annotation_u64(ann);
                    bool is_stdlib = cur_package_.starts_with("std.") ||
                                     cur_package_ == "std";
                    if (!is_stdlib && tc >= 1 && tc <= 128) {
                        warn(std::format(
                            "'#[type_code={}]' on '{}' is in the reserved "
                            "range [1..128] used by stdlib primitives; "
                            "user types should use codes ≥129", tc, target_name));
                    }
                }
            }
        }
    }

    // True iff the pattern is an unconditional catch-all (`_` or bare
    // identifier binding without a guard).  Used by reachability lint
    // (Sprint 5.2 — closes B-pt-07: arm-after-catchall).
    bool is_catchall_pat(hermes::TinyMapView arm) {
        using namespace sema_detail;
        if (arm.has_key(ast::GUARD)) return false;
        if (!arm.has_key(ast::LHS)) return false;
        auto p = map_of(arm.get(ast::LHS.code));
        // Unwrap a single-alt PAT_OR
        if (code_of(p) == ast::PAT_OR && p.has_key(ast::ITEMS)) {
            auto arr = arr_of(p.get(ast::ITEMS.code));
            if (arr.size() == 1) p = map_of(arr.get(0));
            else return false;  // multiple alts: not unconditional unless all wild
        }
        if (code_of(p) != ast::PAT_WILD) return false;
        if (!p.has_key(ast::NAME)) return true;       // anonymous `_`
        return str_of(p.get(ast::NAME.code)) == "_";  // explicit `_`
    }

    // True iff the item AST node carries a non-empty TYPE_PARAMS list.
    bool item_has_type_params(hermes::TinyMapView node) {
        using namespace sema_detail;
        if (!node.has_key(la::TYPE_PARAMS)) return false;
        auto av = node.get(la::TYPE_PARAMS.code);
        if (av.is_null()) return false;
        auto tplist = map_of(av);
        if (!tplist.has_key(la::ITEMS)) return false;
        return arr_of(tplist.get(la::ITEMS.code)).size() > 0;
    }

    // ── Scope management ─────────────────────────────────────────

    struct VarInfo { TypeRef type; bool is_mut = false; };
    struct Frame {
        logos::compiler::StrMap<VarInfo> vars;  // O(1) lookup
        std::vector<std::string> var_order;              // declaration order
    };
    std::vector<Frame> scope_;

    std::set<std::string> moved_vars_;   // variables consumed by move
    std::set<std::string> copy_types_;   // types with impl Copy — never move-only

    int destruct_counter_ = 0;           // unique-name source for `let (...)` temps

    // Phase 7 slice 12: derive-style handler registry, collected from
    // `#[metaprog_handler("name")]` annotations on hook fns.
    std::vector<lir::LProgram::MetaprogHandler> metaprog_handlers_;
    std::vector<lir::LProgram::MetaprogTarget>  metaprog_targets_;

    // Phase 7 slice 17: metaprog-compile mode. When metaprog_mode_ is true,
    // lower_fn skips body lowering for non-handler fns in the entry ast
    // (cur_ast_idx_ == metaprog_entry_ast_idx_). Errors inside skipped
    // bodies don't reach result_.diags. Set via sema_lower's SemaOptions.
    bool   metaprog_mode_           = false;
    size_t metaprog_entry_ast_idx_  = static_cast<size_t>(-1);
    std::vector<std::string> metaprog_keep_fns_;
    size_t cur_ast_idx_             = static_cast<size_t>(-1);

    // Skeleton-skip gate: symbol names already compiled into a linked
    // archive's .o (nm --defined-only). Caller-owned; outlives sema. When a
    // from_binary fn's name is in here, lower_fn skips its body (mlir_gen
    // forward-declares it on the same predicate; the linker resolves it).
    const logos::compiler::StrSet* binary_symbols_ = nullptr;
    // How many from_binary fn bodies were skeleton-skipped this run. Surfaced
    // under LOGOS_SEMA_PHASE_TIMING as an observability hook for the skip path.
    size_t skel_skip_count_         = 0;

    // M5: optional cache for binary-AST sema state, shared across
    // multiple sema_lower invocations in one compile session.
    SemaCache* cache_               = nullptr;

    // M6.1: delta start — collect()+lower_program() skip asts[0..idx).
    // 0 means "process all" (current behavior). Set by sema_lower from
    // SemaOptions::delta_start_idx.
    size_t delta_start_idx_         = 0;

    // M5 step 3b: persistent set of holders whose collect_module()
    // contribution is already in the symbol tables. Survives across
    // sema_lower invocations via install/take_snapshot. Each per-AST
    // loop in collect() checks this set and skips already-processed
    // holders (re-walking would just be deduped by the existing ODR
    // logic, but the walk itself is the cost we want to avoid).
    std::unordered_set<const hermes::MemHolder*> collected_holders_;

    // M5 step 5c: packages declared by non-binary (user) ASTs. Used
    // by take_snapshot to drop user-pkg entries before persisting —
    // user ASTs always re-walk every call, so caching their tables
    // would just trip "duplicate" diagnostics on re-insertion.
    // Populated at top of each per-AST loop in collect when
    // cur_from_binary_ is false; reset across runs.
    StrSet user_pkgs_;
    // Bare-key maps (no pkg in key) — record user-origin keys at insert
    // time so take_snapshot can drop them. Map-keyed-by-name doesn't
    // give us the pkg from the key alone; the value's .package field
    // (where present) handles symbol_name maps separately.
    StrSet user_module_const_keys_;
    StrSet user_generic_const_keys_;
    StrSet user_impl_keys_;             // "Trait::Target" keys added by user impls
    StrSet user_coherence_keys_;         // "Trait[args]::Target" keys
    StrSet user_assoc_type_impl_keys_;   // "Trait::Target::Name" keys
    StrSet user_assoc_const_impl_keys_;
    StrSet user_trait_keys_;             // bare trait names from user code
    StrSet user_type_alias_keys_;        // bare type alias names from user code
    StrSet user_blanket_mangled_;        // BlanketImpl.mangled_name from user code
    // M6.1: user holders added to collected_holders_ under keep_user_state
    // mode (so reset_user_state can drop them again).
    std::unordered_set<const hermes::MemHolder*> user_holders_;

    // Current AST root, set by lower_program at each iteration. Used by
    // sema-side intrinsics (e.g. `template_of::<X>()`) that need to walk
    // the user-root MODULE's ITEMS to bake an AST-node arena offset.
    hermes::TinyMapView cur_root_;

    bool fn_is_metaprog_handler(std::string_view name) const {
        auto base = bare_fn_name(name);
        for (const auto& mh : metaprog_handlers_)
            if (mh.hook_fn == base) return true;
        return false;
    }

    void push_scope() { scope_.emplace_back(); }
    void pop_scope() {
        if (!scope_.empty()) {
            // Remove popped variables from moved set
            for (auto& name : scope_.back().var_order)
                moved_vars_.erase(name);
            scope_.pop_back();
        }
    }

    bool is_move_type(TypeRef t) const;
    // Auto-Copy pass — populates copy_types_ for structs whose every field
    // is a Copy type and which have no `impl Drop`. Called after
    // check_supertrait_impls so manual `impl Copy` entries are already in.
    void compute_auto_copy_types();
    void mark_moved(const std::string& name) { moved_vars_.insert(name); }

    // Writing a move-type RHS into a memory cell (deref, indexed, field, …)
    // is a move. Mark the source so its scope-exit auto-drop is suppressed
    // — the byte pattern now lives in the destination, and the destination
    // owns the Drop responsibility.
    void track_write_move(const lir::LExprPtr& val) {
        if (!val) return;
        if (!is_move_type(val->type)) return;
        mark_moved_expr(expr_ref_of(*val));
    }

    // Mark a moved expression — handles VarRef + nested FieldRead chains.
    // For `outer.field` (move-type) inserts "outer.field" into moved_vars_;
    // for `outer.a.b` inserts "outer.a.b". make_drop_stmt extracts the
    // first-level field name and passes to SDrop.moved_fields so the
    // mlir-gen field-drop loop skips that field. No-op if not a move type
    // or not a recognised l-value chain.
    void mark_moved_expr(lir_view::ExprRef er) {
        if (!er) return;
        using C = lir_schema::expr::Code;
        if (er.kind() == C::VarRef) {
            if (is_move_type(er.type(cur_prog_->type_pool.impl())))
                mark_moved(std::string(lir_view::EVarRefView{er}.name()));
            return;
        }
        if (er.kind() == C::FieldRead) {
            if (!is_move_type(er.type(cur_prog_->type_pool.impl()))) return;
            // Walk down the FieldRead chain, prepending segments. Bottom must
            // be a VarRef for this to be a stable l-value path.
            std::vector<std::string> segs;
            lir_view::ExprRef cur = er;
            while (cur && cur.kind() == C::FieldRead) {
                lir_view::EFieldReadView v{cur};
                segs.emplace_back(std::string(v.field()));
                cur = v.receiver();
            }
            if (!cur || cur.kind() != C::VarRef) return;
            std::string path(lir_view::EVarRefView{cur}.name());
            for (auto it = segs.rbegin(); it != segs.rend(); ++it) {
                path.push_back('.');
                path += *it;
            }
            mark_moved(path);
        }
    }

    std::string drop_fn_for(TypeRef t) const;
    bool has_droppable_fields(TypeRef t) const;
    bool needs_drop(TypeRef t) const {
        return !drop_fn_for(t).empty() || has_droppable_fields(t);
    }

    std::optional<lir::LStmt> make_drop_stmt(const std::string& name, const VarInfo& info) const;
    std::vector<lir::LStmt> collect_drops() const;
    std::vector<lir::LStmt> collect_all_drops() const;

    // Recursive variant of mark_moved_expr that descends into composite
    // producer expressions (Call args, StructLit fields, TupleLit elems,
    // EnumLitData payloads, BlockExpr result). Used by lower_return and
    // by lower_match's arm-value consumer position so any move-typed
    // VarRef carried into the produced value is taken off the
    // collect_drops list — preventing double-drop / use-after-move.
    void mark_moved_in_expr_recursive(lir_view::ExprRef er) {
        if (!er) return;
        using C = lir_schema::expr::Code;
        switch (er.kind()) {
            case C::VarRef:
                if (is_move_type(er.type(cur_prog_->type_pool.impl())))
                    mark_moved(std::string(lir_view::EVarRefView{er}.name()));
                return;
            case C::FieldRead:
                mark_moved_expr(er);
                return;
            case C::EnumLitData:
                lir_view::EEnumLitDataView{er}.each_payload(
                    [&](lir_view::ExprRef a) { mark_moved_in_expr_recursive(a); });
                return;
            case C::Call:
                lir_view::ECallView{er}.each_arg(
                    [&](lir_view::ExprRef a) { mark_moved_in_expr_recursive(a); });
                return;
            case C::StructLit:
                lir_view::EStructLitView{er}.each_field(
                    [&](std::string_view, lir_view::ExprRef v) {
                        mark_moved_in_expr_recursive(v);
                    });
                return;
            case C::TupleLit:
                lir_view::ETupleLitView{er}.each_elem(
                    [&](lir_view::ExprRef a) { mark_moved_in_expr_recursive(a); });
                return;
            case C::BlockExpr:
                mark_moved_in_expr_recursive(lir_view::EBlockExprView{er}.result());
                return;
            default:
                return;
        }
    }

    void define(std::string_view name, TypeRef t, bool is_mut = false) {
        if (!scope_.empty()) {
            auto sname = std::string(name);
            if (!scope_.back().vars.count(sname))
                scope_.back().var_order.push_back(sname);
            scope_.back().vars[sname] = {t, is_mut};
        }
    }

    TypeRef lookup(std::string_view name) const {
        for (auto it = scope_.rbegin(); it != scope_.rend(); ++it) {
            auto f = it->vars.find(std::string(name));
            if (f != it->vars.end()) return f->second.type;
        }
        auto cit = module_consts_.find(std::string(name));
        if (cit != module_consts_.end()) return cit->second;
        return nullptr;
    }

    bool lookup_is_mut(std::string_view name) const {
        for (auto it = scope_.rbegin(); it != scope_.rend(); ++it) {
            auto f = it->vars.find(std::string(name));
            if (f != it->vars.end()) return f->second.is_mut;
        }
        return false;
    }

    // Returns true when a type acts as a pointer/reference (Ptr, Ref, or MutRef).
    static bool is_ref_like(LogosType::Kind k) noexcept {
        return k == LogosType::Kind::Ptr ||
               k == LogosType::Kind::Ref ||
               k == LogosType::Kind::MutRef;
    }

    std::string struct_name_of(std::string_view var_name);
    std::string struct_name_from_type(TypeRef t);

    // ── Module-level symbol tables ───────────────────────────────

    struct SemaFieldInfo  { std::string_view name; TypeRef type; bool is_pub = false; bool is_variadic = false; std::string doc; };
    struct SemaStructInfo { std::vector<SemaFieldInfo> fields; std::vector<TypeParam> type_params;
                            std::vector<std::string> lifetime_params;
                            // B77: declared `where 'a: 'b` outlives pairs on the
                            // struct itself. Caller's outlives graph must satisfy
                            // these (under arg-type substitution) at construction.
                            std::vector<std::pair<std::string, std::string>> lifetime_outlives;
                            bool is_pub = false; std::string source_file;
                            std::string package;
                            bool is_data_plain = true;  // false if any field is Kind::ZonedStruct
                            bool is_annotation_type = false;  // #[annotation] datatype (see LStructDef::is_annotation_type)
                            bool is_tuple_struct = false;  // B-ts-01: `struct Foo(T1, T2);` — positional fields, ctor is `Foo(a, b)` and pattern is `Foo(x, y)`
                            bool no_auto_drop = false;  // `#[no_auto_drop]` — compiler emits NO auto-Drop (user drop + field drop) for this struct. ManuallyDrop<T> lang-item shape.
                            // Phase 1B-13: custom DST — the LAST field has
                            // unsized type (`[T]` / `dyn Trait` / nested DST).
                            // The struct itself becomes unsized; can only
                            // appear behind `&`/`&mut`/`*const`/`*mut` /
                            // `Box`. Construction goes through unsafe raw-
                            // parts assembly (no by-value).
                            bool is_dst = false;
                            // Partial-spec support: when this is a specialization,
                            // base_name is the generic template (e.g. "Map") and
                            // spec_patterns holds one entry per type-param slot
                            // (either concrete types or TypeVars for partial specs).
                            std::string base_name;
                            std::vector<TypeRef> spec_patterns;
                            std::string doc;     // outer `///` doc-comment
                          };
    struct SemaFuncInfo   { std::vector<TypeRef> param_types; TypeRef ret_type;
                            std::vector<TypeParam> type_params; bool is_vararg = false;
                            // CP-cm-16 follow-up: full impl-target pattern (with
                            // TypeVars unsubstituted) for impl-block-derived
                            // methods on partial-spec impls
                            // (`impl<T,E> Trait for Foo<Vec<T>, E>`).
                            // finish_generic_call unifies this against the
                            // concrete receiver to bind impl-level T,E
                            // correctly instead of positional binding of
                            // type_args. Null otherwise.
                            TypeRef impl_target_pattern = nullptr;
                            std::vector<std::string> lifetime_params;  // for B-gn-09 lint
                            // B69: declared `where 'a: 'b` outlives pairs.
                            // Used by call-site cross-check: caller must
                            // satisfy callee's where-bounds under the lifetime
                            // substitution induced by arg-type matching.
                            std::vector<std::pair<std::string, std::string>> lifetime_outlives;
                            bool is_pub = false; bool is_unsafe = false;
                            bool is_extern = false;
                            bool is_fn_macro = false;  // #[fn_macro] callee for name!(...)
                            bool is_token_macro = false; // #[token_macro] callee (str RAW_TEXT)
                            std::string base_name;
                            std::string signature_key;
                            std::string symbol_name;
                            std::string source_file; std::string package;
                            std::string doc;     // outer `///` doc-comment
                            // Trait-aware method mangling: the trait this
                            // method implements (`impl Trait for X`), empty for
                            // inherent impls / free fns. When two traits define
                            // the same method on the same type+signature, the
                            // colliding methods are lazily re-keyed under the
                            // trait-qualified base `<target>__<trait>__<method>`
                            // (see trait_method_registry_).
                            std::string trait_name;
                          };
    struct SemaVariantInfo{
        std::string_view name; int64_t value;
        std::vector<TypeRef> payload_types;  // empty = no payload
        // P4-pm-01: struct-shape variant `V { x: T, y: U }` — parallel
        // names array (same size as payload_types). Empty for
        // tuple-shape and unit variants. Downstream callers can detect
        // struct-shape via `!payload_field_names.empty()` and resolve
        // user-written field names → positional indices.
        std::vector<std::string> payload_field_names;
        bool is_variadic = false;                     // variadic pack payload (...T)
        bool is_struct_shape = false;                 // P4-pm-01: marker for struct-shape variants
        std::string doc;     // Phase A.2: outer `///` doc-comment
    };
    struct SemaEnumInfo   {
        std::vector<SemaVariantInfo> variants;
        std::vector<TypeParam> type_params;  // for generic enums
        std::vector<std::string> lifetime_params;  // B65: enum lifetime params
        TypeRef backing_type = nullptr;  // null = default (i32)
        std::string doc;     // outer `///` doc-comment
    };

    // ── Trait info ───────────────────────────────────────────────
    struct SemaTraitMethodInfo {
        std::string name;
        std::vector<TypeParam> type_params;  // method-level: `fn hash<H: Hasher>(...)` has [H]
        std::vector<TypeRef> param_types;  // includes self
        TypeRef ret_type = nullptr;
        bool has_default = false;   // trait method has a default body
        bool is_unsafe = false;     // declared unsafe fn in trait
        hermes::AnyVal default_ast{};    // AST node for default method (valid when has_default)
        hermes::MemHolder* default_holder = nullptr;  // zone that owns default_ast
        std::string doc;     // Phase A.2: outer `///` doc-comment
    };
    struct SemaAssocTypeInfo {
        std::string name;              // e.g. "Item"
        std::vector<TraitBound> bounds;
        std::vector<TypeParam>  type_params;  // GAT params: type Item<T> has [T]
        std::string doc;     // Phase A.3: outer `///` doc-comment
    };
    struct SemaAssocConstInfo {
        std::string      name;         // e.g. "MAX"
        TypeRef type = nullptr;
        std::string doc;     // Phase A.3: outer `///` doc-comment
    };
    struct SemaTraitInfo {
        std::string name;
        std::string package;                  // pkg this trait was declared in (for B-mv-02 diag)
        std::vector<TypeParam> type_params;  // e.g. trait Into<T> has T
        std::vector<SemaTraitMethodInfo> methods;
        std::vector<SemaAssocTypeInfo>   assoc_types;
        std::vector<SemaAssocConstInfo>  assoc_consts;
        std::vector<TraitBound> supertraits;  // e.g. [{Display,[]}, {Into,[i32]}] for trait Foo: Display + Into<i32>
        bool is_unsafe = false;               // declared as `unsafe trait`
        bool is_auto   = false;               // declared with `auto trait`
        bool is_hermes = false;               // trait carries #[type_code] —
                                              // Hermes-tagged datatype family;
                                              // reflect::<T>() routes through
                                              // Hermes path
        std::string doc;     // outer `///` doc-comment
        // Three-layer split fix: pass0 pre-registers trait NAMES (predeclared=true)
        // so collect_impl in pass2 finds them regardless of iteration order
        // between the impl's file and the trait's defining file. pass2's
        // collect_trait sees the predeclared entry and overwrites it with
        // the real body (vs treating it as a duplicate-trait error).
        bool predeclared = false;
    };
    struct SemaImplInfo {
        std::string trait_name;
        std::string target_type;
        bool        is_unsafe = false;        // declared as `unsafe impl`
        bool        is_negative = false;      // `impl !Trait for X {}`
        // For generic-target impls like `unsafe impl<T: Send> Send for Mutex<T>`:
        // full target pattern (TypeVars unsubstituted) + impl-level type params
        // with bounds. Both empty for non-generic impls.
        TypeRef                target_typeref = nullptr;
        std::vector<TypeParam> impl_type_params;
        // B62: trait-arg lifetimes from `impl Trait<&'a T> for X` — needed at
        // bound-check time to detect HRTB satisfaction mismatch (impl provides
        // concrete region where bound demands universal).
        std::vector<TypeRef>        trait_type_args;
        std::vector<std::string>    trait_lifetime_args;
        // B62: impl-level lifetime params from `impl<'a, T> ...`. A trait_type_arg
        // whose lifetime string is in this set is "generic" (universally quantified
        // at impl site) and can satisfy a HRTB-bound generic lifetime.
        std::vector<std::string>    impl_lifetime_params;
        // B65: outlives bounds on impl-level lifetime params, e.g.
        // `impl<'a, 'b: 'a> ...` → [("'b", "'a")].
        std::vector<std::pair<std::string, std::string>> impl_lifetime_outlives;
        std::string doc;     // outer `///` doc-comment on the impl block
    };

    // Type params in scope for the function/struct currently being processed.
    // Maps type param name → TypeVar LogosType*.
    logos::compiler::StrMap<TypeRef> current_type_params_;
    // Pass-0 only: set of every bare type name (struct/datatype/enum) declared
    // across ALL modules in this emit, pre-scanned before name registration so
    // that specialization-vs-base classification (is_specialization_fn) does
    // not depend on inter-module processing order. nullptr outside pass 0.
    const logos::compiler::StrSet* pass0_decl_names_ = nullptr;
    // Set by collect_impl/lower_impl_block for `impl<T>` blocks so that
    // collect_fn/lower_fn include the impl-level type params in the function.
    std::vector<TypeParam> impl_type_params_;
    // CP-cm-16 follow-up: full impl-target pattern of the enclosing impl
    // block (set by collect_impl/lower_impl_block, cleared at block end).
    // Propagated to SemaFuncInfo::impl_target_pattern for impl methods so
    // finish_generic_call can pattern-unify against concrete receivers.
    TypeRef impl_target_typeref_ = nullptr;
    // Set when the upcoming collect_fn/lower_fn carries `#[no_mangle]` on
    // its annotation list. Reset to false at the end of each collect_fn /
    // lower_fn invocation so the flag never leaks across items.
    bool pending_no_mangle_ = false;
    // Set when the upcoming collect_fn carries `#[fn_macro]`. Reset after
    // collect_fn consumes it onto SemaFuncInfo.is_fn_macro. Function-style
    // macros `name!(...)` resolve only callees with this flag set.
    bool pending_fn_macro_ = false;
    // Set when the upcoming collect_fn carries `#[token_macro]`. Reset
    // after collect_fn consumes it. Token-macros receive their raw
    // source text directly as a `str` arg (slice 3b of fn-macros);
    // future slice 3c lifts this to a structured TokenStream.
    bool pending_token_macro_ = false;
    // Test-harness attribute flags. Consumed by collect_fn / lower_fn; reset
    // after the function-collection consumes them.
    bool pending_is_test_      = false;
    bool pending_should_panic_ = false;
    bool pending_ignore_       = false;
    // `#[should_panic(expected = "msg")]` — TH-th-02. Empty when no expected
    // arg supplied. Consumed by collect_fn / lower_fn alongside the booleans.
    std::string pending_should_panic_expected_;
    // Outer doc-comment buffer accumulated from `///` lines preceding the
    // next real item. Cleared by each collect_* after consuming. Joined with
    // '\n'; each line has had its leading `/// ` (or `///`) stripped.
    std::string pending_doc_;
    // Phase A.3: per-module `//!` inner-doc accumulator. Reset at the start
    // of each lower_module_items / collect_module pass; finalised onto
    // LProgram.module_inner_docs at the end of lower_module_items.
    std::string module_inner_doc_;
    // Consume the accumulated outer doc-comment buffer: move it out and
    // clear the buffer so the next collect_*/lower_* call starts fresh.
    std::string take_pending_doc() {
        std::string d = std::move(pending_doc_);
        pending_doc_.clear();
        return d;
    }
    // Phase A.2: append a single `///` line (raw token text, prefix
    // included) to the supplied buffer. Strips `///` + one optional space
    // and joins with `\n`. Used by the field/variant/method/impl-item
    // iteration loops where doc-lines are interleaved with real members.
    static void append_doc_line(std::string& buf, std::string_view raw) {
        std::string_view stripped = raw.size() >= 3 ? raw.substr(3) : std::string_view{};
        if (!stripped.empty() && stripped.front() == ' ')
            stripped.remove_prefix(1);
        if (!buf.empty()) buf.push_back('\n');
        buf.append(stripped);
    }
    // Phase A.4: try to consume a doc-node (DOC_LINE_LIT or DOC_BLOCK_LIT)
    // into the supplied buffer. Returns true on consume; callers use this
    // in iteration loops where doc-nodes are interleaved with real members.
    bool try_append_doc(std::string& buf, hermes::TinyMapView node) {
        int32_t c = code_of(node);
        if (c == sema_detail::la::DOC_LINE_LIT) {
            append_doc_line(buf, str_of(node.get(sema_detail::la::VALUE.code)));
            return true;
        }
        if (c == sema_detail::la::DOC_BLOCK_LIT) {
            append_doc_block(buf, str_of(node.get(sema_detail::la::VALUE.code)), 3);
            return true;
        }
        return false;
    }
    // Phase A.4: append a `/** ... */` (or `/*! ... */`) block-doc body
    // to buf. Strips the `/**` / `/*!` envelope and the trailing `*/`,
    // then for each line strips a leading run of whitespace + optional
    // `*` indent (common rust-doc shape:
    //     /** First line.
    //      *  Second line.
    //      *  Third line.
    //      */
    // becomes "First line.\nSecond line.\nThird line."). `prefix_len`
    // is 3 for both outer (`/**`) and inner (`/*!`) — caller picks.
    static void append_doc_block(std::string& buf, std::string_view raw,
                                 std::size_t prefix_len)
    {
        if (raw.size() < prefix_len + 2) return;          // need `*/`
        std::string_view body = raw.substr(prefix_len);
        if (body.size() >= 2) body.remove_suffix(2);      // drop `*/`
        // Split into lines and strip per-line leading whitespace + `*`.
        std::string_view rest = body;
        bool first = true;
        while (!rest.empty()) {
            auto nl = rest.find('\n');
            std::string_view line = (nl == std::string_view::npos)
                ? rest : rest.substr(0, nl);
            // Per-line strip: leading whitespace, then optional `*`, then
            // one optional space. Trailing whitespace untouched (caller
            // can render preformatted text if desired).
            std::size_t i = 0;
            while (i < line.size() && (line[i] == ' ' || line[i] == '\t'))
                ++i;
            if (i < line.size() && line[i] == '*') {
                ++i;
                if (i < line.size() && line[i] == ' ') ++i;
            }
            std::string_view stripped = line.substr(i);
            // Drop trailing whitespace.
            while (!stripped.empty() &&
                   (stripped.back() == ' ' || stripped.back() == '\t'))
                stripped.remove_suffix(1);
            // Skip empty first/last lines that arise from the envelope's
            // own newlines (`/**\n ... \n*/`).
            bool drop_empty = stripped.empty() &&
                              (first || nl == std::string_view::npos);
            if (!drop_empty) {
                if (!buf.empty()) buf.push_back('\n');
                buf.append(stripped);
            }
            first = false;
            if (nl == std::string_view::npos) break;
            rest = rest.substr(nl + 1);
        }
    }
    // Set true when the trait being collected has a `#[type_code]` annotation.
    // collect_trait reads + clears it; SemaTraitInfo.is_hermes carries the
    // result through to reflect::<T>() dispatch.
    bool pending_trait_is_hermes_ = false;
    // Mangled name of the currently-being-lowered fn. Used by make_drop_stmt
    // to skip auto-drop on the `self` parameter of a Drop fn (would be
    // infinite self-recursion).
    std::string current_fn_mangled_;

    // Re-export graph: pkg_reexports_["a.b"] = ["x.y", "z"] means `pub use x.y; pub use z;`
    // is declared in package a.b. Used by find_* helpers for transitive import resolution.
    logos::compiler::StrMap<std::vector<std::string>> pkg_reexports_;

    logos::compiler::StrMap<SemaStructInfo>   structs_;
    logos::compiler::StrMap<SemaStructInfo>   datatypes_;  // Hermes datatypes
    // Explicit type_code from #[type_code=N] annotations; populated in lower_module_items.
    logos::compiler::StrMap<uint64_t>         explicit_type_codes_;
    // concrete_name (e.g. "Pair__i32") → SemaStructInfo for explicit specializations.
    logos::compiler::StrMap<SemaStructInfo>   struct_specs_sema_;
    logos::compiler::StrMap<SemaEnumInfo>     enums_;
    logos::compiler::StrMap<SemaFuncInfo>     funcs_;
    // base name -> concrete overload symbols stored in funcs_.
    logos::compiler::StrMap<std::vector<std::string>> func_overloads_;
    // symbol_name -> generic function info.
    logos::compiler::StrMap<SemaFuncInfo>     generic_funcs_;
    // base name -> generic overload symbols stored in generic_funcs_.
    logos::compiler::StrMap<std::vector<std::string>> generic_overloads_;
    // Trait-aware method mangling: plain base `<target>__<method>` -> the
    // distinct traits that define a method of that name on that type with the
    // same signature. Populated lazily when a collision is detected in
    // collect_fn; the colliding methods get re-keyed under the trait-qualified
    // base `<target>__<trait>__<method>`. A plain base present here with >1
    // entry is ambiguous for concrete-receiver dispatch (needs disambiguation).
    logos::compiler::StrMap<std::vector<std::string>> trait_method_registry_;
    // Generic type alias entry: non-generic aliases have type_params empty.
    struct TypeAliasEntry {
        TypeRef         type;
        std::vector<TypeParam>   type_params;
        std::vector<std::string> lifetime_params;  // e.g. ["'z"] for type Foo<'z, T> = ...
    };

    // Lifetime substitution map: "'z" → "'a"  (name → name, erased at codegen).
    using SemaLifetimeSubst = logos::compiler::StrMap<std::string>;
    logos::compiler::StrMap<TypeAliasEntry> type_aliases_;
    logos::compiler::StrMap<TypeRef> module_consts_;
    // P4-pm-06: AST node of each module-const initializer, retained so
    // `match x { CONST => … }` can ctfe-eval CONST and lower as a value
    // pattern (PAT_INT/PAT_BOOL/PAT_CHAR) instead of silently binding
    // the scrutinee to a fresh local named CONST.
    logos::compiler::StrMap<hermes::TinyMapView> module_const_values_;

    // B-ts-01: synth tuple-struct field names "0", "1", … interned in
    // a long-lived list of unique_ptrs so SemaFieldInfo::name (a
    // `string_view`) stays valid for the lifetime of `structs_`.
    // unique_ptr ensures the underlying std::string never moves on
    // pool growth (vector reallocations would invalidate views).
    std::vector<std::unique_ptr<std::string>> synth_field_name_pool_;
    std::string_view intern_synth_field_name(size_t i) {
        while (synth_field_name_pool_.size() <= i)
            synth_field_name_pool_.push_back(
                std::make_unique<std::string>(std::to_string(synth_field_name_pool_.size())));
        return *synth_field_name_pool_[i];
    }

    // Generic compile-time consts: `pub const X<T1, T2, …>: HermesStatic =
    // @{ … <type:T1> … };`. Stores the templated value-AST node + type-params
    // declaration. At each use-site `X<concrete1, concrete2>` sema pushes the
    // type-args into current_type_params_ and re-resolves the value-AST under
    // that scope — slot lookups (`<type:T1>` HERMES_TYPE_LIT) hit the bound
    // type, and the FNV hash of the AST walk substitutes the TypeVar name
    // for the concrete type's str representation, yielding a fresh
    // per-instantiation HStaticLit identity.
    struct GenericConstEntry {
        std::vector<TypeParam>  type_params;
        hermes::TinyMapView     value_node;   // AST of the RHS hermes_lit
        hermes::MemHolder*      holder = nullptr;  // module-of-decl holder
    };
    logos::compiler::StrMap<GenericConstEntry> generic_consts_;
    logos::compiler::StrMap<SemaTraitInfo>    traits_;
    // "TraitName::TypeName" → impl info
    logos::compiler::StrMap<SemaImplInfo>     impls_;
    // Coherence-only set keyed by `Trait[arg1,arg2,...]::Target`. impls_ stays
    // bare (so existing bound-check / has_impl / find_impl callers without
    // trait_args still hit a registered impl); duplicate-detection uses this
    // map so e.g. `impl From<i8> for i32` and `impl From<i16> for i32` are
    // recognised as different impls.
    logos::compiler::StrSet                   coherence_keys_;
    // "TraitName::TypeName::AssocName" → assoc type + type params for substitution.
    struct AssocTypeEntry {
        TypeRef       type;
        std::vector<TypeParam> impl_type_params;  // from enclosing impl<T>
        std::vector<TypeParam> gat_type_params;   // from GAT itself: type Item<T> = ...
        std::string doc;     // Phase A.4: outer `///`/`/** */` doc-comment
    };
    logos::compiler::StrMap<AssocTypeEntry> assoc_type_impls_;

    // "TraitName::TypeName::ConstName" → assoc const type (value evaluated lazily at call site)
    struct AssocConstEntry {
        TypeRef  type;
        hermes::AnyVal    value_ast;  // AST expr node for the constant value
        mutable lir::LExprPtr cached_value = nullptr;  // lowered once, reused at every access site
        std::string doc;     // Phase A.4: outer `///`/`/** */` doc-comment
    };
    logos::compiler::StrMap<AssocConstEntry> assoc_const_impls_;

    // Current trait being defined (set during collect_trait for Self::Item resolution)
    std::string current_trait_name_;
    // Phase 6 (GAT projection): current impl's trait name, set during
    // collect_impl + lower_impl_block. Lets `Self::Item<X>` inside an
    // impl method body resolve through the impl's trait (the impl
    // itself isn't yet in impls_ when method signatures are being
    // type-checked during collect_impl).
    std::string current_impl_trait_name_;
    // Bounds per type param name (set alongside current_type_params_ during push_type_params)
    logos::compiler::StrMap<std::vector<TraitBound>> current_type_bounds_;
    // Phase 1B-9: names of currently-in-scope type params that carry `?Sized`
    // (i.e. have implicit_sized=false). Consulted at substitution-time
    // sized-enforcement: when a `T` in this set is passed as a type-arg to
    // a callee that requires Sized on the corresponding param, emit the
    // same diagnostic as for explicit unsized substitutions.
    logos::compiler::StrSet current_type_relaxed_sized_;

    // Blanket impls: `impl<T: Bound> Trait for T { fn method(…) … }`.  Stored
    // here during collect; consulted at method-call sites when direct lookup
    // on the concrete receiver type fails.  Each entry records one method of
    // one blanket impl.  mangled_name is "T__method" — the generic function
    // lives in generic_funcs_ under this name.
    struct BlanketImpl {
        std::string trait_name;                 // e.g. "Datatype"
        std::string target_typevar;             // e.g. "T"
        std::string bound_trait;                // primary bound, e.g. "Primitive"
        std::vector<std::string> extra_bounds;  // bounds[1..] for AND-filter
        std::string method_name;                // e.g. "storage_new"
        std::string mangled_name;               // target_typevar + "__" + method_name
        // ADR 0008: associated-type equality clauses on the primary bound and
        // each extra bound, parallel to bound_trait/extra_bounds. Keyed by
        // bound trait name so the dispatcher can re-find them.
        // (bound_trait_name, [(assoc_name, expected_type)]) for primary + extras.
        std::vector<std::pair<std::string, TypeRef>> primary_assoc_eqs;
        std::vector<std::pair<std::string,
            std::vector<std::pair<std::string, TypeRef>>>> extra_assoc_eqs;
    };
    std::vector<BlanketImpl> blanket_impls_;

    // ── Package-qualified symbol lookup helpers ───────────────────

    // Look up struct/datatype/enum by LogosType (uses pkg_name if set, else unqualified fallback)
    SemaStructInfo* get_struct_si(TypeRef tr) {
        if (!tr) return nullptr;
        if (!tr.pkg_name().empty()) {
            auto it = structs_.find(sema_key(tr.pkg_name(), tr.struct_name()));
            if (it != structs_.end()) return &it->second;
            return nullptr;
        }
        auto it = structs_.find(tr.struct_name());
        return it != structs_.end() ? &it->second : nullptr;
    }
    SemaStructInfo* get_datatype_si(TypeRef tr) {
        if (!tr) return nullptr;
        if (!tr.pkg_name().empty()) {
            auto it = datatypes_.find(sema_key(tr.pkg_name(), tr.struct_name()));
            if (it != datatypes_.end()) return &it->second;
            return nullptr;
        }
        auto it = datatypes_.find(tr.struct_name());
        return it != datatypes_.end() ? &it->second : nullptr;
    }
    SemaEnumInfo* get_enum_si(TypeRef tr) {
        if (!tr) return nullptr;
        if (!tr.pkg_name().empty()) {
            auto it = enums_.find(sema_key(tr.pkg_name(), tr.enum_name()));
            if (it != enums_.end()) return &it->second;
            return nullptr;
        }
        auto it = enums_.find(tr.enum_name());
        return it != enums_.end() ? &it->second : nullptr;
    }

    // Collect all packages reachable from `pkg` via pub use re-exports (transitive, cycle-safe).
    void collect_reexports(const std::string& pkg,
                           StrSet& visited,
                           std::vector<std::string>& out) const {
        auto it = pkg_reexports_.find(pkg);
        if (it == pkg_reexports_.end()) return;
        for (auto& reexp : it->second) {
            if (visited.insert(reexp).second) {
                out.push_back(reexp);
                collect_reexports(reexp, visited, out);
            }
        }
    }

    // Build the full set of packages to search when resolving a name in context of cur_imports_.
    // Includes directly imported packages AND their transitive pub-use re-exports.
    std::vector<std::string> effective_import_pkgs() const {
        std::vector<std::string> result;
        StrSet visited;
        for (auto& pkg : cur_imports_.wildcard_packages) {
            if (visited.insert(pkg).second) {
                result.push_back(pkg);
                collect_reexports(pkg, visited, result);
            }
        }
        return result;
    }

    // Find struct by user-written name (searches cur_package_ then imports+reexports then unqualified)
    // Generic lookup: try cur_package_ → imported packages → bare key.
    // `pub_access_check` flips on for kinds where SemaInfo carries `is_pub`
    // and `package` (struct/datatype). Enum/trait don't enforce pub on
    // lookup at present (see B-mv-02 family — same arc as cross-pkg
    // resolution rework).
    // Generic qualified lookup. PubCheck=true triggers check_pub_access on
    // imported-pkg hits; the branch is `if constexpr` so SemaEnumInfo /
    // SemaTraitInfo (no is_pub field) compile cleanly with PubCheck=false.
    template <bool PubCheck, class Map>
    auto lookup_qualified_(Map& m, std::string_view name)
        -> std::pair<std::string, typename Map::mapped_type*>
    {
        if (!cur_package_.empty()) {
            auto it = m.find(sema_key(cur_package_, std::string(name)));
            if (it != m.end()) return {cur_package_, &it->second};
        }
        for (auto& pkg : effective_import_pkgs()) {
            auto it = m.find(sema_key(pkg, std::string(name)));
            if (it != m.end()) {
                if constexpr (PubCheck) {
                    check_pub_access(it->second.is_pub,
                                     it->second.package, name);
                }
                return {pkg, &it->second};
            }
        }
        auto it = m.find(std::string(name));
        if (it != m.end()) return {"", &it->second};
        return {"", nullptr};
    }
    std::pair<std::string, SemaStructInfo*> find_struct_by_name(std::string_view name) {
        return lookup_qualified_<true>(structs_, name);
    }
    std::pair<std::string, SemaStructInfo*> find_datatype_by_name(std::string_view name) {
        return lookup_qualified_<true>(datatypes_, name);
    }
    std::pair<std::string, SemaEnumInfo*> find_enum_by_name(std::string_view name) {
        return lookup_qualified_<false>(enums_, name);
    }
    std::pair<std::string, SemaTraitInfo*> find_trait_by_name(std::string_view name) {
        return lookup_qualified_<false>(traits_, name);
    }
    bool has_struct(std::string_view name)   { return find_struct_by_name(name).second   != nullptr; }
    bool has_datatype(std::string_view name) { return find_datatype_by_name(name).second != nullptr; }
    bool has_enum(std::string_view name)     { return find_enum_by_name(name).second     != nullptr; }

    // ── Type parameter helpers ────────────────────────────────────

    // B64: compute per-struct/enum variance via fixed-point over field types.
    // Populates variance_table_ keyed by "pkg.Name" with VarianceMap entries
    // using `#i` (type param i) and `@i` (lifetime param i) as keys.
    void compute_variances();

    std::vector<std::string> read_lifetime_params(hermes::TinyMapView node);
    // B65: read declared outlives bounds. Returns (long, short) pairs from
    // `'long: 'short` clauses (in fn/struct/enum/impl header or where).
    // Scans node.TYPE_PARAMS items (LIFETIME_PARAM with non-empty ITEMS).
    std::vector<std::pair<std::string, std::string>>
        read_lifetime_outlives(hermes::TinyMapView node);
    std::vector<std::pair<std::string, std::string>>
        read_lifetime_outlives_from(hermes::TinyMapView node, int32_t field_code);
    std::vector<TypeParam> read_type_params_from(hermes::TinyMapView node, int32_t field_code);
    std::vector<TypeParam> read_type_params(hermes::TinyMapView node);
    // Read type_args + assoc_eqs from a TRAIT_BOUND node's TYPE_PARAMS slot.
    // ASSOC_EQ_BIND items go to assoc_eqs; everything else is resolved as a type.
    void read_trait_bound_args(hermes::TinyMapView bnode, TraitBound& tb);
    // Phase 1: post-process collected bounds. Walks `tp.bounds` and:
    //   - for each bound with `is_relaxed=true`, validates `trait_name=="Sized"`
    //     (otherwise emits an error: only `?Sized` is permitted);
    //   - clears `tp.implicit_sized` when `?Sized` was seen;
    //   - removes the relaxed entries from `tp.bounds` so downstream code
    //     sees only positive bounds.
    // Call this once per type param after all bounds (including `where`
    // clause additions) have been collected.
    void finalize_relaxed_bounds(TypeParam& tp);

    // ADR 0008: check that a `Trait<Assoc = Type>` clause holds for `concrete`.
    // `concrete_name` is type_str(concrete); `base_name` is its struct base name
    // (or empty if not a struct). Returns false if any expected_type does not
    // match the impl's actual `type Assoc = ...` resolution after subst.
    bool assoc_eqs_satisfied(
        const std::string& trait_name,
        const std::string& concrete_name,
        const std::string& base_name,
        const std::vector<std::pair<std::string, TypeRef>>& expected);

    // Save-and-restore stack so shadowing (e.g. trait<T> + method<T>) doesn't
    // wipe the outer binding on pop. Each push records the old value (if any)
    // keyed by call site (vector address); pop restores in reverse order.
    struct ShadowFrame {
        std::string name;
        TypeRef old_type = nullptr;
        bool had_type = false;
        std::vector<TraitBound> old_bounds;
        bool had_bounds = false;
        // Phase 1B-9: shadow the `?Sized` flag too — without this, popping
        // a frame whose param had relaxed-Sized would leave the outer
        // binding (if any) clobbered.
        bool was_relaxed_sized = false;
    };
    std::vector<std::vector<ShadowFrame>> type_param_shadow_stack_;

    void push_type_params(const std::vector<TypeParam>& tps) {
        type_param_shadow_stack_.emplace_back();
        auto& frames = type_param_shadow_stack_.back();
        frames.reserve(tps.size());
        for (auto& tp : tps) {
            ShadowFrame f;
            f.name = tp.name;
            auto it = current_type_params_.find(tp.name);
            if (it != current_type_params_.end()) { f.had_type = true; f.old_type = it->second; }
            auto bit = current_type_bounds_.find(tp.name);
            if (bit != current_type_bounds_.end()) { f.had_bounds = true; f.old_bounds = bit->second; }
            f.was_relaxed_sized = current_type_relaxed_sized_.count(tp.name) != 0;
            frames.push_back(std::move(f));
            if (tp.is_const) {
                LogosTypeBuilder c; c.kind = LogosType::Kind::ConstVar;
                c.type_var_name = tp.name;
                c.pointee = tp.const_type;
                current_type_params_[tp.name] = pool_->alloc(std::move(c));
            } else {
                current_type_params_[tp.name] = make_typevar(tp.name);
            }
            if (!tp.bounds.empty()) {
                current_type_bounds_[tp.name] = tp.bounds;
            } else {
                current_type_bounds_.erase(tp.name);
            }
            // Phase 1B-9: track `?Sized` opt-out per name. `implicit_sized`
            // was cleared by finalize_relaxed_bounds when `T: ?Sized` is
            // parsed.
            if (!tp.implicit_sized)
                current_type_relaxed_sized_.insert(tp.name);
            else
                current_type_relaxed_sized_.erase(tp.name);
        }
    }
    void pop_type_params(const std::vector<TypeParam>& /*tps*/) {
        if (type_param_shadow_stack_.empty()) return;
        auto& frames = type_param_shadow_stack_.back();
        for (auto rit = frames.rbegin(); rit != frames.rend(); ++rit) {
            if (rit->had_type) current_type_params_[rit->name] = rit->old_type;
            else               current_type_params_.erase(rit->name);
            if (rit->had_bounds) current_type_bounds_[rit->name] = rit->old_bounds;
            else                 current_type_bounds_.erase(rit->name);
            if (rit->was_relaxed_sized) current_type_relaxed_sized_.insert(rit->name);
            else                        current_type_relaxed_sized_.erase(rit->name);
        }
        type_param_shadow_stack_.pop_back();
    }

    // ── Sema-side type substitution (TypeVar → concrete) ────────────

    using SemaSubst = logos::compiler::StrMap<TypeRef>;

    TypeRef subst_type_sema(TypeRef t, const SemaSubst& s,
                                      const SemaLifetimeSubst& ls = {});

    // ── Compatibility ────────────────────────────────────────────
    bool compat(TypeRef from, TypeRef to) const {
        return types_compatible(from, to);
    }
    // B64: post-compat variance gate. Returns true when `from` is variance-
    // compatible with `to`. `permissive` (default true) forwards to outlives()
    // — false at body sites where lifetimes are fn-scope-fixed.
    bool variance_ok(TypeRef from, TypeRef to, bool permissive = true) const {
        if (!from || !to) return true;
        auto adj = outlives_adj(current_outlives_);
        return subtype(from, to, adj, variance_table_, /*depth=*/0, permissive);
    }
    // B69: caller cross-check of callee's `where 'a: 'b` bounds.
    // Walks param_types parallel to arg_types and extracts a callee→caller
    // lifetime substitution map. For each pair in callee_outlives,
    // substitutes via the map and verifies the caller's current_outlives_
    // graph satisfies the resulting (caller_long, caller_short) relation.
    // Permissive on empty source lifetimes (caller may elide).
    void check_call_outlives(
        const std::string& callee_name,
        const std::vector<TypeRef>& callee_param_types,
        const std::vector<lir::LExprPtr>& arg_exprs,
        const std::vector<std::pair<std::string, std::string>>& callee_outlives) {
        if (callee_outlives.empty()) return;
        // Build callee_lt → caller_lt substitution by walking matched
        // param/arg type pairs.
        std::unordered_map<std::string, std::string> subst;
        auto record = [&](std::string_view callee_lt, std::string_view caller_lt) {
            if (callee_lt.empty() || caller_lt.empty()) return;
            std::string ck = outlives_norm(callee_lt);
            std::string vk = outlives_norm(caller_lt);
            auto it = subst.find(ck);
            if (it == subst.end()) subst.emplace(ck, vk);
            // If already present and different — conflict, but caller-region
            // inference would unify; skip strict enforcement here.
        };
        std::function<void(TypeRef, TypeRef)> walk =
            [&](TypeRef pt, TypeRef at) {
            if (!pt || !at) return;
            using K = LogosType::Kind;
            auto pk = pt.kind();
            if ((pk == K::Ref || pk == K::MutRef) &&
                (at.kind() == K::Ref || at.kind() == K::MutRef)) {
                record(pt.lifetime(), at.lifetime());
                walk(pt.pointee(), at.pointee());
                return;
            }
            if (pk == K::Struct || pk == K::ZonedStruct || pk == K::Enum) {
                if (at.kind() == pk) {
                    auto pl = pt.lifetime_args(); auto al = at.lifetime_args();
                    for (size_t i = 0; i < pl.size() && i < al.size(); ++i)
                        record(pl[i], al[i]);
                    auto pa = pt.type_args(); auto aa = at.type_args();
                    for (size_t i = 0; i < pa.size() && i < aa.size(); ++i)
                        walk(pa[i], aa[i]);
                }
                return;
            }
            if (pk == K::Tuple && at.kind() == K::Tuple) {
                auto pe = pt.tuple_elems(); auto ae = at.tuple_elems();
                for (size_t i = 0; i < pe.size() && i < ae.size(); ++i)
                    walk(pe[i], ae[i]);
                return;
            }
            if ((pk == K::Slice || pk == K::Array) && at.kind() == pk) {
                walk(pt.elem(), at.elem());
                return;
            }
            if (pk == K::Ptr && at.kind() == K::Ptr) {
                walk(pt.pointee(), at.pointee());
                return;
            }
        };
        size_t n = std::min(callee_param_types.size(), arg_exprs.size());
        for (size_t i = 0; i < n; ++i)
            if (arg_exprs[i]) walk(callee_param_types[i], arg_exprs[i]->type);
        auto adj = outlives_adj(current_outlives_);
        for (auto& [c_long, c_short] : callee_outlives) {
            // 'static is reserved — always satisfies; skip checking.
            if (outlives_is_static(c_long)) continue;
            auto it_l = subst.find(outlives_norm(c_long));
            auto it_s = subst.find(outlives_norm(c_short));
            // Only enforce when BOTH callee lifetimes are visible at arg
            // positions and mapped to concrete caller lifetimes. Otherwise
            // the lifetime is either internal to the callee or elided at
            // the call site — caller's region inference handles it.
            if (it_l == subst.end() || it_s == subst.end()) continue;
            const std::string& caller_long  = it_l->second;
            const std::string& caller_short = it_s->second;
            if (caller_long == caller_short) continue;
            if (!outlives(caller_long, caller_short, adj, /*permissive_empty=*/false)) {
                error(std::format(
                    "call to '{}': caller does not satisfy callee's "
                    "outlives bound `{}: {}` (under arg-type substitution: "
                    "`{}: {}` required)",
                    callee_name, c_long, c_short, caller_long, caller_short));
            }
        }
    }

    // B77: at a struct literal `S { ... }` (or tuple-struct ctor), verify that
    // the caller's outlives graph satisfies S's declared `where 'a: 'b` clauses
    // under the substitution induced by (a) explicit lt args, when present,
    // and (b) walking field-type pairs.
    void check_struct_lit_outlives(
        const std::string& sname,
        const std::vector<std::string>& sinfo_lt_params,
        const std::vector<std::pair<std::string, std::string>>& sinfo_outlives,
        const std::vector<std::string>& concrete_lt_args,
        const std::vector<SemaFieldInfo>& sinfo_fields = {},
        const std::vector<std::pair<std::string, lir::LExprPtr>>& lit_fields = {}) {
        if (sinfo_outlives.empty() || sinfo_lt_params.empty()) return;
        std::unordered_map<std::string, std::string> subst;
        auto record = [&](std::string_view callee_lt, std::string_view caller_lt) {
            if (callee_lt.empty() || caller_lt.empty()) return;
            std::string ck = outlives_norm(callee_lt);
            std::string vk = outlives_norm(caller_lt);
            if (ck.empty() || vk.empty()) return;
            auto it = subst.find(ck);
            if (it == subst.end()) subst.emplace(ck, vk);
        };
        // Seed from explicit lt args (e.g. `Pair::<'x, 'y> { ... }` or
        // hinted by the binding site).
        for (size_t i = 0; i < sinfo_lt_params.size() && i < concrete_lt_args.size(); ++i)
            record(sinfo_lt_params[i], concrete_lt_args[i]);
        // Walk paired field type / value type — extract lifetimes from
        // refs and recurse through nested aggregates.
        std::function<void(TypeRef, TypeRef)> walk =
            [&](TypeRef pt, TypeRef at) {
            if (!pt || !at) return;
            using K = LogosType::Kind;
            auto pk = pt.kind();
            if ((pk == K::Ref || pk == K::MutRef) &&
                (at.kind() == K::Ref || at.kind() == K::MutRef)) {
                record(pt.lifetime(), at.lifetime());
                walk(pt.pointee(), at.pointee());
                return;
            }
            if (pk == K::Struct || pk == K::ZonedStruct || pk == K::Enum) {
                if (at.kind() == pk) {
                    auto pl = pt.lifetime_args(); auto al = at.lifetime_args();
                    for (size_t i = 0; i < pl.size() && i < al.size(); ++i)
                        record(pl[i], al[i]);
                    auto pa = pt.type_args(); auto aa = at.type_args();
                    for (size_t i = 0; i < pa.size() && i < aa.size(); ++i)
                        walk(pa[i], aa[i]);
                }
                return;
            }
            if (pk == K::Tuple && at.kind() == K::Tuple) {
                auto pe = pt.tuple_elems(); auto ae = at.tuple_elems();
                for (size_t i = 0; i < pe.size() && i < ae.size(); ++i)
                    walk(pe[i], ae[i]);
                return;
            }
            if ((pk == K::Slice || pk == K::Array) && at.kind() == pk) {
                walk(pt.elem(), at.elem());
                return;
            }
            if (pk == K::Ptr && at.kind() == K::Ptr) {
                walk(pt.pointee(), at.pointee());
                return;
            }
        };
        for (auto& [fname, fval] : lit_fields) {
            if (!fval) continue;
            TypeRef fdecl = nullptr;
            for (auto& fi : sinfo_fields) {
                if (fi.name == fname) { fdecl = fi.type; break; }
            }
            if (fdecl) walk(fdecl, fval->type);
        }
        auto adj = outlives_adj(current_outlives_);
        for (auto& [c_long, c_short] : sinfo_outlives) {
            if (outlives_is_static(c_long)) continue;
            auto it_l = subst.find(outlives_norm(c_long));
            auto it_s = subst.find(outlives_norm(c_short));
            if (it_l == subst.end() || it_s == subst.end()) continue;
            const std::string& caller_long  = it_l->second;
            const std::string& caller_short = it_s->second;
            if (caller_long == caller_short) continue;
            if (!outlives(caller_long, caller_short, adj, /*permissive_empty=*/false)) {
                error(std::format(
                    "struct literal '{}': caller does not satisfy declared "
                    "outlives bound `{}: {}` (under lifetime substitution: "
                    "`{}: {}` required)",
                    sname, c_long, c_short, caller_long, caller_short));
            }
        }
    }

    // Emit an "X: variance mismatch …" error if from ↛ to under variance.
    // `permissive` should be false at body sites (return / let-init) where
    // both lifetimes are fn-scope-fixed; true at call-site arg-pass where
    // caller's region inference fills in unresolved regions.
    void check_variance(TypeRef from, TypeRef to, const std::string& ctx,
                        bool permissive = true) {
        if (!from || !to) return;
        if (TypeRef(from).kind() == LogosType::Kind::Error ||
            TypeRef(to).kind() == LogosType::Kind::Error) return;
        if (!types_compatible(from, to)) return;  // outer check handles it
        if (variance_ok(from, to, permissive)) return;
        auto [es, gs] = type_str_pair(to, from);
        error(std::format("{}: variance mismatch — expected {}, got {} — "
                          "lifetime structure incompatible "
                          "(check &mut invariance / contravariance rules)",
                          ctx, es, gs));
    }

    // ── Type resolution ──────────────────────────────────────────

    TypeRef resolve_type(hermes::TinyMapView node);
    // Hash a bare hermes_lit AST (HERMES_MAP / _ARRAY / scalars) and register
    // its lowered LIR HermesVal in cur_prog_->hstatic_registry_; return the
    // corresponding HStaticLit TypeRef. Shared between the LIT_HSTATIC type-
    // arg handler in resolve_type and `pub const X: HermesStatic = @{...};`
    // recognition in collect_const.
    TypeRef resolve_hstatic_value(hermes::TinyMapView val_node);

    // ── Collection phase ─────────────────────────────────────────

    void collect(const std::vector<hermes::Hermes>& asts);
    void simplify_all_types();
    void check_supertrait_impls();
    std::string read_package_name(hermes::TinyMapView mod);
    void check_pub_access(bool is_pub, const std::string& def_package,
                          std::string_view item_name);
    void check_type_bounds(const std::string& target_name,
                           const std::vector<TypeParam>& type_params,
                           const std::vector<TypeRef>& args);

    // Recursive trait-satisfaction: does `concrete` (or `concrete_alt`,
    // an optional unwrapped alias) implement `trait_name`, directly via
    // `impls_` or transitively via any chain of blanket impls?
    //
    // Walks blanket_impls_ for the trait and checks the bounds of each
    // candidate recursively. `seen` prevents infinite recursion through
    // cyclic blanket chains; each candidate gets its own copy of `seen`,
    // so a failed first candidate does not poison sub-checks for the
    // next candidate. Mirrors the now-fixed has_impl in
    // mono_clone.cpp::method_bound_ok.
    //
    // Note: assoc-type-equality clauses (ADR 0008) are NOT validated by
    // this helper — call sites that need them should keep their explicit
    // assoc_eqs_satisfied checks alongside.
    bool sema_has_impl_recursive(const std::string& trait_name,
                                 const std::string& concrete,
                                 const std::string& concrete_alt,
                                 logos::compiler::StrSet& seen);
    void collect_module(hermes::TinyMapView mod, int phase);
    void collect_enum(hermes::TinyMapView node);
    void collect_type_alias(hermes::TinyMapView node);
    void collect_const(hermes::TinyMapView node);
    void collect_trait(hermes::TinyMapView node);
    void collect_impl(hermes::TinyMapView node);
    void collect_struct_spec(hermes::TinyMapView node);
    void collect_struct(hermes::TinyMapView node);
    void collect_datatype(hermes::TinyMapView node, bool is_annotation_type = false);
    TypeRef try_resolve_as_known_type(std::string_view name);
    bool is_known_type_name(std::string_view name) const;
    void extract_typevars_from_type_node(hermes::TinyMapView node,
                                         std::vector<TypeParam>& out);
    bool is_specialization_fn(hermes::TinyMapView node);
    bool is_specialization_struct(hermes::TinyMapView node);
    lir::LStructDef lower_spec_struct(hermes::TinyMapView node);
    lir::LFunction lower_spec_fn(hermes::TinyMapView node);
    void collect_fn(hermes::TinyMapView node, std::string_view struct_ctx = {},
                    std::string_view trait_ctx = {});

    // ── Auto trait satisfaction ───────────────────────────────────

    // Recursively checks whether type T satisfies auto trait `trait_name`
    // (e.g. "Send" or "Sync"). Returns true if satisfied.
    bool is_auto_trait_satisfied(TypeRef tv, std::string_view trait_name,
                                  StrSet& visited);

    // Set by is_auto_trait_satisfied when it finds a non-satisfying field.
    struct AutoTraitOffender {
        std::string      field_name;
        TypeRef field_ty = nullptr;
    };
    AutoTraitOffender last_offender_;

    // ── Loop depth / return type ─────────────────────────────────

    int loop_depth_ = 0;
    // Stack of currently-active labelled loops. Only non-empty labels are
    // pushed. Used by `break 'label` / `continue 'label` to validate that
    // the label is in scope (closes B-st-05 — break with bad label leaked
    // to mlir-gen).
    std::vector<std::string> active_loop_labels_;
    bool inside_unsafe_ = false;
    // Phase 1B: when resolving a type AST node, only contexts that genuinely
    // permit an unsized result set this flag (e.g. a turbofish type argument
    // bound for a `T: ?Sized` parameter, or an impl-self-type at a `?Sized`
    // position). Default OFF: bare `[T]` / `dyn Trait` standalone produce
    // an error so unsized types never slip into value positions silently.
    bool unsized_ok_ = false;
    TypeRef ret_type_ = nullptr;
    // B64/B65: outlives graph of the currently-lowering fn, used by the
    // variance-aware subtype check at coercion sites.
    std::vector<std::pair<std::string, std::string>> current_outlives_;
    // B64: per-struct/enum variance table, computed once via fixed-point
    // over the program's user defs and consulted by the subtype check.
    DefVarianceTable variance_table_;
    TypeRef break_value_type_ = nullptr;  // type yielded by break <expr>
    bool break_without_value_ = false;
    std::string pending_loop_label_;  // set by LABELED_LOOP before lowering inner loop
    bool match_in_tail_position_ = false;
    // B-fn-06: when true, TAIL_EXPR statements act as implicit returns.
    // Set around fn-body lowering; cleared inside block-as-expression
    // contexts (match-arm-body, unsafe-block-as-expr, if-as-expr).
    bool tail_as_return_ = false;
    TypeRef impl_ret_type_inferred_ = nullptr;
    TypeRef hint_enum_type_ = nullptr;
    TypeRef hint_struct_type_ = nullptr;
    // Set by lower_let when the let binding has an explicit type annotation,
    // so a generic-call rhs with insufficient turbofish can unify the fn's
    // return type against the expected type and fill missing type-args.
    // Closes the gap for `let x: Foo<T> = make_foo();` where T appears only
    // in the return type.
    TypeRef hint_call_return_type_ = nullptr;
    // CP-cm-14: when lowering a closure-arg whose params lack type
    // annotations (`|x| body` rather than `|x: T| body`), check this
    // hint. Set by the call-site path (lower_call / lower_method_call)
    // when the corresponding formal is a `fn(T,...)->R` / `Closure`.
    TypeRef hint_closure_formal_ = nullptr;

    // ── Return reachability ───────────────────────────────────────

    bool stmt_always_returns(hermes::TinyMapView stmt);
    bool block_always_returns(hermes::TinyMapView block);
    // Like *_always_returns, but also treats `break`/`continue` as diverging.
    // Used where we need to know "does the tail expression run?" — e.g. match
    // arm body: `{ ...; break; }` never reaches a tail expression.
    bool stmt_always_diverts(hermes::TinyMapView stmt);
    bool block_always_diverts(hermes::TinyMapView block);

    // ── Lowering helpers ─────────────────────────────────────────

    static bool is_numeric(TypeRef t) noexcept {
        if (!t) return false;
        auto k = TypeRef(t).kind();
        return k == LogosType::Kind::F64 ||
               k == LogosType::Kind::F32 ||
               k == LogosType::Kind::FloatLit ||
               k == LogosType::Kind::TypeVar ||
               // Cfg-slot types are deferred until mono resolves them
               // through the bound HermesStatic. Accept here on the trust
               // that mono will substitute a numeric primitive (or fail
               // there with a precise error). Mirrors TypeVar treatment.
               k == LogosType::Kind::CfgSlotType ||
               is_integer_kind(k);
    }
    static bool is_integer(TypeRef t) noexcept {
        return t && is_integer_kind(TypeRef(t).kind());
    }

    TypeRef field_type_of(std::string_view sname, std::string_view fname,
                                    std::string_view pkg_hint = {});
    TypeRef field_type_of_for_type(TypeRef struct_t, std::string_view fname);
    const SemaStructInfo* find_best_sema_struct_spec(std::string_view base_name,
                                                     const std::vector<TypeRef>& type_args);
    std::string canonical_func_type_name(TypeRef t) const;
    std::string function_signature_key(std::string_view base_name,
                                       const std::vector<TypeRef>& param_types,
                                       bool is_vararg) const;
    std::string function_symbol_name(std::string_view base_name,
                                     const SemaFuncInfo& info) const;
    const SemaFuncInfo* find_func_by_symbol(std::string_view symbol) const;
    const SemaFuncInfo* find_generic_func(std::string_view base_name) const;
    const SemaFuncInfo* find_generic_func(std::string_view base_name,
                                          size_t n_args) const;
    const SemaFuncInfo* find_func_by_base_and_signature(std::string_view base_name,
                                                        const std::vector<TypeRef>& param_types,
                                                        bool is_vararg = false) const;
    std::vector<const SemaFuncInfo*> find_func_candidates(std::string_view base_name) const;
    const SemaFuncInfo* resolve_function_call(std::string_view base_name,
                                              const std::vector<lir::LExprPtr>& arg_exprs,
                                              bool allow_generic = true,
                                              bool exact_only = false) const;

    // ── lower_expr ───────────────────────────────────────────────

    lir::LExprPtr lower_expr(hermes::TinyMapView expr);
    lir::LExprPtr lower_binop(hermes::TinyMapView node);
    lir::LExprPtr lower_unary(hermes::TinyMapView node);
    lir::LExprPtr lower_deref(hermes::TinyMapView node);
    lir::LExprPtr lower_call(hermes::TinyMapView node);
    void unify_types(TypeRef formal, TypeRef actual,
                     logos::compiler::StrMap<TypeRef>& bindings);
    bool infer_type_args(const SemaFuncInfo& fi,
                         const std::vector<lir::LExprPtr>& arg_exprs,
                         std::vector<TypeRef>& out_type_args,
                         const SemaSubst& context = {},
                         size_t param_offset = 0);
    lir::LExprPtr finish_generic_call(std::string_view callee_sv,
                                      const SemaFuncInfo& fi,
                                      std::vector<TypeRef> type_args,
                                      std::vector<lir::LExprPtr> arg_exprs);
    lir::LExprPtr lower_generic_call(hermes::TinyMapView node);
    lir::LExprPtr lower_generic_ref(hermes::TinyMapView node);
    lir::LExprPtr lower_method_call(hermes::TinyMapView node);
    // Per-receiver-shape method-dispatch handlers, factored out of the
    // lower_method_call cascade. Each checks its own receiver-type guard and
    // returns nullopt to fall through to the next shape; `recv` is threaded by
    // reference so a handler's coercions persist across fall-through exactly as
    // in the original inline code.
    std::optional<lir::LExprPtr> try_method_on_tuple(
        hermes::TinyMapView node, lir::LExprPtr& recv, std::string_view method_name);
    std::optional<lir::LExprPtr> try_method_on_dstref(
        hermes::TinyMapView node, lir::LExprPtr& recv, std::string_view method_name);
    std::optional<lir::LExprPtr> try_method_on_dyn(
        hermes::TinyMapView node, lir::LExprPtr& recv, std::string_view method_name);
    // Move-tracking shared by the method-dispatch handlers: mark by-value
    // move-type args / receiver as moved so scope-end auto-Drop doesn't fire
    // on ownership the call has transferred. (Promoted from local lambdas.)
    void track_args_moved(const std::vector<lir::LExprPtr>& args);
    void track_recv_moved(const lir::LExprPtr& recv, TypeRef self_formal);
    // Blanket-impl method dispatch: `impl<T: Bound> Trait for T { fn m … }`.
    // Tries every registered blanket against receiver type `type_name`;
    // on a unique match dispatches via finish_generic_call (moving recv +
    // arg_exprs) and returns the call expr. Returns nullptr when no blanket
    // applies (recv/arg_exprs left intact for the caller to continue). On
    // ambiguous overlap (≥2 viable blankets) emits an error and returns
    // nullptr. Shared by the struct- and primitive-receiver paths so a
    // value blanket (`impl<T> Trait for T`) reaches primitives too.
    lir::LExprPtr try_blanket_method_dispatch(
        lir::LExprPtr& recv,
        std::vector<lir::LExprPtr>& arg_exprs,
        std::string_view method_name,
        const std::string& type_name);
    lir::LExprPtr lower_invoke_expr(hermes::TinyMapView node);
    lir::LExprPtr lower_field_read(hermes::TinyMapView node);
    lir::LExprPtr lower_struct_lit(hermes::TinyMapView node);
    lir::LExprPtr lower_index_read(hermes::TinyMapView node);
    lir::LExprPtr lower_arr_lit(hermes::TinyMapView node);
    lir::LExprPtr lower_arr_fill_lit(hermes::TinyMapView node);
    lir::LExprPtr lower_list_comp(hermes::TinyMapView node);
    lir::LExprPtr lower_map_comp(hermes::TinyMapView node);
    lir::LExprPtr lower_hermes_list_comp(hermes::TinyMapView node);
    lir::LExprPtr lower_hermes_map_comp(hermes::TinyMapView node);
    lir::LExprPtr coerce_to_hermes_anyval(lir::LExprPtr val,
                                          const std::string& ctr_var,
                                          TypeRef ctr_t,
                                          std::string_view context);
    lir::LExprPtr lower_hermes_lit(hermes::TinyMapView node);
    lir::LExprPtr lower_hermes_blob(hermes::TinyMapView node);
    lir::LExprPtr lower_quote_item(hermes::TinyMapView node);
    lir::LExprPtr lower_quote_expr(hermes::TinyMapView node);
    lir::LExprPtr lower_quote_ty(hermes::TinyMapView node);

    // Capture context: non-null while lowering a hermes literal that has $-captures.
    // lower_hermes_val populates it as it encounters HERMES_CAP_IDENT/EXPR nodes.
    struct HermesCapCtx {
        std::vector<lir::LExprPtr>               exprs;       // unique capture expressions
        std::vector<TypeRef>             types;       // corresponding types
        uint32_t                                  next_slot = 0; // next param_index
        // dedup: symbol binding name → value_index (for pure EIdent captures)
        logos::compiler::StrMap<uint32_t> ident_dedup;
    };
    HermesCapCtx* hermes_cap_ctx_ = nullptr;

    lir::HermesValPtr lower_hermes_val(hermes::TinyMapView node);
    lir::LExprPtr lower_enum_lit(hermes::TinyMapView node);
    lir::LExprPtr lower_enum_lit_data(hermes::TinyMapView node);
    lir::LExprPtr lower_enum_lit_data_from_static(
            hermes::TinyMapView node, std::string_view ename, std::string_view vname);
    lir::LExprPtr lower_static_call(hermes::TinyMapView node);
    lir::LExprPtr lower_metacall   (hermes::TinyMapView node);
    // Function-style macro `name!(args)` / `name![args]` (slice 1 of
    // fn-macros). Resolves CALLEE against #[fn_macro] fns; ARGs are
    // captured as ExprBlobs and passed through the metacall JIT thunk.
    lir::LExprPtr lower_fn_macro_call(hermes::TinyMapView node);
    // Bare `{ stmts; tail_expr }` as expression — lowers a BLOCK AST node
    // as an expr whose value is the tail expression (or void if absent).
    lir::LExprPtr lower_block_expr(hermes::TinyMapView node);
    // Item-position metacall (MC1.1). Synthesises a void thunk that wraps
    // the inner callee — `let __b = call(); logos_emit_item_blob_subst(&__b);`
    // — and registers a MetacallSite with ret_tag = ItemBlob. Caller
    // (sema.cpp item dispatch) supplies the AST offset of the METACALL_ITEM
    // node so the driver can mark it consumed (CODE → METACALL_ITEM_DONE).
    void          lower_metacall_item(hermes::TinyMapView node, lir::LProgram& prog);
    // Slice 6 of fn-macros — `name!{...}` at module item position.
    // Mirrors lower_metacall_item but resolves callee against
    // #[fn_macro] markers and routes args through the per-site
    // arg-blob shim (slice 3 raw-capture path).
    void          lower_fn_macro_call_item(hermes::TinyMapView node,
                                           lir::LProgram& prog);

public:
    // ── AST → Logos source pretty-printer (sema_render.cpp) ──────────
    // Used by `metacall (<expr>)` and `metacall { ... }` to splice arbitrary
    // expressions/blocks into the synthesised JIT thunk source. Stage 2
    // (item-position) is also reused by `--dump-metaprog` to emit
    // metafn-generated AST documents as readable Logos source. Pure walks;
    // do not modify sema state. Public so dump-driver code outside the
    // class can render arbitrary sub-trees.
    std::string render_expr_src(hermes::TinyMapView node);
    std::string render_stmt_src(hermes::TinyMapView node);
    std::string render_block_src(hermes::TinyMapView node);
    std::string render_type_src(hermes::TinyMapView node);
    std::string render_pat_src(hermes::TinyMapView node);
    std::string render_item_src(hermes::TinyMapView node);
    std::string render_module_src(hermes::TinyMapView node);

    // For the --dump-metaprog driver: temporarily point this checker at a
    // foreign Hermes doc so render_*_src can walk it without running full
    // sema. Caller is responsible for keeping the holder alive across
    // render calls. When `dump_syntactic_types_` is set, render_type_src
    // bypasses resolve_type and walks TYPE_REF / GENERIC_INST / PTR_TYPE /
    // etc. structurally — necessary for fresh checkers that have empty
    // type pools (no user struct/alias is registered).
    void set_holder_for_render(hermes::MemHolder* h) { holder_ = h; }
    void set_render_syntactic(bool on) { dump_syntactic_types_ = on; }
private:
    // Item-rendering sub-helpers (sema_render.cpp Stage 2).
    std::string render_path_parts_(hermes::TinyMapView node);
    std::string render_type_param_src_(hermes::TinyMapView node);
    std::string render_type_param_list_(hermes::TinyMapView node);
    std::string render_param_src_(hermes::TinyMapView node);
    std::string render_param_list_(hermes::TinyMapView node);
    std::string render_field_def_src_(hermes::TinyMapView node);
    std::string render_variant_def_src_(hermes::TinyMapView node);
    // Syntactic type walk used when dump_syntactic_types_ is on (fresh
    // checker with empty type pool — resolve_type would fail).
    std::string render_type_src_syntactic_(hermes::TinyMapView node);
    // Inner Hermes literal renderer — used recursively from
    // render_expr_src for HERMES_MAP entries / HERMES_ARRAY elements.
    // Omits the `@` prefix on scalars (grammar's hermes_val production
    // doesn't accept `@4` / `@"x"` at this position; outer hermes_lit
    // does).
    std::string render_hermes_val_inner_(hermes::TinyMapView node);
    bool dump_syntactic_types_ = false;

    // Render a CTFE-evaluated value as a Logos source literal. Used by both
    // expression-position and item-position metacall arg-splicing to embed
    // CTFE results into the synthesised JIT thunk source.
    static std::string render_ctfe_lit(const logos::compiler::ctfe::CtfeValue& v);

    // Build a typed pass-through LExpr for a `metacall { … }` site.
    //
    // The block's tail-expr type drives sema, but its lowered LIR cannot be
    // returned as the metacall's pass-through — that LIR references LET/FOR
    // bindings introduced *inside* the block, which are out of scope in the
    // surrounding fn. The metacall splice driver replaces the AST node with
    // a real literal (post-JIT) before final mlir-gen, so this expression is
    // throwaway. We still need it to be syntactically valid because the
    // meta-JIT's own mlir-gen pass runs over the user fn (which is then
    // internalised and DCE'd, but mlir-gen happens first) and would choke on
    // dangling var refs.
    //
    // Concretely: a typed-zero literal of the block's type. If a future
    // Phase-2 transform needs a more semantically precise marker, this is
    // the call site to upgrade to a dedicated LIR opcode (one helper, one
    // mlir-gen case).
    lir::LExprPtr make_metacall_placeholder_expr(TypeRef ty);
    lir::LExprPtr lower_if_expr(hermes::TinyMapView node);
    lir::LExprPtr lower_closure_expr(hermes::TinyMapView node);

    // ── lower_stmt and friends ───────────────────────────────────

    lir::LStmt lower_stmt(hermes::TinyMapView stmt);
    lir::LBlock lower_block(hermes::TinyMapView block);
    lir::LStmt lower_let_destruct(hermes::TinyMapView node);
    lir::LStmt lower_let_pat(hermes::TinyMapView node);
    lir::LStmt lower_let(hermes::TinyMapView node);
    lir::LStmt lower_let_else(hermes::TinyMapView node);
    lir::LStmt lower_nested_fn(hermes::TinyMapView node);
    lir::LStmt lower_compound_assign(hermes::TinyMapView node);
    lir::LStmt lower_assign(hermes::TinyMapView node);
    lir::LStmt lower_return(hermes::TinyMapView node);
    lir::Pattern build_pattern(hermes::TinyMapView pnode, TypeRef scrut_type);
    // Internal: build_pattern's body without eager mirror emit. Recurses via
    // build_pattern (so sub-patterns get their own eager emit).
    lir::Pattern build_pattern_impl(hermes::TinyMapView pnode, TypeRef scrut_type);
    // Helper for inline PatWild construction with eager mirror emit.
    lir::Pattern make_pat_wild(std::string_view name);
    // If pnode is a Hermes scalar pattern (PAT_HERMES_NULL/BOOL/INT), returns a
    // bool-typed guard call that evaluates the pattern against `scrut_var`
    // (which must be an AnyVal).  Returns nullptr otherwise.
    struct HermesPatBinding {
        std::string name;        // user-visible binding name in arm body
        std::string av_var;      // AnyVal local holding the value
    };
    lir::LExprPtr build_hermes_pat_guard(hermes::TinyMapView pnode,
                                         const std::string& scrut_var,
                                         TypeRef scrut_type,
                                         const std::string& base_var,
                                         std::vector<lir::LStmt>& out_stmts,
                                         std::vector<HermesPatBinding>& out_bindings);
    // Returns the "inner" (ref-stripped) view type if `t` is Hermes,
    // HermesView<'_>, or HermesStatic (possibly behind &/&mut). nullptr otherwise.
    TypeRef hermes_view_inner(TypeRef t) const {
        TypeRef tr(t);
        if (!tr) return nullptr;
        TypeRef inner = tr;
        if (tr.kind() == LogosType::Kind::Ref ||
            tr.kind() == LogosType::Kind::MutRef)
            inner = tr.pointee();
        if (!inner) return nullptr;
        if ((inner.kind() == LogosType::Kind::Struct ||
             inner.kind() == LogosType::Kind::ZonedStruct) &&
            (inner.struct_name() == "Hermes" ||
             inner.struct_name() == "HermesView" ||
             inner.struct_name() == "HermesStatic"))
            return inner;
        return nullptr;
    }
    // True while lowering match arms where Hermes scalar patterns are
    // explicitly handled by the caller (desugared to guard). Outside this
    // context, PAT_HERMES_* in build_pattern is a diagnostic.
    bool in_match_hermes_ctx_ = false;
    // P4-pm-02: side channel for build_pattern to register nested
    // sub-pats that need irrefutable destructure in the arm-body prologue
    // (e.g. `Some(A { foo: _x })` → synth `__pat_pld_*` binding for the
    // payload slot, then `let A { foo: _x } = __pat_pld_*;` at body
    // start). Caller wires this before build_pattern and consumes the
    // entries when building the arm body.
    struct NestedPatSub {
        std::string                synth_name;
        hermes::TinyMapView        sub_pat_node;
    };
    std::vector<NestedPatSub>* current_pat_nested_subs_ = nullptr;
    // P4-pm-12: names from `mut x` patterns (`match scrut { mut z =>
    // … }`). PatWild's LIR mirror doesn't carry the mut flag, so
    // build_pattern_impl appends to this side-channel and
    // bind_pattern_ref re-defines the binding as mutable.
    logos::compiler::StrSet* current_pat_mut_names_ = nullptr;
    // P4-pm-01 (refutable inner): synthesized `binding == value`
    // predicates collected from refutable sub-patterns inside variant
    // payloads (`E::Foo { f: 1 }`, `Option::Some(2)`). Each entry is a
    // ready-to-AND-combine boolean expression that uses the synth
    // binding name `build_pattern` chose for that payload slot. Match
    // arm builder consumes the list, AND-combines into the arm's guard.
    std::vector<lir::LExprPtr>* current_pat_refutable_guards_ = nullptr;
    void bind_pattern(const lir::Pattern& pat,
                      TypeRef scrut_type = nullptr);
    void bind_pattern_ref(lir_view::PatRef pr, TypeRef scrut_type);
    lir::LStmt lower_if(hermes::TinyMapView node);
    lir::LStmt lower_while(hermes::TinyMapView node);
    lir::LStmt lower_for(hermes::TinyMapView node);
    lir::LStmt lower_for_each(hermes::TinyMapView node);
    lir::LStmt lower_loop(hermes::TinyMapView node);
    lir::LStmt lower_field_write(hermes::TinyMapView node);
    lir::LStmt lower_chain_field_write(hermes::TinyMapView node);
    lir::LStmt lower_chain_field_compound_assign(hermes::TinyMapView node);
    lir::LStmt lower_field_compound_assign(hermes::TinyMapView node);
    lir::LStmt lower_tuple_field_write(hermes::TinyMapView node);
    lir::LStmt lower_tuple_field_compound_assign(hermes::TinyMapView node);
    lir::LStmt lower_deref_field_write(hermes::TinyMapView node);
    lir::LStmt lower_deref_field_compound_assign(hermes::TinyMapView node);
    lir::LStmt lower_index_write(hermes::TinyMapView node);
    lir::LStmt lower_index_compound_assign(hermes::TinyMapView node);
    lir::LStmt lower_field_index_write(hermes::TinyMapView node);
    lir::LStmt lower_field_index_compound_assign(hermes::TinyMapView node);
    lir::LStmt lower_match(hermes::TinyMapView node);
    lir::LExprPtr lower_match_expr(hermes::TinyMapView node);

    // ── lower_fn and declaration lowering ───────────────────────

    lir::LFunction lower_fn(hermes::TinyMapView node, std::string_view struct_ctx = {});
    lir::LStructDef lower_struct_def(hermes::TinyMapView node);
    lir::LEnumDef lower_enum_def(hermes::TinyMapView node);
    lir::LConst lower_const_def(hermes::TinyMapView node);
    lir::LTypeAlias lower_type_alias_def(hermes::TinyMapView node);
    lir::LTraitDef lower_trait_def(hermes::TinyMapView node);
    void lower_impl_block(hermes::TinyMapView node, lir::LProgram& prog);
    void lower_program(const std::vector<hermes::Hermes>& asts, lir::LProgram& prog);
    void lower_module_items(hermes::TinyMapView mod, lir::LProgram& prog);
    lir::LAnnotationValue parse_annot_literal(hermes::TinyMapView v);
    std::optional<lir::LAnnotationInstance>
        build_annotation_instance(hermes::TinyMapView ann,
                                  std::string_view ann_name,
                                  std::string_view ann_pkg,
                                  const SemaStructInfo& ann_info);

    // ── Member overloads of LExpr*-taking helpers (B.5 sites 1-3) ───────
    // These wrap the free ExprRef-based helpers so the variant peek lives
    // here (via expr_ref_of), letting B.6 drop the LExpr-variant payload
    // without touching ~100 call sites in sema_stmt/sema_expr.
    std::optional<int64_t> get_intlit_value(const lir::LExpr* e) const noexcept {
        if (!e) return std::nullopt;
        return logos::compiler::get_intlit_value(expr_ref_of(*e));
    }
    std::optional<int64_t> get_intlit_value(lir_view::ExprRef e) const noexcept {
        return logos::compiler::get_intlit_value(e);
    }
    void widen_int_expr(lir::LExprPtr& e, TypeRef target, LirBuilder b) {
        if (!e || !target || !e->type) return;
        auto ek = TypeRef(e->type).kind();
        auto tk = TypeRef(target).kind();
        if (ek == tk) return;
        bool ok = can_widen_int(ek, tk);
        if (!ok && is_integer_kind(TypeRef(e->type).kind()) && is_integer_kind(TypeRef(target).kind())) {
            if (auto v = get_intlit_value(e))
                if (intlit_fits(*v, TypeRef(target).kind()))
                    ok = true;
        }
        if (!ok) return;
        e = b.cast(std::move(e), target);
    }
    bool arg_compatible_for_dispatch(const lir::LExpr* arg,
                                     TypeRef at,
                                     TypeRef pt) const noexcept {
        if (types_equal(at, pt)) return true;
        if (types_compatible(at, pt)) return true;
        if (arg && at && pt && is_integer_kind(TypeRef(at).kind()) && is_integer_kind(TypeRef(pt).kind()))
            if (auto v = get_intlit_value(arg))
                if (intlit_fits(*v, TypeRef(pt).kind()))
                    return true;
        return false;
    }
};

// ── File-scope helpers used across sema_*.cpp TUs ─────────────────────────
// These are declared/defined inline so each TU gets a copy without ODR
// violations.  They were previously file-static in the anonymous namespace.

inline bool is_integer_kind(LogosType::Kind k) noexcept {
    return k == LogosType::Kind::I32   || k == LogosType::Kind::I64 ||
           k == LogosType::Kind::U8    || k == LogosType::Kind::I8  ||
           k == LogosType::Kind::I16   || k == LogosType::Kind::U16 ||
           k == LogosType::Kind::U32   || k == LogosType::Kind::U64 ||
           k == LogosType::Kind::I24   || k == LogosType::Kind::U24 ||
           k == LogosType::Kind::I56   || k == LogosType::Kind::U56 ||
           k == LogosType::Kind::I128  || k == LogosType::Kind::U128 ||
           k == LogosType::Kind::Usize || k == LogosType::Kind::Isize ||
           k == LogosType::Kind::IntLit || k == LogosType::Kind::Enum;
}

// Bit-width and signedness of a concrete integer kind.  Returns {0,false}
// for non-concrete-integer kinds (IntLit, Enum, non-integers).
inline std::pair<unsigned, bool> int_rank(LogosType::Kind k) noexcept {
    using K = LogosType::Kind;
    switch (k) {
    case K::Usize: return {static_cast<unsigned>(g_target_pointer_bits), false};
    case K::Isize: return {static_cast<unsigned>(g_target_pointer_bits), true};
    case K::I8:   return {8, true};
    case K::U8:   return {8, false};
    case K::I16:  return {16, true};
    case K::U16:  return {16, false};
    case K::I24:  return {24, true};
    case K::U24:  return {24, false};
    case K::I32:  return {32, true};
    case K::U32:  return {32, false};
    case K::I56:  return {56, true};
    case K::U56:  return {56, false};
    case K::I64:  return {64, true};
    case K::U64:  return {64, false};
    case K::I128: return {128, true};
    case K::U128: return {128, false};
    default:      return {0, false};
    }
}

// True iff every value of `from` is representable in `to` — i.e. a safe
// implicit widening:
//   signed   → signed   : to_width ≥ from_width
//   unsigned → unsigned : to_width ≥ from_width
//   unsigned → signed   : to_width > from_width (need one extra bit)
//   signed   → unsigned : never (loses negative values)
inline bool can_widen_int(LogosType::Kind from, LogosType::Kind to) noexcept {
    // Usize/Isize are distinct types — no implicit conversion to/from
    // fixed-width integers (matches Rust). User must `as` explicitly.
    using K = LogosType::Kind;
    bool from_psize = (from == K::Usize || from == K::Isize);
    bool to_psize   = (to   == K::Usize || to   == K::Isize);
    if (from_psize != to_psize) return false;
    auto [fw, fs] = int_rank(from);
    auto [tw, ts] = int_rank(to);
    if (fw == 0 || tw == 0) return false;
    if (fs == ts) return tw >= fw;
    if (!fs && ts) return tw > fw;
    return false;
}

inline TypeRef unify_int(TypeRef a, TypeRef b) noexcept {
    if (TypeRef(a).kind() == LogosType::Kind::IntLit) return b;
    if (TypeRef(b).kind() == LogosType::Kind::IntLit) return a;
    // Widen narrower to wider when safe: caller has already verified compat.
    auto ak = TypeRef(a).kind();
    auto bk = TypeRef(b).kind();
    if (can_widen_int(ak, bk)) return b;
    if (can_widen_int(bk, ak)) return a;
    return a;
}

// widen_int_expr / arg_compatible_for_dispatch are members of SemaChecker
// (see class body above) — they need expr_ref_of to migrate the
// get_intlit_value(LExpr*) variant peek.

// Like unify_int but also promotes FloatLit to a concrete float type (F32/F64).
// Use in contexts where both integers and floats need unification.
inline TypeRef unify_numeric(TypeRef a, TypeRef b) noexcept {
    if (TypeRef(a).kind() == LogosType::Kind::IntLit || TypeRef(a).kind() == LogosType::Kind::FloatLit) return b;
    return a;
}

// ExprRef overload — view-based traversal for callers that already hold
// an ExprRef (e.g. element iteration via EArrLitView/ETupleLitView). Walks
// the same shape (BlockExpr.result → Unary/-/LitInt) but reads from the
// Hermes mirror so it composes with view-migrated read sites without an
// LExpr*/ExprRef impedance mismatch.
inline std::optional<int64_t> get_intlit_value(lir_view::ExprRef e) noexcept {
    if (!e) return std::nullopt;
    using C = lir_schema::expr::Code;
    if (e.kind() == C::BlockExpr) {
        auto inner = lir_view::EBlockExprView{e}.result();
        return get_intlit_value(inner);
    }
    if (e.kind() == C::Unary) {
        auto u = lir_view::EUnaryView{e};
        if (u.op() == "-") {
            auto inner = get_intlit_value(u.operand());
            if (inner) return -(*inner);
        }
        return std::nullopt;
    }
    if (e.kind() == C::LitInt)
        return lir_view::ELitIntView{e}.value();
    return std::nullopt;
}

inline bool intlit_fits(int64_t v, LogosType::Kind k) noexcept {
    switch (k) {
    case LogosType::Kind::I8:  return v >= -128 && v <= 127;
    case LogosType::Kind::U8:  return v >= 0 && v <= 255;
    case LogosType::Kind::I16:  return v >= -32768 && v <= 32767;
    case LogosType::Kind::U16:  return v >= 0 && v <= 65535;
    case LogosType::Kind::I32:  return v >= (int64_t)INT32_MIN && v <= (int64_t)INT32_MAX;
    case LogosType::Kind::U32:  return v >= 0 && (uint64_t)v <= (uint64_t)UINT32_MAX;
    case LogosType::Kind::I24:  return v >= -(INT64_C(1) << 23) && v <= ((INT64_C(1) << 23) - 1);
    case LogosType::Kind::I56:  return v >= -(INT64_C(1) << 55) && v <= ((INT64_C(1) << 55) - 1);
    case LogosType::Kind::U24:  return v >= 0 && (uint64_t)v <= ((UINT64_C(1) << 24) - 1);
    case LogosType::Kind::U56:  return v >= 0 && (uint64_t)v <= ((UINT64_C(1) << 56) - 1);
    case LogosType::Kind::I128: return true; // all int64_t values fit in i128
    case LogosType::Kind::U128: return v >= 0; // non-negative int64_t values fit
    case LogosType::Kind::Usize:
        return g_target_pointer_bits == 64
            ? (v >= 0)
            : (v >= 0 && (uint64_t)v <= (uint64_t)UINT32_MAX);
    case LogosType::Kind::Isize:
        return g_target_pointer_bits == 64
            ? true
            : (v >= (int64_t)INT32_MIN && v <= (int64_t)INT32_MAX);
    default: return true; // i64, u64: all int64_t values fit
    }
}

// True if the literal's magnitude exceeds the representable range for i64
// (or u64 in the unsigned case). Used by LIT_INT lowering to reject
// silently-truncating literals (closes B-ex-07, B-he-04, B-lx-04).
//
// Strategy: accumulate via __builtin_*_overflow.  If a literal carries an
// explicit type-suffix (`123u8`), the per-type bound check is layered on
// top of this raw overflow check at the call site.
inline bool parse_int_literal_overflows(std::string_view sv) noexcept {
    bool negative = !sv.empty() && sv[0] == '-';
    if (negative) sv = sv.substr(1);
    int base = 10;
    if (sv.size() >= 2 && sv[0] == '0') {
        if (sv[1] == 'x' || sv[1] == 'X') { base = 16; sv = sv.substr(2); }
        else if (sv[1] == 'b' || sv[1] == 'B') { base = 2;  sv = sv.substr(2); }
        else if (sv[1] == 'o' || sv[1] == 'O') { base = 8;  sv = sv.substr(2); }
    }
    uint64_t result = 0;
    for (char c : sv) {
        if (c == '_') continue;
        int d = -1;
        if (c >= '0' && c <= '9') d = c - '0';
        else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
        if (d < 0 || d >= base) break;  // suffix start (e.g. 'u', 'i', 'f')
        if (__builtin_mul_overflow(result, (uint64_t)base, &result)) return true;
        if (__builtin_add_overflow(result, (uint64_t)d, &result)) return true;
    }
    if (negative) {
        // -(2^63) = INT64_MIN is representable; anything past that overflows i64.
        if (result > (uint64_t)INT64_MAX + 1) return true;
    }
    return false;
}

inline int64_t parse_int_literal(std::string_view sv) noexcept {
    bool negative = !sv.empty() && sv[0] == '-';
    if (negative) sv = sv.substr(1);
    // NG3: use uint64_t to avoid signed overflow during accumulation.
    // Parsing "9223372036854775808" (INT64_MAX+1) into int64_t would be UB.
    uint64_t result = 0;
    if (sv.size() >= 2 && sv[0] == '0') {
        if (sv[1] == 'x' || sv[1] == 'X') {
            for (char c : sv.substr(2)) {
                if (c == '_') continue;
                if (c >= '0' && c <= '9')      result = result * 16 + (uint64_t)(c - '0');
                else if (c >= 'a' && c <= 'f') result = result * 16 + (uint64_t)(c - 'a' + 10);
                else if (c >= 'A' && c <= 'F') result = result * 16 + (uint64_t)(c - 'A' + 10);
                else break;
            }
        } else if (sv[1] == 'b' || sv[1] == 'B') {
            for (char c : sv.substr(2)) {
                if (c == '_') continue;
                if (c == '0' || c == '1') result = result * 2 + (uint64_t)(c - '0');
                else break;
            }
        } else if (sv[1] == 'o' || sv[1] == 'O') {
            for (char ch : sv.substr(2)) {
                if (ch == '_') continue;
                if (ch >= '0' && ch <= '7') result = result * 8 + (uint64_t)(ch - '0');
                else break;
            }
        } else {
            for (char c : sv) {
                if (c == '_') continue;
                if (c >= '0' && c <= '9') result = result * 10 + (uint64_t)(c - '0');
                else break;
            }
        }
    } else {
        for (char c : sv) {
            if (c == '_') continue;
            if (c >= '0' && c <= '9') result = result * 10 + (uint64_t)(c - '0');
            else break;
        }
    }
    if (negative) {
        // Special case: -(2^63) = INT64_MIN, which overflows if cast to int64 first.
        if (result == (uint64_t)INT64_MAX + 1) return INT64_MIN;
        if (result > (uint64_t)INT64_MAX)      return INT64_MIN;  // saturate
        return -(int64_t)result;
    }
    // Raw bit-cast: preserve full 64-bit pattern for u64 literals whose value
    // exceeds INT64_MAX (e.g. FNV offset basis 0xcbf29ce484222325).
    return (int64_t)result;
}

template <class Pred>
inline bool valid_digit_groups(std::string_view sv, Pred pred) noexcept {
    if (sv.empty()) return false;
    bool prev_us = false;
    for (size_t i = 0; i < sv.size(); ++i) {
        char c = sv[i];
        if (c == '_') {
            if (i == 0 || i + 1 == sv.size() || prev_us) return false;
            prev_us = true;
            continue;
        }
        if (!pred(c)) return false;
        prev_us = false;
    }
    return true;
}

inline bool valid_int_literal_format(std::string_view sv) noexcept {
    if (sv.empty()) return false;
    size_t start = (sv.front() == '-') ? 1 : 0;
    if (start >= sv.size()) return false;

    static constexpr std::string_view suffixes[] = {
        "usize", "isize",
        "i128", "i64", "i56", "i32", "i24", "i16", "i8",
        "u128", "u64", "u56", "u32", "u24", "u16", "u8"
    };
    size_t suffix_len = 0;
    for (auto sfx : suffixes) {
        if (sv.size() >= start + sfx.size() && sv.substr(sv.size() - sfx.size()) == sfx) {
            suffix_len = sfx.size();
            break;
        }
    }

    size_t body_end = sv.size() - suffix_len;
    size_t body_start = start;
    auto is_dec = [](char c) { return c >= '0' && c <= '9'; };
    auto is_hex = [](char c) {
        return (c >= '0' && c <= '9') ||
               (c >= 'a' && c <= 'f') ||
               (c >= 'A' && c <= 'F');
    };
    auto is_bin = [](char c) { return c == '0' || c == '1'; };
    auto is_oct = [](char c) { return c >= '0' && c <= '7'; };

    if (body_end >= body_start + 2 && sv[body_start] == '0') {
        char p = sv[body_start + 1];
        if (p == 'x' || p == 'X') {
            body_start += 2;
            return valid_digit_groups(sv.substr(body_start, body_end - body_start), is_hex);
        }
        if (p == 'b' || p == 'B') {
            body_start += 2;
            return valid_digit_groups(sv.substr(body_start, body_end - body_start), is_bin);
        }
        if (p == 'o' || p == 'O') {
            body_start += 2;
            return valid_digit_groups(sv.substr(body_start, body_end - body_start), is_oct);
        }
    }
    return valid_digit_groups(sv.substr(body_start, body_end - body_start), is_dec);
}

inline bool valid_float_literal_format(std::string_view sv) noexcept {
    if (sv.empty()) return false;
    size_t start = (sv.front() == '-') ? 1 : 0;
    if (start >= sv.size()) return false;

    size_t suffix_len = 0;
    if (sv.size() >= start + 3 &&
        (sv.substr(sv.size() - 3) == "f32" || sv.substr(sv.size() - 3) == "f64"))
        suffix_len = 3;

    std::string_view main = sv.substr(start, sv.size() - start - suffix_len);
    size_t dot = main.find('.');
    if (dot == std::string_view::npos) return false;

    auto is_dec = [](char c) { return c >= '0' && c <= '9'; };
    if (!valid_digit_groups(main.substr(0, dot), is_dec)) return false;

    size_t exp = main.find_first_of("eE", dot + 1);
    std::string_view frac = (exp == std::string_view::npos)
        ? main.substr(dot + 1)
        : main.substr(dot + 1, exp - dot - 1);
    if (!valid_digit_groups(frac, is_dec)) return false;

    if (exp != std::string_view::npos) {
        size_t exp_start = exp + 1;
        if (exp_start < main.size() && (main[exp_start] == '+' || main[exp_start] == '-'))
            ++exp_start;
        if (!valid_digit_groups(main.substr(exp_start), is_dec)) return false;
    }
    return true;
}

// Returns the LogosType::Kind for a numeric suffix like "i32", "u64", "f32".
// Returns Kind::Error if sv has no recognised suffix.
inline LogosType::Kind int_suffix_kind(std::string_view sv) noexcept {
    // Strip leading sign and numeric digits / underscores to find the suffix.
    size_t i = 0;
    if (i < sv.size() && sv[i] == '-') ++i;
    // Skip hex/bin/oct prefix
    if (i + 1 < sv.size() && sv[i] == '0' &&
        (sv[i+1] == 'x' || sv[i+1] == 'X' ||
         sv[i+1] == 'b' || sv[i+1] == 'B' ||
         sv[i+1] == 'o' || sv[i+1] == 'O')) i += 2;
    // Skip digit body
    while (i < sv.size() && (std::isxdigit((unsigned char)sv[i]) || sv[i] == '_')) ++i;
    auto suf = sv.substr(i);
    if (suf == "i8")    return LogosType::Kind::I8;
    if (suf == "i16")   return LogosType::Kind::I16;
    if (suf == "i32")   return LogosType::Kind::I32;
    if (suf == "i24")   return LogosType::Kind::I24;
    if (suf == "i56")   return LogosType::Kind::I56;
    if (suf == "i64")   return LogosType::Kind::I64;
    if (suf == "i128")  return LogosType::Kind::I128;
    if (suf == "u8")    return LogosType::Kind::U8;
    if (suf == "u16")   return LogosType::Kind::U16;
    if (suf == "u32")   return LogosType::Kind::U32;
    if (suf == "u24")   return LogosType::Kind::U24;
    if (suf == "u56")   return LogosType::Kind::U56;
    if (suf == "u64")   return LogosType::Kind::U64;
    if (suf == "u128")  return LogosType::Kind::U128;
    if (suf == "usize") return LogosType::Kind::Usize;
    if (suf == "isize") return LogosType::Kind::Isize;
    return LogosType::Kind::Error;
}

inline LogosType::Kind float_suffix_kind(std::string_view sv) noexcept {
    if (sv.size() >= 3 && sv.substr(sv.size() - 3) == "f32") return LogosType::Kind::F32;
    if (sv.size() >= 3 && sv.substr(sv.size() - 3) == "f64") return LogosType::Kind::F64;
    return LogosType::Kind::Error;
}

// M5 step 3b: opaque snapshot of every collect()-mutated symbol table.
// Defined after SemaChecker so the field types (SemaStructInfo etc.) are
// in scope. SemaChecker is friended so we can spell them; the names
// themselves remain private to outside callers.
class SemaCheckerSnapshot {
public:
    // Symbol tables — direct move targets of SemaChecker members.
    StrMap<SemaChecker::SemaStructInfo>   structs;
    StrMap<SemaChecker::SemaStructInfo>   datatypes;
    StrMap<SemaChecker::SemaStructInfo>   struct_specs_sema;
    StrMap<uint64_t>                       explicit_type_codes;
    StrMap<SemaChecker::SemaEnumInfo>     enums;
    StrMap<SemaChecker::SemaFuncInfo>     funcs;
    StrMap<std::vector<std::string>>       func_overloads;
    StrMap<SemaChecker::SemaFuncInfo>     generic_funcs;
    StrMap<std::vector<std::string>>       generic_overloads;
    StrMap<SemaChecker::TypeAliasEntry>   type_aliases;
    StrMap<TypeRef>                        module_consts;
    StrMap<hermes::TinyMapView>            module_const_values;
    StrMap<SemaChecker::GenericConstEntry> generic_consts;
    StrMap<SemaChecker::SemaTraitInfo>    traits;
    StrMap<SemaChecker::SemaImplInfo>     impls;
    StrSet                                 coherence_keys;
    StrMap<SemaChecker::AssocTypeEntry>   assoc_type_impls;
    StrMap<SemaChecker::AssocConstEntry>  assoc_const_impls;
    std::vector<SemaChecker::BlanketImpl>  blanket_impls;
    std::vector<lir::LProgram::MetaprogHandler> metaprog_handlers;
    std::vector<lir::LProgram::MetaprogTarget>  metaprog_targets;
    std::set<std::string>                   copy_types;
    StrMap<std::vector<std::string>>       pkg_reexports;
    // Holders whose collect_module() contribution is already in these
    // tables. SemaChecker's per-AST loops skip walks for holders in
    // this set — re-walking would be deduped but the walk itself is
    // the cost we want to skip.
    std::unordered_set<const hermes::MemHolder*> collected_holders;

    // Cached HStaticLit registry contributions. Populated by sema when
    // LIT_HSTATIC nodes are encountered at type-arg position; held on
    // LProgram::hstatic_registry_ which is per-call. Cached here so
    // subsequent sema_lower calls can pre-seed the fresh prog's
    // registry instead of re-walking binary ASTs to rediscover them.
    std::unordered_map<uint64_t, lir::LExpr*> hstatic_registry;

    // M5 step 5: synth tuple-struct field-name intern pool. Owns the
    // std::string objects whose string_views are stored on
    // SemaFieldInfo::name. Must outlive the cached SemaStructInfo
    // entries or those views dangle and trip "duplicate field" errors
    // on the next call.
    std::vector<std::unique_ptr<std::string>> synth_field_name_pool;
};

} // namespace logos::compiler
