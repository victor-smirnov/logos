# Ownership

Ownership, move, borrow-check, and region/lifetime rules of Logos (rule domains `borrow` and `region`; 246 rules), extracted from compiler source under `sema/borrow_check`, `sema/move_classify_hpp`, `sema/outlives_hpp`, `sema/region_infer`, `sema/sema_collect`, `sema/sema_decl`, `sema/sema_expr`, `sema/sema_impl_hpp`, `sema/sema_stmt`, `sema/subtype_hpp` plus codegen drop/capture layers (`codegen/mlir_gen*`); rule ids are permanent addresses — several rules restate one mechanism from parallel extraction chunks and are cross-flagged *Overlaps* rather than merged.

## Move/Copy classification (`classify`)

### `borrow.classify.move-vs-copy` — Move vs Copy classification

A value is a Move type (consuming it invalidates its source, and it cannot be moved while borrowed) iff it owns droppable resources and is not Copy; otherwise it is Copy. Structs/enums with a Drop impl are droppable; primitives, raw pointers, &T, and Copy-impl types are Copy. Classification recurses structurally over tuples, arrays, struct fields, and enum payloads, so an aggregate carrying any Move element (e.g. (String, i64)) is itself Move.

Source: `src/compiler/borrow_check.cpp#L266-L321`, `src/compiler/borrow_check.cpp#L237-L264`, `src/compiler/borrow_check.cpp#L102-L104`

### `borrow.classify.mut-ref-is-move` — &mut T is move, &T is copy

&mut T is a Move type: passing or binding a &mut reference moves the unique mutable borrow. &T is Copy.

Source: `src/compiler/borrow_check.cpp#L277-L283`

### `borrow.classify.type-param-move-unless-copy` — Bare type parameter moves unless Copy-bounded

Inside a generic body a bare type-parameter T is a Move type unless it carries an explicit `T: Copy` bound; partial moves of fields typed T are tracked accordingly.

> **Divergence** — B1

*Overlaps:* `borrow.move.typevar-move-unless-copy`, `borrow.generic.copy-bound-is-copy-type`

Source: `src/compiler/borrow_check.cpp#L284-L297`

### `borrow.classify.drop-implies-droppable` — Drop impl or droppable field makes a type need-drop

A struct needs drop iff it has a Drop impl or has any field that (transitively) needs drop; generic instantiations are matched by their concrete (mono-mangled) name so droppable fields of generic containers are not missed.

Source: `src/compiler/borrow_check.cpp#L237-L264`

## Move semantics (`move`)

### `borrow.move.consume-once` — Affine ownership: move consumes once

A Move variable is consumed on its first use in value position; any subsequent use (whole-value or of an already-moved field) is an error (use-after-move).

Source: `src/compiler/borrow_check.cpp#L5-L8`, `src/compiler/borrow_check.cpp#L326-L342`

### `borrow.move.path-overlap` — Move-path overlap semantics

Two dotted paths a,b overlap iff a==b or one is a dot-prefixed ancestor of the other (e.g. "i" overlaps "i.s"); disjoint siblings ("i.t","i.s") do not overlap. Reading/moving a moved leaf, a path under a moved subtree, or a parent containing a moved leaf are all treated as uses of (partially) moved data.

Source: `src/compiler/borrow_check.cpp#L1350-L1360`

### `borrow.move.reinit-clears-deeper` — Re-initialization clears equal and deeper move records

Assigning to path p clears moved-tracking entries equal to p or strictly deeper (dot-prefixed by p) — e.g. writing `o.i` refills `o.i.s`. A moved entry SHALLOWER than p is unaffected: assigning `o.i.s` does not un-move a moved `o.i`.

Source: `src/compiler/borrow_check.cpp#L1369-L1381`

### `borrow.move.partial-move-blocks-use` — Use of a partially- or fully-moved value is rejected

consume(name): rejected ('use of partially moved value (field F moved on line L)') if name.moved_fields is non-empty. Else rejected ('use of moved value', citing the prior move line if known) if name.moved.

Source: `src/compiler/borrow_check.cpp#L1383-L1401`

### `borrow.move.borrowed-cannot-move` — Cannot move a borrowed value; binding mut-ness survives the move

consume(name): rejected ('cannot move while it is borrowed') if name.mut_borrowed, name.shared_borrows>0, or name.mut_reservations>0. Rejected (rustc E0505) if any field of name has a live field borrow (field_borrow_conflicts, need_exclusive=true, empty path) — a live borrow of ANY field blocks moving the whole value, since the move would invalidate the field reference. On success, name's tracked state resets to fresh (clearing borrow/move bookkeeping) but is_mut_binding is preserved across the reset, and name is marked moved at the current line — reassigning a moved-from `let mut` binding still admits `&mut` of it afterward.

Source: `src/compiler/borrow_check.cpp#L1402-L1424`

### `borrow.move.var-consume` — Reading a variable in a consuming position moves a non-Copy value

A bare variable reference `x` evaluated in a consuming position moves `x` when its type is a move (non-Copy) type; the binding is thereafter dead and any later use is an error. In a non-consuming position the variable is only checked-live, not consumed.

Source: `src/compiler/borrow_check.cpp#L3294-L3314`

### `borrow.move.partial-field-path` — Partial move tracks full dotted field paths

Moving a place `root.a.b...` of move type marks that exact path on `root` as moved. A subsequent use is an error if it overlaps the moved path: reading the same path, anything inside it, or any containing parent (including the whole value `root`). Disjoint sibling paths (e.g. `root.a.t` vs moved `root.a.s`) stay usable. A strict-parent read (`root.a` while `root.a.s` moved) errors only for a genuine whole-value read, not when it is merely an intermediate projection toward a disjoint deeper leaf (place-base position).

Source: `src/compiler/borrow_check.cpp#L3442-L3520`

### `borrow.move.no-flow-through-raw-ptr` — Ownership does not flow through a raw pointer

A move out of `(*p).field` where any hop in the field chain (including the root) is `*const T`/`*mut T`-typed is NOT a partial move of any tracked owned root; raw-pointer-rooted projections are excluded from move tracking. References (`&`/`&mut`) keep ordinary tracking.

Source: `src/compiler/borrow_check.cpp#L3457-L3481`

### `borrow.move.value-invalidates-source` — Move type definition

A value is a move type iff consuming it invalidates the source, i.e. it owns a non-Copy value. Consuming a non-move (Copy) value leaves the source usable; consuming a move value renders the source inaccessible.

Source: `include/logos/compiler/move_classify.hpp#L22-L23`, `include/logos/compiler/move_classify.hpp#L30-L32`

### `borrow.move.aggregate-recursion` — Tuple/array move-ness is element-wise

A tuple is a move type iff at least one of its element types is a move type; an array [T; N] is a move type iff its element type T is a move type. Move-ness of aggregates is the structural OR over (existing) element types.

Source: `include/logos/compiler/move_classify.hpp#L34-L40`

### `borrow.move.struct-non-copy` — Struct move-ness

A struct value is a move type iff it is not Copy (a struct that does not satisfy Copy, and whose drop semantics require ownership transfer, is moved on consumption).

Source: `include/logos/compiler/move_classify.hpp#L17-L18`, `include/logos/compiler/move_classify.hpp#L42`

### `borrow.move.enum-drop-or-move-payload` — Enum move-ness

An enum value is a move type iff it is not Copy and either it has a user Drop impl or at least one variant payload is itself a move type.

Source: `include/logos/compiler/move_classify.hpp#L18-L19`, `include/logos/compiler/move_classify.hpp#L41`

### `borrow.move.box-dyn-owning` — Owning `Box<dyn>` is a move type

An owning `Box<dyn Trait>` is always a move type (it owns a heap value whose destructor must run on consumption).

Source: `include/logos/compiler/move_classify.hpp#L14-L15`

### `borrow.move.scalar-not-move` — Scalar and reference types are not move types

Types other than tuple, array, struct, and enum (e.g. scalars, references, raw pointers) are not move types by default and are consumed by copy.

Source: `include/logos/compiler/move_classify.hpp#L43`

### `borrow.move.typevar-move-unless-copy` — Generic type parameter is move unless T: Copy

A bare generic type-parameter (TypeVar) value is classified as a move type (consumed on use, source invalidated) unless its parameter carries an explicit `Copy` bound; absent a Copy bound the parameter is conservatively assumed to own a non-Copy value, matching Rust generic-body move-by-default semantics.

*Overlaps:* `borrow.classify.type-param-move-unless-copy`

Source: `include/logos/compiler/move_classify.hpp#L13-L16`, `src/compiler/borrow_check.cpp#L724-L726`

### `borrow.move.by-value-args` — By-value call arguments are moved

After lowering a call's argument list, each argument expression that denotes a move-type l-value, or an owning `Box<dyn Trait>` binding consumed by a by-value parameter, is marked moved (scope-end auto-drop no longer fires on it). Arguments of other kinds are unaffected (no-op).

*Overlaps:* `borrow.invoke.by-value-arg-moved`, `expr.call.args-move-tracking`

*See also:* `borrow.move.by-value-receiver`

Source: `src/compiler/sema_expr.cpp#L6466-L6474`

### `borrow.move.by-value-receiver` — By-value method receiver is moved

When the resolved method's formal `self` type is not `Ref`, `MutRef`, or `Ptr` (i.e. `self` by value), the call consumes ownership of the receiver: the receiver expression is marked moved so the caller's binding is not auto-dropped at scope end (preventing double-Drop when the method internally transfers ownership, e.g. `Vec::into_iter` moving `self.ptr` into the returned iterator).

*See also:* `borrow.move.by-value-args`

Source: `src/compiler/sema_expr.cpp#L6483-L6491`

### `borrow.move.tuple-element-moved` — Concrete move-type tuple elements are moved into the tuple

A concrete (non-TypeVar) move-type value placed into a tuple element is moved into the tuple (its source binding is marked consumed, so it is not independently dropped at its own scope exit); TypeVar elements are exempt (their drop is routed through the mono mechanism, since a generic may resolve Copy).

Source: `src/compiler/sema_expr.cpp#L1638-L1646`

### `borrow.move.no-move-out-of-borrowed-place` — Cannot move a move-typed value out of a borrowed place (E0507)

Moving a move-typed value by value out of a non-owning place is rejected (E0507): deref of a `&`/`&mut` reference variable (`*r`); index `v[i]`/slice-index `s[i]` of a non-raw container (including user `Index`, lowered to `*v.index(i)`); deref of a user `Deref` (`*x.deref()`) including `Box` (`*b`, since DerefMove is unimplemented); and reading a move-typed field out of a `&`/`&mut` receiver (`r.field`). Exempt: any place whose access chain passes through a raw-pointer (`*const`/`*mut`) hop, and partial moves out of owned receivers.

> **Divergence** — Box move-out (`let s = *b`) is rejected because Logos does not implement Rust's built-in Box DerefMove; in Rust it is allowed.

Source: `src/compiler/sema_impl.hpp#L876-L968`

### `borrow.move.shadowing-new-slot` — Each binding gets a fresh dense slot; shadowing rebinds

Every binding registration (let, parameter, pattern, for, closure binding) is assigned the next dense per-function variable slot; shadowing a name yields a NEW slot, so slots uniquely identify bindings independent of the name string.

*Overlaps:* `borrow.scope.shadowing-fresh-slot`

Source: `src/compiler/sema_impl.hpp#L1868-L1874`

### `borrow.move.scope-exit-clears-moved` — Popping a scope clears its variables' moved state

On scope exit, all variables declared in that scope are removed from the moved set (their names go out of scope, so their move state no longer constrains outer code).

Source: `src/compiler/sema_impl.hpp#L2045-L2052`

### `borrow.move.closure-return-drop-boundary` — A closure body's frame is a drop boundary for `return`

A closure body's own frame(s) are marked a drop boundary: a `return` inside the closure drops only frames belonging to the closure itself and never the enclosing function's frames — which remain on the scope stack (unpopped) so the closure body can still resolve captured names for type lookup.

Source: `src/compiler/sema_impl.hpp#L1853-L1858`, `src/compiler/sema_impl.hpp#L2044`

### `borrow.move.loop-exit-drop-boundary` — `break`/`continue` drops every frame down to the loop-body frame

The scope frame that IS a loop's body is tagged a loop boundary; a `break`/`continue` nested inside the body (even through an inner `if`) exits via the loop edge, bypassing the body block's normal end-of-scope drop path, and must instead drop every frame from the statement's own frame down to AND INCLUDING the loop-body frame.

Source: `src/compiler/sema_impl.hpp#L1859-L1865`, `src/compiler/sema_impl.hpp#L1875-L1878`

### `borrow.move.conditional-move-epilogue-no-double-drop` — Conditional move on one branch suppresses epilogue drop on merge

A parameter/local moved on at least one branch of a diverging conditional (e.g. `if b { return f(x); }`) is recorded as EVER-moved for the whole function body (monotonic, not reverted by per-branch save/restore); function-epilogue drops consult the ever-moved set — not just the post-merge moved set — so a drop is not re-emitted on the branch-merged control-flow block for a variable already moved on some branch, avoiding a double-free when branches are lowered into one merged block.

*Extraction note:* Source comment flags the proper fix as full drop-flag elaboration extended to params, 'tracked' as still-pending — the ever-moved fallback may over-suppress a drop on a merged path where the variable was NOT actually moved.

Source: `src/compiler/sema_impl.hpp#L2063-L2075`, `src/compiler/sema_impl.hpp#L1881-L1886`

### `borrow.move.move-closure-capture-drop-once` — Move-closure captures drop once, in capture order, with the closure

A variable captured BY MOVE into a `move` closure is marked moved (use-after-move enforced at the source), but the closure environment stores only a pointer to the source's storage (no per-capture drop-glue); the source is additionally recorded closure-owned so its destructor still runs exactly once, timed to when the owning closure binding itself drops (in capture order) rather than at the source's own original slot — unless the closure is created in a different frame than the captured source (e.g. a conditional inner block), in which case the capture falls back to its own-slot drop.

Source: `src/compiler/sema_impl.hpp#L1907-L1922`

### `borrow.move.write-into-cell-is-move` — Writing a move-type value into a memory cell moves it

Writing a move-type RHS into a memory cell (deref, indexed, field) is a move of the source: the source's scope-exit auto-drop is suppressed and Drop responsibility transfers to the destination.

Source: `src/compiler/sema_impl.hpp#L2077-L2085`

### `borrow.move.lvalue-path-tracking` — Move of a place tracks the full l-value path

Moving a move-type place marks a precise l-value path: a bare variable marks the variable; a field chain `outer.a.b` rooted at a variable marks `outer.a.b`; a tuple element `t.N` marks `<name>.<index>`. The marked sub-path is then excluded from the aggregate's scope-end drop so that element is not dropped twice.

Source: `src/compiler/sema_impl.hpp#L2177-L2216`

### `borrow.move.no-move-out-of-array-index` — Cannot move a Drop-bearing element out of a fixed-size array by index

Moving by value out of a fixed-size array element via index (`let s = arr[i]`) is rejected when the element type is concrete and needs Drop, because a single array slot cannot be marked moved (the array would still drop it → double free). Generic element types (TypeVar/AssocType/ImplTrait) are exempted, as are borrows/autoref which do not move.

Source: `src/compiler/sema_impl.hpp#L2217-L2257`

### `borrow.move.non-movable-by-value-rejected` — Cannot move a location-anchored value by value

A by-value move of a place whose type is non-movable (location-anchored) is an error; the value must instead be used in place through `&`, `&mut`, or `*mut`.

Source: `src/compiler/sema_impl.hpp#L2159-L2176`

### `borrow.move.recursive-into-producer` — Move propagates into composite producer subexpressions

