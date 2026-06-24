// Logos project — https://github.com/victor-smirnov/logos
//
// mlir_gen_dyn.cpp — Vtable emission, &dyn Trait coercion, dyn dispatch, closures.

#include "mlir_gen_impl.hpp"
#include <algorithm>
#include <cstdio>
#include <map>
#include <set>

namespace logos::compiler {

using namespace lir;

// ---------------------------------------------------------------------------
// emit_tier2_lookup — binary-search lookup for tier-2 type codes.
//
// Emits a func.func @<lookup_name>(type_code: i64) -> ptr that performs
// binary search over the sorted @<codes_name> array and returns the
// corresponding entry from @<fns_name>, or null if not found.
// ---------------------------------------------------------------------------

static void emit_tier2_lookup(
    mlir::OpBuilder& builder, mlir::Location loc,
    mlir::ModuleOp mod,
    const std::string& lookup_name,
    const std::string& codes_name,
    const std::string& fns_name,
    mlir::LLVM::LLVMArrayType codes_arr_type,
    mlir::LLVM::LLVMArrayType fns_arr_type,
    int64_t n)
{
    auto* ctx  = builder.getContext();
    auto  ptr_t = mlir::LLVM::LLVMPointerType::get(ctx);
    auto  i64_t = builder.getI64Type();

    // func @lookup(type_code: i64) -> ptr
    auto fn_type = mlir::FunctionType::get(ctx, {i64_t}, {ptr_t});
    builder.setInsertionPointToEnd(mod.getBody());
    auto fn = builder.create<mlir::func::FuncOp>(loc, lookup_name, fn_type);
    fn->setAttr("sym_visibility", mlir::StringAttr::get(ctx, "private"));
    auto* entry_block = fn.addEntryBlock();
    builder.setInsertionPointToStart(entry_block);

    mlir::Value type_code = entry_block->getArgument(0);

    // Allocas for loop variables: lo, hi, mid.
    mlir::Value alloca_one = builder.create<mlir::arith::ConstantIntOp>(loc, 1LL, 64);
    auto lo_alloca  = builder.create<mlir::LLVM::AllocaOp>(loc, ptr_t, i64_t, alloca_one);
    auto hi_alloca  = builder.create<mlir::LLVM::AllocaOp>(loc, ptr_t, i64_t, alloca_one);
    auto mid_alloca = builder.create<mlir::LLVM::AllocaOp>(loc, ptr_t, i64_t, alloca_one);

    auto zero64   = builder.create<mlir::arith::ConstantIntOp>(loc, 0LL, 64);
    auto nminus1  = builder.create<mlir::arith::ConstantIntOp>(loc, n - 1, 64);
    builder.create<mlir::LLVM::StoreOp>(loc, zero64,  lo_alloca);
    builder.create<mlir::LLVM::StoreOp>(loc, nminus1, hi_alloca);

    // Addresses of the sorted codes and fns globals.
    auto codes_addr = builder.create<mlir::LLVM::AddressOfOp>(loc, ptr_t, codes_name);
    auto fns_addr   = builder.create<mlir::LLVM::AddressOfOp>(loc, ptr_t, fns_name);

    // Create CFG blocks.
    auto* region       = builder.getBlock()->getParent();
    auto* cond_block   = new mlir::Block();
    auto* body_block   = new mlir::Block();
    auto* neq_block    = new mlir::Block();
    auto* lo_upd_block = new mlir::Block();
    auto* hi_upd_block = new mlir::Block();
    auto* found_block  = new mlir::Block();
    auto* exit_block   = new mlir::Block();
    region->push_back(cond_block);
    region->push_back(body_block);
    region->push_back(neq_block);
    region->push_back(lo_upd_block);
    region->push_back(hi_upd_block);
    region->push_back(found_block);
    region->push_back(exit_block);

    // entry → cond
    builder.create<mlir::cf::BranchOp>(loc, cond_block);

    // cond_block: loop while lo <= hi (signed; indices are non-negative).
    builder.setInsertionPointToStart(cond_block);
    {
        auto lo  = builder.create<mlir::LLVM::LoadOp>(loc, i64_t, lo_alloca);
        auto hi  = builder.create<mlir::LLVM::LoadOp>(loc, i64_t, hi_alloca);
        auto cmp = builder.create<mlir::arith::CmpIOp>(
            loc, mlir::arith::CmpIPredicate::sle, lo, hi);
        builder.create<mlir::cf::CondBranchOp>(loc, cmp, body_block, exit_block);
    }

    // body_block: compute mid, load codes[mid], compare.
    builder.setInsertionPointToStart(body_block);
    {
        auto lo2  = builder.create<mlir::LLVM::LoadOp>(loc, i64_t, lo_alloca);
        auto hi2  = builder.create<mlir::LLVM::LoadOp>(loc, i64_t, hi_alloca);
        auto sum  = builder.create<mlir::arith::AddIOp>(loc, lo2, hi2);
        auto two  = builder.create<mlir::arith::ConstantIntOp>(loc, 2LL, 64);
        mlir::Value mid = builder.create<mlir::arith::DivSIOp>(loc, sum, two);
        builder.create<mlir::LLVM::StoreOp>(loc, mid, mid_alloca);

        llvm::SmallVector<mlir::LLVM::GEPArg> gep;
        gep.push_back(int32_t(0));
        gep.push_back(mid);  // dynamic index (mlir::Value)
        auto code_slot = builder.create<mlir::LLVM::GEPOp>(
            loc, ptr_t, codes_arr_type, codes_addr, gep);
        auto code_mid = builder.create<mlir::LLVM::LoadOp>(loc, i64_t, code_slot);

        auto eq = builder.create<mlir::arith::CmpIOp>(
            loc, mlir::arith::CmpIPredicate::eq, code_mid, type_code);
        builder.create<mlir::cf::CondBranchOp>(loc, eq, found_block, neq_block);
    }

    // neq_block: decide whether to go lo or hi.
    // Re-read codes[mid] to get code_mid (mid was stored to alloca by body_block).
    builder.setInsertionPointToStart(neq_block);
    {
        mlir::Value mid3 = builder.create<mlir::LLVM::LoadOp>(loc, i64_t, mid_alloca);
        llvm::SmallVector<mlir::LLVM::GEPArg> gep;
        gep.push_back(int32_t(0));
        gep.push_back(mid3);
        auto code_slot = builder.create<mlir::LLVM::GEPOp>(
            loc, ptr_t, codes_arr_type, codes_addr, gep);
        auto code_mid2 = builder.create<mlir::LLVM::LoadOp>(loc, i64_t, code_slot);
        // Use unsigned comparison: type_codes are logically u64.
        auto lt = builder.create<mlir::arith::CmpIOp>(
            loc, mlir::arith::CmpIPredicate::ult, code_mid2, type_code);
        builder.create<mlir::cf::CondBranchOp>(loc, lt, lo_upd_block, hi_upd_block);
    }

    // lo_upd_block: lo = mid + 1
    builder.setInsertionPointToStart(lo_upd_block);
    {
        auto mid4   = builder.create<mlir::LLVM::LoadOp>(loc, i64_t, mid_alloca);
        auto one    = builder.create<mlir::arith::ConstantIntOp>(loc, 1LL, 64);
        auto new_lo = builder.create<mlir::arith::AddIOp>(loc, mid4, one);
        builder.create<mlir::LLVM::StoreOp>(loc, new_lo, lo_alloca);
        builder.create<mlir::cf::BranchOp>(loc, cond_block);
    }

    // hi_upd_block: hi = mid - 1
    builder.setInsertionPointToStart(hi_upd_block);
    {
        auto mid5   = builder.create<mlir::LLVM::LoadOp>(loc, i64_t, mid_alloca);
        auto one    = builder.create<mlir::arith::ConstantIntOp>(loc, 1LL, 64);
        auto new_hi = builder.create<mlir::arith::SubIOp>(loc, mid5, one);
        builder.create<mlir::LLVM::StoreOp>(loc, new_hi, hi_alloca);
        builder.create<mlir::cf::BranchOp>(loc, cond_block);
    }

    // found_block: return fns[mid].
    builder.setInsertionPointToStart(found_block);
    {
        mlir::Value mid6 = builder.create<mlir::LLVM::LoadOp>(loc, i64_t, mid_alloca);
        llvm::SmallVector<mlir::LLVM::GEPArg> gep;
        gep.push_back(int32_t(0));
        gep.push_back(mid6);
        auto fn_slot = builder.create<mlir::LLVM::GEPOp>(
            loc, ptr_t, fns_arr_type, fns_addr, gep);
        auto fn_ptr = builder.create<mlir::LLVM::LoadOp>(loc, ptr_t, fn_slot);
        builder.create<mlir::func::ReturnOp>(loc, mlir::ValueRange{fn_ptr});
    }

    // exit_block: return null (type_code not found — programmer error).
    builder.setInsertionPointToStart(exit_block);
    {
        auto null_ptr = builder.create<mlir::LLVM::ZeroOp>(loc, ptr_t);
        builder.create<mlir::func::ReturnOp>(loc, mlir::ValueRange{null_ptr});
    }

    builder.setInsertionPointToEnd(mod.getBody());
}

// ---------------------------------------------------------------------------
// Trait vtable info (Pass 1b)
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Tag-dispatch tables (Pass 1c)
// ---------------------------------------------------------------------------
// Tier-1 (type_code 1-222): [223 x ptr] global array, O(1) direct index.
// Tier-2 (type_code 223+): sorted [N x i64] codes + [N x ptr] fns arrays;
//   a per-table binary-search lookup function (@__logos_tier2_*_lookup) is
//   emitted.  Both tiers are filled at program startup by the generated
//   @__logos_tag_dispatch_init function (injected at the top of main() in
//   mlir_gen.cpp Pass 3).
//
// Collision detection (Pass A):
//   For each (tag_system, trait, type_code) registration the compiler emits:
//     @__logos_tagdispatch_<ts>_<trait>_<XXXXXXXXXXXXXXXX> = global i8 1
//   with External linkage.  If two object files register the same tuple the
//   linker sees a multiply-defined symbol and aborts with an error.
//   The sentinel value (1) is arbitrary — only the symbol identity matters.

// Build the collision-detection symbol name.
// Format: __logos_tagdispatch_<ts>_<trait>_<type_code_hex16>
// '::' and other non-identifier chars in names are replaced with '_'.
static std::string collision_sym_name(const std::string& ts,
                                       const std::string& trait,
                                       uint64_t type_code)
{
    // Sanitize: replace chars invalid in LLVM symbol names.
    auto sanitize = [](const std::string& s) {
        std::string r; r.reserve(s.size());
        for (char c : s) r.push_back((c == ':' || c == '<' || c == '>') ? '_' : c);
        return r;
    };
    char hex[17];
    std::snprintf(hex, sizeof(hex), "%016llx", (unsigned long long)type_code);
    // Use "__" (double underscore) as field separator so names that contain a
    // single underscore (e.g. "My_System" or "My_Trait") do not produce the
    // same symbol as a different (ts, trait) combination.
    return "__logos_tagdispatch__" + sanitize(ts) + "__" + sanitize(trait) + "__" + hex;
}

void MLIRGenImpl::emit_tag_dispatch_tables(mlir::ModuleOp mod, const LProgram& prog) {
    if (prog.dispatch_entries.empty()) return;

    // Compute which tag systems are fully covered by the binary archive.
    // For those systems we emit only external declarations (so gen_tagged_dispatch
    // can reference the tables) but no initialiser/body — the archive provides them.
    std::set<std::string> binary_tag_systems;
    if (!prog.binary_symbols.empty()) {
        std::map<std::string, bool> sys_all_binary;
        for (auto& de : prog.dispatch_entries) {
            std::string ts(de.tag_system());
            if (ts.empty() || de.type_code() == 0) continue;
            auto it = sys_all_binary.find(ts);
            if (it == sys_all_binary.end())
                sys_all_binary[ts] = true;
            if (!prog.binary_symbols.has(link_name_str(std::string(de.fn_symbol()))))
                sys_all_binary[ts] = false;
        }
        for (auto& [sys, all_bin] : sys_all_binary)
            if (all_bin) binary_tag_systems.insert(sys);
    }

    auto ptr_t = ptr_type();
    constexpr int kTier1Size = 256;
    auto arr_type_t1 = mlir::LLVM::LLVMArrayType::get(ptr_t, kTier1Size);

    // For binary tag systems emit External declarations so gen_tagged_dispatch
    // can emit AddressOf / call — the linker resolves them to the archive.
    if (!binary_tag_systems.empty()) {
        auto i64_t = builder_.getI64Type();
        std::set<std::string> decl_emitted;
        for (auto& de : prog.dispatch_entries) {
            std::string ts(de.tag_system());
            if (!binary_tag_systems.count(ts)) continue;
            if (de.type_code() == 0) continue;
            auto base = ts + "__" + std::string(de.trait_name()) + "__" + std::string(de.method_name());
            if (de.type_code() < static_cast<uint64_t>(kTier1Size)) {
                auto tname = "__logos_tag_dispatch__" + base;
                if (decl_emitted.insert(tname).second && !mod.lookupSymbol(tname)) {
                    builder_.setInsertionPointToEnd(mod.getBody());
                    builder_.create<mlir::LLVM::GlobalOp>(
                        loc_, arr_type_t1, false, mlir::LLVM::Linkage::External,
                        tname, mlir::Attribute{}, 0);
                }
            } else {
                auto t2key = "__logos_tier2__" + base;
                // forward-declare _codes, _fns as external (size unknown here, use i8 array of 0)
                for (auto& suffix : {std::string("_codes"), std::string("_fns")}) {
                    auto gname = t2key + suffix;
                    if (decl_emitted.insert(gname).second && !mod.lookupSymbol(gname)) {
                        builder_.setInsertionPointToEnd(mod.getBody());
                        auto arr0 = mlir::LLVM::LLVMArrayType::get(i64_t, 0);
                        builder_.create<mlir::LLVM::GlobalOp>(
                            loc_, arr0, false, mlir::LLVM::Linkage::External,
                            gname, mlir::Attribute{}, 0);
                    }
                }
                // forward-declare _lookup function
                auto lkp_name = t2key + "_lookup";
                if (decl_emitted.insert(lkp_name).second && !mod.lookupSymbol(lkp_name)) {
                    builder_.setInsertionPointToEnd(mod.getBody());
                    auto fn_type = builder_.getFunctionType(
                        {builder_.getI64Type()}, {ptr_t});
                    auto decl = builder_.create<mlir::func::FuncOp>(loc_, lkp_name, fn_type);
                    decl.setPrivate();
                }
            }
        }
    }

    // If every tag system is binary, declarations are enough — no definitions.
    if (!binary_tag_systems.empty()) {
        bool all_binary = true;
        for (auto& de : prog.dispatch_entries)
            if (!de.tag_system().empty() && !binary_tag_systems.count(std::string(de.tag_system())))
                { all_binary = false; break; }
        if (all_binary) return;
    }
    auto i64_t = builder_.getI64Type();

    struct Entry { uint64_t type_code; std::string fn_symbol; };
    std::map<std::string, std::vector<Entry>> tier1_tables;
    std::map<std::string, std::vector<Entry>> tier2_tables;
    // Bug fix: store valid tier-2 entries for Pass D to avoid size mismatch.
    std::map<std::string, std::vector<Entry>> tier2_valid_entries;

    for (auto& de : prog.dispatch_entries) {
        std::string ts(de.tag_system());
        // Skip dispatch entries from fully-binary tag systems.
        if (binary_tag_systems.count(ts)) continue;
        // type_code == 0 is unset (no impl registered yet); skip.
        // Codes 1-127 are valid for inline AnyVal slots; 128-255 for tier-1 zone types.
        if (de.type_code() == 0) continue;
        // Use "__" as separator to avoid ambiguity when tag_system or trait_name
        // contains a single underscore (e.g. "My_System__Trait__method" is
        // unambiguous; "My_System_Trait_method" is not).
        auto base = ts + "__" + std::string(de.trait_name()) + "__" + std::string(de.method_name());
        // Module system: dispatch entry stores a bare-module method symbol;
        // qualify it to the emitted link name so the table references resolve.
        auto fsym = link_name_str(std::string(de.fn_symbol()));
        if (de.type_code() < static_cast<uint64_t>(kTier1Size)) {
            tier1_tables["__logos_tag_dispatch__" + base].push_back({de.type_code(), fsym});
        } else {
            tier2_tables["__logos_tier2__" + base].push_back({de.type_code(), fsym});
        }
    }
    if (tier1_tables.empty() && tier2_tables.empty()) return;

    // ── Pass A: emit link-time collision detection sentinels ──────────────
    // One i8 global per unique (tag_system, trait_name, type_code) triple.
    // Deduplication: use a set to avoid emitting twice when a trait has
    // multiple methods (each method produces one LDispatchEntry for the same
    // type_code, but we only need one sentinel per type registration).
    {
        auto i8_t = builder_.getIntegerType(8);
        auto one_attr = mlir::IntegerAttr::get(i8_t, 1);
        std::set<std::string> emitted;
        for (auto& de : prog.dispatch_entries) {
            if (de.type_code() == 0) continue;
            if (binary_tag_systems.count(std::string(de.tag_system()))) continue;
            auto sym = collision_sym_name(std::string(de.tag_system()),
                                          std::string(de.trait_name()), de.type_code());
            if (!emitted.insert(sym).second) continue;    // already emitted this triple
            // Type-check the existing symbol: only skip if it is already the
            // expected i8 sentinel.  Any other symbol with the same name
            // (e.g. a user function) is a naming conflict — warn and skip.
            if (auto existing = mod.lookupSymbol(sym)) {
                if (!llvm::isa<mlir::LLVM::GlobalOp>(existing))
                    std::fprintf(stderr,
                        "warning: collision-sentinel name '%s' shadowed by "
                        "a non-global symbol; dispatch collision detection "
                        "for type_code 0x%llx may be suppressed\n",
                        sym.c_str(), (unsigned long long)de.type_code());
                continue;
            }
            builder_.setInsertionPointToEnd(mod.getBody());
            builder_.create<mlir::LLVM::GlobalOp>(
                loc_, i8_t, /*isConstant=*/true,
                mlir::LLVM::Linkage::External, sym, one_attr);
        }
    }

    // ── Pass B: emit zero-initialized [223 x ptr] globals for tier-1 ─────
    // Use Weak linkage when the table already exists in a binary archive so the
    // linker deduplicates rather than flagging a duplicate-definition error.
    for (auto& [table_name, _entries] : tier1_tables) {
        if (mod.lookupSymbol(table_name)) continue;
        bool in_binary = prog.binary_symbols.has(table_name) > 0;
        auto linkage = in_binary ? mlir::LLVM::Linkage::Weak
                                 : mlir::LLVM::Linkage::External;
        builder_.setInsertionPointToEnd(mod.getBody());
        auto glob = builder_.create<mlir::LLVM::GlobalOp>(
            loc_, arr_type_t1, false, linkage, table_name, mlir::Attribute{}, 0);
        auto& ir = glob.getInitializerRegion();
        builder_.setInsertionPointToStart(builder_.createBlock(&ir));
        builder_.create<mlir::LLVM::ReturnOp>(
            loc_, builder_.create<mlir::LLVM::ZeroOp>(loc_, arr_type_t1));
        builder_.setInsertionPointToEnd(mod.getBody());
    }

    // ── Pass C: emit tier-2 data globals + binary-search lookup functions ─
    for (auto& [t2key, entries] : tier2_tables) {
        // Bug fix: filter entries to only include those with valid callees.
        // This avoids creating sparse arrays with zeros that break binary search.
        std::vector<Entry> valid_entries;
        for (auto& e : entries) {
            if (mod.lookupSymbol<mlir::func::FuncOp>(e.fn_symbol)) {
                valid_entries.push_back(e);
            }
        }
        if (valid_entries.empty()) continue;  // skip tier-2 table if no valid entries
        std::sort(valid_entries.begin(), valid_entries.end(),
                  [](const Entry& a, const Entry& b){ return a.type_code < b.type_code; });

        // Bug fix: store valid_entries for Pass D to use.
        tier2_valid_entries[t2key] = valid_entries;
        int64_t n = static_cast<int64_t>(valid_entries.size());

        bool t2_in_binary = prog.binary_symbols.has(t2key + "_codes") > 0;
        auto t2_linkage = t2_in_binary ? mlir::LLVM::Linkage::Weak
                                       : mlir::LLVM::Linkage::External;

        auto codes_arr_type = mlir::LLVM::LLVMArrayType::get(i64_t, n);
        auto codes_name = t2key + "_codes";
        if (!mod.lookupSymbol(codes_name)) {
            builder_.setInsertionPointToEnd(mod.getBody());
            auto g = builder_.create<mlir::LLVM::GlobalOp>(
                loc_, codes_arr_type, false,
                t2_linkage, codes_name, mlir::Attribute{}, 0);
            auto& ir = g.getInitializerRegion();
            builder_.setInsertionPointToStart(builder_.createBlock(&ir));
            builder_.create<mlir::LLVM::ReturnOp>(
                loc_, builder_.create<mlir::LLVM::ZeroOp>(loc_, codes_arr_type));
            builder_.setInsertionPointToEnd(mod.getBody());
        }

        auto fns_arr_type = mlir::LLVM::LLVMArrayType::get(ptr_t, n);
        auto fns_name = t2key + "_fns";
        if (!mod.lookupSymbol(fns_name)) {
            builder_.setInsertionPointToEnd(mod.getBody());
            auto g = builder_.create<mlir::LLVM::GlobalOp>(
                loc_, fns_arr_type, false,
                t2_linkage, fns_name, mlir::Attribute{}, 0);
            auto& ir = g.getInitializerRegion();
            builder_.setInsertionPointToStart(builder_.createBlock(&ir));
            builder_.create<mlir::LLVM::ReturnOp>(
                loc_, builder_.create<mlir::LLVM::ZeroOp>(loc_, fns_arr_type));
            builder_.setInsertionPointToEnd(mod.getBody());
        }

        auto lookup_name = t2key + "_lookup";
        if (!mod.lookupSymbol<mlir::func::FuncOp>(lookup_name)) {
            emit_tier2_lookup(builder_, loc_, mod,
                              lookup_name, codes_name, fns_name,
                              codes_arr_type, fns_arr_type, n);
        }
    }

    // ── Pass D: emit per-tag-system __logos_tag_dispatch_init__<TagSystem> ──
    // One init function per non-binary tag system avoids symbol clashes when
    // the stdlib archive already provides its own init function.
    // Collect tag systems that have tables to initialize.
    std::set<std::string> active_systems;
    for (auto& [tname, _] : tier1_tables) {
        // table_name is "__logos_tag_dispatch__<sys>__<trait>__<method>"
        // extract sys: strip prefix, then take up to second "__"
        std::string_view sv(tname);
        sv.remove_prefix(sizeof("__logos_tag_dispatch__") - 1);
        auto p = sv.find("__");
        if (p != std::string_view::npos)
            active_systems.insert(std::string(sv.substr(0, p)));
    }
    for (auto& [tname, _] : tier2_valid_entries) {
        std::string_view sv(tname);
        sv.remove_prefix(sizeof("__logos_tier2__") - 1);
        auto p = sv.find("__");
        if (p != std::string_view::npos)
            active_systems.insert(std::string(sv.substr(0, p)));
    }

    for (const auto& sys : active_systems) {
        auto ctor_name = "__logos_tag_dispatch_init__" + sys;
        if (mod.lookupSymbol<mlir::func::FuncOp>(ctor_name)) continue;
        auto ctor_type = mlir::FunctionType::get(builder_.getContext(), {}, {});
        builder_.setInsertionPointToEnd(mod.getBody());
        auto ctor_fn = builder_.create<mlir::func::FuncOp>(loc_, ctor_name, ctor_type);
        ctor_fn->setAttr("sym_visibility",
                         mlir::StringAttr::get(builder_.getContext(), "private"));
        auto* ctor_block = ctor_fn.addEntryBlock();
        builder_.setInsertionPointToStart(ctor_block);

        // Fill tier-1 tables for this tag system.
        for (auto& [table_name, entries] : tier1_tables) {
            std::string_view sv(table_name);
            sv.remove_prefix(sizeof("__logos_tag_dispatch__") - 1);
            auto p = sv.find("__");
            if (p == std::string_view::npos || sv.substr(0, p) != sys) continue;
            auto table_addr = builder_.create<mlir::LLVM::AddressOfOp>(
                loc_, ptr_t, table_name);
            for (auto& e : entries) {
                auto callee = mod.lookupSymbol<mlir::func::FuncOp>(e.fn_symbol);
                if (!callee) continue;
                auto fn_ref = builder_.create<mlir::func::ConstantOp>(
                    loc_, callee.getFunctionType(), e.fn_symbol);
                auto fn_ptr = builder_.create<mlir::UnrealizedConversionCastOp>(
                    loc_, ptr_t, mlir::ValueRange{fn_ref}).getResult(0);
                mlir::Value idx = builder_.create<mlir::arith::ConstantIntOp>(
                    loc_, static_cast<int64_t>(e.type_code), 64);
                llvm::SmallVector<mlir::LLVM::GEPArg> gi;
                gi.push_back(int32_t(0));
                gi.push_back(idx);
                auto slot = builder_.create<mlir::LLVM::GEPOp>(
                    loc_, ptr_t, arr_type_t1, table_addr, gi);
                builder_.create<mlir::LLVM::StoreOp>(loc_, fn_ptr, slot);
            }
        }

        // Fill tier-2 for this tag system.
        for (auto& [t2key, entries] : tier2_valid_entries) {
            std::string_view sv(t2key);
            sv.remove_prefix(sizeof("__logos_tier2__") - 1);
            auto p = sv.find("__");
            if (p == std::string_view::npos || sv.substr(0, p) != sys) continue;
            int64_t n = static_cast<int64_t>(entries.size());
            auto codes_arr_type = mlir::LLVM::LLVMArrayType::get(i64_t, n);
            auto fns_arr_type   = mlir::LLVM::LLVMArrayType::get(ptr_t, n);
            auto codes_addr = builder_.create<mlir::LLVM::AddressOfOp>(
                loc_, ptr_t, t2key + "_codes");
            auto fns_addr = builder_.create<mlir::LLVM::AddressOfOp>(
                loc_, ptr_t, t2key + "_fns");
            for (int64_t i = 0; i < n; ++i) {
                auto callee = mod.lookupSymbol<mlir::func::FuncOp>(entries[i].fn_symbol);
                if (!callee) continue;
                mlir::Value idx = builder_.create<mlir::arith::ConstantIntOp>(loc_, i, 64);
                llvm::SmallVector<mlir::LLVM::GEPArg> gi;
                gi.push_back(int32_t(0));
                gi.push_back(idx);
                mlir::Value code_val = builder_.create<mlir::arith::ConstantIntOp>(
                    loc_, static_cast<int64_t>(entries[i].type_code), 64);
                auto code_slot = builder_.create<mlir::LLVM::GEPOp>(
                    loc_, ptr_t, codes_arr_type, codes_addr, gi);
                builder_.create<mlir::LLVM::StoreOp>(loc_, code_val, code_slot);
                auto fn_ref = builder_.create<mlir::func::ConstantOp>(
                    loc_, callee.getFunctionType(), entries[i].fn_symbol);
                auto fn_ptr = builder_.create<mlir::UnrealizedConversionCastOp>(
                    loc_, ptr_t, mlir::ValueRange{fn_ref}).getResult(0);
                auto fn_slot = builder_.create<mlir::LLVM::GEPOp>(
                    loc_, ptr_t, fns_arr_type, fns_addr, gi);
                builder_.create<mlir::LLVM::StoreOp>(loc_, fn_ptr, fn_slot);
            }
        }

        builder_.create<mlir::func::ReturnOp>(loc_);
        builder_.setInsertionPointToEnd(mod.getBody());
    }

    // Note: the constructor is a func.func, not a llvm.func. We cannot use
    // llvm.mlir.global_ctors here because its verifier requires llvm.func
    // symbols (which only exist after ConvertFuncToLLVM runs, after codegen).
    // Instead, mlir_gen.cpp injects a call to @__logos_tag_dispatch_init at
    // the start of main() after all function bodies are generated (Pass 3).

    // ── Pass E: emit public lookup wrappers for registry access ──────────
    // For each (TagSystem, trait, method) triple, generate:
    //   fn __logos_dispatch_lookup__<TS>__<trait>__<method>(code: u64) -> *const u8
    //
    // The wrapper checks tier-1 (if present) and falls back to tier-2 (if
    // present). Returns null when neither table has an entry. stdlib can
    // call this to expose `hermes_fn_<trait>_<method>(code)` as a registry API
    // for reflection / deferred invocation.
    {
        std::set<std::string> all_bases;
        for (auto& [tkey, _] : tier1_tables) {
            // tkey = "__logos_tag_dispatch__<base>"
            all_bases.insert(tkey.substr(std::string("__logos_tag_dispatch__").size()));
        }
        for (auto& [tkey, _] : tier2_valid_entries) {
            // tkey = "__logos_tier2__<base>"
            all_bases.insert(tkey.substr(std::string("__logos_tier2__").size()));
        }

        auto lookup_ret_type = ptr_t;
        auto lookup_fn_type = mlir::FunctionType::get(
            builder_.getContext(), {i64_t}, {lookup_ret_type});

        for (auto& base : all_bases) {
            auto lookup_sym = "__logos_dispatch_lookup__" + base;
            bool has_t1 = tier1_tables.count("__logos_tag_dispatch__" + base) > 0;
            bool has_t2 = tier2_valid_entries.count("__logos_tier2__" + base) > 0;
            if (!has_t1 && !has_t2) continue;

            // If stdlib declared this symbol `extern fn` (empty-body FuncOp),
            // reuse the existing declaration so the define below satisfies the
            // linker reference. Otherwise create a fresh FuncOp.
            mlir::func::FuncOp fn;
            if (auto existing = mod.lookupSymbol<mlir::func::FuncOp>(lookup_sym)) {
                if (!existing.getBody().empty()) continue;  // already defined
                fn = existing;
            } else {
                builder_.setInsertionPointToEnd(mod.getBody());
                fn = builder_.create<mlir::func::FuncOp>(
                    loc_, lookup_sym, lookup_fn_type);
            }
            // Public (default visibility) — stdlib and user code can call it.
            auto* entry = fn.addEntryBlock();
            builder_.setInsertionPointToStart(entry);
            auto code = entry->getArgument(0);

            auto null_ptr = builder_.create<mlir::LLVM::ZeroOp>(loc_, ptr_t);

            auto gen_tier1_load = [&](mlir::Value code_val) -> mlir::Value {
                auto table_addr = builder_.create<mlir::LLVM::AddressOfOp>(
                    loc_, ptr_t, "__logos_tag_dispatch__" + base);
                llvm::SmallVector<mlir::LLVM::GEPArg> gi;
                gi.push_back(int32_t(0));
                gi.push_back(code_val);
                auto slot = builder_.create<mlir::LLVM::GEPOp>(
                    loc_, ptr_t, arr_type_t1, table_addr, gi);
                return builder_.create<mlir::LLVM::LoadOp>(loc_, ptr_t, slot);
            };
            auto gen_tier2_call = [&](mlir::Value code_val) -> mlir::Value {
                auto t2lkp = mod.lookupSymbol<mlir::func::FuncOp>(
                    "__logos_tier2__" + base + "_lookup");
                if (!t2lkp) return null_ptr;
                return builder_.create<mlir::func::CallOp>(
                    loc_, t2lkp, mlir::ValueRange{code_val}).getResult(0);
            };

            if (has_t1 && !has_t2) {
                // Tier-1 only: direct load. Out-of-range code would read past
                // the table; branch to guard it.
                auto bound = builder_.create<mlir::arith::ConstantIntOp>(
                    loc_, static_cast<int64_t>(kTier1Size), 64);
                auto in_range = builder_.create<mlir::arith::CmpIOp>(
                    loc_, mlir::arith::CmpIPredicate::ult, code, bound);
                auto* hit = new mlir::Block();
                auto* miss = new mlir::Block();
                fn.getBody().push_back(hit);
                fn.getBody().push_back(miss);
                builder_.create<mlir::cf::CondBranchOp>(loc_, in_range, hit, miss);

                builder_.setInsertionPointToStart(hit);
                auto fn_ptr = gen_tier1_load(code);
                builder_.create<mlir::func::ReturnOp>(loc_, mlir::ValueRange{fn_ptr});

                builder_.setInsertionPointToStart(miss);
                builder_.create<mlir::func::ReturnOp>(loc_, mlir::ValueRange{null_ptr});
            } else if (!has_t1 && has_t2) {
                auto fn_ptr = gen_tier2_call(code);
                builder_.create<mlir::func::ReturnOp>(loc_, mlir::ValueRange{fn_ptr});
            } else {
                // Both tiers: branch on code < kTier1Size.
                auto bound = builder_.create<mlir::arith::ConstantIntOp>(
                    loc_, static_cast<int64_t>(kTier1Size), 64);
                auto in_t1 = builder_.create<mlir::arith::CmpIOp>(
                    loc_, mlir::arith::CmpIPredicate::ult, code, bound);
                auto* t1b = new mlir::Block();
                auto* t2b = new mlir::Block();
                fn.getBody().push_back(t1b);
                fn.getBody().push_back(t2b);
                builder_.create<mlir::cf::CondBranchOp>(loc_, in_t1, t1b, t2b);

                builder_.setInsertionPointToStart(t1b);
                auto fn_ptr1 = gen_tier1_load(code);
                builder_.create<mlir::func::ReturnOp>(loc_, mlir::ValueRange{fn_ptr1});

                builder_.setInsertionPointToStart(t2b);
                auto fn_ptr2 = gen_tier2_call(code);
                builder_.create<mlir::func::ReturnOp>(loc_, mlir::ValueRange{fn_ptr2});
            }
            builder_.setInsertionPointToEnd(mod.getBody());
        }
    }
}

// ---------------------------------------------------------------------------
// §6.2 statics (S25): real global storage.
//
// Each `static [mut] NAME: T = expr;` becomes ONE llvm.mlir.global keyed by
// its link symbol. The global is zero-initialised; a @__logos_static_init
// function runs each initializer expr into the global's address at program
// startup (call injected into main's prologue in mlir_gen.cpp Pass 3). This
// gives every static a stable address and correct cross-fn read/write — the
// fix for the S25 segfault / read-before-write garbage where statics were
// const-inlined into a fresh per-use alloca. Extern-block decls (no value)
// emit an External declaration only.
// ---------------------------------------------------------------------------
void MLIRGenImpl::emit_static_globals(mlir::ModuleOp mod, const LProgram& prog) {
    bool any = false;
    for (auto& c : prog.consts) if (c.is_static()) { any = true; break; }
    if (!any) return;

    // A LIBRARY build (`--emit-module`, no `main`) does NOT own its statics'
    // storage or runtime initialization — only the final executable does. The
    // exe that links this archive re-emits + initializes every transitively-used
    // static (it has them in its prog.consts, lowered from the imported
    // .hermes0) inside ITS own `__logos_static_init`, called from `main`. If a
    // library ALSO emitted defined globals + its own `__logos_static_init`, the
    // two collide under the linker's `--allow-multiple-definition`: two
    // `__logos_static_init` symbols and two definitions of each static — the
    // linker resolves them pathologically and `main`'s init call lands inside a
    // data symbol → SIGSEGV (cross-module `pub static` crash). So a library
    // emits EXTERNAL declarations only and no initializer; the exe provides both.
    bool is_library = true;
    for (auto& f : prog.functions)
        if (f && f.name() == "main") { is_library = false; break; }

    auto set_end = [&]{ builder_.setInsertionPointToEnd(mod.getBody()); };

    // Pass A: emit the globals (skip if already present — stdlib precompiled +
    // current TU can both see a `pub static`).
    for (auto& c : prog.consts) {
        if (!c.is_static()) continue;
        std::string c_sym(c.sym());
        if (mod.lookupSymbol(c_sym)) continue;
        auto llty = logos_to_mlir(c.type(pool_impl()));
        if (!llty) llty = builder_.getI32Type();
        set_end();
        if (c.is_extern() || is_library) {
            // External declaration — defined in another object (FFI, or — for a
            // library build — the final executable that links it).
            builder_.create<mlir::LLVM::GlobalOp>(
                loc_, llty, /*isConstant=*/false, mlir::LLVM::Linkage::External,
                c_sym, mlir::Attribute{}, /*alignment=*/0);
            continue;
        }
        // Defined here: zero-initialised, runtime-filled at startup. NOT marked
        // isConstant even for immutable `static` — we store the initializer at
        // startup (a constant global can't be written); immutability is
        // enforced at sema (write to non-mut static rejected).
        auto g = builder_.create<mlir::LLVM::GlobalOp>(
            loc_, llty, /*isConstant=*/false, mlir::LLVM::Linkage::External,
            c_sym, mlir::Attribute{}, /*alignment=*/0);
        auto& region = g.getInitializerRegion();
        auto* blk = builder_.createBlock(&region);
        builder_.setInsertionPointToStart(blk);
        auto zero = builder_.create<mlir::LLVM::ZeroOp>(loc_, llty);
        builder_.create<mlir::LLVM::ReturnOp>(loc_, mlir::ValueRange{zero});
    }

    // Pass B: @__logos_static_init runs every defined static's initializer into
    // its global address. A library build emits no initializer (see above) —
    // the final executable owns it.
    if (is_library) return;
    std::vector<lir_view::ConstView> with_init;
    for (auto& c : prog.consts)
        if (c.is_static() && !c.is_extern() && c.value()) with_init.push_back(c);
    if (with_init.empty()) return;

    auto void_fn_type = mlir::FunctionType::get(builder_.getContext(), {}, {});
    set_end();
    auto init_fn = builder_.create<mlir::func::FuncOp>(
        loc_, "__logos_static_init", void_fn_type);
    init_fn.setPrivate();
    auto* entry = init_fn.addEntryBlock();
    builder_.setInsertionPointToStart(entry);
    auto save_fn = cur_fn_name_;
    cur_fn_name_ = "__logos_static_init";
    for (auto& c : with_init) {
        auto val = gen_expr(c.value());
        if (!val) continue;
        std::string c_sym(c.sym());
        TypeRef c_type = c.type(pool_impl());
        auto addr = builder_.create<mlir::LLVM::AddressOfOp>(loc_, ptr_type(), c_sym);
        TypeRef vt(c_type);
        auto vk = vt.kind();
        bool aggregate = vk == LogosType::Kind::Struct ||
                         vk == LogosType::Kind::ZonedStruct ||
                         vk == LogosType::Kind::Tuple ||
                         vk == LogosType::Kind::Array ||
                         vk == LogosType::Kind::Slice ||
                         vk == LogosType::Kind::Closure ||
                         (vk == LogosType::Kind::Enum &&
                          resolve_tagged_enum(std::string(vt.enum_name()), c_type));
        if (aggregate && val.getType() == ptr_type()) {
            builder_.create<mlir::LLVM::MemcpyOp>(loc_, addr, val,
                                                  size_const(c_type),
                                                  /*isVolatile=*/false);
        } else {
            builder_.create<mlir::LLVM::StoreOp>(loc_, val, addr);
        }
    }
    builder_.create<mlir::func::ReturnOp>(loc_);
    cur_fn_name_ = save_fn;
    has_static_init_ = true;
}


void MLIRGenImpl::emit_trait_vtables(mlir::ModuleOp /*mod*/, const LProgram& prog) {
    // Build a method_base → vector<const LFunction*> index once, so the
    // per-(trait, impl, method) `resolve_methods` lookup below doesn't walk
    // prog.functions linearly. Without the index, the loop is quadratic over
    // (n_impls × n_methods × n_functions); for stdlib that's ~50ms per
    // mlir_gen invocation.
    std::unordered_map<std::string, std::vector<lir_view::FunctionView>>
        method_base_idx;
    method_base_idx.reserve(256);
    for (auto& fp : prog.functions)
        if (fp && !fp.method_base().empty())
            method_base_idx[std::string(fp.method_base())].push_back(fp);
    // Also build a per-struct method_base index (used as the last fallback).
    std::unordered_map<std::string,
        std::unordered_map<std::string, std::vector<lir_view::FunctionView>>>
        struct_method_idx;
    for (auto& sd : prog.structs) {
        auto& sm = struct_method_idx[std::string(sd.name())];
        for (auto& mp : sd.methods())
            if (mp && !mp.method_base().empty())
                sm[std::string(mp.method_base())].push_back(mp);
    }

    // Pre-walk all fns/methods once to build `target_base → set<concrete>`
    // index. Each name `[pkg.]<base>$G<N>$<args>__method[__fg__sig]` carries
    // the concrete struct mangling we want grouped under its template's
    // `<base>` key.
    std::unordered_map<std::string, std::set<std::string>>
        concrete_targets_by_base;
    {
        auto scan = [&](std::string_view name) {
            if (auto dot = name.rfind('.'); dot != std::string_view::npos)
                name = name.substr(dot + 1);
            auto g_pos = name.find("$G");
            if (g_pos == std::string_view::npos) return;
            auto end = name.find("__", g_pos);
            if (end == std::string_view::npos) return;
            std::string base{name.substr(0, g_pos)};
            std::string concrete{name.substr(0, end)};
            concrete_targets_by_base[std::move(base)].insert(std::move(concrete));
        };
        for (auto& fp : prog.functions) if (fp) scan(fp.name());
        for (auto& sd : prog.structs)
            for (auto& mp : sd.methods()) if (mp) scan(mp.name());
    }
    auto collect_concrete_targets = [&](const std::string& target_base)
        -> const std::set<std::string>& {
        static const std::set<std::string> empty;
        auto it = concrete_targets_by_base.find(target_base);
        return it == concrete_targets_by_base.end() ? empty : it->second;
    };

    // Record each trait's method names (vtable slot order) and whether a
    // blanket impl provides it — so build_inline_vtable can synthesize a
    // `<Concrete>__<method>` vtable for types that reach `&dyn Trait` only
    // through the blanket (the blanket impl block registers the typevar
    // target, not each concrete instantiation).
    for (auto& td : prog.traits) {
        std::string tname(td.name());
        auto& mn = trait_method_names_[tname];
        mn.clear();
        // Full supertrait-closure slot order (sema single-sourced this in
        // vtable_method_order); supertrait methods get real slots so they are
        // dispatchable through `&dyn Sub`. Falls back to own methods for a
        // trait with no supertraits (identical to the old behaviour).
        auto vmo = td.vtable_method_order();
        if (!vmo.empty())
            for (auto& [owner, mname] : vmo) mn.push_back(std::string(mname));
        else
            td.each_method([&](lir_view::TraitMethodSigView m) { mn.push_back(std::string(m.name())); });
        {
            auto& us = trait_upcast_supers_[tname];
            us.clear();
            for (auto sv : td.upcast_supertraits()) us.push_back(std::string(sv));
        }
        for (auto& ib : prog.impls)
            if (ib.trait_name() == tname && ib.is_blanket()) {
                blanket_traits_.insert(tname);
                break;
            }
    }

    for (auto& td : prog.traits) {
        std::string td_name(td.name());
        for (auto& ib : prog.impls) {
            if (ib.trait_name() != td_name) continue;

            // Resolve method-symbol given a TARGET (bare or concrete).
            //
            // Match strategy:
            //   1. fn.method_base == trait method name (exact, no string
            //      manipulation of the mangled symbol)
            //   2. fn.name belongs to `target` — strip pkg prefix, then
            //      check the symbol starts with `target__` (bare) or
            //      `concrete__` (post-mono concrete clone).
            //
            // Replaces the old try_match heuristic that walked mangling
            // suffixes with strncmp; method_base is set by sema's
            // lower_fn (raw_name from AST) and propagated by
            // mono_clone's clone_fn.
            auto resolve_methods = [&](std::string_view target) -> std::vector<std::string> {
                auto belongs_to_target = [&](std::string_view nm) -> bool {
                    if (auto dot = nm.rfind('.'); dot != std::string_view::npos)
                        nm = nm.substr(dot + 1);
                    if (nm.size() < target.size() + 2) return false;
                    if (nm.compare(0, target.size(), target) != 0) return false;
                    return nm[target.size()] == '_' && nm[target.size() + 1] == '_';
                };
                std::vector<std::string> methods;
                // Resolve symbols in the full supertrait-closure slot order so
                // a supertrait method (provided by `impl Super for Concrete`)
                // gets its slot; falls back to own methods when no supertraits.
                std::vector<std::string> slot_names;
                auto vmo2 = td.vtable_method_order();
                if (!vmo2.empty())
                    for (auto& on : vmo2) slot_names.push_back(std::string(on.second));
                else
                    td.each_method([&](lir_view::TraitMethodSigView m) { slot_names.push_back(std::string(m.name())); });
                for (auto& mname : slot_names) {
                    std::string sym;
                    // Stage E: LImplBlock.methods was always empty — method
                    // resolution goes straight through the method_base index.
                    if (sym.empty()) {
                        if (auto it = method_base_idx.find(mname);
                            it != method_base_idx.end()) {
                            for (auto fp : it->second) {
                                if (belongs_to_target(fp.name())) {
                                    sym = link_name(fp); break;
                                }
                            }
                        }
                    }
                    if (sym.empty()) {
                        if (auto sit = struct_method_idx.find(std::string(target));
                            sit != struct_method_idx.end()) {
                            if (auto it = sit->second.find(mname);
                                it != sit->second.end()) {
                                for (auto mp : it->second) {
                                    if (belongs_to_target(mp.name())) {
                                        sym = link_name(mp); break;
                                    }
                                }
                            }
                        }
                    }
                    if (sym.empty()) {
                        // Object-safety check: if every fn carrying this
                        // method_base has method-level type-params
                        // (`fn fold<Acc>(...)`), the method is not callable
                        // through &dyn Trait — Rust's object-safety rule.
                        // Emit an empty sentinel so build_inline_vtable
                        // skips silently instead of warning "not found".
                        bool any_concrete = false;
                        bool any_generic  = false;
                        if (auto it = method_base_idx.find(mname);
                            it != method_base_idx.end()) {
                            for (auto fp : it->second) {
                                if (fp.impl_type_params_empty()) any_concrete = true;
                                else                              any_generic  = true;
                            }
                        }
                        if (any_generic && !any_concrete) {
                            // Method-generic only — non-dispatchable slot.
                            methods.emplace_back();
                            continue;
                        }
                        sym = std::string(target) + "__" + mname;
                    }
                    methods.push_back(std::move(sym));
                }
                return methods;
            };

            // Bare-target entry — used by non-generic impls and as a default
            // fallback. For non-generic structs, this is also the lookup key.
            std::string ib_target(ib.target_type());
            dyn_vtable_methods_[td_name + "::" + ib_target] =
                resolve_methods(ib_target);

            // Concrete-target entries — for generic impls, register one
            // vtable per concrete struct instantiation found in mono's
            // output (prog.functions / struct.methods). Lookup at
            // `coerce_to_dyn` keys on the concrete-mangled struct name
            // (`Foo$G1$arg`), so this is what makes generic-impl method
            // dispatch resolve to the right monomorphised symbols.
            //
            // The concrete-targets index keys instantiations under the BARE
            // struct base (`Foo`), but a generic impl's `target_type` is the
            // parameterised pattern (`impl<A> Clam<A> for Foo<A>` →
            // `Foo$G1$A`). Strip the `$G…` suffix to recover the bare base so
            // `&dyn Clam<i64>` over a `Foo<i64>` finds its `Foo$G1$i64` vtable
            // (otherwise: no entry → null vtable slot → SIGSEGV; G158-10).
            std::string_view target_base = ib_target;
            if (auto g = target_base.find("$G"); g != std::string_view::npos)
                target_base = target_base.substr(0, g);
            for (auto& concrete : collect_concrete_targets(std::string(target_base))) {
                dyn_vtable_methods_[td_name + "::" + concrete] =
                    resolve_methods(concrete);
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Build inline vtable [N x ptr] heap-allocated for a concrete type.
// ---------------------------------------------------------------------------

// Emit (once, deduped) the `__drop_in_place__<type>` glue function: a
// `func.func(ptr)` whose body runs the concrete type's FULL drop via
// gen_drop_value, then returns. Becomes slot 0 of every vtable (Rust-faithful;
// size/align slots omitted because Logos `dealloc` = libc `free`). For a Copy /
// drop-less type the body is an empty no-op — still emitted so slot 0 is valid.
std::string MLIRGenImpl::emit_drop_in_place_glue(std::string_view type_name,
                                                  TypeRef ty) {
    std::string key(type_name);
    if (auto it = dyn_drop_glue_.find(key); it != dyn_drop_glue_.end())
        return it->second;
    std::string sym = "__drop_in_place__";
    for (char c : key)
        sym += (std::isalnum((unsigned char)c) || c == '_' || c == '$') ? c : '_';
    dyn_drop_glue_[key] = sym;
    auto parent_mod = builder_.getBlock()->getParent()->getParentOfType<mlir::ModuleOp>();
    if (parent_mod.lookupSymbol(sym)) return sym;
    auto* ctx = builder_.getContext();
    mlir::OpBuilder::InsertionGuard guard(builder_);
    builder_.setInsertionPointToEnd(parent_mod.getBody());
    auto fn_type = mlir::FunctionType::get(ctx, {ptr_type()}, {});
    auto fn = builder_.create<mlir::func::FuncOp>(loc_, sym, fn_type);
    fn->setAttr("sym_visibility", mlir::StringAttr::get(ctx, "private"));
    auto* entry = fn.addEntryBlock();
    builder_.setInsertionPointToStart(entry);
    if (ty) gen_drop_value(entry->getArgument(0), ty, /*top_level=*/true);
    builder_.create<mlir::func::ReturnOp>(loc_);
    return sym;
}

std::string MLIRGenImpl::emit_closure_drop_glue(
        const std::string& closure_id,
        mlir::Type cap_struct,
        const std::vector<std::string>& /*captures*/,
        const std::vector<TypeRef>& capture_types,
        const std::vector<TypeRef>& capture_field_types,
        const std::vector<bool>& capture_drops,
        bool heap_env) {
    if (auto it = closure_drop_glue_.find(closure_id);
        it != closure_drop_glue_.end())
        return it->second;
    std::string sym = "__closure_drop__";
    for (char c : closure_id)
        sym += (std::isalnum((unsigned char)c) || c == '_') ? c : '_';
    closure_drop_glue_[closure_id] = sym;
    auto parent_mod =
        builder_.getBlock()->getParent()->getParentOfType<mlir::ModuleOp>();
    if (parent_mod.lookupSymbol(sym)) return sym;
    auto* ctx = builder_.getContext();
    mlir::OpBuilder::InsertionGuard guard(builder_);
    builder_.setInsertionPointToEnd(parent_mod.getBody());
    // (env_ptr) -> void as an llvm.func — like the closure body itself — so the
    // stored AddressOfOp resolves directly. The body freely emits func.call to
    // regular drop fns / free (same as a closure body, in_llvm_func_=true).
    auto void_t = mlir::LLVM::LLVMVoidType::get(ctx);
    auto fn_t = mlir::LLVM::LLVMFunctionType::get(void_t, {ptr_type()}, false);
    auto fn = builder_.create<mlir::LLVM::LLVMFuncOp>(loc_, sym, fn_t);
    fn.setLinkage(mlir::LLVM::Linkage::Private);
    auto* entry = fn.addEntryBlock(builder_);
    builder_.setInsertionPointToStart(entry);
    bool saved_in_llvm = in_llvm_func_;
    in_llvm_func_ = true;
    auto env_ptr = entry->getArgument(0);
    // Drop each owned droppable capture (env field i+1). RFC-2229 phase-2:
    // when capture_field_types[i] is set the env field holds the NARROW field's
    // value (not the root); drop the FIELD type, not the root.
    for (size_t i = 0; i < capture_types.size(); ++i) {
        if (i >= capture_drops.size() || !capture_drops[i]) continue;
        llvm::SmallVector<mlir::LLVM::GEPArg> gi{int32_t(0), int32_t(i + 1)};
        auto fp = builder_.create<mlir::LLVM::GEPOp>(
            loc_, ptr_type(), cap_struct, env_ptr, gi);
        TypeRef drop_t = (i < capture_field_types.size() && capture_field_types[i])
            ? capture_field_types[i] : capture_types[i];
        gen_drop_value(fp, drop_t, /*top_level=*/true);
    }
    // Free the heap env (escaping closure).
    if (heap_env) call_free(env_ptr);
    in_llvm_func_ = saved_in_llvm;
    if (!is_terminated(builder_.getBlock()))
        builder_.create<mlir::LLVM::ReturnOp>(loc_, mlir::ValueRange{});
    return sym;
}

mlir::Value MLIRGenImpl::build_inline_vtable(std::string_view trait_name,
                                               std::string_view type_name,
                                               TypeRef concrete_ty) {
    std::string sym = ensure_vtable_global(trait_name, type_name, concrete_ty);
    if (sym.empty()) return nullptr;
    // AddressOf the `[N x ptr]` global → a `ptr` to the table (the vtable ptr).
    return builder_.create<mlir::LLVM::AddressOfOp>(loc_, ptr_type(), sym);
}

std::string MLIRGenImpl::ensure_vtable_global(std::string_view trait_name,
                                              std::string_view type_name,
                                              TypeRef concrete_ty) {
    std::string key;
    key.reserve(trait_name.size() + 2 + type_name.size());
    key.append(trait_name); key.append("::"); key.append(type_name);
    // Already built (also breaks supertrait-diamond recursion).
    if (auto git = dyn_vtable_globals_.find(key); git != dyn_vtable_globals_.end())
        return git->second;
    auto vit = dyn_vtable_methods_.find(key);
    // Coexistence: the lookup's type_name is concrete_struct_name, which carries
    // a "$M<module_id>" suffix for a non-stdlib MODULE type, but the registration
    // keyed on the bare `ib.target_type`. Within one compile a (trait, type) pair
    // is unique, so fall back to the bare key — the vtable SYMBOL stays
    // module-qualified (built from the qualified type_name) for link distinctness.
    // Without this, `&ImportedWidget as &dyn ImportedTrait` finds no methods →
    // null vtable → SIGSEGV.
    if (vit == dyn_vtable_methods_.end()) {
        if (auto mp = type_name.find("$M"); mp != std::string_view::npos) {
            std::string bare_key(trait_name);
            bare_key += "::";
            bare_key.append(type_name.substr(0, mp));
            vit = dyn_vtable_methods_.find(bare_key);
        }
    }
    // Blanket fallback: no explicit (trait, type) vtable was registered, but
    // the trait has a blanket impl (`impl<T> Trait for T`) — so this concrete
    // type's methods are the blanket instantiations `<type>__<method>`.
    // Synthesize + cache the entry (verifying each symbol exists in the
    // module). Closes `&Concrete as &dyn BlanketTrait` (e.g. core::any::Any).
    if (vit == dyn_vtable_methods_.end()) {
        std::string tn(trait_name);
        if (blanket_traits_.count(tn)) {
            auto mnit = trait_method_names_.find(tn);
            if (mnit != trait_method_names_.end()) {
                auto parent = builder_.getBlock()->getParent()->getParentOfType<mlir::ModuleOp>();
                std::vector<std::string> synth;
                synth.reserve(mnit->second.size());
                bool all_found = true;
                for (auto& mname : mnit->second) {
                    std::string want = std::string(type_name) + "__" + mname;
                    std::string sym;
                    // The blanket instantiation may be pkg-qualified and carry
                    // a `__g__<sig>` / `__f__<sig>` mangling suffix (the
                    // method's `&self` over the blanket typevar). Match a
                    // symbol whose bare tail is `<type>__<method>` exactly or
                    // followed by `__g__` / `__f__`.
                    for (auto f : parent.getOps<mlir::func::FuncOp>()) {
                        std::string_view nm = f.getSymName();
                        std::string_view bare = nm;
                        if (auto d = bare.rfind('.'); d != std::string_view::npos)
                            bare = bare.substr(d + 1);
                        if (bare.size() < want.size()) continue;
                        if (bare.compare(0, want.size(), want) != 0) continue;
                        std::string_view rest = bare.substr(want.size());
                        if (rest.empty() ||
                            rest.compare(0, 5, "__g__") == 0 ||
                            rest.compare(0, 5, "__f__") == 0) {
                            sym = std::string(nm); break;
                        }
                    }
                    if (sym.empty()) { all_found = false; break; }
                    synth.push_back(std::move(sym));
                }
                if (all_found) {
                    vit = dyn_vtable_methods_.emplace(key, std::move(synth)).first;
                }
            }
        }
        if (vit == dyn_vtable_methods_.end()) return "";
    }
    // Rust-faithful vtable header: [ drop_in_place, size_of_T, align_of_T,
    // method0..N ]. size/align come from the unified layout_of(T) and are
    // encoded as `__logos_lit__<N>` slots — the post-lowering materializer
    // (compile_pipeline) detects the prefix and emits IntToPtr(ConstantInt)
    // instead of AddressOf, keeping the homogeneous [N x ptr] table. Methods
    // follow at slots 3..N (gen_dyn_dispatch adds the +3 shift). size/align
    // enable Rc<dyn>/Arc<dyn> to compute the RcInner layout for clone/drop.
    auto layout = concrete_ty ? layout_of(concrete_ty) : Layout{0, 1};
    std::vector<std::string> slots;
    slots.reserve(vit->second.size() + 3);
    slots.push_back(emit_drop_in_place_glue(type_name, concrete_ty));
    slots.push_back("__logos_lit__" + std::to_string(layout.size));
    slots.push_back("__logos_lit__" + std::to_string(layout.align));
    for (auto& m : vit->second) slots.push_back(m);
    // Stored super-vtable pointers (Rust trait-upcasting): one slot per
    // transitive supertrait, AFTER the methods, in `upcast_supertraits` order.
    // An upcast `&dyn Sub → &dyn Super` loads slot [3 + |methods| + idx(Super)]
    // to recover Super's vtable. Recurse to ensure each super's global exists;
    // the `__logos_vtref__<sym>` marker tells the post-lowering materializer to
    // AddressOf that global (vs. a method func or a size/align literal).
    if (auto sit = trait_upcast_supers_.find(std::string(trait_name));
        sit != trait_upcast_supers_.end()) {
        for (auto& super : sit->second) {
            std::string ssym = ensure_vtable_global(super, type_name, concrete_ty);
            slots.push_back(ssym.empty() ? std::string() : "__logos_vtref__" + ssym);
        }
    }
    auto& methods = slots;
    size_t n = methods.size();
    auto parent_mod = builder_.getBlock()->getParent()->getParentOfType<mlir::ModuleOp>();

    // TRUE STATIC vtable: one `constant [N x ptr]` global per (trait, type) in
    // .rodata/.data.rel.ro — Rust-style, zero heap, zero runtime init. The
    // address-of-method INITIALIZER can't be built here (`llvm.mlir.addressof`
    // of a method needs an `llvm.func`, but methods are still `func.func`
    // pre-lowering), so we emit a zero-init placeholder now + record the slot
    // method symbols; the initializer is materialised after func→llvm lowering
    // (lower_and_emit_object, via the `logos.vtable_specs` module attr). The
    // coercion just takes the global's address — the vtable ptr.
    std::string sym;
    if (auto git = dyn_vtable_globals_.find(key); git != dyn_vtable_globals_.end()) {
        sym = git->second;
    } else {
        sym = "__logos_vtable__";
        sym.reserve(sym.size() + key.size());
        for (char c : key)
            sym += (std::isalnum((unsigned char)c) || c == '_' || c == '$') ? c : '_';
        if (!parent_mod.lookupSymbol(sym)) {
            auto arr_type = mlir::LLVM::LLVMArrayType::get(ptr_type(), n ? n : 1);
            mlir::OpBuilder::InsertionGuard guard(builder_);
            builder_.setInsertionPointToEnd(parent_mod.getBody());
            auto glob = builder_.create<mlir::LLVM::GlobalOp>(
                loc_, arr_type, /*isConstant=*/true,
                mlir::LLVM::Linkage::LinkonceODR, sym, mlir::Attribute{}, 0);
            auto& ir = glob.getInitializerRegion();
            builder_.setInsertionPointToStart(builder_.createBlock(&ir));
            builder_.create<mlir::LLVM::ReturnOp>(
                loc_, builder_.create<mlir::LLVM::ZeroOp>(loc_, arr_type));
            dyn_vtable_specs_.push_back({sym, methods});
        }
        dyn_vtable_globals_[key] = sym;
    }
    return sym;
}

// ---------------------------------------------------------------------------
// Build a &dyn Trait fat pointer from a concrete data_ptr.
// ---------------------------------------------------------------------------

mlir::Value MLIRGenImpl::coerce_to_dyn(mlir::Value data_ptr, std::string_view trait_name,
                                        std::string_view src_type_name,
                                        TypeRef concrete_ty) {
    auto dyn_struct = dyn_llvm_type();
    // The {data,vtable} fat pair lives in a STACK alloca (value-fat-pair model,
    // like a slice) — `&dyn`/`*dyn`/`Box<dyn>` are all uniform 16-byte fat. The
    // value is a pointer to this storage; escape consumers (enum payload, struct
    // field, Vec slot, return) copy the 16 bytes into their OWN inline storage,
    // so the fat pair travels by value and never needs a heap handle. Owning
    // `Box<dyn>`'s `data` half is the heap concrete, freed by vtable[0] on drop.
    // (A former `heap=true` path malloc'd a thin handle for a raw `*dyn` escape /
    // `Ptr<TraitObject>` local — conceptually broken, non-Rust, and provably
    // unreachable across all 5433 tests/stdlib/examples; removed. A future raw
    // escape would use a `*u8`/system-type widened via an intrinsic.)
    auto alloca = create_entry_alloca(dyn_struct);
    if (!alloca) return nullptr;
    // Store data pointer at field 0
    llvm::SmallVector<mlir::LLVM::GEPArg> idx0{int32_t(0), int32_t(0)};
    auto dp = builder_.create<mlir::LLVM::GEPOp>(
        loc_, ptr_type(), dyn_struct, alloca, idx0);
    builder_.create<mlir::LLVM::StoreOp>(loc_, data_ptr, dp);
    // Store vtable pointer at field 1
    auto vtable = build_inline_vtable(trait_name, src_type_name, concrete_ty);
    if (vtable) {
        llvm::SmallVector<mlir::LLVM::GEPArg> idx1{int32_t(0), int32_t(1)};
        auto vp = builder_.create<mlir::LLVM::GEPOp>(
            loc_, ptr_type(), dyn_struct, alloca, idx1);
        builder_.create<mlir::LLVM::StoreOp>(loc_, vtable, vp);
    }
    return alloca;
}

mlir::Value MLIRGenImpl::coerce_value_to_dyn_if_needed(
        mlir::Value val, TypeRef slot_lt, TypeRef val_lt) {
    using K = LogosType::Kind;
    if (!val || !slot_lt || !val_lt) return val;
    auto unbox = [](TypeRef t) -> TypeRef {
        if (is_stdlib_box(t) && TypeRef(t).type_args().size() == 1)
            return TypeRef(t).type_args()[0];
        return t;
    };
    TypeRef ptl = unbox(slot_lt), alt = unbox(val_lt);
    if (!(ptl && TypeRef(ptl).kind() == K::TraitObject)) return val;
    if (alt && TypeRef(alt).kind() == K::TraitObject) return val;  // already dyn
    TypeRef vt_type = val_lt;
    if (TypeRef(vt_type).kind() == K::Ref || TypeRef(vt_type).kind() == K::MutRef)
        vt_type = TypeRef(vt_type).pointee();
    if (is_stdlib_box(vt_type) && TypeRef(vt_type).type_args().size() == 1)
        vt_type = TypeRef(vt_type).type_args()[0];
    if (!vt_type) return val;
    if (val.getType() != ptr_type() &&
        TypeRef(val_lt).kind() != K::Ref &&
        TypeRef(val_lt).kind() != K::MutRef &&
        !is_stdlib_box(val_lt))
        val = spill_to_alloca(val);
    std::string vt_name =
        (TypeRef(vt_type).kind() == K::Struct ||
         TypeRef(vt_type).kind() == K::ZonedStruct)
            ? concrete_struct_name(vt_type)
            : type_str(vt_type);
    // Uniform fat model: a `Box<dyn>` slot is now an INLINE 16-byte {data,vtable}
    // fat pair (like `&dyn`), where `data` = the box's heap concrete pointer
    // (`val` IS that pointer — Box<concrete> = {ptr}). No malloc(16) handle: the
    // pair is a stack alloca the consumer copies (Vec slot / return). Drop frees
    // `data` via vtable[0].
    auto fat = coerce_to_dyn(val, std::string(TypeRef(ptl).trait_name()), vt_name, vt_type);
    return fat ? fat : val;
}

// ---------------------------------------------------------------------------
// Indirect call through &tagged<TS> Trait dispatch.
//
// Dispatch sequence:
//   1. Evaluate receiver — *const u8 pointing to the tagged object.
//   2. Call read_type_code(obj_ptr) → i64 type_code.
//   3a. If type_code < 223 (tier-1): GEP into @__logos_tag_dispatch_*[type_code].
//   3b. If type_code >= 223 (tier-2): call @__logos_tier2_*_lookup(type_code).
//   4. Load/receive fn_ptr.
//   5. Indirect call through fn_ptr with (obj_ptr, user_args…).
// ---------------------------------------------------------------------------

mlir::Value MLIRGenImpl::gen_tagged_dispatch(lir_view::EMethodCallView v,
                                              TypeRef ret_logos_type) {
    constexpr int kTier1Size = 256;
    auto ptr_t = ptr_type();

    auto recv_ref = v.receiver();
    if (!recv_ref) return nullptr;

    // 1. Evaluate the receiver.
    mlir::Value obj_ptr = nullptr;
    if (recv_ref && recv_ref.kind() == lir_schema::expr::Code::VarRef) {
        std::string name(lir_view::EVarRefView{recv_ref}.name());
        auto it = scope_.find(name);
        if (it != scope_.end()) {
            if (let_vars_.count(name))
                obj_ptr = builder_.create<mlir::LLVM::LoadOp>(loc_, ptr_t, it->second);
            else
                obj_ptr = it->second;
        }
    }
    if (!obj_ptr) obj_ptr = gen_expr(recv_ref);
    if (!obj_ptr) return nullptr;

    // 2. <TagSystem>::read_tag(nullptr_self, obj_ptr) → i64.
    // Each TagSystem knows how to extract the type_code from memory for its
    // encoding (legacy 2-byte tag vs. vlen datatag vs. TOM inline header).
    // TagSystems are unit structs with no state, so self can be a null pointer.
    auto parent_mod = builder_.getBlock()->getParent()->getParentOfType<mlir::ModuleOp>();
    auto rtc_base = std::string(v.tag_system()) + "__read_tag";
    std::string rtc_sym = rtc_base;
    auto rtc_fn = parent_mod.lookupSymbol<mlir::func::FuncOp>(rtc_sym);
    if (!rtc_fn && prog_) {
        // Method names may be bare (`Base__read_tag[__f__sig]`) or pkg-qualified
        // (`pkg.Base__read_tag[__f__sig]`). Concrete trait impls land in
        // prog_->functions (not sd.methods), so scan both.
        std::string rtc_f = rtc_base + "__f__";
        std::string rtc_g = rtc_base + "__g__";
        auto try_match = [&](const std::string& nm) {
            return nm == rtc_base ||
                   nm.find(rtc_f) != std::string::npos ||
                   nm.find(rtc_g) != std::string::npos;
        };
        for (auto& sd : prog_->structs) {
            if (sd.name() != v.tag_system()) continue;
            for (auto& mp : sd.methods()) {
                if (!mp) continue;
                if (try_match(std::string(mp.name()))) {
                    rtc_sym = link_name(mp);  // module-qualified emitted name
                    rtc_fn = parent_mod.lookupSymbol<mlir::func::FuncOp>(rtc_sym);
                    if (rtc_fn) break;
                }
            }
            if (rtc_fn) break;
        }
        if (!rtc_fn) {
            for (auto& fn : prog_->functions) {
                if (!fn) continue;
                if (try_match(std::string(fn.name()))) {
                    rtc_sym = link_name(fn);  // module-qualified emitted name
                    rtc_fn = parent_mod.lookupSymbol<mlir::func::FuncOp>(rtc_sym);
                    if (rtc_fn) break;
                }
            }
        }
    }
    if (!rtc_fn) {
        std::fprintf(stderr, "gen_tagged_dispatch: '%s' not found\n", rtc_sym.c_str());
        return nullptr;
    }
    auto null_self = builder_.create<mlir::LLVM::ZeroOp>(loc_, ptr_t);
    mlir::Value type_code_val = builder_.create<mlir::func::CallOp>(
        loc_, rtc_fn, mlir::ValueRange{null_self, obj_ptr}).getResult(0);

    // Identify available tables.
    // Must use the same "__" separator as emit_tag_dispatch_tables.
    auto base_key    = std::string(v.tag_system()) + "__" + std::string(v.tag_trait()) + "__" + std::string(v.method());
    auto table_sym   = "__logos_tag_dispatch__" + base_key;
    auto t2_lkp_sym  = "__logos_tier2__" + base_key + "_lookup";
    bool has_tier1   = (parent_mod.lookupSymbol<mlir::LLVM::GlobalOp>(table_sym) != nullptr);
    bool has_tier2   = (parent_mod.lookupSymbol<mlir::func::FuncOp>(t2_lkp_sym)  != nullptr);

    if (!has_tier1 && !has_tier2) {
        std::fprintf(stderr, "gen_tagged_dispatch: no dispatch table for '%s'\n",
                     table_sym.c_str());
        return nullptr;
    }

    // 3. Resolve fn_ptr (tier-1 or tier-2 path).
    mlir::Value fn_ptr;

    if (!has_tier2) {
        // ── Tier-1 only: direct array lookup ──────────────────────────────
        auto arr_type = mlir::LLVM::LLVMArrayType::get(ptr_t, kTier1Size);
        auto table_addr = builder_.create<mlir::LLVM::AddressOfOp>(loc_, ptr_t, table_sym);
        llvm::SmallVector<mlir::LLVM::GEPArg> gi;
        gi.push_back(int32_t(0));
        gi.push_back(type_code_val);
        auto slot_ptr = builder_.create<mlir::LLVM::GEPOp>(
            loc_, ptr_t, arr_type, table_addr, gi);
        fn_ptr = builder_.create<mlir::LLVM::LoadOp>(loc_, ptr_t, slot_ptr);
    } else {
        // ── Conditional: tier-1 if type_code < 223, else tier-2 lookup ───
        auto fn_ptr_alloca = create_entry_alloca(ptr_t);

        auto boundary = builder_.create<mlir::arith::ConstantIntOp>(
            loc_, static_cast<int64_t>(kTier1Size), 64);
        auto is_tier1 = builder_.create<mlir::arith::CmpIOp>(
            loc_, mlir::arith::CmpIPredicate::ult, type_code_val, boundary);

        auto* region      = builder_.getBlock()->getParent();
        auto* tier1_block = new mlir::Block();
        auto* tier2_block = new mlir::Block();
        auto* merge_block = new mlir::Block();
        region->push_back(tier1_block);
        region->push_back(tier2_block);
        region->push_back(merge_block);

        builder_.create<mlir::cf::CondBranchOp>(loc_, is_tier1, tier1_block, tier2_block);

        // tier1_block
        builder_.setInsertionPointToStart(tier1_block);
        if (has_tier1) {
            auto arr_type = mlir::LLVM::LLVMArrayType::get(ptr_t, kTier1Size);
            auto table_addr = builder_.create<mlir::LLVM::AddressOfOp>(
                loc_, ptr_t, table_sym);
            llvm::SmallVector<mlir::LLVM::GEPArg> gi;
            gi.push_back(int32_t(0));
            gi.push_back(type_code_val);
            auto slot_ptr = builder_.create<mlir::LLVM::GEPOp>(
                loc_, ptr_t, arr_type, table_addr, gi);
            auto t1_fn = builder_.create<mlir::LLVM::LoadOp>(loc_, ptr_t, slot_ptr);
            builder_.create<mlir::LLVM::StoreOp>(loc_, t1_fn, fn_ptr_alloca);
        } else {
            builder_.create<mlir::LLVM::StoreOp>(
                loc_, builder_.create<mlir::LLVM::ZeroOp>(loc_, ptr_t), fn_ptr_alloca);
        }
        builder_.create<mlir::cf::BranchOp>(loc_, merge_block);

        // tier2_block
        builder_.setInsertionPointToStart(tier2_block);
        {
            auto lkp_fn = parent_mod.lookupSymbol<mlir::func::FuncOp>(t2_lkp_sym);
            if (!lkp_fn) {
                // Lookup function missing — store null and fall through.
                builder_.create<mlir::LLVM::StoreOp>(
                    loc_, builder_.create<mlir::LLVM::ZeroOp>(loc_, ptr_t), fn_ptr_alloca);
            } else {
                auto t2_fn = builder_.create<mlir::func::CallOp>(
                    loc_, lkp_fn, mlir::ValueRange{type_code_val}).getResult(0);
                builder_.create<mlir::LLVM::StoreOp>(loc_, t2_fn, fn_ptr_alloca);
            }
        }
        builder_.create<mlir::cf::BranchOp>(loc_, merge_block);

        // merge_block
        builder_.setInsertionPointToStart(merge_block);
        fn_ptr = builder_.create<mlir::LLVM::LoadOp>(loc_, ptr_t, fn_ptr_alloca);
    }

    // 4. Build call args: obj_ptr (self as *const u8) + user args.
    llvm::SmallVector<mlir::Value> args;
    args.push_back(obj_ptr);
    llvm::SmallVector<mlir::Type> param_types;
    param_types.push_back(ptr_t);  // self: *const u8

    v.each_arg([&](lir_view::ExprRef ar){
        if (!ar) { return; }
        auto val = gen_expr(ar);
        if (!val) return;
        args.push_back(val);
        param_types.push_back(val.getType());
    });

    // 6. Build LLVM function type and call indirectly.
    mlir::Type ret_type;
    if (ret_logos_type && TypeRef(ret_logos_type).kind() != LogosType::Kind::Void)
        ret_type = logos_to_mlir(ret_logos_type);
    if (!ret_type)
        ret_type = mlir::LLVM::LLVMVoidType::get(builder_.getContext());
    auto fn_type = mlir::LLVM::LLVMFunctionType::get(ret_type, param_types);

    llvm::SmallVector<mlir::Value> all_operands;
    all_operands.push_back(fn_ptr);
    all_operands.append(args.begin(), args.end());
    auto call = builder_.create<mlir::LLVM::CallOp>(
        loc_, fn_type, mlir::FlatSymbolRefAttr{},
        mlir::ValueRange(all_operands));
    bool is_void = mlir::isa<mlir::LLVM::LLVMVoidType>(fn_type.getReturnType());
    if (is_void) return nullptr;
    return call.getResult();
}

// ---------------------------------------------------------------------------
// Indirect call through &dyn Trait vtable.
// ---------------------------------------------------------------------------

mlir::Value MLIRGenImpl::gen_dyn_dispatch(lir_view::EMethodCallView v,
                                           TypeRef ret_logos_type) {
    // The receiver is a &dyn Trait — a pointer to {data_ptr, vtable_ptr}.

    auto recv_ref = v.receiver();
    if (!recv_ref) return nullptr;
    // Unwrap the implicit-reborrow wrap (inserted by sema's
    // try_implicit_reborrow_mut). The wrap exists for borrow_check; for
    // vtable dispatch we need the underlying VarRef so the var_dyn_trait_ /
    // fat-pair-load logic below fires correctly — without the unwrap, the
    // wrap appears as a non-VarRef receiver, the loader treats it as a
    // by-value fat pair, and field-0/field-1 GEPs misread → segfault.
    if (lir_view::ExprRef inner_var; lir_view::is_reborrow_shape(recv_ref, &inner_var)) {
        recv_ref = inner_var;
        if (!recv_ref) return nullptr;
    }
    TypeRef recv_ty = recv_ref.type(pool_impl());

    // Check if receiver is a variable we know is dyn
    mlir::Value recv_alloca = nullptr;
    bool recv_var_holds_value = false;  // scope_[name] is the value, not an alloca
    if (recv_ref && recv_ref.kind() == lir_schema::expr::Code::VarRef) {
        std::string name(lir_view::EVarRefView{recv_ref}.name());
        auto it = scope_.find(name);
        if (it != scope_.end()) {
            recv_alloca = it->second;
            // A dyn-binding (`let rd: &dyn = …`, a &dyn param) stores the fat-
            // pointer VALUE directly in scope_ (a pointer to the 16-byte storage),
            // not an alloca holding it — mirrors var_tuple_/closure/slice lets.
            // So no extra load is needed to reach the storage.
            recv_var_holds_value =
                var_dyn_trait_.count(name) || var_tuple_.count(name) ||
                ref_param_names_.count(name);  // slice-for `&T` ref binding / &dyn param
        }
    }
    if (!recv_alloca) {
        recv_alloca = gen_expr(recv_ref);
    }
    if (!recv_alloca) return nullptr;
    bool recv_from_varref =
        recv_ref && recv_ref.kind() == lir_schema::expr::Code::VarRef;

    auto dyn_struct = dyn_llvm_type();

    // G168-A: a `&(dyn Trait)` receiver (`Ref<TraitObject>`) — when it is an
    // ALLOCA-backed VARIABLE, scope_ holds the alloca (ADDRESS of the slot
    // holding the fat-pointer value), so load once to reach the value (a pointer
    // to the 16-byte storage). When scope_ holds the VALUE directly (a dyn-let /
    // &dyn param — recv_var_holds_value), or the receiver is a direct value (a
    // method-call result like `v.borrow(i)`, an `&*box` auto-deref), gen_expr/
    // scope_ already yielded the fat-pointer value — loading it would read `data`
    // as the storage address (the Box<dyn>-via-borrow dispatch crash). Load only
    // the address-bearing form.
    if (recv_ty && recv_from_varref && !recv_var_holds_value) {
        TypeRef rlt(recv_ty);
        if ((rlt.kind() == LogosType::Kind::Ref ||
             rlt.kind() == LogosType::Kind::MutRef) && rlt.pointee() &&
            TypeRef(rlt.pointee()).kind() == LogosType::Kind::TraitObject)
            recv_alloca = builder_.create<mlir::LLVM::LoadOp>(
                loc_, ptr_type(), recv_alloca);
    }

    // Value-fat-pair model: normalise the receiver to a POINTER to the 16-byte
    // {data,vtable} storage (so the field-0/field-1 GEPs below work).
    //  - A struct-VALUE receiver (e.g. read of an inline `&dyn` struct field, or
    //    a returned-by-value fat pair) must be spilled to an alloca.
    //  - A `&dyn`/`dyn` (TraitObject) value is ALREADY a pointer to the 16-byte
    //    storage (mirrors a slice value) — use as-is.
    //  - A `&(&dyn)` (Ref<TraitObject>) is a pointer to a slot holding the fat
    //    pointer value; in the value model that slot IS the 16-byte storage, so
    //    it is likewise already the pointer we want — use as-is.
    if (recv_alloca.getType() == dyn_struct)
        recv_alloca = spill_to_alloca(recv_alloca);

    // Load data_ptr (field 0)
    llvm::SmallVector<mlir::LLVM::GEPArg> idx0{int32_t(0), int32_t(0)};
    auto dp = builder_.create<mlir::LLVM::GEPOp>(
        loc_, ptr_type(), dyn_struct, recv_alloca, idx0);
    auto data_ptr = builder_.create<mlir::LLVM::LoadOp>(loc_, ptr_type(), dp);

    // Load vtable_ptr (field 1)
    llvm::SmallVector<mlir::LLVM::GEPArg> idx1{int32_t(0), int32_t(1)};
    auto vp = builder_.create<mlir::LLVM::GEPOp>(
        loc_, ptr_type(), dyn_struct, recv_alloca, idx1);
    auto vtable_ptr = builder_.create<mlir::LLVM::LoadOp>(loc_, ptr_type(), vp);

    // GEP into vtable array to get fn_ptr at vtable_index. Slots 0/1/2 are the
    // drop_in_place glue + size_of_T + align_of_T (Rust-faithful layout:
    // [drop, size, align, method0, method1, …]), so the trait method's
    // declared position shifts by +3.
    llvm::SmallVector<mlir::LLVM::GEPArg> slot_idx{int32_t(v.vtable_index() + 3)};
    auto slot_ptr = builder_.create<mlir::LLVM::GEPOp>(
        loc_, ptr_type(), ptr_type(), vtable_ptr, slot_idx);
    auto fn_ptr = builder_.create<mlir::LLVM::LoadOp>(loc_, ptr_type(), slot_ptr);

    // Build args: data_ptr (self) + user args
    llvm::SmallVector<mlir::Value> args;
    args.push_back(data_ptr);
    v.each_arg([&](lir_view::ExprRef ar){
        if (!ar) return;
        auto val = gen_expr(ar);
        if (!val) return;
        args.push_back(val);
    });

    // Build LLVM function type for the indirect call.
    llvm::SmallVector<mlir::Type> param_types;
    for (auto& a : args) param_types.push_back(a.getType());

    mlir::Type ret_type;
    if (ret_logos_type && TypeRef(ret_logos_type).kind() != LogosType::Kind::Void) {
        // llvm_fn_ret_type handles the by-value aggregate return shape
        // (Struct/ZonedStruct/Enum) — see helper docs and
        // [[baghunt-dyn-in-enum-payload]] for the rationale.
        ret_type = llvm_fn_ret_type(ret_logos_type);
    }
    if (!ret_type)
        ret_type = mlir::LLVM::LLVMVoidType::get(builder_.getContext());
    auto fn_type = mlir::LLVM::LLVMFunctionType::get(ret_type, param_types);

    // Indirect call via function pointer (same pattern as closure calls)
    llvm::SmallVector<mlir::Value> all_operands;
    all_operands.push_back(fn_ptr);
    all_operands.append(args.begin(), args.end());
    auto call = builder_.create<mlir::LLVM::CallOp>(
        loc_, fn_type, mlir::FlatSymbolRefAttr{},
        mlir::ValueRange(all_operands));
    bool is_void = mlir::isa<mlir::LLVM::LLVMVoidType>(fn_type.getReturnType());
    if (is_void) return nullptr;
    return call.getResult();
}

// ---------------------------------------------------------------------------
// Closure generation
// ---------------------------------------------------------------------------

mlir::Value MLIRGenImpl::gen_closure(lir_view::EClosureBoxView v, TypeRef) {
    auto parent_mod = builder_.getBlock()->getParent()->getParentOfType<mlir::ModuleOp>();
    auto save_pt = builder_.saveInsertionPoint();

    // Materialise params, captures, ret_type / closure_id once from the view.
    std::vector<std::pair<std::string, TypeRef>> params;
    v.each_param(pool_impl(), [&](std::string_view name, TypeRef t) {
        params.emplace_back(std::string(name), t);
    });
    std::vector<std::string> captures;
    std::vector<TypeRef> capture_types;
    v.each_capture(pool_impl(), [&](std::string_view name, TypeRef t) {
        captures.emplace_back(name);
        capture_types.push_back(t);
    });
    // RFC-2229 phase-2: per-capture narrow FIELD type (null = whole-root).
    std::vector<TypeRef> capture_field_ts;
    capture_field_ts.reserve(captures.size());
    for (uint64_t i = 0; i < captures.size(); ++i)
        capture_field_ts.push_back(v.capture_field_type(pool_impl(), i));
    // Walk a dotted `field.field…` chain through nested Struct fields starting
    // from `start` (a pointer to a value of `root_t`) and return the address of
    // the leaf field. nullptr if any intermediate isn't a known Struct (then
    // the caller falls back to whole-root capture). Used by both env-fill (read
    // outer struct's leaf at closure CREATION) and body-unpack (write env value
    // into the fake-root struct's leaf at closure ENTRY).
    auto gep_field_chain = [&](mlir::Value start, TypeRef root_t,
                                const std::string& rel_path) -> mlir::Value {
        if (rel_path.empty()) return start;
        TypeRef cur_t = root_t;
        mlir::Value cur = start;
        std::string_view rem(rel_path);
        while (!rem.empty()) {
            if (!cur_t || TypeRef(cur_t).kind() != LogosType::Kind::Struct) {
                return mlir::Value{};
            }
            auto skey = mlir_struct_key(cur_t);
            auto sit = struct_types_.find(skey);
            if (sit == struct_types_.end())
                sit = struct_types_.find(std::string(TypeRef(cur_t).struct_name()));
            auto sdit = all_struct_defs_.find(skey);
            if (sdit == all_struct_defs_.end())
                sdit = all_struct_defs_.find(std::string(TypeRef(cur_t).struct_name()));
            auto dot = rem.find('.');
            std::string fname(rem.substr(0, dot));
            auto fp = gep_field(cur, sit->second, fname);
            if (!fp) return mlir::Value{};
            TypeRef next;
            for (auto& f : sdit->second.fields())
                if (f.name() == fname) { next = f.type(pool_impl()); break; }
            if (dot == std::string_view::npos) return fp;
            rem = rem.substr(dot + 1);
            cur = fp;
            cur_t = next;
        }
        return cur;
    };
    TypeRef ret_t        = v.ret_type(pool_impl());
    std::string closure_id(v.closure_id());
    bool        as_fn_ptr_flag = v.as_fn_ptr();

    auto body_blk = v.body();

    // Array params arrive as `ptr` (matching make_fn_type's plain-fn ABI: the
    // call site materialises an array literal to a stack slot and passes its
    // address). Register var_subscript_ so a body `u[i]` strides by the element
    // type instead of GEP-ing a (non-pointer) array aggregate value — without
    // this the closure body fails MLIR verification on `[T; N]` params.
    auto register_array_param_subscript = [&](const std::string& name, TypeRef pt) {
        if (!pt || TypeRef(pt).kind() != LogosType::Kind::Array || !TypeRef(pt).elem())
            return;
        TypeRef ae = TypeRef(pt).elem();
        mlir::Type et;
        if (ae.kind() == LogosType::Kind::Struct ||
            ae.kind() == LogosType::Kind::ZonedStruct) {
            auto cname = mlir_struct_key(ae);
            auto sit = struct_types_.find(cname);
            if (sit != struct_types_.end()) et = sit->second.llvm_type;
        }
        if (!et) et = logos_to_mlir(ae);
        if (et) var_subscript_[name] = et;
    };

    // Non-capturing closure coerced to fn ptr: emit as plain function (no env_ptr).
    if (as_fn_ptr_flag) {
        // Build function type without env_ptr: (params...) -> ret
        llvm::SmallVector<mlir::Type> fn_params;
        for (auto& [_, pt_type] : params) {
            if (pt_type && TypeRef(pt_type).kind() == LogosType::Kind::Array) {
                fn_params.push_back(ptr_type());  // arrays passed by pointer
                continue;
            }
            auto pt = logos_to_mlir(pt_type);
            if (pt) fn_params.push_back(pt);
        }
        mlir::Type llvm_ret = ret_t
            ? llvm_fn_ret_type(ret_t)
            : mlir::LLVM::LLVMVoidType::get(builder_.getContext());
        if (!llvm_ret) llvm_ret = mlir::LLVM::LLVMVoidType::get(builder_.getContext());
        auto llvm_fn_type = mlir::LLVM::LLVMFunctionType::get(llvm_ret, fn_params, false);
        builder_.setInsertionPointToEnd(parent_mod.getBody());
        auto fn = builder_.create<mlir::LLVM::LLVMFuncOp>(loc_, closure_id, llvm_fn_type);
        fn.setLinkage(mlir::LLVM::Linkage::Private);
        auto* entry = fn.addEntryBlock(builder_);
        builder_.setInsertionPointToStart(entry);
        // Save/restore state (same as regular closure)
        auto saved_scope      = scope_;
        auto saved_lets       = let_vars_;
        auto saved_elems      = var_elem_types_;
        auto saved_ret        = cur_ret_type_;
        auto saved_struct     = var_struct_;
        auto saved_subscript  = var_subscript_;
        auto saved_tuple      = var_tuple_;
        auto saved_te         = var_tagged_enum_;
        auto saved_te_ptr     = var_tagged_enum_ptr_;
        auto saved_local_ptrs = var_local_ptrs_;
        auto saved_dyn_trait  = var_dyn_trait_;
        auto saved_dyn_coerced = dyn_ptr_to_handle_vars_;
        auto saved_loop_stack = loop_stack_;
        auto saved_entry_block = cur_entry_block_;
        cur_entry_block_ = entry;
        scope_.clear(); let_vars_.clear(); var_elem_types_.clear();
        var_struct_.clear(); var_subscript_.clear();
        var_tuple_.clear(); var_tagged_enum_.clear(); var_tagged_enum_ptr_.clear();
        var_local_ptrs_.clear(); var_dyn_trait_.clear(); dyn_ptr_to_handle_vars_.clear(); loop_stack_.clear();
        cur_ret_type_ = ret_t ? llvm_fn_ret_type(ret_t) : mlir::Type{};
        // Bind params starting from arg 0 (no env_ptr)
        for (size_t i = 0; i < params.size(); ++i) {
            scope_[params[i].first] = entry->getArgument(i);
            register_array_param_subscript(params[i].first, params[i].second);
        }
        bool saved_in_llvm = in_llvm_func_;
        in_llvm_func_ = true;
        if (body_blk) gen_block(body_blk);
        if (!is_terminated(builder_.getBlock()))
            builder_.create<mlir::LLVM::ReturnOp>(loc_, mlir::ValueRange{});
        in_llvm_func_ = saved_in_llvm;
        scope_              = saved_scope;
        let_vars_           = saved_lets;
        var_elem_types_     = saved_elems;
        cur_ret_type_       = saved_ret;
        var_struct_         = saved_struct;
        var_subscript_      = saved_subscript;
        var_tuple_          = saved_tuple;
        var_tagged_enum_    = saved_te;
        var_tagged_enum_ptr_ = saved_te_ptr;
        var_local_ptrs_     = saved_local_ptrs;
        var_dyn_trait_      = saved_dyn_trait;
        dyn_ptr_to_handle_vars_ = saved_dyn_coerced;
        loop_stack_         = saved_loop_stack;
        cur_entry_block_    = saved_entry_block;
        builder_.restoreInsertionPoint(save_pt);
        // Return the function address (this IS the fn ptr value)
        return builder_.create<mlir::LLVM::AddressOfOp>(loc_, ptr_type(), closure_id);
    }

    std::vector<bool> capture_is_struct(captures.size(), false);
    std::vector<bool> capture_is_array(captures.size(), false);
    std::vector<bool> capture_is_tuple(captures.size(), false);
    std::vector<bool> capture_is_enum(captures.size(), false);
    std::vector<bool> capture_is_dyn(captures.size(), false);
    std::vector<bool> capture_is_pointer_repr(captures.size(), false);
    // C5-cl-08: per-capture by-ref flag. True for captures whose value is
    // mutated inside the closure body AND the source variable is a let-bound
    // scalar (alloca'd). The env field stores the outer alloca pointer
    // instead of a value copy, so mutations round-trip back.
    std::vector<bool> capture_is_mut_ref(captures.size(), false);
    // G149-3: a `move` closure owns a COPY of each scalar capture. A mutated
    // Copy capture must (a) NOT escape to the outer variable, and (b) persist
    // its mutation across calls. So the env stores a value copy and the body
    // reads/writes THROUGH the env field pointer (not the outer alloca, not a
    // throwaway local copy). This `env_mut` mode achieves both.
    std::vector<bool> capture_is_env_mut(captures.size(), false);
    for (size_t i = 0; i < captures.size(); ++i) {
        const auto& name = captures[i];
        if (!v.capture_is_mut(i)) continue;
        // Only meaningful for scalar (let-bound) captures: the existing
        // pointer-repr categories (struct/etc.) already store a
        // pointer, so mutations propagate without extra plumbing.
        if (!let_vars_.count(name)) continue;
        if (v.is_move()) capture_is_env_mut[i] = true;  // own a mutable copy
        else             capture_is_mut_ref[i] = true;  // borrow: escape to outer
    }
    for (size_t i = 0; i < captures.size(); ++i) {
        const auto& name = captures[i];
        capture_is_struct[i] = var_struct_.count(name);
        capture_is_array[i]  = var_subscript_.count(name);
        capture_is_tuple[i]  = var_tuple_.count(name);
        capture_is_enum[i]   = var_tagged_enum_.count(name);
        capture_is_dyn[i]    = var_dyn_trait_.count(name);
        capture_is_pointer_repr[i] =
            capture_is_struct[i] || capture_is_array[i] ||
            capture_is_tuple[i] || capture_is_enum[i] || capture_is_dyn[i];
    }

    // ESCAPING move-closure ownership transfer. A heap-env `move` closure OWNS
    // a struct/array/tuple/enum capture by VALUE (Rust semantics) instead of
    // borrowing the outer var's address. The capture is moved INTO the env (an
    // inline-by-value env field, memcpy'd at the creation site), the body binds
    // it to the inline field address (one level), and the env-glue drops it.
    // The ORIGINAL scope must NOT drop it (sema removes it from
    // closure_owned_drop_) — the predicate here MUST match that sema decision
    // exactly (else double-free / leak). `&dyn` (capture_is_dyn) stays a borrow:
    // a dyn value-fat-pair is itself a borrowed handle, not owned storage.
    // The decision needs `heap_env`, computed identically to the creation site.
    bool heap_env_pre = v.escapes() && !captures.empty();
    std::vector<bool> capture_own_inline(captures.size(), false);
    if (heap_env_pre && v.is_move()) {
        for (size_t i = 0; i < captures.size(); ++i) {
            if (!capture_is_pointer_repr[i]) continue;
            if (capture_is_dyn[i]) continue;  // dyn handle is a borrow
            if (capture_is_mut_ref[i]) continue;
            capture_own_inline[i] = true;
        }
    }
    // RFC-2229 phase-2: a narrow capture on a Struct root in an ESCAPING (heap-
    // env) `move` closure owns its FIELD value inline — env-glue fires on the
    // Box<dyn Fn> drop, so the field's Drop runs. Non-escaping stays borrow-by-
    // pointer (whole-root model) — sema's mark_moved(path) is also gated on
    // escaping so the original root still drops the field at scope-exit.
    if (heap_env_pre && v.is_move()) {
        for (size_t i = 0; i < captures.size(); ++i)
            if (capture_field_ts[i]) capture_own_inline[i] = true;
    }

    // Build capture struct type.
    // ENV FIELD 0 is reserved for a `drop_glue: ptr` slot (uniform drop
    // protocol — see __closure_drop__ glue). Captures occupy fields 1..N.
    // EVERY closure gets the slot so the drop site is shape-uniform; a
    // closure with nothing to drop stores null there (drop becomes a no-op).
    llvm::SmallVector<mlir::Type> cap_fields;
    cap_fields.push_back(ptr_type());  // field 0: drop_glue
    // For mut-ref captures we also remember the scalar value-type so the
    // closure body can load/store through the pointer with the right load
    // result type.
    std::vector<mlir::Type> cap_value_types(captures.size());
    // Inline aggregate value-type for an owned (moved-in) capture.
    auto inline_cap_type = [&](TypeRef ct) -> mlir::Type {
        TypeRef tv{ct};
        switch (tv.kind()) {
        case LogosType::Kind::Struct:
        case LogosType::Kind::ZonedStruct: {
            auto sit = struct_types_.find(concrete_struct_name(tv));
            if (sit != struct_types_.end()) return sit->second.llvm_type;
            return logos_to_mlir(ct);
        }
        case LogosType::Kind::Array:
            return logos_to_mlir(ct);  // already an array aggregate
        case LogosType::Kind::Tuple:
            return tuple_llvm_type(ct);
        case LogosType::Kind::Enum:
            if (auto* te = resolve_tagged_enum(std::string(tv.enum_name()), tv))
                return te->llvm_type;
            return logos_to_mlir(ct);
        default:
            return logos_to_mlir(ct);
        }
    };
    for (size_t i = 0; i < capture_types.size(); ++i) {
        auto ct = capture_types[i];
        mlir::Type ft;
        if (capture_own_inline[i]) {
            // RFC-2229 phase-2: a narrow move-captured field has a FIELD-sized
            // env slot. Prefer inline_cap_type — for an aggregate field (a
            // sub-Struct / Tuple / Enum) it returns the VALUE type; logos_to_mlir
            // would return ptr_type and the memcpy size would not match the
            // slot layout. Falls back to logos_to_mlir for scalars.
            if (capture_field_ts[i]) {
                ft = inline_cap_type(capture_field_ts[i]);
                if (!ft) ft = logos_to_mlir(capture_field_ts[i]);
            } else {
                ft = inline_cap_type(ct);  // moved-in whole root
            }
        } else if (capture_is_pointer_repr[i] || capture_is_mut_ref[i])
            ft = ptr_type();
        else
            ft = logos_to_mlir(ct);
        if (!ft) ft = builder_.getI32Type();
        cap_fields.push_back(ft);
        if (capture_is_mut_ref[i]) {
            auto vt = logos_to_mlir(ct);
            if (!vt) vt = builder_.getI32Type();
            cap_value_types[i] = vt;
        }
    }
    auto cap_struct = cap_fields.empty()
        ? mlir::LLVM::LLVMStructType::getLiteral(builder_.getContext(), {builder_.getI8Type()})
        : mlir::LLVM::LLVMStructType::getLiteral(builder_.getContext(), cap_fields);

    // Build function type: (env_ptr, params...) -> ret
    llvm::SmallVector<mlir::Type> fn_params;
    fn_params.push_back(ptr_type());  // env pointer
    for (auto& [_, pt_type] : params) {
        if (pt_type && TypeRef(pt_type).kind() == LogosType::Kind::Array) {
            fn_params.push_back(ptr_type());  // arrays passed by pointer
            continue;
        }
        auto pt = logos_to_mlir(pt_type);
        if (pt) fn_params.push_back(pt);
    }
    llvm::SmallVector<mlir::Type> fn_rets;
    if (ret_t) {
        auto rt = llvm_fn_ret_type(ret_t);
        if (rt) fn_rets.push_back(rt);
    }
    // Create the closure function as llvm.func (so llvm.mlir.addressof works)
    builder_.setInsertionPointToEnd(parent_mod.getBody());
    mlir::Type llvm_ret = fn_rets.empty()
        ? mlir::LLVM::LLVMVoidType::get(builder_.getContext()) : fn_rets[0];
    auto llvm_fn_type = mlir::LLVM::LLVMFunctionType::get(llvm_ret, fn_params, false);
    auto fn = builder_.create<mlir::LLVM::LLVMFuncOp>(loc_, closure_id, llvm_fn_type);
    fn.setLinkage(mlir::LLVM::Linkage::Private);
    auto* entry = fn.addEntryBlock(builder_);
    builder_.setInsertionPointToStart(entry);

    // Save/restore mlir_gen state
    auto saved_scope       = scope_;
    auto saved_lets        = let_vars_;
    auto saved_elems       = var_elem_types_;
    auto saved_ret         = cur_ret_type_;
    auto saved_struct      = var_struct_;
    auto saved_subscript   = var_subscript_;
    auto saved_tuple       = var_tuple_;
    auto saved_te          = var_tagged_enum_;
    auto saved_te_ptr      = var_tagged_enum_ptr_;
    auto saved_local_ptrs  = var_local_ptrs_;
    auto saved_dyn_trait   = var_dyn_trait_;
    auto saved_dyn_coerced = dyn_ptr_to_handle_vars_;
    auto saved_loop_stack  = loop_stack_;
    auto saved_entry_block = cur_entry_block_;
    cur_entry_block_ = entry;
    scope_.clear(); let_vars_.clear(); var_elem_types_.clear();
    var_struct_.clear(); var_subscript_.clear();
    var_tuple_.clear(); var_tagged_enum_.clear(); var_tagged_enum_ptr_.clear();
    var_local_ptrs_.clear(); var_dyn_trait_.clear(); dyn_ptr_to_handle_vars_.clear(); loop_stack_.clear();

    bool ret_is_void = mlir::isa<mlir::LLVM::LLVMVoidType>(llvm_ret);
    cur_ret_type_ = ret_is_void ? mlir::Type{} : llvm_ret;

    // Unpack captures from env pointer (arg 0)
    auto env_ptr = entry->getArgument(0);
    for (size_t i = 0; i < captures.size(); ++i) {
        // Capture i lives at env field i+1 (field 0 is drop_glue).
        llvm::SmallVector<mlir::LLVM::GEPArg> idx{int32_t(0), int32_t(i + 1)};
        auto fp = builder_.create<mlir::LLVM::GEPOp>(
            loc_, ptr_type(), cap_struct, env_ptr, idx);
        // For an owned inline capture the env FIELD *is* the data (one level) —
        // bind directly to its address; do NOT load a pointer out of it.
        mlir::Value val;
        if (!capture_own_inline[i])
            val = builder_.create<mlir::LLVM::LoadOp>(loc_, cap_fields[i + 1], fp);
        else
            val = fp;

        TypeRef ct = capture_types[i];
        bool is_struct_cap = capture_is_struct[i];
        bool is_array_cap  = capture_is_array[i];
        bool is_tuple_cap  = capture_is_tuple[i];
        bool is_enum_cap   = capture_is_enum[i];
        bool is_dyn_cap    = capture_is_dyn[i];
        // RFC-2229 phase-2: narrow MOVE capture — env stored just the field's
        // bytes (gated on own_inline = move-narrow only). Materialise a fake
        // root struct on the closure stack with ONLY the captured field
        // populated (other fields untouched: sema guarantees the body never
        // reads them — capture_paths LCA-widens to root otherwise). scope_[root]
        // points to the fake; body's `root.field` FieldRead GEPs the field slot
        // and reads the env-deposited value uniformly.
        if (capture_field_ts[i] && capture_own_inline[i]) {
            auto skey = mlir_struct_key(ct);
            auto sit  = struct_types_.find(skey);
            std::string fpath(v.capture_path(i));
            auto dot = fpath.find('.');
            std::string rel = (dot != std::string::npos) ? fpath.substr(dot + 1) : "";
            if (sit != struct_types_.end() && !rel.empty()) {
                // Fake root struct on the closure stack; chain-GEP through
                // nested Struct fields to reach the leaf slot and memcpy the
                // env-deposited field bytes into it. Intermediate sub-struct
                // slots are uninitialised (sema guarantees the body only reads
                // the captured path — anything wider would LCA-widen the path
                // back to the root and disable this narrow code path).
                auto fake = create_entry_alloca(sit->second.llvm_type);
                if (auto fslot = gep_field_chain(fake, ct, rel)) {
                    builder_.create<mlir::LLVM::MemcpyOp>(
                        loc_, fslot, fp, size_const(capture_field_ts[i]), false);
                    scope_[captures[i]] = fake;
                    var_struct_[captures[i]] = skey;
                    continue;
                }
            }
        }
        if (is_struct_cap || is_array_cap ||
            is_tuple_cap || is_enum_cap || is_dyn_cap) {
            scope_[captures[i]] = val;
            if (is_struct_cap)
                // Use the mono-mangled concrete key (`Vec$G1$i64`), not the bare
                // name (`Vec`) — a method call on a captured GENERIC struct
                // (`v.length()` for a captured `Vec<i64>`) resolves through this
                // key; the bare name misses the registry → the body silently
                // fails to codegen (empty body → void return type mismatch).
                var_struct_[captures[i]] = mlir_struct_key(ct);
            else if (is_array_cap)
                var_subscript_[captures[i]] = logos_to_mlir(ct ? TypeRef(ct).elem() : TypeRef());
            else if (is_tuple_cap)
                var_tuple_.insert(captures[i]);
            else if (is_enum_cap)
                var_tagged_enum_.insert(captures[i]);
            else if (is_dyn_cap && ct)
                var_dyn_trait_[captures[i]] = std::string(TypeRef(ct).trait_name());
        } else if (capture_is_env_mut[i]) {
            // G149-3: `move` closure owns a mutable copy. Alias scope_[name] to
            // the ENV FIELD pointer `fp` so reads/writes go through the env's
            // own storage — the mutation persists across calls (FnMut state)
            // and never touches the outer variable.
            scope_[captures[i]] = fp;
            let_vars_.insert(captures[i]);
            var_elem_types_[captures[i]] = cap_fields[i + 1];
        } else if (capture_is_mut_ref[i]) {
            // val is a pointer to the outer alloca. Alias scope_[name] to it
            // directly so reads/writes inside the body go through the same
            // pointer the env was constructed with — mutations escape.
            scope_[captures[i]] = val;
            let_vars_.insert(captures[i]);
            var_elem_types_[captures[i]] = cap_value_types[i];
        } else {
            auto alloca = create_entry_alloca(cap_fields[i + 1]);
            builder_.create<mlir::LLVM::StoreOp>(loc_, val, alloca);
            scope_[captures[i]] = alloca;
            let_vars_.insert(captures[i]);
            var_elem_types_[captures[i]] = cap_fields[i + 1];
        }
    }

    // Bind params (starting from arg 1)
    for (size_t i = 0; i < params.size(); ++i) {
        scope_[params[i].first] = entry->getArgument(i + 1);
        register_array_param_subscript(params[i].first, params[i].second);
    }

    // Generate body (inside llvm.func — use llvm.return)
    bool saved_in_llvm = in_llvm_func_;
    in_llvm_func_ = true;
    if (body_blk) gen_block(body_blk);
    if (!is_terminated(builder_.getBlock()))
        builder_.create<mlir::LLVM::ReturnOp>(loc_, mlir::ValueRange{});
    in_llvm_func_ = saved_in_llvm;

    // Restore state
    scope_              = saved_scope;
    let_vars_           = saved_lets;
    var_elem_types_     = saved_elems;
    cur_ret_type_       = saved_ret;
    var_struct_         = saved_struct;
    var_subscript_      = saved_subscript;
    var_tuple_          = saved_tuple;
    var_tagged_enum_    = saved_te;
    var_tagged_enum_ptr_ = saved_te_ptr;
    var_local_ptrs_     = saved_local_ptrs;
    var_dyn_trait_      = saved_dyn_trait;
    dyn_ptr_to_handle_vars_ = saved_dyn_coerced;
    loop_stack_         = saved_loop_stack;
    cur_entry_block_    = saved_entry_block;
    builder_.restoreInsertionPoint(save_pt);

    // At the creation site: allocate the capture struct, store captures.
    // G167-3b: an ESCAPING closure (one that is boxed) must heap-allocate its
    // env — the fat value `{fn, env_ptr}` outlives this frame, so a stack
    // `alloca` env would dangle. Non-escaping closures keep the cheap stack
    // env (the common iterator-adapter / local-callback case). Empty-env
    // closures stay on the stack regardless (nothing to outlive).
    bool heap_env = v.escapes() && !captures.empty();
    mlir::Value env_alloca;
    // A closure with NO captures needs no env at all — the body reads nothing
    // from it. Use a NULL env pointer so the (potentially escaping/boxed)
    // closure's drop is a clean no-op (the env-null guard short-circuits) and
    // no dangling stack env is ever read. (Previously a stack `{glue}` env was
    // allocated even for capture-less closures; once boxed+dropped its drop
    // read a dangling stack slot → SIGSEGV.)
    if (captures.empty()) {
        auto null_env = builder_.create<mlir::LLVM::ZeroOp>(loc_, ptr_type());
        auto ctype = closure_llvm_type();
        auto closure_alloca = create_entry_alloca(ctype);
        auto fn_addr = builder_.create<mlir::LLVM::AddressOfOp>(
            loc_, ptr_type(), closure_id);
        llvm::SmallVector<mlir::LLVM::GEPArg> fi{int32_t(0), int32_t(0)};
        auto fp0 = builder_.create<mlir::LLVM::GEPOp>(loc_, ptr_type(), ctype, closure_alloca, fi);
        builder_.create<mlir::LLVM::StoreOp>(loc_, fn_addr, fp0);
        llvm::SmallVector<mlir::LLVM::GEPArg> ei{int32_t(0), int32_t(1)};
        auto ep0 = builder_.create<mlir::LLVM::GEPOp>(loc_, ptr_type(), ctype, closure_alloca, ei);
        builder_.create<mlir::LLVM::StoreOp>(loc_, null_env, ep0);
        return closure_alloca;
    }
    if (heap_env) {
        auto sz = sizeof_struct(cap_struct);
        env_alloca = call_malloc(sz);
        if (!env_alloca) env_alloca = create_entry_alloca(cap_struct);
    } else {
        env_alloca = create_entry_alloca(cap_struct);
    }

    // Decide whether this closure needs a drop glue: it does if the env is
    // HEAP (escaping → the malloc'd env must be freed) OR any capture owns a
    // droppable VALUE (a `move` by-value capture of a String/Vec/etc. that the
    // closure now owns and must drop). A by-ref / pointer-repr / mut-ref
    // capture does NOT own its target → not dropped here.
    bool need_glue = heap_env;
    std::vector<bool> capture_drops(captures.size(), false);
    for (size_t i = 0; i < captures.size(); ++i) {
        // Only an owned by-VALUE capture is dropped. Pointer-repr categories
        // (struct/array/tuple/enum/dyn) stored a pointer COPY borrowed
        // from the outer owner — dropping here would double-free. A mut_ref
        // capture borrows. An env_mut capture owns a scalar copy (Copy → no
        // drop). So a droppable owned capture is a non-pointer-repr,
        // non-mut-ref capture whose type needs drop AND the closure is `move`
        // (a non-move closure borrows; only `move` transfers ownership).
        if (!v.is_move()) continue;
        // An owned inline (moved-in) struct/array/tuple/enum capture: the env
        // OWNS the value, so the env-glue drops it (and the original scope does
        // NOT — sema's matching ownership transfer). Other pointer-repr / mut-ref
        // captures borrow and are not dropped here.
        if (!capture_own_inline[i]) {
            if (capture_is_pointer_repr[i] || capture_is_mut_ref[i]) continue;
        }
        // RFC-2229 phase-2: for a narrow capture the env owns the FIELD value
        // only; its dropability is the FIELD's, not the root's.
        TypeRef drop_t = capture_field_ts[i] ? capture_field_ts[i] : capture_types[i];
        if (drop_t && value_needs_drop(drop_t)) {
            capture_drops[i] = true;
            need_glue = true;
        }
    }

    // Store the drop glue symbol address (or null) into env field 0.
    mlir::Value glue_val;
    if (need_glue) {
        std::string glue_sym = emit_closure_drop_glue(
            closure_id, cap_struct, captures, capture_types, capture_field_ts,
            capture_drops, heap_env);
        glue_val = builder_.create<mlir::LLVM::AddressOfOp>(
            loc_, ptr_type(), glue_sym);
    } else {
        glue_val = builder_.create<mlir::LLVM::ZeroOp>(loc_, ptr_type());
    }
    {
        llvm::SmallVector<mlir::LLVM::GEPArg> gi{int32_t(0), int32_t(0)};
        auto gp = builder_.create<mlir::LLVM::GEPOp>(
            loc_, ptr_type(), cap_struct, env_alloca, gi);
        builder_.create<mlir::LLVM::StoreOp>(loc_, glue_val, gp);
    }

    for (size_t i = 0; i < captures.size(); ++i) {
        auto it = scope_.find(captures[i]);
        if (it == scope_.end()) continue;
        mlir::Value cap_val;
        bool pointer_repr = capture_is_pointer_repr[i];
        bool mut_ref      = capture_is_mut_ref[i];
        auto eit = var_elem_types_.find(captures[i]);
        if (capture_own_inline[i]) {
            // Owned move-in: memcpy the capture VALUE into the inline env field.
            // it->second is the outer var's data address (one level).
            llvm::SmallVector<mlir::LLVM::GEPArg> oidx{int32_t(0), int32_t(i + 1)};
            auto dst = builder_.create<mlir::LLVM::GEPOp>(
                loc_, ptr_type(), cap_struct, env_alloca, oidx);
            mlir::Value src = it->second;
            mlir::Value sz  = size_const(capture_types[i]);
            // RFC-2229 phase-2: narrow capture — source = chain-GEP into the
            // outer struct to reach the LEAF field (handles multi-level paths
            // `p.x.y.z` via nested Struct fields); size = leaf's size (move
            // only the leaf, not whole `p`).
            if (capture_field_ts[i]) {
                std::string fpath(v.capture_path(i));
                auto dot = fpath.find('.');
                std::string rel = (dot != std::string::npos) ? fpath.substr(dot + 1) : "";
                if (auto fp = gep_field_chain(it->second, capture_types[i], rel)) {
                    src = fp;
                    sz  = size_const(capture_field_ts[i]);
                }
            }
            builder_.create<mlir::LLVM::MemcpyOp>(
                loc_, dst, src, sz, /*isVolatile=*/false);
            continue;
        }
        if (pointer_repr)
            cap_val = it->second;
        else if (mut_ref)
            // Outer alloca pointer goes verbatim into the env field; the
            // closure body reads/writes through it.
            cap_val = it->second;
        else if (let_vars_.count(captures[i]) && eit != var_elem_types_.end())
            cap_val = builder_.create<mlir::LLVM::LoadOp>(loc_, eit->second, it->second);
        else
            cap_val = it->second;
        // Capture i lives at env field i+1 (field 0 is drop_glue).
        llvm::SmallVector<mlir::LLVM::GEPArg> idx{int32_t(0), int32_t(i + 1)};
        auto fp = builder_.create<mlir::LLVM::GEPOp>(
            loc_, ptr_type(), cap_struct, env_alloca, idx);
        builder_.create<mlir::LLVM::StoreOp>(loc_, cap_val, fp);
    }

    // Build closure value: { fn_ptr, env_ptr }
    auto ctype = closure_llvm_type();
    auto closure_alloca = create_entry_alloca(ctype);
    auto fn_addr = builder_.create<mlir::LLVM::AddressOfOp>(
        loc_, ptr_type(), closure_id);
    llvm::SmallVector<mlir::LLVM::GEPArg> fi{int32_t(0), int32_t(0)};
    auto fp = builder_.create<mlir::LLVM::GEPOp>(loc_, ptr_type(), ctype, closure_alloca, fi);
    builder_.create<mlir::LLVM::StoreOp>(loc_, fn_addr, fp);
    llvm::SmallVector<mlir::LLVM::GEPArg> ei{int32_t(0), int32_t(1)};
    auto ep = builder_.create<mlir::LLVM::GEPOp>(loc_, ptr_type(), ctype, closure_alloca, ei);
    builder_.create<mlir::LLVM::StoreOp>(loc_, env_alloca, ep);

    return closure_alloca;
}

} // namespace logos::compiler
