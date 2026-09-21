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
| S2 | the captures in the type | Send/Sync exactness; #440's remaining carriers |
| S3 | the body as an L-IR function (env parameter, names rewritten) | the lifted function is checked like any other |
| S4 | codegen reads the lifted function; `gen_closure`'s re-derivation goes | L1 + the IR snapshots |
| S5 | BIR: closure bodies checked, per-capture origins replace the one-origin stand-in | the shadow census: `old_only` falls |
| S6 | delete `closure_kind_`, `closure_capture_env_` and the name-keyed workarounds | each deletion with its diff |

⚠ S1 CANNOT BE BISECTED: the UID change and the coercion arm must land in one
commit, or every literal meeting a written closure type stops type-checking.

## Open

O1. **Symbol and instance multiplication.** MEASURED with `--emit-llvm`: a
closure's `type_str` is literally inside the link symbol
(`@"mg$apply_val__g__F__|| -> i64"`), and a two-literal program emits ONE
generic instance today. With per-literal types it emits two. The ABI spec is
NOT affected (`abi/logos.abi` holds no closure types, and stdlib has no
`dyn Fn`), so the blast radius is user-side symbol text and instance count.
Measure the instance growth on the largest corpus program before S1 lands.

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
