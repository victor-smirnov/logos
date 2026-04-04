// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Victor Smirnov
// Logos project — https://github.com/victor-smirnov/logos
//
// Fiber-aware file IO.
//
// Opens files synchronously (::open is instant) but performs reads and writes
// via the reactor's io_uring, suspending the calling fiber until completion.
//
// Usage:
//
//   auto f = File::open("data.bin", O_RDONLY);
//   char buf[4096];
//   int n = f.read(buf, sizeof(buf));      // async, blocks fiber
//   f.close();
//
//   auto out = File::open("out.bin", O_WRONLY | O_CREAT | O_TRUNC, 0644);
//   out.write(buf, n);                     // async, blocks fiber

#pragma once

#include <logos/reactor/reactor.hpp>
#include <logos/verification/assert.hpp>

#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#include <logos/core/expected.hpp>

namespace logos::reactor {

class File {
public:
    File() = default;

    ~File() { close(); }

    File(const File&)            = delete;
    File& operator=(const File&) = delete;

    File(File&& o) noexcept : fd_(o.fd_), offset_(o.offset_) { o.fd_ = -1; }
    File& operator=(File&& o) noexcept {
        if (this != &o) { close(); fd_ = o.fd_; offset_ = o.offset_; o.fd_ = -1; }
        return *this;
    }

    // -----------------------------------------------------------------------
    // Factories
    // -----------------------------------------------------------------------

    // Open with explicit flags and optional mode (for O_CREAT).
    [[nodiscard]]
    static logos::expected<File> open(const char* path, int flags,
                                      mode_t mode = 0644) noexcept {
        int fd = ::open(path, flags, mode);
        if (fd < 0) return std::unexpected(logos::err(ErrCode::open_error));
        return File{fd};
    }

    [[nodiscard]]
    static logos::expected<File> open(const std::string& path, int flags,
                                      mode_t mode = 0644) noexcept {
        return open(path.c_str(), flags, mode);
    }

    // -----------------------------------------------------------------------
    // IO (suspend fiber via io_uring, sequential offset tracking)
    // -----------------------------------------------------------------------

    // Read up to 'size' bytes at the current sequential offset.
    // Returns bytes read (0 = EOF, <0 = -errno).
    int read(void* buf, size_t size) {
        Reactor* r = Reactor::current();
        LOGOS_ASSERT(r,  "REACTOR-FILE-001", "File::read() called outside reactor");
        LOGOS_ASSERT(fd_ >= 0, "REACTOR-FILE-002", "File::read() on closed file");
        int n = r->read(fd_, buf, size, offset_);
        if (n > 0) offset_ += n;
        return n;
    }

    // Write 'size' bytes at the current sequential offset.
    // Returns bytes written (<0 = -errno).
    int write(const void* buf, size_t size) {
        Reactor* r = Reactor::current();
        LOGOS_ASSERT(r,  "REACTOR-FILE-010", "File::write() called outside reactor");
        LOGOS_ASSERT(fd_ >= 0, "REACTOR-FILE-011", "File::write() on closed file");
        int n = r->write(fd_, buf, size, offset_);
        if (n > 0) offset_ += n;
        return n;
    }

    // Write all bytes, looping until done or error.
    int write_all(const void* buf, size_t size) {
        const char* p         = static_cast<const char*>(buf);
        size_t      remaining = size;
        while (remaining > 0) {
            int n = write(p, remaining);
            if (n <= 0) return n;
            p         += n;
            remaining -= static_cast<size_t>(n);
        }
        return static_cast<int>(size);
    }

    // Seek (does not involve io_uring).
    void seek(off_t pos) noexcept { offset_ = pos; }
    off_t tell() const noexcept { return offset_; }

    // -----------------------------------------------------------------------
    // Lifecycle
    // -----------------------------------------------------------------------

    void close() {
        if (fd_ >= 0) { ::close(fd_); fd_ = -1; offset_ = 0; }
    }

    bool valid() const noexcept { return fd_ >= 0; }
    int  fd()    const noexcept { return fd_; }

private:
    explicit File(int fd) : fd_(fd) {}
    int   fd_     = -1;
    off_t offset_ = 0;
};

} // namespace logos::reactor
