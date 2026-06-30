# ADR 0011 — Generic Writ schemas: VERIFIED implementation plan

**Status:** verified against branch `feat/writ-schemas` (head `29cf0a71`) on 2026-06-30.
De-speculated from six subsystem recipes; every file/function/line below was confirmed
against real code, and against a live `logosc` failure of the target program. Claims the
recipes got WRONG are flagged inline.

Companion to [`0011-writ-schemas.md`](0011-writ-schemas.md) (the *what*) and
[`0011-impl-plan.md`](0011-impl-plan.md) (non-generic *how*, DONE). This is the *how* for
**generic** schemas, as gated increments you can build+test one at a time.

Target: `schema Box<T: WritField> { val: T = 0 }`, used as `Box<i64>` / `Box<str>`, with
DISTINCT per-instantiation `schema_type_code` (`Box<i64>` ≠ `Box<str>`).

> **STATUS (2026-06-30): DONE for Sized type-args (G0–G4), ctest-gated.** `schema Wrap<T:
> WritField> { val: T }` works: construct/read/write `Wrap<i64>`/`Wrap<bool>` (incl. boxed
> wide values), the generic-fn CRUX (`fn get<T:WritField>(w:&Wrap<T>)->T { w.val }` →
> `T__from_wany`/`T__to_wany`, mono-retargeted), distinct per-instance codes (`view_checked`
> matches `Wrap<i64>` but rejects `Wrap<u64>`), and the `T: WritField` bound is enforced
> (`Wrap<Plain>` → clean diagnostic). Implemented exactly per G1–G4 below + use-site
> substitution. **G5 (generic `schema enum`) NOT done.** **`str` as a TYPE-ARG (`Wrap<str>`)
> NOT supported** — `str`=`[u8]` is unsized and trips the `Sized` bound (orthogonal generic
> constraint; `str` still works as a CONCRETE field type). Tests:
> pass/schema_generic, fail/schema_generic_not_writfield.

---

## §0. What is ALREADY DONE on this branch (recipes were STALE here)

Verified by reading the code — do NOT redo these:

- **Grammar:** `tools/peg_gen_cpp/grammars/logos.peg` lines **1196–1209** already carry
  `type_param_list?` in all four schema rules (`pub_schema_def`/`schema_def`/
  `pub_schema_enum_def`/`schema_enum_def`) with EXACTLY the positional captures the
  grammar recipe proposed (`TYPE_PARAMS: $3`/`$4`/`$5` etc.). **The entire grammar recipe
  is already implemented.** Do not touch the .peg.
- **`collect_schema` type-params:** `src/compiler/sema_collect.cpp:4036–4037` already calls
  `info.type_params = read_type_params(node); push_type_params(...)` and pops at
  **4105**. The sema-collect recipe's central claim ("currently LACKS type-param
  handling") is **FALSE for `collect_schema`**. `schema Box<T: WritField> { val: T }`
  PARSES and `val`'s type resolves to `TypeVar(T)` today.
- `read_type_params`/`push_type_params`/`pop_type_params` and `SemaStructInfo.type_params`
  all exist and work (mirror of `collect_struct`).

**Confirmed live failure** of the target program (`build/bin/logosc` on a `Box<i64>` test):
```
error: let 'b': type mismatch — expected Box<i64>, got Box        (make() drops type-args)
error: field read: receiver is not a struct or class (got T)      (ftype = unsubst TypeVar)
```
These two are the real gaps; everything below targets them.

## §1. The two real architectural facts (verified)

1. **Schema metadata is NOT carried through mono.** `mono_clone.cpp` has ZERO references to
   `is_schema`/`schema_fields`/`schema_type_code` (grepped). `clone_struct_def`
   (`mono_clone.cpp:5288`) clones only real fields `{m,z}` + methods. The
   `mono-generic-struct-instantiation` recipe's plan to persist schema metadata into the
   LIR struct mirror (new `lir_schema` keys + `clone_struct_def` substitution + StructView
   accessors) **is real work but UNNECESSARY** — the field desugar reads
   `SemaStructInfo.schema_fields[i].type` at **sema** time (`sema_expr.cpp:9692`,
   `sema_stmt.cpp:7266`), never from the LIR mirror. **Reject that recipe.** Substitute at
   the sema use-site instead (Inc 2/3).

