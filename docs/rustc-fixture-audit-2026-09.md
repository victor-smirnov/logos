# rustc fixture audit, 2026-09-16 — the disputed fixtures

**What this document is.** On 2026-09-15 the owner installed **rustc 1.98.1 (48a229cea 2026-09-01)**
on the box. Until that day every "legality by reading, no rustc binary" marker in the corpus was a
CLAIM, not a measurement. On 2026-09-16 all **786** marked fixtures were measured against Rust twins
compiled (and, where the fixture asserts an exit code or stdout, RUN) with

    /home/victor/.rustup/toolchains/stable-x86_64-unknown-linux-gnu/bin/rustc --edition 2024

Every twin, every rustc stderr, and the four per-shard verdict tables are under
`src/compiler/probes/2026-09-16a-fixtureaudit/`. The verdict is re-runnable.

**Result, by direct tally of the four verdict tables — 786 = 752 + 6 + 23 + 5:**

| class | n | what was done to the header |
|---|---|---|
| CONFIRMED | **752** | 750 had the marker replaced in place by the measured verdict + the twin's path; **2** were left untouched because their matched phrase is incidental prose, not a marker (Part 3) |
| CONTRADICTED | **6** | marker replaced + a one-line pointer to this document; **assertion unchanged** |
| DIVERGENCE | **23** | marker replaced + a one-line pointer to this document; **assertion unchanged** |
| NOT EXPRESSIBLE | **5** | untouched — grep false positives, no legality claim (Part 3) |

**This document holds the 29 that are NOT plain CONFIRMED.** Per the standing rule, a pass fixture
asserting a divergence, and a fixture rustc contradicts, is **a corpus decision with an owner**:
it is reported, never repaired. **No program, no `.expected` and no ledger row was changed by this
round.** Each fixture below carries a one-line pointer to this document in its header and its
assertion is untouched.

⚠ **A twin is a translation.** A CONFIRMED verdict certifies the behaviour of the program the
auditor wrote, not that the Logos fixture is byte-for-byte that program. Every non-mechanical
rewrite is marked `// TWIN:` in the `.rs` file and carried in the TSV's `note` column.

---

## Part 1 — CONTRADICTED (6): rustc disagrees with the corpus

### bc_0907q_dbmcarry_hb_c13_admit
*`tests/logos/pass/bc_0907q_dbmcarry_hb_c13_admit.logos` · shard 1 · twin `gen4/bc_0907q_dbmcarry_hb_c13_admit.rs`*

**What the tree asserts.** `exit: 0`, stdout **`D5` `D5`** — the destructor of `S { n: 5 }` runs
**twice**.

```
let p: (E, i64) = (E::V { f: S { n: 5i64 } }, 9i64);
match &p {
    (E::V { f }, b) => { out = f.n + *b; },
    ...
}
```

**What rustc says.** The twin compiles and runs, **exit 0, stdout `D5` — once.** `match &p` is a
match on a reference: `f` binds by reference under Rust 2024's default binding mode, nothing is
moved out of `p`, and `p`'s single `S` is dropped exactly once at end of scope.

**Which registry applies.** None. The three drop clauses that could shelter this
(`expr.drop.tuple-array-index-order`, `borrow.move.move-closure-capture-drop-once`,
`intrinsic.drop.closure-env-drop-glue`) are all recorded Rust-conformant, and
`design_drop_semantics_rust_canonical` (Victor, 2026-09-08/09) makes drop semantics Rust-canonical
outright. A16 is not in play: `S` has an `impl Drop`, which is A16's own opt-out.

**Why it matters more than the other five.** This is a **double drop pinned as correct**. The other
contradictions are refusals or exit codes; this one asserts that running a destructor twice on one
value is the right answer, and a green pass fixture is the strongest form of that claim.

**The two candidate decisions.**
1. **Rust-canonical (default under the standing rule):** the fixture asserts a defect. It becomes a
   soundness-queue row (`observed: run 0` with a destructor count — a double free is invisible to an
   exit code), the `.expected` drops to one `D5`, and the fix is in the by-reference match binder.
