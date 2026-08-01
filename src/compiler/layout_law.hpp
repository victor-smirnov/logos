// Logos project — https://github.com/victor-smirnov/logos
//
// layout_law.hpp — THE aggregate layout law. One copy, asked by every engine.
//
// `LogosType::scalar_layout` / `int_layout` (sema.hpp) answer the LEAF
// question: how many bytes an integer, a float, a pointer, a fat pair occupy.
// That table was unified in `8ba3c764`, and it was necessary and NOT
// sufficient: the divergences that reached running programs were all in the
// COMPOSITION rule — how members accumulate into an aggregate, whether a union
// is a sum or a max, where an enum's payload starts and whether there is a
// discriminant word at all. Those rules were written out FOUR times (mlir-gen
// over TypeRef, mlir-gen over mlir::Type, sema over SemaStructInfo, mono over
// lir_view::StructView) plus four more times as bare offset walks, and two of
// the copies were missing whole branches:
//
//   * sema had no `is_union()` branch, so `union U { b: [u8;12], big: i64 }`
//     got the SUM (24) where its layout is the MAX (16) — and sema's number is
//     the byte offset at which a custom DST's `[T]` tail begins, so every write
//     through the tail landed 8 bytes past the allocation;
//   * sema had no niche branch and read variant payloads UNSUBSTITUTED, so
//     every `Option<T>` came out {16,8} whatever T was: `Option<i32>` (really
//     8) put the tail at 16, `Option<&i64>` (niche-packed, really 8) likewise;
//   * mono had no Enum case at all — every enum fell to the `default: {8,8}`.
//
// So the law lives HERE, as pure functions over {size, align}, and each engine
// supplies only the two things that are genuinely representation-specific: how
// to enumerate an aggregate's members, and what a member's own layout is.
//
// NOTHING in this header may include a DataLayout — not `llvm::DataLayout`, not
// `mlir::DataLayout`. The law is the answer; a second oracle in scope is how a
// fourth engine gets written. `tests/logos/layout_engine_agreement_gate.sh`
// asserts that on the SOURCE, by include rather than by call spelling: a TU
// that cannot see the declaration cannot ask the question under any spelling.

#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include <logos/compiler/sema.hpp>   // LogosType::Kind / scalar_layout / int_layout

