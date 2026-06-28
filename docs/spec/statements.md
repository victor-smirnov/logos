# Statements

Scope: statement-level constructs of Logos — bindings, assignment, control flow (`if`/`while`/`loop`/`for`/`match`), divergence analysis, and tail expressions. Rules are extracted from two source layers: the grammar definitions (`grammar/logos`) and the semantic-analysis lowering pass (`sema/sema_stmt`, `sema/sema_render`). Each rule id is a permanent, linkable address; never rename or merge ids.

## Statement Dispatch

### `stmt.dispatch.kinds` — statement forms recognized

Statements comprise: let / let-else / let-destructure / let-pattern bindings, nested fn items, assignment (plain / destructuring / compound / place / deref-write / deref-compound), return, if / if-let-chain, labeled loops, while / for / for-each / loop, match, expression-statement, trailing tail-expression, break, continue, unsafe block, and bare block statement. An if-let-chain in statement position is desugared via the expression form and wrapped as a statement-expression.

_Source: `src/compiler/sema_stmt.cpp#L296-L336`_

## Statement Kinds

### `stmt.kinds.dispatch` — Statement forms

A statement is one of: nested-fn, labeled-loop, let-else, let, for, while, loop, return, break, continue, deref-write, if-expr, match, destructure-assign, assign, compound-assign, place-assign, unsafe-block, block, `expr ;` (expression statement), or a trailing `expr` (block tail value). A block without a trailing `;` yields its final expression as the block value.

_Source: `tools/peg_gen/grammars/logos.peg#L1839-L1866`_

## Blocks

### `stmt.block.dead-code-after-terminator` — Unreachable code after a terminator is warned

Within a block, a statement S that follows a hard terminator statement (return / break / continue) is unreachable; the compiler emits a `unreachable code after terminator` warning. Annotation statements following a terminator do not trigger the warning, and only the first such occurrence per block is reported.

**Divergence:** Rust-conformant (unreachable_code lint, here always a warning not deny-by-default)

_Source: `src/compiler/sema_stmt.cpp#L671-L697`_

### `stmt.block.scope-drops-at-exit` — Owned values are dropped at block exit

Each block introduces a lexical scope; on normal fall-through exit (block does not end in return/break/continue), every still-owned value bound in the scope is dropped in reverse-of-introduction order. If the block ends in a terminator, end-of-block drops are not emitted (the terminator path emits its own).

_Source: `src/compiler/sema_stmt.cpp#L664`, `src/compiler/sema_stmt.cpp#L739-L755`_

### `stmt.block.scoping-and-unsafe` — block statement and unsafe block at statement position

A bare brace-delimited block `{ stmts... }` is a valid statement (scoping block), and `unsafe { stmts... }` wraps a block with the unsafe modifier at statement position; both contain a sequence of statements.

_Source: `src/compiler/sema_render.cpp#L894-L927`_

## Scopes and Temporaries

### `stmt.scope.return-temps-dropped-before-terminator` — temporaries in a return value drop before the return terminator

When `return <val>` materializes statement-scoped temporaries, the value is bound to a synthetic local while the temporaries live, the temporaries are then dropped, and only afterward does the return terminator execute — so drops precede the terminator rather than being dead code past it.

_Source: `src/compiler/sema_stmt.cpp#L274-L291`_

### `stmt.scope.temp-drop-at-stmt-end` — statement-scoped temporaries dropped at end of statement

Fresh owned (droppable) temporaries materialized while lowering a statement live to the end of that statement and have their destructors run there, in REVERSE order of creation (Rust temporary-scope semantics). Place/borrow expressions (VarRef, FieldRead, IndexRead, Deref, TupleIndex, SliceIndex, SlicePtr, AddrOf, AddrOfTemp) are not hoistable temporaries; only rvalue-producing kinds (Call, MethodCall, StructLit, EnumLitData, …) are.

_Source: `src/compiler/sema_stmt.cpp#L230-L294`_

## Unsafe Blocks

### `stmt.unsafe-block.context` — unsafe block establishes an unsafe context for its body

An `unsafe { ... }` block lowers its body with an unsafe context active for the duration of the block, restoring the prior context afterward; the body is otherwise an ordinary block.

_Source: `src/compiler/sema_stmt.cpp#L643-L651`_

## Expression Statements

### `stmt.expr.discarded-rvalue-dropped` — discarded statement-expression rvalue runs its destructor

A statement-expression `e;` that produces a fresh owned move-typed value (a non-place rvalue, e.g. `make(p);`) is bound to a synthetic local and dropped at the end of the statement. A bare place expression (`existing_var;`) is not re-dropped, avoiding double-drop against its scope drop.

_Source: `src/compiler/sema_stmt.cpp#L337-L369`_

### `stmt.expr.trailing-semicolon` — expression statement vs tail expression

An expression in statement position is terminated by `;` (EXPR_STMT), whereas a block's final expression in tail position (TAIL_EXPR) carries no trailing `;`.

_Source: `src/compiler/sema_render.cpp#L724-L733`_

## let Bindings

### `stmt.let.annotation-type-hints` — let annotation supplies a type hint to RHS inference

A non-hole let annotation hints RHS literal/inference: enum/struct annotations with type-args, fn-ptr/closure annotations (so untyped closure params infer), array/slice element types (incl. through `&[T]`/`&mut [T]`), and tuple element types (so untyped int literals widen to the annotated element type instead of defaulting).

```logos
let f: fn(i64)->i64 = |x| x+1;
let p:(i64,i64) = (7, 2);
```

_Source: `src/compiler/sema_stmt.cpp#L1723-L1771`_

### `stmt.let.binding-form` — let binding surface form

A let statement has the form `let [mut] NAME [: TYPE] = VALUE ;`: the `mut` keyword is present iff the binding is declared mutable, the `: TYPE` ascription is optional, and an initializer expression VALUE and trailing `;` are always present.

**Uncertainty:** Inferred from the canonical source-rendering; renderer always emits an initializer, suggesting let without `=` is not a form handled here.

_Source: `src/compiler/sema_render.cpp#L694-L709`_

### `stmt.let.closure-capture-drop-ownership` — A closure-RHS let owns its captures' drop slots

