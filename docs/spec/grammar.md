# Grammar

Concrete-syntax rules for Logos source, Writ data literals, and the Hest RPC (hrpc) IDL. Extracted from the PEG grammar (`tools/peg_gen_cpp/grammars/*.peg`) and the parser/renderer sources; each rule id is a permanent linkable address.

## Blocks

### `grammar.block.brace` — Block

A block is `{ stmt* }`: a brace-delimited sequence of zero or more statements.

*Source:* `tools/peg_gen_cpp/grammars/logos.peg#L1821-L1823`, `tools/peg_gen_cpp/grammars/logos.peg#L1882-L1883`

## Expressions and calls

### `grammar.expr.array-literal` — Array literal

arr_lit ::= '[' (expr (',' expr)* ','?)? ']' ; a bracket-delimited comma-separated list of expressions with optional trailing comma.

*Source:* `tools/peg_gen_cpp/grammars/logos.peg#L2966-L2967`

### `grammar.expr.tuple-literal` — Tuple literal arity

tuple_lit ::= '(' expr ',' expr (',' expr)* ')' | '(' expr ',' ')' ; a tuple literal requires either a single element with a trailing comma, or two or more comma-separated elements. '(expr)' alone is not a tuple.

*Source:* `tools/peg_gen_cpp/grammars/logos.peg#L2970-L2973`

### `grammar.expr.closure-param-untyped` — Closure parameter type may be omitted

closure_param allows the type annotation to be omitted: '|x|' is accepted as well as '|x: T|'. Forms: '&mut IDENT', '&IDENT', 'ref IDENT: T', 'mut IDENT: T', 'mut IDENT', '(pat_binding_list): T', 'IDENT: T', 'IDENT'. The omitted type is inferred from the surrounding fn(T)-&gt;R formal at the call site.

**Divergence:** Conformant with Rust closure type-inference.

*Source:* `tools/peg_gen_cpp/grammars/logos.peg#L2979-L3000`

### `grammar.expr.closure-expr` — Closure expression forms

closure_expr is '|' closure_param_list? '|' or '||' (OR token), optionally preceded by 'move' and optionally followed by '-&gt; type', with a body that is either a block or (tried after block forms) a single expression. '|x| expr' / '|| expr' are brace-less expression-body closures; the body expr stops at the enclosing ',' / ')'.

*Source:* `tools/peg_gen_cpp/grammars/logos.peg#L3004-L3039`

### `grammar.expr.paren-expr` — Parenthesised expression

paren_expr ::= '(' expr ')' ; a single parenthesised expression (not a tuple).

*Source:* `tools/peg_gen_cpp/grammars/logos.peg#L3042-L3043`

### `grammar.expr.call-arg-list` — Call argument list

call_arg_list ::= expr (',' expr)* ','? ; a call site's value-argument list with optional trailing comma.

*Source:* `tools/peg_gen_cpp/grammars/logos.peg#L3187-L3189`

### `grammar.expr.call-package-qualified` — Package-qualified free-function call

A call 'IDENT path_dot_ident+ '::' IDENT ('::' '&lt;' type_arg_list '&gt;')? '(' call_arg_list? ')'' resolves a free fn by its dotted package path (RECEIVER = first segment, QUAL_PARTS = rest); this disambiguates same-named free fns across packages (e.g. logos.lang.mem::replace vs logos.lang.ptr::replace).

**Divergence:** Logos path model: '.'-separated package path + '::'-item (vs Rust all-'::').

*Source:* `tools/peg_gen_cpp/grammars/logos.peg#L3191-L3203`

### `grammar.expr.call-turbofish-type` — Turbofish on the type

'Type::&lt;T&gt;::method(args)' is a STATIC_CALL applying type args &lt;T&gt; to the type before selecting an associated fn/method/new; 'Type::&lt;T&gt;(args)' / 'new::&lt;T&gt;(args)' / 'null::&lt;T&gt;(args)' are GENERIC_CALL with the type args on the callee.

*Source:* `tools/peg_gen_cpp/grammars/logos.peg#L3204-L3213`

