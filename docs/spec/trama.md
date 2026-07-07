# Trama

> Scope: Trama, Logos's Jinja2-derived template language, in both surfaces — the STATIC `trama!(params){ body }` compile-time token-macro (lowered via metacall to a native render fn) and the DYNAMIC `Tpl::compile(text, &cat)?.render(&env)?` runtime API (package `logos.std.deem`). Source layers: the template grammar `stdlib/std/wql/grammars/trama.peg` (→ generated `stdlib/std/wql/trama_parser.logos`), the schema family + shared chain-walk `stdlib/std/wql/trama.logos`, the static handler `stdlib/std/wql/trama_render.logos`, and the dynamic engine `stdlib/std/deem/deem.logos`; intent from `ADR 0012` (§6/§10.2 surface + constructs) and `ADR 0012-queue2-interpreter` (§4a Null semantics, §3 dynamic API). The embedded `{{ … }}` / tag expression sublanguage is EL (CEL semantics + Logos comprehension syntax) — its canonical rules live in [deem.md](deem.md) under the `el.*` rule ids; Trama specifies only the template layer and its ONE coupling to EL (the `WRef<SExpr>` expression edge). This section contains only rules whose `domain` is `trama`. Each rule's `id` is its permanent linkable address.

## Embedded expression sublanguage (EL)

### `trama.el.embedded-seam` — Every template expression is EL

Every `{{ … }}` interpolation and every `{% if/for/set … %}` tag expression body is an EL expression (canonical grammar + typing + Null rules in [deem.md](deem.md), rule ids `el.*`); Trama couples to it through exactly one edge — the `WRef<SExpr>` on each statement node — and specifies no expression semantics of its own beyond how it typechecks and renders the resulting value.

*Divergence:* Jinja2 has an untyped ad-hoc expression grammar; Trama delegates to EL, a strongly-typed (static surface) / strictly-checked (dynamic surface) CEL-class language shared verbatim with the `deem!` / `Query` sibling surfaces.

*Evidence:* `stdlib/std/wql/grammars/trama.peg#L227-L232` (`expr <- el::expr`); `stdlib/std/wql/trama.logos#L7-L9` (the EXPRESSION seam); `ADR 0012 §seam`

## Surface — the `trama!` resource macro (STATIC)

### `trama.surface.resource-form` — `resource NAME = trama!(params){ body }`

The static surface is `resource <name> = trama!(<params>){ <template> };`; `#[token_macro] fn trama(name, params, body)` receives the `resource` binding NAME, the raw params source text, and the raw brace-body bytes at compile time, and emits `pub fn NAME(params) -> String` via `logos_emit_source`.

*Divergence:* EXTENSION over Jinja2 — a template is a first-class named, compiled Logos function, not a runtime-loaded file; a missing binding name is a compile error (`trama!: needs a binding name`).

*Evidence:* `stdlib/std/wql/trama_render.logos#L557-L567`, `#L644-L657`; `ADR 0012 §6`

### `trama.surface.params-form` — Params are a real Logos parameter list

`params` is a genuine Logos fn-style parameter list (e.g. `o: &Order, greeting: str`), re-emitted VERBATIM into the generated signature so the compiler parses and type-checks it, and parsed a second time locally (`parse_macro_params`, shared with `deem!`) to drive reflection; at least one parameter is required (`trama!: needs at least one parameter`).

*Divergence:* Replaces Jinja2's dynamic, untyped context dict with a typed, compiler-checked parameter list — the params ARE the type source (the old `data`/`with` DSL header is GONE).

*Evidence:* `stdlib/std/wql/trama_render.logos#L569-L578`, `#L644-L650`; `logos.std.wql.params::parse_macro_params`

### `trama.surface.render-fn-shape` — Generated render function body

The emitted fn opens `let mut __out: String = String::new();`, walks the Trama AST appending to `__out`, and closes `return __out;`; every construct lowers to native Logos statements (`__out.push_str(…)` / `push_i64` / `push_str(wql_f64_to_str(…))`, `if`, indexed `while`, `let`).

*Evidence:* `stdlib/std/wql/trama_render.logos#L22-L26`, `#L646-L654`

