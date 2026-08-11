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
#include <unordered_set>
#include <format>
#include <string>
#include <string_view>

#include <logos/writ/compat.hpp>

#include <logos/compiler/sema_schema.hpp>
#include <logos/writ/compat.hpp>
#include <logos/writ/compat.hpp>
#include <logos/writ/compat.hpp>
#include <logos/writ/compat.hpp>
#include <logos/writ/compat.hpp>
#include <logos/verification/assert.hpp>

namespace logos::compiler {

namespace lir_view { struct ObjectMapRef; }

// ── Type representation ────────────────────────────────────────────────────

// 2c.6.6.B.6: LogosType is no longer an instantiated struct — it has no
// data and no instances. It survives only as a namespace-class holding the
// Kind enum and the TypeUID nested datatype. All readers use TypeRef
// (a fat pointer over the Writ mirror); all writers use LogosTypeBuilder.
// Target pointer width (bits). Single source of truth for usize/isize size
// and any other pointer-sized lowering. Logos ships 64-bit only today; flip
// this constant to retarget. Lives in this header so both sema and mlir-gen
// can read it directly.
inline constexpr int g_target_pointer_bits = 64;

// The reserved marker for a length that is a variadic pack's arity
// (`[T; sizeof...(P)]`). It was respelled as a bare string literal at five
// write sites and decoded at one; a single constant is what makes the write
// and the read the same thing.
inline constexpr std::string_view ARR_LEN_PACK_PFX = "__sizeof_pack:";

// const-length-overhaul: the marker for a deferred const-length EXPRESSION
// (`[T; N + 1]`) carried in the SAME arr_size_var string as a plain symbolic
// name — so an array's length is always a number or one pending computation,
// never an expression masquerading as a type. The body after the marker is a
// postfix (RPN) token stream: `#<int>` literal, `$<name>` const-param, and the
// operators `+ - * / % << >> & | ^` (binary) / `~` (unary negate). `@` cannot
// start an identifier, so this never collides with a real length name.
inline constexpr std::string_view ARR_LEN_EXPR_PFX = "@e:";

// Evaluate a postfix const-length expression against a name lookup. Returns the
// value, or nullopt if a `$name` leaf is unresolved (still symbolic) or the
// stream is ill-formed (div/mod by zero, stack under/overflow). Shared by sema
// (fold-now, empty lookup) and mono (fold-on-bind, substitution lookup).
template <class Lookup>
inline std::optional<int64_t> eval_len_postfix(std::string_view s, Lookup&& lookup) {
    std::vector<int64_t> st;
    auto pop = [&]() -> std::optional<int64_t> {
        if (st.empty()) return std::nullopt;
        int64_t v = st.back(); st.pop_back(); return v;
    };
    size_t i = 0, n = s.size();
    while (i < n) {
        while (i < n && s[i] == ' ') ++i;
        if (i >= n) break;
        size_t j = i; while (j < n && s[j] != ' ') ++j;
        std::string_view tok = s.substr(i, j - i);
        i = j;
        if (tok.empty()) continue;
        char c0 = tok[0];
        if (c0 == '#') {
            int64_t v = 0; size_t k = 1; bool neg = false;
            if (k < tok.size() && tok[k] == '-') { neg = true; ++k; }
            for (; k < tok.size(); ++k) {
                if (tok[k] < '0' || tok[k] > '9') return std::nullopt;
                v = v * 10 + (tok[k] - '0');
            }
            st.push_back(neg ? -v : v);
        } else if (c0 == '$' || c0 == '%') {
            // `$param` (const-generic param) or `%C.CONST` (assoc-const
            // projection through a type-param). The full token, sigil and all,
            // goes to the resolver, which knows how to reach each.
            auto v = lookup(tok);
            if (!v) return std::nullopt;
            st.push_back(*v);
        } else if (tok == "~") {
            auto a = pop(); if (!a) return std::nullopt;
            st.push_back(-*a);
        } else {
            auto b = pop(); auto a = pop();
            if (!a || !b) return std::nullopt;
            int64_t r;
            if      (tok == "+")  r = *a + *b;
            else if (tok == "-")  r = *a - *b;
            else if (tok == "*")  r = *a * *b;
            else if (tok == "/")  { if (*b == 0) return std::nullopt; r = *a / *b; }
            else if (tok == "%")  { if (*b == 0) return std::nullopt; r = *a % *b; }
            else if (tok == "<<") r = *a << *b;
            else if (tok == ">>") r = *a >> *b;
            else if (tok == "&")  r = *a & *b;
            else if (tok == "|")  r = *a | *b;
            else if (tok == "^")  r = *a ^ *b;
            else return std::nullopt;
            st.push_back(r);
        }
    }
    return st.size() == 1 ? std::optional<int64_t>(st[0]) : std::nullopt;
}

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
        ZonedStruct,                 // Writ datatype (C POD layout, no heap types)
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
        WStaticLit,               // WritStatic literal at type-arg position (Foo::<@{...}>); identity = byte-hash over AST. const_val carries the low 64 bits. Inserted after Generic so existing kinds (Generic = 37) keep their numeric IDs.
        CfgSlotType,              // <type:CFG.SLOT> — type at top-level slot of a WritStatic-typed binding. Carries `type_var_name` = CFG ident, `assoc_type_name` = slot key (reused fields). Resolved by mono_subst when CFG is bound to a concrete WStaticLit.
        Usize,                    // pointer-sized unsigned int (u32 on 32-bit, u64 on 64-bit). Distinct from u32/u64 — explicit `as` to/from fixed-width.
        Isize,                    // pointer-sized signed int. Distinct from i32/i64.
        Char,                     // 4-byte Unicode scalar (Rust-style). Distinct from u32; cast required.
        UnsizedSlice,             // Phase 1B: bare `[T]` — unsized slice type. Cannot appear by value
                                  //          (no locals, no by-value params/returns, no plain fields).
                                  //          Valid only behind `&` / `&mut` / `*const` / `*mut`, or as
                                  //          a `T: ?Sized` substitution. `Ref<UnsizedSlice<T>>` is
                                  //          canonicalised to existing Kind::Slice at resolve time.
                                  //          `elem()` carries the element type T.
        UnsizedDyn,               // Phase 1B-4: bare `dyn Trait` — unsized trait-object type.
                                  //            Mirror of UnsizedSlice but for the dyn dispatch side.
                                  //            Cannot appear by value. `Ref<UnsizedDyn<Trait>>` is
                                  //            canonicalised to existing Kind::TraitObject at
                                  //            resolve time. `trait_name()` + `type_args()` carry
                                  //            the trait identity and parameters.
        DstRef,                   // Phase 1B-14: `&DstStruct` / `*const DstStruct` — fat pointer
                                  //              {data_ptr, i64 tail_len}. Represents a reference
                                  //              to a custom-DST struct (struct with `[T]` last
                                  //              field). ABI mirrors Kind::Slice (fat pair stored
                                  //              in memory, addressed by pointer). `struct_name`
                                  //              / `pkg_name` carry the DST struct identity;
                                  //              `mut_ptr` flags `&mut` vs `&`. Mono substitutes
                                  //              `&Wrap<T>` to DstRef when Wrap is_dst at
                                  //              instantiation time.
        Error,                    // sentinel for ill-typed expressions
        Never,                    // the `!` never type — value of a diverging
                                  // expression (return / break / continue /
                                  // panic / `-> !` call / `loop {}`). A subtype
                                  // of every type: coerces to any expected type,
                                  // and unifying it with T yields T. Never
                                  // materialises a value at codegen (the
                                  // diverging expr emits its own terminator).
                                  // Appended after Error to keep kind IDs stable.
        InferredType,             // the `_` placeholder in type position
                                  // (logos-core 1.3). `let x: Vec<_> = …` /
                                  // `&_ as *const i32`. Compatibility-permissive
                                  // on both sides (unifies with any concrete);
                                  // resolved by surrounding context at use
                                  // sites. Appended last to keep kind IDs
                                  // stable across the schema.
        FnItem                    // logos-core 1.4: a fn-item type per
                                  // instantiation. Distinct from FnPtr —
                                  // `foo::<i32>` and `foo::<u32>` get
                                  // DIFFERENT FnItem TypeUIDs even when the
                                  // FnPtr signature is identical (e.g.
                                  // `fn marker<T>() -> i32`). Carries the
                                  // FnPtr-style closure_params / closure_ret
                                  // (the lowered signature) PLUS the callee
                                  // name (`struct_name` slot) and type_args
                                  // for identity. ZST at runtime; auto-
                                  // coerces to FnPtr at every value-use site
                                  // (call, let-binding, arg, return). Bare
                                  // `foo` in expression position produces
                                  // FnItem; auto-coerce restores the prior
                                  // FnPtr behaviour everywhere downstream.
                                  // Every Kind::FnPtr check in sema/mono/
                                  // mlir-gen also accepts Kind::FnItem so
                                  // the source-site swap stays transparent.
    };
    // logos-core 1.4: every site that asks "is this a fn-pointer-like value?"
    // accepts BOTH the bare-FnPtr and the FnItem-distinct shapes — sema/mono/
    // mlir-gen would otherwise crash when an upstream lower_var_ref produced
    // FnItem but a downstream check expected FnPtr exclusively.
    static constexpr bool is_fn_value_kind(Kind k) noexcept {
        return k == Kind::FnPtr || k == Kind::FnItem;
    }

    // ── Signedness: DECIDED HERE, next to the enum that defines the kinds ──
    // A kind's signedness is a property of the kind, so it is written down
    // exactly once — at the definition — and every lowering that branches on
    // it ASKS. It is never re-derived from a list of kind constants retyped
    // at the use site.
    //
    // Why this is a hard rule and not a style preference: before this
    // existed there were ~26 hand-written `k == U8 || k == U16 || …` lists
    // across mlir-gen, ctfe and mono. Each was an independent chance to omit
    // a kind, and 21 of them omitted `Usize`. The consequence was a SILENT
    // MISCOMPILE — a `usize` above 2^63 compared, divided, remaindered and
    // right-shifted as a NEGATIVE i64, and zero/sign-extended the wrong way
    // at every widening cast. `u8`..`u128` were correct, so no fixture that
    // stayed under 2^63 could see it. Adding a new integer kind must be one
    // edit here, not an archaeology pass over the whole backend.
    static constexpr bool is_unsigned_int_kind(Kind k) noexcept {
        return k == Kind::U8   || k == Kind::U16 || k == Kind::U24 ||
               k == Kind::U32  || k == Kind::U56 || k == Kind::U64 ||
               k == Kind::U128 || k == Kind::Usize;
    }
    static constexpr bool is_signed_int_kind(Kind k) noexcept {
        return k == Kind::I8   || k == Kind::I16 || k == Kind::I24 ||
               k == Kind::I32  || k == Kind::I56 || k == Kind::I64 ||
               k == Kind::I128 || k == Kind::Isize;
    }
    // The machine-level question a lowering actually asks: "does this value's
    // LLVM integer representation carry an unsigned magnitude?" — i.e. use a
    // `u`-predicate to compare it, `zext` to widen it, `uitofp` to convert it.
    // Bool (i1: signed i1 `true` is −1, which inverts `false < true`) and Char
    // (a Unicode scalar, never negative) answer yes without being integer
    // TYPES, so they are named here rather than at each call site.
    static constexpr bool is_unsigned_repr_kind(Kind k) noexcept {
        return is_unsigned_int_kind(k) || k == Kind::Bool || k == Kind::Char;
    }

    // ── Scalar layout: DECIDED HERE, next to the enum that defines the kinds ──
    // {byte size, byte alignment} of every kind that has a layout without
    // consulting a definition — the primitives, the reference-like kinds and
    // the fat pairs. Aggregates (Struct / Enum / Tuple / Array) need a
    // definition to size, so they answer `{0,0}` = "ask the phase that owns
    // the defs"; each phase's accumulator supplies them, but they all take
    // their LEAF answers from here.
    //
    // Same rule as is_unsigned_int_kind above, and the same history: this
    // table existed in THREE independent copies (mlir_gen `layout_of`, sema
    // `sema_abi_byte_size`, mono `mono_abi_size`) and all three wrote the
    // STORE size of the odd widths — i24 = 3, i56 = 7. The backend gives a
    // non-power-of-two integer the alignment of the smallest specified integer
    // type at least as wide (x86-64: i24→i32, i56→i64) and rounds its ALLOC
    // size up to that alignment, so the emitted GEP steps 4 / 8 bytes. A
    // container that indexes with `size_of::<T>()` therefore allocated 7 bytes
    // per `i56` element and wrote at stride 8: `Vec<i56>::push` overran the
    // heap (`realloc(): invalid next size`). The size of a value is one fact;
    // it is written once.
    struct ScalarLayout { uint64_t size; uint64_t align; };

    // The BACKEND's rule for an integer of `bits`, keyed by WIDTH rather than by
    // Kind. `getIntegerAlignment` takes the alignment of the smallest SPECIFIED
    // integer type at least as wide, and the largest one if there is none; the
    // x86-64 layout string specifies i1/i8/i16/i32/i64/i128, so a width lands on
    // the next power-of-two byte count, capped at 16. ALLOC size is the store
    // size rounded up to that alignment — i24 → {4,4}, i56 → {8,8}.
    //
    // Two engines ask this question with two different keys: `scalar_layout`
    // has a Kind, and the walk over emitted LLVM-dialect types (`mlir_abi_size`
    // in mlir_gen_impl.hpp) has only an `mlir::IntegerType`'s width. Both read
    // THIS function, so an integer cannot be sized two ways.
    static constexpr ScalarLayout int_layout(unsigned bits) noexcept {
        uint64_t store = (bits + 7) / 8, a = 1;
        while (a < store && a < 16) a <<= 1;
        return { (store + a - 1) / a * a, a };
    }

    static constexpr ScalarLayout scalar_layout(Kind k) noexcept {
        switch (k) {
        case Kind::Void: case Kind::Never:                return {0, 1};
        // A `dyn` TAIL contributes no bytes of its own but forces the enclosing
        // custom-DST struct to POINTER alignment: `Wrap<dyn Tr>` and the sized
        // `Wrap<A>` whose data it aliases must agree on where the tail starts,
        // and the compiler cannot know A. 8 is the conservative assumption
        // every phase must share — mono had it (by accident, via a
        // `default: 8`), mlir-gen and sema said 1, and the disagreement moved
        // the tail of `Inner<i32, dyn Tr>` from offset 8 to offset 4 depending
        // on which phase was asked. (`[T]` is NOT here: its alignment is
        // align_of(T), which IS knowable, so it needs `elem()`.)
        case Kind::UnsizedDyn:                           return {0, 8};
        case Kind::Bool:                                 return int_layout(1);
        case Kind::I8:   case Kind::U8:                  return int_layout(8);
        case Kind::I16:  case Kind::U16:                 return int_layout(16);
        case Kind::I24:  case Kind::U24:                 return int_layout(24);
        case Kind::I32:  case Kind::U32: case Kind::F32:
        case Kind::IntLit: case Kind::Char:              return int_layout(32);
        case Kind::I56:  case Kind::U56:                 return int_layout(56);
        case Kind::I64:  case Kind::U64: case Kind::F64:
        case Kind::FloatLit:
        case Kind::Ptr:  case Kind::Ref: case Kind::MutRef:
        case Kind::FnPtr: case Kind::FnItem: case Kind::TaggedPtr:
        case Kind::Usize: case Kind::Isize:              return int_layout(64);
        case Kind::I128: case Kind::U128:                return int_layout(128);
        // Fat pairs — two pointers wide, pointer-aligned.
        case Kind::Slice: case Kind::Closure:
        case Kind::TraitObject: case Kind::DstRef:       return {16, 8};
        default:                                         return {0, 0};
        }
    }
    // True when scalar_layout(k) answers; false for the aggregate kinds whose
    // size needs a definition.
    static constexpr bool has_scalar_layout(Kind k) noexcept {
        return scalar_layout(k).align != 0;
    }

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

