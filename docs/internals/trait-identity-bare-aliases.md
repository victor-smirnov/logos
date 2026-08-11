# The two bare-spelling aliases in mono, and what each one is worth

Status: **CLOSED 2026-08-10 — both aliases are removed.** Read the final section
first; everything before it is the record of how they were measured, kept
because the two failure modes it documents are the reusable part.

⚠ The body below describes the tree AS IT WAS. Statements in the present tense
("mono files every fact twice", "neither may be removed on its own") were true
when written and are not now. Each superseded claim is marked where it stands.

This file is the record of a measurement, not a design. It exists because both
aliases spent time in the state "green corpus, no witness either way", one was
nearly deleted on that basis, and the other was declared unmeasurable on it.

## THE ROOT THEY BOTH HEDGE (superseded: the root was fixed, see the closing section)

`SemaChecker::collect_impl` (`src/compiler/sema_collect.cpp`) keys `impls_` as
`trait_name + "::" + target` from the **raw spelling**, two lines below where
`canonical_trait_name` is computed for coherence. One key space for two traits.
Everything below is a hedge over that; when the root is canonicalised at emit,
both aliases go **with it**, and the two fixtures named here must be re-derived
against the canonical bound rather than deleted.

Who owns the bare key is decided by collection order. `SemaChecker::collect_trait`
(B-mv-02) keeps the **incumbent** in the bare slot and files the newcomer under
`pkg::Name`. **MEASURED: the stdlib is collected first**, including in a consumer
that links an archive declaring a homonym — linking a package with its own
`trait Any` still leaves the stdlib's `impl<T> Any for T` with
`bare == canon == "Any"`. So the trait that *loses* the bare slot is never the
stdlib's; it is whoever is collected last.

## THE TWO ALIASES

| | site | fixture | verdict |
|---|---|---|---|
| CONCRETE | `Mono::Mono`, impl-indexing loop, `if (impl_canon != impl_trait) concrete_impls_.insert(...)` | `tests/logos/pass/trait_ident_bare_alias_bound.logos` | load-bearing **then**; REMOVED |
| BLANKET | `Mono::populate_trait_engine_`, `if (b_canon != bi.trait_name) trait_engine_.add_blanket(...)` | `tests/logos/pass/trait_blanket_bare_alias_bound.logos` | load-bearing **then**; REMOVED |

Both are consumed through the **same door**: `Mono::mono_has_impl_recursive` is
`return trait_engine_.satisfies(trait_name, concrete_name);` — the trait name is
never canonicalised, and nothing on that path calls
`SemaChecker::resolve_trait_query_name`. They differ only in which kind of
satisfier they file.

## WHY THE BLANKET ALIAS LOOKED UNMEASURABLE

Disabling it (`if (false)`) left all three trait-identity fixtures green,
including the one that reds for the concrete alias. **The mechanism, measured
with a print on every blanket row:** in
`trait_ident_bare_alias_bound` — the homonym-archive program built for this seam —
the whole blanket population is nine rows (`Any`, `Container` x2, `ContainerOrd`
x2, `Datatype` x2, `Into`, `TryInto`) and **every one has `canon == bare`**. The
branch never executes there. Green was not weak evidence; it was no evidence.

Its arm in that fixture that the file's own comment calls "a BLANKET bound",
`impl<A: Hash, B: Hash> Hash for (A, B)`, has a **tuple** target and is keyed
`$tuple$2` in `concrete_impls_` — a concrete fact, never a `blanket_impls_` row.

## THE PROGRAM THAT DOES REACH IT

`tests/logos/pass/trait_blanket_bare_alias_bound.logos` with
`tests/logos/trait_blanket_chain/bmid`. Three properties, each one necessary,
each one measured by removing it:

1. **The losing trait must be declared by a linked package, not the stdlib** —
   see the collection-order result above.
2. **The bounded template must live in the archive.** With the identical trait +
   blanket + `impl<T: Error> Holder<T>` written in the consumer's own file, the
   query still goes to `satisfies("Error","i64") = 0` with the alias off, and the
   program **still compiles and runs correctly**: the method has a second,
   non-worklist instantiation path in its own translation unit.
3. **The biting call must be a void method called as a bare statement.**
   `h.get()` in an expression survives the false negative; `h.store(&mut x);`
   does not.

Path, captured under gdb: `Mono::drain_method_worklist` (`mono_scan.cpp`, the
`if (!method_bound_ok(tmpl, item.subst)) continue;` line) → `Mono::method_bound_ok`
→ `Mono::mono_concrete_satisfies_bound("Error", i64)` →
`Mono::mono_has_impl_recursive` → `trait_engine_.satisfies`. Same gate, same
path as the concrete alias's `HashMap$G2$i64$i64__insert` drop.

Arms, full rebuild each:

```
alias ON   satisfies(Error, i64) = 1   logosc rc 0, program exits 0
alias OFF  satisfies(Error, i64) = 0   logosc rc 1:
  mlir_gen: internal: void call statement DROPPED — the method
  'Holder$Mm23b6613ad003ddcc$G1$i64__store' had no instantiation, so the whole
  call was silently discarded (mono never demanded the callee)
restore    satisfies(Error, i64) = 1   rc 0
```

The perturbation was proven live by a second print **inside** the guarded block,
absent in the OFF arm — an `if (false)` that the compiler kept is otherwise
indistinguishable from a build that did not happen.

## THE HARM ARM BELONGS TO THE ROOT, NOT TO THIS ALIAS

