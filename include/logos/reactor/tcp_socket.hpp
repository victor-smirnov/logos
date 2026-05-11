//
// Fiber-aware TCP socket.
//
// All operations that would block (accept, connect, read, write) suspend the
// calling fiber via the reactor's io_uring event loop and resume it on
// completion.
//
// Usage — server:
//
//   auto server = TcpSocket::listen_on("0.0.0.0", 8080).get();
//   while (true) {
//       auto client = server.accept().get();
//       Reactor::current()->spawn([c = std::move(client)]() mutable {
//           char buf[256];
//           auto n = c.read(buf, sizeof(buf)).get();
//           if (n > 0) c.write_all(buf, n).get();  // echo
//       });
//   }
//
// Usage — client:
//
//   auto sock = TcpSocket::connect_to("127.0.0.1", 8080).get();
//   sock.write_all("hello", 5).get();
//   char reply[64];
//   int n = sock.read(reply, sizeof(reply)).get();

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

    [[nodiscard]]
    static logos::expected<TcpSocket> listen_on(const char* host, uint16_t port,
                                                 int backlog = 128) noexcept
    {
        int fd = ::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
        if (fd < 0) return std::unexpected(logos::err(ErrCode::socket_error));

        int one = 1;
        setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
        setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &one, sizeof(one));

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port   = htons(port);
        if (!host || host[0] == '\0' || std::string(host) == "0.0.0.0") {
            addr.sin_addr.s_addr = INADDR_ANY;
        } else {
            if (inet_aton(host, &addr.sin_addr) == 0) {
                ::close(fd);
                return std::unexpected(logos::err(ErrCode::invalid_host));
            }
        }

        if (::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
            ::close(fd);
            return std::unexpected(logos::err(ErrCode::bind_error));
        }
        if (::listen(fd, backlog) < 0) {
            ::close(fd);
            return std::unexpected(logos::err(ErrCode::listen_error));
        }
        return TcpSocket{fd};
    }

    // Accept next incoming connection (blocks fiber).
    [[nodiscard]]
    logos::expected<TcpSocket> accept() noexcept {
        Reactor* r = Reactor::current();
        LOGOS_ASSERT(r, "REACTOR-TCP-001", "TcpSocket::accept() called outside reactor");
        LOGOS_TRY(auto client_fd, r->accept(fd_));
        return TcpSocket{client_fd};
    }

    // -----------------------------------------------------------------------
    // Factory — client side
    // -----------------------------------------------------------------------

    [[nodiscard]]
    static logos::expected<TcpSocket> connect_to(const char* host, uint16_t port) noexcept {
        int fd = ::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
        if (fd < 0) return std::unexpected(logos::err(ErrCode::socket_error));

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port   = htons(port);
        if (inet_aton(host, &addr.sin_addr) == 0) {
            ::close(fd);
            return std::unexpected(logos::err(ErrCode::invalid_host));
        }

        Reactor* r = Reactor::current();
        LOGOS_ASSERT(r, "REACTOR-TCP-010", "TcpSocket::connect_to() called outside reactor");
        auto rc = r->connect(fd, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr));
        if (!rc) {
            ::close(fd);
            return std::unexpected(std::move(rc.error()));
        }
        return TcpSocket{fd};
    }

    // -----------------------------------------------------------------------
    // Data transfer (all block the fiber)
    // -----------------------------------------------------------------------

    // Read up to 'size' bytes. Returns bytes read (0 = peer closed).
    [[nodiscard]]
    logos::expected<int> read(void* buf, size_t size) noexcept {
        return Reactor::current()->recv(fd_, buf, size, 0);
    }

    // Write up to 'size' bytes.
    [[nodiscard]]
    logos::expected<int> write(const void* buf, size_t size) noexcept {
        return Reactor::current()->send(fd_, buf, size, 0);
    }

    // Write all 'size' bytes, looping until done or error.
    [[nodiscard]]
    logos::expected<void> write_all(const void* buf, size_t size) noexcept {
        const char* p  = static_cast<const char*>(buf);
        size_t remaining = size;
        while (remaining > 0) {
            LOGOS_TRY(auto n, write(p, remaining));
            if (n == 0) return std::unexpected(logos::err(ErrCode::io_error));
            p         += static_cast<size_t>(n);
            remaining -= static_cast<size_t>(n);
        }
        return {};
    }

    // -----------------------------------------------------------------------
    // Lifecycle
    // -----------------------------------------------------------------------

    void close() noexcept {
        if (fd_ >= 0) { ::close(fd_); fd_ = -1; }
    }

    bool valid() const noexcept { return fd_ >= 0; }
    int  fd()    const noexcept { return fd_; }

private:
    explicit TcpSocket(int fd) : fd_(fd) {}
    int fd_ = -1;
};

} // namespace logos::reactor
