# ADR 0012 — wql!/trama! parsing on peg_gen_logos + schema-emission action model

**Status:** DESIGN (Phase 1, 2026-06-30) → **Phase 2 GENERATOR CAPABILITY DONE
(2026-06-30)** → **Phase 4 GENERATE + WIRE DONE (2026-07-01)** → **Phase 5
OPTION-B SELF-DESCRIBING IR DONE (2026-06-30)** → **EL SCHEMA-EMISSION GAPS
CLOSED + FULL BYTE-IDENTITY (2026-07-01)** → **Phase 3 TRAMA PARSER FUNCTIONAL
(T1 raw-text lexer + T2 cross-grammar EL embed) DONE (2026-07-01)** → **PARSE
SWAP + HAND-PARSER RETIREMENT DONE (2026-07-01).** Companion to
[`0012-writ-query-language.md`](0012-writ-query-language.md) and
[`0012-impl-seam.md`](0012-impl-seam.md).

## THE SWAP — wql!/trama! on the generated parsers; hand parsers RETIRED (2026-07-01)

The `wql!` / `trama!` handlers now parse via the peg_gen_logos-GENERATED parsers,
and the hand-written recursive-descent bodies are DELETED — the migration's end.

- **EL swap.** `wql.logos` (main + join + aggregate clauses) and `trama_render.logos`
  now call `logos.std.wql.el_parser::parse_expr(src, &h)` instead of
  `ElParser::parse_root`. The ordered `$param` arg-list is recovered by WALKING the
  self-describing IR (`codegen::collect_params`, pre-order; a comprehension SOURCE
  enters first, matching the hand parser's parse-order side channel) — no parser
  side table. `merge_params` deleted.
- **Trama swap.** `trama_render.logos` calls `trama_parser::parse_tpl(src, &h)` →
  a WArray of TStmt handles; the **T3 post-pass** `chain_array` stitches the
  `next`-linked chain the renderer walks (recursively over `TFor.body`/`TIf.body`/
  `TIf.alt`). `TrParser` + its shared-`ElParser` seam are gone; the type dict
  (`ElTypes`) + loop/set var type names (`TVarTys`) are built from schema
  reflection + `with`, exactly like the `wql!` handler.
- **elif → nested TIf, in the GRAMMAR** (not a handler pass): `if_tail` gained a
  recursive `( {% elif %} expr tpl if_tail => TIf )` alt, so the generated parser
  produces the nested-TIf shape directly (the schema-group action + nullable-rule
  handling already supported it). The handler's T3 pass then only next-links.
- **Three generator fixes the swap forced** (all additive / template-or-embed-only,
  so `peg_gen_logos_oracle` stays **2095/2095** byte-identical on logos.peg):
  1. **whitespace-control trims.** A trim-CLOSE (`-%}`/`-}}`) now CONSUMES the
     following text run's leading whitespace in the emitted template lexer
     (`emit_template_literal_ex`); a text run stopping at a trim-OPEN (`{%-`/`{{-`)
     shrinks its recorded `len` past trailing whitespace (`emit_template_raw_text` +
     `template_has_trim_open`). Faithful to the hand `parse_text`.
  2. **embed reset keeps the `-` marker.** The cross-grammar EL embed reset moved
     from `ecl_` (the `}}`/`%}` proper) to `eend_` (AT any `-` trim marker) — else
     `-}}`/`-%}` re-lexed as plain RMUST/RSTMT and the close's trim was lost.
  3. **export rejects trailing input.** `parse_<export>(src, doc)` now returns a
     null root when `!lex.at_eof()` — a PEG rule can partial-match (`expr` matches
     `a` in `a +`); the hand parsers rejected trailing junk, so a non-EOF remainder
     is a parse error.
- **Retired:** the `ElParser` RD (el.logos) + `TrParser` RD (trama.logos) bodies +
  their `mk_*`/lexer/`parse_el`/`parse_trama`/`parse_block`/`parse_embedded_el`
  helpers. KEPT: `OP_*`, `EL_TY_*`/`el_ty_of_name`, `ElTypes`, the `TStmt` schema
  family + accessors, `reflect.logos`, `codegen.logos`.
- **GATES GREEN:** full `cmake --build`; the wql|trama e2e behavior specs pass
  UNCHANGED (16/16 wql + 6/6 trama incl. elif/truthiness/ws-trim showcases);
  scoped ctest (wql|trama|token_macro|schema|metaprog) **87/87**;
  `peg_gen_logos_oracle` **2095/2095** byte-identical; the migration oracles
  re-based to generated-parser SELF-CONSISTENCY (deterministic + well-formed IR):
  `wql_peg_oracle` **40/40**, `trama_peg_oracle` **24/24** (now incl. elif chains).
  The two former hand-parser UNIT tests (`wql_el_parse`/`wql_trama_parse`) retargeted
  to the generated parsers + mutation-verified (corrupt a check → the test fails).

## Phase 3 — Trama parser MADE FUNCTIONAL (generator T1 + T2) — DONE 2026-07-01

The generated `trama_parser.logos` was a self-contained STAND-IN (a single-mode
`/[^{]+/` text token that stopped at any `{`, and a local `expr` shim). It is now
a FUNCTIONAL Trama parser producing the real Trama AST + embedded EL, via two
additive, schema-mode-gated generator capabilities in `codegen.logos` (logos.peg
output byte-identical → `peg_gen_logos_oracle` stays **2095/2095**):

- **T1 — two-mode (TEXT/TAG) template lexer.** A raw-text token shape `/[^X]+/`
  (`regex_shape==4`, `is_neg_class_plus`) turns on a TEMPLATE lexer
  (`emit_lex_one_template`, gated by `uses_raw_text`). In TEXT mode only tag-OPEN
  delimiters (`{`-starting literals) and the raw-text run apply — keyword/ident
  tokens are NOT lexed in text, so a text run `"if you…"` is verbatim TEXT, not a
  `KW_IF` (the single-mode failure). Entering a tag flips `in_tag`; TAG mode skips
  tag-interior whitespace and matches keywords/`=`/IDENT + the tag-CLOSE
  (`}`-ending literal), which flips back. The mode is carried in the pos-keyed
  token cache (`TokCell.intag_after`), sound because mode-at-pos is deterministic
  and every backtrack target is already cached. Faithful to the hand `parse_text`.
- **T2 — cross-grammar EL embedding.** `%import "…el_parser" as el` + the `expr`
  rule `el::expr` lower to `emit_embed_call`: scan the raw sub-range from the
  cursor to the tag-close (`embed_find_close`, driven by the grammar's `}`-ending
  literals), slice `src`, and call `logos.std.wql.el_parser::parse_expr(sub, doc)`
  into the SHARED arena; the SExpr root is wired into the enclosing TStmt's `expr`
  edge, and the lexer is reset AT the close so the next `RMUST`/`RSTMT` token
  matches. The imported parser is called by FULLY-QUALIFIED path (no `use`, which
  would collide `Lexer`/`Token`/`lexer_new` between the two same-module parsers).
  Mirrors the hand `parse_embedded_el`.

Two latent generator bugs the functional Trama parser exposed (both a NULLP
sentinel collision — "no match" vs "matched, null value" — WORKED AROUND before by
the stand-in never exercising them; FIXED at the class here, oracle-safe):

- **Nullable-rule fail-check.** A rule whose alt is entirely `?`/`*`(min 0)/
  lookahead ALWAYS succeeds; its NULLP result is a legitimate null value (an absent
  `if_tail` → null `alt`), NOT a failure. `rule_is_nullable`/`alt_is_nullable` now
  suppress the `== NULLP { fail }` check on a ref to such a rule (else `{% if %}`
  with no `else` failed the whole `if`).
- **No-action multi-item group VALUE.** A no-action group `( stmt_open KW_ELSE
  stmt_close tpl )` returned NULLP, DISCARDING the else-body `tpl`. It now passes
  through the last value-carrying capture (`last_value_capture`), mirroring the
  rule-level `( expr )` grouping — so `if.alt` carries the real else chain.

Both fixes applied to el.peg's regen too (its `arglist` is nullable; its arg-list
group), regenerated `el_parser.logos` stays **wql_peg_oracle 40/40** IR-identical.

