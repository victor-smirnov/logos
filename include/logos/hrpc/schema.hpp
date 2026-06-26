// Logos project — https://github.com/victor-smirnov/logos
//
// HRPC schema types — Writ-backed request/response/message wrappers.
//
// All types carry an owned Writ (the document) plus a TinyMapView into
// its root map. Field key codes mirror Memoria HRPC for future wire compat.

#pragma once

#include <logos/hrpc/common.hpp>
#include <logos/writ/document.hpp>
#include <logos/writ/compat.hpp>
#include <logos/writ/view.hpp>
#include <logos/writ/any_val.hpp>
#include <logos/core/named_code.hpp>
#include <logos/writ/binary_codec.hpp>
#include <logos/writ/arena_string.hpp>

#include <string_view>
#include <cstdint>

namespace logos::hrpc {

using logos::writ::Writ;
using logos::writ::WritView;
using logos::writ::TinyMapView;
using logos::writ::AnyVal;
using logos::NamedCode;

// ---------------------------------------------------------------------------
// Field key constants — uint8_t codes for TinyObjectMap keys.
// Values match Memoria HRPC schema for binary compatibility.
// ---------------------------------------------------------------------------

namespace keys {

// Request fields
inline constexpr NamedCode<uint8_t> PARAMETERS    {"PARAMETERS",    1};
inline constexpr NamedCode<uint8_t> INPUT_CHANNELS {"INPUT_CHANNELS", 2};
inline constexpr NamedCode<uint8_t> OUTPUT_CHANNELS{"OUTPUT_CHANNELS",3};

// Response fields
inline constexpr NamedCode<uint8_t> RESULT       {"RESULT",       1};
inline constexpr NamedCode<uint8_t> STATUS_CODE  {"STATUS_CODE",  2};
inline constexpr NamedCode<uint8_t> ERROR        {"ERROR",        3};

// Error fields
inline constexpr NamedCode<uint8_t> ERROR_TYPE   {"ERROR_TYPE",   1};
inline constexpr NamedCode<uint8_t> ERROR_DESC   {"ERROR_DESC",   2};

// StreamMessage fields
inline constexpr NamedCode<uint8_t> MSG_DATA     {"MSG_DATA",     1};

// ConnectionMetadata fields
inline constexpr NamedCode<uint8_t> CHAN_BUF_SIZE{"CHAN_BUF_SIZE", 1};

} // namespace keys

// ---------------------------------------------------------------------------
// StatusCode
// ---------------------------------------------------------------------------

enum class StatusCode : uint32_t {
    Ok    = 0,
    Error = 1,
};

// ---------------------------------------------------------------------------
// Request — parameters for an RPC call.
//
// Wire format: TinyObjectMap root with optional fields:
//   PARAMETERS (1)     — any AnyVal
//   INPUT_CHANNELS (2) — uint16_t embedded value
//   OUTPUT_CHANNELS(3) — uint16_t embedded value
// ---------------------------------------------------------------------------

struct Request {
    Writ    doc;
    TinyMapView  map;

    // Create a new empty Request document.
    [[nodiscard]] static logos::expected<Request> make() noexcept {
        Request rq;
        LOGOS_TRY(rq.doc, logos::writ::make_doc());
        LOGOS_TRY(auto tiny_raw, rq.doc.make_tiny_map(4));
        TinyMapView tiny(tiny_raw, rq.doc.holder());
        rq.doc.set_root(tiny.to_anyval());
        rq.map = tiny;
        return rq;
    }

    // Wrap an existing document (used on the receiving side).
    static Request from_doc(Writ doc) noexcept {
        Request rq;
        rq.doc = std::move(doc);
        rq.map = rq.doc.root_object().as_tiny_map();
        return rq;
    }

    [[nodiscard]] logos::expected<void> set_param(NamedCode<uint8_t> key, AnyVal value) noexcept {
        return map.put(key, value);
    }

