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

// ─── Streaming Decoder ─────────────────────────────────────────────────────
//
// Wraps the stateless Decoder with an overlap buffer so that packets
// spanning chunk boundaries are captured.  Call push() with each new
// chunk of audio; packets are emitted via the callback.

class StreamingDecoder {
public:
    using PacketCallback = std::function<void(const DecodedPacket&)>;

    explicit StreamingDecoder(PacketCallback callback)
        : callback_(std::move(callback)) {}

    // Feed a new chunk of audio samples.
    void push(const Sample* samples, size_t num_samples) {
        // Append to overlap buffer
        buffer_.insert(buffer_.end(), samples, samples + num_samples);

        // Keep enough samples to span a full packet
        if (buffer_.size() < PACKET_SAMPLES) return;

        // Decode what we have
        decoder_.decode(buffer_.data(), buffer_.size(), callback_);

        // Keep the last PACKET_SAMPLES for overlap with the next chunk
        if (buffer_.size() > PACKET_SAMPLES) {
            size_t keep = buffer_.size() - PACKET_SAMPLES;
            buffer_.erase(buffer_.begin(), buffer_.begin() + keep);
        }
    }

    // Process all remaining buffered samples.
    void flush() {
        if (!buffer_.empty()) {
            decoder_.decode(buffer_.data(), buffer_.size(), callback_);
            buffer_.clear();
        }
    }

private:
    Decoder                decoder_;
    PacketCallback         callback_;
    std::vector<Sample>    buffer_;
};

} // namespace orbcomm

#endif // ORBCOMM_DSP_DECODER_H
