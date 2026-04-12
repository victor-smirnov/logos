# Rust Feature Status in Logos Grammar
# Source: logos.peg vs rust_feature_inventory.md
# Legend: да / нет / частично (X%) / н/д (not applicable — намеренно не реализуется)

---

## Modules & Use

# Модули (`mod`)
**н/д** — модули намеренно отложены; есть только `package` и `use`.

# Extern crate declarations
**н/д** — Rust-специфика crate-системы.

# Use declarations
**частично (40%)** — есть `use IDENT.IDENT;` и `use IDENT;`, только двухуровневые.

# `as` rename в use
**нет**

# Brace import syntax (`use foo::{a, b}`)
**нет**

# `self` imports
**нет**

# Glob imports (`use foo::*`)
**нет**

# Underscore imports (`use ... as _`)
**нет**

---

## Functions

# Functions (`fn`)
**да**

# Generic functions
**да** — `type_param_list` поддерживается.

# Const functions (`const fn`)
**да** — `KW_CONST KW_FN` в `fn_def`.

# Async functions (`async fn`, Rust 2018)
**н/д** — async намеренно отложено.

# Unsafe functions (`unsafe fn`)
**да**

# Extern functions (`extern "ABI" fn`)
**частично (60%)** — `extern fn ...;` есть, но без строки ABI.

# Variadic functions
**да** — `...` в extern и параметрах.

# Anonymous parameters (до 2018)
**нет**

# Named function parameter patterns
**частично (30%)** — параметры именованные (`IDENT: type`), но не паттерны.

# Function attributes
**частично (50%)** — есть `annotation` (`#[...]`), но только на уровне items.

---

## Types & Aliases

# Type aliases (`type`)
**да** — `type_alias`.

# Generic type aliases
**да** — `type Foo<T> = Bar<T>;` с подстановкой на call site.

---

## Structs

# Structs (`struct`)
**да**

# Tuple structs
**нет** — только именованные поля.

# Unit structs
**нет** — грамматика требует `{ }`, `struct S;` невозможен.

---

## Enums

# Enums (`enum`)
**да**

# Enum discriminants
**да** — `IDENT = INTEGER`.

# Zero-variant enums
**да** — `variant_list?` опционален.

# Discriminant access (`mem::discriminant`, `as`)
**н/д** — библиотечная функция; `as` есть.

---

## Unions

# Unions (`union`)
**нет**

# Pattern matching on unions
**нет**

---

## Constants & Statics

# Constants (`const`)
**да** — `const_def`.

# Unnamed constants (`const _`)
**нет**

# Constants with destructors
**н/д** — семантика.

# Static items (`static`)
**нет** — `KW_STATIC` используется только для методов, не модульного static.

# Mutable statics (`static mut`)
**нет**

# Safe/unsafe statics in extern blocks (2024)
**н/д**

---

## Traits & Impls

# Traits (`trait`)
**да**

# Trait bounds
**да** — `trait_bound`.

# Generic traits
**да**

# Supertraits
**да** — `trait Foo: Bar + Baz` синтаксис и проверка impl completeness реализованы.

# Dyn compatibility (object-safety)
**н/д** — семантическое правило.

# Unsafe traits (`unsafe trait`)
**да** — `unsafe trait` и `unsafe impl` с проверкой пarity.

# Trait methods (default methods)
**да** — `trait_method` допускает тело.

# Associated functions and methods
**да**

# Associated types
**да** — `ASSOC_TYPE_DEF`/`ASSOC_TYPE_IMPL`/`ASSOC_TYPE_REF`.

# Associated constants
**да** — `const NAME: T;` в трейте, `const NAME: T = expr;` в impl, проверка полноты.

# Generic Associated Types (GATs)
**да** — `type Item<T>;` в трейте, `type Item<T> = Concrete;` в impl, `T::Item<i32>` в типах.

# Required where clauses on GATs
**нет**

# Inherent impls
**да** — `impl Type { ... }`.

# Trait impls
**да**

# Trait coherence / orphan rules
**н/д** — семантика.

# Generic implementations
**да** — `KW_IMPL type_param_list ...`.