### `trama.surface.in-package-use` — In-CU macro consumption

A stdlib module may consume `trama!` from a SIBLING module of the same `--emit-module` compilation unit (the handler is metacall-JITed in-CU); this is exercised by the `logos.std.wql.trama_selfuse` canary.

*Divergence:* EXTENSION — no Jinja2 analogue; a language-integration property of the token-macro machinery.

*Evidence:* `stdlib/std/wql/trama_selfuse.logos#L1-L23`

### `trama.surface.bootstrap-limit` — `trama_render` cannot template itself

The `trama_render` handler module can NEVER consume `trama!` (a bootstrap cycle: the handler would depend on its own expansion); every other in-CU module may.

*Evidence:* `stdlib/std/wql/trama_selfuse.logos#L7-L8`

## Body delimiters and escaping

### `trama.body.delimiter-forms` — Three body-delimiter forms

`scan_template` accepts three body forms after `trama!(params)`: a `"…"`-quoted body, a backtick `` `…` ``-quoted body, or a DELIMITER-LESS whole-brace body (no inner quoting) where the entire `{ … }` content is the template.

*Divergence:* EXTENSION over Jinja2's single delimiter convention; the choice is a Logos-source ergonomic (brace-body vs quoted).

*Evidence:* `stdlib/std/wql/trama_render.logos#L94-L123`

### `trama.body.backtick-form` — Backtick body avoids inner-quote escaping

The backtick `` `…` `` form lets the template contain bare `"` EL string literals (`{{ "(" + city + ")" }}`, `{% if x == "NYC" %}`) without escaping; it is the recommended form when embedded EL string literals are used, while the `"…"` form stays for simple templates with no inner quotes.

*Evidence:* `stdlib/std/wql/trama_render.logos#L94-L123`

### `trama.body.delimiterless` — Delimiter-less body is edge-trimmed

When the first non-space byte is neither `"` (34) nor backtick (96), the WHOLE brace body is the template with leading/trailing whitespace TRIMMED; use the backtick/`"` form to preserve exact edge whitespace.

*Divergence:* EXTENSION — Jinja2 has no brace-body notion; edge-trim is a Logos-source concession, opt-out via a quoted form.

*Evidence:* `stdlib/std/wql/trama_render.logos#L105-L116`

### `trama.body.escapes` — Backslash escapes `\{ \} \` \\`