namespace logos::compiler::layout {

// {size, align}: `size` is the ALLOC size — the stride the backend steps
// between two adjacent values — never the store size. `align` is never 0.
struct L {
    uint64_t size = 0;
    uint64_t align = 1;
    friend constexpr bool operator==(const L&, const L&) noexcept = default;
};

constexpr uint64_t align_up(uint64_t v, uint64_t a) noexcept {
    return a > 1 ? (v + a - 1) & ~(a - 1) : v;
}

// ── THE aggregate accumulation rule ─────────────────────────────────────────
// Members in declaration order, each at its own natural alignment; the total
// rounded to the aggregate's own alignment. Matches LLVM's non-packed C layout,
// which is what `llvm::StructLayout` — the layout the object file is emitted
// with — computes.
//
// `place()` returns the member's BYTE OFFSET. That is the whole point of it
// being here: the four bare offset walks (a DST prefix in sema, the same in
// mono, `offset_of!`, the DWARF member list) used to re-derive the offsets with
// their own copy of the padding arithmetic, so a prefix walk could disagree
// with the size walk of the very same struct. They now ask this accumulator,
// so an offset and a size are two READS OF ONE WALK.
class Agg {
    uint64_t offset_ = 0;
    uint64_t align_  = 1;
public:
    // Append `f`; returns the offset it lands at.
    constexpr uint64_t place(L f) noexcept {
        offset_ = align_up(offset_, f.align);
        uint64_t at = offset_;
        offset_ += f.size;
        if (f.align > align_) align_ = f.align;
        return at;
    }
    constexpr void push(L f) noexcept { (void)place(f); }
    // Where a member of layout `f` WOULD land, without appending it. This is
    // how a custom DST's `[T]` tail offset is asked: the tail contributes no
    // bytes but does force alignment.
    constexpr uint64_t next_offset(L f) const noexcept {
        return align_up(offset_, f.align);
    }
    constexpr uint64_t align() const noexcept { return align_; }
    constexpr L finish() const noexcept { return { align_up(offset_, align_), align_ }; }
};

// ── THE union rule ──────────────────────────────────────────────────────────
// Rust reference `items.union.common-storage`: every field at offset 0, size =
// max(field sizes) rounded to align = max(field aligns). A union is where the
// accumulation rule inverts — sum becomes max — which is exactly the branch
// sema did not have.
class Uni {
    uint64_t size_  = 0;
    uint64_t align_ = 1;
public:
    constexpr void push(L f) noexcept {
        if (f.size  > size_)  size_  = f.size;
        if (f.align > align_) align_ = f.align;
    }
    constexpr L finish() const noexcept { return { align_up(size_, align_), align_ }; }
};

// ── THE tagged-enum rule ────────────────────────────────────────────────────
// Value repr is `{ i32 disc, <payload blob at its own alignment> }`. The
// payload does NOT start at 4: it starts at `align_up(4, payload_align)`, so an
// 8-aligned payload puts the disc word in its own padded slot.
inline constexpr uint64_t kDiscBytes = 4;
inline constexpr L        kDisc{ kDiscBytes, kDiscBytes };

constexpr uint64_t tagged_enum_payload_offset(uint64_t payload_align) noexcept {
    return align_up(kDiscBytes, payload_align);
}
constexpr L tagged_enum(L payload) noexcept {
    Agg a; a.push(kDisc); a.push(payload); return a.finish();
}
// A niche-packed enum carries its discriminant INSIDE the payload's spare
// values — a null pointer, a low bit — so it has no disc word and is exactly
// its payload. `Option<&T>` is 8 bytes, not 16.
constexpr L niche_enum(L payload) noexcept { return payload; }

// ── THE niche-eligibility rule ──────────────────────────────────────────────
// Whether an enum packs is part of its LAYOUT, so it is part of the law. It is
// stated over a description of the variants rather than over any engine's type
// representation, so sema, mono and mlir-gen classify identically.
enum class ArmKind : uint8_t {
    Other,          // anything that disqualifies
    Ref,            // `&T` / `&mut T` — never null
    RawPtr,         // `*T` — only trusted inside a `#[zoned2]` enum
    NonNullWrapper, // `#[non_null]` single-8-byte-pointer struct (Box/Rc/Arc)
    Int,            // integer / bool arm
};

struct ArmDesc {
    ArmKind  kind          = ArmKind::Other;
    uint64_t pointee_align = 0;      // Ref only — the low-bit niche needs ≥2
    uint32_t int_bits      = 0;      // Int only
    bool     int_signed    = false;  // Int only
};

struct VariantDesc {
    int64_t  disc      = 0;
    unsigned n_payload = 0;   // non-`()` payload fields
    ArmDesc  arm{};           // meaningful when n_payload == 1
};

enum class NicheKind : uint8_t { None, NullPtr, LowBit };

struct Niche {
    NicheKind kind   = NicheKind::None;
    bool      packed = false;
    int64_t   none_disc = 0, some_disc = 0;   // NullPtr
    int64_t   ptr_disc  = 0, val_disc  = 0;   // LowBit
    uint32_t  val_bits  = 0;
    bool      val_signed = false;
    bool      val_raw    = false;             // 64-bit `#[zoned2]` arm: no `<<1`
};

// `zoned` = the enum carries `#[zoned2]` (Writ zone objects: the allocator
// guarantees ≥2 alignment, and a Pod arm bakes the low-bit tag in itself).
inline Niche classify_niche(bool zoned, std::span<const VariantDesc> vs) noexcept {
    Niche n;
    if (vs.size() != 2) return n;

    // (1) Null-pointer niche — `Option<&T>` shape: one fieldless variant and
    //     one single non-null-pointer payload. The disc is null vs non-null at
    //     offset 0, so there is no disc word.
    const VariantDesc* none_v = nullptr;
    const VariantDesc* some_v = nullptr;
    for (auto& v : vs) {
        if (v.n_payload == 0)      none_v = &v;
        else if (v.n_payload == 1) some_v = &v;
    }
    if (none_v && some_v &&
        (some_v->arm.kind == ArmKind::Ref ||
         some_v->arm.kind == ArmKind::NonNullWrapper)) {
        n.kind = NicheKind::NullPtr;
        n.packed = true;
        n.none_disc = none_v->disc;
        n.some_disc = some_v->disc;
        return n;
    }

    // (2) Low-bit niche — two single-field data arms: one pointer to an ≥2
    //     aligned pointee (low bit always 0), one ≤56-bit integer stored as
    //     `(v<<1)|1`. Packs into ONE word; the disc IS the low bit.
    const VariantDesc* ptr_arm = nullptr;
    const VariantDesc* val_arm = nullptr;
    uint32_t vbits = 0; bool vsigned = false;
    for (auto& v : vs) {
        if (v.n_payload != 1) return n;
        const ArmDesc& a = v.arm;
        bool is_ptr = (a.kind == ArmKind::Ref && a.pointee_align >= 2) ||
                      (zoned && (a.kind == ArmKind::Ref || a.kind == ArmKind::RawPtr));
        bool is_val = a.kind == ArmKind::Int &&
                      (a.int_bits <= 56 || (a.int_bits == 64 && zoned));
        if (is_ptr && !ptr_arm)      ptr_arm = &v;
        else if (is_val && !val_arm) { val_arm = &v; vbits = a.int_bits; vsigned = a.int_signed; }
        else return n;
    }
    if (ptr_arm && val_arm) {
        n.kind = NicheKind::LowBit;
        n.packed = true;
        n.ptr_disc = ptr_arm->disc;
        n.val_disc = val_arm->disc;
        n.val_bits = vbits;
        n.val_signed = vsigned;
        n.val_raw = (vbits == 64);
    }
    return n;
}

// Kind → (bit width, signedness) for a low-bit VALUE arm. Kept here rather
// than re-tabulated per engine: whether `u24` can be an arm is part of the
// layout, and three copies of a table is how the branches drifted apart.
// `usize`/`isize` are deliberately absent — they are 64-bit, and a 64-bit arm
// packs only in the `#[zoned2]` raw form, which names its widths explicitly.
inline bool int_arm_of_kind(LogosType::Kind k, uint32_t& bits, bool& sgn) noexcept {
    using K = LogosType::Kind;
    switch (k) {
    case K::Bool: bits = 1;  sgn = false; return true;
    case K::I8:   bits = 8;  sgn = true;  return true;
    case K::U8:   bits = 8;  sgn = false; return true;
    case K::I16:  bits = 16; sgn = true;  return true;
    case K::U16:  bits = 16; sgn = false; return true;
    case K::I24:  bits = 24; sgn = true;  return true;
    case K::U24:  bits = 24; sgn = false; return true;
    case K::I32:  bits = 32; sgn = true;  return true;
    case K::U32:  bits = 32; sgn = false; return true;
    case K::I56:  bits = 56; sgn = true;  return true;
    // 64-bit arms pack ONLY in a `#[zoned2]` raw niche (no shift — the producer
    // bakes the low-bit-1 tag in); `classify_niche` enforces that.
    case K::I64:  bits = 64; sgn = true;  return true;
    case K::U64:  bits = 64; sgn = false; return true;
    default: return false;
    }
}

// Build an `ArmDesc` for the payload types every engine can classify without
// consulting a struct registry. `nonnull_wrapper` is the one question that
// needs one (`#[non_null]` + exactly 8 bytes → Box/Rc/Arc); the caller answers
// it, the law decides what it means.
inline ArmDesc arm_desc_of_kind(LogosType::Kind k, uint64_t pointee_align,
                                bool nonnull_wrapper) noexcept {
    using K = LogosType::Kind;
    ArmDesc a;
    if (k == K::Ref || k == K::MutRef) {
        a.kind = ArmKind::Ref;
        a.pointee_align = pointee_align;
        return a;
    }
    if (k == K::Ptr) { a.kind = ArmKind::RawPtr; return a; }
    if (nonnull_wrapper) { a.kind = ArmKind::NonNullWrapper; return a; }
    uint32_t bits = 0; bool sgn = false;
    if (int_arm_of_kind(k, bits, sgn)) {
        a.kind = ArmKind::Int; a.int_bits = bits; a.int_signed = sgn;
    }
    return a;
}

// ── THE cross-engine ledger ─────────────────────────────────────────────────
// `verify_layout_engines()` can compare the two mlir-gen engines with
// `llvm::DataLayout` because all three are reachable from one translation unit
// at one moment. Sema's and mono's answers are not: they are computed and
// consumed before mlir-gen exists. A comparison that cannot reach an engine
// reports "no disagreements" about an engine it never asked — which is the
// exact shape of the failure this whole arc is about.
//
// So the engines that run EARLY record what they answered, keyed by the type,
// and the verifier — which does have `llvm::DataLayout` — checks the record.
// The ledger is append-only within one compile and lives for the process.
// The ONE name for a type in the ledger: `<pkg>.<concrete name>`. Both sides
// of the check derive it from the same two pieces (mlir-gen's `qualify_pkg`
// forwards here), so a key cannot be spelled two ways and quietly not match.
inline std::string type_key(std::string_view pkg, std::string_view concrete_name) {
    if (pkg.empty()) return std::string(concrete_name);
    std::string r;
    r.reserve(pkg.size() + 1 + concrete_name.size());
    r.append(pkg); r.push_back('.'); r.append(concrete_name);
    return r;
}

struct LedgerEntry {
    const char* engine;   // "sema_abi_layout" / "mono_abi_layout"
    std::string key;      // the type as that engine named it
    L           answer;
};

// Not thread-safe by construction: logosc compiles one unit per process, and a
// silent partial ledger would be the failure mode we are removing. The floor
// the gate asserts on the recorded count is what makes an empty ledger red.
std::vector<LedgerEntry>& ledger() noexcept;
void record(const char* engine, std::string key, L answer) noexcept;
bool recording_enabled() noexcept;

// ⚠ THE CANARY — FAULT INJECTION SO A GATE CAN PROVE ITS INSTRUMENT IS LIVE.
//
// `verify_layout_engines()` reports "0 disagreements" both when every engine
// agrees and when it never managed to ask one. Those are the same string, the
// same exit code, and the same green gate — which is the failure this whole arc
// is about, one level up. The previous answer was to ENUMERATE the ways the
// check could go blind (an engine stops recording, the census line disappears,
// the lattice does not reach the registry) and floor each one. That list is
// written by the same mind that wrote the check, so it is exactly as incomplete.
//
// `LOGOS_LAYOUT_CANARY=<engine>` makes the compiler lie about that engine's
// layout by one byte. The gate compiles the SAME program a second time with it
// set and demands the census come back with a NONZERO disagreement count naming
// that engine. If it does not, the gate reports ITSELF broken. No one has to
// think of the blinding mode: whatever kills the comparison also kills the
// canary, because the canary is judged by the same `bad.size()` field of the
// same census line as the real run.
//
// The named engine is one of: `sema_abi_layout`, `mono_abi_layout` (perturbed
// HERE, on the way into the ledger — so the recording door, the dedup, the
// key→truth lookup, the per-engine count and the comparison are all ridden),
// `layout_of`, `mlir_abi_size` (perturbed at the comparison site in
// `verify_layout_engines`, which is where those two are asked).
//
// Off unless the variable is set, read once; a normal compile pays one
// already-initialised static read per `record` call.
const char* canary_engine() noexcept;

}  // namespace logos::compiler::layout
