// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Victor Smirnov
// Logos project — https://github.com/victor-smirnov/logos
//
// Fiber-aware TCP socket.
//
// All operations that would block (accept, connect, read, write) suspend the
// calling fiber via the reactor's io_uring event loop and resume it on
// completion.
//
// Usage — server:
//
//   auto server = TcpSocket::listen_on("0.0.0.0", 8080);
//   while (true) {
//       auto client = server.accept();
//       Reactor::current()->spawn([c = std::move(client)]() mutable {
//           char buf[256];
//           int n = c.read(buf, sizeof(buf));
//           if (n > 0) c.write(buf, n);  // echo
//       });
//   }
//
// Usage — client:
//
//   auto sock = TcpSocket::connect_to("127.0.0.1", 8080);
//   sock.write("hello", 5);
//   char reply[64];
//   int n = sock.read(reply, sizeof(reply));

#pragma once

#include <logos/reactor/reactor.hpp>
#include <logos/verification/assert.hpp>

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstring>
#include <stdexcept>
#include <string>

namespace logos::reactor {

class TcpSocket {
public:
    TcpSocket() = default;

    ~TcpSocket() { close(); }

    TcpSocket(const TcpSocket&)            = delete;
    TcpSocket& operator=(const TcpSocket&) = delete;

    TcpSocket(TcpSocket&& o) noexcept : fd_(o.fd_) { o.fd_ = -1; }
    TcpSocket& operator=(TcpSocket&& o) noexcept {
        if (this != &o) { close(); fd_ = o.fd_; o.fd_ = -1; }
        return *this;
    }

    // -----------------------------------------------------------------------
    // Factory — server side
    // -----------------------------------------------------------------------

    // Create a listening TCP socket bound to host:port.
    static TcpSocket listen_on(const char* host, uint16_t port,
                               int backlog = 128)
    {
        int fd = ::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
        if (fd < 0) throw std::runtime_error(std::string("socket: ") + strerror(errno));

        int one = 1;
        setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
        setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &one, sizeof(one));

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port   = htons(port);
        if (!host || host[0] == '\0' || std::string(host) == "0.0.0.0") {
            addr.sin_addr.s_addr = INADDR_ANY;
        } else {
            if (inet_aton(host, &addr.sin_addr) == 0)
                throw std::runtime_error(std::string("invalid host: ") + host);
        }

        if (::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0)
            throw std::runtime_error(std::string("bind: ") + strerror(errno));

        if (::listen(fd, backlog) < 0)
            throw std::runtime_error(std::string("listen: ") + strerror(errno));

        return TcpSocket{fd};
    }

    // Accept next incoming connection (blocks fiber).
    TcpSocket accept() {
        Reactor* r = Reactor::current();
        LOGOS_ASSERT(r, "REACTOR-TCP-001", "TcpSocket::accept() called outside reactor");
        int client_fd = r->accept(fd_);
        if (client_fd < 0)
            throw std::runtime_error(std::string("accept: ") + strerror(-client_fd));
        return TcpSocket{client_fd};
    }

    // -----------------------------------------------------------------------
    // Factory — client side
    // -----------------------------------------------------------------------

    // Connect to host:port (blocks fiber until connected or error).
    static TcpSocket connect_to(const char* host, uint16_t port) {
        int fd = ::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
        if (fd < 0) throw std::runtime_error(std::string("socket: ") + strerror(errno));

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port   = htons(port);
        if (inet_aton(host, &addr.sin_addr) == 0)
            throw std::runtime_error(std::string("invalid host: ") + host);

        Reactor* r = Reactor::current();
        LOGOS_ASSERT(r, "REACTOR-TCP-010", "TcpSocket::connect_to() called outside reactor");
        int rc = r->connect(fd, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr));
        if (rc < 0) {
            ::close(fd);
            throw std::runtime_error(std::string("connect: ") + strerror(-rc));
        }
        return TcpSocket{fd};
    }

    // -----------------------------------------------------------------------
    // Data transfer (all block the fiber)
    // -----------------------------------------------------------------------

    // Read up to 'size' bytes. Returns bytes read (0 = peer closed, <0 = error).
    int read(void* buf, size_t size) {
        return Reactor::current()->recv(fd_, buf, size, 0);
    }

    // Write up to 'size' bytes. Returns bytes written (<0 = error).
    int write(const void* buf, size_t size) {
        return Reactor::current()->send(fd_, buf, size, 0);
    }

    // Write all 'size' bytes, looping until done or error.
    int write_all(const void* buf, size_t size) {
        const char* p  = static_cast<const char*>(buf);
        size_t      remaining = size;
        while (remaining > 0) {
            int n = write(p, remaining);
            if (n <= 0) return n;
            p         += n;
            remaining -= static_cast<size_t>(n);
        }
        return static_cast<int>(size);
    }

    // -----------------------------------------------------------------------
    // Lifecycle
    // -----------------------------------------------------------------------

    void close() {
        if (fd_ >= 0) { ::close(fd_); fd_ = -1; }
    }

    bool valid() const noexcept { return fd_ >= 0; }
    int  fd()    const noexcept { return fd_; }

private:
    explicit TcpSocket(int fd) : fd_(fd) {}
    int fd_ = -1;
};

} // namespace logos::reactor
