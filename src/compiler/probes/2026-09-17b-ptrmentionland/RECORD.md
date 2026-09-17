# 2026-09-17b-ptrmentionland — THE RECOMMENDED ARM IS REFUTED IN THE ABUSE DIRECTION BY A CARRIER ITS OWN BATTERY NEVER VARIED: IT ADMITS TWO PROGRAMS rustc REFUSES, SO NOTHING LANDS IN THE COMPILER — AND THE TWO COUNTER-EXAMPLES FOUND TWO DEFECTS THAT REPRODUCE ON THE SHIPPED BINARY WITH NO ARM AT ALL

site: src/compiler/sema_impl.hpp::collect_param_regions_
site: src/compiler/sema_impl.hpp::build_call_lt_subst_
build: 8f5483d04b7c49ac 43 (base, read) · build-land17b armed (Ptr + FnPtr/Closure/FnItem,
  UNCONDITIONAL, no probe gate), clang-20 RelWithDebInfo — built, measured, REVERTED, dir deleted
measured: 2026-09-17
verdict: **THE ARM IS CONDEMNED AND NOT LANDED.** The two tier-3 rows it closes are real and
  run-verified, but the same change ADMITS two programs rustc refuses (E0597 each). An
  over-admission is strictly worse than the rows. The wall is named with its number below.

## WHAT THIS ROUND WAS ASKED TO DO, AND WHAT IT FOUND

The handoff recommended landing `ptrmentionall`: *"2 rows closed run-verified against rustc
twins, cost 0 in every column the harness owns, both illegal twins still refused, stdlib
intact."* Every one of those statements REPRODUCED here. The recommendation is still wrong,
because the illegal twins it checked were all of ONE SHAPE.

17a's abuse battery is X01 and X02 — both a **bare** `*mut &'a i64` parameter. I wrote the
same escape with the pointer carried in an **array** parameter (B02) and in a **tuple**
parameter (X04). Both are refused on base and **admitted by the arm**:

| abuse program | carrier | rustc 1.98.1 | base | ARMED |
|---|---|---|---|---|
| X01 (17a) | bare `*mut &'a i64` param | REFUSES E0597 | `1/1/-` | `1/1/-` held |
| X02 (17a) | bare param, `'static` demand | REFUSES E0597 | `1/1/-` | `1/1/-` held |
| B01 (mine) | fn-pointer param | REFUSES E0597 | `1/1/-` | `1/1/-` held |
| **B02 (mine)** | **`[*const &'a i64; 2]` param** | **REFUSES E0597** | `1/1/-` | ⛔ **`0/0/5` ADMITTED** |
| **X04 (mine)** | **`(*const &'a i64, i64)` param** | **REFUSES E0597** | `1/1/-` | ⛔ **`0/0/6` ADMITTED** |

Exit 5 and exit 6 are the dead locals' own values: the programs read the stack slot after
the scope ended and found what used to be there. This is the failure PREDICTIONS.md named in
advance — *"If B01, B02, X01 or X02 is ADMITTED, the arm is condemned"* — and it fired.

