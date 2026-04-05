// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Victor Smirnov
//
// Fiber-aware UDP socket.
//
// Uses io_uring sendmsg/recvmsg internally, so each send/recv suspends the
// calling fiber until the kernel completes the operation.
//
// Two usage modes:
//
// Unconnected (server / listener):
//   auto sock = UdpSocket::bind_to("0.0.0.0", 9000).get();
//   UdpEndpoint from;
//   char buf[1500];
//   int n = sock.recv_from(buf, sizeof(buf), from).get();
//   sock.send_to(buf, n, from).get();  // echo
//
// Connected (client — fixed peer, no address per datagram):
//   auto sock = UdpSocket::connect_to("127.0.0.1", 9000).get();
//   sock.send("hello", 5).get();
//   char reply[64]; int n = sock.recv(reply, sizeof(reply)).get();

#pragma once

#include <logos/reactor/reactor.hpp>
#include <logos/verification/assert.hpp>

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstring>
#include <string>
#include <logos/core/expected.hpp>

LOGOS_NS_BEGIN

// ---------------------------------------------------------------------------
// UdpEndpoint — a (host, port) pair for addressing datagrams.
// ---------------------------------------------------------------------------
struct UdpEndpoint {
    sockaddr_in addr{};

    LOGOS_RED UdpEndpoint() { addr.sin_family = AF_INET; }
    LOGOS_RED ~UdpEndpoint() = default;

    LOGOS_RED static logos::expected<UdpEndpoint> make(const char* host, uint16_t port) noexcept {
        UdpEndpoint ep;
        ep.addr.sin_port = htons(port);
        if (!host || host[0] == '\0' || std::string(host) == "0.0.0.0") {
            ep.addr.sin_addr.s_addr = INADDR_ANY;
        } else {
            if (inet_aton(host, &ep.addr.sin_addr) == 0)
                return std::unexpected(logos::err(ErrCode::invalid_host));
        }
        return ep;
    }

    LOGOS_RED uint16_t    port() const noexcept { return ntohs(addr.sin_port); }
    LOGOS_RED std::string host() const { return inet_ntoa(addr.sin_addr); }

    LOGOS_RED sockaddr*       as_sockaddr()       noexcept { return reinterpret_cast<sockaddr*>(&addr); }
    LOGOS_RED const sockaddr* as_sockaddr() const noexcept { return reinterpret_cast<const sockaddr*>(&addr); }
    LOGOS_RED socklen_t       len()         const noexcept { return sizeof(addr); }
};

// ---------------------------------------------------------------------------
// UdpSocket
// ---------------------------------------------------------------------------
class UdpSocket {
public:
    LOGOS_RED UdpSocket() = default;
    LOGOS_RED ~UdpSocket() { close(); }

    UdpSocket(const UdpSocket&)            = delete;
    UdpSocket& operator=(const UdpSocket&) = delete;

    LOGOS_RED UdpSocket(UdpSocket&& o) noexcept : fd_(o.fd_) { o.fd_ = -1; }
    LOGOS_RED UdpSocket& operator=(UdpSocket&& o) noexcept {
        if (this != &o) { close(); fd_ = o.fd_; o.fd_ = -1; }
        return *this;
    }

    // -----------------------------------------------------------------------
    // Factories
    // -----------------------------------------------------------------------

    [[nodiscard]] LOGOS_RED
    static logos::expected<UdpSocket> bind_to(const char* host, uint16_t port) noexcept {
        LOGOS_TRY(auto ep, UdpEndpoint::make(host, port));
        LOGOS_TRY(auto fd, make_socket());

        int one = 1;
        setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
        setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &one, sizeof(one));

        if (::bind(fd, ep.as_sockaddr(), ep.len()) < 0) {
            ::close(fd);
            return std::unexpected(logos::err(ErrCode::bind_error));
        }
        return UdpSocket{fd};
    }

    [[nodiscard]] LOGOS_RED
    static logos::expected<UdpSocket> connect_to(const char* host, uint16_t port) noexcept {
        LOGOS_TRY(auto ep, UdpEndpoint::make(host, port));
        LOGOS_TRY(auto fd, make_socket());

        if (::connect(fd, ep.as_sockaddr(), ep.len()) < 0) {
            ::close(fd);
            return std::unexpected(logos::err(ErrCode::connect_error));
        }
        return UdpSocket{fd};
    }

    [[nodiscard]] LOGOS_RED
    static logos::expected<UdpSocket> create() noexcept {
        LOGOS_TRY(auto fd, make_socket());
        return UdpSocket{fd};
    }

    // -----------------------------------------------------------------------
    // Unconnected send/recv (explicit endpoint per datagram)
    // -----------------------------------------------------------------------

    [[nodiscard]]
    logos::expected<int> send_to(const void* buf, size_t size, const UdpEndpoint& to) noexcept {
        iovec  iov{ const_cast<void*>(buf), size };
        msghdr mh{};
        mh.msg_name    = const_cast<sockaddr*>(to.as_sockaddr());
        mh.msg_namelen = to.len();
        mh.msg_iov     = &iov;
        mh.msg_iovlen  = 1;
        return Reactor::current()->sendmsg(fd_, &mh, 0);
    }

    [[nodiscard]]
    logos::expected<int> recv_from(void* buf, size_t size, UdpEndpoint& from) noexcept {
        iovec  iov{ buf, size };
        msghdr mh{};
        mh.msg_name    = from.as_sockaddr();
        mh.msg_namelen = from.len();
        mh.msg_iov     = &iov;
        mh.msg_iovlen  = 1;
        LOGOS_TRY(auto n, Reactor::current()->recvmsg(fd_, &mh, 0));
        from.addr.sin_port = reinterpret_cast<sockaddr_in*>(mh.msg_name)->sin_port;
        return n;
    }

    // -----------------------------------------------------------------------
    // Connected send/recv (no address needed per datagram)
    // -----------------------------------------------------------------------

    [[nodiscard]]
    logos::expected<int> send(const void* buf, size_t size, int flags = 0) noexcept {
        return Reactor::current()->send(fd_, buf, size, flags);
    }

    [[nodiscard]]
    logos::expected<int> recv(void* buf, size_t size, int flags = 0) noexcept {
        return Reactor::current()->recv(fd_, buf, size, flags);
    }

    // -----------------------------------------------------------------------
    // Lifecycle
    // -----------------------------------------------------------------------

    LOGOS_RED void close() noexcept { if (fd_ >= 0) { ::close(fd_); fd_ = -1; } }
    LOGOS_RED bool valid() const noexcept { return fd_ >= 0; }
    LOGOS_RED int  fd()    const noexcept { return fd_; }

private:
    LOGOS_RED explicit UdpSocket(int fd) : fd_(fd) {}

    LOGOS_RED static logos::expected<int> make_socket() noexcept {
        int fd = ::socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
        if (fd < 0) return std::unexpected(logos::err(ErrCode::socket_error));
        return fd;
    }

    int fd_ = -1;
};

LOGOS_NS_END
