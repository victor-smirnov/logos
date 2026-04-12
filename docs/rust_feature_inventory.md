# Rust Language Feature Inventory
# Source: Rust Reference ~1.94, for use as Logos implementation checklist

---

# Модули (`mod`)
Пространства имён, группирующие items; могут быть inline или в отдельных файлах.

# Extern crate declarations
Декларация связывает внешний crate с локальным именем; в 2018+ обычно не требуется.

# Use declarations
Импортируют имена в текущую область видимости.

# `as` rename в use
Переименование импортируемого имени.

# Brace import syntax (`use foo::{a, b}`)
Импорт нескольких путей одной конструкцией.

# `self` imports
`use foo::{self, bar}` импортирует и сам модуль.

# Glob imports (`use foo::*`)
Импорт всех публичных элементов пути.

# Underscore imports (`use ... as _`)
Импорт трейта ради методов без привязки имени.

# Functions (`fn`)
Именованные функции с параметрами, возвращаемым типом и телом.

# Generic functions
Функции с generic параметрами типов, lifetime и const.

# Const functions (`const fn`)
Функции, вычислимые в const-контексте.

# Async functions (`async fn`, Rust 2018)
Возвращают `Future`; доступны с Rust 2018.

# Unsafe functions (`unsafe fn`)
Вызов требует `unsafe`; могут нарушать инварианты.

# Extern functions (`extern "ABI" fn`)
Функции с указанным ABI для FFI.

# Variadic functions
Функции с `...` в extern блоках для C-style variadic.

# Anonymous parameters (до 2018)
`fn foo(u8)` без имени параметра — запрещено в 2018+.

# Named function parameter patterns
Параметры — паттерны; обязательны с 2018 edition.

# Function attributes
Атрибуты на функциях и их параметрах.

# Type aliases (`type`)
Синоним существующего типа.

# Generic type aliases
Aliases с параметрами типов.

# Structs (`struct`)
Именованный продуктный тип.

# Tuple structs
Структуры с позиционными полями.

# Unit structs
Структуры без полей.

# Enums (`enum`)
Сумма-типы с вариантами.

# Enum discriminants
Целочисленные дискриминанты вариантов, включая явные значения.

# Zero-variant enums
Enums без вариантов — невозможно инстанцировать.

# Discriminant access (`mem::discriminant`, `as`)
Получение значения дискриминанта.

# Unions (`union`)
Типы с перекрывающимися полями; чтение — `unsafe`.

# Pattern matching on unions
Требует `unsafe`.

# Constants (`const`)
Именованные compile-time константы.

# Unnamed constants (`const _`)
Константы без имени.

# Constants with destructors
Значение уничтожается на каждом использовании.

# Static items (`static`)
Значения с 'static памятью.

# Mutable statics (`static mut`)
Глобальная мутабельная переменная; доступ `unsafe`.

# Safe/unsafe statics in extern blocks (2024)
Квалификаторы `safe`/`unsafe` для extern-элементов.

# Traits (`trait`)
Набор требований/API для типов.

# Trait bounds
Ограничения на generic-параметры.

# Generic traits
Трейты с параметрами типов.

# Supertraits
Трейт, требующий реализации других (`trait B: A`).

# Dyn compatibility (object-safety)
Правила, допускающие использование трейта в trait-object.

# Unsafe traits (`unsafe trait`)
Реализация требует `unsafe impl`.

# Trait methods (default methods)
Методы с телами по умолчанию.

# Associated functions and methods
Функции, связанные с типом; методы принимают `self`.

# Associated types
Тип-член трейта.

# Associated constants
Константа-член трейта.

# Generic Associated Types (GATs)
Ассоциированные типы с собственными generic-параметрами.

# Required where clauses on GATs
Обязательные where-условия для GAT.

# Inherent impls
Методы без трейта.

# Trait impls
Реализация трейта для типа.

# Trait coherence / orphan rules
Ограничения, запрещающие противоречивые impl.

# Generic implementations
Generic impl блоки.

# Blanket impls
Impl для всех типов, удовлетворяющих ограничению.

# Generic parameters (type, lifetime, const)
Три рода параметров generics.

# Const generics
Generic-параметры-константы.

# Where clauses
Детальные ограничения вне списка параметров.

# External blocks (`extern { ... }`)
Декларации FFI символов.

# Unsafe extern blocks (Rust 2024)
С 2024 edition `extern` должен быть `unsafe`.

# `link` attribute
Указание библиотеки, с которой линковать.

# `link` verbatim modifier
Передача имени линковщику как есть.

