// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Victor Smirnov
// Logos project — https://github.com/victor-smirnov/logos
//
// Semantic analysis pass for the Logos compiler.
//
// sema_check() runs BEFORE mlir_gen. It builds a symbol table from all
// loaded module ASTs and type-checks every function body.  Any error is
// recorded as a Diag; the caller should abort compilation when ok() == false.
//
// No source-location tracking yet — errors report context by name
// (e.g. "fn quicksort", "struct Point").

#pragma once

#include <logos/hermes/view.hpp>

#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

namespace logos::compiler {

// ── Type representation ────────────────────────────────────────────────────

struct LogosType {
    enum class Kind {
        Void,                     // no return value
        I32, I64, F64, Bool, U8,  // signed/float/bool primitives
        I8, U32, U64,             // additional integer types
        Ptr,                      // *const T / *mut T
        Array,                    // [T; N]
        Struct,                   // user-defined struct
        Enum,                     // discriminant enum (stored as i32)
        IntLit,                   // unresolved integer literal (widens to any integer)
        Error                     // sentinel for ill-typed expressions
    };

    Kind kind = Kind::Error;

    // Ptr
    bool             mut_ptr  = false;    // true → *mut, false → *const
    const LogosType* pointee  = nullptr;  // non-owning, pool-allocated

    // Array
    const LogosType* elem     = nullptr;  // non-owning, pool-allocated
    uint64_t         arr_size = 0;

    // Struct / Enum
    std::string_view struct_name;         // view into Hermes arena
    std::string_view enum_name;           // view into Hermes arena (Enum kind)
};

// Structural equality (pointer-to-pointer not checked — use value comparison).
bool types_equal(const LogosType& a, const LogosType& b) noexcept;

// Human-readable name for error messages.
std::string type_str(const LogosType* t);

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

// ── Entry point ────────────────────────────────────────────────────────────

// Run semantic analysis over all parsed module ASTs.
// ASTs must remain alive for the duration of this call (string_views into them).
// filenames[i] is the source path for asts[i] — used in diagnostics.
SemaResult sema_check(const std::vector<logos::hermes::HermesCtr>& asts,
                      const std::vector<std::string>& filenames = {});

} // namespace logos::compiler
