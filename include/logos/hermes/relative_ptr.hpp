#pragma once

#include <cstdint>
#include <cstddef>
#include <type_traits>

namespace logos::hermes {

// Relative pointer: stores a signed byte offset from its own address to the target.
// Null is represented by offset == 0 (pointing at itself is not a valid use case).
// All Hermes arena pointers are relative, making memory segments fully relocatable.
template <typename T>
class RelativePtr {
public:
    RelativePtr() : offset_(0) {}

    bool is_null() const { return offset_ == 0; }

    T* get() const {
        if (offset_ == 0) return nullptr;
        auto base = reinterpret_cast<const uint8_t*>(this);
        return reinterpret_cast<T*>(const_cast<uint8_t*>(base + offset_));
    }

    // operator* and operator-> are only available for non-void types.
    // We use a separate enable_if approach that GCC handles correctly.
    template <typename U = T, std::enable_if_t<!std::is_void_v<U>, int> = 0>
    U& operator*() const { return *get(); }

    template <typename U = T, std::enable_if_t<!std::is_void_v<U>, int> = 0>
    U* operator->() const { return get(); }

    explicit operator bool() const { return !is_null(); }

    // Set this pointer to point at target.
    void set(T* target) {
        auto base = reinterpret_cast<const uint8_t*>(this);
        auto dest = reinterpret_cast<const uint8_t*>(target);
        offset_ = dest - base;
    }

    void clear() { offset_ = 0; }

    int64_t raw_offset() const { return offset_; }

private:
    int64_t offset_;
};

static_assert(sizeof(RelativePtr<int>) == 8);
static_assert(alignof(RelativePtr<int>) == 8);
static_assert(sizeof(RelativePtr<void>) == 8);

} // namespace logos::hermes
