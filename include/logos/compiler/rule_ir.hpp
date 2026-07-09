// Logos project — https://github.com/victor-smirnov/logos
//
// Rule-IR handoff: a Writ document the COMPILER built, walked by a Logos
// metaprogram, zero-copy.
//
// The token-macro seam can only carry `(u8* ptr, i64 len)` byte ranges, so a
// rule body reached the stdlib handler as TEXT and was parsed a second time
// there. Now the compiler parses it (with the C++ parser generated from the
// same wql.peg the Logos runtime parser comes from) and hands the resulting
// `RQProgram` across as MEMORY: one pointer.
//
// This is the same trick `logos_get_module_ast_oview` already plays with the
// module AST — nothing owning crosses the boundary, only a raw base pointer
// into a never-move arena, valid for as long as the compiler keeps the doc.
//
// Two invariants make it sound:
//   • the doc is a MultiChunk arena — it APPENDS chunks and never relocates, so
//     a pointer handed out stays valid (a GrowableSingleChunk would dangle on
//     grow; that is the arena-realloc hazard this codebase already knows);
//   • the doc outlives the metaprog dispatch loop that runs the handler.
//
// The Logos side needs no deserialiser: `WAny::Ref` resolves to an absolute
// pointer and `.view::<S>()` is a compiler builtin that casts. A handler walks
// the foreign tree and allocates its own new nodes in its own `Writ` — a
// self-relative edge from a foreign slot to a local node is computed from the
// slot's own address, so cross-arena refs are legal in both directions.

#pragma once

#include <stdint.h>

#include <logos/writ/compat.hpp>

namespace logos::compiler::rule_ir {

// Take ownership of `doc` and remember `root` for `site_id`. Overwrites.
void put(uint64_t site_id, logos::writ::Writ doc, logos::writ::AnyVal root);

// The absolute address of the root object, or nullptr if `site_id` is unknown.
// This is what the JIT'd thunk hands the handler.
const uint8_t* root_ptr(uint64_t site_id);

// Drop every stored doc. Called when the metaprog dispatch loop is done: the
// pointers handed out are dead from here on, exactly like the macro-arg blobs.
void clear();

}  // namespace logos::compiler::rule_ir
