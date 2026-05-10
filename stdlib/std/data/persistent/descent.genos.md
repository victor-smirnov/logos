# `genos descent` — shuttle-driven B+tree walks

> **Status:** proto-genos (text-only spec). No interpreter yet — see `~/.claude/plans/genos-experiment.md`. Reference instantiations are hand-written in `descent.logos`; this document is what an AI session reads before extending the family or before the eventual metaprog under it lands.

A *descent* is a single root-to-leaf walk over a B+tree. The Logos persistent stack uses descents for every read-side query — size, key lookup, position lookup, range bounds, range scans. Every descent has the same skeleton: at each branch, a per-aggregate **shuttle** picks one child; an **accumulator** transforms state along the path; at the leaf, a **finalizer** produces the answer. Genos captures this skeleton; concrete descents differ only by which shuttle / accumulator / finalizer they compose.

## Parametric signature

```
genos descent<K, V, AGGR, ACC, FIN, R>
    where
        K   = container key type (Container + ContainerOrd)
        V   = container value type (Container)
        AGGR = aggregate column on BranchNode driving child selection
        ACC = accumulator: state propagated along the descent
        FIN = finalizer: leaf-level computation producing R
        R   = return type
    requires
        AGGR is one of {SUM, MAX-on-K, MAX-on-T} declared in BRANCH_OPS
        ACC is monoidal under per-child contribution
        FIN consumes (leaf, target, ACC) and returns R
```

## Invariants

1. **Memoria max-key invariant**. For any branch B and child index i, `B.keys[i] = max-key in children[i]'s subtree`. Implies `B.children` and `B.keys` have parallel cardinality.

2. **SUM aggregate maintenance**. If a `BRANCH_OPS` op has `kind=SUM` over column C, then for every branch B and child index i, `B.C[i] = sum-over-leaves(c.contribution)` for c in children[i]'s subtree. The metaprog under us emits `branchnode_<C>_shuttle` and (eventually) the maintenance hooks; mutate.logos calls them on every insert/remove/split.

3. **MAX-on-T aggregate maintenance**. Symmetrically, for `kind=MAX` columns, `B.column[i] = max-T in children[i]'s subtree`. MAX-on-K is implicit via invariant 1; explicit MAX columns over other types are user-declared.

4. **NodeARC pinning**. A descent that returns a leaf reference must clone the leaf's NodeARC into the result so the leaf survives concurrent mutations of the source container.

