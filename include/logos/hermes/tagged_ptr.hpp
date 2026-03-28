#pragma once

#include <cstdint>
#include <cstring>
#include <type_traits>

namespace logos::hermes {

// TaggedPtr: an 8-byte polymorphic slot that holds either a relative pointer
// to an arena object, or a small value embedded inline (up to 7 bytes).
//
// Discriminant: bit 0 of the last byte (byte[7]).
//   0 = pointer mode (56-bit signed relative offset, bit-rotated)
//   1 = value mode   (7 bytes of data + 1-byte tag)
//
// In pointer mode, the offset is always even (arena alignment >= 2),
// so after bit rotation the discriminant bit is naturally 0.
//
// In value mode, byte[7] = (type_hash << 1) | 1, and bytes[0..6] hold the value
// (zero-padded for types smaller than 7 bytes).
class TaggedPtr {
public:
    TaggedPtr() : bits_(0) {}

    // --- Discriminant ---

    bool is_null() const { return bits_ == 0; }
    bool is_pointer() const { return !is_null() && (last_byte() & 1) == 0; }
    bool is_value() const { return (last_byte() & 1) == 1; }

    // --- Pointer mode ---

    // Create a TaggedPtr in pointer mode from a byte offset.
    // The offset must be even (arena alignment guarantee).
    static TaggedPtr from_offset(int64_t offset) {
        TaggedPtr p;
        auto u = static_cast<uint64_t>(offset);
        // Rotate: move low byte to high position, shift rest down.
        // This places the always-zero low bit of offset into bit 0 of byte[7].
        p.bits_ = (u >> 8) | (u << 56);
        return p;
    }

    // Recover the signed byte offset (pointer mode only).
    int64_t to_offset() const {
        // Reverse rotation: move high byte back to low position.
        uint64_t top_byte = bits_ >> 56;
        uint64_t raw = (bits_ << 8) | top_byte;
        return static_cast<int64_t>(raw);
    }

    // Dereference: returns pointer to target object relative to this TaggedPtr's address.
    template <typename T>
    T* as_ptr() const {
        auto base = reinterpret_cast<const uint8_t*>(this);
        return reinterpret_cast<T*>(const_cast<uint8_t*>(base + to_offset()));
    }

    // Set this TaggedPtr to point at target (pointer mode).
    void set_pointer(const void* target) {
        auto base = reinterpret_cast<const uint8_t*>(this);
        auto dest = reinterpret_cast<const uint8_t*>(target);
        int64_t offset = dest - base;
        *this = from_offset(offset);
    }

    // --- Value mode ---

    // Embed a small value with a type hash tag.
    // T must be a trivially copyable type with sizeof(T) <= 7.
    // type_hash must fit in 7 bits (< 128).
    template <typename T>
    static TaggedPtr from_value(T value, uint8_t type_hash) {
        static_assert(std::is_trivially_copyable_v<T>);
        static_assert(sizeof(T) <= 7);

        TaggedPtr p;
        p.bits_ = 0;
        std::memcpy(&p.bits_, &value, sizeof(T));
        // Set tag in the last byte: (type_hash << 1) | 1
        auto* bytes = reinterpret_cast<uint8_t*>(&p.bits_);
        bytes[7] = static_cast<uint8_t>((type_hash << 1) | 1);
        return p;
    }

    // Extract the embedded value (value mode only).
    template <typename T>
    T as_value() const {
        static_assert(std::is_trivially_copyable_v<T>);
        static_assert(sizeof(T) <= 7);

        T result{};
        std::memcpy(&result, &bits_, sizeof(T));
        return result;
    }

    // Extract the 7-bit type hash from the tag byte (value mode only).
    uint8_t value_type_hash() const {
        return last_byte() >> 1;
    }

    // --- Relocation ---

    // After moving a TaggedPtr to a different memory address (e.g. during array grow),
    // pointer-mode offsets must be adjusted. Call this on the NEW location, passing
    // the OLD location, to fix up the relative offset.
    void relocate_from(const TaggedPtr* old_location) {
        if (!is_pointer()) return;  // Values are self-contained, no fix needed.
        // Compute absolute target from old offset, then recompute relative to new location.
        auto old_base = reinterpret_cast<const uint8_t*>(old_location);
        auto new_base = reinterpret_cast<const uint8_t*>(this);
        int64_t old_offset = to_offset();
        int64_t new_offset = old_offset + (old_base - new_base);
        *this = from_offset(new_offset);
    }

    // --- Raw access ---

    uint64_t raw() const { return bits_; }
    static TaggedPtr from_raw(uint64_t bits) { TaggedPtr p; p.bits_ = bits; return p; }

private:
    uint64_t bits_;

    uint8_t last_byte() const {
        auto* bytes = reinterpret_cast<const uint8_t*>(&bits_);
        return bytes[7];
    }
};

static_assert(sizeof(TaggedPtr) == 8);
static_assert(alignof(TaggedPtr) == 8);

} // namespace logos::hermes
