#ifndef ORBCOMM_DSP_DECODER_H
#define ORBCOMM_DSP_DECODER_H

#include "types.h"
#include "sdpsk_demod.h"
#include "fletcher.h"
#include "packet_framer.h"
#include "packet_parser.h"
#include <vector>

namespace orbcomm {

// ─── Orbcomm Decoder ───────────────────────────────────────────────────────
//
// Top-level decoder that processes raw PCM audio samples and emits
// decoded Orbcomm packets.

class Decoder {
public:
    using PacketCallback = std::function<void(const DecodedPacket&)>;

    Decoder() = default;

    // Process a buffer of 16-bit mono PCM samples at 48000 Hz.
    // Calls `callback` for each successfully decoded packet.
    size_t decode(const Sample* samples,
                  size_t num_samples,
                  PacketCallback callback)
    {
        std::vector<DecodedPacket> results;

        PacketFramer::scan_all_phases(
            samples, num_samples, 0,
            [&](const ByteArray& raw, uint64_t offset) {
                DecodedPacket pkt;
                if (PacketParser::parse(raw, offset, pkt)) {
                    results.push_back(pkt);
                }
            }
        );

        // Deduplicate by raw hex content (same packet found at multiple phases)
        std::vector<DecodedPacket> unique;
        for (auto& pkt : results) {
            bool dup = false;
            for (auto& u : unique) {
                if (u.raw == pkt.raw) { dup = true; break; }
            }
            if (!dup) {
                unique.push_back(pkt);
                if (callback) callback(pkt);
            }
        }

        return unique.size();
    }
};

} // namespace orbcomm

#endif // ORBCOMM_DSP_DECODER_H
