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
#include <unordered_map>
#include <vector>
#include <optional>
#include <string>

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
        Datatype,                 // Hermes datatype (C POD layout, no heap types)
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

// Structural equality (pointer-to-pointer not checked — use value comparison).
bool types_equal(const LogosType& a, const LogosType& b) noexcept;

// Human-readable name for error messages.
std::string type_str(const LogosType* t);

// Concrete struct name: plain structs → struct_name; generic insts → "Pair__i32__bool".
// Used by mono and mlir_gen to look up instantiated struct definitions.
std::string concrete_struct_name(const LogosType* t);


// ── TypePool ───────────────────────────────────────────────────────────────
//
// Owns all LogosType objects.  std::deque gives pointer stability on push_back.
// Moved into LProgram so pointers remain valid after sema_lower() returns.

class TypePool {
    std::deque<LogosType> pool_;
public:
    TypePool() = default;
    TypePool(TypePool&&) = default;
    TypePool& operator=(TypePool&&) = default;

    // Non-copyable — pointer stability requires unique ownership.
    TypePool(const TypePool&) = delete;
    TypePool& operator=(const TypePool&) = delete;

    const LogosType* alloc(LogosType t) {
        pool_.push_back(std::move(t));
        return &pool_.back();
    }
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
