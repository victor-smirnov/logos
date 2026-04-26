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
#include <cstring>
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

// 2c.4e.3.3: LogosType is now a slim handle. Reads happen through TypeRef
// (which queries the Hermes mirror via the back-refs below). Writes happen
// through LogosTypeBuilder, which carries the data fields and is consumed
// by TypePool::alloc.
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

    // 2c.6.5: slim .kind field removed — readers go through TypeRef(t).kind()
    // which reads the mirror's schema_type_code. The mirror is the single
    // source of truth.

    // 2c.5.4: 32-byte TypeUID — canonical structural identity used as the
    // equality oracle (types_equal is ptr-eq || memcmp(type_uid)).
    // Layout per master plan: byte 0 = kind tag, bytes 1..23 = SHA-256
    // truncation of canonical type expression (lifetime ignored, matches
    // types_equal semantics), bytes 24..31 = reserved member-id (0 for
    // pure types; future trait-dispatch will populate).
    //
    // 2c.6.6.B.1: TypeUID lives in TypePoolImpl::uid_of_, not on the slim
    // struct. The type stays on this class so the pool's side map can
    // reference it.
    struct TypeUID {
        uint8_t bytes[32] = {};
        bool operator==(const TypeUID& o) const noexcept {
            return std::memcmp(bytes, o.bytes, 32) == 0;
        }
    };

    // ── Hermes mirror back-refs ──
    // Set by TypePool::alloc() after building the TinyObjectMap mirror.
    // offset is stable across arena growth (arena is GrowableSingleChunk);
    // mirror pointer is resolved on demand as `hermes_arena_->head().data()
    // + hermes_mirror_off_`. All payload fields live in the mirror; reads go
    // through TypeRef accessors.
    const hermes::Arena*   hermes_arena_      = nullptr;
    hermes::arena_offset_t hermes_mirror_off_ = hermes::NULL_OFFSET;
    const class TypePoolImpl* hermes_pool_     = nullptr;
};

struct LogosTypeBuilder;  // defined below TypeRef

// ── TypeRef ───────────────────────────────────────────────────────────────
//
// Non-owning view over a LogosType living in a TypePool. Carries the fat
// {arena base, offset, pool} triple needed to read the Hermes mirror
// directly. Implicit conversion from `const LogosType*` is preserved during
// the 2c.6.6.B refactor; the reverse implicit conversion is restored as a
// transition aid and removed once every call site uses TypeRef.

class TypeRef {
    const LogosType*          p_     = nullptr;
    const hermes::Arena*      arena_ = nullptr;
    hermes::arena_offset_t    off_{};
    const TypePoolImpl*       pool_  = nullptr;
public:
    constexpr TypeRef() noexcept = default;
    TypeRef(const LogosType* p) noexcept
        : p_(p),
          arena_(p ? p->hermes_arena_ : nullptr),
          off_(p ? p->hermes_mirror_off_ : hermes::arena_offset_t{}),
          pool_(p ? p->hermes_pool_ : nullptr) {}
    constexpr TypeRef(std::nullptr_t) noexcept {}

    constexpr explicit operator bool() const noexcept { return p_ != nullptr; }

    constexpr const LogosType* raw() const noexcept { return p_; }

    friend constexpr bool operator==(TypeRef a, TypeRef b) noexcept { return a.p_ == b.p_; }
    friend constexpr bool operator==(TypeRef a, std::nullptr_t) noexcept { return a.p_ == nullptr; }
    friend constexpr bool operator==(std::nullptr_t, TypeRef a) noexcept { return a.p_ == nullptr; }
    friend constexpr bool operator==(TypeRef a, const LogosType* b) noexcept { return a.p_ == b; }
    friend constexpr bool operator==(const LogosType* a, TypeRef b) noexcept { return a == b.p_; }

    uint8_t* mirror_base() const noexcept {
        return arena_ ? const_cast<uint8_t*>(arena_->head().data()) : nullptr;
    }
    const hermes::TinyObjectMap* mirror() const noexcept {
        return reinterpret_cast<const hermes::TinyObjectMap*>(mirror_base() + off_.value());
    }
    const TypePoolImpl* pool() const noexcept { return pool_; }

    LogosType::Kind kind() const noexcept {
        return LogosType::Kind(
            hermes::schema::variant_of(mirror()->schema_type_code()));
    }

    TypeRef pointee()      const noexcept;
    TypeRef elem()         const noexcept;
    TypeRef assoc_base()   const noexcept;
    TypeRef closure_ret()  const noexcept;