# Blanket impls
**частично (50%)** — грамматически через generic impl; отдельного синтаксиса не нужно.

---

## Generics

# Generic parameters (type, lifetime, const)
**да** — все три: `TYPE_PARAM`, `LIFETIME_PARAM`, `CONST_PARAM`.

# Const generics
**да** — `KW_CONST IDENT COLON type_ref` в `type_param`.

# Where clauses
**да** — `where_clause`.

---

## Extern blocks & FFI

# External blocks (`extern { ... }`)
**нет** — только `extern fn` как отдельный item.

# Unsafe extern blocks (Rust 2024)
**н/д**

# `link` attribute
**н/д**

# `link` verbatim modifier
**н/д**

# `link_name` attribute
**н/д**

# `link_ordinal` attribute
**н/д**

# `no_link` attribute
**н/д**

# ABI specifications
**нет** — `extern fn` без строки ABI.

---

## Visibility

# Visibility (`pub`, `pub(crate)`, `pub(in path)`, `pub(super)`, `pub(self)`)
**частично (30%)** — только `pub`, без квалификаторов.

# Modules via file (`path` attribute)
**н/д**

# `mod.rs` vs `name.rs`
**н/д**

---

## Patterns

# Patterns
**да**

# Literal patterns
**частично (50%)** — integer, negative integer, bool. Нет char/string/float.

# Identifier patterns (incl. `ref`, `mut`, `@`)
**частично (20%)** — только простой ident-binding; нет `ref`, `mut`, `@`.

# Default binding modes (match ergonomics)
**н/д** — семантика.

# Binding mode limitations (2024)
**н/д**

# Wildcard pattern (`_`)
**частично** — `_` через IDENT; специального токена нет.

# Rest pattern (`..`)
**нет**

# Reference patterns (`&`, `&mut`)
**нет**

# Struct patterns
**нет**

# Tuple struct patterns
**нет**

# Tuple patterns
**да** — `PAT_TUPLE`.

# Grouped patterns
**нет**

# Slice patterns
**нет**

# Path patterns
**да** — `PAT_VARIANT`/`PAT_VARIANT_DATA`.

# Or-patterns (`a | b`)
**да** — `PAT_OR`.

# Range patterns (`a..=b`, `a..b`)
**нет**

# Obsolete range pattern (`...`)
**н/д**

# Refutable vs irrefutable patterns
**н/д** — семантика.

---

## Trait Bounds & Lifetimes

# Trait bounds (`T: Trait`)
**да**

# `?Sized`
**нет**

# Lifetime bounds (`'a: 'b`, `T: 'a`)
**нет**

# Higher-ranked trait bounds (HRTB, `for<'a>`)
**нет**

# Implied bounds
**н/д**

# Use bounds (`use<...>`)
**нет**

# Lifetime elision (functions)
**н/д** — семантика.

# Default trait object lifetimes
**н/д**

# `const`/`static` lifetime elision
**н/д**

# Subtyping (lifetime variance)
**н/д**

---

## Type Coercions (all semantic)

# Type coercions
**н/д**

# Deref coercion
**н/д**

# Unsized coercions
**н/д**

# Pointer coercions
**н/д**

# Non-capturing closure → fn pointer coercion
**н/д**

# Least upper bound coercions
**н/д**

---

## Special Types & Traits (library)

# Special type `Box<T>`
**н/д**

# Special type `Rc<T>`
**н/д**

# Special type `Arc<T>`
**н/д**

# Special type `Pin<P>`
**н/д**

# `UnsafeCell<T>`
**н/д**

# `PhantomData<T>`
**н/д**

# Operator traits (std::ops)
**н/д**

# `Deref`/`DerefMut`
**н/д**

# `Drop`
**н/д** — есть `delete`-statement.

# `Copy`
**н/д**

# `Clone`
**н/д**

# `Send`, `Sync`
**н/д**

# `Termination`
**н/д**

# Auto traits
**н/д**

# `Sized`
**н/д**

---

## Const Evaluation (semantic)

# Constant evaluation
**н/д**

# Const contexts
**н/д**