A let whose RHS is a closure literal owns the drop slots of that closure's (un-skipped) captures; they are dropped together with the binding, in capture order.

_Source: `src/compiler/sema_stmt.cpp#L2210-L2222`_

### `stmt.let.declare-uninit` — let without initializer declares an uninitialised binding

`let v: T;` / `let mut v: T;` (annotation, no value) declares the binding with the annotated type and no value; the binding is recorded as declared-uninitialised so a later assignment registers the value without a drop-before-replace, and the variable must be assigned before use. `let` without value and without annotation is an error.

_Source: `src/compiler/sema_stmt.cpp#L1942-L1962`_

### `stmt.let.forms` — let bindings

`let` supports: tuple destructure `let (a,b) [: T] = e;`, `let ref x [: T] = e;` (sugar for `let x = &e;`), `let mut x [: T] [= e];` (mutable, type-only declaration without init allowed when typed), `let x [: T] [= e];`, and `let PAT = e;` (irrefutable full pattern, refutability checked by sema).

_Source: `tools/peg_gen/grammars/logos.peg#L2246-L2275`_

### `stmt.let.inferred-annotation-deferred` — Top-level `_` annotation defers to RHS type

A top-level placeholder annotation `let x: _ = rhs` drops the annotation entirely and adopts the RHS type. A nested `_` inside a composite annotation (`Vec<_>`) is a hole filled from the RHS during inference rather than dropped.

_Source: `src/compiler/sema_stmt.cpp#L1713-L1721`, `src/compiler/sema_stmt.cpp#L2179-L2183`_

### `stmt.let.ref-binding-sugar` — `let ref y = x` is sugar for `let y = &x`

A `ref` binding `let ref y = x` (or `let ref y: T = x`) lowers to taking the address of the RHS, giving `y` type `&T`.

**Divergence:** P4-pm-14

_Source: `src/compiler/sema_stmt.cpp#L1897-L1931`_

## let with Patterns

### `stmt.let-pat.array-fixed-no-rest` — let array pattern requires exact length, no rest

`let [a, b, ..] = arr;` is irrefutable only over a fixed-size array whose length equals the pattern's element count and with no `..` rest (rest is refutable for slices); element bindings must be plain identifiers (`_` discards). Length mismatch, a rest, or a non-identifier element is an error.

_Source: `src/compiler/sema_stmt.cpp#L1111-L1121`, `src/compiler/sema_stmt.cpp#L1229-L1308`_

### `stmt.let-pat.consumes-source` — let struct-destructure consumes the whole value

A `let` struct/tuple-struct/array destructure of a move-typed RHS consumes the whole value: the source is marked moved so its scope-exit drop is suppressed; each field/element binding owns and later drops its own data.

_Source: `src/compiler/sema_stmt.cpp#L1462-L1471`, `src/compiler/sema_stmt.cpp#L1558-L1563`, `src/compiler/sema_stmt.cpp#L1264-L1276`_

### `stmt.let-pat.field-binding-forms` — let struct-pattern field binding forms

A `let` struct-pattern field is bound either by shorthand `{ x }` (binds to the field name), by alias `{ x: a }` where the value is a plain identifier, or by a nested struct pattern `{ x: S2 { .. } }` (recursive destructure). A nested pattern requires the field's type to be a struct whose name matches the nested pattern; other nested-pattern kinds are rejected.

_Source: `src/compiler/sema_stmt.cpp#L1490-L1503`, `src/compiler/sema_stmt.cpp#L1535-L1554`_

### `stmt.let-pat.single-variant-enum-struct` — let E::V { f } over a single-variant enum is irrefutable

`let E::V { f1, f2 } = e;` is permitted when E is a single-variant enum and V is a struct-shaped variant (irrefutable by construction); each user binding is bound to the corresponding payload field, with shorthand `f` or rename `f: x` forms, and only plain-identifier bindings are supported at let-position.

_Source: `src/compiler/sema_stmt.cpp#L1077-L1108`, `src/compiler/sema_stmt.cpp#L1128-L1228`_

### `stmt.let-pat.struct-name-match` — let struct-pattern name must match RHS struct

In `let S { .. } = e;` (and the tuple-struct form), the named struct must equal the RHS value's struct name, and the RHS must be a struct/zoned-struct; otherwise it is an error.

_Source: `src/compiler/sema_stmt.cpp#L1316-L1321`, `src/compiler/sema_stmt.cpp#L1398-L1409`_

### `stmt.let-pat.struct-shapes-only` — let <pattern> = e supports only irrefutable shapes

`let <pattern> = e;` accepts only irrefutable pattern shapes: struct patterns `S { .. }`, tuple-struct patterns `S(..)`, fixed-array patterns `[a,b,..]` matching the array length, and single-variant enum struct patterns `E::V { .. }`. Other (refutable) shapes are rejected, directing the user to `match` or let-else.

_Source: `src/compiler/sema_stmt.cpp#L1118-L1127`_

### `stmt.let-pat.tuple-struct-rest` — let tuple-struct pattern field arity and single rest

`let S(a, b, ..) = e;` over a tuple struct binds by field position; at most one `..` rest is allowed (names before it bind low fields, after it bind the tail). Without a rest the binding count must equal the field count; the named-binding count must not exceed the arity; only plain-identifier element bindings are supported (`_` skips).

_Source: `src/compiler/sema_stmt.cpp#L1331-L1393`_

### `stmt.let-pat.union-requires-unsafe` — let pattern on a union requires unsafe and exactly one field

