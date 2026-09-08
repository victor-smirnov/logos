# ROUND 2026-09-08 — PRICING THE THREE VALGRIND-MINTED ROWS
# build read: 8e5f92705b285d29 43 (identical to the build all three rows cite)
# queue gate rc 0 · 73 rows / 73 programs / # TOTAL 73

## 1. CONTROLS RE-VERIFIED — all three rows still reproduce, digit for digit

replace_site_skips_field_drop_glue   rc 11 (recorded 11)
rc_coerce_unsized_source_not_moved   rc 0, silent compile (recorded)
unsized_local_binds_place_dropped…   rc 0, 2 invalid reads, same two stacks

Row 1's objdump table reproduces EXACTLY as recorded:
  s_scope 1/1 · s_ifexpr 3/3 · s_assign 2/1 · s_field 2/0 · s_nested 1/0
Row 2's three controls reproduce: no-coercion Rc 0 errors · Box unsize 0 errors ·
Rc unsize 3 invalid accesses.
Row 3's fixture tests/logos/pass/custom_dst_smartptr_owning_drop is still pinned
green and still commits the use-after-free (2 invalid reads today).

## 2. THE ROOT, LOCATED — ONE DEFAULTED PARAMETER, NOT A MISSING ARM

`MLIRGenImpl::gen_drop_value(value_ptr, ty, bool top_level = false, …)`
(declared mlir_gen_impl.hpp:1973). The arm that recurses a value's fields AFTER
its user `Drop::drop` already exists and already works — mlir_gen_stmt.cpp:990,
`if (!top_level) return;`. TWO callers in the whole tree pass true
(mlir_gen_dyn.cpp:1018, :1067). Every other drop-emission site defaults to false
and therefore calls the destructor and STOPS.

⚠ AND ITS OWN COMMENT AT mlir_gen_stmt.cpp:2444 IS FALSE AT THE DEFAULT:
"gen_drop_value runs the full destructor (user Drop impl + owned children)" —
the three calls under it pass no third argument.

## 3. THE FAILING POPULATION IS MUCH WIDER THAN THE ROW RECORDS (rule 5)

Eight shapes, destructor-count oracle (a leak undercounts, a double drop
overcounts, so one oracle reads both directions). BASE, want vs got:
  1 two droppable fields, scope exit   want 2  got 0   LEAK
  2 two levels of nesting              want 1  got 0   LEAK
  3 TUPLE element, scope exit          want 1  got 0   LEAK   <- not in the row
  4 ARRAY element, scope exit          want 2  got 0   LEAK   <- not in the row
  5 assignment over a 2-level field    want 2  got 0   LEAK   <- not in the row
  6 assignment over a tuple element    want 2  got 0   LEAK   <- not in the row
  7 field moved out, then dropped      want 1  got 1   OK
  8 plain top-level local              want 1  got 1   OK
The row named three shapes; the property names at least six.

⚠ AND THE ENUM ARM IS WORSE. An enum WITH a user Drop and an owning payload
leaks at EVERY site including the top-level local (want 1, got 0, all three
spellings) — because the scope-exit path's enum branch is guarded
`else if (k == K::Enum && drop_fn.empty())` (mlir_gen_stmt.cpp:1363), so a user
drop_fn disables the payload recursion before `top_level` is ever consulted.
This is NOT enum_payload_partial_move_leak (that row is a partial move in a
match arm); it has no row. NEW.

## 4. THE PREMISE THE FIX MUST NOT ASSUME — measured, and it REFUTES the one-liner

The comment at mlir_gen_stmt.cpp:977 justifies the `top_level` gate: "a by-value
`self` drop consumes the fields, which drop at the drop body's scope end".
That premise is sometimes TRUE and sometimes FALSE, and the compiler is wrong in
BOTH directions today. Two drop bodies, one empty (P1), one that moves the field
out into a local (P2); each shape owes exactly ONE inner destructor call:

                                    BASE   dgall   dgnest   dgrepl
  P1 local        (want 1)            1      1        1        1
  P2 local        (want 1)            2      2        2        2   <- DOUBLE DROP today
  P1 as a field   (want 1)            0      1        1        0
  P2 as a field   (want 1)            1      2        2        1   <- dgall/dgnest CREATE one

