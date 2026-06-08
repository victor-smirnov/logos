// Logos project — https://github.com/victor-smirnov/logos
//
// Logos Typed IR (L-IR) — produced by the semantic analysis pass.
//
// L-IR is the compiler's first fully-typed intermediate representation.
// Every expression node carries a `TypeRef type`.  All name
// lookups, parentheses, and type aliases have been resolved.  The IR
// is suitable for monomorphisation (Batch D) without re-running sema.
//
// Ownership: all LogosType* pointers inside an LProgram are owned by
// LProgram::type_pool (a TypePool value).  Do not outlive LProgram.
//
// Node types use std::variant + std::unique_ptr for recursive sub-nodes.

#pragma once

#include <logos/compiler/sema.hpp>   // LogosType, TypePool, SemaResult
#include <logos/compiler/str_map.hpp>
#include <logos/hermes/config.hpp>    // arena_offset_t
#include <logos/hermes/external_ref.hpp>  // ExternalRef (Phase 4.A: LFunction.body_external_ref)
#include <unordered_set>
#include <string>

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

namespace logos::compiler::lir {

// ── Forward declarations ──────────────────────────────────────────────────

struct LExpr;
struct LStmt;
struct LBlock;
struct LFunction;

// ADR 0007 slice 1b: LExpr is pool-owned by LProgram::expr_pool_.
// LExprPtr is now a non-owning raw handle — allocate via lir::alloc_expr(prog).
// Variant fields previously holding unique_ptr<LExpr> now hold raw LExpr*.
// std::move() on LExprPtr remains legal (it's a no-op pointer copy).
using LExprPtr     = LExpr*;
// ADR 0007 slice 1c: LBlock is pool-owned by LProgram::block_pool_.
// LBlockPtr is a non-owning raw handle — allocate via lir::alloc_block(prog, ...).
using LBlockPtr    = LBlock*;
// M5 step 6: shared_ptr so SemaCache can hold per-binary-AST cached
// LFunctions across sema_lower invocations. Refcount-cheap copy on
// install (no per-fn deep-clone). All call sites that `push_back`
// `std::make_unique<LFunction>(...)` continue to compile — shared_ptr
// has an implicit converting constructor from unique_ptr.
using LFunctionPtr = std::shared_ptr<LFunction>;

// ── Patterns (for match arms) ─────────────────────────────────────────────
//
// B.6 Stage 3.5 step 7e/7f: leaf payload structs (PatInt/PatBool/PatVariant/
// PatWild/PatRange) were deleted — their data flows directly into the mirror
// via lir_mirror_emit_pat_*. Compound payload structs below are scratchpad
// types: sema_stmt fills their fields, then passes to lir_mirror_emit_pat_X.

// Pattern with payload bindings: Option::Some(x) => { use x }
struct PatVariantData {
    std::string enum_name;
    std::string variant;
    int64_t     disc;
    std::vector<std::string>        bindings;      // bound variable names
    std::vector<TypeRef>   binding_types;  // their types
};

// OR pattern: 1 | 2 | 3 — each alternative must be a non-OR pattern.
struct PatOr;
// Tuple pattern: (a, b, c) — matches a tuple, binding each element.
struct PatTuple;
// Struct pattern: Point { x: p, y } — binds/tests struct fields.
struct PatStruct;
// Slice pattern: [a, b] or [first, .., last].
struct PatSlice;
// @ binding: name @ sub_pat — binds scrutinee to name AND tests sub_pat.
struct PatAt;
// ref / ref mut binding — binds scrutinee by reference.
struct PatRefBind {
    std::string      name;
    bool             is_mut;
    TypeRef bind_type;  // &T or &mut T
};
// Reference pattern: &pat or &mut pat — strips one level of indirection.
struct PatRefPat;

// Forward declaration: Pattern is defined after all alternatives are complete
// (see end of pattern section). Sub-pattern containers (vector<Pattern>) work
// with the forward decl.
struct Pattern;

struct PatOr   { std::vector<Pattern> alts; };
struct PatTuple {
    std::vector<std::string>        bindings;      // bound variable names (or "_" to skip)
    std::vector<TypeRef>   binding_types; // their types (filled by sema)
    std::vector<Pattern>            subs;          // sub-pattern per element (parallel to bindings)
};
// PatFieldBinding: sub empty = shorthand binding, sub[0] = explicit sub-pattern.
struct PatFieldBinding {
    std::string          field_name;
    std::vector<Pattern> sub;   // 0 = shorthand, 1 = explicit
};
struct PatStruct {
    std::string                   struct_name;
    std::vector<PatFieldBinding>  fields;
    bool                          has_rest;
};
struct PatSlice {
    std::vector<Pattern>  prefix;   // elements before ..
    std::vector<Pattern>  rest;     // 0 = no rest, 1 = has rest (..)
    std::vector<Pattern>  suffix;   // elements after ..
};
struct PatAt {
    std::string          name;
    std::vector<Pattern> sub;   // exactly 1 element: the inner pattern
    TypeRef     type;
};
struct PatRefPat {
    std::vector<Pattern> inner;  // exactly 1 element: the dereferenced pattern
    bool                 is_mut;
};

// B.6 Stage 3.5 step 7e: Pattern is a POD shell — payload lives in the LIR
// mirror; pattern variant kinds are transient parameter packs, not stored.
struct Pattern {
    mutable hermes::arena_offset_t mirror_offset_{};

    Pattern() = default;
    Pattern(const Pattern&) = default;
    Pattern(Pattern&&) noexcept = default;
    Pattern& operator=(const Pattern&) = default;
    Pattern& operator=(Pattern&&) noexcept = default;
};

struct LMatchArm {
    Pattern                  pat;
    LBlockPtr                body = nullptr;   // arm body (single stmts are wrapped in a 1-stmt block)
    std::optional<LExprPtr>  guard;  // if-guard: arm only matches when guard is true
};

// ── Hermes SDN literal tree ───────────────────────────────────────────────

struct HermesVal;
// ADR 0007 slice 1c: HermesVal is pool-owned by LProgram::hermes_val_pool_.
using HermesValPtr = HermesVal*;

struct HVNull  {};
struct HVBool  { bool value; };
struct HVInt   { int64_t value; };
struct HVFloat { double value; };
struct HVStr   { std::string value; };

struct HVMapEntry {
    std::variant<std::string, int64_t> key;
    HermesValPtr val = nullptr;
};

struct HVMap   {
    std::vector<HVMapEntry> entries;
    std::string key_type;  // "" = ObjectMap (tc=101); "I32" = MapI32AnyVal (tc=105)
};
struct HVArray {
    std::vector<HermesValPtr> elements;
    std::string elem_type;  // "" = AnyVal (ObjectArray tc=100); "I32" = ArrayI32 tc=104; "U64" = ArrayU64 tc=108
};

// Runtime capture placeholder: $x or ${expr} inside an @-literal.
// param_index: position of this PARAM slot in the template blob (0-based, unique per slot).
// value_index: deduplicated capture value (multiple slots can share one value_index if
//              they capture the same pure identifier — one coercion, multiple writes).
struct HVCapture {
    uint32_t param_index;   // slot position in template (unique)
    uint32_t value_index;   // which resolved value to use (deduplicated)
};

// Component-metaprog slice 1B: first-class Logos Type embedded in @-literal.
// Lowers to a TinyObjectMap with schema_type_code = type_hash::Type (107) at codegen.
struct HVType {
    uint32_t    kind;
    uint64_t    uid;   // first 8 bytes of full 32-byte type UID
    std::string name;
};

struct HermesVal {
    mutable hermes::arena_offset_t mirror_offset_{};  // Stage 3g.2 back-pointer