An irrefutable `let U { f } = u;` over a union type requires an enclosing `unsafe` block (it reads the named field's memory) and must name exactly one field with no `..` rest.

**Divergence:** Rust-conformant (items.union.pattern.safety / one-field)

_Source: `src/compiler/sema_stmt.cpp#L1410-L1457`_

### `stmt.let-pat.unknown-field` — let struct-pattern fields must exist

Each field named in a `let` struct pattern must be a declared field of the matched struct; an unknown field is an error. Generic struct field types are substituted with the RHS's type arguments before binding.

_Source: `src/compiler/sema_stmt.cpp#L1504-L1520`_

## let Destructuring

### `stmt.let-destruct.binding-uniqueness` — let-tuple binding names must be unique

All binding names introduced by a `let (..)` destructure (across nesting) must be distinct.

_Source: `src/compiler/sema_stmt.cpp#L860-L863`_

### `stmt.let-destruct.move-on-bind` — let-destructure consumes the source

Destructuring a move-typed value binds each element by moving it out of the source; the source place (and each consumed element) is marked moved so its scope-exit drop is suppressed, preventing double-free.

_Source: `src/compiler/sema_stmt.cpp#L812-L822`, `src/compiler/sema_stmt.cpp#L848-L853`_

### `stmt.let-destruct.nested-tuple` — Nested tuple patterns in let-destructure

A tuple-binding element may itself be a nested tuple pattern `(b, c)`, recursively destructuring the corresponding tuple-typed element.

_Source: `src/compiler/sema_stmt.cpp#L841-L843`_

### `stmt.let-destruct.rest-and-arity` — let-tuple binding arity and single rest

In `let (..) = e;` the binding list binds positionally against the tuple's arity. At most one `..` rest is permitted; with no rest the binding count must equal the tuple arity; with a rest the count of named bindings must not exceed the arity. Names before the rest bind low positions, names after bind the tail positions.

_Source: `src/compiler/sema_stmt.cpp#L791-L811`, `src/compiler/sema_stmt.cpp#L823-L836`_

### `stmt.let-destruct.tuple-required` — let (..) = e requires tuple RHS

A tuple-binding `let (a, b, ...) = e;` requires e to have tuple type; otherwise it is an error.

_Source: `src/compiler/sema_stmt.cpp#L762-L767`_

## let-else

### `stmt.let-else.bindings-in-outer-scope` — let-else pattern bindings escape to the enclosing scope

Bindings introduced by the let-else pattern are defined in the enclosing (outer) scope and remain visible after the statement; the else-block is lowered in a separate nested scope. Each binding takes its type from the matched pattern position. Bindings named `_` are not introduced.

_Source: `src/compiler/sema_stmt.cpp#L1604-L1662`, `src/compiler/sema_stmt.cpp#L1630-L1633`, `src/compiler/sema_stmt.cpp#L1648-L1649`_

### `stmt.let-else.diverging-else` — let-else else-block must diverge

In `let PAT = EXPR else { BLOCK };` the else BLOCK must unconditionally diverge (end in return / break / continue / panic / `loop {}`); a non-diverging else-block is a compile error.

```logos
let Some(x) = opt else { return; };
```

_Source: `src/compiler/sema_stmt.cpp#L1604-L1614`_

### `stmt.let-else.form` — let-else statement

`let PAT = expr else { block };` binds a refutable pattern; on match failure the else block runs (which must diverge).

_Source: `tools/peg_gen/grammars/logos.peg#L2242-L2244`_

## Assignment

### `stmt.assign.compound-place` — Compound assignment over any place

`PLACE op= expr` applies a compound assignment over an arbitrary place: a bare variable takes the simple-var path; any other place desugars to `place = place op rhs` (or an `*Assign` trait-method call). Bare-deref `*p op= v` is handled separately (it is not an atom).

_Source: `tools/peg_gen/grammars/logos.peg#L2317-L2323`_

### `stmt.assign.deferred-init-once` — Deferred initialization permits one write without mut

A non-`mut` local declared without an initializer (`let x: T;`) may be assigned exactly once; that first assignment initializes it. A second assignment to the same non-`mut` local is rejected.

_Source: `src/compiler/sema_stmt.cpp#L2558-L2566`, `src/compiler/sema_stmt.cpp#L2746-L2753`_

### `stmt.assign.destructure` — Destructuring assignment into existing places

Tuple `(a, b) = e;`, array `[a, b] = e;`, and struct `S { f, .. } = e;` destructuring assignment writes into existing places (RFC 2909). Parsed before expr-statements so a parenthesised/bracketed LHS followed by `=` is recognized.

_Source: `tools/peg_gen/grammars/logos.peg#L2306-L2315`_

### `stmt.assign.destructuring-into-places` — Destructuring assignment into existing places

Destructuring assignment `(a,b)=e` / `[a,b]=e` / `S{a,b}=e` writes into EXISTING places (not new bindings), desugared to `let tmp = rhs;` followed by per-place assignments.

**Divergence:** RFC 2909 (Rust-conformant).

_Source: `tools/peg_gen/grammars/logos.peg#L311`_

### `stmt.assign.drop-before-replace` — Assignment drops the old value before overwrite

Assigning to a variable that currently holds a live droppable value runs that value's destructor before the store, and after evaluating the RHS (so `x = f(x)` is sound). The drop is suppressed when the variable was declared without an initializer (runtime drop-flag governs it instead) or is currently whole- or partially moved-out.

_Source: `src/compiler/sema_stmt.cpp#L2714-L2745`_

### `stmt.assign.enum-lit-hint-retype` — Assignment LHS pins enum-literal type

When the LHS variable has enum type, the RHS is lowered with that enum as the expected-type hint; an incompletely-typed generic enum literal RHS (no type-args, or any Error type-arg) is retyped to the LHS's concrete enum spec, provided the LHS is fully concrete and every known (non-error) literal type-arg already matches the LHS at its position (arity must match). A genuine type-arg mismatch is left for the compatibility check to reject.

_Source: `src/compiler/sema_stmt.cpp#L2576-L2629`_

### `stmt.assign.immutable-var` — Assignment to immutable variable is an error

Assigning to a non-`mut` variable is rejected ("assignment to immutable variable"), except the single deferred-initialization write to a `let x: T;` declared without initializer.

_Source: `src/compiler/sema_stmt.cpp#L2558-L2566`_

### `stmt.assign.int-widen` — Implicit integer widening on assignment

On assignment to an integer variable, a non-literal non-enum integer RHS of a narrower integer kind that can widen safely to the LHS kind is implicitly widened.

**Divergence:** Rust has no implicit integer widening on assignment.

_Source: `src/compiler/sema_stmt.cpp#L2638-L2644`_

### `stmt.assign.intlit-fits` — Integer-literal assignment must fit target type

An integer-literal RHS (including elements of array/tuple literals, recursively through nested arrays/tuples) must fit in the target's (element's) integer type; an out-of-range value is rejected: "value V does not fit in T".