GATES GREEN: full `cmake --build`; `peg_gen_logos_oracle` **2095/2095**
byte-identical; new **`trama_peg_oracle` 22/22** (generated `parse_tpl` statement
sequence == hand `parse_trama` over a 22-template corpus — text runs with bare
`{`/newlines [T1], embedded EL with the full expr surface [T2: field-chains/calls/
ternary/comprehensions], if/else, nested if, for, set, ws-control trims);
`wql_peg_oracle` 40/40; scoped ctest (wql|trama|token_macro|schema|metaprog)
**87/87** (the e2e behavior specs still use the hand parsers, unchanged). The
`trama_peg_oracle` genuinely asserts (corrupting the embed slice start → every
embedded-EL case MISMATCHes). Committed `el_parser.logos`/`trama_parser.logos`
reproduce byte-for-byte from `wql_peg_regen`.

REMAINING (NOT Phase 3): T3 statement-chain `next`-linking + elif→nested-TIf
desugaring are HANDLER post-passes (the generated `tpl` yields a WArray of
per-statement nodes; the handler stitches the `next` chain + rewrites elif). The
actual PARSE SWAP — point the wql!/trama! handlers at the generated parsers and
retire the hand `el.logos`/`trama.logos`/`QLexer` — is the next increment.

## EL schema-emission gaps CLOSED — generated EL parser IR == hand parser IR