### `grammar.expr.call-ufcs-qualified` — UFCS qualified-path call

'&lt;Type as Trait&gt;::method(args)' dispatches on the concrete Type; the trait qualifier is consumed and dropped because the type-dispatch already resolves the method.

**Divergence:** Trait qualifier in &lt;T as Tr&gt;::m is dropped (Rust uses it for disambiguation).

*Source:* `tools/peg_gen_cpp/grammars/logos.peg#L3214-L3219`

### `grammar.expr.call-turbofish-method` — Turbofish on the method

'Type::method::&lt;T&gt;(args)' applies type args &lt;T&gt; to the associated method's own generics (distinct from Type::&lt;T&gt;::method); must be matched before plain 'Type::method(args)'.

*Source:* `tools/peg_gen_cpp/grammars/logos.peg#L3220-L3225`

### `grammar.expr.call-static` — Static / associated call

'Type::IDENT(args)' and 'Type::new(args)' are STATIC_CALLs invoking an associated function (including the 'new' constructor) of the receiver type.

*Source:* `tools/peg_gen_cpp/grammars/logos.peg#L3226-L3229`

### `grammar.expr.call-metavar` — Metavariable call

'#IDENT(args)' and '#(expr)(args)' invoke a callee named by a metavariable (NAME_VAR) or by an evaluated expression, used in metaprogramming-expanded call sites.

**Divergence:** No Rust analogue; metaprogramming callee splice.

*Source:* `tools/peg_gen_cpp/grammars/logos.peg#L3230-L3237`

### `grammar.expr.call-free` — Free function call

'IDENT(args)', 'new(args)', and 'null(args)' are plain free-function CALLs by name.

*Source:* `tools/peg_gen_cpp/grammars/logos.peg#L3238-L3243`

## Method calls

### `grammar.method-call.args-shape` — Method-call argument list shape depends on turbofish presence

A method call without turbofish carries its arguments as a flat array under ARGS; a turbofish method call wraps the arguments as `{ ITEMS: [...] }` (mirroring generic/static calls).

*Source:* `src/compiler/sema_expr.cpp#L7987-L8002`, `src/compiler/sema_expr.cpp#L7831-L7837`

## Generics

### `grammar.generic.type-param-list` — Type parameter list

type_param_list ::= '&lt;' type_param (',' type_param)* ','? '&gt;' ; also reused for specialisation patterns (&lt;i32&gt;, &lt;*T&gt;, &lt;[T;4]&gt;). Distinguishing type-var vs concrete is deferred to sema, not grammar.

*Source:* `tools/peg_gen_cpp/grammars/logos.peg#L3073-L3075`

### `grammar.generic.hrtb-binder` — HRTB for&lt;...&gt; binder parsed then dropped

hrtb_binder ::= 'for' '&lt;' LIFETIME (',' LIFETIME)* ','? '&gt;' may prefix any trait_bound. Lifetimes are not tracked structurally, so for&lt;'a&gt; Trait&lt;...&gt; is semantically equivalent to Trait&lt;...&gt; (binder parsed into a disposable head).

**Divergence:** Lifetimes not structurally tracked: HRTB binder is accepted but discarded (Rust enforces it).

*Source:* `tools/peg_gen_cpp/grammars/logos.peg#L3077-L3108`

### `grammar.generic.lifetime-param-outlives` — Lifetime parameter with outlives bound

lifetime_param ::= LIFETIME (':' LIFETIME ('+' LIFETIME)*)? ; a lifetime parameter optionally carries one or more outlives bounds.

*Source:* `tools/peg_gen_cpp/grammars/logos.peg#L3141-L3145`

### `grammar.generic.type-param-forms` — Type parameter forms

type_param admits: lifetime_param; 'IDENT: lifetime_param (+ lifetime_param)*' (type-outlives); ptr/arr specialisation patterns; const params 'const IDENT: T', 'const IDENT...: T' (variadic), 'const #IDENT: T'; variadic type param 'IDENT... (: bounds)?'; metavar '#IDENT (: bounds)?'; 'IDENT: bounds (= default)?'; 'IDENT = default'; or bare 'IDENT'. A repeat-group '#(type_param), *' expands variadically.

