# Ownership: Borrowing and Regions

Scope: the borrow checker and region/lifetime inference of Logos (domains `borrow` and `region`). Rules are extracted from the compiler front-end layers — `sema` (borrow check, move classification, outlives/subtype/variance solvers, region inference, declaration/statement/expression lowering) and `codegen` (closure-environment lowering). Each rule heading is its permanent linkable id; cite rules by that id. Evidence cites the C++ implementation as `file#line`.

## Move vs. copy classification

### `borrow.classify.drop-implies-droppable` — Drop impl or droppable field makes a type need-drop

_Domain: `borrow`._

A struct needs drop iff it has a Drop impl or has any field that (transitively) needs drop; generic instantiations are matched by their concrete (mono-mangled) name so droppable fields of generic containers are not missed.

**Source:** `src/compiler/borrow_check.cpp#L237-L264`

### `borrow.classify.move-vs-copy` — Move vs Copy classification

_Domain: `borrow`._

A value is a Move type (consuming it invalidates its source, and it cannot be moved while borrowed) iff it owns droppable resources and is not Copy; otherwise it is Copy. Structs/enums with a Drop impl are droppable; primitives, raw pointers, &T, and Copy-impl types are Copy. Classification recurses structurally over tuples, arrays, struct fields, and enum payloads, so an aggregate carrying any Move element (e.g. (String, i64)) is itself Move.

**Source:** `src/compiler/borrow_check.cpp#L266-L321`, `src/compiler/borrow_check.cpp#L237-L264`, `src/compiler/borrow_check.cpp#L102-L104`

### `borrow.classify.mut-ref-is-move` — &mut T is move, &T is copy

_Domain: `borrow`._

&mut T is a Move type: passing or binding a &mut reference moves the unique mutable borrow. &T is Copy.

**Source:** `src/compiler/borrow_check.cpp#L277-L283`

### `borrow.classify.type-param-move-unless-copy` — Bare type parameter moves unless Copy-bounded

_Domain: `borrow`._

Inside a generic body a bare type-parameter T is a Move type unless it carries an explicit `T: Copy` bound; partial moves of fields typed T are tracked accordingly.

**Divergence (vs. Rust):** B1

**Source:** `src/compiler/borrow_check.cpp#L284-L297`

## Moves and use-after-move

### `borrow.move.aggregate-recursion` — Tuple/array move-ness is element-wise

_Domain: `borrow`._

A tuple is a move type iff at least one of its element types is a move type; an array [T; N] is a move type iff its element type T is a move type. Move-ness of aggregates is the structural OR over (existing) element types.

**Source:** `include/logos/compiler/move_classify.hpp#L34-L40`

### `borrow.move.borrowed-cannot-move` — Cannot move a borrowed value

_Domain: `borrow`._

Moving `x` is rejected while `x` is mutably borrowed, shared-borrowed, has a mut reservation in flight, or while any field of `x` is borrowed (E0505); a successful move resets `x`'s state and records it as moved.

**Divergence (vs. Rust):** Rust E0505 conformant

**Source:** `src/compiler/borrow_check.cpp#L1402-L1415`

### `borrow.move.box-dyn-owning` — Owning Box<dyn> is a move type

_Domain: `borrow`._

An owning Box<dyn Trait> is always a move type (it owns a heap value whose destructor must run on consumption).

**Source:** `include/logos/compiler/move_classify.hpp#L14-L15`

### `borrow.move.by-value-args` — By-value call arguments are moved

_Domain: `borrow`._

Passing an argument by value to a call moves it: each move-type l-value argument (and each owning `Box<dyn Trait>` binding consumed by a by-value parameter) is marked moved, suppressing scope-end auto-Drop on the caller's binding. Non-move-type / non-owning arguments are unaffected.

**Related:** `borrow.move.by-value-receiver`

**Source:** `src/compiler/sema_expr.cpp#L6433-L6441`

### `borrow.move.by-value-receiver` — By-value method receiver is moved

_Domain: `borrow`._

When the resolved method's formal `self` type is not a reference/pointer (kind not in {Ref, MutRef, Ptr}), the call consumes the receiver: the receiver l-value (move type or owning `Box<dyn Trait>`) is marked moved so scope-end auto-Drop does not double-free ownership transferred into the result. A `&self`/`&mut self`/`*self` receiver is not moved.

**Related:** `borrow.move.by-value-args`

**Source:** `src/compiler/sema_expr.cpp#L6450-L6458`

### `borrow.move.consume-once` — Affine ownership: move consumes once

_Domain: `borrow`._

A Move variable is consumed on its first use in value position; any subsequent use (whole-value or of an already-moved field) is an error (use-after-move).

**Source:** `src/compiler/borrow_check.cpp#L5-L8`, `src/compiler/borrow_check.cpp#L326-L342`

### `borrow.move.enum-drop-or-move-payload` — Enum move-ness

_Domain: `borrow`._

An enum value is a move type iff it is not Copy and either it has a user Drop impl or at least one variant payload is itself a move type.

**Source:** `include/logos/compiler/move_classify.hpp#L18-L19`, `include/logos/compiler/move_classify.hpp#L41`

### `borrow.move.lvalue-path-tracking` — Move of a place tracks the full l-value path

_Domain: `borrow`._

Moving a move-type place marks a precise l-value path: a bare variable marks the variable; a field chain `outer.a.b` rooted at a variable marks `outer.a.b`; a tuple element `t.N` marks `<name>.<index>`. The marked sub-path is then excluded from the aggregate's scope-end drop so that element is not dropped twice.

**Source:** `src/compiler/sema_impl.hpp#L2169-L2208`

### `borrow.move.no-flow-through-raw-ptr` — Ownership does not flow through a raw pointer

_Domain: `borrow`._

A move out of `(*p).field` where any hop in the field chain (including the root) is `*const T`/`*mut T`-typed is NOT a partial move of any tracked owned root; raw-pointer-rooted projections are excluded from move tracking. References (`&`/`&mut`) keep ordinary tracking.

**Source:** `src/compiler/borrow_check.cpp#L3457-L3481`

### `borrow.move.no-move-out-of-array-index` — Cannot move a Drop-bearing element out of a fixed-size array by index

_Domain: `borrow`._

Moving by value out of a fixed-size array element via index (`let s = arr[i]`) is rejected when the element type is concrete and needs Drop, because a single array slot cannot be marked moved (the array would still drop it → double free). Generic element types (TypeVar/AssocType/ImplTrait) are exempted, as are borrows/autoref which do not move.

**Divergence (vs. Rust):** Rust E0508 analog.

**Source:** `src/compiler/sema_impl.hpp#L2209-L2248`

### `borrow.move.no-move-out-of-borrowed-place` — Cannot move a move-typed value out of a borrowed place (E0507)

_Domain: `borrow`._

Moving a move-typed value by value out of a non-owning place is rejected (E0507): deref of a `&`/`&mut` reference variable (`*r`); index `v[i]`/slice-index `s[i]` of a non-raw container (including user `Index`, lowered to `*v.index(i)`); deref of a user `Deref` (`*x.deref()`) including `Box` (`*b`, since DerefMove is unimplemented); and reading a move-typed field out of a `&`/`&mut` receiver (`r.field`). Exempt: any place whose access chain passes through a raw-pointer (`*const`/`*mut`) hop, and partial moves out of owned receivers.

**Divergence (vs. Rust):** Box move-out (`let s = *b`) is rejected because Logos does not implement Rust's built-in Box DerefMove; in Rust it is allowed.

**Source:** `src/compiler/sema_impl.hpp#L877-L968`

### `borrow.move.non-movable-by-value-rejected` — Cannot move a location-anchored value by value

_Domain: `borrow`._

A by-value move of a place whose type is non-movable (location-anchored) is an error; the value must instead be used in place through `&`, `&mut`, or `*mut`.

**Source:** `src/compiler/sema_impl.hpp#L2148-L2168`

### `borrow.move.owning-dyn-by-value` — Passing an owning Box<dyn Trait> by value moves it

_Domain: `borrow`._

An owning `Box<dyn Trait>` binding (collapsed to a bare trait object) is moved when passed by value — the callee frees the handle — so the caller marks it moved to avoid a double-free.

**Source:** `src/compiler/sema_impl.hpp#L2389-L2398`

### `borrow.move.partial-field-path` — Partial move tracks full dotted field paths

_Domain: `borrow`._

Moving a place `root.a.b...` of move type marks that exact path on `root` as moved. A subsequent use is an error if it overlaps the moved path: reading the same path, anything inside it, or any containing parent (including the whole value `root`). Disjoint sibling paths (e.g. `root.a.t` vs moved `root.a.s`) stay usable. A strict-parent read (`root.a` while `root.a.s` moved) errors only for a genuine whole-value read, not when it is merely an intermediate projection toward a disjoint deeper leaf (place-base position).

**Divergence (vs. Rust):** B78/T1-10: full dotted-path granularity (Rust-conformant partial moves).

**Source:** `src/compiler/borrow_check.cpp#L3442-L3520`

### `borrow.move.partial-move-blocks-use` — Use of a partially moved value is rejected

_Domain: `borrow`._

Consuming a value with at least one moved field is rejected as use of a partially moved value; consuming an already wholly-moved value is rejected as use of a moved value.

**Source:** `src/compiler/borrow_check.cpp#L1383-L1401`

### `borrow.move.path-overlap` — Move-path overlap semantics

_Domain: `borrow`._

Two dotted paths conflict iff equal or one is a dot-prefix of the other; reading a moved leaf, a path inside a moved subtree, or a parent containing a moved leaf are all uses of (partially) moved data, while disjoint siblings do not conflict.

**Source:** `src/compiler/borrow_check.cpp#L1350-L1360`

### `borrow.move.recursive-into-producer` — Move propagates into composite producer subexpressions

_Domain: `borrow`._

When a value is produced from a composite expression (Call arg, StructLit field, TupleLit elem, EnumLitData payload, BlockExpr result), any move-typed VarRef carried into the produced value is marked moved and removed from the drop set; FieldRead in producer position is a move of the source. This prevents double-drop / use-after-move for values escaping via construction.

**Source:** `src/compiler/sema_impl.hpp#L2272-L2307`

### `borrow.move.reinit-clears-deeper` — Re-initialization clears equal and deeper move records

_Domain: `borrow`._

Assigning a path `p` re-initializes `p` and all deeper paths `p.*` (clearing their moved state), but a shallower moved entry is NOT resurrected: assigning `o.i.s` does not un-move a moved `o.i`.

**Source:** `src/compiler/borrow_check.cpp#L1369-L1381`

### `borrow.move.scalar-not-move` — Scalar and reference types are not move types

_Domain: `borrow`._

Types other than tuple, array, struct, and enum (e.g. scalars, references, raw pointers) are not move types by default and are consumed by copy.

**Source:** `include/logos/compiler/move_classify.hpp#L43`

### `borrow.move.scope-exit-clears-moved` — Popping a scope clears its variables' moved state

_Domain: `borrow`._

On scope exit, all variables declared in that scope are removed from the moved set (their names go out of scope, so their move state no longer constrains outer code).

**Source:** `src/compiler/sema_impl.hpp#L2037-L2044`

### `borrow.move.shadowing-new-slot` — Each binding gets a fresh dense slot; shadowing rebinds

_Domain: `borrow`._

Every binding registration (let, parameter, pattern, for, closure binding) is assigned the next dense per-function variable slot; shadowing a name yields a NEW slot, so slots uniquely identify bindings independent of the name string.

**Source:** `src/compiler/sema_impl.hpp#L1868-L1874`

### `borrow.move.struct-non-copy` — Struct move-ness

_Domain: `borrow`._

A struct value is a move type iff it is not Copy (a struct that does not satisfy Copy, and whose drop semantics require ownership transfer, is moved on consumption).

**Source:** `include/logos/compiler/move_classify.hpp#L17-L18`, `include/logos/compiler/move_classify.hpp#L42`

### `borrow.move.tuple-element-moved` — Concrete move-type tuple elements are moved into the tuple

_Domain: `borrow`._

A concrete (non-TypeVar) move-type value placed into a tuple element is moved into the tuple (its source binding is marked consumed); TypeVar elements are exempt (their drop is routed through the mono mechanism).

**Divergence (vs. Rust):** TypeVar tuple elements are leniently exempt from move-tracking (note G154-4)

**Source:** `src/compiler/sema_expr.cpp#L1621-L1630`

### `borrow.move.typevar-move-unless-copy` — Generic type parameters are move-classified unless Copy-bound

_Domain: `borrow`._

> **Multiple sources, reworded:** this id was extracted from more than one source layer with differently-phrased but mutually consistent statements. All variants are reproduced verbatim below so no wording is silently chosen; if you find them in genuine conflict, treat this as a flag to reconcile at the source.

- Variant 1 (`src/compiler/borrow_check.cpp#L725-L726`): A bare type-parameter (TypeVar) value is classified as move (consumed on use) unless its parameter has an explicit Copy bound, matching Rust generic-body semantics where T is move-by-default.
- Variant 2 (`include/logos/compiler/move_classify.hpp#L14-L16`): A generic type parameter T is treated as a move type unless it is bound by T: Copy; absent a Copy bound the parameter is conservatively assumed to own a non-Copy value.

**Source:** `src/compiler/borrow_check.cpp#L725-L726`, `include/logos/compiler/move_classify.hpp#L14-L16`

### `borrow.move.value-invalidates-source` — Move type definition

_Domain: `borrow`._

A value is a move type iff consuming it invalidates the source, i.e. it owns a non-Copy value. Consuming a non-move (Copy) value leaves the source usable; consuming a move value renders the source inaccessible.

