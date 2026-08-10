// Logos project — https://github.com/victor-smirnov/logos
//
// mlir_gen_expr.cpp — Expression code generation.

#include "mlir_gen_impl.hpp"
#include "llvm_compat.hpp"

#include <logos/compiler/sha256.hpp>
#include <logos/writ/compat.hpp>
#include <logos/writ/compat.hpp>
#include <logos/writ/compat.hpp>
#include <logos/writ/compat.hpp>
#include <logos/writ/compat.hpp>
#include <logos/writ/compat.hpp>
#include <logos/writ/compat.hpp>
#include <logos/writ/compat.hpp>
#include <logos/writ/compat.hpp>
#include <logos/writ/compat.hpp>
#include <logos/writ/compat.hpp>

#include <cstring>

namespace logos::compiler {

using namespace lir;

namespace {
// Iterate top-level func.func ops directly (mod.getOps<>) instead of
// recursively walking ALL nested ops — orders of magnitude faster for
// modules with thousands of functions.
template<class Pred>
static mlir::func::FuncOp find_fn_matching(mlir::ModuleOp mod, Pred&& pred) {
    for (auto fn : mod.getOps<mlir::func::FuncOp>())
        if (pred(fn)) return fn;
    return {};
}

// Canonicalise a callee/def symbol to its bare comparison key: strip every
// "$M<alnum>" coexistence run, the free-fn `pkg$` prefix, the method `pkg.`
// prefix, and the `__f__`/`__g__` signature suffix. find_func_op's fuzzy
// fallback matches a callee to a def when their canonical keys are equal.
// Lifted out of find_func_op so the per-call O(n) walk can be replaced by a
// canonical→FuncOp index that canonicalises each def name exactly once.
static std::string ffo_canonical(std::string_view in) {
    std::string s;
    s.reserve(in.size());
    for (size_t i = 0; i < in.size();) {
        if (i + 1 < in.size() && in[i] == '$' && in[i + 1] == 'M') {
            i += 2;
            while (i < in.size() && std::isalnum((unsigned char)in[i])) ++i;
        } else {
            s.push_back(in[i++]);
        }
    }
    std::string_view nm = s;
    if (auto d = nm.find('$'); d != std::string_view::npos) {
        bool gen = (d + 2 < nm.size() && nm[d + 1] == 'G' &&
                    nm[d + 2] >= '0' && nm[d + 2] <= '9');
        if (!gen) nm = nm.substr(d + 1);
    }
    if (auto d = nm.rfind('.'); d != std::string_view::npos)
        nm = nm.substr(d + 1);
    if (auto p = nm.find("__f__"); p != std::string_view::npos)
        nm = nm.substr(0, p);
    else if (auto p = nm.find("__g__"); p != std::string_view::npos)
        nm = nm.substr(0, p);
    return std::string(nm);
}

}  // namespace

// (Re)build the canonical→FuncOp index if the module's FuncOp set changed.
// Canonicalises every def name once (vs find_func_op's former per-call walk).
void MLIRGenImpl::ensure_ffo_canon_index(mlir::ModuleOp mod) const {
    if (!ffo_canon_dirty_) return;   // O(1) — no per-call FuncOp recount
    ffo_canon_index_.clear();
    ffo_canon_ambig_.clear();
    ffo_symtab_.clear();
    ffo_base_first_.clear();
    for (auto fn : mod.getOps<mlir::func::FuncOp>()) {
        auto nm = fn.getName();
        // Direct name → FuncOp (a symbol name is unique, so last-wins == only).
        ffo_symtab_[std::string(nm)] = fn;
        // base = name up to the "__f__"/"__g__" method marker → first FuncOp.
        {
            llvm::StringRef nr = nm;
            size_t mk = nr.find("__f__");
            if (mk == llvm::StringRef::npos) mk = nr.find("__g__");
            if (mk != llvm::StringRef::npos)
                ffo_base_first_.emplace(std::string(nr.substr(0, mk)), fn);  // first-wins
        }
        // Canonical (sig-stripped / pkg-stripped) → FuncOp, ambiguous-aware.
        std::string c = ffo_canonical(nm);
        if (ffo_canon_ambig_.count(c)) continue;
        auto [it, ins] = ffo_canon_index_.emplace(c, fn);
        if (!ins && it->second != fn) {
            ffo_canon_ambig_.insert(c);   // overload collision → unresolved
            ffo_canon_index_.erase(it);
        }
    }
    ffo_canon_dirty_ = false;
}

// find_func_op — THE callee-resolution chokepoint. Resolve a callee symbol to
// its FuncOp, tolerating the bare↔module-qualified and sig-stripped forms the
// LIR/mono/bridge produce. A member (not a file-static) so it can consult the
// per-package module map via link_name_str.
mlir::func::FuncOp MLIRGenImpl::find_func_op(mlir::ModuleOp mod,
                                             std::string_view name) const {
    // The cache covers BOTH resolution paths below, keyed by the raw callee name.
    // CRITICAL: MLIR's mod.lookupSymbol is NOT O(1) — it is a LINEAR SCAN of the
    // module's ops, reading each FuncOp's sym_name attribute (getInherentAttr).
    // The same callee recurs across many call sites, so without caching even the
    // "fast" direct-hit path every call paid O(functions) → O(calls × functions)
    // = O(n²) (profile: getInherentAttr 17% + lookupSymbolIn 8.5%). Caching every
    // HIT makes each distinct callee pay the scan ONCE, then O(1). Misses are NOT
    // cached — a name may resolve once a later forward-decl is emitted; cached
    // HITS stay valid (FuncOp defs are never renamed/removed during body-gen).
    std::string key(name);
    if (auto it = find_func_op_cache_.find(key); it != find_func_op_cache_.end())
        return it->second;
    // Build the dirty-gated name→FuncOp + canonical indices once (O(1) when
    // clean), so the lookups below are O(1) hash hits instead of O(funcs)
    // mod.lookupSymbol linear scans.
    ensure_ffo_canon_index(mod);
    // Direct symbol-table hit (free fns — already qualified in the LIR — and
    // intra-module bare names). The common case; now memoised like the rest.
    if (auto it = ffo_symtab_.find(key); it != ffo_symtab_.end()) {
        auto fn = it->second;
        find_func_op_cache_.emplace(std::move(key), fn);
        return fn;
    }
    // Bare miss (the common case for a module-qualified METHOD callee). The
    // remaining resolution is the expensive part (string-building + possible
    // O(n) walks).
    auto resolve = [&]() -> mlir::func::FuncOp {
        // Module system: the callee is BARE but its FuncOp is module-qualified.
        // Try the EXACT qualified form via an O(1) symbol-table lookup IMMEDIATELY
        // — BEFORE any O(n) find_fn_matching walk. Under modules nearly every
        // method callee misses the bare lookup, so deferring the qualified hash
        // probe past the linear walks would make resolution O(calls × functions)
        // = O(n²). It also wins correctness: the exact qualified symbol beats the
        // sig-stripping canonical fallback, which would collapse distinct-base /
        // distinct-signature defs onto the wrong one (String::from→new, or
        // set(_,WAny) for an i64 arg).
        if (auto q = link_name_str(key); q != name)
            if (auto it = ffo_symtab_.find(q); it != ffo_symtab_.end())
                return it->second;
        // (The former exact-name / exact-qualified find_fn_matching walks here
        // were O(n) and redundant: lookupSymbol above and at the top of
        // find_func_op already resolve those exact names in O(1) — a
        // func::FuncOp's getName() IS its symbol-table key.)
        // Hardcoded stdlib intrinsic lookups (e.g. `writ_build_from_template`)
        // must also resolve the post-unify pkg-qualified + sig-suffixed form
        // (`std.writ.ctr$<bare>__f__<sig>`). Match callee↔def by canonical key.
        //
        // Symmetric canonical match: canonicalise BOTH the callee and each
        // candidate (the deleted §P4 bridge did the same). A no-sig / differently
        // -pkg'd callee (e.g. an assoc-const accessor `pkg.T__kassoc_C` with no
        // `__f__sig`) only matches its real `…T__kassoc_C__f__sig` def once the
        // input's pkg+sig are stripped too. UNAMBIGUOUS-only: a canonical key
        // shared by >1 def (overloads) resolves to nothing — first-match would
        // bind the wrong one.
        //
        // Indexed: canonicalising every def name on every call was O(calls ×
        // functions) — ~5M string-builds / 1.1s on a 56-test batch. Build a
        // canonical→FuncOp index (with an ambiguous-key set) ONCE and reuse it;
        // it's rebuilt only if the module's FuncOp set changes (dirty-gated;
        // already built at the top of find_func_op).
        auto cname = ffo_canonical(name);
        if (ffo_canon_ambig_.count(cname)) return {};
        if (auto it = ffo_canon_index_.find(cname); it != ffo_canon_index_.end())
            return it->second;
        return {};
    };
    auto resolved = resolve();
    if (resolved) find_func_op_cache_.emplace(std::move(key), resolved);
    return resolved;
}

// ---------------------------------------------------------------------------
// gen_expr — main dispatcher
// ---------------------------------------------------------------------------

mlir::Value MLIRGenImpl::gen_expr(lir_view::ExprRef er) {
    if (!er) {
        std::fprintf(stderr, "mlir_gen: gen_expr called without LIR mirror\n");
        return nullptr;
    }
    TypeRef ty = er.type(pool_impl());
    using C = lir_schema::expr::Code;
    switch (er.kind()) {
    case C::LitInt:     return gen_expr_kind(lir_view::ELitIntView{er},     ty);
    case C::LitFloat:   return gen_expr_kind(lir_view::ELitFloatView{er},   ty);
    case C::LitBool:    return gen_expr_kind(lir_view::ELitBoolView{er},    ty);
    case C::LitStr:     return gen_expr_kind(lir_view::ELitStrView{er},     ty);
    case C::VarRef:     return gen_expr_kind(lir_view::EVarRefView{er},     ty);
    case C::EnumLit:    return gen_expr_kind(lir_view::EEnumLitView{er},    ty);
    case C::EnumLitData:return gen_expr_kind(lir_view::EEnumLitDataView{er},ty);
    case C::Call:       return spill_slice_call_result(gen_expr_kind(lir_view::ECallView{er},       ty), ty);
    case C::MethodCall: return spill_slice_call_result(gen_expr_kind(lir_view::EMethodCallView{er}, ty), ty);
    case C::BinOp:      return gen_expr_kind(lir_view::EBinOpView{er},      ty);
    case C::Unary:      return gen_expr_kind(lir_view::EUnaryView{er},      ty);
    case C::AddrOf:     return gen_expr_kind(lir_view::EAddrOfView{er},     ty);
    case C::AddrOfTemp: return gen_expr_kind(lir_view::EAddrOfTempView{er}, ty);
    case C::Deref:      return gen_expr_kind(lir_view::EDerefView{er},      ty);
    case C::FieldRead:  return gen_expr_kind(lir_view::EFieldReadView{er},  ty);
    case C::IndexRead:  return gen_expr_kind(lir_view::EIndexReadView{er},  ty);
    case C::StructLit:  return gen_expr_kind(lir_view::EStructLitView{er},  ty);
    case C::ArrLit:     return gen_expr_kind(lir_view::EArrLitView{er},     ty);
    case C::Cast:       return gen_expr_kind(lir_view::ECastView{er},       ty);
    case C::IfExpr:     return gen_expr_kind(lir_view::EIfExprView{er},     ty);
    case C::TupleLit:   return gen_expr_kind(lir_view::ETupleLitView{er},   ty);
    case C::TupleIndex: return gen_expr_kind(lir_view::ETupleIndexView{er}, ty);
    case C::SliceLit:   return gen_expr_kind(lir_view::ESliceLitView{er},   ty);
    case C::SliceIndex: return gen_expr_kind(lir_view::ESliceIndexView{er}, ty);
    case C::SliceLen:   return gen_expr_kind(lir_view::ESliceLenView{er},   ty);
    case C::SlicePtr:   return gen_expr_kind(lir_view::ESlicePtrView{er},   ty);
    case C::ClosureBox: return gen_expr_kind(lir_view::EClosureBoxView{er}, ty);
    case C::ClosureCall:return gen_expr_kind(lir_view::EClosureCallView{er},ty);
    case C::FnPtrCall:  return gen_expr_kind(lir_view::EFnPtrCallView{er},  ty);
    case C::FormatCall: return gen_expr_kind(lir_view::EFormatCallView{er}, ty);
    case C::PackExpand: return gen_expr_kind(lir_view::EPackExpandView{er}, ty);
    case C::Try:        return gen_expr_kind(lir_view::ETryView{er},        ty);
    case C::MatchExpr:  return gen_expr_kind(lir_view::EMatchExprView{er},  ty);
    case C::SizeOf:     return gen_expr_kind(lir_view::ESizeOfView{er},     ty);
    case C::AlignOf:    return gen_expr_kind(lir_view::EAlignOfView{er},    ty);
    case C::GenericRef: {
        // Slice 2 invariant: mono_clone subst_expr rewrites every GenericRef
        // node into a VarRef of FnPtr type before mlir-gen runs. Reaching
        // here means a generic body had an unresolved GenericRef — diagnose
        // loudly rather than emit garbage.
        std::fprintf(stderr, "mlir_gen: unresolved GenericRef '%.*s' — mono failed to substitute its type-args\n",
                     (int)lir_view::EGenericRefView{er}.name().size(),
                     lir_view::EGenericRefView{er}.name().data());
        std::abort();
    }
    case C::TypeCodeOf: return gen_expr_kind(lir_view::ETypeCodeOfView{er}, ty);
    case C::BlockExpr:  return gen_expr_kind(lir_view::EBlockExprView{er},  ty);
    case C::WritLit:  return gen_expr_kind(lir_view::EWritLitView{er},  ty);
    case C::PtrArith:   return gen_expr_kind(lir_view::EPtrArithView{er},   ty);
    case C::PtrDiff:    return gen_expr_kind(lir_view::EPtrDiffView{er},    ty);
    case C::ReflectOf:  return gen_expr_kind(lir_view::EReflectOfView{er},  ty);
    }
    bug("unhandled expr code {} — the expression lowers to no value", int(er.kind()));
    return nullptr;
}

// ---------------------------------------------------------------------------
// Literals
// ---------------------------------------------------------------------------

mlir::Value MLIRGenImpl::gen_expr_kind(lir_view::ELitIntView v, TypeRef type) {
    int64_t value = v.value();
    int width = 32;
    if (type) {
        switch (TypeRef(type).kind()) {
        case LogosType::Kind::I64:
        case LogosType::Kind::U64: width = 64; break;
        case LogosType::Kind::I8:
        case LogosType::Kind::U8:  width = 8;  break;
        case LogosType::Kind::I16:
        case LogosType::Kind::U16: width = 16; break;
        case LogosType::Kind::I24:
        case LogosType::Kind::U24: width = 24; break;
        case LogosType::Kind::I56:
        case LogosType::Kind::U56: width = 56; break;
        case LogosType::Kind::I128:
        case LogosType::Kind::U128: width = 128; break;
        case LogosType::Kind::Usize:
        case LogosType::Kind::Isize:
            // K10-co-05: pointer-sized integers — match the target ABI
            // bit-width so an `Nusize` suffixed literal lowers as i64
            // (on a 64-bit target) instead of falling to the default
            // i32 path. Critical for `&3usize` temp-materialisation:
            // alloca x i64 with c3_i32 stored leaves high bits
            // uninitialised; downstream `load i64` returns garbage.
            width = ::logos::compiler::g_target_pointer_bits;
            break;
        case LogosType::Kind::Bool: width = 1; break;
        case LogosType::Kind::IntLit:
            // Untyped literal: use i64 if value doesn't fit in i32.
            if (value > INT32_MAX || value < INT32_MIN) width = 64;
            break;
        default: break;
        }
    }
    // 128-bit literal: assemble the full value from both halves (low = value,
    // high = value_hi) — a bare int64 would lose the top 64 bits. APInt takes
    // the words low-first.
    if (width == 128) {
        uint64_t words[2] = { (uint64_t)value, (uint64_t)v.value_hi() };
        llvm::APInt big(128, llvm::ArrayRef<uint64_t>(words, 2));
        return builder_.create<mlir::arith::ConstantOp>(
            loc_, builder_.getIntegerType(128),
            builder_.getIntegerAttr(builder_.getIntegerType(128), big));
    }
    return builder_.create<mlir::arith::ConstantIntOp>(loc_, value, width);
}

mlir::Value MLIRGenImpl::gen_expr_kind(lir_view::ELitFloatView v, TypeRef type) {
    bool is_f32 = type && TypeRef(type).kind() == LogosType::Kind::F32;
    if (is_f32) {
        auto f32 = builder_.getF32Type();
        return logos::compat::const_float(builder_, loc_, f32,
                                          llvm::APFloat(float(v.value())));
    }
    auto f64 = builder_.getF64Type();
    return logos::compat::const_float(builder_, loc_, f64,
                                      llvm::APFloat(v.value()));
}

mlir::Value MLIRGenImpl::gen_expr_kind(lir_view::ELitBoolView v, TypeRef) {
    return builder_.create<mlir::arith::ConstantIntOp>(loc_, v.value() ? 1 : 0, 1);
}

mlir::Value MLIRGenImpl::gen_expr_kind(lir_view::ELitStrView v, TypeRef) {
    std::string raw{v.value()};
    bool is_raw = raw.size() >= 3 && raw[0] == 'r' &&
                  (raw[1] == '"' || raw[1] == '#');
    if (is_raw) {
        // Count '#' delimiters: r"...", r#"..."#, r##"..."##, etc.
        size_t hashes = 0;
        size_t p = 1;
        while (p < raw.size() && raw[p] == '#') { ++hashes; ++p; }
        // Strip r + hashes + opening " ... closing " + hashes
        raw = raw.substr(p + 1, raw.size() - p - 1 - hashes - 1);
    } else {
        // Regular string "..." — strip surrounding quotes.
        if (raw.size() >= 2 && raw.front() == '"' && raw.back() == '"')
            raw = raw.substr(1, raw.size() - 2);
    }
    // Process escape sequences (skipped for raw strings).
    std::string text;
    if (is_raw) {
        text = raw;
    } else {
        auto hexval = [](char c) -> int {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            return -1;
        };
        for (size_t i = 0; i < raw.size(); ++i) {
            if (raw[i] == '\\' && i + 1 < raw.size()) {
                switch (raw[i + 1]) {
                    case 'n':  text.push_back('\n'); ++i; break;
                    case 't':  text.push_back('\t'); ++i; break;
                    case 'r':  text.push_back('\r'); ++i; break;
                    case '\\': text.push_back('\\'); ++i; break;
                    case '0':  text.push_back('\0'); ++i; break;
                    case '"':  text.push_back('"');  ++i; break;
                    case '\'': text.push_back('\''); ++i; break;
                    // G160-7: `\xNN` byte escape (mirrors the char-literal
                    // decoder). Exactly two hex digits.
                    case 'x': {
                        int hi = (i + 2 < raw.size()) ? hexval(raw[i + 2]) : -1;
                        int lo = (i + 3 < raw.size()) ? hexval(raw[i + 3]) : -1;
                        if (hi >= 0 && lo >= 0) {
                            text.push_back(static_cast<char>((hi << 4) | lo));
                            i += 3;
                        } else { text.push_back(raw[i]); }
                        break;
                    }
                    // G160-7: `\u{NN..}` unicode escape → UTF-8 bytes.
                    case 'u': {
                        size_t j = i + 2;
                        if (j < raw.size() && raw[j] == '{') {
                            ++j;
                            uint32_t cp = 0; bool any = false;
                            while (j < raw.size() && raw[j] != '}') {
                                int h = hexval(raw[j]);
                                if (h < 0) break;
                                cp = (cp << 4) | (uint32_t)h; any = true; ++j;
                            }
                            if (any && j < raw.size() && raw[j] == '}') {
                                // UTF-8 encode cp.
                                if (cp <= 0x7F) text.push_back((char)cp);
                                else if (cp <= 0x7FF) {
                                    text.push_back((char)(0xC0 | (cp >> 6)));
                                    text.push_back((char)(0x80 | (cp & 0x3F)));
                                } else if (cp <= 0xFFFF) {
                                    text.push_back((char)(0xE0 | (cp >> 12)));
                                    text.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
                                    text.push_back((char)(0x80 | (cp & 0x3F)));
                                } else {
                                    text.push_back((char)(0xF0 | (cp >> 18)));
                                    text.push_back((char)(0x80 | ((cp >> 12) & 0x3F)));
                                    text.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
                                    text.push_back((char)(0x80 | (cp & 0x3F)));
                                }
                                i = j;  // consumed through '}'
                            } else { text.push_back(raw[i]); }
                        } else { text.push_back(raw[i]); }
                        break;
                    }
                    default:   text.push_back(raw[i]); break;
                }
            } else {
                text.push_back(raw[i]);
            }
        }
    }
    // LLVM requires string globals to include a null terminator in the array type.
    // The fat pointer's `len` field holds the content length (without the null byte).
    auto global_name = ".str." + std::to_string(str_counter_++);
    auto parent_mod  = builder_.getBlock()->getParent()->getParentOfType<mlir::ModuleOp>();
    auto save_pt     = builder_.saveInsertionPoint();
    builder_.setInsertionPointToStart(parent_mod.getBody());

    std::string text_with_null = text + '\0';
    auto i8       = builder_.getIntegerType(8);
    auto arr_type = mlir::LLVM::LLVMArrayType::get(i8, text_with_null.size());
    auto str_attr = builder_.getStringAttr(llvm::StringRef(text_with_null.data(), text_with_null.size()));
    builder_.create<mlir::LLVM::GlobalOp>(
        loc_, arr_type, true, mlir::LLVM::Linkage::Internal, global_name, str_attr);

    builder_.restoreInsertionPoint(save_pt);
    auto raw_ptr = builder_.create<mlir::LLVM::AddressOfOp>(loc_, ptr_type(), global_name);

    // Build fat pointer {ptr, len} on the stack and return pointer to it.
    auto stype  = slice_llvm_type();
    auto alloca = create_entry_alloca(stype);
    llvm::SmallVector<mlir::LLVM::GEPArg> pi{int32_t(0), int32_t(0)};
    auto pp = builder_.create<mlir::LLVM::GEPOp>(loc_, ptr_type(), stype, alloca, pi);
    builder_.create<mlir::LLVM::StoreOp>(loc_, raw_ptr, pp);
    llvm::SmallVector<mlir::LLVM::GEPArg> li{int32_t(0), int32_t(1)};
    auto lp = builder_.create<mlir::LLVM::GEPOp>(loc_, ptr_type(), stype, alloca, li);
    auto len_val = builder_.create<mlir::arith::ConstantIntOp>(loc_, (int64_t)text.size(), 64);
    builder_.create<mlir::LLVM::StoreOp>(loc_, len_val, lp);
    return alloca;
}

// ---------------------------------------------------------------------------
// Variable reference
// ---------------------------------------------------------------------------

mlir::Value MLIRGenImpl::gen_expr_kind(lir_view::EVarRefView v, TypeRef type) {
    std::string name{v.name()};
    // §6.2 statics (S25): a "__static_addr:<sym>" VarRef is the ADDRESS of the
    // static's global storage — emit llvm.mlir.addressof. Reads wrap this in a
    // Deref (loads/yields the storage ptr per kind); `&STATIC` collapses to it
    // via the reborrow peephole; writes deref-store into it. One chokepoint.
    if (name.rfind("__static_addr:", 0) == 0) {
        auto sym = name.substr(std::string_view("__static_addr:").size());
        return builder_.create<mlir::LLVM::AddressOfOp>(loc_, ptr_type(), sym);
    }
    // Module constant: re-evaluate inline. (Statics are NOT inlined — they have
    // real storage and never reach here as a bare VarRef.)
    // G156-1: resolve current-function-package first (same as sema).
    if (auto* cv = resolve_const_(name); cv && !cv->is_static())
        return gen_expr(cv->value());

    auto it = scope_.find(name);
    if (it == scope_.end()) {
        auto parent_mod = builder_.getBlock()->getParent()->getParentOfType<mlir::ModuleOp>();
        // Check if name is a free function being used as a bare fn-ptr.
        if (type && LogosType::is_fn_value_kind(TypeRef(type).kind())) {
            auto fn_sym = parent_mod.lookupSymbol<mlir::func::FuncOp>(name);
            if (fn_sym) {
                // Return just the function address as a raw ptr.
                auto fn_ref = builder_.create<mlir::func::ConstantOp>(
                    loc_, fn_sym.getFunctionType(), name);
                return builder_.create<mlir::UnrealizedConversionCastOp>(
                    loc_, ptr_type(), mlir::ValueRange{fn_ref}).getResult(0);
            }
        }
        // Check if name is a free function being used as a value (closure fat pointer).
        // Create a non-capturing closure: {fn_ptr, null_env}.
        if (type && TypeRef(type).kind() == LogosType::Kind::Closure) {
            auto fn_sym = parent_mod.lookupSymbol<mlir::func::FuncOp>(name);
            if (fn_sym) {
                // Build closure fat pointer: { fn_ptr, env_ptr=null }
                auto closure_struct_t = mlir::LLVM::LLVMStructType::getLiteral(
                    builder_.getContext(), {ptr_type(), ptr_type()});
                auto alloca = create_entry_alloca(closure_struct_t);
                // Store the function address as fn_ptr.
                auto fn_ref = builder_.create<mlir::func::ConstantOp>(
                    loc_, fn_sym.getFunctionType(), name);
                auto fn_addr = builder_.create<mlir::UnrealizedConversionCastOp>(
                    loc_, ptr_type(), mlir::ValueRange{fn_ref}).getResult(0);
                llvm::SmallVector<mlir::LLVM::GEPArg> fp_idx{int32_t(0), int32_t(0)};
                auto fp_ptr = builder_.create<mlir::LLVM::GEPOp>(
                    loc_, ptr_type(), closure_struct_t, alloca, fp_idx);
                builder_.create<mlir::LLVM::StoreOp>(loc_, fn_addr, fp_ptr);
                // Store null as env_ptr.
                auto null_ptr = builder_.create<mlir::LLVM::ZeroOp>(loc_, ptr_type());
                llvm::SmallVector<mlir::LLVM::GEPArg> ep_idx{int32_t(0), int32_t(1)};
                auto ep_ptr = builder_.create<mlir::LLVM::GEPOp>(
                    loc_, ptr_type(), closure_struct_t, alloca, ep_idx);
                builder_.create<mlir::LLVM::StoreOp>(loc_, null_ptr, ep_ptr);
                return alloca;
            }
        }
        // Suppress noise from stale VarRefs left over by mono specialization
        // of `Option<void>` / `Result<void, …>` etc. — pattern variable
        // bindings for void payloads (e.g. `Option::Some(v) => return v`)
        // become unreachable but mono still clones them. Print only under
        // an opt-in env var so debugging stays available.
        if (std::getenv("LOGOS_MLIRGEN_DEBUG_UNDEF"))
            std::fprintf(stderr, "mlir_gen: undefined '%s' in fn '%s'\n",
                         name.c_str(), cur_fn_name_.c_str());
        return nullptr;
    }
    // Enum value-repr: the slot holds the inline storage ptr directly (one
    // level, like a Struct) — return it. (Legacy mutable-enum ptr-slot is gone.)
    if (var_tagged_enum_ptr_.count(name))
        return it->second;
    // Struct/array/tuple/tagged-enum/dyn-trait variables: return pointer directly.
    if (var_struct_.count(name))
        return get_struct_ptr(name);
    if (var_subscript_.count(name) || var_tuple_.count(name) ||
        var_tagged_enum_.count(name) || var_dyn_trait_.count(name))
        return it->second;
    // Let-bound scalar: load from alloca.
    if (let_vars_.count(name)) {
        auto et = var_elem_types_.find(name);
        if (et == var_elem_types_.end()) return nullptr;
        return builder_.create<mlir::LLVM::LoadOp>(loc_, et->second, it->second);
    }
    // Parameter SSA value.
    return it->second;
}

// ---------------------------------------------------------------------------
// Enum literals
// ---------------------------------------------------------------------------

mlir::Value MLIRGenImpl::gen_expr_kind(lir_view::EEnumLitView v, TypeRef type) {
    std::string enum_name(v.enum_name());
    int64_t     disc = v.disc();
    // Tagged enum without payload (e.g. Option::None, HttpError::Io):
    // heap-allocate so the pointer can safely escape — including being stored
    // into another enum's payload slot as a pointer (EEnumLitData path).
    auto* te = resolve_tagged_enum(enum_name, type);
    if (te) {
        // Enum value-repr: inline stack storage (alloca), like a Struct — NOT a
        // heap block. The address is one-level `&Enum`. Recursive-by-value enums
        // are already rejected by check_recursive_value_types, so this is safe.
        auto store = create_entry_alloca(te->llvm_type);
        if (!store) return nullptr;
        enum_store_disc(store, *te, disc);
        return store;
    }
    // C-style enum: just the discriminant, sized per backing type.
    return builder_.create<mlir::arith::ConstantIntOp>(
        loc_, disc, enum_disc_bits(enum_name, type));
}

mlir::Value MLIRGenImpl::gen_expr_kind(lir_view::EEnumLitDataView v, TypeRef type) {
    std::string enum_name(v.enum_name());
    int64_t disc = v.disc();
    std::vector<lir_view::ExprRef> payload;
    v.each_payload([&](lir_view::ExprRef pr){ payload.push_back(pr); });

    auto* te = resolve_tagged_enum(enum_name, type);
    if (!te && payload.empty()) {
        // A BRACE-EMPTY variant (`E::Empty4 {}`) carries no data, so an enum
        // whose variants are all like that is a plain C-style enum and has no
        // tagged representation to resolve. The SYNTAX said "data variant"; the
        // FACT is the payload, and the payload is empty. Lower it exactly as
        // `E::Empty5` lowers — the discriminant constant.
        // Measured before: `unknown tagged enum 'E'`, the literal lowered to
        // nothing, and the `classify(E::Empty4 {})` call vanished.
        return builder_.create<mlir::arith::ConstantIntOp>(
            loc_, disc, enum_disc_bits(enum_name, type));
    }
    if (!te) {
        bug("unknown tagged enum '{}' — the enum instance was never emitted, so "
            "every use of it lowers to nothing", enum_name);
        return nullptr;
    }
    auto& info = *te;
    // Enum value-repr: inline stack storage (alloca), like a Struct. Returned
    // by value (caller loads the {disc,payload} struct via llvm_fn_ret_type) or
    // embedded inline into a parent aggregate by memcpy.
    auto alloca = create_entry_alloca(info.llvm_type);
    if (!alloca) return nullptr;
    // Store discriminant (Phase 3.5: via the representation chokepoint).
    enum_store_disc(alloca, info, disc);
    // Store payload into the payload area, bitcasted.
    // LowBit niche: encode the single payload field into the word — the ptr arm
    // raw (low bit 0, guaranteed by ≥2 alignment), the value arm `(v<<1)|1`.
    if (info.niche.packed && info.niche.kind == TaggedEnumInfo::Niche::LowBit) {
        mlir::Value word;
        if (!payload.empty()) {
            auto v = payload[0] ? gen_expr(payload[0]) : nullptr;
            if (!v) return nullptr;
            if (disc == info.niche.val_disc) {
                TypeRef vt = nullptr;
                for (auto& vi : info.variants)
                    if (vi.disc == disc && !vi.logos_types.empty()) { vt = vi.logos_types[0]; break; }
                auto vi64 = coerce_int(v, builder_.getI64Type(), vt);
                if (info.niche.val_raw) {
                    // RAW mode (WAny Pod): the value IS the final word (low-bit-1
                    // already baked in by the producer) — store verbatim, no shift.
                    word = vi64;
                } else {
                    // value arm: extend to i64 (by signedness), then `(v<<1)|1`.
                    auto one = builder_.create<mlir::arith::ConstantIntOp>(loc_, 1, 64);
                    auto sh  = builder_.create<mlir::LLVM::ShlOp>(loc_, vi64, one);
                    word     = builder_.create<mlir::LLVM::OrOp>(loc_, sh, one);
                }
            } else {
                // ptr arm: pointer → i64 raw (low bit 0).
                word = (v.getType() == ptr_type())
                       ? builder_.create<mlir::LLVM::PtrToIntOp>(loc_, builder_.getI64Type(), v).getResult()
                       : coerce_int(v, builder_.getI64Type());
            }
        } else {
            word = builder_.create<mlir::arith::ConstantIntOp>(loc_, 0, 64);
        }
        builder_.create<mlir::LLVM::StoreOp>(loc_, word, alloca);
        return alloca;
    }
    if (!payload.empty()) {
        auto pay_ptr = enum_payload_ptr(alloca, info);
        // Find the variant's field types
        const TaggedEnumInfo::VariantPayload* vp = nullptr;
        for (auto& vi : info.variants)
            if (vi.disc == disc) { vp = &vi; break; }
        if (vp) {
            // Build a struct type for this variant's payload (inline aggregate
            // types so field offsets match the inline memcpy footprint).
            auto pay_struct = variant_payload_struct(*vp);
            for (size_t i = 0; i < payload.size() && i < vp->field_types.size(); ++i) {
                if (!payload[i]) return nullptr;
                TypeRef pl_ty = payload[i].type(pool_impl());
                auto val = gen_expr(payload[i]);
                if (!val) return nullptr;
                llvm::SmallVector<mlir::LLVM::GEPArg> fi{int32_t(0), int32_t(i)};
                auto fp = builder_.create<mlir::LLVM::GEPOp>(
                    loc_, ptr_type(), pay_struct, pay_ptr, fi);
                // For inline structs, val is a *Struct pointer; copy bytes into payload.
                TypeRef lt = i < vp->logos_types.size() ? vp->logos_types[i] : nullptr;
                bool is_inline = lt && (TypeRef(lt).kind() == LogosType::Kind::Struct ||
                                        TypeRef(lt).kind() == LogosType::Kind::ZonedStruct ||
                                        TypeRef(lt).kind() == LogosType::Kind::Tuple ||
                                        TypeRef(lt).kind() == LogosType::Kind::Slice ||
                                        TypeRef(lt).kind() == LogosType::Kind::Closure ||
                                        TypeRef(lt).kind() == LogosType::Kind::Array);
                // Enum value-repr: a nested TAGGED enum payload is a
                // ptr-to-inline-storage; memcpy its full {disc,payload} footprint
                // inline. A C-like enum (no TaggedEnumInfo) is a bare i32 disc —
                // it takes the scalar store branch below.
                if (lt && TypeRef(lt).kind() == LogosType::Kind::Enum &&
                    resolve_tagged_enum(std::string(TypeRef(lt).enum_name()), lt))
                    is_inline = true;
                // `&dyn`/`dyn`/`Box<dyn>` payload (TraitObject): coerce a concrete
                // source to a STACK 16-byte fat pair, then store it INLINE in the
                // payload (memcpy-16) — the fat lives in the enum value, moves with
                // it, and frees with it (Box<dyn> data freed by vtable[0] on drop).
                if (lt && TypeRef(lt).kind() == LogosType::Kind::TraitObject) {
                    val = coerce_value_to_dyn_if_needed(val, lt, pl_ty);
                    is_inline = true;
                }
                if (is_inline) {
                    std::unordered_set<std::string> seen;
                    uint64_t sz = logos_abi_byte_size(lt, seen);
                    auto sz_val = builder_.create<mlir::arith::ConstantIntOp>(loc_, (int64_t)sz, 64);
                    // val may be a struct / array value (e.g. result of a
                    // function call or pointer-deref returning an aggregate)
                    // rather than a pointer. memcpy requires a pointer
                    // source — spill aggregate values.
                    if (mlir::isa<mlir::LLVM::LLVMStructType>(val.getType()) ||
                        mlir::isa<mlir::LLVM::LLVMArrayType>(val.getType()))
                        val = spill_to_alloca(val);
                    builder_.create<mlir::LLVM::MemcpyOp>(loc_, fp, val, sz_val, false);
                } else {
                    // Non-inline scalar payload (a trait-object payload always
                    // takes the inline branch above, so coerce_value_to_dyn_if_needed
                    // is a no-op here — kept only for the harmless general case).
                    val = coerce_value_to_dyn_if_needed(val, lt, pl_ty);
                    builder_.create<mlir::LLVM::StoreOp>(loc_, coerce_int(val, vp->field_types[i]), fp);
                }
            }
        }
    }
    return alloca;
}

// ---------------------------------------------------------------------------
// Binary / Unary operators
// ---------------------------------------------------------------------------

mlir::Value MLIRGenImpl::gen_expr_kind(lir_view::EBinOpView v, TypeRef) {
    // Stage D (consumers-first): read operands straight from the mirror view —
    // no lexpr_of round-trip back to the C++ skeleton. Operand types come from
    // the mirror (ExprRef::type), same value the old node->type field carried.
    if (!v.lhs() || !v.rhs()) return nullptr;
    TypeRef lhs_ty = v.lhs().type(pool_impl());
    TypeRef rhs_ty = v.rhs().type(pool_impl());
    std::string op{v.op()};
    auto lhs = gen_expr(v.lhs());
    if (!lhs) return nullptr;

    // Short-circuit operators: evaluate RHS only when LHS doesn't determine the result.
    // && : if LHS is false, result is false (skip RHS)
    // || : if LHS is true,  result is true  (skip RHS)
    if (op == "&&" || op == "||") {
        auto i1 = builder_.getI1Type();
        auto result_alloca = create_entry_alloca(i1);

        auto* region      = builder_.getBlock()->getParent();
        auto* rhs_block   = new mlir::Block();
        auto* sc_block    = new mlir::Block();
        auto* merge_block = new mlir::Block();
        region->push_back(rhs_block);
        region->push_back(sc_block);
        region->push_back(merge_block);

        // && : evaluate RHS when LHS=true; short-circuit to false when LHS=false
        // || : evaluate RHS when LHS=false; short-circuit to true  when LHS=true
        if (op == "&&")
            builder_.create<mlir::cf::CondBranchOp>(loc_, lhs, rhs_block, sc_block);
        else
            builder_.create<mlir::cf::CondBranchOp>(loc_, lhs, sc_block, rhs_block);

        // Short-circuit block: store the known result without evaluating RHS.
        builder_.setInsertionPointToStart(sc_block);
        auto sc_val = builder_.create<mlir::arith::ConstantIntOp>(
            loc_, (op == "||") ? 1 : 0, 1);
        builder_.create<mlir::LLVM::StoreOp>(loc_, sc_val, result_alloca);
        builder_.create<mlir::cf::BranchOp>(loc_, merge_block);

        // RHS block: evaluate RHS, store its value. A diverging RHS
        // (`c || return false`) already emitted its terminator — skip the
        // store + merge-branch so we don't append after the block terminator.
        builder_.setInsertionPointToStart(rhs_block);
        auto rhs_val = gen_expr(v.rhs());
        if (!is_terminated(builder_.getBlock())) {
            if (!rhs_val)
                rhs_val = builder_.create<mlir::arith::ConstantIntOp>(loc_, 0, 1);
            builder_.create<mlir::LLVM::StoreOp>(loc_, rhs_val, result_alloca);
            builder_.create<mlir::cf::BranchOp>(loc_, merge_block);
        }

        builder_.setInsertionPointToStart(merge_block);
        return builder_.create<mlir::LLVM::LoadOp>(loc_, i1, result_alloca);
    }

    auto rhs = gen_expr(v.rhs());
    if (!rhs) return nullptr;
    // Widen narrower integer operand, using zero-extend for unsigned types.
    if (auto li = mlir::dyn_cast<mlir::IntegerType>(lhs.getType())) {
        if (auto ri = mlir::dyn_cast<mlir::IntegerType>(rhs.getType())) {
            if (li.getWidth() < ri.getWidth()) {
                bool lhs_unsigned = lhs_ty &&
                    LogosType::is_unsigned_repr_kind(TypeRef(lhs_ty).kind());
                if (lhs_unsigned)
                    lhs = builder_.create<mlir::arith::ExtUIOp>(loc_, rhs.getType(), lhs);
                else
                    lhs = builder_.create<mlir::arith::ExtSIOp>(loc_, rhs.getType(), lhs);
            } else if (ri.getWidth() < li.getWidth()) {
                bool rhs_unsigned = rhs_ty &&
                    LogosType::is_unsigned_repr_kind(TypeRef(rhs_ty).kind());
                if (rhs_unsigned)
                    rhs = builder_.create<mlir::arith::ExtUIOp>(loc_, lhs.getType(), rhs);
                else
                    rhs = builder_.create<mlir::arith::ExtSIOp>(loc_, lhs.getType(), rhs);
            }
        }
    }
    // Unify operand types for mixed arithmetic:
    // float+int → convert int to float; float+float of different widths → widen narrower.
    if (mlir::isa<mlir::FloatType>(lhs.getType()) &&
        mlir::isa<mlir::IntegerType>(rhs.getType())) {
        bool rhs_unsigned = rhs_ty &&
            LogosType::is_unsigned_repr_kind(TypeRef(rhs_ty).kind());
        if (rhs_unsigned)
            rhs = builder_.create<mlir::arith::UIToFPOp>(loc_, lhs.getType(), rhs);
        else
            rhs = builder_.create<mlir::arith::SIToFPOp>(loc_, lhs.getType(), rhs);
    }
    if (mlir::isa<mlir::IntegerType>(lhs.getType()) &&
        mlir::isa<mlir::FloatType>(rhs.getType())) {
        bool lhs_unsigned = lhs_ty &&
            LogosType::is_unsigned_repr_kind(TypeRef(lhs_ty).kind());
        if (lhs_unsigned)
            lhs = builder_.create<mlir::arith::UIToFPOp>(loc_, rhs.getType(), lhs);
        else
            lhs = builder_.create<mlir::arith::SIToFPOp>(loc_, rhs.getType(), lhs);
    }
    // float+float of different widths: convert the FloatLit operand to match the typed one.
    // If both are typed floats of different widths, widen the narrower.
    if (lhs.getType() != rhs.getType()) {
        auto lft = mlir::dyn_cast<mlir::FloatType>(lhs.getType());
        auto rft = mlir::dyn_cast<mlir::FloatType>(rhs.getType());
        if (lft && rft) {
            bool lhs_is_lit = lhs_ty && TypeRef(lhs_ty).kind() == LogosType::Kind::FloatLit;
            bool rhs_is_lit = rhs_ty && TypeRef(rhs_ty).kind() == LogosType::Kind::FloatLit;
            if (rhs_is_lit && !lhs_is_lit) {
                // rhs is FloatLit, lhs is typed: coerce rhs to lhs type
                rhs = coerce_float(rhs, lhs.getType());
            } else if (lhs_is_lit && !rhs_is_lit) {
                // lhs is FloatLit, rhs is typed: coerce lhs to rhs type
                lhs = coerce_float(lhs, rhs.getType());
            } else {
                // Both typed floats: widen the narrower
                if (lft.getWidth() < rft.getWidth())
                    lhs = builder_.create<mlir::arith::ExtFOp>(loc_, rhs.getType(), lhs);
                else
                    rhs = builder_.create<mlir::arith::ExtFOp>(loc_, lhs.getType(), rhs);
            }
        }
    }
    bool is_float = mlir::isa<mlir::FloatType>(lhs.getType());
    if (is_float) {
        if (op == "+")  return builder_.create<mlir::arith::AddFOp>(loc_, lhs, rhs);
        if (op == "-")  return builder_.create<mlir::arith::SubFOp>(loc_, lhs, rhs);
        if (op == "*")  return builder_.create<mlir::arith::MulFOp>(loc_, lhs, rhs);
        if (op == "/")  return builder_.create<mlir::arith::DivFOp>(loc_, lhs, rhs);
        if (op == "%")  return builder_.create<mlir::arith::RemFOp>(loc_, lhs, rhs);
        if (op == "==") return builder_.create<mlir::arith::CmpFOp>(loc_, mlir::arith::CmpFPredicate::OEQ, lhs, rhs);
        // `!=` is the NEGATION of `==`, so it is UNORDERED: `NaN != x` is TRUE
        // for every x (IEEE-754, and Rust's `PartialEq for f64`). ONE (ordered
        // not-equal) answers FALSE whenever either operand is NaN, which made
        // `!(a != b)` and `a == b` disagree — and made a `where k != v` filter
        // drop every NaN row. The four relational operators stay ORDERED (a
        // NaN compares false against everything), which is also Rust.
        if (op == "!=") return builder_.create<mlir::arith::CmpFOp>(loc_, mlir::arith::CmpFPredicate::UNE, lhs, rhs);
        if (op == "<")  return builder_.create<mlir::arith::CmpFOp>(loc_, mlir::arith::CmpFPredicate::OLT, lhs, rhs);
        if (op == ">")  return builder_.create<mlir::arith::CmpFOp>(loc_, mlir::arith::CmpFPredicate::OGT, lhs, rhs);
        if (op == "<=") return builder_.create<mlir::arith::CmpFOp>(loc_, mlir::arith::CmpFPredicate::OLE, lhs, rhs);
        if (op == ">=") return builder_.create<mlir::arith::CmpFOp>(loc_, mlir::arith::CmpFPredicate::OGE, lhs, rhs);
    }
    // B-ex-01: runtime overflow trap on integer +/-/*. Replace silent-wrap
    // arith ops with LLVM with-overflow intrinsics + cond_br + llvm.intr.trap
    // (SIGILL on overflow). Intentional wrapping — hashing, modular arith,
    // parsers — must use the `wrapping_add`/`wrapping_sub`/`wrapping_mul`
    // intrinsic family which emits the silent arith op directly.
    if (op == "+" || op == "-" || op == "*") {
        bool is_unsigned = lhs_ty &&
            LogosType::is_unsigned_repr_kind(TypeRef(lhs_ty).kind());
        auto int_ty = mlir::dyn_cast<mlir::IntegerType>(lhs.getType());
        if (int_ty && overflow_checks_) {
            mlir::Type i1 = builder_.getI1Type();
            mlir::Type result_struct = mlir::LLVM::LLVMStructType::getLiteral(
                builder_.getContext(), {int_ty, i1});
            mlir::Operation* intr = nullptr;
            if (op == "+") intr = is_unsigned
                ? (mlir::Operation*)builder_.create<mlir::LLVM::UAddWithOverflowOp>(loc_, result_struct, lhs, rhs)
                : (mlir::Operation*)builder_.create<mlir::LLVM::SAddWithOverflowOp>(loc_, result_struct, lhs, rhs);
            else if (op == "-") intr = is_unsigned
                ? (mlir::Operation*)builder_.create<mlir::LLVM::USubWithOverflowOp>(loc_, result_struct, lhs, rhs)
                : (mlir::Operation*)builder_.create<mlir::LLVM::SSubWithOverflowOp>(loc_, result_struct, lhs, rhs);
            else intr = is_unsigned
                ? (mlir::Operation*)builder_.create<mlir::LLVM::UMulWithOverflowOp>(loc_, result_struct, lhs, rhs)
                : (mlir::Operation*)builder_.create<mlir::LLVM::SMulWithOverflowOp>(loc_, result_struct, lhs, rhs);
            mlir::Value result_v = builder_.create<mlir::LLVM::ExtractValueOp>(
                loc_, intr->getResult(0), llvm::ArrayRef<int64_t>{0});
            mlir::Value ovf_v = builder_.create<mlir::LLVM::ExtractValueOp>(
                loc_, intr->getResult(0), llvm::ArrayRef<int64_t>{1});
            auto* parent_region = builder_.getInsertionBlock()->getParent();
            auto* trap_block = new mlir::Block();
            auto* cont_block = new mlir::Block();
            parent_region->getBlocks().push_back(trap_block);
            parent_region->getBlocks().push_back(cont_block);
            builder_.create<mlir::cf::CondBranchOp>(loc_, ovf_v, trap_block, cont_block);
            builder_.setInsertionPointToStart(trap_block);
            builder_.create<mlir::LLVM::Trap>(loc_);
            builder_.create<mlir::LLVM::UnreachableOp>(loc_);
            builder_.setInsertionPointToStart(cont_block);
            return result_v;
        }
        if (op == "+") return builder_.create<mlir::arith::AddIOp>(loc_, lhs, rhs);
        if (op == "-") return builder_.create<mlir::arith::SubIOp>(loc_, lhs, rhs);
        return builder_.create<mlir::arith::MulIOp>(loc_, lhs, rhs);
    }
    {
        bool is_unsigned = lhs_ty &&
            LogosType::is_unsigned_repr_kind(TypeRef(lhs_ty).kind());
        if (op == "/") {
            if (is_unsigned) return builder_.create<mlir::arith::DivUIOp>(loc_, lhs, rhs);
            return builder_.create<mlir::arith::DivSIOp>(loc_, lhs, rhs);
        }
        if (op == "%") {
            if (is_unsigned) return builder_.create<mlir::arith::RemUIOp>(loc_, lhs, rhs);
            return builder_.create<mlir::arith::RemSIOp>(loc_, lhs, rhs);
        }
    }
    if (op == "&&") return builder_.create<mlir::arith::AndIOp>(loc_, lhs, rhs);
    if (op == "||") return builder_.create<mlir::arith::OrIOp> (loc_, lhs, rhs);
    if (op == "&")  return builder_.create<mlir::arith::AndIOp>(loc_, lhs, rhs);
    if (op == "|")  return builder_.create<mlir::arith::OrIOp> (loc_, lhs, rhs);
    if (op == "^")  return builder_.create<mlir::arith::XOrIOp>(loc_, lhs, rhs);
    if (op == "<<") return builder_.create<mlir::arith::ShLIOp>(loc_, lhs, rhs);
    if (op == ">>") {
        auto it = mlir::dyn_cast<mlir::IntegerType>(lhs.getType());
        bool is_unsigned = it && lhs_ty &&
            LogosType::is_unsigned_repr_kind(TypeRef(lhs_ty).kind());
        if (is_unsigned)
            return builder_.create<mlir::arith::ShRUIOp>(loc_, lhs, rhs);
        return builder_.create<mlir::arith::ShRSIOp>(loc_, lhs, rhs);
    }
    // CP-cm-08: tuple == / != — emit per-field load + cmp, AND together.
    // Without this, Kind::Tuple lowers to ptr_type (the by-pointer ABI for
    // anonymous LLVM struct values), and the generic is_ptr_cmp branch
    // below compares the pointers to the tuple slots — false for any two
    // tuple values held in distinct memory even when contents match.
    // Limitation: primitive-only fields. Tuples with str / nested-tuple /
    // struct fields fall through to the historic pointer-cmp behaviour;
    // follow-up will widen.
    if ((op == "==" || op == "!=") &&
        lhs_ty && rhs_ty &&
        TypeRef(lhs_ty).kind() == LogosType::Kind::Tuple &&
        TypeRef(rhs_ty).kind() == LogosType::Kind::Tuple) {
        auto le = TypeRef(lhs_ty).tuple_elems();
        auto re = TypeRef(rhs_ty).tuple_elems();
        if (le.size() == re.size() && !le.empty()) {
            // Check every field is primitive (handle nested/str later).
            auto is_prim = [](TypeRef t) {
                if (!t) return false;
                using K = LogosType::Kind;
                switch (t.kind()) {
                case K::I8:  case K::I16: case K::I24: case K::I32:
                case K::I56: case K::I64: case K::I128:
                case K::U8:  case K::U16: case K::U24: case K::U32:
                case K::U56: case K::U64: case K::U128:
                case K::F32: case K::F64:
                case K::Bool: case K::Char:
                case K::Usize: case K::Isize:
                case K::IntLit: case K::FloatLit:
                    return true;
                default: return false;
                }
            };
            bool all_prim = true;
            for (auto e : le) if (!is_prim(e)) { all_prim = false; break; }
            for (auto e : re) if (!is_prim(e)) { all_prim = false; break; }
            if (all_prim) {
                mlir::Type struct_ty = tuple_llvm_type(lhs_ty);
                if (struct_ty) {
                    // A call-result tuple is an SSA struct VALUE (not the
                    // by-pointer convention); GEP needs a base pointer —
                    // spill first. Mirrors ETupleIndex's G144-6 spill.
                    mlir::Value lb = lhs, rb = rhs;
                    if (lb.getType() != ptr_type()) lb = spill_to_alloca(lb);
                    if (rb.getType() != ptr_type()) rb = spill_to_alloca(rb);
                    mlir::Value acc;
                    size_t idx = 0;
                    for (auto e : le) {
                        auto elem_t = logos_to_mlir(e);
                        if (!elem_t) { acc = nullptr; break; }
                        auto l_ptr = builder_.create<mlir::LLVM::GEPOp>(
                            loc_, ptr_type(), struct_ty, lb,
                            llvm::ArrayRef<mlir::LLVM::GEPArg>{
                                0, (int32_t)idx});
                        auto r_ptr = builder_.create<mlir::LLVM::GEPOp>(
                            loc_, ptr_type(), struct_ty, rb,
                            llvm::ArrayRef<mlir::LLVM::GEPArg>{
                                0, (int32_t)idx});
                        auto l_val = builder_.create<mlir::LLVM::LoadOp>(
                            loc_, elem_t, l_ptr);
                        auto r_val = builder_.create<mlir::LLVM::LoadOp>(
                            loc_, elem_t, r_ptr);
                        mlir::Value cmp;
                        if (mlir::isa<mlir::FloatType>(elem_t)) {
                            cmp = builder_.create<mlir::arith::CmpFOp>(
                                loc_, mlir::arith::CmpFPredicate::OEQ,
                                l_val, r_val);
                        } else {
                            cmp = builder_.create<mlir::arith::CmpIOp>(
                                loc_, mlir::arith::CmpIPredicate::eq,
                                l_val, r_val);
                        }
                        if (!acc) acc = cmp;
                        else acc = builder_.create<mlir::arith::AndIOp>(
                            loc_, acc, cmp);
                        ++idx;
                    }
                    if (acc) {
                        if (op == "==") return acc;
                        // != → XOR with true (i1 not).
                        auto true_c = builder_.create<mlir::arith::ConstantIntOp>(
                            loc_, 1LL, 1);
                        return builder_.create<mlir::arith::XOrIOp>(
                            loc_, acc, true_c);
                    }
                }
            }
        }
    }

    // Array `==` / `!=` — `[T; N]` VALUE equality: GEP each element, load,
    // compare, AND together. Without this, Kind::Array lowers to a pointer (the
    // by-pointer ABI for aggregates) and the generic ptr-cmp below compares the
    // two array SLOT addresses — false for any two distinct arrays even when
    // their contents match (silent-wrong on a basic op). Primitive element type
    // only, mirroring the tuple `==` fast-path; `[str;N]` / `[Struct;N]` /
    // nested-array elements fall through to the historic pointer-cmp (follow-up
    // will widen, same limitation the tuple path carries).
    if ((op == "==" || op == "!=") &&
        lhs_ty && rhs_ty &&
        TypeRef(lhs_ty).kind() == LogosType::Kind::Array &&
        TypeRef(rhs_ty).kind() == LogosType::Kind::Array &&
        TypeRef(lhs_ty).elem() && TypeRef(rhs_ty).elem()) {
        TypeRef et = TypeRef(lhs_ty).elem();
        uint64_t n  = (uint64_t)TypeRef(lhs_ty).arr_size();
        uint64_t rn = (uint64_t)TypeRef(rhs_ty).arr_size();
        auto is_prim = [](TypeRef t) {
            if (!t) return false;
            using K = LogosType::Kind;
            switch (t.kind()) {
            case K::I8:  case K::I16: case K::I24: case K::I32:
            case K::I56: case K::I64: case K::I128:
            case K::U8:  case K::U16: case K::U24: case K::U32:
            case K::U56: case K::U64: case K::U128:
            case K::F32: case K::F64: case K::Bool: case K::Char:
            case K::Usize: case K::Isize:
            case K::IntLit: case K::FloatLit: return true;
            default: return false;
            }
        };
        if (n == rn && n > 0 && is_prim(et)) {
            mlir::Type arr_ty  = logos_to_mlir(lhs_ty);   // LLVM [N x elem]
            auto       elem_t  = logos_to_mlir(et);
            if (arr_ty && elem_t) {
                mlir::Value lb = lhs, rb = rhs;
                if (lb.getType() != ptr_type()) lb = spill_to_alloca(lb);
                if (rb.getType() != ptr_type()) rb = spill_to_alloca(rb);
                mlir::Value acc;
                for (uint64_t i = 0; i < n; ++i) {
                    auto l_ptr = builder_.create<mlir::LLVM::GEPOp>(
                        loc_, ptr_type(), arr_ty, lb,
                        llvm::ArrayRef<mlir::LLVM::GEPArg>{0, (int32_t)i});
                    auto r_ptr = builder_.create<mlir::LLVM::GEPOp>(
                        loc_, ptr_type(), arr_ty, rb,
                        llvm::ArrayRef<mlir::LLVM::GEPArg>{0, (int32_t)i});
                    auto l_val = builder_.create<mlir::LLVM::LoadOp>(loc_, elem_t, l_ptr);
                    auto r_val = builder_.create<mlir::LLVM::LoadOp>(loc_, elem_t, r_ptr);
                    mlir::Value cmp;
                    if (mlir::isa<mlir::FloatType>(elem_t))
                        cmp = builder_.create<mlir::arith::CmpFOp>(
                            loc_, mlir::arith::CmpFPredicate::OEQ, l_val, r_val);
                    else
                        cmp = builder_.create<mlir::arith::CmpIOp>(
                            loc_, mlir::arith::CmpIPredicate::eq, l_val, r_val);
                    acc = !acc ? cmp
                               : builder_.create<mlir::arith::AndIOp>(loc_, acc, cmp).getResult();
                }
                if (acc) {
                    if (op == "==") return acc;
                    auto true_c = builder_.create<mlir::arith::ConstantIntOp>(loc_, 1LL, 1);
                    return builder_.create<mlir::arith::XOrIOp>(loc_, acc, true_c);
                }
            }
        }
    }

    // G144-5: tuple lexicographic ordering (`<` / `<=` / `>` / `>=`) on an
    // all-primitive tuple — emit per-element load + compare and fold right-to-
    // left (`lt_i || (eq_i && rest)`), mirroring the `==` fast-path above.
    // Without this the tuple lowers to a pointer and the generic path feeds the
    // slot pointer into arith.cmpi. `>`/`>=` are handled by swapping operands;
    // the seed is false for strict (`<`,`>`) and true for non-strict (`<=`,`>=`).
    if ((op == "<" || op == "<=" || op == ">" || op == ">=") &&
        lhs_ty && rhs_ty &&
        TypeRef(lhs_ty).kind() == LogosType::Kind::Tuple &&
        TypeRef(rhs_ty).kind() == LogosType::Kind::Tuple) {
        auto le = TypeRef(lhs_ty).tuple_elems();
        auto re = TypeRef(rhs_ty).tuple_elems();
        auto is_prim_ord = [](TypeRef t) {
            if (!t) return false;
            using K = LogosType::Kind;
            switch (t.kind()) {
            case K::I8:  case K::I16: case K::I24: case K::I32:
            case K::I56: case K::I64: case K::I128:
            case K::U8:  case K::U16: case K::U24: case K::U32:
            case K::U56: case K::U64: case K::U128:
            case K::F32: case K::F64: case K::Bool: case K::Char:
            case K::Usize: case K::Isize:
            case K::IntLit: case K::FloatLit: return true;
            default: return false;
            }
        };
        bool all_prim = le.size() == re.size() && !le.empty();
        if (all_prim) for (auto e : le) if (!is_prim_ord(e)) { all_prim = false; break; }
        if (all_prim) {
            mlir::Type struct_ty = tuple_llvm_type(lhs_ty);
            bool swap = (op == ">" || op == ">=");
            bool strict = (op == "<" || op == ">");
            mlir::Value lp = swap ? rhs : lhs;   // "less-than" operand order
            mlir::Value rp = swap ? lhs : rhs;
            auto is_unsigned = [](LogosType::Kind k) {
                return LogosType::is_unsigned_repr_kind(k);
            };
            if (struct_ty) {
                // By-value tuple operand (call result) → spill before GEP;
                // see the `==` fast-path above.
                if (lp.getType() != ptr_type()) lp = spill_to_alloca(lp);
                if (rp.getType() != ptr_type()) rp = spill_to_alloca(rp);
                // Seed: all-equal ⇒ strict false / non-strict true.
                mlir::Value acc = builder_.create<mlir::arith::ConstantIntOp>(
                    loc_, strict ? 0LL : 1LL, 1);
                for (int i = (int)le.size() - 1; i >= 0; --i) {
                    auto elem_t = logos_to_mlir(le[i]);
                    if (!elem_t) { acc = nullptr; break; }
                    auto gep = [&](mlir::Value base) {
                        auto p = builder_.create<mlir::LLVM::GEPOp>(
                            loc_, ptr_type(), struct_ty, base,
                            llvm::ArrayRef<mlir::LLVM::GEPArg>{0, (int32_t)i});
                        return builder_.create<mlir::LLVM::LoadOp>(loc_, elem_t, p).getResult();
                    };
                    auto lv = gep(lp);
                    auto rv = gep(rp);
                    mlir::Value lt_i, eq_i;
                    if (mlir::isa<mlir::FloatType>(elem_t)) {
                        lt_i = builder_.create<mlir::arith::CmpFOp>(loc_, mlir::arith::CmpFPredicate::OLT, lv, rv);
                        eq_i = builder_.create<mlir::arith::CmpFOp>(loc_, mlir::arith::CmpFPredicate::OEQ, lv, rv);
                    } else {
                        auto pred = is_unsigned(TypeRef(le[i]).kind())
                            ? mlir::arith::CmpIPredicate::ult : mlir::arith::CmpIPredicate::slt;
                        lt_i = builder_.create<mlir::arith::CmpIOp>(loc_, pred, lv, rv);
                        eq_i = builder_.create<mlir::arith::CmpIOp>(loc_, mlir::arith::CmpIPredicate::eq, lv, rv);
                    }
                    // acc = lt_i || (eq_i && acc)
                    auto eq_and = builder_.create<mlir::arith::AndIOp>(loc_, eq_i, acc);
                    acc = builder_.create<mlir::arith::OrIOp>(loc_, lt_i, eq_and);
                }
                if (acc) return acc;
            }
        }
    }

    // For pointer comparisons, use llvm.icmp instead of arith.cmpi
    bool is_ptr_cmp = mlir::isa<mlir::LLVM::LLVMPointerType>(lhs.getType());
    // Rust-style auto-deref at `==` / `!=` for &T / &mut T when both
    // operands point at a primitive scalar. Closes the "ref-int" gap:
    // previously `&i32 == &i32` did pointer-equality (two refs to
    // distinct stack slots holding 1 returned false). Now matches the
    // PartialEq-for-&T blanket: dereference both sides first.
    if (is_ptr_cmp && (op == "==" || op == "!=")) {
        auto is_ref_to_prim = [](TypeRef t) -> TypeRef {
            if (!t) return TypeRef{};
            auto k = t.kind();
            if (k != LogosType::Kind::Ref && k != LogosType::Kind::MutRef)
                return TypeRef{};
            TypeRef pe = t.pointee();
            if (!pe) return TypeRef{};
            using K = LogosType::Kind;
            switch (pe.kind()) {
            case K::I8:  case K::I16: case K::I24: case K::I32: case K::I56:
            case K::I64: case K::I128:
            case K::U8:  case K::U16: case K::U24: case K::U32: case K::U56:
            case K::U64: case K::U128:
            case K::F32: case K::F64:
            case K::Bool:
            case K::IntLit: case K::FloatLit:
                return pe;
            default: return TypeRef{};
            }
        };
        TypeRef lhs_pe = is_ref_to_prim(lhs_ty);
        TypeRef rhs_pe = is_ref_to_prim(rhs_ty);
        if (lhs_pe && rhs_pe) {
            auto elem_t = logos_to_mlir(lhs_pe);
            if (elem_t) {
                lhs = builder_.create<mlir::LLVM::LoadOp>(loc_, elem_t, lhs);
                rhs = builder_.create<mlir::LLVM::LoadOp>(loc_, elem_t, rhs);
                is_ptr_cmp = false;
                // Override lhs_ty / rhs_ty pointee handling for
                // downstream signedness checks: not needed here since
                // CmpIPredicate::eq/ne are sign-agnostic.
            }
        }
    }
    if (op == "==") {
        if (is_ptr_cmp)
            return builder_.create<mlir::LLVM::ICmpOp>(
                loc_, mlir::LLVM::ICmpPredicate::eq, lhs, rhs);
        return builder_.create<mlir::arith::CmpIOp>(loc_, mlir::arith::CmpIPredicate::eq,  lhs, rhs);
    }
    if (op == "!=") {
        if (is_ptr_cmp)
            return builder_.create<mlir::LLVM::ICmpOp>(
                loc_, mlir::LLVM::ICmpPredicate::ne, lhs, rhs);
        return builder_.create<mlir::arith::CmpIOp>(loc_, mlir::arith::CmpIPredicate::ne,  lhs, rhs);
    }
    {
        bool is_unsigned_cmp = lhs_ty &&
            LogosType::is_unsigned_repr_kind(TypeRef(lhs_ty).kind());
        if (op == "<")  return builder_.create<mlir::arith::CmpIOp>(loc_,
            is_unsigned_cmp ? mlir::arith::CmpIPredicate::ult : mlir::arith::CmpIPredicate::slt, lhs, rhs);
        if (op == ">")  return builder_.create<mlir::arith::CmpIOp>(loc_,
            is_unsigned_cmp ? mlir::arith::CmpIPredicate::ugt : mlir::arith::CmpIPredicate::sgt, lhs, rhs);
        if (op == "<=") return builder_.create<mlir::arith::CmpIOp>(loc_,
            is_unsigned_cmp ? mlir::arith::CmpIPredicate::ule : mlir::arith::CmpIPredicate::sle, lhs, rhs);
        if (op == ">=") return builder_.create<mlir::arith::CmpIOp>(loc_,
            is_unsigned_cmp ? mlir::arith::CmpIPredicate::uge : mlir::arith::CmpIPredicate::sge, lhs, rhs);
    }
    std::fprintf(stderr, "mlir_gen: unknown op '%s'\n", op.c_str());
    return nullptr;
}

mlir::Value MLIRGenImpl::gen_expr_kind(lir_view::EUnaryView v, TypeRef) {
    if (!v.operand()) return nullptr;
    auto val = gen_expr(v.operand());
    if (!val) return nullptr;
    auto op = v.op();
    if (op == "-") {
        if (mlir::isa<mlir::FloatType>(val.getType()))
            return builder_.create<mlir::arith::NegFOp>(loc_, val);
        auto zero = builder_.create<mlir::arith::ConstantIntOp>(
            loc_, 0, mlir::cast<mlir::IntegerType>(val.getType()).getWidth());
        return builder_.create<mlir::arith::SubIOp>(loc_, zero, val);
    }
    if (op == "!") {
        auto itype = mlir::dyn_cast<mlir::IntegerType>(val.getType());
        if (!itype) {
            std::fprintf(stderr, "mlir_gen: unary '!' on non-integer type\n");
            return nullptr;
        }
        unsigned width = itype.getWidth();
        if (width == 1) {
            // bool: logical NOT via XOR with 1
            auto one = builder_.create<mlir::arith::ConstantIntOp>(loc_, 1, 1);
            return builder_.create<mlir::arith::XOrIOp>(loc_, val, one);
        } else {
            // integer: bitwise NOT via XOR with all-ones (-1)
            auto allones = builder_.create<mlir::arith::ConstantIntOp>(loc_, -1, width);
            return builder_.create<mlir::arith::XOrIOp>(loc_, val, allones);
        }
    }
    std::fprintf(stderr, "mlir_gen: unknown unary op '%.*s'\n",
                 int(op.size()), op.data());
    return nullptr;
}

// ---------------------------------------------------------------------------
// AddrOf / Deref
// ---------------------------------------------------------------------------

mlir::Type MLIRGenImpl::place_slot_type(TypeRef t) {
    if (!t) return builder_.getI32Type();
    auto k = TypeRef(t).kind();
    if (k == LogosType::Kind::Struct || k == LogosType::Kind::ZonedStruct) {
        auto sit = struct_types_.find(mlir_struct_key(t));
        if (sit != struct_types_.end()) return sit->second.llvm_type;
    }
    if (k == LogosType::Kind::Tuple) {
        if (auto tt = tuple_llvm_type(t)) return tt;
    }
    // Enum value-repr: a TAGGED enum element occupies its full inline
    // {disc,payload} footprint as a place slot (NOT a collapsed ptr) — so
    // `arr[i]` of `[Enum; N]` strides by the real enum size.
    if (k == LogosType::Kind::Enum) {
        if (auto* te = resolve_tagged_enum(std::string(TypeRef(t).enum_name()), t);
            te && te->llvm_type)
            return te->llvm_type;
    }
    // Fat-pointer elements are stored INLINE by value (mirrors the struct-field
    // convention): a bare `&dyn`/`*dyn`/`dyn` (TraitObject) slot is the 16-byte
    // {data,vtable} pair, a closure the 16-byte {fn,env}, a slice the 16-byte
    // {ptr,len}, a custom-DST ref (`&Wrap<[u8]>`) the 16-byte {ptr,len}.
    // logos_to_mlir collapses all of these to an 8-byte `ptr` (the by-pointer
    // value ABI), so as an array/Vec ELEMENT stride that would be half the real
    // footprint — adjacent elements would overlap. RefRepr (Phase 1): the slot
    // type IS the storage type of the reference's repr — a thin ptr stays 8B,
    // every fat kind is its 16B pair. (A self_describing DST is a thin Ptr here,
    // not a DstRef, so it correctly takes the 8B path.) This also fixes a latent
    // gap: a bare DstRef element previously fell through to the 8B ptr below.
    if (auto rk = ref_repr_of(t); rk != RefReprKind::NotARef)
        return repr_storage_type(rk);
    auto m = logos_to_mlir(t);
    return m ? m : builder_.getI32Type();
}

// G163-2: recursively compute the address of an lvalue place expression.
mlir::Value MLIRGenImpl::gen_lvalue_addr(lir_view::ExprRef e) {
    namespace ec = lir_schema::expr;
    if (!e) return nullptr;
    switch (e.kind()) {
    case ec::Code::VarRef: {
        std::string vn(lir_view::EVarRefView{e}.name());
        // Local pointer var (*mut/*const): scope_ holds an alloca(ptr) — the
        // place lives at the loaded pointer, so load it.
        auto lpit = var_local_ptrs_.find(vn);
        if (lpit != var_local_ptrs_.end()) {
            auto slot = get_subscript_ptr(vn);
            return builder_.create<mlir::LLVM::LoadOp>(loc_, ptr_type(), slot);
        }
        // Aggregate / scalar local or ref/ptr param: scope_ entry is the
        // storage address (arrays/structs/tuples) or the pointer value
        // (ref/ptr params) — either way it is the address to GEP from.
        return get_subscript_ptr(vn);
    }
    case ec::Code::Deref: {
        // `*op` — the pointer operand IS the place address.
        auto op = lir_view::EDerefView{e}.operand();
        return op ? gen_expr(op) : nullptr;
    }
    case ec::Code::FieldRead: {
        lir_view::EFieldReadView frv{e};
        if (!frv.receiver()) return nullptr;
        auto [struct_ptr, sname] = gen_recv_struct(frv.receiver());
        if (!struct_ptr || sname.empty()) return nullptr;
        auto sit = struct_types_.find(sname);
        if (sit == struct_types_.end()) return nullptr;
        return gep_field(struct_ptr, sit->second, std::string(frv.field()));
    }
    case ec::Code::TupleIndex: {
        lir_view::ETupleIndexView tv{e};
        if (!tv.receiver()) return nullptr;
        TypeRef recv_t = tv.receiver().type(pool_impl());
        if (recv_t && TypeRef(recv_t).pointee() &&
            TypeRef(recv_t).pointee().kind() == LogosType::Kind::Tuple &&
            (TypeRef(recv_t).kind() == LogosType::Kind::Ref ||
             TypeRef(recv_t).kind() == LogosType::Kind::MutRef ||
             TypeRef(recv_t).kind() == LogosType::Kind::Ptr))
            recv_t = TypeRef(recv_t).pointee();
        // Tuple base address: for a Deref/ref receiver it is the pointer value;
        // otherwise the receiver's own place address.
        mlir::Value tup_ptr = (tv.receiver().kind() == ec::Code::Deref)
            ? gen_expr(tv.receiver())
            : gen_lvalue_addr(tv.receiver());
        if (!tup_ptr) tup_ptr = gen_expr(tv.receiver());
        auto stype = tuple_llvm_type(recv_t);
        if (!tup_ptr || !stype) return nullptr;
        llvm::SmallVector<mlir::LLVM::GEPArg> idx{int32_t(0), int32_t(tv.index())};
        return builder_.create<mlir::LLVM::GEPOp>(loc_, ptr_type(), stype, tup_ptr, idx);
    }
    case ec::Code::SliceIndex: {
        // Slice element address `s[i]` — the place-write / `&mut s[i]` counterpart
        // of gen_expr_kind(ESliceIndexView). Computes the SAME element pointer the
        // read does (load data ptr from the fat descriptor's field 0, GEP by index
        // with the element stride) and returns it WITHOUT the final load — so reads
        // and writes address the identical slot (consistency by construction).
        lir_view::ESliceIndexView sv{e};
        if (!sv.slice() || !sv.index()) return nullptr;
        TypeRef index_ty = sv.index().type(pool_impl());
        auto slice = gen_expr(sv.slice());
        auto index = gen_expr(sv.index());
        if (!slice || !index) return nullptr;
        TypeRef elem_tr = e.type(pool_impl());
        auto stype = slice_llvm_type();
        llvm::SmallVector<mlir::LLVM::GEPArg> pi{int32_t(0), int32_t(0)};
        auto pp = builder_.create<mlir::LLVM::GEPOp>(loc_, ptr_type(), stype, slice, pi);
        auto data_ptr = builder_.create<mlir::LLVM::LoadOp>(loc_, ptr_type(), pp);
        bool idx_unsigned = index_ty &&
            LogosType::is_unsigned_repr_kind(TypeRef(index_ty).kind());
        mlir::Value gep_idx = (idx_unsigned && index.getType() != builder_.getI64Type())
            ? builder_.create<mlir::arith::ExtUIOp>(loc_, builder_.getI64Type(), index).getResult()
            : index;
        // Stride MUST match gen_expr_kind(ESliceIndexView) exactly: the concrete
        // aggregate LLVM type for inline-laid-out Struct/ZonedStruct elements,
        // else the element representation `logos_to_mlir(elem)` (ptr-sized for
        // tuples/dyn — the by-pointer ABI). Using a different stride here than the
        // read uses would make `&mut s[i]` / `s[i]=v` address a different slot.
        mlir::Type stride = place_slot_type(elem_tr);
        if (!stride) stride = builder_.getI32Type();
        llvm::SmallVector<mlir::LLVM::GEPArg> di{gep_idx};
        return builder_.create<mlir::LLVM::GEPOp>(loc_, ptr_type(), stride, data_ptr, di);
    }
    case ec::Code::IndexRead: {
        lir_view::EIndexReadView irv{e};
        auto recv     = irv.receiver();
        if (!irv.index()) return nullptr;
        TypeRef recv_t = recv.type(pool_impl());
        mlir::Value base = gen_lvalue_addr(recv);
        if (!base) return nullptr;
        // Element stride = the receiver's element type's slot type.
        TypeRef elem_t = recv_t ? TypeRef(recv_t).elem() : TypeRef(nullptr);
        mlir::Type stride = place_slot_type(elem_t);
        auto rk = recv_t ? TypeRef(recv_t).kind() : LogosType::Kind::Error;
        // Slice receiver: `base` is the fat {ptr,len} descriptor — load data ptr.
        if (rk == LogosType::Kind::Slice) {
            auto stype = slice_llvm_type();
            llvm::SmallVector<mlir::LLVM::GEPArg> pi{int32_t(0), int32_t(0)};
            auto pp = builder_.create<mlir::LLVM::GEPOp>(loc_, ptr_type(), stype, base, pi);
            base = builder_.create<mlir::LLVM::LoadOp>(loc_, ptr_type(), pp);
        } else if (rk == LogosType::Kind::Array) {
            // base is the array storage; stride is the element slot.
        } else if (recv.kind() == ec::Code::VarRef &&
                   (rk == LogosType::Kind::Ptr || rk == LogosType::Kind::MutRef ||
                    rk == LogosType::Kind::Ref) && TypeRef(recv_t).pointee()) {
            // Bare pointer/ref VARIABLE indexed (`p[i]`, p: *mut T / &mut [T;N]):
            // gen_lvalue_addr(VarRef) already yielded the pointer VALUE. Index it
            // by the element representation — SAME base+stride as the by-value read
            // path gen_expr_kind(EIndexReadView)'s VarRef Ptr/Ref cases (so `&p[i]`
            // and `p[i]` address the identical slot).
            TypeRef pe = TypeRef(recv_t).pointee();
            // `*mut [T;N]` / `&mut [T;N]`: pointee is the array → index its element.
            TypeRef et = (TypeRef(pe).kind() == LogosType::Kind::Array && TypeRef(pe).elem())
                             ? TypeRef(pe).elem() : pe;
            stride = place_slot_type(et);
            if (!stride) stride = builder_.getI32Type();
        } else if (recv.kind() == ec::Code::FieldRead &&
                   (rk == LogosType::Kind::Ptr || rk == LogosType::Kind::MutRef ||
                    rk == LogosType::Kind::Ref) && TypeRef(recv_t).pointee() &&
                   (TypeRef(recv_t).pointee().kind() == LogosType::Kind::TraitObject ||
                    TypeRef(recv_t).pointee().kind() == LogosType::Kind::Closure ||
                    TypeRef(recv_t).pointee().kind() == LogosType::Kind::Slice ||
                    TypeRef(recv_t).pointee().kind() == LogosType::Kind::Tuple)) {
            // Pointer FIELD of fat elements indexed (`self.ptr[i]`, ptr: *mut T,
            // T = &dyn/closure/slice = inline 16-byte fat pair). gen_lvalue_addr
            // gave the field's ADDRESS (slot holding the buffer ptr); LOAD it to
            // get the buffer base, then stride by the 16-byte fat slot. The
            // legacy EAddrOfTemp handler used an 8-byte (logos_to_mlir) stride
            // here → adjacent fat elements overlapped (Vec<&dyn> push corruption).
            base = builder_.create<mlir::LLVM::LoadOp>(loc_, ptr_type(), base);
            stride = place_slot_type(TypeRef(recv_t).pointee());
            if (!stride) stride = builder_.getI32Type();
        } else {
            // Other receiver shapes (e.g. a `*S` pointer FIELD index `s.ptr[i]`)
            // are handled by the EAddrOfTemp legacy handler — leave them.
            return nullptr;
        }
        auto idx = gen_expr(irv.index());
        if (!idx) return nullptr;
        TypeRef it = irv.index().type(pool_impl());
        bool uns = it && LogosType::is_unsigned_repr_kind(it.kind());
        if (uns && idx.getType() != builder_.getI64Type())
            idx = builder_.create<mlir::arith::ExtUIOp>(loc_, builder_.getI64Type(), idx);
        llvm::SmallVector<mlir::LLVM::GEPArg> gi{idx};
        return builder_.create<mlir::LLVM::GEPOp>(loc_, ptr_type(), stride, base, gi);
    }
    default:
        return nullptr;
    }
}

mlir::Value MLIRGenImpl::gen_expr_kind(lir_view::EAddrOfView v, TypeRef) {
    // Address-of: return the alloca pointer directly.
    std::string var_name{v.var_name()};
    auto it = scope_.find(var_name);
    if (it == scope_.end()) {
        // B98.2: module-level const — materialize a temporary stack slot
        // and store the const value, then return the slot address.
        auto* cv = resolve_const_(var_name);   // G156-1: cur-fn-package first
        if (cv) {
            auto val = gen_expr(cv->value());
            if (!val) {
                std::fprintf(stderr, "mlir_gen: & const '%s' eval failed\n", var_name.c_str());
                return nullptr;
            }
            auto alloca = create_entry_alloca(val.getType());
            builder_.create<mlir::LLVM::StoreOp>(loc_, val, alloca);
            return alloca;
        }
        std::fprintf(stderr, "mlir_gen: & undefined '%s'\n", var_name.c_str());
        return nullptr;
    }
    // Parameter whose SSA arg holds a VALUE — `&p` is the address of the param's
    // own storage, so spill the arg to an entry alloca and return that. ONE rule
    // for both shapes: the arg is a value iff its MLIR type is not `ptr` (scalar /
    // by-value fat — checked live here) OR the param is pointer-family (the arg
    // IS a pointer; ptr_family_param_). Replaces the former scalar / Ref / raw-
    // pointer special cases: a raw `*mut T` param used to fall through to
    // returning the pointer VALUE (`fn f(p:*mut T){ &p }` → garbage `*mut *mut T`
    // → segfault); a struct param (arg = object address) is neither non-ptr nor
    // pointer-family, so it returns the arg unchanged.
    if (it->second && (it->second.getType() != ptr_type() ||
                       ptr_family_param_.count(var_name))) {
        auto alloca = create_entry_alloca(it->second.getType());
        builder_.create<mlir::LLVM::StoreOp>(loc_, it->second, alloca);
        // Ref/MutRef: REBIND so reads and further `&p` share one slot —
        // write-through for `&&mut T` chains (closes B3-bg-03 / Sprint 6). Other
        // value args use address-of-a-copy (scope_ untouched).
        if (ref_param_names_.count(var_name)) {
            it->second = alloca;
            ref_param_names_.erase(var_name);
            ptr_family_param_.erase(var_name);
        }
        return alloca;
    }
    // Enum value-repr: `&o` for an enum local is ONE level — the inline
    // storage address itself (like `&Struct`). scope_ already holds that
    // address, so hand it back directly.
    if (it->second && var_tagged_enum_.count(var_name))
        return it->second;
    return it->second;
}

mlir::Value MLIRGenImpl::gen_expr_kind(lir_view::EAddrOfTempView v, TypeRef result_t) {
    namespace ec = lir_schema::expr;
    auto inner_ref = v.inner();
    if (!inner_ref) return nullptr;
    TypeRef inner_t = inner_ref.type(pool_impl());

    // G163-2: `&mut <place>` over a chained index / deref-tuple-index place —
    // `&mut a[i][j]`, `&mut (*p).0`, deep field+index mixes. The recursive
    // place-address helper computes the real element address; if it succeeds,
    // that IS the reference. Gated to is_mut (place writes) and to the
    // IndexRead/TupleIndex tops the per-shape handlers below don't fully
    // recurse on; FieldRead keeps its dedicated, widely-relied-on handler.
    // An IndexRead place uses gen_lvalue_addr for BOTH `&` and `&mut`: it
    // computes the real element address with the correct per-element stride
    // (place_slot_type), which is exactly the reference. This is essential for
    // a `&self` method on a struct-ARRAY element (`hs[i].method()`, auto-ref'd
    // immutable) — the legacy handler below used a ptr (8-byte) stride and
    // pointed `self` into the wrong slot for wider structs (cluster A / N2).
    // gen_lvalue_addr returns null for shapes it doesn't cover (e.g. a `*S`
    // pointer-field indexed), so we fall through to the legacy handler then.
    // TupleIndex stays gated to is_mut — the immutable `&x.N` value-copy path
    // is relied on by the variadic-tuple Eq/Debug recursion.
    if (inner_ref.kind() == ec::Code::IndexRead ||
        inner_ref.kind() == ec::Code::SliceIndex ||
        (v.is_mut() && inner_ref.kind() == ec::Code::TupleIndex)) {
        if (auto addr = gen_lvalue_addr(inner_ref))
            return addr;
    }

    // Reborrow / pointer-identity peephole: `&[mut] *r` ≡ r when r already
    // holds a Ref/MutRef/Ptr — load r's value (which IS the pointer = the
    // reference). The sema peephole used to do this directly; moving it here
    // preserves the explicit `AddrOfTemp(Deref(...))` shape in LIR so borrow-
    // check can distinguish a reborrow from a rebind for `&mut T`.
    if (inner_ref.kind() == ec::Code::Deref) {
        auto deref_op = lir_view::EDerefView{inner_ref}.operand();
        if (deref_op) {
            TypeRef dt = deref_op.type(pool_impl());
            if (dt && (TypeRef(dt).kind() == LogosType::Kind::Ptr ||
                       TypeRef(dt).kind() == LogosType::Kind::MutRef ||
                       TypeRef(dt).kind() == LogosType::Kind::Ref)) {
                if (deref_op) {
                    TypeRef op_ty = dt;
                    auto thin = gen_expr(deref_op);
                    // Reborrowing a fat zone-mut `&mut T` (`&*r` / `&mut *r`): if the
                    // RESULT is a thin reference (`&T` / `*T` — a read reborrow), peel
                    // the {data,zone} pair to its data half; if the result is itself a
                    // fat `&mut T`, keep the pair (the zone rides on).
                    if (ref_repr_of(op_ty) == RefReprKind::FatZoneMut &&
                        !(result_t && ref_repr_of(result_t) == RefReprKind::FatZoneMut))
                        return repr_data(RefReprKind::FatZoneMut, thin);
                    // `&*p` of a thin pointer to a #[self_describing] DST yields a
                    // THIN &DST (DstRef): the reference IS the header pointer (the
                    // tail length is recovered in-band via dst_len at each access),
                    // so there is no fat {data,len} pair to build — and crucially
                    // nothing stack-local to dangle when the `&Foo` is returned.
                    return thin;
                }
            }
        }
    }

    // Special case: &mut <field_read> on an inline struct field must return a
    // GEP into the original struct, NOT a copy.  gen_expr(EFieldRead) always
    // loads, which would give us a by-value copy — useless for mutation.
    if (inner_ref.kind() == ec::Code::FieldRead) {
        lir_view::EFieldReadView frv{inner_ref};
        if (frv.receiver()) {
            auto [ptr, sname] = gen_recv_struct(frv.receiver());
            if (ptr && !sname.empty()) {
                auto sit = struct_types_.find(sname);
                if (sit != struct_types_.end()) {
                    auto& info = sit->second;
                    auto gep = gep_field(ptr, info, std::string(frv.field()));
                    if (gep) return gep;
                }
            }
        }
        // Fall through.
    }
    // `&mut x.N` on a tuple element must return a GEP into the tuple, NOT a
    // copy. gen_expr(ETupleIndex) loads the element by value, so the default
    // addr_of_temp would take the address of a fresh temp holding the copy and
    // the write would never reach the tuple (mirrors the struct-field case
    // above). Gated to `&mut`: the immutable `&x.N` path is relied on to
    // produce a spilled value-copy by the variadic tuple Eq/Debug recursion
    // (which borrows nested-aggregate elements), so leave it untouched.
    if (v.is_mut() && inner_ref.kind() == ec::Code::TupleIndex) {
        lir_view::ETupleIndexView tv{inner_ref};
        if (tv.receiver()) {
            TypeRef recv_t = tv.receiver().type(pool_impl());
            // Auto-deref &(tuple)/&mut(tuple)/*tuple for the GEP base type.
            if (recv_t && TypeRef(recv_t).pointee() &&
                TypeRef(recv_t).pointee().kind() == LogosType::Kind::Tuple &&
                (TypeRef(recv_t).kind() == LogosType::Kind::Ref ||
                 TypeRef(recv_t).kind() == LogosType::Kind::MutRef ||
                 TypeRef(recv_t).kind() == LogosType::Kind::Ptr))
                recv_t = TypeRef(recv_t).pointee();
            if (recv_t && TypeRef(recv_t).kind() == LogosType::Kind::Tuple) {
                auto stype = tuple_llvm_type(recv_t);
                auto recv = gen_expr(tv.receiver());  // pointer to the tuple
                if (stype && recv) {
                    llvm::SmallVector<mlir::LLVM::GEPArg> idx{int32_t(0), int32_t(tv.index())};
                    return builder_.create<mlir::LLVM::GEPOp>(
                        loc_, ptr_type(), stype, recv, idx);
                }
            }
        }
        // Fall through.
    }
    // `&mut arr[i]` on a struct-element-typed array/pointer: take GEP address
    // directly instead of loading the struct by value and then needing to re-spill.
    if (inner_ref.kind() == ec::Code::IndexRead && inner_t) {
        lir_view::EIndexReadView irv{inner_ref};
        auto ir_recv  = irv.receiver();
        auto ir_index = irv.index();
        TypeRef ir_recv_t = ir_recv.type(pool_impl());
        mlir::Value base_ptr;
        mlir::Type  elem_type;
        if (ir_recv.kind() == ec::Code::VarRef) {
            std::string vn(lir_view::EVarRefView{ir_recv}.name());
            auto lpit = var_local_ptrs_.find(vn);
            if (lpit != var_local_ptrs_.end()) {
                auto slot = get_subscript_ptr(vn);
                base_ptr  = builder_.create<mlir::LLVM::LoadOp>(loc_, ptr_type(), slot);
                elem_type = lpit->second;
            } else if (ir_recv_t && ir_recv_t.kind() == LogosType::Kind::Ptr && ir_recv_t.pointee()) {
                auto cname = concrete_struct_name(ir_recv_t.pointee());
                auto sit   = struct_types_.find(cname);
                if (sit != struct_types_.end()) {
                    auto sc = scope_.find(vn);
                    if (sc != scope_.end()) {
                        base_ptr  = sc->second;
                        elem_type = sit->second.llvm_type;
                    }
                }
            } else if (ir_recv_t && ir_recv_t.kind() == LogosType::Kind::Array) {
                auto sp = get_subscript_ptr(vn);
                if (sp) {
                    base_ptr  = sp;
                    elem_type = logos_to_mlir(inner_t);
                }
            }
        } else if (ir_recv.kind() == ec::Code::FieldRead) {
            lir_view::EFieldReadView frv{ir_recv};
            if (frv.receiver()) {
                auto [struct_ptr, sname] = gen_recv_struct(frv.receiver());
                if (struct_ptr && !sname.empty()) {
                    auto& info = struct_types_[sname];
                    auto field_ptr = gep_field(struct_ptr, info, std::string(frv.field()));
                    if (field_ptr) {
                        bool field_is_ptr = ir_recv_t && ir_recv_t.kind() == LogosType::Kind::Ptr;
                        if (field_is_ptr) {
                            base_ptr = builder_.create<mlir::LLVM::LoadOp>(
                                loc_, ptr_type(), field_ptr);
                            TypeRef rpt = ir_recv_t.pointee();
                            if (rpt &&
                                (rpt.kind() == LogosType::Kind::Struct ||
                                 rpt.kind() == LogosType::Kind::ZonedStruct)) {
                                auto cname = concrete_struct_name(rpt);
                                auto sit2  = struct_types_.find(cname);
                                if (sit2 != struct_types_.end())
                                    elem_type = sit2->second.llvm_type;
                            }
                            // Enum value-repr: a `*mut Enum` field (e.g. a
                            // `Vec<Enum>`'s buffer `self.ptr`) strides by the
                            // inline {disc,payload} footprint, not a ptr — so
                            // `self.ptr[i] = val` writes the right element slot.
                            if (!elem_type && rpt && rpt.kind() == LogosType::Kind::Enum) {
                                if (auto* te = resolve_tagged_enum(std::string(rpt.enum_name()), rpt);
                                    te && te->llvm_type)
                                    elem_type = te->llvm_type;
                            }
                            if (!elem_type)
                                elem_type = logos_to_mlir(inner_t);
                        } else {
                            base_ptr  = field_ptr;
                            elem_type = logos_to_mlir(inner_t);
                        }
                    }
                }
            }
        }
        if (base_ptr && elem_type) {
            if (!ir_index) return nullptr;
            auto idx = gen_expr(ir_index);
            if (!idx) return nullptr;
            TypeRef ir_idx_t = ir_index.type(pool_impl());
            bool idx_unsigned = ir_idx_t &&
                LogosType::is_unsigned_repr_kind(ir_idx_t.kind());
            if (idx_unsigned && idx.getType() != builder_.getI64Type())
                idx = builder_.create<mlir::arith::ExtUIOp>(
                    loc_, builder_.getI64Type(), idx);
            llvm::SmallVector<mlir::LLVM::GEPArg> indices{idx};
            return builder_.create<mlir::LLVM::GEPOp>(
                loc_, ptr_type(), elem_type, base_ptr, indices);
        }
    }
    // SL-sl-03: `o.take()` → autoref'd `(&mut o).take()` — for a tagged-enum
    // local we must hand back the real slot, not a spilled copy of the
    // pointer, so the callee's `*self = …` rebind reaches the caller's
    // binding. Restricted to vars that genuinely live in a slot
    // (var_tagged_enum_ptr_); the broader VarRef carve-out broke
    // ref-to-struct lets whose scope_ entry holds the ref value directly.
    if (inner_ref.kind() == ec::Code::VarRef &&
        inner_t && inner_t.kind() == LogosType::Kind::Enum) {
        std::string vn{lir_view::EVarRefView{inner_ref}.name()};
        auto sc = scope_.find(vn);
        if (sc != scope_.end() && sc->second &&
            sc->second.getType() == ptr_type() &&
            var_tagged_enum_.count(vn)) {
            // Enum value-repr: scope_ holds the inline storage address (one
            // level). `(&mut o).take()`'s callee does `*self = …` which now
            // memcpies into this same storage — so hand back the slot directly.
            return sc->second;
        }
    }
    // `&mut self` autoref on a scalar-primitive local (`let mut b: u8;
    // b.make_ascii_uppercase()`): the receiver is a VarRef to a slot-backed
    // local, and the method's `*self = …` rebind must reach that slot.
    // gen_expr(VarRef) loads the value and the scalar tail below spills a
    // fresh copy — so the write would land in the throwaway copy and never
    // reach the caller's binding. Hand back the variable's own slot instead
    // (same lvalue treatment EAddrOf gives a VarRef). Gated on the slot being
    // a real alloca (scope_ entry of ptr_type) — scalar params bound as SSA
    // values (type != ptr) keep the spill path, matching by-value semantics.
    if (inner_ref.kind() == ec::Code::VarRef && inner_t) {
        auto k = inner_t.kind();
        bool is_scalar =
            k == LogosType::Kind::I8   || k == LogosType::Kind::I16  ||
            k == LogosType::Kind::I24  || k == LogosType::Kind::I32  ||
            k == LogosType::Kind::I56  || k == LogosType::Kind::I64  ||
            k == LogosType::Kind::I128 || k == LogosType::Kind::U8   ||
            k == LogosType::Kind::U16  || k == LogosType::Kind::U24  ||
            k == LogosType::Kind::U32  || k == LogosType::Kind::U56  ||
            k == LogosType::Kind::U64  || k == LogosType::Kind::U128 ||
            k == LogosType::Kind::F32  || k == LogosType::Kind::F64  ||
            k == LogosType::Kind::Bool || k == LogosType::Kind::Char ||
            k == LogosType::Kind::Usize|| k == LogosType::Kind::Isize;
        if (is_scalar) {
            std::string vn{lir_view::EVarRefView{inner_ref}.name()};
            auto sc = scope_.find(vn);
            if (sc != scope_.end() && sc->second &&
                sc->second.getType() == ptr_type() && let_vars_.count(vn)) {
                return sc->second;
            }
        }
    }
    auto val = gen_expr(inner_ref);
    if (!val) return nullptr;
    if (inner_t && (inner_t.kind() == LogosType::Kind::Tuple ||
                    inner_t.kind() == LogosType::Kind::Struct ||
                    inner_t.kind() == LogosType::Kind::ZonedStruct ||
                    inner_t.kind() == LogosType::Kind::Array ||
                    // CP-cm-08b: Slice<T> values are already ptr-to-slice-desc
                    // (Logos ABI). Spilling them to an 8-byte alloca and
                    // passing the alloca address makes callees that expect
                    // "ptr to {ptr,i64}" read 16 bytes from an 8-byte slot.
                    // Treat as already-spilled — return the ptr value.
                    inner_t.kind() == LogosType::Kind::Slice ||
                    inner_t.kind() == LogosType::Kind::TraitObject))
        // T0-4 (temp lifetime extension): aggregates are normally
        // pointer-aliased so the value already IS the address — EXCEPT a
        // by-value aggregate (a fn-call return like `&String::from("x")`,
        // `&make_tuple()`), which must spill once to a stack slot; that
        // slot is the reference. spill_to_alloca is a no-op for values
        // that are already pointers, so this covers both cases.
        return spill_to_alloca(val);
    // Enum value-repr: `&<enum temp>` is ONE level — the inline storage address
    // (like `&Struct`), NOT a ptr-to-ptr. A constructed enum (EEnumLitData)
    // already returns its storage alloca ptr; return it directly. A by-value
    // aggregate (func return) spills once to a struct slot — that IS the address.
    if (inner_t && inner_t.kind() == LogosType::Kind::Enum) {
        if (val.getType() == ptr_type())
            return val;
        auto* te = resolve_tagged_enum(std::string(inner_t.enum_name()), inner_t);
        if (te) {
            auto struct_slot = create_entry_alloca(te->llvm_type);
            builder_.create<mlir::LLVM::StoreOp>(loc_, val, struct_slot);
            return struct_slot;
        }
        // C-like enum (i32 disc) — spill the scalar to a slot, address is `&Enum`.
        auto slot = create_entry_alloca(val.getType());
        builder_.create<mlir::LLVM::StoreOp>(loc_, val, slot);
        return slot;
    }
    auto llvm_type = logos_to_mlir(inner_t);
    if (!llvm_type) llvm_type = builder_.getI32Type();
    auto alloca = create_entry_alloca(llvm_type);
    builder_.create<mlir::LLVM::StoreOp>(loc_, val, alloca);
    return alloca;
}

bool MLIRGenImpl::deref_operand_is_ptr_to_dyn_handle(lir_view::ExprRef operand) {
    // UNIFORM FAT MODEL: every dyn value (`&dyn`/`*dyn`/`Box<dyn>`) is a 16-byte
    // {data,vtable} pair, and a `*const/*mut dyn` (Ptr<TraitObject>) always points
    // AT such a 16-byte slot. So `*p` is ALWAYS a no-op reinterpret — the pointer
    // to the 16-byte storage IS the `dyn` value (mirrors a struct/slice place).
    // The legacy 8-byte-handle "load the stored handle out of a container slot"
    // case (`HashMap::get -> *const Box<dyn>`) no longer exists: the slot holds
    // the inline fat pair, so the accessor's returned pointer already addresses
    // the 16-byte storage. Hence: never load here.
    (void)operand;
    return false;
}

mlir::Value MLIRGenImpl::gen_expr_kind(lir_view::EDerefView v, TypeRef type) {
    if (!v.operand()) return nullptr;
    auto ptr = gen_expr(v.operand());
    if (!ptr) return nullptr;
    // Structs/datatypes are always pointer-represented in MLIR/LLVM; the
    // logical *-deref just yields the same pointer.  Subsequent field
    // access or the return-by-value wrap handles the byte-level copy.
    // (Previously only Struct was covered here — Datatype fell through to
    // the load branch, producing a bogus double-load through pass-by-ptr
    // parameters: `*const V3` was treated as `ptr-to-ptr-to-V3`.)
    // `*p` over a `*const/*mut dyn Trait` (Ptr<TraitObject>): the result type is
    // TraitObject (the dyn handle). A `*const/*mut dyn` is a HANDLE by default
    // (the raw fat pointer), so `*p` is a no-op reinterpret — UNLESS p is a
    // genuine pointer-INTO-storage (a container accessor return, `HashMap::get →
    // *const Box<dyn>`), in which case `*p` must LOAD the stored handle. See
    // deref_operand_is_ptr_to_dyn_handle for the provenance discriminator.
    if (type && TypeRef(type).kind() == LogosType::Kind::TraitObject &&
        deref_operand_is_ptr_to_dyn_handle(v.operand())) {
        auto pointee = logos_to_mlir(type);
        if (!pointee) pointee = ptr_type();
        return builder_.create<mlir::LLVM::LoadOp>(loc_, pointee, ptr);
    }
    if (type && (TypeRef(type).kind() == LogosType::Kind::Struct ||
                 TypeRef(type).kind() == LogosType::Kind::ZonedStruct ||
                 // Trait objects are fat-pointer-represented; the dyn handle is
                 // pointer-to-fatslot, so `*p` for a `*const dyn T` HANDLE yields
                 // the same pointer (no load). (The container-accessor exception
                 // is handled above.)
                 TypeRef(type).kind() == LogosType::Kind::TraitObject ||
                 // C6-cc-08 follow-up: `*p` for `p: *const [T; N]` — the array
                 // value is too large to "load by value"; we keep it pointer-
                 // represented so subsequent `(*p)[i]` indexing GEPs into it.
                 TypeRef(type).kind() == LogosType::Kind::Array ||
                 // G163-1: tuples are pointer-represented too (alloca'd, passed
                 // by pointer, indexed via GEP — see gen_expr_kind ETupleLit /
                 // ETupleIndex). `*p` over a `&(T, U)` must yield the SAME
                 // pointer; the load branch read the first tuple field's bytes
                 // AS a pointer (a lost indirection level) → GEP through garbage
                 // → SIGSEGV in `let (a,b) = *p` / `(*p).0`.
                 TypeRef(type).kind() == LogosType::Kind::Tuple))
        return ptr;
    // F3: `*p` over a `*zoned Enum` (a zoned niche enum) MATERIALISES — the slot
    // holds the at-rest word (Ref arm self-relative, anchor = the slot `ptr`);
    // the value is a by-pointer enum with the Ref arm absolute. The `*zoned`
    // operand type is what disambiguates an at-rest slot from a value-form local
    // (both are `*Enum`); a plain `*Enum` falls through to the no-op below.
    if (TypeRef op_ty = v.operand() ? v.operand().type(pool_impl()) : TypeRef(nullptr);
        op_ty && TypeRef(op_ty).zoned_ptr() && zoned_niche_enum_info(type))
        return zoned_enum_materialize(ptr);
    // Enum value-repr: a TAGGED enum is pointer-to-inline-storage (like a
    // Struct), so `*p` over a `&Enum` yields the SAME pointer (no load) — the
    // storage address. (A C-like enum is an i32 value and DOES load below.)
    if (type && TypeRef(type).kind() == LogosType::Kind::Enum &&
        resolve_tagged_enum(std::string(TypeRef(type).enum_name()), type))
        return ptr;
    // FatSlice pointee — str / &[T]: the pointee's storage is its 16-byte
    // {data,len} pair and the slice VALUE convention is pointer-to-that-
    // storage, so `*p` yields the SAME pointer — exactly like the Struct/
    // Tuple/TraitObject branches above. Falling through to the 8-byte load
    // below read the DATA half of the pair and re-interpreted it as the
    // pair pointer → garbage {ptr,len} downstream. This was the fat-element
    // deref class: `*(v.borrow(i))` on a Vec<str>, and `v[i]` on any
    // Vec<str> via the Index-trait desugar `*v.index(i)`. Same convention
    // foundation as place_slot_type / the SliceIndex fat-element fix.
    //
    // Deliberately FatSlice-ONLY (a broader all-fat-kinds version regressed
    // 7 tests): a CLOSURE value is an 8-byte pointer-to-{fn,env} handle
    // (aggregate_member_layout convention) — `*p` over &Closure must LOAD
    // the handle, so FatClosure stays on the load path; TraitObject is
    // handled by its own branch above; RelOffset slots are 8B i64 offsets;
    // FatCustomDst/FatZoneMut deref is unexercised — left on the load path
    // until a repro says otherwise.
    if (type && ref_repr_of(type) == RefReprKind::FatSlice)
        return ptr;
    auto pointee = logos_to_mlir(type);
    if (!pointee) pointee = builder_.getI32Type();
    return builder_.create<mlir::LLVM::LoadOp>(loc_, pointee, ptr);
}

// ---------------------------------------------------------------------------
// Function calls
// ---------------------------------------------------------------------------

mlir::Value MLIRGenImpl::gen_expr_kind(lir_view::ECallView v, TypeRef ret_logos_type) {
    namespace ec = lir_schema::expr;
    std::string callee(v.callee());
    std::vector<lir_view::ExprRef> arg_refs;
    v.each_arg([&](lir_view::ExprRef ar){ arg_refs.push_back(ar); });
    std::vector<lir_view::ExprRef> arg_les;
    arg_les.reserve(arg_refs.size());
    for (auto& ar : arg_refs) {
        if (!ar) return nullptr;
        arg_les.push_back(ar);
    }
    auto parent_mod = builder_.getBlock()->getParent()->getParentOfType<mlir::ModuleOp>();

    // ── Compiler intrinsics recognised by name ────────────────────────────────
    // wrapping_add / wrapping_sub / wrapping_mul — silent two's-complement
    // arithmetic that explicitly opts out of the runtime overflow trap on
    // `+`/`-`/`*`. Same signature as the built-in op; emits the silent
    // arith.* directly (B-ex-01).
    // After pkg-mangling and monomorphization the callee may take the
    // form `pkg$<name>__g__<sig>` or `<name>__g__<sig>` (or just bare
    // `<name>` pre-mono). Strip pkg prefix before matching.
    auto is_wrapping_intr = [&](std::string_view name) {
        auto dollar = callee.rfind('$');
        std::string_view suffix = (dollar == std::string::npos)
            ? std::string_view{callee}
            : std::string_view{callee}.substr(dollar + 1);
        return suffix == name ||
               (suffix.size() > name.size() + 4 &&
                suffix.compare(0, name.size(), name) == 0 &&
                suffix.compare(name.size(), 4, "__g_") == 0);
    };
    if (is_wrapping_intr("wrapping_add") || is_wrapping_intr("wrapping_sub") || is_wrapping_intr("wrapping_mul")) {
        std::string_view base_op =
            is_wrapping_intr("wrapping_add") ? "wrapping_add" :
            is_wrapping_intr("wrapping_sub") ? "wrapping_sub" : "wrapping_mul";
        if (arg_les.size() == 2) {
            auto a = gen_expr(arg_les[0]); if (!a) return nullptr;
            auto b = gen_expr(arg_les[1]); if (!b) return nullptr;
            // Coerce types so the arith op sees matching integer widths.
            if (a.getType() != b.getType()) {
                if (auto ai = mlir::dyn_cast<mlir::IntegerType>(a.getType())) {
                    if (auto bi = mlir::dyn_cast<mlir::IntegerType>(b.getType())) {
                        if (ai.getWidth() < bi.getWidth())
                            a = builder_.create<mlir::arith::ExtUIOp>(loc_, b.getType(), a);
                        else if (bi.getWidth() < ai.getWidth())
                            b = builder_.create<mlir::arith::ExtUIOp>(loc_, a.getType(), b);
                    }
                }
            }
            if (base_op == "wrapping_add")
                return builder_.create<mlir::arith::AddIOp>(loc_, a, b);
            if (base_op == "wrapping_sub")
                return builder_.create<mlir::arith::SubIOp>(loc_, a, b);
            return builder_.create<mlir::arith::MulIOp>(loc_, a, b);
        }
    }

    // ── Trait-object reconstruction intrinsics ────────────────────────────────
    // Both survive mono unfolded and are emitted here (vtables live in mlir-gen).
    // Prefix match: mono mangles a generic call by appending `__<typearg>` to
    // the callee (e.g. `__vtable_of__` → `__vtable_of____Dog`), keeping the
    // type_args on the node. Match the bare prefix; read T from type_args.
    auto match_intr = [&](std::string_view name) {
        // The synthesized callee is `__vtable_of__` / `__dyn_from_parts__`,
        // which mono mangles by APPENDING `__<typearg>` — so the name is always
        // a prefix at position 0. Check that first. A package-qualified callee
        // (hypothetically `pkg.path$name…`) is split on the FIRST `$` (package
        // paths are dotted and never contain `$`); the LAST `$` would land in
        // the middle of the mangled type-arg suffix (`$G4$u64$…$hs_…`) and lose
        // the prefix — the bug that made deeply-generic T (LeafNode<…>) miss.
        if (std::string_view{callee}.rfind(name, 0) == 0) return true;
        auto dollar = callee.find('$');
        if (dollar == std::string::npos) return false;
        return std::string_view{callee}.substr(dollar + 1).rfind(name, 0) == 0;
    };
    auto strip_quotes = [](std::string_view s) {
        if (s.size() >= 2 && s.front() == '"' && s.back() == '"')
            return s.substr(1, s.size() - 2);
        return s;
    };
    // vtable_of::<Trait, T>() -> *const u8 : address of the static vtable for
    // `impl Trait for T`. arg[0] = trait name (lit_str); type_args[0] = concrete T.
    if (match_intr("__vtable_of__")) {
        std::string trait;
        if (!arg_refs.empty() && arg_refs[0].kind() == ec::Code::LitStr)
            trait = std::string(strip_quotes(lir_view::ELitStrView{arg_refs[0]}.value()));
        auto tas = v.type_args(pool_impl());
        if (trait.empty() || tas.empty() || !tas[0]) return nullptr;
        TypeRef T = tas[0];
        std::string vt_name =
            (T.kind() == LogosType::Kind::Struct ||
             T.kind() == LogosType::Kind::ZonedStruct)
                ? concrete_struct_name(T)
                : std::string(type_str(T));
        return build_inline_vtable(trait, vt_name, T);
    }
    // dyn_from_parts::<Trait>(data, vtable) -> *mut dyn Trait : assemble a fat
    // {data, vtable} pair from raw halves. arg[0]=trait (lit_str, unused at
    // codegen — layout is uniform), arg[1]=data ptr, arg[2]=vtable ptr.
    if (match_intr("__dyn_from_parts__")) {
        if (arg_les.size() != 3) return nullptr;
        auto data_v  = gen_expr(arg_les[1]); if (!data_v)  return nullptr;
        auto vtable_v = gen_expr(arg_les[2]); if (!vtable_v) return nullptr;
        auto dyn_struct = dyn_llvm_type();
        auto alloca = create_entry_alloca(dyn_struct);
        if (!alloca) return nullptr;
        llvm::SmallVector<mlir::LLVM::GEPArg> idx0{int32_t(0), int32_t(0)};
        auto dp = builder_.create<mlir::LLVM::GEPOp>(
            loc_, ptr_type(), dyn_struct, alloca, idx0);
        builder_.create<mlir::LLVM::StoreOp>(loc_, data_v, dp);
        llvm::SmallVector<mlir::LLVM::GEPArg> idx1{int32_t(0), int32_t(1)};
        auto vp = builder_.create<mlir::LLVM::GEPOp>(
            loc_, ptr_type(), dyn_struct, alloca, idx1);
        builder_.create<mlir::LLVM::StoreOp>(loc_, vtable_v, vp);
        return alloca;
    }
    // __dyn_data__(a: &dyn Trait) -> *mut u8 : extract the DATA half (field 0)
    // of a `&dyn` fat pair — the inverse of __dyn_from_parts__. The operand is
    // normalised to a pointer-to-16-byte-storage by the SAME navigation that
    // vtable dispatch uses (dyn_storage_ptr), so it is correct in every
    // context — including a GENERIC body, where the naive `a as *const u8`
    // user-level cast mis-extracts (baghunt-any-primitive-dyn-and-downcast).
    // The foundation of `downcast_ref` in logos.lang.any.
    if (match_intr("__dyn_data__")) {
        if (arg_les.empty()) return nullptr;
        auto storage = dyn_storage_ptr(arg_les[0]);
        if (!storage) return nullptr;
        auto dyn_struct = dyn_llvm_type();
        llvm::SmallVector<mlir::LLVM::GEPArg> idx0{int32_t(0), int32_t(0)};
        auto dp = builder_.create<mlir::LLVM::GEPOp>(
            loc_, ptr_type(), dyn_struct, storage, idx0);
        return builder_.create<mlir::LLVM::LoadOp>(loc_, ptr_type(), dp);
    }

    // After pkg-mangling, intrinsic names ship as
    // `std.lang.text$str_from_raw__f__pcst_u8__i64` etc. Strip pkg
    // prefix and the `__f__<sig>` / `__g__<sig>` suffix to recover
    // the bare name for matching against the known-intrinsic set.
    auto bare_intrinsic = [&]() -> std::string {
        auto dollar = callee.rfind('$');
        std::string_view body = (dollar == std::string::npos)
            ? std::string_view{callee}
            : std::string_view{callee}.substr(dollar + 1);
        if (auto p = body.find("__f__"); p != std::string::npos)
            return std::string(body.substr(0, p));
        if (auto p = body.find("__g__"); p != std::string::npos)
            return std::string(body.substr(0, p));
        return std::string(body);
    }();

    // logos-core 5.1: atomic intrinsics. Replace the assembly-stub call shape
    // (`logos_atomic_load32` / `_store32` / `_fetch_add32` / `_cas32` and the
    // 64-bit twins) with MLIR LLVM atomic ops carrying seq-cst ordering. On
    // x86 this generates the same machine code as the hand-written stubs in
    // `stdlib/rt/atomic_ops.S` (plain mov + LOCK-prefixed RMW), but the
    // ordering attribute carries through so non-x86 backends emit correct
    // dmb/lr-sc sequences. The stubs become dead code and link-time garbage-
    // collected; ABI-level ordering Send/Sync soundness lives in the MLIR op
    // rather than scattered in hand-written assembly.
    //
    // Note: the stdlib `_ordered` API variants (`load_ordered`,
    // `store_ordered`, etc.) currently route to the same primitives with the
    // Ordering arg discarded — the lowered MLIR op gets seq-cst regardless.
    // That's CONSERVATIVE (over-synchronizing): correctness preserved across
    // every Ordering value, performance leaves cycles on the table only for
    // Relaxed/Acquire/Release variants on weak-memory targets. Threading the
    // const Ordering value into a per-variant atomic-op attribute is a
    // separate follow-up (depends on Ordering enum const-evaluation).
    // §6.14: const-eval the Ordering arg when literal, else fall back
    // to seq_cst (conservative — always sound). The arg is an
    // ExprRef; if it's an EnumLit with enum_name="Ordering", read
    // its disc directly. The disc→AtomicOrdering map matches Rust's
    // std::sync::atomic::Ordering layout (Relaxed=0, Acquire=1,
    // Release=2, AcqRel=3, SeqCst=4).
    auto read_ordering_at = [&](size_t idx) -> mlir::LLVM::AtomicOrdering {
        if (idx >= arg_refs.size()) return mlir::LLVM::AtomicOrdering::seq_cst;
        auto er = arg_refs[idx];
        if (er.kind() == lir_schema::expr::Code::EnumLit) {
            auto ev = lir_view::EEnumLitView{er};
            if (ev.enum_name() == "Ordering") {
                switch (ev.disc()) {
                    case 0: return mlir::LLVM::AtomicOrdering::monotonic;
                    case 1: return mlir::LLVM::AtomicOrdering::acquire;
                    case 2: return mlir::LLVM::AtomicOrdering::release;
                    case 3: return mlir::LLVM::AtomicOrdering::acq_rel;
                    case 4: return mlir::LLVM::AtomicOrdering::seq_cst;
                    default: break;
                }
            }
        }
        return mlir::LLVM::AtomicOrdering::seq_cst;
    };
    auto emit_atomic_load = [&](mlir::Type res_type, unsigned align,
                                mlir::LLVM::AtomicOrdering ord =
                                    mlir::LLVM::AtomicOrdering::seq_cst,
                                size_t expected_args = 1) -> mlir::Value {
        if (arg_les.size() != expected_args) return nullptr;
        auto ptr_v = gen_expr(arg_les[0]); if (!ptr_v) return nullptr;
        return builder_.create<mlir::LLVM::LoadOp>(
            loc_, res_type, ptr_v, align, /*isVolatile=*/false,
            /*isNonTemporal=*/false, /*isInvariant=*/false,
            /*isInvariantGroup=*/false, ord);
    };
    auto emit_atomic_store = [&](unsigned align,
                                 mlir::LLVM::AtomicOrdering ord =
                                     mlir::LLVM::AtomicOrdering::seq_cst,
                                 size_t expected_args = 2) -> mlir::Value {
        if (arg_les.size() != expected_args) return nullptr;
        auto ptr_v = gen_expr(arg_les[0]); if (!ptr_v) return nullptr;
        auto val_v = gen_expr(arg_les[1]); if (!val_v) return nullptr;
        builder_.create<mlir::LLVM::StoreOp>(
            loc_, val_v, ptr_v, align, /*isVolatile=*/false,
            /*isNonTemporal=*/false, /*isInvariantGroup=*/false, ord);
        return builder_.create<mlir::arith::ConstantIntOp>(
            loc_, /*value=*/0, /*width=*/32);
    };
    // Is the Ordering arg at `idx` a compile-time literal? (read_ordering_at
    // can't distinguish a runtime value from a literal SeqCst — both seq_cst.)
    auto ord_is_literal = [&](size_t idx) -> bool {
        if (idx >= arg_refs.size()) return false;
        auto er = arg_refs[idx];
        return er.kind() == lir_schema::expr::Code::EnumLit &&
               lir_view::EEnumLitView{er}.enum_name() == "Ordering";
    };
    // T2-24: store with a RUNTIME Ordering (the `store_ordered` wrapper path,
    // where read_ordering_at would fall back to seq_cst). On x86-64 a seq_cst
    // store is `xchg` (a full barrier) while every weaker store is a plain
    // `mov` — the ONLY ordering that changes x86 store codegen. So branch on
    // the runtime ordering: SeqCst → seq_cst store, else → release store
    // (`release` soundly covers Relaxed too — a stronger ordering is always
    // sound). Loads / RMW / CAS are byte-identical across orderings on x86,
    // so they keep the plain seq_cst fallback (no branch worth its cost).
    auto emit_atomic_store_runtime =
        [&](unsigned align, size_t ord_idx, size_t expected_args) -> mlir::Value {
        if (arg_les.size() != expected_args) return nullptr;
        auto ptr_v = gen_expr(arg_les[0]); if (!ptr_v) return nullptr;
        auto val_v = gen_expr(arg_les[1]); if (!val_v) return nullptr;
        auto ord_v = gen_expr(arg_les[ord_idx]); if (!ord_v) return nullptr;
        auto ord_ty = mlir::dyn_cast<mlir::IntegerType>(ord_v.getType());
        auto emit_one = [&](mlir::LLVM::AtomicOrdering o) {
            builder_.create<mlir::LLVM::StoreOp>(
                loc_, val_v, ptr_v, align, /*isVolatile=*/false,
                /*isNonTemporal=*/false, /*isInvariantGroup=*/false, o);
        };
        if (!ord_ty) {  // not an integer disc — keep the sound seq_cst store
            emit_one(mlir::LLVM::AtomicOrdering::seq_cst);
            return builder_.create<mlir::arith::ConstantIntOp>(loc_, 0, 32);
        }
        auto sc_disc = builder_.create<mlir::arith::ConstantIntOp>(
            loc_, /*SeqCst=*/4, ord_ty.getWidth());
        auto is_sc = builder_.create<mlir::arith::CmpIOp>(
            loc_, mlir::arith::CmpIPredicate::eq, ord_v, sc_disc);
        auto* region = builder_.getBlock()->getParent();
        auto* sc_block    = new mlir::Block();
        auto* rel_block   = new mlir::Block();
        auto* merge_block = new mlir::Block();
        region->push_back(sc_block);
        region->push_back(rel_block);
        region->push_back(merge_block);
        builder_.create<mlir::cf::CondBranchOp>(loc_, is_sc, sc_block, rel_block);
        builder_.setInsertionPointToStart(sc_block);
        emit_one(mlir::LLVM::AtomicOrdering::seq_cst);
        builder_.create<mlir::cf::BranchOp>(loc_, merge_block);
        builder_.setInsertionPointToStart(rel_block);
        emit_one(mlir::LLVM::AtomicOrdering::release);
        builder_.create<mlir::cf::BranchOp>(loc_, merge_block);
        builder_.setInsertionPointToStart(merge_block);
        return builder_.create<mlir::arith::ConstantIntOp>(loc_, 0, 32);
    };
    auto emit_atomic_fetch_add = [&](mlir::Type res_type,
                                     mlir::LLVM::AtomicOrdering ord =
                                         mlir::LLVM::AtomicOrdering::seq_cst,
                                     size_t expected_args = 2) -> mlir::Value {
        if (arg_les.size() != expected_args) return nullptr;
        auto ptr_v = gen_expr(arg_les[0]); if (!ptr_v) return nullptr;
        auto val_v = gen_expr(arg_les[1]); if (!val_v) return nullptr;
        return builder_.create<mlir::LLVM::AtomicRMWOp>(
            loc_, mlir::LLVM::AtomicBinOp::add, ptr_v, val_v, ord);
    };
    auto emit_atomic_cas = [&](mlir::Type val_type,
                               mlir::LLVM::AtomicOrdering succ =
                                   mlir::LLVM::AtomicOrdering::seq_cst,
                               mlir::LLVM::AtomicOrdering fail =
                                   mlir::LLVM::AtomicOrdering::seq_cst,
                               size_t expected_args = 3) -> mlir::Value {
        if (arg_les.size() != expected_args) return nullptr;
        auto ptr_v = gen_expr(arg_les[0]); if (!ptr_v) return nullptr;
        auto exp_v = gen_expr(arg_les[1]); if (!exp_v) return nullptr;
        auto des_v = gen_expr(arg_les[2]); if (!des_v) return nullptr;
        auto cmpxchg = builder_.create<mlir::LLVM::AtomicCmpXchgOp>(
            loc_, ptr_v, exp_v, des_v, succ, fail);
        return builder_.create<mlir::LLVM::ExtractValueOp>(
            loc_, cmpxchg, mlir::ArrayRef<int64_t>{1});
    };
    // §5 Wave 9 — generic atomic RMW emit helper for swap, fetch_or,
    // fetch_and, fetch_xor, fetch_sub. The LLVM dialect's AtomicBinOp
    // enum maps directly onto the operation kind; all RMW variants
    // share the same signature shape (ptr, val) → old_value.
    auto emit_atomic_rmw = [&](mlir::LLVM::AtomicBinOp op,
                               mlir::Type res_type,
                               mlir::LLVM::AtomicOrdering ord =
                                   mlir::LLVM::AtomicOrdering::seq_cst,
                               size_t expected_args = 2) -> mlir::Value {
        if (arg_les.size() != expected_args) return nullptr;
        auto ptr_v = gen_expr(arg_les[0]); if (!ptr_v) return nullptr;
        auto val_v = gen_expr(arg_les[1]); if (!val_v) return nullptr;
        return builder_.create<mlir::LLVM::AtomicRMWOp>(
            loc_, op, ptr_v, val_v, ord);
    };
    if (bare_intrinsic == "logos_atomic_load32")
        if (auto v = emit_atomic_load(builder_.getI32Type(), 4)) return v;
    if (bare_intrinsic == "logos_atomic_load64")
        if (auto v = emit_atomic_load(builder_.getI64Type(), 8)) return v;
    if (bare_intrinsic == "logos_atomic_store32")
        if (auto v = emit_atomic_store(4)) return v;
    if (bare_intrinsic == "logos_atomic_store64")
        if (auto v = emit_atomic_store(8)) return v;
    if (bare_intrinsic == "logos_atomic_fetch_add32")
        if (auto v = emit_atomic_fetch_add(builder_.getI32Type())) return v;
    if (bare_intrinsic == "logos_atomic_fetch_add64")
        if (auto v = emit_atomic_fetch_add(builder_.getI64Type())) return v;
    if (bare_intrinsic == "logos_atomic_cas32")
        if (auto v = emit_atomic_cas(builder_.getI32Type())) return v;
    if (bare_intrinsic == "logos_atomic_cas64")
        if (auto v = emit_atomic_cas(builder_.getI64Type())) return v;
    // §6.14 _ord variants — last arg is the Ordering enum value.
    if (bare_intrinsic == "logos_atomic_load32_ord")
        if (auto v = emit_atomic_load(builder_.getI32Type(), 4, read_ordering_at(1), 2)) return v;
    if (bare_intrinsic == "logos_atomic_load64_ord")
        if (auto v = emit_atomic_load(builder_.getI64Type(), 8, read_ordering_at(1), 2)) return v;
    if (bare_intrinsic == "logos_atomic_store32_ord")
        if (auto v = ord_is_literal(2) ? emit_atomic_store(4, read_ordering_at(2), 3)
                                       : emit_atomic_store_runtime(4, 2, 3)) return v;
    if (bare_intrinsic == "logos_atomic_store64_ord")
        if (auto v = ord_is_literal(2) ? emit_atomic_store(8, read_ordering_at(2), 3)
                                       : emit_atomic_store_runtime(8, 2, 3)) return v;
    if (bare_intrinsic == "logos_atomic_fetch_add32_ord")
        if (auto v = emit_atomic_fetch_add(builder_.getI32Type(), read_ordering_at(2), 3)) return v;
    if (bare_intrinsic == "logos_atomic_fetch_add64_ord")
        if (auto v = emit_atomic_fetch_add(builder_.getI64Type(), read_ordering_at(2), 3)) return v;
    if (bare_intrinsic == "logos_atomic_cas32_ord")
        if (auto v = emit_atomic_cas(builder_.getI32Type(),
                                     read_ordering_at(3), read_ordering_at(4), 5)) return v;
    if (bare_intrinsic == "logos_atomic_cas64_ord")
        if (auto v = emit_atomic_cas(builder_.getI64Type(),
                                     read_ordering_at(3), read_ordering_at(4), 5)) return v;
    // §5 Wave 9 — RMW variants beyond fetch_add: swap (xchg), fetch_or,
    // fetch_and, fetch_xor, fetch_sub. Each has a 32-bit and 64-bit
    // form plus matching _ord overload taking the Ordering as the last
    // argument. compare_exchange_weak shares the same shape as the
    // regular CAS at the MLIR level — the weak vs strong distinction
    // matters only on platforms that can spuriously fail the strong
    // form; LLVM models both via the same op and the codegen picks the
    // weaker variant when the instruction follows a retry loop.
    using ABinOp = mlir::LLVM::AtomicBinOp;
    auto i32 = builder_.getI32Type();
    auto i64 = builder_.getI64Type();
    if (bare_intrinsic == "logos_atomic_swap32")
        if (auto v = emit_atomic_rmw(ABinOp::xchg, i32)) return v;
    if (bare_intrinsic == "logos_atomic_swap64")
        if (auto v = emit_atomic_rmw(ABinOp::xchg, i64)) return v;
    if (bare_intrinsic == "logos_atomic_fetch_or32")
        if (auto v = emit_atomic_rmw(ABinOp::_or, i32)) return v;
    if (bare_intrinsic == "logos_atomic_fetch_or64")
        if (auto v = emit_atomic_rmw(ABinOp::_or, i64)) return v;
    if (bare_intrinsic == "logos_atomic_fetch_and32")
        if (auto v = emit_atomic_rmw(ABinOp::_and, i32)) return v;
    if (bare_intrinsic == "logos_atomic_fetch_and64")
        if (auto v = emit_atomic_rmw(ABinOp::_and, i64)) return v;
    if (bare_intrinsic == "logos_atomic_fetch_xor32")
        if (auto v = emit_atomic_rmw(ABinOp::_xor, i32)) return v;
    if (bare_intrinsic == "logos_atomic_fetch_xor64")
        if (auto v = emit_atomic_rmw(ABinOp::_xor, i64)) return v;
    if (bare_intrinsic == "logos_atomic_fetch_sub32")
        if (auto v = emit_atomic_rmw(ABinOp::sub, i32)) return v;
    if (bare_intrinsic == "logos_atomic_fetch_sub64")
        if (auto v = emit_atomic_rmw(ABinOp::sub, i64)) return v;
    if (bare_intrinsic == "logos_atomic_cas_weak32")
        if (auto v = emit_atomic_cas(i32)) return v;
    if (bare_intrinsic == "logos_atomic_cas_weak64")
        if (auto v = emit_atomic_cas(i64)) return v;
    // _ord variants — last arg is the Ordering enum value.
    if (bare_intrinsic == "logos_atomic_swap32_ord")
        if (auto v = emit_atomic_rmw(ABinOp::xchg, i32, read_ordering_at(2), 3)) return v;
    if (bare_intrinsic == "logos_atomic_swap64_ord")
        if (auto v = emit_atomic_rmw(ABinOp::xchg, i64, read_ordering_at(2), 3)) return v;
    if (bare_intrinsic == "logos_atomic_fetch_or32_ord")
        if (auto v = emit_atomic_rmw(ABinOp::_or, i32, read_ordering_at(2), 3)) return v;
    if (bare_intrinsic == "logos_atomic_fetch_or64_ord")
        if (auto v = emit_atomic_rmw(ABinOp::_or, i64, read_ordering_at(2), 3)) return v;
    if (bare_intrinsic == "logos_atomic_fetch_and32_ord")
        if (auto v = emit_atomic_rmw(ABinOp::_and, i32, read_ordering_at(2), 3)) return v;
    if (bare_intrinsic == "logos_atomic_fetch_and64_ord")
        if (auto v = emit_atomic_rmw(ABinOp::_and, i64, read_ordering_at(2), 3)) return v;
    if (bare_intrinsic == "logos_atomic_fetch_xor32_ord")
        if (auto v = emit_atomic_rmw(ABinOp::_xor, i32, read_ordering_at(2), 3)) return v;
    if (bare_intrinsic == "logos_atomic_fetch_xor64_ord")
        if (auto v = emit_atomic_rmw(ABinOp::_xor, i64, read_ordering_at(2), 3)) return v;
    if (bare_intrinsic == "logos_atomic_fetch_sub32_ord")
        if (auto v = emit_atomic_rmw(ABinOp::sub, i32, read_ordering_at(2), 3)) return v;
    if (bare_intrinsic == "logos_atomic_fetch_sub64_ord")
        if (auto v = emit_atomic_rmw(ABinOp::sub, i64, read_ordering_at(2), 3)) return v;
    if (bare_intrinsic == "logos_atomic_cas_weak32_ord")
        if (auto v = emit_atomic_cas(i32, read_ordering_at(3), read_ordering_at(4), 5)) return v;
    if (bare_intrinsic == "logos_atomic_cas_weak64_ord")
        if (auto v = emit_atomic_cas(i64, read_ordering_at(3), read_ordering_at(4), 5)) return v;

    // str_from_raw(ptr: *const u8, len: i64) -> str
    // Constructs a str fat-pointer {ptr, len} on the stack, mirroring ELitStr.
    if (bare_intrinsic == "str__str_from_raw" || bare_intrinsic == "str_from_raw") {
        if (arg_les.size() == 2) {
            auto ptr_v = gen_expr(arg_les[0]); if (!ptr_v) return nullptr;
            auto len_v = gen_expr(arg_les[1]); if (!len_v) return nullptr;
            auto stype  = slice_llvm_type();
            auto alloca = create_entry_alloca(stype);
            llvm::SmallVector<mlir::LLVM::GEPArg> pi{int32_t(0), int32_t(0)};
            auto pp = builder_.create<mlir::LLVM::GEPOp>(loc_, ptr_type(), stype, alloca, pi);
            builder_.create<mlir::LLVM::StoreOp>(loc_, ptr_v, pp);
            llvm::SmallVector<mlir::LLVM::GEPArg> li{int32_t(0), int32_t(1)};
            auto lp = builder_.create<mlir::LLVM::GEPOp>(loc_, ptr_type(), stype, alloca, li);
            auto len_i64 = coerce_numeric(len_v, builder_.getIntegerType(64));
            builder_.create<mlir::LLVM::StoreOp>(loc_, len_i64, lp);
            return alloca;
        }
    }

    // Bitwise intrinsics on u64 — emit LLVM dialect ops.
    // popcount/ctlz/cttz return i64 from i64 input; truncate to i32 for u32 return.
    if (bare_intrinsic == "popcount_u64"        || bare_intrinsic == "leading_zeros_u64"  ||
        bare_intrinsic == "trailing_zeros_u64"  || bare_intrinsic == "bswap_u64"          ||
        bare_intrinsic == "bitreverse_u64") {
        if (arg_les.size() == 1) {
            auto v = gen_expr(arg_les[0]); if (!v) return nullptr;
            auto i64_ty = builder_.getIntegerType(64);
            auto i32_ty = builder_.getIntegerType(32);
            v = coerce_int(v, i64_ty);
            mlir::Value res;
            if (bare_intrinsic == "popcount_u64")
                res = builder_.create<mlir::LLVM::CtPopOp>(loc_, i64_ty, v);
            else if (bare_intrinsic == "leading_zeros_u64")
                res = builder_.create<mlir::LLVM::CountLeadingZerosOp>(
                    loc_, i64_ty, v, /*is_zero_poison=*/false);
            else if (bare_intrinsic == "trailing_zeros_u64")
                res = builder_.create<mlir::LLVM::CountTrailingZerosOp>(
                    loc_, i64_ty, v, /*is_zero_poison=*/false);
            else if (bare_intrinsic == "bswap_u64")
                res = builder_.create<mlir::LLVM::ByteSwapOp>(loc_, i64_ty, v);
            else // bitreverse_u64
                res = builder_.create<mlir::LLVM::BitReverseOp>(loc_, i64_ty, v);
            // popcount/ctlz/cttz: Logos return type is u32; truncate.
            if (bare_intrinsic == "popcount_u64"       ||
                bare_intrinsic == "leading_zeros_u64"  ||
                bare_intrinsic == "trailing_zeros_u64")
                res = coerce_int(res, i32_ty);
            return res;
        }
    }

    // pdep_u64 / pext_u64 — BMI2 parallel bit deposit/extract. ONE
    // caller-visible name; the TARGET picks the lowering (same discipline as
    // the sqrt→llvm.sqrt mapping below):
    //   target cpu has bmi2 → inline hardware intrinsic
    //                         (llvm.x86.bmi.pdep.64 / llvm.x86.bmi.pext.64)
    //   else                → call the runtime cpuid-dispatch fallback
    //                         logos_pdep_u64 / logos_pext_u64 (stdlib/rt/
    //                         bitops.c) — even "generic"-built binaries use
    //                         the hardware op on capable hosts.
    // target_has_bmi2_ is single-sourced with the TargetMachine cpu
    // (target_cpu_has_bmi2 in compile_pipeline.cpp), so ISel always accepts
    // what we emit here.
    if (bare_intrinsic == "pdep_u64" || bare_intrinsic == "pext_u64") {
        if (arg_les.size() == 2) {
            const bool is_pdep = bare_intrinsic == "pdep_u64";
            auto x = gen_expr(arg_les[0]);
            if (!x) return nullptr;
            auto m = gen_expr(arg_les[1]);
            if (!m) return nullptr;
            auto i64_ty = builder_.getIntegerType(64);
            x = coerce_int(x, i64_ty);
            m = coerce_int(m, i64_ty);
            if (target_has_bmi2_) {
                auto op = builder_.create<mlir::LLVM::CallIntrinsicOp>(
                    loc_, i64_ty,
                    builder_.getStringAttr(is_pdep ? "llvm.x86.bmi.pdep.64"
                                                   : "llvm.x86.bmi.pext.64"),
                    mlir::ValueRange{x, m});
                return op.getResults();
            }
            const char* rt_name = is_pdep ? "logos_pdep_u64" : "logos_pext_u64";
            if (declared_fn_names_.insert(rt_name).second) {
                auto fn_type = builder_.getFunctionType({i64_ty, i64_ty}, {i64_ty});
                auto fn = mlir::func::FuncOp::create(loc_, rt_name, fn_type);
                fn.setPrivate();
                parent_mod.push_back(fn);
                mark_funcs_dirty();
            }
            auto rt_fn = parent_mod.lookupSymbol<mlir::func::FuncOp>(rt_name);
            if (!rt_fn) return nullptr;
            auto call = builder_.create<mlir::func::CallOp>(
                loc_, rt_fn, mlir::ValueRange{x, m});
            return call.getResult(0);
        }
    }

    // Check if this is a vararg extern fn (declared as llvm.func)
    if (vararg_fns_.count(callee)) {
        auto callee_fn = parent_mod.lookupSymbol<mlir::LLVM::LLVMFuncOp>(callee);
        if (!callee_fn) {
            std::fprintf(stderr, "mlir_gen: undefined vararg function '%s'\n", callee.c_str());
            return nullptr;
        }
        llvm::SmallVector<mlir::Value> args;
        auto fn_type   = callee_fn.getFunctionType();
        auto fixed_inputs = fn_type.getParams();
        for (size_t i = 0; i < arg_les.size(); ++i) {
            auto v = gen_expr(arg_les[i]);
            if (!v) return nullptr;
            if (i < fixed_inputs.size()) v = coerce_numeric(v, fixed_inputs[i]);
            args.push_back(v);
        }
        mlir::Type ret_type = fn_type.getReturnType();
        bool is_void = mlir::isa<mlir::LLVM::LLVMVoidType>(ret_type);
        auto call = builder_.create<mlir::LLVM::CallOp>(
            loc_, fn_type, callee_fn.getName(), mlir::ValueRange(args));
        if (is_void) return nullptr;
        mlir::Value res = call.getResult();
        return res ? res : nullptr;
    }

    auto callee_fn  = find_func_op(parent_mod, callee);
    if (!callee_fn) {
        auto gpos = callee.find("__g__");
        if (gpos != std::string::npos)
            callee_fn = find_func_op(parent_mod, callee.substr(0, gpos));
        // Pkg-mangled generic call (`pkg$base__g__sig`) whose terminator
        // overload is registered under the bare name `base` (the first-
        // registered overload of a same-name pair stays bare when the
        // second one is generic — sema_collect's demote-rename only
        // fires for two non-generic overloads). Strip pkg + g-suffix
        // and retry.
        if (!callee_fn && gpos != std::string::npos) {
            auto dollar = callee.rfind('$', gpos);
            if (dollar != std::string::npos)
                callee_fn = find_func_op(parent_mod,
                    callee.substr(dollar + 1, gpos - dollar - 1));
        }
        // Variadic-shrinking fallback: a recursive call inside a
        // variadic-pack-expand template carries the literal generic
        // mangling `…__g__sig__H__T` that no concrete clone matches.
        // The actual terminator is a sibling **non-generic** overload
        // of the same base whose own symbol is mangled with `__f__sig`.
        // Strip `__g__sig` and search for any `<base>__f__…` defined.
        if (!callee_fn && gpos != std::string::npos) {
            std::string base_with_pkg = callee.substr(0, gpos);
            std::string fn_prefix_pkg = base_with_pkg + "__f__";
            callee_fn = find_fn_matching(parent_mod,
                [&](mlir::func::FuncOp fn) {
                    return fn.getName().starts_with(fn_prefix_pkg);
                });
            if (!callee_fn) {
                auto dollar = base_with_pkg.rfind('$');
                if (dollar != std::string::npos) {
                    std::string fn_prefix_bare =
                        base_with_pkg.substr(dollar + 1) + "__f__";
                    callee_fn = find_fn_matching(parent_mod,
                        [&](mlir::func::FuncOp fn) {
                            return fn.getName().starts_with(fn_prefix_bare);
                        });
                }
            }
        }
        // ── SEPARATOR CLASS, JOIN DIRECTION — ambiguous, and knowingly kept ──
        // These three fallbacks match a CANDIDATE against emitted function
        // names by composing `callee + "__…"`. That is the join half of the
        // class the split sites belong to: because `__` is legal inside an
        // identifier, `callee` "foo" is a prefix of every method of an owner
        // spelled "foo_", so a miss on `foo` can land on `foo_`'s function.
        // (A lint cannot flag this — recomposing from carried parts and
        // comparing is spelled identically and IS the sound pattern. What makes
        // a join probe safe is that the candidate cannot be a proper prefix of
        // another declared name, which is a fact about the registry, not the
        // text. See tests/logos/separator_split_lint.sh, which says so.)
        //
        // They survive because every exact, registry-anchored path is tried
        // FIRST and these run only after all of them missed — at which point
        // the alternative is not a correct answer but a hard failure. The
        // right end state is to delete them and let the miss reach the R2 sink;
        // that is a behaviour change on programs that today link by luck, so it
        // is its own arc, not a rider on this one. Until then this comment is
        // the classification the site was missing.
        if (!callee_fn) {
            std::string generic_prefix = callee + "__g__";
            std::string fn_prefix      = callee + "__f__";
            callee_fn = find_fn_matching(parent_mod,
                [&](mlir::func::FuncOp fn) {
                    llvm::StringRef n = fn.getName();
                    return n.starts_with(generic_prefix) ||
                           n.starts_with(fn_prefix);
                });
        }
        if (!callee_fn) {
            std::string contains_f = "." + callee + "__f__";
            std::string contains_g = "." + callee + "__g__";
            std::string ends_dot = "." + callee;
            callee_fn = find_fn_matching(parent_mod,
                [&](mlir::func::FuncOp fn) {
                    llvm::StringRef n = fn.getName();
                    return n.ends_with(ends_dot) ||
                           n.contains(contains_f) ||
                           n.contains(contains_g);
                });
        }
        if (!callee_fn) {
            std::string callee_prefix = callee + "__";
            callee_fn = find_fn_matching(parent_mod,
                [&](mlir::func::FuncOp fn) {
                    return fn.getName().starts_with(callee_prefix);
                });
        }
    }
    if (!callee_fn) {
        if (::getenv("LOGOS_TRACE_CALL_MISS")) {
            std::fprintf(stderr, "[call-miss] '%s' — near-name FuncOps:\n",
                         callee.c_str());
            // ── SEPARATOR CLASS: AMBIGUOUS, BUT BENIGN BY PURPOSE ────────
            // This fragment is never used to resolve, name or emit anything;
            // it only widens/narrows a set of candidate names PRINTED under an
            // env-gated trace. A truncated fragment prints a SUPERSET — the
            // harmless direction. Do NOT "fix" this into a narrower filter.
            std::string frag = callee.substr(callee.rfind('$') + 1);
            if (auto us = frag.find("__"); us != std::string::npos)
                frag = frag.substr(0, us);
            parent_mod.walk([&](mlir::func::FuncOp fn) {
                if (fn.getName().contains(frag))
                    std::fprintf(stderr, "  %s\n", fn.getName().str().c_str());
            });
        }
        llvm::SmallVector<mlir::Value> args;
        for (size_t i = 0; i < arg_les.size(); ++i) {
            auto v = gen_expr(arg_les[i]);
            if (!v) return nullptr;
            args.push_back(v);
        }
        llvm::SmallVector<mlir::Type> result_types;
        if (ret_logos_type) {
            auto ret_mlir = logos_to_mlir(ret_logos_type);
            if (ret_mlir)
                result_types.push_back(ret_mlir);
        }
        auto call = builder_.create<mlir::func::CallOp>(
            loc_, callee, result_types, mlir::ValueRange(args));
        return call.getNumResults() > 0 ? call.getResult(0) : nullptr;
    }
    llvm::SmallVector<mlir::Value> args;
    auto param_types = callee_fn.getFunctionType().getInputs();
    // Look up Logos-level param types for dyn coercion
    auto fpit = fn_param_types_.find(callee);
    for (size_t i = 0; i < arg_les.size(); ++i) {
        mlir::Value v;
        // When the callee expects a pointer and the arg is an EFieldRead of an
        // inline-embedded struct, pass the field's GEP directly instead of
        // load+spill. This ensures mutations (e.g. &mut self.inner) write back
        // to the original struct, not a disconnected alloca copy.
        if (i < param_types.size() && param_types[i] == ptr_type() &&
            arg_refs[i].kind() == ec::Code::FieldRead) {
            lir_view::EFieldReadView frv{arg_refs[i]};
            std::string fr_field(frv.field());
            if (frv.receiver()) {
                auto [base_ptr, base_sname] = gen_recv_struct(frv.receiver());
                if (base_ptr && !base_sname.empty()) {
                    auto bit = struct_types_.find(base_sname);
                    if (bit != struct_types_.end()) {
                        auto gep = gep_field(base_ptr, bit->second, fr_field);
                        if (gep) {
                            for (auto& f : bit->second.fields) {
                                if (f.name == fr_field &&
                                    mlir::isa<mlir::LLVM::LLVMStructType>(f.type)) {
                                    v = gep;
                                    goto arg_push;
                                }
                            }
                        }
                    }
                }
            }
        }
        v = gen_expr(arg_les[i]);
        if (!v) return nullptr;
    arg_push:
        // Coerce concrete struct → &dyn Trait if param expects it.
        // Box<T> is laid out as { *mut T } so the box value *is* the data pointer;
        // use T as the vtable key so the impl on T (not Box<T>) is looked up.
        if (fpit != fn_param_types_.end() && i < fpit->second.size()) {
            auto param_lt = fpit->second[i];
            auto arg_lt = arg_refs[i].type(pool_impl());
            // G167-7: peel a `Box<TraitObject>` formal (and arg) so a concrete
            // `Box<Square>` passed where a generic param resolved to
            // `Box<dyn Shape>` (e.g. `Vec<Box<dyn Shape>>::push`) is unsize-
            // coerced into a fat `{data, vtable}` handle. Without peeling, the
            // formal looked like `Box<TraitObject>` (Struct), the check below
            // (bare TraitObject only) missed it, and a THIN handle was stored →
            // garbage vtable on later dispatch (SIGSEGV).
            TypeRef ptl = param_lt, alt = arg_lt;
            auto unbox = [](TypeRef& t){
                if (is_stdlib_box(t) && TypeRef(t).type_args().size() == 1)
                    t = TypeRef(t).type_args()[0];
            };
            unbox(ptl); unbox(alt);
            if (ptl && TypeRef(ptl).kind() == LogosType::Kind::TraitObject &&
                alt && TypeRef(alt).kind() != LogosType::Kind::TraitObject) {
                param_lt = ptl;  // use peeled trait object for trait_name below
                TypeRef vt_type = arg_lt;
                // C6-cc-09: `&T` / `&mut T` over a struct → &dyn Trait. The
                // ref value is already a data pointer at the LLVM level; unwrap
                // the pointee to look up the impl on T (not &T).
                if (TypeRef(vt_type).kind() == LogosType::Kind::Ref ||
                    TypeRef(vt_type).kind() == LogosType::Kind::MutRef)
                    vt_type = TypeRef(vt_type).pointee();
                if (is_stdlib_box(vt_type) && TypeRef(vt_type).type_args().size() == 1)
                    vt_type = TypeRef(vt_type).type_args()[0];
                // If the source is a struct value (not a pointer) — applies to
                // bare-struct → &dyn (the original C6-cc-09 surface) — spill
                // so coerce_to_dyn has something to store as data ptr. Existing
                // Box-source path: the Box value is already a 1-field struct
                // whose payload is the data pointer, so the underlying LLVM
                // value flows verbatim; only spill when the source isn't a
                // ref/Box (i.e. genuine struct value).
                if (v.getType() != ptr_type() &&
                    TypeRef(arg_lt).kind() != LogosType::Kind::Ref &&
                    TypeRef(arg_lt).kind() != LogosType::Kind::MutRef &&
                    !is_stdlib_box(arg_lt))
                    v = spill_to_alloca(v);
                // Key the vtable on the mono-mangled concrete name
                // (`Gen$G1$i64`), not the angle-bracket `type_str` form
                // (`Gen<i64>`), which never matches the registry (→ null
                // vtable → SIGSEGV; same root as G158-10).
                std::string vt_name =
                    (TypeRef(vt_type).kind() == LogosType::Kind::Struct ||
                     TypeRef(vt_type).kind() == LogosType::Kind::ZonedStruct)
                        ? concrete_struct_name(vt_type)
                        : type_str(vt_type);
                // Value model: an owning Box<dyn> arg is an inline value fat
                // pair (heap=false), just like a borrowed &dyn — the callee
                // drops it by value (free data), no separate heap handle.
                // Pass the unwrapped concrete type (`vt_type`, e.g. A) so the
                // vtable's drop_in_place slot runs the concrete destructor; an
                // empty concrete_ty here emits an EMPTY drop glue (slot 0 = no-op)
                // → the callee's drop frees the box but LEAKS the struct's
                // droppable fields (String etc.). The explicit `as Box<dyn>`
                // cast path already threads the concrete type; this implicit
                // call-arg coercion must too.
                v = coerce_to_dyn(v, std::string(TypeRef(param_lt).trait_name()), vt_name,
                                  vt_type);
            }
        }
        if (i < param_types.size()) {
            // A by-value aggregate / tagged-enum param has ptr SSA repr
            // (logos_to_mlir → ptr; the value lives in storage). When the arg
            // arrives as a VALUE — a struct, or a niche-packed enum word (i64,
            // e.g. an WAny read straight from `*zoned` storage) — spill it to an
            // alloca and pass the pointer, matching the callee's ptr param.
            // (Previously only structs spilled; a scalar niche word fell to
            // coerce_numeric, which can't make a ptr → arg/param type mismatch.)
            bool param_tagged_enum =
                (fpit != fn_param_types_.end() && i < fpit->second.size() &&
                 fpit->second[i] &&
                 TypeRef(fpit->second[i]).kind() == LogosType::Kind::Enum &&
                 resolve_tagged_enum(
                     std::string(TypeRef(fpit->second[i]).enum_name()),
                     fpit->second[i]) != nullptr);
            if (v.getType() != param_types[i] &&
                param_types[i] == ptr_type() &&
                v.getType() != ptr_type() &&
                (mlir::isa<mlir::LLVM::LLVMStructType>(v.getType()) || param_tagged_enum))
                v = spill_to_alloca(v);
            else if (v.getType() != ptr_type())
                v = coerce_numeric(v, param_types[i], arg_refs[i].type(pool_impl()));
        }
        args.push_back(v);
    }
    // Lower well-known libm math externs to their LLVM intrinsic (sqrt → sqrtsd,
    // etc.). Two wins: no libcall overhead, and — crucially — an opaque call in
    // a loop body blocks LLVM's unroller/vectorizer (n-body's pairwise force loop
    // stays rolled with `call sqrt`, but fully unrolls once it's `llvm.intr.sqrt`,
    // ~1.9× fewer instructions). Gated on the exact extern name + a (f64)->f64
    // shape + the callee being declaration-only (an FFI extern, not a user fn of
    // the same name). Matches rustc (f64::sqrt → llvm.sqrt) and clang's
    // -fno-math-errno: the intrinsic drops libm's errno-on-domain-error, which
    // Logos's math wrappers do not rely on.
    if (args.size() == 1 && callee_fn.isExternal() &&
        mlir::isa<mlir::Float64Type>(args[0].getType()) &&
        callee_fn.getFunctionType().getNumResults() == 1 &&
        mlir::isa<mlir::Float64Type>(callee_fn.getFunctionType().getResult(0))) {
        mlir::Value a = args[0];
        mlir::Type t = a.getType();
        // Recognize either the raw libm extern (`extern fn sqrt`) OR the
        // logos.lang.math wrapper (`sqrt_f64` etc.) — the wrapper is an opaque
        // cross-archive `declare` at the call site, so matching the extern alone
        // would not help user code (the wrapper call still blocks unrolling).
        bool math_pkg = callee.find("logos.lang.math$") != std::string::npos;
        auto is = [&](const char* raw, const char* wrap) {
            return callee == raw ||
                   (math_pkg && callee.find(std::string("$") + wrap + "__") != std::string::npos);
        };
        if (is("sqrt",  "sqrt_f64"))  return builder_.create<mlir::LLVM::SqrtOp>(loc_, t, a);
        if (is("fabs",  "abs_f64"))   return builder_.create<mlir::LLVM::FAbsOp>(loc_, t, a);
        if (is("floor", "floor_f64")) return builder_.create<mlir::LLVM::FFloorOp>(loc_, t, a);
        if (is("ceil",  "ceil_f64"))  return builder_.create<mlir::LLVM::FCeilOp>(loc_, t, a);
        if (is("trunc", "trunc_f64")) return builder_.create<mlir::LLVM::FTruncOp>(loc_, t, a);
        if (is("round", "round_f64")) return builder_.create<mlir::LLVM::RoundOp>(loc_, t, a);
        if (callee == "rint")         return builder_.create<mlir::LLVM::RintOp>(loc_, t, a);
    }
    auto call = builder_.create<mlir::func::CallOp>(loc_, callee_fn, args);
    return call.getNumResults() > 0 ? call.getResult(0) : nullptr;
}

mlir::Value MLIRGenImpl::gen_expr_kind(lir_view::EMethodCallView v, TypeRef ret_logos_type) {
    std::string method(v.method());
    std::string tag_system(v.tag_system());
    std::string resolved_type(v.resolved_type());
    std::string resolved_symbol(v.resolved_symbol());
    int32_t     vtable_index = v.vtable_index();
    auto recv_ref = v.receiver();
    if (!recv_ref) return nullptr;
    TypeRef recv_t = recv_ref.type(pool_impl());

    std::vector<lir_view::ExprRef>    arg_refs;
    v.each_arg([&](lir_view::ExprRef ar){ arg_refs.push_back(ar); });
    std::vector<lir_view::ExprRef> arg_les;
    arg_les.reserve(arg_refs.size());
    for (auto& ar : arg_refs) {
        if (!ar) return nullptr;
        arg_les.push_back(ar);
    }

    if (method == "as_offset" && recv_t) {
        bool is_anyval =
            type_str(recv_t) == "AnyVal" ||
            ((recv_t.kind() == LogosType::Kind::Ptr ||
              recv_t.kind() == LogosType::Kind::Ref ||
              recv_t.kind() == LogosType::Kind::MutRef) &&
             recv_t.pointee() && type_str(recv_t.pointee()) == "AnyVal");
        if (is_anyval) {
            auto recv = gen_expr(recv_ref);
            if (!recv) return nullptr;
            if (recv.getType() == ptr_type())
                return builder_.create<mlir::LLVM::LoadOp>(loc_, builder_.getI32Type(), recv);
            return coerce_numeric(recv, builder_.getI32Type());
        }
    }
    // (Removed: the `__smartptr_dyn_clone__` handler. After the B3 stage-2b
    // flip, Rc<dyn>/Arc<dyn> are structs whose .clone()/clone_ref run the real
    // generic body — the repr-aware marker is no longer emitted by sema.)
    if (!tag_system.empty())
        return gen_tagged_dispatch(v, ret_logos_type);
    // G168-A (g6/g2): a `&dyn Trait` receiver is a TraitObject; a `&(dyn Trait)`
    // (`Ref<TraitObject>`) routes to the same vtable dispatch (gen_dyn_dispatch
    // loads the handle once).
    if (recv_t && vtable_index >= 0 &&
        (recv_t.kind() == LogosType::Kind::TraitObject ||
         ((recv_t.kind() == LogosType::Kind::Ref ||
           recv_t.kind() == LogosType::Kind::MutRef) && recv_t.pointee() &&
          TypeRef(recv_t.pointee()).kind() == LogosType::Kind::TraitObject)))
        return gen_dyn_dispatch(v, ret_logos_type);
    // Primitive receiver fast-path: when the receiver isn't a struct
    // but `resolved_symbol` is supplied, emit a direct func.call to
    // that symbol. The receiver gets auto-ref'd to match the impl's
    // `&self` shape. Used by sema-time chain expansion for variadic-
    // tuple impls (Phase 3 of `[[baghunt-variadic-tuple-impl]]`).
    if (!resolved_symbol.empty() && recv_t) {
        // ⚠ This gate is the COMPLEMENT of what `gen_recv_struct` can name,
        // written as a list — and a list drifts. `usize` / `isize` are DISTINCT
        // kinds from u64/i64 and were missing, so `(str, usize)`'s Debug chain
        // lost its `usize` element call and the enclosing `return` with it
        // (tests/logos/pass/deem_rel_col_hashable, silent, exit 0). Write the
        // predicate as the complement it actually is, so a NEW scalar kind is
        // covered the day it is added; and the fall-through below now REPORTS
        // rather than returning null, so a kind this still misses is a red
        // compile and not a vanished call.
        auto k = recv_t.kind();
        using K = LogosType::Kind;
        bool primitive_recv =
            !(k == K::Struct || k == K::ZonedStruct || k == K::TraitObject ||
              k == K::TaggedPtr || k == K::DstRef || k == K::Ref ||
              k == K::MutRef || k == K::Ptr || k == K::Enum ||
              k == K::TypeVar || k == K::AssocType || k == K::Error ||
              k == K::ImplTrait || k == K::ConstVar || k == K::CfgSlotType ||
              k == K::Never || k == K::Void);
        // A `str` receiver is `[u8]` — NOT a struct, so `gen_recv_struct`
        // below returns an empty type name and the whole method call lowers to
        // NOTHING, silently. Measured: `fmt_debug_to_string::<(i64,str,i64)>`
        // SIGSEGV'd, because the synthesized tuple-Debug chain's `str` element
        // call vanished and took the enclosing `return` with it — in the
        // SHIPPED stdlib archive, at compile exit 0. The gate here was a
        // hand-maintained LIST OF KINDS; the real condition is "the receiver is
        // not a struct and `resolved_symbol` names the exact callee".
        if (primitive_recv) {
            auto parent_mod = builder_.getBlock()->getParent()
                              ->getParentOfType<mlir::ModuleOp>();
            auto callee_fn = find_func_op(parent_mod, resolved_symbol);
            if (callee_fn) {
                auto recv_val = gen_expr(recv_ref);
                if (!recv_val) return nullptr;
                // The self-argument's SHAPE is read off the CALLEE's signature,
                // never assumed: `impl Debug for i64` takes `&self` as a
                // pointer (spill the scalar), `impl Debug for str` takes it as
                // the 16-byte fat slice BY VALUE (load it). Deriving this from
                // the callee is what keeps a new receiver kind from silently
                // producing a shape mismatch.
                auto fnty = callee_fn.getFunctionType();
                mlir::Type p0 = fnty.getNumInputs() ? fnty.getInput(0) : mlir::Type{};
                mlir::Value self_arg = recv_val;
                if (p0 && recv_val.getType() != p0) {
                    if (p0 == ptr_type()) {
                        // Auto-ref: spill scalar to alloca + pass alloca ptr.
                        // Mirrors the gen_expr_kind(EAddrOfTempView) scalar path.
                        auto recv_t_mlir = logos_to_mlir(recv_t);
                        if (!recv_t_mlir) recv_t_mlir = recv_val.getType();
                        auto recv_slot = create_entry_alloca(recv_t_mlir);
                        builder_.create<mlir::LLVM::StoreOp>(loc_, recv_val, recv_slot);
                        self_arg = recv_slot;
                    } else if (recv_val.getType() == ptr_type()) {
                        // The receiver is a pointer to its storage and the
                        // callee wants the value (fat slice / small aggregate).
                        self_arg = builder_.create<mlir::LLVM::LoadOp>(
                            loc_, p0, recv_val);
                    }
                }
                llvm::SmallVector<mlir::Value> all_args;
                all_args.push_back(self_arg);
                for (auto& le : arg_les) {
                    auto av = gen_expr(le);
                    if (!av) return nullptr;
                    all_args.push_back(av);
                }
                auto call = builder_.create<mlir::func::CallOp>(
                    loc_, callee_fn, all_args);
                if (call.getNumResults() > 0) return call.getResult(0);
                return nullptr;
            }
        }
    }
    // A fat zone-mut receiver: the method's `self: &mut T` is ITSELF fat, so pass
    // the full fat value (the inner ptr-to-{data,zone} pair), not the peeled data
    // half — gen_recv_struct(self) inside the callee re-peels it for field access.
    bool recv_is_fat_zone =
        recv_t && ref_repr_of(TypeRef(recv_t)) == RefReprKind::FatZoneMut;
    auto [ptr, tname] = recv_is_fat_zone ? gen_recv_struct_inner(recv_ref)
                                         : gen_recv_struct(recv_ref);
    if (!ptr || tname.empty()) return nullptr;
    if (strip_struct_pkg(tname) == "AnyVal" && ptr.getType() != ptr_type()) {
        auto slot = create_entry_alloca(builder_.getI32Type());
        builder_.create<mlir::LLVM::StoreOp>(loc_, coerce_numeric(ptr, builder_.getI32Type()), slot);
        ptr = slot;
    }
    // Method symbols are pkg-qualified at sema (`pkg.Concrete__method__f__sig`).
    // Build qualified callee from receiver tname's pkg prefix; fall back to
    // bare and to a global suffix scan when needed.
    std::string defining = resolved_type.empty()
                           ? std::string(strip_struct_pkg(tname))
                           : resolved_type;
    std::string tname_pkg;
    // Pkg may have inner dots; split at LAST dot.
    if (auto p = tname.rfind('.'); p != std::string::npos)
        tname_pkg = tname.substr(0, p);
    std::string bare_mangled = defining + "__" + method;
    auto mangled = tname_pkg.empty()
                   ? bare_mangled
                   : tname_pkg + "." + bare_mangled;

    auto callee_name = resolved_symbol.empty() ? mangled : resolved_symbol;
    auto parent_mod  = builder_.getBlock()->getParent()->getParentOfType<mlir::ModuleOp>();
    // find_func_op resolves the module-qualified link form internally (THE
    // chokepoint), so the EXACT overload binds before the signature-blind suffix
    // fallbacks below — no per-site qualification needed here.
    auto callee_fn   = find_func_op(parent_mod, callee_name);
    // O(1) via the base→first-FuncOp index (was an O(funcs) find_fn_matching
    // prefix scan, called up to 3× per unresolved method). first-wins matches
    // the old find_fn_matching (first module-order hit).
    auto walk_prefix = [&](const std::string& cn) -> mlir::func::FuncOp {
        ensure_ffo_canon_index(parent_mod);
        if (auto it = ffo_base_first_.find(cn); it != ffo_base_first_.end())
            return it->second;
        return {};
    };
    if (!callee_fn) callee_fn = walk_prefix(callee_name);
    if (!callee_fn && !resolved_symbol.empty()) {
        callee_name = mangled;
        callee_fn = find_func_op(parent_mod, callee_name);
        if (!callee_fn) callee_fn = walk_prefix(callee_name);
    }
    if (!callee_fn && mangled != bare_mangled) {
        callee_name = bare_mangled;
        callee_fn = find_func_op(parent_mod, callee_name);
        if (!callee_fn) callee_fn = walk_prefix(callee_name);
    }
    if (!callee_fn) {
        std::string suffix1 = "." + bare_mangled;
        std::string contains_f = "." + bare_mangled + "__f__";
        std::string contains_g = "." + bare_mangled + "__g__";
        callee_fn = find_fn_matching(parent_mod,
            [&](mlir::func::FuncOp fn) {
                llvm::StringRef n = fn.getName();
                return n.ends_with(suffix1) ||
                       n.contains(contains_f) ||
                       n.contains(contains_g);
            });
        if (callee_fn) callee_name = callee_fn.getName().str();
    }
    if (!callee_fn) {
        // Suppress noise from the known method-generic-trait-default
        // mono limitation (baghunt CP-cm-12 family): when a trait default
        // method calls another method-level generic method (e.g.
        // `Iterator::reduce` calling `.fold::<Item>(...)`), mono doesn't
        // always synthesize the call-site's `__g__<arg>` instance for
        // unused concrete instantiations. The unreached call site below
        // would refer to a missing FuncOp; since it's not actually
        // dispatched (the enclosing fn is itself a dead instantiation),
        // staying silent keeps build output readable. Set
        // LOGOS_MLIR_DEBUG_UNDEF=1 to re-enable for diagnosis.
        if (std::getenv("LOGOS_MLIR_DEBUG_UNDEF"))
            std::fprintf(stderr, "mlir_gen: method '%s' not found\n", callee_name.c_str());
        // Account for the miss even while staying quiet: the STATEMENT level
        // decides whether this was a dead instantiation (harmless) or a live
        // effect being dropped (a miscompile). See method_lower_misses_.
        ++method_lower_misses_;
        last_method_miss_ = callee_name;
        return nullptr;
    }
    llvm::SmallVector<mlir::Value> args;
    args.push_back(ptr);
    auto param_types = callee_fn.getFunctionType().getInputs();
    // G167-7: Logos-level param types of the (mono'd) callee, for concrete→dyn
    // coercion. Keyed by the mangled symbol; index 0 is `self`, so arg i maps
    // to param i+1.
    // Look up by the RESOLVED FuncOp name (carries the `__g__<arg-mangle>`
    // generic-instance suffix), not the un-suffixed callee_name base.
    auto m_fpit = fn_param_types_.find(callee_fn.getName().str());
    // A fat zone-mut receiver (`&mut T`) passed to a method whose `self` is THIN
    // (`&self`, a read method) must be peeled to its data half — the callee
    // expects a plain pointer, not the {data,zone} pair. A `&mut self` (fat)
    // method keeps the full pair. (self is param index 0.)
    if (recv_is_fat_zone && m_fpit != fn_param_types_.end() && !m_fpit->second.empty() &&
        ref_repr_of(m_fpit->second[0]) != RefReprKind::FatZoneMut)
        args[0] = repr_data(RefReprKind::FatZoneMut, args[0]);
    for (size_t i = 0; i < arg_les.size(); ++i) {
        auto val = gen_expr(arg_les[i]);
        if (!val) return nullptr;
        size_t pi = i + 1;
        // G167-7: a concrete `Box<T>` / `&T` / struct argument passed where the
        // (mono'd) param is a trait object — possibly a GENERIC param `T` later
        // bound to `Box<dyn Trait>` (e.g. `Vec<Box<dyn Shape>>::push`) — must be
        // unsize-coerced into a fat `{data, vtable}` handle. The method-call
        // path previously skipped this entirely (only the free-fn path coerced),
        // so a thin `Box<Square>` was stored and later dispatch read a garbage
        // vtable → SIGSEGV. Mirrors the free-fn coercion; also peels a
        // `Box<TraitObject>` formal to its inner TraitObject.
        if (m_fpit != fn_param_types_.end() && pi < m_fpit->second.size()) {
            auto param_lt = m_fpit->second[pi];
            auto arg_lt   = arg_refs[i].type(pool_impl());
            TypeRef ptl = param_lt;
            if (is_stdlib_box(ptl) && TypeRef(ptl).type_args().size() == 1)
                ptl = TypeRef(ptl).type_args()[0];
            TypeRef alt = arg_lt;
            if (is_stdlib_box(alt) && TypeRef(alt).type_args().size() == 1)
                alt = TypeRef(alt).type_args()[0];
            if (ptl && TypeRef(ptl).kind() == LogosType::Kind::TraitObject &&
                alt && TypeRef(alt).kind() != LogosType::Kind::TraitObject) {
                TypeRef vt_type = arg_lt;
                if (TypeRef(vt_type).kind() == LogosType::Kind::Ref ||
                    TypeRef(vt_type).kind() == LogosType::Kind::MutRef)
                    vt_type = TypeRef(vt_type).pointee();
                if (is_stdlib_box(vt_type) && TypeRef(vt_type).type_args().size() == 1)
                    vt_type = TypeRef(vt_type).type_args()[0];
                if (val.getType() != ptr_type() &&
                    TypeRef(arg_lt).kind() != LogosType::Kind::Ref &&
                    TypeRef(arg_lt).kind() != LogosType::Kind::MutRef &&
                    !is_stdlib_box(arg_lt))
                    val = spill_to_alloca(val);
                std::string vt_name =
                    (TypeRef(vt_type).kind() == LogosType::Kind::Struct ||
                     TypeRef(vt_type).kind() == LogosType::Kind::ZonedStruct)
                        ? concrete_struct_name(vt_type)
                        : type_str(vt_type);
                // Value model: owning Box<dyn> arg = inline value fat pair.
                if (auto fat = coerce_to_dyn(val, std::string(TypeRef(ptl).trait_name()),
                                             vt_name)) {
                    args.push_back(fat);
                    continue;
                }
            }
        }
        if (pi < param_types.size()) {
            // See gen_call: a by-value aggregate / tagged-enum param is ptr-repr;
            // spill a value arg (struct OR niche-enum word i64) to alloca so the
            // pointer matches the callee param (else a scalar niche word would
            // hit coerce_numeric, which can't make a ptr → type mismatch).
            bool param_tagged_enum =
                (m_fpit != fn_param_types_.end() && pi < m_fpit->second.size() &&
                 m_fpit->second[pi] &&
                 TypeRef(m_fpit->second[pi]).kind() == LogosType::Kind::Enum &&
                 resolve_tagged_enum(
                     std::string(TypeRef(m_fpit->second[pi]).enum_name()),
                     m_fpit->second[pi]) != nullptr);
            if (val.getType() != param_types[pi] &&
                param_types[pi] == ptr_type() &&
                val.getType() != ptr_type() &&
                (mlir::isa<mlir::LLVM::LLVMStructType>(val.getType()) || param_tagged_enum))
                val = spill_to_alloca(val);
            else
                val = coerce_numeric(val, param_types[pi], arg_refs[i].type(pool_impl()));
        }
        args.push_back(val);
    }
    auto call = builder_.create<mlir::func::CallOp>(loc_, callee_fn, args);
    return call.getNumResults() > 0 ? call.getResult(0) : nullptr;
}

// ---------------------------------------------------------------------------
// Field / index reads
// ---------------------------------------------------------------------------

mlir::Value MLIRGenImpl::gen_expr_kind(lir_view::EFieldReadView v, TypeRef type) {
    if (!v.receiver()) return nullptr;
    TypeRef recv_ty = v.receiver().type(pool_impl());
    std::string field{v.field()};
    if (TypeRef rt(recv_ty); field == "raw" && rt) {
        bool is_anyval = type_str(recv_ty) == "AnyVal";
        bool is_anyval_ptr = (rt.kind() == LogosType::Kind::Ptr ||
                              rt.kind() == LogosType::Kind::Ref ||
                              rt.kind() == LogosType::Kind::MutRef) &&
                             rt.pointee() &&
                             type_str(rt.pointee()) == "AnyVal";
        if (is_anyval || is_anyval_ptr) {
            auto recv = gen_expr(v.receiver());
            if (!recv) return nullptr;
            if (recv.getType() == ptr_type())
                return builder_.create<mlir::LLVM::LoadOp>(loc_, builder_.getI32Type(), recv);
            return coerce_numeric(recv, builder_.getI32Type());
        }
    }
    auto [ptr, sname] = gen_recv_struct(v.receiver());
    if (!ptr || sname.empty()) return nullptr;
    auto& info = struct_types_[sname];
    auto gep   = gep_field(ptr, info, field);
    if (!gep) {
        if (std::getenv("LOGOS_GEP_ABORT"))
            std::fprintf(stderr, "field-read: recv struct '%s' field '%s' unregistered\n",
                         sname.c_str(), std::string(field).c_str());
        return nullptr;
    }
    // A Slice/Closure/custom-DST-ref field is stored INLINE as a 16-byte fat
    // pair, but the value convention elsewhere is a POINTER to that storage —
    // return the field address rather than loading the pair by value (mirrors
    // the inline struct/enum element convention in EIndexRead). RefRepr (Phase
    // 3): the storage->compute conversion for the always-16B-fat subset is
    // repr_materialize (returns the slot today; a future zoned-ref field would
    // convert its self-relative offset to an absolute ptr right here). A Tuple
    // is aggregate-inline (same slot convention, not a ref). TraitObject is
    // EXCLUDED — a dyn field read carries a by-value 16B aggregate (the load
    // branch below), a context-dependent dyn convention kept off this path.
    if (TypeRef rt(type); rt) {
        auto rk = field_repr(sname, rt);   // zoned2: a thin ptr field reads self-relative
        if (rk == RefReprKind::FatSlice || rk == RefReprKind::FatClosure ||
            rk == RefReprKind::FatCustomDst || rk == RefReprKind::FatZoneMut ||
            rk == RefReprKind::RelOffset)
            return repr_materialize(rk, gep);  // RelOffset: slot + load_i64(slot)
        if (rt.kind() == LogosType::Kind::Tuple)
            return gep;
    }
    for (auto& f : info.fields)
        if (f.name == field)
            return builder_.create<mlir::LLVM::LoadOp>(loc_, f.type, gep);
    return nullptr;
}

mlir::Value MLIRGenImpl::gen_expr_kind(lir_view::EIndexReadView v, TypeRef type) {
    namespace ec = lir_schema::expr;
    auto recv_ref = v.receiver();
    auto idx_ref  = v.index();
    if (!recv_ref || !idx_ref) return nullptr;
    TypeRef recv_t = recv_ref.type(pool_impl());
    TypeRef idx_t  = idx_ref.type(pool_impl());

    mlir::Value arr_ptr;
    mlir::Type  elem_type;

    switch (recv_ref.kind()) {
    case ec::Code::VarRef: {
        std::string name(lir_view::EVarRefView{recv_ref}.name());
        // Ref/ptr-to-array param (`v: &[T; N]`): the var's SSA value IS the
        // pointer to the array (== &elem[0]), so index it directly with the
        // element stride — mirror the `(*v)[i]` (DEREF) path's `default:` case.
        // Without this the else-branch below uses subscript_elem_type(name),
        // which returns the whole-array slot type and loads the array.
        if (recv_t &&
            (recv_t.kind() == LogosType::Kind::Ref ||
             recv_t.kind() == LogosType::Kind::MutRef ||
             recv_t.kind() == LogosType::Kind::Ptr) &&
            recv_t.pointee() &&
            recv_t.pointee().kind() == LogosType::Kind::Array) {
            arr_ptr   = gen_expr(recv_ref);
            elem_type = logos_to_mlir(type);
            if (!elem_type) elem_type = builder_.getI32Type();
            break;
        }
        // Module constant carrying an array literal — re-materialise the
        // value as a fresh on-stack alloca, walk into it like a normal
        // local. The const's TypeRef is what drives elem_type.
        if (auto* cv = resolve_const_(name)) {   // G156-1: cur-fn-package first
            arr_ptr = gen_expr(cv->value());
            if (!arr_ptr) return nullptr;
            TypeRef ct = cv->type(pool_impl());
            if (ct && TypeRef(ct).elem()) {
                elem_type = logos_to_mlir(TypeRef(ct).elem());
            }
            break;
        }
        // `p[i]` over a `*const/*mut dyn Trait` (Ptr<TraitObject>): p holds a
        // pointer-INTO-storage (e.g. `HashMap::get → *const Box<dyn>`); each slot
        // is an 8-byte dyn handle. Stride by `ptr` and load the handle. (`p[0]`
        // is the index form of `*p`; mirrors the EDeref ptr-to-handle LOAD.)
        if (recv_t && recv_t.kind() == LogosType::Kind::Ptr && recv_t.pointee() &&
            recv_t.pointee().kind() == LogosType::Kind::TraitObject) {
            arr_ptr   = gen_expr(recv_ref);
            elem_type = ptr_type();
            break;
        }
        auto lpit = var_local_ptrs_.find(name);
        if (lpit != var_local_ptrs_.end()) {
            auto alloca = get_subscript_ptr(name);
            arr_ptr   = builder_.create<mlir::LLVM::LoadOp>(loc_, ptr_type(), alloca);
            elem_type = lpit->second;
        } else if (recv_t && recv_t.kind() == LogosType::Kind::Ptr &&
                   recv_t.pointee() &&
                   (recv_t.pointee().kind() == LogosType::Kind::Struct ||
                    recv_t.pointee().kind() == LogosType::Kind::ZonedStruct)) {
            auto cname = concrete_struct_name(recv_t.pointee());
            auto sit   = struct_types_.find(cname);
            if (sit != struct_types_.end()) {
                auto sc = scope_.find(name);
                if (sc != scope_.end()) {
                    arr_ptr   = sc->second;
                    elem_type = sit->second.llvm_type;
                }
            }
            if (!arr_ptr) {
                arr_ptr   = get_subscript_ptr(name);
                elem_type = subscript_elem_type(name);
            }
        } else {
            arr_ptr   = get_subscript_ptr(name);
            elem_type = subscript_elem_type(name);
        }
        break;
    }
    case ec::Code::IndexRead: {
        // Nested index `matrix[i][j]` (and deeper, `cube[i][j][k]`): the address
        // of the receiver place `matrix[i]` is computed RECURSIVELY by
        // gen_lvalue_addr (G163-2b); the outer GEP below then strides into it by
        // the result element type. (The old one-level form gen_expr'd a 2-deep
        // receiver, loading the inner array BY VALUE → bad GEP base → crash.)
        if (auto a = gen_lvalue_addr(recv_ref)) {
            arr_ptr   = a;
            elem_type = logos_to_mlir(type);
            if (!elem_type) elem_type = builder_.getI32Type();
        }
        break;
    }
    case ec::Code::FieldRead: {
        // Field index read: field may be an array or a pointer.
        lir_view::EFieldReadView frv{recv_ref};
        auto fr_recv = frv.receiver();
        std::string field(frv.field());
        if (fr_recv) {
            auto [struct_ptr, sname] = gen_recv_struct(fr_recv);
            if (struct_ptr && !sname.empty()) {
                auto& info = struct_types_[sname];
                auto field_ptr = gep_field(struct_ptr, info, field);
                if (field_ptr) {
                    elem_type = logos_to_mlir(type);
                    if (!elem_type) elem_type = builder_.getI32Type();
                    bool field_is_ptr = recv_t && recv_t.kind() == LogosType::Kind::Ptr;
                    if (field_is_ptr) {
                        arr_ptr = builder_.create<mlir::LLVM::LoadOp>(loc_, ptr_type(), field_ptr);
                        TypeRef rpt = recv_t.pointee();
                        if (rpt &&
                            (rpt.kind() == LogosType::Kind::Struct ||
                             rpt.kind() == LogosType::Kind::ZonedStruct)) {
                            auto cname = concrete_struct_name(rpt);
                            auto sit   = struct_types_.find(cname);
                            if (sit != struct_types_.end())
                                elem_type = sit->second.llvm_type;
                        }
                    } else {
                        arr_ptr = field_ptr;
                    }
                }
            }
        }
        if (!arr_ptr) {
            arr_ptr   = gen_expr(recv_ref);
            elem_type = logos_to_mlir(type);
            if (!elem_type) elem_type = builder_.getI32Type();
        }
        break;
    }
    default:
        arr_ptr   = gen_expr(recv_ref);
        elem_type = logos_to_mlir(type);
        if (!elem_type) elem_type = builder_.getI32Type();
        break;
    }

    auto idx = gen_expr(idx_ref);
    if (!idx || !arr_ptr) return nullptr;
    bool idx_unsigned = idx_t && LogosType::is_unsigned_repr_kind(idx_t.kind());
    if (idx_unsigned && idx.getType() != builder_.getI64Type())
        idx = builder_.create<mlir::arith::ExtUIOp>(loc_, builder_.getI64Type(), idx);
    // Cluster A: a struct-typed element of an ARRAY is stored INLINE
    // (sizeof(Struct) per slot — see gen_arr_lit / gen_struct_lit). The
    // FieldRead / IndexRead / default cases above set elem_type =
    // logos_to_mlir(type) = `ptr` for a struct result, which both mis-strides
    // the GEP (8 vs sizeof) and skips the inline-struct return branch below
    // → reading `h.items[i]` (struct element) loaded pointer-bits / SIGSEGV.
    // Use the inline struct slot type. Slices/Vec use a pointer element
    // convention, so restrict to array / field-array (non-slice) receivers.
    if (type && (TypeRef(type).kind() == LogosType::Kind::Struct ||
                 TypeRef(type).kind() == LogosType::Kind::ZonedStruct) &&
        !(recv_t && TypeRef(recv_t).kind() == LogosType::Kind::Slice)) {
        if (auto st = place_slot_type(type);
            st && mlir::isa<mlir::LLVM::LLVMStructType>(st))
            elem_type = st;
    }
    // Enum value-repr: a TAGGED enum element is stored INLINE (sizeof(Enum) per
    // slot — array, Vec buffer `*mut T`, etc). Stride by the inline {disc,payload}
    // footprint and return the slot address (value-repr = ptr-to-storage). This
    // is what makes `Vec<Enum>::get`'s `self.ptr[i]` and `[Enum;N][i]` correct.
    if (type && TypeRef(type).kind() == LogosType::Kind::Enum) {
        if (auto* te = resolve_tagged_enum(std::string(TypeRef(type).enum_name()), type);
            te && te->llvm_type)
            elem_type = te->llvm_type;
    }
    // Closure element: the {fn,env} 16-byte pair is stored INLINE (Box<Closure>
    // / `*mut Closure` slots). Stride by the pair footprint and return the slot
    // ADDRESS (closures are pointer-represented), not an 8-byte load of the fn
    // half. Mirrors the inline-struct / enum element convention.
    if (type && TypeRef(type).kind() == LogosType::Kind::Closure)
        elem_type = closure_llvm_type();
    // Slice element of an ARRAY (`[str; N]` / `[&[T]; N]`) is stored INLINE as a
    // 16-byte {ptr,len} fat pair (logos_to_mlir(Array)); stride by it and return
    // the slot ADDRESS (slice value = ptr-to-storage). A Slice/Vec RECEIVER uses
    // the pointer-element convention, so restrict to non-slice receivers.
    if (type && TypeRef(type).kind() == LogosType::Kind::Slice &&
        !(recv_t && TypeRef(recv_t).kind() == LogosType::Kind::Slice))
        elem_type = slice_llvm_type();
    // TraitObject element (`&dyn`/`*dyn`/`dyn`): the 16-byte {data,vtable} pair
    // is stored INLINE (uniform fat model — same as a slice/closure element).
    // Stride by the pair footprint and return the slot ADDRESS (a dyn value IS
    // a pointer to its fat pair), not an 8-byte load of the data half. This is
    // what makes `Vec<&dyn>::get`/`p[i]` over a `*mut T` buffer correct.
    if (type && TypeRef(type).kind() == LogosType::Kind::TraitObject)
        elem_type = dyn_llvm_type();
    // Tuple element is stored INLINE by value (Rust layout) in array/Vec buffers;
    // stride by the full tuple aggregate and return the slot ADDRESS (a tuple
    // value IS a pointer to its storage), not an 8-byte load. (`Vec<(i64,i64)>`.)
    if (type && TypeRef(type).kind() == LogosType::Kind::Tuple)
        if (auto tt = tuple_llvm_type(type)) elem_type = tt;
    llvm::SmallVector<mlir::LLVM::GEPArg> indices{idx};
    auto gep = builder_.create<mlir::LLVM::GEPOp>(
        loc_, ptr_type(), elem_type, arr_ptr, indices);
    // Inline struct slot: array element IS the struct (sizeof(Struct) per
    // slot). Downstream code consumes structs by pointer, so return the
    // slot address rather than loading the aggregate.
    if (elem_type && mlir::isa<mlir::LLVM::LLVMStructType>(elem_type))
        return gep;
    return builder_.create<mlir::LLVM::LoadOp>(loc_, elem_type, gep);
}

// ---------------------------------------------------------------------------
// Struct / array / tuple literals
// ---------------------------------------------------------------------------

mlir::Value MLIRGenImpl::gen_expr_kind(lir_view::EStructLitView v, TypeRef) {
    return gen_struct_lit(v);
}

mlir::Value MLIRGenImpl::gen_expr_kind(lir_view::EArrLitView v, TypeRef type) {
    mlir::Type elem_type = builder_.getI32Type();
    if (type) {
        // Derive slot type from the whole-array conversion so struct elements
        // get inline LLVM struct slots (sizeof(Struct) per slot).
        if (auto arr_t = mlir::dyn_cast_or_null<mlir::LLVM::LLVMArrayType>(
                logos_to_mlir(type))) {
            elem_type = arr_t.getElementType();
        } else if (TypeRef(type).elem()) {
            auto et = logos_to_mlir(TypeRef(type).elem());
            if (et) elem_type = et;
        }
    }
    return gen_arr_lit(v, elem_type, type ? TypeRef(type).elem() : TypeRef(nullptr));
}

mlir::Value MLIRGenImpl::gen_expr_kind(lir_view::ETupleLitView v, TypeRef type) {
    auto stype = tuple_llvm_type(type);
    if (!stype) return nullptr;
    // Allocate tuple on stack, store each element via GEP.
    auto alloca = create_entry_alloca(stype);
    uint32_t i = 0;
    bool ok = true;
    v.each_elem([&](lir_view::ExprRef er) {
        if (!ok) return;
        if (!er) { ok = false; return; }
        auto val = gen_expr(er);
        if (!val) { ok = false; return; }
        // The element slot type from the tuple layout. A Struct/ZonedStruct
        // element is embedded INLINE (tuple_llvm_type), so its slot is an
        // LLVMStructType, but gen_expr returns the struct BY POINTER. Load the
        // aggregate value and store it into the slot (a value copy) rather than
        // storing the 8-byte pointer into the inline slot — which under-filled
        // larger structs and forced ETupleIndex to compensate with a bogus
        // `load ptr` read (G156-2 / G154-4 prerequisite). Mirrors the inline
        // struct-FIELD path in gen_struct_lit.
        mlir::Type slot_ty;
        if (auto sst = mlir::dyn_cast<mlir::LLVM::LLVMStructType>(stype);
            sst && i < sst.getBody().size())
            slot_ty = sst.getBody()[i];
        if (slot_ty && (mlir::isa<mlir::LLVM::LLVMStructType>(slot_ty) ||
                        mlir::isa<mlir::LLVM::LLVMArrayType>(slot_ty)) &&
            val.getType() == ptr_type()) {
            // Inline aggregate element (struct OR array): gen_expr returns it BY
            // POINTER; load the aggregate value and store it into the inline slot
            // (storing the pointer would under-fill the slot — G158-4 nested).
            val = builder_.create<mlir::LLVM::LoadOp>(loc_, slot_ty, val);
        } else if (TypeRef(type).tuple_elems()[i]) {
            auto et = logos_to_mlir(TypeRef(type).tuple_elems()[i]);
            if (et) val = coerce_numeric(val, et);
        }
        llvm::SmallVector<mlir::LLVM::GEPArg> idx{int32_t(0), int32_t(i)};
        auto gep = builder_.create<mlir::LLVM::GEPOp>(loc_, ptr_type(), stype, alloca, idx);
        builder_.create<mlir::LLVM::StoreOp>(loc_, val, gep);
        ++i;
    });
    if (!ok) return nullptr;
    return alloca;
}

mlir::Value MLIRGenImpl::gen_expr_kind(lir_view::ETupleIndexView v, TypeRef type) {
    if (!v.receiver()) return nullptr;
    TypeRef recv_type = v.receiver().type(pool_impl());
    auto recv = gen_expr(v.receiver());
    if (!recv) return nullptr;
    // Auto-deref: if receiver is &(tuple) or &mut(tuple), use pointee for GEP type.
    // recv is already a pointer to the tuple (passed as ptr in calling convention).
    if (recv_type && TypeRef(recv_type).pointee() &&
        TypeRef(recv_type).pointee().kind() == LogosType::Kind::Tuple &&
        (TypeRef(recv_type).kind() == LogosType::Kind::Ref ||
         TypeRef(recv_type).kind() == LogosType::Kind::MutRef ||
         TypeRef(recv_type).kind() == LogosType::Kind::Ptr))
        recv_type = TypeRef(recv_type).pointee();
    auto stype = tuple_llvm_type(recv_type);
    if (!stype) return nullptr;
    auto elem_mlir = logos_to_mlir(type);
    if (!elem_mlir) return nullptr;
    // G144-6: a call returning a tuple by value yields an SSA struct aggregate
    // (not a pointer); GEP requires a pointer, so spill it to a stack slot
    // first — mirrors the struct-field-on-call-result path.
    if (recv.getType() != ptr_type())
        recv = spill_to_alloca(recv);
    llvm::SmallVector<mlir::LLVM::GEPArg> idx{int32_t(0), int32_t(v.index())};
    auto gep = builder_.create<mlir::LLVM::GEPOp>(loc_, ptr_type(), stype, recv, idx);
    // A Struct/ZonedStruct element is embedded INLINE (tuple_llvm_type) and a
    // struct value is represented BY its address (logos_to_mlir(Struct) == ptr).
    // Return the element GEP — the struct pointer — rather than `load`ing the
    // struct's first field as if the element were a scalar pointer (which fed
    // callees a bogus receiver → SIGSEGV when `t.0` was passed by value).
    // Now consistent with gen_tuple_lit's inline aggregate store. Mirrors
    // struct-field reads. Other kinds (scalars, enums-as-heap-ptr, nested
    // tuples stored as ptr) keep the load.
    if (TypeRef et(type); et && (et.kind() == LogosType::Kind::Struct ||
                                 et.kind() == LogosType::Kind::ZonedStruct))
        return gep;
    // Enum value-repr: a TAGGED enum tuple element is INLINE — its value IS the
    // GEP address (one level, like a Struct). Return it (no load).
    if (TypeRef et(type); et && et.kind() == LogosType::Kind::Enum &&
        resolve_tagged_enum(std::string(et.enum_name()), et))
        return gep;
    // Slice/Closure element is an INLINE 16-byte fat pair (tuple_llvm_type); its
    // value convention is the GEP address (pointer to {ptr,len}/{fn,env}), like
    // a Struct element — return the address, don't load the pair.
    if (TypeRef et(type); et && (et.kind() == LogosType::Kind::Slice ||
                                 et.kind() == LogosType::Kind::Closure ||
                                 et.kind() == LogosType::Kind::TraitObject))
        return gep;
    // Nested tuple element is now embedded INLINE (tuple_llvm_type); its value is
    // the GEP address (a tuple value is a pointer to its storage), like a struct
    // element — return it, don't load an 8-byte ptr from the inline slot.
    if (TypeRef et(type); et && et.kind() == LogosType::Kind::Tuple)
        return gep;
    return builder_.create<mlir::LLVM::LoadOp>(loc_, elem_mlir, gep);
}

// ---------------------------------------------------------------------------
// Cast
// ---------------------------------------------------------------------------

mlir::Value MLIRGenImpl::gen_expr_kind(lir_view::ECastView v, TypeRef type) {
    if (!v.operand()) return nullptr;
    TypeRef op_ty = v.operand().type(pool_impl());
    // Null-handle construct: `0 as *mut dyn` / `0 as &dyn` — an integer (null)
    // cast to a trait object. Under the uniform fat model a dyn value is a
    // 16-byte {data,vtable} pair; produce a ZEROED pair (data=null) so
    // null-handle sentinels (`NodeARC { p: 0 as *mut dyn }`) and the
    // `… as *mut u64 == 0` null checks behave (was an 8-byte null handle).
    if (type && TypeRef(type).kind() == LogosType::Kind::TraitObject && op_ty) {
        auto sk = TypeRef(op_ty).kind();
        bool int_src = sk == LogosType::Kind::IntLit  || sk == LogosType::Kind::I64 ||
                       sk == LogosType::Kind::U64     || sk == LogosType::Kind::I32 ||
                       sk == LogosType::Kind::U32     || sk == LogosType::Kind::Usize ||
                       sk == LogosType::Kind::Isize;
        if (int_src) {
            auto pair_t = dyn_llvm_type();
            auto a = create_entry_alloca(pair_t);
            auto nullp = builder_.create<mlir::LLVM::ZeroOp>(loc_, ptr_type());
            llvm::SmallVector<mlir::LLVM::GEPArg> d0{int32_t(0), int32_t(0)};
            llvm::SmallVector<mlir::LLVM::GEPArg> d1{int32_t(0), int32_t(1)};
            auto p0 = builder_.create<mlir::LLVM::GEPOp>(loc_, ptr_type(), pair_t, a, d0);
            auto p1 = builder_.create<mlir::LLVM::GEPOp>(loc_, ptr_type(), pair_t, a, d1);
            builder_.create<mlir::LLVM::StoreOp>(loc_, nullp, p0);
            builder_.create<mlir::LLVM::StoreOp>(loc_, nullp, p1);
            return a;
        }
    }
    std::string writ_build_fn(v.writ_build_fn());
    // ── Writ typed container cast: &[T] as <I32>[] → Writ. ──────────
    if (!writ_build_fn.empty()) {
        auto val = gen_expr(v.operand());
        if (!val) return nullptr;
        auto parent_mod = builder_.getBlock()->getParent()->getParentOfType<mlir::ModuleOp>();
        auto build_fn = find_func_op(parent_mod, writ_build_fn);
        if (!build_fn) {
            std::fprintf(stderr, "mlir_gen: '%s' not found — add 'use logos.mem.writ.ctr;'\n",
                         writ_build_fn.c_str());
            return nullptr;
        }
        // fix3: dispatch by function name prefix, not arg count — getNumArguments() is fragile
        // (any future 3-arg array builder would silently take the wrong path).
        if (writ_build_fn.rfind("writ_build_map_", 0) == 0 ||
            writ_build_fn.rfind("writ_build_map_", 0) == 0) {
            // Map source: alloca ptr to MapSliceI32 { &[i32], &[AnyVal] }.
            // Slice fields are stored INLINE (16-byte {ptr,len} fat pairs), so
            // the LLVM layout is { {ptr,i64}, {ptr,i64} } — keys_slice IS the
            // inline storage at field 0, vals_slice at field 1 (no pointer
            // indirection, unlike the pre-inline layout).
            auto stype = slice_llvm_type();  // { ptr, i64 }
            auto mtype = mlir::LLVM::LLVMStructType::getLiteral(
                builder_.getContext(), {stype, stype});
            // keys_slice = &field 0 (inline {ptr,len}); extract data ptr + len.
            llvm::SmallVector<mlir::LLVM::GEPArg> k0i{int32_t(0), int32_t(0)};
            auto keys_slice = builder_.create<mlir::LLVM::GEPOp>(loc_, ptr_type(), mtype, val, k0i);
            llvm::SmallVector<mlir::LLVM::GEPArg> kdi{int32_t(0), int32_t(0)};
            auto kdp = builder_.create<mlir::LLVM::GEPOp>(loc_, ptr_type(), stype, keys_slice, kdi);
            auto keys_ptr = builder_.create<mlir::LLVM::LoadOp>(loc_, ptr_type(), kdp);
            llvm::SmallVector<mlir::LLVM::GEPArg> kli{int32_t(0), int32_t(1)};
            auto klp = builder_.create<mlir::LLVM::GEPOp>(loc_, ptr_type(), stype, keys_slice, kli);
            auto len = builder_.create<mlir::LLVM::LoadOp>(loc_, builder_.getIntegerType(64), klp);
            // vals_slice = &field 1 (inline {ptr,len}); extract data ptr.
            llvm::SmallVector<mlir::LLVM::GEPArg> v0i{int32_t(0), int32_t(1)};
            auto vals_slice = builder_.create<mlir::LLVM::GEPOp>(loc_, ptr_type(), mtype, val, v0i);
            llvm::SmallVector<mlir::LLVM::GEPArg> vdi{int32_t(0), int32_t(0)};
            auto vdp = builder_.create<mlir::LLVM::GEPOp>(loc_, ptr_type(), stype, vals_slice, vdi);
            auto vals_ptr = builder_.create<mlir::LLVM::LoadOp>(loc_, ptr_type(), vdp);
            auto call = builder_.create<mlir::func::CallOp>(
                loc_, build_fn, mlir::ValueRange{keys_ptr, vals_ptr, len});
            return call.getNumResults() > 0 ? call.getResult(0) : nullptr;
        }
        // Array source: alloca ptr to { ptr, i64 } (slice representation).
        // Extract data_ptr (field 0) and len (field 1).
        auto stype = slice_llvm_type();
        llvm::SmallVector<mlir::LLVM::GEPArg> pi{int32_t(0), int32_t(0)};
        auto pp = builder_.create<mlir::LLVM::GEPOp>(loc_, ptr_type(), stype, val, pi);
        auto data_ptr = builder_.create<mlir::LLVM::LoadOp>(loc_, ptr_type(), pp);
        llvm::SmallVector<mlir::LLVM::GEPArg> li{int32_t(0), int32_t(1)};
        auto lp = builder_.create<mlir::LLVM::GEPOp>(loc_, ptr_type(), stype, val, li);
        auto len = builder_.create<mlir::LLVM::LoadOp>(loc_, builder_.getIntegerType(64), lp);
        auto call = builder_.create<mlir::func::CallOp>(
            loc_, build_fn, mlir::ValueRange{data_ptr, len});
        return call.getNumResults() > 0 ? call.getResult(0) : nullptr;
    }

    auto val    = gen_expr(v.operand());
    if (!val) return nullptr;

    // ── Supertrait upcast: `&dyn Sub`/`dyn Sub` → `&dyn Super` ───────────
    // Source is a {data,vtable} fat pair for trait `Sub`; recover `Super`'s
    // vtable from the stored super-vtable-pointer slot that Sub's vtable
    // carries after its method slots (Rust trait-upcasting), and build a new
    // fat pair with the SAME data pointer. Sub==Super (identity dyn casts)
    // falls through to the no-op reinterpret below.
    if (type && op_ty) {
        TypeRef tgt(type), src(op_ty);
        TypeRef tgt_to = tgt, src_to = src;
        if ((tgt.kind() == LogosType::Kind::Ref || tgt.kind() == LogosType::Kind::MutRef) &&
            tgt.pointee()) tgt_to = tgt.pointee();
        if ((src.kind() == LogosType::Kind::Ref || src.kind() == LogosType::Kind::MutRef) &&
            src.pointee()) src_to = src.pointee();
        if (tgt_to.kind() == LogosType::Kind::TraitObject &&
            src_to.kind() == LogosType::Kind::TraitObject) {
            std::string sub(src_to.trait_name()), super(tgt_to.trait_name());
            if (!sub.empty() && !super.empty() && sub != super) {
                int idx = -1;
                if (auto sit = trait_upcast_supers_.find(sub);
                    sit != trait_upcast_supers_.end())
                    for (size_t i = 0; i < sit->second.size(); ++i)
                        if (sit->second[i] == super) { idx = (int)i; break; }
                if (idx >= 0) {
                    size_t mcount = trait_method_names_.count(sub)
                        ? trait_method_names_[sub].size() : 0;
                    auto dyn_struct = dyn_llvm_type();
                    mlir::Value src_ptr = val;
                    if (val.getType() == dyn_struct) src_ptr = spill_to_alloca(val);
                    // data = field 0, vtable = field 1 of the source fat pair.
                    llvm::SmallVector<mlir::LLVM::GEPArg> i0{int32_t(0), int32_t(0)};
                    llvm::SmallVector<mlir::LLVM::GEPArg> i1{int32_t(0), int32_t(1)};
                    auto dp = builder_.create<mlir::LLVM::GEPOp>(loc_, ptr_type(), dyn_struct, src_ptr, i0);
                    auto data_ptr = builder_.create<mlir::LLVM::LoadOp>(loc_, ptr_type(), dp);
                    auto vp = builder_.create<mlir::LLVM::GEPOp>(loc_, ptr_type(), dyn_struct, src_ptr, i1);
                    auto vtable_ptr = builder_.create<mlir::LLVM::LoadOp>(loc_, ptr_type(), vp);
                    // Stored super-vtable ptr lives at [3 + |Sub methods| + idx].
                    llvm::SmallVector<mlir::LLVM::GEPArg> si{int32_t(3 + (int)mcount + idx)};
                    auto sp = builder_.create<mlir::LLVM::GEPOp>(loc_, ptr_type(), ptr_type(), vtable_ptr, si);
                    auto super_vt = builder_.create<mlir::LLVM::LoadOp>(loc_, ptr_type(), sp);
                    // Build the upcast {data, super_vtable} fat pair (stack).
                    auto out = create_entry_alloca(dyn_struct);
                    auto o0 = builder_.create<mlir::LLVM::GEPOp>(loc_, ptr_type(), dyn_struct, out, i0);
                    auto o1 = builder_.create<mlir::LLVM::GEPOp>(loc_, ptr_type(), dyn_struct, out, i1);
                    builder_.create<mlir::LLVM::StoreOp>(loc_, data_ptr, o0);
                    builder_.create<mlir::LLVM::StoreOp>(loc_, super_vt, o1);
                    return out;
                }
            }
        }
    }

    // `Box<[T;N]>` → `Box<[T]>` (owning fat slice): unsize coercion. The operand
    // is the stdlib Box struct {ptr:*mut [T;N]} (a thin heap ptr to the N-element
    // array); the target is an OWNING Slice {data,len}. Take the box's heap ptr as
    // `data` and the array length N as `len` — Rust's `Box::new([..]) as Box<[T]>`.
    if (type && TypeRef(type).kind() == LogosType::Kind::Slice &&
        TypeRef(type).owning_slice() && op_ty &&
        stdlib_smart_ptr_kind(op_ty) == TypeRef::OwningKind::Box &&
        TypeRef(op_ty).type_args().size() == 1 &&
        TypeRef(TypeRef(op_ty).type_args()[0]).kind() == LogosType::Kind::Array) {
        uint64_t n = TypeRef(TypeRef(op_ty).type_args()[0]).arr_size();
        mlir::Value data_ptr;
        if (val.getType() == ptr_type()) {
            auto bt = find_struct_it(op_ty);  // pkg-qualified-first (avoid same-name alias)
            mlir::Type sp_ty = bt != struct_types_.end() ? bt->second.llvm_type
                : mlir::LLVM::LLVMStructType::getLiteral(builder_.getContext(), {ptr_type()});
            llvm::SmallVector<mlir::LLVM::GEPArg> gi{int32_t(0), int32_t(0)};
            auto fp = builder_.create<mlir::LLVM::GEPOp>(loc_, ptr_type(), sp_ty, val, gi);
            data_ptr = builder_.create<mlir::LLVM::LoadOp>(loc_, ptr_type(), fp);
        } else {
            data_ptr = builder_.create<mlir::LLVM::ExtractValueOp>(
                loc_, val, llvm::ArrayRef<int64_t>{0});
        }
        auto stype = slice_llvm_type();
        auto out = create_entry_alloca(stype);
        llvm::SmallVector<mlir::LLVM::GEPArg> i0{int32_t(0), int32_t(0)};
        llvm::SmallVector<mlir::LLVM::GEPArg> i1{int32_t(0), int32_t(1)};
        auto p0 = builder_.create<mlir::LLVM::GEPOp>(loc_, ptr_type(), stype, out, i0);
        builder_.create<mlir::LLVM::StoreOp>(loc_, data_ptr, p0);
        auto p1 = builder_.create<mlir::LLVM::GEPOp>(loc_, ptr_type(), stype, out, i1);
        auto nval = builder_.create<mlir::arith::ConstantIntOp>(loc_, (int64_t)n, 64);
        builder_.create<mlir::LLVM::StoreOp>(loc_, nval, p1);
        return out;
    }

    // str (Slice<u8> = fat pointer {ptr, i64}) as *const u8 → extract field 0.
    // Must be checked BEFORE the val.getType() == target early-return because
    // both the alloca ptr (fat struct) and *const u8 are !llvm.ptr in LLVM 17.
    if (TypeRef ot(op_ty);
        ot && ot.kind() == LogosType::Kind::Slice &&
        ot.elem() && ot.elem().kind() == LogosType::Kind::U8 &&
        type && TypeRef(type).kind() == LogosType::Kind::Ptr &&
        TypeRef(type).pointee() && TypeRef(type).pointee().kind() == LogosType::Kind::U8) {
        auto stype = slice_llvm_type();
        llvm::SmallVector<mlir::LLVM::GEPArg> pi{int32_t(0), int32_t(0)};
        auto pp = builder_.create<mlir::LLVM::GEPOp>(loc_, ptr_type(), stype, val, pi);
        return builder_.create<mlir::LLVM::LoadOp>(loc_, ptr_type(), pp);
    }

    auto target = logos_to_mlir(type);
    if (!target) return val;

    // Custom-DST unsize coercion: `*mut/*const/&  ConcreteStruct<Sized>` whose
    // tail param is bound to a concrete type → DstRef with a `dyn` tail (e.g.
    // `*mut Inner<A>` → `*mut Inner<dyn Tr>`). Build the fat {data, vtable} pair:
    // `data` = the (thin) source pointer to the WHOLE struct; `vtable` = the
    // concrete tail type's vtable for the tail trait. Reuses the same
    // {data,vtable} layout + vtable machinery as `&concrete as &dyn`; the tail-
    // field byte offset is applied later at `&p.tail as &dyn` projection (the
    // vtable is shared by the prefix-fat and the tail handle). This is Rust's
    // CoerceUnsized for a struct with an unsized (`dyn`) tail field. Gated on a
    // dyn tail only — slice-tail DSTs build their DstRef via the slice path.
    if (type && TypeRef(type).kind() == LogosType::Kind::DstRef &&
        !TypeRef(type).type_args().empty() && op_ty &&
        val.getType() == ptr_type()) {
        TypeRef tgt_tail = TypeRef(type).type_args().back();
        auto ttk = TypeRef(tgt_tail).kind();
        if (ttk == LogosType::Kind::TraitObject || ttk == LogosType::Kind::UnsizedDyn) {
            TypeRef src(op_ty);
            TypeRef src_pointee =
                (src.kind() == LogosType::Kind::Ptr ||
                 src.kind() == LogosType::Kind::Ref ||
                 src.kind() == LogosType::Kind::MutRef) ? src.pointee() : TypeRef(nullptr);
            if (src_pointee &&
                (TypeRef(src_pointee).kind() == LogosType::Kind::Struct ||
                 TypeRef(src_pointee).kind() == LogosType::Kind::ZonedStruct) &&
                !TypeRef(src_pointee).type_args().empty()) {
                // The tail param's concrete binding is the source instance's
                // last type-arg (the `?Sized` tail param is declared last).
                TypeRef concrete_tail = TypeRef(src_pointee).type_args().back();
                std::string trait = std::string(TypeRef(tgt_tail).trait_name());
                std::string vt_name =
                    (TypeRef(concrete_tail).kind() == LogosType::Kind::Struct ||
                     TypeRef(concrete_tail).kind() == LogosType::Kind::ZonedStruct)
                        ? concrete_struct_name(concrete_tail)
                        : type_str(concrete_tail);
                if (auto alloca = coerce_to_dyn(val, trait, vt_name, concrete_tail))
                    return alloca;
            }
        }
    }

    // NOTE: there is NO "thin `Ptr<TraitObject>` handle" cast. A `*const/*mut dyn
    // Trait` is a 16-byte fat pair (sema folds the literal-`dyn` pointee to bare
    // TraitObject, exactly like `&dyn`), handled by the bare-TraitObject branch
    // below. Two former branches synthesised an 8-byte thin pointer to a
    // heap-copied fat pair for a `Ptr<TraitObject>` target — conceptually broken
    // (non-Rust; thin-handle-to-heap-fat) and provably unreachable (a sweep of
    // all 5433 tests/stdlib/examples never hit them). Removed. If a bare vtable
    // handle is ever needed it should be a `*u8` / compiler system-type widened
    // to a fat pointer via an intrinsic — not a magic heap-promoting cast.

    // `&T` / `&mut T` / `*const T` / `*mut T` (over a concrete) `as &dyn` /
    // `*mut dyn` — the target is bare TraitObject (uniform fat model: `&dyn`
    // AND `*mut dyn` are both 16-byte fat pairs). Synthesize the fat pair.
    if (val.getType() == ptr_type() && target == ptr_type() &&
        type && TypeRef(type).kind() == LogosType::Kind::TraitObject &&
        op_ty &&
        (TypeRef(op_ty).kind() == LogosType::Kind::Ref ||
         TypeRef(op_ty).kind() == LogosType::Kind::MutRef ||
         TypeRef(op_ty).kind() == LogosType::Kind::Ptr) &&
        // G168-A: only unsize a ref to a CONCRETE pointee; a `&dyn`→`dyn`
        // reinterpret (source pointee already a trait object) is a no-op.
        TypeRef(op_ty).pointee() &&
        TypeRef(TypeRef(op_ty).pointee()).kind() != LogosType::Kind::TraitObject) {
        auto pointee = TypeRef(op_ty).pointee();
        std::string src_struct;
        if (pointee && (TypeRef(pointee).kind() == LogosType::Kind::Struct ||
                        TypeRef(pointee).kind() == LogosType::Kind::ZonedStruct))
            src_struct = concrete_struct_name(pointee);
        else if (pointee)
            // Primitive pointee (`&i64 as &dyn Trait` via a blanket impl): the
            // vtable keys on the primitive's bare type name (`i64`), not "" —
            // an empty key makes build_inline_vtable return null → no vtable
            // stored → SIGSEGV on dispatch.
            src_struct = type_str(pointee);
        std::string trait = std::string(TypeRef(type).trait_name());
        if (auto alloca = coerce_to_dyn(val, trait, src_struct, pointee)) return alloca;
    }

    // `box_new(x) as Box<dyn Trait>` (and `concrete as dyn`/`*dyn`): the source is
    // a Box<concrete> / concrete struct VALUE (Kind::Struct), not a Ref/Ptr, so
    // the two branches above don't fire — they require a reference source. In
    // RETURN / arg / any non-let position the cast must coerce itself (a `let b:
    // Box<dyn> = …` works only because gen_let's TraitObject path coerces). Mirror
    // that path here: unwrap a Box<T> to its inner concrete T for the vtable key,
    // pass the box value as the data pointer, and build the fat handle.
    if (target == ptr_type() && op_ty &&
        stdlib_smart_ptr_kind(op_ty) != TypeRef::OwningKind::Borrow &&
        TypeRef(op_ty).type_args().size() == 1) {
        auto sp_kind = stdlib_smart_ptr_kind(op_ty);
        TypeRef tgt_to(type);
        if (tgt_to.kind() == LogosType::Kind::Ptr && tgt_to.pointee())
            tgt_to = tgt_to.pointee();
        TypeRef boxed = TypeRef(op_ty).type_args()[0];
        // A dyn-payload smart pointer is already a handle — don't re-wrap.
        if (tgt_to && tgt_to.kind() == LogosType::Kind::TraitObject &&
            boxed && TypeRef(boxed).kind() != LogosType::Kind::TraitObject) {
            // Field 0 of the smart pointer: Box<T> = {*mut T} (field0 IS data);
            // Rc<T>/Arc<T> = {*mut RcInner<T>} (field0 is the box; data = &val =
            // box + offsetof(val) = box + round_up(8, align(T)), since
            // RcInner = {i32/AtomicI32 strong, i32/AtomicI32 weak, T val}).
            mlir::Value field0;
            if (val.getType() == ptr_type()) {
                auto bt = find_struct_it(op_ty);  // pkg-qualified-first (avoid same-name alias)
                mlir::Type sp_ty = bt != struct_types_.end() ? bt->second.llvm_type
                    : mlir::LLVM::LLVMStructType::getLiteral(builder_.getContext(), {ptr_type()});
                llvm::SmallVector<mlir::LLVM::GEPArg> gi{int32_t(0), int32_t(0)};
                auto fp = builder_.create<mlir::LLVM::GEPOp>(loc_, ptr_type(), sp_ty, val, gi);
                field0 = builder_.create<mlir::LLVM::LoadOp>(loc_, ptr_type(), fp);
            } else {
                field0 = builder_.create<mlir::LLVM::ExtractValueOp>(
                    loc_, val, llvm::ArrayRef<int64_t>{0});
            }
            mlir::Value data_ptr;
            if (sp_kind == TypeRef::OwningKind::Box) {
                data_ptr = field0;
            } else {
                uint64_t align = layout_of(boxed).align ? layout_of(boxed).align : 1;
                uint64_t off = (8 + align - 1) & ~(align - 1);  // offsetof(val): 2×i32 header
                llvm::SmallVector<mlir::LLVM::GEPArg> oi{int32_t(off)};
                data_ptr = builder_.create<mlir::LLVM::GEPOp>(
                    loc_, ptr_type(), builder_.getI8Type(), field0, oi);
            }
            std::string src_struct =
                (TypeRef(boxed).kind() == LogosType::Kind::Struct ||
                 TypeRef(boxed).kind() == LogosType::Kind::ZonedStruct)
                    ? concrete_struct_name(boxed)
                    : type_str(boxed);
            std::string trait = std::string(tgt_to.trait_name());
            // Owning smart-pointer dyn = VALUE fat-pair {data,vtable} (like &dyn,
            // but droppable). data = the concrete object; vtable[0..2] = drop /
            // size / align. heap=false ⇒ inline value (no extra handle); drop is
            // kind-specific (Box→free(data); Rc/Arc→dec strong + free RcInner) —
            // see gen_drop_owning_dyn_handle.
            if (auto alloca = coerce_to_dyn(data_ptr, trait, src_struct, boxed)) return alloca;
        }
    }

    // C6-cc-08 follow-up: fat-pointer → thin-pointer cast.
    //
    //   * `*mut dyn Trait` / `*const dyn Trait` (Ptr<TraitObject>) → `*mut ()`
    //     / `*const ()` (Ptr<Void>): extract data field of the dyn fat pair,
    //     vtable discarded. Restricted to Void target so `*mut dyn T as *mut
    //     Node` raw-reinterpret casts (used in persistent) keep their no-op
    //     semantics.
    //
    //   * `*const [T]` / `*mut [T]` (Slice) → any thin pointer (Ptr<X>) or
    //     Ptr<Array>: extract data field of the slice {ptr,len} pair. Slice's
    //     grammar is an intentional fat-ptr declaration (no raw-reinterpret
    //     overload exists), so widening the source target permits both `as
    //     *const [T; N]` and `as *const ()` from a raw slice.
    //
    // Both MLIR types are `ptr` so the identity check below would short-
    // circuit and emit a no-op cast yielding the fat-pair address instead
    // of the contained data ptr — must run before it.
    // A dyn value materialised as a by-VALUE fat struct `{ptr,ptr}` (e.g.
    // `arc.p` where the TraitObject field was loaded) cast to a thin/void
    // pointer: extract the DATA half (element 0) directly. Mirrors the
    // ptr-form fat_to_thin below but for the already-loaded struct.
    if (op_ty && type && target == ptr_type() &&
        mlir::isa<mlir::LLVM::LLVMStructType>(val.getType())) {
        auto st = mlir::cast<mlir::LLVM::LLVMStructType>(val.getType());
        auto fkv = TypeRef(op_ty).kind();
        bool src_dyn =
            fkv == LogosType::Kind::TraitObject ||
            fkv == LogosType::Kind::Closure ||
            ((fkv == LogosType::Kind::Ref || fkv == LogosType::Kind::MutRef) &&
             TypeRef(op_ty).pointee() &&
             TypeRef(op_ty).pointee().kind() == LogosType::Kind::TraitObject);
        auto tkv = TypeRef(type).kind();
        bool dst_thin = tkv == LogosType::Kind::Ptr && TypeRef(type).pointee() &&
            TypeRef(type).pointee().kind() != LogosType::Kind::TraitObject &&
            TypeRef(type).pointee().kind() != LogosType::Kind::Closure &&
            TypeRef(type).pointee().kind() != LogosType::Kind::Slice;
        if (src_dyn && dst_thin && st.getBody().size() == 2)
            return builder_.create<mlir::LLVM::ExtractValueOp>(
                loc_, val, llvm::ArrayRef<int64_t>{0});
    }

    bool fat_to_thin = false;
    auto fk = TypeRef(op_ty).kind();
    auto tk = TypeRef(type).kind();
    if (val.getType() == ptr_type() && target == ptr_type() && op_ty && type) {
        bool src_is_dyn_ptr =
            fk == LogosType::Kind::Ptr &&
            TypeRef(op_ty).pointee() &&
            (TypeRef(op_ty).pointee().kind() == LogosType::Kind::TraitObject ||
             TypeRef(op_ty).pointee().kind() == LogosType::Kind::Closure);
        // A bare `&dyn`/`*dyn`/closure VALUE (now a 16-byte fat pair) cast to a
        // thin pointer extracts the DATA half (Rust: `*mut dyn as *mut ()` =
        // data). This is the uniform-fat model: `*mut dyn` is bare TraitObject.
        // A `&dyn`/`&mut dyn` (Ref/MutRef over TraitObject) shares the bare
        // TraitObject repr (a pointer to the 16-byte fat pair), so extracting
        // the data half works identically — treat it as a dyn value too.
        bool src_is_dyn_ref =
            (fk == LogosType::Kind::Ref || fk == LogosType::Kind::MutRef) &&
            TypeRef(op_ty).pointee() &&
            TypeRef(op_ty).pointee().kind() == LogosType::Kind::TraitObject;
        bool src_is_dyn_val = fk == LogosType::Kind::TraitObject ||
                              fk == LogosType::Kind::Closure ||
                              src_is_dyn_ref;
        bool src_is_slice = fk == LogosType::Kind::Slice;
        // A custom-DST reference VALUE (DstRef, a pointer to the 16-byte
        // {data, meta} pair) cast to a thin pointer extracts the DATA half —
        // e.g. drop_rc's `free(self.inner as *mut u8)` on an `Rc<dyn>` must
        // free the heap RcInner (the data ptr), not the fat-pair storage.
        // A #[self_describing] DstRef is already a THIN ptr straight to the header
        // (the value IS the data ptr), so the cast is a no-op — exclude it.
        bool src_is_dst = fk == LogosType::Kind::DstRef &&
                          !dstref_pointee_self_describing(op_ty);
        // A fat zone-mut `&mut T` cast to a thin `*mut T`/`*const T` extracts the
        // DATA half (the object pointer), dropping the carried zone.
        bool src_is_zone_mut = (ref_repr_of(op_ty) == RefReprKind::FatZoneMut);
        bool dst_is_void_ptr =
            tk == LogosType::Kind::Ptr &&
            TypeRef(type).pointee() &&
            TypeRef(type).pointee().kind() == LogosType::Kind::Void;
        bool dst_is_thin_ptr =
            tk == LogosType::Kind::Ptr &&
            TypeRef(type).pointee() &&
            TypeRef(type).pointee().kind() != LogosType::Kind::TraitObject &&
            TypeRef(type).pointee().kind() != LogosType::Kind::Closure &&
            TypeRef(type).pointee().kind() != LogosType::Kind::Slice;
        if (src_is_dyn_ptr && dst_is_void_ptr) fat_to_thin = true;
        if (src_is_dyn_val && (dst_is_void_ptr || dst_is_thin_ptr)) fat_to_thin = true;
        if (src_is_slice  && dst_is_thin_ptr) fat_to_thin = true;
        if (src_is_dst    && dst_is_thin_ptr) fat_to_thin = true;
        if (src_is_zone_mut && dst_is_thin_ptr) fat_to_thin = true;
    }
    if (fat_to_thin) {
        auto fat_t = mlir::LLVM::LLVMStructType::getLiteral(
            builder_.getContext(), {ptr_type(), ptr_type()});
        llvm::SmallVector<mlir::LLVM::GEPArg> data_idx{int32_t(0), int32_t(0)};
        auto dp = builder_.create<mlir::LLVM::GEPOp>(
            loc_, ptr_type(), fat_t, val, data_idx);
        return builder_.create<mlir::LLVM::LoadOp>(loc_, ptr_type(), dp);
    }
    // C6-cc-08 follow-up: thin → fat-slice cast. `*const [T; N]` / `*mut
    // [T; N]` (Ptr<Array>) cast to `*const [T]` / `*mut [T]` (Slice)
    // synthesises a `{ptr, len=N}` fat pair on the stack. Without this the
    // cast was a no-op (both ptr at MLIR), so a subsequent fat→thin
    // extraction read the array contents as if they were the data field of
    // a phantom slice pair.
    if (val.getType() == ptr_type() && target == ptr_type() && op_ty && type &&
        fk == LogosType::Kind::Ptr &&
        TypeRef(op_ty).pointee() &&
        TypeRef(op_ty).pointee().kind() == LogosType::Kind::Array &&
        tk == LogosType::Kind::Slice) {
        auto slice_t = slice_llvm_type();
        auto alloca = create_entry_alloca(slice_t);
        llvm::SmallVector<mlir::LLVM::GEPArg> di{int32_t(0), int32_t(0)};
        auto dp = builder_.create<mlir::LLVM::GEPOp>(
            loc_, ptr_type(), slice_t, alloca, di);
        builder_.create<mlir::LLVM::StoreOp>(loc_, val, dp);
        llvm::SmallVector<mlir::LLVM::GEPArg> li{int32_t(0), int32_t(1)};
        auto lp = builder_.create<mlir::LLVM::GEPOp>(
            loc_, ptr_type(), slice_t, alloca, li);
        auto len = builder_.create<mlir::arith::ConstantIntOp>(
            loc_, int64_t(TypeRef(op_ty).pointee().arr_size()), 64);
        builder_.create<mlir::LLVM::StoreOp>(loc_, len, lp);
        return alloca;
    }
    if (val.getType() == target) return val;

    auto fi = mlir::dyn_cast<mlir::IntegerType>(val.getType());
    auto ti = mlir::dyn_cast<mlir::IntegerType>(target);
    if (fi && ti) {
        if (ti.getWidth() > fi.getWidth()) {
            bool src_unsigned = fi.getWidth() == 1 ||
                (op_ty && LogosType::is_unsigned_repr_kind(TypeRef(op_ty).kind()));
            if (src_unsigned)
                return builder_.create<mlir::arith::ExtUIOp>(loc_, target, val);
            return builder_.create<mlir::arith::ExtSIOp>(loc_, target, val);
        }
        if (ti.getWidth() < fi.getWidth())
            return builder_.create<mlir::arith::TruncIOp>(loc_, target, val);
        return val;
    }
    if (mlir::dyn_cast<mlir::IntegerType>(val.getType()) &&
        mlir::dyn_cast<mlir::FloatType>(target)) {
        // Bool must be ZERO-extended before the conversion: sitofp(i1 1) = -1.0
        // (wrong), uitofp(i1 1) = 1.0 (correct). `Kind::Bool` is named inside
        // `LogosType::is_unsigned_repr_kind` (include/logos/compiler/sema.hpp)
        // for exactly this reason, so ONE test decides the whole question here.
        //
        // ── A SECOND DISJUNCT USED TO STAND BESIDE THIS ONE, AND IT IS DELETED
        //    BY MEASUREMENT, NOT BY TASTE. ────────────────────────────────────
        // The deleted clause was a VALUE test, `val.getType() == i1`, and the
        // two were MUTUALLY REDUNDANT across the whole corpus (recorded at the
        // parent commit, 2026-08-10, and the kind-side half re-verified here as
        // control C1): dropping either one ALONE left L2, the generated tier
        // and all 31 gates green, so a two-step cleanup — drop one, see green,
        // drop the other months later, see green because the first is already
        // gone — would silently reintroduce `sitofp(i1 1) = -1.0`. Two clauses
        // that cover each other are not two guards; they are one guard and one
        // booby trap, and the trap is what a green run hides.
        //
        // What separates them is the case each answers ALONE:
        //  · the VALUE test alone answers when NO Logos type reached this site
        //    (`op_ty == nullptr`);
        //  · the KIND test alone answers for a `bool`/`char`/unsigned whose MLIR
        //    value is not i1.
        // The KIND test's private case is WITNESSED and common —
        // `tests/logos/pass/cast_unsigned_to_float.logos` (u56 2^55, u128 2^63),
        // `uint_to_f64.logos`, `int_signedness_cast_unsigned.logos` (u64 max)
        // all go negative under `sitofp` — measured as C4 below. The VALUE
        // test's private case is what was measured away:
        //
        //  (1) INSTRUMENTED COUNT, 2026-08-10. A counter at this arm, logging
        //      every call where `op_ty` is null OR the two clauses disagree, run
        //      over the stdlib build AND `ctest -LE imported` (3231/3231 green,
        //      the generated tier and all 31 gates included): 23 disagreements,
        //      EVERY ONE of them `null_op_ty=0 v=0 k=1` — a non-i1 unsigned, the
        //      KIND test answering alone. ZERO calls with a null `op_ty`. ZERO
        //      calls with an i1 value whose Logos kind is not unsigned-repr.
        //  (2) DERIVATION, which is why (1) is not a coincidence of the corpus:
        //      `Kind::Bool` is the ONLY kind `logos_to_mlir` maps to i1
        //      (`src/compiler/mlir_gen_types.cpp`, the sole `getI1Type()` there),
        //      and `Bool ∈ is_unsigned_repr_kind`. So whenever `op_ty` is
        //      PRESENT, `val.getType() == i1` IMPLIES the KIND test — the VALUE
        //      test could only ever add coverage in the null-`op_ty` case.
        //  (3) A null Logos type IS representable in mlir-gen — `lir_mirror.cpp`
        //      writes the type attribute under `if (ty)`, and the same counter
        //      caught 6 null-source coercions in the SAME run — but all six were
        //      at the `coerce_numeric` call in `MLIRGenImpl::gen_arr_lit`
        //      (`src/compiler/mlir_gen.cpp`), which passes no source type, from
        //      bare int literals in
        //      `tests/logos/pass/writ_as_array_variants.logos` (`let buf7:
        //      [f32; 3] = [1, 2, 3];`), never here. And an i1 cannot reach that
        //      site either: `let a: [f64; 2] = [true, false];` is refused by
        //      sema — "expected [f64; 2], got [bool; 2]" (measured).
        // Deleting this clause is therefore not "cleanup on a green run": it
        // makes the remaining clause SINGLY load-bearing, and THAT is what the
        // fixtures now prove. Three controls, each predicted, each restored:
        //  · C1, on the TWO-clause code: exclude `Kind::Bool` here alone.
        //    PREDICTED green, MEASURED green — `bool_as_f32`,
        //    `wql_agg_avg_bool_value_rule` and the three unsigned fixtures all
        //    pass. That is the redundancy reproducing, and it is why no
        //    single-clause edit could red this site before.
        //  · C3, on THIS code: exclude `Kind::Bool` here alone. PREDICTED red,
        //    MEASURED `bool_as_f32` exit 40 (expected 42 — `true as f32` came
        //    back -1.0) and `wql_agg_avg_bool_value_rule` exit 13, with the
        //    three unsigned fixtures still green. One-sided, so bool is pinned
        //    by itself.
        //  · C4, on THIS code: force `src_unsigned` false. PREDICTED all five
        //    red; MEASURED all five red — `bool_as_f32` 40,
        //    `cast_unsigned_to_float` 1, `int_signedness_cast_unsigned` 21
        //    (predicted 81 from a stale note — the fixture's own first unsigned
        //    check is 21), `uint_to_f64` 1, `wql_agg_avg_bool_value_rule` 13.
        // Each catch is a VALUE catch after the whole program ran, not a
        // compile refusal: the wrong `sitofp` compiles and links fine.
        //
        // ⚠ THE `Kind::Char` MEMBER OF THE SURVIVING TEST WAS CALLED UNOBSERVABLE
        // HERE, AND THAT WAS WRONG — CORRECTED 2026-08-10 BY CONSTRUCTING THE
        // WITNESS. This comment used to end "do not go looking for the char
        // fixture that reds this line, there is none", arguing that
        // `logos_to_mlir` lowers `Kind::Char` to i32 and every Unicode scalar is
        // ≤ 0x10FFFF, hence positive, hence `sitofp` ≡ `uitofp`. The premise is
        // about LEGAL chars; what is not checked is the CAST. `u32 as char` is
        // accepted today with no range test (no char arm exists in any
        // cast-validity check — the open blocklist→allowlist arc), so a `char`
        // holding 0xFFFFFFFF is reachable in an accepted program, and there the
        // two lowerings differ by the whole word: uitofp 4294967295.0 vs sitofp
        // -1.0. `tests/logos/pass/char_as_f64_unsigned.logos` is that fixture.
        // CONTROL, applied to `is_unsigned_repr_kind` itself
        // (`include/logos/compiler/sema.hpp`), not to this line: drop
        // `k == Kind::Char` → that fixture PREDICTED 8, MEASURED 8, while L1
        // 690/690, L2 1894/1894, the 12 684-case generated tier and all 31 gates
        // stayed GREEN — so before it was written the member had no sensor at
        // all, and this note was the reason nobody went looking. An
        // unobservability claim is a prediction; write it only after failing to
        // build the witness, and name what you tried.
        // The SAME control on `k == Kind::Bool` reds `bool_as_f32` at 40 and
        // `wql_agg_avg_bool_value_rule` at 13 (measured), which is the
        // single-clause bite the deletion above was for — measured at the shared
        // predicate rather than at this call site.
        //
        // ⚠ AND THE DELETED CLAUSE IS STILL LIVE TWICE, BOTH IN INT→INT ARMS.
        // `fi.getWidth() == 1 ||` stands in this same function's int→int
        // widening arm (see the `ExtUIOp`/`ExtSIOp` pair above) and in
        // `MLIRGenImpl::coerce_int` (`src/compiler/mlir_gen_impl.hpp`), with the
        // identical shape — a value test OR-ed with the kind test. Removing the
        // one in this function alone left L2 1894/1894 + 12 684 + 31 gates GREEN
        // (measured 2026-08-10), i.e. it is the same "one guard plus a booby
        // trap" pattern this arm was cleaned of, not yet cleaned there. It is
        // NOT deleted here: unlike this arm, `coerce_int` is called with no
        // source type from many sites, so its width-1 disjunct may be the only
        // thing choosing `zext` for a bool — that is a separate measurement,
        // owed before either is touched. Recorded so the next reader does not
        // mistake this arm's tidiness for the file's.
        bool src_unsigned = op_ty &&
            LogosType::is_unsigned_repr_kind(TypeRef(op_ty).kind());
        if (src_unsigned)
            return builder_.create<mlir::arith::UIToFPOp>(loc_, target, val);
        return builder_.create<mlir::arith::SIToFPOp>(loc_, target, val);
    }
    // float → float (truncate or extend)
    if (mlir::dyn_cast<mlir::FloatType>(val.getType()) &&
        mlir::dyn_cast<mlir::FloatType>(target)) {
        auto fv = mlir::cast<mlir::FloatType>(val.getType());
        auto ft = mlir::cast<mlir::FloatType>(target);
        if (ft.getWidth() < fv.getWidth())
            return builder_.create<mlir::arith::TruncFOp>(loc_, target, val);
        return builder_.create<mlir::arith::ExtFOp>(loc_, target, val);
    }
    if (mlir::dyn_cast<mlir::FloatType>(val.getType()) &&
        mlir::dyn_cast<mlir::IntegerType>(target)) {
        bool dst_unsigned = type &&
            LogosType::is_unsigned_repr_kind(TypeRef(type).kind());
        if (dst_unsigned)
            return builder_.create<mlir::arith::FPToUIOp>(loc_, target, val);
        return builder_.create<mlir::arith::FPToSIOp>(loc_, target, val);
    }

    // int → ptr
    if (mlir::dyn_cast<mlir::IntegerType>(val.getType()) && target == ptr_type()) {
        mlir::Value v64;
        bool src_unsigned = op_ty &&
            LogosType::is_unsigned_repr_kind(TypeRef(op_ty).kind());
        if (src_unsigned && val.getType() != builder_.getI64Type())
            v64 = builder_.create<mlir::arith::ExtUIOp>(loc_, builder_.getI64Type(), val);
        else
            v64 = coerce_int(val, builder_.getI64Type());
        // int → pointer. NOTE for fat-pointee targets (`as *const [T]` /
        // `*const str`): a fat-pointer VALUE in this codegen is the ADDRESS
        // of its {data, meta} pair, so the integer is taken AS that address
        // (the established Vec/str pointer-arithmetic idiom). `0 as *const
        // [u8]` therefore yields a NULL PAIR ADDRESS — constructing a
        // dereferenceable null fat pointer requires a valid pair location
        // (see tests/logos/pass/sized_partition_dispatch.logos).
        return builder_.create<mlir::LLVM::IntToPtrOp>(loc_, ptr_type(), v64);
    }
    // ptr → int
    if (val.getType() == ptr_type() && mlir::dyn_cast<mlir::IntegerType>(target))
        return builder_.create<mlir::LLVM::PtrToIntOp>(loc_, target, val);

    // ── R2: A REPORT THE EXIT CODE DOES NOT CONSULT IS A GATE THAT LIES ──────
    // This used to be a raw `fprintf` + `return nullptr`. The nullptr's fate
    // then depended entirely on WHO CONSUMED IT: a `return` statement has its
    // own R2 report, so `fn f(s: str) -> f64 { return s as f64; }` failed the
    // compile — while `a = s as f64;` in a statement position did NOT, and
    // MEASURED at the parent commit that program printed "mlir_gen: unsupported
    // cast", exited 0 and wrote a 1288-byte object file with the assignment
    // silently gone. Whether the compiler told the truth about its own
    // malfunction was decided by the syntactic context of the cast.
    //
    // The channel to say it on already existed (`bugs_`/`bug_null`, enforced at
    // generate()'s single exit, ledgered and canaried); this site simply did not
    // use it. Routing it here reports at the SITE OF THE MALFUNCTION, so the
    // verdict no longer depends on the consumer — and it names both types, which
    // the old line did not.
    //
    // ⚠ THIS CLOSES ONE SITE, NOT THE CLASS OF RAW REPORTS. 44 other
    // `fprintf(stderr, "mlir_gen: ...")` sites still bypass `bugs_`; each is its
    // own arc. Nor does it fix the reason `str as f64` reaches mlir-gen at all —
    // that is sema's cast blocklist→allowlist arc (see sema_expr.cpp, where the
    // float↔pointer instance of the same class was closed one type at a time).
    std::string from_s, to_s;
    { llvm::raw_string_ostream os(from_s); val.getType().print(os); }
    { llvm::raw_string_ostream os(to_s);   target.print(os); }
    return bug_null("unsupported cast: no lowering from '{}' to '{}' — the cast "
                    "produced no value, so whatever consumed it was not emitted",
                    from_s, to_s);
}

// ---------------------------------------------------------------------------
// If-expression / match-expression
// ---------------------------------------------------------------------------

mlir::Value MLIRGenImpl::gen_expr_kind(lir_view::EIfExprView v, TypeRef type) {
    if (!v.cond() || !v.then_val() || !v.else_val()) return nullptr;
    auto cond = gen_expr(v.cond());
    if (!cond) return nullptr;

    // Void-typed if (both branches evaluate to `()`): still emit the
    // branches — they may have side effects (panic call, write, etc.).
    // Without this the cond is computed but no `br` follows, silently
    // dropping both branch bodies. Was the root cause behind
    // assert_eq!(2, 3) not panicking despite the if-then containing
    // __fmt_panic. logos_to_mlir(Void) returns nullptr, which the
    // original `if (!result_type) return nullptr;` short-circuited on.
    mlir::Type result_type = logos_to_mlir(type);
    bool void_if = (type && TypeRef(type).kind() == LogosType::Kind::Void);
    if (!result_type && !void_if) return nullptr;

    // Allocate result slot in the current (entry-reachable) block.
    mlir::Value result_alloca;
    if (result_type) result_alloca = create_entry_alloca(result_type);

    auto* region      = builder_.getBlock()->getParent();
    auto* then_block  = new mlir::Block();
    auto* else_block  = new mlir::Block();
    auto* merge_block = new mlir::Block();
    region->push_back(then_block);
    region->push_back(else_block);
    region->push_back(merge_block);

    builder_.create<mlir::cf::CondBranchOp>(loc_, cond, then_block, else_block);

    builder_.setInsertionPointToStart(then_block);
    auto then_val = gen_expr(v.then_val());
    // P3-pg-04: branch may diverge (e.g. `break` as expression) and
    // already cf.br'd the block. Skip the store+merge cf.br in that
    // case — the block is terminated and merge_block's predecessors
    // simply omit this edge.
    if (!is_terminated(builder_.getBlock())) {
        if (result_type) {
            if (!then_val) then_val = builder_.create<mlir::arith::ConstantIntOp>(loc_, 0, 32);
            then_val = coerce_numeric(then_val, result_type);
            // Branches may return a struct by-value (function call) while the merge
            // slot expects a pointer (struct values are normally pointer-aliased).
            // Spill aggregate values so both branches store a pointer.
            if (result_type == ptr_type() &&
                mlir::isa<mlir::LLVM::LLVMStructType>(then_val.getType()))
                then_val = spill_to_alloca(then_val);
            builder_.create<mlir::LLVM::StoreOp>(loc_, then_val, result_alloca);
        }
        builder_.create<mlir::cf::BranchOp>(loc_, merge_block);
    }

    builder_.setInsertionPointToStart(else_block);
    auto else_val = gen_expr(v.else_val());
    if (!is_terminated(builder_.getBlock())) {
        if (result_type) {
            if (!else_val) else_val = builder_.create<mlir::arith::ConstantIntOp>(loc_, 0, 32);
            else_val = coerce_numeric(else_val, result_type);
            if (result_type == ptr_type() &&
                mlir::isa<mlir::LLVM::LLVMStructType>(else_val.getType()))
                else_val = spill_to_alloca(else_val);
            builder_.create<mlir::LLVM::StoreOp>(loc_, else_val, result_alloca);
        }
        builder_.create<mlir::cf::BranchOp>(loc_, merge_block);
    }

    builder_.setInsertionPointToStart(merge_block);
    if (!result_type) {
        // Void if: synthetic unit value so callers don't deref nullptr.
        return builder_.create<mlir::arith::ConstantIntOp>(loc_, 0, 32);
    }
    return builder_.create<mlir::LLVM::LoadOp>(loc_, result_type, result_alloca);
}

mlir::Value MLIRGenImpl::gen_expr_kind(lir_view::EMatchExprView v, TypeRef type) {
    namespace pc = lir_schema::pat;
    if (!v.scrut()) return nullptr;
    TypeRef scrut_ty = v.scrut().type(pool_impl());
    std::vector<lir_view::EMatchArmRef> arm_refs;
    v.each_arm([&](lir_view::EMatchArmRef a){ arm_refs.push_back(a); });
    mlir::Type result_type = logos_to_mlir(type);
    // Void-typed match (every arm evaluates to `()`): still lower the
    // scrutinee, the dispatch and every arm body — they carry side effects
    // (calls, writes, `f()?` early-returns). logos_to_mlir(Void) is null, and
    // the original `if (!result_type) return nullptr;` short-circuited the
    // ENTIRE match: the scrutinee call and all arm effects vanished with no
    // diagnostic. That silent-drop is the class that masked the void-Ok `?`
    // and gap-C miscompiles. Mirror gen_if's void path — no result slot, emit
    // everything, return a synthetic unit at the merge.
    bool void_match = (type && TypeRef(type).kind() == LogosType::Kind::Void);
    if (!result_type && !void_match) return nullptr;

    // Allocate result slot before the match (entry-block reachable). A void
    // match has no slot — arm values are null (void) and never stored.
    mlir::Value result_alloca;
    if (result_type) result_alloca = create_entry_alloca(result_type);

    // G161-4: coerce an arm's value to the result-slot type. When the result is
    // a by-pointer aggregate (struct/array → result_type == ptr) but the arm
    // produced the aggregate BY VALUE (e.g. a call like `e.clone()` returning a
    // struct value, not a struct-lit pointer), spill it to an alloca and store
    // the POINTER — storing the wide value into the 8-byte ptr slot overflowed
    // and corrupted the stack (→ SIGSEGV on the subsequent load).
    auto store_arm_result = [&](mlir::Value val, mlir::Type rt) -> mlir::Value {
        if (rt == ptr_type() && val && val.getType() != ptr_type() &&
            (mlir::isa<mlir::LLVM::LLVMStructType>(val.getType()) ||
             mlir::isa<mlir::LLVM::LLVMArrayType>(val.getType())))
            return spill_to_alloca(val);
        return coerce_numeric(val, rt);
    };

    auto* region      = builder_.getBlock()->getParent();
    auto* merge_block = new mlir::Block();

    auto scrut = gen_expr(v.scrut());
    if (!scrut) {
        region->push_back(merge_block);
        builder_.create<mlir::cf::BranchOp>(loc_, merge_block);
        builder_.setInsertionPointToStart(merge_block);
        if (!result_type)
            return builder_.create<mlir::arith::ConstantIntOp>(loc_, 0, 32);
        return builder_.create<mlir::LLVM::LoadOp>(loc_, result_type, result_alloca);
    }

    // Detect tagged enum: load discriminant.
    mlir::Value scrut_ptr = nullptr;
    const TaggedEnumInfo* te_info = nullptr;
    if (TypeRef st(scrut_ty); st) {
        // Auto-deref `&Enum` / `&mut Enum` / `*Enum` so `match &enum_val { ... }`
        // works the same as `match enum_val { ... }`. logos-core 4.3: peel
        // arbitrary-depth `&`/`&mut`/`*` chains (e.g. `&&Option<T>`) — pre-fix
        // only one layer was peeled, so deeper chains slipped past with scrut
        // typed `!llvm.ptr` and the downstream `arith.cmpi(scrut, disc:i64)`
        // failed verification (operand 0 must be integer-like).
        TypeRef enum_t = st;
        int via_ref_depth = 0;
        while (enum_t &&
               (enum_t.kind() == LogosType::Kind::Ref ||
                enum_t.kind() == LogosType::Kind::MutRef ||
                enum_t.kind() == LogosType::Kind::Ptr) &&
               enum_t.pointee()) {
            ++via_ref_depth;
            enum_t = enum_t.pointee();
        }
        bool via_ref = via_ref_depth > 0;
        if (enum_t.kind() == LogosType::Kind::Enum) {
            te_info = resolve_tagged_enum(std::string(enum_t.enum_name()), enum_t);
            if (te_info) {
                // Enum value-repr: an enum value IS a pointer to its inline
                // {disc,payload} storage (one level, like a Struct). `&Enum` is
                // the SAME one-level pointer — no extra deref. A by-value
                // aggregate (returned by value from a fn) is spilled. For
                // `&&Enum`/deeper, peel the EXTRA layers via additional Loads
                // before treating the result as the enum-storage pointer.
                if (via_ref) {
                    // scrut already IS A pointer; peel `via_ref_depth - 1`
                    // EXTRA layers so we arrive at the enum-storage pointer.
                    for (int li = 1; li < via_ref_depth; ++li)
                        scrut = builder_.create<mlir::LLVM::LoadOp>(
                            loc_, ptr_type(), scrut);
                } else if (scrut.getType() != ptr_type()) {
                    auto tmp = create_entry_alloca(te_info->llvm_type);
                    builder_.create<mlir::LLVM::StoreOp>(loc_, scrut, tmp);
                    scrut = tmp;
                }
                scrut_ptr = scrut;
                scrut = enum_load_disc(scrut_ptr, *te_info);  // Phase 3.5 chokepoint
            } else if (via_ref) {
                // G165-1: a FIELDLESS / C-like enum has no TaggedEnumInfo — its
                // by-value form is a plain i32 discriminant (not a heap ptr), so
                // `&Enum` is a one-level ptr-to-i32. Load the disc through the
                // ref(s) so the scalar arm tests compare i32==disc rather than
                // the raw `&Enum` pointer (crashed: `arith.cmpi` operand must
                // be integer). Peel extra ref layers first for deeper chains.
                for (int li = 1; li < via_ref_depth; ++li)
                    scrut = builder_.create<mlir::LLVM::LoadOp>(
                        loc_, ptr_type(), scrut);
                scrut = builder_.create<mlir::LLVM::LoadOp>(
                    loc_, builder_.getI32Type(), scrut);
            }
        }
    }
    mlir::Type scrut_type = scrut.getType();

    // Extract payload bindings for a PatVariantData arm into scope.
    std::function<std::vector<std::string>(lir_view::PatRef)> extract_arm_payload =
        [&](lir_view::PatRef pat_ref) -> std::vector<std::string> {
        std::vector<std::string> added;
        if (pat_ref.kind() == pc::Code::VariantData) {
            lir_view::PatVariantDataView pvd{pat_ref};
            if (te_info && scrut_ptr) {
                // Canonical payload binder (bind_enum_payload) — the old
                // inline copy here had its own drifted `ref v` classifier
                // (predates ref_bind_kind) and missed the thin-&Struct
                // payload case (`Option<&P>` then `q.x` mis-read).
                bind_enum_payload(scrut_ptr, te_info, pvd, added, nullptr);
            }
        } else if (pat_ref.kind() == pc::Code::Wild) {
            std::string name(lir_view::PatWildView{pat_ref}.name());
            if (!name.empty() && name != "_") {
                mlir::Value sv = scrut_ptr ? scrut_ptr : scrut;
                auto alloca = create_entry_alloca(sv.getType());
                builder_.create<mlir::LLVM::StoreOp>(loc_, sv, alloca);
                evict_var_shapes(name);
                scope_[name] = alloca;
                let_vars_.insert(name);
                var_elem_types_[name] = sv.getType();
                added.push_back(name);
            }
        } else if (pat_ref.kind() == pc::Code::Tuple) {
            // [UNIFY C-tuple/expr] Route the tuple destructure through the
            // single pat_bind foundation (was a duplicate of the stmt
            // extract_payload tuple binder). Record bound names via
            // collect_pat_bindings so the arm-scope cleanup still erases them.
            mlir::Value tptr = scrut_ptr ? scrut_ptr : scrut;
            if (tptr) {
                if (tptr.getType() != ptr_type()) {
                    auto tt = tuple_llvm_type(scrut_ty);
                    auto a = create_entry_alloca(tt ? tt : ptr_type());
                    builder_.create<mlir::LLVM::StoreOp>(loc_, tptr, a);
                    tptr = a;
                }
                // A tuple value IS a pointer to its inline storage (G1 Rust
                // by-value layout): pat_bind's Tuple case GEPs into `tptr`
                // directly (no load). The legacy alloca-of-ptr `tslot` wrapper
                // that the old by-pointer load consumed would make pat_bind GEP
                // into the pointer bytes → garbage elements.
                std::vector<std::pair<std::string, TypeRef>> binds;
                collect_pat_bindings(pat_ref, scrut_ty, binds);
                for (auto& b : binds) added.push_back(b.first);
                pat_bind(pat_ref, tptr, scrut_ty);
            }
        } else if (pat_ref.kind() == pc::Code::Slice) {
            // Slice/array pattern element bindings (`[x, y]`, `[a, _, c]`,
            // `[h, ..]`) in match-as-expression. The Slice *test* is emitted
            // separately (the literal-element AND-chain below); this binds the
            // named elements into scope so the arm body can read them. Without
            // this the bindings were never created and the arm read garbage.
            // Mirrors the match-statement extract_payload Slice case.
            TypeRef atype(scrut_ty);
            if (atype && atype.kind() == LogosType::Kind::Array && atype.elem()) {
                auto elem_mlir = logos_to_mlir(atype.elem());
                auto arr_mlir  = logos_to_mlir(atype);
                mlir::Value aptr = scrut_ptr ? scrut_ptr : scrut;
                if (aptr && aptr.getType() != ptr_type() && arr_mlir) {
                    auto a = create_entry_alloca(arr_mlir);
                    builder_.create<mlir::LLVM::StoreOp>(loc_, aptr, a);
                    aptr = a;
                }
                if (aptr && elem_mlir && arr_mlir) {
                    auto bind_elem = [&](lir_view::PatRef sp, int32_t idx) {
                        if (!sp) return;
                        llvm::SmallVector<mlir::LLVM::GEPArg> gi{int32_t(0), idx};
                        auto ep = builder_.create<mlir::LLVM::GEPOp>(
                            loc_, ptr_type(), arr_mlir, aptr, gi);
                        if (sp.kind() == pc::Code::Wild) {
                            std::string pwn(lir_view::PatWildView{sp}.name());
                            if (pwn == "_" || pwn.empty()) return;
                            auto val = builder_.create<mlir::LLVM::LoadOp>(loc_, elem_mlir, ep);
                            auto alloca = create_entry_alloca(elem_mlir);
                            builder_.create<mlir::LLVM::StoreOp>(loc_, val, alloca);
                            evict_var_shapes(pwn);
                            scope_[pwn] = alloca;
                            let_vars_.insert(pwn);
                            var_elem_types_[pwn] = elem_mlir;
                            added.push_back(pwn);
                        } else if (sp.kind() == pc::Code::RefBind) {
                            std::string prbn(lir_view::PatRefBindView{sp}.name());
                            if (prbn == "_" || prbn.empty()) return;
                            auto alloca = create_entry_alloca(ptr_type());
                            builder_.create<mlir::LLVM::StoreOp>(loc_, ep, alloca);
                            evict_var_shapes(prbn);
                            scope_[prbn] = alloca;
                            let_vars_.insert(prbn);
                            var_elem_types_[prbn] = ptr_type();
                            added.push_back(prbn);
                        }
                    };
                    lir_view::PatSliceView psl{pat_ref};
                    int32_t idx = 0;
                    psl.each_prefix([&](lir_view::PatRef sp){ bind_elem(sp, idx++); });
                    size_t total = (size_t)atype.arr_size();
                    size_t suf_n = psl.suffix_count();
                    int32_t sidx = (int32_t)(total - suf_n);
                    psl.each_suffix([&](lir_view::PatRef sp){ bind_elem(sp, sidx++); });
                }
            } else if (atype && atype.kind() == LogosType::Kind::Slice && atype.elem()) {
                // Dynamic-slice bindings: GEP through the data pointer (field 0
                // of the {ptr,len} descriptor). Only prefix elements bind
                // (suffix-after-`..` rejected by sema for dynamic slices).
                auto elem_mlir = logos_to_mlir(atype.elem());
                auto stype     = slice_llvm_type();
                mlir::Value sptr = scrut_ptr ? scrut_ptr : scrut;
                if (sptr && sptr.getType() != ptr_type() && stype) {
                    auto a = create_entry_alloca(stype);
                    builder_.create<mlir::LLVM::StoreOp>(loc_, sptr, a);
                    sptr = a;
                }
                if (sptr && elem_mlir) {
                    llvm::SmallVector<mlir::LLVM::GEPArg> di{int32_t(0), int32_t(0)};
                    auto dp = builder_.create<mlir::LLVM::GEPOp>(loc_, ptr_type(), stype, sptr, di);
                    auto data = builder_.create<mlir::LLVM::LoadOp>(loc_, ptr_type(), dp);
                    // Runtime length — needed to index suffix elements from the
                    // tail and to size the named-rest sub-slice.
                    llvm::SmallVector<mlir::LLVM::GEPArg> lgi{int32_t(0), int32_t(1)};
                    auto lp = builder_.create<mlir::LLVM::GEPOp>(loc_, ptr_type(), stype, sptr, lgi);
                    auto len = builder_.create<mlir::LLVM::LoadOp>(loc_, builder_.getI64Type(), lp);
                    auto bind_slice_elem = [&](lir_view::PatRef sp, mlir::Value idx) {
                        if (!sp) return;
                        llvm::SmallVector<mlir::LLVM::GEPArg> gi{idx};
                        auto ep = builder_.create<mlir::LLVM::GEPOp>(loc_, ptr_type(), elem_mlir, data, gi);
                        if (sp.kind() == pc::Code::Wild) {
                            std::string pwn(lir_view::PatWildView{sp}.name());
                            if (pwn == "_" || pwn.empty()) return;
                            auto val = builder_.create<mlir::LLVM::LoadOp>(loc_, elem_mlir, ep);
                            auto alloca = create_entry_alloca(elem_mlir);
                            builder_.create<mlir::LLVM::StoreOp>(loc_, val, alloca);
                            evict_var_shapes(pwn);
                            scope_[pwn] = alloca;
                            let_vars_.insert(pwn);
                            var_elem_types_[pwn] = elem_mlir;
                            added.push_back(pwn);
                        } else if (sp.kind() == pc::Code::RefBind) {
                            std::string prbn(lir_view::PatRefBindView{sp}.name());
                            if (prbn == "_" || prbn.empty()) return;
                            auto alloca = create_entry_alloca(ptr_type());
                            builder_.create<mlir::LLVM::StoreOp>(loc_, ep, alloca);
                            evict_var_shapes(prbn);
                            scope_[prbn] = alloca;
                            let_vars_.insert(prbn);
                            var_elem_types_[prbn] = ptr_type();
                            added.push_back(prbn);
                        }
                    };
                    lir_view::PatSliceView psl{pat_ref};
                    auto i64c = [&](int64_t k){
                        return builder_.create<mlir::arith::ConstantIntOp>(loc_, k, 64).getResult();
                    };
                    size_t pre_n = 0, suf_n = psl.suffix_count();
                    psl.each_prefix([&](lir_view::PatRef){ ++pre_n; });
                    {
                        int64_t idx = 0;
                        psl.each_prefix([&](lir_view::PatRef sp){ bind_slice_elem(sp, i64c(idx++)); });
                    }
                    // G167-6a: suffix elements bind from the tail at `len - suf_n + i`.
                    {
                        size_t i = 0;
                        psl.each_suffix([&](lir_view::PatRef sp){
                            mlir::Value idx = builder_.create<mlir::arith::SubIOp>(
                                loc_, len, i64c((int64_t)(suf_n - i)));
                            bind_slice_elem(sp, idx);
                            ++i;
                        });
                    }
                    // G167-6b: a named rest (`rest @ ..`) binds a sub-slice
                    // descriptor {data + pre_n, len - pre_n - suf_n} so it can be
                    // used locally (`rest.len()`, re-match) as a first-class
                    // `&[T]` place — not just forwarded to another fn.
                    psl.each_rest([&](lir_view::PatRef rp){
                        if (!rp || rp.kind() != pc::Code::Wild) return;
                        std::string rn(lir_view::PatWildView{rp}.name());
                        if (rn == "_" || rn.empty()) return;
                        auto rest_data = builder_.create<mlir::LLVM::GEPOp>(
                            loc_, ptr_type(), elem_mlir, data,
                            llvm::SmallVector<mlir::LLVM::GEPArg>{i64c((int64_t)pre_n)});
                        mlir::Value rest_len = builder_.create<mlir::arith::SubIOp>(
                            loc_, len, i64c((int64_t)(pre_n + suf_n)));
                        auto desc = create_entry_alloca(stype);
                        auto d0 = builder_.create<mlir::LLVM::GEPOp>(loc_, ptr_type(), stype, desc,
                            llvm::SmallVector<mlir::LLVM::GEPArg>{int32_t(0), int32_t(0)});
                        builder_.create<mlir::LLVM::StoreOp>(loc_, rest_data, d0);
                        auto d1 = builder_.create<mlir::LLVM::GEPOp>(loc_, ptr_type(), stype, desc,
                            llvm::SmallVector<mlir::LLVM::GEPArg>{int32_t(0), int32_t(1)});
                        builder_.create<mlir::LLVM::StoreOp>(loc_, rest_len, d1);
                        evict_var_shapes(rn);
                        scope_[rn] = desc;
                        var_slice_[rn] = elem_mlir;
                        added.push_back(rn);
                    });
                }
            }
        } else if (pat_ref.kind() == pc::Code::Struct) {
            // Struct pattern field bindings (`A { f0: x }`, shorthand `A { x }`,
            // `A { .. }`). Mirrors the match-statement extract_payload Struct
            // case: GEP each named field and bind. `{ .. }` binds nothing.
            lir_view::PatStructView ps{pat_ref};
            std::string sname(ps.struct_name());
            auto sit = struct_types_.find(sname);
            if (sit != struct_types_.end()) {
                const StructInfo& sinfo = sit->second;
                mlir::Value sptr = scrut_ptr ? scrut_ptr : scrut;
                if (sptr && sptr.getType() != ptr_type()) {
                    auto a = create_entry_alloca(sptr.getType());
                    builder_.create<mlir::LLVM::StoreOp>(loc_, sptr, a);
                    sptr = a;
                }
                lir_view::StructView sd{};
                if (auto di = all_struct_defs_.find(sname); di != all_struct_defs_.end())
                    sd = di->second;
                if (sptr) ps.each_field([&](lir_view::PatFieldBindingView pfb) {
                    std::string field_name(pfb.field_name());
                    auto bind_field = [&](const std::string& bind_name) {
                        if (bind_name.empty() || bind_name == "_") return;
                        auto fp = gep_field(sptr, sinfo, field_name);
                        if (!fp) return;
                        mlir::Type fmlir;
                        for (auto& sf : sinfo.fields)
                            if (sf.name == field_name) { fmlir = sf.type; break; }
                        if (!fmlir) return;
                        auto val = builder_.create<mlir::LLVM::LoadOp>(loc_, fmlir, fp);
                        auto alloca = create_entry_alloca(fmlir);
                        builder_.create<mlir::LLVM::StoreOp>(loc_, val, alloca);
                        evict_var_shapes(bind_name);
                        scope_[bind_name] = alloca;
                        let_vars_.insert(bind_name);
                        var_elem_types_[bind_name] = fmlir;
                        added.push_back(bind_name);
                    };
                    auto sub = pfb.sub();
                    if (!sub) bind_field(field_name);
                    else if (sub.kind() == pc::Code::Wild)
                        bind_field(std::string(lir_view::PatWildView{sub}.name()));
                    else if (sub.kind() == pc::Code::RefBind) {
                        // `S { f: ref name }` — bind `name : &FieldType` to the
                        // field's address (a borrow, no load/copy). Previously
                        // omitted from this dispatch ⇒ name was never bound and
                        // `*name` in the arm body read uninitialized stack
                        // (the §6.1 P53 union-pattern miscompile).
                        auto rbn = lir_view::PatRefBindView{sub}.name();
                        if (!rbn.empty() && rbn != "_") {
                            std::string bn(rbn);
                            auto fp = gep_field(sptr, sinfo, field_name);
                            if (fp) {
                                TypeRef fty;
                                if (sd) for (auto lf : sd.fields())
                                    if (std::string(lf.name()) == field_name) { fty = lf.type(pool_impl()); break; }
                                bool ref_to_struct = fty &&
                                    (TypeRef(fty).kind() == LogosType::Kind::Struct ||
                                     TypeRef(fty).kind() == LogosType::Kind::ZonedStruct);
                                evict_var_shapes(bn);
                                if (ref_to_struct) {
                                    scope_[bn] = fp;
                                    let_vars_.insert(bn);
                                    var_struct_[bn] = mlir_struct_key(fty);
                                } else {
                                    auto alloca = create_entry_alloca(ptr_type());
                                    builder_.create<mlir::LLVM::StoreOp>(loc_, fp, alloca);
                                    scope_[bn] = alloca;
                                    let_vars_.insert(bn);
                                    var_elem_types_[bn] = ptr_type();
                                }
                                added.push_back(bn);
                            }
                        }
                    }
                    else {
                        // G148-1: refutable field sub (variant / tuple / or /
                        // nested struct) — bind its inner names via the
                        // recursive matcher. Record them so the join sweep
                        // cleans them up.
                        auto fp = gep_field(sptr, sinfo, field_name);
                        if (fp) {
                            TypeRef fty;
                            if (sd) for (auto lf : sd.fields())
                                if (std::string(lf.name()) == field_name) { fty = lf.type(pool_impl()); break; }
                            std::vector<std::pair<std::string, TypeRef>> binds;
                            collect_pat_bindings(sub, fty, binds);
                            pat_bind(sub, fp, fty);
                            for (auto& [nm, bt] : binds) added.push_back(nm);
                        }
                    }
                });
            }
        } else if (pat_ref.kind() == pc::Code::RefBind) {
            // `ref r` / `ref mut r` — bind name as a pointer to the scrutinee
            // slot so `*r` in a guard or body reads through the original
            // value. Mirrors the match-statement path (mlir_gen_stmt.cpp).
            // Without this, the binding is missing and a deref-in-guard
            // (`ref r if *r < 0`) yields a null guard value → CondBranchOp
            // crash.
            std::string prbn(lir_view::PatRefBindView{pat_ref}.name());
            if (!prbn.empty() && prbn != "_") {
                mlir::Value bind_val;
                if (scrut_ptr) {
                    bind_val = scrut_ptr;
                } else if (scrut.getType() == ptr_type()) {
                    bind_val = scrut;
                } else {
                    auto tmp = create_entry_alloca(scrut.getType());
                    builder_.create<mlir::LLVM::StoreOp>(loc_, scrut, tmp);
                    bind_val = tmp;
                }
                auto alloca = create_entry_alloca(ptr_type());
                builder_.create<mlir::LLVM::StoreOp>(loc_, bind_val, alloca);
                evict_var_shapes(prbn);
                scope_[prbn] = alloca;
                let_vars_.insert(prbn);
                var_elem_types_[prbn] = ptr_type();
                added.push_back(prbn);
            }
        } else if (pat_ref.kind() == pc::Code::RefPat) {
            // &pat / &mut pat — recurse into the inner pattern.
            if (auto inner = lir_view::PatRefPatView{pat_ref}.inner())
                added = extract_arm_payload(inner);
        } else if (pat_ref.kind() == pc::Code::At) {
            // `name @ sub` — bind the outer name to the whole scrutinee,
            // then recurse into the sub-pattern. Mirrors the match-statement
            // path (mlir_gen_stmt.cpp). Without this the binding was missing
            // and the arm read garbage.
            lir_view::PatAtView pa{pat_ref};
            std::string aname(pa.name());
            if (!aname.empty() && aname != "_") {
                mlir::Value sv = scrut_ptr ? scrut_ptr : scrut;
                auto alloca = create_entry_alloca(sv.getType());
                builder_.create<mlir::LLVM::StoreOp>(loc_, sv, alloca);
                evict_var_shapes(aname);
                scope_[aname] = alloca;
                let_vars_.insert(aname);
                var_elem_types_[aname] = sv.getType();
                added.push_back(aname);
            }
            if (auto sub = pa.sub()) {
                auto inner = extract_arm_payload(sub);
                added.insert(added.end(), inner.begin(), inner.end());
            }
        } else if (pat_ref.kind() == pc::Code::Or) {
            // Or-pattern bindings: sema's NG4 check guarantees every alternative
            // binds the same name set. For variant alts that share payload
            // shape (e.g. L(x: i32) | R(x: i32)), GEP'ing as the first alt's
            // variant lands at the same offset/type — which is what we want.
            // (Mixed-shape alts are rejected at sema.)
            lir_view::PatRef first;
            lir_view::PatOrView{pat_ref}.each_alt([&](lir_view::PatRef a){
                if (!first) first = a;
            });
            if (first) {
                auto inner = extract_arm_payload(first);
                added.insert(added.end(), inner.begin(), inner.end());
            }
        }
        return added;
    };

    mlir::Block* else_block = merge_block;
    bool exhaustive_discrete = false;
    if (scrut_ty && TypeRef(scrut_ty).kind() == LogosType::Kind::Bool) {
        bool has_true = false, has_false = false, has_wild = false;
        for (size_t ai = 0; ai < arm_refs.size(); ++ai) {
            if (arm_refs[ai].guard()) continue;
            auto pat_ref = arm_refs[ai].pat();
            if (pat_ref.kind() == pc::Code::Wild) { has_wild = true; break; }
            auto check_bool = [&](lir_view::PatRef p) {
                if (p.kind() == pc::Code::Bool) {
                    if (lir_view::PatBoolView{p}.value()) has_true = true; else has_false = true;
                }
            };
            if (pat_ref.kind() == pc::Code::Or) {
                lir_view::PatOrView{pat_ref}.each_alt([&](lir_view::PatRef a){ check_bool(a); });
            } else {
                check_bool(pat_ref);
            }
        }
        exhaustive_discrete = has_wild || (has_true && has_false);
    } else if (scrut_ty && TypeRef(scrut_ty).kind() == LogosType::Kind::Enum) {
        std::set<int32_t> covered;
        bool has_wild = false;
        auto cover_enum = [&](lir_view::PatRef p) {
            if (p.kind() == pc::Code::Variant)          covered.insert(static_cast<int32_t>(lir_view::PatVariantView{p}.disc()));
            else if (p.kind() == pc::Code::VariantData) covered.insert(static_cast<int32_t>(lir_view::PatVariantDataView{p}.disc()));
        };
        for (size_t ai = 0; ai < arm_refs.size(); ++ai) {
            if (arm_refs[ai].guard()) continue;
            auto pat_ref = arm_refs[ai].pat();
            if (pat_ref.kind() == pc::Code::Wild) { has_wild = true; break; }
            if (pat_ref.kind() == pc::Code::Or) {
                lir_view::PatOrView{pat_ref}.each_alt([&](lir_view::PatRef a){ cover_enum(a); });
            } else {
                cover_enum(pat_ref);
            }
        }
        if (has_wild) {
            exhaustive_discrete = true;
        } else {
            std::string en(TypeRef(scrut_ty).enum_name());
            auto eit = enum_types_.find(en);
            if (eit != enum_types_.end()) {
                bool all_covered = true;
                eit->second.each_variant([&](lir_view::EnumVariantView v) {
                    if (covered.count(v.disc()) == 0) all_covered = false;
                });
                exhaustive_discrete = all_covered;
            } else if (auto* te = resolve_tagged_enum(en, scrut_ty)) {
                exhaustive_discrete = std::all_of(
                    te->variants.begin(), te->variants.end(),
                    [&](const TaggedEnumInfo::VariantPayload& v) { return covered.count(v.disc) > 0; });
            }
        }
    }
    if (exhaustive_discrete) {
        auto* default_block = new mlir::Block();
        region->push_back(default_block);
        {
            mlir::OpBuilder::InsertionGuard ig(builder_);
            builder_.setInsertionPointToStart(default_block);
            builder_.create<mlir::LLVM::UnreachableOp>(loc_);
        }
        else_block = default_block;
    }
    // gap C: each arm is its own lexical scope. Exact snapshot/restore
    // replaces the old erase-only cleanup, which forgot the shape maps
    // (var_struct_ etc.) and never re-instated shadowed outer bindings.
    auto match_scope = snapshot_var_scope();
    for (int i = (int)arm_refs.size() - 1; i >= 0; --i) {
        auto arm_guard_ref = arm_refs[i].guard();
        auto arm_value_ref = arm_refs[i].value();
        auto arm_pat_ref = arm_refs[i].pat();
        // G155-5(a): an explicit `&E::Foo{..}` / `&E::Some(x)` ref-pattern over
        // a `&Enum` scrutinee that we already auto-deref'd to a TAGGED enum
        // (te_info set ⇒ `scrut`/`scrut_ptr` already point at the enum struct,
        // `scrut` is the disc). The leading `&` is redundant here — peel it so
        // the inner variant/struct pattern flows through the normal
        // payload-extracting paths below. (The C-like no-payload `&E::A` case,
        // where te_info is null and `scrut` is still a ptr-to-i32, keeps the
        // dedicated RefPat handler further down.)
        if (te_info && arm_pat_ref.kind() == pc::Code::RefPat) {
            if (auto inner = lir_view::PatRefPatView{arm_pat_ref}.inner();
                inner && (inner.kind() == pc::Code::VariantData ||
                          inner.kind() == pc::Code::Variant ||
                          inner.kind() == pc::Code::Struct))
                arm_pat_ref = inner;
        }
        auto* body_block = new mlir::Block();
        region->push_back(body_block);

        mlir::Block* arm_entry = body_block;

        if (arm_guard_ref) {
            // guard_block: extract bindings, evaluate guard, branch to body_block or else_block.
            auto* guard_block = new mlir::Block();
            region->push_back(guard_block);
            {
                mlir::OpBuilder::InsertionGuard ig(builder_);
                builder_.setInsertionPointToStart(guard_block);
                extract_arm_payload(arm_pat_ref);
                auto gval = arm_guard_ref ? gen_expr(arm_guard_ref) : nullptr;
                gval = coerce_int(gval, builder_.getI1Type());
                if (!gval) {
                    // Defensive: a null guard value would crash CondBranchOp.
                    // Treat an unevaluable guard as always-false (fall through
                    // to the next arm) rather than SIGSEGV.
                    gval = builder_.create<mlir::LLVM::ConstantOp>(
                        loc_, builder_.getI1Type(),
                        builder_.getIntegerAttr(builder_.getI1Type(), 0));
                }
                builder_.create<mlir::cf::CondBranchOp>(loc_, gval, body_block, else_block);
            }
            arm_entry = guard_block;
            // body_block: bindings already in scope from guard_block; generate arm value.
            {
                mlir::OpBuilder::InsertionGuard ig(builder_);
                builder_.setInsertionPointToStart(body_block);
                auto val = arm_value_ref ? gen_expr(arm_value_ref) : nullptr;
                restore_var_scope(match_scope);
                if (!is_terminated(builder_.getBlock())) {
                    if (val) {
                        val = store_arm_result(val, result_type);
                        builder_.create<mlir::LLVM::StoreOp>(loc_, val, result_alloca);
                    }
                    builder_.create<mlir::cf::BranchOp>(loc_, merge_block);
                }
            }
        } else {
            mlir::OpBuilder::InsertionGuard ig(builder_);
            builder_.setInsertionPointToStart(body_block);
            extract_arm_payload(arm_pat_ref);
            auto val = arm_value_ref ? gen_expr(arm_value_ref) : nullptr;
            restore_var_scope(match_scope);
            if (!is_terminated(builder_.getBlock())) {
                if (val) {
                    val = store_arm_result(val, result_type);
                    builder_.create<mlir::LLVM::StoreOp>(loc_, val, result_alloca);
                }
                builder_.create<mlir::cf::BranchOp>(loc_, merge_block);
            }
        }

        // Pattern irrefutability — single foundation `is_irrefutable_pattern`
        // in `lir_view`. The pre-foundation lambda here was a narrower copy
        // (missing Slice + Or arms) of the equivalent in mlir_gen_stmt.cpp;
        // hoisting closes the drift (logos-core 4.1). `ref r` / `ref mut r`
        // is irrefutable; a struct pat is irrefutable only when every field
        // sub is (`A { f: Inner::B(v) }` requires a real dispatch test).
        auto pat_irref = [](lir_view::PatRef p) -> bool {
            return lir_view::is_irrefutable_pattern(p);
        };
        bool is_wild = arm_pat_ref.kind() == pc::Code::Wild ||
                       arm_pat_ref.kind() == pc::Code::RefBind ||
                       (arm_pat_ref.kind() == pc::Code::Struct &&
                        pat_irref(arm_pat_ref));
        auto get_disc = [](lir_view::PatRef p) -> int64_t {
            switch (p.kind()) {
            case pc::Code::Variant:     return lir_view::PatVariantView{p}.disc();
            case pc::Code::VariantData: return lir_view::PatVariantDataView{p}.disc();
            case pc::Code::Int:         return lir_view::PatIntView{p}.value();
            case pc::Code::Bool:        return lir_view::PatBoolView{p}.value() ? 1 : 0;
            default: return 0;
            }
        };
        if (is_wild) {
            else_block = arm_entry;
        } else if (arm_pat_ref.kind() == pc::Code::Tuple &&
                   lir_view::PatTupleView{arm_pat_ref}.sub_count() > 0) {
            // [UNIFY D-tuple/expr] Route the refutable-tuple structural test
            // through the single pat_test foundation (was a duplicate inline
            // per-element loop). A tuple value IS a pointer to its inline
            // storage (G1 Rust by-value layout): pat_test's Tuple case GEPs
            // into the tuple base ptr directly (no load), so hand it `tptr`
            // (spilling a by-value scrut first) — NOT the legacy alloca-of-ptr
            // `tslot` wrapper that the old by-pointer load consumed.
            auto* test_block = new mlir::Block();
            region->push_back(test_block);
            {
                mlir::OpBuilder::InsertionGuard ig(builder_);
                builder_.setInsertionPointToStart(test_block);
                mlir::Value tptr;
                if (scrut.getType() == ptr_type()) {
                    tptr = scrut;
                } else {
                    auto tt = tuple_llvm_type(scrut_ty);
                    auto a = create_entry_alloca(tt ? tt : ptr_type());
                    builder_.create<mlir::LLVM::StoreOp>(loc_, scrut, a);
                    tptr = a;
                }
                mlir::Value cond = pat_test(arm_pat_ref, tptr, scrut_ty);
                builder_.create<mlir::cf::CondBranchOp>(loc_, cond, arm_entry, else_block);
            }
            else_block = test_block;
        } else if (arm_pat_ref.kind() == pc::Code::Struct) {
            // G148-1: struct arm with refutable field sub-patterns in
            // match-expression position (`Wrap { x: Inner::A(v), y } => …`).
            // pat_irref already routed fully-irrefutable structs to is_wild;
            // reaching here means a refutable field sub. Hand pat_test the
            // struct data ptr directly (same convention as Tuple) — no
            // alloca-of-pointer wrapper. pat_test's Struct case GEPs into
            // sptr to address fields; nested sub-Struct callers pass
            // `gep_field(parent, fname)` (inline child storage) the same way.
            auto* test_block = new mlir::Block();
            region->push_back(test_block);
            {
                mlir::OpBuilder::InsertionGuard ig(builder_);
                builder_.setInsertionPointToStart(test_block);
                mlir::Value sptr = scrut_ptr ? scrut_ptr : scrut;
                if (sptr && sptr.getType() != ptr_type()) {
                    auto a = create_entry_alloca(sptr.getType());
                    builder_.create<mlir::LLVM::StoreOp>(loc_, sptr, a);
                    sptr = a;
                }
                auto cond = pat_test(arm_pat_ref, sptr, scrut_ty);
                builder_.create<mlir::cf::CondBranchOp>(loc_, cond, arm_entry, else_block);
            }
            else_block = test_block;
        } else if (arm_pat_ref.kind() == pc::Code::Range) {
            // Range arm in match-as-expression. Same shape as the
            // match-stmt path (`lo <= scrut && scrut <= hi`) — was
            // missing here, so range arms silently fell through to
            // the wildcard via the `get_disc` default of 0.
            lir_view::PatRangeView pr{arm_pat_ref};
            auto* test_block = new mlir::Block();
            region->push_back(test_block);
            {
                mlir::OpBuilder::InsertionGuard ig(builder_);
                builder_.setInsertionPointToStart(test_block);
                auto both = emit_range_test(scrut, scrut_ty, pr.lo(), pr.hi());
                builder_.create<mlir::cf::CondBranchOp>(loc_, both, arm_entry, else_block);
            }
            else_block = test_block;
        } else if (arm_pat_ref.kind() == pc::Code::Or) {
            std::vector<lir_view::PatRef> alts;
            lir_view::PatOrView{arm_pat_ref}.each_alt([&](lir_view::PatRef a){ alts.push_back(a); });
            mlir::Block* cur_else = else_block;
            for (int64_t ai = static_cast<int64_t>(alts.size()) - 1; ai >= 0; --ai) {
                auto alt = alts[static_cast<size_t>(ai)];
                auto* test_block = new mlir::Block();
                region->push_back(test_block);
                mlir::OpBuilder::InsertionGuard ig(builder_);
                builder_.setInsertionPointToStart(test_block);
                // G144-2: a wildcard / binding alt (`0 | _`) is irrefutable —
                // it matches unconditionally. Emit an unconditional branch to
                // the arm rather than a bogus `scrut == get_disc(Wild)=0` test
                // (which both mis-matched and left a dead-block arith.constant
                // that failed LLVM translation).
                if (alt.kind() == pc::Code::Wild ||
                    alt.kind() == pc::Code::RefBind) {
                    builder_.create<mlir::cf::BranchOp>(loc_, arm_entry);
                    cur_else = test_block;
                    continue;
                }
                int64_t disc = get_disc(alt);
                auto disc_val = coerce_int(
                    builder_.create<mlir::arith::ConstantIntOp>(loc_, disc, 64), scrut_type);
                auto eq = builder_.create<mlir::arith::CmpIOp>(
                    loc_, mlir::arith::CmpIPredicate::eq, scrut, disc_val);
                builder_.create<mlir::cf::CondBranchOp>(loc_, eq, arm_entry, cur_else);
                cur_else = test_block;
            }
            else_block = cur_else;
        } else if (arm_pat_ref.kind() == pc::Code::Slice &&
                   scrut_ty &&
                   TypeRef(scrut_ty).kind() == LogosType::Kind::Array) {
            // P4-pm-04 (match-as-expression): mirror of mlir_gen_stmt
            // slice-pattern handling. Spill scrut to alloca if it isn't
            // already a pointer; GEP each refutable sub-element and
            // AND-chain equality tests. Trailing `..` absorbs the rest.
            lir_view::PatSliceView sv{arm_pat_ref};
            TypeRef atyp = scrut_ty;
            auto elem_mlir = logos_to_mlir(TypeRef(atyp).elem());
            auto arr_mlir  = logos_to_mlir(atyp);
            size_t total   = (size_t)TypeRef(atyp).arr_size();
            size_t suf_n   = sv.suffix_count();
            auto* test_block = new mlir::Block();
            region->push_back(test_block);
            {
                mlir::OpBuilder::InsertionGuard ig(builder_);
                builder_.setInsertionPointToStart(test_block);
                mlir::Value aptr;
                if (scrut.getType() == ptr_type()) {
                    aptr = scrut;
                } else if (arr_mlir) {
                    auto a = create_entry_alloca(arr_mlir);
                    builder_.create<mlir::LLVM::StoreOp>(loc_, scrut, a);
                    aptr = a;
                }
                mlir::Value cond =
                    builder_.create<mlir::arith::ConstantIntOp>(loc_, 1, 1);
                auto chk_at = [&](lir_view::PatRef sp, int32_t idx) {
                    if (!sp || sp.kind() == pc::Code::Wild) return;
                    int64_t sub_val = 0;
                    if      (sp.kind() == pc::Code::Int)  sub_val = lir_view::PatIntView{sp}.value();
                    else if (sp.kind() == pc::Code::Bool) sub_val = lir_view::PatBoolView{sp}.value() ? 1 : 0;
                    else return;
                    if (!elem_mlir || !arr_mlir || !aptr) return;
                    llvm::SmallVector<mlir::LLVM::GEPArg> gi{int32_t(0), idx};
                    auto ep = builder_.create<mlir::LLVM::GEPOp>(
                        loc_, ptr_type(), arr_mlir, aptr, gi);
                    auto ev = builder_.create<mlir::LLVM::LoadOp>(loc_, elem_mlir, ep);
                    auto cv = coerce_int(
                        builder_.create<mlir::arith::ConstantIntOp>(loc_, sub_val, 64),
                        elem_mlir);
                    auto eq = builder_.create<mlir::arith::CmpIOp>(
                        loc_, mlir::arith::CmpIPredicate::eq, ev, cv);
                    cond = builder_.create<mlir::arith::AndIOp>(loc_, cond, eq);
                };
                int32_t idx = 0;
                sv.each_prefix([&](lir_view::PatRef sp){ chk_at(sp, idx++); });
                int32_t sidx = (int32_t)(total - suf_n);
                sv.each_suffix([&](lir_view::PatRef sp){ chk_at(sp, sidx++); });
                builder_.create<mlir::cf::CondBranchOp>(loc_, cond, arm_entry, else_block);
            }
            else_block = test_block;
        } else if (arm_pat_ref.kind() == pc::Code::Slice &&
                   scrut_ty &&
                   TypeRef(scrut_ty).kind() == LogosType::Kind::Slice &&
                   TypeRef(scrut_ty).elem()) {
            // Dynamic-slice refutable pattern (`[a, b]`, `[a, ..]`) over a
            // runtime-length `&[T]` scrutinee. The slice value is a ptr to
            // {data_ptr, i64 len}. A fixed-length pattern is refutable: gate
            // on `len == N` (no rest) or `len >= N` (trailing `..`), then
            // AND-chain literal-element equality through the data pointer.
            // Suffix-after-`..` is rejected by sema for dynamic slices, so
            // only the prefix carries element constraints. Bindings are
            // emitted separately by extract_arm_payload's Slice case.
            lir_view::PatSliceView sv{arm_pat_ref};
            auto elem_mlir = logos_to_mlir(TypeRef(scrut_ty).elem());
            auto stype     = slice_llvm_type();
            std::vector<lir_view::PatRef> prefix;
            sv.each_prefix([&](lir_view::PatRef sp){ prefix.push_back(sp); });
            // G167-6a: suffix elements after `..` are gated/checked from the
            // runtime length (`len - suf_n + i`).
            std::vector<lir_view::PatRef> suffix;
            sv.each_suffix([&](lir_view::PatRef sp){ suffix.push_back(sp); });
            bool has_rest = (bool)sv.rest();
            auto* test_block = new mlir::Block();
            region->push_back(test_block);
            {
                mlir::OpBuilder::InsertionGuard ig(builder_);
                builder_.setInsertionPointToStart(test_block);
                mlir::Value sptr = scrut.getType() == ptr_type() ? scrut : nullptr;
                if (!sptr && stype) {
                    auto a = create_entry_alloca(stype);
                    builder_.create<mlir::LLVM::StoreOp>(loc_, scrut, a);
                    sptr = a;
                }
                if (sptr) {
                    llvm::SmallVector<mlir::LLVM::GEPArg> li{int32_t(0), int32_t(1)};
                    auto lp = builder_.create<mlir::LLVM::GEPOp>(loc_, ptr_type(), stype, sptr, li);
                    auto len = builder_.create<mlir::LLVM::LoadOp>(loc_, builder_.getI64Type(), lp);
                    auto nconst = builder_.create<mlir::arith::ConstantIntOp>(
                        loc_, (int64_t)(prefix.size() + suffix.size()), 64);
                    // With a rest `..`, the slice may be longer (len >= fixed);
                    // without it, length must equal the fixed element count.
                    auto pred = has_rest ? mlir::arith::CmpIPredicate::sge
                                         : mlir::arith::CmpIPredicate::eq;
                    mlir::Value cond = builder_.create<mlir::arith::CmpIOp>(loc_, pred, len, nconst);
                    llvm::SmallVector<mlir::LLVM::GEPArg> di{int32_t(0), int32_t(0)};
                    auto dp = builder_.create<mlir::LLVM::GEPOp>(loc_, ptr_type(), stype, sptr, di);
                    auto data = builder_.create<mlir::LLVM::LoadOp>(loc_, ptr_type(), dp);
                    auto chk = [&](lir_view::PatRef sp, mlir::Value idx) {
                        if (!sp || sp.kind() == pc::Code::Wild) return;
                        int64_t sub_val = 0;
                        if      (sp.kind() == pc::Code::Int)  sub_val = lir_view::PatIntView{sp}.value();
                        else if (sp.kind() == pc::Code::Bool) sub_val = lir_view::PatBoolView{sp}.value() ? 1 : 0;
                        else return;
                        llvm::SmallVector<mlir::LLVM::GEPArg> gi{idx};
                        auto ep = builder_.create<mlir::LLVM::GEPOp>(loc_, ptr_type(), elem_mlir, data, gi);
                        auto ev = builder_.create<mlir::LLVM::LoadOp>(loc_, elem_mlir, ep);
                        auto cv = coerce_int(
                            builder_.create<mlir::arith::ConstantIntOp>(loc_, sub_val, 64), elem_mlir);
                        auto eq = builder_.create<mlir::arith::CmpIOp>(
                            loc_, mlir::arith::CmpIPredicate::eq, ev, cv);
                        cond = builder_.create<mlir::arith::AndIOp>(loc_, cond, eq);
                    };
                    if (elem_mlir) {
                        for (size_t i = 0; i < prefix.size(); ++i)
                            chk(prefix[i], builder_.create<mlir::arith::ConstantIntOp>(
                                loc_, (int64_t)i, 64));
                        // Suffix index = len - suf_n + i (from the tail).
                        for (size_t i = 0; i < suffix.size(); ++i) {
                            auto off = builder_.create<mlir::arith::ConstantIntOp>(
                                loc_, (int64_t)(suffix.size() - i), 64);
                            mlir::Value idx = builder_.create<mlir::arith::SubIOp>(loc_, len, off);
                            chk(suffix[i], idx);
                        }
                    }
                    builder_.create<mlir::cf::CondBranchOp>(loc_, cond, arm_entry, else_block);
                } else {
                    builder_.create<mlir::cf::BranchOp>(loc_, else_block);
                }
            }
            else_block = test_block;
        } else if (arm_pat_ref.kind() == pc::Code::RefPat &&
                   scrut_ty &&
                   (TypeRef(scrut_ty).kind() == LogosType::Kind::Ref ||
                    TypeRef(scrut_ty).kind() == LogosType::Kind::MutRef) &&
                   TypeRef(scrut_ty).pointee()) {
            // P4-pm-18 (match-as-expr): `match &T { &<scalar> => … }`.
            // Deref scrut and cmp against the inner disc.
            auto inner = lir_view::PatRefPatView{arm_pat_ref}.inner();
            int64_t disc = inner ? get_disc(inner) : 0;
            auto* test_block = new mlir::Block();
            region->push_back(test_block);
            {
                mlir::OpBuilder::InsertionGuard ig(builder_);
                builder_.setInsertionPointToStart(test_block);
                auto pointee_t = TypeRef(scrut_ty).pointee();
                auto elem_mlir = logos_to_mlir(pointee_t);
                if (!elem_mlir) elem_mlir = builder_.getI32Type();
                auto loaded = builder_.create<mlir::LLVM::LoadOp>(
                    loc_, elem_mlir, scrut);
                auto disc_val = coerce_int(
                    builder_.create<mlir::arith::ConstantIntOp>(loc_, disc, 64),
                    elem_mlir);
                auto eq = builder_.create<mlir::arith::CmpIOp>(
                    loc_, mlir::arith::CmpIPredicate::eq, loaded, disc_val);
                builder_.create<mlir::cf::CondBranchOp>(loc_, eq, arm_entry, else_block);
            }
            else_block = test_block;
        } else if (arm_pat_ref.kind() == pc::Code::At) {
            // `name @ sub` (match-as-expr): dispatch on the sub-pattern.
            // Mirrors the match-statement path. Without this, an At arm fell
            // into the catch-all below and was dispatched as `scrut == 0`
            // (get_disc default), so `e @ 1..=100 => …` only matched scrut==0.
            auto sub = lir_view::PatAtView{arm_pat_ref}.sub();
            if (sub && sub.kind() == pc::Code::Range) {
                lir_view::PatRangeView pr{sub};
                auto* test_block = new mlir::Block();
                region->push_back(test_block);
                {
                    mlir::OpBuilder::InsertionGuard ig(builder_);
                    builder_.setInsertionPointToStart(test_block);
                    auto both = emit_range_test(scrut, scrut_ty, pr.lo(), pr.hi());
                    builder_.create<mlir::cf::CondBranchOp>(loc_, both, arm_entry, else_block);
                }
                else_block = test_block;
            } else if (sub && (sub.kind() == pc::Code::Int ||
                               sub.kind() == pc::Code::Bool ||
                               sub.kind() == pc::Code::Variant ||
                               sub.kind() == pc::Code::VariantData)) {
                int64_t disc = get_disc(sub);
                auto* test_block = new mlir::Block();
                region->push_back(test_block);
                {
                    mlir::OpBuilder::InsertionGuard ig(builder_);
                    builder_.setInsertionPointToStart(test_block);
                    auto disc_val = coerce_int(
                        builder_.create<mlir::arith::ConstantIntOp>(loc_, disc, 64), scrut_type);
                    auto eq = builder_.create<mlir::arith::CmpIOp>(
                        loc_, mlir::arith::CmpIPredicate::eq, scrut, disc_val);
                    builder_.create<mlir::cf::CondBranchOp>(loc_, eq, arm_entry, else_block);
                }
                else_block = test_block;
            } else {
                // Irrefutable sub-pattern (e.g. `n @ _`) — arm always runs.
                else_block = arm_entry;
            }
        } else {
            int64_t disc = get_disc(arm_pat_ref);

            auto* test_block = new mlir::Block();
            region->push_back(test_block);
            {
                mlir::OpBuilder::InsertionGuard ig(builder_);
                builder_.setInsertionPointToStart(test_block);
                auto disc_val = coerce_int(
                    builder_.create<mlir::arith::ConstantIntOp>(loc_, disc, 64),
                    scrut_type);
                auto eq = builder_.create<mlir::arith::CmpIOp>(
                    loc_, mlir::arith::CmpIPredicate::eq, scrut, disc_val);
                builder_.create<mlir::cf::CondBranchOp>(loc_, eq, arm_entry, else_block);
            }
            else_block = test_block;
        }
    }

    builder_.create<mlir::cf::BranchOp>(loc_, else_block);
    region->push_back(merge_block);
    builder_.setInsertionPointToStart(merge_block);
    if (!result_type) {
        // Void match: synthetic unit value so callers don't deref nullptr
        // (mirrors gen_if's void merge). All effects were emitted in the arms.
        return builder_.create<mlir::arith::ConstantIntOp>(loc_, 0, 32);
    }
    return builder_.create<mlir::LLVM::LoadOp>(loc_, result_type, result_alloca);
}

// ---------------------------------------------------------------------------
// Closure call
// ---------------------------------------------------------------------------

mlir::Value MLIRGenImpl::gen_expr_kind(lir_view::EClosureBoxView v, TypeRef type) {
    return gen_closure(v, type);
}

mlir::Value MLIRGenImpl::gen_expr_kind(lir_view::EClosureCallView v, TypeRef type) {
    if (!v.callee()) return nullptr;
    auto closure = gen_expr(v.callee());
    if (!closure) return nullptr;

    auto ctype = closure_llvm_type();
    // Load fn_ptr from field 0
    llvm::SmallVector<mlir::LLVM::GEPArg> fi{int32_t(0), int32_t(0)};
    auto fp = builder_.create<mlir::LLVM::GEPOp>(loc_, ptr_type(), ctype, closure, fi);
    auto fn_ptr = builder_.create<mlir::LLVM::LoadOp>(loc_, ptr_type(), fp);
    // Load env_ptr from field 1
    llvm::SmallVector<mlir::LLVM::GEPArg> ei{int32_t(0), int32_t(1)};
    auto ep = builder_.create<mlir::LLVM::GEPOp>(loc_, ptr_type(), ctype, closure, ei);
    auto env_ptr = builder_.create<mlir::LLVM::LoadOp>(loc_, ptr_type(), ep);

    // Build args: env_ptr first, then user args
    llvm::SmallVector<mlir::Value> args;
    args.push_back(env_ptr);

    // Build LLVM function type for indirect call
    llvm::SmallVector<mlir::Type> param_types;
    param_types.push_back(ptr_type());  // env
    bool arg_failed = false;
    v.each_arg([&](lir_view::ExprRef ar) {
        if (arg_failed) return;
        if (!ar) { arg_failed = true; return; }
        auto val = gen_expr(ar);
        if (!val) { arg_failed = true; return; }
        args.push_back(val);
        param_types.push_back(val.getType());
    });
    if (arg_failed) return nullptr;

    // See EFnPtrCall for the struct-return ABI rationale.
    mlir::Type ret = fn_call_ret_llvm_type(type);
    if (!ret) ret = mlir::LLVM::LLVMVoidType::get(builder_.getContext());
    bool is_void = mlir::isa<mlir::LLVM::LLVMVoidType>(ret);
    auto llvm_fn_type = mlir::LLVM::LLVMFunctionType::get(ret, param_types, false);

    // Indirect call via function pointer
    llvm::SmallVector<mlir::Value> all_operands;
    all_operands.push_back(fn_ptr);
    all_operands.append(args.begin(), args.end());
    auto call = builder_.create<mlir::LLVM::CallOp>(
        loc_, llvm_fn_type, mlir::FlatSymbolRefAttr{},
        mlir::ValueRange(all_operands));
    if (is_void) return nullptr;
    auto result = call.getResult();
    if (mlir::isa<mlir::LLVM::LLVMStructType>(ret))
        return spill_to_alloca(result);
    return result;
}

mlir::Value MLIRGenImpl::gen_expr_kind(lir_view::EFnPtrCallView v, TypeRef type) {
    // Bare function pointer call: fn_ptr(arg1, arg2, ...) — no env_ptr.
    if (!v.callee()) return nullptr;
    auto fn_ptr = gen_expr(v.callee());
    if (!fn_ptr) return nullptr;

    // fn_ptr is stored as a scalar (not in an alloca) when it's a let var;
    // but scope_ stores allocas for let-bound scalars, so load it first.
    // Actually FnPtr variables are stored as scalars (like integers) — load from alloca.
    // (fn_ptr here is the raw pointer value, already loaded by gen_expr_kind(EVarRef))

    llvm::SmallVector<mlir::Value> args;
    llvm::SmallVector<mlir::Type> param_types;
    bool arg_failed = false;
    v.each_arg([&](lir_view::ExprRef ar) {
        if (arg_failed) return;
        if (!ar) { arg_failed = true; return; }
        auto val = gen_expr(ar);
        if (!val) { arg_failed = true; return; }
        args.push_back(val);
        param_types.push_back(val.getType());
    });
    if (arg_failed) return nullptr;

    // Return type must match the callee's ABI — tuples/structs/enums are
    // returned by aggregate value (the callee uses sret promotion by the LLVM
    // backend). Using logos_to_mlir(struct) would yield `ptr`, producing a
    // call type that disagrees with the callee and breaks argument passing
    // (rdi becomes the first real arg instead of the hidden sret slot).
    mlir::Type ret = fn_call_ret_llvm_type(type);
    if (!ret) ret = mlir::LLVM::LLVMVoidType::get(builder_.getContext());
    bool is_void = mlir::isa<mlir::LLVM::LLVMVoidType>(ret);
    auto llvm_fn_type = mlir::LLVM::LLVMFunctionType::get(ret, param_types, false);

    llvm::SmallVector<mlir::Value> all_operands;
    all_operands.push_back(fn_ptr);
    all_operands.append(args.begin(), args.end());
    auto call = builder_.create<mlir::LLVM::CallOp>(
        loc_, llvm_fn_type, mlir::FlatSymbolRefAttr{},
        mlir::ValueRange(all_operands));
    if (is_void) return nullptr;
    auto result = call.getResult();
    // If the return is an aggregate (struct/tuple/enum), spill to alloca so
    // the rest of codegen — which expects struct values as ptr — can work.
    if (mlir::isa<mlir::LLVM::LLVMStructType>(ret))
        return spill_to_alloca(result);
    return result;
}

// ---------------------------------------------------------------------------
// Slice helpers
// ---------------------------------------------------------------------------

// RefRepr op: build a reference value from its data + metadata halves
// (from_raw_parts). FatDyn spills a {data,vtable} pair; FatSlice/FatCustomDst a
// {data,len} pair; thin returns the data pointer. Mirrors the old slice_lit.
mlir::Value MLIRGenImpl::repr_construct(RefReprKind k, mlir::Value data, mlir::Value meta) {
    if (k == RefReprKind::ThinPtr || k == RefReprKind::NotARef) return data;
    bool is_dyn = (k == RefReprKind::FatDyn);
    auto stype = is_dyn ? dyn_llvm_type() : slice_llvm_type();
    auto alloca = create_entry_alloca(stype);
    // field 0 = data ptr
    llvm::SmallVector<mlir::LLVM::GEPArg> pi{int32_t(0), int32_t(0)};
    auto pp = builder_.create<mlir::LLVM::GEPOp>(loc_, ptr_type(), stype, alloca, pi);
    builder_.create<mlir::LLVM::StoreOp>(loc_, data, pp);
    // field 1 = metadata: vtable (ptr) for dyn, length (i64) otherwise.
    llvm::SmallVector<mlir::LLVM::GEPArg> li{int32_t(0), int32_t(1)};
    auto lp = builder_.create<mlir::LLVM::GEPOp>(loc_, ptr_type(), stype, alloca, li);
    if (is_dyn) {
        mlir::Value vt = meta;
        if (vt.getType() != ptr_type())
            vt = builder_.create<mlir::LLVM::IntToPtrOp>(
                loc_, ptr_type(), coerce_int(vt, builder_.getI64Type()));
        builder_.create<mlir::LLVM::StoreOp>(loc_, vt, lp);
    } else {
        auto len64 = coerce_int(meta, builder_.getI64Type());
        builder_.create<mlir::LLVM::StoreOp>(loc_, len64, lp);
    }
    return alloca;
}

// Enum representation access (Phase 3.5 chokepoint). Tagged: `{i32 disc,
// payload}`. Niche-packed (Option<&T>-shape): no disc word — the payload (a
// non-null pointer) starts at offset 0 and null encodes the `none` variant.
mlir::Value MLIRGenImpl::enum_payload_ptr(mlir::Value enum_addr,
                                          const TaggedEnumInfo& info) {
    // LowBit niche — READ side (the construct encodes inline and returns before
    // reaching here). Decode by the RUNTIME low bit into a temp the binding loop
    // reads as the variant payload field: value arm (lo=1) → `word>>1` (signed →
    // arithmetic), pointer arm (lo=0) → the word itself (the aligned pointer).
    if (info.niche.packed && info.niche.kind == TaggedEnumInfo::Niche::LowBit) {
        // RAW mode (WAny Pod(u64)): both arms read the word VERBATIM — Pod is the
        // raw tagged word, Ref is the raw pointer — so the payload IS the slot
        // (no decode). The WAny accessors decode value/code from the word.
        if (info.niche.val_raw) return enum_addr;
        // value arm (low bit 1) → read from a temp holding `word>>1` (signed →
        // arithmetic shift); pointer arm (low bit 0) → read from the enum slot
        // itself (the word IS the aligned pointer — mirrors the null-pointer-niche
        // binding, which works). Runtime-select the payload address by the low bit
        // so BOTH binding shapes (load-scalar, &Struct) behave as for a normal slot.
        auto w   = builder_.create<mlir::LLVM::LoadOp>(loc_, builder_.getI64Type(), enum_addr);
        auto one = builder_.create<mlir::arith::ConstantIntOp>(loc_, 1, 64);
        auto z   = builder_.create<mlir::arith::ConstantIntOp>(loc_, 0, 64);
        auto lo  = builder_.create<mlir::LLVM::AndOp>(loc_, w, one);
        auto isval = builder_.create<mlir::LLVM::ICmpOp>(
            loc_, mlir::LLVM::ICmpPredicate::ne, lo, z);
        mlir::Value sh = info.niche.val_signed
            ? builder_.create<mlir::LLVM::AShrOp>(loc_, w, one).getResult()
            : builder_.create<mlir::LLVM::LShrOp>(loc_, w, one).getResult();
        auto tmp = create_entry_alloca(builder_.getI64Type());
        builder_.create<mlir::LLVM::StoreOp>(loc_, sh, tmp);
        return builder_.create<mlir::LLVM::SelectOp>(loc_, isval, tmp, enum_addr);
    }
    // Other niche-packed (null-pointer): the payload IS the enum storage (offset 0).
    if (info.niche.packed) return enum_addr;
    llvm::SmallVector<mlir::LLVM::GEPArg> i{int32_t(0), int32_t(1)};
    return builder_.create<mlir::LLVM::GEPOp>(loc_, ptr_type(), info.llvm_type,
                                              enum_addr, i);
}
void MLIRGenImpl::enum_store_disc(mlir::Value enum_addr, const TaggedEnumInfo& info,
                                  int64_t disc) {
    // LowBit: the disc IS the payload word's low bit — encoded by the construct's
    // payload store (ptr arm raw / value arm `(v<<1)|1`), nothing separate here.
    if (info.niche.packed && info.niche.kind == TaggedEnumInfo::Niche::LowBit)
        return;
    if (info.niche.packed) {
        // The `none` variant writes the niche value (null) at offset 0; the
        // data (`some`) variant writes nothing here — its payload store places
        // the non-null pointer, which IS the discriminant.
        if (disc == info.niche.none_disc) {
            auto nullp = builder_.create<mlir::LLVM::ZeroOp>(loc_, ptr_type());
            builder_.create<mlir::LLVM::StoreOp>(loc_, nullp, enum_addr);
        }
        return;
    }
    llvm::SmallVector<mlir::LLVM::GEPArg> i{int32_t(0), int32_t(0)};
    auto dp = builder_.create<mlir::LLVM::GEPOp>(loc_, ptr_type(), info.llvm_type,
                                                 enum_addr, i);
    auto dv = builder_.create<mlir::arith::ConstantIntOp>(loc_, disc, 32);
    builder_.create<mlir::LLVM::StoreOp>(loc_, dv, dp);
}
void MLIRGenImpl::enum_store_disc_value(mlir::Value enum_addr,
                                        const TaggedEnumInfo& info,
                                        mlir::Value disc_val) {
    if (info.niche.packed) {
        // Untyped-None reassign on a niche enum: the only nullary variant is
        // `none`, so encode it as the niche value (null) at offset 0.
        auto nullp = builder_.create<mlir::LLVM::ZeroOp>(loc_, ptr_type());
        builder_.create<mlir::LLVM::StoreOp>(loc_, nullp, enum_addr);
        return;
    }
    llvm::SmallVector<mlir::LLVM::GEPArg> i{int32_t(0), int32_t(0)};
    auto dp = builder_.create<mlir::LLVM::GEPOp>(loc_, ptr_type(), info.llvm_type,
                                                 enum_addr, i);
    builder_.create<mlir::LLVM::StoreOp>(loc_, disc_val, dp);
}
mlir::Value MLIRGenImpl::enum_load_disc(mlir::Value enum_addr,
                                        const TaggedEnumInfo& info) {
    if (info.niche.packed && info.niche.kind == TaggedEnumInfo::Niche::LowBit) {
        // Decode: load the word; low bit 0 → ptr arm, low bit 1 → value arm.
        auto w   = builder_.create<mlir::LLVM::LoadOp>(loc_, builder_.getI64Type(), enum_addr);
        auto one = builder_.create<mlir::arith::ConstantIntOp>(loc_, 1, 64);
        auto lo  = builder_.create<mlir::LLVM::AndOp>(loc_, w, one);
        auto z64 = builder_.create<mlir::arith::ConstantIntOp>(loc_, 0, 64);
        auto is_val = builder_.create<mlir::LLVM::ICmpOp>(
            loc_, mlir::LLVM::ICmpPredicate::ne, lo, z64);
        auto ptr_c = builder_.create<mlir::arith::ConstantIntOp>(loc_, info.niche.ptr_disc, 32);
        auto val_c = builder_.create<mlir::arith::ConstantIntOp>(loc_, info.niche.val_disc, 32);
        return builder_.create<mlir::LLVM::SelectOp>(loc_, is_val, val_c, ptr_c);
    }
    if (info.niche.packed) {
        // Decode: load the niche pointer; null → none_disc, non-null → some_disc.
        auto p = builder_.create<mlir::LLVM::LoadOp>(loc_, ptr_type(), enum_addr);
        auto nullp = builder_.create<mlir::LLVM::ZeroOp>(loc_, ptr_type());
        auto is_none = builder_.create<mlir::LLVM::ICmpOp>(
            loc_, mlir::LLVM::ICmpPredicate::eq, p, nullp);
        auto none_c = builder_.create<mlir::arith::ConstantIntOp>(
            loc_, info.niche.none_disc, 32);
        auto some_c = builder_.create<mlir::arith::ConstantIntOp>(
            loc_, info.niche.some_disc, 32);
        return builder_.create<mlir::LLVM::SelectOp>(loc_, is_none, none_c, some_c);
    }
    llvm::SmallVector<mlir::LLVM::GEPArg> i{int32_t(0), int32_t(0)};
    auto dp = builder_.create<mlir::LLVM::GEPOp>(loc_, ptr_type(), info.llvm_type,
                                                 enum_addr, i);
    return builder_.create<mlir::LLVM::LoadOp>(loc_, builder_.getI32Type(), dp);
}

// True iff a DstRef's pointee struct has a literal `[T]` slice tail (genuinely
// 16-byte fat). A dyn-tail / TypeVar-tail DstRef is physically thin. See header.
bool MLIRGenImpl::dstref_has_slice_tail(TypeRef t) {
    if (!t || TypeRef(t).kind() != LogosType::Kind::DstRef) return false;
    std::string nm(TypeRef(t).struct_name());
    auto it = all_struct_defs_.find(nm);
    if (it == all_struct_defs_.end() || !it->second.valid() || it->second.fields().empty())
        return false;
    auto lk = TypeRef(it->second.fields().back().type(pool_impl())).kind();
    return lk == LogosType::Kind::Slice || lk == LogosType::Kind::UnsizedSlice;
}

// True iff `t` is a DstRef whose pointee struct is `#[self_describing]`. Such a
// ref is PHYSICALLY THIN (8B = pointer straight to the header; the tail length
// is recovered in-band via `dst_len`), unlike a plain `[T]`-tail DstRef which is
// an 8B ptr to a 16-byte {data,len} pair. CRUCIAL for RETURNING a `&Foo`: a fat
// ref's pair lives in a callee stack alloca and dangles on return, whereas a thin
// ref IS the heap header pointer. The single discriminator routing every
// data/len-extraction + repr/store/access site onto the thin path.
// See docs/internals/self-describing-dst-thin-ref.md.
bool MLIRGenImpl::dstref_pointee_self_describing(TypeRef t) {
    if (!t || TypeRef(t).kind() != LogosType::Kind::DstRef) return false;
    // A monomorphized generic DST is keyed in all_struct_defs_ by its CONCRETE
    // name (`GBlock$G1$i64`), not the bare template name (`GBlock`) that
    // struct_name() returns — so look up the concrete name first (as emit_dst_len
    // does for dst_len resolution), then fall back to the bare name (non-generic).
    auto targs = TypeRef(t).type_args();
    std::vector<TypeRef> targ_vec(targs.begin(), targs.end());
    std::string concrete_pkg(TypeRef(t).pkg_name());
    std::string concrete = concrete_struct_name_raw(
        std::string(TypeRef(t).struct_name()), targ_vec, concrete_pkg);
    for (const std::string& nm : {concrete, std::string(TypeRef(t).struct_name())}) {
        auto it = all_struct_defs_.find(nm);
        if (it != all_struct_defs_.end() && it->second.valid())
            return it->second.self_describing();
    }
    return false;
}

// F3 (ref-repr-design §8): the storage↔compute bridge for a `#[zoned2]` niche
// enum — the compiler-owned generalization of writ's wa_materialize/wa_lower.
// The at-rest slot holds the 8-byte niche word with the Ref arm SELF-RELATIVE
// (anchor = the slot's own address); the compute value is a by-pointer enum
// (a fresh alloca holding the word with the Ref arm ABSOLUTE).
//
//   word r:  r==0 → null;  r&1==1 → Pod (position-independent, copied raw);
//            else Ref → absolute = slot + r  (materialize) / delta = val − slot
//            (lower). The Pod/null arms are identity; only the Ref arm shifts.
mlir::Value MLIRGenImpl::zoned_enum_materialize(mlir::Value slot) {
    auto r    = builder_.create<mlir::LLVM::LoadOp>(loc_, builder_.getI64Type(), slot);
    auto zero = builder_.create<mlir::arith::ConstantIntOp>(loc_, 0, 64);
    auto one  = builder_.create<mlir::arith::ConstantIntOp>(loc_, 1, 64);
    auto lo   = builder_.create<mlir::LLVM::AndOp>(loc_, r, one);
    auto is_pod  = builder_.create<mlir::LLVM::ICmpOp>(
        loc_, mlir::LLVM::ICmpPredicate::ne, lo, zero);
    auto is_null = builder_.create<mlir::LLVM::ICmpOp>(
        loc_, mlir::LLVM::ICmpPredicate::eq, r, zero);
    auto slot_int = builder_.create<mlir::LLVM::PtrToIntOp>(loc_, builder_.getI64Type(), slot);
    auto abs = builder_.create<mlir::LLVM::AddOp>(loc_, slot_int, r);        // Ref → slot + delta
    auto pod_or_ref = builder_.create<mlir::LLVM::SelectOp>(loc_, is_pod, mlir::Value(r), mlir::Value(abs));
    auto word = builder_.create<mlir::LLVM::SelectOp>(loc_, is_null, mlir::Value(zero), mlir::Value(pod_or_ref));
    auto a = create_entry_alloca(builder_.getI64Type());
    builder_.create<mlir::LLVM::StoreOp>(loc_, word, a);
    return a;   // by-pointer enum value: a ptr to the absolute word
}
void MLIRGenImpl::zoned_enum_lower(mlir::Value val, mlir::Value slot) {
    auto w    = builder_.create<mlir::LLVM::LoadOp>(loc_, builder_.getI64Type(), val);
    auto zero = builder_.create<mlir::arith::ConstantIntOp>(loc_, 0, 64);
    auto one  = builder_.create<mlir::arith::ConstantIntOp>(loc_, 1, 64);
    auto lo   = builder_.create<mlir::LLVM::AndOp>(loc_, w, one);
    auto is_pod  = builder_.create<mlir::LLVM::ICmpOp>(
        loc_, mlir::LLVM::ICmpPredicate::ne, lo, zero);
    auto is_null = builder_.create<mlir::LLVM::ICmpOp>(
        loc_, mlir::LLVM::ICmpPredicate::eq, w, zero);
    auto slot_int = builder_.create<mlir::LLVM::PtrToIntOp>(loc_, builder_.getI64Type(), slot);
    auto delta = builder_.create<mlir::LLVM::SubOp>(loc_, w, slot_int);      // Ref → val − slot
    auto pod_or_ref = builder_.create<mlir::LLVM::SelectOp>(loc_, is_pod, mlir::Value(w), mlir::Value(delta));
    auto stored = builder_.create<mlir::LLVM::SelectOp>(loc_, is_null, mlir::Value(zero), mlir::Value(pod_or_ref));
    builder_.create<mlir::LLVM::StoreOp>(loc_, stored, slot);
}

const TaggedEnumInfo* MLIRGenImpl::zoned_niche_enum_info(TypeRef t) {
    if (!t || t.kind() != LogosType::Kind::Enum) return nullptr;
    auto* info = resolve_tagged_enum(std::string(t.enum_name()), t);
    if (info && info->zoned && info->niche.packed) return info;
    return nullptr;
}

// RefRepr op: storage slot -> compute value. See header for the convention.
mlir::Value MLIRGenImpl::repr_materialize(RefReprKind k, mlir::Value slot) {
    if (k == RefReprKind::NotARef) return slot;
    if (k == RefReprKind::ThinPtr)
        return builder_.create<mlir::LLVM::LoadOp>(loc_, ptr_type(), slot);
    if (k == RefReprKind::RelOffset) {
        // Self-relative: load the i64 byte offset stored AT the slot, then GEP the
        // slot's own address by it → absolute thin ptr. `slot` IS the anchor. A
        // null target stored as off = −slot materialises back to address 0.
        auto off = builder_.create<mlir::LLVM::LoadOp>(loc_, builder_.getI64Type(), slot);
        llvm::SmallVector<mlir::LLVM::GEPArg> idx{mlir::Value(off)};
        return builder_.create<mlir::LLVM::GEPOp>(loc_, ptr_type(),
                                                  builder_.getI8Type(), slot, idx);
    }
    // Fat (always-16B pair): the value is the storage address.
    return slot;
}

// RefRepr op: compute value -> storage slot. See header for the convention.
void MLIRGenImpl::repr_lower(RefReprKind k, mlir::Value val, mlir::Value slot) {
    if (k == RefReprKind::ThinPtr || k == RefReprKind::NotARef) {
        builder_.create<mlir::LLVM::StoreOp>(loc_, val, slot);
        return;
    }
    if (k == RefReprKind::RelOffset) {
        // Self-relative: store the i64 byte offset (target − slot) AT the slot.
        // `slot` is the anchor; a null `val` stores −slot, which materialises to 0.
        auto val_int  = builder_.create<mlir::LLVM::PtrToIntOp>(
            loc_, builder_.getI64Type(), val);
        auto slot_int = builder_.create<mlir::LLVM::PtrToIntOp>(
            loc_, builder_.getI64Type(), slot);
        auto off = builder_.create<mlir::arith::SubIOp>(loc_, val_int, slot_int);
        builder_.create<mlir::LLVM::StoreOp>(loc_, off, slot);
        return;
    }
    // Fat: copy the 16-byte {data, meta} pair (val points at the source pair).
    auto sz = builder_.create<mlir::LLVM::ConstantOp>(
        loc_, builder_.getI64Type(), builder_.getI64IntegerAttr(16));
    builder_.create<mlir::LLVM::MemcpyOp>(loc_, slot, val, sz, /*isVolatile=*/false);
}

// Recover the tail length of a #[self_describing] DST from its THIN header
// pointer by calling its `SelfDescribing::dst_len` — the in-band metadata of a
// thin self_describing DstRef (whose physical value IS this header pointer). Used
// as the `meta` half wherever a slice/len is projected off such a ref (the thin
// counterpart of repr_meta's fat-pair-field-1 load). Returns an i64.
mlir::Value MLIRGenImpl::emit_dst_len(mlir::Value thin_ptr, TypeRef dstref_t) {
    if (!thin_ptr || !dstref_t) return thin_ptr;
    // Concrete (type-arg-mangled) struct name so a GENERIC self-describing DST
    // resolves to its monomorphised `Foo$G1$i64__dst_len` instance, not the bare
    // `Foo__dst_len` (which only exists for non-generic structs).
    auto targs = TypeRef(dstref_t).type_args();
    std::vector<TypeRef> targ_vec(targs.begin(), targs.end());
    std::string dstref_pkg(TypeRef(dstref_t).pkg_name());
    std::string sname = concrete_struct_name_raw(
        std::string(TypeRef(dstref_t).struct_name()), targ_vec, dstref_pkg);
    auto sym = resolve_method_symbol(sname, "dst_len");
    auto parent_mod = builder_.getBlock()->getParent()
                          ->getParentOfType<mlir::ModuleOp>();
    mlir::Value len;
    if (parent_mod) {
        if (auto fn = parent_mod.lookupSymbol<mlir::func::FuncOp>(sym)) {
            auto call = builder_.create<mlir::func::CallOp>(
                loc_, fn, mlir::ValueRange{thin_ptr});
            if (call.getNumResults() == 1) len = call.getResult(0);
        }
    }
    // No SelfDescribing impl found (sema enforces one for #[self_describing]
    // structs, so this is a defensive 0-length fallback only).
    if (!len)
        len = builder_.create<mlir::arith::ConstantIntOp>(loc_, 0, 64);
    if (len.getType() != builder_.getI64Type())
        len = coerce_numeric(len, builder_.getI64Type());
    return len;
}

mlir::Value MLIRGenImpl::gen_expr_kind(lir_view::ESliceLitView v, TypeRef type) {
    if (!v.base() || !v.len()) return nullptr;
    auto base = gen_expr(v.base());
    auto meta = gen_expr(v.len());
    if (!base || !meta) return nullptr;
    // RefRepr (Phase 1): build the fat pair for the result reference's repr —
    // a `dyn`-tail projection (FatDyn) stores {data,vtable}, a slice/custom-DST
    // (FatSlice/FatCustomDst) stores {data,len}. repr_construct treats
    // FatCustomDst like FatSlice (len metadata). Default to FatSlice when the
    // type is absent/non-ref (slice_lit always builds the {data,len} shape).
    auto rk = ref_repr_of(type);
    if (rk == RefReprKind::NotARef || rk == RefReprKind::ThinPtr)
        rk = RefReprKind::FatSlice;
    return repr_construct(rk, base, meta);
}

mlir::Value MLIRGenImpl::gen_expr_kind(lir_view::ESliceIndexView v, TypeRef type) {
    if (!v.slice() || !v.index()) return nullptr;
    TypeRef index_ty = v.index().type(pool_impl());
    auto slice = gen_expr(v.slice());
    auto index = gen_expr(v.index());
    if (!slice || !index) return nullptr;
    auto stype = slice_llvm_type();
    // Load ptr from field 0
    llvm::SmallVector<mlir::LLVM::GEPArg> pi{int32_t(0), int32_t(0)};
    auto pp = builder_.create<mlir::LLVM::GEPOp>(loc_, ptr_type(), stype, slice, pi);
    auto data_ptr = builder_.create<mlir::LLVM::LoadOp>(loc_, ptr_type(), pp);
    // GEP into data array by index.
    bool idx_unsigned = index_ty &&
        LogosType::is_unsigned_repr_kind(TypeRef(index_ty).kind());
    mlir::Value gep_idx;
    if (idx_unsigned && index.getType() != builder_.getI64Type())
        gep_idx = builder_.create<mlir::arith::ExtUIOp>(loc_, builder_.getI64Type(), index);
    else
        gep_idx = index;
    // Element slot type + return convention derive from ONE foundation —
    // place_slot_type — exactly the type the lvalue path (`&s[i]` / `s[i]=v`,
    // gen_lvalue_addr::SliceIndex) strides by, so reads and writes address
    // the identical slot by construction. Aggregate slots (inline struct /
    // TUPLE / tagged-enum value-repr / fat {data,meta} pairs) are consumed
    // BY POINTER downstream — return the slot ADDRESS; scalar / thin-ptr
    // slots load the value. (P4-pm-15 hand-rolled only the Struct + fat-ref
    // cases here; logos_to_mlir collapses tuple/enum elements to `ptr` too,
    // so `let v: (i64, i64) = ps[k]` mis-strode by 8B and loaded value bits
    // as a pointer — SIGSEGV. place_slot_type is total over element shapes.)
    mlir::Type slot = place_slot_type(type);
    if (!slot) slot = builder_.getI32Type();
    llvm::SmallVector<mlir::LLVM::GEPArg> di{gep_idx};
    auto elem_ptr = builder_.create<mlir::LLVM::GEPOp>(
        loc_, ptr_type(), slot, data_ptr, di);
    if (mlir::isa<mlir::LLVM::LLVMStructType>(slot))
        return elem_ptr;
    return builder_.create<mlir::LLVM::LoadOp>(loc_, slot, elem_ptr);
}

// RefRepr op: extract the DATA half of a reference value. Thin → the value is
// the data pointer (identity). Fat → load field 0 of the {data,meta} pair the
// value points at (field 0 is a ptr at offset 0 for every fat repr).
mlir::Value MLIRGenImpl::repr_data(RefReprKind k, mlir::Value v) {
    if (k == RefReprKind::ThinPtr || k == RefReprKind::NotARef) return v;
    auto stype = slice_llvm_type();
    llvm::SmallVector<mlir::LLVM::GEPArg> pi{int32_t(0), int32_t(0)};
    auto pp = builder_.create<mlir::LLVM::GEPOp>(loc_, ptr_type(), stype, v, pi);
    return builder_.create<mlir::LLVM::LoadOp>(loc_, ptr_type(), pp);
}

// RefRepr op: extract the METADATA half (len / vtable). Thin → none (null).
// Fat → load field 1 of the {data,meta} pair (returned as i64, matching the
// slice-len extraction; a vtable consumer casts the ptr-sized value).
mlir::Value MLIRGenImpl::repr_meta(RefReprKind k, mlir::Value v) {
    if (k == RefReprKind::ThinPtr || k == RefReprKind::NotARef) return nullptr;
    auto stype = slice_llvm_type();
    llvm::SmallVector<mlir::LLVM::GEPArg> li{int32_t(0), int32_t(1)};
    auto lp = builder_.create<mlir::LLVM::GEPOp>(loc_, ptr_type(), stype, v, li);
    return builder_.create<mlir::LLVM::LoadOp>(loc_, builder_.getI64Type(), lp);
}

mlir::Value MLIRGenImpl::gen_expr_kind(lir_view::ESliceLenView v, TypeRef) {
    if (!v.slice()) return nullptr;
    TypeRef sl_ty = v.slice().type(pool_impl());
    auto slice = gen_expr(v.slice());
    if (!slice) return nullptr;
    // A thin #[self_describing] DstRef carries no out-of-band metadata — its tail
    // length is recovered IN-BAND via dst_len(header_ptr). (slice = the header
    // pointer itself, since repr_data(ThinPtr) is the value.)
    if (dstref_pointee_self_describing(sl_ty))
        return emit_dst_len(slice, sl_ty);
    // RefRepr (Phase 1): len = the metadata half of the fat receiver.
    return repr_meta(ref_repr_of(sl_ty), slice);
}

mlir::Value MLIRGenImpl::gen_expr_kind(lir_view::ESlicePtrView v, TypeRef) {
    if (!v.slice()) return nullptr;
    TypeRef sl_ty = v.slice().type(pool_impl());
    auto slice = gen_expr(v.slice());
    if (!slice) return nullptr;
    // RefRepr (Phase 1): data ptr = the data half of the fat receiver.
    return repr_data(ref_repr_of(sl_ty), slice);
}

// ---------------------------------------------------------------------------
// format() built-in
// ---------------------------------------------------------------------------

int MLIRGenImpl::format_type_tag(TypeRef t) noexcept {
    if (!t) return 0;
    switch (TypeRef(t).kind()) {
        case LogosType::Kind::I32:    return 0;
        case LogosType::Kind::I64:    return 1;
        case LogosType::Kind::Ptr:    return 2;
        case LogosType::Kind::Slice:  return 2;
        case LogosType::Kind::Bool:   return 3;
        case LogosType::Kind::U8:     return 4;
        case LogosType::Kind::U32:    return 5;
        case LogosType::Kind::U64:    return 6;
        case LogosType::Kind::I8:     return 7;
        case LogosType::Kind::I16:    return 0;  // dispatches as i32
        case LogosType::Kind::U16:    return 5;  // dispatches as u32
        case LogosType::Kind::I24:    return 1;  // dispatches as i64
        case LogosType::Kind::I56:    return 1;  // dispatches as i64
        case LogosType::Kind::U24:    return 6;  // dispatches as u64
        case LogosType::Kind::U56:    return 6;  // dispatches as u64
        case LogosType::Kind::I128:   return 1;  // dispatches as i64
        case LogosType::Kind::U128:   return 6;  // dispatches as u64
        case LogosType::Kind::Usize:  return 6;  // dispatches as u64
        case LogosType::Kind::Isize:  return 1;  // dispatches as i64
        case LogosType::Kind::IntLit: return 0;
        default:                      return 0;
    }
}

mlir::Value MLIRGenImpl::gen_expr_kind(lir_view::EFormatCallView v, TypeRef) {
    if (!v.fmt()) return nullptr;
    auto fmt_val = gen_expr(v.fmt());
    if (!fmt_val) return nullptr;

    auto arg_types = v.arg_types(pool_impl());
    std::vector<lir_view::ExprRef> arg_refs;
    v.each_arg([&](lir_view::ExprRef r) { arg_refs.push_back(r); });
    int n = (int)arg_refs.size();

    auto i32_type = builder_.getI32Type();
    auto i64_type = builder_.getI64Type();

    // Allocate [n x i32] tags and [n x i64] data arrays on stack.
    int64_t n_cnt = n > 0 ? n : 1;
    auto tags_alloca = create_entry_alloca(i32_type, n_cnt);
    auto data_alloca = create_entry_alloca(i64_type, n_cnt);

    for (int i = 0; i < n; ++i) {
        int tag = format_type_tag(arg_types[i]);

        // Store tag at tags[i]
        llvm::SmallVector<mlir::LLVM::GEPArg> ti{int32_t(i)};
        auto tgep = builder_.create<mlir::LLVM::GEPOp>(
            loc_, ptr_type(), i32_type, tags_alloca, ti);
        auto tag_val = builder_.create<mlir::arith::ConstantIntOp>(loc_, tag, 32);
        builder_.create<mlir::LLVM::StoreOp>(loc_, tag_val, tgep);

        // Evaluate arg and widen to i64.
        // Unsigned types narrower than 64 bits must be zero-extended, not sign-extended.
        if (!arg_refs[i]) return nullptr;
        auto arg_val = gen_expr(arg_refs[i]);
        if (!arg_val) return nullptr;
        mlir::Value as_i64;
        if (arg_val.getType() == ptr_type()) {
            as_i64 = builder_.create<mlir::LLVM::PtrToIntOp>(loc_, i64_type, arg_val);
        } else {
            TypeRef arg_lt = static_cast<size_t>(i) < arg_types.size() ? arg_types[i] : TypeRef{};
            bool arg_unsigned = arg_lt &&
                LogosType::is_unsigned_repr_kind(arg_lt.kind());
            auto ai = mlir::dyn_cast<mlir::IntegerType>(arg_val.getType());
            if (arg_unsigned && ai && ai.getWidth() < 64)
                as_i64 = builder_.create<mlir::arith::ExtUIOp>(loc_, i64_type, arg_val);
            else
                as_i64 = coerce_int(arg_val, i64_type);
        }

        // Store data at data[i]
        llvm::SmallVector<mlir::LLVM::GEPArg> di{int32_t(i)};
        auto dgep = builder_.create<mlir::LLVM::GEPOp>(
            loc_, ptr_type(), i64_type, data_alloca, di);
        builder_.create<mlir::LLVM::StoreOp>(loc_, as_i64, dgep);
    }

    // Call __format_impl(fmt, tags_ptr, data_ptr, nargs)
    auto mod = builder_.getBlock()->getParent()->getParentOfType<mlir::ModuleOp>();
    auto impl_fn = mod.lookupSymbol<mlir::func::FuncOp>("__format_impl");
    if (!impl_fn) {
        std::fprintf(stderr,
            "mlir_gen: format() requires 'use std.lang.text;' to be imported\n");
        return nullptr;
    }
    auto n_i32 = builder_.create<mlir::arith::ConstantIntOp>(loc_, n, 32);
    llvm::SmallVector<mlir::Value> call_args{fmt_val, tags_alloca, data_alloca, n_i32};
    auto call = builder_.create<mlir::func::CallOp>(loc_, impl_fn, call_args);
    return call.getNumResults() > 0 ? call.getResult(0) : nullptr;
}

// ---------------------------------------------------------------------------
// Misc expression kinds
// ---------------------------------------------------------------------------

mlir::Value MLIRGenImpl::gen_expr_kind(lir_view::EPackExpandView, TypeRef) {
    bug_raw("unexpected EPackExpand (should have been expanded by mono)");
    return nullptr;
}

mlir::Value MLIRGenImpl::gen_expr_kind(lir_view::ESizeOfView v, TypeRef) {
    // Compile-time constant from the unified layout — alignment-correct and
    // consistent with every other size query (enum payload bytes, Vec/HashMap
    // strides, alloc sizes). The former GEP-null trick folded through MLIR's
    // (unset) DataLayout and under-counted inter-field padding (e.g. {i32,i64}
    // → 12 instead of 16), so alloc(sizeof::<T>()) overflowed.
    uint64_t sz = layout_of(v.elem_type(pool_impl())).size;
    return builder_.create<mlir::arith::ConstantIntOp>(loc_, (int64_t)sz, 64);
}

mlir::Value MLIRGenImpl::gen_expr_kind(lir_view::EAlignOfView v, TypeRef) {
    // Compile-time constant from the same unified layout that drives sizeof.
    uint64_t a = layout_of(v.elem_type(pool_impl())).align;
    return builder_.create<mlir::arith::ConstantIntOp>(loc_, (int64_t)(a ? a : 1), 64);
}

mlir::Value MLIRGenImpl::gen_expr_kind(lir_view::EPtrArithView v, TypeRef) {
    auto ptr_ref    = v.ptr();
    auto offset_ref = v.offset();
    auto op         = EPtrArith::Op(v.op_code());
    if (!ptr_ref || !offset_ref) return nullptr;
    TypeRef ptr_ty = ptr_ref.type(pool_impl());
    TypeRef off_ty = offset_ref.type(pool_impl());
    auto p = gen_expr(ptr_ref);
    auto n = gen_expr(offset_ref);
    if (!p || !n) return nullptr;
    // Widen/narrow offset to i64 just in case.
    if (auto it = mlir::dyn_cast<mlir::IntegerType>(n.getType()))
        if (it.getWidth() != 64)
            n = coerce_int(n, builder_.getI64Type(), off_ty);
    // Negate for Sub variants.
    if (op == EPtrArith::ByteSub || op == EPtrArith::Sub) {
        auto zero = builder_.create<mlir::arith::ConstantIntOp>(loc_, 0, 64);
        n = builder_.create<mlir::arith::SubIOp>(loc_, zero, n);
    }
    mlir::Type elem_ty = builder_.getI8Type();  // default: byte indexing
    if (op == EPtrArith::Add || op == EPtrArith::Sub) {
        // Element indexing uses the pointee type from the receiver.
        TypeRef pt = ptr_ty;
        if (pt && pt.pointee()) {
            // Struct/Datatype want their aggregate LLVM type, not ptr.
            if (pt.pointee().kind() == LogosType::Kind::Struct ||
                pt.pointee().kind() == LogosType::Kind::ZonedStruct) {
                auto cname = concrete_struct_name(pt.pointee());
                auto sit = struct_types_.find(cname);
                if (sit != struct_types_.end())
                    elem_ty = sit->second.llvm_type;
                else
                    elem_ty = logos_to_mlir(pt.pointee());
            } else {
                elem_ty = logos_to_mlir(pt.pointee());
            }
        }
    }
    llvm::SmallVector<mlir::LLVM::GEPArg> idx{n};
    return builder_.create<mlir::LLVM::GEPOp>(loc_, ptr_type(), elem_ty, p, idx);
}

mlir::Value MLIRGenImpl::gen_expr_kind(lir_view::EPtrDiffView v, TypeRef) {
    if (!v.lhs() || !v.rhs()) return nullptr;
    TypeRef lhs_ty = v.lhs().type(pool_impl());
    auto a = gen_expr(v.lhs());
    auto b = gen_expr(v.rhs());
    if (!a || !b) return nullptr;
    auto i64ty = builder_.getI64Type();
    auto ai = builder_.create<mlir::LLVM::PtrToIntOp>(loc_, i64ty, a);
    auto bi = builder_.create<mlir::LLVM::PtrToIntOp>(loc_, i64ty, b);
    mlir::Value diff = builder_.create<mlir::arith::SubIOp>(loc_, ai, bi);
    if (v.by_byte()) return diff;
    // Element distance: diff / sizeof(pointee).
    TypeRef pt = lhs_ty;
    if (!pt || !pt.pointee()) return diff;
    mlir::Type elem_mlir = nullptr;
    if (pt.pointee().kind() == LogosType::Kind::Struct ||
        pt.pointee().kind() == LogosType::Kind::ZonedStruct) {
        auto cname = concrete_struct_name(pt.pointee());
        auto sit = struct_types_.find(cname);
        if (sit != struct_types_.end()) elem_mlir = sit->second.llvm_type;
    }
    if (!elem_mlir) elem_mlir = logos_to_mlir(pt.pointee());
    if (!elem_mlir) return diff;
    // sizeof trick.
    mlir::Value zero = builder_.create<mlir::arith::ConstantIntOp>(loc_, 0, 64);
    mlir::Value null_ptr = builder_.create<mlir::LLVM::IntToPtrOp>(loc_, ptr_type(), zero);
    llvm::SmallVector<mlir::LLVM::GEPArg> one{int32_t(1)};
    auto size_ptr = builder_.create<mlir::LLVM::GEPOp>(loc_, ptr_type(), elem_mlir, null_ptr, one);
    auto sz = builder_.create<mlir::LLVM::PtrToIntOp>(loc_, i64ty, size_ptr);
    return builder_.create<mlir::arith::DivSIOp>(loc_, diff, sz);
}

mlir::Value MLIRGenImpl::gen_expr_kind(lir_view::ETypeCodeOfView, TypeRef) {
    // Should have been folded to ELitInt by mono.  Emit 0 as a defensive
    // fallback (not expected to be reached for well-formed programs).
    return builder_.create<mlir::arith::ConstantIntOp>(loc_, 0, 64);
}

mlir::Value MLIRGenImpl::gen_expr_kind(lir_view::EBlockExprView v, TypeRef) {
    // N3: a value-producing block expression scopes its own `let` bindings —
    // an inner `let x` must NOT leak out and clobber an outer `x`'s slot
    // (`let y = { let x = 100; x + 1 }` was silently overwriting the outer x).
    //
    // The restore is SURGICAL: sema reuses EBlockExpr for many *transparent*
    // synthetic constructs (desugar temporaries, match-arm/if-branch bodies)
    // whose bindings must LEAK to the enclosing scope, so a blanket
    // snapshot/restore (as SBlockView does for real `{}` statement blocks)
    // breaks them. We therefore revert ONLY the names that are re-bound by a
    // direct `let` at this block's top level AND already existed in the outer
    // scope — i.e. genuine user shadowing. Synthetic blocks bind fresh
    // `__`-prefixed names (not outer shadows), so they are untouched.
    std::vector<std::string> shadowed;
    if (auto br = v.block(); br) {
        br.each_stmt([&](lir_view::StmtRef s) {
            if (s.kind() != lir_schema::stmt::Code::Let) return;
            std::string n(lir_view::SLetView{s}.name());
            if (scope_.count(n)) shadowed.push_back(n);
        });
    }
    // Snapshot prior state for just the shadowed names.
    struct Saved {
        bool in_scope = false;            mlir::Value scope_val;
        bool in_local_ptr = false;        mlir::Type  local_ptr_val;
        bool in_struct = false;           std::string struct_val;
        bool in_elem = false;             mlir::Type  elem_val;
        bool in_dyn = false;              std::string dyn_val;
        bool in_subscript = false;        mlir::Type  subscript_val;
        bool in_slice = false;            mlir::Type  slice_val;
        bool in_tuple = false, in_tenum = false, in_tenum_ptr = false;
        bool in_raw_dyn = false, in_dyn_ptr_handle = false;
        bool in_ref_param = false, in_ptr_family = false;
    };
    std::unordered_map<std::string, Saved> saved;
    for (auto& n : shadowed) {
        Saved sv;
        if (auto it = scope_.find(n); it != scope_.end())            { sv.in_scope=true; sv.scope_val=it->second; }
        if (auto it = var_local_ptrs_.find(n); it != var_local_ptrs_.end()) { sv.in_local_ptr=true; sv.local_ptr_val=it->second; }
        if (auto it = var_struct_.find(n); it != var_struct_.end())  { sv.in_struct=true; sv.struct_val=it->second; }
        if (auto it = var_elem_types_.find(n); it != var_elem_types_.end()) { sv.in_elem=true; sv.elem_val=it->second; }
        if (auto it = var_dyn_trait_.find(n); it != var_dyn_trait_.end()) { sv.in_dyn=true; sv.dyn_val=it->second; }
        if (auto it = var_subscript_.find(n); it != var_subscript_.end()) { sv.in_subscript=true; sv.subscript_val=it->second; }
        if (auto it = var_slice_.find(n); it != var_slice_.end())    { sv.in_slice=true; sv.slice_val=it->second; }
        sv.in_tuple          = var_tuple_.count(n) > 0;
        sv.in_tenum          = var_tagged_enum_.count(n) > 0;
        sv.in_tenum_ptr      = var_tagged_enum_ptr_.count(n) > 0;
        sv.in_raw_dyn        = var_raw_dyn_.count(n) > 0;
        sv.in_dyn_ptr_handle = dyn_ptr_to_handle_vars_.count(n) > 0;
        sv.in_ref_param      = ref_param_names_.count(n) > 0;
        sv.in_ptr_family     = ptr_family_param_.count(n) > 0;
        saved[n] = sv;
    }
    auto restore = [&]() {
        for (auto& [n, sv] : saved) {
            if (sv.in_scope)      scope_[n] = sv.scope_val;                 else scope_.erase(n);
            if (sv.in_local_ptr)  var_local_ptrs_[n] = sv.local_ptr_val;    else var_local_ptrs_.erase(n);
            if (sv.in_struct)     var_struct_[n] = sv.struct_val;           else var_struct_.erase(n);
            if (sv.in_elem)       var_elem_types_[n] = sv.elem_val;         else var_elem_types_.erase(n);
            if (sv.in_dyn)        var_dyn_trait_[n] = sv.dyn_val;           else var_dyn_trait_.erase(n);
            if (sv.in_subscript)  var_subscript_[n] = sv.subscript_val;     else var_subscript_.erase(n);
            if (sv.in_slice)      var_slice_[n] = sv.slice_val;             else var_slice_.erase(n);
            if (sv.in_tuple)      var_tuple_.insert(n);                     else var_tuple_.erase(n);
            if (sv.in_tenum)      var_tagged_enum_.insert(n);               else var_tagged_enum_.erase(n);
            if (sv.in_tenum_ptr)  var_tagged_enum_ptr_.insert(n);           else var_tagged_enum_ptr_.erase(n);
            if (sv.in_raw_dyn)    var_raw_dyn_.insert(n);                   else var_raw_dyn_.erase(n);
            if (sv.in_dyn_ptr_handle) dyn_ptr_to_handle_vars_.insert(n);    else dyn_ptr_to_handle_vars_.erase(n);
            if (sv.in_ref_param)  ref_param_names_.insert(n);               else ref_param_names_.erase(n);
            if (sv.in_ptr_family) ptr_family_param_.insert(n);              else ptr_family_param_.erase(n);
        }
    };
    if (auto br = v.block(); br) gen_block(br);
    if (is_terminated(builder_.getBlock())) { restore(); return nullptr; }
    mlir::Value result = nullptr;
    if (auto rr = v.result(); rr) {
        result = gen_expr(rr);
    }
    restore();
    return result;
}

// ---------------------------------------------------------------------------
// Try expression: expr?
// ---------------------------------------------------------------------------

mlir::Value MLIRGenImpl::gen_expr_kind(lir_view::ETryView v, TypeRef type) {
    if (!v.inner()) return nullptr;
    TypeRef inner_ty = v.inner().type(pool_impl());
    auto inner_ptr = gen_expr(v.inner());
    if (!inner_ptr) return nullptr;
    // Aggregate returned by value — spill to alloca so GEP works below.
    inner_ptr = spill_to_alloca(inner_ptr);

    auto* te = resolve_tagged_enum(std::string(TypeRef(inner_ty).enum_name()), inner_ty);
    if (!te) {
        std::fprintf(stderr, "mlir_gen: ETry: cannot resolve Result enum\n");
        return nullptr;
    }

    // Load discriminant (Phase 3.5 chokepoint).
    auto disc     = enum_load_disc(inner_ptr, *te);
    auto ok_cst   = builder_.create<mlir::arith::ConstantIntOp>(loc_, v.ok_disc(), 32);
    auto is_ok    = builder_.create<mlir::arith::CmpIOp>(
                        loc_, mlir::arith::CmpIPredicate::eq, disc, ok_cst);

    // A unit Ok type (`Result<(), E>` — the `do_it()?;` statement form)
    // yields no value: logos_to_mlir(void) is null BY CONVENTION, not a
    // failure. The control flow (err early-return!) is still mandatory —
    // treating null as failure here silently DROPPED the whole branch and
    // `?` swallowed the Err (first hit: PkdArray::insert's resize_self
    // chain overran its block instead of propagating OutOfMem).
    auto ok_mlir = logos_to_mlir(type);
    // Only a GENUINE Void ok-type may proceed without a result slot; a null/
    // invalid `type` stays a hard failure (the pre-fix contract) — it must
    // not ride the unit-Ok path.
    if (!ok_mlir && (!TypeRef(type) || TypeRef(type).kind() != LogosType::Kind::Void))
        return nullptr;
    mlir::Value result_alloca;
    if (ok_mlir) result_alloca = create_entry_alloca(ok_mlir);

    auto* region      = builder_.getBlock()->getParent();
    auto* ok_block    = new mlir::Block();
    auto* err_block   = new mlir::Block();
    auto* merge_block = new mlir::Block();
    region->push_back(ok_block);
    region->push_back(err_block);
    region->push_back(merge_block);

    builder_.create<mlir::cf::CondBranchOp>(loc_, is_ok, ok_block, err_block);

    // ── ok_block: extract T payload → store to result_alloca ──────────
    builder_.setInsertionPointToStart(ok_block);
    {
        const TaggedEnumInfo::VariantPayload* ok_vp = nullptr;
        int32_t ok_d = v.ok_disc();
        for (auto& vp : te->variants) if (vp.disc == ok_d) { ok_vp = &vp; break; }

        auto pay_ptr = enum_payload_ptr(inner_ptr, *te);  // Phase 3.5 (LowBit decode)
        if (ok_mlir && ok_vp && !ok_vp->field_types.empty()) {
            auto ps  = mlir::LLVM::LLVMStructType::getLiteral(builder_.getContext(), ok_vp->field_types);
            llvm::SmallVector<mlir::LLVM::GEPArg> fi{int32_t(0), int32_t(0)};
            auto fp  = builder_.create<mlir::LLVM::GEPOp>(loc_, ptr_type(), ps, pay_ptr, fi);
            TypeRef lt = ok_vp->logos_types.empty() ? nullptr : ok_vp->logos_types[0];
            bool is_inline = lt && (TypeRef(lt).kind() == LogosType::Kind::Struct ||
                                    TypeRef(lt).kind() == LogosType::Kind::ZonedStruct ||
                                    TypeRef(lt).kind() == LogosType::Kind::Tuple ||
                                    TypeRef(lt).kind() == LogosType::Kind::Slice ||
                                    TypeRef(lt).kind() == LogosType::Kind::Closure);
            // Enum value-repr: a nested TAGGED enum Ok payload (`Result<Option<T>,E>`)
            // is INLINE — `fp` is its storage address (one level, the value-repr
            // of the Option<T> result). Bind the address (no load).
            bool ok_is_enum = lt && TypeRef(lt).kind() == LogosType::Kind::Enum &&
                resolve_tagged_enum(std::string(TypeRef(lt).enum_name()), lt);
            mlir::Value val;
            if (is_inline || ok_is_enum)
                val = fp;
            else
                val = builder_.create<mlir::LLVM::LoadOp>(loc_, ok_vp->field_types[0], fp);
            builder_.create<mlir::LLVM::StoreOp>(loc_, coerce_int(val, ok_mlir), result_alloca);
        }
        builder_.create<mlir::cf::BranchOp>(loc_, merge_block);
    }

    // ── err_block: extract E payload, build Err return, early func.return ──
    builder_.setInsertionPointToStart(err_block);
    {
        const TaggedEnumInfo::VariantPayload* err_vp = nullptr;
        int32_t err_d = v.err_disc();
        for (auto& vp : te->variants) if (vp.disc == err_d) { err_vp = &vp; break; }

        auto pay_ptr = enum_payload_ptr(inner_ptr, *te);  // Phase 3.5 (LowBit decode)

        auto ret_alloca = create_entry_alloca(te->llvm_type);
        enum_store_disc(ret_alloca, *te, v.err_disc());
        // Copy E payload if it exists
        if (err_vp && !err_vp->field_types.empty()) {
            auto src_ps = mlir::LLVM::LLVMStructType::getLiteral(
                builder_.getContext(), err_vp->field_types);
            llvm::SmallVector<mlir::LLVM::GEPArg> fi{int32_t(0), int32_t(0)};
            auto src_fp  = builder_.create<mlir::LLVM::GEPOp>(loc_, ptr_type(), src_ps, pay_ptr, fi);
            auto err_val = builder_.create<mlir::LLVM::LoadOp>(
                loc_, err_vp->field_types[0], src_fp);
            auto rpp = enum_payload_ptr(ret_alloca, *te);  // Phase 3.5
            auto dst_ps = mlir::LLVM::LLVMStructType::getLiteral(
                builder_.getContext(), err_vp->field_types);
            auto dst_fp = builder_.create<mlir::LLVM::GEPOp>(loc_, ptr_type(), dst_ps, rpp, fi);
            builder_.create<mlir::LLVM::StoreOp>(loc_, err_val, dst_fp);
        }
        // Return: enums are returned as *ptr; struct-return is also handled.
        // N5: when `?` appears inside a CLOSURE, the body is lowered into an
        // `llvm.func` (in_llvm_func_), so the early-return must be an
        // llvm.return, not a func.return (which only parents under func.func).
        mlir::Value ret_operand;
        if (cur_ret_type_ == ptr_type()) {
            ret_operand = ret_alloca;
        } else if (cur_ret_type_ && mlir::isa<mlir::LLVM::LLVMStructType>(cur_ret_type_)) {
            ret_operand = builder_.create<mlir::LLVM::LoadOp>(loc_, cur_ret_type_, ret_alloca);
        }
        if (in_llvm_func_) {
            if (ret_operand)
                builder_.create<mlir::LLVM::ReturnOp>(loc_, mlir::ValueRange{ret_operand});
            else
                builder_.create<mlir::LLVM::ReturnOp>(loc_, mlir::ValueRange{});
        } else {
            if (ret_operand)
                builder_.create<mlir::func::ReturnOp>(loc_, mlir::ValueRange{ret_operand});
            else
                builder_.create<mlir::func::ReturnOp>(loc_, mlir::ValueRange{});
        }
    }

    // ── merge_block: yield Ok value (none for a unit Ok — the established
    // void-expression convention) ──────────────────────────────────────
    builder_.setInsertionPointToStart(merge_block);
    if (!ok_mlir) return nullptr;
    return builder_.create<mlir::LLVM::LoadOp>(loc_, ok_mlir, result_alloca);
}

// ---------------------------------------------------------------------------
// Writ SDN literal — zone blob builder (C++ Writ API + clone())
// ---------------------------------------------------------------------------
//
// Strategy: construct the literal's WritVal tree into a live mutable
// Writ document via the public C++ Writ API (ObjectArray / ObjectMap /
// TypedArray<T> / TypedMap<K,V> / ArenaString / anyval_put), then clone()
// it into a packed arena. Extract the packed bytes as the emit blob.
// PARAM slot offsets come from clone()'s out_params — single source of
// truth for both wire format and PARAM bookkeeping.

namespace {

using logos::writ::AnyVal;
using logos::writ::Arena;
using logos::writ::ArenaMode;
using logos::writ::ArenaString;
using logos::writ::WritAccess;
using logos::writ::ObjectArray;
using logos::writ::ObjectMap;
using logos::writ::TypedArray;
using logos::writ::arena_offset_t;
using logos::writ::anyval_put;
using logos::writ::make_doc;

struct WritZoneBuild {
    std::vector<uint8_t>                        blob;
    std::vector<std::pair<uint32_t, uint32_t>>  param_slots;  // (blob_off, value_idx)
};

// Build a WritVal into the live `doc`, returning the raw AnyVal u32.
// For PARAM (WVCapture), returns the inline PARAM raw; the caller writes it
// into the slot, and clone() will pick it up via its out_params bookkeeping.
// Returns a proper Writ SELF-relative AnyVal (Pod for scalars/captures, Ref for
// strings/arrays/maps/types). NOT the legacy u32 base-relative "raw" — that made
// from_raw(off) resolve to &slot+off = garbage.
static AnyVal build_writ_val(lir_view::WritValRef v,
                               logos::writ::Writ& doc);

// Self-relative Ref to an in-arena object (single-segment doc → base+off is absolute).
static AnyVal ptr_anyval(const void* obj) {
    AnyVal r; r.set_ref(obj); return r;
}

static uint32_t build_object_array(lir_view::WVArrayView arr,
                                   logos::writ::Writ& doc) {
    uint64_t n = arr.size();
    uint32_t a_off = doc.make_array(n ? n : uint64_t{4}).get().offset().value();
    for (uint64_t i = 0; i < n; ++i) {
        AnyVal elem_av = build_writ_val(arr.elem(i), doc);   // may alloc → re-fetch view
        logos::writ::ArrayView(arena_offset_t(a_off), doc.holder())
            .push_back(elem_av).get();
    }
    return a_off;
}

template <typename T>
static uint32_t build_typed_array_scalar(lir_view::WVArrayView arr,
                                         logos::writ::Writ& doc) {
    uint64_t n = arr.size();
    auto* a = doc.make_typed<TypedArray<T>>(n ? n : uint64_t{4}).get();
    uint32_t a_off = static_cast<uint32_t>(
        reinterpret_cast<uint8_t*>(a) - WritAccess::base(doc));
    for (uint64_t i = 0; i < n; ++i) {
        T val = 0;
        auto er = arr.elem(i);
        if (er && er.kind() == lir_schema::writ_val::Code::Int) {
            val = static_cast<T>(lir_view::WVIntView{er}.value());
        }
        auto* cur = reinterpret_cast<TypedArray<T>*>(
            WritAccess::base(doc) + a_off);
        cur->push_back(val, WritAccess::arena(doc)).get();
    }
    return a_off;
}

static uint32_t build_array(lir_view::WVArrayView arr,
                            logos::writ::Writ& doc) {
    auto et = arr.elem_type();
    if (et == "I8")  return build_typed_array_scalar<int8_t>(arr, doc);
    if (et == "U8")  return build_typed_array_scalar<uint8_t>(arr, doc);
    if (et == "I16") return build_typed_array_scalar<int16_t>(arr, doc);
    if (et == "U16") return build_typed_array_scalar<uint16_t>(arr, doc);
    if (et == "I32") return build_typed_array_scalar<int32_t>(arr, doc);
    if (et == "U32") return build_typed_array_scalar<uint32_t>(arr, doc);
    if (et == "I64") return build_typed_array_scalar<int64_t>(arr, doc);
    if (et == "U64") return build_typed_array_scalar<uint64_t>(arr, doc);
    if (et == "F32") return build_typed_array_scalar<float>(arr, doc);
    if (et == "F64") return build_typed_array_scalar<double>(arr, doc);
    return build_object_array(arr, doc);
}

static uint32_t build_object_map(lir_view::WVMapView map,
                                 logos::writ::Writ& doc) {
    uint64_t n = map.size();
    uint32_t cap = 8;
    while (cap < n * 2 || cap < 8) cap <<= 1;
    uint32_t m_off = doc.make_object_map(cap).get().offset().value();
    for (uint64_t i = 0; i < n; ++i) {
        std::string key_str = map.int_keyed()
            ? std::to_string(map.int_key(i))
            : std::string(map.str_key(i));
        AnyVal val_av = build_writ_val(map.value(i), doc);
        logos::writ::MapView(arena_offset_t(m_off), doc.holder())
            .put(key_str, val_av).get();
    }
    return m_off;
}

template <typename Map, typename K>
static uint32_t build_typed_map_anyval(lir_view::WVMapView map,
                                       logos::writ::Writ& doc) {
    uint64_t n = map.size();
    uint32_t cap = n == 0 ? 1 : static_cast<uint32_t>(n);
    auto* m = doc.make_typed<Map>(cap).get();
    uint32_t m_off = static_cast<uint32_t>(
        reinterpret_cast<uint8_t*>(m) - WritAccess::base(doc));
    for (uint64_t i = 0; i < n; ++i) {
        K key = static_cast<K>(map.int_key(i));
        AnyVal val_av = build_writ_val(map.value(i), doc);
        auto* cur = reinterpret_cast<Map*>(
            WritAccess::base(doc) + m_off);
        cur->put(key, val_av);
    }
    return m_off;
}

static uint32_t build_map(lir_view::WVMapView map,
                          logos::writ::Writ& doc) {
    auto kt = map.key_type();
    if (kt == "I32") return build_typed_map_anyval<logos::writ::TypedMap<int32_t>, int32_t>(map, doc);
    if (kt == "U32") return build_typed_map_anyval<logos::writ::TypedMap<uint32_t>, uint32_t>(map, doc);
    if (kt == "I64") return build_typed_map_anyval<logos::writ::TypedMap<int64_t>, int64_t>(map, doc);
    if (kt == "U64") return build_typed_map_anyval<logos::writ::TypedMap<uint64_t>, uint64_t>(map, doc);
    return build_object_map(map, doc);
}

static AnyVal build_writ_val(lir_view::WritValRef v,
                               logos::writ::Writ& doc) {
    if (!v) return AnyVal::null();
    using HC = lir_schema::writ_val::Code;
    switch (v.kind()) {
    case HC::Null:
        return AnyVal::null();
    case HC::Bool:
        // Boolean: writ WA_BOOL = 2 (was legacy type_hash 37).
        return AnyVal::from_value<uint8_t>(
            lir_view::WVBoolView{v}.value() ? 1 : 0, 2);
    case HC::Int: {
        int64_t iv = lir_view::WVIntView{v}.value();
        if (iv >= -8388608LL && iv <= 8388607LL)
            return AnyVal::from_value<int32_t>(static_cast<int32_t>(iv));
        return doc.box<int64_t>(iv).get();
    }
    case HC::Float:
        return doc.box<double>(lir_view::WVFloatView{v}.value()).get();
    case HC::Str: {
        auto sv = lir_view::WVStrView{v}.value();
        return doc.make_string(std::string(sv)).get().to_anyval();
    }
    case HC::Array: {
        uint32_t off = build_array(lir_view::WVArrayView{v}, doc);   // build FIRST (may realloc)
        return ptr_anyval(WritAccess::base(doc) + off);            // then re-fetch base
    }
    case HC::Map: {
        uint32_t off = build_map(lir_view::WVMapView{v}, doc);
        return ptr_anyval(WritAccess::base(doc) + off);
    }
    case HC::Capture:
        // Inline PARAM (tc=127): word = (value_index << 8) | ((127&0x7F)<<1) | 1.
        return AnyVal::pod(lir_view::WVCaptureView{v}.value_index(), 127);
    case HC::Type: {
        // Component-metaprog slice 1C: emit a TinyObjectMap whose
        // schema_type_code = type_hash::Type=107 carrying:
        //   key 0: kind (u32, inline AnyVal)
        //   key 1: uid  (u64, ptr-mode AnyVal)
        //   key 2: name (ArenaString ptr-mode AnyVal)
        lir_view::WVTypeView tv{v};
        auto mv = doc.make_tiny_map_view(/*cap=*/4).get();
        mv.set_schema_type_code(logos::writ::type_hash::Type);
        uint32_t m_off = mv.offset().value();

        AnyVal kind_av = AnyVal::from_value<uint32_t>(tv.kind());
        AnyVal uid_av  = doc.box<uint64_t>(tv.uid()).get();
        AnyVal name_av = doc.make_string(std::string(tv.name())).get().to_anyval();

        // Re-fetch the map by offset each put (a build_*/box alloc above may have
        // relocated under GrowableSingleChunk); the TinyMapView wraps the resolve.
        auto map_at = [&] {
            return logos::writ::TinyMapView(arena_offset_t(m_off), doc.holder());
        };
        map_at().put(0, kind_av).get();
        map_at().put(1, uid_av).get();
        map_at().put(2, name_av).get();

        return map_at().to_anyval();
    }
    }
    return AnyVal::null();
}

// Build the full zone blob for an EWritLit node.
// Steps:
//   1. Make a fresh doc (DocumentHeader at offset 0).
//   2. Build the root value tree.
//   3. Write root AnyVal.raw into DocumentHeader (works for inline + ptr
//      alike — AnyVal bit0 disambiguates on read; see Task 1 in clone.cpp).
//   4. clone() → packed arena + PARAM slot list.
//   5. Extract bytes from packed head() chunk.
// Collect PARAM slots (inline Pod code 127: word = (value_idx<<8)|0xFF) by
// walking every at-rest AnyVal slot reachable from the root of a COMPACTIFIED
// single-segment writ blob. writ's compactify (unlike the legacy clone)
// has no out_params channel, so the capture-patch slot list is rebuilt here.
static void collect_param_slots(
        const uint8_t* base, size_t used, uint64_t slot_off,
        std::vector<std::pair<uint32_t, uint32_t>>& out,
        std::unordered_set<uint64_t>& visited) {
    if (slot_off + 8 > used) return;
    int64_t w = *reinterpret_cast<const int64_t*>(base + slot_off);
    if (w == 0) return;
    if (w & 1) {  // Pod
        uint64_t uw = static_cast<uint64_t>(w);
        if (((uw >> 1) & 0x7F) == 127)
            out.emplace_back(static_cast<uint32_t>(slot_off),
                             static_cast<uint32_t>(uw >> 8));
        return;
    }
    // Ref: object at slot + w; type tag immediately before it (datatag varint).
    uint64_t obj = slot_off + static_cast<uint64_t>(w);
    if (obj == 0 || obj + 8 > used) return;
    // Shared (DAG) and cyclic objects: descend each object ONCE — quote/AST blobs
    // share subtrees heavily, and an unbounded re-walk overflows the stack.
    if (!visited.insert(obj).second) return;
    uint8_t hb = base[obj - 1];
    uint64_t tc = 0;
    if (hb >= 1 && hb <= 222) {
        tc = hb;
    } else if (hb >= 223 && hb <= 230) {
        size_t n = static_cast<size_t>(hb) - 223 + 1;
        for (size_t i = 0; i < n && obj >= 2 + i; ++i)
            tc |= static_cast<uint64_t>(base[obj - 2 - i]) << (i * 8);
    } else {
        return;
    }
    auto walk_elems = [&](uint64_t relptr_field_off, uint64_t n, uint64_t stride,
                          uint64_t first_elem_delta) {
        if (relptr_field_off + 8 > used) return;
        int64_t rel = *reinterpret_cast<const int64_t*>(base + relptr_field_off);
        if (rel == 0) return;
        uint64_t elems = relptr_field_off + static_cast<uint64_t>(rel);
        for (uint64_t i = 0; i < n; ++i)
            collect_param_slots(base, used,
                                elems + i * stride + first_elem_delta, out, visited);
    };
    using namespace logos::writ;
    switch (tc) {
    case tc::ARRAY: {  // ObjectArray {size,cap,data→AnyVal[]}
        uint64_t n = *reinterpret_cast<const uint64_t*>(base + obj);
        walk_elems(obj + 16, n, 8, 0);
        break;
    }
    case tc::MAP: {    // ObjectMap {count,cap,data→MapEntry{key,val}[]} — walk vals of ALL cap slots
        uint64_t cap = *reinterpret_cast<const uint64_t*>(base + obj + 8);
        walk_elems(obj + 16, cap, 16, 8);
        break;
    }
    case tc::TINYMAP: {  // TinyObjectMap {header,schema,data→AnyVal[]}; size = header bits[58:63]
        uint64_t hdr = *reinterpret_cast<const uint64_t*>(base + obj);
        walk_elems(obj + 16, hdr >> 58, 8, 0);
        break;
    }
    case tc::TYPEDVALUE:  // 3 at-rest words {type_name,params,init}
        collect_param_slots(base, used, obj, out, visited);
        collect_param_slots(base, used, obj + 8, out, visited);
        collect_param_slots(base, used, obj + 16, out, visited);
        break;
    case tc::PARAMETER:   // Parameter object: {name,value}
        collect_param_slots(base, used, obj, out, visited);
        collect_param_slots(base, used, obj + 8, out, visited);
        break;
    case tc::MAP_I32: case tc::MAP_U32:
    case tc::MAP_I64: case tc::MAP_U64: {  // TypedMap {size,cap,keys,vals→AnyVal[]}
        uint64_t n = *reinterpret_cast<const uint64_t*>(base + obj);
        walk_elems(obj + 24, n, 8, 0);
        break;
    }
    default:  // strings, boxed scalars, typed arrays, Decimal — leaves
        break;
    }
}

static WritZoneBuild build_writ_zone(lir_view::EWritLitView e) {
    auto doc = logos::writ::make_doc_single_chunk().get();
    AnyVal root_av = build_writ_val(e.root(), doc);
    WritAccess::set_root_offset(doc, root_av);   // AnyVal overload (no offset)

    auto packed = logos::writ::compactify(doc).get();

    auto& packed_arena = WritAccess::arena(packed);
    const uint8_t* data = packed_arena.head().data();
    size_t used = packed_arena.total_used();

    WritZoneBuild out;
    out.blob.assign(data, data + used);
    // The document root word lives at blob offset 0 — walk from it.
    std::unordered_set<uint64_t> visited;
    collect_param_slots(out.blob.data(), used, 0, out.param_slots, visited);
    return out;
}
}  // namespace (zone builder helpers)

// Coerce a Logos runtime value to AnyVal.raw (u32) for writ capture substitution.
// Handles scalars that fit in 24 bits (embed_i24/embed_bool/etc.) and AnyVal passthrough.
// String/large-integer coercion is implemented in C5.
mlir::Value MLIRGenImpl::coerce_to_anyval_raw(mlir::Value v, TypeRef t) {
    if (!v || !t) return builder_.create<mlir::arith::ConstantIntOp>(loc_, 0, 32);
    auto i32_mlir = builder_.getIntegerType(32);
    using K = LogosType::Kind;
    switch (TypeRef(t).kind()) {
        case K::Bool: {
            // writ Pod bool: raw = (bool_val << 8) | (WA_BOOL<<1) | 1 = (b<<8) | 5
            // (WA_BOOL = 2). Was the legacy 0x4B (type_hash 37); build_writ_val matches.
            mlir::Value b = coerce_numeric(v, i32_mlir);
            mlir::Value shifted = builder_.create<mlir::arith::ShLIOp>(loc_, b,
                builder_.create<mlir::arith::ConstantIntOp>(loc_, 8, 32));
            return builder_.create<mlir::arith::OrIOp>(loc_, shifted,
                builder_.create<mlir::arith::ConstantIntOp>(loc_, 5, 32));
        }
        case K::I8:  case K::I16: case K::I32:
        case K::U8:  case K::U16: case K::U32:
        case K::I24: case K::U24: {
            // AnyVal::embed_i24: raw = ((v & 0xFFFFFF) << 8) | 0x2F (type_hash=23=0x17)
            mlir::Value iv = coerce_numeric(v, i32_mlir);
            mlir::Value masked = builder_.create<mlir::arith::AndIOp>(loc_, iv,
                builder_.create<mlir::arith::ConstantIntOp>(loc_, 0xFFFFFF, 32));
            mlir::Value shifted = builder_.create<mlir::arith::ShLIOp>(loc_, masked,
                builder_.create<mlir::arith::ConstantIntOp>(loc_, 8, 32));
            return builder_.create<mlir::arith::OrIOp>(loc_, shifted,
                builder_.create<mlir::arith::ConstantIntOp>(loc_, 0x2F, 32));
        }
        case K::I64: case K::U64: {
            // Truncate to low 24 bits and embed as i24. Values outside ±8M need C5.
            mlir::Value iv = coerce_numeric(v, i32_mlir);
            mlir::Value masked = builder_.create<mlir::arith::AndIOp>(loc_, iv,
                builder_.create<mlir::arith::ConstantIntOp>(loc_, 0xFFFFFF, 32));
            mlir::Value shifted = builder_.create<mlir::arith::ShLIOp>(loc_, masked,
                builder_.create<mlir::arith::ConstantIntOp>(loc_, 8, 32));
            return builder_.create<mlir::arith::OrIOp>(loc_, shifted,
                builder_.create<mlir::arith::ConstantIntOp>(loc_, 0x2F, 32));
        }
        case K::F32: case K::F64:
            // C4 bug fix: F32/F64 need zone-alloc RelPtr encoding (C5).
            // is_capturable no longer allows these; return null AnyVal as fallback.
            return builder_.create<mlir::arith::ConstantIntOp>(loc_, 0, 32);
        case K::Ptr: case K::Ref: case K::MutRef:
            // C4 bug fix: pointer/reference captures need varchar/C5 zone alloc.
            // is_capturable no longer allows these; return null AnyVal as fallback.
            return builder_.create<mlir::arith::ConstantIntOp>(loc_, 0, 32);
        case K::Struct:
            if (TypeRef(t).struct_name() == "AnyVal") {
                // C4 bug fix: use mlir::ArrayRef (not llvm::ArrayRef) for ExtractValueOp
                // to match the MLIR dialect API which takes mlir::ArrayRef<int64_t>.
                return builder_.create<mlir::LLVM::ExtractValueOp>(
                    loc_, v, mlir::ArrayRef<int64_t>{0});
            }
            break;
        default:
            break;
    }
    return builder_.create<mlir::arith::ConstantIntOp>(loc_, 0, 32);
}

// writ capture coercion: scalar capture value -> 8-byte VALUE-FORM WAny word.
// Pod = (v<<8)|(code<<1)|1 (bool code WA_BOOL=2 -> |5; ints as i56 code WA_I56=1
// -> |3). WAny captures pass their niche word through. Zone-alloc kinds
// (strings/floats/ptrs) are handled by the writ_ctr_alloc_* path, not here.
mlir::Value MLIRGenImpl::coerce_to_wany_raw(mlir::Value v, TypeRef t) {
    auto i64_mlir = builder_.getIntegerType(64);
    auto zero64 = [&]() {
        return builder_.create<mlir::arith::ConstantIntOp>(loc_, 0, 64);
    };
    if (!v || !t) return zero64();
    using K = LogosType::Kind;
    switch (TypeRef(t).kind()) {
        case K::Bool: {
            mlir::Value b = coerce_numeric(v, i64_mlir);
            mlir::Value shifted = builder_.create<mlir::arith::ShLIOp>(loc_, b,
                builder_.create<mlir::arith::ConstantIntOp>(loc_, 8, 64));
            return builder_.create<mlir::arith::OrIOp>(loc_, shifted,
                builder_.create<mlir::arith::ConstantIntOp>(loc_, 5, 64));
        }
        case K::I8:  case K::I16: case K::I32: case K::I64:
        case K::U8:  case K::U16: case K::U32: case K::U64:
        case K::I24: case K::U24: {
            mlir::Value iv = coerce_numeric(v, i64_mlir);
            mlir::Value shifted = builder_.create<mlir::arith::ShLIOp>(loc_, iv,
                builder_.create<mlir::arith::ConstantIntOp>(loc_, 8, 64));
            return builder_.create<mlir::arith::OrIOp>(loc_, shifted,
                builder_.create<mlir::arith::ConstantIntOp>(loc_, 3, 64));
        }
        case K::Enum:
            if (TypeRef(t).enum_name() == "WAny") {
                if (v.getType() == i64_mlir) return v;
                if (mlir::isa<mlir::LLVM::LLVMStructType>(v.getType()))
                    return builder_.create<mlir::LLVM::ExtractValueOp>(
                        loc_, v, mlir::ArrayRef<int64_t>{0});
                return coerce_numeric(v, i64_mlir);
            }
            break;
        case K::Struct:
            if (TypeRef(t).struct_name() == "AnyVal") {
                // Legacy 4-byte AnyVal word zero-extended (i24/bool Pod encodings
                // coincide with writ in the low 32 bits).
                mlir::Value w = builder_.create<mlir::LLVM::ExtractValueOp>(
                    loc_, v, mlir::ArrayRef<int64_t>{0});
                return coerce_numeric(w, i64_mlir);
            }
            break;
        default:
            break;
    }
    return zero64();
}

mlir::Value MLIRGenImpl::gen_expr_kind(lir_view::EReflectOfView v, TypeRef) {
    auto i8 = builder_.getIntegerType(8);

    // Compute symbol name deterministically from fqn (same formula as reflection_emit).
    std::string fqn;
    if (TypeRef et = v.type(pool_impl())) {
        auto pkg = et.pkg_name();
        auto sn  = et.struct_name();
        fqn = pkg.empty() ? std::string(sn) : std::string(pkg) + "::" + std::string(sn);
    }
    auto hash = logos::compiler::type_hash_23(fqn);
    static const char hexc[] = "0123456789abcdef";
    std::string sym_name = "__logos_reflect__";
    for (auto b : hash) { sym_name += hexc[b >> 4]; sym_name += hexc[b & 0xF]; }

    auto parent_mod = builder_.getBlock()->getParent()->getParentOfType<mlir::ModuleOp>();
    // Forward-declare the global as external if not already in the module.
    // reflection_emit emitted the real WeakODR global earlier in the same module.
    if (!parent_mod.lookupSymbol(sym_name)) {
        auto save_pt = builder_.saveInsertionPoint();
        builder_.setInsertionPointToStart(parent_mod.getBody());
        auto arr_type = mlir::LLVM::LLVMArrayType::get(i8, 1);
        builder_.create<mlir::LLVM::GlobalOp>(
            loc_, arr_type, /*isConstant=*/true, mlir::LLVM::Linkage::External,
            sym_name, mlir::Attribute{});
        builder_.restoreInsertionPoint(save_pt);
    }

    // ptr = address_of(global) + 8  (past size prefix, pointing to Writ payload)
    auto global_ptr = builder_.create<mlir::LLVM::AddressOfOp>(loc_, ptr_type(), sym_name);
    mlir::Value offset8 = builder_.create<mlir::arith::ConstantIntOp>(loc_, 8, 64);
    auto blob_ptr = builder_.create<mlir::LLVM::GEPOp>(
        loc_, ptr_type(), i8, global_ptr, mlir::ValueRange{offset8});

    // Return WritStatic { ptr: blob_ptr } as an alloca.
    auto sit = struct_types_.find("WritStatic");
    if (sit == struct_types_.end()) return blob_ptr;
    auto alloca = create_entry_alloca(sit->second.llvm_type);
    auto gep = gep_field(alloca, sit->second, "ptr");
    if (!gep) return blob_ptr;
    builder_.create<mlir::LLVM::StoreOp>(loc_, blob_ptr, gep);
    return alloca;
}

mlir::Value MLIRGenImpl::gen_expr_kind(lir_view::EWritLitView v, TypeRef ret_type) {
    // Metacall splice path: pre-serialised blob bypasses build_writ_zone.
    // Same rodata layout as the captures-free @-literal: [u64 size][bytes],
    // ptr returned by the wrapper points after the size prefix.
    // Helper: emit "AddressOf(global) + 8 → WritStatic alloca" given a global
    // symbol name. Used both on cache hit and after a fresh global is created.
    auto materialize_static = [&](const std::string& global_name) -> mlir::Value {
        auto i8 = builder_.getIntegerType(8);
        auto global_ptr = builder_.create<mlir::LLVM::AddressOfOp>(loc_, ptr_type(), global_name);
        mlir::Value offset8 = builder_.create<mlir::arith::ConstantIntOp>(loc_, 8, 64);
        auto blob_ptr = builder_.create<mlir::LLVM::GEPOp>(
            loc_, ptr_type(), i8, global_ptr, mlir::ValueRange{offset8});
        // Materialize as the WritLit's RESULT struct (WritStatic OR the
        // ABI-compatible ExprBlob — both a single `{ptr}`). A fn-macro quote
        // result is ExprBlob and may not import lang.writ.view, so WritStatic
        // can be registered-by-name but unlaid-out (null llvm_type) → prefer the
        // ret_type's registered struct, fall back to WritStatic.
        std::string sname = "WritStatic";
        if (ret_type && TypeRef(ret_type).kind() == LogosType::Kind::Struct) {
            std::string rn(TypeRef(ret_type).struct_name());
            if (!rn.empty()) {
                auto rit = struct_types_.find(rn);
                if (rit != struct_types_.end() && rit->second.llvm_type) sname = rn;
            }
        }
        auto sit = struct_types_.find(sname);
        if (sit == struct_types_.end() || !sit->second.llvm_type) return blob_ptr;
        auto alloca = create_entry_alloca(sit->second.llvm_type);
        auto gep = gep_field(alloca, sit->second, "ptr");
        if (!gep) {
            if (std::getenv("LOGOS_HLIT_DEBUG"))
                std::fprintf(stderr, "hlit: sname=%s rt=%s fields=%zu\n", sname.c_str(),
                    ret_type ? std::string(TypeRef(ret_type).struct_name()).c_str() : "<null>",
                    sit->second.fields.size());
            return blob_ptr;
        }
        builder_.create<mlir::LLVM::StoreOp>(loc_, blob_ptr, gep);
        return alloca;
    };

    if (auto sb = v.static_blob(); !sb.empty()) {
        auto i8 = builder_.getIntegerType(8);
        auto size_le = static_cast<uint64_t>(sb.size());
        std::string prefixed(8, '\0');
        for (int k = 0; k < 8; ++k)
            prefixed[k] = static_cast<char>((size_le >> (k * 8)) & 0xFF);
        prefixed.append(sb.begin(), sb.end());

        // Content-keyed cache: identical bytes share one rodata global.
        if (auto cit = writ_lit_global_cache_.find(prefixed); cit != writ_lit_global_cache_.end()) {
            return materialize_static(cit->second);
        }

        auto lit_idx     = writ_lit_counter_++;
        auto global_name = "__writ_blob_" + std::to_string(lit_idx);
        auto parent_mod  = builder_.getBlock()->getParent()->getParentOfType<mlir::ModuleOp>();
        auto save_pt     = builder_.saveInsertionPoint();
        builder_.setInsertionPointToStart(parent_mod.getBody());

        auto arr_type  = mlir::LLVM::LLVMArrayType::get(i8, prefixed.size());
        auto blob_attr = builder_.getStringAttr(
            llvm::StringRef(prefixed.data(), prefixed.size()));
        auto blob_global = builder_.create<mlir::LLVM::GlobalOp>(
            loc_, arr_type, /*isConstant=*/true, mlir::LLVM::Linkage::Internal,
            global_name, blob_attr);
        blob_global.setUnnamedAddr(mlir::LLVM::UnnamedAddr::Global);
        builder_.restoreInsertionPoint(save_pt);

        writ_lit_global_cache_[prefixed] = global_name;
        return materialize_static(global_name);
    }

    auto [blob, param_slots] = build_writ_zone(v);
    bool has_captures = v.has_captures();
    std::vector<TypeRef> capture_types;
    v.each_capture_type(pool_impl(), [&](TypeRef t){ capture_types.push_back(t); });
    std::vector<lir_view::ExprRef> capture_exprs;
    v.each_capture_expr([&](lir_view::ExprRef er){
        capture_exprs.push_back(er);
    });

    auto i8 = builder_.getIntegerType(8);

    // C8e: static @-literals (no captures) get an 8-byte little-endian size
    // prefix in rodata so that WritStatic::size() can read *(ptr - 8). The
    // resulting bytes are content-keyed in writ_lit_global_cache_ so
    // multiple references to the same const-value (e.g. an associated
    // constant accessed at multiple call sites) share one rodata global and
    // therefore one address.
    if (!has_captures) {
        auto size_le = static_cast<uint64_t>(blob.size());
        std::string prefixed(8, '\0');
        for (int k = 0; k < 8; ++k)
            prefixed[k] = static_cast<char>((size_le >> (k * 8)) & 0xFF);
        prefixed.append(blob.begin(), blob.end());

        if (auto cit = writ_lit_global_cache_.find(prefixed); cit != writ_lit_global_cache_.end()) {
            return materialize_static(cit->second);
        }

        auto lit_idx     = writ_lit_counter_++;
        auto global_name = "__writ_lit_" + std::to_string(lit_idx);
        auto parent_mod  = builder_.getBlock()->getParent()->getParentOfType<mlir::ModuleOp>();
        auto save_pt     = builder_.saveInsertionPoint();
        builder_.setInsertionPointToStart(parent_mod.getBody());

        auto arr_type  = mlir::LLVM::LLVMArrayType::get(i8, prefixed.size());
        auto blob_attr = builder_.getStringAttr(
            llvm::StringRef(prefixed.data(), prefixed.size()));
        auto blob_global = builder_.create<mlir::LLVM::GlobalOp>(
            loc_, arr_type, /*isConstant=*/true, mlir::LLVM::Linkage::Internal,
            global_name, blob_attr);
        blob_global.setUnnamedAddr(mlir::LLVM::UnnamedAddr::Global);
        builder_.restoreInsertionPoint(save_pt);

        writ_lit_global_cache_[prefixed] = global_name;
        return materialize_static(global_name);
    }

    // Capture path: distinct lit_idx + slots table; runtime-evaluated captures
    // mean we don't dedupe these globals.
    auto lit_idx    = writ_lit_counter_++;
    auto global_name = "__writ_lit_" + std::to_string(lit_idx);
    auto parent_mod  = builder_.getBlock()->getParent()->getParentOfType<mlir::ModuleOp>();
    auto save_pt     = builder_.saveInsertionPoint();
    builder_.setInsertionPointToStart(parent_mod.getBody());

    // Capture path: emit plain blob (no size prefix).
    auto arr_type = mlir::LLVM::LLVMArrayType::get(i8, blob.size());
    auto blob_attr = builder_.getStringAttr(
        llvm::StringRef(reinterpret_cast<const char*>(blob.data()), blob.size()));
    auto cap_blob_global = builder_.create<mlir::LLVM::GlobalOp>(
        loc_, arr_type, /*isConstant=*/true, mlir::LLVM::Linkage::Internal,
        global_name, blob_attr);
    cap_blob_global.setUnnamedAddr(mlir::LLVM::UnnamedAddr::Global);

    // ── Capture path ─────────────────────────────────────────────────────────
    // Emit slots table: array of u32 pairs [blob_off, value_idx, ...].
    auto slots_name = "__writ_slots_" + std::to_string(lit_idx);
    size_t n_slots  = param_slots.size();
    size_t n_values = capture_exprs.size();

    {
        auto u32_type  = builder_.getIntegerType(32);
        auto slots_arr = mlir::LLVM::LLVMArrayType::get(u32_type, n_slots * 2);
        llvm::SmallVector<uint32_t> slot_vals;
        for (auto& [off, vidx] : param_slots) { slot_vals.push_back(off); slot_vals.push_back(vidx); }
        auto slots_attr = mlir::DenseIntElementsAttr::get(
            mlir::RankedTensorType::get({static_cast<int64_t>(n_slots * 2)}, u32_type),
            llvm::SmallVector<uint32_t>(slot_vals));
        builder_.create<mlir::LLVM::GlobalOp>(
            loc_, slots_arr, /*isConstant=*/true, mlir::LLVM::Linkage::Internal,
            slots_name, slots_attr);
    }

    builder_.restoreInsertionPoint(save_pt);

    // Check if any capture requires zone allocation (f64, string, *const u8).
    // Zone-alloc captures need the Writ to exist before coercion, so we
    // use the writ_template_ctr_new + writ_ctr_alloc_* + writ_template_patch path.
    auto is_zone_alloc_cap = [](TypeRef t) -> bool {
        if (!t) return false;
        using K = LogosType::Kind;
        K tk = TypeRef(t).kind();
        if (tk == K::F64 || tk == K::F32 || tk == K::FloatLit) return true;
        if (tk == K::Ptr) return true;  // *const u8 → C-string varchar
        if (tk == K::Slice && TypeRef(t).elem() && TypeRef(t).elem().kind() == K::U8) return true; // str → varchar
        if (tk == K::Struct && TypeRef(t).struct_name() == "StringView") return true;
        return false;
    };
    bool any_zone_alloc = false;
    for (auto ct : capture_types) {
        if (is_zone_alloc_cap(ct)) { any_zone_alloc = true; break; }
    }

    // Shared: build slots_ptr, tmpl_ptr, tmpl_size_val, n_slots_v, n_values_v.
    mlir::Value tmpl_ptr_v = builder_.create<mlir::LLVM::AddressOfOp>(loc_, ptr_type(), global_name);
    mlir::Value tmpl_size_v = builder_.create<mlir::arith::ConstantIntOp>(
        loc_, static_cast<int64_t>(blob.size()), 64);
    mlir::Value slots_ptr_v = n_slots > 0
        ? builder_.create<mlir::LLVM::AddressOfOp>(loc_, ptr_type(), slots_name).getResult()
        : [&]() -> mlir::Value {
            mlir::Value z = builder_.create<mlir::arith::ConstantIntOp>(loc_, 0, 64);
            return builder_.create<mlir::LLVM::IntToPtrOp>(loc_, ptr_type(), z);
          }();
    mlir::Value n_slots_v  = builder_.create<mlir::arith::ConstantIntOp>(
        loc_, static_cast<int64_t>(n_slots), 64);
    mlir::Value n_values_v = builder_.create<mlir::arith::ConstantIntOp>(
        loc_, static_cast<int64_t>(n_values), 64);

    // Allocate resolved[] on stack: n_values × u64 (value-form WAny words).
    mlir::Value resolved_ptr = nullptr;
    auto u64_mlir = builder_.getIntegerType(64);
    if (n_values > 0) {
        auto arr_t = mlir::LLVM::LLVMArrayType::get(u64_mlir, n_values);
        resolved_ptr = create_entry_alloca(arr_t);
    } else {
        mlir::Value zero64 = builder_.create<mlir::arith::ConstantIntOp>(loc_, 0, 64);
        resolved_ptr = builder_.create<mlir::LLVM::IntToPtrOp>(loc_, ptr_type(), zero64);
    }

    // ── Zone-alloc path (C5): one or more captures need varchar/f64 in the zone. ─
    if (any_zone_alloc) {
        auto new_fn    = find_func_op(parent_mod, "writ_template_ctr_new");
        auto patch_fn  = find_func_op(parent_mod, "writ_template_install");
        auto alloc_f64_fn = find_func_op(parent_mod, "writ_ctr_alloc_f64");
        auto alloc_str_fn = find_func_op(parent_mod, "writ_ctr_alloc_str");
        auto alloc_cstr_fn = find_func_op(parent_mod, "writ_ctr_alloc_cstr");
        // C5-fix4: check all alloc helpers upfront — missing functions cause silent null AnyVal.
        if (!new_fn || !patch_fn || !alloc_f64_fn || !alloc_str_fn || !alloc_cstr_fn) {
            std::fprintf(stderr, "mlir_gen: writ zone-alloc helpers not found — "
                         "add 'use logos.lang.writ.tmpl;' to your file\n");
            return nullptr;
        }

        // Count zone-alloc captures for capacity estimate (4096 per string, 16 per f64/f32).
        // C5-fix3: only count zone-alloc captures (skip scalar/AnyVal captures).
        // C5-fix2: include K::FloatLit in the f64 branch (16 bytes), not the string branch.
        int64_t extra_cap_bytes = 0;
        for (auto ct : capture_types) {
            using K = LogosType::Kind;
            if (!ct || !is_zone_alloc_cap(ct)) continue;
            K ctk = TypeRef(ct).kind();
            if (ctk == K::F64 || ctk == K::F32 || ctk == K::FloatLit)
                extra_cap_bytes += 16;
            else
                extra_cap_bytes += 4096;  // string: generous estimate
        }
        mlir::Value extra_cap_v = builder_.create<mlir::arith::ConstantIntOp>(
            loc_, extra_cap_bytes, 64);

        // Create the Rc<Writ> sized for template + captures (the template is
        // copied + patched later by writ_template_install, which keeps the
        // blob base local).
        auto new_call = builder_.create<mlir::func::CallOp>(
            loc_, new_fn,
            mlir::ValueRange{tmpl_size_v, extra_cap_v});
        if (new_call.getNumResults() == 0) return nullptr;
        mlir::Value ctr_val  = new_call.getResult(0);
        mlir::Type  ctr_type = new_fn.getFunctionType().getResult(0);

        // Alloca the Rc so we can pass &Rc<Writ> to the alloc helpers.
        mlir::Value ctr_alloca = create_entry_alloca(ctr_type);
        builder_.create<mlir::LLVM::StoreOp>(loc_, ctr_val, ctr_alloca);

        // For each unique capture: gen_expr, coerce, store in resolved[i].
        for (size_t i = 0; i < n_values; ++i) {
            mlir::Value cap_val = gen_expr(capture_exprs[i]);
            if (!cap_val) cap_val = builder_.create<mlir::arith::ConstantIntOp>(loc_, 0, 32);

            TypeRef ct = capture_types[i];
            mlir::Value raw_u32 = nullptr;

            if (is_zone_alloc_cap(ct)) {
                using K = LogosType::Kind;
                K ctk = TypeRef(ct).kind();
                if ((ctk == K::F64 || ctk == K::F32 ||
                     ctk == K::FloatLit) && alloc_f64_fn) {
                    // Widen f32 → f64 if needed. FloatLit defaults to f64.
                    mlir::Value f64_val = cap_val;
                    if (ctk == K::F32) {
                        auto f64_type = builder_.getF64Type();
                        f64_val = builder_.create<mlir::arith::ExtFOp>(loc_, f64_type, cap_val);
                    }
                    // If FloatLit/F64 but value is f32-typed MLIR, widen.
                    if (f64_val && mlir::isa<mlir::Float32Type>(f64_val.getType())) {
                        auto f64_type = builder_.getF64Type();
                        f64_val = builder_.create<mlir::arith::ExtFOp>(loc_, f64_type, f64_val);
                    }
                    auto r = builder_.create<mlir::func::CallOp>(
                        loc_, alloc_f64_fn, mlir::ValueRange{ctr_alloca, f64_val});
                    raw_u32 = r.getNumResults() > 0 ? r.getResult(0) : nullptr;
                } else if (ctk == K::Ptr && alloc_cstr_fn) {
                    // *const u8 — treat as null-terminated C-string.
                    auto r = builder_.create<mlir::func::CallOp>(
                        loc_, alloc_cstr_fn, mlir::ValueRange{ctr_alloca, cap_val});
                    raw_u32 = r.getNumResults() > 0 ? r.getResult(0) : nullptr;
                } else if (ctk == K::Slice && TypeRef(ct).elem() && TypeRef(ct).elem().kind() == K::U8
                           && alloc_str_fn) {
                    // str (&[u8]) fat pointer — load ptr+len fields from the alloca.
                    auto stype = slice_llvm_type();
                    llvm::SmallVector<mlir::LLVM::GEPArg> pi{int32_t(0), int32_t(0)};
                    auto pp = builder_.create<mlir::LLVM::GEPOp>(loc_, ptr_type(), stype, cap_val, pi);
                    mlir::Value sv_ptr = builder_.create<mlir::LLVM::LoadOp>(loc_, ptr_type(), pp);
                    llvm::SmallVector<mlir::LLVM::GEPArg> li{int32_t(0), int32_t(1)};
                    auto lp = builder_.create<mlir::LLVM::GEPOp>(loc_, ptr_type(), stype, cap_val, li);
                    mlir::Value sv_len = builder_.create<mlir::LLVM::LoadOp>(
                        loc_, builder_.getI64Type(), lp);
                    auto r = builder_.create<mlir::func::CallOp>(
                        loc_, alloc_str_fn, mlir::ValueRange{ctr_alloca, sv_ptr, sv_len});
                    raw_u32 = r.getNumResults() > 0 ? r.getResult(0) : nullptr;
                } else if (ctk == K::Struct && TypeRef(ct).struct_name() == "StringView"
                           && alloc_str_fn) {
                    // StringView: extract ptr (field 0) and len (field 1).
                    mlir::Value sv_ptr = builder_.create<mlir::LLVM::ExtractValueOp>(
                        loc_, cap_val, mlir::ArrayRef<int64_t>{0});
                    mlir::Value sv_len = builder_.create<mlir::LLVM::ExtractValueOp>(
                        loc_, cap_val, mlir::ArrayRef<int64_t>{1});
                    // len is u64; writ_ctr_alloc_str takes i64 — reinterpret as i64.
                    auto i64_type = builder_.getIntegerType(64);
                    if (sv_len.getType() != i64_type)
                        sv_len = builder_.create<mlir::arith::BitcastOp>(loc_, i64_type, sv_len);
                    auto r = builder_.create<mlir::func::CallOp>(
                        loc_, alloc_str_fn, mlir::ValueRange{ctr_alloca, sv_ptr, sv_len});
                    raw_u32 = r.getNumResults() > 0 ? r.getResult(0) : nullptr;
                }
            } else {
                raw_u32 = coerce_to_wany_raw(cap_val, ct);
            }

            if (!raw_u32) raw_u32 = builder_.create<mlir::arith::ConstantIntOp>(loc_, 0, 64);

            // Store to resolved[i].
            llvm::SmallVector<mlir::Value> gep_idx{
                builder_.create<mlir::arith::ConstantIntOp>(loc_, 0, 64),
                builder_.create<mlir::arith::ConstantIntOp>(loc_, static_cast<int64_t>(i), 64)};
            auto arr_t = mlir::LLVM::LLVMArrayType::get(u64_mlir, n_values);
            auto slot_ptr = builder_.create<mlir::LLVM::GEPOp>(
                loc_, ptr_type(), arr_t, resolved_ptr, gep_idx);
            builder_.create<mlir::LLVM::StoreOp>(loc_, raw_u32, slot_ptr);
        }

        // Copy the template into the zone, patch PARAM slots, set the root.
        builder_.create<mlir::func::CallOp>(
            loc_, patch_fn,
            mlir::ValueRange{ctr_alloca, tmpl_ptr_v, tmpl_size_v,
                             slots_ptr_v, n_slots_v, resolved_ptr, n_values_v});

        // Return the Rc<Writ> by value (load from alloca).
        return builder_.create<mlir::LLVM::LoadOp>(loc_, ctr_type, ctr_alloca);
    }

    // ── Scalar-only path (C4): all captures are inline WAny Pods (no zone alloc). ──
    for (size_t i = 0; i < n_values; ++i) {
        mlir::Value cap_val = gen_expr(capture_exprs[i]);
        if (!cap_val) cap_val = builder_.create<mlir::arith::ConstantIntOp>(loc_, 0, 32);

        mlir::Value raw_u32 = coerce_to_wany_raw(cap_val, capture_types[i]);
        if (!raw_u32) raw_u32 = builder_.create<mlir::arith::ConstantIntOp>(loc_, 0, 64);

        llvm::SmallVector<mlir::Value> gep_idx{
            builder_.create<mlir::arith::ConstantIntOp>(loc_, 0, 64),
            builder_.create<mlir::arith::ConstantIntOp>(loc_, static_cast<int64_t>(i), 64)};
        auto arr_t = mlir::LLVM::LLVMArrayType::get(u64_mlir, n_values);
        auto slot_ptr = builder_.create<mlir::LLVM::GEPOp>(
            loc_, ptr_type(), arr_t, resolved_ptr, gep_idx);
        builder_.create<mlir::LLVM::StoreOp>(loc_, raw_u32, slot_ptr);
    }

    auto build_fn = find_func_op(parent_mod, "writ_build_from_template");
    if (!build_fn) {
        std::fprintf(stderr, "mlir_gen: writ_build_from_template not found — "
                     "add 'use logos.lang.writ.tmpl;' to your file\n");
        return nullptr;
    }
    llvm::SmallVector<mlir::Value> build_args{
        tmpl_ptr_v, tmpl_size_v, slots_ptr_v, n_slots_v, resolved_ptr, n_values_v};
    auto build_call = builder_.create<mlir::func::CallOp>(loc_, build_fn, mlir::ValueRange(build_args));
    if (build_call.getNumResults() == 0) return nullptr;
    return build_call.getResult(0);
}

} // namespace logos::compiler