The three generator capability gaps that forced the generated EL parser to stub
values are closed in peg_gen_logos's schema-emission mode (all additive /
schema-mode-only — `peg_gen_logos_oracle` stays **2095/2095** on logos.peg):

1. **INTEGER-token → i64 VALUE** — a captured INTEGER token into a WAny/scalar
   field is DECODED via a token→i64 hook: `doc.int(wstr_decode_i64($1))`
   (emit_schema_field, guarded by `capture_is_token_named(..,"INTEGER")`). STRING
   literals intern the UNQUOTED content (`wstr_unquote`, matching el.logos
   parse_string); BOOL literals emit `WAny::from(true/false)` (WA_BOOL at-rest
   encoding, NOT `WAny::from(1u8)` — the two differ at rest).
2. **SExprArr FAN-OUT** — the `args: "argfan"` schema field spreads an
   ARRAY_CAPTURE ($...) of arg edges into the fixed slots a0..a7 via
   set_arg/set_len; multi-arg calls (`contains(a,b)`) carry every arg. The node
   binding is `let mut` (needs a `&mut self` method) — `alt_needs_mut_node`.
3. **COMPREHENSION PLAN** — assembled by the grammar (comp_plan): comp_source
   builds `RScan(src_name)`; an optional `if guard` folds it into `RFilter`
   (fold-mode $0 = the running scan). `SComp.var` carries the loop-var name.

Also fixed a latent generator bug the deep oracle exposed: a **no-action
multi-item alt returned the LAST capture** — for `( expr )` grouping that is the
`)` token (a WString ptr), corrupting the handle currency. Now returns the last
VALUE-carrying capture (`last_value_capture`, skips trailing token/literal
delimiters); fold-chain rules (last item = REP) are unchanged, so logos.peg stays
byte-identical. The C++ generator has the same latent issue but no logos.peg
no-action alt ends in a bare token, so its oracle never exercised it.

The `wql_peg_oracle` was UPGRADED from structural to DEEP byte-identity: it now
compares literal VALUES, NAMES, call ARG lists (SExprArr slots), and
comprehension PLANs (RScan/RFilter) field-for-field over a 40-case corpus
(ints/strings/bools/params/field-chains/all operators/ternary/grouping/single-
and multi-arg calls/comprehensions ± guard). **40/40 IR-identical.** Verified it
genuinely asserts (corrupt the int decode → every int case MISMATCHes).

## Phase 5 — Option B (self-describing IR) landed; name-interning seam RESOLVED