**Divergence:** Logos additions: variadic type/const params ('...'), metavar params ('#'), repeat-group expansion (no Rust equivalent).

*Source:* `tools/peg_gen_cpp/grammars/logos.peg#L3147-L3181`

### `grammar.generic.type-param-default` — Type parameter default

A type parameter may carry a default via 'IDENT (: bounds)? = type_ref'; the default type applies when the argument is omitted at instantiation.

*Source:* `tools/peg_gen_cpp/grammars/logos.peg#L3174-L3177`

### `grammar.generic.type-arg-list` — Type argument list

type_arg_list ::= type_or_lt_arg (',' type_or_lt_arg)* ','? ; generic instantiation argument list (e.g. Vec&lt;i32&gt;).

*Source:* `tools/peg_gen_cpp/grammars/logos.peg#L3183-L3185`

## Trait bounds

### `grammar.trait.relaxed-bound-sized-only` — `?Trait` relaxed bound accepts only `?Sized`

A TRAIT_BOUND may carry a RELAXED marker for the `?Trait` relaxed-bound syntax; in the current phase the only accepted relaxed bound is `?Sized`.

*Source:* `tools/peg_gen_cpp/grammars/logos.peg#L76`

### `grammar.trait.relaxed-bound-question` — Relaxed bound ?Trait

trait_bound may be '?' IDENT (RELAXED). Grammatically any '?Ident' is accepted; sema rejects anything other than '?Sized' (which opts a type parameter out of the implicit Sized bound).

*Source:* `tools/peg_gen_cpp/grammars/logos.peg#L3092-L3096`

### `grammar.trait.bound-forms` — Trait bound forms

A trait_bound is an IDENT optionally with generic args 'Name&lt;bound_arg_list&gt;' or Fn-family parenthesized form 'Name(closure_type_args?) ('-&gt; type)?', each optionally HRTB-prefixed.

*Source:* `tools/peg_gen_cpp/grammars/logos.peg#L3095-L3124`

### `grammar.trait.bound-arg-assoc-eq` — Associated-type equality in bound args

bound_arg_list mixes positional type/lifetime args and associated-type equality clauses; bound_arg ::= IDENT '=' type_ref (ASSOC_EQ_BIND) | type_or_lt_arg.

*Source:* `tools/peg_gen_cpp/grammars/logos.peg#L3126-L3133`

## Modules

### `grammar.module.from-contextual-keyword` — `from` is a contextual keyword in `use … from <module>;`

In `use pkg from <module-expr>;`, `from` is lexed as a plain IDENT token (not a reserved keyword) and validated to equal the text "from" during semantic analysis; a global reserved keyword `from` would clash with the `From::from` trait method name.

*Source:* `tools/peg_gen_cpp/grammars/logos.peg#L91-L97`

## Metaprogramming (quote)

### `grammar.metaprog.quote-item` — quote_item! macro

quote_item_expr ::= 'quote_item' '!' '{' item* '}' ; body is zero or more item declarations producing a typed AST (item-blob) literal.

**Divergence:** No Rust analogue (Rust uses macro_rules!/proc-macro quote).

*Source:* `tools/peg_gen_cpp/grammars/logos.peg#L3051-L3052`

### `grammar.metaprog.quote-expr` — quote_expr! macro

quote_expr_expr ::= 'quote_expr' '!' '{' expr '}' ; body is a single expression producing a typed AST (expr-blob) literal.

**Divergence:** No Rust analogue.

*Source:* `tools/peg_gen_cpp/grammars/logos.peg#L3060-L3061`

### `grammar.metaprog.quote-ty` — quote_ty! macro

quote_ty_expr ::= 'quote_ty' '!' '{' type_ref '}' ; body is a single type expression producing a first-class Type literal (same Type{kind,name,size} shape as type_of::&lt;T&gt;()).

**Divergence:** No Rust analogue.

