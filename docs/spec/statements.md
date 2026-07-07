# Statements

Statement-level semantics of Logos: statement forms, `let` bindings and destructuring, assignment, control-flow statements, divergence, and drop/scope behavior. Extracted from the grammar (`tools/peg_gen/grammars/logos.peg`), Sema (`src/compiler/sema_*`), and codegen (`src/compiler/mlir_gen_stmt.cpp`) layers; each rule cites its source evidence.

## Statement forms

### `stmt.kinds.dispatch` — Statement forms

A statement is one of: nested-fn, labeled-loop, let-else, let, for, while, loop, return, break, continue, deref-write, if-expr, match, destructure-assign, assign, compound-assign, place-assign, unsafe-block, block, `expr ;` (expression statement), or a trailing `expr` (block tail value). A block without a trailing `;` yields its final expression as the block value.

> **Note:** Overlaps `stmt.dispatch.kinds` (Sema-layer inventory of the same statement forms); the two lists differ in granularity, not substance.

Evidence: `tools/peg_gen/grammars/logos.peg#L1839-L1866`

## Statement dispatch (Sema view)

### `stmt.dispatch.kinds` — statement forms recognized

Statements comprise: let / let-else / let-destructure / let-pattern bindings, nested fn items, assignment (plain / destructuring / compound / place / deref-write / deref-compound), return, if / if-let-chain, labeled loops, while / for / for-each / loop, match, expression-statement, trailing tail-expression, break, continue, unsafe block, and bare block statement. An if-let-chain in statement position is desugared via the expression form and wrapped as a statement-expression.

> **Note:** Overlaps `stmt.kinds.dispatch` (grammar-layer inventory); the two lists differ in granularity, not substance.

Evidence: `src/compiler/sema_stmt.cpp#L296-L336`

## Expression statements

### `stmt.expr.trailing-semicolon` — expression statement vs tail expression

An expression in statement position is terminated by `;` (EXPR_STMT), whereas a block's final expression in tail position (TAIL_EXPR) carries no trailing `;`.

Evidence: `src/compiler/sema_render.cpp#L724-L733`

### `stmt.expr.discarded-rvalue-dropped` — discarded statement-expression rvalue runs its destructor

A statement-expression `e;` that produces a fresh owned move-typed value (a non-place rvalue, e.g. `make(p);`) is bound to a synthetic local and dropped at the end of the statement. A bare place expression (`existing_var;`) is not re-dropped, avoiding double-drop against its scope drop.

Evidence: `src/compiler/sema_stmt.cpp#L337-L369`

## Block statements

### `stmt.block.scoping-and-unsafe` — block statement and unsafe block at statement position

A bare brace-delimited block `{ stmts... }` is a valid statement (scoping block), and `unsafe { stmts... }` wraps a block with the unsafe modifier at statement position; both contain a sequence of statements.

Evidence: `src/compiler/sema_render.cpp#L894-L927`

### `stmt.block.shadow-restore-on-exit` — `{ }` block restores shadowed bindings on exit

A `{ }` block statement executes unconditionally. On exit, all name→storage bindings that existed BEFORE the block (scope map, struct/tuple/tagged-enum/dyn-trait/raw-dyn shape tags, local-pointer aliases) are restored to their pre-block values, so a name shadowed by a `let` inside the block reverts to its outer binding after the block. Names newly introduced inside the block are left in place (harmlessly inert, never referenced afterward).

Evidence: `src/compiler/mlir_gen_stmt.cpp#L447-L470`

### `stmt.block.dead-code-after-terminator` — Dead code after a hard terminator warns

While lowering a block's statement list, if the previously-lowered statement is a hard terminator (Return/Break/Continue) and the current AST statement is not an annotation node, block-lowering emits a "unreachable code after terminator" warning; the warning fires at most once per block (further statements in the same trailing tail are not re-warned).

Evidence: `src/compiler/sema_stmt.cpp#L671`; `src/compiler/sema_stmt.cpp#L679-L696`

### `stmt.block.scope-drops-at-exit` — Block lowering pushes/pops a lexical scope and drops at normal exit

lower_block pushes a new lexical scope on entry and pops it on exit; when the block's last lowered statement is not Return/Break/Continue, drops for every live local owned by this scope are appended (in declared order) immediately before the block's normal fall-through/end.

Evidence: `src/compiler/sema_stmt.cpp#L664`; `src/compiler/sema_stmt.cpp#L739-L754`

## Statement scopes and temporaries

### `stmt.scope.temp-drop-at-stmt-end` — statement-scoped temporaries dropped at end of statement

Fresh owned (droppable) temporaries materialized while lowering a statement live to the end of that statement and have their destructors run there, in REVERSE order of creation (Rust temporary-scope semantics). Place/borrow expressions (VarRef, FieldRead, IndexRead, Deref, TupleIndex, SliceIndex, SlicePtr, AddrOf, AddrOfTemp) are not hoistable temporaries; only rvalue-producing kinds (Call, MethodCall, StructLit, EnumLitData, …) are.

Evidence: `src/compiler/sema_stmt.cpp#L230-L294`

### `stmt.scope.return-temps-dropped-before-terminator` — temporaries in a return value drop before the return terminator

When `return <val>` materializes statement-scoped temporaries, the value is bound to a synthetic local while the temporaries live, the temporaries are then dropped, and only afterward does the return terminator execute — so drops precede the terminator rather than being dead code past it.

Evidence: `src/compiler/sema_stmt.cpp#L274-L291`

## Unsafe blocks

### `stmt.unsafe-block.context` — unsafe block establishes an unsafe context for its body

An `unsafe { ... }` block lowers its body with an unsafe context active for the duration of the block, restoring the prior context afterward; the body is otherwise an ordinary block.

Evidence: `src/compiler/sema_stmt.cpp#L643-L651`

## `let` bindings

### `stmt.let.forms` — let bindings

`let` supports: tuple destructure `let (a,b) [: T] = e;`, `let ref x [: T] = e;` (sugar for `let x = &e;`), `let mut x [: T] [= e];` (mutable, type-only declaration without init allowed when typed), `let x [: T] [= e];`, and `let PAT = e;` (irrefutable full pattern, refutability checked by sema).

Evidence: `tools/peg_gen/grammars/logos.peg#L2246-L2275`

### `stmt.let.binding-form` — let binding surface form

A let statement has the form `let [mut] NAME [: TYPE] = VALUE ;`: the `mut` keyword is present iff the binding is declared mutable, the `: TYPE` ascription is optional, and an initializer expression VALUE and trailing `;` are always present.

> **Uncertainty:** Inferred from the canonical source-rendering; renderer always emits an initializer, suggesting let without `=` is not a form handled here.

Evidence: `src/compiler/sema_render.cpp#L694-L709`

### `stmt.let.declare-without-init` — let without initializer

`let v: T;` (no initializer) allocates storage sized/typed by the annotated T and leaves it uninitialized; a later assignment writes through that same storage slot.

```logos
let v: i64;
```

Related: `stmt.let.fresh-decl-resets-drop-state`

Evidence: `src/compiler/mlir_gen_stmt.cpp#L1421-L1436`

### `stmt.let.fresh-decl-resets-drop-state` — Re-declaration resets init/drop state

A fresh `let` declaration of a name resets any stale initialized/uninitialized drop-tracking state left by an earlier same-named binding earlier in the same sequential scope (e.g. shadowing a prior `let` of the same name, or a declaration re-executed on each loop iteration, starts a new drop-elaboration lifecycle for the name).

Related: `stmt.let.declare-without-init`

Evidence: `src/compiler/mlir_gen_stmt.cpp#L1437-L1439`

### `stmt.let.uninit-drop-flag-vs-static` — Conditional init tracked by runtime drop flag

For a declare-without-init binding: if every later assignment statically dominates the binding's uses (single control-flow path to definition), initialization is tracked statically at compile time and drops are placed unconditionally (no runtime state). If assignment is only conditionally reachable (e.g. inside an `if`/loop), a runtime i8 drop-flag is allocated and initialized to 0 at the declaration site (re-initialized on each entry, so a declaration inside a loop body resets it every iteration); the flag governs both drop-before-replace and scope-exit drop.

Evidence: `src/compiler/mlir_gen_stmt.cpp#L1440-L1454`

### `stmt.let.struct-lit-alloca` — Struct-literal let binds directly to the literal's storage

`let name = StructName { ... };` binds `name` directly to the storage allocated for the struct literal (no extra copy); the binding's runtime struct-type key is taken from the let's own type annotation when present, else from the literal's struct name.

Evidence: `src/compiler/mlir_gen_stmt.cpp#L1478-L1486`

### `stmt.let.tuple-lit-binds-value` — Tuple-literal let binds the literal's value directly

`let name = (a, b, ...);` binds `name` directly to the value produced by tuple-literal codegen; `name` is tracked as a tuple-place binding (not a scalar).

Evidence: `src/compiler/mlir_gen_stmt.cpp#L1511-L1518`

### `stmt.let.tuple-value-spill-if-byvalue` — Tuple-typed let spills a by-value aggregate result before binding

`let name: (T1, T2, ...) = expr;` where expr's type is a tuple: if the produced value is already a pointer (e.g. a tuple-literal or a variable reference), `name` binds to it directly; if it is a by-value aggregate (e.g. a function-call return), it is first spilled into a freshly allocated stack slot and `name` binds to that slot's pointer.

Evidence: `src/compiler/mlir_gen_stmt.cpp#L1520-L1537`

### `stmt.let.closure-binds-pointer` — Closure-typed let binds the closure pointer directly