    AnyVal get_param(NamedCode<uint8_t> key) const noexcept {
        if (!map.has_key(key)) return AnyVal{};
        return map.get(key);
    }

    uint16_t input_channels() const noexcept {
        if (!map.has_key(keys::INPUT_CHANNELS)) return 0;
        return map.get(keys::INPUT_CHANNELS).as_value<uint16_t>();
    }

    uint16_t output_channels() const noexcept {
        if (!map.has_key(keys::OUTPUT_CHANNELS)) return 0;
        return map.get(keys::OUTPUT_CHANNELS).as_value<uint16_t>();
    }

    [[nodiscard]] logos::expected<void> set_input_channels(uint16_t n) noexcept {
        return map.put(keys::INPUT_CHANNELS, AnyVal::from_value(n));
    }

    [[nodiscard]] logos::expected<void> set_output_channels(uint16_t n) noexcept {
        return map.put(keys::OUTPUT_CHANNELS, AnyVal::from_value(n));
    }
};

// ---------------------------------------------------------------------------
// Response — result of an RPC call.
//
// Wire format: TinyObjectMap root with fields:
//   STATUS_CODE (2) — uint32_t embedded (StatusCode)
//   RESULT (1)      — optional, any AnyVal (on success)
//   ERROR (3)       — optional, pointer to inner TinyObjectMap (on error)
//     ERROR_DESC(2) — string
// ---------------------------------------------------------------------------

struct Response {
    Writ   doc;
    TinyMapView map;

    // Successful response with no result value.
    [[nodiscard]] static logos::expected<Response> ok() noexcept {
        Response rs;
        LOGOS_TRY(rs.doc, logos::writ::make_doc());
        LOGOS_TRY(auto tiny_raw, rs.doc.make_tiny_map(4));
        TinyMapView tiny(tiny_raw, rs.doc.holder());
        rs.doc.set_root(tiny.to_anyval());
        rs.map = tiny;
        LOGOS_TRY_VOID(rs.map.put(keys::STATUS_CODE,
                       AnyVal::from_value(static_cast<uint32_t>(StatusCode::Ok))));
        return rs;
    }

    // Successful response with a result value.
    [[nodiscard]] static logos::expected<Response> ok(AnyVal result) noexcept {
        LOGOS_TRY(auto rs, Response::ok());
        LOGOS_TRY_VOID(rs.map.put(keys::RESULT, result));
        return rs;
    }

    // Error response with a human-readable description.
    [[nodiscard]] static logos::expected<Response> error(std::string_view description) noexcept {
        Response rs;
        LOGOS_TRY(rs.doc, logos::writ::make_doc());
        LOGOS_TRY(auto root_raw, rs.doc.make_tiny_map(4));
        TinyMapView root(root_raw, rs.doc.holder());
        rs.doc.set_root(root.to_anyval());
        rs.map = root;

        LOGOS_TRY_VOID(rs.map.put(keys::STATUS_CODE,
                       AnyVal::from_value(static_cast<uint32_t>(StatusCode::Error))));

        // Error sub-map with description string.
        LOGOS_TRY(auto err_map_raw,  rs.doc.make_tiny_map(2));
        TinyMapView err_map(err_map_raw, rs.doc.holder());
        LOGOS_TRY(auto desc_str, rs.doc.make_string(description));
        LOGOS_TRY_VOID(err_map.put(keys::ERROR_DESC, desc_str.to_anyval()));
        LOGOS_TRY_VOID(rs.map.put(keys::ERROR, err_map.to_anyval()));
        return rs;
    }

    // Wrap an existing document (used on the receiving side).
    static Response from_doc(Writ doc) noexcept {
        Response rs;
        rs.doc = std::move(doc);
        rs.map = rs.doc.root_object().as_tiny_map();
        return rs;
    }

    bool is_ok() const noexcept {
        if (!map.has_key(keys::STATUS_CODE)) return false;
        uint32_t code = map.get(keys::STATUS_CODE).as_value<uint32_t>();
        return code == static_cast<uint32_t>(StatusCode::Ok);
    }