The load-bearing name-interning seam (§5) is now closed via **Option B**: the IR
schemas carry the textual NAMES they reference as `str` fields, not dense ids into
a parser side table. Concretely (ir.logos / trama.logos):
`SParam.name`, `SVar.name`, `SField.name`, `SCall.fn_name`, `RScan.src_name`,
`SComp.var`, `TFor.var`, `TSet.var` are all `str` now (were `i32`/`u8`). The
`ElParser` dense interning tables (`field_starts/lens/tys`, `param_*`, `var_*` +
their `field_name`/`param_name`/`field_ty`/… accessors) are DELETED; the parser
keeps only a tiny ordered `$param` side channel (the generated-fn arg list).

**Codegen fully decoupled from the parser.** `codegen.logos` + `trama_render.logos`
now read names off the IR nodes (`f.name`, `c.fn_name`, `cp.var`, …) and take a
name→value-type dictionary (`ElTypes`, in el.logos) instead of `&ElParser`. The
handler builds `ElTypes` from the exact two sources the old dense tables were
stamped from — schema REFLECTION (`reflect.logos::stamp_types_from_schema`) + the
`with` clause — plus `register_comp_sources` (walks the IR to flag comprehension
sources as `&[Elem]` params). So the codegen depends only on the IR + a type dict,
never on any parser — which is precisely what makes a peg-generated parser a
drop-in producer of the same IR.

**Both parsers produce the SAME self-describing IR.** The generated
`el_parser.logos`/`trama_parser.logos` were REGENERATED from the updated
`grammars/{el,trama}.peg` (`%schema` field names → `name`/`fn_name`/`src_name`/
`var`; rules capture the IDENT: `SField { …, name: $2 }`, `SParam { name: $2 }`,
`SCall { fn_name: $1, … }`, `TFor/TSet { var: $3, … }`). The generator's
schema-emission mode ALREADY supports a captured-token → `str` field write
(`node.name = wstr_as_str(capN)`), so no generator change was needed — the
regenerated parsers are byte-identical to what the swap needs. `wql_peg_oracle`
now 24/24 structurally identical WITH the self-describing IR (was placeholder ids).

GATES GREEN: full `cmake --build`, `peg_gen_logos_oracle` **2093/2093**
byte-identical, wql|trama|token_macro|schema|metaprog ctest **86/86** (all e2e
behavior specs unchanged), `wql_peg_regen` reproduces the committed parsers
byte-for-byte, `wql_peg_oracle` 24/24. Verified `wql_el_parse` genuinely asserts
(corrupting a name check → exit 6 FAIL).

**Those three generator gaps are now CLOSED** (see the top section): INTEGER
value decode, SExprArr fan-out, and comprehension plan assembly all emit
correctly, and `wql_peg_oracle` confirms the generated EL parser IR is BYTE-
IDENTICAL (deep: value/name/args/plan) to the hand parser's over a 40-case
corpus. The generated EL parser is now a drop-in producer of the exact IR the
wql!/trama! handlers consume — the actual parse SWAP (retire el.logos's RD, point
the handlers at `el_parser::parse_expr`) is the remaining mechanical step.

## Phase 4 — generate + wire (bootstrap severed) — DONE 2026-07-01

The EL + Trama parsers are now GENERATED from `stdlib/std/wql/grammars/{el,trama}.peg`
and CHECKED IN as source artifacts under `stdlib/std/wql/{el_parser,trama_parser}.logos`.
A normal `cmake --build` compiles the committed `.logos` into `liblogos-std`; it NEVER
runs the generator (verified via `ninja -t query`: `liblogos-std.a` depends on the
committed sources + `logosc`, NOT on `bin/peg_gen_logos`). The staged order —
core → liblogos-std (incl. generated parsers) → peg_gen_logos — has **no cycle**.

**Generator capabilities added this phase (all opt-in via `%meta`, so logos.peg stays
byte-identical — `peg_gen_logos_oracle` 2093/2093):**
- `%meta package: "<qualified>"` — the emitted `package` name (distinct from `output`,
  the file basename). Bare package names are NOT importable by the module loader; a
  qualified `package` (`logos.std.wql.el_parser`) is required to `use` the generated
  parser. Defaults to `output` when omitted (logos.peg unaffected).
