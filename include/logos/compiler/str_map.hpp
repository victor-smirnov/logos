#pragma once

#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

namespace logos::compiler {

struct StringHash {
    using is_transparent = void;
    using hash_type      = std::hash<std::string_view>;
    size_t operator()(std::string_view sv) const noexcept { return hash_type{}(sv); }
    size_t operator()(const std::string& s) const noexcept { return hash_type{}(s); }
    size_t operator()(const char* s)        const noexcept { return hash_type{}(s); }
};

template <class V>
using StrMap = std::unordered_map<std::string, V, StringHash, std::equal_to<>>;

using StrSet = std::unordered_set<std::string, StringHash, std::equal_to<>>;

}  // namespace logos::compiler
