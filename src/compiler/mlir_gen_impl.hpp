// Logos project — https://github.com/victor-smirnov/logos
//
// mlir_gen_impl.hpp — MLIRGenImpl class definition shared across all mlir_gen_*.cpp TUs.
//
// Each mlir_gen_*.cpp includes this header and defines a subset of MLIRGenImpl methods.
// The class itself is declared here but NOT defined (no method bodies here).

#pragma once

#include "mlir_gen.hpp"

#include <logos/compiler/lir.hpp>
#include <logos/compiler/lir_mirror.hpp>
#include <logos/compiler/lir_view.hpp>
#include <logos/compiler/sema.hpp>
#include <logos/compiler/probe.hpp>
#include "layout_law.hpp"
#include "mangled_name.hpp"

#include <cstdlib>
#include <format>
#include <string>
#include <vector>

#include <mlir/IR/Builders.h>
#include <mlir/IR/BuiltinOps.h>
#include <mlir/IR/BuiltinTypes.h>
#include <mlir/IR/MLIRContext.h>
#include <mlir/IR/Verifier.h>
#include <mlir/Dialect/Func/IR/FuncOps.h>
#include <mlir/Dialect/Arith/IR/Arith.h>
#include <mlir/Dialect/SCF/IR/SCF.h>
#include <mlir/Dialect/ControlFlow/IR/ControlFlowOps.h>
#include <mlir/Dialect/LLVMIR/LLVMDialect.h>
#include <mlir/Dialect/LLVMIR/LLVMAttrs.h>   // DWARF debug-info attributes

#include <unordered_map>
#include <unordered_set>
#include <map>
#include <string>
#include <vector>
#include <cstdio>
#include <cstdarg>
#include <variant>

namespace logos::compiler {

using namespace lir;

// ---------------------------------------------------------------------------
// THE MLIR-TYPE LAYOUT ENGINE — the backend's rule, over LLVM-dialect types
// ---------------------------------------------------------------------------
//
// `mlir::DataLayout` MUST NOT be used to size a value. Even with the target's
// `dlti.dl_spec` attached (`attach_target_data_layout`), MLIR's LLVMStructType
// layout accumulates each member at its STORE size — `getTypeSize(i56)` = 7 —
// while `llvm::StructLayout`, which is what the emitted GEPs and the object
// actually use, accumulates ALLOC sizes (8). For `{i56, i8, i64}` MLIR answers
// 16 and LLVM answers 24 with `id` at offset 16, so an array-literal element
// memcpy sized from MLIR copied 16 bytes and every `id` past the first was
// whatever the neighbouring row happened to leave there. `8ba3c764` moved three
// engines onto one answer and stamped the layout spec on the module; this is
// the fourth reader, and no `dl_spec` can fix it because the divergence is in
// the STRUCT ACCUMULATION RULE, not in the leaf alignments.
//
// So the walk is written here, once, and every reader of an MLIR type's size /
// alignment / field offset goes through it. Leaf integers delegate to
// `LogosType::int_layout`, the same function `LogosType::scalar_layout` uses,
// so the TypeRef engine (`layout_of`) and this one cannot disagree at a leaf.
// That they do not disagree at an AGGREGATE either — and that both equal
// `llvm::DataLayout`'s own answer — is asserted by `verify_layout_engines()`,
// which runs on every compile.

inline uint64_t layout_align_up(uint64_t v, uint64_t a) {
    return a ? (v + a - 1) / a * a : v;
}

inline uint64_t mlir_abi_align(mlir::Type t) {
    if (auto it = mlir::dyn_cast<mlir::IntegerType>(t))
        return LogosType::int_layout(it.getWidth()).align;
    if (mlir::isa<mlir::Float32Type>(t)) return 4;
    if (mlir::isa<mlir::Float64Type>(t)) return 8;
    if (mlir::isa<mlir::LLVM::LLVMPointerType>(t)) return 8;
    if (auto st = mlir::dyn_cast<mlir::LLVM::LLVMStructType>(t)) {
        if (st.isPacked()) return 1;
        uint64_t m = 1;
        for (auto e : st.getBody()) m = std::max(m, mlir_abi_align(e));
        return m;
    }
    if (auto at = mlir::dyn_cast<mlir::LLVM::LLVMArrayType>(t))
        return mlir_abi_align(at.getElementType());
    return 1;
}

// ALLOC size — the stride the backend steps between two adjacent values of this
// type. Never the store size: `i56` occupies 8 bytes even though it writes 7.
inline uint64_t mlir_abi_size(mlir::Type t) {
    if (auto it = mlir::dyn_cast<mlir::IntegerType>(t))
        return LogosType::int_layout(it.getWidth()).size;
    if (mlir::isa<mlir::Float32Type>(t)) return 4;
    if (mlir::isa<mlir::Float64Type>(t)) return 8;
    if (mlir::isa<mlir::LLVM::LLVMPointerType>(t)) return 8;
    if (auto st = mlir::dyn_cast<mlir::LLVM::LLVMStructType>(t)) {
        bool packed = st.isPacked();
        uint64_t off = 0, maxa = 1;
        for (auto e : st.getBody()) {
            uint64_t a = packed ? 1 : mlir_abi_align(e);
            maxa = std::max(maxa, a);
            off = layout_align_up(off, a) + mlir_abi_size(e);
        }
        return layout_align_up(off, maxa);
    }
    if (auto at = mlir::dyn_cast<mlir::LLVM::LLVMArrayType>(t))
        return at.getNumElements() * mlir_abi_size(at.getElementType());
    return 1;
}

// Byte offset of member `idx` — the same accumulation, stopped early. This is
// what a `getelementptr %S, ptr, i32 0, i32 idx` resolves to.
inline uint64_t mlir_field_offset(mlir::LLVM::LLVMStructType st, unsigned idx) {
    bool packed = st.isPacked();
    uint64_t off = 0;
    auto body = st.getBody();
    for (unsigned i = 0; i < idx && i < body.size(); ++i) {
        off = layout_align_up(off, packed ? 1 : mlir_abi_align(body[i]))
              + mlir_abi_size(body[i]);
    }
    return layout_align_up(off, (packed || idx >= body.size())
                                    ? 1 : mlir_abi_align(body[idx]));
}

// ---------------------------------------------------------------------------
// Struct type registry (MLIR-level)
// ---------------------------------------------------------------------------

struct FieldInfo {
    std::string name;
    mlir::Type  type;
    uint32_t    index;
    std::string struct_name;   // non-empty if field is struct, *struct, &struct, &mut struct.
    std::string trait_name;    // non-empty if field is &dyn Trait / *const dyn Trait / *mut dyn Trait;
                               // signals struct-lit init to fat-pointer-coerce (B-dyn-field).
    bool        is_pointer = false;  // true for *T / &T / &mut T fields. The struct_name
                                     // is still populated (so chain-field access via the
                                     // pointer can resolve), but auto-Drop must skip these
                                     // — they don't own the pointee.
};

struct StructInfo {
    std::string                  name;
    mlir::LLVM::LLVMStructType   llvm_type;
    std::vector<FieldInfo>       fields;
};

// Tagged enum registry: { i32 discriminant, <payload blob of payload_align> }.
// The payload blob carries the widest variant's alignment so an i64 / ptr /
// align-8 struct payload lands on an aligned offset (LLVM places the blob after
// the disc with the needed padding); the enum's own alignment = max(4, align).
struct TaggedEnumInfo {
    std::string                         name;
    mlir::LLVM::LLVMStructType          llvm_type;
    uint64_t                            payload_bytes = 0;
    uint64_t                            payload_align = 1;  // max align over variants
    // Per-variant payload LLVM types (for bitcasting the payload area)
    struct VariantPayload {
        int64_t disc;
        std::vector<mlir::Type>        field_types;   // empty = no payload
        std::vector<TypeRef>  logos_types;   // parallel: original LogosType per field
    };
    std::vector<VariantPayload> variants;

    // F3 (ref-repr-design §8): `#[zoned2]` on the enum. The Ref (low-bit-0)
    // arm of this niche enum is stored SELF-RELATIVE at-rest and absolute as a
    // value — the storage/compute split, bridged by zoned_enum_materialize /
    // zoned_enum_lower (the generalized wa_materialize/wa_lower). Only
    // meaningful together with a LowBit niche.
    bool zoned = false;

    // Phase 3.5 niche optimization. When `packed`, the enum has NO separate
    // discriminant: it is laid out as just its payload (`llvm_type` is the
    // niche field), and the discriminant is encoded in an invalid bit-pattern
    // of that field. MVP = the null-pointer niche for an `Option`-shape enum
    // (2 variants: one fieldless = `none_disc`, one single non-null pointer
    // field = `some_disc`); the niche value is null (0) at offset 0, so
    // sizeof(Option<&T>) == sizeof(&T) == 8, matching Rust.
    struct Niche {
        // NullPtr: Option<&T>-shape — null encodes `none_disc`, the non-null ptr
        //          encodes `some_disc`. Stored as just the pointer (8B).
        // LowBit:  WAny-shape — two data arms disambiguated by the payload word's
        //          LOW BIT. The `ptr_disc` arm holds a pointer to an align≥2 pointee
        //          (so its low bit is always 0) stored RAW; the `val_disc` arm holds
        //          a ≤63-bit integer stored as `(value << 1) | 1` (low bit 1). Read:
        //          low bit 0 → ptr arm (word as ptr), low bit 1 → val arm (word >> 1).
        enum Kind { NoNiche, NullPtr, LowBit };
        Kind    kind      = NoNiche;
        bool    packed    = false;       // kind != NoNiche (enum is just its payload word)
        int64_t none_disc = 0;           // NullPtr: the null variant
        int64_t some_disc = 0;           // NullPtr: the pointer variant
        int64_t ptr_disc  = 0;           // LowBit: the low-bit-0 pointer arm
        int64_t val_disc  = 0;           // LowBit: the low-bit-1 value arm
        uint32_t val_bits = 0;           // LowBit: value arm's bit width (for read sign/zero-extend)
        bool     val_signed = false;     // LowBit: value arm signedness
        // LowBit RAW mode (`#[zoned2]` + a 64-bit val arm, e.g. WAny's Pod(u64)):
        // the val arm word is stored/read VERBATIM — no `(v<<1)|1` shift — because
        // the producer already bakes the low-bit-1 tag into it. Pod = the raw word,
        // Ref = low-bit-0 pointer. The disc is still the low bit.
        bool     val_raw  = false;
    };
    Niche niche;
};

// ---------------------------------------------------------------------------
// MLIRGenImpl
// ---------------------------------------------------------------------------

class MLIRGenImpl {
public:
    explicit MLIRGenImpl(mlir::MLIRContext& ctx)
        : builder_(&ctx)
        , loc_(builder_.getUnknownLoc())
    {}

    mlir::OwningOpRef<mlir::ModuleOp> generate(const LProgram& prog);

    // ── R2: the self-diagnosed-malfunction channel ───────────────────────
    // A message the compiler emits ABOUT ITSELF ("compiler bug", "unknown",
    // "unhandled", "DROPPED", "should have been expanded by") must make the
    // compile FAIL. mlir-gen had no diagnostic channel at all: it printed to
    // stderr, dropped the statement, wrote the object file and returned 0 —
    // so a test could "pass" while a WRITE had silently vanished.
    //
    // Enforced ONCE, at the single exit of generate(); never at the call
    // site. The three shapes exist only so a call site keeps its return type
    // — they are the SAME sink.
    size_t bug_count() const noexcept { return bugs_; }

    // -g: enable DWARF debug-info emission. Set before generate().
    void set_debug_info(bool v) { debug_info_ = v; }
    // Emit runtime overflow checks (trap) on integer +/-/*. Default ON
    // (Logos's safety-first stance). When OFF (`-C overflow-checks=off`), `+`/
    // `-`/`*` lower to plain wrapping arith — vectorizable, branchless, matching
    // rustc release-mode arithmetic. Explicit wrapping_add/sub/mul are
    // unaffected (always unchecked). Set before generate().
    void set_overflow_checks(bool v) { overflow_checks_ = v; }
    // Resolved backend target CPU has BMI2 → pdep_u64/pext_u64 lower to the
    // inline hardware intrinsic instead of the rt cpuid-dispatch fallback.
    void set_target_bmi2(bool v) { target_has_bmi2_ = v; }
    // Resolved backend target CPU name — feeds the module's data-layout spec
    // (attach_target_data_layout). Before generate().
    void set_target_cpu(std::string_view s) { target_cpu_.assign(s); }
    // Primary input source path (DWARF CU file + per-fn fallback). Before generate().
    void set_main_source(std::string_view s) { main_source_.assign(s); }
    void set_shard(int idx, int cnt) { shard_index_ = idx; shard_count_ = cnt; }
    // #61: this gen is one METAPROG FIXPOINT ROUND's JIT gen, not the final
    // object gen. In such a round the program is a SNAPSHOT taken before the
    // metaprog has finished emitting items, so a user struct whose field type
    // is still an unresolved projection (`<typeof(C) as CtrLeafFamily>::…`
    // before the container item's handler has produced `<C>Cfg`) is NOT a
    // malformed program — it is a type that does not exist YET. Registering it
    // is skipped for THIS round only; a metaprog function that actually
    // references the struct still fails loud through the normal
    // unknown-struct path, and the FINAL gen (flag false) refuses exactly as
    // before. See mlir_gen.cpp pass0.
    void set_metaprog_round(bool v) { metaprog_round_ = v; }

    // #120 — A TRAP STUB IN THE FINAL ROUND IS A MALFUNCTION, NOT A NOTE.
    // `poisoned_fns` demotes a function to `ud2` when mono could not
    // instantiate something it needed. Inside a metaprog round that is
    // EXPECTED and harmless: the round is superseded and its object is
    // thrown away — `mono_scan.cpp`'s own message says "expected only before a
    // metaprog emission round". Surviving into the FINAL emission it is
    // neither: the object is written, `logosc` exits 0, and the program dies
    // with SIGILL the moment it reaches the stub.
    //
    // MEASURED (#120): `a.and::<i64>(b)` binds the method-level tparam to the
    // wrong slot, `main` itself is demoted, `logosc: wrote …`, rc 0, and the
    // linked binary dies rc 132 with ZERO destructor calls. Every pass fixture
    // asserts an exit code, so a compile that replaces `main` with a trap and
    // exits 0 is invisible to the corpus BY CONSTRUCTION — the same reasoning
    // that made the `mlir_gen:` channel fatal in #103, one layer up.
    //
    // Routed through the SAME sink, so it is counted, `internal:`-marked and
    // enforced by the existing exit-code machinery rather than a second
    // mechanism that could disagree with it.
    void trap_demotion_check(const std::string& link) {
        if (metaprog_round_) return;
        bug_printf("function '%s' was demoted to a trap stub in the FINAL "
                   "emission round — mono could not instantiate something it "
                   "needs, so this symbol is `ud2` and any call to it dies "
                   "with SIGILL",
                   link.c_str());
    }

private:
    mlir::OpBuilder builder_;
    mlir::Location  loc_;
    std::string     target_cpu_;

    // Stamp the module with the BACKEND's data layout (dlti.dl_spec +
    // llvm.data_layout) so `mlir::DataLayout` and ISel are the same oracle.
    void attach_target_data_layout(mlir::ModuleOp mod);

public:
    // `lo <= scrut && scrut <= hi` for an integer range PATTERN — the ONE
    // emitter behind every site that compiles one (`match`, `if let`,
    // `let … else`, `while let`, at-bindings, or-alternatives, nested
    // `pat_test`). The comparison predicate is a property of the SCRUTINEE'S
    // TYPE and is read from `is_unsigned_repr_kind` here, once.
    //
    // Why it is a function and not a two-line idiom: it WAS the idiom, spelled
    // out at seven sites. `let 100u8..=200u8 = x else {…}` hardcoded `sge`/`sle`
    // and took the else branch for x = 150u8, while `match` on the identical
    // pattern was right; `pat_test` re-derived "is this unsigned" from its own
    // list of kind constants and left out u24, u56 and u128. A site that cannot
    // spell the predicate cannot spell it wrong.
    // The bound is 128 bits WIDE, not 64: it is as wide as the widest scrutinee
    // the language has. A 64-bit parameter truncated every i128/u128 bound.
    mlir::Value emit_range_test(mlir::Value scrut, TypeRef scrut_ty,
                                __int128 lo, __int128 hi);
private:

