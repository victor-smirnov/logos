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

#include <logos/hermes/compat.hpp>

#include <logos/compiler/sema_schema.hpp>
#include <logos/hermes/compat.hpp>
#include <logos/hermes/compat.hpp>
#include <logos/hermes/compat.hpp>
#include <logos/hermes/compat.hpp>
#include <logos/hermes/compat.hpp>
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
    // Phase 2.B (multi-arena IR): arena_id of the arena this TypeRef belongs
    // to. INVALID_ARENA_ID = "local arena" (single-arena fast path, current
    // compiler). Non-INVALID = TypeRef resolved from a cross-arena ExternalRef;
    // the target arena is registered with global_arena_pool() at the indicated
    // id. Single-arena code paths leave this default (INVALID) and behave
    // exactly as before. See docs/internals/multi-arena-ir.md §3.1.
    hermes::arena_id_t        arena_id_ = hermes::INVALID_ARENA_ID;
public:
    constexpr TypeRef() noexcept = default;
    constexpr TypeRef(std::nullptr_t) noexcept {}
    TypeRef(const hermes::Arena* a, hermes::arena_offset_t off,
            const TypePoolImpl* p) noexcept
        : arena_(a), off_(off), pool_(p) {}
    // Cross-arena constructor: explicit arena_id of the (typically remote)
    // arena. Used by ptr_via_mirror's ExternalRef dispatch path.
    TypeRef(const hermes::Arena* a, hermes::arena_offset_t off,
            const TypePoolImpl* p, hermes::arena_id_t aid) noexcept
        : arena_(a), off_(off), pool_(p), arena_id_(aid) {}
    // AnyVal constructors — compute the within-arena offset from a value-form Ref
    // against `a`'s single-chunk base (the cut-over unifies offset/AnyVal handles).
    TypeRef(const hermes::Arena* a, hermes::AnyVal av, const TypePoolImpl* p) noexcept
        : arena_(a),
          off_(av.is_ref() ? av.to_offset(a->head().data()) : hermes::NULL_OFFSET),
          pool_(p) {}
    TypeRef(const hermes::Arena* a, hermes::AnyVal av, const TypePoolImpl* p,
            hermes::arena_id_t aid) noexcept
        : arena_(a),
          off_(av.is_ref() ? av.to_offset(a->head().data()) : hermes::NULL_OFFSET),
          pool_(p), arena_id_(aid) {}

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
    const hermes::Arena* arena() const noexcept { return arena_; }
    const TypePoolImpl* pool() const noexcept { return pool_; }
    // Phase 2.B: arena_id of this TypeRef's arena. INVALID = single-arena
    // (local) fast path; consumers can ignore this field unless they need
    // cross-arena awareness.
    hermes::arena_id_t  arena_id() const noexcept { return arena_id_; }
    bool                is_external() const noexcept { return arena_id_.is_valid(); }

    LogosType::Kind kind() const noexcept {
        return LogosType::Kind(
            hermes::schema::variant_of(mirror()->schema_type_code()));
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
    hermes::StringView lifetime()        const noexcept;
    hermes::StringView struct_name()     const noexcept;
    hermes::StringView enum_name()       const noexcept;
    hermes::StringView pkg_name()        const noexcept;
    hermes::StringView trait_name()      const noexcept;
    hermes::StringView type_var_name()   const noexcept;
    hermes::StringView assoc_type_name() const noexcept;
    hermes::StringView arr_size_var()    const noexcept;

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
std::string type_str(TypeRef t);

// Render an entire Hermes AST document back as Logos source. Used by
// `logosc --dump-metaprog` to display metafn-generated ASTs without
// needing a populated type pool — type-position renders are syntactic
// (TYPE_REF/GENERIC_INST/etc. walked structurally). Holder owns the
// arena bytes; the call is read-only. Returns rendered source ending
// with a newline.
std::string render_module_source_for_dump(hermes::MemHolder* holder,
                                          hermes::arena_offset_t root_offset);

// Walk a metafn-emitted AST document and collect "navigable" function
// names — bare fn names plus `Type__method` for impl-block members.
// Used by `--dump-metaprog`'s per-metacall index file so users can
// grep these names in the global post-mono MLIR / post-mlirgen LLVM
// IR snapshots. The names are pre-mangling (sema later prefixes pkg
// or type qualifiers); user-facing grep fans out via substring match.
std::vector<std::string> collect_fn_names_for_dump(hermes::MemHolder* holder,
                                                   hermes::arena_offset_t root_offset);

// Concrete struct name: plain structs → struct_name; generic insts → "Pair__i32__bool".
// Used by mono and mlir_gen to look up instantiated struct definitions.
std::string concrete_struct_name(TypeRef t);

// Raw variant that takes the struct base name + concrete type args directly.
// Used at a few call sites that would otherwise need to synthesise a stack
// LogosType (which bypasses TypePool's Hermes mirror). The args must already
// be concrete — no TypeVar / IntLit.
std::string concrete_struct_name_raw(std::string_view base_name,
                                     const std::vector<TypeRef>& type_args,
                                     std::string_view pkg = {});

// Set/get the current phase's pkg→module_id map for type module-qualification
// (same-pkg-same-name coexistence). Threaded as a thread_local; null disables.
void set_type_module_map(const std::unordered_map<std::string, std::string>* m);
const std::unordered_map<std::string, std::string>* get_type_module_map();

// The module suffix appended to a type's mangled name for a given owning
// package ("$M<module_id>", or "" for stdlib/no-module). Public so struct/enum
// registration can mint a matching qualified alias for concrete_struct_name
// lookups.
std::string type_module_suffix(std::string_view pkg);

// RAII guard: installs `m` as the active type-module map for the current phase
// (sema run / mono run / mlir generate) and restores the previous on scope exit.
struct TypeModuleScope {
    const std::unordered_map<std::string, std::string>* prev_;
    explicit TypeModuleScope(const std::unordered_map<std::string, std::string>* m)
        : prev_(get_type_module_map()) { set_type_module_map(m); }
    ~TypeModuleScope() { set_type_module_map(prev_); }
    TypeModuleScope(const TypeModuleScope&) = delete;
    TypeModuleScope& operator=(const TypeModuleScope&) = delete;
};


// ── TypePool ───────────────────────────────────────────────────────────────
//
// Owns the Hermes arena that backs all interned types. Each unique type
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

    // Multi-arena IR Phase 3: expose the underlying MemHolder so consumers
    // can wrap the arena as a hermes::Hermes view for publish-phase work
    // (lir_arena_root_begin etc.). Returns nullptr if the pool hasn't yet
    // allocated (no calls to alloc()).
    hermes::MemHolder* holder() noexcept;

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