# `link_name` attribute
Альтернативное имя символа.

# `link_ordinal` attribute
Линковка по ординалу (Windows).

# `no_link` attribute
`extern crate` без линковки.

# Visibility (`pub`, `pub(crate)`, `pub(in path)`, `pub(super)`, `pub(self)`)
Модификаторы видимости.

# Modules via file (`path` attribute)
Указание пути файла модуля.

# `mod.rs` vs `name.rs` (1.30)
С rustc 1.30 предпочтительно именовать файл по имени модуля.

# Patterns
Сопоставление значений и привязка переменных.

# Literal patterns
Сопоставление литералов.

# Identifier patterns (incl. `ref`, `mut`, `@`)
Привязка имён, включая binding modes и `@`-binding.

# Default binding modes (match ergonomics)
Автоматическое добавление `ref`/`ref mut` при match через ссылку.

# Binding mode limitations (2024)
В 2024 edition запрещены некоторые старые комбинации `mut`/`ref`.

# Wildcard pattern (`_`)
Игнорирует значение без привязки.

# Rest pattern (`..`)
Игнорирует оставшиеся элементы в структурах, кортежах, слайсах.

# Reference patterns (`&`, `&mut`)
Разыменование при сопоставлении.

# Struct patterns
Деструктуризация структур.

# Tuple struct patterns
Деструктуризация tuple struct.

# Tuple patterns
Деструктуризация кортежей.

# Grouped patterns
Паттерны в скобках.

# Slice patterns
Сопоставление массивов/слайсов, включая `..`.

# Path patterns
Паттерны-константы, варианты enum.

# Or-patterns (`a | b`)
Альтернативы в одном паттерне.

# Range patterns (`a..=b`, `a..b`)
Диапазоны.

# Obsolete range pattern (`...`, до 2021)
Старый синтаксис диапазона; удалён в 2021.

# Refutable vs irrefutable patterns
Паттерны могут не совпасть или всегда совпадают.

# Trait bounds
`T: Trait`.

# `?Sized`
Снятие Sized-ограничения.

# Lifetime bounds
`'a: 'b`, `T: 'a`.

# Higher-ranked trait bounds (HRTB, `for<'a>`)
Квантор по lifetimes.

# Implied bounds
Неявные ограничения из сигнатур.

# Use bounds (`use<...>`)
Precise capturing — явный контроль захватываемых generics в impl Trait.

# Type coercions
Неявные преобразования в coercion sites.

# Deref coercion
Через `Deref`/`DerefMut`.

# Unsized coercions
Приведение к unsized типам (например, `[T; N]` → `[T]`).

# Pointer coercions
Например `&T` → `*const T`, fn item → fn pointer.

# Non-capturing closure → fn pointer coercion
Замыкание без захватов к fn pointer.

# Least upper bound coercions
Слияние веток к общему типу.

# Subtyping (lifetime variance)
Подтипизация по lifetimes.

# Lifetime elision (functions)
Правила вывода lifetime в сигнатурах.

# Default trait object lifetimes
Правила вывода lifetime для `dyn Trait`.

# `const`/`static` lifetime elision
Правила для элизии в static/const.

# Special type `Box<T>`
Выделение в куче.

# Special type `Rc<T>`
Ссылочный счётчик без атомарности.

# Special type `Arc<T>`
Атомарный ссылочный счётчик.

# Special type `Pin<P>`
Запрет перемещения.

# `UnsafeCell<T>`
Основа interior mutability.

# `PhantomData<T>`
Маркер использования типа без хранения.

# Operator traits (std::ops)
Перегрузка операторов через трейты.

# `Deref`/`DerefMut`
Перегрузка `*`.

# `Drop`
Деструктор.

# `Copy`
Побитовое копирование.

# `Clone`
Явное копирование.

# `Send`, `Sync`
Трейты thread-safety.

# `Termination`
Трейт возврата из `main`.

# Auto traits
Автоматически реализуемые трейты (`Send`, `Sync`, `Unpin`, etc.).

# `Sized`
Известный размер в байтах.

# Constant evaluation
Вычисление выражений во время компиляции.

# Const contexts
Позиции, требующие const-значений.

# Constant expressions
Допустимые в const выражения.

# Const blocks (`const { ... }`)
Блок, вычисляемый в const-контексте.

# Const promotion
Неявное поднятие временных в статические.

# Temporary lifetime extension
Продление жизни временных, привязанных ссылкой в let.

# Destructors (drop glue)
Автоматический порядок вызова Drop.

# Drop scopes
Области, в которых дропятся значения.

