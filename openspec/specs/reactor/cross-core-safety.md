# Cross-Core Lambda Safety Analysis

The user requires a mechanism to ensure that lambdas sent to other cores via `send_to(cpu, lambda)` do **not** access the sender's thread-local memory.

## The Problem
Seastar's `submit_to` takes a lambda. If that lambda captures:
1.  **References** (`&x`): It refers to the sender's stack. By the time the receiver runs it, the sender might have returned (stack UAF) or be mutating `x` (Data Race).
2.  **Pointers** (`T* p`): If `p` points to sender-local heap, the receiver accesses foreign memory without ownership/locks.
3.  **This pointer** (`[=]` in member function): Captures `this`. Same as pointer issue.

## Solution 1: C++ Type System (Concepts)
We can define a concept `Sendable<F>`:
```cpp
template <typename T>
concept Sendable = std::is_trivially_copyable_v<T> || ...; // Hard to define "Safe"
```
**Limitations**:
-   A lambda capture is just a member variable. `struct { int* p; }` is trivially copyable but unsafe.
-   C++ cannot inspect *what* an `int*` points to (stack vs shared).
-   Wrapping every safe pointer in `SharedPtr<T>` helps, but we can't prevent users from passing raw pointers via `reinterpret_cast` or just plain mistakes.

## Solution 2: Clang Plugin (Recommended)
Since we are already using a Clang plugin for Green Fibers, we can extend it to enforce **"Green Safety"**.

### Safety Rules
We define a clang attribute `[[logos::cross_core]]` (implied for `smp::submit_to` arguments). For any lambda passed to such a function:

1.  **No Reference Captures**: `[&]` and `[&x]` are strictly forbidden.
2.  **No Raw Pointer Captures**: `int*` or `T*` in the capture list are forbidden.
    -   *Exception*: Pointers explicitly marked `[[logos::safe_ptr]]` (for shared immutable data).
3.  **Allowed Captures**:
    -   **Values** (PODs): `int`, `double`, `struct { int x; }`.
    -   **Safe Handles**: Types marked `[[logos::sendable]]` (e.g., `SharedDocumentHandle`, `intrusive_ptr<ImmutableBlock>`).
4.  **No `this` Capture**: Implicit or explicit `this` capture is forbidden unless `*this` is proven to be a Sendable Handle itself.

### Implementation Strategy
The plugin visits the `CallExpr` to `submit_to`.
-   Inspects the argument (Lambda).
-   Iterates over `LambdaCapture` list.
-   Checks the `Type` of each captured variable.
-   Emits a **Compile Error** if a rule is violated.

## Level 2: Call Graph / API Restrictions

The user correctly identified that restricting captures is necessary but insufficient. We must also restrict **what the lambda can call**.

**The Risk**: A lambda might capture nothing, but call a function `UnsafeGlobalAccess()` or `BlockThread()`.

**Plugin Verification**:
1.   **Constraint**: Cross-core lambdas should effectively be "pure" regarding the *sender's* state, and "safe" regarding global state.
2.  **Analysis**: The plugin must perform a shallow **Call Graph Analysis**.
    -   Any function called by the lambda must be marked `[[logos::safe_api]]` (or `[[gnu::pure]]`, `constexpr`, etc.).
    -   Alternatively, the plugin can inspect the callee recursively (up to a depth) to check for:
        -   Access to Global Mutable Variables.
        -   Usage of banned APIs (e.g., standard `mutex`, `sleep`).
3.  **Optimization**: To avoid deep recursion, we can require that any function called from a cross-core context must itself be explicitly annotated or trivially safe.

## Conclusion
While C++ Concepts can filter *obviously* bad types (like `std::unique_ptr` without move), they cannot distinguish a pointer-to-local from a pointer-to-shared, nor can they audit the function body. **A Clang Plugin is required** to enforce:
1.  **Data Safety**: Strict capture whitelisting (Value only, Safe Handles).
2.  **Code Safety**: Restricting the call graph to "Safe API" functions only.