class TypePoolImpl;  // PIMPL — owns writ::Arena and offset mapping

struct LogosTypeBuilder;  // defined below TypeRef

// ── TypeRef ───────────────────────────────────────────────────────────────
//
// Non-owning view over an interned type living in a TypePool. Carries the
// fat {arena, offset, pool} triple needed to read the Writ mirror.
// Identity is the arena offset: two TypeRefs are equal iff they point at
// the same mirror node.

class TypeRef {
    const writ::Arena*      arena_ = nullptr;
    // Stage B (self-relative handles): the interned type's mirror is addressed by
    // its ABSOLUTE pointer, resolved once at construction (self-relative AnyVal::
    // resolve() — no base threading). arena_ is retained for offset() (the type
    // identity key into TypePoolImpl::uid_of_ / intern_buckets_, and .writ0
    // serialization) and for holder lookup. nullptr = null ref. Within one arena
    // ptr_ equality ≡ offset equality, so type identity is preserved.
    const uint8_t*            ptr_ = nullptr;
    const TypePoolImpl*       pool_  = nullptr;
    // Phase 2.B (multi-arena IR): arena_id of the arena this TypeRef belongs
    // to. INVALID_ARENA_ID = "local arena" (single-arena fast path, current
    // compiler). Non-INVALID = TypeRef resolved from a cross-arena ExternalRef;
    // the target arena is registered with global_arena_pool() at the indicated
    // id. Single-arena code paths leave this default (INVALID) and behave
    // exactly as before. See docs/internals/multi-arena-ir.md §3.1.
    writ::arena_id_t        arena_id_ = writ::INVALID_ARENA_ID;