# Constant expressions
**н/д**

# Const blocks (`const { ... }`)
**нет**

# Const promotion
**н/д**

# Temporary lifetime extension
**н/д**

---

## Destructors & Drop (semantic)

# Destructors (drop glue)
**н/д**

# Drop scopes
**н/д**

# Temporary scopes (Rust 2024 narrowing)
**н/д**

# `mem::forget` / `ManuallyDrop`
**н/д**

---

## Tokens & Lexer

# Tokens
**да** — секция `%tokens`.

# Literals (integer, float, bool, char, string, byte, byte-string)
**частично (40%)** — integer, float, bool, string, raw-string. Нет char, byte, byte-string.

# C string literals (`c""`)
**нет**

# Raw C string literals (`cr""`)
**нет**

# Raw string literals (`r"..."`)
**частично (50%)** — `r"..."` есть, но без `r#"..."#`.

# Byte string literals (`b""`)
**нет**

# Raw byte string literals
**нет**

# Escape sequences
**частично (20%)** — `\\.` допускает любой escape, без `\x`/`\u{}`-валидации.

# Number literal suffixes
**да** — i8/i16/.../usize, f32/f64.

# Lifetimes / loop labels tokens
**да** — `LIFETIME = /'[a-z]...`.

# Raw lifetimes (`'r#lt`)
**нет**

# Reserved prefixes (Rust 2021)
**н/д**

# Reserved guards (Rust 2024)
**н/д**

# Punctuation / Delimiters
**да**

# Keywords (strict, reserved, weak)
**частично (70%)** — есть keywords; классификация strict/weak/reserved отсутствует.

# Raw identifiers (`r#keyword`)
**нет**

# Comments (line, block, doc, inner doc)
**частично (50%)** — line + block в `%skip`; doc/inner doc не отличаются.

# Shebang
**нет**

---

## Statements

# Statements
**да**

# `let` statements
**да**

# `let ... else`
**да** — `LET_ELSE`.

# Item declarations inside blocks
**нет** — `stmt` не включает items.

# Expression statements
**да**

---

## Expressions

# Block expressions
**да** — `block` используется и как выражение.

# Async blocks (`async { }`)
**н/д**

# Unsafe blocks (`unsafe { }`)
**да** — `unsafe_block`.

# Labeled block expressions
**нет** — только labeled loops.

# Literal expressions
**да**

# Path expressions
**частично (60%)** — `IDENT::IDENT` и `IDENT` есть; многоуровневые пути — нет.

# If expressions
**да**

# If let expressions
**да** — `KW_IF KW_LET pattern`.

# Let chains (Rust 2024)
**нет**

# Match expressions
**да**

# Match guards
**да** — `pattern KW_IF expr FATARROW`.

# Match guard chains (`&&` chain)
**частично** — через обычный `&&` в `expr`.

# Match arm attributes
**нет**

# Loop expressions
**да** — `loop_expr`.

# While loops
**да**

# While let
**да** — `KW_WHILE KW_LET pattern`.

# For loops (iterator loops)
**да** — `for_stmt`/`FOR_EACH`.

# Loop labels
**да** — `labeled_loop_stmt`.

# Break / continue (labeled)
**да**

# Break value
**да** — `KW_BREAK LIFETIME expr` и `KW_BREAK expr`.

# Closures (`|| { }`)
**да** — `closure_expr`.

# Async closures
**н/д**

# Move closures (`move`)
**да** — `KW_MOVE` варианты.

# Closure trait implementations (`Fn`/`FnMut`/`FnOnce`)
**н/д** — семантика.

# Capture precision / RFC 2229
**н/д**

# Call expressions
**да**

# Disambiguating function calls (UFCS)
**частично (50%)** — `IDENT::IDENT(args)` есть (`STATIC_CALL`); `<T as Trait>::item` — нет.

# Method-call expressions
**да** — `METHOD_CALL`.

# Method resolution (auto-ref/deref)
**н/д** — семантика.

# `IntoIterator` for arrays
**н/д**

# Field access expressions
**да** — `FIELD_READ`.

