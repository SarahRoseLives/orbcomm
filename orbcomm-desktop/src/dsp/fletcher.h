#ifndef ORBCOMM_DSP_FLETCHER_H
#define ORBCOMM_DSP_FLETCHER_H

#include "types.h"
#include <cstdint>

namespace orbcomm {

// ─── Fletcher-8 Checksum ───────────────────────────────────────────────────
//
// Verifies or computes the 2-byte Fletcher checksum over a 24-byte packet.
// The last 2 bytes are the checksum, computed so that after processing all
// 24 bytes, both accumulators (sum1 and sum2) reach zero.
//
// C1 = (-sum2_body - sum1_body) & 0xFF
// C2 = sum2_body & 0xFF

class Fletcher {
public:
    // Compute the Fletcher checksum over `length` bytes.
    // Returns (sum1, sum2) as a pair.
    static std::pair<uint8_t, uint8_t> compute(const uint8_t* data, size_t length) {
        uint8_t s1 = 0, s2 = 0;
        for (size_t i = 0; i < length; ++i) {
            s1 = static_cast<uint8_t>(s1 + data[i]);
            s2 = static_cast<uint8_t>(s2 + s1);
        }
        return {s1, s2};
    }

    // Verify that a 24-byte packet has a valid checksum.
    // The entire packet (including checksum bytes) must yield (0, 0).
    static bool verify(const ByteArray& packet) {
        auto [s1, s2] = compute(packet.data(), PACKET_BYTES);
        return s1 == 0 && s2 == 0;
    }

    // Compute the checksum bytes for a 22-byte body.
    // Returns (C1, C2).
    static std::pair<uint8_t, uint8_t> checksum_bytes(const uint8_t* body, size_t body_len) {
        auto [s1, s2] = compute(body, body_len);
        uint8_t c1 = static_cast<uint8_t>((-s2 - s1) & 0xFF);
        uint8_t c2 = s2;
        return {c1, c2};
    }

    // Check if a byte is a known sync byte.
    static bool is_sync_byte(uint8_t b) {
        for (int i = 0; i < SYNC_BYTES_COUNT; ++i) {
            if (b == SYNC_BYTES[i]) return true;
        }
        return false;
    }
};

} // namespace orbcomm

#endif // ORBCOMM_DSP_FLETCHER_H