    static const uint8_t* ptr_from_off(const writ::Arena* a,
                                       writ::arena_offset_t o) noexcept {
        return (a && o != writ::NULL_OFFSET) ? a->head().data() + o.value() : nullptr;
    }
public:
    constexpr TypeRef() noexcept = default;
    constexpr TypeRef(std::nullptr_t) noexcept {}
    // (arena, offset) — resolve against the single-chunk base (the interning/root
    // path: TypePoolImpl::ref(off) and cross-arena r.offset() both feed offsets).
    TypeRef(const writ::Arena* a, writ::arena_offset_t off,
            const TypePoolImpl* p) noexcept
        : arena_(a), ptr_(ptr_from_off(a, off)), pool_(p) {}
    // Cross-arena constructor: explicit arena_id of the (typically remote)
    // arena. Used by ptr_via_mirror's ExternalRef dispatch path.
    TypeRef(const writ::Arena* a, writ::arena_offset_t off,
            const TypePoolImpl* p, writ::arena_id_t aid) noexcept
        : arena_(a), ptr_(ptr_from_off(a, off)), pool_(p), arena_id_(aid) {}
    // AnyVal constructors — self-relative resolve (no base): av.resolve() gives the
    // absolute mirror address directly. Chunk-agnostic (ready for MultiChunk).
    TypeRef(const writ::Arena* a, writ::AnyVal av, const TypePoolImpl* p) noexcept
        : arena_(a), ptr_(av.is_ref() ? av.resolve() : nullptr), pool_(p) {}
    TypeRef(const writ::Arena* a, writ::AnyVal av, const TypePoolImpl* p,
            writ::arena_id_t aid) noexcept
        : arena_(a), ptr_(av.is_ref() ? av.resolve() : nullptr),
          pool_(p), arena_id_(aid) {}

    constexpr explicit operator bool() const noexcept {
        return ptr_ != nullptr;
    }

    writ::arena_offset_t offset() const noexcept {
        auto* b = mirror_base();
        return (ptr_ && b) ? writ::arena_offset_t(static_cast<uint32_t>(ptr_ - b))
                           : writ::NULL_OFFSET;
    }

    friend constexpr bool operator==(TypeRef a, TypeRef b) noexcept {
        return a.ptr_ == b.ptr_;
    }
    friend constexpr bool operator==(TypeRef a, std::nullptr_t) noexcept {
        return a.ptr_ == nullptr;
    }
    friend constexpr bool operator==(std::nullptr_t, TypeRef a) noexcept {
        return a.ptr_ == nullptr;
    }