- `%schema { use: "A"  use: "B" }` — MULTIPLE `use:` entries. A schema whose fields
  reference types from several modules (Trama's `TStmt` fields carry `WRef<SExpr>` from
  the `ir` module) needs both imported; the generator emits one `use` per entry.
- `%meta prefix: "<Gp>"` — prefixes the module-GLOBAL emitted identifiers (the token
  consts `TK_*`, the `NULLP` const, and the runtime cache structs `TokCell`/`MemoCell`
  whose `impl Copy` trait-coherence is module-global). **Load-bearing constraint:** Logos
  `const`s + trait impls share ONE namespace across packages in a single-module
  compile (`--emit-module`), while `struct`s/`fn`s are package-scoped. So two generated
  parsers in the SAME module (`el_parser` + `trama_parser`, both in `logos-std`) collide
  on those names without a prefix. `prefix:` (`El`/`Tr`) disambiguates via a whole-
  identifier textual pass over the final output; empty ⇒ byte-identical (oracle-safe).
  This is a generator resolution of a compiler limitation, NOT a compiler change —
  module-global `const` visibility is arguably by-design (stdlib consts are referenced
  cross-package by bare name), so uniquing at the generator is correct.

**Behavior oracle (`wql_peg_oracle`, opt-in, NOT a ctest):** parses an EL corpus (24
snippets spanning the full CEL-precedence surface: literals/params/field-chains/every
binary+unary op/ternary/grouping/nesting) through BOTH the generated
`el_parser::parse_expr` and the hand-written `el::parse_el`, and asserts the SExpr IR
trees are STRUCTURALLY identical — same schema node code + operator ids (`SBin.op`/
`SUn.op`, which ARE emitted as grammar `INT_LIT`s and match exactly) + tree shape,
recursively over the `WRef<SExpr>` edges. **24/24 MATCH.** Genuinely asserts (verified:
corrupting the op-compare drops it to 12/24 FAIL). Harness `wql_oracle/wql_oracle_main.logos`
reads `WRef<SExpr>` fields INLINE (not via ir.logos's `*_expr()` resolvers) to sidestep
the documented cross-CU `WRef<S>::from_wany` mono-enqueue sharp edge.

**Regen target (`wql_peg_regen`, opt-in):** regenerates the two committed parsers from
the grammars offline — mirrors how `logos_parser.logos` is regenerated + gated by
`peg_gen_logos_oracle`.

**WHY STRUCTURAL, not byte-identical (the remaining work = the actual PARSE SWAP):** the
generated parser stamps the leaf name/value/dense-id fields (`SField.key`, `SParam.idx`,
`SVar.idx`, `SLit.val` for INTEGER literals) with PLACEHOLDERS — the documented
`GAP:key/idx/val` in el.peg. It cannot reproduce the hand parser's dense-id INTERNING
(its `field_name`/`param_name` side tables) without the **names-in-nodes (Option B)**
IR-shape change (frontend-plan §5). So today the two IRs share exactly the tree shape +
node codes + op ids and differ only on those placeholders. The generated parsers COMPILE
into liblogos-std and RUN correctly (`el_parser::parse_expr("a + b * 2")` → the correct
left-assoc `SBin(+, …, SBin(*, …))`), but the WQL/Trama codegen is NOT yet rewired to
them — the swap needs Option B first (reshape `ir.logos` to carry names, rewrite
`codegen.logos`/`trama_render.logos` reads off the nodes, delete the ElParser dense
tables, add a token→int decode for `SLit.val`, then the oracle upgrades to full
byte-identity). Trama additionally needs the generator's T1 (raw-until-delimiter text
lexer mode) + T2 (cross-grammar `parse_expr` call) capabilities before its generated
parser is functional (currently a self-contained stand-in per the trama.peg GAPs).

## Phase 2 — schema-emission mode LANDED in peg_gen_logos

