#ifndef HLPLAYER_CONSTANT_TIME_H
#define HLPLAYER_CONSTANT_TIME_H

#include <cstddef>
#include <cstdint>

namespace hlplayer::crypto {

// Constant-time comparison of two byte sequences.
// Returns true if and only if all bytes are equal.
// Does NOT short-circuit — timing is independent of content.
inline bool constant_time_compare(const uint8_t* a, const uint8_t* b, size_t len) {
    uint8_t result = 0;
    for (size_t i = 0; i < len; ++i) {
        result |= a[i] ^ b[i];
    }
    return result == 0;
}

} // namespace hlplayer::crypto

#endif // HLPLAYER_CONSTANT_TIME_H