2. **A new blessed divergence** saying a by-ref enum-struct-variant binder carries its payload. Nothing
   in the tree argues for this, and it would contradict `design_drop_semantics_rust_canonical`.

---

### bc_0914a_ptrcoerce_hb_d01_admit and bc_0914b_ptrcoerceland_hb_m10_admit
*`tests/logos/pass/…` · shard 1 · twins `gen3/bc_0914a_ptrcoerce_hb_d01_admit.rs`,
`gen3/bc_0914b_ptrcoerceland_hb_m10_admit.rs`*

**These two are ONE finding: `&mut [T; N]` → `*mut T` array decay.**

**What the tree asserts.** d01 `exit: 0`, m10 `exit: 7` — both compile and run.

```
// d01
fn f<'a>(x: &mut [&'a i64; 2]) -> *mut &'a i64 { return x; }
// m10
let mut a: [&'static i64; 2] = [&G, &G];
let q: *mut &i64 = &mut a;
```

**What rustc says.** Both twins **refused, `error[E0308]: mismatched types`** — expected
`*mut &i64`, found `&mut [&i64; 2]`. Rust has no implicit decay from a reference-to-array to a raw
pointer-to-element; the conversion needs `as *mut _` on a slice or `.as_mut_ptr()`.

**Which registry applies.** **None — and this is the load-bearing part.** `docs/DIVERGENCES.md` has no
row for it, and neither does any `coerce.*` clause: `coerce.unsize.ref-concrete-to-trait-object` is
trait-object unsizing, not array decay, and the "decayed" wording elsewhere in the coercion section is
about range-indexing to `&[T]`. **An unregistered coercion is pinned as correct by two pass fixtures.**

**The two candidate decisions.**
1. **Rust-canonical:** the coercion is removed, both fixtures become `fail` fixtures asserting an
   E0308-shaped refusal, and the programs that need it spell `.as_mut_ptr()`.
2. **Canonise it** as a new `coerce.ptr.array-ref-to-elem-ptr` clause with an explicit note that it
   is a deliberate Logos addition. ⚠ This is the branch that needs the owner's eye: it hands out a
   raw pointer with no length, which is exactly the shape the `ptrcoerce` rounds were opened to
   tighten.

---

### bc_0914b_ptrcoerceland_hb_v02i_admit
*`tests/logos/pass/…` · shard 1 · twin `gen4/bc_0914b_ptrcoerceland_hb_v02i_admit.idx.rs`*

**What the tree asserts.** `exit: 9`.

```
let e: &i64 = &v[1u64];
v[*e as u64] = 9i64;
```

**What rustc says.** **Refused, `error[E0502]: cannot borrow `v` as mutable because it is also
borrowed as immutable.`** The shared loan `e` into `v` is still live at the point of the indexed
store, and the store needs `&mut v`.

**Not an index artifact.** The auditor re-measured with the index written `usize` rather than `u64`
(`.idx.rs`, which is why that twin carries the suffix): the refusal persists, so it is the loan, not
the cast. Not A16 (no struct), not a lifetime-naming question.

**The two candidate decisions.**
1. **Rust-canonical:** a queue row, `observed: admits` — an illegal program compiles — and the fix is
   to keep the shared loan live across the `IndexMut` autoref.
2. Decide that Logos's two-phase `IndexMut` borrow deliberately starts after argument evaluation and
   canonise it. ⚠ Note the tree has already been bitten here once: the fail fixture
   `bc_0915f_rustcaudit_vec_index_store_len_in_index_refuse` records that exactly this "the
   `IndexMut` autoref is a two-phase borrow" reading was a reading, made when no rustc was on the box.

---

### bc_0915b_refeqland_hb_g11_admit
*`tests/logos/pass/…` · shard 2 · twin `shard2-hb-late/bc_0915b_refeqland_hb_g11_admit.rs`,
controls `…_CTL_eq_only.rs` and `…_CTL_lt_reborrow.rs`*

**What the tree asserts.** `exit: 0` — a `&mut (bool,u8,f32)` compared against a `&(bool,u8,f32)`
with **both** `==` and `<`.

**What rustc says.** **Refused, `error[E0308]: mismatched types … types differ in mutability`**, at
`ra < rb` only.