    uint8_t* mirror_base() const noexcept {
        return arena_ ? const_cast<uint8_t*>(arena_->head().data()) : nullptr;
    }
    const writ::TinyObjectMap* mirror() const noexcept {
        return reinterpret_cast<const writ::TinyObjectMap*>(ptr_);
    }
    // Absolute mirror address — the in-process type-identity key (segments never
    // move, so the pointer is stable and unique). Used to key TypePoolImpl::
    // uid_of_ / intern_buckets_ without any base+offset round-trip (offset-from-
    // first-chunk-base is unsigned and breaks under MultiChunk; the address does
    // not). offset() is reserved for .writ0 serialization (single rigid segment).
    const uint8_t* addr() const noexcept { return ptr_; }
    const writ::Arena* arena() const noexcept { return arena_; }
    const TypePoolImpl* pool() const noexcept { return pool_; }
    // Phase 2.B: arena_id of this TypeRef's arena. INVALID = single-arena
    // (local) fast path; consumers can ignore this field unless they need
    // cross-arena awareness.
    writ::arena_id_t  arena_id() const noexcept { return arena_id_; }
    bool                is_external() const noexcept { return arena_id_.is_valid(); }

    LogosType::Kind kind() const noexcept {
        return LogosType::Kind(
            writ::schema::variant_of(mirror()->schema_type_code()));
    }

    TypeRef pointee()      const noexcept;
    TypeRef elem()         const noexcept;
    TypeRef assoc_base()   const noexcept;
    TypeRef closure_ret()  const noexcept;

    bool mut_ptr() const noexcept {
        auto av = mirror()->get(sema_schema::MUT_PTR.code);
        return av.is_value() && av.as_value<uint8_t>() != 0;
    }
    // F3 (§6/§8): `*zoned T` — a zoned raw pointer (Ref-arm self-relative at-rest,
    // absolute as a value; deref/assign runs the storage↔compute bridge). Carried
    // in Ptr's const_val bit 0 (free for Ptr), so it interns/serializes/equates via
    // the existing const_val plumbing. False for any non-Ptr or a plain `*T`.
    bool zoned_ptr() const noexcept {
        if (kind() != LogosType::Kind::Ptr) return false;
        auto cv = const_val();
        return cv.has_value() && ((uint64_t(*cv) & 1u) != 0);
    }
    // Owning kind of a TraitObject. Same fat-pair {data,vtable} layout and
    // dispatch for ALL kinds (incl. Borrow), but the owning forms are droppable
    // with kind-specific release: Box → free(data); Rc → dec strong, at 0 →
    // free RcInner (uses vtable size/align for RcInner layout); Arc → atomic.
    // Carried in const_val (overloaded for TraitObject only; no schema change),
    // folded into TypeUID + equality so the four forms intern distinctly.
    enum class OwningKind : uint8_t { Borrow = 0, Box = 1, Rc = 2, Arc = 3 };
    OwningKind trait_owning_kind() const noexcept {
        if (kind() != LogosType::Kind::TraitObject) return OwningKind::Borrow;
        auto cv = const_val();
        return cv ? OwningKind(uint8_t(*cv)) : OwningKind::Borrow;
    }
    bool owning_trait_object() const noexcept {
        return trait_owning_kind() != OwningKind::Borrow;
    }
    // logos-core 2.4(c): bit 8 / bit 9 of TraitObject's const_val carry the
    // `+ Send` / `+ Sync` auto-trait bounds (the trait object MUST satisfy
    // these at the unsize-coercion site). Encoded by make_trait_object's
    // optional `req_send`/`req_sync` params; folded into TypeUID + equality
    // (see put_u64 in TypeUID hashing). Borrow-form `&dyn T` with no bound
    // returns false for both.
    bool trait_requires_send() const noexcept {
        // The `+ Send` / `+ Sync` auto-bound rides in const_val bits 8/9 for
        // BOTH the fat `&dyn` form (TraitObject) and the unsized `dyn Trait`
        // form (UnsizedDyn — the payload of `Box<dyn …>` / `Rc<dyn …>`).
        if (kind() != LogosType::Kind::TraitObject &&
            kind() != LogosType::Kind::UnsizedDyn) return false;
        auto cv = const_val();
        return cv && ((uint64_t(*cv) >> 8) & 1u);
    }
    bool trait_requires_sync() const noexcept {
        if (kind() != LogosType::Kind::TraitObject &&
            kind() != LogosType::Kind::UnsizedDyn) return false;
        auto cv = const_val();
        return cv && ((uint64_t(*cv) >> 9) & 1u);
    }
    // Owning kind of a Slice — `Box<[T]>` collapses to an owning fat slice
    // (OwningKind::Box): same {data,len} layout as `&[T]`, but move-only +
    // droppable (frees the buffer). Borrow for a plain `&[T]`.
    OwningKind slice_owning_kind() const noexcept {
        if (kind() != LogosType::Kind::Slice) return OwningKind::Borrow;
        auto cv = const_val();
        return cv ? OwningKind(uint8_t(*cv)) : OwningKind::Borrow;
    }
    bool owning_slice() const noexcept {
        return slice_owning_kind() != OwningKind::Borrow;
    }
    // Owning kind of a custom-DST ref — `Box<Foo>` (Foo a tail-slice struct)
    // collapses to an owning DstRef (OwningKind::Box): same fat {data,len}
    // layout/access as `&Foo`, but move-only + droppable. Borrow for `&Foo`.
    OwningKind dst_owning_kind() const noexcept {
        if (kind() != LogosType::Kind::DstRef) return OwningKind::Borrow;
        auto cv = const_val();
        return cv ? OwningKind(uint8_t(*cv)) : OwningKind::Borrow;
    }
    bool owning_dst() const noexcept {
        return dst_owning_kind() != OwningKind::Borrow;
    }
    uint64_t arr_size() const noexcept {
        auto av = mirror()->get(sema_schema::ARR_SIZE.code);
        if (av.is_null()) return 0;
        return *av.as_ptr<const uint64_t>();
    }

    // String accessors return realloc-safe owning views (refcounted MemHolder).
    // Implementation is out-of-line in sema.cpp because it needs MemHolder*,
    // which is reachable only through TypePoolImpl (PIMPL).
    writ::StringView lifetime()        const noexcept;
    writ::StringView struct_name()     const noexcept;
    writ::StringView enum_name()       const noexcept;
    writ::StringView pkg_name()        const noexcept;
    writ::StringView trait_name()      const noexcept;
    writ::StringView type_var_name()   const noexcept;
    writ::StringView assoc_type_name() const noexcept;
    writ::StringView arr_size_var()    const noexcept;

