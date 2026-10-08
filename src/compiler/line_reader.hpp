#pragma once

#include <cstdio>
#include <cstdlib>
#include <string_view>
#include <sys/types.h>

namespace logos::compiler {

// Calls `on_line` for every line of `f`, without its trailing "\n"/"\r".
// The line length has no bound: a mangled symbol name runs to hundreds of
// characters, and a fixed buffer splits it into fragments that match nothing.
template <class F>
void for_each_line(FILE* f, F&& on_line) {
    char* buf = nullptr;
    size_t cap = 0;
    ssize_t n;
    while ((n = ::getline(&buf, &cap, f)) >= 0) {
        std::string_view sv(buf, static_cast<size_t>(n));
        while (!sv.empty() && (sv.back() == '\n' || sv.back() == '\r')) sv.remove_suffix(1);
        if (!on_line(sv)) break;
    }
    std::free(buf);
}

}  // namespace logos::compiler
