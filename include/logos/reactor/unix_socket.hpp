//
// Fiber-aware Unix Domain Socket (AF_UNIX, SOCK_STREAM).
//
// Same API as TcpSocket but uses filesystem paths instead of IP addresses.
// Useful for high-performance local IPC (reactor ↔ reactor, HRPC intra-node).
//
// Usage — server:
//
//   auto server = UnixSocket::listen_on("/tmp/my.sock").get();
//   auto client = server.accept().get();
//   char buf[64]; int n = client.read(buf, sizeof(buf)).get();
//
// Usage — client:
//
//   auto sock = UnixSocket::connect_to("/tmp/my.sock").get();
//   sock.write_all("ping", 4).get();

#pragma once

#include <logos/reactor/reactor.hpp>
#include <logos/verification/assert.hpp>

#include <fcntl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <cstring>
#include <logos/core/expected.hpp>

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

    [[nodiscard]]
    static logos::expected<UnixSocket> listen_on(const char* path,
                                                  int backlog = 128) noexcept {
        int fd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
        if (fd < 0) return std::unexpected(logos::err(ErrCode::socket_error));

        sockaddr_un addr{};
        addr.sun_family = AF_UNIX;
        std::strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);
        ::unlink(path);

        if (::bind(fd, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) < 0) {
            ::close(fd);
            return std::unexpected(logos::err(ErrCode::bind_error));
        }
        if (::listen(fd, backlog) < 0) {
            ::close(fd);
            return std::unexpected(logos::err(ErrCode::listen_error));
        }
        return UnixSocket{fd};
    }

    [[nodiscard]]
    logos::expected<UnixSocket> accept() noexcept {
        Reactor* r = Reactor::current();
        LOGOS_ASSERT(r, "REACTOR-UDS-001", "UnixSocket::accept() outside reactor");
        LOGOS_TRY(auto client_fd, r->accept(fd_));
        return UnixSocket{client_fd};
    }

    // -----------------------------------------------------------------------
    // Factory — client
    // -----------------------------------------------------------------------

    [[nodiscard]]
    static logos::expected<UnixSocket> connect_to(const char* path) noexcept {
        int fd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
        if (fd < 0) return std::unexpected(logos::err(ErrCode::socket_error));

        sockaddr_un addr{};
        addr.sun_family = AF_UNIX;
        std::strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);

        Reactor* r = Reactor::current();
        LOGOS_ASSERT(r, "REACTOR-UDS-010", "UnixSocket::connect_to() outside reactor");
        auto rc = r->connect(fd, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr));
        if (!rc) { ::close(fd); return std::unexpected(std::move(rc.error())); }
        return UnixSocket{fd};
    }

    // -----------------------------------------------------------------------
    // IO
    // -----------------------------------------------------------------------

    [[nodiscard]]
    logos::expected<int> read(void* buf, size_t size) noexcept {
        return Reactor::current()->recv(fd_, buf, size, 0);
    }

    [[nodiscard]]
    logos::expected<int> write(const void* buf, size_t size) noexcept {
        return Reactor::current()->send(fd_, buf, size, 0);
    }

    [[nodiscard]]
    logos::expected<void> write_all(const void* buf, size_t size) noexcept {
        const char* p = static_cast<const char*>(buf);
        size_t rem = size;
        while (rem > 0) {
            LOGOS_TRY(auto n, write(p, rem));
            if (n == 0) return std::unexpected(logos::err(ErrCode::io_error));
            p += static_cast<size_t>(n);
            rem -= static_cast<size_t>(n);
        }
        return {};
    }

    // -----------------------------------------------------------------------
    // Lifecycle
    // -----------------------------------------------------------------------

    void close() noexcept { if (fd_ >= 0) { ::close(fd_); fd_ = -1; } }
    bool valid() const noexcept { return fd_ >= 0; }
    int  fd()    const noexcept { return fd_; }

private:
    explicit UnixSocket(int fd) : fd_(fd) {}
    int fd_ = -1;
};

} // namespace logos::reactor
