# Feature-interaction audit (2026-09-26)

Pairwise and triple probes of 29 Rust features against logosc, rustc 1.98.1 as oracle. Raw findings clustered by suspected mechanism; one to two representatives per cluster re-run, minimized and checked against the queue. Companion to `docs/audit/2026-09-26-sema-path-inventory.md` and ADR 0030 (`docs/adr/0030-hir-core-layer.md`).

Method: 852 raw findings from 16 probe agents were CLUSTERED by suspected mechanism; only 1-2 representatives per cluster were independently re-run, minimized and deduplicated against the queue; members inherit the representatives' verdict ("mixed" = representatives disagree, needs manual triage).

## 1. Summary

| item | count |
|---|---|
| feature pairs probed (14 units x 29, every unordered pair once) | 406 |
| triples probed (triples-own 16, triples-types 16) | 32 |
| programs written (pairs / triples / total, as reported by probe agents) | 2545 / 236 / 2781 |
| pair verdicts: finding / ok / known / unsupported | 373 / 24 / 5 / 4 |
| raw findings | 852 |
| clusters (all raw findings assigned; sizes sum to 852) | 250 |
| clusters confirmed / mixed / refuted / blessed | 250 / 0 / 0 / 0 |
| confirmed clusters duplicating an existing row | 3 (25 findings) |
| confirmed NEW clusters (incl. 1 partial overlap) | 247 (827 findings) |
| of which likely same mechanism as another NEW cluster (merge candidates, §3.2) | 10 |

NEW clusters by severity (1 = silent wrong output / UB / accepts unsound, 2 = crash / ICE / leak, 3 = rejects common valid code, 4 = cosmetic or rare):

| severity | clusters | findings |
|---|---|---|
| 1 | 81 | 288 |
| 2 | 48 | 144 |
| 3 | 115 | 388 |
| 4 | 3 | 7 |

NEW clusters by kind:

| kind | meaning | clusters | findings |
|---|---|---|---|
| MC | miscompile (silent wrong output / UB / runtime crash) | 68 | 236 |
| LK | leak or skipped destructor | 10 | 36 |
| AI | accepts invalid | 27 | 86 |
| CC | compile crash / ICE / hang | 33 | 95 |
| RV | rejects valid | 109 | 374 |

Severity x kind (NEW clusters):

| sev | MC | LK | AI | CC | RV |
|---|---|---|---|---|---|
| 1 | 62 | 3 | 16 | 0 | 0 |
| 2 | 5 | 7 | 3 | 32 | 1 |
| 3 | 1 | 0 | 6 | 1 | 107 |
| 4 | 0 | 0 | 2 | 0 | 1 |

## 2. Interaction matrix

One char per unordered pair (symmetric). Legend: `.` ok (SAME output, twins agree), `X` finding (all its clusters are confirmed), `M` mixed, `K` only a known queue row hit, `U` unsupported feature blocked the probe, `B` blessed divergence, blank not probed, `\` diagonal. No pair is M, B or blank.

```
       1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24 25 26 27 28 29
F1     \  X  X  X  X  X  X  X  X  X  X  X  X  X  X  X  X  X  X  X  X  X  X  X  X  X  X  X  X
F2     X  \  X  X  .  X  X  X  X  X  X  X  X  X  .  X  X  X  X  X  X  X  X  X  X  X  .  X  X
F3     X  X  \  X  X  X  X  X  X  X  .  X  X  X  X  X  X  X  X  X  X  X  X  X  X  X  X  X  X
F4     X  X  X  \  X  X  X  X  X  X  X  X  X  X  X  X  X  X  X  X  X  X  X  X  X  X  X  X  X
F5     X  .  X  X  \  X  X  X  X  X  X  X  X  X  X  X  X  X  X  X  X  X  X  U  X  X  X  X  X
F6     X  X  X  X  X  \  X  X  X  X  .  X  X  X  X  X  X  K  X  X  X  X  K  K  X  X  X  X  X
F7     X  X  X  X  X  X  \  X  X  X  X  X  .  X  X  X  X  X  X  X  X  X  K  X  X  X  X  X  X
F8     X  X  X  X  X  X  X  \  X  X  X  X  .  X  X  X  X  X  X  X  X  X  X  X  X  X  .  X  X
F9     X  X  X  X  X  X  X  X  \  X  X  X  X  X  X  X  X  .  X  X  X  X  X  .  X  X  .  X  X
F10    X  X  X  X  X  X  X  X  X  \  X  X  X  X  X  X  X  X  .  X  X  X  X  X  X  X  X  X  X
F11    X  X  .  X  X  .  X  X  X  X  \  X  .  .  X  X  X  X  .  X  X  .  U  .  X  X  X  X  X
F12    X  X  X  X  X  X  X  X  X  X  X  \  X  X  X  X  X  X  X  X  X  X  X  X  X  X  X  X  X
F13    X  X  X  X  X  X  .  .  X  X  .  X  \  X  X  X  X  X  X  X  X  X  X  X  .  X  .  X  X
F14    X  X  X  X  X  X  X  X  X  X  .  X  X  \  X  X  X  X  X  X  X  X  X  X  X  X  .  X  X
F15    X  .  X  X  X  X  X  X  X  X  X  X  X  X  \  X  X  X  X  X  X  X  X  X  X  X  X  X  X
F16    X  X  X  X  X  X  X  X  X  X  X  X  X  X  X  \  X  X  X  X  X  X  X  X  X  X  X  X  X
F17    X  X  X  X  X  X  X  X  X  X  X  X  X  X  X  X  \  X  X  X  X  X  X  X  X  X  .  X  X
F18    X  X  X  X  X  K  X  X  .  X  X  X  X  X  X  X  X  \  X  X  X  X  X  X  X  X  .  X  X
F19    X  X  X  X  X  X  X  X  X  .  .  X  X  X  X  X  X  X  \  X  X  X  X  X  X  X  X  X  X
F20    X  X  X  X  X  X  X  X  X  X  X  X  X  X  X  X  X  X  X  \  X  X  X  X  X  X  X  K  X
F21    X  X  X  X  X  X  X  X  X  X  X  X  X  X  X  X  X  X  X  X  \  X  X  .  X  X  X  X  X
F22    X  X  X  X  X  X  X  X  X  X  .  X  X  X  X  X  X  X  X  X  X  \  X  X  X  X  .  X  X
F23    X  X  X  X  X  K  K  X  X  X  U  X  X  X  X  X  X  X  X  X  X  X  \  X  X  X  U  X  X
F24    X  X  X  X  U  K  X  X  .  X  .  X  X  X  X  X  X  X  X  X  .  X  X  \  X  X  X  X  X
F25    X  X  X  X  X  X  X  X  X  X  X  X  .  X  X  X  X  X  X  X  X  X  X  X  \  X  X  X  X
F26    X  X  X  X  X  X  X  X  X  X  X  X  X  X  X  X  X  X  X  X  X  X  X  X  X  \  X  U  X
F27    X  .  X  X  X  X  X  .  .  X  X  X  .  .  X  X  .  .  X  X  X  .  U  X  X  X  \  X  X
F28    X  X  X  X  X  X  X  X  X  X  X  X  X  X  X  X  X  X  X  K  X  X  X  X  X  U  X  \  X
F29    X  X  X  X  X  X  X  X  X  X  X  X  X  X  X  X  X  X  X  X  X  X  X  X  X  X  X  X  \
```

| key | feature | key | feature | key | feature |
|---|---|---|---|---|---|
| F1 | generics | F2 | traits | F3 | dyn Trait |
| F4 | impl Trait | F5 | assoc types/consts | F6 | closures |
| F7 | refs/lifetimes | F8 | match patterns | F9 | if-let/while-let/let-else |
| F10 | enums w/ payloads | F11 | structs/tuple structs | F12 | tuples/arrays/slices |
| F13 | ownership/moves | F14 | Drop | F15 | smart pointers |
| F16 | iterators/for | F17 | Option/Result/? | F18 | operator overloading |
| F19 | methods/autoderef | F20 | control flow as expr | F21 | ints/casts/char/bool |
| F22 | strings/format | F23 | collections | F24 | shadowing/nested fns |
| F25 | const generics/consts/statics | F26 | recursion | F27 | unsafe/raw ptrs |
| F28 | derive | F29 | type inference |  |  |

Non-X pairs: K = F7xF23 (hashmap_str_key_borrow_escapes_admitted), F6xF18 (bug_boxed_closure_runtime_garbage), F6xF23 and F6xF24 (loop_local_move_closure_shares_slot_wrong), F20xF28 (derive_default_nonpod_field_refused); U = F11xF23, F23xF27 (map API is unsafe raw-receiver), F26xF28 (Option/Box/Vec lack Clone/==), F5xF24 (const item in fn body, generic nested fn).

Triples (not in the matrix): 5 of 16 triples-own and 16 of 16 triples-types produced findings.

## 3. Confirmed NEW clusters

Ordered by severity, then size. `inv` = rule number in the path inventory §2 ranked table. ADR column = ADR 0030 migration step expected to close it; `-` means outside the migration (grammar, stdlib, metaprog derive handlers, borrow checker, or a policy decision). Kind codes as in §1. Members are `unit#n` (`tt` = triples-types, `to` = triples-own); repro paths are the representatives' minimized pairs.

### 3.1 Table