    HermesVal() = default;
    HermesVal(const HermesVal&) = default;
    HermesVal(HermesVal&&) noexcept = default;
    HermesVal& operator=(const HermesVal&) = default;
    HermesVal& operator=(HermesVal&&) noexcept = default;
};

// A Hermes SDN literal (@{...}, @[...], @scalar) lowered to a tree.
// If has_captures == false: pure compile-time blob (current ZoneBuilder path).
// If has_captures == true:  template blob + runtime substitution.
//   capture_exprs[v] = Logos expression for value_index v.
//   capture_types[v] = resolved LogosType* for value_index v (for coercion).
//   capture_param_count = total number of PARAM slots in template.
struct EHermesLit {
    HermesValPtr root = nullptr;
    bool has_captures = false;
    std::vector<LExprPtr>                    capture_exprs;   // one per unique value
    std::vector<TypeRef>                     capture_types;   // one per unique value
    uint32_t                                 capture_param_count = 0; // total slots
    // M.x: pre-serialised Hermes blob (metacall HermesStatic splice). When
    // non-empty, codegen emits these bytes directly into rodata as
    // [u64 size][bytes] and returns HermesStatic{ptr=global+8}. `root` and
    // capture-related fields are ignored on this path.
    std::string                              static_blob;
};

// ── Expression node payloads ──────────────────────────────────────────────

struct ELitInt    { int64_t value; };
struct ELitFloat  { double value; };
struct ELitBool   { bool value; };
struct ELitStr    { std::string value; };

struct EVarRef    { std::string name; };

struct EEnumLit   {
    std::string enum_name;
    std::string variant;
    int64_t     disc;           // discriminant value (fits backing type, default i32)
};

// Enum variant with payload: Option::Some(42)
struct EEnumLitData {
    std::string           enum_name;
    std::string           variant;
    int64_t               disc;
    std::vector<LExprPtr> payload;  // payload values
};

struct ECall      {
    std::string                   callee;
    std::vector<TypeRef> type_args;  // empty for non-generic calls
    std::vector<LExprPtr>         args;
};

struct EMethodCall {
    LExprPtr                      receiver = nullptr;
    std::string                   method;
    // Concrete function symbol selected by sema for direct calls.
    // Empty means "resolve by receiver type + method name" in later phases.
    std::string                   resolved_symbol{};
    std::vector<TypeRef> type_args;  // [NEW] for generic methods
    std::vector<LExprPtr>         args;
    int32_t                       vtable_index = -1;  // -1 = direct (struct), >=0 = virtual (class)
    std::string                   resolved_type{};    // class/struct where method is defined (for inherited methods)
    // Tag-dispatch info: set when receiver is &tagged<TS> Trait.
    // When non-empty, codegen reads type_code via TS and dispatches through
    // the @__logos_tag_dispatch_<tag_system>_<trait_name>_<method> table.
    std::string                   tag_system{};       // e.g. "DataTypeTagSystem" (empty = not tagged dispatch)
    std::string                   tag_trait{};        // e.g. "Stringify"
};

struct EBinOp {
    std::string op;             // "+", "-", "==", "&&", ...
    LExprPtr    lhs = nullptr;
    LExprPtr    rhs = nullptr;
};

struct EUnary {
    std::string op;             // "-" or "!"
    LExprPtr    operand = nullptr;
};

// & address-of: returns alloca pointer for a variable (does not dereference)
struct EAddrOf {
    std::string var_name;
};

// Address of a temporary rvalue: &expr where expr is not a named variable.
// Codegen spills the inner expression to an anonymous alloca.
struct EAddrOfTemp {
    LExprPtr inner = nullptr;
    bool     is_mut = false;  // true → &mut T, false → &T
};

struct EDeref {
    LExprPtr operand = nullptr;
};

struct EFieldRead {
    LExprPtr    receiver = nullptr;
    std::string field;
};

struct EIndexRead {
    LExprPtr receiver = nullptr;
    LExprPtr index = nullptr;
};

struct EStructLit {
    std::string name;
    std::vector<std::pair<std::string, LExprPtr>> fields;
};

struct EArrLit {
    std::vector<LExprPtr> elems;
    // element type is LExpr::type->elem
};

struct ECast {
    LExprPtr operand = nullptr;
    // target type is LExpr::type.
    // For Hermes typed container casts (e.g. &[i32] as <I32>[]):
    //   hermes_build_fn names the stdlib builder (e.g. "hermes_build_array_i32").
    //   source type is Slice; result type is Hermes.
    std::string hermes_build_fn = {};  // empty = ordinary numeric/pointer cast
};

// if cond { then_val } else { else_val }  — used when if is an expression.
// Both branches must yield the same type.
struct EIfExpr {
    LExprPtr cond = nullptr;
    LExprPtr then_val = nullptr;
    LExprPtr else_val = nullptr;
};

// Match expression arm: pattern [guard] => expr
struct EMatchArm {
    Pattern                  pat;
    std::optional<LExprPtr>  guard;
    LExprPtr                 value = nullptr;
};

// match expr { pat => val, ... } — produces a value
struct EMatchExpr {
    LExprPtr               scrut = nullptr;
    std::vector<EMatchArm> arms;
};

// Tuple literal: (a, b, c)
struct ETupleLit {
    std::vector<LExprPtr> elems;
};

// Tuple element access: t.0, t.1
struct ETupleIndex {
    LExprPtr  receiver = nullptr;
    uint32_t  index;
};

// Closure: wrapper for the full EClosure (defined after LBlock).
// ADR 0007 slice 1c: EClosure is pool-owned by LProgram::closure_pool_.
struct EClosure;
struct EClosureBox {
    EClosure* inner = nullptr;
};

// Closure call: closure(args...)
struct EClosureCall {
    LExprPtr              callee = nullptr;
    std::vector<LExprPtr> args;
};

// Call via fn(T) -> R bare function pointer (no env_ptr, no fat pointer).
struct EFnPtrCall {
    LExprPtr              callee = nullptr;  // EVarRef to the fn-ptr variable
    std::vector<LExprPtr> args;
};

// Slice construction: &arr (whole array → slice) or &arr[lo..hi]
struct ESliceLit {
    LExprPtr base = nullptr;    // pointer to first element
    LExprPtr len = nullptr;     // length as i64
};

// Slice element access: s[i]
struct ESliceIndex {
    LExprPtr slice = nullptr;
    LExprPtr index = nullptr;
};

// Slice length: s.len()
struct ESliceLen {
    LExprPtr slice = nullptr;
};

// Slice / str as_ptr: s.as_ptr() → *const u8
struct ESlicePtr {
    LExprPtr slice = nullptr;
};

// format() compiler built-in: format("x={}, y={}", x, y)
// Returns *mut u8 (heap-allocated, caller frees via format_free).
// The compiler builds tags[] and data[] arrays and calls __format_impl.
struct EFormatCall {
    LExprPtr                    fmt = nullptr;        // format string expr
    std::vector<LExprPtr>       args;       // arguments (without fmt)
    std::vector<TypeRef> arg_types; // parallel to args, resolved at sema
};

// Variadic pack expansion: args... in function body.
// Expanded by mono into individual EVarRef nodes.
struct EPackExpand {
    std::string var_name;  // the pack variable being expanded (e.g. "rest")
};

// sizeof::<T>() — size in bytes of type T, computed at compile time via GEP trick.
struct ESizeOf {
    TypeRef elem_type = nullptr;
};

// alignof::<T>() — alignment in bytes of type T, computed at compile time via GEP trick.
struct EAlignOf {
    TypeRef elem_type = nullptr;
};

// Generic-fn reference at expression position: `IDENT::<T1, T2, ...>` (no call).
// Carries the pre-mangle base name + type-args (which may contain TypeVars in
// generic-context uses). Mono's subst_expr substitutes the TypeVars under the
// current SubstMap, mangles the symbol via mangle(name, subst_args), enqueues
// the instantiation, and rewrites this node into a plain EVarRef whose name
// is the mangled symbol and whose type is FnPtr. Never reaches mlir-gen.
struct EGenericRef {
    std::string          name;       // pre-mangle base
    std::vector<TypeRef> type_args;  // may contain TypeVars at template-time
};

// Raw-pointer arithmetic intrinsic methods:
//   p.byte_add(n)         — byte_add: offset n bytes, result same pointer type
//   p.byte_sub(n)         — byte_sub: offset -n bytes
//   p.add(n) / p.sub(n)   — scale n by sizeof(pointee), then byte offset
//   p.byte_offset_from(q) — (p - q) in bytes, result i64
//   p.offset_from(q)      — (p - q) / sizeof(pointee), result i64
struct EPtrArith {
    enum Op { ByteAdd, ByteSub, Add, Sub };
    Op       op;
    LExprPtr ptr = nullptr;
    LExprPtr offset = nullptr;
};

struct EPtrDiff {
    bool     by_byte;   // true = byte distance, false = element distance
    LExprPtr lhs = nullptr;
    LExprPtr rhs = nullptr;
};

// type_code_of::<T>() — Hermes wire-format type_code of T.  Deferred to mono
// so each instantiation of a generic function gets T's own type_code.
struct ETypeCodeOf {
    TypeRef elem_type = nullptr;
};

// Try expression: expr? — extract Ok(v) or early-return Err(e).
// inner must have enum type "Result" with 2 type args [T, E].
// ok_disc / err_disc are the discriminant values for Ok and Err variants.
// The ETry expression itself has type T (the Ok payload type).
struct ETry {
    LExprPtr inner = nullptr;
    int32_t  ok_disc  = 0;   // discriminant of Ok  (typically 0)
    int32_t  err_disc = 1;   // discriminant of Err (typically 1)
};

// Represents an inline block of statements returning a final value
struct EBlockExpr {
    LBlockPtr block = nullptr;
    LExprPtr result = nullptr; // may be null if it evaluates to void
};


// reflect::<T>() — returns HermesStatic view of T's TypeInfo rodata global.
struct EReflectOf { TypeRef type; };

// ── Expression node ───────────────────────────────────────────────────────

struct LExpr {
    TypeRef type = nullptr;   // always set; error_t() on ill-typed nodes
    // Stage 3g.2 / 7e: back-pointer to this node's Hermes mirror is the only
    // payload — the variant kind has been dropped, all readers go through the
    // mirror via lir_view::ExprRef.
    mutable hermes::arena_offset_t mirror_offset_{};
};

// ── Statement node payloads ───────────────────────────────────────────────

struct SLet {
    std::string      name;
    TypeRef type;         // concrete type (annotations resolved; IntLit → i32)
    bool             is_mut;
    LExprPtr         value = nullptr;
};

struct SAssign    { std::string name; LExprPtr value = nullptr; };

struct SReturn    { LExprPtr value = nullptr; };   // value is null for void return

// else_: null → no else; block with single SIf → else-if chain
struct SIf {
    LExprPtr                  cond = nullptr;
    LBlockPtr                 then_ = nullptr;
    std::optional<LBlockPtr>  else_;
};

struct SWhile {
    LExprPtr  cond = nullptr;
    LBlockPtr body = nullptr;
    std::string label;  // optional loop label (e.g. "'outer"), empty = unlabeled
};

struct SFor {
    std::string      var;
    LExprPtr         lo = nullptr;
    LExprPtr         hi = nullptr;
    bool             inclusive;
    LBlockPtr        body = nullptr;
    std::string      label;  // optional loop label, empty = unlabeled
};

struct SLoop {
    LBlockPtr        body = nullptr;
    TypeRef result_type = nullptr;  // non-null when loop yields a value
    std::string      break_slot;             // alloca name for the break value (non-empty ↔ result_type != null)
    std::string      label;                  // optional loop label, empty = unlabeled
};
struct SBreak     { LExprPtr value = nullptr; std::string label; };  // label: target loop label (may be empty)
struct SContinue  { std::string label; };                   // label: target loop label (may be empty)
struct SBlock     { LBlockPtr body = nullptr; };  // scoping block statement

struct SFieldWrite {
    std::string receiver;
    std::string field;
    LExprPtr    value = nullptr;
};

struct SIndexWrite {
    std::string arr;
    LExprPtr    index = nullptr;
    LExprPtr    value = nullptr;
};

// a.field[index] = value — field index write (e.g. self.ptr[i] = val)
struct SFieldIndexWrite {
    std::string receiver;   // struct/class variable
    std::string field;      // pointer-typed field name
    LExprPtr    index = nullptr;
    LExprPtr    value = nullptr;
};

// (*ptr_var).field = value  — field write through a named pointer variable
struct SDerefFieldWrite {
    std::string receiver;    // variable name (holds *mut ClassName)
    std::string type_name;   // class or struct name of the pointee
    std::string field;
    LExprPtr    value = nullptr;
};

// a.b.c.…z = value  — chained field write (2+ levels deep, N-ary).
// Path order: receiver → mid_field → extras[0] → extras[1] → … → field.
// `extras` is empty for the legacy depth-2 form (a.b.c).
// Emitted as a chain of GEPs (auto-deref'ing pointer fields) terminating in a store.
struct SChainFieldWrite {
    std::string              receiver;    // outer variable name
    std::string              mid_field;   // first intermediate field
    std::vector<std::string> extras;      // additional intermediates between mid_field and field
    std::string              field;       // final field name (write target)
    LExprPtr                 value = nullptr;
};

struct SExprStmt  { LExprPtr expr = nullptr; };

// *ptr = value;  — write through a raw pointer
struct SDerefWrite { LExprPtr ptr = nullptr; LExprPtr value = nullptr; };

// var.N = value;  — tuple field write (N is a small integer index)
struct STupleWrite {
    std::string      receiver;      // local variable holding the tuple
    uint32_t         index;         // field index (0, 1, ...)
    LExprPtr         value = nullptr;
    TypeRef recv_type = nullptr;  // LogosType of the tuple variable
};

// for item in array { body } — iterates over a fixed-size array
struct SForEach {
    std::string      var;         // loop variable name (item)
    LExprPtr         iter = nullptr;        // the array or slice expression
    TypeRef elem_type;   // element type
    int64_t          arr_size;    // static array size; 0 for slices
    bool             is_slice = false;  // true → iter is &[T] (dynamic length from fat pointer)
    LBlockPtr        body = nullptr;
};

struct SMatch {
    LExprPtr               scrut = nullptr;
    std::vector<LMatchArm> arms;
};

// let-else: let Pat = expr else { block (must diverge) };
// After this statement, the pattern's bindings are in scope.
struct SLetElse {
    Pattern               pat;        // the irrefutable-or-test pattern
    LExprPtr              scrut = nullptr;      // scrutinee expression
    LBlockPtr             else_block = nullptr; // must-diverge block
    // G161-3: refutable-inner guard exprs (e.g. `__refut_0 == 1` for
    // `let Some(1) = … else`). Each must hold after the pattern's bindings are
    // bound, else the else block runs. Empty for a plain variant/literal test.
    std::vector<LExprPtr> guards;
};

// Auto-generated drop call: Type__drop(var) at scope exit
struct SDrop {
    std::string      var_name;
    std::string      drop_fn;          // user's explicit drop (may be empty)
    TypeRef type;
    bool             drop_fields = false;  // auto-drop droppable fields after drop_fn
    std::vector<std::string> moved_fields; // field names of `var_name` consumed by move; auto-drop must skip them
};

// ── Statement node ────────────────────────────────────────────────────────

struct LStmt {
    uint32_t line = 0;             // source line (0 = unknown)
    mutable hermes::arena_offset_t mirror_offset_{};  // Stage 3g.2 back-pointer

