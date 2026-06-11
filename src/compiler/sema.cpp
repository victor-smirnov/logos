// Logos project — https://github.com/victor-smirnov/logos
//
// Semantic analysis + L-IR lowering — core file.
//
// This file contains:
//   - Free utility functions (types_equal, type_str, types_compatible, etc.)
//   - SemaChecker::run(), init_primitives(), lookup_type_by_name()
//   - Scope/drop helpers, type-param helpers, subst_type_sema, resolve_type
//   - lower_program(), lower_module_items(), sema_lower() entry point
//
// Method bodies for collect_*, lower_expr/stmt/decl are in sema_collect.cpp,
// sema_expr.cpp, sema_stmt.cpp, sema_decl.cpp respectively.

#include "sema_impl.hpp"
#include "ctfe.hpp"

#include <logos/compiler/lir_mirror.hpp>
#include <logos/compiler/sha256.hpp>
#include <logos/compiler/sema_schema.hpp>
#include <logos/compiler/move_classify.hpp>
#include <logos/hermes/compat.hpp>
#include <logos/hermes/compat.hpp>
#include <logos/hermes/compat.hpp>
#include <logos/hermes/compat.hpp>
#include <logos/hermes/compat.hpp>
#include <logos/hermes/compat.hpp>
#include <logos/hermes/compat.hpp>
#include <logos/hermes/compat.hpp>
#include <logos/verification/assert.hpp>

#include <chrono>
#include <cstdio>
#include <format>
#include <functional>

namespace logos::compiler {

// ── TypePool PIMPL ─────────────────────────────────────────────────────────
//
// Owns the single Hermes arena that backs every interned type. Each unique
// type lives as a TinyObjectMap inside this arena; TypeRef is a fat pointer
// {arena, offset, pool} into it.
//
// Arena mode: GrowableSingleChunk so that RelativePtrs resolved against
// arena.head().data() are always valid (MultiChunk would scatter tail
// allocations into separate buffers, breaking TinyObjectMap's fixed-base
// addressing).

class TypePoolImpl {
public:
    // Owning ref to MemHolder: refcount==1 at init, dropped in dtor. Views
    // (StringView et al.) take additional refs via Own<>. The arena moves on
    // grow() — never cache base pointers; always re-fetch via holder_->base().
    hermes::MemHolder*                                     holder_ = nullptr;

    // Phase 7 lite: latch so the 3.5 GB warning fires at most once per
    // TypePool instance (TypePool::alloc is the hot path).
    bool                                                   size_warned_ = false;

    hermes::Arena&       arena()       noexcept { return holder_->arena(); }
    const hermes::Arena& arena() const noexcept { return holder_->arena(); }
    hermes::MemHolder*   holder() const noexcept { return holder_; }

    // 2c.5.4: intern table keyed by TypeUID (32-byte SHA-256-derived).
    // Bucket walk via builder_equals_typeref preserves byte-strict equality
    // (lifetime, pkg_name, lifetime_args, const_val) which TypeUID
    // intentionally collapses to match types_equal semantics.
    struct UIDHash {
        size_t operator()(const LogosType::TypeUID& u) const noexcept {
            size_t h = 0; std::memcpy(&h, u.bytes, sizeof(h)); return h;
        }
    };
    std::unordered_map<LogosType::TypeUID,
                       std::vector<hermes::arena_offset_t>, UIDHash> intern_buckets_;

    // 2c.6.6.B.6: TypeUID per offset. Populated by TypePool::alloc(); read by
    // put_sub (UID composition) and types_equal.
    std::unordered_map<hermes::arena_offset_t, LogosType::TypeUID> uid_of_;

    LogosType::TypeUID uid_of(TypeRef p) const noexcept {
        if (!p) return LogosType::TypeUID{};
        auto it = uid_of_.find(p.offset());
        return it != uid_of_.end() ? it->second : LogosType::TypeUID{};
    }

    TypeRef ref(hermes::arena_offset_t off) const noexcept {
        return TypeRef{&arena(), off, this};
    }

    TypePoolImpl(logos::InitTag& tag) {
        // hermes2 MemHolder::make returns a holder with refcount 1 (owning); the
        // GrowableSingleChunk arena is the mirror/TypePool's single segment.
        auto h = hermes::MemHolder::make(512ull * 1024 * 1024, hermes::ArenaMode::GrowableSingleChunk);
        if (!h) {
            tag.fail(std::move(h.error()));
            return;
        }
        holder_ = *h;  // initial owning reference (refcount 1)
        // Reserve offset 0 for the DocumentHeader so a zero offset reads as
        // the canonical "null" sentinel for AnyVal / RelativePtr.
        auto hdr_exp = arena().allocate_raw(sizeof(hermes::DocumentHeader),
                                            alignof(hermes::DocumentHeader));
        if (!hdr_exp) {
            tag.fail(std::move(hdr_exp.error()));
            return;
        }
        auto* hdr = static_cast<hermes::DocumentHeader*>(*hdr_exp);
        hdr->root = hermes::AnyVal{};
    }

    ~TypePoolImpl() {
        if (holder_) holder_->unref();
    }
    TypePoolImpl(const TypePoolImpl&) = delete;
    TypePoolImpl& operator=(const TypePoolImpl&) = delete;

    static std::unique_ptr<TypePoolImpl> make() {
        logos::InitTag tag;
        auto p = std::make_unique<TypePoolImpl>(tag);
        LOGOS_ASSERT(tag.ok(), "SEMA-TYPEPOOL-001",
            "TypePool Hermes arena initialisation failed");
        return p;
    }

    hermes::arena_offset_t offset_of(const void* p) const noexcept {
        auto off = static_cast<uint32_t>(
            static_cast<const uint8_t*>(p) - arena().head().data());
        return hermes::arena_offset_t{off};
    }

    hermes::TinyObjectMap* at(hermes::arena_offset_t off) noexcept {
        return reinterpret_cast<hermes::TinyObjectMap*>(
            arena().head().data() + off.value());
    }

    // Allocate an ArenaString and return an AnyVal pointing at it.
    hermes::AnyVal put_string(std::string_view s) {
        auto p = hermes::ArenaString::create(arena(), s);
        LOGOS_ASSERT(p.has_value(), "SEMA-TYPEPOOL-003", "ArenaString alloc failed");
        return hermes::AnyVal::from_offset(arena().head().data(), offset_of(*p));
    }

    // Translate a TypeRef to an AnyVal pointing at its mirror.
    hermes::AnyVal ptr_to_mirror(TypeRef p) {
        if (!p) return hermes::AnyVal{};
        return hermes::AnyVal::from_offset(arena().head().data(), p.offset());
    }

    // Build an ObjectArray from a vector<TypeRef> and return AnyVal.
    hermes::AnyVal put_type_vec(const std::vector<TypeRef>& v) {
        auto arr = hermes::ObjectArray::create(arena(), v.empty() ? 1 : v.size());
        LOGOS_ASSERT(arr.has_value(), "SEMA-TYPEPOOL-003", "ObjectArray alloc failed");
        auto arr_off = offset_of(*arr);
        for (auto elem : v) {
            auto v_any = ptr_to_mirror(elem);
            auto r = reinterpret_cast<hermes::ObjectArray*>(
                         arena().head().data() + arr_off.value())
                     ->push_back(v_any, arena());
            LOGOS_ASSERT(r.has_value(), "SEMA-TYPEPOOL-003", "ObjectArray push failed");
        }
        return hermes::AnyVal::from_offset(arena().head().data(), arr_off);
    }

    // Build an ObjectArray from a vector<std::string> (lifetime_args).
    hermes::AnyVal put_string_vec(const std::vector<std::string>& v) {
        auto arr = hermes::ObjectArray::create(arena(), v.empty() ? 1 : v.size());
        LOGOS_ASSERT(arr.has_value(), "SEMA-TYPEPOOL-003", "ObjectArray alloc failed");
        auto arr_off = offset_of(*arr);
        for (const auto& s : v) {
            auto v_any = put_string(s);
            auto r = reinterpret_cast<hermes::ObjectArray*>(
                         arena().head().data() + arr_off.value())
                     ->push_back(v_any, arena());
            LOGOS_ASSERT(r.has_value(), "SEMA-TYPEPOOL-003", "ObjectArray push failed");
        }
        return hermes::AnyVal::from_offset(arena().head().data(), arr_off);
    }

    // Build a Hermes mirror for `t` and return its arena offset.
    // Every field populated on the C++ struct is also written to the mirror
    // under the key defined in sema_schema.hpp. Reads still go through the
    // raw struct pointer — Phase 2c.3 will switch TypeRef to read the mirror.
    hermes::arena_offset_t mirror(const LogosTypeBuilder& t) {
        namespace k = sema_schema;

        // Pre-allocate all sub-values first (each may grow the arena and
        // invalidate `map`); re-fetch the map pointer before every put via
        // the at(map_off) helper.
        hermes::AnyVal v_mut_ptr, v_arr_size, v_const_val;
        hermes::AnyVal v_pointee, v_elem, v_assoc_base, v_closure_ret;
        hermes::AnyVal v_lifetime, v_arr_size_var, v_struct_name, v_enum_name;
        hermes::AnyVal v_pkg_name, v_trait_name, v_type_var_name, v_assoc_type_name;
        hermes::AnyVal v_type_args, v_tuple_elems, v_closure_params, v_gat_args;
        hermes::AnyVal v_lifetime_args;

        // mut_ptr slot: *mut vs *const (Ptr), &mut vs & (DstRef), &mut [T] vs
        // &[T] (Slice — B6/P2-11). (TraitObject's owning kind rides in const_val
        // instead — persisted below.) Persist whenever set so the read-back
        // matches the builder value when interning.
        if ((t.kind == LogosType::Kind::Ptr ||
             t.kind == LogosType::Kind::DstRef ||
             t.kind == LogosType::Kind::Slice) && t.mut_ptr) {
            v_mut_ptr = hermes::AnyVal::from_value<uint8_t>(1, hermes::type_hash::Bool);
        }
        if (t.kind == LogosType::Kind::Array && t.arr_size != 0) {
            auto av = hermes::anyval_put<uint64_t>(arena(), t.arr_size);
            LOGOS_ASSERT(av.has_value(), "SEMA-TYPEPOOL-003", "arr_size put failed");
            v_arr_size = *av;
        }
        if (t.const_val.has_value()) {
            auto av = hermes::anyval_put<int64_t>(arena(), *t.const_val);
            LOGOS_ASSERT(av.has_value(), "SEMA-TYPEPOOL-003", "const_val put failed");
            v_const_val = *av;
        }

        if (t.pointee)     v_pointee     = ptr_to_mirror(t.pointee);
        if (t.elem)        v_elem        = ptr_to_mirror(t.elem);
        if (t.assoc_base)  v_assoc_base  = ptr_to_mirror(t.assoc_base);
        if (t.closure_ret) v_closure_ret = ptr_to_mirror(t.closure_ret);

        if (!t.lifetime.empty())        v_lifetime        = put_string(t.lifetime);
        if (!t.arr_size_var.empty())    v_arr_size_var    = put_string(t.arr_size_var);
        if (!t.struct_name.empty())     v_struct_name     = put_string(t.struct_name);
        if (!t.enum_name.empty())       v_enum_name       = put_string(t.enum_name);
        if (!t.pkg_name.empty())        v_pkg_name        = put_string(t.pkg_name);
        if (!t.trait_name.empty())      v_trait_name      = put_string(t.trait_name);
        if (!t.type_var_name.empty())   v_type_var_name   = put_string(t.type_var_name);
        if (!t.assoc_type_name.empty()) v_assoc_type_name = put_string(t.assoc_type_name);

        if (!t.type_args.empty())       v_type_args       = put_type_vec(t.type_args);
        if (!t.tuple_elems.empty())     v_tuple_elems     = put_type_vec(t.tuple_elems);
        if (!t.closure_params.empty())  v_closure_params  = put_type_vec(t.closure_params);
        if (!t.gat_args.empty())        v_gat_args        = put_type_vec(t.gat_args);
        if (!t.lifetime_args.empty())   v_lifetime_args   = put_string_vec(t.lifetime_args);

        // Create the map last so it doesn't get moved around by sub-allocs
        // (the map's own grow() handles relocation internally during put()).
        auto map_exp = hermes::TinyObjectMap::create(arena(), /*capacity=*/8);
        LOGOS_ASSERT(map_exp.has_value(), "SEMA-TYPEPOOL-002",
            "TinyObjectMap allocation failed");
        hermes::arena_offset_t map_off = offset_of(*map_exp);
        (*map_exp)->set_schema_type_code(
            hermes::schema::type(int32_t(t.kind)));

        auto put = [&](const sema_schema::Key& key, hermes::AnyVal val) {
            if (val.is_null()) return;
            auto r = at(map_off)->put(key.code, val, arena());
            LOGOS_ASSERT(r.has_value(), "SEMA-TYPEPOOL-003",
                "TinyObjectMap put failed");
        };

        put(k::MUT_PTR,          v_mut_ptr);
        put(k::ARR_SIZE,         v_arr_size);
        put(k::CONST_VAL,        v_const_val);
        put(k::POINTEE,          v_pointee);
        put(k::ELEM,             v_elem);
        put(k::ASSOC_BASE,       v_assoc_base);
        put(k::CLOSURE_RET,      v_closure_ret);
        put(k::LIFETIME,         v_lifetime);
        put(k::ARR_SIZE_VAR,     v_arr_size_var);
        put(k::STRUCT_NAME,      v_struct_name);
        put(k::ENUM_NAME,        v_enum_name);
        put(k::PKG_NAME,         v_pkg_name);
        put(k::TRAIT_NAME,       v_trait_name);
        put(k::TYPE_VAR_NAME,    v_type_var_name);
        put(k::ASSOC_TYPE_NAME,  v_assoc_type_name);
        put(k::TYPE_ARGS,        v_type_args);
        put(k::TUPLE_ELEMS,      v_tuple_elems);
        put(k::CLOSURE_PARAMS,   v_closure_params);
        put(k::GAT_ARGS,         v_gat_args);
        put(k::LIFETIME_ARGS,    v_lifetime_args);

        return map_off;
    }

};

TypePool::TypePool() = default;
TypePool::~TypePool() = default;
TypePool::TypePool(TypePool&&) noexcept = default;
TypePool& TypePool::operator=(TypePool&&) noexcept = default;

// ── M5 SemaCache implementation ───────────────────────────────────────────
//
// Step 2: skeleton. Owns the shared TypePool and (in later steps) per-AST
// snapshots. SemaChecker / sema_lower consult this when wired in via
// SemaOptions::cache.
//
// M5 step 6: cached snapshot of LIR items contributed by binary-AST
// lowering. Captured once at end of the first lower_program (after the
// impl-method re-attachment pass), reused on subsequent sema_lower calls
// by splicing into prog upfront and then skipping lower_module_items for
// every binary AST. Per-AST keying was rejected because impl-method
// early-binding (sema_decl.cpp's `target_struct_tmpl->methods.push_back`)
// can mutate a struct from a *different* AST, so per-AST slices would
// lose those cross-AST methods. A single post-loop bundle captures the
// fully-assembled binary contribution.
//
// LFunction bodies are shared across LPrograms via shared_ptr<LFunction>
// (Step 6a); splice is a refcount-bump per method. Other aggregates are
// value-copied; their nested method vectors refcount-bump too.
//
// from_binary_module flag (present on LStructDef + LFunction) drives the
// per-item filter for structs/functions; for unflagged vectors (impls,
// enums, consts, ...) the capture walks per-AST [start,end) ranges
// recorded during the iter-0 loop.
struct LirBundle {
    std::vector<lir::LStructDef>      structs;
    std::vector<lir::LStructDef>      struct_specializations;
    std::vector<lir::LEnumDef>        enums;
    std::vector<lir::LFunctionPtr>    functions;
    std::vector<lir::LFunctionPtr>    specializations;
    std::vector<lir::LConst>          consts;
    std::vector<lir::LTypeAlias>      type_aliases;
    std::vector<lir::LTraitDef>       traits;
    std::vector<lir::LImplBlock>      impls;
    std::vector<lir::LInstAnnotation> inst_annotations;
    std::vector<lir::LDispatchEntry>  dispatch_entries;
    std::vector<std::pair<std::string, std::string>> module_inner_docs;
    StrSet                            reflect_requests;
    bool                              valid = false;
    // M6.1: per-vector "binary boundary" — entries [0, *_binary_end) came
    // from binary-AST ranges; entries [*_binary_end, .size()) came from
    // user-AST ranges (only ever non-zero in keep_user_state mode). Set
    // at capture by emitting binary ranges first, recording the size,
    // then emitting user ranges. reset_user_state truncates each vector
    // back to *_binary_end so the binary cache survives the user wipe.
    // structs/struct_specializations/functions/specializations don't need
    // boundary markers — they're partitioned by the from_binary_module
    // flag on the entry itself, which reset uses for filtering.
    size_t enums_binary_end = 0;
    size_t consts_binary_end = 0;
    size_t type_aliases_binary_end = 0;
    size_t traits_binary_end = 0;
    size_t impls_binary_end = 0;
    size_t inst_annotations_binary_end = 0;
    size_t dispatch_entries_binary_end = 0;
    size_t module_inner_docs_binary_end = 0;
};

class SemaCacheImpl {
public:
    // Shared across all sema_lower invocations that pass this cache.
    // Allocated lazily on first use via TypePool::alloc()'s internal init.
    TypePool shared_pool;

    // M5 step 5: refcount-holding handles to the four LIR pools. After
    // step 4 the pools are shared_ptr<vector<...>>; cache must hold a
    // ref or mono's out_ destruction would drop the last refcount and
    // free the underlying vectors — dangling cached LExpr* etc.
    std::shared_ptr<std::vector<std::unique_ptr<lir::LExpr>>>      expr_pool;
    std::shared_ptr<std::vector<std::unique_ptr<lir::LBlock>>>     block_pool;
    std::shared_ptr<std::vector<std::unique_ptr<lir::HermesVal>>>  hermes_val_pool;
    std::shared_ptr<std::vector<std::unique_ptr<lir::EClosure>>>   closure_pool;

    // M5 step 3b: persistent SemaChecker symbol tables. Moved out at end
    // of each sema_lower call, moved in at start of the next. Initially
    // null; populated after the first invocation.
    std::unique_ptr<SemaCheckerSnapshot> snapshot;

    // M5 step 6: cached LIR contribution from binary ASTs (single bundle,
    // captured after lower_program completes on the first call). Subsequent
    // calls splice the bundle into prog upfront and skip lower_module_items
    // for every binary AST.
    LirBundle lir_bundle;

    // M6.1: keep-user-state mode toggle. When true, take_snapshot skips
    // the Step 5c filter and the bundle capture skips the from_binary_module
    // filter. Driver toggles this on for the dispatch loop and off before
    // final sema; reset_user_state() also flips it off and post-hoc-filters
    // the existing snapshot+bundle to drop user content.
    bool keep_user_state = false;

    // M6.1: persisted user-key tracking. When keep_user_state=true,
    // take_snapshot copies SemaChecker's user_*_keys_ here so that a
    // subsequent reset_user_state() call can post-hoc-filter the snapshot
    // (the SemaChecker that built them is destroyed at end of run()).
    StrSet persisted_user_pkgs;
    StrSet persisted_user_module_const_keys;
    StrSet persisted_user_generic_const_keys;
    StrSet persisted_user_impl_keys;
    StrSet persisted_user_coherence_keys;
    StrSet persisted_user_assoc_type_impl_keys;
    StrSet persisted_user_assoc_const_impl_keys;
    StrSet persisted_user_trait_keys;
    StrSet persisted_user_type_alias_keys;
    StrSet persisted_user_blanket_mangled;
    std::unordered_set<const hermes::MemHolder*> persisted_user_holders;
};

SemaCache::SemaCache() : impl_(std::make_unique<SemaCacheImpl>()) {}
SemaCache::~SemaCache() = default;
SemaCache::SemaCache(SemaCache&&) noexcept = default;
SemaCache& SemaCache::operator=(SemaCache&&) noexcept = default;
TypePool& SemaCache::shared_pool() noexcept { return impl_->shared_pool; }
void SemaCache::set_keep_user_state(bool v) noexcept { impl_->keep_user_state = v; }
bool SemaCache::keep_user_state() const noexcept { return impl_->keep_user_state; }

// M6.1: post-hoc filter the snapshot/bundle to drop user content. Mirrors
// the Step 5c filter logic in take_snapshot but operates on persisted
// user_*_keys_ (saved by take_snapshot under keep_user_state=true).
void SemaCache::reset_user_state() {
    auto* c = impl_.get();
    c->keep_user_state = false;
    if (c->snapshot) {
        auto* s = c->snapshot.get();
        for (auto& k : c->persisted_user_module_const_keys) {
            s->module_consts.erase(k);
            s->module_const_values.erase(k);
        }
        for (auto& k : c->persisted_user_generic_const_keys)    s->generic_consts.erase(k);
        for (auto& k : c->persisted_user_impl_keys)             s->impls.erase(k);
        for (auto& k : c->persisted_user_coherence_keys)        s->coherence_keys.erase(k);
        for (auto& k : c->persisted_user_assoc_type_impl_keys)  s->assoc_type_impls.erase(k);
        for (auto& k : c->persisted_user_assoc_const_impl_keys) s->assoc_const_impls.erase(k);
        for (auto& k : c->persisted_user_trait_keys)            s->traits.erase(k);
        for (auto& k : c->persisted_user_type_alias_keys)       s->type_aliases.erase(k);
        if (!c->persisted_user_blanket_mangled.empty()) {
            s->blanket_impls.erase(
                std::remove_if(s->blanket_impls.begin(), s->blanket_impls.end(),
                    [&](const auto& b) {
                        return !b.mangled_name.empty() &&
                               c->persisted_user_blanket_mangled.count(b.mangled_name);
                    }),
                s->blanket_impls.end());
        }
        if (!c->persisted_user_pkgs.empty()) {
            auto erase_pkg_key = [&](auto& map) {
                for (auto it = map.begin(); it != map.end(); ) {
                    std::string_view key = it->first;
                    auto sep = key.find("::");
                    if (sep != std::string_view::npos) {
                        std::string_view pkg = key.substr(0, sep);
                        if (c->persisted_user_pkgs.find(pkg) != c->persisted_user_pkgs.end()) {
                            it = map.erase(it); continue;
                        }
                    }
                    ++it;
                }
            };
            erase_pkg_key(s->structs);
            erase_pkg_key(s->datatypes);
            erase_pkg_key(s->enums);
            erase_pkg_key(s->type_aliases);
            erase_pkg_key(s->module_consts);
            erase_pkg_key(s->module_const_values);
            erase_pkg_key(s->generic_consts);
            erase_pkg_key(s->traits);
            erase_pkg_key(s->explicit_type_codes);
            auto erase_by_pkg_field = [&](auto& map) {
                for (auto it = map.begin(); it != map.end(); ) {
                    if (c->persisted_user_pkgs.count(it->second.package))
                        it = map.erase(it);
                    else
                        ++it;
                }
            };
            erase_by_pkg_field(s->funcs);
            erase_by_pkg_field(s->generic_funcs);
            erase_by_pkg_field(s->struct_specs_sema);
            auto erase_orphan_overloads = [&](auto& overloads_map, auto& fn_map) {
                for (auto it = overloads_map.begin(); it != overloads_map.end(); ) {
                    auto& syms = it->second;
                    syms.erase(std::remove_if(syms.begin(), syms.end(),
                                              [&](const std::string& s) {
                                                  return !fn_map.count(s);
                                              }),
                               syms.end());
                    if (syms.empty()) { it = overloads_map.erase(it); continue; }
                    ++it;
                }
            };
            erase_orphan_overloads(s->func_overloads, s->funcs);
            erase_orphan_overloads(s->generic_overloads, s->generic_funcs);
            for (auto it = s->pkg_reexports.begin(); it != s->pkg_reexports.end(); ) {
                if (c->persisted_user_pkgs.count(it->first)) it = s->pkg_reexports.erase(it);
                else ++it;
            }
        }
        // Drop user holders from collected_holders.
        for (auto* h : c->persisted_user_holders) s->collected_holders.erase(h);
    }
    // Bundle: truncate each unflagged vector to its binary boundary
    // (drops user-AST-captured entries) and filter struct/fn vectors by
    // the from_binary_module flag. Bundle stays valid so the next
    // sema_lower (final user sema) reuses the binary cache via splice —
    // avoiding the ~50ms cost of re-walking binary asts.
    auto& b = c->lir_bundle;
    auto only_binary_vec = [](auto& vec, auto pred) {
        vec.erase(std::remove_if(vec.begin(), vec.end(), pred), vec.end());
    };
    only_binary_vec(b.structs, [](const lir::LStructDef& sd) {
        return !sd.from_binary_module;
    });
    only_binary_vec(b.struct_specializations, [](const lir::LStructDef& sd) {
        return !sd.from_binary_module;
    });
    only_binary_vec(b.functions, [](const lir::LFunctionPtr& fp) {
        return !fp || !fp->from_binary_module;
    });
    only_binary_vec(b.specializations, [](const lir::LFunctionPtr& fp) {
        return !fp || !fp->from_binary_module;
    });
    auto filter_methods = [](std::vector<lir::LStructDef>& v) {
        for (auto& sd : v) {
            sd.methods.erase(
                std::remove_if(sd.methods.begin(), sd.methods.end(),
                    [](const lir::LFunctionPtr& m) {
                        return !m || !m->from_binary_module;
                    }),
                sd.methods.end());
        }
    };
    filter_methods(b.structs);
    filter_methods(b.struct_specializations);
    // Unflagged vectors: truncate to the binary boundary recorded at
    // capture time. [0, *_binary_end) is binary-origin (preserved);
    // [*_binary_end, .size()) is user-origin (dropped).
    b.enums.resize(b.enums_binary_end);
    b.consts.resize(b.consts_binary_end);
    b.type_aliases.resize(b.type_aliases_binary_end);
    b.traits.resize(b.traits_binary_end);
    b.impls.resize(b.impls_binary_end);
    b.inst_annotations.resize(b.inst_annotations_binary_end);
    b.dispatch_entries.resize(b.dispatch_entries_binary_end);
    b.module_inner_docs.resize(b.module_inner_docs_binary_end);
    b.reflect_requests.clear();
    // valid stays true: next sema_lower will splice the (now binary-only)
    // bundle and skip the binary lower walk as in default mode.
    // Drop persisted user keys.
    c->persisted_user_pkgs.clear();
    c->persisted_user_module_const_keys.clear();
    c->persisted_user_generic_const_keys.clear();
    c->persisted_user_impl_keys.clear();
    c->persisted_user_coherence_keys.clear();
    c->persisted_user_assoc_type_impl_keys.clear();
    c->persisted_user_assoc_const_impl_keys.clear();
    c->persisted_user_trait_keys.clear();
    c->persisted_user_type_alias_keys.clear();
    c->persisted_user_blanket_mangled.clear();
    c->persisted_user_holders.clear();
}

// ── M5 step 3b: SemaChecker snapshot take / install ──────────────────────

std::unique_ptr<SemaCheckerSnapshot> SemaChecker::take_snapshot() {
    auto s = std::make_unique<SemaCheckerSnapshot>();
    s->structs              = std::move(structs_);
    s->datatypes            = std::move(datatypes_);
    s->struct_specs_sema    = std::move(struct_specs_sema_);
    s->explicit_type_codes  = std::move(explicit_type_codes_);
    s->enums                = std::move(enums_);
    s->funcs                = std::move(funcs_);
    s->func_overloads       = std::move(func_overloads_);
    s->generic_funcs        = std::move(generic_funcs_);
    s->generic_overloads    = std::move(generic_overloads_);
    s->type_aliases         = std::move(type_aliases_);
    s->module_consts        = std::move(module_consts_);
    s->module_const_values  = std::move(module_const_values_);
    s->generic_consts       = std::move(generic_consts_);
    s->traits               = std::move(traits_);
    s->impls                = std::move(impls_);
    s->impls_all            = std::move(impls_all_);
    s->coherence_keys       = std::move(coherence_keys_);
    s->assoc_type_impls     = std::move(assoc_type_impls_);
    s->assoc_const_impls    = std::move(assoc_const_impls_);
    s->blanket_impls        = std::move(blanket_impls_);
    // M5 step 3b: COPY (not move) metaprog_handlers — prog.metaprog_handlers
    // is moved out at the end of run() AFTER take_snapshot. Vector is small.
    // Handlers are stdlib-stable so caching across calls is safe.
    s->metaprog_handlers    = metaprog_handlers_;
    // M5 step 5c: DO NOT cache metaprog_targets — they are populated only
    // from user (non-binary) ASTs, which always re-walk every call. A cached
    // copy would re-add identical entries on install, double-firing hooks.
    // The fresh walk re-discovers them every call.
    s->copy_types           = std::move(copy_types_);
    s->pkg_reexports        = std::move(pkg_reexports_);
    s->collected_holders    = std::move(collected_holders_);
    s->synth_field_name_pool = std::move(synth_field_name_pool_);

    // M5 step 5c: drop entries owned by user packages — user ASTs are
    // re-walked on every sema_lower invocation, so caching their entries
    // would just trip "duplicate const/function/trait/..." diags on
    // re-insertion (most maps lack first-seen ODR dedup). The user_pkgs_
    // set was populated by collect at every non-binary per-AST iteration.
    // M5 step 5c: drop user-origin entries from maps whose keys don't
    // carry pkg info. Tracked at insert time in user_*_keys_ sets.
    //
    // M6.1: in keep_user_state mode (set by run_metaprog_dispatch), SKIP
    // the filter and persist the user_*_keys_ to the cache so a later
    // SemaCache::reset_user_state() can post-hoc replay the filter.
    if (cache_ && cache_->impl()->keep_user_state) {
        auto* c = cache_->impl();
        // Persist into cache (union with prior calls so dispatch loop
        // accumulates the full user-key set across all iters).
        for (auto& k : user_pkgs_) c->persisted_user_pkgs.insert(k);
        for (auto& k : user_module_const_keys_)    c->persisted_user_module_const_keys.insert(k);
        for (auto& k : user_generic_const_keys_)   c->persisted_user_generic_const_keys.insert(k);
        for (auto& k : user_impl_keys_)            c->persisted_user_impl_keys.insert(k);
        for (auto& k : user_coherence_keys_)       c->persisted_user_coherence_keys.insert(k);
        for (auto& k : user_assoc_type_impl_keys_) c->persisted_user_assoc_type_impl_keys.insert(k);
        for (auto& k : user_assoc_const_impl_keys_)c->persisted_user_assoc_const_impl_keys.insert(k);
        for (auto& k : user_trait_keys_)           c->persisted_user_trait_keys.insert(k);
        for (auto& k : user_type_alias_keys_)      c->persisted_user_type_alias_keys.insert(k);
        for (auto& k : user_blanket_mangled_)      c->persisted_user_blanket_mangled.insert(k);
        // collected_holders user-portion: in keep_user_state mode collect
        // adds all (binary + user) holders to the set; we record which ones
        // are user-origin here so reset_user_state can drop them.
        for (auto* h : user_holders_)              c->persisted_user_holders.insert(h);
        return s;  // skip the filter below
    }
    for (auto& k : user_module_const_keys_) {
        s->module_consts.erase(k);
        s->module_const_values.erase(k);
    }
    for (auto& k : user_generic_const_keys_)      s->generic_consts.erase(k);
    for (auto& k : user_impl_keys_)               s->impls.erase(k);
    for (auto& k : user_coherence_keys_)          s->coherence_keys.erase(k);
    for (auto& k : user_assoc_type_impl_keys_)    s->assoc_type_impls.erase(k);
    for (auto& k : user_assoc_const_impl_keys_)   s->assoc_const_impls.erase(k);
    for (auto& k : user_trait_keys_)              s->traits.erase(k);
    for (auto& k : user_type_alias_keys_)         s->type_aliases.erase(k);
    // blanket_impls_ — vector; drop entries whose mangled_name was tagged
    // as user-origin (regular blankets use real mangled names; satisfaction
    // markers carry a synthetic "$marker$..." name set at insertion time).
    if (!user_blanket_mangled_.empty()) {
        s->blanket_impls.erase(
            std::remove_if(s->blanket_impls.begin(), s->blanket_impls.end(),
                [&](const SemaChecker::BlanketImpl& b) {
                    return !b.mangled_name.empty() &&
                           user_blanket_mangled_.count(b.mangled_name);
                }),
            s->blanket_impls.end());
    }
    if (!user_pkgs_.empty()) {
        // Map keyed by `sema_key(pkg, name)` = "pkg::name" — strip if
        // the pkg prefix matches a user pkg. Use std::string_view for
        // the prefix-compare to avoid per-entry string allocation; on
        // big stdlib maps (1000+ entries) this is the difference
        // between ~0ms and several ms per call.
        auto erase_pkg_key = [&](auto& map) {
            for (auto it = map.begin(); it != map.end(); ) {
                std::string_view key = it->first;
                auto sep = key.find("::");
                if (sep != std::string_view::npos) {
                    std::string_view pkg = key.substr(0, sep);
                    // user_pkgs_ is StrSet (StringHash w/ transparent); look
                    // up by string_view without materialising a std::string.
                    if (user_pkgs_.find(pkg) != user_pkgs_.end()) {
                        it = map.erase(it); continue;
                    }
                }
                ++it;
            }
        };
        erase_pkg_key(s->structs);
        erase_pkg_key(s->datatypes);
        erase_pkg_key(s->enums);
        erase_pkg_key(s->type_aliases);
        erase_pkg_key(s->module_consts);
        erase_pkg_key(s->module_const_values);
        erase_pkg_key(s->generic_consts);
        erase_pkg_key(s->traits);
        erase_pkg_key(s->explicit_type_codes);

        // Map valued by SemaFuncInfo / SemaStructInfo with .package field
        // — check the value's package against user_pkgs_.
        auto erase_by_pkg_field = [&](auto& map) {
            for (auto it = map.begin(); it != map.end(); ) {
                if (user_pkgs_.count(it->second.package)) {
                    it = map.erase(it);
                } else {
                    ++it;
                }
            }
        };
        erase_by_pkg_field(s->funcs);
        erase_by_pkg_field(s->generic_funcs);
        erase_by_pkg_field(s->struct_specs_sema);

        // func_overloads_ / generic_overloads_ are bare-name → vector<sym_name>.
        // Drop overload symbol_names that point into funcs/generic_funcs that
        // we just erased. Empty overload lists are erased entirely.
        auto erase_orphan_overloads = [&](auto& overloads_map, auto& fn_map) {
            for (auto it = overloads_map.begin(); it != overloads_map.end(); ) {
                auto& syms = it->second;
                syms.erase(std::remove_if(syms.begin(), syms.end(),
                                          [&](const std::string& s) {
                                              return !fn_map.count(s);
                                          }),
                           syms.end());
                if (syms.empty()) { it = overloads_map.erase(it); continue; }
                ++it;
            }
        };
        erase_orphan_overloads(s->func_overloads, s->funcs);
        erase_orphan_overloads(s->generic_overloads, s->generic_funcs);

        // Drop user pkgs from pkg_reexports.
        for (auto it = s->pkg_reexports.begin(); it != s->pkg_reexports.end(); ) {
            if (user_pkgs_.count(it->first)) it = s->pkg_reexports.erase(it);
            else ++it;
        }

        // impls_ / assoc_*_impls_ / coherence_keys_ keys are
        // "Trait::Target[::Name]". Target name doesn't carry pkg
        // directly, and impls_ overwrites on re-insert (`m[key] = v;`
        // not first-seen). Left unfiltered — if duplicate-impl diags
        // surface, add a .package field to SemaImplInfo + filter here.
        //
        // blanket_impls / metaprog_handlers / copy_types / impls_ etc.
        // are left as-is; either stdlib-stable (handlers) or use
        // overwrite-on-conflict semantics (impls_).
    }
    return s;
}

void SemaChecker::install_snapshot(std::unique_ptr<SemaCheckerSnapshot> s) {
    if (!s) return;
    structs_              = std::move(s->structs);
    datatypes_            = std::move(s->datatypes);
    struct_specs_sema_    = std::move(s->struct_specs_sema);
    explicit_type_codes_  = std::move(s->explicit_type_codes);
    enums_                = std::move(s->enums);
    funcs_                = std::move(s->funcs);
    func_overloads_       = std::move(s->func_overloads);
    generic_funcs_        = std::move(s->generic_funcs);
    generic_overloads_    = std::move(s->generic_overloads);
    type_aliases_         = std::move(s->type_aliases);
    module_consts_        = std::move(s->module_consts);
    module_const_values_  = std::move(s->module_const_values);
    generic_consts_       = std::move(s->generic_consts);
    traits_               = std::move(s->traits);
    impls_                = std::move(s->impls);
    impls_all_            = std::move(s->impls_all);
    coherence_keys_       = std::move(s->coherence_keys);
    assoc_type_impls_     = std::move(s->assoc_type_impls);
    assoc_const_impls_    = std::move(s->assoc_const_impls);
    blanket_impls_        = std::move(s->blanket_impls);
    metaprog_handlers_    = std::move(s->metaprog_handlers);
    metaprog_targets_     = std::move(s->metaprog_targets);
    copy_types_           = std::move(s->copy_types);
    pkg_reexports_        = std::move(s->pkg_reexports);
    collected_holders_    = std::move(s->collected_holders);
    synth_field_name_pool_ = std::move(s->synth_field_name_pool);
}

// ── Canonical structural hash ──
// 2c.5.4: canonical TypeUID computation.
//
// Layout per master plan: byte 0 = kind tag, bytes 1..23 = SHA-256 trim of
// canonical structural serialization (lifetime ignored, matches types_equal),
// bytes 24..31 = reserved member-id (0 for pure types).
//
// Serialization composes sub-type UIDs (already computed bottom-up) so the
// induction "sub-uid equality ⇔ sub-types-equal" closes the same way the
// previous u64 hash did.
namespace {

inline void put_byte(std::string& s, uint8_t b) { s.push_back(char(b)); }
inline void put_u64(std::string& s, uint64_t v) {
    for (int i = 0; i < 8; ++i) { s.push_back(char(v & 0xFF)); v >>= 8; }
}
inline void put_str(std::string& s, std::string_view v) {
    put_u64(s, v.size());
    s.append(v);
}
inline void put_sub(std::string& s, const TypePoolImpl* impl, TypeRef p) {
    if (!p) { for (int i = 0; i < 32; ++i) s.push_back(0); return; }
    auto uid = impl ? impl->uid_of(p) : LogosType::TypeUID{};
    s.append(reinterpret_cast<const char*>(uid.bytes), 32);
}

LogosType::TypeUID compute_type_uid(const TypePoolImpl* impl,
                                    const LogosTypeBuilder& t) noexcept {
    std::string buf;
    buf.reserve(64);
    put_byte(buf, uint8_t(t.kind));
    using K = LogosType::Kind;
    switch (t.kind) {
    case K::Ptr:
        put_byte(buf, t.mut_ptr ? 1 : 0);
        // const_val bit 0 = `*zoned T` (F3) — a zoned raw pointer must intern
        // DISTINCTLY from a plain `*T`, else the dedup collapses them and whichever
        // is interned first wins (the `*zoned T` loses its flag intermittently,
        // by interning order). Mirrors DstRef/TraitObject hashing const_val.
        put_u64(buf, uint64_t(t.const_val.value_or(0)));
        put_sub(buf, impl, t.pointee);
        break;
    case K::Ref:
    case K::MutRef:
        // lifetime intentionally omitted — matches types_equal semantics.
        put_sub(buf, impl, t.pointee);
        break;
    case K::Array:
        put_u64(buf, t.arr_size);
        put_str(buf, t.arr_size_var);
        put_sub(buf, impl, t.elem);
        break;
    case K::Struct:
    case K::ZonedStruct:
        put_str(buf, t.pkg_name);
        put_str(buf, t.struct_name);
        for (auto a : t.type_args) put_sub(buf, impl, a);
        break;
    case K::Enum:
        put_str(buf, t.pkg_name);
        put_str(buf, t.enum_name);
        for (auto a : t.type_args) put_sub(buf, impl, a);
        break;
    case K::Tuple:
        for (auto e : t.tuple_elems) put_sub(buf, impl, e);
        break;
    case K::Slice:
        put_byte(buf, t.mut_ptr ? 1 : 0);
        // const_val carries the owning kind (Borrow vs Box) — an owning
        // `Box<[T]>` slice interns distinctly from a borrowed `&[T]`.
        put_byte(buf, (uint8_t)(t.const_val.value_or(0)));
        put_sub(buf, impl, t.elem);
        break;
    case K::UnsizedSlice:
        put_sub(buf, impl, t.elem);
        break;
    case K::UnsizedDyn:
        put_str(buf, t.trait_name);
        for (auto a : t.type_args) put_sub(buf, impl, a);
        break;
    case K::DstRef:
        put_str(buf, t.pkg_name);
        put_str(buf, t.struct_name);
        put_byte(buf, t.mut_ptr ? 1 : 0);
        // const_val = owning kind (Borrow vs Box) — an owning `Box<Foo>` custom-
        // DST interns distinctly from a borrowed `&Foo`.
        put_byte(buf, (uint8_t)(t.const_val.value_or(0)));
        for (auto a : t.type_args) put_sub(buf, impl, a);
        break;
    case K::Closure:
    case K::FnPtr:
        for (auto p : t.closure_params) put_sub(buf, impl, p);
        put_sub(buf, impl, t.closure_ret);
        break;
    case K::FnItem:
        // logos-core 1.4: distinct instantiations of the same fn must intern
        // to distinct TypeUIDs even when the FnPtr signature collapses (e.g.
        // `marker<T>() -> i32` with unused T). `struct_name` carries the
        // fn's symbol name; type_args (turbofish) further refines identity.
        put_str(buf, t.struct_name);
        for (auto a : t.type_args) put_sub(buf, impl, a);
        for (auto p : t.closure_params) put_sub(buf, impl, p);
        put_sub(buf, impl, t.closure_ret);
        break;
    case K::TraitObject:
        // const_val carries the owning kind (Borrow/Box/Rc/Arc) in the LOW
        // byte; logos-core 2.4(c) adds bit 8 = `+ Send` and bit 9 = `+ Sync`
        // auto-trait bounds. Hash the FULL u64 so `&dyn T` and `&dyn T + Send`
        // get distinct TypeUIDs.
        put_u64(buf, uint64_t(t.const_val.value_or(0)));
        put_str(buf, t.trait_name);
        for (auto a : t.type_args) put_sub(buf, impl, a);
        break;
    case K::TaggedPtr:
        put_str(buf, t.trait_name);
        break;
    case K::ImplTrait:
        put_str(buf, t.struct_name);
        break;
    case K::TypeVar:
    case K::ConstVar:
        put_str(buf, t.type_var_name);
        break;
    case K::AssocType:
        put_str(buf, t.trait_name);
        put_str(buf, t.assoc_type_name);
        put_sub(buf, impl, t.assoc_base);
        for (auto a : t.gat_args) put_sub(buf, impl, a);
        break;
    case K::IntLit:
        // const_val distinguishes IntLit instances — the type pool dedupes
        // by UID, so without this two `{integer}` types with different
        // const_val would collapse and lose the value (notably breaks the
        // sizeof-pack array-size path which materialises IntLit(N) via
        // pool->alloc to feed subst_type_sema).
        put_u64(buf, uint64_t(t.const_val.value_or(0)));
        break;
    case K::HStaticLit:
        // Identity = the byte-hash stashed in const_val. Without this,
        // two distinct `Foo::<@{...}>` instantiations would dedupe to the
        // same TypeRef and collapse the configuration.
        put_u64(buf, uint64_t(t.const_val.value_or(0)));
        break;
    case K::CfgSlotType:
        // Identity = (cfg-typevar-name, slot-key). Reuses type_var_name +
        // assoc_type_name as carrier fields; both must contribute or
        // distinct slots collapse to one interned TypeRef.
        put_str(buf, t.type_var_name);
        put_str(buf, t.assoc_type_name);
        break;
    default:
        break;  // primitives: kind tag alone identifies
    }

    auto sha = sha256(buf);
    LogosType::TypeUID uid{};
    uid.bytes[0] = uint8_t(t.kind);
    std::memcpy(&uid.bytes[1], sha.data(), 23);
    // bytes[24..31] left zero (reserved for future member-id / dispatch).
    return uid;
}

// Hash adapter for unordered_map keying — first 8 bytes of the (already
// well-distributed) SHA-256 trim are sufficient.
struct TypeUIDHash {
    size_t operator()(const LogosType::TypeUID& u) const noexcept {
        size_t h = 0;
        std::memcpy(&h, u.bytes, sizeof(h));
        return h;
    }
};

} // namespace

// 2c.5.2b: byte-strict structural compare between a fresh builder and an
// already-interned TypeRef. Sub-types compared by ptr-equality (interning is
// bottom-up, so all sub-types in `b` are canonical and `t` was constructed
// from canonical sub-types as well). Distinguishes lifetime (borrow_check
// reads .lifetime() off the ptr), Enum type_args, and TypeVar names — fields
// types_equal collapses but consumers read directly.
namespace {

bool vec_ptr_eq(const std::vector<TypeRef>& a,
                const std::vector<TypeRef>& b) noexcept {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) if (a[i] != b[i]) return false;
    return true;
}

bool builder_equals_typeref(const LogosTypeBuilder& t, TypeRef r) noexcept {
    if (!r) return false;
    if (t.kind != r.kind()) return false;
    using K = LogosType::Kind;
    switch (t.kind) {
    case K::Ptr:
        return t.mut_ptr == r.mut_ptr() && t.pointee == r.pointee();
    case K::Ref:
    case K::MutRef:
        return t.pointee == r.pointee() &&
               t.lifetime == r.lifetime();
    case K::Array:
        return t.arr_size == r.arr_size() &&
               t.arr_size_var == r.arr_size_var() &&
               t.elem == r.elem();
    case K::Struct:
    case K::ZonedStruct:
        return t.struct_name == r.struct_name() &&
               t.pkg_name == r.pkg_name() &&
               vec_ptr_eq(t.type_args, r.type_args()) &&
               t.lifetime_args == r.lifetime_args();
    case K::Enum:
        return t.enum_name == r.enum_name() &&
               t.pkg_name == r.pkg_name() &&
               vec_ptr_eq(t.type_args, r.type_args()) &&
               t.lifetime_args == r.lifetime_args();
    case K::Tuple:
        return vec_ptr_eq(t.tuple_elems, r.tuple_elems());
    case K::Slice:
        // B6/P2-11: `&mut [T]` and `&[T]` are distinct (mutability in identity);
        // without this the type-pool dedups a fresh `&mut [T]` into an existing
        // `&[T]` and the mut bit is silently lost. const_val = owning kind so an
        // owning `Box<[T]>` slice is distinct from a borrowed `&[T]`.
        return t.elem == r.elem() && t.mut_ptr == r.mut_ptr() &&
               t.const_val == r.const_val();
    case K::UnsizedSlice:
        return t.elem == r.elem();
    case K::UnsizedDyn:
        return t.trait_name == r.trait_name() &&
               vec_ptr_eq(t.type_args, r.type_args());
    case K::DstRef:
        return t.struct_name == r.struct_name() &&
               t.pkg_name == r.pkg_name() &&
               t.mut_ptr == r.mut_ptr() &&
               t.const_val == r.const_val() &&   // owning kind (Borrow/Box)
               vec_ptr_eq(t.type_args, r.type_args());
    case K::Closure:
    case K::FnPtr:
        return vec_ptr_eq(t.closure_params, r.closure_params()) &&
               t.closure_ret == r.closure_ret();
    case K::FnItem:
        // logos-core 1.4: FnItem equality = same fn (struct_name) + same
        // type-args + same signature. Two distinct fns with the same FnPtr
        // sig get DIFFERENT FnItems; distinct instantiations of one generic
        // fn similarly differ.
        return t.struct_name == r.struct_name() &&
               vec_ptr_eq(t.type_args, r.type_args()) &&
               vec_ptr_eq(t.closure_params, r.closure_params()) &&
               t.closure_ret == r.closure_ret();
    case K::TraitObject:
        return t.const_val == r.const_val() &&  // owning kind (Borrow/Box/Rc/Arc)
               t.trait_name == r.trait_name() &&
               vec_ptr_eq(t.type_args, r.type_args());
    case K::TaggedPtr:
        return t.trait_name == r.trait_name();
    case K::ImplTrait:
        return t.struct_name == r.struct_name();
    case K::TypeVar:
    case K::ConstVar:
        return t.type_var_name == r.type_var_name() &&
               t.const_val == r.const_val();
    case K::AssocType:
        return t.trait_name == r.trait_name() &&
               t.assoc_type_name == r.assoc_type_name() &&
               t.assoc_base == r.assoc_base() &&
               vec_ptr_eq(t.gat_args, r.gat_args()) &&
               t.lifetime_args == r.lifetime_args();  // B88
    case K::CfgSlotType:
        return t.type_var_name == r.type_var_name() &&
               t.assoc_type_name == r.assoc_type_name();
    default:
        return true;  // primitives — kind alone is identity
    }
}

} // namespace

hermes::Arena* TypePool::arena() noexcept {
    return impl_ ? &impl_->arena() : nullptr;
}
const hermes::Arena* TypePool::arena() const noexcept {
    return impl_ ? &impl_->arena() : nullptr;
}
hermes::Arena& TypePool::arena_or_init() {
    if (!impl_) impl_ = TypePoolImpl::make();
    return impl_->arena();
}
hermes::MemHolder* TypePool::holder() noexcept {
    return impl_ ? impl_->holder() : nullptr;
}
LogosType::TypeUID TypePool::uid_of(TypeRef t) const noexcept {
    return impl_ ? impl_->uid_of(t) : LogosType::TypeUID{};
}

TypeRef TypePool::alloc(LogosTypeBuilder t) {
    if (!impl_) impl_ = TypePoolImpl::make();
    // Multi-arena IR Phase 5.B step 3: a builder can carry foreign TypeRef
    // fields (e.g. produced by subst_type when an Array's element is a
    // Struct that didn't itself need substitution). Their offsets are
    // meaningless in this pool's arena and would be written into the
    // mirror as-is by impl_->mirror(), garbling later reads. Localize
    // every child TypeRef before computing the UID + interning so the
    // resulting type is self-consistent.
    if (t.pointee     && t.pointee.is_external())     t.pointee     = intern_foreign(t.pointee);
    if (t.elem        && t.elem.is_external())        t.elem        = intern_foreign(t.elem);
    if (t.assoc_base  && t.assoc_base.is_external())  t.assoc_base  = intern_foreign(t.assoc_base);
    if (t.closure_ret && t.closure_ret.is_external()) t.closure_ret = intern_foreign(t.closure_ret);
    for (auto& a : t.type_args)      if (a && a.is_external()) a = intern_foreign(a);
    for (auto& e : t.tuple_elems)    if (e && e.is_external()) e = intern_foreign(e);
    for (auto& p : t.closure_params) if (p && p.is_external()) p = intern_foreign(p);
    for (auto& g : t.gat_args)       if (g && g.is_external()) g = intern_foreign(g);

    LogosType::TypeUID uid = compute_type_uid(impl_.get(), t);
    auto& bucket = impl_->intern_buckets_[uid];
    for (auto cand_off : bucket) {
        TypeRef cand = impl_->ref(cand_off);
        if (builder_equals_typeref(t, cand)) return cand;
    }

    auto off = impl_->mirror(t);
    impl_->uid_of_[off] = uid;
    bucket.push_back(off);

    // Phase 7 lite — arena size monitoring. The Hermes arena has a hard
    // 4 GB ceiling (32-bit offsets). Before rolling multi-arena lands
    // (full Phase 7), give the user a heads-up at 3.5 GB and a clear
    // diagnostic + abort at 3.9 GB instead of a mid-emit hermes OOM.
    // The warning latches via TypePoolImpl::size_warned_ to avoid
    // spamming the hot path. Override thresholds via env for testing.
    {
        const uint64_t MB = uint64_t{1024} * 1024;
        uint64_t warn_at = 3500 * MB;
        uint64_t err_at  = 3900 * MB;
        if (const char* w = std::getenv("LOGOS_ARENA_WARN_MB"))
            warn_at = std::strtoull(w, nullptr, 10) * MB;
        if (const char* e = std::getenv("LOGOS_ARENA_ERR_MB"))
            err_at  = std::strtoull(e, nullptr, 10) * MB;
        uint64_t used = impl_->arena().head().used;
        if (used > err_at) {
            std::fprintf(stderr,
                "FATAL: TypePool arena reached %llu MB (limit %llu MB). The\n"
                "Hermes arena has a 4 GB hard ceiling (32-bit offsets). The\n"
                "compiler does not yet support rolling to a new arena\n"
                "mid-emit (Phase 7 of the multi-arena IR refactor). To\n"
                "continue, split the module into smaller compilation units\n"
                "or ship parts as separate `logos.module`s.\n",
                (unsigned long long)(used / MB),
                (unsigned long long)(err_at / MB));
            std::abort();
        }
        if (used > warn_at && !impl_->size_warned_) {
            std::fprintf(stderr,
                "WARNING: TypePool arena past %llu MB (current %llu MB,\n"
                "         hard 4 GB ceiling). Approaching the Hermes\n"
                "         arena limit — split the module or wait for\n"
                "         multi-arena IR Phase 7 (rolling arenas).\n",
                (unsigned long long)(warn_at / MB),
                (unsigned long long)(used / MB));
            impl_->size_warned_ = true;
        }
    }

    return impl_->ref(off);
}

TypeRef TypePool::intern_foreign(TypeRef tv) {
    if (!tv || !tv.is_external()) return tv;
    // alloc() now recursively localizes every TypeRef field in the
    // builder; to_builder + alloc is enough.
    return alloc(tv.to_builder());
}

// ── TypeRef pointer-valued accessors (Phase 2c.4d.0) ───────────────────────
//
// These cross-check the mirror's AnyVal pointee offset against the source
// struct field via TypePoolImpl's inverse map. The struct field is still
// what we return; the check confirms the mirror stays consistent under
// real workloads before 2c.4d flips reads to the mirror.
namespace {

// 2c.4e.3.1: accessors source from TypeRef's fat-pointer fields directly.
//
// Phase 2.B (multi-arena IR): when the child AnyVal points at an ExternalRef
// object, dispatch through ArenaPool to the target arena. The returned
// TypeRef carries the target arena_id; pool_ defaults to nullptr (target
// arena's TypePoolImpl isn't yet wired through ArenaPool — Phase 2.C). All
// read-only accessors on TypeRef (kind, names, type_args, etc.) still work
// against the target arena's bytes; pool-dependent paths (trait dispatch
// out-of-line strings) need explicit pool plumbing — see TypeRef::pool()
// docs for the limitation.
TypeRef ptr_via_mirror(const TypeRef& self, sema_schema::Key key) {
    if (!self) return {};
    auto av = self.mirror()->get(key.code);
    if (av.is_null()) return {};

    // Single-arena fast path: AnyVal points at a normal mirror node in the
    // same arena. This branch covers ~100% of current single-arena work.
    if (!hermes::is_external_ref_av(av)) [[likely]] {
        if (self.pool()) {
            return self.pool()->ref(av.to_offset(self.mirror_base()));
        }
        // Self is already cross-arena (pool=nullptr). Chain into the same
        // foreign arena: same arena bytes, same arena_id, no local pool.
        // Phase 5.B step 3: needed so subst_type can chase pointee/elem on a
        // TypeRef whose root came from a foreign mirror.
        return TypeRef(self.arena(), av,
                       /*pool=*/nullptr, self.arena_id());
    }

    // Cross-arena dispatch: AnyVal points at an ExternalRef object; resolve
    // via global ArenaPool. Returned TypeRef has pool_ = nullptr — caller
    // gets read-only access; further pool-dependent accessors degrade
    // gracefully (return null StringView etc.).
    auto* ref = reinterpret_cast<const hermes::ExternalRef*>(
        av.resolve());
    auto r = hermes::resolve_external_ref(*ref);
    if (!r.ok()) return {};
    return TypeRef(&r.mem->arena(), r.offset(), /*pool=*/nullptr, ref->arena_id());
}

}  // namespace

TypeRef TypeRef::pointee()     const noexcept { return ptr_via_mirror(*this, sema_schema::POINTEE);     }
TypeRef TypeRef::elem()        const noexcept { return ptr_via_mirror(*this, sema_schema::ELEM);        }
TypeRef TypeRef::assoc_base()  const noexcept { return ptr_via_mirror(*this, sema_schema::ASSOC_BASE);  }
TypeRef TypeRef::closure_ret() const noexcept { return ptr_via_mirror(*this, sema_schema::CLOSURE_RET); }

// String accessors return realloc-safe owning views. The MemHolder is reached
// via pool_ for local TypeRefs; for cross-arena TypeRefs (pool_ == nullptr,
// arena_id_ valid) it is looked up from the global ArenaPool. Both paths
// yield a working StringView; only fully synthetic TypeRefs (no pool, no
// arena_id) degrade to a null view.
namespace {
hermes::MemHolder* holder_for(const TypeRef& self) noexcept {
    if (self.pool()) return self.pool()->holder();
    if (self.arena_id().is_valid()) {
        return hermes::global_arena_pool().get(self.arena_id());
    }
    return nullptr;
}
hermes::StringView ostr_via_mirror(const TypeRef& self,
                                    sema_schema::Key key) noexcept {
    if (!self) return {};
    auto* holder = holder_for(self);
    if (!holder) return {};
    auto av = self.mirror()->get(key.code);
    if (av.is_null()) return {};
    return hermes::StringView(av, holder);
}
} // namespace

hermes::StringView TypeRef::lifetime()        const noexcept { return ostr_via_mirror(*this, sema_schema::LIFETIME);        }
hermes::StringView TypeRef::struct_name()     const noexcept { return ostr_via_mirror(*this, sema_schema::STRUCT_NAME);     }
hermes::StringView TypeRef::enum_name()       const noexcept { return ostr_via_mirror(*this, sema_schema::ENUM_NAME);       }
hermes::StringView TypeRef::pkg_name()        const noexcept { return ostr_via_mirror(*this, sema_schema::PKG_NAME);        }
hermes::StringView TypeRef::trait_name()      const noexcept { return ostr_via_mirror(*this, sema_schema::TRAIT_NAME);      }
hermes::StringView TypeRef::type_var_name()   const noexcept { return ostr_via_mirror(*this, sema_schema::TYPE_VAR_NAME);   }
hermes::StringView TypeRef::assoc_type_name() const noexcept { return ostr_via_mirror(*this, sema_schema::ASSOC_TYPE_NAME); }
hermes::StringView TypeRef::arr_size_var()    const noexcept { return ostr_via_mirror(*this, sema_schema::ARR_SIZE_VAR);    }

// 2c.4e.3.0/.1: vector accessors via mirror ObjectArray, sourced from
// TypeRef's base/off/pool fat pointer.
//
// Phase 5.B step 3: cross-arena TypeRefs (pool=nullptr, arena_id valid) chain
// element types into the same foreign arena instead of crashing on
// pool()->ref(). Elements stay non-pool-bound; downstream read accessors
// keep working (kind, names, type_args via the arena_id fallback in
// holder_for).
namespace {
std::vector<TypeRef> type_vec_via_mirror(const TypeRef& self,
                                          sema_schema::Key key) {
    std::vector<TypeRef> result;
    if (!self) return result;
    auto* base = self.mirror_base();
    auto av = self.mirror()->get(key.code);
    if (av.is_null()) return result;
    auto* arr = av.as_ptr<const hermes::ObjectArray>();
    result.reserve(arr->size());
    for (uint64_t i = 0; i < arr->size(); ++i) {
        auto e = const_cast<hermes::ObjectArray*>(arr)->get(i);
        if (self.pool()) {
            result.push_back(self.pool()->ref(e.to_offset(self.mirror_base())));
        } else {
            result.push_back(TypeRef(self.arena(), e,
                                     /*pool=*/nullptr, self.arena_id()));
        }
    }
    return result;
}
std::vector<std::string> string_vec_via_mirror(const TypeRef& self,
                                                sema_schema::Key key) {
    std::vector<std::string> result;
    if (!self) return result;
    auto* base = self.mirror_base();
    auto av = self.mirror()->get(key.code);
    if (av.is_null()) return result;
    auto* arr = av.as_ptr<const hermes::ObjectArray>();
    result.reserve(arr->size());
    for (uint64_t i = 0; i < arr->size(); ++i) {
        auto e = const_cast<hermes::ObjectArray*>(arr)->get(i);
        auto* s = e.as_ptr<const hermes::ArenaString>();
        result.emplace_back(s->view());
    }
    return result;
}
}  // namespace

std::vector<TypeRef> TypeRef::type_args()      const noexcept { return type_vec_via_mirror(*this, sema_schema::TYPE_ARGS); }
std::vector<TypeRef> TypeRef::tuple_elems()    const noexcept { return type_vec_via_mirror(*this, sema_schema::TUPLE_ELEMS); }
std::vector<TypeRef> TypeRef::closure_params() const noexcept { return type_vec_via_mirror(*this, sema_schema::CLOSURE_PARAMS); }
std::vector<TypeRef> TypeRef::gat_args()       const noexcept { return type_vec_via_mirror(*this, sema_schema::GAT_ARGS); }
std::vector<std::string> TypeRef::lifetime_args()  const noexcept { return string_vec_via_mirror(*this, sema_schema::LIFETIME_ARGS); }

// Reconstruct a LogosTypeBuilder from a TypeRef by reading every field
// through the mirror accessors. Callers use this when they want to
// copy-and-mutate an interned type (e.g. mono substitution).
LogosTypeBuilder TypeRef::to_builder() const {
    LogosTypeBuilder b;
    if (!*this) return b;
    b.kind            = kind();
    b.mut_ptr         = mut_ptr();
    b.pointee         = pointee();
    b.lifetime        = std::string(lifetime());
    b.elem            = elem();
    b.arr_size        = arr_size();
    b.arr_size_var    = std::string(arr_size_var());
    b.struct_name     = std::string(struct_name());
    b.enum_name       = std::string(enum_name());
    b.pkg_name        = std::string(pkg_name());
    b.type_args       = type_args();
    b.lifetime_args   = lifetime_args();
    b.tuple_elems     = tuple_elems();
    b.closure_params  = closure_params();
    b.closure_ret     = closure_ret();
    b.trait_name      = std::string(trait_name());
    b.type_var_name   = std::string(type_var_name());
    b.assoc_base      = assoc_base();
    b.assoc_type_name = std::string(assoc_type_name());
    b.gat_args        = gat_args();
    b.const_val       = const_val();
    return b;
}

namespace la = ast;
using hermes::TinyMapView;
using hermes::ArrayView;
using hermes::StringView;
using hermes::AnyVal;
using hermes::MemHolder;

// ── types_equal ─────────────────────────────────────────────────────────────

// 2c.5.3: post-interning, types_equal collapses to ptr-or-hash compare.
//
// Invariant after 2c.5.2b: every LogosType* comes from the interned pool;
// two distinct ptrs are byte-strict different. Hash buckets can only hold
// distinct ptrs that differ in fields types_equal ignores (lifetime, pkg_name,
// lifetime_args, const_val). Therefore within the pool:
//   hash_eq ⇒ types_equal   (and ptr_eq trivially ⇒ types_equal).
//
// Sub-types are interned bottom-up so sub-hash equality ⇒ sub-types-equal,
// closing the inductive step for Closure/FnPtr/Ref/etc.
bool types_equal(TypeRef a, TypeRef b) noexcept {
    if (!a || !b) return false;
    if (a == b) return true;
    auto* pa = a.pool();
    auto* pb = b.pool();
    if (!pa || pa != pb) return false;
    return pa->uid_of(a) == pa->uid_of(b);
}

// ── Generic struct name helpers ───────────────────────────────────────────────

static std::string mangle_type_for_name(TypeRef t);

// G149-6: when set (only during a fn-ptr-impl method's signature mangling),
// a fn-pointer type mangles to its arity-only `$fnptr$<n>` form so the symbol
// is stable across the impl's TypeVars A,B,C. Toggled by function_signature_key.
static bool g_mangle_erase_fnptr = false;

std::string concrete_struct_name(TypeRef t) {
    if (!t || (TypeRef(t).kind() != LogosType::Kind::Struct &&
               TypeRef(t).kind() != LogosType::Kind::ZonedStruct)) return {};
    std::string base(TypeRef(t).struct_name());
    if (!TypeRef(t).type_args().empty()) {
        base += "$G";
        base += std::to_string(TypeRef(t).type_args().size());
        for (auto a : TypeRef(t).type_args()) { base += "$"; base += mangle_type_for_name(a); }
    }
    return base;
}

std::string concrete_struct_name_raw(std::string_view base_name,
                                     const std::vector<TypeRef>& type_args) {
    if (type_args.empty()) return std::string(base_name);
    std::string r(base_name);
    r += "$G";
    r += std::to_string(type_args.size());
    for (auto a : type_args) { r += "$"; r += mangle_type_for_name(a); }
    return r;
}

static std::string mangle_type_for_name(TypeRef t) {
    if (!t) return "null";
    switch (TypeRef(t).kind()) {
    case LogosType::Kind::Ptr:
        return (TypeRef(t).mut_ptr() ? "pmut_" : "pcst_") + mangle_type_for_name(TypeRef(t).pointee());
    case LogosType::Kind::Ref:
        return "ref_" + mangle_type_for_name(TypeRef(t).pointee());
    case LogosType::Kind::MutRef:
        return "refmut_" + mangle_type_for_name(TypeRef(t).pointee());
    case LogosType::Kind::Array:
        return "arr" + std::to_string(TypeRef(t).arr_size()) + "_" + mangle_type_for_name(TypeRef(t).elem());
    case LogosType::Kind::Struct:
    case LogosType::Kind::ZonedStruct:
        return concrete_struct_name(t);
    case LogosType::Kind::Tuple: {
        std::string r = "tup$" + std::to_string(TypeRef(t).tuple_elems().size());
        for (auto e : TypeRef(t).tuple_elems()) { r += "$"; r += mangle_type_for_name(e); }
        return r;
    }
    case LogosType::Kind::Slice:
        return "slice_" + mangle_type_for_name(TypeRef(t).elem());
    case LogosType::Kind::UnsizedSlice:
        return "uslice_" + mangle_type_for_name(TypeRef(t).elem());
    case LogosType::Kind::UnsizedDyn:
        return "udyn_" + std::string(TypeRef(t).trait_name());
    case LogosType::Kind::DstRef:
        return (TypeRef(t).mut_ptr() ? "dstmutref_" : "dstref_") +
               std::string(TypeRef(t).struct_name());
    case LogosType::Kind::AssocType:
        return mangle_type_for_name(TypeRef(t).assoc_base()) + "::" + std::string(TypeRef(t).assoc_type_name());
    case LogosType::Kind::HStaticLit: {
        // hs_<hex64>: identity-stable suffix for HermesStatic value used as
        // const-generic argument. Two `@{...}` literals with identical bytes
        // hash to the same const_val and therefore the same suffix.
        char buf[24];
        std::snprintf(buf, sizeof(buf), "hs_%016llx",
                      (unsigned long long)(uint64_t)(TypeRef(t).const_val().value_or(0)));
        return std::string(buf);
    }
    case LogosType::Kind::FnItem:
    case LogosType::Kind::FnPtr:
        // G149-6: erase fn-ptr Self in a fn-ptr-impl method signature to its
        // arity-only form so the symbol is stable across the impl's A,B,C.
        // Outside that context, keep the full type_str (distinct fn-ptr-value
        // params must stay distinguishable). FnItem (logos-core 1.4) mangles
        // identically — it's a ZST that auto-coerces to FnPtr at value
        // positions, so symbol-level identity follows the FnPtr signature.
        if (g_mangle_erase_fnptr)
            return "$fnptr$" + std::to_string(TypeRef(t).closure_params().size());
        return type_str(t);
    case LogosType::Kind::TraitObject:
        // Distinguish an OWNING Box<dyn T> from a borrowed &dyn T in the
        // mangled type name so a generic struct instance like Vec<Box<dyn T>>
        // and Vec<&dyn T> get DISTINCT spec names — otherwise they collapse to
        // one struct spec and the element type-var binds to the borrow form,
        // losing owning-ness (→ element never dropped, leak). Borrowed &dyn
        // keeps its historical type_str mangling.
        if (TypeRef(t).owning_trait_object()) {
            std::string r = "owndyn_" + std::string(TypeRef(t).trait_name());
            for (auto a : TypeRef(t).type_args()) { r += "$"; r += mangle_type_for_name(a); }
            return r;
        }
        return type_str(t);
    default:
        return type_str(t);  // primitives / TypeVar / Enum already valid identifiers
    }
}

std::string SemaChecker::canonical_func_type_name(TypeRef t) const {
    return mangle_type_for_name(t);
}

std::string SemaChecker::function_signature_key(std::string_view base_name,
                                                const std::vector<TypeRef>& param_types,
                                                bool is_vararg) const {
    // G149-6: a fn-ptr-impl method (`$fnptr$N__method`) has a fn-pointer `Self`
    // carrying the impl's TypeVars (`fn(A,B)->C`). Erase such fn-ptr params to
    // their arity-only `$fnptr$<n>` form so the symbol is stable across A,B,C
    // (one emission per arity) and matches the call site's `$fnptr$N` cname.
    bool erase = base_name.rfind("$fnptr$", 0) == 0;
    bool saved = g_mangle_erase_fnptr;
    g_mangle_erase_fnptr = erase;
    std::string key(base_name);
    for (auto pt : param_types) {
        key += "__";
        key += canonical_func_type_name(pt);
    }
    if (param_types.empty()) key += "__void";
    if (is_vararg) key += "__vararg";
    g_mangle_erase_fnptr = saved;
    return key;
}

std::string SemaChecker::function_symbol_name(std::string_view base_name,
                                             const SemaChecker::SemaFuncInfo& info) const {
    // Pkg-qualified mangling: `pkg$base__f__sig` (or `__g__` for generic).
    // Two packages defining `error(msg: str)` get distinct symbols
    // (`std.log$error__f__str` vs `std.compiler.metaprog$error__f__str`)
    // so they coexist in funcs_, in user `.o`, and at link time.
    //
    // Carve-outs that stay bare:
    //   - extern fns: ABI symbols (malloc, printf) must keep their C name.
    //   - struct methods (base_name contains `__`): already disambiguated
    //     by their struct's pkg-qualified name in mlir_gen.
    bool is_method = base_name.find("__") != std::string_view::npos;
    bool with_pkg  = !info.package.empty() && !info.is_extern;

    std::string key = function_signature_key(base_name, info.param_types, info.is_vararg);
    auto suffix = key.substr(std::string(base_name).size() + 2);
    std::string out;
    if (with_pkg) {
        out = info.package;
        out += is_method ? '.' : '$';
    }
    out += std::string(base_name);
    out += info.type_params.empty() ? "__f__" : "__g__";
    out += suffix;
    return out;
}

const SemaChecker::SemaFuncInfo* SemaChecker::find_func_by_symbol(std::string_view symbol) const {
    if (auto it = funcs_.find(std::string(symbol)); it != funcs_.end())
        return &it->second;
    if (auto it = generic_funcs_.find(std::string(symbol)); it != generic_funcs_.end())
        return &it->second;
    return nullptr;
}

const SemaChecker::SemaFuncInfo* SemaChecker::find_generic_func(std::string_view base_name) const {
    if (auto git = generic_overloads_.find(std::string(base_name)); git != generic_overloads_.end()) {
        for (auto& sym : git->second) {
            auto fit = generic_funcs_.find(sym);
            if (fit != generic_funcs_.end())
                return &fit->second;
        }
    }
    return nullptr;
}

const SemaChecker::SemaFuncInfo* SemaChecker::find_generic_func(std::string_view base_name,
                                                                size_t n_args) const {
    const SemaFuncInfo* fallback = nullptr;
    const SemaFuncInfo* first_arity = nullptr;
    if (auto git = generic_overloads_.find(std::string(base_name)); git != generic_overloads_.end()) {
        for (auto& sym : git->second) {
            auto fit = generic_funcs_.find(sym);
            if (fit == generic_funcs_.end()) continue;
            auto& fi = fit->second;
            bool arity_ok = fi.is_vararg ? n_args >= fi.param_types.size()
                                         : fi.param_types.size() == n_args;
            if (!arity_ok) { if (!fallback) fallback = &fi; continue; }
            if (!cur_package_.empty() && fi.package == cur_package_) return &fi;
            if (!first_arity) first_arity = &fi;
        }
    }
    if (first_arity) return first_arity;
    return fallback;
}

const SemaChecker::SemaFuncInfo* SemaChecker::find_func_by_base_and_signature(
        std::string_view base_name,
        const std::vector<TypeRef>& param_types,
        bool is_vararg) const {
    for (auto* fi : find_func_candidates(base_name)) {
        if (fi->is_vararg != is_vararg) continue;
        if (fi->param_types.size() != param_types.size()) continue;
        bool same = true;
        for (size_t i = 0; i < param_types.size(); ++i) {
            if (!fi->param_types[i] || !param_types[i]) { same = false; break; }
            if (types_equal(fi->param_types[i], param_types[i])) continue;
            // logos-core 1.4: arg-side FnItem auto-coerces to FnPtr at the
            // method-call signature lookup, just like types_compatible's
            // FnItem→FnPtr rule. Without this, every method that takes
            // `f: fn() -> R` parameter type rejects bare fn-name args
            // (which now type as FnItem) — the exact-signature lookup
            // path bypasses the regular coerce.
            if (param_types[i].kind() == LogosType::Kind::FnItem &&
                fi->param_types[i].kind() == LogosType::Kind::FnPtr &&
                types_compatible(param_types[i], fi->param_types[i]))
                continue;
            same = false; break;
        }
        if (same) return fi;
    }
    return nullptr;
}

std::vector<const SemaChecker::SemaFuncInfo*> SemaChecker::find_func_candidates(std::string_view base_name) const {
    std::vector<const SemaChecker::SemaFuncInfo*> all;
    if (auto it = func_overloads_.find(std::string(base_name)); it != func_overloads_.end()) {
        for (auto& sym : it->second) {
            auto fit = funcs_.find(sym);
            if (fit != funcs_.end()) all.push_back(&fit->second);
        }
    } else {
        auto fit = funcs_.find(std::string(base_name));
        if (fit != funcs_.end() && fit->second.source_file.size())
            all.push_back(&fit->second);
    }
    if (auto git = generic_overloads_.find(std::string(base_name)); git != generic_overloads_.end()) {
        for (auto& sym : git->second) {
            auto fit = generic_funcs_.find(sym);
            if (fit != generic_funcs_.end()) all.push_back(&fit->second);
        }
    }
    // Visibility filter: under pkg-qualified mangling two packages can
    // define the same base+sig fn. The user's call site sees only fns
    // reachable through cur_package_ (own pkg) or cur_imports_ (use
    // pkg;). Empty pkg (extern fns / prelude) stay visible.
    // Fallback: if filtering would leave nothing, return everything —
    // sema-internal lookups during synthetic phases (metaprog stubs,
    // mono pre-image) may run before cur_imports_ is primed.
    std::vector<const SemaChecker::SemaFuncInfo*> out;
    out.reserve(all.size());
    for (auto* fi : all) {
        if (fi->package.empty() ||
            fi->package == cur_package_ ||
            std::find(cur_imports_.wildcard_packages.begin(),
                      cur_imports_.wildcard_packages.end(),
                      fi->package) != cur_imports_.wildcard_packages.end()) {
            out.push_back(fi);
        }
    }
    if (out.empty()) return all;
    return out;
}

bool SemaChecker::is_divergent_call_node(hermes::TinyMapView node) {
    int32_t cc = code_of(node);
    if (cc != la::CALL.code && cc != la::FN_MACRO_CALL.code) return false;
    auto callee = str_of(node.get(la::CALLEE.code));
    // `panic` is a stdlib macro that wraps `__fmt_panic` — its registered
    // signature returns `!` once resolved. But the macro form parses to
    // FN_MACRO_CALL "panic" before expansion (reachability sees the
    // un-expanded AST), and depending on import order the user-facing
    // `panic` symbol may not be visible yet at the call site. Keep the
    // name fast-path as an anchor for the macro shape; the generic
    // Never-return check below handles every other diverging callee.
    // §6.11 marker-macros (unreachable!/todo!/unimplemented!) lower
    // through `panic!` in `lower_builtin_macro`, so they're handled
    // by the `panic` fast-path indirectly.
    if (callee == "panic") return true;
    for (auto* fi : find_func_candidates(std::string(callee)))
        if (fi && fi->ret_type &&
            TypeRef(fi->ret_type).kind() == LogosType::Kind::Never)
            return true;
    return false;
}

const SemaChecker::SemaFuncInfo* SemaChecker::resolve_function_call(
        std::string_view base_name,
        const std::vector<lir::LExprPtr>& arg_exprs,
        bool allow_generic,
        bool exact_only) const {
    const SemaChecker::SemaFuncInfo* best = nullptr;
    int best_score = -1;
    bool ambiguous = false;

    auto candidates = find_func_candidates(base_name);
    for (auto* fi : candidates) {
        if (!fi || fi->type_params.size() || fi->source_file.empty()) continue;
        bool arity_ok = fi->is_vararg ? arg_exprs.size() >= fi->param_types.size()
                                      : arg_exprs.size() == fi->param_types.size();
        if (!arity_ok) continue;

        int score = 0;
        bool ok = true;
        for (size_t i = 0; i < fi->param_types.size(); ++i) {
            auto at = arg_exprs[i] ? arg_exprs[i]->type : nullptr;
            auto pt = fi->param_types[i];
            if (!at || !pt) { ok = false; break; }
            if (types_equal(at, pt)) score = std::max(score, 2);
            else if (!exact_only && types_compatible(at, pt)) score = std::max(score, 1);
            else { ok = false; break; }
        }
        if (!ok) continue;
        if (score > best_score) {
            best = fi;
            best_score = score;
            ambiguous = false;
        } else if (score == best_score && best_score != -1) {
            ambiguous = true;
        }
    }

    if (ambiguous) {
        // Tiebreaker: prefer a candidate whose package matches cur_package_.
        // Local definition shadows imported same-named pub fn (Rust/C++ rule).
        const SemaFuncInfo* local = nullptr;
        bool local_ambiguous = false;
        for (auto* fi : candidates) {
            if (!fi || fi->type_params.size() || fi->source_file.empty()) continue;
            if (fi->package != cur_package_) continue;
            bool arity_ok = fi->is_vararg ? arg_exprs.size() >= fi->param_types.size()
                                          : arg_exprs.size() == fi->param_types.size();
            if (!arity_ok) continue;
            int score = 0;
            bool ok = true;
            for (size_t i = 0; i < fi->param_types.size(); ++i) {
                auto at = arg_exprs[i] ? arg_exprs[i]->type : nullptr;
                auto pt = fi->param_types[i];
                if (!at || !pt) { ok = false; break; }
                if (types_equal(at, pt)) score = std::max(score, 2);
                else if (!exact_only && types_compatible(at, pt)) score = std::max(score, 1);
                else { ok = false; break; }
            }
            if (!ok) continue;
            if (score == best_score) {
                if (local) local_ambiguous = true;
                local = fi;
            }
        }
        if (local && !local_ambiguous) return local;
        const_cast<SemaChecker*>(this)->error(std::format("ambiguous call to '{}'", base_name));
        return nullptr;
    }
    if (best || !allow_generic) return best;
    return nullptr;
}

// ── types_compatible ─────────────────────────────────────────────────────────

bool types_compatible(TypeRef from, TypeRef to) noexcept {
    if (!from || !to) return false;
    if (types_equal(from, to)) return true;
    // logos-core 1.4: FnItem auto-coerces to FnPtr at every value-use site
    // (call arg, let-binding, return, etc.). Two FnItems with identical
    // FnPtr signatures intern distinctly (different fn identity), so a
    // single `let x: fn(i32) -> i32 = some_fn;` would otherwise reject —
    // accept the coerce when source is FnItem and target is FnPtr of the
    // matching signature. The reverse direction (FnPtr → FnItem) is NOT
    // accepted: a bare fn-ptr value can't be retroactively assigned an
    // identity. Same-FnItem types are caught by the types_equal fast path
    // above; same-sig DIFFERENT FnItems are NOT compatible here (the
    // `if c { foo::<i32> } else { foo::<u32> }` merge needs both to
    // coerce to a common FnPtr — handled at the if/match merge site
    // outside types_compatible).
    // FnItem → FnPtr ONLY (not FnItem → FnItem). Same-FnItem types are
    // caught by types_equal above; two DIFFERENT FnItems with the same
    // signature must NOT collapse — that's the distinction logos-core
    // 1.4 brings.
    if (from.kind() == LogosType::Kind::FnItem &&
        to.kind() == LogosType::Kind::FnPtr) {
        auto fp = from.closure_params();
        auto tp = to.closure_params();
        if (fp.size() != tp.size()) return false;
        for (size_t i = 0; i < fp.size(); ++i)
            if (!types_compatible(fp[i], tp[i])) return false;
        if (from.closure_ret() && to.closure_ret() &&
            !types_compatible(from.closure_ret(), to.closure_ret())) return false;
        return true;
    }
    // The never type `!` is a subtype of every type: a diverging expression
    // (return / break / continue / panic / `-> !` call) coerces to whatever
    // the context expects. Only ONE direction is valid — `Never → T` — since
    // `!` is empty. The reverse (`T → Never`) used to be accepted here as
    // "harmless because Never never yields a real value", but that admitted
    // unsound shapes (e.g. a value-returning expr quietly typed `!`,
    // suppressing exhaustiveness or divergence checks downstream). Rust
    // rejects `T → !`; we do too now (logos-core item 1.1).
    if (from.kind() == LogosType::Kind::Never) return true;
    // `_` placeholder unifies with anything in either direction —
    // resolution happens via the surrounding annotation/RHS unifier
    // (logos-core 1.3).
    if (from.kind() == LogosType::Kind::InferredType ||
        to.kind()   == LogosType::Kind::InferredType) return true;
    // logos-core 1.3 (nested): Struct-vs-Struct with the same base
    // name and arity is compatible iff every pair of type-args is
    // compatible. Enables `let v: Vec<_> = vec_new::<i32>();` —
    // `Vec<i32>` and `Vec<_>` aren't `types_equal` (different
    // TypeUIDs), but the element-wise rule lets `i32` match `_`.
    if (from.kind() == LogosType::Kind::Struct &&
        to.kind()   == LogosType::Kind::Struct &&
        from.struct_name() == to.struct_name() &&
        from.pkg_name()    == to.pkg_name()) {
        auto fa = from.type_args();
        auto ta = to.type_args();
        if (fa.size() == ta.size()) {
            for (size_t i = 0; i < fa.size(); ++i)
                if (!types_compatible(fa[i], ta[i])) goto struct_mismatch;
            return true;
        }
    }
    struct_mismatch:;
    if (from.kind() == LogosType::Kind::IntLit && is_integer_kind(to.kind())) return true;
    if (from.kind() == LogosType::Kind::IntLit && to.kind() == LogosType::Kind::TypeVar) return true;
    if (from.kind() == LogosType::Kind::IntLit &&
        (to.kind() == LogosType::Kind::F32 || to.kind() == LogosType::Kind::F64)) return true;
    if (from.kind() == LogosType::Kind::FloatLit &&
        (to.kind() == LogosType::Kind::F32 || to.kind() == LogosType::Kind::F64 ||
         to.kind() == LogosType::Kind::TypeVar)) return true;
    // Cfg-slot types are deferred placeholders for HermesStatic-bound
    // primitives. Treat them like TypeVar at sema for coercion checks:
    // any concrete numeric (and IntLit/FloatLit) compatible-with the
    // resolved primitive — mono enforces the resolved-type compatibility
    // when STORE_CFG is substituted. Flowing in BOTH directions so both
    // `slot_typed = 0u64` and `u64_typed = slot_value` checks pass.
    if (from.kind() == LogosType::Kind::IntLit && to.kind() == LogosType::Kind::CfgSlotType) return true;
    if (from.kind() == LogosType::Kind::FloatLit && to.kind() == LogosType::Kind::CfgSlotType) return true;
    if (from.kind() == LogosType::Kind::CfgSlotType || to.kind() == LogosType::Kind::CfgSlotType) {
        if (is_integer_kind(from.kind()) || is_integer_kind(to.kind())) return true;
        if (from.kind() == LogosType::Kind::F32 || from.kind() == LogosType::Kind::F64 ||
            to.kind() == LogosType::Kind::F32 || to.kind() == LogosType::Kind::F64) return true;
    }
    if (from.kind() == LogosType::Kind::Enum   && is_integer_kind(to.kind())) return true;
    // NOTE: implicit `int → enum` is intentionally NOT allowed (Rust requires an
    // explicit cast / variant). Permitting it made a data/niche enum (e.g. HAny)
    // a spurious overload candidate for an integer arg — `push(7i64)` resolved to
    // `push(HAny)`, reinterpreting the integer as the by-pointer enum's storage
    // pointer → UB. Explicit `as` casts go through the cast path, not here.
    // Safe implicit integer widening (e.g. u32 → i64, i32 → i64, u8 → u32).
    // Value preservation guaranteed; signed→unsigned never allowed here.
    if (can_widen_int(from.kind(), to.kind())) return true;
    if (from.kind() == LogosType::Kind::Array &&
        to.kind() == LogosType::Kind::Ptr   &&
        from.elem() && to.pointee())
        return types_equal(from.elem(), to.pointee());
    // Arrays are compatible if same size and elements are compatible (handles nested arrays).
    if (TypeRef(from).kind() == LogosType::Kind::Array && TypeRef(to).kind() == LogosType::Kind::Array &&
        TypeRef(from).arr_size() == TypeRef(to).arr_size() && TypeRef(from).elem() && TypeRef(to).elem())
        return types_compatible(TypeRef(from).elem(), TypeRef(to).elem());
    if (from.kind() == LogosType::Kind::Slice && to.kind() == LogosType::Kind::Slice && from.elem() && to.elem()) {
        if (!from.mut_ptr() && to.mut_ptr()) return false;
        return types_compatible(from.elem(), to.elem());
    }
    // Tuple: element-wise compatibility (e.g. ({integer}, {integer}) → (i32, i32))
    if (TypeRef(from).kind() == LogosType::Kind::Tuple && TypeRef(to).kind() == LogosType::Kind::Tuple) {
        if (TypeRef(from).tuple_elems().size() != TypeRef(to).tuple_elems().size()) return false;
        for (size_t i = 0; i < TypeRef(from).tuple_elems().size(); ++i)
            if (!types_compatible(TypeRef(from).tuple_elems()[i], TypeRef(to).tuple_elems()[i])) return false;
        return true;
    }
    // C5-cl-04 slice: `&Closure → Closure` / `&mut Closure → Closure`. Users
    // typically spell the boxable-closure surface as `&dyn FnMut(…)`, which
    // sema resolves to bare Closure (since `dyn Fn*` IS already fat-ptr-like
    // — the `&` carries no extra meaning). To make `take_ref(&cl)` type-check
    // against `f: &dyn FnMut(…)` (parsed as `Closure`), accept a reference
    // over a Closure as a Closure value.
    if (TypeRef(to).kind() == LogosType::Kind::Closure &&
        (TypeRef(from).kind() == LogosType::Kind::Ref ||
         TypeRef(from).kind() == LogosType::Kind::MutRef) &&
        TypeRef(from).pointee() &&
        TypeRef(from).pointee().kind() == LogosType::Kind::Closure)
        return types_compatible(TypeRef(from).pointee(), to);
    // Struct → &dyn Trait coercion (impl check deferred to codegen).
    // Also accept `&T` / `&mut T` over a struct (the natural unsize-coercion
    // source form): `foo(&b)` where `foo` expects `&dyn Trait` and `b: T`.
    if (TypeRef(to).kind() == LogosType::Kind::TraitObject &&
        (TypeRef(from).kind() == LogosType::Kind::Struct ||
         (TypeRef(from).kind() == LogosType::Kind::Ptr && TypeRef(from).pointee()) ||
         ((TypeRef(from).kind() == LogosType::Kind::Ref ||
           TypeRef(from).kind() == LogosType::Kind::MutRef) &&
          TypeRef(from).pointee() &&
          TypeRef(from).pointee().kind() == LogosType::Kind::Struct)))
        return true;
    // Array-to-pointer decay: `&[T; N]` / `&mut [T; N]` → `*const T` / `*mut T`
    // (and `&T` / `&mut T`). The reference's runtime value IS the array base
    // pointer, which equals a pointer to element 0, so the decay is a no-op at
    // the LLVM level (no retype/codegen needed — both flow as `ptr`). This
    // restores the historical `&mut arr` thin-pointer form as a coercion now
    // that `&mut arr` is lowered to the precise `&mut [T; N]` (G162-2). Must
    // not WIDEN mutability: a shared `&[T; N]` only decays to a const/shared
    // element pointer.
    if ((from.kind() == LogosType::Kind::Ref || from.kind() == LogosType::Kind::MutRef) &&
        from.pointee() && from.pointee().kind() == LogosType::Kind::Array &&
        from.pointee().elem()) {
        bool from_mut = from.kind() == LogosType::Kind::MutRef;
        TypeRef aelem = from.pointee().elem();
        if (to.kind() == LogosType::Kind::Ptr && to.pointee() &&
            (from_mut || !to.mut_ptr()))
            return types_compatible(aelem, to.pointee());
        if ((to.kind() == LogosType::Kind::Ref || to.kind() == LogosType::Kind::MutRef) &&
            to.pointee() &&
            (from_mut || to.kind() == LogosType::Kind::Ref))
            return types_compatible(aelem, to.pointee());
    }
    // &T / &mut T → *const T / *mut T coercions (for backward compat with existing raw-ptr code)
    if ((TypeRef(from).kind() == LogosType::Kind::Ref || TypeRef(from).kind() == LogosType::Kind::MutRef) &&
        TypeRef(to).kind() == LogosType::Kind::Ptr &&
        TypeRef(from).pointee() && TypeRef(to).pointee())
        return types_compatible(TypeRef(from).pointee(), TypeRef(to).pointee());
    // *const T / *mut T → &T (reverse coercion — less safe but needed for existing code)
    if (TypeRef(from).kind() == LogosType::Kind::Ptr &&
        (TypeRef(to).kind() == LogosType::Kind::Ref || TypeRef(to).kind() == LogosType::Kind::MutRef) &&
        TypeRef(from).pointee() && TypeRef(to).pointee())
        return types_compatible(TypeRef(from).pointee(), TypeRef(to).pointee());
    // &mut T → &T coercion (shared ref from exclusive ref)
    if (TypeRef(from).kind() == LogosType::Kind::MutRef && TypeRef(to).kind() == LogosType::Kind::Ref &&
        TypeRef(from).pointee() && TypeRef(to).pointee())
        return types_compatible(TypeRef(from).pointee(), TypeRef(to).pointee());
    // B3-bg-06: `&Vec<T> → &[T]` / `&mut Vec<T> → &[T]` Deref-like coercion.
    // In Logos `&[T]` is the Slice fat-pointer type itself (NOT Ref<Slice>),
    // so the from side is `Ref<Vec<T>>` / `MutRef<Vec<T>>` and the to side
    // is `Slice<T>`. Vec's `{ptr, len, cap}` layout has `{ptr, len}` (the
    // slice fat-pointer) as a prefix, so at the LLVM level the pointer is
    // reused verbatim — no runtime conversion needed. Hardcoded to the
    // stdlib Vec struct; full `Deref` trait surface is the longer path.
    if ((TypeRef(from).kind() == LogosType::Kind::Ref || TypeRef(from).kind() == LogosType::Kind::MutRef) &&
        TypeRef(to).kind() == LogosType::Kind::Slice &&
        TypeRef(from).pointee() &&
        TypeRef(from).pointee().kind() == LogosType::Kind::Struct &&
        TypeRef(from).pointee().struct_name() == "Vec" &&
        !TypeRef(from).pointee().type_args().empty() &&
        TypeRef(to).elem())
        return types_compatible(TypeRef(from).pointee().type_args()[0],
                                TypeRef(to).elem());
    // &T → &T and &mut T → &mut T with compatible pointees (e.g. &{integer} → &i32)
    if (TypeRef(from).kind() == LogosType::Kind::Ref && TypeRef(to).kind() == LogosType::Kind::Ref &&
        TypeRef(from).pointee() && TypeRef(to).pointee())
        return types_compatible(TypeRef(from).pointee(), TypeRef(to).pointee());
    if (TypeRef(from).kind() == LogosType::Kind::MutRef && TypeRef(to).kind() == LogosType::Kind::MutRef &&
        TypeRef(from).pointee() && TypeRef(to).pointee())
        return types_compatible(TypeRef(from).pointee(), TypeRef(to).pointee());
    // *mut T → *const T coercion (dropping write permission is always safe).
    if (TypeRef(from).kind() == LogosType::Kind::Ptr && TypeRef(to).kind() == LogosType::Kind::Ptr &&
        TypeRef(from).mut_ptr() && !TypeRef(to).mut_ptr() &&
        TypeRef(from).pointee() && TypeRef(to).pointee())
        return types_compatible(TypeRef(from).pointee(), TypeRef(to).pointee());
    // *const u8 (or any *T) → &tagged<TS> Trait coercion.
    // &tagged<TS> Trait is a thin pointer to a tagged object.  The caller passes
    // a raw *const u8 and the compiler reads the tag at dispatch time.
    if (TypeRef(to).kind() == LogosType::Kind::TaggedPtr && TypeRef(from).kind() == LogosType::Kind::Ptr)
        return true;
    // Class pointer covariance: *mut/const Derived is compatible with *const/mut Class
    // (same class — exact equality handled above; hierarchy checked in SemaChecker::compat)
    return false;
}

// ── type_str ─────────────────────────────────────────────────────────────────

std::string type_str(TypeRef t) {
    if (!t) return "<null>";
    switch (TypeRef(t).kind()) {
    case LogosType::Kind::Void:   return "void";
    case LogosType::Kind::I32:    return "i32";
    case LogosType::Kind::I64:    return "i64";
    case LogosType::Kind::F64:    return "f64";
    case LogosType::Kind::F32:    return "f32";
    case LogosType::Kind::Bool:   return "bool";
    case LogosType::Kind::U8:     return "u8";
    case LogosType::Kind::I8:     return "i8";
    case LogosType::Kind::I16:    return "i16";
    case LogosType::Kind::U16:    return "u16";
    case LogosType::Kind::I24:    return "i24";
    case LogosType::Kind::U24:    return "u24";
    case LogosType::Kind::I56:    return "i56";
    case LogosType::Kind::U56:    return "u56";
    case LogosType::Kind::U32:    return "u32";
    case LogosType::Kind::U64:    return "u64";
    case LogosType::Kind::I128:   return "i128";
    case LogosType::Kind::U128:   return "u128";
    case LogosType::Kind::Usize:  return "usize";
    case LogosType::Kind::Isize:  return "isize";
    case LogosType::Kind::Char:   return "char";
    case LogosType::Kind::IntLit:   return "{integer}";
    case LogosType::Kind::HStaticLit: {
        char buf[24];
        std::snprintf(buf, sizeof(buf), "@hs_%016llx",
                      (unsigned long long)(uint64_t)(TypeRef(t).const_val().value_or(0)));
        return std::string(buf);
    }
    case LogosType::Kind::FloatLit: return "{float}";
    case LogosType::Kind::Ptr:
        return std::string(TypeRef(t).zoned_ptr() ? (TypeRef(t).mut_ptr() ? "*zoned mut " : "*zoned ")
                                                  : (TypeRef(t).mut_ptr() ? "*mut " : "*const "))
             + type_str(TypeRef(t).pointee());
    case LogosType::Kind::Ref: {
        std::string s = "&";
        if (!TypeRef(t).lifetime().empty()) { s.append(TypeRef(t).lifetime()); s += " "; }
        return s + type_str(TypeRef(t).pointee());
    }
    case LogosType::Kind::MutRef: {
        std::string s = "&";
        if (!TypeRef(t).lifetime().empty()) { s.append(TypeRef(t).lifetime()); s += " "; }
        return s + "mut " + type_str(TypeRef(t).pointee());
    }
    case LogosType::Kind::Array:
        return std::format("[{}; {}]", type_str(TypeRef(t).elem()), TypeRef(t).arr_size());
    case LogosType::Kind::Struct:
    case LogosType::Kind::ZonedStruct:
        if (TypeRef(t).type_args().empty() && TypeRef(t).lifetime_args().empty()) return std::string(TypeRef(t).struct_name());
        { std::string r = std::string(TypeRef(t).struct_name()) + "<";
          bool first = true;
          for (auto& lt : TypeRef(t).lifetime_args()) {
              if (!first) r += ", "; first = false;
              r += lt;
          }
          for (size_t i = 0; i < TypeRef(t).type_args().size(); ++i) {
              if (!first) r += ", "; first = false;
              r += type_str(TypeRef(t).type_args()[i]);
          }
          return r + ">"; }
    case LogosType::Kind::Tuple: {
        std::string r = "(";
        for (size_t i = 0; i < TypeRef(t).tuple_elems().size(); ++i) {
            if (i) r += ", ";
            r += type_str(TypeRef(t).tuple_elems()[i]);
        }
        return r + ")"; }
    case LogosType::Kind::Slice:
        return std::format("&{}[{}]", TypeRef(t).mut_ptr() ? "mut " : "",
                           type_str(TypeRef(t).elem()));
    case LogosType::Kind::UnsizedSlice:
        return std::format("[{}]", type_str(TypeRef(t).elem()));
    case LogosType::Kind::UnsizedDyn:
        return std::format("dyn {}", TypeRef(t).trait_name());
    case LogosType::Kind::DstRef: {
        std::string s = TypeRef(t).mut_ptr() ? "&mut " : "&";
        s += TypeRef(t).struct_name();
        auto args = TypeRef(t).type_args();
        if (!args.empty()) {
            s += "<";
            for (size_t i = 0; i < args.size(); ++i) {
                if (i) s += ", ";
                s += type_str(args[i]);
            }
            s += ">";
        }
        return s;
    }
    case LogosType::Kind::Closure: {
        std::string r = "|";
        for (size_t i = 0; i < TypeRef(t).closure_params().size(); ++i) {
            if (i) r += ", ";
            r += type_str(TypeRef(t).closure_params()[i]);
        }
        r += "| -> ";
        r += type_str(TypeRef(t).closure_ret());
        return r; }
    case LogosType::Kind::FnPtr: {
        std::string r = "fn(";
        for (size_t i = 0; i < TypeRef(t).closure_params().size(); ++i) {
            if (i) r += ", ";
            r += type_str(TypeRef(t).closure_params()[i]);
        }
        r += ") -> ";
        r += type_str(TypeRef(t).closure_ret());
        return r; }
    case LogosType::Kind::FnItem: {
        // logos-core 1.4: render as `fn ITEM<name>(args) -> ret` — the
        // leading "fn ITEM<name>" makes it visually distinct from a bare
        // FnPtr in diagnostics. Auto-coerces to FnPtr at every use site,
        // so the user-facing surface still reads as FnPtr most of the
        // time; this form only surfaces when the FnItem identity itself
        // is a type error.
        std::string r = "fn ITEM<";
        r += std::string(TypeRef(t).struct_name());
        if (!TypeRef(t).type_args().empty()) {
            r += "::<";
            for (size_t i = 0; i < TypeRef(t).type_args().size(); ++i) {
                if (i) r += ", ";
                r += type_str(TypeRef(t).type_args()[i]);
            }
            r += ">";
        }
        r += ">(";
        for (size_t i = 0; i < TypeRef(t).closure_params().size(); ++i) {
            if (i) r += ", ";
            r += type_str(TypeRef(t).closure_params()[i]);
        }
        r += ") -> ";
        r += type_str(TypeRef(t).closure_ret());
        return r; }
    case LogosType::Kind::Enum:        return std::string(TypeRef(t).enum_name());
    case LogosType::Kind::TraitObject: {
        std::string r = "&dyn " + std::string(TypeRef(t).trait_name());
        auto ta = TypeRef(t).type_args();
        if (!ta.empty()) {
            r += "<";
            for (size_t i = 0; i < ta.size(); ++i) {
                if (i) r += ", ";
                r += type_str(ta[i]);
            }
            r += ">";
        }
        return r; }
    case LogosType::Kind::TaggedPtr:   return "&tagged<" + std::string(TypeRef(t).struct_name()) + "> " + std::string(TypeRef(t).trait_name());
    case LogosType::Kind::TypeVar:     return std::string(TypeRef(t).type_var_name());
    case LogosType::Kind::ConstVar:    return std::string(TypeRef(t).type_var_name());
    case LogosType::Kind::AssocType: {
        std::string r = type_str(TypeRef(t).assoc_base()) + "::" + std::string(TypeRef(t).assoc_type_name());
        if (!TypeRef(t).gat_args().empty()) {
            r += "<";
            for (size_t i = 0; i < TypeRef(t).gat_args().size(); ++i) {
                if (i) r += ", ";
                r += type_str(TypeRef(t).gat_args()[i]);
            }
            r += ">";
        }
        return r;
    }
    case LogosType::Kind::ImplTrait:   return "impl " + std::string(TypeRef(t).struct_name());
    case LogosType::Kind::Generic:     return "generic " + std::string(TypeRef(t).struct_name());
    case LogosType::Kind::CfgSlotType: return "<cfg-slot-type>";
    case LogosType::Kind::Error:       return "<error>";
    case LogosType::Kind::Never:       return "!";
    case LogosType::Kind::InferredType: return "_";
    }
    return "<unknown>";
}

// ── SemaChecker method definitions ───────────────────────────────────────────

lir::LProgram SemaChecker::run(const std::vector<hermes::Hermes>& asts,
                                const std::vector<std::string>& filenames,
                                const std::vector<bool>& from_binary) {
    filenames_ = &filenames;
    from_binary_ = from_binary.empty() ? nullptr : &from_binary;

    const bool sema_phase_timing = []{
        const char* e = std::getenv("LOGOS_SEMA_PHASE_TIMING");
        return e && e[0] && e[0] != '0';
    }();
    auto t_phase = std::chrono::steady_clock::now();
    auto sema_tick = [&](const char* label) {
        if (!sema_phase_timing) return;
        auto now = std::chrono::steady_clock::now();
        auto us = std::chrono::duration_cast<std::chrono::microseconds>(now - t_phase).count();
        std::fprintf(stderr, "[sema-phase] %-24s %6ld us\n", label, (long)us);
        t_phase = now;
    };

    lir::LProgram prog;
    // M5 step 3a: if a cache is wired in, plug its shared TypePool into
    // this LProgram. All subsequent alloc()s land in the cache's arena,
    // so TypeRefs survive past mono_pass(std::move(prog)) — cache holds
    // an independent refcount on the same TypePoolImpl.
    if (cache_) {
        prog.type_pool = cache_->shared_pool().shared_clone();
        // M5 step 5: alias prog's LIR pools to the cache-held shared_ptrs.
        // Without this, cached LExpr*/LBlock*/etc. (e.g. via
        // hstatic_registry) dangle as soon as mono's out_ pool refcount
        // drops to zero. Cache's shared_ptr holds the storage alive.
        if (cache_->impl()->expr_pool)        prog.expr_pool_        = cache_->impl()->expr_pool;
        if (cache_->impl()->block_pool)       prog.block_pool_       = cache_->impl()->block_pool;
        if (cache_->impl()->hermes_val_pool)  prog.hermes_val_pool_  = cache_->impl()->hermes_val_pool;
        if (cache_->impl()->closure_pool)     prog.closure_pool_     = cache_->impl()->closure_pool;
    }
    pool_ = &prog.type_pool;  // bind so all alloc()s share prog's arena

    // M5 step 3b+5a+5c: install cached symbol tables + restore the
    // hstatic_registry. Step 4's shared expr_pool_ keeps cached LExpr*
    // valid; Step 5b's per-binary-AST skip in collect keeps user ASTs
    // re-walking each call (so strict validation still fires) without
    // tripping ODR-less duplicate checks; Step 5c's user-pkg filter in
    // take_snapshot ensures the persisted state contains binary-origin
    // entries only.
    if (cache_ && cache_->impl()->snapshot) {
        prog.hstatic_registry_ = cache_->impl()->snapshot->hstatic_registry;
        install_snapshot(std::move(cache_->impl()->snapshot));
    }
    // Set cur_prog_ before `collect` so LIT_HSTATIC encountered inside
    // type-alias rhs / supertrait bounds / etc. can register into
    // prog.hstatic_registry_ during collection. Otherwise alias-routed
    // HermesStatic const-args produce TypeRefs whose hash is in
    // const_val but whose literal never lands in the registry, and
    // mono later fails to materialise `__const_param:CFG`.
    cur_prog_ = &prog;

    init_primitives();
    sema_tick("init_primitives");
    phase_ = SemaPhase::Collect;
    collect(asts);
    sema_tick("collect");

    if (!result_.ok()) {
        prog.diags = std::move(result_);
        return prog;
    }

    phase_ = SemaPhase::Lower;
    lower_program(asts, prog);
    sema_tick("lower_program");

    // M5 step 3b: snapshot symbol tables for the next sema_lower call.
    // lower_program reads the tables but never mutates them (verified by
    // grep across sema_expr/stmt/decl), so taking the snapshot now is
    // semantically equivalent to taking it right after collect. The
    // tables move OUT of *this* checker — fine because this checker is
    // destroyed when run() returns; the LProgram already has everything
    // it needs from the maps via TypeRefs and LIR items.
    //
    // Also sync the cache's TypePool handle to prog.type_pool so the
    // next call's shared_clone returns a valid (refcount-bumped) handle
    // to the same TypePoolImpl. Without this the cache's pool stays
    // empty (default-constructed) and each call gets a fresh impl,
    // dangling the snapshot's TypeRefs once prog goes out of scope.
    if (cache_) {
        cache_->impl()->shared_pool = prog.type_pool.shared_clone();
        // M5 step 5: keep refcounts on the LIR pools so cached
        // LExpr*/LBlock*/etc. survive past mono's out_ destruction.
        cache_->impl()->expr_pool        = prog.expr_pool_;
        cache_->impl()->block_pool       = prog.block_pool_;
        cache_->impl()->hermes_val_pool  = prog.hermes_val_pool_;
        cache_->impl()->closure_pool     = prog.closure_pool_;
        cache_->impl()->snapshot = take_snapshot();
        // M5 step 5a: also capture prog.hstatic_registry_ (per-LProgram
        // field, not in SemaChecker). With pools shared above the LExpr*
        // entries here stay alive for the next call.
        cache_->impl()->snapshot->hstatic_registry = prog.hstatic_registry_;
    }

    // Enforce the "one eidos per (genos, tag-system)" invariant at compile
    // time.  Two different impl targets that end up with the same
    // (tag_system, type_code) pair would overwrite each other in the
    // dispatch table (and trigger a link-time collision via the sentinel
    // globals in mlir_gen_dyn.cpp).  Surface a clearer diagnostic here.
    {
        StrMap<std::string> seen;
        for (const auto& de : prog.dispatch_entries) {
            if (de.type_code == 0) continue;
            auto key = de.tag_system + "#" + std::to_string(de.type_code);
            auto [it, inserted] = seen.emplace(std::move(key), de.impl_type_name);
            if (!inserted && it->second != de.impl_type_name) {
                ctx_.clear();
                error(std::format(
                    "two eide register for genos type_code {} in tag system '{}': "
                    "'{}' and '{}' — only one eidos per (genos, tag-system) is allowed",
                    de.type_code, de.tag_system,
                    it->second, de.impl_type_name));
            }
        }
    }

    // Phase 7 slice 12: validate `#[metaprog_handler(...)]` registrations.
    // Hook fn must be a non-extern, non-generic free fn taking a single
    // `*const u8` parameter (target node ptr) and returning ().
    // Trigger names must be unique across the program — collisions would
    // make handler dispatch ambiguous.
    {
        for (const auto& mh : metaprog_handlers_) {
            ctx_ = std::format("fn {}", mh.hook_fn);
            node_line_ = 0;
            if (mh.trigger == "<missing>") {
                error("#[metaprog_handler] requires a string-literal trigger name, e.g. #[metaprog_handler(\"derive_debug\")]");
                continue;
            }
            // Phase 7 slice 14: multiple handlers per trigger are allowed —
            // all fire on each match in source-declaration order. No
            // dedup of the (trigger, hook_fn) pair: registering the same
            // fn twice would call it twice, which is the user's bug.
            // mh.hook_fn was captured as the bare AST name; the actual
            // emitted symbol may carry `pkg$base__f__sig` mangling.
            const lir::LFunction* fn = nullptr;
            for (const auto& f : prog.functions)
                if (bare_fn_name(f->name) == mh.hook_fn) { fn = f.get(); break; }
            if (!fn) { error("#[metaprog_handler] not a free fn"); continue; }
            if (fn->is_extern)
                error("#[metaprog_handler] cannot be applied to extern fn");
            if (!fn->type_params.empty())
                error("#[metaprog_handler] hook must not be generic");
            if (fn->params.size() != 1) {
                error("#[metaprog_handler] hook must take exactly one parameter (target_offset: u32)");
                continue;
            }
            // Param is the AnyVal-style offset of the triggered item
            // within the module's Hermes doc. Hooks reconstruct the
            // node via AnyVal::from_offset(target_offset) + existing
            // HermesView/OView API.
            auto pt = TypeRef(fn->params[0].type);
            if (pt.kind() != LogosType::Kind::U32)
                error("#[metaprog_handler] hook param must be u32 (offset of triggered item)");
            if (fn->ret_type && TypeRef(fn->ret_type).kind() != LogosType::Kind::Void)
                error("#[metaprog_handler] hook must return ()");
        }
    }

    // B-gn-05: every specialisation `fn helper<Foo>` needs a generic `fn
    // helper<T>` to specialise on; otherwise the def silently disappears
    // (lowered into prog.specializations, never resolved at call sites)
    // and the user gets a misleading "undefined function 'helper'".
    {
        std::set<std::string> generic_bases;
        for (auto& f : prog.functions) {
            if (!f->type_params.empty())
                generic_bases.insert(std::string(bare_fn_name(f->name)));
        }
        for (auto& s : prog.specializations) {
            // s->name is the bare base (e.g. "helper" / "describe").
            if (!generic_bases.count(std::string(bare_fn_name(s->name)))) {
                ctx_  = std::format("fn {}", s->name);
                file_ = s->source_file;
                node_line_ = 0;
                error(std::format(
                    "specialisation 'fn {}<...>' has no generic counterpart "
                    "'fn {}<T>' to specialise on. If you meant a regular "
                    "free fn, rename the type parameter so it doesn't shadow "
                    "an existing type.",
                    s->name, s->name));
            }
        }
    }

    prog.diags      = std::move(result_);
    prog.metaprog_handlers = std::move(metaprog_handlers_);
    prog.metaprog_targets = std::move(metaprog_targets_);

    // Stage 3g.1: populate prog.mirror_table after lowering completes. With
    // pool_ bound to prog.type_pool throughout sema, mirror offsets and
    // TypeRef offsets share the same arena from the start.
    lir_mirror_emit_into(prog, *prog.mirror_table);

    // M5 step 6: lir_mirror_emit_into's backfill path (taken for cached
    // LFunction bodies whose mirror_offset_ is already set from a prior
    // sema_lower) registers only the top-level stmt/block in their reverse
    // maps; it does NOT recurse into sub-exprs / sub-blocks / sub-HVs /
    // sub-closures of cached items. Mono's lexpr_of / lblock_of /
    // hermes_val_of / etc. would then return null inside cached bodies,
    // corrupting subst (e.g. SReturn loses its value). Mirror
    // lir_mirror_populate_moved's pool-sweep approach: every pooled node
    // with a non-zero mirror_offset_ goes into the reverse map. Cheap
    // (single pass per append-only pool, runs every sema_lower regardless
    // of cache state — the cost is negligible vs the lower walk savings).
    if (prog.expr_pool_) {
        for (auto& uptr : *prog.expr_pool_)
            if (uptr && uptr->mirror_offset_ != hermes::arena_offset_t{})
                prog.mirror_table->expr_by_offset[
                    uptr->mirror_offset_.value()] = uptr.get();
    }
    if (prog.block_pool_) {
        for (auto& uptr : *prog.block_pool_)
            if (uptr && uptr->mirror_offset_ != hermes::arena_offset_t{})
                prog.mirror_table->block_by_offset[
                    uptr->mirror_offset_.value()] = uptr.get();
    }
    if (prog.hermes_val_pool_) {
        for (auto& uptr : *prog.hermes_val_pool_)
            if (uptr && uptr->mirror_offset_ != hermes::arena_offset_t{})
                prog.mirror_table->hermes_val_by_offset[
                    uptr->mirror_offset_.value()] = uptr.get();
    }
    // EClosure is not pooled-keyed in the reverse-map set (closure_box_inner
    // maps LExpr* → EClosure*, populated by LirBuilder at construction).
    // Stmts and Patterns are not pooled (owned by their parent vectors); the
    // backfill walk in lir_mirror_emit_into reaches them only via the
    // function-body recursion, which is shallow. Walk every cached LFunction
    // body recursively here, registering every LStmt and Pattern in the
    // reverse maps. The walk operates entirely off in-memory fields (no
    // mirror reads), so it works regardless of table state.
    auto register_lstmts_in_block = [&](auto&& self, const lir::LBlock& blk) -> void {
        for (auto& st : blk.stmts) {
            if (st.mirror_offset_ != hermes::arena_offset_t{})
                prog.mirror_table->stmt_by_offset[st.mirror_offset_.value()] = &st;
            // LStmt carries no in-memory children other than mirror_offset_
            // (the variant fields were retired at Stage B.6). All children
            // are reachable only through the mirror view — sub-blocks are
            // separately pooled and covered by the block_pool_ sweep above.
        }
    };
    auto walk_fn = [&](const lir::LFunction& fn) {
        register_lstmts_in_block(register_lstmts_in_block, fn.body);
    };
    for (auto& f : prog.functions)        if (f) walk_fn(*f);
    for (auto& f : prog.specializations)  if (f) walk_fn(*f);
    for (auto& s : prog.structs)
        for (auto& m : s.methods) if (m) walk_fn(*m);
    for (auto& s : prog.struct_specializations)
        for (auto& m : s.methods) if (m) walk_fn(*m);
    for (auto& i : prog.impls)
        for (auto& m : i.methods) if (m) walk_fn(*m);

    return prog;
}

void SemaChecker::init_primitives() {
    auto ap = [&](LogosType::Kind k) {
        LogosTypeBuilder t; t.kind = k;
        prims_[int(k)] = pool_->alloc(t);
    };
    ap(LogosType::Kind::Void);
    ap(LogosType::Kind::I32);
    ap(LogosType::Kind::I64);
    ap(LogosType::Kind::F64);
    ap(LogosType::Kind::F32);
    ap(LogosType::Kind::Bool);
    ap(LogosType::Kind::U8);
    ap(LogosType::Kind::I8);
    ap(LogosType::Kind::I16);
    ap(LogosType::Kind::U16);
    ap(LogosType::Kind::U32);
    ap(LogosType::Kind::U64);
    ap(LogosType::Kind::I24);
    ap(LogosType::Kind::U24);
    ap(LogosType::Kind::I24);
    ap(LogosType::Kind::I56);
    ap(LogosType::Kind::U24);
    ap(LogosType::Kind::U56);
    ap(LogosType::Kind::I128);
    ap(LogosType::Kind::U128);
    ap(LogosType::Kind::Usize);
    ap(LogosType::Kind::Isize);
    ap(LogosType::Kind::Char);
    ap(LogosType::Kind::IntLit);
    ap(LogosType::Kind::FloatLit);
    ap(LogosType::Kind::Error);
    ap(LogosType::Kind::Never);
    ap(LogosType::Kind::InferredType);
}

TypeRef SemaChecker::lookup_type_by_name(std::string_view name) {
    if (name == "i32")  return prim(LogosType::Kind::I32);
    if (name == "i64")  return prim(LogosType::Kind::I64);
    if (name == "f64")  return prim(LogosType::Kind::F64);
    if (name == "f32")  return prim(LogosType::Kind::F32);
    if (name == "bool") return prim(LogosType::Kind::Bool);
    if (name == "u8")   return prim(LogosType::Kind::U8);
    if (name == "i8")   return prim(LogosType::Kind::I8);
    if (name == "i16")  return prim(LogosType::Kind::I16);
    if (name == "u16")  return prim(LogosType::Kind::U16);
    if (name == "u32")  return prim(LogosType::Kind::U32);
    if (name == "u64")  return prim(LogosType::Kind::U64);
    if (name == "i24")  return prim(LogosType::Kind::I24);
    if (name == "u24")  return prim(LogosType::Kind::U24);
    if (name == "i56")  return prim(LogosType::Kind::I56);
    if (name == "u56")  return prim(LogosType::Kind::U56);
    if (name == "i128") return prim(LogosType::Kind::I128);
    if (name == "u128") return prim(LogosType::Kind::U128);
    if (name == "usize") return prim(LogosType::Kind::Usize);
    if (name == "isize") return prim(LogosType::Kind::Isize);
    if (name == "char")  return prim(LogosType::Kind::Char);
    if (name == "void") return prim(LogosType::Kind::Void);
    if (name == "!")    return prim(LogosType::Kind::Never);
    if (name == "str") {
        // Phase 1B-3: `str` keyword. Default meaning is the existing
        // fat-pointer form Slice<u8> (Rust's `&str` shape). When the
        // surrounding context explicitly permits an unsized result
        // (e.g. turbofish for a `T: ?Sized` parameter), produce the
        // unsized form so the substitution canonicalisation can route
        // `&T` to the same Slice<u8> ABI without double-wrapping.
        if (unsized_ok_) return make_unsized_slice_type(u8_t());
        return make_slice_type(u8_t());
    }
    auto tvit = current_type_params_.find(std::string(name));
    if (tvit != current_type_params_.end()) return tvit->second;
    // Type alias: check current package and imports too
    {
        auto ukey = std::string(name);
        auto check_alias = [&](const std::string& key) -> TypeRef {
            auto it = type_aliases_.find(key);
            if (it != type_aliases_.end() &&
                it->second.type_params.empty() && it->second.lifetime_params.empty())
                return it->second.type;
            return nullptr;
        };
        // B-mv-02: probe the current package's own alias FIRST so a user
        // `type MemDestroyer` shadows a same-name stdlib alias that holds the
        // bare slot (Rust scoping). Then the bare slot, then wildcard imports.
        if (!cur_package_.empty()) if (auto t = check_alias(sema_key(cur_package_, ukey))) return t;
        if (auto t = check_alias(ukey)) return t;
        for (auto& pkg : cur_imports_.wildcard_packages)
            if (auto t = check_alias(sema_key(pkg, ukey))) return t;
    }
    {
        auto [spkg, ssi] = find_struct_by_name(name);
        if (ssi) {
            // §3 Wave 9 — bare `Pair` (no `<…>`) over a generic struct
            // whose type-params all have defaults resolves to the
            // defaulted instantiation `Pair<default0, default1, …>`.
            // resolve_type_generic_inst already handles this for the
            // `Pair<X>` (GENERIC_INST) path; mirror it here so a plain
            // TYPE_REF annotation `let p: Pair` matches the construction
            // shape `Pair { a, b }` instead of producing the unsubst'd
            // `Pair` with no args. Only applies when ALL params default.
            if (!ssi->type_params.empty()) {
                std::vector<TypeRef> args;
                bool all_default = true;
                SemaSubst dsubst;
                for (auto& tp : ssi->type_params) {
                    if (!tp.default_type) { all_default = false; break; }
                    TypeRef d = dsubst.empty() ? tp.default_type
                                               : subst_type_sema(tp.default_type, dsubst);
                    args.push_back(d);
                    dsubst[tp.name] = d;
                }
                if (all_default && !args.empty()) {
                    std::vector<std::string> lt_args;
                    return make_generic_struct(name, std::move(args), std::move(lt_args), spkg);
                }
            }
            return make_struct_type(name, spkg);
        }
    }
    {
        auto [dpkg, dsi] = find_datatype_by_name(name);
        if (dsi) return make_datatype_type(name, dpkg);
    }
    {
        auto [epkg, esi] = find_enum_by_name(name);
        if (esi) return make_enum_type(name, epkg);
    }
    return nullptr;
}

// ── Drop/move helpers ────────────────────────────────────────────────────────

bool SemaChecker::is_move_type(TypeRef t) const {
    // Shared aggregate-recursion skeleton (moveclass::is_move_type); the leaf /
    // struct / enum callbacks below reproduce sema's exact semantics.
    auto leaf = [&](TypeRef x) -> std::optional<bool> {
        // An owning Box<dyn Trait> owns heap data and is non-Copy → move type.
        // (A borrowed &dyn is Copy-like and not a move type.) This lets EVERY
        // move-tracking site (let-RHS, call args, field / tuple-element moves)
        // suppress the source's drop uniformly — no per-site owning-dyn casing.
        if (TypeRef(x).owning_trait_object()) return true;
        // An owning `Box<[T]>` slice owns its heap buffer (non-Copy) → move type;
        // a borrowed `&[T]` is Copy-like (not a move type).
        if (TypeRef(x).owning_slice()) return true;
        // An owning `Box<Foo>` custom-DST is heap-owned (non-Copy) → move type.
        if (TypeRef(x).owning_dst()) return true;
        // TypeVar — generic body. We don't know if T resolves to a Copy or move
        // type at sema; treat as move (conservative). If T resolves to Copy at
        // mono, the suppressed scope-exit drop is harmless (Copy has no Drop). If
        // T resolves to a move-type, the suppression avoids double-free across
        // slots that bitwise-share the value (Vec.push/remove, `let v: T = *ptr`).
        // Cross-arm move pollution is handled by lower_match/lower_if save/restore.
        if (TypeRef(x).kind() == LogosType::Kind::TypeVar) {
            // §B1: a `T: Copy` bound makes T provably Copy (Copy and Drop are
            // mutually exclusive), so `x: T` used by-value is NOT moved. Only an
            // explicit Copy bound counts (Rust); else stay conservative-move.
            auto nm = std::string(TypeRef(x).type_var_name());
            auto it = current_type_bounds_.find(nm);
            if (it != current_type_bounds_.end())
                for (auto& b : it->second)
                    if (b.trait_name == "Copy") return std::optional<bool>(false);
            return std::optional<bool>(true);
        }
        return std::nullopt;
    };
    // G156-2: an enum is a move type iff it carries a droppable payload or has a
    // user `impl Drop`. Plain C-like / all-Copy-payload enums stay non-move.
    auto enum_is_move = [&](TypeRef x) {
        return !drop_fn_for(x).empty() || has_droppable_fields(x);
    };
    // Struct types are move types unless they implement Copy.
    auto struct_is_move = [&](TypeRef x) {
        return !copy_types_.count(std::string(TypeRef(x).struct_name()));
    };
    return moveclass::is_move_type(t, leaf, struct_is_move, enum_is_move);
}

TypeRef SemaChecker::normalize_assoc_eq(TypeRef t) const {
    if (!t || TypeRef(t).kind() != LogosType::Kind::AssocType) return t;
    TypeRef base = TypeRef(t).assoc_base();
    if (!base || TypeRef(base).kind() != LogosType::Kind::TypeVar) return t;
    auto bit = current_type_bounds_.find(std::string(TypeRef(base).type_var_name()));
    if (bit == current_type_bounds_.end()) return t;
    std::string an(TypeRef(t).assoc_type_name());
    std::string tn(TypeRef(t).trait_name());
    for (auto& b : bit->second) {
        // Match the trait (when the projection records one) and the assoc name.
        if (!tn.empty() && !b.trait_name.empty() && b.trait_name != tn) continue;
        for (auto& [name, ty] : b.assoc_eqs)
            if (name == an && ty) return ty;
    }
    return t;
}

std::string SemaChecker::drop_fn_for(TypeRef t) const {
    if (!t) return {};
    // TypeVar — a generic param. Whether the substituted concrete type has a
    // Drop impl is unknown at sema; emit a deferred drop stmt with a sentinel
    // drop_fn that mono's SDrop case rewrites (or removes) after substitution.
    if (TypeRef(t).kind() == LogosType::Kind::TypeVar) return "__typevar_pending__drop";
    std::string type_name;
    if (TypeRef(t).kind() == LogosType::Kind::Struct) type_name = std::string(TypeRef(t).struct_name());
    // Enums can carry a user `Drop` impl too (`impl Drop for E`). Keyed by the
    // enum name → `E__drop`. The SDrop codegen loads the heap pointer (enums
    // are heap-ptr-to-struct) before calling the drop fn.
    if (TypeRef(t).kind() == LogosType::Kind::Enum) type_name = std::string(TypeRef(t).enum_name());
    if (type_name.empty()) return {};
    std::string mangled = type_name + "__drop";
    // B-mv-02: a candidate Drop impl must belong to the SAME package as `t`.
    // A user `struct Vec<T>` and the stdlib `Vec<T>` share the bare concrete
    // name `Vec$G1$i32` (struct TYPES are package-qualified at the MLIR level,
    // but the `Type__drop` method symbol is not), so without this guard the
    // user's drop-less Vec picks up the stdlib Vec's Drop and gets dropped via
    // a heap-freeing `Vec$G1$i32__drop` (SIGSEGV). Treat an empty package on
    // either side as a wildcard (intrinsics / pre-pkg-tracking paths).
    auto t_pkg = TypeRef(t).pkg_name();
    auto pkg_matches = [&](TypeRef cand_struct) -> bool {
        if (!cand_struct) return false;
        auto cp = TypeRef(cand_struct).pkg_name();
        return cp.empty() || t_pkg.empty() || cp == t_pkg;
    };
    std::vector<TypeRef> sig{t};
    if (auto* fi = find_func_by_base_and_signature(mangled, sig, false))
        return fi->symbol_name.empty() ? mangled : fi->symbol_name;
    for (auto* cand : find_func_candidates(mangled)) {
        if (!cand || cand->param_types.size() != 1) continue;
        auto pt = cand->param_types[0];
        if (pt && types_equal(pt, t))
            return cand->symbol_name.empty() ? mangled : cand->symbol_name;
    }
    // `fn drop(&mut self)` / `fn drop(&self)` — the canonical stdlib `Drop`
    // shape. The param type is `&mut T` / `&T` (a ref to the struct), not the
    // struct by value, so the by-value checks above miss it. The SDrop codegen
    // already calls the drop fn with the value's address (same ABI as the
    // by-value form, since structs pass by pointer), so matching the ref form
    // here is sufficient — no codegen change needed.
    for (auto* cand : find_func_candidates(mangled)) {
        if (!cand || cand->param_types.size() != 1) continue;
        auto pt = cand->param_types[0];
        if (!pt) continue;
        auto pk = TypeRef(pt).kind();
        if ((pk == LogosType::Kind::Ref || pk == LogosType::Kind::MutRef) &&
            TypeRef(pt).pointee() && types_equal(TypeRef(pt).pointee(), t) &&
            pkg_matches(TypeRef(pt).pointee()))
            return cand->symbol_name.empty() ? mangled : cand->symbol_name;
    }
    // Generic Drop impl: `impl<T> Drop for Foo<T>` registers Foo__drop with
    // param Foo<TypeVar>. Strict types_equal can't match a concrete
    // Foo<i64>. Fall back to a base-name match — any one-param candidate
    // whose param is a struct of the same base name accepts the concrete
    // after monomorphisation. mono_clone's SDrop case re-mangles the
    // returned template name to <concrete_struct_name>__drop at clone time,
    // matching the symbol clone_struct_def emits when instantiating the
    // struct's methods.
    for (auto* cand : find_func_candidates(mangled)) {
        if (!cand || cand->param_types.size() != 1) continue;
        auto pt = cand->param_types[0];
        if (!pt) continue;
        auto pk = TypeRef(pt).kind();
        // Accept `&mut self` / `&self` by peeling one ref level.
        if ((pk == LogosType::Kind::Ref || pk == LogosType::Kind::MutRef) &&
            TypeRef(pt).pointee()) {
            pt = TypeRef(pt).pointee();
            pk = TypeRef(pt).kind();
        }
        if (pk != LogosType::Kind::Struct && pk != LogosType::Kind::ZonedStruct) continue;
        if (TypeRef(pt).struct_name() == TypeRef(t).struct_name() && pkg_matches(pt))
            return cand->symbol_name.empty() ? mangled : cand->symbol_name;
    }
    return {};
}

bool SemaChecker::has_droppable_fields(TypeRef t) const {
    if (!t) return false;
    // A field/element/payload is droppable if it has a drop fn or transitively
    // droppable fields — BUT a `T: Copy`-bounded TypeVar is provably non-droppable
    // (Copy and Drop are mutually exclusive), even though drop_fn_for(TypeVar)
    // returns the `__typevar_pending__drop` sentinel. Without this, a generic
    // `Foo<T: Copy>` (tuple element / enum payload) is wrongly treated as
    // droppable → move-tracking + drop-glue over-trigger (crash on Copy-only
    // generic enums). Mirrors the Copy-bound check in is_move_type.
    auto is_copy_tv = [&](TypeRef x) -> bool {
        if (!x || TypeRef(x).kind() != LogosType::Kind::TypeVar) return false;
        auto bit = current_type_bounds_.find(std::string(TypeRef(x).type_var_name()));
        if (bit != current_type_bounds_.end())
            for (auto& b : bit->second) if (b.trait_name == "Copy") return true;
        return false;
    };
    auto member_droppable = [&](TypeRef m) -> bool {
        return m && !is_copy_tv(m) && (!drop_fn_for(m).empty() || has_droppable_fields(m));
    };
    // An owning Box<dyn Trait> owns its heap data (dropped via vtable[0]
    // drop_in_place + free). A borrowed &dyn is not droppable.
    if (TypeRef(t).owning_trait_object()) return true;
    // An owning `Box<[T]>` slice owns its heap buffer (free + element drops);
    // a borrowed `&[T]` is not droppable.
    if (TypeRef(t).owning_slice()) return true;
    // An owning `Box<Foo>` custom-DST owns its heap block — droppable.
    if (TypeRef(t).owning_dst()) return true;
    // G158-4: an array `[T; N]` owns its elements — droppable if the element is.
    if (TypeRef(t).kind() == LogosType::Kind::Array)
        return member_droppable(TypeRef(t).elem());
    // G156-2 / G154-4: a tuple owns its elements — droppable if any element is.
    if (TypeRef(t).kind() == LogosType::Kind::Tuple) {
        for (auto e : TypeRef(t).tuple_elems())
            if (member_droppable(e)) return true;
        return false;
    }
    // G156-2 enum-half: an enum owns its active variant's payload — droppable if
    // any variant has a droppable payload field. Generic enum payloads are
    // stored as TypeVars; map them through the enum's type_params → this
    // TypeRef's concrete type_args (shallow; nested generics fall back to bare).
    if (TypeRef(t).kind() == LogosType::Kind::Enum) {
        auto eit = enums_.end();
        if (!TypeRef(t).pkg_name().empty())
            eit = enums_.find(sema_key(TypeRef(t).pkg_name(), TypeRef(t).enum_name()));
        if (eit == enums_.end()) eit = enums_.find(std::string(TypeRef(t).enum_name()));
        if (eit == enums_.end()) return false;
        auto targs = TypeRef(t).type_args();
        auto& tparams = eit->second.type_params;
        auto concretize = [&](TypeRef pt) -> TypeRef {
            if (pt && TypeRef(pt).kind() == LogosType::Kind::TypeVar) {
                auto nm = TypeRef(pt).type_var_name();
                for (size_t k = 0; k < tparams.size() && k < targs.size(); ++k)
                    if (tparams[k].name == nm) return targs[k];
            }
            return pt;
        };
        for (auto& v : eit->second.variants)
            for (auto pt : v.payload_types) {
                TypeRef cpt = concretize(pt);
                if (member_droppable(cpt))
                    return true;
            }
        return false;
    }
    if (TypeRef(t).kind() != LogosType::Kind::Struct) return false;
    auto sit = structs_.end();
    if (!TypeRef(t).pkg_name().empty()) {
        auto qkey = sema_key(TypeRef(t).pkg_name(), TypeRef(t).struct_name());
        sit = structs_.find(qkey);
    }
    if (sit == structs_.end()) sit = structs_.find(std::string(TypeRef(t).struct_name()));
    if (sit == structs_.end()) return false;
    // `#[no_auto_drop]` (ManuallyDrop<T> lang-item shape): the compiler must
    // not run the inner field's destructor at scope exit — the wrapper's
    // whole purpose is to suppress that. Treat as having no droppable fields.
    if (sit->second.no_auto_drop) return false;
    for (auto& f : sit->second.fields) {
        if (!drop_fn_for(f.type).empty()) return true;
        if (has_droppable_fields(f.type)) return true;
    }
    return false;
}

// Auto-Copy: a struct with no `impl Drop` and whose every field is itself
// a Copy type behaves as Copy (uniform structural rule — no
// `#[derive(Copy)]` opt-in). Fixpoint over the struct dependency graph;
// `impl Copy for X` entries inserted by collect_impl seed the set.
//
// Copy field kinds: all primitives (integers, floats, bool, char,
// usize/isize), raw pointers (*const/*mut), references (&/&mut),
// function pointers, and structs already known Copy. Tuples are Copy
// iff every element is Copy. Anything else (TypeVar, Closure, Array of
// non-Copy, Slice, TraitObject, Enum-with-payload, etc.) blocks Copy.
//
// `impl Drop for X` blocks Copy regardless of field shape. has_droppable_fields
// is *not* used here — transitive drop comes from a field type, and if that
// type isn't Copy, the field-kind check already rejects.
void SemaChecker::compute_auto_copy_types() {
    using K = LogosType::Kind;
    auto field_kind_is_trivially_copy = [](K k) {
        switch (k) {
            case K::I8: case K::I16: case K::I24: case K::I32:
            case K::I56: case K::I64: case K::I128:
            case K::U8: case K::U16: case K::U24: case K::U32:
            case K::U56: case K::U64: case K::U128:
            case K::F32: case K::F64:
            case K::Bool: case K::Char:
            case K::Usize: case K::Isize:
            // `&mut T` is NOT Copy in Rust — exclusive references are move-only.
            // Listing it here used to auto-promote `struct S { r: &mut T }` to
            // Copy, which is unsound (a binding holding `r.clone()` could then
            // alias the same target as the original). M2's `is_move_type`
            // already treats MutRef as move-type at borrow_check; this brings
            // the struct-auto-Copy classifier into line.
            case K::Ptr: case K::Ref:
            case K::FnPtr: case K::FnItem: case K::TaggedPtr:
            case K::Enum:               // payload-less enums; payload enums rejected below
                return true;
            default: return false;
        }
    };
    auto has_drop_impl = [&](const std::string& bare_name) {
        // Drop registration: collect_impl inserts into impls_ keyed
        // "Drop::<target>". Plain bare-name lookup matches both
        // `impl Drop for X` and pkg-qualified variants.
        return impls_.count("Drop::" + bare_name) != 0;
    };
    // is_copy_field: does this field-type qualify as Copy given the current
    // pending-copy set? Recurses into struct/tuple shapes; bottoms out on
    // primitive kinds or the pending set.
    std::function<bool(TypeRef)> is_copy_field;
    is_copy_field = [&](TypeRef t) -> bool {
        if (!t) return false;
        auto k = TypeRef(t).kind();
        if (field_kind_is_trivially_copy(k)) {
            // Enum: Copy iff no variant has a payload AND no impl Drop.
            // (Logos enums-with-payload are tagged unions storing owned data.)
            if (k == K::Enum) {
                auto ename = std::string(TypeRef(t).struct_name());
                auto eit = enums_.find(ename);
                if (eit == enums_.end()) return true;  // unknown — be generous
                for (auto& v : eit->second.variants)
                    if (!v.payload_types.empty()) return false;
                return true;
            }
            return true;
        }
        if (k == K::Struct) {
            // Look up by qualified-then-bare key, same as has_droppable_fields.
            auto sit = structs_.end();
            if (!TypeRef(t).pkg_name().empty()) {
                auto qkey = sema_key(TypeRef(t).pkg_name(), TypeRef(t).struct_name());
                sit = structs_.find(qkey);
            }
            if (sit == structs_.end()) sit = structs_.find(std::string(TypeRef(t).struct_name()));
            if (sit == structs_.end()) return false;  // unknown — conservative
            return copy_types_.count(std::string(TypeRef(t).struct_name())) != 0;
        }
        if (k == K::Tuple) {
            for (auto sub : TypeRef(t).type_args())
                if (!is_copy_field(sub)) return false;
            return true;
        }
        return false;
    };

    // Fixpoint: each round, promote any non-Drop struct whose every field is
    // a copy_field. Stops when no new struct is promoted.
    bool changed = true;
    while (changed) {
        changed = false;
        for (auto& [skey, info] : structs_) {
            // skey is "pkg::name" (sema_key separator) or "name"; strip pkg
            // for the copy_types_ set because that's what is_move_type keys
            // on (TypeRef::struct_name() returns bare).
            std::string bare = skey;
            if (auto sep = bare.rfind("::"); sep != std::string::npos)
                bare = bare.substr(sep + 2);
            if (copy_types_.count(bare)) continue;
            // Spec / annotation / Hermes datatypes — leave to manual `impl Copy`.
            if (!info.is_data_plain) continue;
            if (info.fields.empty()) continue;  // zero-sized; skip (Logos treats odd)
            if (has_drop_impl(bare)) continue;
            bool all_copy = true;
            for (auto& f : info.fields) {
                if (!is_copy_field(f.type)) { all_copy = false; break; }
            }
            if (all_copy) {
                copy_types_.insert(bare);
                changed = true;
            }
        }
    }
}

std::string SemaChecker::trait_targ_suffix(const std::vector<TypeRef>& args) const {
    if (args.empty()) return {};
    std::string s = "$G" + std::to_string(args.size());
    for (auto a : args) {
        s += "$";
        std::string ts = a ? type_str(a) : std::string("?");
        for (char& c : ts) if (!(std::isalnum((unsigned char)c) || c == '_')) c = '_';
        s += ts;
    }
    return s;
}

const SemaChecker::AssocTypeEntry* SemaChecker::find_assoc_type_entry(
        const std::string& trait_name, const std::string& target,
        const std::string& aname) const {
    // G156-1: prefer the trait-arg-suffixed key when the args are known from
    // the current impl context (two `Trait<T>` impls for one type at distinct T
    // register their assoc types under distinct suffixed keys).
    if (current_impl_trait_name_ == trait_name && !current_impl_trait_args_.empty()) {
        auto it = assoc_type_impls_.find(
            trait_name + trait_targ_suffix(current_impl_trait_args_)
            + "::" + target + "::" + aname);
        if (it != assoc_type_impls_.end()) return &it->second;
    }
    auto it = assoc_type_impls_.find(trait_name + "::" + target + "::" + aname);
    return it != assoc_type_impls_.end() ? &it->second : nullptr;
}

// P2-15 object-safety (Rust E0038 / dyn-compatibility). A trait coerced to a
// trait object must be dyn-dispatchable: every method needs a vtable slot. Reject
// (once per trait) when a method can't have one. A method with `where Self: Sized`
// is EXCLUDED from the vtable, so it never affects object-safety (skipped here).
// Detected E0038 cases:
//   • generic method (`fn f<T>`)         — no single monomorphized slot;
//   • no `self` receiver (associated fn) — nothing to dispatch on;
//   • returns `Self` by value            — size unknown behind the object;
//   • `Self` by value in a parameter     — caller can't name the erased type.
void SemaChecker::check_trait_object_safe(const std::string& trait_name) {
    if (dyn_safety_reported_.count(trait_name)) return;  // dedup: report once
    auto [pkg, ti] = find_trait_by_name(trait_name);
    if (!ti) return;  // unknown trait already diagnosed at the call site
    auto is_self = [](TypeRef t) {
        return t && TypeRef(t).kind() == LogosType::Kind::TypeVar &&
               std::string(TypeRef(t).type_var_name()) == "Self";
    };
    std::function<bool(TypeRef)> mentions_self = [&](TypeRef t) -> bool {
        if (!t) return false;
        if (is_self(t)) return true;
        for (auto a : TypeRef(t).type_args()) if (mentions_self(a)) return true;
        if (TypeRef(t).pointee() && mentions_self(TypeRef(t).pointee())) return true;
        if (TypeRef(t).elem() && mentions_self(TypeRef(t).elem())) return true;
        for (auto e : TypeRef(t).tuple_elems()) if (mentions_self(e)) return true;
        return false;
    };
    // logos-core 3.3: a trait with a Generic Associated Type item is
    // NOT object-safe — GAT instantiation requires a concrete impl
    // (the type is parameterised by something the vtable can't know).
    // Rust E0038 lists this; pre-fix it was undocumented in
    // check_trait_object_safe and slipped through to a runtime
    // segfault on dispatch.
    for (auto& at : ti->assoc_types) {
        if (!at.type_params.empty()) {
            dyn_safety_reported_.insert(trait_name);
            error(std::format("the trait `{}` is not object-safe (cannot be a "
                              "`dyn {}` trait object) because it has a generic "
                              "associated type `{}<{}>` — GAT instantiation "
                              "needs a concrete impl",
                              trait_name, trait_name, at.name,
                              at.type_params.empty() ? std::string()
                                                      : at.type_params[0].name));
            return;
        }
    }
    // logos-core 2.8 (opaque return): a method that returns `impl Trait`
    // (or has `impl Trait` in a param) is NOT object-safe — the concrete
    // type isn't known until monomorphisation, so the vtable slot has no
    // single ABI. Walk param/ret recursively for any ImplTrait kind.
    std::function<bool(TypeRef)> mentions_impl_trait = [&](TypeRef t) -> bool {
        if (!t) return false;
        if (TypeRef(t).kind() == LogosType::Kind::ImplTrait) return true;
        for (auto a : TypeRef(t).type_args()) if (mentions_impl_trait(a)) return true;
        if (TypeRef(t).pointee() && mentions_impl_trait(TypeRef(t).pointee())) return true;
        if (TypeRef(t).elem() && mentions_impl_trait(TypeRef(t).elem())) return true;
        for (auto e : TypeRef(t).tuple_elems()) if (mentions_impl_trait(e)) return true;
        return false;
    };
    for (auto& m : ti->methods) {
        if (m.requires_sized_self) continue;   // excluded from the vtable
        std::string reason;
        if (!m.type_params.empty())
            reason = "it has a generic method `" + m.name +
                     "` (a generic method has no vtable slot)";
        else if (!m.has_self_receiver)
            reason = "its associated function `" + m.name +
                     "` has no `self` receiver (nothing to dispatch on)";
        else if (mentions_self(m.ret_type))
            reason = "its method `" + m.name + "` returns `Self` (size unknown "
                     "behind a trait object)";
        else if (mentions_impl_trait(m.ret_type))
            reason = "its method `" + m.name + "` returns `impl Trait` "
                     "(opaque return type — no single vtable slot ABI)";
        else {
            for (size_t i = 1; i < m.param_types.size(); ++i)
                if (is_self(m.param_types[i])) {
                    reason = "its method `" + m.name +
                             "` takes `Self` by value as a parameter";
                    break;
                }
            if (reason.empty()) {
                for (size_t i = 1; i < m.param_types.size(); ++i)
                    if (mentions_impl_trait(m.param_types[i])) {
                        reason = "its method `" + m.name +
                                 "` takes `impl Trait` as a parameter "
                                 "(opaque type — no single vtable slot ABI)";
                        break;
                    }
            }
        }
        if (!reason.empty()) {
            dyn_safety_reported_.insert(trait_name);
            error(std::format("the trait `{}` is not object-safe (cannot be a "
                              "`dyn {}` trait object) because {} — give it a "
                              "`where Self: Sized` bound or avoid `dyn`",
                              trait_name, trait_name, reason));
            return;
        }
    }
}

std::optional<lir::LStmt> SemaChecker::make_drop_stmt(const std::string& name, const VarInfo& info) const {
    // B8: a declared-uninit var's drop is gated at RUNTIME by mlir-gen's dynamic
    // drop flag (it only runs the destructor if the slot holds a live value), so
    // we EMIT the drop here regardless of static init-state — the flag handles
    // the early-return / conditional-init paths. (Previously this skipped uninit
    // vars statically; the flag is exact and also drops conditionally-stored
    // values that the static skip leaked.)
    // Owning `Box<dyn Trait>` (an owning TraitObject — value fat-pair, heap-
    // owned data): emit a drop with the `__box_dyn__drop` sentinel; mlir-gen's
    // SDrop calls vtable slot-0 drop_in_place + frees the boxed data. Now
    // TYPE-DRIVEN (the type carries owning-ness) — the legacy info.owning_dyn
    // flag is still honoured for any binding whose annotation collapsed before
    // the type distinction reached it.
    if (info.type &&
        (TypeRef(info.type).owning_trait_object() ||
         (info.owning_dyn &&
          TypeRef(info.type).kind() == LogosType::Kind::TraitObject))) {
        lir::LStmt s; s.line = node_line_;
        if (cur_prog_)
            s.mirror_offset_ = lir_mirror_emit_drop(
                *cur_prog_, node_line_, name, "__box_dyn__drop", info.type, false, {});
        return s;
    }
    auto dfn = drop_fn_for(info.type);
    bool df  = has_droppable_fields(info.type);
    if (dfn.empty() && !df) return std::nullopt;
    // Suppress auto-drop of the `self` param of a Drop fn — calling drop
    // on `self` from inside its own drop body is infinite recursion.
    // Detected via name match: when the resolved drop_fn equals the
    // currently-being-lowered fn's mangled name. Mono will re-mangle the
    // call site to the concrete instance, but the SAME identity match
    // holds because both sides see the same template name at sema time.
    if (!dfn.empty() && !current_fn_mangled_.empty()) {
        // Both names may carry an overload-disambig "__g__..." suffix —
        // strip from each before compare so the self-recursion check
        // catches the template-vs-template equivalence.
        // Strip pkg prefix (`pkg.`) and overload-disambig suffix (`__[fg]__`)
        // before compare so the self-recursion check catches the
        // template-vs-template equivalence. After unconditional pkg-mangling
        // dfn carries `pkg.Base__drop__g__sig` while current_fn_mangled_ may
        // be the bare `Base__drop`.
        auto strip_g = [](std::string s) {
            if (auto dot = s.rfind('.'); dot != std::string::npos)
                s = s.substr(dot + 1);
            if (auto p = s.find("__g__"); p != std::string::npos) s.resize(p);
            else if (auto p = s.find("__f__"); p != std::string::npos) s.resize(p);
            return s;
        };
        if (strip_g(dfn) == strip_g(current_fn_mangled_)) return std::nullopt;
    }
    // Collect field paths that were moved out of this var. moved_vars_ stores
    // dotted paths like "<name>.<field>" (and deeper, ignored at this level —
    // a field move implies the whole field is gone, so we only need the
    // first-level field name). The mlir-gen field-drop loop reads this and
    // skips matching fields, so the underlying value isn't released twice.
    std::vector<std::string> moved_fields;
    {
        std::string prefix = name + ".";
        for (auto& mv : moved_vars_) {
            if (mv.size() <= prefix.size()) continue;
            if (mv.compare(0, prefix.size(), prefix) != 0) continue;
            std::string rest = mv.substr(prefix.size());
            // Take only the first segment — a deeper move (a.b.c) still means
            // a.b is partially consumed; we suppress the whole-field drop.
            auto dot = rest.find('.');
            std::string field = (dot == std::string::npos) ? rest : rest.substr(0, dot);
            // De-dup
            bool seen = false;
            for (auto& f : moved_fields) if (f == field) { seen = true; break; }
            if (!seen) moved_fields.push_back(std::move(field));
        }
    }
    lir::LStmt s; s.line = node_line_;
    if (cur_prog_)
        s.mirror_offset_ = lir_mirror_emit_drop(*cur_prog_, node_line_, name, dfn, info.type, df, moved_fields);
    return s;
}

std::vector<lir::LStmt> SemaChecker::collect_drops() const {
    std::vector<lir::LStmt> drops;
    if (scope_.empty()) return drops;
    auto& frame = scope_.back();
    for (auto it = frame.var_order.rbegin(); it != frame.var_order.rend(); ++it) {
        // G156-7: a var moved into a `move` closure stays in moved_vars_ (so
        // use-after-move is enforced) but its destructor must still run — the
        // closure only borrows its storage. Un-skip those.
        if (moved_vars_.count(*it) && !closure_owned_drop_.count(*it)) continue;
        auto vit = frame.vars.find(*it);
        if (vit == frame.vars.end()) continue;
        if (auto d = make_drop_stmt(*it, vit->second))
            drops.push_back(std::move(*d));
    }
    return drops;
}

std::vector<lir::LStmt> SemaChecker::collect_all_drops() const {
    std::vector<lir::LStmt> drops;
    for (auto fit = scope_.rbegin(); fit != scope_.rend(); ++fit) {
        for (auto it = fit->var_order.rbegin(); it != fit->var_order.rend(); ++it) {
            // G156-7: un-skip move-closure-owned captures (still must drop).
            if (moved_vars_.count(*it) && !closure_owned_drop_.count(*it)) continue;
            auto vit = fit->vars.find(*it);
            if (vit == fit->vars.end()) continue;
            if (auto d = make_drop_stmt(*it, vit->second))
                drops.push_back(std::move(*d));
        }
        // G156-7: stop at a closure boundary — a `return` inside a closure body
        // drops only the closure's own frames, never the enclosing function's
        // captured locals (which the env borrows / the original owns).
        if (fit->closure_boundary) break;
    }
    return drops;
}

std::vector<lir::LStmt> SemaChecker::collect_drops_to_loop() const {
    std::vector<lir::LStmt> drops;
    for (auto fit = scope_.rbegin(); fit != scope_.rend(); ++fit) {
        for (auto it = fit->var_order.rbegin(); it != fit->var_order.rend(); ++it) {
            if (moved_vars_.count(*it) && !closure_owned_drop_.count(*it)) continue;
            auto vit = fit->vars.find(*it);
            if (vit == fit->vars.end()) continue;
            if (auto d = make_drop_stmt(*it, vit->second))
                drops.push_back(std::move(*d));
        }
        // Stop AFTER dropping the loop-body frame: break/continue leaves the
        // loop via its edge, so the iteration's locals (incl. this frame's) are
        // released here; outer (enclosing-fn) frames stay live. A closure
        // boundary also stops the walk (a break can't cross it).
        if (fit->loop_boundary || fit->closure_boundary) break;
    }
    return drops;
}

// ── Name helpers ─────────────────────────────────────────────────────────────

std::string SemaChecker::struct_name_of(std::string_view var_name) {
    auto t = lookup(var_name);
    if (!t) return {};
    if (TypeRef(t).kind() == LogosType::Kind::Struct ||
        TypeRef(t).kind() == LogosType::Kind::ZonedStruct) return TypeRef(t).struct_name().to_string();
    if (is_ref_like(TypeRef(t).kind()) && TypeRef(t).pointee() &&
        (TypeRef(t).pointee().kind() == LogosType::Kind::Struct ||
         TypeRef(t).pointee().kind() == LogosType::Kind::ZonedStruct))
        return TypeRef(t).pointee().struct_name().to_string();
    return {};
}


std::string SemaChecker::struct_name_from_type(TypeRef t) {
    if (!t) return {};
    if (TypeRef(t).kind() == LogosType::Kind::Struct || TypeRef(t).kind() == LogosType::Kind::ZonedStruct) {
        if (!TypeRef(t).type_args().empty()) return concrete_struct_name(t);
        return TypeRef(t).struct_name().to_string();
    }
    if (is_ref_like(TypeRef(t).kind()) && TypeRef(t).pointee() &&
        (TypeRef(t).pointee().kind() == LogosType::Kind::Struct ||
         TypeRef(t).pointee().kind() == LogosType::Kind::ZonedStruct)) {
        if (!TypeRef(t).pointee().type_args().empty()) return concrete_struct_name(TypeRef(t).pointee());
        return TypeRef(t).pointee().struct_name().to_string();
    }
    return {};
}


// ── Type parameter helpers ───────────────────────────────────────────────────

std::vector<std::string> SemaChecker::read_lifetime_params(TinyMapView node) {
    std::vector<std::string> result;
    if (!node.has_key(la::TYPE_PARAMS)) return result;
    AnyVal tpav = node.get(la::TYPE_PARAMS.code);
    if (tpav.is_null()) return result;
    auto tplist = map_of(tpav);
    if (!tplist.has_key(la::ITEMS)) return result;
    auto tpitems = arr_of(tplist.get(la::ITEMS.code));
    for (uint64_t i = 0; i < tpitems.size(); ++i) {
        auto tpnode = map_of(tpitems.get(i));
        if (code_of(tpnode) != la::LIFETIME_PARAM) continue;
        result.push_back(std::string(str_of(tpnode.get(la::NAME.code))));
    }
    return result;
}

// B65: extract `'long: 'short` clauses from a node's TYPE_PARAMS items.
// LIFETIME_PARAM nodes with non-empty ITEMS encode outlives bounds:
//   `'long: 'a + 'b + 'c` → ('long, 'a), ('long, 'b), ('long, 'c)
// Each ITEMS entry is a LIFETIME_PARAM sub-node with NAME = shorter lifetime.
std::vector<std::pair<std::string, std::string>>
SemaChecker::read_lifetime_outlives_from(TinyMapView node, int32_t field_code) {
    std::vector<std::pair<std::string, std::string>> result;
    if (!node.has_key(field_code)) return result;
    AnyVal tpav = node.get(field_code);
    if (tpav.is_null()) return result;
    auto tplist = map_of(tpav);
    if (!tplist.has_key(la::ITEMS)) return result;
    auto tpitems = arr_of(tplist.get(la::ITEMS.code));
    for (uint64_t i = 0; i < tpitems.size(); ++i) {
        auto tpnode = map_of(tpitems.get(i));
        if (code_of(tpnode) != la::LIFETIME_PARAM) continue;
        if (!tpnode.has_key(la::ITEMS)) continue;
        std::string longer(str_of(tpnode.get(la::NAME.code)));
        auto inner = arr_of(tpnode.get(la::ITEMS.code));
        for (uint64_t j = 0; j < inner.size(); ++j) {
            auto inode = map_of(inner.get(j));
            if (code_of(inode) != la::LIFETIME_PARAM) continue;
            std::string shorter(str_of(inode.get(la::NAME.code)));
            result.emplace_back(longer, shorter);
        }
    }
    return result;
}

std::vector<std::pair<std::string, std::string>>
SemaChecker::read_lifetime_outlives(TinyMapView node) {
    return read_lifetime_outlives_from(node, la::TYPE_PARAMS.code);
}

bool SemaChecker::assoc_eqs_satisfied(
    const std::string& trait_name,
    const std::string& concrete_name,
    const std::string& base_name,
    const std::vector<std::pair<std::string, TypeRef>>& expected) {
    if (expected.empty()) return true;
    for (auto& [aname, expected_ty] : expected) {
        if (!expected_ty) continue;
        // Look up the impl's `type Assoc = X` for this trait+concrete.
        // 1. Direct (concrete name).
        std::string key = trait_name + "::" + concrete_name + "::" + aname;
        auto it = assoc_type_impls_.find(key);
        if (it == assoc_type_impls_.end() && !base_name.empty() && base_name != concrete_name) {
            std::string bkey = trait_name + "::" + base_name + "::" + aname;
            it = assoc_type_impls_.find(bkey);
        }
        TypeRef found = (it != assoc_type_impls_.end()) ? it->second.type : nullptr;
        // 2. Blanket-derived: collect_impl keys blanket-impl assoc-types under
        // `Trait::$blanket$Trait$BoundTrait$Target::AssocName`. If `concrete`
        // doesn't have a direct impl but satisfies a blanket's bounds, use
        // that blanket's assoc-type definition. The stored type may reference
        // the blanket's target typevar (e.g. `type P = DT::Prim`); substitute
        // target → concrete and recursively resolve via subst_type_sema so
        // chains like `K: Primitive ⇒ K: HasPrim<P = K::Prim = i32>` reduce
        // to a concrete type before the equality check.
        if (!found) {
            for (auto& bi : blanket_impls_) {
                if (bi.trait_name != trait_name) continue;
                logos::compiler::StrSet seen_pri;
                bool ok = bi.bound_trait.empty()
                    || sema_has_impl_recursive(bi.bound_trait, concrete_name, base_name, seen_pri);
                if (ok) {
                    for (auto& eb : bi.extra_bounds) {
                        logos::compiler::StrSet seen_eb;
                        if (!sema_has_impl_recursive(eb, concrete_name, base_name, seen_eb)) {
                            ok = false; break;
                        }
                    }
                }
                if (!ok) continue;
                std::string bkey = trait_name + "::$blanket$" + trait_name + "$"
                                 + bi.bound_trait + "$" + bi.target_typevar
                                 + "::" + aname;
                auto bit = assoc_type_impls_.find(bkey);
                if (bit == assoc_type_impls_.end()) continue;
                TypeRef concrete_t = lookup_type_by_name(concrete_name);
                if (!concrete_t && !base_name.empty() && base_name != concrete_name)
                    concrete_t = lookup_type_by_name(base_name);
                if (concrete_t) {
                    SemaSubst bsubst;
                    bsubst[bi.target_typevar] = concrete_t;
                    found = subst_type_sema(bit->second.type, bsubst);
                } else {
                    found = bit->second.type;
                }
                break;
            }
        }
        if (!found) return false;
        if (!types_equal(found, expected_ty)) return false;
    }
    return true;
}

// ── Phase 2-1: cfg!() predicate evaluation ────────────────────────────────
//
// cfg!() syntax is restricted to a fixed grammar (IDENT / IDENT="str" /
// all(...) / any(...) / not(...)). A full re-parse via the main Logos
// parser would be overkill; the mini-parser below tokenises the raw
// text directly. Builtin keys resolve against compile-target metadata;
// `feature = "name"` resolves against the `cfg_features_` set populated
// from --cfg flags / lforge manifest.

namespace {

struct CfgLexer {
    std::string_view src;
    size_t pos = 0;
    void skip_ws() { while (pos < src.size() && std::isspace(static_cast<unsigned char>(src[pos]))) ++pos; }
    bool eof() { skip_ws(); return pos >= src.size(); }
    char peek() { skip_ws(); return pos < src.size() ? src[pos] : '\0'; }
    bool consume(char c) { skip_ws(); if (pos < src.size() && src[pos] == c) { ++pos; return true; } return false; }
    std::string read_ident() {
        skip_ws();
        size_t start = pos;
        while (pos < src.size()) {
            char c = src[pos];
            if (std::isalnum(static_cast<unsigned char>(c)) || c == '_') ++pos;
            else break;
        }
        return std::string(src.substr(start, pos - start));
    }
    std::string read_string_lit() {
        skip_ws();
        if (pos >= src.size() || src[pos] != '"') return {};
        ++pos;
        size_t start = pos;
        while (pos < src.size() && src[pos] != '"') ++pos;
        std::string s(src.substr(start, pos - start));
        if (pos < src.size()) ++pos;  // closing quote
        return s;
    }
};

}  // namespace

// Compile-target metadata. For the current Logos runtime these match the
// platform the compiler is built for. Eventually a `--target` flag would
// override; for now we expose host-platform values.
static const char* k_target_arch =
#if defined(__x86_64__)
    "x86_64"
#elif defined(__aarch64__)
    "aarch64"
#elif defined(__i386__)
    "x86"
#else
    "unknown"
#endif
    ;
static const char* k_target_os =
#if defined(__linux__)
    "linux"
#elif defined(__APPLE__)
    "macos"
#elif defined(_WIN32)
    "windows"
#else
    "unknown"
#endif
    ;
static const char* k_target_endian =
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
    "big"
#else
    "little"
#endif
    ;
static const char* k_target_family =
#if defined(__unix__) || defined(__APPLE__)
    "unix"
#elif defined(_WIN32)
    "windows"
#else
    ""
#endif
    ;
static const char* k_target_pointer_width = sizeof(void*) == 8 ? "64" : "32";

static bool match_cfg_key_value(std::string_view key, std::string_view val,
                                const logos::compiler::StrSet& features) {
    if (key == "target_arch")          return val == k_target_arch;
    if (key == "target_os")            return val == k_target_os;
    if (key == "target_endian")        return val == k_target_endian;
    if (key == "target_family")        return val == k_target_family;
    if (key == "target_pointer_width") return val == k_target_pointer_width;
    if (key == "feature")              return features.count(std::string(val)) != 0;
    // Unknown key — match Rust: silently false.
    return false;
}

static bool match_cfg_flag(std::string_view name,
                           const logos::compiler::StrSet& features) {
    // Bare identifiers: `unix`, `windows`, `test`, `debug_assertions`, etc.
    if (name == "unix")               return std::string_view(k_target_family) == "unix";
    if (name == "windows")            return std::string_view(k_target_family) == "windows";
    if (name == "test")               return features.count("test") != 0;
    if (name == "debug_assertions")   return features.count("debug_assertions") != 0;
    // Otherwise treat as a feature-like flag, checked against features set.
    return features.count(std::string(name)) != 0;
}

static bool parse_and_eval_cfg(CfgLexer& lex,
                               const logos::compiler::StrSet& features);

static bool parse_cfg_combinator_args(CfgLexer& lex,
                                      const logos::compiler::StrSet& features,
                                      std::vector<bool>& out) {
    // Already past '('. Read predicates separated by commas until ')'.
    while (true) {
        if (lex.eof()) return false;
        if (lex.peek() == ')') { lex.consume(')'); return true; }
        bool v = parse_and_eval_cfg(lex, features);
        out.push_back(v);
        lex.skip_ws();
        if (lex.consume(',')) continue;
        if (lex.consume(')')) return true;
        return false;
    }
}

static bool parse_and_eval_cfg(CfgLexer& lex,
                               const logos::compiler::StrSet& features) {
    std::string ident = lex.read_ident();
    if (ident.empty()) return false;
    if (lex.consume('(')) {
        // Combinator: all(...) / any(...) / not(...).
        std::vector<bool> children;
        if (!parse_cfg_combinator_args(lex, features, children)) return false;
        if (ident == "all") {
            for (bool b : children) if (!b) return false;
            return true;
        }
        if (ident == "any") {
            for (bool b : children) if (b) return true;
            return false;
        }
        if (ident == "not") {
            if (children.size() != 1) return false;
            return !children[0];
        }
        // Unknown combinator → false (treat as no-match).
        return false;
    }
    if (lex.consume('=')) {
        std::string val = lex.read_string_lit();
        return match_cfg_key_value(ident, val, features);
    }
    // Bare ident.
    return match_cfg_flag(ident, features);
}

bool SemaChecker::evaluate_cfg_node(hermes::TinyMapView /*pred_node*/) {
    // Currently unused — cfg!() ARGS go through evaluate_cfg_predicate
    // which parses RAW_TEXT directly. Keeping the prototype for the
    // forthcoming #[cfg(...)] attribute path which has parsed-AST args.
    return false;
}

bool SemaChecker::evaluate_cfg_predicate(hermes::TinyMapView node) {
    if (!node.has_key(la::RAW_TEXT)) return false;
    std::string raw(str_of(node.get(la::RAW_TEXT.code)));
    CfgLexer lex{raw};
    return parse_and_eval_cfg(lex, cfg_features_);
}

bool SemaChecker::match_cfg_predicate_kv(std::string_view key, std::string_view val) {
    return match_cfg_key_value(key, val, cfg_features_);
}
bool SemaChecker::match_cfg_predicate_flag(std::string_view name) {
    return match_cfg_flag(name, cfg_features_);
}

bool SemaChecker::evaluate_cfg_annotation(hermes::TinyMapView ann) {
    // §6.8: cfg combinators in attribute position via ANNOT_CALL.
    // `#[cfg(all(unix, target_arch = "x86_64"))]` parses with the
    // first arg as ANNOT_CALL{NAME:"all", ARGS:[bare-NAME unix,
    // ANNOT_KV target_arch="x86_64"]}; evaluate_cfg_arg recurses
    // into each entry. Top-level: multi-arg list is implicit AND
    // (matches Rust). Single-arg form (most common case:
    // `#[cfg(target_os = "linux")]`) is just a 1-entry list.
    if (!ann.has_key(la::ARGS)) return true;  // `#[cfg]` with no args — match
    auto args_av = ann.get(la::ARGS.code);
    if (args_av.is_null()) return true;
    auto args_list = map_of(args_av);
    if (!args_list.has_key(la::ITEMS)) return true;
    auto items = arr_of(args_list.get(la::ITEMS.code));
    for (uint64_t i = 0; i < items.size(); ++i) {
        auto arg = map_of(items.get(i));
        if (!evaluate_cfg_arg(arg)) return false;
    }
    return true;
}

// §6.8: shared evaluator for one annot_args entry — handles
// `ANNOT_KV` (key=lit), `ANNOT_CALL` (combinator), and the legacy
// bare-NAME flag form. Recurses through nested combinators.
bool SemaChecker::evaluate_cfg_arg(hermes::TinyMapView arg) {
    int32_t code = code_of(arg);
    if (code == la::ANNOT_CALL && arg.has_key(la::NAME) && arg.has_key(la::ARGS)) {
        std::string head(str_of(arg.get(la::NAME.code)));
        auto inner_list = map_of(arg.get(la::ARGS.code));
        std::vector<bool> child_results;
        if (inner_list.has_key(la::ITEMS)) {
            auto items = arr_of(inner_list.get(la::ITEMS.code));
            child_results.reserve(items.size());
            for (uint64_t i = 0; i < items.size(); ++i) {
                auto sub = map_of(items.get(i));
                child_results.push_back(evaluate_cfg_arg(sub));
            }
        }
        if (head == "all") {
            for (bool b : child_results) if (!b) return false;
            return true;
        }
        if (head == "any") {
            for (bool b : child_results) if (b) return true;
            return false;
        }
        if (head == "not") {
            if (child_results.size() != 1) {
                error("cfg `not` takes exactly one predicate");
                return false;
            }
            return !child_results[0];
        }
        error(std::format("unknown cfg combinator `{}` (expected all/any/not)", head));
        return false;
    }
    if (code == la::ANNOT_KV && arg.has_key(la::NAME) && arg.has_key(la::VALUE)) {
        std::string key(str_of(arg.get(la::NAME.code)));
        auto val_node = map_of(arg.get(la::VALUE.code));
        std::string val;
        if (code_of(val_node) == la::LIT_STR && val_node.has_key(la::VALUE)) {
            val = std::string(str_of(val_node.get(la::VALUE.code)));
            if (val.size() >= 2 && val.front() == '"' && val.back() == '"')
                val = val.substr(1, val.size() - 2);
        }
        return match_cfg_key_value(key, val, cfg_features_);
    }
    if (arg.has_key(la::NAME)) {
        std::string name(str_of(arg.get(la::NAME.code)));
        return match_cfg_flag(name, cfg_features_);
    }
    return false;
}

bool SemaChecker::is_effective_dst(TypeRef t) {
    if (!t) return false;
    auto k = t.kind();
    if (k != LogosType::Kind::Struct && k != LogosType::Kind::ZonedStruct) return false;
    std::string sn(t.struct_name());
    // Re-entrancy guard. The tail-field probe below runs subst_type_sema, whose
    // Ptr/Ref handlers canonicalise `*mut/&DstStruct` → DstRef by re-calling
    // is_effective_dst on the pointee. A self-referential struct
    // (`S<T>{val:*mut S<S<T>>}`) would recurse forever. But a field behind a
    // pointer is ALWAYS sized — re-entering for a struct already on the stack
    // means its tail reaches itself only through a pointer, so it is NOT a DST.
    thread_local std::unordered_set<std::string> in_progress;
    if (!in_progress.insert(sn).second) return false;
    struct PopGuard { std::unordered_set<std::string>& s; std::string n;
        ~PopGuard() { s.erase(n); } } pop{in_progress, sn};
    // Layout-only query: look up WITHOUT pub enforcement. is_effective_dst is
    // called on substituted field types (e.g. `Arc<i32>`'s `inner: *mut
    // ArcInner<T>` → recurse into the package-private `ArcInner`), and a
    // privacy diagnostic must not fire from a pure structural probe.
    auto [spkg, ssi] = lookup_qualified_<false>(structs_, sn);
    if (!ssi) { auto [dpkg, dsi] = lookup_qualified_<false>(datatypes_, sn); ssi = dsi; }
    if (!ssi) return false;
    if (ssi->is_dst) return true;
    // Generic instantiation check: substitute type-args into the
    // template's last field; if it lands on UnsizedSlice/UnsizedDyn,
    // the instance is effectively DST.
    if (ssi->fields.empty() || ssi->type_params.empty()) return false;
    auto args = t.type_args();
    if (args.empty()) return false;
    SemaSubst tmp;
    for (size_t i = 0; i < ssi->type_params.size() && i < args.size(); ++i)
        tmp[ssi->type_params[i].name] = args[i];
    auto subst_last = subst_type_sema(ssi->fields.back().type, tmp);
    if (!subst_last) return false;
    auto sk = subst_last.kind();
    return sk == LogosType::Kind::UnsizedSlice || sk == LogosType::Kind::UnsizedDyn;
}

bool SemaChecker::ptr_rel_compatible(TypeRef a, TypeRef b) {
    auto one = [&](TypeRef rp, TypeRef pt) -> bool {
        if (!rp || !pt) return false;
        auto pk = TypeRef(pt).kind();
        if ((pk != LogosType::Kind::Ptr && pk != LogosType::Kind::Ref &&
             pk != LogosType::Kind::MutRef) || !TypeRef(pt).pointee())
            return false;
        TypeRef pointee = TypeRef(pt).pointee();
        // (a) Concrete #[rel_ptr] struct `RP<U>` ↔ `*U` (post-mono / non-generic).
        if (TypeRef(rp).kind() == LogosType::Kind::Struct) {
            auto [pkg, ssi] = find_struct_by_name(std::string(TypeRef(rp).struct_name()));
            if (!ssi || !ssi->rel_ptr) return false;
            auto ta = TypeRef(rp).type_args();
            // Type-erased rel_ptr (NO type arg) — an `any_object_ptr` into a
            // type-tagged object — coerces to a thin `u8` pointer (`*const u8` /
            // `*mut u8`), the raw form the Hermes tag dispatcher reads (it
            // recovers the real type from the object's vlen tag). We never
            // silently type it to `*T`; the user casts `*u8 as *T` explicitly.
            if (ta.empty()) return TypeRef(pointee).kind() == LogosType::Kind::U8;
            return types_equal(pointee, ta[0]);
        }
        // (b) Abstract GAT projection `Z::Ptr<U>` (assoc base = a generic
        // type-param) ↔ `*U`: the zone's pointer form. Accept generically; mono
        // resolves `Z::Ptr` to `*U` (Heap) or a #[rel_ptr] struct (Hermes) and the
        // field read/write does materialize/lower. Gated on the GAT arg matching
        // the pointee so this is the zone-pointer shape, not an arbitrary assoc.
        if (TypeRef(rp).kind() == LogosType::Kind::AssocType) {
            TypeRef base = TypeRef(rp).assoc_base();
            if (base && TypeRef(base).kind() == LogosType::Kind::TypeVar) {
                auto ga = TypeRef(rp).gat_args();   // `Z::Ptr<U>` GAT args, not type_args
                return !ga.empty() && types_equal(pointee, ga[0]);
            }
        }
        return false;
    };
    return one(a, b) || one(b, a);
}

TypeRef SemaChecker::self_describing_dst_ref(TypeRef pointee, bool is_mut) {
    if (!pointee) return nullptr;
    TypeRef p{pointee};
    if (p.kind() != LogosType::Kind::Struct &&
        p.kind() != LogosType::Kind::ZonedStruct)
        return nullptr;
    if (!is_effective_dst(pointee)) return nullptr;
    std::string sn(p.struct_name());
    std::string spkg(p.pkg_name());
    if (spkg.empty()) {
        auto [pk, ssi] = find_struct_by_name(sn);
        if (ssi) spkg = pk;
    }
    // pub-check-FREE lookup (the DST may be foreign-private) — mirrors the
    // raw-ptr thin-decision sites.
    SemaStructInfo* rssi = find_struct_repr_(spkg, sn);
    if (!rssi || !rssi->self_describing) return nullptr;
    // Contract: materializing a fat `&Foo` from a thin pointer recovers the tail
    // length by calling `dst_len`, so a #[self_describing] DST borrowed this way
    // MUST `impl SelfDescribing`. Without it the length would silently read 0.
    // (A self-describing DST used only through raw `*mut`/byte arithmetic — the
    // Segment pattern — never reaches here, so it is not forced to impl it.)
    if (!impls_.count("SelfDescribing::" + sn))
        error(std::format(
            "#[self_describing] struct '{0}' is borrowed as a fat reference "
            "(`&{0}`) but does not implement `SelfDescribing` — its "
            "`dst_len` is required to recover the tail length", sn));
    std::vector<TypeRef> targs = p.type_args();
    return make_dst_ref(sn, spkg, is_mut, std::move(targs));
}

uint64_t SemaChecker::sema_abi_byte_size(TypeRef t, logos::compiler::StrSet& seen) {
    using K = LogosType::Kind;
    if (!t) return 8;
    TypeRef tv{t};
    switch (tv.kind()) {
    case K::Void:    return 0;
    case K::Bool:    return 1;
    case K::U8: case K::I8:      return 1;
    case K::I16: case K::U16:    return 2;
    case K::I24: case K::U24:    return 3;
    case K::I32: case K::U32: case K::F32: case K::IntLit:
    case K::Char:                return 4;
    case K::I56: case K::U56:    return 7;
    case K::I64: case K::U64: case K::F64: case K::FloatLit:
    case K::Ptr:  case K::Ref:  case K::MutRef:
    case K::FnPtr: case K::FnItem: case K::TaggedPtr:
    case K::Usize: case K::Isize: return 8;
    case K::I128: case K::U128:  return 16;
    case K::Slice: case K::Closure: case K::TraitObject: case K::DstRef:
        return 16;
    case K::UnsizedSlice: case K::UnsizedDyn:
        return 0;
    case K::Array:
        if (!tv.elem()) return 0;
        return tv.arr_size() * sema_abi_byte_size(tv.elem(), seen);
    case K::Tuple: {
        uint64_t off = 0, max_align = 1;
        for (auto e : tv.tuple_elems()) {
            uint64_t esz = sema_abi_byte_size(e, seen);
            uint64_t align = std::min(esz, (uint64_t)8);
            if (align > 1) off = (off + align - 1) & ~(align - 1);
            off += esz;
            if (align > max_align) max_align = align;
        }
        return (off + max_align - 1) & ~(max_align - 1);
    }
    case K::Struct:
    case K::ZonedStruct: {
        std::string sn(tv.struct_name());
        if (seen.count(sn)) return 8;  // cycle guard
        auto [spkg, ssi] = find_struct_by_name(sn);
        if (!ssi) { auto [dpkg, dsi] = find_datatype_by_name(sn); ssi = dsi; }
        if (!ssi) return 8;  // unknown — assume pointer size
        seen.insert(sn);
        uint64_t off = 0, max_align = 1;
        for (auto& f : ssi->fields) {
            uint64_t esz = sema_abi_byte_size(f.type, seen);
            uint64_t align = std::min(esz, (uint64_t)8);
            if (align > 1) off = (off + align - 1) & ~(align - 1);
            off += esz;
            if (align > max_align) max_align = align;
        }
        seen.erase(sn);
        return (off + max_align - 1) & ~(max_align - 1);
    }
    case K::Enum: {
        // Simplified: tag (i32) + max variant payload. Mirrors mlir_gen.
        auto [epkg, esi] = find_enum_by_name(std::string(tv.enum_name()));
        if (!esi) return 8;
        uint64_t max_payload = 0;
        for (auto& v : esi->variants) {
            uint64_t variant = 0;
            for (auto& pt : v.payload_types) {
                if (TypeRef(pt).kind() == K::Void) continue;
                variant += sema_abi_byte_size(pt, seen);
            }
            if (variant > max_payload) max_payload = variant;
        }
        return 4 + max_payload;
    }
    default: return 8;
    }
}

void SemaChecker::finalize_relaxed_bounds(TypeParam& tp) {
    // Walk in-place: keep positive bounds, consume `?Trait` markers.
    // `?Sized` clears the implicit Sized bound; any other relaxed name
    // is a hard error. The relaxed bound itself is not propagated as a
    // positive bound — downstream code (mono, bound-check) should never
    // see one in tp.bounds.
    auto it = tp.bounds.begin();
    while (it != tp.bounds.end()) {
        if (!it->is_relaxed) { ++it; continue; }
        if (it->trait_name == "Sized") {
            tp.implicit_sized = false;
        } else {
            error(std::format(
                "type parameter '{}': relaxed bound '?{}' is not permitted "
                "(only `?Sized` is supported)",
                tp.name, it->trait_name));
        }
        it = tp.bounds.erase(it);
    }
}

void SemaChecker::read_trait_bound_args(TinyMapView bnode, TraitBound& tb) {
    // Phase 1: `?Trait` relaxed-bound marker. Grammar emits RELAXED=true
    // for the `?IDENT` form. Only `?Sized` is semantically valid; other
    // relaxed names are rejected when bound list is finalized on the
    // parent type-param. Stored on the bound itself so the post-parse
    // sweep can find it.
    if (bnode.has_key(la::RELAXED)) {
        auto rav = bnode.get(la::RELAXED.code);
        if (!rav.is_null() && rav.is_value()) {
            tb.is_relaxed = (rav.as_value<uint8_t>() != 0);
        }
    }
    // Sprint 5.7: Fn-family parenthesized form `Fn(args) -> ret`.
    // PARAMS holds the arg-type list; RET_TYPE holds the return type
    // (both optional). Distinct slots from TYPE_PARAMS so the two
    // bound forms coexist. `is_fn_family` flagged here for downstream
    // dispatch (sema bound-check, mono substitution).
    if (tb.trait_name == "Fn" || tb.trait_name == "FnMut" ||
        tb.trait_name == "FnOnce") {
        tb.is_fn_family = true;
    }
    if (bnode.has_key(la::PARAMS)) {
        auto pav = bnode.get(la::PARAMS.code);
        if (!pav.is_null()) {
            auto pmap = map_of(pav);
            if (pmap.has_key(la::ITEMS)) {
                auto pitems = arr_of(pmap.get(la::ITEMS.code));
                for (uint64_t i = 0; i < pitems.size(); ++i)
                    tb.fn_params.push_back(resolve_type(map_of(pitems.get(i))));
            }
        }
    }
    if (bnode.has_key(la::RET_TYPE)) {
        auto rav = bnode.get(la::RET_TYPE.code);
        if (!rav.is_null()) tb.fn_ret = resolve_type(map_of(rav));
    }

    // B63 limit-1: capture `for<'a, 'b>` binders from HRTB_BINDERS slot.
    // Items may be raw LIFETIME terminals (strings) or LIFETIME_PARAM maps
    // depending on grammar action shape — accept both defensively.
    if (bnode.has_key(la::HRTB_BINDERS)) {
        auto hav = bnode.get(la::HRTB_BINDERS.code);
        if (!hav.is_null()) {
            auto hmap = map_of(hav);
            if (hmap.has_key(la::ITEMS)) {
                auto hitems = arr_of(hmap.get(la::ITEMS.code));
                for (uint64_t i = 0; i < hitems.size(); ++i) {
                    auto av = hitems.get(i);
                    if (av.is_null()) continue;
                    if (av.is_value()) {
                        tb.hrtb_binders.push_back(std::string(str_of(av)));
                    } else {
                        auto m = map_of(av);
                        if (m.has_key(la::NAME))
                            tb.hrtb_binders.push_back(
                                std::string(str_of(m.get(la::NAME.code))));
                    }
                }
            }
        }
    }

    if (!bnode.has_key(la::TYPE_PARAMS)) return;
    auto tpav = bnode.get(la::TYPE_PARAMS.code);
    if (tpav.is_null()) return;
    auto tamap = map_of(tpav);
    if (!tamap.has_key(la::ITEMS)) return;
    auto items = arr_of(tamap.get(la::ITEMS.code));
    for (uint64_t i = 0; i < items.size(); ++i) {
        auto item = map_of(items.get(i));
        int32_t ic = code_of(item);
        if (ic == la::ASSOC_EQ_BIND) {
            std::string aname(str_of(item.get(la::NAME.code)));
            TypeRef rhs = nullptr;
            if (item.has_key(la::TYPE))
                rhs = resolve_type(map_of(item.get(la::TYPE.code)));
            tb.assoc_eqs.emplace_back(std::move(aname), rhs);
        } else if (ic == la::LIFETIME_PARAM) {
            // L1: lifetime arg at trait-bound TYPE_PARAMS position
            // (e.g. `Foo<'a>` bound). Logos doesn't track regions
            // structurally for bound dispatch — capture for record
            // (lifetime_args) and skip the resolve_type path that
            // would otherwise error "unexpected type node 131".
            if (item.has_key(la::NAME))
                tb.lifetime_args.push_back(std::string(str_of(item.get(la::NAME.code))));
        } else {
            tb.type_args.push_back(resolve_type(item));
        }
    }
}

std::vector<TypeParam> SemaChecker::read_type_params_from(TinyMapView node, int32_t field_code) {
    std::vector<TypeParam> result;
    AnyVal tpav = node.get(field_code);
    if (tpav.is_null()) return result;
    auto tplist = map_of(tpav);
    if (!tplist.has_key(la::ITEMS)) return result;
    auto tpitems = arr_of(tplist.get(la::ITEMS.code));
    // Pre-pass: add all type param names as typevars so bounds referencing sibling params resolve.
    std::vector<std::string> temp_params;
    for (uint64_t i = 0; i < tpitems.size(); ++i) {
        auto tpnode = map_of(tpitems.get(i));
        if (code_of(tpnode) == la::TYPE_PARAM) {
            auto name = std::string(str_of(tpnode.get(la::NAME.code)));
            if (!current_type_params_.count(name)) {
                current_type_params_[name] = make_typevar(name);
                temp_params.push_back(name);
            }
        }
    }
    for (uint64_t i = 0; i < tpitems.size(); ++i) {
        auto tpnode = map_of(tpitems.get(i));
        if (code_of(tpnode) == la::LIFETIME_PARAM) continue;
        if (code_of(tpnode) == la::CONST_PARAM) {
            TypeParam tp;
            tp.name = std::string(str_of(tpnode.get(la::NAME.code)));
            tp.is_const = true;
            tp.const_type = resolve_type(map_of(tpnode.get(la::TYPE.code)));
            if (tpnode.has_key(la::IS_VARIADIC)) {
                AnyVal av = tpnode.get(la::IS_VARIADIC.code);
                tp.is_variadic = !av.is_null() && av.is_value() && av.as_value<uint8_t>() != 0;
            }
            result.push_back(std::move(tp));
            continue;
        }
        if (code_of(tpnode) != la::TYPE_PARAM) continue;
        TypeParam tp;
        tp.name = std::string(str_of(tpnode.get(la::NAME.code)));
        if (tpnode.has_key(la::IS_VARIADIC)) {
            AnyVal av = tpnode.get(la::IS_VARIADIC.code);
            tp.is_variadic = !av.is_null() && av.is_value() && av.as_value<uint8_t>() != 0;
        }
        if (tpnode.has_key(la::ITEMS)) {
            auto bounds = arr_of(tpnode.get(la::ITEMS.code));
            for (uint64_t b = 0; b < bounds.size(); ++b) {
                auto bnode = map_of(bounds.get(b));
                if (code_of(bnode) == la::TRAIT_BOUND) {
                    TraitBound tb;
                    tb.trait_name = std::string(str_of(bnode.get(la::NAME.code)));
                    read_trait_bound_args(bnode, tb);
                    tp.bounds.push_back(std::move(tb));
                } else if (code_of(bnode) == la::LIFETIME_PARAM) {
                    // B65: `T: 'a (+ 'b)*` — type-outlives bounds.
                    tp.lifetime_outlives.push_back(
                        std::string(str_of(bnode.get(la::NAME.code))));
                }
            }
        }
        // Default type argument `<T = Default>` / `<T: Bound = Default>` — the
        // grammar stores it in the TYPE slot (a non-const TYPE_PARAM otherwise
        // never carries TYPE).
        if (tpnode.has_key(la::TYPE)) {
            AnyVal dav = tpnode.get(la::TYPE.code);
            if (!dav.is_null() && dav.is_pointer())
                tp.default_type = resolve_type(map_of(dav));
        }
        finalize_relaxed_bounds(tp);
        result.push_back(std::move(tp));
    }
    // Remove temp typevars added in pre-pass (push_type_params will re-add them properly).
    for (auto& name : temp_params)
        current_type_params_.erase(name);
    return result;
}

std::vector<TypeParam> SemaChecker::read_type_params(TinyMapView node) {
    std::vector<TypeParam> result;
    if (!node.has_key(la::TYPE_PARAMS)) return result;
    AnyVal tpav = node.get(la::TYPE_PARAMS.code);
    if (tpav.is_null()) return result;
    // type_param_list => { ITEMS: $... }
    auto tplist = map_of(tpav);
    if (!tplist.has_key(la::ITEMS)) return result;
    auto tpitems = arr_of(tplist.get(la::ITEMS.code));
    // Pre-pass: add all type param names as typevars so bounds referencing sibling params resolve.
    std::vector<std::string> temp_params;
    for (uint64_t i = 0; i < tpitems.size(); ++i) {
        auto tpnode = map_of(tpitems.get(i));
        if (code_of(tpnode) == la::TYPE_PARAM) {
            auto name = std::string(str_of(tpnode.get(la::NAME.code)));
            if (!current_type_params_.count(name)) {
                current_type_params_[name] = make_typevar(name);
                temp_params.push_back(name);
            }
        }
    }
    for (uint64_t i = 0; i < tpitems.size(); ++i) {
        auto tpnode = map_of(tpitems.get(i));
        // Skip lifetime params ('a) — deferred to borrow checker.
        if (code_of(tpnode) == la::LIFETIME_PARAM) continue;
        if (code_of(tpnode) == la::CONST_PARAM) {
            TypeParam tp;
            tp.name = std::string(str_of(tpnode.get(la::NAME.code)));
            tp.is_const = true;
            tp.const_type = resolve_type(map_of(tpnode.get(la::TYPE.code)));
            if (tpnode.has_key(la::IS_VARIADIC)) {
                AnyVal av = tpnode.get(la::IS_VARIADIC.code);
                tp.is_variadic = !av.is_null() && av.is_value() && av.as_value<uint8_t>() != 0;
            }
            result.push_back(std::move(tp));
            continue;
        }
        if (code_of(tpnode) != la::TYPE_PARAM) continue;
        TypeParam tp;
        tp.name = std::string(str_of(tpnode.get(la::NAME.code)));
        // Check variadic flag (T...)
        if (tpnode.has_key(la::IS_VARIADIC)) {
            AnyVal av = tpnode.get(la::IS_VARIADIC.code);
            tp.is_variadic = !av.is_null() && av.is_value() && av.as_value<uint8_t>() != 0;
        }
        // Optional bounds: ITEMS contains TRAIT_BOUND nodes
        if (tpnode.has_key(la::ITEMS)) {
            auto bounds = arr_of(tpnode.get(la::ITEMS.code));
            for (uint64_t b = 0; b < bounds.size(); ++b) {
                auto bnode = map_of(bounds.get(b));
                if (code_of(bnode) == la::TRAIT_BOUND) {
                    TraitBound tb;
                    tb.trait_name = std::string(str_of(bnode.get(la::NAME.code)));
                    read_trait_bound_args(bnode, tb);
                    tp.bounds.push_back(std::move(tb));
                }
            }
        }
        // Default type argument `<T = Default>` / `<T: Bound = Default>` — the
        // grammar stores it in the TYPE slot (a non-const TYPE_PARAM otherwise
        // never carries TYPE). Filled at use sites in resolve_type.
        if (tpnode.has_key(la::TYPE)) {
            AnyVal dav = tpnode.get(la::TYPE.code);
            if (!dav.is_null() && dav.is_pointer())
                tp.default_type = resolve_type(map_of(dav));
        }
        // Validate: variadic param must be last
        if (tp.is_variadic && i + 1 < tpitems.size())
            error("variadic type parameter must be last in the type parameter list");
        // Note: relaxed-bound finalization happens after `where`-clause
        // merge below, so a `where T: ?Sized` clause is also honored.
        result.push_back(std::move(tp));
    }
    // Merge bounds from `where T: Trait, U: Trait2` clause. The pre-pass
    // typevars are still in scope here (erased AFTER the merge below) so a
    // where-clause bound whose args reference a sibling type-param — e.g.
    // `where F: FnOnce(T, T) -> bool` — resolves `T` instead of failing with
    // "unknown type 'T'" (matches the inline `<T, F: FnOnce(T,T)->bool>` form,
    // which already worked because its bounds resolve during the loop above).
    if (node.has_key(la::WHERE)) {
        AnyVal wav = node.get(la::WHERE.code);
        if (!wav.is_null()) {
            auto wnode = map_of(wav);
            if (wnode.has_key(la::ITEMS)) {
                auto witems = arr_of(wnode.get(la::ITEMS.code));
                for (uint64_t i = 0; i < witems.size(); ++i) {
                    auto constraint = map_of(witems.get(i));
                    if (code_of(constraint) != la::TYPE_PARAM) continue;
                    // G158-6: a reference-typed subject `where &T: Trait` carries
                    // TYPE (the `&T` node) instead of NAME. Resolve it; if it's
                    // `&T`/`&mut T` over a type-param, record the bounds on T
                    // flagged `on_ref_subject` (so the method resolver applies
                    // them only to a matching reference receiver).
                    if (!constraint.has_key(la::NAME) && constraint.has_key(la::TYPE)) {
                        auto refty = resolve_type(map_of(constraint.get(la::TYPE.code)));
                        if (!refty) continue;
                        bool is_mut = TypeRef(refty).kind() == LogosType::Kind::MutRef;
                        TypeRef pointee = ((TypeRef(refty).kind() == LogosType::Kind::Ref ||
                                            is_mut) && TypeRef(refty).pointee())
                                          ? TypeRef(refty).pointee() : TypeRef(nullptr);
                        if (!pointee || TypeRef(pointee).kind() != LogosType::Kind::TypeVar)
                            continue;
                        std::string sub_name(TypeRef(pointee).type_var_name());
                        TypeParam* tp_ptr = nullptr;
                        for (auto& tp : result)
                            if (tp.name == sub_name) { tp_ptr = &tp; break; }
                        if (!tp_ptr) {
                            TypeParam tp; tp.name = sub_name;
                            result.push_back(std::move(tp));
                            tp_ptr = &result.back();
                        }
                        if (constraint.has_key(la::ITEMS)) {
                            auto rbounds = arr_of(constraint.get(la::ITEMS.code));
                            for (uint64_t b = 0; b < rbounds.size(); ++b) {
                                auto bnode = map_of(rbounds.get(b));
                                if (code_of(bnode) != la::TRAIT_BOUND) continue;
                                TraitBound tb;
                                tb.trait_name = std::string(str_of(bnode.get(la::NAME.code)));
                                read_trait_bound_args(bnode, tb);
                                tb.on_ref_subject = true;
                                tb.is_ref_mut = is_mut;
                                tp_ptr->bounds.push_back(std::move(tb));
                            }
                        }
                        continue;
                    }
                    auto tname = std::string(str_of(constraint.get(la::NAME.code)));
                    // Find the type param in result and add bounds.
                    TypeParam* tp_ptr = nullptr;
                    for (auto& tp : result)
                        if (tp.name == tname) { tp_ptr = &tp; break; }
                    if (!tp_ptr) {
                        // type param in where clause not in param list — add it
                        TypeParam tp; tp.name = tname;
                        result.push_back(std::move(tp));
                        tp_ptr = &result.back();
                    }
                    if (constraint.has_key(la::ITEMS)) {
                        auto bounds = arr_of(constraint.get(la::ITEMS.code));
                        for (uint64_t b = 0; b < bounds.size(); ++b) {
                            auto bnode = map_of(bounds.get(b));
                            if (code_of(bnode) == la::TRAIT_BOUND) {
                                TraitBound tb;
                                tb.trait_name = std::string(str_of(bnode.get(la::NAME.code)));
                                read_trait_bound_args(bnode, tb);
                                tp_ptr->bounds.push_back(std::move(tb));
                            }
                        }
                    }
                }
            }
        }
    }
    // Remove temp typevars added in the pre-pass (now that both the inline
    // bounds AND the where-clause bounds have been resolved with the sibling
    // type-params in scope). push_type_params re-adds them properly later.
    for (auto& name : temp_params)
        current_type_params_.erase(name);
    // Phase 1: finalize relaxed bounds (`?Sized`) after all bounds —
    // including those from `where` clauses — have been merged. This is
    // the canonical post-parse point for type-param invariants.
    for (auto& tp : result) finalize_relaxed_bounds(tp);
    return result;
}

// ── Sema-side type substitution ──────────────────────────────────────────────

TypeRef SemaChecker::subst_type_sema(TypeRef t, const SemaSubst& s,
                                               const SemaLifetimeSubst& ls) {
    if (!t) return t;
    switch (t.kind()) {
    case LogosType::Kind::ConstVar:
    case LogosType::Kind::TypeVar: {
        auto it = s.find(std::string(t.type_var_name()));
        return (it != s.end()) ? TypeRef(it->second) : t;
    }
    case LogosType::Kind::Array: {
        auto elem = subst_type_sema(t.elem(), s, ls);
        uint64_t size = t.arr_size();
        std::string symbolic{t.arr_size_var()};
        if (!symbolic.empty()) {
            auto it = s.find(symbolic);
            if (it != s.end()) {
                TypeRef sub(it->second);
                if (auto cv = sub.const_val()) {
                    size = (uint64_t)*cv;
                    symbolic = "";
                } else if (sub.kind() == LogosType::Kind::ConstVar) {
                    symbolic = std::string(sub.type_var_name());
                }
            }
        }
        if (elem == t.elem() && size == t.arr_size() && symbolic == t.arr_size_var()) return t;
        return make_array(elem, size, symbolic);
    }
    case LogosType::Kind::Ptr: {
        auto inner = subst_type_sema(t.pointee(), s, ls);
        // Phase 1B-2: `*const [T]` / `*mut [T]` after substitution are fat
        // pointers. When substitution lands an UnsizedSlice<U> inside a
        // raw pointer, canonicalise to the existing Kind::Slice so the
        // type matches the SLICE_TYPE grammar route (which also lowers
        // `*const [T]` directly to Slice).
        if (inner && inner.kind() == LogosType::Kind::UnsizedSlice)
            return make_slice_type(inner.elem());
        // Phase 1B-4: same canonicalisation for UnsizedDyn → TraitObject.
        if (inner && inner.kind() == LogosType::Kind::UnsizedDyn) {
            std::vector<TypeRef> args_vec = inner.type_args();
            return make_trait_object(inner.trait_name(), std::move(args_vec));
        }
        // Phase 1B-14/15: `*const DstStruct` / `*mut DstStruct` → DstRef —
        // UNLESS the DST is #[self_describing], in which case a raw pointer
        // stays THIN (8B, meta recovered in-band at deref). ref-repr §6.
        if (inner && is_effective_dst(inner)) {
            std::string sn(inner.struct_name());
            std::string spkg(inner.pkg_name());
            if (spkg.empty()) {
                auto [p, ssi] = find_struct_by_name(sn);
                if (ssi) spkg = p;
                else { auto [pd, dsi] = find_datatype_by_name(sn); if (dsi) spkg = pd; }
            }
            // A RAW `*const/*mut` to a #[self_describing] DST stays THIN (8B);
            // the tail length is recoverable in-band at deref. Consulted via a
            // pub-check-FREE lookup: this runs in the substituting (caller's)
            // package context, where the DST struct is often private (e.g.
            // ArcInner) — a visibility error here would be spurious. ref-repr §6.
            SemaStructInfo* rssi = find_struct_repr_(spkg, sn);
            if (rssi && rssi->self_describing) {
                if (inner == t.pointee()) return t;
                return make_ptr(t.mut_ptr(), inner, t.zoned_ptr());
            }
            std::vector<TypeRef> args_vec = inner.type_args();
            return make_dst_ref(sn, spkg, t.mut_ptr(), std::move(args_vec));
        }
        if (inner == t.pointee()) return t;
        return make_ptr(t.mut_ptr(), inner, t.zoned_ptr());   // F3: preserve *zoned
    }
    case LogosType::Kind::Ref:
    case LogosType::Kind::MutRef: {
        auto inner = subst_type_sema(t.pointee(), s, ls);
        std::string lt{t.lifetime()};
        if (!lt.empty()) { auto it = ls.find(lt); if (it != ls.end()) lt = it->second; }
        // Phase 1B-2: `&[T]` / `&mut [T]` after substitution are fat
        // pointers. When substitution lands an UnsizedSlice<U> inside a
        // safe reference, canonicalise to the existing Kind::Slice — the
        // same kind produced by the `&[T]` grammar route. Lifetime info
        // is dropped here because Kind::Slice does not carry per-instance
        // lifetimes (Logos lifetime model is elision-based at this layer).
        if (inner && inner.kind() == LogosType::Kind::UnsizedSlice)
            return make_slice_type(inner.elem(), t.kind() == LogosType::Kind::MutRef);
        // Phase 1B-4: same canonicalisation for UnsizedDyn → TraitObject.
        if (inner && inner.kind() == LogosType::Kind::UnsizedDyn) {
            std::vector<TypeRef> args_vec = inner.type_args();
            return make_trait_object(inner.trait_name(), std::move(args_vec));
        }
        // Phase 1B-14/15: `&DstStruct` / `&mut DstStruct` → DstRef.
        if (inner && is_effective_dst(inner)) {
            std::string sn(inner.struct_name());
            std::string spkg(inner.pkg_name());
            if (spkg.empty()) {
                auto [p, ssi] = find_struct_by_name(sn);
                if (ssi) spkg = p;
                else { auto [pd, dsi] = find_datatype_by_name(sn); if (dsi) spkg = pd; }
            }
            std::vector<TypeRef> targs = inner.type_args();
            return make_dst_ref(sn, spkg, t.kind() == LogosType::Kind::MutRef, std::move(targs));
        }
        if (inner == t.pointee() && lt == t.lifetime()) return t;
        return make_ref(t.kind() == LogosType::Kind::MutRef, inner, lt);
    }
    case LogosType::Kind::Struct:
    case LogosType::Kind::ZonedStruct: {
        if (t.type_args().empty() && t.lifetime_args().empty()) return t;
        std::vector<TypeRef> new_args;
        bool changed = false;
        for (auto a : t.type_args()) {
            auto na = subst_type_sema(a, s, ls);
            changed |= (na != a);
            new_args.push_back(na);
        }
        std::vector<std::string> new_lt_args;
        bool lt_changed = false;
        for (auto& lt : t.lifetime_args()) {
            auto it = ls.find(lt);
            if (it != ls.end()) { new_lt_args.push_back(it->second); lt_changed = true; }
            else                  new_lt_args.push_back(lt);
        }
        if (!changed && !lt_changed) return t;
        LogosTypeBuilder nt;
        nt.kind = t.kind();
        nt.struct_name = t.struct_name();
        nt.pkg_name = t.pkg_name();  // preserve package qualification after substitution
        nt.type_args = std::move(new_args);
        nt.lifetime_args = std::move(new_lt_args);
        return pool_->alloc(std::move(nt));
    }
    case LogosType::Kind::Enum: {
        if (t.type_args().empty() && t.lifetime_args().empty()) return t;
        std::vector<TypeRef> new_args;
        bool changed = false;
        for (auto a : t.type_args()) {
            auto na = subst_type_sema(a, s, ls);
            changed |= (na != a);
            new_args.push_back(na);
        }
        std::vector<std::string> new_lt_args;
        bool lt_changed = false;
        for (auto& lt : t.lifetime_args()) {
            auto it = ls.find(lt);
            if (it != ls.end()) { new_lt_args.push_back(it->second); lt_changed = true; }
            else                  new_lt_args.push_back(lt);
        }
        if (!changed && !lt_changed) return t;
        LogosTypeBuilder nt;
        nt.kind = LogosType::Kind::Enum;
        nt.enum_name = t.enum_name();
        nt.pkg_name = t.pkg_name();  // preserve package qualification after substitution
        nt.type_args = std::move(new_args);
        nt.lifetime_args = std::move(new_lt_args);
        return pool_->alloc(std::move(nt));
    }
    case LogosType::Kind::Tuple: {
        // Variadic-tuple pack expansion: `(A...)` is represented as
        // `Tuple<[TypeVar(A)]>` (single-elem with a pack TypeVar).
        // When subst maps A to a concrete Tuple, splice its elements
        // in place of the pack — yielding the full concrete tuple.
        // Used by variadic-tuple impl dispatch (Phase 4 step 3).
        auto orig_elems = t.tuple_elems();
        if (orig_elems.size() == 1 && orig_elems[0] &&
            TypeRef(orig_elems[0]).kind() == LogosType::Kind::TypeVar) {
            std::string tv_name(TypeRef(orig_elems[0]).type_var_name());
            auto it = s.find(tv_name);
            if (it != s.end()) {
                TypeRef mapped(it->second);
                if (mapped.kind() == LogosType::Kind::Tuple) {
                    std::vector<TypeRef> new_elems;
                    for (auto e : mapped.tuple_elems())
                        new_elems.push_back(subst_type_sema(e, s, ls));
                    return make_tuple_type(std::move(new_elems));
                }
            }
        }
        std::vector<TypeRef> new_elems;
        bool changed = false;
        for (auto e : t.tuple_elems()) {
            auto ne = subst_type_sema(e, s, ls);
            changed |= (ne != e);
            new_elems.push_back(ne);
        }
        if (!changed) return t;
        return make_tuple_type(std::move(new_elems));
    }
    case LogosType::Kind::Slice: {
        auto elem = subst_type_sema(t.elem(), s, ls);
        if (elem == t.elem()) return t;
        return make_slice_type(elem, t.mut_ptr());
    }
    case LogosType::Kind::UnsizedSlice: {
        auto elem = subst_type_sema(t.elem(), s, ls);
        if (elem == t.elem()) return t;
        return make_unsized_slice_type(elem);
    }
    case LogosType::Kind::UnsizedDyn: {
        if (t.type_args().empty()) return t;
        std::vector<TypeRef> new_args;
        bool changed = false;
        for (auto a : t.type_args()) {
            auto na = subst_type_sema(a, s, ls);
            changed |= (na != a);
            new_args.push_back(na);
        }
        if (!changed) return t;
        return make_unsized_dyn_type(t.trait_name(), std::move(new_args));
    }
    case LogosType::Kind::DstRef: {
        // Phase 1B-15: substitute type-args.
        if (t.type_args().empty()) return t;
        std::vector<TypeRef> new_args;
        bool changed = false;
        for (auto a : t.type_args()) {
            auto na = subst_type_sema(a, s, ls);
            changed |= (na != a);
            new_args.push_back(na);
        }
        if (!changed) return t;
        return make_dst_ref(t.struct_name(), t.pkg_name(), t.mut_ptr(),
                            std::move(new_args));
    }
    case LogosType::Kind::TraitObject: {
        if (t.type_args().empty()) return t;
        std::vector<TypeRef> new_args;
        bool changed = false;
        for (auto a : t.type_args()) {
            auto na = subst_type_sema(a, s, ls);
            changed |= (na != a);
            new_args.push_back(na);
        }
        if (!changed) return t;
        return make_trait_object(t.trait_name(), std::move(new_args),
                                 /*owning=*/t.trait_owning_kind(),
                                 /*req_send=*/t.trait_requires_send(),
                                 /*req_sync=*/t.trait_requires_sync());
    }
    case LogosType::Kind::Closure:
    case LogosType::Kind::FnItem:
    case LogosType::Kind::FnPtr: {
        std::vector<TypeRef> new_params;
        bool changed = false;
        for (auto p : t.closure_params()) {
            auto np = subst_type_sema(p, s, ls);
            changed |= (np != p);
            new_params.push_back(np);
        }
        auto new_ret = subst_type_sema(t.closure_ret(), s, ls);
        changed |= (new_ret != t.closure_ret());
        if (!changed) return t;

        LogosTypeBuilder nt;
        nt.kind = t.kind();  // preserve Closure vs FnPtr vs FnItem
        nt.closure_params = std::move(new_params);
        nt.closure_ret = new_ret;
        // logos-core 1.4: FnItem identity rides in struct_name + type_args;
        // preserve both across substitution.
        if (t.kind() == LogosType::Kind::FnItem) {
            nt.struct_name = std::string(t.struct_name());
            for (auto a : t.type_args()) {
                auto na = subst_type_sema(a, s, ls);
                nt.type_args.push_back(na);
            }
        }
        return pool_->alloc(std::move(nt));
    }
    case LogosType::Kind::CfgSlotType: {
        // Substitute the cfg-typevar binding; if it now resolves to a
        // concrete HStaticLit, walk the registered LIR mirror via the
        // encoded path (assoc_type_name) and return the resolved TypeRef.
        std::string cfg_name(t.type_var_name());
        std::string path_enc(t.assoc_type_name());
        auto it = s.find(cfg_name);
        if (it == s.end()) return t;
        TypeRef cfg = TypeRef(it->second);
        cfg = subst_type_sema(cfg, s, ls);
        if (!cfg || TypeRef(cfg).kind() != LogosType::Kind::HStaticLit) return t;
        if (!cur_prog_) return t;
        uint64_t hash = (uint64_t)cfg.const_val().value_or(0);
        auto rit = cur_prog_->hstatic_registry_.find(hash);
        if (rit == cur_prog_->hstatic_registry_.end()) return t;
        if (!rit->second || rit->second->mirror_offset_ == hermes::arena_offset_t{}) return t;
        lir_view::ExprRef eref(cur_prog_->type_pool.arena(), rit->second->mirror_offset_);
        if (eref.kind() != lir_schema::expr::Code::HermesLit) return t;
        // Decode path.
        struct Step { char kind; std::string name; int64_t index; };
        std::vector<Step> steps;
        {
            size_t p = 0;
            while (p < path_enc.size()) {
                Step st{};
                st.kind = path_enc[p++];
                size_t e = path_enc.find('\x1F', p);
                if (e == std::string::npos) e = path_enc.size();
                std::string payload = path_enc.substr(p, e - p);
                if (st.kind == 'F') st.name = std::move(payload);
                else st.index = std::stoll(payload);
                steps.push_back(std::move(st));
                p = e + 1;
            }
        }
        lir_view::HermesValRef cur = lir_view::EHermesLitView{eref}.root();
        for (auto& st : steps) {
            using K = lir_schema::hermes_val::Code;
            bool found = false;
            if (st.kind == 'F' || st.kind == 'I') {
                if (cur.kind() != K::Map) return t;
                auto map = lir_view::HVMapView{cur};
                if (st.kind == 'F' && !map.int_keyed()) {
                    for (uint64_t i = 0, n = map.size(); i < n; ++i)
                        if (map.str_key(i) == st.name) { cur = map.value(i); found = true; break; }
                } else if (st.kind == 'I' && map.int_keyed()) {
                    for (uint64_t i = 0, n = map.size(); i < n; ++i)
                        if (map.int_key(i) == st.index) { cur = map.value(i); found = true; break; }
                }
            } else if (st.kind == 'A') {
                if (cur.kind() != K::Array) return t;
                auto arr = lir_view::HVArrayView{cur};
                if ((uint64_t)st.index >= arr.size()) return t;
                cur = arr.elem((uint64_t)st.index);
                found = true;
            }
            if (!found) return t;
        }
        if (cur.kind() == lir_schema::hermes_val::Code::Type) {
            std::string tname(lir_view::HVTypeView{cur}.name());
            if (auto resolved = const_cast<SemaChecker*>(this)->try_resolve_as_known_type(tname))
                return resolved;
        }
        return t;
    }
    case LogosType::Kind::AssocType: {
        // Substitute the base type first.
        auto subbed_base = subst_type_sema(t.assoc_base(), s, ls);
        
        // Try resolving: if base is substituted to a concrete type, look up impl.
        TypeRef concrete = nullptr;
        if (subbed_base && TypeRef(subbed_base).kind() != LogosType::Kind::TypeVar && TypeRef(subbed_base).kind() != LogosType::Kind::ConstVar) {
            concrete = subbed_base;
        } else if (subbed_base && TypeRef(subbed_base).kind() == LogosType::Kind::TypeVar) {
             // Even if it's a typevar, perhaps it is a known concrete name like "i32" (though unlikely for TypeVar)
             // actually if it's still a typevar, we can't resolve it yet.
        }
        // If still unresolved, try looking up the *substituted* base typevar by name.
        // Using the original base (often "Self") here can over-resolve in generic contexts
        // where Self was already substituted to another typevar (e.g. T).
        if (!concrete && subbed_base && TypeRef(subbed_base).kind() == LogosType::Kind::TypeVar) {
            if (auto looked = const_cast<SemaChecker*>(this)->lookup_type_by_name(TypeRef(subbed_base).type_var_name())) {
                if (TypeRef(looked).kind() != LogosType::Kind::TypeVar &&
                    TypeRef(looked).kind() != LogosType::Kind::ConstVar) {
                    concrete = looked;
                }
            }
        }

        // Substitute gat_args as well
        std::vector<TypeRef> subbed_gat_args;
        bool gat_changed = false;
        for (auto ga : t.gat_args()) {
            auto nga = subst_type_sema(ga, s, ls);
            gat_changed |= (nga != ga);
            subbed_gat_args.push_back(nga);
        }

        // TypeVar-with-bound branch: `K::AssocType` where K is a still-typevar
        // and K's bounds include some `BoundTrait` for which there exists a
        // blanket `impl<DT: BoundTrait> Trait for DT { type AssocType = … }`.
        // Reduce by substituting the blanket's target typevar with K (kept as
        // a TypeVar). Closes abstraction-debt #6 — `K::ViewInStore` → `*const K`
        // when K: PodRef in the surrounding generic scope.
        if (!concrete && subbed_base &&
            TypeRef(subbed_base).kind() == LogosType::Kind::TypeVar) {
            std::string tvname = std::string(TypeRef(subbed_base).type_var_name());
            auto bit = current_type_bounds_.find(tvname);
            if (bit != current_type_bounds_.end()) {
                // Build a flat set of bound trait names for this typevar.
                StrSet tv_bound_set;
                for (auto& tb : bit->second) tv_bound_set.insert(tb.trait_name);
                // G156-1: t.trait_name() may carry a `$G…` trait-arg suffix; the
                // key PREFIX keeps it (matches the suffixed registration), but
                // the `$blanket$<trait>` target segment uses the BARE name.
                std::string full_tn(t.trait_name());
                std::string bare_tn = strip_trait_targ_suffix(full_tn);
                for (auto& bi : blanket_impls_) {
                    if (bi.trait_name != bare_tn) continue;
                    if (!tv_bound_set.count(bi.bound_trait)) continue;
                    bool all_extra = true;
                    for (auto& eb : bi.extra_bounds)
                        if (!tv_bound_set.count(eb)) { all_extra = false; break; }
                    if (!all_extra) continue;
                    std::string blanket_key = full_tn + "::$blanket$"
                        + bare_tn + "$" + bi.bound_trait
                        + "$" + bi.target_typevar
                        + "::" + std::string(t.assoc_type_name());
                    auto bait = assoc_type_impls_.find(blanket_key);
                    if (bait == assoc_type_impls_.end()) continue;
                    SemaSubst bsubst;
                    bsubst[bi.target_typevar] = subbed_base;
                    return subst_type_sema(bait->second.type, bsubst);
                }
            }
        }

        if (concrete) {
            std::string concrete_name = type_str(concrete);
            // Helper: build combined substitution (impl params + GAT params)
            auto make_subst = [&](const AssocTypeEntry& entry) -> SemaSubst {
                SemaSubst combined;
                for (size_t i = 0; i < entry.impl_type_params.size() &&
                                   i < TypeRef(concrete).type_args().size(); ++i)
                    combined[entry.impl_type_params[i].name] = TypeRef(concrete).type_args()[i];
                for (size_t i = 0; i < entry.gat_type_params.size() &&
                                   i < subbed_gat_args.size(); ++i)
                    combined[entry.gat_type_params[i].name] = subbed_gat_args[i];
                return combined;
            };

            // 1. Direct lookup (non-generic impls: key stored under concrete name).
            //    G156-1: find_assoc_type_entry prefers the trait-arg-suffixed key
            //    (dual `Trait<T>` impls) when args are known from the impl ctx.
            std::string tn(t.trait_name()), an(t.assoc_type_name());
            if (auto* e = find_assoc_type_entry(tn, concrete_name, an))
                return subst_type_sema(e->type, make_subst(*e));
            // 2. Base-name fallback (generic impls).
            std::string base_name = (TypeRef(concrete).kind() == LogosType::Kind::Struct)
                                    ? std::string(TypeRef(concrete).struct_name()) : "";
            if (!base_name.empty() && base_name != concrete_name) {
                if (auto* e2 = find_assoc_type_entry(tn, base_name, an))
                    return subst_type_sema(e2->type, make_subst(*e2));
            }
            // 3. Blanket-impl fallback: `impl<T: Bound> Trait for T` provides
            // `type Assoc = …`.  Use it when `concrete` satisfies Bound.
            std::string bare_tn3 = strip_trait_targ_suffix(tn);
            for (auto& bi : blanket_impls_) {
                if (bi.trait_name != bare_tn3) continue;
                // Concrete type must implement every bound of the blanket.
                auto bound_satisfied = [&](const std::string& bt) {
                    if (impls_.count(bt + "::" + concrete_name)) return true;
                    if (!base_name.empty() && base_name != concrete_name &&
                        impls_.count(bt + "::" + base_name)) return true;
                    return false;
                };
                if (!bound_satisfied(bi.bound_trait)) continue;
                bool all_extra = true;
                for (auto& eb : bi.extra_bounds)
                    if (!bound_satisfied(eb)) { all_extra = false; break; }
                if (!all_extra) continue;
                std::string blanket_key = tn + "::$blanket$"
                    + bare_tn3 + "$" + bi.bound_trait
                    + "$" + bi.target_typevar
                    + "::" + std::string(t.assoc_type_name());
                auto bait = assoc_type_impls_.find(blanket_key);
                if (bait == assoc_type_impls_.end()) continue;
                // Substitute the blanket's target typevar → concrete.
                SemaSubst bsubst;
                bsubst[bi.target_typevar] = concrete;
                return subst_type_sema(bait->second.type, bsubst);
            }
        }
        // B88: substitute GAT lifetime args.
        std::vector<std::string> subbed_lt_args;
        bool lt_changed = false;
        for (auto& lt : t.lifetime_args()) {
            auto it = ls.find(lt);
            if (it != ls.end()) { subbed_lt_args.push_back(it->second); lt_changed = true; }
            else                  subbed_lt_args.push_back(lt);
        }
        if (subbed_base != t.assoc_base() || gat_changed || lt_changed) {
            LogosTypeBuilder nt = t.to_builder();
            nt.assoc_base    = subbed_base;
            nt.gat_args      = std::move(subbed_gat_args);
            nt.lifetime_args = std::move(subbed_lt_args);
            return pool_->alloc(std::move(nt));
        }
        return t;
    }
    default: return t;
    }
}

// ── Type resolution ──────────────────────────────────────────────────────────

TypeRef SemaChecker::resolve_type_cfg_slot(TinyMapView node) {
    int32_t tc = code_of(node); (void)tc;
    // <type:CFG.path> — extract a type from a HermesStatic-typed binding
    // through an arbitrary path of field/index steps. Each step is an
    // AST item with OP discriminator (0=field_str, 1=field_int,
    // 2=array_idx). Two resolution paths:
    //   • CFG is a const-generic type-param of the enclosing item.
    //     Defer; mono_subst resolves once the param is bound.
    //   • CFG is a type alias to an HStaticLit (`pub type Cfg = @{…};`).
    //     Resolve eagerly by walking the registered LIR mirror.
    //
    // The path is encoded into assoc_type_name (string-typed slot we
    // already reuse on CfgSlotType) using a delimited form:
    //   "F<name>\x1F" | "I<int>\x1F" | "A<int>\x1F"  (one per step)
    // Decoded by mono_subst at concretisation time.
    auto cfg_name = std::string(str_of(node.get(la::NAME.code)));
    bool is_typeparam = current_type_params_.count(cfg_name) > 0;

    // B-ty-06: when CFG is a generic type-param, it must be a const-
    // generic of HermesStatic kind for cfg_slot extraction to make
    // sense.  Inspect current_type_params_[cfg_name] — for const params
    // push_type_params stores a ConstVar whose pointee is const_type.
    if (is_typeparam) {
        auto it = current_type_params_.find(cfg_name);
        if (it != current_type_params_.end()) {
            TypeRef tv = it->second;
            bool ok = TypeRef(tv).kind() == LogosType::Kind::ConstVar &&
                      TypeRef(tv).pointee() &&
                      TypeRef(TypeRef(tv).pointee()).kind() == LogosType::Kind::Struct &&
                      TypeRef(TypeRef(tv).pointee()).struct_name() == "HermesStatic";
            if (!ok) {
                error(std::format(
                    "'<type:{0}.…>': type-param '{0}' must be declared "
                    "as 'const {0}: HermesStatic' for cfg_slot extraction",
                    cfg_name));
            }
        }
    }

    // Read path steps from ITEMS array.
    struct Step {
        int kind;          // 0=field_str, 1=field_int, 2=array_idx
        std::string name;  // for kind=0
        int64_t  index;    // for kind=1, 2
    };
    std::vector<Step> steps;
    if (node.has_key(la::ITEMS)) {
        auto items_av = node.get(la::ITEMS.code);
        if (!items_av.is_null()) {
            auto items = arr_of(items_av);
            for (uint64_t i = 0; i < items.size(); ++i) {
                auto step_node = map_of(items.get(i));
                Step s{};
                if (step_node.has_key(la::OP)) {
                    auto opv = step_node.get(la::OP.code);
                    if (opv.is_value() && !opv.is_pointer())
                        s.kind = (int)opv.as_value<int32_t>();
                }
                if (s.kind == 0) {
                    s.name = std::string(str_of(step_node.get(la::NAME.code)));
                } else {
                    // INTEGER token comes through as a string (peg lexer).
                    if (step_node.has_key(la::INDEX)) {
                        auto sv = str_of(step_node.get(la::INDEX.code));
                        s.index = parse_int_literal(sv);
                    }
                }
                steps.push_back(std::move(s));
            }
        }
    }
    if (steps.empty()) {
        error("<type:CFG.path>: empty path");
        return error_t();
    }

    // Encode path for deferred resolution.
    auto encode = [&] {
        std::string r;
        for (auto& s : steps) {
            if (s.kind == 0) { r += 'F'; r += s.name; }
            else if (s.kind == 1) { r += 'I'; r += std::to_string(s.index); }
            else { r += 'A'; r += std::to_string(s.index); }
            r += '\x1F';
        }
        return r;
    };

    if (!is_typeparam) {
        // Eager resolution against an HStaticLit alias.
        TypeRef cfg_t = try_resolve_as_known_type(cfg_name);
        if (cfg_t && TypeRef(cfg_t).kind() == LogosType::Kind::HStaticLit && cur_prog_) {
            uint64_t hash = (uint64_t)cfg_t.const_val().value_or(0);
            auto rit = cur_prog_->hstatic_registry_.find(hash);
            if (rit != cur_prog_->hstatic_registry_.end() && rit->second &&
                rit->second->mirror_offset_ != hermes::arena_offset_t{}) {
                lir_view::ExprRef eref(cur_prog_->type_pool.arena(), rit->second->mirror_offset_);
                if (eref.kind() == lir_schema::expr::Code::HermesLit) {
                    // Walk path through the Hermes value.
                    lir_view::HermesValRef cur = lir_view::EHermesLitView{eref}.root();
                    bool ok = true;
                    for (auto& s : steps) {
                        using K = lir_schema::hermes_val::Code;
                        if (s.kind == 0 || s.kind == 1) {
                            if (cur.kind() != K::Map) { ok = false; break; }
                            auto map = lir_view::HVMapView{cur};
                            bool found = false;
                            if (s.kind == 0 && !map.int_keyed()) {
                                for (uint64_t i = 0, n = map.size(); i < n; ++i)
                                    if (map.str_key(i) == s.name) {
                                        cur = map.value(i); found = true; break;
                                    }
                            } else if (s.kind == 1 && map.int_keyed()) {
                                for (uint64_t i = 0, n = map.size(); i < n; ++i)
                                    if (map.int_key(i) == s.index) {
                                        cur = map.value(i); found = true; break;
                                    }
                            }
                            if (!found) { ok = false; break; }
                        } else { // s.kind == 2 — array
                            if (cur.kind() != K::Array) { ok = false; break; }
                            auto arr = lir_view::HVArrayView{cur};
                            if ((uint64_t)s.index >= arr.size()) { ok = false; break; }
                            cur = arr.elem((uint64_t)s.index);
                        }
                    }
                    if (ok && cur.kind() == lir_schema::hermes_val::Code::Type) {
                        std::string tname(lir_view::HVTypeView{cur}.name());
                        if (auto resolved = try_resolve_as_known_type(tname))
                            return resolved;
                    }
                }
            }
        }
    }
    LogosTypeBuilder t;
    t.kind = LogosType::Kind::CfgSlotType;
    t.type_var_name = cfg_name;       // CFG ident
    t.assoc_type_name = encode();     // encoded path
    return pool_->alloc(std::move(t));
}

TypeRef SemaChecker::resolve_type_assoc_ref(TinyMapView node) {
    int32_t tc = code_of(node); (void)tc;
    // base::Item or base::Item<A,B> — associated type reference (plain or GAT)
    auto base_type = resolve_type(map_of(node.get(la::RECEIVER.code)));
    auto assoc      = std::string(str_of(node.get(la::FIELD.code)));  // "Item"
    // Read GAT type args if present (e.g. T::Item<i32>)
    std::vector<TypeRef> gat_args;
    // B88: GAT lifetime args (e.g. T::Item<'a>) — separate from gat_args
    // (which holds type-position args) so they can be matched against
    // the trait's GAT lt-params at use-site validation.
    std::vector<std::string> gat_lt_args;
    if (node.has_key(la::TYPE_PARAMS)) {
        auto tpav = node.get(la::TYPE_PARAMS.code);
        if (!tpav.is_null()) {
            auto tplist = map_of(tpav);
            if (tplist.has_key(la::ITEMS)) {
                auto items = arr_of(tplist.get(la::ITEMS.code));
                for (uint64_t i = 0; i < items.size(); ++i) {
                    auto item = map_of(items.get(i));
                    if (code_of(item) == la::LIFETIME_PARAM) {
                        auto name_av = item.get(la::NAME.code);
                        if (!name_av.is_null()) {
                            std::string lt(str_of(name_av));
                            gat_lt_args.push_back(std::move(lt));
                        }
                        continue;
                    }
                    gat_args.push_back(resolve_type(item));
                }
            }
        }
    }
    std::string trait_for_assoc;
    // G156-1: the trait's concrete type-args at this projection site (from the
    // type-var bound or the current impl). Used to disambiguate two
    // `Trait<T>` impls for one type (each declaring the same assoc type).
    std::vector<TypeRef> trait_args_for_assoc;

    if (TypeRef(base_type).kind() == LogosType::Kind::TypeVar) {
        auto tp_name = TypeRef(base_type).type_var_name();
        if (tp_name == "Self" && !current_trait_name_.empty()) {
            trait_for_assoc = current_trait_name_;
        } else {
            auto bit = current_type_bounds_.find(tp_name);
            if (bit != current_type_bounds_.end()) {
                // Walk each bound trait AND its supertrait chain to find
                // the assoc type. A `Container: Datatype` bound pulls in
                // Datatype's `View` through the supertrait edge.
                std::vector<std::string> worklist;
                std::vector<std::vector<TypeRef>> worklist_args;  // parallel: bound's trait args
                StrSet seen;
                for (auto& b : bit->second) {
                    worklist.push_back(b.trait_name);
                    worklist_args.push_back(b.type_args);
                }
                while (!worklist.empty() && trait_for_assoc.empty()) {
                    std::string tn = std::move(worklist.back());
                    worklist.pop_back();
                    std::vector<TypeRef> targs = std::move(worklist_args.back());
                    worklist_args.pop_back();
                    if (!seen.insert(tn).second) continue;
                    auto tit = find_trait_iter_scoped(tn);
                    if (tit == traits_.end()) continue;
                    for (auto& at : tit->second.assoc_types) {
                        if (at.name == assoc) {
                            trait_for_assoc = tn;
                            trait_args_for_assoc = targs;
                            break;
                        }
                    }
                    if (!trait_for_assoc.empty()) break;
                    for (auto& sup : tit->second.supertraits) {
                        worklist.push_back(sup.trait_name);
                        worklist_args.push_back(sup.type_args);
                    }
                }
            }
        }
    } else if (TypeRef(base_type).kind() == LogosType::Kind::CfgSlotType) {
        // CfgSlotType base — type isn't known until mono substitutes
        // CFG. Resolve by assoc-name alone: pick the first trait that
        // declares an assoc type with this name. Mono's subst_type
        // for AssocType then resolves via concrete_impls_ /
        // blanket_impls_ once the base becomes concrete.
        for (auto& [tname, tinfo] : traits_) {
            for (auto& at : tinfo.assoc_types) {
                if (at.name == assoc) { trait_for_assoc = tname; break; }
            }
            if (!trait_for_assoc.empty()) break;
        }
    } else if (TypeRef(base_type).kind() == LogosType::Kind::AssocType) {
        // T::A::B — search bounds of the associated type itself if we had them,
        // but currently we only store trait_name for the assoc type.
        // We'll search the trait indicated by base_type's own resolution.
        auto tit = find_trait_iter_scoped(TypeRef(base_type).trait_name());
        if (tit != traits_.end()) {
            // This is slightly wrong: T::A might be bound to traits OTHER than the one it's defined in.
            // But our current system doesn't support "type Item: Bound;".
            // So we look in the trait that owns the associated type.
        }
        // Fallback: check all traits implemented by the concrete type if base is already concrete,
        // or just error if we can't find it.
    }

    // Phase 6: check the current impl's trait first. When `Self::Item<X>`
    // appears inside an impl method body (or signature), `Self` resolves
    // to the impl's target type (concrete struct, not TypeVar), and the
    // impl itself isn't yet in impls_ when collect_impl is still walking
    // method signatures. Look up the assoc-type definition on the
    // impl's trait directly.
    if (trait_for_assoc.empty() && !current_impl_trait_name_.empty()) {
        auto tit = find_trait_iter_scoped(current_impl_trait_name_);
        if (tit != traits_.end()) {
            for (auto& at : tit->second.assoc_types) {
                if (at.name == assoc) {
                    trait_for_assoc = current_impl_trait_name_;
                    // G156-1: inside `impl Trait<Args> for C`, `Self::Assoc`
                    // belongs to THIS impl's trait instantiation.
                    trait_args_for_assoc = current_impl_trait_args_;
                    break;
                }
            }
        }
    }
    if (trait_for_assoc.empty()) {
        // Check all traits for ANY type that might have this assoc type (last resort lookup).
        // Try both the full concrete name (e.g. "Box<i32>") and the base struct name ("Box")
        // to handle generic impls like impl<V> Trait for Box<V>.
        std::string cname = type_str(base_type);
        std::string base_name;
        if (TypeRef(base_type).kind() == LogosType::Kind::Struct ||
            TypeRef(base_type).kind() == LogosType::Kind::ZonedStruct)
            base_name = TypeRef(base_type).struct_name();

        for (auto& [tname, tinfo] : traits_) {
            bool found_impl = impls_.count(tname + "::" + cname) > 0
                           || (!base_name.empty() && impls_.count(tname + "::" + base_name) > 0);
            if (found_impl) {
                for (auto& at : tinfo.assoc_types) {
                    if (at.name == assoc) { trait_for_assoc = tname; break; }
                }
            }
            if (!trait_for_assoc.empty()) break;
        }
    }

    if (trait_for_assoc.empty()) {
        error(std::format("no associated type '{}' found for '{}'", assoc, type_str(base_type)));
        return error_t();
    }
    // Bug 5 fix: check GAT arity against the trait's declaration.
    auto tit_gat = traits_.find(trait_for_assoc);
    if (tit_gat != traits_.end()) {
        for (auto& at_def : tit_gat->second.assoc_types) {
            if (at_def.name == assoc) {
                size_t expected_gat = at_def.type_params.size();
                if (!at_def.type_params.empty() && gat_args.size() != expected_gat)
                    error(std::format("associated type '{}::{}' expects {} GAT argument(s), got {}",
                                      trait_for_assoc, assoc, expected_gat, gat_args.size()));
                // Enforce trait bounds on GAT type parameters.
                if (!at_def.type_params.empty() && gat_args.size() == expected_gat)
                    check_type_bounds(trait_for_assoc + "::" + assoc,
                                      at_def.type_params, gat_args);
                break;
            }
        }
    }
    // G156-1: when the base is already CONCRETE and the trait has type-args
    // (so two `Trait<T>` impls could declare the same-named assoc type),
    // resolve the projection NOW using the args from the impl/bound context.
    // Otherwise a deferred AssocType node {base, trait, name} would intern
    // identically across the two impls (it carries no trait args) and collapse
    // to one — the wrong one. Gated to generic traits (non-empty suffix); the
    // legacy deferred path is unchanged for non-generic traits and TypeVar
    // bases. The suffixed key is registered by collect_impl (ASSOC_TYPE_IMPL).
    if (gat_args.empty() &&
        TypeRef(base_type).kind() != LogosType::Kind::TypeVar &&
        TypeRef(base_type).kind() != LogosType::Kind::ConstVar &&
        TypeRef(base_type).kind() != LogosType::Kind::CfgSlotType &&
        TypeRef(base_type).kind() != LogosType::Kind::AssocType) {
        std::string sfx = trait_targ_suffix(trait_args_for_assoc);
        if (!sfx.empty()) {
            std::string cn = type_str(base_type);
            auto it = assoc_type_impls_.find(trait_for_assoc + sfx + "::" + cn + "::" + assoc);
            if (it == assoc_type_impls_.end() &&
                (TypeRef(base_type).kind() == LogosType::Kind::Struct ||
                 TypeRef(base_type).kind() == LogosType::Kind::ZonedStruct)) {
                std::string bn(TypeRef(base_type).struct_name());
                if (!bn.empty() && bn != cn)
                    it = assoc_type_impls_.find(trait_for_assoc + sfx + "::" + bn + "::" + assoc);
            }
            if (it != assoc_type_impls_.end()) {
                SemaSubst sub;
                for (size_t i = 0; i < it->second.impl_type_params.size() &&
                                   i < TypeRef(base_type).type_args().size(); ++i)
                    sub[it->second.impl_type_params[i].name] = TypeRef(base_type).type_args()[i];
                return subst_type_sema(it->second.type, sub);
            }
        }
    }
    LogosTypeBuilder t;
    t.kind            = LogosType::Kind::AssocType;
    t.assoc_base      = base_type;
    // G156-1: bake the trait's concrete type-args into the deferred node's
    // trait_name (e.g. "Producer$G1$i64") so a TypeVar-base projection like
    // `P::Item` for `P: Producer<i64>` resolves to the right impl once P is
    // substituted at mono — two `Trait<T>` impls would otherwise intern to one
    // identical node and collapse. Empty suffix (non-generic traits) leaves
    // trait_name bare, preserving legacy behaviour. Bare-name consumers strip
    // the suffix via strip_trait_targ_suffix().
    t.trait_name      = trait_for_assoc + trait_targ_suffix(trait_args_for_assoc);
    t.assoc_type_name = assoc;
    t.gat_args        = std::move(gat_args);
    // B88: stash GAT lifetime args on lifetime_args field — distinct
    // from struct lt args (AssocType doesn't have struct lt_args
    // semantics) so reusing the slot is safe and lets the existing
    // lifetime_args mirror accessor read them.
    t.lifetime_args   = std::move(gat_lt_args);

    auto result = pool_->alloc(std::move(t));
    // Propagate bounds for T::Item back into the context
    auto tit = traits_.find(trait_for_assoc);
    if (tit != traits_.end()) {
        for (auto& at : tit->second.assoc_types) {
            if (at.name == assoc && !at.bounds.empty()) {
                current_type_bounds_[type_str(result)] = at.bounds;
                break;
            }
        }
    }
    // Gap-4: if the base type-param carries an equality bound `Trait<A = V>`,
    // normalize `T::A` directly to V at resolution time (so annotations like
    // `fn f<T: Foo<A=i64>>(…) -> T::A` see the concrete type).
    if (auto norm = normalize_assoc_eq(result); norm != result) return norm;
    return result;
}

TypeRef SemaChecker::resolve_type_generic_inst(TinyMapView node) {
    int32_t tc = code_of(node); (void)tc;
    auto name = str_of(node.get(la::NAME.code));

    // Generic compile-time const: `pub const X<T1, T2>: HermesStatic =
    // @{...};`. Push type-args into current_type_params_ and re-resolve
    // the saved value-AST under that scope. resolve_hstatic_value walks
    // the AST and substitutes TypeVar HERMES_TYPE_LIT names through
    // current_type_params_, producing a fresh per-instantiation
    // HStaticLit identity.
    {
        auto git = generic_consts_.find(std::string(name));
        if (git != generic_consts_.end()) {
            std::vector<TypeRef> args;
            if (node.has_key(la::ITEMS)) {
                auto items = arr_of(node.get(la::ITEMS.code));
                for (uint64_t i = 0; i < items.size(); ++i)
                    args.push_back(resolve_type(map_of(items.get(i))));
            }
            if (args.size() != git->second.type_params.size()) {
                error(std::format("generic const '{}' expects {} type argument(s), got {}",
                                  name, git->second.type_params.size(), args.size()));
                return error_t();
            }
            // Save + push type-param bindings.
            StrMap<TypeRef> saved_params;
            for (size_t i = 0; i < args.size(); ++i) {
                const std::string& pname = git->second.type_params[i].name;
                auto it = current_type_params_.find(pname);
                if (it != current_type_params_.end()) saved_params[pname] = it->second;
                current_type_params_[pname] = args[i];
            }
            // Switch holder_ to the const decl's holder so arr_of/map_of
            // resolve offsets against the correct base. Restored after.
            auto* saved_holder = holder_;
            if (git->second.holder) holder_ = git->second.holder;
            TypeRef result = resolve_hstatic_value(git->second.value_node);
            holder_ = saved_holder;
            // Restore type-params.
            for (size_t i = 0; i < args.size(); ++i) {
                const std::string& pname = git->second.type_params[i].name;
                auto sit = saved_params.find(pname);
                if (sit != saved_params.end()) current_type_params_[pname] = sit->second;
                else current_type_params_.erase(pname);
            }
            return result;
        }
    }

    // Generic type alias: type Foo<T> = Bar<T>;  →  Foo<i32> resolves to Bar<i32>
    {
        auto ait = type_aliases_.find(std::string(name));
        if (ait != type_aliases_.end() &&
            (!ait->second.type_params.empty() || !ait->second.lifetime_params.empty())) {
            // Resolve type and lifetime arguments at the call site.
            std::vector<TypeRef> args;
            std::vector<std::string> lt_args;
            if (node.has_key(la::ITEMS)) {
                auto items = arr_of(node.get(la::ITEMS.code));
                for (uint64_t i = 0; i < items.size(); ++i) {
                    auto item = map_of(items.get(i));
                    if (code_of(item) == la::LIFETIME_PARAM) {
                        lt_args.push_back(std::string(str_of(item.get(la::NAME.code))));
                        continue;
                    }
                    args.push_back(resolve_type(item));
                }
            }
            size_t expected = ait->second.type_params.size();
            if (args.size() != expected)
                error(std::format("type alias '{}' expects {} type argument(s), got {}",
                                  name, expected, args.size()));
            size_t lt_expected = ait->second.lifetime_params.size();
            if (lt_args.size() != lt_expected)
                error(std::format("type alias '{}' expects {} lifetime argument(s), got {}",
                                  name, lt_expected, lt_args.size()));
            SemaSubst s;
            for (size_t i = 0; i < expected && i < args.size(); ++i)
                s[ait->second.type_params[i].name] = args[i];
            SemaLifetimeSubst ls;
            auto& lparams = ait->second.lifetime_params;
            for (size_t i = 0; i < lparams.size() && i < lt_args.size(); ++i)
                ls[lparams[i]] = lt_args[i];
            return subst_type_sema(ait->second.type, s, ls);
        }
    }

    // Smart-pointer-of-dyn = an OWNING trait object: Box<dyn>/Rc<dyn>/Arc<dyn>
    // all collapse to the same fat-pair {data,vtable} with identical dispatch,
    // differing only in release semantics (carried as the OwningKind). This is
    // the lang-item behaviour (Rust's owned_box + CoerceUnsized) — FQN-gated on
    // the name resolving to the stdlib package so a user struct of the same
    // name is NOT hijacked. The owning kind drives the kind-specific drop:
    // Box→free(data); Rc/Arc→dec strong, free RcInner at the last reference.
    {
        TypeRef::OwningKind sp_kind = TypeRef::OwningKind::Borrow;
        std::string_view sp_pkg;
        if      (name == "Box") { sp_kind = TypeRef::OwningKind::Box; sp_pkg = "logos.mem.boxed"; }
        else if (name == "Rc")  { sp_kind = TypeRef::OwningKind::Rc;  sp_pkg = "logos.mem.rc"; }
        else if (name == "Arc") { sp_kind = TypeRef::OwningKind::Arc; sp_pkg = "logos.mem.sync"; }
        if (sp_kind != TypeRef::OwningKind::Borrow && node.has_key(la::ITEMS) &&
            find_struct_by_name(name).first == sp_pkg) {
            auto items = arr_of(node.get(la::ITEMS.code));
            if (items.size() == 1) {
                // Box/Rc/Arc hold the value behind a pointer, so the inner type
                // may be unsized (`Box<dyn Trait>`, `Box<[T]>`). Probe under
                // unsized_ok_ so a bare `[T]` / `dyn` inner resolves here without
                // the unsized-by-value diagnostic (matches the ?Sized param).
                bool was_ok = unsized_ok_;
                unsized_ok_ = true;
                auto inner = resolve_type(map_of(items.get(0)));
                unsized_ok_ = was_ok;
                // TraitObject OR UnsizedDyn (the probe runs under unsized_ok_, so
                // bare `dyn Trait` now resolves to the unsized form — both denote
                // the same owning trait object).
                // B3 stage-2b FLIP: `Rc<dyn>` no longer collapses to an owning
                // trait object {data,vtable}; it resolves to the Rc STRUCT
                // {inner: *mut RcInner<dyn>} (the inner ptr becomes a fat DstRef
                // per the custom-DST machinery) by falling through to the normal
                // generic-struct path below. All generic methods (clone/drop/
                // deref) then run directly on the struct — no repr-aware
                // specials. Box<dyn> still collapses here (flip pending).
                if (sp_kind != TypeRef::OwningKind::Rc &&
                    sp_kind != TypeRef::OwningKind::Arc &&
                    inner && (TypeRef(inner).kind() == LogosType::Kind::TraitObject ||
                              TypeRef(inner).kind() == LogosType::Kind::UnsizedDyn)) {
                    TypeRef ti(inner);
                    std::vector<TypeRef> targs(ti.type_args().begin(), ti.type_args().end());
                    return make_trait_object(ti.trait_name(), std::move(targs), sp_kind,
                                             /*req_send=*/ti.trait_requires_send(),
                                             /*req_sync=*/ti.trait_requires_sync());
                }
                // `Box<[T]>` (and Rc/Arc<[T]>) — heap unsized slice. Collapse to
                // an OWNING fat slice {data,len}: same layout as `&[T]`, move-only,
                // droppable per sp_kind (Box→free+drop-elems). Mirrors the
                // Box<dyn> collapse above (CoerceUnsized lang-item behaviour).
                if (inner && TypeRef(inner).kind() == LogosType::Kind::UnsizedSlice &&
                    TypeRef(inner).elem() && sp_kind == TypeRef::OwningKind::Box)
                    return make_slice_type(TypeRef(inner).elem(), /*is_mut=*/false, sp_kind);
                // `Box<Foo>` where Foo is a custom-DST tail-slice struct — heap-
                // owned unsized struct. Collapse to an OWNING DstRef {data,len}:
                // same fat layout + field/tail access as `&Foo`, move-only,
                // droppable (drop tail+prefix, free block). Mirrors Box<[T]>.
                if (inner && sp_kind == TypeRef::OwningKind::Box &&
                    (TypeRef(inner).kind() == LogosType::Kind::Struct ||
                     TypeRef(inner).kind() == LogosType::Kind::ZonedStruct)) {
                    auto [ipkg, issi] = find_struct_by_name(
                        std::string(TypeRef(inner).struct_name()));
                    if (issi && issi->is_dst) {
                        auto ia = TypeRef(inner).type_args();
                        return make_dst_ref(TypeRef(inner).struct_name(),
                                            TypeRef(inner).pkg_name(), /*is_mut=*/false,
                                            std::vector<TypeRef>(ia.begin(), ia.end()),
                                            sp_kind);
                    }
                }
            }
        }
    }
    auto [spkg, ssi] = find_struct_by_name(name);
    auto [dpkg, dsi] = find_datatype_by_name(name);
    auto [epkg, esi] = find_enum_by_name(name);
    // Cross-pkg ambiguity: user's local `pub struct Foo` shadowing a
    // datatype `#[zoned] pub struct Foo` from an imported package
    // would otherwise lose to the datatype because the dispatch
    // below checks is_dtype before is_struct. Pin to cur_package_
    // when one kind is local and the other isn't.
    if (!cur_package_.empty()) {
        bool s_local = ssi && spkg == cur_package_;
        bool d_local = dsi && dpkg == cur_package_;
        bool e_local = esi && epkg == cur_package_;
        if (s_local && !d_local) { dsi = nullptr; }
        if (s_local && !e_local) { esi = nullptr; }
        if (d_local && !s_local) { ssi = nullptr; }
        if (d_local && !e_local) { esi = nullptr; }
        if (e_local && !s_local) { ssi = nullptr; }
        if (e_local && !d_local) { dsi = nullptr; }
    }
    bool is_struct = ssi != nullptr;
    bool is_dtype  = dsi != nullptr;
    bool is_enum   = esi != nullptr;
    if (!is_struct && !is_dtype && !is_enum) {
        // Metaprog discovery loop runs sema BEFORE handler hooks
        // emit derived items. Unknown types referenced from any
        // user-side ast may be ones a hook will synthesise — the
        // final, non-metaprog sema pass after the loop will
        // re-resolve and surface a real error if the type still
        // doesn't exist. Silently fall through here.
        if (metaprog_mode_)
            return error_t();
        error(std::format("unknown generic type '{}'", name));
        return error_t();
    }
    // Resolve each type arg (TypeVars in current scope are expanded).
    // Collect LIFETIME_PARAM items ('a) separately — erased at codegen but
    // tracked for borrow checking (struct fields that borrow through a lifetime).
    // Phase 1B-5: when the target type-param at index i has
    // `implicit_sized=false` (declared with `?Sized`), enable unsized_ok_
    // for that arg's resolution so bare `[T]` / `dyn Trait` parse without
    // the unsized-by-value diagnostic. The Sized-enforcement check below
    // catches the inverse case (unsized arg at sized param).
    const std::vector<TypeParam>* target_params =
        ssi ? &ssi->type_params :
        dsi ? &dsi->type_params :
        esi ? &esi->type_params : nullptr;
    std::vector<TypeRef> args;
    std::vector<std::string> lt_args;
    if (node.has_key(la::ITEMS)) {
        auto items = arr_of(node.get(la::ITEMS.code));
        size_t type_arg_idx = 0;  // separate index — lifetimes don't consume a param slot
        for (uint64_t i = 0; i < items.size(); ++i) {
            auto item = map_of(items.get(i));
            if (code_of(item) == la::LIFETIME_PARAM) {
                lt_args.push_back(std::string(str_of(item.get(la::NAME.code))));
                continue;
            }
            bool was_ok = unsized_ok_;
            if (target_params && type_arg_idx < target_params->size() &&
                !(*target_params)[type_arg_idx].implicit_sized) {
                unsized_ok_ = true;
            }
            args.push_back(resolve_type(item));
            unsized_ok_ = was_ok;
            ++type_arg_idx;
        }
    }
    // Default type arguments (Rust parity): when fewer type-args are named than
    // the generic has params, fill trailing params from their declared defaults
    // (`struct S<T, U = i64>` → `S<A>` ≡ `S<A, i64>`). A default may reference an
    // earlier param (`<T, U = T>`), so substitute the already-bound args.
    if (target_params && args.size() < target_params->size()) {
        SemaSubst dsubst;
        for (size_t i = 0; i < args.size(); ++i)
            dsubst[(*target_params)[i].name] = args[i];
        for (size_t i = args.size(); i < target_params->size(); ++i) {
            const TypeParam& tp = (*target_params)[i];
            if (!tp.default_type) break;   // no default → leave to the arity check
            TypeRef d = dsubst.empty() ? tp.default_type
                                       : subst_type_sema(tp.default_type, dsubst);
            args.push_back(d);
            dsubst[tp.name] = d;
        }
    }
    // Phase 1B-5/10: Sized-enforcement at struct/enum/datatype generic
    // instantiation. Parallel to the fn-call path in finish_generic_call.
    // Phase 1B-10 adds the TypeVar→Sized propagation check too.
    if (target_params) {
        for (size_t i = 0; i < args.size() && i < target_params->size(); ++i) {
            if (!(*target_params)[i].implicit_sized) continue;
            auto t = args[i];
            if (!t) continue;
            auto k = t.kind();
            if (k == LogosType::Kind::UnsizedSlice ||
                k == LogosType::Kind::UnsizedDyn) {
                error(std::format(
                    "generic '{}': type argument '{}' has unsized type `{}` "
                    "but the type parameter '{}' requires `Sized` "
                    "(add `T: ?Sized` to relax the bound)",
                    name, type_str(t), type_str(t),
                    (*target_params)[i].name));
            } else if (k == LogosType::Kind::TypeVar) {
                std::string tvname(t.type_var_name());
                if (current_type_relaxed_sized_.count(tvname)) {
                    error(std::format(
                        "generic '{}': type argument '{}' is a `?Sized` "
                        "outer type parameter; cannot be passed to '{}' "
                        "which requires `Sized` (add `?Sized` to the "
                        "target's bound or constrain the outer parameter "
                        "to `Sized`)",
                        name, tvname, (*target_params)[i].name));
                }
            }
        }
    }
    if (is_enum) {
        if (esi) {
            check_type_arg_arity(name, esi->type_params, args, "enum");
            check_type_bounds(std::string(name), esi->type_params, args);
        }
        return make_generic_enum(name, std::move(args), std::move(lt_args), epkg);
    }
    if (is_dtype) {
        if (dsi) {
            check_type_arg_arity(name, dsi->type_params, args, "datatype");
            check_type_bounds(std::string(name), dsi->type_params, args);
        }
        return make_generic_datatype(name, std::move(args), std::move(lt_args), dpkg);
    }
    if (ssi) {
        check_type_arg_arity(name, ssi->type_params, args, "struct");
        check_type_bounds(std::string(name), ssi->type_params, args);
    }
    return make_generic_struct(name, std::move(args), std::move(lt_args), spkg);
}

TypeRef SemaChecker::resolve_type(TinyMapView node) {
    int32_t tc = code_of(node);

    if (tc == la::ANTIQUOT_TYPE) {
        std::string nm;
        if (node.has_key(la::NAME)) nm = std::string(str_of(node.get(la::NAME.code)));
        error("`$" + nm + "` antiquotation is only valid inside `quote_ty! { ... }`");
        return error_t();
    }
    if (tc == la::ANTIQUOT_PACK) {
        std::string nm;
        if (node.has_key(la::NAME)) nm = std::string(str_of(node.get(la::NAME.code)));
        error("`$" + nm + "...` pack-splice is only valid inside `quote_ty! { ... }`");
        return error_t();
    }

    if (tc == la::TYPEOF_TYPE) {
        // typeof(expr) — lower the inner expression purely for type inference.
        // Expression is not evaluated at runtime; only its sema-computed type
        // is returned.  The LExpr we build is discarded after this call.
        auto expr_node = map_of(node.get(la::VALUE.code));
        auto lex = lower_expr(expr_node);
        if (!lex || !lex->type) return error_t();
        return lex->type;
    }

    if (tc == la::CFG_SLOT_TYPE) return resolve_type_cfg_slot(node);

    if (tc == la::PTR_TYPE) {
        bool mut = false;
        AnyVal mv = node.get(la::MUTPTR.code);
        if (!mv.is_null() && mv.is_value()) mut = mv.as_value<uint8_t>() != 0;
        // F3: `*zoned T` / `*zoned mut T` — the contextual `zoned` modifier rides
        // in NAME (grammar `STAR IDENT ... type_ref`). Validate it (the only legal
        // word after `*` besides the mut/const keywords) and carry it into make_ptr.
        bool zoned = false;
        if (node.has_key(la::NAME)) {
            std::string mod(str_of(node.get(la::NAME.code)));
            if (mod == "zoned") zoned = true;
            else error(std::format(
                "unknown raw-pointer modifier '*{}' — expected '*const', '*mut', "
                "or '*zoned'", mod));
        }
        auto inner = node.has_key(la::POINTEE)
                      ? resolve_type(map_of(node.get(la::POINTEE.code)))
                      : error_t();
        // A raw `*const/*mut dyn Trait` is a FAT pointer (Rust): same 16-byte
        // {data,vtable} representation as `&dyn`. Canonicalise the literal-`dyn`
        // pointee form to bare TraitObject (like REF_TYPE folds `&dyn`), so a
        // raw dyn pointer is the inline fat pair — DISTINCT from `*mut T`
        // (generic, mono'd to TraitObject → `Ptr<TraitObject>`, e.g. a Vec<&dyn>
        // buffer = a thin pointer to inline fat elements). Gated on the IMMEDIATE
        // pointee node being a bare `dyn`.
        if (node.has_key(la::POINTEE) &&
            code_of(map_of(node.get(la::POINTEE.code))) == la::DYN_TYPE &&
            inner && inner.kind() == LogosType::Kind::TraitObject)
            return inner;
        // Phase 1B-14: `*const DstStruct` / `*mut DstStruct` → DstRef
        // (fat pointer). Same canonicalisation as REF_TYPE for DST. Use
        // is_effective_dst (not the raw template `is_dst` flag) so a generic
        // struct whose tail type-param is substituted with an unsized type
        // (`Inner<dyn Tr>`, `Inner<[T]>`) is recognised as DST per-instance —
        // not just structs whose template tail is a literal slice.
        if (inner && is_effective_dst(inner)) {
            std::string sn(inner.struct_name());
            std::string spkg(inner.pkg_name());
            if (spkg.empty()) {
                auto [p, ssi] = find_struct_by_name(sn);
                if (ssi) spkg = p;
                else { auto [pd, dsi] = find_datatype_by_name(sn); if (dsi) spkg = pd; }
            }
            // A RAW pointer (`*mut/*const`) to a SELF-DESCRIBING DST stays THIN
            // (8B): its tail length/metadata is recoverable in-band at deref, so
            // the pointer need not carry it. (A non-self-describing DST — bare
            // `[T]` tail, e.g. Wrap<[u8]> — keeps a fat DstRef carrying the len;
            // and `&/&mut/Box<T>` stay DstRef regardless.) Pub-check-FREE lookup:
            // resolving a foreign DST pointer must not emit a visibility error.
            // ref-repr §6.
            SemaStructInfo* rssi = find_struct_repr_(spkg, sn);
            if (rssi && rssi->self_describing)
                return make_ptr(mut, inner, zoned);
            std::vector<TypeRef> targs = inner.type_args();
            return make_dst_ref(sn, spkg, mut, std::move(targs));
        }
        return make_ptr(mut, inner, zoned);
    }

    if (tc == la::REF_TYPE) {
        // A reference legitimately wraps an UNSIZED pointee (`&[T]`, `&dyn`) —
        // it's a fat pointer. Permit the bare `[T]` / `dyn` pointee here (the
        // canonicalisation below folds `&UnsizedSlice`→`Slice` etc.). Needed
        // because the impl-target grammar resolves `impl Tr for &[T]` through
        // ref_type (not type_ref's slice_type alt), so the pointee arrives as
        // UNSIZED_SLICE_TYPE. Gate on the IMMEDIATE pointee node being a bare
        // unsized form — otherwise `unsized_ok_` would leak into nested type-arg
        // resolution (`&Box<dyn>` would let `dyn` inside Box resolve as unsized
        // and break the Box<T: Sized> bound check).
        bool ref_pointee_unsized = false;
        if (node.has_key(la::POINTEE)) {
            int32_t pc = code_of(map_of(node.get(la::POINTEE.code)));
            ref_pointee_unsized = (pc == la::UNSIZED_SLICE_TYPE || pc == la::DYN_TYPE);
            // §6 Wave 9 — `&str` parses as REF_TYPE wrapping TYPE_REF{name="str"}.
            // Without the unsized-ok flag, `str` resolves to Sized Slice<u8>,
            // and the outer Ref then wraps it: `Ref<Slice<u8>>` = `&&[u8]`.
            // That's the silent-doubled-& shape that mismatched every
            // `&str = "literal"` site (S20 / §6 question-op-str root).
            // Let `str` resolve as unsized here so the canonicalisation
            // below folds `&UnsizedSlice<u8>` → `Slice<u8>` and matches
            // the str literal.
            if (pc == la::TYPE_REF) {
                auto pn = map_of(node.get(la::POINTEE.code));
                if (pn.has_key(la::NAME) && str_of(pn.get(la::NAME.code)) == "str")
                    ref_pointee_unsized = true;
            }
        }
        bool was_ok = unsized_ok_;
        if (ref_pointee_unsized) unsized_ok_ = true;
        auto inner = node.has_key(la::POINTEE)
                      ? resolve_type(map_of(node.get(la::POINTEE.code)))
                      : error_t();
        unsized_ok_ = was_ok;
        std::string lt;
        if (node.has_key(la::LIFETIME))
            lt = std::string(str_of(node.get(la::LIFETIME.code)));
        // Phase 1B-11: canonicalise `&UnsizedSlice<T>` → `Slice<T>` and
        // `&UnsizedDyn<Trait>` → `TraitObject<Trait>` at resolve time too
        // (not only at substitution per 1B-2). This is needed when an
        // impl-on-unsized method body refers to `&Self` literally — the
        // resolved type must be the canonical fat-pointer form, not a
        // nested Ref<Unsized>.
        if (inner && inner.kind() == LogosType::Kind::UnsizedSlice)
            return make_slice_type(inner.elem());
        if (inner && inner.kind() == LogosType::Kind::UnsizedDyn) {
            std::vector<TypeRef> args_vec = inner.type_args();
            return make_trait_object(inner.trait_name(), std::move(args_vec));
        }
        // Phase 1B-14: `&DstStruct` → Kind::DstRef (fat pointer to the
        // custom-DST struct). is_dst is on SemaStructInfo, looked up
        // by struct name.
        if (inner && is_effective_dst(inner)) {
            std::string sn(inner.struct_name());
            std::string spkg(inner.pkg_name());
            if (spkg.empty()) {
                auto [p, ssi] = find_struct_by_name(sn);
                if (ssi) spkg = p;
                else { auto [pd, dsi] = find_datatype_by_name(sn); if (dsi) spkg = pd; }
            }
            std::vector<TypeRef> targs = inner.type_args();
            return make_dst_ref(sn, spkg, /*is_mut=*/false, std::move(targs));
        }
        return make_ref(false, inner, std::move(lt));
    }

    if (tc == la::MUT_REF_TYPE) {
        // `&mut [T]` / `&mut dyn` — same gated unsized-pointee allowance as `&`.
        bool ref_pointee_unsized = false;
        if (node.has_key(la::POINTEE)) {
            int32_t pc = code_of(map_of(node.get(la::POINTEE.code)));
            ref_pointee_unsized = (pc == la::UNSIZED_SLICE_TYPE || pc == la::DYN_TYPE);
        }
        bool was_ok = unsized_ok_;
        if (ref_pointee_unsized) unsized_ok_ = true;
        auto inner = node.has_key(la::POINTEE)
                      ? resolve_type(map_of(node.get(la::POINTEE.code)))
                      : error_t();
        unsized_ok_ = was_ok;
        std::string lt;
        if (node.has_key(la::LIFETIME))
            lt = std::string(str_of(node.get(la::LIFETIME.code)));
        // Phase 1B-11: same canonicalisation for `&mut`.
        if (inner && inner.kind() == LogosType::Kind::UnsizedSlice)
            return make_slice_type(inner.elem(), /*is_mut=*/true);
        if (inner && inner.kind() == LogosType::Kind::UnsizedDyn) {
            std::vector<TypeRef> args_vec = inner.type_args();
            return make_trait_object(inner.trait_name(), std::move(args_vec));
        }
        // Phase 1B-14/15: `&mut DstStruct` → Kind::DstRef. Includes
        // post-substitution DST (generic `?Sized` instantiation).
        if (inner && is_effective_dst(inner)) {
            std::string sn(inner.struct_name());
            std::string spkg(inner.pkg_name());
            if (spkg.empty()) {
                auto [p, ssi] = find_struct_by_name(sn);
                if (ssi) spkg = p;
                else { auto [pd, dsi] = find_datatype_by_name(sn); if (dsi) spkg = pd; }
            }
            std::vector<TypeRef> targs = inner.type_args();
            return make_dst_ref(sn, spkg, /*is_mut=*/true, std::move(targs));
        }
        return make_ref(true, inner, std::move(lt));
    }

    // Sprint 6.2 / B-ty-07: `&&T` and `&&mut T` — lexer collapses `&&`.
    if (tc == la::DOUBLE_REF_TYPE) {
        auto inner = node.has_key(la::POINTEE)
                      ? resolve_type(map_of(node.get(la::POINTEE.code)))
                      : error_t();
        return make_ref(false, make_ref(false, inner));
    }
    if (tc == la::DOUBLE_REF_MUT_TYPE) {
        auto inner = node.has_key(la::POINTEE)
                      ? resolve_type(map_of(node.get(la::POINTEE.code)))
                      : error_t();
        return make_ref(false, make_ref(true, inner));
    }

    if (tc == la::SLICE_TYPE) {
        auto elem = node.has_key(la::TYPE) ? resolve_type(map_of(node.get(la::TYPE.code))) : error_t();
        bool is_mut = false;
        if (node.has_key(la::MUTPTR)) { AnyVal mv = node.get(la::MUTPTR.code); is_mut = !mv.is_null() && mv.is_value() && mv.as_value<uint8_t>() != 0; }
        return make_slice_type(elem, is_mut);
    }

    if (tc == la::UNSIZED_SLICE_TYPE) {
        // Phase 1B: bare `[T]` — unsized slice type. Valid only when the
        // surrounding context explicitly opts in via `unsized_ok_` (e.g. a
        // turbofish type argument bound for a `T: ?Sized` parameter — Phase
        // 1B-2). Any other position (function param/return type, local-var
        // type ascription, struct/enum field, type alias RHS, etc.) is a
        // hard error: unsized types have no size and cannot occupy value
        // positions. The `&[T]` / `*const [T]` / `*mut [T]` syntaxes are
        // handled by SLICE_TYPE above and never reach this branch.
        auto elem = node.has_key(la::TYPE)
            ? resolve_type(map_of(node.get(la::TYPE.code)))
            : error_t();
        if (!unsized_ok_) {
            error(std::format(
                "the type `[{}]` is unsized: it cannot be used by value. "
                "Wrap it in a reference (`&[{}]`) or pointer "
                "(`*const [{}]` / `*mut [{}]`).",
                type_str(elem), type_str(elem),
                type_str(elem), type_str(elem)));
            // Continue with the unsized type so downstream type-checking
            // can still produce useful diagnostics; the error above is the
            // load-bearing signal.
        }
        return make_unsized_slice_type(elem);
    }

    if (tc == la::PAREN_TYPE) {
        // B-ty-09: `(T)` — paren-wrapped type, structurally same as T.
        if (!node.has_key(la::TYPE)) return error_t();
        return resolve_type(map_of(node.get(la::TYPE.code)));
    }

    if (tc == la::TUPLE_TYPE) {
        // (A...) — variadic-arity tuple target for `impl<A...> Trait
        // for (A...)`. Represented as a Tuple with a single TypeVar
        // element naming the pack. Variadic-ness is recovered from
        // the surrounding impl's TypeParam.is_variadic flag.
        if (node.has_key(la::IS_VARIADIC) &&
            node.get(la::IS_VARIADIC.code).as_value<uint8_t>() != 0 &&
            node.has_key(la::NAME)) {
            std::string pack_name(str_of(node.get(la::NAME.code)));
            LogosTypeBuilder tv;
            tv.kind = LogosType::Kind::TypeVar;
            tv.type_var_name = pack_name;
            std::vector<TypeRef> elems;
            elems.push_back(pool_->alloc(std::move(tv)));
            return make_tuple_type(std::move(elems));
        }
        if (!node.has_key(la::ITEMS))
            return void_t();  // () = unit/void type
        std::vector<TypeRef> elems;
        auto items = arr_of(node.get(la::ITEMS.code));
        if (items.size() == 0) return void_t();
        for (uint64_t i = 0; i < items.size(); ++i)
            elems.push_back(resolve_type(map_of(items.get(i))));
        return make_tuple_type(std::move(elems));
    }

    if (tc == la::DYN_TYPE) {
        auto tname = std::string(str_of(node.get(la::NAME.code)));
        // C5-cl-04 slice: `dyn Fn(…)` / `dyn FnMut(…)` / `dyn FnOnce(…)` —
        // Logos's Fn-family isn't a registered trait (it's a bound-check
        // shortcut), but the trait-object layout for these matches the
        // existing Closure type exactly: {fn_ptr, env_ptr}. So we resolve
        // `dyn Fn*(...) -> R` directly to Kind::Closure, which gets the
        // existing call-via-fat-pointer dispatch + Box<Closure> layout for
        // free.
        if (tname == "Fn" || tname == "FnMut" || tname == "FnOnce") {
            LogosTypeBuilder t;
            t.kind = LogosType::Kind::Closure;
            if (node.has_key(la::PARAMS)) {
                auto params_node = map_of(node.get(la::PARAMS.code));
                if (params_node.has_key(la::ITEMS)) {
                    auto items = arr_of(params_node.get(la::ITEMS.code));
                    for (uint64_t i = 0; i < items.size(); ++i)
                        t.closure_params.push_back(resolve_type(map_of(items.get(i))));
                }
            }
            t.closure_ret = node.has_key(la::RET_TYPE)
                ? resolve_type(map_of(node.get(la::RET_TYPE.code)))
                : void_t();
            return pool_->alloc(std::move(t));
        }
        if (!traits_.count(tname))
            error(std::format("unknown trait '{}' in &dyn type", tname));
        // Optional type-args: &dyn Trait<T,…> — same shape as Struct<T,…>.
        // logos-core 2.4(c): the grammar now also collects per-bound AUTO_TRAIT_BOUND
        // (`+ Send`/`+ Sync`/…) and AUTO_LIFE_BOUND (`+ 'a`) nodes into the
        // SAME ITEMS array. Filter by CODE here to bucket them: type-args drive
        // the TraitObject's type_args; AUTO_TRAIT_BOUND folds into the bound
        // set we'll fold into TraitObject's const_val (Send / Sync bits); the
        // lifetime bound is recorded for future §2.1 region_infer wiring.
        std::vector<TypeRef> args;
        bool req_send = false, req_sync = false;
        if (node.has_key(la::ITEMS)) {
            auto items = arr_of(node.get(la::ITEMS.code));
            for (uint64_t i = 0; i < items.size(); ++i) {
                auto item = map_of(items.get(i));
                int32_t ic = code_of(item);
                // B63.3: HRTB binder sub-node (from `for<...>` prefix) gets
                // collected by $...; it has no CODE. Skip.
                if (ic < 0) continue;
                // L1: skip LIFETIME_PARAM at trait-arg position; lifetimes
                // aren't part of TypeUID for trait dispatch.
                if (ic == la::LIFETIME_PARAM) continue;
                // logos-core 2.4(c) bounds — filter out and collect.
                if (ic == la::AUTO_TRAIT_BOUND.code) {
                    auto bname = std::string(str_of(item.get(la::NAME.code)));
                    if (bname == "Send") req_send = true;
                    else if (bname == "Sync") req_sync = true;
                    // Other auto-traits (Copy, Unpin, ...) recorded as no-ops
                    // — extend the bit-field when they become load-bearing.
                    continue;
                }
                if (ic == la::AUTO_LIFE_BOUND.code) continue;  // lifetime bound — recorded by grammar but enforce-phase (§2.1) absent
                args.push_back(resolve_type(item));
            }
        }
        // Phase 1B-4: at unsized-ok positions (turbofish for `T: ?Sized`),
        // bare `dyn Trait` is the unsized form — distinct from the existing
        // fat-pointer Kind::TraitObject which represents `&dyn Trait`.
        // Substitution canonicalises `&UnsizedDyn` back to TraitObject,
        // matching the `&dyn Trait` grammar route. Outside unsized-ok
        // context, behaviour is unchanged (legacy: bare `dyn Trait` and
        // `&dyn Trait` both produce TraitObject; downstream sema rejects
        // bare-by-value when it matters).
        if (unsized_ok_)
            return make_unsized_dyn_type(tname, std::move(args));
        // P2-15: forming a `&dyn Trait` fat trait object requires Trait to be
        // object-safe (dyn-compatible). A non-object-safe method has no vtable
        // slot → dispatch would crash; reject at the type-resolution point.
        check_trait_object_safe(tname);
        // logos-core 2.4(c): pass the auto-trait bounds we extracted from
        // ITEMS so the TraitObject carries `+ Send` / `+ Sync` in its
        // const_val bits, enabling the unsize-coercion check downstream.
        return make_trait_object(tname, std::move(args),
                                 TraitOwningKind::Borrow,
                                 req_send, req_sync);
    }

    if (tc == la::TAGGED_TYPE) {
        // &tagged<TS> Trait — thin pointer with tag-based dispatch.
        // struct_name = tag system type name; trait_name = dispatched trait name.
        auto tname = std::string(str_of(node.get(la::NAME.code)));
        if (!traits_.count(tname))
            error(std::format("unknown trait '{}' in &tagged type", tname));
        // Resolve the tag system type (used to check it's a struct).
        TypeRef ts_type = nullptr;
        if (node.has_key(la::TYPE.code))
            ts_type = resolve_type(map_of(node.get(la::TYPE.code)));
        std::string ts_name = ts_type ? std::string(TypeRef(ts_type).struct_name()) : "";
        if (ts_name.empty())
            error("&tagged<TS> Trait: TS must be a concrete struct type");
        LogosTypeBuilder t;
        t.kind       = LogosType::Kind::TaggedPtr;
        t.struct_name = ts_name;   // tag system type name
        t.trait_name  = tname;     // dispatched trait name
        return pool_->alloc(std::move(t));
    }

    if (tc == la::IMPL_TYPE) {
        auto tname = std::string(str_of(node.get(la::NAME.code)));
        // g4/K5: at PARAMETER position, `impl <bound>` desugars to a fresh
        // synthetic generic type-param bounded by <bound> — an `impl Trait`
        // param is exactly a once-used generic. resolve_type captures the full
        // bound (Fn-family signature / generic args) the same way a type-param
        // bound does, via read_trait_bound_args. RETURN position (flag off)
        // keeps the dedicated ImplTrait handling below.
        if (impl_param_desugar_active_) {
            TypeParam tp;
            tp.name = "__impl_" + std::to_string(pending_impl_trait_params_.size());
            tp.implicit_sized = true;
            TraitBound tb;
            tb.trait_name = tname;
            read_trait_bound_args(node, tb);
            tp.bounds.push_back(std::move(tb));
            auto tv = make_typevar(tp.name);
            pending_impl_trait_params_.push_back(std::move(tp));
            return tv;
        }
        LogosTypeBuilder t;
        t.kind = LogosType::Kind::ImplTrait;
        t.struct_name = tname;  // reuse struct_name to store trait name
        return pool_->alloc(std::move(t));
    }

    if (tc == la::CLOSURE_TYPE) {
        LogosTypeBuilder t;
        t.kind = LogosType::Kind::Closure;
        if (node.has_key(la::PARAMS)) {
            auto params_node = map_of(node.get(la::PARAMS.code));
            if (params_node.has_key(la::ITEMS)) {
                auto items = arr_of(params_node.get(la::ITEMS.code));
                for (uint64_t i = 0; i < items.size(); ++i)
                    t.closure_params.push_back(resolve_type(map_of(items.get(i))));
            }
        }
        t.closure_ret = node.has_key(la::RET_TYPE)
            ? resolve_type(map_of(node.get(la::RET_TYPE.code)))
            : void_t();
        return pool_->alloc(std::move(t));
    }

    if (tc == la::FN_PTR_TYPE) {
        // fn(T1, T2) -> R — bare function pointer, single ptr in LLVM.
        // Reuse closure_params / closure_ret fields.
        LogosTypeBuilder t;
        t.kind = LogosType::Kind::FnPtr;
        if (node.has_key(la::PARAMS)) {
            auto params_node = map_of(node.get(la::PARAMS.code));
            if (params_node.has_key(la::ITEMS)) {
                auto items = arr_of(params_node.get(la::ITEMS.code));
                for (uint64_t i = 0; i < items.size(); ++i)
                    t.closure_params.push_back(resolve_type(map_of(items.get(i))));
            }
        }
        t.closure_ret = node.has_key(la::RET_TYPE)
            ? resolve_type(map_of(node.get(la::RET_TYPE.code)))
            : void_t();
        return pool_->alloc(std::move(t));
    }

    if (tc == la::LIT_INT) {
        auto sv = str_of(node.get(la::VALUE.code));
        LogosTypeBuilder t; t.kind = LogosType::Kind::IntLit;
        int64_t v = parse_int_literal(sv);
        if (node.has_key(la::LO_NEG)) {
            AnyVal av = node.get(la::LO_NEG.code);
            if (!av.is_null() && av.is_value() && av.as_value<uint8_t>() != 0)
                v = -v;
        }
        t.const_val = v;
        return pool_->alloc(std::move(t));
    }

    if (tc == la::ARR_TYPE) {
        auto elem = node.has_key(la::TYPE)
                     ? resolve_type(map_of(node.get(la::TYPE.code)))
                     : error_t();
        uint64_t n = 0;
        std::string symbolic;
        // MP-mc-01: `[T; metacall { <expr> }]` — array length via metacall
        // splice. Block tail expression evaluated by ctfe and the integer
        // result becomes the size. Logos's replacement for Rust's
        // const-eval at this position.
        if (node.has_key(la::BODY)) {
            auto blk = map_of(node.get(la::BODY.code));
            hermes::TinyMapView tail{};
            bool have_tail = false;
            if (blk.has_key(la::ITEMS)) {
                auto items = arr_of(blk.get(la::ITEMS.code));
                for (uint64_t i = items.size(); i-- > 0; ) {
                    auto s = map_of(items.get(i));
                    int32_t sc = code_of(s);
                    if ((sc == la::TAIL_EXPR || sc == la::EXPR_STMT) && s.has_key(la::VALUE)) {
                        tail = map_of(s.get(la::VALUE.code));
                        have_tail = true;
                        break;
                    }
                }
            }
            if (!have_tail) {
                error("metacall in array length must contain a single integer expression");
                return make_array(elem, 0, symbolic);
            }
            auto r = ctfe::eval_expr(tail, holder_);
            if (!r) {
                error(std::format("metacall in array length: {}", r.error().msg));
                return make_array(elem, 0, symbolic);
            }
            n = static_cast<uint64_t>(r.value().i);
            return make_array(elem, n, symbolic);
        }
        // [T; sizeof...(P)] — grammar's sizeof_pack alt encodes the pack form
        // as OP="sizeof" + NAME=<pack-ident>. Lower to a symbolic arr_size_var
        // "__sizeof_pack:P" that mono_subst resolves once P expands.
        if (node.has_key(la::OP) && node.has_key(la::NAME)) {
            auto op = std::string(str_of(node.get(la::OP.code)));
            auto pn = std::string(str_of(node.get(la::NAME.code)));
            if (op != "sizeof") {
                error(std::format("expected 'sizeof...(T)' in array size, got '{}...(T)'", op));
            } else {
                auto it = current_type_params_.find(pn);
                if (it == current_type_params_.end()) {
                    error(std::format("[T; sizeof...({})]: undefined type parameter", pn));
                } else {
                    symbolic = std::string("__sizeof_pack:") + pn;
                }
            }
            return make_array(elem, 0, symbolic);
        }
        if (node.has_key(la::SIZE)) {
            auto av = node.get(la::SIZE.code);
            auto parse_array_size = [](std::string_view sv) -> uint64_t {
                uint64_t r = 0;
                for (char c : sv) {
                    if (c >= '0' && c <= '9') r = r * 10 + (c - '0');
                    else break;
                }
                return r;
            };
            if (av.is_value()) {
                auto sv = str_of(av);
                // If sv starts with a digit, it's a literal size.
                if (!sv.empty() && std::isdigit(sv[0])) {
                    n = parse_array_size(sv);
                } else {
                    // Otherwise, it might be a symbolic constant parameter.
                    symbolic = std::string(sv);
                }
            } else if (av.is_pointer()) {
                // Safety fallback: if it's somehow a string object
                auto sv = str_of(av);
                if (!sv.empty() && std::isdigit(sv[0])) {
                    n = parse_array_size(sv);
                } else {
                    symbolic = std::string(sv);
                }
            }
        }
        return make_array(elem, n, symbolic);
    }

    if (tc == la::ASSOC_TYPE_REF) return resolve_type_assoc_ref(node);

    // <ElemType>[] and <K,V>{} — Hermes typed container type-expressions.
    // Resolved to a special Struct type: struct_name="HermesArr"/"HermesMap",
    // type_args[0] = elem/key type, type_args[1] = val type (map only).
    // The result type of an `as <T>[]` cast is always Hermes (owning zone).
    if (tc == la::HERMES_ARR_TYPE) {
        auto elem_name = str_of(node.get(la::TYPE.code));
        // Resolve element type — must be a known Hermes scalar type name.
        static const StrMap<const char*> arr_elem_map = {
            {"I8",  "ArrayI8"},  {"U8",  "ArrayU8"},
            {"I16", "ArrayI16"}, {"U16", "ArrayU16"},
            {"U32", "ArrayU32"}, {"I32", "ArrayI32"},
            {"I64", "ArrayI64"}, {"U64", "ArrayU64"},
            {"F32", "ArrayF32"}, {"F64", "ArrayF64"},
        };
        auto it = arr_elem_map.find(std::string(elem_name));
        if (it == arr_elem_map.end()) {
            error(std::format("<{}>[] type: unsupported element type '{}'; "
                              "supported: I8/U8/I16/U16/I32/U32/I64/U64/F32/F64",
                              elem_name, elem_name));
            return error_t();
        }
        // Resolve the underlying logos primitive type for the element.
        TypeRef elem_t = nullptr;
        if      (elem_name == "I8")  elem_t = prim(LogosType::Kind::I8);
        else if (elem_name == "U8")  elem_t = prim(LogosType::Kind::U8);
        else if (elem_name == "I16") elem_t = prim(LogosType::Kind::I16);
        else if (elem_name == "U16") elem_t = prim(LogosType::Kind::U16);
        else if (elem_name == "U32") elem_t = prim(LogosType::Kind::U32);
        else if (elem_name == "I32") elem_t = prim(LogosType::Kind::I32);
        else if (elem_name == "I64") elem_t = prim(LogosType::Kind::I64);
        else if (elem_name == "U64") elem_t = prim(LogosType::Kind::U64);
        else if (elem_name == "F32") elem_t = prim(LogosType::Kind::F32);
        else if (elem_name == "F64") elem_t = prim(LogosType::Kind::F64);
        else elem_t = error_t();
        // Result type: struct LogosType with special name "HermesArr".
        return make_generic_struct("HermesArr", {elem_t});
    }
    if (tc == la::HERMES_MAP_TYPE) {
        auto key_name = str_of(node.get(la::TYPE.code));
        auto val_name = node.has_key(la::RET_TYPE.code)
            ? str_of(node.get(la::RET_TYPE.code)) : std::string_view{"AnyVal"};
        // C6-fix1: removed "Varchar" — it was advertised as supported but key_t
        // resolution only handled "I32", producing silent error_t() for Varchar.
        static const StrMap<const char*> map_key_map = {
            {"I32", "I32"}, {"U32", "U32"}, {"I64", "I64"}, {"U64", "U64"},
        };
        if (map_key_map.find(std::string(key_name)) == map_key_map.end()) {
            error(std::format("<{},{}>" "{{}} type: unsupported key type '{}'; "
                              "supported: I32/U32/I64/U64", key_name, val_name, key_name));
            return error_t();
        }
        TypeRef key_t = nullptr;
        if      (key_name == "I32") key_t = prim(LogosType::Kind::I32);
        else if (key_name == "U32") key_t = prim(LogosType::Kind::U32);
        else if (key_name == "I64") key_t = prim(LogosType::Kind::I64);
        else if (key_name == "U64") key_t = prim(LogosType::Kind::U64);
        else key_t = error_t();
        TypeRef val_t = nullptr;
        if (val_name == "AnyVal") {
            val_t = make_struct_type("AnyVal");
        } else {
            // C6-fix2: emit error for unsupported val type (previously silent error_t()).
            error(std::format("<{},{}>" "{{}} type: unsupported val type '{}'; "
                              "supported: AnyVal", key_name, val_name, val_name));
            return error_t();
        }
        return make_generic_struct("HermesMap", {key_t, val_t});
    }

    if (tc == la::PACK_EXPAND) {
        // T... in type-arg position — refer to a variadic type parameter
        // currently in scope. Sema yields the TypeVar; mono expands it via
        // cur_packs_ at call sites that iterate type_args.
        auto name = std::string(str_of(node.get(la::NAME.code)));
        auto it = current_type_params_.find(name);
        if (it == current_type_params_.end()) {
            error(std::format("pack expand: undefined type parameter '{}'", name));
            return error_t();
        }
        return it->second;
    }

    if (tc == la::TYPE_REF) {
        auto name = str_of(node.get(la::NAME.code));
        // `_` as a type — Rust's placeholder for type inference (`let x:
        // Vec<_> = vec_new::<i32>();`). Sema renders it as `Kind::InferredType`
        // and `types_compatible` is permissive on either side; downstream
        // context (annotation-RHS unification, generic-arg inference) is
        // expected to resolve it (logos-core 1.3).
        if (name == "_") return inferred_t();
        if (name == "Self") {
            auto tvit = current_type_params_.find("Self");
            if (tvit != current_type_params_.end()) return tvit->second;
        }
        auto t = lookup_type_by_name(name);
        if (t) return t;
        // See #20 sister site below: in metaprog discovery loop, swallow
        // unknown-type silently across all asts — the type may be
        // synthesised by a hook later. Final non-metaprog sema pass
        // catches real errors.
        if (metaprog_mode_)
            return error_t();
        // Bug 4 fix: give a more informative error when a generic alias is used
        // without its required type arguments.
        auto ait = type_aliases_.find(std::string(name));
        if (ait != type_aliases_.end() &&
            (!ait->second.type_params.empty() || !ait->second.lifetime_params.empty()))
            error(std::format("generic type alias '{}' requires type arguments", name));
        else
            error(std::format("unknown type '{}'", name));
        return error_t();
    }

    if (tc == la::GENERIC_INST) return resolve_type_generic_inst(node);

    if (tc == la::LIT_HSTATIC) {
        // HermesStatic literal at type-arg position: Foo::<@{...}>.
        if (!node.has_key(la::VALUE)) {
            error("HermesStatic type-arg: missing literal payload");
            return error_t();
        }
        return resolve_hstatic_value(map_of(node.get(la::VALUE.code)));
    }
    // Bare hermes-lit codes also reach resolve_type when `pub const X:
    // HermesStatic = @{...}` is being recognised in collect_const — there
    // the value-AST is the unwrapped hermes_lit, not LIT_HSTATIC.
    if (tc == la::HERMES_MAP.code || tc == la::HERMES_ARRAY.code ||
        tc == la::HERMES_STR.code || tc == la::HERMES_INT.code ||
        tc == la::HERMES_NEG_INT.code || tc == la::HERMES_FLOAT.code ||
        tc == la::HERMES_BOOL.code || tc == la::HERMES_NULL.code) {
        return resolve_hstatic_value(node);
    }

    error(std::format("unexpected type node code {}", tc));
    return error_t();
}

TypeRef SemaChecker::resolve_hstatic_value(TinyMapView val_node) {
    // Identity = byte-hash over the AST (content only, position-free) so two
    // identical `@{...}` instances at different sites produce the same TypeRef.
    {
        // FNV-1a 64-bit hash, schema-aware (content only, position-free).
        // Walks the hermes_lit AST tree using each node CODE's known shape
        // — distinguishes string-valued (HERMES_INT/STR/FLOAT) from
        // map-valued (HERMES_ENTRY's VALUE) children, so identical content
        // at different source positions hashes to the same value.
        auto fnv_byte = [](uint64_t h, uint8_t b) {
            return (h ^ b) * 0x100000001b3ULL;
        };
        auto fnv_u64 = [&](uint64_t h, uint64_t x) {
            for (int i = 0; i < 8; ++i) { h = fnv_byte(h, (uint8_t)(x & 0xff)); x >>= 8; }
            return h;
        };
        auto fnv_str = [&](uint64_t h, std::string_view s) {
            h = fnv_u64(h, (uint64_t)s.size());
            for (char c : s) h = fnv_byte(h, (uint8_t)c);
            return h;
        };
        std::function<uint64_t(hermes::TinyMapView, uint64_t)> walk;
        walk = [&](hermes::TinyMapView n, uint64_t h) -> uint64_t {
            int32_t c = code_of(n);
            h = fnv_u64(h, (uint64_t)(int64_t)c);
            if (c == la::HERMES_MAP.code || c == la::HERMES_ARRAY.code) {
                if (n.has_key(la::ITEMS) && !n.get(la::ITEMS.code).is_null()) {
                    auto items = arr_of(n.get(la::ITEMS.code));
                    h = fnv_u64(h, (uint64_t)items.size());
                    // B-he-02: duplicate-key check at this layer (was only done
                    // for `pub const … = @{...}` via eval_static_hermes_lit;
                    // hstatic literals at type-arg position skipped through here
                    // unchecked).
                    if (c == la::HERMES_MAP.code) {
                        logos::compiler::StrSet seen_keys;
                        for (uint64_t i = 0; i < items.size(); ++i) {
                            auto entry = map_of(items.get(i));
                            if (code_of(entry) != la::HERMES_ENTRY.code) continue;
                            if (!entry.has_key(la::KEY)) continue;
                            auto raw = str_of(entry.get(la::KEY.code));
                            std::string key(raw);
                            if (key.size() >= 2 && key.front() == '"' && key.back() == '"')
                                key = key.substr(1, key.size() - 2);
                            if (key.empty()) continue;
                            if (!seen_keys.insert(key).second) {
                                error(std::format("duplicate key '{}' in Hermes map literal", key));
                            }
                        }
                    }
                    for (uint64_t i = 0; i < items.size(); ++i)
                        h = walk(map_of(items.get(i)), h);
                }
            } else if (c == la::HERMES_ENTRY.code) {
                if (n.has_key(la::KEY))
                    h = fnv_str(h, str_of(n.get(la::KEY.code)));
                if (n.has_key(la::VALUE))
                    h = walk(map_of(n.get(la::VALUE.code)), h);
                if (n.has_key(la::LO_NEG)) {
                    auto av = n.get(la::LO_NEG.code);
                    if (av.is_value() && av.as_value<uint8_t>() != 0) h = fnv_byte(h, 1);
                }
            } else if (c == la::HERMES_INT.code || c == la::HERMES_NEG_INT.code ||
                       c == la::HERMES_FLOAT.code || c == la::HERMES_STR.code) {
                if (n.has_key(la::VALUE))
                    h = fnv_str(h, str_of(n.get(la::VALUE.code)));
            } else if (c == la::HERMES_BOOL.code) {
                if (n.has_key(la::VALUE)) {
                    auto av = n.get(la::VALUE.code);
                    if (av.is_value() && av.as_value<uint8_t>() != 0) h = fnv_byte(h, 1);
                }
            } else if (c == la::HERMES_TYPE_LIT.code) {
                // 3a': grammar feeds a simple_type child via TYPE — resolve
                // it with current_type_params_ in scope and hash the
                // canonical type_str. That subsumes the legacy NAME-only
                // path since type-param substitution flows through
                // resolve_type → lookup_type_by_name.
                if (n.has_key(la::TYPE)) {
                    auto type_node = map_of(n.get(la::TYPE.code));
                    TypeRef t = resolve_type(type_node);
                    if (t) h = fnv_str(h, type_str(t));
                } else if (n.has_key(la::NAME)) {
                    // Legacy AST shape (kept for safety; pre-3a' grammar).
                    auto nm = str_of(n.get(la::NAME.code));
                    auto pit = current_type_params_.find(std::string(nm));
                    if (pit != current_type_params_.end() && pit->second) {
                        h = fnv_str(h, type_str(pit->second));
                    } else {
                        h = fnv_str(h, nm);
                    }
                }
            }
            // HERMES_NULL / unknown: code-only contribution (already mixed in).
            return h;
        };
        uint64_t hash = walk(val_node, 0xcbf29ce484222325ULL);
        // Register the lowered @-literal so mono can materialise it in
        // place of `__const_param:CFG` references inside generic bodies.
        // First-write-wins: identical hashes resolve to the same registered
        // LExpr (content-only identity).
        if (cur_prog_ && !cur_prog_->hstatic_registry_.count(hash)) {
            auto lit = lower_hermes_lit(val_node);
            if (lit) cur_prog_->hstatic_registry_[hash] = lit;
        }
        LogosTypeBuilder t; t.kind = LogosType::Kind::HStaticLit;
        t.const_val = (int64_t)hash;  // bit-pattern reuse; mangling reads it as u64
        return pool_->alloc(std::move(t));
    }
}

// ── Lowering helpers ─────────────────────────────────────────────────────────

// Match `concrete` against `pattern`, binding TypeVars.  Minimal mirror of
// mono's match_type — only the cases we need for struct-spec selection.
static bool match_type_sema(TypeRef c, TypeRef p,
                            StrMap<TypeRef>& bindings) {
    if (!c || !p) return false;
    if (p.kind() == LogosType::Kind::TypeVar) {
        auto it = bindings.find(p.type_var_name());
        if (it != bindings.end()) return types_equal(c, TypeRef(it->second));
        bindings[std::string(p.type_var_name())] = c;
        return true;
    }
    if (p.kind() != c.kind()) return false;
    switch (p.kind()) {
    case LogosType::Kind::Ptr:
        return p.mut_ptr() == c.mut_ptr() &&
               match_type_sema(c.pointee(), p.pointee(), bindings);
    case LogosType::Kind::Ref:
    case LogosType::Kind::MutRef:
        return match_type_sema(c.pointee(), p.pointee(), bindings);
    case LogosType::Kind::Array:
        return p.arr_size() == c.arr_size() &&
               match_type_sema(c.elem(), p.elem(), bindings);
    case LogosType::Kind::Struct:
    case LogosType::Kind::ZonedStruct:
        return p.struct_name() == c.struct_name();
    default:
        return types_equal(c, p);
    }
}

static int specificity_sema(TypeRef t) {
    if (!t || t.kind() == LogosType::Kind::TypeVar) return 0;
    if (t.kind() == LogosType::Kind::Ptr)   return 1 + specificity_sema(t.pointee());
    if (t.kind() == LogosType::Kind::Array) return 1 + specificity_sema(t.elem());
    return 100;
}

// Find the most specific spec in struct_specs_sema_ whose patterns match
// `type_args` under template `base_name`.  Returns nullptr if none match.
const SemaChecker::SemaStructInfo* SemaChecker::find_best_sema_struct_spec(
        std::string_view base_name,
        const std::vector<TypeRef>& type_args) {
    const SemaStructInfo* best      = nullptr;
    std::vector<int>      best_vec;
    for (auto& [key, info] : struct_specs_sema_) {
        if (info.base_name != base_name) continue;
        if (info.spec_patterns.size() != type_args.size()) continue;
        StrMap<TypeRef> binds;
        bool ok = true;
        std::vector<int> scores(type_args.size());
        for (size_t i = 0; i < type_args.size(); ++i) {
            if (!match_type_sema(type_args[i], info.spec_patterns[i], binds)) { ok = false; break; }
            scores[i] = specificity_sema(info.spec_patterns[i]);
        }
        if (!ok) continue;
        // Lexicographic comparison: prefer higher specificity at earlier positions.
        if (!best || scores > best_vec) { best = &info; best_vec = scores; }
    }
    return best;
}

TypeRef SemaChecker::field_type_of(std::string_view sname, std::string_view fname,
                                             std::string_view pkg_hint) {
    SemaStructInfo* si = nullptr;
    // If we have a pkg_hint, try the fully-qualified key first (avoids import-scope dependence).
    if (!pkg_hint.empty()) {
        auto qkey = sema_key(std::string(pkg_hint), std::string(sname));
        auto sit = structs_.find(qkey);
        if (sit != structs_.end()) si = &sit->second;
        if (!si) { auto dit = datatypes_.find(qkey); if (dit != datatypes_.end()) si = &dit->second; }
    }
    if (!si) { auto [pkg, ssi] = find_struct_by_name(sname); si = ssi; }
    if (!si) { auto [pkg, dsi] = find_datatype_by_name(sname); si = dsi; }
    if (!si) return nullptr;
    for (auto& f : si->fields) {
        if (f.name == fname) return f.type;
        if (f.is_variadic && fname.starts_with(f.name) && fname.size() > f.name.size() + 1 && fname[f.name.size()] == '_')
            return f.type;
    }
    return nullptr;
}

TypeRef SemaChecker::field_type_of_for_type(TypeRef struct_t,
                                             std::string_view fname) {
    if (!struct_t || (TypeRef(struct_t).kind() != LogosType::Kind::Struct &&
                      TypeRef(struct_t).kind() != LogosType::Kind::ZonedStruct)) return nullptr;
    // Check for a concrete specialization first (including partial specs
    // via pattern matching).
    if (!TypeRef(struct_t).type_args().empty()) {
        if (auto* spec = find_best_sema_struct_spec(TypeRef(struct_t).struct_name(), TypeRef(struct_t).type_args())) {
            for (auto& f : spec->fields) {
                if (f.name == fname) return f.type;
                if (f.is_variadic && fname.starts_with(f.name) && fname.size() > f.name.size() + 1 && fname[f.name.size()] == '_')
                    return f.type;
            }
            return nullptr;  // field not in specialization
        }
    }
    auto raw = field_type_of(TypeRef(struct_t).struct_name(), fname, TypeRef(struct_t).pkg_name());
    if (!raw || TypeRef(struct_t).type_args().empty()) return raw;

    // If it's a variadic expansion (name_N), we need to resolve it against the type arguments.
    if (fname.find('_') != std::string::npos) {
        SemaStructInfo* si2 = nullptr;
        { auto [pkg, ssi] = find_struct_by_name(TypeRef(struct_t).struct_name()); si2 = ssi; }
        if (!si2) { auto [pkg, dsi] = find_datatype_by_name(TypeRef(struct_t).struct_name()); si2 = dsi; }
        if (si2) {
            for (auto& f : si2->fields) {
                if (f.is_variadic && fname.starts_with(f.name) && fname.size() > f.name.size() + 1 && fname[f.name.size()] == '_') {
                    size_t idx = std::stoull(std::string(fname.substr(f.name.size() + 1)));
                    if (f.type && TypeRef(f.type).kind() == LogosType::Kind::TypeVar) {
                        for (size_t i = 0, arg_idx = 0; i < si2->type_params.size(); ++i) {
                            if (si2->type_params[i].is_variadic) {
                                if (si2->type_params[i].name == TypeRef(f.type).type_var_name()) {
                                    if (arg_idx + idx < TypeRef(struct_t).type_args().size())
                                        return TypeRef(struct_t).type_args()[arg_idx + idx];
                                }
                                break;
                            } else {
                                arg_idx++;
                            }
                        }
                    }
                    return raw;
                }
            }
        }
    }

    // Bug 5: look up structs_ OR datatypes_ for the substitution info.
    // Try bare name first (same-package/unqualified), then pkg_name-qualified key.
    SemaStructInfo* si2 = nullptr;
    { auto it = structs_.find(TypeRef(struct_t).struct_name()); if (it != structs_.end()) si2 = &it->second; }
    if (!si2) { auto it = datatypes_.find(TypeRef(struct_t).struct_name()); if (it != datatypes_.end()) si2 = &it->second; }
    if (!si2 && !TypeRef(struct_t).pkg_name().empty()) {
        auto qkey = sema_key(TypeRef(struct_t).pkg_name(), TypeRef(struct_t).struct_name());
        { auto it = structs_.find(qkey); if (it != structs_.end()) si2 = &it->second; }
        if (!si2) { auto it = datatypes_.find(qkey); if (it != datatypes_.end()) si2 = &it->second; }
    }
    if (!si2) return raw;
    SemaSubst subst;
    auto& tps2 = si2->type_params;
    for (size_t i = 0, j = 0; i < tps2.size() && j < TypeRef(struct_t).type_args().size(); ++i) {
        if (tps2[i].is_variadic) break;
        subst[tps2[i].name] = TypeRef(struct_t).type_args()[j++];
    }
    // Bug 4: build lifetime substitution so &'z T fields resolve to caller's lifetime.
    SemaLifetimeSubst ls;
    auto& lps = si2->lifetime_params;
    for (size_t i = 0; i < lps.size() && i < TypeRef(struct_t).lifetime_args().size(); ++i)
        ls[lps[i]] = TypeRef(struct_t).lifetime_args()[i];
    return subst_type_sema(raw, subst, ls);
}

// ── lower_program and lower_module_items ─────────────────────────────────────

void SemaChecker::lower_program(const std::vector<hermes::Hermes>& asts, lir::LProgram& prog) {
    using namespace ast;
    const bool phase_dbg = []{
        const char* e = std::getenv("LOGOS_SEMA_PHASE_TIMING");
        return e && e[0] && e[0] != '0';
    }();
    auto t_phase = std::chrono::steady_clock::now();
    auto tick = [&](const char* l) {
        if (!phase_dbg) return;
        auto now = std::chrono::steady_clock::now();
        auto us = std::chrono::duration_cast<std::chrono::microseconds>(now - t_phase).count();
        std::fprintf(stderr, "[sema-lower] %-24s %6ld us\n", l, (long)us);
        t_phase = now;
    };
    // B64: variance fixed-point — runs after collect_*, before any lower_fn
    // (the subtype check at return / arg / let-init sites consults the table).
    compute_variances();
    tick("variances");
    // Per-ast timing accumulators (only populated when phase_dbg).
    int64_t total_binary_us  = 0;
    int64_t total_user_us    = 0;
    size_t  count_binary     = 0;
    size_t  count_user       = 0;
    int64_t max_binary_us    = 0;
    int64_t max_user_us      = 0;
    // M5 step 6: LIR bundle install + capture state.
    //   m5_bundle_active: bundle was valid at start of run() and spliced
    //     into prog before the loop; binary ASTs skip lower_module_items.
    //   m5_capture_active: bundle was NOT valid → this run is the populator;
    //     we record per-binary-AST ranges in unflagged prog vectors so the
    //     bundle can be assembled after the loop + re-attachment pass.
    const char* m5_lir_off_env = std::getenv("LOGOS_M5_LIR_OFF");
    const bool  m5_lir_off     = m5_lir_off_env && m5_lir_off_env[0] && m5_lir_off_env[0] != '0';
    const bool m5_bundle_active = cache_ && cache_->impl()->lir_bundle.valid && !m5_lir_off;
    // M6.1: in keep_user_state mode the bundle accumulates incrementally
    // across sema_lower calls — every call captures its own delta-asts'
    // contributions and appends to the bundle. In default mode the bundle
    // is captured-once-on-first-call (current Step 6 behavior).
    const bool m5_keep_user = cache_ && cache_->impl()->keep_user_state;
    const bool m5_capture_active = cache_ && !m5_lir_off &&
        (m5_keep_user || !cache_->impl()->lir_bundle.valid);
    if (m5_bundle_active) {
        // Splice the bundle into prog up-front. Per-AST loop will skip
        // binary ASTs entirely (their contribution is already in prog).
        const auto& c = cache_->impl()->lir_bundle;
        prog.structs.insert                (prog.structs.end(),                c.structs.begin(),                c.structs.end());
        prog.struct_specializations.insert (prog.struct_specializations.end(), c.struct_specializations.begin(), c.struct_specializations.end());
        prog.enums.insert                  (prog.enums.end(),                  c.enums.begin(),                  c.enums.end());
        prog.functions.insert              (prog.functions.end(),              c.functions.begin(),              c.functions.end());
        prog.specializations.insert        (prog.specializations.end(),        c.specializations.begin(),        c.specializations.end());
        prog.consts.insert                 (prog.consts.end(),                 c.consts.begin(),                 c.consts.end());
        prog.type_aliases.insert           (prog.type_aliases.end(),           c.type_aliases.begin(),           c.type_aliases.end());
        prog.traits.insert                 (prog.traits.end(),                 c.traits.begin(),                 c.traits.end());
        prog.impls.insert                  (prog.impls.end(),                  c.impls.begin(),                  c.impls.end());
        prog.inst_annotations.insert       (prog.inst_annotations.end(),       c.inst_annotations.begin(),       c.inst_annotations.end());
        prog.dispatch_entries.insert       (prog.dispatch_entries.end(),       c.dispatch_entries.begin(),       c.dispatch_entries.end());
        prog.module_inner_docs.insert      (prog.module_inner_docs.end(),      c.module_inner_docs.begin(),      c.module_inner_docs.end());
        for (auto& r : c.reflect_requests) prog.reflect_requests.insert(r);
    }
    // Per-binary-AST range tracking for unflagged vectors (when capturing).
    // Indices into unflagged prog vectors right after each binary AST's
    // lower_module_items. Used to filter binary contributions out of the
    // post-loop prog state for bundling.
    struct BinaryAstRange {
        size_t structs_b = 0, structs_e = 0;
        size_t struct_specs_b = 0, struct_specs_e = 0;
        size_t functions_b = 0, functions_e = 0;
        size_t specializations_b = 0, specializations_e = 0;
        size_t enums_b = 0, enums_e = 0;
        size_t consts_b = 0, consts_e = 0;
        size_t type_aliases_b = 0, type_aliases_e = 0;
        size_t traits_b = 0, traits_e = 0;
        size_t impls_b = 0, impls_e = 0;
        size_t inst_annotations_b = 0, inst_annotations_e = 0;
        size_t dispatch_entries_b = 0, dispatch_entries_e = 0;
        size_t module_inner_docs_b = 0, module_inner_docs_e = 0;
        bool   is_binary = true;
    };
    std::vector<BinaryAstRange> m5_ranges;
    StrSet m5_refl0;
    if (m5_capture_active) m5_refl0 = prog.reflect_requests;
    for (size_t i = 0; i < asts.size(); ++i) {
        // M6.1: delta mode — this AST was processed in a prior sema_lower
        // call within this compile session. Its prog contributions are
        // expected to already be in `prog` (either via bundle splice for
        // binary, or via cache's keep_user_state for user content) — no
        // need to re-walk it.
        if (i < delta_start_idx_) continue;
        auto _ast_t0 = phase_dbg
            ? std::chrono::steady_clock::now()
            : std::chrono::steady_clock::time_point{};
        cur_ast_idx_ = i;
        holder_ = asts[i].holder();
        file_ = (filenames_ && i < filenames_->size()) ? (*filenames_)[i] : std::string{};
        cur_from_binary_ = (from_binary_ && i < from_binary_->size()) ? (*from_binary_)[i] : false;
        cur_from_lazy_   = (is_lazy_     && i < is_lazy_->size())     ? (*is_lazy_)[i]     : false;
        auto root = asts[i].root_object().as_tiny_map();
        cur_root_ = root;
        cur_package_ = read_package_name(root);
        // M5 step 6: bundle hit → binary AST is already in prog; skip the
        // entire lower walk for it.
        if (m5_bundle_active && cur_from_binary_) {
            if (phase_dbg) {
                auto _ast_t1 = std::chrono::steady_clock::now();
                auto us = std::chrono::duration_cast<std::chrono::microseconds>(_ast_t1 - _ast_t0).count();
                total_binary_us += us;
                ++count_binary;
                if (us > max_binary_us) max_binary_us = us;
            }
            continue;
        }
        // Record per-AST baseline sizes for unflagged vectors so the
        // bundle assembler can pick out this AST's contributions.
        // M6.1: in keep_user_state mode, capture user-AST contributions too.
        BinaryAstRange rng{};
        const bool capture_this_ast =
            m5_capture_active &&
            (cur_from_binary_ || (cache_ && cache_->impl()->keep_user_state));
        if (capture_this_ast) {
            rng.structs_b           = prog.structs.size();
            rng.struct_specs_b      = prog.struct_specializations.size();
            rng.functions_b         = prog.functions.size();
            rng.specializations_b   = prog.specializations.size();
            rng.enums_b             = prog.enums.size();
            rng.consts_b            = prog.consts.size();
            rng.type_aliases_b      = prog.type_aliases.size();
            rng.traits_b            = prog.traits.size();
            rng.impls_b             = prog.impls.size();
            rng.inst_annotations_b  = prog.inst_annotations.size();
            rng.dispatch_entries_b  = prog.dispatch_entries.size();
            rng.module_inner_docs_b = prog.module_inner_docs.size();
        }
        // Rebuild import scope (same logic as in collect()) so find_*_by_name
        // works during lowering for cross-package type lookups.
        cur_imports_ = {};
        if (root.has_key(USES)) {
            auto uses_av = root.get(USES.code);
            if (!uses_av.is_null() && uses_av.is_pointer()) {
                auto uses = arr_of(uses_av);
                for (uint64_t ui = 0; ui < uses.size(); ++ui) {
                    auto use_node = map_of(uses.get(ui));
                    int32_t use_code = USE.code;
                    if (use_node.has_key(CODE)) {
                        auto cv = use_node.get(CODE.code);
                        if (!cv.is_null() && !cv.is_pointer())
                            use_code = cv.as_value<int32_t>();
                    }
                    std::string dotted;
                    if (use_node.has_key(NAME))
                        dotted = std::string(str_of(use_node.get(NAME.code)));
                    if (use_node.has_key(mod::PATH_PARTS)) {
                        auto parts = arr_of(use_node.get(mod::PATH_PARTS.code));
                        for (uint64_t pi = 0; pi < parts.size(); ++pi) {
                            auto part = map_of(parts.get(pi));
                            if (!part.has_key(NAME)) continue;
                            if (!dotted.empty()) dotted += '.';
                            dotted += std::string(str_of(part.get(NAME.code)));
                        }
                    }
                    // GR-gp-02: `use pkg.{a, b, c};` is parsed as
                    // USE_VARIANTS with a lowercase TYPE_NAME. When the
                    // TYPE_NAME starts with a lowercase letter, desugar
                    // to N wildcard imports `<dotted>.<TYPE_NAME>.<item>`;
                    // uppercase TYPE_NAME is the real enum-variant form
                    // handled below.
                    if (use_code == USE_VARIANTS.code && use_node.has_key(TYPE_NAME)) {
                        std::string tn(str_of(use_node.get(TYPE_NAME.code)));
                        if (!tn.empty() && tn[0] >= 'a' && tn[0] <= 'z') {
                            std::string prefix = dotted.empty()
                                ? tn : (dotted + "." + tn);
                            if (use_node.has_key(VARIANTS)) {
                                auto vlist_av = use_node.get(VARIANTS.code);
                                if (!vlist_av.is_null() && vlist_av.is_pointer()) {
                                    auto vlist = arr_of(vlist_av);
                                    for (uint64_t vi = 0; vi < vlist.size(); ++vi) {
                                        auto v = map_of(vlist.get(vi));
                                        if (!v.has_key(NAME)) continue;
                                        auto bare = std::string(str_of(v.get(NAME.code)));
                                        cur_imports_.wildcard_packages.push_back(
                                            prefix + "." + bare);
                                    }
                                }
                            }
                            continue;
                        }
                    }
                    // CP-cm-02: record bare-variant aliases (mirrors the
                    // collect-pass build_import_scope in sema_collect.cpp).
                    if (use_code == USE_VARIANTS.code &&
                        use_node.has_key(VARIANTS)) {
                        auto vlist_av = use_node.get(VARIANTS.code);
                        if (!vlist_av.is_null() && vlist_av.is_pointer()) {
                            std::string enum_qual;
                            if (use_node.has_key(TYPE_NAME))
                                enum_qual = std::string(
                                    str_of(use_node.get(TYPE_NAME.code)));
                            auto vlist = arr_of(vlist_av);
                            for (uint64_t vi = 0; vi < vlist.size(); ++vi) {
                                auto v = map_of(vlist.get(vi));
                                if (!v.has_key(NAME)) continue;
                                auto bare = std::string(
                                    str_of(v.get(NAME.code)));
                                cur_imports_.variant_aliases[bare] = enum_qual;
                            }
                        }
                    }
                    if (!dotted.empty())
                        cur_imports_.wildcard_packages.push_back(std::move(dotted));
                }
            }
        }
        // Three-layer split Phase 3.4: same implicit-prelude injection as
        // sema_collect.cpp's maybe_inject_implicit_prelude. Source-side
        // ASTs only; binary archives skip (their producer already applied
        // its own prelude). Self-import and explicit-use dedup.
        if (!implicit_prelude_.empty() && !cur_from_binary_
            && cur_package_ != implicit_prelude_) {
            bool opt_out = false;
            if (root.has_key(la::ITEMS)) {
                auto items = arr_of(root.get(la::ITEMS.code));
                for (uint64_t ii = 0; ii < items.size(); ++ii) {
                    auto it = map_of(items.get(ii));
                    if (code_of(it) != la::INNER_ANNOTATION.code) continue;
                    if (!it.has_key(NAME)) continue;
                    if (str_of(it.get(NAME.code)) == "no_implicit_prelude") {
                        opt_out = true; break;
                    }
                }
            }
            if (!opt_out &&
                std::find(cur_imports_.wildcard_packages.begin(),
                          cur_imports_.wildcard_packages.end(),
                          implicit_prelude_)
                == cur_imports_.wildcard_packages.end()) {
                cur_imports_.wildcard_packages.push_back(implicit_prelude_);
            }
        }
        lower_module_items(root, prog);
        if (capture_this_ast) {
            rng.is_binary           = cur_from_binary_;
            rng.structs_e           = prog.structs.size();
            rng.struct_specs_e      = prog.struct_specializations.size();
            rng.functions_e         = prog.functions.size();
            rng.specializations_e   = prog.specializations.size();
            rng.enums_e             = prog.enums.size();
            rng.consts_e            = prog.consts.size();
            rng.type_aliases_e      = prog.type_aliases.size();
            rng.traits_e            = prog.traits.size();
            rng.impls_e             = prog.impls.size();
            rng.inst_annotations_e  = prog.inst_annotations.size();
            rng.dispatch_entries_e  = prog.dispatch_entries.size();
            rng.module_inner_docs_e = prog.module_inner_docs.size();
            m5_ranges.push_back(rng);
        }
        if (phase_dbg) {
            auto _ast_t1 = std::chrono::steady_clock::now();
            auto us = std::chrono::duration_cast<std::chrono::microseconds>(_ast_t1 - _ast_t0).count();
            if (cur_from_binary_) {
                total_binary_us += us;
                ++count_binary;
                if (us > max_binary_us) max_binary_us = us;
            } else {
                total_user_us += us;
                ++count_user;
                if (us > max_user_us) max_user_us = us;
            }
            if (us >= 3000) {  // also flag outliers (≥3ms)
                std::fprintf(stderr,
                    "[sema-lower]   ast[%zu] %s file='%s' %ldus\n",
                    i, cur_from_binary_ ? "(bin)" : "(usr)",
                    file_.c_str(), (long)us);
            }
        }
    }
    if (phase_dbg) {
        std::fprintf(stderr,
            "[sema-lower] per-ast: binary=%zu/%lldus(max=%lld) user=%zu/%lldus(max=%lld)\n",
            count_binary, (long long)total_binary_us, (long long)max_binary_us,
            count_user, (long long)total_user_us, (long long)max_user_us);
        // How many from_binary fn bodies were skeleton-skipped this run
        // (their symbol is in a linked .o, so the body is forward-declared
        // + linked rather than lowered).
        std::fprintf(stderr,
            "[sema-lower] skel_skip_count=%zu\n", skel_skip_count_);
    }
    cur_package_ = {};
    cur_imports_ = {};

    // ── Impl-method re-attachment pass ─────────────────────────────
    // lower_impl_block tries to attach methods to their target struct's
    // template by scanning prog.structs at impl-lowering time. When the
    // struct is in a *later-processed* ast (typical for derive-emitted
    // structs in synth docs appended at the end), the lookup misses
    // and methods land in prog.functions. mono's
    // struct_method_templates_ is built from struct.methods only, so
    // those orphaned methods are never cloned for concrete struct
    // instantiations → dyn vtable lookup fails.
    //
    // Walk prog.functions for impl-method-shaped names
    // (`<Struct>__<method>__[fg]__<sig>`) whose <Struct> exists as a
    // generic template in prog.structs. Move into struct.methods.
    {
        // Bare struct name → all generic templates of that name (there may be
        // same-named generic templates in distinct packages, e.g. stdlib
        // `Rc<T>` vs a user struct also named `Rc`). We disambiguate by package
        // at lookup so a user struct's impl method is NEVER mis-hosted into a
        // stdlib generic template of the same bare name (which would drop it
        // from prog.functions AND from the user struct's own emission → a `&user
        // Rc as &dyn Trait` vtable slot then references an un-emitted method →
        // SIGSEGV). Fixes user-struct-shadows-stdlib (Rc/Arc/Box/Vec/…).
        std::unordered_map<std::string, std::vector<lir::LStructDef*>> templates_by_name;
        for (auto& sd : prog.structs) {
            if (sd.type_params.empty()) continue;
            templates_by_name[sd.name].push_back(&sd);
        }
        auto is_impl_method_shape = [](std::string_view nm) -> std::string_view {
            if (auto dot = nm.rfind('.'); dot != std::string_view::npos)
                nm.remove_prefix(dot + 1);
            auto sep = nm.find("__");
            if (sep == std::string_view::npos) return {};
            // Require __f__ or __g__ further along — distinguishes impl
            // methods from coincidentally-named free fns.
            if (nm.find("__f__", sep) == std::string_view::npos &&
                nm.find("__g__", sep) == std::string_view::npos)
                return {};
            auto base = nm.substr(0, sep);
            if (base.empty() || base[0] == '$') return {};
            return base;
        };
        std::vector<lir::LFunctionPtr> kept;
        kept.reserve(prog.functions.size());
        for (auto& fp : prog.functions) {
            if (!fp) continue;
            auto base = is_impl_method_shape(fp->name);
            lir::LStructDef* host = nullptr;
            if (!base.empty()) {
                auto it = templates_by_name.find(std::string(base));
                if (it != templates_by_name.end()) {
                    // Only adopt the method into a generic template in the SAME
                    // package as the method (declared in `impl … for <Struct>`).
                    // A cross-package bare-name match (user `Rc` vs stdlib
                    // `Rc<T>`) must NOT move it. Prefer exact-pkg; if the method
                    // carries no package, fall back to a sole candidate.
                    for (auto* cand : it->second)
                        if (cand->pkg == fp->package) { host = cand; break; }
                    if (!host && fp->package.empty() && it->second.size() == 1)
                        host = it->second.front();
                }
            }
            if (host) host->methods.push_back(std::move(fp));
            else      kept.push_back(std::move(fp));
        }
        prog.functions = std::move(kept);
    }

    // ── M5 step 6: bundle-assembly (populator run only) ─────────────
    // Walk the assembled prog and copy binary-origin items into the
    // cache bundle. For LStructDef/LFunctionPtr we use the
    // `from_binary_module` flag (survives re-attachment and matches
    // both prog.functions orphans and struct.methods early-bindings).
    // For unflagged vectors (impls, enums, consts, ...) we use the
    // per-binary-AST [b,e) ranges captured during the loop — these
    // vectors are append-only across the loop, so original positions
    // are still valid post-re-attachment.
    if (m5_capture_active) {
        auto& b = cache_->impl()->lir_bundle;
        const bool keep_user = cache_ && cache_->impl()->keep_user_state;
        // M6.1: in keep_user_state mode the bundle accumulates across
        // sema_lower calls. Structs/struct_specs/fns/specializations are
        // captured via the original Step 6 path (iterate prog with
        // flag-relaxed filter — keep_user accepts user content too) and
        // since the iteration covers all of prog (bundle splice + this
        // call's deltas), we CLEAR these vectors first to avoid the
        // splice contents being re-captured as duplicates. This keeps
        // capture-after-re-attachment semantics so cross-AST early-binding
        // is reflected naturally. Cost: ~3000 shared_ptr refcount-bumps
        // per call (~1-2ms).
        //
        // Unflagged vectors (impls/enums/consts/...) use per-AST ranges,
        // which capture only this call's deltas — these APPEND to the
        // existing bundle. *_binary_end is set only on the first capture
        // (binary ranges emitted first); subsequent calls (keep_user) add
        // user deltas which don't extend the binary region.
        if (keep_user) {
            b.structs.clear();
            b.struct_specializations.clear();
            b.functions.clear();
            b.specializations.clear();
        }
        auto take_methods = [keep_user](const std::vector<lir::LFunctionPtr>& src) {
            std::vector<lir::LFunctionPtr> dst;
            dst.reserve(src.size());
            for (auto& m : src)
                if (m && (keep_user || m->from_binary_module)) dst.push_back(m);
            return dst;
        };
        for (auto& sd : prog.structs) {
            if (!keep_user && !sd.from_binary_module) continue;
            auto copy = sd;
            copy.methods = take_methods(sd.methods);
            b.structs.push_back(std::move(copy));
        }
        for (auto& sd : prog.struct_specializations) {
            if (!keep_user && !sd.from_binary_module) continue;
            auto copy = sd;
            copy.methods = take_methods(sd.methods);
            b.struct_specializations.push_back(std::move(copy));
        }
        for (auto& fp : prog.functions)
            if (fp && (keep_user || fp->from_binary_module)) b.functions.push_back(fp);
        for (auto& fp : prog.specializations)
            if (fp && (keep_user || fp->from_binary_module)) b.specializations.push_back(fp);
        // Unflagged vectors: per-AST range emit. Binary first (to set the
        // boundary), then user. On non-first call (keep_user mode only,
        // bundle already valid), the boundary stays at its first-call value.
        const bool first_capture = !b.valid;
        auto emit_range = [&](const BinaryAstRange& r) {
            for (size_t k = r.enums_b; k < r.enums_e; ++k)
                b.enums.push_back(prog.enums[k]);
            for (size_t k = r.consts_b; k < r.consts_e; ++k)
                b.consts.push_back(prog.consts[k]);
            for (size_t k = r.type_aliases_b; k < r.type_aliases_e; ++k)
                b.type_aliases.push_back(prog.type_aliases[k]);
            for (size_t k = r.traits_b; k < r.traits_e; ++k)
                b.traits.push_back(prog.traits[k]);
            for (size_t k = r.impls_b; k < r.impls_e; ++k)
                b.impls.push_back(prog.impls[k]);
            for (size_t k = r.inst_annotations_b; k < r.inst_annotations_e; ++k)
                b.inst_annotations.push_back(prog.inst_annotations[k]);
            for (size_t k = r.dispatch_entries_b; k < r.dispatch_entries_e; ++k)
                b.dispatch_entries.push_back(prog.dispatch_entries[k]);
            for (size_t k = r.module_inner_docs_b; k < r.module_inner_docs_e; ++k)
                b.module_inner_docs.push_back(prog.module_inner_docs[k]);
        };
        for (auto& r : m5_ranges) if (r.is_binary) emit_range(r);
        if (first_capture) {
            b.enums_binary_end             = b.enums.size();
            b.consts_binary_end            = b.consts.size();
            b.type_aliases_binary_end      = b.type_aliases.size();
            b.traits_binary_end            = b.traits.size();
            b.impls_binary_end             = b.impls.size();
            b.inst_annotations_binary_end  = b.inst_annotations.size();
            b.dispatch_entries_binary_end  = b.dispatch_entries.size();
            b.module_inner_docs_binary_end = b.module_inner_docs.size();
        }
        for (auto& r : m5_ranges) if (!r.is_binary) emit_range(r);
        // reflect_requests: capture diff against pre-loop snapshot. Filter
        // to entries whose fqn starts with a binary pkg name; we don't have
        // that mapping at hand, so for now capture nothing (binary code
        // typically doesn't use the reflect intrinsic at top level).
        (void)m5_refl0;
        b.valid = true;
    }
}

// Parse one annotation literal AST node into an LAnnotationValue.
// Handles LIT_INT, LIT_FLOAT, LIT_BOOL, LIT_STR, ENUM_LIT, ANNOT_ARR.
lir::LAnnotationValue SemaChecker::parse_annot_literal(TinyMapView v) {
    using Kind = lir::LAnnotationValue::Kind;
    lir::LAnnotationValue out;
    int32_t c = code_of(v);
    if (c == la::LIT_INT) {
        out.kind = Kind::Int;
        auto sv = str_of(v.get(la::VALUE.code));
        out.i = parse_int_literal(sv);
    } else if (c == la::LIT_FLOAT) {
        out.kind = Kind::Float;
        std::string s(str_of(v.get(la::VALUE.code)));
        s.erase(std::remove(s.begin(), s.end(), '_'), s.end());
        // strip optional f32/f64 suffix
        if (s.size() > 3 && (s.ends_with("f32") || s.ends_with("f64")))
            s.resize(s.size() - 3);
        out.f = std::stod(s);
    } else if (c == la::LIT_BOOL) {
        out.kind = Kind::Bool;
        AnyVal av = v.get(la::VALUE.code);
        bool b = !av.is_null() && av.is_value() && av.as_value<uint8_t>() != 0;
        out.i = b ? 1 : 0;
    } else if (c == la::LIT_STR) {
        out.kind = Kind::Str;
        auto raw = str_of(v.get(la::VALUE.code));
        // Strip surrounding quotes (raw form may lack them for r"..." — handle both).
        std::string inner;
        if (!raw.empty() && (raw.front() == '"')) {
            inner.assign(raw.substr(1, raw.size() - 2));
            // Unescape common escapes (same set used elsewhere in the compiler).
            std::string dec;
            dec.reserve(inner.size());
            for (size_t i = 0; i < inner.size(); ++i) {
                if (inner[i] == '\\' && i + 1 < inner.size()) {
                    switch (inner[++i]) {
                    case 'n':  dec += '\n'; break;
                    case 't':  dec += '\t'; break;
                    case 'r':  dec += '\r'; break;
                    case '\\': dec += '\\'; break;
                    case '"':  dec += '"';  break;
                    case '0':  dec += '\0'; break;
                    default:   dec += '\\'; dec += inner[i]; break;
                    }
                } else {
                    dec += inner[i];
                }
            }
            out.s = std::move(dec);
        } else if (!raw.empty() && raw.starts_with("r\"")) {
            // r"..." raw string: strip r" ... "
            out.s.assign(raw.substr(2, raw.size() - 3));
        } else {
            out.s = std::string(raw);
        }
    } else if (c == la::ENUM_LIT) {
        out.kind = Kind::Enum;
        out.enum_name = std::string(str_of(v.get(la::NAME.code)));
        out.enum_variant = std::string(str_of(v.get(la::FIELD.code)));
    } else if (c == la::ANNOT_ARR) {
        out.kind = Kind::Array;
        if (v.has_key(la::ITEMS)) {
            auto items = arr_of(v.get(la::ITEMS.code));
            for (uint64_t i = 0; i < items.size(); ++i)
                out.arr.push_back(parse_annot_literal(map_of(items.get(i))));
        }
    }
    return out;
}

// Given an annotation AST node whose NAME resolves to a `#[annotation]` datatype,
// build an LAnnotationInstance.  Positional arguments are matched to fields in
// declaration order; named arguments (ANNOT_KV) match by field name.  Missing
// fields are left unset.  Unknown field names / type mismatches emit an error
// but do not abort.
std::optional<lir::LAnnotationInstance>
SemaChecker::build_annotation_instance(TinyMapView ann,
                                       std::string_view ann_name,
                                       std::string_view ann_pkg,
                                       const SemaStructInfo& ann_info) {
    lir::LAnnotationInstance inst;
    inst.ann_name = std::string(ann_name);
    inst.ann_pkg  = std::string(ann_pkg);
    inst.ann_fqn  = ann_pkg.empty() ? std::string(ann_name)
                                    : std::string(ann_pkg) + "::" + std::string(ann_name);

    // Case 1: #[A] — no args.
    if (!ann.has_key(la::ARGS) && !ann.has_key(la::VALUE)) return inst;

    // Case 2: #[A = lit] — single positional, maps to first field.
    if (ann.has_key(la::VALUE)) {
        auto v = map_of(ann.get(la::VALUE.code));
        if (ann_info.fields.empty()) {
            error(std::format("annotation '{}' takes no arguments", ann_name));
            return inst;
        }
        inst.kv.emplace_back(std::string(ann_info.fields[0].name), parse_annot_literal(v));
        return inst;
    }

    // Case 3: #[A(args...)] — iterate arg list.
    auto args_map = map_of(ann.get(la::ARGS.code));
    if (!args_map.has_key(la::ITEMS)) return inst;
    auto items = arr_of(args_map.get(la::ITEMS.code));
    size_t pos_idx = 0;
    for (uint64_t i = 0; i < items.size(); ++i) {
        auto arg = map_of(items.get(i));
        int32_t ac = code_of(arg);
        if (ac == la::ANNOT_KV) {
            auto key = std::string(str_of(arg.get(la::NAME.code)));
            // Validate the field exists on the annotation datatype.
            bool found = false;
            for (auto& f : ann_info.fields) if (f.name == key) { found = true; break; }
            if (!found) {
                error(std::format("annotation '{}' has no field '{}'", ann_name, key));
                continue;
            }
            auto val = map_of(arg.get(la::VALUE.code));
            inst.kv.emplace_back(std::move(key), parse_annot_literal(val));
        } else if (ac == la::ANNOT_POS) {
            if (pos_idx >= ann_info.fields.size()) {
                error(std::format("annotation '{}' takes at most {} positional args",
                                  ann_name, ann_info.fields.size()));
                break;
            }
            auto val = map_of(arg.get(la::VALUE.code));
            inst.kv.emplace_back(std::string(ann_info.fields[pos_idx].name),
                                 parse_annot_literal(val));
            ++pos_idx;
        }
        // Legacy {NAME: $1} bare-ident form has no CODE key — ignore for user annotations.
    }
    return inst;
}

void SemaChecker::lower_module_items(TinyMapView mod, lir::LProgram& prog) {
    if (!mod.has_key(la::ITEMS)) return;
    auto items = arr_of(mod.get(la::ITEMS.code));

    cur_prog_ = &prog;

    // Annotations accumulate until the next non-annotation item, then are consumed.
    std::vector<TinyMapView> pending_annots;

    auto apply_annots_to_struct = [&](lir::LStructDef& sd) {
        for (auto& ann : pending_annots) {
            auto aname = std::string(str_of(ann.get(la::NAME.code)));
            if (aname == "type_code" && ann.has_key(la::VALUE)) {
                sd.type_code = read_annotation_u64(ann);
                // Cache with fully-qualified key so type_code_of::<T>() works
                // across packages.  Bare sd.name would collide if two packages
                // define the same struct name with different type_codes.
                auto fqn = cur_package_.empty() ? sd.name
                                                 : cur_package_ + "::" + sd.name;
                explicit_type_codes_[fqn] = sd.type_code;
            } else if (aname == "annotation") {
                // Marker: this datatype is itself a user-annotation declaration.
                sd.is_annotation_type = true;
            } else {
                // User annotation: NAME must resolve to a registered `#[annotation]` datatype.
                auto [pkg, info] = find_datatype_by_name(aname);
                if (info && info->is_annotation_type) {
                    if (auto inst = build_annotation_instance(ann, aname, pkg, *info))
                        sd.annotations.push_back(std::move(*inst));
                }
                // Unknown annotations silently ignored (future compiler-internal
                // keys, or forward-declared not-yet-seen).
            }
        }
    };

    // Apply the STRUCTURAL boolean flags (#[zone_mut], #[zoned2], #[rel_ptr],
    // #[self_describing], #[borrow_carrying]) from the pending annotations onto an
    // LStructDef. For regular structs these flow collect → structs_ → lower_struct_def;
    // SPECIALISATIONS bypass structs_ (lower_spec_struct builds the LStructDef directly),
    // so without this they silently lose every struct-level attribute — e.g. a
    // `#[zone_mut] struct HMap<HString,V>` spec would clone with zone_mut=false, making
    // ref_repr_of treat `&mut` as thin and corrupting the zone-carrying receiver.
    auto apply_struct_flags = [&](lir::LStructDef& sd) {
        for (auto& ann : pending_annots) {
            auto aname = std::string(str_of(ann.get(la::NAME.code)));
            if      (aname == "zone_mut")        sd.zone_mut        = true;
            else if (aname == "zoned2")          sd.zoned2          = true;
            else if (aname == "rel_ptr")         sd.rel_ptr         = true;
            else if (aname == "self_describing") sd.self_describing = true;
            else if (aname == "borrow_carrying") sd.borrow_carrying = true;
        }
    };

    auto apply_annots_to_trait = [&](lir::LTraitDef& td) {
        for (auto& ann : pending_annots) {
            auto aname = std::string(str_of(ann.get(la::NAME.code)));
            if (aname == "tag_dispatch" && ann.has_key(la::ARGS)) {
                // ARGS: { ITEMS: [ { NAME: "system_name" } ] }
                auto args_map = map_of(ann.get(la::ARGS.code));
                if (args_map.has_key(la::ITEMS)) {
                    auto arr = arr_of(args_map.get(la::ITEMS.code));
                    if (arr.size() > 0)
                        td.tag_dispatch_system = std::string(str_of(map_of(arr.get(0)).get(la::NAME.code)));
                }
            }
            // #[type_code=N] on a trait makes it a genos: the code identifies
            // the logical datatype family, and each `impl Trait for Eidos`
            // propagates the code to its target struct during lowering.
            else if (aname == "type_code" && ann.has_key(la::VALUE)) {
                td.type_code = read_annotation_u64(ann);
            }
        }
    };

    // §6.7: same flatten-EXTERN_BLOCK pass as sema_collect. lower_module_items
    // iterates the original module ITEMS to emit per-item LIR; an extern block
    // would otherwise be unhandled and its children skipped. Splice each
    // block's child EXTERN_FN entries into a flat worklist that lower_*
    // dispatch reads in order.
    std::vector<TinyMapView> flat_items;
    flat_items.reserve(items.size());
    for (uint64_t i = 0; i < items.size(); ++i) {
        auto it = map_of(items.get(i));
        if (code_of(it) == la::EXTERN_BLOCK) {
            if (it.has_key(la::ITEMS)) {
                auto block_items = arr_of(it.get(la::ITEMS.code));
                for (uint64_t j = 0; j < block_items.size(); ++j)
                    flat_items.push_back(map_of(block_items.get(j)));
            }
            continue;
        }
        flat_items.push_back(it);
    }
    for (uint64_t i = 0; i < flat_items.size(); ++i) {
        auto item = flat_items[i];
        int32_t c = code_of(item);
        if (c == la::ANNOTATION) {
            pending_annots.push_back(item);
            continue;
        }
        if (c == la::DOC_LINE_LIT) {
            // Mirror collect-phase doc accumulation: strip `/// ` prefix and
            // join with '\n'. Consumed by the next LXxx via take_pending_doc().
            append_doc_line(pending_doc_, str_of(item.get(la::VALUE.code)));
            continue;
        }
        if (c == la::DOC_BLOCK_LIT) {
            // Phase A.4: `/** ... */` outer block doc-comment.
            append_doc_block(pending_doc_,
                             str_of(item.get(la::VALUE.code)),
                             /*prefix_len=*/3);
            continue;
        }
        if (c == la::INNER_DOC_LIT) {
            // Phase A.3: `//!` lines accumulate into the module-level inner-
            // doc buffer; finalised after the items loop.
            std::string_view raw = str_of(item.get(la::VALUE.code));
            std::string_view stripped = raw.size() >= 3 ? raw.substr(3) : std::string_view{};
            if (!stripped.empty() && stripped.front() == ' ')
                stripped.remove_prefix(1);
            if (!module_inner_doc_.empty()) module_inner_doc_.push_back('\n');
            module_inner_doc_.append(stripped);
            continue;
        }
        if (c == la::INNER_DOC_BLOCK_LIT) {
            // Phase A.4: `/*! ... */` inner block doc-comment.
            append_doc_block(module_inner_doc_,
                             str_of(item.get(la::VALUE.code)),
                             /*prefix_len=*/3);
            continue;
        }
        if (c == la::INSTANTIATE_DECL) {
            // `instantiate Foo<T>;` / `pub instantiate Foo<T>;` — pre-instantiation
            // root pin (C++ `template class Foo<int>;` analog). Pushes an
            // LInstAnnotation that mono picks up via the existing path: it demands
            // struct instantiation, which in the current eager scheme clones every
            // method. When lazy method codegen lands (L1), the annotation will also
            // pin all methods as roots so the worklist transitively pulls everything
            // they call. `pub` is stored for L3 lib-site re-export semantics; until
            // separate codegen exists it's a marker.
            if (!item.has_key(la::TYPE.code)) {
                error("instantiate declaration missing type expression");
                pending_annots.clear();
                continue;
            }
            auto type_node = map_of(item.get(la::TYPE.code));
            TypeRef resolved = resolve_type(type_node);
            if (resolved && TypeRef(resolved).kind() != LogosType::Kind::Error) {
                if (TypeRef(resolved).kind() != LogosType::Kind::Struct &&
                    TypeRef(resolved).kind() != LogosType::Kind::ZonedStruct &&
                    TypeRef(resolved).kind() != LogosType::Kind::Enum) {
                    error("instantiate target must be a struct, datatype, or enum");
                } else if (TypeRef(resolved).type_args().empty()) {
                    // B-mt-02: `instantiate Foo;` on a non-generic type adds
                    // no information.  Reject with a clear diagnostic.
                    auto nm = (TypeRef(resolved).kind() == LogosType::Kind::Enum)
                                ? std::string(TypeRef(resolved).enum_name())
                                : std::string(TypeRef(resolved).struct_name());
                    error(std::format("'instantiate {0};': '{0}' is not generic — "
                                      "'instantiate' only applies to generic templates",
                                      nm));
                } else {
                    lir::LInstAnnotation ia;
                    ia.canonical_name = std::string(cur_package_) + "::" + type_str(resolved);
                    if ((TypeRef(resolved).kind() == LogosType::Kind::Struct ||
                         TypeRef(resolved).kind() == LogosType::Kind::ZonedStruct) &&
                        !TypeRef(resolved).type_args().empty()) {
                        ia.mangled_name = concrete_struct_name(resolved);
                    } else if (TypeRef(resolved).kind() == LogosType::Kind::Struct ||
                               TypeRef(resolved).kind() == LogosType::Kind::ZonedStruct) {
                        ia.mangled_name = TypeRef(resolved).struct_name();
                    } else {
                        ia.mangled_name = TypeRef(resolved).enum_name();
                    }
                    ia.struct_type = resolved;
                    ia.is_root_pin = true;
                    ia.is_pub_reexport = item.has_key(la::IS_PUB) &&
                                         item.get(la::IS_PUB.code).is_value() &&
                                         item.get(la::IS_PUB.code).as_value<uint8_t>() != 0;
                    prog.inst_annotations.push_back(std::move(ia));
                }
            }
            pending_annots.clear();
            continue;
        }
        if (c == la::METACALL_ITEM) {
            // MC1.1: synthesise a void thunk that calls the metafn and
            // forwards the resulting QuoteItemBlob to logos_emit_item_blob_subst.
            // Driver invokes the thunk and marks this node consumed.
            lower_metacall_item(item, prog);
            pending_annots.clear();
            continue;
        }
        if (c == la::FN_MACRO_CALL_ITEM) {
            // Slice 6 of fn-macros: `name!{...}` at item position.
            // Parallel to METACALL_ITEM but routes through the fn-macro
            // pipeline (callee returns ItemList / QuoteItemBlob).
            lower_fn_macro_call_item(item, prog);
            pending_annots.clear();
            continue;
        }
        if (c == la::FN_MACRO_CALL_ITEM_DONE) {
            pending_annots.clear();
            continue;
        }
        if (c == la::METACALL_ITEM_DONE) {
            // Driver-set marker: this node has already been processed in
            // a prior round (its thunk has run, items have been spliced).
            // Silently skip.
            pending_annots.clear();
            continue;
        }
        // §6.1: route UNION_DEF through STRUCT-shaped lowering for
        // slice 1. The downstream type-checking sees the union as a
        // struct (same field shape); layout / unsafe-gating is the
        // follow-up soundness slice (see §6.1 catalog body).
        if (c == la::UNION_DEF) {
            // Synth the path by re-using STRUCT lower with the
            // union's NAME / FIELDS / TYPE_PARAMS slots — the
            // FIELD_DEF nodes have the same shape so lower_struct
            // accepts them verbatim.
            // For now just fall through: lower_module_items checks
            // STRUCT below; we need a STRUCT-equivalent code so the
            // existing handler fires. Treat UNION_DEF as STRUCT.
            c = la::STRUCT.code;
        }
        if      (c == la::STRUCT) {
            // Explicit struct instantiation: `#[type_code=N] struct Pair<i32>;`
            // Has TYPE key, no NAME key — delegate to same logic as DATATYPE inst.
            if (!item.has_key(la::NAME.code)) {
                if (!item.has_key(la::TYPE.code)) {
                    error("struct instantiation declaration missing type expression");
                } else {
                    auto type_node = map_of(item.get(la::TYPE.code));
                    // B-it-08: `pub struct Foo<T>;` (forward-decl-style with bare
                    // type-vars at item scope) hits resolve_type with T unbound,
                    // which produces a misleading "unknown type 'T'" diagnostic.
                    // Detect the shape and surface a specific message.
                    if (code_of(type_node) == la::GENERIC_INST && type_node.has_key(la::ITEMS)) {
                        auto args = arr_of(type_node.get(la::ITEMS.code));
                        bool has_unbound_var = false;
                        for (uint64_t ai = 0; ai < args.size(); ++ai) {
                            auto a = map_of(args.get(ai));
                            if (code_of(a) == la::TYPE_REF && a.has_key(la::NAME)) {
                                std::string an(str_of(a.get(la::NAME.code)));
                                if (!try_resolve_as_known_type(an) &&
                                    current_type_params_.count(an) == 0) {
                                    has_unbound_var = true;
                                    break;
                                }
                            }
                        }
                        if (has_unbound_var) {
                            std::string nm(str_of(type_node.get(la::NAME.code)));
                            error(std::format(
                                "'struct {0}<...>;': explicit instantiation requires "
                                "concrete type arguments. For a generic struct "
                                "definition write the body directly: "
                                "`pub struct {0}<...> {{ ... }}` (B-it-08).",
                                nm));
                            pending_annots.clear();
                            continue;
                        }
                    }
                    TypeRef resolved = resolve_type(type_node);
                    // B-it-07: `struct Empty;` (no body, not a real instantiation)
                    // resolves to error_t and silently drops.  When the type
                    // expression is a bare ident with no type-args, surface
                    // a clear diagnostic.
                    if ((!resolved || TypeRef(resolved).kind() == LogosType::Kind::Error) &&
                        type_node.has_key(la::NAME) &&
                        !type_node.has_key(la::ITEMS)) {
                        std::string nm(str_of(type_node.get(la::NAME.code)));
                        error(std::format(
                            "'struct {0};': '{0}' is not defined — "
                            "did you mean 'struct {0} {{ ... }}' to declare a body?",
                            nm));
                        pending_annots.clear();
                        continue;
                    }
                    if (resolved && TypeRef(resolved).kind() != LogosType::Kind::Error) {
                        lir::LInstAnnotation ia;
                        ia.canonical_name = std::string(cur_package_) + "::" + type_str(resolved);
                        if ((TypeRef(resolved).kind() == LogosType::Kind::Struct ||
                             TypeRef(resolved).kind() == LogosType::Kind::ZonedStruct) &&
                            !TypeRef(resolved).type_args().empty()) {
                            ia.mangled_name = concrete_struct_name(resolved);
                        } else if (TypeRef(resolved).kind() == LogosType::Kind::Struct ||
                                   TypeRef(resolved).kind() == LogosType::Kind::ZonedStruct) {
                            ia.mangled_name = TypeRef(resolved).struct_name();
                        }
                        for (auto& ann : pending_annots) {
                            auto aname = std::string(str_of(ann.get(la::NAME.code)));
                            if (aname == "type_code" && ann.has_key(la::VALUE)) {
                                ia.type_code = read_annotation_u64(ann);
                            }
                        }
                        if (ia.type_code != 0)
                            explicit_type_codes_[ia.canonical_name] = ia.type_code;
                        ia.struct_type = resolved;
                        prog.inst_annotations.push_back(std::move(ia));
                    }
                }
                pending_annots.clear();
                continue;
            }
            // Check if #[zoned] annotation is present → treat as zoned struct.
            bool has_zoned = false;
            for (auto& ann : pending_annots) {
                auto aname = std::string(str_of(ann.get(la::NAME.code)));
                if (aname == "zoned") { has_zoned = true; break; }
            }
            std::string struct_doc = take_pending_doc();
            if (is_specialization_struct(item)) {
                auto sd = lower_spec_struct(item);
                // Structural flags (zone_mut/zoned2/rel_ptr/…) apply to EVERY spec,
                // not just #[zoned] ones — they bypass the collect→structs_ path that
                // carries them for regular structs.
                apply_struct_flags(sd);
                if (has_zoned) {
                    sd.is_zoned = true;
                    apply_annots_to_struct(sd);
                }
                sd.doc = std::move(struct_doc);
                prog.struct_specializations.push_back(std::move(sd));
            } else if (has_zoned) {
                auto sd = lower_struct_def(item);
                sd.is_zoned = true;
                sd.pkg = std::string(cur_package_);
                { auto [pkg, dsi] = find_datatype_by_name(sd.name); if (dsi) sd.is_data_plain = dsi->is_data_plain; }
                apply_annots_to_struct(sd);
                if (sd.type_params.empty()) {
                    std::string canon = std::string(cur_package_) + "::" + sd.name;
                    sd.type_hash = type_hash_23(canon);
                    if (sd.type_code == 0) {
                        uint64_t raw = type_hash_56bit(sd.type_hash);
                        sd.type_code = (raw < 128) ? (raw + 128) : raw;
                    }
                }
                sd.doc = std::move(struct_doc);
                prog.structs.push_back(std::move(sd));
            } else {
                auto sd = lower_struct_def(item);
                sd.pkg = std::string(cur_package_);
                sd.doc = std::move(struct_doc);
                prog.structs.push_back(std::move(sd));
            }
        }
        else if (c == la::DATATYPE) {
            if (!item.has_key(la::NAME.code)) {
                // Explicit instantiation declaration: #[type_code=N] datatype SomeType<T>;
                // Has TYPE key, no NAME key, no FIELDS key.
                if (!item.has_key(la::TYPE.code)) {
                    error("explicit instantiation declaration missing type expression");
                } else {
                    auto type_node = map_of(item.get(la::TYPE.code));
                    TypeRef resolved = resolve_type(type_node);
                    if (resolved && TypeRef(resolved).kind() != LogosType::Kind::Error) {
                        lir::LInstAnnotation ia;
                        // Include package prefix for a globally unique canonical name.
                        ia.canonical_name = std::string(cur_package_) + "::" + type_str(resolved);
                        // Mangled name for matching against monomorphized struct defs.
                        if ((TypeRef(resolved).kind() == LogosType::Kind::Struct ||
                             TypeRef(resolved).kind() == LogosType::Kind::ZonedStruct) &&
                            !TypeRef(resolved).type_args().empty()) {
                            ia.mangled_name = concrete_struct_name(resolved);
                        } else if (TypeRef(resolved).kind() == LogosType::Kind::Struct ||
                                   TypeRef(resolved).kind() == LogosType::Kind::ZonedStruct) {
                            ia.mangled_name = TypeRef(resolved).struct_name();
                        }
                        for (auto& ann : pending_annots) {
                            auto aname = std::string(str_of(ann.get(la::NAME.code)));
                            if (aname == "type_code" && ann.has_key(la::VALUE)) {
                                ia.type_code = read_annotation_u64(ann);
                            }
                        }
                        // Register into explicit_type_codes_ so sema-time queries
                        // (`type_code_of::<Foo<i32>>()`) resolve to the annotated code.
                        if (ia.type_code != 0)
                            explicit_type_codes_[ia.canonical_name] = ia.type_code;
                        // Store resolved type so mono can demand struct instantiation
                        // even when no Logos code references this type directly.
                        ia.struct_type = resolved;
                        prog.inst_annotations.push_back(std::move(ia));
                    }
                }
            } else if (is_specialization_struct(item)) {
                // Datatype specialization (e.g. `pub eidos Map<Bitmap, V> { ... }`).
                auto sd = lower_spec_struct(item);
                sd.is_zoned = true;
                // Apply #[type_code=N] annotations on full (all-concrete) specs.
                // Without this, `impl Trait for Map<i32, AnyVal>` has no way to
                // find the annotated code during dispatch-entry emission.
                bool all_concrete = !sd.spec_patterns.empty();
                for (auto p : sd.spec_patterns)
                    if (!p || TypeRef(p).kind() == LogosType::Kind::TypeVar) { all_concrete = false; break; }
                if (all_concrete) {
                    for (auto& ann : pending_annots) {
                        auto aname = std::string(str_of(ann.get(la::NAME.code)));
                        if (aname == "type_code" && ann.has_key(la::VALUE)) {
                            sd.type_code = read_annotation_u64(ann);
                            // Register mangled fqn so dispatch-entry lookup
                            // (target = "Map$G2$i32$AnyVal") succeeds.
                            auto inst_type = make_generic_struct(sd.name, sd.spec_patterns);
                            std::string mangled = concrete_struct_name(inst_type);
                            std::string canon = type_str(inst_type);  // "Name<Args>"
                            auto fqn_mangled = cur_package_.empty()
                                ? mangled : cur_package_ + "::" + mangled;
                            auto fqn_canon = cur_package_.empty()
                                ? canon : cur_package_ + "::" + canon;
                            explicit_type_codes_[fqn_mangled] = sd.type_code;
                            explicit_type_codes_[fqn_canon]   = sd.type_code;
                            // Also register under the template's package (see
                            // matching note in the genos-spec annotation path).
                            std::string tmpl_pkg;
                            { auto [pkg, dsi] = find_datatype_by_name(sd.name); if (dsi) tmpl_pkg = dsi->package; }
                            if (tmpl_pkg.empty()) { auto [pkg, ssi] = find_struct_by_name(sd.name); if (ssi) tmpl_pkg = ssi->package; }
                            if (!tmpl_pkg.empty() && tmpl_pkg != cur_package_) {
                                explicit_type_codes_[tmpl_pkg + "::" + mangled] = sd.type_code;
                                explicit_type_codes_[tmpl_pkg + "::" + canon]   = sd.type_code;
                            }
                        }
                    }
                }
                sd.doc = take_pending_doc();
                prog.struct_specializations.push_back(std::move(sd));
            } else {
                // Normal datatype definition.
                auto sd = lower_struct_def(item);
                sd.is_zoned = true;
                sd.pkg = std::string(cur_package_);
                sd.doc = take_pending_doc();
                // Propagate is_data_plain only for datatypes (not regular structs).
                { auto [pkg, dsi] = find_datatype_by_name(sd.name); if (dsi) sd.is_data_plain = dsi->is_data_plain; }
                apply_annots_to_struct(sd);
                // Compute type_hash for concrete (non-generic) datatypes only.
                // Generic templates get their hash at instantiation time in mono_pass.
                if (sd.type_params.empty()) {
                    std::string canon = std::string(cur_package_) + "::" + sd.name;
                    sd.type_hash = type_hash_23(canon);
                    // Auto-assign type_code from hash; ensure it's outside the reserved
                    // inline-AnyVal range 1-127 (those are for zone-stored types >= 128).
                    if (sd.type_code == 0) {
                        uint64_t raw = type_hash_56bit(sd.type_hash);
                        sd.type_code = (raw < 128) ? (raw + 128) : raw;
                    }
                }
                prog.structs.push_back(std::move(sd));
            }
        }
        else if (c == la::ENUM) {
            auto ed = lower_enum_def(item);
            ed.doc = take_pending_doc();
            prog.enums.push_back(std::move(ed));
        }
        else if (c == la::FN || c == la::EXTERN_FN) {
            // lower_fn / lower_spec_fn read pending_doc_ at entry — no
            // post-hoc assignment needed.
            //
            // Translate `#[test]` / `#[should_panic]` / `#[ignore]` from
            // the pending_annots buffer to the SemaChecker pending_*
            // flags lower_fn consumes (sema_decl.cpp:88). Mirrors
            // sema_collect.cpp:1206-1217 — without this, lower_fn reads
            // residual flag state left over from the collect phase,
            // which mistakenly tags the first lower'd fn with
            // is_test=true if the last fn collect saw was #[test].
            pending_is_test_      = false;
            pending_should_panic_ = false;
            pending_ignore_       = false;
            pending_should_panic_expected_.clear();
            for (auto& ann : pending_annots) {
                auto nm = str_of(ann.get(la::NAME.code));
                if (nm == "test")    pending_is_test_      = true;
                if (nm == "ignore")  pending_ignore_       = true;
                if (nm == "should_panic") {
                    pending_should_panic_ = true;
                    if (ann.has_key(la::ARGS.code)) {
                        auto args_map = map_of(ann.get(la::ARGS.code));
                        if (args_map.has_key(la::ITEMS.code)) {
                            auto items_arr = arr_of(args_map.get(la::ITEMS.code));
                            for (uint64_t kk = 0; kk < items_arr.size(); ++kk) {
                                auto a = map_of(items_arr.get(kk));
                                if (code_of(a) != la::ANNOT_KV) continue;
                                if (!a.has_key(la::NAME.code) ||
                                    !a.has_key(la::VALUE.code)) continue;
                                auto kname = str_of(a.get(la::NAME.code));
                                if (kname != "expected") continue;
                                auto v = map_of(a.get(la::VALUE.code));
                                if (code_of(v) != la::LIT_STR) continue;
                                auto raw = str_of(v.get(la::VALUE.code));
                                if (raw.size() >= 2 && raw.front() == '"'
                                    && raw.back() == '"') {
                                    pending_should_panic_expected_.assign(
                                        raw.substr(1, raw.size() - 2));
                                }
                                break;
                            }
                        }
                    }
                }
            }
            if (is_specialization_fn(item)) {
                auto fp = std::make_unique<lir::LFunction>(lower_spec_fn(item));
                prog.specializations.push_back(std::move(fp));
            } else {
                auto fp = std::make_unique<lir::LFunction>(lower_fn(item));
                prog.functions.push_back(std::move(fp));
            }
        }
        else if (c == la::CONST_DEF) {
            auto cd = lower_const_def(item);
            cd.doc = take_pending_doc();
            prog.consts.push_back(std::move(cd));
        }
        else if (c == la::TYPE_ALIAS) {
            auto ta = lower_type_alias_def(item);
            ta.doc = take_pending_doc();
            prog.type_aliases.push_back(std::move(ta));
        }
        else if (c == la::TRAIT_DEF) {
            // Explicit genos specialization decl: `#[type_code=N] pub genos Array<i32>;`.
            // No NAME on the decl (name lives inside TYPE node); type_ref is
            // `Bag<Args>` where Bag is a trait name, not a struct.  Don't call
            // resolve_type — traits aren't types.  Extract trait_name + args
            // manually and canonicalize.
            if (!item.has_key(la::NAME.code) && item.has_key(la::TYPE.code)) {
                auto type_node = map_of(item.get(la::TYPE.code));
                if (code_of(type_node) != la::GENERIC_INST) {
                    error("genos specialization decl must have type arguments");
                } else {
                    auto tname = std::string(str_of(type_node.get(la::NAME.code)));
                    std::string canon = std::string(cur_package_) + "::" + tname + "<";
                    bool arg_ok = true;
                    if (type_node.has_key(la::ITEMS.code)) {
                        auto items2 = arr_of(type_node.get(la::ITEMS.code));
                        for (uint64_t i = 0; i < items2.size(); ++i) {
                            auto at = resolve_type(map_of(items2.get(i)));
                            if (!at || TypeRef(at).kind() == LogosType::Kind::Error) { arg_ok = false; break; }
                            if (i) canon += ", ";
                            canon += type_str(at);
                        }
                    }
                    canon += ">";
                    if (!arg_ok) {
                        error(std::format("genos '{}': cannot resolve type arguments", tname));
                    } else {
                        lir::LInstAnnotation ia;
                        ia.canonical_name = canon;
                        // Also compute the mangled eidos name ("Map$G2$Bitmap$AnyVal")
                        // for an eidos with the same name as the genos — this is the
                        // typical case where `genos Foo<X>` specialization's type_code
                        // should land on the like-named `eidos Foo<X>` struct.
                        auto items2 = arr_of(type_node.get(la::ITEMS.code));
                        std::vector<TypeRef> resolved_args;
                        for (uint64_t i = 0; i < items2.size(); ++i)
                            resolved_args.push_back(resolve_type(map_of(items2.get(i))));
                        // Find the template's kind (datatype or struct) using package-aware lookup.
                        TypeRef like_eidos = nullptr;
                        {
                            auto [dpkg, dsi] = find_datatype_by_name(tname);
                            if (dsi) like_eidos = make_generic_datatype(tname, resolved_args, {}, dpkg);
                        }
                        if (!like_eidos) {
                            auto [spkg, ssi] = find_struct_by_name(tname);
                            if (ssi) like_eidos = make_generic_struct(tname, resolved_args, {}, spkg);
                        }
                        // Legacy bare-name fallback for same-package structs
                        if (!like_eidos) {
                            if (datatypes_.count(tname))
                                like_eidos = make_generic_datatype(tname, resolved_args);
                            else if (structs_.count(tname))
                                like_eidos = make_generic_struct(tname, resolved_args);
                        }
                        if (like_eidos)
                            ia.mangled_name = concrete_struct_name(like_eidos);
                        for (auto& ann : pending_annots) {
                            auto aname = std::string(str_of(ann.get(la::NAME.code)));
                            if (aname == "type_code" && ann.has_key(la::VALUE)) {
                                ia.type_code = read_annotation_u64(ann);
                            }
                        }
                        if (ia.type_code != 0) {
                            explicit_type_codes_[ia.canonical_name] = ia.type_code;
                            // Register mangled fqn key too so the dispatch-entry
                            // emission (sema_decl.cpp) can find the type_code via
                            // mangled-target lookup.
                            if (!ia.mangled_name.empty())
                                explicit_type_codes_[std::string(cur_package_) + "::" + ia.mangled_name] = ia.type_code;
                            // Also register under the *template's* package, for both
                            // the canonical-name and mangled keys.  Every lookup site
                            // (dispatch-entry emission in sema_decl.cpp, type_code_of
                            // in sema_expr.cpp, etc.) resolves the package via
                            // `datatypes_[base].package` — the template's package.
                            // When a genos specialisation is declared in a *different*
                            // package from its template (e.g. `genos Map<Varchar,
                            // AnyVal>` lives in hermes.objectmap while `datatype
                            // Map<K,V>` lives in hermes.map), lookups keyed by the
                            // template's package would otherwise miss this annotation
                            // and fall back to the auto-hashed type_code, silently
                            // producing a different code at the use site than the
                            // one registered in the dispatch table.  Mirror both keys
                            // under the template's package to keep the two sides in
                            // agreement.
                            std::string tmpl_pkg;
                            { auto [pkg, dsi] = find_datatype_by_name(tname); if (dsi) tmpl_pkg = dsi->package; }
                            if (tmpl_pkg.empty()) { auto [pkg, ssi] = find_struct_by_name(tname); if (ssi) tmpl_pkg = ssi->package; }
                            if (!tmpl_pkg.empty() && tmpl_pkg != cur_package_) {
                                // Canonical form: "pkg::Name<Args>".  ia.canonical_name
                                // was built from cur_package_ at the top of this block
                                // (`canon = cur_package_ + "::" + tname + "<…>"`) so we
                                // reconstruct the template-package form by substring.
                                auto colon2 = ia.canonical_name.find("::");
                                if (colon2 != std::string::npos) {
                                    explicit_type_codes_[tmpl_pkg + ia.canonical_name.substr(colon2)] = ia.type_code;
                                }
                                if (!ia.mangled_name.empty())
                                    explicit_type_codes_[tmpl_pkg + "::" + ia.mangled_name] = ia.type_code;
                            }
                        }
                        prog.inst_annotations.push_back(std::move(ia));
                    }
                }
            } else {
                auto td = lower_trait_def(item);
                apply_annots_to_trait(td);
                // Reject #[type_code] on a template genos (type_params present).
                // It would collide at dispatch-table level: every concrete
                // specialization would land in the same tag-system slot.
                if (td.type_code != 0) {
                    auto tit = traits_.find(td.name);
                    if (tit != traits_.end() && !tit->second.type_params.empty())
                        error(std::format("genos '{}': #[type_code] on a template "
                                          "(parametric) genos is forbidden — "
                                          "attach it to a concrete specialization "
                                          "(e.g. `#[type_code=N] genos {}<T>;`)",
                                          td.name, td.name));
                }
                td.doc = take_pending_doc();
                prog.traits.push_back(std::move(td));
            }
        }
        else if (c == la::IMPL_BLOCK) lower_impl_block(item, prog);
        // Defensive: clear any unused doc from items that didn't consume it.
        pending_doc_.clear();
        pending_annots.clear();
    }
    // Phase A.3: commit per-module `//!` accumulator into LProgram and reset.
    if (!module_inner_doc_.empty()) {
        prog.module_inner_docs.push_back({file_, std::move(module_inner_doc_)});
        module_inner_doc_.clear();
    }
}

// ── B64: per-struct/enum variance via fixed-point ─────────────────────────────

namespace {

// Compute the variance with which `target` (a type-param or lifetime-param
// name, e.g. "T" or "'a") appears in `t`, given the in-progress variance
// table for other user defs and an `ambient` variance context (the variance
// at which `t`'s containing position is held).
Variance variance_in_type(TypeRef t,
                          const std::string& target,
                          bool target_is_lifetime,
                          const DefVarianceTable& table,
                          Variance ambient = Variance::Co)
{
    if (!t) return Variance::BiVar;
    using K = LogosType::Kind;
    switch (t.kind()) {
        case K::TypeVar:
            if (!target_is_lifetime && std::string(t.type_var_name()) == target)
                return ambient;
            return Variance::BiVar;
        case K::Ref: {
            Variance v = Variance::BiVar;
            if (target_is_lifetime && std::string(t.lifetime()) == target)
                v = variance_meet(v, ambient);  // Ref is Co in lt
            v = variance_meet(v, variance_in_type(t.pointee(), target,
                                                  target_is_lifetime, table, ambient));
            return v;
        }
        case K::MutRef: {
            Variance v = Variance::BiVar;
            if (target_is_lifetime && std::string(t.lifetime()) == target)
                v = variance_meet(v, ambient);  // MutRef is Co in lt
            // Inv in pointee.
            v = variance_meet(v, variance_in_type(t.pointee(), target,
                                                  target_is_lifetime, table,
                                                  variance_compose(ambient, Variance::Inv)));
            return v;
        }
        case K::Ptr:
            // B84: *const T is Co in pointee (matches Rust); *mut T is Inv.
            return variance_in_type(t.pointee(), target, target_is_lifetime, table,
                                    variance_compose(ambient,
                                        t.mut_ptr() ? Variance::Inv : Variance::Co));
        case K::Tuple: {
            Variance v = Variance::BiVar;
            for (auto e : t.tuple_elems())
                v = variance_meet(v, variance_in_type(e, target,
                                                       target_is_lifetime, table, ambient));
            return v;
        }
        case K::Array:
        case K::Slice:
            return variance_in_type(t.elem(), target, target_is_lifetime, table, ambient);
        case K::Struct:
        case K::ZonedStruct:
        case K::Enum: {
            // logos-core 2.2: `UnsafeCell<T>` is the interior-mutability
            // lang-item — Inv in T (Rust's invariance rule, since `&T`
            // can mutate the interior; a covariant relationship between
            // `UnsafeCell<&'long X>` and `UnsafeCell<&'short X>` would
            // be unsound). Recognised by qualified name.
            if (t.kind() == K::Struct &&
                std::string(t.struct_name()) == "UnsafeCell" &&
                std::string(t.pkg_name()) == "logos.lang.cell") {
                Variance v = Variance::BiVar;
                for (auto a : t.type_args())
                    v = variance_meet(v, variance_in_type(a, target,
                                                           target_is_lifetime, table,
                                                           variance_compose(ambient, Variance::Inv)));
                return v;
            }
            std::string key = std::string(t.pkg_name()) +
                              (t.pkg_name().empty() ? "" : ".") +
                              std::string(t.kind() == K::Enum ? t.enum_name() : t.struct_name());
            auto it = table.find(key);
            const VarianceMap* vm = (it == table.end()) ? nullptr : &it->second;
            auto var_for = [&](size_t i, bool is_lt) -> Variance {
                if (!vm) return Variance::Co;
                std::string ikey = (is_lt ? "@" : "#") + std::to_string(i);
                auto vit = vm->find(ikey);
                return (vit == vm->end()) ? Variance::Co : vit->second;
            };
            Variance v = Variance::BiVar;
            size_t i = 0;
            for (auto a : t.type_args()) {
                Variance inner_ambient = variance_compose(ambient, var_for(i++, false));
                v = variance_meet(v, variance_in_type(a, target,
                                                       target_is_lifetime, table,
                                                       inner_ambient));
            }
            i = 0;
            for (auto& lt : t.lifetime_args()) {
                if (target_is_lifetime && std::string(lt) == target) {
                    Variance inner_ambient = variance_compose(ambient, var_for(i, true));
                    v = variance_meet(v, inner_ambient);
                }
                ++i;
            }
            return v;
        }
        case K::FnItem:
        case K::FnPtr: {
            Variance v = Variance::BiVar;
            for (auto p : t.closure_params())
                v = variance_meet(v, variance_in_type(p, target, target_is_lifetime, table,
                                                       variance_compose(ambient, Variance::Contra)));
            v = variance_meet(v, variance_in_type(t.closure_ret(), target,
                                                   target_is_lifetime, table, ambient));
            return v;
        }
        case K::TraitObject: {
            // `dyn Trait<T...> + 'a`: lifetime bound Co (ref-like — the
            // erased object's storage must outlive `'a`), each type
            // argument Invariant (Rust spec — without per-trait declared
            // variance, a dyn-trait param is invariant). Auto-trait
            // bounds (`+ Send` / `+ Sync`) are set-membership and
            // contribute nothing to variance over the lifetime/type
            // axes — they're checked separately at the unsize site.
            // Pre-fix this fell through to `default → BiVar`, which
            // accepts wrong directions silently (logos-core 2.3).
            Variance v = Variance::BiVar;
            if (target_is_lifetime && !std::string(t.lifetime()).empty() &&
                std::string(t.lifetime()) == target)
                v = variance_meet(v, ambient);
            for (auto a : t.type_args())
                v = variance_meet(v, variance_in_type(a, target,
                                                       target_is_lifetime, table,
                                                       variance_compose(ambient, Variance::Inv)));
            return v;
        }
        default:
            return Variance::BiVar;
    }
}

} // anonymous namespace

void SemaChecker::compute_variances() {
    variance_table_.clear();
    auto seed = [&](const std::string& key,
                    const std::vector<TypeParam>& tps,
                    const std::vector<std::string>& lts) {
        VarianceMap m;
        for (size_t i = 0; i < tps.size(); ++i)
            m["#" + std::to_string(i)] = Variance::BiVar;
        for (size_t i = 0; i < lts.size(); ++i)
            m["@" + std::to_string(i)] = Variance::BiVar;
        variance_table_[key] = std::move(m);
    };
    // structs_/datatypes_/enums_ are keyed by "pkg::Name" (sema_key). Subtype
    // lookup uses "pkg.Name". Strip the "pkg::" prefix from the map key and
    // re-join with "." to match what subtype expects.
    auto qkey = [](const std::string& pkg, const std::string& map_key) {
        std::string name = map_key;
        if (!pkg.empty()) {
            std::string prefix = pkg + "::";
            if (name.compare(0, prefix.size(), prefix) == 0)
                name = name.substr(prefix.size());
            return pkg + "." + name;
        }
        return name;
    };
    for (auto& [k, si] : structs_)
        seed(qkey(si.package, k), si.type_params, si.lifetime_params);
    for (auto& [k, si] : datatypes_)
        seed(qkey(si.package, k), si.type_params, si.lifetime_params);
    for (auto& [k, ei] : enums_)
        seed(k, ei.type_params, ei.lifetime_params);

    bool changed = true;
    int rounds = 0;
    const int MAX_ROUNDS = 32;
    while (changed && rounds++ < MAX_ROUNDS) {
        changed = false;
        auto update_def =
            [&](const std::string& key,
                const std::vector<TypeParam>& tps,
                const std::vector<std::string>& lts,
                auto field_types_fn) {
            VarianceMap& vm = variance_table_[key];
            for (size_t i = 0; i < tps.size(); ++i) {
                Variance v = Variance::BiVar;
                for (auto ft : field_types_fn()) {
                    v = variance_meet(v,
                        variance_in_type(ft, tps[i].name, /*lt=*/false,
                                         variance_table_, Variance::Co));
                }
                std::string ikey = "#" + std::to_string(i);
                if (vm[ikey] != v) { vm[ikey] = v; changed = true; }
            }
            for (size_t i = 0; i < lts.size(); ++i) {
                Variance v = Variance::BiVar;
                for (auto ft : field_types_fn()) {
                    v = variance_meet(v,
                        variance_in_type(ft, lts[i], /*lt=*/true,
                                         variance_table_, Variance::Co));
                }
                std::string ikey = "@" + std::to_string(i);
                if (vm[ikey] != v) { vm[ikey] = v; changed = true; }
            }
        };
        for (auto& [k, si] : structs_) {
            update_def(qkey(si.package, k), si.type_params, si.lifetime_params,
                       [&]() {
                           std::vector<TypeRef> ts;
                           for (auto& f : si.fields) ts.push_back(f.type);
                           return ts;
                       });
        }
        for (auto& [k, si] : datatypes_) {
            update_def(qkey(si.package, k), si.type_params, si.lifetime_params,
                       [&]() {
                           std::vector<TypeRef> ts;
                           for (auto& f : si.fields) ts.push_back(f.type);
                           return ts;
                       });
        }
        for (auto& [k, ei] : enums_) {
            update_def(k, ei.type_params, ei.lifetime_params,
                       [&]() {
                           std::vector<TypeRef> ts;
                           for (auto& v : ei.variants)
                               for (auto pt : v.payload_types) ts.push_back(pt);
                           return ts;
                       });
        }
    }
}

// ── Entry point ───────────────────────────────────────────────────────────────

lir::LProgram sema_lower(const std::vector<logos::hermes::Hermes>& asts,
                          const std::vector<std::string>& filenames,
                          const std::vector<bool>& from_binary,
                          SemaOptions opts,
                          const std::vector<bool>& is_lazy) {
    auto t_outer = std::chrono::steady_clock::now();
    const bool phase_dbg = []{
        const char* e = std::getenv("LOGOS_SEMA_PHASE_TIMING");
        return e && e[0] && e[0] != '0';
    }();
    SemaChecker checker;
    checker.set_metaprog_options(opts.metaprog_mode, opts.entry_ast_idx);
    checker.set_metaprog_keep_fns(opts.metaprog_keep_fns);
    checker.set_cache(opts.cache);
    checker.set_delta_start_idx(opts.delta_start_idx);
    checker.set_implicit_prelude(std::move(opts.implicit_prelude));
    // Skeleton-skip gate: a from_binary fn whose symbol is already in a linked
    // archive's .o has its body skipped (forward-declared + linked). The set is
    // self-gating — a library build's own not-yet-compiled fns are absent, so
    // their bodies are lowered locally and mono's scan_fn sees their generics.
    checker.set_binary_symbols(&opts.binary_symbols);
    // Phase 2-4: ingest cfg flags. `feature=name` adds `name` to the
    // feature set; bare `flag` is reserved (future use). Equal sign is
    // the discriminator.
    for (auto& f : opts.cfg_flags) {
        auto eq = f.find('=');
        if (eq != std::string::npos) {
            auto key = f.substr(0, eq);
            auto val = f.substr(eq + 1);
            if (key == "feature") checker.add_cfg_feature(val);
        }
        // bare flags (no `=`) are placeholder for future target-key
        // overrides like `--cfg target_pointer_width=32`. No-op for now.
    }
    // Phase 6: thread is_lazy[] through SemaChecker so lower_fn can stamp
    // LFunction.from_lazy_module. Default empty → all lazy_=false → no
    // behaviour change (back-compat).
    checker.set_is_lazy(&is_lazy);
    auto prog = checker.run(asts, filenames, from_binary);
    if (phase_dbg) {
        auto t_after_run = std::chrono::steady_clock::now();
        auto run_us = std::chrono::duration_cast<std::chrono::microseconds>(t_after_run - t_outer).count();
        std::fprintf(stderr, "[sema-outer] sema_lower (excl. dtor)  %6ld us\n",
                     (long)run_us);
    }
    return prog;
}

} // namespace logos::compiler
