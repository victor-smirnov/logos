# 2026-09-14l-storeedge — PREDICTIONS, by name, written before the build

Three names at ONE site (apply_flow_outparams, the A2 `reborrow_of_.add(dst, p)` loop). Rule 9: the outer guard
alone (`a2skipall`, every A2 edge) and two inner predicates (`a2elem`: operand type is an element type arg of the
out-param's container; `a2elemshr`: the same AND the operand is a SHARED `&`).
`a2skipall` is only priced by `ceiling-probe.sh` + hand battery; `a2elem`/`a2elemshr` by every column.

## bc_admits
- a2elem:    closes {buffer-reuse-pattern-issue-147694}; NOT two-phase-across-loop.
- a2elemshr: closes {buffer-reuse-pattern-issue-147694}; NOT two-phase-across-loop.
- a2skipall: closes {buffer-reuse-pattern-issue-147694}; cost > 0 expected (A2's own witnesses: a `&mut` stored
  into a struct out-param, `wire(&mut t, &mut vs); t.x.push(c.mk()); c.bump()`).

## Hand battery (hb/, by path; base verdicts measured 0230e503bd682184)
Illegal, base ADMITS, predicted REFUSED under a2elem and a2elemshr:
  L01/l01 (the row), l05 (for-loop, after use), b2 (outer push then block push, after use), c07 (clear between),
  c09 (`push(&y); push(&x); x = 9; len` — E0506 through the loan channel), c10 (loop push(&x), `x = 3` after),
  c17 (outer push, then `&mut buffer` alias pushes a block local), c19 (outer push, loop pushes locals, after use),
  c06 (alias `r = &mut buffer` pushing loop locals) — UNCERTAIN (dst is `r`, whose note_reborrow edge is kept).
Illegal, base ADMITS, predicted REFUSED under a2elem only: c02 (`Vec<&mut i64>`, the `&mut` element store).
Illegal, base ADMITS, predicted UNMOVED (not this fact): L07, l04, c12 (no use after the loop — the pass-2 dangle
  carry), c18 (assign twin of that), c04e / two-phase-across-loop (door 1: lu at the raise), c14 (array element
  store — the edge writer is note_reborrow, not A2), w02 (a `&mut Vec` element written through `vs[0]`).
Legal, base REFUSES, predicted COMPILES and RUNS exit 0 under a2elem and a2elemshr: b4.
Legal, base compiles, predicted unchanged (compile + run exit as base): L04 L05 L06 b3 b7 b9 c05 c11 c13 w01 w03 w04
  w05 w06. Hazard shapes for the arm: w01/w03 (a `&mut Vec` stored as an element and written through `vs[0]` —
  the edge a2elem drops is what re-homes that write), w04 (struct field `&mut` — A2's own shape, must stay legal).
Illegal, base REFUSES, predicted still refused with the same sentence: L02 L03 l02 l03 b1 b5 b6 c01 c04 c04b c04c
  c04d c08 c16 c20.