# Temporary scopes (Rust 2024 narrowing)
`if let`-temporaries дропаются до else, tail-expr дропаются после.

# `mem::forget` / `ManuallyDrop`
Подавление деструкторов.

# Tokens
Лексические единицы.

# Literals (integer, float, bool, char, string, byte, byte-string)
Виды литералов.

# C string literals (`c""`, Rust 2021)
C-строковые литералы с 2021 edition.

# Raw C string literals (`cr""`, Rust 2021)
Raw-вариант C-строк.

# Raw string literals (`r"..."`, `r#"..."#`)
Без эскейпов.

# Byte string literals (`b""`)
Массив байт.

# Raw byte string literals
Raw-вариант byte-строк.

# Escape sequences
Эскейпы в строковых/char литералах: simple, `\x`, `\u{}`, string continuation.

# Number literal suffixes
Типовые суффиксы `i32`, `u64`, `f32` и т.д.

# Lifetimes / loop labels tokens
`'a`, `'label`.

# Raw lifetimes (`'r#lt`, Rust 2021)
Keyword-lifetimes через raw-синтаксис с 2021.

# Reserved prefixes (Rust 2021)
`k#`, `f""` и т.п. зарезервированы лексером с 2021.

# Reserved guards (Rust 2024)
`#"..."#`, `##` зарезервированы с 2024.

# Punctuation / Delimiters
Знаки `+`, `-`, `{`, `}` и т.д.

# Keywords (strict, reserved, weak)
Классификация ключевых слов.

# Raw identifiers (`r#keyword`)
Использование ключевых слов как идентификаторов.

# Comments (line, block, doc, inner doc)
`//`, `/* */`, `///`, `//!`.

# Shebang
`#!...` в начале crate-файла.

# Statements
Декларации и expression-statements.

# `let` statements
Локальные переменные с паттерном, типом, инициализатором.

# `let ... else`
Early-return, если паттерн не совпал.

# Item declarations inside blocks
Вложенные items внутри функций/блоков.

# Expression statements
Выражения, используемые как statement.

# Block expressions
`{ ... }` как выражение.

# Async blocks (`async { }`, Rust 2018)
Блок, возвращающий Future.

# Unsafe blocks (`unsafe { }`)
Разрешают небезопасные операции.

# Labeled block expressions
Блоки с меткой для `break`.

# Literal expressions
Выражения-литералы.

# Path expressions
Имена переменных, функций, констант и т.п.

# If expressions
Ветвление.

# If let expressions
Ветвление с паттерном.

# Let chains (Rust 2024)
Цепочки `if let ... && ...` доступны с 2024.

# Match expressions
Сопоставление паттернов.

# Match guards
Условия `if` в ветках match.

# Match guard chains (`&&` chain)
Цепочки условий в match guard.

# Match arm attributes
Атрибуты на ветках.

# Loop expressions
`loop { }` — бесконечный цикл.

# While loops
С условием.

# While let
С паттерном.

# For loops (iterator loops)
Итерация по `IntoIterator`.

# Loop labels
`'label: loop`.

# Break / continue (labeled)
Управление потоком с метками.

# Break value
Возврат значения из loop.

# Closures (`|| { }`)
Анонимные функции с захватом.

# Async closures (Rust 2018)
Замыкания, возвращающие Future.

# Move closures (`move`)
Принудительный move-захват.

# Closure trait implementations (`Fn`/`FnMut`/`FnOnce`)
Какие трейты реализует замыкание.

# Capture precision / RFC 2229 (2021)
Более точный захват полей (disjoint capture), edition-зависимо.

# Call expressions
Вызов функций/замыканий.

# Disambiguating function calls (UFCS, `Type::fn(...)`)
Квалифицированные вызовы.

# Method-call expressions
`receiver.method(...)`.

# Method resolution (auto-ref/deref)
Автодобавление `&`/`&mut`/`*`.

# `IntoIterator` for arrays (Rust 2021 method resolution)
В 2021+ массив реализует IntoIterator в методном разрешении.

# Field access expressions
`.field`.

# Automatic dereferencing в field access
Автодереференс при доступе к полю.

# Tuple indexing expressions
`.0`, `.1`.

# Struct expressions
`Foo { a, b }`.

# Functional update syntax (`..other`)
Остальные поля из другого значения.

# Struct field init shorthand
`Foo { x }` вместо `Foo { x: x }`.

# Enum variant expressions
Конструирование варианта enum.

# Array expressions
`[a, b, c]`, `[v; N]`.

# Array indexing
`a[i]`.

# Tuple expressions
`(a, b, c)`.