    LStmt() = default;
    LStmt(const LStmt&) = default;
    LStmt(LStmt&&) noexcept = default;
    LStmt& operator=(const LStmt&) = default;
    LStmt& operator=(LStmt&&) noexcept = default;
};

// ── Block ─────────────────────────────────────────────────────────────────

struct LBlock {
    std::vector<LStmt> stmts;
    mutable hermes::arena_offset_t mirror_offset_{};  // Stage 3g.2 back-pointer
};

// ── Top-level declarations ────────────────────────────────────────────────

struct LParam {
    std::string      name;
    TypeRef type;
    bool             is_variadic = false;  // variadic pack parameter
    // A by-value `Box<dyn Trait>` param: the type collapses to a bare
    // TraitObject, but the callee OWNS the heap handle (frees it via
    // __box_dyn__drop). Call sites must coerce the arg to a HEAP fat handle
    // (not a stack fat pair) so the callee's free() is valid.
    bool             owning_box_dyn = false;
};

// EClosure — defined after LParam and LBlock (both needed).
struct EClosure {
    std::string                     closure_id;
    std::vector<LParam>             params;
    TypeRef                ret_type = nullptr;
    LBlock                          body;
    bool                            is_move = false;
    std::vector<std::string>        captures;
    std::vector<TypeRef>   capture_types;
    // C5-cl-08: per-capture flag — set when the closure body mutates the
    // captured outer variable (Assign / CompoundAssign / DerefWrite of an
    // address-of-capture). True entries are stored in the env struct as a
    // raw pointer to the outer alloca instead of a value copy, so mutations
    // round-trip back. Size matches `captures` once sema finishes the scan.
    std::vector<bool>               mut_captures;
    // RFC-2229 phase-1: the FIELD PATH that the closure body actually reads
    // (parallel to `captures`/`mut_captures`; one entry per capture). Examples:
    // `||p.x`     → captures=["p"], capture_paths=["p.x"];
    // `||p`       → captures=["p"], capture_paths=["p"];
    // `||p.x.y`   → captures=["p"], capture_paths=["p.x.y"].
    // Env layout + codegen keep capturing the WHOLE root variable (no behaviour
    // change at runtime — non-move closures stay correct, sequential). But
    // borrow-check uses the path to register a borrow on the precise field —
    // RFC-2229 disjoint-sibling exclusivity (`&mut p.x` while a `||p.x` closure
    // is live is rejected; disjoint `&mut p.y` is allowed). When multiple paths
    // off the same root are read, the recorded path is their lowest common
    // ancestor (still sound; widening allows disjoint where Rust does).
    std::vector<std::string>        capture_paths;
    // RFC-2229 phase-2 (move-precision): for a narrow capture path (e.g. "p.x"),
    // the TYPE of the path itself (the field) — distinct from capture_types[i]
    // which is the ROOT type. When set, the env-slot for this capture is
    // field-sized (not root-sized), the env-fill reads from the outer struct's
    // FIELD slot (move only `p.x`, leave `p.y` intact), and the body's unpack
    // materialises a fake root struct populated only at this field path so the
    // body's FieldRead(root, …) chain works. nullptr (default) = whole-root
    // capture, current behaviour. Parallel to `captures`.
    std::vector<TypeRef>            capture_field_types;
    // When true: non-capturing closure coerced to fn ptr; emitted without env_ptr.
    bool                            as_fn_ptr = false;
    // G167-3b: the closure value escapes its creating frame (it is BOXED —
    // lowered where the expected type is `Box<…Fn…>`). Its captured-env must
    // be HEAP-allocated rather than stack-`alloca`'d, else the env pointer
    // dangles once the creating fn returns (boxing gives the closure heap
    // lifetime). Non-escaping closures (iterator-adapter args, locals) keep
    // the cheap stack env.
    bool                            escapes = false;
};

struct LFunction {
    std::string              name;
    // Unmangled method name as written in the source (e.g. "cow_clone").
    // Equals the bare fn name for free fns; for struct/impl methods this
    // is the part after `Target__`. Used by mlir_gen_dyn's vtable
    // construction to bind trait methods to their impl symbols by
    // exact-name lookup instead of mangling-string heuristics.
    std::string              method_base;
    // Package the fn was declared in (sema's `package …;` of the source
    // module). Used by mlir_gen's pkg-rename pass to disambiguate
    // same-named struct methods from distinct pkgs (Box-vs-UserBox).
    std::string              package;
    std::vector<TypeParam>   type_params;    // TypeVar names (generic def, empty otherwise)
    std::vector<std::string> lifetime_params; // Lifetime param names, e.g. ["'a", "'b"]
    // B65: outlives bounds declared in the function header or where clause.
    // Each pair (long, short) means "'long: 'short" (i.e. 'long lives at least
    // as long as 'short). Built into a transitive outlives graph at query time.
    std::vector<std::pair<std::string, std::string>> lifetime_outlives;
    std::vector<LParam>      params;
    TypeRef         ret_type  = nullptr;
    LBlock                   body;
    bool                     is_extern = false;
    bool                     is_vararg = false;
    bool                     is_pub    = false;
    // Phase 7 slice 17: set by sema in metaprog-mode for entry-file fns
    // whose body was skipped (non-handler user fns). meta_prog driver must
    // filter these out before mono/MLIR — the body is empty and there's
    // nothing valid to lower.
    bool                     is_metaprog_stub = false;

