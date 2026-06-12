# Adversarial Testing #2 — borrowck × codegen (2026-06-12)

Second sweep, focused per Victor's direction on feature INTERACTION around
the borrow checker and codegen. Two probe groups: **P** (Rust-accepts → must
compile AND compute correctly) and **F** (Rust-rejects → must reject).
Probes in /tmp/adv2; regressions in `tests/logos/{pass,fail}/adv2_*`.

Status legend: `Open` — not started; `✅ Closed` — fixed this session.

| ID | Class | Status | Root / fix | Regression |
|---|---|---|---|---|
| ADV2-1 | False reject: mutation/projection through let-bound `&mut S` demanded `mut` on the BINDING (`m.a += 1`, `&mut m.b`, reborrow args) | ✅ Closed | take_field_borrow had no root-type knowledge — now takes the root TypeRef: ref-roots skip the binding-mut check (mutability comes from the &mut type); `&mut` through a SHARED-ref root is a proper E0596-analog error. | `adv2_reborrow_through_mutref_binding`, fail `adv2_mut_through_shared_root` |
| ADV2-2 | `*w op= v` through user DerefMut unsupported ("left side must be a pointer or mutable reference") | ✅ Closed | DEREF_COMPOUND now desugars through the canonical `emit_generic_deref_call` (shape-aware, generic wrappers); the old DEREF_WRITE-only hand-rolled lookup also missed generics. | `adv2_derefmut_compound_write` |
| ADV2-3 | **Soundness**: two live `iter_mut()` accepted (aliasing &mut); iterator values didn't carry the receiver borrow | ✅ Closed | (a) VecIter/VecIterMut marked `#[borrow_carrying]` (adapters inherit transitively via type-args); (b) elision rule extended: `&self → BC-type` result borrows self; (c) bare-VarRef receivers (no AddrOfTemp wrap) now record the borrow, field-precise for chains (`self.arc.deref_mut()` borrows `self.arc`, not all of self), held by the binding (NLL release at last use — FieldBorrow gained holder + release); (d) Let handler routes BC-typed bindings through take_ref_borrows. | fail `adv2_two_iter_mut` |
| ADV2-4 | **Soundness**: move of ANY generic droppable struct while borrowed accepted (`let c = b;` Box, `let w = v;` Vec) | ✅ Closed | Two roots: (a) drop_types registered only mono names (`Box$G1$i64`) while fn-body TypeRefs spell bare templates — `$G`-strip at registration makes every generic droppable struct move-classified; (b) extract_borrow_place's Deref walk stopped at owning containers — `&*b` now roots at the Box/Rc/user-Deref variable. | fail `adv2_move_box_while_deref_borrowed`, `adv2_move_vec_while_elem_borrowed` |
| ADV2-5 | Fallout precision from ADV2-3/4 (4 latent classes surfaced) | ✅ Closed | (a) direct (attribute) vs TRANSITIVE BC registration split — only direct marks strip `$G` (a `FilterIter$G…$VecIterMut…` spec is BC because of its args, bare FilterIter is not — false E0716 on Slice chains); (b) prov_of: a by-VALUE-self adapter consumes the temp receiver into the result — only ref-self methods' results point INTO the temporary (`v.iter().enumerate()` in for-loops); (c) match-stmt arm merge now divergence-gated like if-stmt — a `return out` arm must not poison `out` for the next loop iteration (iter_collect_vec, surfaced when Vec became move-classified); (d) bare-receiver recording skips Rc/Arc roots — shared-ownership handles are the blessed interior-mutability domain (Hermes residency escape: `h.array()` then `hold(&mut h, root)`). | covered by core_8_adv_iter_* + hermes_residency + iter_collect_vec |
| ADV2-6 | Index-position autoderef absent (`w[0]` where W: Deref<Vec>) | ✅ Closed | Bounded deref walk before index dispatch in lower_index_read (mirrors method-resolution autoderef; fat targets — Slice/TraitObject — use the deref call's fat value directly). Write side (`w[0] += v`) works via the same resolution. | `adv2_index_autoderef_wrapper` |
| ADV2-7 | f07/f08: move of all-Copy-field struct while borrowed "accepted" | Not a bug | B1 registered divergence (auto-Copy synthesis): the "move" is a copy, semantics consistent. Real move-while-borrowed works (f09/f10). Goes away if/when B1 closes. | — |

Green matrix (Rust-accepts verified end-to-end, `adv2_borrowck_green_matrix` +
session probes): NLL last-use release (incl. `&mut v[0]` then push), two-phase
borrow `v.push(v.len())`, disjoint-field split `&mut s.x`+`&mut s.y`,
`match &mut enum` payload mutation, per-iteration loop reborrows, FnMut
capture then post-closure use, deep `&mut` chain returns (`fn(&mut Out) ->
&mut i64`), partial-move + field-reinit restoring whole-struct use, iter_mut
element writes, match-guard borrows, early-return drop order (= rustc).

Codegen under borrows all verified by value, not just acceptance: reborrow
writes land in the right places, drops exactly once (valgrind on p06/p14
shapes), capture/scope drop order matches rustc.

Known coarseness (accepted, documented):
- Bare-receiver borrow recording is field-precise but an INDEXED receiver
  chain collapses to the whole container element rule (same as elsewhere).
- `arc.deref_mut()` on a non-mut `Arc` binding still allowed (existing
  permissive bare-receiver policy; Rust would demand `let mut arc`). The
  exclusivity is enforced; only the binding-mut legality stays permissive —
  flipping it is a separate sweep over stdlib uses (`store_open` etc. rely
  on it today).
