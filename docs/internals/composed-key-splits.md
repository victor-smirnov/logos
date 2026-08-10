# `::` splits in the compiler — the 12 sites, classified

`tests/logos/separator_split_lint.sh` censuses `__` splits. It deliberately does **not** census `::`;
this file is why, and it is the per-site read that the lint's comment defers to.

## Why `::` is a different risk from `__`

`__` is legal *inside* a Logos identifier, so `name.find("__")` is a guess about text — censusable,
because the guess is visible in the source line.

`::` is not legal inside an identifier (MEASURED 2026-08-10: the grammar refuses
`impl pkg::Trait for T` with `syntax error near 'impl'`, rc 4). A `::` split is therefore never a
guess about *text*; it is a bet about **composition** — that nothing ever put a *already-qualified*
name into the operand. That bet is a whole-program property. `Mono::concrete_impls_` held exactly
this bet (`k.find("::")` over `"trait::type"`, guarded by a comment asserting the left operand is a
bare `impl.trait_name()`); canonicalising trait identity would have put `pkg::Hash` there, silently.
A per-site regex cannot see a whole-program property. A per-site read can, and a check at the
**composition** can.

## Classification: all 12 matches of `\.(find|rfind)\("::"\)` in `src/compiler/`

Left operand's provenance is the classification; the enclosing symbol is named so the site can be
found without a line number.

| # | file · enclosing symbol | operand | cut | class |
|---|---|---|---|---|
| 1 | sema.cpp · `SemaCache::reset_user_state`, lambda `erase_pkg_key` | key of `structs`/`datatypes`/`enums`/`type_aliases`/`module_consts`/`module_const_values`/`generic_consts`/`traits`/`explicit_type_codes` | **first** | PATH PARSE (pkg prefix) |
| 2 | sema.cpp · `SemaChecker::take_snapshot`, lambda `erase_pkg_key` | same nine maps | **first** | PATH PARSE (pkg prefix) |
| 3 | sema.cpp · `SemaChecker::install_snapshot`, const-index rebuild | `module_consts_` key | **last** | PATH PARSE (pkg prefix) |
| 4 | sema.cpp · `SemaChecker::run`, lambda `bare_of` (G156-1 ambiguous-type set) | `structs_`/`enums_` key | **last** | PATH PARSE |
| 5 | sema.cpp · `SemaChecker::run`, `layout::recording_enabled()` sweep | `structs_` key | **last** | PATH PARSE |
| 6 | sema.cpp · `SemaChecker::compute_auto_copy_types` | `structs_` key | **last** | PATH PARSE |
| 7 | sema.cpp · `SemaChecker::compute_auto_copy_types`, StableLayout field check | `structs_` key (inner scan) | **last** | PATH PARSE — but see §"the site the pattern would have missed" |
| 8 | sema.cpp · `SemaChecker::lower_module_items`, instance-annotation type-code mirroring | `ia_canonical` = `cur_package_ + "::" + tname + "<…>"` | **first** | COMPOSED-KEY SPLIT, safe by construction (see below) |
| 9 | sema_collect.cpp · `SemaChecker::collect`, B-at-01 unknown-annotation warning | `datatypes_` key | **last** | PATH PARSE |
| 10 | sema_impl.hpp · `check_recursive_value_types`, `visit_struct` | `structs_` key | **last** | PATH PARSE (diagnostic text only) |
| 11 | sema_impl.hpp · `check_recursive_value_types`, `visit_enum` | `enums_` key | **last** | PATH PARSE (diagnostic text only) |
| 12 | sema_impl.hpp · `resolve_trait_query_name` | `traits_` key | *none* | NOT A SPLIT — `== npos` presence test ("is this key bare?"); no substring taken |

**None of the 12 is a `concrete_impls_`-shaped split**, i.e. none cuts a key whose left half is an
arbitrary *trait* name. Eleven of them cut a key composed by `SemaChecker::sema_key(pkg, name)`
(`sema_impl.hpp`: `r.append(pkg); r.append("::"); r.append(name);`) — the key of `structs_`,
`enums_`, `datatypes_`, `traits_`, `type_aliases_`, `module_consts_`, `generic_consts_` — and the
part they take is the **package**. A package spelling cannot contain `::`:
`SemaChecker::read_package_name` builds it from `la::NAME` plus `la::mod::PATH_PARTS` joined with
`"."`. That is a *local, checkable* reason, not a comment-defended one.

Site 8 is the one genuine composed-key split, and it is the reason the mix of `find` and `rfind` is
not sloppiness: its key is `pkg::Name<Args>` and `<Args>` may itself carry `::`, so the **first** cut
is the only correct one there. Sites 1–2 also use `find` because their maps include
`explicit_type_codes_`, which is keyed that same way.