    std::vector<TypeRef> type_args()      const noexcept;
    std::vector<TypeRef> tuple_elems()    const noexcept;
    std::vector<TypeRef> closure_params() const noexcept;
    std::vector<TypeRef> gat_args()       const noexcept;
    std::vector<std::string> lifetime_args() const noexcept;

    std::optional<int64_t> const_val() const noexcept {
        auto av = mirror()->get(sema_schema::CONST_VAL.code);
        if (av.is_null()) return std::nullopt;
        return *av.as_ptr<const int64_t>();
    }

    LogosTypeBuilder to_builder() const;
};

// Resolving a symbolic array length against a substitution was implemented
// TWICE — once in sema (subst_type_sema) and once in mono (subst_type) — and
// the two had drifted: mono decoded the pack prefix and consulted the pack
// table, sema relied on the caller having inserted the PREFIXED name as a key
// in the substitution map. Both "worked", by different rules. One
// implementation, parameterized over the two lookups, is what keeps them from
// drifting again.
//
//   lookup(name)     -> TypeRef  (null when the name is not bound)
//   pack_size(name)  -> a pair<bool,uint64_t>: {found, arity}
struct ArrLenSubst {
    uint64_t    size;
    std::string symbolic;   // empty ⇒ fully resolved
};

// Build the eval_len_postfix leaf resolver from two lookups:
//   nl(name)          → TypeRef bound to a const-param / type-param (or null)
//   al(type, const)   → i64 value of `type::const` assoc-const (or nullopt)
// Handles `$param` (read nl's bound value) and `%C.CONST` (resolve C via nl to
// a concrete struct/enum, then al on its name). Shared by subst_arr_len and the
// encoded-ConstVar folds (sema + mono), each supplying its own lookups.
template <class NameLookup, class AssocLookup>
inline auto make_len_leaf_resolver(NameLookup nl, AssocLookup al) {
    return [nl, al](std::string_view tok) -> std::optional<int64_t> {
        if (tok.empty()) return std::nullopt;
        std::string_view body = tok.substr(1);
        if (tok[0] == '$') {
            TypeRef t = nl(std::string(body));
            if (t) { if (auto cv = t.const_val()) return *cv; }
            return std::nullopt;
        }
        // `%C.CONST` — resolve the type-param, then its assoc-const value.
        auto dot = body.find('.');
        if (dot == std::string_view::npos) return std::nullopt;
        TypeRef ct = nl(std::string(body.substr(0, dot)));
        if (!ct) return std::nullopt;
        std::string tn;
        auto k = ct.kind();
        if (k == LogosType::Kind::Struct || k == LogosType::Kind::ZonedStruct)
            tn = std::string(ct.struct_name());
        else if (k == LogosType::Kind::Enum)
            tn = std::string(ct.enum_name());
        else return std::nullopt;
        return al(tn, std::string(body.substr(dot + 1)));
    };
}

template <class Lookup, class PackSize, class AssocLookup>
inline ArrLenSubst subst_arr_len(uint64_t size, std::string_view var,
                                 Lookup&& lookup, PackSize&& pack_size,
                                 AssocLookup&& assoc_lookup) {
    if (var.empty()) return {size, std::string()};
    std::string sym(var);
    if (sym.rfind(ARR_LEN_PACK_PFX, 0) == 0) {
        std::string pname = sym.substr(ARR_LEN_PACK_PFX.size());
        auto [found, arity] = pack_size(pname);
        if (found) return {arity, std::string()};
        // Fall through: the pack may instead be bound under the prefixed name
        // in the substitution map, which is how the sema side spells it.
    }
    // const-length-overhaul: a deferred const EXPRESSION (`N + 1`,
    // `C::STREAMS + 1`) rides the SAME symbolic-length string, postfix-encoded
    // under ARR_LEN_EXPR_PFX. It is NOT a type — the array's length is always a
    // number (arr_size) or this pending computation, never an expression-typed
    // node. Evaluate against the name + assoc-const lookups; every leaf
    // resolved ⇒ a concrete size, else keep it symbolic for a later subst.
    if (sym.rfind(ARR_LEN_EXPR_PFX, 0) == 0) {
        std::string_view post = std::string_view(sym).substr(ARR_LEN_EXPR_PFX.size());
        auto v = eval_len_postfix(post, make_len_leaf_resolver(
            [&](const std::string& n) -> TypeRef { return lookup(n); },
            assoc_lookup));
        if (v && *v >= 0) return {static_cast<uint64_t>(*v), std::string()};
        return {size, sym};
    }
    auto bound = lookup(sym);
    if (bound) {
        if (auto cv = bound.const_val()) return {static_cast<uint64_t>(*cv), std::string()};
        if (bound.kind() == LogosType::Kind::ConstVar)
            return {size, std::string(bound.type_var_name())};
    }
    return {size, sym};
}

