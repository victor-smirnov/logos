// SPDX-License-Identifier: Apache-2.0
// Logos runtime — filesystem metadata helpers.
//
// Thin C wrappers over libc so Logos doesn't need to know struct stat
// layout. Returned ints follow the convention "negative = -errno".

#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

// Returns 1 if path exists, 0 if not (ENOENT), -errno otherwise.
int logos_path_exists(const char* path) {
    struct stat st;
    if (lstat(path, &st) == 0) return 1;
    if (errno == ENOENT || errno == ENOTDIR) return 0;
    return -errno;
}

// Returns 1 if path is a directory, 0 if exists but not a dir or doesn't
// exist, -errno on real errors.
int logos_path_is_dir(const char* path) {
    struct stat st;
    if (stat(path, &st) == 0) return S_ISDIR(st.st_mode) ? 1 : 0;
    if (errno == ENOENT || errno == ENOTDIR) return 0;
    return -errno;
}

// Returns 1 if path is a regular file, 0 if exists but not a file or doesn't
// exist, -errno on real errors.
int logos_path_is_file(const char* path) {
    struct stat st;
    if (stat(path, &st) == 0) return S_ISREG(st.st_mode) ? 1 : 0;
    if (errno == ENOENT || errno == ENOTDIR) return 0;
    return -errno;
}

// Returns size in bytes, or -errno on error.
int64_t logos_path_size(const char* path) {
    struct stat st;
    if (stat(path, &st) != 0) return -errno;
    return (int64_t)st.st_size;
}

// Returns modification time in nanoseconds since epoch, or -errno on error.
int64_t logos_path_mtime_ns(const char* path) {
    struct stat st;
    if (stat(path, &st) != 0) return -errno;
    return (int64_t)st.st_mtim.tv_sec * 1000000000LL + (int64_t)st.st_mtim.tv_nsec;
}

// Returns 0 on success, -errno otherwise. EEXIST is also reported as -EEXIST
// (caller decides whether that's an error).
int logos_mkdir(const char* path, int mode) {
    if (mkdir(path, (mode_t)mode) == 0) return 0;
    return -errno;
}

int logos_rmdir(const char* path) {
    if (rmdir(path) == 0) return 0;
    return -errno;
}

int logos_unlink(const char* path) {
    if (unlink(path) == 0) return 0;
    return -errno;
}

// Resolves path through realpath(). Writes up to out_cap bytes (NOT including
// terminator) into out. Returns length written, or -errno on failure. If the
// resolved path doesn't fit, returns -ERANGE (caller can retry with bigger buf).
int64_t logos_realpath(const char* path, char* out, int64_t out_cap) {
    char tmp[PATH_MAX];
    if (realpath(path, tmp) == NULL) return -errno;
    size_t n = strlen(tmp);
    if ((int64_t)n > out_cap) return -ERANGE;
    memcpy(out, tmp, n);
    return (int64_t)n;
}

// opendir/readdir/closedir wrappers.
//
// logos_opendir: returns opaque handle (DIR*) cast to void*, or NULL on error.
// On NULL, caller can read errno via logos_last_errno() — but to keep things
// simple we emit a separate path: caller checks for NULL and treats it as
// generic "open failed".
void* logos_opendir(const char* path) {
    return (void*)opendir(path);
}

// Reads the next entry. Skips "." and "..".
//   Returns 1 on success: name written to out (NUL-terminated within out_cap),
//                          *out_is_dir set to 1 if entry is a directory.
//   Returns 0 at end of stream.
//   Returns -errno on error.
//   Returns -ERANGE if name doesn't fit in out_cap.
int logos_readdir_name(void* d, char* out, int64_t out_cap, int* out_is_dir) {
    if (d == NULL) return -EINVAL;
    DIR* dir = (DIR*)d;
    struct dirent* e;
    for (;;) {
        errno = 0;
        e = readdir(dir);
        if (e == NULL) {
            if (errno != 0) return -errno;
            return 0;
        }
        // Skip "." and ".."
        const char* nm = e->d_name;
        if (nm[0] == '.' && (nm[1] == '\0' || (nm[1] == '.' && nm[2] == '\0'))) continue;
        size_t n = strlen(nm);
        if ((int64_t)n + 1 > out_cap) return -ERANGE;
        memcpy(out, nm, n);
        out[n] = '\0';
        if (out_is_dir) {
#ifdef DT_DIR
            if (e->d_type == DT_DIR) {
                *out_is_dir = 1;
            } else if (e->d_type == DT_UNKNOWN) {
                // File system doesn't fill d_type; we don't stat here to
                // keep cost low. Caller can call logos_path_is_dir.
                *out_is_dir = 0;
            } else {
                *out_is_dir = 0;
            }
#else
            *out_is_dir = 0;
#endif
        }
        return 1;
    }
}

void logos_closedir(void* d) {
    if (d != NULL) closedir((DIR*)d);
}
