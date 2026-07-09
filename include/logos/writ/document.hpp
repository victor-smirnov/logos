// Logos project — https://github.com/victor-smirnov/logos

#pragma once

#include <new>
#include <cstring>
#include <string_view>
#include <type_traits>

#include <logos/writ/mem_holder.hpp>
#include <logos/writ/any_val.hpp>
#include <logos/writ/tiny_object_map.hpp>
#include <logos/writ/object_array.hpp>
#include <logos/writ/object_map.hpp>
#include <logos/writ/arena_string.hpp>
#include <logos/writ/view.hpp>
#include <logos/core/expected.hpp>

namespace logos::writ {

// DocumentHeader — the untagged header at OFFSET 0 of every Writ document arena:
// it holds the document root (an at-rest AnyVal). Putting it at offset 0 means a
// loaded blob always finds the root at the start.
struct DocumentHeader {
    AnyVal root;
};

// WritCtr — an OWNING handle to a Writ document: the MemHolder (residency) plus
// its in-arena DocumentHeader (the root slot). Move-only (a unique owning Rc); the
// holder's refcount is released on destruction.
class WritCtr {
public:
    WritCtr() noexcept = default;

    // Create an empty document (root = null) in a fresh holder.
    [[nodiscard]] static logos::expected<WritCtr>
    make(size_t capacity = 4096, ArenaMode mode = ArenaMode::MultiChunk) noexcept {
        LOGOS_TRY(auto* h, MemHolder::make(capacity, mode));     // refcount 1
        auto hr = init_header(h);
        if (!hr) { h->unref(); return std::unexpected(std::move(hr.error())); }
        return WritCtr(h, *hr);
    }

    // Load a document from a rigid single-segment blob (a compactify() dump). The
    // DocumentHeader is at offset 0 of the blob.
    [[nodiscard]] static logos::expected<WritCtr>
    from_bytes(const void* data, size_t size) noexcept {
        LOGOS_TRY(auto* h, MemHolder::from_bytes(data, size));   // refcount 1
        auto* hdr = reinterpret_cast<DocumentHeader*>(h->arena().head().data());
        return WritCtr(h, hdr);
    }

    // Wrap an EXISTING holder as a shared-owning doc handle (the legacy
    // `WritView(holder)` / `Writ(holder)` spelling): takes a +1 ref, so the
    // holder outlives this handle; the header is at offset 0. Used by emit_module /
    // reflection to read/extend a holder owned elsewhere (e.g. prog.type_pool).
    explicit WritCtr(MemHolder* h) noexcept
        : holder_(h),
          header_(h ? reinterpret_cast<DocumentHeader*>(h->arena().head().data()) : nullptr) {
        if (holder_) holder_->ref();
    }

    WritCtr(WritCtr&& o) noexcept : holder_(o.holder_), header_(o.header_) {
        o.holder_ = nullptr; o.header_ = nullptr;
    }
    WritCtr& operator=(WritCtr&& o) noexcept {
        if (this != &o) {
            if (holder_) holder_->unref();
            holder_ = o.holder_; header_ = o.header_;
            o.holder_ = nullptr; o.header_ = nullptr;
        }
        return *this;
    }
    // COPYABLE — shared refcounted ownership (the legacy `Writ = Own<WritView>`
    // semantics). A copy takes a +1 ref on the holder + shares the header; the doc
    // lives as long as any handle. Needed because logosc copies AST handles (into
    // module lists, caches, ParsedModule) rather than moving them.
    WritCtr(const WritCtr& o) noexcept : holder_(o.holder_), header_(o.header_) {
        if (holder_) holder_->ref();
    }
    WritCtr& operator=(const WritCtr& o) noexcept {
        if (this != &o) {
            if (o.holder_) o.holder_->ref();
            if (holder_) holder_->unref();
            holder_ = o.holder_; header_ = o.header_;
        }
        return *this;
    }
    ~WritCtr() noexcept { if (holder_) holder_->unref(); }

