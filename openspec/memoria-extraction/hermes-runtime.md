# Hermes as Logos Runtime Foundation

## 1. Overview

Hermes is the data substrate on which Logos operates. Every Logos entity -- code, data, types, metadata -- is a Hermes object. This document extracts the specific Hermes capabilities that Logos depends on and identifies gaps.

## 2. Core Capabilities Used by Logos

### 2.1 Arena Allocator
- Contiguous memory segments with relative pointers
- Relocatable without pointer swizzling
- Suitable for: mmap, IPC, network transfer, hardware accelerators, executable embedding
- 64-bit internal pointers (even on 32-bit architectures)
- Copying GC for compaction

**Logos usage:** Every M-Code module/assembly is a Hermes document. Code can be memory-mapped from storage, shared between processes, or loaded into accelerators without serialization overhead.

### 2.2 Tagged Type System
- 1-32 byte type tags allocated before each object
- Short tags (1 byte) for common types fit into alignment gaps -- zero overhead
- 8/16-bit tag hash in object pointers for integrity checking (planned)
- 56-bit integers/identifiers (tag + value fit in 64-bit slot)
- Monomorphic generics
- Datatypes as first-class objects (collections of datatypes, processing, type hash computation)

**Logos usage:** M-Code type references use 192-256 bit hash codes of normalized type declarations. Type dispatch in the interpreter uses tag-based dispatch. 56-bit integers are the standard small integer type.

### 2.3 TinyObjectMap
- 16-byte overhead per map
- Keys: integers 0..51
- O(1) lookup via PopCnt instruction
- Values up to 56 bits embedded in hash array (no pointer indirection)
- No empty slots in hash array

**Logos usage:** Primary representation for M-Code entities (Method, Argument, Statement, etc.). The existing `dsl::Method` and `dsl::Argument` already use TinyObjectMap via `TinyObjectBase`. Every code model node will be a TinyObjectMap with numbered fields.

### 2.4 ObjectArray and TypedArray
- ObjectArray: heterogeneous, each element is Object (via ERelativePtr)
- TypedArray<DT>: homogeneous, optimized memory layout
- Both support: get, push_back, set, remove, for_each, stringify

**Logos usage:** Code blocks are ObjectArrays of statements. Argument lists are ArrayOf<Argument>. Typed arrays for homogeneous data in constant pools.

### 2.5 ObjectMap and TypedMap
- ObjectMap: Varchar → Object (string-keyed)
- TypedMap: arbitrary key DT → Object
- Both support: put, get, remove, iterate

**Logos usage:** Symbol tables, module exports/imports, metadata dictionaries.

### 2.6 Datatypes with Constructors
- `MyType<Parameter>` -- parametric (like C++ templates)
- `Decimal(10, 2)` -- constructor parameters (shared state for all instances)
- Constructor does not create new type -- dynamic specialization
- Type hash from normalized declaration

**Logos usage:** Logos types map to Hermes datatypes. Dependent type parameters expressed via constructor arguments. Type checking uses hash comparison.

### 2.7 String Externalization (Text Format)
- All Hermes objects have canonical text representation
- JSON-like syntax with types: `@Array<Int> = [1, 2, 3]`
- Type-directed parsing: `"19345..."@Decimal(50,3)`
- Text format is a DSL (not static format like JSON)

**Logos usage:** M-Code text format for human-readable code representation. Debug output. REPL interaction.

### 2.8 HermesPath
- JMESPath-like query language for Hermes documents
- Already implemented and working

**Logos usage:** Querying code model (find all functions with annotation X, find all uses of type Y). Metaprogramming support.

### 2.9 Template Engine
- Jinja-like syntax
- HermesPath as expression language

**Logos usage:** Code generation in metaprogramming. Template-based DSL construction.

### 2.10 Schema Processor
- Declarative and imperative constraints
- Interactive mode (language-server-like)

**Logos usage:** Type checking, M-Code validation, schema enforcement for code model documents. Language server for Logos IDE integration.

### 2.11 Profiles
- pico: fixed-size arrays, TinyObjectMap, Int56, strings
- nano: adds Int56→Object map
- micro: adds all integer/float types, semantic graph
- basic: adds dynamic (growable) containers

**Logos usage:** Different Logos runtime profiles for different deployment targets. Embedded (pico/nano) vs full (basic+).

## 3. Gaps and Extensions Needed

### 3.1 Code Model Schema
Need to define Hermes schemas for all M-Code entities:
- Module, Assembly, Class, Function, Rule, Statement, Expression
- Type reference (hash + optional source declaration)
- Code registry entries (native function bindings)
- Currently only Method and Argument exist as stubs

### 3.2 Bytecode Representation
M-Code needs a compact binary representation for interpretation:
- Opcodes encoded in Hermes (TypedArray<UInt8> or similar)
- Operand references to local variables, constants, types
- Must be Hermes-native (not a separate format)

### 3.3 Code Registry
Binding descriptors for native C++ functions:
- Function signature (parameter types, return type)
- Physical address or symbol name
- Calling convention metadata
- Safety annotations
- Auto-generatable via MBT from C++ sources

### 3.4 Ownership/Lifetime Annotations
Hermes does not currently model ownership semantics:
- Need metadata fields for Own/Ref/MutRef on type references
- Lifetime parameters on function signatures
- Borrow checker operates on M-Code AST, not Hermes types directly

### 3.5 Dependent Type Proofs
- Proof objects as Hermes documents (attached as metadata)
- Proof verification as schema validation
- Integration with type checker

### 3.6 RETE Working Memory Interface
- Need efficient mapping: Hermes facts ↔ RETE alpha/beta network
- Pattern representation in Hermes
- Working memory as Hermes document or Memoria container
