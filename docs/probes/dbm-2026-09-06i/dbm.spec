name: dbmtup
file: src/compiler/sema_stmt.cpp
---
            if (default_ref && et && TypeRef(et).kind() != LogosType::Kind::Error &&
                TypeRef(et).kind() != LogosType::Kind::TypeVar && is_move_type(et))
                et = make_ref(default_mut, et);
---
            // PROBE dbmtup / dbmall (2026-09-06i): the default-binding-mode wrap
            // is gated on is_move_type — installed as a double-free guard, wrong
            // as a MODE rule. The crude arm drops the carve-out at the TUPLE door
            // so a Copy element under a `&`/`&mut` scrutinee binds by reference.
            if (default_ref && et && TypeRef(et).kind() != LogosType::Kind::Error &&
                TypeRef(et).kind() != LogosType::Kind::TypeVar &&
                (is_move_type(et) || logos::probe::on("dbmtup") ||
                 logos::probe::on("dbmall")))
                et = make_ref(default_mut, et);
===
name: dbmstr
file: src/compiler/sema_stmt.cpp
---
                if (default_ref && ftype &&
                    TypeRef(ftype).kind() != LogosType::Kind::Error &&
                    TypeRef(ftype).kind() != LogosType::Kind::TypeVar &&
                    is_move_type(ftype))
                    bt = make_ref(default_mut, ftype);
---
                // PROBE dbmstr / dbmall (2026-09-06i): same carve-out, STRUCT door.
                if (default_ref && ftype &&
                    TypeRef(ftype).kind() != LogosType::Kind::Error &&
                    TypeRef(ftype).kind() != LogosType::Kind::TypeVar &&
                    (is_move_type(ftype) || logos::probe::on("dbmstr") ||
                     logos::probe::on("dbmall")))
                    bt = make_ref(default_mut, ftype);
===
name: dbmslc
file: src/compiler/sema_stmt.cpp
---
        if (sl_default_ref && elem_t &&
            TypeRef(elem_t).kind() != LogosType::Kind::Error &&
            TypeRef(elem_t).kind() != LogosType::Kind::TypeVar &&
            is_move_type(elem_t))
            elem_t = make_ref(sl_default_mut, elem_t);
---
        // PROBE dbmslc / dbmall (2026-09-06i): same carve-out, SLICE door.
        if (sl_default_ref && elem_t &&
            TypeRef(elem_t).kind() != LogosType::Kind::Error &&
            TypeRef(elem_t).kind() != LogosType::Kind::TypeVar &&
            (is_move_type(elem_t) || logos::probe::on("dbmslc") ||
             logos::probe::on("dbmall")))
            elem_t = make_ref(sl_default_mut, elem_t);
===