    // ── DWARF debug info (-g) ─────────────────────────────────────────────
    // Path: per-stmt FileLineColLoc fused with the current fn's DISubprogram →
    // translateModuleToLLVMIR emits DWARF. Locations are only debug-fused while
    // emitting a TOP-LEVEL function body; nested compiler-generated functions
    // (closures, drop glue, ctors) suspend the scope via DebugScopeSuspend so a
    // single DISubprogram never leaks onto two LLVM functions.
    bool                              debug_info_ = false;
    // Silent-drop accounting for the method-call lowering. The give-up path in
    // gen_expr_kind(EMethodCallView) (callee FuncOp missing) is deliberately
    // quiet, because DEAD generic instantiations legitimately reference
    // FuncOps mono never synthesized. But the same silence also swallows LIVE
    // calls whose instantiation mono simply missed — that is how the whole
    // payload write of logos.mem.pkd.pdtbuf's variable-length rows vanished
    // with a clean build. The counter lets the STATEMENT level tell the two
    // apart: a miss that coincides with a statement lowering to nothing is a
    // dropped effect, not a dead instantiation.
    size_t                            method_lower_misses_ = 0;
    std::string                       last_method_miss_;
    bool                              overflow_checks_ = true;  // trap on int +/-/* overflow (off = wrapping)
    bool                              target_has_bmi2_ = false; // target cpu has BMI2 (pdep/pext inline)
    std::string                       main_source_;      // primary input path (CU file + fallback)
    int                               shard_index_ = -1;  // <0 = emit every body
    int                               shard_count_ = 1;
    bool                              metaprog_round_ = false;  // #61
    mlir::LLVM::DICompileUnitAttr     di_cu_;            // one per module (lazy)
    mlir::LLVM::DISubprogramAttr      di_subprogram_;    // current fn (null outside a body)
    mlir::LLVM::DIFileAttr            di_file_;          // current fn's file
    uint32_t                          di_scope_line_ = 0;// current fn's decl/scope line
    std::unordered_map<std::string, mlir::LLVM::DIFileAttr> di_files_;  // path → DIFile cache

    // DIFile for `path` (split dir/name), cached.
    mlir::LLVM::DIFileAttr di_file_for(std::string_view path);
    // The single module compile unit, created on first use against `file`.
    mlir::LLVM::DICompileUnitAttr ensure_di_cu(mlir::LLVM::DIFileAttr file);
    // Build (and install in di_subprogram_/di_file_) the DISubprogram for `fn`,
    // attach it to `func`'s location so the translator sets its subprogram.
    // No-op unless debug_info_. Returns false on nothing-to-do.
    void begin_fn_debug(mlir::func::FuncOp func, lir_view::FunctionView fn);
    // Clear current-fn debug scope (loc_ → unknown, di_subprogram_ → null).
    void end_fn_debug();
    // Fused (FileLineColLoc, di_subprogram_) location for `line`; falls back to
    // the scope line for line==0. Returns unknown loc when debug is inactive.
    mlir::Location dbg_loc(uint32_t line);

    // ── DI type builder (Stage 2) ─────────────────────────────────────────
    // LogosType → DWARF DITypeAttr. Scalars→DIBasicType, ptr/ref→DIDerivedType
    // (pointer), struct/tuple→DICompositeType with members+offsets; aggregates
    // not yet modelled get an opaque sized composite. Cached by TypeRef offset;
    // di_struct_inprogress_ guards self-referential structs (via pointer fields).
    std::unordered_map<writ::arena_offset_t, mlir::LLVM::DITypeAttr> di_type_cache_;
    std::unordered_set<std::string> di_struct_inprogress_;
    mlir::LLVM::DITypeAttr di_type(TypeRef t);
    mlir::LLVM::DITypeAttr di_struct_type(TypeRef t);
    mlir::LLVM::DITypeAttr di_leaf_from_mlir(mlir::Type t);
    // Build the DISubroutineType (ret + param DI types) for a function.
    mlir::LLVM::DISubroutineTypeAttr di_subroutine_type(lir_view::FunctionView fn);

    // Enum pretty-print metadata: MLIR 20 can't express DWARF variant parts, so
    // per-enum-instance layout (disc / variant names / payload types / niche) is
    // collected here (keyed by the DWARF type name = type_str) as JSON and
    // emitted as the `__logos_debug_meta` global, which the gdb printer reads.
    std::map<std::string, std::string> di_enum_meta_;
    void collect_enum_meta(TypeRef t);
    void emit_debug_metadata(mlir::ModuleOp mod);

    // RAII: suspend the active debug scope while emitting a nested compiler-
    // generated function (its FuncOp + body must NOT inherit the caller's
    // DISubprogram). Restores on scope exit.
    struct DebugScopeSuspend {
        MLIRGenImpl* self;
        mlir::LLVM::DISubprogramAttr saved_sp;
        mlir::Location               saved_loc;
        explicit DebugScopeSuspend(MLIRGenImpl* s)
            : self(s), saved_sp(s->di_subprogram_), saved_loc(s->loc_) {
            self->di_subprogram_ = {};
            self->loc_ = self->builder_.getUnknownLoc();
        }
        ~DebugScopeSuspend() {
            self->di_subprogram_ = saved_sp;
            self->loc_ = saved_loc;
        }
    };

    // Set in generate(); used by view-ref helpers below to resolve LExpr*/LStmt*/Pattern*
    // back to mirror offsets so callers can read fields through lir_view types.
    const LProgram*       prog_   = nullptr;
    const LirMirrorTable* mirror_ = nullptr;

    // Resolve an ExprRef back to its variant LExpr* via the mirror's reverse
    // map. Used inside view-handlers to recurse through gen_expr() on
    // sub-expressions while the rest of the dispatcher still walks variants.
    lir_view::StmtRef stmt_ref_of(const lir_view::StmtRef& s) const noexcept {
        return s;
    }
    lir_view::PatRef pat_ref_of(const Pattern& p) const noexcept {
        if (!mirror_ || !prog_) return {};
        auto it = mirror_->pat.find(&p);
        if (it == mirror_->pat.end()) return {};
        return lir_view::PatRef(prog_->type_pool.arena(), it->second);
    }
    // Resolve `<struct>__<method>` to the actual mangled fn symbol in
    // prog_->structs (sema may append `__f__sig` / `__g__sig` under
    // overload mangling). Returns the bare convention name as fallback
    // when no match is found.
    //
    // ⚠ METHOD RESOLUTION IS PACKAGE-SCOPED. The scan below used to be
    // `if (sd.name() != bare_struct) continue;` over EVERY struct in the
    // program — bare, package-blind, first-registered-wins — so a user
    // `struct String` took `logos.mem.string.String`'s methods and a user
    // `struct Ident` took `logos.std.compiler.metaprog.Ident`'s. Both were
    // RUNTIME SIGSEGVs, measured (fixtures mlirgen_odr_drop_glue_homonym,
    // mlirgen_odr_operator_homonym): the element drop glue of a
    // `Vec<test.String>` emitted a call to
    // `logos.mem.string.String__drop__f__String` and handed it a
    // `%test.String`, and `a == b` on the user's own `Ident` emitted
    // `logos.std.compiler.metaprog.Ident__eq__f__ref_Ident__ref_Ident`.
    // The operator case is the sharper one: the collision-free twin does not
    // COMPILE ("has no method"), so the homonym program was only ever
    // accepted because it silently stole a foreign package's method.
    //
    // `pkg` is the owning package of the struct the CALLER means (from the
    // TypeRef's `pkg_name()`); empty means "no package in hand", which
    // reproduces the old behaviour exactly. The order is sema's recorded
    // find_struct_repr_ order — ⚠ QUALIFIED KEY FIRST, BARE SLOT LAST — with
    // one added rule that is the actual fix: if a struct of exactly that
    // package+name EXISTS, its method table is AUTHORITATIVE. Not finding
    // the method there means the type has no such method, and falling
    // through to a homonym in another package would be the theft above. The
    // bare pass still runs for every call that names no package and for
    // every name whose owning package holds no such struct, so no site that
    // resolved before becomes a miss.
    // `pkg_owns_struct`, when non-null, is set true iff a struct of exactly
    // `pkg` + this bare name EXISTS in the program. A caller that gets
    // `*pkg_owns_struct == true` back together with the plain `base` fallback
    // has an AUTHORITATIVE NEGATIVE — that type has no such method — and must
    // not go on to resolve `base` through any package-blind channel.
    std::string resolve_method_symbol(std::string_view struct_name,
                                      std::string_view method_name,
                                      std::string_view pkg = {},
                                      bool* pkg_owns_struct = nullptr) const noexcept {
        if (pkg_owns_struct) *pkg_owns_struct = false;
        auto bare_struct = strip_struct_pkg(struct_name);
        std::string base; base.reserve(bare_struct.size() + 2 + method_name.size());
        base.append(bare_struct); base.append("__"); base.append(method_name);
        if (!prog_) return base;
        // After unification, method names may be `[pkg.]Base__method[__f__sig]`.
        // Match either bare base or a pkg-qualified form ending with `.base`.
        auto matches_base = [&](std::string_view nm, const std::string& b) -> bool {
            if (nm == b) return true;
            // Check `Base__method__[fg]__sig` exact prefix
            bool starts = nm.size() > b.size() &&
                          nm.compare(0, b.size(), b) == 0 &&
                          (nm.compare(b.size(), 5, "__f__") == 0 ||
                           nm.compare(b.size(), 5, "__g__") == 0);
            if (starts) return true;
            // Check `pkg.Base__method[__[fg]__sig]` — pkg may have inner dots,
            // so split at the LAST dot (boundary between pkg and bare name).
            auto dot = nm.rfind('.');
            if (dot != std::string_view::npos) {
                std::string_view rest = nm.substr(dot + 1);
                if (rest == b) return true;
                if (rest.size() > b.size() &&
                    rest.compare(0, b.size(), b) == 0 &&
                    (rest.compare(b.size(), 5, "__f__") == 0 ||
                     rest.compare(b.size(), 5, "__g__") == 0))
                    return true;
            }
            return false;
        };
        // THE DESTRUCTOR IS THE `Drop` TRAIT'S `drop`. When an inherent method
        // and the `Drop` impl share the name, `collect_fn`'s G156-5 leaves the
        // inherent on the plain base and files the trait one under
        // `<T>__Drop__drop`; a name match on `<T>__drop` therefore answers with
        // the INHERENT method, which is not a destructor. Ask for the qualified
        // key first and only then for the plain one. PROBES.md 2026-09-04drop §3 site C.
        std::string qbase;
        if (method_name == "drop") qbase = std::string(bare_struct) + "__Drop__drop";
        // `stop_on_owned` reproduces the authoritative-negative rule below; the
        // qualified probe must NOT stop there, or a type whose only `drop` is
        // inherent would resolve to a symbol that does not exist.
        auto search = [&](const std::string& b, bool stop_on_owned) -> std::string {
            auto matches = [&](std::string_view nm) { return matches_base(nm, b); };
            auto scan_methods = [&](lir_view::StructView sd) -> std::string {
                std::string found;
                sd.each_method([&](lir_view::FunctionView mp) {
                    if (found.empty() && matches(mp.name())) found = std::string(mp.name());
                });
                return found;
            };
            // Pass 1 — QUALIFIED. Only the struct the caller's package actually
            // names. `owned` records that such a struct EXISTS, which is what
            // closes the bare pass off below.
            bool owned = false;
            if (!pkg.empty()) {
                for (auto& sd : prog_->structs) {
                    if (sd.name() != bare_struct || sd.pkg() != pkg) continue;
                    owned = true;
                    if (pkg_owns_struct) *pkg_owns_struct = true;
                    if (auto found = scan_methods(sd); !found.empty()) return found;
                }
                // Free-function / trait-impl form, same package only. `matches`
                // accepts a bare spelling too, which for a pkg-owned struct is
                // that struct's own un-mangled method — keep it.
                for (auto& fn : prog_->functions) {
                    if (!fn) continue;
                    std::string_view nm = fn.name();
                    auto dot = nm.rfind('.');
                    std::string_view fpkg = dot == std::string_view::npos
                                                ? std::string_view{} : nm.substr(0, dot);
                    if (!fpkg.empty() && fpkg != pkg) continue;
                    if (matches(nm)) return std::string(nm);
                }
                // The package OWNS a struct of this name and it has no such
                // method: that is the answer. Falling through would steal a
                // homonym's (measured SIGSEGVs — see the note above).
                if (owned) return stop_on_owned ? b : std::string{};
            }
            // Pass 2 — BARE, LAST RESORT. Reached when the caller named no
            // package, or when no struct of that package+name exists at all.
            for (auto& sd : prog_->structs) {
                if (sd.name() != bare_struct) continue;
                if (auto found = scan_methods(sd); !found.empty()) return found;
            }
            for (auto& fn : prog_->functions) {
                if (!fn) continue;
                if (matches(fn.name())) return std::string(fn.name());
            }
            return stop_on_owned ? b : std::string{};
        };
        if (!qbase.empty()) {
            if (auto q = search(qbase, false); !q.empty()) return q;
            if (pkg_owns_struct) *pkg_owns_struct = false;
        }
        return search(base, true);
    }

    const TypePoolImpl* pool_impl() const noexcept {
        return prog_ ? prog_->type_pool.impl() : nullptr;
    }

    std::unordered_map<std::string, StructInfo>        struct_types_;
    std::unordered_map<std::string, lir_view::StructView> all_struct_defs_; // name→def for recursive registration
    std::unordered_map<std::string, lir_view::EnumView> enum_types_;
    std::unordered_map<std::string, TaggedEnumInfo>    tagged_enums_;
    std::unordered_map<std::string, mlir::Type>        type_aliases_;
    // G156-1: package-scoped consts. module_consts_ is keyed by the
    // package-qualified `pkg::name` (matches sema). const_pkg_of_ maps a bare
    // name → its sole owning package; ambiguous_const_names_ holds names in ≥2
    // packages. A const VarRef (BARE in the LIR) is resolved current-function-
    // package first, then a uniquely-named const — identical to sema's
    // resolve_const_key, so both layers pick the SAME const by construction.
    std::unordered_map<std::string, lir_view::ConstView> module_consts_;
    std::unordered_map<std::string, std::string> const_pkg_of_;
    std::unordered_set<std::string> ambiguous_const_names_;
    std::string cur_fn_pkg_;   // owning package of the function being lowered
    void const_index_add_(std::string_view pkg, std::string_view name) {
        auto [it, ins] = const_pkg_of_.emplace(std::string(name), std::string(pkg));
        if (!ins && it->second != pkg) ambiguous_const_names_.insert(std::string(name));
    }
    const lir_view::ConstView* resolve_const_(std::string_view name) const {
        std::string q = cur_fn_pkg_.empty() ? std::string(name)
                                            : cur_fn_pkg_ + "::" + std::string(name);
        if (auto it = module_consts_.find(q); it != module_consts_.end()) return &it->second;
        if (!ambiguous_const_names_.count(std::string(name))) {
            auto pit = const_pkg_of_.find(std::string(name));
            if (pit != const_pkg_of_.end()) {
                std::string k = pit->second.empty() ? std::string(name)
                                                    : pit->second + "::" + std::string(name);
                if (auto it = module_consts_.find(k); it != module_consts_.end()) return &it->second;
            }
        }
        return nullptr;
    }
    // logos_to_mlir cache keyed by TypeRef offset. Same TypeRef
    // value appears in many fn signatures (e.g. `&self` across 50+
    // methods on the same struct); without the cache, make_fn_type
    // re-computes the MLIR Type for each occurrence. Offsets are
    // stable for the lifetime of a single mlir_gen invocation (the
    // LProgram's type_pool arena isn't mutated by mlir_gen).
    std::unordered_map<writ::arena_offset_t, mlir::Type> logos_to_mlir_cache_;