_Source: `src/compiler/sema_stmt.cpp#L2645-L2713`_

### `stmt.assign.place` — General place assignment

`PLACE = expr;` where PLACE is an arbitrary postfix-chain lvalue (chained index `a[i][j]`, deref+tuple-index `(*p).0`, deeper mixes); sema computes the address and emits a deref-write. Tried after the specialized single/two-level write forms and after bare-variable assignment.

_Source: `tools/peg_gen/grammars/logos.peg#L2295-L2304`_

### `stmt.assign.place-forms` — assignment place forms

Assignment statements take the forms: `NAME = VALUE ;`, `NAME OP VALUE ;` (compound assign), `*NAME = VALUE ;` (deref write), `*NAME OP VALUE ;` (deref compound assign), `RECEIVER.FIELD = VALUE ;` (field write), and `RECEIVER.PATH... = VALUE ;` (chained field write through a dot-separated path).

_Source: `src/compiler/sema_render.cpp#L739-L885`_

### `stmt.assign.reassign-revives` — Reassignment revives a moved variable

Assigning a new value to a variable clears its moved-out state, making it usable again; the RHS source, if a move-type, is marked moved so its scope-exit drop is suppressed.

_Source: `src/compiler/sema_stmt.cpp#L2746-L2756`_

### `stmt.assign.simple` — Simple variable assignment

`name = expr;` assigns to a simple variable place.

_Source: `tools/peg_gen/grammars/logos.peg#L2292-L2293`_

### `stmt.assign.static-mut-unsafe` — Writing a mutable static requires unsafe

Writing to a module-level `static mut` requires an enclosing `unsafe` block: outside unsafe it is rejected (Rust `items.static.mut.safety`). A local binding (or type parameter) shadowing the static's name reclassifies the write as a normal local assignment, suppressing the static-mut gate.

_Source: `src/compiler/sema_stmt.cpp#L2538-L2557`_

### `stmt.assign.type-mismatch` — Assignment RHS type-compatibility

`x = e` requires `typeof(e)` compatible with `typeof(x)`; otherwise "assignment: type mismatch — expected T, got U". A `#[rel_ptr]` ↔ `*T` relation is also accepted.

_Source: `src/compiler/sema_stmt.cpp#L2630-L2637`_

### `stmt.assign.undefined-var` — Assignment to undefined variable is an error

`x = e` where `x` is not bound is rejected: "assignment to undefined variable".

_Source: `src/compiler/sema_stmt.cpp#L2529-L2537`_

## Compound Assignment

### `stmt.compound-assign.deref-desugar` — *p op= v desugars to *p = *p op v

A deref-compound assignment `*p op= v` desugars to `*p = (*p op v)`, reading the pointee, applying the binary operator, and writing the result back.

_Source: `src/compiler/sema_stmt.cpp#L516-L568`_

### `stmt.compound-assign.deref-mut-dispatch` — compound deref-assign on a DerefMut struct dispatches deref_mut

When the left side of `*w op= v` is a struct (or zoned struct) implementing DerefMut<T>, the operation desugars through `w.deref_mut()` (yielding &mut T): `*(w.deref_mut()) = *(w.deref_mut()) op v`.

_Source: `src/compiler/sema_stmt.cpp#L529-L552`_

## Dereferencing Writes

### `stmt.deref-write.const-ptr-readonly` — cannot write through a *const pointer

A `*const T` pointer is read-only; writing through it is an error — only `*mut T` or `&mut T` may be written through.

_Source: `src/compiler/sema_stmt.cpp#L561-L562`, `src/compiler/sema_stmt.cpp#L632-L634`_

### `stmt.deref-write.pointer-or-mutref` — deref-write/compound left side must be pointer or mutable reference

The left side of `*p = v` (and `*p op= v`) must be a pointer or a mutable reference; otherwise it is an error.

_Source: `src/compiler/sema_stmt.cpp#L553-L557`, `src/compiler/sema_stmt.cpp#L629-L631`_

### `stmt.deref-write.raw-ptr-unsafe` — writing through a raw pointer requires unsafe

Writing through `&mut T` is safe; writing through a raw pointer (`*mut`/`*const`) requires an unsafe context. Outside unsafe, a raw-pointer deref-write/compound is an error.

_Source: `src/compiler/sema_stmt.cpp#L558-L560`, `src/compiler/sema_stmt.cpp#L625-L628`_

### `stmt.deref-write.rhs-type-hint` — deref-write RHS inferred against the pointee type

The RHS of `*p = v` is inferred with an enum/struct type hint taken from the pointee type when the pointee is a parameterized enum or struct, so a bare `None` resolves to `Option<T>` matching the slot rather than a discriminant-only constant.

_Source: `src/compiler/sema_stmt.cpp#L575-L595`_

### `stmt.deref-write.user-deref-mut` — *x = v on a DerefMut struct dispatches deref_mut

When `*x = v` is applied to a struct `x` implementing DerefMut<T>, it dispatches `x.deref_mut()` (returning &mut T) and writes `v` through the resulting reference; this requires `x` to be a mutable binding for `&mut self` materialization.

_Source: `src/compiler/sema_stmt.cpp#L597-L624`_

### `stmt.deref-write.variance-invariant` — deref-write value must invariantly match the pointee type

In `*ptr = val` the value's type must invariant-match (strict, fn-scope-fixed lifetimes) the pointee type of the pointer/reference.

_Source: `src/compiler/sema_stmt.cpp#L635-L639`_

## Destructuring Assignment

### `stmt.destructure-assign.place-checks` — Destructuring-assignment places must be mutable locals or `_`

Each destructuring-assignment place must name an existing variable, which must be mutable; `_` (or empty) discards by evaluating the accessor for effect only. Assignment to an undefined or immutable variable is an error. Each assigned place is treated as initialised (cleared from the uninitialised set).

_Source: `src/compiler/sema_stmt.cpp#L895-L912`_

### `stmt.destructure-assign.rest-and-redundant-parens` — Destructuring-assignment rest and redundant parens

