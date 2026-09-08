name: dgall
file: src/compiler/mlir_gen_stmt.cpp
---
                // Owner (top_level) also drops the fields after the user drop
                // (mirrors SDrop). Nested: stop (by-value self consumes them).
                if (!top_level) return;
---
                // Owner (top_level) also drops the fields after the user drop
                // (mirrors SDrop). Nested: stop (by-value self consumes them).
                // PROBE dgall (2026-09-08): the post-user-drop field recursion
                // is gated on `top_level`, which DEFAULTS FALSE (mlir_gen_impl.hpp)
                // and is passed true by only two callers in the tree. The crude
                // arm removes the gate entirely, at every caller.
                if (!top_level && !logos::probe::on("dgall")) return;
===
name: dgrepl
file: src/compiler/mlir_gen_stmt.cpp
---
            gen_drop_value(it->second, val_ty);
            builder_.create<mlir::cf::BranchOp>(loc_, cont_blk);
            builder_.setInsertionPointToStart(cont_blk);
        }
        builder_.create<mlir::LLVM::StoreOp>(
            loc_, builder_.create<mlir::arith::ConstantIntOp>(loc_, 1, 8), flag_it->second);
    } else if (uninit_static_.count(name)) {
        // B8 static-uninit var: its init state is statically tracked. The FIRST
        // (dominating) assignment overwrites garbage → no drop; later ones drop
        // the live value unconditionally.
        if (uninit_assigned_.count(name)) {
            if (val_ty) gen_drop_value(it->second, val_ty);
        } else {
            uninit_assigned_.insert(name);
        }
    } else if (v.drop_old() && val_ty) {
        gen_drop_value(it->second, val_ty);
---
            gen_drop_value(it->second, val_ty, /*top_level=*/logos::probe::on("dgrepl"));
            builder_.create<mlir::cf::BranchOp>(loc_, cont_blk);
            builder_.setInsertionPointToStart(cont_blk);
        }
        builder_.create<mlir::LLVM::StoreOp>(
            loc_, builder_.create<mlir::arith::ConstantIntOp>(loc_, 1, 8), flag_it->second);
    } else if (uninit_static_.count(name)) {
        // B8 static-uninit var: its init state is statically tracked. The FIRST
        // (dominating) assignment overwrites garbage → no drop; later ones drop
        // the live value unconditionally.
        if (uninit_assigned_.count(name)) {
            if (val_ty) gen_drop_value(it->second, val_ty, /*top_level=*/logos::probe::on("dgrepl"));
        } else {
            uninit_assigned_.insert(name);
        }
    } else if (v.drop_old() && val_ty) {
        gen_drop_value(it->second, val_ty, /*top_level=*/logos::probe::on("dgrepl"));
===
name: dgnest
file: src/compiler/mlir_gen_stmt.cpp
---
                    gen_drop_value(fp, ft, /*top_level=*/false,
                                   child_skips.empty() ? nullptr : &child_skips);
                }
            }
        } else if (k == K::Tuple) {
            auto ttype = tuple_llvm_type(st);
            auto elems = st.tuple_elems();
            if (ttype)
                for (int i = (int)elems.size() - 1; i >= 0; --i) {
                    std::set<std::string> child_skips;
                    if (split_skip_paths(&moved, std::to_string(i), child_skips)) continue;
                    TypeRef et(elems[i]);
                    auto ek = et ? TypeRef(et).kind() : K::Error;
                    if (!et || ek == K::Ref || ek == K::MutRef || ek == K::Ptr) continue;
                    if (!value_needs_drop(et)) continue;
                    llvm::SmallVector<mlir::LLVM::GEPArg> gi{int32_t(0), int32_t(i)};
                    auto gep = builder_.create<mlir::LLVM::GEPOp>(loc_, ptr_type(), ttype, it->second, gi);
                    // Enum value-repr: a nested enum element is inline — drop on the GEP.
                    gen_drop_value(gep, et, /*top_level=*/false,
                                   child_skips.empty() ? nullptr : &child_skips);
---
                    gen_drop_value(fp, ft, /*top_level=*/logos::probe::on("dgnest"),
                                   child_skips.empty() ? nullptr : &child_skips);
                }
            }
        } else if (k == K::Tuple) {
            auto ttype = tuple_llvm_type(st);
            auto elems = st.tuple_elems();
            if (ttype)
                for (int i = (int)elems.size() - 1; i >= 0; --i) {
                    std::set<std::string> child_skips;
                    if (split_skip_paths(&moved, std::to_string(i), child_skips)) continue;
                    TypeRef et(elems[i]);
                    auto ek = et ? TypeRef(et).kind() : K::Error;
                    if (!et || ek == K::Ref || ek == K::MutRef || ek == K::Ptr) continue;
                    if (!value_needs_drop(et)) continue;
                    llvm::SmallVector<mlir::LLVM::GEPArg> gi{int32_t(0), int32_t(i)};
                    auto gep = builder_.create<mlir::LLVM::GEPOp>(loc_, ptr_type(), ttype, it->second, gi);
                    // Enum value-repr: a nested enum element is inline — drop on the GEP.
                    gen_drop_value(gep, et, /*top_level=*/logos::probe::on("dgnest"),
                                   child_skips.empty() ? nullptr : &child_skips);
===
name: rcunsz
file: src/compiler/sema_expr.cpp
---
    std::string fname0(ssi->fields[0].name);
    auto fr = builder().field_read(std::move(e), fname0, sft[0]);
---
    std::string fname0(ssi->fields[0].name);
    // PROBE rcunsz (2026-09-08): this rebuild consumes its operand by FIELD
    // READ and never records the move, so the source binding stays live and
    // is dropped a second time. Box's unsize DOES record it (control).
    if (logos::probe::on("rcunsz")) mark_moved_expr(expr_ref_of(e));
    auto fr = builder().field_read(std::move(e), fname0, sft[0]);
===
