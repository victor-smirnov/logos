name: slicemv
file: src/compiler/sema_stmt.cpp
---
        case ps::Code::Slice:
            // An array / slice pattern moves ELEMENTS, and those are partial
            // moves the slice-pattern lowering and the borrow checker already
            // record per element (`a.2`): `[.., z]` then `[w, ..]` is legal.
            // A whole-array mark here would refuse it and coarsen the sentence
            // (measured: 1 pass + 4 fail fixtures).
            return false;
---
        case ps::Code::Slice: {
            // An array / slice pattern moves ELEMENTS, and those are partial
            // moves the slice-pattern lowering and the borrow checker already
            // record per element (`a.2`): `[.., z]` then `[w, ..]` is legal.
            // A whole-array mark here would refuse it and coarsen the sentence
            // (measured: 1 pass + 4 fail fixtures).
            // PROBE 2026-09-16b-armbind (slicemv / slicemvn) — see src/compiler/PROBES.md.
            lir_view::PatSliceView sv_{pr};
            TypeRef et_ = ty ? TypeRef(ty).elem() : TypeRef(nullptr);
            bool any_ = false;
            sv_.each_prefix([&](lir_view::PatRef sp){ if (!any_ && pattern_moves_out(sp, et_)) any_ = true; });
            sv_.each_suffix([&](lir_view::PatRef sp){ if (!any_ && pattern_moves_out(sp, et_)) any_ = true; });
            if (any_) {
                logos::probe::census("armbind.slice.movesout");
                if (logos::probe::on("slicemv")) return true;
                if (logos::probe::on("slicemvn") && !sv_.rest()) return true;
            }
            return false;
        }