At most one `..` rest is allowed in a tuple/array destructuring-assignment list; places before it map to low positions and after it to the tail. With no rest the place count must equal the source arity; with a rest the named-place count must not exceed it. A single nested-tuple place whose source arity is not 1 (`((a,b)) = e`) is unwrapped to `(a,b) = e`.

_Source: `src/compiler/sema_stmt.cpp#L959-L989`, `src/compiler/sema_stmt.cpp#L998-L1004`_

### `stmt.destructure-assign.tuple-array-struct` — Destructuring assignment into existing places

Destructuring assignment (RFC 2909) assigns into pre-existing places: `(a,b)=e` reads tuple positions, `[a,b]=e` reads array indices, `S{x:a,y}=e` reads struct fields. The RHS type must match the form (tuple / array / struct respectively); a struct form also requires a struct/zoned-struct RHS.

_Source: `src/compiler/sema_stmt.cpp#L870-L877`, `src/compiler/sema_stmt.cpp#L914-L948`, `src/compiler/sema_stmt.cpp#L1028-L1039`_

## if Statements

### `stmt.if.cond-must-be-bool` — if condition must be bool

A non-pattern `if` condition must have type bool; Error and Never types are accepted (Never permits `if return x {}`), any other type is a compile error.

**Divergence:** Accepting Never as an `if` condition (e.g. `if (return x){}`) is a Logos accommodation, tag G160-10.

_Source: `src/compiler/sema_stmt.cpp#L5955-L5961`_

### `stmt.if.definite-assignment-merge` — Definite-assignment merge across if branches

A variable is uninitialized at the if's merge point iff it is uninitialized on ANY incoming non-diverging branch path (union of currently-uninit sets over non-diverging branches); diverging branches contribute nothing to the merge.

_Source: `src/compiler/sema_stmt.cpp#L5980-L6040`_

### `stmt.if.move-merge-by-branch` — Per-branch move state merges by union over non-diverging branches

Across an `if`'s branches each branch is analyzed from the pre-if move state; the post-if moved set is the union of moves from all non-diverging branches (a branch ending in return/break/continue diverges and contributes nothing). A missing then/else branch behaves as a non-diverging fall-through preserving the pre-if state.

_Source: `src/compiler/sema_stmt.cpp#L5966-L6038`_

### `stmt.if.no-trailing-semi` — if at statement position needs no trailing semicolon

An `if` used in statement position requires no trailing `;` because it is a brace-bounded expression.

_Source: `src/compiler/sema_render.cpp#L887-L892`_

## if-let

### `stmt.if-let.desugar-to-match` — if-let desugars to a two-arm match

`if let P = e { THEN } else { ELSE }` lowers to `match e { P => THEN, _ => ELSE }`, with nested-payload destructures emitted before THEN so their bindings are in scope.

_Source: `src/compiler/sema_stmt.cpp#L5843-L5951`_

### `stmt.if-let.guard-no-nested-variant-binding` — let-chain condition cannot reference nested enum-variant bindings

A let-chain trailing condition may reference nested tuple/struct payload bindings (re-extracted as a guard prologue) but may NOT reference bindings from a nested enum-variant payload pattern; doing so is a compile error.

_Source: `src/compiler/sema_stmt.cpp#L5885-L5902`_

### `stmt.if-let.let-chain-trailing-cond` — let-chain trailing condition becomes a match-arm guard

`if let P = e && <cond> { THEN } else { ELSE }` desugars to `match e { P if <cond> => THEN, _ => ELSE }`: the chain condition becomes an arm guard, sees the pattern's bindings, must be bool (else Error accepted), and is conjoined AFTER the pattern's own refutable guards.

_Source: `src/compiler/sema_stmt.cpp#L5872-L5903`, `src/compiler/sema_stmt.cpp#L5938-L5945`_

### `stmt.if-let.refutable-inner-guards` — Nested refutable payload predicates gate the then-arm

Refutable inner patterns (nested variant/literal payload predicates) of an if-let pattern are conjoined into the then-arm guard; a predicate failure falls through to the wildcard else-arm.

_Source: `src/compiler/sema_stmt.cpp#L5928-L5937`_

## while Loops

### `stmt.while.body-not-definitely-assigning` — while body does not establish definite assignment

A `while`/`while let` loop may execute zero times, so assignments/initializations performed in its body do not count as definitely-initialized in the enclosing scope; the definite-assignment state is restored to its pre-loop value on every exit path.

_Source: `src/compiler/sema_stmt.cpp#L6050-L6058`_

### `stmt.while.cond-or-let` — while and while-let

A while statement is either `while COND BLOCK` (condition form) or `while let PAT = VALUE BLOCK` (while-let form).

_Source: `src/compiler/sema_render.cpp#L777-L790`_

### `stmt.while.condition-bool` — while condition must be bool

In `while COND { ... }`, COND must have type `bool` (or be an error type); any other type is a type error.

```logos
while x < 10 { }
```

_Source: `src/compiler/sema_stmt.cpp#L6225-L6230`_

### `stmt.while.forms` — while and while-let

`while cond { }` is a conditional loop; `while let PAT = e [&& guard] { }` is a while-let loop; `while LET-CHAIN { }` is a while-let chain (≥2 segments starting with let), ordered first so it is not shadowed.

_Source: `tools/peg_gen/grammars/logos.peg#L2277-L2287`_

### `stmt.while.label-binds-loop` — loop label attaches to the while loop itself

A leading loop label `'a:` on a `while`/`while let` binds to that loop (added to the active-label set for its body) and is consumed before lowering the body, so an unlabeled nested loop inside the body cannot capture it; `break 'a` / `continue 'a` inside the body resolve to the labeled loop.

```logos
'a: while cond { while inner { break 'a; } }
```

_Source: `src/compiler/sema_stmt.cpp#L6111-L6116`, `src/compiler/sema_stmt.cpp#L6170-L6176`, `src/compiler/sema_stmt.cpp#L6220-L6246`_

### `stmt.while.line-maps-to-header` — while loop maps to its header source line

The emitted `while` loop is attributed to the source line of the `while` keyword (the loop header), not the last line of its body, for debug/stepping purposes.

_Source: `src/compiler/sema_stmt.cpp#L6059-L6064`, `src/compiler/sema_stmt.cpp#L6243-L6247`_

