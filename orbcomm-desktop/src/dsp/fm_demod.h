#ifndef ORBCOMM_DSP_FM_DEMOD_H
#define ORBCOMM_DSP_FM_DEMOD_H

#include "types.h"
#include <cmath>
#include <vector>

namespace orbcomm {

// ─── FM Demodulator ───────────────────────────────────────────────────────
//
// Converts complex IQ samples to real audio via phase-difference FM
// discrimination.  The output is proportional to the original modulating
// signal (SDPSK baseband).
//
//   audio[n] = arg(iq[n] * conj(iq[n-1]))
//
// For narrowband FM with small deviation, this linear discriminator
// recovers the modulating signal without needing arctan per sample.

class FmDemodulator {
public:
    FmDemodulator() : prev_i_(0.0f), prev_q_(0.0f) {}

    // Demodulate one IQ sample, returning the instantaneous frequency.
    Real demodulate_one(Complex sample) {
        Real i = static_cast<Real>(std::real(sample));
        Real q = static_cast<Real>(std::imag(sample));

        // Phase difference via cross-product / dot-product
        // angle(s[n] * conj(s[n-1])) = atan2(cross, dot)
        Real dot   = i * prev_i_ + q * prev_q_;
        Real cross = i * prev_q_ - q * prev_i_;
        prev_i_ = i;
        prev_q_ = q;

        return std::atan2(cross, dot);
    }

    // Demodulate a buffer of IQ samples in-place to real audio.
    // The first sample is dropped (no previous sample for difference).
    // output must have room for num_iq - 1 samples.
    void demodulate(const Complex* iq, Real* output, size_t num_iq) {
        if (num_iq < 2) return;

        for (size_t i = 1; i < num_iq; ++i) {
            output[i - 1] = demodulate_one(iq[i]);
        }
    }

    // Convenience: demoduate into a vector
    std::vector<Real> demodulate(const std::vector<Complex>& iq) {
        std::vector<Real> out(iq.size() > 0 ? iq.size() - 1 : 0);
        if (!iq.empty()) {
            demodulate(iq.data(), out.data(), iq.size());
        }
        return out;
    }

    // Reset the previous-sample state.
    void reset() {
        prev_i_ = 0.0f;
        prev_q_ = 0.0f;
    }

private:
    Real prev_i_ = 0.0f;
    Real prev_q_ = 0.0f;
};

} // namespace orbcomm

#endif // ORBCOMM_DSP_FM_DEMOD_H
