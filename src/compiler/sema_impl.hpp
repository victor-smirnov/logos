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
#include <logos/compiler/probe.hpp>
#include <logos/compiler/subtype.hpp>
#include <logos/compiler/unit_graph.hpp>   // THE unit-key rule (§1.1)
#include "layout_law.hpp"
#include "mangled_name.hpp"
#include "ctfe.hpp"   // T2-14: ctfe_eval_const signature (CtfeValue/CtfeError)
#include <logos/compiler/sha256.hpp>
#include <logos/compiler/str_map.hpp>
#include <logos/writ/compat.hpp>
#include <logos/writ/compat.hpp>
#include <logos/writ/compat.hpp>
#include <logos/writ/compat.hpp>
#include <logos/writ/compat.hpp>

#include <cctype>
#include <cstdint>
#include <cstdio>
#include <format>
#include <functional>
#include <optional>
#include <set>
#include <string>
#include <cstdlib>   // strtod — parse_float_literal
#include <cerrno>
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
// ⚠ ONE declaration, in `sema.hpp` — this re-declaration used to shadow it with
// a narrower signature, which is how a defaulted second parameter turns into an
// "ambiguous call" at every use site rather than into the default.
std::string type_str(TypeRef t, bool source_form);

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
    using writ::TinyMapView;
    using writ::ArrayView;
    using writ::StringView;
    using writ::AnyVal;
    using writ::MemHolder;
}

// M5 step 3b: snapshot of all collect()-mutated symbol tables + the
// "already collected" holder set. Held by SemaCacheImpl across the
// 5+ sema_lower invocations per compile session. Defined in
// sema_impl.hpp where SemaChecker's private nested types are visible;
// friended so the fields below can spell `SemaChecker::SemaStructInfo`
// etc. directly. PIMPL-style: SemaChecker exposes only take/install
// methods, never the field types.
class SemaCheckerSnapshot;

// Stage E: the LProgram::MetaprogHandler / MetaprogTarget structs were deleted
// (those tables now live as Writ mirror views — MetaprogHandlerView /
// MetaprogTargetView). Sema still needs transient C++ staging buffers while
// collecting handlers/targets (no LProgram host handy at collection time); the
// views are direct-built at the move-into-prog point. These PODs are
// sema-internal scaffolding, not part of LProgram.
// UnitGraph §1.4: def_ast_idx/def_source_file record WHERE the hook is defined.
// Both are in hand at the collect site; without them a Trigger edge has a
// consumer and no provider.
struct MetaprogHandlerStage { std::string trigger; std::string hook_fn;
                              int64_t def_ast_idx = -1; std::string def_source_file; };
struct MetaprogTargetStage  { size_t ast_idx; uint32_t item_offset; std::string trigger; };

class SemaChecker {
public:
    friend class SemaCheckerSnapshot;

    lir::LProgram run(const std::vector<writ::Writ>& asts,
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
    // G156-1: dependency-archive nominal decls (pkg,name) for the ambiguity universe.
    void set_dep_nominal_decls(const std::vector<std::pair<std::string, std::string>>* d) { dep_nominal_decls_ = d; }
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
    // Module system: per-AST owning-module id (mangle key). Parallel to
    // from_binary_/is_lazy_. Lifetime-borrowed; caller (sema_lower) keeps
    // the vector alive across run(). Null/empty or a per-index empty string
    // → no module-qualified mangling for that AST (plain user program).
    void set_module_ids(const std::vector<std::string>* v) { module_ids_ = v; }
    // UnitGraph §1.2: per-AST compile-unit key, parallel to filenames_. Set by
    // the EMITTER for a generated doc; empty for an ordinary source file, which
    // then keys on its own path. Lifetime-borrowed like the others.
    void set_ast_unit_key(const std::vector<std::string>* v) { ast_unit_key_ = v; }
    void set_module_name_to_id(const std::unordered_map<std::string, std::string>* m) { module_name_to_id_ = m; }
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
    // Size covers every Kind value (uses the LAST enumerator).
    std::array<TypeRef, int(LogosType::Kind::InferredType) + 1> prims_{};

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
    TypeRef never_t()   { return prim(LogosType::Kind::Never); }
    TypeRef inferred_t(){ return prim(LogosType::Kind::InferredType); }

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

    TypeRef make_ptr(bool mut, TypeRef pointee, bool zoned = false) {
        LogosTypeBuilder t; t.kind = LogosType::Kind::Ptr;
        t.mut_ptr = mut; t.pointee = pointee;
        // F3 (ref-repr-design §6/§8): `*zoned T` — a zoned pointer. Stored in the
        // free-for-Ptr const_val (bit 0), so it interns distinctly, serializes,
        // and equality-checks via the existing const_val plumbing (no MUT_PTR-style
        // per-site threading; sidesteps the P2-11 serialization trap). Deref/assign
        // of a `*zoned T` runs the zoned_enum_* (or RelOffset) storage↔compute bridge.
        if (zoned) t.const_val = 1;
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
    // ── Compiler-SYNTHESISED types: the package is FIXED, never resolved ──────
    // task #102, the root under #99. resolve_struct_pkg_/resolve_enum_pkg_ go
    // through lookup_qualified_, whose FIRST tier is sema_key(cur_package_,name)
    // — the package of the MODULE BEING COMPILED, ahead of every import. So when
    // the compiler synthesised one of ITS OWN internal types inside a module
    // that happened to declare a homonym, the synthesised type was handed the
    // USER's package and BECAME the user's type: types_compatible then correctly
    // found the two equal (it does compare pkg_name) and the wrong lowering
    // followed with NO diagnostic — measured, a 4-line `struct WritStatic` +
    // `pub const SHAPE: WritStatic = @{...}` compiled rc 0 and ran to exit 2.
    //
    // The synth_* constructors below consult THIS table and never the
    // declaration tables, so the fallback to a user package is IMPOSSIBLE rather
    // than unlikely. The owner package is read from each declaration's `package`
    // LINE, never from its directory (the #99 brief got five of nine wrong that
    // way). A name ABSENT from the table is compiler-owned and its package is
    // EMPTY — which is exactly what is_anyval's `pkg.empty()` discriminator
    // already assumes and, before this, could not enforce: `AnyVal`, `WritArr`
    // and `WritMap` are declared in no .logos anywhere.
    //
    // ⚠ `Ident` is declared TWICE — stdlib/mem/compiler/tokens/tokens.logos
    // (logos.std.compiler.tokens) and stdlib/mem/compiler/metaprog/ast.logos
    // (logos.std.compiler.metaprog). Both live synthesis sites are the QUOTE
    // path, whose `IdentSpan.ptr` is declared `*const Ident` in metaprog's
    // emitter.logos, so metaprog is the one they mean. The two layouts are
    // byte-identical today (tokens.logos says so in a comment); that
    // coincidence is NOT the reason for the choice and must not become one.
    //
    // ⚠ AN EARLIER DRAFT OF THIS COMMENT SAID "a module importing both measured
    // green either way". THAT WAS FALSE, and it is corrected here rather than
    // softened: one import ORDER was measured and the result generalised to
    // both. Measured properly (task #105), the orders differ —
    //   `use logos.std.compiler.tokens;` BEFORE `use …metaprog;`  -> rc 1,
    //       `quote_expr!: '#foo' — expected Ident or ExprBlob`
    //   metaprog first, or tokens absent                          -> rc 0, and
    //       the antiquoted value flows (run exit 37)
    // The metaprog choice below is still the right one and is unaffected. What
    // is affected is the claim: a fact asserted in a comment that the tree
    // contradicts is worse than no comment, because the next reader spends the
    // measurement it saved them and then some.
    static std::string_view synth_owner_pkg_(std::string_view name) {
        struct SynthOwner { std::string_view name, pkg; };
        // Not a bare-name lookup KEY: the name here is a compile-time literal
        // written by the compiler at the synthesis site, and the value is the
        // qualification. This table is what makes those sites qualified.
        static constexpr SynthOwner tbl[] = {
            // stdlib/mem/compiler/metaprog/ast.logos, emitter.logos
            {"Type",          "logos.std.compiler.metaprog"},
            {"ExprBlob",      "logos.std.compiler.metaprog"},
            {"Ident",         "logos.std.compiler.metaprog"},
            {"QuoteItemBlob", "logos.std.compiler.metaprog"},
            {"IdentSpan",     "logos.std.compiler.metaprog"},
            // stdlib/lang/writ/*
            {"WritStatic",    "logos.lang.writ.wstatic"},
            {"Wu6",           "logos.lang.writ.wmap"},
            {"WSchemaH",      "logos.lang.writ.wmap"},
            {"WMap",          "logos.lang.writ.wmap"},
            {"WAny",          "logos.lang.writ.anyval"},
            {"Writ",          "logos.lang.writ.container"},
            {"Allocator",     "logos.lang.writ.allocator"},
            // compiler-owned, declared nowhere: AnyVal, WritArr, WritMap —
            // deliberately ABSENT, so they get the empty package.
        };
        for (const auto& e : tbl) if (e.name == name) return e.pkg;
        return {};
    }
    TypeRef make_synth_struct(std::string_view name) {
        LogosTypeBuilder t; t.kind = LogosType::Kind::Struct; t.struct_name = name;
        t.pkg_name = std::string(synth_owner_pkg_(name));
        return pool_->alloc(std::move(t));
    }
    TypeRef make_synth_datatype(std::string_view name) {
        LogosTypeBuilder t; t.kind = LogosType::Kind::ZonedStruct; t.struct_name = std::string(name);
        t.pkg_name = std::string(synth_owner_pkg_(name));
        return pool_->alloc(std::move(t));
    }
    TypeRef make_synth_enum(std::string_view name) {
        LogosTypeBuilder t; t.kind = LogosType::Kind::Enum; t.enum_name = name;
        t.pkg_name = std::string(synth_owner_pkg_(name));
        return pool_->alloc(std::move(t));
    }
    TypeRef make_synth_generic_struct(std::string_view name,
                                      std::vector<TypeRef> args,
                                      std::vector<std::string> lt_args = {}) {
        LogosTypeBuilder t; t.kind = LogosType::Kind::Struct;
        t.struct_name   = std::string(name);
        t.type_args     = std::move(args);
        t.lifetime_args = std::move(lt_args);
        t.pkg_name = std::string(synth_owner_pkg_(name));
        return pool_->alloc(std::move(t));
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
    // ── THE ELISION ENGINE — MINT AND UNIFY (probes ltmintunify/ltmintsubst) ──
    // See src/compiler/PROBES.md 2026-08-31 (the minting round) and the
    // `lt_is_minted` comment in outlives.hpp. Minted names are LOCAL to the fn
    // being lowered; they are never written back into SemaFuncInfo, so a callee
    // signature read at a call site is unchanged by this.
    unsigned lt_mint_n_ = 0;
    std::vector<std::string> minted_lts_;   // this signature's minted regions
    std::string mint_lt_() {
        return std::string("'%") + std::to_string(++lt_mint_n_);
    }
    size_t decl_lt_arity_(TypeRef t) {
        using K = LogosType::Kind;
        if (t.kind() == K::Enum) {
            auto [p, ei] = find_enum_by_name(t.enum_name());
            (void)p; return ei ? ei->lifetime_params.size() : 0;
        }
        auto [sp, si] = find_struct_by_name(t.struct_name());
        (void)sp;
        if (si) return si->lifetime_params.size();
        auto [dp, di] = find_datatype_by_name(t.struct_name());
        (void)dp;
        return di ? di->lifetime_params.size() : 0;
    }
    // Give every ELIDED lifetime slot in `t` a fresh name; `out` collects every
    // region met, minted or written, in signature order (the elision rules
    // count those). When `fixed` is non-empty every elided slot takes THAT name
    // instead — elision rule 1/3's unification, the half `ltmintfresh` lacked.
    TypeRef mint_type_lts_(TypeRef t, std::vector<std::string>& out,
                           const std::string& fixed = {}, int depth = 0) {
        if (!t || depth > 24) return t;
        using K = LogosType::Kind;
        auto fresh = [&]() {
            if (!fixed.empty()) return fixed;
            std::string n = mint_lt_();
            minted_lts_.push_back(n);
            return n;
        };
        switch (t.kind()) {
        case K::Ref:
        case K::MutRef: {
            std::string lt(t.lifetime());
            // `'_` IS AN ELIDED SLOT THAT WAS SPELLED. It is the placeholder,
            // not a name — `fn make<'a>(r:&'a i32) -> H<'_>` is legal and the
            // mint must give it a region like any other elided slot, or the
            // comparison is `'_` against `'a` by spelling (measured: 2 of
            // `ltmintfree`'s legal refusals).
            if ((logos::probe::on("ltmintimpl") || logos::probe::arm_inst()) && (lt == "'_" || lt == "_")) lt.clear();
            if (lt.empty()) { lt = fresh(); logos::probe::census("mint.ref.elided"); }
            else            { logos::probe::census("mint.ref.written"); }
            out.push_back(lt);
            TypeRef inner = mint_type_lts_(t.pointee(), out, fixed, depth + 1);
            return make_ref(t.kind() == K::MutRef, inner, lt);
        }
        case K::Tuple: {
            std::vector<TypeRef> es;
            bool changed = false;
            for (auto e : t.tuple_elems()) {
                auto ne = mint_type_lts_(e, out, fixed, depth + 1);
                changed |= (ne != e);
                es.push_back(ne);
            }
            return changed ? make_tuple_type(std::move(es)) : t;
        }
        case K::Struct:
        case K::ZonedStruct:
        case K::Enum: {
            std::vector<std::string> lts = t.lifetime_args();
            size_t arity = decl_lt_arity_(t);
            // DOOR 3: an elided `Ref` carries ZERO lifetime args while the
            // declaration has two, and BOTH comparators bail on the arity
            // mismatch (`if (sl.size() != pl.size()) return true;`) before any
            // lifetime is compared. Filling to the declared arity is what makes
            // the shape guard a comparison.
            if (lts.size() < arity) {
                logos::probe::census("mint.structarg.absent", arity - lts.size());
                lts.resize(arity);
            }
            for (auto& l : lts)
                if ((logos::probe::on("ltmintimpl") || logos::probe::arm_inst()) && (l == "'_" || l == "_")) l = fresh();
                else if (l.empty()) { l = fresh(); logos::probe::census("mint.structarg.elided"); }
                else           { logos::probe::census("mint.structarg.written"); }
            for (auto& l : lts) out.push_back(l);
            std::vector<TypeRef> as;
            for (auto a : t.type_args()) as.push_back(mint_type_lts_(a, out, fixed, depth + 1));
            if (t.kind() == K::Enum)
                return make_generic_enum(t.enum_name(), std::move(as), std::move(lts),
                                         t.pkg_name());
            if (t.kind() == K::ZonedStruct)
                return make_generic_datatype(t.struct_name(), std::move(as), std::move(lts),
                                             t.pkg_name());
            return make_generic_struct(t.struct_name(), std::move(as), std::move(lts),
                                       t.pkg_name());
        }
        case K::Slice: {
            // ⚠ Kind::Slice HAS NO LIFETIME SLOT. `&[T]` canonicalises to Slice
            // at resolve_type and the region of the borrow is DROPPED there, so
            // no mint can give this parameter a name. Counted, not fixed.
            logos::probe::census("mint.slice.noslot");
            auto ne = mint_type_lts_(t.elem(), out, fixed, depth + 1);
            return ne == t.elem() ? t : make_slice_type(ne, t.mut_ptr());
        }
        case K::Array: {
            auto ne = mint_type_lts_(t.elem(), out, fixed, depth + 1);
            return ne == t.elem() ? t
                                  : make_array(ne, t.arr_size(), t.arr_size_var());
        }
        case K::DstRef:
            logos::probe::census("mint.dstref.noslot");
            return t;
        case K::TraitObject:
            logos::probe::census("mint.traitobject.noslot");
            return t;
        default:
            return t;
        }
    }

    // ── SUBSTITUTE AT THE CALL (probes ltsubstcall / ltmintsubst) ───────────
    // The METHOD path already builds a callee->caller lifetime map and applies
    // it to the return type (see `lt_subst` in sema_expr.cpp); the FREE-FN path
    // passes a TYPE substitution only, so a callee's `'a` reaches the caller as
    // the literal string `'a` and is compared against the caller's own `'a` by
    // SPELLING. `lifereg_callretlt` priced the crude form — rename every callee
    // lifetime to an unnameable token — and its cost 0 was refuted by the first
    // hand-written legal program (PROBES.md 2026-08-28: `fn pick<'a,T>(x:&'a T)
    // -> &'a T` called from `fn f<'a>(x:&'a i64)`). This is the substitution the
    // crude form skipped: a callee region SEEN at an argument becomes the
    // caller's actual region, and only a callee region seen NOWHERE becomes a
    // fresh unspellable one.
    // ── ONE MAP, TWO CONSUMERS: THE CALLEE'S BINDERS, INSTANTIATED ─────────
    // `build_call_lt_subst_` pairs each declared PARAMETER type against the
    // actual ARGUMENT type and reads the callee's own regions off the caller's.
    // The return type consumes it (`subst_call_ret_lts_`) and so does the
    // ARGUMENT COMPARISON (`inst_call_params_`) — a callee binder at an
    // argument position is UNIVERSALLY QUANTIFIED, so comparing the caller's
    // region against the literal name `'r` is comparing a region against a
    // BINDER (rule 12). Measured: `region-two-refs-same-region-pick-rg` and
    // `regions-infer-contravariance-due-to-ret` are refused with
    // "expected &'r BoxedInt, got &BoxedInt" for exactly that reason.
    //
    // ⚠ THREE OUTCOMES, NOT TWO. A callee region is
    //   MAPPED    — some argument gave it a region: use it.
    //   MENTIONED — it appears in a parameter type but that argument's own
    //               region is unnamed. It is NOT free: the caller does
    //               constrain it, we merely cannot name the constraint. It
    //               becomes ELIDED, which is the permissive answer the
    //               comparators already have for "no name".
    //   FREE      — it appears in NO parameter type at all. The caller
    //               instantiates it at any region it likes, `'static`
    //               included, and `'static` is what
    //               `stdlib/lang/writ/objdata.logos`'s `wod_view_array<'a>(p)
    //               -> &'a WArray` needs to compile.
    // Conflating MENTIONED with FREE is what put `B<'static>` into
    // `borrowck-unused-mut-locals` and refused it.
    void collect_param_regions_(TypeRef t, std::unordered_set<std::string>& out,
                                int depth = 0) {
        if (!t || depth > 24) return;
        using K = LogosType::Kind;
        switch (t.kind()) {
        case K::Ref: case K::MutRef:
            if (!t.lifetime().empty()) out.insert(std::string(t.lifetime()));
            collect_param_regions_(t.pointee(), out, depth + 1);
            return;
        case K::Struct: case K::ZonedStruct: case K::Enum:
            for (auto& l : t.lifetime_args()) if (!l.empty()) out.insert(l);
            for (auto a : t.type_args()) collect_param_regions_(a, out, depth + 1);
            return;
        case K::Tuple:
            for (auto e : t.tuple_elems()) collect_param_regions_(e, out, depth + 1);
            return;
        case K::Slice: case K::Array:
            collect_param_regions_(t.elem(), out, depth + 1);
            return;
        default: return;
        }
    }
    logos::compiler::StrMap<std::string> build_call_lt_subst_(const std::vector<TypeRef>& param_types,
                                           const std::vector<std::string>& lifetime_params,
                                           const std::vector<lir::LExprPtr>& args) {
        SemaLifetimeSubst ls;
        LtCands cands;
        std::function<void(TypeRef, TypeRef)> walk = [&](TypeRef pt, TypeRef at) {
            if (!pt || !at) return;
            using K = LogosType::Kind;
            auto pk = pt.kind();
            if ((pk == K::Ref || pk == K::MutRef) &&
                (at.kind() == K::Ref || at.kind() == K::MutRef)) {
                std::string p(pt.lifetime()), a(at.lifetime());
                if (!p.empty() && !a.empty()) {
                    cands[p].push_back(a);
                    if (!ls.count(p)) ls.emplace(p, a);
                }
                walk(pt.pointee(), at.pointee());
                return;
            }
            if ((pk == K::Struct || pk == K::ZonedStruct || pk == K::Enum) &&
                at.kind() == pk) {
                auto pl = pt.lifetime_args(); auto al = at.lifetime_args();
                for (size_t i = 0; i < pl.size() && i < al.size(); ++i)
                    if (!pl[i].empty() && !al[i].empty()) {
                        cands[pl[i]].push_back(al[i]);
                        if (!ls.count(pl[i])) ls.emplace(pl[i], al[i]);
                    }
                auto pa = pt.type_args(); auto aa = at.type_args();
                for (size_t i = 0; i < pa.size() && i < aa.size(); ++i) walk(pa[i], aa[i]);
                return;
            }
            if (pk == K::Tuple && at.kind() == K::Tuple) {
                auto pe = pt.tuple_elems(); auto ae = at.tuple_elems();
                for (size_t i = 0; i < pe.size() && i < ae.size(); ++i) walk(pe[i], ae[i]);
                return;
            }
            if ((pk == K::Slice || pk == K::Array) && at.kind() == pk) {
                walk(pt.elem(), at.elem());
                return;
            }
        };
        for (size_t i = 0; i < param_types.size() && i < args.size(); ++i)
            if (args[i]) walk(param_types[i], expr_type(args[i]));
        census_meet_("call", lifetime_params, cands, {});
        std::unordered_set<std::string> mentioned;
        const bool three_way_ = logos::probe::arm_inst() || logos::probe::arm_subst();
        if (three_way_)
            for (auto pt : param_types) collect_param_regions_(pt, mentioned);
        for (auto& lp : lifetime_params) {
            if (ls.count(lp)) { logos::probe::census("subst.call.mapped"); continue; }
            if (three_way_ && mentioned.count(lp)) {
                logos::probe::census("subst.call.mentioned");
                ls[lp] = "";                       // constrained, but unnameable
                continue;
            }
            // A callee region no argument mentions is INTERNAL to the callee.
            // It is not the caller's same-spelled binder, and it is not related
            // to anything the caller can name.
            logos::probe::census("subst.call.unmapped");
            // ⚠ THE TWO POLICIES, AND THE STDLIB PICKS BETWEEN THEM. A callee
            // region no argument mentions is FREE — the caller instantiates it,
            // so it is not a rigid region of anyone's scope. Treating it as a
            // fresh rigid one (`ltsubstcall`) is what refuses
            // `stdlib/lang/writ/objdata.logos`'s `wod_view_array<'a>(p) -> &'a
            // WArray`, the `&*(ptr as *const T)` view idiom: the method's own
            // return slot is minted, the callee's `'a` is rigid, and the two
            // cannot be related. Its upper bound is `'static` (a free region may
            // be instantiated at any region, `'static` included), and that is
            // the policy `ltsubstfree` / `ltmintfree` take.
            if (logos::probe::on("ltsubstfree") || logos::probe::on("ltmintfree") ||
                logos::probe::on("ltmintimpl") || three_way_)
                ls[lp] = "'static";
            else
                ls[lp] = mint_lt_();
        }
        return ls;
    }
    // The ARGUMENT side of the same map. Returns an empty vector when there is
    // nothing to instantiate, so the caller keeps its own `param_types`.
    std::vector<TypeRef> inst_call_params_(const std::vector<TypeRef>& param_types,
                                           const std::vector<std::string>& lifetime_params,
                                           const std::vector<lir::LExprPtr>& args) {
        if (!(logos::probe::arm_inst() || logos::probe::arm_subst()))
            return {};
        if (param_types.empty() || lifetime_params.empty()) return {};
        logos::probe::census("inst.call.site");
        auto ls = build_call_lt_subst_(param_types, lifetime_params, args);
        if (ls.empty()) return {};
        std::vector<TypeRef> out;
        out.reserve(param_types.size());
        bool any = false;
        for (auto pt : param_types) {
            auto np = subst_type_sema(pt, {}, ls);
            any |= (np != pt);
            out.push_back(np);
        }
        if (any) logos::probe::census("inst.call.differs");
        return any ? out : std::vector<TypeRef>{};
    }
    // ── THE STRUCT'S BINDERS, INSTANTIATED AT THE LITERAL ──────────────────
    // The field-init `check_variance` already carries "permissive — struct's
    // lifetime args are inferred at this site", and permissive=true is enough
    // only while the VALUE's regions are elided. Once every elided slot has a
    // name, `Bcx<'a>{fcx: &'a Fcx<'a>}` compares the DECLARED `'a` against a
    // minted `'%1` by spelling.
    //
    // ⚠ AND ERASING THE BINDER IS NOT THE FIX — MEASURED, TWICE, BY THE `fail`
    // ORACLE. Substituting `'a` to the elided region makes the comparison
    // vacuous and un-refuses two PINNED illegal programs whose whole subject is
    // that one binder appears TWICE:
    //     account-for-lifetimes-in-closure-suggestion   SameLifetime<'a>{ t:
    //         TwoThings<'a,'a> } from a TwoThings<'a,'b>
    //     nondeterministic-lifetime-errors-15034        Parser<'a>{ lexer:
    //         &'a mut Lexer<'a> } from a `&'a mut Lexer` (E0621)
    // Neither is visible to a pass-only oracle (rule 15's neighbour: an
    // un-refusal in the `fail` half). So the binder is INSTANTIATED from the
    // values — first occurrence wins, and a second occurrence that disagrees is
    // the refusal those two fixtures pin.
    // ── TWO OR MORE REGIONS INTO ONE BINDER: THE CENSUS, THEN THE MEET ─────
    // Every binder-binding walk in this compiler is FIRST-OCCURRENCE-WINS, so
    // a binder offered two different regions silently keeps one and refuses
    // the other's field. `LtCands` keeps them all; `census_meet_` counts the
    // arrivals per site and splits the multi-candidate ones by the binder's
    // DECLARED VARIANCE. Prose in src/compiler/PROBES.md 2026-08-31k.
    using LtCands = logos::compiler::StrMap<std::vector<std::string>>;
    // `vkey` is the variance-table key ("pkg.Name") when the binders belong to
    // a TYPE DEFINITION; empty when they are a fn's own — nothing in this tree
    // computes a variance for a fn's binder, so the guard cannot be asked
    // there and the census says so in its own bucket.
    std::optional<Variance> binder_variance_(const std::string& vkey, size_t i) {
        if (vkey.empty()) return std::nullopt;
        auto it = variance_table_.find(vkey);
        if (it == variance_table_.end()) return std::nullopt;
        auto vit = it->second.find("@" + std::to_string(i));
        if (vit == it->second.end()) return std::nullopt;
        return vit->second;
    }
    // A binder EVERY one of whose occurrences is covariant may be instantiated
    // at the MEET of the regions offered for it: the meet's only obligation is
    // that each offered region outlive it, and for a covariant occurrence that
    // is discharged by construction — no region inference, no CFG point. An
    // INVARIANT occurrence demands EQUALITY instead and no meet discharges
    // that, which is exactly what account-for-lifetimes-in-closure-suggestion
    // (`&'a mut &'a i64`) and nondeterministic-lifetime-errors-15034
    // (`&'a mut Lexer<'a>`) pin.
    bool binder_is_covariant_(const std::string& vkey, size_t i) {
        auto v = binder_variance_(vkey, i);
        return v && (*v == Variance::Co || *v == Variance::BiVar);
    }
    void census_meet_(const char* site,
                      const std::vector<std::string>& binders,
                      const LtCands& cands,
                      const std::string& vkey) {
        if (!logos::probe::census_armed()) return;
        const std::string p = std::string("meet.") + site;
        for (size_t i = 0; i < binders.size(); ++i) {
            auto it = cands.find(binders[i]);
            if (it == cands.end() || it->second.empty()) continue;
            logos::probe::census(p + ".binder");
            std::unordered_set<std::string> d(it->second.begin(), it->second.end());
            if (d.size() < 2) continue;
            logos::probe::census(p + ".multi");
            auto v = binder_variance_(vkey, i);
            if (!v) { logos::probe::census(p + ".multi.novariance"); continue; }
            logos::probe::census(p + ((*v == Variance::Co || *v == Variance::BiVar)
                                      ? ".multi.co" : ".multi.inv"));
        }
    }
    // PROBES ltmintmeet / ltmeetco (the covariance-GUARDED meet, on the mint
    // and on the substitution half respectively) and ltmeetany (the SAME meet
    // with the guard removed — rule 9's second name for the inner predicate,
    // and the abuse direction of the exemption).
    static bool probe_meet_() {
        return logos::probe::on("ltmintmeet") || logos::probe::on("ltmeetco") ||
               logos::probe::on("ltmeetany") || logos::probe::on("ltmintmeetrg") ||
               logos::probe::on("ltmintmeetamb") ||
               // ltregmeet (2026-08-31m) — the meet under the COVARIANCE GUARD
               // ALONE. No opaque predicate, no ambient stand-in: with the
               // region slot the variance fixpoint reads `&'a str` / `&'a [T]`
               // in a recorded position, so its answer IS the evidence those
               // two stand-ins were substituting for.
               logos::probe::on("ltregmeet");
    }
    static bool probe_meet_unguarded_() { return logos::probe::on("ltmeetany"); }
    // PROBE ltmintmeetrg — THE SECOND INNER PREDICATE (rule 9). Measured on
    // this binary, one variable apart (h1/h3/h4 in PROBES.md 2026-08-31k):
    //   struct L<'a> { i: &'a i64 }  struct P<'a> { l: &'a mut L<'a> }  → Inv
    //   struct L<'a> { i: &'a str }  (or `&'a [i64]`) → the SAME P is **Co**
    // because `&'a str` canonicalises to `Kind::Slice` and the region is
    // dropped at resolve_type, so `'a` appears in NO recorded position of `L`
    // and the fixpoint calls it BiVar — which composes to Co under the `&mut`.
    // The variance table cannot tell "this binder appears nowhere" from "this
    // binder appears only where the type cannot record it" (rule 16), and that
    // is what un-refuses the pinned `nondeterministic-lifetime-errors-15034`
    // under a guard that is otherwise exactly right. A def with a region-losing
    // slot anywhere in its reachable field types is REGION-OPAQUE and its
    // declared variance is not evidence.
    static bool probe_meet_opaque_guard_() {
        return logos::probe::on("ltmintmeetrg");
    }
    bool type_region_opaque_(TypeRef t, std::unordered_set<std::string>& seen,
                             int depth = 0) {
        if (!t || depth > 12) return false;
        using K = LogosType::Kind;
        switch (t.kind()) {
        case K::Slice: case K::TraitObject: case K::TaggedPtr:
            return true;
        case K::Ref: case K::MutRef: case K::Ptr:
            return type_region_opaque_(t.pointee(), seen, depth + 1);
        case K::Array:
            return type_region_opaque_(t.elem(), seen, depth + 1);
        case K::Tuple:
            for (auto e : t.tuple_elems())
                if (type_region_opaque_(e, seen, depth + 1)) return true;
            return false;
        case K::FnPtr: case K::Closure:
            for (auto p : t.closure_params())
                if (type_region_opaque_(p, seen, depth + 1)) return true;
            return type_region_opaque_(t.closure_ret(), seen, depth + 1);
        case K::Struct: case K::ZonedStruct: case K::Enum: {
            for (auto a : t.type_args())
                if (type_region_opaque_(a, seen, depth + 1)) return true;
            std::string nm(t.kind() == K::Enum ? t.enum_name() : t.struct_name());
            if (nm.empty() || !seen.insert(nm).second) return false;
            if (t.kind() == K::Enum) {
                auto [ep, ei] = find_enum_by_name(nm);
                (void)ep;
                if (!ei) return true;   // cannot read it ⇒ cannot vouch for it
                for (auto& v : ei->variants)
                    for (auto pt : v.payload_types)
                        if (type_region_opaque_(pt, seen, depth + 1)) return true;
                return false;
            }
            auto [sp, si] = find_struct_by_name(nm);
            (void)sp;
            if (si) {
                for (auto& fl : si->fields)
                    if (type_region_opaque_(fl.type, seen, depth + 1)) return true;
                return false;
            }
            auto [dp, di] = find_datatype_by_name(nm);
            (void)dp;
            if (di) {
                for (auto& fl : di->fields)
                    if (type_region_opaque_(fl.type, seen, depth + 1)) return true;
                return false;
            }
            return true;                // unknown def ⇒ cannot vouch for it
        }
        default:
            return false;
        }
    }
    template <class DeclFields>
    bool decl_fields_region_opaque_(const DeclFields& decl_fields) {
        std::unordered_set<std::string> seen;
        for (auto& f : decl_fields)
            if (type_region_opaque_(f.type, seen)) return true;
        return false;
    }

    // PROBE ltmintmeetamb — THE SECOND INNER PREDICATE, SHARPENED (rule 9).
    //
    // `ltmintmeetrg` withholds the meet whenever a region-losing slot is
    // reachable AT ALL, and PROBES.md 2026-08-31k measured what that costs:
    //   m1  struct S<'a> { a: &'a i32, b: Option<&'a i32>, s: &'a str }
    // is LEGAL and rg refuses it — one field away from the subject fixture.
    //
    // THE SHARPENING. A region `resolve_type` drops is ALWAYS the region OF A
    // REFERENCE (`&'a [T]` / `&'a str` / `&'a dyn` / `&'a Dst` all canonicalise
    // to a kind with no region slot), and a reference's own region occurs
    // COVARIANTLY in its own position — so the contribution the variance
    // fixpoint OMITTED is exactly the AMBIENT at that position. Omitting a Co
    // contribution cannot move a def's variance in the PERMISSIVE direction:
    // variance_meet(V, Co) differs from V only by BiVar→Co, and
    // binder_is_covariant_ already reads BiVar as covariant. Omitting a NON-Co
    // one can, and does — that is the 15034 pin, where the lost slot sits
    // under a `&mut`:
    //   struct L<'a> { i: &'a str }   struct P<'a> { l: &'a mut L<'a> }
    // `'a` is recorded NOWHERE in L, the fixpoint calls it BiVar, BiVar
    // composes to Co under the `&mut`, and P's declared Co is not evidence.
    // Reached from P's own fields the lost slot's ambient is Inv, so this
    // predicate withholds the meet and the pin holds (measured: under
    // `ltmintmeet`, the covariance guard ALONE, 15034 goes rc 1 -> 0).
    //
    // ⚠ CONSERVATIVE WHEREVER THE AMBIENT IS NOT KNOWABLE: an unreadable def,
    // the depth cap, and a nested def's TYPE ARGUMENT whose declared variance
    // is missing or BiVar all compose as Inv. Withholding a meet REFUSES; it
    // never accepts. The losing kinds are the FULL set here (DstRef and the
    // two Unsized kinds included), which rg's list did not carry.
    static bool probe_meet_amb_guard_() {
        return logos::probe::on("ltmintmeetamb");
    }
    Variance def_lt_var_(TypeRef t, size_t i, bool is_lt) {
        using K = LogosType::Kind;
        std::string key = std::string(t.pkg_name()) +
                          (t.pkg_name().empty() ? "" : ".") +
                          std::string(t.kind() == K::Enum ? t.enum_name()
                                                          : t.struct_name());
        auto it = variance_table_.find(key);
        if (it == variance_table_.end()) return Variance::Inv;
        auto vit = it->second.find((is_lt ? "@" : "#") + std::to_string(i));
        if (vit == it->second.end()) return Variance::Inv;
        // BiVar here means "recorded nowhere" — which is the very confusion
        // this predicate exists to police (rule 16). It is not evidence.
        return vit->second == Variance::BiVar ? Variance::Inv : vit->second;
    }
    bool region_loss_noncov_(TypeRef t, Variance amb,
                             std::unordered_set<std::string>& seen,
                             int depth = 0) {
        if (!t) return false;
        if (depth > 12) return true;            // cannot know the ambient
        using K = LogosType::Kind;
        auto noncov = [&] { return amb != Variance::Co; };
        switch (t.kind()) {
        case K::Slice:
            if (noncov()) return true;
            return region_loss_noncov_(t.elem(), amb, seen, depth + 1);
        case K::TraitObject:
            if (noncov()) return true;
            for (auto a : t.type_args())
                if (region_loss_noncov_(a, Variance::Inv, seen, depth + 1))
                    return true;
            return false;
        case K::TaggedPtr:
        case K::UnsizedSlice:
        case K::UnsizedDyn:
            return noncov();
        case K::DstRef:
            if (noncov()) return true;
            for (auto a : t.type_args())
                if (region_loss_noncov_(a, Variance::Inv, seen, depth + 1))
                    return true;
            return false;
        case K::Ref:
            return region_loss_noncov_(t.pointee(), amb, seen, depth + 1);
        case K::MutRef:
            return region_loss_noncov_(t.pointee(),
                                       variance_compose(amb, Variance::Inv),
                                       seen, depth + 1);
        case K::Ptr:
            return region_loss_noncov_(
                t.pointee(),
                variance_compose(amb, t.mut_ptr() ? Variance::Inv : Variance::Co),
                seen, depth + 1);
        case K::Array:
            return region_loss_noncov_(t.elem(), amb, seen, depth + 1);
        case K::Tuple:
            for (auto e : t.tuple_elems())
                if (region_loss_noncov_(e, amb, seen, depth + 1)) return true;
            return false;
        case K::FnPtr: case K::Closure: case K::FnItem:
            for (auto p : t.closure_params())
                if (region_loss_noncov_(p, variance_compose(amb, Variance::Contra),
                                        seen, depth + 1)) return true;
            return region_loss_noncov_(t.closure_ret(), amb, seen, depth + 1);
        case K::Struct: case K::ZonedStruct: case K::Enum: {
            size_t i = 0;
            for (auto a : t.type_args()) {
                Variance inner = variance_compose(amb, def_lt_var_(t, i++, false));
                if (region_loss_noncov_(a, inner, seen, depth + 1)) return true;
            }
            std::string nm(t.kind() == K::Enum ? t.enum_name() : t.struct_name());
            if (nm.empty()) return true;
            // Keyed by name AND ambient: the same def reached covariantly and
            // under a `&mut` is two different questions.
            std::string mk = nm + "#" + std::to_string((int)amb);
            if (!seen.insert(mk).second) return false;
            if (t.kind() == K::Enum) {
                auto [ep, ei] = find_enum_by_name(nm);
                (void)ep;
                if (!ei) return true;      // cannot read it ⇒ cannot vouch
                for (auto& v : ei->variants)
                    for (auto pt : v.payload_types)
                        if (region_loss_noncov_(pt, amb, seen, depth + 1))
                            return true;
                return false;
            }
            auto [sp, si] = find_struct_by_name(nm);
            (void)sp;
            if (si) {
                for (auto& fl : si->fields)
                    if (region_loss_noncov_(fl.type, amb, seen, depth + 1))
                        return true;
                return false;
            }
            auto [dp, di] = find_datatype_by_name(nm);
            (void)dp;
            if (di) {
                for (auto& fl : di->fields)
                    if (region_loss_noncov_(fl.type, amb, seen, depth + 1))
                        return true;
                return false;
            }
            return true;                   // unknown def ⇒ cannot vouch
        }
        default:
            return false;
        }
    }
    template <class DeclFields>
    bool decl_fields_region_evidence_bad_(const DeclFields& decl_fields) {
        std::unordered_set<std::string> seen;
        for (auto& f : decl_fields)
            if (region_loss_noncov_(f.type, Variance::Co, seen)) return true;
        return false;
    }

    template <class DeclFields>
    logos::compiler::StrMap<std::string> structlit_lt_subst_(
            const std::vector<std::string>& lifetime_params,
            const DeclFields& decl_fields,
            const std::vector<std::pair<std::string, lir::LExprPtr>>& fields,
            const std::string& vkey = {}) {
        logos::compiler::StrMap<std::string> flt;
        LtCands cands;
        if (lifetime_params.empty()) return flt;
        std::function<void(TypeRef, TypeRef)> walk = [&](TypeRef dt, TypeRef at) {
            if (!dt || !at) return;
            using K = LogosType::Kind;
            auto dk2 = dt.kind();
            if ((dk2 == K::Ref || dk2 == K::MutRef) &&
                (at.kind() == K::Ref || at.kind() == K::MutRef)) {
                std::string d(dt.lifetime()), a(at.lifetime());
                if (!d.empty() && !a.empty()) {
                    cands[d].push_back(a);
                    if (!flt.count(d)) flt.emplace(d, a);
                }
                walk(dt.pointee(), at.pointee());
                return;
            }
            if ((dk2 == K::Struct || dk2 == K::ZonedStruct || dk2 == K::Enum) &&
                at.kind() == dk2) {
                auto dl = dt.lifetime_args(); auto al = at.lifetime_args();
                for (size_t i = 0; i < dl.size() && i < al.size(); ++i)
                    if (!dl[i].empty() && !al[i].empty()) {
                        cands[dl[i]].push_back(al[i]);
                        if (!flt.count(dl[i])) flt.emplace(dl[i], al[i]);
                    }
                auto da = dt.type_args(); auto aa = at.type_args();
                for (size_t i = 0; i < da.size() && i < aa.size(); ++i) walk(da[i], aa[i]);
                return;
            }
            if (dk2 == K::Tuple && at.kind() == K::Tuple) {
                auto de = dt.tuple_elems(); auto ae = at.tuple_elems();
                for (size_t i = 0; i < de.size() && i < ae.size(); ++i) walk(de[i], ae[i]);
                return;
            }
        };
        for (auto& f : decl_fields)
            for (auto& [fname, fval] : fields)
                if (fval && fname == f.name) { walk(f.type, expr_type(fval)); break; }
        census_meet_("structlit", lifetime_params, cands, vkey);
        // Only the struct's OWN binders are instantiated here; anything else in
        // a declared field type is a name from an enclosing scope.
        logos::compiler::StrMap<std::string> out;
        for (size_t i = 0; i < lifetime_params.size(); ++i) {
            const std::string& lp = lifetime_params[i];
            auto it = cands.find(lp);
            if (it == cands.end() || it->second.empty()) continue;
            std::unordered_set<std::string> d(it->second.begin(), it->second.end());
            // THE MEET. Two DIFFERENT regions were offered for one binder and
            // the binder is covariant: it is instantiated at the region both
            // outlive. Nobody wrote that region down and it needs no name —
            // an elided slot is the comparators' existing spelling for "a
            // region whose only obligation is discharged here". Without the
            // guard this is R3's erasure, which un-refuses two pinned illegal
            // programs; with it, those two are Inv and keep first-wins.
            if (d.size() >= 2 && probe_meet_() &&
                (probe_meet_unguarded_() ||
                 (binder_is_covariant_(vkey, i) &&
                  !(probe_meet_opaque_guard_() &&
                    decl_fields_region_opaque_(decl_fields)) &&
                  !(probe_meet_amb_guard_() &&
                    decl_fields_region_evidence_bad_(decl_fields))))) {
                logos::probe::census("meet.structlit.applied");
                out[lp] = "";
                continue;
            }
            out[lp] = it->second.front();
        }
        return out;
    }
    TypeRef subst_call_ret_lts_(const std::vector<TypeRef>& param_types,
                                const std::vector<std::string>& lifetime_params,
                                const std::vector<lir::LExprPtr>& args,
                                TypeRef ret) {
        if (!ret || lifetime_params.empty()) return ret;
        logos::probe::census("subst.call.site");
        auto ls = build_call_lt_subst_(param_types, lifetime_params, args);
        return ls.empty() ? ret : subst_type_sema(ret, {}, ls);
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
        TypeRef at(expr_type(arg));
        if (!at) return false;
        bool is_ref_or_ptr =
            at.kind() == LogosType::Kind::Ref ||
            at.kind() == LogosType::Kind::MutRef ||
            at.kind() == LogosType::Kind::Ptr;
        if (!is_ref_or_ptr) return false;
        TypeRef pointee = at.pointee();
        if (!pointee || pointee.kind() != LogosType::Kind::Array) return false;
        // ELEMENT IDENTITY, not compatibility. types_compatible allows the
        // scalar widening that is fine for values and fatal for memory:
        // `&[i32; N]` "compatible with" `&[i64]` would reinterpret the buffer
        // at the wrong stride and read garbage upper bits (T0-5). A slice
        // aliases raw memory, so its element type must match EXACTLY.
        //
        // A HOLE (`&[_]`) is not a comparison, it is a request: fill it from
        // the array's element. An IntLit element likewise adopts the hinted
        // concrete element (the literal has no committed width yet) — that is
        // value typing, not memory reinterpretation: the array is BUILT at
        // the adopted width, not viewed at a different one.
        TypeRef want_elem = et.elem();
        TypeRef have_elem = pointee.elem();
        if (want_elem && TypeRef(want_elem).kind() == LogosType::Kind::InferredType) {
            want_elem = have_elem;
        } else if (have_elem && want_elem &&
                   TypeRef(have_elem).kind() == LogosType::Kind::IntLit &&
                   is_integer_kind(TypeRef(want_elem).kind()) &&
                   TypeRef(want_elem).kind() != LogosType::Kind::IntLit) {
            // adopt below via the arg rewrite: the slice takes the hinted
            // element; codegen already widens IntLit array elements in place.
        } else if (!types_equal(have_elem, want_elem)) {
            return false;
        }
        int64_t n = static_cast<int64_t>(pointee.arr_size());
        // B6/P2-11: carry the source's mutability — `&mut [T;N]` (or `*mut`)
        // decays to a `&mut [T]` slice, `&[T;N]` to a shared `&[T]`. Without
        // this a `&mut arr` arg would become a shared slice and be (wrongly)
        // rejected against a `&mut [T]` param.
        bool src_mut = at.kind() == LogosType::Kind::MutRef ||
                       (at.kind() == LogosType::Kind::Ptr && at.mut_ptr());
        // Build a slice_lit using the ref/ptr value as the data ptr and N as
        // the length. arg holds the address expression; reuse it directly.
        // want_elem: the hole-filled element when expected was `&[_]`.
        auto len = builder().lit_int(n, prim(LogosType::Kind::I64));
        arg = builder().slice_lit(std::move(arg), std::move(len),
                                  make_slice_type(want_elem, src_mut));
        return true;
    }
    // Inverse of the above. `&arr` over an array *variable* is lowered
    // context-free to a fat-pointer slice `&[T]` (sema_expr.cpp `&array_var`
    // branch) — discarding the precise `&[T; N]` type. When the parameter
    // actually wants `&[T; N]` (a ref-to-array), recover it: the slice_lit's
    // base is the `addr_of(arr)`, so re-synthesize a fresh `&[T; N]` addr_of
    // from the array var's name. Soundness: re-look up the var to confirm it
    // is an array whose real size == N (else a `[T; M]` var would be retyped
    // to a wrong-length `&[T; N]`). Only the shared `&` case (Kind::Ref / Ptr);
    // a shared slice never satisfies a `&mut [T; N]` param.
    // Every coercion a WRITE to a typed place performs. `let x: T = e` grew
    // these one at a time; plain assignment, field writes and index/deref
    // writes grew NONE, so `s.f = &arr` (f: &[T]) was a type error while
    // `let f: &[T] = &arr` was not. Same target type, same source expression,
    // different answer depending on the statement form.
    // Returns true iff `rhs` was rewritten.
    bool apply_place_coercions(lir::LExprPtr& rhs, TypeRef target);

    bool try_coerce_slice_to_array_ref(lir::LExprPtr& arg, TypeRef expected) {
        if (!arg || !expected) return false;
        TypeRef et(expected);
        if (et.kind() != LogosType::Kind::Ref &&
            et.kind() != LogosType::Kind::Ptr) return false;
        TypeRef pointee = et.pointee();
        if (!pointee || pointee.kind() != LogosType::Kind::Array) return false;
        TypeRef at(expr_type(arg));
        if (!at || at.kind() != LogosType::Kind::Slice) return false;
        if (!types_compatible(at.elem(), pointee.elem())) return false;
        auto xref = expr_ref_of(arg);
        if (!xref || xref.kind() != lir_schema::expr::Code::SliceLit) return false;
        auto base = lir_view::ESliceLitView{xref}.base();
        if (!base || base.kind() != lir_schema::expr::Code::AddrOf) return false;
        std::string var_name(lir_view::EAddrOfView{base}.var_name());
        if (var_name.empty()) return false;
        auto vt = lookup(var_name);
        if (!vt || TypeRef(vt).kind() != LogosType::Kind::Array) return false;
        if (TypeRef(vt).arr_size() != pointee.arr_size()) return false;
        arg = builder().addr_of(var_name, expected);
        return true;
    }
    // G158-7: does an `&T` / `&mut T` argument satisfy a `&dyn Trait`
    // (TraitObject) parameter by an unsizing coercion? True when the pointee
    // is either (a) a TypeVar whose in-scope bound-closure includes the
    // trait, or (b) a concrete struct/enum that implements the trait. The arg
    // expr is NOT rewritten — mlir-gen's call-site `coerce_to_dyn` builds the
    // fat pointer (keying on the arg's still-`&T` type). Defined in
    // sema_expr.cpp (needs find_trait_iter_scoped / impls_).
    bool ref_arg_satisfies_dyn(TypeRef at, TypeRef pt);
    // logos-core 2.4(c): enforce `+ Send` / `+ Sync` auto-trait bounds on a
    // dyn target at coercion sites. types_compatible's Struct→TraitObject
    // branch is a blanket-accept (impl check deferred), so this is the
    // soundness gate that emits a specific diagnostic when the source's
    // pointee doesn't structurally satisfy the bound.
    void check_dyn_auto_bounds_at_coercion(lir_view::ExprRef arg, TypeRef pt);

    // If param `pt` is a trait-object (`&dyn Trait` / `&mut dyn Trait`) and
    // `arg` is a `&Concrete`/`&mut Concrete` that satisfies the trait (directly
    // or via a blanket impl), wrap `arg` in an explicit dyn-coercion cast so it
    // unsizes to the fat pointer — Rust's implicit unsized coercion in argument
    // position. Returns true if it coerced. Generalises the array-element
    // coercion (a4d23821) to any call/method-arg position. No-op when `arg`
    // already fits `pt` or `pt` is not a trait object.
    bool coerce_arg_to_dyn(lir::LExprPtr& arg, TypeRef pt);
    bool coerce_dyn_upcast(lir::LExprPtr& arg, TypeRef pt);
    // A TUPLE or ARRAY literal's slot types have no second source: mlir-gen
    // reads them off the LITERAL NODE's own type (tuple_llvm_type / gen_arr_lit's
    // `logos_elem`), unlike a struct field, whose type comes from the struct
    // declaration and is therefore right in every context. So an implicit unsize
    // (`&Concrete` into a `&dyn Trait` slot) inside one of those literals must be
    // recorded HERE, at the coercion site, or the literal keeps the thin type and
    // every consumer (layout, call ABI, `t.0.method()`, `arr[i].method()`) reads
    // a 16-byte fat pair out of an 8-byte object. Returns true iff it stamped.
    // One aggregate slot: does `tgt` accept `at` only via a `&Concrete` → `&dyn
    // Trait` unsize? (A pure type question — coerce_arg_to_dyn cannot answer it:
    // it opens with a types_compatible early-out that blanket-accepts exactly
    // this pair.)
    bool aggregate_slot_needs_unsize(TypeRef at, TypeRef tgt);
    // Stamp `target` onto a tuple/array literal IN PLACE, walking match arms /
    // if branches / block results to reach the literal where it sits.
    bool retype_aggregate_lit_to(lir_view::ExprRef er, TypeRef target);
    // CoerceUnsized for a smart-pointer/wrapper struct: `Rc<A>` → `Rc<dyn Tr>`
    // (same struct, a field unsizes sized→DstRef/TraitObject/slice). Rebuilds
    // `e` in place via struct_lit, coercing the changed field. Returns true iff
    // applied. Shared by explicit `as` (lower_cast) and the implicit coercion
    // points (arg / let / return). Single-field shape only.
    bool try_struct_unsize_coerce(lir::LExprPtr& e, TypeRef target);
    bool try_implicit_reborrow_mut(lir::LExprPtr& arg, TypeRef pt,
                                   bool allow_downgrade = true);

    // Single chokepoint that binds a method receiver to its formal `self`
    // slot: implicit reborrow (Rust auto-reborrow at method-recv coercion
    // sites, downgrade DISABLED so `&mut Self` doesn't get downgraded to
    // `&Self` — that would dispatch through the wrong impl key for
    // `impl X for &mut M`-style ref-impls) and by-value-self move tracking.
    // Every method-dispatch path calls this so the pair can't drift.
    void bind_method_receiver(lir::LExprPtr& recv, TypeRef formal_self);

    // Canonical arg→param coercion pipeline. The 8 implicit coercions Logos
    // applies at coercion sites — `bare_enum → typed_enum`, closure literal
    // → fn-ptr, `&[T;N] ↔ &[T]`, `&dyn Sub → &dyn Super`, `&Concrete →
    // &dyn Trait`, `&mut T` auto-reborrow, integer widening — are all
    // individually-callable helpers, and the ORDER they run in is the
    // chokepoint that used to drift across ~15 hand-rolled call sites. This
    // helper enshrines the canonical order in one place; each call site
    // declares which coercions it wants via a flag mask (Standard = all 8,
    // Minimal = reborrow + widen for sites where the type-coerce surface is
    // intentionally narrow). Adding a new coercion in the future = one edit
    // here, not 15.
    //
    // Sites kept INLINE (deliberately not converted): struct-literal field
    // assignment (Rust MOVES — no reborrow), the `widen-first` struct-
    // method exact path which uses a different order, and the
    // `coerce_arg_to_dyn-after-widen` trait-method / generic-method-arg
    // sites — those orderings have been load-bearing under the suite and
    // pinning them with a comment is cheaper than provably-equivalent flag
    // expansion. Adding a flag to this helper to express them would defeat
    // the foundation's value (one canonical order).
    enum CoerceFlag : uint32_t {
        CFLAG_NONE             = 0,
        CFLAG_BARE_ENUM        = 1u << 0,  // try_retype_bare_enum_arg
        CFLAG_CLOSURE_TO_FNPTR = 1u << 1,
        CFLAG_ARRAY_TO_SLICE   = 1u << 2,  // try_coerce_array_ref_to_slice
        CFLAG_SLICE_TO_ARRAY   = 1u << 3,  // try_coerce_slice_to_array_ref
        CFLAG_DYN_UPCAST       = 1u << 4,  // coerce_dyn_upcast
        CFLAG_ARG_TO_DYN       = 1u << 5,  // coerce_arg_to_dyn
        CFLAG_IMPLICIT_REBORROW = 1u << 6, // try_implicit_reborrow_mut
        CFLAG_WIDEN_INT        = 1u << 7,
        // Side CHECKS, not coercions. They were unconditional in the pipeline
        // executor — argument-position semantics grafted onto every caller.
        // As flags they are table rows like everything else: an assignment
        // position must not get E0507 until the checker understands Copy
        // bounds there (stdlib iter.logos:1842 is the evidence:
        // `self.buf[j] = self.buf[j+1]` under a Copy-bounded T).
        CFLAG_CHECK_E0507      = 1u << 8,  // move-out-of-borrowed-place check
        CFLAG_CHECK_DYN_BOUNDS = 1u << 9,  // dyn auto-trait bound enforcement
        CFLAG_ACCEPT_REF_DYN   = 1u << 11, // accept ref_arg_satisfies_dyn (G158-7)
        CFLAG_SKIP_UNRESOLVED  = 1u << 12, // skip TypeVar/AssocType FORMALS —
                                           // call rows only: mono re-judges an
                                           // instantiated arg, but a let/GAT
                                           // annotation is judged HERE (the
                                           // gat_bounds_violation refusal
                                           // vanished when this was universal)
        CFLAG_ACCEPT_SD_THIN   = 1u << 10, // accept sd_thin_compatible pairs
                                           // (thin one-repr receiver forms) —
                                           // the call family's extra acceptor
        CFLAG_STANDARD = CFLAG_BARE_ENUM | CFLAG_CLOSURE_TO_FNPTR |
                         CFLAG_ARRAY_TO_SLICE | CFLAG_SLICE_TO_ARRAY |
                         CFLAG_DYN_UPCAST | CFLAG_IMPLICIT_REBORROW |
                         CFLAG_WIDEN_INT |
                         CFLAG_CHECK_E0507 | CFLAG_CHECK_DYN_BOUNDS,
        CFLAG_MINIMAL  = CFLAG_IMPLICIT_REBORROW | CFLAG_WIDEN_INT,
    };
    // ── expect_type: the ONE judgment ───────────────────────────────────
    // WHERE an expression meets an expected type. The position is the only
    // thing allowed to vary coercion behaviour, and it does so through
    // mask_for()'s single table — never through per-site code. A syntax node
    // may CALL this; it may not re-implement any part of it.
    enum class CoercePos : uint8_t {
        CallArg,          // plain fn call argument
        MethodArg,        // struct/trait method argument (order pinned by suite)
        ClosureArg,       // closure / fn-ptr call argument
        LetInit,          // let x: T = e
        PlaceWrite,       // x = e, s.f = e, a[i] = e, *p = e
        StructLitField,   // S { f: e } — no reborrow: Rust MOVES here
        TupleElem,        // tuple literal element under a hint
        ArrayElem,        // array literal element under a hint
        Return,           // return e / implicit tail (Box→dyn consume here)
        BranchArm,        // if/match arm against the surrounding expectation
        ConstInit,        // const/static initializer
        Operand,          // an operand of a larger construct (compound-assign
                          // RHS): verdict + widen only — reborrow/unsize make
                          // no sense for `place op rhs`
    };
    static uint32_t mask_for(CoercePos pos);

    // Runs the coercion pipeline for `pos`, then verdicts. On mismatch emits
    //   "{ctx}: expected {}, got {}"
    // and returns false. The monopoly on that diagnostic lives HERE: a
    // position that forgets to route through expect_type cannot reject an
    // expression at all, so a new syntax node fails loudly in review, not
    // silently in a user's build.
    bool expect_type(lir::LExprPtr& e, TypeRef expected, CoercePos pos,
                     std::string_view ctx);

    void coerce_arg_to_param(lir::LExprPtr& arg, TypeRef pt,
                              uint32_t flags = CFLAG_STANDARD);

    // Build a `str_eq(a, b)` bool guard for a string-literal pattern. A raw
    // `a == b` LBinOp would pointer-compare two str slices; the stdlib `str_eq`
    // does a content compare. Returns null if `str_eq` isn't in scope.
    lir::LExprPtr make_str_eq_guard(lir::LExprPtr a, lir::LExprPtr b);

    // Synthesize a default value for `t` (the body of `<t>::default()`). For an
    // array `[E; N]` this builds `[E::default(); N]` (recursing on E); for a
    // primitive/struct it emits a call to `<t>::default()`'s resolved symbol.
    // Returns null if `t` (or an element) has no Default impl in scope.
    lir::LExprPtr default_value_for(TypeRef t);

    // Retype an incompletely-typed generic enum-literal argument to the
    // parameter's concrete enum spec. A bare `Opt::None` / partially-inferred
    // `Opt::Some(3)` passed directly as a call argument carries no (or
    // `<error>`) type-args, so mlir-gen emits a C-style i32 discriminant where
    // the heap-ptr enum is expected (operand-type mismatch). The parameter type
    // pins the missing args. Mirrors lower_assign's retype
    // ([[baghunt-replace-ref-option-cascade]]). Only fires when the literal's
    // known (non-error) type-args already match the target's, so a genuine
    // mismatch is still rejected downstream.
    // Recursively detect an "incomplete" type: itself a TypeVar/Error, or any
    // nested type-arg / element / pointee that is incomplete. A nested
    // payload-less enum literal `Option::Some(Option::None)` lowers the outer
    // to `Option<Option<TypeVar>>` — the top-level arg is a concrete Enum, but
    // its type-arg is incomplete, so a shallow check would miss it.
    bool enum_arg_unresolved(TypeRef t) {
        if (!t) return true;
        TypeRef tr(t);
        auto k = tr.kind();
        if (k == LogosType::Kind::Error || k == LogosType::Kind::TypeVar) return true;
        if (k == LogosType::Kind::Enum) {
            auto args = tr.type_args();
            auto [pkg, esi] = find_enum_by_name(tr.enum_name());
            (void)pkg;
            // A generic enum carrying fewer type-args than its declared params
            // (notably ZERO, e.g. a bare `Option`) is incomplete — the inner
            // payload-less variant never had its T pinned.
            if (esi && args.size() < esi->type_params.size()) return true;
            for (auto ta : args)
                if (enum_arg_unresolved(ta)) return true;
        }
        if (k == LogosType::Kind::Struct || k == LogosType::Kind::ZonedStruct) {
            auto args = tr.type_args();
            auto [pkg, ssi] = find_struct_by_name(tr.struct_name());
            (void)pkg;
            if (ssi && args.size() < ssi->type_params.size()) return true;
            for (auto ta : args)
                if (enum_arg_unresolved(ta)) return true;
        }
        if (k == LogosType::Kind::Tuple)
            for (auto el : tr.tuple_elems())
                if (enum_arg_unresolved(el)) return true;
        if ((k == LogosType::Kind::Ref || k == LogosType::Kind::MutRef) && tr.pointee())
            return enum_arg_unresolved(tr.pointee());
        return false;
    }
    // K4-root: pin a (possibly nested) enum-literal expression to a concrete
    // enum type via the LIR mirror, then project the concrete type-args through
    // the matched variant's payload types and recurse into each payload
    // sub-expr that is itself an enum literal. Without recursion, the inner
    // `Option::None` of `Option::Some(Option::None)` stays a bare C-style enum
    // (inline i32) while the outer slot is a heap pointer — deref → SIGSEGV.
    void retype_enum_lit_recursive(lir_view::ExprRef e, TypeRef concrete) {
        using EC = lir_schema::expr::Code;
        auto ek = e.kind();
        if (ek != EC::EnumLit && ek != EC::EnumLitData) return;
        if (!concrete || TypeRef(concrete).kind() != LogosType::Kind::Enum) return;
        lir_mirror_retype_expr(*cur_prog_, e.addr(), concrete);
        if (ek != EC::EnumLitData) return;
        lir_view::EEnumLitDataView v{e};
        std::string en(v.enum_name());
        std::string vn(v.variant());
        auto [pkg, esi] = find_enum_by_name(en);
        (void)pkg;
        if (!esi) return;
        const SemaVariantInfo* vinfo = nullptr;
        for (auto& vv : esi->variants) if (vv.name == vn) { vinfo = &vv; break; }
        if (!vinfo) return;
        SemaSubst subst;
        auto cta = TypeRef(concrete).type_args();
        for (size_t i = 0; i < esi->type_params.size() && i < cta.size(); ++i)
            if (cta[i]) subst[esi->type_params[i].name] = cta[i];
        size_t idx = 0;
        v.each_payload([&](lir_view::ExprRef pe) {
            if (idx < vinfo->payload_types.size()) {
                TypeRef ptt = vinfo->payload_types[idx];
                if (ptt && !subst.empty()) ptt = subst_type_sema(ptt, subst);
                if (ptt && TypeRef(ptt).kind() == LogosType::Kind::Enum)
                    retype_enum_lit_recursive(pe, ptt);
            }
            ++idx;
        });
    }
    bool try_retype_bare_enum_arg(lir::LExprPtr& arg, TypeRef expected) {
        if (!arg || !expected) return false;
        TypeRef at(expr_type(arg)), pt(expected);
        // Peel `&Enum<T>` / `&mut Enum<T>` on the target so a `&Option::None`
        // arg vs `&Option<i32>` formal still triggers retype (the call-arg
        // coercion site may have wrapped pt in a ref). Pre-fix this peel
        // lived only in the local lambda at sema_expr.cpp:3417; folding it
        // here lets the lambda dissolve into the member fn (logos-core 1.2).
        if ((pt.kind() == LogosType::Kind::Ref ||
             pt.kind() == LogosType::Kind::MutRef) && pt.pointee())
            pt = pt.pointee();
        if (at.kind() != LogosType::Kind::Enum ||
            pt.kind() != LogosType::Kind::Enum) return false;
        if (pt.type_args().empty()) return false;
        if (at.enum_name() != pt.enum_name()) return false;
        auto rk = expr_ref_of(arg).kind();
        if (rk != lir_schema::expr::Code::EnumLit &&
            rk != lir_schema::expr::Code::EnumLitData) return false;
        auto aa = at.type_args();
        auto pa = pt.type_args();
        bool incomplete = aa.empty();
        if (!incomplete)
            for (auto ta : aa)
                if (enum_arg_unresolved(ta)) { incomplete = true; break; }
        if (!incomplete) return false;
        for (auto ta : pa)
            if (enum_arg_unresolved(ta)) return false;
        if (!aa.empty()) {
            if (aa.size() != pa.size()) return false;
            for (size_t i = 0; i < aa.size(); ++i)
                if (!enum_arg_unresolved(aa[i]) && !types_compatible(aa[i], pa[i])) return false;
        }
        // Retype the top node (keeps LExpr.type in sync for the post-call
        // compat check) then recurse into nested payload enum-lits.
        builder().retype_expr(arg, pt);
        retype_enum_lit_recursive(expr_ref_of(arg), pt);
        return true;
    }
    // Coerce a non-capturing closure to fn ptr when target type is FnPtr.
    // Returns true if coercion was applied (arg's type is changed to FnPtr).
    bool try_coerce_closure_to_fnptr(lir::LExprPtr& arg, TypeRef expected) {
        TypeRef er(expected);
        if (!arg || !er || er.kind() != LogosType::Kind::FnPtr) return false;
        TypeRef at(expr_type(arg));
        if (!at || at.kind() != LogosType::Kind::Closure) return false;
        auto xref = expr_ref_of(arg);
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
    // `lt` — THE REGION SLOT (PROBE ltregslot, PROBES.md 2026-08-31m). Empty
    // for every caller that does not write it, which is every caller unarmed;
    // the field is ignored by compute_type_uid (so `&'a [T]` and `&[T]` stay
    // types_equal, exactly as `&'a T` and `&T` already are) and DISTINGUISHED
    // by builder_equals_typeref (so the two get distinct pool entries and the
    // region survives, exactly as Ref's already does).
    TypeRef make_slice_type(TypeRef elem, bool is_mut = false,
                            TypeRef::OwningKind owning = TypeRef::OwningKind::Borrow,
                            std::string lt = {}) {
        LogosTypeBuilder t; t.kind = LogosType::Kind::Slice;
        t.elem = elem;
        t.mut_ptr = is_mut;
        t.lifetime = std::move(lt);
        // Owning `Box<[T]>` slice — same 16-byte {data,len} layout as a borrowed
        // `&[T]`, but move-only and droppable (frees the buffer + drops elements).
        // Rides in const_val, exactly like TraitObject's owning kind.
        if (owning != TypeRef::OwningKind::Borrow)
            t.const_val = int64_t(uint8_t(owning));
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
                                  std::vector<TypeRef> args = {},
                                  bool req_send = false,
                                  bool req_sync = false) {
        LogosTypeBuilder t; t.kind = LogosType::Kind::UnsizedDyn;
        t.trait_name = std::string(tname);
        t.type_args = std::move(args);
        // `dyn Trait + Send/Sync` inside an owning container (`Box<dyn …>`):
        // carry the auto-bound bits in const_val (same encoding as
        // make_trait_object) so the bound survives type construction and the
        // unsize-coercion check can enforce it (T1-12 residual — the bits were
        // dropped here, so `Box<dyn T + Send>` ≡ `Box<dyn T>`).
        uint64_t packed = 0;
        if (req_send) packed |= TRAIT_BOUND_SEND_BIT;
        if (req_sync) packed |= TRAIT_BOUND_SYNC_BIT;
        if (packed != 0)
            t.const_val = int64_t(packed);
        return pool_->alloc(std::move(t));
    }
    // Phase 1B-14: `&DstStruct` / `&mut DstStruct` / `*const DstStruct` —
    // fat pointer to a custom-DST struct. Stored as {data_ptr, tail_len}
    // and passed by pointer (same ABI as Kind::Slice). is_mut differentiates
    // `&` vs `&mut`/`*mut` for borrow-checker purposes.
    TypeRef make_dst_ref(std::string_view struct_name,
                         std::string_view pkg_name,
                         bool is_mut,
                         std::vector<TypeRef> type_args = {},
                         TypeRef::OwningKind owning = TypeRef::OwningKind::Borrow,
                         std::string lt = {}) {
        LogosTypeBuilder t; t.kind = LogosType::Kind::DstRef;
        t.struct_name = std::string(struct_name);
        t.pkg_name = std::string(pkg_name);
        t.mut_ptr = is_mut;
        t.type_args = std::move(type_args);
        t.lifetime = std::move(lt);   // THE REGION SLOT — see make_slice_type
        // Owning `Box<Foo>` custom-DST — same fat {data,len} layout + field/tail
        // access as a borrowed `&Foo`, but move-only + droppable (drop the tail
        // elements + prefix fields, then free the heap block). Rides in const_val
        // like Slice/TraitObject owning kinds.
        if (owning != TypeRef::OwningKind::Borrow)
            t.const_val = int64_t(uint8_t(owning));
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
    // {size, align} form — the actual computation; sema_abi_byte_size is the
    // size-only view of it. Leaf kinds come from LogosType::scalar_layout so
    // sema, mono and mlir-gen cannot disagree about a primitive's footprint.
    using AbiLayout = logos::compiler::layout::L;
    AbiLayout sema_abi_layout(TypeRef t, logos::compiler::StrSet& seen);
    // Sema's half of the niche rule — the two facts that need sema's registry
    // (`#[non_null]` 8-byte wrapper, a reference's pointee alignment). The
    // eligibility DECISION is `layout::classify_niche`, shared with mlir-gen.
    logos::compiler::layout::ArmDesc sema_niche_arm(TypeRef t,
                                                    logos::compiler::StrSet& seen);
    // Phase 2-1: cfg!() compile-time predicate evaluation. Walks the
    // ARGS items of a FN_MACRO_CALL with callee="cfg" and returns the
    // boolean truth value. Recursive on `all(...)`/`any(...)`/`not(...)`
    // combinators. Built-in keys (target_pointer_width, target_arch,
    // target_os, target_endian, target_family) resolved against
    // compile-target metadata. `feature = "name"` resolved against
    // `cfg_features_` (populated from --cfg flags / lforge manifest).
    bool evaluate_cfg_predicate(writ::TinyMapView fn_macro_call_node);
    // The single ARG of the cfg!() call is a predicate AST node — could
    // be a parenthesised IDENT (`target_os`), a key=value (e.g.
    // `target_os = "linux"`), or a combinator call (`all(...)`).
    // This helper interprets one predicate node.
    bool evaluate_cfg_node(writ::TinyMapView pred_node);
    // Phase 2-2: evaluate `#[cfg(...)]` attached to an item. Annotation
    // args parsed by the annotation grammar (ANNOT_KV / bare IDENT only
    // for MVP — combinators like `all(...)` not supported in attribute
    // form yet). Multi-arg list treats as conjunction.
    bool evaluate_cfg_annotation(writ::TinyMapView annotation_node);
    // §6.8: shared per-arg evaluator. Handles ANNOT_CALL (all/any/not
    // combinators — recursive), ANNOT_KV (key=lit), and bare-NAME
    // (flag). Called by evaluate_cfg_annotation for each entry of the
    // top-level annot_args list, and by itself for the children of an
    // ANNOT_CALL.
    bool evaluate_cfg_arg(writ::TinyMapView arg_node);
    // §6.8 / T0-6: the single conditional-compilation gate for an item's
    // accumulated annotation list — cfg_attr activation (mutates the list)
    // followed by cfg evaluation. Returns true when the item must be
    // dropped. Shared by the collection AND lowering walks.
    bool cfg_attrs_drop_item(std::vector<writ::TinyMapView>& pending_annots);
    // logos-core 1.3: fill `_` holes in a let-annotation from the RHS type.
    TypeRef fill_inferred_from_rhs(TypeRef ann, TypeRef rhs);
    // True when t contains a `_` (InferredType) hole at any depth.
    bool type_has_inferred(TypeRef t);

    // T2-14: CTFE-evaluate `node` with a ConstResolver bound to
    // module_const_values_, so a const PATH (`metacall { N }`, N a const)
    // folds. Use at every type/value-position CTFE site instead of a bare
    // ctfe::eval_expr — the resolver is what closes K10-co-06's PATH folding.
    logos::expected<logos::compiler::ctfe::CtfeValue,
                    logos::compiler::ctfe::CtfeError>
    ctfe_eval_const(writ::TinyMapView node, writ::MemHolder* h) noexcept;

    // ── array length ────────────────────────────────────────────────────
    // The resolved form of an ARR_LEN node. A length is EITHER concrete OR a
    // name — never both, and never neither: `ok == false` means it has been
    // diagnosed and the caller must not build a type from it.
    struct ArrayLen {
        bool        ok = true;
        uint64_t    value = 0;
        // A symbolic length: a plain const-param name, or (const-length-
        // overhaul) a deferred const EXPRESSION postfix-encoded under
        // ARR_LEN_EXPR_PFX. Either way it rides arr_size_var and folds to a
        // size at mono. Never a type.
        std::string symbolic;
    };
    // THE resolver. Every position that can carry a length calls this and
    // nothing else, so the type position and the expression position cannot
    // drift apart again.
    //
    // Zero is a legal length in EVERY position: `[T; 0]` is an empty array
    // type and `[v; 0]` is an empty array literal, exactly as in Rust. The
    // fill position used to reject it — an unregistered divergence that
    // survived only because the two positions had separate code.
    ArrayLen resolve_array_len(writ::TinyMapView len_node);

    // const-length-overhaul: encode a const-expression AST node (a BINOP /
    // UNARY / PAREN_EXPR tree over ARR_LEN leaves) as a postfix (RPN) string —
    // `#<int>` literal, `$<name>` const-param, arithmetic operators. Leaves are
    // resolved through resolve_array_len (reusing every atom path). Returns
    // nullopt on an unsupported form (a diagnostic is emitted). Shared by the
    // array-length position and the const-generic-argument position; the caller
    // folds it (eval_len_postfix) or defers it under ARR_LEN_EXPR_PFX.
    std::optional<std::string> build_const_expr_postfix(writ::TinyMapView node);

    // const-length-overhaul: the ctfe'd value of `<type_name>::<const_name>`
    // (any trait impl or inherent), for folding a `C::CONST` projection in a
    // length / const-arg at sema-time substitution. Mono uses its own
    // pre-indexed table (assoc_const_values_); this is the sema-side twin.
    std::optional<int64_t> sema_assoc_const_value(const std::string& type_name,
                                                  const std::string& const_name);

    // T2-29: is `t` an UNINHABITED type (no value can exist)? Never; an
    // empty enum or one whose every variant has an uninhabited payload; a
    // struct/tuple with an uninhabited field; `[T; N>0]` with uninhabited
    // T. Used to elide unconstructable match arms from exhaustiveness
    // (`match r { Ok(n) => … }` over `Result<i32, Void>`).
    bool is_type_uninhabited(TypeRef t, int depth = 0);

    // T1-8 (audit-v2, E0408 analog): every alternative of an or-pattern must
    // bind the same variable names. AST-level check, fired for TOP-LEVEL
    // match-arm alternations (`Some(x) | None => …`) from both match
    // lowering paths; the nested PatOr builder has its own equivalent.
    void check_or_alt_binding_consistency(writ::TinyMapView pat_or);
    void collect_ast_pat_bindings(writ::TinyMapView pat,
                                  std::vector<std::string>& out);

    // T1-7 (audit-v2): capture types per interned closure type, recorded at
    // closure-literal lowering and consumed by the auto-trait engine
    // (Send/Sync walk captures, not parameter types). Keyed by type_str of
    // the Closure TypeRef; same-signature literals UNION their captures
    // (conservative-correct). By-ref captures are stored as `&[mut] T`.
    std::unordered_map<std::string, std::vector<TypeRef>> closure_capture_env_;
    // Inferred Fn-family kind of each closure literal, keyed by the interned
    // closure type_str (same union-by-signature model as closure_capture_env_):
    // 0 = Fn (reads captures only), 1 = FnMut (mutates a capture), 2 = FnOnce
    // (consumes/moves a capture out of the env). Stored as the MAX (most
    // restrictive) across same-signature literals — conservative-correct: if any
    // literal of a signature is FnMut, a `F: Fn` bound over that type is refused.
    // ⚠ OPEN DEFECT, MEASURED, NOT FIXED (#69 class C — "a lookup KEY is not an
    // IDENTITY"). "Conservative-correct" is true only for SEND/SYNC-style facts
    // that are properties of a TYPE. The Fn-family kind is a property of a
    // LITERAL, and keying it by signature makes one literal's verdict answer for
    // another's:
    //     let mut n: i64 = 0i64;
    //     let mut h = || -> i64 { n = n + 1i64; return n; };   // FnMut
    //     let k = || -> i64 { return 9i64; };                  // Fn, mutates
    //     apply_val(k)                                         // nothing
    // is refused with "closure does not implement `Fn`" AT `k`. Deleting `h`, or
    // giving `k` a different signature, admits — so the refusal is `h`'s verdict
    // read through `k`'s key. This over-refuses any function holding two
    // same-signature closures where one mutates a capture; no generic, deem or
    // imported ingredient is needed. The imported witness is
    // tests/imported/pass/unboxed-closures/call-through-ref-to-fn-bound-b158
    // (line 26), which no gate watches.
    // The repair is per-LITERAL closure identity, not a wider key: closure types
    // intern by params/ret in make_closure_type, and the read site
    // (check_type_bounds in sema_collect.cpp) sees only a TypeRef, so nothing
    // short of giving the literal an identity that survives to the bound check
    // fixes it. That is a type-identity change with mono/mangling reach and is
    // filed as its own task rather than patched here.
    std::unordered_map<std::string, int> closure_kind_;
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
    // A #[rel_ptr] struct `RP<T>` is value-transparent to `*T`/`&T`/`&mut T`
    // (its compute form is an absolute thin ptr; only storage is a self-relative
    // offset). Accept the coercion both ways at value-flow sites.
    bool ptr_rel_compatible(TypeRef a, TypeRef b);
    // If `pointee` is a #[self_describing] custom-DST struct, return the fat
    // `DstRef` type for a `&`/`&mut` borrow of it (so `&*thin_ptr` types the
    // same as the `&Foo` annotation does via resolve_type); else null. The fat
    // len is materialized at codegen by calling `dst_len`. ref-repr §6.
    TypeRef self_describing_dst_ref(TypeRef pointee, bool is_mut);
    // `#[zone_mut]` fat-`&mut` contract (definitions in sema.cpp next to
    // self_describing_dst_ref; the rationale lives there).
    bool zone_mut_pointee(TypeRef pointee);
    bool reject_thin_zone_mut_ref(TypeRef pointee, TypeRef src_ref_t);
    bool reject_thin_zone_mut_nested(TypeRef expected, TypeRef actual, int depth = 0);
    // #95: a value ALREADY TYPED as a thin aggregate (`(&Sq, i64)`) arriving
    // where the fat one (`(&dyn Shape, i64)`) is wanted. An aggregate slot is
    // not a coercion site; a LITERAL at a coercion site has already been
    // stamped by retype_aggregate_lit_to before this runs, so what reaches
    // here has no literal to carry the expectation and is refused.
    bool reject_uncoerced_aggregate_unsize(TypeRef expected, TypeRef actual);
    // The same walk, asked as a QUESTION (no diagnostic). Needed because two
    // decision sites guard their `expect_type` with `!types_compatible(...)` —
    // and types_compatible is precisely what blanket-accepts this pair, so the
    // coercion/refusal would never be reached. Callers OR it into that guard.
    //
    // ⚠ THE TERMINATION GUARD IS A NODE BUDGET AND EXHAUSTING IT REFUSES.
    // The first cut of this walk carried `depth > 8 → return false`, i.e. it
    // answered "no uncoerced slot found" when it had merely STOPPED LOOKING —
    // the PERMISSIVE direction on a question whose `true` is a refusal. MEASURED
    // (/home/logos/sandbox/vfy95/d/dep01..dep12): nesting depth 1-8 refused,
    // depth ≥ 9 COMPILED AND SIGSEGVed (rc 139), and the same cap defeated the
    // impl check at depth (h2/g03_wrongtrait_deep.logos: depth 10 into
    // `&dyn Other` with `Sq: !Other`, rc 139). Depth is also the WRONG measure:
    // the walk's cost is the size of the type TREE, and type ALIASES make that
    // tree exponential in the source (`type A_{n} = (A_{n-1}, A_{n-1})` — 2^n
    // nodes from n lines; MEASURED, whole-compile 0.34 s at n=8, 1.83 s at
    // n=14). So the guard counts NODES, and running out of them is answered the
    // safe way: refuse, with its own diagnostic, rather than admit a value whose
    // fat half would be read out of a thin pointer.
    static constexpr int AGG_SLOT_NODE_BUDGET = 4096;
    bool find_uncoerced_aggregate_slot(TypeRef expected, TypeRef actual,
                                       const std::string& path,
                                       bool at_slot, std::string* out_path,
                                       TypeRef* out_exp, TypeRef* out_got,
                                       int* budget, bool* out_exhausted) const;
    bool aggregate_unsize_pending(TypeRef expected, TypeRef actual) const {
        int budget = AGG_SLOT_NODE_BUDGET;
        return find_uncoerced_aggregate_slot(expected, actual, "", false,
                                             nullptr, nullptr, nullptr,
                                             &budget, nullptr);
    }
    // Owning kind of the trait object (Borrow / Box / Rc / Arc). All four share
    // the fat-pair {data,vtable} layout and dispatch, but the owning kinds
    // differ in release semantics. The kind rides in the otherwise-unused
    // `const_val` slot (overloaded for TraitObject only — no schema change)
    // and is folded into TypeUID + equality so the four forms intern distinctly.
    // logos-core 2.4(c): the same const_val slot encodes auto-trait bounds in
    // its upper bits — bit 8 = `+ Send`, bit 9 = `+ Sync`. Folded into
    // TypeUID/equality so `&dyn T` and `&dyn T + Send` intern distinctly.
    using TraitOwningKind = TypeRef::OwningKind;
    static constexpr uint64_t TRAIT_BOUND_SEND_BIT = 1ull << 8;
    static constexpr uint64_t TRAIT_BOUND_SYNC_BIT = 1ull << 9;
    TypeRef make_trait_object(std::string_view tname,
                              std::vector<TypeRef> args = {},
                              TraitOwningKind owning = TraitOwningKind::Borrow,
                              bool req_send = false,
                              bool req_sync = false,
                              std::string lt = {}) {
        LogosTypeBuilder t; t.kind = LogosType::Kind::TraitObject;
        t.trait_name = std::string(tname);
        t.type_args = std::move(args);
        t.lifetime = std::move(lt);   // THE REGION SLOT — see make_slice_type
        uint64_t packed = uint8_t(owning);
        if (req_send) packed |= TRAIT_BOUND_SEND_BIT;
        if (req_sync) packed |= TRAIT_BOUND_SYNC_BIT;
        if (packed != 0)
            t.const_val = int64_t(packed);
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
    // re-implementation flips this to direct Writ-zone emission with no
    // caller change.
    LirBuilder builder() {
        return LirBuilder(*cur_prog_);
    }

    // ── Mirror ref accessors (read-only view of just-built L-IR nodes) ──
    // Every LirBuilder-constructed node has mirror_ptr_ set, so these
    // never return a null Ref unless `e` was built outside the builder.
    // LExprPtr is now lir_view::ExprRef — the expression IS its own mirror view,
    // so expr_ref_of is the identity. Kept as a named chokepoint for clarity.
    lir_view::ExprRef expr_ref_of(lir_view::ExprRef e) const noexcept {
        return e;
    }
    // LStmt is now lir_view::StmtRef — the statement IS its own mirror view,
    // so stmt_ref_of is the identity. Kept as a named chokepoint for clarity.
    lir_view::StmtRef stmt_ref_of(const lir_view::StmtRef& s) const noexcept {
        return s;
    }
    // Stage D4.1 chokepoint: the type of an L-IR expression read from its Writ
    // mirror (replaces the husk LExpr::type cache field, which goes away with the
    // skeleton). Null-safe via lir::eref. The single place to memoize if the
    // per-read TinyObjectMap lookup ever shows up in a profile ("optimize later").
    TypeRef expr_type(lir_view::ExprRef e) const noexcept {
        return e.type(cur_prog_->type_pool.impl());
    }
    // E0507: is `e` a place from which a MOVE-typed value cannot be moved out by
    // value — `*r` (deref of a `&`/`&mut` reference VARIABLE), `v[i]`/`s[i]`
    // (index of a NON-raw container, incl. `v[i]` over a user Index trait which
    // lowers to `*(v.index(i))`). Raw-pointer deref/index is exempt (unsafe; the
    // programmer's job — this is how mem/ptr/Vec primitives legitimately move
    // out, e.g. `let old = p[0]; p[0] = new` with p:*mut T). Box deref-move
    // (`*b` = Deref of a `deref()` call — LEGAL, Box owns its content) and
    // field-out-of-`&self` are NOT flagged here (documented).
    bool is_unowned_move_source(const lir::LExprPtr& e) {
        if (!e) return false;
        auto r = expr_ref_of(e);
        if (!r) return false;
        using Code = lir_schema::expr::Code;
        const auto* pool = cur_prog_->type_pool.impl();
        // A place whose chain passes through a RAW-pointer hop (`(*p).f[i]`,
        // `self.data[i]` with self:*mut) is unsafe — aliasing is the
        // programmer's responsibility, exempt from E0507. Walks receivers.
        std::function<bool(lir_view::ExprRef)> roots_through_raw =
            [&](lir_view::ExprRef x) -> bool {
            while (x) {
                auto t = x.type(pool);
                if (t && t.kind() == LogosType::Kind::Ptr) return true;
                switch (x.kind()) {
                    case Code::FieldRead:  x = lir_view::EFieldReadView{x}.receiver(); break;
                    // ⚠ THE EXEMPTION WALK MUST BE AT LEAST AS WIDE AS THE RULE
                    // IT EXEMPTS. This is the SAME switch's omission in the
                    // OVER-REFUSING direction: with TupleIndex now refusing on a
                    // reference receiver, a chain hopping through `p.0.f[i]`
                    // (p: *mut) would stop the walk here, miss the raw hop and
                    // be refused — inside the mem/ptr/Vec idiom the stdlib is
                    // built on. A gate's exemption has to be checked in the
                    // ABUSE direction and in the STARVE direction both.
                    case Code::TupleIndex: x = lir_view::ETupleIndexView{x}.receiver(); break;
                    case Code::IndexRead:  x = lir_view::EIndexReadView{x}.receiver(); break;
                    case Code::SliceIndex: x = lir_view::ESliceIndexView{x}.slice();   break;
                    case Code::Deref:      x = lir_view::EDerefView{x}.operand();       break;
                    default: return false;
                }
            }
            return false;
        };
        if (roots_through_raw(r)) return false;
        auto is_raw = [&](lir_view::ExprRef x) {
            auto t = x ? x.type(pool) : TypeRef(nullptr);
            return t && t.kind() == LogosType::Kind::Ptr;
        };
        // `v[i]` over a user Index lowers to `*(v.index(i))`, and `*x` over a
        // user Deref to `*(x.deref())` — Deref(MethodCall/Call
        // index|index_mut|deref|deref_mut). Moving a move-typed value out of any
        // of these is E0507: index/deref return `&Output`, so a by-value move
        // out of the pointee duplicates the owner (double-free). This includes
        // Box (`let s = *b`): Rust's `Box` has built-in DerefMove that Logos
        // does NOT implement — left unchecked it bit-copies the content and both
        // the copy and the Box's Drop free it (double-free, abort). Rejecting it
        // turns silent UB into a clear error (Rc/user-Deref move-out is E0507 in
        // Rust too; only Box is the exception, and only with real DerefMove
        // support — a separate codegen feature).
        auto is_deref_or_index_call = [&](lir_view::ExprRef op) {
            auto tail = [](std::string_view c, std::string_view suf) {
                return c.size() >= suf.size() &&
                       c.substr(c.size() - suf.size()) == suf;
            };
            if (!op) return false;
            if (op.kind() == Code::MethodCall) {
                auto m = lir_view::EMethodCallView{op}.method();
                return m == "index" || m == "index_mut" ||
                       m == "deref" || m == "deref_mut";
            }
            if (op.kind() == Code::Call) {
                std::string_view c = lir_view::ECallView{op}.callee();
                return tail(c, "__index") || tail(c, "__index_mut") ||
                       tail(c, "__deref") || tail(c, "__deref_mut");
            }
            return false;
        };
        switch (r.kind()) {
            case Code::IndexRead:
                return !is_raw(lir_view::EIndexReadView{r}.receiver());
            case Code::SliceIndex:
                return !is_raw(lir_view::ESliceIndexView{r}.slice());
            case Code::Deref: {
                auto op = lir_view::EDerefView{r}.operand();
                if (op && op.kind() == Code::VarRef) {
                    auto ot = op.type(pool);
                    return ot && (ot.kind() == LogosType::Kind::Ref ||
                                  ot.kind() == LogosType::Kind::MutRef);
                }
                return is_deref_or_index_call(op);
            }
            case Code::FieldRead: {
                // Move a move-typed field out of a `&`/`&mut` receiver
                // (`fn f(r:&S)->T{r.field}`) — E0507. Owned receivers (by-value
                // self / locals) are partial moves (allowed) — their receiver
                // type is a Struct, not a reference.
                auto recv = lir_view::EFieldReadView{r}.receiver();
                auto rt = recv ? recv.type(pool) : TypeRef(nullptr);
                return rt && (rt.kind() == LogosType::Kind::Ref ||
                              rt.kind() == LogosType::Kind::MutRef);
            }
            // ⚠ A TUPLE ELEMENT IS A FIELD WHOSE NAME IS ITS INDEX, and this
            // switch said so for `s.f` and not for `t.0`, so `fn f(r:&(S,i64))
            // -> S { return r.0; }` moved a Box owner out of a shared reference
            // and duplicated it. The struct spelling of the identical program
            // refused E0507.
            // ⚠ THE RECEIVER GATE IS LOAD-BEARING, NOT COPIED FOR SYMMETRY. A
            // bare `return true` here would refuse `fn f(t:(S,i64)) -> S
            // { return t.0; }` — an OWNED tuple partial move, which is legal
            // and compiles today. Measured both directions before and after.
            case Code::TupleIndex: {
                auto recv = lir_view::ETupleIndexView{r}.receiver();
                auto rt = recv ? recv.type(pool) : TypeRef(nullptr);
                return rt && (rt.kind() == LogosType::Kind::Ref ||
                              rt.kind() == LogosType::Kind::MutRef);
            }
            default: return false;
        }
    }
    lir_view::PatRef pat_ref_of(const lir::Pattern& p) const noexcept {
        if (p.mirror_ptr_ == nullptr) return {};
        return lir_view::PatRef(cur_prog_->type_pool.arena(), p.mirror_ptr_);
    }

    template<typename K>
    lir_view::StmtRef make_stmt_emit(uint32_t line, K&& k) {
        struct { uint32_t line = 0; const uint8_t* mirror_ptr_ = nullptr; } s;
        s.line = line;
        if (cur_prog_) {
            using KT = std::decay_t<K>;
            auto& p = *cur_prog_;
            if constexpr (std::is_same_v<KT, lir::SLet>) {
                // Phase-1: resolve the binding's dense slot here (single SLet→
                // mirror chokepoint), so every lower_let* site is covered at
                // once. define() ran before this for the binding, so lookup_slot
                // returns the just-assigned slot; NO_SLOT for un-define()'d
                // synthetic temps (downstream name-keys those).
                s.mirror_ptr_ = lir_mirror_emit_let(p, line, k.name, k.type, k.value,
                                                       k.is_mut, lookup_slot(k.name),
                                                       k.compiler_glue);
            } else if constexpr (std::is_same_v<KT, lir::SAssign>) {
                s.mirror_ptr_ = lir_mirror_emit_assign(p, line, k.name, k.value);
            } else if constexpr (std::is_same_v<KT, lir::SReturn>) {
                s.mirror_ptr_ = lir_mirror_emit_return(p, line, k.value);
            } else if constexpr (std::is_same_v<KT, lir::SIf>) {
                lir_view::BlockRef eb = k.else_.has_value() ? *k.else_ : lir_view::BlockRef{};
                s.mirror_ptr_ = lir_mirror_emit_if_stmt(p, line, k.cond, k.then_, eb);
            } else if constexpr (std::is_same_v<KT, lir::SWhile>) {
                s.mirror_ptr_ = lir_mirror_emit_while(p, line, k.cond, k.body, k.label);
            } else if constexpr (std::is_same_v<KT, lir::SFor>) {
                s.mirror_ptr_ = lir_mirror_emit_for(p, line, k.var, k.lo, k.hi, k.inclusive, k.body, k.label, k.slot);
            } else if constexpr (std::is_same_v<KT, lir::SLoop>) {
                s.mirror_ptr_ = lir_mirror_emit_loop(p, line, k.body, k.label, k.break_slot, k.result_type);
            } else if constexpr (std::is_same_v<KT, lir::SBreak>) {
                s.mirror_ptr_ = lir_mirror_emit_break(p, line, k.value, k.label);
            } else if constexpr (std::is_same_v<KT, lir::SContinue>) {
                s.mirror_ptr_ = lir_mirror_emit_continue(p, line, k.label);
            } else if constexpr (std::is_same_v<KT, lir::SBlock>) {
                s.mirror_ptr_ = lir_mirror_emit_block_stmt(p, line, k.body, k.transparent);
            } else if constexpr (std::is_same_v<KT, lir::SFieldWrite>) {
                s.mirror_ptr_ = lir_mirror_emit_field_write(p, line, k.receiver, k.field, k.value);
            } else if constexpr (std::is_same_v<KT, lir::SIndexWrite>) {
                s.mirror_ptr_ = lir_mirror_emit_index_write(p, line, k.arr, k.index, k.value);
            } else if constexpr (std::is_same_v<KT, lir::SFieldIndexWrite>) {
                s.mirror_ptr_ = lir_mirror_emit_field_index_write(p, line, k.receiver, k.field, k.index, k.value);
            } else if constexpr (std::is_same_v<KT, lir::SExprStmt>) {
                s.mirror_ptr_ = lir_mirror_emit_expr_stmt(p, line, k.expr);
            } else if constexpr (std::is_same_v<KT, lir::SMatch>) {
                s.mirror_ptr_ = lir_mirror_emit_match_stmt(p, line, k.scrut, k.arms);
            } else if constexpr (std::is_same_v<KT, lir::SForEach>) {
                s.mirror_ptr_ = lir_mirror_emit_for_each(p, line, k.var, k.iter, k.elem_type, k.arr_size, k.is_slice, k.body, k.slot);
            } else if constexpr (std::is_same_v<KT, lir::SDerefWrite>) {
                s.mirror_ptr_ = lir_mirror_emit_deref_write(p, line, k.ptr, k.value);
            } else if constexpr (std::is_same_v<KT, lir::SDrop>) {
                s.mirror_ptr_ = lir_mirror_emit_drop(p, line, k.var_name, k.drop_fn, k.type, k.drop_fields, k.moved_fields);
            } else if constexpr (std::is_same_v<KT, lir::SDerefFieldWrite>) {
                s.mirror_ptr_ = lir_mirror_emit_deref_field_write(p, line, k.receiver, k.type_name, k.field, k.value);
            } else if constexpr (std::is_same_v<KT, lir::STupleWrite>) {
                s.mirror_ptr_ = lir_mirror_emit_tuple_write(p, line, k.receiver, k.index, k.value, k.recv_type);
            } else if constexpr (std::is_same_v<KT, lir::SLetElse>) {
                s.mirror_ptr_ = lir_mirror_emit_let_else(p, line, k.pat, k.scrut, k.else_block, k.guards);
            } else if constexpr (std::is_same_v<KT, lir::SChainFieldWrite>) {
                s.mirror_ptr_ = lir_mirror_emit_chain_field_write(p, line, k.receiver, k.mid_field, k.extras, k.field, k.value);
            } else {
                static_assert(sizeof(K) == 0, "make_stmt_emit: unknown stmt kind");
            }
        }
        (void)k;  // payload swallowed — only mirror_ptr_ matters now
        if (s.mirror_ptr_ == nullptr) return lir_view::StmtRef{};
        return lir_view::StmtRef(cur_prog_->type_pool.arena(), s.mirror_ptr_);
    }

    template<typename K>
    lir::WritValPtr alloc_hv_emit(K&& k) {
        using KT = std::decay_t<K>;
        auto& p = *cur_prog_;
        const uint8_t* mo = nullptr;
        if constexpr (std::is_same_v<KT, lir::WVNull>) {
            mo = lir_mirror_emit_hv_null(p);
        } else if constexpr (std::is_same_v<KT, lir::WVBool>) {
            mo = lir_mirror_emit_hv_bool(p, k.value);
        } else if constexpr (std::is_same_v<KT, lir::WVInt>) {
            mo = lir_mirror_emit_hv_int(p, k.value);
        } else if constexpr (std::is_same_v<KT, lir::WVFloat>) {
            mo = lir_mirror_emit_hv_float(p, k.value);
        } else if constexpr (std::is_same_v<KT, lir::WVStr>) {
            mo = lir_mirror_emit_hv_str(p, k.value);
        } else if constexpr (std::is_same_v<KT, lir::WVMap>) {
            mo = lir_mirror_emit_hv_map(p, k.entries, k.key_type);
        } else if constexpr (std::is_same_v<KT, lir::WVArray>) {
            mo = lir_mirror_emit_hv_array(p, k.elements, k.elem_type);
        } else if constexpr (std::is_same_v<KT, lir::WVCapture>) {
            mo = lir_mirror_emit_hv_capture(p, k.param_index, k.value_index);
        } else if constexpr (std::is_same_v<KT, lir::WVType>) {
            mo = lir_mirror_emit_hv_type(p, k.kind, k.uid, k.name);
        } else {
            static_assert(sizeof(K) == 0, "alloc_hv_emit: unknown WritVal payload");
        }
        (void)k;  // payload consumed by per-kind dispatch above
        auto* hv = lir::alloc_writ_val(*cur_prog_);
        hv->mirror_ptr_ = mo;
        return hv;
    }

    // ── File / line tracking ─────────────────────────────────────

    const std::vector<std::string>* filenames_    = nullptr;
    const std::vector<bool>*        from_binary_  = nullptr;
    // Phase 6 (multi-arena IR) item-level lazy. Parallel to from_binary_;
    // per-AST flag indicating the source archive shipped only parsed AST
    // (no .o, no LIR blob — see ModuleManifest::lazy). Stamped onto
    // FunctionDraft.from_lazy_module; consumed by post-mono reach analysis to
    // skip mlir-gen for lazy fns not reached from any non-lazy caller.
    const std::vector<bool>*        is_lazy_      = nullptr;
    // Module system: per-AST owning-module id, parallel to from_binary_.
    // Empty/null → no module (plain user program); a non-empty id is baked
    // into the symbol mangle so same-named packages from different modules
    // (or versions) get distinct symbols (C++ module-linkage model).
    const std::vector<std::string>* module_ids_   = nullptr;
    // UnitGraph §1.2: per-AST compile-unit key, parallel to filenames_.
    const std::vector<std::string>* ast_unit_key_ = nullptr;
    // Module system: package dotted-name → owning-module id, accumulated across
    // all files (own + binary). Copied into LProgram::pkg_module_ids so mono
    // can module-qualify synthesised method-call symbols. One package maps to
    // one module in a coherent build.
    std::unordered_map<std::string, std::string> pkg_module_ids_;
    // G156-1: bare nominal names declared in ≥2 DISTINCT packages across the full
    // transitive type universe (own + binary deps). Built post-collect from
    // structs_/enums_ and threaded via set_ambiguous_type_names so the type-arg
    // manglers tag ONLY genuine cross-package collisions. Lives on the checker so
    // the installed pointer stays valid through lower_program's mangling.
    std::unordered_set<std::string> ambiguous_type_names_;
    // §3: module canonical NAME → id (from SemaOptions; resolves `use pkg from
    // <name>`). nullptr/empty → `from` clauses can't resolve.
    const std::unordered_map<std::string, std::string>* module_name_to_id_ = nullptr;
    std::string  file_;
    std::string  cur_package_;
    lir::LProgram* cur_prog_ = nullptr;  // set during lower_module_items, used by lower_generic_call
    bool         cur_from_binary_ = false;   // current file is from a binary module
    std::string  cur_module_id_;             // owning-module id of the current file (mangle key)
    // UnitGraph §1.2: compile-unit key of the current file. Kept in lockstep
    // with file_ at every place file_ is assigned — a fn stamped with a stale
    // key would be scheduled into the wrong object, so the two move together.
    std::string  cur_unit_key_;
    // The unit key for ast index `ai`. Delegates to THE rule in unit_graph.hpp
    // — sema stamps functions with it and UnitGraph::build creates nodes with
    // it, so a function can never be stamped into a unit the graph never made.
    // Sema never stamps an empty key, so an EMPTY UNIT_KEY on a function means
    // exactly "undeclared, owner derived" (mono clone / dependency body).
    std::string unit_key_for_ast(size_t ai) const {
        static const std::vector<std::string> kEmpty;
        return logos::compiler::unit_key_for_ast(filenames_ ? *filenames_ : kEmpty,
                                                 ast_unit_key_ ? *ast_unit_key_ : kEmpty,
                                                 ai);
    }
    bool         cur_from_lazy_   = false;   // current file is from a lazy archive
    uint32_t     node_line_ = 0;

    // Per-file import scope (wildcard: `use foo.bar;` makes all pub symbols of foo.bar visible)
    struct ImportScope {
        std::vector<std::string> wildcard_packages;
        // §3: `use pkg from <module>;` — package dotted-name → REQUIRED owning
        // module id. A candidate for such a package is visible only if its
        // fi->module_id matches. Absent ⇒ plain `use pkg;` (any module).
        std::unordered_map<std::string, std::string> pkg_from_module_id;
        // CP-cm-02: `use pkg.Path.Type.{V1, V2, …};` brings enum variants
        // into bare scope. Map keyed by the bare variant name → the dotted
        // enum-type qualifier so lookup paths can resolve `V1` as if it
        // were written `Type::V1`. Last-write-wins on name collision.
        std::unordered_map<std::string, std::string> variant_aliases;
    };
    ImportScope cur_imports_;

    // Qualified key: "pkg::name" or "name" if pkg empty
    //
    // ⚠ THE PACKAGE HALF IS CHECKED HERE. Eleven sites across sema.cpp /
    // sema_collect.cpp / this header take a symbol-table key apart again at
    // `::` — `SemaCache::reset_user_state`, `take_snapshot`, `install_snapshot`,
    // `run` (×3), `compute_auto_copy_types`, the StableLayout field check, the
    // B-at-01 annotation census, `check_recursive_value_types` (×2). Every one
    // of them cuts to recover the PACKAGE half, and every one is correct only
    // while the key holds AT MOST ONE separator. That is a whole-program
    // property, and a whole-program property defended by a comment is exactly
    // how `Mono::concrete_impls_` stayed a latent miscompile for as long as it
    // did: its `k.find("::")` rested on a comment asserting the left operand is
    // a bare trait name. So: a CHECK, not a comment.
    //
    // Package spellings cannot contain `::` by construction —
    // `SemaChecker::read_package_name` joins `la::mod::PATH_PARTS` with `"."` —
    // and this asserts that where a future qualified package would first
    // arrive. Always-on (not NDEBUG-gated): a check that vanishes in the
    // shipping build is not the instrument this class needs.
    //
    // ⚠ AND THE `name` HALF IS **NOT** CHECKED HERE — MEASURED, not assumed.
    // Checking it aborts the stdlib build immediately:
    //   sema_key(pkg='logos.lang.fabric', name='PrimVec$G1$DT::Prim')
    // from `lookup_qualified_` ← `find_struct_by_name` ← `lower_field_read`.
    // sema_key is BOTH the key composer (at the ~20 collect/insert sites) and
    // the PROBE composer (`lookup_qualified_` builds a candidate key per
    // package), and the probe's `name` is a user-written type spelling, which
    // for an un-normalised associated-type projection legitimately contains
    // `::`. Such a probe never matches and never inserts, so it is harmless —
    // but it means "no operand is qualified" is FALSE as a global statement.
    // The property the splitters actually need is about the keys that are
    // STORED, and it is enforced over the stored keys by
    // `check_symbol_key_separators` (called from `run`), which is blind to who
    // composed them and therefore also covers keys built by raw `+ "::" +`.
    [[noreturn]] static void sema_key_qualified_operand(
            const char* which, std::string_view pkg, std::string_view name) {
        std::fprintf(stderr,
            "logosc INTERNAL: sema_key(%s) operand is already qualified: "
            "pkg='%.*s' name='%.*s'\n"
            "  The symbol-table key \"pkg::name\" is split back apart at `::` by "
            "eleven sites in sema; a qualified operand makes every one of those "
            "splits cut in the wrong place. Carry the parts (a pair/tuple key) "
            "instead of composing a string that must be re-parsed.\n",
            which, (int)pkg.size(), pkg.data(), (int)name.size(), name.data());
        std::abort();
    }
    static std::string sema_key(std::string_view pkg, std::string_view name) {
        if (pkg.find("::") != std::string_view::npos)
            sema_key_qualified_operand("pkg", pkg, name);
        if (pkg.empty()) return std::string(name);
        std::string r;
        r.reserve(pkg.size() + 2 + name.size());
        r.append(pkg); r.append("::"); r.append(name);
        return r;
    }

    // THE STORED-KEY INVARIANT the `rfind("::")` splitters depend on.
    //
    // Two different cuts are in use over these tables and they only agree while
    // a key holds AT MOST ONE separator:
    //   * FIRST cut (`key.find("::")`) — `SemaCache::reset_user_state` and
    //     `take_snapshot`'s `erase_pkg_key`; wants only a separator-free PACKAGE
    //     half, which `sema_key`'s own check above already guarantees.
    //   * LAST cut (`key.rfind("::")`) — `install_snapshot`'s const-index
    //     rebuild over `module_consts_`, `run`'s `bare_of` over
    //     `structs_`/`enums_`, `run`'s layout-recording sweep and
    //     `compute_auto_copy_types` and the StableLayout field scan over
    //     `structs_`, `sema_collect`'s B-at-01 census over `datatypes_`, and
    //     `check_recursive_value_types` over `structs_`/`enums_`.
    // A key with TWO separators makes those two cuts disagree — the first cut
    // yields the real package, the last yields `pkg::Outer` as the "package"
    // and a fragment as the "bare name". That is the `Mono::concrete_impls_`
    // failure mode exactly, and nothing about the text of either call site
    // reveals it; only the population of the map does. So the check is over the
    // POPULATION, not over the call sites, and it is therefore blind to which
    // composer built the key — including keys built by a raw `+ "::" +` rather
    // than by `sema_key`.
    //
    // NOT audited, deliberately: `explicit_type_codes_` (keyed
    // `pkg::Name<Args>`, whose `<Args>` may legitimately carry `::`) and
    // `type_aliases_`/`generic_consts_`, which are only ever FIRST-cut; and
    // `traits_`, whose one `::` site (`resolve_trait_query_name`) is a
    // `== npos` presence test that extracts no substring.
    void check_symbol_key_separators() const {
        auto audit = [](const char* which, const auto& map) {
            for (auto& kv : map) {
                std::string_view k(kv.first);
                auto a = k.find("::");
                if (a == std::string_view::npos) continue;
                if (k.find("::", a + 2) == std::string_view::npos) continue;
                std::fprintf(stderr,
                    "logosc INTERNAL: %s key '%.*s' carries more than one `::`\n"
                    "  Sema recovers the package half of these keys with BOTH "
                    "find(\"::\") and rfind(\"::\"); with two separators the two "
                    "cuts disagree and the bare name silently becomes a fragment. "
                    "Carry the parts (a pair/tuple key) instead of a string that "
                    "must be re-parsed.\n",
                    which, (int)k.size(), k.data());
                std::abort();
            }
        };
        audit("structs_", structs_);
        audit("enums_", enums_);
        audit("datatypes_", datatypes_);
        audit("module_consts_", module_consts_);
    }

    // B-mv-03: the impl registry carries TWO key spellings for one impl — the
    // RAW `Trait::Target` that ~50 bare-text probes across sema compose, and
    // the IDENTITY `pkg::Trait::Target` that a canonicalised bound asks with.
    // Carrying both is a deliberate choice (re-keying would move the bare slot
    // under a homonym and silence the `Drop::`/`Copy::`/`Index::` gates), and a
    // carried split needs a MECHANICAL check, not a comment — a comment is what
    // protected the last separator invariant, and it did not.
    //
    // THE INVARIANT: an impl whose trait identity differs from its written
    // spelling is filed under BOTH keys. If the alias insert in collect_impl is
    // dropped, weakened, or made conditional on something else, the identity
    // probes go quiet — a bound refusal that finds nothing looks exactly like a
    // bound that was never asked — and this aborts instead.
    //
    // Over the POPULATION, not the call sites: it is blind to which composer
    // built the key, so it holds for entries that entered through the `str` →
    // `&[u8]` alias and for impls re-entering from a binary dependency's LIR
    // just as it does for the primary insert.
    void check_impl_registry_key_identity() const {
        for (auto& kv : impls_) {
            const auto& info = kv.second;
            if (info.canonical_trait.empty() ||
                info.canonical_trait == info.trait_name) continue;
            const std::string bare  = info.trait_name + "::" + info.target_type;
            const std::string ident = info.canonical_trait + "::" + info.target_type;
            // The entry under `kv.first` proves one of the two exists; require
            // the OTHER. (`kv.first` may be neither when the target was mangled
            // on the way in — `$ref_`/`&[u8]` — so check both explicitly.)
            bool have_bare  = impls_.count(bare) != 0  || kv.first == bare;
            bool have_ident = impls_.count(ident) != 0 || kv.first == ident;
            if (have_bare && have_ident) continue;
            std::fprintf(stderr,
                "logosc INTERNAL: impls_ entry '%s' is filed under only %s key\n"
                "  impl '%s' for '%s' resolves to trait identity '%s'. The registry "
                "must hold BOTH the raw key '%s' (bare-text probes: Drop::, Copy::, "
                "Index::, the prefix-strip sweeps) and the identity key '%s' (a "
                "canonicalised bound's lookup). One without the other means a bound "
                "over a homonym trait silently answers from the wrong trait's impls, "
                "or answers nothing at all and reads as a clean refusal.\n",
                kv.first.c_str(), have_bare ? "the raw" : "the identity",
                info.trait_name.c_str(), info.target_type.c_str(),
                info.canonical_trait.c_str(), bare.c_str(), ident.c_str());
            std::abort();
        }
    }

    // B-mv-03: the GROUND for a bound refusal — WHICH trait the written name
    // denotes here and WHERE the registry was consulted. Empty when the bound's
    // trait owns the bare slot: there is no homonym to disambiguate and the
    // message would only add noise.
    std::string bound_lookup_ground(const TraitBound& b) const {
        if (b.canonical_trait.empty() || b.canonical_trait == b.trait_name)
            return {};
        return std::format(
            " — here '{}' denotes the trait registered as '{}', and the impl "
            "registry was searched under that identity; a different same-named "
            "trait owns the bare name '{}' and its impls do NOT satisfy this bound",
            b.trait_name, b.canonical_trait, b.trait_name);
    }

    // #114 — A TYPEVAR ARGUMENT IS A MOVE UNLESS ITS PARAMETER IS `Copy`-BOUND.
    //
    // The three INDIRECT-call arms (`f(args)` through a callable variable, and
    // the two expression-as-callee arms) used to skip EVERY TypeVar argument
    // when marking by-value args moved, on the ground that "`T: Copy` → passed
    // by copy, reused after the call". That blanket skip bought the suppression
    // of one over-refusal at the price of a DOUBLE FREE for every non-Copy `T`.
    // MEASURED at 93e123df with a heap-owning payload:
    //     fn go<Item>(v: Item, f: fn(Item) -> bool) { f(v); }
    //     go::<Inner>(mk(1), pred)
    //   -> `DROP n=1` twice, `free(): double free detected in tcache 2`, rc 134.
    // The direct-call path (`track_args_moved`) has no such skip, which is why
    // the same program with `eat::<Item>(v)` is clean — the two paths disagreed.
    // That two-line program is the whole of #114: `Iterator::any`/`all`/
    // `position`/`fold` reach it because a trait default calls its `FnMut`
    // parameter with a by-value `Item`.
    //
    // The question decidable AT SEMA is not "will the instantiation be Copy"
    // (it is a TypeVar; it is not known here) but Rust's own rule: does the
    // PARAMETER carry a `Copy` bound. An unbounded `T` is a move type at the
    // generic site even when some instantiation happens to be Copy — Rust
    // refuses reuse-after-move there too. So: skip only the Copy-BOUNDED ones.
    bool typevar_param_is_copy_bounded(TypeRef t) const {
        if (!t || TypeRef(t).kind() != LogosType::Kind::TypeVar) return false;
        // KEY-IDENTITY: a TYPE-PARAMETER name, scoped to the signature being
        // checked — see SemaChecker::normalize_assoc_eq for the full ground.
        auto it = current_type_bounds_.find(
            std::string(TypeRef(t).type_var_name()));
        if (it == current_type_bounds_.end()) return false;
        // A bound's written name may be bare (`Copy`) or the registry's
        // canonical identity (`logos.lang.marker::Copy`) — #97/#100: a lookup
        // KEY is not an IDENTITY, so compare the trailing segment of both.
        auto last_seg = [](std::string_view s) -> std::string_view {
            auto p = s.find_last_of(":.");
            return p == std::string_view::npos ? s : s.substr(p + 1);
        };
        for (auto& b : it->second)
            if (last_seg(b.trait_name) == "Copy" ||
                last_seg(b.canonical_trait) == "Copy") return true;
        return false;
    }

    uint32_t     tmp_var_count_ = 0;   // for generating unique internal names

    uint32_t get_line(writ::TinyMapView node) noexcept {
        using namespace sema_detail;
        if (node.is_null()) return 0;
        AnyVal av = node.get(la::SRC_LINE.code);
        if (av.is_null() || !av.is_value()) return 0;
        return av.as_value<uint32_t>();
    }

    // ── meta @{} helpers ─────────────────────────────────────────


    // ── Writ helpers ───────────────────────────────────────────

    writ::MemHolder* holder_ = nullptr;

    // Slice 7 of metaprog-quote: lower_writ_blob may build a Writ doc
    // from blob bytes (when the blob carries an AST fragment) and recurse
    // into lower_expr while pointing holder_ at the new doc's holder.
    // The Writ objects must outlive the recursion AND any LIR mirror
    // back-fill that runs later — keep them alive for the SemaChecker's
    // lifetime by stashing here.
    std::vector<writ::Writ> blob_docs_;

    int32_t code_of(writ::TinyMapView node) noexcept {
        using namespace sema_detail;
        if (node.is_null()) return -1;
        AnyVal av = node.get(la::CODE.code);
        return av.is_null() ? -1 : av.as_value<int32_t>();
    }

    std::string_view str_of(writ::AnyVal av) noexcept {
        using namespace sema_detail;
        if (av.is_null()) return {};
        return StringView(av, holder_).view();
    }

    writ::TinyMapView map_of(writ::AnyVal av) noexcept {
        using namespace sema_detail;
        if (av.is_null()) return TinyMapView{};
        // Type-checked: a non-TOM pointee (string / array — e.g. `$...`
        // collector junk or antiquot leftovers in post-subst synth docs)
        // yields the null view instead of a garbage map over foreign bytes.
        // Callers already handle null; before this, walking an arbitrary
        // emitted doc (--gen-dir render) could segfault on such elements.
        if (!av.is_pointer()) return TinyMapView{};
        const uint8_t* p = av.resolve();
        if (!p || logos::writ::TypeTag::read_before(p).type_code()
                      != logos::writ::type_hash::TinyObjectMap)
            return TinyMapView{};
        return TinyMapView(av, holder_);
    }

    // §4 module system: read the `pub(module)` marker from an item decl node's
    // VIS sub-node (set by the grammar `pub_vis` rule). Returns true for
    // `pub(module)` (module-linkage); false for plain `pub` / no VIS / non-pub.
    // Validates the contextual word == "module" (errors on e.g. `pub(crate)`).
    bool read_module_vis(writ::TinyMapView node) {
        using namespace sema_detail;
        if (!node.has_key(la::VIS)) return false;
        auto vav = node.get(la::VIS.code);
        if (vav.is_null() || !vav.is_pointer()) return false;
        auto vis = map_of(vav);
        if (!vis.has_key(la::NAME)) return false;           // plain `pub`
        std::string w(str_of(vis.get(la::NAME.code)));
        if (w.empty()) return false;
        if (w != "module") {
            error(std::format("unsupported visibility `pub({})` — only "
                              "`pub(module)` is recognised", w));
            return false;
        }
        return true;
    }

    writ::ArrayView arr_of(writ::AnyVal av) noexcept {
        using namespace sema_detail;
        return ArrayView(av, holder_);
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
    std::vector<writ::TinyMapView> collect_arg_asts(writ::TinyMapView node) noexcept {
        using namespace sema_detail;
        std::vector<writ::TinyMapView> out;
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
    std::vector<lir::LExprPtr> lower_call_args(writ::TinyMapView node) {
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
    // FQN-checked: the stdlib owned-box lang type `logos.mem.boxed.Box<T>`,
    // NOT a user struct that merely happens to be named "Box". Matching by bare
    // name would let a user `struct Box` in their own package hijack the box
    // special-casing (unsize / owning-drop / deref) — a real shadowing hazard
    // (cf. the earlier user-`Rc`-vs-stdlib-`Rc` collision). pkg is tolerated
    // empty for internal paths where it was stripped; a user Box always carries
    // its own (non-boxed) package, so the collision case is still rejected.
    static bool is_stdlib_box(TypeRef t) {
        if (!is_named_struct(t, "Box")) return false;
        auto pkg = TypeRef(t).pkg_name();
        return pkg.empty() || pkg == "logos.mem.boxed";
    }
    // FQN-checked, the same shape and for the same reason as is_stdlib_box.
    // `AnyVal` is the one member of this family with NO declaration anywhere:
    // MEASURED over stdlib/, tests/ and the 6132 generated units under
    // build/tests/logos/facts/*/gen/, `struct AnyVal` occurs exactly ONCE
    // tree-wide — in tests/logos/ir/param_attrs_freeze_lattice.logos, as a USER
    // struct. Its only producer is the compiler's own make_struct_type("AnyVal")
    // on the writ path, and that carries NO package: a census over a full 53-
    // target stdlib+examples build saw `is_anyval` answer YES with an EMPTY pkg
    // and with nothing else. So `pkg.empty()` here is the DISCRIMINATOR, not a
    // tolerance hatch — a user `struct AnyVal` always carries its declaring
    // package, and before this it was accepted, lowered to an i32 (mlir_gen_types
    // getI32Type / {4,4} / "no interior pointers") and its i64 field read back as
    // garbage that varied run to run, with no diagnostic (task #99).
    // ⚠ RESIDUAL, unproven: make_struct_type resolves an omitted pkg from the
    // declaration table BY BARE NAME, so a module that BOTH declares its own
    // `struct AnyVal` AND drives the writ path would hand the synthesised type
    // the user's package and be refused here. That direction is an over-refusal,
    // not a wrong answer, and no program in the corpus takes it.
    static bool is_anyval(TypeRef t) {
        return is_named_struct(t, "AnyVal") && TypeRef(t).pkg_name().empty();
    }
    // FQN-checked (task #99). `WritStatic` is declared ONCE tree-wide, in
    // stdlib/lang/writ/wstatic.logos, package `logos.lang.writ.wstatic` — NOT
    // `logos.lang.writ` and not the directory path; the `package` line is the
    // truth. A census over a full 53-target stdlib+examples build and over all
    // 7406 corpus .logos files (5887 of which compile) saw this predicate answer
    // YES for that package and for NOTHING else — never for an empty package —
    // so there is no `pkg.empty()` tolerance here to argue about: it would be a
    // hatch with no measured user.
    static bool is_writ_static(TypeRef t) {
        return is_named_struct(t, "WritStatic") &&
               TypeRef(t).pkg_name() == "logos.lang.writ.wstatic";
    }
    // FQN-checked (task #99). Declared once, stdlib/lang/writ/container.logos:29,
    // package `logos.lang.writ.container`.
    // ⚠ CELL: UNEXERCISED, not dead — same argument as is_string_view. The
    // census never saw it answer YES, and its own call site says why: the live
    // runtime container is `Rc<Writ>`, matched at sema_expr by a SEPARATE
    // disjunct, and the bare `Writ` spelling this predicate names is the legacy
    // one. The type is live in stdlib, so the arm is qualified, not deleted; the
    // B-he-05 `WritStatic ← Writ` diagnostic it gates stays reachable in
    // principle. No fixture can see it either way.
    static bool is_writ(TypeRef t) {
        return is_named_struct(t, "Writ") &&
               TypeRef(t).pkg_name() == "logos.lang.writ.container";
    }
    // FQN-checked (task #99). Declared once, stdlib/lang/writ/wstatic.logos:36,
    // package `logos.lang.writ.wstatic` (the same module as WritStatic).
    // ⚠ CELL: UNEXERCISED, not dead. The census over a full stdlib+examples
    // build and all 7406 corpus .logos never saw this predicate answer YES —
    // but the TYPE is live (wstatic.logos uses it); what no corpus program does
    // is capture a StringView into a writ `${…}`, which is this predicate's one
    // call site (the capture-legality switch in sema_expr). So the arm is
    // QUALIFIED rather than deleted: deleting it would silently retire a
    // capability, while a bare name here would let a user `struct StringView`
    // acquire varchar-capture semantics. No fixture can see either direction —
    // the program that tried is recorded in the task notes.
    static bool is_string_view(TypeRef t) {
        return is_named_struct(t, "StringView") &&
               TypeRef(t).pkg_name() == "logos.lang.writ.wstatic";
    }
    // FQN-checked (task #99). ⚠ `Ident` is declared TWICE in stdlib — at
    // stdlib/mem/compiler/tokens/tokens.logos:79 (package
    // `logos.std.compiler.tokens`) and at
    // stdlib/mem/compiler/metaprog/ast.logos:158 (package
    // `logos.std.compiler.metaprog`) — so the owning package could not be read
    // off the directory tree; the `package` line and a census were both needed.
    // MEASURED over a full stdlib+examples build and all 7406 corpus .logos:
    // this predicate answers YES for `logos.std.compiler.metaprog` and NOTHING
    // else, never for the tokens `Ident` and never for an empty package. That is
    // consistent with its two call sites, which type quote-placeholder variables.
    static bool is_ident(TypeRef t) {
        return is_named_struct(t, "Ident") &&
               TypeRef(t).pkg_name() == "logos.std.compiler.metaprog";
    }
    // FQN-checked (task #99). Declared once, stdlib/mem/compiler/metaprog/
    // ast.logos:203, package `logos.std.compiler.metaprog` (NOT the directory
    // path). Census over a full stdlib+examples build and all 7406 corpus
    // .logos: that package and nothing else, never empty. A user `struct
    // ExprBlob` had already been met once as a live wrong answer (task #59).
    static bool is_exprblob(TypeRef t) {
        return is_named_struct(t, "ExprBlob") &&
               TypeRef(t).pkg_name() == "logos.std.compiler.metaprog";
    }
    // ── DELETED (task #99): `is_dataref`, and both arms it gated. ──────────
    // Unlike its eight siblings, this predicate's SUBJECT does not exist:
    // `struct DataRef` is declared in NO .logos anywhere — not in stdlib/, not
    // in tests/, not in any of the 6132 generated units under
    // build/tests/logos/facts/*/gen/ — and the compiler never synthesises one
    // (no make_struct_type("DataRef") site). The only three mentions left in the
    // tree are prose: a docs line, a `type_diag_2` .expected, and two pass
    // fixtures whose comments call `Held<T>` "the writ successor of DataRef".
    // An instrumented census over a full 53-target stdlib+examples build and all
    // 7406 corpus .logos confirmed it answered YES zero times. So this was not
    // an unexercised path (is_writ / is_string_view's cell — their types are
    // live) but an arm no program can reach, kept alive only by a bare name that
    // a user `struct DataRef` could have walked into: the ergonomic-deref write
    // and read would then have fired on a stranger's struct, in unsafe context,
    // through a `.mut_ptr()` / `.ptr()` method it does not have. Deleted rather
    // than shipped guarded.
    // FQN-checked (task #99). Declared once, stdlib/mem/compiler/metaprog/
    // ast.logos:189, package `logos.std.compiler.metaprog`. Census: that package
    // and nothing else, never empty.
    static bool is_quote_item_blob(TypeRef t) {
        return is_named_struct(t, "QuoteItemBlob") &&
               TypeRef(t).pkg_name() == "logos.std.compiler.metaprog";
    }
    // FQN-checked (task #99). Declared once, stdlib/mem/compiler/metaprog/
    // itemlist.logos:18 — package `logos.std.compiler.metaprog`, NOT
    // `…metaprog.itemlist`: the file sits in its own directory but declares the
    // same package as ast.logos. Census: that package and nothing else, never
    // empty.
    static bool is_item_list(TypeRef t) {
        return is_named_struct(t, "ItemList") &&
               TypeRef(t).pkg_name() == "logos.std.compiler.metaprog";
    }

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
    uint64_t read_annotation_u64(writ::TinyMapView ann) {
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
    // ── Struct/enum attribute FLAGS — the single point of truth ─────────────
    // Every phase that needs these flags (collect, datatype lowering, the
    // SPECIALIZATION path that bypasses structs_) parses through HERE. Adding
    // a flag attribute = one field + one branch below; never a bare string
    // compare at a call site (three drifting parsers caused the zoned2-spec
    // segfault class).
    struct StructAttrFlags {
        bool datatype        = false;  // #[datatype] — promote to the datatype pipeline
        bool annotation      = false;  // #[annotation] — annotation datatype (promotes)
        bool zoned           = false;  // #[zoned] — self-relative ptr fields / niche Ref arm
        bool zone_mut        = false;  // #[zone_mut] — fat zone-carrying &mut
        bool rel_ptr         = false;  // #[rel_ptr]
        bool self_describing = false;  // #[self_describing] — thin-*Self DST
        bool pinned          = false;  // #[pinned] — non-movable
        bool borrow_carrying = false;  // #[borrow_carrying] — value may hold an arena Ref
        bool no_auto_drop    = false;  // #[no_auto_drop]
        bool non_null        = false;  // #[non_null] — single 8B ptr field never null (Box/Rc/Arc); enables Option<T> NullPtr niche
        bool promotes_to_datatype() const { return datatype || annotation; }
    };
    template <typename Annots>
    StructAttrFlags parse_struct_attr_flags(const Annots& anns) {
        StructAttrFlags f;
        for (auto& ann : anns) {
            auto n = str_of(ann.get(sema_detail::la::NAME.code));
            if      (n == "datatype")        f.datatype        = true;
            else if (n == "annotation")      f.annotation      = true;
            else if (n == "zoned")           f.zoned           = true;
            else if (n == "zone_mut")        f.zone_mut        = true;
            else if (n == "rel_ptr")         f.rel_ptr         = true;
            else if (n == "self_describing") f.self_describing = true;
            else if (n == "pinned")          f.pinned          = true;
            else if (n == "borrow_carrying") f.borrow_carrying = true;
            else if (n == "no_auto_drop")    f.no_auto_drop    = true;
            else if (n == "non_null")        f.non_null        = true;
        }
        return f;
    }

    static unsigned attr_builtin_targets(std::string_view name) {
        auto bit = [](AttrTarget t) { return 1u << unsigned(t); };
        if (name == "type_code")
            return bit(AttrTarget::Struct) | bit(AttrTarget::Datatype) |
                   bit(AttrTarget::Enum)   | bit(AttrTarget::Trait);
        if (name == "zoned")           return bit(AttrTarget::Struct) | bit(AttrTarget::Enum);
        if (name == "datatype")        return bit(AttrTarget::Struct);
        // A custom-DST (`[T]` tail) marked self-describing: its tail length /
        // metadata is recoverable from its own bytes (a prefix field / header),
        // so a RAW pointer to it (`*mut/*const T`) is THIN (8B) and the fat
        // metadata is recovered in-band at deref — vs a non-self-describing DST
        // (bare `[T]` tail, e.g. Wrap<[u8]>) whose raw pointer must stay a fat
        // DstRef carrying the length. See docs/internals/ref-repr-design.md §6.
        if (name == "self_describing") return bit(AttrTarget::Struct);
        if (name == "rel_ptr")         return bit(AttrTarget::Struct);
        if (name == "pinned")          return bit(AttrTarget::Struct);
        if (name == "zone_mut")        return bit(AttrTarget::Struct);
        if (name == "borrow_carrying") return bit(AttrTarget::Struct) | bit(AttrTarget::Enum);
        if (name == "no_auto_drop")    return bit(AttrTarget::Struct);
        if (name == "non_null")        return bit(AttrTarget::Struct);
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
        // logos-core 1.5: `#[repr(...)]` minimal. Recognised on structs
        // (`transparent`) and enums (integer discriminant width). Other
        // modes parse-then-reject so silent drift is impossible.
        if (name == "repr")
            return bit(AttrTarget::Struct) | bit(AttrTarget::Enum);
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
        // An array's LENGTH is a use of whatever names it. `fn f<const N: i64>(
        // a: [T; N])` uses N — the walk did not look here, so N counted as
        // unused and the lint fired on correct code (and, worse, would have
        // stayed silent about a genuinely unused one for the wrong reason).
        if (k == LogosType::Kind::Array) {
            auto asv = tr.arr_size_var();
            if (!asv.empty()) {
                std::string nm(asv);
                if (nm.rfind(ARR_LEN_PACK_PFX, 0) == 0)
                    nm = nm.substr(ARR_LEN_PACK_PFX.size());
                if (!nm.empty()) tv_names.insert(nm);
            }
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
                // const-generic params (e.g. `const CFG: WritStatic`) are
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
                    // #100: QUALIFIED KEY FIRST, BARE SLOT LAST.
                    // ⚠ `find_trait_by_name` IS scope-aware — and this sweep
                    // has NO SCOPE. It runs from `SemaChecker::collect` AFTER
                    // the module loop sets `cur_package_ = {}` (sema_collect.cpp,
                    // two statements above the call), so `cur_package_` is empty
                    // and `cur_imports_` is whatever the LAST module left behind.
                    // Every probe therefore fell straight through to the bare
                    // slot, i.e. to whichever homonym registered first — the
                    // stdlib one. MEASURED: 27 of the 127 stdlib trait names
                    // (Borrow, Iterator, From, Index, Try, …) refused a 6-line
                    // user trait of the same name with the STDLIB trait's arity
                    // ("expected 1 type arg(s), got 0"), while the `dyn` spelling
                    // of the identical trait compiled and ran — the asymmetry
                    // that let a dyn-only probe certify this cell as closed.
                    // The bound already CARRIES the answer: `canonical_trait` is
                    // the traits_ key the name resolved to IN THE SCOPE WHERE THE
                    // BOUND WAS WRITTEN, captured by `read_trait_bound_args`
                    // (B-mv-03), which is the only thing that still knows the
                    // scope by the time this sweep runs.
                    // The bare fallback is NOT deleted: a bound built outside
                    // `read_trait_bound_args` has an empty `canonical_trait`, and
                    // for it the behaviour is byte-identical to before.
                    SemaTraitInfo* ti = nullptr;
                    if (!b.canonical_trait.empty()) {
                        auto tit = traits_.find(b.canonical_trait);
                        if (tit != traits_.end()) ti = &tit->second;
                    }
                    if (!ti) ti = find_trait_by_name(b.trait_name).second;
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
                           const std::vector<writ::TinyMapView>& annots) {
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
    bool is_catchall_pat(writ::TinyMapView arm) {
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

    // True iff ANY arm of this match binds by `ref mut`. That puts the
    // SCRUTINEE PLACE in a mutable use position: `match *b { E::V(ref mut y) =>
    // … }` over a `Box` must cross `deref_mut`, not `deref`, or the binding is
    // a mutable borrow of a place reached through a `&`. The arms are already
    // in hand where the scrutinee is lowered, so this is a QUESTION NOBODY
    // ASKED, not a phase-ordering problem.
    // It reuses `collect_ast_pat_bindings` — the tree's one pattern walker —
    // rather than growing a second one that would have to be kept in agreement
    // with it; the walker sets `pat_scan_saw_ref_mut_` as it goes.
    bool arms_bind_ref_mut(writ::TinyMapView node) {
        using namespace sema_detail;
        if (!node.has_key(ast::ITEMS)) return false;
        auto arms = arr_of(node.get(ast::ITEMS.code));
        std::vector<std::string> names;
        for (uint64_t i = 0; i < arms.size(); ++i) {
            auto arm = map_of(arms.get(i));
            if (code_of(arm) != ast::MATCH_ARM || !arm.has_key(ast::LHS)) continue;
            names.clear();
            pat_scan_saw_ref_mut_ = false;
            collect_ast_pat_bindings(map_of(arm.get(ast::LHS.code)), names);
            if (pat_scan_saw_ref_mut_) return true;
        }
        return false;
    }

    // Set by `collect_ast_pat_bindings` when the pattern it walked carries a
    // `ref mut` binding. Valid only immediately after a call: reset it, call,
    // read it. (A member instead of an out-parameter so the walker's twelve
    // recursive call sites stay unchanged — the property is recorded at the
    // single entry check every sub-pattern passes through.)
    bool pat_scan_saw_ref_mut_ = false;

    // True iff the item AST node carries a non-empty TYPE_PARAMS list.
    bool item_has_type_params(writ::TinyMapView node) {
        using namespace sema_detail;
        if (!node.has_key(la::TYPE_PARAMS)) return false;
        auto av = node.get(la::TYPE_PARAMS.code);
        if (av.is_null()) return false;
        auto tplist = map_of(av);
        if (!tplist.has_key(la::ITEMS)) return false;
        return arr_of(tplist.get(la::ITEMS.code)).size() > 0;
    }

    // ── Scope management ─────────────────────────────────────────

    struct VarInfo { TypeRef type; bool is_mut = false; bool owning_dyn = false;
                     uint32_t slot = 0; };
    struct Frame {
        logos::compiler::StrMap<VarInfo> vars;  // O(1) lookup
        std::vector<std::string> var_order;              // declaration order
        // G156-7: a closure body's own frame is a DROP boundary. The enclosing
        // function's frames stay on the stack (so the body can `lookup` captured
        // names for type resolution), but a `return` inside the closure must NOT
        // drop the enclosing function's locals — only the closure's own frames.
        // collect_all_drops stops after a boundary frame.
        bool closure_boundary = false;
        // G167-4: the lower_block scope that IS a loop's body. A `break`/
        // `continue` nested inside the body (e.g. in an `if`) exits via the loop
        // edge WITHOUT falling through the body block's normal end-of-scope
        // drops — so collect_drops_to_loop() must drop every frame from the
        // statement down to AND INCLUDING this one. Set by lower_block when
        // pending_loop_body_scope_ is armed by the loop lowering.
        bool loop_boundary = false;
        // #118 — the frame `lower_block` pushes for a `{ ... }` statement
        // list. Only such a frame HAS a statement list to splice a hidden
        // `let` into, so it is the only kind that can host a drop flag; every
        // other frame (params, a match arm, an if-expression branch) retargets
        // to its nearest block frame.
        bool block_frame = false;
        // #118 — CONDITIONAL-MOVE DROP FLAGS. A local moved on SOME reaching
        // path and not on others has an ownership state that is not a static
        // fact, so its scope-exit destructor needs a runtime carrier. Maps the
        // local's name to the name of the hidden `bool` that carries it (true
        // = still owned). Lives in the frame that DECLARES the local, so it
        // dies with that frame — a sibling block that reuses the name is not
        // confused by it, and the flag `let` is always in scope at the drop.
        logos::compiler::StrMap<std::string> cond_move_flags;
        // #121-A — PATHS UNDER A LOCAL OF THIS FRAME THAT A BRANCH MERGE
        // DETERMINED ARE MOVED ON *EVERY* REACHING PATH. Such a path gets no
        // flag (the static suppression is exact) and `moved_vars_` normally
        // carries it to the drop as a skip — but a merge inside a LOOP BODY
        // has its per-branch move set reverted before the enclosing frame's
        // drops are emitted, so the witness is gone and the container (or a
        // flagged ANCESTOR of the path) destroys the place a second time.
        // Recorded here instead, on the frame that owns the root, so it
        // survives exactly as long as the local it describes.
        std::set<std::string> cond_move_static_moves;
    };
    std::vector<Frame> scope_;
    // #118 — hidden `let mut __df_N: bool = true;` statements waiting to be
    // spliced into the block of the frame whose local they guard. The move
    // that needs a flag is discovered at a BRANCH MERGE, arbitrarily deep
    // inside the statement being lowered, while the flag must be declared in
    // the ENCLOSING block of the local it guards — `lower_block` flushes the
    // entries addressed to its own frame just before it appends the statement
    // that produced them, so the declaration always precedes every use.
    std::vector<std::pair<size_t, lir_view::StmtRef>> pending_frame_lets_;
    // Phase-1 string-interning: per-function dense variable SLOT counter. Each
    // call to define() (let / param / pattern / for / closure binding — the
    // single registrar) hands out the next slot; the final value is the
    // function's local_count. Reset per function in lower_fn. Shadowing gives a
    // NEW slot, so slots uniquely identify bindings and downstream (borrow,
    // mlir) can key var state on the integer instead of the name string.
    uint32_t next_slot_ = 0;
    // G167-4: armed by while/loop/for right before lowering the body block;
    // consumed by the next push_scope (lower_block) to tag its frame as the
    // loop body for break/continue drop-glue.
    bool pending_loop_body_scope_ = false;

    std::set<std::string> moved_vars_;   // variables consumed by move
    // §7.1 follow-up: EVER-moved across branches (lifted per-fn). Per-branch
    // save/restore reverts moved_vars_ on diverging paths but mlir-gen
    // merges branches; param drops at fn epilogue must consult this set so a
    // conditional move (e.g. `if b { return f(x); }`) doesn't trigger a
    // drop on the merge that re-frees a moved-out heap.
    std::set<std::string> body_ever_moved_;
    // B8 drop-before-replace: vars declared WITHOUT an initializer (`let mut
    // x: T;`). Such a var is not definitely-initialized at a later assignment
    // (a conditional path may have left it uninit), so we must NOT drop the
    // old value on reassignment (that would free garbage). A var declared WITH
    // a value is omitted here → it IS definitely-init at every reassignment
    // (branches don't de-initialize) → safe to drop-before-replace.
    // B8: vars declared WITHOUT an initializer (`let mut x: T;`). mlir-gen gives
    // each a runtime drop flag that decides drop-before-replace + scope-exit drop
    // exactly; sema only uses this set to suppress the static drop_old hint.
    std::set<std::string> decl_uninit_vars_;
    // logos-core 2.7: definite-assignment tracker — vars CURRENTLY uninitialised
    // at this program point. Parallel to decl_uninit_vars_ but DROPS the var on
    // first assignment (so subsequent reads see "init"). Reading a var in this
    // set is a hard error ("use of possibly uninitialised binding"). At a
    // CFG-merge (if/else, match), the post-merge state is the UNION of the
    // branches — i.e. uninit if uninit on ANY incoming path. Diverging branches
    // (the ones whose tail is a return/break/continue/panic) contribute nothing.
    // Loops are conservative: vars assigned only inside the body do not become
    // init at the outer scope. Closures get their own (saved+restored) tracker.
    std::set<std::string> currently_uninit_vars_;
    // G156-7: vars moved into a `move` closure that nonetheless must still be
    // DROPPED at their scope exit. A move closure's env stores a POINTER to the
    // source's storage (borrows it; closures have no capture drop-glue), and the
    // source is marked moved (so use-after-move is enforced) — but suppressing
    // its drop would leak. So the source stays in moved_vars_ (use-check works)
    // AND is recorded here; collect_drops/collect_all_drops un-skip it so its
    // destructor runs exactly once. Monotonic (no save/restore needed).
    std::set<std::string> closure_owned_drop_;
    // Rust capture-drop ORDER for the un-skipped captures above: they drop
    // WITH their owning closure binding (at its var_order slot, in capture
    // order), not at their own slots. Populated by lower_let when its direct
    // RHS is a closure; consumed by emit_frame_drops. Same-frame only — an
    // owner in a different frame falls back to own-slot drops (a closure
    // created in a conditional inner block must not hoist the outer
    // capture's drop into branch-only code).
    std::vector<std::string> pending_closure_capture_drops_;
    std::unordered_map<std::string, std::vector<std::string>> closure_drop_group_;
    std::unordered_map<std::string, std::string> capture_owner_;
    // Shared per-frame drop emission (group-aware) — the single inner loop
    // behind collect_drops / collect_all_drops / collect_drops_to_loop and
    // the fn-epilogue param walk (was 4 drifting copies).
    void emit_frame_drops(const Frame& frame, std::vector<lir_view::StmtRef>& drops,
                          const std::set<std::string>* extra_skip = nullptr) const;
    std::set<std::string> copy_types_;   // types with impl Copy — never move-only
    // T1-13: extern-block statics (declaration only, foreign storage) —
    // every access requires `unsafe`.
    std::set<std::string> module_extern_statics_;
    // Conditional Copy (`impl<P: Copy> Copy for Pin<P>`, Rust-style): target
    // name → target type-arg positions bound to a Copy-bounded impl param.
    // An instance is Copy iff every recorded position's arg is itself Copy
    // (struct_type_is_copy evaluates via is_move_type recursion). The blanket
    // `impl<P> Copy for Pin<P>` made Pin<Box<T>> Copy → a "move" bitwise-
    // copied and BOTH bindings dropped (double free, adversarial t03).
    std::unordered_map<std::string, std::vector<size_t>> conditional_copy_;
    bool struct_type_is_copy(TypeRef x) const;

    int destruct_counter_ = 0;           // unique-name source for `let (...)` temps

    // Phase 7 slice 12: derive-style handler registry, collected from
    // `#[metaprog_handler("name")]` annotations on hook fns.
    std::vector<MetaprogHandlerStage> metaprog_handlers_;
    std::vector<MetaprogTargetStage>  metaprog_targets_;
    // Type names carrying a metaprog-handler annotation whose emission has not
    // happened yet this round. A deferred capability check must not read the
    // ABSENCE of a synthesized impl as the type lacking the capability.
    logos::compiler::StrSet           metaprog_annotated_types_;

    // Phase 7 slice 17: metaprog-compile mode. When metaprog_mode_ is true,
    // lower_fn skips body lowering for non-handler fns in the entry ast
    // (cur_ast_idx_ == metaprog_entry_ast_idx_). Errors inside skipped
    // bodies don't reach result_.diags. Set via sema_lower's SemaOptions.
    bool   metaprog_mode_           = false;
    size_t metaprog_entry_ast_idx_  = static_cast<size_t>(-1);
    // The entry-ast gate above under-generalizes for multi-module CUs
    // (emit_module passes entry_ast_idx = -1: N modules, no single entry):
    // ANY module with a still-pending item-position macro callsite
    // (FN_MACRO_CALL_ITEM / METACALL_ITEM, not yet *_DONE) may reference
    // items the macro hasn't synthesized yet, so its plain fn bodies must
    // be stubbed out of the metaprog JIT slice exactly like the entry
    // ast's. Computed per module at the top of lower_module_items.
    bool   cur_ast_has_pending_item_mc_ = false;
    // Packages flagged pending-item this sema run (see lower_module_items'
    // discovery block): pending-ness propagates to importing modules so the
    // dispatch slice never keeps a fully-lowered caller of an erased stub.
    std::set<std::string> metaprog_pending_pkgs_;

    // E0121 analog (audit-v2 T0-3): `_` is not allowed within types on item
    // signatures (fn params / return type / const item type). Set while
    // resolve_type runs on a signature position; resolve_type's TYPE_REF "_"
    // case errors instead of yielding InferredType. resolve_type recurses
    // through generic args / elems itself, so nested `_` (Vec<_>, &_,
    // [_; N]) is caught at the same single chokepoint. RAII guard below.
    bool in_item_signature_ = false;
    // Set while a type ALIAS's RHS is resolved at declaration time. A failure
    // there is not a user error: the alias keeps its AST and its use sites
    // re-resolve it (collect_type_alias / lookup_type_by_name), so the
    // diagnostic must wait for the use site — where the name is actually
    // wanted and the metaprog round that defines it has had its chance.
    bool alias_decl_resolve_ = false;
    struct ItemSignatureGuard {
        bool& flag; bool saved;
        explicit ItemSignatureGuard(bool& f) : flag(f), saved(f) { f = true; }
        ~ItemSignatureGuard() { flag = saved; }
    };
    std::vector<std::string> metaprog_keep_fns_;
    size_t cur_ast_idx_             = static_cast<size_t>(-1);

    // Skeleton-skip gate: symbol names already compiled into a linked
    // archive's .o (nm --defined-only). Caller-owned; outlives sema. When a
    // from_binary fn's name is in here, lower_fn skips its body (mlir_gen
    // forward-declares it on the same predicate; the linker resolves it).
    const logos::compiler::StrSet* binary_symbols_ = nullptr;
    // G156-1: dep-archive nominal decls (pkg,name) from the v3 exports trailer.
    const std::vector<std::pair<std::string, std::string>>* dep_nominal_decls_ = nullptr;
    // How many from_binary fn bodies were skeleton-skipped this run. Surfaced
    // under LOGOS_SEMA_PHASE_TIMING as an observability hook for the skip path.
    size_t skel_skip_count_         = 0;
    size_t tmpl_ext_ref_count_      = 0;  // generic templates routed to a published blob body

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
    std::unordered_set<const writ::MemHolder*> collected_holders_;

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
    std::unordered_set<const writ::MemHolder*> user_holders_;

    // Current AST root, set by lower_program at each iteration. Used by
    // sema-side intrinsics (e.g. `template_of::<X>()`) that need to walk
    // the user-root MODULE's ITEMS to bake an AST-node arena offset.
    writ::TinyMapView cur_root_;

    bool fn_is_metaprog_handler(std::string_view name) const {
        auto base = bare_fn_name(name);
        for (const auto& mh : metaprog_handlers_)
            if (mh.hook_fn == base) return true;
        return false;
    }

    void push_scope() { scope_.emplace_back(); }
    void push_closure_scope() { scope_.emplace_back(); scope_.back().closure_boundary = true; }
    void pop_scope() {
        if (!scope_.empty()) {
            // Remove popped variables from moved set
            for (auto& name : scope_.back().var_order)
                moved_vars_.erase(name);
            scope_.pop_back();
        }
    }

    bool is_move_type(TypeRef t) const;
    // Gap-4: normalize an associated-type projection `T::A` to a concrete type
    // when T carries an equality bound `Trait<A = V>` in scope. Returns t
    // unchanged if it isn't a normalizable projection. Non-recursive (one hop).
    TypeRef normalize_assoc_eq(TypeRef t) const;
    // Auto-Copy pass — populates copy_types_ for structs whose every field
    // is a Copy type and which have no `impl Drop`. Called after
    // check_supertrait_impls so manual `impl Copy` entries are already in.
    void compute_auto_copy_types();
    void mark_moved(const std::string& name) {
        moved_vars_.insert(name);
        // §7.1 follow-up: track EVER-moved across branches. per-branch
        // save/restore (lower_if / lower_match) reverts moved_vars_ on
        // diverging branches, but Logos's mlir-gen merges branches into a
        // single CFG block — a drop emitted in the merge fires for ALL
        // incoming branches, including the one that moved the var. To
        // avoid double-free on conditional-move shapes (e.g. recursion-
        // tail-call-no-arg-leak), fn-epilogue param drops consult this
        // ever-set, NOT just the post-merge moved_vars_. (Proper fix is
        // B8-style drop-flag elaboration extended to params — tracked.)
        body_ever_moved_.insert(name);
    }

    // #118 — allocate (once) the drop flag for a conditionally-moved local,
    // declaring it in the frame that OWNS the local. Returns "" when no flag
    // is possible or needed: a dotted field path, a name not visible in a live
    // frame below a closure boundary, or a local whose scope exit runs no
    // destructor at all (nothing to guard). Idempotent per (frame, name): an
    // inner merge that already armed the flag makes every enclosing merge a
    // no-op, which is what keeps `if c { if d { consume(a); } }` to ONE flag
    // and one clear.
    // #121 — the type of ONE segment of a moved-path spelling. `moved_vars_`
    // writes a tuple element the same way it writes a struct field (`t.0`),
    // but `field_type_of_for_type` answers only for structs, so a tuple
    // segment has to be resolved here or the whole path silently declines its
    // flag (and the pre-#121 leak stands for exactly that spelling).
    TypeRef path_segment_type(TypeRef base, const std::string& seg) {
        if (!base) return nullptr;
        if (TypeRef(base).kind() == LogosType::Kind::Tuple) {
            if (seg.empty() ||
                seg.find_first_not_of("0123456789") != std::string::npos)
                return nullptr;
            auto es = TypeRef(base).tuple_elems();
            size_t i = std::stoul(seg);
            return i < es.size() ? es[i] : TypeRef(nullptr);
        }
        return field_type_of_for_type(base, seg);
    }

    // #121-A — remember that `name` (a dotted path) is moved on every reaching
    // path of a merge, on the frame that OWNS its root. See the field's comment.
    void note_static_move(const std::string& name) {
        auto dot = name.find('.');
        if (dot == std::string::npos) return;   // a whole local needs no skip
        std::string root = name.substr(0, dot);
        for (size_t i = scope_.size(); i-- > 0; ) {
            if (scope_[i].vars.count(root)) {
                scope_[i].cond_move_static_moves.insert(name);
                return;
            }
            if (scope_[i].closure_boundary) return;
        }
    }

    std::string cond_move_flag_for(const std::string& name) {
        // #121 — A DOTTED PATH IS HALF THE KEYSPACE OF THE MAP THIS ELABORATES,
        // and it used to be dropped on the floor here (`if (name.find('.') !=
        // npos) return {}`), so `if c { consume(h.p); }` leaked `h.p` on the
        // path that did not move it — exactly the plain-local shape #118 fixed,
        // one dot away. The path's ROOT owns the frame; the path's own TYPE
        // decides droppability.
        std::string root = name.substr(0, name.find('.'));
        size_t fi = SIZE_MAX;
        for (size_t i = scope_.size(); i-- > 0; ) {
            if (scope_[i].vars.count(root)) { fi = i; break; }
            if (scope_[i].closure_boundary) return {};
        }
        if (fi == SIZE_MAX) return {};
        // The flag DECLARATION must land in a statement list that encloses
        // both every clear and the guarded drop. That is the nearest block
        // frame at or below the declaring frame — or, for the params frame
        // (which has none below it), the function's body block just above.
        size_t target = SIZE_MAX;
        for (size_t j = fi + 1; j-- > 0; )
            if (scope_[j].block_frame) { target = j; break; }
        if (target == SIZE_MAX)
            for (size_t j = fi + 1; j < scope_.size(); ++j)
                if (scope_[j].block_frame) { target = j; break; }
        if (target == SIZE_MAX) return {};
        auto& fr = scope_[fi];
        if (auto it = fr.cond_move_flags.find(name); it != fr.cond_move_flags.end())
            return it->second;
        auto vit = fr.vars.find(root);
        if (vit == fr.vars.end()) return {};
        const VarInfo& info = vit->second;
        // #121 — the droppability question is about the PATH, not the root: it
        // is `h.p`'s destructor the flag guards, and `h` itself may well have
        // none. Walk the segments; an unresolvable one (an array element, a
        // deref, a projection this predicate cannot name) declines the flag,
        // which leaves the pre-#121 behaviour for that shape rather than
        // inventing a drop for a place the emitter cannot address.
        TypeRef pt = info.type;
        bool path_owning_dyn = info.owning_dyn;
        for (size_t p = root.size(); p < name.size(); ) {
            size_t e = name.find('.', p + 1);
            if (e == std::string::npos) e = name.size();
            std::string seg = name.substr(p + 1, e - p - 1);
            if (!pt) return {};
            pt = path_segment_type(pt, seg);
            path_owning_dyn = false;
            p = e;
        }
        if (!pt) return {};
        bool droppable =
            (TypeRef(pt).owning_trait_object() ||
             (path_owning_dyn &&
              TypeRef(pt).kind() == LogosType::Kind::TraitObject) ||
             !drop_fn_for(pt).empty() || has_droppable_fields(pt));
        if (!droppable || !cur_prog_) return {};
        std::string fl = std::format("__df_{}", tmp_var_count_++);
        lir::SLet sl;
        sl.name = fl;
        sl.type = prim(LogosType::Kind::Bool);
        sl.is_mut = true;
        sl.value = builder().lit_bool(true, prim(LogosType::Kind::Bool));
        pending_frame_lets_.emplace_back(target, make_stmt_emit(node_line_, std::move(sl)));
        fr.cond_move_flags[name] = fl;
        return fl;
    }

    // Every local for which a flag CLEAR has been spliced, in emission order.
    // A merge snapshots its size before lowering a branch: the names appended
    // while that branch was lowered are the ones an INNER merge already
    // handled inside it, and must not be cleared a second time at the outer
    // merge (the inner clear is conditional on the inner path; an outer one
    // would fire on the whole branch).
    std::vector<std::string> flag_clear_log_;
    bool has_cond_move_flag(const std::string& name) const {
        // #121 — the flag for a dotted path is keyed by the FULL path but
        // stored in the frame that owns its ROOT, so the walk must stop on the
        // root's frame, not on a frame that happens to hold the whole name
        // (none ever does).
        std::string root = name.substr(0, name.find('.'));
        for (size_t i = scope_.size(); i-- > 0; ) {
            if (scope_[i].cond_move_flags.count(name)) return true;
            if (scope_[i].vars.count(root)) return false;
            if (scope_[i].closure_boundary) return false;
        }
        return false;
    }

    // Wrap a drop in `if <flag> { ... }` (see cond_move_flag_for).
    lir_view::StmtRef guard_with_flag(const std::string& flag,
                                      lir_view::StmtRef d) const;

    // One alternative path through a branch construct that REACHES the
    // enclosing frame's scope-exit drops. `blk` is null for a synthesised
    // fall-through (a missing `else`, an unmatched tail) — such a path moves
    // nothing, so it never needs a clear spliced into it.
    struct CondMoveBranch {
        std::vector<lir_view::StmtRef>* blk = nullptr;
        // An EXPRESSION-form arm (match-expr / if-expr) has no statement list
        // to splice into; its clear is appended by rebuilding the arm value as
        // `{ let t = <val>; __df = false; t }`.
        lir::LExprPtr*                  val = nullptr;
        std::set<std::string> moves;
        size_t clear_mark = 0;   // flag_clear_log_ size before this branch
        size_t clear_end  = 0;   // ... and after it
    };
    void elaborate_cond_moves(const std::set<std::string>& pre,
                              std::vector<CondMoveBranch>& reaching);

    // #118 — append a statement AFTER an expression's evaluation without
    // changing its value: `{ let t = <v>; <s>; t }`. Used to clear a drop flag
    // on an expression-form arm, where there is no statement list to splice
    // into. Same shape lower_match_expr already uses for its arm drops.
    lir::LExprPtr append_stmt_to_value(lir::LExprPtr v, lir_view::StmtRef st) {
        // #122 — AN ARM THAT LEAVES BY THE LOOP EDGE ENDS IN A TERMINATOR, and
        // a statement appended AFTER it is dead code: the flag would stay set
        // on exactly the path that performed the move, and the enclosing
        // frame's guarded drop would then destroy a value already moved out.
        // Rebuild the block with the clear spliced BEFORE the terminator —
        // the same placement `splice_flag_clear` gives the statement form.
        if (v && v.kind() == lir_schema::expr::Code::BlockExpr) {
            auto bv = lir_view::EBlockExprView{v};
            std::vector<lir_view::StmtRef> stmts;
            bool terminated = false;
            if (auto b = bv.block())
                b.each_stmt([&](lir_view::StmtRef s) {
                    stmts.push_back(s);
                    if (!s) return;
                    auto k = s.kind();
                    terminated = (k == lir_schema::stmt::Code::Return ||
                                  k == lir_schema::stmt::Code::Break ||
                                  k == lir_schema::stmt::Code::Continue);
                });
            if (terminated) {
                splice_flag_clear(stmts, std::move(st));
                return builder().block_expr(lir_mirror_block(*cur_prog_, stmts),
                                            bv.result(), TypeRef(expr_type(v)));
            }
        }
        TypeRef vt = v ? TypeRef(expr_type(v)) : TypeRef(void_t());
        std::vector<lir_view::StmtRef> blk;
        if (!v || TypeRef(vt).kind() == LogosType::Kind::Void) {
            if (v) {
                lir::SExprStmt es; es.expr = std::move(v);
                blk.push_back(make_stmt_emit(node_line_, std::move(es)));
            }
            blk.push_back(std::move(st));
            return builder().block_expr(lir_mirror_block(*cur_prog_, blk),
                                        nullptr, vt);
        }
        std::string tmp = std::format("__dfv_{}", tmp_var_count_++);
        lir::SLet sl;
        sl.name = tmp; sl.type = vt; sl.is_mut = false; sl.value = std::move(v);
        blk.push_back(make_stmt_emit(node_line_, std::move(sl)));
        blk.push_back(std::move(st));
        return builder().block_expr(lir_mirror_block(*cur_prog_, blk),
                                    builder().var_ref(tmp, vt), vt);
    }

    // `__df_N = false;` — emitted on the path that performed the move.
    lir_view::StmtRef cond_move_clear_stmt(const std::string& flag) {
        lir::SAssign sa;
        sa.name  = flag;
        sa.value = builder().lit_bool(false, prim(LogosType::Kind::Bool));
        return make_stmt_emit(node_line_, std::move(sa));
    }

    // Splice a flag clear into a branch's statement list, BEFORE a trailing
    // `return`/`break`/`continue` (and before the drop glue lower_block
    // already inserted ahead of it) — appending after a terminator would be
    // dead code, and the loop-exit path is exactly the one that needs it.
    void splice_flag_clear(std::vector<lir_view::StmtRef>& blk,
                           lir_view::StmtRef clear) const {
        using C = lir_schema::stmt::Code;
        size_t at = blk.size();
        while (at > 0) {
            auto br = stmt_ref_of(blk[at - 1]);
            if (!br) break;
            auto k = br.kind();
            if (k == C::Return || k == C::Break || k == C::Continue ||
                k == C::Drop) { --at; continue; }
            break;
        }
        blk.insert(blk.begin() + (ptrdiff_t)at, std::move(clear));
    }

    // Writing a move-type RHS into a memory cell (deref, indexed, field, …)
    // is a move. Mark the source so its scope-exit auto-drop is suppressed
    // — the byte pattern now lives in the destination, and the destination
    // owns the Drop responsibility.
    void track_write_move(const lir::LExprPtr& val) {
        if (!val) return;
        if (!is_move_type(expr_type(val))) return;
        mark_moved_expr(expr_ref_of(val));
    }

    // Mark a moved expression — handles VarRef + nested FieldRead chains.
    // For `outer.field` (move-type) inserts "outer.field" into moved_vars_;
    // for `outer.a.b` inserts "outer.a.b". make_drop_stmt extracts the
    // first-level field name and passes to SDrop.moved_fields so the
    // mlir-gen field-drop loop skips that field. No-op if not a move type
    // or not a recognised l-value chain.
    // Zone Step 4 (pin): is `t` a NON-MOVABLE (location-anchored) type, so a
    // by-value move (memcpy of its storage) would be unsound? Two anchoring
    // sources, transitive through struct / tuple / array fields but NOT through a
    // pointer or reference (inline storage only):
    //   (1) an inline `#[rel_ptr]` field — its stored i64 is a self-relative
    //       offset (target − &field); memcpy carries it to a wrong anchor.
    //   (2) a `#[pinned]` type (e.g. the at-rest WAnyRel) — bits anchored to
    //       its slot; accessed in place and materialised to a movable value form.
    // Asymmetry: a `#[rel_ptr]` type ITSELF is movable (its value-form is the
    // resolved absolute pointer — flagged only when embedded as a field); a
    // `#[pinned]` type itself IS non-movable (no implicit value-form).
    bool is_non_movable_type(TypeRef t, int depth = 0) {
        using K = LogosType::Kind;
        if (!t || depth > 16) return false;
        auto k = TypeRef(t).kind();
        if (k == K::Tuple) {
            for (auto e : TypeRef(t).tuple_elems())
                if (is_non_movable_type(e, depth + 1)) return true;
            return false;
        }
        if (k == K::Array) return is_non_movable_type(TypeRef(t).elem(), depth + 1);
        if (k == K::Struct || k == K::ZonedStruct) {
            auto [pkg, ssi] = find_struct_by_name(TypeRef(t).struct_name());
            if (!ssi) return false;
            // SEMANTICALLY both `#[rel_ptr]` and `#[pinned]` are non-movable (a
            // memcpy-move of the location-anchored relative bits breaks the
            // anchor); duplication is a COPY via the absolute intermediate
            // (materialise → lower, re-anchoring). But the COMPILER flags them
            // asymmetrically, because of HOW each is read:
            //  • `#[pinned]` (WAnyRel): accessed via `*mut` + an EXPLICIT
            //    materialise to a different value type — the bare type never
            //    appears as a read-out value, so flagging it here is safe and
            //    correct (it IS non-movable).
            //  • `#[rel_ptr]`: a field read AUTO-materialises and the result is
            //    transiently typed `RelPtr<T>` before coercing to `*T` — flagging
            //    the bare type here would trip the move-check on that very read.
            //    So it is flagged only when embedded as a FIELD (below), where a
            //    whole-struct memcpy WOULD carry the delta. In practice rel_ptr is
            //    non-movable anyway: you always materialise to `*T` or keep it in a
            //    slot. (Flagging the bare rel_ptr type too needs field reads to
            //    produce `*T` directly — a follow-up, not needed for the container.)
            if (ssi->pinned) return true;
            // `#[zoned2]`: self-relative pointer fields are anchored to their own
            // slot — a whole-struct memcpy would carry stale deltas, so the struct
            // is non-movable (can't occupy a by-value stack slot).
            if (ssi->zoned2) return true;
            for (auto& f : ssi->fields) {
                TypeRef ft = f.type;
                auto fk = TypeRef(ft).kind();
                if (fk == K::Struct || fk == K::ZonedStruct) {
                    auto [fp, fssi] = find_struct_by_name(TypeRef(ft).struct_name());
                    if (fssi && (fssi->rel_ptr || fssi->pinned)) return true;  // (1) rel_ptr / (2) pinned field
                    if (is_non_movable_type(ft, depth + 1)) return true;       // nested inline
                } else if (fk == K::Tuple || fk == K::Array) {
                    if (is_non_movable_type(ft, depth + 1)) return true;
                }
                // pointer / reference fields are NOT followed — only inline storage.
            }
            return false;
        }
        return false;
    }

    // #121-A — THE MOVE-PATH SPELLING, ENUMERATED BY THE PROPERTY INSTEAD OF
    // BY THE NODE. A place is a projection CHAIN of segments rooted at a local;
    // `moved_vars_` spells a struct field and a tuple element the SAME way
    // (`o.i`, `t.0`), but the two readers are different nodes (FieldRead /
    // TupleIndex) and the two walkers below used to be separate — each one
    // bottoming out only at a VarRef. So a chain that MIXES them was recorded
    // as moved by NEITHER: `consume(t.0.p)` (FieldRead over TupleIndex) and
    // `consume(o.i.0)` (TupleIndex over FieldRead) left `moved_vars_` empty,
    // the container's scope-exit drop had nothing to skip, and the value was
    // destroyed a SECOND time. MEASURED with no conditional anywhere in the
    // program: `M1 M2 D1 D2 D1`, rc 0, silent — a genuine double free of a
    // heap-owning payload, pre-existing and independent of the drop flags.
    // One walker over both segment kinds; returns "" when the chain does not
    // bottom out in a VarRef (a deref, an index, a call result — not a stable
    // l-value path this set can name).
    std::string move_path_of(lir_view::ExprRef er) const {
        using C = lir_schema::expr::Code;
        std::vector<std::string> segs;
        lir_view::ExprRef cur = er;
        while (cur) {
            if (cur.kind() == C::FieldRead) {
                lir_view::EFieldReadView v{cur};
                segs.emplace_back(std::string(v.field()));
                cur = v.receiver();
            } else if (cur.kind() == C::TupleIndex) {
                lir_view::ETupleIndexView v{cur};
                segs.emplace_back(std::to_string(v.index()));
                cur = v.receiver();
            } else break;
        }
        if (!cur || cur.kind() != C::VarRef) return {};
        std::string path(lir_view::EVarRefView{cur}.name());
        for (auto it = segs.rbegin(); it != segs.rend(); ++it) {
            path.push_back('.');
            path += *it;
        }
        return path;
    }

    void mark_moved_expr(lir_view::ExprRef er) {
        if (!er) return;
        using C = lir_schema::expr::Code;
        // Zone Step 4 (pin): reject moving — by value — a PLACE of a non-movable
        // (location-anchored) type: one inlining a `#[rel_ptr]` field, or a
        // `#[pinned]` at-rest type. Borrows, in-place writes, and method autoref
        // never reach mark_moved_expr; reading a rel_ptr field materialises to an
        // absolute pointer — so only a genuine by-value move of the anchored
        // storage trips this.
        if (er.kind() == C::VarRef || er.kind() == C::FieldRead ||
            er.kind() == C::TupleIndex || er.kind() == C::IndexRead) {
            auto mt = er.type(cur_prog_->type_pool.impl());
            if (mt && is_non_movable_type(mt)) {
                error(std::format(
                    "cannot move a value of type `{}` by value: it is location-"
                    "anchored (a self-relative `#[rel_ptr]` field, or a `#[pinned]` "
                    "type) — use it in place (through `&`, `&mut`, or `*mut`)",
                    type_str(mt)));
                return;
            }
        }
        if (er.kind() == C::VarRef) {
            std::string nm(lir_view::EVarRefView{er}.name());
            auto vt_ = er.type(cur_prog_->type_pool.impl());
            // ⚠ NOT THE SITE FOR "`&mut T` IS AFFINE, NOT Copy". A probe on
            // exactly this arm fired ZERO times over 324 ledger programs while
            // the arm itself fired 9854 — every caller pre-gates on
            // `is_move_type` (sema_stmt.cpp 1001, 1049, 1082, 1798, 2661, 7409,
            // 8869, 8899), so a `&mut` operand never arrives. The four rows
            // that need it (reborrow-sugg-move-then-borrow,
            // moved-value-suggest-reborrow-issue-127285--r32 / --t32,
            // issue-83924) have to be priced at those CALLER GATES; the
            // correction itself is already written once, at
            // `SemaChecker::struct_type_is_copy`. Recorded 2026-08-30.
            if (is_move_type(vt_) || lookup_owning_dyn(nm))
                mark_moved(nm);
            return;
        }
        // G154-4: moving a struct field or a tuple element out by value
        // (`consume(h.p)`, `consume(t.0)`) marks `<name>.<seg>` so the
        // container's scope-end Drop (SDrop struct / tuple branch) skips it —
        // else it is dropped twice (double-free). #121-A: ONE walker for both
        // segment kinds, so a MIXED chain (`t.0.p`, `o.i.0`) is recorded too.
        if (er.kind() == C::FieldRead || er.kind() == C::TupleIndex) {
            if (!is_move_type(er.type(cur_prog_->type_pool.impl()))) return;
            std::string path = move_path_of(er);
            if (!path.empty()) mark_moved(path);
        }
        // Moving a Drop-bearing element OUT of a fixed-size array by index
        // (`let s = arr[i]`, `return arr[i]`, `f(arr[i])`) aliases the array's
        // storage: Logos can't mark a single array slot moved, so the array
        // still drops the element at scope end → DOUBLE-FREE. Rust rejects this
        // ("cannot move out of index"). Only reject when the element actually
        // needs a Drop — plain value structs / primitives (e.g. metaprog `Type`)
        // shallow-copy safely. Borrows / autoref (`&arr[i]`, `arr[i].m()`) do
        // not move and never reach mark_moved_expr, so they are unaffected.
        // ⚠ SliceIndex IS THE SAME STEP, and its absence made `match sl[0]`
        // over a Drop-bearing element the one shape that still double-freed
        // after #110 R2 closed the array spelling. The gates below are NOT
        // decorative and are copied unchanged: the concreteness triple exists
        // because generic stdlib iterators legitimately move an element out
        // while managing the container's drop, and slice iterators are that
        // same family. `needs_drop` keeps a Copy element compiling — pinned as
        // the admit half, exactly as #110 R2 pinned its own.
        // The receiver test differs by one kind and one kind only: an array
        // receiver is Kind::Array (possibly behind &/&mut/*), a slice receiver
        // is Kind::Slice, which owns nothing — the backing storage does.
        if (er.kind() == C::SliceIndex) {
            auto et = er.type(cur_prog_->type_pool.impl());
            if (et && TypeRef(et).kind() != LogosType::Kind::TypeVar &&
                TypeRef(et).kind() != LogosType::Kind::AssocType &&
                TypeRef(et).kind() != LogosType::Kind::ImplTrait &&
                needs_drop(et)) {
                lir_view::ESliceIndexView sv{er};
                auto recv = sv.slice();
                auto rt = recv ? recv.type(cur_prog_->type_pool.impl()) : TypeRef{};
                if (rt && TypeRef(rt).kind() == LogosType::Kind::Slice)
                    error(std::format(
                        "cannot move out of type `{}`, a non-copy slice",
                        type_str(rt)));
            }
        }
        if (er.kind() == C::IndexRead) {
            auto et = er.type(cur_prog_->type_pool.impl());
            // Only CONCRETE Drop-bearing elements. A generic `[T; N]` (TypeVar
            // element) can't be classified at sema — and generic stdlib code
            // legitimately moves `arr[i]` out while managing the array's drop
            // (ArrayIntoIter / MapWindowsIter consume the whole array). Defer
            // those; reject only the unmanaged concrete `let s = arr[i]` move.
            if (et && TypeRef(et).kind() != LogosType::Kind::TypeVar &&
                TypeRef(et).kind() != LogosType::Kind::AssocType &&
                TypeRef(et).kind() != LogosType::Kind::ImplTrait &&
                needs_drop(et)) {
                lir_view::EIndexReadView iv{er};
                auto recv = iv.receiver();
                auto rt = recv ? recv.type(cur_prog_->type_pool.impl()) : TypeRef{};
                bool from_array = rt &&
                    (TypeRef(rt).kind() == LogosType::Kind::Array ||
                     ((TypeRef(rt).kind() == LogosType::Kind::Ref ||
                       TypeRef(rt).kind() == LogosType::Kind::MutRef ||
                       TypeRef(rt).kind() == LogosType::Kind::Ptr) &&
                      TypeRef(rt).pointee() &&
                      TypeRef(TypeRef(rt).pointee()).kind() == LogosType::Kind::Array));
                if (from_array) {
                    // Use the bare array type (peel any &/&mut/* prefix the
                    // receiver came in with) so the message reads `[T; N]`,
                    // matching rustc's E0508 wording exactly.
                    TypeRef arr_t = (TypeRef(rt).kind() == LogosType::Kind::Array)
                        ? rt : TypeRef(rt).pointee();
                    error(std::format(
                        "cannot move out of type `{}`, a non-copy array",
                        type_str(arr_t)));
                }
            }
        }
    }

    std::string drop_fn_for(TypeRef t) const;
    // #123 — `#[no_auto_drop]` on the struct behind `t` (see sema.cpp).
    bool type_no_auto_drop(TypeRef t) const;
    bool has_droppable_fields(TypeRef t) const;
    bool needs_drop(TypeRef t) const {
        return !drop_fn_for(t).empty() || has_droppable_fields(t);
    }

    // `extra_moved` is UNIONED into the `moved_vars_`-derived skip list
    // (relative paths). Two callers need it, for the same reason:
    //   • #121-A, the ancestor's guarded drop — emitted through a fresh temp
    //     (`__cmfd_N`), a name `moved_vars_` has never heard of, so the derived
    //     list is empty by construction and the drop destroys the subtree AS A
    //     UNIT, including a descendant the taken path already moved out;
    //   • #121-A, the CONTAINER's own drop — a path that carries a drop FLAG is
    //     owned by that flag's guarded drop, and the container must never
    //     recurse into it. `moved_vars_` says so only when the merge happened
    //     to leave the path in it, which a merge INSIDE A LOOP BODY does not
    //     (the per-branch save/restore reverts it), so the field was destroyed
    //     once unguarded by the container and once by its flag.
    // The two rules are one rule: a subtree's ownership is not one bit, and
    // whoever holds the bit for a sub-place owns it alone.
    std::optional<lir_view::StmtRef> make_drop_stmt(
        const std::string& name, const VarInfo& info,
        const std::vector<std::string>* extra_moved = nullptr) const;

    // #121-A — the paths under `root` that no ENCLOSING drop may recurse into,
    // spelled RELATIVE to `root`: those carrying their OWN drop flag (destroyed
    // by that flag's guarded drop in `emit_cond_move_field_drops`), and those a
    // merge proved moved on every reaching path (destroyed by whoever took
    // them). Both are witnesses `moved_vars_` cannot be trusted to still hold —
    // it is reverted per branch, and a merge inside a loop body loses it.
    std::vector<std::string> flagged_descendants(const Frame& frame,
                                                 const std::string& root) const {
        std::vector<std::string> out;
        std::string pre = root + ".";
        auto add = [&](const std::string& q) {
            if (q.size() <= pre.size()) return;
            if (q.compare(0, pre.size(), pre) != 0) return;
            std::string rel = q.substr(pre.size());
            for (auto& o : out) if (o == rel) return;
            out.push_back(std::move(rel));
        };
        for (auto& [q, _f] : frame.cond_move_flags) add(q);
        for (auto& q : frame.cond_move_static_moves) add(q);
        return out;
    }
    // #121 — the guarded destructors for conditionally-moved FIELD PATHS
    // rooted at `root`. See the definition in sema.cpp.
    void emit_cond_move_field_drops(const Frame& frame, const std::string& root,
                                    std::vector<lir_view::StmtRef>& drops) const;
    std::vector<lir_view::StmtRef> collect_drops() const;
    std::vector<lir_view::StmtRef> collect_all_drops() const;
    // G167-4: drops for a `break`/`continue` — every frame from the innermost
    // down to AND INCLUDING the enclosing loop-body frame (loop_boundary), so a
    // break/continue nested in an `if` still runs the loop body's destructors.
    std::vector<lir_view::StmtRef> collect_drops_to_loop() const;

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
            // #110 R1 — `return t.0;` DROPPED THE ELEMENT TWICE. This switch
            // had a FieldRead case and no TupleIndex one, so the tuple-element
            // path that `mark_moved_expr` has held since G154-4 was simply not
            // reached from the two consumer positions that route through here
            // (lower_return, lower_match's arm value). `t`'s scope-exit SDrop
            // then got no `moved_fields=["0"]` and freed element 0 that the
            // returned value already owned. MEASURED 2026-08-22: `fn prod() ->
            // W { let t: (W,i64) = (W{..}, 1); return t.0; }` printed two
            // destructor lines; the byte-identical `let d: W = t.0; return d;`
            // (which goes through lower_let, not this switch) printed one, and
            // `return h.f;` on a STRUCT field printed one. Concrete carrier, no
            // enum and no generics — so this is NOT the auto-Copy root above.
            case C::TupleIndex:
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
            // #110 — AN INDIRECT CALL CONSUMES ITS ARGUMENTS TOO. Only the
            // direct `Call` code was walked, so `return pred(v);` with `pred:
            // fn(T) -> bool` left `v` unmarked and the payload was dropped by
            // BOTH the callee and the arm. MEASURED 2026-08-22: the generic
            // `fn isa<T>(s: Option<T>, pred: fn(T)->bool) { match s { Some(v)
            // => return pred(v), … } }` printed two destructor lines; swapping
            // the fn-POINTER for a direct generic call `eat(v)` printed one,
            // and so did binding the result first — the difference is exactly
            // this expression code. `logos.lang.option.Option::is_some_and` is
            // that body, so the stdlib combinator double-dropped its payload.
            // (A method call's by-value args are marked at their own site —
            // MEASURED one drop both concretely and through a generic body —
            // so MethodCall is deliberately NOT added here.)
            case C::FnPtrCall:
                lir_view::EFnPtrCallView{er}.each_arg(
                    [&](lir_view::ExprRef a) { mark_moved_in_expr_recursive(a); });
                return;
            case C::ClosureCall:
                lir_view::EClosureCallView{er}.each_arg(
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

    // Phase-1: pattern bindings reserve their slot at BUILD time (before the
    // pat node is emitted, since define() runs after via define_bindings).
    // Build-local name→slot map; cleared at each top-level pattern build entry
    // so Or-pattern alternatives (`Some(x)|Other(x)`) share ONE slot for `x`.
    std::unordered_map<std::string, uint32_t> pat_bind_slots_;
    uint32_t pattern_build_depth_ = 0;  // 0 at top-level pattern build (clear point)
    uint32_t reserve_pat_slot(std::string_view name) {
        std::string sn(name);
        auto it = pat_bind_slots_.find(sn);
        if (it != pat_bind_slots_.end()) return it->second;  // Or-dedup
        uint32_t s = next_slot_++;
        pat_bind_slots_.emplace(std::move(sn), s);
        return s;
    }
    void clear_pat_bind_slots() { pat_bind_slots_.clear(); }

    // reuse_slot != NO_SLOT → bind reuses an already-reserved slot (pattern
    // bindings, set at build time) instead of consuming a fresh one.
    void define(std::string_view name, TypeRef t, bool is_mut = false,
                uint32_t reuse_slot = 0xFFFFFFFFu) {
        // Zone Step 4 (pin): a non-movable (location-anchored) type may never
        // occupy a by-value slot — a `let` local, a parameter, or a `match` /
        // `for` / closure / destructure binding. define() is the SINGLE registrar
        // for all of them, so this one check covers every by-value slot uniformly.
        // (The two remaining by-value positions are handled at their own choke
        // points: a by-value RETURN in sema_decl::lower_fn, and a by-value CONSUME
        // into a non-slot destination — field-write / assignment — in
        // mark_moved_expr.) The backing SEGMENT is a `[u8; N]` buffer (not anchored)
        // and pointers/refs are unflagged, so `*mut T` slots and field-wise
        // in-place construction through a pointer stay legal — the value just never
        // lives by value on the stack, where its self-relative anchor would break.
        if (t && is_non_movable_type(t))
            error(std::format(
                "cannot bind `{}`: type `{}` is location-anchored (a self-relative "
                "`#[rel_ptr]` field, or a `#[pinned]` type) and may not occupy a "
                "by-value slot — it must live behind a pointer, in place (e.g. an "
                "arena / `[u8; N]` buffer), built through a `*mut {}`)",
                std::string(name), type_str(t), type_str(t)));
        if (!scope_.empty()) {
            auto sname = std::string(name);
            if (!scope_.back().vars.count(sname))
                scope_.back().var_order.push_back(sname);
            // Phase-1: fresh dense slot per binding (shadowing → new slot),
            // unless a pattern pre-reserved one at build time.
            uint32_t slot = (reuse_slot == 0xFFFFFFFFu) ? next_slot_++ : reuse_slot;
            scope_.back().vars[sname] = {t, is_mut, false, slot};
        }
    }

    TypeRef lookup(std::string_view name) const {
        for (auto it = scope_.rbegin(); it != scope_.rend(); ++it) {
            auto f = it->vars.find(std::string(name));
            if (f != it->vars.end()) return f->second.type;
        }
        // G156-1: package-scoped const — resolve current-package first.
        std::string ck = resolve_const_key(name);
        if (!ck.empty()) {
            auto cit = module_consts_.find(ck);
            if (cit != module_consts_.end()) return cit->second;
        }
        return nullptr;
    }

    // Phase-1: resolve a NAME to its current in-scope variable slot (innermost
    // wins → shadowing-correct). Returns UINT32_MAX for names that aren't a
    // local binding (module consts, unresolved/synthesized refs); such EVarRefs
    // carry no slot and downstream falls back to name-keying.
    static constexpr uint32_t NO_SLOT = 0xFFFFFFFFu;
    uint32_t lookup_slot(std::string_view name) const {
        for (auto it = scope_.rbegin(); it != scope_.rend(); ++it) {
            auto f = it->vars.find(std::string(name));
            if (f != it->vars.end()) return f->second.slot;
        }
        return NO_SLOT;
    }

    bool lookup_is_mut(std::string_view name) const {
        for (auto it = scope_.rbegin(); it != scope_.rend(); ++it) {
            auto f = it->vars.find(std::string(name));
            if (f != it->vars.end()) return f->second.is_mut;
        }
        return false;
    }

    // An owning `Box<dyn Trait>` binding (collapsed to bare TraitObject). Passing
    // it by value MOVES it (the callee frees the handle), so the caller must
    // mark it moved to avoid a double-free. Mirrors lookup_is_mut.
    bool lookup_owning_dyn(std::string_view name) const {
        for (auto it = scope_.rbegin(); it != scope_.rend(); ++it) {
            auto f = it->vars.find(std::string(name));
            if (f != it->vars.end()) return f->second.owning_dyn;
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
                            bool is_pub = false;
                            bool is_module_only = false;  // §4: `pub(module)`
                            std::string source_file;
                            std::string package;
                            std::string module_id;  // owning-module id (mangle key); empty = no module
                            bool is_data_plain = true;  // false if any field is Kind::ZonedStruct
                            bool is_annotation_type = false;  // #[annotation] datatype (see StructDraft::is_annotation_type)
                            bool is_tuple_struct = false;  // B-ts-01: `struct Foo(T1, T2);` — positional fields, ctor is `Foo(a, b)` and pattern is `Foo(x, y)`
                            bool no_auto_drop = false;  // `#[no_auto_drop]` — compiler emits NO auto-Drop (user drop + field drop) for this struct. ManuallyDrop<T> lang-item shape.
                            // Phase 1B-13: custom DST — the LAST field has
                            // unsized type (`[T]` / `dyn Trait` / nested DST).
                            // The struct itself becomes unsized; can only
                            // appear behind `&`/`&mut`/`*const`/`*mut` /
                            // `Box`. Construction goes through unsafe raw-
                            // parts assembly (no by-value).
                            bool is_dst = false;
                            // `#[self_describing]`: a custom-DST whose tail
                            // length/metadata is recoverable in-band, so raw
                            // `*mut/*const T` is a THIN pointer (meta recovered
                            // at deref) rather than a fat DstRef. (ref-repr §6)
                            bool self_describing = false;
                            // `#[rel_ptr]`: a self-relative pointer type — stored as
                            // an 8B i64 byte-offset from the field's own address,
                            // materialized to an absolute thin ptr on load (RefRepr
                            // RelOffset). Opaque (no field access); transparent to
                            // `*Pointee` at the value level. (ref-repr §6 / zone-as-parameter)
                            bool rel_ptr = false;
                            // `#[pinned]`: a location-anchored at-rest type (e.g. the
                            // relative WAnyRel) that must NOT be moved by value —
                            // its bits are anchored to its storage slot; it is accessed
                            // in place and materialised explicitly to a movable value
                            // form. Non-movable itself (unlike #[rel_ptr], whose value
                            // form is the resolved absolute pointer). Drives the
                            // is_non_movable_type pin check. (writ minimal-container)
                            bool pinned = false;
                            // `#[zone_mut]`: a `&mut T` to this type is a FAT ref
                            // {data, zone=*mut Allocator} carrying its Writ zone, so
                            // grow methods reach the allocator from &mut self. Read
                            // `&T` stays thin. (writ-zone-mut-fat-ref §)
                            bool zone_mut = false;
                            // `#[zoned2]`: all thin pointer fields of this struct are
                            // stored SELF-RELATIVE (RelOffset i64) and materialize to
                            // absolute pointers in compute — the untagged zoned-ref
                            // case (ref-repr §6). Non-movable (can't be stack-allocated;
                            // offsets are anchored to the slot). (writ ptr foundation)
                            bool zoned2 = false;
                            // `#[borrow_carrying]`: a value type whose value may
                            // contain a borrow — an absolute Ref into an arena (e.g.
                            // WAny). The borrow checker tracks such values' ESCAPE
                            // like a reference: a method/ctor returning one ties the
                            // result to its ref receiver/arg, so returning it past
                            // the source's scope is rejected unless laundered through
                            // a holder (HeldAny). (writ WAny escape safety)
                            bool borrow_carrying = false;
                            // `#[non_null]`: this struct is a single 8-byte pointer
                            // wrapper whose pointer is GUARANTEED non-null (Box/Rc/Arc).
                            // Lets `Option<ThisStruct>` use the NullPtr niche (None =
                            // the null pointer, so the enum is pointer-sized). Opt-in
                            // soundness contract — the struct author asserts no live
                            // value holds a null pointer.
                            bool non_null = false;
                            // logos-core §6.1: this type was declared as `union NAME { … }`
                            // rather than `struct`. Layout is max-of-fields aligned to
                            // max-alignment (vs struct's sum-of-fields); every field READ
                            // requires enclosing `unsafe` (Rust soundness contract — only
                            // one field is "active" at a time and the active one is
                            // implementation-defined). Set in `collect_struct` when the
                            // dispatching item code is `UNION_DEF`.
                            bool is_union = false;
                            // logos-core 1.5: `#[repr(transparent)]` — single-field
                            // wrapper inherits its field's layout exactly
                            // (size + align + niche). Set by sema_collect when
                            // the annotation is seen; layout consumed by
                            // layout-aware passes (mlir-gen `logos_abi_byte_size`,
                            // niche optimization). Other `#[repr(...)]` modes
                            // (`C`/`packed`/`align`) are NOT covered yet —
                            // they parse-then-reject so silent drift is
                            // impossible.
                            bool repr_transparent = false;
                            // Partial-spec support: when this is a specialization,
                            // base_name is the generic template (e.g. "Map") and
                            // spec_patterns holds one entry per type-param slot
                            // (either concrete types or TypeVars for partial specs).
                            std::string base_name;
                            std::vector<TypeRef> spec_patterns;
                            std::string doc;     // outer `///` doc-comment
                            // ── ADR 0011: Writ schemas ──
                            // Declared via `schema S {…}` — a typed VIEW over a
                            // map-like Writ object (backing = WMap<Wu6,WAny> TOM).
                            // The ONLY real struct field (in `fields`) is the
                            // synthetic `m: *const WMap<Wu6,WAny>`; the user's
                            // declared sugar fields live in schema_fields/_keys and
                            // are accessed via desugared get/set (NOT struct fields).
                            bool is_schema = false;
                            uint64_t schema_type_code = 0;             // `code(expr)` value; 0 = none
                            std::vector<SemaFieldInfo> schema_fields;  // declared fields (name+type)
                            std::vector<uint8_t> schema_keys;          // parallel TOM key (0..51)
                            // `schema enum E { V(S), … }` — closed union over schemas.
                            // The view is the same {m} struct; the concrete schema is
                            // recovered at `match` from the pointee's schema_type_code.
                            bool is_schema_enum = false;
                            std::vector<std::pair<std::string, TypeRef>> schema_variants; // variant name → concrete schema view type
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
                            // §4: `pub(module)` — exported within the owning module
                            // only (module-linkage), not to consumers. Implies
                            // is_pub (crosses package boundaries within the module).
                            bool is_module_only = false;
                            bool is_extern = false;
                            bool is_fn_macro = false;  // #[fn_macro] callee for name!(...)
                            bool is_token_macro = false; // #[token_macro] callee (str RAW_TEXT)
                            std::string base_name;
                            // SEPARATOR CLASS — the two facts every consumer
                            // used to re-derive from `base_name.find("__")`.
                            // A free fn may legally be named `a__b`, and a
                            // method `b` on struct `a` mangles to the SAME
                            // `a__b`; the string cannot tell them apart, and
                            // asking it produced a wrong "duplicate function"
                            // rejection of legal code (measured). Both are
                            // known at collect_fn/lower_fn — carry them.
                            std::string owner_struct;   // == struct_ctx; "" for a free fn
                            bool        is_method = false;   // the FACT, not a spelling
                            std::string signature_key;
                            std::string symbol_name;
                            std::string source_file; std::string package;
                            // UnitGraph §1.2: the compile UNIT this fn was
                            // DECLARED into — the emitter's key for a generated
                            // chunk, the file for an ordinary source fn. Never
                            // empty for a sema-lowered fn; mono clones do not
                            // inherit it (absence ⟺ derived owner).
                            std::string unit_key;
                            std::string module_id;  // owning-module id (mangle key); empty = no module
                            std::string doc;     // outer `///` doc-comment
                            // Trait-aware method mangling: the trait this
                            // method implements (`impl Trait for X`), empty for
                            // inherent impls / free fns. When two traits define
                            // the same method on the same type+signature, the
                            // colliding methods are lazily re-keyed under the
                            // trait-qualified base `<target>__<trait>__<method>`
                            // (see trait_method_registry_).
                            std::string trait_name;
                            // G156-1: the impl's concrete trait type-args
                            // (`impl Trait<u64> for X` → [u64]). Two impls of the
                            // same trait name for one type at distinct args mangle
                            // their methods by these so they coexist + dispatch.
                            std::vector<TypeRef> trait_type_args;
                            // logos-core 1.1: Rust-2024 `!`-fallback for
                            // inference vars unified ONLY against Never.
                            // Discriminator: did the callee's body always
                            // diverge (panic-tail / loop {}-tail)? If yes
                            // AND a type-param is otherwise unbound at the
                            // callsite, fall back to `!`. Computed by
                            // `body_always_diverges_simple` at collect time.
                            // Distinguishes `fn f<T>() -> T { panic(); }`
                            // (T → ! via fallback) from `fn f<T>() -> T
                            // { return 0; }` (T unbound → ambiguous error).
                            bool body_always_diverges = false;
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
        bool is_pub = false;                 // T1-9: cross-pkg visibility
        bool is_module_only = false;          // §4: `pub(module)`
        std::string package;                 // pkg this enum was declared in
        std::string module_id;               // owning-module id (mangle key); empty = no module
        std::vector<TypeParam> type_params;  // for generic enums
        std::vector<std::string> lifetime_params;  // B65: enum lifetime params
        TypeRef backing_type = nullptr;  // null = default (i32)
        std::string doc;     // outer `///` doc-comment
        bool zoned2 = false; // #[zoned2]: niche enum's Ref arm self-relative at-rest (F3)
        bool borrow_carrying = false; // #[borrow_carrying]: a value (WAny) may hold a Ref into an arena
    };

    // ── Trait info ───────────────────────────────────────────────
    struct SemaTraitMethodInfo {
        std::string name;
        std::vector<TypeParam> type_params;  // method-level: `fn hash<H: Hasher>(...)` has [H]
        std::vector<TypeRef> param_types;  // includes self
        TypeRef ret_type = nullptr;
        bool has_default = false;   // trait method has a default body
        bool is_unsafe = false;     // declared unsafe fn in trait
        bool has_self_receiver = false;  // first param is `self`/`&self`/`&mut self`/`self: …`
        bool requires_sized_self = false;  // `where Self: Sized` → excluded from the
                                            // vtable (ignored for object-safety, P2-15)
        // §8.5: per-method where-clause bounds whose subject is a TRAIT
        // type-param (e.g. `where Item: Ord` on `fn max()` in
        // Iterator<Item>). Captured here for the per-impl default-method
        // gating in lower_target — when an impl substitutes Item with a
        // concrete type, the bound is rewritten and looked up in impls_
        // (sema_has_impl_recursive). A failing bound skips default synth
        // for that impl (the method is simply unavailable, matching
        // Rust's conditional-default-method semantics). Self-side bounds
        // (`where Self: Sized`) live in `requires_sized_self` above.
        struct ParamBound {
            std::string param_name;   // the trait type-param being bounded (Item)
            std::string trait_name;   // the required trait (Ord)
        };
        std::vector<ParamBound> where_param_bounds;
        writ::AnyVal default_ast{};    // AST node for default method (valid when has_default)
        writ::MemHolder* default_holder = nullptr;  // zone that owns default_ast
        std::string doc;     // Phase A.2: outer `///` doc-comment
    };
    struct SemaAssocTypeInfo {
        std::string name;              // e.g. "Item"
        std::vector<TraitBound> bounds;
        std::vector<TypeParam>  type_params;  // GAT params: type Item<T> has [T]
        TypeRef default_type = nullptr; // §3 c13: `type Item = i32;` default
        std::string doc;     // Phase A.3: outer `///` doc-comment
    };
    struct SemaAssocConstInfo {
        std::string      name;         // e.g. "MAX"
        TypeRef type = nullptr;
        bool has_default = false;      // §6 f1: `const X: i32 = 42;` default
        // T2-14: the default value's AST node (when has_default) — projected
        // into `assoc_const_impls_` for each `impl Trait for T {}` that omits
        // the const, so `T::CONST` resolves to the trait default.
        writ::AnyVal   default_value_ast;
        std::string doc;     // Phase A.3: outer `///` doc-comment
    };
    struct SemaTraitInfo {
        std::string name;
        bool is_pub = false;                  // T1-9: cross-pkg visibility
        bool is_module_only = false;           // §4: `pub(module)`
        std::string package;                  // pkg this trait was declared in (for B-mv-02 diag)
        std::vector<TypeParam> type_params;  // e.g. trait Into<T> has T
        std::vector<SemaTraitMethodInfo> methods;
        std::vector<SemaAssocTypeInfo>   assoc_types;
        std::vector<SemaAssocConstInfo>  assoc_consts;
        std::vector<TraitBound> supertraits;  // e.g. [{Display,[]}, {Into,[i32]}] for trait Foo: Display + Into<i32>
        bool is_unsafe = false;               // declared as `unsafe trait`
        bool is_auto   = false;               // declared with `auto trait`
        bool is_writ = false;               // trait carries #[type_code] —
                                              // Writ-tagged datatype family;
                                              // reflect::<T>() routes through
                                              // Writ path
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
        // B-mv-02: canonical (scope-resolved) registry key of the implemented
        // trait, captured at collect time when cur_package_ is the impl's own
        // package. Lets later GLOBAL passes (supertrait verification) that
        // iterate impls_ without per-impl scope resolve to the SAME trait the
        // impl actually targets, instead of whatever same-name trait holds the
        // bare slot. Empty ⇒ fall back to bare trait_name (non-colliding).
        std::string canonical_trait;
        // ── A LOOKUP KEY IS NOT AN IDENTITY: THE TARGET HALF (#88) ───────
        // The impls_ key is `Trait::Target` with a BARE target, so the stdlib's
        // `Copy::TypeId` and a user package's `Drop::TypeId` land on one key.
        // `target_typeref` cannot answer: it is documented as set only for
        // GENERIC-target impls, and MEASURED 2026-08-21 it is null for every
        // plain `impl Drop for X` (the resolve has not run yet at collect time).
        // So the identity is captured HERE, at the one moment cur_package_ is
        // the impl's own package AND the local declaration registry can be
        // asked: non-empty exactly when THIS package declares a type of that
        // name, i.e. when `target` denotes a LOCAL type rather than a foreign
        // one. Empty ⇒ the target is whatever owns the bare slot, unchanged.
        std::string target_pkg;
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
    // g4/K5: `impl Trait` parameter desugar. When active (set around a
    // function's param-type resolution in collect_fn / lower_fn), resolve_type
    // turns each top-level `impl <bound>` param type into a fresh synthetic
    // generic type-param (collected in pending_impl_trait_params_) and returns
    // its TypeVar — the param is then an ordinary generic. Each site drains the
    // pending params into the function's type_params. `impl Trait` in RETURN
    // position keeps the existing ImplTrait handling (flag is false there).
    bool impl_param_desugar_active_ = false;
    std::vector<TypeParam> pending_impl_trait_params_;
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
    bool try_append_doc(std::string& buf, writ::TinyMapView node) {
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
    // collect_trait reads + clears it; SemaTraitInfo.is_writ carries the
    // result through to reflect::<T>() dispatch.
    bool pending_trait_is_writ_ = false;
    // Mangled name of the currently-being-lowered fn. Used by make_drop_stmt
    // to skip auto-drop on the `self` parameter of a Drop fn (would be
    // infinite self-recursion).
    std::string current_fn_mangled_;

    // Re-export graph: pkg_reexports_["a.b"] = ["x.y", "z"] means `pub use x.y; pub use z;`
    // is declared in package a.b. Used by find_* helpers for transitive import resolution.
    logos::compiler::StrMap<std::vector<std::string>> pkg_reexports_;

    logos::compiler::StrMap<SemaStructInfo>   structs_;
    logos::compiler::StrMap<SemaStructInfo>   datatypes_;  // Writ datatypes
    // Explicit type_code from #[type_code=N] annotations; populated in lower_module_items.
    logos::compiler::StrMap<uint64_t>         explicit_type_codes_;
    // concrete_name (e.g. "Pair__i32") → SemaStructInfo for explicit specializations.
    logos::compiler::StrMap<SemaStructInfo>   struct_specs_sema_;
    // True when ANY (partial) specialization is registered for this struct
    // base name — a spec may govern instantiations the base's params would
    // reject (unsized args at a slice-pattern position), so gates that
    // consult the base's type_params relax when this holds.
    // Identity of each struct name's FIRST declaration (pass-0), so the
    // bounded-pattern spec rule in is_specialization_struct can tell "the
    // base decl revisited in a later pass" (its own bounds are ordinary
    // param bounds) from "a genuine SECOND decl" (a bound-discriminated
    // specialization).
    logos::compiler::StrMap<std::pair<void*, uint32_t>> first_struct_decl_;
    bool struct_has_specs(std::string_view base_name) const {
        for (auto& [k, info] : struct_specs_sema_)
            if (info.base_name == base_name) return true;
        return false;
    }
    // Any spec of this struct discriminated by pattern-var BOUNDS?
    // (Symbol disambiguation for same-signature impl methods keys off this.)
    bool struct_has_bounded_spec(std::string_view base_name) const {
        for (auto& [k, info] : struct_specs_sema_) {
            if (info.base_name != base_name) continue;
            for (auto& tp : info.type_params)
                if (!tp.bounds.empty()) return true;
        }
        return false;
    }
    // NAME-ONLY bound reader for spec patterns: TRAIT_BOUND names per
    // TYPE_PARAM, relaxed (`?Trait`) markers dropped, NO type resolution —
    // spec patterns may carry parametrized bounds whose args (assoc types
    // etc.) can't resolve in this context; the bound-spec gate only needs
    // trait names.
    std::vector<TypeParam> read_spec_pattern_bounds(writ::TinyMapView node) {
        std::vector<TypeParam> out;
        if (!node.has_key(sema_detail::la::TYPE_PARAMS)) return out;
        writ::AnyVal tpav = node.get(sema_detail::la::TYPE_PARAMS.code);
        if (tpav.is_null()) return out;
        auto tplist = map_of(tpav);
        if (!tplist.has_key(sema_detail::la::ITEMS)) return out;
        auto items = arr_of(tplist.get(sema_detail::la::ITEMS.code));
        for (uint64_t i = 0; i < items.size(); ++i) {
            auto n = map_of(items.get(i));
            if (code_of(n) != sema_detail::la::TYPE_PARAM) continue;
            TypeParam tp;
            tp.name = std::string(str_of(n.get(sema_detail::la::NAME.code)));
            if (n.has_key(sema_detail::la::ITEMS)) {
                auto bs = arr_of(n.get(sema_detail::la::ITEMS.code));
                for (uint64_t b = 0; b < bs.size(); ++b) {
                    auto bn = map_of(bs.get(b));
                    if (code_of(bn) != sema_detail::la::TRAIT_BOUND) continue;
                    writ::AnyVal rv = bn.get(sema_detail::la::RELAXED.code);
                    if (!rv.is_null() && rv.is_value() &&
                        rv.as_value<uint8_t>() != 0) continue;
                    TraitBound tb;
                    tb.trait_name = std::string(str_of(bn.get(sema_detail::la::NAME.code)));
                    tp.bounds.push_back(std::move(tb));
                }
            }
            out.push_back(std::move(tp));
        }
        return out;
    }
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
        std::string     package;  // B-mv-02: owning package for cross-pkg coexistence
        // ADR 0021 Phase 4a: RHS AST, retained for GENERIC aliases only. When
        // the RHS instantiates a generic const (`type PMap<K,V> =
        // CtrClass<PMapCfg<K,V>>`), decl-time resolution erases the
        // typevars into one WStaticLit hash (typevar NAMES get hashed) — every
        // instantiation would share it. Use sites therefore RE-RESOLVE the
        // saved RHS under the concrete bindings (exactly the generic-const
        // mechanism), yielding a fresh per-instantiation identity. Gated at
        // use time on the RHS actually mentioning a generic const.
        writ::TinyMapView rhs_node{};
        writ::MemHolder*  holder = nullptr;   // module-of-decl holder
    };

    // Lifetime substitution map: "'z" → "'a"  (name → name, erased at codegen).
    using SemaLifetimeSubst = logos::compiler::StrMap<std::string>;
    logos::compiler::StrMap<TypeAliasEntry> type_aliases_;
    // G156-1: package-scoped consts (Rust parity). module_consts_ /
    // module_const_values_ are keyed by the PACKAGE-QUALIFIED key
    // sema_key(pkg, name) so two `pub const FOO` in different packages coexist.
    // Resolution of a bare `FOO` prefers cur_package_ then a uniquely-named
    // global/imported const; a bare use of a name declared in ≥2 packages (none
    // in cur_package_) is an "ambiguous const" error. const_pkg_of_ maps a bare
    // const name → its sole owning package (for the unique-bare fallback);
    // ambiguous_const_names_ holds names seen in ≥2 packages.
    logos::compiler::StrMap<TypeRef> module_consts_;
    // P4-pm-06: AST node of each module-const initializer, retained so
    // `match x { CONST => … }` can ctfe-eval CONST and lower as a value
    // pattern (PAT_INT/PAT_BOOL/PAT_CHAR) instead of silently binding
    // the scrutinee to a fresh local named CONST.
    logos::compiler::StrMap<writ::TinyMapView> module_const_values_;
    std::unordered_map<std::string, std::string> const_pkg_of_;   // bare name → sole pkg
    std::unordered_set<std::string> ambiguous_const_names_;         // bare name in ≥2 pkgs

    // G156-1: register (pkg, name) into the const uniqueness index. Returns true
    // if this is the FIRST time the bare name is seen (bare fallback available).
    void const_index_add(std::string_view pkg, std::string_view name) {
        auto [it, ins] = const_pkg_of_.emplace(std::string(name), std::string(pkg));
        if (!ins && it->second != pkg) ambiguous_const_names_.insert(std::string(name));
    }
    // Resolve a bare const name to its package-qualified map key, current package
    // first, then a uniquely-named const. Empty ⇒ not a const OR ambiguous.
    std::string resolve_const_key(std::string_view name) const {
        std::string q = sema_key(cur_package_, name);
        if (module_consts_.count(q)) return q;
        if (!ambiguous_const_names_.count(std::string(name))) {
            auto it = const_pkg_of_.find(std::string(name));
            if (it != const_pkg_of_.end()) {
                std::string k = sema_key(it->second, name);
                if (module_consts_.count(k)) return k;
            }
        }
        return {};
    }
    // True iff a bare use of `name` is ambiguous in cur_package_ (declared in ≥2
    // packages, none of them cur_package_).
    bool const_name_ambiguous_here(std::string_view name) const {
        return ambiguous_const_names_.count(std::string(name))
            && !module_consts_.count(sema_key(cur_package_, name));
    }
    // Resolve a bare const name to its value-initializer AST (current package
    // first). Null view ⇒ not found / ambiguous.
    writ::TinyMapView resolve_const_value(std::string_view name) const {
        std::string k = resolve_const_key(name);
        if (k.empty()) return {};
        auto it = module_const_values_.find(k);
        return it != module_const_values_.end() ? it->second : writ::TinyMapView{};
    }
    // §6.2: names of `static mut` items declared in this module —
    // var-reads and place-assigns require an enclosing `unsafe` block
    // per Rust spec `items.static.mut.safety`. Populated by
    // `collect_const` when the routing layer sees a STATIC_DEF code.
    std::unordered_set<std::string> module_static_muts_;

    // §6.2 statics (S25): ALL `static [mut]` items of this module, name →
    // link symbol ("<pkg>$<NAME>"; extern-block decls keep the bare name).
    // Reads lower as Deref(VarRef("__static_addr:<sym>", *T)) and writes as
    // SDerefWrite through the same address expr, so the whole class rides
    // the canonical place machinery; mlir-gen's only special case is the
    // "__static_addr:" prefix → llvm.mlir.addressof of the global.
    logos::compiler::StrMap<std::string> module_statics_;

    // True iff `name` is a module static NOT shadowed by a local binding or
    // a type/const-generic param (mirrors the module_static_muts_ S18 rule).
    // ── ONE NAME FOR "DOES NAMING THIS STATIC HERE REQUIRE `unsafe`" ────────
    // `lower_var_ref` (a READ) and `lower_place_assign` (a WRITE) each asked
    // this and each carried its own copy of the three exemptions — module-scope
    // shadowing, const-generic name pollution (§6.2 S18), and extern-vs-`mut`.
    // The two BORROW paths (`&S` / `&mut S`) route around `lower_var_ref`
    // entirely, so they asked it NOWHERE and a `&mut` of a mutable static —
    // strictly stronger than either a read or a write — was admitted outside
    // `unsafe` while both weaker forms refused. This predicate is that question
    // asked once; the callers supply only the NOUN for the diagnostic.
    // `is_extern` distinguishes the two upstream rules
    // (items.extern.static vs items.static.mut.safety).
    bool static_access_needs_unsafe(std::string_view name, bool& is_extern) const {
        std::string n(name);
        const bool ext = module_extern_statics_.count(n) != 0;
        if (!ext && module_static_muts_.count(n) == 0) return false;
        // Shadowed by a LOCAL in any frame: the name denotes the binding, not
        // the global. A SET OF STRINGS CANNOT SAY WHICH BINDING A NAME DENOTES.
        for (auto it = scope_.rbegin(); it != scope_.rend(); ++it)
            if (it->vars.count(n)) return false;
        // Const-generic parameters share the name namespace with statics.
        if (current_type_params_.count(n)) return false;
        is_extern = ext;
        return true;
    }

    bool is_module_static_unshadowed(std::string_view name) const {
        if (module_statics_.find(std::string(name)) == module_statics_.end())
            return false;
        for (auto it = scope_.rbegin(); it != scope_.rend(); ++it)
            if (it->vars.count(std::string(name))) return false;
        if (current_type_params_.count(std::string(name))) return false;
        return true;
    }

    // The address-expr name for a static: "__static_addr:<sym>".
    std::string static_addr_name(std::string_view name) const {
        auto it = module_statics_.find(std::string(name));
        return "__static_addr:" + (it != module_statics_.end()
                                       ? it->second : std::string(name));
    }

    // §6.1: transient flag set by `lower_place_assign` (and similar
    // write-path lowerings) before calling `lower_expr` on the LHS
    // place. `lower_field_read` consults it to skip the union-field
    // unsafe gate for WRITES — Rust spec writes to union fields are
    // safe (`items.union.fields.write-safety`), only READS need
    // `unsafe` (`items.union.fields.read-safety`). The flag is
    // RAII-restored at the end of each write-path lowering.
    bool in_place_write_lhs_ = false;

    // The field auto-deref step in `lower_field_read` must cross a user
    // `Deref` impl MUTABLY when the field is being borrowed mutably —
    // `&mut b.f` on a `Box<S>` is `&mut (*b).f` and `*b` there is
    // `b.deref_mut()`. The method sibling asks the same question with
    // `target_method_wants_mut_self`; a field has no method to ask about, so
    // the use context has to be carried in. The same channel answers the same
    // question for an EXPLICIT deref: a `match *b { … ref mut y … }` scrutinee
    // (see `arms_bind_ref_mut`).
    // SET by the `&mut <field place>` arm of `lower_expr` and by the match
    // scrutinee, both restoring what they saved; CONSUMED and cleared by
    // `lower_field_read` (which re-arms it only for a receiver that is itself a
    // field place, so `&mut a.b.f` steps mutably at both levels) and by
    // `lower_deref`. Every other position lowers with it FALSE — an over-eager
    // mutable step would refuse `&mut f(a.b)` for a non-`mut` `a`, and a
    // `Deref`-only type has no `deref_mut` to call at all.
    bool mut_place_ctx_ = false;

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

    // Generic compile-time consts: `pub const X<T1, T2, …>: WritStatic =
    // @{ … <type:T1> … };`. Stores the templated value-AST node + type-params
    // declaration. At each use-site `X<concrete1, concrete2>` sema pushes the
    // type-args into current_type_params_ and re-resolves the value-AST under
    // that scope — slot lookups (`<type:T1>` WRIT_TYPE_LIT) hit the bound
    // type, and the FNV hash of the AST walk substitutes the TypeVar name
    // for the concrete type's str representation, yielding a fresh
    // per-instantiation WStaticLit identity.
    struct GenericConstEntry {
        std::vector<TypeParam>  type_params;
        writ::TinyMapView     value_node;   // AST of the RHS writ_lit
        writ::MemHolder*      holder = nullptr;  // module-of-decl holder
    };
    logos::compiler::StrMap<GenericConstEntry> generic_consts_;
    // A Writ document is a TEMPLATE when a `<type:T>` slot still holds an open
    // parameter: `@{ "key": <type:K>, "value": <type:V> }` under generic K/V.
    // Such a document has CONTENT but no VALUE — and everything downstream of
    // a config doc keys on its content HASH, so hashing one anyway mints a
    // perfectly well-formed identity for a configuration that does not exist.
    // (Measured before this existed: `typeof(Map::<K, V>)` generated a family
    // for the literal type name "K" and died as `unknown type '<error>'`.)
    // hash → the open parameter names, in document order, for the diagnostic.
    std::unordered_map<uint64_t, std::string> parametric_wstatic_;
    // Hashes already reported as templates — one diagnostic per document, not
    // one per use site.
    std::unordered_set<uint64_t> parametric_reported_;
    logos::compiler::StrMap<SemaTraitInfo>    traits_;
    // "TraitName::TypeName" → impl info
    logos::compiler::StrMap<SemaImplInfo>     impls_;
    // Same key, but ALL impls (impls_ is single-valued / last-wins, so two
    // `Trait<A>` impls of one Self — `From<i32>` AND `From<i16>` for `i64`, or
    // `Iterator<i32>` vs the generic `Iterator<&T> for VecIter<T>` — collide).
    // Used by check_type_bounds to verify a parametrized bound `T: Trait<Args>`
    // against the impls' actual trait-args (single-valued impls_ can't, which
    // let `I: Iterator<i32>` be satisfied by an `Iterator<&i32>` impl).
    logos::compiler::StrMap<std::vector<SemaImplInfo>> impls_all_;
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
        writ::AnyVal    value_ast;  // AST expr node for the constant value
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
    // The lifetime params declared on the ENCLOSING impl header
    // (`impl<'a> H<'a> { fn get(self: &H<'a>) ... }`). Set/restored in
    // lower_impl_block; read by compute_fn_lifetime_outlives, whose `known()`
    // scope is "the fn's own <'a> PLUS the impl's". Landed 2026-08-28 — the
    // narrower scope cost 65 legal programs on the borrow-check corpus.
    std::vector<std::string> current_impl_lifetime_params_;
    // G156-1: the impl's CONCRETE trait type-args (`impl Trait<u64> for X` → [u64]),
    // set during collect_impl. Two impls of the same trait NAME for one type at
    // distinct concrete args mangle their methods with these args so they coexist.
    std::vector<TypeRef> current_impl_trait_args_;
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
        // B-mv-03: trait IDENTITIES for `trait_name` / `bound_trait` /
        // `extra_bounds`, captured in collect_impl where the DECLARING scope is
        // live. Empty ⇒ not captured; the query helpers below fall back to the
        // raw spelling, i.e. the pre-B-mv-03 behaviour.
        // ⚠ `bound_trait` itself must stay RAW: it is spelled into the
        // `$blanket$<trait>$<bound>$<target>` registration key at three sites in
        // collect_impl and read back by the assoc-type resolver in sema.cpp and
        // by the completeness check. Canonicalising THAT string would desync the
        // key from its own registration. The identity lives in a sibling field.
        std::string canonical_trait;
        std::string canonical_bound_trait;
        std::vector<std::string> canonical_extra_bounds;
        // The name to ASK the impl registry with.
        const std::string& query_trait() const {
            return canonical_trait.empty() ? trait_name : canonical_trait;
        }
        const std::string& query_bound_trait() const {
            return canonical_bound_trait.empty() ? bound_trait : canonical_bound_trait;
        }
        const std::string& query_extra_bound(size_t i) const {
            return (i < canonical_extra_bounds.size() &&
                    !canonical_extra_bounds[i].empty())
                       ? canonical_extra_bounds[i] : extra_bounds[i];
        }
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
    // B-mv-03: does this blanket implement the trait the query names?
    // A query carrying an IDENTITY (`pkg::Trait` — from
    // TraitBound::canonical_trait or BlanketImpl::canonical_bound_trait)
    // matches on identity, so a homonym's blanket cannot answer for the
    // bare-slot trait. A BARE query keeps matching the raw spelling, which is
    // exactly the pre-B-mv-03 union — the ~50 bare-text probes across sema are
    // unchanged by construction, not by hope.
    // ⚠ MATCH ON IDENTITY IN BOTH DIRECTIONS. This used to read
    //     if (q.find("::") != npos) return bi.query_trait() == q;
    //     return bi.trait_name == q;              // bare query → raw match
    // on the reasoning that a BARE query must keep the pre-B-mv-03 union so the
    // ~50 bare-text probes stay unchanged. MEASURED, and that reasoning is
    // wrong in the direction that matters: a query is bare exactly when the
    // bound's trait OWNS the bare slot — which, for every stdlib trait, is
    // always. So the raw arm let any package's homonym blanket answer for the
    // stdlib trait's bound.
    //   package epk (no homonym):  pub fn err_probe<T: Error>(x: T) -> i64
    //   consumer:  pub trait Error {…}  +  impl<T: Copy> Error for T
    //              err_probe(5i64)  ⇒  rc 0, COMPILED.
    //   control (same consumer, local trait deleted) ⇒ rc 1, the correct
    //   refusal "'err_probe': type 'i64' does not implement trait 'Error'".
    // The bare arm was the whole difference.
    // Identity matching does NOT narrow the bare-text probes: a blanket whose
    // trait owns the bare slot has `canonical_trait == trait_name`, so it still
    // answers a bare query. What stops answering is precisely a blanket whose
    // trait was pushed under `pkg::Name` by the B-mv-02 collision — a DIFFERENT
    // trait that was never entitled to answer. An uncaptured canonical falls
    // back to the raw spelling in query_trait(), i.e. the old behaviour, so the
    // permissive path survives only where identity is genuinely unknown.
    // This also retires the `q.find("::")` text-guess — a substring probe used
    // to classify a key is the shape that produced the separator-class bug.
    // ⚠ NORMALISE BOTH SIDES. Either side may arrive as a bare-text probe, a
    // traits_ registry key (BARE for the homonym that owns the bare slot), or
    // an already-qualified identity. Comparing the strings as they arrive made
    // the answer depend on which spelling the caller happened to hold.
    bool blanket_implements(const BlanketImpl& bi, const std::string& q) const {
        return impl_key_trait(bi.query_trait()) == impl_key_trait(q);
    }

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
                // §3.2b: `use pkg from <module>;` restricts a package's TYPES /
                // enums / traits (not just its free fns) to the named module —
                // skip a match imported `from` a different module than the one
                // owning this package. (pkg_module_ids_[pkg] is the package's
                // owning module.) Empty restriction map ⇒ no-op (common case).
                if (auto rit = cur_imports_.pkg_from_module_id.find(pkg);
                    rit != cur_imports_.pkg_from_module_id.end()) {
                    auto mit = pkg_module_ids_.find(pkg);
                    std::string pkg_mod =
                        (mit != pkg_module_ids_.end()) ? mit->second : std::string{};
                    if (pkg_mod != rit->second) continue;
                }
                if constexpr (PubCheck) {
                    // §4: pass the module-linkage info so a `pub(module)` type
                    // accessed from another module gets a "module-private"
                    // diagnostic (pkg_module_ids_[pkg] = the package's owning
                    // module — uniform across infos, incl. traits with no own id).
                    auto mit = pkg_module_ids_.find(pkg);
                    std::string pkg_mod =
                        (mit != pkg_module_ids_.end()) ? mit->second : std::string{};
                    check_pub_access(it->second.is_pub, it->second.package, name,
                                     it->second.is_module_only, pkg_mod);
                }
                return {pkg, &it->second};
            }
        }
        // logos-core 6.6: the bare-key fallback tier was a visibility-
        // check bypass — a non-`pub` item from another package could be
        // resolved through it without going through `check_pub_access`.
        // Apply the same pub-check the package-qualified tier above
        // uses, scoped to the case where the resolved item belongs to
        // a DIFFERENT package than `cur_package_` (own-package bare
        // entries — e.g. primitives, builtins — are always permitted).
        auto it = m.find(std::string(name));
        if (it != m.end()) {
            if constexpr (PubCheck) {
                if (!it->second.package.empty() &&
                    it->second.package != cur_package_) {
                    check_pub_access(it->second.is_pub,
                                     it->second.package, name);
                }
            }
            return {"", &it->second};
        }
        return {"", nullptr};
    }
    std::pair<std::string, SemaStructInfo*> find_struct_by_name(std::string_view name) {
        return lookup_qualified_<true>(structs_, name);
    }
    // Pub-check-FREE struct lookup, for internal representation decisions (e.g.
    // the #[self_describing] flag) that may be consulted from a FOREIGN
    // package's monomorphisation/substitution context. The pub-checking
    // find_struct_by_name would emit a spurious "X is private" diagnostic when
    // such a query lands on a non-pub struct (e.g. ArcInner in logos.mem.sync)
    // — visibility is irrelevant to a layout/repr question. Prefers the
    // package-qualified key; falls back to the bare legacy slot.
    SemaStructInfo* find_struct_repr_(std::string_view pkg, std::string_view name) {
        if (!pkg.empty()) {
            auto it = structs_.find(sema_key(pkg, name));
            if (it != structs_.end()) return &it->second;
        }
        auto it = structs_.find(std::string(name));
        if (it != structs_.end()) return &it->second;
        return nullptr;
    }
    // Same, for `#[zoned]` datatypes — the second registry a struct-kind
    // TypeRef can resolve into. LAYOUT queries take this path too: asking the
    // pub-checking, bare-name lookup makes a foreign package's private type
    // answer "unknown" (→ the {8,8} default) and makes two same-named types
    // from different packages alias onto whichever registered first.
    SemaStructInfo* find_datatype_repr_(std::string_view pkg, std::string_view name) {
        if (!pkg.empty()) {
            auto it = datatypes_.find(sema_key(pkg, name));
            if (it != datatypes_.end()) return &it->second;
        }
        auto it = datatypes_.find(std::string(name));
        if (it != datatypes_.end()) return &it->second;
        return nullptr;
    }
    std::pair<std::string, SemaStructInfo*> find_datatype_by_name(std::string_view name) {
        return lookup_qualified_<true>(datatypes_, name);
    }
    // Pub-check-FREE, pkg-qualified enum lookup — the layout counterpart of
    // find_struct_repr_. Asking the pub-checking bare-name lookup made every
    // foreign package's enum answer "unknown": `Option<Location>` was sized
    // {8,8} instead of {32,8} and `PanicInfo` came out 24 against the object
    // file's 48. Found by the ledger check, not by a program.
    SemaEnumInfo* find_enum_repr_(std::string_view pkg, std::string_view name) {
        if (!pkg.empty()) {
            auto it = enums_.find(sema_key(pkg, name));
            if (it != enums_.end()) return &it->second;
        }
        auto it = enums_.find(std::string(name));
        if (it != enums_.end()) return &it->second;
        return nullptr;
    }
    std::pair<std::string, SemaEnumInfo*> find_enum_by_name(std::string_view name) {
        return lookup_qualified_<true>(enums_, name);
    }
    std::pair<std::string, SemaTraitInfo*> find_trait_by_name(std::string_view name) {
        // NOTE (T1-9): traits stay on the UNCHECKED lookup — many callers
        // are introspective probes (type-param shadow warnings, enum-lit
        // assoc-fn fallbacks) where a privacy diagnostic would be spurious.
        // The REFERENCE site that introduces a foreign trait (collect_impl)
        // applies check_pub_access explicitly.
        return lookup_qualified_<false>(traits_, name);
    }
    // P2-15 object-safety (dyn-compatibility, Rust E0038): a trait used as a
    // trait object (`&dyn`/`*dyn`/`Box<dyn>`) must be object-safe, else a method
    // has no vtable slot → crash on dispatch. Checked when a `dyn Trait` type is
    // resolved; reported once per offending trait (dedup set).
    std::set<std::string> dyn_safety_reported_;
    void check_trait_object_safe(const std::string& trait_name);
    // Scope-aware iterator into traits_: probes `cur_package_::name`, then each
    // imported/re-exported package, then the bare name (legacy slot). Same
    // resolution order as find_trait_by_name / lookup_qualified_, but returns
    // the map iterator so call sites that need the SemaTraitInfo in place
    // (collect_impl validation, method/assoc registration) keep working after
    // the B-mv-02 fix made a user trait that collides with an imported
    // same-name trait register under its package-qualified key only.
    logos::compiler::StrMap<SemaTraitInfo>::iterator
    find_trait_iter_scoped(std::string_view name) {
        if (!cur_package_.empty()) {
            auto it = traits_.find(sema_key(cur_package_, name));
            if (it != traits_.end()) return it;
        }
        for (auto& pkg : effective_import_pkgs()) {
            auto it = traits_.find(sema_key(pkg, name));
            if (it != traits_.end()) return it;
        }
        return traits_.find(std::string(name));
    }
    // Canonical registry key for a trait NAME as resolved in the current scope:
    // the bare name for a trait that uniquely owns the bare slot (no behaviour
    // change vs the legacy registry), or `pkg::Name` for a same-name trait that
    // a B-mv-02 collision pushed under its package-qualified key. Use this
    // wherever a trait name is composed into a registry key (impls_, coherence,
    // assoc) or looked up, so a user trait and a same-named stdlib/prelude trait
    // route to DISTINCT entries. Falls back to the bare name when unresolved
    // (forward refs / not-yet-collected) — callers then behave as before.
    // ── The IMPL-REGISTRY identity of a trait, ALWAYS package-qualified ──
    //
    // ⚠ A REGISTRY KEY IS NOT AN IDENTITY, and conflating them was a live
    // defect. `canonical_trait_name` returns the traits_ KEY, which is the BARE
    // name for whichever homonym owns the bare slot. Using that as the impl-key
    // identity means the incumbent's identity key is `Hash::i64` — the exact
    // string every OTHER homonym's impl is also filed under as its raw
    // bare-text alias. So a bound over the incumbent hit the newcomer's alias,
    // and vice versa.
    // MEASURED: an archive declaring `trait Hash` + `pub fn ord2_tag<T: Hash>`,
    // consumer calls `ord2_tag(5i64)` ⇒ admitted, then
    //   error: 'func.call' op 'i64__tag' does not reference a valid function
    // while the control (archive trait renamed `MyHash`) refuses cleanly, rc 1.
    // The identity below is `pkg::Name` for EVERY trait that resolves, so it
    // can never equal another trait's bare alias, and the raw key keeps serving
    // the ~50 bare-text probes untouched.
    // Composed structurally from the trait's own `package` + `name`, NOT by
    // probing the key for a `::` substring — a substring test over a key space
    // is the shape that produced the separator-class bug.
    std::string impl_key_trait(std::string_view regkey) const {
        auto it = traits_.find(std::string(regkey));
        if (it == traits_.end()) return std::string(regkey);
        const auto& ti = it->second;
        if (ti.package.empty() || ti.name.empty()) return std::string(regkey);
        return sema_key(ti.package, ti.name);
    }
    std::string canonical_trait_name(std::string_view name) {
        auto it = find_trait_iter_scoped(name);
        if (it != traits_.end()) return it->first;
        return std::string(name);
    }
    // ── Trait identity for the mono-time trait QUERY intrinsics ──────────
    // `has_trait::<T, Tr>()` and `has_trait_of::<Tr>(t)` carry the trait to
    // mono as a STRING argument. That string is now the trait's IDENTITY — the
    // traits_ registry key — not the spelling written at the query site, and
    // mono's fact table is keyed by the same identity (impl_keys::
    // CANONICAL_TRAIT → Mono::concrete_impls_, a PAIR). So two packages that
    // each declare `trait Hash` are two traits with two fact sets, and a query
    // naming one cannot read the other's impls.
    // What is still refused, and why:
    //   · a name that denotes NO trait anywhere — it would otherwise be
    //     answered `false`, or even `true` when some unrelated impl happens to
    //     be keyed under that spelling;
    //   · a name that denotes SEVERAL traits and does not resolve to any of
    //     them in this scope (neither local, nor imported, nor the bare slot).
    // ⚠ THE "RESIDUAL" THAT USED TO BE CLAIMED HERE WAS RE-MEASURED AND IS
    // GONE. It read: mono also inserts each fact under the impl's BARE
    // spelling as an alias, so a query naming the bare-slot trait still sees a
    // homonym's impls. That alias was retired in Mono::Mono after
    // tests/logos/pass/trait_ident_pkg_chain.logos (three packages, two binary
    // archives) measured the mirror direction ANSWERING CORRECTLY with the
    // alias still in place. What the same probe DID reproduce is on the BOUND
    // side and does not pass through this function: `impls_` is keyed by the
    // RAW trait spelling in SemaChecker::collect_impl, so one key space holds
    // two homonym traits and a generic bound admits the wrong trait's
    // concretes. See that fixture's header and the comment at the retired
    // alias in mono.cpp.
    // ⚠ NOT an import check. Resolution falls back to the bare slot exactly
    // as find_trait_iter_scoped does, so a module that does NOT import the
    // trait's package keeps answering — capability is a property of the type,
    // not of the asker's import view (tests/logos/pass/
    // meta_trait_query_without_import.logos and ..._emit_import_view.logos
    // pin that, and both must stay green).
    // Every traits_ key spelling `name` bare or as `pkg::name`.
    std::vector<std::string> trait_keys_spelling(std::string_view name) const {
        std::vector<std::string> out;
        if (name.empty()) return out;
        const std::string suffix = "::" + std::string(name);
        for (auto& kv : traits_) {
            std::string_view k{kv.first};
            if (k == name ||
                (k.size() > suffix.size() && k.substr(k.size() - suffix.size()) == suffix))
                out.push_back(kv.first);
        }
        // traits_ is a hash map: sort so the DIAGNOSTIC is byte-stable across
        // runs and a fail fixture can pin the trait list it names.
        std::sort(out.begin(), out.end());
        return out;
    }
    // Resolve the trait NAME written at a query site to the trait IDENTITY that
    // mono's fact table is keyed by, or refuse loudly. `intrinsic` is spelled
    // into the message. Returns nullopt iff a diagnostic was emitted.
    std::optional<std::string>
    resolve_trait_query_name(std::string_view intrinsic, std::string_view name) {
        auto keys = trait_keys_spelling(name);
        if (keys.size() == 1) {
            // Unique spelling: use THAT key even when scoped resolution would
            // miss it (a trait living only under `pkg::Name` whose package the
            // asker does not import). Import-invariant by construction.
            return keys[0];
        }
        if (!keys.empty()) {
            // Several traits share the bare spelling. Resolve exactly as any
            // other trait name in this scope does — local package, then
            // imports, then the bare slot.
            std::string canon = canonical_trait_name(name);
            if (std::find(keys.begin(), keys.end(), canon) != keys.end())
                return canon;
        }
        if (keys.empty()) {
            std::string where = cur_package_.empty()
                ? std::string("the global trait registry")
                : std::format("package '{}'", cur_package_);
            std::string imps;
            for (auto& p : effective_import_pkgs()) {
                if (!imps.empty()) imps += ", ";
                imps += p;
            }
            error(std::format(
                "{}: '{}' does not name a trait; looked in {}, imported packages [{}], "
                "and the global trait registry",
                intrinsic, name, where, imps));
            return std::nullopt;
        }
        // keys.size() > 1 and none of them is what the name resolves to here:
        // the query names several traits and no scope rule picks one.
        //
        // ⚠ BELIEVED UNREACHABLE, AND THAT BELIEF IS ARGUED, NOT MEASURED — which
        // is why the branch stays. The argument: `collect_trait` (sema_collect.cpp,
        // the B-mv-02 block) registers a `pkg::Name` key ONLY when a bare incumbent
        // already holds the slot, so whenever `trait_keys_spelling` returns >1 the
        // bare key is among them; and `canonical_trait_name`'s last step is
        // `traits_.find(name)` on exactly that bare key, which therefore succeeds.
        // No input was constructed that reaches here, and nothing asserts the
        // message below.
        // ⚠ A green corpus is the ABSENCE OF A WITNESS, not a proof — this arc
        // already shipped one false unreachability claim ("there is no char fixture
        // that reds this line"; there was). So: do NOT delete this on the strength
        // of a green run. If it ever fires, the invariant above has changed —
        // some path now registers a `pkg::Name` trait with no bare incumbent —
        // and THAT is the thing to go and read, not this diagnostic.
        std::string list;
        for (auto& k : keys) {
            if (!list.empty()) list += ", ";
            auto it = traits_.find(k);
            list += (it != traits_.end() && !it->second.package.empty() &&
                     k.find("::") == std::string::npos)
                        ? it->second.package + "::" + std::string(name)
                        : k;
        }
        error(std::format(
            "{}: trait name '{}' is ambiguous — it names {} distinct traits ({}) "
            "and resolves to none of them in this scope",
            intrinsic, name, keys.size(), list));
        return std::nullopt;
    }
    bool has_struct(std::string_view name)   { return find_struct_by_name(name).second   != nullptr; }
    bool has_datatype(std::string_view name) { return find_datatype_by_name(name).second != nullptr; }
    bool has_enum(std::string_view name)     { return find_enum_by_name(name).second     != nullptr; }

    // ── Type parameter helpers ────────────────────────────────────

    // B64: compute per-struct/enum variance via fixed-point over field types.
    // Populates variance_table_ keyed by "pkg.Name" with VarianceMap entries
    // using `#i` (type param i) and `@i` (lifetime param i) as keys.
    void compute_variances();

    std::vector<std::string> read_lifetime_params(writ::TinyMapView node);
    // B65: read declared outlives bounds. Returns (long, short) pairs from
    // `'long: 'short` clauses (in fn/struct/enum/impl header or where).
    // Scans node.TYPE_PARAMS items (LIFETIME_PARAM with non-empty ITEMS).
    std::vector<std::pair<std::string, std::string>>
        read_lifetime_outlives(writ::TinyMapView node);
    std::vector<std::pair<std::string, std::string>>
        read_lifetime_outlives_from(writ::TinyMapView node, int32_t field_code);
    std::vector<TypeParam> read_type_params_from(writ::TinyMapView node, int32_t field_code);
    std::vector<TypeParam> read_type_params(writ::TinyMapView node);
    // Read type_args + assoc_eqs from a TRAIT_BOUND node's TYPE_PARAMS slot.
    // ASSOC_EQ_BIND items go to assoc_eqs; everything else is resolved as a type.
    void read_trait_bound_args(writ::TinyMapView bnode, TraitBound& tb);
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
            if (arg_exprs[i]) walk(callee_param_types[i], expr_type(arg_exprs[i]));
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
            if (fdecl) walk(fdecl, expr_type(fval));
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
        // These two `permissive` flags are not two mechanisms: they are two
        // doors onto ONE line in include/logos/compiler/outlives.hpp, and a
        // careful round belongs at that line, not here. The adjudication and
        // every number are in src/compiler/PROBES.md (measured 2026-08-27 on
        // a 423-row ledger, RE-MEASURED 2026-08-31 on the 310-row one).
        // PROBE lifereg_callargstrict: every call-argument site passes the
        // default permissive=true, whose tail declares two named lifetimes
        // that appear in NO outlives clause compatible.
        if (logos::probe::on("lifereg_callargstrict") &&
            !ctx.starts_with("struct literal"))
            permissive = false;
        // PROBE lifereg_structlitstrict: the struct-literal field-init sites
        // pass permissive=true EXPLICITLY, for a stated and DIFFERENT reason
        // (struct-scope binding), so it is priced separately.
        if (logos::probe::on("lifereg_structlitstrict") &&
            ctx.starts_with("struct literal"))
            permissive = false;
        if (!types_compatible(from, to)) return;  // outer check handles it
        if (variance_ok(from, to, permissive)) return;
        auto [es, gs] = type_str_pair(to, from);
        error(std::format("{}: variance mismatch — expected {}, got {} — "
                          "lifetime structure incompatible "
                          "(check &mut invariance / contravariance rules)",
                          ctx, es, gs));
    }

    // ── Type resolution ──────────────────────────────────────────

    TypeRef resolve_type(writ::TinyMapView node);
    // Hash a bare writ_lit AST (WRIT_MAP / _ARRAY / scalars) and register
    // its lowered LIR WritVal in cur_prog_->wstatic_registry_; return the
    // corresponding WStaticLit TypeRef. Shared between the LIT_WSTATIC type-
    // arg handler in resolve_type and `pub const X: WritStatic = @{...};`
    // recognition in collect_const.
    TypeRef resolve_wstatic_value(writ::TinyMapView val_node);
    // ADR 0021 C1 — canonical CFG value-column representation. A value column
    // whose type cannot ride `<type:T>` (an unsized / VLE column — `str`, the
    // Slice<u8> shape, today) is represented in the config document as a bare
    // string TAG ("str"), identically on BOTH arrival paths: the concrete-decl
    // path (emit_cfg_doc) emits the literal tag, and a generic `<type:V>`
    // substituted at V=str must canonicalize to the SAME tag. Without this, a
    // generic `Map<u64,str>` (WRIT_TYPE_LIT → `<type:&[u8]>`) and a concrete
    // str-map (WRIT_STR `"str"`) hash to DIFFERENT families — an identity
    // split. Returns the tag text WITHOUT quotes ("str"), or "" when T is
    // sized and rides `<type:T>`. ONE definition, consumed by the identity
    // hash (resolve_wstatic_value) AND the source render (render_writ_val_inner_
    // / render_expr_src), so a value type has ONE CFG representation.
    std::string cfg_str_tag_(TypeRef t) const;
    // ADR 0021: the metaclass wrapper type over a config document —
    // `CtrClass<@hs(CFG)>`. Canonical construction site (pkg threaded).
    TypeRef make_metaclass_wrapper(TypeRef cfg) {
        LogosTypeBuilder wt;
        wt.kind = LogosType::Kind::Struct;
        wt.struct_name = "CtrClass";
        wt.pkg_name = "logos.lcm.canon.metaclass";
        wt.type_args.push_back(cfg);
        return pool_->alloc(std::move(wt));
    }
    // Generic compile-time const under CONCRETE args (ADR 0021: shared by the
    // type-position use site and the typeof(<container-decl>) bridge).
    TypeRef resolve_generic_wstatic_const(const std::string& name,
                                          const std::vector<TypeRef>& args);

    // ── Collection phase ─────────────────────────────────────────

    void collect(const std::vector<writ::Writ>& asts);
    void simplify_all_types();
    void check_supertrait_impls();
    // Supertrait-closure vtable layout — single source of truth for dyn-Trait
    // slot ordering (see LTraitDef.vtable_method_order / upcast_supertraits).
    void trait_vtable_layout(
        const std::string& trait,
        std::vector<std::pair<std::string, const SemaTraitMethodInfo*>>& method_order,
        std::vector<std::string>& upcast_supers);
    std::string read_package_name(writ::TinyMapView mod);
    void check_pub_access(bool is_pub, const std::string& def_package,
                          std::string_view item_name,
                          bool is_module_only = false,
                          const std::string& def_module_id = {});
    void check_type_bounds(const std::string& target_name,
                           const std::vector<TypeParam>& type_params,
                           const std::vector<TypeRef>& args);
    // ── ADR 0021 Phase 4a: factory-backed marker deferral ────────
    // `CtrClass<WStaticLit>` from logos.lcm.canon.metaclass is the
    // (wave-0, only) factory-backed metaclass marker: its CtrFamily impl is
    // GENERATED by the driver's mono→factory drain, which runs after this
    // sema round. A trait-bound failure or an unresolved assoc-type
    // projection against such a marker is therefore deferred, not diagnosed:
    // the deferral records a REQUIRED factory demand on cur_prog_ and the
    // post-drain re-sema re-checks strictly (the impl exists by then; a
    // still-deferring round is escalated to a hard error by the driver).
    // Returns the CFG doc's content hash when `t` is such a marker.
    std::optional<uint64_t> factory_backed_marker_hash(TypeRef t) const;
    // Record the required demand (dedup by hash); false when `t` is not a
    // factory-backed marker or no program is being lowered (stay strict).
    bool defer_factory_backed(TypeRef t);
    // True when the type AST subtree contains a GENERIC_INST of a registered
    // generic const (probed lazily at alias USE time — generic_consts_ is
    // fully populated by then, unlike at alias-collect time).
    bool type_ast_mentions_generic_const(writ::TinyMapView n);
    // Quiet-probe mode for check_type_bounds: bound-based partial-spec
    // selection (a `struct S<T: Bounds>` spec matches only when the concrete
    // arg SATISFIES the bounds) must test satisfaction without emitting
    // diagnostics. The probe flag turns every failure site in
    // check_type_bounds into `bounds_probe_ok_ = false` instead of error().
    bool bounds_probe_    = false;
    bool bounds_probe_ok_ = true;
    bool type_bounds_satisfied_quiet(const std::string& target_name,
                                     const std::vector<TypeParam>& type_params,
                                     const std::vector<TypeRef>& args) {
        bool was = bounds_probe_, wok = bounds_probe_ok_;
        bounds_probe_ = true; bounds_probe_ok_ = true;
        check_type_bounds(target_name, type_params, args);
        bool ok = bounds_probe_ok_;
        bounds_probe_ = was; bounds_probe_ok_ = wok;
        return ok;
    }

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
    void collect_module(writ::TinyMapView mod, int phase);
    // ADR 0021: container-decl registration split from collect phase 2 —
    // also runs for cache-skipped binary holders (containers_ is not cached).
    void register_container_decls(writ::TinyMapView mod);
    void collect_enum(writ::TinyMapView node);
    void collect_type_alias(writ::TinyMapView node);
    void collect_const(writ::TinyMapView node);
    void collect_trait(writ::TinyMapView node);
    void collect_impl(writ::TinyMapView node);
    void collect_struct_spec(writ::TinyMapView node,
                             const StructAttrFlags* attr_flags = nullptr);
    void collect_struct(writ::TinyMapView node);
    void collect_schema(writ::TinyMapView node);        // ADR 0011: `schema S {…}`
    void collect_schema_enum(writ::TinyMapView node);   // ADR 0011: `schema enum E {…}`
    void collect_datatype(writ::TinyMapView node, bool is_annotation_type = false);
    TypeRef try_resolve_as_known_type(std::string_view name);
    bool is_known_type_name(std::string_view name) const;
    void extract_typevars_from_type_node(writ::TinyMapView node,
                                         std::vector<TypeParam>& out);
    bool is_specialization_fn(writ::TinyMapView node);
    bool is_specialization_struct(writ::TinyMapView node);
    DeclBuilder lower_spec_struct(writ::TinyMapView node);
    DeclBuilder lower_spec_fn(writ::TinyMapView node);
    void collect_fn(writ::TinyMapView node, std::string_view struct_ctx = {},
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
    // Per-active-loop break-value attribution frame. A `break 'label v`
    // attributes its value-type to the frame matching the label (an unlabeled
    // break targets the innermost frame), so a value breaking to an OUTER
    // labeled loop is no longer stolen by an inner `loop`. One frame is pushed
    // per active loop of any kind (for / while / loop). Only `loop` reads its
    // frame's value_type to become a value-yielding expression.
    struct LoopBreakFrame {
        std::string label;
        TypeRef value_type = nullptr;
        bool without_value = false;
    };
    std::vector<LoopBreakFrame> loop_break_frames_;
    std::string pending_loop_label_;  // set by LABELED_LOOP before lowering inner loop
    // Set by `lower_loop` to communicate "no `break` reached this loop's
    // frame" back to the LOOP-as-expression caller. `loop { /* no break */ }`
    // is a diverging expression — its type is `!`, not `()`. Reset on every
    // call to `lower_loop` so a sibling loop with no diverging shape doesn't
    // poison the next one. (See logos-core item 1.1.)
    bool last_loop_diverged_ = false;
    bool match_in_tail_position_ = false;
    // B-fn-06: when true, TAIL_EXPR statements act as implicit returns.
    // Set around fn-body lowering; cleared inside block-as-expression
    // contexts (match-arm-body, unsafe-block-as-expr, if-as-expr).
    bool tail_as_return_ = false;
    TypeRef impl_ret_type_inferred_ = nullptr;
    TypeRef hint_enum_type_ = nullptr;
    TypeRef hint_struct_type_ = nullptr;
    // Expected TUPLE type for a tuple-literal in value position (a `(i64,i64)`
    // param/let). Without it, untyped int-literal elements default to i32 — so
    // `f((7, 2))` against a `(i64, i64)` param built an `{i32,i32}` buffer the
    // callee then read as `{i64,i64}` (silent garbage). TUPLE_LIT lowering widens
    // each element to the matching expected element type.
    TypeRef hint_tuple_type_ = nullptr;
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
    // g6b: expected ELEMENT type for an array/slice literal, from a `let
    // arr: [&dyn Trait; N] = [...]` annotation (or analogous context). Lets
    // lower_arr_lit type a HETEROGENEOUS `[&Sq, &Ci]` as `[&dyn Trait; N]` —
    // coercing each `&Concrete` to `&dyn Trait` (the unsize coercion done
    // per-element at codegen) instead of rejecting on element-type mismatch.
    TypeRef hint_arr_elem_type_ = nullptr;
    // The EXPECTED type of the expression currently being lowered, when the
    // surrounding position knows it (an annotated `let`, a `return`). Branch
    // merges need it: `if`/`match` arms are unified against EACH OTHER, so
    // arms that are mutually incompatible but both coercible to the expected
    // type — `let x: &[T] = if c { &a3 } else { &a5 }` — had nothing to be
    // coerced TO and simply errored.
    TypeRef hint_expected_type_ = nullptr;

    // T2-28: when a call is written with an explicit package qualifier
    // (`logos.lang.mem::replace(...)`), this holds the dotted package
    // ("logos.lang.mem") for the duration of that call's resolution. The
    // free-fn candidate lookups (find_func_candidates / find_generic_func*)
    // then accept ONLY functions whose `.package` matches exactly — this is
    // what disambiguates same-named free fns across packages (mem::replace
    // vs ptr::replace). Empty = unqualified call (normal import-based scope).
    std::string call_pkg_qualifier_;
    bool pkg_qualifier_ok(const SemaFuncInfo& fi) const {
        return call_pkg_qualifier_.empty() ||
               fi.package == call_pkg_qualifier_;
    }
    // Reconstruct the dotted package from a qualified-call node's
    // RECEIVER (first segment) + PATH_PARTS (the rest). "" if not qualified.
    std::string extract_pkg_qualifier(writ::TinyMapView node);

    // ── Return reachability ───────────────────────────────────────

    bool stmt_always_returns(writ::TinyMapView stmt);
    bool block_always_returns(writ::TinyMapView block);
    // Like *_always_returns, but also treats `break`/`continue` as diverging.
    // Used where we need to know "does the tail expression run?" — e.g. match
    // arm body: `{ ...; break; }` never reaches a tail expression.
    bool stmt_always_diverts(writ::TinyMapView stmt);
    bool block_always_diverts(writ::TinyMapView block);
    // logos-core 1.1: callee-body always diverges (panic-tail / loop{}-tail).
    // STRICTER than *_returns — RETURN normal-paths return false. Used by
    // `infer_type_args` to fall back unbound type-params to `!`
    // (Rust-2024 `!`-fallback rule, narrowed to: var unbound AND callee
    // body always diverges).
    bool body_always_diverges_simple(writ::TinyMapView body_node);
    // "Does a `break` TARGET this loop?" — the single divergence question for
    // a LOOP / LABELED_LOOP node. Label- and nesting-aware, does not descend
    // into closures or nested fns. Every AST-phase divergence gate delegates
    // here; the lowering-phase answer is last_loop_diverged_ (lower_loop) and
    // the two must agree.
    bool loop_has_targeting_break(writ::TinyMapView loop_node);
    // "Is this node an infinite `loop { ... }`?" — the CODE test the three
    // divergence sites need. LOOP, or a LABELED_LOOP that wraps one. A
    // LABELED_LOOP over a `while`/`for` is NOT one: those exit on their own
    // condition, and loop_has_targeting_break answers targeting for them too
    // (it accepts WHILE/FOR/FOR_EACH as `owner`), so widening a divergence
    // site to the LABELED_LOOP CODE alone would read `'a: while c { }` as
    // diverging. Unknown BODY codes answer FALSE — a divergence gate must
    // default to the refusing side.
    bool is_infinite_loop_node(writ::TinyMapView n);

    // ── Lowering helpers ─────────────────────────────────────────

    static bool is_numeric(TypeRef t) noexcept {
        if (!t) return false;
        auto k = TypeRef(t).kind();
        return k == LogosType::Kind::F64 ||
               k == LogosType::Kind::F32 ||
               k == LogosType::Kind::FloatLit ||
               k == LogosType::Kind::TypeVar ||
               // Cfg-slot types are deferred until mono resolves them
               // through the bound WritStatic. Accept here on the trust
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
    // Generic-overload selection by ARGUMENT SHAPE. find_generic_func picks
    // the first arity-matching overload — wrong as soon as one base name
    // carries several generic impls distinguished by their param types
    // (Pin<&T>::new vs Pin<&mut T>::new vs Pin<Box<T>>::new). Scores each
    // candidate by unifying its params against the actuals and checking the
    // SUBSTITUTED params: exact (+2/param) beats coercion-compatible (+1);
    // any incompatible param rejects. Returns nullptr when no candidate
    // matches (or <2 overloads exist) — callers keep their first-wins
    // fallback, so single-overload behavior is untouched. (sema_expr.cpp)
    const SemaFuncInfo* find_generic_func_for_args(
            std::string_view base_name,
            const std::vector<TypeRef>& arg_types,
            bool is_method_recv);
    // #[self_describing] thin-one-repr leniency: every reference form over
    // such a struct (raw ptr, &/&mut, DstRef-canonicalised) is a single thin
    // representation, so the forms interconvert when the struct matches —
    // EXCEPT that a mutable expected form still demands a mutable actual
    // (const→mut stays an error). The SHARED predicate for arg checks and
    // method-receiver matching; `a` = actual, `b` = expected.
    bool sd_thin_compatible(TypeRef a, TypeRef b) {
        // {struct name, is-mutable form}; empty name = not a thin ref form.
        auto dst_form = [&](TypeRef t) -> std::pair<std::string, bool> {
            if (!t) return {};
            TypeRef u = t;
            bool mut_form = false;
            auto k = u.kind();
            if ((k == LogosType::Kind::Ptr ||
                 k == LogosType::Kind::Ref ||
                 k == LogosType::Kind::MutRef) && u.pointee()) {
                mut_form = (k == LogosType::Kind::MutRef) ||
                           (k == LogosType::Kind::Ptr && u.mut_ptr());
                u = u.pointee();
            }
            auto uk = TypeRef(u).kind();
            if (uk == LogosType::Kind::DstRef)
                return {std::string(TypeRef(u).struct_name()),
                        mut_form || TypeRef(u).mut_ptr()};
            if (uk == LogosType::Kind::Struct ||
                uk == LogosType::Kind::ZonedStruct)
                return {std::string(TypeRef(u).struct_name()), mut_form};
            return {};
        };
        auto [an, amut] = dst_form(a);
        auto [bn, bmut] = dst_form(b);
        if (an.empty() || an != bn) return false;
        if (bmut && !amut) return false;  // expected mutable, actual const
        auto [dp, dsi] = find_struct_by_name(an);
        (void)dp;
        return dsi && dsi->self_describing;
    }

    const SemaFuncInfo* find_func_by_base_and_signature(std::string_view base_name,
                                                        const std::vector<TypeRef>& param_types,
                                                        bool is_vararg = false) const;
    std::vector<const SemaFuncInfo*> find_func_candidates(std::string_view base_name) const;

    // ── A BUILTIN NAME IS NOT AN IDENTITY ────────────────────────────────
    // The intrinsic intercepts in `lower_call`'s early builtin section and in
    // `lower_type_intrinsic` are a SCAN KEYED BY THE BARE CALLEE SPELLING:
    // `callee == "popcount_u64"` answers for whoever writes that name, so a
    // user package declaring `fn popcount_u64` never reaches
    // resolve_function_call — its body is emitted, never called, and the call
    // site silently gets llvm.ctpop (measured: the user's `return 12345` lost,
    // no diagnostic, rc 0). The intrinsics that DO have a stdlib declaration
    // (`str_from_raw` in logos.lang.str, whose body is dead by design) own the
    // bare slot legitimately; the ones that have no declaration anywhere
    // (`popcount_u64`, `pdep_u64`, the reflection family) own it by default.
    // Either way the owner is the stdlib, and a declaration from a NON-stdlib
    // package is a DIFFERENT identity that must win.
    //
    // ⚠ An `extern fn` does NOT shadow. `extern fn logos_atomic_fetch_add64`
    // (bench/mpsc_stress.logos, and the same spelling inside
    // stdlib/lang/atomic) DECLARES the very runtime symbol the intrinsic
    // lowers — it names the same entity, it does not introduce a second one.
    // Only a definition with a body is a rival identity.
    //
    // All 192 stdlib packages are `logos.*` (checked by listing
    // `^package` over stdlib/, 2026-08-21) and no non-stdlib .logos in the
    // tree declares one, so the prefix test IS the ownership test.
    bool builtin_name_shadowed(std::string_view callee) const {
        for (const SemaFuncInfo* fi : find_func_candidates(callee)) {
            if (!fi) continue;
            if (fi->is_extern) continue;
            const std::string& p = fi->package;
            if (p.empty()) continue;
            if (p == "logos" || p.starts_with("logos.")) continue;
            return true;
        }
        return false;
    }

    // Direct call / macro-call to a `-> !` (Never-returning) function — used
    // to decide whether a syntactic position diverges. Generalises the
    // historical hand-coded `callee == "panic"` carve-outs once the Never
    // type became real (logos-core 1.1). `panic`/`abort`/`exit`/user
    // `fn foo() -> !` all qualify.
    bool is_divergent_call_node(writ::TinyMapView node);
    const SemaFuncInfo* resolve_function_call(std::string_view base_name,
                                              const std::vector<lir::LExprPtr>& arg_exprs,
                                              bool allow_generic = true,
                                              bool exact_only = false) const;

    // ── lower_expr ───────────────────────────────────────────────

    lir::LExprPtr lower_expr(writ::TinyMapView expr);
    lir::LExprPtr lower_expr_inner(writ::TinyMapView expr);
    lir::LExprPtr lower_offset_of(writ::TinyMapView node);  // offset_of!(Type, field)
    lir::LExprPtr lower_binop(writ::TinyMapView node);
    lir::LExprPtr lower_unary(writ::TinyMapView node);
    lir::LExprPtr lower_deref(writ::TinyMapView node);
    // Box DerefMove: lowers `*box_var` (move-typed Box) to `box_take(b)`.
    // Returns null when not applicable (operand isn't a bare Box var, or its
    // element is Copy) — caller falls through to the normal deref path.
    lir::LExprPtr try_lower_box_deref_move(writ::TinyMapView deref_node);
    lir::LExprPtr lower_call(writ::TinyMapView node);
    // lower_expr literal/cast sub-handlers, factored out of its switch so every
    // case delegates uniformly. Each lowers one expr kind from `expr` + members.
    lir::LExprPtr lower_int_lit(writ::TinyMapView expr);
    lir::LExprPtr lit_int_from_text(std::string_view sv, bool negate);
    lir::LExprPtr lower_char_lit(writ::TinyMapView expr);
    lir::LExprPtr lower_bytes_lit(writ::TinyMapView expr);
    lir::LExprPtr lower_var_ref(writ::TinyMapView expr);
    lir::LExprPtr lower_cast(writ::TinyMapView expr);
    // resolve_type sub-handlers, factored out of the resolve_type tc-dispatch.
    // Each lowers one type-syntax kind (keyed by node's CODE) to a TypeRef and
    // depends only on `node` + members (no state shared across branches).
    TypeRef resolve_type_generic_inst(writ::TinyMapView node);
    TypeRef resolve_type_assoc_ref(writ::TinyMapView node);
    TypeRef resolve_type_cfg_slot(writ::TinyMapView node);
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
    lir::LExprPtr lower_generic_call(writ::TinyMapView node);
    // The leading magic-builtin / type-trait intrinsic dispatch of
    // lower_generic_call (is_same, type_of, has_trait, typelist_*, tuple_*, …).
    // Returns the lowered call when `callee` is such an intrinsic; nullopt to
    // fall through to lower_generic_call's general generic-resolution path.
    std::optional<lir::LExprPtr> lower_type_intrinsic(
        writ::TinyMapView node, std::string_view callee);
    // Helpers shared by the type-intrinsic handlers (promoted from lambdas).
    std::vector<TypeRef> collect_type_args(writ::TinyMapView node);
    lir::LExprPtr bool_lit(bool v);
    // Individual large intrinsic handlers split out of lower_type_intrinsic's
    // flat callee-dispatch (each terminal; uses node + members only).
    lir::LExprPtr lower_intrinsic_tuple_all_eq(writ::TinyMapView node);
    lir::LExprPtr lower_intrinsic_type_code_of(writ::TinyMapView node);
    lir::LExprPtr lower_intrinsic_generic_of(writ::TinyMapView node);
    lir::LExprPtr lower_intrinsic_template_of(writ::TinyMapView node);
    lir::LExprPtr lower_intrinsic_has_trait_of(writ::TinyMapView node);
    lir::LExprPtr lower_intrinsic_dst_from_raw_parts(writ::TinyMapView node, std::string_view callee);
    lir::LExprPtr lower_intrinsic_zone_mut_ref(writ::TinyMapView node);
    lir::LExprPtr lower_intrinsic_reflect(writ::TinyMapView node);
    lir::LExprPtr lower_intrinsic_get_annotation(writ::TinyMapView node);
    lir::LExprPtr lower_generic_ref(writ::TinyMapView node);
    lir::LExprPtr lower_method_call(writ::TinyMapView node);
    // Per-receiver-shape method-dispatch handlers, factored out of the
    // lower_method_call cascade. Each checks its own receiver-type guard and
    // returns nullopt to fall through to the next shape; `recv` is threaded by
    // reference so a handler's coercions persist across fall-through exactly as
    // in the original inline code.
    std::optional<lir::LExprPtr> try_method_on_tuple(
        writ::TinyMapView node, lir::LExprPtr& recv, std::string_view method_name);
    std::optional<lir::LExprPtr> try_method_on_slice(
        writ::TinyMapView node, lir::LExprPtr& recv, std::string_view method_name);
    std::optional<lir::LExprPtr> try_method_on_dstref(
        writ::TinyMapView node, lir::LExprPtr& recv, std::string_view method_name);
    std::optional<lir::LExprPtr> try_method_on_dyn(
        writ::TinyMapView node, lir::LExprPtr& recv, std::string_view method_name);
    std::optional<lir::LExprPtr> try_method_on_raw_ptr(
        writ::TinyMapView node, lir::LExprPtr& recv, std::string_view method_name);
    std::optional<lir::LExprPtr> try_method_on_tagged(
        writ::TinyMapView node, lir::LExprPtr& recv, std::string_view method_name);
    // Move-tracking shared by the method-dispatch handlers: mark by-value
    // move-type args / receiver as moved so scope-end auto-Drop doesn't fire
    // on ownership the call has transferred. (Promoted from local lambdas.)
    // `formals` / `formal_off` let this see the PARAMETER a by-value `&mut`
    // argument is bound to; without them a move and a reborrow are the same
    // expression (see the body).
    void track_args_moved(const std::vector<lir::LExprPtr>& args,
                          const std::vector<TypeRef>* formals = nullptr,
                          size_t formal_off = 0);
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
    lir::LExprPtr lower_invoke_expr(writ::TinyMapView node);
    lir::LExprPtr lower_field_read(writ::TinyMapView node);
    // ADR 0011 — convert a WAny (from a schema `get`) to the field's declared
    // type via the matching WAny accessor (as_bool/as_i64/as_u64/as_f64/resolve).
    lir::LExprPtr schema_wany_to_typed(lir::LExprPtr anyval, TypeRef ftype,
                                       std::string_view sname, std::string_view field);
    // ADR 0011 — the stdlib type-name a field type uses in its `WritField` impl
    // (`bool`/`i64`/`str`/…), for resolving `<name>__from_wany`/`__to_wany`. Empty
    // if the type isn't a scalar/str WritField (pointers/WAny handled separately).
    std::string writfield_type_name(TypeRef t);
    // ADR 0011 — the schema_type_code for a (possibly generic) instantiation.
    // For a generic instance (`Wrap<i64>`) it folds the concrete type-args into the
    // variant bits → DISTINCT per-instantiation codes; for a non-generic schema it
    // returns base_code unchanged. SHARED by make/view_checked AND schema-enum match
    // so all three sites compute the SAME code (must not drift).
    uint64_t schema_instance_code(TypeRef inst_type, uint64_t base_code,
                                  std::string_view pkg);
    // ADR 0011 — `wr.make::<S>()` (construct), `x.view::<S>()` / `x.child::<S>()`
    // (trusted bind). Returns nullopt unless method+type-arg name a schema.
    std::optional<lir::LExprPtr>
    try_schema_method(lir::LExprPtr& recv, std::string_view method_name,
                      const std::vector<TypeRef>& type_args);
    lir::LExprPtr lower_struct_lit(writ::TinyMapView node);
    lir::LExprPtr lower_index_read(writ::TinyMapView node);
    // `&f[i]` / `&mut f[i]` over a user Index/IndexMut struct: dispatch to
    // index()/index_mut() and return the resulting place reference DIRECTLY
    // (no deref, no temp). Returns nullptr if `node` is not an INDEX_READ
    // over a user-Index type, so the caller falls through to its generic
    // address-of path. is_mut selects index_mut (requires IndexMut).
    lir::LExprPtr lower_index_place(writ::TinyMapView node, bool is_mut);
    lir::LExprPtr lower_arr_lit(writ::TinyMapView node);
    lir::LExprPtr lower_arr_fill_lit(writ::TinyMapView node);
    lir::LExprPtr lower_list_comp(writ::TinyMapView node);
    lir::LExprPtr lower_map_comp(writ::TinyMapView node);
    lir::LExprPtr lower_writ_list_comp(writ::TinyMapView node);
    lir::LExprPtr lower_writ_map_comp(writ::TinyMapView node);
    lir::LExprPtr coerce_to_writ_anyval(lir::LExprPtr val,
                                          const std::string& ctr_var,
                                          TypeRef ctr_t,
                                          std::string_view context);
    lir::LExprPtr lower_writ_lit(writ::TinyMapView node);
    lir::LExprPtr lower_writ_blob(writ::TinyMapView node);
    lir::LExprPtr lower_quote_item(writ::TinyMapView node);
    lir::LExprPtr lower_quote_expr(writ::TinyMapView node);
    lir::LExprPtr lower_quote_ty(writ::TinyMapView node);

    // Capture context: non-null while lowering a writ literal that has $-captures.
    // lower_writ_val populates it as it encounters WRIT_CAP_IDENT/EXPR nodes.
    struct WritCapCtx {
        std::vector<lir::LExprPtr>               exprs;       // unique capture expressions
        std::vector<TypeRef>             types;       // corresponding types
        uint32_t                                  next_slot = 0; // next param_index
        // dedup: symbol binding name → value_index (for pure EIdent captures)
        logos::compiler::StrMap<uint32_t> ident_dedup;
    };
    WritCapCtx* writ_cap_ctx_ = nullptr;

    lir::WritValPtr lower_writ_val(writ::TinyMapView node);
    lir::LExprPtr lower_enum_lit(writ::TinyMapView node);
    lir::LExprPtr lower_enum_lit_data(writ::TinyMapView node);
    // g9/B121: `T::CONST` where T is an abstract type-param whose bound trait
    // declares `const CONST`. Returns a zero-arg accessor call
    // `T__kassoc_CONST()` (mono rewrites `T__` → concrete; lower_impl_block
    // emits the per-impl accessor) or nullptr if cname isn't such a projection.
    lir::LExprPtr try_lower_generic_assoc_const(const std::string& cname,
                                                const std::string& mname);
    // G167-1/-2: synthesize a Closure type from an Fn-family bound on a
    // TypeVar so an untyped closure literal (`|x|`) appearing where that
    // Fn-bounded type-param is expected (a method `F: FnMut(..)` formal, a
    // generic struct field `f: F`) infers its parameter types from the
    // bound's signature. `tv` is the declared (TypeVar) type; `tparams`
    // carry the bound; `subst` is applied to the bound's param/ret types.
    // Returns a Closure type, or null if `tv` isn't an Fn-bounded TypeVar.
    TypeRef closure_hint_from_fn_bound(TypeRef tv,
                                       const std::vector<TypeParam>& tparams,
                                       const SemaSubst& subst);
    // G167-3: peel Ref/MutRef/Ptr and single-type-arg generic wrappers
    // (`Box<dyn Fn(..)>`) to expose an inner Closure/FnPtr signature, so a
    // closure literal in a wrapped expected-type context (e.g. returned via
    // `box_new(|x| ..)` where the fn returns `Box<dyn Fn(..)>`) can infer its
    // parameter types. Returns the inner callable type, or null.
    TypeRef peel_to_callable(TypeRef t);
    lir::LExprPtr lower_enum_lit_data_from_static(
            writ::TinyMapView node, std::string_view ename, std::string_view vname);
    lir::LExprPtr lower_static_call(writ::TinyMapView node);
    // Trait-static method dispatch through a bound type-param: `Z::m(args)` /
    // `Z::m::<T..>(args)` where Z: SomeTrait declares static `m`. Substitutes
    // Self→Z and the method's OWN type-params (explicit turbofish, else inferred
    // from arg types) into the return type + passes them as call type-args (so
    // mono instantiates `<Concrete>__m::<..>`). nullptr if Z isn't such a param.
    lir::LExprPtr lower_typaram_static_method(
        const std::string& cname, const std::string& mname,
        std::vector<TypeRef> explicit_targs,
        std::vector<lir::LExprPtr> arg_exprs);
    lir::LExprPtr lower_metacall   (writ::TinyMapView node);
    // Function-style macro `name!(args)` / `name![args]` (slice 1 of
    // fn-macros). Resolves CALLEE against #[fn_macro] fns; ARGs are
    // captured as ExprBlobs and passed through the metacall JIT thunk.
    lir::LExprPtr lower_fn_macro_call(writ::TinyMapView node);
    // The leading compiler-built-in macro dispatch of lower_fn_macro_call
    // (cfg!, line!, column!, file!, include!, include_str!/_bytes!, env!,
    // concat!, concat_bytes!, stringify!, compile_error!). Returns the lowered
    // result when `callee_name` is a built-in; nullopt to fall through to the
    // user-defined #[fn_macro] resolution path.
    std::optional<lir::LExprPtr> lower_builtin_macro(
        writ::TinyMapView node, const std::string& callee_name);
    // Large individual built-in macro handlers split out of lower_builtin_macro.
    lir::LExprPtr lower_macro_include(writ::TinyMapView node);
    // Parse `wrap_body` as the tail expression of a synthetic
    // `fn __f() -> i32 { <wrap_body> }`, then lower that expression in the
    // current context (holder swapped to the freshly-parsed doc). Used by
    // the `vec!` builtin to re-parse + lower a synthesized `vec_from_arr([…])`
    // call. Returns error_expr() (after a diagnostic) on parse failure.
    lir::LExprPtr lower_reparsed_tail_expr(const std::string& wrap_body,
                                           std::string_view err_ctx);
    lir::LExprPtr lower_macro_concat(writ::TinyMapView node);
    lir::LExprPtr lower_macro_concat_bytes(writ::TinyMapView node);
    // Bare `{ stmts; tail_expr }` as expression — lowers a BLOCK AST node
    // as an expr whose value is the tail expression (or void if absent).
    static bool is_stmt_only_code(int32_t c);
    lir::LExprPtr lower_block_expr(writ::TinyMapView node);
    // Item-position metacall (MC1.1). Synthesises a void thunk that wraps
    // the inner callee — `let __b = call(); logos_emit_item_blob_subst(&__b);`
    // — and registers a MetacallSite with ret_tag = ItemBlob. Caller
    // (sema.cpp item dispatch) supplies the AST offset of the METACALL_ITEM
    // node so the driver can mark it consumed (CODE → METACALL_ITEM_DONE).
    void          lower_metacall_item(writ::TinyMapView node, lir::LProgram& prog);
    // Slice 6 of fn-macros — `name!{...}` at module item position.
    // Mirrors lower_metacall_item but resolves callee against
    // #[fn_macro] markers and routes args through the per-site
    // arg-blob shim (slice 3 raw-capture path).
    void          lower_fn_macro_call_item(writ::TinyMapView node,
                                           lir::LProgram& prog);
    // ADR 0016 (M2b-2): `mapping` ITEM lowering — validates the parsed item
    // (rel signatures, contextual `rel` keyword, column types), reconstructs
    // canonical (name, params, body) text from the CHECKED nodes, and routes
    // it through emit_token_macro_item_site to the `__mapping_item` handler
    // (logos.std.wql.mapping_item).
    void          lower_mapping_def(writ::TinyMapView node,
                                    lir::LProgram& prog);
    // Which exported wql.peg entry the compiler parses `raw_text` with before
    // handing the handler a pointer into its own arena. `None` = the legacy
    // all-strings ABI (the handler parses the text itself).
    //   Program — `[rel …]* from … select …`, a deem! body
    //   RelList — `rel …+` with no entry query, a mapping body; carries one
    //             extra string blob (the per-rel `pub` mask), since visibility
    //             is an ITEM-layer concept the query grammar knows nothing of.
    enum class IrEntry { None, Program, RelList };

    // The checked pieces of one `mapping M(params) { rel … }` item,
    // reconstructed from the AST by reconstruct_mapping_def. `err` non-empty
    // means the item is invalid and says why.
    struct MappingParts {
        std::string name;
        std::string params_text;           // canonical "g: &Writ, floor: i64"
        std::string body_text;             // canonical `rel …+` list, '\n'-joined
        std::string pub_mask;              // one '0'/'1' per rel, decl order
        std::vector<std::string> rel_names;
        std::string first_param_name;      // the source param (first)
        std::string first_param_type;      // its syntactic type, e.g. "&Writ"
        std::vector<std::pair<std::string, std::string>> params;  // (name, type)
        size_t      nparams = 0;
        // Generic form `mapping M<S: Bound>(g: &S)` (§6 T3): a pure rule
        // module — no standalone fns; consumed via `deem!(w: M<Concrete>)`.
        std::string type_param;           // "S", empty = concrete mapping
        std::string bound;                // the source-trait bound
        bool        is_pub = false;
        bool        is_module_only = false;   // `pub(module)`
        std::string err;
    };
    bool          reconstruct_mapping_def(writ::TinyMapView node,
                                          MappingParts& out);

    // Module-level `mapping` registry (ADR 0016 M2b-2). Filled by a pre-scan in
    // lower_module_items so declaration order never matters, and again by
    // lower_mapping_def (idempotent overwrite) so macro-generated mappings in
    // later metacall rounds register too. Consumed by lower_fn_macro_call_item:
    // a deem! param typed by a mapping name splices that mapping's rules into
    // the consumer's program (fusion, not materialization).
    struct MappingInfo {
        std::string src_param_name;        // the mapping's own source param name
        std::string src_param_type;        // its syntactic type, e.g. "&Writ"
        std::string body_text;             // canonical rel list
        size_t      nrels = 0;
        bool        enrichable = false;    // source-first param shape holds
        std::string type_param;            // "S" for `mapping M<S: Bound>`
        std::string bound;                 // the bound source trait
        bool        is_pub = false;        // cross-package consumption needs pub
        bool        is_module_only = false;// `pub(module)`: same-module only
        std::string package;               // defining package
        std::string module_id;             // defining module (mangle key)
        // Scalar params after the source (`floor: i64`, …). Bound at the
        // consumption site by NAME IDENTITY: the consumer must declare a
        // param with the same name and type — the spliced rule bodies then
        // resolve the scalar exactly as the mapping wrote it, no EL rename.
        std::vector<std::pair<std::string, std::string>> scalars;
    };
    std::unordered_map<std::string, MappingInfo> mappings_;

    // ADR 0020 wave-0: module-level `container` registry. Filled by the same
    // lower_module_items pre-scan as mappings_ (declaration order never
    // matters; CONTAINER_DEF_DONE nodes from a BINARY module's AST re-register
    // here for cross-module Canon reasoning) and again by lower_container_def
    // (idempotent overwrite) for macro-generated containers in later metacall
    // rounds.
    struct ContainerCol     { std::string name, ty; bool is_param = false; };  // ty syntactic; is_param: ty ∈ container generics
    struct ContainerMeasure { std::string mfn, arg; };                          // ("count",""), ("max","key")
    // BTFL 8b: one `ops { … }` entry — verb + optional arg head + optional
    // inner ident: ("insert","",""), ("select","count",""), ("scan","val",""),
    // ("seek","max","key"). The verb VOCABULARY is Canon's to judge, not sema's.
    struct ContainerOp      { std::string verb, arg_head, arg_inner; };
    // BTFL 8b: one `stream N { entry {…} measure m; ops {…} }` block.
    struct ContainerStream  {
        std::string name;
        std::vector<ContainerCol>     entry;
        std::vector<ContainerMeasure> measures;
        std::vector<ContainerOp>      ops;
    };
    struct ContainerInfo {
        std::string name;  std::string package;  // package-qualified identity
        bool is_pub = false;  bool is_module_only = false;
        // True while the declaration node is an UNCONSUMED CONTAINER_DEF in
        // the LATEST pre-scan (the driver flips it to _DONE after its handler
        // ran and emitted). Deem sites over this container's backing type
        // defer while it holds (see enrich_deem_params) — refreshed every
        // pre-scan, so it releases the iteration after the emission lands
        // regardless of whether the checker persists across iterations.
        bool pending = false;
        std::string generics_src;                 // "<T>" / "<K, V>" verbatim (bounds included)
        std::vector<std::string> generic_names;   // ["T"] / ["K","V"]
        std::string backing_src;                  // "VecCtr<T>" — SYNTACTIC render (mapping precedent; resolved only on the emission path)
        std::string backing_pkg;                  // defining package of the backing base type if resolvable, else ""
        std::string kind;                         // "vector" | "ordered_map" | "node" | "branch" | "multimap"
        std::vector<ContainerCol>     entry;      // top-level (BTSS sugar ≡ one anonymous stream)
        std::vector<ContainerMeasure> measures;   // top-level (BTSS sugar)
        std::vector<ContainerOp>      ops;        // top-level `ops {…}` (BTSS sugar; optional)
        std::vector<ContainerStream>  streams;    // BTFL 8b `stream N {…}` blocks (empty = BTSS sugar)
    };
    std::unordered_map<std::string, ContainerInfo> containers_;
    // Reconstructs + validates one CONTAINER_DEF(_DONE) node. Validation here
    // is SHAPE only (clause leads, one kind ∈ {vector, ordered_map}, one
    // non-empty entry, measure arities) — COMPLETENESS (ordered_map requires
    // measure(max(first key col))) is Canon's verdict, never sema's
    // (judge-not-doer, ADR 0020 §5).
    // The one-line `k=v` serialization of a container DECLARATION, shared by
    // the item's lowering and by a deem plan's record.
    std::string   container_spec_line(const ContainerInfo& info) const;
    bool          reconstruct_container_def(writ::TinyMapView node,
                                            ContainerInfo& out,
                                            std::string& err);
    // BTFL 8b: parses ONE clause array — the container body or a `stream`
    // block's nested body (`strm` != null). Fills kind/entry/measures/ops
    // (+ streams at top level). Shape validation only (Canon judges the ops
    // vocabulary + references at decl time).
    bool          reconstruct_container_clauses(writ::ArrayView carr,
                                                const std::string& cname,
                                                ContainerInfo& out,
                                                ContainerStream* strm,
                                                std::string& err);
    // ADR 0020 wave-0: `container` ITEM lowering — registers the declaration,
    // serializes it to the one-line spec string, and routes (name, spec)
    // through emit_token_macro_item_site to the `__container_item` handler
    // (logos.std.canon.container_item), which mirrors it to the FACT doc.
    void          lower_container_def(writ::TinyMapView node,
                                      lir::LProgram& prog);

    // ── Generic containers (ADR 0020 wave-0, S2) ──────────────────────────
    // A `container Map<K,V>` used with CONCRETE args auto-generates a concrete
    // family per (K,V). The generic decl itself emits nothing (the template);
    // the resolver, on meeting `Map<u64,str>`, emits a concrete container DECL
    // SOURCE (this builder) whose NAME is the `$`-mangled instance name, drains
    // it through the normal container path (main.cpp driver), and returns the
    // struct type for the mangled family — deferring the fn body until it lands.

    // build_concrete_container_src — RETIRED (ADR 0021 Phase 4b, harvest gone).
    // True if `base` names a registered GENERIC container (non-empty generics).
    bool          is_generic_container_base(std::string_view base) const;
    // True if `sname` names a not-yet-generated instance of a registered
    // generic container (its family emission is still pending in the fixpoint).
    bool          is_pending_container_type(std::string_view sname);

    // ADR 0016 §6 — sources as relational interfaces. A trait's `rel` members
    // declare a relational vocabulary; an impl binds each rel to its native
    // materializer (`rel edge = writ_graph_edges;`). A deem!/mapping param
    // typed by an implementing type carries the trait's relations, described
    // to the stdlib walker by a SPEC beside the rule IR — the walker's
    // hardcoded Writ/IncrRec dispatch dies. The Writ/IncrRec built-ins are
    // seeded here as the FIRST registrations of the open mechanism; they move
    // to stdlib declarations when cross-module decl-export lands.
    struct TraitRelCol { std::string name, ty; };
    // `file`/`line` are the trait DECLARATION's place, carried because the
    // column check is deferred to the final pass: by then the walk is over and
    // the ambient diagnostic context belongs to whatever item happened to be
    // last, which would attribute a rel error to an unrelated impl.
    struct TraitRelSig { std::string rel; std::vector<TraitRelCol> cols;
                         std::string file; uint32_t line = 0; };
    std::unordered_map<std::string, std::vector<TraitRelSig>> trait_rels_;

    // ── rel COLUMN TYPES (ADR 0024 S1/S2) ────────────────────────────────
    // A rel's rows are a SET (deduplicated) and its columns are join keys, so
    // a column type must be hashable — that, and not membership of a fixed
    // three-name list, is the actual requirement. `Hash` is the checkable form
    // of it: the stdlib impls it for i8/i32/i64/u8/u32/u64/usize/bool/str and
    // deliberately NOT for floats (NaN != NaN breaks both the hash key and the
    // total order), which is exactly the admissible set. A user type joins by
    // deriving it.
    //
    // Checked in the FINAL pass, never at collect time: `trait S { rel r(c: T) }`
    // and `impl Hash for T` may appear in any order, and an order-dependent
    // diagnostic is worse than none.
    bool rel_col_type_hashable(const std::string& ty) {
        logos::compiler::StrSet seen;
        return sema_has_impl_recursive("Hash", ty, {}, seen);
    }
    void check_rel_column_types();
    // ADR 0024 S6 — ONE declared access operation of a source.
    //
    // A planner used to know a family's operations by NAME CONVENTION
    // (`__ctr_at_` / `__ctr_from_` / `__ctr_upto_`) and their applicability by
    // a hardcoded rule, so a source offering a fourth shape had no way to say
    // so and one offering two could not say that either. This is the source
    // SAYING it: which column, which comparison it answers, what implements
    // it, and whether the rows it returns are exactly the matches.
    struct SourceRelOp {
        std::string col;      // the column the operation keys on
        std::string cmp;      // the comparison class: eq / ge / le / gt / lt
        std::string fn;       // the implementing materializer
        bool        exact = false;   // false = a SUPERSET; the query keeps its filter
    };

    struct SourceRelBind {
        std::string trait_name;           // which trait declared the rel
        std::string rel;                  // trait rel name
        std::string mat_fn;               // materializer: fn(&T) -> Vec<RowTuple>
        std::string mat_module;           // its package (empty = resolve at use)
        std::vector<SourceRelOp> ops;     // declared access operations (ADR 0024 S6)
        // ADR 0024 S4 — the SIZE operation: `size <rel> = <fn>;`, a fn of the
        // source alone reporting how many rows the relation holds. DECLARED for
        // the same reason every other capability on this plane is: a Memoria
        // container publishes `measure count`, a `mem` collection has `len`, and
        // a hand-written source may have neither — so a size cannot be derived
        // from a source's shape without excluding the third. Empty = the source
        // does not report one, and the plan says so rather than assuming.
        std::string size_fn;
        // ADR 0025 S3 — the ORDER declaration: `order <rel> = <col>;`, naming
        // the column the relation's rows already arrive sorted by. DECLARED and
        // not derived, and the pairing is the point: the TYPE is the authority
        // on WHETHER the producer is ordered (its return type implements
        // `OrderedBy<K>` or it does not, and `native_source_spec` refuses the
        // declaration when it does not), while the DECLARATION is the only
        // place that can say WHICH of the relation's columns that order is over
        // — a `(key, val)` row has two and `OrderedBy<u64>` names neither.
        // Empty = the source declares no order, and an `order by` over it keeps
        // its Sort node.
        std::string ord_col;
        std::vector<TraitRelCol> cols;
    };
    std::unordered_map<std::string, std::vector<SourceRelBind>> source_impls_;
    bool          builtin_sources_seeded_ = false;
    void          seed_builtin_source_impls();
    // Build the native-source spec for one deem!/mapping param, or "" when its
    // type implements no source trait. Entry grammar (consumed by
    // plan_walker::register_native_rels):
    //   <regname>=<matfn>[!<flags>[%<ret-ty>]][#<arg>][@<module>]
    //           :<param>(<col> <ty>,…)[{<col> <cmp> <fn> <flags>[%<ret-ty>]|…}]
    //           [^<ordcol>][$<sizefn>[%<ret-ty>]];
    // flags are a SET of letters: `e` exact / `s` superset (operations only),
    // `i` the producer returns an iterator (ADR 0024 S4), `b` the pull unit is
    // a batch (ADR 0025 §1), and the three ORDER axes (ADR 0025 S3) `o`
    // `OrderedBy` / `r` `Bidirectional` / `n` `RandomAccess`. A SET, tested by
    // MEMBERSHIP (`pw_flag`), which is what lets a letter land without every
    // reader changing. Operations are
    // separated by `|`, not `,`, because a return type may contain commas.
    // `$` carries the SIZE operation — one per relation, since a size is asked
    // once and of the source as a whole, never per column or per row.
    // `^` carries the ORDERED COLUMN, which the type cannot supply: `o` says
    // the rows are sorted, `^` says by WHICH of the relation's columns.
    // regname = the param name itself for a single-rel vocabulary, else
    // <param>_<rel>; module omitted when the materializer lives in the
    // consuming package.
    std::string   native_source_spec(const std::string& pname,
                                     const std::string& ptype_stripped);
    // ADR 0024 S4 — does this producer STREAM? True when its return type
    // implements `Iterator<Item>`, false when it hands back a container.
    //
    // The source is not asked. Making streaming a KEYWORD on the declaration
    // would let it drift — a source could say `stream` and return a `Vec`, and
    // the plan would be built on a claim nothing checks. The return type cannot
    // lie: it is the declaration, and the type system already enforces it. So
    // a source joins the streaming plane by returning an iterator, and that is
    // the whole of the opt-in.
    //
    // ⚠ Membership only: the ITEM type is not matched against the relation's
    // row type. A source returning `Iterator<WrongThing>` is diagnosed by the
    // host compiler on generated code, which is exactly the diagnostic this ADR
    // is trying to abolish — recorded as debt, not overlooked.
    bool          producer_streams_(const std::string& fn_name);
    // ADR 0025 §1 — the BATCH half of the same question, and the exact twin of
    // `producer_streams_`: does the producer's return type implement
    // `BatchStream`? The two are not exclusive as far as this channel is
    // concerned — a batch producer is still "consumable in place" (the `i`
    // flag, ADR 0024 S4), and `b` says only that THE PULL UNIT IS A BATCH, so
    // the consumer pulls with `next_batch()` and runs §1's inner index loop
    // instead of one `next()` per row. Membership only, with the same recorded
    // debt as above: the BATCH type is not matched against the row type here.
    bool          producer_batches_(const std::string& fn_name);
    // The shared half of the two above: does the producer's RETURN TYPE carry
    // an impl of `trait_name`? Written once so the two flags cannot come to
    // disagree about which type they asked about.
    bool          producer_impls_trait_(const std::string& fn_name,
                                        const std::string& trait_name);
    // The producer's return type, as TEXT — what the emitted binding is
    // annotated with. Empty when the function is not resolvable.
    std::string   producer_ret_type_(const std::string& fn_name);
    // Does this parameter-type TEXT name something the deem pipeline can bind
    // a source from — a registered mapping, or a type (or type family, by
    // base name) carrying a source impl? The pipeline matches by text, so a
    // `false` here is the signal that the written spelling has to be replaced
    // by the resolved one (see lower_deem_def).
    bool          source_named_by_text_(std::string_view ptype) const;
    // Is `name` a `deem` PLAN recorded this round whose bindings have not been
    // generated yet? A plan over a container class compiles where that class
    // meets its type arguments — at a factory drain, a round or more after the
    // call to it was first seen. Calls to it are therefore not-yet-resolved,
    // not undefined; the driver reports a plan that never got instantiated.
    bool          pending_deem_plan_(std::string_view name) const;
    // Record that THIS pass could not finish something because its producer
    // runs later in the same fixpoint. `what` is the thing awaited, `who` the
    // producer that owes it; the site comes from the current context. An
    // unsatisfied record is reported by the driver when the fixpoint stops
    // moving — a deferral that cannot name what it waits for can only be a
    // silence followed by a cascade.
    void          note_pending_(std::string kind, std::string what,
                                std::string who);
    // Record that plan `callee` was called with an argument of this type —
    // the family the plan must be instantiated against.
    bool          note_deem_plan_inst_(std::string_view callee, TypeRef arg);
    // Shared by the deem! macro path and the `deem` ITEM (ADR 0016): mapping
    // fusion + native-source spec over a deem parameter list. Mutates
    // params_text (mapping-type substitution) and raw_text (rule-list
    // prepend); fills enrich + natspec. False = error already reported.
    bool          enrich_deem_params(const std::string& callee_label,
                                     std::string& params_text,
                                     std::string& raw_text,
                                     std::string& enrich,
                                     std::string& natspec);
    void          lower_deem_def(writ::TinyMapView node, lir::LProgram& prog);
    // `#[deem_source(<param> = "<materializer>")]` on a deem item: bind that
    // parameter's rel to the NAMED materializer instead of the one its source
    // impl carries. The seam a capability-directed plan needs — a specialized
    // scan is still the same relation, so everything downstream is unchanged;
    // only which function produces the rows differs. Empty = no override.
    logos::compiler::StrMap<std::string> deem_source_override_;

    // FUSION PROVENANCE (ADR 0024 S0). `enrich_deem_params` splices every bound
    // mapping's rule bodies AHEAD of the query the user wrote, so an offset in
    // the text handed to the parser does not mean what it appears to mean. Each
    // entry is one prepended segment: its EXCLUSIVE end in the fused text and
    // the mapping it came from. Recorded at the splice, stamped onto the parsed
    // root, cleared per item. Empty = nothing spliced, offsets are the user's.
    std::vector<std::pair<size_t, std::string>> deem_fusion_segs_;

    // Shared tail of the #[token_macro] ITEM path (pack arg blobs, synth the
    // JIT thunk, register the MetacallSiteStage); factored from
    // lower_fn_macro_call_item so lower_mapping_def rides the same seam.
    // nargs selects the 1/2/3-str handler ABI when ir_entry == None.
    void          emit_token_macro_item_site(writ::TinyMapView node,
                                             lir::LProgram& prog,
                                             const SemaFuncInfo* macro_info,
                                             bool rt_is_il, int nargs,
                                             const std::string& resource_name,
                                             const std::string& params_text,
                                             const std::string& raw_text,
                                             IrEntry ir_entry = IrEntry::None,
                                             const std::string& pub_mask = {},
                                             const std::string& natspec = {},
                                             const std::string& rules_text = {});

public:
    // ── AST → Logos source pretty-printer (sema_render.cpp) ──────────
    // Used by `metacall (<expr>)` and `metacall { ... }` to splice arbitrary
    // expressions/blocks into the synthesised JIT thunk source. Stage 2
    // (item-position) is also reused by `--dump-metaprog` to emit
    // metafn-generated AST documents as readable Logos source. Pure walks;
    // do not modify sema state. Public so dump-driver code outside the
    // class can render arbitrary sub-trees.
    std::string render_expr_src(writ::TinyMapView node);
    std::string render_stmt_src(writ::TinyMapView node);
    std::string render_block_src(writ::TinyMapView node);
    std::string render_type_src(writ::TinyMapView node);
    std::string render_pat_src(writ::TinyMapView node);
    std::string render_item_src(writ::TinyMapView node);
    // BTFL 8b: one container clause line (kind/entry/measure/ops/stream) at
    // `depth` indent levels — recursion handles nested stream blocks.
    std::string render_container_clause_src_(writ::TinyMapView cl, int depth);
    std::string render_module_src(writ::TinyMapView node);

    // For the --dump-metaprog driver: temporarily point this checker at a
    // foreign Writ doc so render_*_src can walk it without running full
    // sema. Caller is responsible for keeping the holder alive across
    // render calls. When `dump_syntactic_types_` is set, render_type_src
    // bypasses resolve_type and walks TYPE_REF / GENERIC_INST / PTR_TYPE /
    // etc. structurally — necessary for fresh checkers that have empty
    // type pools (no user struct/alias is registered).
    void set_holder_for_render(writ::MemHolder* h) { holder_ = h; }
    void set_render_syntactic(bool on) { dump_syntactic_types_ = on; }
private:
    // Item-rendering sub-helpers (sema_render.cpp Stage 2).
    std::string render_path_parts_(writ::TinyMapView node);
    std::string render_type_param_src_(writ::TinyMapView node);
    // One element of a `<…>` list at USE position (a type ARGUMENT, not a
    // declared parameter) — the impl-header form `impl Trait<Args> for T`,
    // where the grammar stores `type_arg_list` in the TYPE_PARAMS slot.
    // Handles the non-type shapes `type_or_lt_arg` admits (lifetime,
    // const-arg, quote pack/repeat) and hands real types to render_type_src.
    std::string render_type_arg_src_(writ::TinyMapView node);
    // Syntactic render of one TRAIT_BOUND node (`Copy`, `?Sized`,
    // `Iterator<Item = T>`, `Fn(A) -> R`). A bound is NOT a type —
    // resolve_type rejects the node — so the bounds loop of
    // render_type_param_src_ must never route it through render_type_src.
    std::string render_trait_bound_src_(writ::TinyMapView node);
    std::string render_type_param_list_(writ::TinyMapView node);
    std::string render_param_src_(writ::TinyMapView node);
    std::string render_param_list_(writ::TinyMapView node);
    std::string render_field_def_src_(writ::TinyMapView node);
    std::string render_variant_def_src_(writ::TinyMapView node);
    // `pub ` / `pub(module) ` / "" — §4 visibility prefix of an item decl.
    std::string render_vis_prefix_(writ::TinyMapView node);
    // Syntactic type walk used when dump_syntactic_types_ is on (fresh
    // checker with empty type pool — resolve_type would fail).
    std::string render_type_src_syntactic_(writ::TinyMapView node);
    // Inner Writ literal renderer — used recursively from
    // render_expr_src for WRIT_MAP entries / WRIT_ARRAY elements.
    // Omits the `@` prefix on scalars (grammar's writ_val production
    // doesn't accept `@4` / `@"x"` at this position; outer writ_lit
    // does).
    std::string render_writ_val_inner_(writ::TinyMapView node);
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
    lir::LExprPtr lower_if_expr(writ::TinyMapView node);
    // §6.4: let-chain desugar. Recursively builds nested `if let`/
    // `if` LIR over the seg list, with the ELSE branch duplicated at
    // each fall-through site (matches Rust's classic desugar; user-
    // visible side effects in ELSE are dup'd, which is an accepted
    // limitation of the simple expansion).
    lir::LExprPtr lower_if_let_chain(writ::TinyMapView node);
    lir::LExprPtr lower_closure_expr(writ::TinyMapView node);

    // ── lower_stmt and friends ───────────────────────────────────

    lir_view::StmtRef lower_stmt(writ::TinyMapView stmt);
    lir_view::StmtRef lower_stmt_inner(writ::TinyMapView stmt);
    lir_view::BlockRef lower_block(writ::TinyMapView block);
    lir_view::StmtRef lower_let_destruct(writ::TinyMapView node);
    lir_view::StmtRef lower_let_pat(writ::TinyMapView node);
    lir_view::StmtRef lower_let(writ::TinyMapView node);
    lir_view::StmtRef lower_let_else(writ::TinyMapView node);
    lir_view::StmtRef lower_nested_fn(writ::TinyMapView node);
    lir_view::StmtRef lower_compound_assign(writ::TinyMapView node);
    lir_view::StmtRef lower_place_compound_assign(writ::TinyMapView node,
                                           writ::TinyMapView place_node,
                                           const std::string& base_op);
    std::string render_place_node(writ::TinyMapView n);
    lir_view::StmtRef lower_assign(writ::TinyMapView node);
    lir_view::StmtRef lower_destructure_assign(writ::TinyMapView node);
    lir_view::StmtRef lower_return(writ::TinyMapView node);
    lir::Pattern build_pattern(writ::TinyMapView pnode, TypeRef scrut_type);
    // Internal: build_pattern's body without eager mirror emit. Recurses via
    // build_pattern (so sub-patterns get their own eager emit).
    lir::Pattern build_pattern_impl(writ::TinyMapView pnode, TypeRef scrut_type);
    // build_pattern_impl sub-handlers, factored out of its pc-keyed dispatch.
    // Each lowers one pattern kind; depends only on pnode/scrut_type/members
    // (recurses via build_pattern), no state shared across branches.
    lir::Pattern build_pattern_variant(writ::TinyMapView pnode, TypeRef scrut_type);
    lir::Pattern build_pattern_variant_data(writ::TinyMapView pnode, TypeRef scrut_type);
    lir::Pattern build_pattern_bytes(writ::TinyMapView pnode, TypeRef scrut_type);
    lir::Pattern build_pattern_or(writ::TinyMapView pnode, TypeRef scrut_type);
    // Helper for inline PatWild construction with eager mirror emit.
    lir::Pattern make_pat_wild(std::string_view name);
    // If pnode is a Writ scalar pattern (PAT_WRIT_NULL/BOOL/INT), returns a
    // bool-typed guard call that evaluates the pattern against `scrut_var`
    // (which must be an AnyVal).  Returns nullptr otherwise.
    struct WritPatBinding {
        std::string name;        // user-visible binding name in arm body
        std::string av_var;      // AnyVal local holding the value
    };
    lir::LExprPtr build_writ_pat_guard(writ::TinyMapView pnode,
                                         const std::string& scrut_var,
                                         TypeRef scrut_type,
                                         const std::string& base_var,
                                         std::vector<lir_view::StmtRef>& out_stmts,
                                         std::vector<WritPatBinding>& out_bindings);
    // Returns the "inner" (ref-stripped) view type if `t` is Writ,
    // WritView<'_>, or WritStatic (possibly behind &/&mut). nullptr otherwise.
    TypeRef writ_view_inner(TypeRef t) const {
        TypeRef tr(t);
        if (!tr) return nullptr;
        TypeRef inner = tr;
        if (tr.kind() == LogosType::Kind::Ref ||
            tr.kind() == LogosType::Kind::MutRef)
            inner = tr.pointee();
        if (!inner) return nullptr;
        if ((inner.kind() == LogosType::Kind::Struct ||
             inner.kind() == LogosType::Kind::ZonedStruct) &&
            (inner.struct_name() == "Writ" ||
             inner.struct_name() == "WritView" ||
             inner.struct_name() == "WritStatic" ||
             inner.struct_name() == "Rc"))   // writ runtime container Rc<Writ>
            return inner;
        return nullptr;
    }
    // True while lowering match arms where Writ scalar patterns are
    // explicitly handled by the caller (desugared to guard). Outside this
    // context, PAT_WRIT_* in build_pattern is a diagnostic.
    bool in_match_writ_ctx_ = false;
    // P4-pm-02: side channel for build_pattern to register nested
    // sub-pats that need irrefutable destructure in the arm-body prologue
    // (e.g. `Some(A { foo: _x })` → synth `__pat_pld_*` binding for the
    // payload slot, then `let A { foo: _x } = __pat_pld_*;` at body
    // start). Caller wires this before build_pattern and consumes the
    // entries when building the arm body.
    struct NestedPatSub {
        std::string                synth_name;
        writ::TinyMapView        sub_pat_node;
    };
    std::vector<NestedPatSub>* current_pat_nested_subs_ = nullptr;
    // B170-E: when ≥0, build_pattern_variant selects this alternative index of a
    // multi-alt PAT_OR appearing as a variant payload arg (`Some((a,_)|(_,a))`).
    // Set per fanned-out effective arm so the or distributes into one arm per
    // alternative (`Some(P|Q)` → `Some(P) | Some(Q)`), each re-evaluating the
    // guard with its own bindings (rustc backtracks alts under a failing guard).
    int32_t payload_or_alt_ = -1;
    // K4: emit `let <variant-sub-pat> = synth else { loop {} }` body-prologue
    // stmts (into `out`) that re-extract the bindings of a nested variant
    // payload pattern (e.g. `Some(Some(v))`), defining them in the current
    // scope. Recurses for deeper nesting. The owning arm's guard already
    // ensured the match, so the else block is dead.
    void emit_nested_variant_lets(const std::string& synth_name, TypeRef synth_t,
                                  writ::TinyMapView sub_pat,
                                  std::vector<lir_view::StmtRef>& out);
    // Emit body-prologue `let` destructures for the nested sub-patterns
    // collected in `nested_subs` (tuple/struct/variant payloads). Shared by
    // match arms and if-let/while-let so all three handle nested payload
    // patterns identically. `for_guard` skips the refutable nested-variant
    // let-else (used when building a guard prologue, not the arm body).
    void emit_nested_pat_destructure(const std::vector<NestedPatSub>& nested_subs,
                                     std::vector<lir_view::StmtRef>& out, bool for_guard);
    // G-CONF-1: bind a `for PATTERN in iter` loop variable. `src_var` holds one
    // element (type `src_type`); defines the pattern's bindings in the current
    // scope and appends the destructure `let`s to `out`. Returns false (with a
    // diagnostic) for a pattern shape not yet supported in for-position. A bare
    // single binding is handled by the caller (NAME fast-path) and never reaches
    // here.
    bool emit_for_pattern_destructure(writ::TinyMapView pat,
                                      const std::string& src_var, TypeRef src_type,
                                      std::vector<lir_view::StmtRef>& out);
    // K4: recursive AST-level exhaustiveness for nested enum-payload patterns.
    // The LIR-level check skips guarded arms, so a desugared nested match
    // (`Some(Some(v))` / `Some(None)` / `None`) looks non-exhaustive. This
    // verifies coverage on the original AST patterns, descending into each
    // variant's single payload. `pats` are the unguarded arm LHS nodes.
    bool ast_patterns_exhaustive(std::vector<writ::TinyMapView> pats, TypeRef ty);
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

    // Temporary-scope drop (Rust temporary scope = end of statement). When a
    // DROPPABLE rvalue is auto-ref'd as a `&self`/`&mut self` method receiver
    // (`W::mk(…).get()`), the materialized temporary must live to the end of the
    // enclosing statement and then drop. lower_stmt installs this collector; the
    // auto-ref site hoists such a temp into it (name, type, value); lower_stmt
    // then wraps the statement in an SBlock with those hoisted `let`s prepended,
    // so the block's scope-exit drop runs the destructors (no explicit drop
    // emission — reuses the normal scope-drop machinery). nullptr ⇒ not in a
    // statement context (or nested expr already inside one). Save/restore across
    // lower_stmt recursion.
    // (name, type, value, is_mut) — is_mut=true when the hoisted receiver temp
    // is auto-ref'd `&mut self` (so the emitted `let` must be `let mut`, else a
    // `&mut self` method call on a temporary — `iter_copied(..).find(..)` —
    // fails borrow-check: "cannot borrow '__rtmp' as mutable").
    std::vector<std::tuple<std::string, TypeRef, lir::LExprPtr, bool>>* cur_stmt_temp_hoist_ = nullptr;
    // The scope_ frame index the active temp collector belongs to. Every hoisted
    // temp is ALSO define()'d into this frame (a real scope-tracked local), so an
    // early exit inside the same statement/expression (`if b && make().m() {
    // return; }`, `b && return x`) drops it via collect_all_drops /
    // collect_drops_to_loop — the wrap's fall-through drops are unreachable past
    // a terminator. SIZE_MAX ⇒ no collector installed.
    size_t cur_stmt_temp_hoist_frame_ = SIZE_MAX;
    // Hoist a fresh droppable rvalue into the active statement/expression
    // temp-scope: appends (name,type,value,is_mut) to the collector, defines the
    // synth local in the collector's frame, and returns a VarRef to it. The
    // caller substitutes the VarRef for the rvalue; the installer emits the
    // `let`s and the scope drops.
    lir::LExprPtr hoist_stmt_temp(lir::LExprPtr v, bool is_mut);
    // Rust temporary LIFETIME EXTENSION (destructors.md,
    // r[destructors.scope.lifetime-extension.exprs]). Borrow nodes that sit in
    // an extending position of a `let` initializer — operand of a borrow, of a
    // cast / array / braced-struct / tuple, an enum-variant ctor argument, a
    // block or branch tail — bind temporaries that must outlive the STATEMENT,
    // so they keep the plain addr_of_temp frame slot instead of taking the
    // statement-scope hoist. Keyed by AST node pointer, which is stable and
    // position-determined; the set only ever grows. `CALL` arguments are
    // deliberately NOT extending (Rust excludes them) — that is the leak the
    // hoist exists to fix.
    std::unordered_set<const void*> extending_borrow_nodes_;
    void mark_extending_borrows(writ::TinyMapView e);
    // Lower a LAZILY- or REPEATEDLY-evaluated subexpression (a `&&`/`||` RHS, a
    // while-loop condition, a while-let scrutinee, an if-expression branch, an
    // expression-bodied closure) in its OWN temporary scope: droppable rvalue
    // receivers materialized inside it are bound + dropped WITHIN the resulting
    // expression (a block-expr), not hoisted to the enclosing statement — the
    // statement-level hoist would evaluate them EAGERLY (and, for a loop
    // condition, exactly ONCE — an infinite-loop miscompile). Returns the plain
    // lowered expression when no temps were materialized.
    lir::LExprPtr lower_expr_temp_scoped(writ::TinyMapView node);
    // Emit `return <val>` with the FULL scope unwind, mirroring lower_block's
    // Return-statement handling (bind the value FIRST — it may read/move locals
    // the drops release — then collect_all_drops, then the terminator). For use
    // by desugars that synthesize early returns in EXPRESSION position (`?`),
    // which lower_block's statement-level drop insertion never sees.
    std::vector<lir_view::StmtRef> make_return_with_drops(lir::LExprPtr val);
    // #122 — APPEND A LOWERED STATEMENT WITH THE SCOPE UNWIND ITS TERMINATOR
    // NEEDS. `lower_block` did this inline, so it was the ONLY block builder
    // that unwound: every block lowered in EXPRESSION position (an if-expr /
    // if-let-expr branch, a match-expr block arm, a bare block-expr, an
    // `unsafe {}` at expression position) appended `lower_stmt(s)` raw, and a
    // `return` / `break` / `continue` reached through one of them ran ZERO
    // destructors — a leak of every live local, measured at
    // `let k = if c { 10 } else { return -1; };` with two untouched droppable
    // locals (2 created, 0 dropped). Every such site now routes here.
    // Return  ⇒ bind the value FIRST (it may read locals the drops release),
    //           then collect_all_drops (innermost frame outward, stopping at a
    //           closure boundary), then the terminator.
    // Break/  ⇒ collect_drops_to_loop: every frame down to AND INCLUDING the
    // Continue   loop body, since the loop edge bypasses the body's end drops.
    // Anything else is appended unchanged, so a block with no terminator is
    // byte-identical to the pre-#122 shape.
    void push_stmt_with_unwind(std::vector<lir_view::StmtRef>& out,
                               lir_view::StmtRef lowered);
    // #122 — HOW a lowered EXPRESSION arm diverges, mirroring the statement
    // form's `branch_div_kind` in lower_if: 0 = reaches the merge, 1 = leaves
    // the function by `return`, 2 = leaves by the LOOP EDGE (`break` /
    // `continue`). The distinction is the whole point: a `return` arm's moves
    // are settled at its own unwind and it never reaches the enclosing frame's
    // scope-exit drops, but a `break`/`continue` arm DOES reach them — via the
    // loop edge — so it is a reaching path for drop-flag elaboration.
    // lower_if_expr / lower_match_expr classified BOTH as "diverging" (both
    // type as Never) and dropped them from `reaching`; with one arm left,
    // `elaborate_cond_moves` bailed at `reaching.size() < 2` and the union
    // merge's static suppression LEAKED the value on the break path
    // (`loop { let k = if c { x } else { break; }; … }`, 1 created 0 dropped).
    int expr_arm_div_kind(const lir::LExprPtr& v) const;
    // Set by lower_return when its value lowering hoisted statement-temporaries:
    // the value must be pre-bound to this local so lower_stmt can emit
    // `let __t…; let __rv = <value>; drop __t…; return __rv;` — the temp drops
    // must run BEFORE the `return` terminator, else they are dead code past it
    // and the temporaries leak. {name, type, value-expr}. Reset each lower_stmt.
    std::optional<std::tuple<std::string, TypeRef, lir::LExprPtr>> pending_ret_bind_;
    bool is_hoistable_temp_rvalue(lir_view::ExprRef e);
    // Auto-ref a method receiver to `&self`/`&mut self`. When `recv` is a fresh
    // DROPPABLE rvalue and a statement temp-scope is active, hoist it to a named
    // local (so its scope-exit drop runs at end of statement — Rust temporary
    // scope) and borrow that; otherwise spill via addr_of_temp as before.
    lir::LExprPtr materialize_recv_ref(lir::LExprPtr recv, bool is_mut, TypeRef ref_type);

    // Phase 2b: emit a generic-aware `recv.deref()` (or deref_mut) step. If
    // recv's type implements Deref/DerefMut — INCLUDING a generic impl like
    // Box/Rc/Arc whose `deref` is not a concrete symbol at sema time (so
    // find_func_by_base_and_signature can't see it) — emit the deref method
    // call via method_call_v (mono monomorphizes it, exactly like an explicit
    // `x.deref()`), and return the dereferenced Target place. nullopt when the
    // receiver type implements no Deref. The concrete Target is computed by
    // substituting the Deref impl's target pattern against recv's type.
    std::optional<lir::LExprPtr> emit_generic_deref_step(lir::LExprPtr recv, bool want_mut);
    std::optional<lir::LExprPtr> emit_generic_deref_call(lir::LExprPtr recv, bool want_mut);

    void bind_pattern(const lir::Pattern& pat,
                      TypeRef scrut_type = nullptr);
    void bind_pattern_ref(lir_view::PatRef pr, TypeRef scrut_type);
    lir_view::StmtRef lower_if(writ::TinyMapView node);
    lir_view::StmtRef lower_while(writ::TinyMapView node);
    lir_view::StmtRef lower_for(writ::TinyMapView node);
    lir_view::StmtRef lower_for_each(writ::TinyMapView node);
    lir_view::StmtRef lower_loop(writ::TinyMapView node);
    lir_view::StmtRef lower_place_assign(writ::TinyMapView node);
    bool place_write_supported(writ::TinyMapView place);
    bool check_place_writable(writ::TinyMapView place);
    TypeRef resolve_place_type(writ::TinyMapView place);
    std::optional<lir_view::StmtRef> try_index_mut_assign(
        const std::string& arr_name, TypeRef arr_type,
        writ::TinyMapView idx_node, writ::TinyMapView val_node);
    // ADR 0011 — schema field WRITE: `p.field = v` ⇒ `(&mut* p.m).set(KEY, WAny::from(v))`.
    std::optional<lir_view::StmtRef> try_schema_field_write(
        const std::string& recv_name, const std::string& field_name,
        writ::TinyMapView val_node);
    bool place_recv_is_simple(writ::TinyMapView recv);
    bool place_field_base_ok(writ::TinyMapView recv);
    writ::TinyMapView unwrap_paren_node(writ::TinyMapView n);
    lir_view::StmtRef lower_match(writ::TinyMapView node);
    // ADR 0011 — desugar a `match` over a `schema enum` into an if-chain on the
    // pointee's schema_type_code. Assumes scrut_type is a schema-enum view.
    lir_view::StmtRef lower_schema_enum_match(writ::TinyMapView node,
                                              lir::LExprPtr scrut, TypeRef scrut_type);
    lir::LExprPtr lower_match_expr(writ::TinyMapView node);
    // G156-2: mark a by-value move-type match scrutinee var as moved when an
    // unguarded arm binds+moves it (whole-binding / struct / tuple / variant
    // payload). Shared by the statement and expression match paths so the
    // enum's scope-exit Drop doesn't double-free a value a binding/result owns.
    void mark_match_scrutinee_moved(const lir::LExprPtr& scrut, TypeRef scrut_type,
                                    writ::TinyMapView node);
    // G156-1: mangle an impl's concrete trait type-args into a `$G<n>$<a1>$…`
    // suffix appended to a trait name in a qualified method base
    // (`X__Trait$G1$u64__m`). Uses the `$G` generic-marker scheme so
    // bare_fn_name preserves it (a plain `$` is stripped as a pkg separator).
    // Empty for no args. Must be byte-identical across collect/lower/dispatch.
    std::string trait_targ_suffix(const std::vector<TypeRef>& args) const;
    // G156-1: strip a `$G<n>$...` trait-type-arg suffix baked into an
    // AssocType's trait_name, recovering the bare trait name. No-op when absent.
    static std::string strip_trait_targ_suffix(std::string_view s) {
        auto d = s.find('$');
        return std::string(d == std::string_view::npos ? s : s.substr(0, d));
    }
    // G156-1: look up an assoc-type impl, preferring the trait-type-arg-suffixed
    // key (dual `Trait<T>` impls for one type) when the args are known from the
    // current impl context (current_impl_trait_args_); falls back to the plain
    // (single-impl / non-generic-trait) key. Returns nullptr if neither exists.
    const AssocTypeEntry* find_assoc_type_entry(const std::string& trait_name,
                                                const std::string& target,
                                                const std::string& aname) const;
    // Exhaustiveness analysis for a lowered match (enum / bool scrutinee):
    // emits a diagnostic if a non-guarded wildcard is absent and some
    // variant / bool value is uncovered. Read-only over `smatch`; factored
    // out of lower_match.
    void check_match_exhaustiveness(const lir::SMatch& smatch, TypeRef scrut_type,
                                    bool ast_proven_exhaustive = false);

    // ── lower_fn and declaration lowering ───────────────────────

    // Direct-build the Func decl mirror STRAIGHT into the program WritCtr and
    // return the open DeclBuilder (caller stores .view<FunctionView>() /
    // .self.addr()). No heap accumulator. When `out_type_params` is non-null,
    // lower_fn does NOT emit TYPE_PARAMS / IMPL_TYPE_PARAMS / IMPL_TARGET_PATTERN
    // / IS_PUB / WHERE_TYPE_BOUNDS into the builder — those are deferred to the
    // impl-method callers (which filter type_params, set impl-level params, the
    // target pattern, trait-method visibility, and per-method where-bounds AFTER
    // lowering) — and instead writes the computed sema-side type_params into
    // `*out_type_params`. When null (free fns / collected struct methods), the
    // builder is complete on return.
    DeclBuilder lower_fn(writ::TinyMapView node, std::string_view struct_ctx = {},
                         std::vector<TypeParam>* out_type_params = nullptr);
    // Derive `lifetime_outlives` from the fn's params/return implied bounds
    // plus its where-clause (and merge where-clause type-param lifetime
    // bounds). Reads `node` + the fn's signature locals; appends to
    // `lifetime_outlives` and mutates `type_params[].lifetime_outlives`.
    // Factored out of lower_fn (Stage E: takes locals, not a Draft).
    void compute_fn_lifetime_outlives(
        writ::TinyMapView node,
        std::string_view fn_name,
        const std::vector<std::string>& lifetime_params,
        const std::vector<lir::LParam>& params,
        TypeRef ret_type,
        std::vector<TypeParam>& type_params,
        std::vector<std::pair<std::string, std::string>>& lifetime_outlives);
    DeclBuilder lower_struct_def(writ::TinyMapView node);
    // Stage E direct-build: builds the whole enum mirror (NAME/PKG/DOC/flags/
    // backing/variants/type_params) STRAIGHT into the program WritCtr via
    // DeclBuilder (no Draft) and returns an EnumView. DOC is consumed here via
    // take_pending_doc(); the caller just pushes the View.
    lir_view::EnumView lower_enum_def(writ::TinyMapView node);
    // Stage E direct-build: builds NAME+TYPE_REF into a Const DeclBuilder (in
    // the program WritCtr, no Draft) and returns it + the value ExprRef. The
    // caller adds VALUE/DOC/IS_STATIC/etc. (the static path overrides VALUE for
    // externals) and pushes a ConstView.
    std::pair<DeclBuilder, lir_view::ExprRef> lower_const_def(writ::TinyMapView node);
    std::pair<std::string, TypeRef> lower_type_alias_def(writ::TinyMapView node);
    DeclBuilder lower_trait_def(writ::TinyMapView node);
    void lower_impl_block(writ::TinyMapView node, lir::LProgram& prog);
    void lower_program(const std::vector<writ::Writ>& asts, lir::LProgram& prog);
    void lower_module_items(writ::TinyMapView mod, lir::LProgram& prog);
    lir::LAnnotationValue parse_annot_literal(writ::TinyMapView v);
    std::optional<lir::LAnnotationInstance>
        build_annotation_instance(writ::TinyMapView ann,
                                  std::string_view ann_name,
                                  std::string_view ann_pkg,
                                  const SemaStructInfo& ann_info);

    // ── Member overloads of LExpr*-taking helpers (B.5 sites 1-3) ───────
    // These wrap the free ExprRef-based helpers so the variant peek lives
    // here (via expr_ref_of), letting B.6 drop the LExpr-variant payload
    // without touching ~100 call sites in sema_stmt/sema_expr.
    std::optional<int64_t> get_intlit_value(lir_view::ExprRef e) const noexcept {
        return logos::compiler::get_intlit_value(e);
    }
    void widen_int_expr(lir::LExprPtr& e, TypeRef target, LirBuilder b) {
        if (!e || !target || !expr_type(e)) return;
        auto ek = TypeRef(expr_type(e)).kind();
        auto tk = TypeRef(target).kind();
        // G149-2 (silent miscompile): `&<int-literal>` passed where `&T` is
        // expected. The arg lowers to an AddrOfTemp whose inner literal stays
        // its default width (IntLit→i32), so codegen allocates an i32 temp and
        // stores i32 — but the callee loads `T` (e.g. i64) through the pointer,
        // reading adjacent stack garbage. Recurse into the temp's inner literal
        // and widen it to the pointee so the temp's slot is sized to T.
        if ((ek == LogosType::Kind::Ref || ek == LogosType::Kind::MutRef) &&
            (tk == LogosType::Kind::Ref || tk == LogosType::Kind::MutRef) &&
            TypeRef(expr_type(e)).pointee() && TypeRef(target).pointee()) {
            auto er = expr_ref_of(e);
            if (er.kind() == lir_schema::expr::Code::AddrOfTemp) {
                lir_view::EAddrOfTempView av{er};
                TypeRef ipt = TypeRef(target).pointee();
                if (auto inner = av.inner()) {
                    TypeRef inner_ty = inner.type(cur_prog_->type_pool.impl());
                    auto ik = inner_ty ? inner_ty.kind()
                                       : LogosType::Kind::Error;
                    bool widenable = is_integer_kind(ik) &&
                                     is_integer_kind(TypeRef(ipt).kind()) &&
                                     ik != TypeRef(ipt).kind() &&
                                     (can_widen_int(ik, TypeRef(ipt).kind()) ||
                                      (get_intlit_value(inner) &&
                                       intlit_fits(*get_intlit_value(inner),
                                                   TypeRef(ipt).kind())));
                    if (widenable) {
                        lir::LExprPtr casted = b.cast(inner, ipt);
                        e = b.addr_of_temp(casted, av.is_mut(), target);
                    }
                }
            }
            return;
        }
        if (ek == tk) return;
        bool ok = can_widen_int(ek, tk);
        // A literal may adopt a NARROWER concrete integer it fits — but never
        // an Enum: `coerce.enum.to-integer-discriminant` allows enum→int, and
        // FORBIDS int→enum (it would reinterpret the value as the enum's
        // storage). is_integer_kind includes Enum for other purposes, so the
        // exclusion must be explicit here, exactly as it already is in
        // arg_compatible_for_dispatch ("pt is guarded too"). This is how the
        // refusal pinned by coerce_diag_1__enum-to-integer-discriminant
        // vanished: the old let path never ran this adoption; the expect_type
        // pipeline does.
        if (!ok && ek != LogosType::Kind::Enum &&
            TypeRef(target).kind() != LogosType::Kind::Enum &&
            is_integer_kind(ek) && is_integer_kind(TypeRef(target).kind())) {
            if (auto v = get_intlit_value(e))
                if (intlit_fits(*v, TypeRef(target).kind()))
                    ok = true;
        }
        if (!ok) return;
        e = b.cast(std::move(e), target);
    }
    bool arg_compatible_for_dispatch(lir_view::ExprRef arg,
                                     TypeRef at,
                                     TypeRef pt) const noexcept {
        if (types_equal(at, pt)) return true;
        if (types_compatible(at, pt)) return true;
        // A `&[E; N]` argument dispatches against a `&[E]` slice param: the
        // call site will decay it (CFLAG_ARRAY_TO_SLICE). The selector must
        // accept what the coercion pipeline can produce, or the candidate is
        // rejected before the coercion ever runs and the call reports
        // "no method" instead of coercing.
        if (at && pt &&
            (TypeRef(at).kind() == LogosType::Kind::Ref ||
             TypeRef(at).kind() == LogosType::Kind::MutRef) &&
            TypeRef(at).pointee() &&
            TypeRef(TypeRef(at).pointee()).kind() == LogosType::Kind::Array &&
            TypeRef(pt).kind() == LogosType::Kind::Slice &&
            (!TypeRef(pt).mut_ptr() ||
             TypeRef(at).kind() == LogosType::Kind::MutRef) &&
            types_equal(TypeRef(TypeRef(at).pointee()).elem(),
                        TypeRef(pt).elem()))
            return true;
        // An UNSUFFIXED int LITERAL that fits a narrower integer param dispatches
        // (e.g. `push(u8)` with `7`). Gated on `at == IntLit`: a SUFFIXED literal
        // (`9u64`, `5i32`) has a CONCRETE type and must match by type, not flex by
        // value — else `push(9u64)` spuriously also matches `push(i64)` (the value
        // fits i64) and the first-declared overload wins, picking the wrong width.
        // (Rust parity: `9u64` is `u64`, period.) NOTE: `IntLit` excludes `Enum`,
        // so a data/niche-enum param can't be hit by an integer (would reinterpret
        // the int as the enum's by-pointer storage → UB); `pt` is guarded too.
        if (arg && at && pt &&
            TypeRef(at).kind() == LogosType::Kind::IntLit &&
            is_integer_kind(TypeRef(pt).kind()) &&
            TypeRef(pt).kind() != LogosType::Kind::Enum)
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
    // WIDTH is this table's business; SIGNEDNESS is not — it is read from
    // LogosType::is_signed_int_kind so a kind can never be wide here and
    // signed-vs-unsigned somewhere else.
    unsigned w = 0;
    switch (k) {
    case K::Usize: case K::Isize: w = static_cast<unsigned>(g_target_pointer_bits); break;
    case K::I8:   case K::U8:   w = 8;   break;
    case K::I16:  case K::U16:  w = 16;  break;
    case K::I24:  case K::U24:  w = 24;  break;
    case K::I32:  case K::U32:  w = 32;  break;
    case K::I56:  case K::U56:  w = 56;  break;
    case K::I64:  case K::U64:  w = 64;  break;
    case K::I128: case K::U128: w = 128; break;
    default:      return {0, false};
    }
    return {w, LogosType::is_signed_int_kind(k)};
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
// Writ mirror so it composes with view-migrated read sites without an
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

// The value of a FLOAT literal, with the suffix and the `_` separators already
// stripped by the caller. `noexcept` and total: every input yields a double.
//
// This exists because `std::stod` THROWS, and the compiler called it at four
// sites without a handler. libstdc++ raises `std::out_of_range` whenever
// `strtod` sets ERANGE — and strtod sets ERANGE for an UNDERFLOW as well as an
// overflow, so every SUBNORMAL literal killed the compiler:
// `let s: f64 = 5.0e-324f64;` terminated with
// `terminate called after throwing an instance of 'std::out_of_range'` and no
// source location, no diagnostic, nothing a caller could act on.
//
// The correct answer is what `strtod` already computed: on underflow it returns
// the correctly-rounded subnormal (or ±0.0), on overflow ±HUGE_VAL = ±inf.
// Both are the values the literal denotes. ERANGE is not an error here.
inline double parse_float_literal(std::string_view sv) noexcept {
    std::string s(sv);
    errno = 0;
    const char* b = s.c_str();
    char* end = nullptr;
    double v = std::strtod(b, &end);
    if (end == b) return 0.0;  // no conversion — the caller's format check ran first
    return v;
}

// 128-bit magnitude parse (sign ignored — caller negates). Mirrors
// parse_int_literal but accumulates into unsigned __int128, so `u128`/`i128`
// literals whose value exceeds 64 bits round-trip intact. Stops at the suffix.
inline unsigned __int128 parse_int_literal_u128(std::string_view sv) noexcept {
    if (!sv.empty() && sv[0] == '-') sv = sv.substr(1);
    int base = 10;
    if (sv.size() >= 2 && sv[0] == '0') {
        if (sv[1] == 'x' || sv[1] == 'X') { base = 16; sv = sv.substr(2); }
        else if (sv[1] == 'b' || sv[1] == 'B') { base = 2;  sv = sv.substr(2); }
        else if (sv[1] == 'o' || sv[1] == 'O') { base = 8;  sv = sv.substr(2); }
    }
    unsigned __int128 result = 0;
    for (char c : sv) {
        if (c == '_') continue;
        int d = -1;
        if (c >= '0' && c <= '9') d = c - '0';
        else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
        if (d < 0 || d >= base) break;  // suffix start
        result = result * (unsigned __int128)base + (unsigned __int128)d;
    }
    return result;
}

// Does the literal's magnitude exceed 128 bits? (the wide analogue of
// parse_int_literal_overflows, used for `u128`/`i128` suffixed literals).
inline bool parse_int_literal_overflows_128(std::string_view sv) noexcept {
    if (!sv.empty() && sv[0] == '-') sv = sv.substr(1);
    int base = 10;
    if (sv.size() >= 2 && sv[0] == '0') {
        if (sv[1] == 'x' || sv[1] == 'X') { base = 16; sv = sv.substr(2); }
        else if (sv[1] == 'b' || sv[1] == 'B') { base = 2;  sv = sv.substr(2); }
        else if (sv[1] == 'o' || sv[1] == 'O') { base = 8;  sv = sv.substr(2); }
    }
    unsigned __int128 result = 0;
    for (char c : sv) {
        if (c == '_') continue;
        int d = -1;
        if (c >= '0' && c <= '9') d = c - '0';
        else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
        if (d < 0 || d >= base) break;
        unsigned __int128 prev = result;
        result = result * (unsigned __int128)base + (unsigned __int128)d;
        if (result / (unsigned __int128)base != prev) return true;  // overflowed 128 bits
    }
    return false;
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
    auto is_dec = [](char c) { return c >= '0' && c <= '9'; };
    size_t dot = main.find('.');
    if (dot == std::string_view::npos) {
        // Bare-mantissa exponent (`5e9`, `5e-11`): no decimal point, so an
        // exponent is REQUIRED (a plain integer is not a float literal).
        size_t exp0 = main.find_first_of("eE");
        if (exp0 == std::string_view::npos) return false;
        if (!valid_digit_groups(main.substr(0, exp0), is_dec)) return false;
        size_t es = exp0 + 1;
        if (es < main.size() && (main[es] == '+' || main[es] == '-')) ++es;
        return valid_digit_groups(main.substr(es), is_dec);
    }

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
    StrMap<writ::TinyMapView>            module_const_values;
    StrMap<SemaChecker::GenericConstEntry> generic_consts;
    StrMap<SemaChecker::SemaTraitInfo>    traits;
    StrMap<SemaChecker::SemaImplInfo>     impls;
    StrMap<std::vector<SemaChecker::SemaImplInfo>> impls_all;
    StrSet                                 coherence_keys;
    StrMap<SemaChecker::AssocTypeEntry>   assoc_type_impls;
    StrMap<SemaChecker::AssocConstEntry>  assoc_const_impls;
    std::vector<SemaChecker::BlanketImpl>  blanket_impls;
    std::vector<MetaprogHandlerStage> metaprog_handlers;
    std::vector<MetaprogTargetStage>  metaprog_targets;
    std::set<std::string>                   copy_types;
    std::unordered_map<std::string, std::vector<size_t>> conditional_copy;
    StrMap<std::vector<std::string>>       pkg_reexports;
    // ADR 0016 registries: cross-round persistence — the round-2 sema skips
    // re-collecting cached (stdlib) holders, so anything collect-derived that
    // is not snapshotted would silently VANISH between metaprog rounds (the
    // emitted `impl GraphSource for T` then failed with "trait declares no
    // rel 'edge'" — the trait was collected one round earlier).
    std::unordered_map<std::string, std::vector<SemaChecker::TraitRelSig>>   trait_rels;
    std::unordered_map<std::string, std::vector<SemaChecker::SourceRelBind>> source_impls;
    std::unordered_map<std::string, SemaChecker::MappingInfo>                mappings;
    bool builtin_sources_seeded = false;
    // Holders whose collect_module() contribution is already in these
    // tables. SemaChecker's per-AST loops skip walks for holders in
    // this set — re-walking would be deduped but the walk itself is
    // the cost we want to skip.
    std::unordered_set<const writ::MemHolder*> collected_holders;

    // Cached WStaticLit registry contributions. Populated by sema when
    // LIT_WSTATIC nodes are encountered at type-arg position; held on
    // LProgram::wstatic_registry_ which is per-call. Cached here so
    // subsequent sema_lower calls can pre-seed the fresh prog's
    // registry instead of re-walking binary ASTs to rediscover them.
    std::unordered_map<uint64_t, lir::LExprPtr> wstatic_registry;

    // M5 step 5: synth tuple-struct field-name intern pool. Owns the
    // std::string objects whose string_views are stored on
    // SemaFieldInfo::name. Must outlive the cached SemaStructInfo
    // entries or those views dangle and trip "duplicate field" errors
    // on the next call.
    std::vector<std::unique_ptr<std::string>> synth_field_name_pool;
};

} // namespace logos::compiler
