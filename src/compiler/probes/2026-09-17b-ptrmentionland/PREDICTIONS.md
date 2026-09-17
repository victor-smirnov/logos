# PREDICTIONS — round 2026-09-17b-ptrmentionland (THE LANDING)

Written **before any armed binary exists** (`build-land17b` is mid-build as this
file is saved; the source edit is in the tree, and this file is therefore no
better on that point than 17a's was — it says so). Every number below is a
prediction, not a reading.

base `build_hash 8f5483d04b7c49ac 43` · queue `# TOTAL` 233 · tier3 121 · gate rc 0 in flight

## WHAT IS LANDING, AND WHAT IS NOT

`collect_param_regions_` gains **`K::Ptr`** and **`K::FnPtr`/`Closure`/`FnItem`**,
unconditionally (no probe gate). That is strictly less than 17a's `ptrmentionall`.

**Deliberately NOT landed**: the `TraitObject`/`DstRef` arm and the `Slice`
`lifetime()` slot. 17a co-landed them on a cost-0 reading with **zero rows of
their own**, and cost 0 is not a safety claim (rule 5). C01/C02 are the carrier
probes; if they compile on base and are unmoved armed, those arms change no
program's verdict and are an unexercised relaxation — reason 3, not co-landed.

## PREDICTED VERDICTS (`cc/diag/run`), base measured, armed predicted

| program | base (MEASURED) | armed (PREDICTED) | role |
|---|---|---|---|
| `mutptr_region_param_elided_let_arg_refused` | `1/1/-` | **`0/0/0`** | ROW CLOSES, rustc twin runs 0 |
| `fnptr_call_result_region_param_reads_static_refused` | `1/1/-` | **`0/0/18`** | ROW CLOSES, rustc twin runs 18 |
| `refptr_inner_region_elision_demands_static_refused` | `1/1/-` | **`0/0/1`** | ⛔ compiles, WRONG (rustc twin runs 0) |
| `static_arg_pins_shared_callee_region_refused` | `1/1/-` | `1/1/-` | control, must NOT move |
| `generic_type_param_two_regions_first_wins_refused` | `1/1/-` | `1/1/-` | control, must NOT move |
| G01 ptr-to-ptr `*mut *mut &'a i64` | `1/1/-` | **`0/0/0`** | the Ptr arm must RECURSE |
| G02 ptr inside an array param | `1/1/-` | **`0/0/0`** | Ptr reached THROUGH the Array arm |
| G03 fn-ptr, two regions | `0/0/0` | `0/0/0` | control — already legal on base |
| G04 fn-ptr, region in return only | `0/0/0` | `0/0/0` | control — already legal on base |
| C01 `&'a dyn Shape` param | `0/0/0` | `0/0/0` | **no carrier** for the TraitObject arm |
| C02 `&'a [i64]` param | `0/0/0` | `0/0/0` | **no carrier** for the Slice slot |
| **B01 fn-ptr region escapes** | `1/1/-` | **`1/1/-` — MUST STAY REFUSED** | abuse direction |
| **B02 ptr-in-array region escapes** | `1/1/-` | **`1/1/-` — MUST STAY REFUSED** | abuse direction |
| L01 / L02 / L05 (17a) | `1/1/-` | `0/0/0` | Ptr arm, plain / const / tuple |
| L03 (17a) `&*mut &'a i64` | `1/1/-` | **`0/0/1`** | compiles, WRONG — second defect |
| L04 (17a) fn-ptr param region | `1/1/-` | `0/0/0` | the FnPtr arm |
| L06 / N01 / N02 (17a) | `0/0/0` | `0/0/0` | controls, already legal |
| **X01 / X02 (17a)** | `1/1/-` | **`1/1/-` — MUST STAY REFUSED** | abuse direction |
| X03 (17a) | `1/1/-` | `0/0/1` | same second defect as L03 |

**If B01, B02, X01 or X02 is ADMITTED, the arm is condemned** and this round
reports that instead of landing. An over-admission of a program rustc refuses
(E0597 on all four, measured today) is strictly worse than the two rows.

## THE ROW THAT MUST NOT BE DELETED

The queue gate judges a `refuses` row by *"did it stop being refused"*. Under
this arm `refptr` stops being refused and the gate will demand its deletion. Its
program then **computes the wrong answer** (predicted run 1; rustc runs 0), so
deleting it would buy a closure with a miscompile. Predicted handling: the row
STAYS, its `observed` re-recorded from `refuses` to **`run 1`** — a strictly
STRONGER oracle (a compile no longer satisfies it; only the right exit code
does) — with the defect it now names, `&*mut T` parameter equality comparing the
outer reference, written into its header against the rustc twin.

## COST COLUMNS — predicted

* `fail_text_oracle.py` — predicted **0 changed**, and this is the column most
  able to be non-zero: the arm changes a region from `'static` to elided, which
  changes what a still-refused program PRINTS (rule 15: an rc column is blind to
  that).
* spec fail tier by name — predicted 0 (not covered by that oracle).
* `run_oracle.py` — predicted 0 changed after subtracting `cast-region-to-uint`.
* stdlib 4 layers — predicted 4 of 4. `wod_view_array<'a>(p: *const u8, …)` is
  the site named as this arm's predicted failure mode; **measured today its
  pointee is `u8`, which mentions no region**, so the Ptr arm cannot change its
  answer and the stdlib is predicted intact. If it breaks, that reading is wrong.
* valgrind — the closing programs have no drop glue; not expected to apply.
