// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Victor Smirnov
// Logos project — https://github.com/victor-smirnov/logos
//
// Semantic analysis types: LogosType, TypePool, SemaResult, Diag.
//
// The entry point for semantic analysis + L-IR lowering is sema_lower(),
// declared in lir.hpp (which includes this header).

#pragma once

#include <deque>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <unordered_map>
#include <vector>
#include <optional>
#include <string>
#include <string_view>

#include <logos/hermes/arena_string.hpp>

#include <logos/compiler/sema_schema.hpp>
#include <logos/hermes/arena.hpp>
#include <logos/hermes/tiny_object_map.hpp>
#include <logos/hermes/schema_codes.hpp>
#include <logos/verification/assert.hpp>

namespace logos::compiler {

// ── Type representation ────────────────────────────────────────────────────

struct LogosType {
    enum class Kind {
        Void,                     // no return value
        I32, I64, F64, F32, Bool, U8,  // signed/float/bool primitives
        I8, I16, U16, U32, U64,   // additional integer types
        I24, U24,                 // 24-bit (pairs with AnyVal inline Integer/UInteger)
        I56, U56, I128, U128,     // non-power-of-two and extended widths
        Ptr,                      // *const T / *mut T  (raw/unsafe pointer)
        Ref,                      // &T     — shared reference (borrow-checked)
        MutRef,                   // &mut T — exclusive mutable reference (borrow-checked)
        Array,                    // [T; N]
        Struct,                   // user-defined struct
        ZonedStruct,                 // Hermes datatype (C POD layout, no heap types)
        Enum,                     // discriminant enum (stored as i32)
        Tuple,                    // (T1, T2, ...) — anonymous product type
        Slice,                    // &[T] — fat pointer (ptr, len)
        Closure,                  // |params| -> ret (closure type)
        TraitObject,              // &dyn Trait — fat pointer {data, vtable}
        TypeVar,                  // abstract type variable (e.g. T in fn f<T>)
        IntLit,                   // unresolved integer literal (widens to any integer)
        FloatLit,                 // unresolved float literal (widens to f32 or f64, defaults to f64)
        AssocType,                // T::Item — type param's associated type (resolved by mono)
        ImplTrait,                // impl Trait — opaque return type, resolved during lowering
        ConstVar,                 // [NEW] symbolic constant parameter (Bug 13)
        FnPtr,                    // fn(T1, T2) -> R — bare function pointer (single ptr)
        TaggedPtr,                // &tagged<TS> Trait — thin tag-dispatched pointer (*const u8)
        Error                     // sentinel for ill-typed expressions
    };

    Kind kind = Kind::Error;

    // Ptr / Ref / MutRef — all use pointee
    bool             mut_ptr  = false;    // Ptr only: true → *mut, false → *const
    const LogosType* pointee  = nullptr;  // non-owning, pool-allocated

    // Ref / MutRef — lifetime annotation
    std::string      lifetime;            // "'a", "'static", "'_", "" = elided

    // Array
    const LogosType* elem     = nullptr;  // non-owning, pool-allocated
    uint64_t         arr_size = 0;
    std::string      arr_size_var;        // [NEW] for symbolic size 'N' (Bug 13)

    // Struct / Enum
    std::string      struct_name;         // base struct name (owned; never mangled)
    std::string      enum_name;           // enum name (owned)
    std::string      pkg_name;            // package owning this type ("std.vec", "hermes.ctr", etc.); empty for unpackaged

    // Generic struct instantiation: Pair<i32> → struct_name="Pair", type_args=[i32]
    // Empty for plain (non-generic) structs.
    std::vector<const LogosType*> type_args;

    // Lifetime arguments for struct instantiation: StringView<'z> → lifetime_args=["'z"]
    // Parallel to type_args but for lifetime params. Erased at codegen.
    std::vector<std::string> lifetime_args;

    // Tuple
    std::vector<const LogosType*> tuple_elems;  // element types (Tuple kind only)

    // Closure
    std::vector<const LogosType*> closure_params;  // parameter types
    const LogosType* closure_ret = nullptr;         // return type

    // TraitObject — &dyn Trait
    std::string      trait_name;          // e.g. "Display" (TraitObject kind only)

    // TypeVar — name stored as a std::string (owns its storage)
    std::string      type_var_name;       // e.g. "T" (TypeVar kind only)

