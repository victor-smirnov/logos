// Logos project — https://github.com/victor-smirnov/logos

#pragma once

#include <new>
#include <cstring>

#include <logos/hermes2/mem_holder.hpp>
#include <logos/hermes2/any_val.hpp>
#include <logos/core/expected.hpp>

namespace logos::hermes2 {

// DocumentHeader — the untagged header at OFFSET 0 of every Hermes2 document arena:
// it holds the document root (an at-rest AnyVal). Putting it at offset 0 means a
// loaded blob always finds the root at the start.
struct DocumentHeader {
    AnyVal root;
};

// HermesCtr — an OWNING handle to a Hermes2 document: the MemHolder (residency) plus
// its in-arena DocumentHeader (the root slot). Move-only (a unique owning Rc); the
// holder's refcount is released on destruction.
class HermesCtr {
public:
    HermesCtr() noexcept = default;

    // Create an empty document (root = null) in a fresh holder.
    [[nodiscard]] static logos::expected<HermesCtr>
    make(size_t capacity = 4096, ArenaMode mode = ArenaMode::MultiChunk) noexcept {
        LOGOS_TRY(auto* h, MemHolder::make(capacity, mode));     // refcount 1
        auto hr = init_header(h);
        if (!hr) { h->unref(); return std::unexpected(std::move(hr.error())); }
        return HermesCtr(h, *hr);
    }

    // Load a document from a rigid single-segment blob (a compactify() dump). The
    // DocumentHeader is at offset 0 of the blob.
    [[nodiscard]] static logos::expected<HermesCtr>
    from_bytes(const void* data, size_t size) noexcept {
        LOGOS_TRY(auto* h, MemHolder::from_bytes(data, size));   // refcount 1
        auto* hdr = reinterpret_cast<DocumentHeader*>(h->arena().head().data());
        return HermesCtr(h, hdr);
    }

    HermesCtr(HermesCtr&& o) noexcept : holder_(o.holder_), header_(o.header_) {
        o.holder_ = nullptr; o.header_ = nullptr;
    }
    HermesCtr& operator=(HermesCtr&& o) noexcept {
        if (this != &o) {
            if (holder_) holder_->unref();
            holder_ = o.holder_; header_ = o.header_;
            o.holder_ = nullptr; o.header_ = nullptr;
        }
        return *this;
    }
    HermesCtr(const HermesCtr&) = delete;
    HermesCtr& operator=(const HermesCtr&) = delete;
    ~HermesCtr() noexcept { if (holder_) holder_->unref(); }

    bool       is_null() const noexcept { return holder_ == nullptr; }
    explicit operator bool() const noexcept { return holder_ != nullptr; }
    MemHolder* holder()  const noexcept { return holder_; }
    Arena&     arena()   const noexcept { return holder_->arena(); }

    AnyVal root()           const noexcept { return header_->root; }   // by-value re-anchor
    void   set_root(AnyVal v) noexcept { header_->root = v; }          // assignment lowers

    // The single-segment blob bytes (valid only when this doc is single-chunk, e.g.
    // a compactify() result): {head().data(), head().used}.
    const uint8_t* blob_data() const noexcept { return holder_->arena().head().data(); }
    size_t         blob_size() const noexcept { return holder_->arena().head().used; }

private:
    HermesCtr(MemHolder* h, DocumentHeader* hdr) noexcept : holder_(h), header_(hdr) {}

    // Allocate the DocumentHeader at offset 0 of a fresh arena.
    static logos::expected<DocumentHeader*> init_header(MemHolder* h) noexcept {
        LOGOS_TRY(auto* mem, h->arena().allocate_raw(sizeof(DocumentHeader), alignof(DocumentHeader)));
        return new (mem) DocumentHeader();
    }

    MemHolder*      holder_ = nullptr;
    DocumentHeader* header_ = nullptr;
};

} // namespace logos::hermes2