5. **Out-of-range protocol**. Descents that take a target outside the tree's range either:
   - clamp to nearest valid result (range queries — caller's bound check)
   - return a null-handle NodeARC + zero offset (position seeks), OR
   - return total size / `len` (bound queries).
   Each finalizer declares its OOR convention.

## Algorithm body (pseudocode)

```logos
genos pmap_descend<K, V, AGGR, ACC, FIN, R>(
    arc:    NodeARC,           // root or any subtree
    target: AGGR.target_type,  // u64 for SUM, *const K for MAX-on-K, …
    init:   ACC,               // starting accumulator value
) -> R
{
    cur   := clone_arc(arc)
    state := init

    while not is_leaf(cur):
        br := as_branch(cur)
        // Shuttle picks child + remainder via the aggregate column.
        // Concrete shuttle is per-AGGR; metaprog emits a wrapper named
        // `branchnode_<column>_shuttle` per declared op.
        (idx, target') := AGGR.shuttle(br.<AGGR.column>, target)

        // Accumulator gathers per-level contribution. For SUM/position
        // descents this sums children[0..idx]'s subtree counts; for
        // pure key-bounded walks it's a no-op.
        state := ACC.step(state, br, idx)

        target := target'
        cur    := clone_arc(br.children[idx])

    leaf := as_leaf(cur)
    return FIN.apply(leaf, target, state)
}
```

The `genos` shape lifts via partial application: each concrete descent in `descent.logos` is `pmap_descend` with specific (AGGR, ACC, FIN, R) substituted.

## Concrete instantiations (in `descent.logos`)

| Symbol | AGGR | ACC | FIN | R | Use |
|---|---|---|---|---|---|
| `pmap_size_rec` | — (no descent: branch reads SUM column directly) | — | — | u64 | container length |
| `pmap_descend_to_n` | SUM(subtree_size) | — | `(leaf, off, _) → (leaf, off)` | `(NodeARC, u64)` | position → leaf |
| `pmap_lower_bound` | MAX-on-K (via shuttle_max_k) | running pos sum | `leaf_lower_bound` | u64 | first pos with key ≥ target |
| `pmap_upper_bound` | MAX-on-K (strict) | running pos sum | `leaf_upper_bound` | u64 | first pos with key > target |

Future instantiations (currently hand-written or not yet implemented):

| Symbol | AGGR | ACC | FIN | R | Use |
|---|---|---|---|---|---|
| `pmap_iter_open_at` | SUM(subtree_size) | path stack | leaf + cursor | PMapIter | open iter at pos N |
| `pmap_iter_open_lower` | MAX-on-K | path stack + pos | leaf + cursor | PMapIter | open iter at lower_bound(K) |
| `pmap_count_le(k)` | MAX-on-K | running pos sum | `leaf_upper_bound` | u64 | count of keys ≤ k |
| `pmap_count_in(lo, hi)` | — | — | — | u64 | `count_le(hi) - count_le(lo)` (composes the above) |
| `pmap_range_sum(lo, hi, col)` | MAX-on-K + SUM(col) bound by leaf | … | … | u64 | sum of aggregate column over key range |

The pattern: **each new query reuses the same descent shape, varying only the four type-level parameters**. Once the metaprog under this genos lands, adding a new query type becomes a one-line entry in a spec table — no descent loop is rewritten.

## Hook surface (operations the genos body uses)

The body refers to operations whose semantics live outside the descent itself:

| Hook | Signature | Purpose |
|---|---|---|
| `clone_arc(node)` | `NodeARC → NodeARC` | rc++ on the underlying block |
| `is_leaf(node)` | `NodeARC → bool` | header.is_leaf flag |
| `as_branch(node)` | `NodeARC → *mut BranchNode` | typed cast (caller knows non-leaf) |
| `as_leaf(node)` | `NodeARC → *mut LeafNode` | typed cast (caller knows leaf) |
| `AGGR.shuttle(column, target)` | `(*const Column, Target) → (idx, target')` | per-aggregate child selector |
| `AGGR.column` | implicit | which BranchNode field this descent reads |
| `ACC.step(state, branch, chosen_idx)` | `(ACC, Branch, idx) → ACC` | per-level accumulator update |
| `FIN.apply(leaf, target, state)` | `(*mut LeafNode, Target, ACC) → R` | leaf-level finalize |

Concrete instantiations supply each hook; the descent body is invariant under their choice.

## Test vectors

A conformance harness runs each instantiation against canonical inputs. Same inputs against the (future) genos interpreter must produce the same outputs.

```yaml
- name: empty_tree
  setup: PMap<u64,u64>, no inserts
  cases:
    - pmap_size_rec()           = 0
    - pmap_descend_to_n(0)      = (null_arc, 0)
    - pmap_lower_bound(7)       = 0
    - pmap_upper_bound(7)       = 0

- name: single_leaf_3_keys
  setup: insert (1,10), (3,30), (5,50)
  cases:
    - pmap_size_rec()           = 3
    - pmap_descend_to_n(0)      = (leaf, 0)   # key=1
    - pmap_descend_to_n(2)      = (leaf, 2)   # key=5
    - pmap_descend_to_n(3)      = null
    - pmap_lower_bound(0)       = 0
    - pmap_lower_bound(3)       = 1
    - pmap_lower_bound(4)       = 2
    - pmap_lower_bound(6)       = 3
    - pmap_upper_bound(3)       = 2
    - pmap_upper_bound(5)       = 3

- name: multi_level_fanout4
  setup: insert keys 0..29 in shuffled order, fanout=4
  cases:
    - pmap_size_rec()           = 30
    - pmap_descend_to_n(7).leaf.keys[idx] = 7
    - pmap_descend_to_n(29).leaf.keys[idx] = 29
    - pmap_descend_to_n(30)     = null
    - pmap_lower_bound(15)      = 15
    - pmap_upper_bound(15)      = 16
```

## Reference instantiations

- `pmap_descend_to_n` — `stdlib/std/data/persistent/descent.logos:197`
- `pmap_lower_bound`  — `stdlib/std/data/persistent/descent.logos:262`
- `pmap_upper_bound`  — `stdlib/std/data/persistent/descent.logos:301`
- `pmap_size_rec`     — `stdlib/std/data/persistent/descent.logos:341`

Test files: `tests/logos/pass/persistent_get_at.logos`, `persistent_bounds.logos`, `persistent_iter*.logos`.

## How an AI session uses this

When extending the family (add `pmap_iter_range_k`, say):

1. Read this genos to understand the shape — the descent loop is fixed; you choose AGGR, ACC, FIN, R.
2. Choose: AGGR = MAX-on-K (key-driven descent); ACC = path stack (recording for the iter); FIN = construct PMapIter at leaf + cursor.
3. Reference instantiations show how each hook is implemented for similar parameter combinations.
4. Add to test vectors first. New cases extend the existing yaml — they're the conformance contract for any concrete implementation.
5. Implement the concrete fn following the genos skeleton; verify against test vectors.

The order matters: spec → test vectors → implementation. The spec is the canonical statement; the implementation must conform.

When the deterministic metaprog under this genos lands (step 4 of `persistent-roadmap-v2.md`), step 5 becomes "add a row to the metaprog's spec table" instead of writing the function body — but the genos itself is unchanged.

## Calibration notes

(For the bounded-experiment retrospective in `~/.claude/plans/genos-experiment.md`.)

- **What feels right:** the AGGR/ACC/FIN/R parameter split. Once it clicks, every concrete descent fits cleanly. The cardinality argument is real — one descent shape, many instantiations.
- **What feels uncertain:** notation for accumulator vs target. `target := target'` looks Pythonic; might prefer `next_target` naming or fold into a single accumulator-with-target tuple. Probably resolves once an interpreter forces a commitment.
- **Hook surface:** seven operations covers everything we built in step 1. Shouldn't grow much — most extensions are new (AGGR, FIN) combinations, not new primitive hooks.
- **What an AI session would still need beyond this doc:** concrete LeafNode/BranchNode struct shapes (= cow.logos's BRANCH_OPS), the cmp_view_key / Container trait surface, and the Logos-side rc protocol. All standard codebase knowledge — not genos-specific.
- **Format observation:** YAML for test vectors works; Logos pseudocode for body works. Markdown narrative for invariants reads well. No friction yet.
