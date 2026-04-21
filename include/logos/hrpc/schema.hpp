// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Victor Smirnov
// Logos project — https://github.com/victor-smirnov/logos
//
// HRPC schema types — Hermes-backed request/response/message wrappers.
//
// All types carry an owned Hermes (the document) plus a TinyMapView into
// its root map. Field key codes mirror Memoria HRPC for future wire compat.

#pragma once

#include <logos/hrpc/common.hpp>
#include <logos/hermes/document.hpp>
#include <logos/hermes/view.hpp>
#include <logos/hermes/any_val.hpp>
#include <logos/hermes/named_code.hpp>
#include <logos/hermes/binary_codec.hpp>
#include <logos/hermes/arena_string.hpp>

#include <string_view>
#include <cstdint>

namespace logos::hrpc {

using logos::hermes::Hermes;
using logos::hermes::HermesView;
using logos::hermes::TinyMapView;
using logos::hermes::AnyVal;
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
    Hermes    doc;
    TinyMapView  map;

    // Create a new empty Request document.
    [[nodiscard]] static logos::expected<Request> make() noexcept {
        Request rq;
        LOGOS_TRY(rq.doc, logos::hermes::make_doc());
        LOGOS_TRY(auto tiny, rq.doc.make_tiny_map(4));
        rq.doc.set_root(tiny);
        rq.map = tiny;
        return rq;
    }

    // Wrap an existing document (used on the receiving side).
    static Request from_doc(Hermes doc) noexcept {
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
    Hermes   doc;
    TinyMapView map;

    // Successful response with no result value.
    [[nodiscard]] static logos::expected<Response> ok() noexcept {
        Response rs;
        LOGOS_TRY(rs.doc, logos::hermes::make_doc());
        LOGOS_TRY(auto tiny, rs.doc.make_tiny_map(4));
        rs.doc.set_root(tiny);
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
        LOGOS_TRY(rs.doc, logos::hermes::make_doc());
        LOGOS_TRY(auto root, rs.doc.make_tiny_map(4));
        rs.doc.set_root(root);
        rs.map = root;

        LOGOS_TRY_VOID(rs.map.put(keys::STATUS_CODE,
                       AnyVal::from_value(static_cast<uint32_t>(StatusCode::Error))));

        // Error sub-map with description string.
        LOGOS_TRY(auto err_map,  rs.doc.make_tiny_map(2));
        LOGOS_TRY(auto desc_str, rs.doc.make_string(description));
        LOGOS_TRY_VOID(err_map.put(keys::ERROR_DESC, AnyVal::from_offset(desc_str.offset())));
        LOGOS_TRY_VOID(rs.map.put(keys::ERROR, AnyVal::from_offset(err_map.offset())));
        return rs;
    }

    // Wrap an existing document (used on the receiving side).
    static Response from_doc(Hermes doc) noexcept {
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
        TinyMapView err_map(err_val.to_offset(), doc.holder());
        if (!err_map.has_key(keys::ERROR_DESC)) return {};
        AnyVal desc_val = err_map.get(keys::ERROR_DESC);
        if (desc_val.is_null()) return {};

        // desc_val is a pointer to ArenaString.
        logos::hermes::StringView sv(desc_val.to_offset(), doc.holder());
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
    Hermes   doc;
    TinyMapView map;

    [[nodiscard]] static logos::expected<StreamMessage> make(AnyVal data) noexcept {
        StreamMessage msg;
        LOGOS_TRY(msg.doc, logos::hermes::make_doc());
        LOGOS_TRY(auto tiny, msg.doc.make_tiny_map(2));
        msg.doc.set_root(tiny);
        msg.map = tiny;
        LOGOS_TRY_VOID(msg.map.put(keys::MSG_DATA, data));
        return msg;
    }

    static StreamMessage from_doc(Hermes doc) noexcept {
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
    Hermes   doc;
    TinyMapView map;

    [[nodiscard]] static logos::expected<ConnectionMetadata> make(uint64_t buffer_size = 1024 * 1024) noexcept {
        ConnectionMetadata meta;
        LOGOS_TRY(meta.doc, logos::hermes::make_doc());
        LOGOS_TRY(auto tiny, meta.doc.make_tiny_map(2));
        meta.doc.set_root(tiny);
        meta.map = tiny;
        // uint64_t doesn't fit in 7 bytes as value mode, store as uint32_t
        // (buffer sizes under 4GB are sufficient).
        LOGOS_TRY_VOID(meta.map.put(keys::CHAN_BUF_SIZE,
                         AnyVal::from_value(static_cast<uint32_t>(buffer_size))));
        return meta;
    }

    static ConnectionMetadata from_doc(Hermes doc) noexcept {
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
