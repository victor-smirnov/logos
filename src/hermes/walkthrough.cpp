// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Victor Smirnov
// Logos project — https://github.com/victor-smirnov/logos
//
// Hermes API Walkthrough
// ======================
// Interactive tour of the Hermes data format — run under a debugger.
// Set breakpoints at the "STOP" comments to inspect values at each stage.
//
// Build:  cmake --build build --target hermes_walkthrough
// Debug:  gdb ./build/src/hermes/hermes_walkthrough
//         (gdb) break main
//         (gdb) run

#include <logos/hermes/access.hpp>
#include <logos/hermes/document.hpp>
#include <logos/hermes/text_parser.hpp>
#include <logos/hermes/stringify.hpp>
#include <logos/hermes/type_ops.hpp>
#include <logos/hermes/binary_codec.hpp>
#include <logos/hermes/path.hpp>
#include <logos/hermes/template.hpp>
#include <logos/hermes/compound_types.hpp>

#include <print>
#include <string>

using namespace logos::hermes;

// ============================================================================
// 1. Documents & Ownership Model
// ============================================================================
//
// HermesCtr = Own<HermesCtrView>  — owning document handle (refcounted).
// HermesCtrView                   — non-owning view (MemHolder* + root_override).
// MemHolder                       — refcounted arena owner.
//
// Own<View> adds ref on construction, removes on destruction.
// Non-owning Views are valid as long as some Own<> keeps the holder alive.

static void walkthrough_documents() {
    std::println("\n=== 1. Documents & Ownership ===");

    // Create a fresh document. Arena is allocated with 65536-byte capacity.
    HermesCtr doc = make_doc();

    // The document owns a MemHolder with a GrowableSingleChunk arena.
    // base() returns the start of the arena segment — all offsets are relative to this.
    uint8_t* base = HermesCtrAccess::base(doc);
    std::println("  HermesCtrAccess::base(doc)       = {}", static_cast<void*>(base));
    std::println("  doc.has_root()   = {}", doc.has_root());
    // STOP: inspect doc, base, doc.holder()->use_count()

    // Copy semantics: Own<> adds a ref. Both share the same MemHolder.
    HermesCtr doc2 = doc;
    std::println("  After copy: use_count = {}", doc.holder()->use_count());
    // STOP: verify use_count == 2

    // Move semantics: transfers ownership, no refcount change.
    HermesCtr doc3 = std::move(doc2);
    std::println("  After move: use_count = {}, doc2 null = {}",
        doc.holder()->use_count(), doc2.is_null());
    // STOP: doc2 should be null, use_count still 2
}

// ============================================================================
// 2. AnyVal — The Universal Value Slot
// ============================================================================
//
// 8 bytes. Two modes:
//   Pointer mode: stores arena_offset_t (segment-relative), discriminant byte[7] == 0
//   Value mode:   up to 7 bytes of inline data + type_hash in byte[7] (odd, != 0)
//
// Embedded types: int8..int32, uint8..uint32, float, bool — fit in 7 bytes.
// Pointer types:  strings, arrays, maps — stored in arena, AnyVal holds offset.

static void walkthrough_any_val() {
    std::println("\n=== 2. AnyVal ===");

    // Value mode: embed an int32_t directly.
    AnyVal v = AnyVal::from_value(int32_t(42));
    std::println("  is_value={}  is_pointer={}  is_null={}",
        v.is_value(), v.is_pointer(), v.is_null());
    std::println("  as_value<int32_t>() = {}", v.as_value<int32_t>());
    std::println("  value_type_hash()   = {} (Integer={})",
        v.value_type_hash(), type_hash::Integer);
    // STOP: inspect v.raw() — the 8-byte representation

    // Value mode: embed a float.
    AnyVal vf = AnyVal::from_value(3.14f);
    std::println("  float value = {}", vf.as_value<float>());

    // Value mode: embed a bool.
    AnyVal vb = AnyVal::from_value(uint8_t(1));
    std::println("  bool value  = {}", vb.as_value<uint8_t>());

    // Pointer mode: create from an arena offset.
    AnyVal p = AnyVal::from_offset(arena_offset_t{128});
    std::println("  pointer offset = {}", p.to_offset());
    // STOP: inspect p.raw() — byte[7] should be 0

    // Null.
    AnyVal null;
    std::println("  null: is_null={}, raw={:#x}", null.is_null(), null.raw());
}

