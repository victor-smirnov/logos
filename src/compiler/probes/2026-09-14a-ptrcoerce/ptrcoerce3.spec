name: ebpaddrof
file: src/compiler/borrow_check.cpp
---
        for (auto it = path_parts.rbegin(); it != path_parts.rend(); ++it) {
            if (!bp.path.empty()) bp.path.push_back('.');
            bp.path.append(*it);
        }
    }
    return bp;
}
---
        for (auto it = path_parts.rbegin(); it != path_parts.rend(); ++it) {
            if (!bp.path.empty()) bp.path.push_back('.');
            bp.path.append(*it);
        }
    }
    // PROBES 2026-09-14a-ptrcoerce batch 3: ebpaddrof / ebpaddrofmut — the walker has no arm for Code::AddrOf (an
    // explicit `&v` / `&mut v` sema builds, e.g. try_index_mut_assign's receiver), so every one of its consumers gets an
    // EMPTY root for it and check_recv_conflict / record_borrow return on their first line. Root it at the variable —
    // the repair by DELEGATION, reaching all consumers at once (batch 2's aorecvsk repaired one).
    if (cur && cur.kind() == Code::AddrOf) {
        TypeRef aot_ = cur.type(pool);
        bool aomut_ = aot_ && aot_.kind() == LogosType::Kind::MutRef;
        logos::probe::census(aomut_ ? "ebp.addrof.mut" : "ebp.addrof.shared");
        if (logos::probe::on("ebpaddrof") || (aomut_ && logos::probe::on("ebpaddrofmut"))) {
            bp.root = std::string(EAddrOfView{cur}.var_name());
            bp.root_type = aot_ ? TypeRef(aot_).pointee() : TypeRef(nullptr);
            for (auto it = path_parts.rbegin(); it != path_parts.rend(); ++it) {
                if (!bp.path.empty()) bp.path.push_back('.');
                bp.path.append(*it);
            }
        }
    }
    return bp;
}
===
name: ebpaddrofmut
file: src/compiler/borrow_check.cpp
---
// Merge Phase-1 move state from 'other' into 'base' (union of moved sets).
---
// (PROBES 2026-09-14a-ptrcoerce batch 3: `ebpaddrofmut` arms the AddrOf root in extract_borrow_place for `&mut` only.)
// Merge Phase-1 move state from 'other' into 'base' (union of moved sets).
