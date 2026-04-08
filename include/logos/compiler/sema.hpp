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
#include <string>
#include <vector>

namespace logos::compiler {

// ── Type representation ────────────────────────────────────────────────────

struct LogosType {
    enum class Kind {
        Void,                     // no return value
        I32, I64, F64, Bool, U8,  // signed/float/bool primitives
        I8, U32, U64,             // additional integer types
        Ptr,                      // *const T / *mut T  (raw/unsafe pointer)
        Ref,                      // &T     — shared reference (borrow-checked)
        MutRef,                   // &mut T — exclusive mutable reference (borrow-checked)
        Array,                    // [T; N]
        Struct,                   // user-defined struct
        Class,                    // user-defined class (virtual dispatch via vtable)
        Enum,                     // discriminant enum (stored as i32)
        Tuple,                    // (T1, T2, ...) — anonymous product type
        Slice,                    // &[T] — fat pointer (ptr, len)
        Closure,                  // |params| -> ret (closure type)
        TraitObject,              // &dyn Trait — fat pointer {data, vtable}
        TypeVar,                  // abstract type variable (e.g. T in fn f<T>)
        IntLit,                   // unresolved integer literal (widens to any integer)
        AssocType,                // T::Item — type param's associated type (resolved by mono)
        ImplTrait,                // impl Trait — opaque return type, resolved during lowering
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

    // Struct / Enum
    std::string      struct_name;         // base struct name (owned; never mangled)
    std::string      enum_name;           // enum name (owned)

    // Generic struct instantiation: Pair<i32> → struct_name="Pair", type_args=[i32]
    // Empty for plain (non-generic) structs.
    std::vector<const LogosType*> type_args;

    // Tuple
    std::vector<const LogosType*> tuple_elems;  // element types (Tuple kind only)

    // Closure
    std::vector<const LogosType*> closure_params;  // parameter types
    const LogosType* closure_ret = nullptr;         // return type

    // TraitObject — &dyn Trait
    std::string      trait_name;          // e.g. "Display" (TraitObject kind only)

    // TypeVar — name stored as a std::string (owns its storage)
    std::string      type_var_name;       // e.g. "T" (TypeVar and AssocType kind)

    // AssocType — associated type: T::Item
    // type_var_name: the type param ("T")
    // trait_name:    the trait that declares it ("Iterator")
    std::string      assoc_type_name;     // e.g. "Item" (AssocType kind only)
};

// ── Trait bound (for type parameter bounds) ────────────────────────────────

struct TraitBound {
    std::string trait_name;   // e.g. "Ord", "Clone" — no runtime semantics until Batch F
};

// ── Type parameter ────────────────────────────────────────────────────────

struct TypeParam {
    std::string              name;          // e.g. "T"
    std::vector<TraitBound>  bounds;        // e.g. [Ord, Clone]
    bool                     is_variadic = false;  // T... variadic pack
};

// Structural equality (pointer-to-pointer not checked — use value comparison).
bool types_equal(const LogosType& a, const LogosType& b) noexcept;

// Human-readable name for error messages.
std::string type_str(const LogosType* t);

// Concrete struct name: plain structs → struct_name; generic insts → "Pair__i32__bool".
// Used by mono and mlir_gen to look up instantiated struct definitions.
std::string concrete_struct_name(const LogosType* t);

// Concrete class name: same mangling scheme for generic classes.
std::string concrete_class_name(const LogosType* t);

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
