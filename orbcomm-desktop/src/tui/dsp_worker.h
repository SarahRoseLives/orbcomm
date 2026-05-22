#ifndef ORBCOMM_TUI_DSP_WORKER_H
#define ORBCOMM_TUI_DSP_WORKER_H

#include "dsp/types.h"
#include "dsp/decoder.h"
#include "dsp/resampler.h"
#include "dsp/rtl_source.h"
#include "dsp/fm_demod.h"
#include "dsp/packet_parser.h"

#include <atomic>
#include <chrono>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <memory>

namespace orbcomm {
namespace tui {

// ─── DSP Worker ────────────────────────────────────────────────────────────
//
// Runs the full decode pipeline in a background thread.
// Supports two input modes:
//   1. WAV file playback (paced to simulate real-time)
//   2. RTL-SDR live capture
//
// Thread-safe packet output via a mutex-protected queue.

struct WorkerState {
    std::atomic<bool> running{false};
    std::atomic<bool> is_rtl{false};
    std::atomic<uint64_t> samples_processed{0};
    std::atomic<uint64_t> packets_decoded{0};
    std::atomic<uint64_t> total_samples{0};   // WAV file length
    std::atomic<double>   frequency_mhz{137.5};
    std::atomic<int>      gain{20};
    std::string           input_path;
};

class DspWorker {
public:
    using PacketCallback = std::function<void(const DecodedPacket&)>;

    DspWorker() = default;
    ~DspWorker() { stop(); }

    WorkerState& state() { return state_; }

    // Lock the packet queue and retrieve pending packets.
    // Call this from the UI thread at ~10 Hz.
    std::vector<DecodedPacket> drain_packets() {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        auto pkts = std::move(packet_queue_);
        packet_queue_.clear();
        return pkts;
    }

    // Start decoding from a WAV file (simulates real-time by pacing).
    void start_file(const std::string& path) {
        stop();
        state_.input_path = path;
        state_.is_rtl = false;
        state_.running = true;
        thread_ = std::thread(&DspWorker::run_file, this);
    }

    // Start decoding from RTL-SDR.
    void start_rtl(RtlSource::Config cfg) {
        stop();
        state_.is_rtl = true;
        state_.frequency_mhz = cfg.frequency / 1e6;
        state_.gain = cfg.gain;
        state_.running = true;
        rtl_config_ = cfg;
        thread_ = std::thread(&DspWorker::run_rtl, this);
    }

    void stop() {
        state_.running = false;
        if (thread_.joinable()) {
            thread_.join();
        }
        if (rtl_source_) {
            rtl_source_->stop();
        }
    }

private:
    void push_packet(const DecodedPacket& pkt) {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        packet_queue_.push_back(pkt);
    }

    // ─── WAV File Playback ─────────────────────────────────────────────

    void run_file() {
        // Read entire WAV into memory
        std::ifstream file(state_.input_path, std::ios::binary);
        if (!file) return;

        // Skip WAV header
        file.seekg(44, std::ios::beg);

        std::vector<Sample> all_samples;
        int16_t s;
        while (file.read(reinterpret_cast<char*>(&s), 2)) {
            all_samples.push_back(s);
        }

        state_.total_samples = all_samples.size();

        // Process in chunks to simulate real-time
        constexpr size_t chunk_samples = SAMPLE_RATE / 10; // 100ms chunks
        StreamingDecoder stream_decoder(
            [this](const DecodedPacket& pkt) {
                push_packet(pkt);
                state_.packets_decoded.fetch_add(1);
            }
        );
        size_t offset = 0;
        auto start_time = std::chrono::steady_clock::now();

        while (state_.running && offset < all_samples.size()) {
            size_t chunk = std::min(chunk_samples, all_samples.size() - offset);

            // Feed chunk to streaming decoder (handles overlap)
            stream_decoder.push(all_samples.data() + offset, chunk);

            offset += chunk;
            state_.samples_processed = offset;

            // Pace to real-time: 48000 sps → chunk_samples / 48000 seconds
            auto elapsed = std::chrono::steady_clock::now() - start_time;
            auto expected = std::chrono::microseconds(
                static_cast<int64_t>(offset) * 1000000 / SAMPLE_RATE
            );
            if (expected > elapsed) {
                std::this_thread::sleep_for(expected - elapsed);
            }
        }

        stream_decoder.flush();
        state_.running = false;
    }

    // ─── RTL-SDR Live Capture ──────────────────────────────────────────

    void run_rtl() {
#ifdef HAS_RTLSDR
        rtl_source_ = std::make_unique<RtlSource>();
        rtl_source_->open(rtl_config_);

        // Buffer for accumulating IQ samples
        std::vector<uint8_t> iq_buffer;
        std::mutex iq_mutex;
        Decoder decoder;

        rtl_source_->start([&](const uint8_t* data, size_t num_iq) {
            if (!state_.running) return;

            // Process IQ → FM audio → SDPSK decode
            auto audio = RtlSdrPipeline::process(
                data, num_iq,
                rtl_config_.sample_rate,
                SAMPLE_RATE
            );

            // Convert to Sample format and decode
            std::vector<Sample> samples(audio.size());
            for (size_t i = 0; i < audio.size(); ++i) {
                // Scale: FM demod output is in radians, scale to 16-bit range
                samples[i] = static_cast<Sample>(
                    std::clamp(audio[i] * 20000.0f, -32767.0f, 32767.0f)
                );
            }

            decoder.decode(
                samples.data(), samples.size(),
                [this](const DecodedPacket& pkt) {
                    push_packet(pkt);
                    state_.packets_decoded.fetch_add(1);
                }
            );

            state_.samples_processed.fetch_add(samples.size());
        });
#else
        (void)rtl_config_;
#endif
    }

    WorkerState                     state_;
    std::thread                     thread_;
    std::mutex                      queue_mutex_;
    std::vector<DecodedPacket>      packet_queue_;
    RtlSource::Config               rtl_config_;
    std::unique_ptr<RtlSource>      rtl_source_;
};

} // namespace tui
} // namespace orbcomm

#endif // ORBCOMM_TUI_DSP_WORKER_H