    // Specialisation support (set by sema, cleared by mono after instantiation).
    // is_specialization == true  →  this is a specialisation of `name`.
    // spec_patterns: one LogosType* per type-param position; may contain TypeVar
    //   for partial specialisations (e.g. fn foo<*T> → pattern = *const TypeVar<T>).
    bool                          is_specialization = false;
    std::vector<TypeRef> spec_patterns;

    // Set when this function was loaded from a binary module (.hermes0 in .a).
    // Non-generic functions with this flag are forward-declared only; the linker
    // resolves them from the archive's .o member.
    bool from_binary_module = false;

    // Phase 6 (multi-arena IR) item-level lazy. Set when this function comes
    // from a `lowering lazy` archive: its body is lowered into the consumer's
    // LProgram (same path as user code), but a post-mono reach analysis
    // determines whether mlir-gen actually emits a body. Lazy fns not in the
    // reach closure of any non-lazy fn are skipped at codegen, eliminating
    // the per-consumer "lower-everything" tax for big lazy libraries.
    bool from_lazy_module = false;

    // Phase 4.A (multi-arena IR): when LOGOS_SEMA_USE_BLOB=1, sema skips
    // body lowering for non-generic from_binary_module fns and stores a
    // cross-arena handle here pointing into the registered library arena.
    // Default INVALID — body was lowered locally (legacy path) or is not
    // available cross-arena (e.g. generic templates).
    // Phase 4.B will resolve the target obj_id via the library's name→obj_id
    // export table; Phase 4.C will teach mono/codegen to traverse it.
    hermes::ExternalRef body_external_ref{};

    // Absolute path of the source file this function was lowered from.
    // Empty for fns reconstructed from binary modules. Used by the
    // --emit-file mode to filter mlir-gen body emission to a single
    // source file (other files' fns become forward-decls only and the
    // linker resolves at archive-merge time).
    std::string source_file;

