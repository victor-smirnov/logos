// Logos project — https://github.com/victor-smirnov/logos
//
// mono_clone.cpp — Expression/statement substitution and function/type cloning.

#include "mono_impl.hpp"
#include "logos/compiler/sha256.hpp"
#include <logos/compiler/lir_mirror.hpp>
#include <logos/compiler/lir_builder.hpp>
#include <functional>

namespace logos::compiler {

// ── Structural TypeHash (mini-Memoria block_type_hash / ctr_type_hash) ────────
//
// FNV-1a-64 over a tag-prefixed walk of T's structure. Layout-stable: bears
// no struct/field name, so refactors that don't change physical layout
// (rename fields, rename struct, internal reorganisation) preserve the hash.
// Inspired by legacy Memoria's `TypeHash<T>::Value` (compile-time MD5 over
// type-list of constituent codes); we use FNV-1a-64 to share infrastructure
// with HermesStatic and ObjectMap-key hashing already in stdlib.
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
static constexpr uint64_t TH_TAG_HSTAT  = 0x88;  // HermesStatic literal: mix const_val
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
            return th_mix_u64(h, t.arr_size());
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
        case K::HStaticLit: {
            // HermesStatic literal: identity = byte-hash of the underlying
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
            const lir::LStructDef* tmpl = nullptr;
            for (auto& sd : in_.structs)
                if (sd.name == base && (tpkg.empty() || sd.pkg == tpkg)) {
                    tmpl = &sd; break;
                }
            if (!tmpl)
                for (auto& sd : in_.structs)
                    if (sd.name == base) { tmpl = &sd; break; }
            if (!tmpl) {
                seen.erase(sk);
                return th_mix_u64(h, 0);
            }
            SubstMap fsubst;
            for (size_t i = 0, j = 0; i < tmpl->type_params.size(); ++i) {
                if (j < t.type_args().size())
                    fsubst[tmpl->type_params[i].name] = t.type_args()[j++];
            }
            h = th_mix_u64(h, static_cast<uint64_t>(tmpl->fields.size()));
            for (auto& f : tmpl->fields) {
                TypeRef ft = subst_type(f.type, fsubst);
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

    auto has_explicit = [&](const std::string& name) -> bool {
        return concrete_impls_.count(std::string(trait_name) + "::" + name) > 0;
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
    case Kind::FnPtr:
        return true;

    // Raw pointers: !Send/!Sync unless explicit unsafe impl (Rust rule).
    case Kind::Ptr: {
        std::string tstr = type_str(tv);
        return has_explicit(tstr);
    }

    // &T  : Send iff T: Sync; Sync iff T: Sync.
    case Kind::Ref:
        return is_auto_satisfied(tv.pointee(), "Sync", visited);

    // &mut T: Send iff T: Send; Sync iff T: Sync.
    case Kind::MutRef:
        return is_auto_satisfied(tv.pointee(),
                                  trait_name == "Send" ? "Send" : "Sync",
                                  visited);

    case Kind::Array:
        return tv.elem() ? is_auto_satisfied(tv.elem(), trait_name, visited) : true;
    case Kind::Slice:
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
        // Locate the struct definition. Look in out_ first (post-mono shape),
        // then in_ (pre-mono templates).
        const lir::LStructDef* sd = nullptr;
        for (auto& s : out_.structs) if (s.name == cn) { sd = &s; break; }
        if (!sd) for (auto& s : in_.structs) if (s.name == base) { sd = &s; break; }
        if (!sd) return true;  // unknown — be lenient (matches sema)
        // Build subst from struct's type-params to the concrete tv's type-args.
        SubstMap subst;
        if (!tv.type_args().empty() && !sd->type_params.empty()) {
            size_t n = std::min(tv.type_args().size(), sd->type_params.size());
            for (size_t j = 0; j < n; ++j)
                subst[sd->type_params[j].name] = tv.type_args()[j];
        }
        for (auto& f : sd->fields) {
            TypeRef ft = f.type;
            if (ft && ft.kind() == Kind::TypeVar && !subst.empty()) {
                auto it = subst.find(std::string(ft.type_var_name()));
                if (it != subst.end()) ft = it->second;
            }
            if (!is_auto_satisfied(ft, trait_name, visited)) return false;
        }
        return true;
    }

    case Kind::Enum: {
        std::string ename{tv.enum_name()};
        if (has_explicit(ename) || has_explicit(type_str(tv))) return true;
        const lir::LEnumDef* ed = nullptr;
        for (auto& e : out_.enums) if (e.name == ename) { ed = &e; break; }
        if (!ed) for (auto& e : in_.enums) if (e.name == ename) { ed = &e; break; }
        if (!ed) return true;
        for (auto& v : ed->variants)
            for (auto pt : v.payload_types)
                if (!is_auto_satisfied(pt, trait_name, visited)) return false;
        return true;
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

lir::LExprPtr Mono::subst_expr(const lir::LExpr& e, const SubstMap& s,
                          const PackMap& /*unused*/) {
    // packs are stored in cur_packs_ (set by clone_fn)
    auto* result = lir::alloc_expr(out_);
    result->type = subst_type(e.type, s);

    // Stage 3g.4b: every LExpr reaching mono is mirrored — sema's LirBuilder
    // emits per-node, and lir_mirror_emit_into runs once at end of sema. The
    // view switch handles all 41 expr Codes; no std::visit fallback remains.
    auto eref = expr_ref_of(e);
    if (!eref) {
        std::fprintf(stderr,
            "mono.subst_expr: input LExpr lacks mirror_offset_\n");
        std::abort();
    }
    {
        auto subst_child_expr = [&](lir_view::ExprRef er) -> lir::LExprPtr {
            auto* le = lexpr_of(er);
            return le ? subst_expr(*le, s) : nullptr;
        };
        auto subst_child_block = [&](lir_view::BlockRef br) -> lir::LBlock {
            auto* lb = lblock_of(br);
            return lb ? subst_block(*lb, s) : lir::LBlock{};
        };
        using C = lir_schema::expr::Code;
        switch (eref.kind()) {
        // Stage 2: variant-free leaf-kind cases. Mirror emitted directly,
        // result->kind stays at default (unread by view-based readers).
        case C::LitInt:
            result->mirror_offset_ = lir_mirror_emit_lit_int(
                out_, result->type, lir_view::ELitIntView{eref}.value());
            break;
        case C::LitFloat:
            result->mirror_offset_ = lir_mirror_emit_lit_float(
                out_, result->type, lir_view::ELitFloatView{eref}.value());
            break;
        case C::LitBool:
            result->mirror_offset_ = lir_mirror_emit_lit_bool(
                out_, result->type, lir_view::ELitBoolView{eref}.value());
            break;
        case C::LitStr: {
            // Copy to std::string: lir_mirror_emit_* may grow the arena,
            // which invalidates string_view pointers into it.
            std::string v(lir_view::ELitStrView{eref}.value());
            result->mirror_offset_ = lir_mirror_emit_lit_str(out_, result->type, v);
            break;
        }
        case C::VarRef: {
            std::string n(lir_view::EVarRefView{eref}.name());
            // Const-generic value-use: sema emits "__const_param:N" for a
            // `<const N: T>` param referenced in expression position. Two
            // lowerings depending on the param's kind:
            //   IntLit / scalar  → lit_int with the substituted value.
            //   HStaticLit       → splice in the registered HermesStatic
            //                       literal (same EHermesLit shape as
            //                       inline `let s: HermesStatic = @{...};`).
            constexpr std::string_view CP_PFX = "__const_param:";
            if (n.compare(0, CP_PFX.size(), CP_PFX) == 0) {
                std::string pname = n.substr(CP_PFX.size());
                auto sit = s.find(pname);
                if (sit != s.end() && sit->second) {
                    auto kind = TypeRef(sit->second).kind();
                    if (kind == LogosType::Kind::HStaticLit) {
                        uint64_t h = (uint64_t)sit->second.const_val().value_or(0);
                        auto rit = out_.hstatic_registry_.find(h);
                        if (rit == out_.hstatic_registry_.end()) {
                            std::fprintf(stderr,
                                "mono: __const_param:%s — HermesStatic registry "
                                "miss for hash %016llx\n",
                                pname.c_str(), (unsigned long long)h);
                            // Fall through to var-ref emission so we don't crash.
                        } else {
                            // Deep-clone the registered literal at this site.
                            // subst_expr returns a freshly-pooled LExpr in out_;
                            // splice its mirror_offset_ + type into `result` so
                            // the surrounding clone-loop sees a well-formed node.
                            SubstMap empty;
                            auto cloned = subst_expr(*rit->second, empty);
                            if (cloned) {
                                result->mirror_offset_ = cloned->mirror_offset_;
                                result->type           = cloned->type;
                                break;
                            }
                        }
                    }
                    if (sit->second.const_val()) {
                        int64_t v = *sit->second.const_val();
                        result->mirror_offset_ = lir_mirror_emit_lit_int(
                            out_, result->type, v);
                        break;
                    }
                }
            }
            result->mirror_offset_ = lir_mirror_emit_var_ref(out_, result->type, n);
            break;
        }
        case C::AddrOf: {
            std::string n(lir_view::EAddrOfView{eref}.var_name());
            result->mirror_offset_ = lir_mirror_emit_addr_of(out_, result->type, n);
            break;
        }
        case C::PackExpand: {
            std::string n(lir_view::EPackExpandView{eref}.var_name());
            result->mirror_offset_ = lir_mirror_emit_pack_expand(out_, result->type, n);
            break;
        }
        case C::SizeOf: {
            auto t = lir_view::ESizeOfView{eref}.elem_type(out_.type_pool.impl());
            result->mirror_offset_ = lir_mirror_emit_size_of(
                out_, result->type, subst_type(t, s));
            break;
        }
        case C::AlignOf: {
            auto t = lir_view::EAlignOfView{eref}.elem_type(out_.type_pool.impl());
            result->mirror_offset_ = lir_mirror_emit_align_of(
                out_, result->type, subst_type(t, s));
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
            result->mirror_offset_ = lir_mirror_emit_var_ref(
                out_, result->type, mangled);
            break;
        }
        case C::Deref: {
            auto op = subst_child_expr(lir_view::EDerefView{eref}.operand());
            result->mirror_offset_ = lir_mirror_emit_deref(
                out_, result->type, op);
            break;
        }
        case C::FieldRead: {
            lir_view::EFieldReadView v{eref};
            std::string field(v.field());
            auto rcv = subst_child_expr(v.receiver());
            result->mirror_offset_ = lir_mirror_emit_field_read(
                out_, result->type, rcv, field);
            break;
        }
        case C::TupleIndex: {
            lir_view::ETupleIndexView v{eref};
            uint32_t idx = v.index();
            auto rcv = subst_child_expr(v.receiver());
            result->mirror_offset_ = lir_mirror_emit_tuple_index(
                out_, result->type, rcv, idx);
            break;
        }
        case C::IndexRead: {
            lir_view::EIndexReadView v{eref};
            auto rcv = subst_child_expr(v.receiver());
            auto idx = subst_child_expr(v.index());
            result->mirror_offset_ = lir_mirror_emit_index_read(
                out_, result->type, rcv, idx);
            break;
        }
        case C::Cast: {
            lir_view::ECastView v{eref};
            std::string hbf(v.hermes_build_fn());
            auto op = subst_child_expr(v.operand());
            result->mirror_offset_ = lir_mirror_emit_cast(
                out_, result->type, op, hbf);
            break;
        }
        case C::Try: {
            lir_view::ETryView v{eref};
            int32_t ok_disc  = v.ok_disc();
            int32_t err_disc = v.err_disc();
            auto inner = subst_child_expr(v.inner());
            result->mirror_offset_ = lir_mirror_emit_try(
                out_, result->type, inner, ok_disc, err_disc);
            break;
        }
        case C::SliceLit: {
            lir_view::ESliceLitView v{eref};
            auto base = subst_child_expr(v.base());
            auto len  = subst_child_expr(v.len());
            result->mirror_offset_ = lir_mirror_emit_slice_lit(
                out_, result->type, base, len);
            break;
        }
        case C::SliceIndex: {
            lir_view::ESliceIndexView v{eref};
            auto slice = subst_child_expr(v.slice());
            auto idx   = subst_child_expr(v.index());
            result->mirror_offset_ = lir_mirror_emit_slice_index(
                out_, result->type, slice, idx);
            break;
        }
        case C::SliceLen: {
            auto sl = subst_child_expr(lir_view::ESliceLenView{eref}.slice());
            result->mirror_offset_ = lir_mirror_emit_slice_len(out_, result->type, sl);
            break;
        }
        case C::SlicePtr: {
            auto sl = subst_child_expr(lir_view::ESlicePtrView{eref}.slice());
            result->mirror_offset_ = lir_mirror_emit_slice_ptr(out_, result->type, sl);
            break;
        }
        case C::IfExpr: {
            lir_view::EIfExprView v{eref};
            auto cond = subst_child_expr(v.cond());
            auto thn  = subst_child_expr(v.then_val());
            auto els  = subst_child_expr(v.else_val());
            result->mirror_offset_ = lir_mirror_emit_if_expr(
                out_, result->type, cond, thn, els);
            break;
        }
        case C::TupleLit: {
            std::vector<lir::LExprPtr> elems;
            lir_view::ETupleLitView{eref}.each_elem(
                [&](lir_view::ExprRef er) { elems.push_back(subst_child_expr(er)); });
            result->mirror_offset_ = lir_mirror_emit_tuple_lit(
                out_, result->type, elems);
            break;
        }
        case C::ArrLit: {
            std::vector<lir::LExprPtr> elems;
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
                result->type && result->type.kind() == LogosType::Kind::Array &&
                result->type.arr_size() > 1) {
                uint64_t target = result->type.arr_size();
                while (elems.size() < target)
                    elems.push_back(subst_child_expr(fill_src));
            }
            result->mirror_offset_ = lir_mirror_emit_arr_lit(
                out_, result->type, elems);
            break;
        }
        case C::ClosureCall: {
            lir_view::EClosureCallView v{eref};
            auto callee = subst_child_expr(v.callee());
            std::vector<lir::LExprPtr> args;
            v.each_arg([&](lir_view::ExprRef er) { args.push_back(subst_child_expr(er)); });
            // Sprint 5.7 follow-up: if the synth-closure path in
            // sema_expr::lower_call emitted ClosureCall for a generic
            // `F: FnOnce(args) -> R` and mono substituted F to a
            // concrete FnPtr type, switch the call kind. The callee
            // var-ref now carries the substituted type (sema emitted
            // var_ref with TypeVar F; mono's subst_type rewrites it
            // to the concrete instantiation).
            bool route_to_fn_ptr = callee && callee->type &&
                TypeRef(callee->type).kind() == LogosType::Kind::FnPtr;
            if (route_to_fn_ptr) {
                result->mirror_offset_ = lir_mirror_emit_fn_ptr_call(
                    out_, result->type, callee, args);
            } else {
                result->mirror_offset_ = lir_mirror_emit_closure_call(
                    out_, result->type, callee, args);
            }
            break;
        }
        case C::FnPtrCall: {
            lir_view::EFnPtrCallView v{eref};
            auto callee = subst_child_expr(v.callee());
            std::vector<lir::LExprPtr> args;
            v.each_arg([&](lir_view::ExprRef er) { args.push_back(subst_child_expr(er)); });
            result->mirror_offset_ = lir_mirror_emit_fn_ptr_call(
                out_, result->type, callee, args);
            break;
        }
        case C::FormatCall: {
            lir_view::EFormatCallView v{eref};
            auto fmt = subst_child_expr(v.fmt());
            auto arg_types = v.arg_types(out_.type_pool.impl());
            std::vector<lir::LExprPtr> args;
            v.each_arg([&](lir_view::ExprRef er) { args.push_back(subst_child_expr(er)); });
            result->mirror_offset_ = lir_mirror_emit_format_call(
                out_, result->type, fmt, args, arg_types);
            break;
        }
        case C::PtrArith: {
            lir_view::EPtrArithView v{eref};
            uint8_t op = v.op_code();
            auto ptr = subst_child_expr(v.ptr());
            auto off = subst_child_expr(v.offset());
            result->mirror_offset_ = lir_mirror_emit_ptr_arith(
                out_, result->type, op, ptr, off);
            break;
        }
        case C::PtrDiff: {
            lir_view::EPtrDiffView v{eref};
            bool by_byte = v.by_byte();
            auto lhs = subst_child_expr(v.lhs());
            auto rhs = subst_child_expr(v.rhs());
            result->mirror_offset_ = lir_mirror_emit_ptr_diff(
                out_, result->type, by_byte, lhs, rhs);
            break;
        }
        case C::BlockExpr: {
            lir_view::EBlockExprView v{eref};
            lir::LBlock* nb_block = nullptr;
            lir::LExprPtr nb_result = nullptr;
            if (auto br = v.block(); br) {
                nb_block = lir::alloc_block(out_, subst_child_block(br));
                lir_mirror_emit_block_node(out_, *nb_block);
            }
            if (auto rr = v.result(); rr)
                nb_result = subst_child_expr(rr);
            result->mirror_offset_ = lir_mirror_emit_block_expr(
                out_, result->type, nb_block, nb_result);
            break;
        }
        case C::ReflectOf: {
            auto resolved = subst_type(
                lir_view::EReflectOfView{eref}.type(out_.type_pool.impl()), s);
            result->mirror_offset_ = lir_mirror_emit_reflect_of(
                out_, result->type, resolved);
            if (resolved && TypeRef(resolved).kind() == LogosType::Kind::ZonedStruct &&
                TypeRef(resolved).type_args().empty()) {
                std::string pkg{TypeRef(resolved).pkg_name()};
                std::string fqn = pkg.empty() ? std::string(TypeRef(resolved).struct_name())
                                              : pkg + "::" + std::string(TypeRef(resolved).struct_name());
                out_.reflect_requests.insert(fqn);
            }
            break;
        }
        case C::Unary: {
            lir_view::EUnaryView v{eref};
            std::string op{v.op()};
            auto new_op = subst_child_expr(v.operand());
            auto vt = new_op ? new_op->type : TypeRef{};
            if (vt && TypeRef(vt).kind() == LogosType::Kind::Struct) {
                std::string method_name;
                if      (op == "-") method_name = "neg";
                else if (op == "!") method_name = "not_";
                if (!method_name.empty()) {
                    std::string bare = concrete_struct_name(vt) + "__" + method_name;
                    std::string pkg{vt.pkg_name()};
                    std::string callee = pkg.empty() ? bare : pkg + "." + bare;
                    std::vector<lir::LExprPtr> args; args.push_back(std::move(new_op));
                    result->mirror_offset_ = lir_mirror_emit_call(
                        out_, result->type, callee, {}, args);
                    break;
                }
            }
            result->mirror_offset_ = lir_mirror_emit_unary(
                out_, result->type, op, new_op);
            break;
        }
        case C::BinOp: {
            lir_view::EBinOpView v{eref};
            std::string op{v.op()};
            auto new_lhs = subst_child_expr(v.lhs());
            auto new_rhs = subst_child_expr(v.rhs());
            auto lt = new_lhs ? new_lhs->type : TypeRef{};
            if (lt && TypeRef(lt).kind() == LogosType::Kind::Struct) {
                std::string method_name;
                if      (op == "+")  method_name = "add";
                else if (op == "-")  method_name = "sub";
                else if (op == "*")  method_name = "mul";
                else if (op == "/")  method_name = "div";
                else if (op == "%")  method_name = "rem";
                else if (op == "==") method_name = "eq";
                else if (op == "!=") method_name = "ne";
                else if (op == "<")  method_name = "lt";
                else if (op == "<=") method_name = "le";
                else if (op == ">")  method_name = "gt";
                else if (op == ">=") method_name = "ge";
                if (!method_name.empty()) {
                    std::string bare = concrete_struct_name(lt) + "__" + method_name;
                    std::string pkg{lt.pkg_name()};
                    std::string callee = pkg.empty() ? bare : pkg + "." + bare;
                    std::vector<lir::LExprPtr> args;
                    args.push_back(std::move(new_lhs));
                    args.push_back(std::move(new_rhs));
                    result->mirror_offset_ = lir_mirror_emit_call(
                        out_, result->type, callee, {}, args);
                    break;
                }
            }
            result->mirror_offset_ = lir_mirror_emit_bin_op(
                out_, result->type, op, new_lhs, new_rhs);
            break;
        }
        case C::AddrOfTemp: {
            lir_view::EAddrOfTempView v{eref};
            bool is_mut = v.is_mut();
            auto inner = subst_child_expr(v.inner());
            result->mirror_offset_ = lir_mirror_emit_addr_of_temp(
                out_, result->type, inner, is_mut);
            break;
        }
        case C::EnumLit: {
            lir_view::EEnumLitView v{eref};
            std::string enum_name(v.enum_name());
            std::string variant(v.variant());
            int64_t disc = v.disc();
            TypeRef rt(result->type);
            if (rt && rt.kind() == LogosType::Kind::Enum &&
                !rt.type_args().empty()) {
                std::string cname = std::string(rt.enum_name());
                for (auto a : rt.type_args()) { cname += "__"; cname += mangle_type(a); }
                enum_name = std::move(cname);
                record_needed_enum(result->type);
            }
            result->mirror_offset_ = lir_mirror_emit_enum_lit(
                out_, result->type, enum_name, variant, disc);
            break;
        }
        case C::EnumLitData: {
            lir_view::EEnumLitDataView v{eref};
            std::string variant(v.variant());
            int64_t disc = v.disc();
            std::string enum_name;
            TypeRef rt(result->type);
            if (rt && rt.kind() == LogosType::Kind::Enum &&
                !rt.type_args().empty()) {
                std::string cname = std::string(rt.enum_name());
                for (auto a : rt.type_args()) { cname += "__"; cname += mangle_type(a); }
                enum_name = std::move(cname);
                record_needed_enum(result->type);
            } else {
                enum_name = std::string(v.enum_name());
            }
            std::vector<lir::LExprPtr> payload;
            v.each_payload([&](lir_view::ExprRef er) {
                payload.push_back(subst_child_expr(er));
            });
            result->mirror_offset_ = lir_mirror_emit_enum_lit_data(
                out_, result->type, enum_name, variant, disc, payload);
            break;
        }
        case C::StructLit: {
            lir_view::EStructLitView v{eref};
            std::string name;
            TypeRef rt2(result->type);
            if (rt2 && (rt2.kind() == LogosType::Kind::Struct ||
                        rt2.kind() == LogosType::Kind::ZonedStruct) &&
                !rt2.type_args().empty())
                name = concrete_struct_name(result->type);
            else
                name = std::string(v.name());
            std::vector<std::pair<std::string, lir::LExprPtr>> fields;
            v.each_field([&](std::string_view fname, lir_view::ExprRef er) {
                fields.push_back({std::string(fname), subst_child_expr(er)});
            });
            record_needed_struct(result->type);
            result->mirror_offset_ = lir_mirror_emit_struct_lit(
                out_, result->type, name, fields);
            break;
        }
        case C::New: {
            lir_view::ENewView v{eref};
            std::string class_name(v.class_name());
            std::vector<std::pair<std::string, lir::LExprPtr>> fields;
            v.each_field([&](std::string_view fname, lir_view::ExprRef er) {
                fields.push_back({std::string(fname), subst_child_expr(er)});
            });
            result->mirror_offset_ = lir_mirror_emit_new(
                out_, result->type, class_name, fields);
            break;
        }
        case C::MatchExpr: {
            lir_view::EMatchExprView v{eref};
            auto scrut = subst_child_expr(v.scrut());
            std::vector<lir::EMatchArm> arms;
            PatSubstWalker pw([&](TypeRef t) { return subst_type(t, s); },
                              out_.type_pool.impl(), &out_);
            v.each_arm([&](lir_view::EMatchArmRef arm) {
                lir::EMatchArm na;
                if (auto pr = arm.pat(); pr) na.pat = pw.walk(pr);
                else                         na.pat = lir::Pattern{};
                if (auto gr = arm.guard(); gr)
                    na.guard = subst_child_expr(gr);
                na.value = subst_child_expr(arm.value());
                arms.push_back(std::move(na));
            });
            result->mirror_offset_ = lir_mirror_emit_match_expr(
                out_, result->type, scrut, arms);
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
                result->mirror_offset_ = lir_mirror_emit_type_code_of(
                    out_, result->type, resolved);
            } else {
                uint64_t code = 0;
                if (TypeRef(resolved).kind() == LogosType::Kind::Struct ||
                    TypeRef(resolved).kind() == LogosType::Kind::ZonedStruct) {
                    std::string mangled = TypeRef(resolved).type_args().empty()
                        ? std::string(TypeRef(resolved).struct_name())
                        : concrete_struct_name(resolved);
                    for (auto& sd : out_.structs)
                        if (sd.name == mangled && sd.type_code != 0)
                            { code = sd.type_code; break; }
                    if (code == 0)
                        for (auto& ia : out_.inst_annotations)
                            if (ia.mangled_name == mangled && ia.type_code != 0)
                                { code = ia.type_code; break; }
                }
                if (code == 0) {
                    auto hash = type_hash_23(type_str(resolved));
                    uint64_t raw = type_hash_56bit(hash);
                    code = (raw < 128) ? (raw + 128) : raw;
                }
                result->mirror_offset_ = lir_mirror_emit_lit_int(
                    out_, result->type, (int64_t)code);
            }
            break;
        }
        case C::HermesLit: {
            lir_view::EHermesLitView v{eref};
            namespace hvc = lir_schema::hermes_val;
            std::function<lir::HermesValPtr(lir_view::HermesValRef)> clone_hv =
                [&](lir_view::HermesValRef vref) -> lir::HermesValPtr {
                auto out = lir::alloc_hermes_val(out_);
                if (!vref) {
                    std::fprintf(stderr,
                        "mono.clone_hv: null HermesValRef\n");
                    std::abort();
                }
                switch (vref.kind()) {
                case hvc::Code::Null:
                    out->mirror_offset_ = lir_mirror_emit_hv_null(out_);
                    break;
                case hvc::Code::Bool:
                    out->mirror_offset_ = lir_mirror_emit_hv_bool(
                        out_, lir_view::HVBoolView{vref}.value());
                    break;
                case hvc::Code::Int:
                    out->mirror_offset_ = lir_mirror_emit_hv_int(
                        out_, lir_view::HVIntView{vref}.value());
                    break;
                case hvc::Code::Float:
                    out->mirror_offset_ = lir_mirror_emit_hv_float(
                        out_, lir_view::HVFloatView{vref}.value());
                    break;
                case hvc::Code::Str: {
                    std::string s(lir_view::HVStrView{vref}.value());
                    out->mirror_offset_ = lir_mirror_emit_hv_str(out_, s);
                    break;
                }
                case hvc::Code::Capture: {
                    lir_view::HVCaptureView cv{vref};
                    out->mirror_offset_ = lir_mirror_emit_hv_capture(
                        out_, cv.param_index(), cv.value_index());
                    break;
                }
                case hvc::Code::Type: {
                    lir_view::HVTypeView tv{vref};
                    out->mirror_offset_ = lir_mirror_emit_hv_type(
                        out_, tv.kind(), tv.uid(), std::string(tv.name()));
                    break;
                }
                case hvc::Code::Map: {
                    lir_view::HVMapView mv{vref};
                    std::string key_type(mv.key_type());
                    bool int_keys = mv.int_keyed();
                    std::vector<lir::HVMapEntry> entries;
                    entries.reserve(mv.size());
                    for (uint64_t i = 0; i < mv.size(); ++i) {
                        lir::HVMapEntry ent;
                        if (int_keys) ent.key = mv.int_key(i);
                        else          ent.key = std::string(mv.str_key(i));
                        ent.val = clone_hv(mv.value(i));
                        entries.push_back(std::move(ent));
                    }
                    out->mirror_offset_ = lir_mirror_emit_hv_map(
                        out_, entries, key_type);
                    break;
                }
                case hvc::Code::Array: {
                    lir_view::HVArrayView av{vref};
                    std::string elem_type(av.elem_type());
                    std::vector<lir::HermesValPtr> elements;
                    elements.reserve(av.size());
                    for (uint64_t i = 0; i < av.size(); ++i)
                        elements.push_back(clone_hv(av.elem(i)));
                    out->mirror_offset_ = lir_mirror_emit_hv_array(
                        out_, elements, elem_type);
                    break;
                }
                }
                return out;
            };
            lir::HermesValPtr root = nullptr;
            if (auto root_ref = v.root()) root = clone_hv(root_ref);
            bool has_captures = v.has_captures();
            uint32_t capture_param_count = v.capture_param_count();
            std::vector<lir::LExprPtr> capture_exprs;
            std::vector<TypeRef> capture_types;
            v.each_capture_expr([&](lir_view::ExprRef er) {
                capture_exprs.push_back(subst_child_expr(er));
            });
            v.each_capture_type(out_.type_pool.impl(),
                [&](TypeRef ct) { capture_types.push_back(subst_type(ct, s)); });
            result->mirror_offset_ = lir_mirror_emit_hermes_lit(
                out_, result->type, root, has_captures,
                capture_exprs, capture_types, capture_param_count,
                v.static_blob());
            break;
        }
        case C::Call: {
            lir_view::ECallView v{eref};
            lir::ECall nc;
            nc.callee = std::string(v.callee());
            for (auto ta : v.type_args(out_.type_pool.impl())) {
                // Pack-key may be encoded as TypeVar (type pack) or ConstVar
                // (const pack); both store the pack name in `type_var_name`.
                if (ta && (ta.kind() == LogosType::Kind::TypeVar ||
                           ta.kind() == LogosType::Kind::ConstVar)) {
                    auto pit = cur_packs_.find(std::string(ta.type_var_name()));
                    if (pit != cur_packs_.end()) {
                        for (auto pt : pit->second) nc.type_args.push_back(pt);
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
                result->mirror_offset_ = lir_mirror_emit_lit_int(out_, result->type, n);
                break;
            }
            // type_of::<T>() intrinsic: sema lowered to magic call with T in
            // type_args[0]. After subst_type above, type_args[0] is the
            // concrete monomorphized type — emit its kind as u32 literal.
            if (nc.callee == "__type_kind_of__") {
                int64_t k = nc.type_args.empty() ? 0
                          : (int64_t)nc.type_args[0].kind();
                result->mirror_offset_ = lir_mirror_emit_lit_int(out_, result->type, k);
                break;
            }
            // hstatic_hash_of::<CFG>() — byte-hash identity of CFG as u64.
            // Post-subst, type_args[0] is HStaticLit kind whose const_val
            // is the hash.
            if (nc.callee == "__hstatic_hash_of__") {
                int64_t v = (nc.type_args.empty() || !nc.type_args[0])
                          ? 0
                          : (int64_t)(uint64_t)nc.type_args[0].const_val().value_or(0);
                result->mirror_offset_ = lir_mirror_emit_lit_int(out_, result->type, v);
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
                result->mirror_offset_ = lir_mirror_emit_lit_int(
                    out_, result->type, (int64_t)h);
                break;
            }
            // type_of::<T>().name — magic intrinsic that mono replaces with
            // a &[u8] literal of the canonical type_str(T) for concrete T.
            if (nc.callee == "__type_name_of__") {
                std::string s = nc.type_args.empty() ? std::string()
                              : type_str(nc.type_args[0]);
                result->mirror_offset_ = lir_mirror_emit_lit_str(out_, result->type, s);
                break;
            }
            // type_of::<T>().uid — TypeUID = first 8 bytes of
            // SHA-256(type_str(T)) as u64. Stable identity for
            // quote_ty! reification (mono-time reverse lookup) and the
            // shared byte source for Hermes schema_type_code / TagSystem.
            if (nc.callee == "__type_uid_of__") {
                uint64_t uid = 0;
                if (!nc.type_args.empty() && nc.type_args[0]) {
                    uid = type_hash_64bit(type_hash_23(type_str(nc.type_args[0])));
                    uid_to_type_[uid] = nc.type_args[0];
                }
                result->mirror_offset_ = lir_mirror_emit_lit_int(
                    out_, result->type, (int64_t)uid);
                break;
            }
            // Type-trait predicates: mono evaluates after substitution. Each
            // returns a lit_bool of the answer for the concrete substituted T.
            {
                using K = LogosType::Kind;
                auto bool_of = [&](bool r) {
                    result->mirror_offset_ =
                        lir_mirror_emit_lit_bool(out_, result->type, r);
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
                    bool_of(tk == K::I8 || tk == K::I16 || tk == K::I24 ||
                            tk == K::I32 || tk == K::I56 || tk == K::I64 ||
                            tk == K::I128); break;
                }
                else if (nc.callee == "__is_unsigned__") {
                    bool_of(tk == K::U8 || tk == K::U16 || tk == K::U24 ||
                            tk == K::U32 || tk == K::U56 || tk == K::U64 ||
                            tk == K::U128); break;
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
                result->type = i64_t;
                result->mirror_offset_ = lit->mirror_offset_;
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
                        std::string cname;
                        if (T.kind() == LogosType::Kind::Struct ||
                            T.kind() == LogosType::Kind::ZonedStruct)
                            cname = concrete_struct_name(T);
                        else if (T.kind() == LogosType::Kind::Enum)
                            cname = std::string(T.enum_name());
                        else
                            cname = type_str(T);
                        if (auto p = cname.find("$G"); p != std::string::npos)
                            cname = cname.substr(0, p);
                        // Sprint 5.7: route through trait_engine (same
                        // semantics as the inlined walker that used to
                        // live here). Engine populates lazily from
                        // concrete_impls_ + blanket_impls_.
                        StrSet unused;
                        answer = mono_has_impl_recursive(trait, cname, unused);
                    }
                }
                LogosTypeBuilder b_b; b_b.kind = LogosType::Kind::Bool;
                TypeRef bool_t = out_.type_pool.alloc(std::move(b_b));
                result->type = bool_t;
                result->mirror_offset_ =
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
                    std::string cname;
                    if (T.kind() == LogosType::Kind::Struct ||
                        T.kind() == LogosType::Kind::ZonedStruct)
                        cname = concrete_struct_name(T);
                    else if (T.kind() == LogosType::Kind::Enum)
                        cname = std::string(T.enum_name());
                    else
                        cname = type_str(T);
                    if (auto p = cname.find("$G"); p != std::string::npos)
                        cname = cname.substr(0, p);
                    // Sprint 5.7: route through trait_engine. Same
                    // semantics as the inlined walker that used to
                    // live here.
                    StrSet unused;
                    answer = mono_has_impl_recursive(trait, cname, unused);
                }
                LogosTypeBuilder b_b; b_b.kind = LogosType::Kind::Bool;
                TypeRef bool_t = out_.type_pool.alloc(std::move(b_b));
                result->type = bool_t;
                result->mirror_offset_ =
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
                result->type = i64_t;
                result->mirror_offset_ = lit->mirror_offset_;
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
                TypeRef type_t = result->type;  // sema set this to struct Type
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
                    uint64_t uid = type_hash_64bit(type_hash_23(type_str(ti)));
                    uid_to_type_[uid] = ti;
                    f.emplace_back("uid", b.lit_int((int64_t)uid, u64_t));
                }
                auto sl = b.struct_lit("Type", std::move(f), type_t);
                result->type = type_t;
                result->mirror_offset_ = sl->mirror_offset_;
                break;
            }
            // reify_type(t: Type) -> Type — recover source TypeRef from
            // a direct producer expression and emit a fresh `Type`
            // struct lit. MVP shapes (no let indirection):
            //   1. struct_lit("Type", ...) where the "uid" field is
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
                uint64_t uid = type_hash_64bit(type_hash_23(type_str(ti)));
                uid_to_type_[uid] = ti;
                TypeRef type_t = result->type;
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
                auto sl = b.struct_lit("Type", std::move(f), type_t);
                result->type = type_t;
                result->mirror_offset_ = sl->mirror_offset_;
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
                        LogosTypeBuilder sb;
                        sb.kind = LogosType::Kind::Struct;
                        sb.struct_name = tmpl_name;
                        for (auto& s : out_.structs)
                            if (s.name == tmpl_name) { sb.pkg_name = s.pkg; break; }
                        sb.type_args = direct;
                        TypeRef inst_t = out_.type_pool.alloc(std::move(sb));
                        collect_type_for_structs(inst_t);
                        uint64_t uid = type_hash_64bit(
                            type_hash_23(type_str(inst_t)));
                        uid_to_type_[uid] = inst_t;
                        TypeRef type_t = result->type;
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
                        auto sl = b.struct_lit("Type",
                                               std::move(f), type_t);
                        result->type = type_t;
                        result->mirror_offset_ = sl->mirror_offset_;
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
                LogosTypeBuilder sb;
                sb.kind = LogosType::Kind::Struct;
                sb.struct_name = tmpl_name;
                // M0.4 follow-up: thread pkg from the template definition
                // so metacall-instantiated specs share UID/registry keys
                // with the rest of the pipeline.
                for (auto& s : out_.structs)
                    if (s.name == tmpl_name) { sb.pkg_name = s.pkg; break; }
                sb.type_args = targs;
                TypeRef inst_t = out_.type_pool.alloc(std::move(sb));
                collect_type_for_structs(inst_t);
                uint64_t uid = type_hash_64bit(type_hash_23(type_str(inst_t)));
                uid_to_type_[uid] = inst_t;
                TypeRef type_t = result->type;
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
                auto sl = b.struct_lit("Type", std::move(f), type_t);
                result->type = type_t;
                result->mirror_offset_ = sl->mirror_offset_;
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
                LogosTypeBuilder sb;
                sb.kind = LogosType::Kind::Struct;
                sb.struct_name = tmpl_name;
                // M0.4 follow-up: thread pkg from the template definition
                // so metacall-instantiated specs share UID/registry keys
                // with the rest of the pipeline.
                for (auto& s : out_.structs)
                    if (s.name == tmpl_name) { sb.pkg_name = s.pkg; break; }
                sb.type_args = targs;
                TypeRef inst_t = out_.type_pool.alloc(std::move(sb));
                collect_type_for_structs(inst_t);
                uint64_t uid = type_hash_64bit(type_hash_23(type_str(inst_t)));
                uid_to_type_[uid] = inst_t;
                TypeRef type_t = result->type;
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
                auto sl = b.struct_lit("Type", std::move(f), type_t);
                result->type = type_t;
                result->mirror_offset_ = sl->mirror_offset_;
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
                uint64_t uid = type_hash_64bit(type_hash_23(type_str(inst_t)));
                uid_to_type_[uid] = inst_t;
                TypeRef type_t = result->type;
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
                auto sl = b.struct_lit("Type", std::move(f), type_t);
                result->type = type_t;
                result->mirror_offset_ = sl->mirror_offset_;
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
                const lir::LEnumDef* edef = nullptr;
                if (!nc.type_args.empty()) {
                    TypeRef E = nc.type_args[0];
                    if (E && E.kind() == LogosType::Kind::Enum) {
                        std::string base{E.enum_name()};
                        for (auto& ed : in_.enums)
                            if (ed.name == base) { edef = &ed; break; }
                    }
                }
                LirBuilder b(out_);
                if (nc.callee == "__variant_count_of__") {
                    int64_t n = edef ? (int64_t)edef->variants.size() : 0;
                    LogosTypeBuilder i64_b; i64_b.kind = LogosType::Kind::I64;
                    TypeRef i64_t = out_.type_pool.alloc(std::move(i64_b));
                    auto lit = b.lit_int(n, i64_t);
                    result->type = i64_t;
                    result->mirror_offset_ = lit->mirror_offset_;
                    break;
                }
                TypeRef elem_t = result->type ? result->type.elem() : nullptr;
                std::vector<lir::LExprPtr> elems;
                if (nc.callee == "__variant_names_of__") {
                    if (edef)
                        for (auto& vr : edef->variants)
                            elems.push_back(b.lit_str(vr.name, elem_t));
                } else if (nc.callee == "__variant_payload_counts_of__") {
                    if (edef)
                        for (auto& vr : edef->variants)
                            elems.push_back(b.lit_int(
                                (int64_t)vr.payload_types.size(), elem_t));
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
                        for (auto& vr : edef->variants) {
                            for (auto& pt : vr.payload_types) {
                                TypeRef pty = subst_type(pt, s);
                                if (!pty) pty = pt;
                                uint64_t uid = type_hash_64bit(
                                    type_hash_23(type_str(pty)));
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
                                    b.struct_lit("Type", std::move(f), elem_t));
                            }
                        }
                    }
                }
                LogosTypeBuilder ab; ab.kind = LogosType::Kind::Array;
                ab.elem = elem_t;
                ab.arr_size = (int64_t)elems.size();
                TypeRef new_arr_t = out_.type_pool.alloc(std::move(ab));
                result->type = new_arr_t;
                result->mirror_offset_ =
                    lir_mirror_emit_arr_lit(out_, new_arr_t, elems);
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
                result->type = i64_t;
                result->mirror_offset_ = lit->mirror_offset_;
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
                        const lir::LStructDef* match = nullptr;
                        for (auto& sd : in_.structs)
                            if (sd.name == base &&
                                (tpkg.empty() || sd.pkg == tpkg)) {
                                match = &sd; break;
                            }
                        if (!match)
                            for (auto& sd : in_.structs)
                                if (sd.name == base) { match = &sd; break; }
                        if (match) n = (int64_t)match->fields.size();
                    }
                }
                LirBuilder b(out_);
                LogosTypeBuilder i64_b; i64_b.kind = LogosType::Kind::I64;
                TypeRef i64_t = out_.type_pool.alloc(std::move(i64_b));
                auto lit = b.lit_int(n, i64_t);
                result->type = i64_t;
                result->mirror_offset_ = lit->mirror_offset_;
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
                        const lir::LStructDef* tmpl = nullptr;
                        for (auto& sd : in_.structs)
                            if (sd.name == base &&
                                (tpkg.empty() || sd.pkg == tpkg)) {
                                tmpl = &sd; break;
                            }
                        if (!tmpl)
                            for (auto& sd : in_.structs)
                                if (sd.name == base) { tmpl = &sd; break; }
                        if (tmpl)
                            for (auto& f : tmpl->fields)
                                field_names.push_back(f.name);
                    }
                }
                TypeRef elem_t = result->type ? result->type.elem() : nullptr;
                LirBuilder b(out_);
                std::vector<lir::LExprPtr> elems;
                for (auto& nm : field_names)
                    elems.push_back(b.lit_str(nm, elem_t));
                LogosTypeBuilder ab; ab.kind = LogosType::Kind::Array;
                ab.elem = elem_t;
                ab.arr_size = (int64_t)field_names.size();
                TypeRef new_arr_t = out_.type_pool.alloc(std::move(ab));
                result->type = new_arr_t;
                result->mirror_offset_ =
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
                            const lir::LStructDef* tmpl = nullptr;
                            for (auto& sd : in_.structs)
                                if (sd.name == base &&
                                    (tpkg.empty() || sd.pkg == tpkg)) {
                                    tmpl = &sd; break;
                                }
                            if (!tmpl)
                                for (auto& sd : in_.structs)
                                    if (sd.name == base) { tmpl = &sd; break; }
                            if (tmpl) {
                                SubstMap fsubst;
                                for (size_t i = 0, j = 0;
                                     i < tmpl->type_params.size(); ++i) {
                                    if (j < T.type_args().size())
                                        fsubst[tmpl->type_params[i].name] =
                                            T.type_args()[j++];
                                }
                                for (auto& f : tmpl->fields)
                                    elem_types.push_back(subst_type(f.type, fsubst));
                            }
                        }
                    }
                } else {
                    elem_types = nc.type_args;
                }
                TypeRef elem_t = result->type ? result->type.elem() : nullptr;
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
                std::vector<lir::LExprPtr> elems;
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
                        uint64_t uid = type_hash_64bit(type_hash_23(type_str(ti)));
                        uid_to_type_[uid] = ti;
                        f.emplace_back("uid", b.lit_int((int64_t)uid, u64_t));
                    }
                    elems.push_back(b.struct_lit("Type", std::move(f), elem_t));
                }
                LogosTypeBuilder ab; ab.kind = LogosType::Kind::Array;
                ab.elem = elem_t;
                ab.arr_size = (int64_t)elem_types.size();
                TypeRef new_arr_t = out_.type_pool.alloc(std::move(ab));
                result->type = new_arr_t;
                result->mirror_offset_ =
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
                    nc.args.push_back(subst_child_expr(ar));
                }
            });
            // Generic static-trait-dispatch: rewrite "[pkg.]DT__method" prefix
            // when DT is bound by the substitution map. Pkg prefix (set by
            // unified mangling) is stripped before checking the subst map.
            {
                std::string callee_pkg;
                std::string callee_body = nc.callee;
                // Pkg may have inner dots; split at LAST dot.
                if (auto dot = callee_body.rfind('.'); dot != std::string::npos) {
                    callee_pkg = callee_body.substr(0, dot);
                    callee_body = callee_body.substr(dot + 1);
                }
                auto sep = callee_body.find("__");
                if (sep != std::string::npos) {
                    std::string prefix = callee_body.substr(0, sep);
                    auto it = s.find(prefix);
                    if (it != s.end() && it->second) {
                        std::string cname;
                        TypeRef t = it->second;
                        if (TypeRef(t).kind() == LogosType::Kind::Struct)
                            cname = concrete_struct_name(t);
                        else
                            cname = type_str(t);
                        if (cname == "&[u8]") cname = "str";
                        if (!cname.empty()) {
                            // Use the substituted type's pkg if available.
                            std::string new_pkg{TypeRef(t).pkg_name()};
                            if (new_pkg.empty()) new_pkg = callee_pkg;
                            std::string bare = cname + callee_body.substr(sep);
                            nc.callee = new_pkg.empty() ? bare : new_pkg + "." + bare;
                        }
                    }
                }
            }
            // Rewrite callee if it's a generic call already instantiated.
            if (!nc.type_args.empty()) {
                bool rewritten_as_struct_method = false;
                auto sep = nc.callee.find("__");
                if (sep != std::string::npos) {
                    std::string struct_part = nc.callee.substr(0, sep);
                    std::string method_part = nc.callee.substr(sep);
                    auto sit = struct_templates_.find(struct_part);
                    if (sit != struct_templates_.end()) {
                        bool all_concrete = true;
                        for (auto ta : nc.type_args)
                            if (ta && TypeRef(ta).kind() == LogosType::Kind::TypeVar)
                                { all_concrete = false; break; }
                        if (all_concrete) {
                            size_t n_impl_tp = sit->second->type_params.size();
                            size_t n_args    = std::min(n_impl_tp, nc.type_args.size());
                            std::vector<TypeRef> args(
                                nc.type_args.begin(), nc.type_args.begin() + n_args);
                            std::string cname = concrete_struct_name_raw(struct_part, args);
                            nc.callee = cname + method_part;
                            nc.type_args.clear();
                            rewritten_as_struct_method = true;
                        }
                    }
                }
                if (!rewritten_as_struct_method)
                    nc.callee = mangle(nc.callee, nc.type_args);
            }
            result->mirror_offset_ = lir_mirror_emit_call(
                out_, result->type, nc.callee, nc.type_args, nc.args);
            break;
        }
        case C::MethodCall: {
            lir_view::EMethodCallView v{eref};
            auto recv_ref = v.receiver();
            auto orig_recv_type = recv_ref.type(out_.type_pool.impl());
            auto new_recv = subst_child_expr(recv_ref);
            std::string method{v.method()};
            std::string resolved_symbol{v.resolved_symbol()};
            std::string resolved_type{v.resolved_type()};
            std::string tag_system{v.tag_system()};
            std::string tag_trait{v.tag_trait()};
            int32_t vtable_index = v.vtable_index();
            // Unwrap pointer/reference for TypeVar check.
            auto orig_inner = orig_recv_type;
            if (orig_inner && (TypeRef(orig_inner).kind() == LogosType::Kind::Ptr ||
                               TypeRef(orig_inner).kind() == LogosType::Kind::Ref ||
                               TypeRef(orig_inner).kind() == LogosType::Kind::MutRef) &&
                TypeRef(orig_inner).pointee())
                orig_inner = TypeRef(orig_inner).pointee();
            if (orig_inner && TypeRef(orig_inner).kind() == LogosType::Kind::TypeVar &&
                new_recv && new_recv->type) {
                std::string cname;
                auto rt = new_recv->type;
                if (TypeRef(rt).kind() == LogosType::Kind::Struct ||
                    TypeRef(rt).kind() == LogosType::Kind::ZonedStruct)
                    cname = concrete_struct_name(rt);
                else if ((TypeRef(rt).kind() == LogosType::Kind::Ptr ||
                          TypeRef(rt).kind() == LogosType::Kind::Ref ||
                          TypeRef(rt).kind() == LogosType::Kind::MutRef) && TypeRef(rt).pointee()) {
                    if (TypeRef(rt).pointee().kind() == LogosType::Kind::Struct ||
                        TypeRef(rt).pointee().kind() == LogosType::Kind::ZonedStruct)
                        cname = concrete_struct_name(TypeRef(rt).pointee());
                    else {
                        std::string ptr_cname = type_str(rt);
                        std::string ptr_fn = ptr_cname + "__" + method;
                        bool ptr_exists = templates_.count(ptr_fn) || specs_.count(ptr_fn);
                        if (!ptr_exists)
                            for (auto& f : in_.functions)
                                if (bare_fn_name(f->name) == ptr_fn) { ptr_exists = true; break; }
                        if (!ptr_exists)
                            for (auto& f : out_.functions)
                                if (bare_fn_name(f->name) == ptr_fn) { ptr_exists = true; break; }
                        cname = ptr_exists ? ptr_cname : type_str(TypeRef(rt).pointee());
                    }
                }
                if (cname.empty()) cname = type_str(rt);
                if (cname == "&[u8]") cname = "str";
                if (!cname.empty()) {
                    lir::ECall nc;
                    std::string base_fn = cname + "__" + method;
                    std::string tmpl_key = base_fn;
                    if (!templates_.count(tmpl_key) && !specs_.count(tmpl_key)) {
                        // Pkg-qualified at sema: look for any template name
                        // ending with `.<base_fn>__g__sig` or starting with it.
                        std::string p = base_fn + "__g__";
                        std::string p_dot = "." + base_fn + "__g__";
                        for (auto& [kn, _] : templates_) {
                            if (kn.rfind(p, 0) == 0 ||
                                kn.find(p_dot) != std::string::npos) {
                                tmpl_key = kn; break;
                            }
                        }
                        if (tmpl_key == base_fn)
                            for (auto& [kn, _] : specs_) {
                                if (kn.rfind(p, 0) == 0 ||
                                    kn.find(p_dot) != std::string::npos) {
                                    tmpl_key = kn; break;
                                }
                            }
                    }
                    nc.callee = tmpl_key;
                    nc.args.push_back(std::move(new_recv));
                    v.each_arg([&](lir_view::ExprRef ar) {
                        nc.args.push_back(subst_child_expr(ar));
                    });
                    for (auto ta : v.type_args(out_.type_pool.impl())) {
                        if (ta && ta.kind() == LogosType::Kind::TypeVar) {
                            auto pit = cur_packs_.find(std::string(ta.type_var_name()));
                            if (pit != cur_packs_.end()) {
                                for (auto pt : pit->second) nc.type_args.push_back(pt);
                                continue;
                            }
                        }
                        nc.type_args.push_back(subst_type(ta, s));
                    }
                    // T9-tr-02: only mangle when the impl method is itself
                    // generic. Sema's TypeVar-receiver path stashes the
                    // trait's type-args on the call so we can mangle for
                    // generic-impl methods (`impl<A> Trait<A> for T`), but
                    // for concrete impls (`impl Trait<isize> for T`) the
                    // template's type_params is empty and we must drop the
                    // sema-stashed args — otherwise we'd mangle a non-
                    // generic symbol into a non-existent suffix.
                    size_t tmpl_tparam_count = 0;
                    if (auto tit = templates_.find(tmpl_key); tit != templates_.end()) {
                        tmpl_tparam_count = tit->second->type_params.size();
                    } else if (auto sit = specs_.find(tmpl_key);
                               sit != specs_.end() && !sit->second.empty()) {
                        tmpl_tparam_count = sit->second.front()->type_params.size();
                    }
                    if (tmpl_tparam_count == 0) nc.type_args.clear();
                    else if (nc.type_args.size() > tmpl_tparam_count)
                        nc.type_args.resize(tmpl_tparam_count);
                    if (!nc.type_args.empty())
                        nc.callee = mangle(tmpl_key, nc.type_args);
                    result->mirror_offset_ = lir_mirror_emit_call(
                        out_, result->type, nc.callee, nc.type_args, nc.args);
                    break;
                }
                // Fallback: keep as method call
                std::vector<lir::LExprPtr> mc_args;
                v.each_arg([&](lir_view::ExprRef ar) {
                    mc_args.push_back(subst_child_expr(ar));
                });
                result->mirror_offset_ = lir_mirror_emit_method_call(
                    out_, result->type, new_recv, method, resolved_symbol,
                    {}, mc_args, vtable_index, resolved_type,
                    tag_system, tag_trait);
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
            if (nm.receiver && nm.receiver->type) {
                TypeRef rt = nm.receiver->type;
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
                        if (templates_.count(base_name) || specs_.count(base_name))
                            return base_name;
                        std::string p = base_name + "__";
                        for (auto& [kname, _] : templates_)
                            if (kname.rfind(p, 0) == 0) return kname;
                        for (auto& [kname, _] : specs_)
                            if (kname.rfind(p, 0) == 0) return kname;
                        return {};
                    };
                    std::string mono_base = pick_mono_template_key();
                    if (auto* spec = find_best_spec(mono_base.empty() ? base_name : mono_base,
                                                    combined_args)) {
                        std::vector<lir::LExprPtr> args;
                        args.push_back(std::move(nm.receiver));
                        v.each_arg([&](lir_view::ExprRef ar) {
                            args.push_back(subst_child_expr(ar));
                        });
                        result->mirror_offset_ = lir_mirror_emit_call(
                            out_, result->type, spec->name, {}, args);
                        rewritten = true;
                    } else if (!combined_args.empty() && !mono_base.empty()) {
                        std::string callee = mangle(mono_base, combined_args);
                        std::vector<lir::LExprPtr> args;
                        args.push_back(std::move(nm.receiver));
                        v.each_arg([&](lir_view::ExprRef ar) {
                            args.push_back(subst_child_expr(ar));
                        });
                        result->mirror_offset_ = lir_mirror_emit_call(
                            out_, result->type, callee, combined_args, args);
                        rewritten = true;
                    }
                }
            }
            if (!rewritten) {
                v.each_arg([&](lir_view::ExprRef ar) {
                    nm.args.push_back(subst_child_expr(ar));
                });
                result->mirror_offset_ = lir_mirror_emit_method_call(
                    out_, result->type, nm.receiver, nm.method, nm.resolved_symbol,
                    nm.type_args, nm.args, nm.vtable_index, resolved_type,
                    nm.tag_system, nm.tag_trait);
            }
            break;
        }
        case C::ClosureBox: {
            lir_view::EClosureBoxView v{eref};
            auto br = v.body();
            if (!br) {
                result->mirror_offset_ = lir_mirror_emit_closure_box(
                    out_, result->type, nullptr);
                break;
            }
            auto nc = lir::alloc_closure(out_);
            nc->closure_id = std::string(v.closure_id());
            v.each_param(out_.type_pool.impl(),
                [&](std::string_view nm, TypeRef pt) {
                    nc->params.push_back({std::string(nm), subst_type(pt, s)});
                });
            nc->ret_type  = subst_type(v.ret_type(out_.type_pool.impl()), s);
            nc->body      = subst_child_block(br);
            lir_mirror_emit_block_node(out_, nc->body);
            nc->is_move   = v.is_move();
            nc->as_fn_ptr = v.as_fn_ptr();
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
            result->mirror_offset_ = lir_mirror_emit_closure_box(
                out_, result->type, nc);
            break;
        }
        default:
            std::fprintf(stderr,
                "mono.subst_expr: unhandled expr Code=%d\n",
                int(eref.kind()));
            std::abort();
        }
    }

    lir_mirror_emit_expr_node(out_, *result);
    return result;
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
        p.mirror_offset_ = lir_mirror_emit_pat_variant(*prog_, en, vn, disc);
        return p;
    }
    case pc::Code::Int: {
        int64_t v = lir_view::PatIntView{pref}.value();
        lir::Pattern p;
        p.mirror_offset_ = lir_mirror_emit_pat_int(*prog_, v);
        return p;
    }
    case pc::Code::Bool: {
        bool v = lir_view::PatBoolView{pref}.value();
        lir::Pattern p;
        p.mirror_offset_ = lir_mirror_emit_pat_bool(*prog_, v);
        return p;
    }
    case pc::Code::Wild: {
        std::string name(lir_view::PatWildView{pref}.name());
        lir::Pattern p;
        p.mirror_offset_ = lir_mirror_emit_pat_wild(*prog_, name);
        return p;
    }
    case pc::Code::Range: {
        lir_view::PatRangeView v{pref};
        int64_t lo = v.lo(), hi = v.hi();
        lir::Pattern p;
        p.mirror_offset_ = lir_mirror_emit_pat_range(*prog_, lo, hi);
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
        auto off = lir_mirror_emit_pat_variant_data(
            *prog_, n.enum_name, n.variant, n.disc, n.bindings, n.binding_types);
        lir::Pattern p;
        p.mirror_offset_ = off;
        return p;
    }
    case pc::Code::Or: {
        lir::PatOr n;
        lir_view::PatOrView{pref}.each_alt(
            [&](lir_view::PatRef alt) { n.alts.push_back(walk(alt)); });
        auto off = lir_mirror_emit_pat_or(*prog_, n.alts);
        lir::Pattern p;
        p.mirror_offset_ = off;
        return p;
    }
    case pc::Code::Tuple: {
        lir_view::PatTupleView v{pref};
        lir::PatTuple n;
        v.each_binding([&](std::string_view s) { n.bindings.emplace_back(s); });
        v.each_binding_type(pool_, [&](TypeRef t) { n.binding_types.push_back(st_(t)); });
        v.each_sub([&](lir_view::PatRef sp) { n.subs.push_back(walk(sp)); });
        auto off = lir_mirror_emit_pat_tuple(
            *prog_, n.bindings, n.binding_types, n.subs);
        lir::Pattern p;
        p.mirror_offset_ = off;
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
            n.fields.push_back(std::move(pfb));
        });
        auto off = lir_mirror_emit_pat_struct(
            *prog_, n.struct_name, n.fields, n.has_rest);
        lir::Pattern p;
        p.mirror_offset_ = off;
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
        p.mirror_offset_ = off;
        return p;
    }
    case pc::Code::At: {
        lir_view::PatAtView v{pref};
        lir::PatAt n;
        n.name = std::string(v.name());
        n.type = st_(v.type(pool_));
        if (auto sub = v.sub()) n.sub.push_back(walk(sub));
        auto off = lir_mirror_emit_pat_at(*prog_, n.name, n.sub, n.type);
        lir::Pattern p;
        p.mirror_offset_ = off;
        return p;
    }
    case pc::Code::RefBind: {
        lir_view::PatRefBindView v{pref};
        lir::PatRefBind n;
        n.name      = std::string(v.name());
        n.is_mut    = v.is_mut();
        n.bind_type = st_(v.bind_type(pool_));
        auto off = lir_mirror_emit_pat_ref_bind(
            *prog_, n.name, n.is_mut, n.bind_type);
        lir::Pattern p;
        p.mirror_offset_ = off;
        return p;
    }
    case pc::Code::RefPat: {
        lir_view::PatRefPatView v{pref};
        lir::PatRefPat n;
        n.is_mut = v.is_mut();
        if (auto inner = v.inner()) n.inner.push_back(walk(inner));
        auto off = lir_mirror_emit_pat_ref_pat(*prog_, n.inner, n.is_mut);
        lir::Pattern p;
        p.mirror_offset_ = off;
        return p;
    }
    }
    return lir::Pattern{};
}

