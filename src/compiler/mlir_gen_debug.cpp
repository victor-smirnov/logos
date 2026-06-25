// Logos project — https://github.com/victor-smirnov/logos
//
// mlir_gen_debug.cpp — DWARF debug-info (-g) emission for MLIRGen.
//
// Strategy: attach MLIR LLVM-dialect debug-info attributes (DICompileUnit /
// DISubprogram / DIFile) and per-statement FileLineColLoc locations fused with
// the current function's DISubprogram. translateModuleToLLVMIR then lowers these
// to LLVM debug metadata, and the LLVM backend emits standard DWARF — so stock
// gdb/lldb get source-level line tables (Stage 1), and later locals/types.
//
// Locations are debug-fused ONLY while emitting a TOP-LEVEL user function body
// (begin_fn_debug … end_fn_debug). Nested compiler-generated functions
// (closures, drop glue, ctors) suspend the scope via DebugScopeSuspend so a
// single DISubprogram never attaches to two LLVM functions (which the LLVM
// verifier rejects and which produces nonsensical DWARF).

#include "mlir_gen_impl.hpp"

#include <mlir/IR/Location.h>
#include <mlir/Interfaces/DataLayoutInterfaces.h>

#include <llvm/Support/Path.h>

namespace logos::compiler {

using namespace lir;

namespace {
// DWARF source language for the compile unit. Logos is Rust-shaped but we keep
// language-neutral C semantics here; gdb display is driven by our own
// pretty-printers (Stage 4/5), not by language-specific DWARF heuristics.
constexpr unsigned kDwarfLangC99 = 0x000c;  // DW_LANG_C99

// DWARF tags / base-type encodings (subset).
constexpr unsigned DW_TAG_member           = 0x0d;
constexpr unsigned DW_TAG_pointer_type     = 0x0f;
constexpr unsigned DW_TAG_structure_type   = 0x13;
constexpr unsigned DW_TAG_base_type        = 0x24;
constexpr unsigned DW_ATE_boolean          = 0x02;
constexpr unsigned DW_ATE_float            = 0x04;
constexpr unsigned DW_ATE_signed           = 0x05;
constexpr unsigned DW_ATE_unsigned         = 0x07;
constexpr unsigned DW_ATE_UTF              = 0x10;

inline uint64_t align_up(uint64_t v, uint64_t a) {
    return a ? (v + a - 1) / a * a : v;
}

// x86-64 SysV ABI alignment (bytes) for an MLIR/LLVM type. Computed directly
// rather than via mlir::DataLayout: at mlir-gen time the module carries no data
// layout spec, so DataLayout returns defaults (e.g. align(i64)=4) that disagree
// with the target layout the LLVM backend actually uses — which would mis-place
// DWARF struct members. This mirrors what Clang/LLVM emit for non-packed types.
inline uint64_t abi_align_bytes(mlir::Type t) {
    if (auto it = mlir::dyn_cast<mlir::IntegerType>(t)) {
        uint64_t bytes = (it.getWidth() + 7) / 8, a = 1;
        while (a < bytes && a < 16) a <<= 1;
        return a;
    }
    if (mlir::isa<mlir::Float32Type>(t)) return 4;
    if (mlir::isa<mlir::Float64Type>(t)) return 8;
    if (mlir::isa<mlir::LLVM::LLVMPointerType>(t)) return 8;
    if (auto st = mlir::dyn_cast<mlir::LLVM::LLVMStructType>(t)) {
        uint64_t m = 1;
        for (auto e : st.getBody()) m = std::max(m, abi_align_bytes(e));
        return m;
    }
    if (auto at = mlir::dyn_cast<mlir::LLVM::LLVMArrayType>(t))
        return abi_align_bytes(at.getElementType());
    return 1;
}
} // namespace

mlir::LLVM::DIFileAttr MLIRGenImpl::di_file_for(std::string_view path) {
    std::string key(path);
    if (auto it = di_files_.find(key); it != di_files_.end()) return it->second;
    llvm::StringRef p(key);
    llvm::StringRef name = llvm::sys::path::filename(p);
    llvm::StringRef dir  = llvm::sys::path::parent_path(p);
    if (name.empty()) name = "<logos>";
    auto f = mlir::LLVM::DIFileAttr::get(builder_.getContext(), name, dir);
    di_files_.emplace(std::move(key), f);
    return f;
}

mlir::LLVM::DICompileUnitAttr
MLIRGenImpl::ensure_di_cu(mlir::LLVM::DIFileAttr file) {
    if (di_cu_) return di_cu_;
    auto* ctx = builder_.getContext();
    auto id = mlir::DistinctAttr::create(mlir::UnitAttr::get(ctx));
    di_cu_ = mlir::LLVM::DICompileUnitAttr::get(
        ctx, id, kDwarfLangC99, file,
        mlir::StringAttr::get(ctx, "logosc"),
        /*isOptimized=*/false,
        mlir::LLVM::DIEmissionKind::Full,
        mlir::LLVM::DINameTableKind::Default);
    return di_cu_;
}

void MLIRGenImpl::begin_fn_debug(mlir::func::FuncOp func, lir_view::FunctionView fn) {
    di_subprogram_ = {};
    di_file_       = {};
    di_scope_line_ = 0;
    loc_ = builder_.getUnknownLoc();
    if (!debug_info_) return;

    std::string src(fn.source_file());
    if (src.empty()) src = main_source_;     // single-file pipeline: per-fn path empty
    if (src.empty()) src = "<logos>";
    auto* ctx = builder_.getContext();
    di_file_ = di_file_for(src);
    auto cu  = ensure_di_cu(di_file_);

    // Function decl/scope line: the LIR has no decl line on the function, so use
    // the first body statement's source line (the closest available anchor).
    uint32_t scope_line = 0;
    fn.body().each_stmt([&](lir_view::StmtRef s) {
        if (scope_line == 0 && s.line() != 0) scope_line = s.line();
    });
    if (scope_line == 0) scope_line = 1;
    di_scope_line_ = scope_line;

    // Readable name for DWARF `name` (gdb display + `break <fn>`). method_base
    // is the unmangled source name (e.g. "add"); fall back to the raw symbol.
    //
    // We deliberately DO NOT set DW_AT_linkage_name: Logos mangled symbols carry
    // `$` (module suffixes, type-arg packs) which gdb (C language) cannot
    // demangle, so it would register the function under the mangled name and
    // break `break add`. The mangled symbol still lives in the ELF symtab (so
    // `break <mangled>` works) and gdb correlates the subprogram by address.
    std::string bare(fn.method_base());
    if (bare.empty()) bare = std::string(fn.name());
    auto flags = mlir::LLVM::DISubprogramFlags::Definition;
    if (bare == "main" || fn.name() == "main")
        flags = flags | mlir::LLVM::DISubprogramFlags::MainSubprogram;

    // Subroutine type (return + param DI types) so gdb shows the signature and
    // `whatis`/`ptype <fn>` work.
    auto sub_type = di_subroutine_type(fn);

    auto sp = mlir::LLVM::DISubprogramAttr::get(
        ctx,
        /*recId=*/mlir::DistinctAttr{},
        /*isRecSelf=*/false,
        /*id=*/mlir::DistinctAttr::create(mlir::UnitAttr::get(ctx)),
        /*compileUnit=*/cu,
        /*scope=*/di_file_,
        /*name=*/mlir::StringAttr::get(ctx, bare),
        /*linkageName=*/mlir::StringAttr{},   // see note above (gdb `$` demangle)
        /*file=*/di_file_,
        /*line=*/scope_line,
        /*scopeLine=*/scope_line,
        /*subprogramFlags=*/flags,
        /*type=*/sub_type,
        /*retainedNodes=*/llvm::ArrayRef<mlir::LLVM::DINodeAttr>{},
        /*annotations=*/llvm::ArrayRef<mlir::LLVM::DINodeAttr>{});
    di_subprogram_ = sp;

    // Attach the subprogram to the function via a fused location so the LLVM
    // translation sets llvmFunc->setSubprogram(). The underlying point loc gives
    // the function's source line.
    auto fl = mlir::FileLineColLoc::get(ctx, di_file_.getName(), scope_line, 0);
    func->setLoc(mlir::FusedLoc::get(ctx, {mlir::Location(fl)}, sp));

    // Body ops emitted before the first statement (param binding, prologue) get
    // the function-scope location so any call there carries a valid !dbg.
    loc_ = dbg_loc(scope_line);
}

void MLIRGenImpl::end_fn_debug() {
    di_subprogram_ = {};
    di_file_       = {};
    di_scope_line_ = 0;
    loc_ = builder_.getUnknownLoc();
}

mlir::Location MLIRGenImpl::dbg_loc(uint32_t line) {
    if (!debug_info_ || !di_subprogram_) return builder_.getUnknownLoc();
    if (line == 0) line = di_scope_line_ ? di_scope_line_ : 1;
    auto* ctx = builder_.getContext();
    auto fl = mlir::FileLineColLoc::get(ctx, di_file_.getName(), line, 0);
    return mlir::FusedLoc::get(ctx, {mlir::Location(fl)}, di_subprogram_);
}

// ── DI type builder (Stage 2) ───────────────────────────────────────────────

// Leaf DI type from an MLIR type (used for struct/tuple members). Scalars are
// faithful; LLVM pointers → void*; aggregates → opaque sized composite (no
// members) — keeps the builder recursion-free. Signedness is lost here (an
// MLIR i64 may be Logos i64 or u64) — acceptable for member display.
mlir::LLVM::DITypeAttr MLIRGenImpl::di_leaf_from_mlir(mlir::Type t) {
    auto* ctx = builder_.getContext();
    if (auto it = mlir::dyn_cast<mlir::IntegerType>(t)) {
        unsigned w = it.getWidth();
        unsigned enc = (w == 8) ? DW_ATE_signed : DW_ATE_signed;  // members: signed default
        std::string nm = "i" + std::to_string(w);
        return mlir::LLVM::DIBasicTypeAttr::get(ctx, DW_TAG_base_type, nm, (uint64_t)w, enc);
    }
    if (mlir::isa<mlir::Float32Type>(t))
        return mlir::LLVM::DIBasicTypeAttr::get(ctx, DW_TAG_base_type, "f32", 32, DW_ATE_float);
    if (mlir::isa<mlir::Float64Type>(t))
        return mlir::LLVM::DIBasicTypeAttr::get(ctx, DW_TAG_base_type, "f64", 64, DW_ATE_float);
    if (mlir::isa<mlir::LLVM::LLVMPointerType>(t))
        return mlir::LLVM::DIDerivedTypeAttr::get(
            ctx, DW_TAG_pointer_type, mlir::StringAttr{}, mlir::LLVM::DITypeAttr{},
            /*sizeInBits=*/64, /*alignInBits=*/64, /*offsetInBits=*/0,
            /*dwarfAddressSpace=*/std::nullopt, /*extraData=*/mlir::LLVM::DINodeAttr{});
    // Aggregate (struct/array/...) → opaque, correctly sized.
    uint64_t bits = 0;
    {
        mlir::DataLayout dl(builder_.getBlock()->getParentOp()->getParentOfType<mlir::ModuleOp>());
        bits = dl.getTypeSizeInBits(t);
    }
    std::string nm;
    if (auto st = mlir::dyn_cast<mlir::LLVM::LLVMStructType>(t); st && st.isIdentified())
        nm = st.getName().str();
    return mlir::LLVM::DICompositeTypeAttr::get(
        ctx, DW_TAG_structure_type, mlir::StringAttr::get(ctx, nm), di_file_,
        /*line=*/0, /*scope=*/mlir::LLVM::DIScopeAttr{}, /*baseType=*/mlir::LLVM::DITypeAttr{},
        mlir::LLVM::DIFlags::Zero, /*sizeInBits=*/bits, /*alignInBits=*/0,
        /*elements=*/llvm::ArrayRef<mlir::LLVM::DINodeAttr>{},
        mlir::LLVM::DIExpressionAttr{}, mlir::LLVM::DIExpressionAttr{},
        mlir::LLVM::DIExpressionAttr{}, mlir::LLVM::DIExpressionAttr{});
}

// Struct DICompositeType with members + byte offsets, from the registered LLVM
// struct layout. Members' types come from di_leaf_from_mlir (recursion-free).
mlir::LLVM::DITypeAttr MLIRGenImpl::di_struct_type(std::string_view mlir_key,
                                                   std::string_view display_name) {
    auto* ctx = builder_.getContext();
    auto sit = struct_types_.find(std::string(mlir_key));
    if (sit == struct_types_.end())
        return mlir::LLVM::DICompositeTypeAttr::get(
            ctx, DW_TAG_structure_type, mlir::StringAttr::get(ctx, display_name), di_file_,
            0, mlir::LLVM::DIScopeAttr{}, mlir::LLVM::DITypeAttr{}, mlir::LLVM::DIFlags::Zero,
            0, 0, llvm::ArrayRef<mlir::LLVM::DINodeAttr>{}, mlir::LLVM::DIExpressionAttr{},
            mlir::LLVM::DIExpressionAttr{}, mlir::LLVM::DIExpressionAttr{}, mlir::LLVM::DIExpressionAttr{});

    mlir::DataLayout dl(builder_.getBlock()->getParentOp()->getParentOfType<mlir::ModuleOp>());
    llvm::SmallVector<mlir::LLVM::DINodeAttr> members;
    uint64_t off = 0;
    for (auto& f : sit->second.fields) {
        uint64_t fbits = dl.getTypeSizeInBits(f.type);
        uint64_t falign = abi_align_bytes(f.type);                     // bytes (x86-64)
        off = align_up(off, falign * 8);
        members.push_back(mlir::LLVM::DIDerivedTypeAttr::get(
            ctx, DW_TAG_member, mlir::StringAttr::get(ctx, f.name),
            di_leaf_from_mlir(f.type), fbits, /*alignInBits=*/(uint32_t)(falign * 8),
            /*offsetInBits=*/off, std::nullopt, mlir::LLVM::DINodeAttr{}));
        off += fbits;
    }
    uint64_t total = dl.getTypeSizeInBits(sit->second.llvm_type);
    return mlir::LLVM::DICompositeTypeAttr::get(
        ctx, DW_TAG_structure_type, mlir::StringAttr::get(ctx, display_name), di_file_,
        /*line=*/0, mlir::LLVM::DIScopeAttr{}, mlir::LLVM::DITypeAttr{}, mlir::LLVM::DIFlags::Zero,
        /*sizeInBits=*/total, /*alignInBits=*/0, members,
        mlir::LLVM::DIExpressionAttr{}, mlir::LLVM::DIExpressionAttr{},
        mlir::LLVM::DIExpressionAttr{}, mlir::LLVM::DIExpressionAttr{});
}

mlir::LLVM::DITypeAttr MLIRGenImpl::di_type(TypeRef t) {
    if (!t) return {};                  // void / null
    auto* ctx = builder_.getContext();
    auto off = t.offset();
    if (auto it = di_type_cache_.find(off); it != di_type_cache_.end()) return it->second;

    auto basic = [&](const char* nm, uint64_t bits, unsigned enc) {
        return mlir::LLVM::DIBasicTypeAttr::get(ctx, DW_TAG_base_type, nm, bits, enc);
    };
    auto ptr_to = [&](mlir::LLVM::DITypeAttr base) -> mlir::LLVM::DITypeAttr {
        return mlir::LLVM::DIDerivedTypeAttr::get(
            ctx, DW_TAG_pointer_type, mlir::StringAttr{}, base,
            64, 64, 0, std::nullopt, mlir::LLVM::DINodeAttr{});
    };

    mlir::LLVM::DITypeAttr result;
    using K = LogosType::Kind;
    switch (t.kind()) {
        case K::Void: return {};   // void return type — no DI node
        case K::I8:   result = basic("i8",   8,   DW_ATE_signed);   break;
        case K::I16:  result = basic("i16",  16,  DW_ATE_signed);   break;
        case K::I24:  result = basic("i24",  24,  DW_ATE_signed);   break;
        case K::I32:  result = basic("i32",  32,  DW_ATE_signed);   break;
        case K::I56:  result = basic("i56",  56,  DW_ATE_signed);   break;
        case K::I64:  result = basic("i64",  64,  DW_ATE_signed);   break;
        case K::I128: result = basic("i128", 128, DW_ATE_signed);   break;
        case K::Isize:result = basic("isize",64,  DW_ATE_signed);   break;
        case K::U8:   result = basic("u8",   8,   DW_ATE_unsigned); break;
        case K::U16:  result = basic("u16",  16,  DW_ATE_unsigned); break;
        case K::U24:  result = basic("u24",  24,  DW_ATE_unsigned); break;
        case K::U32:  result = basic("u32",  32,  DW_ATE_unsigned); break;
        case K::U56:  result = basic("u56",  56,  DW_ATE_unsigned); break;
        case K::U64:  result = basic("u64",  64,  DW_ATE_unsigned); break;
        case K::U128: result = basic("u128", 128, DW_ATE_unsigned); break;
        case K::Usize:result = basic("usize",64,  DW_ATE_unsigned); break;
        case K::Bool: result = basic("bool", 8,   DW_ATE_boolean);  break;
        case K::Char: result = basic("char", 32,  DW_ATE_UTF);      break;
        case K::F32:  result = basic("f32",  32,  DW_ATE_float);    break;
        case K::F64:  result = basic("f64",  64,  DW_ATE_float);    break;
        case K::IntLit:   result = basic("i64", 64, DW_ATE_signed); break;
        case K::FloatLit: result = basic("f64", 64, DW_ATE_float);  break;
        case K::Ptr: case K::Ref: case K::MutRef: {
            result = ptr_to(t.pointee() ? di_type(t.pointee()) : mlir::LLVM::DITypeAttr{});
            break;
        }
        case K::FnPtr: case K::Closure:
            result = ptr_to(mlir::LLVM::DITypeAttr{});
            break;
        case K::Struct: case K::ZonedStruct:
            result = di_struct_type(mlir_struct_key(t), type_str(t));
            break;
        default: {
            // Tuple/Array/Enum/Slice/TraitObject/… → opaque, correctly sized.
            std::unordered_set<std::string> seen;
            uint64_t bytes = logos_abi_byte_size(t, seen);
            result = mlir::LLVM::DICompositeTypeAttr::get(
                ctx, DW_TAG_structure_type, mlir::StringAttr::get(ctx, type_str(t)), di_file_,
                0, mlir::LLVM::DIScopeAttr{}, mlir::LLVM::DITypeAttr{}, mlir::LLVM::DIFlags::Zero,
                /*sizeInBits=*/bytes * 8, 0, llvm::ArrayRef<mlir::LLVM::DINodeAttr>{},
                mlir::LLVM::DIExpressionAttr{}, mlir::LLVM::DIExpressionAttr{},
                mlir::LLVM::DIExpressionAttr{}, mlir::LLVM::DIExpressionAttr{});
            break;
        }
    }
    if (result) di_type_cache_[off] = result;
    return result;
}

void MLIRGenImpl::emit_local_dbg_declare(std::string_view name, TypeRef ty,
                                         uint32_t line) {
    if (!debug_info_ || !di_subprogram_ || !ty) return;
    auto it = scope_.find(std::string(name));
    if (it == scope_.end() || !it->second) return;
    mlir::Value slot = it->second;
    // Slot must be an alloca (the variable's own storage). SSA-value bindings
    // (tuples/closures bound by value, ref aliases) are skipped — DbgDeclare on
    // them would mislead gdb. (DbgValue handling is future work.)
    auto alloca = slot.getDefiningOp<mlir::LLVM::AllocaOp>();
    if (!alloca) return;
    // Size guard: the alloca's element must be the same ABI size as the
    // variable's type. Rejects `let r = &s` (slot = s's alloca, sizes differ)
    // while accepting scalars, structs/enums/arrays/tuples, and mut-ref slots
    // (alloca-of-ptr, ptr-sized == &T size).
    std::unordered_set<std::string> seen;
    uint64_t ty_bytes = logos_abi_byte_size(ty, seen);
    mlir::DataLayout dl(builder_.getBlock()->getParentOp()->getParentOfType<mlir::ModuleOp>());
    uint64_t slot_bytes = dl.getTypeSize(alloca.getElemType());
    if (ty_bytes == 0 || ty_bytes != slot_bytes) return;

    auto dty = di_type(ty);
    if (!dty) return;
    auto* ctx = builder_.getContext();
    auto var = mlir::LLVM::DILocalVariableAttr::get(
        ctx, di_subprogram_, mlir::StringAttr::get(ctx, name), di_file_,
        line ? line : di_scope_line_, /*arg=*/0, /*alignInBits=*/0, dty,
        mlir::LLVM::DIFlags::Zero);
    builder_.create<mlir::LLVM::DbgDeclareOp>(dbg_loc(line), slot, var);
}

void MLIRGenImpl::emit_param_dbg_declare(std::string_view name, TypeRef ty,
                                         mlir::Value arg, unsigned arg_no,
                                         uint32_t line) {
    if (!debug_info_ || !di_subprogram_ || !ty || !arg) return;
    auto dty = di_type(ty);
    if (!dty) return;
    auto* ctx = builder_.getContext();
    auto var = mlir::LLVM::DILocalVariableAttr::get(
        ctx, di_subprogram_, mlir::StringAttr::get(ctx, name), di_file_,
        line ? line : di_scope_line_, /*arg=*/arg_no, /*alignInBits=*/0, dty,
        mlir::LLVM::DIFlags::Zero);
    auto loc = dbg_loc(line);
    // Aggregate (struct/enum/array/tuple) arriving as a pointer = the object's
    // address → DbgDeclare. Scalars / pointers / by-value aggregates → DbgValue.
    using K = LogosType::Kind;
    bool aggregate = false;
    switch (ty.kind()) {
        case K::Struct: case K::ZonedStruct: case K::Enum:
        case K::Array: case K::Tuple: aggregate = true; break;
        default: break;
    }
    if (aggregate && mlir::isa<mlir::LLVM::LLVMPointerType>(arg.getType()))
        builder_.create<mlir::LLVM::DbgDeclareOp>(loc, arg, var);
    else
        builder_.create<mlir::LLVM::DbgValueOp>(loc, arg, var);
}

mlir::LLVM::DISubroutineTypeAttr
MLIRGenImpl::di_subroutine_type(lir_view::FunctionView fn) {
    auto* ctx = builder_.getContext();
    const auto* pool = pool_impl();
    llvm::SmallVector<mlir::LLVM::DITypeAttr> types;
    // Index 0 = return type (null = void).
    TypeRef ret = fn.ret_type(pool);
    types.push_back(ret ? di_type(ret) : mlir::LLVM::DITypeAttr{});
    for (auto& p : fn.params())
        types.push_back(di_type(p.type(pool)));
    return mlir::LLVM::DISubroutineTypeAttr::get(ctx, /*callingConvention=*/0, types);
}

} // namespace logos::compiler