    // Impl-level type params (with their bounds) that were stripped from
    // type_params when this method was attached to a generic struct template.
    // Preserved so mono can check whether the impl bound is satisfied for the
    // struct's concrete type args before instantiating this method.
    // Each entry: bound on struct's type-param at position `index` within the
    // struct's type_params.  Empty when no impl-level type params.
    std::vector<TypeParam>        impl_type_params;
    // CP-cm-16 follow-up: full impl-target type pattern (with TypeVars
    // unsubstituted) for impl-block-derived methods on **enum** receivers.
    // E.g. for `impl<T,E> FromIterator<Result<T,E>> for Result<Vec<T>, E>`
    // this carries `Result<Vec<T>, E>`. Mono's `instantiate_enum_templates`
    // unifies this pattern against the concrete receiver to bind impl-level
    // T,E correctly when the impl is partially specialised (e.g. T appears
    // inside a Vec<…> in the target). Null for non-impl fns + for impls
    // whose target pattern is a bare enum (positional binding suffices).
    TypeRef                       impl_target_pattern = nullptr;
    // §8.5: per-method where-bounds whose SUBJECT is an arbitrary type
    // EXPRESSION (not just a bare type-param), expressed in the impl's
    // generic terms. E.g. `fn max() where Item: Ord` on
    // `impl<T> Iterator<&T> for VecIter<T>` records `{&T, "Ord"}`. Mono
    // substitutes the subject with the clone's concrete args and checks
    // satisfaction — gating default-method synthesis when sema can't
    // (the trait-arg still mentions a TypeVar). Generalises the bare-
    // type-param `impl_type_params[].bounds` gate to compound subjects
    // like `&T` / `[T;0]` / `EnumPair<T>`.
    std::vector<std::pair<TypeRef, std::string>> where_type_bounds;
    // Outer doc-comment (`/// ...`) lines joined with '\n', leading `/// ` (or
    // `///`) stripped from each. Empty when the fn has no doc-comment.
    std::string                   doc;
    // Test harness attributes. is_test = `#[test]`; should_panic / ignored
    // are modifiers (only meaningful when is_test is true, validated in sema).
    // should_panic_expected_msg = optional `expected = "..."` arg on
    // `#[should_panic]`; empty means any panic accepted (matches Rust).
    bool                          is_test       = false;
    bool                          should_panic  = false;
    bool                          ignored       = false;
    std::string                   should_panic_expected_msg;
};

struct LField {
    std::string      name;
    TypeRef type;
    bool             is_variadic = false;
    std::string      doc;     // Phase A.2: outer `///` doc-comment
};

// User annotation (Java-like). Values are literal-only (variant a of the plan):
// string, int, float, bool, enum variant, or homogeneous-literal array.
struct LAnnotationValue {
    enum class Kind { Int, Float, Bool, Str, Enum, Array };
    Kind                          kind = Kind::Int;
    int64_t                       i = 0;                 // Int, Bool (0/1)
    double                        f = 0.0;               // Float
    std::string                   s;                     // Str, Enum.enum_name, Enum.variant concat
    std::string                   enum_name;             // Enum
    std::string                   enum_variant;          // Enum
    std::vector<LAnnotationValue> arr;                   // Array
};

struct LAnnotationInstance {
    std::string                                  ann_name;     // annotation datatype name (e.g. "Deprecated")
    std::string                                  ann_pkg;      // package that declared it
    // Resolved fully-qualified name (pkg::Name); used for equality in has_annotation.
    std::string                                  ann_fqn;
    // Ordered list of (field_name, value) pairs as specified at the use site.
    // Positional args get resolved to field names during sema.
    std::vector<std::pair<std::string, LAnnotationValue>> kv;
};

struct LStructDef {
    std::string              name;
    std::string              pkg;            // package that declares this struct/datatype
    std::vector<TypeParam>   type_params;    // empty for non-generic structs
    std::vector<std::string> lifetime_params; // e.g. ["'a", "'z"]; erased at codegen
    // B65: outlives bounds on the struct's lifetime params, e.g.
    // `struct Foo<'a, 'b: 'a>` → [("'b", "'a")].
    std::vector<std::pair<std::string, std::string>> lifetime_outlives;
    std::vector<LField>       fields;
    std::vector<LFunctionPtr> methods;
    bool                     is_pub        = false;
    bool                     is_zoned   = false;  // Hermes datatype (C POD layout)
    uint64_t                 type_code     = 0;      // explicit #[type_code=N]; 0 = auto-assign
    bool                     is_data_plain = true;   // no relative-ptr fields → value-copyable
    bool                     from_binary_module = false;  // loaded from binary archive
    // Phase 1B-14: custom DST — the last field has unsized type ([T] / dyn).
    // The struct itself is unsized; `&Self` / `*const Self` etc. are fat
    // pointers `{*const u8, i64 tail_len}` (same shape as Slice fat-ptr).
    bool                     is_dst        = false;
    // Hermes2 / RefRepr: `#[self_describing]` — this DST struct recovers its
    // tail length from an in-band prefix field (e.g. Segment's `cap`), so a
    // `*const Self` / `*mut Self` raw pointer stays THIN (8B, kind=Ptr) rather
    // than fattening to a 16B DstRef. `&Self` / `&mut Self` / `Box<Self>` keep
    // the fat DstRef repr (they don't carry the in-band length contract). The
    // marker dissolves the self-referential-DST-pointer bug without regressing
    // Rc/Arc<dyn> (those use the fat DstRef path). Consulted at the Ptr→DstRef
    // canonicalisation in mono_subst + sema resolve_type.
    bool                     self_describing = false;
    // `#[rel_ptr]`: self-relative pointer type — RefRepr RelOffset (8B i64 byte
    // offset storage, absolute thin ptr compute; materialize = slot + load(slot)).
    bool                     rel_ptr = false;
    // `#[borrow_carrying]`: a value type whose value may hold a Ref into an arena
    // (e.g. HAny) — the borrow checker escape-tracks such values like references.
    bool                     borrow_carrying = false;
    // `#[zone_mut]`: a `&mut T` to this type is a FAT ref {data, zone=*mut Allocator}
    // carrying its Hermes zone, so grow methods reach the allocator from &mut self;
    // read `&T` stays thin. (hermes2-zone-mut-fat-ref)
    bool                     zone_mut = false;
    // `#[zoned2]`: every thin pointer field is stored SELF-RELATIVE (RelOffset i64,
    // anchored to the field's own slot) and materialises to an absolute pointer in
    // compute — the untagged zoned-reference case (ref-repr-design §6). Non-movable.
    bool                     zoned2 = false;
    // logos-core §6.1: this type was declared as `union NAME { … }`.
    // Layout is max-of-fields aligned to max-alignment (vs struct's
    // sum-of-fields); only one field is "active" at a time. Field-
    // reads require enclosing `unsafe` (Rust soundness contract);
    // struct-lit construction initializes exactly one field.
    bool                     is_union      = false;
    // logos-core 1.5: `#[repr(transparent)]` — single-field wrapper inherits
    // its field's layout EXACTLY (size + align). Recognised by sema_collect
    // (`SemaStructInfo::repr_transparent`); consumed by mlir-gen's `layout_of`
    // Struct case to return the field's layout directly, bypassing the
    // aggregate-with-padding path. Required for sound `NonZeroI64`-style
    // wrapper-type ABI identity at FFI boundaries.
    bool                     repr_transparent = false;
    // SHA-256 of canonical type string, truncated to 23 bytes; all-zero = not yet computed
    // (zero for generic templates — hashed at instantiation time in mono_pass).
    std::array<uint8_t, 23>  type_hash     = {};

    // User-annotation metadata.
    bool                             is_annotation_type = false;  // true → this datatype is itself a `#[annotation]` marker type
    std::vector<LAnnotationInstance> annotations;                  // user-annotations attached to this type

    // Specialisation support (mirrors LFunction).
    bool                          is_specialization = false;
    std::vector<TypeRef> spec_patterns;
    // Outer doc-comment (`/// ...`) on the struct/datatype declaration.
    std::string                   doc;
};


struct LVariant {
    std::string name;
    int64_t     disc;
    std::vector<TypeRef> payload_types;  // empty = no payload (C-style)
    bool        is_variadic = false;              // variadic pack payload (...T)
    std::string doc;     // Phase A.2: outer `///` doc-comment
};

struct LEnumDef {
    std::string              name;
    std::string              pkg;            // package that declares this enum
    std::vector<TypeParam>   type_params;   // empty for non-generic enums
    std::vector<std::string> lifetime_params;
    // B65: outlives bounds on the enum's lifetime params.
    std::vector<std::pair<std::string, std::string>> lifetime_outlives;
    std::vector<LVariant>    variants;
    TypeRef         backing_type = nullptr;  // null = default (i32)
    bool has_payload() const {
        for (auto& v : variants)
            if (!v.payload_types.empty()) return true;
        return false;
    }
    // Outer doc-comment on the enum declaration.
    std::string doc;
};

// ── Trait definition ──────────────────────────────────────────────────────

struct LTraitMethodSig {
    std::string              name;
    std::vector<LParam>      params;
    TypeRef         ret_type = nullptr;
    std::string              doc;     // Phase A.2: outer `///` doc-comment
};

struct LAssocTypeDef {
    std::string              name;    // e.g. "Item"
    std::vector<TraitBound>  bounds;  // e.g. [Ord, Clone]
    std::string              doc;     // Phase A.3: outer `///` doc-comment
};

struct LTraitDef {
    std::string                    name;
    std::string                    pkg;                 // package that declares this trait/genos
    std::vector<LAssocTypeDef>     assoc_types;        // associated type declarations
    std::vector<LTraitMethodSig>   methods;
    std::vector<std::string>       type_params;         // empty for non-generic traits
    std::string                    tag_dispatch_system; // #[tag_dispatch(system_name)]; empty = none
    uint64_t                       type_code = 0;       // #[type_code=N] — Hermes-tagged trait identity;
                                                        // propagates to each eidos via `impl Trait for Eidos`
    bool                           is_auto   = false;   // declared with `auto trait` (compiler-synthesized impls)
    // Outer doc-comment on the trait declaration.
    std::string                    doc;

