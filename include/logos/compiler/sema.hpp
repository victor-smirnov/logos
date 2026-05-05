// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Victor Smirnov
// Logos project — https://github.com/victor-smirnov/logos
//
// Semantic analysis types: LogosType, TypePool, SemaResult, Diag.
//
// The entry point for semantic analysis + L-IR lowering is sema_lower(),
// declared in lir.hpp (which includes this header).

#pragma once

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>
#include <optional>
#include <set>
#include <format>
#include <string>
#include <string_view>

#include <logos/hermes/arena_string.hpp>

#include <logos/compiler/sema_schema.hpp>
#include <logos/hermes/arena.hpp>
#include <logos/hermes/tiny_object_map.hpp>
#include <logos/hermes/schema_codes.hpp>
#include <logos/hermes/view.hpp>
#include <logos/verification/assert.hpp>

namespace logos::compiler {

// ── Type representation ────────────────────────────────────────────────────

// 2c.6.6.B.6: LogosType is no longer an instantiated struct — it has no
// data and no instances. It survives only as a namespace-class holding the
// Kind enum and the TypeUID nested datatype. All readers use TypeRef
// (a fat pointer over the Hermes mirror); all writers use LogosTypeBuilder.
// Target pointer width (bits). Single source of truth for usize/isize size
// and any other pointer-sized lowering. Logos ships 64-bit only today; flip
// this constant to retarget. Lives in this header so both sema and mlir-gen
// can read it directly.
inline constexpr int g_target_pointer_bits = 64;

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
        Generic,                  // unapplied generic constructor (value-handle only; no pool entry)
        HStaticLit,               // HermesStatic literal at type-arg position (Foo::<@{...}>); identity = byte-hash over AST. const_val carries the low 64 bits. Inserted after Generic so existing kinds (Generic = 37) keep their numeric IDs.
        CfgSlotType,              // <type:CFG.SLOT> — type at top-level slot of a HermesStatic-typed binding. Carries `type_var_name` = CFG ident, `assoc_type_name` = slot key (reused fields). Resolved by mono_subst when CFG is bound to a concrete HStaticLit.
        Usize,                    // pointer-sized unsigned int (u32 on 32-bit, u64 on 64-bit). Distinct from u32/u64 — explicit `as` to/from fixed-width.
        Isize,                    // pointer-sized signed int. Distinct from i32/i64.
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

};

class TypePoolImpl;  // PIMPL — owns hermes::Arena and offset mapping

struct LogosTypeBuilder;  // defined below TypeRef

// ── TypeRef ───────────────────────────────────────────────────────────────
//
// Non-owning view over an interned type living in a TypePool. Carries the
// fat {arena, offset, pool} triple needed to read the Hermes mirror.
// Identity is the arena offset: two TypeRefs are equal iff they point at
// the same mirror node.

class TypeRef {
    const hermes::Arena*      arena_ = nullptr;
    hermes::arena_offset_t    off_{};  // NULL_OFFSET when null
    const TypePoolImpl*       pool_  = nullptr;
public:
    constexpr TypeRef() noexcept = default;
    constexpr TypeRef(std::nullptr_t) noexcept {}
    TypeRef(const hermes::Arena* a, hermes::arena_offset_t off,
            const TypePoolImpl* p) noexcept
        : arena_(a), off_(off), pool_(p) {}

    constexpr explicit operator bool() const noexcept {
        return off_ != hermes::NULL_OFFSET;
    }

    hermes::arena_offset_t offset() const noexcept { return off_; }

    friend constexpr bool operator==(TypeRef a, TypeRef b) noexcept {
        return a.off_ == b.off_;
    }
    friend constexpr bool operator==(TypeRef a, std::nullptr_t) noexcept {
        return a.off_ == hermes::NULL_OFFSET;
    }
    friend constexpr bool operator==(std::nullptr_t, TypeRef a) noexcept {
        return a.off_ == hermes::NULL_OFFSET;
    }

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

    // String accessors return realloc-safe owning views (refcounted MemHolder).
    // Implementation is out-of-line in sema.cpp because it needs MemHolder*,
    // which is reachable only through TypePoolImpl (PIMPL).
    hermes::OStringView lifetime()        const noexcept;
    hermes::OStringView struct_name()     const noexcept;
    hermes::OStringView enum_name()       const noexcept;
    hermes::OStringView pkg_name()        const noexcept;
    hermes::OStringView trait_name()      const noexcept;
    hermes::OStringView type_var_name()   const noexcept;
    hermes::OStringView assoc_type_name() const noexcept;
    hermes::OStringView arr_size_var()    const noexcept;

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
// Write-side companion to TypeRef. Builder code populates fields freely and
// hands the result to TypePool::alloc, which writes them into the Hermes
// mirror and returns a TypeRef. Also what TypeRef::to_builder() returns
// when callers need to copy-and-mutate an interned type.
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
    // Associated-type equality clauses: `Trait<Item = i32>` → [{ "Item", i32 }].
    // Only populated when the bound includes `Name = Type` arguments.
    std::vector<std::pair<std::string, TypeRef>> assoc_eqs;
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
// Owns the Hermes arena that backs all interned types. Each unique type
// lives as a TinyObjectMap inside that arena; TypeRef is a fat pointer
// into it. The pool is moved into LProgram so the arena stays alive for
// the rest of the compilation pipeline.

class TypePool {
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

    // Phase 3b: expose the underlying Hermes arena. The compiler's L-IR mirror
    // shares this arena with the type mirror so cross-references (TypeRef
    // offsets stored on L-IR nodes, sub-expression offsets, etc.) all live in
    // a single offset space. Returns nullptr if the pool has not yet allocated
    // (no calls to alloc()).
    hermes::Arena*       arena() noexcept;
    const hermes::Arena* arena() const noexcept;

    // Phase 3b: ensure the pool's arena is initialised (allocates the empty
    // arena if no types have been interned yet). Used by the L-IR mirror
    // emitter when the program contains no LogosType allocations.
    hermes::Arena&       arena_or_init();

    // Phase 3d: expose the impl pointer so lir_view callers can wrap a raw
    // arena offset into a TypeRef (TypeRef stores pool* for trait/method
    // resolution; nullptr-pool TypeRefs work for kind/name accessors only).
    const TypePoolImpl* impl() const noexcept { return impl_.get(); }

    // Component-metaprog slice 1B: public access to per-type 32-byte UID.
    LogosType::TypeUID uid_of(TypeRef t) const noexcept;
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
        // B-li-01: dedupe across all print calls in this process so the
        // multi-phase sema driver doesn't emit the same warning 3×.  Errors
        // are also deduped — repeating an error N times adds no signal.
        // Keyed by (level, file, line, message); context is omitted because
        // multi-phase reruns may produce different contexts for the same
        // underlying issue.
        static std::set<std::string> g_seen;
        for (auto& d : diags) {
            std::string key = std::format("{}|{}|{}|{}",
                int(d.level), d.file, d.line, d.message);
            if (!g_seen.insert(std::move(key)).second) continue;
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