// ============================================================================
// 3. Building Documents with the View API
// ============================================================================
//
// HermesCtrView provides factory methods:
//   make_tiny_map()   → TinyMap (Own<TinyMapView>)
//   make_array()      → Array  (Own<ArrayView>)
//   make_object_map() → Map    (Own<MapView>)
//   make_string()     → String (Own<StringView>)
//   make_value<T>()   → T*     (raw pointer, arena-allocated)
//
// Views carry (offset, holder*). Dereference via holder->base() + offset.

static void walkthrough_building() {
    std::println("\n=== 3. Building Documents ===");

    auto doc = make_doc();

    // --- TinyMap: bitmap-indexed sparse map, keys 0..51 ---
    auto tmap = doc.make_tiny_map();
    tmap.put(0, AnyVal::from_value(int32_t(100)));
    tmap.put(5, AnyVal::from_value(int32_t(200)));
    std::println("  TinyMap: size={}, has_key(0)={}, has_key(1)={}",
        tmap.size(), tmap.has_key(0), tmap.has_key(1));
    std::println("  TinyMap[0] = {}", tmap.get(0).as_value<int32_t>());
    // STOP: inspect tmap.ptr(), tmap.offset(), tmap.ptr()->bitmap()

    // --- Array: dynamic heterogeneous array ---
    auto arr = doc.make_array();
    arr.push_back(AnyVal::from_value(int32_t(10)));
    arr.push_back(AnyVal::from_value(int32_t(20)));
    arr.push_back(AnyVal::from_value(float(3.14f)));
    std::println("  Array: size={}", arr.size());
    std::println("  Array[0]={}, Array[2]={}",
        arr.get(0).as_value<int32_t>(), arr.get(2).as_value<float>());
    // STOP: inspect arr.ptr(), arr.ptr()->capacity()

    // --- String: arena-allocated UTF-8 ---
    auto s = doc.make_string("Hello, Hermes!");
    std::println("  String: '{}' (len={})", s.view(), s.length());
    // STOP: inspect s.ptr(), the ArenaString layout (varint length + data)

    // --- Pointer-mode values in containers ---
    // Strings are too large for AnyVal value mode, so we use pointer mode.
    // The slot stores the string's arena offset.
    arr.push_back(AnyVal{});  // placeholder
    arr.slot(3)->set_offset(s.offset());
    AnyVal* str_slot = arr.slot(3);
    auto str_view = str_slot->as_ptr<ArenaString>(HermesCtrAccess::base(doc))->view();
    std::println("  Array[3] is_pointer={}, points to '{}'",
        str_slot->is_pointer(), str_view);
    // STOP: compare str_slot->to_offset() with s.offset()

    // --- Map: string-keyed hash map ---
    auto map = doc.make_object_map();
    map.put("name", AnyVal::from_value(int32_t(42)));
    map.put("active", AnyVal::from_value(uint8_t(1)));
    std::println("  Map: size={}, has('name')={}, get('name')={}",
        map.size(), map.has("name"), map.get("name").as_value<int32_t>());

    // Store a string value in map (pointer mode).
    // Allocate string BEFORE put() to avoid arena realloc between operations.
    auto greeting = doc.make_string("world");
    map.put("greeting", AnyVal{});
    // Re-fetch base after allocations (arena may have grown).
    uint8_t* b2 = HermesCtrAccess::base(doc);
    map.get_slot("greeting")->set_pointer(greeting.ptr(), b2);
    auto gsv = map.get_slot("greeting")->as_ptr<ArenaString>(b2)->view();
    std::println("  Map['greeting'] = '{}'", gsv);

    // Set the root.
    doc.set_root(map);
    std::println("  doc.has_root()={}, root is ObjectMap at offset {}",
        doc.has_root(), map.offset());
    // STOP: inspect DocumentHeader at HermesCtrAccess::base(doc), verify root_offset == map.offset()
}