    bool mut_ptr() const noexcept {
        auto av = mirror()->get(sema_schema::MUT_PTR.code, mirror_base());
        return av.is_value() && av.as_value<uint8_t>() != 0;
    }
    uint64_t arr_size() const noexcept {
        auto av = mirror()->get(sema_schema::ARR_SIZE.code, mirror_base());
        if (av.is_null()) return 0;
        return *av.as_ptr<const uint64_t>(mirror_base());
    }

private:
    std::string_view str_from_mirror(sema_schema::Key key) const noexcept {
        auto av = mirror()->get(key.code, mirror_base());
        if (av.is_null()) return {};
        return av.as_ptr<const hermes::ArenaString>(mirror_base())->view();
    }
public:
    std::string_view lifetime()        const noexcept { return str_from_mirror(sema_schema::LIFETIME);        }
    std::string_view struct_name()     const noexcept { return str_from_mirror(sema_schema::STRUCT_NAME);     }
    std::string_view enum_name()       const noexcept { return str_from_mirror(sema_schema::ENUM_NAME);       }
    std::string_view pkg_name()        const noexcept { return str_from_mirror(sema_schema::PKG_NAME);        }
    std::string_view trait_name()      const noexcept { return str_from_mirror(sema_schema::TRAIT_NAME);      }
    std::string_view type_var_name()   const noexcept { return str_from_mirror(sema_schema::TYPE_VAR_NAME);   }
    std::string_view assoc_type_name() const noexcept { return str_from_mirror(sema_schema::ASSOC_TYPE_NAME); }
    std::string_view arr_size_var()    const noexcept { return str_from_mirror(sema_schema::ARR_SIZE_VAR);    }

    std::vector<TypeRef> type_args()      const noexcept;
    std::vector<TypeRef> tuple_elems()    const noexcept;
    std::vector<TypeRef> closure_params() const noexcept;
    std::vector<TypeRef> gat_args()       const noexcept;
    std::vector<std::string> lifetime_args() const noexcept;

    std::optional<int64_t> const_val() const noexcept {
        auto av = mirror()->get(sema_schema::CONST_VAL.code, mirror_base());
        if (av.is_null()) return std::nullopt;
        return *av.as_ptr<const int64_t>(mirror_base());
    }

    LogosTypeBuilder to_builder() const;
};

// ── LogosTypeBuilder ──────────────────────────────────────────────────────
//
// 2c.4e.3.3: write-side companion to slim LogosType. Builder code populates
// fields freely and hands the result to TypePool::alloc, which writes them
// into the Hermes mirror and returns a slim LogosType*. The builder is also
// what TypeRef::to_builder() returns when callers need to copy-and-mutate
// an interned type.
struct LogosTypeBuilder {
    using Kind = LogosType::Kind;

    Kind kind = Kind::Error;

    // Ptr / Ref / MutRef — all use pointee
    bool        mut_ptr  = false;   // Ptr only: true → *mut, false → *const
    TypeRef     pointee;            // non-owning, pool-allocated

    // Ref / MutRef — lifetime annotation
    std::string lifetime;           // "'a", "'static", "'_", "" = elided

    // Array
    TypeRef     elem;               // non-owning, pool-allocated
    uint64_t    arr_size = 0;
    std::string arr_size_var;       // for symbolic size 'N'

    // Struct / Enum
    std::string struct_name;        // base struct name (owned; never mangled)
    std::string enum_name;          // enum name (owned)
    std::string pkg_name;           // package owning this type

    // Generic struct instantiation: Pair<i32> → struct_name="Pair", type_args=[i32]
    std::vector<TypeRef> type_args;

    // Lifetime arguments for struct instantiation
    std::vector<std::string> lifetime_args;

    // Tuple
    std::vector<TypeRef> tuple_elems;

    // Closure
    std::vector<TypeRef> closure_params;
    TypeRef     closure_ret;

    // TraitObject — &dyn Trait
    std::string trait_name;

    // TypeVar — name stored as a std::string (owns its storage)
    std::string type_var_name;

    // AssocType — associated type
    TypeRef     assoc_base;
    std::string assoc_type_name;
    std::vector<TypeRef> gat_args;

    // Constant literal value (for monomorphized constant generics)
    std::optional<int64_t> const_val;
};

// ── Trait bound (for type parameter bounds) ────────────────────────────────

struct TraitBound {
    std::string                   trait_name;  // e.g. "Into", "Add"
    std::vector<TypeRef> type_args;   // e.g. Into<i32> -> [i32]
};

// ── Type parameter ────────────────────────────────────────────────────────

struct TypeParam {
    std::string              name;          // e.g. "T"
    std::vector<TraitBound>  bounds;        // e.g. [Ord, Clone]
    bool                     is_variadic = false;  // T... variadic pack
    bool                     is_const    = false;  // const N: T
    TypeRef         const_type  = nullptr;
};

// Structural equality (pointer-to-pointer not checked — use value comparison).
bool types_equal(TypeRef a, TypeRef b) noexcept;

// Human-readable name for error messages.
std::string type_str(TypeRef t);

// Concrete struct name: plain structs → struct_name; generic insts → "Pair__i32__bool".
// Used by mono and mlir_gen to look up instantiated struct definitions.
std::string concrete_struct_name(TypeRef t);

// Raw variant that takes the struct base name + concrete type args directly.
// Used at a few call sites that would otherwise need to synthesise a stack
// LogosType (which bypasses TypePool's Hermes mirror). The args must already
// be concrete — no TypeVar / IntLit.
std::string concrete_struct_name_raw(std::string_view base_name,
                                     const std::vector<TypeRef>& type_args);


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

    TypeRef alloc(LogosTypeBuilder t);
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