    // Names already forward-declared in the current generate() pass.
    // Replaces `mod.lookupSymbol(name)` as a duplicate-declaration guard:
    // MLIR's SymbolTable cache is invalidated by every push_back so each
    // lookupSymbol walks the module afresh — O(n) per call, O(n²) total
    // across 3500+ symbols.
    std::unordered_set<std::string>                        declared_fn_names_;
    std::unordered_set<std::string>                    vararg_fns_;  // names of vararg extern fns

    // Per-function: variables holding &dyn Trait values (name → trait name).
    std::unordered_map<std::string, std::string>  var_dyn_trait_;
    // Subset of var_dyn_trait_ declared as RAW `*const/*mut dyn` — those keep
    // 8-byte HANDLE store semantics on assignment (aliasing is the point of a
    // raw pointer); reference/Box dyn vars get the fat-pair coercion+memcpy.
    std::unordered_set<std::string>               var_raw_dyn_;
    // Build a {data,vtable} fat pair from a CONCRETE source value (peeling
    // Ref/MutRef/Ptr, unwrapping an owning Box<T>), vtable keyed on
    // trait_name. ONE coercion shared by every dyn store site (let/assign) —
    // gen_return keeps its heap-handle variant.
    mlir::Value coerce_concrete_source_to_dyn(mlir::Value data_ptr,
                                              TypeRef s_val_ty,
                                              std::string_view trait_name);
    // Function name → Logos-level parameter types (for dyn coercion at call sites).
    std::unordered_map<std::string, std::vector<TypeRef>> fn_param_types_;
    // Function name → per-param owning-Box<dyn> flag: the param collapsed to a
    // bare TraitObject but the callee owns+frees the heap handle, so the call
    // site must coerce the arg to a HEAP fat handle (heap=true).
    std::unordered_map<std::string, std::vector<bool>> fn_param_owning_box_dyn_;

    // Per-function state.
    std::unordered_map<std::string, mlir::Value>  scope_;
    std::unordered_set<std::string>               let_vars_;
    // B8 dynamic drop flags: a `let mut x: T;` declared WITHOUT an initializer
    // gets a hidden i8 flag (0 = slot empty, 1 = holds a live value), like
    // Rust's drop flags. Each assignment drops the OLD value only if the flag
    // is set, then sets it; scope-exit/return drops only if set. This gives
    // exact drop semantics for conditionally-initialized vars (`let mut x; if c
    // { x = a; } x = b;` drops `a` iff c was true) that no static analysis can
    // resolve. name → flag alloca.
    std::unordered_map<std::string, mlir::Value>  uninit_drop_flag_;
    // B8 drop elaboration (Rust-style): a declared-uninit var needs a RUNTIME
    // drop flag ONLY if its init state is not statically determinable — i.e. it
    // has an assignment nested inside a conditional/loop (deeper than its decl).
    // Determined by a pre-scan of the fn body (prescan_uninit_flags). Vars whose
    // every assignment statically dominates (straight-line) are flag-FREE: drops
    // are placed statically via the `assigned` set tracked during codegen
    // (uninit_static_ = needs static tracking, uninit_assigned_ = currently holds
    // a live value at this codegen point). This elides the flag + branch for the
    // common straight-line case, matching Rust's MIR drop elaboration.
    std::unordered_set<std::string>               uninit_flag_needed_;
    std::unordered_set<std::string>               uninit_static_;
    std::unordered_set<std::string>               uninit_assigned_;
    void prescan_uninit_flags(lir_view::BlockRef block, int depth,
                              std::unordered_map<std::string, int>& decl_depth);
    // Per-function: let-vars bound directly from a container accessor returning
    // `*const/*mut dyn` (e.g. `let p = map.get(&k);` → `*const Box<dyn>`). Such a
    // var holds a pointer-INTO-storage, so `*p` must LOAD the stored handle —
    // unlike a coerced `*const dyn` handle / param / field, where `*p` is a no-op.
    // The two are type-indistinguishable (both `Ptr<TraitObject>`); we track the
    // accessor-return provenance here. See gen_expr_kind(EDerefView).
    std::unordered_set<std::string>               dyn_ptr_to_handle_vars_;
    std::unordered_map<std::string, mlir::Type>   var_elem_types_;
    std::unordered_map<std::string, std::string>  var_struct_;
    std::unordered_map<std::string, mlir::Type>   var_subscript_;
    // Slice-typed variables/params (`&[T]` / `&mut [T]` — Kind::Slice). scope_
    // holds a pointer to the fat `{ptr, len}` descriptor, so indexed read/write
    // must GEP field 0 + load the data pointer before striding by element.
    // Maps name → element MLIR type (the GEP stride for the data array).
    std::unordered_map<std::string, mlir::Type>   var_slice_;
    std::unordered_set<std::string>              var_tuple_;
    std::unordered_set<std::string>              var_tagged_enum_;
    // Mutable tagged-enum variables use a pointer slot (alloca-of-ptr) for rebinding.
    // scope_[name] = ptr_slot alloca; reading loads the ptr; assigning stores new ptr.
    std::unordered_set<std::string>              var_tagged_enum_ptr_;
    // Local let-bound pointer variables (*mut T / *const T): maps name → pointee MLIR type.
    // Needed because scope_[name] is an alloca(ptr), so indexing requires a load first.
    std::unordered_map<std::string, mlir::Type>   var_local_ptrs_;

    // Full snapshot of the name-keyed variable-classification state, for correct
    // LEXICAL scoping of if-branches / loop bodies / match arms. These maps are
    // keyed by bare name; without restoring them at scope exit, a `let` in one
    // branch leaks its classification and corrupts a SIBLING `let` of the same
    // name but a different kind (e.g. `let k: WAny` then a sibling `let mut k: i64`),
    // which then mis-resolves in VarRef (returns the alloca ptr instead of a load).
    struct VarScopeSnapshot {
        std::unordered_map<std::string, mlir::Value>  scope;
        std::unordered_map<std::string, std::string>  dyn_trait;
        std::unordered_map<std::string, std::string>  var_struct;
        std::unordered_map<std::string, mlir::Type>   elem_types;
        std::unordered_map<std::string, mlir::Type>   subscript;
        std::unordered_map<std::string, mlir::Type>   local_ptrs;
        std::unordered_map<std::string, mlir::Type>   slice;
        std::unordered_set<std::string>               let_vars;
        std::unordered_set<std::string>               tuple;
        std::unordered_set<std::string>               tagged_enum;
        std::unordered_set<std::string>               tagged_enum_ptr;
        std::unordered_set<std::string>               raw_dyn;
        std::unordered_set<std::string>               dyn_ptr_handle;
        std::unordered_set<std::string>               ref_params;
        std::unordered_set<std::string>               ptr_family;
    };
    VarScopeSnapshot snapshot_var_scope() const {
        return { scope_, var_dyn_trait_, var_struct_, var_elem_types_, var_subscript_,
                 var_local_ptrs_, var_slice_, let_vars_, var_tuple_, var_tagged_enum_,
                 var_tagged_enum_ptr_, var_raw_dyn_, dyn_ptr_to_handle_vars_,
                 ref_param_names_, ptr_family_param_ };
    }
    // Restore by full assignment: erases bindings introduced inside the scope AND
    // re-instates any shadowed outer bindings — exact lexical-scope semantics.
    void restore_var_scope(const VarScopeSnapshot& s) {
        scope_                  = s.scope;
        var_dyn_trait_          = s.dyn_trait;
        var_struct_             = s.var_struct;
        var_elem_types_         = s.elem_types;
        var_subscript_          = s.subscript;
        var_local_ptrs_         = s.local_ptrs;
        var_slice_              = s.slice;
        let_vars_               = s.let_vars;
        var_tuple_              = s.tuple;
        var_tagged_enum_        = s.tagged_enum;
        var_tagged_enum_ptr_    = s.tagged_enum_ptr;
        var_raw_dyn_            = s.raw_dyn;
        dyn_ptr_to_handle_vars_ = s.dyn_ptr_handle;
        ref_param_names_        = s.ref_params;
        ptr_family_param_       = s.ptr_family;
    }
    // Peer-shape eviction — THE binder foundation (gap C, 2026-07). All the
    // per-var classification maps above are keyed by BARE name; a fresh binding
    // of a name (`let`, pattern bind, loop var) must first drop every stale
    // shape claim a previous same-named binding left behind, else uses of the
    // NEW binding mis-resolve through the OLD shape (e.g. a stale var_struct_
    // entry makes a scalar re-let read as a struct pointer → verifier ICE or
    // silent garbage). Every binder registration site calls this BEFORE
    // claiming its own shape (and AFTER generating its initializer, which may
    // legitimately read the OLD binding: `let x: u64 = x.field;`).
    // Deliberately does NOT touch scope_/let_vars_ (immediately overwritten by
    // the caller) nor the uninit_* drop-elaboration state (B8 machinery resets
    // it at declare-without-init sites; SDrop placement depends on it).
    void evict_var_shapes(const std::string& n) {
        var_struct_.erase(n);
        var_subscript_.erase(n);
        var_tuple_.erase(n);
        var_tagged_enum_.erase(n);
        var_tagged_enum_ptr_.erase(n);
        var_dyn_trait_.erase(n);
        var_raw_dyn_.erase(n);
        var_local_ptrs_.erase(n);
        var_elem_types_.erase(n);
        var_slice_.erase(n);
        dyn_ptr_to_handle_vars_.erase(n);
        ref_param_names_.erase(n);
        ptr_family_param_.erase(n);
    }
    // Names of fn parameters whose type is Ref/MutRef. `&p` for such a
    // param means "address of param storage" — we must spill the SSA
    // arg into an entry alloca and return the alloca. Without the
    // spill, `&p` returns p itself (the inner pointer), breaking
    // `&&mut T` chains (was B3-bg-03 / Sprint 6).
    std::unordered_set<std::string>               ref_param_names_;
    // Pointer-family params (`*mut`/`*const`/`&`/`&mut`) bound as SSA args. Their
    // arg IS a pointer value, so `&p` is the address of the param's own slot →
    // EAddrOf must spill. (Scalars are caught in EAddrOf by an SSA-type check;
    // aggregate by-value params arrive AS a pointer = the object address and are
    // NOT here, so `&p` returns that address unchanged.) Together these unify the
    // EAddrOf `&p` rule into one condition "the SSA arg holds a value".
    std::unordered_set<std::string>               ptr_family_param_;
    mlir::Type                                    cur_ret_type_;
    TypeRef                              cur_fn_ret_logos_type_ = nullptr;
    std::string                                   cur_fn_name_;
    bool                                          in_llvm_func_ = false;
    // Entry block of the function currently being emitted.  All LLVM::AllocaOp
    // instructions must be inserted here (at the top) so LLVM treats them as
    // *static* allocas — otherwise an alloca inside a loop body grows the
    // stack frame on every iteration and eventually overflows.
    mlir::Block*                                  cur_entry_block_ = nullptr;

    struct LoopBlocks {
        mlir::Block*  cont;
        mlir::Block*  exit;
        mlir::Value   break_slot;  // alloca for break-value; null if loop is void
        std::string   label;       // loop label (e.g. "'outer"), empty = unlabeled
    };
    std::vector<LoopBlocks> loop_stack_;

    int str_counter_ = 0;
    int promoted_const_counter_ = 0;   // #92 const promotion
    int writ_lit_counter_ = 0;

    // Memoize EWritLit codegen by content. Identical blob bytes resolve to
    // the same rodata global so multiple accesses to the same const-value
    // expression (e.g. an associated constant) preserve pointer identity at
    // -O0, where LLVM's ConstantMerge is not running. Keyed by the final
    // rodata bytes (size prefix included for the static path), value is the
    // emitted global symbol name.
    std::unordered_map<std::string, std::string> writ_lit_global_cache_;

    // "Trait::Type" → mangled method names in vtable slot order
    std::unordered_map<std::string, std::vector<std::string>> dyn_vtable_methods_;
    // "Trait::Type" → symbol name of the STATIC vtable global (emitted once,
    // `[N x ptr]` of method addresses). A `&dyn` coercion takes its address
    // instead of malloc'ing+filling a fresh vtable per coercion (the recurring
    // per-coercion vtable leak; Rust vtables are static).
    std::unordered_map<std::string, std::string> dyn_vtable_globals_;
    // (vtable global sym → ordered method symbols). build_inline_vtable emits a
    // zero-init `constant [N x ptr]` placeholder global; the real address-of-
    // method initializer is materialised AFTER func→llvm lowering (in
    // lower_and_emit_object, where the methods are `llvm.func` so addressof is
    // valid) → a true `.data.rel.ro`/`.rodata` static vtable. Carried to the
    // pipeline via the `logos.vtable_specs` module attribute set in generate().
    std::vector<std::pair<std::string, std::vector<std::string>>> dyn_vtable_specs_;
    // Trait name → its method names in vtable slot order, and whether the
    // trait has a blanket impl (`impl<T> Trait for T`). Used by
    // build_inline_vtable to synthesize a `<Concrete>__<method>` vtable on the
    // fly when a concrete type reaches `&dyn Trait` only through a blanket
    // (whose impl block registered the typevar target, not each concrete).
    std::unordered_map<std::string, std::vector<std::string>> trait_method_names_;
    std::unordered_set<std::string> blanket_traits_;
    // Trait name → ordered transitive supertraits (LTraitDef.upcast_supertraits,
    // single-sourced by sema). Drives the stored super-vtable-pointer slots that
    // each `dyn Trait` vtable carries after its method slots, and the upcast
    // `&dyn Sub → &dyn Super` index. Empty for a trait with no supertraits (so
    // its vtable layout is unchanged).
    std::unordered_map<std::string, std::vector<std::string>> trait_upcast_supers_;

    // drop_in_place glue: every vtable's slot 0 is a `__drop_in_place__<type>`
    // function that runs the concrete type's FULL drop (Rust-faithful). Maps a
    // vtable type-name key → the emitted glue symbol (dedup; emitted once per
    // concrete type). Empty body for a non-droppable type — harmless no-op.
    std::unordered_map<std::string, std::string> dyn_drop_glue_;
    // Emit (once) the drop_in_place glue fn for concrete type `ty` keyed on
    // `type_name`; returns its symbol (always non-empty so it can fill slot 0).
    std::string emit_drop_in_place_glue(std::string_view type_name, TypeRef ty);

    // Closure env drop glue: per closure-id, a `__closure_drop__<id>(env_ptr)`
    // fn that drops each owned droppable capture (env field i+1) then, if the
    // env is heap-allocated (escaping closure), frees the env. Stored at env
    // field 0 and invoked when an OWNED closure value is dropped. Deduped per
    // closure-id.
    std::unordered_map<std::string, std::string> closure_drop_glue_;
    std::string emit_closure_drop_glue(
        const std::string& closure_id,
        mlir::Type cap_struct,
        const std::vector<std::string>& captures,
        const std::vector<TypeRef>& capture_types,
        // RFC-2229 phase-2: per-capture narrow FIELD type (null = whole-root).
        // Drop-glue drops the FIELD value when set (only the narrow piece the
        // env actually owns), not the root.
        const std::vector<TypeRef>& capture_field_types,
        const std::vector<bool>& capture_drops,
        bool heap_env);