lir::LStmt Mono::subst_stmt(const lir::LStmt& st, const SubstMap& s) {
    // Returns LStmt with mirror_offset_=0 by design. Caller (instantiate_fn /
    // clone_struct_def) bulk-emits via lir_mirror_emit_function only after the
    // owning LFunction reaches a heap-stable address (unique_ptr) and its
    // body's stmt-vector buffers stop reallocating. See the call sites in
    // the worklist drain and per-method emit loop for the full rationale.
    lir::LStmt ns;
    ns.line = st.line;

    auto sref = stmt_ref_of(st);
    if (!sref) return ns;  // mirror miss — defensive

    auto subst_child_expr = [&](lir_view::ExprRef er) -> lir::LExprPtr {
        auto* le = lexpr_of(er);
        return le ? subst_expr(*le, s) : nullptr;
    };
    auto subst_child_block = [&](lir_view::BlockRef br) -> lir::LBlock {
        auto* lb = lblock_of(br);
        return lb ? subst_block(*lb, s) : lir::LBlock{};
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
        ns.mirror_offset_ = lir_mirror_emit_let(
            out_, ns.line, name, ty, value, is_mut);
        break;
    }
    case SCode::Assign: {
        lir_view::SAssignView v{sref};
        std::string name(v.name());
        auto value = subst_child_expr(v.value());
        ns.mirror_offset_ = lir_mirror_emit_assign(
            out_, ns.line, name, value);
        break;
    }
    case SCode::Return: {
        auto val = lir_view::SReturnView{sref}.value();
        auto value = val ? subst_child_expr(val) : nullptr;
        ns.mirror_offset_ = lir_mirror_emit_return(
            out_, ns.line, value);
        break;
    }
    case SCode::If: {
        lir_view::SIfView v{sref};
        auto cond = subst_child_expr(v.cond());
        auto* then_blk = lir::alloc_block(out_, subst_child_block(v.then_block()));
        lir_mirror_emit_block_node(out_, *then_blk);
        lir::LBlock* else_blk = nullptr;
        if (auto eb = v.else_block()) {
            else_blk = lir::alloc_block(out_, subst_child_block(eb));
            lir_mirror_emit_block_node(out_, *else_blk);
        }
        ns.mirror_offset_ = lir_mirror_emit_if_stmt(
            out_, ns.line, cond, then_blk, else_blk);
        break;
    }
    case SCode::While: {
        lir_view::SWhileView v{sref};
        auto cond = subst_child_expr(v.cond());
        auto* body = lir::alloc_block(out_, subst_child_block(v.body()));
        lir_mirror_emit_block_node(out_, *body);
        std::string label(v.label());
        ns.mirror_offset_ = lir_mirror_emit_while(
            out_, ns.line, cond, body, label);
        break;
    }
    case SCode::For: {
        lir_view::SForView v{sref};
        std::string var(v.var());
        auto lo = subst_child_expr(v.lo());
        auto hi = subst_child_expr(v.hi());
        bool inclusive = v.inclusive();
        auto* body = lir::alloc_block(out_, subst_child_block(v.body()));
        lir_mirror_emit_block_node(out_, *body);
        std::string label(v.label());
        ns.mirror_offset_ = lir_mirror_emit_for(
            out_, ns.line, var, lo, hi, inclusive, body, label);
        break;
    }
    case SCode::Loop: {
        lir_view::SLoopView v{sref};
        auto* body = lir::alloc_block(out_, subst_child_block(v.body()));
        lir_mirror_emit_block_node(out_, *body);
        TypeRef result_type = v.result_type(pool);
        std::string break_slot(v.break_slot());
        std::string label(v.label());
        ns.mirror_offset_ = lir_mirror_emit_loop(
            out_, ns.line, body, label, break_slot, result_type);
        break;
    }
    case SCode::Block: {
        lir_view::SBlockView v{sref};
        auto* blk = lir::alloc_block(out_, subst_child_block(v.body()));
        lir_mirror_emit_block_node(out_, *blk);
        ns.mirror_offset_ = lir_mirror_emit_block_stmt(
            out_, ns.line, blk);
        break;
    }
    case SCode::Break: {
        lir_view::SBreakView v{sref};
        lir::LExprPtr value = nullptr;
        if (auto val = v.value()) value = subst_child_expr(val);
        std::string label(v.label());
        ns.mirror_offset_ = lir_mirror_emit_break(
            out_, ns.line, value, label);
        break;
    }
    case SCode::Continue: {
        std::string label(lir_view::SContinueView{sref}.label());
        ns.mirror_offset_ = lir_mirror_emit_continue(
            out_, ns.line, label);
        break;
    }
    case SCode::FieldWrite: {
        lir_view::SFieldWriteView v{sref};
        std::string receiver(v.receiver());
        std::string field(v.field());
        auto value = subst_child_expr(v.value());
        ns.mirror_offset_ = lir_mirror_emit_field_write(
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
        ns.mirror_offset_ = lir_mirror_emit_chain_field_write(
            out_, ns.line, receiver, mid_field, extras, field, value);
        break;
    }
    case SCode::DerefFieldWrite: {
        lir_view::SDerefFieldWriteView v{sref};
        std::string receiver(v.receiver());
        std::string type_name(v.type_name());
        std::string field(v.field());
        auto value = subst_child_expr(v.value());
        ns.mirror_offset_ = lir_mirror_emit_deref_field_write(
            out_, ns.line, receiver, type_name, field, value);
        break;
    }
    case SCode::IndexWrite: {
        lir_view::SIndexWriteView v{sref};
        std::string arr(v.arr());
        auto idx = subst_child_expr(v.index());
        auto value = subst_child_expr(v.value());
        ns.mirror_offset_ = lir_mirror_emit_index_write(
            out_, ns.line, arr, idx, value);
        break;
    }
    case SCode::FieldIndexWrite: {
        lir_view::SFieldIndexWriteView v{sref};
        std::string receiver(v.receiver());
        std::string field(v.field());
        auto idx = subst_child_expr(v.index());
        auto value = subst_child_expr(v.value());
        ns.mirror_offset_ = lir_mirror_emit_field_index_write(
            out_, ns.line, receiver, field, idx, value);
        break;
    }
    case SCode::DerefWrite: {
        lir_view::SDerefWriteView v{sref};
        auto ptr = subst_child_expr(v.ptr());
        auto value = subst_child_expr(v.value());
        ns.mirror_offset_ = lir_mirror_emit_deref_write(
            out_, ns.line, ptr, value);
        break;
    }
    case SCode::TupleWrite: {
        lir_view::STupleWriteView v{sref};
        std::string receiver(v.receiver());
        uint32_t index = v.index();
        auto value = subst_child_expr(v.value());
        TypeRef recv_type = v.recv_type(pool);
        ns.mirror_offset_ = lir_mirror_emit_tuple_write(
            out_, ns.line, receiver, index, value, recv_type);
        break;
    }
    case SCode::ExprStmt: {
        auto expr = subst_child_expr(lir_view::SExprStmtView{sref}.expr());
        ns.mirror_offset_ = lir_mirror_emit_expr_stmt(
            out_, ns.line, expr);
        break;
    }
    case SCode::Delete: {
        auto expr = subst_child_expr(lir_view::SDeleteView{sref}.expr());
        ns.mirror_offset_ = lir_mirror_emit_delete(
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
                for (auto& sd : out_.structs) {
                    bool match = (!cname.empty() && sd.name == cname) ||
                                 sd.name == TypeRef(ty).struct_name();
                    if (!match) continue;
                    for (auto& f : sd.fields) {
                        if (f.type && (TypeRef(f.type).kind() == LogosType::Kind::Struct ||
                                       TypeRef(f.type).kind() == LogosType::Kind::ZonedStruct)) {
                            drop_fields = true;
                            break;
                        }
                    }
                    break;
                }
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
        ns.mirror_offset_ = lir_mirror_emit_drop(
            out_, ns.line, var_name, drop_fn, ty, drop_fields, moved_fields);
        break;
    }
    case SCode::Match: {
        lir_view::SMatchView v{sref};
        auto scrut = subst_child_expr(v.scrut());
        std::vector<lir::LMatchArm> arms;
        v.each_arm([&](lir_view::EMatchArmRef arm) {
            lir::LMatchArm na;
            if (auto pref = arm.pat()) na.pat = subst_pattern(pref, s);
            na.body = lir::alloc_block(out_, subst_child_block(arm.body()));
            lir_mirror_emit_block_node(out_, *na.body);
            if (auto g = arm.guard()) na.guard = subst_child_expr(g);
            arms.push_back(std::move(na));
        });
        ns.mirror_offset_ = lir_mirror_emit_match_stmt(
            out_, ns.line, scrut, arms);
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
        if (arr_size == 0 && !is_slice && iter && iter->type &&
            iter->type.kind() == LogosType::Kind::Array)
            arr_size = (int64_t)iter->type.arr_size();
        auto* body = lir::alloc_block(out_, subst_child_block(v.body()));
        lir_mirror_emit_block_node(out_, *body);
        ns.mirror_offset_ = lir_mirror_emit_for_each(
            out_, ns.line, var, iter, elem_type, arr_size, is_slice, body);
        break;
    }
    case SCode::LetElse: {
        lir_view::SLetElseView v{sref};
        lir::Pattern pat;
        if (auto pref = v.pat()) pat = subst_pattern(pref, s);
        auto scrut = subst_child_expr(v.scrut());
        auto* else_block = lir::alloc_block(out_, subst_child_block(v.else_block()));
        lir_mirror_emit_block_node(out_, *else_block);
        ns.mirror_offset_ = lir_mirror_emit_let_else(
            out_, ns.line, pat, scrut, else_block);
        break;
    }
    default: break;
    }

    // ns is local; address registration happens later via
    // lir_mirror_emit_function once the LStmt sits at a stable heap address.
    // Children were registered by their own _node emit calls.
    return ns;
}


// ── Clone a function with substitution (empty SubstMap = verbatim copy) ─

lir::LFunction Mono::clone_fn(const lir::LFunction& fn, const SubstMap& s,
                         const PackMap& packs) {
    cur_packs_ = packs;  // make available to subst_expr
    lir::LFunction nf;
    nf.name               = fn.name;
    nf.method_base        = fn.method_base;
    nf.package            = fn.package;
    nf.is_extern          = fn.is_extern;
    nf.is_vararg          = fn.is_vararg;
    // Never propagate from_binary_module to cloned functions: clone_fn is
    // called by mono to create instantiations, which are new functions not
    // present in the binary archive. The archive contains only the pre-compiled
    // non-generic originals (identified via LProgram::binary_symbols in mlir_gen).
    nf.ret_type  = subst_type(fn.ret_type, s);
    // B65: lifetime params + outlives bounds are preserved verbatim through
    // mono. Lifetime substitution is identity (lifetimes are not in the
    // SubstMap), so the original pairs remain valid on the cloned signature.
    nf.lifetime_params   = fn.lifetime_params;
    nf.lifetime_outlives = fn.lifetime_outlives;
    for (auto& p : fn.params) {
        if (p.is_variadic) {
            // Expand variadic param into N concrete params.
            // Find the pack type for this param's TypeVar name.
            std::string pack_name;
            TypeRef pt(p.type);
            if (pt && pt.kind() == LogosType::Kind::TypeVar)
                pack_name = std::string(pt.type_var_name());
            auto pit = packs.find(pack_name);
            if (pit != packs.end()) {
                for (size_t i = 0; i < pit->second.size(); ++i) {
                    auto expanded_name = make_pack_arg_name(p.name, i);
                    nf.params.push_back({expanded_name, pit->second[i]});
                }
            }
        } else {
            nf.params.push_back({p.name, subst_type(p.type, s)});
        }
    }
    nf.body = subst_block(fn.body, s, packs);
    // type_params left empty: instantiated functions are monomorphic
    return nf;
}


// ── Struct monomorphization ───────────────────────────────────

// Sprint 5.4: populate the trait_engine from current mono tables.
// Cheap; called on demand. Invalidated by trait_engine_dirty_ when
// new impls or blankets land mid-pass.
void Mono::populate_trait_engine_() {
    trait_engine_ = trait_engine::TraitEngine{};   // fresh
    // (D) direct impls — concrete_impls_ keys are "trait::type".
    for (auto& k : concrete_impls_) {
        auto pos = k.find("::");
        if (pos == std::string::npos) continue;
        trait_engine_.add_impl(k.substr(0, pos), k.substr(pos + 2));
    }
    // (B) blanket impls — preserve "all bounds in one AND" semantics:
    // primary bound first, then extras. Empty primary bound +
    // empty extras is the "unconditional impl-for-all" case, which
    // mono_has_impl_recursive returned true for. Represent as a
    // blanket with empty bounds list (engine returns true).
    for (auto& bi : blanket_impls_) {
        std::vector<std::string> bounds;
        if (!bi.bound_trait.empty()) bounds.push_back(bi.bound_trait);
        for (auto& eb : bi.extra_bounds) bounds.push_back(eb);
        trait_engine_.add_blanket(bi.trait_name, std::move(bounds));
    }
    // (S) shape-auto: closure types satisfy Fn / FnMut / FnOnce.
    // Sprint 5.5 keystone — the engine can answer
    // `satisfies("Fn", "|i32| -> i32")` etc. without requiring an
    // explicit `impl Fn for <every closure>` in stdlib. Canonical
    // closure type name is "|T1, ...| -> R" (see type_str in
    // sema.cpp). Sema's bound-resolver picks this up in Sprint 5.7.
    auto is_closure_typename = [](std::string_view n) {
        return !n.empty() && n.front() == '|';
    };
    trait_engine_.add_shape_auto_impl("Fn",     "closure", is_closure_typename);
    trait_engine_.add_shape_auto_impl("FnMut",  "closure", is_closure_typename);
    trait_engine_.add_shape_auto_impl("FnOnce", "closure", is_closure_typename);
    trait_engine_dirty_ = false;
}

// L1.4: bound-gate, factored from clone_struct_def's method loop. Returns
// false when any of method `m`'s impl_type_params bounds is unsatisfied
// under substitution `s`.
//
// Sprint 5.6: implementation now routes through trait_engine_. The
// `seen` parameter is kept for source compatibility but ignored —
// the engine has its own per-query cycle guard. Existing call sites
// pass a StrSet by reference; we don't break them.
bool Mono::mono_has_impl_recursive(const std::string& trait_name,
                                   const std::string& concrete_name,
                                   StrSet& /*seen*/) {
    if (trait_engine_dirty_) populate_trait_engine_();
    return trait_engine_.satisfies(trait_name, concrete_name);
}

bool Mono::method_bound_ok(const lir::LFunction& m, const SubstMap& s) {
    for (auto& itp : m.impl_type_params) {
        if (itp.bounds.empty()) continue;
        auto sit = s.find(itp.name);
        if (sit == s.end()) continue;
        TypeRef concrete = sit->second;
        if (!concrete) continue;
        std::string cname;
        if (TypeRef(concrete).kind() == LogosType::Kind::Struct ||
            TypeRef(concrete).kind() == LogosType::Kind::ZonedStruct)
            cname = concrete_struct_name(concrete);
        else if (TypeRef(concrete).kind() == LogosType::Kind::Enum)
            cname = TypeRef(concrete).enum_name();
        else
            cname = type_str(concrete);
        if (auto p = cname.find("$G"); p != std::string::npos)
            cname = cname.substr(0, p);
        // Cycle-guard + per-attempt seen semantics live in
        // mono_has_impl_recursive (factored for reuse at mono_subst.cpp's
        // assoc-type fallback).
        auto has_impl = [&](const std::string& trait, const std::string& cn) {
            StrSet seen;
            return mono_has_impl_recursive(trait, cn, seen);
        };
        for (auto& tb : itp.bounds) {
            bool is_auto = false;
            for (auto& td : out_.traits)
                if (td.name == tb.trait_name) { is_auto = td.is_auto; break; }
            if (is_auto) {
                StrSet visited;
                if (!is_auto_satisfied(concrete, tb.trait_name, visited))
                    return false;
                continue;
            }
            if (!has_impl(tb.trait_name, cname)) return false;
            // B62/B63: HRTB satisfaction — universal-position + bijectivity
            // checks. Bound binders (any non-empty, non-'static lifetime in
            // type_args) must align with impl-level lifetime params, and the
            // skolem↔impl-region mapping must be 1-1. See sema_collect.cpp's
            // region_ok for the full rule.
            if (!tb.type_args.empty()) {
                const lir::LImplBlock* ib = nullptr;
                for (auto& cand : out_.impls) {
                    if (cand.trait_name == tb.trait_name &&
                        cand.target_type == cname) { ib = &cand; break; }
                }
                if (ib && !ib->trait_type_args.empty()) {
                    std::unordered_map<std::string, std::string> i2s;
                    auto univ = [&](const std::string& lt) {
                        for (auto& nm : ib->impl_lifetime_params)
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
                                        ib->trait_type_args.size());
                    for (size_t i = 0; i < n; ++i)
                        if (!walk(TypeRef(tb.type_args[i]),
                                  TypeRef(ib->trait_type_args[i]))) return false;
                }
            }
        }
    }
    return true;
}

// Clone a struct def with substitution; rename to new_name.
// Method names are rewritten from "Base__method" to "new_name__method".
lir::LStructDef Mono::clone_struct_def(const lir::LStructDef& tmpl,
                                  const SubstMap& s,
                                  const PackMap& packs,
                                  const std::string& new_name) {
    lir::LStructDef nd;
    nd.name = new_name;
    nd.pkg  = tmpl.pkg;
    nd.is_zoned = tmpl.is_zoned;
    // type_params cleared: result is monomorphic
    for (auto& f : tmpl.fields) {
        if (f.is_variadic) {
            std::string pack_name;
            TypeRef ft(f.type);
            if (ft && ft.kind() == LogosType::Kind::TypeVar)
                pack_name = std::string(ft.type_var_name());
            auto pit = packs.find(pack_name);
            if (pit != packs.end()) {
                for (size_t i = 0; i < pit->second.size(); ++i) {
                    nd.fields.push_back({f.name + "_" + std::to_string(i), pit->second[i]});
                }
            }
        } else {
            nd.fields.push_back({f.name, subst_type(f.type, s)});
        }
    }
    // L1.4: in lazy mode, skip eager method cloning. drain_method_worklist
    // will clone methods on demand (from L1.1 call-site hook, L1.2 dispatch
    // pin, L1.3 is_root_pin). The bound gate runs there too.
    if (lazy_methods_ && !tmpl.type_params.empty()) return nd;
    for (auto& m_up : tmpl.methods) {
        auto& m = *m_up;
        if (!method_bound_ok(m, s)) continue;
        auto nm = clone_fn(m, s, packs);
        // Rename method: "[pkg.]OldBase__methodName[__g__T]" →
        //                "[pkg.]new_name__methodName[__g__T]".
        // Preserve pkg prefix and any sig suffix; replace base name only.
        std::string mn = m.name;
        std::string mn_pkg;
        // Pkg may have inner dots; split at LAST dot.
        if (auto dot = mn.rfind('.'); dot != std::string::npos) {
            mn_pkg = mn.substr(0, dot);
            mn = mn.substr(dot + 1);
        }
        auto sep = mn.find("__");
        if (sep != std::string::npos) {
            std::string rest = mn.substr(sep);  // "__methodName[__g__T]"
            std::string new_bare = new_name + rest;
            nm.name = mn_pkg.empty() ? new_bare : mn_pkg + "." + new_bare;
        }
        // Specialization: if the user wrote `impl Foo<Concrete> { fn m ... }`
        // separately from `impl<T> Foo<T> { fn m ... }`, the concrete method
        // lives in in_.functions under the mangled name.  Skip cloning the
        // blanket version for this concrete — the free-fn path will emit it
        // with the correct body.
        bool overridden = false;
        for (auto& fn : in_.functions) {
            if (!fn->type_params.empty()) continue;
            if (bare_fn_name(fn->name) == nm.name) { overridden = true; break; }
        }
        if (overridden) continue;
        // Substitute struct type in params/ret as needed (already done by clone_fn).
        nd.methods.push_back(std::make_unique<lir::LFunction>(std::move(nm)));
        lir_mirror_emit_function(out_, *out_.mirror_table, *nd.methods.back());
    }
    return nd;
}


// Return the best-matching struct specialisation for (base_name, type_args).
const lir::LStructDef* Mono::find_best_struct_spec(
    const std::string& base_name,
    const std::vector<TypeRef>& type_args) {
    auto sit = struct_specs_.find(base_name);
    if (sit == struct_specs_.end()) return nullptr;

    const lir::LStructDef* best       = nullptr;
    std::vector<int>       best_vec;
    bool                   ambiguous  = false;

    for (auto* spec : sit->second) {
        if (spec->spec_patterns.size() != type_args.size()) continue;
        SubstMap dummy;
        bool ok = true;
        for (size_t i = 0; i < type_args.size(); ++i) {
            if (!match_type(type_args[i], spec->spec_patterns[i], dummy)) {
                ok = false; break;
            }
        }
        if (!ok) continue;
        auto svec = specificity_vec(spec->spec_patterns);
        if (!best || svec > best_vec) {
            best_vec  = svec;
            best      = spec;
            ambiguous = false;
        } else if (svec == best_vec) {
            ambiguous = true;
        }
    }
    if (ambiguous) {
        in_.diags.diags.push_back({Diag::Level::Error, "mono",
            std::format("ambiguous specializations for struct '{}'", base_name),
            "", 0});
    }
    return best;
}


// Walk all output functions collecting generic struct instantiations needed.
void Mono::collect_struct_needs_from_output() {
    auto& arena = out_.type_pool.arena_or_init();
    for (auto& fn : out_.functions) {
        collect_type_for_structs(fn->ret_type);
        for (auto& p : fn->params) collect_type_for_structs(p.type);
        if (fn->body.mirror_offset_ != hermes::arena_offset_t{})
            collect_struct_needs_from_block(
                lir_view::BlockRef(&arena, fn->body.mirror_offset_));
    }
    // Also walk already-instantiated structs (field types may reference more).
    for (auto& sd : out_.structs)
        for (auto& f : sd.fields) collect_type_for_structs(f.type);
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
    case SCode::For:
        collect_struct_needs_from_block(lir_view::SForView{s}.body());
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
    case SCode::Delete:
        collect_struct_needs_from_expr(lir_view::SDeleteView{s}.expr());
        break;
    case SCode::Drop:
        break;
    case SCode::Match: {
        lir_view::SMatchView v{s};
        collect_struct_needs_from_expr(v.scrut());
        v.each_arm([&](lir_view::EMatchArmRef arm) {
            if (auto g = arm.guard()) collect_struct_needs_from_expr(g);
            collect_struct_needs_from_block(arm.body());
        });
        break;
    }
    case SCode::LetElse: {
        lir_view::SLetElseView v{s};
        collect_struct_needs_from_expr(v.scrut());
        collect_struct_needs_from_block(v.else_block());
        break;
    }
    default: break;
    }
}