| # | cluster | sev | kind | size | ADR | inv | mechanism |
|---|---|---|---|---|---|---|---|
| 1 | `tail-return-owned-double-drop` | 1 | MC | 18 | S2 | 30,17 | Owned local/param/field named by a fn or closure tail expr is not a move: dropped at scope exit and returned (double free, UAF). |
| 2 | `closure-param-ref-to-fat-pointer` | 1 | MC | 11 | S1 | 28,31 | Closure param `&&dyn`/`&Box<dyn>`/enum-payload binder of fat ptr is one indirection short (segfault on dispatch; filter over windows reads pointer as data). |
| 3 | `derive-debug-enum-type-name` | 1 | MC | 11 | - (metaprog) | - | derive_debug has no enum case: prints the type name for every variant. |
| 4 | `const-generic-array-length-unbound-in-body` | 1 | MC | 10 | S9 | 35 | Slice view of [T; N] under const-generic mono keeps length 0: len/iter/iter_mut/range-slice see nothing. |
| 5 | `derive-partialeq-enum-always-true` | 1 | MC | 9 | - (metaprog) | - | derive_partial_eq has no enum case: zero-field struct branch emits `eq -> true`. |
| 6 | `exhaustiveness-unchecked-infinite-domains` | 1 | AI | 9 | S3 | 24 | No exhaustiveness for int/char/&str/slice/struct-with-int-field scrutinees; uncovered value segfaults or returns garbage. |
| 7 | `exhaustiveness-skipped-for-ref-scrutinee` | 1 | AI | 8 | S3 | 24 | E0004 skipped when the scrutinee is a reference; missing arm falls off the match (UB). |
| 8 | `closure-returns-aggregate-via-stack-alloca` | 1 | MC | 7 | S1 | 28 | Closure returning a tuple returns a pointer to its own stack alloca. |
| 9 | `const-param-value-unbound-at-mono` | 1 | MC | 7 | R0+S9 | - | Turbofish const arg inside format args mangled as type `integer`: W reads 0 or ICE. |
| 10 | `for-by-value-array-fat-elements` | 1 | MC | 7 | S1 | 20 | By-value for over an array of fat-pointer / 16-byte elements loads the element with the wrong layout. |
| 11 | `for-over-ref-vec-enum-stride` | 1 | MC | 7 | S1 | 20 | `for x in &v` / slice over enums >8 bytes steps with stride 8 (.iter() correct). |
| 12 | `move-closure-capture-shares-slot` | 1 | MC | 7 | S4+S10 | 40 | Move closure env stays in the creating frame / aliases the source slot: boxed/returned closures read dangling or later-mutated captures. (partial overlap with `loop_local_move_closure_shares_slot_wrong`) |
| 13 | `option-ref-unwrap-instance-no-value` | 1 | MC | 7 | S9 | - | No blanket Ord for &T: max/min where-gate skips synthesis, call resolves to a missing symbol; under `?` silently dropped (garbage read). |
| 14 | `rc-derefmut` | 1 | AI | 7 | - (stdlib) | - | stdlib `impl DerefMut for Rc<T>`: shared-aliasing &mut in safe code, UAF via Vec realloc. |
| 15 | `derive-copy-heap-field-accepted` | 1 | AI | 6 | S9 | 11 | No E0204: impl/derive Copy on heap-owning fields accepted, bitwise copy double-frees. |
| 16 | `loop-break-value-inference-wrong` | 1 | MC | 6 | S2+S7 | 47,27 | Break-value type taken from the first break; later breaks lowered at the wrong type (Some(8) -> None, i32 in i64 slot). |
| 17 | `aggregate-literal-elements-mixed-width` | 1 | MC | 5 | S7 | 2,23 | Later tuple/array elements of an array literal keep i32 literals while the first fixes i64: mixed layout, garbage reads. |
| 18 | `const-generic-args-ignored-in-type-identity` | 1 | AI | 5 | S9 | - | Type equality ignores const args of nominal types; V<1> passed as V<8> reads out of bounds. |
| 19 | `const-static-array-init-ignores-declared-type` | 1 | MC | 5 | S7 | 2 | const/static array initialisers emit unsuffixed literals as i32 regardless of declared element type. |
| 20 | `format-args-temporaries-dropped-early` | 1 | MC | 5 | R0 | - | println!/print! render-and-reparse binds each arg in a hidden let: argument temporaries drop before the print, not at end of statement. |
| 21 | `format-in-value-position-leaks` | 1 | MC | 5 | S2 | 3,30 | A block expr nested as the tail of a value block (if/match arm, let init) loses its value: format! leaks, i64 reads 0, String segfaults. |
| 22 | `nested-match-as-block-tail-value` | 1 | MC | 5 | S2 | 30 | match (or loop) as tail of a block used as a value (closure body, arm, else) lowered with no value: void, wrong arm, hang, MLIR crash. |
| 23 | `shift-lhs-literal-ignores-expected-type` | 1 | MC | 5 | S7 | 2 | Shift LHS literal typed i32 despite expected u64/i64: `1 << 40` traps, `1 << 31` silently sign-extends. |
| 24 | `slice-pattern-element-offset` | 1 | MC | 5 | S7 | 2,23 | Wrong element stride / after-`..` offsets in slice patterns; representative traced it to mixed-width array literal (see aggregate-literal-elements-mixed-width). |
| 25 | `assoc-type-equality-unchecked` | 1 | AI | 4 | S9 | 35,37 | Trait<Item = i64> not checked at call/return site (E0271 missing); bool returned as i64. |
| 26 | `fn-tail-temporaries-not-dropped` | 1 | LK | 4 | S2 | 17 | Temporaries in a fn-body tail expr (incl. RefCell guards) are never dropped, and neither are the frame's locals. |
| 27 | `literal-behind-ref-coercion-built-i32` | 1 | MC | 4 | S7+S4 | 42,43 | i32-defaulted literal/binding passed behind & where &i64 expected; &i32->&i64 accepted, 8-byte read of 4-byte slot. |
| 28 | `operator-method-on-generic-instance-not-emitted` | 1 | MC | 4 | S9 | 37,11 | impl<T: PartialEq or PartialOrd> methods never monomorphised; call dropped silently in an if condition (pass fixture core_6_10_derive_partial_eq vacuous). |
| 29 | `unit-path-pattern-binds-under-ref` | 1 | MC | 4 | S3 | - | Bare `None`/const path in a pattern under ref default binding mode resolves as a fresh catch-all binding. |
| 30 | `closure-call-aggregate-arg-abi` | 1 | MC | 3 | S1 | 28 | Indirect call (closure/fn ptr) with aggregate rvalue arg (field place, call result) passes the field value where a pointer is expected. |
| 31 | `fnonce-closure-treated-copy` | 1 | AI | 3 | S10 | 40 | Closure moving a capture out never classified FnOnce: callable twice, double free. |
| 32 | `nested-fn-codegen-miscompile` | 1 | MC | 3 | S7 | 2 | Closure/nested-fn call args get no expected type: `&[1,2,3]` built as [i32] and read as &[i64]. |
| 33 | `non-u8-as-char-accepted` | 1 | AI | 3 | S4 | - | Cast blocklist has no char arm: u32/i64 as char accepted, surrogates constructible. |
| 34 | `operator-on-enum-lowered-as-integer` | 1 | MC | 3 | S8 | 26 | Operators on enum operands lowered as builtin integer ops; user Add/Sub/Neg ignored (unit enums silently wrong). |
| 35 | `range-literal-element-type` | 1 | MC | 3 | S10 | 46 | usize/u32/isize range bounds routed to RangeI32: silent truncation above 2^31. |
| 36 | `raw-deref-unsafe-check-gaps` | 1 | AI | 3 | S8 | 7 | &*p / &mut *p lowering bypasses the E0133 raw-deref check; raw receivers autoderef. |
| 37 | `assoc-projection-move-double-free` | 1 | MC | 2 | S5+S9 | 9 | Move out of Box<M::Out> / M::Out field not marked moved: double free. |
| 38 | `assoc-projection-never-dropped` | 1 | LK | 2 | S9 | 17 | Drop glue not resolved for T::Out values after mono. |
| 39 | `bounded-generic-impl-method-vanishes` | 1 | MC | 2 | S9 | 37 | Inherent impl<T: PartialEq> bound unsatisfied for builtin types: method never instantiated, println arg prints empty. |
| 40 | `branch-arms-not-unsized` | 1 | MC | 2 | S4 | 27 | Expected dyn type not pushed into arms; branch takes first arm's concrete type: other arm's data paired with wrong vtable, or refused. |
| 41 | `break-aggregate-value-lost` | 1 | MC | 2 | S2 | 47 | loop break slot typed ptr but aggregate call result stored by value: packed bits dereferenced as a pointer. |
| 42 | `closure-param-from-generic-bound-sibling` | 1 | MC | 2 | S7 | 45,8 | Untyped closure param keeps the callee's TypeVar T although T is fixed by a sibling arg; by-value form compiles to garbage. |
| 43 | `guard-uses-nested-payload-binding-undefined` | 1 | MC | 2 | S3 | - | Guard using a binding from a tuple-struct subpattern under a variant: binding out of scope (SIGSEGV or silent fallthrough). |
| 44 | `index-assign-no-drop-old` | 1 | MC | 2 | S6 | 16 | IndexMut assign sugar skips drop-before-replace. |
| 45 | `int-widening-through-reference-accepted` | 1 | AI | 2 | S4 | 42 | Scalar widening predicate applied to shared-ref pointees: &u8 accepted as &i64 with no conversion. |
| 46 | `iter-over-array-of-arrays-element-copy` | 1 | MC | 2 | S3 | 31 | Indexing a pattern-bound `&[T;N]` from an Option payload reads the wrong address; iter_mut writes to a copy. |
| 47 | `loop-conditional-move-drop-flags` | 1 | MC | 2 | S5 | 10 | Two sequential move+exit branches in a loop: second exit drops the moved local again; conditional move of loop var leaks. |
| 48 | `mut-self-method-through-shared-generic-ref` | 1 | AI | 2 | S8 | 15 | Receiver mutability not checked for trait dispatch through &T (type param) and &dyn. |
| 49 | `none-arm-typed-as-scrutinee` | 1 | MC | 2 | S3 | - | Same mechanism as unit-path-pattern-binds-under-ref: bare None arm binds a catch-all typed &Option. |
| 50 | `out-of-range-literal-accepted` | 1 | AI | 2 | S7 | 2 | Literal range check missing on const/static initialisers and operator RHS; silent truncation. |
| 51 | `rangefrom-iterator-empty` | 1 | MC | 2 | L0+S10 | 46 | Same mechanism as open-range-for-head-parse: RangeFrom lowered to range_i32(lo, <error>). |
| 52 | `ref-binding-mode-refutable-subpattern-runtime` | 1 | MC | 2 | S7 | 3 | `&(4, 8)` at a `&(i64,i64)` call arg is built as (i32,i32) and read as i64 (stack over-read); patterns are innocent. |
| 53 | `rest-pattern-partial-move-drop` | 1 | MC | 2 | S3+S5 | - | let struct pattern binding only Copy fields marks the Drop scrutinee moved (Drop skipped); ..base then destructure double-frees. |
| 54 | `rust2024-temporary-scopes` | 1 | MC | 2 | S2 | 17 | Rust 2024 temp scopes missing: block-tail temp dropped after locals, failed if-let scrutinee lives through else; fn-body tail temp skips all drops. |
| 55 | `shared-ref-to-mut-ptr-accepted` | 1 | AI | 2 | S4 | 43 | Ref->raw coercion ignores mutability: &T -> *mut T. |
| 56 | `slice-array-as-ptr-typing` | 1 | MC | 2 | S8 | 1 | try_method_on_slice types as_ptr as *const u8 for every element type (p.add(1) steps one byte). |
| 57 | `tail-match-conditional-move-leaks` | 1 | MC | 2 | S2+S5 | 10 | Tail-position match/if that moves a param in some arms: no per-branch drop flags at tail exit (match leaks unmoved, if/else drops the moved one: double free). |
| 58 | `untyped-closure-param-from-later-call-wrong` | 1 | MC | 2 | S7 | 45 | Closure with <error> param lowered as void no-op instead of refused (related #526). |
| 59 | `user-index-operand-unchecked` | 1 | AI | 2 | S6+S8 | 25 | Index sugar desugar skips the Idx argument type check. |
| 60 | `user-name-collides-with-std-method` | 1 | MC | 2 | S8 | 36 | Private stdlib extern (pow, sqrt) visible everywhere and beats a local generic fn; std count/last beat user trait methods. |
| 61 | `vec-swap-tuple-elements` | 1 | MC | 2 | S1 | 4 | Copying a tuple into a binding aliases the source storage. |
| 62 | `break-value-type-mismatch-accepted` | 1 | AI | 1 | S2 | 47,27 | break-as-expression skips break-type unification: &str reinterpreted as i64. |
| 63 | `byvalue-method-through-box-field-double-free` | 1 | MC | 1 | S5 | 9 | Move out of a Box via a field path not marked; owner drop frees again. |
| 64 | `collect-refs-drops-loan` | 1 | AI | 1 | - (borrowck) | - | FromIterator result does not carry the Item type's loans: dangling refs admitted. |
| 65 | `compound-assign-uses-add` | 1 | MC | 1 | S6 | 12 | `+=` on generic deref / user-IndexMut place falls back to Add; E0368 admitted. |
| 66 | `const-expr-fmt-arg-uninit` | 1 | MC | 1 | S7 | 23 | i64 const materialised by-ref at i32 width: upper 4 bytes uninitialised. |
| 67 | `fnonce-generic-capture-leak` | 1 | LK | 1 | S2 | 17 | Same mechanism as fn-tail-temporaries-not-dropped (receiver temp in tail). |
| 68 | `generic-instance-key-collision` | 1 | MC | 1 | S9 | 38 | Mangling drops type args of a generic enum nested as a type arg: Vec<Option<i64>> == Vec<Option<Box<_>>>. |
| 69 | `generic-param-moved-into-ctor-double-drop` | 1 | MC | 1 | S2+S5 | 9 | Generic `let r = Box::new(t); return r` double-drops t (plus tail-return root). |
| 70 | `if-arms-unsize-miscompiled` | 1 | MC | 1 | S4 | 19,27 | if/match whose Box::new arms coerce to an annotated Box<dyn>: wrong vtable, invalid free of a stack address. |
| 71 | `literal-lhs-of-rem-typed-signed` | 1 | MC | 1 | S7 | 2 | Unsuffixed literal LHS with unsigned RHS lowers the op signed (1 % u64::MAX == 0, 3 < u64::MAX false). |
| 72 | `match-on-deref-mut-raw-param` | 1 | MC | 1 | S2 | 30 | match as tail of a nested/unsafe block in fn tail lowers to `unreachable` (no return). |
| 73 | `move-closure-returning-capture-double-drop` | 1 | MC | 1 | S10 | 40 | Closure moving its capture out: source never marked moved, dropped twice. |
| 74 | `move-out-of-box-in-match-uaf` | 1 | MC | 1 | S2+S5 | 9 | `*b` as tail value of a block/if/arm does not mark box contents moved: double free. |
| 75 | `nested-tuple-pattern-through-ref` | 1 | MC | 1 | S3 | - | Default binding mode not applied to &(..) inside a tuple-pattern field: binds the pointer. |
| 76 | `open-range-for-head-parse` | 1 | MC | 1 | L0+S10 | 46 | `a..` lowered to a bounded Range with end 0 (empty); `for x in a.. {` does not parse. |
| 77 | `rangeinclusive-u8-contains` | 1 | MC | 1 | S10+S4 | 43 | u8 range widened to RangeOfIncl<i32>; &u8 passed unchecked as &i32 (4-byte read of 1-byte slot). |
| 78 | `rawptr-write-closure-classified-fnmut` | 1 | MC | 1 | S10+S6 | - | Write through captured raw ptr `(*p).v = ..` in a closure targets the capture slot; suggested `mut` fix miscompiles. |
| 79 | `reassign-ref-box-dyn-double-free` | 1 | MC | 1 | S6+S1 | 5 | Reassigning a ref binding whose pointee holds dyn lowered as memcpy through the old referent. |
| 80 | `u16-mul-overflow-unchecked` | 1 | MC | 1 | S7 | 2 | Narrow-int op with unsuffixed literal computed at i32, checked at i32, truncated: no trap. |
| 81 | `vec-contains-bitcopy` | 1 | MC | 1 | - (stdlib) | - | vec_contains moves elements out of the raw buffer and drops the copy. |
| 82 | `fmt-instance-for-ref-payload-missing` | 2 | CC | 12 | S9 | - | Generic stdlib Debug/Display impl not instantiated for T = &U (Option<&T>, Vec<&T>, &Box<String>). |
| 83 | `const-generic-struct-literal-error-type` | 2 | CC | 10 | S7 | 3,8 | Const arg of an unannotated struct literal not inferred from expected type: `Struct$G1$<error>` reaches mlir_gen. |
| 84 | `into-iter-adapter-leaks-source` | 2 | LK | 9 | S9 | 17 | Drop glue of a generic struct bound to a bare type param runs nothing: adapters over into_iter leak the buffer. |
| 85 | `box-dyn-drop-glue-missing` | 2 | LK | 7 | S4+S9 | 38 | Unsize at a generic-param arg emits a vtable without drop glue; vtable deduped per (type,trait) poisons other sites. |
| 86 | `generic-static-trait-call-not-emitted` | 2 | CC | 7 | S9 | 14 | Trait static fn / assoc const / identity From via generic param or literal never instantiated: func.call to missing symbol. |
| 87 | `literal-subpattern-under-ref-no-deref` | 2 | CC | 7 | S3 | 32 | Literal/range/`@`-or subpattern under ref default binding compared without the implicit deref (MLIR type mismatch). |
| 88 | `closure-byvalue-param-not-dropped` | 2 | LK | 6 | S5 | 17 | Bare-expression closure body skips the drop of by-value params (map/unwrap_or_else closures leak). |
| 89 | `slice-pattern-subpattern-bindings-lost` | 2 | CC | 6 | S3 | 34 | Bindings nested in element subpatterns of a slice pattern (under ref mode, and in let-else) are never bound. |
| 90 | `borrowed-closure-literal-arg` | 2 | MC | 5 | S10 | 40 | `&<closure literal>` rvalue borrow adds an indirection level: call loads the fn pointer from the wrong slot (jump into stack). |
| 91 | `box-dyn-struct-field-unsize` | 2 | MC | 5 | S4 | 6,19 | Struct-literal field is not a coercion site: Box::new(C) into a Box<dyn> field keeps a thin pointer; dispatch through uninitialised vtable. |
| 92 | `collect-vec-underscore-ice` | 2 | CC | 5 | S7 | - | Turbofish `_` never unified with Item; InferredType reaches mono/mlir_gen. |
| 93 | `empty-macro-args-metacall` | 2 | CC | 5 | R0 | - | panic!() with empty repetition splice hits zero-capacity map in metacall. |
| 94 | `generic-typevar-leaks-to-codegen` | 2 | CC | 5 | S7 | 8 | Closure result depending on untyped param leaves U bound to the callee's T; `Option__T` reaches mlir_gen. |
| 95 | `adapter-instance-no-value` | 2 | CC | 3 | S9 | - | Adapter over Box<dyn Iterator>/Copied/Bytes: consumer instance mangled with unresolved params, no value. |
| 96 | `box-dyn-represented-as-ref-dyn` | 2 | CC | 3 | S9 | 38 | Box<dyn> lowered as &dyn: impl for Box<dyn> treated as impl<T> Box<T>; LLVM layout abort. |
| 97 | `derive-partialeq-tuple-struct-codegen` | 2 | CC | 3 | - (metaprog) | - | Derive handlers read empty field names on tuple structs; == lowers to an i32 condition. |
| 98 | `temp-receiver-or-place-base-not-dropped` | 2 | LK | 3 | S5 | 17 | Rvalue used as method receiver / place base via autoderef (Box temp, T::mk(), const item) never gets a drop scheduled. |
| 99 | `closure-unsafe-block-body-return` | 2 | CC | 2 | S2 | - | Closure whose body is a bare `unsafe { ..; }` ending in an assignment is treated as value-returning (llvm.return operand). |
| 100 | `collect-into-vec-underscore-refused` | 2 | RV | 2 | S7 | - | Nested `_` in let annotation never solves collect's C. |
| 101 | `disjoint-closure-capture-missing` | 2 | MC | 2 | S10 | - | RFC 2229 narrowing skipped when the root implements Drop. |
| 102 | `dyn-eq-operator-builtin` | 2 | AI | 2 | S8 | 26 | == on dyn operands skips impl check and compares stack-slot addresses. |
| 103 | `generic-body-checked-per-instantiation` | 2 | AI | 2 | S9 | 37 | Operators on unbounded T and moves of projection values checked only per instantiation. |
| 104 | `generic-struct-update-infers-nothing` | 2 | CC | 2 | S7 | 8 | FRU base type not unified with the literal's generic args: R$G1$<error>. |
| 105 | `infinite-size-recursive-type-crash` | 2 | CC | 2 | - | - | Infinite-size check misses self-containment via generic enum payload; is_move_type recurses unbounded. |
| 106 | `move-out-of-box-leaks-allocation` | 2 | LK | 2 | S2+S5 | - | Tail-position match arm moving `*n` never frees the box shell. |
| 107 | `nested-fn-item-in-block-expr` | 2 | CC | 2 | S2 | - | Nested fn item in a value-position block is lowered via the closure path and drops its tail value (llvm.return operand). |
| 108 | `no-vtable-box-box-dyn` | 2 | CC | 2 | S8+S4 | 1 | Autoderef through two Box layers to dyn asks for a &dyn->&dyn vtable. |
| 109 | `parse-target-unresolved-ice` | 2 | CC | 2 | S7 | - | str::parse F from expected type leaks `<error>` to mono; main demoted to trap stub. |
| 110 | `untyped-closure-param-codegen-crash` | 2 | CC | 2 | S7 | 45 | Mixed typed/untyped closure params reach mlir_gen with no type (SIGSEGV). |
| 111 | `byvalue-trait-method-via-ref-copy-generic` | 2 | CC | 1 | S8 | 1 | By-value trait method via autoderef of &T (generic): pointer passed without load. |
| 112 | `char-classification-ascii-only` | 2 | MC | 1 | - (stdlib) | - | char Unicode predicates delegate to ASCII. |
| 113 | `closure-capture-raw-pointer-undefined` | 2 | CC | 1 | S10 | - | Capture analysis misses receiver of intrinsic raw-pointer method (.add). |
| 114 | `closure-conditional-move-drop-flag-ice` | 2 | CC | 1 | S5+S10 | 10 | Drop flag for a conditionally moved closure param never declared. |
| 115 | `const-generic-array-default-method-trap` | 2 | CC | 1 | S9 | - | Default method calling Self method, instantiated for impl<const N> on [T; N]: trap stub. |
| 116 | `derive-const-generic-struct` | 2 | CC | 1 | - (metaprog)+S9 | - | Derives on const-generic structs: N treated as type param, eq not emitted, derived-Copy instance missing for statics. |
| 117 | `empty-array-literal-turbofish-slice` | 2 | CC | 1 | S7 | 3 | Empty array literal elem type not taken from turbofish-bound &[T]. |
| 118 | `generic-bst-compile-hang` | 2 | CC | 1 | - (borrowck perf) | - | BIR struct_tp_invariant unmemoized over recursive generic struct (exponential). |
| 119 | `generic-enum-method-duplicate-mangling` | 2 | CC | 1 | S9 | 38 | Recursive generic method via match-bound Box instantiated under an unrelated live instantiation's name. |
| 120 | `generic-struct-byvalue-never-dropped` | 2 | LK | 1 | S9 | 17 | Same mechanism as into-iter-adapter-leaks-source (field glue skipped for T = generic struct). |
| 121 | `integer-placeholder-in-tuple-to-codegen` | 2 | CC | 1 | S7 | 23 | Literal defaulting skipped for tuple elements typed through a value join. |
| 122 | `match-arm-block-semicolon-ignored` | 2 | AI | 1 | S2 | - | Block expr ending in `expr;` typed and valued as expr. |
| 123 | `metacall-print-box-types` | 2 | CC | 1 | R0 | - | Zero-arg println!() metacall module lacks mem alloc/dealloc decls when a user type has a Box field. |
| 124 | `no-vtable-integer-literal-to-dyn` | 2 | CC | 1 | S7 | 23 | {integer} unresolved at an unsize site reaches vtable lookup. |
| 125 | `primitive-method-path-as-fn` | 2 | CC | 1 | S8 | - | Stdlib primitive method path as fn value: symbol never declared. |
| 126 | `sema-missing-trait-check-before-vtable` | 2 | CC | 1 | S4 | 6 | Unsize to dyn has no E0277 in sema; backend 'no vtable' guard fires. |
| 127 | `struct-update-tail-no-drop-base` | 2 | LK | 1 | S2 | 17 | Same mechanism as fn-tail-temporaries-not-dropped. |
| 128 | `unclosed-brace-crash` | 2 | CC | 1 | - (grammar) | - | Parser memo table indexed at EOF inside an unclosed block. |
| 129 | `vec-truncate-drop-order` | 2 | MC | 1 | - (stdlib) | - | Vec::truncate pops from the back: drops in reverse. |
| 130 | `deref-coercion-missing-at-coercion-site` | 3 | RV | 15 | S4 | 18 | Deref coercion missing at call args/assignments/if branches (&&str->&str, &Box<T>->&T, &mut Box<T>->&mut T). |
| 131 | `closure-param-from-expected-fn-signature` | 3 | RV | 13 | S7+S10 | 45 | Closure params/return not inferred from return-position impl Fn / Box<dyn Fn> / &mut F expectation. |
| 132 | `collect-sum-target-from-return-type` | 3 | RV | 12 | S2+S7 | 3 | Return-type-only generic method (sum/collect) in tail/return gets no expected type from the fn signature. |
| 133 | `generic-inference-through-deref-coercion` | 3 | RV | 12 | S7 | 8 | Generic arg inference does not try deref/unsize (&Vec<T>->&[T], &Box<E<T>>->&E<T>). |
| 134 | `vec-macro-requires-copy` | 3 | RV | 12 | R0 | 3 | vec! without a let annotation reparses as vec_from_arr<T: Copy>. |
| 135 | `assoc-type-binding-not-normalized` | 3 | RV | 11 | S9 | 35 | Assoc-type equality bound not used to normalize the projection in operator typing. |
| 136 | `amp-impl-trait-and-bounds-parse` | 3 | RV | 10 | - (grammar) | - | `&impl Trait`, `impl A + B`, `impl Trait + 'a` do not parse. |
| 137 | `range-index-only-slice-array` | 3 | RV | 10 | S6 | 25,33 | Range index accepts only slice/array receivers (no Vec/String deref); `&mut a[r]` typed &mut &[T]. |
| 138 | `for-in-over-generic-iterator-param` | 3 | RV | 9 | S10 | - | for-in lowering looks for next() on concrete structs only; ignores Iterator bound on type param / impl Trait. |
| 139 | `smart-pointer-inherent-method-shadows-pointee` | 3 | RV | 9 | S8 | 1 | Probe autorefs to raw `*mut Box<T>` receivers: Box/Rc/Ref get/get_mut shadow pointee methods. |
| 140 | `unsize-in-aggregate-generic-ctor-slot` | 3 | RV | 9 | R0+S4 | 6 | No Box<T>->Box<dyn> at vec! elements / nested generic ctor args; two spellings of Vec<&dyn B> fail identity. |
| 141 | `borrow-of-unit-path-undefined` | 3 | RV | 8 | S8 | - | Operand of & resolves a bare path as a variable only (&UnitStruct, &None). |
| 142 | `closure-ref-pattern-param-not-peeled` | 3 | RV | 8 | S3+S10 | 45 | `&x` closure-param pattern with adapter-inferred param type leaves x as &T. |
| 143 | `guard-in-loop-assign-twice` | 3 | RV | 8 | S3 | - | Guard-arm / refutable nested bindings re-initialised on loop back-edge: bogus E0384. |
| 144 | `vec-new-element-from-later-use` | 3 | RV | 8 | S7 | - | Vec::new() element hole not filled from a later call argument / param; checked as variance mismatch. |
| 145 | `partialeq-eq-both-ambiguous` | 3 | RV | 7 | - (stdlib cmp split) | - | Logos Eq declares its own eq/ne: PartialEq+Eq make eq ambiguous or duplicate ne. |
| 146 | `break-none-after-break-some-refused` | 3 | RV | 6 | S2+S7 | 27,47 | Sibling break values typed independently; no unification. |
| 147 | `deferred-let-no-type-parse` | 3 | RV | 6 | - (grammar)+S7 | - | `let x;` without type or init does not parse. |
| 148 | `nested-fn-not-in-scope` | 3 | RV | 6 | S8 | - | Block-local fn name visible only after its declaration (no recursion, no forward use); generic nested fn does not parse. |
| 149 | `rpit-not-opaque` | 3 | AI | 6 | S9 | - | RPIT resolved to the hidden type at call sites: fields, inherent methods, Copy, type identity leak. |
| 150 | `split-adapter-static-lifetime` | 3 | RV | 6 | - (borrowck) | - | Iterator default adapter mono with 'a = 'static fails 'a: 'static outlives check. |
| 151 | `const-N-not-inferred-from-array` | 3 | RV | 5 | S7 | 8 | Struct literal checks the field against the unsubstituted [T; N]. |
| 152 | `derive-debug-c-field-lexed-cstr` | 3 | RV | 5 | R0 | - | `##ident` string antiquote: sema sniffs `c` prefix of LIT_STR VALUE, field names starting with c become CStr. |
| 153 | `expected-type-not-reaching-literal-in-ctor` | 3 | RV | 5 | S7 | 3 | Expected type from turbofish/closure param/field not propagated into literals inside enum/generic ctor args. |
| 154 | `format-args-reparse-loses-deref` | 3 | RV | 5 | R0 | - | &*box_dyn inside format args loses the deref before &dyn unsize. |
| 155 | `parse-target-through-question` | 3 | RV | 5 | S10+S7 | - | Expected type does not flow back through `?` / method receiver into return-only generic inference. |
| 156 | `refmut-method-temp-behind-ref` | 3 | RV | 5 | S6 | 15 | &mut method on a field reached via user DerefMut takes the shared Deref step. |
| 157 | `structural-copy-clone-not-trait-impls` | 3 | RV | 5 | S9 | 11 | Structurally-Copy types (Option<Copy>, auto-Copy structs, ({integer},T)) are not Copy/Clone for bounds, .clone() or derives. |
| 158 | `cmp-impls-require-eq-or-missing` | 3 | RV | 4 | - (stdlib) | - | == on Vec/Option/Result/arrays and Vec::contains require Eq; Vec has no eq at all. |
| 159 | `const-item-as-const-generic-arg` | 3 | RV | 4 | S8 | - | Bare const path in a generic-arg slot resolved only as a type. |
| 160 | `double-deref-box-dyn-to-dyn-refused` | 3 | RV | 4 | R0+S4 | - | `&(*b)` (parenthesised, as println! renders it) not recognised as pointee borrow. |
| 161 | `generic-callable-param-to-adapter` | 3 | RV | 4 | S7 | 8 | Method type arg not inferred from a generic F's Fn bound (map(f), map(&f)). |
| 162 | `nested-fn-captures-outer-local` | 3 | AI | 4 | S8 | - | Nested fn desugared to a let-bound closure: reads enclosing locals (no E0434). |
| 163 | `no-implicit-reborrow-of-mut-receiver` | 3 | RV | 4 | S8 | 15 | Autoderef receiver through &mut Box<dyn>/&mut [T;N] moves the &mut binding. |
| 164 | `partialord-marker-no-lt` | 3 | RV | 4 | - (stdlib cmp split) | 26 | PartialOrd is an empty marker; derive emits nothing usable for < >. |
| 165 | `trait-static-call-self-inferred` | 3 | RV | 4 | S7+S8 | 14 | Default::default() resolves only from a let annotation. |
| 166 | `assoc-const-typed-self` | 3 | RV | 3 | S9 | - | Trait assoc-const type not Self-substituted before impl check. |
| 167 | `assoc-fn-path-as-value` | 3 | RV | 3 | - (grammar) | - | KW_NEW: `Type::new` only parses as a call head. |
| 168 | `blanket-impl-not-found-for-bound` | 3 | RV | 3 | S9 | 22 | Blanket impls (supertrait via blanket, impl<F: FnMut> for F, tuple) not found for bounds. |
| 169 | `box-dyn-as-ref-missing` | 3 | RV | 3 | S8 | 1 | Method lookup on Box<dyn> goes only to the trait; Box AsRef/Deref lack ?Sized. |
| 170 | `box-rc-display-debug-missing` | 3 | RV | 3 | - (stdlib) | - | No forwarding Display/Debug for Box/Rc/Ref. |
| 171 | `break-value-operand-treated-as-move` | 3 | RV | 3 | S2 | 47 | Break edge drops loop locals before evaluating the break operand. |
| 172 | `closure-return-region-of-param` | 3 | RV | 3 | - (borrowck) | - | Closure return region not tied to param region through generic Fn bound. |
| 173 | `core-impls-missing-char-sum` | 3 | RV | 3 | - (stdlib) | - | char not Hash/Ord; Sum/Product only i32/i64. |
| 174 | `derived-impls-invisible-in-impl-blocks` | 3 | RV | 3 | - (metaprog order) | - | Impl-method bodies are checked before derive-generated impls exist. |
| 175 | `field-slice-region-projection-refused` | 3 | RV | 3 | - (borrowck) | - | Reference through &'a [T]/&'a dyn field or slice-pattern element treated as a local place. |
| 176 | `lifetime-self-receiver-parse` | 3 | RV | 3 | - (grammar) | - | `&'a self` / `&'a mut self` receiver shorthand does not parse. |
| 177 | `ref-t-not-clone` | 3 | RV | 3 | - (stdlib) | - | No blanket impl Clone for &T. |
| 178 | `tail-divergence-not-all-paths` | 3 | RV | 3 | S2 | 21 | Tail loop-with-break-value / let-chain if-else not seen as returning on all paths. |
| 179 | `temp-lifetime-for-head-const-promotion` | 3 | RV | 3 | S10 | - | for-head temporaries not extended to loop end; const elements not promoted. |
| 180 | `trait-upcast-only-at-call-args` | 3 | RV | 3 | S4 | 18 | dyn->dyn supertrait upcast only at call args, not let/return/Box. |
| 181 | `closure-if-without-else-refused` | 3 | RV | 2 | S2 | - | Else-less if in expression position refused regardless of type. |
| 182 | `copied-fold-closure-param-generic` | 3 | RV | 2 | S7 | 45 | Copied/Cloned closure param expected type keeps unsubstituted T. |
| 183 | `derive-lifetime-generic-struct` | 3 | RV | 2 | - (metaprog) | - | Derive impl header drops lifetime params (E0726). |
| 184 | `dyn-default-object-lifetime` | 3 | RV | 2 | - (borrowck) | - | Box<dyn Tr> counted as a borrowed elision input; default object lifetime ignored. |
| 185 | `enum-eq-without-impl-accepted` | 3 | AI | 2 | S8 | 26 | C-like enum == falls back to discriminant compare without an impl (E0369 missing). |
| 186 | `generic-struct-literal-field-inference` | 3 | RV | 2 | S7 | 3 | Nested generic field ctor (Vec::new(), None) in a generic literal inferred as <error>. |
| 187 | `impl-elided-lifetime-vs-trait-signature` | 3 | RV | 2 | S9 | - | Impl conformance: elided param region vs trait-arg region shared with ref Self. |
| 188 | `impl-for-concrete-tuple-parse` | 3 | RV | 2 | - (grammar) | - | `impl Tr for (i64, i64)` needs a non-empty impl generic list. |
| 189 | `impl-trait-nested-in-type` | 3 | RV | 2 | S9 | - | impl Trait nested in Option/Box return/arg never resolved. |
| 190 | `nested-unit-variant-type-arg-inference` | 3 | RV | 2 | S7 | 8 | Variant ctor args: expected type not propagated through Box::new. |
| 191 | `operator-impl-for-ref-self` | 3 | RV | 2 | S8 | 26 | Unary/non-Self-Rhs operator impls with Self = &T not dispatched. |
| 192 | `operator-via-generic-impl` | 3 | RV | 2 | S8+S9 | 26 | Binop overload lookup misses impl<T> Add for M<T> (.add() works). |
| 193 | `primitive-inherent-methods-missing` | 3 | RV | 2 | S8 | 1 | &primitive receiver not autoderef'd to stdlib inherent primitive impls. |
| 194 | `private-imported-name-shadows-local` | 3 | RV | 2 | S8 | - | Imported package's private struct outranks a local enum of the same name. |
| 195 | `question-in-closure-attributed-to-fn` | 3 | RV | 2 | S10 | - | `?` check runs while closure return type is unset. |
| 196 | `refutable-subpattern-in-struct-field` | 3 | RV | 2 | S3 | - | Array subpattern in struct field refused under ref binding mode. |
| 197 | `slice-debug-missing` | 3 | RV | 2 | - (stdlib) | - | No Debug for [T]. |
| 198 | `tuple-index-no-autoderef` | 3 | RV | 2 | S6 | 31 | Tuple index peels one &/&mut, never Box/Deref. |
| 199 | `tuple-ord-missing` | 3 | RV | 2 | S9 | 11 | Tuples have builtin comparison ops but implement no cmp traits. |
| 200 | `vec-repeat-form` | 3 | RV | 2 | R0 | - | vec![e; n] with an annotation emits `push(e; n)`; runtime/assoc counts unsupported. |
| 201 | `zip-bogus-param-outlives` | 3 | RV | 2 | S9 | - | WF check of inferred method type args ignores implied outlives bounds. |
| 202 | `as-mut-underscore-cast` | 3 | RV | 1 | S7 | - | `as *mut _` placeholder never unified. |
| 203 | `assoc-const-refers-const-param` | 3 | RV | 1 | S8 | - | Type::<N>::item path does not bind the impl const param. |
| 204 | `assoc-type-path-in-expr` | 3 | RV | 1 | - (grammar)+S9 | 35 | Projection cannot head an expr path; Default::default() at projection-typed site mismatches D::Elem vs D::Elem. |
| 205 | `box-dyn-fn-not-fn` | 3 | RV | 1 | - (stdlib) | 22 | No blanket Fn for Box<F: ?Sized>. |
| 206 | `box-no-clone` | 3 | RV | 1 | - (stdlib) | - | No impl Clone for Box<T>; .clone() autoderefs to T::clone. |
| 207 | `closure-captured-mut-param-needs-mut` | 3 | RV | 1 | S10 | - | Unique-borrow capture of an immutable &mut binding modelled as &mut of the binding. |
| 208 | `collect-into-result-option` | 3 | RV | 1 | S7 | 3 | Tail collect inference gap; annotation leaks into closure body; enum method path resolved as variant. |
| 209 | `const-in-fn-body-parse` | 3 | RV | 1 | - (grammar) | - | Only nested fn allowed in statement position; const/static/struct items refused. |
| 210 | `custom-iterator-box-dyn-item-defaults` | 3 | RV | 1 | - (borrowck) | - | Elided object lifetime of Box<dyn> in a return tied to &mut self. |
| 211 | `default-method-self-static-generic-impl` | 3 | RV | 1 | S8 | 14 | Self:: path in a generic impl does not bind the impl's type params. |
| 212 | `derive-ord-enum-self` | 3 | RV | 1 | S9 | 38 | Enum impl inheriting >=2 default trait methods: Self unbound after the first (every impl Ord for enum refused). |
| 213 | `derive-raw-pointer-field` | 3 | RV | 1 | S8 | 1 | Method call on raw pointer resolves on the pointee; pointer's own Ord/Hash unreachable. |
| 214 | `dyn-assoc-binding-parse` | 3 | RV | 1 | - (grammar) | - | `dyn Tr<Assoc = T>` does not parse. |
| 215 | `dyn-iterator-not-object-safe` | 3 | RV | 1 | S9a | - | stdlib Iterator adapters lack `where Self: Sized`. |
| 216 | `enum-second-default-method` | 3 | RV | 1 | S9 | 38 | Default-method synthesis for enum impls attaches only the first provided method (cf. derive-ord-enum-self). |
| 217 | `float-exp-format` | 3 | MC | 1 | - (stdlib rt) | - | {:e} uses C %e. |
| 218 | `from-bound-ignored-by-question-into` | 3 | RV | 1 | S10 | - | `?` From lookup concrete-impl only; tail .into() gets no target. |
| 219 | `generic-enum-method-no-t-inference` | 3 | RV | 1 | S8 | 8 | &GenericEnum<T> receiver: impl T not unified with caller T (breaks &Option<T>.is_some()). |
| 220 | `generic-eq-moves-operands` | 3 | RV | 1 | S8 | 26,9 | == on a generic ADT instance takes operands by value instead of autoref. |
| 221 | `hashmap-iter-zippair` | 3 | RV | 1 | - (stdlib) | - | Map iterators yield ZipPair struct, not tuples. |
| 222 | `index-clone-keeps-borrow` | 3 | RV | 1 | - (borrowck) | - | Return of type T from a &T param treated as holding a loan on the argument. |
| 223 | `iter-mut-item-enum-method-receiver` | 3 | RV | 1 | S8 | 1 | Method on &Enum with unsuffixed literal arg fails resolution (no autoderef on literal pre-pass). |
| 224 | `let-annotation-flows-into-receiver` | 3 | RV | 1 | S7 | 3 | let annotation Vec<X> pushed into the receiver vec! of a method chain. |
| 225 | `method-generic-drops-impl-bound` | 3 | RV | 1 | S9 | 37 | impl-level T: Copy dropped when the method has its own generic. |
| 226 | `move-closure-self-field-through-ref` | 3 | RV | 1 | S10 | - | Move closure capturing r.f through a ref moves (*r).f instead of capturing r; neighbour: escaping move closure over &Vec reads OOB. |
| 227 | `move-dyn-out-of-box-accepted` | 3 | AI | 1 | S9 | - | No implicit Sized on let bindings: `let d = *box_dyn` accepted (unsized local). |
| 228 | `mul-default-rhs-duplicate-output` | 3 | RV | 1 | S9 | - | Defaulted trait type arg not applied when keying impl assoc types (order-dependent). |
| 229 | `mut-slice-pattern-binds-shared` | 3 | RV | 1 | S3 | - | Slice pattern on &mut [T] binds ref, not ref mut. |
| 230 | `no-vtable-str-literal-to-dyn` | 3 | CC | 1 | S4+S9 | 6 | &&str -> &dyn Display accepted by sema without an impl; backend refuses. |
| 231 | `null-mut-generic-field` | 3 | RV | 1 | S7 | 3 | Generic struct literal field type with enclosing T not pushed into call inference. |
| 232 | `option-combinators-take-fn-pointers` | 3 | RV | 1 | - (stdlib) | - | Option/Result combinators typed as bare fn pointers. |
| 233 | `partial-move-array-len-accepted` | 3 | AI | 1 | S5 | 9 | Array .len() folded to a constant before borrowck; use after (partial) move not seen. |
| 234 | `qualified-path-generic-args-parse` | 3 | RV | 1 | - (grammar) | - | `<Ty<Args> as Tr>::item` does not parse. |
| 235 | `range-in-payload-exhaustiveness` | 3 | RV | 1 | S3 | 24 | Integer ranges under an enum payload are not counted for coverage. |
| 236 | `raw-mut-dyn-reborrow-typing` | 3 | RV | 1 | S4 | 43 | Deref of raw dyn pointer identity-typed; &mut dyn loses mut for ref->raw. |
| 237 | `refcell-replace-noncopy` | 3 | RV | 1 | - (stdlib) | - | RefCell::replace/swap move out of a raw pointer. |
| 238 | `rest-pattern-partial-move-refused` | 3 | RV | 1 | S3+S5 | - | match/if-let/let-else scrutinee of a partially moved place counted as a whole use. |
| 239 | `returned-generic-impl-fn-call-void` | 3 | RV | 1 | S7 | 45 | Same mechanism as closure-param-from-expected-fn-signature. |
| 240 | `rpit-capture-mut-param-unenforced` | 3 | AI | 1 | S9 | - | Borrowck sees the hidden type, not the Rust 2024 capture set; `+ use<>` unparsed. |
| 241 | `string-methods-not-via-autoderef` | 3 | RV | 1 | - (stdlib) | - | String has no Deref<Target=str>; receiver autoderef never reaches str methods (borderline vs blessed str=[u8]). |
| 242 | `tail-match-no-expected-type` | 3 | RV | 1 | S2+S7 | 3 | Bare-expression arms of a tail match get no expected type from the fn return. |
| 243 | `tuple-struct-closure-param-pattern-parse` | 3 | RV | 1 | - (grammar) | - | PEG commits to bare IDENT before pat_single in closure params. |
| 244 | `vec-iter-find-variance` | 3 | RV | 1 | - (borrowck) | - | Elided param region lost on VecIter receiver; &mut Self generic default method gets invariance mismatch. |
| 245 | `implicit-integer-widening-accepted` | 4 | AI | 5 | - (policy) | 42 | coerce.int.implicit-widening: value-preserving widening at args/returns/branches/generics; documented, not blessed. |
| 246 | `recursive-rpit-refused` | 4 | RV | 1 | S9 | - | Recursive self-call's opaque type not identified with the hidden type. |
| 247 | `struct-literal-in-scrutinee-accepted` | 4 | AI | 1 | - (grammar) | - | match scrutinee parsed with general expr (struct literals allowed). |

### 3.2 Merge candidates

Representatives' summaries show these NEW clusters share the mechanism of another cluster; fix and fixture them together.

| cluster | same mechanism as |
|---|---|
| `none-arm-typed-as-scrutinee` | `unit-path-pattern-binds-under-ref` |
| `struct-update-tail-no-drop-base` | `fn-tail-temporaries-not-dropped` |
| `fnonce-generic-capture-leak` | `fn-tail-temporaries-not-dropped` |
| `rangefrom-iterator-empty` | `open-range-for-head-parse` |
| `returned-generic-impl-fn-call-void` | `closure-param-from-expected-fn-signature` |
| `generic-struct-byvalue-never-dropped` | `into-iter-adapter-leaks-source` |
| `enum-second-default-method` | `derive-ord-enum-self` |
| `slice-pattern-element-offset` | `aggregate-literal-elements-mixed-width` |
| `generic-param-moved-into-ctor-double-drop` | `tail-return-owned-double-drop` |
| `ref-binding-mode-refutable-subpattern-runtime` | `literal-behind-ref-coercion-built-i32` |

### 3.3 Repros and members

1. `tail-return-owned-double-drop` (sev 1, MC, 18, S2)
   - Logos: `tests/interactions/min/f2v__m2.logos`
   - Rust: `tests/interactions/min/f2v__m1.rs`
   - members: 1#1 2#1 2#16 3#1 4#1 5#2 6#1 7#1 8#1 9#1 10#1 11#1 11#2 11#3 12#1 13#1 14#1 tt#1
2. `closure-param-ref-to-fat-pointer` (sev 1, MC, 11, S1)
   - Logos: `tests/interactions/min/dynref__v2.logos`
   - Rust: `tests/interactions/min/dynref__v2.rs`
   - members: 1#3 2#18 5#12 8#10 9#7 12#5 14#4 tt#10 tt#15 4#13 12#6
3. `derive-debug-enum-type-name` (sev 1, MC, 11, - (metaprog))
   - Logos: `tests/interactions/min/verify_m39__min.logos`
   - Rust: `tests/interactions/min/verify_m39__min.rs`
   - members: 3#23 4#21 5#36 6#16 7#13 8#21 10#9 12#8 13#7 14#18 tt#39
4. `const-generic-array-length-unbound-in-body` (sev 1, MC, 10, S9)
   - Logos: `tests/interactions/min/cga_2089993__m2.logos`
   - Rust: `tests/interactions/min/cga_2089993__m2.rs`
   - members: 2#5 3#11 8#16 9#10 10#15 14#9 tt#7 7#21 7#22 3#13
5. `derive-partialeq-enum-always-true` (sev 1, MC, 9, - (metaprog))
   - Logos: `tests/interactions/min/v17__derive_enum_eq.logos`
   - Rust: `tests/interactions/min/v17__derive_enum_eq.rs`
   - members: 2#9 3#3 4#20 5#34 6#15 8#20 10#8 12#7 13#6
6. `exhaustiveness-unchecked-infinite-domains` (sev 1, AI, 9, S3)
   - Logos: `tests/interactions/min/f33__v1.logos`
   - Rust: `tests/interactions/min/f33__v1.rs`
   - members: 5#31 5#32 6#11 7#4 8#3 11#10 12#13 14#19 10#19
7. `exhaustiveness-skipped-for-ref-scrutinee` (sev 1, AI, 8, S3)
   - Logos: `tests/interactions/min/fi_verify_m129_2080033__mref.logos`
   - Rust: `tests/interactions/min/fi_verify_m129_2080033__mref.rs`
   - members: 1#6 2#7 4#2 5#39 6#10 8#2 9#8 tt#4
8. `closure-returns-aggregate-via-stack-alloca` (sev 1, MC, 7, S1)
   - Logos: `tests/interactions/min/fi-verify-p24n__min.logos`
   - Rust: `tests/interactions/min/fi-verify-p24n__min.rs`
   - members: 1#4 2#2 5#25 6#3 9#4 10#2 to#1
9. `const-param-value-unbound-at-mono` (sev 1, MC, 7, R0+S9)
   - Logos: `tests/interactions/min/fiverify_u28g_YYvj__d2.logos`
   - Rust: `tests/interactions/min/fiverify_u28g_YYvj__d2.rs`
   - members: 2#6 3#12 5#51 7#23 10#32 14#29 4#22
10. `for-by-value-array-fat-elements` (sev 1, MC, 7, S1)
   - Logos: `tests/interactions/min/fi-verify-p22c__m12.logos`
   - Rust: `tests/interactions/min/fi-verify-p22c__m12.rs`
   - members: 1#15 2#22 8#12 11#13 13#12 14#10 tt#2
11. `for-over-ref-vec-enum-stride` (sev 1, MC, 7, S1)
   - Logos: `tests/interactions/min/fiverify_p16m__m15.logos`
   - Rust: `tests/interactions/min/fiverify_p16m__m15.rs`
   - members: 3#5 6#2 7#6 8#13 11#12 14#5 tt#3
12. `move-closure-capture-shares-slot` (sev 1, MC, 7, S4+S10)
   - Logos: `tests/interactions/min/fi_verify_m11e_2073856__a.logos`
   - Rust: `tests/interactions/min/fi_verify_m11e_2073856__a.rs`
   - members: 1#13 2#19 7#11 8#14 10#3 6#20 12#9
13. `option-ref-unwrap-instance-no-value` (sev 1, MC, 7, S9)
   - Logos: `tests/interactions/min/itermax_verify_2175568__m.logos`
   - Rust: `tests/interactions/min/itermax_verify_2175568__m.rs`
   - members: 3#34 5#46 8#23 9#25 10#39 12#11 13#17
14. `rc-derefmut` (sev 1, AI, 7, - (stdlib))
   - Logos: `tests/interactions/min/rcdm.PoHH__rc_uaf.logos`
   - Rust: `tests/interactions/min/rcdm.PoHH__rc_uaf.rs`
   - members: 2#14 4#16 5#44 6#13 8#8 11#18 14#20
15. `derive-copy-heap-field-accepted` (sev 1, AI, 6, S9)
   - Logos: `tests/interactions/min/u29c_RBSW__min.logos`
   - Rust: `tests/interactions/min/u29c_RBSW__min.rs`
   - members: 3#8 5#43 6#12 7#19 12#14 14#21
16. `loop-break-value-inference-wrong` (sev 1, MC, 6, S2+S7)
   - Logos: `tests/interactions/min/fi_verify_z31e__m3.logos`
   - Rust: `tests/interactions/min/fi_verify_z31e__m3.rs`
   - members: 1#10 4#7 5#5 11#14 tt#8 13#3
17. `aggregate-literal-elements-mixed-width` (sev 1, MC, 5, S7)
   - Logos: `tests/interactions/min/fi_verify_u0j__n1.logos`
   - Rust: `tests/interactions/min/fi_verify_u0j__n1.rs`
   - members: 2#4 3#6 5#17 9#12 tt#6
18. `const-generic-args-ignored-in-type-identity` (sev 1, AI, 5, S9)
   - Logos: `tests/interactions/min/fiverify_cg17__m2.logos`
   - Rust: `tests/interactions/min/fiverify_cg17__m2.rs`
   - members: 2#8 5#52 7#26 9#9 10#20
19. `const-static-array-init-ignores-declared-type` (sev 1, MC, 5, S7)
   - Logos: `tests/interactions/min/fi_verify_m74__s2.logos`
   - Rust: `tests/interactions/min/fi_verify_m74__s2.rs`
   - members: 5#50 7#2 9#11 10#11 14#8
20. `format-args-temporaries-dropped-early` (sev 1, MC, 5, R0)
   - Logos: `tests/interactions/min/fi_verify_p12d__m.logos`
   - Rust: `/home/logos/sandbox/fi_verify_p12d/m.rs`
   - members: 5#47 6#18 10#12 11#6 14#17
21. `format-in-value-position-leaks` (sev 1, MC, 5, S2)
   - Logos: `tests/interactions/min/fi_verify_f57d__t1.logos`
   - Rust: `tests/interactions/min/fi_verify_f57d__t1.rs`
   - members: 4#5 5#30 13#14 14#15 to#7
22. `nested-match-as-block-tail-value` (sev 1, MC, 5, S2)
   - Logos: `tests/interactions/min/fi-verify-p10n-2059873__min.logos`
   - Rust: `tests/interactions/min/fi-verify-p10n-2059873__min.rs`
   - members: 2#3 6#5 6#6 8#15 13#11
23. `shift-lhs-literal-ignores-expected-type` (sev 1, MC, 5, S7)
   - Logos: `tests/interactions/min/shl__b1.logos`
   - Rust: `tests/interactions/min/shl__b1.rs`
   - members: 6#17 7#27 8#19 10#22 11#17
24. `slice-pattern-element-offset` (sev 1, MC, 5, S7)
   - Logos: `tests/interactions/min/fi_verify_z25_2084681__p1.logos`
   - Rust: `tests/interactions/min/fi_verify_z25_2084681__p1.rs`
   - members: 1#7 4#12 11#11 14#6 11#9
25. `assoc-type-equality-unchecked` (sev 1, AI, 4, S9)
   - Logos: `tests/interactions/min/verify_u1b_2137279__u1b.logos`
   - Rust: `tests/interactions/min/verify_u1b_2137279__u1b.rs`
   - members: 4#17 tt#17 12#23 8#5
26. `fn-tail-temporaries-not-dropped` (sev 1, LK, 4, S2)
   - Logos: `tests/interactions/min/verify_m109__mn.logos`
   - Rust: `tests/interactions/min/verify_m109__mn.rs`
   - members: 4#3 11#5 12#2 14#14
27. `literal-behind-ref-coercion-built-i32` (sev 1, MC, 4, S7+S4)
   - Logos: `tests/interactions/min/fiverify_m25_5058__m2.logos`
   - Rust: `tests/interactions/min/fiverify_m25_5058__m2.rs`
   - members: 9#13 4#9 14#7 tt#5
28. `operator-method-on-generic-instance-not-emitted` (sev 1, MC, 4, S9)
   - Logos: `tests/interactions/min/fi_verify_geq__m1.logos`
   - Rust: `tests/interactions/min/fi_verify_geq__m1.rs`
   - members: 3#28 14#30 3#30 13#16
29. `unit-path-pattern-binds-under-ref` (sev 1, MC, 4, S3)
   - Logos: `tests/interactions/min/verify_u18g.VYSn__min.logos`
   - Rust: `tests/interactions/min/verify_u18g.VYSn__min.rs`
   - members: 1#5 3#2 10#6 10#7
30. `closure-call-aggregate-arg-abi` (sev 1, MC, 3, S1)
   - Logos: `tests/interactions/min/f1_C31f__min1.logos`
   - Rust: `tests/interactions/min/f1_C31f__min1.rs`
   - members: 5#1 3#17 7#9
31. `fnonce-closure-treated-copy` (sev 1, AI, 3, S10)
   - Logos: `tests/interactions/min/p07d__min.logos`
   - Rust: `tests/interactions/min/p07d__min.rs`
   - members: 6#14 tt#16 to#8
32. `nested-fn-codegen-miscompile` (sev 1, MC, 3, S7)
   - Logos: `tests/interactions/min/fiv_f51c__i.logos`
   - Rust: `tests/interactions/min/fiv_f51c__i.rs`
   - members: 6#7 10#23 13#4
33. `non-u8-as-char-accepted` (sev 1, AI, 3, S4)
   - Logos: `tests/interactions/min/fiv_u32char_2101729__m2.logos`
   - Rust: `tests/interactions/min/fiv_u32char_2101729__m.rs`
   - members: 3#22 12#26 13#44
34. `operator-on-enum-lowered-as-integer` (sev 1, MC, 3, S8)
   - Logos: `tests/interactions/min/fi_verify_enumops__d1.logos`
   - Rust: `tests/interactions/min/fi_verify_enumops__d1.rs`
   - members: 12#15 14#26 3#9
35. `range-literal-element-type` (sev 1, MC, 3, S10)
   - Logos: `tests/interactions/min/fi_verify_usize__u1.logos`
   - Rust: `tests/interactions/min/fi_verify_usize__u1.rs`
   - members: 7#7 14#34 6#48
36. `raw-deref-unsafe-check-gaps` (sev 1, AI, 3, S8)
   - Logos: `tests/interactions/min/fiv_f26__f26min.logos`
   - Rust: `tests/interactions/min/fiv_f26__f26min.rs`
   - members: 5#26 9#28 9#30
37. `assoc-projection-move-double-free` (sev 1, MC, 2, S5+S9)
   - Logos: `tests/interactions/min/m62v__g3.logos`
   - Rust: `tests/interactions/min/m62v__g3.rs`
   - members: 2#23 4#15
38. `assoc-projection-never-dropped` (sev 1, LK, 2, S9)
   - Logos: `tests/interactions/min/z12d-EK21__a.logos`
   - Rust: `tests/interactions/min/z12d-EK21__a.rs`
   - members: 3#15 11#8
39. `bounded-generic-impl-method-vanishes` (sev 1, MC, 2, S9)
   - Logos: `tests/interactions/min/v_peq.LO48__min_peq.logos`
   - Rust: `tests/interactions/min/v_peq.LO48__min_peq.rs`
   - members: 4#10 6#31
40. `branch-arms-not-unsized` (sev 1, MC, 2, S4)
   - Logos: `tests/interactions/min/fiv_p06d__v_ref.logos`
   - Rust: `tests/interactions/min/fiv_p06d__v_ref.rs`
   - members: 1#38 2#37
41. `break-aggregate-value-lost` (sev 1, MC, 2, S2)
   - Logos: `tests/interactions/min/fi-verify-p25j__m2.logos`
   - Rust: `tests/interactions/min/fi-verify-p25j__m2.rs`
   - members: 1#9 6#4
42. `closure-param-from-generic-bound-sibling` (sev 1, MC, 2, S7)
   - Logos: `tests/interactions/min/fiv_p01l__m7.logos`
   - Rust: `tests/interactions/min/fiv_p01l__m7.rs`
   - members: 11#38 6#52
43. `guard-uses-nested-payload-binding-undefined` (sev 1, MC, 2, S3)
   - Logos: `tests/interactions/min/fi_verify_f34__m10.logos`
   - Rust: `tests/interactions/min/fi_verify_f34__m10.rs`
   - members: 13#10 2#25
44. `index-assign-no-drop-old` (sev 1, MC, 2, S6)
   - Logos: `tests/interactions/min/fi_verify_ab1_yDir__min.logos`
   - Rust: `tests/interactions/min/fi_verify_ab1_yDir__min.rs`
   - members: 1#8 14#13
45. `int-widening-through-reference-accepted` (sev 1, AI, 2, S4)
   - Logos: `tests/interactions/min/fi_verify_p12r5.D3oA__min.logos`
   - Rust: `tests/interactions/min/fi_verify_p12r5.D3oA__min.rs`
   - members: 7#3 13#45
46. `iter-over-array-of-arrays-element-copy` (sev 1, MC, 2, S3)
   - Logos: `tests/interactions/min/fiv_m108__a1.logos`
   - Rust: `tests/interactions/min/fiv_m108__a1.rs`
   - members: 9#14 3#7
47. `loop-conditional-move-drop-flags` (sev 1, MC, 2, S5)
   - Logos: `tests/interactions/min/fiverify_p22n__n1.logos`
   - Rust: `tests/interactions/min/fiverify_p22n__n1.rs`
   - members: 6#9 13#2
48. `mut-self-method-through-shared-generic-ref` (sev 1, AI, 2, S8)
   - Logos: `tests/interactions/min/v_genmut__m1.logos`
   - Rust: `tests/interactions/min/v_genmut__m1.rs`
   - members: 3#4 8#4
49. `none-arm-typed-as-scrutinee` (sev 1, MC, 2, S3)
   - Logos: `tests/interactions/min/fi-verify-p15j__min.logos`
   - Rust: `tests/interactions/min/fi-verify-p15j__min.rs`
   - members: 6#35 8#44
50. `out-of-range-literal-accepted` (sev 1, AI, 2, S7)
   - Logos: `tests/interactions/min/fi_verify_r27_2100612__m300.logos`
   - Rust: `/home/logos/.claude/jobs/3202196e/tmp/fi_verify_r27_2100612/m.rs`
   - members: 7#25 10#21
51. `rangefrom-iterator-empty` (sev 1, MC, 2, L0+S10)
   - Logos: `tests/interactions/min/fiv_f54__m2.logos`
   - Rust: `tests/interactions/min/fiv_f54__m2.rs`
   - members: 11#15 13#5
52. `ref-binding-mode-refutable-subpattern-runtime` (sev 1, MC, 2, S7)
   - Logos: `tests/interactions/min/fi_verify_o7.enpC__p1.logos`
   - Rust: `tests/interactions/min/fi_verify_o7.enpC__p1.rs`
   - members: 1#11 2#21
53. `rest-pattern-partial-move-drop` (sev 1, MC, 2, S3+S5)
   - Logos: `tests/interactions/min/fi_verify_f43.NAz2__m1.logos`
   - Rust: `tests/interactions/min/fi_verify_f43.NAz2__m1.rs`
   - members: 2#24 5#41
54. `rust2024-temporary-scopes` (sev 1, MC, 2, S2)
   - Logos: `tests/interactions/min/fi_verify_m130__f4.logos`
   - Rust: `tests/interactions/min/fi_verify_m130__f4.rs`
   - members: 4#6 5#33
55. `shared-ref-to-mut-ptr-accepted` (sev 1, AI, 2, S4)
   - Logos: `tests/interactions/min/fi_verify_f27__f27m.logos`
   - Rust: `tests/interactions/min/fi_pairs-5__f27_shared_ref_to_mut_ptr.rs`
   - members: 5#27 12#25
56. `slice-array-as-ptr-typing` (sev 1, MC, 2, S8)
   - Logos: `tests/interactions/min/m116_v__m2.logos`
   - Rust: `tests/interactions/min/m116_v__m2.rs`
   - members: 13#41 4#43
57. `tail-match-conditional-move-leaks` (sev 1, MC, 2, S2+S5)
   - Logos: `tests/interactions/min/fiverify-z43-VDBb__m7.logos`
   - Rust: `tests/interactions/min/fiverify-z43-VDBb__m7.rs`
   - members: 11#4 9#2
58. `untyped-closure-param-from-later-call-wrong` (sev 1, MC, 2, S7)
   - Logos: `tests/interactions/min/fi_verify_closinfer__g.logos`
   - Rust: `tests/interactions/min/fi_verify_closinfer__g.rs`
   - members: 13#8 7#8
59. `user-index-operand-unchecked` (sev 1, AI, 2, S6+S8)
   - Logos: `tests/interactions/min/verify_idx_QddQ__m1.logos`
   - Rust: `tests/interactions/min/verify_idx_QddQ__m1.rs`
   - members: 9#26 10#18
60. `user-name-collides-with-std-method` (sev 1, MC, 2, S8)
   - Logos: `tests/interactions/min/fi_verify_pow__m9.logos`
   - Rust: `tests/interactions/min/fi_verify_pow__m9.rs`
   - members: 10#42 1#51
61. `vec-swap-tuple-elements` (sev 1, MC, 2, S1)
   - Logos: `tests/interactions/min/swapScRd__m5.logos`
   - Rust: `tests/interactions/min/swapScRd__m5.rs`
   - members: 8#17 13#13
62. `break-value-type-mismatch-accepted` (sev 1, AI, 1, S2)
   - Logos: `tests/interactions/min/verify_p17r__n2.logos`
   - Rust: `tests/interactions/min/verify_p17r__n2.rs`
   - members: 2#11
63. `byvalue-method-through-box-field-double-free` (sev 1, MC, 1, S5)
   - Logos: `tests/interactions/min/fi_verify_m129_2195300__d.logos`
   - Rust: `tests/interactions/min/fi_verify_m129_2195300__d.rs`
   - members: 9#16
64. `collect-refs-drops-loan` (sev 1, AI, 1, - (borrowck))
   - Logos: `tests/interactions/min/collect.RX5M__m1.logos`
   - Rust: `tests/interactions/min/collect.RX5M__m1.rs`
   - members: 8#7
65. `compound-assign-uses-add` (sev 1, MC, 1, S6)
   - Logos: `tests/interactions/min/fiverify_aa_onUM__aa_min3.logos`
   - Rust: `tests/interactions/min/fiverify_aa_onUM__aa_min3.rs`
   - members: 3#10
66. `const-expr-fmt-arg-uninit` (sev 1, MC, 1, S7)
   - Logos: `tests/interactions/min/fiverify-p29-ca7p__n1.logos`
   - Rust: `tests/interactions/min/fiverify-p29-ca7p__n1.rs`
   - members: 6#32
67. `fnonce-generic-capture-leak` (sev 1, LK, 1, S2)
   - Logos: `tests/interactions/min/fi_verify_fnonce_str__tail_temp.logos`
   - Rust: `tests/interactions/min/fi_verify_fnonce_str__tail_temp.rs`
   - members: 14#69
68. `generic-instance-key-collision` (sev 1, MC, 1, S9)
   - Logos: `tests/interactions/min/fiv_f40__m3.logos`
   - Rust: `tests/interactions/min/fiv_f40__m3.rs`
   - members: 5#38
69. `generic-param-moved-into-ctor-double-drop` (sev 1, MC, 1, S2+S5)
   - Logos: `tests/interactions/min/fi_verify_genbox__n1.logos`
   - Rust: `tests/interactions/min/fi_verify_genbox__n1.rs`
   - members: 14#2
70. `if-arms-unsize-miscompiled` (sev 1, MC, 1, S4)
   - Logos: `tests/interactions/min/fi-verify-p06e-xu52__m1.logos`
   - Rust: `tests/interactions/min/fi-verify-p06e-xu52__m1.rs`
   - members: 2#20
71. `literal-lhs-of-rem-typed-signed` (sev 1, MC, 1, S7)
   - Logos: `tests/interactions/min/fi_verify_z63_IXqb__m4.logos`
   - Rust: `tests/interactions/min/fi_verify_z63_IXqb__m4.rs`
   - members: 11#16
72. `match-on-deref-mut-raw-param` (sev 1, MC, 1, S2)
   - Logos: `tests/interactions/min/fiverify_m103_VXYk__min.logos`
   - Rust: `tests/interactions/min/fiverify_m103_VXYk__min.rs`
   - members: 9#15
73. `move-closure-returning-capture-double-drop` (sev 1, MC, 1, S10)
   - Logos: `tests/interactions/min/fiverify_m17_WTJF__a3.logos`
   - Rust: `tests/interactions/min/fiverify_m17_WTJF__a3.rs`
   - members: 7#10
74. `move-out-of-box-in-match-uaf` (sev 1, MC, 1, S2+S5)
   - Logos: `tests/interactions/min/p17q.IRQ9__q1.logos`
   - Rust: `tests/interactions/min/p17q.IRQ9__q1.rs`
   - members: 6#8
75. `nested-tuple-pattern-through-ref` (sev 1, MC, 1, S3)
   - Logos: `tests/interactions/min/m95v__m.logos`
   - Rust: `tests/interactions/min/m95v__m.rs`
   - members: 4#11
76. `open-range-for-head-parse` (sev 1, MC, 1, L0+S10)
   - Logos: `tests/interactions/min/fi_verify_ae1__min_open_range.logos`
   - Rust: `tests/interactions/min/fi_verify_ae1__min_open_range.rs`
   - members: 1#47
77. `rangeinclusive-u8-contains` (sev 1, MC, 1, S10+S4)
   - Logos: `tests/interactions/min/u8rc__min.logos`
   - Rust: `tests/interactions/min/u8rc__min.rs`
   - members: 8#18
78. `rawptr-write-closure-classified-fnmut` (sev 1, MC, 1, S10+S6)
   - Logos: `tests/interactions/min/z16leFh__d3.logos`
   - Rust: `tests/interactions/min/z16leFh__d3.rs`
   - members: 11#43
79. `reassign-ref-box-dyn-double-free` (sev 1, MC, 1, S6+S1)
   - Logos: `tests/interactions/min/fi_verify_boxref__d6.logos`
   - Rust: `tests/interactions/min/fi_verify_boxref__d6.rs`
   - members: 14#3
80. `u16-mul-overflow-unchecked` (sev 1, MC, 1, S7)
   - Logos: `tests/interactions/min/u16mul__narrow_lit.logos`
   - Rust: `tests/interactions/min/u16mul__narrow_lit.rs`
   - members: 10#17
81. `vec-contains-bitcopy` (sev 1, MC, 1, - (stdlib))
   - Logos: `tests/interactions/min/vc28__mc.logos`
   - Rust: `tests/interactions/min/vc28__mc.rs`
   - members: 12#3
82. `fmt-instance-for-ref-payload-missing` (sev 2, CC, 12, S9)
   - Logos: `tests/interactions/min/v__a.logos`
   - Rust: `tests/interactions/min/v__a.rs`
   - members: 1#49 2#29 5#40 6#25 7#32 8#25 9#22 10#38 11#28 12#17 14#31 14#32
83. `const-generic-struct-literal-error-type` (sev 2, CC, 10, S7)
   - Logos: `tests/interactions/min/fiv-p29i-cBQJ__min.logos`
   - Rust: `tests/interactions/min/fiv-p29i-cBQJ__min.rs`
   - members: 2#30 3#31 6#28 7#24 8#27 9#21 10#33 11#25 14#28 tt#14
84. `into-iter-adapter-leaks-source` (sev 2, LK, 9, S9)
   - Logos: `tests/interactions/min/fi-verify-m8h.7QbN__m.logos`
   - Rust: `tests/interactions/min/fi-verify-m8h.7QbN__m.rs`
   - members: 1#14 5#23 6#33 7#30 8#31 14#16 to#5 tt#45 12#28
85. `box-dyn-drop-glue-missing` (sev 2, LK, 7, S4+S9)
   - Logos: `tests/interactions/min/m46_2157033__min.logos`
   - Rust: `tests/interactions/min/m46_2157033__min.rs`
   - members: 4#14 5#11 7#29 10#16 12#4 to#3 tt#9
86. `generic-static-trait-call-not-emitted` (sev 2, CC, 7, S9)
   - Logos: `tests/interactions/min/m63__m4.logos`
   - Rust: `(inline in verdict)`
   - members: 1#25 1#26 1#27 8#26 10#37 tt#13 13#15
87. `literal-subpattern-under-ref-no-deref` (sev 2, CC, 7, S3)
   - Logos: `tests/interactions/min/v119__a.logos`
   - Rust: `tests/interactions/min/v119__a.rs`
   - members: 1#12 2#26 9#23 5#29 7#33 11#24 tt#12
88. `closure-byvalue-param-not-dropped` (sev 2, LK, 6, S5)
   - Logos: `tests/interactions/min/fi_verify_f3_2058661__m1.logos`
   - Rust: `tests/interactions/min/fi_verify_f3_2058661__m1.rs`
   - members: 4#4 5#3 8#29 9#3 10#14 11#7
89. `slice-pattern-subpattern-bindings-lost` (sev 2, CC, 6, S3)
   - Logos: `tests/interactions/min/p14f-verify__m1.logos`
   - Rust: `tests/interactions/min/p14f-verify__m1.rs`
   - members: 1#45 2#27 11#23 tt#11 7#5 3#32
90. `borrowed-closure-literal-arg` (sev 2, MC, 5, S10)
   - Logos: `tests/interactions/min/fi-verify-p05k__n7.logos`
   - Rust: `tests/interactions/min/fi-verify-p05k__n7.rs`
   - members: 2#17 5#4 8#11 9#5 10#4
91. `box-dyn-struct-field-unsize` (sev 2, MC, 5, S4)
   - Logos: `tests/interactions/min/fi_verify_m55_2072703__a1.logos`
   - Rust: `tests/interactions/min/fi_verify_m55_2072703__a1.rs`
   - members: 1#2 8#9 9#6 10#5 to#2
92. `collect-vec-underscore-ice` (sev 2, CC, 5, S7)
   - Logos: `tests/interactions/min/fi_verify_collect__m6.logos`
   - Rust: `tests/interactions/min/fi_verify_collect__m6.rs`
   - members: 4#26 6#26 7#31 8#24 12#19
93. `empty-macro-args-metacall` (sev 2, CC, 5, R0)
   - Logos: `tests/interactions/min/fi_verify_panic_noargs__m4.logos`
   - Rust: `tests/interactions/min/fi_verify_panic_noargs__r.rs`
   - members: 2#31 3#33 8#28 12#22 14#57
94. `generic-typevar-leaks-to-codegen` (sev 2, CC, 5, S7)
   - Logos: `tests/interactions/min/fi-verify-p02f__u9.logos`
   - Rust: `tests/interactions/min/fi-verify-p02f__u9.rs`
   - members: 2#28 9#20 14#25 5#6 9#19
95. `adapter-instance-no-value` (sev 2, CC, 3, S9)
   - Logos: `tests/interactions/min/fi_verify_boxiter__bi_cnt.logos`
   - Rust: `tests/interactions/min/fi_verify_boxiter__bi_min.rs`
   - members: 10#40 12#18 12#20
96. `box-dyn-represented-as-ref-dyn` (sev 2, CC, 3, S9)
   - Logos: `tests/interactions/min/fiv_p01b__m1.logos`
   - Rust: `tests/interactions/min/fiv_p01b__m1.rs`
   - members: 2#32 2#33 13#30
97. `derive-partialeq-tuple-struct-codegen` (sev 2, CC, 3, - (metaprog))
   - Logos: `tests/interactions/min/fi_verify_tsd__t6.logos`
   - Rust: `tests/interactions/min/fi_verify_tsd__t6.rs`
   - members: 6#24 7#15 1#54
98. `temp-receiver-or-place-base-not-dropped` (sev 2, LK, 3, S5)
   - Logos: `tests/interactions/min/verify_m25b_2057968__pc.logos`
   - Rust: `tests/interactions/min/verify_m25b_2057968__pc.rs`
   - members: 10#13 9#18 3#16
99. `closure-unsafe-block-body-return` (sev 2, CC, 2, S2)
   - Logos: `tests/interactions/min/fiv_z16b_2058447__l.logos`
   - Rust: `tests/interactions/min/fiv_z16b_2058447__l.rs`
   - members: 4#24 11#26
100. `collect-into-vec-underscore-refused` (sev 2, RV, 2, S7)
   - Logos: `tests/interactions/min/fi_verify_z34.wRM2__min.logos`
   - Rust: `tests/interactions/min/fi_verify_z34.wRM2__min.rs`
   - members: 11#61 13#26
101. `disjoint-closure-capture-missing` (sev 2, MC, 2, S10)
   - Logos: `tests/interactions/min/m18b.wRWv__d.logos`
   - Rust: `tests/interactions/min/m18b.wRWv__d.rs`
   - members: 7#12 12#10
102. `dyn-eq-operator-builtin` (sev 2, AI, 2, S8)
   - Logos: `tests/interactions/min/u05b.N9lN__u05b_min.logos`
   - Rust: `tests/interactions/min/fi_dir_pairs-14__u05b_bad.rs`
   - members: 14#11 14#12
103. `generic-body-checked-per-instantiation` (sev 2, AI, 2, S9)
   - Logos: `tests/interactions/min/fiv_p02r_FrGe__a.logos`
   - Rust: `tests/interactions/min/fiv_p02r_FrGe__a.rs`
   - members: 3#19 to#9
104. `generic-struct-update-infers-nothing` (sev 2, CC, 2, S7)
   - Logos: `tests/interactions/min/gupd__min_gen_fru.logos`
   - Rust: `tests/interactions/min/gupd__min_gen_fru.rs`
   - members: 10#30 14#27
105. `infinite-size-recursive-type-crash` (sev 2, CC, 2, -)
   - Logos: `tests/interactions/min/fi_verify_p18r_2176343__m1.logos`
   - Rust: `tests/interactions/min/fi_verify_p18r_2176343__m1.rs`
   - members: 9#27 12#16
106. `move-out-of-box-leaks-allocation` (sev 2, LK, 2, S2+S5)
   - Logos: `tests/interactions/min/m33vqtM__x3.logos`
   - Rust: `tests/interactions/min/m33vqtM__x3.rs`
   - members: 8#30 9#17
107. `nested-fn-item-in-block-expr` (sev 2, CC, 2, S2)
   - Logos: `tests/interactions/min/fi_verify_m90__min.logos`
   - Rust: `tests/interactions/min/fi_verify_m90__min.rs`
   - members: 3#35 9#24
108. `no-vtable-box-box-dyn` (sev 2, CC, 2, S8+S4)
   - Logos: `tests/interactions/min/fi_verify_y11__m.logos`
   - Rust: `tests/interactions/min/fi_verify_y11__m.rs`
   - members: 1#20 11#21
109. `parse-target-unresolved-ice` (sev 2, CC, 2, S7)
   - Logos: `tests/interactions/min/fi_verify_m70__min.logos`
   - Rust: `tests/interactions/min/fi_verify_m70__min.rs`
   - members: 4#25 7#34
110. `untyped-closure-param-codegen-crash` (sev 2, CC, 2, S7)
   - Logos: `tests/interactions/min/fi_verify_f33c-4tLL__m9.logos`
   - Rust: `tests/interactions/min/fi_verify_f33c-4tLL__m9.rs`
   - members: 13#9 1#44
111. `byvalue-trait-method-via-ref-copy-generic` (sev 2, CC, 1, S8)
   - Logos: `tests/interactions/min/p02e__v3.logos`
   - Rust: `tests/interactions/min/p02e__v3.rs`
   - members: 6#30
112. `char-classification-ascii-only` (sev 2, MC, 1, - (stdlib))
   - Logos: `tests/interactions/min/fi_verify_char_uni__cu.logos`
   - Rust: `tests/interactions/min/fi_verify_char_uni__cu.rs`
   - members: 12#12
113. `closure-capture-raw-pointer-undefined` (sev 2, CC, 1, S10)
   - Logos: `tests/interactions/min/fi-verify-p08f.pRr6__c1.logos`
   - Rust: `tests/interactions/min/fi-verify-p08f.pRr6__c1.rs`
   - members: 6#29
114. `closure-conditional-move-drop-flag-ice` (sev 2, CC, 1, S5+S10)
   - Logos: `tests/interactions/min/fi_verify_z53c__min.logos`
   - Rust: `tests/interactions/min/fi_verify_z53c__min.rs`
   - members: 11#27
115. `const-generic-array-default-method-trap` (sev 2, CC, 1, S9)
   - Logos: `tests/interactions/min/fi_verify_m19__pf.logos`
   - Rust: `tests/interactions/min/fi_verify_m19__pf.rs`
   - members: 10#36
116. `derive-const-generic-struct` (sev 2, CC, 1, - (metaprog)+S9)
   - Logos: `tests/interactions/min/fi_verify_m74__h7.logos`
   - Rust: `tests/interactions/min/fi_verify_m74__h7.rs`
   - members: 7#20
117. `empty-array-literal-turbofish-slice` (sev 2, CC, 1, S7)
   - Logos: `tests/interactions/min/v_y1__m7.logos`
   - Rust: `tests/interactions/min/v_y1__r7.rs`
   - members: 11#22
118. `generic-bst-compile-hang` (sev 2, CC, 1, - (borrowck perf))
   - Logos: `tests/interactions/min/y9ur1i__min.logos`
   - Rust: `tests/interactions/min/y9ur1i__min.rs`
   - members: 11#31
119. `generic-enum-method-duplicate-mangling` (sev 2, CC, 1, S9)
   - Logos: `tests/interactions/min/fi_verify_y8dup__h2.logos`
   - Rust: `tests/interactions/min/fi_verify_y8dup__h2.rs`
   - members: 11#30
120. `generic-struct-byvalue-never-dropped` (sev 2, LK, 1, S9)
   - Logos: `tests/interactions/min/fi-verify-m16l__vb.logos`
   - Rust: `tests/interactions/min/fi-verify-m16l__vb.rs`
   - members: to#6
121. `integer-placeholder-in-tuple-to-codegen` (sev 2, CC, 1, S7)
   - Logos: `tests/interactions/min/fiv_p27i_x__b.logos`
   - Rust: `tests/interactions/min/fiv_p27i_x__b.rs`
   - members: 6#27
122. `match-arm-block-semicolon-ignored` (sev 2, AI, 1, S2)
   - Logos: `tests/interactions/min/semi_arm__min.logos`
   - Rust: `tests/interactions/min/semi_arm__min.rs`
   - members: 8#43
123. `metacall-print-box-types` (sev 2, CC, 1, R0)
   - Logos: `tests/interactions/min/verify-boxprint-2173748__d2.logos`
   - Rust: `tests/interactions/min/verify-boxprint-2173748__d2.rs`
   - members: 4#23
124. `no-vtable-integer-literal-to-dyn` (sev 2, CC, 1, S7)
   - Logos: `tests/interactions/min/fi_verify_dynlit.tNo7__m_i32.logos`
   - Rust: `tests/interactions/min/fi_verify_dynlit.tNo7__r_i32.rs`
   - members: 3#36
125. `primitive-method-path-as-fn` (sev 2, CC, 1, S8)
   - Logos: `tests/interactions/min/fi_verify_z59__c1.logos`
   - Rust: `tests/interactions/min/fi_verify_z59__c1.rs`
   - members: 11#29
126. `sema-missing-trait-check-before-vtable` (sev 2, CC, 1, S4)
   - Logos: `tests/interactions/min/f13v__m2.logos`
   - Rust: `tests/interactions/min/f13v__m1.rs`
   - members: 5#13
127. `struct-update-tail-no-drop-base` (sev 2, LK, 1, S2)
   - Logos: `tests/interactions/min/fiv_u18k__min_tail_temp.logos`
   - Rust: `tests/interactions/min/fiv_u18k__min_tail_temp.rs`
   - members: 3#14
128. `unclosed-brace-crash` (sev 2, CC, 1, - (grammar))
   - Logos: `tests/interactions/min/f15E7jX__m1.logos`
   - Rust: `tests/interactions/min/f15E7jX__m1.rs`
   - members: 5#15
129. `vec-truncate-drop-order` (sev 2, MC, 1, - (stdlib))
   - Logos: `tests/interactions/min/fiv-trunc-2194838__m.logos`
   - Rust: `tests/interactions/min/fiv-trunc-2194838__m.rs`
   - members: 6#19
130. `deref-coercion-missing-at-coercion-site` (sev 3, RV, 15, S4)
   - Logos: `tests/interactions/min/fiverify_m115__a.logos`
   - Rust: `tests/interactions/min/fiverify_m115__a.rs`
   - members: 1#35 2#49 4#42 6#42 7#36 8#50 9#48 11#59 12#43 13#28 14#47 tt#40 3#38 2#48 12#44
131. `closure-param-from-expected-fn-signature` (sev 3, RV, 13, S7+S10)
   - Logos: `tests/interactions/min/fi_verify_m133__a.logos`
   - Rust: `tests/interactions/min/fi_verify_m133__a.rs`
   - members: 1#37 3#43 4#34 5#53 9#39 10#44 11#45 tt#34 7#41 8#40 13#22 14#40 12#59
132. `collect-sum-target-from-return-type` (sev 3, RV, 12, S2+S7)
   - Logos: `tests/interactions/min/m16__sum_tail.logos`
   - Rust: `tests/interactions/min/m16__sum_tail.rs`
   - members: 1#31 2#54 3#41 4#45 5#21 8#37 9#57 10#47 11#33 12#29 14#43 tt#24
133. `generic-inference-through-deref-coercion` (sev 3, RV, 12, S7)
   - Logos: `tests/interactions/min/fi_verify_f7__m1.logos`
   - Rust: `tests/interactions/min/fi_verify_f7__m1.rs`
   - members: 1#33 3#42 4#31 5#7 6#43 7#35 9#33 10#31 13#27 8#42 11#35 14#39
134. `vec-macro-requires-copy` (sev 3, RV, 12, R0)
   - Logos: `tests/interactions/min/fi_verify_vecnc__a.logos`
   - Rust: `tests/interactions/min/fi_verify_vecnc__a.rs`
   - members: 1#36 3#44 4#30 6#44 7#39 9#37 10#27 13#21 14#35 tt#26 12#37 11#56
135. `assoc-type-binding-not-normalized` (sev 3, RV, 11, S9)
   - Logos: `tests/interactions/min/fi_verify_m71__min.logos`
   - Rust: `tests/interactions/min/fi_verify_m71__min.rs`
   - members: 1#28 4#29 5#22 6#49 9#42 10#49 12#30 tt#18 11#47 7#47 tt#19
136. `amp-impl-trait-and-bounds-parse` (sev 3, RV, 10, - (grammar))
   - Logos: `tests/interactions/min/fi_verify_m49__b1.logos`
   - Rust: `tests/interactions/min/fi_verify_m49__a1.rs`
   - members: 1#40 2#40 5#18 10#53 12#31 8#46 7#45 11#44 14#50 2#42
137. `range-index-only-slice-array` (sev 3, RV, 10, S6)
   - Logos: `tests/interactions/min/f8LOOx__m.logos`
   - Rust: `tests/interactions/min/f8LOOx__m.rs`
   - members: 1#34 5#8 12#39 tt#25 14#46 5#16 3#46 10#51 4#32 8#47
138. `for-in-over-generic-iterator-param` (sev 3, RV, 9, S10)
   - Logos: `tests/interactions/min/verify_m37b_x__a.logos`
   - Rust: `tests/interactions/min/verify_m37b_x__a.rs`
   - members: 1#29 2#41 7#44 9#38 10#45 11#46 13#31 14#42 8#38
139. `smart-pointer-inherent-method-shadows-pointee` (sev 3, RV, 9, S8)
   - Logos: `tests/interactions/min/fi_verify_boxget__box_get_shadow.logos`
   - Rust: `tests/interactions/min/fi_verify_boxget__box_get_shadow.rs`
   - members: 3#52 4#37 6#37 7#54 9#50 11#58 12#32 13#33 14#38
140. `unsize-in-aggregate-generic-ctor-slot` (sev 3, RV, 9, R0+S4)
   - Logos: `tests/interactions/min/fi_verify_m14_2076064__min.logos`
   - Rust: `tests/interactions/min/fi_verify_m14_2076064__min.rs`
   - members: 2#36 6#45 7#49 8#48 10#43 11#41 14#36 tt#28 11#42
141. `borrow-of-unit-path-undefined` (sev 3, RV, 8, S8)
   - Logos: `tests/interactions/min/fiverify_p09e_1JFm__min2.logos`
   - Rust: `tests/interactions/min/fiverify_p09e_1JFm__min2.rs`
   - members: 1#55 3#39 4#40 6#53 9#40 10#50 13#38 tt#30
142. `closure-ref-pattern-param-not-peeled` (sev 3, RV, 8, S3+S10)
   - Logos: `tests/interactions/min/fi_verify_m35__a.logos`
   - Rust: `tests/interactions/min/fi_verify_m35__a.rs`
   - members: 1#48 4#41 6#47 7#42 11#39 14#41 9#47 10#54
143. `guard-in-loop-assign-twice` (sev 3, RV, 8, S3)
   - Logos: `tests/interactions/min/fiv_f28__m9.logos`
   - Rust: `tests/interactions/min/fiv_f28__m9.rs`
   - members: 1#42 4#28 5#28 6#41 8#32 9#44 13#29 14#55
144. `vec-new-element-from-later-use` (sev 3, RV, 8, S7)
   - Logos: `tests/interactions/min/fi_verify_m12__f.logos`
   - Rust: `tests/interactions/min/fi_verify_m12__c.rs`
   - members: 1#46 3#53 4#44 8#36 9#34 11#37 10#48 7#43
145. `partialeq-eq-both-ambiguous` (sev 3, RV, 7, - (stdlib cmp split))
   - Logos: `tests/interactions/min/fi_verify_f47__both.logos`
   - Rust: `tests/interactions/min/fi_verify_f47__both.rs`
   - members: 3#26 5#45 7#17 9#54 12#21 14#60 tt#36
146. `break-none-after-break-some-refused` (sev 3, RV, 6, S2+S7)
   - Logos: `tests/interactions/min/m73v__e.logos`
   - Rust: `tests/interactions/min/m73v__e.rs`
   - members: 2#46 3#49 4#8 9#43 12#45 14#58
147. `deferred-let-no-type-parse` (sev 3, RV, 6, - (grammar)+S7)
   - Logos: `tests/interactions/min/d1v_2147782__min.logos`
   - Rust: `tests/interactions/min/d1v_2147782__min.rs`
   - members: 1#57 2#57 4#51 12#46 tt#44 7#61
148. `nested-fn-not-in-scope` (sev 3, RV, 6, S8)
   - Logos: `tests/interactions/min/v17b__nfr.logos`
   - Rust: `tests/interactions/min/v17b__nfr.rs`
   - members: 1#56 2#56 6#40 10#52 12#55 9#35
149. `rpit-not-opaque` (sev 3, AI, 6, S9)
   - Logos: `tests/interactions/min/rpit_yphZ__rpit_field.logos`
   - Rust: `tests/interactions/min/rpit_yphZ__rpit_field.rs`
   - members: 2#13 3#18 4#18 12#24 14#23 9#31
150. `split-adapter-static-lifetime` (sev 3, RV, 6, - (borrowck))
   - Logos: `tests/interactions/min/fiv_m72__c1.logos`
   - Rust: `tests/interactions/min/fiv_m72__c1.rs`
   - members: 1#58 2#50 4#49 7#53 11#62 13#32
151. `const-N-not-inferred-from-array` (sev 3, RV, 5, S7)
   - Logos: `tests/interactions/min/fi_verify_m140__m140d.logos`
   - Rust: `(inline in verdict)`
   - members: 4#46 7#48 9#58 11#52 5#49
152. `derive-debug-c-field-lexed-cstr` (sev 3, RV, 5, R0)
   - Logos: `tests/interactions/min/vcfield__p_c.logos`
   - Rust: `tests/interactions/min/vcfield__p_c.rs`
   - members: 3#24 5#37 6#23 7#14 10#24
153. `expected-type-not-reaching-literal-in-ctor` (sev 3, RV, 5, S7)
   - Logos: `tests/interactions/min/fi_verify_litenum__c1.logos`
   - Rust: `tests/interactions/min/fi_verify_litenum__c1.rs`
   - members: 3#40 7#37 1#50 13#35 13#23
154. `format-args-reparse-loses-deref` (sev 3, RV, 5, R0)
   - Logos: `tests/interactions/min/fi_verify_m03f.EMj8__a.logos`
   - Rust: `tests/interactions/min/fi_verify_m03f.EMj8__a.rs`
   - members: 1#16 8#49 11#20 12#36 14#48
155. `parse-target-through-question` (sev 3, RV, 5, S10+S7)
   - Logos: `tests/interactions/min/fi_verify_parseq__m.logos`
   - Rust: `tests/interactions/min/fi_verify_parseq__m.rs`
   - members: 8#41 10#46 11#55 12#42 14#68
156. `refmut-method-temp-behind-ref` (sev 3, RV, 5, S6)
   - Logos: `tests/interactions/min/verify_refmut_field__m7.logos`
   - Rust: `tests/interactions/min/verify_refmut_field__r7.rs`
   - members: 1#18 4#39 6#36 7#51 8#45
157. `structural-copy-clone-not-trait-impls` (sev 3, RV, 5, S9)
   - Logos: `tests/interactions/min/fi_verify_m16opt__min.logos`
   - Rust: `tests/interactions/min/fi_verify_m16opt__min.rs`
   - members: 8#35 12#49 tt#27 11#53 3#25
158. `cmp-impls-require-eq-or-missing` (sev 3, RV, 4, - (stdlib))
   - Logos: `tests/interactions/min/fi_verify_f44__m.logos`
   - Rust: `tests/interactions/min/fi_verify_f44__m.rs`
   - members: 3#27 7#59 12#61 5#42
159. `const-item-as-const-generic-arg` (sev 3, RV, 4, S8)
   - Logos: `tests/interactions/min/fi_verify_m135__a2.logos`
   - Rust: `tests/interactions/min/fi_verify_m135__a2.rs`
   - members: 3#45 8#56 9#52 11#51
160. `double-deref-box-dyn-to-dyn-refused` (sev 3, RV, 4, R0+S4)
   - Logos: `tests/interactions/min/fi_verify_f9__k.logos`
   - Rust: `tests/interactions/min/fi_verify_f9__k.rs`
   - members: 1#17 4#36 5#9 10#41
161. `generic-callable-param-to-adapter` (sev 3, RV, 4, S7)
   - Logos: `tests/interactions/min/fi_verify_z58__m4.logos`
   - Rust: `tests/interactions/min/fi_verify_z58__m1.rs`
   - members: 1#32 11#54 tt#32 tt#33
162. `nested-fn-captures-outer-local` (sev 3, AI, 4, S8)
   - Logos: `tests/interactions/min/fi_verify_u29nested__m4.logos`
   - Rust: `tests/interactions/min/fi_verify_u29nested__m.rs`
   - members: 2#12 3#20 4#19 8#6
163. `no-implicit-reborrow-of-mut-receiver` (sev 3, RV, 4, S8)
   - Logos: `tests/interactions/min/fi-verify-m7x__m2.logos`
   - Rust: `tests/interactions/min/fi-verify-m7x__m2.rs`
   - members: 7#50 to#4 4#33 12#34
164. `partialord-marker-no-lt` (sev 3, RV, 4, - (stdlib cmp split))
   - Logos: `tests/interactions/min/fi_verify_m73__m73min.logos`
   - Rust: `tests/interactions/min/fi_verify_m73__m73min.rs`
   - members: 4#48 7#18 10#57 14#61
165. `trait-static-call-self-inferred` (sev 3, RV, 4, S7+S8)
   - Logos: `tests/interactions/min/fi_verify_m11g_2243084__e.logos`
   - Rust: `tests/interactions/min/fi_verify_m11g_2243084__e.rs`
   - members: 10#29 12#60 14#63 8#57
166. `assoc-const-typed-self` (sev 3, RV, 3, S9)
   - Logos: `tests/interactions/min/fi_verify_f20__m2.logos`
   - Rust: `tests/interactions/min/fi_verify_f20__m2.rs`
   - members: 2#53 5#20 7#46
167. `assoc-fn-path-as-value` (sev 3, RV, 3, - (grammar))
   - Logos: `tests/interactions/min/m19i__b2.logos`
   - Rust: `tests/interactions/min/m19i__b2.rs`
   - members: 8#54 12#62 tt#43
168. `blanket-impl-not-found-for-bound` (sev 3, RV, 3, S9)
   - Logos: `tests/interactions/min/verify_m23__min.logos`
   - Rust: `tests/interactions/min/verify_m23__min.rs`
   - members: 1#23 4#35 10#35
169. `box-dyn-as-ref-missing` (sev 3, RV, 3, S8)
   - Logos: `tests/interactions/min/boxasref.Y4Ls__c.logos`
   - Rust: `tests/interactions/min/boxasref.Y4Ls__c.rs`
   - members: 8#55 13#42 14#49
170. `box-rc-display-debug-missing` (sev 3, RV, 3, - (stdlib))
   - Logos: `tests/interactions/min/fi_verify_m22a__bx.logos`
   - Rust: `tests/interactions/min/fi_verify_m22a__bx.rs`
   - members: 12#54 13#34 3#48
171. `break-value-operand-treated-as-move` (sev 3, RV, 3, S2)
   - Logos: `tests/interactions/min/verify_f56__m9.logos`
   - Rust: `tests/interactions/min/verify_f56__m9.rs`
   - members: 12#47 tt#35 13#39
172. `closure-return-region-of-param` (sev 3, RV, 3, - (borrowck))
   - Logos: `tests/interactions/min/fi_verify_m71__t1.logos`
   - Rust: `tests/interactions/min/fi_verify_m71__t1.rs`
   - members: 5#24 10#58 12#40
173. `core-impls-missing-char-sum` (sev 3, RV, 3, - (stdlib))
   - Logos: `tests/interactions/min/fi_verify_m19h__m2.logos`
   - Rust: `tests/interactions/min/fi_verify_m19h__m19h.rs`
   - members: 8#52 12#52 11#63
174. `derived-impls-invisible-in-impl-blocks` (sev 3, RV, 3, - (metaprog order))
   - Logos: `tests/interactions/min/fi_verify_clone14__n3.logos`
   - Rust: `tests/interactions/min/fi_verify_clone14__r3.rs`
   - members: 6#21 10#25 14#62
175. `field-slice-region-projection-refused` (sev 3, RV, 3, - (borrowck))
   - Logos: `tests/interactions/min/fi_verify_slicepat__g.logos`
   - Rust: `tests/interactions/min/fi_verify_slicepat__g.rs`
   - members: 6#50 3#51 14#54
176. `lifetime-self-receiver-parse` (sev 3, RV, 3, - (grammar))
   - Logos: `tests/interactions/min/lsv__p.logos`
   - Rust: `tests/interactions/min/lsv__p.rs`
   - members: 5#55 tt#31 11#57
177. `ref-t-not-clone` (sev 3, RV, 3, - (stdlib))
   - Logos: `tests/interactions/min/fi_verify_m39__m1.logos`
   - Rust: `tests/interactions/min/fi_verify_m39__m1.rs`
   - members: 9#36 10#28 14#45
178. `tail-divergence-not-all-paths` (sev 3, RV, 3, S2)
   - Logos: `tests/interactions/min/fi-verify-p27c__min.logos`
   - Rust: `tests/interactions/min/fi-verify-p27c__min.rs`
   - members: 2#44 2#45 8#33
179. `temp-lifetime-for-head-const-promotion` (sev 3, RV, 3, S10)
   - Logos: `tests/interactions/min/fi_verify_m53_2183349__a.logos`
   - Rust: `tests/interactions/min/fi_verify_m53_2183349__a.rs`
   - members: 1#53 7#56 9#53
180. `trait-upcast-only-at-call-args` (sev 3, RV, 3, S4)
   - Logos: `tests/interactions/min/m7v__let_ann.logos`
   - Rust: `tests/interactions/min/m7v__let_ann.rs`
   - members: 1#19 2#34 tt#29
181. `closure-if-without-else-refused` (sev 3, RV, 2, S2)
   - Logos: `tests/interactions/min/fi_verify_m70_own__m.logos`
   - Rust: `tests/interactions/min/fi_verify_m70_own__m.rs`
   - members: 9#41 12#58
182. `copied-fold-closure-param-generic` (sev 3, RV, 2, S7)
   - Logos: `tests/interactions/min/fi_verify_f31__a.logos`
   - Rust: `tests/interactions/min/fi_verify_f31__a.rs`
   - members: 1#52 13#24
183. `derive-lifetime-generic-struct` (sev 3, RV, 2, - (metaprog))
   - Logos: `tests/interactions/min/fi_verify_m29__min.logos`
   - Rust: `tests/interactions/min/fi_verify_m29__min.rs`
   - members: 6#22 12#57
184. `dyn-default-object-lifetime` (sev 3, RV, 2, - (borrowck))
   - Logos: `tests/interactions/min/fi_verify_f10__m.logos`
   - Rust: `tests/interactions/min/fi_verify_f10__m.rs`
   - members: 2#35 5#10
185. `enum-eq-without-impl-accepted` (sev 3, AI, 2, S8)
   - Logos: `tests/interactions/min/fiverify_f37_1Hks__m.logos`
   - Rust: `tests/interactions/min/fiverify_f37_1Hks__m.rs`
   - members: 2#10 5#35
186. `generic-struct-literal-field-inference` (sev 3, RV, 2, S7)
   - Logos: `tests/interactions/min/fi_verify_boxvec__a4.logos`
   - Rust: `tests/interactions/min/fi_verify_boxvec__a4.rs`
   - members: 14#37 6#51
187. `impl-elided-lifetime-vs-trait-signature` (sev 3, RV, 2, S9)
   - Logos: `tests/interactions/min/fi_verify_m89__d.logos`
   - Rust: `tests/interactions/min/fi_verify_m89__d.rs`
   - members: 4#47 tt#21
188. `impl-for-concrete-tuple-parse` (sev 3, RV, 2, - (grammar))
   - Logos: `tests/interactions/min/verify-m14-2149979__a.logos`
   - Rust: `tests/interactions/min/verify-m14-2149979__a.rs`
   - members: 5#14 10#34
189. `impl-trait-nested-in-type` (sev 3, RV, 2, S9)
   - Logos: `tests/interactions/min/fiv-p07e__m4.logos`
   - Rust: `tests/interactions/min/fiv-p07e__m4.rs`
   - members: 2#43 12#33
190. `nested-unit-variant-type-arg-inference` (sev 3, RV, 2, S7)
   - Logos: `tests/interactions/min/y3b__n6.logos`
   - Rust: `tests/interactions/min/y3b__n6.rs`
   - members: 9#32 11#36
191. `operator-impl-for-ref-self` (sev 3, RV, 2, S8)
   - Logos: `tests/interactions/min/neg_ref__a.logos`
   - Rust: `tests/interactions/min/neg_ref__a.rs`
   - members: 2#47 9#45
192. `operator-via-generic-impl` (sev 3, RV, 2, S8+S9)
   - Logos: `tests/interactions/min/fi_verify_m19_2232711__c.logos`
   - Rust: `tests/interactions/min/fi_verify_m19_2232711__c.rs`
   - members: 3#29 tt#20
193. `primitive-inherent-methods-missing` (sev 3, RV, 2, S8)
   - Logos: `tests/interactions/min/fiv_m64_w8k5__m.logos`
   - Rust: `tests/interactions/min/fiv_m64_w8k5__m.rs`
   - members: 12#51 tt#41
194. `private-imported-name-shadows-local` (sev 3, RV, 2, S8)
   - Logos: `tests/interactions/min/fi_verify_f19__m9.logos`
   - Rust: `tests/interactions/min/fi_verify_f19__m9.rs`
   - members: 7#55 13#37
195. `question-in-closure-attributed-to-fn` (sev 3, RV, 2, S10)
   - Logos: `tests/interactions/min/fi_verify_f55b__m1.logos`
   - Rust: `tests/interactions/min/fi_verify_f55b__m1.rs`
   - members: 9#49 13#25
196. `refutable-subpattern-in-struct-field` (sev 3, RV, 2, S3)
   - Logos: `tests/interactions/min/fi_verify_m57c__mn7.logos`
   - Rust: `tests/interactions/min/fi_verify_m57c__mn7.rs`
   - members: 10#55 3#47
197. `slice-debug-missing` (sev 3, RV, 2, - (stdlib))
   - Logos: `tests/interactions/min/f49iQ7D__m1.logos`
   - Rust: `tests/interactions/min/f49iQ7D__m1.rs`
   - members: 12#53 13#40
198. `tuple-index-no-autoderef` (sev 3, RV, 2, S6)
   - Logos: `tests/interactions/min/fiv_m137__min.logos`
   - Rust: `tests/interactions/min/fiv_m137__min.rs`
   - members: 1#43 9#55
199. `tuple-ord-missing` (sev 3, RV, 2, S9)
   - Logos: `tests/interactions/min/fiverify-ord-242g__tord.logos`
   - Rust: `tests/interactions/min/fiverify-ord-242g__tord.rs`
   - members: 12#50 tt#38
200. `vec-repeat-form` (sev 3, RV, 2, R0)
   - Logos: `tests/interactions/min/fi_verify_f50__min.logos`
   - Rust: `tests/interactions/min/fi_verify_f50__min.rs`
   - members: 5#48 14#53
201. `zip-bogus-param-outlives` (sev 3, RV, 2, S9)
   - Logos: `tests/interactions/min/fi_verify_a3zip__minimal.logos`
   - Rust: `tests/interactions/min/fi_verify_a3zip__minimal.rs`
   - members: 1#30 11#32
202. `as-mut-underscore-cast` (sev 3, RV, 1, S7)
   - Logos: `tests/interactions/min/fi_verify_f62__m4.logos`
   - Rust: `tests/interactions/min/fi_verify_f62__m4.rs`
   - members: 13#43
203. `assoc-const-refers-const-param` (sev 3, RV, 1, S8)
   - Logos: `tests/interactions/min/fiv_acg__a1.logos`
   - Rust: `tests/interactions/min/acg__a1.rs`
   - members: 14#51
204. `assoc-type-path-in-expr` (sev 3, RV, 1, - (grammar)+S9)
   - Logos: `tests/interactions/min/fi_verify_assoc_path__n1.logos`
   - Rust: `tests/interactions/min/fi_verify_assoc_path__n1.rs`
   - members: 14#52
205. `box-dyn-fn-not-fn` (sev 3, RV, 1, - (stdlib))
   - Logos: `tests/interactions/min/fiverify_boxfn.xBUH__m1.logos`
   - Rust: `tests/interactions/min/fiverify_boxfn.xBUH__m1.rs`
   - members: 1#39
206. `box-no-clone` (sev 3, RV, 1, - (stdlib))
   - Logos: `tests/interactions/min/fiverify_boxclone.9RSf__m1.logos`
   - Rust: `tests/interactions/min/fiverify_boxclone.9RSf__m1.rs`
   - members: 6#38
207. `closure-captured-mut-param-needs-mut` (sev 3, RV, 1, S10)
   - Logos: `tests/interactions/min/u24a.9qHN__a.logos`
   - Rust: `tests/interactions/min/u24a.9qHN__a.rs`
   - members: 3#50
208. `collect-into-result-option` (sev 3, RV, 1, S7)
   - Logos: `tests/interactions/min/verify_u25a_xUkc__min.logos`
   - Rust: `tests/interactions/min/verify_u25a_xUkc__min.rs`
   - members: 4#50
209. `const-in-fn-body-parse` (sev 3, RV, 1, - (grammar))
   - Logos: `tests/interactions/min/fi_verify_localconst_diEP__a.logos`
   - Rust: `tests/interactions/min/fi_verify_localconst_diEP__a.rs`
   - members: 14#67
210. `custom-iterator-box-dyn-item-defaults` (sev 3, RV, 1, - (borrowck))
   - Logos: `tests/interactions/min/fi_verify_m05h__f.logos`
   - Rust: `tests/interactions/min/fi_verify_m05h__f.rs`
   - members: 12#35
211. `default-method-self-static-generic-impl` (sev 3, RV, 1, S8)
   - Logos: `tests/interactions/min/fi_verify_u01m__d.logos`
   - Rust: `tests/interactions/min/fi_verify_u01m__d.rs`
   - members: 1#24
212. `derive-ord-enum-self` (sev 3, RV, 1, S9)
   - Logos: `tests/interactions/min/fiv_m50__g3.logos`
   - Rust: `tests/interactions/min/fiv_m50__f4.rs`
   - members: tt#37
213. `derive-raw-pointer-field` (sev 3, RV, 1, S8)
   - Logos: `tests/interactions/min/fi_verify_m29d__min.logos`
   - Rust: `tests/interactions/min/fi_verify_m29d__min.rs`
   - members: 12#56
214. `dyn-assoc-binding-parse` (sev 3, RV, 1, - (grammar))
   - Logos: `tests/interactions/min/fi_verify_dynassoc.ElKa__m1.logos`
   - Rust: `tests/interactions/min/fi_verify_dynassoc.ElKa__m1.rs`
   - members: 1#21
215. `dyn-iterator-not-object-safe` (sev 3, RV, 1, S9a)
   - Logos: `tests/interactions/min/u05b_dyniter__dyn_iter.logos`
   - Rust: `tests/interactions/min/u05b_dyniter__dyn_iter.rs`
   - members: 1#22
216. `enum-second-default-method` (sev 3, RV, 1, S9)
   - Logos: `tests/interactions/min/fiv_p03v__th.logos`
   - Rust: `tests/interactions/min/fiv_p03v__th.rs`
   - members: 6#34
217. `float-exp-format` (sev 3, MC, 1, - (stdlib rt))
   - Logos: `tests/interactions/min/fiv_u27a_W1dr__n.logos`
   - Rust: `tests/interactions/min/fiv_u27a_W1dr__n.rs`
   - members: 7#28
218. `from-bound-ignored-by-question-into` (sev 3, RV, 1, S10)
   - Logos: `tests/interactions/min/fi-verify-p02h__m1.logos`
   - Rust: `tests/interactions/min/fi-verify-p02h__m1.rs`
   - members: 2#38
219. `generic-enum-method-no-t-inference` (sev 3, RV, 1, S8)
   - Logos: `tests/interactions/min/fi_verify_y6i_2245997__c2.logos`
   - Rust: `tests/interactions/min/fi_verify_y6i_2245997__c2.rs`
   - members: 11#34
220. `generic-eq-moves-operands` (sev 3, RV, 1, S8)
   - Logos: `tests/interactions/min/fi_verify_f09c__m1.logos`
   - Rust: `tests/interactions/min/fi_verify_f09c__m1.rs`
   - members: 13#18
221. `hashmap-iter-zippair` (sev 3, RV, 1, - (stdlib))
   - Logos: `tests/interactions/min/hm.oA2W__hm_min.logos`
   - Rust: `tests/interactions/min/hm.oA2W__hm_min.rs`
   - members: 8#53
222. `index-clone-keeps-borrow` (sev 3, RV, 1, - (borrowck))
   - Logos: `tests/interactions/min/fiv-p21f-itQw__q1.logos`
   - Rust: `tests/interactions/min/fiv-p21f-itQw__q1.rs`
   - members: 6#39
223. `iter-mut-item-enum-method-receiver` (sev 3, RV, 1, S8)
   - Logos: `tests/interactions/min/fiverify_m92.f8Vk__min.logos`
   - Rust: `tests/interactions/min/fiverify_m92.f8Vk__min.rs`
   - members: 4#38
224. `let-annotation-flows-into-receiver` (sev 3, RV, 1, S7)
   - Logos: `tests/interactions/min/fi_verify_m32__a1.logos`
   - Rust: `tests/interactions/min/fi_verify_m32__a1.rs`
   - members: 7#38
225. `method-generic-drops-impl-bound` (sev 3, RV, 1, S9)
   - Logos: `tests/interactions/min/fi-verify-p02m__m2.logos`
   - Rust: `tests/interactions/min/fi-verify-p02m__m2.rs`
   - members: 2#39
226. `move-closure-self-field-through-ref` (sev 3, RV, 1, S10)
   - Logos: `tests/interactions/min/fi_verify_m18__min.logos`
   - Rust: `tests/interactions/min/fi_verify_m18__min.rs`
   - members: tt#22
227. `move-dyn-out-of-box-accepted` (sev 3, AI, 1, S9)
   - Logos: `tests/interactions/min/r20v__m1.logos`
   - Rust: `tests/interactions/min/r20v__m1.rs`
   - members: 9#29
228. `mul-default-rhs-duplicate-output` (sev 3, RV, 1, S9)
   - Logos: `tests/interactions/min/fiv_m134__b1.logos`
   - Rust: `tests/interactions/min/fiv_m134__b1.rs`
   - members: 9#46
229. `mut-slice-pattern-binds-shared` (sev 3, RV, 1, S3)
   - Logos: `tests/interactions/min/z19v__min.logos`
   - Rust: `tests/interactions/min/z19v__min.rs`
   - members: 11#49
230. `no-vtable-str-literal-to-dyn` (sev 3, CC, 1, S4+S9)
   - Logos: `tests/interactions/min/u6b__m.logos`
   - Rust: `tests/interactions/min/u6b__m.rs`
   - members: 4#27
231. `null-mut-generic-field` (sev 3, RV, 1, S7)
   - Logos: `tests/interactions/min/fi_verify_nullmut__m4.logos`
   - Rust: `tests/interactions/min/fi_verify_nullmut__m4.rs`
   - members: 12#38
232. `option-combinators-take-fn-pointers` (sev 3, RV, 1, - (stdlib))
   - Logos: `tests/interactions/min/fi_verify_optcomb__opt_closure_min.logos`
   - Rust: `tests/interactions/min/fi_verify_optcomb__opt_closure_min.rs`
   - members: 8#51
233. `partial-move-array-len-accepted` (sev 3, AI, 1, S5)
   - Logos: `tests/interactions/min/fi-verify-p19d__m.logos`
   - Rust: `tests/interactions/min/fi-verify-p19d__m.rs`
   - members: 2#15
234. `qualified-path-generic-args-parse` (sev 3, RV, 1, - (grammar))
   - Logos: `tests/interactions/min/fi_verify_m141__min.logos`
   - Rust: `tests/interactions/min/fi_verify_m141__min.rs`
   - members: 9#56
235. `range-in-payload-exhaustiveness` (sev 3, RV, 1, S3)
   - Logos: `tests/interactions/min/fiverify_z7_KUt9__m1.logos`
   - Rust: `tests/interactions/min/fiverify_z7_KUt9__m1.rs`
   - members: 11#48
236. `raw-mut-dyn-reborrow-typing` (sev 3, RV, 1, S4)
   - Logos: `tests/interactions/min/fi_verify_m60__a.logos`
   - Rust: `tests/interactions/min/fi_verify_m60__a.rs`
   - members: 9#51
237. `refcell-replace-noncopy` (sev 3, RV, 1, - (stdlib))
   - Logos: `tests/interactions/min/rcrep__rc_min.logos`
   - Rust: `tests/interactions/min/rcrep__rc_min.rs`
   - members: 7#52
238. `rest-pattern-partial-move-refused` (sev 3, RV, 1, S3+S5)
   - Logos: `tests/interactions/min/fiverify_p15m__min.logos`
   - Rust: `tests/interactions/min/fiverify_p15m__min.rs`
   - members: 2#52
239. `returned-generic-impl-fn-call-void` (sev 3, RV, 1, S7)
   - Logos: `tests/interactions/min/fi_verify_m17__b1.logos`
   - Rust: `tests/interactions/min/fi_verify_m17__b1.rs`
   - members: tt#23
240. `rpit-capture-mut-param-unenforced` (sev 3, AI, 1, S9)
   - Logos: `tests/interactions/min/fi_verify_rpit_mut__m.logos`
   - Rust: `tests/interactions/min/fi_verify_rpit_mut__m.rs`
   - members: 14#24
241. `string-methods-not-via-autoderef` (sev 3, RV, 1, - (stdlib))
   - Logos: `tests/interactions/min/fi_verify_strauto.s6sh__string_str_method.logos`
   - Rust: `tests/interactions/min/fi_verify_strauto.s6sh__string_str_method.rs`
   - members: 7#60
242. `tail-match-no-expected-type` (sev 3, RV, 1, S2+S7)
   - Logos: `tests/interactions/min/fi_verify_ag1_1ENT__min.logos`
   - Rust: `tests/interactions/min/fi_verify_ag1_1ENT__min.rs`
   - members: 1#41
243. `tuple-struct-closure-param-pattern-parse` (sev 3, RV, 1, - (grammar))
   - Logos: `tests/interactions/min/p18c.UKyX__case_c.logos`
   - Rust: `tests/interactions/min/p18c.UKyX__m.rs`
   - members: 2#55
244. `vec-iter-find-variance` (sev 3, RV, 1, - (borrowck))
   - Logos: `tests/interactions/min/fi_verify_m24c__min.logos`
   - Rust: `tests/interactions/min/fi_verify_m24c__min.rs`
   - members: 12#41
245. `implicit-integer-widening-accepted` (sev 4, AI, 5, - (policy))
   - Logos: `tests/interactions/min/fiv_r27b__m.logos`
   - Rust: `(inline in verdict)`
   - members: 1#59 5#54 11#19 12#27 14#22
246. `recursive-rpit-refused` (sev 4, RV, 1, S9)
   - Logos: `tests/interactions/min/fiv_f19rec_bPFo__f19min.logos`
   - Rust: `tests/interactions/min/fiv_f19rec_bPFo__f19m.rs`
   - members: 5#19
247. `struct-literal-in-scrutinee-accepted` (sev 4, AI, 1, - (grammar))
   - Logos: `tests/interactions/min/fiv_u14r1_UAEZ__m5.logos`
   - Rust: `tests/interactions/min/fiv_u14r1_UAEZ__m5.rs`
   - members: 3#21

## 4. Clusters duplicating existing rows

| cluster | size | sev | existing row | note |
|---|---|---|---|---|
| `int-literal-let-defaulted-early` | 7 | 3 | `let_tuple_literal_width_from_later_use_refused` | scalar variants (compound assign, narrowing to u8) not covered by the row's tuple program; widen the row |
| `derive-repeat-group-no-cursor` | 9 | 3 | `derive_clone_nonprimitive_field_refused` | root is quote REPEAT_GROUP over 0/1 elements, not a heap field; retitle the row |
| `nested-pattern-in-variant-payload-unsupported` | 9 | 3 | `enum_payload_nested_pattern_absent (unrowed_backlog)` | row is in `unrowed_backlog.ledger`, not `soundness_queue.ledger` |
| `move-closure-capture-shares-slot` | 7 | 1 | `loop_local_move_closure_shares_slot_wrong` (partial) | representatives disagree on duplication: pairs-10#3 (Box<dyn Fn> env left in callee frame) = dup; pairs-12#9 (aggregate Copy capture aliases source, no loop) = new. Counted as NEW |

Pair-level K verdicts (not clusters): `hashmap_str_key_borrow_escapes_admitted`, `bug_boxed_closure_runtime_garbage` (off-ledger), `loop_local_move_closure_shares_slot_wrong` (x2), `derive_default_nonpod_field_refused`. Known rows also hit incidentally: `labeled_block_expr_refused`, `assignment_as_expression_refused`, `range_pattern_const_bound_refused`, `custom_iterator_item_assoc_type_refused`, `iterator_trait_item_assoc_type_refused`, `iter_filter_capturing_closure_refused`, `let_else_nondiverging_else_admitted`, `range_not_generic_refused`, `closure_param_type_from_usage_refused`, `byte_char_literal_refused`, `sized_supertrait_blocks_impl_refused`, `closure_move_capture_drop_delayed_wrong`, `fnonce_generic_consume_skips_capture_drop`, `vec_literal_elem_type_ignores_call_site_wrong`, `generic_trait_multi_impl_dispatch_refused`, `match_guard_temp_drop_timing_wrong`, `constant_promotion_refused`, `let_binding_named_like_const_admitted`.

## 5. Mixed clusters

None by status: every cluster's representatives agreed on real/not-blessed. The only disagreement is on duplication (`move-closure-capture-shares-slot`, §4); triage by splitting it into an env-in-callee-frame part (dup) and an aggregate-capture-aliasing part (new).

Clustering risk: several representatives found a different root than the cluster key (e.g. `slice-pattern-element-offset` -> array literal widths, `ref-binding-mode-refutable-subpattern-runtime` -> tuple literal behind `&`, `nested-fn-codegen-miscompile` -> closure-call arg expected type, `iter-over-array-of-arrays-element-copy` -> payload-bound `&[T;N]` indexing, `fnonce-generic-capture-leak` -> tail temporaries). Members of those clusters inherit a verdict re-established for a different mechanism; re-run each member against the representative's minimal form before closing.

## 6. Hot spots

Feature participation in NEW clusters (a cluster tagged with 2-3 features by mechanism, not by the pair it was found in):

| feature | clusters | findings | sev-1 clusters |
|---|---|---|---|
| F7 refs/lifetimes | 36 | 140 | 13 |
| F1 generics | 35 | 102 | 10 |
| F6 closures | 34 | 128 | 12 |
| F16 iterators/for | 32 | 135 | 14 |
| F3 dyn Trait | 30 | 104 | 6 |
| F15 smart pointers | 25 | 102 | 6 |
| F12 tuples/arrays/slices | 25 | 104 | 12 |
| F8 match patterns | 24 | 83 | 13 |
| F20 control flow as expr | 21 | 79 | 11 |
| F29 type inference | 21 | 86 | 5 |
| F13 ownership/moves | 19 | 78 | 10 |
| F23 collections | 19 | 79 | 6 |
| F28 derive | 19 | 76 | 6 |
| F10 enums w/ payloads | 19 | 61 | 9 |
| F21 ints/casts/char/bool | 19 | 61 | 13 |
| F14 Drop | 18 | 62 | 10 |
| F22 strings/format | 18 | 80 | 6 |
| F18 operator overloading | 18 | 52 | 6 |
| F19 methods/autoderef | 16 | 44 | 4 |
| F11 structs/tuple structs | 16 | 53 | 2 |
| F17 Option/Result/? | 15 | 60 | 3 |
| F25 const generics/consts/statics | 15 | 57 | 6 |
| F2 traits | 13 | 36 | 1 |
| F5 assoc types/consts | 12 | 36 | 3 |
| F27 unsafe/raw ptrs | 11 | 16 | 5 |
| F4 impl Trait | 11 | 54 | 3 |
| F24 shadowing/nested fns | 8 | 26 | 2 |
| F26 recursion | 6 | 13 | 0 |
| F9 if-let/while-let/let-else | 4 | 11 | 1 |

ADR 0030 step load (a cluster naming two steps counts for both):

| step | clusters | findings | sev-1 clusters |
|---|---|---|---|
| R0 | 10 | 55 | 2 |
| S1 | 7 | 38 | 7 |
| S2 | 24 | 84 | 13 |
| S3 | 16 | 64 | 8 |
| S4 | 18 | 70 | 8 |
| S5 | 13 | 25 | 7 |
| S6 | 8 | 24 | 5 |
| S7 | 44 | 167 | 14 |
| S8 | 29 | 78 | 6 |
| S9 | 41 | 144 | 11 |
| L0 | 2 | 3 | 2 |
| S9a | 1 | 1 | 0 |
| S10 | 21 | 71 | 8 |
| - (outside ADR 0030) | 47 | 129 | 5 |

Mapping to the duplicated-path rules of the path inventory §2 (clusters whose mechanism sits in a ranked rule):

| inv # | rule | clusters | findings | clusters |
|---|---|---|---|---|
| 3 | TINF expected type flows down | 12 | 53 | `format-in-value-position-leaks`, `vec-macro-requires-copy`, `ref-binding-mode-refutable-subpattern-runtime`, `const-generic-struct-literal-error-type`, `expected-type-not-reaching-literal-in-ctor`, `tail-match-no-expected-type`, `let-annotation-flows-into-receiver`, `collect-sum-target-from-return-type`, `collect-into-result-option`, `empty-array-literal-turbofish-slice`, `null-mut-generic-field`, `generic-struct-literal-field-inference` |
| 17 | MOVE scope exit drops every live local | 10 | 47 | `tail-return-owned-double-drop`, `fn-tail-temporaries-not-dropped`, `rust2024-temporary-scopes`, `temp-receiver-or-place-base-not-dropped`, `closure-byvalue-param-not-dropped`, `assoc-projection-never-dropped`, `into-iter-adapter-leaks-source`, `generic-struct-byvalue-never-dropped`, `struct-update-tail-no-drop-base`, `fnonce-generic-capture-leak` |
| 8 | TINF generic args inferred jointly | 9 | 43 | `const-generic-struct-literal-error-type`, `const-N-not-inferred-from-array`, `generic-inference-through-deref-coercion`, `generic-callable-param-to-adapter`, `generic-typevar-leaks-to-codegen`, `closure-param-from-generic-bound-sibling`, `nested-unit-variant-type-arg-inference`, `generic-struct-update-infers-nothing`, `generic-enum-method-no-t-inference` |
| 1 | NAME method-call probe | 8 | 21 | `derive-raw-pointer-field`, `smart-pointer-inherent-method-shadows-pointee`, `box-dyn-as-ref-missing`, `no-vtable-box-box-dyn`, `byvalue-trait-method-via-ref-copy-generic`, `primitive-inherent-methods-missing`, `slice-array-as-ptr-typing`, `iter-mut-item-enum-method-receiver` |
| 2 | TINF literal adopts expected int type | 8 | 27 | `nested-fn-codegen-miscompile`, `slice-pattern-element-offset`, `aggregate-literal-elements-mixed-width`, `const-static-array-init-ignores-declared-type`, `out-of-range-literal-accepted`, `shift-lhs-literal-ignores-expected-type`, `literal-lhs-of-rem-typed-signed`, `u16-mul-overflow-unchecked` |
| 26 | NAME binary op -> trait method | 7 | 16 | `partialord-marker-no-lt`, `generic-eq-moves-operands`, `enum-eq-without-impl-accepted`, `operator-on-enum-lowered-as-integer`, `dyn-eq-operator-builtin`, `operator-impl-for-ref-self`, `operator-via-generic-impl` |
| 45 | TINF closure params from expected callable | 7 | 30 | `closure-param-from-expected-fn-signature`, `closure-ref-pattern-param-not-peeled`, `closure-param-from-generic-bound-sibling`, `untyped-closure-param-from-later-call-wrong`, `untyped-closure-param-codegen-crash`, `copied-fold-closure-param-generic`, `returned-generic-impl-fn-call-void` |
| 9 | MOVE by-value use records a move | 6 | 7 | `generic-eq-moves-operands`, `assoc-projection-move-double-free`, `partial-move-array-len-accepted`, `move-out-of-box-in-match-uaf`, `byvalue-method-through-box-field-double-free`, `generic-param-moved-into-ctor-double-drop` |
| 38 | GEN impl identity / vtable slots | 6 | 14 | `derive-ord-enum-self`, `box-dyn-drop-glue-missing`, `box-dyn-represented-as-ref-dyn`, `generic-instance-key-collision`, `enum-second-default-method`, `generic-enum-method-duplicate-mangling` |
| 23 | TINF unsuffixed literal default | 5 | 13 | `slice-pattern-element-offset`, `aggregate-literal-elements-mixed-width`, `no-vtable-integer-literal-to-dyn`, `integer-placeholder-in-tuple-to-codegen`, `const-expr-fmt-arg-uninit` |
| 27 | COER branch LUB | 5 | 16 | `if-arms-unsize-miscompiled`, `branch-arms-not-unsized`, `loop-break-value-inference-wrong`, `break-none-after-break-some-refused`, `break-value-type-mismatch-accepted` |
| 37 | GEN deferred bound obligations | 5 | 13 | `operator-method-on-generic-instance-not-emitted`, `assoc-type-equality-unchecked`, `generic-body-checked-per-instantiation`, `bounded-generic-impl-method-vanishes`, `method-generic-drops-impl-bound` |
| 47 | CF break value | 5 | 18 | `break-aggregate-value-lost`, `loop-break-value-inference-wrong`, `break-none-after-break-some-refused`, `break-value-type-mismatch-accepted`, `break-value-operand-treated-as-move` |
| 6 | COER unsize to dyn | 4 | 16 | `box-dyn-struct-field-unsize`, `unsize-in-aggregate-generic-ctor-slot`, `sema-missing-trait-check-before-vtable`, `no-vtable-str-literal-to-dyn` |
| 11 | GEN does C implement Tr | 4 | 17 | `structural-copy-clone-not-trait-impls`, `operator-method-on-generic-instance-not-emitted`, `derive-copy-heap-field-accepted`, `tuple-ord-missing` |
| 30 | CF returned value judged identically | 4 | 29 | `tail-return-owned-double-drop`, `format-in-value-position-leaks`, `nested-match-as-block-tail-value`, `match-on-deref-mut-raw-param` |
| 35 | GEN assoc-type projection | 4 | 26 | `const-generic-array-length-unbound-in-body`, `assoc-type-binding-not-normalized`, `assoc-type-equality-unchecked`, `assoc-type-path-in-expr` |
| 40 | TINF closure env escape | 4 | 16 | `borrowed-closure-literal-arg`, `move-closure-capture-shares-slot`, `fnonce-closure-treated-copy`, `move-closure-returning-capture-double-drop` |
| 43 | COER ref/raw-pointer coercions | 4 | 8 | `literal-behind-ref-coercion-built-i32`, `shared-ref-to-mut-ptr-accepted`, `raw-mut-dyn-reborrow-typing`, `rangeinclusive-u8-contains` |
| 10 | MOVE CFG join + drop flags | 3 | 5 | `tail-match-conditional-move-leaks`, `loop-conditional-move-drop-flags`, `closure-conditional-move-drop-flag-ice` |
| 14 | NAME UFCS resolves to trait impl | 3 | 12 | `generic-static-trait-call-not-emitted`, `default-method-self-static-generic-impl`, `trait-static-call-self-inferred` |
| 15 | NAME &mut self needs mutable receiver | 3 | 11 | `no-implicit-reborrow-of-mut-receiver`, `refmut-method-temp-behind-ref`, `mut-self-method-through-shared-generic-ref` |
| 24 | PAT exhaustiveness | 3 | 18 | `exhaustiveness-skipped-for-ref-scrutinee`, `exhaustiveness-unchecked-infinite-domains`, `range-in-payload-exhaustiveness` |
| 28 | CG return ABI | 3 | 21 | `closure-call-aggregate-arg-abi`, `closure-returns-aggregate-via-stack-alloca`, `closure-param-ref-to-fat-pointer` |
| 31 | CG place address vs value read | 3 | 15 | `closure-param-ref-to-fat-pointer`, `iter-over-array-of-arrays-element-copy`, `tuple-index-no-autoderef` |
| 42 | COER int widening never in place | 3 | 11 | `literal-behind-ref-coercion-built-i32`, `int-widening-through-reference-accepted`, `implicit-integer-widening-accepted` |
| 46 | CF integer range iteration | 3 | 6 | `range-literal-element-type`, `open-range-for-head-parse`, `rangefrom-iterator-empty` |
| 18 | COER coercion-site judgment | 2 | 18 | `deref-coercion-missing-at-coercion-site`, `trait-upcast-only-at-call-args` |
| 19 | CG unsize at coercion sites | 2 | 6 | `box-dyn-struct-field-unsize`, `if-arms-unsize-miscompiled` |
| 20 | CG inline storage type / stride | 2 | 14 | `for-over-ref-vec-enum-stride`, `for-by-value-array-fat-elements` |
| 22 | GEN blanket applicability | 2 | 4 | `blanket-impl-not-found-for-bound`, `box-dyn-fn-not-fn` |
| 25 | PLACE a[i] Index vs IndexMut | 2 | 12 | `range-index-only-slice-array`, `user-index-operand-unchecked` |
| 4 | CG store value into place | 1 | 2 | `vec-swap-tuple-elements` |
| 5 | PLACE plain assignment | 1 | 1 | `reassign-ref-box-dyn-double-free` |
| 7 | NAME unsafe fn call E0133 | 1 | 3 | `raw-deref-unsafe-check-gaps` |
| 12 | PLACE compound assignment | 1 | 1 | `compound-assign-uses-add` |
| 16 | MOVE reassign drop-before-replace | 1 | 2 | `index-assign-no-drop-old` |
| 21 | CF divergence is one fact | 1 | 3 | `tail-divergence-not-all-paths` |
| 32 | PAT arm test | 1 | 7 | `literal-subpattern-under-ref-no-deref` |
| 33 | PLACE place address (mlir) | 1 | 10 | `range-index-only-slice-array` |
| 34 | CG pattern lowering doors | 1 | 6 | `slice-pattern-subpattern-bindings-lost` |
| 36 | NAME overload selection | 1 | 2 | `user-name-collides-with-std-method` |

103 NEW clusters (305 findings) map to no ranked rule: grammar gaps, stdlib/derive handlers, borrow-checker region rules, and single-door codegen defects (§1 of the inventory warns the score under-ranks those).

Reading: the drop/exit family (inv 17, 30, 9, 10; ADR S2+S5) and the literal/expected-type family (inv 2, 3, 8, 23, 45; ADR S7) together hold most sev-1 findings. `tail-return-owned-double-drop` alone (18 findings, all 14 pair units) is the single largest defect; it and `fn-tail-temporaries-not-dropped` both close with S2 (tail -> `EXIT return`). The corpus hides it because pass fixtures almost always write `return x;`.

## 7. Refuted / blessed

No cluster refuted or blessed. Probe-level observations excluded as blessed or spelling, not counted as findings:

- usize -> i64 lengths/indices in Logos twins (blessed).
- RefCell double borrow aborts instead of panicking with 101 (blessed).
- `Mul<i64>` / ops-trait spelling (blessed ops-trait shape).
- `{:?}` of `&[u8]` prints as a string; `String` lacking `contains`/`chars` without `.as_str()` (blessed str = [u8]; `string-methods-not-via-autoderef` kept as borderline).
- `static [0; 8]`, assoc const `W*H` (no const-eval).
- `tagged`, `new` are Logos keywords (spelling); `new` does cause `assoc-fn-path-as-value`.
- `derive_eq`/`derive_partial_eq` attribute spelling, `box_into_raw`/`box_from_raw`, `replace_ref` spellings.
- Structural implicit Copy of raw-pointer structs.
- HashMap iteration order difference vs Rust (Rust itself is random).
- rustc deny-by-default lints (`dangerous_implicit_autorefs`, const OOB index) not enforced; recorded as lint-level only.
- f64 findings recorded only (float Deem policy).

## 8. Method and limits

- Binary: `logosc 0.47.0-preview+main-g3df07251`, 9 commits behind HEAD `c6e603f77` at verification time; no `src/` change in between was checked for the 250 clusters (one verifier confirmed none for its case).
- Oracle: rustc 1.98.1, `--edition 2024`; a Logos verdict of legal needed a linked run (`logosc -o` writes a relocatable object; link as `scripts/run_oracle.py`), valgrind for memory claims.
- Probing: 16 agents (14 pair units, 2 triple units), each writing a program per pair plus rustc-rejected twins; `.../tmp/fi` was an existing file, so probe and verify directories are siblings (`fi_dir`, `fi_pairs-N`, `fi_verify_*`), and some repros live in the session scratchpad or `/home/logos/sandbox`. Scratchpad paths are session-local and noexec; copy repros into the tree before they expire.
- Clustering: 852 findings -> 250 clusters by suspected mechanism; 1-2 representatives re-run per cluster; members inherit the verdict. Member-level claims (exact outputs, kinds) are the probe agents' and are not re-verified. Severity and kind are the representative's; a cluster may contain milder members.
- Matrix X means the probe unit reported at least one finding for that pair; since every cluster is confirmed, X is a confirmed-cluster hit modulo the clustering caveat. Features per cluster in §6 are assigned by mechanism, not inherited from the pair.
- Queue dedup was a grep over `tests/logos/soundness_queue.ledger`, `tests/soundness/open/` and (some verifiers) the other ledgers and the 09-26 parity tickets; spelled-differently rows can be missed.
- Several verifiers accidentally wrote probe files into `/home/logos` or the auto-memory directory and moved them out; stray files remain in `/home/logos` (m103.*, m95.*, p, u7_bad2.logos, f_hashmap_iter_tuple_pattern.logos, pl/pr) and, per one verifier, a rustc binary `r` in the auto-memory directory (verifiers report, not re-checked).