// ── LogosTypeBuilder ──────────────────────────────────────────────────────
//
// Write-side companion to TypeRef. Builder code populates fields freely and
// hands the result to TypePool::alloc, which writes them into the Writ
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

    // Array. INVARIANT: a length is EITHER concrete OR symbolic, never both —
    // `arr_size_var` non-empty ⇒ `arr_size == 0`. NOT the converse: `[T; 0]`
    // is a legitimate empty array with no symbolic name. A stale name next to
    // a resolved size defeats every "is this bound?" check downstream,
    // including the one at code emission.
    TypeRef     elem;               // non-owning, pool-allocated
    uint64_t    arr_size = 0;
    std::string arr_size_var;       // symbolic length: a const-param name,
                                    // ARR_LEN_PACK_PFX + pack name, or (const-
                                    // length-overhaul) a deferred const
                                    // EXPRESSION postfix-encoded under
                                    // ARR_LEN_EXPR_PFX. All fold to arr_size.

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
    // B-mv-03: the trait IDENTITY this bound denotes — the `traits_` registry
    // key `trait_name` resolves to IN THE SCOPE WHERE THE BOUND WAS WRITTEN
    // (bare for a trait that uniquely owns the bare slot, `pkg::Name` for a
    // same-name trait a B-mv-02 collision pushed under its qualified key).
    // ⚠ WHY IT IS A FIELD AND NOT A CALL AT THE CHECK SITE: bounds are CHECKED
    // at the USE site (SemaChecker::check_type_bounds is reached from
    // sema_expr.cpp call/method paths and from sema.cpp struct/type paths),
    // where `cur_imports_` is the CALLER's import view — canonicalising there
    // would answer differently depending on who calls the generic. Captured at
    // read time by SemaChecker::read_trait_bound_args, the same pattern
    // SemaImplInfo::canonical_trait already uses at collect_impl.
    // Empty ⇒ never captured (a bound built outside read_trait_bound_args);
    // consumers fall back to `trait_name`, i.e. the pre-B-mv-03 behaviour.
    std::string                   canonical_trait;
    // The IMPL-REGISTRY identity: `pkg::Trait`, ALWAYS package-qualified.
    // ⚠ DISTINCT FROM `canonical_trait`, AND THE DIFFERENCE IS A DEFECT CLASS.
    // `canonical_trait` is the traits_ REGISTRY key, which is the BARE name for
    // whichever homonym owns the bare slot — i.e. the same string every OTHER
    // homonym's impl is filed under as its raw bare-text alias. Composing an
    // impls_ key from it made a bound over one trait read the other's impls.
    // This field is what impl-registry keys and mono's fact table are built
    // from; `canonical_trait` stays the registry key because traits_ lookups
    // and the auto-trait probe are keyed by THAT. Empty ⇒ never captured;
    // consumers fall back to `trait_name`.
    std::string                   identity_trait;
    std::vector<TypeRef> type_args;   // e.g. Into<i32> -> [i32]
    // L1: lifetime args at trait-bound position (e.g. `Foo<'a>` → ["a"]).
    // Parsed but not enforced (no region inference); needed so the
    // trait-bound resolver doesn't try to resolve LIFETIME_PARAM
    // nodes as types.
    std::vector<std::string> lifetime_args;
    // B63 limit-1: explicit `for<'a, 'b>` HRTB binder list. Lifetimes in
    // type_args that appear here are universally quantified at bound
    // satisfaction time. Lifetimes NOT here (and not "static") refer to
    // outer-scope fn lifetime params and are not skolems.
    std::vector<std::string> hrtb_binders;
    // Associated-type equality clauses: `Trait<Item = i32>` → [{ "Item", i32 }].
    // Only populated when the bound includes `Name = Type` arguments.
    std::vector<std::pair<std::string, TypeRef>> assoc_eqs;
    // Sprint 5.7: parenthesized Fn-family bound `Fn(args) -> ret`.
    // When set, `fn_params` holds the args and `fn_ret` the return
    // type. Distinct slots from type_args so a bound can carry both
    // (`Fn<…>(args) -> ret` is not Rust, but the storage is uniform).
    std::vector<TypeRef> fn_params;
    TypeRef              fn_ret = nullptr;
    bool                 is_fn_family = false;  // trait is one of Fn / FnMut / FnOnce
    // Phase 1: `?Trait` relaxed-bound marker. Only `?Sized` is accepted by
    // sema; any other relaxed trait is a hard error at bound-collection time.
    // When set, the bound does NOT add a positive bound on the type param —
    // it removes the implicit Sized bound that would otherwise apply.
    bool                 is_relaxed = false;
    // G158-6: a `where &T: Trait` / `where &mut T: Trait` clause records the
    // bound on type-param `T` but flags that the SUBJECT is a reference to T
    // (`&T`/`&mut T`), not `T` itself. The method resolver consults these only
    // for a matching reference receiver; `is_ref_mut` distinguishes `&mut`.
    bool                 on_ref_subject = false;
    bool                 is_ref_mut = false;
};

// ── Type parameter ────────────────────────────────────────────────────────

struct TypeParam {
    std::string              name;          // e.g. "T"
    std::vector<TraitBound>  bounds;        // e.g. [Ord, Clone]
    bool                     is_variadic = false;  // T... variadic pack
    bool                     is_const    = false;  // const N: T
    TypeRef         const_type  = nullptr;
    // Default type argument `<T = i64>` (stored in the TYPE_PARAM's TYPE slot by
    // the grammar). Null when absent. Filled at use sites with fewer args.
    TypeRef         default_type = nullptr;
    // B65: type-outlives bounds — `T: 'a` declares that T's data lives for
    // at least 'a. Stored as a list of lifetime names (with apostrophe).
    // Empty when the type param has no outlives bound.
    std::vector<std::string> lifetime_outlives;
    // Phase 1: every type param has an implicit `Sized` bound. Writing
    // `T: ?Sized` (a relaxed bound) clears this flag, permitting the
    // param to be bound to an unsized type. Currently no per-type
    // sizedness tracking exists, so the flag is recorded but its
    // enforcement is partial — full enforcement lands when standalone
    // unsized types (`str`, `[T]`, `dyn Trait`) gain first-class status.
    bool                     implicit_sized = true;
};

// Structural equality (pointer-to-pointer not checked — use value comparison).
bool types_equal(TypeRef a, TypeRef b) noexcept;

// Human-readable name for error messages.
//
// ⚠ TWO JOBS, AND THEY DIVERGE IN EXACTLY ONE CASE — hence the parameter.
// `type_str` is read as a NAME (`sema_expr`'s `tname + "__" + method` is the
// symbol a primitive/enum method call resolves to, and an enum's methods are
// emitted under its BARE name: `Result__ne`), and it is ALSO read as SOURCE
// (`SemaChecker::render_type_src` returns it verbatim into a synthesised block
// that is then RE-PARSED — that is how `println!` carries its argument types).
// For an enum those two answers are not the same string: the name must stay
// `Result`, the source must say `Result<T, E>`.
//
// Dropping the arguments in the source form is not cosmetic. MEASURED:
// `println!("{}", sizeof::<Option<Arc<i32>>>())` printed **4** — the bare word
// `Option` re-parses to an enum with no arguments, so no instance is found, the
// law is handed "no payload" and answers the i32 discriminant. The same
// expression OUTSIDE `println!` was right (8), which is why nothing noticed.
// `sizeof::<G<i64>>()` printed 4 for 16, `sizeof::<G<i32>>()` 4 for 8.
//
// `source_form` is FALSE by default, so every existing caller — every symbol
// name and every diagnostic — is byte-identical to before. Only the renderer
// asks for the other answer, and the flag threads through the recursion so a
// generic enum nested inside a type argument is rendered too.
std::string type_str(TypeRef t, bool source_form = false);

// Render an entire Writ AST document back as Logos source. Used by
// `logosc --dump-metaprog` to display metafn-generated ASTs without
// needing a populated type pool — type-position renders are syntactic
// (TYPE_REF/GENERIC_INST/etc. walked structurally). Holder owns the
// arena bytes; the call is read-only. Returns rendered source ending
// with a newline.
std::string render_module_source_for_dump(writ::MemHolder* holder,
                                          writ::arena_offset_t root_offset);

// Walk a metafn-emitted AST document and collect "navigable" function
// names — bare fn names plus `Type__method` for impl-block members.
// Used by `--dump-metaprog`'s per-metacall index file so users can
// grep these names in the global post-mono MLIR / post-mlirgen LLVM
// IR snapshots. The names are pre-mangling (sema later prefixes pkg
// or type qualifiers); user-facing grep fans out via substring match.
std::vector<std::string> collect_fn_names_for_dump(writ::MemHolder* holder,
                                                   writ::arena_offset_t root_offset);