    // ── MLIR helpers ─────────────────────────────────────────────

    static bool is_terminated(mlir::Block* block) noexcept {
        if (!block || block->empty()) return false;
        return block->back().hasTrait<mlir::OpTrait::IsTerminator>();
    }

    mlir::Value i32_zero() {
        return builder_.create<mlir::arith::ConstantIntOp>(loc_, 0, 32);
    }
    mlir::Value i64_one() {
        return builder_.create<mlir::arith::ConstantIntOp>(loc_, 1, 64);
    }
    mlir::LLVM::LLVMPointerType ptr_type() {
        return mlir::LLVM::LLVMPointerType::get(builder_.getContext());
    }

    // Create an LLVM::AllocaOp in the current function's entry block so it
    // is recognised as a static alloca (reused across loop iterations /
    // function calls, never growing the stack dynamically).  Returns the
    // alloca pointer.  The builder's insertion point is restored before
    // returning so the caller can continue emitting at its original site.
    mlir::Value create_entry_alloca(mlir::Type elem_type, int64_t count = 1) {
        if (!cur_entry_block_) {
            // Fallback for callers outside a tracked function body (should
            // not happen in practice; preserve old behaviour just in case).
            auto cnt = builder_.create<mlir::arith::ConstantIntOp>(loc_, count, 64);
            return builder_.create<mlir::LLVM::AllocaOp>(
                loc_, ptr_type(), elem_type, cnt);
        }
        mlir::OpBuilder::InsertionGuard guard(builder_);
        builder_.setInsertionPointToStart(cur_entry_block_);
        auto cnt = builder_.create<mlir::arith::ConstantIntOp>(loc_, count, 64);
        return builder_.create<mlir::LLVM::AllocaOp>(
            loc_, ptr_type(), elem_type, cnt);
    }

    // Spill an aggregate value (struct/enum/array returned by value) to an
    // alloca. Used when passing such a value to a function that expects a
    // pointer.
    // A Call/MethodCall returning a Slice/str now yields the 16-byte {ptr,len}
    // fat pair BY VALUE (slice-return-by-value ABI, A3/A4 leak fix). Every
    // downstream slice consumer (s[i], .len, field stores, arg passing) expects
    // a pointer-to-{ptr,len}. Spill the value back to a stack slot and hand back
    // the slot address so the by-value→by-pointer transition is transparent.
    mlir::Value spill_slice_call_result(mlir::Value v, TypeRef ty) {
        // A by-value 16B fat return (a Slice, or a fat `&mut` zone reference) comes
        // back as an LLVM struct value; spill it to an alloca so the consumer sees
        // the usual ptr-to-{data,meta} pair (repr_data/repr_meta gep). Without this
        // a returned fat ref is a struct value and field access geps it directly.
        if (!v || !ty) return v;
        bool fat_returnable = (ty.kind() == LogosType::Kind::Slice) ||
                              (ref_repr_of(ty) == RefReprKind::FatZoneMut);
        // A tagged/niche enum is RETURNED by value (an aggregate; mlir_gen_fn.cpp),
        // but its value-repr is by-POINTER (logos_to_mlir(Enum) == ptr). Spill the
        // aggregate result to a slot so consumers (method `self`, match scrutinee,
        // field/disc access) see a pointer — otherwise a by-value enum call result
        // is used as if it were a pointer (e.g. `f().method()` → `*(self …)`),
        // emitting `llvm.load(aggregate)`. (This is the niche-enum-byvalue bug.)
        bool enum_returnable = ty.kind() == LogosType::Kind::Enum &&
                               resolve_tagged_enum(std::string(ty.enum_name()), ty) != nullptr;
        if (!fat_returnable && !enum_returnable) return v;
        if (mlir::isa<mlir::LLVM::LLVMStructType>(v.getType()) ||
            mlir::isa<mlir::LLVM::LLVMArrayType>(v.getType()))
            return spill_to_alloca(v);
        return v;
    }

    mlir::Value spill_to_alloca(mlir::Value v) {
        auto t = v.getType();
        if (!mlir::isa<mlir::LLVM::LLVMStructType>(t) &&
            !mlir::isa<mlir::LLVM::LLVMArrayType>(t))
            return v;
        auto alloca = create_entry_alloca(t);
        builder_.create<mlir::LLVM::StoreOp>(loc_, v, alloca);
        return alloca;
    }

    mlir::Value coerce_int(mlir::Value v, mlir::Type to,
                           TypeRef src_lt = nullptr) {
        if (!v || !to || v.getType() == to) return v;
        auto fi = mlir::dyn_cast<mlir::IntegerType>(v.getType());
        auto ti = mlir::dyn_cast<mlir::IntegerType>(to);
        if (!fi || !ti) return v;
        if (ti.getWidth() > fi.getWidth()) {
            // Pick zero vs sign extend by *source* signedness when known.
            // Bool (i1) is always zero-extended.  Without src_lt, fall back
            // to sign-extend to preserve legacy behavior for signed sources.
            bool src_unsigned = fi.getWidth() == 1 ||
                (src_lt &&
                 LogosType::is_unsigned_repr_kind(TypeRef(src_lt).kind()));
            if (src_unsigned)
                return builder_.create<mlir::arith::ExtUIOp>(loc_, to, v);
            return builder_.create<mlir::arith::ExtSIOp>(loc_, to, v);
        }
        if (ti.getWidth() < fi.getWidth())
            return builder_.create<mlir::arith::TruncIOp>(loc_, to, v);
        return v;
    }

    mlir::Value coerce_float(mlir::Value v, mlir::Type to) {
        if (!v || !to || v.getType() == to) return v;
        auto fv = mlir::dyn_cast<mlir::FloatType>(v.getType());
        auto ft = mlir::dyn_cast<mlir::FloatType>(to);
        if (!fv || !ft) return v;
        if (ft.getWidth() < fv.getWidth())
            return builder_.create<mlir::arith::TruncFOp>(loc_, to, v);
        return builder_.create<mlir::arith::ExtFOp>(loc_, to, v);
    }

    // Coerce any numeric value: int→int, float→float, int→float.
    // Does NOT handle float→int (that requires an explicit cast).
    // src_lt: Logos source type — required for correct signed/unsigned int→float conversion.
    mlir::Value coerce_numeric(mlir::Value v, mlir::Type to,
                               TypeRef src_lt = nullptr) {
        if (!v || !to || v.getType() == to) return v;
        // int → int
        if (mlir::isa<mlir::IntegerType>(v.getType()) && mlir::isa<mlir::IntegerType>(to))
            return coerce_int(v, to, src_lt);
        // float → float (truncate or extend)
        if (mlir::isa<mlir::FloatType>(v.getType()) && mlir::isa<mlir::FloatType>(to))
            return coerce_float(v, to);
        // int → float: use unsigned op for unsigned Logos types.
        //
        // ⚠ THE SAME QUESTION IS DECIDED IN `gen_expr_kind(ECastView, …)`
        // (`src/compiler/mlir_gen_expr.cpp`) AND THE TWO SITES MUST AGREE. Both
        // now ask ONE thing — `is_unsigned_repr_kind` on the SOURCE Logos type,
        // which names `Kind::Bool` so that `uitofp(i1 1) = 1.0` rather than
        // `sitofp(i1 1) = -1.0`. Do NOT "harden" this by adding an
        // `v.getType() == i1` disjunct: that clause stood at the ECast site,
        // was measured mutually redundant with the kind test, and was deleted
        // there on 2026-08-10 — the long comment at that site carries the
        // counts. Two clauses that imply each other are one guard plus a trap.
        //
        // The one thing this site has that the ECast site does not is a NULL
        // source type: the same instrumented run measured 6 calls here with
        // `src_lt == nullptr` (all from `MLIRGenImpl::gen_arr_lit`, bare int
        // literals in `let buf7: [f32; 3] = [1, 2, 3];`,
        // `tests/logos/pass/writ_as_array_variants.logos`) against 0 at the
        // ECast site. Every one of the six was a non-i1 value, and an i1 cannot
        // arrive here at all: sema refuses every implicit bool→float shape —
        // `[f64; 2] = [true, false]`, `S { f: true }` with `f: f64`,
        // `let a: f64 = if c { true } else { false }` and passing a `bool` to an
        // `f64` parameter were each measured refused. A bool reaches a float
        // only through `as`, i.e. through the ECast site, where its type is
        // present.
        if (mlir::isa<mlir::IntegerType>(v.getType()) && mlir::isa<mlir::FloatType>(to)) {
            bool src_unsigned = src_lt &&
                LogosType::is_unsigned_repr_kind(TypeRef(src_lt).kind());
            if (src_unsigned)
                return builder_.create<mlir::arith::UIToFPOp>(loc_, to, v);
            return builder_.create<mlir::arith::SIToFPOp>(loc_, to, v);
        }
        return v;
    }

    // ── Type conversion ──────────────────────────────────────────
    mlir::Type logos_to_mlir(TypeRef tv);

    // LLVM type used in fn-return position. Differs from logos_to_mlir
    // only for aggregate-by-value returns (Struct/ZonedStruct/Enum):
    // logos_to_mlir returns ptr_type for these (the "passed by ptr"
    // shorthand used at param/field/scope positions), but the actual
    // fn-def returns the literal LLVM struct value. Indirect calls
    // and closure-fn synthesis must use this struct type for the
    // return slot, otherwise the call gets typed `() -> ptr` while
    // the callee writes the full aggregate — silent corruption that
    // segfaults the next match on the result. See
    // [[baghunt-dyn-in-enum-payload]] for the originating fix.
    mlir::Type llvm_fn_ret_type(TypeRef ret_t) {
        if (!ret_t) return mlir::Type{};
        TypeRef rt{ret_t};
        if (rt.kind() == LogosType::Kind::Struct ||
            rt.kind() == LogosType::Kind::ZonedStruct) {
            auto sit = struct_types_.find(mlir_struct_key(rt));
            if (sit == struct_types_.end())
                sit = struct_types_.find(std::string(rt.struct_name()));
            if (sit != struct_types_.end()) return sit->second.llvm_type;
        }
        if (rt.kind() == LogosType::Kind::Enum) {
            if (auto* te = resolve_tagged_enum(std::string(rt.enum_name()), rt))
                return te->llvm_type;
        }
        // Trait-object value-fat-pair: return the 16-byte {data,vtable} pair BY
        // VALUE (mirrors how we'd return a slice's {ptr,len}). Without this a
        // `-> &dyn T` returned a single ptr and the callee had to malloc a
        // surviving fat slot (a leak). Covers bare `dyn`, `&dyn`/`&mut dyn`,
        // `*const dyn`/`*mut dyn`.
        // Only a BARE `dyn`/`&dyn`/`&mut dyn` (sema flattens these to a single
        // TraitObject node) returns by-value as the 16-byte fat pair. A
        // `Ref/MutRef<TraitObject>` (i.e. `&T` where T is itself `&dyn`, as in
        // `Vec<&dyn>::index -> &T`) is a genuine POINTER into storage — keep it
        // thin. Raw `*const/*mut dyn` likewise stays a thin handle.
        if (rt.kind() == LogosType::Kind::TraitObject)
            return dyn_llvm_type();
        // Slice/str fat-pair: return the 16-byte {ptr,len} BY VALUE (mirrors the
        // TraitObject fat-pair above). logos_to_mlir(Slice)=ptr, which forced
        // gen_return to malloc(16)+memcpy a surviving heap slot (a leak, A3/A4).
        // `str` IS Slice<u8> so it gets the same treatment. The caller spills the
        // returned value back to a stack slot (slices are consumed by-pointer).
        if (rt.kind() == LogosType::Kind::Slice)
            return slice_llvm_type();
        return logos_to_mlir(ret_t);
    }

    // ONE resolution of an enum DECLARATION from a TypeRef — bare name first,
    // then the mono instance name, exactly the two steps `resolve_tagged_enum`
    // takes. It has to be both: after mono the generic TEMPLATE is gone and only
    // `GT__i32` is in `enum_types_`, while the TypeRef still spells the base
    // `GT` with its args. A bare-name-only lookup therefore MISSED for every
    // generic enum and fell back to the default i32 discriminant.
    //
    // MEASURED before this existed, with `enum GT<T> : u64 { A, B }`:
    // `sizeof::<GT<i32>>()` = 4 for an eight-byte type, and in
    // `struct Holder<T> { h: GT<T>, k: u8, v: i64 }` the GEP wrote `k` at byte
    // 4 while `offset_of!` — sema, which resolves the TEMPLATE and still saw
    // the backing type — claimed 8. Two numbers for one field, and a `malloc`
    // sized off `sizeof` short by four bytes.
    const lir_view::EnumView* find_enum_decl(std::string_view name, TypeRef type);

    // MLIR type for a C-style enum's discriminant.  Uses the enum's
    // explicit backing type if declared (`enum Foo : u64 {}`), else i32.
    // `type` is the TypeRef the name came from, so a GENERIC enum resolves to
    // its instance; passing none is the non-generic case.
    mlir::Type enum_disc_mlir(const std::string& enum_name,
                              TypeRef type = TypeRef(nullptr)) {
        if (auto* ev = find_enum_decl(enum_name, type))
            if (auto bt = ev->backing_type(pool_impl()))
                if (auto t = logos_to_mlir(bt)) return t;
        return builder_.getI32Type();
    }
    unsigned enum_disc_bits(const std::string& enum_name,
                            TypeRef type = TypeRef(nullptr)) {
        auto t = enum_disc_mlir(enum_name, type);
        if (auto it = mlir::dyn_cast<mlir::IntegerType>(t)) return it.getWidth();
        return 32;
    }