void Mono::collect_struct_needs_from_expr(lir_view::ExprRef e) {
    if (!e) return;
    const TypePoolImpl* pool = out_.type_pool.impl();
    collect_type_for_structs(e.type(pool));
    using ECode = lir_schema::expr::Code;
    switch (e.kind()) {
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
    case ECode::New:
        lir_view::ENewView{e}.each_field_value(
            [&](lir_view::ExprRef fv) { collect_struct_needs_from_expr(fv); });
        break;
    case ECode::FormatCall: {
        lir_view::EFormatCallView v{e};
        collect_struct_needs_from_expr(v.fmt());
        v.each_arg([&](lir_view::ExprRef a) { collect_struct_needs_from_expr(a); });
        break;
    }
    case ECode::PackExpand:
        break;
    case ECode::MatchExpr: {
        lir_view::EMatchExprView v{e};
        collect_struct_needs_from_expr(v.scrut());
        v.each_arm([&](lir_view::EMatchArmRef arm) {
            if (auto g = arm.guard()) collect_struct_needs_from_expr(g);
            collect_struct_needs_from_expr(arm.value());
        });
        break;
    }
    case ECode::BlockExpr: {
        lir_view::EBlockExprView v{e};
        if (auto blk = v.block()) collect_struct_needs_from_block(blk);
        if (auto r = v.result()) collect_struct_needs_from_expr(r);
        break;
    }
    default: break;
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

            const lir::LStructDef* tmpl = nullptr;
            PackMap packs;
            // Prefer pkg-qualified lookup so cross-pkg same-named structs
            // route to their own template; fall back to bare for legacy
            // call sites that don't carry pkg.
            std::string struct_pkg{TypeRef(struct_t).pkg_name()};
            if (auto* spec = find_best_struct_spec(base, TypeRef(struct_t).type_args())) {
                for (size_t i = 0; i < spec->spec_patterns.size() &&
                                   i < TypeRef(struct_t).type_args().size(); ++i)
                    match_type(TypeRef(struct_t).type_args()[i], spec->spec_patterns[i], subst);
                tmpl = spec;
            } else {
                auto it = struct_pkg.empty() ? struct_templates_.end()
                                             : struct_templates_.find(struct_pkg + "." + base);
                if (it == struct_templates_.end())
                    it = struct_templates_.find(base);
                if (it == struct_templates_.end()) continue;
                tmpl = it->second;
                for (size_t i = 0, j = 0; i < tmpl->type_params.size(); ++i) {
                    if (tmpl->type_params[i].is_variadic) {
                        std::vector<TypeRef> pack;
                        while (j < TypeRef(struct_t).type_args().size()) pack.push_back(TypeRef(struct_t).type_args()[j++]);
                        packs[tmpl->type_params[i].name] = std::move(pack);
                    } else if (j < TypeRef(struct_t).type_args().size()) {
                        subst[tmpl->type_params[i].name] = TypeRef(struct_t).type_args()[j++];
                    }
                }
            }

            // Pkg-qualified primary key + bare back-compat key (last-wins).
            // Sites that have a TypeRef use qualified_cname for routing;
            // sites that only have sd.name fall through bare lookup.
            concrete_struct_types_[qcname] = struct_t;
            concrete_struct_types_[cname]  = struct_t;
            auto inst = clone_struct_def(*tmpl, subst, packs, cname);
            // The generic's home pkg owns the conceptual identity. A spec in a
            // different pkg only contributes layout; the cloned inst should
            // carry the generic's pkg so user-side TypeRefs (resolved against
            // the generic decl) and mlir-side struct names agree.
            // Inst pkg comes from the generic template (not the spec, which
            // may live in a different pkg and only contribute layout).
            if (!struct_pkg.empty())
                if (auto git = struct_templates_.find(struct_pkg + "." + base);
                    git != struct_templates_.end()) {
                    inst.pkg = git->second->pkg;
                }
            if (inst.pkg.empty())
                if (auto git = struct_templates_.find(base);
                    git != struct_templates_.end())
                    inst.pkg = git->second->pkg;
            // Mirror emit happens here — *after* clone, *before* scan_fn — because
            // only at this point are stmt/expr addresses stable. Two conditions:
            //   (a) all recursive subst_block push_backs are done, so the
            //       LBlock::stmts vector buffers won't reallocate again and
            //       LStmt addresses inside them are fixed;
            //   (b) each LFunction sits behind a unique_ptr in inst.methods,
            //       so the LFunction itself (and therefore its body LBlock) is
            //       heap-stable even when `inst` is later moved into out_.structs.
            // subst_stmt deliberately returns LStmt with mirror_offset_=0 — emitting
            // mid-clone would point the mirror at a transient vector slot.
            for (auto& m : inst.methods)
                lir_mirror_emit_function(out_, *out_.mirror_table, *m);
            for (auto& m : inst.methods) scan_fn(*m);
            // Apply explicit instantiation annotation if present (sets type_code
            // on a specific generic instantiation, e.g. `#[type_code=100] eidos Array<AnyVal>;`).
            for (auto& ia : out_.inst_annotations) {
                if (ia.mangled_name == cname && ia.type_code != 0) {
                    inst.type_code = ia.type_code;
                    break;
                }
            }
            // Collect field types of new struct for further instantiation.
            for (auto& f : inst.fields) collect_type_for_structs(f.type);
            out_.structs.push_back(std::move(inst));
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
            auto fn_inst = instantiate_fn(*item.tmpl, item.mangled, item.subst, item.packs);
            // Same two-step stabilization as the struct-method case above:
            // (a) instantiate_fn's recursive subst_block push_backs have ended
            //     so stmt-vector buffers are fixed; (b) make_unique parks the
            //     LFunction at a heap-stable address before emit walks it.
            out_.functions.push_back(std::make_unique<lir::LFunction>(std::move(fn_inst)));
            auto& fn_ref = *out_.functions.back();
            lir_mirror_emit_function(out_, *out_.mirror_table, fn_ref);
            scan_fn(fn_ref);
            ++stats_.fn_instances;
            note_fn_worklist_size(worklist_.size());
        }
        depth_ = 0;
    }
}


