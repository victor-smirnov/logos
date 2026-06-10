// Logos project — https://github.com/victor-smirnov/logos

#pragma once

#include <unordered_map>

#include <logos/hermes2/mem_holder.hpp>
#include <logos/hermes2/any_val.hpp>
#include <logos/core/expected.hpp>

namespace logos::hermes2 {

// DeepCopyState — the src→dst dedup map driving clone/compaction (Memoria's
// DeepCopyState). Keyed by the SRC object pointer (src is immutable, never moves);
// the value is the DST object pointer. Breaks cycles + shared subgraphs: an object
// is copied ONCE; later references resolve to the existing dst copy.
//
// This first implementation copies into a NEVER-MOVE (MultiChunk) dst arena, so the
// dst pointers are stable (no realloc) and the map stores them directly — no
// AddrResolver needed. (The single-segment-immutable variant that reallocs the dst
// will swap this map's value for an AddrResolver<void>, re-resolved after each
// alloc; see project_hermes2_cpp_migration.)
class DeepCopyState {
public:
    explicit DeepCopyState(MemHolder* dst) noexcept : dst_(dst) {}

    Arena&     arena()  noexcept { return dst_->arena(); }
    MemHolder* holder() noexcept { return dst_; }

    void* resolve(const void* src) const noexcept {
        auto it = map_.find(src);
        return it == map_.end() ? nullptr : it->second;
    }
    void map(const void* src, void* dst) { map_[src] = dst; }

private:
    MemHolder* dst_;
    std::unordered_map<const void*, void*> map_;
};

// Deep-copy a tagged object (absolute src ptr) into dedup's arena; returns the dst
// object pointer (nullptr on OOM). Recurses through Ref children with cycle/shared
// dedup. Dispatches per-type on the in-band TypeTag.
void* deep_copy_object(const uint8_t* src_obj, DeepCopyState& dedup) noexcept;

// Deep-copy a value-form AnyVal: null → null, Pod → verbatim, Ref → recurse the
// pointee and return a value-form Ref to the dst copy.
AnyVal deep_copy_anyval(AnyVal src_av, DeepCopyState& dedup) noexcept;

struct ClonedDoc {
    MemHolder* holder;   // a fresh holder (refcount 1 — caller owns + must unref)
    AnyVal     root;     // value-form Ref to the cloned root inside `holder`
};

// clone / compactify — ONE operation: deep-copy the tree reachable from `root` into
// a fresh never-move holder. Only live, reachable objects are copied, so dead space
// in the source is dropped (the compaction effect). `root` must be a value-form
// AnyVal the caller keeps alive (its source holder outlives this call).
[[nodiscard]] logos::expected<ClonedDoc> clone(AnyVal root) noexcept;

} // namespace logos::hermes2