2. **The desugar must recover the base template + substitute itself.** `lower_field_read`
   (`sema_expr.cpp:9682`) does `struct_name_from_type(recv_base_t)` which, for a type with
   type-args, returns the **mangled** name (`concrete_struct_name` → `"Box$G1$i64"`, see
   `sema.cpp:3290–3294`). `find_struct_by_name("Box$G1$i64")` finds NOTHING (instances
   aren't in `structs_`). Fix: look up by **bare** `TypeRef(recv_base_t).struct_name()`
   ("Box"), then `subst_type_sema(schema_fields[i].type, {T→i64})` using the receiver's
   `type_args()` zipped against the template's `type_params`.

## §2. THE CRUX — what to emit for a TypeVar field type

`writfield_type_name` (`sema_expr.cpp:9323`) is a Kind→name switch that returns `""` for a
`TypeVar`. Today's concrete desugar emits a fully-resolved free call (e.g.
`i64__from_wany(wany)` / `i64__to_wany(val,z)`).

**For a field whose substituted type is STILL a `TypeVar(T)`** (i.e. the read/write happens
inside a generic body where `T` is unbound — e.g. a method of `Box<T>`, or
`fn f<T>(b: Box<T>){ b.val }`), emit a **plain `ECall` with the type-param name as the
callee prefix**:

```cpp
// READ:  builder().call("T__from_wany", /*type_args=*/{}, {anyval}, ftype);
// WRITE: builder().call("T__to_wany",   /*type_args=*/{}, {val, z}, wany);   // ftype kept as TypeVar
```
where `"T"` is `TypeRef(ftype).type_var_name()`.

**Mechanism (verified, cite `mono_clone.cpp:2909–2967`, case `C::Call`):** mono splits the
callee at the first `__`, takes the prefix (`"T"`), looks it up in the active substitution
map `s` (`auto it = s.find(prefix)`), and if bound to a concrete type computes
`cname` (`type_str(t)` → `"i64"`; the special case `if (cname == "&[u8]") cname = "str";` at
**2961** handles `str`/`Slice<u8>`), then **rewrites** `nc.callee = cname + callee_body.substr(sep)`
→ `"i64__from_wany"`. The trait-impl methods register under exactly those symbols
(`impl WritField for i64 { fn from_wany... }` → `i64__from_wany`, confirmed in
`stdlib/lang/writ/wmap.logos:435`).

**PROVEN PRECEDENT:** `lower_typaram_static_method` (`sema_expr.cpp:13284–13342`) does this
verbatim — it ends with `finish_generic_call(cname + "__" + mname, synth, ...)` where
`cname` is the bare type-param name; `finish_generic_call` emits `builder().call(callee, ...)`
(`sema_expr.cpp:4397`). This is the LIR node + retarget path we reuse.

> **Recipe correction:** the `mono-aware-field-call` recipe claimed we must put
> `[TypeVar(T)]` in `ECall.type_args` and that "mono substitutes type_args and keeps the
> callee verbatim". **WRONG** on both counts. The retarget keys off the **callee prefix**
> looked up in the subst map, NOT type_args; `from_wany`/`to_wany` are non-generic in their
> own right so type_args should be `{}`. (Carrying a stray `[T]` is harmless after subst but
> pointless and risks the `__g__`/`__f__` template-suffix path at 2980 — leave it empty.)

**For a field whose substituted type is CONCRETE** (the common `Box<i64>` use-site, where the
receiver carries `[i64]` and we substitute before desugaring): `writfield_type_name` returns
`"i64"` and the EXISTING concrete path emits `i64__from_wany` directly. No mono involvement
needed. **This is the primary path the test exercises.**

---

## §3. Gated increments (build + test each in order)

> Build: `cd build && cmake --build . -j12`. Loop-gate with L2; full ctest only after the
> group (`bash ../tests/logos/ctest-summary.sh`). New tests go in `tests/logos/pass/`.