// Concrete struct name: plain structs → struct_name; generic insts → "Pair__i32__bool".
// Used by mono and mlir_gen to look up instantiated struct definitions.
std::string concrete_struct_name(TypeRef t);

// Raw variant that takes the struct base name + concrete type args directly.
// Used at a few call sites that would otherwise need to synthesise a stack
// LogosType (which bypasses TypePool's Writ mirror). The args must already
// be concrete — no TypeVar / IntLit.
std::string concrete_struct_name_raw(std::string_view base_name,
                                     const std::vector<TypeRef>& type_args,
                                     std::string_view pkg = {});

// Set/get the current phase's pkg→module_id map for type module-qualification
// (same-pkg-same-name coexistence). Threaded as a thread_local; null disables.
// Two backings coexist: sema installs its working C++ map; mono/mlir install the
// LProgram's heap-free ObjectMapRef (Stage E). type_module_suffix reads whichever
// is active.
void set_type_module_map(const std::unordered_map<std::string, std::string>* m);
const std::unordered_map<std::string, std::string>* get_type_module_map();
void set_type_module_map_ref(const lir_view::ObjectMapRef* m);
const lir_view::ObjectMapRef* get_type_module_map_ref();

// The canonical package suffix folded into a type's mangled identity for a given
// (bare name, owning package). For a UNIQUELY-named type: "$M<module_id>" for a
// non-stdlib module type, "" for stdlib/no-module (legacy). For an AMBIGUOUS name
// (declared in ≥2 packages across the transitive universe — see the
// ambiguous-set): "$M<16-hex FNV-1a64(module_id 0x1f pkg)>", stdlib INCLUDED, so
// two same-named types in different packages get DISTINCT struct-defs/layouts
// (G156-1). Every producer AND consumer of a struct/enum mangled name MUST route
// through this so def==use by construction. Takes the name so ambiguity can gate
// the fold; pass the bare (unmangled) type name.
std::string type_module_suffix(std::string_view name, std::string_view pkg);

// G156-1 — the phase-scoped ambiguous-type-name set. A bare nominal name is
// "ambiguous" iff it is declared in ≥2 DISTINCT packages across the current
// build's full transitive type universe (own + every dependency module's
// exported struct/enum decls). type_module_suffix folds the package for names in
// this set. Threaded as a thread_local (mirrors set_type_module_map); null ⇒ no
// fold (legacy mangle). Built once in sema, carried via LProgram to mono/mlir.
void set_ambiguous_type_names(const std::unordered_set<std::string>* s);
const std::unordered_set<std::string>* get_ambiguous_type_names();

// G156-1 — feed one (bare name, owning pkg) declaration into an ambiguity
// accumulator. `first_pkg` tracks the first package seen per name; the second
// DISTINCT package for a name inserts it into `out`. `$`-bearing names skipped.
// Used identically by sema (template tables) and mono/mlir (prog.structs/enums)
// so the resulting set is byte-identical across phases of one build.
void ambiguous_set_accumulate(std::unordered_map<std::string, std::string>& first_pkg,
                              std::unordered_set<std::string>& out,
                              std::string_view name, std::string_view pkg);


// RAII guard: installs `m` as the active type-module map for the current phase
// (sema run / mono run / mlir generate) and restores the previous on scope exit.
struct TypeModuleScope {
    const std::unordered_map<std::string, std::string>* prev_;
    const lir_view::ObjectMapRef*                        prev_ref_;
    // G156-1: also save/restore the ambiguous-type-name set pointer so a nested
    // phase can't leak its set into the parent. Installed empty here; the phase
    // populates its own set and calls set_ambiguous_type_names() once complete.
    const std::unordered_set<std::string>*               prev_amb_;
    // C++-map backing (sema's working pkg_module_ids_).
    explicit TypeModuleScope(const std::unordered_map<std::string, std::string>* m)
        : prev_(get_type_module_map()), prev_ref_(get_type_module_map_ref()),
          prev_amb_(get_ambiguous_type_names()) {
        set_type_module_map(m);
        set_type_module_map_ref(nullptr);
        set_ambiguous_type_names(nullptr);
    }
    // ObjectMapRef backing (LProgram's heap-free pkg_module_ids; Stage E).
    explicit TypeModuleScope(const lir_view::ObjectMapRef* m)
        : prev_(get_type_module_map()), prev_ref_(get_type_module_map_ref()),
          prev_amb_(get_ambiguous_type_names()) {
        set_type_module_map(nullptr);
        set_type_module_map_ref(m);
        set_ambiguous_type_names(nullptr);
    }
    ~TypeModuleScope() { set_type_module_map(prev_); set_type_module_map_ref(prev_ref_); set_ambiguous_type_names(prev_amb_); }
    TypeModuleScope(const TypeModuleScope&) = delete;
    TypeModuleScope& operator=(const TypeModuleScope&) = delete;
};


// ── TypePool ───────────────────────────────────────────────────────────────
//
// Owns the Writ arena that backs all interned types. Each unique type
// lives as a TinyObjectMap inside that arena; TypeRef is a fat pointer
// into it. The pool is moved into LProgram so the arena stays alive for
// the rest of the compilation pipeline.

class TypePool {
    // M5 Step 1: shared_ptr so multiple TypePools can refer to the same
    // interning state. Used by SemaCache to keep stdlib's type-arena alive
    // across the 5+ sema_lower invocations per compile session — TypeRefs
    // stored in cached SemaStructInfo/etc. need their underlying pool to
    // outlive any single LProgram. Mono's `std::move(in_.type_pool)` still
    // works (shared_ptr is movable); after move, source has empty impl_,
    // dest gets the refcounted handle.
    std::shared_ptr<TypePoolImpl>  impl_;  // lazily created on first alloc()
public:
    TypePool();
    ~TypePool();
    TypePool(TypePool&&) noexcept;
    TypePool& operator=(TypePool&&) noexcept;

    // Non-copyable by default to preserve existing semantics; sharing is
    // explicit via shared_clone() so call sites that want a refcounted
    // copy must opt in.
    TypePool(const TypePool&) = delete;
    TypePool& operator=(const TypePool&) = delete;

    // M5 Step 1: cheap clone via shared_ptr refcount bump. The returned
    // TypePool sees the same interned types as `*this` and stays in sync:
    // alloc() on either side appends to the same arena. Used by SemaCache
    // to share stdlib types across multiple sema_lower invocations without
    // re-allocating them.
    TypePool shared_clone() const noexcept {
        TypePool t;
        t.impl_ = impl_;
        return t;
    }

    TypeRef alloc(LogosTypeBuilder t);