Constructed: a package generic over the **stdlib** `Error`
(`pub fn err_probe<T: Error>(x: T) -> i64 { … x.source() … }`), and a consumer
that declares its own `trait Error` plus `impl<T: Copy> Error for T`.
`err_probe(5i64)` is wrongly admitted and dies at

```
error: 'func.call' op 'i64__source' does not reference a valid function
```

the `i64__tag` shape. **MEASURED IDENTICAL WITH THE BLANKET ALIAS ON AND OFF.**
The control — the same consumer without its local trait — gets the correct
refusal, `'err_probe': type 'i64' does not implement trait 'Error' required by
parameter 'T'`. So the wrong admission happens in sema over the raw-keyed
`impls_`, before mono is asked: it is the root's harm, and the blanket alias is a
**consumer question only**.

Not landed as a fixture: it is red today, and a `fail/` door on
`'i64__source' does not reference a valid function` would be asserting the
defect.

### ⚠ RE-MEASURED AFTER THE MERGE — the paragraph above is superseded

The claim "it is red today" was re-run on the merged tree and did not hold, and
what replaced it was worse than a red. Reconstructed with a body that does NOT
call a trait method (`pub fn err_probe<T: Error>(x: T) -> i64 { return 7i64; }`
in an archive), the consumer that declares its own `trait Error` +
`impl<T: Copy> Error for T` **compiled, rc 0** — no verifier failure to notice,
because nothing had to be instantiated. The wrong ADMISSION was the whole
defect; the MLIR error was only the loudest of its symptoms.

Control, the same consumer with its local trait deleted: **rc 1**, the correct
`'err_probe': type 'i64' does not implement trait 'Error' required by parameter
'T'`. The two arms differ only by the presence of the homonym.

Root, and it is NOT the raw `impls_` key: `blanket_implements` matched a **bare**
query by raw spelling, on the reasoning that a bare query must preserve the
pre-B-mv-03 union for the ~50 bare-text probes. But a query is bare exactly when
the bound's trait **owns the bare slot** — for every stdlib trait, always — so
that arm was the main path, not a legacy corner. Fixed by matching on identity
in both directions (`bi.query_trait() == q`), which costs the bare-text probes
nothing: a blanket whose trait owns the bare slot has `canonical_trait ==
trait_name` and still answers. Landed with a PAIR of fixtures —
`fail/trait_blanket_homonym_bound_refused` and
`pass/trait_blanket_homonym_bound_admits` — because the refusal's message is
byte-identical to what an over-refusing compiler prints, so the fail door cannot
carry the claim alone.

## CONSEQUENCE (superseded — see the closing section)

Neither alias may be removed on its own. Both are false-negative hedges over one
raw key, both now have a fixture that reds when they go, and the harm they were
suspected of is not theirs. They come out **with** the canonicalisation of the
bound trait name, and the two fixtures get re-derived at that time.

## ✅ CLOSED — BOTH ALIASES ARE GONE, AND THEY DID NOT GO TOGETHER

The prediction above got one thing right and one thing wrong. Right: the harm
was not theirs, and the fixtures were re-derived rather than deleted. Wrong:
**"neither may be removed on its own" was an assumption, not a finding.** The
two hedged different things, and measuring them one at a time — with the other
restored and a green checkpoint between — is what separated them.

| | consumers | retired when |
|---|---|---|
| BLANKET | queries that can carry an identity: the blanket's own bound, the eager candidate list, mono_subst's assoc fallback, method_bound_ok via `TraitQuery` | the identity reached mono through LIR (`IDENTITY_BOUND_TRAIT`, `IDENTITY_EXTRA_BOUNDS`, `TB_IDENTITY`) |
| CONCRETE | mono's OWN hardcoded probes — `has_concrete_impl_("Drop", …)`, `"Copy"`, the auto-trait names — text written into the compiler, which can never carry an identity | `Mono::bare_trait_identities_` gave those probes a bare-spelling → identities index, built from `out_.traits` (`name()` + `pkg()`) |

**The index is not the alias renamed.** The alias made the bare key answer for
*everyone*, so an identity query could land on it and read a homonym's impls —
that was the defect, not the hedge. The index is consulted **only** when the
query carries no identity, so a compiler probe still means "some trait spelled
`Drop`" while an identity query reaches exactly one trait.

**How each removal was measured**, because this arc contains both failure modes:
a green suite over a branch that never executed (the blanket alias, previously
called "unmeasurable"), and a deletion on a green suite that shipped a real
regression (the concrete alias). Both were closed with a print **inside** the
guarded block, proving the branch live before believing any green:

```
blanket alias   fires 10  times compiling trait_blanket_bare_alias_bound
concrete alias  fires 475 times compiling trait_ident_bare_alias_bound
                fires 452 times compiling trait_ident_pkg_chain
```

Reached **and** no-op is a measurement. Green over an unproven branch is not.

**Ordering was the content, not a detail.** Narrowing `concrete_has_impl` before
the fact tables were identity-keyed broke the stdlib build
(`Vec$G1$tup$3$slice_u8$i64$slice_u8__fmt`); disabling the concrete alias before
the index existed broke `libsd_dst_mod.a` and reddened `trait_ident_pkg_chain`
with a **wrong answer**, exit 45. Each step only became safe after the previous
one landed.

Both fixtures survive with rewritten headers stating what they pin now. Neither
was deleted: each is still the only corpus member of its shape.
