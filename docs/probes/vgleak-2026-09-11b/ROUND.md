# vgleak-2026-09-11b — A BORROW OF AN OWNING `Box<dyn>` IS THE OWNER'S OWN VarRef

base build `19ff93338332ba46 43` → fixed `260d74a881ec778e 43` (`scripts/build_hash.py`, READ).

## STEP 1, DERIVED

```
871d1bddd / 8decdfaa5 / eb9e83414      status: clean
# TOTAL  soundness_queue 81   bc_admits 85   bc_admits_blocked 8
probe-log-lint   271 records, every site symbol resolves
build_hash       19ff93338332ba46 43
queue gate       rc 0 — 81 rows (t1=19 t2=12 t3=42 t4=8)
```

The handed-down report's census is confirmed row for row. Its own corrections to the
2026-09-11 prompt (the closed `replace_site_skips_field_drop_glue`, the dead 09-08 leak
numbers, ENVIRONMENT-not-argv, and `LOGOS_LIB_DIR` present and working) were re-checked
against the text actually given and all four still hold. No new correction is owed.

## WHAT WAS FUNDED, AND WHAT WAS DECLINED BY NAME

FUNDED: `boxdyn_arg_deref_borrow_kills_box_drop` — the one item on the handed-down list
that is a COMPILER defect with a `run` oracle. CLOSED.

DECLINED, each with the number:

  * **`BufReader`/`BufWriter` have no `impl Drop`** — 9 records / 7 fixtures, the largest
    remaining class. Unchanged from the previous round's verdict and for the same reason:
    adding `Drop` to two public stdlib types is an API decision with an owner. Re-read this
    round: `stdlib/std/io/buffered/buffered.logos` still declares no `Drop` and still says
    "Call `close` to free the buffer". REPORTED, NOT EDITED.
  * **writ/fabric/dview stores** — 19 records / 8 fixtures. Arena-shaped, and an arena freed
    at process exit is not a leak; nothing in the tree states the contract either way, so
    closing them requires a contract decision, not a measurement. Not rowed.
  * **`deem_incr_static_retract_e2e`** — 12 records / 1 fixture, and 10 one-fixture singles.
    Needs one reduction each; no shared property found, so no class to fix.

## THE DEFECT — THE ROW'S OWN CONTROLS NAMED THE WRONG DISCRIMINATOR

The row recorded "the borrow is not the discriminator, the ARGUMENT POSITION is", because
`let r: &dyn Sp = &b;` was clean. That is a true measurement and a false root. Both spellings
lower identically: sema_expr's "&Box<dyn Trait> → borrowed &dyn Trait (Deref coercion)"
returns `var_ref("b", TraitObject(OwningKind::Borrow))` — no AddrOf node at all, because an
owning trait object and a borrowed one are the same `{data,vtable}` fat pair. The borrow IS
the owner's own VarRef, re-typed. They differ only in whether a consumer that marks moves is
downstream — a `let` has none, a call argument does.

`mark_moved_expr` (sema_impl.hpp:4497) then asked `lookup_owning_dyn(nm)`: a bit on the
BINDING, reachable only by NAME. So the borrow was marked moved, the local left
`collect_drops`, its destructor never ran and its block was never freed.

## THE CLASS, BY PROPERTY — `tools/dlog`, new rule `ownfact_reads.dl`

The ownership fact exists TWICE: name-keyed (`VarInfo::owning_dyn` / `lookup_owning_dyn`) and
type-keyed (`TypeRef::owning_trait_object` / `trait_owning_kind`). The claim checked is the
tree's own idiom — a context reading the name-keyed bit reads the type-keyed one too.
Over sema.cpp + sema_expr.cpp + sema_decl.cpp + sema_stmt.cpp:

| relation | rows |
|---|---|
| `namekeyed_read` | **6** — cond_move_flag_for :4190 · lower_fn sema_decl.cpp:1427 · lower_let sema_stmt.cpp:2901 · make_drop_stmt sema.cpp:4020 · lookup_owning_dyn :4817 · mark_moved_expr :4497 |
| `crosschecked` | **4** (the two writers + both other readers) |
| `uncrosschecked` | **2** — `lookup_owning_dyn` (the ACCESSOR, i.e. the definition of the name-keyed read) and `mark_moved_expr` (**the only decision site**) |

A grep for `owning_dyn` cannot answer this: it also matches the LOCAL `path_owning_dyn`, a
different declaration, and it cannot tell a cross-checked read from a bare one.

