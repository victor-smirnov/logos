# Memoria SSRLE — seven correctness bugs

**Component:** `core/ssrle` (packed searchable RLE symbol sequence) and its packed-container SO surface.
**C++ tree:** `github.com/victor-smirnov/memoria` @ `870b6aa6d` (2025-07-02).
**Found:** during an independent reimplementation of SSRLE (codec, run algebra, packed
structure, full rank/select/count op family). Each bug below was reproduced against the
listed source, then fixed in the port; the "Correct behavior" note states what the port does.

Paths are relative to the repository root:

- `C` = `core/include/memoria/core/ssrle/`
- `P` = `containers/include/memoria/core/packed/sseq/`
- `T` = `tests/reactor/tests/packed/sequence/ssrle/`

---

## Summary

| # | Site | Class | Effect |
|---|------|-------|--------|
| 1 | `C/ssrle_7bps.hpp:37,65` + `C/ssrle_common.hpp:519,545` | codec | `Bps=7`, `plen=8` run encodes to a null run — silent data loss |
| 2 | `C/ssrle_common.hpp:1209` | dead template | `SSRLERun::remove` calls an undefined `Traits::remove` |
| 3 | `C/ssrle_common.hpp:223` | invalid run | split of `{plen=1, rl>1}` at the ends emits `{rl=0}` runs |
| 4 | `C/ssrle_common.hpp:314,323` | un-encodable run | insert same-pattern fast paths sum `run_length` with no budget check |
| 5 | `P/packed_ssrle_seq_so.hpp:557,569` | wrong result | `select_bw_gt`/`select_bw_ge` mirror-total via `rank_neq` (copy-paste) |
| 6 | `P/packed_ssrle_seq_so.hpp:660` | off-by-one | `count_bw` never examines `seq[idx]`; miscounts the blocker (oracle-blind) |
| 7 | `P/packed_ssrle_seq_so.hpp:72,449` | unsigned wrap | `select_fw_*_from` `dec_rank` underflows when the match is in the anchor's run |

---

## Bug 1 — `Bps=7`: `max_pattern_length()` exceeds the `LEN_BITS` field width

**Where.** `C/ssrle_7bps.hpp`:

```
LEN_BITS  = 3                                   // :37
DATA_BITS = CODE_WORD_BITS(64) - CODE_WORD_SIZE_BITS(2) - LEN_BITS(3) = 59   // :40
max_pattern_length() = DATA_BITS / Bps = 59 / 7 = 8                          // :65-67
```

The run header carries the pattern length in a `LEN_BITS`-wide field. For `Bps=7`,
`LEN_BITS=3` encodes only `0..7`, but `max_pattern_length()` returns **8**.

**Why it is a bug.** Encode writes the pattern length with no mask to the field width
(`C/ssrle_common.hpp:519`):

```cpp
code_word |= run.pattern_length() << CODE_WORD_SIZE_BITS;   // 8 == 0b1000, three-bit field
```

Decode reads exactly `LEN_BITS` bits (`C/ssrle_common.hpp:545`):

```cpp
run.pattern_length_ = code_word & make_mask_safe(PATTERN_LEN /* =3 */);   // 0b1000 & 0b111 == 0
```

A `plen=8` run round-trips to `plen=0`, i.e. a **null (stream-terminator) run** — the eight
symbols are silently dropped, and the high bit of the length also leaks into the pattern field.
`Bps=7` is the only affected width: for every other `Bps`, `DATA_BITS/Bps ≤ 2^LEN_BITS − 1`.

**Trigger.** Any `plen=8` run at `Bps=7`. It is reachable through normal construction:
`make_run` caps at `max_pattern_length()==8` (`C/ssrle_common.hpp:94-96`), and the
`insert`/`merge` pattern-concatenation paths gate only on `is_fit` (the `DATA_BITS` budget:
`8*7+0 = 56 ≤ 59`, so `is_fit` says yes) — neither path knows about the `LEN_BITS` cap.

**Correct behavior (port).** Cap the encodable pattern length at
`min(DATA_BITS/Bps, 2^LEN_BITS − 1)` — i.e. **7** for `Bps=7` — on `make_run` and on every
pattern-concatenating branch. Identical to C++ for all `Bps ≠ 7`.

---

## Bug 2 — `SSRLERun::remove` calls an undefined `Traits::remove` (dead template)

**Where.** `C/ssrle_common.hpp:1209`:

```cpp
SSRLERunArray<Bps> remove(RunSizeT from, RunSizeT to) const {
    return Traits::remove(*this, from, to);      // :1211
}
```

**Why it is a bug.** No `remove` static method is defined in `SSRLERunTraits<Bps>`,
`SSRLERunCommonTraits<Bps>`, or any `ssrle_*bps.hpp` specialization (a repository-wide search
of `core/ssrle/*.hpp` finds only the two lines above). The member is well-formed only because
member functions of a class template are instantiated on use — nothing in the tree calls
`SSRLERun::remove`, so the missing symbol is never diagnosed. Any first caller is an immediate
compile error.

**Trigger.** `run.remove(a, b)` anywhere.