    AnyVal result() const noexcept {
        if (!map.has_key(keys::RESULT)) return AnyVal{};
        return map.get(keys::RESULT);
    }

    // Returns a view into the arena's error description string, or empty view if none.
    // Lifetime: tied to this Response's arena — do not outlive the Response.
    std::string_view error_description() const noexcept {
        if (!map.has_key(keys::ERROR)) return {};
        AnyVal err_val = map.get(keys::ERROR);
        if (err_val.is_null()) return {};

        // err_val is a pointer to TinyObjectMap.
        TinyMapView err_map(err_val, doc.holder());
        if (!err_map.has_key(keys::ERROR_DESC)) return {};
        AnyVal desc_val = err_map.get(keys::ERROR_DESC);
        if (desc_val.is_null()) return {};

        // desc_val is a pointer to ArenaString.
        logos::writ::StringView sv(desc_val, doc.holder());
        return sv.view();
    }
};

// ---------------------------------------------------------------------------
// StreamMessage — a single chunk of data on a streaming channel.
//
// Wire format: TinyObjectMap root with field:
//   MSG_DATA (1) — any AnyVal
// ---------------------------------------------------------------------------

struct StreamMessage {
    Writ   doc;
    TinyMapView map;

    [[nodiscard]] static logos::expected<StreamMessage> make(AnyVal data) noexcept {
        StreamMessage msg;
        LOGOS_TRY(msg.doc, logos::writ::make_doc());
        LOGOS_TRY(auto tiny_raw, msg.doc.make_tiny_map(2));
        TinyMapView tiny(tiny_raw, msg.doc.holder());
        msg.doc.set_root(tiny.to_anyval());
        msg.map = tiny;
        LOGOS_TRY_VOID(msg.map.put(keys::MSG_DATA, data));
        return msg;
    }

    static StreamMessage from_doc(Writ doc) noexcept {
        StreamMessage msg;
        msg.doc = std::move(doc);
        msg.map = msg.doc.root_object().as_tiny_map();
        return msg;
    }

    AnyVal data() const noexcept {
        if (!map.has_key(keys::MSG_DATA)) return AnyVal{};
        return map.get(keys::MSG_DATA);
    }
};

// ---------------------------------------------------------------------------
// ConnectionMetadata — exchanged during session negotiation.
//
// Wire format: TinyObjectMap root with field:
//   CHAN_BUF_SIZE (1) — uint64_t (channel buffer size in bytes)
// ---------------------------------------------------------------------------

struct ConnectionMetadata {
    Writ   doc;
    TinyMapView map;

    [[nodiscard]] static logos::expected<ConnectionMetadata> make(uint64_t buffer_size = 1024 * 1024) noexcept {
        ConnectionMetadata meta;
        LOGOS_TRY(meta.doc, logos::writ::make_doc());
        LOGOS_TRY(auto tiny_raw, meta.doc.make_tiny_map(2));
        TinyMapView tiny(tiny_raw, meta.doc.holder());
        meta.doc.set_root(tiny.to_anyval());
        meta.map = tiny;
        // uint64_t doesn't fit in 7 bytes as value mode, store as uint32_t
        // (buffer sizes under 4GB are sufficient).
        LOGOS_TRY_VOID(meta.map.put(keys::CHAN_BUF_SIZE,
                         AnyVal::from_value(static_cast<uint32_t>(buffer_size))));
        return meta;
    }

    static ConnectionMetadata from_doc(Writ doc) noexcept {
        ConnectionMetadata meta;
        meta.doc = std::move(doc);
        meta.map = meta.doc.root_object().as_tiny_map();
        return meta;
    }

    uint64_t channel_buffer_size() const noexcept {
        if (!map.has_key(keys::CHAN_BUF_SIZE)) return 1024 * 1024;
        return static_cast<uint64_t>(
            map.get(keys::CHAN_BUF_SIZE).as_value<uint32_t>());
    }
};

} // namespace logos::hrpc