    // ── Struct / enum / class registration ──────────────────────
    // MLIR struct keys carry the package prefix so same-named structs in
    // different packages don't alias at the LLVM struct-type level. The bare
    // `concrete_struct_name(t)` is reused for method-symbol mangling (which
    // is package-agnostic — see mono.cpp's "<Struct>__<method>" pattern) and
    // as a back-compat alias key in struct_types_.
    static std::string qualify_pkg(std::string_view pkg, std::string_view name) {
        // ONE spelling of a type's key, shared with the ledger the early
        // engines record into (layout_law.hpp) — a key that differs by a
        // separator would make every cross-engine entry silently "unmatched".
        return logos::compiler::layout::type_key(pkg, name);
    }
    static std::string_view strip_struct_pkg(std::string_view qualified) {
        // Inverse of qualify_pkg: split at the last '.'. Struct base names
        // never contain '.' in their mangled form, so this is unambiguous.
        auto p = qualified.rfind('.');
        if (p == std::string_view::npos) return qualified;
        return qualified.substr(p + 1);
    }
    std::string mlir_struct_key(TypeRef t) {
        if (!t) return {};
        auto base = concrete_struct_name(t);
        return qualify_pkg(t.pkg_name(), base);
    }
    // Resolve `struct_types_` for a struct/ZonedStruct TypeRef, PKG-QUALIFIED
    // first (mlir_struct_key), bare name only as a fallback. The bare-name slot
    // is a "first-registered wins" back-compat alias (register_struct), so a
    // bare `concrete_struct_name(t)` lookup silently ALIASES two same-named
    // structs from different packages onto whichever registered first — wrong
    // LLVM aggregate ⇒ wrong element stride / field layout (e.g. a user
    // `struct Item` vs an imported `logos.std.compiler.metaprog.Item`). Any site
    // that has the TypeRef (carrying pkg) in hand and needs the struct's LLVM
    // type for LAYOUT/STRIDE must route through this, not bare-name lookup.
    std::unordered_map<std::string, StructInfo>::iterator
    find_struct_it(TypeRef t) {
        if (!t) return struct_types_.end();
        auto it = struct_types_.find(mlir_struct_key(t));
        if (it != struct_types_.end()) return it;
        return struct_types_.find(concrete_struct_name(t));
    }
    // Same pkg-qualified-first resolution for `all_struct_defs_` (the def→layout
    // registry that drives layout_of / size / dereferenceable / loop-var memcpy).
    // Its bare-name slot is the same "first-registered wins" alias, so a bare
    // lookup mis-sizes a user struct that shares a name with an imported one.
    std::unordered_map<std::string, lir_view::StructView>::iterator
    find_struct_def_it(TypeRef t) {
        if (!t) return all_struct_defs_.end();
        auto it = all_struct_defs_.find(mlir_struct_key(t));
        if (it != all_struct_defs_.end()) return it;
        return all_struct_defs_.find(concrete_struct_name(t));
    }
    // #60 — struct-PATTERN identity. `PatStructView::struct_name()` is a BARE
    // spelling: it carries neither the package nor the `$M<fp>` ambiguous-name
    // fold, so `struct_types_.find(ps.struct_name())` lands in the
    // first-registered-wins bare alias slot installed by register_struct — a
    // user `struct ExprBlob` then binds the IMPORTED homonym's field layout
    // (measured: `match x { ExprBlob{a,b} => .. }` printed empty bindings;
    // fixtures bc_odr_pat_*). The SCRUTINEE TypeRef carries pkg + fold, so
    // narrow it to the struct the pattern names and let find_struct_it /
    // find_struct_def_it resolve QUALIFIED-FIRST. Returns a null TypeRef when
    // the scrutinee is not (a ref/ptr chain to) a struct of exactly that bare
    // name — enum-variant payload patterns, tuple scrutinees, missing type
    // info — and the caller keeps the bare lookup as the LAST resort. That is
    // sema's recorded find_struct_repr_ order (⚠ QUALIFIED KEY FIRST, BARE SLOT
    // LAST): a program declaring its own S still cannot alias a foreign S, and
    // no site that resolved before becomes a miss.
    static TypeRef pat_struct_ty(TypeRef scrut_ty, std::string_view bare) {
        TypeRef t = scrut_ty;
        for (int i = 0; i < 8 && t; ++i) {
            auto k = TypeRef(t).kind();
            if (k == LogosType::Kind::Ref || k == LogosType::Kind::MutRef ||
                k == LogosType::Kind::Ptr) { t = TypeRef(t).pointee(); continue; }
            break;
        }
        if (!t) return TypeRef{};
        auto k = TypeRef(t).kind();
        if (k != LogosType::Kind::Struct && k != LogosType::Kind::ZonedStruct)
            return TypeRef{};
        if (TypeRef(t).struct_name() != bare) return TypeRef{};
        return t;
    }
    // Module system (symbol-mangle rewrite, emission boundary): qualified LINK
    // symbol of a def (methods gain `<module>..`; free fns unchanged).
    std::string link_name(lir_view::FunctionView fn) const {
        if (!prog_) return std::string(fn.name());
        return sym::link_name(fn, prog_->pkg_module_ids);
    }
    // Qualify a (bare-module) method-CALL callee STRING to its exact link symbol
    // (full signature preserved) — used by the canonical() bridge before its
    // signature-stripping (ambiguous) fallback. Method shape: part before the
    // first `__` carries a `.`; free fns use `$` / already `..` → unchanged.
    // THE callee-resolution chokepoint (defined in mlir_gen_expr.cpp). Resolves
    // a callee symbol to its FuncOp across the bare↔module-qualified and
    // sig-stripped forms the LIR/mono/bridge produce.
    mlir::func::FuncOp find_func_op(mlir::ModuleOp mod,
                                    std::string_view name) const;
    // Memoised SUCCESSFUL resolutions (callee string → FuncOp). FuncOp defs are
    // stable once created (the bridge renames call sites, never defs), so caching
    // hits is sound. Amortises the per-call bare-miss→link_name_str→qualified-hit
    // work for popular callees (push/get/deref) on codegen-heavy bodies. Misses
    // are NOT cached (a name may resolve once a later forward-decl is emitted).
    mutable std::unordered_map<std::string, mlir::func::FuncOp> find_func_op_cache_;

    // find_func_op's canonical-match index (see find_func_op). Maps each def's
    // canonical key → its FuncOp; ambiguous keys (shared by >1 def) live in the
    // set and resolve to nothing. Rebuilt lazily when the module's FuncOp count
    // changes (stable during body-gen, so built once). Replaces a per-call O(n)
    // canonicalising walk.
    mutable std::unordered_map<std::string, mlir::func::FuncOp> ffo_canon_index_;
    mutable std::unordered_set<std::string> ffo_canon_ambig_;
    // Direct symbol-name → FuncOp index, built in the SAME dirty-gated pass as
    // the canonical index. Replaces find_func_op's mod.lookupSymbol(name) calls
    // — MLIR's lookupSymbolIn is an O(funcs) LINEAR SCAN reading each sym_name
    // (getInherentAttr). This makes both the direct and qualified lookups O(1).
    mutable std::unordered_map<std::string, mlir::func::FuncOp> ffo_symtab_;
    // base (name up to "__f__"/"__g__") → FIRST FuncOp with that base. Drives
    // walk_prefix (the overload-ambiguous method fallback) in O(1) instead of an
    // O(funcs) find_fn_matching prefix scan. First-wins (matches find_fn_matching
    // returning the first module-order hit); built in the same dirty-gated pass.
    mutable std::unordered_map<std::string, mlir::func::FuncOp> ffo_base_first_;
    // Set true whenever a func::FuncOp is added to the module (mark_funcs_dirty
    // at every create site). ensure_ffo_canon_index rebuilds only when dirty —
    // replacing the per-call O(funcs) staleness recount. A stale-by-miss index
    // is self-correcting via L4: a canonical-fallback callee would fail to
    // resolve, breaking a cross-module test.
    // ⚠ THE CANONICAL FALLBACK IS PACKAGE-BLIND (see find_func_op). It maps a
    // callee to a def by a key that ffo_canonical STRIPS the package off, so
    // `test.Ident__eq` bound
    // `logos_mem..logos.std.compiler.metaprog.Ident__eq__f__ref_Ident__ref_Ident`
    // — a foreign package's method, handed two `%test.Ident`s. `ffo_canon_pkg_`
    // records the package of whichever def each canonical key resolved to, so
    // that bind can be REFUSED when the callee names a package that declares
    // its own struct of that name. Same key set as ffo_canon_index_.
    mutable std::unordered_map<std::string, std::string> ffo_canon_pkg_;
    // Does `pkg` declare a struct that OWNS this `<Owner>__<method>…` symbol?
    // ⚠ ANCHORED ON A CARRIED PART, NOT A `__` SPLIT: the owner is not guessed
    // by cutting at the first `__` (legal inside an identifier — the separator
    // class), it is each candidate struct's own NAME, recomposed with `__` and
    // compared as a prefix. Index: pkg → the struct names it declares.
    mutable std::unordered_map<std::string, std::vector<std::string>> pkg_struct_names_;
    mutable bool pkg_struct_names_built_ = false;
    bool pkg_owns_symbol_owner(std::string_view pkg, std::string_view sym) const {
        if (!prog_ || pkg.empty() || sym.empty()) return false;
        if (!pkg_struct_names_built_) {
            for (auto& sd : prog_->structs)
                pkg_struct_names_[std::string(sd.pkg())].emplace_back(sd.name());
            pkg_struct_names_built_ = true;
        }
        auto it = pkg_struct_names_.find(std::string(pkg));
        if (it == pkg_struct_names_.end()) return false;
        for (auto& nm : it->second) {
            if (nm.empty() || sym.size() <= nm.size() + 2) continue;
            if (sym.compare(0, nm.size(), nm) != 0) continue;
            if (sym.compare(nm.size(), 2, "__") == 0) return true;
        }
        return false;
    }
    mutable bool ffo_canon_dirty_ = true;
    void mark_funcs_dirty() const noexcept { ffo_canon_dirty_ = true; }
    void ensure_ffo_canon_index(mlir::ModuleOp mod) const;

    std::string link_name_str(const std::string& callee) const {
        if (!prog_ || callee.find("..") != std::string::npos) return callee;
        // ⚠ SEPARATOR CLASS. The package head used to be "the last `.` BEFORE
        // the first `__`" — but `__` is legal inside a package SEGMENT
        // (`pkg.a__b.w__m`), and that cut then names the package `pkg` and
        // prefixes the wrong module id. `pkg_module_ids` IS the registry of
        // declared packages: take the LONGEST dotted prefix it knows.
        auto known = [&](std::string_view p) {
            return !prog_->pkg_module_ids.get_str(std::string(p)).empty();
        };
        auto pkg_sv = mname::package_prefix(callee, known);
        if (!pkg_sv) return callee;
        std::string_view mid = prog_->pkg_module_ids.get_str(std::string(*pkg_sv));
        if (mid.empty()) return callee;
        return std::string(mid) + ".." + callee;
    }
    // AUDIT INSTRUMENT for the retired "first `let` is spelled `__…` ⇒ the
    // block is sema-synthesized" heuristic — the way to find a NEW SBlock
    // producer that forgot to state stmt_keys::TRANSPARENT. Off unless
    // LOGOS_TRANSPARENT_AUDIT=1; it is NOT a gate (it fires on the legal user
    // program `{ let __x = …; … }`, which is exactly the case the flag exists
    // to get right). MEASURED 2026-08-02 over all 5554 registered pass
    // programs: ZERO disagreements. See gen_stmt_kind(SBlockView).
    const bool transparent_audit_ = [] {
        const char* e = std::getenv("LOGOS_TRANSPARENT_AUDIT");
        return e && e[0] && e[0] != '0';
    }();
    // Name a failing sub-expression in an R2 report: for a call, WHICH callee
    // — the difference between a compiler-bug report someone can act on and a
    // shrug. (assertion-as-diagnostic class.)  ONE home, so the `let`, the
    // expression-statement and the `return` guards all report alike.
    static std::string describe_expr(lir_view::ExprRef er) {
        if (!er) return " (no expression)";
        auto ek = er.kind();
        if (ek == lir_schema::expr::Code::Call)
            return std::format(" (call to '{}')",
                               std::string(lir_view::ECallView{er}.callee()));
        if (ek == lir_schema::expr::Code::MethodCall)
            return std::format(" (method call '{}' resolved to '{}')",
                               std::string(lir_view::EMethodCallView{er}.method()),
                               std::string(lir_view::EMethodCallView{er}.resolved_symbol()));
        return std::format(" (expr kind {})", static_cast<int>(ek));
    }
    // ── R2 sink state (see bug_count() above) ───────────────────────────
    static constexpr size_t kBugLogMax = 64;
    // `mutable` because several lowering helpers that can only report a
    // malfunction (logos_to_mlir, link resolution) are const members. The
    // channel is a property of the COMPILE, not of the object's logical state.
    mutable size_t                   bugs_ = 0;
    mutable std::vector<std::string> bug_log_;
    void bug_raw(std::string msg) const {
        ++bugs_;
        std::fprintf(stderr, "mlir_gen: internal: %s\n", msg.c_str());
        if (bug_log_.size() < kBugLogMax) bug_log_.push_back(std::move(msg));
        // OPT-IN debugging aid for a stack trace. The CHECK is never opt-in.
        if (const char* e = std::getenv("LOGOS_MLIRGEN_ABORT_ON_BUG");
            e && e[0] && e[0] != '0')
            std::abort();
    }
    template <class... A>
    void bug(std::format_string<A...> f, A&&... a) const {
        bug_raw(std::format(f, std::forward<A>(a)...));
    }
    template <class... A>
    std::nullptr_t bug_null(std::format_string<A...> f, A&&... a) const {
        bug_raw(std::format(f, std::forward<A>(a)...));
        return nullptr;
    }
    template <class... A>
    bool bug_false(std::format_string<A...> f, A&&... a) const {
        bug_raw(std::format(f, std::forward<A>(a)...));
        return false;
    }
    template <class... A>
    mlir::Value bug_value(std::format_string<A...> f, A&&... a) const {
        bug_raw(std::format(f, std::forward<A>(a)...));
        return {};
    }
    // ── #103: THE PRINTF ADAPTER ONTO THE SAME SINK ─────────────────────
    // Not a second channel: it formats and calls `bug_raw`, so a message sent
    // here is counted, logged, `internal:`-marked and enforced exactly like one
    // sent through `bug()`. It exists because the 40 raw
    // `std::fprintf(stderr, "mlir_gen: …")` malfunction reports this round
    // converts were written in printf spelling with printf arguments
    // (`%.*s` + int/ptr pairs, `.c_str()` everywhere), and hand-translating 40
    // format strings to `std::format` is 40 chances to change a message, swap
    // two arguments or lose a specifier — in a round whose whole subject is
    // that this channel cannot be trusted. The message text is preserved
    // byte-for-byte; only the prefix and the trailing newline move to the sink.
    __attribute__((format(printf, 2, 3)))
    void bug_printf(const char* fmt, ...) const {
        va_list ap; va_start(ap, fmt);
        va_list ap2; va_copy(ap2, ap);
        int n = std::vsnprintf(nullptr, 0, fmt, ap);
        va_end(ap);
        std::string msg;
        if (n > 0) { msg.resize(size_t(n)); std::vsnprintf(msg.data(), size_t(n) + 1, fmt, ap2); }
        else       { msg = fmt; }
        va_end(ap2);
        bug_raw(std::move(msg));
    }
    __attribute__((format(printf, 2, 3)))
    std::nullptr_t bug_printf_null(const char* fmt, ...) const {
        va_list ap; va_start(ap, fmt);
        va_list ap2; va_copy(ap2, ap);
        int n = std::vsnprintf(nullptr, 0, fmt, ap);
        va_end(ap);
        std::string msg;
        if (n > 0) { msg.resize(size_t(n)); std::vsnprintf(msg.data(), size_t(n) + 1, fmt, ap2); }
        else       { msg = fmt; }
        va_end(ap2);
        bug_raw(std::move(msg));
        return nullptr;
    }
    // ── #103: REGISTRATION FAILURE IS ROUND-AWARE ───────────────────────
    // `register_struct` runs in every metaprog fixpoint round as well as in the
    // final gen. A metaprog round's program is a SNAPSHOT the metaprog has not
    // finished emitting into (#61), and mlir_gen.cpp's caller already DEFERS a
    // failed registration in that case rather than refusing — so the message
    // printed from inside register_struct was describing a decision the caller
    // had already declined to make. MEASURED 2026-08-22 over the four fixtures
    // that emitted this shape (ctr_leaf_family_spelling,
    // ctr_vec_leaf_family_spelling, typeof_container_hand_written_state,
    // typeof_container_field_admit): the FINAL gen — the one whose module
    // becomes the object file — asks the same question over the same types and
    // answers CLEAN, 0 lines, in every one of them. The recoverability is
    // therefore established by re-running the identical check at the point the
    // object is built, not by "the tests still pass".
    // The metaprog-round line is NOT silenced and NOT env-gated: it is printed
    // every time, spelled `warning:` and naming the round, so the channel gets
    // LOUDER here, not quieter. The final gen routes to the R2 sink and fails.
    template <class... A>
    bool struct_reg_fail(std::format_string<A...> f, A&&... a) const {
        if (metaprog_round_) {
            std::fprintf(stderr,
                "mlir_gen: warning: metaprog round — deferred, not refused: %s\n",
                std::format(f, std::forward<A>(a)...).c_str());
            return false;
        }
        return bug_false(f, std::forward<A>(a)...);
    }

