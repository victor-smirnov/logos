# Logos: Language Design Specification (Draft)

## 1. Introduction
This document outlines the syntax, semantics, and design principles of the **Logos** programming language. Logos is a domain-specific companion to C++, intended for high-complexity data modeling and complex event processing. Given its primary role as an AI-generated language, the design optimizes for **unambiguous parsing, strict static verification, and structural correspondence to the underlying Memoria `Hermes` data model**.

## 2. Theoretical Foundations
Logos is not just a language but an operationalization of the **Self-Applicable Turing Machine** concept. It allows developers (and AI) to build "Intrapersonal Intelligence" into autonomous agents. 
- The language inherently supports **reflection and introspection**. Programs can observe their own runtime state (e.g., execution time, confidence metrics) just as they observe external data.
- **Forward-Chaining Execution (RETE)**: Programs are defined as sets of reactive rules (`pattern -> reaction`) rather than strict top-down imperative sequences.

## 3. Core Syntax & Semantics

### 3.1 AI-First Generability
- **S-Expression / AST-centric text format**: While readable by humans, the textual format of Logos directly maps $1:1$ to a Hermes Document AST. There are no complex operator precedences or context-dependent parsing rules.
- **Explicit Context**: Macros or hidden global variables are strictly controlled. When AI generates Logos, it has all the necessary type definitions and trait implementations explicitly available in the local scope.

### 3.2 Dependent Types & Verification
Logos incorporates Dependent Types to shift runtime checks into compile-time proofs.
- An AI generating Logos code will emit both the algorithm and the type-level proof of its correctness.
- **Example**: An array indexing operation requires the compiler to prove that the index is within bounds:
  ```logos
  // Pseudo-code syntax for dependently typed array access
  Array<Int, size: 10> my_array;
  Int index = 5;
  // AI provides proof 'P' that index < size
  return my_array.get(index, P);
  ```
- **Graph Safety**: Pointer/Reference types into Semantic Graphs carry safety proofs (e.g., "target node is guaranteed to exist in the current transaction view").

### 3.3 Ownership and Borrowing (Rust-like)
Memory safety in Memoria's decentralized and highly parallel environments relies on strict ownership semantics.
- `Own<T>` vs `Ref<T>` vs `MutRef<T>`.
- The Borrow Checker enforces rules statically. The AI is highly adept at satisfying lifetime constraints mathematically.
- This entirely eliminates garbage collection overhead for Logos runtime execution, except where underlying Hermes documents utilize their specific copying GC.

### 3.4 Safe Structs vs. Unsafe Classes
Memory management in Logos explicitly separates stack-allocated safe types from heap-allocated manual types:
- `struct`: Stack-allocated, passed by value (or by safe reference `&T` / `&mut T`). Interactions with `struct` types are verified by the Borrow Checker and are **100% safe by default**.
- `class` (and `new` keyword): Heap-allocated objects that yield raw pointers (`*mut T`). Because their memory must be managed manually (using the `delete` keyword), accessing their fields or invoking methods via implicit dereferencing requires an explicit `unsafe { ... }` context. 
- In the future, safe smart pointers (e.g., `Box<T>`, `Rc<T>`) will be introduced to allow safe interactions with heap-allocated objects without requiring `unsafe`.

## 4. First-Class Data Structures
Logos code natively manipulates Memoria's powerful data primitives:
- Native syntax for `TinyObjectMap` dynamic objects.
- First-class syntax for defining Knowledge Graph triples (RDF-like properties).
- Variables map seamlessly to `Int56`, maintaining standard sizes without boxing.

## 5. Execution Model
- **M-Code Target**: Logos compiles down to the generalized M-Code (which itself is structured as Hermes docs).
- **Execution Strategy**: The compiler can output to an interpreter, directly transpile to optimized C++, or JIT to LLVM/MLIR for query execution.