When a value is produced from a composite expression (Call arg, StructLit field, TupleLit elem, EnumLitData payload, BlockExpr result), any move-typed VarRef carried into the produced value is marked moved and removed from the drop set; FieldRead in producer position is a move of the source. This prevents double-drop / use-after-move for values escaping via construction.

Source: `src/compiler/sema_impl.hpp#L2274-L2314`

### `borrow.move.owning-dyn-by-value` — Passing an owning `Box<dyn Trait>` by value moves it

An owning `Box<dyn Trait>` binding (collapsed to a bare trait object) is moved when passed by value — the callee frees the handle — so the caller marks it moved to avoid a double-free.

Source: `src/compiler/sema_impl.hpp#L2397-L2406`

## Value uses (`use`)

### `borrow.use.place-base-suppresses-value-use` — Visiting a projection's receiver base is place-forming, not a value-use

Visiting the receiver/base sub-expression of a place projection (x.f, x[i], recv.method(), t.N) suppresses field-borrow VALUE-use conflict checks for the duration (in_addr_source_=true): the precise place was already conflict-checked at the projection site itself. Liveness and move-state checks (check_live, moved_fields) are NOT suppressed and still run on the base chain.

Source: `src/compiler/borrow_check.cpp#L1426-L1439`

### `borrow.use.moved-or-mut-borrowed` — Use of moved or mutably-borrowed value

check_live(name): rejected ('use of moved value', citing the prior move line if known) if name.moved; rejected ('cannot use while it is mutably borrowed') if name.mut_borrowed.

Source: `src/compiler/borrow_check.cpp#L1450-L1463`

## Consuming positions (`eval`)

### `borrow.eval.consuming-positions` — Operand consumption is determined by expression position

Operands are evaluated consuming in value positions and non-consuming in place/borrow positions: BinOp/Unary operands, call arguments, struct/array/tuple/enum-payload literal elements, and index expressions are consuming; Deref, AddrOf source, place-base receivers (FieldRead/IndexRead/TupleIndex receivers), slice/closure-callee operands are non-consuming; Cast and Try and Block-result propagate the surrounding consuming flag.

Source: `src/compiler/borrow_check.cpp#L3436-L3439`, `src/compiler/borrow_check.cpp#L3526-L3536`, `src/compiler/borrow_check.cpp#L3630-L3672`, `src/compiler/borrow_check.cpp#L3727-L3772`

## Variable reads: use-after-move, definite assignment (`var-ref`)

### `borrow.var-ref.use-after-move` — Use of a moved variable is an error

Reading a variable after its value has been moved out is a compile error: 'use of moved variable'.

*Overlaps:* `borrow.use.moved-or-mut-borrowed`

Source: `src/compiler/sema_expr.cpp#L586-L587`

### `borrow.var-ref.definite-assignment` — Use of a possibly-uninitialised binding is an error

Reading a binding declared without an initializer (`let x: T;`) before a definite assignment on the current path is a compile error (Rust E0381); at if/match merge points a binding is uninitialised if uninitialised on any incoming path.

Source: `src/compiler/sema_expr.cpp#L588-L594`

## `let` bindings (`let`)

### `borrow.let.ref-from-temp-dangles` — A let-bound reference borrowing into a per-statement temporary is rejected (E0716)

Binding a reference (or borrow-carrying value) whose provenance is a temporary value dropped at the end of the binding statement is an error: the reference would outlive its own statement, but the temporary it borrows into is dropped when the statement ends. The owning value must first be bound to a variable so it outlives the borrow.

```logos
let v = make().view();  // error
let h = make(); let v = h.view();  // ok
```

> **Divergence** — Rust E0716 analog (temporary value dropped while borrowed)

Source: `src/compiler/borrow_check.cpp#L2709-L2721`

### `borrow.let.borrow-carrying-routes-loan` — let RHS that is a reference / closure / borrow-carrying value / aggregate literal records a loan held by the binding

