#pragma once

// Source positions (ADR 0030 H0). Every AST node carries SRC_SPAN: the
// file-relative byte offset of its first token << 23 | its length (23 bits,
// saturating). The FILE is the document's — sema knows it (file_), exactly as
// it knows SRC_LINE's file — so a span needs no global offset space and an AST
// decoded from a module archive needs no rebasing. Line and column are derived
// from the offset through the file's line table, registered here by whoever
// read the text (the module loader). A file without a table (an archive's
// module, whose text is not at hand) yields no column; its SRC_LINE still holds.

#include <algorithm>
#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace logos::compiler {

struct SrcSpan {
    uint32_t start = 0;   // file-relative byte offset of the first token
    uint32_t len   = 0;   // bytes to the end of the last token (saturates at 2^23-1)
    bool valid() const noexcept { return start != 0 || len != 0; }
};

inline SrcSpan unpack_src_span(int64_t v) noexcept {
    if (v <= 0) return {};
    auto u = static_cast<uint64_t>(v);
    return {static_cast<uint32_t>(u >> 23), static_cast<uint32_t>(u & 0x7FFFFFu)};
}

class SourceMap {
public:
    static SourceMap& global() {
        static SourceMap m;
        return m;
    }

    // Record the line table of `file` from its text. Idempotent per file.
    void add(const std::string& file, std::string_view text) {
        std::vector<uint32_t> starts{0};
        for (size_t i = 0; i < text.size(); ++i)
            if (text[i] == '\n') starts.push_back(static_cast<uint32_t>(i + 1));
        std::lock_guard<std::mutex> g(mu_);
        lines_[file] = std::move(starts);
    }

    // 1-based line and column of byte `offset` in `file`; false when the file
    // has no table.
    bool line_col(const std::string& file, uint32_t offset,
                  uint32_t& line, uint32_t& col) const {
        std::lock_guard<std::mutex> g(mu_);
        auto it = lines_.find(file);
        if (it == lines_.end() || it->second.empty()) return false;
        const auto& s = it->second;
        auto up = std::upper_bound(s.begin(), s.end(), offset);
        size_t idx = static_cast<size_t>(up - s.begin()) - 1;
        line = static_cast<uint32_t>(idx + 1);
        col  = offset - s[idx] + 1;
        return true;
    }

private:
    mutable std::mutex mu_;
    std::unordered_map<std::string, std::vector<uint32_t>> lines_;
};

} // namespace logos::compiler