    // ── Supertrait / trait-object dispatch + upcasting metadata ──────────
    // Single-sourced in sema (compute_supertrait_vtable_layout) and read by
    // mlir-gen, so the vtable slot ordering cannot drift between phases.
    //
    // `supertraits` — direct supertrait names (`trait Dog: Animal + Pet` →
    //   ["Animal","Pet"]); informational.
    // `vtable_method_order` — the FLATTENED transitive method-slot order for
    //   `dyn <this trait>`: each supertrait's methods (deepest-first, deduped)
    //   followed by this trait's own methods. A method's index here is its
    //   vtable slot (the `+3` drop/size/align header is added at codegen). This
    //   makes a supertrait method callable through `&dyn Sub` (it has a real
    //   slot) and lets sema and mlir-gen agree on every slot.
    // `upcast_supertraits` — ordered transitive supertraits (deepest-first,
    //   deduped, EXCLUDING this trait). One stored super-vtable pointer slot is
    //   emitted per entry, AFTER the method slots; an upcast `&dyn Sub → &dyn
    //   Super` loads the slot at index_of(Super) to recover Super's vtable
    //   (Rust trait-upcasting; handles multiple supertraits / diamonds).
    std::vector<std::string>       supertraits;
    std::vector<std::pair<std::string,std::string>> vtable_method_order; // (owner_trait, method)
    std::vector<std::string>       upcast_supertraits;
};

struct LImplBlock {
    std::string              trait_name;
    std::string               target_type;  // concrete type name (e.g. "Point")
    std::vector<LFunctionPtr> methods;
    // Associated type definitions: "Item" → i32
    StrMap<TypeRef> assoc_types;
    bool                     is_unsafe = false;  // declared as `unsafe impl`
    bool                     is_negative = false; // `impl !Trait for X {}`

    // Blanket impl support: `impl<T: Bound> Trait for T` — target_type is a
    // type-parameter name; applies to every concrete type implementing Bound.
    bool        is_blanket   = false;
    std::string bound_trait;    // primary bound (first); used for mangling.
    std::vector<std::string> extra_bounds;  // additional bounds beyond bounds[0]
                                            // for `impl<T: A + B + C> Trait for T`.
                                            // mono uses these to filter concrete
                                            // impls that don't satisfy every bound.
    // ADR 0008: associated-type equality clauses on the primary/extra bounds.
    // Parallel to bound_trait/extra_bounds.
    std::vector<std::pair<std::string, TypeRef>> primary_assoc_eqs;
    std::vector<std::pair<std::string,
        std::vector<std::pair<std::string, TypeRef>>>> extra_assoc_eqs;

    // For generic-target impls: full target pattern (`Mutex<T>`) and impl-level
    // type params with their bounds.  Empty for non-generic impls and blanket
    // impls (those use bound_trait/extra_bounds above).
    TypeRef                target_typeref = nullptr;
    std::vector<TypeParam> impl_type_params;
    // B62: trait-position args from `impl Trait<&'a U> for X` plus the
    // impl's own lifetime params (`'a` in `impl<'a, T>`). Used by
    // method_bound_ok to detect HRTB satisfaction mismatch (impl provides
    // concrete region where bound demands universal).
    std::vector<TypeRef>          trait_type_args;
    std::vector<std::string>      trait_lifetime_args;
    std::vector<std::string>      impl_lifetime_params;
    // B65: outlives bounds from `impl<'a, 'b: 'a> ...`.
    std::vector<std::pair<std::string, std::string>> lifetime_outlives;
    // Outer doc-comment on the impl block.
    std::string doc;
};

struct LConst {
    std::string      name;
    TypeRef type;
    LExprPtr         value = nullptr;
    // Outer doc-comment on the const declaration.
    std::string      doc;
};

struct LTypeAlias {
    std::string      name;
    TypeRef type;
    // Outer doc-comment on the type alias.
    std::string      doc;
};

// ── Program ───────────────────────────────────────────────────────────────
//
// Owns all LogosType* objects via type_pool.  All other pointers in this
// program graph point into type_pool or into the string arena — do not
// outlive LProgram.

// Metadata bound to a generic instantiation via an explicit instantiation declaration.
// E.g.: #[type_code=42] datatype Array<i32>;
struct LInstAnnotation {
    std::string       canonical_name;  // fully-qualified canonical type string, e.g. "pkg::Array<i32>"
    std::string       mangled_name;    // concrete struct name after monomorphization, e.g. "Array$G1$i32"
    uint64_t          type_code = 0;   // 0 = not specified
    // Pointer to the concrete LogosType for this instantiation (owned by type_pool).
    // Non-null when the annotation was created from a #[type_code=N] eidos Foo<T>;
    // declaration that had a resolved target type available at sema time.
    // Used by mono to demand struct instantiation even when no Logos code references
    // Foo<T> directly (e.g. blob literals like @<I32>[...] produce type_code-tagged
    // objects at C++ level without instantiating the Logos struct).
    TypeRef  struct_type = nullptr;
    // True when this annotation came from `instantiate Foo<T>;` (or pub form) —
    // i.e. it's a *root pin* requesting all methods of the instance, not just a
    // type_code binding. In the current eager mono scheme this is redundant
    // (every demanded struct gets full method clone anyway); under L1's lazy
    // collector it will additionally pin every inherent + trait method as a
    // worklist root, giving the C++ `template class Foo<int>;` semantics.
    bool     is_root_pin = false;
    // True when declared with `pub instantiate ...`. Lib-site re-export marker
    // for downstream packages once separate codegen lands; semantically a no-op
    // until then.
    bool     is_pub_reexport = false;
};

// One entry in a tag-based dispatch table.
// Emitted by sema when `impl Trait for SomeDatatype` is lowered and the Trait
// carries a `#[tag_dispatch(TagSystemName)]` annotation.
//
// Tier-1 (type_code 128-222): contributes to a static [223 x ptr] array
//   global named `__logos_tag_dispatch_<tag_system>_<trait>_<method>`.
// Tier-2 (type_code 223+): contributes to a cuckoo hash (future work);
//   only a collision-detection symbol is emitted for now.
struct LDispatchEntry {
    std::string tag_system;       // e.g. "DataTypeTagSystem"
    std::string trait_name;       // e.g. "Stringify"
    std::string method_name;      // e.g. "stringify"  (unmangled trait method name)
    std::string fn_symbol;        // mangled impl fn, e.g. "Point__stringify"
    std::string impl_type_name;   // e.g. "Point"  (for diagnostics / collision symbol)
    uint64_t    type_code = 0;    // the datatype's type_code
};

// TypeInfo rodata global for reflect::<T>() intrinsic.
// symbol = "__logos_reflect__<type_hash_hex>", blob = packed Hermes doc (with 8-byte size prefix).
struct LReflectGlobal {
    std::string           symbol;
    std::vector<uint8_t>  blob;   // [u64 size_le][hermes_doc bytes...]
};

} // namespace logos::compiler::lir
namespace logos::compiler { struct LirMirrorTable; }
namespace logos::compiler::lir {

struct LProgram {
    SemaResult             diags;

    TypePool               type_pool;  // owns all LogosType*

    // ADR 0007 slice 1b: pool that owns every LExpr in this program. Builder
    // and mono allocate through `alloc_expr(prog)`; variant fields hold raw
    // LExpr* into this pool. Pool is append-only and survives until LProgram
    // destruction, so handles never dangle.
    //
    // M5 step 4: wrapped in shared_ptr so multiple LPrograms can share the
    // SAME underlying vector. The SemaCache uses this to keep cached
    // LExpr* alive across sema_lower invocations even after mono moves
    // the LProgram into its out_ and discards it. Vector reallocation
    // during push_back doesn't invalidate `LExpr*` (heap objects don't
    // move when the vector resizes — only the unique_ptr slots do).
    std::shared_ptr<std::vector<std::unique_ptr<LExpr>>>      expr_pool_;