### `stmt.while.loop-depth-context` — while body is in loop context

Statements in a `while`/`while let` body execute in loop context (loop depth incremented, a break-frame pushed), so `break` and `continue` are permitted there and resolve to this loop.

_Source: `src/compiler/sema_stmt.cpp#L6168-L6177`, `src/compiler/sema_stmt.cpp#L6233-L6242`_

## while-let

### `stmt.while-let.chain-cond-no-nested-variant-bindings` — while-let chain condition cannot reference nested enum-variant bindings

A `while let` chain trailing condition may not reference bindings introduced by a nested enum-variant subpattern of the same pattern; such a reference is an error (match in the body instead).

**Uncertainty:** Stated as a current implementation limitation ('cannot yet'); likely a temporary divergence rather than an intended language rule.

_Source: `src/compiler/sema_stmt.cpp#L6149-L6155`_

### `stmt.while-let.chain-multiseg-desugar` — multi-segment while-let chain desugars to nested if-let-else-break

A multi-segment let-chain `while let P1 = e1 && (let P2 = e2 | cond) && ... { BODY }` desugars to `loop { { if let P1 = e1 { { if let P2 = e2 { ... BODY ... } else { break; } } } else { break; } } }`, building inside-out so each segment (a `let PAT = VALUE` or a boolean condition) wraps the running body and falls to `break` on failure; a chain requires at least 2 segments.

```logos
while let Some(a) = x.next() && let Some(b) = y.next() { f(a, b); }
```

_Source: `src/compiler/sema_stmt.cpp#L6065-L6107`_

### `stmt.while-let.chain-trailing-cond-must-be-bool` — while-let trailing chain condition must be bool

In a `while let PAT = EXPR && COND` chain, the trailing condition COND must have type `bool` (or error); it is evaluated with the pattern's bindings in scope and folded into the match-arm guard so the loop continues only when the pattern matches AND COND holds.

```logos
while let Some(x) = it.next() && x > 0 { }
```

_Source: `src/compiler/sema_stmt.cpp#L6138-L6166`, `src/compiler/sema_stmt.cpp#L6199-L6205`_

### `stmt.while-let.desugar-loop-match` — while-let desugars to loop + match

`while let PAT = EXPR { BODY }` is equivalent to `loop { match EXPR { PAT => { BODY }, _ => break } }`: the loop continues while EXPR matches PAT (binding PAT each iteration), and terminates the first time it does not.

```logos
while let Some(x) = iter.next() { use(x); }
```

_Source: `src/compiler/sema_stmt.cpp#L6108-L6217`_

### `stmt.while-let.refutable-pattern-bindings-scope-body` — while-let pattern bindings scope over body

Bindings introduced by the `while let` pattern are in scope only within the loop body (a fresh scope is pushed for the matched arm and popped after the body).

_Source: `src/compiler/sema_stmt.cpp#L6133-L6185`_

## loop

### `stmt.loop.break-value-type` — Loop expression value comes from break values

If any `break <expr>` targets the loop, the loop expression's type is the unified type of those break values, and the loop yields that value at exit; `break` without value gives the loop unit/no-value but marks it non-diverging.

_Source: `src/compiler/sema_stmt.cpp#L6863-L6866`, `src/compiler/sema_stmt.cpp#L6877-L6880`_

### `stmt.loop.conservative-init` — Loop body may run zero times for init analysis

A `loop { ... }` body is treated as possibly executing zero times: any variable that becomes definitely-initialized only inside the loop body remains uninitialized in the enclosing scope after the loop (the pre-loop uninit set is restored once the body is lowered).

_Source: `src/compiler/sema_stmt.cpp#L6853-L6856`, `src/compiler/sema_stmt.cpp#L6870`_

### `stmt.loop.diverges-never` — Loop with no reachable break diverges (type !)

A `loop` whose body contains no `break` reaching the loop diverges; in expression position its type is `!` (never). A loop is non-diverging iff some `break` targeting it carries a value (giving a result type) or breaks without value.

_Source: `src/compiler/sema_stmt.cpp#L6863-L6873`_

### `stmt.loop.forms` — loop and labeled loop

An infinite loop is `loop BLOCK`; a labeled loop is `'LABEL: BLOCK` where labels use a leading single-quote sigil.

_Source: `src/compiler/sema_render.cpp#L792-L804`_

### `stmt.loop.infinite` — loop block

`loop { ... }` is an unconditional loop.

_Source: `tools/peg_gen/grammars/logos.peg#L1888-L1894`_

### `stmt.loop.label-scope` — Loop label active only within its body

A loop label is bound (pushed onto the active label set) only for the duration of lowering the loop body, and is captured before body lowering so the label of the immediately-enclosing loop is the pending label; nested loops push/pop their labels with their bodies.

_Source: `src/compiler/sema_stmt.cpp#L6846-L6848`, `src/compiler/sema_stmt.cpp#L6859-L6867`_

### `stmt.loop.labeled` — Labeled loop

`'label: for/while/loop { ... }` attaches a lifetime-syntax label to a loop, targetable by `break 'label` / `continue 'label`.

_Source: `tools/peg_gen/grammars/logos.peg#L1891-L1902`_

## Labeled Loops

### `stmt.labeled-loop.label-binding` — labeled loop binds its label to the inner loop

`'label: <loop>` extracts the label, makes it the pending loop label, and lowers the inner for/while/loop with that label in scope so break/continue can target it.

_Source: `src/compiler/sema_stmt.cpp#L317-L330`_

## for Loops

### `stmt.for.each` — For-each loop

`for x in iter { }` iterates over an iterable. The loop variable may be a simple identifier (fast path) or a full destructuring pattern `for (a,b) in v { }`, in which case the pattern is bound against each element.

_Source: `tools/peg_gen/grammars/logos.peg#L1878-L1886`_

### `stmt.for.range` — For-range loop

`for i in lo..hi { }` iterates the exclusive integer range; `for i in lo..=hi { }` iterates the inclusive range.

_Source: `tools/peg_gen/grammars/logos.peg#L1868-L1877`_

### `stmt.for.range-and-iter` — for over range and for-each over iterator