    // AssocType — associated type: base::Item or base::Item<A,B> (GAT)
    // trait_name:    the trait that declares it ("Iterator")
    const LogosType* assoc_base = nullptr;  // the base type (e.g. TypeVar(T) or AssocType(T::A))
    std::string      assoc_type_name;       // e.g. "Item" (AssocType kind only)
    std::vector<const LogosType*> gat_args; // GAT type arguments: T::Item<i32> → [i32]

    // Constant literal value (for monomorphized constant generics)
    std::optional<int64_t> const_val;

    // ── Hermes mirror back-refs (Phase 2c.4a) ──
    // Set by TypePool::alloc() after building the TinyObjectMap mirror.
    // offset is stable across arena growth (arena is GrowableSingleChunk);
    // mirror pointer is resolved on demand as `hermes_arena_->head().data()
    // + hermes_mirror_off_`. Used by TypeRef accessors in Phase 2c.4b+ to
    // read fields from the mirror instead of the struct.
    const hermes::Arena*   hermes_arena_      = nullptr;
    hermes::arena_offset_t hermes_mirror_off_ = hermes::NULL_OFFSET;
    const class TypePoolImpl* hermes_pool_     = nullptr;
};

// ── Trait bound (for type parameter bounds) ────────────────────────────────

struct TraitBound {
    std::string                   trait_name;  // e.g. "Into", "Add"
    std::vector<const LogosType*> type_args;   // e.g. Into<i32> -> [i32]
};

// ── Type parameter ────────────────────────────────────────────────────────

struct TypeParam {
    std::string              name;          // e.g. "T"
    std::vector<TraitBound>  bounds;        // e.g. [Ord, Clone]
    bool                     is_variadic = false;  // T... variadic pack
    bool                     is_const    = false;  // const N: T
    const LogosType*         const_type  = nullptr;
};

// ── TypeRef ───────────────────────────────────────────────────────────────
//
// Non-owning view over a LogosType living in a TypePool (Phase 2 transition:
// wraps `const LogosType*` with implicit conversion both ways so existing
// call sites work unchanged; view methods provide the API surface that will
// remain after the underlying storage moves to a Hermes zone in Phase 2c).

class TypeRef {
    const LogosType* p_ = nullptr;
public:
    constexpr TypeRef() noexcept = default;
    constexpr TypeRef(const LogosType* p) noexcept : p_(p) {}

    // Backward compatibility with `const LogosType*` — both directions
    // are implicit so TypeRef and raw pointers are mixable in signatures
    // during the incremental rewrite.
    constexpr operator const LogosType*() const noexcept { return p_; }
    constexpr const LogosType* operator->() const noexcept { return p_; }
    constexpr const LogosType& operator*()  const noexcept { return *p_; }
    constexpr explicit operator bool() const noexcept { return p_ != nullptr; }

    constexpr const LogosType* raw() const noexcept { return p_; }

    friend constexpr bool operator==(TypeRef a, TypeRef b) noexcept { return a.p_ == b.p_; }
    friend constexpr bool operator==(TypeRef a, const LogosType* b) noexcept { return a.p_ == b; }
    friend constexpr bool operator==(const LogosType* a, TypeRef b) noexcept { return a == b.p_; }

    // ── View accessors ──
    // These match what a Hermes-backed reader will expose in Phase 2c;
    // new code should prefer them over direct field access.

    // Hermes mirror helpers — used by accessors that have been cut over to
    // reading from the TinyObjectMap (Phase 2c.4b+). The struct back-refs
    // are populated by TypePool::alloc(); offset is stable across arena
    // growth, so the base is re-derived on each call.
    // Note: returns non-const uint8_t* because TinyObjectMap::get/slot take
    // a mutable base (even from const methods). The arena is logically const
    // here; const_cast is safe as the reads are side-effect-free.
    uint8_t* mirror_base() const noexcept {
        return const_cast<uint8_t*>(
            p_->hermes_arena_->head().data());
    }
    const hermes::TinyObjectMap* mirror() const noexcept {
        return reinterpret_cast<const hermes::TinyObjectMap*>(
            mirror_base() + p_->hermes_mirror_off_.value());
    }