## The two invariants, and where each is now enforced

The eleven splits do not all need the same thing:

* **first-cut sites (1, 2, 8)** need only: the *package* half carries no `::`.
* **last-cut sites (3, 4, 5, 6, 7, 9, 10, 11)** need more: the key carries **at most one** `::`.
  With two separators the two cuts *disagree* — `find` yields the real package, `rfind` yields
  `pkg::Outer` as the "package" and a fragment as the "bare name". Silently.

Both are now checked, and neither by a comment:

1. `SemaChecker::sema_key` rejects a `pkg` operand that already contains `::` — always-on, aborts
   with an internal-error message. It fires at the *composition*, the event no splitter can see.
2. `SemaChecker::check_symbol_key_separators` (called from `SemaChecker::run` immediately after
   collect, before the first last-cut consumer and before `take_snapshot` carries the tables into the
   next call) rejects any **stored** key in `structs_`, `enums_`, `datatypes_`, `module_consts_` that
   carries more than one `::`. It is a check over the **population**, so it is blind to who composed
   the key — it covers keys built by a raw `+ "::" +` just as well as `sema_key`'s.

Both were proved to bite by planting the defect where it lives (`sema_key(cur_package_ + "::PLANT", sname)`
at the struct-collect site; `structs_["a::b::c"]` before the audit); both aborted with the intended
message, rc 134; both restores re-measured green.

### What was measured and refused: checking the `name` operand too

`sema_key` is **both** the key composer (≈20 collect/insert sites) and the *probe* composer —
`lookup_qualified_` builds a candidate key per package. Rejecting a qualified `name` aborts the
stdlib build immediately:

```
sema_key(pkg='logos.lang.fabric', name='PrimVec$G1$DT::Prim')
  lookup_qualified_ ← find_struct_by_name ← lower_field_read ← … ← SemaChecker::run
```

The probe's `name` is a user-written type spelling, and an un-normalised associated-type projection
legitimately contains `::`. Such a probe never matches and never inserts, so it is harmless — but it
makes "no operand is qualified" **false as a global statement**. That is why the second instrument is
over stored keys rather than over operands, and it is a measurement, not a judgement call.

## The site the pattern would have missed

Site 7's enclosing block (`SemaChecker::compute_auto_copy_types`, StableLayout field check) recovers
the target of an `impls_` key — composed `trait + "::" + target` in `sema_collect.cpp` — with
`ikey.substr(kSlPrefix.size())` after a fixed-prefix test `ikey.rfind(kSlPrefix, 0) != 0`, where
`kSlPrefix = "StableLayout::"`. **That is the `concrete_impls_` class exactly, and it contains zero
occurrences of `find("::")`** — a `find`-keyed lint is blind to it. Its behaviour if the prefix stops
matching (trait identity canonicalised to `pkg::StableLayout`, or `StableLayout` gaining type args)
is *fail-closed*: the loop skips every impl and the field check silently verifies nothing. Not a
miscompile, but a check that goes quiet, which is worse than one that goes red.

This is the concrete argument against re-proposing the blanket `\.(find|rfind)\("::"\)` alternation:
it is simultaneously a **rubber stamp** (12 entries, 11 of them the same safe idiom) and
**incomplete** (blind to the one site in this file set that is actually the dangerous shape).

## How to recognise a dangerous `::` site (the text does not say)

The operand is a key the compiler **built by concatenation**, and the code is recovering a part of
it — by `find`/`rfind`, by fixed-prefix `substr`, **or by suffix compare**. Contrast the safe idiom:
the operand is a `sema_key` and the part taken is the *package*, whose spelling is `.`-joined by
construction.

## `Mono::assoc_impls_` — yes, it should be a tuple key

`mono.cpp` composes `assoc_impls_[impl_trait + targ_sfx + "::" + impl_target + "::" + aname]` (and
mirrors it bare). It is never `find`-split, but it is not therefore safe by anything but luck:
`mono_subst.cpp` matches it with `const std::string tail = "::" + concrete_base + "::" + assoc_type_name`
and a `k.compare(k.size() - tail.size(), …)` suffix compare (plus a `pfx` prefix compare), and sema
does the same over `assoc_type_impls_`. **A suffix compare over a 3-part composed key has the
identical failure mode as a split**: it is correct only while no `concrete_base` is itself qualified
or parameterised — a whole-program invariant nothing checks. A `(trait, concrete, aname)` key deletes
the scan the same way the `std::pair` key deleted both `concrete_impls_` parse sites. Not done here:
it is mono's, and a sibling slice owns mono.
