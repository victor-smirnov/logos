# ADR 0029. A closure is a type: per-literal identity, captures in the type, and the body as an L-IR function

Status: ACCEPTED as direction (Victor, 2026-09-20: «делаем как в Rust. Заодно
добавим замыкания в L-IR»). S0 is LANDED. Scope: the closure model, from sema's
type minting to the L-IR's function list; the borrow checkers and codegen are
consumers, not subjects. Tracking issue to be filed; slices S0-S6 below.

## Problem

**A closure's type in Logos is its REPRESENTATION, not its identity.**
`SemaChecker::make_closure_type(params, ret)` interns by SIGNATURE, so every
closure literal with the same params and return is literally the same type. That
is deliberate: `dyn Fn*` resolves to `Kind::Closure` too, because (sema.cpp, the
`DYN_TYPE` arm) "the trait-object layout for these matches the existing Closure
type exactly: `{fn_ptr, env_ptr}` for both". Logos therefore has ONE callable
type and it is the ERASED one — every closure is `dyn`-like at the type level.

In Rust a closure literal gets an anonymous NOMINAL type: a compiler-generated
struct whose fields are the captures. The Fn-family is which of Fn/FnMut/FnOnce
that type implements. `dyn Fn` is a separate erased form the author opts into.

Because the type carries no identity, facts that belong to a LITERAL live in
side tables keyed by `type_str`, i.e. by the signature:

| table | what it holds | how it is keyed |
|---|---|---|
| `closure_capture_env_` | capture types, for Send/Sync | UNION over same-signature literals |
| `closure_kind_` | the Fn-family | MAX over same-signature literals |
| `closure_kind_by_id_` | the Fn-family, per literal | a STRING closure id |

### What that has cost, measured

- **One literal's verdict answered for another.** `apply_val<F: Fn()->i64>(k)`
  was refused — "its body mutates a captured variable" — because an unrelated
  sibling `h` of the same signature mutates one. Deleting `h` admitted the same
  program. The imported witness
  (`unboxed-closures/call-through-ref-to-fn-bound-b158`) had been DISABLED in
  `tests/logos/CMakeLists.txt` with this as its ground, task #111.
- **#440, a live double free.** An `FnOnce` closure in a struct FIELD called
  twice frees its capture twice (`free(): double free detected`, exit 134). It
  reproduces on three carriers, including `f: dyn FnOnce() -> String` and
  `struct H<F> where F: FnOnce()` — Rust's own shape — so the family in the type
  is necessary and not sufficient.
- **#442 / #105**, both closed by S0: a closure in a generic function
  instantiated twice did not compile (`redefinition of symbol '__closure_0'`).