// ============================================================================
// 4. Text Parser & Stringify Text Parser & Stringify
// ============================================================================
//
// parse("...") → HermesCtr with the parsed value as root.
// stringify(doc) → text representation.
//
// Supports: integers (42, -7, 0xFF, 0b1010, 100ll, 255_u8),
//           floats (3.14, 3.14f, 2.718d, 1e3),
//           booleans (true, false), null,
//           strings ("escaped", 'raw'),
//           arrays ([1, 2, 3]),
//           maps ({key: value, "quoted key": value}),
//           type declarations (Array<Integer>, Decimal(10, 2)),
//           typed values (@Integer = 42),
//           parameters (?paramName),
//           comments (// line comment).

static void walkthrough_parser() {
    std::println("\n=== 4. Text Parser & Stringify ===");

    // Simple values.
    {
        auto doc = parse("42");
        std::println("  parse('42') -> root = {}", *HermesCtrAccess::root<int32_t>(doc));
        std::println("  stringify   -> '{}'", stringify(doc));
    }
    // STOP: inspect HermesCtrAccess::root<int32_t>(doc), the TypeTag before the int32_t

    // String.
    {
        auto doc = parse("\"hello\\nworld\"");
        std::println("  parse string -> '{}'", HermesCtrAccess::root<ArenaString>(doc)->view());
    }

    // Array of mixed types.
    {
        auto doc = parse("[1, 3.14, \"text\", true, null]");
        uint8_t* base = HermesCtrAccess::base(doc);
        auto* arr = HermesCtrAccess::root<ObjectArray>(doc);
        std::println("  parse array -> size={}", arr->size());
        std::println("    [0] int   = {}", arr->get(0, base).as_value<int32_t>());
        std::println("    [1] float = {}", arr->get(1, base).as_value<float>());
        std::println("    [2] str   = '{}'",
            arr->slot(2, base)->as_ptr<ArenaString>(base)->view());
        std::println("    [3] bool  = {}", arr->get(3, base).as_value<uint8_t>());
        // STOP: inspect arr, walk through slots, see pointer vs value mode
    }

    // Nested map.
    {
        auto doc = parse(R"({
            user: {name: "Alice", age: 30},
            scores: [95, 87, 92],
            active: true
        })");
        std::println("  Nested map -> '{}'", stringify(doc));
        // STOP: inspect the ObjectMap tree, follow pointers from slots
    }

    // Type declarations.
    {
        auto doc = parse("Array<Integer>");
        uint8_t* base = HermesCtrAccess::base(doc);
        auto* dt = HermesCtrAccess::root<DatatypeData>(doc);
        std::println("  Type: name='{}', has_params={}",
            dt->name_view(base), dt->has_params());
        std::println("  stringify -> '{}'", stringify(doc));
    }

    // Typed value.
    {
        auto doc = parse("@Integer = 42");
        uint8_t* base = HermesCtrAccess::base(doc);
        auto* tv = HermesCtrAccess::root<TypedValueData>(doc);
        std::println("  TypedValue: type='{}', value={}",
            tv->datatype.get(base)->name_view(base),
            tv->value.as_value<int32_t>());
    }

    // Parameter.
    {
        auto doc = parse("?userId");
        std::println("  Parameter: name='{}'",
            HermesCtrAccess::root<ParameterData>(doc)->name_view(HermesCtrAccess::base(doc)));
    }

    // Round-trip: parse -> stringify -> parse -> stringify.
    {
        auto doc1 = parse("{x: [1, 2], y: @Integer = 3}");
        std::string s1 = stringify(doc1);
        auto doc2 = parse(s1);
        std::string s2 = stringify(doc2);
        std::println("  Round-trip: '{}' == '{}' -> {}",
            s1, s2, s1 == s2 ? "OK" : "MISMATCH");
    }
}

// ============================================================================
// 5. Binary Codec
// ============================================================================
//
// binary_encode(doc) -> compact binary bytes (depth-first, self-describing).
// binary_decode(bytes) -> new document.
// NOT the same as the zero-copy arena layout — designed for streaming.

static void walkthrough_binary() {
    std::println("\n=== 5. Binary Codec ===");

    auto doc = parse("{name: \"Alice\", scores: [95, 87, 92]}");
    std::println("  Arena size: {} bytes", HermesCtrAccess::arena(doc).total_used());

    // Encode.
    auto bytes = binary_encode(doc);
    std::println("  Encoded:    {} bytes", bytes.size());
    // STOP: inspect bytes — each value prefixed with TypeTag

    // Decode.
    auto decoded = binary_decode(bytes.data(), bytes.size());
    std::println("  Decoded:    arena={} bytes", HermesCtrAccess::arena(decoded).total_used());
    std::println("  stringify:  '{}'", stringify(decoded));

    // Double round-trip.
    auto bytes2 = binary_encode(decoded);
    std::println("  Round-trip: identical={}", bytes == bytes2);
}

