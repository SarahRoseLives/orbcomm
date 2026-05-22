#ifndef ORBCOMM_DSP_RTL_SOURCE_H
#define ORBCOMM_DSP_RTL_SOURCE_H

#include "types.h"
#include <cstdint>
#include <vector>
#include <functional>
#include <stdexcept>
#include <string>
#include <atomic>
#include <thread>
#include <chrono>

#ifdef HAS_RTLSDR
#include <rtl-sdr.h>
#endif

namespace orbcomm {

// ─── RTL-SDR Source ────────────────────────────────────────────────────────
//
// RAII wrapper around librtlsdr for streaming IQ samples.
// Uses the asynchronous read API for non-blocking operation.

class RtlSource {
public:
    using IQCallback = std::function<void(const uint8_t* data, size_t num_iq)>;

    struct Config {
        uint32_t frequency    = 137500000;  // Hz
        uint32_t sample_rate  = 240000;     // Hz (240k)
        int      gain         = 20;         // dB (0 = auto)
        int      device_index = 0;
        int      buffer_count = 16;         // number of transfer buffers
        int      buffer_size  = 16384;      // bytes per buffer
    };

#ifdef HAS_RTLSDR
    RtlSource() : dev_(nullptr) {}

    ~RtlSource() { stop(); }

    // List available devices.
    static std::vector<std::string> list_devices() {
        std::vector<std::string> devs;
        uint32_t count = rtlsdr_get_device_count();
        for (uint32_t i = 0; i < count; ++i) {
            char mfg[256] = {}, prod[256] = {}, serial[256] = {};
            rtlsdr_get_device_usb_strings(i, mfg, prod, serial);
            char buf[512];
            snprintf(buf, sizeof(buf), "[%u] %s %s %s", i, mfg, prod, serial);
            devs.emplace_back(buf);
        }
        return devs;
    }

    // Open device and configure.
    void open(const Config& cfg) {
        if (dev_) stop();

        int rc = rtlsdr_open(&dev_, cfg.device_index);
        if (rc < 0) {
            throw std::runtime_error("rtlsdr_open failed: " +
                                     std::to_string(rc));
        }

        config_ = cfg;

        rtlsdr_set_sample_rate(dev_, cfg.sample_rate);
        rtlsdr_set_center_freq(dev_, cfg.frequency);
        rtlsdr_set_tuner_gain_mode(dev_, cfg.gain == 0 ? 0 : 1);
        if (cfg.gain > 0) {
            rtlsdr_set_tuner_gain(dev_, cfg.gain);
        }
        rtlsdr_reset_buffer(dev_);
    }

    // Start streaming.  The callback is called from a librtlsdr thread
    // for each buffer of IQ data received.
    // Note: the callback must be fast and not block.
    void start(IQCallback callback) {
        if (!dev_) {
            throw std::runtime_error("RtlSource not opened");
        }

        callback_ = std::move(callback);
        running_ = true;

        rtlsdr_read_async(dev_,
            [](unsigned char* buf, uint32_t len, void* ctx) {
                auto* self = static_cast<RtlSource*>(ctx);
                if (self->running_ && self->callback_) {
                    // len is in bytes; convert to IQ pairs
                    self->callback_(buf, len / 2);
                }
            },
            this,
            config_.buffer_count,
            config_.buffer_size
        );
    }

    // Stop streaming.
    void stop() {
        running_ = false;
        if (dev_) {
            rtlsdr_cancel_async(dev_);
            rtlsdr_close(dev_);
            dev_ = nullptr;
        }
    }

    bool is_open() const { return dev_ != nullptr; }
    const Config& config() const { return config_; }

private:
    rtlsdr_dev_t* dev_;
    Config        config_;
    IQCallback    callback_;
    std::atomic<bool> running_{false};
};

#else // !HAS_RTLSDR

    // Stub implementation when librtlsdr is not available.
    RtlSource() = default;
    ~RtlSource() = default;

    static std::vector<std::string> list_devices() {
        return {};
    }

    void open(const Config&) {
        throw std::runtime_error(
            "RTL-SDR support not compiled in. Rebuild with -DHAS_RTLSDR=ON"
        );
    }

    void start(IQCallback) {
        throw std::runtime_error("RTL-SDR not available");
    }

    void stop() {}
    bool is_open() const { return false; }
    const Config& config() const { return config_; }

private:
    Config config_;
};

#endif // HAS_RTLSDR

} // namespace orbcomm

#endif // ORBCOMM_DSP_RTL_SOURCE_H
