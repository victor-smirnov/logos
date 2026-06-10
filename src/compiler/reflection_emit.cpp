// reflection_emit.cpp — post-sema pass that builds TypeInfo rodata blobs
// for types referenced by reflect::<T>() and types with user annotations.
//
// For each requested type, builds a Hermes ObjectMap document with:
//   { name, pkg, type_code, kind, is_data_plain, fields: [...], annotations: [...] }
// Serializes to a blob with an 8-byte little-endian size prefix.
// Symbol: "__logos_reflect__<type_hash_hex>" with WeakODR linkage.

#include <logos/compiler/lir.hpp>
#include <logos/compiler/lir_mirror.hpp>
#include <logos/compiler/lir_view.hpp>
#include <logos/compiler/sha256.hpp>

#include <logos/hermes2/compat.hpp>
#include <logos/hermes2/compat.hpp>
#include <logos/hermes2/compat.hpp>
#include <logos/hermes2/compat.hpp>
#include <logos/hermes2/compat.hpp>
#include <logos/hermes2/compat.hpp>
#include <logos/hermes2/compat.hpp>
#include <logos/hermes2/compat.hpp>

namespace logos::compiler {

using logos::hermes2::Hermes;
using logos::hermes2::HermesAccess;
using logos::hermes2::ObjectMap;
using logos::hermes2::ObjectArray;
using logos::hermes2::ArenaString;
using logos::hermes2::AnyVal;
using logos::hermes2::arena_offset_t;
using logos::hermes2::anyval_put;
using logos::hermes2::make_doc;
using logos::hermes2::clone;

namespace {

// ── Hermes building helpers ───────────────────────────────────────────────

static AnyVal hval_str(Hermes& doc, std::string_view s) {
    auto* as = ArenaString::create(HermesAccess::arena(doc), s).get();
    // Hermes2 AnyVal is SELF-relative: build the Ref via set_ref(absolute ptr), NOT
    // from_raw(offset) (the Hermes1 base-relative convention — resolve() would then be
    // &slot+offset = garbage). The returned temporary re-anchors when stored.
    AnyVal r; r.set_ref(as); return r;
}

// u64: store in arena with U64 tag (type_code=27); readable via get_u64().
static AnyVal hval_u64(Hermes& doc, uint64_t v) {
    return anyval_put<uint64_t>(HermesAccess::arena(doc), v).get();
}

// i64: inline if fits in i24, else arena.
static AnyVal hval_i64(Hermes& doc, int64_t v) {
    if (v >= -8388608LL && v <= 8388607LL)
        return AnyVal::from_value<int32_t>(static_cast<int32_t>(v));
    return anyval_put<int64_t>(HermesAccess::arena(doc), v).get();
}

// bool: type_hash=37, inline value mode.
static AnyVal hval_bool([[maybe_unused]] Hermes& doc, bool v) {
    return AnyVal::from_value<uint8_t>(v ? 1u : 0u, 37);
}

static uint32_t begin_map(Hermes& doc, uint8_t log2 = 3) {
    auto* m = ObjectMap::create(HermesAccess::arena(doc), log2).get();
    return static_cast<uint32_t>(
        reinterpret_cast<uint8_t*>(m) - HermesAccess::base(doc));
}

static void map_put(Hermes& doc, uint32_t m_off, std::string_view key, AnyVal val) {
    auto* m = reinterpret_cast<ObjectMap*>(HermesAccess::base(doc) + m_off);
    m->put(std::string(key), val, HermesAccess::arena(doc)).get();
}

static uint32_t begin_array(Hermes& doc) {
    auto* a = ObjectArray::create(HermesAccess::arena(doc), 4).get();
    return static_cast<uint32_t>(
        reinterpret_cast<uint8_t*>(a) - HermesAccess::base(doc));
}

static void array_push(Hermes& doc, uint32_t a_off, AnyVal val) {
    auto* a = reinterpret_cast<ObjectArray*>(HermesAccess::base(doc) + a_off);
    a->push_back(val, HermesAccess::arena(doc)).get();
}

// Self-relative Ref to an in-arena object at byte offset `off` (the arena is a
// single segment, so base(doc)+off is the absolute address). NOT from_raw(off).
static AnyVal as_ptr(Hermes& doc, uint32_t off) {
    AnyVal r; r.set_ref(HermesAccess::base(doc) + off); return r;
}

// ── Annotation value serializer ───────────────────────────────────────────

static AnyVal annot_val_to_hval(Hermes& doc, const lir::LAnnotationValue& v) {
    using K = lir::LAnnotationValue::Kind;
    switch (v.kind) {
    case K::Int:   return hval_i64(doc, v.i);
    case K::Float: return anyval_put<double>(HermesAccess::arena(doc), v.f).get();
    case K::Bool:  return hval_bool(doc, v.i != 0);
    case K::Str:   return hval_str(doc, v.s);
    case K::Enum:  return hval_str(doc, v.enum_name + "::" + v.enum_variant);
    case K::Array: {
        uint32_t arr = begin_array(doc);
        for (auto& item : v.arr)
            array_push(doc, arr, annot_val_to_hval(doc, item));
        return as_ptr(doc, arr);
    }
    }
    return AnyVal{};
}

// ── Build one annotation instance as a Hermes map ────────────────────────

static AnyVal build_annotation_map(Hermes& doc, const lir::LAnnotationInstance& inst) {
    uint32_t m = begin_map(doc);
    std::string_view type_key = inst.ann_fqn.empty() ? inst.ann_name : inst.ann_fqn;
    map_put(doc, m, "type", hval_str(doc, type_key));
    for (auto& [k, v] : inst.kv)
        map_put(doc, m, k, annot_val_to_hval(doc, v));
    return as_ptr(doc, m);
}

// ── Build one field entry ─────────────────────────────────────────────────

static std::string type_name_of(TypeRef t) {
    if (!t) return "?";
    using K = LogosType::Kind;
    switch (t.kind()) {
    case K::I8:   return "i8";   case K::U8:   return "u8";
    case K::I16:  return "i16";  case K::U16:  return "u16";
    case K::I32:  return "i32";  case K::U32:  return "u32";
    case K::I64:  return "i64";  case K::U64:  return "u64";
    case K::I128: return "i128"; case K::U128: return "u128";
    case K::F32:  return "f32";  case K::F64:  return "f64";
    case K::Bool: return "bool";
    case K::Struct:   return std::string(t.struct_name());
    case K::ZonedStruct: return std::string(t.struct_name());
    case K::Enum:     return std::string(t.enum_name());
    default: return "?";
    }
}

static AnyVal build_field_map(Hermes& doc, const lir::LField& f) {
    uint32_t m = begin_map(doc, 2);  // 4 buckets
    map_put(doc, m, "name",      hval_str(doc, f.name));
    map_put(doc, m, "type_name", hval_str(doc, type_name_of(f.type)));
    map_put(doc, m, "offset",    hval_u64(doc, 0));  // layout not yet computed at LIR stage
    map_put(doc, m, "size",      hval_u64(doc, 0));
    return as_ptr(doc, m);
}

// ── Build TypeInfo blob for one struct ───────────────────────────────────

static std::vector<uint8_t> build_type_info_blob(lir::LProgram& prog, const lir::LStructDef& sd) {
    auto doc = logos::hermes2::make_doc_single_chunk(131072).get();

    // Root map — log2=4 → 16 buckets.
    uint32_t root = begin_map(doc, 4);

    map_put(doc, root, "name",          hval_str(doc, sd.name));
    map_put(doc, root, "pkg",           hval_str(doc, sd.pkg));
    map_put(doc, root, "type_code",     hval_u64(doc, sd.type_code));
    map_put(doc, root, "kind",          hval_i64(doc, sd.is_zoned ? 2 : 1));
    map_put(doc, root, "is_data_plain", hval_bool(doc, sd.is_data_plain));

    // Fields array
    uint32_t fields_arr = begin_array(doc);
    for (auto& f : sd.fields)
        array_push(doc, fields_arr, build_field_map(doc, f));
    map_put(doc, root, "fields", as_ptr(doc, fields_arr));

    // Annotations array
    uint32_t annots_arr = begin_array(doc);
    for (auto& inst : sd.annotations)
        array_push(doc, annots_arr, build_annotation_map(doc, inst));
    map_put(doc, root, "annotations", as_ptr(doc, annots_arr));

    HermesAccess::set_root_offset(doc, arena_offset_t(root));

    // Compact the document
    auto packed = compactify(doc).get();
    auto& arena = HermesAccess::arena(packed);
    const uint8_t* data = arena.head().data();
    size_t used = arena.total_used();

    // Prepend 8-byte LE size so HermesStatic::size() works.
    std::vector<uint8_t> blob(8 + used);
    for (int k = 0; k < 8; ++k)
        blob[k] = static_cast<uint8_t>((used >> (k * 8)) & 0xFF);
    std::copy(data, data + used, blob.begin() + 8);
    return blob;
}

// ── Symbol name from type hash ────────────────────────────────────────────

static std::string reflect_symbol(const std::array<uint8_t, 23>& hash) {
    static const char hex[] = "0123456789abcdef";
    std::string sym = "__logos_reflect__";
    for (auto b : hash) { sym += hex[b >> 4]; sym += hex[b & 0xF]; }
    return sym;
}

static std::vector<uint8_t> build_genos_info_blob(lir::LProgram& prog, const lir::LTraitDef& td) {
    auto doc = logos::hermes2::make_doc_single_chunk(65536).get();
    uint32_t root = begin_map(doc, 3);
    map_put(doc, root, "name", hval_str(doc, td.name));
    map_put(doc, root, "pkg",  hval_str(doc, td.pkg));
    map_put(doc, root, "kind", hval_i64(doc, 3));
    if (td.type_code != 0)
        map_put(doc, root, "type_code", hval_u64(doc, td.type_code));
    HermesAccess::set_root_offset(doc, arena_offset_t(root));
    auto packed = compactify(doc).get();
    auto& arena = HermesAccess::arena(packed);
    const uint8_t* data = arena.head().data();
    size_t used = arena.total_used();
    std::vector<uint8_t> blob(8 + used);
    for (int k = 0; k < 8; ++k)
        blob[k] = static_cast<uint8_t>((used >> (k * 8)) & 0xFF);
    std::copy(data, data + used, blob.begin() + 8);
    return blob;
}

} // anonymous namespace

// ── Public entry point ────────────────────────────────────────────────────

lir::LProgram reflection_emit(lir::LProgram prog) {
    // Collect all types that need TypeInfo:
    // - Explicitly requested via reflect::<T>()
    // - Annotated datatypes (always emit so runtime can read annotations)
    std::unordered_set<std::string> to_emit;
    for (auto& fqn : prog.reflect_requests)
        to_emit.insert(fqn);
    for (auto& sd : prog.structs) {
        if (sd.is_zoned && !sd.annotations.empty()) {
            std::string fqn = sd.pkg.empty() ? sd.name : sd.pkg + "::" + sd.name;
            to_emit.insert(fqn);
        }
    }

    // Track emitted symbols to avoid duplicates within this prog.
    std::unordered_set<std::string> emitted;

    for (auto& sd : prog.structs) {
        if (!sd.is_zoned) continue;
        if (sd.type_hash == std::array<uint8_t, 23>{}) continue; // generic template
        std::string fqn = sd.pkg.empty() ? sd.name : sd.pkg + "::" + sd.name;
        if (to_emit.find(fqn) == to_emit.end()) continue;

        auto sym = reflect_symbol(sd.type_hash);
        if (!emitted.insert(sym).second) continue;  // already done

        auto blob = build_type_info_blob(prog, sd);
        prog.reflection_globals.push_back({std::move(sym), std::move(blob)});
    }

    // Emit TypeInfo for Hermes-tagged traits (have #[type_code]) or reflect-requested.
    for (auto& td : prog.traits) {
        std::string fqn = td.pkg.empty() ? td.name : td.pkg + "::" + td.name;
        bool requested = to_emit.count(fqn) > 0;
        if (td.type_code == 0 && !requested) continue;
        if (!td.type_params.empty()) continue; // generic template, skip
        // Compute type_hash from fqn (same algorithm as for structs).
        auto hash = type_hash_23(fqn);
        auto sym = reflect_symbol(hash);
        if (!emitted.insert(sym).second) continue;
        auto blob = build_genos_info_blob(prog, td);
        prog.reflection_globals.push_back({std::move(sym), std::move(blob)});
    }

    return prog;
}

} // namespace logos::compiler
