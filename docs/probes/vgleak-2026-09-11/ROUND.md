# vgleak-2026-09-11 — THE 09-08 LEAK DATA WAS DEAD; 30 OF 90 FIXTURES ARE CLEAN

build read: `19ff93338332ba46 43` (`python3 scripts/build_hash.py`).
queue gate before: rc 0, 80 rows. after: rc 0, 81 rows.

## THE RE-SWEEP (the whole round's premise)

Same 93 fixtures, same per-fixture CMake **ENVIRONMENT** — which is where the
"per-fixture flags" live; they are not argv. `LOGOS_VERIFY_LAYOUT=1` and
`LOGOS_MLIRGEN_BUG_LEDGER` are set by `set_tests_properties(... ENVIRONMENT ...)`,
extracted from every `CTestTestfile.cmake` (6665 pass fixtures mapped, 93 needed,
0 missing). `LOGOS_FACTS_DIR` dropped so nothing writes into the build tree.

|                  | 09-08 | 09-11 |
|------------------|-------|-------|
| LEAK             | 87    | 57    |
| OK               | 3     | 34    |
| CORRUPT          | 3     | 2     |
| loss records     | 270   | 137   |
| immediate sites  | 39    | 36    |
| (site, caller)   | 75    | 42    |
| full stacks      | 110   | 53    |

Diffed BOTH ways: 30 fixtures LEAK -> OK, **0 the other way**, 1 CORRUPT -> OK.

`fires:` valgrind `--leak-check=full --show-leak-kinds=definite,indirect`,
93 fixtures, `xargs -P24`; survivors checked (`pgrep valgrind` empty).
Data: `/home/logos/sandbox/vg-round3/{leaks.tsv,recs3.pkl,vg/}`.

## THE QUESTION THE PROMPT SAID MUST NOT BE CLAIMED IS NOW MEASURED

Of the 32 fixtures in the `vec$vec_new`-from-`mk` class, **30 are clean**: all
11 `cond_move_*`, all 7 `dupown_*`, all 11 `rawdup_*`, and
`no_auto_drop_sibling_ctl`. Three remain and two of them are not that shape at
all (`deem_incr_static_retract_e2e`, `ptr_drop_in_place_recurses`); the third,
`no_auto_drop_sibling`, is a deliberate suppression.

## ATTRIBUTION — 72 OF 137 RECORDS ARE NOT DEFECTS

Row unit = the site PLUS the reason it is never freed, per the entry's own rule.

| recs | fixtures | class | reason | verdict |
|-----|-----|-------|--------|---------|
| 26 | 18 | bare `malloc` in the fixture's `unsafe fn main()` | raw-pointer/zone/layout probes; one is literally `let _spacer` "intervening alloc (clobber)" | neither — test code |
| 29 | 5 | fixture-local `unsafe fn` allocators (`zeroed` x2, `cstr` x2, `new_node`) | `fs_meta` says "Leak the buffer for the lifetime of this call (test-only convenience)" | neither — test code |
| 13 | 1 | `no_auto_drop_sibling` | `#[no_auto_drop]`, and 13 is EXACTLY its 13 suppressed values; `_ctl` is clean | neither — by design |
| 3 | 2 | `relany$arena_new`, `zvec$arena_new` (+1 indirect) | 4096 b arenas freed at process exit | neither — arena |
| 1 | 1 | `box_into_raw` | `box_leak` — "never freed", by design | neither — by design |

## ATTRIBUTED TO ROWS THAT ALREADY EXIST — 14 records, no new row owed

* **`operator_autoref_temp_never_dropped`** (tier 1) — 11 recs / 3 fixtures.
  Re-verified by hand on today's binary: `s == String::from("hej")` loses 8 b;
  the by-value argument `take(String::from("hej"))` and the method receiver
  `String::from("hej").len()` are both CLEAN. `wql_el_cmp_measured::m_string`
  compares six pairs of `k_string(..)` temporaries with `==` (8 recs);
  `string_eq_empty` one; `field-replace-in-struct-with-drop-b154` two — a
  fixture the row's own header already names.
* **`weak_local_never_dropped`** (tier 1) — 3 recs / 3 fixtures, all three
  already named in the row. One-variable control re-run today: `Rc` alone
  CLEAN, `r.clone()` CLEAN, `r.downgrade()` **12 b lost**.

## ROWED THIS ROUND — `boxdyn_arg_deref_borrow_kills_box_drop`, tier 1, `run 1`

Passing `&b` for a `&dyn Tr` parameter, where `b` is a `Box<dyn Tr>` local,
deletes that local's scope-exit drop entirely.

ONE-VARIABLE CONTROL, same binary, one line changed:

| program | bytes lost | destructor count | rc |
|---------|-----------|------------------|----|
| `use_ref(&b)` | 16 | 0 | 1 |
| `b.v()` | 0 | 1 | 0 |

Further controls, all clean, bounding where a fix must not reach: no borrow at
all; return-position unsize (`let b: Box<dyn Sp> = make()`, the other half of
`coerce_box_dyn`); and — the sharp one — **`let r: &dyn Sp = &b;` is CLEAN**, so
the borrow is not the discriminator, the ARGUMENT position is. `Box<A>` without
`dyn` cannot express the shape at all (`&Box<A>` does not coerce to `&A`), so
the `dyn` half is not incidental.

Oracle is a destructor count through a raw `*mut i64`, because a leak reads as a
clean exit in every other column in this tree.

## A NEGATIVE CONTROL THAT MUST NOT BE RE-PROPOSED

**"A heap owner in `main` is never dropped" is REFUTED.** Eight hand programs —
`String`/`Box`/`Vec` local in a plain `main`, in an `unsafe fn main()`, in a
callee, in an inner block, moved into a struct, returned from a factory — are
ALL clean. The 26 `main`-allocated records are bare `malloc`, not stdlib owners.

## WHAT DESERVES FUNDING

1. **`BufReader`/`BufWriter` have no `impl Drop`** — 9 recs / 7 fixtures, the
   largest remaining class and the only one that is a single fix. Both hold a
   raw `alloc(cap)`; `stdlib/std/io/buffered/buffered.logos` declares no `Drop`
   for either and the doc comment says "Call `close` to free the buffer when
   done". Rust's `BufReader` frees on drop, so the standing Rust rule says the
   stdlib is wrong — but adding `Drop` to two public types is an API decision
   with an owner. **Report, do not edit.**
2. **`boxdyn_arg_deref_borrow_kills_box_drop`** — now rowed, a one-line control,
   and the arm that is right next door is the `let`-position coercion.
3. **The writ/fabric/dview stores**, 19 recs / 8 fixtures — arena-shaped, and
   an arena freed at process exit is not a leak. Cheapest close in the list if
   the contract says so; nothing says so yet.

## CORRECTIONS TO THE PROMPT

* `replace_site_skips_field_drop_glue` is **not** an open row — closed by
  `1979d72f4`. The prompt and the backlog entry both assert it is open.
* The 09-08 numbers (87/270/39/75/110) are all stale; every one moved.
* "the per-fixture CMake flags" are ENVIRONMENT properties, not argv.
* The STEP-1 gate command **does** carry `LOGOS_LIB_DIR` and works (rc 0);
  the correction four rounds recorded is fixed and stays fixed.