**Correct behavior (port).** Run-level remove is derived from the existing
extract/split machinery (`split` twice, drop the middle; equivalently
`extract_to`/`remove_to`). The member should either be given a real definition or deleted.

---

## Bug 3 — `split` of `{plen=1, rl>1}` at the ends emits invalid `{rl=0}` runs

**Where.** `C/ssrle_common.hpp:223-237`, the `run_length() > 1 && pattern_length() == 1`
branch:

```cpp
auto left  = run;
auto right = run;
left.run_length_  = at;                          // :230
right.run_length_ = run.run_length() - at;       // :231
split.left.append(left);                          // :233  — unconditional
split.right.append(right);                        // :234  — unconditional
```

**Why it is a bug.** For `plen=1`, `full_run_length == run_length`, so a legal split point
`at` ranges over `0..run_length`. At `at == 0`, `left.run_length_ == 0`; at `at == run_length`,
`right.run_length_ == 0`. Both are appended without a guard, so the result carries an
**invalid `{plen=1, rl=0}` run**. Every other branch guards this: the `plen>1` case checks
`if (left) … if (right) …` (`:251-252`, `operator bool` == `run_length != 0`), and the
`run_length()==1` case special-cases `at==0` / `at==pattern_length` (`:196-201`). Downstream
`count_symbols` happens not to break (`0 × plen == 0`), but the run vector is malformed and any
consumer that assumes `rl ≥ 1` (or re-encodes it) is exposed.

**Trigger.** `SSRLERun{plen=1, rl>1}.split(0)` or `.split(rl)`.

**Correct behavior (port).** Push each half under the same validity guard the other branches
use — a zero-length half is dropped, matching the `plen>1` path.

---

## Bug 4 — insert same-pattern fast paths sum `run_length` with no budget check

**Where.** `C/ssrle_common.hpp`, `insert`, the two same-pattern fast paths:

```cpp
if (self.pattern_length()==1 && run.pattern_length()==1 && self.pattern()==run.pattern()) { // :314
    auto r_run = self; r_run.run_length_ += run.run_length(); result.append(r_run); return; }
else if (is_same_pattern(self, run) && at % self.pattern_length() == 0) {                     // :323
    auto r_run = self; r_run.run_length_ += run.run_length(); result.append(r_run); return; }
```

**Why it is a bug.** Both paths concatenate two same-pattern runs by summing `run_length`
with **no `is_fit` check**. `merge` — the analogous operation — guards the identical sum
(`:479-487`):

```cpp
if (RunTraits::is_fit(self.pattern_length_, self.run_length_ + run.run_length())) {
    self.run_length_ += run.run_length(); return true;
}
```

`run_length` is stored in the residual bits after the header and pattern
(`DATA_BITS − plen*Bps` bits). A sum whose `run_length_bitsize` exceeds that residual makes the
run **un-encodable**: `estimate_size` then needs more than `CODE_UNITS_PER_WORD_MAX (=4)` units,
which overflows the 2-bit unit-size field. `insert`'s output is thus not safe to hand to the
codec, whereas `merge`'s is.

**Trigger.** Insert two runs of the same single-symbol pattern whose combined `run_length`
crosses the `max_run_length(plen)` boundary — e.g. two `{plen=1, rl}` runs with
`rl ≈ max_run_length(1)`.

**Correct behavior (port).** Guard the sum with `is_fit(plen, rl_self + rl_run)` on both fast
paths; on overflow, fall through to the general path, which splits into encodable pieces.

---

## Bug 5 — `select_bw_gt` / `select_bw_ge` compute the mirror total with `rank_neq`

**Where.** `P/packed_ssrle_seq_so.hpp`, the no-index backward-select forms:

```cpp
SelectResult select_bw_gt(SeqSizeT rank, SymbolT symbol) const {          // :557
    SeqSizeT size = this->size();
    SeqSizeT full_rank = rank_neq(size, symbol);                           // :560  — should be rank_gt
    if (rank < full_rank) return select_fw_gt(full_rank - rank - 1, symbol);
    else return SelectResult{size, full_rank};
}
SelectResult select_bw_ge(SeqSizeT rank, SymbolT symbol) const {          // :569
    SeqSizeT full_rank = rank_neq(size, symbol);                           // :572  — should be rank_ge
    ...
}
```

**Why it is a bug.** Backward select mirrors forward select about the op's own total:
`select_bw(rank) = select_fw(full_rank − rank − 1)`, where `full_rank` must be the total count
of matches **for that op**. `select_bw_gt`/`select_bw_ge` instead use `rank_neq` — a
copy-paste from `select_bw_neq` (`:524`). The sibling forms are correct
(`select_bw_lt` → `rank_lt` `:536`, `select_bw_le` → `rank_le` `:548`, `select_bw_eq` →
`rank_eq` `:512`) and so are **all** the from-index forms (`:604-624`, which use `rank_gt` /
`rank_ge`). When symbols smaller than `symbol` are present, `rank_neq > rank_gt`, so the
mirrored forward rank is wrong: the found position is off, and the not-found branch reports
`rank_neq` as the total instead of the gt/ge count.

