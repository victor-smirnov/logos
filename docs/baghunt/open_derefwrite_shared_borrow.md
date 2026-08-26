# CLOSED 2026-08-26 (`438c8197c`) — `*r = v` was admitted while a shared borrow through `r` was live

**Status**: FIXED. Found 2026-08-26 by `tools/dlog/cluster_divergence.dl`.
The DerefWrite fall-through now delegates to `check_recv_conflict`, the
whole-root mutable-use predicate that already carried the raw-ptr and
non-empty-path exemptions. Pinned by `fail/bc_derefwrite_shared_borrow_fail`
+ `pass/bc_derefwrite_shared_dead_admit`. Still NOT fixed on this path, each its
own finding: no `place_write_loans` call (the LOAN/provenance channel for
`*r = v` stays empty), and no §B6 add_ref_sources/holder-escape deposits.

## The program

```logos
package e_deref;
struct S { f: i64 }
fn main() -> i32 {
    let mut s: S = S { f: 1i64 };
    let r: &mut S = &mut s;
    let b: &S = &*r;
    *r = S { f: 2i64 };     // rc 0 — rustc: E0506, assignment to a borrowed value
    return b.f as i32;
}
```

**One-variable twin** — the diff is the package line and the write line, nothing
else; both sides hold `let b: &S = &*r;` live:

| write | verdict |
|---|---|
| `r.f = 2i64;` | REFUSED — "cannot borrow 'r.f' as mutable: 'r' has shared borrows" |
| `*r = S { f: 2i64 };` | **rc 0, admitted** |

## How it was found