*Source:* `tools/peg_gen_cpp/grammars/logos.peg#L3068-L3069`

## Writ data-literal grammar

### `grammar.writ.entry-key-kinds` — Writ entry keys

writ_entry ::= (STRING | '-' INTEGER | INTEGER) ':' writ_val ; a map key is a quoted string, a negative integer, or a non-negative integer. A '-' INTEGER key carries LO_NEG.

**Divergence:** No Rust analogue; Writ data-literal grammar.

*Source:* `tools/peg_gen_cpp/grammars/logos.peg#L2931-L2936`

### `grammar.writ.type-slot-path` — Writ CFG type-slot

writ_val may be '&lt;' 'type' ':' IDENT path_step+ '&gt;' producing a CFG_SLOT_TYPE (slot extraction keeping an IDENT-only head followed by path steps).

**Divergence:** No Rust analogue; Writ embedded-type slot.

*Source:* `tools/peg_gen_cpp/grammars/logos.peg#L2945-L2946`

### `grammar.writ.capture-placeholders` — Writ runtime capture placeholders

Inside a Writ literal, '${' expr '}' captures an arbitrary expression (WRIT_CAP_EXPR) and '$' IDENT captures a named binding (WRIT_CAP_IDENT) as a runtime value.

**Divergence:** No Rust analogue; Writ interpolation.

*Source:* `tools/peg_gen_cpp/grammars/logos.peg#L2949-L2950`

### `grammar.writ.nested-at-optional` — Optional @ on nested Writ aggregates

A nested writ_map / writ_array inside a writ_val may optionally be prefixed by '@'; '@'-prefixed and bare forms are equivalent.

**Divergence:** No Rust analogue; Writ literal nesting.

*Source:* `tools/peg_gen_cpp/grammars/logos.peg#L2951-L2955`

### `grammar.writ.scalar-values` — Writ scalar values

writ_val scalars: RAW_STRING/STRING -&gt; WRIT_STR; FLOAT -&gt; WRIT_FLOAT; '-' INTEGER -&gt; WRIT_NEG_INT; INTEGER -&gt; WRIT_INT; 'true'/'false' -&gt; WRIT_BOOL; 'null' -&gt; WRIT_NULL.

**Divergence:** No Rust analogue; Writ scalar literals.

*Source:* `tools/peg_gen_cpp/grammars/logos.peg#L2956-L2963`

### `grammar.writ.map-literal` — Writ map literal

writ_map ::= '{' (writ_entry (',' writ_entry)* ','?)? '}' ; a brace-delimited, comma-separated list of key:value entries (writ_entry), producing {CODE:MAP, ITEMS:[...]}. The entry list may be empty and a single trailing comma after the last entry is permitted.

*Source:* `tools/peg_gen_cpp/grammars/logos.peg#L2924-L2925`, `tools/peg_gen_cpp/grammars/writ.peg#L94-L95`

### `grammar.writ.type-literal` — Writ embedded type literal `<type: T>`

A Writ value may embed a Logos Type as a first-class value via `'<' 'type' ':' simple_type '>'`, producing a WRIT_TYPE_LIT node carrying the rendered type T. Any simple_type is accepted, including generic instantiations (e.g. Vec&lt;u8&gt;, Result&lt;T,E&gt;); it renders back as `<type: T>`.

**Divergence:** No Rust analogue; type-as-value embedding.

*Source:* `tools/peg_gen_cpp/grammars/logos.peg#L2941-L2948`, `src/compiler/sema_render.cpp#L1526-L1531`

### `grammar.writ.export-entrypoints` — Writ grammar public entry rules

The Writ data-format grammar exposes exactly the rules { value, map, array, typed_value, typed_collection } as importable entry points; importing grammars may start parsing at any of these and at no other Writ rule.

*Source:* `tools/peg_gen_cpp/grammars/writ.peg#L17-L17`

### `grammar.writ.node-kinds` — Writ AST node kinds

