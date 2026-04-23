// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Victor Smirnov
// Wrappers for liburing inline helpers so Logos can extern them.
// liburing exposes most prep/seen functions as static inline — they can't
// be linked directly.  These thin C wrappers give them real symbol entries.

#include <liburing.h>
#include <poll.h>      // POLLIN
#include <fcntl.h>     // O_* constants
#include <stddef.h>

// ── Existing fiber reactor helpers ────────────────────────────────────────

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

// ── IoRing (std.io_uring) — init / prep / submit / wait / free ────────────

// Size of struct io_uring so Logos can alloc the right amount of memory.
int logos_io_uring_sizeof(void)
{
    return (int)sizeof(struct io_uring);
}

// Initialize the ring.  Returns 0 on success, negative errno on failure.
int logos_io_uring_init(int entries, struct io_uring *ring)
{
    return io_uring_queue_init(entries, ring, 0);
}

// Tear down the ring (unmaps, closes fd).
void logos_io_uring_exit(struct io_uring *ring)
{
    io_uring_queue_exit(ring);
}

// Get a free SQE slot.  Returns NULL if the ring is full.
struct io_uring_sqe *logos_io_uring_get_sqe(struct io_uring *ring)
{
    return io_uring_get_sqe(ring);
}

// Prepare a read SQE.
void logos_io_uring_prep_read(struct io_uring_sqe *sqe,
                               int fd, void *buf, unsigned nbytes,
                               long long offset)
{
    io_uring_prep_read(sqe, fd, buf, nbytes, (__u64)offset);
}

// Prepare a write SQE.
void logos_io_uring_prep_write(struct io_uring_sqe *sqe,
                                int fd, const void *buf, unsigned nbytes,
                                long long offset)
{
    io_uring_prep_write(sqe, fd, buf, nbytes, (__u64)offset);
}

// Prepare an openat SQE.  Use dirfd=-100 (AT_FDCWD) for relative-to-cwd.
void logos_io_uring_prep_openat(struct io_uring_sqe *sqe,
                                 int dirfd, const char *path,
                                 int flags, unsigned mode)
{
    io_uring_prep_openat(sqe, dirfd, path, flags, mode);
}

// Prepare a close SQE.
void logos_io_uring_prep_close(struct io_uring_sqe *sqe, int fd)
{
    io_uring_prep_close(sqe, fd);
}

// Submit all pending SQEs and wait for at least wait_nr completions.
// Returns number of SQEs submitted, or negative errno on error.
int logos_io_uring_submit_and_wait(struct io_uring *ring, int wait_nr)
{
    return io_uring_submit_and_wait(ring, (unsigned)wait_nr);
}

// Peek at the oldest unread CQE without blocking.
// Returns 0 and sets *cqe on success; returns -EAGAIN if none ready.
int logos_io_uring_peek_cqe2(struct io_uring *ring, struct io_uring_cqe **cqe)
{
    return io_uring_peek_cqe(ring, cqe);
}

// Block until at least one CQE is available.
int logos_io_uring_wait_cqe2(struct io_uring *ring, struct io_uring_cqe **cqe)
{
    return io_uring_wait_cqe(ring, cqe);
}

// Read the result field from a CQE (negative = -errno, non-negative = success).
int logos_io_uring_cqe_result(struct io_uring_cqe *cqe)
{
    return cqe->res;
}

// Mark a CQE as consumed so the kernel can reuse that slot.
void logos_io_uring_cqe_advance(struct io_uring *ring, struct io_uring_cqe *cqe)
{
    io_uring_cqe_seen(ring, cqe);
}

// ── Socket ops for the fiber reactor (accept/recv/send/connect) ───────────

void logos_io_uring_prep_accept(struct io_uring_sqe *sqe, int fd,
                                 void *addr, void *addrlen, int flags)
{
    io_uring_prep_accept(sqe, fd, (struct sockaddr *)addr,
                         (socklen_t *)addrlen, flags);
}

void logos_io_uring_prep_recv(struct io_uring_sqe *sqe, int fd,
                               void *buf, unsigned nbytes, int flags)
{
    io_uring_prep_recv(sqe, fd, buf, nbytes, flags);
}

void logos_io_uring_prep_send(struct io_uring_sqe *sqe, int fd,
                               const void *buf, unsigned nbytes, int flags)
{
    io_uring_prep_send(sqe, fd, buf, nbytes, flags);
}

void logos_io_uring_prep_connect(struct io_uring_sqe *sqe, int fd,
                                  const void *addr, int addrlen)
{
    io_uring_prep_connect(sqe, fd, (const struct sockaddr *)addr,
                          (socklen_t)addrlen);
}

// Retrieve the 64-bit user_data that was attached to a CQE via
// logos_uring_sqe_set_data64.  The reactor stores the waiting fiber
// pointer there, so it can re-enqueue the fiber when the CQE arrives.
uint64_t logos_io_uring_cqe_user_data(struct io_uring_cqe *cqe)
{
    return io_uring_cqe_get_data64(cqe);
}

// Prepare a no-op SQE — generates a CQE with res=0.  Useful for unit
// tests of the reactor that don't depend on a real fd being ready.
void logos_io_uring_prep_nop(struct io_uring_sqe *sqe)
{
    io_uring_prep_nop(sqe);
}
