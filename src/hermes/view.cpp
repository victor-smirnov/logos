// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Victor Smirnov
// Logos project — https://github.com/victor-smirnov/logos

#include <logos/hermes/view.hpp>

namespace logos::hermes {

static DocumentHeader* get_header(MemHolder* holder) {
    return reinterpret_cast<DocumentHeader*>(holder->base());
}

// --- HermesCtrView ---

bool HermesCtrView::has_root() const {
    if (!holder_) return false;
    if (root_override_ != NULL_OFFSET) return true;
    return get_header(holder_)->has_root();
}

void HermesCtrView::set_root(void* object) {
    get_header(holder_)->root_offset = offset_of(object);
}

void HermesCtrView::set_root_offset(arena_offset_t offset) {
    get_header(holder_)->root_offset = offset;
}

template <typename T>
T* HermesCtrView::root() const {
    arena_offset_t off = (root_override_ != NULL_OFFSET)
        ? root_override_
        : get_header(holder_)->root_offset;
    if (off == NULL_OFFSET) return nullptr;
    return reinterpret_cast<T*>(base() + off);
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
    TaggedPtr tp = TaggedPtr::from_offset(off);
    return Object(ObjectView(tp, holder_));
}

TinyMap HermesCtrView::make_tiny_map(uint8_t capacity) {
    auto* p = TinyObjectMap::create(holder_->arena(), capacity);
    return TinyMap(offset_of(p), holder_);
}

Array HermesCtrView::make_array(uint64_t capacity) {
    auto* p = ObjectArray::create(holder_->arena(), capacity);
    return Array(offset_of(p), holder_);
}

Map HermesCtrView::make_object_map(uint8_t log2_buckets) {
    auto* p = ObjectMap::create(holder_->arena(), log2_buckets);
    return Map(offset_of(p), holder_);
}

String HermesCtrView::make_string(std::string_view str) {
    auto* p = ArenaString::create(holder_->arena(), str);
    return String(offset_of(p), holder_);
}

// --- make_doc ---

static HermesCtr make_doc_impl(MemHolder* holder) {
    auto* hdr = static_cast<DocumentHeader*>(
        holder->arena().allocate_raw(sizeof(DocumentHeader), alignof(DocumentHeader)));
    hdr->root_offset = NULL_OFFSET;
    return HermesCtr(HermesCtrView(holder));
}

HermesCtr make_doc(size_t capacity) {
    return make_doc_impl(new MemHolder(capacity, ArenaMode::GrowableSingleChunk));
}

HermesCtr make_doc_multi(size_t initial_capacity) {
    return make_doc_impl(new MemHolder(initial_capacity, ArenaMode::MultiChunk));
}

} // namespace logos::hermes