**Why the bare shape is not representative.** `MENTIONED` is the permissive answer ("the
caller constrains this binder, we merely cannot name the constraint"). The arm's premise is
that the comparators still check the caller's own constraint afterwards. For the bare
parameter something downstream does refuse the escape. For the array and tuple carriers
nothing does. The arm relaxes a refusal without the compensating check, and the harness
cannot see it: **cost 0 in every column, and two use-after-scope programs admitted.**

## THE COLUMNS REPRODUCED, AND THEY ARE NOT A SAFETY CLAIM

| column | measured |
|---|---|
| queue gate, whole ledger, armed vs unarmed | **3 rows move**, 0 open — exactly the predicted set |
| the two target rows | `mutptr` `0/0/0` (rustc 0) · `fnptr_call_result` `0/0/18` (rustc 18) |
| `refptr` | `0/0/1` — compiles, WRONG (rustc 0) |
| controls `static_arg_pins`, `generic_type_param_two_regions` | unmoved, `1/1/-` |
| base queue gate, 233 rows | rc 0, all hold |

Rule 5 in one line: **cost 0 is not a safety claim, and neither are hand programs all of one
shape.** Every harness column agreed with the handoff. Two programs I wrote refuted it.

## THE LEGAL SIDE WORKED — WHICH IS WHY THIS IS A WALL AND NOT A DEAD END

| program | base | ARMED | rustc twin |
|---|---|---|---|
| G01 ptr-to-ptr `*mut *mut &'a i64` | `1/1/-` | `0/0/0` | accepts, runs 0 |
| G02 ptr inside an array param | `1/1/-` | `0/0/0` | accepts, runs 0 |
| L01/L02/L05 (17a) | `1/1/-` | `0/0/0` | accepts |
| L04 fn-ptr param region | `1/1/-` | `0/0/0` | accepts |
| G03/G04 fn-ptr controls | `0/0/0` | `0/0/0` | accepts — already legal on base |

The door is real and the diagnosis is right. What is missing is the escape check that must
accompany the relaxation, and that is a different change from this one.

## NEIGHBOURS — measured (standing rule 2026-09-12)

| neighbour | closed here / rowed | reason and number |
|---|---|---|
| `K::FnPtr` at the same walker | neither — the whole arm is withdrawn | it closes `fnptr_call_result` (`0/0/18`) and its own abuse twin B01 HOLDS, so it is the salvageable half; it is withdrawn only because it ships in one edit with the Ptr arm |
| `K::TraitObject`/`DstRef` arm | **NOT co-landed — reason 1, no carrier** | C01 (`&'a dyn Shape` param) compiles and runs 0 on BASE and is unmoved armed. A `&dyn` parameter reaches TraitObject only through the Ref arm, which already inserts the region. 17a co-landed this on a cost-0 reading; it changes no program's verdict |
| Slice `lifetime()` slot | **NOT co-landed — reason 1, no carrier** | C02 (`&'a [i64]` param) compiles and runs 0 on base, unmoved armed: the region lives on the enclosing Ref, not on the Slice |
| `collect_input_regions_` | rowed, reason 1 — no carrier | N01/N02 compile and run 0 on base; re-measured, unmoved |

## THE TWO DEFECTS THE COUNTER-EXAMPLES FOUND — BOTH ON THE SHIPPED BINARY, NO ARM

Rows opened, `# TOTAL` 233 → 235, both re-derived by direct listing:

**1. `ptr_under_struct_field_region_escape_admitted` (tier 1, `admits`).** X05 — the same
escape with the pointer in a STRUCT FIELD — **compiles clean and runs (exit 7) on BASE**.
rustc refuses it, E0597. The bare, array and tuple carriers are all refused on base, but by
the `variance mismatch — expected *const &'static i64` sentence, i.e. by a *different
defect*; the struct carrier reaches the binder by another route, so nothing refuses it at
all. ⚠ This means the accidental refusal, not an escape check, is what has been holding the
other three — and it is exactly what the arm removes.

**2. `refptr_param_eq_compares_outer_ref` (tier 1, `run 1`).** D02 — `==` on a `&*mut i64`
parameter compares the OUTER REFERENCE, not the pointer: two copies of one pointer compare
unequal, exit 1 where rustc gives 0. Control on the same binary: the direct `p == q`
spelling runs 0 correctly (D01).

⚠ **IT CORRECTS A RECORDED CLAIM.** 17a saw this only through `&*mut &'a i64` and recorded
it as *"the 'static spelling is still refused on base, so the wrong answer is unreachable
until this round's region door opens — doors in series."* **Measured here: remove the inner
reference and it is reachable today, on the shipped compiler, with no arm.** The reachability
finding was an artifact of testing one spelling.

## CONTRADICTIONS OF RECORDED CLAIMS

1. 17a: "both illegal twins still refused ⇒ the abuse direction is paid for." Refuted — the
   twins were one shape; two more carriers are admitted.
2. 17a: the `&*mut` equality wrong answer is "unreachable until the region door opens".
   Refuted — D02 reproduces on base.
3. 17a co-landed the TraitObject/DstRef and Slice arms at cost 0. Measured: **no carrier**;
   C01/C02 are unmoved. Cost 0 was not evidence of value, only of silence.
4. The prompt's tier-3 count is stale in the same direction 17a recorded: it says 123, the
   tree says **121** (of 233 before this round's two tier-1 rows).

## WHAT THE NEXT ROUND SHOULD DO — THE WALL, WITH ITS NUMBER

The Ptr/FnPtr arm closes **2 tier-3 rows** and admits **2 use-after-scope programs**. It
cannot land until the escape check exists for a region reached through a raw pointer. The
right question is `ptr_under_struct_field_region_escape_admitted`: that row is the SAME
missing check, reachable with no arm, and a fix for it is the prerequisite door. Doors in
series, and this time the series is measured rather than assumed — **fix the escape check
first, then this arm is a two-row closure with its abuse direction genuinely paid.**

## MISTAKES OF MY OWN

* My first G02 Rust twin did not compile — `unsafe { **t[0] } + unsafe { **t[1] }` is a
  parse error in Rust, mine and not the compiler's. Rewritten as one `unsafe` block, and the
  Logos program was changed to match so the twin is a twin.
* I ran `tools/dlog/ask.sh` against `src/compiler/sema_impl.cpp`, which does not exist — it
  is a header. The owed BASE-tree run of `region_walker_arms.dl` is therefore **still not
  done**: by the time I had the right TU list the tree was armed, and I did not re-run it
  after the revert. It stays owed, and I say so rather than reporting the armed run as base.
* I added the two shelf programs while the armed queue-gate run was still going, so that
  run's tail reports "on the shelf with NO ledger row" for both — an artifact of my timing,
  not a finding. The authoritative gate result is the clean re-run over 235 rows.