A Writ value parses to exactly one node kind from the closed set { MAP, ARRAY, STRING, INTEGER, FLOAT, BOOLEAN, NULL_VAL, TYPED_VALUE, DATATYPE, MAP_ENTRY, PARAM_VAL }. Each node carries a CODE field equal to its kind code.

*Source:* `tools/peg_gen_cpp/grammars/writ.peg#L31-L43`

### `grammar.writ.typed-value-form` — TYPED_VALUE node

A TYPED_VALUE node represents a typename-applied-to-value construction of the form Typename(value), e.g. Date("2026-01-01").

*Source:* `tools/peg_gen_cpp/grammars/writ.peg#L39`

### `grammar.writ.datatype-form` — DATATYPE node

A DATATYPE node represents a type name with optional generic parameters of the form Typename&lt;Param, ...&gt;.

*Source:* `tools/peg_gen_cpp/grammars/writ.peg#L40`

### `grammar.writ.value-alternatives` — Writ value grammar

value &lt;- param_val / typed_value / typed_collection / map / array / STRING / FLOAT / INTEGER / TRUE / FALSE / NULL. The ordered alternation is the embeddable entry point for a Writ value; PEG ordering means earlier alternatives win (param_val, typed_value, and typed_collection are attempted before bare map/array/scalars).

*Source:* `tools/peg_gen_cpp/grammars/writ.peg#L81-L91`

### `grammar.writ.scalar-literals` — Scalar literal node codes

A bare STRING yields {CODE:STRING}, FLOAT yields {CODE:FLOAT}, INTEGER yields {CODE:INTEGER}, TRUE/FALSE yield {CODE:BOOLEAN, VALUE:true|false}, and NULL yields {CODE:NULL_VAL}. Boolean keywords carry the literal value; NULL carries no value.

*Source:* `tools/peg_gen_cpp/grammars/writ.peg#L86-L91`

### `grammar.writ.map-entry-key` — Map entry key/value

map_entry &lt;- (STRING / IDENT) ':' value producing {CODE:MAP_ENTRY, KEY, VALUE}. A key may be either a quoted STRING or a bare IDENT; the value is any Writ value.

*Source:* `tools/peg_gen_cpp/grammars/writ.peg#L97-L98`

### `grammar.writ.typed-value-ctor` — Typed value constructor

typed_value &lt;- datatype '(' value ')' producing {CODE:TYPED_VALUE, NAME:datatype, VALUE:value}. Exactly one inner value is wrapped by the named type constructor (e.g. Date("2026-01-01")).

*Source:* `tools/peg_gen_cpp/grammars/writ.peg#L104-L106`

### `grammar.writ.typed-collection` — Typed array/map syntax hints

typed_collection &lt;- typed_array / typed_map; typed_array &lt;- '&lt;' datatype '&gt;' array; typed_map &lt;- '&lt;' datatype '&gt;' map | '&lt;' datatype ',' datatype '&gt;' map. Type parameters preceding the collection are parse-time hints only: the parser still yields plain ARRAY/MAP nodes. A typed map may declare just the key type (value type defaults to AnyVal) or both key and value types.

*Source:* `tools/peg_gen_cpp/grammars/writ.peg#L114-L119`

### `grammar.writ.datatype` — Datatype reference with generic args

datatype &lt;- IDENT ('&lt;' type_arg (',' type_arg)* '&gt;')? producing {CODE:DATATYPE, NAME:IDENT, PARAMS:[...]}. The generic argument list, when '&lt;...&gt;' is present, must be non-empty.

*Source:* `tools/peg_gen_cpp/grammars/writ.peg#L121-L125`

### `grammar.writ.type-arg` — Generic type argument

type_arg &lt;- datatype / INTEGER. A generic argument is either a nested datatype or a non-type integer literal (C++-style const generic argument).

*Source:* `tools/peg_gen_cpp/grammars/writ.peg#L127-L128`

### `grammar.writ.array-literal` — Writ array literal

array &lt;- '[' (value (',' value)*)? ','? ']' producing {CODE:ARRAY, ITEMS:[...]}: a bracket-delimited, comma-separated list of Writ values. The element list may be empty and a single trailing comma is permitted. The same production is used both standalone and as the embedded `writ_array` form in Logos source.

