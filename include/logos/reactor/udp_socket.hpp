// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Victor Smirnov
// Logos project — https://github.com/victor-smirnov/logos
//
// Fiber-aware UDP socket.
//
// Uses io_uring sendmsg/recvmsg internally, so each send/recv suspends the
// calling fiber until the kernel completes the operation.
//
// Two usage modes:
//
// Unconnected (server / listener):
//   auto sock = UdpSocket::bind_to("0.0.0.0", 9000);
//   UdpEndpoint from;
//   char buf[1500];
//   int n = sock.recv_from(buf, sizeof(buf), from);
//   sock.send_to(buf, n, from);  // echo
//
// Connected (client — fixed peer, no address per datagram):
//   auto sock = UdpSocket::connect_to("127.0.0.1", 9000);
//   sock.send("hello", 5);
//   char reply[64]; int n = sock.recv(reply, sizeof(reply));
//
// Notes:
// - Maximum useful datagram size is limited by the OS UDP buffer and MTU.
// - For QUIC, the caller handles framing; UdpSocket just provides the
//   unreliable datagram transport beneath it.

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

namespace logos::reactor {

// ---------------------------------------------------------------------------
// UdpEndpoint — a (host, port) pair for addressing datagrams.
// ---------------------------------------------------------------------------
struct UdpEndpoint {
    sockaddr_in addr{};

    UdpEndpoint() { addr.sin_family = AF_INET; }

    // Returns nullptr on invalid host.
    static logos::expected<UdpEndpoint> make(const char* host, uint16_t port) noexcept {
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

    uint16_t    port() const noexcept { return ntohs(addr.sin_port); }
    std::string host() const { return inet_ntoa(addr.sin_addr); }

    sockaddr*       as_sockaddr()       noexcept { return reinterpret_cast<sockaddr*>(&addr); }
    const sockaddr* as_sockaddr() const noexcept { return reinterpret_cast<const sockaddr*>(&addr); }
    socklen_t       len()         const noexcept { return sizeof(addr); }
};

// ---------------------------------------------------------------------------
// UdpSocket
// ---------------------------------------------------------------------------
class UdpSocket {
public:
    UdpSocket() = default;
    ~UdpSocket() { close(); }

    UdpSocket(const UdpSocket&)            = delete;
    UdpSocket& operator=(const UdpSocket&) = delete;

    UdpSocket(UdpSocket&& o) noexcept : fd_(o.fd_) { o.fd_ = -1; }
    UdpSocket& operator=(UdpSocket&& o) noexcept {
        if (this != &o) { close(); fd_ = o.fd_; o.fd_ = -1; }
        return *this;
    }

    // -----------------------------------------------------------------------
    // Factories
    // -----------------------------------------------------------------------

    // Bind to host:port (server / listener).
    [[nodiscard]]
    static logos::expected<UdpSocket> bind_to(const char* host, uint16_t port) noexcept {
        auto ep_exp = UdpEndpoint::make(host, port);
        if (!ep_exp) return std::unexpected(std::move(ep_exp.error()));
        auto& ep = *ep_exp;

        auto fd_exp = make_socket();
        if (!fd_exp) return std::unexpected(std::move(fd_exp.error()));
        int fd = *fd_exp;

        int one = 1;
        setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
        setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &one, sizeof(one));

        if (::bind(fd, ep.as_sockaddr(), ep.len()) < 0) {
            ::close(fd);
            return std::unexpected(logos::err(ErrCode::bind_error));
        }
        return UdpSocket{fd};
    }

    // Connect to peer (client — sets default destination for send/recv).
    [[nodiscard]]
    static logos::expected<UdpSocket> connect_to(const char* host, uint16_t port) noexcept {
        auto ep_exp = UdpEndpoint::make(host, port);
        if (!ep_exp) return std::unexpected(std::move(ep_exp.error()));
        auto& ep = *ep_exp;

        auto fd_exp = make_socket();
        if (!fd_exp) return std::unexpected(std::move(fd_exp.error()));
        int fd = *fd_exp;

        if (::connect(fd, ep.as_sockaddr(), ep.len()) < 0) {
            ::close(fd);
            return std::unexpected(logos::err(ErrCode::connect_error));
        }
        return UdpSocket{fd};
    }

    // Create an unbound, unconnected socket (manual bind/connect later).
    [[nodiscard]]
    static logos::expected<UdpSocket> create() noexcept {
        auto fd_exp = make_socket();
        if (!fd_exp) return std::unexpected(std::move(fd_exp.error()));
        return UdpSocket{*fd_exp};
    }

    // -----------------------------------------------------------------------
    // Unconnected send/recv (explicit endpoint per datagram)
    // -----------------------------------------------------------------------

    // Send datagram to 'to'. Returns bytes sent, or -errno on error.
    int send_to(const void* buf, size_t size, const UdpEndpoint& to) {
        iovec  iov{ const_cast<void*>(buf), size };
        msghdr mh{};
        mh.msg_name    = const_cast<sockaddr*>(to.as_sockaddr());
        mh.msg_namelen = to.len();
        mh.msg_iov     = &iov;
        mh.msg_iovlen  = 1;
        return Reactor::current()->sendmsg(fd_, &mh, 0);
    }

    // Receive one datagram; fills 'from' with sender's address.
    // Returns bytes received, or -errno on error.
    int recv_from(void* buf, size_t size, UdpEndpoint& from) {
        iovec  iov{ buf, size };
        msghdr mh{};
        mh.msg_name    = from.as_sockaddr();
        mh.msg_namelen = from.len();
        mh.msg_iov     = &iov;
        mh.msg_iovlen  = 1;
        int n = Reactor::current()->recvmsg(fd_, &mh, 0);
        if (n >= 0) from.addr.sin_port = reinterpret_cast<sockaddr_in*>(mh.msg_name)->sin_port;
        return n;
    }

    // -----------------------------------------------------------------------
    // Connected send/recv (no address needed per datagram)
    // -----------------------------------------------------------------------

    int send(const void* buf, size_t size, int flags = 0) {
        return Reactor::current()->send(fd_, buf, size, flags);
    }

    int recv(void* buf, size_t size, int flags = 0) {
        return Reactor::current()->recv(fd_, buf, size, flags);
    }

    // -----------------------------------------------------------------------
    // Lifecycle
    // -----------------------------------------------------------------------

    void close() { if (fd_ >= 0) { ::close(fd_); fd_ = -1; } }
    bool valid() const noexcept { return fd_ >= 0; }
    int  fd()    const noexcept { return fd_; }

private:
    explicit UdpSocket(int fd) : fd_(fd) {}

    static logos::expected<int> make_socket() noexcept {
        int fd = ::socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
        if (fd < 0) return std::unexpected(logos::err(ErrCode::socket_error));
        return fd;
    }

    int fd_ = -1;
};

} // namespace logos::reactor
