# Cross-package impl-method monomorphization miscompile

**Status: FIXED 2026-06-19.** `Mono::enqueue_if_needed` early struct-method
path now recovers the impl-level type-param names from the method's
`impl_target_pattern` (via the new `collect_pattern_typevars`) and binds the
call's `type_args` to them by name, when the method carries no own
`type_params`. Repro `rewrap`/`pin_box`/`adv1` pass; L4 5733/5733, zero
regressions; the stock-20 clean build of the full stdlib (with `Pin<Box>` in
`logos.mem.boxed`) compiles and the pin tests pass. Fix in
`src/compiler/mono_scan.cpp` + `src/compiler/mono_impl.hpp`. Original
diagnosis kept below.

## Symptom

A generic method defined by an `impl<T> Foo<...T...>` block that lives in a
DIFFERENT package from the struct `Foo` is monomorphized into an empty
`llvm.unreachable` stub → call sites segfault (or read garbage).

Surfaced by the Rust-faithful Pin refactor: moving `Pin<Box<T>>` impls from
`logos.lang.pin` to `logos.mem.boxed` (so `lang` no longer depends on `mem`)
made those impls *cross-package* (on `Pin`, which is from `logos.lang.pin`).
`Pin<Box<T>>::as_ref` / `as_mut` / `Deref::deref` then segfault.

## Minimal repro (no Box, single file)

```logos
package pn; use logos.lang.pin;
impl<T> Pin<&T> {
    pub fn rewrap(self: Pin<&T>) -> Pin<&T> {
        let r: &T = self.get_ref();
        return unsafe { Pin::new_unchecked(r) };
    }
}
fn main() -> i32 {
    let x = 7i64;
    let p: Pin<&i64> = unsafe { Pin::new_unchecked(&x) };
    let q = p.rewrap();           // segfault
    return *q.get_ref() as i32;
}
```

Controls that all PASS (isolate the trigger):
- same call from a free `fn wrap_it<T>(r:&T)->Pin<&T>` — OK.
- a `Pin::new_unchecked` for a CONCRETE type (non-generic caller) — OK.
- a user newtype `Wrap<P>` with the same shape (generic ctor + `impl<T> Wrap<&T>`
  + conditional `Copy`) — OK.
- `get_ref` (same `impl<T> Pin<&T>`, but in the SAME package as `Pin`) — OK.

So the trigger is precisely: **a generic method on an impl whose receiver
struct is foreign (different package), reached via the generic-fn worklist
rather than struct-template instantiation.**

## Root cause (traced through mono)

Emitted MLIR for the instance:

```
func.func @pn.Pin__rewrap__g__Pin$G1$ref_T__i64(%arg0: !llvm.ptr) -> !llvm.ptr {
    %0 = llvm.alloca ... x !llvm.ptr
    llvm.unreachable          // body never lowered
}
```

Instrumented `instantiate_fn`:

```
mangled = pn.Pin__rewrap__g__Pin$G1$ref_T__i64
tmpl    = pn.Pin__rewrap__g__Pin$G1$ref_T   tmpl_tparams = 0   tmpl_body_stmts = 4
```

- The method is NOT in `in_.functions` (it is a *method*, lives in an impl's
  method list, not a free fn).
- The worklist enqueues it as `item.tmpl` (the method `LFunction`) with
  `type_params = 0` — the method carries no own type params; `T` is the
  IMPL's param, supplied externally.
- `item.subst` does NOT bind `T → i64`. So `clone_fn` substitutes nothing,
  the body keeps `T`, the return type stays `Pin<&T>` (unresolved) → mlir-gen
  renders it as a bare `!llvm.ptr` and emits `unreachable`.

Why same-package works: same-package methods are instantiated through
`Mono::instantiate_struct_templates` → `clone_struct_def`, which at
`mono_clone.cpp:~5551` does
`match_type(struct_t.type_args[i], spec->spec_patterns[i], subst)` — i.e. it
binds the impl param `T` by matching the concrete receiver's type-args against
the impl target pattern (`Pin<&i64>` vs `Pin<&T>` ⟹ `T=i64`). Cross-package
impl methods are NOT attached to the struct's template method set, so they
miss this matching entirely.

Also note (answers "why no package in the mangle"): `concrete_struct_name`
deliberately returns the BARE struct name; the package is prepended by
`SemaChecker::function_symbol_name` (sema.cpp:1466) as `<impl-pkg>.<base>`.
For a cross-package impl this yields `<defining-pkg>.<bare type>__m` plus an
unsubstituted self-type in the `__g__` overload suffix
(`Pin$G1$ref_T`) — neither the type's own package nor `T=i64`.

## Fix direction

Make cross-package impl methods on a generic receiver bind the impl's
type params from the concrete receiver, the same way the struct-template path
does. Two candidate approaches:

1. **Attach them to struct-template instantiation.** During collection, gather
   methods from ALL impls of a struct (including cross-package / cross-module)
   into the struct's instantiable method set, so `clone_struct_def`'s
   `match_type` binds `T`. Cross-MODULE (`--emit-module`) makes this harder —
   the mem module's impl methods must be visible to the consumer's `Pin`
   template.
2. **Bind at the worklist enqueue.** Where a call to such a method is enqueued
   (mono_scan), compute `subst` by `match_type`-ing the concrete receiver's
   type-args against the impl target pattern before pushing the worklist item
   — mirroring `clone_struct_def`. Smaller blast radius; preferred first try.

Gate any fix on full L4 (the multi-impl / `__g__` mangle path is a known
minefield — see memory `ref_multi_impl_selection`). Re-enable the Pin<Box>
mem refactor (stdlib/mem/boxed/pin_box.logos) and confirm pin_box / adv1 /
pin_get_mut tests pass on BOTH jenny and the stock-20 clean build.

## Related

- The Pin refactor itself: `stdlib/lang/pin/pin.logos` (generic
  `impl<P> Pin<P>` core accessors), `stdlib/mem/boxed/pin_box.logos`.
- Sibling already-fixed class (enum methods skipping `scan_fn`): commit
  `3a5432d2` (memory `project_audit_v2_fixes` FINDING #2).