    // Transitional fallback: some call sites construct stack LogosType
    // without going through TypePool::alloc() (e.g. mono_clone builds a
    // temporary Struct type to compute its concrete name). Those instances
    // have no mirror, so we read straight from the struct. Phase 2c.4d
    // will require every LogosType to live in the arena and remove this.
    LogosType::Kind kind() const noexcept {
        if (!p_->hermes_arena_) return p_->kind;
        return LogosType::Kind(
            hermes::schema::variant_of(mirror()->schema_type_code()));
    }

    // Pointer-valued accessors. Struct fields are authoritative; when a
    // mirror is wired, we cross-check the mirror's AnyVal pointee offset
    // resolves (via TypePoolImpl's inverse map) to the same LogosType*.
    // Full cutover is 2c.4d — returns will then come straight from the
    // mirror and the struct fields can be deleted.
    TypeRef pointee()      const noexcept;
    TypeRef elem()         const noexcept;
    TypeRef assoc_base()   const noexcept;
    TypeRef closure_ret()  const noexcept;

    bool mut_ptr() const noexcept {
        if (!p_->hermes_arena_) return p_->mut_ptr;
        auto av = mirror()->get(sema_schema::MUT_PTR.code, mirror_base());
        return av.is_value() && av.as_value<uint8_t>() != 0;
    }
    uint64_t arr_size() const noexcept {
        if (!p_->hermes_arena_) return p_->arr_size;
        auto av = mirror()->get(sema_schema::ARR_SIZE.code, mirror_base());
        if (av.is_null()) return 0;
        return *av.as_ptr<const uint64_t>(mirror_base());
    }

    // String accessors still return `const std::string&` backed by the
    // struct field; 2c.4d.2 tried flipping to string_view but broke ~100
    // callers (string concat, std::string-typed API boundaries). A dedicated
    // migration pass will do the flip after 2c.4e deletes the struct. For
    // now we cross-check the mirror's ArenaString matches the source on
    // every access, exercising the read-path under load.
private:
    void check_str_mirror(sema_schema::Key key, const std::string& src,
                          const char* name) const noexcept {
        if (!p_->hermes_arena_) return;
        auto av = mirror()->get(key.code, mirror_base());
        if (src.empty()) {
            LOGOS_ASSERT(av.is_null(),
                         "SEMA-TYPEREF-STR-0001",
                         "%s mirror has value for empty struct field", name);
            return;
        }
        LOGOS_ASSERT(!av.is_null(),
                     "SEMA-TYPEREF-STR-0002",
                     "%s mirror null for populated struct field", name);
        auto view = av.as_ptr<const hermes::ArenaString>(mirror_base())->view();
        LOGOS_ASSERT(view == src,
                     "SEMA-TYPEREF-STR-0003",
                     "%s mirror text mismatch", name);
    }
public:
    const std::string& lifetime()        const noexcept { check_str_mirror(sema_schema::LIFETIME,        p_->lifetime,        "lifetime");        return p_->lifetime; }
    const std::string& struct_name()     const noexcept { check_str_mirror(sema_schema::STRUCT_NAME,     p_->struct_name,     "struct_name");     return p_->struct_name; }
    const std::string& enum_name()       const noexcept { check_str_mirror(sema_schema::ENUM_NAME,       p_->enum_name,       "enum_name");       return p_->enum_name; }
    const std::string& pkg_name()        const noexcept { check_str_mirror(sema_schema::PKG_NAME,        p_->pkg_name,        "pkg_name");        return p_->pkg_name; }
    const std::string& trait_name()      const noexcept { check_str_mirror(sema_schema::TRAIT_NAME,      p_->trait_name,      "trait_name");      return p_->trait_name; }
    const std::string& type_var_name()   const noexcept { check_str_mirror(sema_schema::TYPE_VAR_NAME,   p_->type_var_name,   "type_var_name");   return p_->type_var_name; }
    const std::string& assoc_type_name() const noexcept { check_str_mirror(sema_schema::ASSOC_TYPE_NAME, p_->assoc_type_name, "assoc_type_name"); return p_->assoc_type_name; }
    const std::string& arr_size_var()    const noexcept { check_str_mirror(sema_schema::ARR_SIZE_VAR,    p_->arr_size_var,    "arr_size_var");    return p_->arr_size_var; }