- **The Send/Sync answer came from a SIGNATURE, and it was wrong in BOTH
  directions** (measured 2026-09-20 against the committed binary, and the note
  in `sema_auto_trait.cpp` had claimed a union "cannot admit an unsound answer
  — only a stricter one"). Over-refusal: a `Send` literal beside a
  `*mut`-capturing sibling of the identical signature was refused, and deleting
  the sibling admitted the same program. Unsound admit: `fn take(b: Box<dyn
  Fn() -> i32>) { need_send(b) }` — a box that may hold a closure built in
  another package around a raw pointer — compiled when a trivial `move || x`
  appeared EARLIER in the same file and was refused when it did not, or when
  `take` was merely moved above it. A thread-safety verdict that turns on
  declaration order is not a stricter answer. Both are `tests/logos/{pass,fail}/
  closure_{send_not_from_sibling,dyn_send_needs_own_env}`.
- **The new borrow checker cannot see captures.** `borrow_bir.inc`: "Captures
  are not visible in the type: one origin stands for whatever the closure may
  hold." Every capture loan is indistinguishable once the closure value is
  copied, returned or passed.
- **The body is not a function.** It is an OPEN TERM: it names the enclosing
  scope's variables, and nothing between sema and codegen binds them. Codegen
  closes it at MLIR time by GEP-ing each capture out of the env and writing the
  capture's NAME into its own scope map — and, for a narrow RFC-2229 capture,
  by materialising a fake root struct so the body's `root.field` chain works.
  Most of the new checker's `old_only` disagreements are closure-body
  diagnostics it therefore cannot produce.

## Decisions

D1. **A closure literal's type is its own type.** The identity rides
`const_val` (bits 21.. are free; 0-7 owning kind, 16 RAW_FAT, 18 DYN_MUT_BORROW,
19-20 FnFamily are taken) and enters `compute_type_uid`'s `case K::Closure:`,
which today hashes params and ret alone. `builder_equals_typeref` already
compares `const_val`, so the pool entries exist; only the UID collapses them.

D2. **The erased form stays, and becomes a COERCION.** `dyn Fn*` and a written
`|T| -> R` keep minting the identity-less type. A literal reaching such a slot
is an unsize-style coercion, which `types_compatible` must gain: it has NO
Closure→Closure arm today, and the 216 written closure-type surfaces in the
corpus meet literals only because the UID collapses them.

D3. **The type carries the captures.** This is what "as in Rust" means: the
closure type IS the env struct. It is what buys per-capture origins in BIR,
exact Send/Sync (the union degenerates to a singleton), and #440's remaining
carriers.

D4. **The body becomes an L-IR function**, with the env as its first parameter
and the free names rewritten to projections of it — the step codegen performs
today by name. Both checkers then see closure bodies for free.

D7. **The erased `dyn Fn*` STATES ITS FAMILY IN THE TYPE too** (2026-09-21).
D1 put a literal's family in `const_val`; a `dyn Fn*` kept stating it only as
the string in `trait_name`, so every reader that wanted the family compared a
bare entity name. Five such ladders entered the compiler during this arc and one
went stale, which is how #440's Rust-shaped carrier was called twice. There is
ONE spelling-to-family step now, at the `DYN_TYPE` mint where the spelling is
read off the syntax, and `closure_fn_family()` is the only reader. ⚠ It makes D2's
coercion arm LIVE for `dyn`: a slot that STATES a family takes only a literal
that fits it, so an `FnMut` literal into `dyn Fn` is refused, as in Rust. The
BOUND population is NOT consolidated by this — a bound's family is still its
trait NAME, in four hand copies, and #438's per-bound `trait_def` DefId is the
repair when someone takes it.

D5. **A bare `|T| -> R` DECLARATION is NOT decided here.** Reading it as `Fn`
was implemented and REFUTED: it refuses a legal program through an explicit type
ARGUMENT (`apply_n::<|| -> i64>(bump, 5)`), where the family cannot be spelled
at all. Naming a family in a type-argument position is a grammar question and is
out of this ADR's scope.

## Slices

| id | content | gate |
|---|---|---|
| S0 | a closure id per INSTANTIATION (mono) | LANDED `82ebecfc9`; closes #442, #105 |
| S1 | per-literal type identity: the UID arm, the `types_compatible` erasure arm, the mangler case, `type_str` as `[closure@file:line]` | whole corpus; symbol count on a two-literal program; the 216 written surfaces |
| S2 | the captures in the type | LANDED; two measured Send defects, both directions, now fixtures |
| S3 | the body as an L-IR function (env parameter, names rewritten) | the lifted function is checked like any other |
| S4 | codegen reads the lifted function; `gen_closure`'s re-derivation goes | L1 + the IR snapshots |
| S5 | BIR: closure bodies checked, per-capture origins replace the one-origin stand-in | the shadow census: `old_only` falls |
| S6 | delete `closure_kind_`, `closure_capture_env_` and the name-keyed workarounds | each deletion with its diff |

⚠ S1 CANNOT BE BISECTED: the UID change and the coercion arm must land in one
commit, or every literal meeting a written closure type stops type-checking.

## Open

O1. **ANSWERED, 2026-09-20, and the answer changes S1.** The worry was that
per-literal types multiply generic instances, because a closure's `type_str` is
literally inside the link symbol (`@"mg$apply_val__g__F__|| -> i64"`). Measured
with `--emit-llvm` before touching anything:

- the LARGEST real programs contain NO closures at all: `deem_memoria_showcase`
  1115 defines, 0 closure bodies, 0 closure-typed instances; `writ_showcase` 0;
- `stdlib` holds exactly ONE closure literal (`lang/cmp/ord.logos`) against 40
  Fn-family BOUNDS, so the adapters are declared there and instantiated by user
  code;
- over 60 closure-heavy fixtures: 68 closure bodies against 32 closure-typed
  instances, and the worst per-file sharing is 2 bodies to 1 instance;
- ⚠ AND WHERE CLOSURES MEET THE STDLIB ADAPTERS THE CLOSURE IS NOT IN THE KEY AT
  ALL. `test_harness_coretest_iter_filter`: 6 closure bodies, ONE closure-typed
  instance. The instantiated symbols read
  `FilterIter$G2$CopiedIter$G2$SliceIter$G1$i32$i32$i32__next__g__…` — keyed by
  the ITERATOR and ELEMENT types; the closure is erased out of them.

So the multiplication is bounded by a handful of fixtures at ×2, and it is not
a reason to avoid per-literal types.

**DECISION TAKEN FROM THE MEASUREMENT (D6): the MANGLER IS NOT CHANGED.** The
identity exists for TYPE CHECKING; the SYMBOL stays keyed by the representation
(the signature), exactly as today. That is sound here and not in Rust for a
reason that is specific to Logos: a closure value is already a fat pair
`{fn_ptr, env_ptr}` whose LAYOUT does not depend on its captures, so a generic
instance shared by two same-signature literals is still correct — the call goes
through the pointer. Rust monomorphises per closure type because its closure IS
the env struct, by value. Keeping the mangler unchanged removes O1 entirely and
takes symbol churn out of S1.

O2. **How much precision S5 actually buys.** Two attempts to construct a
program where the one-origin stand-in over-refuses both FAILED — the closure
call's `sig_to_result = ~1` means the call's result inherits no capture loans at
all. The imprecision may be largely dormant. ⚠ The converse is the live
question: the OLD checker tracks captures precisely (its RFC-2229 arm) and S7 of
ADR 0028 deletes it, so measure what is LOST at that point, not what is gained
here.

O3. **Closure `DefId`s.** `def_table.hpp`'s `DefKind` has no anonymous kind and
`intern` needs a name. Rust's scheme is a `{closure#N}` child path under the
enclosing item. Whether S1's identity is a DefId or a dense index is decided by
what S3 needs for the lifted function's name.