**Trigger.** Any sequence containing both symbols `> symbol` and symbols `< symbol`
(so `rank_neq ≠ rank_gt`), backward-selected with `op ∈ {GT, GE}`.

**Correct behavior (port).** Use the op's own total (`rank_gt` / `rank_ge`) for every op;
verified by a reversed-stream symmetry law (`select_bw` over a stream equals
`select_fw` over its reverse).

---

## Bug 6 — `count_bw` never examines `seq[idx]` and miscounts the blocker (oracle-blind)

**Where.** `P/packed_ssrle_seq_so.hpp:660-670`:

```cpp
SeqSizeT count_bw(SeqSizeT idx, SymbolT symbol) const {
    SeqSizeT rank = rank_neq(idx, symbol);                       // [0, idx) — excludes seq[idx]
    if (rank) {
        SeqSizeT next_idx = select_fw_neq(rank - 1, symbol).idx; // last non-sym strictly before idx
        return idx + 1 - next_idx;
    }
    else return idx + 1;
}
```

**Why it is a bug.** `count_bw` should return the length of the maximal run of `symbol`
**ending at `idx` inclusive**, i.e. `0` when `seq[idx] != symbol`. The computation takes
`rank_neq` over the half-open prefix `[0, idx)`, so it **never looks at `seq[idx]`**, and the
`idx + 1 - next_idx` form counts the blocking non-symbol itself. Contrast `count_fw` (`:653`),
whose `select_fw_neq(rank)` naturally includes `seq[idx]`.

Concrete failures (`symbol = A`):

- `seq = [A, A, B]`, `idx = 2` (`seq[2]=B`): correct `0`; C++ returns `3`
  (`rank_neq(2)=0` → `idx+1`).
- `seq = [B, A, A]`, `idx = 2`: correct `2`; C++ returns `3`
  (`rank_neq(2)=1`, `next_idx=0`, `idx+1-0=3` — counts the leading `B`).

**Oracle-blindness (note).** The test reference model reproduces the identical composition —
`T/ssrleseq_test_base.hpp:741-756` is line-for-line the same
`rank_neq(idx) / select_fw_neq(rank-1) / idx+1-next_idx`. The count test
(`T/ssrleseq_count_test.hpp:154-155`) compares the SO result against this model, so it is
**comparing two identical wrong values** and can never observe the defect. Fixing the SO
without also fixing the model would flip the suite from silently-passing to failing.

**Trigger.** Any `count_bw(idx, symbol)` where `seq[idx] != symbol`, or where a non-`symbol`
sits immediately before the trailing `symbol` run.

**Correct behavior (port).** Take `rank_neq` over `[0, idx]` (i.e. `idx+1`), find the last
non-`symbol` at or before `idx`, and return `idx − last_idx` — the maximal `symbol` run ending
at `idx` inclusive, `0` when `seq[idx] != symbol`. This is `count_fw` under stream reversal and
is checked against that symmetry law (against an independent materialized model, not the
matching composition).

---

## Bug 7 — `select_fw_*_from`: `dec_rank` unsigned-wraps when the match is in the anchor's run

**Where.** `P/packed_ssrle_seq_so.hpp`. The from-index forward selects rebase the rank, select
globally, then subtract the rebase (`:449-477`, all six ops), e.g.:

```cpp
SelectResult select_fw_ge(SeqSizeT idx, SeqSizeT rank, SymbolT symbol) const {   // :449
    SeqSizeT rank_base = rank_ge(idx, symbol);
    return select_fw_ge(rank + rank_base, symbol).dec_rank(rank_base);
}
```

`dec_rank` (`:72`) subtracts unsigned:

```cpp
SelectResult dec_rank(const SeqSizeT& rr) { rank -= rr; return *this; }   // SeqSizeT == u64
```

**Why it is a bug.** `rank_base` counts matches in `[0, idx)`, including the partial matches of
the run that straddles `idx`. When the selected match falls **inside that same run**, the prefix
rank reported by `select_fw` (the accumulated rank at the found run) is **smaller than
`rank_base`**, so `rank -= rank_base` underflows to a value near `2^64`. The returned `idx` is
correct; only the `rank` field is garbage. In the BTFL descent the shuttle consumes the `rank`
field only of **not-found** results (which are exact), so the corruption is latent — but the
`rank` of a found result is unusable.

**Trigger.** `select_fw_op_from(idx, rank, symbol, op)` where the `(rank+1)`-th match at or after
`idx` lies within the run that contains `idx`.

**Correct behavior (port).** Clamp the decrement: `rank = res.rank > rank_base ?
res.rank − rank_base : 0`. Not-found results already carry the exact tail count
(matches in `[idx, size)`), so clamping the found case to `0` is the consistent reading
("no whole matching runs were skipped past `idx`").

---

## Provenance

All seven were verified against the C++ source at the commit above and independently
reimplemented; bugs 1, 3, 4 additionally corrupt or invalidate the encoded/run representation,
bugs 5–7 return wrong query results, and bug 2 is a latent compile error. Bug 6's test oracle
shares the defect, so the existing suite does not cover it.
