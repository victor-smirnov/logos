# Probe measurements

⚠ **This file exists so that recording a measurement does not cost a rebuild.**
`scripts/probe-batch.sh` builds *before* it prices, so a round that writes its
numbers into a `.cpp` comment must rebuild to commit them — measured on
2026-08-29, that is one extra ~150 s build plus a re-run of L1, the ledger and
`-L bc` under a new build identity, for no change in behaviour. This file is not
compiled. It sits beside the sources and is versioned with them.

⚠ **The link to the code is a SYMBOL, never a line number.** A line number in a
record about another file goes stale silently and a stale number is read as
current. `scripts/probe-log-lint.py` checks every `site:` here against the named
file and reds if the symbol is gone — so a rename breaks the record loudly
instead of leaving it pointing at nothing.

⚠ **A record here is a MEASUREMENT, not a verdict about the tree.** Each carries
the build it was taken under, because a ceiling decays as the ledger shrinks and
a cost grows as the corpus widens: both numbers are about a population that
moves. Re-price before funding anything measured more than a round ago.

## Format

    ## <probe-name>
    site: <path>::<symbol>
    build: <build-hash>            what `scripts/build_hash.py` said
    measured: <YYYY-MM-DD>
    fires: <n>                     arrivals; 0 means NEVER FIRED, which is not a zero
    ceiling: <n>                   ledger rows a crude edit closes — an UPPER bound
    cost: <n>                      legal programs it refuses — a LOWER bound
    verdict: <one line>
    note: <free text, any length>

---

## selftest_refuse
site: src/compiler/borrow_check.cpp::record_borrow
build: 3aeaa1737dd22dd3
measured: 2026-08-29
fires: 364946
ceiling: 365
cost: 1033
verdict: the harness's known answer — it must close everything
note: refuses every borrow the pass records. A reader that has never SEEN a row
  close cannot tell a dead hypothesis from a broken reader, and on its first run
  this one WAS broken. `ceiling-probe.sh --selftest` asserts it closes the whole
  ledger; if that number is ever small, the READER is what broke, not the tree.

## selftest_inert
site: src/compiler/borrow_check.cpp::record_borrow
build: 3aeaa1737dd22dd3
measured: 2026-08-28
fires: 1599734
ceiling: 0
cost: 0
verdict: the null pole — it must change nothing
note: proves the reader does not invent changes. Fires on every recorded borrow
  and moves no verdict in either direction.

## recvresvamut
site: src/compiler/borrow_check.cpp::check_recv_conflict
build: 3aeaa1737dd22dd3
measured: 2026-08-29
fires: —
ceiling: 5
cost: 11
verdict: STOP — and the decline was for the WRONG REASON
note: recorded as a stop sign for days. Compiling each cost fixture by hand
  showed all nine are ONE diagnostic — "cannot borrow 'X' as mutable: not
  declared as mut" — i.e. `take_borrow`'s binding-mut check, not the reservation.
  All nine are rc 0 under the narrow `recvamutraw`, which landed at 3/0. A
  mechanism buried as too expensive was buried for a reason that was not its own.

## rootkeep
site: src/compiler/borrow_check.cpp::extract_borrow_place
build: (pre-store; measured against the 447-row ledger)
measured: 2026-08-27
fires: 427
ceiling: 0
cost: 0
verdict: REFUTED at the IndexRead site; NOT MEASURED at the SliceIndex twin
note: ⚠ ONE NAME GUARDED TWO BAILS. The coverage map counts 21,299 arrivals at
  the IndexRead spelling and ZERO at the SliceIndex one, so the 427 fires
  measured the first and said nothing about the second. The fire counter cannot
  catch this by construction — it aggregates by NAME, so two sites are one
  number. Only a per-region count separates them.

## genrecvtie
site: src/compiler/borrow_check.cpp::take_ref_borrows
build: (pre-store)
measured: 2026-08-27
fires: 1
ceiling: 0
cost: 0
verdict: UNPRICEABLE by the ledger — not refuted
note: fired ONCE in 423 ledger compiles and 14,075 times over the pass corpus,
  where it still changed nothing. An insurance probe on the same lookup without
  the pruning guard fired 176,555, which proved the site hot and named the
  pruner. The defect's population is the stdlib and pass corpus, not the ledger:
  proven-live is necessary and not sufficient.