`let name: <closure type> = expr;` binds `name` directly to the pointer value produced by closure codegen, tracked as a tuple-like place binding.

Evidence: `src/compiler/mlir_gen_stmt.cpp#L1540-L1547`

### `stmt.let.fnptr-scalar-slot` — Function-pointer-typed let stores into a dedicated scalar slot

`let name: fn(T) -> R = expr;` stores the function-pointer value into a dedicated pointer-sized scalar alloca; `name` is a plain scalar (pointer-typed) binding. A bare function-item reference (the per-instantiation zero-sized value produced by referencing a plain fn) lowers identically to a function pointer at this site — same representation, same binding path.

Evidence: `src/compiler/mlir_gen_stmt.cpp#L1549-L1561`

### `stmt.let.slice-value-copy-semantics` — Slice/str let bindings are independent Copy values, never aliases

Slice and str values are Copy {ptr,len} pairs. `let name: <slice or str type> = expr;`: if expr yields a by-value {ptr,len} struct not yet in storage, it is first spilled to a fresh alloca. If expr yields an existing slice/str PLACE pointer (e.g. a variable, or a field/method access returning `str`/`&[T]`), a fresh 16-byte {ptr,len} slot is allocated and the value is memcpy'd into it — `name` never aliases the source's storage. Consequently a later write through a `let mut` binding cannot clobber the source place, and a later write to a mutable source cannot leak into an already-bound copy.

```logos
let mut r: str = a; r = "x";
```

Evidence: `src/compiler/mlir_gen_stmt.cpp#L1564-L1595`

### `stmt.let.enum-value-copy-semantics` — Tagged-enum let bindings copy the payload footprint into fresh storage

For a tagged-enum-typed `let name = expr;`: whatever value/pointer expr's codegen returns, the enum's inline {discriminant,payload} footprint is memcpy'd (sized to the enum's finalized layout) into a freshly allocated slot, and `name` binds to that fresh slot — `name` never aliases the source place (e.g. a payload-extract or a plain variable copy). A by-value aggregate return is stored whole into the fresh slot first; a bare discriminant scalar (e.g. a no-payload variant) is written via the discriminant-store path. Move-only double-free safety across the (no longer live) source is enforced separately by borrow-checking, which marks the source's drop as skipped.

Evidence: `src/compiler/mlir_gen_stmt.cpp#L1597-L1635`

### `stmt.let.copy-rebind-independent` — let binding from a place is an independent copy

`let name: S = y;` for struct-typed S: if `y` evaluates to an existing struct pointer (a place — e.g. `let copy = orig;`), a fresh slot is always allocated and the struct's bytes memcpy'd into it, rather than aliasing `y`'s storage — so a later mutation through `name` (for `impl Copy` structs) never observably mutates `orig`; for move-only structs the borrow checker separately forbids further use of `orig`, making the copy redundant but harmless.

Related: `stmt.let.array-rebind-copy`, `stmt.let.slice-value-copy-semantics`, `stmt.let.enum-value-copy-semantics`

Evidence: `src/compiler/mlir_gen_stmt.cpp#L1637-L1668`

### `stmt.let.mut-ref-independent-slot` — Mutable reference/pointer let bindings get their own slot; immutable ones alias

`let mut r: &S | &mut S | *mut S = expr;` (a MUTABLE binding of reference/raw-pointer-to-struct type) allocates its own pointer-sized storage slot, distinct from the pointee's storage, and stores the reference/pointer value into it — so a later reassignment `r = &s2;` overwrites only `r`'s own slot and never corrupts the pointee `s1`'s storage. An IMMUTABLE `let r: &S = expr;` binding instead binds directly to the referent's address (no separate slot allocated) since `r` can never be reassigned.

```logos
let mut r = &s1; r = &s2;
```

Evidence: `src/compiler/mlir_gen_stmt.cpp#L1672-L1701`; `src/compiler/mlir_gen_stmt.cpp#L1803-L1824`

### `stmt.let.array-rebind-copy` — Array let-rebind copies whole array

`let b: [T; N] = a;` where `a` evaluates to a pointer to an existing array place memcpy's the array's whole byte footprint into `b`'s freshly allocated array slot (a plain pointer store would overwrite only the first machine word of the destination, leaving the remainder of the array uninitialized).

```logos
let b: [i32; 4] = a;
```

Related: `stmt.let.copy-rebind-independent`

Evidence: `src/compiler/mlir_gen_stmt.cpp#L1890-L1908`

### `stmt.let.scalar-coercion` — Scalar let coerces RHS to annotated type

A scalar `let` binding's initializer value is passed through integer coercion (widen/narrow/sign-adjust) then float coercion to the binding's declared scalar type before being stored into its slot.

Evidence: `src/compiler/mlir_gen_stmt.cpp#L1909-L1911`

### `stmt.let.destructure-bindings-leak` — Destructuring `let` bindings leak into the enclosing scope

A destructuring `let` (`let Pair{a,b} = e`, `let (a,b) = e`) lowers to a synthetic transparent block whose first statement is a compiler temp named with the `__dst`/`__destruct` prefix. Such a block is EXEMPTED from the normal end-of-block shadow-restore, so its field bindings — including ones that shadow a same-named binding from an enclosing scope — leak into (persist in) the enclosing scope after the destructuring `let`.

Evidence: `src/compiler/mlir_gen_stmt.cpp#L424-L446`

### `stmt.let.uninit-drop-flag-when-conditional-init` — Uninitialized `let` needs a runtime drop flag iff conditionally assigned

A `let mut x: T;` declared WITHOUT an initializer requires a runtime drop flag iff `x` is later assigned at a conditional/loop nesting depth STRICTLY DEEPER than its declaration depth (the initialized/uninitialized state is then not statically known at scope exit). A bare `{ }` block does not increase nesting depth (it runs unconditionally); each of `if`/`while`/`loop`/`for`/`for-each` body, each `match` arm body, and a `let-else` else-block adds one level of depth. A var assigned only at its declaration depth is drop-flag-free (its init state is static).

Evidence: `src/compiler/mlir_gen_stmt.cpp#L310-L353`

### `stmt.let.definite-assignment` — Use of possibly-uninitialised binding is an error

A variable declared without an initializer (`let mut x: T;`) is tracked as currently-uninitialised; reading it before assignment is a hard error. First assignment marks it initialised. At a CFG merge the post-state is uninit if uninit on any non-diverging incoming path; diverging branches (tail return/break/continue/panic) contribute nothing; loops are conservative (body-only assignments do not initialise the outer scope). Closures get their own saved/restored tracker.

Evidence: `src/compiler/sema_impl.hpp#L1897-L1906`

### `stmt.let.no-drop-before-replace-uninit` — Reassigning a declared-but-uninitialized binding never drops the old value

