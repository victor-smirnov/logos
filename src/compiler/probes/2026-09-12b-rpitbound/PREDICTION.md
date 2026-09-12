# PREDICTION — round 2026-09-12b, written BEFORE any compiler source was edited

base build: `0e2b64ecc2cb7064 43`, `build/bin/logosc` Sep 12 07:46, tree clean at `c4acfe2fe`.
baseline `gate-run.sh -L bc`: rc 0, build 1063, 2784 tests already measured, 6720 recorded / 0 failed.
baseline `soundness_queue_gate.sh` (LOGOS_LIB_DIR supplied): rc 0, 85 rows.

## THE CLASS, BY PROPERTY

Not "the Fn-family kind is unchecked". The property is:
**a declared `impl Trait` RETURN bound is adopted without ever being checked against the
hidden concrete type.** `sema_decl.cpp:1737` replaces the `ImplTrait` return type with
`impl_ret_type_inferred_` and checks NOTHING; `sema.cpp:8424` (the RETURN-position
`IMPL_TYPE` branch of `resolve_type`) stores only `struct_name = trait name` and never calls
`read_trait_bound_args`. So the declared obligation is dropped at BOTH ends.

Enumerated members of the class, by that property and not by spelling:
* the Fn-family KIND obligation (`impl Fn` given a mutating / consuming closure) — the
  ledger row `bck.NEW-CMUT`;
* the ordinary trait obligation (`impl Speak` given a type that does not implement `Speak`);
* every other trait obligation written in a return position.
ONE site decides all of them. This is one structural change, not three.

## PREDICTED CLOSED SET — a number and a list

**bc_admits: exactly 1 row.**
  * `borrow-immutable-upvar-mutation-impl-trait` (`bck.NEW-CMUT`)
Predicted NOT to close, and why: `impl-trait-captures` (`-> impl Foo<'a>`, W does implement
Foo) and `issue-95079-missing-move-in-nested-closure` (`-> impl Iterator<Item = ()>`) are
lifetime/escape roots whose hidden type satisfies the bound.

**soundness_queue: 0 rows closed.** `impl_fn_return_stack_env_dangles` is the only queue row
with an RPIT and all three of its closures are READ-ONLY (kind 0) — the bound is satisfied,
its `run 1` defect is untouched.

## MY OWN COUNTER-EXAMPLES (shapes the pricing round did not use)

ILLEGAL — all four compile rc 0 today, all four must be REFUSED after:
  c1 `-> impl Fn(i64) -> i64`, mutating capture, non-empty signature
  c2 `-> impl Fn`, capture MOVED OUT, `impl Drop` present so A16 auto-Copy cannot hide it
  c3 `-> impl FnMut` (req 1) given a consuming closure (kind 2)
  c5 RPIT in a `impl S { fn maker(&self) }` METHOD position, mutating capture

LEGAL — all compile and run today; each must still compile AND RUN THE SAME after:
  L1 `-> impl Fn`, read-only capture            rc 14
  L2 `-> impl FnMut`, mutating, called twice    rc 9
  L3 `-> impl FnOnce`, consuming, `impl Drop`   rc 9
  L5 `-> impl Fn`, captures NOTHING             rc 4
  L6 `fn pass_through<T: Speak>(t: T) -> impl Speak` — hidden type is a TYPE PARAM, the
     check MUST defer (this is the over-refusal the arm is most likely to commit)  rc 6
  L7 `-> impl Speak` returning a real implementor  rc 8

## COST COLUMN THE PRICING ROUND DID NOT PRICE

The pricing round priced `-> impl Fn` only (11 arrivals). The CLASS fix touches every
`-> impl X` in the tree: **20 files**, carrying `impl Fn`, `Iterator<Item=()>`, `Foo<'a>`,
`Animal`, `Trait`, `Greeter`, `Doubler`, `Display`, `Valueable`, `Compute`, `Shape`, `Tag`,
`Tr`, `T`. Two of them are `fail` fixtures whose pinned text a new diagnostic would move.
That is the population that has to come back clean.