⚠ **CROSS-CHECKED AGAINST A PER-SITE READ**, because `ctx_of` coarsens and this tool has
already been wrong in exactly that direction (37 vs 0). By hand: sema.cpp:4020 is
`owning_trait_object() || (info.owning_dyn && kind()==TraitObject)`; sema_impl.hpp:4203 is
the same expression over the PATH type; sema_impl.hpp:4497 had no type test at all.
**dlog says 1 decision site; the per-site read says 1 decision site.** The two numbers agree,
and they are reported side by side. Class size is one, and this is what was enumerated to
know it. `selftest.sh` was run on this tree before the rule was trusted.

## THE FIX — ONE GATE, ONE SITE

`mark_moved_expr`'s VarRef arm: a VarRef whose OWN type is `Kind::TraitObject` and
`!owning_trait_object()` is a borrowed trait object and is never moved. All three disjuncts
sit behind it; each answers "does this expression own something", and only the type can say.

## PREDICTION vs OUTCOME

Declared in `PREDICTION.md` before the edit: **1 row closed**, four named hand programs to
change, eight named controls to hold. Outcome: exactly that, with no exceptions.

| hand program | control (rebuilt at HEAD) | fixed |
|---|---|---|
| the row's own program | rc **1**, 16 b definitely lost | rc 0, 0 b |
| METHOD `&dyn` arg | rc **1**, 16 b | rc 0, 0 b |
| dyn UPCAST at a `&dyn` arg | rc **1** | rc 0, 0 b |
| two `&dyn` args in one scope | rc **1** | rc 0, 0 b |
| `tests/spec/pass/coerce_box_dyn.logos` (corpus) | rc 0, **8 b definitely lost** | rc 0, **0 b** |
| the new pass fixture | **REFUSED** "use of moved variable 'b'" | rc 0, stdout `D|END` |

