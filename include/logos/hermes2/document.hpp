// Logos project — https://github.com/victor-smirnov/logos

#pragma once

#include <new>
#include <cstring>
#include <string_view>

#include <logos/hermes2/mem_holder.hpp>
#include <logos/hermes2/any_val.hpp>
#include <logos/hermes2/tiny_object_map.hpp>
#include <logos/hermes2/object_array.hpp>
#include <logos/hermes2/object_map.hpp>
#include <logos/hermes2/arena_string.hpp>
#include <logos/hermes2/view.hpp>
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

    // Wrap an EXISTING holder as a shared-owning doc handle (the Hermes1
    // `HermesView(holder)` / `Hermes(holder)` spelling): takes a +1 ref, so the
    // holder outlives this handle; the header is at offset 0. Used by emit_module /
    // reflection to read/extend a holder owned elsewhere (e.g. prog.type_pool).
    explicit HermesCtr(MemHolder* h) noexcept
        : holder_(h),
          header_(h ? reinterpret_cast<DocumentHeader*>(h->arena().head().data()) : nullptr) {
        if (holder_) holder_->ref();
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
    // COPYABLE — shared refcounted ownership (the Hermes1 `Hermes = Own<HermesView>`
    // semantics). A copy takes a +1 ref on the holder + shares the header; the doc
    // lives as long as any handle. Needed because logosc copies AST handles (into
    // module lists, caches, ParsedModule) rather than moving them.
    HermesCtr(const HermesCtr& o) noexcept : holder_(o.holder_), header_(o.header_) {
        if (holder_) holder_->ref();
    }
    HermesCtr& operator=(const HermesCtr& o) noexcept {
        if (this != &o) {
            if (o.holder_) o.holder_->ref();
            if (holder_) holder_->unref();
            holder_ = o.holder_; header_ = o.header_;
        }
        return *this;
    }
    ~HermesCtr() noexcept { if (holder_) holder_->unref(); }

    bool       is_null() const noexcept { return holder_ == nullptr; }
    explicit operator bool() const noexcept { return holder_ != nullptr; }
    MemHolder* holder()  const noexcept { return holder_; }
    Arena&     arena()   const noexcept { return holder_->arena(); }

    // Null-safe (a default/empty doc handle reads as null root) — Hermes1's Own<>
    // returned a null Object for a null handle, and logosc relies on that for
    // lazy/placeholder AST slots.
    AnyVal root()           const noexcept { return header_ ? header_->root : AnyVal{}; }
    void   set_root(AnyVal v) noexcept { header_->root = v; }          // assignment lowers
    Object root_object()    const noexcept { return header_ ? Object(header_->root, holder_) : Object{}; }

    // Seal the arena: forbid further allocations (the document becomes immutable).
    void seal() noexcept { holder_->arena().seal(); }

    // Parser backtracking watermark. The generated parser calls this on every
    // alternative but does NOT roll back (the result is [[maybe_unused]]); on the
    // never-move MultiChunk parser doc, a failed alternative's nodes are simply left
    // dead (unreferenced) — correct, just transient memory. Returns a monotonic token.
    size_t arena_checkpoint() const noexcept { return holder_->arena().total_used(); }

    // ── Producer conveniences (build objects directly in this document's arena) ──
    // Thin wrappers over the container ::create factories. Returned pointers are
    // stable (never-move arena). Wire them into a parent via AnyVal::set_ref(ptr) /
    // set_root(av). Mirror the Hermes1 make_tiny_map/make_array/make_string surface
    // so the logosc producer (parser/codegen) is a near-mechanical rename.
    // Tiny map → RAW pointer (node-building: `node->put(...)`). Arrays/maps/strings →
    // OWNING views (the parser's handle style: `.push_back(av).get()`, `.to_anyval()`).
    [[nodiscard]] logos::expected<TinyObjectMap*> make_tiny_map(uint64_t cap = 4) noexcept {
        return TinyObjectMap::create(arena(), cap);
    }
    [[nodiscard]] logos::expected<ArrayView> make_array(uint64_t cap = 4) noexcept {
        LOGOS_TRY(auto* a, ObjectArray::create(arena(), cap));
        return ArrayView(a, holder_);
    }
    [[nodiscard]] logos::expected<MapView> make_object_map(uint64_t cap = 8) noexcept {
        LOGOS_TRY(auto* m, ObjectMap::create(arena(), cap));
        return MapView(m, holder_);
    }
    [[nodiscard]] logos::expected<StringView> make_string(std::string_view s) noexcept {
        LOGOS_TRY(auto* s2, ArenaString::create(arena(), s));
        return StringView(s2, holder_);
    }

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

// The DocumentHeader sits at offset 0 of a holder's head chunk (HermesCtr::make and
// compactify both place it there). Used by the multi-arena layer to reach a
// registered module's root without a HermesCtr handle.
inline DocumentHeader* doc_header(MemHolder* h) noexcept {
    return reinterpret_cast<DocumentHeader*>(h->arena().head().data());
}

// Hermes1-spelling factory for the logosc producer (parser/quote/reflection). The doc
// is a PRE-SIZED GrowableSingleChunk: the metaprog code addresses the AST by raw
// `base + offset` (e.g. lower_quote_item's walkers), which REQUIRES one contiguous
// chunk; and a never-move single segment also keeps the parser's held node/view ptrs
// valid (a realloc would both scatter the base+offset model AND dangle them). Lazy
// zero (arena.cpp) keeps RSS to touched pages, so the big reserve is cheap. The reserve
// is sized so realistic module ASTs never realloc; a genuinely larger AST still grows
// (rare) — bump PRESIZE if a real module exceeds it.
[[nodiscard]] inline logos::expected<HermesCtr>
make_doc(size_t capacity = 65536, ArenaMode mode = ArenaMode::GrowableSingleChunk) noexcept {
    constexpr size_t PRESIZE = size_t(64) * 1024 * 1024;   // 64 MiB lazy reserve
    if (mode == ArenaMode::GrowableSingleChunk && capacity < PRESIZE) capacity = PRESIZE;
    return HermesCtr::make(capacity, mode);
}

} // namespace logos::hermes2
