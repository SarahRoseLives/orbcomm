#ifndef ORBCOMM_DSP_RESAMPLER_H
#define ORBCOMM_DSP_RESAMPLER_H

#include "types.h"
#include "fm_demod.h"
#include <vector>
#include <algorithm>

namespace orbcomm {

// ─── Rational Resampler ────────────────────────────────────────────────────
//
// Decimates a real signal by an integer factor using linear interpolation.
// For the RTL-SDR pipeline: 240 kHz → 48 kHz (factor 5).
//
// Linear interpolation is sufficient for narrowband FM audio because the
// signal bandwidth (~12.5 kHz) is well below the output Nyquist (24 kHz).

class Resampler {
public:
    // Decimate by an integer factor.
    // Each output sample is the linear interpolation of input samples
    // at the decimated positions.
    static std::vector<Real> decimate(const Real* input, size_t input_len,
                                       int factor) {
        if (factor <= 0) return {};
        size_t output_len = input_len / factor;
        std::vector<Real> output(output_len);

        for (size_t i = 0; i < output_len; ++i) {
            Real pos = static_cast<Real>(i) * factor;
            size_t idx = static_cast<size_t>(pos);
            Real frac = pos - static_cast<Real>(idx);

            Real a = input[idx];
            Real b = (idx + 1 < input_len) ? input[idx + 1] : a;
            output[i] = a + frac * (b - a);
        }

        return output;
    }

    // Convenience: decimate a vector
    static std::vector<Real> decimate(const std::vector<Real>& input,
                                       int factor) {
        return decimate(input.data(), input.size(), factor);
    }

    // Anti-alias filter using a simple boxcar moving average before decimation.
    // The filter length should be >= decimation factor to prevent aliasing.
    // This is a cheap approximation of a sinc filter — good enough for NFM.
    static std::vector<Real> aa_filter(const Real* input, size_t len,
                                        int window) {
        std::vector<Real> out(len);
        Real sum = 0.0;
        for (size_t i = 0; i < len; ++i) {
            sum += input[i];
            if (i >= static_cast<size_t>(window)) {
                sum -= input[i - window];
            }
            size_t n = std::min(i + 1, static_cast<size_t>(window));
            out[i] = sum / static_cast<Real>(n);
        }
        return out;
    }

    // Full pipeline: anti-alias filter + decimate
    static std::vector<Real> decimate_filtered(const Real* input,
                                                 size_t len,
                                                 int factor) {
        auto filtered = aa_filter(input, len, factor);
        return decimate(filtered.data(), filtered.size(), factor);
    }
};

// ─── IQ to Real Converter ─────────────────────────────────────────────────
//
// Converts interleaved uint8_t IQ from RTL-SDR (unsigned 8-bit offset binary)
// to floating-point complex samples, then FM-demodulates and decimates
// to produce 48 kHz real audio suitable for SDPSK decoding.
//
// RTL-SDR IQ format: interleaved uint8_t, values 0..255, center at 127.5.
//
// Pipeline:  uint8 IQ → float complex → FM demod → decimate → audio

class RtlSdrPipeline {
public:
    // Process a buffer of RTL-SDR uint8 IQ samples and produce
    // real audio at the target sample rate.
    //
    // Parameters:
    //   iq_data    - raw uint8 IQ from rtl_sdr
    //   num_iq     - number of IQ pairs (each pair = 2 bytes)
    //   iq_rate    - RTL-SDR sample rate in Hz (e.g., 240000)
    //   audio_rate - desired audio rate (48000)
    //
    // Returns: vector of Real audio samples at audio_rate
    static std::vector<Real> process(
        const uint8_t* iq_data,
        size_t num_iq,
        double iq_rate,
        double audio_rate)
    {
        // Convert uint8 IQ to complex float
        std::vector<Complex> iq(num_iq);
        for (size_t i = 0; i < num_iq; ++i) {
            Real i_val = (static_cast<Real>(iq_data[2 * i]) - 127.4f) / 128.0f;
            Real q_val = (static_cast<Real>(iq_data[2 * i + 1]) - 127.4f) / 128.0f;
            iq[i] = Complex(i_val, q_val);
        }

        // FM demodulate at IQ rate
        FmDemodulator fm;
        auto audio_fm = fm.demodulate(iq);

        // Decimate to audio rate
        int factor = static_cast<int>(iq_rate / audio_rate + 0.5);
        if (factor < 1) factor = 1;

        // Anti-alias filter then decimate
        auto filtered = Resampler::aa_filter(
            audio_fm.data(), audio_fm.size(), factor
        );
        return Resampler::decimate(filtered.data(), filtered.size(), factor);
    }
};

} // namespace orbcomm

#endif // ORBCOMM_DSP_RESAMPLER_H
