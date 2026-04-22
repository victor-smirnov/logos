// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Victor Smirnov
// Wrappers for liburing inline helpers so Logos can extern them.
// liburing exposes most prep/seen functions as static inline — they can't
// be linked directly.  These thin C wrappers give them real symbol entries.

#include <liburing.h>
#include <poll.h>      // POLLIN

void logos_uring_prep_poll_add(struct io_uring_sqe *sqe, int fd, int mask)
{
    io_uring_prep_poll_add(sqe, fd, mask);
}

void logos_uring_sqe_set_data64(struct io_uring_sqe *sqe, uint64_t data)
{
    io_uring_sqe_set_data64(sqe, data);
}

void logos_uring_cqe_seen(struct io_uring *ring, struct io_uring_cqe *cqe)
{
    io_uring_cqe_seen(ring, cqe);
}

int logos_uring_wait_cqe(struct io_uring *ring, struct io_uring_cqe **cqe_ptr)
{
    return io_uring_wait_cqe(ring, cqe_ptr);
}

int logos_uring_peek_cqe(struct io_uring *ring, struct io_uring_cqe **cqe_ptr)
{
    return io_uring_peek_cqe(ring, cqe_ptr);
}
