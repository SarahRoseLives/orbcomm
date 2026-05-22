#ifndef ORBCOMM_DSP_SDPSK_DEMOD_H
#define ORBCOMM_DSP_SDPSK_DEMOD_H

#include "types.h"

namespace orbcomm {

// ─── SDPSK Demodulator ─────────────────────────────────────────────────────
//
// Demodulates SDPSK (Symmetric Differential Phase Shift Keying) at 4800 bps
// from audio sampled at 48000 Hz (10 samples per bit).
//
// Each bit is encoded as two 5-sample halves of opposite polarity:
//   Bit 1: [+A,+A,+A,+A,+A, -A,-A,-A,-A,-A]
//   Bit 0: [-A,-A,-A,-A,-A, +A,+A,+A,+A,+A]
//
// The matched filter correlates 10 samples against:
//   [-1,-1,+1,+1,+1,+1,+1,-1,-1,-1]
//
// Positive correlation → bit 1, negative → bit 0.

class SdpskDemodulator {
public:
    SdpskDemodulator() = default;

    // Demodulate a single bit from exactly SAMPLES_PER_BIT samples.
    // Returns 1 or 0.
    static int demodulate_bit(const Sample* samples) {
        int corr = 0;
        for (int i = 0; i < SAMPLES_PER_BIT; ++i) {
            corr += static_cast<int>(samples[i]) * CORRELATION_PATTERN[i];
        }
        return corr > 0 ? 1 : 0;
    }

    // Demodulate a continuous buffer of samples into bits.
    // The result has (num_samples / SAMPLES_PER_BIT) bits.
    static Bits demodulate_samples(const Sample* samples, size_t num_samples) {
        Bits bits;
        size_t num_bits = num_samples / SAMPLES_PER_BIT;
        bits.reserve(num_bits);
        for (size_t i = 0; i < num_bits; ++i) {
            bits.push_back(static_cast<uint8_t>(
                demodulate_bit(samples + i * SAMPLES_PER_BIT)
            ));
        }
        return bits;
    }

    // Demodulate samples starting at a given bit-phase offset.
    // offset = 0..9 selects which sample in the first bit period to start at.
    static Bits demodulate_with_offset(const Sample* samples,
                                        size_t num_samples,
                                        int offset) {
        // Skip the first `offset` samples, then demodulate normally.
        size_t remaining = num_samples - offset;
        return demodulate_samples(samples + offset, remaining);
    }

    // Convert bits to bytes (MSB first).
    // The caller must ensure bits.size() is large enough.
    static void bits_to_bytes(const uint8_t* bits, size_t /*num_bits*/,
                               uint8_t* bytes, size_t num_bytes) {
        for (size_t b = 0; b < num_bytes; ++b) {
            uint8_t byte = 0;
            for (int i = 0; i < 8; ++i) {
                byte = static_cast<uint8_t>((byte << 1) | bits[b * 8 + i]);
            }
            bytes[b] = byte;
        }
    }

    // Convert a bit vector to a byte array.
    static ByteArray bits_to_packet(const uint8_t* bits) {
        ByteArray pkt{};
        bits_to_bytes(bits, PACKET_BITS, pkt.data(), PACKET_BYTES);
        return pkt;
    }
};

} // namespace orbcomm

#endif // ORBCOMM_DSP_SDPSK_DEMOD_H
