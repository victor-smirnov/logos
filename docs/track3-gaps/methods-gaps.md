# Method-dispatch gaps surfaced by Track 3 imports

| ID | Surface | Gap | Surfaced by | Repro |
|---|---|---|---|---|
| M7-mt-01 | Method name `new` inside a `trait { ... }` body | `fn new(self: &Self) -> bool;` inside a trait fails to parse ("syntax error near 'fn'"). Renaming to e.g. `mk` works. `new` as a free fn / inside an `impl` block parses fine. Looks like a grammar precedence issue with `new` being treated as a token in trait-body context. | `trait-method-resolution-7575` | as above |
| M7-mt-02 | Trait method default bodies | Logos requires trait method declarations (no body); `trait T { fn f(&self) {} }` rejected. Closing this is a small grammar fix. | `trait-method-resolution-7575` (rewritten to put bodies in impls) | `fn f(&self) {}` inside `trait { … }` |
| M7-mt-03 | Generic method-dispatch via `Sized` bound | `impl<T: Sized> A for *const T` chained against `impl<T> B for *const [T]` — `Sized` not in Logos. | `method-two-traits-distinguished-via-where-clause` (not imported) | n/a |
| M7-mt-04 | UFCS-style explicit method call `Foo::bar(&self)` | Not yet tested — most rustc tests using it sit on other gaps too. | (future) | n/a |