    // Is the program being compiled RIGHT NOW listed in the R2 ledger?
    //
    // `LOGOS_MLIRGEN_BUG_LEDGER` names the ledger FILE; it does not grant an
    // excuse. That distinction is the whole point: a boolean env var is a
    // global off-switch, and it also forces the excused set to be hand-listed
    // wherever the variable is set — a second copy of a population that already
    // exists as an artifact. Here the artifact decides, so exporting the
    // variable buys nothing for a program the ledger does not name.
    //
    // Ledger row: `<test-base> <count> <repo-relative path, no .logos>`; `#`
    // starts a comment. Only column 3 is read — the count is re-measured by
    // logos_00_mlir_gen_bug_ledger, which is where a count belongs.
    bool bug_ledgered_here() const {
        const char* lp = std::getenv("LOGOS_MLIRGEN_BUG_LEDGER");
        if (!lp || !lp[0] || main_source_.empty()) return false;
        std::FILE* f = std::fopen(lp, "r");
        if (!f) {
            // A named-but-unreadable ledger is a broken invocation, not a
            // licence: say so and fail closed.
            std::fprintf(stderr,
                "mlir_gen: LOGOS_MLIRGEN_BUG_LEDGER='%s' cannot be opened —"
                " the exclusion is REFUSED\n", lp);
            return false;
        }
        bool hit = false;
        char line[1024];
        while (!hit && std::fgets(line, sizeof line, f)) {
            std::string_view sv(line);
            if (auto h = sv.find('#'); h != std::string_view::npos) sv = sv.substr(0, h);
            // third whitespace-separated column
            size_t col = 0, i = 0;
            std::string_view path;
            while (i < sv.size()) {
                while (i < sv.size() && std::isspace(static_cast<unsigned char>(sv[i]))) ++i;
                size_t b = i;
                while (i < sv.size() && !std::isspace(static_cast<unsigned char>(sv[i]))) ++i;
                if (i > b && ++col == 3) { path = sv.substr(b, i - b); break; }
            }
            if (path.empty()) continue;
            // The ledger stores a repo-relative path with no extension; the
            // compiler is handed whatever the caller typed. Suffix-match on
            // "<path>.logos" so both an absolute and a relative invocation
            // resolve, and a shorter path can never match a longer one by
            // accident (the '/' before the match is required).
            std::string want = std::string(path) + ".logos";
            if (main_source_.size() >= want.size() &&
                main_source_.compare(main_source_.size() - want.size(), want.size(), want) == 0 &&
                (main_source_.size() == want.size() ||
                 main_source_[main_source_.size() - want.size() - 1] == '/'))
                hit = true;
        }
        std::fclose(f);
        return hit;
    }

    bool register_struct(lir_view::StructView sd);
    void register_tagged_enum(lir_view::EnumView ed);
    uint64_t variant_payload_bytes(lir_view::EnumVariantView v);

    // ── Unified in-memory layout ─────────────────────────────────────────────
    // THE single source of truth for the {size, alignment} of any Logos type,
    // matching the non-packed LLVM aggregate layout codegen emits. sizeof /
    // alignof / enum payload bytes / variant footprint / DST field offsets /
    // inline-copy strides all DERIVE from this — add a type kind to the one
    // switch and every size/align query follows.
public:
    // ONE {size, align} type across the engines — `layout::L` — so a layout
    // crosses an engine boundary without a conversion that could reorder or
    // drop a field. The composition rules that build it live in layout_law.hpp.
    using Layout = logos::compiler::layout::L;
private:
    // TRUE iff the type still carries something mono refuses to instantiate
    // (TypeVar / Error / AssocType / ConstVar / …). Such a type has no instance
    // by construction, so "no definition registered" means something different
    // about it than it does about a concrete one — see `layout_of`'s Struct case.
    bool type_has_unresolved_residue(TypeRef t, int depth = 0);
    bool type_mentions_never(TypeRef t, int depth = 0);
    // True while lowering a body whose own signature carries unresolved
    // residue (TypeVar / Error / AssocType) — see gen_fn_body. Template
    // residue, not an instance; the R2 silent-drop guards do not apply.
    bool cur_fn_has_residue_ = false;
    Layout layout_of(TypeRef t, std::unordered_set<std::string>& seen);
    Layout layout_of(TypeRef t) { std::unordered_set<std::string> s; return layout_of(t, s); }
    // `layout_of`'s Struct case, keyed by the DEFINITION instead of a TypeRef —
    // so verify_layout_engines can ask the TypeRef engine about a registered
    // struct without synthesising a TypeRef for it.
    Layout struct_def_layout(lir_view::StructView sv,
                             std::unordered_set<std::string>& seen);
    // `layout_of`'s Enum case, split out for the same reason — and so the ONE
    // decision (`layout::enum_layout`) has ONE caller in this engine.
    Layout enum_def_layout(const std::string& ename, TypeRef t);
    // ⚠ WHEN `layout_of` MAY WRITE ITS ANSWER TO THE CROSS-ENGINE LEDGER.
    //
    // sema and mono have FINISHED when they answer, so every answer they give
    // is an answer the compiler acted on. `layout_of` is different: it is asked
    // DURING registration, and Pass 0 registers structs before enums on
    // purpose (`register_tagged_enum` needs the struct table), so a struct field
    // of type `Option<i64>` is sized while `tagged_enums_` is still empty and
    // gets the C-like fallback. That answer is discarded — the enum bodies are
    // set afterwards and the struct bodies rebuilt — but it is still an ANSWER,
    // and a ledger that keeps the first one per key would compare a number the
    // compiler never used and report a disagreement about nothing.
    //
    // So this engine's row is measured at ONE defined moment: inside
    // `verify_layout_engines`, when every registry is complete — which is the
    // same moment, and the same call, the A-vs-C arm has always used.
    bool layout_ledger_open_ = false;
    // Does this struct carry an `[T]` / `dyn` tail at any depth? Such a type has
    // no static size, so `layout_of` (the sized PREFIX) and the LLVM stand-in
    // aggregate answer different questions and are not comparable.
    bool struct_is_unsized(lir_view::StructView sv,
                           std::unordered_set<std::string>& seen);
    // THE ENGINE-AGREEMENT GATE. Compares, for every registered struct type,
    // the TypeRef engine (layout_of), the MLIR-type engine (mlir_abi_size) and
    // `llvm::DataLayout` on the mirrored llvm::Type — the layout the object is
    // actually emitted with. A disagreement is a hard compile error naming the
    // type and both answers; it is not allowed to become a wrong program. Runs
    // on every compile. Set `LOGOS_VERIFY_LAYOUT=1` to print what it observed.
    void verify_layout_engines();
    // True iff T is "Freeze" (Rust): NO interior mutability in its own
    // transitively-inline bytes — no `logos.lang.cell.UnsafeCell` reachable
    // through fields/payload/elements WITHOUT crossing a pointer/reference.
    // Pointers STOP the recursion (a `&UnsafeCell`/`*UnsafeCell` field keeps the
    // container Freeze; that is why Arc/Rc stay Freeze). CONSERVATIVE: unknown /
    // unresolvable types return FALSE (not-Freeze) so we never wrongly emit
    // readonly/noalias on `&T`. Sound basis for shared-ref attributes (Slice C).
    bool type_is_freeze(TypeRef t, std::unordered_set<std::string>& seen);
    bool type_is_freeze(TypeRef t) { std::unordered_set<std::string> s; return type_is_freeze(t, s); }
    // {size,align} of a type as an AGGREGATE MEMBER (struct field / tuple
    // element / enum payload field). Mirrors register_struct / tuple_llvm_type /
    // variant_payload_struct: Slice/Closure/Tuple members are stored as an
    // 8-byte POINTER (logos_to_mlir → ptr), NOT their by-value footprint;
    // struct/enum/array/bare-dyn members are inline (layout_of); AnyVal is i32.
    Layout aggregate_member_layout(TypeRef m, std::unordered_set<std::string>& seen);
    // {size, align} of one variant's payload (struct/tuple of its fields) —
    // both the enum's payload_bytes and payload_align derive from this.
    Layout variant_payload_layout(lir_view::EnumVariantView v);
    // Describe ONE enum-variant payload type for `layout::classify_niche`.
    // The engine answers only what is representation-specific (is this a
    // `#[non_null]` 8-byte wrapper? what does its pointee align to?); the
    // eligibility DECISION is the shared law's.
    logos::compiler::layout::ArmDesc niche_arm_desc(TypeRef t);

    // ── RefRepr — reference-representation registry (Phase 0 scaffold) ────────
    // Consolidates the ~50 per-kind switches that hardcode how a reference-like
    // type is laid out (storage) and manipulated (compute). Phase 0 reproduces
    // CURRENT behavior and is NOT YET ROUTED into the codegen sites (dead code);
    // later phases migrate the sites to dispatch through these descriptors.
    // See docs/internals/ref-repr-design.md.
    enum class RefReprKind {
        NotARef,        // not a reference-like type
        ThinPtr,        // *T / &T / &mut T / fn-ptr — 8B thin pointer
        FatSlice,       // &[T] (Slice) — {data,len} 16B
        FatDyn,         // &dyn / TraitObject — {data,vtable} 16B
        FatClosure,     // closure — {fn,env} 16B
        FatCustomDst,   // &CustomDst (DstRef) — {data,meta} 16B
        FatZoneMut,     // &mut T for a #[zone_mut] type — {data, zone=*mut Allocator}
                        // 16B, returned by value (like FatSlice). The zone (allocator)
                        // rides the &mut so grow methods reach it from &mut self.
        RelOffset,      // self-relative pointer — storage = i64 byte offset from
                        // the slot's own address; compute = absolute thin ptr.
                        // materialize = slot + load_i64(slot); lower = store(slot,
                        // target − slot). The writ / #[rel_ptr] zoned pointer.
    };
    // Classify a reference-like TypeRef into its repr kind (NotARef otherwise).
    RefReprKind ref_repr_of(TypeRef t);
    // The EFFECTIVE repr of a struct field: like ref_repr_of(field_type), but a
    // thin pointer field inside a #[zoned2] struct stores SELF-RELATIVE (RelOffset)
    // — the untagged zoned-reference case (ref-repr-design §6). `owner_key` is the
    // field's containing-struct key (all_struct_defs_ key).
    RefReprKind field_repr(const std::string& owner_key, TypeRef field_type);

    // Recover the tail length of a #[self_describing] DST from its THIN header
    // pointer by calling the struct's `dst_len(*const Self)` with `thin_ptr`.
    // The in-band metadata of a thin self_describing DstRef (whose physical value
    // IS the header pointer); the thin counterpart of repr_meta. Returns an i64.
    // `dstref_t` is the DstRef type (carries the struct name). ref-repr §6.
    mlir::Value emit_dst_len(mlir::Value thin_ptr, TypeRef dstref_t);

    // `ref v` / `ref mut v` pattern-binding classifier — single source for the
    // (formerly three) enum-payload binding loops. Given the SEMA-assigned
    // binding type (payload wrapped in N Ref layers) and the payload's own
    // type, returns true when the binding should bind the slot ADDRESS (a
    // borrow), and sets `added_depth` = indirection layers ADDED by `ref` on
    // top of the payload's own THIN-ref layers. Fat-ref payloads
    // (`&dyn`/`&[T]`/`&str`/closure) return false so their dedicated inline-
    // fat handlers own the 16-byte layout. See mlir_gen_stmt.cpp.
    bool ref_bind_kind(TypeRef binding_type, TypeRef payload_type,
                       int& added_depth);
    // Compute representation (the SSA value type). Today uniformly a thin pointer
    // (the fat pair lives in storage; the value is a pointer to it).
    mlir::Type  repr_value_type(RefReprKind k);
    // Storage representation (the in-field / in-element slot LLVM type).
    mlir::Type  repr_storage_type(RefReprKind k);
    // Storage {size, align}.
    Layout      repr_storage_layout(RefReprKind k);
    // By-VALUE return ABI of a reference. A separate axis from storage_type:
    // dyn/slice fat pairs are returned by value as their 16B storage (the
    // A3/A4 slice/dyn-return-by-value leak fix — a ptr-to-local would dangle),
    // whereas closures and custom-DST refs are returned as their 8B by-pointer
    // value (storage owned by the callee escape path / caller slot, not
    // materialized in the return). Thin refs return their 8B value. NotARef →
    // nullptr (caller falls through to logos_to_mlir for non-reference returns).
    mlir::Type  repr_return_type(RefReprKind k);
    // Extract the data / metadata half of a reference value (compute form).
    // Thin: data = the value itself, no metadata (meta returns null). Fat: load
    // field 0 / field 1 of the {data, meta} pair the value points at (mirrors
    // slice_ptr / slice_len).
    mlir::Value repr_data(RefReprKind k, mlir::Value v);
    mlir::Value repr_meta(RefReprKind k, mlir::Value v);
    // Construct a reference value from its data + metadata halves (from_raw_parts).
    // Thin: the value IS the data pointer. Fat: spill a {data, meta} pair to an
    // alloca and return its address (meta stored as vtable-ptr for FatDyn, else
    // an i64 length). Mirrors slice_lit.
    mlir::Value repr_construct(RefReprKind k, mlir::Value data, mlir::Value meta);

    // STORAGE <-> COMPUTE conversion — the heart of the storage/compute split,
    // made first-class so a future repr (a self-relative `zoned T`, whose
    // conversion is offset±anchor, not identity) plugs in as just a new pair of
    // these. Today every repr's conversion is trivial:
    //   materialize(slot)  — storage slot  -> compute value. Thin: load the 8B
    //     ptr. Fat (always-16B pair): the value IS the storage address (the
    //     by-pointer fat value convention) -> return slot. NOTE: FatDyn shares
    //     the 16B form but some sites carry a dyn value as a by-value aggregate
    //     instead of a slot address; those sites keep their own handling and do
    //     NOT route here (the dyn value-convention is context-dependent).
    //   lower(val, slot)   — compute value -> storage slot. Thin: store the 8B
    //     ptr. Fat: memcpy the 16B pair (val is a ptr to the source pair).
    mlir::Value repr_materialize(RefReprKind k, mlir::Value slot);
    void        repr_lower(RefReprKind k, mlir::Value val, mlir::Value slot);

    // F3 (§8): storage↔compute bridge for a `#[zoned2]` niche enum (the
    // compiler-owned wa_materialize/wa_lower). `slot` is the at-rest word
    // (Ref arm self-relative, anchor = slot); the value is a by-pointer enum
    // (ptr to a fresh alloca holding the word with the Ref arm absolute).
    mlir::Value zoned_enum_materialize(mlir::Value slot);
    void        zoned_enum_lower(mlir::Value val, mlir::Value slot);
    // True iff `t` is a tagged enum carrying the `#[zoned2]` (zoned) marker —
    // i.e. its Ref arm is stored self-relative at-rest. Returns the TaggedEnumInfo
    // (or nullptr) so callers can reuse it.
    const TaggedEnumInfo* zoned_niche_enum_info(TypeRef t);

    // True iff `t` is a DstRef whose pointee struct has a literal `[T]` slice
    // tail — i.e. a GENUINELY 16-byte {data,len} fat ref (the len is carried
    // inline). A `dyn`-tail DstRef (`&RcInner<dyn>`) is physically thin (8-byte;
    // the vtable lives in the heap object) and returns false. Discriminates the
    // 16B-fat custom-DST ref from the thin dyn-tail/escape handle at the
    // store/copy sites. Looks the pointee struct up in all_struct_defs_ and
    // checks the last field's kind (Slice / UnsizedSlice).
    bool dstref_has_slice_tail(TypeRef t);

    // True iff `t` is a DstRef to a #[self_describing] DST — PHYSICALLY THIN (8B,
    // pointer straight to the header; tail length recovered in-band via dst_len).
    // The discriminator routing data/len-extraction + repr/store/access onto the
    // thin path. See docs/internals/self-describing-dst-thin-ref.md.
    bool dstref_pointee_self_describing(TypeRef t);