**Decomposed, so the finding names the exact half** — this is the part that keeps it from being
over-stated:
- `_CTL_eq_only` (the `==` line alone): **compiles, exit 0.** `core` ships
  `impl PartialEq<&B> for &mut A`, so the `==` half is **Rust-canonical and not in dispute**.
- `_CTL_lt_reborrow` (`&*ra < rb`): **compiles, exit 0.** The ordering *answer* is Rust's.
- So the Logos-only part is exactly the **implicit `&mut` → `&` reborrow at a relational operator**,
  because `core` ships no cross-mutability `PartialOrd`.

**Which registry applies.** None. A1–A17 has no row, and none of the six `*.binop.*` clauses
(`expr.binop.integer-overflow-trap`, `expr.binop.comparison-signedness`,
`expr.binop.string-vs-str-eq`, `trait.binop.partial-ord-derive`, `trait.binop.tuple-eq-impl`,
`type.binop.bitwise-integer-or-bool`) covers mixed-mutability comparison or a reborrow at a
relational operator. Its sibling `g11s_admit` (all-shared ordering over the same tuple) is CONFIRMED,
so **only the mixed-mutability spelling is at issue**.

**The two candidate decisions.** (1) Rust-canonical: refuse, and the program spells `&*ra < rb`.
(2) Canonise the implicit reborrow at relational operators — which is arguably the coherent extension
of `trait.bounds.partialeq-via-eq`, since Logos already routes these operators through its own trait
shape rather than `core`'s impl set.

---

### bc_0913d_staticdemand_hb_x06_refuse — **an over-refusal, the expensive direction**
*`tests/logos/fail/…` · shard 4 · twin `shard4-fail/twins/bc_0913d_staticdemand_hb_x06_refuse.rs`*

**What the tree asserts.** A refusal, in these words:

    [fn f]: call to 'two': caller does not satisfy callee's outlives bound `'a: 'b`
    (under arg-type substitution: `'p: 'q` required)

**What rustc says.** **ACCEPTS. exit 0, empty stderr.** The twin is verbatim-faithful — no `// TWIN:`
rewrite was needed.

```
impl K { fn two<'a,'b>(&self, x: &'a i64, y: &'b i64) -> i64 where 'a: 'b { … } }
fn f<'p,'q>(k: &K, x: &'p i64, y: &'q i64) -> i64 { return k.two(x, y); }
```

`'a` and `'b` are the **callee's** region parameters. rustc infers them afresh at the call site and is
free to shrink `'a` to satisfy `'a: 'b`; no obligation is transferred to the caller's `'p`/`'q`.
Logos substitutes the caller's regions literally and then demands `'p: 'q`.

**This is a LEGAL PROGRAM REFUSED** — per the round's brief the most expensive thing in the queue to
get wrong, and here it is pinned as a *fail fixture*, which is the strongest form of the claim.
It will not be caught by pattern: of the `staticdemand` family, **29 siblings in this same `fail`
shard are CONFIRMED, and 11 more in `tests/logos/pass` — 40 correct fixtures around one wrong one.**

**The two candidate decisions.** (1) Rust-canonical: the fixture is deleted or inverted and a
soundness-queue row is opened with `observed: refuses` (fix = it compiles AND RUNS). (2) Keep an
explicitly-declared stricter outlives rule and canonise it — but note that would be a divergence in
the *refusing* direction, which the tree has no other instance of in this family.

---

## Part 2 — DIVERGENCE (23): the difference is blessed, and the clause is named

These are **not** defects. Each is a real difference from Rust that a registry row or a `docs/spec`
clause already canonises. They are listed so that the corpus records *which* clause each pass fixture
leans on — a pass fixture resting on a divergence is still a corpus decision the owner should see.

### 14 × lifetimes are not structural (E0106 / E0621 / E0637)
`bc_0908_fatrepr_fix_hb_g1_admit`, `…_g2_admit`, `…_g3_admit`, `…_g4_admit`, `…_g6_admit`,
`…_g7_admit`, `…_g8_admit`, `bc_0908_fatrepr_hb_b4_admit`, `bc_0911d_liferegb_hb_l1_admit`,
`…_l5b_admit`, `…_l7_admit`, `bc_0912d_slicehop_hb_l15_admit`,
`bc_0913f_declarrivalland_hb_g_w01_admit`, `bc_0914f_thrurefland_hb_d12_admit`.