# Range expressions
`a..b`, `a..=b`, `..b`, `a..`, `..`.

# Grouped expressions
`(expr)`.

# Borrow operators (`&`, `&mut`)
Создание ссылок.

# Raw borrow operators (`&raw const`, `&raw mut`)
Создание сырых указателей без промежуточной ссылки.

# Dereference operator (`*`)
Разыменование.

# Try propagation (`?`)
Пропагация ошибок/none.

# Negation operators (`-`, `!`)
Арифметическое/битовое отрицание.

# Arithmetic/logical binary operators
`+ - * / % & | ^ << >>`.

# Comparison operators
`== != < > <= >=`.

# Lazy boolean operators
`&&`, `||`.

# Type cast (`as`)
Явное приведение типов.

# Numeric cast semantics
Правила приведения чисел.

# Assignment expression
`=`.

# Destructuring assignment
Присваивание в паттерн.

# Compound assignment (`+=`, `-=`, ...)
Через соответствующие трейты.

# Await expressions (`.await`, Rust 2018)
Ожидание Future.

# Return expressions
`return value`.

# `_` expressions
Placeholder в lvalue-контексте.

# Expression precedence
Приоритет операторов.

# Place vs value expressions
Категории выражений.

# Moved and copied types
Правила перемещения/копирования.

# Temporaries
Временные значения выражений.

# Implicit borrows
Неявные заёмы в некоторых местах.

# Overloading traits
Поведение операторов через трейты.

# Array type `[T; N]`
Тип массива фиксированной длины.

# Slice type `[T]`
Unsized слайс.

# Boolean type
`bool`.

# Character type
`char` — unicode scalar value.

# Textual types (`str`)
UTF-8 строковый слайс.

# Numeric types (`u8`..`u128`, `i8`..`i128`, `f32`, `f64`)
Целые и плавающие.

# Machine-dependent integers (`usize`, `isize`)
Размер зависит от платформы.

# Tuple types
Продуктные кортежи.

# Never type (`!`)
Расходящийся тип.

# Inferred type (`_`)
Placeholder для вывода.

# Function item types
Уникальный zero-sized тип каждой функции.

# Function pointer types (`fn(...) -> T`)
Явные указатели на функции.

# Closure types
Анонимные типы замыканий.

# References (`&T`, `&mut T`)
Безопасные ссылки.

# Raw pointers (`*const T`, `*mut T`)
Небезопасные указатели.

# Smart pointers
Типы, реализующие Deref (Box, Rc, Arc, …).

# Struct types
Типы-структуры.

# Enum types
Типы-enum.

# Union types
Типы-union.

# Trait objects (`dyn Trait`)
Динамическая диспетчеризация.

# `dyn` keyword (обязателен с 2021)
До 2021 мог опускаться.

# Trait object lifetime bounds
Правила lifetime для `dyn Trait`.

# Impl Trait (return position)
Анонимный тип в возврате.

# Impl Trait в аргументах
Универсально-квантифицированный параметр.

# RPIT in traits (RPITIT)
Return-position impl Trait в методах трейта.

# Precise capturing (`use<...>`)
Явный контроль, какие generics захватываются в RPIT.

# 2024 edition: автоматический захват lifetime в RPIT
С 2024 edition все in-scope lifetimes захватываются автоматически.

# Dynamically sized types (DSTs)
`str`, `[T]`, `dyn Trait` — размер неизвестен в compile time.

# Type parameters
Generic параметры типов.

# Recursive types
Типы, ссылающиеся на себя (через указатель).

# Parenthesized types
`(T)` как тип.

# Interior mutability
`Cell`/`RefCell`/`UnsafeCell`.

# Visibility and privacy
Правила приватности.

# Name resolution
Разрешение имён.

# Namespaces (types, values, macros, lifetimes, labels)
Разные пространства имён.

# Scopes
Области видимости.

# Preludes (std, extern, language, macro_use, tool)
Неявно импортируемые имена.

# Extern prelude (edition-зависимо)
Доступ к внешним crate без `extern crate` в 2018+.

# `no_std` attribute
Отключение std prelude, включение core.

# `no_implicit_prelude`
Отключает prelude в модуле.

# Paths
Квалифицированные имена.

# Path qualifiers (`::`, `crate::`, `self::`, `super::`, `Self::`)
Виды префиксов путей.

# Qualified paths (`<T as Trait>::item`)
UFCS-путь.

# Macros by example (`macro_rules!`)
Декларативные макросы.

# Procedural macros (function-like, derive, attribute)
Макросы на Rust, работающие на AST.