A raw text run may escape `\{`, `\}`, `` \` ``, and `\\`; the backslash SURVIVES in the TEXT slice at parse time and is de-escaped to the bare char at render time (`emit_escaped`, static; `push_deescaped`, dynamic) — so a `\{{` does not open a tag and a stray brace stays literal.

*Divergence:* EXTENSION — Jinja2 escapes differently (`{% raw %}` / `{{ '{{' }}`); Trama uses C-style backslash escapes for the four template metacharacters.

*Evidence:* `stdlib/std/wql/grammars/trama.peg#L154-L161`; `stdlib/std/wql/trama_render.logos#L139-L154` (`emit_escaped`); `stdlib/std/deem/deem.logos#L1790-L1808` (`push_deescaped`)

## Template constructs

### `trama.tag.text` — Literal text run → `TText`

A run of bytes up to the next `{{`/`{%` (a bare `{` not opening a tag stays text) is a `TText` node carrying the boxed literal payload; the static path emits `__out.push_str("<escaped bytes>")`, the dynamic path appends the de-escaped bytes.

*Divergence:* Conformant with Jinja2 literal text, modulo the escape rules of `trama.body.escapes`.

*Evidence:* `stdlib/std/wql/grammars/trama.peg#L181` (`TEXT => TText`); `stdlib/std/wql/trama.logos#L45-L48`; `stdlib/std/wql/trama_render.logos#L376-L382`

### `trama.tag.interpolate` — `{{ expr }}` → `TVar`, per-type formatting

`{{ expr }}` is a `TVar` whose `expr: WRef<SExpr>` renders by inferred EL value-type: STR via `push_str` (concat flattened to successive pushes), BOOL as the words `true`/`false`, FLT via `wql_f64_to_str` (shortest round-trip `%g` decimal, Rust-`Display` convention), else the i64 value via `push_i64`; the dynamic path mirrors this in `rt_push`.

*Divergence:* Jinja2 renders via a single dynamic `str()`/`__str__`; Trama routes on the STATICALLY inferred type, and a Null (lenient, dynamic only) renders as the empty string (`trama.dynamic.null-render`).

*Evidence:* `stdlib/std/wql/grammars/trama.peg#L184-L187`; `stdlib/std/wql/trama_render.logos#L383-L423`; `stdlib/std/deem/deem.logos#L812-L825` (`rt_push`); `logos.std.wql.el::wql_f64_to_str`

### `trama.tag.if` — `{% if %}/{% elif %}/{% else %}/{% endif %}` → `TIf`

`{% if expr %} then {% elif e2 %} … {% else %} alt {% endif %}` is a `TIf { expr, body, alt }`; the condition is coerced to `bool` by Jinja truthiness (bool bare, i64 → `!= 0`, str → non-empty), and each `elif` is desugared at PARSE time into a nested `TIf` in the enclosing `alt` so the walk is uniform.

*Divergence:* Conformant with Jinja2 `if`/`elif`/`else`/`endif` and truthiness; `elif`→nested-`if` is an internal desugaring, not observable.

*Evidence:* `stdlib/std/wql/grammars/trama.peg#L204-L221`; `stdlib/std/wql/trama.logos#L59-L64`; `stdlib/std/wql/trama_render.logos#L450-L491`

### `trama.tag.if-const-fold` — Static const-condition collapses to the taken branch

When the static optimizer const-folds a `{% if %}` condition to a const BOOL, only the taken branch's statements are emitted — no `if` wrapper, the other branch is dropped; a bare INT/STR literal condition (`{% if 1 %}`) is not a const bool and keeps its runtime truthiness guard, but a folding comparison (`1 > 0`) collapses.

*Divergence:* EXTENSION — a compile-time optimization with no Jinja2 analogue.

*Evidence:* `stdlib/std/wql/trama_render.logos#L456-L477`; `logos.std.wql.optimize::sexpr_const`

### `trama.tag.for` — `{% for v in coll %}/{% endfor %}` → `TFor`

`{% for v in expr %} body {% endfor %}` is a `TFor { var, expr, body }`; the static path emits an indexed `while` over `(expr).len()` binding `v` to the element, the dynamic path iterates a Writ array binding `v` per element; `expr` may be a schema'd object-array (elements bind as nodes) or a scalar-element array (elements bind as their scalar), and a null/non-array collection iterates as an EMPTY loop.

*Divergence:* Conformant with Jinja2 `{% for v in seq %}`, but STRONGLY typed — the loop var's element type is resolved (static) / shape-driven (dynamic); Jinja `loop.*` helpers are not provided (EXTENSION gap).

*Evidence:* `stdlib/std/wql/grammars/trama.peg#L193-L199`; `stdlib/std/wql/trama.logos#L53-L58`; `stdlib/std/wql/trama_render.logos#L492-L544`; `stdlib/std/deem/deem.logos#L1835-L1861`

### `trama.tag.set` — `{% set v = expr %}` → `TSet`

`{% set v = expr %}` is a `TSet { var, expr }`; the static path emits a typed `let v: <ty> = expr;` whose type (str/bool/f64/i64) follows the assigned expression, the dynamic path binds `v` to the evaluated `RtVal` in the local render bindings.

*Divergence:* Conformant with Jinja2 `{% set %}`; the binding is a typed Logos `let` (static) rather than a dynamic context write.

*Evidence:* `stdlib/std/wql/grammars/trama.peg#L189-L191`; `stdlib/std/wql/trama.logos#L65-L69`; `stdlib/std/wql/trama_render.logos#L424-L449`; `stdlib/std/deem/deem.logos#L1862-L1865`

### `trama.tag.whitespace-control` — `{%- -%}` / `{{- -}}` trim variants

Each tag delimiter has a whitespace-control variant (`{{-`/`-}}`, `{%-`/`-%}`) that trims the adjacent text run; both variants are accepted at every tag site.

*Divergence:* Conformant with Jinja2 whitespace-control markers.

*Evidence:* `stdlib/std/wql/grammars/trama.peg#L19`, `#L127-L135`, `#L184-L187`, `#L224-L225`; lexed as `TrTK_{L,R}{STMT,MUST}_TRIM` in `stdlib/std/wql/trama_parser.logos#L28-L31`, `#L164-L223`

### `trama.ast.chain` — Statements are a `next`-linked `TStmt` chain

A template block is a null-terminated singly-linked chain: every `TStmt` carries `next: WRef<TStmt>` (key 1) and a `for`/`if` body (and `if` else) is the head `WRef<TStmt>` of its own chain; the generated parser yields a WArray of statement handles and the `chain_array` post-pass stitches the `next` links (shared by both surfaces).

*Divergence:* EXTENSION — an IR representation detail, not surface-observable; Trama's AST is a first-class Writ schema tree (category `0x0013`), dogfooding ADR 0011.

*Evidence:* `stdlib/std/wql/trama.logos#L17-L19`, `#L45-L73` (schemas), `#L220-L234` (`chain_array`)

## Typing and reflection (STATIC)

### `trama.typing.schema-params-no-annotation` — Schema params need no annotation

A param whose core type resolves to a schema struct is REFLECTED (`stamp_types_from_schema`): its field accesses (`{{ o.total }}`) type automatically and it is bound in the struct-type env so field chains resolve — no `with`/`data` clause exists.

*Divergence:* Replaces Jinja2's untyped attribute access with schema-driven static field typing.

*Evidence:* `stdlib/std/wql/trama_render.logos#L600-L624`; `logos.std.wql.reflect::stamp_types_from_schema`

### `trama.typing.scalar-params` — Scalar/str params render directly

A scalar or `str` param (`greeting: str`) gets its EL value-type set directly (`el_ty_of_name`), so a bare `{{ greeting }}` renders with the correct push (str vs bool/f64/i64) and the param name is usable directly in the template.

*Evidence:* `stdlib/std/wql/trama_render.logos#L611-L616`

### `trama.typing.recursive-loop-reflection` — Loop vars reflect through the schema

`reflect_loops` follows every `{% for v in EXPR %}`: it resolves `EXPR`'s element type `T` from the schema, records `v`'s Logos binding type, binds `v → T` in the env so `v.member` chains and NESTED loops (`{% for l in item.lines %}`) resolve, and reflects `T`'s fields so `v.member` types automatically.

*Divergence:* EXTENSION — static, schema-driven loop-variable typing with no Jinja2 analogue; a struct element binds BY REFERENCE (`let v: &Ty = &(iter)[__i];`) to avoid moving out of an index.

*Evidence:* `stdlib/std/wql/trama_render.logos#L298-L345` (`reflect_loops`), `#L509-L533` (by-ref element binding)

### `trama.typing.udfs-from-module` — Top-level fns are template UDFs

The module's top-level fns are registered as UDFs (`stamp_udfs_from_module`), so `{{ myfn(o.total) }}` / `{% if myfn(x) %}` route through the SHARED `emit_call` path (zero trama-specific lowering); same-named builtins shadow user fns.

*Divergence:* EXTENSION over Jinja2 filters/globals — arbitrary type-checked Logos functions, resolved builtin-first.

*Evidence:* `stdlib/std/wql/trama_render.logos#L625-L628`

### `trama.typing.errors-are-diagnostics` — Static errors are compile diagnostics

On the static surface, unknown-function / arity / tuple-in-render errors are latched via `error()` (a build failure) and emission continues over a safe placeholder; a `{{ tuple }}` is rejected (templates render TEXT, not rows).

*Divergence:* EXTENSION — Jinja2 surfaces errors at render time; the static surface fails the BUILD.

*Evidence:* `stdlib/std/wql/trama_render.logos#L390-L398`, `#L561-L562`, `#L575-L576`, `#L584`

## Dynamic surface — `Tpl::compile` / `render` (RUNTIME)

### `trama.dynamic.compile-render` — `Tpl::compile(text, &cat)?` / `.render(&env)?`

The runtime API is `Tpl::compile(text: str, cat: &SchemaCatalog) -> Result<Tpl, QError>` (parse + chain-stitch + shared IR optimizer + env-independent checks, compile-once) then `render(env: &QEnv) -> Result<String, QError>` (strict check against catalog+env, then a `TStmt` walk building a `String`, re-entrant over different envs).

*Divergence:* EXTENSION — Jinja2's canonical mode; here the template TEXT arrives at RUNTIME but is parsed by the SAME generated `parse_tpl` and rendered by the SAME shared chain-walk as the static surface.

*Evidence:* `stdlib/std/deem/deem.logos#L1882-L1923`; `ADR 0012-queue2 §3`

### `trama.dynamic.errors-are-values` — Errors are `QError` values, not diagnostics

On the dynamic surface every error is a `Result::Err(QError)` VALUE (a positioned message), never a compile diagnostic — the caller is a running program; a malformed template yields `Tpl::compile: template parse error`.

*Divergence:* Inverts the static surface (`trama.typing.errors-are-diagnostics`); this is the ADR §3 "errors are values" contract for runtime text.

*Evidence:* `stdlib/std/deem/deem.logos#L88-L119` (`QError`), `#L1889-L1904`; `ADR 0012-queue2 §3`

### `trama.dynamic.strict-check` — Strict typing against catalog + env

Compile is env-independent (unresolvable roots type UNKNOWN, deferred; unknown fn names defer to render); render is STRICT — every template root name must resolve to a `QEnv` binding and every field to a catalog entry, with the full EL type lattice applied.

*Evidence:* `stdlib/std/deem/deem.logos#L41-L49`, `#L840-L846`, `#L1889-L1914`; `ADR 0012-queue2 §4`

### `trama.dynamic.udf-registry` — Templates share the query UDF registry

Template UDFs resolve at render against the SAME env UDF/UDA registry that `Query` uses (`register_fn`/`register_agg`, builtin-first per ADR §6) — one unified registry across the query and template surfaces.

*Divergence:* EXTENSION — Jinja2 filters/globals are a template-only namespace; here templates and queries share one function registry.

*Evidence:* `stdlib/std/deem/deem.logos#L592-L662`; `ADR 0012-queue2 §6`, `§I3`

### `trama.dynamic.lenient-erased` — Erased bindings enable lenient render

Lenient-ness is a per-BINDING property: `env.bind_node_erased(name, node)` / `env.bind_source_erased(name, arr)` type the root as `dyn`; fields on an erased value resolve at RUNTIME by name and misses yield `RtVal::Null`, which propagates CEL-style.

*Divergence:* EXTENSION over both Jinja2 and the static surface (which is strict-only, D4); this is the ADR §4a lenient half for erased/untyped data.

*Evidence:* `stdlib/std/deem/deem.logos#L570-L590` (`bind_node_erased`/`bind_source_erased`); `ADR 0012-queue2 §4a`

### `trama.dynamic.null-render` — `Null` renders as the empty string

In lenient mode a `{{ x }}` whose value is `RtVal::Null` renders as the EMPTY string; a `Null` `{% if %}` predicate takes the else branch (`!Null` → `true`), and a non-array lenient `{% for %}` collection iterates as EMPTY.

*Divergence:* EXTENSION — CEL-style Null semantics (`ADR 0012-queue2 §4a` propagation table) with no static-surface analogue; strict bindings never produce Null.

*Evidence:* `stdlib/std/deem/deem.logos#L814-L825` (Null → no push), `#L1821-L1834`, `#L1835-L1860`; `ADR 0012-queue2 §4a` (`{{ x }}` render → empty string; `{% if %}`/`{% for %}` rows)

### `trama.dynamic.str-ownership` — Rendered strings view a per-render scratch arena

Computed strings (concat/upper/lower) produced during render are interned into a per-render scratch `Writ` arena whose views stay valid for the render; the final `String` is built before the scratch drops.

*Divergence:* EXTENSION — a runtime memory-management detail, not surface-observable.

*Evidence:* `stdlib/std/deem/deem.logos#L44-L48`, `#L827-L838` (`intern`), `#L1916-L1921`