A for-over-range loop is `for NAME in LHS (`..`|`..=`) RHS BLOCK`, where `..=` denotes an inclusive upper bound and `..` an exclusive one; a for-each loop is `for NAME in ITER BLOCK`.

_Source: `src/compiler/sema_render.cpp#L806-L831`_

## break

### `stmt.break.forms` — break

`break;`, `break expr;`, `break 'label;`, and `break 'label expr;` are all valid; a value and/or a target label are optional. A bare `break` may also be terminated by `,`.

_Source: `tools/peg_gen/grammars/logos.peg#L1904-L1911`_

### `stmt.break.outside-loop` — break only inside a loop

A `break` is an error outside any loop. A labeled `break 'l` is an error unless `'l` is an active in-scope loop label.

_Source: `src/compiler/sema_stmt.cpp#L456-L465`_

### `stmt.break.target-resolution` — break targets the matching labeled or innermost loop

A labeled `break 'l v` targets the nearest enclosing loop with label `'l` (searched innermost-out); an unlabeled `break v` targets the innermost loop. The break value attributes to the target frame, so a value breaking to an outer labeled loop is not consumed by an inner loop.

_Source: `src/compiler/sema_stmt.cpp#L466-L502`_

### `stmt.break.value-consistency` — all breaks of a loop must agree on value presence and type

All breaks targeting the same loop must agree: a loop cannot mix value-carrying and value-less breaks (`break v` vs `break`); and all break values must have mutually compatible types, the loop's break type being the unification of them.

_Source: `src/compiler/sema_stmt.cpp#L483-L501`_

## continue

### `stmt.continue.forms` — continue

`continue;` and `continue 'label;` are valid; the target label is optional. A bare `continue` may also be terminated by `,`.

_Source: `tools/peg_gen/grammars/logos.peg#L1913-L1916`_

### `stmt.continue.outside-loop` — continue only inside a loop with in-scope label

A `continue` is an error outside any loop. A labeled `continue 'l` is an error unless `'l` is an active in-scope loop label.

_Source: `src/compiler/sema_stmt.cpp#L504-L514`_

## break / continue (Shared)

### `stmt.break-continue.drops-to-loop-boundary` — break/continue drops frames up to and including the loop body

A `break` or `continue` (including one nested inside an `if`) exits via the loop edge; before the control transfer it drops every live scope frame from the current point down to and including the loop body frame, bypassing the intervening blocks' normal end-of-block drops.

_Source: `src/compiler/sema_stmt.cpp#L665-L670`, `src/compiler/sema_stmt.cpp#L725-L735`_

### `stmt.break-continue.label-value` — break and continue with optional label and value

`break ['LABEL] [VALUE] ;` may carry an optional loop label (single-quote sigil) and an optional break value; `continue ['LABEL] ;` may carry an optional loop label.

_Source: `src/compiler/sema_render.cpp#L833-L855`_

## return

### `borrow.return.intlit-fits` — Integer-literal return must fit return type

An integer-literal return value (including elements of returned array/tuple literals, recursively) must fit the return type's (element's) integer kind; otherwise "return: literal value V does not fit in T".

_Source: `src/compiler/sema_stmt.cpp#L2858-L2927`_

### `stmt.return.drops-after-value-eval` — Return value evaluated before scope drops

On `return e`, e is fully evaluated before any pending scope drops run: the value is hoisted into a fresh temporary, then all in-scope drops execute, then the temporary is returned. This ordering guarantees e may borrow locals that the drops would otherwise release first.

_Source: `src/compiler/sema_stmt.cpp#L699-L724`_

### `stmt.return.form` — return statement

`return [expr];` returns from the enclosing function with an optional value; may be terminated by `;` or `,`.

_Source: `tools/peg_gen/grammars/logos.peg#L2289-L2290`_

### `stmt.return.hint-propagation` — Return type hints literal/closure inference

The declared return type seeds expected-type hints while lowering the return value: a generic enum/struct return type pins literal type-params; a fn-ptr/closure or wrapped-callable (`Box<dyn Fn(..)>`) return type infers an untyped closure literal's params; a (possibly `&`-wrapped) array/slice return type supplies an array-literal element-type hint.

_Source: `src/compiler/sema_stmt.cpp#L2775-L2808`_

### `stmt.return.impl-trait-infer` — impl-Trait return type inferred from first return

When the declared return type is `impl Trait`, the concrete return type is inferred from the first non-error return expression.

_Source: `src/compiler/sema_stmt.cpp#L2830-L2834`_

### `stmt.return.optional-value` — return statement with optional value

A return statement is `return [VALUE] ;`; the value expression is optional (return without a value when no VALUE is present).

_Source: `src/compiler/sema_render.cpp#L711-L722`_

### `stmt.return.type-mismatch` — Return value type-compatibility

`return e` requires `typeof(e)` compatible with the declared return type, after normalizing associated-type projections via equality bounds (`T: Trait<A=V>`); otherwise "return type mismatch — expected T, got U".

_Source: `src/compiler/sema_stmt.cpp#L2835-L2843`_

### `stmt.return.value-required` — Return-without-value only in unit/never/impl-Trait functions

`return;` (no value) is rejected in a function whose return type is not unit, error, or `impl Trait`: "return without value in function returning T".

_Source: `src/compiler/sema_stmt.cpp#L2994-L3001`_

## Tail Expressions

### `stmt.tail.closure-inference-return` — non-void tail in closure inference body is the implicit return

In a closure body lowered in inference mode (no declared return type), a non-void, non-error tail expression is the closure's implicit return; a void/error tail is an expression-statement. No return-type compatibility check is applied (the closure has no declared type).

_Source: `src/compiler/sema_stmt.cpp#L390-L398`_

### `stmt.tail.enum-hint-from-ret-type` — tail enum-literal inference threads the fn return type

When the function's declared return type is an enum, a tail-position enum literal (`Result::Ok(v)`, `Either::L(x)`) is inferred against that return type so the enum's type parameters not constrained by the chosen variant are resolved from the return type rather than inferred as error.

_Source: `src/compiler/sema_stmt.cpp#L402-L419`_

### `stmt.tail.implicit-return-nonvoid` — non-void tail expression is an implicit return at fn body

