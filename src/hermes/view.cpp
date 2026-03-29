// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Victor Smirnov
// Logos project — https://github.com/victor-smirnov/logos

#include <logos/hermes/view.hpp>
#include <logos/hermes/document.hpp>

namespace logos::hermes {

// --- ViewBase ---

uint8_t* ViewBase::base() const {
    return mem_->base();
}

// --- TinyMapView ---

Arena& TinyMapView::arena() const { return mem_->arena(); }

// --- ArrayView ---

Arena& ArrayView::arena() const { return mem_->arena(); }

// --- MapView ---

Arena& MapView::arena() const { return mem_->arena(); }

// --- ObjectView conversions ---

TinyMapView ObjectView::as_tiny_map() const {
    return TinyMapView(tagged_.to_offset(), mem_);
}

ArrayView ObjectView::as_array() const {
    return ArrayView(tagged_.to_offset(), mem_);
}

MapView ObjectView::as_map() const {
    return MapView(tagged_.to_offset(), mem_);
}

StringView ObjectView::as_string() const {
    return StringView(tagged_.to_offset(), mem_);
}

DatatypeView ObjectView::as_datatype() const {
    return DatatypeView(tagged_.to_offset(), mem_);
}

ParameterView ObjectView::as_parameter() const {
    return ParameterView(tagged_.to_offset(), mem_);
}

// --- Document ---

Document Document::create(size_t capacity) {
    return Document(HermesCtr::make_shared_ctr(capacity));
}

bool Document::has_root() const {
    return ctr_ && ctr_->has_root();
}

void Document::set_root(const ViewBase& view) {
    ctr_->set_root_offset(view.offset());
}

void Document::set_root_offset(arena_offset_t offset) {
    ctr_->set_root_offset(offset);
}

ObjectView Document::root() const {
    if (!has_root()) return ObjectView{};
    auto* hdr = ctr_->header();
    // Build a TaggedPtr in pointer mode from the root offset.
    TaggedPtr tp = TaggedPtr::from_offset(hdr->root.offset());
    return ObjectView(tp, ctr_);
}

TinyMapView Document::make_tiny_map(uint8_t capacity) {
    auto* p = ctr_->make_tiny_map(capacity);
    return TinyMapView(ctr_->offset_of(p), ctr_);
}

ArrayView Document::make_array(uint64_t capacity) {
    auto* p = ctr_->make_array(capacity);
    return ArrayView(ctr_->offset_of(p), ctr_);
}

MapView Document::make_object_map(uint8_t log2_buckets) {
    auto* p = ctr_->make_object_map(log2_buckets);
    return MapView(ctr_->offset_of(p), ctr_);
}

StringView Document::make_string(std::string_view str) {
    auto* p = ctr_->make_string(str);
    return StringView(ctr_->offset_of(p), ctr_);
}

HermesCtr& Document::ctr() { return *ctr_; }
const HermesCtr& Document::ctr() const { return *ctr_; }

} // namespace logos::hermes
