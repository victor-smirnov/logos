# Logos Stdlib Provenance (Rust core → Logos mapping)

Policy: tests-driven for files with coretest coverage, module-driven
for files without. Inventory below tells you which is which.

Status legend:
- ✅ ported full — Rust API surface complete in Logos (possibly with
  divergent names / shape, noted)
- ⚠️ partial — some Rust items ported, others missing
- 🔁 divergence — by design (different runtime model, Logos-specific
  conventions); reopen if a port surfaces a need
- ❌ TODO — not touched yet
- ➖ N/A — Rust-internal scaffold (macros expanded inline, lang items,
  pre-build infra); no port intended

Source roots:
- Rust: `/home/victor/cxx/rust/library/core/src/`
- Logos: `/home/victor/devel/logos/stdlib/`
- Coretests: `/home/victor/devel/logos/tests/imported/pass/`

## core/

### Top-level files

| Rust file | Logos equivalent | Status | Coretest | Notes |
|---|---|---|---|---|
| `any.rs` | `stdlib/mem/any/any.logos` | 🔁 | — | Rust shape is `TypeId` + `&dyn Any` downcast; Logos uses Hermes `TypeInfo` (reflection over fields/annotations) keyed by stable type_code. Different mechanism, same intent. |
| `arch.rs` | — | 🔁 | — | Architecture intrinsics; Logos has no per-arch surface (compiler builtins instead). |
| `array.rs` | `lang/iter/iter.logos` (fixed-array iteration) | ⚠️ partial | `iterators/test_harness_coretest_iter_array.logos` | `for x in [a,b,c]` works; `array::from_ref` / `from_mut` / `IntoIter` / `try_from` not in stdlib. |
| `ascii.rs` | `lang/char/char.logos` (`is_ascii_*` methods) | ⚠️ partial | `char/test_harness_coretest_char_ascii.logos` | `EscapeDefault` iterator + `AsciiChar` enum absent. |
| `asserting.rs` | — | ➖ | — | Internal `assert!` formatter machinery; Logos's `assert!`/`assert_eq!`/`assert_ne!` are metacall handlers in `std/fmt/fmt.logos`. |
| `bool.rs` | `lang/bool/bool.logos` | ✅ | `bool/test_harness_coretest_bool.logos`, `bool/test_harness_coretest_bool_conv.logos`, `bool/test_harness_coretest_bool_to_string.logos` | `then`, `then_some`, `ok_or`, `ok_or_else`. Bitwise via `lang/ops`. |
| `borrow.rs` | — | ❌ TODO | — | `Borrow<T>` / `BorrowMut<T>` traits not declared. Mostly cosmetic; Logos uses `&T` directly. |
| `cell.rs` | `lang/cell/cell.logos` | ⚠️ partial | `cell/test_harness_coretest_unsafe_cell.logos` | `UnsafeCell` (B100) + `Cell` (Copy bound) + `RefCell` (Ref/RefMut guards with Drop-based counter, panic on conflict). `CoerceUnsized`/`DispatchFromDyn` impls absent (no language support). `Ref::map`/`filter_map`/`map_split` absent. Pattern-extraction `try_borrow*` now works (match-arm Drop gap fixed 2026-05-18). |
| `clone.rs` | `lang/clone/clone.logos` | ⚠️ partial | `clone/test_harness_coretest_clone.logos` | `Clone` + `Copy` traits. `CloneToUninit` / `TrivialClone` absent. Logos `Clone::clone` takes `self: Self` (vs Rust `&self`) — pending migration. |
| `cmp.rs` | `lang/cmp/cmp.logos` + `lang/cmp/ord.logos` | ✅ | `cmp/test_harness_coretest_cmp.logos`, `cmp/test_harness_coretest_ordering_*.logos`, `cmp/test_harness_coretest_user_defined_eq.logos` | `Eq`/`PartialEq`/`PartialOrd`/`Ord`, `Ordering` (+reverse/is_lt/then/then_with), per-type min/max/clamp. |
| `contracts.rs` | — | 🔁 | — | Unstable `#[requires]`/`#[ensures]`; no Logos equivalent. |
| `default.rs` | `lang/default/default.logos` | ✅ | `option/test_harness_coretest_option_default.logos`, `option/test_harness_coretest_option_unwrap_or_default.logos` | `Default` trait. |
| `error.rs` | `lang/error/error.logos` | ⚠️ partial | `error_trait_chain.logos`, `dyn_in_enum_payload.logos` | `Error` trait with Rust-shape `fn source(&self) -> Option<&dyn Error>` + `error_chain_len(&dyn Error)` walker. Wired for `ParseIntError`/`TryFromIntError`. Achieved by fixing two mlir-gen bugs: match-extract for TraitObject payload now binds the handle directly (mirroring `gen_let`), and `gen_dyn_dispatch`'s indirect-call return-type now handles Enum (was Struct only). No `Display+Debug` supertrait bound (lives in `std.fmt`, mem tier). No `provide()`/`description()`/`cause()`. |
| `escape.rs` | — | ❌ TODO | — | `EscapeIterInner` shared between ascii/char escape iterators. |
| `ffi/` | extern decls + `rt/*.c` | 🔁 | — | Different FFI shim: Logos uses `unsafe extern fn` declarations + `.c` shim files in `stdlib/rt/`; no `c_int`/`c_void`/`CStr`/`va_list` types. |
| `field.rs` | `mem/any/any.logos` (`FieldInfo`) | 🔁 | — | Unstable `field_of!` macro / `FieldRepresentingType`. Logos exposes fields via runtime `FieldsView` from `TypeInfo`. |
| `fmt/` | `std/fmt/fmt.logos` + `Display`/`Debug` traits in `mem/string/string.logos` + Rust-shape support types in `mem/fmt/fmt.logos` | ⚠️ partial | `bool/test_harness_coretest_bool_to_string.logos`, `fmt_formatter_smoke.logos` | `format!`/`print!`/`println!`/`eprintln!` metacalls; `Display`/`Debug`/`LowerHex`/`UpperHex`/`Octal`/`Binary`/`LowerExp`/`UpperExp`. `Formatter`/`FmtWrite`/`Arguments`/`fmt::Error` now declared in `logos.mem.fmt` (FmtWrite avoids name-clash with std.io.write::Write — B-mv-02). Display/Debug migration to Rust shape deferred. |
| `future/` | `std-new/rt/fiber/future.logos` | 🔁 | — | Logos has fibers (FutureSlot/poll-less); no `async fn` syntax / `Pin` / poll-based Future. |
| `hash/` | `lang/hash/hash.logos` | ⚠️ partial | — | `Hash` + `Hasher` traits + `FxHasher`. No `SipHasher`/`BuildHasher`/`DefaultHasher`. |
| `hint.rs` | — | ⚠️ partial | — | `unreachable!()` macro exists; `black_box`, `spin_loop`, `must_use` not exposed. |
| `index.rs` | — | 🔁 | — | Unstable `Clamp` slice-index helper. Indexing on `Vec`/`String` happens via methods, not `Index<Idx>`. |
| `internal_macros.rs` | — | ➖ | — | Internal `forward_ref_*!` macros. |
| `intrinsics/` | compiler builtins in `src/compiler/*` | 🔁 | — | All `intrinsics::*` are compiler-emitted; `popcount_u64`/`leading_zeros_u64`/`sqrt`/etc. exposed via `lang/cmp/ord.logos` + `lang/math/math.logos`. |
| `io/` | `std/io/` (read/write/buffered/bytes/fs/net/pipe/http/linux-uring) | ⚠️ partial | — | core::io is just `Read`/`Write` + `BorrowedBuf`; Logos's `std/io` is closer to `std::io` (sockets, fs, http, io_uring). `Read`/`Write` traits in `std/io/read|write` but no `BorrowedBuf`. |
| `iter/` | `lang/iter/iter.logos` (1.9K LOC) | ⚠️ partial | 41 `iterators/test_harness_coretest_iter_*.logos` files + `iter_try_fold.logos` | Most adapters ported (map/filter/filter_map/scan/fuse/take/skip/take_while/skip_while/step_by/inspect/peekable/once/empty/repeat/cycle/chain/zip/enumerate/rev/from_fn/repeat_n). Short-circuit terminals `try_fold<Acc, BV, F: FnMut(Acc, Item) -> ControlFlow<BV, Acc>>` + `try_for_each<BV, F: FnMut(Item) -> ControlFlow<BV, ()>>` added 2026-05-18, both fn-ptr and closure form. `flatten`/`array_chunks`/`map_windows`/`intersperse`/`successors` absent (successors blocked on [[baghunt-mono-fn-ptr-field-typevar]]). `cloned`/`copied` intentionally omitted (Logos iters yield by value, not refs). `DoubleEndedIterator`/`ExactSizeIterator` present; `FusedIterator`/`TrustedLen` absent. |
| `lib.miri.rs` / `lib.rs` | `stdlib/logos.module` | ➖ | — | Crate entry. |
| `macros/` | `std/fmt/fmt.logos` (metacalls) + grammar macros | 🔁 | `macros/test_harness_coretest_assert_macros.logos` | `assert!`/`assert_eq!`/`assert_ne!`/`println!`/etc. implemented as metaprog handlers. `macro_rules!` (MC-mc-01) still Open. |
| `marker.rs` | `lang/marker/marker.logos` | ⚠️ partial | — | `Sized` + `Never` exposed. `Send`/`Sync` referenced (e.g. `Arc`) but not declared. `Unpin`/`PhantomData`/`PhantomPinned`/`Destruct`/`Tuple` absent. |
| `mem/` | `mem/mem/mem.logos` | ⚠️ partial | `mem/test_harness_coretest_mem.logos`, `mem/test_harness_coretest_size_of.logos` | `swap_ref`/`replace_ref`/`take_ref` (B101) + raw `alloc`/`dealloc`/`realloc_buf`. `size_of`/`align_of` are compiler builtins. `MaybeUninit`/`ManuallyDrop`/`Alignment`/`drop_guard` absent. |
| `net/` | `std/io/net/` (+ `tls/`, `url/`) | 🔁 | — | Logos exposes runtime sockets; core's `IpAddr`/`Ipv4Addr`/`Ipv6Addr`/`SocketAddr` types not declared as such. |
| `num/` | `lang/cmp/ord.logos` + `lang/math/math.logos` | ⚠️ partial | 16 `num/test_harness_coretest_*.logos` (i32/i64/u32/u64 batteries) | Heavy coverage for integer methods. Missing: `NonZero`/`Wrapping`/`Saturating` newtype wrappers, `FpCategory`, `ParseIntError`/`ParseFloatError`, `f128`/`f16`, `to_str_radix`/`from_str_radix`. |
| `ops.rs` (re-exports from `ops/`) | `lang/ops/ops.logos` | ⚠️ partial | `deref_index_user_dispatch.logos`, `control_flow_basic.logos`, `op_assign_user_dispatch.logos` | `Add/Sub/Mul/Div/Rem/Neg/Shl/Shr/BitAnd/BitOr/BitXor/Not` + all 10 `*Assign` variants + `Fn`/`FnMut`/`FnOnce` (variadic-pack `<A...>`) + `Deref`/`DerefMut`/`Index`/`IndexMut` + `ControlFlow<B,C>`. `*x` / `a[i]` / `x op= y` operator-form sema-dispatched for user-typed structs. `DerefMut`/`IndexMut` write-side dispatch deferred (write today via explicit `*x.deref_mut() = v`). Method-autoderef-through-Deref-chain deferred. No `Range*` types (Logos has its own `lang/range/RangeI32`/`RangeI64`), no `Try`/`FromResidual` (compiler-builtin in TRY_EXPR). |
| `option.rs` | `lang/option/option.logos` | ⚠️ partial | 11 `option/test_harness_coretest_*.logos` | Surface: `is_some`/`is_none`/`unwrap`/`expect`/`unwrap_or`/`unwrap_or_default`/`unwrap_or_else`/`map`/`map_or`/`map_or_else`/`ok_or`/`ok_or_else`/`and`/`and_then`/`is_some_and`/`or`/`or_else`/`take`/`replace`/`as_ref`/`as_mut`/`xor`/`filter` + free fns `option_zip`/`option_flatten`. Missing: `iter`/`into_iter`/`as_deref`/`copied`/`cloned`/`unzip`/`flatten` (as method)/`zip`/`get_or_insert*`/`insert`. |
| `os/` | `std-new/os/os.logos` + `std-new/os/unix/signal/` | 🔁 | — | core::os has `darwin/` shims only; Logos exposes process/host info via `os.logos`. |
| `panic/` + `panicking.rs` + `panic.rs` | `lang/panic/panic.logos` + `rt/test_recovery.c` | ⚠️ partial | `panic_catch_unwind.logos` | `panic(msg: str)` + `assert(cond, msg)` + `Location {file,line,column}` (constructed explicitly — no `#[track_caller]` auto-threading yet) + `PanicInfo {message, location: Option<Location>}` + `catch_unwind(f: fn()) -> Result<(), str>` (fn-ptr only; Rust's FnOnce+R+Box<dyn Any> divergence — closure capture + arbitrary payloads pending). No `UnwindSafe`/`RefUnwindSafe`/`AssertUnwindSafe`. Test harness setjmp recovery shared with `#[test]`. |
| `pat.rs` | — | 🔁 | — | Nightly `pattern_type!` macro; not on Logos roadmap. |
| `pin.rs` + `pin/unsafe_pinned.rs` | — | 🔁 | — | No `Pin`/`Unpin`; Logos has no `async`/no self-referential generators. |
| `prelude/` | implicit (no prelude module yet) | ❌ TODO | — | Logos requires explicit `use logos.lang.*` for now. |
| `primitive.rs` | — | ➖ | — | Just re-exports primitive type names. |
| `primitive_docs.rs` | — | ➖ | — | Doc-only. |
| `process.rs` | `std/process/process.logos` | ⚠️ partial | — | core::process is `ExitCode`/`Termination`/`abort`/`exit`; Logos exposes `exit_process` etc. |
| `profiling.rs` | — | 🔁 | — | Internal `#[profiling]` scaffolding. |
| `ptr/` | `lang/ptr/ptr.logos` (+ compiler builtins `*const T`/`*mut T` syntax + `dst_from_raw_parts` from Phase 1B-14) | ⚠️ partial | — | `null`/`null_mut`/`read`/`write`/`copy`/`copy_nonoverlapping`/`swap`/`replace`/`drop_in_place`/`eq`/`addr_eq` + `NonNull<T>` (`new`/`new_unchecked`/`dangling`/`from_ref`/`from_mut`/`as_ptr`/`as_ref`/`as_mut`/`cast`/`add`/`sub`/`offset`). Missing: `read_volatile`/`write_volatile`/`read_unaligned`/`write_unaligned`/`without_provenance`/`with_exposed_provenance`/`slice_from_raw_parts`/`hash`/`fn_addr_eq`, `Unique<T>`. |
| `random.rs` | `std-new/random/random.logos` | 🔁 | — | core::random is `RandomSource`/`Distribution` traits (unstable). Logos exposes concrete `Rng` (xorshift-ish) + `os_random` directly. |
| `range/` + `range.rs` | `lang/range/range.logos` | 🔁 | `iterators/test_harness_coretest_iter_range*.logos` | Logos has `RangeI32`/`RangeI64`/`RevRangeI32`/`RevRangeI64`. Generic `Range<T>`/`RangeFrom`/`RangeTo`/`RangeFull`/`RangeInclusive` + `RangeBounds` absent. |
| `result.rs` | `lang/result/result.logos` | ⚠️ partial | 4 `result/test_harness_coretest_result*.logos`, `option-result/test_harness_coretest_qmark_from.logos` | Surface: `is_ok`/`is_err`/`is_ok_and`/`is_err_and`/`unwrap`/`unwrap_err`/`expect`/`expect_err`/`unwrap_or`/`unwrap_or_else`/`ok`/`err`/`map`/`map_err`/`map_or`/`map_or_else`/`and`/`or`/`and_then`/`or_else`. Missing: `iter`/`as_ref`/`as_mut`/`contains`/`contains_err`/`flatten`/`transpose`/`unwrap_or_default`/`into_ok`. |
| `slice/` | `lang/slice/slice.logos` + `lang/iter/iter.logos` (`SliceIter`) | ⚠️ partial | `iterators/test_harness_coretest_iter_array.logos` + `slice_basic` | Iteration via SliceIter; method surface: `slice_split_at` (returns SplitPair with raw ptr+len for each half — the slice-fat-pointer-as-struct-field shape segfaults), `slice_chunks` / `slice_windows` (Chunks / Windows iters), `slice_binary_search<T: Ord+Copy>` (Result<i64,i64>), `slice_sort<T: Ord+Copy>` (insertion sort, raw ptr+len). `sort_by`/`sort_unstable`/`SliceIndex` trait absent. `slice_from_raw::<T>(ptr,len) -> &[T]` intrinsic added (parametric mirror of `str_from_raw`). |
| `str/` | `lang/str/str.logos` + `lang/str/split.logos` + `lang/str/utf8.logos` | ⚠️ partial | — | `str_len`/`str_eq`/`str_starts_with`/`str_ends_with`/`str_index_of`/`str_contains`/`str_trim_start`/`str_trim_end`/`str_trim` + `Splitter`/`ByteSplitter` + UTF-8 helpers + `Chars`/`CharIndices`/`Bytes`/`Lines` iterators. Missing: `Pattern` trait, `Matches`/`MatchIndices`, `parse::<T>`. |
| `sync/atomic.rs` + `sync/mod.rs` | `lang/atomic/atomic.logos` + `mem/sync/arc.logos` | ⚠️ partial | atomic_ordering | `AtomicI32`/`AtomicU32`/`AtomicI64`/`AtomicU64`/`AtomicBool`/`AtomicPtr<T>` + `load`/`store`/`fetch_add`/`fetch_sub`/`compare_exchange`. `Ordering` enum (Relaxed/Acquire/Release/AcqRel/SeqCst) + `_ordered` variants on AtomicI32 — all variants currently degrade to seq-cst on x86 (TSO + LOCK-RMW already provides it). Other Atomic* types need the same `_ordered` overloads added. No `AtomicI8`/`AtomicI16` etc. |
| `task/` | `std-new/rt/fiber/sync.logos` (Latch/Chan) | 🔁 | — | `Poll`/`Waker`/`Context` model not present (no async/await). |
| `time.rs` | `std-new/time/time.logos` (+ `datetime/`) | ⚠️ partial | — | `Duration`/`Instant`/`SystemTime` constructors + accessors. Logos uses ns-i64 internally; core uses `(secs:u64, nanos:u32)`. No `Duration` arithmetic / saturating ops surface. |
| `tuple.rs` | per-arity stdlib impls (Eq/Display/Debug for 2/3/4-tuples) + planned variadic-tuple impls | ⚠️ partial | `tuple/test_harness_coretest_tuple_eq.logos` | Tuples are first-class; trait impls hand-written per arity (variadic `impl<A...>` is a baghunt item). |
| `ub_checks.rs` | — | ➖ | — | Internal UB-check scaffolding. |
| `unicode/` | `lang/char/char.logos` (predicates only) | ⚠️ partial | — | ASCII predicates ported; full Unicode `general_category`/`UPPERCASE_TABLE`/etc. absent. |
| `unit.rs` | language built-in `()` | ➖ | — | No `impl ()` extension fns yet. |
| `unsafe_binder.rs` | — | 🔁 | — | Nightly `unsafe<>` binder syntax. |
| `wtf8.rs` | — | 🔁 | — | OS-string internal encoding; not exposed publicly even in core. |

### Subdirs

#### core/array/

| File | Logos | Status | Notes |
|---|---|---|---|
| `mod.rs` | — (`array.rs` parent already mapped) | ⚠️ partial | See `array.rs` row above. |
| `ascii.rs` | — | ❌ TODO | `[u8; N]::as_ascii` / `as_ascii_unchecked`. |
| `drain.rs` | — | ❌ TODO | `array::Drain`. |
| `equality.rs` | language built-in (per-elem) | ⚠️ partial | `PartialEq` for `[T; N]` works via codegen for primitives; generic blanket not registered. |
| `iter.rs` / `iter/` | `lang/iter/iter.logos` (`SliceIter`) | ⚠️ partial | `IntoIter<T, N>` owned-array iterator absent; for-loop over `[T; N]` lowers directly. |

#### core/ascii/

| File | Logos | Status | Notes |
|---|---|---|---|
| `ascii_char.rs` | — | ❌ TODO | `AsciiChar` enum + `from_u8`/`to_char`/`as_str`. |

#### core/bstr/

| File | Logos | Status | Notes |
|---|---|---|---|
| `mod.rs` / `traits.rs` | — | ❌ TODO | `ByteStr`/`ByteString` unstable; Logos uses `*const u8 + len` ad hoc. |

#### core/cell/

| File | Logos | Status | Coretest | Notes |
|---|---|---|---|---|
| `once.rs` | `lang/cell/cell.logos` | ⚠️ partial | — | `OnceCell<T>` ported via free-fn `once_cell_new::<T>()` (T: Default workaround for lack of MaybeUninit-in-lang). Surface: `is_set`/`get`/`get_ptr`/`set`/`get_or_init`/`into_inner`. `wait`/blocking variants live in `std::sync::OnceLock`, not core. |
| `lazy.rs` | `lang/cell/cell.logos` | ⚠️ partial | — | `LazyCell<T>` ported via free-fn `lazy_cell_new::<T>(init: fn() -> T)` (Default bound inherited from OnceCell). Surface: `force`. `into_inner` deferred (needs Result<T, fn()->T> returning the unconsumed closure on empty). Closure type is plain `fn`, not Fn-family — captures wait on priority #1. |

(`UnsafeCell` lives in parent `cell.rs` → `lang/cell/cell.logos`, see above.)

#### core/char/

| File | Logos | Status | Coretest | Notes |
|---|---|---|---|---|
| `mod.rs` | `lang/char/char.logos` | ⚠️ partial | `char/test_harness_coretest_char.logos` | Predicates + ASCII ops. |
| `methods.rs` | `lang/char/char.logos` | ⚠️ partial | `char/test_harness_coretest_char.logos` | `to_digit` returns `Option<u32>`. `from_digit`/`encode_utf8`/`encode_utf16` absent. |
| `convert.rs` | `lang/convert/convert.logos` (u32→char TryFrom, B99) | ⚠️ partial | `char/test_harness_coretest_char_to_digit_option.logos` | `TryFromCharError`/`CharTryFromError` types absent. |
| `decode.rs` | `lang/str/utf8.logos` | ⚠️ partial | — | UTF-8 decode helpers; `DecodeUtf16` iterator absent. |

#### core/clone/

| File | Logos | Status | Notes |
|---|---|---|---|
| `uninit.rs` | — | ❌ TODO | `CloneToUninit` trait. |

#### core/cmp/

| File | Logos | Status | Notes |
|---|---|---|---|
| `bytewise.rs` | — | ➖ | Internal `BytewiseEq` specialization marker. |

#### core/convert/

| File | Logos | Status | Coretest | Notes |
|---|---|---|---|---|
| `mod.rs` | `lang/convert/convert.logos` | ⚠️ partial | `casts/test_harness_coretest_convert_from.logos` | `From`/`Into`/`TryFrom`/`TryInto` traits + ~25 widening primitive impls + ~12 TryFrom impls (B99). Missing: `AsRef`/`AsMut`/`Infallible` (as type)/`identity`. |
| `num.rs` | `lang/convert/convert.logos` (B99 batch) | ⚠️ partial | `casts/test_harness_coretest_convert_from.logos` | Widening i8→i16/i32/i64, u8→…, bool→i32/i64/u32/u64/u8, char→u32, u32→char. Narrowing TryFroms partial. |

#### core/ffi/
🔁 entire subtree. Logos uses `unsafe extern fn` and C shim files in `stdlib/rt/`.

#### core/fmt/

| File | Logos | Status | Notes |
|---|---|---|---|
| `mod.rs` | `std/fmt/fmt.logos` + `Display`/`Debug` in `mem/string/string.logos` + `mem/fmt/fmt.logos` | ⚠️ partial | metacall-based `format!`/`print!`. Rust-shape support types (`Formatter`/`FmtWrite`/`Arguments`/`fmt::Error`) declared in `logos.mem.fmt` (Formatter wraps `*mut String`, exposes `write_str`/`write_char`; spec fields width/fill/align/precision/sign_plus/alternate/zero_pad carried but legacy `fmt_pad` still drives padding). Trait named `FmtWrite` (not `Write`) to dodge cross-pkg collision with `std.io.write::Write` (B-mv-02). Display/Debug migration to `fn fmt(&self, f: &mut Formatter) -> Result<(), Error>` deferred — touches every primitive/stdlib impl + `format!`/`println!` metacalls. |
| `builders.rs` | — | ❌ TODO | `DebugStruct`/`DebugTuple`/`DebugList`/`DebugSet`/`DebugMap`. |
| `float.rs` / `nofloat.rs` | inline in `std/fmt/fmt.logos` | ⚠️ partial | Float formatting present; precision/scientific via `LowerExp`/`UpperExp` traits. |
| `num_buffer.rs` / `num.rs` | inline | ⚠️ partial | Integer formatting present. |
| `rt.rs` | — | ➖ | Internal runtime helpers for format-string parsing. |

#### core/future/
🔁 entire subtree. No `async`/`await`/`Future`/`Pin`/`IntoFuture` in Logos. `std-new/rt/fiber/future.logos` provides a fiber-based `FutureSlot<T>` with different shape.

#### core/hash/

| File | Logos | Status | Notes |
|---|---|---|---|
| `mod.rs` | `lang/hash/hash.logos` | ⚠️ partial | `Hash` + `Hasher` traits; missing `BuildHasher`/`BuildHasherDefault`/`SipHasher13`. |
| `sip.rs` | — | ❌ TODO | SipHasher implementation. (Logos has FxHasher only.) |

#### core/intrinsics/
🔁 entire subtree. Compiler builtins; exposed selectively via stdlib (`popcount_u64` etc.).

#### core/io/

| File | Logos | Status | Notes |
|---|---|---|---|
| `mod.rs` | `std/io/read/read.logos`, `std/io/write/write.logos` | ⚠️ partial | `Read`/`Write` traits exist; surface differs from core. |
| `borrowed_buf.rs` | — | ❌ TODO | `BorrowedBuf`/`BorrowedCursor`. |
| `error.rs` | — | ❌ TODO | `io::Error`/`ErrorKind`. |

#### core/iter/

| File group | Logos | Status | Notes |
|---|---|---|---|
| `mod.rs` (re-exports) | `lang/iter/iter.logos` | ⚠️ partial | See iter row above. |
| `range.rs` | `lang/range/range.logos` + `lang/iter` Range iterators | ⚠️ partial | `Step` trait absent. |
| `traits/iterator.rs` | `Iterator<Item>` in `lang/iter` | ⚠️ partial | Most methods ported as default-body + free-fn variants. |
| `traits/double_ended.rs` | `DoubleEndedIterator<Item>` | ✅ | — |
| `traits/exact_size.rs` | `ExactSizeIterator<Item>` | ✅ | — |
| `traits/collect.rs` | `IntoIterator` + `FromIterator` | ✅ | — |
| `traits/accum.rs` | `Sum` + `Product` | ✅ | — |
| `traits/marker.rs` | — | ❌ TODO | `FusedIterator`/`TrustedLen`. |
| `traits/unchecked_iterator.rs` | — | ➖ | Internal. |
| `adapters/map.rs` | `MapIter` + `iter_map` | ✅ | — |
| `adapters/filter.rs` | `FilterIter` + `iter_filter` | ✅ | — |
| `adapters/filter_map.rs` | `FilterMapIter` + `iter_filter_map` | ✅ | — |
| `adapters/scan.rs` | `ScanIter` + `iter_scan` | ✅ | — |
| `adapters/take.rs` | `TakeIter` | ✅ | — |
| `adapters/take_while.rs` | `TakeWhileIter` | ✅ | — |
| `adapters/skip.rs` | `SkipIter` | ✅ | — |
| `adapters/skip_while.rs` | `SkipWhileIter` | ✅ | — |
| `adapters/step_by.rs` | `StepByIter` | ✅ | — |
| `adapters/fuse.rs` | `FuseIter` | ✅ | — |
| `adapters/inspect.rs` | `InspectIter` | ✅ | — |
| `adapters/peekable.rs` | `PeekableIter` | ✅ | — |
| `adapters/enumerate.rs` | `EnumIter` | ✅ | — |
| `adapters/rev.rs` | `RevIter` | ✅ | — |
| `adapters/chain.rs` | `ChainIter` | ✅ | — |
| `adapters/zip.rs` | `ZipIter` | ✅ | — |
| `adapters/cycle.rs` | `CycleIter` | ✅ | — |
| `adapters/cloned.rs` | — | ❌ TODO | — |
| `adapters/copied.rs` | — | ❌ TODO | — |
| `adapters/flatten.rs` | — | ❌ TODO | `Flatten` / `FlatMap`. |
| `adapters/array_chunks.rs` | — | ❌ TODO | — |
| `adapters/map_windows.rs` | — | ❌ TODO | — |
| `adapters/intersperse.rs` | — | ❌ TODO | — |
| `adapters/by_ref_sized.rs` | — | ❌ TODO | — |
| `sources/once.rs` | `OnceIter` + `iter_once` | ✅ | — |
| `sources/empty.rs` | `EmptyIter` + `iter_empty` | ✅ | — |
| `sources/repeat.rs` | `RepeatIter` + `iter_repeat` | ✅ | — |
| `sources/once_with.rs` | — | ❌ TODO | — |
| `sources/repeat_n.rs` | — | ❌ TODO | — |
| `sources/repeat_with.rs` | — | ❌ TODO | — |
| `sources/from_fn.rs` | — | ❌ TODO | — |
| `sources/successors.rs` | — | ❌ TODO | — |
| `sources/from_coroutine.rs` / `generator.rs` | — | 🔁 | No coroutines. |

#### core/macros/

| File | Logos | Status | Notes |
|---|---|---|---|
| `mod.rs` | metacalls in `std/fmt/fmt.logos`, grammar handles `assert!`/`vec!`/etc. | ⚠️ partial | `panic!`/`assert!`/`assert_eq!`/`assert_ne!`/`unreachable!`/`println!`/`print!`/`eprintln!`/`eprint!`/`format!` work. `dbg!`/`todo!`/`writeln!`/`write!`/`matches!`/`include_str!`/`env!` absent. |
| `panic.md` | — | ➖ | doc-only. |

#### core/marker/

| File | Logos | Status | Notes |
|---|---|---|---|
| `variance.rs` | — | ❌ TODO | `PhantomCovariant`/etc. variance markers (unstable). |

#### core/mem/

| File | Logos | Status | Coretest | Notes |
|---|---|---|---|---|
| `mod.rs` | `mem/mem/mem.logos` | ⚠️ partial | `mem/test_harness_coretest_mem.logos`, `mem/test_harness_coretest_size_of.logos` | `swap`/`replace`/`take` (suffixed `_ref`) + `size_of`/`align_of` (compiler builtins). `forget`/`needs_drop`/`zeroed`/`uninitialized`/`discriminant`/`variant_count` absent. |
| `alignment.rs` | — | ❌ TODO | — | `Alignment` newtype. |
| `drop_guard.rs` | — | ❌ TODO | — | `DropGuard`. |
| `manually_drop.rs` | — | ❌ TODO | — | `ManuallyDrop<T>`. |
| `maybe_dangling.rs` | — | ❌ TODO | — | `MaybeDangling<T>`. |
| `maybe_uninit.rs` | `mem/uninit/uninit.logos` | ⚠️ partial | `mem/test_harness_coretest_mem_maybe_uninit.logos` | `new`/`uninit`/`zeroed`/`write`/`as_ptr`/`as_mut_ptr`/`assume_init`/`assume_init_read`/`assume_init_ref`/`assume_init_mut`. Storage is `value: T` (not union); `uninit()` is zero-fill via alloc+memset, not true-undef. Caveat with `T: Drop` documented in module header. Slice / array surface (`transpose`/`as_bytes`/`write_copy_of_slice`/etc.) deferred. |
| `transmutability.rs` | — | 🔁 | — | Unstable `TransmuteFrom`. |
| `type_info.rs` | `mem/any/any.logos` | 🔁 | — | Different shape (see `any.rs`). |

#### core/net/
🔁 entire subtree. Different shape; Logos exposes sockets via `std/io/net/`.

#### core/num/

| File | Logos | Status | Coretest | Notes |
|---|---|---|---|---|
| `mod.rs` | `lang/cmp/ord.logos` | ⚠️ partial | `num/test_harness_coretest_int_battery.logos`, `num/test_harness_coretest_*` (i32/i64/u32/u64) | Heavy method coverage; missing newtype wrappers + parse error types. |
| `int_macros.rs` / `uint_macros.rs` | `lang/cmp/ord.logos` (impl blocks per-type) | ⚠️ partial | per-type batteries (B104) | Logos enumerates explicitly (no `macro_rules!`). Coverage: arith/bitcount/rotate/swap_bytes/pow/checked/wrapping/overflowing/saturating/signum/abs. |
| `nonzero.rs` | `lang/num/nonzero.logos` | ⚠️ partial | — | `NonZero<T>` generic + per-type aliases (`NonZeroI8`..`NonZeroI64`, `NonZeroU8`..`NonZeroU64`, `NonZeroIsize`/`NonZeroUsize`). Methods: `new`/`new_unchecked`/`get`. Bound is `T: Default + Eq + Copy` (vs Rust's sealed `ZeroablePrimitive`). NO niche optimization — `Option<NonZero<i32>>` is 8 bytes (Rust: 4). Aliases work at type position only; static methods don't dispatch through aliases (must use `NonZero::<T>::new`). Method surface (`leading_zeros`/`count_ones`/`rotate_left`/checked_*/etc.) absent. |
| `wrapping.rs` | `lang/arith/arith.logos` (free fns `wrapping_add`/etc.) | ⚠️ partial | `num/test_harness_coretest_u64_wrapping_checked.logos` | Method form (`x.wrapping_add(y)`) ported; `Wrapping<T>` newtype absent. |
| `saturating.rs` | inline `saturating_add`/etc. methods | ⚠️ partial | `num/test_harness_coretest_i32_saturating.logos` | Method form ported; `Saturating<T>` newtype absent. |
| `niche_types.rs` | — | ➖ | Internal compiler-niche helpers. |
| `error.rs` | `lang/num/error.logos` | ✅ | — | `IntErrorKind` (Empty/InvalidDigit/PosOverflow/NegOverflow/Zero), `ParseIntError { kind }`, `TryFromIntError { kind }`. Both now `impl Error` (leaf — `has_source` returns false). |
| `f128.rs` / `f16.rs` | — | ❌ TODO | — | Logos has f32/f64 only. |
| `f32.rs` / `f64.rs` | `lang/math/math.logos` | ⚠️ partial | — | Transcendentals + constants via wrapper fns; method form on `f64` partial. |
| `float_parse.rs` | — | ❌ TODO | — | No `.parse::<f64>()` yet. |
| `traits.rs` | — | ❌ TODO | — | `Bounded`/`FromStr`/etc. trait surface for primitives. |
| `imp/` | — | ➖ | Internal LLVM-intrinsic shims. |
| `shells/` | — | ➖ | Per-type stub generators. |

#### core/ops/

| File | Logos | Status | Notes |
|---|---|---|---|
| `mod.rs` (re-exports) | `lang/ops/ops.logos` | ⚠️ partial | Sub-files below. |
| `arith.rs` | `lang/ops/ops.logos` | ✅ | `Add`/`Sub`/`Mul`/`Div`/`Rem`/`Neg` + `AddAssign`/`SubAssign`/`MulAssign`/`DivAssign`/`RemAssign`. `x op= y` sema-dispatches to `x.op_assign(y)` for user-typed structs (mutating). |
| `bit.rs` | `lang/ops/ops.logos` | ✅ | `BitAnd`/`BitOr`/`BitXor`/`Not`/`Shl`/`Shr` + `BitAndAssign`/`BitOrAssign`/`BitXorAssign`/`ShlAssign`/`ShrAssign`. |
| `function.rs` | `lang/ops/ops.logos` | ⚠️ partial | `Fn`/`FnMut`/`FnOnce` with variadic-pack `<A...>` + assoc `Output`. Bound-only use in fn-generics; closure capture working through the Fn-family infrastructure landed earlier. |
| `deref.rs` | `lang/ops/ops.logos` | ⚠️ partial | `Deref<Target>`/`DerefMut<Target>`. `*x` for user types dispatches to `.deref()` then re-derefs the resulting `&Target` (mirrors built-in Box auto-deref). DerefMut write-side dispatch deferred; method-autoderef-through-Deref-chain deferred. |
| `index.rs` | `lang/ops/ops.logos` | ⚠️ partial | `Index<Idx,Output>`/`IndexMut<Idx,Output>`. `a[i]` for user-typed structs dispatches to `.index(i)` (any Idx type accepted — non-integer keys like `m["k"]` once str-keyed maps land). IndexMut write-side deferred. |
| `index_range.rs` | — | 🔁 | Internal `IndexRange`. |
| `range.rs` | `lang/range/range.logos` (different shape) | 🔁 | Logos has typed `RangeI32`/`RangeI64`; no `Bound`/`RangeBounds`/`RangeInclusive`. |
| `drop.rs` | `lang/drop/drop.logos` | ✅ | `Drop` trait. |
| `try_trait.rs` | compiler-builtin `TRY_EXPR` desugar (B100) | 🔁 | `?` operator implemented in sema; no public `Try`/`FromResidual` traits to impl. From-coerce on heterogeneous-E supported. |
| `control_flow.rs` | `lang/ops/ops.logos` | ⚠️ partial | `ControlFlow<B, C>` enum + `is_continue`/`is_break`. Used by `Iterator::try_fold` and `try_for_each` (added). Rust default-parameterises `C = ()`; Logos doesn't carry default generic args so the continue payload is always spelled explicitly. `?`-operator routing through ControlFlow (Rust's Try/FromResidual on it) deferred — `?` lowering is compiler-builtin (TRY_EXPR) and doesn't consult traits yet. |
| `unsize.rs` | compiler-builtin (Phase 1B) | 🔁 | `CoerceUnsized`/`DispatchFromDyn` are intrinsics; coercion works for `&[T;N]`→`&[T]`, `&T`→`&dyn Trait`. |
| `async_function.rs` | — | 🔁 | No `async fn`. |
| `coroutine.rs` | — | 🔁 | No coroutines. |

#### core/os/
🔁 entire subtree. Logos `std-new/os/` has process/host info; no `darwin/` shims.

#### core/panic/

| File | Logos | Status | Notes |
|---|---|---|---|
| `location.rs` | `lang/panic/panic.logos` | ⚠️ partial | `Location {file,line,column}` + `location(...)` constructor. No `Location::caller()` (needs `#[track_caller]` auto-threading). |
| `panic_info.rs` | `lang/panic/panic.logos` | ⚠️ partial | `PanicInfo {message, location: Option<Location>}`. Hook doesn't auto-populate location (see above). |
| `unwind_safe.rs` | — | ❌ TODO | `UnwindSafe`/`RefUnwindSafe`/`AssertUnwindSafe`. |

#### core/pin/
🔁 entire subtree.

#### core/prelude/
❌ TODO. No implicit prelude in Logos yet.

#### core/ptr/

| File | Logos | Status | Notes |
|---|---|---|---|
| `mod.rs` | `lang/ptr/ptr.logos` (+ compiler-builtin pointer ops) | ⚠️ partial | `null`/`null_mut`/`read`/`write`/`copy`/`copy_nonoverlapping`/`swap`/`replace`/`drop_in_place`/`eq`/`addr_eq` ported. Volatile / unaligned / provenance / slice-from-raw-parts variants pending. |
| `const_ptr.rs` / `mut_ptr.rs` | — | ⚠️ partial | Method-form on raw pointers absent (use deref + offset). |
| `non_null.rs` | `lang/ptr/ptr.logos` (`NonNull<T>`) | ⚠️ partial | `new`/`new_unchecked`/`dangling`/`from_ref`/`from_mut`/`as_ptr`/`as_ref`/`as_mut`/`cast`/`add`/`sub`/`offset` ported. Address/provenance/byte-offset/aligned-cast variants pending. |
| `unique.rs` | — | ❌ TODO | `Unique<T>` (internal). |
| `metadata.rs` | compiler-builtin (DST metadata, Phase 1B-14) | 🔁 | `dst_from_raw_parts` builtin instead of `Pointee::Metadata`. |

#### core/range/

| File | Logos | Status | Notes |
|---|---|---|---|
| `iter.rs` / `legacy.rs` | `lang/range/range.logos` | 🔁 | Different shape (typed `RangeI32`/`RangeI64`). |

#### core/slice/

| File | Logos | Status | Notes |
|---|---|---|---|
| `mod.rs` | `lang/iter/iter.logos` (`SliceIter`) + grammar `&[T]` | ⚠️ partial | Iteration works; method surface (`split_at`/`chunks`/`windows`/`sort`/`binary_search`) absent. |
| `iter.rs` / `iter/` | `lang/iter/iter.logos` (`SliceIter`) | ⚠️ partial | Forward iter only — no `IterMut`/`Chunks`/`Windows`/`Split`. |
| `cmp.rs` | language built-in for primitives | ❌ TODO | Generic `PartialEq` for `[T]` not registered. |
| `ascii.rs` | — | ❌ TODO | `[u8]::eq_ignore_ascii_case`, escape iter. |
| `index.rs` | — | ❌ TODO | `SliceIndex<I>`. |
| `memchr.rs` | — | ❌ TODO | Byte search primitives. |
| `raw.rs` | compiler-builtin (slice ptr+len) | 🔁 | — |
| `rotate.rs` | — | ❌ TODO | `[T]::rotate_left`/`rotate_right`. |
| `sort/` | — | ❌ TODO | Sort algorithms. |
| `specialize.rs` | — | ➖ | Internal specialization. |

#### core/str/

| File | Logos | Status | Notes |
|---|---|---|---|
| `mod.rs` | `lang/str/str.logos` | ⚠️ partial | Free-fn surface; method-form on `str` mostly via `mem/string/string.logos` `String`. |
| `converts.rs` | — | ⚠️ partial | `from_utf8`/`from_utf8_unchecked` via `lang/str/utf8.logos` helpers. |
| `count.rs` | — | ➖ | Internal char-count optimization. |
| `error.rs` | — | ❌ TODO | `Utf8Error`/`ParseBoolError`. |
| `iter.rs` | — | ❌ TODO | `Chars`/`CharIndices`/`Bytes`/`Lines`/`SplitWhitespace`. |
| `lossy.rs` | — | ❌ TODO | `Utf8Chunks`. |
| `pattern.rs` | `lang/str/split.logos` (basic) | ⚠️ partial | `Pattern` trait absent; `Splitter`/`ByteSplitter` only. |
| `traits.rs` | — | ❌ TODO | `FromStr`. |
| `validations.rs` | `lang/str/utf8.logos` | ⚠️ partial | UTF-8 validation. |

#### core/sync/

| File | Logos | Status | Notes |
|---|---|---|---|
| `mod.rs` | — | ➖ | re-exports. |
| `atomic.rs` | `lang/atomic/atomic.logos` | ⚠️ partial | No memory `Ordering` — Logos atomics are seq_cst by default; no `AtomicI8`/`AtomicI16`. |
| `sync_view.rs` | — | 🔁 | Unstable view types. |

#### core/task/
🔁 entire subtree (`Poll`/`Waker`/`Context`/`RawWaker`/`Ready`). Fiber model instead.

#### core/unicode/

| File | Logos | Status | Notes |
|---|---|---|---|
| `mod.rs` | `lang/char/char.logos` (ASCII only) | ⚠️ partial | No general-category / script tables. |
| `printable.rs` / `unicode_data.rs` | — | ❌ TODO | Generated Unicode tables. |
| `printable.py` | — | ➖ | Build-time codegen. |

## Logos-only modules (no Rust core counterpart)

These exist because Logos's runtime/data model differs; track them
for completeness but no Rust port is meaningful.

### Lang
- `stdlib/lang/hermes/{anyval,datatag,mem_holder,own,relptr,tags,typetag,view,zone}` — Hermes datatype/storage/view fabric (AnyVal, OView, Zone<M>, TypeTagSystem).
- `stdlib/lang/logos.module` — language module manifest.

### Mem
- `stdlib/mem/hermes/{alloc,array,check,clone,ctr,decimal,document,equal,fabric,hashing,hbs_read,hbs_write,map,objectmap,parser,pat,registry,release,relptr_traits,scalar,string,stringify,tag_system,typed_value,view}` — Hermes serialization / immutable-doc / object-map infrastructure (Logos's mini-Memoria).
- `stdlib/mem/encoding/{base64,csv,hex,json}` — encoding helpers. JSON has writer + escape + validate; no full parser yet.
- `stdlib/mem/collections/{btree,deque,hashmap,set}` — `BTreeMap<K,V>` (Ord-based), `VecDeque<T>`, `HashMap<K,V>` (FxHasher), `HashSet<K>`. Iter + basic methods; no `entry` API.

### Std (legacy / being retired per `std-new` plan)
- `stdlib/std/compiler/{metaprog,tokens}` — `derive_clone`/`derive_branch_node`/AST helpers/format!/print! metacalls. **Phase A C4** completed; ships as `.hermes0` blobs.
- `stdlib/std/data/persistent` — mini-Memoria persistent data (B+tree, Snap/Store, COW). Logos-only.
- `stdlib/std/io/{buffered,bytes,fs,http,linux/uring,net/{tls,url},pipe,read,write}` — full I/O surface (sockets, fs, http, io_uring, TLS, URL parsing). No core equivalent.
- `stdlib/std/lang/text/regex` — regex engine.
- `stdlib/std/process/process.logos`, `stdlib/std/sync/sync.logos`, `stdlib/std/fmt/fmt.logos` — `process`/`sync`/`fmt`.

### Std-new (Phase A migration target)
- `stdlib/std-new/crypto/crypto.logos` — crypto primitives.
- `stdlib/std-new/env/{env,args}.logos` — env vars, argv parsing (`has_flag`/`get_option`/`positional`).
- `stdlib/std-new/log/log.logos` — leveled logging.
- `stdlib/std-new/os/{os.logos,unix/signal/}` — pid/uid/hostname/arch + Unix signals.
- `stdlib/std-new/random/random.logos` — concrete `Rng` (xorshift-style) + OS entropy.
- `stdlib/std-new/rt/fiber/{fiber,future,reactor,sync,thread}.logos` — fiber scheduler, FutureSlot, reactor, Latch/Chan, OS thread JoinHandle.
- `stdlib/std-new/testing/testing.logos` — `TestCtx` test runner (parallel to `#[test]` harness).
- `stdlib/std-new/thread/thread.logos` — OS-thread spawn/join.
- `stdlib/std-new/time/{time.logos,datetime/datetime.logos}` — `Duration`/`Instant`/`SystemTime` + calendar `DateTime`.

### Rt (C/asm shims)
- `stdlib/rt/{atomic_ops.S,fiber_ctx.S,env.c,fmt_native.c,fs_meta.c,metaprog_stubs.c,test_recovery.c,thread_uring.c}` — assembly + C shims linked into runtime. No core equivalent.

## Coretest coverage summary (file count)

- `bool/`: 3
- `casts/`: 1 (`convert_from`)
- `cell/`: 1 (`unsafe_cell`, B100)
- `char/`: 3
- `clone/`: 1
- `cmp/`: 5
- `iterators/`: 41
- `macros/`: 1 (`assert_macros`)
- `mem/`: 2 (`mem`, `size_of` — B101)
- `num/`: 16 (i32/i64/u32/u64 batteries — B103/B104)
- `option/`: 11
- `option-result/`: 1 (`qmark_from`, B100)
- `result/`: 4
- `tuple/`: 1

**Total: 91 coretest files.** Provenance for each is in `tests/imported/RUSTC-PROVENANCE.md`.

## High-priority gaps (suggested order)

Cross-referenced from individual rows above:
1. `core::ops::{Fn,FnMut,FnOnce}` — blocks closure trait surface; affects iter `.map`/etc. taking closures vs `fn` ptrs.
2. ~~`core::ops::{Deref,DerefMut,Index,IndexMut}`~~ — landed 2026-05-18. Trait shape uses type-params `Deref<Target>`/`Index<Idx,Output>` (Logos doesn't carry assoc-types outside the Fn-family). `*x` and `a[i]` for user-typed structs sema-dispatch to `.deref()`/`.index(i)` mirroring the unary-op pattern (call returns `&T`, sema then re-derefs). Idx accepts non-integer keys for `a[i]`. Deferred: DerefMut/IndexMut write-side dispatch (write today via explicit `*x.deref_mut() = v` / `*a.index_mut(i) = v`), method-autoderef-through-Deref-chain (call `(*box).m()` explicitly), and `*Assign` operator family.
3. ~~`core::mem::MaybeUninit`~~ — initial port landed (mem/uninit, B105). Drop-T variant + slice/array surface remain.
4. ~~`core::ptr` standalone `read`/`write`/`copy_nonoverlapping` / `NonNull`~~ — initial port landed (`lang/ptr`, follow-up to B105). Volatile/unaligned/provenance variants remain.
5. ~~`core::num::NonZero` + `core::num::error::*`~~ — landed (`lang/num/nonzero`, `lang/num/error`, follow-up to B105). NonZero method surface (count_ones/leading_zeros/checked_*) deferred.
6. ~~`core::iter` adapters: `from_fn`/`repeat_n`/`successors`~~ — landed. `cloned`/`copied` intentionally omitted (Logos iters yield by value). `successors` migrated to `<T, F: FnMut(T)->Option<T>>` after Fn-family fixes; capturing-closure form still crashes (sibling of MapIter chain bugs). `flatten` deferred — needs IntoIterator-on-Item constraint.
7. ~~`core::str::{Chars,CharIndices,Bytes,Lines}`~~ — landed 2026-05-18. Chars existed prior; CharIndices/Bytes/Lines added (Lines handles CRLF). `Pattern` trait still TODO (heavy — parameterises split/find/contains across str/char/closure-pattern variants).
8. ~~`core::slice` method surface (`split_at`/`chunks`/`windows`/`sort`/`binary_search`)~~ — landed 2026-05-18 (`lang/slice/slice.logos`). split_at uses raw ptr+len pairs (slice-as-struct-field segfault); sort uses raw ptr+len (`&mut arr` → `&mut [T]` coercion doesn't fire for var-bound arrays). `sort_by` / `SliceIndex` trait still TODO.
9. ~~`core::cell::{Cell,RefCell,OnceCell,LazyCell}`~~ — landed 2026-05-18. Cell (Copy), RefCell (Ref/RefMut + Drop counter), OnceCell + LazyCell (Default-bound storage, fn-ptr init). `Ref::map`/`filter_map` and `try_borrow*` Result-extraction deferred behind [[baghunt-match-arm-binding-no-drop]].
10. ~~`core::sync::atomic::Ordering`~~ — landed 2026-05-18. Enum + `_ordered` method variants on AtomicI32 (Relaxed/Acquire/Release/AcqRel/SeqCst). All variants currently lower to seq-cst on x86 (TSO + LOCK-RMW already provides it); weaker-memory backends (AArch64/RISC-V) would route to dmb / lr-sc as backend-codegen work. Other Atomic* types need the same `_ordered` overloads (mechanical, deferred).
11. ⚠️ `core::fmt::{Formatter,Arguments,Write}` — support types landed 2026-05-18 in `lang.mem.fmt` (Formatter wraps `*mut String`, exposes write_str/write_char, carries width/fill/align/precision/sign_plus/alternate/zero_pad spec fields). Trait named `FmtWrite` to dodge `std.io.write::Write` cross-pkg collision (B-mv-02). Display/Debug migration from `fn fmt(self, &mut String)` to `fn fmt(&self, f: &mut Formatter) -> Result<(), Error>` deferred — touches every primitive/stdlib impl + `format!`/`println!`/`print!`/`eprintln!`/`eprint!`/`panic!`/`assert!`/`assert_eq!`/`assert_ne!` metacalls + the `fmt_pad` driver itself. Follow-up: Formatter.pad() method to replace standalone fmt_pad.
12. ~~`core::panic::{Location,PanicInfo,catch_unwind}`~~ — landed 2026-05-18. `Location {file,line,column}` (explicit construct only — no `#[track_caller]` auto-threading), `PanicInfo {message, location: Option<Location>}`, `catch_unwind(f: fn()) -> Result<(), str>` reuses the same setjmp recovery the `#[test]` harness installs (`logos_run_with_recovery` + `logos_panic_last_msg{,_len}`). Divergence from Rust: fn-ptr (no closure capture / no R / no Box<dyn Any> payload). `UnwindSafe`/`RefUnwindSafe`/`AssertUnwindSafe` marker traits still TODO.