// ── Class monomorphization ────────────────────────────────────

// Clone a class def with substitution; rename to new_name.
// Mirrors clone_struct_def but preserves vtable_order, parent_name, etc.

lir::LEnumDef Mono::clone_enum_def(const lir::LEnumDef& tmpl,
                              const SubstMap& s,
                              const PackMap& packs,
                              const std::string& new_name) {
    lir::LEnumDef nd;
    nd.name = new_name;
    nd.pkg  = tmpl.pkg;
    for (auto& v : tmpl.variants) {
        lir::LVariant nv;
        nv.name = v.name;
        nv.disc = v.disc;
        // Variadic expansion for variants like Multi(...T)
        if (v.is_variadic && !v.payload_types.empty()) {
            auto pt = v.payload_types[0];
            if (TypeRef(pt).kind() == LogosType::Kind::TypeVar) {
                auto pit = packs.find(TypeRef(pt).type_var_name());
                if (pit != packs.end()) {
                    for (auto pt_in_pack : pit->second)
                        nv.payload_types.push_back(subst_type(pt_in_pack, s));
                } else {
                    nv.payload_types.push_back(subst_type(pt, s));
                }
            } else {
                nv.payload_types.push_back(subst_type(pt, s));
            }
        } else {
            for (auto pt : v.payload_types)
                nv.payload_types.push_back(subst_type(pt, s));
        }
        nd.variants.push_back(std::move(nv));
    }
    return nd;
}


