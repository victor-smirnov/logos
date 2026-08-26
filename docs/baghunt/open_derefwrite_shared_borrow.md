# OPEN — `*r = v` is admitted while a shared borrow through `r` is live

**Status**: confirmed live, repro proven, NOT fixed. Found 2026-08-26 by
`tools/dlog/cluster_divergence.dl`.

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
