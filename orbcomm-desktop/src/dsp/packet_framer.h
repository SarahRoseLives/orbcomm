#ifndef ORBCOMM_DSP_PACKET_FRAMER_H
#define ORBCOMM_DSP_PACKET_FRAMER_H

#include "types.h"
#include "sdpsk_demod.h"
#include "fletcher.h"
#include <functional>

namespace orbcomm {

// ─── Packet Framer ─────────────────────────────────────────────────────────
//
// Scans a demodulated bit stream for valid Orbcomm packets.
// Implements a sliding-window detector: for each bit position, assembles
// 192 bits into a candidate 24-byte packet, checks the sync byte and
// Fletcher checksum. Valid packets are emitted via callback.

class PacketFramer {
public:
    using PacketCallback = std::function<void(const ByteArray&, uint64_t bit_offset)>;

    PacketFramer() = default;

    // Scan a bit buffer for valid packets.
    // Calls `callback` for each valid packet found.
    // After a packet is found, skips PACKET_BITS bits forward to avoid overlaps.
    static size_t scan(const uint8_t* bits,
                       size_t num_bits,
                       uint64_t base_offset,
                       PacketCallback callback)
    {
        size_t found = 0;
        size_t i = 0;
        uint8_t buf[PACKET_BYTES];

        while (i + PACKET_BITS <= num_bits) {
            // Convert bits to bytes
            SdpskDemodulator::bits_to_bytes(bits + i, PACKET_BITS, buf, PACKET_BYTES);

            ByteArray pkt;
            std::copy(buf, buf + PACKET_BYTES, pkt.begin());

            // Check sync byte and checksum
            if (Fletcher::is_sync_byte(pkt[0]) && Fletcher::verify(pkt)) {
                callback(pkt, base_offset + i);
                ++found;
                i += PACKET_BITS;  // skip past this packet
            } else {
                ++i;  // slide by 1 bit
            }
        }

        return found;
    }

    // Scan with all 10 possible bit-phase offsets.
    // This handles the case where the bit boundary doesn't align with sample boundary.
    static size_t scan_all_phases(const Sample* samples,
                                   size_t num_samples,
                                   uint64_t base_offset,
                                   PacketCallback callback)
    {
        size_t total = 0;

        for (int offset = 0; offset < SAMPLES_PER_BIT; ++offset) {
            Bits bits = SdpskDemodulator::demodulate_with_offset(
                samples, num_samples, offset
            );

            total += scan(
                bits.data(), bits.size(),
                base_offset + offset,
                callback
            );
        }

        return total;
    }
};

} // namespace orbcomm

#endif // ORBCOMM_DSP_PACKET_FRAMER_H