### Increment G1 — `make`/`view`/`view_checked` return the INSTANTIATED type
**Files/anchors:** `src/compiler/sema_expr.cpp`, `try_schema_method` (**9194**), specifically
`view_t` at **9207** and the `wrap` closure at **9211–9216**.
**Change:** `st = type_args[0]` is the turbofish type (`Box<i64>`, `Kind::Struct`,
`struct_name()=="Box"`, `type_args()==[i64]`). Line 9199 already binds the TEMPLATE ssi via
the BARE name (✓ — recipe's "line 9198 rejects Box<i64>" is WRONG; it passes). Replace
`view_t = make_struct_type(sname, spkg)` with:
```cpp
TypeRef view_t = TypeRef(st).type_args().empty()
    ? make_struct_type(sname, spkg)
    : make_generic_struct(sname, std::vector<TypeRef>(TypeRef(st).type_args().begin(),
                                                       TypeRef(st).type_args().end()), {}, spkg);
```
`wrap`'s `struct_lit(sname, …, view_t)` keeps the bare NAME but now the TYPE carries args;
mono's StructLit/`collect_type_for_structs` (`mono_impl.hpp:1115`) instantiates `Box$G1$i64`
(layout = `{m,z}`, T-independent). `make_generic_struct` exists (`sema_impl.hpp:299`).
**Mirror:** every other `make_generic_struct("Vec",{elem})` call site.
**Test:** `let b: Box<i64> = h.make::<Box<i64>>();` now type-checks (the `expected Box<i64>,
got Box` error disappears). Field read still fails — that's G2.
**RISK:** `struct_lit` passes bare name "Box" with type `Box<i64>`; mlir-gen must resolve
layout via the type not the name. Verify the emitted instance struct decl name matches what
StructLit lowering expects (other generic struct-lits work this way, so low risk — but
confirm with `-S`/objdump if a "no such struct Box" surfaces).

### Increment G2 — field READ substitutes the field type from the receiver's args
**Files/anchors:** `src/compiler/sema_expr.cpp`, `lower_field_read` schema branch **9681–9706**.
**Change:** after finding the template `sch_si` by the **bare** name
(`TypeRef(recv_base_t).struct_name()`, NOT `struct_name_from_type` which mangles), build a
`SemaSubst` from `sch_si->type_params[i].name → TypeRef(recv_base_t).type_args()[i]` and
`ftype = subst_type_sema(sch_si->schema_fields[found].type, subst)` before the
`schema_wany_to_typed` call. (`subst_type_sema` = `sema_impl.hpp:3353`; `SemaSubst` =
`StrMap<TypeRef>` = `sema_impl.hpp:3351`.) Then in `schema_wany_to_typed` (**9340**) add, BEFORE
the `writfield_type_name` block (9366), a TypeVar arm that emits the CRUX call
`builder().call(tvname + "__from_wany", {}, {anyval}, ftype)`.
**Construct to mirror:** the CONCRETE path already in `schema_wany_to_typed` (9366–9376) +
the TypeVar-defer precedent `type_code_of` at `sema_expr.cpp:4716–4718`.
**Test:** `tests/logos/pass/schema_generic_read.logos`: `schema Box<T: WritField>{val:T=0}`;
`let b = h.make::<Box<i64>>(); if b.val != 0i64 {return 1}` → exit 0. (The "receiver is not a
struct (got T)" error disappears.)
**RISK:** the read in a CONCRETE use-site must substitute to `i64` so the concrete path runs;
if substitution is skipped, the TypeVar arm fires and mono won't retarget at a non-generic
site (no `T` in subst map) → wrong/abort. Substitution at the use-site is the safeguard.

### Increment G3 — field WRITE substitutes the field type
**Files/anchors:** `src/compiler/sema_stmt.cpp`, `try_schema_field_write` **7241–7322**;
field type read at **7266**, `writfield_type_name` use at **7287–7307**.
**Change:** same as G2 — look up template by bare `TypeRef(base).struct_name()`, build subst
from `ssi->type_params` × `TypeRef(base).type_args()`, `ftype = subst_type_sema(...)`. Add a
TypeVar arm before 7287 emitting `builder().call(tvname + "__to_wany", {}, {val, z}, wany)`
(z is the view-carried allocator, already extracted at 7301–7303).
**Construct to mirror:** the concrete `to_wany` path at 7294–7307.
**Test:** extend G2's test with `b.val = 42i64; if b.val != 42i64 {return 2}`.
**RISK:** the type-compat check at 7269–7270 compares `expr_type(val)` against `ftype`; after
subst, `ftype=i64` ✓. If subst is omitted, `ftype=T` and the check spuriously passes/fails.

### Increment G4 — DISTINCT per-instantiation `schema_type_code`
**Files/anchors:** `src/compiler/sema_expr.cpp`, `try_schema_method` make branch **9248** and
view_checked branch **9276** (both currently emit `ssi->schema_type_code`, the SINGLE template
code).
**Change:** when `!ssi->type_params.empty() && !st.type_args().empty()`, compute a distinct
code. Reuse the PROVEN sema pattern (cite `sema_expr.cpp:4705–4708`):
```cpp
uint64_t code = ssi->schema_type_code;
if (!ssi->type_params.empty() && !TypeRef(st).type_args().empty()) {
    std::string canon = std::string(spkg) + "::" + concrete_struct_name(st);  // "Box$G1$i64"
    uint64_t variant = type_hash_56bit(type_hash_23(canon)) & writ::schema::VARIANT_MASK;
    code = (ssi->schema_type_code & writ::schema::CATEGORY_MASK) | variant;
}
```
`type_hash_23`/`type_hash_56bit` already used in this file (4706–4707); `CATEGORY_MASK`/
`VARIANT_MASK` in `include/logos/writ/schema_codes.hpp` (verified: 16-bit cat / 48-bit
variant). Use `code` at both 9248 and 9276 so make() stamps and view_checked() compares the
SAME per-instance code. **No mono hook needed** — both sites see the concrete `Box<i64>` turbofish at sema.
**Test:** `tests/logos/pass/schema_generic_view_checked.logos` — build a TOM stamped with
`Box<i64>`'s code, `view_checked::<Box<i64>>()` is `Some`, `view_checked::<Box<str>>()` on the
same TOM is `None`. (Mirror `schema_view_checked.logos`.)
**RISK/UNKNOWN:** `concrete_struct_name` applies `type_module_suffix(pkg)` (`sema.cpp:1421`);
the `canon` string above prepends `spkg` AND the suffix is already inside the mangled name —
pick ONE canonicalization and use it identically at make and view_checked (they're the same
call site here, so internally consistent; cross-TU consistency is moot because the code is a
local hash). Stable across compiles (SHA-256). Birthday-collision at 48 bits is negligible.

### Increment G5 — generic `schema enum` (OPTIONAL / lower priority)
**Files/anchors:** `src/compiler/sema_collect.cpp`, `collect_schema_enum` **4112–4170**.
**Change:** unlike `collect_schema`, this function does **NOT** call
`read_type_params`/`push_type_params`/`pop_type_params` (verified — the sema-collect recipe is
CORRECT only here). Add them mirroring `collect_schema` (4036–4037, 4105) so variant types
like `Ok(Box<T>)` resolve `T`. Then mirror G4's per-instance code for the enum's
category-keyed match (`sema_stmt.cpp:8340–8395`).
**Test:** `schema enum E<T> { A(Box<T>) }` parses + matches.
**RISK:** schema-enum match reads variant `schema_type_code` from each arm's `SemaStructInfo`
(`sema_stmt.cpp:8395`); generic variants need per-instance codes there too — defer unless
needed.

---

## §4. Test corpus to add
- `pass/schema_generic_read.logos` (G2)
- `pass/schema_generic_write.logos` (G3, extends G2)
- `pass/schema_generic_view_checked.logos` (G4 — the Box<i64> ≠ Box<str> safety check)
- `pass/schema_generic_in_fn.logos` (CRUX coverage: `fn f<T: WritField>(b: Box<T>) -> T { b.val }`
  forces the TypeVar arm + mono retarget; instantiate at `f::<i64>` and `f::<str>`)
- `fail/schema_generic_not_writfield.logos` (`Box<SomeStruct>` where SomeStruct: !WritField —
  the bound is checked at mono/impl-resolution, expect a clean diagnostic)

## §5. Build/validate
Per MEMORY: edit → `cd build && cmake --build . -j12` → L2 each commit; full ctest
(`bash ../tests/logos/ctest-summary.sh`) after the group (stdlib `wmap.logos` is untouched, so
no `LOGOS_LIB_OPT` rebuild concern unless a new WritField impl is added).
