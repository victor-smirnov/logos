# Logos Data Structures (Extracted from Memoria)

## 1. Hermes: The Canonical Object Format
Logos utilizes the **Hermes** format for representing both its own Abstract Syntax Tree (AST) and for handling unstructured/semi-structured data within the language.

### 1.1 Key Properties of Hermes in Logos
*   **Relocatability**: Hermes uses relative pointers, making objects inherently relocatable. A Logos object graph can be serialized, mmap'ed, sent over the network, or shared with hardware accelerators without pointer swizzling.
*   **Zero-Copy Integration**: Because Logos objects *are* Hermes objects, reading from a Memoria backend container is a zero-copy operation.
*   **Tagged Memory**: Every object has a type-tag (1 to 32 bytes). In Logos, these tags (often hashes of the type declaration) facilitate $O(1)$ runtime type checking and dynamic dispatch.
*   **Immutability**: Logos objects can be marked immutable at runtime, allowing safe, lock-free sharing between threads.

## 2. Advanced Datatypes & Collections
Standard languages offer generic collections (e.g., `std::vector<T>`). Logos, via Hermes, offers both generic and *structurally typed* collections.

### 2.1 The `TinyObjectMap`
A highly optimized map for short integer/string keys to objects, fitting in just 16 bytes overhead.
*   **Relevance to Logos**: This map is the foundational structure for dynamic objects or C-like structs computed at runtime without relying on statically compiled C++ objects. It allows Logos to behave dynamically while retaining memory efficiency.

### 2.2 Compressed Containers & Semantic Graphs
For large, highly structured data, Logos defaults to Memoria's **Containers**.
*   **Semantic Graphs**: Logos has first-class syntax for defining Knowledge/Semantic Graphs (RDF-like relation triples). 
*   **Hardware Alignment**: Operations on these advanced graphs and variable-length integers are specifically designed to be easily offloaded to Memoria's Hardware Accelerators (MAA).

## 3. Parametric "Datatypes"
In Logos, parametrization goes beyond compile-time generics.
*   A type like `Decimal(10, 2)` does not just generate a new distinct compile-time type. The constructor `(10, 2)` represents shared state for all instances of that `Decimal`.
*   Logos bridges the gap between static C++ templates and dynamic runtime objects by treating "Datatypes" as first-class objects themselves.