# Automatic dereferencing в field access
**н/д**

# Tuple indexing expressions
**да** — `TUPLE_INDEX`.

# Struct expressions
**да** — `STRUCT_LIT`.

# Functional update syntax (`..other`)
**да** — `struct_update_lit`.

# Struct field init shorthand
**да** — `FIELD_SHORTHAND`.

# Enum variant expressions
**да** — `ENUM_LIT`/`ENUM_LIT_DATA`.

# Array expressions
**да** — `ARR_LIT`, `ARR_FILL_LIT`.

# Array indexing
**да** — `INDEX_READ`.

# Tuple expressions
**да** — `TUPLE_LIT`, включая unit `()`.

# Range expressions
**частично (30%)** — `..`/`..=` в for и struct-update; отдельного range expression нет.

# Grouped expressions
**да** — `paren_expr`.

# Borrow operators (`&`, `&mut`)
**да** — `AMP` и `ADDR_OF_MUT`.

# Raw borrow operators (`&raw const`, `&raw mut`)
**нет**

# Dereference operator (`*`)
**да** — `DEREF`.

# Try propagation (`?`)
**да** — `TRY_EXPR`.

# Negation operators (`-`, `!`)
**да**

# Arithmetic/logical binary operators
**да** — `+ - * / % & | ^ << >>`.

# Comparison operators
**да**

# Lazy boolean operators
**да** — `&&`, `||`.

# Type cast (`as`)
**да** — `cast_expr`.

# Numeric cast semantics
**н/д**

# Assignment expression
**частично** — только как `assign_stmt`, не выражение.

# Destructuring assignment
**нет** — `let (a,b) = ...` есть, но присваивание в паттерн — нет.

# Compound assignment (`+=`, `-=`, ...)
**да** — `compound_assign_op`.

# Await expressions (`.await`)
**н/д**

# Return expressions
**да**

# `_` expressions
**нет**

# Expression precedence
**да** — через nesting уровней в грамматике.

# Place vs value expressions
**н/д**

# Moved and copied types
**н/д**

# Temporaries
**н/д**

# Implicit borrows
**н/д**

# Overloading traits
**н/д**

# Labeled block `break` с value
**частично** — labeled loop `break value` есть; labeled blocks — нет.

# Qualified path expressions (`<T as Trait>::CONST`)
**нет**

# Underscore expression в присваивании
**нет**

---

## Types

# Array type `[T; N]`
**да** — `arr_type`.

# Slice type `[T]`
**да** — `slice_type` (как `&[T]`/`&mut [T]`).

# Boolean type
**да**

# Character type
**частично** — имя типа `char` можно написать, литералов char нет.

# Textual types (`str`)
**частично** — как идентификатор типа.

# Numeric types (`u8`..`u128`, `i8`..`i128`, `f32`, `f64`)
**да**

# Machine-dependent integers (`usize`, `isize`)
**да**

# Tuple types
**да** — `tuple_type`.

# Never type (`!`)
**нет**

# Inferred type (`_`)
**нет**

# Function item types
**н/д** — семантика.

# Function pointer types (`fn(...) -> T`)
**да** — `fn_ptr_type`.

# Closure types
**да** — `closure_type` (`|T1,T2| -> R`).

# References (`&T`, `&mut T`)
**да** — `ref_type`/`mut_ref_type`.

# Raw pointers (`*const T`, `*mut T`)
**да** — `ptr_type`.

# Smart pointers
**н/д** — библиотечные.

# Struct types
**да**

# Enum types
**да**

# Union types
**нет**

# Trait objects (`dyn Trait`)
**да** — `dyn_type`.

# `dyn` keyword (обязателен с 2021)
**да** — `KW_DYN` обязателен.

# Trait object lifetime bounds
**нет**

# Impl Trait (return position)
**частично (40%)** — `impl_type` есть как `impl IDENT`; полноценные bounds нет.

# Impl Trait в аргументах
**частично** — `impl_type` допустим в `type_ref`.

# RPIT in traits (RPITIT)
**частично**

# Precise capturing (`use<...>`)
**нет**