    // ADR 0007 slice 1c: matching pools for LBlock / HermesVal / EClosure.
    // Same semantics as expr_pool_ — append-only, lifetime = LProgram. Mono
    // moves these alongside expr_pool_ to keep raw handles valid.
    std::shared_ptr<std::vector<std::unique_ptr<LBlock>>>     block_pool_;
    std::shared_ptr<std::vector<std::unique_ptr<HermesVal>>>  hermes_val_pool_;
    std::shared_ptr<std::vector<std::unique_ptr<EClosure>>>   closure_pool_;

    // Phase 3b: Hermes mirror back-references. Populated by lir_mirror_emit.
    // Held by unique_ptr to keep lir.hpp free of <unordered_map> for the
    // millions of TUs that include it just for the variant tree.
    std::unique_ptr<::logos::compiler::LirMirrorTable> mirror_table;

    std::vector<LStructDef>      structs;
    std::vector<LStructDef>      struct_specializations;  // struct specs (consumed by mono)
    std::vector<LEnumDef>        enums;
    std::vector<LFunctionPtr>    functions;        // free functions and extern fn
    std::vector<LFunctionPtr>    specializations;  // fn specialisations (consumed by mono)
    std::vector<LConst>          consts;
    std::vector<LTypeAlias>      type_aliases;
    std::vector<LTraitDef>       traits;
    std::vector<LImplBlock>      impls;
    std::vector<LInstAnnotation> inst_annotations; // explicit instantiation declarations
    std::vector<LDispatchEntry>  dispatch_entries; // tag-dispatch table entries

    // Phase A.3: inner doc-comments (`//!`) collected from all source modules.
    // Each entry: { source_file, joined_doc_text }. Append-only; downstream
    // rustdoc-style tooling consumes this directly.
    std::vector<std::pair<std::string, std::string>> module_inner_docs;

    // Populated by sema when reflect::<T>() is lowered; consumed by reflection_emit pass.
    StrSet reflect_requests; // fqn of types to reflect

    // Populated by reflection_emit pass; consumed by mlir_gen.
    std::vector<LReflectGlobal> reflection_globals;

    // Phase 7 slice 12: trigger-name → handler-fn-name. Hook fns annotated
    // `#[metaprog_handler("trigger")]` register here; user items carrying
    // `#[trigger]` cause the handler to fire once per item with the item's
    // node pointer as argument. Vector (not map) so duplicate-trigger
    // collisions surface as sema errors with deterministic ordering.
    struct MetaprogHandler {
        std::string trigger;     // user-facing name, e.g. "derive_debug"
        std::string hook_fn;     // fn name to JIT-invoke
    };
    std::vector<MetaprogHandler> metaprog_handlers;

    // Phase 7 slice 12: per-iter list of (ast_idx, item_offset, trigger_name)
    // for top-level items in the user sources whose annotations match a
    // registered handler trigger. The driver computes the absolute target
    // pointer at invoke time as `asts[ast_idx].holder()->base() + offset`
    // (offsets are stable; base may move on arena growth between iters).
    struct MetaprogTarget {
        size_t      ast_idx;
        uint32_t    item_offset;  // offset of the TinyObjectMap node in the holder
        std::string trigger;
    };
    std::vector<MetaprogTarget> metaprog_targets;

    // hstatic-as-const-generic: registry of HermesStatic literals encountered
    // at type-arg position (`Foo::<@{...}>`), keyed by content-hash. Sema
    // populates at LIT_HSTATIC resolution time; mono looks up by the
    // HStaticLit's const_val (the same hash) to materialise the literal in
    // place of `__const_param:CFG` references inside generic bodies.
    // LExpr* points into expr_pool_; lifetimes match LProgram.
    std::unordered_map<uint64_t, LExpr*> hstatic_registry_;

    // M.1: per-site record of `metacall <call_expr>` occurrences in the user
    // entry-file AST. Sema synthesises a no-arg thunk fn (`__metacall_thunk_<idx>`)
    // that wraps the literal-folded call; the driver looks up the thunk in the
    // metaprog JIT, invokes it, and splices the resulting literal back into the
    // AST node at `expr_offset` (overwriting CODE+VALUE in place via TOM::put).
    struct MetacallSite {
        size_t      ast_idx;       // index into asts[] (== entry_ast_idx for now)
        uint32_t    expr_offset;   // arena offset of the METACALL TOM node
        std::string thunk_name;    // mangled name of the synthesised thunk fn
        // Stage 2: synthesised thunk source. Driver feeds it through
        // logos_emit_source so the metaprog JIT compiles a no-arg fn that
        // returns the const-folded callee result. Empty if thunk synthesis
        // failed (e.g. unsupported call shape) — driver skips such sites.
        std::string thunk_source;
        // Return-type discriminator for the driver (avoids re-deriving from L-IR).
        enum class RetTag { Bool, I8, I16, I24, I32, I56, I64, U8, U16, U24, U32, U56, U64, F32, F64, Str, HermesStatic, Hermes, ExprBlob, ItemBlob };
        RetTag      ret_tag = RetTag::I64;
        // MC1.2: simple-name of the free-fn callee (no turbofish, no `Type::`
        // prefix). Driver passes these as `metaprog_keep_fns` on re-sema so
        // the callee body isn't stubbed out in metaprog_mode.
        std::string callee_name;
    };
    std::vector<MetacallSite> metacall_sites;

    // Function-style macro arg blobs (slice 1.3b of fn-macros).
    //
    // Keyed by site_id (== index into metacall_sites). Each entry is a
    // vector of per-arg blobs; each blob has the same `[u64 size][bytes]`
    // ABI as HermesStatic so the JIT thunk can construct ExprBlob from
    // the bare pointer returned by the `logos_macro_arg` host shim.
    //
    // Populated by sema's `lower_fn_macro_call`; consumed by the driver
    // (main.cpp) immediately before each macro thunk is invoked: the
    // driver swaps the active map into a process-global registry that
    // `logos_macro_arg` reads, then clears the registry after invoke.
    //
    // Non-fn-macro sites keep their slot empty.
    std::unordered_map<uint64_t, std::vector<std::vector<uint8_t>>>
        macro_arg_blobs;

    // Symbol names present in binary archives on the search path.
    // mlir_gen skips functions whose mangled name is in this set (they're
    // already compiled and will be found by the linker in the .a).
    StrSet binary_symbols;

    bool ok()                         const noexcept { return diags.ok(); }
    void print_diags(std::FILE* fp = stderr) const noexcept { diags.print(fp); }

    LProgram();
    ~LProgram();
    LProgram(LProgram&&) noexcept;
    LProgram& operator=(LProgram&&) noexcept;
    LProgram(const LProgram&)            = delete;
    LProgram& operator=(const LProgram&) = delete;
};

// ADR 0007 slice 1b: allocator for LExpr nodes in the program pool.
// Returns a non-owning raw pointer; the unique_ptr in expr_pool_ keeps
// the node alive for the lifetime of the LProgram.
//
// M5 step 4: pools are shared_ptr<vector<unique_ptr<X>>>. Constructor
// initializes them; if ever passed a default-constructed LProgram (e.g.
// from {} init), lazy-init here to avoid null-deref. Sharing across
// LPrograms happens via shared_ptr copy at mono's out_ assignment.
inline LExpr* alloc_expr(LProgram& prog) {
    if (!prog.expr_pool_) prog.expr_pool_ = std::make_shared<std::vector<std::unique_ptr<LExpr>>>();
    prog.expr_pool_->push_back(std::make_unique<LExpr>());
    return prog.expr_pool_->back().get();
}

// Slice 1c allocators. Forward Args... so callers can `alloc_block(prog, std::move(inner))`
// or `alloc_block(prog)` for default-construction.
template <class... Args>
inline LBlock* alloc_block(LProgram& prog, Args&&... args) {
    if (!prog.block_pool_) prog.block_pool_ = std::make_shared<std::vector<std::unique_ptr<LBlock>>>();
    prog.block_pool_->push_back(std::make_unique<LBlock>(std::forward<Args>(args)...));
    return prog.block_pool_->back().get();
}
template <class... Args>
inline HermesVal* alloc_hermes_val(LProgram& prog, Args&&... args) {
    if (!prog.hermes_val_pool_) prog.hermes_val_pool_ = std::make_shared<std::vector<std::unique_ptr<HermesVal>>>();
    prog.hermes_val_pool_->push_back(std::make_unique<HermesVal>(std::forward<Args>(args)...));
    return prog.hermes_val_pool_->back().get();
}
template <class... Args>
inline EClosure* alloc_closure(LProgram& prog, Args&&... args) {
    if (!prog.closure_pool_) prog.closure_pool_ = std::make_shared<std::vector<std::unique_ptr<EClosure>>>();
    prog.closure_pool_->push_back(std::make_unique<EClosure>(std::forward<Args>(args)...));
    return prog.closure_pool_->back().get();
}

} // namespace logos::compiler::lir