So: P2-as-a-local ALREADY double-drops on the unmodified compiler (a new,
unrowed tier-1 defect), and arming the recursion unconditionally converts
P2-as-a-field from correct to a double drop. A fix that only flips the
predicate trades a leak class for a double-free class. The recursion has to
consult what the drop BODY consumed, or the body has to own its own fields'
drops — which is one design decision, not two.

## 5. A PINNED PASS FIXTURE ASSERTS THE DEFECT — CORPUS DECISION, OWNER'S CALL

tests/logos/pass/drop_glue_three_levels — C{nested:B}, B has a user Drop and an
A field with a user Drop. Its OWN COMMENT says
    "c drops via glue → B::drop(b3) → A::drop(a7)"
and its `.expected` says
    stdout: b3
MEASURED today, same program, one binary:
    BASE "b3"   dgrepl "b3"   dgall "b3 a7"   dgnest "b3 a7"
The fixture pins the leak as the rule, and its comment says the opposite. Its
sibling tests/logos/pass/drop_nested_explicit pins the CORRECT behaviour
("o9 i2 i1") one level up, where `top_level` is true. Two green pass fixtures in
one family, contradictory expectations.
⚠ NOT EDITED. Closing replace_site_skips_field_drop_glue REDS this fixture, and
the repair is to change its `.expected` to `b3 a7` — which is what its own
comment already asks for. That is the owner's call, not this round's.

## 6. THE STANDING ROW'S SCOPE — CORRECTION, MEASURED

boxed_move_closure_fat_capture_env_overflow names three corpus members. On
today's binary, by valgrind stack:
  * bc_objlt_str_literal — 2 errors, both inside `bc_objlt_str_literal$f`, the
    boxed-closure function. ITS OWN MECHANISM. Correct member.
  * custom_dst_smartptr_owning_drop — 2 errors, `__drop_in_place__A` and
    `MyRc$G1$udyn_Tr__drop_me` reading a block that same function freed. No
    closure, no boxed env. That is unsized_local_binds_place_dropped_after_free.
  * tests/spec/pass/coerce_4 — errors in `Rc$G1$A__drop` (the SOURCE binding's
    scope-exit drop, in main) on a block freed by `Rc$G1$udyn_Sp__drop_rc`
    inside take_rc_dyn. No closure either. That is
    rc_coerce_unsized_source_not_moved, and the rcunsz probe REPAIRS it to 0
    valgrind errors, which settles the attribution.
Two of the three members belong to other rows. Recorded here for that row's
owner; the row itself is NOT re-scoped by this round.

## 7. THE PROBE TABLE — 4 probes, ONE build, every column read

probe    fires  ceiling  cost  cfail  stdlib  RUNTIME (6553 pass fixtures RUN)
dgall    15729     0        1     0     ok    1 changed (+cast-region-to-uint)
dgrepl     552     0        0     0     ok    not run (bounded by dgall)
dgnest    4022     0        1     0     ok    not run (bounded by dgall)
rcunsz      43     0        0     0     ok    0 changed (+cast-region-to-uint)

⚠ THE `ceiling` COLUMN IS 0 FOR ALL FOUR AND THAT IS NOT A REFUTATION. It counts
`bc_admits.ledger` rows closed; none of these four defects is a bc row — they are
soundness-queue rows. probe-batch's "STOP cost>=ceiling" on dgall/dgnest is the
formula meeting a column that is structurally blind to this ledger, not a verdict.
All four are PROVEN LIVE (fires 552 … 15729).

COST, NAMED (rule: diff BOTH ways, name every member):
  dgall  cost 1  = logos_03_ownership_pass_drop_glue_three_levels
  dgnest cost 1  = logos_03_ownership_pass_drop_glue_three_levels  (the SAME one)
  dgrepl cost 0, rcunsz cost 0
RUNTIME, NAMED, base vs armed over 6553 compiled+linked+RUN pass fixtures, both
directions, no fixture appearing on only one side:
  dgall  : drop_glue_three_levels (stdout sha; rc unchanged 42)
           + cast-region-to-uint (prints a stack address — subtracted by name)
  rcunsz : cast-region-to-uint ONLY. Zero real changes.

