// Logos project — https://github.com/victor-smirnov/logos
//
// mono_clone.cpp — Expression/statement substitution and function/type cloning.

#include <logos/compiler/op_lang_items.hpp>
#include <cstdio>
#include "mono_impl.hpp"
#include "logos/compiler/sha256.hpp"
#include <logos/compiler/lir_mirror.hpp>
#include <logos/compiler/lir_builder.hpp>
#include <algorithm>
#include <functional>

namespace logos::compiler {

// ── Structural TypeHash (mini-Memoria block_type_hash / ctr_type_hash) ────────
//
// FNV-1a-64 over a tag-prefixed walk of T's structure. Layout-stable: bears
// no struct/field name, so refactors that don't change physical layout
// (rename fields, rename struct, internal reorganisation) preserve the hash.
// Inspired by legacy Memoria's `TypeHash<T>::Value` (compile-time MD5 over
// type-list of constituent codes); we use FNV-1a-64 to share infrastructure
// with WritStatic and ObjectMap-key hashing already in stdlib.
//
// Tag values pick low integers so the most common tags fit in one byte mix.
// Primitive codes mirror legacy Memoria's table for wire-format proximity:
// i8=7, u8=6, i16=8, u16=9, i32=1, u32=10, i64=2, u64=8 collide on legacy
// (legacy assigns u64=8 same as i16) — we deliberately diverge to give every
// primitive a unique code, which matters more for collision avoidance than
// historical legacy parity (we have no on-disk format yet).

static constexpr uint64_t TH_FNV_OFFSET_BASIS = 14695981039346656037ULL;
static constexpr uint64_t TH_FNV_PRIME        = 1099511628211ULL;

// Tag bytes for non-primitive shapes. Distinct from any primitive code.
// Reserved 0x80+ keeps room for primitive codes 1..0x40 if we ever need them.
static constexpr uint64_t TH_TAG_STRUCT = 0x80;
static constexpr uint64_t TH_TAG_TUPLE  = 0x81;
static constexpr uint64_t TH_TAG_ARRAY  = 0x82;
static constexpr uint64_t TH_TAG_PTR    = 0x83;  // *const T
static constexpr uint64_t TH_TAG_REF    = 0x84;  // &T
static constexpr uint64_t TH_TAG_MUTREF = 0x85;  // &mut T
static constexpr uint64_t TH_TAG_SLICE  = 0x86;
static constexpr uint64_t TH_TAG_ENUM   = 0x87;
static constexpr uint64_t TH_TAG_HSTAT  = 0x88;  // WritStatic literal: mix const_val
static constexpr uint64_t TH_TAG_FNPTR  = 0x89;
static constexpr uint64_t TH_TAG_VOID   = 0x01;

static uint64_t th_mix_u64(uint64_t h, uint64_t v) noexcept {
    for (int i = 0; i < 8; ++i) {
        uint8_t b = static_cast<uint8_t>((v >> (i * 8)) & 0xff);
        h ^= b;
        h *= TH_FNV_PRIME;
    }
    return h;
}

static uint64_t th_primitive_code(LogosType::Kind k) noexcept {
    using K = LogosType::Kind;
    switch (k) {
        case K::Bool: return 0x05;
        case K::I8:   return 0x07;
        case K::U8:   return 0x06;
        case K::I16:  return 0x08;
        case K::U16:  return 0x09;
        case K::I32:  return 0x0A;
        case K::U32:  return 0x0B;
        case K::I64:  return 0x0C;
        case K::U64:  return 0x0D;
        case K::I24:  return 0x0E;
        case K::U24:  return 0x0F;
        case K::I56:  return 0x10;
        case K::U56:  return 0x11;
        case K::I128: return 0x12;
        case K::U128: return 0x13;
        case K::F32:  return 0x14;
        case K::F64:  return 0x15;
        default:      return 0;
    }
}

uint64_t Mono::compute_type_hash(TypeRef t, StrSet& seen) noexcept {
    using K = LogosType::Kind;
    uint64_t h = TH_FNV_OFFSET_BASIS;
    if (!t) return th_mix_u64(h, 0);
    K k = t.kind();
    switch (k) {
        case K::Void:
            return th_mix_u64(h, TH_TAG_VOID);
        case K::Bool:
        case K::I8:  case K::U8:
        case K::I16: case K::U16:
        case K::I32: case K::U32:
        case K::I64: case K::U64:
        case K::I24: case K::U24:
        case K::I56: case K::U56:
        case K::I128: case K::U128:
        case K::F32: case K::F64:
            return th_mix_u64(h, th_primitive_code(k));
        case K::Ptr:
            h = th_mix_u64(h, TH_TAG_PTR);
            return th_mix_u64(h, compute_type_hash(t.pointee(), seen));
        case K::Ref:
            h = th_mix_u64(h, TH_TAG_REF);
            return th_mix_u64(h, compute_type_hash(t.pointee(), seen));
        case K::MutRef:
            h = th_mix_u64(h, TH_TAG_MUTREF);
            return th_mix_u64(h, compute_type_hash(t.pointee(), seen));
        case K::Slice:
            h = th_mix_u64(h, TH_TAG_SLICE);
            return th_mix_u64(h, compute_type_hash(t.elem(), seen));
        case K::Array: {
            h = th_mix_u64(h, TH_TAG_ARRAY);
            h = th_mix_u64(h, compute_type_hash(t.elem(), seen));
            h = th_mix_u64(h, t.arr_size());
            // The length name too — sema's type UID mixes it, and a hash that
            // does not collides every symbolic array onto every other one.
            std::string asv(t.arr_size_var());
            for (char ch : asv) h = th_mix_u64(h, static_cast<uint64_t>(ch));
            return h;
        }
        case K::Tuple: {
            h = th_mix_u64(h, TH_TAG_TUPLE);
            auto elems = t.tuple_elems();
            h = th_mix_u64(h, static_cast<uint64_t>(elems.size()));
            for (auto e : elems)
                h = th_mix_u64(h, compute_type_hash(e, seen));
            return h;
        }
        case K::FnPtr:
            // Treat as opaque scalar for now — fn-pointers hashing equal
            // across signatures is the conservative choice; refine if a
            // wire format pins it down.
            return th_mix_u64(h, TH_TAG_FNPTR);
        case K::WStaticLit: {
            // WritStatic literal: identity = byte-hash of the underlying
            // CFG value (already stored in const_val()). No structural
            // recursion — opaque to the compiler at this level.
            h = th_mix_u64(h, TH_TAG_HSTAT);
            uint64_t v = static_cast<uint64_t>(t.const_val().value_or(0));
            return th_mix_u64(h, v);
        }
        case K::Enum:
            // Without per-variant layout introspection wired up here, we
            // hash by enum-tag-only. Refine when block_type_hash needs
            // discriminate variants (= when first persistent enum lands).
            return th_mix_u64(h, TH_TAG_ENUM);
        case K::Struct:
        case K::ZonedStruct: {
            // Cycle guard: recursive structs (e.g. linked-list-style) would
            // otherwise blow the stack. Mix a marker on re-entry.
            std::string sk = type_str(t);
            if (!seen.insert(sk).second)
                return th_mix_u64(h, 0xCAFEBABEull);
            h = th_mix_u64(h, TH_TAG_STRUCT);
            std::string base{t.struct_name()};
            std::string tpkg{t.pkg_name()};
            const TypePoolImpl* cth_pool = out_.type_pool.impl();
            lir_view::StructView tmpl;
            for (auto& sd : in_.structs)
                if (sd.name() == base && (tpkg.empty() || sd.pkg() == tpkg)) {
                    tmpl = sd; break;
                }
            if (!tmpl.valid())
                for (auto& sd : in_.structs)
                    if (sd.name() == base) { tmpl = sd; break; }
            if (!tmpl.valid()) {
                seen.erase(sk);
                return th_mix_u64(h, 0);
            }
            SubstMap fsubst;
            auto tmpl_tps = tmpl.type_params();
            for (size_t i = 0, j = 0; i < tmpl_tps.size(); ++i) {
                if (j < t.type_args().size())
                    fsubst[std::string(tmpl_tps[i].name())] = t.type_args()[j++];
            }
            auto tmpl_fields = tmpl.fields();
            h = th_mix_u64(h, static_cast<uint64_t>(tmpl_fields.size()));
            for (auto f : tmpl_fields) {
                TypeRef ft = subst_type(f.type(cth_pool), fsubst);
                h = th_mix_u64(h, compute_type_hash(ft, seen));
            }
            seen.erase(sk);
            return h;
        }
        default:
            // TypeVar / ConstVar / unresolved — should not reach mono
            // post-substitution. Fall through to a stable but identifiable
            // sentinel so callers can grep for unexpected zero-hashes.
            return th_mix_u64(h, 0xDEADBEEFull);
    }
}

// ── Auto-trait structural check (Rust-style) ──────────────────────────────────
// Mirrors sema_auto_trait.cpp but works against mono's tables.  Used by the
// bound gate in clone_struct_def so that `impl<T: Send> Foo<T>` only clones
// methods when the substituted T actually satisfies Send/Sync.
bool Mono::is_auto_satisfied(TypeRef tv, std::string_view trait_name, StrSet& visited) {
    using Kind = LogosType::Kind;
    if (!tv) return true;
    if (tv.kind() == Kind::Error) return true;

    auto cycle_key = type_str(tv) + "::" + std::string(trait_name);
    if (!visited.insert(cycle_key).second) return true;

    // Auto-trait names arrive as bare compiler text ("Fst", "Send", …), so ask
    // every identity declared under the spelling — facts are identity-keyed and
    // the bare duplicate that used to answer here is gone.
    auto has_any_ = [&](std::string_view tn, const std::string& target) -> bool {
        for (auto& id : bare_trait_identities_(std::string(tn)))
            if (has_concrete_impl_(id, target)) return true;
        return false;
    };
    auto has_explicit = [&](const std::string& name) -> bool {
        return has_any_(trait_name, name);
    };

    switch (tv.kind()) {
    case Kind::Void:
    case Kind::Bool:
    case Kind::I8:  case Kind::I16:  case Kind::I32:  case Kind::I64:
    case Kind::U8:  case Kind::U16:  case Kind::U32:  case Kind::U64:
    case Kind::I24: case Kind::U24:  case Kind::I56:  case Kind::U56:
    case Kind::I128: case Kind::U128:
    case Kind::F32: case Kind::F64:
    case Kind::IntLit: case Kind::FloatLit:
        return true;

    // Fn items/pointers: fine for Send/Sync/Unpin, NOT for Fst (a code
    // address inside a dumpable block is meaningless elsewhere).
    case Kind::FnItem:
    case Kind::FnPtr:
        return trait_name != "Fst";

    // Raw pointers: !Send/!Sync unless explicit unsafe impl (Rust rule).
    case Kind::Ptr: {
        std::string tstr = type_str(tv);
        return has_explicit(tstr);
    }

    // &T  : Send iff T: Sync; Sync iff T: Sync. Never Fst.
    case Kind::Ref:
        if (trait_name == "Fst") return false;
        return is_auto_satisfied(tv.pointee(), "Sync", visited);

    // &mut T: Send iff T: Send; Sync iff T: Sync. Never Fst.
    case Kind::MutRef:
        if (trait_name == "Fst") return false;
        return is_auto_satisfied(tv.pointee(),
                                  trait_name == "Send" ? "Send" : "Sync",
                                  visited);

    case Kind::Array:
        return tv.elem() ? is_auto_satisfied(tv.elem(), trait_name, visited) : true;
    case Kind::Slice:
        if (trait_name == "Fst") return false;    // a fat reference — never
        return tv.elem() ? is_auto_satisfied(tv.elem(), "Sync", visited) : true;

    case Kind::Tuple:
        for (auto e : tv.tuple_elems())
            if (!is_auto_satisfied(e, trait_name, visited)) return false;
        return true;

    case Kind::Struct:
    case Kind::ZonedStruct: {
        std::string base{tv.struct_name()};
        std::string cn = concrete_struct_name(tv);
        // Match sema_auto_trait.cpp: try mangled, type_str, and unmangled base.
        if (has_explicit(cn) || has_explicit(type_str(tv)) || has_explicit(base))
            return true;
        // Fst = no-Drop (mirror of the sema rule): an own Drop impl kills it.
        if (trait_name == "Fst" &&
            (has_any_("Drop", cn) || has_any_("Drop", base)))
            return false;
        // Locate the struct definition. Look in out_ first (post-mono shape),
        // then in_ (pre-mono templates).
        const TypePoolImpl* ias_pool = out_.type_pool.impl();
        lir_view::StructView sd;
        for (auto& s : out_.structs) if (s.name() == cn) { sd = s; break; }
        if (!sd.valid()) for (auto& s : in_.structs) if (s.name() == base) { sd = s; break; }
        if (!sd.valid()) return true;  // unknown — be lenient (matches sema)
        // Build subst from struct's type-params to the concrete tv's type-args.
        SubstMap subst;
        auto sd_tps = sd.type_params();
        if (!tv.type_args().empty() && !sd_tps.empty()) {
            size_t n = std::min(tv.type_args().size(), sd_tps.size());
            for (size_t j = 0; j < n; ++j)
                subst[std::string(sd_tps[j].name())] = tv.type_args()[j];
        }
        for (auto f : sd.fields()) {
            TypeRef ft = f.type(ias_pool);
            if (ft && ft.kind() == Kind::TypeVar && !subst.empty()) {
                auto it = subst.find(std::string(ft.type_var_name()));
                if (it != subst.end()) ft = it->second;
            }
            if (!is_auto_satisfied(ft, trait_name, visited)) return false;
        }
        return true;
    }

    case Kind::UnsizedSlice:
        // Bare `[E]`: Fst iff E is Fst (mirror of the sema rule).
        if (trait_name == "Fst")
            return tv.elem() ? is_auto_satisfied(tv.elem(), "Fst", visited) : true;
        return false;

    case Kind::Enum: {
        if (trait_name == "Fst") {
            std::string en{tv.enum_name()};
            if (has_any_("Drop", en)) return false;
        }
        std::string ename{tv.enum_name()};
        if (has_explicit(ename) || has_explicit(type_str(tv))) return true;
        auto ed = find_enum_view(tv);   // PKG-QUALIFIED FIRST — see find_enum_view
        if (!ed) return true;
        bool ok = true;
        ed->each_variant([&](lir_view::EnumVariantView v) {
            v.each_payload_type(out_.type_pool.impl(), [&](TypeRef pt) {
                if (!is_auto_satisfied(pt, trait_name, visited)) ok = false;
            });
        });
        return ok;
    }

    // TypeVar: should be substituted before we get here; if not, conservative true.
    case Kind::TypeVar:
        return true;

    default:
        return false;
    }
}

// Type-subst function used by the pattern walker.
using TypeSubstFn = std::function<TypeRef(TypeRef)>;
namespace {
class PatSubstWalker {
public:
    PatSubstWalker(TypeSubstFn st, const TypePoolImpl* pool, lir::LProgram* prog) noexcept
        : st_(std::move(st)), pool_(pool), prog_(prog) {}
    lir::Pattern walk(lir_view::PatRef pref) const;
private:
    TypeSubstFn         st_;
    const TypePoolImpl* pool_;
    lir::LProgram*      prog_;
};
} // anonymous

lir::Pattern Mono::subst_pattern(const lir::Pattern& pat, const SubstMap& s) {
    auto pref = pat_ref_of(pat);
    if (!pref) return pat;  // mirror miss — pass through (defensive)
    return subst_pattern(pref, s);
}

lir::Pattern Mono::subst_pattern(lir_view::PatRef pref, const SubstMap& s) {
    PatSubstWalker w([&](TypeRef t) { return subst_type(t, s); },
                     out_.type_pool.impl(), &out_);
    return w.walk(pref);
}

lir_view::StructView Mono::resolve_struct_layout(TypeRef t, SubstMap& m_out) {
    const TypePoolImpl* rsl_pool = out_.type_pool.impl();
    auto args = t.type_args();
    std::string base{t.struct_name()};
    // Prefer the best-matching partial specialisation (e.g. WMap<WString,V> over the
    // empty base WMap<K,V>); bind its pattern type-vars via match_type, exactly as
    // instantiate_struct_templates does — otherwise layout reads the wrong fields.
    if (auto spec = find_best_struct_spec(base, args); spec.valid()) {
        auto pats = spec.spec_patterns(rsl_pool);
        for (size_t i = 0; i < pats.size() && i < args.size(); ++i)
            match_type(args[i], pats[i], m_out);
        return spec;
    }
    auto sit = find_any_struct(t.pkg_name(), t.struct_name());
    if (!sit.valid()) return {};
    auto sit_tps = sit.type_params();
    for (size_t i = 0; i < sit_tps.size() && i < args.size(); ++i)
        m_out[std::string(sit_tps[i].name())] = args[i];
    return sit;
}

uint64_t Mono::mono_abi_size(TypeRef t) { return mono_abi_layout(t).size; }

namespace lay = logos::compiler::layout;

// The accumulation rule, the union rule and the enum rule are ONE copy, in
// layout_law.hpp. Mono's transcription used to be an accumulator ALONE: no
// `is_union()` branch (sum where the layout is a max), no `#[repr(transparent)]`
// branch, and NO ENUM CASE — every enum fell through to `default: {8,8}`, so
// `Option<i32>` and `Option<i64>` and a 40-byte enum were all 8 bytes to the
// phase that computes a DST prefix's field offsets.
Mono::AbiLayout Mono::mono_abi_layout(TypeRef t) {
    using K = LogosType::Kind;
    if (!t) {
        lay::record_declined("mono_abi_layout", "<null type>", "no type to size");
        return {8, 8};
    }
    // `&mut` to a #[zone_mut] struct is the {data, zone} pair (mlir-gen's
    // RefReprKind::FatZoneMut) — every engine must size it so.
    if (t.kind() == K::MutRef && t.pointee() &&
        (TypeRef(t.pointee()).kind() == K::Struct || TypeRef(t.pointee()).kind() == K::ZonedStruct)) {
        SubstMap zm;
        auto zsv = resolve_struct_layout(t.pointee(), zm);
        if (zsv.valid() && zsv.zone_mut()) return {16, 8};
    }
    // Leaf kinds: the ONE table, at the enum (LogosType::scalar_layout).
    if (auto sl = LogosType::scalar_layout(t.kind()); sl.align != 0)
        return {sl.size, sl.align};
    switch (t.kind()) {
    // Unsized `[T]` tail: no bytes of its own, aligned as its element.
    case K::UnsizedSlice:
        return { 0, t.elem() ? mono_abi_layout(t.elem()).align : 1 };
    case K::Array: {
        if (!t.elem()) return {0, 1};
        auto e = mono_abi_layout(t.elem());
        return { t.arr_size() * e.size, e.align };
    }
    case K::Tuple: {
        lay::Agg agg;
        for (auto e : t.tuple_elems()) agg.push(mono_abi_layout(e));
        return agg.finish();
    }
    case K::Struct: case K::ZonedStruct: {
        SubstMap m;
        auto sit = resolve_struct_layout(t, m);
        auto key = lay::type_key(t.pkg_name(), concrete_struct_name(t));
        // A DECLINE, NOT A DEFAULT. mono resolves through the TEMPLATE registry,
        // so a miss here is a struct this run has no declaration for at all —
        // `{8,8}` is a guess that reads as an answer and enters no ledger.
        if (!sit.valid()) {
            lay::record_declined("mono_abi_layout", key,
                                 "no struct template resolvable for this instance");
            return {8, 8};
        }
        return struct_view_layout(sit, m, key);
    }
    case K::Enum: return mono_enum_layout(t);
    default: {
        // NAME THE OFFENDER. `kind_key` alone reports `<kind 32>` — a number
        // that says a decline happened and nothing about WHAT declined, which
        // is exactly the shape of message that sends a reader bisecting. An
        // AssocType / CfgSlotType carries the two halves of its identity
        // (`T::Item`), and a decline on one of them means mono handed the
        // layout law a projection it never substituted — so print it.
        std::string key = lay::kind_key(t.kind());
        if (t.kind() == K::AssocType || t.kind() == K::CfgSlotType) {
            std::string owner(t.type_var_name());
            std::string member(t.assoc_type_name());
            if (!owner.empty() || !member.empty())
                key += " (" + (owner.empty() ? std::string("?") : owner) + "::"
                     + (member.empty() ? std::string("?") : member) + ")";
        }
        lay::record_declined("mono_abi_layout", key,
                             "kind has no layout rule in mono_abi_layout");
        return {8, 8};
    }
    }
}

// A struct's layout FROM ITS DEFINITION, under a substitution. Split out of the
// Struct case so the end-of-run sweep can ask this engine about every struct it
// emitted — including every generic INSTANTIATION — without synthesising a
// TypeRef for each and hoping the name resolves back to the same def.
Mono::AbiLayout Mono::struct_view_layout(lir_view::StructView sv, const SubstMap& m,
                                         std::string key) {
    const TypePoolImpl* p = out_.type_pool.impl();
    auto fl = [&](lir_view::LFieldView f) {
        TypeRef fty = f.type(p);
        return mono_abi_layout(m.empty() ? fty : subst_type(fty, m));
    };
    auto fields = sv.fields();
    // THE LAW DECIDES WHICH SHAPE THIS IS — see `agg_shape` in layout_law.hpp.
    std::vector<lay::L> ml;
    ml.reserve(fields.size());
    for (auto f : fields) ml.push_back(fl(f));
    auto ans = lay::aggregate_layout(
        lay::agg_shape(sv.repr_transparent(), sv.is_union(), fields.size()), ml);
    lay::record("mono_abi_layout", std::move(key), ans);
    return { ans.layout.size, ans.layout.align };
}

// One enum's layout, in mono's terms: payload = max over variants of the
// aggregate of that variant's (substituted) payload types; niche eligibility
// from the shared classifier; then the shared enum rule.
Mono::AbiLayout Mono::mono_enum_layout(TypeRef t) {
    using K = LogosType::Kind;
    std::string ename(t.enum_name());
    auto edo = find_enum_view(t);   // PKG-QUALIFIED FIRST — see find_enum_view
    if (!edo) return {8, 8};
    lir_view::EnumView ed = *edo;
    const TypePoolImpl* p = out_.type_pool.impl();
    SubstMap m;
    {
        std::vector<std::string> tps;
        ed.each_type_param([&](lir_view::EnumTParamView tp) {
            tps.push_back(std::string(tp.name()));
        });
        auto args = t.type_args();
        for (size_t i = 0; i < tps.size() && i < args.size(); ++i)
            m[tps[i]] = args[i];
    }
    std::vector<lay::VariantDesc> vds;
    lay::L payload{0, 1};
    ed.each_variant([&](lir_view::EnumVariantView v) {
        lay::Agg pv;
        lay::VariantDesc vd;
        vd.disc = v.disc();
        TypeRef sole{nullptr};
        v.each_payload_type(p, [&](TypeRef pt0) {
            if (TypeRef(pt0).kind() == K::Void) return;
            TypeRef pt = m.empty() ? pt0 : subst_type(pt0, m);
            pv.push(mono_abi_layout(pt));
            ++vd.n_payload;
            sole = pt;
        });
        if (vd.n_payload == 1) vd.arm = mono_niche_arm(sole);
        vds.push_back(vd);
        auto pl = pv.finish();
        if (pl.size  > payload.size)  payload.size  = pl.size;
        if (pl.align > payload.align) payload.align = pl.align;
    });
    // ONE call, four facts — the same call sema and mlir-gen make. THE BUG THIS
    // SHAPE EXISTS TO PREVENT WAS HERE: mono ran payload → classify → tagged
    // unconditionally, so `enum B : u64` answered {4,4}, and this number is the
    // byte offset of a DstRef prefix field. `backing` is now an argument mono
    // has to produce; it cannot be a branch mono does not have.
    TypeRef bt = ed.backing_type(p);
    auto ans = lay::enum_layout(
        lay::any_payload(vds),
        lay::classify_niche(ed.zoned2(), vds).packed,
        payload,
        bt ? lay::backing_layout(TypeRef(bt).kind()) : std::nullopt);
    // Keyed by the MONO INSTANCE spelling (`Base__arg__arg`) — the one
    // `record_needed_enum` mints and the one mlir-gen registers the instance
    // under — so a generic enum's row really lands in the cross-engine
    // comparison instead of in `n_unmatched`. A non-generic enum is its bare
    // name in all three engines, which is why the C-LIKE cells match at all.
    std::string ekey = enum_instance_name(ename, t.type_args());
    lay::record("mono_abi_layout", lay::type_key(t.pkg_name(), ekey), ans);
    return { ans.layout.size, ans.layout.align };
}

lay::ArmDesc Mono::mono_niche_arm(TypeRef t) {
    using K = LogosType::Kind;
    auto k = TypeRef(t).kind();
    uint64_t pointee_align = 0;
    if ((k == K::Ref || k == K::MutRef) && TypeRef(t).pointee()) {
        // A pointee already being laid out is on a cycle THROUGH this
        // reference (`struct Node<'a> { next: Option<&'a Node<'a>> }`): it
        // holds a pointer, so its alignment is at least a pointer's — all the
        // niche rule asks (≥ 2). Recomputing it recursed without end.
        const uint64_t pk = uint64_t(TypeRef(TypeRef(t).pointee()).offset().value());
        if (niche_pointee_in_progress_.insert(pk).second) {
            pointee_align = mono_abi_layout(TypeRef(t).pointee()).align;
            niche_pointee_in_progress_.erase(pk);
        } else {
            pointee_align = 8;
        }
    }
    bool nonnull_wrapper = false;
    if (k == K::Struct || k == K::ZonedStruct) {
        SubstMap m;
        auto sv = resolve_struct_layout(t, m);
        if (sv.valid() && sv.non_null()) nonnull_wrapper = mono_abi_layout(t).size == 8;
    }
    return lay::arm_desc_of_kind(k, pointee_align, nonnull_wrapper);
}

bool Mono::let_init_is_owned_dyn_tail(const std::string& var, const SubstMap& s) {
    auto it = type_let_inits_.find(var);
    if (it == type_let_inits_.end() || !it->second) return false;
    lir_view::ExprRef rhs = it->second;
    // `let _v: T = *p;` with `p: *mut T` and T bound to `dyn` — the body of
    // `ptr::drop_in_place::<dyn Trait>` (#463). A RAW pointer to a trait object
    // is the fat pair already, and what it points at is an OWNED unsized value
    // being dropped in place: the same case as the DST-tail field below. (A
    // borrowed `&dyn` is not a raw pointer, so it never takes this arm.)
    if (rhs.kind() == lir_schema::expr::Code::Deref) {
        auto op = lir_view::EDerefView{rhs}.operand();
        if (!op) return false;
        TypeRef pt = subst_type(op.type(out_.type_pool.impl()), s);
        if (!pt) return false;
        auto pk = TypeRef(pt).kind();
        if (pk == LogosType::Kind::TraitObject && TypeRef(pt).raw_fat()) return true;
        if (pk == LogosType::Kind::Ptr && TypeRef(pt).pointee()) {
            auto qk = TypeRef(TypeRef(pt).pointee()).kind();
            return qk == LogosType::Kind::UnsizedDyn || qk == LogosType::Kind::TraitObject;
        }
        return false;
    }
    if (rhs.kind() != lir_schema::expr::Code::FieldRead) return false;
    lir_view::EFieldReadView fr{rhs};
    auto recv = fr.receiver();
    if (!recv) return false;
    // Receiver type after substitution: must be a fat custom-DST (`*mut/&
    // ArcInner<dyn>` → DstRef). A thin Ptr/Ref receiver (genuine `Arc<&dyn>`,
    // sized inner) is NOT a DST tail → leave the drop a no-op.
    TypeRef rt = subst_type(recv.type(out_.type_pool.impl()), s);
    if (!rt || TypeRef(rt).kind() != LogosType::Kind::DstRef) return false;
    uint64_t off = 0; TypeRef ftype;
    if (!mono_dst_prefix_field(rt, fr.field(), off, ftype) || !ftype) return false;
    auto fk = TypeRef(ftype).kind();
    return fk == LogosType::Kind::UnsizedDyn ||
           (fk == LogosType::Kind::TraitObject &&
            TypeRef(ftype).trait_owning_kind() == TypeRef::OwningKind::Borrow);
}

bool Mono::mono_dst_prefix_field(TypeRef dstref, std::string_view field,
                                 uint64_t& off_out, TypeRef& ftype_out) {
    using K = LogosType::Kind;
    SubstMap m;
    auto sit = resolve_struct_layout(dstref, m);
    if (!sit.valid() || sit.fields().empty()) return false;
    const TypePoolImpl* mdpf_pool = out_.type_pool.impl();
    // Field offsets from THE LAW — the same call that answers the struct's
    // size, with the same SHAPE, so a prefix offset and a size cannot drift
    // apart and a union-shaped prefix cannot be summed.
    auto sit_fields = sit.fields();
    std::vector<lay::L>   ml;
    std::vector<TypeRef>  fts;
    ml.reserve(sit_fields.size());
    fts.reserve(sit_fields.size());
    for (auto f : sit_fields) {
        TypeRef fty = f.type(mdpf_pool);
        TypeRef ft = m.empty() ? fty : subst_type(fty, m);
        fts.push_back(ft);
        ml.push_back(mono_abi_layout(ft));
    }
    std::vector<uint64_t> offs(ml.size(), 0);
    lay::aggregate_layout(
        lay::agg_shape(sit.repr_transparent(), sit.is_union(), ml.size()), ml, offs);
    size_t fi = 0;
    for (auto f : sit_fields) {
        uint64_t off = offs[fi];
        TypeRef ft = fts[fi];
        ++fi;
        if (f.name() == field) {
            // Returns the field's byte offset + (substituted) type. The caller
            // branches on the kind: a sized PREFIX field → offset deref; the
            // unsized TAIL (UnsizedDyn/UnsizedSlice) → fat-pair projection that
            // reuses the DstRef's carried metadata (vtable/len). `off` is already
            // aligned to this field's natural alignment above (dyn/slice → 8).
            (void)K::Error;
            off_out = off; ftype_out = ft; return true;
        }
    }
    return false;
}

lir_view::ExprRef Mono::subst_expr(lir_view::ExprRef eref, const SubstMap& s,
                          const PackMap& /*unused*/) {
    // packs are stored in cur_packs_ (set by clone_fn)
    if (!eref) {
        std::fprintf(stderr,
            "mono.subst_expr: input ExprRef is null\n");
        std::abort();
    }
    // No husk: build the mirror directly and return an ExprRef over it. `rt_`
    // is the (substituted) node type passed to the emitters; `mp_` is the
    // absolute address of the emitted mirror in out_'s arena.
    TypeRef rt_{};
    const uint8_t* mp_ = nullptr;
    // Phase 5.B: read type from the mirror via the view. Sema keeps mirror's
    // TYPE in sync with C++ LExpr.type via lir_mirror_update_type at the 5
    // post-construction sites (sema_stmt:1244, sema_expr:629/1419/4408/12216).
    // View-based read works for local refs AND for cross-arena refs (where
    // there is no local LExpr to dereference at all).
    rt_ = subst_type(eref.type(out_.type_pool.impl()), s);

    {
        auto subst_child_expr = [&](lir_view::ExprRef er) -> lir_view::ExprRef {
            return er ? subst_expr(er, s) : lir_view::ExprRef{};
        };
        auto subst_child_block = [&](lir_view::BlockRef br) -> lir_view::BlockRef {
            return br ? subst_block(br, s) : lir_view::BlockRef{};
        };
        // Bridge: a few metaprog intrinsic branches build their result entirely
        // through LirBuilder (which speaks LExprPtr husks). Wrap an already-
        // emitted ExprRef (from subst_child_expr) as a thin husk so it can be
        // fed into those LirBuilder chains. The mirror is shared, not re-emitted.
        // LExprPtr is now lir_view::ExprRef — the mirror view IS the handle,
        // so feeding an ExprRef into a LirBuilder chain needs no husk wrapper.
        auto child_husk = [&](lir_view::ExprRef e) -> lir::LExprPtr {
            return e;
        };
        using C = lir_schema::expr::Code;
        switch (eref.kind()) {
        // Stage 2: variant-free leaf-kind cases. Mirror emitted directly,
        // result->kind stays at default (unread by view-based readers).
        case C::LitInt: {
            // Preserve the HIGH half of a 128-bit literal (i128/u128) when
            // cloning — re-emitting via the 64-bit path would silently drop it.
            auto liv = lir_view::ELitIntView{eref};
            int64_t lo = liv.value();
            int64_t hi = liv.value_hi();
            mp_ = (hi != 0)
                ? lir_mirror_emit_lit_int_128(out_, rt_, (uint64_t)lo, (uint64_t)hi)
                : lir_mirror_emit_lit_int(out_, rt_, lo);
            break;
        }
        case C::LitFloat:
            mp_ = lir_mirror_emit_lit_float(
                out_, rt_, lir_view::ELitFloatView{eref}.value());
            break;
        case C::LitBool:
            mp_ = lir_mirror_emit_lit_bool(
                out_, rt_, lir_view::ELitBoolView{eref}.value());
            break;
        case C::LitStr: {
            // Copy to std::string: lir_mirror_emit_* may grow the arena,
            // which invalidates string_view pointers into it.
            std::string v(lir_view::ELitStrView{eref}.value());
            mp_ = lir_mirror_emit_lit_str(out_, rt_, v);
            break;
        }
        case C::VarRef: {
            std::string n(lir_view::EVarRefView{eref}.name());
            // Const-generic value-use: sema emits "__const_param:N" for a
            // `<const N: T>` param referenced in expression position. Two
            // lowerings depending on the param's kind:
            //   IntLit / scalar  → lit_int with the substituted value.
            //   WStaticLit       → splice in the registered WritStatic
            //                       literal (same EWritLit shape as
            //                       inline `let s: WritStatic = @{...};`).
            constexpr std::string_view CP_PFX = "__const_param:";
            if (n.compare(0, CP_PFX.size(), CP_PFX) == 0) {
                std::string pname = n.substr(CP_PFX.size());
                auto sit = s.find(pname);
                if (sit != s.end() && sit->second) {
                    auto kind = TypeRef(sit->second).kind();
                    if (kind == LogosType::Kind::WStaticLit) {
                        uint64_t h = (uint64_t)sit->second.const_val().value_or(0);
                        auto rav = out_.wstatic_registry_.get(std::to_string(h));
                        if (rav.is_null()) {
                            std::fprintf(stderr,
                                "mono: __const_param:%s — WritStatic registry "
                                "miss for hash %016llx\n",
                                pname.c_str(), (unsigned long long)h);
                            // Fall through to var-ref emission so we don't crash.
                        } else {
                            // Deep-clone the registered literal at this site.
                            // subst_expr returns a freshly-pooled LExpr in out_;
                            // splice its mirror_ptr_ + type into `result` so
                            // the surrounding clone-loop sees a well-formed node.
                            //
                            // wstatic_registry_ lives on out_, so we must
                            // construct the view over out_.type_pool.arena()
                            // explicitly — expr_ref_of() would route through
                            // effective_src_arena() which, during a Phase 5.B
                            // cross-arena body walk, points at the foreign
                            // arena and would garble this local read.
                            SubstMap empty;
                            lir_view::ExprRef src_eref(
                                out_.type_pool.arena(),
                                rav);
                            auto cloned = subst_expr(src_eref, empty);
                            if (cloned) {
                                mp_ = cloned.addr();
                                break;
                            }
                        }
                    }
                    if (sit->second.const_val()) {
                        int64_t v = *sit->second.const_val();
                        mp_ = lir_mirror_emit_lit_int(
                            out_, rt_, v);
                        break;
                    }
                }
            }
            // T2-24 (B): const-arg specialization — a param baked to a
            // compile-time literal in this spec clone. `rt_` is the
            // param's (substituted) type, so the emitted literal is well-typed.
            if (!current_const_args_.empty()) {
                if (auto cit = current_const_args_.find(n);
                    cit != current_const_args_.end()) {
                    const ConstArgVal& cv = cit->second;
                    mp_ = cv.is_enum
                        ? lir_mirror_emit_enum_lit(out_, rt_,
                              cv.enum_name, cv.variant, cv.ival)
                        : lir_mirror_emit_lit_int(out_, rt_, cv.ival);
                    break;
                }
            }
            // Phase-1: carry the var slot across monomorphization (else
            // post-mono borrow/mlir lose it and fall back to name-keying).
            mp_ = lir_mirror_emit_var_ref(
                out_, rt_, n, lir_view::EVarRefView{eref}.var_slot());
            break;
        }
        case C::AddrOf: {
            std::string n(lir_view::EAddrOfView{eref}.var_name());
            mp_ = lir_mirror_emit_addr_of(out_, rt_, n, lir_view::EAddrOfView{eref}.origin());
            break;
        }
        case C::PackExpand: {
            std::string n(lir_view::EPackExpandView{eref}.var_name());
            mp_ = lir_mirror_emit_pack_expand(out_, rt_, n);
            break;
        }
        case C::SizeOf: {
            auto t = lir_view::ESizeOfView{eref}.elem_type(out_.type_pool.impl());
            mp_ = lir_mirror_emit_size_of(
                out_, rt_, subst_type(t, s));
            break;
        }
        case C::AlignOf: {
            auto t = lir_view::EAlignOfView{eref}.elem_type(out_.type_pool.impl());
            mp_ = lir_mirror_emit_align_of(
                out_, rt_, subst_type(t, s));
            break;
        }
        case C::GenericRef: {
            // Slice 2: substitute TypeVars in type_args, mangle the symbol,
            // enqueue the instantiation, and rewrite this node into a plain
            // VarRef carrying the mangled name + the (already-substituted)
            // FnPtr type. mlir-gen / borrow-check / scan never see GenericRef.
            lir_view::EGenericRefView v{eref};
            std::string base(v.name());
            auto raw_args = v.type_args(out_.type_pool.impl());
            std::vector<TypeRef> sargs;
            sargs.reserve(raw_args.size());
            for (auto t : raw_args) sargs.push_back(subst_type(t, s));
            std::string mangled = mangle(base, sargs);
            // Enqueue the instantiation if not already scheduled.
            enqueue_if_needed(mangled, sargs);
            mp_ = lir_mirror_emit_var_ref(
                out_, rt_, mangled);
            break;
        }
        case C::Deref: {
            auto op = subst_child_expr(lir_view::EDerefView{eref}.operand());
            mp_ = lir_mirror_emit_deref(
                out_, rt_, op);
            break;
        }
        case C::FieldRead: {
            lir_view::EFieldReadView v{eref};
            std::string field(v.field());
            auto rcv = subst_child_expr(v.receiver());
            // Stage 2 (B): per-instantiation re-lowering. If THIS instance made
            // the receiver a custom-DST fat pointer (`*mut RcInner<dyn>` →
            // DstRef), the field-access shape baked thin at sema-lower time
            // (T abstract) is wrong — a thin struct GEP would read the fat
            // pointer's data-half low bits as the field. Re-emit the projection
            // off the fat pointer's data half (mirror of sema_expr's DstRef field
            // path): a sized PREFIX field → typed offset deref; the unsized TAIL
            // → fat-pair {data+off, carried metadata} reusing the DstRef's own
            // len (slice tail) or vtable (dyn tail).
            TypeRef rcv_type = rcv ? rcv.type(out_.type_pool.impl()) : TypeRef{};
            if (rcv && rcv_type &&
                TypeRef(rcv_type).kind() == LogosType::Kind::DstRef) {
                uint64_t off = 0; TypeRef ftype;
                if (mono_dst_prefix_field(rcv_type, field, off, ftype) && ftype) {
                    auto fk = TypeRef(ftype).kind();
                    LogosTypeBuilder u8b; u8b.kind = LogosType::Kind::U8;
                    TypeRef u8t = out_.type_pool.alloc(std::move(u8b));
                    auto mk_ptr = [&](TypeRef pointee) {
                        LogosTypeBuilder pb; pb.kind = LogosType::Kind::Ptr;
                        pb.mut_ptr = false; pb.pointee = pointee;
                        return out_.type_pool.alloc(std::move(pb));
                    };
                    TypeRef u8p = mk_ptr(u8t);
                    LogosTypeBuilder i64b; i64b.kind = LogosType::Kind::I64;
                    TypeRef i64t = out_.type_pool.alloc(std::move(i64b));
                    auto mk_node = [&](TypeRef ty, const uint8_t* mo) {
                        (void)ty;  // mirror already carries the type
                        return lir_view::ExprRef(out_.type_pool.arena(), mo);
                    };
                    // data half (field 0) + tail/field pointer = data + off.
                    auto data = mk_node(u8p, lir_mirror_emit_slice_ptr(out_, u8p, rcv));
                    auto offl = mk_node(i64t, lir_mirror_emit_lit_int(out_, i64t, (int64_t)off));
                    auto fpu8 = mk_node(u8p, lir_mirror_emit_ptr_arith(
                        out_, u8p, (uint8_t)lir::EPtrArith::Op::ByteAdd, data, offl));
                    if (fk == LogosType::Kind::UnsizedDyn ||
                        (fk == LogosType::Kind::TraitObject &&
                         TypeRef(ftype).trait_owning_kind() == TypeRef::OwningKind::Borrow)) {
                        // dyn tail → `&dyn Tr` {data+off, vtable=DstRef's field1}.
                        // The tail field substitutes to UnsizedDyn (bare) or
                        // TraitObject (a `dyn Trait` arg canonicalised to the
                        // uniform fat form — the common cross-module case); both
                        // denote the unsized dyn tail, so recover the vtable.
                        // Built via a slice_lit typed as TraitObject (metadata slot
                        // = vtable), mirroring sema's dyn-tail projection.
                        LogosTypeBuilder tb; tb.kind = LogosType::Kind::TraitObject;
                        tb.trait_name = std::string(TypeRef(ftype).trait_name());
                        tb.pkg_name   = std::string(TypeRef(ftype).pkg_name());   // #438
                        // Materialise the type-args ONCE: `type_args()` returns a
                        // fresh std::vector each call, so begin()/end() across two
                        // calls are iterators into DIFFERENT temporaries — a garbage
                        // range that blows up vector's length check for any
                        // non-empty list (`dyn Trait<CFG>`). Move the vector wholesale.
                        tb.type_args = TypeRef(ftype).type_args();
                        TypeRef to_t = out_.type_pool.alloc(std::move(tb));
                        auto vtbl = mk_node(i64t, lir_mirror_emit_slice_len(out_, i64t, rcv));
                        rt_ = to_t;
                        mp_ =
                            lir_mirror_emit_slice_lit(out_, to_t, fpu8, vtbl);
                        break;
                    }
                    if (fk == LogosType::Kind::UnsizedSlice) {
                        // slice tail → `&[U]` {data+off, len=DstRef's field1}.
                        TypeRef elem = TypeRef(ftype).elem();
                        LogosTypeBuilder sb; sb.kind = LogosType::Kind::Slice; sb.elem = elem;
                        TypeRef sl_t = out_.type_pool.alloc(std::move(sb));
                        auto fpe = mk_node(mk_ptr(elem),
                            lir_mirror_emit_cast(out_, mk_ptr(elem), fpu8, ""));
                        auto len = mk_node(i64t, lir_mirror_emit_slice_len(out_, i64t, rcv));
                        rt_ = sl_t;
                        mp_ =
                            lir_mirror_emit_slice_lit(out_, sl_t, fpe, len);
                        break;
                    }
                    // sized prefix field → typed offset deref.
                    auto fpft = mk_node(mk_ptr(ftype),
                        lir_mirror_emit_cast(out_, mk_ptr(ftype), fpu8, ""));
                    rt_ = ftype;
                    mp_ = lir_mirror_emit_deref(out_, ftype, fpft);
                    break;
                }
            }
            mp_ = lir_mirror_emit_field_read(
                out_, rt_, rcv, field);
            break;
        }
        case C::TupleIndex: {
            lir_view::ETupleIndexView v{eref};
            uint32_t idx = v.index();
            auto rcv = subst_child_expr(v.receiver());
            mp_ = lir_mirror_emit_tuple_index(
                out_, rt_, rcv, idx);
            break;
        }
        case C::IndexRead: {
            lir_view::EIndexReadView v{eref};
            auto rcv = subst_child_expr(v.receiver());
            auto idx = subst_child_expr(v.index());
            mp_ = lir_mirror_emit_index_read(
                out_, rt_, rcv, idx);
            break;
        }
        case C::Cast: {
            lir_view::ECastView v{eref};
            std::string hbf(v.writ_build_fn());
            auto op = subst_child_expr(v.operand());
            mp_ = lir_mirror_emit_cast(
                out_, rt_, op, hbf);
            break;
        }
        case C::Try: {
            lir_view::ETryView v{eref};
            int32_t ok_disc  = v.ok_disc();
            int32_t err_disc = v.err_disc();
            auto inner = subst_child_expr(v.inner());
            mp_ = lir_mirror_emit_try(
                out_, rt_, inner, ok_disc, err_disc);
            break;
        }
        case C::SliceLit: {
            lir_view::ESliceLitView v{eref};
            auto base = subst_child_expr(v.base());
            auto len  = subst_child_expr(v.len());
            mp_ = lir_mirror_emit_slice_lit(
                out_, rt_, base, len);
            break;
        }
        case C::SliceIndex: {
            lir_view::ESliceIndexView v{eref};
            auto slice = subst_child_expr(v.slice());
            auto idx   = subst_child_expr(v.index());
            mp_ = lir_mirror_emit_slice_index(
                out_, rt_, slice, idx);
            break;
        }
        case C::SliceLen: {
            auto sl = subst_child_expr(lir_view::ESliceLenView{eref}.slice());
            mp_ = lir_mirror_emit_slice_len(out_, rt_, sl);
            break;
        }
        case C::SlicePtr: {
            auto sl = subst_child_expr(lir_view::ESlicePtrView{eref}.slice());
            mp_ = lir_mirror_emit_slice_ptr(out_, rt_, sl);
            break;
        }
        case C::IfExpr: {
            lir_view::EIfExprView v{eref};
            auto cond = subst_child_expr(v.cond());
            auto thn  = subst_child_expr(v.then_val());
            auto els  = subst_child_expr(v.else_val());
            mp_ = lir_mirror_emit_if_expr(
                out_, rt_, cond, thn, els);
            break;
        }
        case C::TupleLit: {
            std::vector<lir_view::ExprRef> elems;
            lir_view::ETupleLitView{eref}.each_elem(
                [&](lir_view::ExprRef er) { elems.push_back(subst_child_expr(er)); });
            mp_ = lir_mirror_emit_tuple_lit(
                out_, rt_, elems);
            break;
        }
        case C::ArrLit: {
            std::vector<lir_view::ExprRef> elems;
            // Track the single source ref so we can re-substitute the value
            // N times for `[v; sizeof...(P)]` fill-literals.
            lir_view::ExprRef fill_src;
            uint64_t src_count = 0;
            lir_view::EArrLitView{eref}.each_elem(
                [&](lir_view::ExprRef er) {
                    fill_src = er;
                    ++src_count;
                    if (er && er.kind() == lir_schema::expr::Code::PackExpand) {
                        TypeRef at = er.type(out_.type_pool.impl());
                        std::string pack_name;
                        bool is_const_pack = false;
                        if (at && (at.kind() == LogosType::Kind::TypeVar ||
                                   at.kind() == LogosType::Kind::ConstVar)) {
                            pack_name = std::string(at.type_var_name());
                            is_const_pack = (at.kind() == LogosType::Kind::ConstVar);
                        }
                        auto pit = cur_packs_.find(pack_name);
                        if (pit != cur_packs_.end()) {
                            std::string pe_var_name(lir_view::EPackExpandView{er}.var_name());
                            for (size_t pi = 0; pi < pit->second.size(); ++pi) {
                                if (is_const_pack && pit->second[pi].const_val()) {
                                    TypeRef et = pit->second[pi].pointee();
                                    if (!et) et = pit->second[pi];
                                    elems.push_back(LirBuilder(out_).lit_int(
                                        *pit->second[pi].const_val(), et));
                                } else {
                                    elems.push_back(LirBuilder(out_).var_ref(
                                        make_pack_arg_name(pe_var_name, pi),
                                        pit->second[pi]));
                                }
                            }
                            return;
                        }
                    }
                    elems.push_back(subst_child_expr(er));
                });
            // [v; sizeof...(P)] fill: sema emits a single-element arr_lit
            // whose type carries arr_size_var; subst_type promoted the array
            // length to N. Repeat the value N times by re-substituting from
            // the original source expr.
            if (src_count == 1 && elems.size() == 1 &&
                rt_ && rt_.kind() == LogosType::Kind::Array) {
                // The length MUST be bound by now. It used to be guarded on
                // `arr_size() > 1` alone, so an unresolved length — which
                // reports size 0 — silently emitted a ONE-element array: the
                // fill quietly produced the wrong number of elements instead
                // of failing.
                if (!rt_.arr_size_var().empty()) {
                    std::fprintf(stderr,
                        "mono: array fill literal reached emission with an unbound "
                        "length '%s'; it would silently produce a 1-element array.\n",
                        std::string(rt_.arr_size_var()).c_str());
                    std::abort();
                }
                uint64_t target = rt_.arr_size();
                while (elems.size() < target)
                    elems.push_back(subst_child_expr(fill_src));
            }
            mp_ = lir_mirror_emit_arr_lit(
                out_, rt_, elems);
            break;
        }
        case C::ClosureCall: {
            lir_view::EClosureCallView v{eref};
            auto callee = subst_child_expr(v.callee());
            std::vector<lir_view::ExprRef> args;
            v.each_arg([&](lir_view::ExprRef er) { args.push_back(subst_child_expr(er)); });
            // Sprint 5.7 follow-up: if the synth-closure path in
            // sema_expr::lower_call emitted ClosureCall for a generic
            // `F: FnOnce(args) -> R` and mono substituted F to a
            // concrete FnPtr type, switch the call kind. The callee
            // var-ref now carries the substituted type (sema emitted
            // var_ref with TypeVar F; mono's subst_type rewrites it
            // to the concrete instantiation).
            TypeRef ct = callee ? callee.type(out_.type_pool.impl()) : TypeRef{};
            auto k = ct ? ct.kind() : LogosType::Kind::Error;
            if (LogosType::is_fn_value_kind(k)) {
                mp_ = lir_mirror_emit_fn_ptr_call(
                    out_, rt_, callee, args);
            } else if (k == LogosType::Kind::Struct ||
                       k == LogosType::Kind::ZonedStruct) {
                // Deferred-2: F was bound by Fn / FnMut / FnOnce and
                // substituted to a user struct with an explicit
                // `impl Fn[Mut|Once]<...> for Struct { fn call[_mut|
                // _once](...) }`. Route the call through the struct's
                // matching method. The original bound's trait name
                // isn't currently threaded through ClosureCall
                // metadata, so probe the struct's method registry
                // for "call" / "call_mut" / "call_once" and pick the
                // first that exists. Covers all three Fn-family
                // bounds with O(1) lookups per call site.
                // The method of the struct's Fn-family impl, read off the impl
                // by trait identity (ADR 0030 S8 row 6): `Fn::call`, else
                // `FnMut::call_mut`, else `FnOnce::call_once` — never a
                // `<Struct>__call` composed and probed for.
                std::string callee_name;
                for (auto [tr, m] : {std::pair<const char*, const char*>{"logos.lang.ops::Fn", "call"},
                                     {"logos.lang.ops::FnMut", "call_mut"},
                                     {"logos.lang.ops::FnOnce", "call_once"}}) {
                    callee_name = trait_item_symbol_(tr, ct, m, int64_t(args.size()) + 1);
                    if (!callee_name.empty()) break;
                }
                if (callee_name.empty()) {
                    trait_item_misses_.push_back(std::format(
                        "a call of `{}` through an Fn-family bound resolves to no Fn / FnMut / FnOnce impl "
                        "(ADR 0030 S8 row 6)", type_str(ct)));
                    mp_ = lir_mirror_emit_closure_call(out_, rt_, callee, args, v.call_mode());
                    break;
                }
                // Method-style: prepend the receiver as the self arg.
                std::vector<lir_view::ExprRef> call_args;
                call_args.push_back(std::move(callee));
                for (auto& a : args) call_args.push_back(std::move(a));
                mp_ = lir_mirror_emit_call(
                    out_, rt_, callee_name, {}, call_args);
            } else {
                mp_ = lir_mirror_emit_closure_call(
                    out_, rt_, callee, args, v.call_mode());
            }
            break;
        }
        case C::FnPtrCall: {
            lir_view::EFnPtrCallView v{eref};
            auto callee = subst_child_expr(v.callee());
            std::vector<lir_view::ExprRef> args;
            v.each_arg([&](lir_view::ExprRef er) { args.push_back(subst_child_expr(er)); });
            mp_ = lir_mirror_emit_fn_ptr_call(
                out_, rt_, callee, args);
            break;
        }
        case C::FormatCall: {
            lir_view::EFormatCallView v{eref};
            auto fmt = subst_child_expr(v.fmt());
            auto arg_types = v.arg_types(out_.type_pool.impl());
            std::vector<lir_view::ExprRef> args;
            v.each_arg([&](lir_view::ExprRef er) { args.push_back(subst_child_expr(er)); });
            mp_ = lir_mirror_emit_format_call(
                out_, rt_, fmt, args, arg_types);
            break;
        }
        case C::PtrArith: {
            lir_view::EPtrArithView v{eref};
            uint8_t op = v.op_code();
            auto ptr = subst_child_expr(v.ptr());
            auto off = subst_child_expr(v.offset());
            mp_ = lir_mirror_emit_ptr_arith(
                out_, rt_, op, ptr, off);
            break;
        }
        case C::PtrDiff: {
            lir_view::EPtrDiffView v{eref};
            bool by_byte = v.by_byte();
            auto lhs = subst_child_expr(v.lhs());
            auto rhs = subst_child_expr(v.rhs());
            mp_ = lir_mirror_emit_ptr_diff(
                out_, rt_, by_byte, lhs, rhs);
            break;
        }
        case C::BlockExpr: {
            lir_view::EBlockExprView v{eref};
            lir_view::BlockRef nb_block{};
            lir_view::ExprRef nb_result{};
            if (auto br = v.block(); br) {
                nb_block = subst_child_block(br);
            }
            if (auto rr = v.result(); rr)
                nb_result = subst_child_expr(rr);
            mp_ = lir_mirror_emit_block_expr(
                out_, rt_, nb_block, nb_result);
            break;
        }
        case C::ReflectOf: {
            auto resolved = subst_type(
                lir_view::EReflectOfView{eref}.type(out_.type_pool.impl()), s);
            mp_ = lir_mirror_emit_reflect_of(
                out_, rt_, resolved);
            if (resolved && TypeRef(resolved).kind() == LogosType::Kind::ZonedStruct &&
                TypeRef(resolved).type_args().empty()) {
                std::string pkg{TypeRef(resolved).pkg_name()};
                std::string fqn = pkg.empty() ? std::string(TypeRef(resolved).struct_name())
                                              : pkg + "::" + std::string(TypeRef(resolved).struct_name());
                lir_mirror_map_put_null(out_, out_.reflect_requests, fqn);
            }
            break;
        }
        case C::Unary: {
            lir_view::EUnaryView v{eref};
            std::string op{v.op()};
            auto new_op = subst_child_expr(v.operand());
            auto vt = new_op ? new_op.type(out_.type_pool.impl()) : TypeRef{};
            // A user operator on a concrete struct (a template's `-t` with
            // `T: Neg`): the method of the impl of the operator's lang trait
            // for that type (ADR 0030 S8 row 6), never a composed name.
            if (vt && TypeRef(vt).kind() == LogosType::Kind::Struct)
                if (const OpLangItem* oi = unary_op_item(op))
                    if (std::string callee = trait_item_symbol_(oi->trait, vt, oi->method, 1); !callee.empty()) {
                        std::vector<lir_view::ExprRef> args; args.push_back(std::move(new_op));
                        mp_ = lir_mirror_emit_call(out_, rt_, callee, {}, args);
                        break;
                    }
            mp_ = lir_mirror_emit_unary(
                out_, rt_, op, new_op);
            break;
        }
        case C::BinOp: {
            lir_view::EBinOpView v{eref};
            std::string op{v.op()};
            auto new_lhs = subst_child_expr(v.lhs());
            auto new_rhs = subst_child_expr(v.rhs());
            auto lt = new_lhs ? new_lhs.type(out_.type_pool.impl()) : TypeRef{};
            // As Unary; the rhs type picks among impls of one operator trait at
            // different type arguments (`Add<V>` / `Add<&V>`).
            if (lt && TypeRef(lt).kind() == LogosType::Kind::Struct)
                if (const OpLangItem* oi = binary_op_item(op)) {
                    std::vector<TypeRef> rtys{new_rhs ? new_rhs.type(out_.type_pool.impl()) : TypeRef{}};
                    if (std::string callee = trait_item_symbol_(oi->trait, lt, oi->method, 2, &rtys); !callee.empty()) {
                        std::vector<lir_view::ExprRef> args;
                        args.push_back(std::move(new_lhs));
                        args.push_back(std::move(new_rhs));
                        mp_ = lir_mirror_emit_call(out_, rt_, callee, {}, args);
                        break;
                    }
                }
            mp_ = lir_mirror_emit_bin_op(
                out_, rt_, op, new_lhs, new_rhs);
            break;
        }
        case C::AddrOfTemp: {
            lir_view::EAddrOfTempView v{eref};
            bool is_mut = v.is_mut();
            auto inner = subst_child_expr(v.inner());
            mp_ = lir_mirror_emit_addr_of_temp(
                out_, rt_, inner, is_mut, v.origin());
            break;
        }
        case C::EnumLit: {
            lir_view::EEnumLitView v{eref};
            std::string enum_name(v.enum_name());
            std::string variant(v.variant());
            int64_t disc = v.disc();
            TypeRef rt(rt_);
            if (rt && rt.kind() == LogosType::Kind::Enum &&
                !rt.type_args().empty()) {
                std::string cname = std::string(rt.enum_name());
                for (auto a : rt.type_args()) { cname += "__"; cname += mangle_type(a); }
                enum_name = std::move(cname);
                record_needed_enum(rt_);
            }
            mp_ = lir_mirror_emit_enum_lit(
                out_, rt_, enum_name, variant, disc);
            break;
        }
        case C::EnumLitData: {
            lir_view::EEnumLitDataView v{eref};
            std::string variant(v.variant());
            int64_t disc = v.disc();
            std::string enum_name;
            TypeRef rt(rt_);
            if (rt && rt.kind() == LogosType::Kind::Enum &&
                !rt.type_args().empty()) {
                std::string cname = std::string(rt.enum_name());
                for (auto a : rt.type_args()) { cname += "__"; cname += mangle_type(a); }
                enum_name = std::move(cname);
                record_needed_enum(rt_);
            } else {
                enum_name = std::string(v.enum_name());
            }
            std::vector<lir_view::ExprRef> payload;
            v.each_payload([&](lir_view::ExprRef er) {
                payload.push_back(subst_child_expr(er));
            });
            mp_ = lir_mirror_emit_enum_lit_data(
                out_, rt_, enum_name, variant, disc, payload);
            break;
        }
        case C::StructLit: {
            lir_view::EStructLitView v{eref};
            std::string name;
            TypeRef rt2(rt_);
            if (rt2 && (rt2.kind() == LogosType::Kind::Struct ||
                        rt2.kind() == LogosType::Kind::ZonedStruct) &&
                !rt2.type_args().empty())
                name = concrete_struct_name(rt_);
            else
                name = std::string(v.name());
            std::vector<std::pair<std::string, lir_view::ExprRef>> fields;
            v.each_field([&](std::string_view fname, lir_view::ExprRef er) {
                fields.push_back({std::string(fname), subst_child_expr(er)});
            });
            record_needed_struct(rt_);
            mp_ = lir_mirror_emit_struct_lit(
                out_, rt_, name, fields);
            break;
        }
        case C::MatchExpr: {
            lir_view::EMatchExprView v{eref};
            auto scrut = subst_child_expr(v.scrut());
            std::vector<lir::EMatchArmView> arms;
            PatSubstWalker pw([&](TypeRef t) { return subst_type(t, s); },
                              out_.type_pool.impl(), &out_);
            v.each_arm([&](lir_view::EMatchArmRef arm) {
                lir::EMatchArmView na;
                if (auto pr = arm.pat(); pr) na.pat = pw.walk(pr);
                else                         na.pat = lir::Pattern{};
                if (auto gr = arm.guard(); gr)
                    na.guard = subst_child_expr(gr);
                na.value = subst_child_expr(arm.value());
                na.line  = arm.line();
                arms.push_back(std::move(na));
            });
            mp_ = lir_mirror_emit_match_expr(
                out_, rt_, scrut, arms);
            break;
        }
        case C::TypeCodeOf: {
            auto resolved = subst_type(
                lir_view::ETypeCodeOfView{eref}.elem_type(out_.type_pool.impl()), s);
            bool has_tv = false;
            std::function<void(TypeRef)> walk = [&](TypeRef t) {
                if (!t || has_tv) return;
                if (TypeRef(t).kind() == LogosType::Kind::TypeVar) { has_tv = true; return; }
                for (auto a : TypeRef(t).type_args()) walk(a);
                if (TypeRef(t).pointee()) walk(TypeRef(t).pointee());
                if (TypeRef(t).elem())    walk(TypeRef(t).elem());
            };
            walk(resolved);
            if (has_tv || !resolved) {
                mp_ = lir_mirror_emit_type_code_of(
                    out_, rt_, resolved);
            } else {
                uint64_t code = 0;
                if (TypeRef(resolved).kind() == LogosType::Kind::Struct ||
                    TypeRef(resolved).kind() == LogosType::Kind::ZonedStruct) {
                    std::string mangled = TypeRef(resolved).type_args().empty()
                        ? std::string(TypeRef(resolved).struct_name())
                        : concrete_struct_name(resolved);
                    for (auto& sd : out_.structs)
                        if (sd.name() == mangled && sd.type_code() != 0)
                            { code = sd.type_code(); break; }
                    if (code == 0)
                        for (auto& ia : out_.inst_annotations)
                            if (ia.mangled_name() == mangled && ia.type_code() != 0)
                                { code = ia.type_code(); break; }
                }
                if (code == 0) {
                    auto hash = type_hash_23(type_str(resolved));
                    uint64_t raw = type_hash_56bit(hash);
                    code = (raw < 128) ? (raw + 128) : raw;
                }
                mp_ = lir_mirror_emit_lit_int(
                    out_, rt_, (int64_t)code);
            }
            break;
        }
        case C::WritLit: {
            lir_view::EWritLitView v{eref};
            namespace hvc = lir_schema::writ_val;
            std::function<lir::WritValPtr(lir_view::WritValRef)> clone_hv =
                [&](lir_view::WritValRef vref) -> lir::WritValPtr {
                auto out = lir::alloc_writ_val(out_);
                if (!vref) {
                    std::fprintf(stderr,
                        "mono.clone_hv: null WritValRef\n");
                    std::abort();
                }
                switch (vref.kind()) {
                case hvc::Code::Null:
                    out->mirror_ptr_ = lir_mirror_emit_hv_null(out_);
                    break;
                case hvc::Code::Bool:
                    out->mirror_ptr_ = lir_mirror_emit_hv_bool(
                        out_, lir_view::WVBoolView{vref}.value());
                    break;
                case hvc::Code::Int:
                    out->mirror_ptr_ = lir_mirror_emit_hv_int(
                        out_, lir_view::WVIntView{vref}.value());
                    break;
                case hvc::Code::Float:
                    out->mirror_ptr_ = lir_mirror_emit_hv_float(
                        out_, lir_view::WVFloatView{vref}.value());
                    break;
                case hvc::Code::Str: {
                    std::string s(lir_view::WVStrView{vref}.value());
                    out->mirror_ptr_ = lir_mirror_emit_hv_str(out_, s);
                    break;
                }
                case hvc::Code::Capture: {
                    lir_view::WVCaptureView cv{vref};
                    out->mirror_ptr_ = lir_mirror_emit_hv_capture(
                        out_, cv.param_index(), cv.value_index());
                    break;
                }
                case hvc::Code::Type: {
                    lir_view::WVTypeView tv{vref};
                    out->mirror_ptr_ = lir_mirror_emit_hv_type(
                        out_, tv.kind(), tv.uid(), std::string(tv.name()));
                    break;
                }
                case hvc::Code::Map: {
                    lir_view::WVMapView mv{vref};
                    std::string key_type(mv.key_type());
                    bool int_keys = mv.int_keyed();
                    std::vector<lir::WVMapEntry> entries;
                    entries.reserve(mv.size());
                    for (uint64_t i = 0; i < mv.size(); ++i) {
                        lir::WVMapEntry ent;
                        if (int_keys) ent.key = mv.int_key(i);
                        else          ent.key = std::string(mv.str_key(i));
                        ent.val = clone_hv(mv.value(i));
                        entries.push_back(std::move(ent));
                    }
                    out->mirror_ptr_ = lir_mirror_emit_hv_map(
                        out_, entries, key_type);
                    break;
                }
                case hvc::Code::Array: {
                    lir_view::WVArrayView av{vref};
                    std::string elem_type(av.elem_type());
                    std::vector<lir::WritValPtr> elements;
                    elements.reserve(av.size());
                    for (uint64_t i = 0; i < av.size(); ++i)
                        elements.push_back(clone_hv(av.elem(i)));
                    out->mirror_ptr_ = lir_mirror_emit_hv_array(
                        out_, elements, elem_type);
                    break;
                }
                }
                return out;
            };
            lir::WritValPtr root = nullptr;
            if (auto root_ref = v.root()) root = clone_hv(root_ref);
            bool has_captures = v.has_captures();
            uint32_t capture_param_count = v.capture_param_count();
            std::vector<lir_view::ExprRef> capture_exprs;
            std::vector<TypeRef> capture_types;
            v.each_capture_expr([&](lir_view::ExprRef er) {
                capture_exprs.push_back(subst_child_expr(er));
            });
            v.each_capture_type(out_.type_pool.impl(),
                [&](TypeRef ct) { capture_types.push_back(subst_type(ct, s)); });
            // Copy static_blob OUT of the source arena before emit: emit
            // allocates into the same arena (mono moves in_.type_pool into
            // out_), and growth invalidates the source-side string_view.
            // Same pattern used at line 908 for WVStrView.
            std::string static_blob_copy(v.static_blob());
            mp_ = lir_mirror_emit_writ_lit(
                out_, rt_, root, has_captures,
                capture_exprs, capture_types, capture_param_count,
                static_blob_copy);
            break;
        }
        case C::Call: {
            lir_view::ECallView v{eref};
            lir::ECall nc;
            nc.callee = std::string(v.callee());
            for (auto ta : v.type_args(out_.type_pool.impl())) {
                // `$fs...` call-splice: sema encodes a reflected-[Type]
                // producer var as a marker TypeVar `__splicepack$<var>`. Chase
                // the var to its producer call (type_let_inits_) and fold the
                // element TypeRefs straight into the callee's type-args — so a
                // recursive generic fn folds over a struct's field types (or
                // args / tuple elems / typelist tail). The expanded concrete
                // types then drive normal variadic instantiation (mirrors how a
                // `T...` pack expands from cur_packs_, but the source is a
                // producer rather than a caller pack).
                if (ta && ta.kind() == LogosType::Kind::TypeVar) {
                    std::string nm(ta.type_var_name());
                    static constexpr std::string_view kSplice = "__splicepack$";
                    if (nm.rfind(kSplice.data(), 0) == 0) {
                        std::string vn = nm.substr(kSplice.size());
                        lir_view::ExprRef prod{};
                        if (auto it = type_let_inits_.find(vn);
                            it != type_let_inits_.end()) prod = it->second;
                        for (int hops = 0; hops < 8 && prod &&
                             prod.kind() == lir_schema::expr::Code::VarRef; ++hops) {
                            std::string v2(lir_view::EVarRefView{prod}.name());
                            auto it = type_let_inits_.find(v2);
                            if (it == type_let_inits_.end()) break;
                            prod = it->second;
                        }
                        bool ok = false;
                        std::vector<TypeRef> elems;
                        if (prod && prod.kind() == lir_schema::expr::Code::Call) {
                            lir_view::ECallView pc{prod};
                            std::string cn(pc.callee());
                            auto tas = pc.type_args(out_.type_pool.impl());
                            if (cn == "__type_refs_of__") {
                                for (auto a : tas) elems.push_back(subst_type(a, s));
                                ok = true;
                            } else if (cn == "__args_of__") {
                                if (!tas.empty()) {
                                    TypeRef T = subst_type(tas[0], s);
                                    for (auto a : T.type_args()) elems.push_back(a);
                                    ok = true;
                                }
                            } else if (cn == "__typelist_tail__") {
                                if (!tas.empty()) {
                                    TypeRef T = subst_type(tas[0], s);
                                    auto pk = T.type_args();
                                    for (size_t i = 1; i < pk.size(); ++i)
                                        elems.push_back(pk[i]);
                                    ok = true;
                                }
                            } else if (cn == "__tuple_elems_of__") {
                                if (!tas.empty()) {
                                    TypeRef T = subst_type(tas[0], s);
                                    if (T && T.kind() == LogosType::Kind::Tuple)
                                        for (auto a : T.tuple_elems()) elems.push_back(a);
                                    ok = true;
                                }
                            } else if (cn == "__field_types_of__") {
                                if (!tas.empty()) {
                                    // A non-struct T has no fields → empty pack
                                    // (matches __field_types_of__'s [Type;0]).
                                    // Mono monomorphizes BOTH arms of a runtime
                                    // `if t.is_struct()`, so a leaf type still
                                    // instantiates the struct-arm splice; it
                                    // must degrade to empty, not abort. An empty
                                    // variadic pack resolves to the 0-arg base
                                    // overload (same as a `T...` pack going empty).
                                    ok = true;
                                    TypeRef T = subst_type(tas[0], s);
                                    if (T && (T.kind() == LogosType::Kind::Struct ||
                                              T.kind() == LogosType::Kind::ZonedStruct)) {
                                        std::string base{T.struct_name()};
                                        std::string tpkg{T.pkg_name()};
                                        const TypePoolImpl* sp_pool = out_.type_pool.impl();
                                        lir_view::StructView tmpl;
                                        for (auto& sd : in_.structs)
                                            if (sd.name() == base &&
                                                (tpkg.empty() || sd.pkg() == tpkg)) { tmpl = sd; break; }
                                        if (!tmpl.valid())
                                            for (auto& sd : in_.structs)
                                                if (sd.name() == base) { tmpl = sd; break; }
                                        if (tmpl.valid()) {
                                            SubstMap fsubst;
                                            auto tmpl_tps = tmpl.type_params();
                                            for (size_t i = 0, j = 0;
                                                 i < tmpl_tps.size(); ++i)
                                                if (j < T.type_args().size())
                                                    fsubst[std::string(tmpl_tps[i].name())] =
                                                        T.type_args()[j++];
                                            for (auto f : tmpl.fields())
                                                elems.push_back(subst_type(f.type(sp_pool), fsubst));
                                            ok = true;
                                        }
                                    }
                                }
                            }
                        }
                        if (!ok) {
                            std::fprintf(stderr,
                                "mono.splicepack: '%s' is not a recognized "
                                "[Type] producer (field_types_of / args_of / "
                                "type_refs_of / tuple_elems_of / typelist_tail)\n",
                                vn.c_str());
                            std::abort();
                        }
                        for (auto e : elems) nc.type_args.push_back(e);
                        continue;
                    }
                }
                // Pack-key may be encoded as TypeVar (type pack) or ConstVar
                // (const pack); both store the pack name in `type_var_name`.
                if (ta && (ta.kind() == LogosType::Kind::TypeVar ||
                           ta.kind() == LogosType::Kind::ConstVar)) {
                    auto pit = cur_packs_.find(std::string(ta.type_var_name()));
                    if (pit != cur_packs_.end()) {
                        // Phase 5.B step 3: pack entries may be foreign
                        // TypeRefs (cur_packs_ comes from a caller's
                        // SubstMap which may have been built over the
                        // foreign body's type-arg references). localize
                        // before splicing into the local nc.type_args.
                        for (auto pt : pit->second) nc.type_args.push_back(localize_type(pt));
                        continue;
                    }
                }
                nc.type_args.push_back(subst_type(ta, s));
            }
            // sizeof...(T) intrinsic: sema lowered it to a magic call that
            // carries the pack TypeVar in type_args[0]. After expansion above,
            // nc.type_args holds the concrete pack types — emit their count.
            if (nc.callee == "__sizeof_pack__") {
                int64_t n = (int64_t)nc.type_args.size();
                mp_ = lir_mirror_emit_lit_int(out_, rt_, n);
                break;
            }
            // type_of::<T>() intrinsic: sema lowered to magic call with T in
            // type_args[0]. After subst_type above, type_args[0] is the
            // concrete monomorphized type — emit its kind as u32 literal.
            if (nc.callee == "__type_kind_of__") {
                int64_t k = nc.type_args.empty() ? 0
                          : (int64_t)nc.type_args[0].kind();
                mp_ = lir_mirror_emit_lit_int(out_, rt_, k);
                break;
            }
            // wstatic_hash_of::<CFG>() — byte-hash identity of CFG as u64.
            // Post-subst, type_args[0] is WStaticLit kind whose const_val
            // is the hash.
            if (nc.callee == "__wstatic_hash_of__") {
                int64_t v = (nc.type_args.empty() || !nc.type_args[0])
                          ? 0
                          : (int64_t)(uint64_t)nc.type_args[0].const_val().value_or(0);
                mp_ = lir_mirror_emit_lit_int(out_, rt_, v);
                break;
            }
            // type_hash::<T>() — structural FNV-1a-64 hash of T.
            // Layout-stable: bears no struct/field name, recurses into
            // field types after subst. See compute_type_hash() above.
            if (nc.callee == "__type_hash_of__") {
                StrSet seen;
                uint64_t h = (nc.type_args.empty())
                           ? 0
                           : compute_type_hash(nc.type_args[0], seen);
                mp_ = lir_mirror_emit_lit_int(
                    out_, rt_, (int64_t)h);
                break;
            }
            // type_of::<T>().name — magic intrinsic that mono replaces with
            // a &[u8] literal of the canonical type_str(T) for concrete T.
            if (nc.callee == "__type_name_of__") {
                std::string s = nc.type_args.empty() ? std::string()
                              : type_str(nc.type_args[0]);
                mp_ = lir_mirror_emit_lit_str(out_, rt_, s);
                break;
            }
            // type_of::<T>().uid — TypeUID = first 8 bytes of
            // SHA-256(type_str(T)) as u64. Stable identity for
            // quote_ty! reification (mono-time reverse lookup) and the
            // shared byte source for Writ schema_type_code / TagSystem.
            if (nc.callee == "__type_uid_of__") {
                uint64_t uid = 0;
                if (!nc.type_args.empty() && nc.type_args[0]) {
                    uid = type_hash_64bit(type_hash_23(type_id_canon(nc.type_args[0])));
                    uid_to_type_[uid] = nc.type_args[0];
                }
                mp_ = lir_mirror_emit_lit_int(
                    out_, rt_, (int64_t)uid);
                break;
            }
            // High 64 bits of the 128-bit nominal UID (TypeId.hi). Same canon
            // input as __type_uid_of__ (the low half) so the two halves agree.
            if (nc.callee == "__type_uid_hi_of__") {
                uint64_t uid_hi = 0;
                if (!nc.type_args.empty() && nc.type_args[0])
                    uid_hi = type_hash_hi64(type_hash_23(type_id_canon(nc.type_args[0])));
                mp_ = lir_mirror_emit_lit_int(
                    out_, rt_, (int64_t)uid_hi);
                break;
            }
            // Type-trait predicates: mono evaluates after substitution. Each
            // returns a lit_bool of the answer for the concrete substituted T.
            {
                using K = LogosType::Kind;
                auto bool_of = [&](bool r) {
                    mp_ =
                        lir_mirror_emit_lit_bool(out_, rt_, r);
                };
                if (nc.callee == "__is_same__") {
                    bool r = (nc.type_args.size() == 2 &&
                              nc.type_args[0] == nc.type_args[1]);
                    bool_of(r); break;
                }
                K tk = (nc.type_args.empty() || !nc.type_args[0])
                       ? K::Error : nc.type_args[0].kind();
                bool integer = (tk == K::I8  || tk == K::I16 || tk == K::I24 ||
                                tk == K::I32 || tk == K::I56 || tk == K::I64 ||
                                tk == K::I128 ||
                                tk == K::U8  || tk == K::U16 || tk == K::U24 ||
                                tk == K::U32 || tk == K::U56 || tk == K::U64 ||
                                tk == K::U128);
                bool floating = (tk == K::F32 || tk == K::F64);
                if      (nc.callee == "__is_ptr__")       { bool_of(tk == K::Ptr); break; }
                else if (nc.callee == "__is_ref__")       { bool_of(tk == K::Ref); break; }
                else if (nc.callee == "__is_mut_ref__")   { bool_of(tk == K::MutRef); break; }
                else if (nc.callee == "__is_struct__")    { bool_of(tk == K::Struct); break; }
                else if (nc.callee == "__is_zoned__")     { bool_of(tk == K::ZonedStruct); break; }
                else if (nc.callee == "__is_enum__")      { bool_of(tk == K::Enum); break; }
                else if (nc.callee == "__is_tuple__")     { bool_of(tk == K::Tuple); break; }
                else if (nc.callee == "__is_slice__")     { bool_of(tk == K::Slice); break; }
                else if (nc.callee == "__is_array__")     { bool_of(tk == K::Array); break; }
                else if (nc.callee == "__is_bool__")      { bool_of(tk == K::Bool); break; }
                else if (nc.callee == "__is_float__")     { bool_of(floating); break; }
                else if (nc.callee == "__is_integer__")   { bool_of(integer); break; }
                else if (nc.callee == "__is_signed__") {
                    bool_of(LogosType::is_signed_int_kind(tk)); break;
                }
                else if (nc.callee == "__is_unsigned__") {
                    bool_of(LogosType::is_unsigned_int_kind(tk)); break;
                }
                else if (nc.callee == "__is_primitive__") {
                    bool_of(tk == K::Bool || floating || integer); break;
                }
            }
            // args_count_of::<T>() — emit lit_int N where N is T's number
            // of generic type arguments (0 for non-generic / primitive T).
            if (nc.callee == "__args_count_of__") {
                int64_t n = 0;
                if (!nc.type_args.empty())
                    n = (int64_t)nc.type_args[0].type_args().size();
                LirBuilder b(out_);
                LogosTypeBuilder i64_b; i64_b.kind = LogosType::Kind::I64;
                TypeRef i64_t = out_.type_pool.alloc(std::move(i64_b));
                auto lit = b.lit_int(n, i64_t);
                rt_ = i64_t;
                mp_ = lit.addr();
                break;
            }
            // has_trait::<T, Trait>() — bool. Recursive lookup against
            // concrete_impls_ + blanket_impls_, same shape as the bound-check
            // already used for blanket method emission.
            if (nc.callee == "__has_trait__") {
                bool answer = false;
                std::string trait;
                // Args are populated below the dispatch chain — read the trait
                // name directly off the original expression's first lit_str arg.
                v.each_arg([&](lir_view::ExprRef ar) {
                    if (!trait.empty()) return;
                    if (ar && ar.kind() == lir_schema::expr::Code::LitStr)
                        trait = std::string(lir_view::ELitStrView{ar}.value());
                });
                if (!nc.type_args.empty() && !trait.empty()) {
                    TypeRef T = nc.type_args[0];
                    if (T) {
                        // ADR 0030 S9: the type's impl, by C-OBL.
                        StrSet unused;
                        answer = mono_concrete_satisfies_bound(TraitQuery(trait), T, unused);
                    }
                }
                LogosTypeBuilder b_b; b_b.kind = LogosType::Kind::Bool;
                TypeRef bool_t = out_.type_pool.alloc(std::move(b_b));
                rt_ = bool_t;
                mp_ =
                    lir_mirror_emit_lit_bool(out_, bool_t, answer);
                break;
            }
            // has_trait_of::<Trait>(t: Type) — Type-method form. Recovers
            // concrete T from t's StructLit "uid" field (a __type_uid_of__
            // call), then runs the same impl-table recursion as __has_trait__.
            if (nc.callee == "__has_trait_of__") {
                std::string trait;
                lir_view::ExprRef t_ref;
                size_t arg_idx = 0;
                v.each_arg([&](lir_view::ExprRef ar) {
                    if (arg_idx == 0 && ar &&
                        ar.kind() == lir_schema::expr::Code::LitStr)
                        trait = std::string(lir_view::ELitStrView{ar}.value());
                    else if (arg_idx == 1) t_ref = ar;
                    ++arg_idx;
                });
                auto chase = [&](lir_view::ExprRef er) {
                    for (int hops = 0; hops < 8 &&
                         er &&
                         er.kind() == lir_schema::expr::Code::VarRef; ++hops) {
                        std::string vn(lir_view::EVarRefView{er}.name());
                        auto it = type_let_inits_.find(vn);
                        if (it == type_let_inits_.end()) break;
                        er = it->second;
                    }
                    return er;
                };
                t_ref = chase(t_ref);
                TypeRef T{};
                if (t_ref && t_ref.kind() == lir_schema::expr::Code::StructLit) {
                    lir_view::EStructLitView{t_ref}.each_field(
                        [&](std::string_view fname,
                            lir_view::ExprRef fer) {
                            if (T || fname != "uid") return;
                            if (fer && fer.kind() ==
                                      lir_schema::expr::Code::Call) {
                                lir_view::ECallView cv{fer};
                                if (cv.callee() == "__type_uid_of__") {
                                    auto tas = cv.type_args(
                                        out_.type_pool.impl());
                                    if (!tas.empty())
                                        T = subst_type(tas[0], s);
                                }
                            }
                        });
                }
                bool answer = false;
                if (T && !trait.empty()) {
                    // ADR 0030 S9: the type's impl, by C-OBL.
                    StrSet unused;
                    answer = mono_concrete_satisfies_bound(TraitQuery(trait), T, unused);
                }
                LogosTypeBuilder b_b; b_b.kind = LogosType::Kind::Bool;
                TypeRef bool_t = out_.type_pool.alloc(std::move(b_b));
                rt_ = bool_t;
                mp_ =
                    lir_mirror_emit_lit_bool(out_, bool_t, answer);
                break;
            }
            // typelist_len::<L>() — emit lit_int(N) where N = L.type_args().
            // O(1) probe; the typelevel-pack equivalent of args_count_of (the
            // canonical use case is L = TypeList<T...>).
            if (nc.callee == "__typelist_len__") {
                int64_t n = 0;
                if (!nc.type_args.empty())
                    n = (int64_t)nc.type_args[0].type_args().size();
                LirBuilder b(out_);
                LogosTypeBuilder i64_b; i64_b.kind = LogosType::Kind::I64;
                TypeRef i64_t = out_.type_pool.alloc(std::move(i64_b));
                auto lit = b.lit_int(n, i64_t);
                rt_ = i64_t;
                mp_ = lit.addr();
                break;
            }
            // typelist_head::<L>() / typelist_nth::<L>(i) — emit a single
            // `Type { kind, name, size }` struct lit for L's pack element.
            // Empty pack / out-of-range index → fatal compile error.
            if (nc.callee == "__typelist_head__" ||
                nc.callee == "__typelist_nth__") {
                if (nc.type_args.empty()) {
                    std::fprintf(stderr,
                        "mono.%s: missing type argument\n", nc.callee.c_str());
                    std::abort();
                }
                auto pack = nc.type_args[0].type_args();
                int64_t idx = 0;
                if (nc.callee == "__typelist_nth__") {
                    bool got = false;
                    v.each_arg([&](lir_view::ExprRef ar) {
                        if (got) return;
                        if (ar && ar.kind() == lir_schema::expr::Code::LitInt) {
                            idx = lir_view::ELitIntView{ar}.value();
                            got = true;
                        }
                    });
                    if (!got) {
                        std::fprintf(stderr,
                            "mono.__typelist_nth__: index must be a literal int\n");
                        std::abort();
                    }
                }
                if (idx < 0 || (size_t)idx >= pack.size()) {
                    std::fprintf(stderr,
                        "mono.%s: index %lld out of range (pack size %zu)\n",
                        nc.callee.c_str(),
                        (long long)idx, pack.size());
                    std::abort();
                }
                TypeRef ti = pack[idx];
                TypeRef type_t = rt_;  // sema set this to struct Type
                LogosTypeBuilder u32_b; u32_b.kind = LogosType::Kind::U32;
                TypeRef u32_t = out_.type_pool.alloc(std::move(u32_b));
                LogosTypeBuilder u8_b;  u8_b.kind  = LogosType::Kind::U8;
                TypeRef u8_t  = out_.type_pool.alloc(std::move(u8_b));
                LogosTypeBuilder sl_b;  sl_b.kind  = LogosType::Kind::Slice;
                sl_b.elem = u8_t;
                TypeRef slice_u8_t = out_.type_pool.alloc(std::move(sl_b));
                LogosTypeBuilder i64_b; i64_b.kind = LogosType::Kind::I64;
                TypeRef i64_t = out_.type_pool.alloc(std::move(i64_b));
                LogosTypeBuilder u64_b; u64_b.kind = LogosType::Kind::U64;
                TypeRef u64_t = out_.type_pool.alloc(std::move(u64_b));
                LirBuilder b(out_);
                std::vector<std::pair<std::string, lir::LExprPtr>> f;
                f.emplace_back("kind", b.lit_int((int64_t)ti.kind(), u32_t));
                f.emplace_back("name", b.lit_str(type_str(ti), slice_u8_t));
                f.emplace_back("size", b.size_of(ti, i64_t));
                f.emplace_back("align", b.align_of(ti, i64_t));
                {
                    uint64_t uid = type_hash_64bit(type_hash_23(type_id_canon(ti)));
                    uid_to_type_[uid] = ti;
                    f.emplace_back("uid", b.lit_int((int64_t)uid, u64_t));
                }
                auto sl = b.struct_lit(type_t, std::move(f));
                rt_ = type_t;
                mp_ = sl.addr();
                break;
            }
            // reify_type(t: Type) -> Type — recover source TypeRef from
            // a direct producer expression and emit a fresh `Type`
            // struct lit. MVP shapes (no let indirection):
            //   1. a `Type` struct lit (built by the #106 synthesis overload
            //      `struct_lit(type_t, …)`, whose stored name comes from the
            //      TypeRef, not from a bare literal) where the "uid" field is
            //      a `__type_uid_of__::<T>()` call (sema producers like
            //      `type_of`, `quote_ty!`). Pull T from type_args, subst.
            //   2. Call to a mono-emitting Type producer
            //      (`__typelist_head__`, `__typelist_nth__`,
            //      `__args_of__`...) — defer; flag as unsupported for now.
            if (nc.callee == "__reify_type__") {
                TypeRef ti{};
                lir_view::ExprRef arg_ref;
                v.each_arg([&](lir_view::ExprRef ar) {
                    if (!arg_ref) arg_ref = ar;
                });
                if (!arg_ref) {
                    std::fprintf(stderr,
                        "mono.__reify_type__: missing argument\n");
                    std::abort();
                }
                // Follow VarRef back through `let` bindings recorded by
                // subst_stmt's Let case. Cap the chain to avoid pathological
                // self-referencing input.
                for (int hops = 0; hops < 8 &&
                     arg_ref &&
                     arg_ref.kind() == lir_schema::expr::Code::VarRef; ++hops) {
                    std::string vn(lir_view::EVarRefView{arg_ref}.name());
                    auto it = type_let_inits_.find(vn);
                    if (it == type_let_inits_.end()) break;
                    arg_ref = it->second;
                }
                if (arg_ref.kind() == lir_schema::expr::Code::Call) {
                    lir_view::ECallView cv{arg_ref};
                    auto cn = cv.callee();
                    if (cn == "__typelist_nth__" || cn == "__typelist_head__") {
                        auto tas = cv.type_args(out_.type_pool.impl());
                        if (!tas.empty()) {
                            TypeRef L = subst_type(tas[0], s);
                            auto pack = L.type_args();
                            int64_t idx = 0;
                            if (cn == "__typelist_nth__") {
                                bool got = false;
                                cv.each_arg([&](lir_view::ExprRef ar) {
                                    if (got) return;
                                    if (ar && ar.kind() ==
                                              lir_schema::expr::Code::LitInt) {
                                        idx = lir_view::ELitIntView{ar}.value();
                                        got = true;
                                    }
                                });
                            }
                            if (idx >= 0 && (size_t)idx < pack.size())
                                ti = pack[idx];
                        }
                    }
                }
                if (arg_ref.kind() == lir_schema::expr::Code::StructLit) {
                    lir_view::EStructLitView{arg_ref}.each_field(
                        [&](std::string_view fname, lir_view::ExprRef fer) {
                            if (ti || fname != "uid") return;
                            if (fer &&
                                fer.kind() == lir_schema::expr::Code::Call) {
                                lir_view::ECallView cv{fer};
                                if (cv.callee() == "__type_uid_of__") {
                                    auto tas = cv.type_args(out_.type_pool.impl());
                                    if (tas.size() >= 1)
                                        ti = subst_type(tas[0], s);
                                }
                            }
                        });
                }
                if (!ti) {
                    std::fprintf(stderr,
                        "mono.__reify_type__: argument shape not yet "
                        "supported (must be a direct sema producer like "
                        "type_of::<T>() or quote_ty! { T })\n");
                    std::abort();
                }
                uint64_t uid = type_hash_64bit(type_hash_23(type_id_canon(ti)));
                uid_to_type_[uid] = ti;
                TypeRef type_t = rt_;
                LogosTypeBuilder u32_b; u32_b.kind = LogosType::Kind::U32;
                TypeRef u32_t = out_.type_pool.alloc(std::move(u32_b));
                LogosTypeBuilder u8_b;  u8_b.kind  = LogosType::Kind::U8;
                TypeRef u8_t  = out_.type_pool.alloc(std::move(u8_b));
                LogosTypeBuilder sl_b;  sl_b.kind  = LogosType::Kind::Slice;
                sl_b.elem = u8_t;
                TypeRef slice_u8_t = out_.type_pool.alloc(std::move(sl_b));
                LogosTypeBuilder i64_b; i64_b.kind = LogosType::Kind::I64;
                TypeRef i64_t = out_.type_pool.alloc(std::move(i64_b));
                LogosTypeBuilder u64_b; u64_b.kind = LogosType::Kind::U64;
                TypeRef u64_t = out_.type_pool.alloc(std::move(u64_b));
                LirBuilder b(out_);
                std::vector<std::pair<std::string, lir::LExprPtr>> f;
                f.emplace_back("kind", b.lit_int((int64_t)ti.kind(), u32_t));
                f.emplace_back("name", b.lit_str(type_str(ti), slice_u8_t));
                f.emplace_back("size", b.size_of(ti, i64_t));
                f.emplace_back("align", b.align_of(ti, i64_t));
                f.emplace_back("uid",  b.lit_int((int64_t)uid, u64_t));
                auto sl = b.struct_lit(type_t, std::move(f));
                rt_ = type_t;
                mp_ = sl.addr();
                break;
            }
            // apply(name: &[u8], args: [Type; N]) -> Type — instantiate a
            // struct template by name with TypeRefs recovered from each
            // array-literal element via the same uid-source shapes
            // __reify_type__ accepts. First MP5 piece — gives users runtime
            // type-level composition over Type values.
            if (nc.callee == "__type_apply__") {
                std::string tmpl_name;
                lir_view::ExprRef arr_ref;
                bool got_name = false;
                size_t arg_idx = 0;
                v.each_arg([&](lir_view::ExprRef ar) {
                    if (arg_idx == 0) {
                        if (ar && ar.kind() == lir_schema::expr::Code::LitStr) {
                            tmpl_name = std::string(
                                lir_view::ELitStrView{ar}.value());
                            got_name = true;
                        }
                    } else if (arg_idx == 1) {
                        if (ar) arr_ref = ar;
                    }
                    ++arg_idx;
                });
                if (!got_name) {
                    std::fprintf(stderr,
                        "mono.__type_apply__: name must be a string literal\n");
                    std::abort();
                }
                // LIT_STR value is stored with surrounding quote chars;
                // strip them so struct_name matches the canonical form.
                if (tmpl_name.size() >= 2 &&
                    tmpl_name.front() == '"' && tmpl_name.back() == '"')
                    tmpl_name = tmpl_name.substr(1, tmpl_name.size() - 2);
                // Follow VarRef → let-init for the args array too, so
                // `let xs = [...]; type_apply(name, xs)` works.
                for (int hops = 0; hops < 8 &&
                     arr_ref &&
                     arr_ref.kind() == lir_schema::expr::Code::VarRef; ++hops) {
                    std::string vn(lir_view::EVarRefView{arr_ref}.name());
                    auto it = type_let_inits_.find(vn);
                    if (it == type_let_inits_.end()) break;
                    arr_ref = it->second;
                }
                // Pack-splice fast path: if the args producer is a Type-array
                // intrinsic (`type_refs_of` / `args_of` / `field_types_of` /
                // `tuple_elems_of` / `typelist_tail`), pull element TypeRefs
                // directly from its type_args/struct-template instead of
                // requiring an ArrLit shape (mono folds these later).
                if (arr_ref &&
                    arr_ref.kind() == lir_schema::expr::Code::Call) {
                    lir_view::ECallView cv2{arr_ref};
                    auto cn2 = cv2.callee();
                    auto fold_pack = [&](std::vector<TypeRef>& out) -> bool {
                        auto tas = cv2.type_args(out_.type_pool.impl());
                        if (cn2 == "__type_refs_of__") {
                            // type-args ARE the pack (multiple args, one per member).
                            for (auto a : tas) out.push_back(subst_type(a, s));
                            return true;
                        }
                        if (cn2 == "__args_of__") {
                            if (tas.empty()) return false;
                            TypeRef T = subst_type(tas[0], s);
                            for (auto a : T.type_args()) out.push_back(a);
                            return true;
                        }
                        if (cn2 == "__typelist_tail__") {
                            if (tas.empty()) return false;
                            TypeRef T = subst_type(tas[0], s);
                            auto pack = T.type_args();
                            for (size_t i = 1; i < pack.size(); ++i)
                                out.push_back(pack[i]);
                            return true;
                        }
                        if (cn2 == "__tuple_elems_of__") {
                            if (tas.empty()) return false;
                            TypeRef T = subst_type(tas[0], s);
                            if (T && T.kind() == LogosType::Kind::Tuple)
                                for (auto a : T.tuple_elems())
                                    out.push_back(a);
                            return true;
                        }
                        return false;
                    };
                    std::vector<TypeRef> direct;
                    if (fold_pack(direct)) {
                        TypeRef inst_t = build_generic_struct_typeref(tmpl_name, direct);
                        collect_type_for_structs(inst_t);
                        uint64_t uid = type_hash_64bit(
                            type_hash_23(type_id_canon(inst_t)));
                        uid_to_type_[uid] = inst_t;
                        TypeRef type_t = rt_;
                        LirBuilder b(out_);
                        LogosTypeBuilder u32_b; u32_b.kind = LogosType::Kind::U32;
                        TypeRef u32_t = out_.type_pool.alloc(std::move(u32_b));
                        LogosTypeBuilder u8_b;  u8_b.kind  = LogosType::Kind::U8;
                        TypeRef u8_t  = out_.type_pool.alloc(std::move(u8_b));
                        LogosTypeBuilder slu8_b;
                        slu8_b.kind = LogosType::Kind::Slice;
                        slu8_b.elem = u8_t;
                        TypeRef slice_u8_t =
                            out_.type_pool.alloc(std::move(slu8_b));
                        LogosTypeBuilder i64_b; i64_b.kind = LogosType::Kind::I64;
                        TypeRef i64_t = out_.type_pool.alloc(std::move(i64_b));
                        LogosTypeBuilder u64_b; u64_b.kind = LogosType::Kind::U64;
                        TypeRef u64_t = out_.type_pool.alloc(std::move(u64_b));
                        auto kind_e = b.lit_int(
                            (int64_t)inst_t.kind(), u32_t);
                        auto name_e = b.lit_str(
                            type_str(inst_t), slice_u8_t);
                        auto size_e = b.size_of(inst_t, i64_t);
                        auto align_e = b.align_of(inst_t, i64_t);
                        auto uid_e  = b.lit_int(
                            (int64_t)(uint64_t)uid, u64_t);
                        std::vector<std::pair<std::string,
                            lir::LExprPtr>> f;
                        f.emplace_back("kind", std::move(kind_e));
                        f.emplace_back("name", std::move(name_e));
                        f.emplace_back("size", std::move(size_e));
                        f.emplace_back("align", std::move(align_e));
                        f.emplace_back("uid",  std::move(uid_e));
                        auto sl = b.struct_lit(type_t, std::move(f));
                        rt_ = type_t;
                        mp_ = sl.addr();
                        break;
                    }
                }
                if (!arr_ref ||
                    arr_ref.kind() != lir_schema::expr::Code::ArrLit) {
                    std::fprintf(stderr,
                        "mono.__type_apply__: args must be an array literal\n");
                    std::abort();
                }
                // Recover a TypeRef from a producer ExprRef — same shapes the
                // __reify_type__ intercept handles. Returns null on
                // unsupported shape. Follows VarRef → let-init at top.
                auto recover = [&](lir_view::ExprRef er) -> TypeRef {
                    if (!er) return {};
                    for (int hops = 0; hops < 8 &&
                         er &&
                         er.kind() == lir_schema::expr::Code::VarRef; ++hops) {
                        std::string vn(lir_view::EVarRefView{er}.name());
                        auto it = type_let_inits_.find(vn);
                        if (it == type_let_inits_.end()) break;
                        er = it->second;
                    }
                    if (er.kind() == lir_schema::expr::Code::Call) {
                        lir_view::ECallView cv{er};
                        auto cn = cv.callee();
                        if (cn == "__typelist_nth__" ||
                            cn == "__typelist_head__") {
                            auto tas = cv.type_args(out_.type_pool.impl());
                            if (tas.empty()) return {};
                            TypeRef L = subst_type(tas[0], s);
                            auto pack = L.type_args();
                            int64_t idx = 0;
                            if (cn == "__typelist_nth__") {
                                bool got = false;
                                cv.each_arg([&](lir_view::ExprRef ar2) {
                                    if (got) return;
                                    if (ar2 && ar2.kind() ==
                                              lir_schema::expr::Code::LitInt) {
                                        idx = lir_view::ELitIntView{ar2}.value();
                                        got = true;
                                    }
                                });
                            }
                            if (idx < 0 || (size_t)idx >= pack.size())
                                return {};
                            return pack[idx];
                        }
                    }
                    if (er.kind() == lir_schema::expr::Code::StructLit) {
                        TypeRef found{};
                        lir_view::EStructLitView{er}.each_field(
                            [&](std::string_view fname,
                                lir_view::ExprRef fer) {
                                if (found || fname != "uid") return;
                                if (fer && fer.kind() ==
                                          lir_schema::expr::Code::Call) {
                                    lir_view::ECallView cv{fer};
                                    if (cv.callee() == "__type_uid_of__") {
                                        auto tas = cv.type_args(
                                            out_.type_pool.impl());
                                        if (!tas.empty())
                                            found = subst_type(tas[0], s);
                                    }
                                }
                            });
                        return found;
                    }
                    return {};
                };
                std::vector<TypeRef> targs;
                lir_view::EArrLitView av{arr_ref};
                uint64_t cnt = av.count();
                for (uint64_t i = 0; i < cnt; ++i) {
                    auto er = av.elem(i);
                    TypeRef ti = recover(er);
                    if (!ti) {
                        std::fprintf(stderr,
                            "mono.__type_apply__: arg[%llu] is not a recognized "
                            "Type producer\n", (unsigned long long)i);
                        std::abort();
                    }
                    targs.push_back(ti);
                }
                // Pkg threads from the template definition (M0.4 follow-up)
                // so metacall-instantiated specs share UID/registry keys.
                TypeRef inst_t = build_generic_struct_typeref(tmpl_name, targs);
                collect_type_for_structs(inst_t);
                uint64_t uid = type_hash_64bit(type_hash_23(type_id_canon(inst_t)));
                uid_to_type_[uid] = inst_t;
                TypeRef type_t = rt_;
                LogosTypeBuilder u32_b; u32_b.kind = LogosType::Kind::U32;
                TypeRef u32_t = out_.type_pool.alloc(std::move(u32_b));
                LogosTypeBuilder u8_b;  u8_b.kind  = LogosType::Kind::U8;
                TypeRef u8_t  = out_.type_pool.alloc(std::move(u8_b));
                LogosTypeBuilder sl_b;  sl_b.kind  = LogosType::Kind::Slice;
                sl_b.elem = u8_t;
                TypeRef slice_u8_t = out_.type_pool.alloc(std::move(sl_b));
                LogosTypeBuilder i64_b; i64_b.kind = LogosType::Kind::I64;
                TypeRef i64_t = out_.type_pool.alloc(std::move(i64_b));
                LogosTypeBuilder u64_b; u64_b.kind = LogosType::Kind::U64;
                TypeRef u64_t = out_.type_pool.alloc(std::move(u64_b));
                LirBuilder b(out_);
                std::vector<std::pair<std::string, lir::LExprPtr>> f;
                f.emplace_back("kind", b.lit_int((int64_t)inst_t.kind(), u32_t));
                f.emplace_back("name", b.lit_str(type_str(inst_t), slice_u8_t));
                f.emplace_back("size", b.size_of(inst_t, i64_t));
                f.emplace_back("align", b.align_of(inst_t, i64_t));
                f.emplace_back("uid",  b.lit_int((int64_t)uid, u64_t));
                auto sl = b.struct_lit(type_t, std::move(f));
                rt_ = type_t;
                mp_ = sl.addr();
                break;
            }
            // __apply_generic__(g: Type, args: [Type; N]) — instantiate
            // generic constructor `g` (produced by generic_of) with `args`.
            // Recovers the constructor name from g's StructLit "name" field
            // (a LitStr), then routes through the same Struct allocation as
            // __type_apply__.
            if (nc.callee == "__apply_generic__") {
                lir_view::ExprRef g_ref;
                lir_view::ExprRef arr_ref;
                size_t arg_idx = 0;
                v.each_arg([&](lir_view::ExprRef ar) {
                    if (arg_idx == 0) g_ref = ar;
                    else if (arg_idx == 1) arr_ref = ar;
                    ++arg_idx;
                });
                auto chase = [&](lir_view::ExprRef er) {
                    for (int hops = 0; hops < 8 &&
                         er &&
                         er.kind() == lir_schema::expr::Code::VarRef; ++hops) {
                        std::string vn(lir_view::EVarRefView{er}.name());
                        auto it = type_let_inits_.find(vn);
                        if (it == type_let_inits_.end()) break;
                        er = it->second;
                    }
                    return er;
                };
                g_ref = chase(g_ref);
                arr_ref = chase(arr_ref);
                std::string tmpl_name;
                if (g_ref && g_ref.kind() == lir_schema::expr::Code::StructLit) {
                    lir_view::EStructLitView{g_ref}.each_field(
                        [&](std::string_view fname,
                            lir_view::ExprRef fer) {
                            if (!tmpl_name.empty() || fname != "name") return;
                            if (fer && fer.kind() ==
                                      lir_schema::expr::Code::LitStr) {
                                tmpl_name = std::string(
                                    lir_view::ELitStrView{fer}.value());
                                if (tmpl_name.size() >= 2 &&
                                    tmpl_name.front() == '"' &&
                                    tmpl_name.back() == '"')
                                    tmpl_name = tmpl_name.substr(
                                        1, tmpl_name.size() - 2);
                            }
                        });
                }
                if (tmpl_name.empty()) {
                    std::fprintf(stderr,
                        "mono.__apply_generic__: g must be a generic_of value\n");
                    std::abort();
                }
                if (!arr_ref || arr_ref.kind() !=
                        lir_schema::expr::Code::ArrLit) {
                    std::fprintf(stderr,
                        "mono.__apply_generic__: args must be an array literal\n");
                    std::abort();
                }
                auto recover = [&](lir_view::ExprRef er) -> TypeRef {
                    er = chase(er);
                    if (!er) return {};
                    if (er.kind() == lir_schema::expr::Code::Call) {
                        lir_view::ECallView cv{er};
                        auto cn = cv.callee();
                        if (cn == "__typelist_nth__" ||
                            cn == "__typelist_head__") {
                            auto tas = cv.type_args(out_.type_pool.impl());
                            if (tas.empty()) return {};
                            TypeRef L = subst_type(tas[0], s);
                            auto pack = L.type_args();
                            int64_t idx = 0;
                            if (cn == "__typelist_nth__") {
                                bool got = false;
                                cv.each_arg([&](lir_view::ExprRef ar2) {
                                    if (got) return;
                                    if (ar2 && ar2.kind() ==
                                              lir_schema::expr::Code::LitInt) {
                                        idx = lir_view::ELitIntView{ar2}.value();
                                        got = true;
                                    }
                                });
                            }
                            if (idx < 0 || (size_t)idx >= pack.size())
                                return {};
                            return pack[idx];
                        }
                    }
                    if (er.kind() == lir_schema::expr::Code::StructLit) {
                        TypeRef found{};
                        lir_view::EStructLitView{er}.each_field(
                            [&](std::string_view fname,
                                lir_view::ExprRef fer) {
                                if (found || fname != "uid") return;
                                if (fer && fer.kind() ==
                                          lir_schema::expr::Code::Call) {
                                    lir_view::ECallView cv{fer};
                                    if (cv.callee() == "__type_uid_of__") {
                                        auto tas = cv.type_args(
                                            out_.type_pool.impl());
                                        if (!tas.empty())
                                            found = subst_type(tas[0], s);
                                    }
                                }
                            });
                        return found;
                    }
                    return {};
                };
                std::vector<TypeRef> targs;
                lir_view::EArrLitView av{arr_ref};
                uint64_t cnt = av.count();
                for (uint64_t i = 0; i < cnt; ++i) {
                    TypeRef ti = recover(av.elem(i));
                    if (!ti) {
                        std::fprintf(stderr,
                            "mono.__apply_generic__: arg[%llu] not a "
                            "Type producer\n", (unsigned long long)i);
                        std::abort();
                    }
                    targs.push_back(ti);
                }
                // Pkg threads from the template definition (M0.4 follow-up)
                // so metacall-instantiated specs share UID/registry keys.
                TypeRef inst_t = build_generic_struct_typeref(tmpl_name, targs);
                collect_type_for_structs(inst_t);
                uint64_t uid = type_hash_64bit(type_hash_23(type_id_canon(inst_t)));
                uid_to_type_[uid] = inst_t;
                TypeRef type_t = rt_;
                LogosTypeBuilder u32_b; u32_b.kind = LogosType::Kind::U32;
                TypeRef u32_t = out_.type_pool.alloc(std::move(u32_b));
                LogosTypeBuilder u8_b;  u8_b.kind  = LogosType::Kind::U8;
                TypeRef u8_t  = out_.type_pool.alloc(std::move(u8_b));
                LogosTypeBuilder sl_b;  sl_b.kind  = LogosType::Kind::Slice;
                sl_b.elem = u8_t;
                TypeRef slice_u8_t = out_.type_pool.alloc(std::move(sl_b));
                LogosTypeBuilder i64_b; i64_b.kind = LogosType::Kind::I64;
                TypeRef i64_t = out_.type_pool.alloc(std::move(i64_b));
                LogosTypeBuilder u64_b; u64_b.kind = LogosType::Kind::U64;
                TypeRef u64_t = out_.type_pool.alloc(std::move(u64_b));
                LirBuilder b(out_);
                std::vector<std::pair<std::string, lir::LExprPtr>> f;
                f.emplace_back("kind", b.lit_int((int64_t)inst_t.kind(), u32_t));
                f.emplace_back("name", b.lit_str(type_str(inst_t), slice_u8_t));
                f.emplace_back("size", b.size_of(inst_t, i64_t));
                f.emplace_back("align", b.align_of(inst_t, i64_t));
                f.emplace_back("uid",  b.lit_int((int64_t)uid, u64_t));
                auto sl = b.struct_lit(type_t, std::move(f));
                rt_ = type_t;
                mp_ = sl.addr();
                break;
            }
            // __tuple_type_apply__([Type; N]) and __array_type_apply__(Type, N) —
            // tuple/array sibling forms of __type_apply__, used by quote_ty!
            // antiquot lowering for `($t1, ...)` and `[$t; N]` shapes.
            if (nc.callee == "__tuple_type_apply__" ||
                nc.callee == "__array_type_apply__") {
                bool is_tuple = (nc.callee == "__tuple_type_apply__");
                lir_view::ExprRef a0_ref;
                lir_view::ExprRef a1_ref;
                size_t arg_idx = 0;
                v.each_arg([&](lir_view::ExprRef ar) {
                    if (arg_idx == 0) a0_ref = ar;
                    else if (arg_idx == 1) a1_ref = ar;
                    ++arg_idx;
                });
                auto chase = [&](lir_view::ExprRef er) {
                    for (int hops = 0; hops < 8 &&
                         er &&
                         er.kind() == lir_schema::expr::Code::VarRef; ++hops) {
                        std::string vn(lir_view::EVarRefView{er}.name());
                        auto it = type_let_inits_.find(vn);
                        if (it == type_let_inits_.end()) break;
                        er = it->second;
                    }
                    return er;
                };
                auto recover = [&](lir_view::ExprRef er) -> TypeRef {
                    er = chase(er);
                    if (!er) return {};
                    if (er.kind() == lir_schema::expr::Code::Call) {
                        lir_view::ECallView cv{er};
                        auto cn = cv.callee();
                        if (cn == "__typelist_nth__" ||
                            cn == "__typelist_head__") {
                            auto tas = cv.type_args(out_.type_pool.impl());
                            if (tas.empty()) return {};
                            TypeRef L = subst_type(tas[0], s);
                            auto pack = L.type_args();
                            int64_t idx = 0;
                            if (cn == "__typelist_nth__") {
                                bool got = false;
                                cv.each_arg([&](lir_view::ExprRef ar2) {
                                    if (got) return;
                                    if (ar2 && ar2.kind() ==
                                              lir_schema::expr::Code::LitInt) {
                                        idx = lir_view::ELitIntView{ar2}.value();
                                        got = true;
                                    }
                                });
                            }
                            if (idx < 0 || (size_t)idx >= pack.size())
                                return {};
                            return pack[idx];
                        }
                    }
                    if (er.kind() == lir_schema::expr::Code::StructLit) {
                        TypeRef found{};
                        lir_view::EStructLitView{er}.each_field(
                            [&](std::string_view fname,
                                lir_view::ExprRef fer) {
                                if (found || fname != "uid") return;
                                if (fer && fer.kind() ==
                                          lir_schema::expr::Code::Call) {
                                    lir_view::ECallView cv{fer};
                                    if (cv.callee() == "__type_uid_of__") {
                                        auto tas = cv.type_args(
                                            out_.type_pool.impl());
                                        if (!tas.empty())
                                            found = subst_type(tas[0], s);
                                    }
                                }
                            });
                        return found;
                    }
                    return {};
                };
                TypeRef inst_t;
                if (is_tuple) {
                    auto arr_ref = chase(a0_ref);
                    if (!arr_ref || arr_ref.kind() !=
                            lir_schema::expr::Code::ArrLit) {
                        std::fprintf(stderr,
                            "mono.__tuple_type_apply__: args must be array literal\n");
                        std::abort();
                    }
                    std::vector<TypeRef> elems;
                    lir_view::EArrLitView av{arr_ref};
                    uint64_t cnt = av.count();
                    for (uint64_t i = 0; i < cnt; ++i) {
                        TypeRef ti = recover(av.elem(i));
                        if (!ti) {
                            std::fprintf(stderr,
                                "mono.__tuple_type_apply__: arg[%llu] not a "
                                "Type producer\n", (unsigned long long)i);
                            std::abort();
                        }
                        elems.push_back(ti);
                    }
                    LogosTypeBuilder tb;
                    tb.kind = LogosType::Kind::Tuple;
                    tb.tuple_elems = std::move(elems);
                    inst_t = out_.type_pool.alloc(std::move(tb));
                } else {
                    TypeRef et = recover(a0_ref);
                    if (!et) {
                        std::fprintf(stderr,
                            "mono.__array_type_apply__: elem not a Type producer\n");
                        std::abort();
                    }
                    auto sz_ref = chase(a1_ref);
                    if (!sz_ref || sz_ref.kind() !=
                            lir_schema::expr::Code::LitInt) {
                        std::fprintf(stderr,
                            "mono.__array_type_apply__: size must be literal int\n");
                        std::abort();
                    }
                    int64_t n = lir_view::ELitIntView{sz_ref}.value();
                    LogosTypeBuilder ab;
                    ab.kind = LogosType::Kind::Array;
                    ab.elem = et;
                    ab.arr_size = (uint64_t)n;
                    inst_t = out_.type_pool.alloc(std::move(ab));
                }
                collect_type_for_structs(inst_t);
                uint64_t uid = type_hash_64bit(type_hash_23(type_id_canon(inst_t)));
                uid_to_type_[uid] = inst_t;
                TypeRef type_t = rt_;
                LogosTypeBuilder u32_b; u32_b.kind = LogosType::Kind::U32;
                TypeRef u32_t = out_.type_pool.alloc(std::move(u32_b));
                LogosTypeBuilder u8_b;  u8_b.kind  = LogosType::Kind::U8;
                TypeRef u8_t  = out_.type_pool.alloc(std::move(u8_b));
                LogosTypeBuilder sl_b;  sl_b.kind  = LogosType::Kind::Slice;
                sl_b.elem = u8_t;
                TypeRef slice_u8_t = out_.type_pool.alloc(std::move(sl_b));
                LogosTypeBuilder i64_b; i64_b.kind = LogosType::Kind::I64;
                TypeRef i64_t = out_.type_pool.alloc(std::move(i64_b));
                LogosTypeBuilder u64_b; u64_b.kind = LogosType::Kind::U64;
                TypeRef u64_t = out_.type_pool.alloc(std::move(u64_b));
                LirBuilder b(out_);
                std::vector<std::pair<std::string, lir::LExprPtr>> f;
                f.emplace_back("kind", b.lit_int((int64_t)inst_t.kind(), u32_t));
                f.emplace_back("name", b.lit_str(type_str(inst_t), slice_u8_t));
                f.emplace_back("size", b.size_of(inst_t, i64_t));
                f.emplace_back("align", b.align_of(inst_t, i64_t));
                f.emplace_back("uid",  b.lit_int((int64_t)uid, u64_t));
                auto sl = b.struct_lit(type_t, std::move(f));
                rt_ = type_t;
                mp_ = sl.addr();
                break;
            }
            // variant_count_of::<E>() / variant_names_of::<E>() /
            // variant_payload_counts_of::<E>() /
            // variant_payload_types_flat_of::<E>() — enum decompose. All
            // four take E in nc.type_args[0]; non-enum or unknown E yields
            // 0 / empty arrays.
            if (nc.callee == "__variant_count_of__" ||
                nc.callee == "__variant_names_of__" ||
                nc.callee == "__variant_payload_counts_of__" ||
                nc.callee == "__variant_payload_types_flat_of__") {
                std::optional<lir_view::EnumView> edef;
                if (!nc.type_args.empty()) {
                    TypeRef E = nc.type_args[0];
                    if (E && E.kind() == LogosType::Kind::Enum) {
                        std::string base{E.enum_name()};
                        for (auto& ed : in_.enums)
                            if (ed.name() == base) { edef = ed; break; }
                    }
                }
                LirBuilder b(out_);
                auto* edef_pool = out_.type_pool.impl();
                if (nc.callee == "__variant_count_of__") {
                    int64_t n = edef ? (int64_t)edef->variant_count() : 0;
                    LogosTypeBuilder i64_b; i64_b.kind = LogosType::Kind::I64;
                    TypeRef i64_t = out_.type_pool.alloc(std::move(i64_b));
                    auto lit = b.lit_int(n, i64_t);
                    rt_ = i64_t;
                    mp_ = lit.addr();
                    break;
                }
                TypeRef elem_t = rt_ ? rt_.elem() : nullptr;
                std::vector<lir_view::ExprRef> elems;
                if (nc.callee == "__variant_names_of__") {
                    if (edef)
                        edef->each_variant([&](lir_view::EnumVariantView vr) {
                            elems.push_back(b.lit_str(std::string(vr.name()), elem_t));
                        });
                } else if (nc.callee == "__variant_payload_counts_of__") {
                    if (edef)
                        edef->each_variant([&](lir_view::EnumVariantView vr) {
                            elems.push_back(b.lit_int(
                                (int64_t)vr.payload_types(edef_pool).size(), elem_t));
                        });
                } else { // __variant_payload_types_flat_of__
                    if (edef) {
                        LogosTypeBuilder u32_b; u32_b.kind = LogosType::Kind::U32;
                        TypeRef u32_t = out_.type_pool.alloc(std::move(u32_b));
                        LogosTypeBuilder u8_b; u8_b.kind = LogosType::Kind::U8;
                        TypeRef u8_pt = out_.type_pool.alloc(std::move(u8_b));
                        LogosTypeBuilder sl_b; sl_b.kind = LogosType::Kind::Slice;
                        sl_b.elem = u8_pt;
                        TypeRef slice_u8_t = out_.type_pool.alloc(std::move(sl_b));
                        LogosTypeBuilder i64_b; i64_b.kind = LogosType::Kind::I64;
                        TypeRef i64_t = out_.type_pool.alloc(std::move(i64_b));
                        LogosTypeBuilder u64_b; u64_b.kind = LogosType::Kind::U64;
                        TypeRef u64_t = out_.type_pool.alloc(std::move(u64_b));
                        edef->each_variant([&](lir_view::EnumVariantView vr) {
                            vr.each_payload_type(edef_pool, [&](TypeRef pt) {
                                TypeRef pty = subst_type(pt, s);
                                if (!pty) pty = pt;
                                uint64_t uid = type_hash_64bit(
                                    type_hash_23(type_id_canon(pty)));
                                uid_to_type_[uid] = pty;
                                std::vector<std::pair<std::string,
                                    lir::LExprPtr>> f;
                                f.emplace_back("kind",
                                    b.lit_int((int64_t)pty.kind(), u32_t));
                                f.emplace_back("name",
                                    b.lit_str(type_str(pty), slice_u8_t));
                                f.emplace_back("size",
                                    b.size_of(pty, i64_t));
                                f.emplace_back("align",
                                    b.align_of(pty, i64_t));
                                f.emplace_back("uid",
                                    b.lit_int((int64_t)uid, u64_t));
                                elems.push_back(
                                    b.struct_lit(elem_t, std::move(f)));
                            });
                        });
                    }
                }
                LogosTypeBuilder ab; ab.kind = LogosType::Kind::Array;
                ab.elem = elem_t;
                ab.arr_size = (int64_t)elems.size();
                TypeRef new_arr_t = out_.type_pool.alloc(std::move(ab));
                rt_ = new_arr_t;
                mp_ =
                    lir_mirror_emit_arr_lit(out_, new_arr_t, elems);
                break;
            }
            // __tuple_all_eq__::<T>(a, b) — variadic-tuple Eq chain
            // expansion. Sema emits this placeholder when T is unbound
            // (e.g. T = (A...) in a variadic impl body). Mono receives
            // it with T already pack-substituted to the concrete tuple.
            // Emit the `&&`-chain of `a.{i}.eq(&b.{i})` per element.
            //
            // Args (a/b) are populated AFTER this branch in the each_arg
            // pass below. To get them here we re-walk v.each_arg into a
            // local vector.
            if (nc.callee == "__tuple_all_eq__") {
                TypeRef T = nc.type_args.empty() ? TypeRef{} : nc.type_args[0];
                LirBuilder lb(out_);
                LogosTypeBuilder bb; bb.kind = LogosType::Kind::Bool;
                TypeRef bool_t = out_.type_pool.alloc(std::move(bb));
                std::vector<lir::LExprPtr> sub_args;
                v.each_arg([&](lir_view::ExprRef ar) {
                    sub_args.push_back(child_husk(subst_child_expr(ar)));
                });
                if (!T || T.kind() != LogosType::Kind::Tuple ||
                    sub_args.size() < 2) {
                    auto lit = lb.lit_bool(true, bool_t);
                    rt_ = bool_t;
                    mp_ = lit.addr();
                    break;
                }
                // Recursive chain builder: handles nested tuple elements
                // by inlining the inner chain rather than emitting another
                // `__tuple_all_eq__` call (mono doesn't re-process emitted
                // intrinsics, so deferred re-entry would leave an
                // unresolved symbol).
                std::function<lir::LExprPtr(TypeRef, lir::LExprPtr, lir::LExprPtr)>
                build_chain = [&](TypeRef TT,
                                   lir::LExprPtr a_r,
                                   lir::LExprPtr b_r) -> lir::LExprPtr {
                    auto es = TT.tuple_elems();
                    if (es.empty()) return lb.lit_bool(true, bool_t);
                    lir::LExprPtr ch = nullptr;
                    for (size_t i = 0; i < es.size(); ++i) {
                        TypeRef et = es[i];
                        auto a_f = lb.tuple_index(a_r, (uint32_t)i, et);
                        auto b_f = lb.tuple_index(b_r, (uint32_t)i, et);
                        LogosTypeBuilder rb;
                        rb.kind    = LogosType::Kind::Ref;
                        rb.pointee = et;
                        TypeRef et_ref = out_.type_pool.alloc(std::move(rb));
                        lir::LExprPtr cmp = nullptr;
                        if (et.kind() == LogosType::Kind::Ptr) {
                            // A raw pointer compares by ADDRESS (Rust's
                            // `impl PartialEq for *const T`): the builtin `==`.
                            cmp = lb.bin_op("==", a_f, b_f, bool_t);
                        } else if (et.kind() == LogosType::Kind::Tuple) {
                            // Nested — inline the inner chain. Use the
                            // field refs as the new receivers.
                            auto inner_a_ref = lb.addr_of_temp(a_f, false, et_ref, lir_schema::expr::BorrowOrigin::Desugar);
                            auto inner_b_ref = lb.addr_of_temp(b_f, false, et_ref, lir_schema::expr::BorrowOrigin::Desugar);
                            cmp = build_chain(et, inner_a_ref, inner_b_ref);
                        } else {
                            // Primitive / user-struct / slice element: resolve
                            // `eq` for THIS element type by SIGNATURE — a
                            // function whose method base is `eq` and whose two
                            // parameters are the element type (by value for a
                            // slice, by shared reference otherwise).
                            //
                            // #438: this used to scan function NAMES for the
                            // `<T>__eq__f__` prefix and, when the scan found
                            // nothing, invent the callee `<T>__eq` — a symbol no
                            // declaration carries. Measured over 120 corpus
                            // compiles: 1156 of the old borrow checker's
                            // bare-name fallbacks were exactly these invented
                            // names. A miss is now a diagnostic, not a name.
                            std::string callee_sym = eq_instance_for(et, et_ref);
                            // No declaration with that signature: the element's
                            // `Eq` impl (`&T`'s at `&i32`, a generic one) —
                            // called with both sides borrowed, its `&self` exactly.
                            bool via_impl = false;
                            if (callee_sym.empty() && et.kind() != LogosType::Kind::Slice) {
                                callee_sym = trait_item_symbol_("logos.lang.cmp::Eq", et, "eq", 2);
                                via_impl = !callee_sym.empty();
                            }
                            if (callee_sym.empty()) {
                                in_.diags.diags.push_back({Diag::Level::Error, "mono",
                                    std::format("tuple equality: no `eq` implementation "
                                                "for element type '{}'", type_str(et)), diag_file_, diag_line_});
                                return nullptr;
                            }
                            // For Slice elems (str), pass by-value — the
                            // impl signature is `(slice, slice)`, not
                            // `(&str, &str)`. mlir-gen's primitive-receiver
                            // fast-path doesn't apply (slice receiver).
                            // Use a direct func call instead of method_call.
                            if (et.kind() == LogosType::Kind::Slice) {
                                std::vector<lir::LExprPtr> dargs;
                                dargs.push_back(a_f);
                                dargs.push_back(b_f);
                                cmp = lb.call(callee_sym, {}, dargs, bool_t);
                            } else {
                                auto b_f_ref = lb.addr_of_temp(b_f, false, et_ref, lir_schema::expr::BorrowOrigin::Desugar);
                                if (et.kind() == LogosType::Kind::Enum || via_impl) {
                                    // An ENUM element (`Option<i64>`): `eq(&self,
                                    // &other)` called directly with both sides
                                    // borrowed — a by-value enum receiver on the
                                    // method-call path lowers to no value.
                                    auto a_f_ref = lb.addr_of_temp(a_f, false, et_ref,
                                                                   lir_schema::expr::BorrowOrigin::Desugar);
                                    cmp = lb.call(callee_sym, {}, {a_f_ref, b_f_ref}, bool_t);
                                } else {
                                std::vector<lir::LExprPtr> margs;
                                margs.push_back(b_f_ref);
                                cmp = lb.method_call(a_f, "eq", callee_sym, {},
                                                      margs, -1, bool_t);
                                }
                            }
                        }
                        ch = ch ? lb.bin_op("&&", ch, cmp, bool_t) : cmp;
                    }
                    return ch;
                };
                auto chain = build_chain(T, sub_args[0], sub_args[1]);
                rt_ = bool_t;
                mp_ = chain.addr();
                break;
            }
            // __tuple_each_field_debug__::<T>(self, f) — expand the variadic
            // tuple Debug impl into a chain of free-fn calls over the
            // step-helpers (fmt_tuple_open/sep/close + fmt_seq) interleaved
            // with each field's `fmt`. Mirrors __tuple_all_eq__ but produces a
            // `fmt_seq`-combined Result chain instead of an `&&` bool chain.
            if (nc.callee == "__tuple_each_field_debug__") {
                TypeRef T = nc.type_args.empty() ? TypeRef{} : nc.type_args[0];
                LirBuilder lb(out_);
                TypeRef res_t = rt_;  // Result<(), Error>
                std::vector<lir::LExprPtr> sub_args;
                v.each_arg([&](lir_view::ExprRef ar) {
                    sub_args.push_back(child_husk(subst_child_expr(ar)));
                });
                auto resolve_fn = [&](const std::string& base) -> std::string {
                    std::string prefix = base + "__f__";
                    auto has = [&](const std::string& full) {
                        size_t p = full.find(prefix);
                        // ⚠ SEPARATOR CLASS. The head boundary was tested as
                        // `full[p-1] == '.'` alone — but a FREE fn's link name
                        // is composed by `sym::mangle` as `<pkg>$<base>`, not
                        // `<pkg>.<base>`. So `fmt_seq` NEVER resolved: the
                        // expansion emitted a call to the bare fallback name,
                        // mlir-gen found no such function, and the whole
                        // `return tuple_each_field_debug…` chain lowered to
                        // NOTHING — an 8-tuple's Debug::fmt fell off its end
                        // and returned garbage, in the SHIPPED stdlib archive,
                        // with the compile at exit 0. Both composed separators
                        // are boundaries; neither alone is.
                        return p != std::string::npos &&
                               (p == 0 || full[p - 1] == '.' || full[p - 1] == '$');
                    };
                    for (auto& fn : out_.functions) if (has(std::string(fn.name()))) return std::string(fn.name());
                    for (auto& fn : in_.functions)  if (has(std::string(fn.name()))) return std::string(fn.name());
                    return base;
                };
                std::string seq_sym    = resolve_fn("fmt_seq");
                std::string open_sym   = resolve_fn("fmt_tuple_open");
                std::string sep_sym    = resolve_fn("fmt_tuple_sep");
                std::string close_sym  = resolve_fn("fmt_tuple_close");
                std::string close1_sym = resolve_fn("fmt_tuple_close1");
                // Resolve `<elem>__Debug__fmt` (trait-qualified, since fmt
                // collides across the fmt-family traits) or plain `<elem>__fmt`.
                auto resolve_dbg = [&](const std::string& en) -> std::string {
                    for (const char* infix : {"__Debug__fmt__f__", "__fmt__f__"}) {
                        std::string prefix = en + infix;
                        auto has = [&](const std::string& full) {
                            size_t p = full.find(prefix);
                            // `$` is a composed head boundary too — see
                            // resolve_fn above.
                            return p != std::string::npos &&
                                   (p == 0 || full[p - 1] == '.' || full[p - 1] == '$');
                        };
                        for (auto& fn : out_.functions) if (has(std::string(fn.name()))) return std::string(fn.name());
                        for (auto& fn : in_.functions)  if (has(std::string(fn.name()))) return std::string(fn.name());
                    }
                    return en + "__fmt";
                };
                if (!T || T.kind() != LogosType::Kind::Tuple || sub_args.size() < 2) {
                    auto cl = lb.call(close_sym, {},
                        { sub_args.empty() ? nullptr : sub_args.back() }, res_t);
                    rt_ = res_t;
                    mp_ = cl.addr();
                    break;
                }
                auto self_r = sub_args[0];
                auto f_r    = sub_args[1];
                // f_r is `&mut Formatter` reused at every call below — each
                // reuse must reborrow (borrow_check treats `&mut T` as a
                // move-type, so a bare reuse consumes f_r on call #1 and
                // rejects calls #2..#N). `lb.reuse_mut_ref` wraps as
                // `AddrOfTemp(Deref(f_r))` per call; `f_r` stays usable
                // across the chain as the canonical source. Any future
                // synthesizer that reuses a `&mut T` reaches for the same
                // helper, so the pattern can't be missed silently.
                std::function<lir::LExprPtr(TypeRef, lir::LExprPtr)>
                build = [&](TypeRef TT, lir::LExprPtr selfr) -> lir::LExprPtr {
                    auto es = TT.tuple_elems();
                    size_t n = es.size();
                    lir::LExprPtr chain = lb.call(open_sym, {}, { lb.reuse_mut_ref(f_r) }, res_t);
                    for (size_t i = 0; i < n; ++i) {
                        if (i > 0) {
                            auto sep = lb.call(sep_sym, {}, { lb.reuse_mut_ref(f_r) }, res_t);
                            chain = lb.call(seq_sym, {}, { chain, sep }, res_t);
                        }
                        TypeRef et = es[i];
                        auto field = lb.tuple_index(selfr, (uint32_t)i, et);
                        lir::LExprPtr fld = nullptr;
                        if (et.kind() == LogosType::Kind::Tuple) {
                            LogosTypeBuilder rb; rb.kind = LogosType::Kind::Ref; rb.pointee = et;
                            TypeRef et_ref = out_.type_pool.alloc(std::move(rb));
                            auto inner = lb.addr_of_temp(field, false, et_ref, lir_schema::expr::BorrowOrigin::Desugar);
                            fld = build(et, inner);
                        } else {
                            std::string en = type_str(et);
                            if (et.kind() == LogosType::Kind::Slice &&
                                et.elem() && et.elem().kind() == LogosType::Kind::U8)
                                en = "str";
                            std::string sym = resolve_dbg(en);
                            if (et.kind() != LogosType::Kind::Array && sym != en + "__fmt") {
                                // By-value receiver; mlir-gen's primitive-receiver
                                // fast-path spills it for the `&self` param.
                                fld = lb.method_call(field, "fmt", sym, {}, { lb.reuse_mut_ref(f_r) }, -1, res_t);
                            } else {
                                // No `<name>__fmt` instance to name (an array, an
                                // enum over one, …): call the generic dispatcher
                                // `fmt_debug::<E>(&field, fmt)`, which reaches the
                                // impl through ordinary dispatch.
                                std::string tmpl;
                                for (auto& [k, _] : templates_) {
                                    auto q = k.find("fmt_debug");
                                    if (q == std::string::npos || (q > 0 && k[q - 1] != '.' && k[q - 1] != '$')) continue;
                                    auto tail = std::string_view(k).substr(q + 9);
                                    if (tail.empty() || tail.starts_with("__g__") || tail.starts_with("__f__")) { tmpl = k; break; }
                                }
                                LogosTypeBuilder rb; rb.kind = LogosType::Kind::Ref; rb.pointee = et;
                                TypeRef et_ref = out_.type_pool.alloc(std::move(rb));
                                auto fref = lb.addr_of_temp(field, false, et_ref, lir_schema::expr::BorrowOrigin::Desugar);
                                std::vector<TypeRef> targs{et};
                                std::string callee = tmpl.empty() ? std::string("fmt_debug") : mangle(tmpl, targs);
                                if (!tmpl.empty()) enqueue_if_needed(callee, targs);
                                fld = lb.call(callee, targs, { fref, lb.reuse_mut_ref(f_r) }, res_t);
                            }
                        }
                        chain = lb.call(seq_sym, {}, { chain, fld }, res_t);
                    }
                    auto cl = lb.call(n == 1 ? close1_sym : close_sym, {}, { lb.reuse_mut_ref(f_r) }, res_t);
                    chain = lb.call(seq_sym, {}, { chain, cl }, res_t);
                    return chain;
                };
                auto chain = build(T, self_r);
                rt_ = res_t;
                mp_ = chain.addr();
                break;
            }
            // tuple_count_of::<T>() — emit lit_int N for tuple T, 0 otherwise.
            if (nc.callee == "__tuple_count_of__") {
                int64_t n = 0;
                if (!nc.type_args.empty()) {
                    TypeRef T = nc.type_args[0];
                    if (T && T.kind() == LogosType::Kind::Tuple)
                        n = (int64_t)T.tuple_elems().size();
                }
                LirBuilder b(out_);
                LogosTypeBuilder i64_b; i64_b.kind = LogosType::Kind::I64;
                TypeRef i64_t = out_.type_pool.alloc(std::move(i64_b));
                auto lit = b.lit_int(n, i64_t);
                rt_ = i64_t;
                mp_ = lit.addr();
                break;
            }
            // field_count_of::<T>() — emit lit_int N for struct T (0 for
            // non-struct or unknown-struct T). Same template lookup as the
            // field_*_of intrinsics below.
            if (nc.callee == "__field_count_of__") {
                int64_t n = 0;
                if (!nc.type_args.empty()) {
                    TypeRef T = nc.type_args[0];
                    if (T && (T.kind() == LogosType::Kind::Struct ||
                              T.kind() == LogosType::Kind::ZonedStruct)) {
                        std::string base{T.struct_name()};
                        std::string tpkg{T.pkg_name()};
                        lir_view::StructView match;
                        for (auto& sd : in_.structs)
                            if (sd.name() == base &&
                                (tpkg.empty() || sd.pkg() == tpkg)) {
                                match = sd; break;
                            }
                        if (!match.valid())
                            for (auto& sd : in_.structs)
                                if (sd.name() == base) { match = sd; break; }
                        if (match.valid()) n = (int64_t)match.fields().size();
                    }
                }
                LirBuilder b(out_);
                LogosTypeBuilder i64_b; i64_b.kind = LogosType::Kind::I64;
                TypeRef i64_t = out_.type_pool.alloc(std::move(i64_b));
                auto lit = b.lit_int(n, i64_t);
                rt_ = i64_t;
                mp_ = lit.addr();
                break;
            }
            // field_names_of::<T>() — emit [&[u8]; N] of struct field names.
            // For non-struct or unknown-struct T → empty array.
            if (nc.callee == "__field_names_of__") {
                std::vector<std::string> field_names;
                if (!nc.type_args.empty()) {
                    TypeRef T = nc.type_args[0];
                    if (T && (T.kind() == LogosType::Kind::Struct ||
                              T.kind() == LogosType::Kind::ZonedStruct)) {
                        std::string base{T.struct_name()};
                        std::string tpkg{T.pkg_name()};
                        lir_view::StructView tmpl;
                        for (auto& sd : in_.structs)
                            if (sd.name() == base &&
                                (tpkg.empty() || sd.pkg() == tpkg)) {
                                tmpl = sd; break;
                            }
                        if (!tmpl.valid())
                            for (auto& sd : in_.structs)
                                if (sd.name() == base) { tmpl = sd; break; }
                        if (tmpl.valid())
                            for (auto f : tmpl.fields())
                                field_names.push_back(std::string(f.name()));
                    }
                }
                TypeRef elem_t = rt_ ? rt_.elem() : nullptr;
                LirBuilder b(out_);
                std::vector<lir_view::ExprRef> elems;
                for (auto& nm : field_names)
                    elems.push_back(b.lit_str(nm, elem_t));
                LogosTypeBuilder ab; ab.kind = LogosType::Kind::Array;
                ab.elem = elem_t;
                ab.arr_size = (int64_t)field_names.size();
                TypeRef new_arr_t = out_.type_pool.alloc(std::move(ab));
                rt_ = new_arr_t;
                mp_ =
                    lir_mirror_emit_arr_lit(out_, new_arr_t, elems);
                break;
            }
            // args_of::<T>() intrinsic — emits [Type; N] from the concrete
            // T's type_args() (empty array for non-generic T). Shares the
            // child-build code below with __type_refs_of__ and
            // __field_types_of__ (the latter resolves field types via the
            // struct template + a fresh SubstMap).
            if (nc.callee == "__args_of__" ||
                nc.callee == "__type_refs_of__" ||
                nc.callee == "__tuple_elems_of__" ||
                nc.callee == "__typelist_tail__" ||
                nc.callee == "__field_types_of__") {
                std::vector<TypeRef> elem_types;
                if (nc.callee == "__args_of__") {
                    if (!nc.type_args.empty())
                        for (auto a : nc.type_args[0].type_args())
                            elem_types.push_back(a);
                } else if (nc.callee == "__typelist_tail__") {
                    if (!nc.type_args.empty()) {
                        auto pack = nc.type_args[0].type_args();
                        for (size_t i = 1; i < pack.size(); ++i)
                            elem_types.push_back(pack[i]);
                    }
                } else if (nc.callee == "__tuple_elems_of__") {
                    if (!nc.type_args.empty()) {
                        TypeRef T = nc.type_args[0];
                        if (T && T.kind() == LogosType::Kind::Tuple)
                            for (auto a : T.tuple_elems())
                                elem_types.push_back(a);
                    }
                } else if (nc.callee == "__field_types_of__") {
                    if (!nc.type_args.empty()) {
                        TypeRef T = nc.type_args[0];
                        if (T && (T.kind() == LogosType::Kind::Struct ||
                                  T.kind() == LogosType::Kind::ZonedStruct)) {
                            std::string base{T.struct_name()};
                            std::string tpkg{T.pkg_name()};
                            const TypePoolImpl* fto_pool = out_.type_pool.impl();
                            lir_view::StructView tmpl;
                            for (auto& sd : in_.structs)
                                if (sd.name() == base &&
                                    (tpkg.empty() || sd.pkg() == tpkg)) {
                                    tmpl = sd; break;
                                }
                            if (!tmpl.valid())
                                for (auto& sd : in_.structs)
                                    if (sd.name() == base) { tmpl = sd; break; }
                            if (tmpl.valid()) {
                                SubstMap fsubst;
                                auto tmpl_tps = tmpl.type_params();
                                for (size_t i = 0, j = 0;
                                     i < tmpl_tps.size(); ++i) {
                                    if (j < T.type_args().size())
                                        fsubst[std::string(tmpl_tps[i].name())] =
                                            T.type_args()[j++];
                                }
                                for (auto f : tmpl.fields())
                                    elem_types.push_back(subst_type(f.type(fto_pool), fsubst));
                            }
                        }
                    }
                } else {
                    elem_types = nc.type_args;
                }
                TypeRef elem_t = rt_ ? rt_.elem() : nullptr;
                LogosTypeBuilder u32_b; u32_b.kind = LogosType::Kind::U32;
                TypeRef u32_t = out_.type_pool.alloc(std::move(u32_b));
                LogosTypeBuilder u8_b;  u8_b.kind  = LogosType::Kind::U8;
                TypeRef u8_t  = out_.type_pool.alloc(std::move(u8_b));
                LogosTypeBuilder sl_b;  sl_b.kind  = LogosType::Kind::Slice;
                sl_b.elem = u8_t;
                TypeRef slice_u8_t = out_.type_pool.alloc(std::move(sl_b));
                LogosTypeBuilder i64_b; i64_b.kind = LogosType::Kind::I64;
                TypeRef i64_t = out_.type_pool.alloc(std::move(i64_b));
                LogosTypeBuilder u64_b; u64_b.kind = LogosType::Kind::U64;
                TypeRef u64_t = out_.type_pool.alloc(std::move(u64_b));
                LirBuilder b(out_);
                std::vector<lir_view::ExprRef> elems;
                for (auto& ti : elem_types) {
                    std::vector<std::pair<std::string, lir::LExprPtr>> f;
                    f.emplace_back("kind",
                        b.lit_int((int64_t)ti.kind(), u32_t));
                    f.emplace_back("name",
                        b.lit_str(type_str(ti), slice_u8_t));
                    f.emplace_back("size",
                        b.size_of(ti, i64_t));
                    f.emplace_back("align",
                        b.align_of(ti, i64_t));
                    {
                        uint64_t uid = type_hash_64bit(type_hash_23(type_id_canon(ti)));
                        uid_to_type_[uid] = ti;
                        f.emplace_back("uid", b.lit_int((int64_t)uid, u64_t));
                    }
                    elems.push_back(b.struct_lit(elem_t, std::move(f)));
                }
                LogosTypeBuilder ab; ab.kind = LogosType::Kind::Array;
                ab.elem = elem_t;
                ab.arr_size = (int64_t)elem_types.size();
                TypeRef new_arr_t = out_.type_pool.alloc(std::move(ab));
                rt_ = new_arr_t;
                mp_ =
                    lir_mirror_emit_arr_lit(out_, new_arr_t, elems);
                break;
            }
            v.each_arg([&](lir_view::ExprRef ar) {
                if (ar && ar.kind() == lir_schema::expr::Code::PackExpand) {
                    std::string pe_var_name(lir_view::EPackExpandView{ar}.var_name());
                    std::string pack_name;
                    bool is_const_pack = false;
                    TypeRef at = ar.type(out_.type_pool.impl());
                    if (at && (at.kind() == LogosType::Kind::TypeVar ||
                               at.kind() == LogosType::Kind::ConstVar)) {
                        pack_name = std::string(at.type_var_name());
                        is_const_pack = (at.kind() == LogosType::Kind::ConstVar);
                    }
                    auto pit = cur_packs_.find(pack_name);
                    if (pit != cur_packs_.end()) {
                        for (size_t pi = 0; pi < pit->second.size(); ++pi) {
                            // Const-pack element: emit literal int with the
                            // param's underlying numeric type. Type-pack
                            // element: emit per-element var_ref synthesized
                            // by clone_fn into the callee's signature.
                            if (is_const_pack && pit->second[pi].const_val()) {
                                TypeRef et = pit->second[pi].pointee();
                                if (!et) et = pit->second[pi];
                                nc.args.push_back(LirBuilder(out_).lit_int(
                                    *pit->second[pi].const_val(), et));
                            } else {
                                nc.args.push_back(LirBuilder(out_).var_ref(
                                    make_pack_arg_name(pe_var_name, pi),
                                    pit->second[pi]));
                            }
                        }
                        if (templates_.count(nc.callee)) {
                            for (auto pt : pit->second)
                                nc.type_args.push_back(pt);
                        }
                    }
                } else {
                    nc.args.push_back(child_husk(subst_child_expr(ar)));
                }
            });
            // ADR 0030 S8 row 6: a trait item through a type (`T::m(..)`) is the
            // method of the impl of that trait for Self, once Self is concrete —
            // read off the impl, not re-spelled from the callee.
            if (std::string_view ti_trait = v.trait_item_trait(); !ti_trait.empty()) {
                TypeRef self_c = v.trait_item_self(out_.type_pool.impl());
                if (self_c) self_c = subst_type(self_c, s);
                bool conc = self_c && !contains_typevar(self_c);
                std::vector<TypeRef> ti_args;
                for (auto a : v.trait_item_args(out_.type_pool.impl()))
                    ti_args.push_back(a ? subst_type(a, s) : TypeRef{});
                for (auto a : nc.type_args) if (!a || contains_typevar(a)) conc = false;
                std::string sym;
                if (conc) {
                    std::vector<TypeRef> arg_tys;
                    for (auto& a : nc.args) arg_tys.push_back(a ? TypeRef(a.type(out_.type_pool.impl())) : TypeRef{});
                    sym = trait_item_symbol_(std::string(ti_trait), self_c, v.trait_item_method(),
                                             int64_t(nc.args.size()), nullptr, &ti_args, &nc.type_args, &arg_tys);
                }
                if (!sym.empty()) {
                    mp_ = lir_mirror_emit_call(out_, rt_, sym, {}, nc.args);
                    break;
                }
                // No impl answers (yet): a malfunction in the last round, a
                // deferred emission before it — never a composed name.
                trait_item_misses_.push_back(std::format(
                    "the trait item `{}::{}` resolves to no impl at Self `{}` (ADR 0030 S8 row 6)",
                    ti_trait, v.trait_item_method(), self_c ? type_str(self_c) : std::string("<none>")));
                mp_ = lir_mirror_emit_call(out_, rt_, nc.callee, nc.type_args, nc.args);
                break;
            }
            // A static call whose callee is spelled by a bound type variable
            // (`T__m`) and carries no trait item is a sema site this round did
            // not stamp: nothing re-spells it here (ADR 0030 S8 row 6).
            {
                std::string body = nc.callee;
                if (auto dot = body.rfind('.'); dot != std::string::npos) body = body.substr(dot + 1);
                for (size_t p = body.find("__"); p != std::string::npos; p = body.find("__", p + 1))
                    if (p > 0 && s.count(body.substr(0, p))) {
                        trait_item_misses_.push_back(std::format(
                            "the static call `{}` names a type parameter and carries no trait item "
                            "(ADR 0030 S8 row 6)", nc.callee));
                        break;
                    }
            }
            // Rewrite callee if it's a generic call already instantiated.
            if (!nc.type_args.empty()) {
                bool rewritten_as_struct_method = false;
                // ⚠ SEPARATOR CLASS. `nc.callee.find("__")` names `w` for
                // `w_`'s methods and cuts inside any method name containing
                // `__`; the struct-template lookup then misses and the rewrite
                // is skipped in SILENCE. The declared struct templates ARE the
                // registry, and the lookup itself is the membership test —
                // walk the candidate boundaries LONGEST-FIRST and take the
                // first prefix that actually resolves.
                size_t sep = std::string::npos;
                {
                    std::vector<size_t> bounds;
                    for (size_t p = nc.callee.find("__"); p != std::string::npos;
                         p = nc.callee.find("__", p + 1))
                        if (p > 0) bounds.push_back(p);
                    for (auto it2 = bounds.rbegin(); it2 != bounds.rend(); ++it2)
                        if (find_struct_template_unguarded(nc.callee.substr(0, *it2)).valid())
                            { sep = *it2; break; }
                    // No declared struct template is a prefix. That is a FACT
                    // (this callee is not a struct-method composition), not a
                    // licence to guess — but keep the legacy first-`__` cut so
                    // the pre-existing best-effort path survives for callees
                    // whose owner is not yet registered.
                    if (sep == std::string::npos && !bounds.empty())
                        sep = bounds.front();
                }
                if (sep != std::string::npos) {
                    std::string struct_part = nc.callee.substr(0, sep);
                    std::string method_part = nc.callee.substr(sep);
                    auto sit_ptr = find_struct_template_unguarded(struct_part);
                    if (sit_ptr.valid()) {
                        bool all_concrete = true;
                        for (auto ta : nc.type_args)
                            if (ta && TypeRef(ta).kind() == LogosType::Kind::TypeVar)
                                { all_concrete = false; break; }
                        // CP-cm-15: only do receiver-concretization +
                        // type_args.clear() when ALL type_args are
                        // receiver-level (no method-level tail). If a
                        // tail remains, leave the callee as bare-template
                        // form + full type_args so enqueue_if_needed can
                        // drive a single full specialization. Without
                        // this guard, `Wrap<T>::make_box<U>` calls land
                        // at a receiver-only spec with `U` still TypeVar
                        // in the body.
                        size_t n_impl_tp = sit_ptr.type_param_count();
                        bool has_method_level_tail =
                            nc.type_args.size() > n_impl_tp;
                        if (all_concrete && !has_method_level_tail) {
                            size_t n_args    = std::min(n_impl_tp, nc.type_args.size());
                            std::vector<TypeRef> args(
                                nc.type_args.begin(), nc.type_args.begin() + n_args);
                            // Structured impl self-type (`impl<T> Pin<&T>`):
                            // the method type_args carry the IMPL-level params
                            // ([T] = Pt), but the struct's concrete args are
                            // the substituted PATTERN args ([&T]{T:=Pt} = &Pt)
                            // — positional copy would name `Pin$G1$Pt` while
                            // the spec instantiates as `Pin$G1$ref_Pt`.
                            lir_view::FunctionView mt;
                            if (auto* smt = find_struct_method_templates_unguarded(
                                    struct_part)) {
                                // Match by the FULL mangled fn name — short-
                                // name keys can't disambiguate overloads.
                                for (auto& [mk, mf] : *smt)
                                    if (mf && mf.name() == nc.callee)
                                        { mt = mf; break; }
                            }
                            // Partial-spec impl method (impl<E> PkdArray<[E]>):
                            // it lives in the SPEC StructView's METHODS, not the
                            // base method-template map, and the call's type_args
                            // bind the SPEC's pattern vars — the positional copy
                            // above named the wrong struct instance ($G1$u8 for
                            // a PkdArray<[u8]> receiver). Recover the struct's
                            // concrete args by substituting the spec patterns.
                            if (!mt.valid()) {
                                std::string bare_sp = struct_part;
                                if (auto dp2 = bare_sp.rfind('.'); dp2 != std::string::npos)
                                    bare_sp = bare_sp.substr(dp2 + 1);
                                for (const std::string& skey : {struct_part, bare_sp}) {
                                    auto sit2 = struct_specs_.find(skey);
                                    if (sit2 == struct_specs_.end()) continue;
                                    for (auto spec : sit2->second) {
                                        lir_view::FunctionView sm;
                                        spec.each_method([&](lir_view::FunctionView m) {
                                            if (!sm.valid() && m && m.name() == nc.callee)
                                                sm = m;
                                        });
                                        if (!sm.valid()) continue;
                                        auto pats = spec.spec_patterns(out_.type_pool.impl());
                                        std::vector<std::string> pvars;
                                        for (auto pt2 : pats)
                                            collect_pattern_typevars(pt2, pvars);
                                        SubstMap ib2;
                                        for (size_t ai = 0; ai < pvars.size() &&
                                                            ai < nc.type_args.size(); ++ai)
                                            ib2[pvars[ai]] = nc.type_args[ai];
                                        std::vector<TypeRef> pargs2;
                                        bool resolved2 = true;
                                        for (auto pt2 : pats) {
                                            auto sa = subst_type(pt2, ib2);
                                            if (!sa || contains_typevar(sa) ||
                                                contains_assoc_type(sa))
                                                { resolved2 = false; break; }
                                            pargs2.push_back(sa);
                                        }
                                        if (resolved2) {
                                            args = std::move(pargs2);
                                            mt = sm;
                                            // Schedule the spec instance so its
                                            // methods (this one included) clone.
                                            record_needed_struct(
                                                build_generic_struct_typeref(
                                                    bare_sp, args,
                                                    std::string(spec.pkg())));
                                        }
                                        break;
                                    }
                                    if (mt.valid()) break;
                                }
                            }
                            TypeRef mt_itp = mt ? mt.impl_target_pattern(out_.type_pool.impl()) : TypeRef{};
                            if (mt && mt_itp) {
                                auto pat = TypeRef(mt_itp);
                                auto pa = pat.type_args();
                                if (!pa.empty()) {
                                    // Impl-level params are STRIPPED from the
                                    // template's type_params (kept in
                                    // impl_type_params); the call's type_args
                                    // carry [impl-level..., method-level...].
                                    SubstMap ib;
                                    size_t ai = 0;
                                    for (auto& tp : mt.impl_type_params())
                                        if (ai < nc.type_args.size())
                                            ib[std::string(tp.name())] = nc.type_args[ai++];
                                    for (auto& tp : mt.type_params())
                                        if (ai < nc.type_args.size())
                                            ib[std::string(tp.name())] = nc.type_args[ai++];
                                    std::vector<TypeRef> pargs;
                                    bool resolved = true;
                                    for (auto a : pa) {
                                        auto sa = subst_type(a, ib);
                                        if (!sa || contains_typevar(sa) ||
                                            contains_assoc_type(sa))
                                            { resolved = false; break; }
                                        pargs.push_back(sa);
                                    }
                                    if (resolved) args = std::move(pargs);
                                }
                            }
                            // Method-call callee: the BASE struct name stays bare
                            // (a generic type-arg still qualifies via mangle_type_for_name).
                            // The method's module qualification is applied later by the
                            // find_func_op chokepoint (§P3); qualifying the base here too
                            // would double-apply and produce a symbol find_func_op can't
                            // resolve (cross-module `&imported as &dyn`, generic method on
                            // an imported type).
                            std::string cname = concrete_struct_name_raw(struct_part, args);
                            nc.callee = cname + method_part;
                            nc.type_args.clear();
                            rewritten_as_struct_method = true;
                            // A generic-struct method call rewritten to concrete
                            // `Struct$G..$A__method` form must ALSO ensure that
                            // instance's methods get monomorphized — clearing
                            // type_args above turns this into a plain named call
                            // that neither scan_expr (no type_args) nor the
                            // struct-instantiation pass will otherwise pull in
                            // when the CU never CONSTRUCTS the struct. This is
                            // the generic-WritField field-read seam: a unit that
                            // only READS a `WRef<S>` field emits
                            // `WRef<S>::from_wany` (returning Self) but never
                            // materializes a `WRef<S>` value, so `WRef$G..$S`'s
                            // methods (from_wany) would never be cloned → mlir-gen
                            // "does not reference a valid function". Recording the
                            // receiver struct as needed schedules
                            // instantiate_struct_templates() to clone ALL its
                            // methods — exactly how a locally-constructed instance
                            // gets them. record_needed_struct no-ops on
                            // non-generic / TypeVar-bearing / already-done structs,
                            // so this is idempotent and cheap. The receiver type is
                            // recovered from the impl-target pattern substituted
                            // with `args` (works for any Self-shape, e.g. `WRef<S>`
                            // as well as `impl<T> Pin<&T>`); the method's own
                            // return type `rt_` is a fallback for the common
                            // Self-returning constructor/`from_wany` case.
                            if (mt && mt_itp) {
                                // Bind impl-level params by UNIFYING the impl-
                                // target pattern args against the STRUCT's
                                // concrete args — positional copy is wrong for
                                // structured patterns (impl<E> PkdArray<[E]>:
                                // struct arg [u8] binds E:=u8, not E:=[u8] —
                                // the positional form looped instantiation
                                // depth: PkdArray<[[u8]]>, <[[[u8]]]>, …).
                                SubstMap ib;
                                {
                                    auto pa2 = TypeRef(mt_itp).type_args();
                                    for (size_t i2 = 0; i2 < pa2.size() &&
                                                        i2 < args.size(); ++i2) {
                                        SubstMap b2;
                                        if (args[i2] && pa2[i2] &&
                                            unify_impl_target(args[i2], pa2[i2], b2))
                                            for (auto& [bk2, bv2] : b2)
                                                if (!ib.count(bk2)) ib[bk2] = bv2;
                                    }
                                }
                                TypeRef recv_t = subst_type(TypeRef(mt_itp), ib);
                                if (recv_t &&
                                    (recv_t.kind() == LogosType::Kind::Struct ||
                                     recv_t.kind() == LogosType::Kind::ZonedStruct))
                                    record_needed_struct(recv_t);
                            }
                            record_needed_struct(rt_);
                        }
                    }
                }
                if (!rewritten_as_struct_method)
                    nc.callee = mangle(nc.callee, nc.type_args);
            }
            // T2-24 (B): redirect to a const-arg specialization when the
            // callee's const-want params got compile-time literals here.
            maybe_const_specialize(nc);
            mp_ = lir_mirror_emit_call(
                out_, rt_, nc.callee, nc.type_args, nc.args);
            break;
        }
        case C::MethodCall: {
            lir_view::EMethodCallView v{eref};
            auto recv_ref = v.receiver();
            auto orig_recv_type = recv_ref.type(out_.type_pool.impl());
            // Receiver kept as a husk: this case may RE-TYPE it in place
            // (TypeVar→TraitObject dyn-receiver retarget) and threads it into
            // EMethodCall (which holds LExprPtr). The husk is a thin handle
            // over the already-emitted mirror.
            lir::LExprPtr new_recv = child_husk(subst_child_expr(recv_ref));
            std::string method{v.method()};
            std::string resolved_symbol{v.resolved_symbol()};
            std::string resolved_type{v.resolved_type()};
            std::string tag_system{v.tag_system()};
            std::string tag_trait{v.tag_trait()};
            int32_t vtable_index = v.vtable_index();
            // G158-7: a generic receiver that MONOMORPHISED to a trait object
            // (`tick_generic<C: ?Sized + Counter>(c: &mut C)` invoked with a
            // `&mut dyn Counter`) must dispatch through the vtable, not a static
            // `<cname>__<method>` symbol. After subst the receiver is
            // `MutRef(TraitObject)` / pointee `UnsizedDyn` with no vtable index
            // (sema couldn't know C would be a dyn). Re-type the receiver to a
            // bare TraitObject (matching a direct `&dyn`/`&mut dyn` call) and set
            // the slot from the trait's method order, then emit a dyn method
            // call. Gated on vtable_index<0 + empty resolved_symbol/tag so it
            // never disturbs an already-resolved dispatch.
            // The mirror stores vtable_index in a 24-bit field, so an unset
            // `-1` round-trips as 0x00FFFFFF. Treat any value with the 24-bit
            // sign bit set as "unset/virtual-unknown".
            bool vtable_unset = ((uint32_t)vtable_index) >= 0x00800000u;
            if (vtable_unset && resolved_symbol.empty() && tag_system.empty() &&
                new_recv && new_recv.type(out_.type_pool.impl())) {
                TypeRef nrt = new_recv.type(out_.type_pool.impl());
                while (nrt && (TypeRef(nrt).kind() == LogosType::Kind::Ptr ||
                               TypeRef(nrt).kind() == LogosType::Kind::Ref ||
                               TypeRef(nrt).kind() == LogosType::Kind::MutRef) &&
                       TypeRef(nrt).pointee())
                    nrt = TypeRef(nrt).pointee();
                if (nrt && (TypeRef(nrt).kind() == LogosType::Kind::TraitObject ||
                            TypeRef(nrt).kind() == LogosType::Kind::UnsizedDyn) &&
                    !TypeRef(nrt).trait_name().empty()) {
                    std::string tname(TypeRef(nrt).trait_name());
                    int slot = -1;
                    for (auto& td : out_.traits) {
                        if (td.name() != tname) continue;
                        int mi = 0;
                        td.each_method([&](lir_view::TraitMethodSigView m) {
                            if (slot < 0 && m.name() == method) slot = mi;
                            ++mi;
                        });
                        break;
                    }
                    if (slot >= 0) {
                        LogosTypeBuilder tob;
                        tob.kind = LogosType::Kind::TraitObject;
                        tob.trait_name = tname;
                        tob.pkg_name   = std::string(TypeRef(nrt).pkg_name());   // #438
                        tob.type_args = TypeRef(nrt).type_args();
                        LirBuilder(out_).retype_expr(new_recv, out_.type_pool.alloc(std::move(tob)));
                        std::vector<lir_view::ExprRef> mc_args;
                        v.each_arg([&](lir_view::ExprRef ar) {
                            mc_args.push_back(subst_child_expr(ar));
                        });
                        mp_ = lir_mirror_emit_method_call(
                            out_, rt_, new_recv, method, "",
                            {}, mc_args, slot, "", "", "");
                        break;
                    }
                }
            }
            // Unwrap pointer/reference for TypeVar check.
            auto orig_inner = orig_recv_type;
            if (orig_inner && (TypeRef(orig_inner).kind() == LogosType::Kind::Ptr ||
                               TypeRef(orig_inner).kind() == LogosType::Kind::Ref ||
                               TypeRef(orig_inner).kind() == LogosType::Kind::MutRef) &&
                TypeRef(orig_inner).pointee())
                orig_inner = TypeRef(orig_inner).pointee();
            // G149-1: an associated-type projection (`G::R`) receiver — e.g.
            // `let r = g.get(); r.method()` where `get(): Self::R` — needs the
            // same EMethodCall→concrete-symbol retargeting as a TypeVar
            // receiver once subst has normalized the projection to a concrete
            // type (assoc_impls_ lookup in subst_type). Without it the method
            // stays unresolved on an unrecognised receiver kind → mlir-gen
            // "unsupported receiver kind" → truncated body. Only retarget when
            // the substituted receiver is actually concrete (not still a
            // TypeVar/AssocType): an unresolved projection is left as-is.
            bool orig_retargetable =
                orig_inner &&
                (TypeRef(orig_inner).kind() == LogosType::Kind::TypeVar ||
                 TypeRef(orig_inner).kind() == LogosType::Kind::AssocType);
            bool new_concrete = false;
            if (new_recv && new_recv.type(out_.type_pool.impl())) {
                TypeRef nrt{new_recv.type(out_.type_pool.impl())};
                if ((nrt.kind() == LogosType::Kind::Ptr ||
                     nrt.kind() == LogosType::Kind::Ref ||
                     nrt.kind() == LogosType::Kind::MutRef) && nrt.pointee())
                    nrt = TypeRef(nrt.pointee());
                new_concrete = nrt.kind() != LogosType::Kind::TypeVar &&
                               nrt.kind() != LogosType::Kind::AssocType;
            }
            // `self: &Self` with `Self = &T` (a `where &T: Trait` bound): the
            // receiver is `&&T`, one layer more than the peel above removes.
            // Retarget it ONLY when the reference impl it names exists
            // (`$ref_<C>__m` / `$mut_ref_<C>__m`): any other `&&T` receiver is
            // resolved elsewhere, and claiming it here would compose a name
            // nothing carries.
            if (!orig_retargetable && orig_inner && new_recv &&
                (TypeRef(orig_inner).kind() == LogosType::Kind::Ref ||
                 TypeRef(orig_inner).kind() == LogosType::Kind::MutRef) &&
                TypeRef(orig_inner).pointee() &&
                TypeRef(TypeRef(orig_inner).pointee()).kind() == LogosType::Kind::TypeVar) {
                TypeRef nr{new_recv.type(out_.type_pool.impl())};
                TypeRef inner = nr && nr.pointee() ? TypeRef(nr.pointee()) : TypeRef{};
                TypeRef conc = inner && inner.pointee() ? TypeRef(inner.pointee()) : TypeRef{};
                if (conc && (inner.kind() == LogosType::Kind::Ref || inner.kind() == LogosType::Kind::MutRef)) {
                    std::string base_c =
                        (conc.kind() == LogosType::Kind::Struct || conc.kind() == LogosType::Kind::ZonedStruct)
                            ? concrete_struct_name(conc)
                        : conc.kind() == LogosType::Kind::Enum ? enum_instance_name(conc)
                                                               : type_str(conc);
                    std::string key = (inner.kind() == LogosType::Kind::MutRef ? "$mut_ref_" : "$ref_") +
                                      base_c + "__" + method;
                    auto names_it = [&](std::string_view t) {
                        return t == key || starts_with_parts(t, key, "__");
                    };
                    bool exists = templates_.count(key) || specs_.count(key);
                    if (!exists) for (auto& f : in_.functions)  if (names_it(bare_fn_name(f.name()))) { exists = true; break; }
                    if (!exists) for (auto& f : out_.functions) if (names_it(bare_fn_name(f.name()))) { exists = true; break; }
                    if (exists) { orig_retargetable = true; new_concrete = true; }
                }
            }
            // ADR 0030 S8 row 6: a bound's method on a receiver that became a
            // concrete struct is the method of the impl of the bound's trait
            // (`tag_trait`) for that struct — read off the impl, kept a method
            // call with its callee recorded.
            if (orig_retargetable && new_concrete && !tag_trait.empty() && new_recv &&
                new_recv.type(out_.type_pool.impl())) {
                // `Self` is the bound's type parameter (or projection, or the
                // `&T` of a `where &T: Trait` bound) at this instantiation.
                TypeRef cr = subst_type(orig_inner, s);
                // The call carries the trait's arguments first (`Iterator$G1$T`:
                // one), then the method's own; the trait's select the impl.
                std::string_view tt = tag_trait;
                // The traits the tag can name, each with its arity: the one
                // `$G<n>` says, else every declaration of that bare name (by its
                // identity) — homonyms with different arities split the call's
                // arguments differently, and the Self's impls decide.
                std::vector<std::pair<std::string, size_t>> cands;
                if (auto d = tt.find("$G"); d != std::string_view::npos) {
                    size_t n = 0;
                    for (size_t i = d + 2; i < tt.size() && tt[i] >= '0' && tt[i] <= '9'; ++i)
                        n = n * 10 + size_t(tt[i] - '0');
                    tt = tt.substr(0, d);
                    cands.emplace_back(std::string(tt), n);
                } else {
                    for (auto* pr : {&out_, &in_})
                        for (auto& td : pr->traits) {
                            if (td.name() != tt) continue;
                            std::string id = td.pkg().empty() ? std::string(td.name())
                                                              : std::string(td.pkg()) + "::" + std::string(td.name());
                            bool dup = false;
                            for (auto& c : cands) dup = dup || c.first == id;
                            if (!dup) cands.emplace_back(std::move(id), td.type_params().size());
                        }
                    if (cands.empty()) cands.emplace_back(std::string(tt), 0);
                }
                int64_t nargs = 0;
                v.each_arg([&](lir_view::ExprRef) { ++nargs; });
                std::vector<TypeRef> call_targs;
                for (auto ta : v.type_args(out_.type_pool.impl())) call_targs.push_back(ta ? subst_type(ta, s) : TypeRef{});
                std::string sym;
                bool sym_ambiguous = false;
                for (auto& [ident, arity] : cands) {
                    if (call_targs.size() < arity) continue;
                    std::vector<TypeRef> trait_args(call_targs.begin(), call_targs.begin() + arity);
                    std::vector<TypeRef> method_args(call_targs.begin() + arity, call_targs.end());
                    bool args_concrete = true;
                    for (auto a : method_args) if (!a || contains_typevar(a)) args_concrete = false;
                    if (!args_concrete) continue;
                    std::string r = trait_item_symbol_(ident, cr, method, nargs + 1, nullptr, &trait_args, &method_args);
                    // SL-sl-02, as sema's bound check: a `PartialEq` / `PartialOrd`
                    // bound is satisfied by the type's `Eq` / `Ord` impl, which
                    // carries the method.
                    if (r.empty() && (tt == "PartialEq" || tt == "PartialOrd"))
                        r = trait_item_symbol_(tt == "PartialEq" ? "logos.lang.cmp::Eq" : "logos.lang.cmp::Ord", cr,
                                               method, nargs + 1, nullptr, &trait_args, &method_args);
                    if (r.empty()) continue;
                    if (sym.empty()) sym = std::move(r);
                    else if (sym != r) sym_ambiguous = true;
                }
                if (sym_ambiguous) sym.clear();
                if (!sym.empty()) {
                    std::vector<lir_view::ExprRef> mc_args;
                    v.each_arg([&](lir_view::ExprRef ar) { mc_args.push_back(subst_child_expr(ar)); });
                    mp_ = lir_mirror_emit_method_call(out_, rt_, new_recv, method, sym, {}, mc_args,
                                                      -1, "", "", "");
                    break;
                }
            }
            // A bound's method whose receiver became concrete and that the impl
            // did not answer above: a malfunction (an impl sema accepted and mono
            // cannot find), never a `<Concrete>__<method>` to compose.
            if (orig_retargetable && new_concrete && new_recv && new_recv.type(out_.type_pool.impl())) {
                in_.diags.diags.push_back({Diag::Level::Error, "mono",
                    std::format("internal: the method `{}` of bound `{}` resolves to no impl at the "
                                "concrete receiver `{}` (ADR 0030 S8 row 6: a bound's method is the "
                                "method of the impl for Self; nothing composes a name)",
                                method, tag_trait.empty() ? std::string("<untagged>") : tag_trait,
                                type_str(new_recv.type(out_.type_pool.impl()))), diag_file_, diag_line_});
                std::vector<lir_view::ExprRef> mc_args;
                v.each_arg([&](lir_view::ExprRef ar) { mc_args.push_back(subst_child_expr(ar)); });
                mp_ = lir_mirror_emit_method_call(out_, rt_, new_recv, method, resolved_symbol, {}, mc_args,
                                                  vtable_index, resolved_type, tag_system, tag_trait);
                break;
            }
            // Non-trait-method-on-TypeVar path.
            lir::EMethodCall nm;
            nm.receiver = std::move(new_recv);
            nm.method = method;
            nm.resolved_symbol = resolved_symbol;
            for (auto ta : v.type_args(out_.type_pool.impl())) {
                if (ta && ta.kind() == LogosType::Kind::TypeVar) {
                    auto pit = cur_packs_.find(std::string(ta.type_var_name()));
                    if (pit != cur_packs_.end()) {
                        for (auto pt : pit->second) nm.type_args.push_back(pt);
                        continue;
                    }
                }
                nm.type_args.push_back(subst_type(ta, s));
            }
            nm.vtable_index = vtable_index;
            nm.tag_system = tag_system;
            nm.tag_trait  = tag_trait;
            // SPECIALIZATION LOOKUP (Bug 12)
            bool rewritten = false;
            if (nm.receiver && nm.receiver.type(out_.type_pool.impl())) {
                TypeRef rt = nm.receiver.type(out_.type_pool.impl());
                while (rt && (TypeRef(rt).kind() == LogosType::Kind::Ptr ||
                              TypeRef(rt).kind() == LogosType::Kind::Ref ||
                              TypeRef(rt).kind() == LogosType::Kind::MutRef) && TypeRef(rt).pointee()) {
                    rt = TypeRef(rt).pointee();
                }
                if (rt &&
                    (TypeRef(rt).kind() == LogosType::Kind::Struct ||
                     TypeRef(rt).kind() == LogosType::Kind::ZonedStruct ||
                     TypeRef(rt).kind() == LogosType::Kind::Enum)) {
                    std::vector<TypeRef> combined_args = TypeRef(rt).type_args();
                    for (auto mta : nm.type_args) combined_args.push_back(mta);
                    std::string base_struct;
                    if (!resolved_type.empty()) {
                        base_struct = resolved_type;
                    } else if (TypeRef(rt).kind() == LogosType::Kind::Enum) {
                        base_struct = TypeRef(rt).enum_name();
                    } else {
                        base_struct = TypeRef(rt).struct_name();
                    }
                    std::string base_name = base_struct + "__" + method;
                    auto pick_mono_template_key = [&]() -> std::string {
                        // The call's resolved_symbol (set by sema) is often the
                        // exact pkg-qualified template key — e.g. a trait-default
                        // body calling another method-generic method on `self`
                        // resolves to `pkg.Struct__method__g__<sig-with-tvars>`.
                        // Prefer it directly.
                        if (!resolved_symbol.empty() &&
                            (templates_.count(resolved_symbol) || specs_.count(resolved_symbol)))
                            return resolved_symbol;
                        if (templates_.count(base_name) || specs_.count(base_name))
                            return base_name;
                        std::string p = base_name + "__";
                        std::string p_dot = "." + p;   // pkg-qualified keys
                        for (auto& [kname, _] : templates_)
                            if (kname.rfind(p, 0) == 0 ||
                                kname.find(p_dot) != std::string::npos) return kname;
                        for (auto& [kname, _] : specs_)
                            if (kname.rfind(p, 0) == 0 ||
                                kname.find(p_dot) != std::string::npos) return kname;
                        return {};
                    };
                    std::string mono_base = pick_mono_template_key();
                    if (auto spec = find_best_spec(mono_base.empty() ? base_name : mono_base,
                                                    combined_args)) {
                        std::vector<lir_view::ExprRef> args;
                        args.push_back(nm.receiver);
                        v.each_arg([&](lir_view::ExprRef ar) {
                            args.push_back(subst_child_expr(ar));
                        });
                        mp_ = lir_mirror_emit_call(
                            out_, rt_, std::string(spec.name()), {}, args);
                        rewritten = true;
                    } else if (!combined_args.empty() && !mono_base.empty()) {
                        std::string callee = mangle(mono_base, combined_args);
                        std::vector<lir_view::ExprRef> args;
                        args.push_back(nm.receiver);
                        v.each_arg([&](lir_view::ExprRef ar) {
                            args.push_back(subst_child_expr(ar));
                        });
                        // Pull the specialisation into the worklist — without
                        // this the spec is named but never cloned, and mlir-gen
                        // silently drops the call (broken body). Fires for a
                        // trait-default method calling another method-generic
                        // method on `self`.
                        enqueue_if_needed(callee, combined_args);
                        mp_ = lir_mirror_emit_call(
                            out_, rt_, callee, combined_args, args);
                        rewritten = true;
                    }
                }
            }
            // Fallback for a trait-default method whose body calls another
            // METHOD-GENERIC method on a `self` typed as `Self` (a generic
            // struct): the call resolved (sema) to the callee's generic
            // struct-method TEMPLATE name carrying the FULL [impl + method]
            // type-args (nm.type_args = [T, A, F, …]). The block above can't
            // place it (its base_struct = struct_name(), not the concrete
            // `Struct$G…$…`, and combined_args double-counts the struct args).
            // Mangle the template directly with nm.type_args (== what a direct
            // concrete call produces) + enqueue.
            //
            // GATED to the method-generic case only: fire when the resolved
            // template has its OWN (method-level) type-params. A method with
            // only impl-level params (e.g. `Zone<M>::release`) must NOT take
            // this path — its concrete spec is the struct-name form
            // `Zone$G1$Mutable__release`, and template-mangling it would emit a
            // bogus `…__g__…$M__Mutable` (M unsubstituted) the existing path
            // already handles correctly.
            if (!rewritten && !resolved_symbol.empty() && !nm.type_args.empty() &&
                resolved_symbol.find("__g__") != std::string::npos) {
                bool all_concrete = true;
                for (auto ta : nm.type_args)
                    if (!ta || TypeRef(ta).kind() == LogosType::Kind::TypeVar)
                        { all_concrete = false; break; }
                // ⚠ SEPARATOR CLASS — registry-anchored, longest match. The
                // owner used to be `rs_tail.substr(0, rs_tail.find("__"))`,
                // which for `arr___pick__g__…` names `arr` and not `arr_`:
                // the template lookup missed, the specialisation was skipped
                // in SILENCE, logosc exited 0 and the program returned
                // garbage. `split_owner_method` asks the declared owners.
                std::string rs_struct, rs_method;
                if (auto om = split_owner_method(resolved_symbol)) {
                    // ⚠ `om->method` is the cut at the FIRST `__f__`/`__g__`,
                    // and that marker is legal INSIDE a method name — so this
                    // is a guess whenever the method's own name contains one
                    // (`fn a__f__b<U>`). It is used here only to seed
                    // `rs_struct`/`rs_method`, and every consumer below looks
                    // the result up in a template table that fails closed: a
                    // wrong short name finds nothing and the path is abandoned,
                    // it does not produce a wrong instantiation. The sound form
                    // is the registry match in mono_scan.cpp (the owner's method
                    // table decides); this site does not have that table in
                    // hand. Do NOT promote this to "exact" — an earlier comment
                    // did, and a two-line program refuted it.
                    if (!om->tail.empty()) {
                        rs_struct = std::string(om->owner);
                        rs_method = std::string(om->method);
                    }
                }
                lir_view::FunctionView tmpl;
                if (all_concrete && !rs_struct.empty() && !rs_method.empty()) {
                    if (auto* smt = find_struct_method_templates_unguarded(rs_struct)) {
                        auto mit = smt->find(rs_method);
                        if (mit == smt->end())
                            for (auto& [k, fn] : *smt)
                                if (starts_with_parts(k, rs_method, "__g__")) { mit = smt->find(k); break; }
                        if (mit != smt->end()) tmpl = mit->second;
                    }
                }
                // Only the method-generic case (template has its own tparams).
                if (tmpl && !tmpl.type_params_empty()) {
                    std::string callee = mangle(resolved_symbol, nm.type_args);
                    enqueue_if_needed(callee, nm.type_args);
                    std::vector<lir_view::ExprRef> args;
                    args.push_back(nm.receiver);
                    v.each_arg([&](lir_view::ExprRef ar) {
                        args.push_back(subst_child_expr(ar));
                    });
                    mp_ = lir_mirror_emit_call(
                        out_, rt_, callee, nm.type_args, args);
                    rewritten = true;
                }
            }
            if (!rewritten) {
// (Deferred-method receiver structs are recorded as needed at the
                // enqueue_method_inst DEFER site — recording eagerly here for every
                // dispatched call receiver tripled mono struct materialization.)

                v.each_arg([&](lir_view::ExprRef ar) {
                    nm.args.push_back(child_husk(subst_child_expr(ar)));
                });
                // T2-24 (B): const-arg spec for a concrete method kept as a
                // method call. The receiver is `self` (param 0) but isn't in
                // nm.args, so build a combined view [recv, args…] to map a
                // const-want param index directly. No-op unless the callee
                // body is available to clone.
                if (!nm.resolved_symbol.empty() && nm.receiver) {
                    if (auto inst = exact_method_instance(
                            nm.receiver.type(out_.type_pool.impl()), nm.method,
                            nm.resolved_symbol); !inst.empty())
                        nm.resolved_symbol = std::move(inst);
                    std::vector<lir::LExprPtr> combined;
                    combined.reserve(nm.args.size() + 1);
                    combined.push_back(nm.receiver);
                    for (auto a : nm.args) combined.push_back(a);
                    nm.resolved_symbol =
                        const_specialize_callee(nm.resolved_symbol, combined);
                }
                mp_ = lir_mirror_emit_method_call(
                    out_, rt_, nm.receiver, nm.method, nm.resolved_symbol,
                    nm.type_args, nm.args, nm.vtable_index, resolved_type,
                    nm.tag_system, nm.tag_trait);
            }
            break;
        }
        case C::ClosureBox: {
            lir_view::EClosureBoxView v{eref};
            auto br = v.body();
            if (!br) {
                mp_ = lir_mirror_emit_closure_box(
                    out_, rt_, nullptr);
                break;
            }
            auto nc = lir::alloc_closure(out_);
            // #442: A CLOSURE ID MUST BE PER INSTANTIATION. sema mints it once
            // per LITERAL (`__closure_0`), and codegen names the emitted MLIR
            // function exactly that, so a closure inside a generic function
            // instantiated at two types asked for ONE symbol twice:
            //   error: redefinition of symbol named '__closure_0'
            // and the whole module failed verification. Every other generic
            // entity is keyed per instantiation; a closure was not.
            // The substitution is what distinguishes the instances, and it is
            // in hand here. Keys are sorted so the suffix does not depend on
            // the map's iteration order; an EMPTY substitution (a verbatim
            // clone, not an instantiation) leaves the id untouched, so no
            // existing symbol is re-spelled.
            std::string cid(v.closure_id());
            if (!s.empty()) {
                std::vector<std::string> ks;
                ks.reserve(s.size());
                // Inference solutions (`?iN`, `?lK`) are the template's own,
                // the same in every instance: they name no instantiation.
                for (auto& kv : s) if (!kv.first.starts_with('?')) ks.push_back(kv.first);
                std::sort(ks.begin(), ks.end());
                for (auto& k : ks) {
                    auto it = s.find(k);
                    if (it == s.end() || !it->second) continue;
                    cid += "__";
                    cid += mangle_type(it->second);
                }
            }
            nc->closure_id = std::move(cid);
            v.each_param(out_.type_pool.impl(),
                [&](std::string_view nm, TypeRef pt) {
                    nc->params.push_back({std::string(nm), subst_type(pt, s)});
                });
            nc->ret_type  = subst_type(v.ret_type(out_.type_pool.impl()), s);
            nc->body      = subst_child_block(br);
            nc->is_move   = v.is_move();
            nc->ret_tied  = v.ret_tied();
            nc->as_fn_ptr = v.as_fn_ptr();
            nc->escapes   = v.escapes();  // G167-3b: preserve heap-env flag
            nc->fn_once   = v.fn_once();
            v.each_capture_name([&](std::string_view cn) {
                nc->captures.push_back(std::string(cn));
            });
            v.each_capture(out_.type_pool.impl(),
                [&](std::string_view, TypeRef ct) {
                    nc->capture_types.push_back(subst_type(ct, s));
                });
            // C5-cl-08: carry per-capture mut-flag across substitution.
            nc->mut_captures.resize(nc->captures.size(), false);
            for (size_t i = 0; i < nc->captures.size(); ++i)
                nc->mut_captures[i] = v.capture_is_mut(i);
            // ADR 0028: carry the per-capture MODE and the widening bit.
            nc->capture_modes.resize(nc->captures.size(), 0);
            nc->capture_widened.resize(nc->captures.size(), 0);
            for (size_t i = 0; i < nc->params.size(); ++i) nc->param_slots.push_back(v.param_slot(i));
            nc->capture_body_moved.resize(nc->captures.size(), 0);
            for (size_t i = 0; i < nc->captures.size(); ++i) {
                nc->capture_modes[i]   = v.capture_mode(i);
                nc->capture_widened[i] = v.capture_widened(i) ? 1 : 0;
                nc->capture_body_moved[i] = v.capture_body_moved(i) ? 1 : 0;
            }
            // RFC-2229: carry per-capture field PATH across substitution (else
            // post-mono borrow-check reads only the root and disjoint sibling
            // mutation is wrongly rejected).
            nc->capture_paths.resize(nc->captures.size());
            for (size_t i = 0; i < nc->captures.size(); ++i)
                nc->capture_paths[i] = std::string(v.capture_path(i));
            // RFC-2229 phase-2: carry per-capture FIELD TYPE (narrow paths) too.
            nc->capture_field_types.resize(nc->captures.size());
            for (size_t i = 0; i < nc->captures.size(); ++i) {
                auto ft = v.capture_field_type(out_.type_pool.impl(), i);
                nc->capture_field_types[i] = ft ? subst_type(ft, s) : TypeRef{};
            }
            mp_ = lir_mirror_emit_closure_box(
                out_, rt_, nc);
            break;
        }
        default:
            std::fprintf(stderr,
                "mono.subst_expr: unhandled expr Code=%d\n",
                int(eref.kind()));
            std::abort();
        }
    }

    // Mirror already emitted by the per-kind direct emitters above; mp_ is its
    // absolute address in out_'s arena. Return a view over it (null mp_ → null
    // ExprRef, equivalent to the old null-mirror husk).
    return lir_view::ExprRef(out_.type_pool.arena(), mp_);
}


// View-based pattern subst: walks input via PatRef (mirror-tracked), builds
// new lir::Pattern variants from view fields. M1-M5 fixes preserved.
lir::Pattern PatSubstWalker::walk(lir_view::PatRef pref) const {
    namespace pc = lir_schema::pat;
    if (!pref) return lir::Pattern{};  // defensive: no mirror entry
    switch (pref.kind()) {
    case pc::Code::Variant: {
        lir_view::PatVariantView v{pref};
        std::string en(v.enum_name());
        std::string vn(v.variant());
        int64_t disc = v.disc();
        lir::Pattern p;
        p.mirror_ptr_ = lir_mirror_emit_pat_variant(*prog_, en, vn, disc);
        return p;
    }
    case pc::Code::Int: {
        int64_t v = lir_view::PatIntView{pref}.value();
        lir::Pattern p;
        p.mirror_ptr_ = lir_mirror_emit_pat_int(*prog_, v);
        return p;
    }
    case pc::Code::Bool: {
        bool v = lir_view::PatBoolView{pref}.value();
        lir::Pattern p;
        p.mirror_ptr_ = lir_mirror_emit_pat_bool(*prog_, v);
        return p;
    }
    case pc::Code::Str: {
        std::string v(lir_view::PatStrView{pref}.value());
        lir::Pattern p;
        p.mirror_ptr_ = lir_mirror_emit_pat_str(*prog_, v);
        return p;
    }
    case pc::Code::Wild: {
        lir_view::PatWildView wv{pref};
        std::string name(wv.name());
        lir::Pattern p;
        p.mirror_ptr_ = lir_mirror_emit_pat_wild(*prog_, name, wv.bind_slot(), wv.is_mut());  // Phase-1
        return p;
    }
    case pc::Code::Range: {
        lir_view::PatRangeView v{pref};
        __int128 lo = v.lo(), hi = v.hi();
        lir::Pattern p;
        p.mirror_ptr_ = lir_mirror_emit_pat_range(*prog_, lo, hi);
        return p;
    }
    case pc::Code::VariantData: {
        lir_view::PatVariantDataView v{pref};
        lir::PatVariantData n;
        n.enum_name = std::string(v.enum_name());
        n.variant   = std::string(v.variant());
        n.disc      = v.disc();
        v.each_binding([&](std::string_view s) { n.bindings.emplace_back(s); });
        v.each_binding_type(pool_, [&](TypeRef t) { n.binding_types.push_back(st_(t)); });
        // The payload sub-patterns (ADR 0030 S3), cloned positionally.
        std::vector<const uint8_t*> subs;
        for (auto sp : v.subs()) subs.push_back(sp ? walk(sp).mirror_ptr_ : nullptr);
        auto off = lir_mirror_emit_pat_variant_data(
            *prog_, n.enum_name, n.variant, n.disc, n.bindings, n.binding_types,
            v.bind_slots(),        // Phase-1: carry slots
            v.bind_ref_modes_raw(),    // and the binding modes, `mut` bit included
            subs);
        lir::Pattern p_;
        p_.mirror_ptr_ = off;
        return p_;
    }
    case pc::Code::Or: {
        lir::PatOr n;
        lir_view::PatOrView{pref}.each_alt(
            [&](lir_view::PatRef alt) { n.alts.push_back(walk(alt)); });
        auto off = lir_mirror_emit_pat_or(*prog_, n.alts);
        lir::Pattern p;
        p.mirror_ptr_ = off;
        return p;
    }
    case pc::Code::Tuple: {
        lir_view::PatTupleView v{pref};
        lir::PatTuple n;
        v.each_binding([&](std::string_view s) { n.bindings.emplace_back(s); });
        v.each_binding_type(pool_, [&](TypeRef t) { n.binding_types.push_back(st_(t)); });
        v.each_sub([&](lir_view::PatRef sp) { n.subs.push_back(walk(sp)); });
        auto off = lir_mirror_emit_pat_tuple(
            *prog_, n.bindings, n.binding_types, n.subs, v.bind_slots());  // Phase-1
        lir::Pattern p;
        p.mirror_ptr_ = off;
        return p;
    }
    case pc::Code::Struct: {
        lir_view::PatStructView v{pref};
        lir::PatStruct n;
        n.struct_name = std::string(v.struct_name());
        n.has_rest    = v.has_rest();
        v.each_field([&](lir_view::PatFieldBindingView fbv) {
            lir::PatFieldBinding pfb;
            pfb.field_name = std::string(fbv.field_name());
            if (auto sub = fbv.sub()) pfb.sub.push_back(walk(sub));
            pfb.slot = fbv.bind_slot();  // Phase-1
            pfb.is_mut = fbv.is_mut();
            n.fields.push_back(std::move(pfb));
        });
        auto off = lir_mirror_emit_pat_struct(
            *prog_, n.struct_name, n.fields, n.has_rest);
        lir::Pattern p;
        p.mirror_ptr_ = off;
        return p;
    }
    case pc::Code::Slice: {
        lir_view::PatSliceView v{pref};
        lir::PatSlice n;
        v.each_prefix([&](lir_view::PatRef pp) { n.prefix.push_back(walk(pp)); });
        if (auto r = v.rest()) n.rest.push_back(walk(r));
        v.each_suffix([&](lir_view::PatRef pp) { n.suffix.push_back(walk(pp)); });
        auto off = lir_mirror_emit_pat_slice(
            *prog_, n.prefix, n.rest, n.suffix);
        lir::Pattern p;
        p.mirror_ptr_ = off;
        return p;
    }
    case pc::Code::At: {
        lir_view::PatAtView v{pref};
        lir::PatAt n;
        n.name = std::string(v.name());
        n.type = st_(v.type(pool_));
        n.is_mut = v.is_mut();
        n.ref_mode = v.ref_mode();
        if (auto sub = v.sub()) n.sub.push_back(walk(sub));
        auto off = lir_mirror_emit_pat_at(*prog_, n.name, n.sub, n.type, v.bind_slot(), n.is_mut, n.ref_mode);  // Phase-1
        lir::Pattern p;
        p.mirror_ptr_ = off;
        return p;
    }
    case pc::Code::RefBind: {
        lir_view::PatRefBindView v{pref};
        lir::PatRefBind n;
        n.name      = std::string(v.name());
        n.is_mut    = v.is_mut();
        n.bind_type = st_(v.bind_type(pool_));
        auto off = lir_mirror_emit_pat_ref_bind(
            *prog_, n.name, n.is_mut, n.bind_type, v.bind_slot());  // Phase-1
        lir::Pattern p;
        p.mirror_ptr_ = off;
        return p;
    }
    case pc::Code::RefPat: {
        lir_view::PatRefPatView v{pref};
        lir::PatRefPat n;
        n.is_mut = v.is_mut();
        if (auto inner = v.inner()) n.inner.push_back(walk(inner));
        auto off = lir_mirror_emit_pat_ref_pat(*prog_, n.inner, n.is_mut);
        lir::Pattern p;
        p.mirror_ptr_ = off;
        return p;
    }
    }
    return lir::Pattern{};
}

lir_view::StmtRef Mono::subst_stmt(lir_view::StmtRef sref, const SubstMap& s) {
    // Returns a StmtRef into out_ (the eager-emitted mirror). Null StmtRef ==
    // old null/empty LStmt. Each case eager-emits its mirror via
    // lir_mirror_emit_<kind> and stores the result into the local husk `ns`
    // below; the function returns a view over ns.mirror_ptr_.
    struct { uint32_t line = 0; const uint8_t* mirror_ptr_ = nullptr; } ns;
    if (!sref) return lir_view::StmtRef{};  // null source — defensive
    // Phase 5.B: read line from the mirror via the view (cross-arena safe).
    ns.line = lir_view::stmt_line(sref);
    if (ns.line) diag_line_ = ns.line;

    // subst_stmt still builds lir::LStmt with LExprPtr husk members; bridge the
    // ExprRef that subst_expr now returns into a thin husk over its mirror.
    auto subst_child_expr = [&](lir_view::ExprRef er) -> lir::LExprPtr {
        if (!er) return {};
        // subst_expr returns an ExprRef into out_; LExprPtr now IS ExprRef.
        return subst_expr(er, s);
    };
    auto subst_child_block = [&](lir_view::BlockRef br) -> lir_view::BlockRef {
        return br ? subst_block(br, s) : lir_view::BlockRef{};
    };

    using SCode = lir_schema::stmt::Code;
    const TypePoolImpl* pool = out_.type_pool.impl();

    switch (sref.kind()) {
    case SCode::Let: {
        lir_view::SLetView v{sref};
        std::string name(v.name());
        TypeRef ty = subst_type(v.type(pool), s);
        bool is_mut = v.is_mut();
        auto rhs = v.value();
        if (rhs) type_let_inits_[name] = rhs;
        auto value = subst_child_expr(rhs);
        ns.mirror_ptr_ = lir_mirror_emit_let(
            out_, ns.line, name, ty, value, is_mut, v.var_slot(),
            v.compiler_glue(), v.destructure_tmp(), v.annot_lifetime());  // Phase-1: carry slot; #121-A: carry provenance
        break;
    }
    case SCode::Assign: {
        lir_view::SAssignView v{sref};
        std::string name(v.name());
        auto value = subst_child_expr(v.value());
        ns.mirror_ptr_ = lir_mirror_emit_assign(
            out_, ns.line, name, value, v.drop_old());  // B8: preserve drop-before-replace
        break;
    }
    case SCode::Return: {
        auto val = lir_view::SReturnView{sref}.value();
        auto value = val ? subst_child_expr(val) : nullptr;
        ns.mirror_ptr_ = lir_mirror_emit_return(
            out_, ns.line, value);
        break;
    }
    case SCode::If: {
        lir_view::SIfView v{sref};
        auto cond = subst_child_expr(v.cond());
        auto then_blk = subst_child_block(v.then_block());
        lir_view::BlockRef else_blk{};
        if (auto eb = v.else_block()) {
            else_blk = subst_child_block(eb);
        }
        ns.mirror_ptr_ = lir_mirror_emit_if_stmt(
            out_, ns.line, cond, then_blk, else_blk);
        break;
    }
    case SCode::While: {
        lir_view::SWhileView v{sref};
        auto cond = subst_child_expr(v.cond());
        auto body = subst_child_block(v.body());
        std::string label(v.label());
        ns.mirror_ptr_ = lir_mirror_emit_while(
            out_, ns.line, cond, body, label);
        break;
    }
    case SCode::For: {
        lir_view::SForView v{sref};
        std::string var(v.var());
        auto lo = subst_child_expr(v.lo());
        auto hi = subst_child_expr(v.hi());
        bool inclusive = v.inclusive();
        auto body = subst_child_block(v.body());
        std::string label(v.label());
        ns.mirror_ptr_ = lir_mirror_emit_for(
            out_, ns.line, var, lo, hi, inclusive, body, label, v.var_slot(), v.var_mut());  // Phase-1
        break;
    }
    case SCode::Loop: {
        lir_view::SLoopView v{sref};
        auto body = subst_child_block(v.body());
        // The loop's value type is the template's: `loop { break t }` in `g<T>`
        // is a T loop, an i64 one in `g::<i64>`.
        TypeRef result_type = subst_type(v.result_type(pool), s);
        std::string break_slot(v.break_slot());
        std::string label(v.label());
        ns.mirror_ptr_ = lir_mirror_emit_loop(
            out_, ns.line, body, label, break_slot, result_type);
        break;
    }
    case SCode::Block: {
        lir_view::SBlockView v{sref};
        auto blk = subst_child_block(v.body());
        // Carry the TRANSPARENT marker through the clone: dropping it would
        // make a monomorphized copy of a synthetic block re-acquire a lexical
        // scope its bindings must escape.
        ns.mirror_ptr_ = lir_mirror_emit_block_stmt(
            out_, ns.line, blk, v.transparent());
        break;
    }
    case SCode::Break: {
        lir_view::SBreakView v{sref};
        lir::LExprPtr value = nullptr;
        if (auto val = v.value()) value = subst_child_expr(val);
        std::string label(v.label());
        ns.mirror_ptr_ = lir_mirror_emit_break(
            out_, ns.line, value, label);
        break;
    }
    case SCode::Continue: {
        std::string label(lir_view::SContinueView{sref}.label());
        ns.mirror_ptr_ = lir_mirror_emit_continue(
            out_, ns.line, label);
        break;
    }
    case SCode::FieldWrite: {
        lir_view::SFieldWriteView v{sref};
        std::string receiver(v.receiver());
        std::string field(v.field());
        auto value = subst_child_expr(v.value());
        ns.mirror_ptr_ = lir_mirror_emit_field_write(
            out_, ns.line, receiver, field, value);
        break;
    }
    case SCode::ChainFieldWrite: {
        lir_view::SChainFieldWriteView v{sref};
        std::string receiver(v.receiver());
        std::string mid_field(v.mid_field());
        std::string field(v.field());
        std::vector<std::string> extras;
        v.each_extra([&](std::string_view s) { extras.emplace_back(s); });
        auto value = subst_child_expr(v.value());
        ns.mirror_ptr_ = lir_mirror_emit_chain_field_write(
            out_, ns.line, receiver, mid_field, extras, field, value);
        break;
    }
    case SCode::DerefFieldWrite: {
        lir_view::SDerefFieldWriteView v{sref};
        std::string receiver(v.receiver());
        std::string type_name(v.type_name());
        std::string field(v.field());
        auto value = subst_child_expr(v.value());
        ns.mirror_ptr_ = lir_mirror_emit_deref_field_write(
            out_, ns.line, receiver, type_name, field, value);
        break;
    }
    case SCode::IndexWrite: {
        lir_view::SIndexWriteView v{sref};
        std::string arr(v.arr());
        auto idx = subst_child_expr(v.index());
        auto value = subst_child_expr(v.value());
        ns.mirror_ptr_ = lir_mirror_emit_index_write(
            out_, ns.line, arr, idx, value);
        break;
    }
    case SCode::FieldIndexWrite: {
        lir_view::SFieldIndexWriteView v{sref};
        std::string receiver(v.receiver());
        std::string field(v.field());
        auto idx = subst_child_expr(v.index());
        auto value = subst_child_expr(v.value());
        ns.mirror_ptr_ = lir_mirror_emit_field_index_write(
            out_, ns.line, receiver, field, idx, value);
        break;
    }
    case SCode::DerefWrite: {
        lir_view::SDerefWriteView v{sref};
        auto ptr = subst_child_expr(v.ptr());
        auto value = subst_child_expr(v.value());
        ns.mirror_ptr_ = lir_mirror_emit_deref_write(
            out_, ns.line, ptr, value, v.drop_old());  // T1.5: preserve drop_old
        break;
    }
    case SCode::TupleWrite: {
        lir_view::STupleWriteView v{sref};
        std::string receiver(v.receiver());
        uint32_t index = v.index();
        auto value = subst_child_expr(v.value());
        TypeRef recv_type = v.recv_type(pool);
        ns.mirror_ptr_ = lir_mirror_emit_tuple_write(
            out_, ns.line, receiver, index, value, recv_type);
        break;
    }
    case SCode::ExprStmt: {
        auto expr = subst_child_expr(lir_view::SExprStmtView{sref}.expr());
        ns.mirror_ptr_ = lir_mirror_emit_expr_stmt(
            out_, ns.line, expr);
        break;
    }
    case SCode::Drop: {
        lir_view::SDropView v{sref};
        std::string var_name(v.var_name());
        std::string drop_fn(v.drop_fn());
        TypeRef ty = subst_type(v.type(pool), s);
        bool drop_fields = v.drop_fields();
        // Sentinel from sema: original type was a TypeVar (generic param);
        // substitution has now produced a concrete type. Resolve to the
        // actual drop fn (or skip entirely if the substituted type has no
        // Drop impl).
        if (drop_fn == "__typevar_pending__drop") {
            drop_fn.clear();
            if (ty && (TypeRef(ty).kind() == LogosType::Kind::Struct ||
                       TypeRef(ty).kind() == LogosType::Kind::ZonedStruct)) {
                auto cname = concrete_struct_name(ty);
                if (!cname.empty()) drop_fn = cname + "__drop";
                // Propagate drop_fields=true so mlir-gen's SDrop walks
                // the substituted struct's droppable fields. Mirrors
                // sema's make_drop_stmt convention for direct SDrops
                // (drop_fn called when present, then fields auto-walked).
                // Without this, structs like IterFrame { branch_arc:
                // NodeARC, ... } used as Vec<T> elements would never
                // auto-drop their fields (when Vec.drop's mono'd body
                // bitwise-reads each element into a typed local).
                // Lookup uses the CONCRETE mangled name (out_.structs
                // holds monomorphized defs after clone_struct_def);
                // falls back to bare name for non-generic structs.
                const TypePoolImpl* df_pool = out_.type_pool.impl();
                for (auto& sd : out_.structs) {
                    bool match = (!cname.empty() && sd.name() == cname) ||
                                 sd.name() == TypeRef(ty).struct_name();
                    if (!match) continue;
                    for (auto fv : sd.fields()) {
                        TypeRef f_type = fv.type(df_pool);
                        if (!f_type) continue;
                        // Force field-recursion if ANY field's drop is non-trivial.
                        // Must mirror mlir-gen's gen_drop_value / value_needs_drop
                        // coverage, NOT just Struct/ZonedStruct: an owning fat field
                        // (Box<dyn>/Rc<dyn> = owning TraitObject, Box<[T]>, Box<DST>)
                        // is droppable too, and was silently dropped on the floor —
                        // a `struct H { pin: Rc<dyn Tr> }` / `{ obj: Box<dyn Tr> }`
                        // used as a Vec/Rc element never released its dyn payload.
                        auto fkk = TypeRef(f_type).kind();
                        if (fkk == LogosType::Kind::Struct ||
                            fkk == LogosType::Kind::ZonedStruct ||
                            fkk == LogosType::Kind::Enum ||
                            fkk == LogosType::Kind::Tuple ||
                            fkk == LogosType::Kind::Array ||
                            fkk == LogosType::Kind::Closure ||
                            (fkk == LogosType::Kind::TraitObject &&
                             TypeRef(f_type).owning_trait_object()) ||
                            (fkk == LogosType::Kind::Slice &&
                             TypeRef(f_type).owning_slice()) ||
                            (fkk == LogosType::Kind::DstRef &&
                             TypeRef(f_type).owning_dst())) {
                            drop_fields = true;
                            break;
                        }
                    }
                    break;
                }
            }
            // Enum value-repr / tuple / array: the substituted concrete type
            // owns its variant payload / elements INLINE, but sema emitted the
            // sentinel drop with drop_fields=false (has_droppable_fields was
            // false for the opaque TypeVar). Set drop_fields=true so mlir-gen's
            // SDrop runs gen_drop_value's variant-switch / element recursion
            // (it internally no-ops when nothing is droppable and dispatches a
            // user Drop impl itself). Without this, a `Vec<Enum-with-String>` /
            // `Vec<(String,…)>` element bound by-value in the mono'd Vec::drop
            // loop never freed its payload (the P1 Vec-element-drop leak).
            else if (ty && TypeRef(ty).owning_trait_object()) {
                // owning Box<dyn> element (e.g. Vec<Box<dyn T>>'s `let _x: T =
                // p[i]` move-and-drop): route directly to the box-dyn value
                // drop (vtable[0] drop_in_place + free data) via the explicit
                // sentinel — the generic field-recursion does not drop a
                // top-level TraitObject.
                drop_fn = "__box_dyn__drop";
            }
            else if (ty && (TypeRef(ty).kind() == LogosType::Kind::UnsizedDyn ||
                            // A `dyn Trait` arg canonicalised to the uniform fat
                            // form arrives as TraitObject(owning=Borrow) — the
                            // SAME owned unsized tail (the common cross-module
                            // `Arc<dyn>`/`Rc<dyn>` case; mirror of 227fe173's
                            // is_effective_dst / mono inst_dst / field-projection
                            // additions). It must drop_in_place too, else the
                            // concrete node's destructor never runs (memory
                            // leak). GATED on the move SOURCE being an owned
                            // DST-tail projection (`self.inner.val` off a fat
                            // DstRef): a genuine borrowed `&dyn` local — same
                            // TraitObject(Borrow) type — must NOT drop its
                            // referent, so the bare-type check is insufficient.
                            (TypeRef(ty).kind() == LogosType::Kind::TraitObject &&
                             TypeRef(ty).trait_owning_kind() ==
                                 TypeRef::OwningKind::Borrow &&
                             let_init_is_owned_dyn_tail(var_name, s)))) {
                // Move-out drop of an unsized `dyn` TAIL (`let _v: T =
                // self.inner.val`, T bound to `dyn`): the RHS re-lowered to a
                // `&dyn` handle; drop the concrete payload in place via
                // vtable[0] (no free — the block is freed separately by the
                // surrounding drop). Mirrors the sized case ("run Drop, don't
                // free the storage").
                drop_fn = "__dyn_drop_in_place__";
            }
            else if (ty && (TypeRef(ty).kind() == LogosType::Kind::Enum ||
                            TypeRef(ty).kind() == LogosType::Kind::Tuple ||
                            TypeRef(ty).kind() == LogosType::Kind::Array ||
                            TypeRef(ty).kind() == LogosType::Kind::Closure)) {
                // Closure: `let _inner: T = p[0]` in Box<T>::drop with
                // T=Closure — run the env drop glue (free heap env + owned
                // captures) via mlir-gen's SDrop Closure branch.
                drop_fields = true;
            }
        }
        // Re-mangle drop_fn for the substituted concrete struct type. Sema's
        // drop_fn_for returns the template name (e.g. "Foo__drop") for
        // generic struct instances because types_equal can't match
        // Foo<TypeVar> against Foo<concrete>. clone_struct_def emits the
        // monomorphised Drop fn under "<concrete_struct_name>__drop"
        // (e.g. "Foo$G1$i64__drop"); rewrite the call here to point there.
        if (!drop_fn.empty() && ty &&
            (TypeRef(ty).kind() == LogosType::Kind::Struct ||
             TypeRef(ty).kind() == LogosType::Kind::ZonedStruct) &&
            !TypeRef(ty).type_args().empty()) {
            auto cname = concrete_struct_name(ty);
            if (!cname.empty()) drop_fn = cname + "__drop";
        }
        std::vector<std::string> moved_fields;
        v.each_moved_field([&](std::string_view f) { moved_fields.emplace_back(f); });
        ns.mirror_ptr_ = lir_mirror_emit_drop(
            out_, ns.line, var_name, drop_fn, ty, drop_fields, moved_fields, v.var_slot());
        break;
    }
    case SCode::ForEach: {
        lir_view::SForEachView v{sref};
        std::string var(v.var());
        auto iter = subst_child_expr(v.iter());
        TypeRef elem_type = subst_type(v.elem_type(pool), s);
        int64_t arr_size = v.arr_size();
        bool is_slice = v.is_slice();
        // Symbolic-length iterables (e.g. `for x in arr` where arr has type
        // `[T; sizeof...(P)]`) record arr_size==0 at sema; re-derive from the
        // substituted iter type once the pack length is concrete.
        TypeRef iter_t = iter ? iter.type(out_.type_pool.impl()) : TypeRef{};
        if (arr_size == 0 && !is_slice && iter && iter_t &&
            iter_t.kind() == LogosType::Kind::Array)
            arr_size = (int64_t)iter_t.arr_size();
        auto body = subst_child_block(v.body());
        ns.mirror_ptr_ = lir_mirror_emit_for_each(
            out_, ns.line, var, iter, elem_type, arr_size, is_slice, body, v.var_slot(), v.var_mut(), v.label());  // Phase-1
        break;
    }
    case SCode::LetElse: {
        lir_view::SLetElseView v{sref};
        lir::Pattern pat;
        if (auto pref = v.pat()) pat = subst_pattern(pref, s);
        auto scrut = subst_child_expr(v.scrut());
        auto else_block = subst_child_block(v.else_block());
        ns.mirror_ptr_ = lir_mirror_emit_let_else(
            out_, ns.line, pat, scrut, else_block);
        break;
    }
    default: break;
    }

    // Children were registered by their own _node emit calls. Return a view
    // over the eager-emitted mirror.
    return lir_view::StmtRef(out_.type_pool.arena(), ns.mirror_ptr_);
}


// ── Clone a function with substitution (empty SubstMap = verbatim copy) ─

DeclBuilder Mono::clone_fn(lir_view::FunctionView fn, const SubstMap& s_in,
                         const PackMap& packs) {
    namespace dk = lir_schema::decl_keys;
    // Diagnostics raised while cloning this body name its file (restored for
    // the instance that was being cloned when this one was requested).
    struct DiagPosGuard {
        Mono& m; std::string file; uint32_t line;
        ~DiagPosGuard() { m.diag_file_ = std::move(file); m.diag_line_ = line; }
    } diag_pos_guard{*this, diag_file_, diag_line_};
    diag_file_ = std::string(fn.source_file());
    diag_line_ = 0;
    // Local type inference: the body's solved inference variables (`?iN`) join
    // the substitution, each read through the instance's own (a solution may
    // name the template's parameters).
    SubstMap s = s_in;
    if (auto it = in_.infer_substs.find(std::string(fn.name())); it != in_.infer_substs.end())
        for (auto& [n, t] : it->second)
            if (!s.count(n)) s[n] = s_in.empty() ? t : subst_type(t, s_in);
    // A template from a precompiled module carries its solutions on the decl.
    // `s` now differs from `s_in` by inference solutions only; whether this is
    // an INSTANCE (UNIT_KEY, DECL_RET_TYPE, INSTANCE_OF_BINARY) is `s_in`'s.
    fn.each_infer_subst([&](lir_view::LParamView p) {
        std::string n(p.name());
        TypeRef t = p.type(out_.type_pool.impl());
        if (t && !s.count(n)) s[n] = s_in.empty() ? t : subst_type(t, s_in);
    });
    cur_packs_ = packs;  // make available to subst_expr
    // Stage E: read the template via its FunctionView mirror. Type reads MUST
    // use out_.type_pool.impl() — mono moved in_.type_pool into out_ at run()
    // start, so in_ is moved-from (the same gotcha that bit clone_enum_def).
    const TypePoolImpl* pool = out_.type_pool.impl();
    // Direct-build: write the substituted fn mirror STRAIGHT into out_.
    DeclBuilder nf(out_, lir_schema::decl::Code::Func, /*cap=*/40);
    nf.str_always(dk::NAME,         fn.name());
    nf.str(dk::METHOD_BASE,         fn.method_base());
    nf.str(dk::PKG,                 fn.package());
    // DWARF: the instance keeps its template's file — without this every
    // mono'd fn loses SOURCE_FILE and debug info collapses to main_source_.
    nf.str(dk::SOURCE_FILE,         fn.source_file());
    // UnitGraph §1.2/§1.3. clone_fn is ALSO the verbatim-copy path (empty
    // SubstMap), so "the key is gone after mono" would have meant EVERY
    // function, not just synthesized instances — and the whole partition would
    // silently collapse into Common. (It did: measured 0 of 10748 attributed.)
    //
    // The distinction mono actually holds is the SubstMap: empty = this is the
    // same declared function, carried through, so its declared owner carries
    // with it. Non-empty = a synthesized INSTANCE nobody declared, whose owner
    // must be derived from its referrers. Dropping the key is how it says so.
    if (s_in.empty())        nf.str(dk::UNIT_KEY, fn.unit_key());
    if (fn.is_extern())   nf.flag(dk::IS_EXTERN, true);
    // Propagate visibility onto the instance: a `pub` template's instantiation
    // is itself part of the public ABI surface. Nothing in codegen keys on this
    // flag (mlir_gen emits every reachable instance regardless); it is consumed
    // only by the `--emit-abi` pub-allowlist sidecar, which must see monomorphised
    // pub-generic symbols (e.g. `Vec<T>::push` instances) or it would silently
    // drop real public symbols. See emit_module's `.abi-pub` emission.
    if (fn.is_pub())      nf.flag(dk::IS_PUB, true);
    // Carry the metaprog macro-hook marker (#[fn_macro]/#[token_macro]) through
    // mono so the `--emit-abi` pub-allowlist sidecar (which reads the post-mono
    // mirror) still sees it and excludes these compiler-invoked hooks from the
    // public ABI surface. Even non-generic hooks pass through clone_fn, so
    // without this the flag set in lower_fn would be dropped here.
    if (fn.is_macro_hook()) nf.flag(dk::IS_MACRO_HOOK, true);
    nf.i64_if(dk::LOCAL_COUNT, (int64_t)fn.local_count());  // Phase-1: preserve slot count.
    if (fn.is_vararg())   nf.flag(dk::IS_VARARG, true);
    // Never propagate from_binary_module to cloned functions: clone_fn is
    // called by mono to create instantiations, which are new functions not
    // present in the binary archive. The archive contains only the pre-compiled
    // non-generic originals (identified via LProgram::binary_symbols in mlir_gen).
    // Phase 6 (multi-arena IR): from_lazy_module IS propagated. The lazy
    // archive ships only parsed AST, so cloned items are still "originating
    // from a lazy module" — their bodies need the same reach-based emit
    // filter that mlir_gen applies to the originals.
    if (fn.from_lazy_module()) nf.flag(dk::FROM_LAZY_MODULE, true);
    // An instance of a binary module's generic template: the template was
    // checked by its library's pre-mono pass, which checks exactly the
    // functions with type parameters, their own or their impl's. A variadic
    // instance is not covered.
    if (fn.from_binary_module() && !s_in.empty() && packs.empty() &&
        (!fn.type_params_empty() || !fn.impl_type_params_empty()))
        nf.flag(dk::INSTANCE_OF_BINARY, true);
    nf.type(dk::RET_TYPE, subst_type(fn.ret_type(pool), s));
    // ADR 0028: keep the signature as DECLARED (before substitution) for the
    // borrow checker's elision; a clone of a clone keeps the first one.
    if (!s_in.empty()) nf.type(dk::DECL_RET_TYPE, localize_type(fn.decl_ret_type(pool)));
    // B65: lifetime params + outlives bounds are preserved verbatim through
    // mono. Lifetime substitution is identity (lifetimes are not in the
    // SubstMap), so the original pairs remain valid on the cloned signature.
    {
        auto lps = fn.lifetime_params();
        if (!lps.empty()) {
            auto la = nf.array(dk::LIFETIME_PARAMS);
            for (auto lp : lps) la.push_str(lp);
        }
        auto los = fn.lifetime_outlives();
        // A TYPE PARAMETER'S lifetime bound (`T: 'static`) rides along as the
        // pair ("T:<name>", lt): the instance keeps `T` in its declared
        // parameter types (the borrow checker labels those regions `T:<name>:k`)
        // but loses its type parameters, and with them the bound (#462).
        std::vector<std::pair<std::string, std::string>> tp_los;
        if (!s_in.empty())
            for (auto tp : fn.type_params())
                for (auto lt : tp.lifetime_outlives())
                    tp_los.emplace_back("T:" + std::string(tp.name()), std::string(lt));
        if (!los.empty() || !tp_los.empty()) {
            auto lo = nf.array(dk::LIFETIME_OUTLIVES);
            for (auto& [a, b] : los) { lo.push_str(a); lo.push_str(b); }
            for (auto& [a, b] : tp_los) { lo.push_str(a); lo.push_str(b); }
        }
    }
    {
        auto pa = nf.array(dk::PARAMS);
        for (auto p : fn.params()) {
            if (p.is_variadic()) {
                // Expand variadic param into N concrete params.
                // Find the pack type for this param's TypeVar name.
                std::string pack_name;
                TypeRef pt = p.type(pool);
                if (pt && pt.kind() == LogosType::Kind::TypeVar)
                    pack_name = std::string(pt.type_var_name());
                auto pit = packs.find(pack_name);
                if (pit != packs.end()) {
                    for (size_t i = 0; i < pit->second.size(); ++i) {
                        auto expanded_name = make_pack_arg_name(std::string(p.name()), i);
                        // Phase 5.C: localize foreign pack entries so the
                        // cloned fn's params don't hold offsets into a remote
                        // arena (mlir_gen reads via TypeRef accessors which
                        // work cross-arena, but downstream sites that route
                        // TypeRef through mirror writes — e.g. emit_function
                        // header / signature mangling — assume local pool).
                        pa.push_param({expanded_name, localize_type(pit->second[i])});
                    }
                }
            } else {
                pa.push_param({std::string(p.name()), subst_type(p.type(pool), s),
                               p.is_variadic(), p.owning_box_dyn(), p.slot(),
                               s_in.empty() ? TypeRef{} : localize_type(p.decl_type(pool))});
            }
        }
    }
    // Phase 5.B step 2: cross-arena body source. When sema skipped this fn's
    // body (Phase 4.A — non-generic from_binary; Phase 5.B step 3 also
    // skips generic templates from binary), body_external_ref points at the
    // body's mirror in a foreign arena (stdlib's published EXPORTS). Resolve
    // it, set src_arena_ for the duration of the walk so view helpers route
    // refs through the foreign arena, then restore.
    //
    // Falls back to the local body_ref when body_external_ref is INVALID
    // (the legacy path: body was lowered locally by this run's sema).
    lir_view::BlockRef src_body;
    const writ::Arena* saved_src_arena = src_arena_;
    auto body_ext = fn.body_external_ref();
    if (body_ext.arena_id().is_valid()) {
        auto resolved = writ::resolve_external_ref(body_ext);
        if (resolved.ok()) {
            src_arena_ = &resolved.mem->arena();
            src_body = lir_view::BlockRef(
                src_arena_, resolved.offset(),
                body_ext.arena_id());
        }
    }
    if (!src_body) {
        src_body = fn.body();
    }
    // A template from a precompiled module: its body carries the solutions.
    SubstMap sb = s;
    if (src_body) {
        auto sav = src_body.mirror()->get(lir_schema::stmt_keys::INFER_SUBSTS.code);
        if (!sav.is_null()) {
            auto* arr = sav.as_ptr<const writ::ObjectArray>();
            for (uint64_t i = 0; i < arr->size(); ++i) {
                auto el = arr->get(i);
                if (el.is_null()) continue;
                lir_view::LParamView p{lir_view::detail::make_sub_ref<lir_view::DeclRef>(src_body, el)};
                std::string n(p.name());
                TypeRef t = p.type(pool);
                if (t && !sb.count(n)) sb[n] = s_in.empty() ? localize_type(t) : subst_type(localize_type(t), s_in);
            }
        }
    }
    nf.block(dk::BODY, subst_block(src_body, sb, packs));
    src_arena_ = saved_src_arena;
    // type_params left empty: instantiated functions are monomorphic
    return nf;
}


// ── Signature-only clone for binary_symbols fast path ──────────
//
// Copies the function signature (params/return/flags) and substitutes
// TypeVars on the surface types, but leaves body empty. mlir_gen needs
// the signature for forward_declare; the body is in liblstdlib.a and
// would be skipped anyway. Caller must not run lir_mirror_emit_function
// or scan_fn on the result — body.mirror_ptr_ stays default-zero, so
// scan_fn's mirror_offset guard short-circuits if accidentally invoked.
DeclBuilder Mono::clone_fn_signature(lir_view::FunctionView fn,
                                         const SubstMap& s,
                                         const PackMap& packs) {
    namespace dk = lir_schema::decl_keys;
    cur_packs_ = packs;
    const TypePoolImpl* pool = out_.type_pool.impl();  // see clone_fn (moved-pool)
    DeclBuilder nf(out_, lir_schema::decl::Code::Func, /*cap=*/40);
    nf.str_always(dk::NAME,         fn.name());
    nf.str(dk::METHOD_BASE,         fn.method_base());
    nf.str(dk::PKG,                 fn.package());
    // DWARF: the instance keeps its template's file — without this every
    // mono'd fn loses SOURCE_FILE and debug info collapses to main_source_.
    nf.str(dk::SOURCE_FILE,         fn.source_file());
    // UnitGraph §1.2/§1.3. clone_fn is ALSO the verbatim-copy path (empty
    // SubstMap), so "the key is gone after mono" would have meant EVERY
    // function, not just synthesized instances — and the whole partition would
    // silently collapse into Common. (It did: measured 0 of 10748 attributed.)
    //
    // The distinction mono actually holds is the SubstMap: empty = this is the
    // same declared function, carried through, so its declared owner carries
    // with it. Non-empty = a synthesized INSTANCE nobody declared, whose owner
    // must be derived from its referrers. Dropping the key is how it says so.
    if (s.empty())        nf.str(dk::UNIT_KEY, fn.unit_key());
    if (fn.is_extern())   nf.flag(dk::IS_EXTERN, true);
    if (fn.is_pub())      nf.flag(dk::IS_PUB, true);  // ABI pub-allowlist — see clone_fn.
    if (fn.is_macro_hook()) nf.flag(dk::IS_MACRO_HOOK, true);  // ABI pub-allowlist — see clone_fn.
    nf.i64_if(dk::LOCAL_COUNT, (int64_t)fn.local_count());  // Phase-1: preserve slot count.
    if (fn.is_vararg())   nf.flag(dk::IS_VARARG, true);
    if (fn.from_lazy_module()) nf.flag(dk::FROM_LAZY_MODULE, true);  // Phase 6 — see clone_fn.
    nf.type(dk::RET_TYPE, subst_type(fn.ret_type(pool), s));
    if (!s.empty()) nf.type(dk::DECL_RET_TYPE, localize_type(fn.decl_ret_type(pool)));
    {
        auto lps = fn.lifetime_params();
        if (!lps.empty()) {
            auto la = nf.array(dk::LIFETIME_PARAMS);
            for (auto lp : lps) la.push_str(lp);
        }
        auto los = fn.lifetime_outlives();
        // A TYPE PARAMETER'S lifetime bound (`T: 'static`) rides along as the
        // pair ("T:<name>", lt): the instance keeps `T` in its declared
        // parameter types (the borrow checker labels those regions `T:<name>:k`)
        // but loses its type parameters, and with them the bound (#462).
        std::vector<std::pair<std::string, std::string>> tp_los;
        if (!s.empty())
            for (auto tp : fn.type_params())
                for (auto lt : tp.lifetime_outlives())
                    tp_los.emplace_back("T:" + std::string(tp.name()), std::string(lt));
        if (!los.empty() || !tp_los.empty()) {
            auto lo = nf.array(dk::LIFETIME_OUTLIVES);
            for (auto& [a, b] : los) { lo.push_str(a); lo.push_str(b); }
            for (auto& [a, b] : tp_los) { lo.push_str(a); lo.push_str(b); }
        }
    }
    {
        auto pa = nf.array(dk::PARAMS);
        for (auto p : fn.params()) {
            if (p.is_variadic()) {
                std::string pack_name;
                TypeRef pt = p.type(pool);
                if (pt && pt.kind() == LogosType::Kind::TypeVar)
                    pack_name = std::string(pt.type_var_name());
                auto pit = packs.find(pack_name);
                if (pit != packs.end()) {
                    for (size_t i = 0; i < pit->second.size(); ++i) {
                        auto expanded_name = make_pack_arg_name(std::string(p.name()), i);
                        // Phase 5.C: localize foreign pack entries (see
                        // matching site in clone_fn for rationale).
                        pa.push_param({expanded_name, localize_type(pit->second[i])});
                    }
                }
            } else {
                pa.push_param({std::string(p.name()), subst_type(p.type(pool), s),
                               p.is_variadic(), p.owning_box_dyn(), p.slot(),
                               s.empty() ? TypeRef{} : localize_type(p.decl_type(pool))});
            }
        }
    }
    return nf;
}


// ── Struct monomorphization ───────────────────────────────────


// Best-effort NAME → TypeRef for the auto-trait shape predicates above:
// primitives by spelling; instantiated/plain structs and enums through the
// registries mono already maintains. Null for anything else (tuples, refs —
// their canonical names aren't keys here), which the predicate treats as
// "not satisfied" — conservative, and sema's bound check (TypeRef-based)
// remains the authoritative error surface.
TypeRef Mono::mono_typeref_by_name_(const std::string& n) {
    auto prim = [&](LogosType::Kind k) -> TypeRef {
        LogosTypeBuilder b; b.kind = k;
        return out_.type_pool.alloc(std::move(b));
    };
    if (n == "i8")   return prim(LogosType::Kind::I8);
    if (n == "i16")  return prim(LogosType::Kind::I16);
    if (n == "i32")  return prim(LogosType::Kind::I32);
    if (n == "i64")  return prim(LogosType::Kind::I64);
    if (n == "i128") return prim(LogosType::Kind::I128);
    if (n == "u8")   return prim(LogosType::Kind::U8);
    if (n == "u16")  return prim(LogosType::Kind::U16);
    if (n == "u32")  return prim(LogosType::Kind::U32);
    if (n == "u64")  return prim(LogosType::Kind::U64);
    if (n == "u128") return prim(LogosType::Kind::U128);
    if (n == "f32")  return prim(LogosType::Kind::F32);
    if (n == "f64")  return prim(LogosType::Kind::F64);
    if (n == "bool") return prim(LogosType::Kind::Bool);
    if (n == "char") return prim(LogosType::Kind::Char);
    if (n == "usize") return prim(LogosType::Kind::Usize);
    if (n == "isize") return prim(LogosType::Kind::Isize);
    if (auto it = concrete_struct_types_.find(n); it != concrete_struct_types_.end())
        return it->second;
    auto struct_ref = [&](lir_view::StructView sd) -> TypeRef {
        LogosTypeBuilder b;                       // the ONE mono struct/enum
        b.kind = sd.is_zoned() ? LogosType::Kind::ZonedStruct
                               : LogosType::Kind::Struct;   // name→TypeRef site
        b.struct_name = n;
        b.pkg_name = std::string(sd.pkg());
        return out_.type_pool.alloc(std::move(b));
    };
    for (auto& sd : out_.structs) if (sd.name() == n) return struct_ref(sd);
    for (auto& sd : in_.structs)  if (sd.name() == n) return struct_ref(sd);
    for (auto& ed : out_.enums)
        if (ed.name() == n) {
            LogosTypeBuilder b;
            b.kind = LogosType::Kind::Enum;
            b.enum_name = n;
            b.pkg_name = std::string(ed.pkg());
            return out_.type_pool.alloc(std::move(b));
        }
    return TypeRef{};
}

// L1.4: bound-gate, factored from clone_struct_def's method loop. Returns
// false when any of method `m`'s impl_type_params bounds is unsatisfied
// under substitution `s`.
//
// Sprint 5.6: implementation now routes through trait_engine_. The
// `seen` parameter is kept for source compatibility but ignored —
// the engine has its own per-query cycle guard. Existing call sites
// pass a StrSet by reference; we don't break them.
void Mono::build_bare_trait_index_() {
    if (bare_trait_ids_built_) return;
    bare_trait_ids_built_ = true;
    for (auto td : out_.traits) {
        std::string n(td.name());
        if (n.empty()) continue;
        std::string pkg(td.pkg());
        if (pkg.empty()) continue;              // bare itself is appended below
        std::string id = pkg + "::" + n;
        auto& v = bare_trait_ids_[n];
        if (std::find(v.begin(), v.end(), id) == v.end()) v.push_back(id);
    }
}

std::vector<std::string> Mono::bare_trait_identities_(const std::string& bare) {
    build_bare_trait_index_();
    std::vector<std::string> out;
    if (auto it = bare_trait_ids_.find(bare); it != bare_trait_ids_.end())
        out = it->second;
    // The spelling itself stays last: a trait with no PKG, or one that never
    // reached out_.traits (binary-only decls), is still answerable.
    out.push_back(bare);
    return out;
}



// `impl Trait for &T` / `&mut T` registers under collect_impl's
// `$ref_`/`$mut_ref_` mangling, which is STRUCTURE-aware (struct pointee →
// `$ref_<Name>`; any other pointee → `$ref_<type_str(whole-ref)>`, keeping
// the `&`). We must mirror that from the TypeRef — string-munging the raw
// `type_str` (`&&i32`) is unsound: stripping one `&` collides `&&i32` onto
// `&i32`'s key. Returns "" for non-ref types.
std::string Mono::ref_target_key(TypeRef t) {
    TypeRef ct{t};
    if (ct.kind() != LogosType::Kind::Ref &&
        ct.kind() != LogosType::Kind::MutRef)
        return {};
    std::string pfx = (ct.kind() == LogosType::Kind::MutRef) ? "$mut_ref_" : "$ref_";
    TypeRef pt = ct.pointee();
    if (pt && (TypeRef(pt).kind() == LogosType::Kind::Struct ||
               TypeRef(pt).kind() == LogosType::Kind::ZonedStruct))
        return pfx + concrete_struct_name(pt);
    return pfx + type_str(ct);
}

// See header comment. Steps:
//   1) Quick reject via mono_has_impl_recursive on the stripped
//      concrete name. If no impl at all matches, false.
//   2) If concrete has no type-args (primitive / non-generic struct /
//      enum), the stripped-name check is sufficient — return true.
//   3) Otherwise, find an impl in out_.impls matching trait + bare
//      target_type. Unify its target_typeref against `concrete` to
//      extract a TypeVar→arg substitution. For each impl_type_param,
//      recursively check every bound against the substituted arg.
//   4) If any impl satisfies all its bounds against the concrete's
//      type-args, return true. Otherwise false.
const obl::ImplTable& Mono::obl_table_now_() {
    if (out_.impls.size() == obl_table_n_) return obl_table_;
    obl_table_ = {};
    obl_memo_.clear();
    obl_table_n_ = out_.impls.size();
    const TypePoolImpl* pool = out_.type_pool.impl();
    uint32_t src = 0;
    for (auto& impl : out_.impls) {
        obl::ImplFact f;
        f.source = src++;
        f.trait = std::string(impl.identity_trait().empty() ? impl.trait_name() : impl.identity_trait());
        f.self = impl.self_type(pool);
        if (!f.self) f.self = impl.target_typeref(pool);
        if (!f.self || f.trait.empty()) continue;
        f.trait_args = impl.trait_type_args(pool);
        impl.each_impl_type_param([&](lir_view::FnTParamView tp) {
            f.generics.emplace_back(tp.name());
            if (tp.is_variadic()) f.pack = std::string(tp.name());
            tp.each_bound([&](lir_view::FnTraitBoundView b) {
                std::string id(b.identity_trait().empty() ? b.trait_name() : b.identity_trait());
                f.bounds.push_back({std::string(tp.name()), std::move(id), b.type_args(pool)});
            });
        });
        f.negative = impl.is_negative();
        impl.each_assoc_type([&](lir_view::AssocEntryView ae) {
            f.assoc_types.emplace_back(std::string(ae.name()), ae.type(pool));
        });
        // Logos's `impl Tr for str` (Self `[u8]`): also a fact about `&[u8]`,
        // which answers only when nothing else does (impl_candidates_); Copy /
        // Clone need `Sized`, so for them only that one (see sema's obl_str_facts_).
        if (f.generics.empty() && TypeRef(f.self).kind() == LogosType::Kind::UnsizedSlice &&
            TypeRef(f.self).elem() && TypeRef(TypeRef(f.self).elem()).kind() == LogosType::Kind::U8) {
            const auto& env = obl_env_();
            f.fallback = true;
            obl::ImplFact by_ref = f;
            LogosTypeBuilder sl;
            sl.kind = LogosType::Kind::Slice;
            sl.elem = TypeRef(f.self).elem();
            by_ref.self = out_.type_pool.alloc(sl);
            obl_table_.add(std::move(by_ref));
            if (f.trait == env.lang.copy || f.trait == env.lang.clone) continue;
        }
        obl_table_.add(std::move(f));
    }
    return obl_table_;
}

const obl::Env& Mono::obl_env_() {
    if (obl_env_cache_) return *obl_env_cache_;
    obl_env_cache_.emplace();
    obl::Env& e = *obl_env_cache_;
    e.lang = obl::LangIds::active();
    e.auto_trait = [this](TypeRef self, std::string_view trait) -> std::optional<bool> {
        for (auto& td : out_.traits) {
            if (!td.is_auto()) continue;
            std::string id = td.pkg().empty() ? std::string(td.name()) : std::string(td.pkg()) + "::" + std::string(td.name());
            if (id != trait) continue;
            StrSet v;
            return is_auto_satisfied(self, td.name(), v);
        }
        return std::nullopt;
    };
    e.subst = [this](TypeRef t, const obl::Subst& s) {
        SubstMap m;
        for (auto& [k, v] : s) m[k] = v;
        return subst_type(t, m);
    };
    e.is_open = [](TypeRef t) { return contains_typevar(t); };
    e.mentions_tv = [](TypeRef t) { return contains_typevar(t); };
    e.unsized_of = [this](TypeRef s) {
        LogosTypeBuilder us;
        us.kind = LogosType::Kind::UnsizedSlice;
        us.elem = TypeRef(s).elem();
        return out_.type_pool.alloc(us);
    };
    return e;
}

// The impls C-OBL selects for `self: trait<trait_args>`. A bare spelling asks
// every trait of that name. Logos's nominal `impl … for str` facts answer
// only when no other impl does (ImplFact::fallback).
std::unordered_set<uint32_t> Mono::impl_candidates_(std::string_view trait, TypeRef self,
                                                    const std::vector<TypeRef>* trait_args) {
    std::unordered_set<uint32_t> out;
    if (!self) return out;
    std::vector<std::string> ids;
    if (trait.find("::") != std::string_view::npos) ids.emplace_back(trait);
    else ids = bare_trait_identities_(std::string(trait));
    const std::vector<TypeRef> args = trait_args ? *trait_args : std::vector<TypeRef>{};
    for (auto& id : ids)
        for (auto& s : obl::candidates(obl_table_now_(), obl_env_(), id, self, args)) out.insert(s.impl->source);
    return out;
}

// `<self as trait<args>>::name` by C-OBL (a bare spelling asks every trait of
// that name); null when no impl answers or two answer differently.
TypeRef Mono::project_assoc_(std::string_view trait, TypeRef self, const std::vector<TypeRef>& args,
                             std::string_view name) {
    if (!self || contains_typevar(self)) return {};
    std::vector<std::string> ids;
    if (trait.find("::") != std::string_view::npos) ids.emplace_back(trait);
    else ids = bare_trait_identities_(std::string(trait));
    for (auto& id : ids)
        if (auto r = obl::project(obl_table_now_(), obl_env_(), id, self, args, name)) return *r;
    return {};
}

// ADR 0030 S9 row 4: every mono gate's bound is C-OBL's question
// (obligation.hpp). A bare spelling (`Ord`) asks every trait of that name, as
// the deleted engine did. The deleted engine answered no for real impls (a raw
// pointer's `Eq`, `logos.lang.str::Bytes: Iterator`), yes for a homonym's (a
// user `String` by the stdlib `String`'s `Clone`), and no for `&[u8]: Clone`.
bool Mono::mono_concrete_satisfies_bound(const TraitQuery& q,
                                         TypeRef concrete,
                                         StrSet&) {
    if (!concrete) return false;
    const auto& table = obl_table_now_();
    std::string key = q.key();
    key += '\x1f';
    key += type_str(concrete);
    if (auto it = obl_memo_.find(key); it != obl_memo_.end()) return it->second;
    obl::Selection sel = obl::select(table, obl_env_(), q.key(), concrete, {});
    if (!sel.holds() && !q.has_identity)
        for (auto& id : bare_trait_identities_(q.spelling)) {
            sel = obl::select(table, obl_env_(), id, concrete, {});
            if (sel.holds()) break;
        }
    return obl_memo_[std::move(key)] = sel.holds();
}

bool Mono::method_bound_ok(lir_view::FunctionView m, const SubstMap& s) {
    auto* mbo_pool = out_.type_pool.impl();
    // §8.5: type-EXPRESSION where-bounds (`fn max() where Item: Ord` on
    // `impl<T> Iterator<&T> for VecIter<T>` → subject `&T`). Substitute the
    // subject with this clone's args and check satisfaction. This is the
    // gate sema deferred for compound Items: it admits `&i32: Ord` (VecIter)
    // and rejects `EnumPair<i32>: Ord` / `[i32;0]: Ord` (EnumIter /
    // ArrayChunksIter) so their `max`/`min` are never synthesised.
    bool wbad = false;
    m.each_where_bound([&](lir_view::FnWhereBoundView wb) {
        if (wbad) return;
        TypeRef subj = subst_type(wb.subject(mbo_pool), s);
        if (!subj) return;
        // Still-abstract after subst (nested call where the impl param is
        // itself a TypeVar): defer — an outer mono pass resolves it.
        if (contains_typevar(subj)) return;
        StrSet seen;
        if (!mono_concrete_satisfies_bound(std::string(wb.trait()), subj, seen)) wbad = true;
    });
    if (wbad) return false;
    for (auto& itp : m.impl_type_params()) {
        if (itp.bounds_empty()) continue;
        auto sit = s.find(std::string(itp.name()));
        if (sit == s.end()) continue;
        TypeRef concrete = sit->second;
        if (!concrete) continue;
        std::string cname;
        if (TypeRef(concrete).kind() == LogosType::Kind::Struct ||
            TypeRef(concrete).kind() == LogosType::Kind::ZonedStruct)
            cname = concrete_struct_name(concrete);
        else if (TypeRef(concrete).kind() == LogosType::Kind::Enum)
            cname = TypeRef(concrete).enum_name();
        else if (TypeRef(concrete).kind() == LogosType::Kind::Tuple)
            // Tuple impls key on `$tuple$N` (see mono_concrete_satisfies_bound);
            // keeps the HRTB impl lookup below consistent for tuple concretes.
            cname = "$tuple$" +
                    std::to_string(TypeRef(concrete).tuple_elems().size());
        else
            cname = type_str(concrete);
        if (auto p = cname.find("$G"); p != std::string::npos)
            cname = cname.substr(0, p);
        // Cycle-guard + per-attempt seen semantics live in
        // mono_has_impl_recursive (factored for reuse at mono_subst.cpp's
        // assoc-type fallback).
        // Deeper variant: when the concrete type has type-args, the
        // bare-name lookup above can lie ("Vec impls Debug" without
        // checking T satisfies its own bound). Recurse via
        // mono_concrete_satisfies_bound; see
        // [[baghunt-mono-blanket-bound-recursion]].
        auto concrete_has_impl = [&](const TraitQuery& q) {
            StrSet seen;
            return mono_concrete_satisfies_bound(q, concrete, seen);
        };
        struct MBound {
            std::string trait_name;
            // The bound's always-qualified trait identity (TB_IDENTITY), so a
            // bound checked here asks for ONE trait's facts. The raw spelling
            // is kept beside it because auto-trait lookup and the trait-decl
            // scan below are keyed by the DECLARED name.
            std::string identity;
            bool is_fn_family;
            std::vector<TypeRef> type_args;
            std::vector<std::string> hrtb_binders;
        };
        std::vector<MBound> itp_bounds;
        itp.each_bound([&](lir_view::FnTraitBoundView tbv) {
            MBound mb;
            mb.trait_name = std::string(tbv.trait_name());
            mb.identity   = std::string(tbv.identity_trait());
            mb.is_fn_family = tbv.is_fn_family();
            mb.type_args = tbv.type_args(mbo_pool);
            for (auto b : tbv.hrtb_binders()) mb.hrtb_binders.push_back(std::string(b));
            itp_bounds.push_back(std::move(mb));
        });
        for (auto& tb : itp_bounds) {
            // Fn / FnMut / FnOnce parenthesized bounds are compiler-
            // intrinsic — satisfied by any fn-pointer or closure type
            // (per sema_collect.cpp:982; mono's trait engine has no
            // FnMut-for-fn-ptr impl registered). Without this short-
            // circuit method_bound_ok returns false and the impl
            // method silently disappears from dispatch. See
            // [[baghunt-mapiter-fn-param-mono-loop]].
            //
            // TypeVar passes too: this happens in nested generic
            // calls (e.g. `reduce` → `fold` where F is still
            // `ReduceFn` TypeVar at fold's mono-enqueue site —
            // the outer reduce's own mono will resolve it later);
            // struct-with-Fn-impl also accepted (see Deferred-2
            // bridge in the ClosureCall handler).
            if (tb.is_fn_family) {
                auto k = TypeRef(concrete).kind();
                if (LogosType::is_fn_value_kind(k) ||
                    k == LogosType::Kind::Closure ||
                    k == LogosType::Kind::TypeVar ||
                    k == LogosType::Kind::Struct ||
                    k == LogosType::Kind::ZonedStruct)
                    continue;
                return false;
            }
            bool is_auto = false;
            for (auto& td : out_.traits)
                if (td.name() == tb.trait_name) { is_auto = td.is_auto(); break; }
            if (is_auto) {
                StrSet visited;
                if (!is_auto_satisfied(concrete, tb.trait_name, visited))
                    return false;
                continue;
            }
            // Asks with BOTH spellings: the fact tables by IDENTITY, the
            // legacy out_.impls scans and the auto-trait names by the WRITTEN
            // name. An earlier attempt narrowed this to the identity ALONE and
            // broke the stdlib build —
            //   'Vec$G1$tup$3$slice_u8$i64$slice_u8__fmt' does not reference a
            //   valid function
            // — because those scans still compared spellings, so the query went
            // unanswered and a legitimate method was DROPPED, the same
            // false-negative direction the bare aliases used to hedge. The
            // tables were identity-keyed FIRST (TraitQuery::matches); only then
            // could this carry both.
            if (!concrete_has_impl(TraitQuery(tb.trait_name, tb.identity)))
                return false;
            // B62/B63: HRTB satisfaction — universal-position + bijectivity
            // checks. Bound binders (any non-empty, non-'static lifetime in
            // type_args) must align with impl-level lifetime params, and the
            // skolem↔impl-region mapping must be 1-1. See sema_collect.cpp's
            // region_ok for the full rule.
            if (!tb.type_args.empty()) {
                lir_view::ImplView ib{};
                for (auto& cand : out_.impls) {
                    if (cand.trait_name() == tb.trait_name &&
                        cand.target_type() == cname) { ib = cand; break; }
                }
                auto ib_tta = ib ? ib.trait_type_args(mbo_pool) : std::vector<TypeRef>{};
                auto ib_lt_params = ib ? ib.impl_lifetime_params() : std::vector<std::string_view>{};
                auto ib_outlives = ib ? ib.lifetime_outlives()
                                      : std::vector<std::pair<std::string_view, std::string_view>>{};
                if (ib && !ib_tta.empty()) {
                    std::unordered_map<std::string, std::string> i2s;
                    auto univ = [&](const std::string& lt) {
                        for (auto& nm : ib_lt_params)
                            if (nm == lt) return true;
                        return false;
                    };
                    auto unify = [&](const std::string& blt,
                                     const std::string& ilt) -> bool {
                        if (blt.empty() || blt == "static" || blt == "'static") {
                            if (ilt == blt) return true;
                            if (univ(ilt)) return true;
                            return false;
                        }
                        if (!univ(ilt)) return false;
                        auto b = i2s.emplace(ilt, blt);
                        if (!b.second && b.first->second != blt) return false;
                        return true;
                    };
                    std::function<bool(TypeRef, TypeRef)> walk =
                        [&](TypeRef bt, TypeRef it) -> bool {
                        if (!bt || !it) return true;
                        bool b_ref = bt.kind() == LogosType::Kind::Ref ||
                                     bt.kind() == LogosType::Kind::MutRef;
                        bool i_ref = it.kind() == LogosType::Kind::Ref ||
                                     it.kind() == LogosType::Kind::MutRef;
                        if (b_ref && i_ref) {
                            if (!unify(std::string(bt.lifetime()),
                                       std::string(it.lifetime()))) return false;
                            return walk(bt.pointee(), it.pointee());
                        }
                        if (bt.kind() != it.kind()) return true;
                        if (bt.kind() == LogosType::Kind::Struct ||
                            bt.kind() == LogosType::Kind::ZonedStruct ||
                            bt.kind() == LogosType::Kind::Enum) {
                            auto blts = bt.lifetime_args();
                            auto ilts = it.lifetime_args();
                            size_t nl = std::min(blts.size(), ilts.size());
                            for (size_t i = 0; i < nl; ++i)
                                if (!unify(blts[i], ilts[i])) return false;
                            auto bts = bt.type_args();
                            auto its = it.type_args();
                            size_t nt = std::min(bts.size(), its.size());
                            for (size_t i = 0; i < nt; ++i)
                                if (!walk(bts[i], its[i])) return false;
                        }
                        return true;
                    };
                    size_t n = std::min(tb.type_args.size(),
                                        ib_tta.size());
                    for (size_t i = 0; i < n; ++i)
                        if (!walk(TypeRef(tb.type_args[i]),
                                  TypeRef(ib_tta[i]))) return false;
                    // B85: skolemization-aware impl-where check. After the
                    // unify pass, i2s maps each impl-lt that's bound to a
                    // bound-side lifetime. If the impl has a where-clause
                    // outlives `'a: 'b` and BOTH 'a and 'b are mapped to
                    // bound-side binder lifetimes (i.e. skolems), the
                    // constraint is unsatisfiable under universal quant —
                    // reject. (If one side is mapped to 'static or to a
                    // caller-named lifetime, the outlives may be discharged
                    // elsewhere; conservatively allow.)
                    auto is_binder_lt = [&](const std::string& lt) -> bool {
                        for (auto& b : tb.hrtb_binders)
                            if (b == lt) return true;
                        return false;
                    };
                    for (auto& [longi, shorti] : ib_outlives) {
                        auto lit = i2s.find(std::string(longi));
                        auto sit = i2s.find(std::string(shorti));
                        if (lit == i2s.end() || sit == i2s.end()) continue;
                        if (lit->second == sit->second) continue;  // refl
                        if (is_binder_lt(lit->second) && is_binder_lt(sit->second))
                            return false;
                    }
                }
            }
        }
    }
    return true;
}

// Clone a struct def with substitution; rename to new_name.
// Method names are rewritten from "Base__method" to "new_name__method".
DeclBuilder Mono::clone_struct_def(lir_view::StructView tmpl,
                                  const SubstMap& s,
                                  const PackMap& packs,
                                  const std::string& new_name) {
    namespace stk = lir_schema::struct_keys;
    // CRITICAL: read template types via out_'s pool — mono moves in_.type_pool
    // into out_ at run() start, so in_'s pool is moved-from. (Same gotcha that
    // bit clone_enum_def.)
    const TypePoolImpl* pool = out_.type_pool.impl();
    // Stage E direct-build: build the substituted struct mirror STRAIGHT into out_.
    DeclBuilder nd(out_, lir_schema::decl::Code::Struct, /*cap=*/40);
    nd.str_always(stk::NAME, new_name);
    nd.str(stk::PKG, tmpl.pkg());
    if (tmpl.is_pub())          nd.flag(stk::IS_PUB, true);           // ABI pub-scoping — mirrors clone_fn.
    if (tmpl.is_zoned())        nd.flag(stk::IS_ZONED, true);
    bool is_dst = tmpl.is_dst();  // Phase 1B-15: preserved; possibly upgraded below.
    if (tmpl.self_describing())  nd.flag(stk::SELF_DESCRIBING, true);  // Writ: thin-*Self marker preserved.
    if (tmpl.rel_ptr())          nd.flag(stk::REL_PTR, true);          // RefRepr RelOffset marker preserved.
    if (tmpl.borrow_carrying())  nd.flag(stk::BORROW_CARRYING, true);  // WAny escape-tracking marker preserved.
    if (tmpl.zone_mut())         nd.flag(stk::ZONE_MUT, true);         // Writ: fat-`&mut` zone marker preserved.
    if (tmpl.zoned2())           nd.flag(stk::ZONED2, true);           // Writ: auto-relative ptr-field marker preserved.
    if (tmpl.non_null())         nd.flag(stk::NON_NULL, true);         // #[non_null]: Option<T> NullPtr-niche marker preserved.
    if (tmpl.is_union())         nd.flag(stk::IS_UNION, true);         // §6.1: preserved through mono clone.
    // `#[repr(transparent)]`: preserved. It was the ONE layout marker missing
    // from this list, so every generic instance (`UnsafeCell<i32>`) reached
    // mlir-gen as a plain product while sema and mono — which resolve the
    // TEMPLATE and substitute — still read it as transparent. Reported by the
    // ENGINE × SHAPE matrix as a shape disagreement, not by any byte count:
    // a one-member product and a transparent wrapper weigh the same, so the
    // divergence was invisible to a size comparison and would have surfaced the
    // first time transparency meant something other than size.
    if (tmpl.repr_transparent()) nd.flag(stk::REPR_TRANSPARENT, true);
    if (tmpl.no_auto_drop())     nd.flag(stk::NO_AUTO_DROP, true);     // #123: drop suppression survives monomorphisation.
    // is_data_plain defaults true on the (now-deleted) Draft → ALWAYS emitted.
    nd.bool_always(stk::IS_DATA_PLAIN, true);
    // type_params cleared: result is monomorphic.
    // B87: preserve lifetime_params + lifetime_outlives so post-mono
    // dropck can identify "this struct had a lifetime parameter in its
    // template form" — lifetimes are erased at runtime but the markers
    // are needed for soundness checks (Drop touching 'a).
    {
        auto lps = tmpl.lifetime_params();
        if (!lps.empty()) {
            auto la_ = nd.array(stk::LIFETIME_PARAMS);
            for (auto lp : lps) la_.push_str(lp);
        }
        auto los = tmpl.lifetime_outlives();
        if (!los.empty()) {
            auto lo = nd.array(stk::LIFETIME_OUTLIVES);
            for (auto& [a, b] : los) { lo.push_str(a); lo.push_str(b); }
        }
    }
    // Substitute field types; expand variadic packs. Collect into locals first
    // so the DST-inheritance check can inspect the last field's type.
    std::vector<lir::LField> fields;
    for (auto f : tmpl.fields()) {
        if (f.is_variadic()) {
            std::string pack_name;
            TypeRef ft(f.type(pool));
            if (ft && ft.kind() == LogosType::Kind::TypeVar)
                pack_name = std::string(ft.type_var_name());
            auto pit = packs.find(pack_name);
            if (pit != packs.end()) {
                for (size_t i = 0; i < pit->second.size(); ++i) {
                    // Phase 5.C: localize pack entries (see clone_fn).
                    fields.push_back({std::string(f.name()) + "_" + std::to_string(i),
                                      localize_type(pit->second[i]), false, {}});
                }
            }
        } else {
            fields.push_back({std::string(f.name()), subst_type(f.type(pool), s), false, {}});
        }
    }
    // Phase 1B-15: DST inheritance through generic instantiation. If the
    // post-substitution LAST field has UnsizedSlice / UnsizedDyn type
    // (i.e. T bound to an unsized form for the `?Sized` template), the
    // cloned struct becomes a custom-DST.
    if (!fields.empty()) {
        auto last_t = TypeRef(fields.back().type);
        if (last_t && (last_t.kind() == LogosType::Kind::UnsizedSlice ||
                       last_t.kind() == LogosType::Kind::UnsizedDyn)) {
            is_dst = true;
        }
    }
    if (is_dst) nd.flag(stk::IS_DST, true);
    if (!fields.empty()) {
        auto fa = nd.array(stk::FIELDS);
        for (auto& f : fields) fa.push_field(f);
    }
    // METHODS array ALWAYS created (even empty) so in-place appends work later.
    auto ma = nd.array(stk::METHODS);
    // L1.4: in lazy mode, skip eager method cloning. drain_method_worklist
    // will clone methods on demand (from L1.1 call-site hook, L1.2 dispatch
    // pin, L1.3 is_root_pin). The bound gate runs there too.
    // Specialization templates ALWAYS clone eagerly: their TYPE_PARAMS carry
    // the bound-discriminator pattern vars (not lazy-cloneable call-site
    // params), and the lazy on-demand hooks resolve against the BASE
    // template's method map — a lazy-skipped spec's methods would never
    // materialize (the pre-bounds [E]-era spec mirrors had no TYPE_PARAMS,
    // so they eagerly cloned by accident of the emptiness test).
    if (lazy_methods_ && !tmpl.type_params_empty() &&
        !tmpl.is_specialization()) return nd;
    const TypePoolImpl* csd_pool = pool;
    auto tmpl_tparams = tmpl.type_params();
    for (auto m : tmpl.methods()) {
        // Structured impl self-type (`impl<T> Pin<&T>` on `struct Pin<P>`):
        // the impl-level params don't share names with the struct's own, so
        // the struct subst alone leaves them unbound. Bind them by unifying
        // the pattern's args against the concrete struct args; a pattern that
        // does NOT match this instantiation (Pin<&T> method on a Pin<Box<…>>
        // spec) means the method doesn't belong to it — skip the clone.
        SubstMap ms;
        const SubstMap* msel = &s;
        bool pattern_mismatch = false;
        TypeRef m_itp = m.impl_target_pattern(csd_pool);
        if (m_itp) {
            auto pa = TypeRef(m_itp).type_args();
            if (!pa.empty() && pa.size() == tmpl_tparams.size()) {
                ms = s;
                bool extended = false;
                for (size_t i = 0; i < pa.size(); ++i) {
                    auto cit = s.find(std::string(tmpl_tparams[i].name()));
                    if (cit == s.end() || !cit->second) continue;
                    if (contains_typevar(cit->second)) continue;  // defer
                    if (contains_assoc_type(pa[i]))
                        continue;  // projection — not structurally decidable
                    SubstMap b;
                    if (!unify_impl_target(cit->second, pa[i], b)) {
                        pattern_mismatch = true;
                        break;
                    }
                    for (auto& [bk, bv] : b)
                        if (!ms.count(bk)) { ms[bk] = bv; extended = true; }
                }
                if (extended && !pattern_mismatch) msel = &ms;
            }
        }
        if (pattern_mismatch) continue;
        if (!method_bound_ok(m, *msel)) continue;

        // Compute the final renamed method name first so the binary-symbol
        // fast path below can consult it.
        std::string final_name = std::string(m.name());
        {
            std::string mn = std::string(m.name());
            std::string mn_pkg;
            if (auto dot = mn.rfind('.'); dot != std::string::npos) {
                mn_pkg = mn.substr(0, dot);
                mn = mn.substr(dot + 1);
            }
            // ⚠ SEPARATOR CLASS. `rest = mn.substr(mn.find("__"))` re-derives
            // the owner/method boundary from the spelling. For a template
            // whose name ends in `_` the first `__` falls INSIDE the owner:
            // `arr___pick` → rest `___pick`, and the instance was emitted as
            // `arr_$G1$i64___pick` — one underscore too many, so nothing ever
            // called it (measured: SIGSEGV, compile exit 0). BOTH parts are
            // carried here: `tmpl.name()` is the owner and `m.method_base()`
            // is the method. Recompose and compare.
            std::string_view mb = m.method_base();
            std::string rest;
            bool composed = false;
            std::string_view own_bare = mname::split_pkg(tmpl.name()).second;
            if (!mb.empty()) {
                if (auto tail = mname::sig_of(mn, own_bare, mb)) {
                    rest = "__" + std::string(mb) + std::string(*tail);
                    composed = true;
                } else if (auto om = mname::split_by_method(mn, mb)) {
                    // The symbol's owner is not the template's own name (a
                    // blanket-impl host, a re-hosted spec fn). The METHOD is
                    // still carried, so the boundary is still not guessed.
                    rest = "__" + std::string(om->method) + std::string(om->tail);
                    composed = true;
                }
            }
            if (!composed) {
                // No METHOD_BASE at all. Anchored fallback — a GUESS, kept only
                // so pre-existing best-effort behaviour survives.
                if (auto sep = mn.find("__"); sep != std::string::npos) {
                    rest = mn.substr(sep);
                    composed = true;
                }
            }
            if (composed) {
                std::string new_bare = new_name + rest;
                final_name = mn_pkg.empty() ? new_bare : mn_pkg + "." + new_bare;
            }
        }

        // Specialization: if the user wrote `impl Foo<Concrete> { fn m ... }`
        // separately from `impl<T> Foo<T> { fn m ... }`, the concrete method
        // lives in in_.functions under the mangled name.  Skip cloning the
        // blanket version for this concrete — the free-fn path will emit it
        // with the correct body.
        bool overridden = false;
        for (auto& fn : in_.functions) {
            if (!fn.type_params_empty()) continue;
            if (bare_fn_name(fn.name()) == final_name) { overridden = true; break; }
        }
        if (overridden) continue;

        // Binary-symbol fast path: body is in liblstdlib.a, mlir_gen will skip
        // body emission, and any transitive instantiations from this body are
        // already in the archive. Signature-only stub is enough.
        if (binary_has_link(final_name, m.package())) {
            auto nm = clone_fn_signature(m, *msel, packs);
            nm.str_always(lir_schema::decl_keys::NAME, final_name);
            ma.push_ref(nm.view<lir_view::FunctionView>().self.addr());
            continue;
        }

        auto nm = clone_fn(m, *msel, packs);
        nm.str_always(lir_schema::decl_keys::NAME, final_name);
        // Substitute struct type in params/ret as needed (already done by clone_fn).
        ma.push_ref(nm.view<lir_view::FunctionView>().self.addr());
    }
    return nd;
}


// Return the best-matching struct specialisation for (base_name, type_args).
lir_view::StructView Mono::find_best_struct_spec(
    const std::string& base_name,
    const std::vector<TypeRef>& type_args) {
    auto sit = struct_specs_.find(base_name);
    if (sit == struct_specs_.end()) return {};

    const TypePoolImpl* pool = out_.type_pool.impl();
    lir_view::StructView   best;
    std::vector<int>       best_vec;
    bool                   ambiguous  = false;

    for (auto spec : sit->second) {
        auto pats = spec.spec_patterns(pool);
        if (pats.size() != type_args.size()) continue;
        SubstMap dummy;
        bool ok = true;
        // Bound gates apply only to bound-DISCRIMINATED specs (all pattern
        // positions TypeVar) — mirrors sema; shape-selected specs' pattern
        // bounds are ordinary constraints.
        bool all_tv = true;
        for (auto pp : pats)
            if (!pp || TypeRef(pp).kind() != LogosType::Kind::TypeVar)
                { all_tv = false; break; }
        std::vector<int> bound_bonus(type_args.size(), 0);
        for (size_t i = 0; i < type_args.size(); ++i) {
            if (!match_type(type_args[i], pats[i], dummy)) {
                ok = false; break;
            }
            // Bound-discriminated pattern (`struct S<T: Copy + Fst>`): the
            // spec matches only args SATISFYING the pattern var's bounds
            // (mirrors sema's find_best_sema_struct_spec gate). TypeVar-
            // bearing args accept — template-time scans re-run concrete.
            if (all_tv && pats[i] && TypeRef(pats[i]).kind() == LogosType::Kind::TypeVar) {
                std::string pv(TypeRef(pats[i]).type_var_name());
                for (auto tp : spec.type_params()) {
                    if (tp.name() != pv || tp.bounds_empty()) continue;
                    int nb = 0;
                    if (type_args[i] && !contains_typevar(type_args[i])) {
                        bool sat = true;
                        tp.each_bound([&](lir_view::FnTraitBoundView b) {
                            if (!sat) return;
                            StrSet seen;
                            if (!mono_concrete_satisfies_bound(
                                    std::string(b.trait_name()),
                                    type_args[i], seen))
                                sat = false;
                            ++nb;
                        });
                        if (!sat) { ok = false; }
                    } else {
                        tp.each_bound([&](lir_view::FnTraitBoundView) { ++nb; });
                    }
                    bound_bonus[i] = nb;
                    break;
                }
                if (!ok) break;
            }
        }
        if (!ok) continue;
        auto svec = specificity_vec(pats);
        for (size_t i = 0; i < svec.size() && i < bound_bonus.size(); ++i)
            svec[i] += bound_bonus[i];
        if (!best.valid() || svec > best_vec) {
            best_vec  = svec;
            best      = spec;
            ambiguous = false;
        } else if (svec == best_vec) {
            ambiguous = true;
        }
    }
    if (ambiguous) {
        in_.diags.diags.push_back({Diag::Level::Error, "mono",
            std::format("ambiguous specializations for struct '{}'", base_name), diag_file_, diag_line_});
    }
    return best;
}


// Walk all output functions collecting generic struct instantiations needed.
void Mono::collect_struct_needs_from_output() {
    out_.type_pool.arena_or_init();
    auto* csn_pool = out_.type_pool.impl();
    for (auto& fn : out_.functions) {
        collect_type_for_structs(fn.ret_type(csn_pool));
        for (auto& p : fn.params()) collect_type_for_structs(p.type(csn_pool));
        if ((bool)fn.body())
            collect_struct_needs_from_block(
                fn.body());
    }
    // Also walk already-instantiated structs (field types may reference more).
    for (auto& sd : out_.structs)
        for (auto f : sd.fields()) collect_type_for_structs(f.type(csn_pool));
    // AND THE ENUMS — a variant payload is a type position like a struct field.
    // This was missing, and mlir-gen sizes every registered enum's payload from
    // exactly these types. FOUND BY THE `layout_of` FATAL, not by a test:
    // `enum Json { …, Array(Vec<Json>), Object(Vec<JsonField>) }` is emitted by
    // anything that merely `use`s the json module, its payload was sized
    // through `Vec<Json>`, and `Vec<Json>` was never instantiated because no
    // VALUE of it existed in that program. `Json`'s payload_bytes was therefore
    // computed from an 8-byte stand-in for a `Vec`.
    for (auto& ed : out_.enums)
        ed.each_variant([&](lir_view::EnumVariantView v) {
            v.each_payload_type(csn_pool,
                                [&](TypeRef pt) { collect_type_for_structs(pt); });
        });
    // AND THE CONSTS/STATICS — a `static S: G<i32>` is a demand for `G<i32>`
    // whether or not any emitted fn body reads it. It was only reached through
    // a READING fn, so an unread static — or any static in a metaprog round,
    // where the readers are stubbed — was emitted with an unregistered struct
    // type and mlir-gen refused ("unknown struct 'G$G1$i32'").
    for (auto& c : out_.consts) {
        collect_type_for_structs(c.type(csn_pool));
        collect_struct_needs_from_expr(c.value());
    }
}


void Mono::collect_struct_needs_from_block(lir_view::BlockRef b) {
    if (!b) return;
    b.each_stmt([&](lir_view::StmtRef s) { collect_struct_needs_from_stmt(s); });
}


void Mono::collect_struct_needs_from_stmt(lir_view::StmtRef s) {
    if (!s) return;
    using SCode = lir_schema::stmt::Code;
    const TypePoolImpl* pool = out_.type_pool.impl();
    switch (s.kind()) {
    case SCode::Let: {
        lir_view::SLetView v{s};
        collect_type_for_structs(v.type(pool));
        collect_struct_needs_from_expr(v.value());
        break;
    }
    case SCode::Assign:
        collect_struct_needs_from_expr(lir_view::SAssignView{s}.value());
        break;
    case SCode::Return:
        if (auto v = lir_view::SReturnView{s}.value()) collect_struct_needs_from_expr(v);
        break;
    case SCode::If: {
        lir_view::SIfView v{s};
        collect_struct_needs_from_expr(v.cond());
        collect_struct_needs_from_block(v.then_block());
        collect_struct_needs_from_block(v.else_block());
        break;
    }
    case SCode::While: {
        lir_view::SWhileView v{s};
        collect_struct_needs_from_expr(v.cond());
        collect_struct_needs_from_block(v.body());
        break;
    }
    case SCode::For: {
        lir_view::SForView v{s};
        collect_struct_needs_from_expr(v.lo());
        collect_struct_needs_from_expr(v.hi());
        collect_struct_needs_from_block(v.body());
        break;
    }
    case SCode::ForEach: {
        lir_view::SForEachView v{s};
        collect_type_for_structs(v.elem_type(pool));
        collect_struct_needs_from_expr(v.iter());
        collect_struct_needs_from_block(v.body());
        break;
    }
    case SCode::Break:
        if (auto v = lir_view::SBreakView{s}.value())
            collect_struct_needs_from_expr(v);
        break;
    case SCode::Continue:
        break;
    case SCode::Loop:
        collect_struct_needs_from_block(lir_view::SLoopView{s}.body());
        break;
    case SCode::Block:
        collect_struct_needs_from_block(lir_view::SBlockView{s}.body());
        break;
    case SCode::FieldWrite:
        collect_struct_needs_from_expr(lir_view::SFieldWriteView{s}.value());
        break;
    case SCode::ChainFieldWrite:
        collect_struct_needs_from_expr(lir_view::SChainFieldWriteView{s}.value());
        break;
    case SCode::DerefFieldWrite:
        collect_struct_needs_from_expr(lir_view::SDerefFieldWriteView{s}.value());
        break;
    case SCode::IndexWrite: {
        lir_view::SIndexWriteView v{s};
        collect_struct_needs_from_expr(v.index());
        collect_struct_needs_from_expr(v.value());
        break;
    }
    case SCode::FieldIndexWrite: {
        lir_view::SFieldIndexWriteView v{s};
        collect_struct_needs_from_expr(v.index());
        collect_struct_needs_from_expr(v.value());
        break;
    }
    case SCode::DerefWrite: {
        lir_view::SDerefWriteView v{s};
        collect_struct_needs_from_expr(v.ptr());
        collect_struct_needs_from_expr(v.value());
        break;
    }
    case SCode::TupleWrite:
        collect_struct_needs_from_expr(lir_view::STupleWriteView{s}.value());
        break;
    case SCode::ExprStmt:
        collect_struct_needs_from_expr(lir_view::SExprStmtView{s}.expr());
        break;
    case SCode::Drop:
        break;
    case SCode::LetElse: {
        lir_view::SLetElseView v{s};
        collect_struct_needs_from_expr(v.scrut());
        collect_struct_needs_from_block(v.else_block());
        break;
    }
    // NO `default:` — see the note on `collect_struct_needs_from_expr`. Every
    // stmt code is named, so a new one is a compile error here rather than a
    // silently unvisited subtree. `ForEach`, `Break`-with-value and `For`'s
    // bounds were exactly that: reachable statements the walk did not name.
    }
}


// ── WHAT MAKES A TYPE NEEDED ────────────────────────────────────────────────
//
// This walk decides which generic struct/enum INSTANCES exist at all. It used
// to read `e.type()` — the type of the VALUE the expression produces — and
// recurse through a hand-picked subset of children, everything else falling to
// `default: break`. Two holes of one shape followed from that:
//
//   * a type can be USED WITHOUT A VALUE. `sizeof::<T>()` / `align_of::<T>()`
//     / `typecodeof` / `reflectof` consume T for its LAYOUT; the expression's
//     own type is `usize`, so T was never seen. MEASURED at HEAD:
//     `sizeof::<W<i128>>()` = 8 for a 32-byte type, and the same file with one
//     unused `let w: W<i128>` = 32. `mono` never cloned the instance, mlir-gen
//     found no definition, and `malloc(sizeof(..))` asked for 8 bytes;
//   * a node the switch did not NAME hid every type under it. `ForEach`,
//     `Break`-with-value, `For`'s bounds, `IfExpr`, `TupleLit`, `Try`,
//     `ClosureCall` and a dozen more were unvisited subtrees.
//
// So: the type-arguments of the layout intrinsics are collected, and the switch
// carries NO `default:` — every expr code is named, and a new one is a compile
// error here instead of a silently unvisited subtree. Same discipline as
// `scan_expr`, which was already exhaustive; the two walks are now the same
// shape over the same node set, which is what makes them comparable.
void Mono::collect_struct_needs_from_expr(lir_view::ExprRef e) {
    if (!e) return;
    const TypePoolImpl* pool = out_.type_pool.impl();
    collect_type_for_structs(e.type(pool));
    using ECode = lir_schema::expr::Code;
    switch (e.kind()) {
    // ── A TYPE USED WITHOUT A VALUE IS AN INSTANTIATION DEMAND ──────────
    case ECode::SizeOf:
        collect_type_for_structs(lir_view::ESizeOfView{e}.elem_type(pool));
        break;
    case ECode::AlignOf:
        collect_type_for_structs(lir_view::EAlignOfView{e}.elem_type(pool));
        break;
    case ECode::TypeCodeOf:
        collect_type_for_structs(lir_view::ETypeCodeOfView{e}.elem_type(pool));
        break;
    case ECode::ReflectOf:
        collect_type_for_structs(lir_view::EReflectOfView{e}.type(pool));
        break;
    case ECode::Call:
        lir_view::ECallView{e}.each_arg(
            [&](lir_view::ExprRef a) { collect_struct_needs_from_expr(a); });
        break;
    case ECode::MethodCall: {
        lir_view::EMethodCallView v{e};
        collect_struct_needs_from_expr(v.receiver());
        v.each_arg([&](lir_view::ExprRef a) { collect_struct_needs_from_expr(a); });
        break;
    }
    case ECode::BinOp: {
        lir_view::EBinOpView v{e};
        collect_struct_needs_from_expr(v.lhs());
        collect_struct_needs_from_expr(v.rhs());
        break;
    }
    case ECode::Unary:
        collect_struct_needs_from_expr(lir_view::EUnaryView{e}.operand());
        break;
    case ECode::Deref:
        collect_struct_needs_from_expr(lir_view::EDerefView{e}.operand());
        break;
    case ECode::FieldRead:
        collect_struct_needs_from_expr(lir_view::EFieldReadView{e}.receiver());
        break;
    case ECode::IndexRead: {
        lir_view::EIndexReadView v{e};
        collect_struct_needs_from_expr(v.receiver());
        collect_struct_needs_from_expr(v.index());
        break;
    }
    case ECode::StructLit:
        lir_view::EStructLitView{e}.each_field_value(
            [&](lir_view::ExprRef fv) { collect_struct_needs_from_expr(fv); });
        break;
    case ECode::ArrLit:
        lir_view::EArrLitView{e}.each_elem(
            [&](lir_view::ExprRef el) { collect_struct_needs_from_expr(el); });
        break;
    case ECode::Cast:
        collect_struct_needs_from_expr(lir_view::ECastView{e}.operand());
        break;
    case ECode::FormatCall: {
        lir_view::EFormatCallView v{e};
        collect_struct_needs_from_expr(v.fmt());
        v.each_arg([&](lir_view::ExprRef a) { collect_struct_needs_from_expr(a); });
        break;
    }
    case ECode::MatchExpr: {
        lir_view::EMatchExprView v{e};
        collect_struct_needs_from_expr(v.scrut());
        v.each_arm([&](lir_view::EMatchArmRef arm) {
            if (auto g = arm.guard()) collect_struct_needs_from_expr(g);
            if (auto val = arm.value()) collect_struct_needs_from_expr(val);
        });
        break;
    }
    case ECode::BlockExpr: {
        lir_view::EBlockExprView v{e};
        if (auto blk = v.block()) collect_struct_needs_from_block(blk);
        if (auto r = v.result()) collect_struct_needs_from_expr(r);
        break;
    }
    case ECode::IfExpr: {
        lir_view::EIfExprView v{e};
        collect_struct_needs_from_expr(v.cond());
        collect_struct_needs_from_expr(v.then_val());
        collect_struct_needs_from_expr(v.else_val());
        break;
    }
    case ECode::TupleLit:
        lir_view::ETupleLitView{e}.each_elem(
            [&](lir_view::ExprRef el) { collect_struct_needs_from_expr(el); });
        break;
    case ECode::TupleIndex:
        collect_struct_needs_from_expr(lir_view::ETupleIndexView{e}.receiver());
        break;
    case ECode::EnumLitData:
        lir_view::EEnumLitDataView{e}.each_payload(
            [&](lir_view::ExprRef p) { collect_struct_needs_from_expr(p); });
        break;
    case ECode::SliceLit: {
        lir_view::ESliceLitView v{e};
        collect_struct_needs_from_expr(v.base());
        collect_struct_needs_from_expr(v.len());
        break;
    }
    case ECode::SliceIndex: {
        lir_view::ESliceIndexView v{e};
        collect_struct_needs_from_expr(v.slice());
        collect_struct_needs_from_expr(v.index());
        break;
    }
    case ECode::SliceLen:
        collect_struct_needs_from_expr(lir_view::ESliceLenView{e}.slice());
        break;
    case ECode::SlicePtr:
        collect_struct_needs_from_expr(lir_view::ESlicePtrView{e}.slice());
        break;
    case ECode::ClosureBox:
        collect_struct_needs_from_block(lir_view::EClosureBoxView{e}.body());
        break;
    case ECode::ClosureCall: {
        lir_view::EClosureCallView v{e};
        collect_struct_needs_from_expr(v.callee());
        v.each_arg([&](lir_view::ExprRef a) { collect_struct_needs_from_expr(a); });
        break;
    }
    case ECode::FnPtrCall: {
        lir_view::EFnPtrCallView v{e};
        collect_struct_needs_from_expr(v.callee());
        v.each_arg([&](lir_view::ExprRef a) { collect_struct_needs_from_expr(a); });
        break;
    }
    case ECode::Try:
        collect_struct_needs_from_expr(lir_view::ETryView{e}.inner());
        break;
    case ECode::AddrOfTemp:
        collect_struct_needs_from_expr(lir_view::EAddrOfTempView{e}.inner());
        break;
    case ECode::PtrArith: {
        lir_view::EPtrArithView v{e};
        collect_struct_needs_from_expr(v.ptr());
        collect_struct_needs_from_expr(v.offset());
        break;
    }
    case ECode::PtrDiff: {
        lir_view::EPtrDiffView v{e};
        collect_struct_needs_from_expr(v.lhs());
        collect_struct_needs_from_expr(v.rhs());
        break;
    }
    case ECode::WritLit:
        lir_view::EWritLitView{e}.each_capture_expr(
            [&](lir_view::ExprRef c) { collect_struct_needs_from_expr(c); });
        break;
    // Leaf / no-recurse: `e.type()` above is the whole type content. Named
    // rather than defaulted — see the note on this function.
    case ECode::LitInt:
    case ECode::LitFloat:
    case ECode::LitBool:
    case ECode::LitStr:
    case ECode::VarRef:
    case ECode::EnumLit:
    case ECode::AddrOf:
    case ECode::PackExpand:
    case ECode::GenericRef:  // rewritten to VarRef during subst_expr
        break;
    }
}


// Process all pending struct instantiations (may discover more via field types).
void Mono::instantiate_struct_templates() {
    collect_struct_needs_from_output();

    while (!needed_struct_insts_.empty()) {
        // Take a copy of current needs (instantiating may add more).
        auto current = std::move(needed_struct_insts_);
        needed_struct_insts_.clear();

        for (auto& [qcname, info] : current) {
            if (struct_done_.count(qcname)) continue;
            struct_done_.insert(qcname);

            TypeRef struct_t = info.first;
            depth_ = info.second;

            // Bare cname (no pkg prefix) — used as the cloned struct's
            // sd.name and as the back-compat key in concrete_struct_types_.
            // qcname is "pkg.bare" or just "bare" when struct_t has no pkg.
            const std::string cname = concrete_struct_name(struct_t);
            const std::string base{TypeRef(struct_t).struct_name()};
            SubstMap subst;

            // ADR 0021 §3: metaclass demand. Instantiating a factory-backed
            // marker type (CtrClass<CFG>) records a demand for the CFG's
            // container family, keyed by the WritStatic doc's content hash.
            // The marker itself IS a real (empty) template — cloning proceeds
            // normally below; the demand is drained by the driver, which
            // invokes the metaclass factory and re-enters dispatch. Keyed on
            // the raw u64 hash (mangled spellings diverge: hs_ vs @hs_).
            if (base == "CtrClass" &&
                struct_pkg_is_metaclass(struct_t) &&
                !TypeRef(struct_t).type_args().empty()) {
                TypeRef cfg = TypeRef(struct_t).type_args()[0];
                if (cfg && TypeRef(cfg).kind() == LogosType::Kind::WStaticLit) {
                    uint64_t h = (uint64_t)TypeRef(cfg).const_val().value_or(0);
                    if (h && factory_demand_hashes_.insert(h).second)
                        out_.factory_demands.push_back(
                            {base, std::string(TypeRef(struct_t).pkg_name()),
                             h, qcname});
                }
            }

            const TypePoolImpl* ist_pool = out_.type_pool.impl();
            lir_view::StructView tmpl;
            PackMap packs;
            // Prefer pkg-qualified lookup so cross-pkg same-named structs
            // route to their own template; fall back to bare for legacy
            // call sites that don't carry pkg.
            std::string struct_pkg{TypeRef(struct_t).pkg_name()};
            if (auto spec = find_best_struct_spec(base, TypeRef(struct_t).type_args()); spec.valid()) {
                auto pats = spec.spec_patterns(ist_pool);
                for (size_t i = 0; i < pats.size() &&
                                   i < TypeRef(struct_t).type_args().size(); ++i)
                    match_type(TypeRef(struct_t).type_args()[i], pats[i], subst);
                tmpl = spec;
            } else {
                tmpl = find_struct_template_pkg_first(struct_pkg, base);
                if (!tmpl.valid()) {
                    // Generic-container coherence (S1): a concrete container
                    // stack (e.g. Map$G2$u64$str) is emitted as a fully-formed
                    // struct with NO template — the generic container decl emits
                    // nothing, the driver feeds concrete decls. If a struct
                    // literally named `cname` already exists in out_.structs,
                    // the instance is SATISFIED: references link by name. Do not
                    // record missing and do not poison referencing fns.
                    bool already_present = false;
                    for (auto& sd : out_.structs)
                        if (sd.name() == cname) { already_present = true; break; }
                    if (already_present) {
                        concrete_struct_types_[qcname] = struct_t;
                        concrete_struct_types_[cname]  = struct_t;
                        continue;
                    }
                    // Deferred-emission poison guard (see mono_impl.hpp): the
                    // template may be a macro-emitted struct that doesn't
                    // exist yet (dispatch iter 0). Remember the instance so
                    // fns referencing it demote to declarations.
                    missing_struct_insts_.insert(qcname);
                    missing_struct_insts_.insert(cname);
                    continue;
                }
                auto tmpl_tps = tmpl.type_params();
                for (size_t i = 0, j = 0; i < tmpl_tps.size(); ++i) {
                    if (tmpl_tps[i].is_variadic()) {
                        std::vector<TypeRef> pack;
                        while (j < TypeRef(struct_t).type_args().size()) pack.push_back(TypeRef(struct_t).type_args()[j++]);
                        packs[std::string(tmpl_tps[i].name())] = std::move(pack);
                    } else if (j < TypeRef(struct_t).type_args().size()) {
                        subst[std::string(tmpl_tps[i].name())] = TypeRef(struct_t).type_args()[j++];
                    }
                }
            }

            // Pkg-qualified primary key + bare back-compat key (last-wins).
            // Sites that have a TypeRef use qualified_cname for routing;
            // sites that only have sd.name fall through bare lookup.
            concrete_struct_types_[qcname] = struct_t;
            concrete_struct_types_[cname]  = struct_t;
            // Module-tag-less aliases: a method call lowered to a NAMED call
            // (the DstRef dispatch path) mints its receiver base via
            // concrete_struct_name_raw WITHOUT pkg — the §P3 bare-name
            // discipline — so its "<Concrete>__<method>" spelling carries no
            // "$M<module>" coexistence tag. Key the instance under the
            // stripped spellings too, or the lazy-method enqueue (scan_expr
            // L1.5 / deferred drain) never resolves those callees and the
            // method instance is never cloned (module-mode-only failure).
            auto strip_mtags = [](std::string s) {
                for (size_t p; (p = s.find("$M")) != std::string::npos; ) {
                    size_t q = p + 2;
                    while (q < s.size() && s[q] != '$') ++q;   // id = 'm'+hex, runs to next '$'
                    s.erase(p, q - p);
                }
                return s;
            };
            if (auto sq = strip_mtags(qcname); sq != qcname)
                concrete_struct_types_[sq] = struct_t;
            if (auto sc = strip_mtags(cname); sc != cname)
                concrete_struct_types_[sc] = struct_t;
            auto inst = clone_struct_def(tmpl, subst, packs, cname);
            // The generic's home pkg owns the conceptual identity. A spec in a
            // different pkg only contributes layout; the cloned inst should
            // carry the generic's pkg so user-side TypeRefs (resolved against
            // the generic decl) and mlir-side struct names agree.
            // Inst pkg comes from the generic template (not the spec, which
            // may live in a different pkg and only contribute layout).
            if (auto git = find_struct_template_pkg_first(struct_pkg, base); git.valid())
                inst.str(lir_schema::struct_keys::PKG, git.pkg());
            auto inst_sv = inst.view<lir_view::StructView>();
            // Stage E direct-build: the struct mirror (and its method fn-views)
            // were built straight into out_ by clone_struct_def — no separate
            // emit. Each method body's stmt/expr addresses are stable (segments
            // never move), so scan_fn can read them via the FunctionView now.
            inst_sv.each_method([&](lir_view::FunctionView m) { scan_fn(m); });
            // Apply explicit instantiation annotation if present (sets type_code
            // on a specific generic instantiation, e.g. `#[type_code=100] eidos Array<AnyVal>;`).
            for (auto& ia : out_.inst_annotations) {
                if (ia.mangled_name() == cname && ia.type_code() != 0) {
                    inst.i64(lir_schema::struct_keys::TYPE_CODE, (int64_t)ia.type_code());
                    break;
                }
            }
            // Collect field types of new struct for further instantiation.
            const TypePoolImpl* fpool = out_.type_pool.impl();
            inst_sv.each_field([&](lir_view::LFieldView f) { collect_type_for_structs(f.type(fpool)); });
            out_.structs.push_back(inst_sv);
            ++stats_.struct_instances;
        }
        depth_ = 0;

        // Drain any fn-worklist items added by struct-method scans above.
        note_fn_worklist_size(worklist_.size());
        while (!worklist_.empty()) {
            auto item = std::move(worklist_.back());
            worklist_.pop_back();
            depth_ = item.depth;
            note_depth(depth_);
            auto fn_inst = instantiate_fn(item.tmpl, item.mangled, item.subst, item.packs);
            // The decl mirror was direct-built into out_; store the View and
            // scan it (the builder stays alive this iteration).
            auto fn_inst_v = fn_inst.view<lir_view::FunctionView>();
            out_.functions.push_back(fn_inst_v);
            scan_fn(fn_inst_v);
            ++stats_.fn_instances;
            note_fn_worklist_size(worklist_.size());
        }
        depth_ = 0;
    }
}


// ── Class monomorphization ────────────────────────────────────

// Clone a class def with substitution; rename to new_name.
// Mirrors clone_struct_def but preserves vtable_order, parent_name, etc.

// Stage E: read the template enum via EnumView, apply subst_type / variadic-pack
// expansion to each variant's payload EXACTLY as before, emit the substituted
// enum's Writ mirror into out_, and return an EnumView. Matches the old
// clone behaviour: only name/pkg/zoned2/variants are carried (type_params /
// backing_type / borrow_carrying / doc are intentionally dropped on instances).
lir_view::EnumView Mono::clone_enum_def(lir_view::EnumView tmpl,
                              const SubstMap& s,
                              const PackMap& packs,
                              const std::string& new_name) {
    namespace dk = lir_schema::decl_keys;
    namespace vk = lir_schema::variant_keys;
    // in_.type_pool was moved into out_ at run() start; read the (shared-arena)
    // template payloads through out_'s live pool handle.
    auto* pool = out_.type_pool.impl();
    // Stage E direct-build: build the substituted enum mirror STRAIGHT into out_.
    // type_params / borrow_carrying / doc are dropped on instances: an instance
    // is monomorphic, and neither of the others is layout.
    //
    // ⚠ BACKING_TYPE IS NOT IN THAT LIST AND USED TO BE. `enum GT<T> : u64` lost
    // its backing type at instantiation, so every post-mono engine sized
    // `GT<i32>` as the default i32 discriminant. MEASURED, before the fix:
    // `sizeof::<GT<i32>>()` = 4 for an eight-byte type, and in
    // `struct Holder<T> { h: GT<T>, k: u8, v: i64 }` the GEP wrote `k` at byte 4
    // while `offset_of!` — sema, which resolves the TEMPLATE and still saw the
    // backing type — claimed 8. Two numbers for one field, four bytes apart,
    // and `malloc(sizeof)` short by four.
    //
    // The backing type IS the C-like enum's layout. Dropping it here is the same
    // defect as mono not reading it in `mono_enum_layout`, one phase earlier:
    // the law can only be as complete as what the engine was handed.
    DeclBuilder nd(out_, lir_schema::decl::Code::Enum, /*cap=*/16);
    nd.str_always(dk::NAME, new_name);
    nd.str(dk::PKG, tmpl.pkg());
    if (tmpl.is_pub()) nd.flag(dk::IS_PUB, true);   // ABI pub-scoping — mirrors clone_fn.
    if (tmpl.zoned2()) nd.flag(dk::ZONED2, true);   // F3: preserve niche enum's at-rest-relative marker
    if (auto bt = tmpl.backing_type(pool)) nd.type(dk::BACKING_TYPE, bt);
    auto va = nd.array(dk::VARIANTS);
    tmpl.each_variant([&](lir_view::EnumVariantView v) {
        std::vector<TypeRef> payloads;
        auto pts = v.payload_types(pool);
        // Variadic expansion for variants like Multi(...T)
        if (v.is_variadic() && !pts.empty()) {
            auto pt = pts[0];
            if (TypeRef(pt).kind() == LogosType::Kind::TypeVar) {
                auto pit = packs.find(TypeRef(pt).type_var_name());
                if (pit != packs.end()) {
                    for (auto pt_in_pack : pit->second)
                        payloads.push_back(subst_type(pt_in_pack, s));
                } else {
                    payloads.push_back(subst_type(pt, s));
                }
            } else {
                payloads.push_back(subst_type(pt, s));
            }
        } else {
            for (auto pt : pts)
                payloads.push_back(subst_type(pt, s));
        }
        auto vb = va.submap(lir_schema::stmt::Count + 3, /*cap=*/8);
        vb.str_always(vk::V_NAME, v.name());
        vb.i64(vk::V_DISC, v.disc());
        if (!payloads.empty()) {
            auto pa = vb.array(vk::V_PAYLOAD_TYPES);
            for (auto t : payloads) pa.push_type(t);
        }
        if (v.is_variadic()) vb.flag(vk::V_IS_VARIADIC, true);
    });
    return nd.view<lir_view::EnumView>();
}


void Mono::instantiate_enum_templates() {
    // Instantiate generic enums that were recorded during function cloning.
    // Simple approach: iterate until no more needed (fixed-point).
    while (true) {
        std::vector<std::pair<std::string, EnumInst>> todo;
        for (auto& [cname, info] : needed_enum_insts_) {
            if (enum_done_.count(cname)) continue;
            todo.push_back({cname, info});
        }
        if (todo.empty()) break;
        for (auto& [cname, info] : todo) {
            enum_done_.insert(cname);
            const auto& args = info.args;
            depth_ = info.depth;
            // The base is CARRIED by the demand (record_needed_enum stored
            // `tr.enum_name()`), never re-derived by splitting `cname` at the
            // first "__" — that cut lands INSIDE any base ending in '_'.
            const std::string& base = info.base;
            auto tmpl = find_enum_template_bare(base);
            if (!tmpl) {
                // R2: a demanded instance with no template is a malfunction,
                // not an input shape — silently `continue`ing here is exactly
                // what let a `let` statement vanish behind a green gate.
                in_.diags.diags.push_back({Diag::Level::Error, "mono",
                    std::format("no template for generic enum '{}' (pkg '{}') demanded as "
                                "instance '{}' with {} type-arg(s) — the instance would be "
                                "silently absent and every use of it dropped at mlir-gen",
                                base, info.pkg, cname, args.size()), diag_file_, diag_line_});
                continue;
            }
            // Materialize the template's type-params (name + is_variadic) so the
            // positional substitution below can index them like the old vector.
            std::vector<std::pair<std::string, bool>> tps;
            tmpl->each_type_param([&](lir_view::EnumTParamView tp) {
                tps.push_back({std::string(tp.name()), tp.is_variadic()});
            });
            // Build substitution map and packs
            SubstMap subst;
            PackMap packs;
            for (size_t i = 0, j = 0; i < tps.size(); ++i) {
                if (tps[i].second) {
                    std::vector<TypeRef> pack;
                    while (j < args.size()) pack.push_back(args[j++]);
                    packs[tps[i].first] = std::move(pack);
                } else if (j < args.size()) {
                    subst[tps[i].first] = args[j++];
                }
            }
            // Instantiate: substitute payload types and methods
            auto inst = clone_enum_def(*tmpl, subst, packs, cname);

            // Void payload skip: `Option<()>` / `Result<(), E>` etc.
            // map their `()` payload to Kind::Void. Method bodies like
            // `Option::unwrap` (`match self { Some(v) => return v }`)
            // would emit a `return v` of void-typed v — at mlir-gen
            // the pattern-bound `v` has no slot (void carries no
            // value) and VarRef(v) is undefined. Skip cloning these
            // specs entirely; real call sites for `Option<()>::unwrap`
            // are uncallable in user code (void can't be passed/
            // returned as a value). Pure marker / discriminant-only
            // uses still work via the bare `match` shape.
            bool any_void_arg = false;
            for (auto& [_, v] : subst) {
                if (v && v.kind() == LogosType::Kind::Void) {
                    any_void_arg = true;
                    break;
                }
            }
            if (any_void_arg) {
                out_.enums.push_back(std::move(inst));
                ++stats_.enum_instances;
                continue;
            }

            // Instantiate any impl<T> methods stored as generic functions in prog.functions.
            // Convention: function name starts with "Base__" and has matching type params.
            //
            // Self-referential return types (e.g. `Option::as_ref(self: &Option<T>)
            // -> Option<&T>`) would cascade here without bound: instantiating
            // Option<i32> clones as_ref<i32>, whose body references Option<&i32>;
            // that triggers Option<&i32> instantiation which clones as_ref<&i32>
            // whose body references Option<&&i32>; etc. — depth limit hit at 64.
            //
            // Skip the eager clone when the method body's body-types include
            // a recursive `Option<X>` reference whose X is structurally larger
            // than `T`. Real call sites still enqueue the spec via scan_fn's
            // enqueue_if_needed → templates_ lookup path.
            //
            // Cheaper conservative check: if the method's return type or any
            // param type references the same enum base with a TypeVar that's
            // NESTED inside another type-constructor (Ref / Ptr / Array / etc.)
            // — i.e. not bare T — assume self-referential and skip.
            //
            // Refinement: "structurally larger" only matters when the args
            // contain TypeVars from the fn's own type params. A fully-
            // concrete `Result<(), Error>` return type doesn't recurse on
            // mono — substitution leaves it identical. Without this
            // refinement, my `impl<T,E> Debug for Result<T,E> { fn
            // fmt_debug(&self,f)->Result<(),Error> }` is rejected because
            // the return mentions Result with concrete `()`/`Error` args.
            auto type_contains_typevar = [](TypeRef t) -> bool {
                std::function<bool(TypeRef)> walk_tv = [&](TypeRef u) -> bool {
                    if (!u) return false;
                    if (u.kind() == LogosType::Kind::TypeVar) return true;
                    if (u.pointee() && walk_tv(u.pointee())) return true;
                    if (u.elem() && walk_tv(u.elem())) return true;
                    for (auto a : u.type_args()) if (walk_tv(a)) return true;
                    return false;
                };
                return walk_tv(t);
            };
            auto* iet_pool = out_.type_pool.impl();
            auto is_self_referential = [&](lir_view::FunctionView fn) -> bool {
                auto self_ref_type = [&](TypeRef t) -> bool {
                    if (!t) return false;
                    auto k = t.kind();
                    if (k != LogosType::Kind::Enum) return false;
                    if (std::string(t.enum_name()) != base) return false;
                    for (auto a : t.type_args()) {
                        if (!a) continue;
                        // Bare TypeVar T is fine; anything else (Ref<T>,
                        // Ptr<T>, Option<T>, ...) is structurally larger
                        // — but ONLY counts as self-referential if the
                        // larger structure mentions a TypeVar. Fully
                        // concrete args (`Result<(), Error>`) don't recurse.
                        if (a.kind() != LogosType::Kind::TypeVar &&
                            type_contains_typevar(a)) return true;
                    }
                    return false;
                };
                std::function<bool(TypeRef)> walk = [&](TypeRef t) -> bool {
                    if (!t) return false;
                    if (self_ref_type(t)) return true;
                    if (t.pointee() && walk(t.pointee())) return true;
                    if (t.elem() && walk(t.elem())) return true;
                    for (auto a : t.type_args()) if (walk(a)) return true;
                    return false;
                };
                for (auto& p : fn.params()) if (walk(p.type(iet_pool))) return true;
                if (walk(fn.ret_type(iet_pool))) return true;
                return false;
            };
            std::string prefix = base + "__";
            for (auto& fn : in_.functions) {
                if (fn.type_params_empty()) continue;
                // Strip pkg prefix (`pkg.`) before matching the bare base name.
                std::string_view bare = fn.name();
                std::string fn_pkg;
                if (auto dot = bare.rfind('.'); dot != std::string_view::npos) {
                    fn_pkg = std::string(bare.substr(0, dot));
                    bare = bare.substr(dot + 1);
                }
                if (bare.substr(0, prefix.size()) != prefix) continue;
                // Materialize the fn's type-params (with bounds) once so the
                // per-tp accesses below (was struct field iteration) work off
                // a stable local rather than re-reading the View each time.
                struct IetTBound {
                    std::string trait_name;
                };
                struct IetTParam {
                    std::string name;
                    bool is_variadic;
                    std::vector<IetTBound> bounds;
                };
                std::vector<IetTParam> fn_tparams;
                fn.each_type_param([&](lir_view::FnTParamView tpv) {
                    IetTParam tpe;
                    tpe.name = std::string(tpv.name());
                    tpe.is_variadic = tpv.is_variadic();
                    tpv.each_bound([&](lir_view::FnTraitBoundView tbv) {
                        tpe.bounds.push_back({std::string(tbv.trait_name())});
                    });
                    fn_tparams.push_back(std::move(tpe));
                });
                // Match type params to subst keys
                bool matches = fn_tparams.size() == tps.size();
                if (!matches) continue;
                // A generic enum method has TWO mangled-name conventions
                // depending on its call site, and instantiate_enum_templates
                // is the one place that must satisfy both:
                //   • cname-INSERT (`Option__i8__eq`) — a direct concrete-
                //     receiver call (`o.eq(&p)`) or the `==`/`!=` operator
                //     lowering (sema routes to `<enum>__eq`), and every
                //     non-generic method of a generic enum (`unwrap`).
                //   • type-arg APPEND (`Option__eq__g__sig__i8`) — a
                //     trait-bound dispatch (`fn f<T: Eq>(t:&T){ t.eq(..) }`
                //     with T=Option<i8>): collect_fn flattened the impl
                //     header's `T` into the method's type_params, so the
                //     method is mangled `__g__` and the bound-dispatch site
                //     appends the type-arg after the signature.
                // The method body is identical, so emit it under the primary
                // name and, when it's a generic-mangled method, also under
                // the insert-form alias (and vice-versa) so whichever name a
                // caller demands resolves. Unused copies are dead-stripped.
                std::string method_suffix(bare.substr(base.size()));
                std::string arg_suffix = cname.substr(base.size());
                bool is_generic_method =
                    method_suffix.find("__g__") != std::string::npos;
                std::string bare_primary = is_generic_method
                    ? std::string(bare) + arg_suffix      // append form
                    : cname + method_suffix;              // cname-insert form
                std::string bare_alias = is_generic_method
                    ? cname + method_suffix               // also the insert form
                    : std::string();
                auto qualify = [&](const std::string& b) {
                    return fn_pkg.empty() ? b : fn_pkg + "." + b;
                };
                std::string inst_name = qualify(bare_primary);
                std::string alias_name =
                    bare_alias.empty() ? std::string() : qualify(bare_alias);
                // Skip only when BOTH names are already emitted. A different
                // call path may have already emitted the primary (e.g. a direct
                // `Option<i64> == …` emits the append form) while the insert-
                // form alias is still demanded by a nested generic body (the
                // `*a == *b` over `&Option<i64>` inside `Option<Option<i64>>::eq`
                // resolves to the insert form). Skipping the whole iteration on
                // the primary alone would drop the alias → "does not reference a
                // valid function". Fall through and let the per-name guards
                // below emit whichever is missing.
                bool need_primary = !done_.count(inst_name);
                bool need_alias   = !alias_name.empty() && !done_.count(alias_name);
                if (!need_primary && !need_alias) continue;
                if (is_self_referential(fn)) continue;
                SubstMap fn_subst = subst;
                PackMap fn_packs = packs;
                // CP-cm-16 follow-up: partial-spec impl target. If the fn
                // carries an impl_target_pattern that's NOT just the bare
                // enum (e.g. `Result<Vec<T>, E>` vs `Result<T, E>`), unify
                // against the concrete receiver type to bind impl-level
                // T,E from the nested type-constructors instead of from
                // positional `args[j]`. Method-level tparams (if any)
                // would have caused the matches-check above to fail; this
                // path is reached only for fns with no method-level
                // tparams, so the unified bindings cover all of
                // fn.type_params for impl-target-bearing impls.
                bool used_pattern = false;
                TypeRef fn_itp = fn.impl_target_pattern(iet_pool);
                if (fn_itp) {
                    TypeRef pat = fn_itp;
                    if (pat.kind() == LogosType::Kind::Enum &&
                        std::string(pat.enum_name()) == base) {
                        auto pa = pat.type_args();
                        if (pa.size() == args.size()) {
                            SubstMap impl_bind;
                            bool ok = true;
                            for (size_t i = 0; i < pa.size(); ++i) {
                                if (!unify_impl_target(args[i], pa[i], impl_bind)) {
                                    ok = false; break;
                                }
                            }
                            if (ok) {
                                for (auto& tp : fn_tparams) {
                                    auto it_b = impl_bind.find(tp.name);
                                    if (it_b == impl_bind.end()) { ok = false; break; }
                                    fn_subst[tp.name] = it_b->second;
                                }
                                if (ok) used_pattern = true;
                            }
                        }
                    }
                }
                if (!used_pattern) {
                    // Override type params with the enum's type param names if different
                    for (size_t i = 0, j = 0; i < fn_tparams.size(); ++i) {
                        if (fn_tparams[i].is_variadic) {
                             std::vector<TypeRef> pack;
                             while (j < args.size()) pack.push_back(args[j++]);
                             fn_packs[fn_tparams[i].name] = std::move(pack);
                        } else if (j < args.size()) {
                            fn_subst[fn_tparams[i].name] = args[j++];
                        }
                    }
                }
                // CP-cm-15: gate by type-param bound satisfaction. Without
                // this, `impl<T: Echo> Echo for Option<T>` clones for EVERY
                // Option<X> mono creates (incl. Option<&mut i32> arising
                // from stdlib's `.take()` chains), even when X doesn't
                // implement Echo. The clone's body method-dispatches on T
                // and lowers to a wrong-arity call (`&mut i32` passed to a
                // fn expecting `i32`), tripping mlir-gen verification.
                //
                // Bounds on impl-block-derived enum methods live in
                // fn.type_params[i].bounds (impl_type_params is empty
                // for this path — the impl's T flattens into the fn's
                // type_params at sema-collect time). Mirror method_bound_ok
                // logic against type_params.
                bool bounds_ok = true;
                for (auto& tp : fn_tparams) {
                    if (tp.bounds.empty()) continue;
                    auto sit = fn_subst.find(tp.name);
                    if (sit == fn_subst.end()) continue;
                    TypeRef concrete = sit->second;
                    if (!concrete) continue;
                    std::string cname;
                    auto ck = TypeRef(concrete).kind();
                    if (ck == LogosType::Kind::Struct ||
                        ck == LogosType::Kind::ZonedStruct)
                        cname = concrete_struct_name(concrete);
                    else if (ck == LogosType::Kind::Enum)
                        cname = TypeRef(concrete).enum_name();
                    else
                        cname = type_str(concrete);
                    if (auto p = cname.find("$G"); p != std::string::npos)
                        cname = cname.substr(0, p);
                    // &[u8] is the canonical wire form for str; impls
                    // register under "str" in the trait engine.
                    if (cname == "&[u8]") cname = "str";
                    for (auto& tb : tp.bounds) {
                        // Auto-trait check parity with method_bound_ok.
                        bool is_auto = false;
                        for (auto& td : out_.traits)
                            if (td.name() == tb.trait_name) {
                                is_auto = td.is_auto(); break;
                            }
                        if (is_auto) {
                            StrSet visited;
                            if (!is_auto_satisfied(concrete, tb.trait_name, visited)) {
                                bounds_ok = false; break;
                            }
                            continue;
                        }
                        StrSet seen;
                        // Recurse-aware: handles `impl<U: Bound> Trait
                        // for Wrapper<U>` correctly when concrete is
                        // Wrapper<X> with X not satisfying Bound.
                        // See [[baghunt-mono-blanket-bound-recursion]].
                        if (!mono_concrete_satisfies_bound(tb.trait_name, concrete, seen)) {
                            bounds_ok = false; break;
                        }
                    }
                    if (!bounds_ok) break;
                }
                if (!bounds_ok) continue;
                // Completeness gate: a blanket free-fn whose subst leaves some
                // type-param unbound (e.g. the OUTPUT `D` in
                // `impl<S, D: From<S>> Into<D> for S` when this path binds only
                // the target S=Concrete) must NOT be cloned — its body would
                // emit unsubstituted `D::method` (`@D__myfrom`) that can't
                // lower. The real call site (record_needed with full type_args
                // incl. the return-inferred D) emits the correct spec.
                bool fully_bound = true;
                for (auto& tp : fn_tparams) {
                    if (tp.is_variadic) continue;
                    if (!fn_subst.count(tp.name) && !fn_packs.count(tp.name)) {
                        fully_bound = false; break;
                    }
                }
                if (!fully_bound) continue;
                // Emit the alias copy (same body, alternate mangled name) so
                // both the bound-dispatch (append) and direct-call/operator
                // (cname-insert) forms link — emitting whichever name a caller
                // demanded but no path has produced yet. (One may already be
                // done via another call site; see need_primary/need_alias.)
                // Direct-build: DeclBuilder is non-copyable + emits in place, so
                // clone afresh for each needed name (replaces the old deep-copy).
                if (need_alias) {
                    auto alias = clone_fn(fn, fn_subst, fn_packs);
                    alias.str_always(lir_schema::decl_keys::NAME, alias_name);
                    done_.insert(alias_name);
                    auto alias_v = alias.view<lir_view::FunctionView>();
                    out_.functions.push_back(alias_v);
                    // Scan the cloned enum-method body for nested generic calls
                    // (mirrors the struct-method path's `scan_fn(*m)`). Without
                    // this, a method like `Option::take` that calls a generic FREE
                    // fn (`mem::replace_ref::<Option<T>>`) never enqueues that
                    // specialization → mlir-gen "does not reference a valid
                    // function". The enclosing fixpoint drains anything enqueued.
                    scan_fn(alias_v);
                }
                if (need_primary) {
                    auto nm = clone_fn(fn, fn_subst, fn_packs);
                    nm.str_always(lir_schema::decl_keys::NAME, inst_name);
                    done_.insert(inst_name);
                    auto nm_v = nm.view<lir_view::FunctionView>();
                    out_.functions.push_back(nm_v);
                    scan_fn(nm_v);
                }
            }
            out_.enums.push_back(std::move(inst));
            ++stats_.enum_instances;
        }
    }
}



} // namespace logos::compiler