**Source:** `include/logos/compiler/move_classify.hpp#L22-L23`, `include/logos/compiler/move_classify.hpp#L30-L32`

### `borrow.move.var-consume` — Reading a variable in a consuming position moves a non-Copy value

_Domain: `borrow`._

A bare variable reference `x` evaluated in a consuming position moves `x` when its type is a move (non-Copy) type; the binding is thereafter dead and any later use is an error. In a non-consuming position the variable is only checked-live, not consumed.

**Source:** `src/compiler/borrow_check.cpp#L3294-L3314`

### `borrow.move.write-into-cell-is-move` — Writing a move-type value into a memory cell moves it

_Domain: `borrow`._

Writing a move-type RHS into a memory cell (deref, indexed, field) is a move of the source: the source's scope-exit auto-drop is suppressed and Drop responsibility transfers to the destination.

**Source:** `src/compiler/sema_impl.hpp#L2069-L2077`

## Use of moved or borrowed places

### `borrow.use.moved-or-mut-borrowed` — Use of moved or mutably-borrowed value

_Domain: `borrow`._

A value-use of `x` is rejected if `x` is moved (use of moved value) or while `x` is mutably borrowed.

**Source:** `src/compiler/borrow_check.cpp#L1444-L1455`

## Variable reference and definite assignment

### `borrow.var-ref.definite-assignment` — Use of a possibly-uninitialised binding is an error

_Domain: `borrow`._

Reading a binding declared without an initializer (`let x: T;`) before a definite assignment on the current path is a compile error (Rust E0381); at if/match merge points a binding is uninitialised if uninitialised on any incoming path.

**Source:** `src/compiler/sema_expr.cpp#L588-L594`

### `borrow.var-ref.use-after-move` — Use of a moved variable is an error

_Domain: `borrow`._

Reading a variable after its value has been moved out is a compile error: 'use of moved variable'.

**Source:** `src/compiler/sema_expr.cpp#L586-L587`

## Taking borrows

### `borrow.take.call-arg-mut-reservation` — Two-phase borrow reservation during call-argument evaluation

_Domain: `borrow`._

Inside function-call argument evaluation, a `&mut x` is taken as a reservation that is compatible with shared borrows of `x` created during the same argument evaluation, but is rejected if any shared borrow of `x` pre-exists from an outer scope. Two `&mut x` in the same call (overlapping reservations) still conflict.

**Divergence (vs. Rust):** Rust two-phase borrow conformant

**Source:** `src/compiler/borrow_check.cpp#L1278-L1321`

### `borrow.take.field-borrow-blocks-whole-mut` — Any field borrow blocks a whole-value &mut

_Domain: `borrow`._

Taking `&mut x` is rejected if any field-path borrow (shared or mut) of `x` is active.

**Source:** `src/compiler/borrow_check.cpp#L1265-L1272`

### `borrow.take.moved-cannot-borrow` — Cannot borrow a moved value

_Domain: `borrow`._

Taking any borrow (shared or mut) of a value that has been moved is rejected.

**Source:** `src/compiler/borrow_check.cpp#L1245-L1249`

### `borrow.take.mut-exclusive` — &mut is exclusive with existing mut and shared borrows

_Domain: `borrow`._

Taking `&mut x` is rejected if `x` is already mutably borrowed, if another mut reservation is in flight, or (outside call-arg evaluation) if any shared borrow of `x` is active.

**Source:** `src/compiler/borrow_check.cpp#L1273-L1328`

### `borrow.take.mut-requires-mut-binding` — &mut requires a mut binding

_Domain: `borrow`._

Taking `&mut x` is rejected unless `x` is declared `mut` or is a function parameter.

**Source:** `src/compiler/borrow_check.cpp#L1259-L1264`

### `borrow.take.shared-vs-mut` — Shared borrow excludes an active mut borrow

_Domain: `borrow`._

Taking `&x` is rejected if `x` is already mutably borrowed or if any field of `x` is mutably borrowed; otherwise multiple shared borrows coexist.

**Source:** `src/compiler/borrow_check.cpp#L1329-L1343`

## Receiver borrows

### `borrow.recv.bare-place-self-conflict` — Bare-place method receiver self-borrow conflict

_Domain: `borrow`._

A method call on a whole-variable (non-field) bare-place receiver borrowing `self` conflicts with a live borrow of that variable: it is rejected if the variable is already mutably borrowed, or (for `&mut self`) if it has active shared borrows or any active field borrow. A raw-pointer-typed root is unchecked; reference-typed roots are checked.

**Source:** `src/compiler/borrow_check.cpp#L1601-L1624`

## Reborrowing

### `borrow.reborrow.ref-deref` — &*ptr reborrow

_Domain: `borrow`._

`&*p` where `p` has pointer kind (raw `*T`, `&T`, or `&mut T`) is a reborrow yielding `&(pointee(p))` preserving the reborrow shape; for a struct with a Deref impl `&*x` is `&(x.deref())`.

**Source:** `src/compiler/sema_expr.cpp#L2535-L2552`

## Borrow exclusivity

### `borrow.exclusivity.disjoint-field-paths` — Disjoint field paths borrow independently

_Domain: `borrow`._

Borrows are tracked by dotted field path; disjoint field paths of the same value may be borrowed simultaneously, even mutably. Two borrows conflict iff one path is a prefix of the other (equal included); a whole-value borrow is path "" and conflicts with every field path.

**Source:** `src/compiler/borrow_check.cpp#L343-L350`, `src/compiler/borrow_check.cpp#L521-L529`

### `borrow.exclusivity.shared-vs-mut` — Shared vs exclusive borrow exclusivity

_Domain: `borrow`._

&T (shared) may be held multiply and blocks moves and &mut of the same place; &mut T (exclusive) permits one at a time and blocks moves and all other borrows of the same place.

**Source:** `src/compiler/borrow_check.cpp#L10-L14`, `src/compiler/borrow_check.cpp#L327-L336`

### `borrow.exclusivity.two-phase` — Two-phase borrows for &mut call arguments

_Domain: `borrow`._

A &mut x taken as a function-call argument is reserved during the rest of argument evaluation and activated at call entry; a reservation does not block concurrent shared reads but does block other mutable borrows.

**Source:** `src/compiler/borrow_check.cpp#L333-L336`

## Borrow conflicts

### `borrow.conflict.mut-while-borrowed` — Cannot take &mut a place that is already borrowed

_Domain: `borrow`._

A new borrow of `root.path` conflicts when `root` (or an overlapping path) is already borrowed: a `&mut` borrow conflicts with any existing mutable borrow, with any existing shared borrow, and with any existing shared-field or mut-field borrow whose path overlaps `path`. Path overlap is prefix-or-equal in either direction.

**Divergence (vs. Rust):** B81/B93.2: method-receiver/auto-borrow sites get the same path-aware conflict checks as explicit &mut.

**Source:** `src/compiler/borrow_check.cpp#L3393-L3424`

### `borrow.conflict.overlapping-regions` — Borrow conflict requires same target, a mutable participant, and overlapping live regions

_Domain: `borrow`._

Two borrows b1, b2 of the same borrow target conflict iff (b1.target == b2.target) AND (b1.is_mut OR b2.is_mut) AND the inferred live point-sets of their regions intersect (∃ point P ∈ region(b1) ∩ region(b2)). Two shared (&) borrows of the same target never conflict; a borrow of a distinct target never conflicts.

**Source:** `src/compiler/region_infer.cpp#L841-L868`

### `borrow.conflict.read-vs-mut-borrow` — Whole/field read collides with an outstanding mut borrow (E0503)

_Domain: `borrow`._

Reading a whole value or a field path while an overlapping mut borrow is outstanding is an error (E0503). A shared field borrow leaves whole/overlapping reads legal; only mut field borrows block reads. A partial MOVE of a path collides with ANY outstanding overlapping borrow (E0505: need_exclusive). Skipped in borrow-source position (`&root.path`), where the AddrOf site already resolved the conflict.

**Source:** `src/compiler/borrow_check.cpp#L3300-L3313`, `src/compiler/borrow_check.cpp#L3504-L3514`

### `borrow.conflict.tpb-reservation-shared-read` — A mut reservation passed as a call argument tolerates concurrent shared reads of the same target

_Domain: `borrow`._

A two-phase-borrow (TPB) mut-reservation does not conflict with a concurrent shared (&) borrow of the same target: when one of a conflicting pair is a TPB reservation and the other is non-mut, no conflict is reported. A reservation still conflicts with any other mut borrow or reservation of the same target.

**Divergence (vs. Rust):** B82 (two-phase-borrow reservation compatible with shared reads)

**Source:** `src/compiler/region_infer.cpp#L849-L854`

## Mutation through borrows

### `borrow.mut.require-mut-binding` — &mut of a place requires a mut binding (or parameter)

_Domain: `borrow`._

Taking `&mut x` (explicit AddrOf or implicit AddrOfTemp auto-borrow) requires the root binding `x` to be declared `mut`, unless it is a function parameter. Borrowing through a reference root (`&`/`&mut`) needs no `mut` on the reference binding. A raw-pointer root is unchecked.

**Source:** `src/compiler/borrow_check.cpp#L3321-L3373`

## Binding mutability

### `borrow.mutability.binding-mut-required` — Mutation/&mut requires a mut binding

_Domain: `borrow`._

Taking &mut x or assigning to x requires x to be a `let mut` binding; against an immutable binding both are rejected.

**Source:** `src/compiler/borrow_check.cpp#L337-L339`

## Two-phase borrows

### `borrow.two-phase.call-arg-reservation` — Mutable borrows in call-argument position are two-phase

_Domain: `borrow`._

A `&mut` borrow taken while evaluating the arguments of a call (call/method-call/closure-call/fn-pointer-call/format-call) is a two-phase-borrow reservation rather than an immediately-active mutable borrow.

**Source:** `src/compiler/region_infer.cpp#L352`, `src/compiler/region_infer.cpp#L376`, `src/compiler/region_infer.cpp#L422-L458`

## Auto-borrow of method receivers

### `borrow.autoborrow.method-receiver-transient` — Method-call receiver borrow is scoped to the call

_Domain: `borrow`._

A `&self`/`&mut self` method borrows its receiver for the duration of the call only; the implicit receiver borrow is released at the enclosing scope-pop (NLL), so consecutive calls `b.foo(); b.bar();` do not conflict. A bare-place receiver (VarRef/FieldRead, not an explicit AddrOfTemp) still incurs the whole-root conflict check: `&mut self` (kind 2) vs an outstanding borrow of the receiver root errors (iterator-invalidation, e.g. `let r=&v[i]; v.push(..)`).

**Divergence (vs. Rust):** B93.2/B94: auto-borrows are check-only and NLL-released, not recorded.

**Source:** `src/compiler/borrow_check.cpp#L3344-L3433`, `src/compiler/borrow_check.cpp#L3543-L3562`

## Places and field paths

### `borrow.place.field-path-extraction` — Borrow place: field path with index/deref granularity

_Domain: `borrow`._

