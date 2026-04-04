// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Victor Smirnov
// Logos project — https://github.com/victor-smirnov/logos

#include <logos/hermes/access.hpp>
#include <logos/verification/assert.hpp>
#include <logos/core/err.hpp>

namespace logos::hermes {

// Resolve an ObjectView into an AnyVal that is safe to store in dst_holder's arena.
// If value is embedded (is_value()) or already in the same arena — returned as-is.
// If value is a pointer from a different arena — deep-copied into dst_holder's arena.
static logos::expected<AnyVal> resolve_for_arena(const ObjectView& value,
                                                   MemHolder* dst_holder) noexcept {
    if (!value.is_pointer()) return value.tagged();  // embedded value — arena-independent

    LOGOS_ASSERT(value.holder() != nullptr, "HERMES-ANYVAL-001",
        "ObjectView in pointer mode must have a valid MemHolder");

    if (value.holder() == dst_holder) return value.tagged();  // same arena — safe as-is

    // Cross-arena: deep-copy the object into the destination arena.
    HermesCtrView dst(dst_holder);
    const void* src_obj = value.tagged().as_ptr<void>(value.holder()->base());
    LOGOS_TRY(auto* copy, copy_object_into(src_obj, value.holder()->base(), dst));

    AnyVal result;
    result.set_pointer(copy, HermesCtrAccess::base(dst));  // re-fetched after possible arena growth
    return result;
}

// --- NamedCode checked access ---

AnyVal TinyMapView::get(NamedCode<uint8_t> key) const {
    LOGOS_ASSERT(has_key(key.code), "HERMES-TINYMAP-001",
        "Required field '{}' ({}) not found in TinyMap", key.name, int(key.code));
    return ptr()->get(key.code, base());
}

// --- Cross-arena safe put/push_back overloads ---

logos::expected<void> TinyMapView::put(uint8_t key, const ObjectView& value) noexcept {
    LOGOS_TRY(auto resolved, resolve_for_arena(value, holder_));
    return ptr()->put(key, resolved, arena());
}

logos::expected<void> ArrayView::push_back(const ObjectView& value) noexcept {
    LOGOS_TRY(auto resolved, resolve_for_arena(value, holder_));
    return ptr()->push_back(resolved, arena());
}

logos::expected<void> MapView::put(std::string_view key, const ObjectView& value) noexcept {
    LOGOS_TRY(auto resolved, resolve_for_arena(value, holder_));
    return ptr()->put(key, resolved, arena());
}

static DocumentHeader* get_header(MemHolder* holder) {
    return reinterpret_cast<DocumentHeader*>(holder->base());
}

// --- HermesCtrView ---

bool HermesCtrView::has_root() const {
    if (!holder_) return false;
    if (root_override_ != NULL_OFFSET) return true;
    return get_header(holder_)->has_root();
}

void HermesCtrView::set_root(void* object) noexcept {
    get_header(holder_)->root_offset = offset_of(object);
}

void HermesCtrView::set_root_offset(arena_offset_t offset) noexcept {
    get_header(holder_)->root_offset = offset;
}

template <typename T>
T* HermesCtrView::root() const noexcept {
    arena_offset_t off = (root_override_ != NULL_OFFSET)
        ? root_override_
        : get_header(holder_)->root_offset;
    if (off == NULL_OFFSET) return nullptr;
    return reinterpret_cast<T*>(base() + off.value());
}

// Explicit instantiations for common types.
template void* HermesCtrView::root<void>() const;
template TinyObjectMap* HermesCtrView::root<TinyObjectMap>() const;
template ObjectArray* HermesCtrView::root<ObjectArray>() const;
template ObjectMap* HermesCtrView::root<ObjectMap>() const;
template ArenaString* HermesCtrView::root<ArenaString>() const;
template int32_t* HermesCtrView::root<int32_t>() const;
template uint8_t* HermesCtrView::root<uint8_t>() const;
template float* HermesCtrView::root<float>() const;
template double* HermesCtrView::root<double>() const;
template int64_t* HermesCtrView::root<int64_t>() const;
template uint32_t* HermesCtrView::root<uint32_t>() const;
template uint16_t* HermesCtrView::root<uint16_t>() const;
template int16_t* HermesCtrView::root<int16_t>() const;
template int8_t* HermesCtrView::root<int8_t>() const;
template DatatypeData* HermesCtrView::root<DatatypeData>() const;
template TypedValueData* HermesCtrView::root<TypedValueData>() const;
template ParameterData* HermesCtrView::root<ParameterData>() const;

Object HermesCtrView::root_object() const {
    if (!has_root()) return Object{};
    auto off = get_header(holder_)->root_offset;
    AnyVal tp = AnyVal::from_offset(off);
    return Object(ObjectView(tp, holder_));
}

logos::expected<TinyMap> HermesCtrView::make_tiny_map(uint8_t capacity) noexcept {
    LOGOS_TRY(auto* p, TinyObjectMap::create(holder_->arena(), capacity));
    return TinyMap(offset_of(p), holder_);
}

logos::expected<Array> HermesCtrView::make_array(uint64_t capacity) noexcept {
    LOGOS_TRY(auto* p, ObjectArray::create(holder_->arena(), capacity));
    return Array(offset_of(p), holder_);
}

logos::expected<Map> HermesCtrView::make_object_map(uint8_t log2_buckets) noexcept {
    LOGOS_TRY(auto* p, ObjectMap::create(holder_->arena(), log2_buckets));
    return Map(offset_of(p), holder_);
}

logos::expected<String> HermesCtrView::make_string(std::string_view str) noexcept {
    LOGOS_TRY(auto* p, ArenaString::create(holder_->arena(), str));
    return String(offset_of(p), holder_);
}

// --- make_doc ---

static logos::expected<HermesCtr> make_doc_impl(MemHolder* holder) noexcept {
    LOGOS_TRY(auto* hdr_void, holder->arena().allocate_raw(sizeof(DocumentHeader), alignof(DocumentHeader)));
    auto* hdr = static_cast<DocumentHeader*>(hdr_void);
    hdr->root_offset = NULL_OFFSET;
    return HermesCtr(HermesCtrView(holder));
}

// MemHolder has a private destructor (heap-only), so we construct it with new
// and use the InitTag protocol manually to check for OOM.
static logos::expected<MemHolder*> make_mem_holder(size_t capacity, ArenaMode mode) noexcept {
    logos::InitTag tag;
    auto* holder = new MemHolder(tag, capacity, mode);
    if (!tag.ok()) {
        // Arena construction failed — destroy via the ref-counting protocol.
        holder->ref();
        holder->unref();  // drops to 0, calls delete via the private dtor
        return std::unexpected(std::move(tag.err));
    }
    return holder;
}

logos::expected<HermesCtr> make_doc(size_t capacity) noexcept {
    LOGOS_TRY(auto* holder, make_mem_holder(capacity, ArenaMode::GrowableSingleChunk));
    return make_doc_impl(holder);
}

logos::expected<HermesCtr> make_doc_multi(size_t initial_capacity) noexcept {
    LOGOS_TRY(auto* holder, make_mem_holder(initial_capacity, ArenaMode::MultiChunk));
    return make_doc_impl(holder);
}

} // namespace logos::hermes
