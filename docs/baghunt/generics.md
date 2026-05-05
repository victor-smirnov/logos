# Bug catalog: Generics & Bounds

**Group**: 7 — Generics & Bounds
**Grammar rules covered**: `type_param_list`, `type_param`, `lifetime_param`, `type_arg_list`, `bound_arg_list`, `bound_arg`, `trait_bound`, `super_list`, `where_clause`
**Reference doc**: [docs/language/reference/generics-traits.md](../language/reference/generics-traits.md)
**Implementation entry points**:
- [src/compiler/sema.cpp](../../src/compiler/sema.cpp) — `unify_types`, `match_type_sema`, `find_best_sema_struct_spec`, type-param substitution
- [src/compiler/sema_collect.cpp](../../src/compiler/sema_collect.cpp) — type-param collection, bound parsing
- [src/compiler/mono_subst.cpp](../../src/compiler/mono_subst.cpp) — `subst_type`
- [src/compiler/mono_clone.cpp](../../src/compiler/mono_clone.cpp) — `find_best_struct_spec`, `clone_struct_def`, `clone_fn`

**Hunt date**: 2026-05-04
**Repros**: `/tmp/baghunt/generics/`

## Bugs

### B-gn-01: Duplicate type parameter `<T, T>` silently accepted

**Severity**: P0 (silent shadowing)
**Status**: fixed-in-M0.1 (check_unique_names on type_params; tests/logos/fail/dup_type_param)
**Repro**: `B02/` —
```logos
struct Foo<T, T> { a: T, b: T }
fn main() -> i32 { return 0; }
```
**Observed**: Compiles cleanly.
**Expected**: "duplicate type parameter 'T'".
**Suspected root**: Type-param collection in [sema_collect.cpp](../../src/compiler/sema_collect.cpp) doesn't dedup-check. Same family as missing-uniqueness-check cluster (B-it-03/04/05, B-fn-02, B-ca-04).
**Tags**: `oversight:simple`, `tech-debt:missing-uniqueness-check`

### B-gn-02: Duplicate lifetime parameter `<'a, 'a>` silently accepted

**Severity**: P0 (silent shadowing)
**Status**: fixed-in-M0.1 (check_unique_names on lifetime_params; tests/logos/fail/dup_lifetime_param)
**Repro**: `B16/` —
```logos
fn helper<'a, 'a>(x: &'a i32, y: &'a i32) -> i32 { return 0; }
fn main() -> i32 { return 0; }
```
**Observed**: Compiles cleanly.
**Expected**: "duplicate lifetime parameter ''a'".
**Suspected root**: Lifetime-param collection lacks dup-check. Same family as B-gn-01.
**Tags**: `oversight:simple`, `tech-debt:missing-uniqueness-check`

### B-gn-03: Trait bound on missing trait silently accepted at definition

**Severity**: P1 diagnostic (deferred validation)
**Status**: fixed (check_trait_bounds_well_formed post-collect; emits at definition site)
**Repro**: `B04/` —
```logos
fn helper<T: NonExistentTrait>(x: T) -> T { return x; }
fn main() -> i32 { return 0; }
```
**Observed**: Compiles cleanly when `helper` is never instantiated. Error fires only when called: see G11 for the actual call which produces "type 'i32' does not implement trait 'NonExistentTrait'".
**Expected**: At definition site: "unknown trait 'NonExistentTrait' in bound on parameter T". Should error before any call attempts.
**Suspected root**: Bound trait names are looked up lazily — only at trait-satisfaction check time, not at registration. The registration path stores the bound name as a string and trusts it.
**Tags**: `tech-debt:lazy-validation`, `oversight:simple`

### B-gn-04: Trait bound with arity-mismatched type-args silently accepted

**Severity**: P0 (silent shadowing)
**Status**: fixed (check_trait_bounds_well_formed arity check)
**Repro**: `B20/` —
```logos
trait Foo<U> { fn x(self: *const Self, y: U) -> i32 { return 0; } }
fn helper<T: Foo<i32, i64>>(x: T) -> i32 { return 0; }
fn main() -> i32 { return 0; }
```
**Observed**: Compiles cleanly. The bound `Foo<i32, i64>` has two args but `Foo` declares only `U`.
**Expected**: "trait 'Foo' in bound takes 1 type-arg, got 2".
**Suspected root**: Same `missing-arity-check` cluster as B-ty-03/04/05 — bound-arg count not validated.
**Tags**: `tech-debt:missing-arity-check`