A variable declared without an initializer (`let mut x: T;`) is not definitely-initialized at a later reassignment (a conditional path may have left it uninitialized), so reassignment must NOT drop the prior value first (that would drop garbage). A variable declared WITH an initializer is definitely-initialized at every subsequent reassignment (branches don't de-initialize) and is safe to drop-before-replace.

Evidence: `src/compiler/sema_impl.hpp#L1887-L1896`

### `stmt.let.inferred-annotation-deferred` — Top-level `_` annotation defers to RHS type

A top-level placeholder annotation `let x: _ = rhs` drops the annotation entirely and adopts the RHS type. A nested `_` inside a composite annotation (`Vec<_>`) is a hole filled from the RHS during inference rather than dropped.

Evidence: `src/compiler/sema_stmt.cpp#L1713-L1721`; `src/compiler/sema_stmt.cpp#L2179-L2183`

### `stmt.let.annotation-type-hints` — let annotation supplies a type hint to RHS inference

A non-hole let annotation hints RHS literal/inference: enum/struct annotations with type-args, fn-ptr/closure annotations (so untyped closure params infer), array/slice element types (incl. through `&[T]`/`&mut [T]`), and tuple element types (so untyped int literals widen to the annotated element type instead of defaulting).

```logos
let f: fn(i64)->i64 = |x| x+1;
let p:(i64,i64) = (7, 2);
```

Evidence: `src/compiler/sema_stmt.cpp#L1723-L1771`

### `stmt.let.ref-binding-sugar` — `let ref y = x` is sugar for `let y = &x`

A `ref` binding `let ref y = x` (or `let ref y: T = x`) lowers to taking the address of the RHS, giving `y` type `&T`.

Evidence: `src/compiler/sema_stmt.cpp#L1897-L1931`

### `stmt.let.declare-uninit` — let without initializer declares an uninitialised binding

`let v: T;` / `let mut v: T;` (annotation, no value) declares the binding with the annotated type and no value; the binding is recorded as declared-uninitialised so a later assignment registers the value without a drop-before-replace, and the variable must be assigned before use. `let` without value and without annotation is an error.

Evidence: `src/compiler/sema_stmt.cpp#L1942-L1962`

### `stmt.let.closure-capture-drop-ownership` — A closure-RHS let owns its captures' drop slots

A let whose RHS is a closure literal owns the drop slots of that closure's (un-skipped) captures; they are dropped together with the binding, in capture order.

Evidence: `src/compiler/sema_stmt.cpp#L2210-L2222`

## `let` with irrefutable patterns

### `stmt.let-pat.struct-shapes-only` — let `<pattern>` = expr accepts only shapes provable irrefutable here

`let <pattern> = expr;` accepts only pattern shapes this lowering can prove irrefutable: plain struct patterns, tuple-struct patterns (rewritten via synthesized "0","1",... field names), fixed-size array patterns whose element count matches the array length exactly (no rest), and struct-shaped single-variant-enum patterns; any other pattern shape at this position is rejected with a diagnostic directing the user to `match`/`let-else`.

> **Divergence from Rust:** B4

Evidence: `src/compiler/sema_stmt.cpp#L1079-L1136`

### `stmt.let-pat.struct-name-match` — Struct-pattern let requires exact struct-kind and name match

A struct-pattern `let S{...} = rhs;` requires rhs's static type to be Struct/ZonedStruct AND the struct's name to equal the pattern's name S; a kind or name mismatch is a compile error.

Evidence: `src/compiler/sema_stmt.cpp#L1407-L1418`

### `stmt.let-pat.unknown-field` — Struct destructuring-let rejects unknown field names

Each field named in a struct destructuring-let pattern must exist on the (possibly generic-substituted) matched struct's field list; a name absent from the field list is a compile error.

Evidence: `src/compiler/sema_stmt.cpp#L1513-L1517`

### `stmt.let-pat.field-binding-forms` — Struct destructuring-let field entries: shorthand, rename, nested struct

A struct destructuring-let field entry binds via shorthand (`f` -> local binding named `f`) or a plain-identifier rename (`f: x` -> local binding named `x`); a nested struct sub-pattern (`f: T{...}`) recurses, requiring the sub-pattern's struct name to equal the field's struct type's name; any other sub-pattern form in field-value position is rejected as unsupported at this lowering.

Evidence: `src/compiler/sema_stmt.cpp#L1499-L1512`; `src/compiler/sema_stmt.cpp#L1544-L1563`

### `stmt.let-pat.consumes-source` — Struct destructuring-let marks both source and spill-temp moved

A struct destructuring-let that moves the matched value marks BOTH the original source place moved (suppressing its own scope-exit drop) AND the temporary holding the spilled value moved (suppressing that temp's drop too), since ownership of every field has transferred into the individual field bindings.

Evidence: `src/compiler/sema_stmt.cpp#L1471-L1474`; `src/compiler/sema_stmt.cpp#L1567-L1572`

### `stmt.let-pat.union-requires-unsafe` — Irrefutable let-pattern over a union requires unsafe + exactly one field

An irrefutable `let U{f} = u;` pattern over a union type reads memory through the named field (the same hazard as a `match` arm over a union) and is a compile error unless lowering occurs inside an `unsafe` block; the pattern must additionally name exactly one field and must not contain a `..` rest, each violation reported separately.

Evidence: `src/compiler/sema_stmt.cpp#L1419-L1466`

### `stmt.let-pat.array-fixed-no-rest` — let [p0,p1,...] = arr requires exact fixed-length match, no rest

`let [p0, p1, ...] = arr;` is treated as irrefutable, and thus legal at `let`, only when arr's static type is a fixed-size Array and the pattern's element count equals the array length exactly; a `..` rest in this position is a compile error (refutable-shape restriction), and an element-count mismatch is a compile error. Each element position must bind a plain identifier or `_` to skip — any other element-pattern shape is a compile error.

> **Divergence from Rust:** B4

Evidence: `src/compiler/sema_stmt.cpp#L1120-L1136`; `src/compiler/sema_stmt.cpp#L1238-L1269`; `src/compiler/sema_stmt.cpp#L1286-L1317`

### `stmt.let-pat.tuple-struct-rest` — let Foo(p0,p1,...) = rhs: single rest, arity, position mapping

`let Foo(p0, p1, ...) = rhs;` over a tuple struct requires rhs's type to be exactly Struct `Foo`; at most one `..` rest is allowed among the positional bindings, the named-binding count must not exceed the struct's field arity, and — mirroring match-arm tuple-struct patterns — names before the rest map to low field positions while names after map to the trailing positions. Each position must bind a plain identifier or `_` to skip; any other pattern shape at a position is a compile error, as is a final field-count mismatch after rest expansion.

Evidence: `src/compiler/sema_stmt.cpp#L1322-L1406`

### `stmt.let-pat.single-variant-enum-struct` — let E::V{...} = rhs is irrefutable only for a single-variant struct-shaped enum

`let E::V{f1, f2, ...} = rhs;` is treated as irrefutable, and thus legal at `let`, only when enum E has exactly one variant and that variant V is struct-shaped; it lowers to one synthetic `match` with a single irrefutable arm per requested binding, each projecting one payload field by position (other payload positions bound to `_`). Each pattern field must name an existing payload field of V (else compile error) and bind via shorthand or a plain-identifier rename — any other field-value pattern form is a compile error.

Evidence: `src/compiler/sema_stmt.cpp#L1086-L1119`; `src/compiler/sema_stmt.cpp#L1137-L1237`

## Tuple destructuring `let`

### `stmt.let-destruct.tuple-required` — let (...) = rhs requires a tuple-typed rhs

`let (p0, p1, ...) = rhs;` requires rhs's static type to have Tuple kind; any other kind is a compile error ("right-hand side must be a tuple, got `<T>`") and lowering degrades to a bare expression statement.

Evidence: `src/compiler/sema_stmt.cpp#L758-L767`

### `stmt.let-destruct.rest-and-arity` — Tuple destructuring-let: single `..` rest, arity checks, position mapping

In a tuple destructuring-let binding list: at most one `..` rest element is allowed (else error); without a rest the binding-list length must equal the tuple arity exactly (else "expected N bindings, got M"); with a rest, the named-binding count must not exceed the arity (else "N bindings exceed tuple arity M"). Names before the rest bind positions 0.. in order; names after the rest bind the trailing `arity - trailing_count ..` positions, so the rest absorbs the unmatched middle.

Evidence: `src/compiler/sema_stmt.cpp#L800-L820`; `src/compiler/sema_stmt.cpp#L832-L846`

### `stmt.let-destruct.nested-tuple` — Tuple destructuring-let binds nested tuple sub-patterns recursively

A tuple destructuring-let binding-list element may itself be a nested tuple binding list (`PAT_TUPLE` with `NAMES`), recursively bound against that position's tuple-typed element, closing `let (a, (b, c)) = ...;` (and deeper nesting) over arbitrary depth.

Evidence: `src/compiler/sema_stmt.cpp#L790-L793`; `src/compiler/sema_stmt.cpp#L850-L852`

### `stmt.let-destruct.binding-uniqueness` — Tuple destructuring-let binding names must be pairwise distinct

All leaf binding names introduced across an entire (possibly nested) tuple destructuring-let must be pairwise distinct across the whole pattern; a repeated name is a compile error in the `let (...) destructure` binding context.

Evidence: `src/compiler/sema_stmt.cpp#L793`; `src/compiler/sema_stmt.cpp#L855`; `src/compiler/sema_stmt.cpp#L869-L872`

### `stmt.let-destruct.move-on-bind` — Tuple destructuring-let marks each consumed source place moved

Destructuring a move-typed source marks the source place moved at every level it is consumed: the original rhs expression when spilled into the top temporary, each nested level's source place when spilled into its own temporary, and each leaf element's source expression when bound to a name — so no level's temporary double-frees a value now owned by a deeper binding. The move-marking helper self-gates to VarRef/FieldRead/TupleIndex places, so a tuple-literal rhs is a no-op.

Evidence: `src/compiler/sema_stmt.cpp#L771-L778`; `src/compiler/sema_stmt.cpp#L821-L826`; `src/compiler/sema_stmt.cpp#L856-L859`

## `let`-`else`

### `stmt.let-else.form` — let-else statement

`let PAT = expr else { block };` binds a refutable pattern; on match failure the else block runs (which must diverge).

Evidence: `tools/peg_gen/grammars/logos.peg#L2242-L2244`

### `stmt.let-else.refutable-binding-diverging-else` — let-else binds on match, else block must diverge

`let Pat = expr else { block };` tests `Pat` against `expr`: on match the pattern's bindings enter the enclosing scope and control falls through; on failure the else block runs and must diverge (terminate with return/unreachable). A named-wildcard pattern binds the value directly with no test; a unit variant tests the discriminant only; a data-carrying variant tests the discriminant and extracts payload bindings.

Evidence: `src/compiler/mlir_gen_stmt.cpp#L4763-L4784`

### `stmt.let-else.scrutinee-tested-then-bind-or-diverge` — let-else evaluates scrutinee, tests pattern, binds on match else runs diverging block

`let PAT = EXPR else { BLOCK }` evaluates EXPR once, tests it against PAT; on match the pattern's bindings enter the enclosing scope and control continues; on mismatch BLOCK runs. BLOCK must diverge: if it does not terminate, control is unreachable.

Evidence: `src/compiler/mlir_gen_stmt.cpp#L4812-L4813`; `src/compiler/mlir_gen_stmt.cpp#L4955-L4962`; `src/compiler/mlir_gen_stmt.cpp#L5027-L5028`

### `stmt.let-else.wildcard-irrefutable` — Wildcard/binding let-else pattern always matches

When PAT is a wildcard or a bare binding (not `_`), the pattern is irrefutable: it always matches and binds the scrutinee value; the else block is unreachable.

Evidence: `src/compiler/mlir_gen_stmt.cpp#L4816-L4837`

### `stmt.let-else.enum-pattern-refutable` — Enum variant pattern in let-else is refutable on discriminant

For an enum scrutinee, a `Variant`/`VariantData` pattern matches iff the scrutinee's discriminant equals the pattern variant's discriminant; otherwise the else block runs. A C-like (all-nullary) enum scrutinee is its own discriminant value.

Evidence: `src/compiler/mlir_gen_stmt.cpp#L4859-L4892`; `src/compiler/mlir_gen_stmt.cpp#L4902-L4920`; `src/compiler/mlir_gen_stmt.cpp#L4877-L4885`

### `stmt.let-else.or-pattern-disjunction` — Or-pattern in let-else matches any alternative

`let A(x) | B(x) = v else …` matches iff the scrutinee discriminant equals any alternative's discriminant (logical OR of per-alt tests). All alternatives must bind the same names at the same payload layout; bindings are extracted using the first alternative's payload.

Evidence: `src/compiler/mlir_gen_stmt.cpp#L4793-L4809`; `src/compiler/mlir_gen_stmt.cpp#L4906-L4913`

### `stmt.let-else.literal-pattern-refutable` — Literal/bool/range pattern in let-else is refutable

For a non-enum integer scrutinee, an `Int` or `Bool` pattern matches iff the scrutinee equals the literal; a `Range` pattern (`lo..=hi`) matches iff `lo <= scrut <= hi` (signed). On mismatch the else block runs.

Evidence: `src/compiler/mlir_gen_stmt.cpp#L4921-L4950`

### `stmt.let-else.tuple-pattern-irrefutable` — Tuple pattern in let-else is irrefutable

A tuple pattern in let-else is always irrefutable: it unconditionally matches and binds each non-`_` element field to the corresponding scrutinee tuple element.

Evidence: `src/compiler/mlir_gen_stmt.cpp#L4925-L4926`; `src/compiler/mlir_gen_stmt.cpp#L4969-L4991`

### `stmt.let-else.match-ergonomics-autoderef` — let-else auto-derefs reference scrutinee for enum patterns

When the scrutinee has type `&Enum`, `&mut Enum`, or `*Enum`, the enum variant pattern is tested against the referenced enum (match-ergonomics auto-deref), so `let Some(v) = &opt else …` behaves like a by-value match.

Evidence: `src/compiler/mlir_gen_stmt.cpp#L4845-L4858`

### `stmt.let-else.inner-refutable-guard` — Refutable sub-patterns in let-else add value guards

A refutable inner sub-pattern (e.g. `let Some(1) = … else`) lowers to a guard test on the bound payload after the variant discriminant matches; if any guard fails the else block runs. The match succeeds only when the discriminant test and all guards hold.

Evidence: `src/compiler/mlir_gen_stmt.cpp#L5005-L5024`

### `stmt.let-else.diverging-else` — let-else else-block must diverge

In `let PAT = EXPR else { BLOCK };` the else BLOCK must unconditionally diverge (end in return / break / continue / panic / `loop {}`); a non-diverging else-block is a compile error.

```logos
let Some(x) = opt else { return; };
```

Evidence: `src/compiler/sema_stmt.cpp#L1604-L1614`

### `stmt.let-else.bindings-in-outer-scope` — let-else pattern bindings escape to the enclosing scope

Bindings introduced by the let-else pattern are defined in the enclosing (outer) scope and remain visible after the statement; the else-block is lowered in a separate nested scope. Each binding takes its type from the matched pattern position. Bindings named `_` are not introduced.

Evidence: `src/compiler/sema_stmt.cpp#L1604-L1662`; `src/compiler/sema_stmt.cpp#L1630-L1633`; `src/compiler/sema_stmt.cpp#L1648-L1649`

## Let chains

### `stmt.let-chain.nested-if-let-desugar` — `if let` chain desugars to nested if/if-let with duplicated else

An `if`/`if let` chain (segments joined by `&&`, mixing plain conditions and `let PAT = EXPR`) desugars to nested `if`/`if let` LIR, one nesting level per segment, matching rustc's classic chained-let expansion. The chain's `else` branch (if present) is duplicated at every fall-through nesting level, so side effects in `else` execute once per short-circuit exit rather than being deduplicated — an accepted consequence of the simple expansion.

Evidence: `src/compiler/sema_impl.hpp#L4124-L4130`

## Assignment

### `stmt.assign.place-forms` — assignment place forms

Assignment statements take the forms: `NAME = VALUE ;`, `NAME OP VALUE ;` (compound assign), `*NAME = VALUE ;` (deref write), `*NAME OP VALUE ;` (deref compound assign), `RECEIVER.FIELD = VALUE ;` (field write), and `RECEIVER.PATH... = VALUE ;` (chained field write through a dot-separated path).

Evidence: `src/compiler/sema_render.cpp#L739-L885`

### `stmt.assign.destructuring-into-places` — Destructuring assignment into existing places

Destructuring assignment `(a,b)=e` / `[a,b]=e` / `S{a,b}=e` writes into EXISTING places (not new bindings), desugared to `let tmp = rhs;` followed by per-place assignments.

> **Divergence from Rust:** RFC 2909 (Rust-conformant).

> **Note:** Overlaps `stmt.assign.destructure` and the `destructure-assign` group; all ids preserved.

Evidence: `tools/peg_gen/grammars/logos.peg#L311`

### `stmt.assign.simple` — Simple variable assignment

`name = expr;` assigns to a simple variable place.

Evidence: `tools/peg_gen/grammars/logos.peg#L2292-L2293`

### `stmt.assign.place` — General place assignment

`PLACE = expr;` where PLACE is an arbitrary postfix-chain lvalue (chained index `a[i][j]`, deref+tuple-index `(*p).0`, deeper mixes); sema computes the address and emits a deref-write. Tried after the specialized single/two-level write forms and after bare-variable assignment.

Evidence: `tools/peg_gen/grammars/logos.peg#L2295-L2304`

### `stmt.assign.destructure` — Destructuring assignment into existing places

Tuple `(a, b) = e;`, array `[a, b] = e;`, and struct `S { f, .. } = e;` destructuring assignment writes into existing places (RFC 2909). Parsed before expr-statements so a parenthesised/bracketed LHS followed by `=` is recognized.

> **Note:** Overlaps `stmt.assign.destructuring-into-places` and the `destructure-assign` group; all ids preserved.

Evidence: `tools/peg_gen/grammars/logos.peg#L2306-L2315`

### `stmt.assign.compound-place` — Compound assignment over any place

`PLACE op= expr` applies a compound assignment over an arbitrary place: a bare variable takes the simple-var path; any other place desugars to `place = place op rhs` (or an `*Assign` trait-method call). Bare-deref `*p op= v` is handled separately (it is not an atom).

Evidence: `tools/peg_gen/grammars/logos.peg#L2317-L2323`

### `stmt.assign.eval-rhs-before-drop-old` — Assignment order: eval RHS, drop old, then store

For `x = e` where `x` is a live (definitely-initialized) place of a droppable type: `e` is evaluated first, then the OLD value of `x` is dropped (running its full destructor: user `Drop` impl + owned children), then the new value is stored. This makes `x = f(x)` read the pre-assignment `x` safely.

Evidence: `src/compiler/mlir_gen_stmt.cpp#L2000-L2003`; `src/compiler/mlir_gen_stmt.cpp#L2049-L2051`; `src/compiler/mlir_gen_stmt.cpp#L2153`

### `stmt.assign.conditional-drop-flag` — Reassignment of a conditionally-initialized place drops via a runtime flag

If a place's liveness depends on control flow reaching it along some paths but not others (e.g. `if c { x = a; } x = b;`), the compiler maintains a per-place runtime 1-byte init flag; on `x = b` the old value is dropped only if the flag is nonzero at runtime, and the flag is then set to mark the place live for the new value.

Evidence: `src/compiler/mlir_gen_stmt.cpp#L2015-L2039`

### `stmt.assign.first-write-skips-drop` — First (dominating) assignment to a statically-uninitialized place skips the drop

For a place whose initialization state is known statically (not flag-tracked), the first assignment reached on the dominating path is known to overwrite indeterminate storage and does not run a drop; every subsequent assignment to that place unconditionally drops the prior (live) value first.

Evidence: `src/compiler/mlir_gen_stmt.cpp#L2040-L2048`

### `stmt.assign.undefined-var` — Assignment to undefined variable is an error

`x = e` where `x` is not bound is rejected: "assignment to undefined variable".

Evidence: `src/compiler/sema_stmt.cpp#L2538-L2546`

### `stmt.assign.static-mut-unsafe` — Writing a mutable static requires unsafe

Writing to a module-level `static mut` requires an enclosing `unsafe` block: outside unsafe it is rejected (Rust `items.static.mut.safety`). A local binding (or type parameter) shadowing the static's name reclassifies the write as a normal local assignment, suppressing the static-mut gate.

Evidence: `src/compiler/sema_stmt.cpp#L2547-L2566`

### `stmt.assign.immutable-var` — Assignment to immutable variable is an error

Assigning to a non-`mut` variable is rejected ("assignment to immutable variable"), except the single deferred-initialization write to a `let x: T;` declared without initializer.

Evidence: `src/compiler/sema_stmt.cpp#L2567-L2575`

### `stmt.assign.deferred-init-once` — Deferred initialization permits one write without mut

A non-`mut` local declared without an initializer (`let x: T;`) may be assigned exactly once; that first assignment initializes it. A second assignment to the same non-`mut` local is rejected.

Evidence: `src/compiler/sema_stmt.cpp#L2567-L2575`; `src/compiler/sema_stmt.cpp#L2756-L2762`

### `stmt.assign.enum-lit-hint-retype` — Assignment LHS pins enum-literal type

When the LHS variable has enum type, the RHS is lowered with that enum as the expected-type hint; an incompletely-typed generic enum literal RHS (no type-args, or any Error type-arg) is retyped to the LHS's concrete enum spec, provided the LHS is fully concrete and every known (non-error) literal type-arg already matches the LHS at its position (arity must match). A genuine type-arg mismatch is left for the compatibility check to reject.

Evidence: `src/compiler/sema_stmt.cpp#L2585-L2638`

### `stmt.assign.type-mismatch` — Assignment RHS type-compatibility

`x = e` requires `typeof(e)` compatible with `typeof(x)`; otherwise "assignment: type mismatch — expected T, got U". A `#[rel_ptr]` ↔ `*T` relation is also accepted.

Evidence: `src/compiler/sema_stmt.cpp#L2639-L2646`

### `stmt.assign.int-widen` — Implicit integer widening on assignment

On assignment to an integer variable, a non-literal non-enum integer RHS of a narrower integer kind that can widen safely to the LHS kind is implicitly widened.

> **Divergence from Rust:** Rust has no implicit integer widening on assignment.

Evidence: `src/compiler/sema_stmt.cpp#L2647-L2653`

### `stmt.assign.intlit-fits` — Integer-literal assignment must fit target type

An integer-literal RHS (including elements of array/tuple literals, recursively through nested arrays/tuples) must fit in the target's (element's) integer type; an out-of-range value is rejected: "value V does not fit in T".

Evidence: `src/compiler/sema_stmt.cpp#L2654-L2722`

### `stmt.assign.drop-before-replace` — Assignment drops the old value before overwrite

Assigning to a variable that currently holds a live droppable value runs that value's destructor before the store, and after evaluating the RHS (so `x = f(x)` is sound). The drop is suppressed when the variable was declared without an initializer (runtime drop-flag governs it instead) or is currently whole- or partially moved-out.

Evidence: `src/compiler/sema_stmt.cpp#L2723-L2754`

### `stmt.assign.reassign-revives` — Reassignment revives a moved variable

Assigning a new value to a variable clears its moved-out state, making it usable again; the RHS source, if a move-type, is marked moved so its scope-exit drop is suppressed.

Evidence: `src/compiler/sema_stmt.cpp#L2755-L2765`

### `stmt.assign.static-write-address` — Assignment to an unshadowed module static writes through its address

`STATIC = v` where `STATIC` names an unshadowed module-level static lowers to a deref-write through the static's global address (mut-qualified per its `static mut` declaration), using the canonical place-store conventions (struct memcpy, enum footprint, fat pairs), rather than the local-slot assign path used for ordinary variables.

Evidence: `src/compiler/sema_stmt.cpp#L2766-L2775`

## Compound assignment

### `stmt.compound-assign.deref-desugar` — `*p op= v` desugars to `*p = *p op v`

A deref-compound assignment `*p op= v` desugars to `*p = (*p op v)`, reading the pointee, applying the binary operator, and writing the result back.

Evidence: `src/compiler/sema_stmt.cpp#L516-L568`

### `stmt.compound-assign.deref-mut-dispatch` — compound deref-assign on a DerefMut struct dispatches deref_mut

When the left side of `*w op= v` is a struct (or zoned struct) implementing `DerefMut<T>`, the operation desugars through `w.deref_mut()` (yielding &mut T): `*(w.deref_mut()) = *(w.deref_mut()) op v`.

Evidence: `src/compiler/sema_stmt.cpp#L529-L552`

## Destructuring assignment

### `stmt.destructure-assign.tuple-array-struct` — Destructuring assignment supports tuple/array/struct rhs shapes

Destructuring assignment into existing places selects its rhs-shape requirement from the parsed op discriminant: tuple form (`(a,b) = e`, requires rhs Tuple), array form (`[a,b] = e`, requires rhs Array), struct form (`S{x:a,y} = e`, requires rhs Struct or ZonedStruct); a shape mismatch is a compile error (skipped when rhs is already Error-kind). The rhs is spilled once into a synthetic temporary, and each place is assigned an accessor read off that temporary (tuple-index / index-read / field-read respectively).

Evidence: `src/compiler/sema_stmt.cpp#L879-L897`; `src/compiler/sema_stmt.cpp#L924-L929`; `src/compiler/sema_stmt.cpp#L1038-L1045`

### `stmt.destructure-assign.place-checks` — Destructuring-assignment places must be existing mutable locals

Each destructuring-assignment place is either `_`/empty (discarded — its accessor is still evaluated as a statement for side effects) or an existing variable name; assigning to an undefined name or to a non-`mut` variable is a compile error. A successful assignment clears the target from the currently-uninitialized set exactly as a scalar assignment would, even though this lowering path bypasses the ordinary assignment entry point.

Evidence: `src/compiler/sema_stmt.cpp#L905-L920`

### `stmt.destructure-assign.rest-and-redundant-parens` — Destructuring assignment: redundant-paren unwrap + rest/arity rules

In a tuple/array destructuring-assignment place-list: (a) a place-list of exactly one nested-tuple place, when the source arity is not 1, is treated as redundant parens and the inner list is bound directly (`((a,b)) = e` is equivalent to `(a,b) = e`); (b) at most one `..` rest place is allowed; without one the place-count must equal the source arity, with one the named-place count must not exceed it, and the rest absorbs the unmatched middle exactly as for destructuring-let.

Evidence: `src/compiler/sema_stmt.cpp#L968-L998`; `src/compiler/sema_stmt.cpp#L1007-L1013`

## Dereference writes

### `stmt.deref-write.pointer-or-mutref` — deref-write/compound left side must be pointer or mutable reference

The left side of `*p = v` (and `*p op= v`) must be a pointer or a mutable reference; otherwise it is an error.

Evidence: `src/compiler/sema_stmt.cpp#L553-L557`; `src/compiler/sema_stmt.cpp#L629-L631`

### `stmt.deref-write.raw-ptr-unsafe` — writing through a raw pointer requires unsafe

Writing through `&mut T` is safe; writing through a raw pointer (`*mut`/`*const`) requires an unsafe context. Outside unsafe, a raw-pointer deref-write/compound is an error.

Evidence: `src/compiler/sema_stmt.cpp#L558-L560`; `src/compiler/sema_stmt.cpp#L625-L628`

### `stmt.deref-write.const-ptr-readonly` — cannot write through a *const pointer

A `*const T` pointer is read-only; writing through it is an error — only `*mut T` or `&mut T` may be written through.

Evidence: `src/compiler/sema_stmt.cpp#L561-L562`; `src/compiler/sema_stmt.cpp#L632-L634`

### `stmt.deref-write.user-deref-mut` — `*x = v` on a DerefMut struct dispatches deref_mut

When `*x = v` is applied to a struct `x` implementing `DerefMut<T>`, it dispatches `x.deref_mut()` (returning &mut T) and writes `v` through the resulting reference; this requires `x` to be a mutable binding for `&mut self` materialization.

Evidence: `src/compiler/sema_stmt.cpp#L597-L624`

### `stmt.deref-write.variance-invariant` — deref-write value must invariantly match the pointee type

In `*ptr = val` the value's type must invariant-match (strict, fn-scope-fixed lifetimes) the pointee type of the pointer/reference.

Evidence: `src/compiler/sema_stmt.cpp#L635-L639`

### `stmt.deref-write.rhs-type-hint` — deref-write RHS inferred against the pointee type

The RHS of `*p = v` is inferred with an enum/struct type hint taken from the pointee type when the pointee is a parameterized enum or struct, so a bare `None` resolves to `Option<T>` matching the slot rather than a discriminant-only constant.

Evidence: `src/compiler/sema_stmt.cpp#L575-L595`

## `if` statements

### `stmt.if.diverging-cond-truncates` — a diverging if-condition makes the if body unreachable

If evaluating an `if` condition diverges (already terminates control flow, e.g. `if (return x) {}`), no part of the `if`/`else`/merge is reachable and the statement produces no further code.

Evidence: `src/compiler/mlir_gen_stmt.cpp#L2162-L2168`

### `stmt.if.both-branches-diverge-no-merge` — if where both branches diverge has no fall-through

An `if`/`else` where neither branch falls through (both diverge/terminate) has no merge point; control after the `if` is unreachable.

Evidence: `src/compiler/mlir_gen_stmt.cpp#L2180-L2195`

### `stmt.if.cond-must-be-bool` — if condition must be bool

A non-pattern `if` condition must have type bool; Error and Never types are accepted (Never permits `if return x {}`), any other type is a compile error.

Evidence: `src/compiler/sema_stmt.cpp#L5955-L5961`

### `stmt.if.move-merge-by-branch` — Per-branch move state merges by union over non-diverging branches

Across an `if`'s branches each branch is analyzed from the pre-if move state; the post-if moved set is the union of moves from all non-diverging branches (a branch ending in return/break/continue diverges and contributes nothing). A missing then/else branch behaves as a non-diverging fall-through preserving the pre-if state.

Evidence: `src/compiler/sema_stmt.cpp#L5966-L6038`

### `stmt.if.definite-assignment-merge` — Definite-assignment merge across if branches

A variable is uninitialized at the if's merge point iff it is uninitialized on ANY incoming non-diverging branch path (union of currently-uninit sets over non-diverging branches); diverging branches contribute nothing to the merge.

Evidence: `src/compiler/sema_stmt.cpp#L5980-L6040`

### `stmt.if.no-trailing-semi` — if at statement position needs no trailing semicolon

An `if` used in statement position requires no trailing `;` because it is a brace-bounded expression.

Evidence: `src/compiler/sema_render.cpp#L887-L892`

## `if let`

### `stmt.if-let.desugar-to-match` — if-let desugars to a two-arm match

`if let P = e { THEN } else { ELSE }` lowers to `match e { P => THEN, _ => ELSE }`, with nested-payload destructures emitted before THEN so their bindings are in scope.

Evidence: `src/compiler/sema_stmt.cpp#L5843-L5951`

### `stmt.if-let.let-chain-trailing-cond` — let-chain trailing condition becomes a match-arm guard

`if let P = e && <cond> { THEN } else { ELSE }` desugars to `match e { P if <cond> => THEN, _ => ELSE }`: the chain condition becomes an arm guard, sees the pattern's bindings, must be bool (else Error accepted), and is conjoined AFTER the pattern's own refutable guards.

Evidence: `src/compiler/sema_stmt.cpp#L5872-L5903`; `src/compiler/sema_stmt.cpp#L5938-L5945`

### `stmt.if-let.guard-no-nested-variant-binding` — let-chain condition cannot reference nested enum-variant bindings

A let-chain trailing condition may reference nested tuple/struct payload bindings (re-extracted as a guard prologue) but may NOT reference bindings from a nested enum-variant payload pattern; doing so is a compile error.

Evidence: `src/compiler/sema_stmt.cpp#L5885-L5902`

### `stmt.if-let.refutable-inner-guards` — Nested refutable payload predicates gate the then-arm

Refutable inner patterns (nested variant/literal payload predicates) of an if-let pattern are conjoined into the then-arm guard; a predicate failure falls through to the wildcard else-arm.

Evidence: `src/compiler/sema_stmt.cpp#L5928-L5937`

## `match` statements

### `stmt.match.scrutinee-form` — Match statement

`match SCRUT { ARM* }` matches a scrutinee against arms. A bare-identifier scrutinee is parsed specially (as a var-ref) so `match e { ... }` does not mis-parse `e {` as a struct literal; complex scrutinee expressions fall through to the general expr form.

Evidence: `tools/peg_gen/grammars/logos.peg#L1918-L1928`

### `stmt.match.arm` — Match arm with optional guard

A match arm is `PAT [if GUARD] => BODY`, where BODY is a block, an expression, or a statement, with an optional trailing comma. The optional `if GUARD` is a guard expression gating the arm.

Evidence: `tools/peg_gen/grammars/logos.peg#L1930-L1941`

## `loop`

### `stmt.loop.forms` — loop and labeled loop

An infinite loop is `loop BLOCK`; a labeled loop is `'LABEL: BLOCK` where labels use a leading single-quote sigil.

Evidence: `src/compiler/sema_render.cpp#L792-L804`

### `stmt.loop.infinite` — loop block

`loop { ... }` is an unconditional loop.

Evidence: `tools/peg_gen/grammars/logos.peg#L1888-L1894`

### `stmt.loop.labeled` — Labeled loop

`'label: for/while/loop { ... }` attaches a lifetime-syntax label to a loop, targetable by `break 'label` / `continue 'label`.

Evidence: `tools/peg_gen/grammars/logos.peg#L1891-L1902`

### `stmt.loop.break-value-binding` — loop expression value is supplied by break

An infinite `loop { ... break e; ... }` evaluates to the value `e` of the `break` whose target is that loop; the break value is stored into the loop's result slot. `break` without a value or in a loop with no result type produces no loop value.

Evidence: `src/compiler/mlir_gen_stmt.cpp#L2349-L2371`; `src/compiler/mlir_gen_stmt.cpp#L2406-L2412`

### `stmt.loop.never-breaks-is-divergent` — a loop that never breaks is divergent

An infinite `loop` body that contains no reachable `break` targeting it never exits; control after the loop is unreachable (the loop has type `!`).

Evidence: `src/compiler/mlir_gen_stmt.cpp#L2382-L2388`

### `stmt.loop.conservative-init` — Loop body may run zero times for init analysis

A `loop { ... }` body is treated as possibly executing zero times: any variable that becomes definitely-initialized only inside the loop body remains uninitialized in the enclosing scope after the loop (the pre-loop uninit set is snapshotted and restored once the body is lowered).

Evidence: `src/compiler/sema_stmt.cpp#L6877-L6894`

### `stmt.loop.diverges-never` — Loop with no reachable break diverges (type !)

A `loop` whose body contains no `break` reaching the loop diverges; in expression position its type is `!` (never). A loop is non-diverging iff some `break` targeting it carries a value (giving a result type) or breaks without value.

Evidence: `src/compiler/sema_stmt.cpp#L6887-L6889`; `src/compiler/sema_stmt.cpp#L6895-L6897`

### `stmt.loop.break-value-type` — Loop expression value comes from break values

If any `break <expr>` targets the loop, the loop expression's type is the unified type recorded on its break-frame, and lowering allocates a break-value slot the loop yields at exit; `break` without value marks the frame non-diverging but gives no result type.

Evidence: `src/compiler/sema_stmt.cpp#L6887-L6889`; `src/compiler/sema_stmt.cpp#L6901-L6904`

### `stmt.loop.label-scope` — Loop label active only within its body

A loop label is bound (pushed onto the active label set) only for the duration of lowering the loop body, and is captured before body lowering so the label of the immediately-enclosing loop is the pending label; nested loops push/pop their labels with their bodies.

Evidence: `src/compiler/sema_stmt.cpp#L6871-L6872`; `src/compiler/sema_stmt.cpp#L6883-L6885`; `src/compiler/sema_stmt.cpp#L6891`

## Labeled loops

### `stmt.labeled-loop.label-binding` — labeled loop binds its label to the inner loop

`'label: <loop>` extracts the label, makes it the pending loop label, and lowers the inner for/while/loop with that label in scope so break/continue can target it.

Evidence: `src/compiler/sema_stmt.cpp#L317-L330`

## `while`

### `stmt.while.forms` — while and while-let

`while cond { }` is a conditional loop; `while let PAT = e [&& guard] { }` is a while-let loop; `while LET-CHAIN { }` is a while-let chain (≥2 segments starting with let), ordered first so it is not shadowed.

Evidence: `tools/peg_gen/grammars/logos.peg#L2277-L2287`

### `stmt.while.cond-or-let` — while and while-let

A while statement is either `while COND BLOCK` (condition form) or `while let PAT = VALUE BLOCK` (while-let form).

Evidence: `src/compiler/sema_render.cpp#L777-L790`

### `stmt.while.condition-bool` — while condition must be bool

In `while COND { ... }`, COND must have type `bool` (or be an error type); any other type is a type error.

```logos
while x < 10 { }
```

Evidence: `src/compiler/sema_stmt.cpp#L6225-L6230`

### `stmt.while.body-not-definitely-assigning` — while body does not establish definite assignment

A `while`/`while let` loop may execute zero times, so assignments/initializations performed in its body do not count as definitely-initialized in the enclosing scope; the definite-assignment state is restored to its pre-loop value on every exit path.

Evidence: `src/compiler/sema_stmt.cpp#L6050-L6058`

### `stmt.while.label-binds-loop` — loop label attaches to the while loop itself

A leading loop label `'a:` on a `while`/`while let` binds to that loop (added to the active-label set for its body) and is consumed before lowering the body, so an unlabeled nested loop inside the body cannot capture it; `break 'a` / `continue 'a` inside the body resolve to the labeled loop.

```logos
'a: while cond { while inner { break 'a; } }
```

Evidence: `src/compiler/sema_stmt.cpp#L6111-L6116`; `src/compiler/sema_stmt.cpp#L6170-L6176`; `src/compiler/sema_stmt.cpp#L6220-L6246`

### `stmt.while.loop-depth-context` — while body is in loop context

Statements in a `while`/`while let` body execute in loop context (loop depth incremented, a break-frame pushed), so `break` and `continue` are permitted there and resolve to this loop.

Evidence: `src/compiler/sema_stmt.cpp#L6168-L6177`; `src/compiler/sema_stmt.cpp#L6233-L6242`

### `stmt.while.line-maps-to-header` — while loop maps to its header source line

The emitted `while` loop is attributed to the source line of the `while` keyword (the loop header), not the last line of its body, for debug/stepping purposes.

Evidence: `src/compiler/sema_stmt.cpp#L6059-L6064`; `src/compiler/sema_stmt.cpp#L6243-L6247`

## `while let`

### `stmt.while-let.desugar-loop-match` — while-let desugars to loop + match

`while let PAT = EXPR { BODY }` is equivalent to `loop { match EXPR { PAT => { BODY }, _ => break } }`: the loop continues while EXPR matches PAT (binding PAT each iteration), and terminates the first time it does not.

```logos
while let Some(x) = iter.next() { use(x); }
```

Evidence: `src/compiler/sema_stmt.cpp#L6108-L6217`

### `stmt.while-let.refutable-pattern-bindings-scope-body` — while-let pattern bindings scope over body

Bindings introduced by the `while let` pattern are in scope only within the loop body (a fresh scope is pushed for the matched arm and popped after the body).

Evidence: `src/compiler/sema_stmt.cpp#L6133-L6185`

### `stmt.while-let.chain-trailing-cond-must-be-bool` — while-let trailing chain condition must be bool

In a `while let PAT = EXPR && COND` chain, the trailing condition COND must have type `bool` (or error); it is evaluated with the pattern's bindings in scope and folded into the match-arm guard so the loop continues only when the pattern matches AND COND holds.

```logos
while let Some(x) = it.next() && x > 0 { }
```

Evidence: `src/compiler/sema_stmt.cpp#L6138-L6166`; `src/compiler/sema_stmt.cpp#L6199-L6205`

### `stmt.while-let.chain-cond-no-nested-variant-bindings` — while-let chain condition cannot reference nested enum-variant bindings

A `while let` chain trailing condition may not reference bindings introduced by a nested enum-variant subpattern of the same pattern; such a reference is an error (match in the body instead).

> **Uncertainty:** Stated as a current implementation limitation ('cannot yet'); likely a temporary divergence rather than an intended language rule.

Evidence: `src/compiler/sema_stmt.cpp#L6149-L6155`

### `stmt.while-let.chain-multiseg-desugar` — multi-segment while-let chain desugars to nested if-let-else-break

A multi-segment let-chain `while let P1 = e1 && (let P2 = e2 | cond) && ... { BODY }` desugars to `loop { { if let P1 = e1 { { if let P2 = e2 { ... BODY ... } else { break; } } } else { break; } } }`, building inside-out so each segment (a `let PAT = VALUE` or a boolean condition) wraps the running body and falls to `break` on failure; a chain requires at least 2 segments.

```logos
while let Some(a) = x.next() && let Some(b) = y.next() { f(a, b); }
```

Evidence: `src/compiler/sema_stmt.cpp#L6065-L6107`

## `for` loops

### `stmt.for.range-and-iter` — for over range and for-each over iterator

A for-over-range loop is `for NAME in LHS (`..`|`..=`) RHS BLOCK`, where `..=` denotes an inclusive upper bound and `..` an exclusive one; a for-each loop is `for NAME in ITER BLOCK`.

Evidence: `src/compiler/sema_render.cpp#L806-L831`

### `stmt.for.range` — For-range loop

`for i in lo..hi { }` iterates the exclusive integer range; `for i in lo..=hi { }` iterates the inclusive range.

Evidence: `tools/peg_gen/grammars/logos.peg#L1868-L1877`

### `stmt.for.each` — For-each loop

`for x in iter { }` iterates over an iterable. The loop variable may be a simple identifier (fast path) or a full destructuring pattern `for (a,b) in v { }`, in which case the pattern is bound against each element.

Evidence: `tools/peg_gen/grammars/logos.peg#L1878-L1886`

### `stmt.for.range-loop-type-widening` — numeric range-for uses the wider of bound types for the induction variable

A range `for i in lo..hi` / `lo..=hi` uses an induction variable whose width is the wider of the lo and hi integer types (default i32, widened to i64+ when a bound is wider), so wide bounds are not truncated. Bounds are extended to the loop type with unsigned extension when the bound type is unsigned, otherwise signed.

Evidence: `src/compiler/mlir_gen_stmt.cpp#L2250-L2272`; `src/compiler/mlir_gen_stmt.cpp#L2299-L2303`

### `stmt.for.range-comparison-signedness` — range-for comparison and inclusivity follow bound signedness

A range `for` continues while `i < hi` (exclusive) or `i <= hi` (inclusive `..=`); the comparison is unsigned when the upper-bound type is unsigned, otherwise signed. The induction variable is incremented by 1 after each body execution.

Evidence: `src/compiler/mlir_gen_stmt.cpp#L2304-L2336`

## `for`-each iteration

### `stmt.for-each.slice-yields-element-reference` — for-in over a slice yields &T element references

Iterating `for x in &[T]` (a slice) binds `x` to a reference into the original buffer (the element address), not a copied value; the slice is iterated over its runtime `len` field. Mutating through `x` mutates the original buffer.

> **Uncertainty:** binding-as-reference is asserted as Rust parity in comments; exact mutability of the binding is not fully constrained here.

Evidence: `src/compiler/mlir_gen_stmt.cpp#L2474-L2529`

## `break`

### `stmt.break.forms` — break

`break;`, `break expr;`, `break 'label;`, and `break 'label expr;` are all valid; a value and/or a target label are optional. A bare `break` may also be terminated by `,`.

Evidence: `tools/peg_gen/grammars/logos.peg#L1904-L1911`

### `stmt.break.label-resolution` — Labeled break targets the matching enclosing loop; unlabeled break targets the innermost

`break;`/`break v;` with no label transfers control to the innermost enclosing loop. `break 'label;`/`break 'label v;` searches the enclosing loop stack from innermost outward for a loop whose label equals `'label` and targets that loop.

Evidence: `src/compiler/mlir_gen_stmt.cpp#L2549-L2567`

### `stmt.break.label-targets-named-loop` — labeled break/continue target the matching enclosing loop

An unlabeled `break`/`continue` targets the innermost enclosing loop; a labeled `break 'l`/`continue` targets the nearest enclosing loop whose label equals the given label.

Evidence: `src/compiler/mlir_gen_stmt.cpp#L2394-L2417`

### `stmt.break.outside-loop` — break only inside a loop

A `break` is an error outside any loop. A labeled `break 'l` is an error unless `'l` is an active in-scope loop label.

Evidence: `src/compiler/sema_stmt.cpp#L456-L465`

### `stmt.break.target-resolution` — break targets the matching labeled or innermost loop

A labeled `break 'l v` targets the nearest enclosing loop with label `'l` (searched innermost-out); an unlabeled `break v` targets the innermost loop. The break value attributes to the target frame, so a value breaking to an outer labeled loop is not consumed by an inner loop.

Evidence: `src/compiler/sema_stmt.cpp#L466-L502`

### `stmt.break.value-consistency` — all breaks of a loop must agree on value presence and type

All breaks targeting the same loop must agree: a loop cannot mix value-carrying and value-less breaks (`break v` vs `break`); and all break values must have mutually compatible types, the loop's break type being the unification of them.

Evidence: `src/compiler/sema_stmt.cpp#L483-L501`

## `continue`

### `stmt.continue.forms` — continue

`continue;` and `continue 'label;` are valid; the target label is optional. A bare `continue` may also be terminated by `,`.

Evidence: `tools/peg_gen/grammars/logos.peg#L1913-L1916`

### `stmt.continue.for-increments-first` — continue in a counted for runs the increment before the next iteration

In a counted/iterating `for`, `continue` branches to the increment step (advance index/induction variable) before re-evaluating the loop condition, rather than skipping straight to the condition.

Evidence: `src/compiler/mlir_gen_stmt.cpp#L2319-L2323`; `src/compiler/mlir_gen_stmt.cpp#L2531-L2536`; `src/compiler/mlir_gen_stmt.cpp#L2632-L2637`

### `stmt.continue.labeled-target` — Labeled `continue` targets the matching enclosing loop

A labeled `continue 'label` transfers control to the continuation block of the nearest enclosing loop (searching the loop stack from innermost to outermost) whose label equals `'label`. An unlabeled `continue`, or a label matching no enclosing loop on the stack, falls back to the innermost loop's continuation block.

Evidence: `src/compiler/mlir_gen_stmt.cpp#L403-L414`

### `stmt.continue.outside-loop` — continue only inside a loop with in-scope label

A `continue` is an error outside any loop. A labeled `continue 'l` is an error unless `'l` is an active in-scope loop label.

Evidence: `src/compiler/sema_stmt.cpp#L504-L514`

## `break` / `continue` interaction with drops and labels

### `stmt.break-continue.label-value` — break and continue with optional label and value

`break ['LABEL] [VALUE] ;` may carry an optional loop label (single-quote sigil) and an optional break value; `continue ['LABEL] ;` may carry an optional loop label.

Evidence: `src/compiler/sema_render.cpp#L833-L855`

### `stmt.break-continue.drops-to-loop-boundary` — break/continue drop every frame down to and including the loop-body frame

Lowering `break`/`continue` inserts drops for every scope frame from the current point down to AND INCLUDING the frame tagged as the enclosing loop's body boundary, so a `break`/`continue` nested inside an `if` inside the loop body still releases all intervening locals — bypassing the block's ordinary single-scope end-of-block drop sequence.

Evidence: `src/compiler/sema_stmt.cpp#L667-L670`; `src/compiler/sema_stmt.cpp#L726-L734`

## `return`

### `stmt.return.form` — return statement

`return [expr];` returns from the enclosing function with an optional value; may be terminated by `;` or `,`.

Evidence: `tools/peg_gen/grammars/logos.peg#L2289-L2290`

### `stmt.return.optional-value` — return statement with optional value

A return statement is `return [VALUE] ;`; the value expression is optional (return without a value when no VALUE is present).

Evidence: `src/compiler/sema_render.cpp#L711-L722`

### `stmt.return.fallthrough-void-implicit` — Void function body may fall through to an implicit return

A function with no return type (void) whose body reaches the end of its final block without an explicit terminator gets a plain `return` with no operand inserted implicitly.

Evidence: `src/compiler/mlir_gen_fn.cpp#L509-L525`

### `stmt.return.exhaustive-tail-unreachable` — Sema-accepted exhaustive non-void tail lowers to `unreachable`, not a return

For a non-void function whose body falls through to the end of codegen without an explicit terminator (e.g. an exhaustive tuple/struct `match` with no wildcard arm — a path sema's reachability analysis has already accepted as complete, but which mlir-gen's dispatch lowering does not itself prove exhaustive), mlir-gen inserts an `unreachable` terminator instead of a value-producing return, so the function still verifies structurally; this path is dead code, never executed at runtime.

Related: `stmt.return.fallthrough-void-implicit`

Evidence: `src/compiler/mlir_gen_fn.cpp#L523-L536`

### `stmt.return.never-fn-operandless` — return in a `-> !` function is operand-less

If the enclosing function's return type is the never type `!`, a `return e` evaluates `e` for its side effects (it may itself diverge and terminate control flow) but produces a value-less return; the function has a 0-result signature.

```logos
fn f() -> ! { return diverging() }
```

Evidence: `src/compiler/mlir_gen_stmt.cpp#L2024-L2034`

### `stmt.return.empty-yields-unit` — bare return yields no value

A `return` with no operand produces a value-less return.

Evidence: `src/compiler/mlir_gen_stmt.cpp#L2150-L2155`

### `stmt.return.all-paths-required` — Non-void fn body must return on all paths

In a specialization fn body, if the declared return type is neither void nor an error type, every control-flow path through the lowered body must return a value; otherwise it is a compile error "not all paths return a value".

Evidence: `src/compiler/sema_collect.cpp#L4736-L4741`

### `stmt.return.drops-after-value-eval` — return evaluates its value before running pending scope drops

Lowering `return expr;` evaluates/lowers expr first; if any scope drops are pending at that point, the return value is spilled into a fresh temporary `__ret_tmp_N`, all pending drops are emitted, and the Return is rewritten to return the temporary — guaranteeing expr (which may borrow locals about to be dropped) is fully evaluated before drop glue runs. With no pending drops, the temp-spill step is skipped and drops are appended directly before the original Return.

Evidence: `src/compiler/sema_stmt.cpp#L699-L724`

### `stmt.return.value-required` — Return-without-value only in unit/never/impl-Trait functions

`return;` (no value) is rejected in a function whose return type is not unit, error, or `impl Trait`: "return without value in function returning T".

Evidence: `src/compiler/sema_stmt.cpp#L3019-L3024`

### `stmt.return.type-mismatch` — Return value type-compatibility

`return e` requires `typeof(e)` compatible with the declared return type, after normalizing associated-type projections via equality bounds (`T: Trait<A=V>`); otherwise "return type mismatch — expected T, got U".

Evidence: `src/compiler/sema_stmt.cpp#L2859-L2868`

### `stmt.return.impl-trait-infer` — impl-Trait return type inferred from first return

When the declared return type is `impl Trait`, the concrete return type is inferred from the first non-error return expression.

Evidence: `src/compiler/sema_stmt.cpp#L2854-L2858`

### `stmt.return.hint-propagation` — Return type hints literal/closure inference

The declared return type seeds expected-type hints while lowering the return value: a generic enum/struct return type pins literal type-params; a fn-ptr/closure or wrapped-callable (`Box<dyn Fn(..)>`) return type infers an untyped closure literal's params; a (possibly `&`-wrapped) array/slice return type supplies an array-literal element-type hint.

Evidence: `src/compiler/sema_stmt.cpp#L2784-L2817`

### `stmt.return.temp-hoist-before-return` — Return value's hoisted temporaries drop before control transfers

When lowering the return expression hoists droppable statement-temporaries (e.g. a droppable rvalue receiver `make().get()`), the return value is bound to a synthetic local first; the enclosing statement lowering then emits the temporaries' drops before the `return` of that local, so the temporaries are not leaked (they would be, since drops normally emitted after a statement are dead code after a `return`).

Evidence: `src/compiler/sema_stmt.cpp#L3002-L3015`

## Tail expressions

### `stmt.tail.implicit-return-nonvoid` — non-void tail expression is an implicit return at fn body

At function-body level with a non-void declared return type, a trailing expression (no semicolon) is an implicit `return <e>`. If its type is Void it is lowered as an expression-statement instead; only a non-void tail becomes an implicit return.

Evidence: `src/compiler/sema_stmt.cpp#L371-L450`

### `stmt.tail.closure-inference-return` — non-void tail in closure inference body is the implicit return

In a closure body lowered in inference mode (no declared return type), a non-void, non-error tail expression is the closure's implicit return; a void/error tail is an expression-statement. No return-type compatibility check is applied (the closure has no declared type).

Evidence: `src/compiler/sema_stmt.cpp#L390-L398`

### `stmt.tail.enum-hint-from-ret-type` — tail enum-literal inference threads the fn return type

When the function's declared return type is an enum, a tail-position enum literal (`Result::Ok(v)`, `Either::L(x)`) is inferred against that return type so the enum's type parameters not constrained by the chosen variant are resolved from the return type rather than inferred as error.

Evidence: `src/compiler/sema_stmt.cpp#L402-L419`

### `stmt.tail.return-type-mismatch` — tail implicit return type-checked like explicit return

The type of a tail-position implicit return must be compatible with the declared return type, must pass the variance gate, and must satisfy dyn+auto bound checks at coercion — identically to an explicit `return`. Moving out of a value behind a reference or out of an index in tail-return position is rejected (E0507).

Evidence: `src/compiler/sema_stmt.cpp#L427-L447`

## Divergence analysis

### `stmt.diverge.return-stmt` — return statement always returns

A `return` statement is a diverging statement: control never falls through to the following statement.

Evidence: `src/compiler/sema_stmt.cpp#L28-L29`

### `stmt.diverge.never-returning-call` — call to a Never-returning fn diverges

A call expression `f(...)` (including the macro form `panic!(...)` which parses as FN_MACRO_CALL) in expression-statement, tail-expression, or let-initializer position is divergent — control never falls through — iff the callee is named `panic` OR any candidate function with that name has return type `!` (Never). `panic` is recognized by name even without a `!` annotation.

> **Divergence from Rust:** A: `panic` recognized as divergent by hardcoded callee name (Logos historically lacked the `!` type); now generalized to any `-> !` callee.

Evidence: `src/compiler/sema_stmt.cpp#L34-L53`; `src/compiler/sema_stmt.cpp#L208-L218`

### `stmt.diverge.block-value` — block/if/match in expr-stmt position diverges if body does

An expression-statement or tail-expression whose value is a BLOCK, IF, or MATCH diverges iff that nested construct always returns; a value of RETURN/BREAK/CONTINUE expression form always diverges.

Evidence: `src/compiler/sema_stmt.cpp#L54-L64`

### `stmt.diverge.tail-expr-context` — tail expression counts as implicit return only at fn-body context

A trailing expression (TAIL_EXPR, no semicolon) is treated as an implicit return — and thus a diverging tail — only when it is in function-body position; in match-arm-body or block-as-expression contexts the same node is the block's value, not a return.

Evidence: `src/compiler/sema_stmt.cpp#L65-L69`

### `stmt.diverge.let-diverging-init` — let with diverging initializer never binds

`let x = <e>;` diverges when its initializer `<e>` is a RETURN/BREAK/CONTINUE expression, a divergent call, a BLOCK that always returns, or an IF/MATCH that always returns; the binding never occurs.

Evidence: `src/compiler/sema_stmt.cpp#L87-L95`

### `stmt.diverge.infinite-loop` — loop without break diverges

A `loop { ... }` statement never falls through to the next statement (it is an infinite loop, diverging unless exited by a non-fallthrough construct).

Evidence: `src/compiler/sema_stmt.cpp#L96-L99`

### `stmt.diverge.if-both-branches` — if/else diverges iff both branches diverge

An `if` always returns iff it has an `else` and both the then-block and the else-branch always return; an `if` without `else` never forces a return.

Evidence: `src/compiler/sema_stmt.cpp#L100-L109`

### `stmt.diverge.match-all-arms` — match diverges iff all arms diverge

A non-empty `match` always returns iff every arm's body always returns; an expression arm (`pat => expr`) provides a value and does not count as diverging; an empty match does not force a return.

Evidence: `src/compiler/sema_stmt.cpp#L110-L131`

### `stmt.diverge.block-reaches` — block diverges if any statement diverges

A block always returns iff at least one of its statements always returns (a diverging statement makes all following statements unreachable).

Evidence: `src/compiler/sema_stmt.cpp#L135-L143`

### `stmt.diverge.break-continue-divert` — break/continue divert control flow

`break` and `continue` are divergent for loop-body fallthrough analysis: like `return`, control does not fall through to the following statement. if/else and match propagate diversion when all branches/arms divert.

Evidence: `src/compiler/sema_stmt.cpp#L145-L182`

## Never-type fallback

### `stmt.fallback.never-only-on-provable-divergence` — Never-fallback gated on provably non-returning body

A generic return type-param may fall back to `!` only when the callee body provably never returns normally — i.e. the body's last statement is a divergent call (`panic`/`-> !`), a `loop`, or an expression-statement/tail wrapping a `loop`. A body ending in `return 0;` does NOT qualify (that is a normal return, leaving the type-param ambiguous).

> **Divergence from Rust:** A: implements a Rust-2024-style `!`-fallback but with a stricter, narrower divergence predicate than full `block_always_returns`.

Evidence: `src/compiler/sema_stmt.cpp#L194-L226`

## Nested `fn` items

### `stmt.fn.nested-lifts-to-toplevel` — Nested function lifted to a free function

A `fn name(params) [-> T] { body }` at statement position is lifted to a top-level free function (gensym name); the local name is bound as a fn-ptr `let`. A nested fn does not capture enclosing locals.

> **Note:** Possible layer conflict: this grammar-layer rule says a nested `fn` is lifted to a top-level free function with the local name bound as a fn-ptr `let`, while `stmt.nested-fn.let-bound-closure` (Sema layer) describes lowering to a `let`-bound closure with the closure’s inferred type. Both are preserved verbatim; reconcile at freeze.

Evidence: `tools/peg_gen/grammars/logos.peg#L303`

## Nested `fn` lowering

### `stmt.nested-fn.let-bound-closure` — Statement-position fn desugars to a let-bound closure

A function item at statement position `fn inner(params) [-> T] { body }` is lowered as an immutable local binding `let inner = |params| -> T { body }`; the binding's type is the closure's inferred type.

> **Note:** Possible layer conflict with `stmt.fn.nested-lifts-to-toplevel` (grammar layer: lift to top-level free fn + fn-ptr `let`). Both are preserved verbatim; reconcile at freeze.

Evidence: `src/compiler/sema_stmt.cpp#L1673-L1691`