**What the tree asserts.** Each compiles and exits with its pinned code (0, or 5 for `d12`/`g_w01`).
The shapes are `enum E { N, S(&[i64]) }`, `struct P { a: &i64, b: &i64 }`, `struct H { d: &dyn Tr }` —
**a reference type in a declaration with no lifetime written at all.**

**What rustc says.** `error[E0106]: missing lifetime specifier` for eleven of them; `E0621` for `l7`
(explicit-lifetime-required through an array element); `E0637` for `g_w01`
(`where &T: Show` needs `&'a T` with a named binder). `d12` fails on a related elision shape.

**Clause.** The family is canonised four times over:
`grammar.generic.hrtb-binder` (grammar.md:117 — the `for<'a>` binder is parsed and **discarded**),
`generic.bound.lifetime-arg-not-structural` (traits-generics.md:2064),
`region.impl.trait-arg-lifetime-erased` (ownership.md:1813), and
`type.identity.lifetime-ignored` (divergences.md:2576 — `&'a T` and `&'b T` are the same type).

⚠ **Reported precisely, because the family is registered but this exact spelling is not.** All four
clauses are about lifetimes not being *structural* — in bound dispatch, in trait selection, in type
identity. **None of them names declaration-site elision** — the right to write `struct P { a: &i64 }`
with no lifetime parameter at all. The divergence is real and its logic plainly covers these
programs, but the registry entry that should say so in as many words does not exist. That is a
documentation gap for the owner, not a defect in the fixtures.

### 2 × A16 structural auto-Copy
`bc_0914f_thrurefland_hb_d15_admit`, `bc_0914f_thrurefland_hb_o11_admit` (both `exit: 5`).

rustc refuses both with **`error[E0505]: cannot move out of `s1` because it is borrowed`**. Under
`#[derive(Clone, Copy)]` **both twins accept and give exit 5** — and they are the **only two** of the
35 copy-variant twins the auditor ran whose refusal vanishes that way, which is what makes the
attribution decisive rather than assumed.

**Row/clause:** `docs/DIVERGENCES.md` **A16** (structural auto-Copy, canonised by Victor 2026-08-24),
spelled in the spec as `type.copy.struct-structural-auto` (divergences.md:2362) **and again** as
`type.copy.structural-auto` (divergences.md:2366) — ⚠ **two clause ids state the same rule**; the
owner may want one retired. Both fixtures' headers already claimed A16; the audit confirms the claim.

⚠ **Method note, because it changed the answer.** The auditor first keyed the A16 instrument on
**E0382** (use-after-move) and concluded A16 "never fires". It fires as **E0505** (move-out-of-borrowed).
That is enumeration by spelling rather than by property — and before that, the whole copy-variant
instrument was silently dead, because a `.copy.rs` filename makes an invalid crate name, so all 35
variants had been failing on *naming* and were recorded as "copy also refuses".

### 2 × A6 closure-type syntax
`bc_0909g_closureenv_hb_f8_admit`, `bc_0909g_closureenv_hb_r3_admit` (both `exit: 0`).

Both take a closure by the Logos type syntax in parameter position — `fn f(inner: ||->i64, …)`.
rustc cannot parse it (`expected type, found ||`); Rust spells this with an `Fn`-family bound.

**Clause:** `type.closure.type` (divergences.md:554 and types.md:1674), whose text names **A6**
outright: *"A6: Rust spells closures via Fn-family bounds; Logos has a dedicated `|..|->R` closure
type syntax."* Not expressible in Rust as written; the *behaviour* is ordinary.

### 1 × function overloading by signature
`bc_0908_fatrepr_fix_hb_d8_admit` (`exit: 0`) — two inherent `eat` methods on `H`, one taking `i64`
and one `Rc<dyn Sp>`. rustc: **`error[E0592]: duplicate definitions with name `eat``**.

**Clause:** `item.fn.signature-overloading` (items.md:194), whose Divergence line reads *"Rust does
not permit free-function overloading by signature."*