### B-gn-05: Type-param shadowing user struct produces misleading diagnostic

**Severity**: P1 diagnostic
**Status**: partial — defensive shadow-warning in collect_fn (struct/enum/trait); the `<Bar>` parser-collapse case still slips through (parser issue, not diagnostic)
**Repro**: `B13/` —
```logos
struct Bar { x: i32 }
fn helper<Bar>(x: Bar) -> Bar { return x; }
fn main() -> i32 { return helper::<i32>(5); }
```
**Observed**: `error: call to undefined function 'helper'`.
**Expected**: Either: (a) compile fine since type-params shadow types within their scope (Rust accepts this), OR (b) "type parameter 'Bar' shadows type 'Bar' from the surrounding scope". The current diagnostic — "undefined function" — is misleading; the function IS defined, but something about the shadowing breaks it.
**Suspected root**: Either monomorphization collides on the name `Bar` between the type-param and the struct, or the shadowing itself breaks fn name lookup. Worth deeper inspection in [src/compiler/mono_clone.cpp](../../src/compiler/mono_clone.cpp).
**Tags**: `tech-debt:misleading-diagnostic`, `tech-debt:name-collision-cascade`

### B-gn-06: Const-generic arity-mismatch reported as type-mismatch

**Severity**: P1 diagnostic
**Status**: fixed — primary diagnostic now says "expected 2 type arg(s), got 3"; cascading "type mismatch" follow-up is benign
**Repro**: `B17/` —
```logos
struct Foo<T, const N: usize> { arr: [T; N] }
fn main() -> i32 {
    let f: Foo<i32, 5, 7> = Foo::<i32, 5, 7> { arr: [0; 5] };
    return 0;
}
```
**Observed**: `error: let 'f': type mismatch — expected Foo<i32, {integer}, {integer}>, got Foo<i32, {integer}>`
**Expected**: "type 'Foo' takes 2 type-args (1 type, 1 const), got 3".
**Suspected root**: The arity error is detected (the "got" form has fewer args) but reported as a generic type-mismatch rather than a dedicated arity diagnostic. Mixing arity violations with type mismatches confuses the user.
**Tags**: `tech-debt:diagnostic-imprecise`

### B-gn-07: Unused type parameter — diagnostic at use-site only

**Severity**: P1 diagnostic
**Status**: fixed (post-collect check_unused_generics_in_funcs lint; bound-type-args also count as use)
**Repro**: `B19/` —
```logos
fn helper<T>(x: i32) -> i32 { return x; }
fn main() -> i32 { return helper(5); }
```
**Observed**: At call site: `error: 'helper': could not infer all type arguments — use explicit f::<T>(...) syntax`.
**Expected**: At definition site: warning "type parameter 'T' is unused; consider removing or using `_`". The use-site error is technically correct (T can't be inferred without info), but the root cause (T is unused in the signature) should be diagnosed at the definition.
**Suspected root**: No "phantom type-param" check at fn lowering. Compare with Rust's `unused_variables` lint — Logos has no equivalent.
**Tags**: `oversight:simple`, `design:incomplete`

### B-gn-08: Type-param-in-method shadows impl-type-param silently

**Severity**: P2 design (intentional shadowing or oversight?)
**Status**: fixed — the warning lives at the `is_specialization_fn` early-return inside `collect_fn` (where `impl_type_params_` is still populated; `lower_spec_fn` runs later in a different scope). When the method has a bare-IDENT type-param whose name appears in `impl_type_params_`, sema warns: "method's '{T}' shadows the impl-block's '{T}'; the method is silently treated as a specialisation on the impl's '{T}'. Rename one if that wasn't intended." The implicit-specialisation behaviour itself is preserved; the warning surfaces the footgun without breaking any existing pattern.
**Repro**: `B15/` —
```logos
struct Foo<T> { x: T }
impl<T> Foo<T> {
    fn helper<T>(self: *const Foo<T>, y: T) -> T { return y; }
    //         ^ which T binds where?
}
fn main() -> i32 { return 0; }
```
**Observed**: Compiles cleanly. The inner `T` shadows the outer `T` everywhere it's mentioned in the method, including `Foo<T>` (where presumably the user meant the outer impl's T).
**Expected**: At minimum a warning: "type parameter 'T' shadows the impl-block's 'T'; references to T below bind to the inner one". Potentially this is correct shadowing behavior, but the silent shadowing of the *receiver type's* T is a footgun.
**Suspected root**: Type-param scope-stack push/pop is correct (inner T shadows outer), but the user's likely intent (use outer T as receiver type-arg) is silently violated.
**Tags**: `design:incomplete`, `tech-debt:silent-shadowing`