// ============================================================================
// 6. Compactify & Zero-Copy Serialization
// ============================================================================
//
// compactify(doc) -> new document with minimal single-chunk arena.
// from_bytes_copy(ptr, size) -> document loaded from raw arena bytes.
//
// The arena layout IS the serialization format — no encoding/decoding needed.
// You can write arena bytes to disk and mmap them back.

static void walkthrough_compactify() {
    std::println("\n=== 6. Compactify & Zero-Copy ===");

    auto doc = make_doc();
    auto map = doc.make_tiny_map();
    doc.set_root(map);

    map.put(0, AnyVal::from_value(int32_t(42)));
    auto s = doc.make_string("hello");
    map.put(1, AnyVal{});
    map.slot(1)->set_pointer(s.ptr(), HermesCtrAccess::base(doc));

    std::println("  Original arena: {} bytes used of {} capacity",
        HermesCtrAccess::arena(doc).total_used(), HermesCtrAccess::arena(doc).head().capacity);

    // Compactify: deep-copy into minimal arena.
    auto compact = compactify(doc);
    std::println("  Compact arena:  {} bytes used", HermesCtrAccess::arena(compact).total_used());
    // STOP: compare HermesCtrAccess::arena(doc).total_used() vs HermesCtrAccess::arena(compact).total_used()

    // Zero-copy round-trip: write arena bytes, then load back.
    uint8_t* data = HermesCtrAccess::base(compact);
    size_t size = HermesCtrAccess::arena(compact).total_used();

    auto loaded = from_bytes_copy(data, size);
    uint8_t* lb = HermesCtrAccess::base(loaded);
    auto* lmap = HermesCtrAccess::root<TinyObjectMap>(loaded);
    std::println("  Loaded: map[0]={}, map[1]='{}'",
        lmap->get(0, lb).as_value<int32_t>(),
        lmap->slot(1, lb)->as_ptr<ArenaString>(lb)->view());
    // STOP: compare memory at HermesCtrAccess::base(compact) and HermesCtrAccess::base(loaded) — should be identical
}

// ============================================================================
// 7. HermesPath — JMESPath-like Query Language
// ============================================================================
//
// eval_path(data, "expression") -> new document with result.
//
// Supported: identifiers, dot notation, array indices [0], [-1],
//            slices [0:3], [::2], wildcards [*], {*},
//            filters [?age > 18], comparisons (==, !=, <, >, <=, >=),
//            logical (&&, ||, !), pipe (|), functions (length, sort, ...),
//            multiselect lists [a, b], multiselect hashes {x: a, y: b}.

static void walkthrough_path() {
    std::println("\n=== 7. HermesPath ===");

    auto data = parse(R"({
        user: {name: "Alice", age: 30},
        items: [
            {name: "book", price: 15},
            {name: "pen", price: 3},
            {name: "laptop", price: 999}
        ]
    })");

    // Simple identifier.
    {
        auto r = eval_path(data, "user.name");
        std::println("  user.name = '{}'", HermesCtrAccess::root<ArenaString>(r)->view());
    }
    // STOP: inspect r — it shares data's MemHolder with root_override

    // Array index.
    {
        auto r = eval_path(data, "items[0].name");
        std::println("  items[0].name = '{}'", HermesCtrAccess::root<ArenaString>(r)->view());
    }

    // Negative index (last element).
    {
        auto r = eval_path(data, "items[-1].price");
        std::println("  items[-1].price = {}", *HermesCtrAccess::root<int32_t>(r));
    }

    // NOTE: slice, wildcard, filter create NEW objects in result arena.
    // With GrowableSingleChunk, arena realloc can invalidate the raw pointer
    // returned by the evaluator. These operations work correctly when used
    // via the template engine (which deep-copies), but direct eval_path
    // for aggregate operations has a known pointer-stability limitation.
    //
    // Simple path queries (identifier, dot notation, index) always work
    // because they return pointers from the data arena (read-only, no realloc).

    // Function (returns embedded value — works reliably).
    {
        auto r = eval_path(data, "length(items)");
        if (r.has_root()) {
            std::println("  length(items) = {}", *HermesCtrAccess::root<int32_t>(r));
        }
    }
}