    // Enum representation access — the SINGLE chokepoint for the tagged-enum
    // memory layout, so niche-packing (Phase 3.5) becomes a localized change
    // rather than an edit across every construct/match/drop site. Today every
    // tagged enum is `{ i32 disc @field0, payload @field1 }` and these just GEP
    // those fields; a niche-packed enum will instead encode/decode the
    // discriminant in an invalid bit-pattern of the payload here.
    //   enum_payload_ptr — address of the payload area for `enum_addr`.
    //   enum_store_disc  — write the discriminant for variant `disc`.
    //   enum_load_disc   — read the discriminant as an i32 value.
    mlir::Value enum_payload_ptr(mlir::Value enum_addr, const TaggedEnumInfo& info);
    void        enum_store_disc(mlir::Value enum_addr, const TaggedEnumInfo& info,
                                int64_t disc);
    // Store a RUNTIME discriminant value (not a compile-time constant) — used by
    // the "untyped None reassign" paths where the disc is an i32 SSA value. For
    // a niche-packed enum this has no statically-known variant, so it is only
    // valid on a non-niche `{disc,payload}` enum (asserted there).
    void        enum_store_disc_value(mlir::Value enum_addr, const TaggedEnumInfo& info,
                                      mlir::Value disc_val);
    mlir::Value enum_load_disc(mlir::Value enum_addr, const TaggedEnumInfo& info);

    // Byte size (= layout_of(t).size). Thin wrapper kept for existing callers.
    uint64_t logos_abi_byte_size(TypeRef t,
                                  std::unordered_set<std::string>& seen) {
        return layout_of(t, seen).size;
    }

    // i64 byte-size CONSTANT of a Logos type, from the unified layout. Use this
    // for value-copy memcpy sizes instead of `mlir::DataLayout::getTypeSize` —
    // at mlir-gen time the module has no target datalayout, so MLIR's DataLayout
    // PACKS aggregates (drops inter-field padding) and under-copies (e.g. an
    // {i32, i64}/enum payload-at-offset-8 copied as 12 bytes loses 4 bytes).
    // THE SLOT-SIZE SENSOR (#80). Every value-copy rebind arm in gen_assign
    // memcpy's N bytes into a local's slot on the ASSUMPTION that the slot was
    // allocated at the value's storage size. That assumption was silently false
    // for every deferred-init fat local (`let v: str;` allocated 8 bytes, the
    // rebind wrote 16) and the overflow landed on the NEXT local — a stack
    // overwrite with no diagnostic. Where the destination is a local alloca we
    // can SEE the allocated type: a slot smaller than the copy is a compiler
    // malfunction, reported as one. Non-alloca destinations (params, GEPs into
    // aggregates, globals) are not decided here and stay silent.
    //
    // MEASURED, and the reporting arm is PINNED. Over the whole fixture corpus
    // (4 473 programs, 399 of them reaching a call site) and a full
    // stdlib+examples build the guard is ASKED 45 494 times — 17 113 + 28 381 —
    // and `have` equalled `want` on every single one, with no struct-key miss
    // anywhere (recipe: a temporary fprintf on this line and at the
    // `struct_types_` lookup in place_slot_type, then the two sweeps). So the
    // report itself is unreachable from correct codegen, which is the point of
    // a sensor and also the thing that makes a "green" here worthless. The arm
    // is executed by `fail/mlirgen_slot_fits_sensor`, through the name-scoped
    // fault injection in `declare_local_place` (a local named
    // `__slotfit_canary*` gets the pre-#80 8-byte handle slot); delete the
    // injection and that fixture goes green-by-silence, which is exactly the
    // failure it exists to make visible.
    void check_slot_fits(mlir::Value slot, uint64_t bytes,
                         std::string_view what, std::string_view name) const {
        if (!slot) return;
        auto al = slot.getDefiningOp<mlir::LLVM::AllocaOp>();
        if (!al) return;
        auto et = al.getElemType();
        if (!et) return;
        uint64_t have = mlir_abi_size(et);
        if (have >= bytes) return;
        bug("assignment to '{}' copies {} bytes of {} into a {}-byte slot — "
            "the slot was allocated from a different type than the value's "
            "storage type", name, bytes, what, have);
    }
    // Fault injection for `place_slot_type`'s two DECLINE arms (the null type
    // and the struct-key miss). Both report a compiler malfunction, so correct
    // codegen cannot reach either — and an unexecutable report is exactly the
    // green nobody ran that `check_slot_fits` was caught being. Set only inside
    // `declare_local_place` for a local whose name starts with
    // `__slotdecline_canary`, and cleared on the next statement; pinned by
    // `fail/mlirgen_place_slot_decline`.
    bool slot_decline_canary_ = false;
    mlir::Value size_const(TypeRef t) {
        return builder_.create<mlir::LLVM::ConstantOp>(
            loc_, builder_.getI64Type(),
            builder_.getI64IntegerAttr((int64_t)layout_of(t).size));
    }

    // Resolve a tagged enum name from the expression type (handles generic enums).
    const TaggedEnumInfo* resolve_tagged_enum(const std::string& name, TypeRef type);

    // Build the LLVM struct type for a variant's payload, using the INLINE
    // aggregate type (not the collapsed `ptr`) for struct/tuple payload fields.
    // The constructor memcpy's aggregate payload fields inline (their full ABI
    // byte size), so the payload struct type used for field GEPs must reserve
    // the same inline footprint — otherwise a field after an aggregate (e.g.
    // the `z` in `C(T2, i64)`) lands at the wrong offset and aliases the
    // aggregate's bytes. Pass `vp.field_types` collapsed for scalar fields.
    mlir::LLVM::LLVMStructType variant_payload_struct(
        const TaggedEnumInfo::VariantPayload& vp);

    // Build the anonymous LLVM struct type for a tuple.
    mlir::Type tuple_llvm_type(TypeRef t);

    // Compute the LLVM return type matching how function definitions return
    // values (struct/tuple/enum aggregates by value, not as ptr). Used to
    // build correct ABI-matching call types for indirect / fn-pointer calls.
    mlir::Type fn_call_ret_llvm_type(TypeRef ret_type);

    // Slice LLVM type: { ptr, i64 }
    mlir::Type slice_llvm_type();

    // Closure LLVM type: { fn_ptr, env_ptr }
    mlir::Type closure_llvm_type();

    // Trait-object fat pair: { data_ptr, vtable_ptr }, 16-byte value-repr.
    mlir::Type dyn_llvm_type();

    // ── Vtable / dyn ─────────────────────────────────────────────
    void emit_trait_vtables(mlir::ModuleOp mod, const LProgram& prog);
    void emit_tag_dispatch_tables(mlir::ModuleOp mod, const LProgram& prog);
    // §6.2 statics (S25): emit one llvm.mlir.global per `static [mut]` item
    // and a @__logos_static_init that runs each initializer into its global
    // address at program startup (called from main's prologue, Pass 3).
    void emit_static_globals(mlir::ModuleOp mod, const LProgram& prog);
    bool has_static_init_ = false;  // set by emit_static_globals if any non-
                                    // extern static needs runtime init
    mlir::Value build_inline_vtable(std::string_view trait_name,
                                     std::string_view type_name,
                                     TypeRef concrete_ty = {});
    // Ensure the `[N x ptr]` vtable global for (trait, type) exists (placeholder
    // + recorded spec) and return its symbol; "" if no methods are registered.
    // build_inline_vtable = ensure_vtable_global + AddressOf. Recurses to build
    // each supertrait's vtable global for the stored super-vtable-pointer slots.
    std::string ensure_vtable_global(std::string_view trait_name,
                                     std::string_view type_name,
                                     TypeRef concrete_ty);
    // Build a fat {data,vtable} pair. `heap=false` (default) → stack alloca:
    // used for borrow `&dyn`/`&mut dyn` (value-fat-pair model; no leak). The
    // CONSUMER copies the 16 bytes when it escapes (struct field / array /
    // Vec<&dyn> / by-value return). `heap=true` → malloc(16): used for OWNING
    // `Box<dyn>` and raw `*const/*mut dyn` handles, whose single-word handle is
    // stored/escapes (Vec<Box<dyn>>, persistent NodeARC.p) and survives via the
    // heap slot (Box<dyn>'s drop frees it).
    mlir::Value coerce_to_dyn(mlir::Value data_ptr, std::string_view trait_name,
                               std::string_view src_type_name,
                               TypeRef concrete_ty = {});
    // G168-A: unsize-coerce a concrete `Box<Concrete>` / `&Concrete` / struct
    // value into a fat `{data,vtable}` handle when the destination SLOT is a
    // trait object (`dyn`/`Box<dyn>`/`&dyn`) but the VALUE is still concrete —
    // e.g. an enum-variant payload typed `Box<dyn>` constructed from a
    // `Box<Concrete>`. No-op when not applicable or already a trait object.
    mlir::Value coerce_value_to_dyn_if_needed(mlir::Value val, TypeRef slot_lt,
                                              TypeRef val_lt);
    mlir::Value gen_dyn_dispatch(lir_view::EMethodCallView v, TypeRef ret_logos_type);
    mlir::Value gen_tagged_dispatch(lir_view::EMethodCallView v, TypeRef ret_logos_type);
    // Normalise a `&dyn Trait` expression to a POINTER to its 16-byte
    // {data,vtable} storage (the receiver-navigation shared by vtable dispatch
    // and the `__dyn_data__` / `__dyn_vtable__` reconstruction intrinsics).
    // Value-fat-pair model: handles a dyn-let / &dyn param (scope_ holds the
    // storage pointer directly), an alloca-backed `&(&dyn)` (load once), and a
    // by-value fat pair (spill). Returns null on failure.
    mlir::Value dyn_storage_ptr(lir_view::ExprRef recv_ref);

    // ── malloc / free helpers ─────────────────────────────────────
    void ensure_malloc_free(mlir::ModuleOp mod);
    mlir::Value call_malloc(mlir::Value size);
    void call_free(mlir::Value ptr);
    mlir::Value sizeof_struct(mlir::LLVM::LLVMStructType struct_type);

    // Cheap allocation-free replacement for `type_str(t) == "AnyVal"`.
    // The old form materialised an std::string for every check; this fires
    // in hot paths (make_fn_type, logos_to_mlir) where stdlib's 3500+
    // forward_declare calls each do several checks per fn. AnyVal is
    // declared `#[zoned] struct` so its kind is ZonedStruct, not Struct —
    // the type_str-based check matched both because type_str renders the
    // struct_name for both kinds.
    static bool is_anyval(TypeRef t) noexcept {
        if (!t) return false;
        auto k = t.kind();
        if (k != LogosType::Kind::Struct && k != LogosType::Kind::ZonedStruct)
            return false;
        // pkg-CHECKED, mirroring the sema-side twin (task #99). AnyVal has no
        // declaration in any .logos; the compiler synthesises it un-packaged.
        // A user `struct AnyVal` carries its own package and must NOT be lowered
        // as an i32 — that read its i64 field as garbage, silently.
        return t.struct_name() == "AnyVal" && t.pkg_name().empty();
    }
    // pkg-CHECKED (task #99), the mlir twin of sema's `is_string_view`. The writ
    // capture path treats a StringView as a (ptr,len) pair and EXTRACTS fields 0
    // and 1 out of it; a user `struct StringView` of any other shape would have
    // had two arbitrary words read out of it and handed to writ_ctr_alloc_str.
    // Declared once tree-wide: stdlib/lang/writ/wstatic.logos:36, package
    // `logos.lang.writ.wstatic`.
    static bool is_stdlib_string_view(TypeRef t) noexcept {
        if (!t) return false;
        auto k = t.kind();
        if (k != LogosType::Kind::Struct && k != LogosType::Kind::ZonedStruct)
            return false;
        return t.struct_name() == "StringView" &&
               t.pkg_name() == "logos.lang.writ.wstatic";
    }
    // FQN-checked stdlib `logos.mem.boxed.Box<T>` (not a user struct named Box).
    // See the sema-side twin for rationale. pkg tolerated empty for internal
    // paths (mlir-gen sometimes strips struct pkg); a user Box keeps its own.
    static bool is_stdlib_box(TypeRef t) noexcept {
        return is_stdlib_smart_ptr(t, "Box", "logos.mem.boxed");
    }
    // NOTE: `Box<dyn Trait>` does NOT appear as a Box<TraitObject> struct in
    // mlir-gen — sema collapses it to an OWNING bare `TraitObject` (16-byte
    // {data,vtable} fat pair, IDENTICAL repr to `&dyn`; differs only by ownership
    // → drop calls vtable[0] then deallocs `data`). So all dyn representation /
    // dispatch / stride code keys uniformly on Kind::TraitObject; no Box special-
    // casing is needed here.
    // FQN-checked stdlib smart-pointer struct (name + package; pkg tolerated
    // empty for internal paths where it was stripped).
    static bool is_stdlib_smart_ptr(TypeRef t, std::string_view name,
                                    std::string_view pkg) noexcept {
        if (!t) return false;
        auto k = t.kind();
        if (k != LogosType::Kind::Struct && k != LogosType::Kind::ZonedStruct)
            return false;
        if (t.struct_name() != name) return false;
        auto p = t.pkg_name();
        return p.empty() || p == pkg;
    }
    // Smart-pointer kind of a CONCRETE Rc<T>/Arc<T>/Box<T> struct value (the
    // SOURCE of a `as Rc<dyn>` unsize cast), or Borrow if not a smart pointer.
    static TypeRef::OwningKind stdlib_smart_ptr_kind(TypeRef t) noexcept {
        if (is_stdlib_smart_ptr(t, "Box", "logos.mem.boxed")) return TypeRef::OwningKind::Box;
        if (is_stdlib_smart_ptr(t, "Rc",  "logos.mem.rc"))    return TypeRef::OwningKind::Rc;
        if (is_stdlib_smart_ptr(t, "Arc", "logos.mem.sync"))  return TypeRef::OwningKind::Arc;
        return TypeRef::OwningKind::Borrow;
    }

    // ── Function type from LFunction ─────────────────────────────
    mlir::FunctionType make_fn_type(lir_view::FunctionView fn);
    // Stamp type-derived LLVM parameter attributes (noundef/align/dereferenceable
    // on thin references, noalias on &mut) onto a freshly-created FuncOp. Mirrors
    // make_fn_type's slot-push order to keep MLIR arg indices aligned. See
    // docs/internals/param-attrs.md for the soundness gates.
    void apply_param_attrs(mlir::func::FuncOp f, lir_view::FunctionView fn);
    // When `is_binary_skip` is true, the FuncOp is created private so the
    // module ends up with a declaration-only entry (no body, matching the
    // archive-resident implementation).
    void forward_declare(mlir::ModuleOp mod, lir_view::FunctionView fn,
                          bool is_binary_skip = false);
    bool gen_function_body(mlir::func::FuncOp func, lir_view::FunctionView fn);

    // ── Block ─────────────────────────────────────────────────────
    // Stage 3g.3: BlockRef / StmtRef in signatures so the dispatcher no
    // longer round-trips offset → C++ block ptr → offset via lblock_of.
    void gen_block(lir_view::BlockRef block);

    // ── Statements ────────────────────────────────────────────────
    void gen_stmt(lir_view::StmtRef stmt);