    bool       is_null() const noexcept { return holder_ == nullptr; }
    explicit operator bool() const noexcept { return holder_ != nullptr; }
    MemHolder* holder()  const noexcept { return holder_; }
    Arena&     arena()   const noexcept { return holder_->arena(); }

    // The DocumentHeader lives at the head chunk's offset 0. RECOMPUTE it from the
    // holder on every access rather than trusting the cached `header_`: a
    // GrowableSingleChunk arena reallocs (moves) when it grows (e.g. a metaprog subst
    // that appends to a from_bytes doc sized exactly to the blob), which would dangle
    // a cached header ptr. holder_->base() is always the current head base.
    // Null-safe — a default/empty handle reads as a null root (lazy/placeholder slots).
    DocumentHeader* live_header() const noexcept {
        return holder_ ? reinterpret_cast<DocumentHeader*>(holder_->base()) : nullptr;
    }
    AnyVal root()           const noexcept { auto* h = live_header(); return h ? h->root : AnyVal{}; }
    void   set_root(AnyVal v) noexcept { if (auto* h = live_header()) h->root = v; }
    Object root_object()    const noexcept { auto* h = live_header(); return h ? Object(h->root, holder_) : Object{}; }

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
    // set_root(av). Mirror the legacy make_tiny_map/make_array/make_string surface
    // so the logosc producer (parser/codegen) is a near-mechanical rename.
    // Tiny map → RAW pointer (node-building: `node->put(...)`). Arrays/maps/strings →
    // OWNING views (the parser's handle style: `.push_back(av).get()`, `.to_anyval()`).
    [[nodiscard]] logos::expected<TinyObjectMap*> make_tiny_map(uint64_t cap = 4) noexcept {
        return TinyObjectMap::create(arena(), cap);
    }
    // View-returning tiny-map producer (the node-builder path that wants the
    // wrapper rather than the raw pointer — use .offset()/.put()/.to_anyval()).
    [[nodiscard]] logos::expected<TinyMapView> make_tiny_map_view(uint64_t cap = 4) noexcept {
        LOGOS_TRY(auto* m, TinyObjectMap::create(arena(), cap));
        return TinyMapView(m, holder_);
    }
    [[nodiscard]] logos::expected<ArrayView> make_array(uint64_t cap = 4) noexcept {
        LOGOS_TRY(auto* a, ObjectArray::create(arena(), cap));
        return ArrayView(a, holder_);
    }
    [[nodiscard]] logos::expected<MapView> make_object_map(uint64_t cap = 8) noexcept {
        LOGOS_TRY(auto* m, ObjectMap::create(arena(), cap));
        return MapView(m, holder_);
    }
    // Producer for a typed container with a static create(Arena&, cap) but no
    // dedicated owning view (TypedArray<T> / TypedMap<T>) — returns the raw
    // pointer. Routes creation through the document even where rule-3 view access
    // doesn't apply. M is resolved at the call site (no extra include needed here).
    template <typename M>
    [[nodiscard]] logos::expected<M*> make_typed(uint64_t cap) noexcept {
        return M::create(arena(), cap);
    }
    [[nodiscard]] logos::expected<StringView> make_string(std::string_view s) noexcept {
        LOGOS_TRY(auto* s2, ArenaString::create(arena(), s));
        return StringView(s2, holder_);
    }

    // Box a wide scalar (i64/u64/f32/f64 — doesn't fit AnyVal's inline Pod niche)
    // into this document and return a Ref AnyVal to it. The WritCtr-encapsulated
    // successor of the free anyval_put(arena, v) — producers box through the
    // document, never the raw arena.
    template <typename T>
    [[nodiscard]] logos::expected<AnyVal> box(T v) noexcept {
        uint64_t code;
        if constexpr (std::is_same_v<T, double>)     code = tc::F64;
        else if constexpr (std::is_same_v<T, float>) code = tc::F32;
        else if constexpr (std::is_unsigned_v<T>)    code = tc::U64;
        else                                         code = tc::I64;
        LOGOS_TRY(void* mem, arena().allocate(sizeof(T),
                                              alignof(T) < 2 ? 2 : alignof(T), TypeTag(code)));
        std::memcpy(mem, &v, sizeof(T));
        AnyVal a; a.set_ref(mem); return a;
    }