At function-body level with a non-void declared return type, a trailing expression (no semicolon) is an implicit `return <e>`. If its type is Void it is lowered as an expression-statement instead; only a non-void tail becomes an implicit return.

_Source: `src/compiler/sema_stmt.cpp#L371-L450`_

### `stmt.tail.return-type-mismatch` — tail implicit return type-checked like explicit return

The type of a tail-position implicit return must be compatible with the declared return type, must pass the variance gate, and must satisfy dyn+auto bound checks at coercion — identically to an explicit `return`. Moving out of a value behind a reference or out of an index in tail-return position is rejected (E0507).

_Source: `src/compiler/sema_stmt.cpp#L427-L447`_

## Divergence Analysis

### `stmt.diverge.block-reaches` — block diverges if any statement diverges

A block always returns iff at least one of its statements always returns (a diverging statement makes all following statements unreachable).

_Source: `src/compiler/sema_stmt.cpp#L135-L143`_

### `stmt.diverge.block-value` — block/if/match in expr-stmt position diverges if body does

An expression-statement or tail-expression whose value is a BLOCK, IF, or MATCH diverges iff that nested construct always returns; a value of RETURN/BREAK/CONTINUE expression form always diverges.

_Source: `src/compiler/sema_stmt.cpp#L54-L64`_

### `stmt.diverge.break-continue-divert` — break/continue divert control flow

`break` and `continue` are divergent for loop-body fallthrough analysis: like `return`, control does not fall through to the following statement. if/else and match propagate diversion when all branches/arms divert.

_Source: `src/compiler/sema_stmt.cpp#L145-L182`_

### `stmt.diverge.if-both-branches` — if/else diverges iff both branches diverge

An `if` always returns iff it has an `else` and both the then-block and the else-branch always return; an `if` without `else` never forces a return.

_Source: `src/compiler/sema_stmt.cpp#L100-L109`_

### `stmt.diverge.infinite-loop` — loop without break diverges

A `loop { ... }` statement never falls through to the next statement (it is an infinite loop, diverging unless exited by a non-fallthrough construct).

_Source: `src/compiler/sema_stmt.cpp#L96-L99`_

### `stmt.diverge.let-diverging-init` — let with diverging initializer never binds

`let x = <e>;` diverges when its initializer `<e>` is a RETURN/BREAK/CONTINUE expression, a divergent call, a BLOCK that always returns, or an IF/MATCH that always returns; the binding never occurs.

_Source: `src/compiler/sema_stmt.cpp#L87-L95`_

### `stmt.diverge.match-all-arms` — match diverges iff all arms diverge

A non-empty `match` always returns iff every arm's body always returns; an expression arm (`pat => expr`) provides a value and does not count as diverging; an empty match does not force a return.

_Source: `src/compiler/sema_stmt.cpp#L110-L131`_

### `stmt.diverge.never-returning-call` — call to a Never-returning fn diverges

A call expression `f(...)` (including the macro form `panic!(...)` which parses as FN_MACRO_CALL) in expression-statement, tail-expression, or let-initializer position is divergent — control never falls through — iff the callee is named `panic` OR any candidate function with that name has return type `!` (Never). `panic` is recognized by name even without a `!` annotation.

**Divergence:** A: `panic` recognized as divergent by hardcoded callee name (Logos historically lacked the `!` type); now generalized to any `-> !` callee.

_Source: `src/compiler/sema_stmt.cpp#L34-L53`, `src/compiler/sema_stmt.cpp#L208-L218`_

### `stmt.diverge.return-stmt` — return statement always returns

A `return` statement is a diverging statement: control never falls through to the following statement.

_Source: `src/compiler/sema_stmt.cpp#L28-L29`_

### `stmt.diverge.tail-expr-context` — tail expression counts as implicit return only at fn-body context

A trailing expression (TAIL_EXPR, no semicolon) is treated as an implicit return — and thus a diverging tail — only when it is in function-body position; in match-arm-body or block-as-expression contexts the same node is the block's value, not a return.

_Source: `src/compiler/sema_stmt.cpp#L65-L69`_

## Never-Type Fallback

### `stmt.fallback.never-only-on-provable-divergence` — Never-fallback gated on provably non-returning body

A generic return type-param may fall back to `!` only when the callee body provably never returns normally — i.e. the body's last statement is a divergent call (`panic`/`-> !`), a `loop`, or an expression-statement/tail wrapping a `loop`. A body ending in `return 0;` does NOT qualify (that is a normal return, leaving the type-param ambiguous).

**Divergence:** A: implements a Rust-2024-style `!`-fallback but with a stricter, narrower divergence predicate than full `block_always_returns`.

_Source: `src/compiler/sema_stmt.cpp#L194-L226`_

## Nested Functions

### `stmt.fn.nested-lifts-to-toplevel` — Nested function lifted to a free function

A `fn name(params) [-> T] { body }` at statement position is lifted to a top-level free function (gensym name); the local name is bound as a fn-ptr `let`. A nested fn does not capture enclosing locals.

_Source: `tools/peg_gen/grammars/logos.peg#L303`_

## Nested Functions (Closures)

### `stmt.nested-fn.let-bound-closure` — Statement-position fn desugars to a let-bound closure

A function item at statement position `fn inner(params) [-> T] { body }` is lowered as an immutable local binding `let inner = |params| -> T { body }`; the binding's type is the closure's inferred type.

_Source: `src/compiler/sema_stmt.cpp#L1673-L1691`_

## match

### `stmt.match.arm` — Match arm with optional guard

A match arm is `PAT [if GUARD] => BODY`, where BODY is a block, an expression, or a statement, with an optional trailing comma. The optional `if GUARD` is a guard expression gating the arm.

_Source: `tools/peg_gen/grammars/logos.peg#L1930-L1941`_

### `stmt.match.scrutinee-form` — Match statement

`match SCRUT { ARM* }` matches a scrutinee against arms. A bare-identifier scrutinee is parsed specially (as a var-ref) so `match e { ... }` does not mis-parse `e {` as a struct literal; complex scrutinee expressions fall through to the general expr form.

_Source: `tools/peg_gen/grammars/logos.peg#L1918-L1928`_

