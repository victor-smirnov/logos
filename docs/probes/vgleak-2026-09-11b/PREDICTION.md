# PREDICTION — declared BEFORE the edit, build 19ff93338332ba46 43

## The change (one site)

`SemaChecker::mark_moved_expr`, sema_impl.hpp:4497 — the VarRef arm consults the
NAME-keyed ownership bit (`lookup_owning_dyn(nm)`) without asking the
expression's OWN type. `&b` on a `b: Box<dyn Tr>` local lowers (sema_expr.cpp
~3170) to a bare `VarRef("b")` RE-TYPED `TraitObject(OwningKind::Borrow)` — the
AddrOf is gone, because an owning trait object and a borrowed one are the same
fat pair. So every consumer that marks that VarRef moved consumes a BORROW.

Gate the whole VarRef arm on: the expression's own type is not a BORROWED
trait object.

## Class, enumerated BY PROPERTY (tools/dlog, `ownfact_reads.dl`)

Reads of the name-keyed bit (`VarInfo::owning_dyn` field + `lookup_owning_dyn`)
over sema.cpp / sema_expr.cpp / sema_decl.cpp / sema_stmt.cpp: **6**.
Cross-checked against the type-keyed predicate in the same context: **4**
(`make_drop_stmt` sema.cpp:4020, `cond_move_flag_for` sema_impl.hpp:4190, and the
two WRITERS `lower_fn` sema_decl.cpp:1427 / `lower_let` sema_stmt.cpp:2901).
NOT cross-checked: **2** — `lookup_owning_dyn` itself (the accessor, i.e. the
definition of the name-keyed read) and `mark_moved_expr` sema_impl.hpp:4497,
**the only decision site**. Class size = 1.

## Predicted rows closed: 1

  * `boxdyn_arg_deref_borrow_kills_box_drop` (tier 1, `run 1`) — rc 1 -> 0,
    destructor count 0 -> 1, 16 bytes -> 0.

No other soundness_queue row is predicted to change. No bc_admits row.

## Predicted behaviour changes (hand programs, /tmp/claude-1004/lk)

Defect, base -> fixed:
  * t1  free-fn `&dyn` arg + later `b.v()`   : REFUSED "use of moved variable 'b'" -> compiles, rc 0
  * t3  METHOD `&dyn` arg                     : rc 1, 16 b lost -> rc 0, 0 b
  * t6  dyn UPCAST at a `&dyn` arg            : rc 1 -> rc 0
  * t11 two `&dyn` args                       : rc 1 -> rc 0

Controls that MUST NOT move (all recorded on the base binary):
  * c1 by-value `Box<dyn>` arg, source reused : refused "use of moved variable 'b'"
  * c2 `let c: Box<dyn> = b;` source reused   : refused "use of moved variable 'b'"
  * c3 `Rc<A>`->`Rc<dyn>` CoerceUnsized (R1)  : refused "use of moved variable 'r'"
  * c4 by-value dyn UPCAST (R2), reused       : refused "use of moved variable 'b'"
  * c5 by-value `Box<dyn>` arg, no reuse      : rc 0, one destructor
  * t2 `let r: &dyn Sp = &b;`                 : rc 0, clean
  * t10 `&Concrete` -> `&dyn`                 : rc 0, clean
  * p5 struct-literal field `&dyn` from `&b`  : rc 0, clean

Off-class, NOT this change (recorded, both pre-existing):
  * t5 `&mut b` -> `&mut dyn Sp` : mlir_gen "no vtable for '&dyn Sp' as '&dyn Sp'"
  * p4 `Rc<dyn Sp>` local, `&b`  : mlir_gen "no vtable for 'Rc$G1$udyn_Sp' as '&dyn Sp'"