The generator now supports the **`%schema` directive** → SCHEMA-EMISSION mode
(additive; grammars without it keep the raw-TOM path byte-identical, oracle stays
green). A `%schema { use: "mod"  arena: external  ref_wrap: "WRef"  S { field:
"type", ... } }` block makes peg_gen_logos emit, per node-producing alt:
`let n: S = doc.make::<S>(); n.field = <typed write>; return n.as_ref().raw() as
*const u8;` — first-class Writ schema views (ADR 0011), NOT raw `WMap` put/get.
Field-type strings drive the write: `"ref T"` → `WRef::<T>::from_any(hand_any(cap))`
(edge; wrapper configurable via `ref_wrap:`), `"str"` → interned string field,
`"WAny"` → verbatim dynamic value, scalar (`i32`/`u8`/…) → `<v> as T`. Fold-mode
`$0` maps to the ref field (left-assoc binary). Group sub-alts covered too.
External-arena entry (`parse_<export>(src, doc: &Writ) -> WAny`) supported for the
wql!/trama! handler to pass its own `&Writ`.

Implementation: `tools/peg_gen_logos/pkg/ast.logos` (SCHEMA_DECL/SCHEMA_FIELD/FTYPE
codes), `grammar_parser.logos` (`parse_schema` + `%schema` dispatch),
`codegen.logos` (Codegen `schema_mode`/`schemas`/`schema_use`/`arena_ext`/`ref_wrap`
+ `emit_schema_action`/`emit_schema_group_action`/`emit_schema_field`). Verified
end-to-end (compile + link + RUN + negative control) by
`tools/peg_gen_logos/schema_test/` (opt-in target `peg_gen_logos_schema_test`).
GATES green: full build, `peg_gen_logos_oracle` 2093/2093 byte-identical,
wql|trama|token_macro|schema|metaprog ctest 86/86.

REMAINING (later phases, per §6 sequencing): author el.peg/wql.peg/trama.peg over
the SExpr/RExpr/TStmt schemas (Option B: names in nodes), commit the generated
parsers + a `wql_peg_regen` target, and swap the hand-written parse step. A known
pre-existing compiler mono-enqueue bug (isolated `--emit-module` of a package that
only READS a `WRef<S>` field it never constructs) must be fixed (or the generated
parsers built in-CU with their construction sites) for the package-based swap; it
is orthogonal to the generator and does not affect the generated code's validity
(it compiles standalone / whole-program).

Move `wql!`/`trama!` PARSING off the hand-written recursive-descent
(`stdlib/std/wql/{el,trama}.logos`, the `QLexer` in `wql.logos`) onto
peg_gen_logos-generated parsers, and teach peg_gen to emit **schema-based** node
construction (`h.make::<S>()` + typed schema field writes) instead of raw
`WMap<Wu6,WAny>` put/get. Only the PARSE step changes; the wql/trama HANDLERS +
CODEGEN keep working over the same schema'd IR.

---

## 1. peg_gen_logos action model today (the thing we extend)

A `.peg` rule alt has an action `=> { CODE: N, FIELD: $k, ... }`. The generator
(`tools/peg_gen_logos/pkg/codegen.logos`) emits, per alt, an alt-fn returning
`*const u8` (an erased node ptr). The node is built RAW:

- `emit_action` (codegen.logos:2308) → `mk_node(doc, code, cap)` when the action
  names `CODE`, else a bare `doc.tinymap(cap)`.
- `mk_node` (generated, emitted at codegen.logos:876) =
  `doc.tinymap(cap)` + `node.set(F_CODE, WAny::from(code as i56))` +
  `node.set_schema_type_code(code as u64)`.
- Each action field → `emit_action_field` (codegen.logos:2347):
  `node.set(F_<name>, WAny::ref_to(cap<n>))` for `$n` captures,
  `WAny::ref_to(rcap...)` for `$...` array captures, `WAny::from(lit as i56)` for
  int/str, `WAny::from(1u8/0u8)` for bool.

