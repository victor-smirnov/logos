// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Victor Smirnov
// Logos project — https://github.com/victor-smirnov/logos
//
// HRPC public API entry point.
//
// Include this header to get the full HRPC API:
//   Session, EndpointRegistry, Context, Call, plus TCP convenience helpers.

#pragma once

#include <logos/hrpc/session.hpp>
#include <logos/hrpc/endpoint.hpp>
#include <logos/hrpc/context.hpp>
#include <logos/hrpc/call.hpp>

namespace logos::hrpc {

// ---------------------------------------------------------------------------
// TCP convenience helpers
// ---------------------------------------------------------------------------

// Connect to an HRPC server over TCP. Returns a new Session in Client mode.
// Must be called from within a reactor fiber.
inline Session connect_tcp(const char* host, uint16_t port) {
    auto sock = logos::reactor::TcpSocket::connect_to(host, port);
    return Session(std::move(sock), SessionSide::Client);
}

} // namespace logos::hrpc