void Mono::instantiate_enum_templates() {
    // Instantiate generic enums that were recorded during function cloning.
    // Simple approach: iterate until no more needed (fixed-point).
    while (true) {
        std::vector<std::pair<std::string, std::pair<std::vector<TypeRef>, int>>> todo;
        for (auto& [cname, info] : needed_enum_insts_) {
            if (enum_done_.count(cname)) continue;
            todo.push_back({cname, info});
        }
        if (todo.empty()) break;
        for (auto& [cname, info] : todo) {
            enum_done_.insert(cname);
            const auto& args = info.first;
            depth_ = info.second;
            // Find the template
            // Extract base name from cname (before first __)
            std::string base = cname;
            auto pos = base.find("__");
            if (pos != std::string::npos) base = base.substr(0, pos);
            auto tit = enum_templates_.find(base);
            if (tit == enum_templates_.end()) continue;
            auto* tmpl = tit->second;
            // Build substitution map and packs
            SubstMap subst;
            PackMap packs;
            for (size_t i = 0, j = 0; i < tmpl->type_params.size(); ++i) {
                if (tmpl->type_params[i].is_variadic) {
                    std::vector<TypeRef> pack;
                    while (j < args.size()) pack.push_back(args[j++]);
                    packs[tmpl->type_params[i].name] = std::move(pack);
                } else if (j < args.size()) {
                    subst[tmpl->type_params[i].name] = args[j++];
                }
            }
            // Instantiate: substitute payload types and methods
            auto inst = clone_enum_def(*tmpl, subst, packs, cname);

            // Instantiate any impl<T> methods stored as generic functions in prog.functions.
            // Convention: function name starts with "Base__" and has matching type params.
            std::string prefix = base + "__";
            for (auto& fn_up : in_.functions) {
                auto& fn = *fn_up;
                if (fn.type_params.empty()) continue;
                // Strip pkg prefix (`pkg.`) before matching the bare base name.
                std::string_view bare = fn.name;
                std::string fn_pkg;
                if (auto dot = bare.rfind('.'); dot != std::string_view::npos) {
                    fn_pkg = std::string(bare.substr(0, dot));
                    bare = bare.substr(dot + 1);
                }
                if (bare.substr(0, prefix.size()) != prefix) continue;
                // Match type params to subst keys
                bool matches = fn.type_params.size() == tmpl->type_params.size();
                if (!matches) continue;
                std::string bare_inst = cname + std::string(bare.substr(base.size()));
                std::string inst_name = fn_pkg.empty() ? bare_inst
                                                       : fn_pkg + "." + bare_inst;
                if (done_.count(inst_name)) continue;
                SubstMap fn_subst = subst;
                PackMap fn_packs = packs;
                // Override type params with the enum's type param names if different
                for (size_t i = 0, j = 0; i < fn.type_params.size(); ++i) {
                    if (fn.type_params[i].is_variadic) {
                         std::vector<TypeRef> pack;
                         while (j < args.size()) pack.push_back(args[j++]);
                         fn_packs[fn.type_params[i].name] = std::move(pack);
                    } else if (j < args.size()) {
                        fn_subst[fn.type_params[i].name] = args[j++];
                    }
                }
                auto nm = clone_fn(fn, fn_subst, fn_packs);
                nm.name = inst_name;
                done_.insert(inst_name);
                out_.functions.push_back(std::make_unique<lir::LFunction>(std::move(nm)));
                lir_mirror_emit_function(out_, *out_.mirror_table, *out_.functions.back());
            }
            out_.enums.push_back(std::move(inst));
            ++stats_.enum_instances;
        }
    }
}



} // namespace logos::compiler