`cluster_divergence.dl` groups arms by a callee they SHARE rather than by the
switch they sit in — the repair of `arm_divergence`, whose premise ("the arms of
one switch are peers") was measured wrong. Of the seven arms of `visit_stmt`
that call `place_write_loans` — every spelling of a write — six also call
`check_live`, and `DerefWrite` is the seventh. Ranked second of 23 rows.

⚠ The ratio that exposes it is 6/7. Against the switch's ~22 live arms it is
0.27, far below any threshold a tuned rule could sit above. **The denominator was
the whole finding**; no amount of adjusting `arm_divergence` would have reached it.

## Why it is not fixed here, and what the next round must not repeat

Three attempts, each a guess, each measured and each wrong:

1. `check_live` added under the existing `saw_index` decomposition — never ran.
2. `check_live` added for a bare-VarRef pointer — ran, changed nothing.
3. **The instrumented build settled the shape**: `ptr.kind` is 4 (`VarRef`) for
   `*r = v` and 12 (`AddrOfTemp`) for `r.f = v`. The whole decomposition in that
   arm is gated on `AddrOfTemp`, so the plainest deref write in the language
   reaches none of it.

⚠ **AND `check_live` IS THE WRONG CHECK ANYWAY.** It tests `dangling`, `moved`
and `mut_borrowed` — deliberately NOT `shared_borrows`, because *using* a value
under a shared borrow is legal and only *writing* is not. The sibling arms'
refusal for `r.f = v` does not come from their `check_live` call at all; it
comes from the `AddrOfTemp` place decomposition taking a borrow of the place.
So the cluster rule pointed at a REAL asymmetry and the obvious repair for it is
not the fix. The missing check is a shared-borrow conflict on the written place,
and it belongs on the path that has the place, not in `check_live`.

**Next round**: start from where `r.f = v` acquires its refusal (the AddrOfTemp
decomposition and `take_borrow`), and give the bare-VarRef pointer the same
route. Do not re-attempt `check_live`.

---

# CLOSED 2026-08-26 (`6ede8b442`) — a RANGE projection recorded no loan: `let r: &[i64] = a[0..2];`

**Status**: FIXED for the view-base spelling. Found 2026-08-26 by
`place_walkers` (`take_ref_borrows` 1/5). The SliceLit arm now marks
`slice_view_base_` and the AddrOfTemp fall-through records a whole-root borrow
when it is set — a method autoref is never wrapped in a SliceLit, which is what
keeps the measured-wrong blanket "record on empty path" out of it. Pinned by
`fail/bc_range_view_holds_loan` + `pass/bc_range_view_nll_admit`.

⚠ THE CLASS IS NOT CLOSED, only this spelling. "Empty path" at that
fall-through still covers at least five different situations (Call-lowered
autoref receiver; deref chain, peeled in `262f066f`; view base — this fix;
fake_param bypass; unknown root), and re-slicing an already-slice local builds
no SliceLit and still records nothing. Closing the class needs an enumerator —
an env-gated census at that fall-through — which does not exist yet.

```logos
let mut a: [i64; 4] = [1i64, 2i64, 3i64, 4i64];
let r: &[i64] = a[0u64..2u64];
a[0u64] = 9i64;          // rc 0 — rustc: E0506
return r[0u64];
```

**One-TOKEN twin**, built by the verifier after rejecting the first pair as two
properties apart:

| binding | verdict |
|---|---|
| `let r: &i64  = &a[0u64];` | REFUSED — "cannot assign through 'a[..]'" |
| `let r: &[i64] = &a[0u64..2u64];` | **rc 0**, and the loan dump is EMPTY |

## The mechanism, measured — and the report's attribution was wrong

The row named `take_ref_borrows` as missing a `SliceIndex` arm. **There is no
SliceIndex node.** An instrumented run shows the two spellings lower differently:

```
&a          ->  SliceLit(23) -> AddrOf(11)        loan recorded
a[0..2]     ->  Call(7) -> SliceLit(23) -> AddrOfTemp(12)     nothing recorded
```

A range desugars to `slice_get_range` (sema_expr.cpp:11760), so the value is a
**Call**. `take_ref_borrows` IS entered and DOES reach the argument — the arm
chain works. The loss is at the end: `AddrOf` takes a borrow directly, while
`AddrOfTemp` routes through `extract_borrow_place`, and for `AddrOfTemp(VarRef a)`
the path comes back EMPTY with `index_in_chain` false, so both guards of that
decomposition miss and control falls through recording nothing.

⚠ **This is the same "empty path" gap as `&mut **p`**, fixed narrowly on
2026-08-26 (`262f066f`) by peeling a deref chain — which does not cover a bare
VarRef inner.

⚠ **AND THE BROAD FIX IS KNOWN TO BREAK THE STDLIB.** "Record a whole-root borrow
whenever the path is empty" was tried the same day and measured: it also fires
for a plain `AddrOfTemp(VarRef)`, i.e. **every method autoref**, so `it.next()`
in a loop conflicted with itself and `liblogos-lang` stopped building
(`iter_min`, `iter_max`).

**Next round**: the distinction that matters is between an autoref receiver,
whose borrow the MethodCall arm already records, and an explicit argument with a
holder. Find it there. Two attempts that must not be repeated: adding a
`SliceIndex` arm to `take_ref_borrows` (no such node exists on this path), and
recording on empty-path unconditionally.

---

# CLOSED 2026-08-26 (`a3e95a42b`) — sema called `loop { break; }` diverging, so a `let-else` dropped its loans

**Status**: the sema root is FIXED. Found 2026-08-26 by `cluster_divergence`
(LetElse / merge_loans, merge_provs). `SemaChecker::loop_has_targeting_break`
is the AST-phase predicate — label- and nesting-aware, mirroring lower_loop's
frame search — and all three classification sites delegate to it. Pinned by
`fail/let_else_loop_with_break_fail` + `pass/let_else_loop_no_break_admit`
(the let-else consumer) and `fail/loop_break_targets_outer_no_return_fail` +
`pass/loop_break_targets_inner_admit` (return reachability, bt_upper_bound's
shape — a presence-of-token walk reds liblogos-lang there).

⚠ STILL OPEN, two items. (1) `la::LABELED_LOOP` is handled by the predicate but
line 96 is not extended to it, so `'a: loop { }` in a let-else else is still
refused — the opposite direction, and its two BODY spellings need their own
measurement. (2) borrow_check's LetElse arm still restores `states_`/`prov_`
UNCONDITIONALLY where the If arm consults `cur_diverged_` and picks
restore-vs-merge from it: the same two-notions shape one layer down. The repair
is delegation to that join, NOT bolting `merge_loans` onto an unconditional
restore (break/continue escapes already deposit via `break_states.push_back` /
`continue_states.push_back`, and merging a diverged else would double-count
them). Its control has to neuter the sema gate temporarily, because after this
fix no program reaches that line.

`sema_stmt.cpp:96` classifies a `loop` as DIVERGING without inspecting its body
for a `break` that targets it. So:

```logos
let Some(v) = opt else { loop { break; } };   // gate: "diverges"
```

passes, while control actually **falls through**. borrow_check's `LetElse` arm
then restores the pre-else state — throwing away the loan the else branch
raised — and `g3.logos` compiles with `r` aliasing `x` across
`let y = x; *r = 7i32;`.

⚠ **THE NAMED CALLEE IS NOT THE ROOT FIX.** The row says the arm skips
`merge_loans`/`merge_provs`. Adding them would close this instance —
`merge_loans` (borrow_check.cpp:1098-1111) ORs `mut_borrowed` and maxes the
counters into base, which is exactly the bit the restore erases; the loan RECORD
survives, the dump shows `target=x holder=r is_mut=1` live at function end, only
the VarState bit the check reads is rolled back. But that is defence-in-depth
**behind a lying gate**. For a genuinely diverging else there is no join to
merge, and the escape route that does exist — a `break` out of an enclosing loop
— is already handled by the Break arm's `break_states.push_back(...)` at
borrow_check.cpp:10131, confirmed identical for `let-else` and `if`.

**Next round**: fix the divergence classification in sema — test a `loop` body
for a `break` targeting it, the way rustc's `!`-typing of `loop` does. Only then
decide whether the merge is still wanted as a second line.