CONTROL REVERT: a git worktree at `871d1bddd`, `logosc` built from it, every measurement in
the "control" column taken on THAT binary — not on a recollection of the pre-edit tree. For
this `run` row the old binary is shown doing the wrong thing AT RUN TIME (rc 1 and 16 bytes
lost under valgrind), not merely compiling differently. (The link used the fixed tree's
stdlib archives; the deleted drop is emitted in the program's own object, not in them.)

OVER-REFUSAL CONTROLS — six, written for this round in shapes neither the row nor the
2026-09-08b landing used, all UNCHANGED across the fix, each still refused with the identical
sentence: by-value `Box<dyn>` arg with the source re-read; `let c: Box<dyn Sp> = b;` with the
source re-read; `Rc<A>`→`Rc<dyn Sp>` CoerceUnsized (R1); BY-VALUE dyn upcast at a call arg
(R2); by-value `Box<dyn>` arg with no reuse (rc 0, exactly one destructor, valgrind-clean);
`&Concrete`→`&dyn` at an arg. Plus the two shapes that were already clean and stay clean
(`let r: &dyn Sp = &b;`, a struct-literal `&dyn Sp` field from `&b`).

## TWO NEW DEFECTS FOUND, BOTH ROWED

Written as over-refusal controls, both fail in **mlir-gen** on the control binary and the
fixed one alike — so neither is caused or repaired here, and neither is an over-refusal
created by this change:

  * `boxdyn_mutborrow_arg_no_vtable` (tier 3, `refuses`) — `&mut b` for a `&mut dyn Sp`
    parameter: `no vtable for '&dyn Sp' as '&dyn Sp'`, a coercion asked from a type to
    itself. One token from the pass fixture, which compiles and runs clean.
  * `rcdyn_borrow_arg_no_vtable` (tier 3, `refuses`) — an `Rc<dyn Sp>` local for a `&dyn Sp`
    parameter: `no vtable for 'Rc$G1$udyn_Sp' as '&dyn Sp'`. One token from the pass
    fixture: `Box` works, `Rc` does not.

Both are legal Rust by deref coercion. ⚠ There is no rustc binary on this box; that legality
claim rests on reading.

## LEDGERS

`soundness_queue.ledger` 81 → **82**: one row DELETED (closed, program landed as the fixture
pair `tests/logos/{pass,fail}/boxdyn_borrow_arg_keeps_box_drop`) and two ADDED. `# TOTAL`
re-derived by direct listing (`awk '!/^#/ && NF' | wc -l` = 82). The gate was also run with
the closed row RESTORED and reads **rc 1**, "NO LONGER REPRODUCES — want run=1, read run=0" —
the red direction is proven, not assumed.

`unrowed_backlog.ledger`: the `vg_leak_records` entry's "ROWED THIS ROUND — 1 record" is
rewritten to ROWED-AND-CLOSED with the root and the `coerce_box_dyn` 8 b → 0 measurement.
The entry's "WHAT IS LEFT — 50 records" is unchanged and the entry stays OPEN; `# TOTAL 15`
unchanged. **One record RESOLVED inside the entry; the entry is not closed.**

No bc-ledger row opened or closed.

## CONTRADICTS A RECORDED CLAIM

* **The row's own header** says "`let r: &dyn Sp = &b;` is CLEAN, SO THE BORROW IS NOT THE
  DISCRIMINATOR; THE ARGUMENT POSITION IS, and a fix must not reach the `let` form." The
  measurement is right and the inference is wrong: both forms produce the same re-typed
  VarRef, and the `let` is clean only because no move-marking consumer stands downstream of
  it. The fix is keyed on the TYPE and reaches neither form's borrow.
* **PROBES.md 2026-09-08b** says "Self-gating is what makes it safe in a by-REFERENCE
  coercion position: `&Concrete -> &dyn` has a non-move operand, so nothing is marked." True
  for `&Concrete` and FALSE for `&Box<dyn>`, whose operand is not a ref at all but the
  owner's own VarRef with the owning bit still on its binding. That sentence is the reason
  the borrow case was believed covered.
* **The row claims the argument position is the whole difference.** Measured here: the
  METHOD-call argument position leaks 16 bytes too, and the dyn-UPCAST argument position
  does as well — three positions, one root, none of them separately recorded.

## EVERY ORACLE'S rc, ON THE FINAL TREE

| oracle | rc | number |
|---|---|---|
| `soundness_queue_gate.sh` | **0** | 82 rows (t1=18 t2=12 t3=44 t4=8), `# TOTAL` says 82 |
| the same gate with the CLOSED row restored | **1** | "NO LONGER REPRODUCES — want run=1, read run=0" |
| `test-levels.sh L1` (from `build/`) | **0** | 806/806 + 12 684 generated + 143 tier_commit |
| `gate-run.sh -L bc` | **0** | store: `gate-db: build 1047 — 2769 passed / 0 failed / 2 other, 2771 recorded` |
| `scripts/stdlib-cost.sh` | **0** | "all four layers compile under 'nothing armed'" |
| `scripts/run_oracle.py` | **0** | 6661 pass fixtures compiled, linked and RUN |
| `scripts/fail_text_oracle.py` | **0** | 1492 fail fixtures; **match column 1492/1492**, rc column 1485×1 + 7×4 |
| `cmake --build build -j32` | **0** | and a second no-op build leaves `build_hash` identical |
| `tools/dlog/selftest.sh` | **0** | 19 walkers / 24 findings / try_path 1-5 / domain 42-5; duty discriminates 1 → 0 |
| `probe-log-lint.py` | — | 271 records, every site symbol resolves |

⚠ **THE `run_oracle` DIFF IS AGAINST AN OLDER BASELINE AND IS SAID SO.** The newest table on
the box is `sandbox/vg-round3/run_oracle-2026-09-08c.tsv` (6553 rows), three landings back —
there is no same-binary control table, because the control worktree held only `logosc`.
Diffed both ways: 6553 common rows, **28 movers, none of them into damage**; damaged 63 → 48,
16 repaired; the ONE row that "became damaged" (`coex_dyn_bare_key`, `LINK`) is a row that did
not exist in the baseline at all and belongs to the cross-package/memoria `LINK` class this
oracle cannot link single-object. So the movers are the 09-08c→HEAD landings (drop glue, the
hrtb/typeof `c90` repairs), not this change.

For the TEXT column the honest statement is the same: no same-configure baseline exists, and
`fail_text_oracle.py` self-invalidates across a rebuild. What can be said without one: this
change makes move-marking strictly RARER, so it can only DELETE a "use of moved variable"
line, never add one — and a deleted line that a `.expected` pins turns the substring match
false, which is exactly what `-L bc`'s 1492 fail fixtures and the oracle's 1492/1492 match
column both say did not happen.

`tests/logos/{pass,fail}/boxdyn_borrow_arg_keeps_box_drop` are registered (#4429, #8357) and
both green.

## A RECORDED CLAIM RE-MEASURED, BY ACCIDENT AND THEN ON PURPOSE

The fix first landed with a SEVEN-LINE explanation beside it in `sema_impl.hpp` — prose in a
COMPILED header, which the 2026-08-29 record says moves the binary hash. Trimming it to a
two-line marker and rebuilding moved `build_hash` **63548bfe58e8d6df -> 260d74a881ec778e**
with no code change whatsoever. The claim holds, measured again today.

So every gate above was re-run on `260d74a881ec778e`, not carried over: queue gate rc 0, L1
rc 0 (806/806 + 12 684 + 143), `-L bc` rc 0 (2769/0, store build 1047), `run_oracle` 6661
rows and `fail_text_oracle` 1492 rows. The two `run_oracle` tables across the trim differ in
**ONE** row — `cast-region-to-uint`, the row this harness subtracts by name because it prints
a stack address — and the two `fail_text` tables are byte-identical.
