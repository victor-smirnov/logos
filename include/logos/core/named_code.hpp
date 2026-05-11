// Logos project — https://github.com/victor-smirnov/logos

#pragma once

namespace logos {

// NamedCode<T>: a numeric constant paired with a compile-time symbolic name.
//
// Architectural pattern: use instead of bare integer constants wherever the
// value appears in tracing, assertions, or switch statements. The name makes
// error messages and traces self-describing — especially useful when an LLM
// or a human reads a trace log:
//
//   "Required field 'LEFT' (1) not found"   vs   "Required field 1 not found"
//   "Unknown node code 'FLATTEN' (6)"       vs   "Unknown node code 6"
//
// Implicitly converts to T, so it composes with any API that takes a raw integer.
//
// Typical usage — define constants in a dedicated namespace:
//
//   namespace path_ast {
//       using Key  = NamedCode<uint8_t>;   // TinyObjectMap field key
//       using Code = NamedCode<int32_t>;   // node type discriminant
//
//       inline constexpr Key  LEFT     {"LEFT",     1};
//       inline constexpr Code FLATTEN  {"FLATTEN",  6};
//   }
template <typename T>
struct NamedCode {
    const char* name;
    T           code;

    constexpr NamedCode(const char* name, T code) noexcept
        : name(name), code(code) {}

    constexpr operator T() const noexcept { return code; }
};

} // namespace logos