    void gen_stmt_kind(lir_view::SLetView v);
    void gen_stmt_kind(lir_view::SAssignView v);
    void gen_stmt_kind(lir_view::SReturnView v);
    void gen_stmt_kind(lir_view::SIfView v);
    void gen_stmt_kind(lir_view::SWhileView v);
    void gen_stmt_kind(lir_view::SForView v);
    void gen_stmt_kind(lir_view::SLoopView v);
    void gen_stmt_kind(lir_view::SBreakView v);
    void gen_stmt_kind(lir_view::SContinueView v);
    void gen_stmt_kind(lir_view::SFieldWriteView v);
    void gen_stmt_kind(lir_view::STupleWriteView v);
    void gen_stmt_kind(lir_view::SDerefFieldWriteView v);
    void gen_stmt_kind(lir_view::SIndexWriteView v);
    void gen_stmt_kind(lir_view::SFieldIndexWriteView v);
    void gen_stmt_kind(lir_view::SExprStmtView v);
    void gen_stmt_kind(lir_view::SMatchView v);
    void gen_stmt_kind(lir_view::SForEachView v);
    void gen_stmt_kind(lir_view::SBlockView v);
    void gen_stmt_kind(lir_view::SDropView v);
    // G158-4: recursively drop a value of type `ty` located at `value_ptr`
    // (struct → user drop + fields; tuple → elements; enum → variant-switched
    // payload; array → each element; ref/ptr/scalar → nothing). Handles
    // arbitrary nesting (array-of-struct, struct-with-array-field, …).
    // `top_level=true` mirrors SDrop's owner semantics: after a user `impl
    // Drop` runs, the value's FIELDS/payload are ALSO dropped (the owner drops
    // both). `top_level=false` (nested) calls the user drop and stops (the
    // by-value `self` consumes its own fields at the drop body's scope end).
    // skip_paths (T1-10/B78): dotted paths RELATIVE to this value whose
    // sub-values were moved out — an exact segment match skips that
    // child entirely; a deeper path recurses with the stripped remainder
    // so only the moved leaf is suppressed and its siblings still drop.
    void gen_drop_value(mlir::Value value_ptr, TypeRef ty, bool top_level = false,
                        const std::set<std::string>* skip_paths = nullptr);
    // Drop an OWNING `Box<dyn Trait>` whose binding storage `handle` IS the
    // 8-byte heap handle to a 16-byte {data,vtable} fat pair. Sequence (null-
    // guarded): load data(field0)+vtable(field1); call vtable[0](data)
    // (drop_in_place runs the concrete's destructor + its owned fields);
    // free(data) (the boxed concrete); free(handle) (the fat slot).
    void gen_drop_owning_dyn_handle(mlir::Value handle, TypeRef::OwningKind kind);
    // Drop an owning `Box<[T]>` fat slice: drop each element (runtime loop, if T
    // is droppable) then free the heap buffer. `slice_ptr` points at {data,len}.
    void gen_drop_owning_slice(mlir::Value slice_ptr, TypeRef ty);
    // Drop an owning `Box<Foo>` custom-DST: drop droppable prefix fields + tail
    // elements (runtime loop over the fat-pointer length) then free the block.
    // `dst_ptr` points at the {data,len} fat pair (DstRef value = slice repr).
    void gen_drop_owning_dst(mlir::Value dst_ptr, TypeRef ty);
    // Drop the concrete payload behind a `&dyn` fat pair IN PLACE — run
    // vtable[0](data) (the concrete Drop) only, with NO free and NO refcount
    // change. This is the move-out-drop of an unsized `dyn` tail (`let _v: T =
    // self.inner.val` with T = dyn): same "run Drop, don't free the block"
    // semantics as the sized case (the block is freed separately by the caller).
    // `fat_ptr` points at the 16-byte {data,vtable} pair.
    void gen_drop_dyn_in_place(mlir::Value fat_ptr);
    // Codegen-side "does a value of this type own anything droppable" — mirrors
    // sema's has_droppable_fields; gates gen_drop_value recursion to avoid empty
    // GEP/loop emission for non-droppable members.
    bool value_needs_drop(TypeRef ty);
    // #123 — `#[no_auto_drop]` on the struct behind `ty` (see mlir_gen_stmt.cpp).
    bool type_is_no_auto_drop(TypeRef ty);
    void gen_stmt_kind(lir_view::SDerefWriteView v);
    void gen_stmt_kind(lir_view::SLetElseView v);
    void gen_stmt_kind(lir_view::SChainFieldWriteView v);

    void gen_let(lir_view::SLetView v);
    void gen_let_inner(lir_view::SLetView v);
    // -g: emit a DbgDeclare for a memory-backed local. Safe rule — only when the
    // slot is an AllocaOp whose element size == the variable's ABI size (rejects
    // ref aliases / SSA bindings). name's slot read from scope_.
    void emit_local_dbg_declare(std::string_view name, TypeRef ty, uint32_t line);
    // -g: emit DWARF info for a parameter. Aggregate-by-pointer args (struct/
    // enum/array arriving as ptr) → DbgDeclare(arg); scalar/pointer/by-value
    // args → DbgValue(arg). arg_no is 1-based.
    void emit_param_dbg_declare(std::string_view name, TypeRef ty,
                                mlir::Value arg, unsigned arg_no, uint32_t line);
    void gen_assign(lir_view::SAssignView v);
    void gen_return(lir_view::SReturnView v);
    void gen_if(lir_view::SIfView v);
    void gen_while(lir_view::SWhileView v);
    void gen_for(lir_view::SForView v);
    void gen_loop(lir_view::SLoopView v);
    void gen_break(lir_view::SBreakView v);
    void gen_continue();
    void gen_for_each(lir_view::SForEachView v);
    void gen_field_write(lir_view::SFieldWriteView v);
    void gen_deref_field_write(lir_view::SDerefFieldWriteView v);
    void gen_chain_field_write(lir_view::SChainFieldWriteView v);
    void gen_tuple_write(lir_view::STupleWriteView v);
    void gen_index_write(lir_view::SIndexWriteView v);
    void gen_field_index_write(lir_view::SFieldIndexWriteView v);
    void gen_match(lir_view::SMatchView v);

    // ── Expressions ───────────────────────────────────────────────
    mlir::Value gen_expr(lir_view::ExprRef er);

    mlir::Value gen_expr_kind(lir_view::ELitIntView v,   TypeRef type);
    mlir::Value gen_expr_kind(lir_view::ELitFloatView v, TypeRef type);
    mlir::Value gen_expr_kind(lir_view::ELitBoolView v,  TypeRef);
    mlir::Value gen_expr_kind(lir_view::ELitStrView v,   TypeRef);
    mlir::Value gen_expr_kind(lir_view::EVarRefView v, TypeRef type);
    mlir::Value gen_expr_kind(lir_view::EEnumLitView v, TypeRef type);
    mlir::Value gen_expr_kind(lir_view::EEnumLitDataView v, TypeRef type);
    mlir::Value gen_expr_kind(lir_view::EBinOpView v, TypeRef);
    mlir::Value gen_expr_kind(lir_view::EUnaryView v, TypeRef);
    mlir::Value gen_expr_kind(lir_view::EAddrOfView v, TypeRef);
    mlir::Value gen_expr_kind(lir_view::EAddrOfTempView v, TypeRef);
    // #92 const promotion — read-only static storage for `&<const expr>`.
    // Null iff the shape is outside const_promote::is_const_value.
    mlir::Value gen_promoted_const(lir_view::ExprRef e, TypeRef t);
    mlir::Value gen_expr_kind(lir_view::EDerefView v, TypeRef type);
    // True when `*operand` over a `*const/*mut dyn` is a genuine pointer-INTO-
    // storage (a container accessor return, e.g. `HashMap::get → *const Box<dyn>`)
    // and must LOAD the stored handle — as opposed to the default, where the
    // value already IS the dyn handle (the raw fat pointer) and `*p` is a no-op.
    bool deref_operand_is_ptr_to_dyn_handle(lir_view::ExprRef operand);
    mlir::Value gen_expr_kind(lir_view::ECallView v, TypeRef ret_logos_type);
    mlir::Value gen_expr_kind(lir_view::EMethodCallView v, TypeRef ret_logos_type);
    mlir::Value gen_expr_kind(lir_view::EFieldReadView v, TypeRef type);
    mlir::Value gen_expr_kind(lir_view::EIndexReadView v, TypeRef type);
    mlir::Value gen_expr_kind(lir_view::EStructLitView v, TypeRef);
    mlir::Value gen_expr_kind(lir_view::EArrLitView v, TypeRef type);
    mlir::Value gen_expr_kind(lir_view::ECastView v, TypeRef type);
    mlir::Value gen_expr_kind(lir_view::EIfExprView v, TypeRef type);
    mlir::Value gen_expr_kind(lir_view::EMatchExprView v, TypeRef type);
    mlir::Value gen_expr_kind(lir_view::ETupleLitView v, TypeRef type);
    mlir::Value gen_expr_kind(lir_view::ETupleIndexView v, TypeRef type);
    mlir::Value gen_expr_kind(lir_view::EClosureBoxView v, TypeRef type);
    mlir::Value gen_closure(lir_view::EClosureBoxView v, TypeRef);
    mlir::Value gen_expr_kind(lir_view::EClosureCallView v, TypeRef type);
    mlir::Value gen_expr_kind(lir_view::EFnPtrCallView v, TypeRef type);
    mlir::Value gen_expr_kind(lir_view::ESliceLitView v, TypeRef);
    mlir::Value gen_expr_kind(lir_view::ESliceIndexView v, TypeRef type);
    mlir::Value gen_expr_kind(lir_view::ESliceLenView v, TypeRef);
    mlir::Value gen_expr_kind(lir_view::ESlicePtrView v, TypeRef);
    mlir::Value gen_expr_kind(lir_view::EFormatCallView v, TypeRef);
    mlir::Value gen_expr_kind(lir_view::EPackExpandView v, TypeRef);
    mlir::Value gen_expr_kind(lir_view::ESizeOfView v, TypeRef);
    mlir::Value gen_expr_kind(lir_view::EAlignOfView v, TypeRef);
    mlir::Value gen_expr_kind(lir_view::ETypeCodeOfView v, TypeRef);
    mlir::Value gen_expr_kind(lir_view::EBlockExprView v, TypeRef);
    mlir::Value gen_expr_kind(lir_view::ETryView v, TypeRef type);
    mlir::Value gen_expr_kind(lir_view::EWritLitView v, TypeRef);
    mlir::Value gen_expr_kind(lir_view::EPtrArithView v, TypeRef);
    mlir::Value gen_expr_kind(lir_view::EPtrDiffView v, TypeRef);
    mlir::Value gen_expr_kind(lir_view::EReflectOfView v, TypeRef);
    // Coerce a Logos runtime value to AnyVal.raw (u32) for writ capture substitution.
    mlir::Value coerce_to_anyval_raw(mlir::Value v, TypeRef t);
    // writ: coerce a scalar capture to an 8-byte value-form WAny word.
    mlir::Value coerce_to_wany_raw(mlir::Value v, TypeRef t);

    // ── Struct helpers ────────────────────────────────────────────
    mlir::Value get_struct_ptr(const std::string& name);
    mlir::Value gep_field(mlir::Value base, const StructInfo& info,
                          const std::string& field_name);
    std::pair<mlir::Value, std::string> gen_recv_struct(lir_view::ExprRef recv);
    std::pair<mlir::Value, std::string> gen_recv_struct_inner(lir_view::ExprRef recv);
    mlir::Value gen_struct_lit(lir_view::EStructLitView v);

    // Bind the payload of a tagged-enum VariantData pattern given a pointer to
    // the enum struct (`enum_ptr`) and its TaggedEnumInfo. Handles scalar,
    // inline-struct, ref-bind, and trait-object payload bindings. Used by the
    // tuple-element recursion in match payload extraction (a tuple whose
    // element is an enum, e.g. `(E::A(x), F::B(y))`); the bound names are
    // appended to `added`. Shared by the statement and expression match paths.
    void bind_enum_payload(mlir::Value enum_ptr,
                           const TaggedEnumInfo* te,
                           lir_view::PatVariantDataView pvd,
                           std::vector<std::string>& added,
                           const std::unordered_map<std::string, mlir::Value>* shared = nullptr);

    // Recursive pattern matcher for arbitrarily nested patterns (a tuple
    // element that is itself a tuple / variant / or-pattern). `slot_ptr` points
    // to the value's storage (for an enum value the slot holds the heap ptr;
    // pat_test/pat_bind load it). pat_test returns a pure i1 (no control flow —
    // an or-pattern is the OR of its alts' tests). pat_bind binds names into
    // scope_; for an or-pattern it dispatches per-alt and binds into the
    // pre-created `shared` allocas (name→alloca) so the join sees one slot.
    mlir::Value pat_test(lir_view::PatRef pat, mlir::Value slot_ptr, TypeRef ty);
    void        pat_bind(lir_view::PatRef pat, mlir::Value slot_ptr, TypeRef ty,
                         const std::unordered_map<std::string, mlir::Value>* shared = nullptr);
    // Collect (name,type) pairs a pattern binds (first-alt for or). Used to
    // pre-create shared allocas for an or-pattern's bindings.
    void collect_pat_bindings(lir_view::PatRef pat, TypeRef ty,
                              std::vector<std::pair<std::string, TypeRef>>& out);
    // D3 (task #50): after a pattern binding stored a THIN ref/ptr-to-struct
    // value into its alloca(ptr) slot, register the plain-let mut-ref protocol
    // (var_struct_ + var_local_ptrs_) so gen_recv_struct/get_struct_ptr LOAD
    // through the slot instead of returning the slot address as the object.
    void register_thin_ref_struct_binding(const std::string& name, TypeRef ty);

    // ── Array helpers ─────────────────────────────────────────────
    mlir::Value get_subscript_ptr(const std::string& name);
    mlir::Type subscript_elem_type(const std::string& name);
    // G163-2: recursively compute the ADDRESS of an lvalue place expression
    // (VarRef / IndexRead / FieldRead / TupleIndex / Deref chain) — the real
    // storage address, not a value copy. Returns null for shapes it can't
    // address (callers must treat null as "not a place"). Used by the general
    // place-write (`a[i][j] = v`, `(*p).0 = v`, deep mixes) and `&mut <place>`.
    mlir::Value gen_lvalue_addr(lir_view::ExprRef e);
    // MLIR slot type for one element/field of a place (struct/tuple inline
    // aggregate type, else logos_to_mlir) — the GEP stride into an aggregate.
    mlir::Type place_slot_type(TypeRef t);
    // Declare a LOCAL PLACE for a binding that has no initialiser
    // (`let v: T;`): allocate the slot at the STORAGE type of T and register
    // the shape the read/assign paths resolve `v` through. One site, so the
    // deferred spelling cannot drift from the initialised `let` arms — the two
    // used to decide the slot independently and disagreed for every fat repr
    // (#80: `logos_to_mlir(str)` is the 8-byte HANDLE type, so `v = "abcde"`
    // memcpy'd 16 bytes into an 8-byte slot). Returns the alloca, or null when
    // the type has no representation.
    mlir::Value declare_local_place(const std::string& nm, TypeRef ty);
    mlir::Value gen_arr_lit(lir_view::EArrLitView v, mlir::Type elem_type,
                            TypeRef logos_elem = TypeRef(nullptr));
    // Large-fill fast path: when EVERY element of the array literal is the
    // SAME simple constant (the shape sema's `[v; N]` fill expansion
    // produces) and N >= a small threshold, emit ONE memset (all-zero-bits
    // scalars) or ONE store/memcpy inside a compact index loop into `dst`
    // instead of N unrolled per-element stores. The unrolled form produced
    // O(N) straight-line memops per literal — pathological SelectionDAG /
    // scheduler times on the big fixed-array structs (queue-2 QEnv/RBinds/
    // QRelReg tables surfaced it: minutes-long std-layer builds). Returns
    // true when the fast path was emitted (caller skips the per-element
    // loop); false = not a splat, fall through.
    bool gen_arr_fill_fast(lir_view::EArrLitView v,
                           mlir::LLVM::LLVMArrayType arr_type,
                           mlir::Type elem_type, mlir::Value dst);

    // ── format() built-in ─────────────────────────────────────────
    static int format_type_tag(TypeRef t) noexcept;
};

} // namespace logos::compiler