*Source:* `tools/peg_gen_cpp/grammars/writ.peg#L101-L102`, `tools/peg_gen_cpp/grammars/logos.peg#L2927-L2928`

### `grammar.writ.param-placeholder` — Positional parameter placeholder

param_val &lt;- '$' INTEGER producing a PARAM_VAL node {CODE:PARAM_VAL, VALUE:INTEGER}. '$N' is a positional template parameter placeholder where N is a non-negative decimal integer; the node is assigned the reserved runtime type_hash 127 (tag 0xFF).

*Source:* `tools/peg_gen_cpp/grammars/writ.peg#L130-L133`, `tools/peg_gen_cpp/grammars/writ.peg#L42`

### `grammar.writ.value-forms` — Writ literal value forms

A Writ literal value is one of: null, a boolean, an integer, a negative integer, a float, a string, a map, an array, a type literal, or a config slot-type. Each form has a distinct node code (WRIT_NULL, WRIT_BOOL, WRIT_INT, WRIT_NEG_INT, WRIT_FLOAT, WRIT_STR, WRIT_MAP, WRIT_ARRAY, WRIT_TYPE_LIT, CFG_SLOT_TYPE).

*Source:* `src/compiler/sema_render.cpp#L1479-L1550`

### `grammar.writ.null-literal` — Writ null literal

A missing/null Writ value and the WRIT_NULL form both denote the literal `null`.

*Source:* `src/compiler/sema_render.cpp#L1477`, `src/compiler/sema_render.cpp#L1480`

### `grammar.writ.bool-literal` — Writ boolean literal

A WRIT_BOOL value renders as `true` iff its VALUE is a non-null scalar whose byte is nonzero, else `false`; an absent or zero VALUE is `false`.

*Source:* `src/compiler/sema_render.cpp#L1481-L1485`

### `grammar.writ.int-literal` — Writ integer and float literals

WRIT_INT and WRIT_FLOAT carry their VALUE as the verbatim source token text. A WRIT_NEG_INT renders as `-` prefixed to its VALUE token text (sign is a separate node form, not part of the magnitude token).

*Source:* `src/compiler/sema_render.cpp#L1486-L1491`

### `grammar.writ.string-literal` — Writ string literal retains quotes

A WRIT_STR's VALUE is the STRING token captured WITH its surrounding quotes; the quoted text is the literal verbatim (no separate unquoting/requoting step).

*Source:* `src/compiler/sema_render.cpp#L1492-L1495`

### `grammar.writ.cfg-slot-type` — Writ config slot-type path

A CFG_SLOT_TYPE denotes a type referenced by a dotted path: a head NAME followed by zero or more `.`-separated step NAMEs, rendered `<type: name.step1.step2…>`.

*Source:* `src/compiler/sema_render.cpp#L1532-L1545`

### `grammar.writ.array-render` — Writ array value rendering

A WRIT_ARRAY value renders as `[` &lt;items&gt; `]` where items are comma-separated rendered Writ values; an empty array renders as `[]`.

*Source:* `src/compiler/sema_render.cpp#L1514-L1525`

### `grammar.writ.map-render` — Writ map value rendering

A WRIT_MAP value renders as `{` &lt;entries&gt; `}` where entries are comma-separated. Each entry (WRIT_ENTRY) renders as `KEY: VALUE`; an entry carrying no VALUE renders as just `KEY: ` (key with empty value). An empty map renders as `{}`.

*Source:* `src/compiler/sema_render.cpp#L1496-L1513`

## Hest RPC (hrpc) IDL grammar

### `grammar.hrpc.file-structure` — HRPC file top-level structure

file := package_def? import_def* top_def* — an HRPC source file is an optional single package declaration, followed by zero+ imports, followed by zero+ top-level definitions, in that order.

*Source:* `tools/peg_gen_cpp/grammars/hrpc.peg#L100-L101`

### `grammar.hrpc.package-decl` — Package declaration