# 2024 edition: автоматический захват lifetime в RPIT
**н/д**

# Dynamically sized types (DSTs)
**н/д** — семантика.

# Type parameters
**да**

# Recursive types
**н/д**

# Parenthesized types
**нет** — `(T)` как тип не поддерживается.

# Interior mutability
**н/д**

---

## Visibility & Names

# Visibility and privacy
**частично (30%)** — `pub` есть, без путевых модификаторов.

# Name resolution
**н/д**

# Namespaces (types, values, macros, lifetimes, labels)
**н/д**

# Scopes
**н/д**

# Preludes
**н/д**

# Extern prelude
**н/д**

# `no_std` attribute
**н/д**

# `no_implicit_prelude`
**н/д**

# Paths
**частично (30%)** — `IDENT::IDENT` поддерживается; длинные пути, `crate::`/`super::`/`self::` — нет.

# Path qualifiers (`::`, `crate::`, `self::`, `super::`, `Self::`)
**нет**

# Qualified paths (`<T as Trait>::item`)
**нет**

# Pub use re-exports
**нет**

---

## Macros (all н/д)

# Macros by example (`macro_rules!`)
**н/д**

# Procedural macros
**н/д**

# Macro expansion / hygiene
**н/д**

---

## Attributes

# Attributes
**да** — `annotation` (`#[name]`, `#[name(args)]`, `#[name = int]`).

# Inner vs outer attributes (`#![...]` vs `#[...]`)
**нет** — только outer `#[...]`.

# Meta item attribute syntax
**частично (60%)** — три формы есть; args — только идентификаторы, value — только integer.

# Active vs inert attributes
**н/д**

# Tool attributes
**н/д**

# Unsafe attributes `#[unsafe(attr)]`
**нет**

# Conditional compilation (`cfg`, `cfg_attr`)
**н/д**

# Derive attribute (`#[derive(...)]`)
**н/д**

# Built-in derives
**н/д**

# `automatically_derived`
**н/д**

# Lint levels
**н/д**

# `#[expect]` (Rust 1.81)
**н/д**

# `deprecated` attribute
**н/д**

# `must_use` attribute
**н/д**

# `diagnostic::on_unimplemented`
**н/д**

# `diagnostic::do_not_recommend`
**н/д**

# `inline` attribute
**н/д**

# `cold` attribute
**н/д**

# `naked` attribute
**н/д**

# `no_builtins` attribute
**н/д**

# `target_feature` attribute
**н/д**

# `track_caller` attribute
**н/д**

# Testing attributes
**н/д**

# Limits attributes
**н/д**

# `non_exhaustive`
**н/д**

# `repr` attribute
**н/д**

# Crate-level attributes
**н/д**

# `#[macro_export]`
**н/д**

# Attribute macros
**н/д**

# Derive helper attributes
**н/д**

# `macro_use` prelude
**н/д**

---

## Unsafe

# `unsafe` keyword
**да** — `unsafe fn`, `unsafe { }`.

# Unsafe trait impls (`unsafe impl`)
**да** — реализовано вместе с `unsafe trait`.

# Behavior considered undefined (UB)
**н/д**

# Behavior not considered unsafe
**н/д**

---

## Runtime & Linking (н/д)

# Inline assembly (`asm!`, `global_asm!`)
**н/д**

# Panic и unwinding
**н/д**

# Panic runtime
**н/д**

# Divergence
**н/д**

# Memory model / memory allocation
**н/д**

# Runtime
**н/д**

# Linkage
**н/д**

# Overflow checks
**н/д**

# Crate types
**н/д**

# Input format (BOM, CRLF)
**н/д**

---

## Logos-specific (не в Rust)

Конструкции, специфичные для Logos и не входящие в Rust-чеклист:
- `datatype` — Hermes-совместимый flat POD тип с fat pointer
- `class` + `extends` + `abstract` — классы с наследованием (временно)
- `new`/`delete` statements
- `tagged<TS>` thin pointers — тег-диспетчеризация
- Variadic generics (`T...`) + pack expansion (`IDENT...`)
- `schema` — декларация Hermes-схемы (планируется)