So **construction is a fixed shape**: one map kind (`WMap<Wu6,WAny>` TOM), keys
from the grammar's `%fields` (u8), values via `WAny::from`/`WAny::ref_to`. There
is NO schema type at emit time — only a numeric code stamped into a slot. The
node model is fully data-driven by `%fields`/`%nodes`; it is not tied to the
logos-AST specifically (writ.peg/hrpc.peg/logos.peg all reuse the same machinery
with different field/node tables).

**Can it emit schema construction with arbitrary codes/keys?** Yes, additively.
The generator already knows, per alt: the node CODE (→ the schema type), and each
field's key + value expression. Mapping code→schema-name and key→field-name is a
table lookup; emitting `h.make::<S>(); n.field = value; return hand_of(n.as_ref())`
is a different `emit_action`/`emit_action_field`/`mk_node` body over the SAME
captured data. This is **small additive surgery in codegen.logos**, not a rewrite:
a new emission mode selected by a grammar directive, reusing all of the
lexer/rule/capture/backtracking/memo machinery unchanged.

**The exemplar to reproduce** is what el.logos writes BY HAND today
(el.logos:379-443): `let n: SLit = h.make::<SLit>(); n.val = v; return hand_of(n.as_ref());`
— typed schema field writes, WRef edges via `WRef::<SExpr>::from_any(hand_any(child))`.
Schema-emission mode = generate exactly these constructors from the grammar.

## 2. Dependencies — peg_gen depends only on stdlib-CORE

Confirmed. `tools/peg_gen_logos` imports ONLY: `logos.lang.str`,
`logos.lang.writ.{container,anyval,array,wmap,wstring}`, `logos.lang.panic`,
`logos.mem.{string,collections.vec}`, `logos.std.{env,io.fs}`, and its own
`peg_gen_logos.{version,ast,grammar_parser,codegen}`. NOTHING under `std/wql`.
The generated wql/trama parsers will live in `stdlib/std/wql/` — a LEAF nothing
in core imports. No build cycle exists as long as the generated parser is a
committed source artifact (not produced during `cmake --build`).

## 3. Output wiring today (the pattern to mirror)

- **peg_gen_logos itself**: `tools/peg_gen_logos/CMakeLists.txt` builds the tool
  binary; there is NO in-build invocation of it on any grammar. Regeneration is
  offline/manual.
- **The C++ path** (`src/compiler/CMakeLists.txt:28-38`) DOES run `peg_gen_cpp`
  at build time to regenerate `logos_parser.{hpp,cpp}` into `build/` — but that
  is the C++ generator, in-core.
- **Oracle**: `peg_gen_logos_oracle` custom target (CMakeLists.txt:72-78 →
  `oracle/run.sh`) rebuilds two logos_parser harnesses (C++-generated +
  Logos-generated, both from logos.peg) and diffs `writ::stringify` over the
  whole `tests/logos` corpus (2056/2056 byte-identical). Opt-in, NOT a ctest;
  the canary that peg_gen still parses Logos correctly.

**Chosen wiring for wql/trama** (mirrors "check in the generated output"):
commit `stdlib/std/wql/el_parser.logos` + `trama_parser.logos` as source
artifacts; add a separate **opt-in REGEN target** (`wql_peg_regen`, analog of the
oracle target) that runs `peg_gen_logos el.peg`/`trama.peg` → the committed files.
Normal `cmake --build` compiles the committed .logos; it never runs the generator
→ no cycle, no core→wql dependency at build time.

## 4. "As a library" — what must change to run peg_gen on el.peg/trama.peg

Little. `main.logos` already is a general CLI: `peg_gen_logos <grammar.peg>
[--out-dir <dir>]` → `parse_grammar_string` → `codegen_logos` → write
`<output>.logos` (output name from `%meta output:`). To target el/trama:

1. **Author `tools/peg_gen_cpp/grammars/el.peg` + `trama.peg`** (grammars live
   there alongside logos/writ/hrpc). el.peg encodes the EL grammar already
   documented in el.logos:9-21; trama.peg encodes the `{{ }}`/`{% %}` template
   grammar. `%fields`/`%nodes` are the SCHEMA field keys + schema codes (SExpr/
   RExpr/TStmt from ir.logos/trama.logos).
