# PREDICTION — written 2026-09-08 BEFORE any compiler edit
# build read at declaration time: 8e5f92705b285d29 43
# queue gate rc 0 · 73 rows / 73 programs / # TOTAL 73

## THE CLASS, BY THE PROPERTY (not by the spelling)

PROPERTY: a sema COERCION REWRITE that replaces a by-value MOVE-TYPE operand
`e` with a SYNTHESIZED node (`StructLit(FieldRead(e))` or `Cast(e)`), after
which the consumer's `mark_moved_expr(e)` sees the synthesized node — whose
kind is neither VarRef nor FieldRead nor TupleIndex nor IndexRead — and
therefore RECORDS NOTHING. The source stays live: use-after-move is admitted
and the source's scope-exit drop runs on storage the coercion result owns.

THE CLASS ALREADY HAS ONE MEMBER FIXED IN THIS TREE, and it names the failure
itself: sema_expr.cpp `expect_type`, CoercePos::Return, `Box<Concrete> ->
Box<dyn Trait>` — `mark_moved_expr(expr_ref_of(e)); e = builder().cast(...)`,
commented "else codegen gets a mis-keyed vtable AND an un-consumed Box (a
double free)". Mark-before-rebuild is the tree's own idiom; it was applied at
ONE position out of the rewrites that need it.

ENUMERATION of the rewrite sites that consume a by-value operand:
  R1 try_struct_unsize_coerce   sema_expr.cpp:935   NO mark   <- 6 call sites
  R2 coerce_dyn_upcast          sema_expr.cpp:~15444 NO mark
  R3 coerce_arg_to_dyn          sema_expr.cpp:~15466 NO mark
  R4 expect_type Return/Box     sema_expr.cpp:~15042 HAS mark (the precedent)
R1's six call sites: sema_expr.cpp:1001 (explicit `as`), 5294, 5354 (call arg,
two arms), 15090 (coerce_arg_to_param), 15455 (coerce_arg_to_dyn), and
sema_stmt.cpp:4945 (apply_place_coercions — a `let`/assignment target). The
prior round's report named FIVE and missed sema_stmt.cpp:4945.

## MEASURED AT BASE — the class is 8 admitted illegal programs, not 1

sandbox /home/logos/sandbox/rcunsz, build/bin/logosc, LOGOS_LIB_DIR=build/lib/logos
  c1_as_let       explicit `as` in a let, source reused        COMPILES  (illegal)
  c2_callarg      implicit coercion at a fn-call argument      COMPILES  (illegal)
  c3_let_implicit implicit coercion at a typed `let`           COMPILES  (illegal)
  c5_field_src    source is a STRUCT FIELD, moved out          COMPILES  (illegal)
  c8_structlit    coercion in a struct-literal FIELD value     COMPILES  (illegal)
  c9_enumpayload  coercion in an ENUM PAYLOAD                  COMPILES  (illegal)
  c11_assign      coercion on an ASSIGNMENT rhs                COMPILES  (illegal)
  c12_upcast      Box<dyn Ext> -> Box<dyn Base> UPCAST, reused COMPILES  (illegal)
  c6_box_ctl      Box<A> -> Box<dyn Sp>, reused                REFUSED "use of moved
                                                               variable 'b'"  <- R4-adjacent
                                                               control, already correct
c12 is the R2 sibling and is NOT an `Rc` program at all — it is what proves the
class is the PROPERTY and not "Rc by name".

## PREDICTED AFTER THE CHANGE (mark-before-rebuild at R1, R2, R3)

REFUSED, each with "use of moved variable '<src>'" — the exact sentence the R4
control already prints — READ, not inferred:
  c1 c2 c3 c8 c9 c11 c12    (7 rows of the class)
  c5_field_src: predicted refused as a PARTIAL move of `h.r`; the sentence may
  differ (mark_moved_expr's FieldRead arm records a PATH, not a name) — READ IT.
STILL COMPILING AND RUNNING, exit 0 (over-refusal controls, varied in shape):
  n1_temp        coercion of a TEMPORARY (no place at all)
  n2_nouse       coercion, source never touched again
  n3_use_before  source used BEFORE the coercion
  n4_clone       the CLONE is coerced, the original stays live
  n5_two         two separate bindings, one coerced, the other used
  n6_loop        a coercion inside a while loop over fresh values
  c6_box_ctl     unchanged (already refused, same sentence)
  c4_return      `return rc;` coerced at the return — legal, must still run

CORPUS PREDICTION, declared as a NUMBER AND A LIST:
  ceiling-probe / gate cost:  0 fixtures  (the prior round measured the R1
    one-liner at cost 0, cfail 0, stdlib ok, runtime 0 of 6553). R2 and R3 are
    NEW this round and are the reason the number could move; if it does, the
    members will be named both ways.
  bc_admits rows closed: 0 (this is a soundness-queue defect, not a bc row).
  soundness-queue rows CLOSED: exactly ONE —
      rc_coerce_unsized_source_not_moved
  soundness-queue rows NOT touched, DECLINED by name in the report:
      replace_site_skips_field_drop_glue
      unsized_local_binds_place_dropped_after_free
  new # TOTAL predicted: 73 - 1 + (new rows minted) — re-derived by direct
  listing at the commit, never by arithmetic in this file.

## A SEPARATE FINDING, ALREADY MEASURED, NOT PART OF THIS CLASS
A METHOD whose by-value parameter is an unsized wrapper struct is NOT FOUND at
all: `impl Holder { fn eat(&self, r: Rc<dyn Sp>) -> i64 }` + `h.eat(rc)` gives
"method call: 'Holder' has no method 'eat'". The byte-identical method with
`r: Rc<A>` IS found (and correctly reports "use of moved variable 'rc'"), and
the same signature as a FREE function is found. That is an over-refusal of a
legal program, it is not a coercion-move defect, and it gets its own row.
