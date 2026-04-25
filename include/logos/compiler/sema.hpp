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

    // Kept on the slim struct for fast type tagging without a mirror lookup
    // (TypeRef::kind() still goes through the mirror so it works for any
    // legacy raw pointer; this duplicate just speeds the hot debug path).
    Kind kind = Kind::Error;

    // 2c.5.4: 32-byte TypeUID — canonical structural identity used as the
    // equality oracle (types_equal is ptr-eq || memcmp(type_uid)).
    // Layout per master plan: byte 0 = kind tag, bytes 1..23 = SHA-256
    // truncation of canonical type expression (lifetime ignored, matches
    // types_equal semantics), bytes 24..31 = reserved member-id (0 for
    // pure types; future trait-dispatch will populate).
    struct TypeUID {
        uint8_t bytes[32] = {};
        bool operator==(const TypeUID& o) const noexcept {
            return std::memcmp(bytes, o.bytes, 32) == 0;
        }
    };
    TypeUID type_uid;

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
    bool             mut_ptr  = false;    // Ptr only: true → *mut, false → *const
    const LogosType* pointee  = nullptr;  // non-owning, pool-allocated

    // Ref / MutRef — lifetime annotation
    std::string      lifetime;            // "'a", "'static", "'_", "" = elided

    // Array
    const LogosType* elem     = nullptr;  // non-owning, pool-allocated
    uint64_t         arr_size = 0;
    std::string      arr_size_var;        // for symbolic size 'N'

    // Struct / Enum
    std::string      struct_name;         // base struct name (owned; never mangled)
    std::string      enum_name;           // enum name (owned)
    std::string      pkg_name;            // package owning this type

    // Generic struct instantiation: Pair<i32> → struct_name="Pair", type_args=[i32]
    std::vector<const LogosType*> type_args;

    // Lifetime arguments for struct instantiation
    std::vector<std::string> lifetime_args;

    // Tuple
    std::vector<const LogosType*> tuple_elems;

    // Closure
    std::vector<const LogosType*> closure_params;
    const LogosType* closure_ret = nullptr;

    // TraitObject — &dyn Trait
    std::string      trait_name;

    // TypeVar — name stored as a std::string (owns its storage)
    std::string      type_var_name;

    // AssocType — associated type
    const LogosType* assoc_base = nullptr;
    std::string      assoc_type_name;
    std::vector<const LogosType*> gat_args;

    // Constant literal value (for monomorphized constant generics)
    std::optional<int64_t> const_val;
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
    // 2c.4e.3.1: fat-pointer storage. The mirror is the source of truth;
    // base_/off_ locate the TinyObjectMap directly, pool_ resolves
    // arena offsets back to LogosType* via mirror_inverse_. p_ remains as
    // the bridge for `.raw()` until 2c.4e.3.2 retires it.
    const LogosType*          p_    = nullptr;
    uint8_t*                  base_ = nullptr;
    hermes::arena_offset_t    off_{};
    const TypePoolImpl*       pool_ = nullptr;
public:
    constexpr TypeRef() noexcept = default;
    TypeRef(const LogosType* p) noexcept
        : p_(p),
          base_(p ? const_cast<uint8_t*>(p->hermes_arena_->head().data()) : nullptr),
          off_(p ? p->hermes_mirror_off_ : hermes::arena_offset_t{}),
          pool_(p ? p->hermes_pool_ : nullptr) {}

    // 2c.4e.2b: operator-> and operator* removed. Callers that reached
    // struct fields (e.g. `t.pointee()->kind`) must now use TypeRef
    // accessor methods (`t.pointee().kind()`). `.raw()` remains as the
    // last bridge to const LogosType* for APIs that still expect a
    // pointer (borrow_check, mono's SubstMap, types_equal); 2c.4e.2c will
    // flip storage to {base,off} and remove .raw() too.
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
    uint8_t* mirror_base() const noexcept { return base_; }
    const hermes::TinyObjectMap* mirror() const noexcept {
        return reinterpret_cast<const hermes::TinyObjectMap*>(base_ + off_.value());
    }
    const TypePoolImpl* pool() const noexcept { return pool_; }

    // 2c.4e.1: every LogosType reachable through TypeRef goes through
    // TypePool::alloc(), so the mirror is always wired. Accessors read
    // directly from the TinyObjectMap with no struct fallback.
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

    // 2c.4e.1: string accessors read the mirror ArenaString directly.
    // The returned string_view is valid as long as the arena is alive
    // (GrowableSingleChunk — lifetime matches the TypePool).
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

    // 2c.4e.3.0: vector accessors read the mirror's ObjectArray and return
    // by value. Callers using range-for, .size(), .empty(), op[] keep working
    // unchanged (rvalue lifetime-extends through full expression / range-for).
    // Out-of-line definitions live in sema.cpp (TypePoolImpl::mirror_inverse_
    // is needed to translate offsets back to LogosType*).
    std::vector<const LogosType*> type_args()      const noexcept;
    std::vector<const LogosType*> tuple_elems()    const noexcept;
    std::vector<const LogosType*> closure_params() const noexcept;
    std::vector<const LogosType*> gat_args()       const noexcept;
    std::vector<std::string>      lifetime_args()  const noexcept;

    // 2c.4e.1: const_val read from mirror. Returns by value — struct-ref
    // return is gone now that the mirror is authoritative.
    std::optional<int64_t> const_val() const noexcept {
        auto av = mirror()->get(sema_schema::CONST_VAL.code, mirror_base());
        if (av.is_null()) return std::nullopt;
        return *av.as_ptr<const int64_t>(mirror_base());
    }

    // 2c.4e.3.3: produce a LogosTypeBuilder reading every field from the
    // mirror. Used by the "copy and mutate one field" sites that previously
    // did `LogosType nt = *tv.raw()`. After LogosType is slimmed to back-refs
    // only, this remains the only sanctioned way to obtain a writable view
    // of an interned type.
    LogosTypeBuilder to_builder() const;
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
                                     const std::vector<const LogosType*>& type_args);


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

    const LogosType* alloc(LogosTypeBuilder t);
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
