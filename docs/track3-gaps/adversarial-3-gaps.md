# Adversarial Testing #3 — sweep-#2 hardening (2026-06-14)

Third sweep: a *pristine* re-audit of the sweep-#2 fixes (borrowck × codegen)
per Victor's "постестировать с пристрастием". Method: sibling/variant probes
around each closed class, rustc 1.93 as oracle, fix the class. Probes in
/tmp/adv3; regressions in `tests/logos/{pass,fail}/adv3_*`.

**Result: every other sweep-#2 fix held** (iter_mut aliasing, DerefMut compound
writes, index autoderef — all verified by value). Found ONE unclosed class —
a direct continuation of what sweep #2 started — plus three latent bugs it had
been masking. All fixed in `borrow_check.cpp` (one file, +127/-12).

Status legend: `✅ Closed` — fixed this session.

| ID | Class | Status | Root / fix | Regression |
|---|---|---|---|---|
| ADV3-1 | **Soundness**: a live borrow of a FIELD did not block whole-value MOVE / whole-value USE / field READ. Sweep #2 recorded field borrows and checked them only when TAKING another borrow of the field; the other three access sites ignored them. `let r=&s.val; let s2=s;` (E0505), `let r=&mut s.a; let x=s.a;` (E0503), move-whole-while-field-borrowed all slipped. | ✅ Closed | New `field_borrow_conflicts(st, target, path, need_exclusive, verb)` wired into the three access sites: `consume` (whole move → blocked by ANY field borrow), `VarRef` value-read (whole read → blocked by a MUT field borrow), `FieldRead` (field read → mut borrow = E0503; partial move → ANY borrow = E0505). Place-base position (`w.f`/`w[i]`/`recv.m()` receiver, `&place` source) is gated by `in_addr_source_` + `visit_place_base` so naming a place is not a value-use. | fail `adv3_move_while_field_borrowed`, `adv3_read_while_field_mut_borrowed`; pass `adv3_field_borrow_nll_release` |
| ADV3-2 | **Latent (move-classification)**: a GENERIC droppable container (`Wrap<T>{v:Vec<T>}`) was mis-classified as non-move, so move-while-field-borrowed slipped for it even after ADV3-1. | ✅ Closed | `has_droppable_fields` matched the struct def by the bare base name (`Wrap`), but the mono def is `Wrap$G1$i64` — the droppable `Vec` field was invisible. Match by `concrete_struct_name(t)` (== base for non-generic). Same `$G` naming mismatch as ADV2-4, on the def-lookup side. | fail `adv3_move_generic_while_field_borrowed` |
| ADV3-3 | **Latent (NLL)**: a field borrow via explicit `let r = &mut s.b` never NLL-released (only at scope-pop) — the recording site didn't thread the holder. | ✅ Closed | `take_field_borrow` calls in the explicit-borrow path passed no `holder`; the empty holder skips `release_dead_borrows`. Thread `holder` at both `&place`-field sites. Latent until ADV3-1's field-read check existed. | pass `adv3_field_borrow_nll_release` |
| ADV3-4 | **Latent (false-positive)**: `&mut p[i]` through a RAW-pointer field (`self.data[i]`, `data:*mut T`) wrongly recorded a field borrow — broke stdlib `VecIterMut::next` once ADV3-1 consulted field borrows. | ✅ Closed | Indexing a raw pointer is an unsafe deref (Rust parity: no tracked borrow). `extract_borrow_place` now bails (empty root) at `IndexRead`/`SliceIndex` whose base is `Kind::Ptr`, mirroring the existing `Deref`-of-raw-ptr rule. | covered by stdlib build + `adv3_generic_field_method_call` |

Also fixed a false-positive surfaced during the work: a generic trait-method
call `w.writer.wr(w.buf)` lowers to a Call passing `&mut w.writer` + arg `w.buf`;
walking the arg's receiver chain reported whole-`w` as a conflicting use. The
`#2`/`#3` checks now respect `in_addr_source_` in place-base position
(`adv3_generic_field_method_call`, mirrors stdlib `BufWriter::flush`).

Green matrix (verified by value): disjoint field splits + NLL release of field
borrows (`adv3_field_borrow_nll_release`), nested-field disjoint access, generic
field method calls, DerefMut compound (`*w += v`), index autoderef (`w[i]`,
`w[i] += v`). adv1/adv2 17/17, full L4 5678/5678.

Note: whole-value `&s`/`&mut s` while a field is borrowed was ALREADY rejected
(pre-existing checks via `check_recv_conflict` / AddrOf) — not a hole.
