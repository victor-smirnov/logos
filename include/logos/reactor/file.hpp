// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Victor Smirnov
//
// Fiber-aware file IO.
//
// Opens files synchronously (::open is instant) but performs reads and writes
// via the reactor's io_uring, suspending the calling fiber until completion.
//
// Usage:
//
//   auto f = File::open("data.bin", O_RDONLY).get();
//   char buf[4096];
//   int n = f.read(buf, sizeof(buf)).get();      // async, blocks fiber
//   f.close();
//
//   auto out = File::open("out.bin", O_WRONLY | O_CREAT | O_TRUNC, 0644).get();
//   out.write_all(buf, n).get();                 // async, blocks fiber

#pragma once

#include <logos/reactor/reactor.hpp>
#include <logos/verification/assert.hpp>

#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#include <logos/core/expected.hpp>

LOGOS_NS_BEGIN

class File {
public:
    LOGOS_RED File() = default;

    LOGOS_RED ~File() { close(); }

    File(const File&)            = delete;
    File& operator=(const File&) = delete;

    LOGOS_RED File(File&& o) noexcept : fd_(o.fd_), offset_(o.offset_) { o.fd_ = -1; }
    LOGOS_RED File& operator=(File&& o) noexcept {
        if (this != &o) { close(); fd_ = o.fd_; offset_ = o.offset_; o.fd_ = -1; }
        return *this;
    }

    // -----------------------------------------------------------------------
    // Factories
    // -----------------------------------------------------------------------

    [[nodiscard]] LOGOS_RED
    static logos::expected<File> open(const char* path, int flags,
                                      mode_t mode = 0644) noexcept {
        int fd = ::open(path, flags, mode);
        if (fd < 0) return std::unexpected(logos::err(ErrCode::open_error));
        return File{fd};
    }

    [[nodiscard]] LOGOS_RED
    static logos::expected<File> open(const std::string& path, int flags,
                                      mode_t mode = 0644) noexcept {
        return open(path.c_str(), flags, mode);
    }

    // -----------------------------------------------------------------------
    // IO (suspend fiber via io_uring, sequential offset tracking)
    // -----------------------------------------------------------------------

    // Read up to 'size' bytes at the current sequential offset.
    // Returns bytes read (0 = EOF).
    [[nodiscard]]
    logos::expected<int> read(void* buf, size_t size) noexcept {
        Reactor* r = Reactor::current();
        auto rd = r->read(fd_, buf, size, offset_);
        if (!rd) return std::unexpected(std::move(rd.error()));
        if (*rd > 0) offset_ += *rd;
        return *rd;
    }

    // Write 'size' bytes at the current sequential offset.
    [[nodiscard]]
    logos::expected<int> write(const void* buf, size_t size) noexcept {
        Reactor* r = Reactor::current();
        auto wr = r->write(fd_, buf, size, offset_);
        if (!wr) return std::unexpected(std::move(wr.error()));
        if (*wr > 0) offset_ += *wr;
        return *wr;
    }

    // Write all bytes, looping until done or error.
    [[nodiscard]]
    logos::expected<void> write_all(const void* buf, size_t size) noexcept {
        const char* p         = static_cast<const char*>(buf);
        size_t      remaining = size;
        while (remaining > 0) {
            LOGOS_TRY(auto n, write(p, remaining));
            if (n == 0) return std::unexpected(logos::err(ErrCode::io_error));
            p         += static_cast<size_t>(n);
            remaining -= static_cast<size_t>(n);
        }
        return {};
    }

    // Seek (does not involve io_uring).
    LOGOS_RED void  seek(off_t pos) noexcept { offset_ = pos; }
    LOGOS_RED off_t tell() const noexcept { return offset_; }

    // -----------------------------------------------------------------------
    // Lifecycle
    // -----------------------------------------------------------------------

    LOGOS_RED void close() noexcept {
        if (fd_ >= 0) { ::close(fd_); fd_ = -1; offset_ = 0; }
    }

    LOGOS_RED bool valid() const noexcept { return fd_ >= 0; }
    LOGOS_RED int  fd()    const noexcept { return fd_; }

private:
    LOGOS_RED explicit File(int fd) : fd_(fd) {}
    int   fd_     = -1;
    off_t offset_ = 0;
};

LOGOS_NS_END
