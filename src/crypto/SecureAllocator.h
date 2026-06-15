#ifndef HLPLAYER_SECURE_ALLOCATOR_H
#define HLPLAYER_SECURE_ALLOCATOR_H

#include <cstddef>
#include <cstdint>
#include <vector>

#ifdef _WIN32
#  include <windows.h>
#else
#  include <cstring>
#endif

namespace hlplayer::crypto {

/// Allocator that securely zeroes memory on deallocation.
/// Prevents sensitive key material from lingering in heap.
template<typename T>
struct SecureAllocator {
    using value_type = T;

    SecureAllocator() noexcept = default;

    template<typename U>
    constexpr SecureAllocator(const SecureAllocator<U>&) noexcept {}

    T* allocate(size_t n) {
        return static_cast<T*>(::operator new(n * sizeof(T)));
    }

    void deallocate(T* p, size_t n) noexcept {
        if (p) {
#ifdef _WIN32
            ::SecureZeroMemory(p, n * sizeof(T));
#else
            // Use volatile to prevent compiler from optimizing away the zero loop
            volatile uint8_t* vp = reinterpret_cast<volatile uint8_t*>(p);
            for (size_t i = 0; i < n * sizeof(T); ++i) {
                vp[i] = 0;
            }
#endif
            ::operator delete(p);
        }
    }

    template<typename U>
    bool operator==(const SecureAllocator<U>&) const noexcept { return true; }

    template<typename U>
    bool operator!=(const SecureAllocator<U>&) const noexcept { return false; }

    template<typename U>
    struct rebind {
        using other = SecureAllocator<U>;
    };
};

/// std::vector<uint8_t> that securely erases memory on destruction.
using SecureBytes = std::vector<uint8_t, SecureAllocator<uint8_t>>;

} // namespace hlplayer::crypto

#endif // HLPLAYER_SECURE_ALLOCATOR_H