    // Multi-arena IR Phase 5.B step 3: rebuild a foreign TypeRef inside this
    // pool. No-op for local refs (returns input unchanged). For foreign
    // refs, recurses through every child reference and reallocates the
    // whole type tree locally so the returned TypeRef's offset is
    // meaningful when later stored in this pool's arena (e.g. via
    // `AnyVal::from_offset(tr.offset())` in a mirror node's TYPE field).
    //
    // Cheap on the hot path: the foreign-check short-circuits to a single
    // pointer comparison + arena_id_ test for local refs.
    TypeRef intern_foreign(TypeRef tv);

    // Phase 3b: expose the underlying Writ arena. The compiler's L-IR mirror
    // shares this arena with the type mirror so cross-references (TypeRef
    // offsets stored on L-IR nodes, sub-expression offsets, etc.) all live in
    // a single offset space. Returns nullptr if the pool has not yet allocated
    // (no calls to alloc()).
    writ::Arena*       arena() noexcept;
    const writ::Arena* arena() const noexcept;

    // Phase 3b: ensure the pool's arena is initialised (allocates the empty
    // arena if no types have been interned yet). Used by the L-IR mirror
    // emitter when the program contains no LogosType allocations.
    writ::Arena&       arena_or_init();

    // The pool's Writ document, initialising it if needed. THE handle for all
    // object creation (ctr.make_string / make_array / make_tiny_map …) — callers
    // route producer work through this instead of pulling the raw arena.
    writ::WritCtr&   ctr_or_init();

    // Phase 3d: expose the impl pointer so lir_view callers can wrap a raw
    // arena offset into a TypeRef (TypeRef stores pool* for trait/method
    // resolution; nullptr-pool TypeRefs work for kind/name accessors only).
    const TypePoolImpl* impl() const noexcept { return impl_.get(); }

    // Multi-arena IR Phase 3: expose the underlying MemHolder so consumers
    // can wrap the arena as a writ::Writ view for publish-phase work
    // (lir_arena_root_begin etc.). Returns nullptr if the pool hasn't yet
    // allocated (no calls to alloc()).
    writ::MemHolder* holder() noexcept;

    // Component-metaprog slice 1B: public access to per-type 32-byte UID.
    LogosType::TypeUID uid_of(TypeRef t) const noexcept;
};

// ── M5 sema cache ─────────────────────────────────────────────────────────
//
// Persistent cache of per-AST sema state, shared across multiple
// sema_lower invocations in one compile session. Caller (main.cpp's
// compile dispatcher) creates one SemaCache and passes it to each
// sema_lower call via SemaOptions::cache. The cache owns:
//   - A shared TypePool that outlives any single LProgram.
//   - Per-binary-AST snapshots of symbol-table entries (collect output)
//     and LIR items (lower_program output, future steps).
//
// PIMPL: implementation in sema.cpp has visibility into SemaChecker's
// private SemaStructInfo / SemaEnumInfo / etc. types.
class SemaCacheImpl;
class SemaCache {
    std::unique_ptr<SemaCacheImpl> impl_;
public:
    SemaCache();
    ~SemaCache();
    SemaCache(SemaCache&&) noexcept;
    SemaCache& operator=(SemaCache&&) noexcept;
    SemaCache(const SemaCache&) = delete;
    SemaCache& operator=(const SemaCache&) = delete;

    // Returns the shared pool (the one all cached TypeRefs reference).
    // SemaChecker uses this as its working pool when a cache is wired in.
    TypePool& shared_pool() noexcept;

    // M6.1: keep-user-state mode. When true, snapshots preserve user
    // contributions (skip the Step 5c filter) and the LIR bundle captures
    // user items in addition to binary. Required when SemaOptions::
    // delta_start_idx is used across multiple sema_lower calls — the
    // delta call relies on the cache having all prior asts' state.
    // Used by run_metaprog_dispatch to avoid re-processing user ASTs
    // across dispatch-loop iterations.
    void set_keep_user_state(bool v) noexcept;
    bool keep_user_state() const noexcept;

    // M6.1: invalidate user-only state from the cache. Re-applies the
    // Step 5c filter to the existing snapshot, re-filters the bundle to
    // binary-only, drops user holders from collected_holders, and resets
    // keep_user_state to false. Used by run_metaprog_dispatch after the
    // dispatch loop completes so the *next* sema_lower (final user sema)
    // can run in default mode without seeing duplicated user content.
    void reset_user_state();

    // Implementation handle for SemaChecker internals (cross-module access).
    SemaCacheImpl*       impl()       noexcept { return impl_.get(); }
    const SemaCacheImpl* impl() const noexcept { return impl_.get(); }
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

// Diagnostic output format. Set by main from --diag-format=<text|json>.
// Stored as a global because SemaResult::print is called from many sites
// without options threading. JSON form emits NDJSON: one object per line.
enum class DiagFormat { Text, Json };
inline DiagFormat& diag_format_global() noexcept {
    static DiagFormat g = DiagFormat::Text;
    return g;
}

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
        bool as_json = (diag_format_global() == DiagFormat::Json);
        for (auto& d : diags) {
            std::string key = std::format("{}|{}|{}|{}",
                int(d.level), d.file, d.line, d.message);
            if (!g_seen.insert(std::move(key)).second) continue;
            const char* lev = (d.level == Diag::Level::Error) ? "error" : "warning";
            if (as_json) {
                std::fprintf(fp,
                    "{\"level\":\"%s\",\"file\":", lev);
                json_escape_to(fp, d.file);
                std::fprintf(fp, ",\"line\":%u,\"context\":", d.line);
                json_escape_to(fp, d.context);
                std::fprintf(fp, ",\"message\":");
                json_escape_to(fp, d.message);
                std::fprintf(fp, "}\n");
            } else if (d.line > 0 && !d.file.empty())
                std::fprintf(fp, "%s:%u: %s [%s]: %s\n",
                             d.file.c_str(), d.line, lev, d.context.c_str(), d.message.c_str());
            else
                std::fprintf(fp, "%s [%s]: %s\n", lev, d.context.c_str(), d.message.c_str());
        }
    }

    // Minimal JSON string escape — write `"...escaped..."` to fp.
    static void json_escape_to(std::FILE* fp, const std::string& s) noexcept {
        std::fputc('"', fp);
        for (unsigned char c : s) {
            switch (c) {
                case '"':  std::fputs("\\\"", fp); break;
                case '\\': std::fputs("\\\\", fp); break;
                case '\b': std::fputs("\\b", fp); break;
                case '\f': std::fputs("\\f", fp); break;
                case '\n': std::fputs("\\n", fp); break;
                case '\r': std::fputs("\\r", fp); break;
                case '\t': std::fputs("\\t", fp); break;
                default:
                    if (c < 0x20) std::fprintf(fp, "\\u%04x", c);
                    else          std::fputc(c, fp);
            }
        }
        std::fputc('"', fp);
    }
};

} // namespace logos::compiler