## 8. ADDITIVITY — MEASURED, AND IT IS NOT ADDITIVE (rule 13 again)

On the eight-shape destructor oracle, shapes CLOSED out of the six wrong ones:
  dgrepl  0/6   (it does close the ROW's program, rc 11 -> 0: an assignment
                 over a LOCAL is the one site it arms)
  dgnest  3/6   (shapes 1, 3, and half of 6)
  dgall   6/6
  dgrepl + dgnest = 3.  dgall = 6.  The increment from combining is +3, and it
  comes from sites NEITHER site-probe touches: the deep recursion at
  mlir_gen_stmt.cpp:1024, the array branch, and :1042.
On the row's own objdump table:
  dgrepl  s_assign 2/2 fixed, s_field 2/0 and s_nested 1/0 UNCHANGED
  dgnest  s_nested 1/1 and s_field 2/1 fixed, s_assign 2/1 UNCHANGED
  dgall   all three 2/2, 2/2, 1/1
=> THE MINTING ROUND'S OPEN QUESTION IS ANSWERED: the three failing shapes are
NOT one site and NOT three roots. They are ONE PREDICATE reaching at least FIVE
emission sites, no two of which any single site-fix moves together. Blame is per
site; credit is per set; and the set is larger than the row.

## 9. WHAT DESERVES FUNDING

FUND: rcunsz — `try_struct_unsize_coerce` (sema_expr.cpp:935) must record the
move of its operand before it rebuilds it. It is one line at THE shared helper,
so all five coercion points (sema_expr.cpp:1001, 5294, 5354, 15090, 15455)
inherit it. Measured: closes rc_coerce_unsized_source_not_moved with the exact
rustc-equivalent sentence "use of moved variable 'rc'" (READ, and identical to
what the Box control already prints); repairs tests/spec/pass/coerce_4 from 9
valgrind errors to 0; cost 0, cfail 0, stdlib ok, runtime 0 of 6553. The
over-refusal controls all still compile and run clean: the coercion of a
temporary, the no-coercion Rc call, and the Box unsize.
⚠ THE CRUDE ARM IS NOT THE FIX (rule 7). It marks the operand moved for EVERY
CoerceUnsized, including one whose operand is not a place; that is a no-op today
because mark_moved_expr self-gates, but the landing must say so deliberately.

DO NOT FUND YET: dgall / a `top_level` flip. Its corpus price is one fixture and
its runtime price is zero, which is as cheap as this queue gets — and it is
still the WRONG FIX, because the premise test shows it converts a correct shape
(a drop body that moves its own field out, as a FIELD) into a double drop, and
the corpus contains no program of that shape to say so. Fund the DESIGN question
first: does the drop BODY own its fields' drops, or does the call site recurse
minus what the body consumed? One decision closes
replace_site_skips_field_drop_glue, the four unrowed leak shapes, the unrowed
enum shape, and the unrowed P2-local double drop. Until it is decided, a
one-line flip trades a leak class for a double-free class.

BLOCKED ON AN OWNER (report only, nothing edited):
  * tests/logos/pass/drop_glue_three_levels — `.expected` "b3" contradicts the
    fixture's own comment; the repair is "b3 a7".
  * tests/logos/pass/custom_dst_smartptr_owning_drop — pinned green, commits a
    use-after-free (row 3).
  * boxed_move_closure_fat_capture_env_overflow's member list — two of three
    members belong to other rows.

NEW, UNROWED, FOUND THIS ROUND (candidates for minting, not minted here):
  * an enum with a user Drop and an owning payload leaks at EVERY site,
    including the top-level local (mlir_gen_stmt.cpp:1363, `&& drop_fn.empty()`)
  * a struct whose user drop body MOVES a field out double-drops that field
    when the value is a top-level local (BASE: want 1, got 2)
  * tuple-element, array-element, two-level-nesting and nested-field-assignment
    leaks — four shapes of replace_site_skips_field_drop_glue's property that
    the row's text does not mention