### 1 × PartialOrd is an empty marker in the Logos stdlib
`bc_0914o_autoreffund_hb_h06_admit` (`exit: 0`, stdout `n=1001 r=0`). rustc: **`error[E0046]: not all
trait items implemented`** — Logos's `impl PartialOrd for W { fn lt … }` supplies only `lt`, but
Rust's `PartialOrd` requires `partial_cmp`.

**Clause:** `trait.bounds.partialeq-via-eq` (divergences.md:2337): *"Logos Eq/Ord carry the methods
Rust puts on PartialEq/PartialOrd; full split pending."* Logos's stdlib declares
`pub trait PartialOrd {}` as an empty marker.

### 3 × relational operators from an INHERENT method
`bc_0915a_refeq_hb_e39_admit` (inherent `partial_cmp`), `bc_0915b_refeqland_hb_f08r_admit` and
`…_f08s_admit` (inherent `lt`) — all `exit: 0`.

The **literal** twins are refused, **`error[E0369]: binary operation `<=` cannot be applied to type
`&D`` … "an implementation of `PartialOrd` might be missing"**. Rewritten as a real `impl PartialOrd`,
**all three compile and exit 0.**

**Clause:** `trait.binop.partial-ord-derive` (traits-generics.md:1369) — relational ops derive from
`partial_cmp`/`lt` when the direct method is present; its own text says it *"Mirrors Rust's default
PartialOrd lt/le/gt/ge bodies."*

**So for these three the ANSWER is Rust-canonical and only the ROUTE diverges**: Rust demands the
method arrive through a trait impl, Logos accepts an inherent one. Both literal-translation controls
are kept beside the twins as `*_INHERENT.rs`.

---

## Part 3 — the 5 NOT EXPRESSIBLE, which are grep false positives

`deem_direct_stream_pull`, `deem_incr_diff_harness`, `lforge_graph_cas`,
`wql_domain_static_u64_sum_accumulator`, `generic_ref_registry_dispatch`.

In each the phrase "by reading" is **incidental prose** — "counts ten blocks by reading the emitted",
"Build inputs by reading from disk" — and not a legality marker. All five are Logos-only (Deem,
Writ/wql, lforge FS-CAS, `GENERIC_REF`/`wstatic_hash_of`), i.e. A6 additions with no Rust twin.
**Their headers were not touched.**

Two more, `bc_d1r5_h7_param_root_admits` and `destructure_param_move_elem_no_double_free`, match the
grep only on incidental prose but *do* make a legality claim elsewhere in their headers. They were
measured anyway — **both CONFIRMED** (accepts and runs, exit 7 and exit 0) — but since the matched
phrase is mid-sentence and not a marker, rewriting it would have corrupted the sentence, so their
headers are likewise unchanged and the verdict is recorded only in the TSV.

---

## Part 4 — what the audit says about the corpus as a whole

- **752 of 786 marked fixtures were right** (and 5 of the remainder were never claims at all).
  The "by reading" markers were overwhelmingly sound.
- **The misses do not cluster.** Six contradictions across four families, and the worst of them
  (`x06`) sits among 29 correct siblings. **A wrong reading is not findable by pattern** — which is
  the argument for this audit having been run exhaustively rather than by sampling.
- **Two of the six are unregistered coercions** (array decay, cross-mutability relational reborrow):
  behaviour the compiler has that no divergence row or clause describes. Those are the ones most
  likely to surprise, because nothing in the documentation predicts them.
- ⚠ **The auditors' own instruments were wrong more often than the tree was.** Recorded here because
  it bounds how much this document is worth: a piped `$?` read rc 0 off a failing rustc (every verdict
  would have been a false "accepted"); `printf` over a non-NUL-terminated Rust `&str` bled rodata into
  stdout and manufactured 38 false mismatches; a `.copy.rs` filename made an invalid crate name and
  silently disabled the entire A16 instrument; and **58 of one shard's 93 first-round refusals were
  translator bugs, not tree defects.** Every refused twin was therefore hand-read before being
  classified, and no fixture was called CONTRADICTED on a script's word alone.
