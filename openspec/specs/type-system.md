# Logos Type System & Safety Specification

This document consolidates the Memory Model, Execution Model, and Type System requirements to serve as the **specification for the Logos Clang Plugin**.

## 1. Memory Model

The memory is strictly categorized into **Mutable Thread-Local** and **Immutable Shared** domains to enable a shared-nothing execution model with efficient zero-copy data sharing.

### 1.1. Partitioned Heap (Mutable)
*   **Ownership**: Strictly Thread-Local (Shard-Local).
*   **Safety Invariant**: A pointer to Mutable Heap `T*` **MUST NOT** be passed to another core.
*   **Detection**: Address-based. `(ptr >> SHIFT) == core_id`.

### 1.2. Shared Immutable Memory
*   **Ownership**: Shared across all cores. Read-Only.
*   **Safety Invariant**: 
    1.  Data is chemically immutable (e.g., const pages).
    2.  Lifecycle is managed by **Handle** types (ref-counted or GC'd).
    3.  Raw pointers `T*` to this memory are allowed *only* if wrapped in a **Safe Handle**.
*   **Categories**:
    1.  **Shared Blocks**: 4K-aligned raw buffers (B-Tree Nodes).
    2.  **Shared Documents**: Variable-sized, offset-based, pointer-free (FlatBuffers-like).

## 2. Cross-Core Communication (SMP)

Inter-core communication happens via **Message Passing**.

*   **Mechanism**: SPSC Ring Buffers (Arena).
*   **Payload**: `std::function<void()>` (or equivalent lambda).
*   **API**: `smp::submit_to(cpu_id, lambda)`.

### 2.1. The Safety Challenge
When a lambda is sent to another core:
1.  It is constructed on Core A (Sender).
2.  It is executed on Core B (Receiver).
3.  **Danger**: Capturing pointers/references to Core A's stack or mutable heap leads to Race Conditions or UAF.

## 3. Type System Extensions (Clang Plugin)

To enforce the safety invariants, the Clang Plugin will implement a "Green/Red" style Safety Analysis, specifically for **Cross-Core Safety**.

### 3.1. Attributes / Traits

| Mechanism | Target | Behavior |
| :--- | :--- | :--- |
| `[[logos::cross_core]]` | Function Parameter | The argument (lambda) is subject to **Strict Capture Verification**. |
| `[[logos::safe_api]]` | Function | Marks a function as safe to call from cross-core lambdas (side-effect free or explicitly thread-safe). |
| `template<typename T> struct is_sendable` | Type Trait | **Specialized** for safe types. The plugin checks `is_sendable_v<T>` (or equivalent concept). |

### 3.2. Validation Rules

When the plugin encounters a call to a function with a `[[logos::cross_core]]` parameter (e.g., `submit_to`), it validates the argument expression (Lambda):

#### **Rule A: Data Safety (Captures)**
The Lambda's capture list is inspected.
1.  **BANNED**: Reference Captures `[&]`, `[&var]`. (Prevents Stack UAF).
2.  **BANNED**: Raw Pointers `T*`, `T&`. (Prevents Mutable Heap Races).
3.  **BANNED**: Implicit `this` capture.
4.  **ALLOWED**: Trivial Value Types (PODs: `int`, `struct Point`).
5.  **ALLOWED**: Types where `is_sendable<T>::value` is true (e.g., Handles).


#### **Rule B: Code Safety (Call Graph)**
The Lambda's body is inspected (Shallow Analysis).
1.  **Constraint**: Logic must be "Pure" with respect to the Sender's state.
2.  **Function Calls**: Calls to other functions are allowed **ONLY IF**:
    *   The callee is `constexpr`.
    *   The callee is marked `[[logos::safe_api]]`.
    *   The callee is a standard library function known to be safe (math, algorithms on local data).
3.  **Global Access**: Write access to global mutable variables is **BANNED**.

### 3.3. Inference
*   **Auto-Sendable**: Structs composed entirely of `Sendable` types are implicitly `Sendable`.
*   **Lambdas**: A lambda is implicitly `Sendable` if all its captures are `Sendable`.

## 4. Example

```cpp
// 1. Safe Handle Definition
struct SharedDocHandle {
    intrusive_ptr<SharedDoc> ptr; 
};

// Mark as Sendable via Trait
template<> struct logos::is_sendable<SharedDocHandle> : std::true_type {};

// 2. Mutable State
int local_counter = 0;

// 3. Usage
void dangerous_code() {
    SharedDocHandle doc = load_doc();
    
    // ERROR: Capture of local reference &local_counter
    smp::submit_to(1, [&local_counter, doc]() { 
        local_counter++; 
    });

    // ERROR: Call to unsafe function
    smp::submit_to(1, [doc]() {
        unsafe_global_mutation(); 
    });

    // OK: Captures safe handle (value) and safe POD (value)
    smp::submit_to(1, [doc, x=42]() {
        doc->process(x); 
    });
}
```