The borrowed place of &expr is computed by walking field reads (accumulating a dotted path), index/slice steps (whole-element granularity: prior path components are discarded and the path is the route to the container, no disjointness on the index value), and reference/owning-container derefs (which root the borrow on the deref'd variable). The walk terminates at a variable reference giving the root; `&*r` roots on r.

**Source:** `src/compiler/borrow_check.cpp#L537-L644`

### `borrow.place.raw-ptr-no-borrow` — Indexing or dereferencing a raw pointer creates no tracked borrow

_Domain: `borrow`._

Indexing through a raw pointer (p[i], p: *mut/*const T) or dereferencing a raw pointer (*p) is an unsafe raw access that creates no tracked borrow of the base; aliasing safety is the programmer's responsibility inside unsafe (Rust parity).

**Source:** `src/compiler/borrow_check.cpp#L577-L602`, `src/compiler/borrow_check.cpp#L603-L629`

## Path overlap

### `borrow.path.access-vs-field-borrow` — Accessing a place conflicts with overlapping field borrows

_Domain: `borrow`._

Accessing target.path is rejected if it overlaps a tracked mutable field-borrow of the same root; an exclusive access (whole move or partial move) additionally conflicts with any overlapping shared field-borrow, whereas a plain read conflicts only with a mutable field-borrow.

**Divergence (vs. Rust):** E0503/E0505

**Source:** `src/compiler/borrow_check.cpp#L1131-L1165`

### `borrow.path.prefix-conflict` — Field-path borrow conflict by prefix overlap

_Domain: `borrow`._

Path P is a prefix of path Q iff P==Q or Q begins with P+".". Two field-path borrows of the same root conflict iff their paths overlap (one is a prefix of the other) AND at least one is mutable. The empty path denotes the whole value and overlaps every path.

**Source:** `src/compiler/borrow_check.cpp#L1112-L1125`, `src/compiler/borrow_check.cpp#L1140-L1141`

## Field borrows

### `borrow.field.mut-binding-required` — &mut of a field requires a mut binding (non-reference root)

_Domain: `borrow`._

`&mut x.p` where `x` has a non-reference value type is rejected unless `x` is declared `mut` or is a function parameter.

**Source:** `src/compiler/borrow_check.cpp#L1202-L1209`

### `borrow.field.mutability-through-reference-root` — Field mutation legality from reference-typed root

_Domain: `borrow`._

When the root `x` of a field place `x.p` has reference type, mutation legality is determined by the reference TYPE not the binding's `mut`: a `&mut`-typed root permits `&mut x.p`; a `&`-typed (shared) root rejects `&mut x.p` (E0596); the mut-binding declaration check is skipped for reference-typed roots.

**Divergence (vs. Rust):** Rust E0596 conformant

**Source:** `src/compiler/borrow_check.cpp#L1192-L1209`

### `borrow.field.overlapping-path-conflict` — Field borrows conflict on overlapping paths

_Domain: `borrow`._

`&mut x.p` is rejected if any overlapping path `x.q` is already shared-borrowed; any new borrow of `x.p` is rejected if an overlapping path is already mutably borrowed. Two paths overlap when equal or one is a dot-prefix of the other; disjoint sibling fields do not conflict.

**Related:** `borrow.move.path-overlap`

**Source:** `src/compiler/borrow_check.cpp#L1210-L1227`

### `borrow.field.whole-borrow-blocks-field` — Whole-value borrow blocks any field borrow

_Domain: `borrow`._

A field-path borrow of `x.p` is rejected if `x` is already mutably borrowed; a `&mut x.p` is also rejected if `x` has any active shared borrow.

**Source:** `src/compiler/borrow_check.cpp#L1175-L1186`

## Union field borrows

### `borrow.union.field-borrow-borrows-all` — Borrowing one union field borrows the whole union

_Domain: `borrow`._

Because union fields share storage, a borrow of any one field of a union implicitly borrows ALL fields; field-path borrows of a union root are coerced to whole-root borrows so any other field-path of the same union overlaps and conflicts.

**Source:** `src/compiler/borrow_check.cpp#L934-L949`

## Indexed writes

### `borrow.indexwrite.borrowed-container` — Element write through a borrowed container is rejected

_Domain: `borrow`._

An element write `arr[i] = v` (and `recv.f[i] = v`) is an error while the container/receiver root has any active borrow (shared or mutable); the write goes through the container and conflicts with the outstanding borrow. The index and value are consumed.

```logos
let r = &arr[0]; arr[1] = v;  // error: arr is borrowed
```

**Source:** `src/compiler/borrow_check.cpp#L2837-L2854`, `src/compiler/borrow_check.cpp#L2859-L2876`, `src/compiler/borrow_check.cpp#L2904-L2932`

## Field writes

### `borrow.fieldwrite.reinit-field` — Field write reinitializes the moved-out field and its subpaths

_Domain: `borrow`._

Writing to a field `r.f = v` reinitializes `r.f`, clearing the partially-moved state of `r.f` and every path beneath it; the receiver is no longer treated as partially-moved on account of `f`. The value is consumed.

```logos
let _ = s.v; s.v = w;  // ok: s.v rebound
```

**Source:** `src/compiler/borrow_check.cpp#L2805-L2828`, `src/compiler/borrow_check.cpp#L2933-L2963`

## Assignment

### `borrow.assign.borrowed-lhs` — Assignment to a borrowed variable is rejected

_Domain: `borrow`._

Assigning to a variable `x` (`x = v`) is an error while `x` has any active borrow: a shared borrow ⇒ "cannot assign to 'x' because it is borrowed"; a mutable borrow ⇒ "cannot assign to 'x' while it is mutably borrowed".

```logos
let r = &x; x = 1;  // error: x is borrowed
```

**Source:** `src/compiler/borrow_check.cpp#L2748-L2755`

### `borrow.assign.immutable-var` — Write to immutable binding rejected

_Domain: `borrow`._

A write whose place is rooted at a non-`mut` local variable is rejected ('assignment to immutable variable').

**Source:** `src/compiler/sema_stmt.cpp#L6995-L6998`

### `borrow.assign.raw-ptr-unsafe` — Write through raw pointer requires *mut and unsafe

_Domain: `borrow`._

A write through a place rooted at a raw pointer `*const T` is rejected; through `*mut T` it is permitted only inside an `unsafe` context.

**Source:** `src/compiler/sema_stmt.cpp#L6970-L6979`, `src/compiler/sema_stmt.cpp#L7007-L7020`, `src/compiler/sema_stmt.cpp#L7043-L7053`

### `borrow.assign.reinit-reowns` — Assignment re-owns the destination

_Domain: `borrow`._

An assignment `x = v` clears any prior move/borrow state of `x` (re-owns it): after the assignment `x` is a fully-initialized, unmoved binding. The RHS is consumed (moved) unless it is a reference value or an aggregate literal, in which case its constituent borrows are tracked instead.

**Source:** `src/compiler/borrow_check.cpp#L2756-L2770`

### `borrow.assign.shared-ref` — Write through shared reference rejected

_Domain: `borrow`._

A write through a place rooted at a shared reference `&T` (or a shared `&DstStruct`) is rejected; a write through `&mut T` (or a `&mut DstStruct`) is permitted.

**Source:** `src/compiler/sema_stmt.cpp#L6958-L6969`, `src/compiler/sema_stmt.cpp#L7011-L7013`

### `borrow.assign.shared-slice-elem` — Element write through shared slice rejected

_Domain: `borrow`._

Writing an element through a shared slice `&[T]` (`s[i] = v`) is rejected; a `&mut [T]` slice is writable.

**Source:** `src/compiler/sema_stmt.cpp#L7034-L7042`

### `borrow.assign.static-mut-unsafe` — static mut write requires unsafe; immutable static not writable

_Domain: `borrow`._

A write to a place rooted at a `static mut` is permitted (storage is mutable) but requires an `unsafe` block; a write to a plain immutable `static` is rejected.

**Divergence (vs. Rust):** Rust-conformant (items.static.mut.safety)

**Source:** `src/compiler/sema_stmt.cpp#L6981-L6994`

## `let` bindings

### `borrow.let.borrow-carrying-routes-loan` — let RHS that is a reference / closure / borrow-carrying value / aggregate literal records a loan held by the binding

_Domain: `borrow`._

When a `let name = val` has a value that is a reference, a closure, a borrow-carrying-typed value (e.g. `v.iter_mut()`), or an aggregate literal containing borrows, the borrows in `val` are recorded as loans held by `name` (released at `name`'s last use, NLL). Otherwise the value is consumed (move-tracked).

**Related:** `borrow.aggregate.literal-held-borrow`, `borrow.closure.capture-by-ref-loan`

**Source:** `src/compiler/borrow_check.cpp#L2666-L2697`

### `borrow.let.box-dyn-owning-drop` — Owning Box<dyn Trait> binding drops only when ownership is transferred

_Domain: `borrow`._

An `let x: Box<dyn Trait>` binding (which collapses to a bare owning TraitObject) is marked for drop (drop_in_place + free) only when the RHS genuinely transfers ownership — a `box_new(..) as Box<dyn>` cast or a value-returning constructor call. Reads of a handle copy out of a container (deref / index / method-call) are excluded to avoid double-free.

**Source:** `src/compiler/sema_stmt.cpp#L1706-L1712`, `src/compiler/sema_stmt.cpp#L2223-L2240`

### `borrow.let.move-rhs-variable` — let of a move-typed place marks the source moved

_Domain: `borrow`._

If the RHS of a let is a place (variable reference or struct-field-read chain) of a move type, the source is marked moved (recording dotted paths so per-field auto-drop on the source struct is suppressed).

**Source:** `src/compiler/sema_stmt.cpp#L2242-L2247`

### `borrow.let.no-move-behind-ref` — Cannot move a move-typed value out of a reference deref (E0507)

_Domain: `borrow`._

`let s = *r` that would move a move-typed value out of a `&`/`&mut` deref of a reference variable is rejected (E0507); copy-typed values copy out fine.

**Source:** `src/compiler/sema_stmt.cpp#L1932-L1941`

### `borrow.let.provenance-propagation` — Provenance flows from RHS to a let binding of reference, borrow-carrying, or lifetime-parameterized struct type

_Domain: `borrow`._

A `let` binding whose declared type is a reference or borrow-carrying type inherits the RHS reference-provenance. A binding of a struct/zoned-struct type that carries lifetime arguments also inherits the RHS provenance (it borrows through its lifetime parameter).

**Source:** `src/compiler/borrow_check.cpp#L2701-L2718`

### `borrow.let.ref-from-temp-dangles` — A let-bound reference borrowing into a per-statement temporary is rejected (E0716)

_Domain: `borrow`._

Binding a reference (or borrow-carrying value) whose provenance is a temporary value dropped at the end of the binding statement is an error: the reference would outlive the temporary it borrows into. The owning value must first be bound to a variable so it outlives the borrow.

```logos
let v = make().view();  // error
let h = make(); let v = h.view();  // ok
```

**Divergence (vs. Rust):** Rust E0716 (temporary dropped while borrowed)

**Source:** `src/compiler/borrow_check.cpp#L2701-L2714`

### `region.let.temporary-lifetime-extension` — let-init borrow of an rvalue extends the temporary

_Domain: `region`._

`let p = &<rvalue>` / `let p = &mut <rvalue>` (rvalue = scalar literal or value-producing call/struct/tuple literal) extends the temporary's lifetime to the enclosing scope: a hidden named temporary holds the value and is dropped at scope end, and the binding is rewritten to borrow that named temporary. A void/Never/error-typed rvalue keeps the degenerate inline spill (nothing to drop).

```logos
let p = &String::from("x");
```

**Source:** `src/compiler/sema_stmt.cpp#L1773-L1895`

## Enum-literal payloads

### `borrow.enum-lit.payload-consumes-source` — Enum payload arguments are moved

_Domain: `borrow`._

Each payload argument of move type is marked moved at construction; constructing an enum literal consumes its move-type payload sources, preventing later use.

**Source:** `src/compiler/sema_expr.cpp#L12268-L12276`

### `borrow.enum-lit.payload-move` — Payload arguments consume move-typed sources

_Domain: `borrow`._

Each move-typed payload argument consumes (moves out of) its source expression at the enum-literal construction site.

**Source:** `src/compiler/sema_expr.cpp#L12629-L12636`

### `expr.enum-lit.lifetime-inference` — Lifetime-arg inference from payload args

_Domain: `region`._

Lifetime arguments of the constructed enum are inferred by structurally co-walking each variant payload formal type against the corresponding argument's type, binding each formal lifetime to the first argument lifetime seen across reference, tuple, struct, zoned-struct and enum positions.

**Source:** `src/compiler/sema_expr.cpp#L12353-L12383`, `src/compiler/sema_expr.cpp#L12508-L12522`

### `region.enum-lit.lifetime-arg-inference` — Enum lifetime args inferred from payload reference lifetimes

_Domain: `region`._

An enum's lifetime parameters are inferred by walking each (declared payload type, actual argument type) pair, mapping reference/struct/enum/tuple lifetimes back to the enum's lifetime parameters; unresolved lifetime parameters yield empty lifetime args on the constructed type.

**Source:** `src/compiler/sema_expr.cpp#L12018-L12053`, `src/compiler/sema_expr.cpp#L12146-L12165`

## `match` arms

### `borrow.match.arm-binding-drop` — arm-scope pattern bindings are dropped before the arm value escapes

_Domain: `borrow`._

Pattern bindings introduced by a non-divergent value-form arm are dropped at arm-scope exit; bindings consumed by the arm value are first marked moved (lower_return semantics), then remaining droppables are dropped after the arm value is hoisted into a temporary that is yielded. Error/Never-typed arm values skip this.

**Related:** `borrow.match.scrutinee-moved-by-binding`

**Source:** `src/compiler/sema_stmt.cpp#L9552-L9592`

### `borrow.match.definite-assignment-merge` — definite-assignment merges across match arms like if/else

_Domain: `borrow`._

Definite-assignment state is reset to the pre-match state for each arm and merged after the match as the union of still-uninitialized variables over non-diverging arms (a variable is uninitialized post-match iff uninitialized on any falling-through arm). Diverging arms contribute nothing.

**Source:** `src/compiler/sema_stmt.cpp#L8438-L8444`, `src/compiler/sema_stmt.cpp#L8818-L8829`

### `borrow.match.merge-arms` — Match move-state is the union over non-diverging arms

_Domain: `borrow`._

Each match arm is checked from the pre-match state; a diverging arm (return/break/continue tail) contributes nothing to the join. The post-match state unions moves across surviving arms (a path moved in any surviving arm is moved after the match); borrow-source and dangling facts are likewise unioned (a binding borrows/dangles if any arm makes it so). If every arm diverges, the match diverges.

**Source:** `src/compiler/borrow_check.cpp#L3076-L3159`

### `borrow.match.per-arm-move-reset` — move state resets per arm; post-match is union over non-diverging arms

_Domain: `borrow`._

Each match arm is checked from the move state before the match; a variable's post-match moved status is the union of moves from arms that fall through (do not return/break/continue). Diverging arms contribute no post-match moves.

**Source:** `src/compiler/sema_stmt.cpp#L8424-L8444`, `src/compiler/sema_stmt.cpp#L8546-L8550`, `src/compiler/sema_stmt.cpp#L8804-L8827`

### `borrow.match.scrutinee-moved-by-binding` — binding+moving a payload out of a by-value scrutinee marks it moved

_Domain: `borrow`._

A match-expression that binds and moves a payload out of a by-value move-type scrutinee (`let x = match v { Ok(s) => s }`) marks the scrutinee moved, so its scope-exit Drop does not double-free a value the result already owns (G156-2).

**Related:** `expr.match.temp-scrutinee-dropped`, `borrow.match.arm-binding-drop`

**Source:** `src/compiler/sema_stmt.cpp#L8938-L8944`

## `if` branches

### `borrow.if.merge-survivors` — If/else move-state merge keeps only surviving branches

_Domain: `borrow`._

After `if c { A } else { B }`, the post-state is the merge (union of moves) of the non-diverging branches: if both branches diverge the whole `if` diverges; if exactly one diverges, the post-state is the surviving branch's; otherwise the two branches' move and provenance states are merged.

**Source:** `src/compiler/borrow_check.cpp#L3002-L3036`

## Loops

### `borrow.loop.body-borrows-scoped` — Loop bodies: borrows are body-scoped, only moves of outer vars propagate

_Domain: `borrow`._

A loop body is analyzed in its own scope; borrows taken inside are released at body end. Only moves of OUTER variables propagate out of the loop. Provenance is merged conservatively across the loop (the body may execute zero or more times). Loop-iteration variables are local to the body.

**Source:** `src/compiler/borrow_check.cpp#L2636-L2655`

### `borrow.loop.body-fixpoint` — Loop bodies are borrow-checked under loop semantics

_Domain: `borrow`._

While/For/Loop/ForEach bodies are checked via the loop-body protocol: the condition (while) and range bounds (for) are consumed before the body; for-each iterators are inspected non-consumingly; the loop variable is a fresh binding scoped to the body.

**Source:** `src/compiler/borrow_check.cpp#L3039-L3073`

## Control-flow join of move state

### `borrow.flow.break-continue-diverge` — Break and continue diverge the current statement flow

_Domain: `borrow`._

`break` and `continue` mark the current control-flow path as diverged for move-state propagation; their post-state does not flow to the following statements at the same level.

**Source:** `src/compiler/borrow_check.cpp#L3161-L3166`

### `borrow.flow.diverged-arm-skipped` — Diverging branch arms do not contribute moves to the join

_Domain: `borrow`._

A branch arm that diverges (ends in return/break/continue) is excluded from the move-state merge: its moves do not pollute the join, since control reaching the join cannot have come through that arm.

**Note:** Merge-skip logic enforcing this lives in If/Match merge sites outside this slice; here only the cur_diverged_ flag is declared.

**Source:** `src/compiler/borrow_check.cpp#L784-L788`

### `borrow.flow.if-branch-merge` — If-expression merges move/provenance state across branches

_Domain: `borrow`._

An `if`/`else` expression evaluates the condition consuming, then each branch from the same pre-branch state; the post-state is the merge of both branches (a value moved in either branch is treated as moved after; provenance is unioned).

**Source:** `src/compiler/borrow_check.cpp#L3674-L3688`

### `borrow.flow.match-branch-merge` — Match merges move/provenance across arms; arm bindings are arm-scoped

_Domain: `borrow`._

A `match` evaluates the scrutinee non-consuming, then each arm from the same pre-match state with its pattern bindings declared in an arm-local scope (guard evaluated consuming). The post-match state merges all arms: a place moved in any arm (when present in the pre-state) is treated as moved after; provenance is unioned across arms.

**Source:** `src/compiler/borrow_check.cpp#L3691-L3723`

### `borrow.flow.merge-moved-on-join` — Move state at control-flow join is the union of branches

_Domain: `borrow`._

At a control-flow merge of two branch states, a place is considered moved in the joined state iff it is moved in EITHER branch (move-state is unioned, not intersected); a non-diverging branch's moves propagate into the join.

**Related:** `borrow.flow.diverged-arm-skipped`

**Source:** `src/compiler/borrow_check.cpp#L648-L652`

## Merging move sets

### `borrow.merge.moves-union` — Branch merge unions move state; borrows are scope-local

_Domain: `borrow`._

At control-flow joins, move (Phase-1) state is merged conservatively by union of moved sets; borrows are scope-local (released by scope pop) and do not survive merges. Variables of outer scope moved inside a loop body are dead after the loop.

**Source:** `src/compiler/borrow_check.cpp#L20-L23`, `src/compiler/borrow_check.cpp#L646-L652`

## Returns

### `borrow.return.diverges` — Return consumes its value and diverges control flow

_Domain: `borrow`._

A `return v` consumes (moves) `v` and marks the current control-flow path as diverged, so its post-state does not flow to a join point.

**Source:** `src/compiler/borrow_check.cpp#L2790-L2797`

### `borrow.return.mark-moved` — Return expression moves its move-type subexpressions

_Domain: `borrow`._

Any move-type variable or field appearing in the return expression (recursing through enum-payloads, call args, struct/tuple-literal fields, and block-expr results) is marked moved so scope-exit drop collection does not double-free it.

**Source:** `src/compiler/sema_stmt.cpp#L2928-L2977`

### `borrow.return.no-move-out-of-ref` — Cannot return a move-out of a reference/index

_Domain: `borrow`._

Returning a move-type value taken out of a value behind a reference or out of an index is rejected (E0507).

**Source:** `src/compiler/sema_stmt.cpp#L2849-L2850`

### `region.return.dangling-local-or-temp` — Returning a reference with local or temporary provenance is rejected as dangling

_Domain: `region`._

If the function returns a reference (or borrow-carrying) type and the returned expression's provenance is is_local or is_temp, the return is an error: a reference to a temporary (is_temp / AddrOfTemp source) reports 'cannot return reference to temporary value: dangling reference'; a reference to a named local reports 'cannot return reference to local variable <name>: dangling reference'.

**Source:** `src/compiler/borrow_check.cpp#L1854-L1882`

### `region.return.elision-single-ref-param` — Elided return lifetime with one reference parameter must derive from that parameter

_Domain: `region`._

When the return lifetime is elided ('_) and exactly one reference-typed parameter exists, the returned reference's provenance must include that sole parameter; otherwise 'lifetime elision: return reference must derive from <p> (the only reference parameter)' is reported. With multiple ref parameters the source is ambiguous and any param source is accepted.

**Source:** `src/compiler/borrow_check.cpp#L1944-L1960`

### `region.return.named-lifetime-must-outlive` — Returned borrow source lifetime must equal or outlive the declared return lifetime

_Domain: `region`._

When the return type has an explicit (non-'_) lifetime 'ret, each traced parameter source's declared lifetime 'src must satisfy 'src == 'ret OR 'src outlives 'ret (an explicit `where 'src: 'ret`, or 'static); otherwise a 'lifetime mismatch: return type has lifetime 'ret but <src> has lifetime 'src' error is reported.

**Source:** `src/compiler/borrow_check.cpp#L1884-L1940`

### `region.return.non-ref-param-source-deferred` — Returns sourced from a non-reference (aggregate) parameter defer to the type checker

_Domain: `region`._

When the return-type lifetime check finds no ref-typed parameter sources (provenance traces to a struct/aggregate parameter holding refs, e.g. returning `x.y` with `y: &'b u8`), no lifetime error is raised; the declared-return type match is trusted as already verified by the type checker. Likewise an elided outer ref lifetime over an aggregate-pointing param defers to the type checker.

**Note:** B86: deferral characterized as compensating for incomplete impl-level lt_arg propagation; the normative content is that such returns are accepted.

**Source:** `src/compiler/borrow_check.cpp#L1887-L1903`, `src/compiler/borrow_check.cpp#L1922-L1935`

### `region.return.temp-drops-before-terminator` — Return-value temporaries drop before the return terminator

_Domain: `region`._

When lowering a `return <value>` whose value materialized statement-temporaries, the value is pre-bound to a local and the temporaries are dropped before the `return` terminator (`let __t..; let __rv = value; drop __t..; return __rv;`) so the drops are not dead code after the terminator.

**Source:** `src/compiler/sema_impl.hpp#L4229-L4234`

### `region.return.untraced-is-safe` — A returned borrow with empty, non-local provenance is accepted

_Domain: `region`._

If the returned expression has empty provenance and is not local/temp (e.g. a function-call result, global, or untraceable expression), the return is accepted even when ref parameters exist (conservative non-error).

**Source:** `src/compiler/borrow_check.cpp#L1961-L1968`

## Closure-capture liveness

### `borrow.live.closure-captures` — Closure capture requires live captured variables

_Domain: `borrow`._

Forming a closure checks each captured variable is live (not moved/dead) at the point of capture.

**Source:** `src/compiler/borrow_check.cpp#L3760-L3764`

## Liveness dataflow

### `borrow.liveness.backward-dataflow` — Variable liveness computed by backward dataflow to fixed point

_Domain: `borrow`._

Liveness over program points (block, stmt-index) is solved by the standard backward dataflow equations: live_out(P) = ∪ live_in(succ(P)); live_in(P) = use(P) ∪ (live_out(P) \ def(P)). Within a block the successor of a statement is the next statement; the successor of the last statement is the union of live_in at the first statement of each CFG successor block; an empty block forwards its successors' live-in. The solution is the least fixed point.

**Source:** `src/compiler/region_infer.cpp#L772-L835`

## Regions and dangling references (borrow side)

### `borrow.region.contains-origin` — A borrow's region contains its creation point

_Domain: `borrow`._

Every borrow expression `&x`, `&mut x`, or a borrow of a temporary creates a fresh region that must contain the CFG point at which the borrow is taken.

**Source:** `src/compiler/region_infer.cpp#L344-L361`, `src/compiler/region_infer.cpp#L363-L385`

### `borrow.region.dangling-after-scope-exit` — Use of reference after referent leaves scope (E0597)

_Domain: `region`._

A reference/borrow-carrying binding whose referent local has gone out of scope is flagged dangling; the first subsequent use reports E0597 ('does not live long enough'), once per binding. A stored borrow never used after its referent dies is sound (NLL).

**Divergence (vs. Rust):** Rust NLL E0597 conformant; line-granular release (DIVERGENCES B6 closed)

**Source:** `src/compiler/borrow_check.cpp#L1433-L1456`

### `borrow.region.nll-liveness-extent` — Borrow region spans the liveness of its holder (NLL)

_Domain: `borrow`._

When a borrow is bound to a named holder (let/assign LHS), its region must contain every CFG point at which the holder variable is live. A borrow's region thus extends exactly to last-use of the reference, not to end of lexical scope (non-lexical lifetimes).

**Source:** `src/compiler/region_infer.cpp#L85-L104`

## Non-lexical lifetimes

### `borrow.nll.capture-flow-store` — Storing a borrowing argument into a receiver taints the receiver's provenance

_Domain: `borrow`._

When a `&mut self` method is called on a tracked local receiver and a by-value borrow-carrying argument (or an argument whose ref-type equals the receiver container's element type, e.g. `Vec<&T>::push(&x)`) is stored into the receiver, the receiver transitively acquires the argument's borrow of the source local. A later use of the receiver after that source local dies is then E0597. `&self` reads and `&x` ref-args do not taint (so `v.contains(&x)`/`v.len()` stay clean).

**Divergence (vs. Rust):** B6: NLL E0597 via capture-flow on container-element stores.

**Source:** `src/compiler/borrow_check.cpp#L3563-L3604`

### `borrow.nll.dangling-ref-first-use-error` — NLL E0597: a borrow outliving its referent errors at first later use

_Domain: `borrow`._

A reference/borrow-carrying binding that outlives a local it borrows becomes dangling when that local goes out of scope; this is not an error in itself — only the FIRST subsequent USE of the dangling binding is rejected (NLL: a stored borrow never used after its referent dies is accepted). A binding dying in the same scope as its source is always fine.

**Divergence (vs. Rust):** B6

**Source:** `src/compiler/borrow_check.cpp#L765-L775`, `src/compiler/borrow_check.cpp#L878-L893`

### `borrow.nll.release-at-last-use` — Borrows are non-lexical: released once the holder's last use has passed

_Domain: `borrow`._

Each named local has a last-use line = the maximum line at which it is read across the function body. A borrow whose holder's last-use line is <= the current statement line is released (its loan on the borrowed place is cleared). Both whole-value and field-path borrows release this way; borrows with no named holder are never released by this mechanism.

**Source:** `src/compiler/borrow_check.cpp#L2304-L2311`, `src/compiler/borrow_check.cpp#L2579-L2623`

## Borrow scopes

### `borrow.scope.borrows-released-at-scope-end` — Borrows are released at the end of their lexical scope

_Domain: `borrow`._

Each borrow (shared, mutable, or outstanding mut-reservation) is held by the scope frame in which it was taken; on exit from that scope the borrow is released (shared count decremented, mut flag cleared, or one reservation removed), restoring the target's borrow availability.

**Source:** `src/compiler/borrow_check.cpp#L826-L855`

### `borrow.scope.lexical-and-nll` — Borrow scope: lexical with NLL release

_Domain: `borrow`._

A bound borrow lives in the scope of its holder binding but is released once the holder's last use has passed (non-lexical lifetimes). Call-site borrows (&x in arguments) not bound to a holder are transient and released after the call.

**Source:** `src/compiler/borrow_check.cpp#L13-L14`, `src/compiler/borrow_check.cpp#L507-L529`

### `borrow.scope.shadowing-fresh-slot` — Each binding gets a fresh slot; shadowing allocates a new slot

_Domain: `borrow`._

Each binding occupies a fresh dense slot allocated at definition; re-declaring a name in the same/inner scope (shadowing) allocates a new slot rather than reusing the prior one (unless a pattern pre-reserved a slot for an Or-alternative).

**Source:** `src/compiler/sema_impl.hpp#L2347-L2355`

### `borrow.scope.stored-borrow-outlives-referent` — Every binding records its borrow sources for end-of-scope outlives checking (E0597)

_Domain: `borrow`._

Every `let` binding records the local borrow sources of its value, so that at scope exit a stored borrow that outlives its referent can be detected and rejected.

**Divergence (vs. Rust):** Rust E0597 (borrowed value does not live long enough)

**Source:** `src/compiler/borrow_check.cpp#L2728-L2730`

## Scoped (held) borrows

### `borrow.scoped.addrof-takes-place-borrow` — &place takes a borrow of that place, of the reference's mutability

_Domain: `borrow`._

An `&x` / `&mut x` (AddrOf) takes a scoped borrow of x whose mutability is that of the formed reference type, held under the binding holder.

**Source:** `src/compiler/borrow_check.cpp#L1988-L1994`

### `borrow.scoped.call-result-aliases-ref-args` — A call result bound to a reference holds borrows of every reference argument

_Domain: `borrow`._

When a function-call result is bound to a reference (`let r = f(&a, &b)`), each reference-typed argument is borrowed under the holder (the let binding); a mutation or `&mut` of any such argument while r is live is rejected. NLL releases at the holder's last use. (Conservative upper bound matching Rust elision.)

**Source:** `src/compiler/borrow_check.cpp#L2119-L2135`

### `borrow.scoped.conditional-borrow-all-branches` — A conditionally-formed reference borrows every branch operand for the holder's scope

_Domain: `borrow`._

When a reference is formed through a control-flow expression (`if c { &mut x } else { &mut y }`, `match t { A => &x, _ => &y }`), a scoped borrow is taken on every branch's borrowed place (both x and y), held under the binding holder. Non-borrow sub-expressions (condition, scrutinee, guards) are visited normally.

**Source:** `src/compiler/borrow_check.cpp#L1975-L1980`, `src/compiler/borrow_check.cpp#L2112-L2117`, `src/compiler/borrow_check.cpp#L2209-L2221`

### `borrow.scoped.field-path-borrow-disjoint` — A field-path borrow is path-precise; disjoint sibling fields may be borrowed independently

_Domain: `borrow`._

A borrow of a field chain `&o.f.g` takes a path-aware (dotted) borrow on the root, so disjoint sibling fields borrow without conflict; a borrow whose path overlaps (equal or prefix of) a moved field reports 'use of moved field <root>.<f> (moved on line N)'.

**Source:** `src/compiler/borrow_check.cpp#L2048-L2107`

### `borrow.scoped.index-reborrow-borrows-receiver` — &v[i] borrows the whole indexed container (its receiver)

_Domain: `borrow`._

An indexing reference `&v[i]` / `&mut v[i]` desugars to `&*(Vec::index(&v,i))`; the borrow is recorded on the index method's receiver (the whole container v), so a `v.push()` while the element ref is live is rejected (iterator/element invalidation). `&mut v[i]` forces the receiver borrow to be mutable even when the desugared index_mut self-kind is unresolved.

**Divergence (vs. Rust):** Element borrow is whole-container coarse (rustc E0499 parity for aliasing two `&mut v[i]`).

**Source:** `src/compiler/borrow_check.cpp#L2026-L2047`

### `borrow.scoped.index-subexpr-visited-before-borrow` — Index/sub-expressions of a borrowed place are checked before the place is borrowed

_Domain: `borrow`._

When a borrowed place contains an index or other sub-expression, the inner expression is visited (its sub-checks run) before the borrow is registered on the root, avoiding a spurious self-conflict where the recursive visit of the root sees its own freshly-set borrow.

**Source:** `src/compiler/borrow_check.cpp#L2075-L2107`

### `borrow.scoped.method-result-holds-receiver-borrow` — A reference-returning method holds a borrow of its receiver for the result's lifetime

_Domain: `borrow`._

A method whose result borrows self (fully-elided &self->&ret, or borrow-carrying result) holds a scoped borrow of the receiver's root place under the holder, with the receiver's mutability; `let v = c.get_ref(); c.set(...)` while v is live is rejected. The borrow is field-precise when the receiver is a field chain, and whole-root otherwise.

**Source:** `src/compiler/borrow_check.cpp#L2136-L2199`

### `borrow.scoped.method-self-mutability` — The receiver borrow mutability follows the method's self kind

_Domain: `borrow`._

The receiver borrow held for a self-borrowing method result is mutable iff the method takes self by mutable reference (method_self_kind == 2) or an outer `&mut` reborrow forced it mutable; otherwise it is shared.

**Source:** `src/compiler/borrow_check.cpp#L2138-L2142`, `src/compiler/borrow_check.cpp#L2186-L2198`

### `borrow.scoped.raw-pointer-root-unchecked` — Borrows through a raw-pointer root are not tracked

_Domain: `borrow`._

When a self-borrowing method's receiver roots at a raw pointer, no receiver borrow is recorded (raw pointers are outside borrow checking, Rust parity). Borrows through `&`/`&mut` reference roots are tracked.

**Source:** `src/compiler/borrow_check.cpp#L2153-L2159`, `src/compiler/borrow_check.cpp#L2173-L2199`

### `borrow.scoped.rc-arc-root-exempt` — Self-borrowing method results on Rc/Arc roots do not hold a receiver borrow

_Domain: `borrow`._

When a self-borrowing method's bare-VarRef receiver roots at an Rc or Arc value, no scoped receiver borrow is recorded: shared-ownership handles are the blessed interior-mutability domain, so `h.array()` followed by `hold(&mut h, root)` is permitted.

**Divergence (vs. Rust):** Logos-specific exemption for Rc/Arc receivers (residency-escape / interior-mutability pattern).

**Source:** `src/compiler/borrow_check.cpp#L2160-L2199`

### `borrow.scoped.reborrow-borrows-ref-not-pointee` — A reborrow registers a borrow on the reference variable, not its pointee

_Domain: `borrow`._

A reborrow of shape `&*r` / `&mut *r` where r is reference-typed registers a borrow on r itself (freezing r for the borrow's scope), not on r's pointee. NLL releases on the holder's last use, restoring r. Reborrow mutability comes from the formed reference, drawing on r's borrow capacity rather than r's binding-mutness.

**Source:** `src/compiler/borrow_check.cpp#L1998-L2025`

### `borrow.scoped.union-field-is-whole-value` — A field borrow on a union root is a whole-value borrow

_Domain: `borrow`._

When the borrowed place's root is a union, a field-path borrow is redirected to a whole-value borrow of the root (all sibling fields of a union alias).

**Source:** `src/compiler/borrow_check.cpp#L2071-L2107`

## Temporaries

### `borrow.temp.distinct-targets` — Each borrow of a temporary is a distinct borrow target

_Domain: `borrow`._

Each borrow of a distinct temporary value is a distinct borrow with its own region; two temporary borrows never alias the same borrow target.

**Source:** `src/compiler/region_infer.cpp#L369-L374`

### `region.temp.receiver-hoist` — Droppable rvalue auto-ref receiver lives to end of statement

_Domain: `region`._

A fresh owning (move-type) rvalue receiver auto-referenced to `&self`/`&mut self` is hoisted into the enclosing statement's block as a named local so it lives to end-of-statement and is then dropped by scope exit, matching Rust temporary-scope semantics; non-droppable or place/borrow receivers keep a plain temp spill.

**Source:** `src/compiler/sema_expr.cpp#L80-L98`

### `region.temp.ref-into-temporary-dangles-on-escape` — Reference into a statement-scoped temporary dangles on escape

_Domain: `region`._

A reference borrowing into a statement-scoped temporary (a fresh value with no named storage: a literal, struct/tuple/array/enum literal, or call/method/closure-call result, including compiler-materialized `__rtmp_N` receivers) dangles the moment it escapes its enclosing statement (e.g. `let v = make().view();`).

**Divergence (vs. Rust):** Rust E0716 (temporary dropped while borrowed)

**Source:** `src/compiler/borrow_check.cpp#L436-L443`, `src/compiler/borrow_check.cpp#L457-L481`

## Per-function scope

### `borrow.fn.scope-is-per-function` — Borrow checking is strictly per-function

_Domain: `borrow`._

Borrow/move/dangling/dropck state is established freshly per function: parameters are declared as initialized bindings, reference parameters record their lifetime (and pointee aggregate lifetime args), and no borrow facts cross function bodies. A body-less function (extern/stub) is not borrow-checked.

**Source:** `src/compiler/borrow_check.cpp#L3188-L3267`

## Closures

### `borrow.closure.capture-by-ref-loan` — Non-move closure captures register field-path borrows of captured places

_Domain: `borrow`._

A non-`move` closure capturing place `p` by reference registers a borrow of `p` held for the closure value's lifetime: a mutated/`&mut` capture registers a `&mut` (exclusive) loan, a shared capture registers a `&` (shared) loan. Captures of a strict sub-field `p.x` register a precise FIELD-PATH borrow (so disjoint sibling access `&mut p.y` beside `|| p.x` is allowed, conflicting `&mut p.x` is rejected); a whole-root capture registers a whole-value borrow for `&mut` (or a liveness check for shared). The loan is released at the closure holder's last use (NLL).

**Divergence (vs. Rust):** RFC-2229 disjoint closure capture: field-path precision, but a whole-var SHARED capture is treated as a liveness check only (not a recorded shared borrow) to avoid blocking sibling mutation

**Related:** `borrow.closure.move-takes-ownership`

**Source:** `src/compiler/borrow_check.cpp#L2233-L2267`

### `borrow.closure.disjoint-field-capture` — RFC-2229 disjoint closure capture of a struct field path

_Domain: `borrow`._

A closure captures the narrowest field path it uses rather than the whole variable (RFC-2229). For an escaping `move` closure a narrow capture owns only the leaf FIELD value inline (the field's Drop runs via env glue; the original root still drops the rest); a non-escaping narrow capture borrows the whole root by pointer. The captured path is the LCA-widening of all uses, so the body only reads the captured leaf.

**Source:** `src/compiler/mlir_gen_dyn.cpp#L1833-L1841`, `src/compiler/mlir_gen_dyn.cpp#L1978-L2007`, `src/compiler/mlir_gen_dyn.cpp#L2140-L2146`

### `borrow.closure.move-owns-non-move-borrows` — move closure owns by-value captures; non-move closure borrows

_Domain: `borrow`._

A non-`move` closure captures by reference (env stores a pointer/borrow of the outer variable; mutations escape to the outer variable). A `move` closure transfers ownership of each by-value capture into the env: an owned droppable capture is dropped by the closure's env drop glue and NOT by the original scope (exactly one owner). `&dyn` handle captures remain borrows even under `move` (a dyn value-fat-pair is a borrowed handle, not owned storage).

**Source:** `src/compiler/mlir_gen_dyn.cpp#L1791-L1832`, `src/compiler/mlir_gen_dyn.cpp#L2117-L2147`

### `borrow.closure.move-takes-ownership` — move closure captures take ownership, registering no borrow

_Domain: `borrow`._

A `move` closure takes ownership of each captured place; it registers no borrow (the captured value is consumed into the closure rather than loaned).

**Related:** `borrow.closure.capture-by-ref-loan`

**Source:** `src/compiler/borrow_check.cpp#L2243`, `src/compiler/borrow_check.cpp#L2238-L2239`

### `borrow.closure.mut-scalar-fnmut-state` — Mutated scalar capture: move owns a persistent copy, non-move escapes

_Domain: `borrow`._

For a mutated scalar (let-bound) capture: a `move` closure stores a value copy in the env and reads/writes through the env field (the mutation persists across calls as FnMut state and never touches the outer variable, which is Copy so needs no drop); a non-`move` closure stores the outer variable's address so mutations escape to the outer variable.

**Source:** `src/compiler/mlir_gen_dyn.cpp#L1780-L1800`, `src/compiler/mlir_gen_dyn.cpp#L2026-L2040`

### `borrow.closure.skip-source-drop-on-body-move` — Per-capture skip of source-side drop when body moves the capture

_Domain: `borrow`._

For each capture, the closure's source-side owned-drop is skipped iff the closure body itself moved that capture into a callee, because the callee's parameter drop is then the canonical drop site; this prevents a double-drop. The body-moved-capture set is computed as the variables newly moved during body lowering relative to a pre-body snapshot.

**Source:** `src/compiler/sema_expr.cpp#L14233-L14241`, `src/compiler/sema_expr.cpp#L14335-L14338`

### `region.closure.escaping-env-heap-allocated` — Escaping (boxed) closure heap-allocates its environment

_Domain: `region`._

A closure that escapes its defining frame (e.g. boxed as `Box<dyn Fn>`) and has captures must heap-allocate its env so the {fn, env_ptr} value outlives the frame; a non-escaping closure with captures uses a stack env; a capture-less closure uses a null env pointer (its drop is a no-op).

**Source:** `src/compiler/mlir_gen_dyn.cpp#L2081-L2115`

## Aggregate literals

### `borrow.aggregate.literal-held-borrow` — Borrows inside an aggregate literal are held by the binding the aggregate flows into

_Domain: `borrow`._

A `&`/`&mut` borrow placed into a struct/tuple/array LITERAL field or element is held for the lifetime of the binding the aggregate flows into (same holder), released at that holder's last use (NLL). Non-borrow fields/elements are consumed (move-tracked) as ordinary values.

```logos
let g = Guard { r: &mut f };
let t = (&a, &b);
let arr = [&x];
```

**Source:** `src/compiler/borrow_check.cpp#L2278-L2295`, `src/compiler/borrow_check.cpp#L2269-L2277`

## Call arguments

### `borrow.callargs.scope-ref-borrows` — Call-site reference arguments create scoped borrows released after the call

_Domain: `borrow`._

Each call's arguments are evaluated inside a fresh call-site borrow scope: a reference-typed argument takes its referenced borrows for the call, a non-reference argument is consumed, and all such call-site borrows are released when the scope pops after the call.

**Source:** `src/compiler/borrow_check.cpp#L3281-L3290`, `src/compiler/borrow_check.cpp#L3609-L3627`

## Calls

### `expr.call.args-move-tracking` — By-value move-type arguments are marked moved

_Domain: `borrow`._

Passing a by-value argument of a move type (including an owning `Box<dyn>`) marks the source binding as moved.

**Source:** `src/compiler/sema_expr.cpp#L3330`, `src/compiler/sema_expr.cpp#L3583-L3584`

### `expr.call.outlives-cross-check` — Caller cross-checks callee where 'a: 'b bounds

_Domain: `region`._

At a non-generic concrete call, the callee's declared lifetime-outlives constraints (`where 'a: 'b`) are checked against the actual arguments.

**Source:** `src/compiler/sema_expr.cpp#L3326-L3328`

## Generic invocation

### `borrow.invoke.by-value-arg-moved` — By-value move-type call argument is marked moved

_Domain: `borrow`._

A by-value argument of move (non-Copy) type passed to a closure/fn-ptr call is marked moved so its owning scope does not also drop it (preventing double-free). Arguments are NOT marked moved when the parameter is a reference (`&T`/`&mut T`) or when the argument's type is an un-substituted TypeVar (move-ness unknown in a generic body).

**Source:** `src/compiler/sema_expr.cpp#L6246-L6288`

## Method calls

### `borrow.method.args-moved` — Method arguments tracked as moved

_Domain: `borrow`._

Passing arguments to a method call marks those argument values as moved for borrow/move analysis.

**Source:** `src/compiler/sema_expr.cpp#L9038`, `src/compiler/sema_expr.cpp#L9132`

### `region.method.lifetime-subst` — Return-type lifetime substitution from call site

_Domain: `region`._

A lifetime substitution is built by structurally walking each method formal param against its actual (receiver paired with param0, args with the rest), binding each method lifetime to the corresponding caller lifetime; this substitution is applied (with type-arg substitution) to the method return type so e.g. `fn get<'a>(&'a self)->Item<'a>` called with `&'b` yields `Item<'b>`.

**Source:** `src/compiler/sema_expr.cpp#L9054-L9105`

## Pass / mono propagation

### `borrow.pass.generic-template-checked` — Generic fn bodies are borrow-checked even when never instantiated

_Domain: `borrow`._

A dedicated pre-monomorphization pass borrow-checks generic function bodies directly (exclusivity-only mode, no region inference, imprecise move tracking on TypeVars), so an uninstantiated generic is still checked. The post-mono pass checks concrete functions and specializations with full region inference. Functions loaded from a precompiled binary module and extern functions are skipped (already checked when their layer was built).

**Divergence (vs. Rust):** Rust-conformant (uninstantiated generics are still checked).

**Source:** `src/compiler/borrow_check.cpp#L3788-L3818`, `src/compiler/borrow_check.cpp#L3849-L3852`

### `borrow.pass.region-conflict-diag` — Region inference reports overlapping-borrow conflicts

_Domain: `borrow`._

Region inference runs before the lexical borrow check and shares the same declared `'a: 'b` outlives source; it reports a conflict when two borrows of the same target have overlapping live regions where at least one is mutable. The later borrow (by source line) is the offending one and the earlier is reported as the still-live borrow.

**Divergence (vs. Rust):** B72/B73: NLL region-based conflict diagnostics.

**Source:** `src/compiler/borrow_check.cpp#L3805-L3846`

## Drop glue

### `borrow.drop.break-continue-to-loop-frame` — break/continue runs drops down to enclosing loop body

_Domain: `borrow`._

A `break`/`continue` runs the destructors of every scope frame from the innermost enclosing frame down to and including the enclosing loop-body frame, so a break/continue nested inside an `if` still drops the loop body's locals.

**Source:** `src/compiler/sema_impl.hpp#L2261-L2264`

### `borrow.drop.needs-drop-recursive` — Drop-required iff own Drop or droppable field

_Domain: `borrow`._

A type needs drop glue iff it has a user/own drop function OR it transitively contains at least one droppable field (needs_drop(t) = own_drop(t) ∨ has_droppable_fields(t)).

**Source:** `src/compiler/sema_impl.hpp#L2252-L2256`

## Drop-check

### `borrow.dropck.assign-borrow-into-drop-struct` — Dropck-light: borrows stored in a Drop lifetime-struct are tracked

_Domain: `borrow`._

When an assignment fills a dropck-relevant (Drop-having, lifetime-parameterised) struct binding with freshly-borrowed locals, those borrow sources are recorded against the binding so its later Drop cannot reference a borrow whose source has died.

**Note:** Pattern is detected syntactically (dropck-light); exact soundness scope depends on struct_is_dropck_relevant/collect_borrow_locals outside this unit.

**Source:** `src/compiler/borrow_check.cpp#L2739-L2743`, `src/compiler/borrow_check.cpp#L2771-L2782`

### `borrow.dropck.drop-binding-must-outlive-borrowed-local` — A Drop-having binding may not borrow a local that dies first

_Domain: `borrow`._

If a binding's type has a Drop impl and its declared type carries a lifetime parameter (dropck-relevant), then every local it borrows at construction must outlive the binding; a local going out of scope while such a binding still lives is rejected (the binding's Drop would run after the local dies).

**Divergence (vs. Rust):** B87

**Source:** `src/compiler/borrow_check.cpp#L856-L877`, `src/compiler/borrow_check.cpp#L918-L933`

### `borrow.dropck.record-local-borrow-sources` — Dropck-relevant let bindings record the local places they borrow from

_Domain: `borrow`._

When a `let` binds a value whose type is Drop-and-lifetime (dropck) relevant, the local variables the value borrows from are recorded against the binding (with its line), so that drop-order checking can detect a borrowed referent being dropped before the borrowing binding.

**Source:** `src/compiler/borrow_check.cpp#L2719-L2727`

## Pinned (non-movable) types

### `borrow.pin.non-movable-no-by-value-slot` — Location-anchored types may not occupy a by-value slot

_Domain: `borrow`._

A non-movable (location-anchored) type — one with a self-relative `#[rel_ptr]`/`#[zoned2]` field, or a `#[pinned]` type — may not be bound to any by-value slot (let local, parameter, match/for/closure/destructure binding); it must live behind a pointer, in place (e.g. an arena or `[u8;N]` buffer), and be built through a `*mut T`.

**Divergence (vs. Rust):** A8: `#[pinned]` is non-movability as a property of the TYPE (no value-form), distinct from Rust's pointer-level Pin<P>.

**Source:** `src/compiler/sema_impl.hpp#L2327-L2346`, `src/compiler/sema_impl.hpp#L2440-L2464`

## Borrow escape

### `borrow.escape.borrow-carrying-struct` — borrow_carrying struct values are escape-tracked like references

_Domain: `borrow`._

Values of a struct annotated `#[borrow_carrying]` are tracked by the borrow checker for escape/lifetime like ordinary references.

**Divergence (vs. Rust):** A: #[borrow_carrying] Logos addition for opaque borrow-holding types (WAny).

**Source:** `src/compiler/sema_decl.cpp#L1197-L1199`, `src/compiler/sema_decl.cpp#L1412`

### `borrow.escape.borrow-carrying-value` — #[borrow_carrying] values escape-tracked like references

_Domain: `borrow`._

A `#[borrow_carrying]` value type may contain an absolute Ref into an arena; the borrow checker tracks its escape like a reference — a method/ctor returning one ties the result to its ref receiver/arg, so returning it past the source's scope is rejected unless laundered through a holder.

**Source:** `src/compiler/sema_impl.hpp#L2465-L2472`, `src/compiler/sema_impl.hpp#L2586`

### `region.escape.borrow-carrying-type` — Borrow-carrying types and transitive containers are escape-tracked

_Domain: `region`._

A `#[borrow_carrying]` type (e.g. WAny) is escape-tracked like a reference, as is any generic container whose type-argument is transitively borrow-carrying (`Vec<WAny>`, `Option<WAny>`, `Box<WAny>`). A residency-exempt laundered-escape type (`Held<T>`/`HeldAny`) is never borrow-carrying, including via its type-arguments. A raw pointer (no type-args) is not borrow-carrying.

**Divergence (vs. Rust):** Logos addition (#[borrow_carrying] arena escape model)

**Source:** `src/compiler/borrow_check.cpp#L1626-L1651`

### `region.escape.value-local-root-walk` — Value-local root of a borrow place

_Domain: `region`._

The dangling-root of a borrow/receiver place is found by walking one optional leading address-of-temp, then a field/tuple-index/index/deref projection chain to a terminal variable, where a raw-pointer deref stops the walk (pointee not tied to pointer's stack lifetime). The terminal must be a value local (not a parameter, not a tracked reference binding) for the reference to be considered dangling-on-escape.

**Divergence (vs. Rust):** Rust parity (raw-pointer deref breaks provenance, cf. box_leak)

**Source:** `src/compiler/borrow_check.cpp#L1653-L1689`

## Borrow-carrying types

### `region.borrow-carrying.escape-tracked` — #[borrow_carrying] values are escape-tracked like references

_Domain: `region`._

A value of a `#[borrow_carrying]` struct or enum holds a borrow into an arena and is escape-tracked like a reference; returning it escapes the borrow as if returning the bare reference. Borrow-carrying-ness propagates transitively: a struct with an inline field, or an enum with a variant payload, of a (transitively) borrow-carrying type is itself borrow-carrying, as is a container whose generic type-argument is borrow-carrying (e.g. Vec<WAny>).

**Divergence (vs. Rust):** Logos addition (no Rust equivalent)

**Source:** `src/compiler/borrow_check.cpp#L52-L54`, `src/compiler/borrow_check.cpp#L137-L164`, `src/compiler/borrow_check.cpp#L204-L227`

### `region.borrow-carrying.residency-holder-exempt` — Residency-holder packages are exempt from borrow-carrying

_Domain: `region`._

A struct with an Rc/Arc field (a residency-holder / laundered-escape package such as Held<T>/HeldAny) ref-counts the arena alive independent of any local, so it is NOT borrow-carrying and may safely escape — even via its type-arguments. An explicit `#[borrow_carrying]` annotation overrides this auto-exemption.

**Divergence (vs. Rust):** Logos addition (no Rust equivalent)

**Source:** `src/compiler/borrow_check.cpp#L55-L60`, `src/compiler/borrow_check.cpp#L165-L203`, `src/compiler/borrow_check.cpp#L207-L209`

## Generic bodies

### `borrow.generic.copy-bound-is-copy-type` — A type parameter is move unless it carries a Copy bound

_Domain: `borrow`._

In a generic body a bare type parameter `T` is move-classified for use-after-move tracking unless `T` is declared with a `Copy` bound, in which case its values are Copy and not consumed on use.

**Divergence (vs. Rust):** B1

**Source:** `src/compiler/borrow_check.cpp#L3216-L3226`

### `borrow.generic.exclusivity-only-pre-mono` — Generic templates borrow-check exclusivity only, deferring move checks

_Domain: `borrow`._

When borrow-checking a generic function template before monomorphization, only borrow-exclusivity conflicts are reported; move/use-after-move diagnostics are suppressed (imprecise over TypeVar values) and are fully checked on each monomorphized specialization.

**Divergence (vs. Rust):** Implementation strategy, not a user-visible language divergence; final move-checking is Rust-conformant on concrete instantiations.

**Source:** `src/compiler/borrow_check.cpp#L3180-L3186`

## Consuming evaluation positions

### `borrow.eval.consuming-positions` — Operand consumption is determined by expression position

_Domain: `borrow`._

Operands are evaluated consuming in value positions and non-consuming in place/borrow positions: BinOp/Unary operands, call arguments, struct/array/tuple/enum-payload literal elements, and index expressions are consuming; Deref, AddrOf source, place-base receivers (FieldRead/IndexRead/TupleIndex receivers), slice/closure-callee operands are non-consuming; Cast and Try and Block-result propagate the surrounding consuming flag.

**Source:** `src/compiler/borrow_check.cpp#L3436-L3439`, `src/compiler/borrow_check.cpp#L3526-L3536`, `src/compiler/borrow_check.cpp#L3630-L3672`, `src/compiler/borrow_check.cpp#L3727-L3772`

## Diagnostic deduplication

### `borrow.diag.dedupe-across-mono` — Identical borrow diagnostics across instantiations are de-duplicated

_Domain: `borrow`._

A generic template and each of its monomorphizations report the same borrow error (same level/line/context/message, mono suffix stripped); such identical diagnostics are collapsed so the user sees one error rather than one per instantiation.

**Source:** `src/compiler/borrow_check.cpp#L3854-L3870`

## Lifetime parameters and regions

### `region.lifetime.param-fresh-region` — Each lifetime parameter denotes a distinct region

_Domain: `region`._

Each non-empty declared lifetime parameter ('a, 'b, ...) of a function denotes its own distinct region; references to the same lifetime name within the function resolve to that one region.

**Source:** `src/compiler/region_infer.cpp#L52-L53`

## Lifetime substitution

### `region.lifetime-subst.method-param-pairing` — Lifetime substitution by structural pairing of method formals vs actuals

_Domain: `region`._

Lifetime parameters of a dispatched method are substituted by structurally walking each formal parameter type against its actual argument type (receiver vs param0, then arg[i] vs param[i+1]): for matching Ref/MutRef pairs the formal lifetime maps to the actual lifetime and the walk recurses into pointees; for matching nominal types (Struct/ZonedStruct/Enum) lifetime-args are paired positionally. First binding wins per formal lifetime.

**Source:** `src/compiler/sema_expr.cpp#L7637-L7669`

## Borrow provenance (region side)

### `region.provenance.aggregate-literal-merge` — Aggregate literal provenance is the merge of its element/field initializers

_Domain: `region`._

A struct literal, tuple literal, or enum-data literal has provenance equal to the merge of the provenances of its field values / elements / payloads; returning the aggregate escapes any borrow carried by a borrow-carrying field. Pod/owned fields contribute empty provenance.

**Source:** `src/compiler/borrow_check.cpp#L1821-L1845`

### `region.provenance.bare-value-local-receiver` — Reference result of a method on a bare value-local receiver is local provenance

_Domain: `region`._

If a method's reference/borrow-carrying result has otherwise-empty provenance but its receiver roots at a value-local (e.g. `Rc::deref` on `h` directly), the result provenance is marked is_local.

**Source:** `src/compiler/borrow_check.cpp#L1799-L1806`

### `region.provenance.borrow-carrying-call-aliases-ref-args` — A borrow-carrying function result may alias its reference arguments

_Domain: `region`._

A free-function/constructor call returning a #[borrow_carrying] value has provenance equal to the merge of the provenances of its reference-typed arguments (`WAny::from(&x)` aliases x). A non-borrow-carrying call result is caller-owned (empty provenance). Value (non-ref) arguments contribute no provenance.

**Source:** `src/compiler/borrow_check.cpp#L1808-L1820`

### `region.provenance.control-flow-merge` — Provenance of a value-producing control-flow expression is the merge of its branches

_Domain: `region`._

An if-expr's provenance is the merge of its then and else value provenances; a block-expr's is its result's; a match-expr's is the merge over all arm values. Merge unions param sources and ORs the is_local/is_temp flags.

**Source:** `src/compiler/borrow_check.cpp#L1762-L1774`

### `region.provenance.local-borrow` — Borrow of a local variable is local provenance

_Domain: `region`._

`&x` (AddrOf) where x is a tracked local (not a parameter, not a materialized temp) has provenance is_local=true. Such a borrow is valid in scope but dangles if it escapes (e.g. is returned).

**Source:** `src/compiler/borrow_check.cpp#L1707-L1714`

### `region.provenance.materialized-temp-statement-scoped` — Borrow of a materialized statement-temporary is statement-scoped (is_temp)

_Domain: `region`._

A borrow whose root is a materialized statement-temporary (`__rtmp_N`, the hoisted local for a fresh rvalue receiver in `make().view()` => `(&__rtmp_0).view()`) has provenance is_temp=true: the temporary drops at end of statement, so any reference into it dangles once it escapes the statement.

**Source:** `src/compiler/borrow_check.cpp#L1710-L1711`, `src/compiler/borrow_check.cpp#L1716-L1732`

### `region.provenance.method-result-borrows-receiver` — A reference-returning method result borrows its receiver (lifetime elision)

_Domain: `region`._

For a method call whose result type is a reference (or a #[borrow_carrying] value type), the result's provenance equals the receiver's provenance (output lifetime ties to &self). A non-ref, non-borrow-carrying result has empty provenance (owned).

**Source:** `src/compiler/borrow_check.cpp#L1775-L1789`, `src/compiler/borrow_check.cpp#L1806`

### `region.provenance.param-ref-source` — Reference to a ref-typed parameter carries that parameter as provenance source

_Domain: `region`._

A `VarRef` to a parameter `p` whose type is a reference has provenance source {p}. An `AddrOf p` of a reference parameter likewise yields source {p}. Provenance source identifies which named inputs a returned borrow may point into.

**Source:** `src/compiler/borrow_check.cpp#L1698-L1714`

### `region.provenance.projection-transparent` — Place projections forward the receiver's provenance

_Domain: `region`._

Field read, deref, tuple index, cast, and index read forward the provenance of their operand/receiver unchanged.

**Source:** `src/compiler/borrow_check.cpp#L1752-L1761`

### `region.provenance.ref-aliases-params-or-local` — Reference provenance tracks param/local origin

_Domain: `region`._

Each reference-typed variable tracks the set of function parameters it may alias and whether any path originates from a local; provenance from a global or function return value is treated as safe to return. Provenance merges across branches by union of param-sets and OR of the is_local/is_temp flags.

**Source:** `src/compiler/borrow_check.cpp#L426-L455`

### `region.provenance.ref-self-method-temp-receiver` — A ref-self method on a temporary receiver yields a statement-temp borrow

_Domain: `region`._

When a ref-self method (self by reference, method_self_kind != 0) is called on a temporary receiver, the result points into that temporary, so provenance is_temp=true. A by-value-self adapter (self: Self) instead consumes/moves the temporary into the result and does not produce a statement-temp escape.

**Source:** `src/compiler/borrow_check.cpp#L1789-L1806`

### `region.provenance.temp-lifetime-extension` — Direct &<temporary> bound to a let is lifetime-extended (local, not statement-temp)

_Domain: `region`._

A DIRECT borrow of a literal/struct-literal/call rvalue (`let r = &mut 5;`) is lifetime-extended: the temporary lives as long as the binding, so provenance is is_local (NOT is_temp). It is therefore caught only when returned past the scope, not at the binding site.

**Note:** Distinction between materialized __rtmp temp (statement-scoped) and direct &temp (lifetime-extended) inferred from the two AddrOfTemp branches.

**Source:** `src/compiler/borrow_check.cpp#L1737-L1744`

### `region.provenance.unknown-conservative-accept` — Unresolvable borrow provenance is conservatively accepted

_Domain: `region`._

When a borrow's root cannot be traced to a parameter, local, or temporary, provenance is empty (unknown) and the borrow is conservatively accepted (treated as caller-owned / non-escaping).

**Source:** `src/compiler/borrow_check.cpp#L1750`, `src/compiler/borrow_check.cpp#L1846-L1848`

### `region.provenance.value-local-root-borrow` — Borrow rooted at a value local is local provenance

_Domain: `region`._

A borrow rooted at a by-value local through field/index/deref chains (`&c.x`, `&c.a[i]`, `&*h` where h is a value-local smart pointer) has provenance is_local=true and dangles if returned.

**Source:** `src/compiler/borrow_check.cpp#L1745-L1749`

## Borrow provenance (move-flow side)

### `borrow.prov.binding-never-borrows-itself` — A binding is never recorded as borrowing itself

_Domain: `borrow`._

When recording the local sources a binding borrows from, the binding's own name is removed from its source set; a reborrow such as let r2 = &*r records r (not r2) as the source.

**Source:** `src/compiler/borrow_check.cpp#L983-L1003`

### `borrow.prov.borrow-returning-call-ties-to-ref-inputs` — A borrow-returning call's provenance is its reference inputs

_Domain: `borrow`._

When a method or free function returns a reference (or borrow-carrying type), the result's borrow provenance is the set of reference-typed inputs (receiver and reference arguments) it was derived from — lifetime-elision provenance — so the result cannot escape the scope of any borrowed local it transitively names.

**Source:** `src/compiler/borrow_check.cpp#L1062-L1096`

### `borrow.prov.locals-only-not-params` — Borrow-source tracking covers only local variables, not parameters

_Domain: `borrow`._

Provenance/dangling tracking records as borrow sources only locally declared variables; function parameters are filtered out, since a borrow of a parameter does not dangle within the function body.

**Source:** `src/compiler/borrow_check.cpp#L958-L962`, `src/compiler/borrow_check.cpp#L1032-L1044`

### `borrow.prov.ref-copy-propagates-sources` — Copying a reference binding propagates its borrow sources

_Domain: `borrow`._

Assigning one reference binding from another (o = r) makes o borrow whatever r borrows: the source set is propagated, so an aliased borrow cannot escape its referent's scope via a copy.

**Source:** `src/compiler/borrow_check.cpp#L1097-L1106`

## Lifetime elision

### `region.elision.ambiguous-output` — Output lifetime elision (E0106) ambiguity

_Domain: `region`._

When the return type structurally contains an unannotated reference (`&T`, `&&T`, `(&T,..)`, `[&T;N]`, `&[T]`; references inside generic type-args are excluded), elision requires a source: it is an error (E0106) if there are >=2 input lifetime positions and no `&self`/`&mut self` receiver. With exactly one input lifetime or a `&self` receiver, elision succeeds; the zero-input case is left to explicit annotation.

```logos
fn h(a:&i32, b:&i32) -> &i32  // error E0106
fn f(a:&i32) -> &i32  // ok (rule 2)
```

**Note:** Zero-input elided-ref return (e.g. returning &'static) is not flagged here; deferred to dangling-borrow check.

**Source:** `src/compiler/sema_decl.cpp#L772-L836`

### `region.elision.method-result-borrows-self` — Elided &self -> &T (or borrow-carrying) ties result to receiver

_Domain: `region`._

A method with a reference first parameter, a reference or borrow-carrying return type, and no explicit lifetime parameters has its result lifetime tied to the receiver (self-borrowing). A method with explicit lifetime parameters does NOT count as self-borrowing.

**Divergence (vs. Rust):** Rust lifetime-elision conformant

**Source:** `src/compiler/borrow_check.cpp#L1531-L1567`

### `region.elision.operator-desugar-self-borrow` — Operator-desugared call self-borrowing by name agreement

_Domain: `region`._

For an operator-desugared/trait method call carrying an empty resolved symbol (e.g. `v[i]`, `*p`), the result borrows the receiver iff EVERY method with that unmangled name is self-borrowing; any disagreement or no match yields not-self-borrowing.

**Source:** `src/compiler/borrow_check.cpp#L1550-L1567`

## Lifetime subtyping and variance

### `region.subtype.empty-lifetime-wildcard` — Empty lifetime acts as a wildcard in structural equality

_Domain: `region`._

In lifetime-aware structural equality, an empty (elided) lifetime on either side matches any lifetime (region inference resolves it). Otherwise lifetimes are equal iff syntactically identical, or mutually outliving ('a: 'b and 'b: 'a) which is treated as equality.

**Source:** `include/logos/compiler/subtype.hpp#L65-L72`, `include/logos/compiler/subtype.hpp#L49-L53`

### `region.subtype.mutref-invariant-pointee` — &mut 'a T is covariant in lifetime, invariant in pointee

_Domain: `region`._

For mutable references: &'a mut T <: &'b mut U iff 'a: 'b (covariant lifetime) and T == U with lifetimes (invariant pointee). Hence &mut &'static T is NOT a subtype of &mut &'a T.

**Related:** `region.subtype.ref-covariant`

**Source:** `include/logos/compiler/subtype.hpp#L220-L225`, `include/logos/compiler/subtype.hpp#L9`

### `region.subtype.ref-covariant` — &'a T is covariant in lifetime and pointee

_Domain: `region`._

For shared references: &'a T <: &'b U iff 'a: 'b (covariant lifetime) and T <: U (covariant pointee).

**Related:** `region.subtype.mutref-invariant-pointee`

**Source:** `include/logos/compiler/subtype.hpp#L214-L219`

### `region.subtype.variance-positions` — Per-position variance dictates the lifetime/type relation direction

_Domain: `region`._

At a position with variance v: Bivariant always holds; Covariant requires sub <: sup (lifetimes: 'sub: 'sup); Contravariant requires sup <: sub (lifetimes: 'sup: 'sub); Invariant requires equality (lifetime-aware structural equality for types; normalized-lifetime identity for lifetimes).

**Source:** `include/logos/compiler/subtype.hpp#L154-L183`

## Outlives relation

### `region.outlives.callee-bound-checked-at-call` — Callee `where 'a: 'b` bounds checked at the call site

_Domain: `region`._

When a fn declares outlives bounds `'a: 'b`, each call site must satisfy them under the callee→caller lifetime substitution induced by matching parameter types to argument types. The substitution is derived by walking paired (param_type, arg_type): for matched &/&mut, struct/zoned-struct/enum lifetime_args, tuple/slice/array/ptr elements, callee lifetimes map to the corresponding caller lifetimes. A bound is enforced only when BOTH its lifetimes appear at argument positions and map to concrete distinct caller lifetimes; otherwise (internal or elided) it is left to caller region inference. Unsatisfied → error.

**Related:** `region.outlives.static-always-satisfies`, `region.outlives.struct-lit-bound-checked`

**Source:** `src/compiler/sema_impl.hpp#L3352-L3438`

### `region.outlives.declared-clause-seed` — where-clause `'longer: 'shorter` imposes an outlives constraint

_Domain: `region`._

A declared bound `'longer: 'shorter` imposes the constraint region('longer) ⊇ region('shorter) (longer must contain everything shorter contains). A clause naming a lifetime not declared on the function is ignored at this stage (already a sema error).

**Source:** `src/compiler/region_infer.cpp#L55-L70`

### `region.outlives.implied-from-references` — Implied outlives from nested references

_Domain: `region`._

For each `&'a T` (or `&'a mut T`) appearing in a parameter or return type, every reference lifetime `'b` (and struct/enum/zoned lifetime-arg) nested strictly inside its referent yields an implied bound `'b: 'a` (the inner must outlive the enclosing reference). Implied bounds are emitted only when both lifetimes are declared on the fn (or `'static`); template-internal generic lifetimes are not surfaced.

**Source:** `src/compiler/sema_decl.cpp#L38-L82`

### `region.outlives.lifetime-must-be-declared` — Outlives clauses reference only declared lifetimes

_Domain: `region`._

Every lifetime name appearing in a struct or enum outlives bound (header `<'a: 'b>`, where-clause, or type-param `T: 'a`) must be a declared lifetime parameter of that item, the implicit `'static`, or empty; an undeclared lifetime is a compile error. `'static`/`static` are always known.

**Source:** `src/compiler/sema_decl.cpp#L1241-L1264`, `src/compiler/sema_decl.cpp#L1448-L1471`

### `region.outlives.normalize-apostrophe` — Lifetime names normalized to leading-apostrophe form

_Domain: `region`._

A lifetime region name is identified by its with-apostrophe spelling: the bare form `a` and the prefixed form `'a` denote the same region. Both `'static` and `static` denote the 'static region. Region identity comparisons and graph keys are taken after this normalization.

**Source:** `include/logos/compiler/outlives.hpp#L29-L33`, `include/logos/compiler/outlives.hpp#L35-L37`

### `region.outlives.permissive-elided-source` — Elided longer side treated as compatible at coercion sites; strict at borrow-return

_Domain: `region`._

An empty/elided longer (source) region is treated as satisfying any named outlives constraint at variance/subtype-coercion sites, deferring to call-site region inference (permissive mode). Strict-mode callers (the borrow-check return path) reject an elided source against a named target.

**Note:** Permissive vs strict is a mode flag; default at general sites is permissive.

**Source:** `include/logos/compiler/outlives.hpp#L68`, `include/logos/compiler/outlives.hpp#L86-L92`

### `region.outlives.permissive-unmentioned-pair` — Two unmentioned named regions assumed compatible in permissive mode

_Domain: `region`._

In permissive mode, if two named non-static regions L and S neither equal nor reach one another and NEITHER appears anywhere in the explicit outlives graph, L: S is assumed to hold (region inference is expected to unify them). If either L or S appears in the graph (but no path connects them), L: S is rejected. In strict mode, an unestablished named constraint is always rejected.

**Divergence (vs. Rust):** Permissive default; Rust requires the outlives relation to be explicitly established (would reject).

**Source:** `include/logos/compiler/outlives.hpp#L86-L102`

### `region.outlives.reflexive` — Outlives is reflexive

_Domain: `region`._

> **Multiple sources, reworded:** this id was extracted from more than one source layer with differently-phrased but mutually consistent statements. All variants are reproduced verbatim below so no wording is silently chosen; if you find them in genuine conflict, treat this as a flag to reconcile at the source.

- Variant 1 (`include/logos/compiler/outlives.hpp#L70`): For any region r, r: r holds (r outlives itself).
- Variant 2 (`src/compiler/region_infer.cpp#L110-L111`): For every lifetime 'a, 'a: 'a holds.

**Source:** `include/logos/compiler/outlives.hpp#L70`, `src/compiler/region_infer.cpp#L110-L111`

### `region.outlives.static-always-known` — `'static` is an always-declared lifetime

_Domain: `region`._

The lifetime `'static` (spelled `'static` or `static`) is treated as declared in every scope; it is never an undeclared-lifetime error.

**Source:** `src/compiler/sema_decl.cpp#L33-L37`, `src/compiler/sema_decl.cpp#L115-L119`

### `region.outlives.static-always-satisfies` — `'static` satisfies any outlives bound

_Domain: `region`._

The `'static` lifetime is reserved and always satisfies any outlives obligation; an outlives bound whose longer side is `'static` is never checked.

**Source:** `src/compiler/sema_impl.hpp#L3418-L3419`, `src/compiler/sema_impl.hpp#L3513-L3514`

### `region.outlives.static-is-top` — 'static outlives every region

_Domain: `region`._

'static: r holds for every region r ('static is the longest-living region). Conversely, no region other than 'static satisfies r: 'static unless r is 'static or r reaches 'static through the explicit graph.

**Source:** `include/logos/compiler/outlives.hpp#L69`, `include/logos/compiler/outlives.hpp#L91`

### `region.outlives.static-longest` — 'static outlives every lifetime

_Domain: `region`._

'static outlives every lifetime: 'static: 'a holds for all 'a.

**Source:** `src/compiler/region_infer.cpp#L124-L126`

### `region.outlives.static-requires-declared` — Outliving 'static must be explicitly declared

_Domain: `region`._

A concrete lifetime 'a does not outlive 'static (i.e. 'a: 'static does not hold) unless an explicit bound `'a: 'static` is declared; such a declared edge is honored.

**Source:** `src/compiler/region_infer.cpp#L112-L117`, `src/compiler/region_infer.cpp#L129-L151`

### `region.outlives.struct-lit-bound-checked` — Struct declared outlives bounds checked at struct-literal construction

_Domain: `region`._

At a struct literal `S { .. }` or tuple-struct ctor where `S` declares `where 'a: 'b`, the caller's outlives graph must satisfy each bound under the substitution seeded by (a) explicit lifetime args and (b) walking each field's declared type against its initializer's type. A bound is enforced only when both lifetimes map to concrete distinct caller lifetimes; unsatisfied → error.

**Related:** `region.outlives.callee-bound-checked-at-call`

**Source:** `src/compiler/sema_impl.hpp#L3440-L3529`

### `region.outlives.transitive` — Outlives is transitive (reachability over declared clauses)

_Domain: `region`._

Outlives is the reflexive-transitive closure of the declared outlives clauses: 'a: 'b holds iff 'b is reachable from 'a by following declared `'x: 'y` edges (including through 'static).

**Source:** `src/compiler/region_infer.cpp#L129-L151`

### `region.outlives.transitive-bfs` — Outlives is transitive over the explicit constraint graph

_Domain: `region`._

Given an explicit outlives graph of declared pairs (longer: shorter) parsed from `where 'long: 'short [+ 'mid ...]`, long: short holds if short is reachable from long by following declared outlives edges (transitive closure / BFS over the directed adjacency longer->shorter).

**Source:** `include/logos/compiler/outlives.hpp#L42-L50`, `include/logos/compiler/outlives.hpp#L72-L85`

### `region.outlives.unconstrained-short` — Empty (elided) shorter side is satisfied by any region

_Domain: `region`._

If the shorter side of an outlives query is empty/elided, the constraint r: <empty> is vacuously satisfied for any longer region r.

**Source:** `include/logos/compiler/outlives.hpp#L67`

### `region.outlives.unconstrained-shorter-vacuous` — Outliving an unconstrained lifetime is vacuous

_Domain: `region`._

Any lifetime outlives an unconstrained (empty/anonymous) shorter lifetime; an unconstrained longer lifetime outlives nothing (strict mode).

**Note:** Empty-string lifetime denotes the unconstrained/anonymous case; mapping to surface syntax inferred.

**Source:** `src/compiler/region_infer.cpp#L127-L128`

### `region.outlives.undeclared-lifetime-error` — Outlives/bound lifetimes must be declared

_Domain: `region`._

Every lifetime name used in a function's outlives clause, or in a type-parameter's `T: 'lt` bound, must be a lifetime parameter declared on that function (or `'static`); use of an undeclared lifetime name is ill-formed.

**Source:** `src/compiler/sema_decl.cpp#L113-L134`

### `region.outlives.where-clause-type-param` — Where-clause type-outlives bounds augment a type parameter

_Domain: `region`._

A where-clause entry `T: 'a` (a TYPE_PARAM whose inner items include LIFETIME_PARAMs) attaches each lifetime as an outlives bound on the matching declared type parameter T; entries naming an unknown type parameter are ignored.

**Source:** `src/compiler/sema_decl.cpp#L1211-L1240`, `src/compiler/sema_decl.cpp#L1418-L1447`

### `region.outlives.where-type-outlives` — where-clause type-outlives bounds attach to type params

_Domain: `region`._

A where-clause entry `T: 'lt` adds the lifetime bound `'lt` to the matching type parameter `T`; where-clause lifetime-outlives entries are merged into the function's outlives set.

**Source:** `src/compiler/sema_decl.cpp#L83-L111`

## Lifetime bounds on impls

### `region.bounds.hrtb-outlives-unsat` — HRTB outlives between two bound binders is unsatisfiable

_Domain: `region`._

If an impl declares a where-clause outlives `'a: 'b` between two impl-side lifetime params that BOTH map to distinct bound binders under universal quantification, the constraint is unsatisfiable and the bound is rejected. Reflexive (same skolem) mappings are accepted.

**Source:** `src/compiler/sema_collect.cpp#L1083-L1100`

### `region.bounds.impl-tie-injectivity` — Impl-tied lifetime slots require matching bound binders

_Domain: `region`._

Region matching walks Ref/MutRef pointees and Struct/Enum lifetime+type args recursively. If an impl uses the same lifetime in two trait-arg positions, the bound must use the same binder in those positions (reverse-injective). The forward direction may collapse distinct bound binders onto one impl lifetime (impl strictly more general).

**Source:** `src/compiler/sema_collect.cpp#L1031-L1082`

### `region.bounds.universal-lifetime-position` — Bound lifetime must align with a free impl lifetime param

_Domain: `region`._

When matching a bound's trait-arg lifetimes against an impl's, each non-empty non-'static bound lifetime must align with an impl lifetime that is a free impl-level parameter (not a region concretely pinned at the impl); a 'static (or empty) bound lifetime matches only an exactly-equal impl lifetime or a free impl param.

**Source:** `src/compiler/sema_collect.cpp#L1008-L1046`

## Lifetimes in impls

### `region.impl.outlives-undeclared` — Impl outlives clause may only name declared lifetimes or 'static

_Domain: `region`._

Each lifetime name appearing in an impl-level outlives clause `'a: 'b` must be either a declared impl lifetime parameter or `'static`; otherwise it is an error ("use of undeclared lifetime name '...' in outlives clause").

**Source:** `src/compiler/sema_decl.cpp#L1891-L1905`

### `region.impl.trait-arg-lifetime-erased` — Lifetime arguments at trait-argument position are not tracked for trait dispatch

_Domain: `region`._

Lifetime parameters appearing among impl trait type-arguments are skipped: regions are not tracked structurally for trait selection/dispatch.

**Divergence (vs. Rust):** Logos does not use regions in trait selection; Rust late-bound/early-bound lifetimes participate in coherence.

**Source:** `src/compiler/sema_decl.cpp#L2020-L2022`

## By-ref pattern bindings

### `region.pat.by-ref-binding-inherits-borrows` — By-reference match binding inherits scrutinee borrows

_Domain: `region`._

When a `match`-arm variant-data binding has reference type or borrow-carrying type, it inherits the scrutinee's borrow sources, so the borrow cannot be smuggled past the referent's scope via the binding. By-value bindings copy out and carry no borrow.

**Divergence (vs. Rust):** Rust NLL conformant (DIVERGENCES B6)

**Source:** `src/compiler/borrow_check.cpp#L1506-L1528`

## Dangling references

### `region.dangling.dyn-trait-ref` — &dyn Trait data half is a borrowed reference

_Domain: `region`._

A borrowing trait object (&dyn Trait, non-owning Kind::TraitObject) is treated as a reference kind for dangling-return detection: returning &dyn Trait to a local is rejected; an owning Box<dyn Trait> does not qualify.

**Divergence (vs. Rust):** logos-core 2.1 default trait-object lifetime rule

**Source:** `src/compiler/borrow_check.cpp#L488-L501`

### `region.dangling.no-return-local-ref` — No returning a reference to a local

_Domain: `region`._

A function returning &T or &mut T must not return a reference whose provenance includes a local variable; parameters outlive the call and are safe to borrow from, locals are not.

**Source:** `src/compiler/borrow_check.cpp#L16-L18`, `src/compiler/borrow_check.cpp#L426-L443`

## Stored-borrow lifetime (E0597)

### `region.e0597.store-borrow-into-place` — Storing a borrow into a place records its lifetime sources

_Domain: `region`._

Storing a reference into a place (`x = &y`, `root.f = &y`, `root.0 = &y`, struct-literal field) records the borrow source `y` against the destination's root local; a later use of the root after `y` dies is an E0597 dangling-borrow error. Writes through a deref or into an element (not the root's own storage) do not record a source.

**Source:** `src/compiler/borrow_check.cpp#L2783-L2785`, `src/compiler/borrow_check.cpp#L2824-L2827`, `src/compiler/borrow_check.cpp#L2964-L2981`, `src/compiler/borrow_check.cpp#L2993`

## Temporary scopes

### `region.temp-scope.receiver-temp-end-of-statement` — Droppable auto-ref'd method receiver temporary lives to end of statement

_Domain: `region`._

When a droppable rvalue is auto-referenced as a `&self`/`&mut self` method receiver (`W::mk(..).get()`), the materialized temporary lives until the end of the enclosing statement and is then dropped (Rust temporary-scope semantics), via hoisting into a statement-scoped `let` whose scope-exit runs the destructor; the hoisted `let` is `let mut` when the receiver is `&mut self`.

**Source:** `src/compiler/sema_impl.hpp#L4214-L4240`

## Region CFG

### `region.cfg.let-else-diverging` — let-else else-block diverges

_Domain: `region`._

The else-block of a let-else statement is a diverging branch with no control-flow edge to the code following the statement; bindings introduced by the let are not live in the else-block.

**Source:** `src/compiler/region_infer.cpp#L296-L304`

### `region.cfg.loop-back-edge` — Loop bodies have a back-edge to the loop head

_Domain: `region`._

while/for/for-each loop bodies have control-flow back-edges to the loop head, and `loop` bodies back-edge to themselves; consequently a value live across a loop iteration is live at the loop head, extending borrow regions over the whole loop.

**Source:** `src/compiler/region_infer.cpp#L227-L269`

## Region constraint solving

### `region.solve.constraint-fixpoint` — Region contents are the least fixpoint of Contains/Outlives constraints

_Domain: `region`._

Each region's set of CFG points is the least solution satisfying: Contains(r, P) ⟹ P ∈ r; Outlives(longer, shorter) ⟹ points(shorter) ⊆ points(longer). Lifetimes propagate monotonically from shorter to longer.

**Source:** `src/compiler/region_infer.cpp#L154-L180`