### B-gn-09: Lifetime parameter declared but never used silently accepted

**Severity**: P2 design (consistency with B-gn-07)
**Status**: fixed — `check_unused_generics_in_funcs` extended with the lifetime branch (SemaFuncInfo carries `lifetime_params`; same use-collection via `collect_type_var_uses`).
**Repro**: `B18/` —
```logos
fn helper<'a>(x: i32) -> i32 { return x; }
fn main() -> i32 { return helper(5); }
```
**Observed**: Compiles cleanly with no warning.
**Expected**: Same as B-gn-07 — at least a warning. (Rust warns on unused lifetime params.)
**Suspected root**: No phantom-lifetime-param check at fn lowering.
**Tags**: `design:incomplete`

### B-gn-10: Trailing comma in type-param list rejected

**Severity**: P2 design (continuation of trailing-comma cluster)
**Status**: fixed-in-Sprint6.1 (type_param_list trailing COMMA?; tests/logos/pass/trailing_comma_lists)
**Repro**: `B03/` —
```logos
struct Foo<T, U,> { a: T, b: U }
```
**Observed**: `syntax error near 'struct' at line 2`.
**Expected**: Accept (consistency with `field_def_list`, etc.).
**Suspected root**: Same as B-fn-03/04/05 — missing `COMMA?` at end of `type_param_list` production.
**Tags**: `oversight:simple`, `tech-debt:grammar-inconsistency`

## Tag summary

| Tag | Count | Bugs |
|---|---|---|
| `oversight:simple` | 5 | B-gn-01, B-gn-02, B-gn-03, B-gn-07, B-gn-10 |
| `tech-debt:missing-uniqueness-check` | 2 | B-gn-01, B-gn-02 |
| `tech-debt:lazy-validation` | 1 | B-gn-03 |
| `tech-debt:missing-arity-check` | 1 | B-gn-04 |
| `tech-debt:misleading-diagnostic` | 1 | B-gn-05 |
| `tech-debt:name-collision-cascade` | 1 | B-gn-05 |
| `tech-debt:diagnostic-imprecise` | 1 | B-gn-06 |
| `tech-debt:silent-shadowing` | 1 | B-gn-08 |
| `tech-debt:grammar-inconsistency` | 1 | B-gn-10 |
| `design:incomplete` | 3 | B-gn-07, B-gn-08, B-gn-09 |

**Cluster preview (cumulative across groups 1-7)**:
- **missing-uniqueness-check** now at **8 bugs** (fields, variants, traits, params, consts, type-params, lifetime-params). The single architectural fix unblocks the most.
- **missing-arity-check** at **4 bugs** (B-ty-03/04/05, B-gn-04). Type-arg count validation absent at every generic instantiation site.
- **lazy-validation** (B-gn-03) is a new flavor: bound trait names checked only at trait-satisfaction time, not at definition. Should be eager.
- **trailing-comma** continues — now at **5 bugs** (B-fn-03/04/05, B-gn-10).

## Regression-confirmed (NOT bugs)

- **G01**: empty `<>` rejected.
- **G05**: bound that references self type-param works.
- **G06**: where clause works.
- **G08**: const N: usize works.
- **G09**: lifetime + bound works.
- **G10**: variadic `<T...>` works.
- **G11**: bound trait check fires correctly when method is called with wrong type.
- **G12**: i32 doesn't impl trait Foo correctly diagnosed at call site.
- **G14**: two lifetimes in fn sig work.
- **G21**: phantom type param works (Foo<i32> and Foo<i64> are distinct types per pkg-uid fix).
- **G22**: generic method works.
- **G23**: variadic + concrete param mix works.

## Notes for Phase 3

- The **missing-uniqueness-check** cluster is now huge (8 bugs across 7 groups). One pass through sema with a `dedup_names` helper plus 8 call sites (struct fields, enum variants, trait defs, fn params, consts, type-params, lifetime-params, possibly more) closes a significant chunk of P0 bugs.
- The **lazy-validation** tag (B-gn-03) is conceptually different from the other P0/P1 issues: bound names ARE checked, just at the wrong time. Architectural fix is "validate at definition, not at first use".
- B-gn-08 (silent shadowing of T in nested generic scopes) is a legitimate Rust-feature; whether to keep silent or warn depends on language-design preference. Phase 3 question.