When a `let name = val;` has a value that is a reference kind, a closure type, a borrow-carrying-typed value (e.g. `v.iter_mut()`), or an aggregate literal (struct/tuple/array) that may itself hold nested borrows, the borrows reachable from `val` are recorded as loans held by `name` (released at `name`'s last use, NLL). Otherwise the value is visited as an ordinary consuming move (preserving move-tracking, e.g. `let h2 = h;`).

*See also:* `borrow.aggregate.literal-held-borrow`, `borrow.closure.capture-by-ref-loan`

Source: `src/compiler/borrow_check.cpp#L2673-L2706`

### `borrow.let.provenance-propagation` — Provenance flows from RHS to a let binding of reference, borrow-carrying, or lifetime-parameterized struct type

A `let` binding whose declared type is a reference or borrow-carrying type inherits the RHS's reference-provenance (`prov_of(val)`) into `prov_[name]`. A binding of a `struct`/`ZonedStruct` type that carries non-empty lifetime arguments also inherits the RHS provenance (the struct instance borrows through its lifetime parameter), feeding downstream dropck/outlives checks.

Source: `src/compiler/borrow_check.cpp#L2709-L2726`

### `borrow.let.no-move-behind-ref` — Cannot move a move-typed value out of a reference deref (E0507)

`let s = *r` that would move a move-typed value out of a `&`/`&mut` deref of a reference variable is rejected (E0507); copy-typed values copy out fine.

*Overlaps:* `borrow.move.no-move-out-of-borrowed-place`

Source: `src/compiler/sema_stmt.cpp#L1932-L1941`

### `borrow.let.move-rhs-variable` — let of a move-typed place marks the source moved

If the RHS of a let is a place (variable reference or struct-field-read chain) of a move type, the source is marked moved (recording dotted paths so per-field auto-drop on the source struct is suppressed).

Source: `src/compiler/sema_stmt.cpp#L2242-L2247`

### `borrow.let.box-dyn-owning-drop` — Owning `Box<dyn Trait>` binding drops only when ownership is transferred

An `let x: Box<dyn Trait>` binding (which collapses to a bare owning TraitObject) is marked for drop (drop_in_place + free) only when the RHS genuinely transfers ownership — a `box_new(..) as Box<dyn>` cast or a value-returning constructor call. Reads of a handle copy out of a container (deref / index / method-call) are excluded to avoid double-free.

Source: `src/compiler/sema_stmt.cpp#L1706-L1712`, `src/compiler/sema_stmt.cpp#L2223-L2240`

### `region.let.temporary-lifetime-extension` — let-init borrow of an rvalue extends the temporary

`let p = &<rvalue>` / `let p = &mut <rvalue>` (rvalue = scalar literal or value-producing call/struct/tuple literal) extends the temporary's lifetime to the enclosing scope: a hidden named temporary holds the value and is dropped at scope end, and the binding is rewritten to borrow that named temporary. A void/Never/error-typed rvalue keeps the degenerate inline spill (nothing to drop).

```logos
let p = &String::from("x");
```

Source: `src/compiler/sema_stmt.cpp#L1773-L1895`

## Assignment (`assign`)

### `borrow.assign.borrowed-lhs` — Assignment to a borrowed variable is rejected

Assigning to a variable `x` (`x = v`) is an error while `x` has any active borrow: a shared borrow ⇒ "cannot assign to 'x' because it is borrowed"; a mutable borrow ⇒ "cannot assign to 'x' while it is mutably borrowed".

```logos
let r = &x; x = 1;  // error: x is borrowed
```

Source: `src/compiler/borrow_check.cpp#L2756-L2763`

### `borrow.assign.reinit-reowns` — Assignment re-owns the destination

An assignment `x = v` clears any prior move/borrow state of `x` (re-owns it) while preserving the `is_mut_binding` (declared-`mut`) property across the reset; after the assignment `x` is a fully-initialized, unmoved binding. The RHS is consumed (moved) unless it is a reference value or an aggregate literal, in which case its constituent borrows are tracked (`take_ref_borrows`) instead of being moved.

Source: `src/compiler/borrow_check.cpp#L2764-L2786`

### `borrow.assign.immutable-var` — Write to immutable binding rejected

A write whose place is rooted at a non-`mut` local variable is rejected ('assignment to immutable variable').

Source: `src/compiler/sema_stmt.cpp#L7019-L7023`

### `borrow.assign.shared-ref` — Write through shared reference rejected

A write through a place rooted at a shared reference `&T` (or a shared `&DstStruct`) is rejected; a write through `&mut T` (or a `&mut DstStruct`) is permitted.

Source: `src/compiler/sema_stmt.cpp#L6982-L6993`, `src/compiler/sema_stmt.cpp#L7035-L7038`

### `borrow.assign.raw-ptr-unsafe` — Write through raw pointer requires *mut and unsafe

A write through a place rooted at a raw pointer `*const T` is rejected; through `*mut T` it is permitted only inside an `unsafe` context.

Source: `src/compiler/sema_stmt.cpp#L6994-L7004`, `src/compiler/sema_stmt.cpp#L7025-L7046`

### `borrow.assign.static-mut-unsafe` — static mut write requires unsafe; immutable static not writable

A write to a place rooted at a `static mut` is permitted (storage is mutable) but requires an `unsafe` block; a write to a plain immutable `static` is rejected.

> **Divergence** — Rust-conformant (items.static.mut.safety)

Source: `src/compiler/sema_stmt.cpp#L7005-L7018`

### `borrow.assign.shared-slice-elem` — Element write through shared slice rejected

For a field/index/tuple-index place whose receiver resolves to `Slice` type, writing is permitted only through a `&mut [T]` slice; a shared `&[T]` receiver is rejected ('cannot write through a shared `&[T]` slice'). The same receiver-kind switch also governs a receiver resolving to `MutRef` (writable) or `Ptr` (needs `*mut` + unsafe).

Source: `src/compiler/sema_stmt.cpp#L7055-L7077`

## Field writes (`fieldwrite`)

### `borrow.fieldwrite.reinit-field` — Field write reinitializes the moved-out field and its subpaths

Writing to a field place — surface `r.f = v`, or the lowered `SDerefWrite(AddrOfTemp(FieldRead-chain), v)` form for a nested `o.i.s = v` — reinitializes that field, clearing the partially-moved state of the field and every path beneath it from the receiver's `moved_fields` set before the write's own liveness check runs; the receiver is no longer treated as partially-moved on account of that field. The value is consumed.

```logos
let _ = s.v; s.v = w;  // ok: s.v rebound
```

Source: `src/compiler/borrow_check.cpp#L2823-L2839`, `src/compiler/borrow_check.cpp#L2951-L2981`

## Index writes (`indexwrite`)

### `borrow.indexwrite.borrowed-container` — Element write through a borrowed container is rejected

An element write `arr[i] = v`, `recv.field[i] = v`, or a lowered `*AddrOfTemp(<root>[i]…)`/`*AddrOfTemp(<root>.f[i]…)` place (walked through FieldRead/IndexRead/SliceIndex/TupleIndex down to a root `VarRef`) is an error while the root container has any active borrow: shared ⇒ "cannot assign to '`<place>`' because '`<root>`' is borrowed"; mutable ⇒ "...while '`<root>`' is mutably borrowed". The index and value are consumed.

```logos
let r = &arr[0]; arr[1] = v;  // error: arr is borrowed
```

Source: `src/compiler/borrow_check.cpp#L2855-L2872`, `src/compiler/borrow_check.cpp#L2877-L2894`, `src/compiler/borrow_check.cpp#L2922-L2950`

## Struct literals (`struct-lit`)

### `borrow.struct-lit.field-value-move` — move-typed field value is consumed

A struct-literal field-init value whose type is a move-type is marked moved (mark_moved_expr) once placed into the literal, in both the generic and non-generic construction paths, so the source binding cannot be used or dropped again by the surrounding scope.

Source: `src/compiler/sema_expr.cpp#L10363-L10366`, `src/compiler/sema_expr.cpp#L10557-L10559`

### `region.struct-lit.outlives-check` — struct-literal checks declared `where 'a: 'b` outlives constraints

At the point a struct literal is constructed, its bound lifetime-args (from the type hint, or default) are checked against the struct's declared lifetime-outlives constraints (check_struct_lit_outlives), using the literal's field-init expressions as the evidence set.

Source: `src/compiler/sema_expr.cpp#L10369-L10374`, `src/compiler/sema_expr.cpp#L10565-L10571`

## Enum literals (`enum-lit`)

### `borrow.enum-lit.payload-move` — Payload arguments consume move-typed sources

Each move-typed payload argument consumes (moves out of) its source expression at the enum-literal construction site.

*Overlaps:* `borrow.enum-lit.payload-consumes-source`

Source: `src/compiler/sema_expr.cpp#L12629-L12636`

### `borrow.enum-lit.payload-consumes-source` — Enum payload arguments are moved

Each payload argument of move type is marked moved at construction; constructing an enum literal consumes its move-type payload sources, preventing later use.

*Overlaps:* `borrow.enum-lit.payload-move`

Source: `src/compiler/sema_expr.cpp#L12268-L12276`

### `expr.enum-lit.lifetime-inference` — Lifetime-arg inference from payload args

Lifetime arguments of the constructed enum are inferred by structurally co-walking each variant payload formal type against the corresponding argument's type, binding each formal lifetime to the first argument lifetime seen across reference, tuple, struct, zoned-struct and enum positions.

*Overlaps:* `region.enum-lit.lifetime-arg-inference`

Source: `src/compiler/sema_expr.cpp#L12353-L12383`, `src/compiler/sema_expr.cpp#L12508-L12522`

### `region.enum-lit.lifetime-arg-inference` — Enum lifetime args inferred from payload reference lifetimes

An enum's lifetime parameters are inferred by walking each (declared payload type, actual argument type) pair, mapping reference/struct/enum/tuple lifetimes back to the enum's lifetime parameters; unresolved lifetime parameters yield empty lifetime args on the constructed type.

*Overlaps:* `expr.enum-lit.lifetime-inference`

Source: `src/compiler/sema_expr.cpp#L12018-L12053`, `src/compiler/sema_expr.cpp#L12146-L12165`

## Aggregate literals (`aggregate`)

### `borrow.aggregate.literal-held-borrow` — Borrows inside an aggregate literal are held by the binding the aggregate flows into

A `&`/`&mut` borrow placed into a struct/tuple/array LITERAL field or element is registered with the SAME holder that the aggregate value flows into (recursive borrow-taking), held for that holder's lifetime and released at the holder's last use (NLL), rather than only through the end of the constructing statement. Non-borrow fields/elements are consumed (move-tracked) as ordinary values. A `let` binding whose RHS is such an aggregate literal is routed the same way even when the binding's nominal type is not itself borrow-carrying.

```logos
let g = Guard { r: &mut f };
let t = (&a, &b);
let arr = [&x];
```

Source: `src/compiler/borrow_check.cpp#L2286-L2303`, `src/compiler/borrow_check.cpp#L2689-L2702`

## Calls (`call`)

### `expr.call.args-move-tracking` — By-value move-type arguments are marked moved

Passing a by-value argument of a move type (including an owning `Box<dyn>`) marks the source binding as moved.

*Overlaps:* `borrow.move.by-value-args`, `borrow.invoke.by-value-arg-moved`

Source: `src/compiler/sema_expr.cpp#L3330`, `src/compiler/sema_expr.cpp#L3583-L3584`

### `expr.call.outlives-cross-check` — Caller cross-checks callee where 'a: 'b bounds

At a non-generic concrete call, the callee's declared lifetime-outlives constraints (`where 'a: 'b`) are checked against the actual arguments.

Source: `src/compiler/sema_expr.cpp#L3326-L3328`

## Call arguments (`callargs`)

### `borrow.callargs.scope-ref-borrows` — Call-site reference arguments create scoped borrows released after the call

Each call's arguments are evaluated inside a fresh call-site borrow scope: a reference-typed argument takes its referenced borrows for the call, a non-reference argument is consumed, and all such call-site borrows are released when the scope pops after the call.

Source: `src/compiler/borrow_check.cpp#L3281-L3290`, `src/compiler/borrow_check.cpp#L3609-L3627`

## Invocation moves (`invoke`)

### `borrow.invoke.by-value-arg-moved` — By-value move-type call argument is marked moved

A by-value argument of move (non-Copy) type passed to a closure/fn-ptr call is marked moved so its owning scope does not also drop it (preventing double-free). Arguments are NOT marked moved when the parameter is a reference (`&T`/`&mut T`) or when the argument's type is an un-substituted TypeVar (move-ness unknown in a generic body).

*Overlaps:* `borrow.move.by-value-args`, `expr.call.args-move-tracking`

Source: `src/compiler/sema_expr.cpp#L6246-L6288`

## Method arguments (`method`)

### `borrow.method.args-moved` — Method arguments tracked as moved

Passing arguments to a method call marks those argument values as moved for borrow/move analysis.

*Overlaps:* `borrow.method-call.args-moved`

Source: `src/compiler/sema_expr.cpp#L9038`, `src/compiler/sema_expr.cpp#L9132`

### `region.method.lifetime-subst` — Return-type lifetime substitution from call site

A lifetime substitution is built by structurally walking each method formal param against its actual (receiver paired with param0, args with the rest), binding each method lifetime to the corresponding caller lifetime; this substitution is applied (with type-arg substitution) to the method return type so e.g. `fn get<'a>(&'a self)->Item<'a>` called with `&'b` yields `Item<'b>`.

*Overlaps:* `region.lifetime-subst.method-param-pairing`

Source: `src/compiler/sema_expr.cpp#L9054-L9105`

## Method-call arguments (`method-call`)

### `borrow.method-call.args-moved` — Method-call arguments are tracked as moved at the call site

Once a method call is fully resolved (either the `finish_generic_call` route or the plain `EMethodCall` route), its explicit arguments are recorded as moved via `track_args_moved` before the call node is built, so a subsequent use of a moved-by-value argument is caught by move-checking.

*Overlaps:* `borrow.method.args-moved`

Source: `src/compiler/sema_expr.cpp#L9085`, `src/compiler/sema_expr.cpp#L9179`

### `region.method-call.lifetime-subst` — Method-call lifetime substitution propagates caller lifetimes into the return type

A lifetime substitution is built by structurally walking the method's formal param types (self, then each arg) against the actual receiver/arg types: matching `Ref`/`MutRef` pairs record a `method-lifetime → caller-lifetime` binding (first writer wins) and recurse into pointees; matching `Struct`/`ZonedStruct`/`Enum` pairs record their `lifetime_args` positionally and recurse into `type_args`; matching `Tuple` pairs recurse into elements. This substitution (plus the type substitution) is applied to `fi.ret_type`, so e.g. `fn get<'a>(self: &'a T) -> Self::Item<'a>` called with `t: &'b T` yields return type `Self::Item<'b>`, not a literal `'a` that would collide with the caller's own `'a`.

*Overlaps:* `region.lifetime-subst.method-param-pairing`

Source: `src/compiler/sema_expr.cpp#L9101-L9152`

## Method receivers (`recv`)

### `borrow.recv.bare-place-self-conflict` — Bare-place method receiver self-borrow conflict

method_self_kind(call) resolves the call's receiver self-kind (0=by-value,1=&self,2=&mut self) via resolved_symbol, falling back to the base method name (all same-named overloads must agree on param-0 kind, else ambiguous->0). check_recv_conflict(bp,is_mut): for a bare whole-variable receiver (bp.path empty) whose root is not a raw-pointer type, rejected if the root is already mut_borrowed; a mut call is also rejected if the root has shared_borrows>0 or any tracked field borrow. Raw-pointer roots are unchecked; reference-typed roots ARE checked, so a &mut-self call through a &mut-typed variable still conflicts with a live borrow of that variable.

Source: `src/compiler/borrow_check.cpp#L1577-L1632`

## Auto-borrowed receivers (`autoborrow`)

### `borrow.autoborrow.method-receiver-transient` — Method-call receiver borrow is scoped to the call

A `&self`/`&mut self` method borrows its receiver for the duration of the call only; the implicit receiver borrow is released at the enclosing scope-pop (NLL), so consecutive calls `b.foo(); b.bar();` do not conflict. A bare-place receiver (VarRef/FieldRead, not an explicit AddrOfTemp) still incurs the whole-root conflict check: `&mut self` (kind 2) vs an outstanding borrow of the receiver root errors (iterator-invalidation, e.g. `let r=&v[i]; v.push(..)`).

Source: `src/compiler/borrow_check.cpp#L3344-L3433`, `src/compiler/borrow_check.cpp#L3543-L3562`

## Taking borrows (`take`)

### `borrow.take.moved-cannot-borrow` — Cannot borrow a moved value

take_borrow(target): taking any borrow (shared or mut) of target is rejected ('cannot borrow moved value') if target.moved.

Source: `src/compiler/borrow_check.cpp#L1245-L1249`

### `borrow.take.mut-requires-mut-binding` — &mut requires a mut binding

take_borrow(target, is_mut=true): unless skip_mut_binding_check, rejected ('not declared as mut') unless target.is_mut_binding or target is a known function parameter. skip_mut_binding_check is set only by the bare-receiver elision recorder (tracks exclusivity only, leaving binding-mut legality of bare-place receivers permissive).

*Overlaps:* `borrow.mut.require-mut-binding`

Source: `src/compiler/borrow_check.cpp#L1259-L1264`

### `borrow.take.field-borrow-blocks-whole-mut` — Any field borrow blocks a whole-value &mut

take_borrow(target, is_mut=true): rejected ('field of target is already borrowed') if target has any tracked field-path borrow, shared or mut (mut_field_borrows or shared_field_borrows non-empty).

Source: `src/compiler/borrow_check.cpp#L1265-L1272`

### `borrow.take.mut-exclusive` — &mut is exclusive with existing mut, reservation, and shared borrows

take_borrow(target, is_mut=true): rejected ('already mutably borrowed') if target.mut_borrowed is set, or if target.mut_reservations>0 (an in-flight &mut reservation from a sibling call argument is itself a conflict, matching rustc's rejection of `f(&mut x, &mut x)`); outside call-argument evaluation, also rejected if target.shared_borrows>0.

Source: `src/compiler/borrow_check.cpp#L1273-L1284`, `src/compiler/borrow_check.cpp#L1322-L1329`

### `borrow.take.call-arg-mut-reservation` — Two-phase borrow reservation during call-argument evaluation

Inside function-call argument evaluation (in_call_args_>0), take_borrow(target,is_mut=true) records a reservation (target.mut_reservations++) instead of setting mut_borrowed. A shared borrow of target pre-existing from an OUTER scope conflicts with the reservation ('N shared borrow(s) active'); a shared borrow taken within the SAME call's argument-evaluation frame does not conflict with it.

*Extraction note:* Approximates two-phase-borrow activation with a call-frame heuristic (reservation compatible only with borrows recorded in the current scope frame) rather than per-use-point activation; edge cases spanning nested calls are not exercised in this slice.

*Overlaps:* `borrow.two-phase.call-arg-reservation`, `borrow.exclusivity.two-phase`, `borrow.flow.two-phase-mut-reservation`

Source: `src/compiler/borrow_check.cpp#L1285-L1321`

### `borrow.take.shared-vs-mut` — Shared borrow excludes an active mut or mut-field borrow

take_borrow(target, is_mut=false): rejected if target.mut_borrowed ('already mutably borrowed') or target.mut_field_borrows non-empty ('field of target is mutably borrowed'); else target.shared_borrows++ and the borrow is recorded on the current scope (multiple shared borrows coexist).

Source: `src/compiler/borrow_check.cpp#L1329-L1345`

## Scoped borrows (`scoped`)

### `borrow.scoped.conditional-borrow-all-branches` — A conditionally-formed reference borrows every branch operand for the holder's scope

When a reference is formed through a control-flow expression (`if c { &mut x } else { &mut y }`, `match t { A => &x, _ => &y }`), a scoped borrow is taken on every branch's borrowed place (both x and y), held under the binding holder. Non-borrow sub-expressions (condition, scrutinee, guards) are visited normally.

Source: `src/compiler/borrow_check.cpp#L1983-L1988`, `src/compiler/borrow_check.cpp#L2120-L2125`, `src/compiler/borrow_check.cpp#L2217-L2229`

### `borrow.scoped.addrof-takes-place-borrow` — &place takes a borrow of that place, of the reference's mutability

An `&x` / `&mut x` (AddrOf) takes a scoped borrow of x whose mutability is that of the formed reference type, held under the binding holder.

Source: `src/compiler/borrow_check.cpp#L1996-L2002`

### `borrow.scoped.reborrow-borrows-ref-not-pointee` — A reborrow registers a borrow on the reference variable, not its pointee

A reborrow of shape `&*r` / `&mut *r` where r is reference-typed registers a borrow on r itself (freezing r for the borrow's scope), not on r's pointee. NLL releases on the holder's last use, restoring r. Reborrow mutability comes from the formed reference, drawing on r's borrow capacity rather than r's binding-mutness.

Source: `src/compiler/borrow_check.cpp#L2006-L2033`

### `borrow.scoped.index-reborrow-borrows-receiver` — &v[i] borrows the whole indexed container (its receiver)

An indexing reference `&v[i]` / `&mut v[i]` desugars to `&*(Vec::index(&v,i))`; the borrow is recorded on the index method's receiver (the whole container v), so a `v.push()` while the element ref is live is rejected (iterator/element invalidation). `&mut v[i]` forces the receiver borrow to be mutable even when the desugared index_mut self-kind is unresolved.

Source: `src/compiler/borrow_check.cpp#L2034-L2055`

### `borrow.scoped.field-path-borrow-disjoint` — A field-path borrow is path-precise; disjoint sibling fields may be borrowed independently

A borrow of a field chain `&o.f.g` takes a path-aware (dotted) borrow on the root, so disjoint sibling fields borrow without conflict; a borrow whose path overlaps (equal or prefix of) a moved field reports 'use of moved field `<root>.<f>` (moved on line N)'.

Source: `src/compiler/borrow_check.cpp#L2056-L2115`

### `borrow.scoped.union-field-is-whole-value` — A field borrow on a union root is a whole-value borrow

When the borrowed place's root is a union, a field-path borrow is redirected to a whole-value borrow of the root (all sibling fields of a union alias).

Source: `src/compiler/borrow_check.cpp#L2079-L2115`

### `borrow.scoped.index-subexpr-visited-before-borrow` — Index/sub-expressions of a borrowed place are checked before the place is borrowed

When a borrowed place contains an index or other sub-expression, the inner expression is visited (its sub-checks run) before the borrow is registered on the root, avoiding a spurious self-conflict where the recursive visit of the root sees its own freshly-set borrow.

Source: `src/compiler/borrow_check.cpp#L2083-L2115`

### `borrow.scoped.call-result-aliases-ref-args` — A call result bound to a reference holds borrows of every reference argument

When a function-call result is bound to a reference (`let r = f(&a, &b)`), each reference-typed argument is borrowed under the holder (the let binding); a mutation or `&mut` of any such argument while r is live is rejected. NLL releases at the holder's last use. (Conservative upper bound matching Rust elision.)

Source: `src/compiler/borrow_check.cpp#L2127-L2143`

### `borrow.scoped.method-result-holds-receiver-borrow` — A reference-returning method holds a borrow of its receiver for the result's lifetime

A method whose result borrows self (fully-elided &self->&ret, or borrow-carrying result) holds a scoped borrow of the receiver's root place under the holder, with the receiver's mutability; `let v = c.get_ref(); c.set(...)` while v is live is rejected. The borrow is field-precise when the receiver is a field chain, and whole-root otherwise.

Source: `src/compiler/borrow_check.cpp#L2144-L2207`

### `borrow.scoped.method-self-mutability` — The receiver borrow mutability follows the method's self kind

The receiver borrow held for a self-borrowing method result is mutable iff the method takes self by mutable reference (method_self_kind == 2) or an outer `&mut` reborrow forced it mutable; otherwise it is shared.

Source: `src/compiler/borrow_check.cpp#L2146-L2150`, `src/compiler/borrow_check.cpp#L2194-L2206`

### `borrow.scoped.raw-pointer-root-unchecked` — Borrows through a raw-pointer root are not tracked

When a self-borrowing method's receiver roots at a raw pointer, no receiver borrow is recorded (raw pointers are outside borrow checking, Rust parity). Borrows through `&`/`&mut` reference roots are tracked.

Source: `src/compiler/borrow_check.cpp#L2161-L2167`, `src/compiler/borrow_check.cpp#L2181-L2207`

### `borrow.scoped.rc-arc-root-exempt` — Self-borrowing method results on Rc/Arc roots do not hold a receiver borrow

When a self-borrowing method's bare-VarRef receiver roots at an Rc or Arc value, no scoped receiver borrow is recorded: shared-ownership handles are the blessed interior-mutability domain, so `h.array()` followed by `hold(&mut h, root)` is permitted.

> **Divergence** — Logos-specific exemption for Rc/Arc receivers (residency-escape / interior-mutability pattern).

Source: `src/compiler/borrow_check.cpp#L2168-L2207`

## Mutable borrows require `mut` bindings (`mut`)

### `borrow.mut.require-mut-binding` — &mut of a place requires a mut binding (or parameter)

Taking `&mut x` (explicit AddrOf or implicit AddrOfTemp auto-borrow) requires the root binding `x` to be declared `mut`, unless it is a function parameter. Borrowing through a reference root (`&`/`&mut`) needs no `mut` on the reference binding. A raw-pointer root is unchecked.

*Overlaps:* `borrow.mutability.binding-mut-required`, `borrow.take.mut-requires-mut-binding`

Source: `src/compiler/borrow_check.cpp#L3321-L3373`

## Binding mutability (`mutability`)

### `borrow.mutability.binding-mut-required` — Mutation/&mut requires a mut binding

Taking &mut x or assigning to x requires x to be a `let mut` binding; against an immutable binding both are rejected.

*Overlaps:* `borrow.mut.require-mut-binding`

Source: `src/compiler/borrow_check.cpp#L337-L339`

## Borrow exclusivity (`exclusivity`)

### `borrow.exclusivity.shared-vs-mut` — Shared vs exclusive borrow exclusivity

&T (shared) may be held multiply and blocks moves and &mut of the same place; &mut T (exclusive) permits one at a time and blocks moves and all other borrows of the same place.

Source: `src/compiler/borrow_check.cpp#L10-L14`, `src/compiler/borrow_check.cpp#L327-L336`

### `borrow.exclusivity.two-phase` — Two-phase borrows for &mut call arguments

A &mut x taken as a function-call argument is reserved during the rest of argument evaluation and activated at call entry; a reservation does not block concurrent shared reads but does block other mutable borrows.

*Overlaps:* `borrow.two-phase.call-arg-reservation`

Source: `src/compiler/borrow_check.cpp#L333-L336`

### `borrow.exclusivity.disjoint-field-paths` — Disjoint field paths borrow independently

Borrows are tracked by dotted field path; disjoint field paths of the same value may be borrowed simultaneously, even mutably. Two borrows conflict iff one path is a prefix of the other (equal included); a whole-value borrow is path "" and conflicts with every field path.

Source: `src/compiler/borrow_check.cpp#L343-L350`, `src/compiler/borrow_check.cpp#L521-L529`

## Two-phase borrows (`two-phase`)

### `borrow.two-phase.call-arg-reservation` — Mutable borrows in call-argument position are two-phase

A `&mut` borrow taken while evaluating the arguments of a call (call/method-call/closure-call/fn-pointer-call/format-call) is a two-phase-borrow reservation rather than an immediately-active mutable borrow.

*Overlaps:* `borrow.take.call-arg-mut-reservation`, `borrow.exclusivity.two-phase`

Source: `src/compiler/region_infer.cpp#L371-L372`, `src/compiler/region_infer.cpp#L385`, `src/compiler/region_infer.cpp#L409`, `src/compiler/region_infer.cpp#L455-L491`

## Borrow conflicts (`conflict`)

### `borrow.conflict.mut-while-borrowed` — Cannot take &mut a place that is already borrowed

A new borrow of `root.path` conflicts when `root` (or an overlapping path) is already borrowed: a `&mut` borrow conflicts with any existing mutable borrow, with any existing shared borrow, and with any existing shared-field or mut-field borrow whose path overlaps `path`. Path overlap is prefix-or-equal in either direction.

Source: `src/compiler/borrow_check.cpp#L3393-L3424`

### `borrow.conflict.read-vs-mut-borrow` — Whole/field read collides with an outstanding mut borrow (E0503)

Reading a whole value or a field path while an overlapping mut borrow is outstanding is an error (E0503). A shared field borrow leaves whole/overlapping reads legal; only mut field borrows block reads. A partial MOVE of a path collides with ANY outstanding overlapping borrow (E0505: need_exclusive). Skipped in borrow-source position (`&root.path`), where the AddrOf site already resolved the conflict.

Source: `src/compiler/borrow_check.cpp#L3300-L3313`, `src/compiler/borrow_check.cpp#L3504-L3514`

### `borrow.conflict.overlapping-regions` — Borrow conflict requires same target, a mutable participant, and overlapping live regions

Two borrows b1, b2 of the same borrow target conflict iff (b1.target == b2.target) AND (b1.is_mut OR b2.is_mut) AND (barring the TPB-reservation/shared-read exception) the inferred live point-sets of their regions intersect (∃ point P ∈ region(b1) ∩ region(b2)). Two shared (&) borrows of the same target never conflict; a borrow of a distinct target never conflicts. The reported conflict point is the first point found shared between the two regions' point-sets.

*See also:* `borrow.conflict.tpb-reservation-shared-read`

Source: `src/compiler/region_infer.cpp#L903-L936`

### `borrow.conflict.tpb-reservation-shared-read` — A mut reservation passed as a call argument tolerates concurrent shared reads of the same target

A two-phase-borrow (TPB) mut-reservation does not conflict with a concurrent shared (&) borrow/read of the same target: when one of a conflicting pair is a TPB reservation and the other is non-mut, no conflict is reported. A reservation still conflicts with any other mut borrow or reservation of the same target.

> **Divergence** — Logos-specific carve-out with no Rust equivalent: Rust's borrow checker rejects a mutable borrow of a place that overlaps a concurrent shared borrow of the same place (E0502-style); Logos permits this specific pattern for TPB call-argument reservations.

Source: `src/compiler/region_infer.cpp#L914-L919`

## Field borrows (`field`)

### `borrow.field.whole-borrow-blocks-field` — Whole-value borrow blocks any field borrow

take_field_borrow(target,path): rejected if target is already mut_borrowed (blocks ANY new field borrow, shared or mut) — 'target.path: target is already mutably borrowed'. Else, if the new borrow is mut and target.shared_borrows>0, rejected — 'target.path as mutable: target has shared borrows'.

Source: `src/compiler/borrow_check.cpp#L1176-L1186`

### `borrow.field.mutability-through-reference-root` — Field mutation legality from reference-typed root

When the root of field place target.path has reference type (&T/&mut T), mutation legality is decided by the reference's TYPE, not target's `mut` binding: a `&mut`-typed root permits `&mut target.path`; a `&`-typed (shared) root REJECTS `&mut target.path` outright (rustc E0596) regardless of it->is_mut_binding.

Source: `src/compiler/borrow_check.cpp#L1187-L1201`

### `borrow.field.mut-binding-required` — &mut of a field requires a mut binding (non-reference root)

`&mut target.path` where target's root is NOT a reference type is rejected ('not declared as mut') unless target is declared `mut` or is a known function parameter.

Source: `src/compiler/borrow_check.cpp#L1202-L1209`

### `borrow.field.overlapping-path-conflict` — Field borrows conflict on overlapping paths

A new borrow of target.path is rejected if it overlaps (paths_overlap) an existing tracked entry: a mut request conflicting with any live shared_field_borrows entry, or ANY request conflicting with any mut_field_borrows entry. Two paths overlap iff equal or one is a dot-prefix of the other; disjoint sibling paths do not conflict. On success, the borrow is recorded (mut_field_borrows.insert / shared_field_borrows[path]++) and registered on the current scope for end-of-scope release.

*See also:* `borrow.move.path-overlap`

Source: `src/compiler/borrow_check.cpp#L1210-L1233`

## Borrow paths (`path`)

### `borrow.path.prefix-conflict` — Field-path borrow conflict by prefix overlap

Path P is a prefix of path Q iff P==Q or Q begins with P+".". Two field-path borrows of the same root conflict iff their paths overlap (one is a prefix of the other) AND at least one is mutable. The empty path denotes the whole value and overlaps every path.

Source: `src/compiler/borrow_check.cpp#L1112-L1125`, `src/compiler/borrow_check.cpp#L1140-L1141`

### `borrow.path.access-vs-field-borrow` — Accessing a place conflicts with overlapping field borrows

Accessing target.path is rejected if it overlaps a tracked mutable field-borrow of the same root; an exclusive access (whole move or partial move) additionally conflicts with any overlapping shared field-borrow, whereas a plain read conflicts only with a mutable field-borrow. An empty path (whole-value access) overlaps and is rejected against every tracked field-borrow.

Source: `src/compiler/borrow_check.cpp#L1131-L1165`

## Borrow places (`place`)

### `borrow.place.field-path-extraction` — Borrow place: field path with index/deref granularity

The borrowed place of &expr is computed by walking field reads (accumulating a dotted path), index/slice steps (whole-element granularity: prior path components are discarded and the path is the route to the container, no disjointness on the index value), and reference/owning-container derefs (which root the borrow on the deref'd variable). The walk terminates at a variable reference giving the root; `&*r` roots on r.

Source: `src/compiler/borrow_check.cpp#L537-L644`

### `borrow.place.raw-ptr-no-borrow` — Indexing or dereferencing a raw pointer creates no tracked borrow

Indexing through a raw pointer (p[i], p: *mut/*const T) or dereferencing a raw pointer (*p) is an unsafe raw access that creates no tracked borrow of the base; aliasing safety is the programmer's responsibility inside unsafe (Rust parity).

Source: `src/compiler/borrow_check.cpp#L577-L602`, `src/compiler/borrow_check.cpp#L603-L629`

## Union field borrows (`union`)

### `borrow.union.field-borrow-borrows-all` — Borrowing one union field borrows the whole union

Because union fields share storage, a borrow of any one field of a union implicitly borrows ALL fields; field-path borrows of a union root are coerced to whole-root borrows so any other field-path of the same union overlaps and conflicts.

> **Divergence** — A-union-field-borrow-whole (Rust has no native untagged unions with this exact borrow-widening rule; treat as Logos-specific until reconciled against a §A tag)

Source: `src/compiler/borrow_check.cpp#L934-L949`

## Reborrows (`reborrow`)

### `borrow.reborrow.ref-deref` — `&*p` is a reborrow, preserved in shape for borrow-check

`&*p` where `p: *T` / `&mut T` / `&T` lowers preserving the `AddrOfTemp(Deref(p))` node shape (not collapsed to `p` itself), so borrow-check can see the reborrow of p's referent; mlir-gen peepholes the shape back to a plain copy at codegen. When `p` is instead a struct with a user `Deref` impl (no built-in pointer kind), `&*p` dispatches to `p.deref()` via the generic-deref-call path.

*See also:* `expr.deref.user-deref-impl`

Source: `src/compiler/sema_expr.cpp#L2568-L2585`

## Temporaries (`temp`)

### `borrow.temp.distinct-targets` — Each borrow of a temporary is a distinct borrow target

Each borrow of a distinct temporary value is a distinct borrow with its own region; two temporary borrows never alias the same borrow target (tagged uniquely by region id).

Source: `src/compiler/region_infer.cpp#L401-L407`

### `region.temp.ref-into-temporary-dangles-on-escape` — Reference into a statement-scoped temporary dangles on escape

A reference borrowing into a statement-scoped temporary (a fresh value with no named storage: a literal, struct/tuple/array/enum literal, or call/method/closure-call result, including compiler-materialized `__rtmp_N` receivers) dangles the moment it escapes its enclosing statement (e.g. `let v = make().view();`).

Source: `src/compiler/borrow_check.cpp#L436-L443`, `src/compiler/borrow_check.cpp#L457-L481`

### `region.temp.receiver-hoist` — Droppable rvalue auto-ref receiver lives to end of statement

A fresh owning (move-type) rvalue receiver auto-referenced to `&self`/`&mut self` is hoisted into the enclosing statement's block as a named local so it lives to end-of-statement and is then dropped by scope exit, matching Rust temporary-scope semantics; non-droppable or place/borrow receivers keep a plain temp spill.

*Overlaps:* `region.temp-scope.receiver-temp-end-of-statement`

Source: `src/compiler/sema_expr.cpp#L80-L98`

## Temporary scopes (`temp-scope`)

### `region.temp-scope.receiver-temp-end-of-statement` — Auto-ref'd droppable receiver temporary lives to end of enclosing statement

A method call whose receiver is auto-ref'd from a freshly-materialized DROPPABLE rvalue (`W::mk(..).get()`) hoists that rvalue into a named local at the enclosing-statement boundary; the statement is wrapped in a block with that `let` prepended so the temporary's Drop fires at ordinary scope exit — realizing Rust's "temporary scope = end of the enclosing statement" without a dedicated drop-insertion mechanism. When the auto-ref is `&mut self`, the synthesized binding is declared `let mut` (a plain `let` would fail borrow-check on the subsequent mutable borrow).

*Overlaps:* `region.temp.receiver-hoist`

Source: `src/compiler/sema_impl.hpp#L4260-L4274`, `src/compiler/sema_impl.hpp#L4282-L4286`

## Borrow scopes (`scope`)

Scope-end release below is the lexical backstop; NLL rules (`borrow.nll.*`, `borrow.region.nll-liveness-extent`) narrow release to the holder's last use. The two mechanisms are complementary, reconciled by `borrow.scope.lexical-and-nll`.

### `borrow.scope.lexical-and-nll` — Borrow scope: lexical with NLL release

A bound borrow lives in the scope of its holder binding but is released once the holder's last use has passed (non-lexical lifetimes). Call-site borrows (&x in arguments) not bound to a holder are transient and released after the call.

Source: `src/compiler/borrow_check.cpp#L13-L14`, `src/compiler/borrow_check.cpp#L507-L529`

### `borrow.scope.borrows-released-at-scope-end` — Borrows are released at the end of their lexical scope

Each borrow (shared, mutable, or outstanding mut-reservation) is held by the scope frame in which it was taken; on exit from that scope the borrow is released (shared count decremented, mut flag cleared, or one reservation removed), restoring the target's borrow availability. Field-path borrows recorded on the same frame are released the same way.

Source: `src/compiler/borrow_check.cpp#L826-L855`

### `borrow.scope.stored-borrow-outlives-referent` — Every binding records its borrow sources for end-of-scope outlives checking (E0597)

Every `let` binding (not only dropck-relevant ones) records the local borrow sources of its RHS value, so that at scope-pop a stored borrow whose holder outlives the referent it borrows from can be detected and rejected.

> **Divergence** — Rust E0597 analog (borrowed value does not live long enough)

*Overlaps:* `borrow.region.dangling-after-scope-exit`

Source: `src/compiler/borrow_check.cpp#L2736-L2738`

### `borrow.scope.shadowing-fresh-slot` — Each binding gets a fresh slot; shadowing allocates a new slot

Each binding occupies a fresh dense slot allocated at definition; re-declaring a name in the same/inner scope (shadowing) allocates a new slot rather than reusing the prior one (unless a pattern pre-reserved a slot for an Or-alternative).

*Overlaps:* `borrow.move.shadowing-new-slot`

Source: `src/compiler/sema_impl.hpp#L2355-L2364`

## Non-lexical lifetimes (NLL) (`nll`)

### `borrow.nll.dangling-ref-first-use-error` — NLL E0597: a borrow outliving its referent errors at first later use

A reference/borrow-carrying binding that outlives a local it borrows becomes dangling when that local goes out of scope; this is not an error in itself — only the FIRST subsequent USE of the dangling binding is rejected (a stored borrow never used after its referent dies is accepted, matching non-lexical-lifetime semantics). A binding dying in the same scope as its source is never flagged dangling.

Source: `src/compiler/borrow_check.cpp#L765-L775`, `src/compiler/borrow_check.cpp#L878-L893`

### `borrow.nll.borrow-released-at-last-use` — Borrow released at last use, not only at scope end (NLL)

Independent of lexical scope-end release, a borrow whose holder variable's last-use line (computed once per function, over the whole body, before checking) has been reached is released — a non-lexical-lifetimes narrowing of the scope-based release.

*Extraction note:* Only the field declaration and its documenting comment are within this unit; the comparison/release logic against cur_line lives outside this line range.

*Overlaps:* `borrow.nll.release-at-last-use`

Source: `src/compiler/borrow_check.cpp#L796-L799`

### `borrow.nll.release-at-last-use` — Borrows are non-lexical: released once the holder's last use has passed

A pre-pass over each fn body (`scan_uses_expr`/`scan_uses_stmt`/`scan_uses_block`) computes, per named local, the maximum line at which it is read. After visiting each statement in a block, every active borrow (whole-value or field-path) whose holder's last-use line is at or before the current statement's line is released (its exclusive-borrow flag cleared or shared-borrow count decremented). Borrows with no named holder are never released by this mechanism.

*Overlaps:* `borrow.nll.borrow-released-at-last-use`, `borrow.scope.lexical-and-nll`

Source: `src/compiler/borrow_check.cpp#L2315-L2319`, `src/compiler/borrow_check.cpp#L2321-L2464`, `src/compiler/borrow_check.cpp#L2466-L2581`, `src/compiler/borrow_check.cpp#L2590-L2631`, `src/compiler/borrow_check.cpp#L2633-L2640`

### `borrow.nll.capture-flow-store` — Storing a borrowing argument into a receiver taints the receiver's provenance

When a `&mut self` method is called on a tracked local receiver and a by-value borrow-carrying argument (or an argument whose ref-type equals the receiver container's element type, e.g. `Vec<&T>::push(&x)`) is stored into the receiver, the receiver transitively acquires the argument's borrow of the source local. A later use of the receiver after that source local dies is then E0597. `&self` reads and `&x` ref-args do not taint (so `v.contains(&x)`/`v.len()` stay clean).

> **Divergence** — B6: NLL E0597 via capture-flow on container-element stores.

Source: `src/compiler/borrow_check.cpp#L3563-L3604`

## Borrow regions (`region`)

### `borrow.region.dangling-after-scope-exit` — Use of reference after referent leaves scope (E0597)

check_live(name): if name has a tracked dangling_ entry (its borrow-source local went out of scope while name still borrows it), rejected with E0597 ('name does not live long enough: it is borrowed by SRC, which is used here after SRC goes out of scope'). The diagnostic fires once per binding — the dangling_ entry is erased after report.

*Extraction note:* Release of dangling-tracking is line-granular (per formerly-DIVERGENCES-§B6, now closed): multiple borrow-conflicting statements on one physical line can under-report.

*Overlaps:* `borrow.scope.stored-borrow-outlives-referent`, `region.e0597.store-borrow-into-place`

Source: `src/compiler/borrow_check.cpp#L1441-L1449`

### `borrow.region.contains-origin` — A borrow's region contains its creation point

Every borrow expression `&x`, `&mut x`, or a borrow of a temporary creates a fresh region that must contain the CFG point at which the borrow is taken.

Source: `src/compiler/region_infer.cpp#L377-L393`, `src/compiler/region_infer.cpp#L396-L417`

### `borrow.region.nll-liveness-extent` — Borrow region spans the liveness of its holder (NLL)

When a borrow is bound to a named holder (let/assign LHS), its region must contain every CFG point at which the holder variable is live. A borrow's region thus extends exactly to last-use of the reference, not to end of lexical scope (non-lexical lifetimes).

Source: `src/compiler/region_infer.cpp#L113-L137`

## Liveness (`liveness`)

### `borrow.liveness.backward-dataflow` — Variable liveness computed by backward dataflow to fixed point

Liveness over program points (block, stmt-index) is solved by the standard backward dataflow equations: live_out(P) = ∪ live_in(succ(P)); live_in(P) = use(P) ∪ (live_out(P) \ def(P)). Within a block the successor of a statement is the next statement; the successor of the last statement is the union of live_in at the first statement of each CFG successor block; an empty (zero-statement) block forwards its successors' live-in. The solution is the least fixed point, reached by iterating all points in reverse block/statement order (bounded iteration count).

Source: `src/compiler/region_infer.cpp#L811-L901`

### `borrow.liveness.rebind-kills-cross-key` — A name rebind ends prior liveness across bare/slot-qualified key forms

Borrow-liveness tracks live variables by a key that is either bare (name only, e.g. an `Assign` LHS) or slot-qualified (name + binding-slot, e.g. a `let` binding). A bare def of a name kills liveness of every slot-qualified key sharing that name (a reassignment ends the currently-live binding's upward liveness). Symmetrically, a slot-qualified def (a fresh `let name = …` binding) kills the liveness of the bare key for the same name, so a borrow held under an earlier same-named binding does not leak liveness across a shadowing `let` of the same name into sibling scopes.

*Extraction note:* The bare-vs-slot-qualified two-key liveness scheme is an internal representation detail for tracking `let`-shadowed bindings of the same name; the language-level guarantee inferred here is that shadowing a name with a new `let` binding starts a fresh liveness scope that does not spuriously conflict with borrows of the shadowed binding.

Source: `src/compiler/region_infer.cpp#L866-L872`, `src/compiler/region_infer.cpp#L873-L890`

### `region.liveness.slot-qualified-binding` — Liveness keys a binding by name+slot, not name alone

For NLL liveness computation, each declared binding introduced by let/for/for-each is keyed by "name#slot" using a fresh dense per-binding slot allocated at sema time (a shadowing redeclaration of the same name, including in a sibling or nested scope, allocates a new slot). Two same-named bindings in sibling scopes are therefore distinct liveness keys and their live ranges never merge.

Source: `src/compiler/region_infer.cpp#L26-L46`, `src/compiler/region_infer.cpp#L532-L534`, `src/compiler/region_infer.cpp#L714-L718`

### `region.liveness.slotless-fallback` — Slotless sites fall back to bare-name liveness with conservative merging

Sites without a binding slot (fn parameters, and statement-level write receivers such as an Assign LHS) use the bare variable name as the liveness key. A bare DEF (e.g. a plain `x = expr` reassignment) kills liveness of every slot-qualified variant of that name, ending the prior binding's upward live range; a bare USE conservatively marks every same-named slot-qualified holder as live, over-approximating rather than under-approximating region extent.

Source: `src/compiler/region_infer.cpp#L34-L38`, `src/compiler/region_infer.cpp#L118-L128`, `src/compiler/region_infer.cpp#L536-L539`, `src/compiler/region_infer.cpp#L720-L726`

## Capture liveness (`live`)

### `borrow.live.closure-captures` — Closure capture requires live captured variables

Forming a closure checks each captured variable is live (not moved/dead) at the point of capture.

Source: `src/compiler/borrow_check.cpp#L3760-L3764`

## Control-flow state merging (`flow`)

### `borrow.flow.merge-moved-on-join` — Move state at control-flow join is the union of branches

At a control-flow merge of two branch states, a place is considered moved in the joined state iff it is moved in EITHER branch (move-state is unioned, not intersected); a non-diverging branch's moves propagate into the join.

*See also:* `borrow.flow.diverged-arm-skipped`

Source: `src/compiler/borrow_check.cpp#L648-L652`

### `borrow.flow.diverged-arm-skipped` — Diverging branch arms do not contribute moves to the join

A branch arm that diverges (ends in return/break/continue) is excluded from the move-state merge: its moves do not pollute the join, since control reaching the join cannot have come through that arm.

*Extraction note:* Merge-skip logic enforcing this lives in If/Match merge sites outside this slice; here only the cur_diverged_ flag is declared.

Source: `src/compiler/borrow_check.cpp#L784-L788`

### `borrow.flow.two-phase-mut-reservation` — Mutable borrows taken during call-argument evaluation are reservations

While the checker is nested inside evaluation of a call's argument list (in_call_args_ > 0), a newly taken `&mut` borrow is recorded as a reservation (mut_reservations) rather than an activated exclusive borrow, so it does not conflict with concurrent shared reads of the same target for the remainder of that argument-list evaluation (two-phase borrow). The reservation is released the same as an activated mut borrow at scope end.

*Overlaps:* `borrow.take.call-arg-mut-reservation`

Source: `src/compiler/borrow_check.cpp#L736-L739`, `src/compiler/borrow_check.cpp#L833-L840`

### `borrow.flow.break-continue-diverge` — Break and continue diverge the current statement flow

`break` and `continue` mark the current control-flow path as diverged for move-state propagation; their post-state does not flow to the following statements at the same level.

Source: `src/compiler/borrow_check.cpp#L3181-L3184`

### `borrow.flow.if-branch-merge` — If-expression merges move/provenance state across branches

An `if`/`else` expression evaluates the condition consuming, then each branch from the same pre-branch state; the post-state is the merge of both branches (a value moved in either branch is treated as moved after; provenance is unioned).

Source: `src/compiler/borrow_check.cpp#L3674-L3688`

### `borrow.flow.match-branch-merge` — Match merges move/provenance across arms; arm bindings are arm-scoped

A `match` evaluates the scrutinee non-consuming, then each arm from the same pre-match state with its pattern bindings declared in an arm-local scope (guard evaluated consuming). The post-match state merges all arms: a place moved in any arm (when present in the pre-state) is treated as moved after; provenance is unioned across arms.

Source: `src/compiler/borrow_check.cpp#L3691-L3723`

## Branch merge (`merge`)

### `borrow.merge.moves-union` — Branch merge unions move state; borrows are scope-local

At control-flow joins, move (Phase-1) state is merged conservatively by union of moved sets; borrows are scope-local (released by scope pop) and do not survive merges. Variables of outer scope moved inside a loop body are dead after the loop.

Source: `src/compiler/borrow_check.cpp#L20-L23`, `src/compiler/borrow_check.cpp#L646-L652`

## `if` merging (`if`)

### `borrow.if.merge-survivors` — If/else move-state merge keeps only surviving branches

After `if c { A } else { B }`, the post-state is the merge (union of moves) of the non-diverging branches: if both branches diverge the whole `if` diverges; if exactly one diverges, the post-state is the surviving branch's; otherwise the two branches' move and provenance states are merged (`merge_moves`/`merge_provs`).

Source: `src/compiler/borrow_check.cpp#L3020-L3053`

## `match` (`match`)

### `borrow.match.merge-arms` — Match move-state is the union over non-diverging arms

Each match arm is checked from the pre-match state (move/provenance/ref-borrow-source/dangling snapshots), independently of prior arms; a diverging arm (return/break/continue tail) contributes nothing to the join. The post-match state unions moves across surviving arms (a path moved in any surviving arm is moved after the match); borrow-source and dangling facts are likewise unioned across surviving arms (a binding borrows/dangles if any arm makes it so). If every arm diverges, the match diverges.

Source: `src/compiler/borrow_check.cpp#L3094-L3177`

### `borrow.match.scrutinee-move-tracking` — Whole-value move-out through an unguarded match arm marks the scrutinee moved

When an unguarded match arm binds-and-moves the whole by-value move-type scrutinee — as a whole binding, or via struct/tuple/variant-payload destructure — the scrutinee variable is marked moved for the remainder of the enclosing scope, so the enum's own scope-exit Drop does not double-free a value now owned by an arm binding or by the match expression's result. Applied uniformly by both statement-position and expression-position match lowering.

Source: `src/compiler/sema_impl.hpp#L4330-L4335`

### `borrow.match.per-arm-move-reset` — move state resets per arm; post-match is union over non-diverging arms

Each match arm is checked from the move state as it stood before the match (reset at every arm boundary); a variable's post-match moved status is the union of moves contributed by arms that fall through (do not return/break/continue). Diverging arms contribute no post-match moves; if every arm diverges, the pre-match move state is kept.

Source: `src/compiler/sema_stmt.cpp#L8726-L8738`, `src/compiler/sema_stmt.cpp#L8848-L8852`, `src/compiler/sema_stmt.cpp#L9106-L9131`

### `borrow.match.definite-assignment-merge` — definite-assignment merges across match arms like if/else

Definite-assignment state is reset to the pre-match state for each arm and merged after the match as the union of still-uninitialized variables over non-diverging arms (a variable is uninitialized post-match iff uninitialized on any falling-through arm). Diverging arms contribute nothing; if every arm diverges, the pre-match uninit state is kept.

Source: `src/compiler/sema_stmt.cpp#L8740-L8746`, `src/compiler/sema_stmt.cpp#L8852`, `src/compiler/sema_stmt.cpp#L9117-L9131`

### `borrow.match.scrutinee-moved-by-binding` — binding+moving a payload out of a by-value scrutinee marks it moved

A match-expression that binds and moves a payload out of a by-value move-type scrutinee (`let x = match v { Ok(s) => s }`) marks the scrutinee moved, so its scope-exit Drop does not double-free a value the result already owns (G156-2).

*See also:* `expr.match.temp-scrutinee-dropped`, `borrow.match.arm-binding-drop`

Source: `src/compiler/sema_stmt.cpp#L8938-L8944`

### `borrow.match.arm-binding-drop` — arm-scope pattern bindings are dropped before the arm value escapes

Pattern bindings introduced by a non-divergent value-form arm are dropped at arm-scope exit; bindings consumed by the arm value are first marked moved (lower_return semantics), then remaining droppables are dropped after the arm value is hoisted into a temporary that is yielded. Error/Never-typed arm values skip this.

*See also:* `borrow.match.scrutinee-moved-by-binding`

Source: `src/compiler/sema_stmt.cpp#L9552-L9592`

## Loops (`loop`)

### `borrow.loop.body-borrows-scoped` — Loop bodies: borrows are body-scoped, only moves of outer vars propagate

A loop body is analyzed in its own scope (loop-iteration variables declared local to it); borrows taken inside are released when the scope is popped at body end. Only MOVES of pre-existing OUTER variables propagate out of the loop to the enclosing state. Provenance is merged conservatively across the loop (the body may execute zero or more times).

Source: `src/compiler/borrow_check.cpp#L2644-L2663`

### `borrow.loop.body-fixpoint` — Loop bodies are borrow-checked under loop semantics

While/For/Loop/ForEach bodies are checked via the loop-body protocol: the condition (while) and range bounds (for) are consumed before the body; for-each's iterator expression is visited non-consumingly; the loop variable (for/for-each) is a fresh binding scoped to the body.

*Extraction note:* The loop-body fixpoint/merge algorithm itself (visit_loop_body) is defined outside this slice; only the per-loop-kind entry semantics (what's consumed, what's bound) are evidenced here.

Source: `src/compiler/borrow_check.cpp#L3057-L3091`

## Drops at control-flow exits (`drop`)

### `borrow.drop.break-continue-to-loop-frame` — break/continue runs drops down to enclosing loop body

A `break`/`continue` runs the destructors of every scope frame from the innermost enclosing frame down to and including the enclosing loop-body frame, so a break/continue nested inside an `if` still drops the loop body's locals.

Source: `src/compiler/sema_impl.hpp#L2269-L2272`

## Drop check (`dropck`)

### `borrow.dropck.drop-binding-must-outlive-borrowed-local` — A Drop-having binding may not borrow a local that dies first

A binding is dropck-relevant iff its type is a struct that has a Drop impl (needs_drop) AND either its declared template has a lifetime parameter or its TypeRef carries explicit lifetime_args. For such a binding, every local it borrows at construction must outlive it; a local going out of scope while the binding still lives is rejected (the binding's Drop would run after the local dies).

Source: `src/compiler/borrow_check.cpp#L856-L877`, `src/compiler/borrow_check.cpp#L918-L933`

### `borrow.dropck.record-local-borrow-sources` — Dropck-relevant let bindings record the local places they borrow from

When a `let` binds a value whose type is Drop-and-lifetime (dropck) relevant, the local variables the value borrows from are collected and recorded against the binding name (with its line), so that drop-order checking can detect a borrowed referent being dropped before the borrowing binding.

Source: `src/compiler/borrow_check.cpp#L2727-L2735`

### `borrow.dropck.assign-borrow-into-drop-struct` — Dropck-light: borrows stored in a Drop lifetime-struct are tracked

When an assignment (re-)fills a dropck-relevant (Drop-having, lifetime-parameterised) struct binding with a value that syntactically wraps freshly-borrowed locals, those borrow sources are recorded against the binding (with the assignment's line) so its later Drop cannot reference a borrow whose source has died — the same bookkeeping performed at initial declaration.

*Extraction note:* Pattern is detected syntactically (dropck-light); exact soundness scope depends on struct_is_dropck_relevant/collect_borrow_locals defined outside this unit.

Source: `src/compiler/borrow_check.cpp#L2789-L2800`

## Closures and captures (`closure`)

### `borrow.closure.move-owns-non-move-borrows` — move closure owns by-value captures; non-move closure borrows

A non-`move` closure captures by reference (env stores a pointer/borrow of the outer variable; mutations escape to the outer variable). A `move` closure transfers ownership of each by-value capture into the env: an owned droppable capture is dropped by the closure's env drop glue and NOT by the original scope (exactly one owner). `&dyn` handle captures remain borrows even under `move` (a dyn value-fat-pair is a borrowed handle, not owned storage).

Source: `src/compiler/mlir_gen_dyn.cpp#L1791-L1832`, `src/compiler/mlir_gen_dyn.cpp#L2117-L2147`

### `borrow.closure.mut-scalar-fnmut-state` — Mutated scalar capture: move owns a persistent copy, non-move escapes

For a mutated scalar (let-bound) capture: a `move` closure stores a value copy in the env and reads/writes through the env field (the mutation persists across calls as FnMut state and never touches the outer variable, which is Copy so needs no drop); a non-`move` closure stores the outer variable's address so mutations escape to the outer variable.

Source: `src/compiler/mlir_gen_dyn.cpp#L1780-L1800`, `src/compiler/mlir_gen_dyn.cpp#L2026-L2040`

### `borrow.closure.disjoint-field-capture` — RFC-2229 disjoint closure capture of a struct field path

A closure captures the narrowest field path it uses rather than the whole variable (RFC-2229). For an escaping `move` closure a narrow capture owns only the leaf FIELD value inline (the field's Drop runs via env glue; the original root still drops the rest); a non-escaping narrow capture borrows the whole root by pointer. The captured path is the LCA-widening of all uses, so the body only reads the captured leaf.

Source: `src/compiler/mlir_gen_dyn.cpp#L1833-L1841`, `src/compiler/mlir_gen_dyn.cpp#L1978-L2007`, `src/compiler/mlir_gen_dyn.cpp#L2140-L2146`

### `borrow.closure.capture-by-ref-loan` — Non-move closure captures register field-path (RFC-2229) borrows

A non-`move` closure capturing place `p` by reference registers a borrow of `p` held for the closure holder's lifetime: a mutated/`&mut` capture registers a `&mut` (exclusive) loan, a shared capture registers a `&` (shared) loan/liveness check. A capture of a strict sub-field `p.x` registers a precise FIELD-PATH borrow (so disjoint sibling access `&mut p.y` beside `|| p.x` is allowed; a conflicting `&mut p.x` is rejected); a whole-root capture instead registers a whole-value borrow (mut) or a bare liveness check (shared), since Logos captures a whole variable and a whole-var shared borrow would otherwise block disjoint sibling mutation. Loans release at the closure holder's last use (NLL).

> **Divergence** — RFC-2229 disjoint closure capture: field-path precision, but a whole-var SHARED capture is a liveness check only (not a recorded shared borrow) to avoid blocking sibling mutation

*See also:* `borrow.closure.move-takes-ownership`

Source: `src/compiler/borrow_check.cpp#L2241-L2276`

### `borrow.closure.move-takes-ownership` — move closure captures take ownership, registering no borrow

A `move` closure takes ownership of each captured place; it registers no borrow (the captured value is consumed into the closure rather than loaned).

*See also:* `borrow.closure.capture-by-ref-loan`

Source: `src/compiler/borrow_check.cpp#L2247`, `src/compiler/borrow_check.cpp#L2251`

### `borrow.closure.skip-source-drop-on-body-move` — Per-capture skip of source-side drop when body moves the capture

For each capture, the closure's source-side owned-drop is skipped iff the closure body itself moved that capture into a callee, because the callee's parameter drop is then the canonical drop site; this prevents a double-drop. The body-moved-capture set is computed as the variables newly moved during body lowering relative to a pre-body snapshot.

Source: `src/compiler/sema_expr.cpp#L14233-L14241`, `src/compiler/sema_expr.cpp#L14335-L14338`

### `region.closure.escaping-env-heap-allocated` — Escaping (boxed) closure heap-allocates its environment

A closure that escapes its defining frame (e.g. boxed as `Box<dyn Fn>`) and has captures must heap-allocate its env so the {fn, env_ptr} value outlives the frame; a non-escaping closure with captures uses a stack env; a capture-less closure uses a null env pointer (its drop is a no-op).

Source: `src/compiler/mlir_gen_dyn.cpp#L2081-L2115`

## Non-movable (pinned) types (`pin`)

### `borrow.pin.non-movable-no-by-value-slot` — Location-anchored types may not occupy a by-value slot

A non-movable (location-anchored) type — one with a self-relative `#[rel_ptr]`/`#[zoned2]` field, or a `#[pinned]` type — may not be bound to any by-value slot (let local, parameter, match/for/closure/destructure binding); it must live behind a pointer, in place (e.g. an arena or `[u8;N]` buffer), and be built through a `*mut T`.

> **Divergence** — A8: `#[pinned]` is non-movability as a property of the TYPE (no value-form), distinct from Rust's pointer-level `Pin<P>`.

Source: `src/compiler/sema_impl.hpp#L2337-L2354`, `src/compiler/sema_impl.hpp#L2454-L2461`

## Returns (`return`)

### `borrow.return.diverges` — Return consumes its value and diverges control flow

A `return v` consumes (moves) `v` and marks the current control-flow path as diverged, so its post-state does not flow to a join point.

Source: `src/compiler/borrow_check.cpp#L2808-L2814`

### `borrow.return.no-move-out-of-ref` — Cannot return a move-out of a reference/index

Returning a move-type value taken out of a value behind a reference or out of an index is rejected (E0507).

Source: `src/compiler/sema_stmt.cpp#L2873-L2874`

### `borrow.return.intlit-fits` — Integer-literal return must fit return type

An integer-literal return value (including elements of returned array/tuple literals, recursively) must fit the return type's (element's) integer kind; otherwise "return: literal value V does not fit in T".

Source: `src/compiler/sema_stmt.cpp#L2883-L2951`

### `borrow.return.mark-moved` — Return expression moves its move-type subexpressions

Any move-type variable or field appearing in the return expression (recursing through enum-payloads, call args, struct/tuple-literal fields, and block-expr results) is marked moved so scope-exit drop collection does not double-free it.

Source: `src/compiler/sema_stmt.cpp#L2955-L3001`

### `region.return.dangling-local-or-temp` — Returning a reference with local or temporary provenance is rejected as dangling

If the function returns a reference (or borrow-carrying) type and the returned expression's provenance is is_local or is_temp, the return is an error: a reference to a temporary (is_temp / AddrOfTemp source) reports 'cannot return reference to temporary value: dangling reference'; a reference to a named local reports 'cannot return reference to local variable `<name>`: dangling reference'.

*Overlaps:* `region.dangling.no-return-local-ref`

Source: `src/compiler/borrow_check.cpp#L1862-L1890`

### `region.return.named-lifetime-must-outlive` — Returned borrow source lifetime must equal or outlive the declared return lifetime

When the return type has an explicit (non-'_) lifetime 'ret, each traced parameter source's declared lifetime 'src must satisfy 'src == 'ret OR 'src outlives 'ret (an explicit `where 'src: 'ret`, or 'static); otherwise a 'lifetime mismatch: return type has lifetime 'ret but `<src>` has lifetime 'src' error is reported.

Source: `src/compiler/borrow_check.cpp#L1892-L1948`

### `region.return.non-ref-param-source-deferred` — Returns sourced from a non-reference (aggregate) parameter defer to the type checker

When the return-type lifetime check finds no ref-typed parameter sources (provenance traces to a struct/aggregate parameter holding refs, e.g. returning `x.y` with `y: &'b u8`), no lifetime error is raised; the declared-return type match is trusted as already verified by the type checker. Likewise an elided outer ref lifetime over an aggregate-pointing param defers to the type checker.

*Extraction note:* B86: deferral characterized as compensating for incomplete impl-level lt_arg propagation; the normative content is that such returns are accepted.

Source: `src/compiler/borrow_check.cpp#L1895-L1911`, `src/compiler/borrow_check.cpp#L1930-L1943`

### `region.return.elision-single-ref-param` — Elided return lifetime with one reference parameter must derive from that parameter

When the return lifetime is elided ('_) and exactly one reference-typed parameter exists, the returned reference's provenance must include that sole parameter; otherwise 'lifetime elision: return reference must derive from `<p>` (the only reference parameter)' is reported. With multiple ref parameters the source is ambiguous and any param source is accepted.

Source: `src/compiler/borrow_check.cpp#L1952-L1968`

### `region.return.untraced-is-safe` — A returned borrow with empty, non-local provenance is accepted

If the returned expression has empty provenance and is not local/temp (e.g. a function-call result, global, or untraceable expression), the return is accepted even when ref parameters exist (conservative non-error).

Source: `src/compiler/borrow_check.cpp#L1969-L1976`

### `region.return.temp-drops-before-terminator` — Return-value temporaries drop before the return terminator

If lowering a `return`'s value expression hoists statement-temporaries, their `let`s and drops are sequenced strictly before the `return` terminator (`let __t..; let __rv = <value>; drop __t..; return __rv;`); temporaries dropped after a terminator would be unreachable and leak.

Source: `src/compiler/sema_impl.hpp#L4275-L4280`

## Escape tracking (`escape`)

### `borrow.escape.borrow-carrying-struct` — borrow_carrying struct/enum values are escape-tracked like references

Values of a struct or enum annotated `#[borrow_carrying]` are tracked by the borrow checker for escape/lifetime like ordinary references.

> **Divergence** — A: #[borrow_carrying] Logos addition for opaque borrow-holding types (WAny).

*Overlaps:* `borrow.escape.borrow-carrying-value`, `region.borrow-carrying.escape-tracked`, `region.escape.borrow-carrying-type`

Source: `src/compiler/sema_decl.cpp#L1226-L1228`, `src/compiler/sema_decl.cpp#L1444`

### `borrow.escape.borrow-carrying-value` — #[borrow_carrying] values escape-tracked like references

A `#[borrow_carrying]` value type (struct or enum) may contain an absolute Ref into an arena; the borrow checker tracks its escape like a reference — a method/ctor returning one ties the result to its ref receiver/arg, so returning it past the source's scope is rejected unless laundered through a holder.

*Overlaps:* `borrow.escape.borrow-carrying-struct`

Source: `src/compiler/sema_impl.hpp#L2473-L2480`, `src/compiler/sema_impl.hpp#L2610`

### `region.escape.borrow-carrying-type` — Borrow-carrying types and transitive containers are escape-tracked

is_borrow_carrying_type(t): a named struct/zoned-struct/niche-enum type is borrow-carrying iff registered in ts_.borrow_carrying, UNLESS its name is registered in ts_.residency_exempt (laundered escape wrappers `Held<T>`/`HeldAny`, holding an Rc/Arc that keeps the arena alive, are NEVER borrow-carrying — even through their type-args, e.g. `Held<WArray<WAny>>`). A generic container whose element type-arg is itself borrow-carrying (`Vec<WAny>`, `Option<WAny>`, `Box<WAny>`) is transitively borrow-carrying. A raw pointer to such a type (*mut WAny) has no type-args on the pointer itself and is NOT checked.

> **Divergence** — Logos-only extension: #[borrow_carrying] generalizes escape/lifetime tracking to non-reference (arena-view) types; Rust has no analogous whole-value lifetime annotation.

*Overlaps:* `region.borrow-carrying.escape-tracked`

Source: `src/compiler/borrow_check.cpp#L1634-L1659`

### `region.escape.value-local-root-walk` — Value-local root of a borrow place

value_local_root(e): strips one optional leading AddrOfTemp, then follows a FieldRead/TupleIndex/IndexRead/Deref chain to its terminal. A Deref of a raw-pointer-typed operand stops the walk and returns empty (the pointee is not tied to the pointer's stack lifetime, e.g. box_leak's `&mut *into_raw(b)`). If the terminal is a VarRef naming a tracked VALUE local that is NOT a function parameter and NOT a tracked ref-binding (absent from prov_), its name is returned; otherwise empty. A reference rooted at such a local is treated as dangling once the local's scope ends.

Source: `src/compiler/borrow_check.cpp#L1661-L1697`

## `#[borrow_carrying]` types (`borrow-carrying`)

### `region.borrow-carrying.escape-tracked` — #[borrow_carrying] values are escape-tracked like references

A value of a `#[borrow_carrying]` struct or enum holds a borrow into an arena and is escape-tracked like a reference; returning it escapes the borrow as if returning the bare reference. Borrow-carrying-ness propagates transitively: a struct with an inline field, or an enum with a variant payload, of a (transitively) borrow-carrying type is itself borrow-carrying, as is a container whose generic type-argument is borrow-carrying (e.g. `Vec<WAny>)`.

> **Divergence** — Logos addition (no Rust equivalent)

*Overlaps:* `borrow.escape.borrow-carrying-struct`, `region.escape.borrow-carrying-type`

Source: `src/compiler/borrow_check.cpp#L52-L54`, `src/compiler/borrow_check.cpp#L137-L164`, `src/compiler/borrow_check.cpp#L204-L227`

### `region.borrow-carrying.residency-holder-exempt` — Residency-holder packages are exempt from borrow-carrying

A struct with an Rc/Arc field (a residency-holder / laundered-escape package such as `Held<T>`/`HeldAny`) ref-counts the arena alive independent of any local, so it is NOT borrow-carrying and may safely escape — even via its type-arguments. An explicit `#[borrow_carrying]` annotation overrides this auto-exemption.

> **Divergence** — Logos addition (no Rust equivalent)

Source: `src/compiler/borrow_check.cpp#L55-L60`, `src/compiler/borrow_check.cpp#L165-L203`, `src/compiler/borrow_check.cpp#L207-L209`

## Borrow provenance (holder tracking) (`prov`)

### `borrow.prov.borrow-returning-call-ties-to-ref-inputs` — A borrow-returning call's provenance is its reference inputs

When a method or free function returns a reference (or borrow-carrying type), the result's borrow provenance is the set of reference-typed inputs (receiver and reference arguments for methods; reference arguments for free calls) it was derived from — a lifetime-elision provenance model — so the result cannot escape the scope of any borrowed local it transitively names.

Source: `src/compiler/borrow_check.cpp#L1062-L1096`

### `borrow.prov.ref-copy-propagates-sources` — Copying a reference binding propagates its borrow sources

Assigning one reference binding from another (o = r) makes o borrow whatever r borrows: the source set is propagated, so an aliased borrow cannot escape its referent's scope via a copy.

Source: `src/compiler/borrow_check.cpp#L1097-L1106`

### `borrow.prov.binding-never-borrows-itself` — A binding is never recorded as borrowing itself

When recording the local sources a binding borrows from, the binding's own name is removed from its source set; a reborrow such as let r2 = &*r records r (not r2) as the source.

Source: `src/compiler/borrow_check.cpp#L987-L1002`, `src/compiler/borrow_check.cpp#L1009-L1022`

### `borrow.prov.locals-only-not-params` — Borrow-source tracking covers only local variables, not parameters

Provenance/dangling tracking records as borrow sources only locally declared variables (present in the current function's state map); function parameters are filtered out, since a borrow of a parameter does not dangle within the function body.

Source: `src/compiler/borrow_check.cpp#L953-L963`, `src/compiler/borrow_check.cpp#L1032-L1044`

### `borrow.prov.desugared-index-mut-outer-mut-authoritative` — Outer &mut is authoritative for desugared IndexMut reborrows

For a compiler-desugared `&mut v[i]` (AddrOfTemp over Deref over a MethodCall to index_mut), the borrow's mutability is taken from the outer `&mut` regardless of what escape analysis can resolve for the inner desugared index_mut call — this prevents two concurrent `&mut v[i]` borrows on the same aggregate from being under-classified as shared and going undetected as aliasing.

Source: `src/compiler/borrow_check.cpp#L727-L732`

## Reference provenance (`provenance`)

### `region.provenance.ref-aliases-params-or-local` — Reference provenance tracks param/local origin

Each reference-typed variable tracks the set of function parameters it may alias and whether any path originates from a local; provenance from a global or function return value is treated as safe to return. Provenance merges across branches by union of param-sets and OR of the is_local/is_temp flags.

Source: `src/compiler/borrow_check.cpp#L426-L455`

### `region.provenance.param-ref-source` — Reference to a ref-typed parameter carries that parameter as provenance source

A `VarRef` to a parameter `p` whose type is a reference has provenance source {p}. An `AddrOf p` of a reference parameter likewise yields source {p}. Provenance source identifies which named inputs a returned borrow may point into.

Source: `src/compiler/borrow_check.cpp#L1706-L1722`

### `region.provenance.local-borrow` — Borrow of a local variable is local provenance

`&x` (AddrOf) where x is a tracked local (not a parameter, not a materialized temp) has provenance is_local=true. Such a borrow is valid in scope but dangles if it escapes (e.g. is returned).

Source: `src/compiler/borrow_check.cpp#L1715-L1722`

### `region.provenance.materialized-temp-statement-scoped` — Borrow of a materialized statement-temporary is statement-scoped (is_temp)

A borrow whose root is a materialized statement-temporary (`__rtmp_N`, the hoisted local for a fresh rvalue receiver in `make().view()` => `(&__rtmp_0).view()`) has provenance is_temp=true: the temporary drops at end of statement, so any reference into it dangles once it escapes the statement.

Source: `src/compiler/borrow_check.cpp#L1718-L1719`, `src/compiler/borrow_check.cpp#L1724-L1740`

### `region.provenance.temp-lifetime-extension` — Direct `&<temporary>` bound to a let is lifetime-extended (local, not statement-temp)

A DIRECT borrow of a literal/struct-literal/call rvalue (`let r = &mut 5;`) is lifetime-extended: the temporary lives as long as the binding, so provenance is is_local (NOT is_temp). It is therefore caught only when returned past the scope, not at the binding site.

*Extraction note:* Distinction between materialized __rtmp temp (statement-scoped) and direct &temp (lifetime-extended) inferred from the two AddrOfTemp branches.

Source: `src/compiler/borrow_check.cpp#L1745-L1752`

### `region.provenance.value-local-root-borrow` — Borrow rooted at a value local is local provenance

A borrow rooted at a by-value local through field/index/deref chains (`&c.x`, `&c.a[i]`, `&*h` where h is a value-local smart pointer) has provenance is_local=true and dangles if returned.

Source: `src/compiler/borrow_check.cpp#L1753-L1757`

### `region.provenance.unknown-conservative-accept` — Unresolvable borrow provenance is conservatively accepted

When a borrow's root cannot be traced to a parameter, local, or temporary, provenance is empty (unknown) and the borrow is conservatively accepted (treated as caller-owned / non-escaping).

Source: `src/compiler/borrow_check.cpp#L1758`, `src/compiler/borrow_check.cpp#L1854-L1856`

### `region.provenance.projection-transparent` — Place projections forward the receiver's provenance

Field read, deref, tuple index, cast, and index read forward the provenance of their operand/receiver unchanged.

Source: `src/compiler/borrow_check.cpp#L1760-L1769`

### `region.provenance.control-flow-merge` — Provenance of a value-producing control-flow expression is the merge of its branches

An if-expr's provenance is the merge of its then and else value provenances; a block-expr's is its result's; a match-expr's is the merge over all arm values. Merge unions param sources and ORs the is_local/is_temp flags.

Source: `src/compiler/borrow_check.cpp#L1770-L1782`

### `region.provenance.method-result-borrows-receiver` — A reference-returning method result borrows its receiver (lifetime elision)

For a method call whose result type is a reference (or a #[borrow_carrying] value type), the result's provenance equals the receiver's provenance (output lifetime ties to &self). A non-ref, non-borrow-carrying result has empty provenance (owned).

Source: `src/compiler/borrow_check.cpp#L1783-L1797`, `src/compiler/borrow_check.cpp#L1814`

### `region.provenance.ref-self-method-temp-receiver` — A ref-self method on a temporary receiver yields a statement-temp borrow

When a ref-self method (self by reference, method_self_kind != 0) is called on a temporary receiver, the result points into that temporary, so provenance is_temp=true. A by-value-self adapter (self: Self) instead consumes/moves the temporary into the result and does not produce a statement-temp escape.

Source: `src/compiler/borrow_check.cpp#L1797-L1814`

### `region.provenance.bare-value-local-receiver` — Reference result of a method on a bare value-local receiver is local provenance

If a method's reference/borrow-carrying result has otherwise-empty provenance but its receiver roots at a value-local (e.g. `Rc::deref` on `h` directly), the result provenance is marked is_local.

Source: `src/compiler/borrow_check.cpp#L1807-L1814`

### `region.provenance.borrow-carrying-call-aliases-ref-args` — A borrow-carrying function result may alias its reference arguments

A free-function/constructor call returning a #[borrow_carrying] value has provenance equal to the merge of the provenances of its reference-typed arguments (`WAny::from(&x)` aliases x). A non-borrow-carrying call result is caller-owned (empty provenance). Value (non-ref) arguments contribute no provenance.

Source: `src/compiler/borrow_check.cpp#L1816-L1828`

### `region.provenance.aggregate-literal-merge` — Aggregate literal provenance is the merge of its element/field initializers

A struct literal, tuple literal, or enum-data literal has provenance equal to the merge of the provenances of its field values / elements / payloads; returning the aggregate escapes any borrow carried by a borrow-carrying field. Pod/owned fields contribute empty provenance.

Source: `src/compiler/borrow_check.cpp#L1829-L1853`

## Dangling references (`dangling`)

### `region.dangling.no-return-local-ref` — No returning a reference to a local

A function returning &T or &mut T must not return a reference whose provenance includes a local variable; parameters outlive the call and are safe to borrow from, locals are not.

*Overlaps:* `region.return.dangling-local-or-temp`

Source: `src/compiler/borrow_check.cpp#L16-L18`, `src/compiler/borrow_check.cpp#L426-L443`

### `region.dangling.dyn-trait-ref` — &dyn Trait data half is a borrowed reference

A borrowing trait object (&dyn Trait, non-owning Kind::TraitObject) is treated as a reference kind for dangling-return detection: returning &dyn Trait to a local is rejected; an owning `Box<dyn Trait>` does not qualify.

> **Divergence** — logos-core 2.1 default trait-object lifetime rule

Source: `src/compiler/borrow_check.cpp#L488-L501`

## Stored borrows (E0597) (`e0597`)

### `region.e0597.store-borrow-into-place` — Storing a borrow into a place records its lifetime sources

Storing a reference into a place — `x = &y` (rebind), `root.f = &y` / chained `root.f1.f2 = &y`, `root.N = &y` (tuple field), or a lowered `SDerefWrite` whose LHS walks a PURE FieldRead/TupleIndex chain to a non-parameter root `VarRef` — records the borrow source(s) `y` against the destination's root local (`record_ref_sources`/`add_ref_sources`); a rebind first re-owns the binding (clearing any prior dangling record). A later use of the root after `y` dies is diagnosed E0597 "does not live long enough". Writes through a deref or into an element (not the root's own storage) do not record a source.

*Overlaps:* `borrow.region.dangling-after-scope-exit`

Source: `src/compiler/borrow_check.cpp#L2801-L2803`, `src/compiler/borrow_check.cpp#L2842-L2845`, `src/compiler/borrow_check.cpp#L2896-L2903`, `src/compiler/borrow_check.cpp#L2982-L2999`, `src/compiler/borrow_check.cpp#L3006-L3013`

## Outlives relation (`outlives`)

### `region.outlives.normalize-apostrophe` — Lifetime names normalized to leading-apostrophe form

A lifetime region name is identified by its with-apostrophe spelling: the bare form `a` and the prefixed form `'a` denote the same region. Both `'static` and `static` denote the 'static region. Region identity comparisons and graph keys are taken after this normalization.

Source: `include/logos/compiler/outlives.hpp#L29-L33`, `include/logos/compiler/outlives.hpp#L35-L37`

### `region.outlives.static-is-top` — 'static outlives every region

'static: r holds for every region r ('static is the longest-living region). Conversely, no region other than 'static satisfies r: 'static unless r is 'static or r reaches 'static through the explicit graph.

*Overlaps:* `region.outlives.static-longest`, `region.outlives.static-always-satisfies`, `region.outlives.static-always-known`

Source: `include/logos/compiler/outlives.hpp#L69`, `include/logos/compiler/outlives.hpp#L91`

### `region.outlives.unconstrained-short` — Empty (elided) shorter side is satisfied by any region

If the shorter side of an outlives query is empty/elided, the constraint r: `<empty>` is vacuously satisfied for any longer region r.

Source: `include/logos/compiler/outlives.hpp#L67`

### `region.outlives.transitive-bfs` — Outlives is transitive over the explicit constraint graph

Given an explicit outlives graph of declared pairs (longer: shorter) parsed from `where 'long: 'short [+ 'mid ...]`, long: short holds if short is reachable from long by following declared outlives edges (transitive closure / BFS over the directed adjacency longer->shorter).

*Overlaps:* `region.outlives.transitive`

Source: `include/logos/compiler/outlives.hpp#L42-L50`, `include/logos/compiler/outlives.hpp#L72-L85`

### `region.outlives.permissive-elided-source` — Elided longer side treated as compatible at coercion sites; strict at borrow-return

An empty/elided longer (source) region is treated as satisfying any named outlives constraint at variance/subtype-coercion sites, deferring to call-site region inference (permissive mode). Strict-mode callers (the borrow-check return path) reject an elided source against a named target.

*Extraction note:* Permissive vs strict is a mode flag; default at general sites is permissive.

Source: `include/logos/compiler/outlives.hpp#L68`, `include/logos/compiler/outlives.hpp#L86-L92`

### `region.outlives.permissive-unmentioned-pair` — Two unmentioned named regions assumed compatible in permissive mode

In permissive mode, if two named non-static regions L and S neither equal nor reach one another and NEITHER appears anywhere in the explicit outlives graph, L: S is assumed to hold (region inference is expected to unify them). If either L or S appears in the graph (but no path connects them), L: S is rejected. In strict mode, an unestablished named constraint is always rejected.

> **Divergence** — Permissive default; Rust requires the outlives relation to be explicitly established (would reject).

Source: `include/logos/compiler/outlives.hpp#L86-L102`

### `region.outlives.reflexive` — Outlives is reflexive

The outlives relation is reflexive: for any region/lifetime r, r: r holds (every region outlives itself).

Source: `include/logos/compiler/outlives.hpp#L70`, `src/compiler/region_infer.cpp#L110-L111`

### `region.outlives.declared-clause-seed` — where-clause `'longer: 'shorter` imposes an outlives constraint

A declared bound `'longer: 'shorter` imposes the constraint region('longer) ⊇ region('shorter) (longer must contain everything shorter contains), seeded as a CFG-point-independent constraint. A clause naming a lifetime not declared on the function is ignored at this stage (already a sema error).

*Overlaps:* `region.outlives.clause-syntax`

Source: `src/compiler/region_infer.cpp#L82-L98`

### `region.outlives.static-longest` — 'static outlives every lifetime

'static outlives every lifetime: 'static: 'a holds for all 'a.

*Overlaps:* `region.outlives.static-is-top`

Source: `src/compiler/region_infer.cpp#L148-L150`, `src/compiler/region_infer.cpp#L159`

### `region.outlives.static-requires-declared` — Outliving 'static must be explicitly declared

A concrete lifetime 'a does not outlive 'static (i.e. 'a: 'static does not hold) unless an explicit bound `'a: 'static` is declared; such a declared edge is honored (through the same reachability walk used for all outlives clauses).

Source: `src/compiler/region_infer.cpp#L146-L156`, `src/compiler/region_infer.cpp#L163-L181`

### `region.outlives.transitive` — Outlives is transitive (reachability over declared clauses)

Outlives is the reflexive-transitive closure of the declared outlives clauses: 'a: 'b holds iff 'a == 'b, or 'b is reachable from 'a by following declared `'x: 'y` edges (including through 'static); unreachable pairs are conservatively rejected.

*Overlaps:* `region.outlives.transitive-bfs`

Source: `src/compiler/region_infer.cpp#L144`, `src/compiler/region_infer.cpp#L163-L184`

### `region.outlives.unconstrained-shorter-vacuous` — Outliving an unconstrained lifetime is vacuous

Any lifetime outlives an unconstrained (empty/anonymous) shorter lifetime; an unconstrained longer lifetime outlives nothing (strict mode).

*Extraction note:* Empty-string lifetime denotes the unconstrained/anonymous case; mapping to surface syntax inferred.

Source: `src/compiler/region_infer.cpp#L160-L161`

### `region.outlives.implied-from-references` — Implied outlives from nested references

For each `&'a T` (or `&'a mut T`) appearing in a parameter or return type, every reference lifetime `'b` (and struct/enum/zoned lifetime-arg) nested strictly inside its referent yields an implied bound `'b: 'a` (the inner must outlive the enclosing reference). Implied bounds are emitted only when both lifetimes are declared on the fn (or `'static`); template-internal generic lifetimes are not surfaced.

Source: `src/compiler/sema_decl.cpp#L38-L82`

### `region.outlives.static-always-known` — `'static` is an always-declared lifetime

The lifetime `'static` (spelled `'static` or `static`) is treated as declared in every scope; it is never an undeclared-lifetime error.

Source: `src/compiler/sema_decl.cpp#L33-L37`, `src/compiler/sema_decl.cpp#L115-L119`

### `region.outlives.undeclared-lifetime-error` — Outlives/bound lifetimes must be declared

Every lifetime name used in a function's outlives clause, or in a type-parameter's `T: 'lt` bound, must be a lifetime parameter declared on that function (or `'static`); use of an undeclared lifetime name is ill-formed.

*Overlaps:* `region.outlives.lifetime-must-be-declared`

Source: `src/compiler/sema_decl.cpp#L113-L134`

### `region.outlives.where-type-outlives` — where-clause type-outlives bounds attach to type params

A where-clause entry `T: 'lt` adds the lifetime bound `'lt` to the matching type parameter `T`; where-clause lifetime-outlives entries are merged into the function's outlives set.

*Overlaps:* `region.outlives.where-clause-type-param`

Source: `src/compiler/sema_decl.cpp#L83-L111`

### `region.outlives.lifetime-must-be-declared` — Outlives clauses reference only declared lifetimes

Every lifetime name appearing in a struct or enum outlives bound (header `<'a: 'b>`, where-clause, or type-param `T: 'a`) must be a declared lifetime parameter of that item, the implicit `'static`, or empty; an undeclared lifetime is a compile error. `'static`/`static` are always known.

*Overlaps:* `region.outlives.undeclared-lifetime-error`

Source: `src/compiler/sema_decl.cpp#L1270-L1293`, `src/compiler/sema_decl.cpp#L1480-L1503`

### `region.outlives.where-clause-type-param` — Where-clause type-outlives bounds augment a type parameter

A where-clause entry `T: 'a` (a TYPE_PARAM whose inner items include LIFETIME_PARAMs) attaches each lifetime as an outlives bound on the matching declared type parameter T; entries naming an unknown type parameter are ignored.

*Overlaps:* `region.outlives.where-type-outlives`

Source: `src/compiler/sema_decl.cpp#L1239-L1269`, `src/compiler/sema_decl.cpp#L1449-L1479`

### `region.outlives.clause-syntax` — Declared lifetime outlives bounds 'long: 'short

A `'long: 'short` outlives clause may appear in a fn/struct/enum/impl generic header or `where` clause; sema reads these as (long, short) lifetime-name pairs from `LIFETIME_PARAM` items carrying a non-empty bound list.

*Overlaps:* `region.outlives.declared-clause-seed`

Source: `src/compiler/sema_impl.hpp#L3259-L3266`

### `region.outlives.callee-bound-checked-at-call` — Callee `where 'a: 'b` bounds checked at the call site

At a call site, for each callee `where 'a: 'b` outlives bound, build a callee-lifetime→caller-lifetime substitution by walking (param_type, arg_type) pairs (through refs, struct/zoned-struct/enum lifetime+type args, tuples, slices/arrays, raw ptrs). If both 'a and 'b map to concrete caller lifetimes and differ, the caller's current outlives graph must already prove `caller_long: caller_short`, else error `call to '{callee}': caller does not satisfy callee's outlives bound ...`. If either lifetime is unmapped (internal to callee or elided at the call site), the bound is NOT enforced here — deferred to caller's region inference.

*See also:* `region.outlives.static-always-satisfies`, `region.outlives.struct-lit-bound-checked`

Source: `src/compiler/sema_impl.hpp#L3382-L3462`

### `region.outlives.static-always-satisfies` — 'static trivially satisfies any outlives bound

When checking a `where 'a: 'b` (or struct-declared) outlives bound, if the long lifetime 'a is `'static`, the bound is skipped unconditionally (never produces an error) — 'static always outlives everything.

*Overlaps:* `region.outlives.static-is-top`

Source: `src/compiler/sema_impl.hpp#L3442-L3443`, `src/compiler/sema_impl.hpp#L3538`

### `region.outlives.struct-lit-bound-checked` — Struct/tuple-struct literal outlives bounds checked against caller's graph

At a struct or tuple-struct literal `S { ... }` whose declaration carries `where 'a: 'b` clauses, build a lifetime-parameter substitution from (a) explicit lifetime type-args at the literal and (b) walking (declared field type, literal field value type) pairs. For each declared bound whose both sides map to distinct concrete caller lifetimes, the caller's outlives graph must prove the substituted relation, else error `struct literal '{S}': caller does not satisfy declared outlives bound ...`. Bounds are skipped if unmapped, 'static, or both sides equal after substitution.

*See also:* `region.outlives.callee-bound-checked-at-call`

Source: `src/compiler/sema_impl.hpp#L3468-L3553`

## Lifetime bounds and binders (`bounds`)

### `region.bounds.universal-lifetime-position` — Bound lifetime must align with a free impl lifetime param

When matching a bound's trait-arg lifetimes against an impl's, each non-empty non-'static bound lifetime must align with an impl lifetime that is a free impl-level parameter (not a region concretely pinned at the impl); a 'static (or empty) bound lifetime matches only an exactly-equal impl lifetime or a free impl param.

Source: `src/compiler/sema_collect.cpp#L1008-L1046`

### `region.bounds.impl-tie-injectivity` — Impl-tied lifetime slots require matching bound binders

Region matching walks Ref/MutRef pointees and Struct/Enum lifetime+type args recursively. If an impl uses the same lifetime in two trait-arg positions, the bound must use the same binder in those positions (reverse-injective). The forward direction may collapse distinct bound binders onto one impl lifetime (impl strictly more general).

Source: `src/compiler/sema_collect.cpp#L1031-L1082`

### `region.bounds.hrtb-outlives-unsat` — HRTB outlives between two bound binders is unsatisfiable

If an impl declares a where-clause outlives `'a: 'b` between two impl-side lifetime params that BOTH map to distinct bound binders under universal quantification, the constraint is unsatisfiable and the bound is rejected. Reflexive (same skolem) mappings are accepted.

Source: `src/compiler/sema_collect.cpp#L1083-L1100`

## Higher-ranked trait bounds (`hrtb`)

### `region.hrtb.impl-lifetime-must-be-universally-quantified` — HRTB bound satisfied only by impl-level (universal) lifetimes

An impl's trait-arg lifetime (`impl Trait<&'a T> for X`) satisfies a higher-ranked (`for<'a> …`) bound on the trait only when that lifetime is one of the impl's own declared lifetime params (`impl<'a, T> …` — universally quantified at the impl site); an impl that supplies a concrete region where the bound demands a universal lifetime is an HRTB satisfaction mismatch.

*Extraction note:* The enforcing bound-check code is outside this slice; this rule is derived from the field-declaration comments (B62) describing the data recorded and its intended use, not from the check site itself.

Source: `src/compiler/sema_impl.hpp#L2693-L2701`

## Lifetime elision (`elision`)

### `region.elision.method-result-borrows-self` — Elided &self -> &T (or borrow-carrying) ties result to receiver

is_self_borrowing(f) holds iff f's first parameter is a reference kind, f has NO explicit lifetime parameters, AND f's return type is a reference kind or a #[borrow_carrying] type. A method with explicit lifetime parameters may tie its result to a non-self argument instead, so it is classified NOT self-borrowing (avoids over-conservative borrowing of self).

Source: `src/compiler/borrow_check.cpp#L1539-L1556`

### `region.elision.operator-desugar-self-borrow` — Operator-desugared call self-borrowing by name agreement

result_borrows_self(call): if call.resolved_symbol resolves to a function, defer to is_self_borrowing on it. Otherwise (operator-desugared/trait calls — `v[i]`->Index, `*p`->Deref — carry an empty resolved_symbol), fall back to ALL functions sharing the call's unmangled method name: classified self-borrowing only if EVERY same-named method is self-borrowing; any disagreement or no match yields NOT self-borrowing (conservative).

Source: `src/compiler/borrow_check.cpp#L1558-L1575`

### `region.elision.ambiguous-output` — Output lifetime elision (E0106) ambiguity

When a return type structurally contains an unannotated reference (`&T`/`&mut T`, recursed through pointee/elem/tuple-element positions but NOT through generic type-arguments), Rust elision rule 2/3 must supply a unique source: this is flagged as ill-formed (E0106) exactly when the input side has >=2 structural reference positions (counted through pointee/elem/tuple-elements AND type-args) and there is no `&self`/`&mut self` receiver. Exactly one input lifetime, or a `&self`/`&mut self` receiver, elides successfully; the zero-input case (e.g. a `'static`-sourced return) is left unflagged, deferred to explicit annotation or the dangling-borrow check.

```logos
fn h(a:&i32, b:&i32) -> &i32  // error E0106
fn f(a:&i32) -> &i32  // ok (rule 2)
```

*Extraction note:* Zero-input elided-ref return (e.g. returning &'static) is not flagged here; deferred to a separate dangling-borrow check not visible in this unit.

Source: `src/compiler/sema_decl.cpp#L772-L836`

## Lifetime parameters (`lifetime`)

### `region.lifetime.param-fresh-region` — Each lifetime parameter denotes a distinct region

Each non-empty declared lifetime parameter ('a, 'b, ...) of a function denotes its own distinct region; references to the same lifetime name within the function resolve to that one region.

Source: `src/compiler/region_infer.cpp#L80-L81`

## Lifetime substitution (`lifetime-subst`)

### `region.lifetime-subst.method-param-pairing` — Lifetime substitution by structural pairing of method formals vs actuals

Lifetime parameters of a dispatched method are substituted by structurally walking each formal parameter type against its actual argument type (receiver vs param0, then arg[i] vs param[i+1]): for matching Ref/MutRef pairs the formal lifetime maps to the actual lifetime and the walk recurses into pointees; for matching nominal types (Struct/ZonedStruct/Enum) lifetime-args are paired positionally. First binding wins per formal lifetime.

*Overlaps:* `region.lifetime-subst.method-call-pairing`, `region.method.lifetime-subst`, `region.method-call.lifetime-subst`

Source: `src/compiler/sema_expr.cpp#L7637-L7669`

### `region.lifetime-subst.method-call-pairing` — Lifetime substitution built by structural pairing of method formals vs actuals

For a resolved trait/generic method call, a lifetime-substitution map is built by structurally walking the method's formal parameter types (self first, then each argument in order) against the actual receiver/argument types: when both sides are Ref/MutRef, the formal's lifetime name is bound to the actual's lifetime name (first binding wins, ties broken by first occurrence) and the walk recurses into the pointee; when both sides are the same Struct/ZonedStruct/Enum kind, their lifetime-argument lists are paired positionally and bound the same way.

*Overlaps:* `region.lifetime-subst.method-param-pairing`

Source: `src/compiler/sema_expr.cpp#L7684-L7716`

## Region subtyping and variance (`subtype`)

### `region.subtype.ref-covariant` — &'a T is covariant in lifetime and pointee

For shared references: `&'a T <: &'b U` iff 'a: 'b (covariant lifetime) and `T <: U` (covariant pointee).

*See also:* `region.subtype.mutref-invariant-pointee`

Source: `include/logos/compiler/subtype.hpp#L214-L219`

### `region.subtype.mutref-invariant-pointee` — &mut 'a T is covariant in lifetime, invariant in pointee

For mutable references: `&'a mut T <: &'b mut U` iff 'a: 'b (covariant lifetime) and `T == U` with lifetimes (invariant pointee). Hence &mut &'static T is NOT a subtype of &mut &'a T.

*See also:* `region.subtype.ref-covariant`

Source: `include/logos/compiler/subtype.hpp#L220-L225`, `include/logos/compiler/subtype.hpp#L9`

### `region.subtype.empty-lifetime-wildcard` — Empty lifetime acts as a wildcard in structural equality

In lifetime-aware structural equality, an empty (elided) lifetime on either side matches any lifetime (region inference resolves it). Otherwise lifetimes are equal iff syntactically identical, or mutually outliving ('a: 'b and 'b: 'a) which is treated as equality.

Source: `include/logos/compiler/subtype.hpp#L65-L72`, `include/logos/compiler/subtype.hpp#L49-L53`

### `region.subtype.variance-positions` — Per-position variance dictates the lifetime/type relation direction

At a position with variance v: Bivariant always holds; Covariant requires `sub <: sup` (lifetimes: 'sub: 'sup); Contravariant requires `sup <: sub` (lifetimes: 'sup: 'sub); Invariant requires equality (lifetime-aware structural equality for types; normalized-lifetime identity for lifetimes).

Source: `include/logos/compiler/subtype.hpp#L154-L183`

## Region constraint solving (`solve`)

### `region.solve.constraint-fixpoint` — Region contents are the least fixpoint of Contains/Outlives constraints

Each region's set of CFG points is the least solution satisfying: Contains(r, P) ⇒ P ∈ r; Outlives(longer, shorter) ⇒ points(shorter) ⊆ points(longer). Lifetimes propagate monotonically from shorter to longer.

Source: `src/compiler/region_infer.cpp#L187-L213`

## Control-flow graph construction (`cfg`)

### `region.cfg.loop-back-edge` — Loop bodies have a back-edge to the loop head

while/for/for-each loop bodies have control-flow back-edges to the loop head, and `loop` bodies back-edge to themselves; consequently a value live across a loop iteration is live at the loop head, extending borrow regions over the whole loop.

Source: `src/compiler/region_infer.cpp#L260-L302`

### `region.cfg.let-else-diverging` — let-else else-block diverges

The else-block of a let-else statement is a diverging branch with no control-flow edge to the code following the statement; bindings introduced by the let are not live in the else-block.

Source: `src/compiler/region_infer.cpp#L329-L338`

## Pattern bindings (`pat`)

### `region.pat.by-ref-binding-inherits-borrows` — By-reference match binding inherits scrutinee borrows

propagate_pat_sources: for a VariantData pattern matched against a scrutinee with tracked borrow-sources srcs, each sub-binding whose static type is a reference kind or a #[borrow_carrying] type inherits srcs as its own ref-borrow-sources (recorded at the match line), so the borrow cannot be smuggled past the referent's scope via the binding. A by-value binding (neither reference nor borrow-carrying) does not inherit — it copies out and carries no borrow.

Source: `src/compiler/borrow_check.cpp#L1514-L1536`

## Function-level rules (`fn`)

### `borrow.fn.scope-is-per-function` — Borrow checking is strictly per-function

Borrow/move/dangling/dropck/provenance state (VarState table, scope stack, ref-borrow-source/line maps, dangling map, dropck-source/line maps, Copy-typevar set, param-lifetime maps) is reset at the start of `check(fn)` for every function — no facts cross function bodies. A function with no body mirror (extern declaration / metaprog stub / materialized `from_binary_module`) is skipped entirely (returns before any block walk). Each parameter is declared as an initialized binding; a reference-typed parameter records its declared lifetime, and if it points to a lifetime-parameterized struct/enum, the pointee's explicit lifetime-argument list is also captured (even when empty) to support later return-lifetime validation.

Source: `src/compiler/borrow_check.cpp#L3206-L3285`

### `region.fn.lifetime-outlives-implied` — Fn lifetime_outlives derives from implied param/return bounds plus where-clause

A function's `lifetime_outlives` bound set is computed from (a) implied bounds carried by its parameter and return types, merged with (b) any explicit `where`-clause lifetime bounds; the where-clause lifetime bounds are additionally folded into the corresponding type-param's own `lifetime_outlives` field.

*Extraction note:* Only the declaration + doc-comment are in this slice; compute_fn_lifetime_outlives' implementation body is defined elsewhere.

Source: `src/compiler/sema_impl.hpp#L4376-L4388`

## Impl-level lifetime rules (`impl`)

### `region.impl.outlives-undeclared` — Impl outlives clause may only name declared lifetimes or 'static

Each lifetime name appearing in an impl-level outlives clause `'a: 'b` must be either a declared impl lifetime parameter or `'static`/`static`; otherwise it is a compile error: "impl {Trait} for {Target}: use of undeclared lifetime name '{name}' in outlives clause".

Source: `src/compiler/sema_decl.cpp#L1934-L1949`

### `region.impl.trait-arg-lifetime-erased` — Lifetime arguments at trait-argument position are not tracked for trait dispatch

A LIFETIME_PARAM occurring among an impl's trait type-arguments (`impl SomeTrait<'a, T> for X`) is skipped when positionally resolving trait type arguments: regions are not tracked structurally for trait selection/dispatch.

> **Divergence** — Logos does not use regions in trait selection/coherence; Rust's HRTB/lifetime args participate in trait-ref identity even though they are erased at codegen.

Source: `src/compiler/sema_decl.cpp#L2064-L2066`

## Generics and borrow checking (`generic`)

### `borrow.generic.exclusivity-only-pre-mono` — Generic templates borrow-check exclusivity only, deferring move checks

When borrow-checking a generic function template before monomorphization (`exclusivity_only_=true`), only borrow-exclusivity conflicts (write vs. active shared/mut borrow) are reported; move/use-after-move diagnostics are suppressed (imprecise over TypeVar values) and are fully checked on each monomorphized specialization.

*Extraction note:* Field declaration and rationale comment are in this slice; call sites that consult exclusivity_only_ to gate move-diagnostics are outside this slice.

Source: `src/compiler/borrow_check.cpp#L3198-L3204`

### `borrow.generic.copy-bound-is-copy-type` — A type parameter is move unless it carries an explicit Copy bound

In a generic body a bare type parameter `T` is move-classified for use-after-move tracking unless `T` carries an explicit trait bound literally named `Copy` (recorded per-function in `copy_tvs_`), in which case its values are Copy and not consumed on use.

*Overlaps:* `borrow.classify.type-param-move-unless-copy`

Source: `src/compiler/borrow_check.cpp#L3234-L3244`

## Pass structure (`pass`)

### `borrow.pass.generic-template-checked` — Generic fn bodies are borrow-checked even when never instantiated

A dedicated pre-monomorphization pass borrow-checks generic function bodies directly (exclusivity-only mode, no region inference, imprecise move tracking on TypeVars), so an uninstantiated generic is still checked. The post-mono pass checks concrete functions and specializations with full region inference. Functions loaded from a precompiled binary module and extern functions are skipped (already checked when their layer was built).

> **Divergence** — Rust-conformant (uninstantiated generics are still checked).

Source: `src/compiler/borrow_check.cpp#L3788-L3818`, `src/compiler/borrow_check.cpp#L3849-L3852`

### `borrow.pass.region-conflict-diag` — Region inference reports overlapping-borrow conflicts

Region inference runs before the lexical borrow check and shares the same declared `'a: 'b` outlives source; it reports a conflict when two borrows of the same target have overlapping live regions where at least one is mutable. The later borrow (by source line) is the offending one and the earlier is reported as the still-live borrow.

Source: `src/compiler/borrow_check.cpp#L3805-L3846`

## Diagnostics (`diag`)

### `borrow.diag.dedupe-across-mono` — Identical borrow diagnostics across instantiations are de-duplicated

A generic template and each of its monomorphizations report the same borrow error (same level/line/context/message, mono suffix stripped); such identical diagnostics are collapsed so the user sees one error rather than one per instantiation.

Source: `src/compiler/borrow_check.cpp#L3854-L3870`