2. **Schema-emission mode**: a `%meta store: "schema"` (or `%schema { code→S }`)
   directive telling codegen to emit `h.make::<S>()` constructors + typed writes,
   plus a `use` of the IR module. Absent ⇒ current raw-TOM mode (keeps
   logos/writ/hrpc byte-identical → oracle stays green).
3. **A REGEN target** (§3) writing into `stdlib/std/wql/`.

Entry point = existing `main.logos`; config = the new `%meta`/`%schema` block;
output path = `--out-dir stdlib/std/wql`.

## 5. Name interning — how the generated parser feeds codegen

**This is the load-bearing seam.** Today codegen.logos recovers names via the
`ElParser` dense tables: `p.field_name(key)`, `p.param_name(idx)`, `p.field_ty`,
`p.param_ty`, `p.var_name` — keyed by the dense ids stored in the schema nodes
(`SField.key`, `SParam.idx`, `SVar.idx`). The nodes store only dense ids, not
names; the tables invert id→name.

A peg-generated parser has no ElParser and no such tables. Two options:

- **(A) reproduce the tables** — the generated parser carries the same dense
  interning namespaces + `field_name`/`param_name` accessors. High-fidelity to
  the current codegen but couples the generic generator to wql-specific
  interning; awkward to express in a .peg action.
- **(B) store names/types IN the nodes** — RECOMMENDED. Give the schema nodes a
  name field (`SField.name: str`, `SParam.name: str`, `SVar.name: str`, plus the
  declared `ty`), populate it at parse time from the matched IDENT text, and have
  codegen read `f.name` directly instead of `p.field_name(f.key)`. This is the
  clean resolution the mandate asks for: the IR becomes self-describing (a Writ
  property — serialization already carries it), the generated parser needs no
  side tables, and codegen drops its ElParser dependence. Schema string fields
  are already supported (ADR 0011 `str` field = `wstring_in_alloc` write /
  `as_wstr` read). The dense id can stay as a secondary field or be dropped.

Option B also removes the current `#[borrow_carrying]` "launder the parser handle
through .raw()" gymnastics (project memory: the codegen reads `&parser` after a
`parse_root` that ties WAny to `&mut parser`) — with names in nodes, codegen
needs no parser reference at all.

## 6. Recommended approach — schema-emission direct (Option B interning)

Direct schema-emission (not generic-tree + thin transform):

- Teach codegen.logos a **schema-emission mode** producing the exact constructor
  shape el.logos hand-writes (`h.make::<S>()` + typed field writes + WRef edges),
  gated by a grammar directive so logos/writ/hrpc keep the raw-TOM mode and the
  oracle stays byte-identical.
- Author el.peg/trama.peg with `%fields`/`%nodes` = the SExpr/RExpr/TStmt schema
  keys/codes, and store field/param/var NAMES + types in the nodes (Option B) so
  codegen reads them off the IR and no interning tables are needed.
- Commit el_parser.logos/trama_parser.logos + a `wql_peg_regen` opt-in target.
- Swap wql.logos/trama_render.logos to call the generated parser; keep the
  handler clause-lexing (`from/where/select`, `fn/data`) — only the where/select/
  template-body EL parse moves to the generated parser.

Rationale: a generic-tree+transform layer would re-introduce exactly the raw-TOM
→ typed-view conversion that ADR 0011 exists to eliminate, and would need a
second walk. Direct schema-emission dogfoods ADR 0011 end-to-end (parser writes
typed views; codegen reads typed views) and reuses the whole peg_gen machinery.
Invasiveness is confined to codegen.logos emission bodies + new grammars +
node-schema name fields — all additive, all behind a directive, oracle-safe.

## 7. Canary + bootstrap invariants

- **Oracle green at each phase end**: schema-emission is behind a directive;
  logos.peg keeps the default raw-TOM mode → generated logos_parser byte-identical
  → `peg_gen_logos_oracle` stays 2056/2056.
- **No build cycle**: generated el_parser/trama_parser are committed sources;
  regeneration is an opt-in target; core never imports std/wql.
