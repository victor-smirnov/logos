# 2026-09-14l-storeedge — TARGETS, written before any compiler edit

## Target rows, by name
- bc_admits `buffer-reuse-pattern-issue-147694` (root `bck.NEW-L`)
- bc_admits `two-phase-across-loop` (root `bck.NEW-L`), tested as the second member of the root id; predicted NOT to move (below)

## Why this block
- `bck.NEW-L` is the candidate the 14j pricing named ("the push deposit never reaches 12i's loop back-edge arm").
  Re-measured on base 0230e503bd682184 before choosing; the handed-down mechanism is REFUTED and replaced:
  the back-edge arm is not the missing door for the ledger row.
- Survey: `bck.NEW-L` has no `site:`-bearing record that priced it. Three definitions: (1) a `## name` record whose
  verdict line names one of its programs — none (only `bck.B` has one of 37 roots); (2) 14j's window definition —
  zero windows; (3) the survey mentions it has (14e-thruref list, fnptrbinder2 note) are list lines, not prices.
- Shape: an arm that exists (§B6 dangling deposit at pop_scope, E0597 at the holder's later use; the loan channel's
  holder re-home) reached through a fact the code does not carry.

## The fact, measured on base 0230e503bd682184 (LOGOS_DUMP_BC_RELEASE)
- `buffer.push(&x)` runs apply_flow_outparams' A2 "prospective half": `reborrow_of_.add(buffer, x)` — an ALIAS edge.
- Every later destination re-home (`rehome_reborrow`, `resolve_place_reborrow` via place_write_root) chases
  `endpoint(buffer)` = `x`: a later `buffer.push(&d)` deposits its §B6 source and its loan on `x`, not on `buffer`.
- In a loop, pass 1's push mints `buffer -> data`; `reborrow_of_` is not restored for pass 2, so pass 2's push
  re-homes to `data` itself: loan `target=data holder=data` (pass 1: `holder=buffer`), and store_ref_sources skips
  a binding borrowing itself — nothing is deposited, nothing dangles.
- Loop-free witnesses of the same fact: `buffer.push(&x); { let d; buffer.push(&d); } buffer.len()` ADMITTED;
  its legal twin `... { let d; buffer.push(&d); } let y = x;` REFUSED "'data' ... borrowed by 'x'".
- The edge says "buffer IS x" where the program says "buffer HOLDS a reference to x" — a STORE as a container
  element, which the door-8b arm already distinguishes (`stored_ref_elem`: arg type == an element type arg).

## Not this block
- `two-phase-across-loop`: base REFUSES the port with upstream's later use restored (`return strings.len()` after
  the loop — upstream has `println!("{:?}", strings)`); the port as written has none, and base admits it. The
  holder's uses in the body sit AT/BELOW the raise, so 12i's `lu < raise_point` window never keeps the loan across
  the back edge. A door-1 question, not the store edge. Predicted unmoved by every arm here.
- `L07`-shape (loop push, no use after the loop) and the assign twin `c18` (`h = &data` in a loop, `*h` at the top,
  no use after) are ADMITTED on base: pass 2 restores `dangling_` and nothing carries iteration 1's dangle to
  iteration 2's use. Doors in series with this block; predicted unmoved.