package_def := 'package' qualified_name ';' — at most one package declaration per file (production is optional, non-repeating).

*Source:* `tools/peg_gen_cpp/grammars/hrpc.peg#L104-L105`, `tools/peg_gen_cpp/grammars/hrpc.peg#L100`

### `grammar.hrpc.import-decl` — Import declaration

import_def := 'import' STRING ';' — import path is a string literal.

*Source:* `tools/peg_gen_cpp/grammars/hrpc.peg#L108-L109`

### `grammar.hrpc.option-decl` — Option declaration

option_def := 'option' IDENT '=' option_value ';' where option_value := STRING | INTEGER | IDENT. Options may appear at top level and inside message bodies.

*Source:* `tools/peg_gen_cpp/grammars/hrpc.peg#L112-L115`, `tools/peg_gen_cpp/grammars/hrpc.peg#L118`, `tools/peg_gen_cpp/grammars/hrpc.peg#L124`

### `grammar.hrpc.top-def` — Top-level definition kinds

top_def := message_def | enum_def | service_def | option_def — the four item kinds permitted at file scope.

*Source:* `tools/peg_gen_cpp/grammars/hrpc.peg#L118`

### `grammar.hrpc.message-decl` — Message definition

message_def := 'message' IDENT '{' message_body* '}' where message_body := field_def | oneof_def | enum_def | message_def | option_def. Messages nest (message/enum may appear inside a message body).

*Source:* `tools/peg_gen_cpp/grammars/hrpc.peg#L121-L124`

### `grammar.hrpc.field-decl` — Message field definition

field_def := field_label? type_ref IDENT '=' INTEGER ';' where field_label := 'optional' | 'required' | 'repeated'. Every field carries an explicit numeric tag; the label is optional.

*Source:* `tools/peg_gen_cpp/grammars/hrpc.peg#L127-L130`

### `grammar.hrpc.oneof-decl` — Oneof definition

oneof_def := 'oneof' IDENT '{' oneof_field* '}'; oneof_field := type_ref IDENT '=' INTEGER ';'. Oneof members are unlabeled tagged fields and may not nest other oneofs/messages.

*Source:* `tools/peg_gen_cpp/grammars/hrpc.peg#L133-L137`

### `grammar.hrpc.enum-decl` — Enum definition

enum_def := 'enum' IDENT '{' enum_value_def* '}'; enum_value_def := IDENT '=' INTEGER ';'. Each enum value binds a name to an explicit integer.

*Source:* `tools/peg_gen_cpp/grammars/hrpc.peg#L140-L144`

### `grammar.hrpc.service-decl` — Service definition

service_def := 'service' IDENT '{' rpc_def* '}' — a service body contains zero+ rpc method declarations only.

*Source:* `tools/peg_gen_cpp/grammars/hrpc.peg#L147-L148`

### `grammar.hrpc.rpc-method` — RPC method declaration

rpc_def := 'rpc' IDENT '(' rpc_type ')' 'returns' '(' rpc_type ')' ';' — each method has exactly one input and one output rpc_type.

*Source:* `tools/peg_gen_cpp/grammars/hrpc.peg#L150-L151`

### `grammar.hrpc.rpc-stream` — Streaming RPC type

rpc_type := 'stream' type_ref | type_ref — an rpc input or output may be marked streaming with the 'stream' keyword prefix.

*Source:* `tools/peg_gen_cpp/grammars/hrpc.peg#L154-L156`

### `grammar.hrpc.type-ref` — Type reference

type_ref := 'map' '&lt;' type_ref ',' type_ref '&gt;' | qualified_name — a type is either a map with key and value type arguments or a qualified name; maps may nest.

*Source:* `tools/peg_gen_cpp/grammars/hrpc.peg#L160-L162`

### `grammar.hrpc.qualified-name` — Dotted qualified name

qualified_name := IDENT ('.' IDENT)* — a non-empty dot-separated sequence of identifier components.

*Source:* `tools/peg_gen_cpp/grammars/hrpc.peg#L166-L171`
