# PREDICTIONS — round 2026-09-17a-ptrmention

Written after the source edit and BEFORE the first armed build exists, before any
armed measurement. (The 2026-09-14d record names "PREDICTIONS.md written after the
source edits" as one of its own mistakes; this one is no better on that point and
says so — the edit is in the tree as I write. It IS before any armed binary and
before every number below.)

base build_hash `8f5483d04b7c49ac 43` · queue `# TOTAL` 233 · tier3 121 · queue gate rc 0

## THE FACT (the door)

`sema_impl.hpp::collect_param_regions_` answers **"which regions does this
parameter type mention?"**. `build_call_lt_subst_` splits on that answer:

* MAPPED — an argument gave the binder a region;
* MENTIONED — it appears in a parameter type, region unnameable → `""` (elided, permissive);
* FREE — it appears in NO parameter type → **`'static`**.

`collect_param_regions_`'s arm set is Ref/MutRef, Struct/ZonedStruct/Enum, Tuple,
Slice/Array. It has **no `K::Ptr`, no `K::FnPtr`, no `K::TraitObject`/`DstRef`**, and
does not read a fat pointer's own lifetime slot. So a binder mentioned only under
one of those is called FREE and pinned to `'static`.

Measured on base before the arm (`LOGOS_CENSUS`): `subst.call.unmapped 2` on
`mutptr`, `refptr` and `fnptr_call_result`; `subst.call.mapped 4` on
`static_arg_pins` (a different branch).

Three siblings in the same file already have the arms: `type_mentions_lt_`
(Ptr + fat), `type_region_opaque_` (Ptr + FnPtr), `region_loss_noncov_` (Ptr + FnPtr).

## ARMS

* `ptrmention` — `K::Ptr` only.
* `ptrmentionall` — `K::Ptr` + `FnPtr`/`Closure`/`FnItem` + `TraitObject`/`DstRef`
  + the `Slice` lifetime slot.

Two names because the Ptr arm and the fn/fat arms are separable and a single name
could not tell which one paid (rule 9).

## QUEUE ROWS — predicted BY NAME, to be diffed BOTH ways

| row | `ptrmention` | `ptrmentionall` | why |
|---|---|---|---|
| `mutptr_region_param_elided_let_arg_refused` | CLOSES | CLOSES | `*mut &'a i64` param, `unmapped 2` |
| `refptr_inner_region_elision_demands_static_refused` | CLOSES | CLOSES | `&*mut &'a i64` param, `unmapped 2` |
| `fnptr_call_result_region_param_reads_static_refused` | does NOT move | CLOSES | `fn(&'r i64)` is `K::FnPtr`, Ptr arm cannot see it |
| `static_arg_pins_shared_callee_region_refused` | does NOT move | does NOT move | `mapped 4`, first-wins at a MAPPED binder — a different door |
| `generic_type_param_two_regions_first_wins_refused` | does NOT move | does NOT move | TYPE binder, not a region binder (14d reason 1) |

No other queue row is predicted to move in either direction. **A row moving that is
not in this table refutes the grouping**, and the diff is to be read both ways
(rule 6: a ceiling bounds the count, not the set).

## HAND BATTERY — base verdicts measured, armed predicted

| id | shape | base | `ptrmention` | `ptrmentionall` |
|---|---|---|---|---|
| L01 | `*mut &'a i64` param | refused | compiles, runs 0 | compiles, runs 0 |
| L02 | `*const &'b i64` param | refused | compiles, runs 0 | compiles, runs 0 |
| L03 | `&*mut &'a i64` param | refused | compiles, runs 0 | compiles, runs 0 |
| L04 | `fn(&'r i64) -> i64` param | refused | **still refused** | compiles, runs 0 |
| L05 | `(*mut &'a i64, i64)` tuple | refused | compiles, runs 0 | compiles, runs 0 |
| L06 | `*mut &'a i64` in a struct field | **compiles rc 0 on base** | unchanged | unchanged |
| X01 | region under `*mut` escapes its scope | refused | **must STAY refused** | **must STAY refused** |
| X02 | callee writes `'static` under `*mut` | refused | **must STAY refused** | **must STAY refused** |

L06 compiling on base is why the population is not uniform: the struct-literal path
reaches the binder by another route. It is a control, not a target.

## THE ABUSE DIRECTION — the whole cost

Both illegal twins are refused **by rustc 1.98.1, measured**, each E0597
(`illegal_ptr_region_escapes.rs`, `illegal_ptr_region_static_demand.rs`; the second
says "argument requires that `v` is borrowed for `'static`").

⚠ **They are refused on base for the WRONG reason** — the same `variance mismatch`
sentence the legal programs get, i.e. refused by the defect rather than by the
escape check. So the arm removes the accidental refusal, and whether they stay
refused rests on machinery this round has not yet read. **If either is ADMITTED
under either arm, the arm is condemned** and the round reports that instead: an
over-admission of a program rustc refuses is strictly worse than the five rows.

X02 is predicted to stay refused on a reading that must be checked: an explicitly
written `'static` is not in `lifetime_params`, so no binder is instantiated and
this walker is never consulted for it.

## COST COLUMNS — predicted

Base recorded from ONE configure (`build/`, hash above): stdlib 4 of 4, rc 0 ·
queue gate 233 hold, rc 0 · `fail_text_oracle` and `run_oracle` baselines in flight.

Predicted, and every one to be MEASURED armed-vs-unarmed and diffed both ways:

* `fail_text_oracle.py` — the column most likely to be non-zero. The arm changes a
  region from `'static` to elided, which changes what a refused program PRINTS even
  where the verdict is unchanged, and an rc-based column is blind to that (rule 15).
  Predicted: **some changed text**, possibly 0 changed rc.
* spec fail tier **by name** (not covered by that oracle) — predicted 0.
* `run_oracle.py` — predicted 0 changed after subtracting `cast-region-to-uint`.
* stdlib — predicted 4 of 4. ⚠ `objdata.logos`'s `wod_view_array<'a>(p) -> &'a WArray`
  is named in `build_call_lt_subst_`'s own comment as the reason FREE→`'static`
  exists at all. If a `*`-typed parameter there stops being FREE, the stdlib is
  exactly where it shows. This is the predicted failure mode of the whole arm.
* valgrind — no drop glue moves; not expected to apply.

`ceiling-probe.sh` is NOT a column here: it cannot see a queue row (16m measured its
ceiling as 0 with two rows one door away).