# Macro expansion / hygiene
Гигиена идентификаторов в макросах.

# Attributes
`#[...]` и `#![...]` метаданные.

# Inner vs outer attributes (`#![...]` vs `#[...]`)
Атрибуты, применяемые к содержащему элементу vs следующему элементу.

# Meta item attribute syntax
Формы атрибутов (`word`, `list`, `name = value`).

# Active vs inert attributes
Атрибуты, преобразующие или нет.

# Tool attributes
`#[clippy::..]`, `#[rustfmt::..]`.

# Unsafe attributes `#[unsafe(attr)]`
Обёртка для атрибутов с unsafe-эффектами.

# Conditional compilation (`cfg`, `cfg_attr`)
Условная компиляция.

# Derive attribute (`#[derive(...)]`)
Автогенерация реализаций трейтов.

# Built-in derives (`Debug`, `Clone`, `Copy`, `Eq`, `PartialEq`, `Ord`, `PartialOrd`, `Hash`, `Default`)
Стандартные derive.

# `automatically_derived`
Пометка сгенерированных impl.

# Lint levels (`allow`, `warn`, `deny`, `forbid`, `expect`)
Управление lint-уровнями.

# `#[expect]` (Rust 1.81)
Ожидание срабатывания lint.

# `deprecated` attribute
Пометка устаревших items.

# `must_use` attribute
Предупреждение, если результат игнорирован.

# `diagnostic::on_unimplemented`
Кастомное сообщение о нереализованном трейте.

# `diagnostic::do_not_recommend`
Исключить impl из подсказок компилятора.

# `inline` attribute
Подсказка инлайнинга.

# `cold` attribute
Маркировка редко вызываемых функций.

# `naked` attribute
Голая функция без пролога/эпилога.

# `no_builtins` attribute
Запрет использования builtins.

# `target_feature` attribute
Включение аппаратных фич.

# `track_caller` attribute
Передача места вызова во внутренние panics.

# Testing attributes
`#[test]`, `#[bench]`, `#[should_panic]`, `#[ignore]`.

# Limits attributes
`recursion_limit`, `type_length_limit`.

# `non_exhaustive`
Запрет исчерпывающего сопоставления извне crate.

# `repr` attribute
Управление layout структур/enum (`C`, `transparent`, `packed`, `align(n)`, integer reprs).

# Crate-level attributes
`crate_name`, `crate_type`, `no_main`, `windows_subsystem`, и т.п.

# `#[macro_export]`
Экспорт `macro_rules!` макроса.

# Attribute macros
Прок-макросы в виде атрибутов, преобразующие item.

# Derive helper attributes
Атрибуты, видимые derive-макросу (например, `#[serde(...)]`).

# `macro_use` prelude
Макросы, автоматически видимые из external crate.

# ABI specifications
`extern "C"`, `"system"`, `"Rust"`, etc.

# Inline assembly (`asm!`, `global_asm!`)
Вставка ассемблера.

# Panic и unwinding
Раскрутка стека или abort.

# Panic runtime (`panic = "abort" | "unwind"`)
Выбор стратегии паники.

# Divergence
Никогда не возвращающие вычисления.

# `unsafe` keyword
Разрешает небезопасные возможности в пяти позициях.

# Unsafe trait impls (`unsafe impl`)
Подтверждение unsafe-контрактов трейта.

# Behavior considered undefined (UB)
Список UB в Rust.

# Behavior not considered unsafe
Действия, формально не UB (leaks, deadlocks).

# Memory model / memory allocation
Семантика памяти, аллокации, лайфтаймы.

# Runtime
Точка входа, main, аргументы.

# Linkage
Виды линковки crate (bin, lib, rlib, dylib, cdylib, staticlib, proc-macro).

# Overflow checks
Поведение арифметики при overflow (debug vs release, `Wrapping`, checked/overflowing/saturating).

# Subtyping & variance
Ковариантность/контравариантность/инвариантность параметров.

# Labeled block `break` с value
Возврат значения из labeled block.

# Async fn traits (`AsyncFn`, `AsyncFnMut`, `AsyncFnOnce`)
Трейты для async-замыканий.

# Qualified path expressions (`<T as Trait>::CONST`)
UFCS в выражениях.

# Underscore expression в присваивании
`_ = expr;` для явного игнорирования значения.

# Pub use re-exports
Повторный экспорт с `pub use`.

# Crate types (`bin`, `lib`, `proc-macro`, `rlib`, `dylib`, `cdylib`, `staticlib`)
Типы crate для линковки.

# Input format (BOM, CRLF)
Правила чтения исходников.
