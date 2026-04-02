// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Victor Smirnov
// Logos project — https://github.com/victor-smirnov/logos
//
// Fiber-aware Unix Domain Socket (AF_UNIX, SOCK_STREAM).
//
// Same API as TcpSocket but uses filesystem paths instead of IP addresses.
// Useful for high-performance local IPC (reactor ↔ reactor, HRPC intra-node).
//
// Usage — server:
//
//   auto server = UnixSocket::listen_on("/tmp/my.sock");
//   auto client = server.accept();
//   char buf[64]; int n = client.read(buf, sizeof(buf));
//
// Usage — client:
//
//   auto sock = UnixSocket::connect_to("/tmp/my.sock");
//   sock.write("ping", 4);

#pragma once

#include <logos/reactor/reactor.hpp>
#include <logos/verification/assert.hpp>

#include <fcntl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <cstring>
#include <stdexcept>
#include <string>

namespace logos::reactor {

class UnixSocket {
public:
    UnixSocket() = default;
    ~UnixSocket() { close(); }

    UnixSocket(const UnixSocket&)            = delete;
    UnixSocket& operator=(const UnixSocket&) = delete;

    UnixSocket(UnixSocket&& o) noexcept : fd_(o.fd_) { o.fd_ = -1; }
    UnixSocket& operator=(UnixSocket&& o) noexcept {
        if (this != &o) { close(); fd_ = o.fd_; o.fd_ = -1; }
        return *this;
    }

    // -----------------------------------------------------------------------
    // Factory — server
    // -----------------------------------------------------------------------

    static UnixSocket listen_on(const char* path, int backlog = 128) {
        int fd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
        if (fd < 0) throw std::runtime_error(std::string("socket: ") + strerror(errno));

        sockaddr_un addr{};
        addr.sun_family = AF_UNIX;
        std::strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);

        ::unlink(path);  // remove stale socket file if present

        if (::bind(fd, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) < 0) {
            ::close(fd);
            throw std::runtime_error(std::string("bind: ") + strerror(errno));
        }
        if (::listen(fd, backlog) < 0) {
            ::close(fd);
            throw std::runtime_error(std::string("listen: ") + strerror(errno));
        }
        return UnixSocket{fd};
    }

    UnixSocket accept() {
        Reactor* r = Reactor::current();
        LOGOS_ASSERT(r, "REACTOR-UDS-001", "UnixSocket::accept() outside reactor");
        int client_fd = r->accept(fd_);
        if (client_fd < 0)
            throw std::runtime_error(std::string("accept: ") + strerror(-client_fd));
        return UnixSocket{client_fd};
    }

    // -----------------------------------------------------------------------
    // Factory — client
    // -----------------------------------------------------------------------

    static UnixSocket connect_to(const char* path) {
        int fd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
        if (fd < 0) throw std::runtime_error(std::string("socket: ") + strerror(errno));

        sockaddr_un addr{};
        addr.sun_family = AF_UNIX;
        std::strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);

        Reactor* r = Reactor::current();
        LOGOS_ASSERT(r, "REACTOR-UDS-010", "UnixSocket::connect_to() outside reactor");
        int rc = r->connect(fd, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr));
        if (rc < 0) { ::close(fd); throw std::runtime_error(std::string("connect: ") + strerror(-rc)); }
        return UnixSocket{fd};
    }

    // -----------------------------------------------------------------------
    // IO
    // -----------------------------------------------------------------------

    int read(void* buf, size_t size) {
        return Reactor::current()->recv(fd_, buf, size, 0);
    }

    int write(const void* buf, size_t size) {
        return Reactor::current()->send(fd_, buf, size, 0);
    }

    int write_all(const void* buf, size_t size) {
        const char* p = static_cast<const char*>(buf);
        size_t rem = size;
        while (rem > 0) {
            int n = write(p, rem);
            if (n <= 0) return n;
            p += n; rem -= static_cast<size_t>(n);
        }
        return static_cast<int>(size);
    }

    // -----------------------------------------------------------------------
    // Lifecycle
    // -----------------------------------------------------------------------

    void close() { if (fd_ >= 0) { ::close(fd_); fd_ = -1; } }
    bool valid() const noexcept { return fd_ >= 0; }
    int  fd()    const noexcept { return fd_; }

private:
    explicit UnixSocket(int fd) : fd_(fd) {}
    int fd_ = -1;
};

} // namespace logos::reactor
