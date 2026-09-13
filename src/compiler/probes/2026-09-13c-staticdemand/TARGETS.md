# 2026-09-13c-staticdemand — TARGETS, written before the compiler was touched

Base binary 86911f4ef2b44caf 43 (read), HEAD 9aa431c28, bc_admits # TOTAL 74, queue gate rc 0.

## Rows, by id
| row | root | door |
|---|---|---|
| issue-69114-static-mut-ty | nllmoves.R1 | M1: write to a `static mut` whose type is `&u8` (elided = 'static) |
| regions-static-bound | lifereg.L2 | M2: callee `where 'a: 'static` never checked at the call |
| regions-pattern-typing-issue-19552 | lifereg.NEW-4 | M3: `T: 'static` with an EMPTY region — hand-measured only (installed probe `sttpempty`) |

## Why this block
Both roots have ZERO PROBES.md records (nllmoves.R1, lifereg.L2) and the shape that paid twice:
an ARM THAT EXISTS reached through a fact the site does not carry. Controls on the base binary:
- M1: `let r: &'static u8 = &n` REFUSED; `h.r = &n` (field `&'static`) REFUSED; `*pp = &n` REFUSED;
  `BAR = &n` with `static mut BAR: &'static u8` ADMITTED; the installed probe `lifereg_varassign`
  (recorded ceiling 0 on 2026-08-27) now REFUSES the explicit-'static static-mut write and a
  `let mut r: &'static` local — the record DECAYED — but NOT the row: the row's `&u8` is elided,
  and a static item's elided region is not read as 'static. Doors in series.
  `lifereg_varassign` itself is CONDEMNED BY HAND: refuses legal `let mut r = &FOO; r = &n;` and
  `let mut r: &u8 = &FOO; r = &n;` (the local's type inherits the init's 'static).
- M2: `fn sid(t: &'static i64)` called with `u: &i64` / `&n` REFUSED; the same demand spelled
  `where 'a: 'static` ADMITTED at every door (direct `&n`, a param, a method, transitive `'a:'b,'b:'static`).
  `check_call_outlives` skips any pair whose short side is 'static: 'static is never in `subst`.
- M3: not in this batch. `sttpempty` (installed) closes 19552 but refuses legal `let o: Option<&i64> =
  Option::Some(&S); assert_static(o)` and `let arr: [&i64;1] = [&S]; let w: &i64 = arr[0]; assert_static(w)`.
  The second is the row's own shape with a static: a COLLISION, not a purchase. Doors in series: an elided
  let annotation nested inside an ADT arg / array elem drops the init's 'static.

## Excluded by name
Self/impl-header plane; lifereg.B/NEW-B2 holder-deposit plane; A16 rows; argresvact; bck.D+nllmoves.D;
bck.NEW-CAPMOVE — per the ledger's own notes. impl-trait-captures (nllmoves.NEW-N1) and
explicit-static-bound-on-trait (lifereg.NEW-4) are REPORTED as corpus questions, not priced.