    const std::vector<const LogosType*>& type_args()      const noexcept { return p_->type_args; }
    const std::vector<const LogosType*>& tuple_elems()    const noexcept { return p_->tuple_elems; }
    const std::vector<const LogosType*>& closure_params() const noexcept { return p_->closure_params; }
    const std::vector<const LogosType*>& gat_args()       const noexcept { return p_->gat_args; }
    const std::vector<std::string>&      lifetime_args()  const noexcept { return p_->lifetime_args; }

    // const_val still returns the struct's optional — the caller pattern is
    // `if (t.const_val()) use(*t.const_val())`. We cross-check against the
    // mirror (debug only via LOGOS_ASSERT) to keep the read-path honest.
    const std::optional<int64_t>& const_val() const noexcept {
        if (p_->hermes_arena_) {
            auto av = mirror()->get(sema_schema::CONST_VAL.code, mirror_base());
            if (p_->const_val.has_value()) {
                LOGOS_ASSERT(!av.is_null(),
                             "LOGOS-Compiler-TypeRef-0001",
                             "mirror missing CONST_VAL for populated optional");
                LOGOS_ASSERT(*av.as_ptr<const int64_t>(mirror_base()) == *p_->const_val,
                             "LOGOS-Compiler-TypeRef-0002",
                             "mirror CONST_VAL mismatch");
            } else {
                LOGOS_ASSERT(av.is_null(),
                             "LOGOS-Compiler-TypeRef-0003",
                             "mirror has CONST_VAL for empty optional");
            }
        }
        return p_->const_val;
    }
};

// Structural equality (pointer-to-pointer not checked — use value comparison).
bool types_equal(const LogosType& a, const LogosType& b) noexcept;

// Human-readable name for error messages.
std::string type_str(TypeRef t);

// Concrete struct name: plain structs → struct_name; generic insts → "Pair__i32__bool".
// Used by mono and mlir_gen to look up instantiated struct definitions.
std::string concrete_struct_name(TypeRef t);


// ── TypePool ───────────────────────────────────────────────────────────────
//
// Owns all LogosType objects.  std::deque gives pointer stability on push_back.
// Moved into LProgram so pointers remain valid after sema_lower() returns.
//
// Phase 2c.2: TypePool also owns a Hermes arena and mirrors every allocated
// LogosType into a TinyObjectMap node inside it. The mirror is not yet read
// by anyone — Phase 2c.3 will switch TypeRef view accessors to read from it.
// Readers that currently resolve through the raw struct pointer are
// unaffected.

class TypePoolImpl;  // PIMPL — owns hermes::Arena and offset mapping

class TypePool {
    std::deque<LogosType>          pool_;
    std::unique_ptr<TypePoolImpl>  impl_;  // lazily created on first alloc()
public:
    TypePool();
    ~TypePool();
    TypePool(TypePool&&) noexcept;
    TypePool& operator=(TypePool&&) noexcept;

    // Non-copyable — pointer stability requires unique ownership.
    TypePool(const TypePool&) = delete;
    TypePool& operator=(const TypePool&) = delete;

    const LogosType* alloc(LogosType t);
};

// ── Diagnostics ────────────────────────────────────────────────────────────

struct Diag {
    enum class Level { Error, Warning };
    Level       level   = Level::Error;
    std::string context;  // e.g. "fn main" or "struct Point"
    std::string message;
    std::string file;     // source file (empty if unknown)
    uint32_t    line = 0; // source line (0 if unknown)
};

// ── Result ─────────────────────────────────────────────────────────────────

struct SemaResult {
    std::vector<Diag> diags;

    bool ok() const noexcept {
        for (auto& d : diags)
            if (d.level == Diag::Level::Error) return false;
        return true;
    }

    void print(std::FILE* fp = stderr) const noexcept {
        for (auto& d : diags) {
            const char* lev = (d.level == Diag::Level::Error) ? "error" : "warning";
            if (d.line > 0 && !d.file.empty())
                std::fprintf(fp, "%s:%u: %s [%s]: %s\n",
                             d.file.c_str(), d.line, lev, d.context.c_str(), d.message.c_str());
            else
                std::fprintf(fp, "%s [%s]: %s\n", lev, d.context.c_str(), d.message.c_str());
        }
    }
};

} // namespace logos::compiler