// ============================================================================
// 8. Template Engine (Jinja-like)
// ============================================================================
//
// render("template", data) -> rendered string.
//
// Supported: {{ expr }} — variable output (HermesPath expression),
//            {% for x in expr %} ... {% endfor %}
//            {% if expr %} ... {% elif expr %} ... {% else %} ... {% endif %}
//            {% set var = expr %}

static void walkthrough_templates() {
    std::println("\n=== 8. Template Engine ===");

    auto data = parse(R"({
        name: "Alice",
        items: ["apple", "banana", "cherry"],
        show_greeting: true,
        count: 3
    })");

    // Variable substitution.
    {
        std::string r = render("Hello, {{ name }}!", data);
        std::println("  Variable: '{}'", r);
    }

    // For loop.
    {
        std::string r = render("Items: {% for x in items %}[{{ x }}]{% endfor %}", data);
        std::println("  For loop: '{}'", r);
    }

    // Conditional.
    {
        std::string r = render(
            "{% if show_greeting %}Hi {{ name }}{% else %}Bye{% endif %}", data);
        std::println("  If/else:  '{}'", r);
    }

    // Set variable.
    {
        std::string r = render(
            "{% set doubled = count %}Count is {{ doubled }}", data);
        std::println("  Set:      '{}'", r);
    }
}

// ============================================================================
// 9. Memory Layout Deep Dive
// ============================================================================
//
// Arena layout (GrowableSingleChunk):
//   [DocumentHeader (8 bytes)] [TypeTag|Object] [TypeTag|Object] ...
//
// Each arena object is preceded by a TypeTag:
//   bits [2:0]  = code_len (0-7 extra bytes)
//   bits [4:3]  = descriptor (Data=0, Array=1, Map=2)
//   bits [7:5]  = upper bits of type_code
//   bytes [1..code_len] = remaining type_code bytes
//
// RelativePtr<T>: 4 bytes (arena_offset_t), points from segment base.
// AnyVal:      8 bytes, pointer or embedded value.

static void walkthrough_memory_layout() {
    std::println("\n=== 9. Memory Layout ===");

    auto doc = make_doc(256);
    auto map = doc.make_tiny_map();
    doc.set_root(map);
    map.put(0, AnyVal::from_value(int32_t(7)));

    uint8_t* base = HermesCtrAccess::base(doc);
    size_t used = HermesCtrAccess::arena(doc).total_used();

    // Dump arena hex.
    std::print("  Arena dump ({} bytes):\n  ", used);
    for (size_t i = 0; i < used; ++i) {
        std::print("{:02x} ", base[i]);
        if ((i + 1) % 16 == 0 && i + 1 < used) std::print("\n  ");
    }
    std::println("");

    // Annotate.
    auto* hdr = reinterpret_cast<DocumentHeader*>(base);
    std::println("  DocumentHeader at [0]: root_offset = {}", hdr->root_offset);
    std::println("  TinyObjectMap at [{}]: size={}, bitmap={:#x}",
        map.offset(), map.size(), map.ptr()->bitmap());

    // Read TypeTag before the TinyObjectMap.
    TypeTag tag = TypeTag::read_before(reinterpret_cast<const uint8_t*>(map.ptr()));
    std::println("  TypeTag: type_code={}, descriptor={}, byte_length={}",
        tag.type_code(), static_cast<int>(tag.descriptor()), tag.byte_length());
    // STOP: walk through the hex dump with the TypeTag/offset annotations
}

// ============================================================================
// Main
// ============================================================================

int main() {
    logos::hermes::hermes_init();
    std::println("========================================");
    std::println("  Hermes API Walkthrough");
    std::println("  Set breakpoints at STOP comments");
    std::println("========================================");

    walkthrough_documents();
    walkthrough_any_val();
    walkthrough_building();
    walkthrough_parser();
    walkthrough_binary();
    walkthrough_compactify();
    walkthrough_path();
    walkthrough_templates();
    walkthrough_memory_layout();

    std::println("\n=== Walkthrough complete ===");
    return 0;
}