namespace logos::compiler {

// Strip `function_symbol_name` mangling layers
// (`pkg$base__f__sig` / `pkg$base__g__sig`) and return the bare base
// name. Used at sites that compare an AST-captured string (always
// bare, e.g. metaprog hook trigger) against an LFunction `name` (may
// be mangled by the time mono / final-strip / dispatch runs).
//
// Pkg prefix vs struct-generic `$G\d+`: strip the first `$` unless it
// introduces the generic marker (`$G` followed by a digit). The pkg
// form is `<pkgname>$<base>` (pkgname may be `main` or a dotted path);
// the struct-generic form is `<Type>$G<n>$<arg1>$...` and must stay
// intact on method symbols (`Foo$G1$i32__get`).
inline std::string_view bare_fn_name(std::string_view nm) noexcept {
    if (auto p = nm.find("__f__"); p != std::string_view::npos) nm = nm.substr(0, p);
    else if (auto p = nm.find("__g__"); p != std::string_view::npos) nm = nm.substr(0, p);
    // Strip free-fn pkg prefix (`pkg$base`, pkg may have inner dots like
    // `logos.lang.cmp$error`). Guards:
    //   - `$Gn` generic-args marker (`Vec$G1$i64`) — not a pkg separator.
    //   - a `$` at position 0, OR immediately preceded by `.` (`fp.$fnptr$2`):
    //     that `$` STARTS a method's type name (`$fnptr$N`/`$tuple$N`/`$slice$…`)
    //     after the method-pkg dot — NOT a free-fn pkg-base separator. Without
    //     this guard the strip ate the leading `$` (G149-6 bug).
    if (auto d = nm.find('$'); d != std::string_view::npos && d > 0 && nm[d - 1] != '.') {
        bool is_generic_marker = (d + 2 < nm.size()
                                  && nm[d + 1] == 'G'
                                  && nm[d + 2] >= '0' && nm[d + 2] <= '9');
        if (!is_generic_marker) nm = nm.substr(d + 1);
    }
    // Strip method pkg prefix (`pkg.Concrete__method`). Pkg may have inner
    // dots; split at LAST dot. The bare side starts with the type name and
    // contains no dots.
    if (auto d = nm.rfind('.'); d != std::string_view::npos)
        nm = nm.substr(d + 1);
    return nm;
}

} // namespace logos::compiler

// B.6 Stage 3.5 step 7e: variant `kind` fields removed from
// LExpr/LStmt/Pattern/HermesVal. Schema codes (lir_schema::expr/stmt/pat) are
// the sole source of truth; payload lives in the Hermes mirror.

#include <logos/compiler/lir_schema.hpp>

// ── Entry point ───────────────────────────────────────────────────────────

#include <logos/hermes/document.hpp>

namespace logos::compiler {

// Phase 7 slice 17: metaprog-compile mode. When `metaprog_mode` is true,
// sema_lower compiles handlers + non-entry-file (stdlib) bodies fully, but
// skips lowering the bodies of non-handler fns in the entry ast. Errors
// inside skipped bodies are dropped. Used to JIT-compile metaprog handlers
// even when surrounding user code references not-yet-emitted impls. The
// final, full sema pass runs with `metaprog_mode = false` after expansion
// has converged.
// M5: opaque pointer to the sema cache shared across multiple sema_lower
// invocations in one compile session (defined in sema.hpp).
class SemaCache;

struct SemaOptions {
    bool metaprog_mode = false;
    size_t entry_ast_idx = static_cast<size_t>(-1);
    // MC1.2: in metaprog_mode, the bodies of non-handler entry-file fns are
    // normally skipped. Names listed here are treated like handlers for the
    // skip decision — their bodies ARE lowered. Driver populates this on the
    // second pass with callees of item-position metacall sites.
    std::vector<std::string> metaprog_keep_fns;
    // Phase 2-4: configuration flags from `--cfg` CLI args. Each entry is
    // either `feature=name` (adds name to the feature set) or a bare
    // `flag` (placeholder for future use). Populated by main.cpp from
    // argv and propagated into SemaChecker.cfg_features_ during sema_lower.
    std::vector<std::string> cfg_flags;
    // M5: optional cache for binary-module AST processing — sema_lower
    // skips re-walking ASTs whose snapshot is already in the cache.
    // Owned by the caller; outlives all sema_lower invocations that
    // share it. nullptr → no caching (fresh sema each call).
    SemaCache* cache = nullptr;
    // M6.1: incremental sema across calls. delta_start_idx > 0 tells
    // SemaChecker to skip collect()+lower for asts[0..delta_start_idx);
    // their symbol-table entries and LIR contributions are expected to be
    // in the cache (which must also have metaprog_delta=true so user
    // content is preserved across calls — see SemaCache::set_metaprog_delta).
    // Used by run_metaprog_dispatch to avoid re-processing ASTs that were
    // already lowered in a prior iter of the dispatch loop.
    size_t delta_start_idx = 0;
    // Skeleton-skip gate: the set of symbol names already compiled into a
    // linked archive's .o (collected via `nm --defined-only` over the
    // search-path / dependency archives). A from_binary fn whose name is in
    // here has its body in that .o, so sema skips lowering it and the linker
    // resolves the symbol — the same membership test mlir_gen's is_binary_skip
    // uses, so sema-skip and codegen-forward-declare stay in lockstep. Generic
    // template names and non-generic methods of generic structs are NOT
    // concrete .o symbols, so they're absent here and keep local bodies
    // (lowered for consumer-side instantiation).
    StrSet binary_symbols;

    // Three-layer split Phase 3.4: dotted package name to inject as an
    // implicit wildcard import for every NON-binary AST in this run that
    // does not carry `#![no_implicit_prelude]`. Empty means "no implicit
    // prelude" (legacy). Sourced from emit_module's manifest.prelude.
    // Files loaded from binary archives skip injection (their producer
    // already applied its own prelude when the archive was built).
    std::string implicit_prelude;
};

// Run semantic analysis and produce L-IR from all parsed module ASTs.
// The ASTs must remain alive for the duration of this call (string_views).
// filenames[i] is the source path for asts[i] — used in diagnostics.
// from_binary: parallel to asts/filenames; true means the file came from a
// binary module archive and its non-generic symbols should not be re-emitted.
// is_lazy: parallel to asts/filenames; true means the file came from a
// `lowering lazy` archive. Lazy fns get LFunction.from_lazy_module=true and
// participate in post-mono reach analysis (mlir-gen skips unreached lazy
// bodies). Default: empty → no lazy modules (back-compat).
lir::LProgram sema_lower(const std::vector<hermes::Hermes>& asts,
                          const std::vector<std::string>& filenames = {},
                          const std::vector<bool>& from_binary = {},
                          SemaOptions opts = {},
                          const std::vector<bool>& is_lazy = {});

// Build TypeInfo rodata blobs for types in reflect_requests and annotated datatypes.
// Populates prog.reflection_globals with LReflectGlobal entries.
lir::LProgram reflection_emit(lir::LProgram prog);

} // namespace logos::compiler
