# bufdrop-2026-09-11 — TARGET, written before the tree was touched

## THE TARGET, BY NAME

Not a soundness_queue row. The subject is fixed by the prompt and it is an
`unrowed_backlog.ledger` / `vg_leak_records` GROUP:

    stdlib/std/io/buffered/buffered.logos —
      BufWriter<W>   holds a raw `alloc(cap)`, declares NO `impl Drop`
      BufReader<R>   holds a raw `alloc(cap)`, declares NO `impl Drop`

The ledger names this group "9 recs / 7 fixtures, the largest remaining class
and the only one that is a single fix".

## WHY THIS BLOCK OVER THE OTHERS

The other three surviving groups in `vg_leak_records` each need a DECISION the
measurement cannot supply:

  * writ/fabric/dview stores (19 recs / 8 fixtures) — arena-shaped; "an arena
    freed at process exit is not a leak" is a CONTRACT question and nothing in
    the tree states the contract either way. No oracle.
  * `deem_incr_static_retract_e2e` (12 recs / 1 fixture) — needs a reduction
    first; one fixture, no class.
  * 10 singles over 9 fixtures — no shared property found by two rounds.

This block is the opposite shape, and it is the shape that has paid every time:
**an arm that EXISTS reached through a fact the code does not carry.** The drop
machinery is complete (18 generic `impl<..> Drop` in the stdlib compile and run
today); `close` already contains the exact free, null-guarded; the only missing
thing is the DECLARATION that the type owns its buffer. Nothing has to be built.

Two previous rounds DECLINED it as "an API decision with an owner". Under the
standing rule that verdict is wrong: the question "what should this mean?" is
answered by Rust unless a blessed divergence says otherwise, and both registries
were grepped BY CONSTRUCT this round with zero hits (see ROUND.md).

## WHAT WOULD REFUTE IT

  * a double free at a `close`-then-scope-exit site (the stdlib has one:
    `stdlib/std/io/http/server.logos:219`, `br.close()` with `br` still in scope);
  * an ABI verdict of BREAKING without a bump;
  * a corpus cost that is not zero.