    // Integer AnyVal: inline Pod when the value fits the 56-bit niche, boxed as
    // W_I64 otherwise. Mirrors stdlib/lang/writ/container.logos:make_int, so a
    // document built by the C++ producer and one built by the Logos producer
    // encode the same integer identically.
    [[nodiscard]] logos::expected<AnyVal> make_int(int64_t v) noexcept {
        if (AnyVal::fits_i56(v)) return AnyVal::pod(v, uint8_t(tc::WA_I56));
        return box<int64_t>(v);
    }
    // Float AnyVal: always boxed (an f64 never fits the inline Pod niche).
    // Mirrors container.logos:box_f64.
    [[nodiscard]] logos::expected<AnyVal> make_f64(double v) noexcept {
        return box<double>(v);
    }

    // The single-segment blob bytes (valid only when this doc is single-chunk, e.g.
    // a compactify() result): {head().data(), head().used}.
    const uint8_t* blob_data() const noexcept { return holder_->arena().head().data(); }
    size_t         blob_size() const noexcept { return holder_->arena().head().used; }

private:
    WritCtr(MemHolder* h, DocumentHeader* hdr) noexcept : holder_(h), header_(hdr) {}

    // Allocate the DocumentHeader at offset 0 of a fresh arena.
    static logos::expected<DocumentHeader*> init_header(MemHolder* h) noexcept {
        LOGOS_TRY(auto* mem, h->arena().allocate_raw(sizeof(DocumentHeader), alignof(DocumentHeader)));
        return new (mem) DocumentHeader();
    }

    MemHolder*      holder_ = nullptr;
    DocumentHeader* header_ = nullptr;
};

// The DocumentHeader sits at offset 0 of a holder's head chunk (WritCtr::make and
// compactify both place it there). Used by the multi-arena layer to reach a
// registered module's root without a WritCtr handle.
inline DocumentHeader* doc_header(MemHolder* h) noexcept {
    return reinterpret_cast<DocumentHeader*>(h->arena().head().data());
}

// legacy-spelling factory. The logosc producer (parser) builds into a NEVER-MOVE
// MultiChunk arena: it grows by APPENDING chunks, so an existing object never moves and
// the parser's held node ptrs / owning views stay valid across allocations (a
// single-chunk realloc would dangle them). The trade-off: raw `base + offset`
// addressing is INVALID across a chunk boundary, so metaprog code that walks the AST
// must resolve()/follow Refs (position-independent) — never reconstruct `base + off`.
[[nodiscard]] inline logos::expected<WritCtr>
make_doc(size_t capacity = 65536, ArenaMode mode = ArenaMode::MultiChunk) noexcept {
    return WritCtr::make(capacity, mode);
}

// Single-segment doc for METAPROG producers that address the tree by raw
// `base(doc) + offset` (quote_item! / blob substitution / reflection / mlir-gen
// reflection blobs). A GrowableSingleChunk guarantees ONE contiguous segment so
// base+offset is valid; pre-sized past any realloc so the in-flight container `this`
// and held base+offset never dangle. Lazy-zero (arena.cpp) keeps the reserve cheap
// (only touched pages commit). Parser docs do NOT use this — they hold owning views
// and need MultiChunk never-move; they also never base+offset across a chunk.
[[nodiscard]] inline logos::expected<WritCtr>
make_doc_single_chunk(size_t min_capacity = 0) noexcept {
    constexpr size_t PRESIZE = size_t(8) * 1024 * 1024;   // 8 MiB lazy reserve
    return WritCtr::make(min_capacity < PRESIZE ? PRESIZE : min_capacity,
                           ArenaMode::GrowableSingleChunk);
}

} // namespace logos::writ
