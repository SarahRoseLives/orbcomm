#ifndef ORBCOMM_TUI_SIGNAL_PANEL_H
#define ORBCOMM_TUI_SIGNAL_PANEL_H

#include <string>
#include <sstream>
#include <iomanip>
#include <chrono>

namespace orbcomm {
namespace tui {

// ─── Signal Panel ─────────────────────────────────────────────────────────
//
// Displays real-time stats: packet count, decode rate, sample progress,
// frequency, and gain.  Produces FTXUI DOM elements.

struct SignalStats {
    uint64_t packets_total    = 0;
    uint64_t samples_processed = 0;
    uint64_t samples_total     = 0;
    double   frequency_mhz     = 137.5;
    int      gain              = 20;
    bool     running           = false;
    bool     is_rtl            = false;
    double   elapsed_sec       = 0.0;
    double   packet_rate       = 0.0;  // packets per minute
};

class SignalPanel {
public:
    SignalStats compute(const WorkerState& state) {
        SignalStats s;

        s.packets_total     = state.packets_decoded.load();
        s.samples_processed = state.samples_processed.load();
        s.samples_total     = state.total_samples.load();
        s.frequency_mhz     = state.frequency_mhz.load();
        s.gain              = state.gain.load();
        s.running           = state.running.load();
        s.is_rtl            = state.is_rtl.load();

        auto now = std::chrono::steady_clock::now();
        s.elapsed_sec = std::chrono::duration<double>(now - start_time_).count();

        if (s.elapsed_sec > 0.5) {
            s.packet_rate = s.packets_total / s.elapsed_sec * 60.0;
        }

        return s;
    }

    void reset_clock() {
        start_time_ = std::chrono::steady_clock::now();
    }

    std::string render(const SignalStats& s) const {
        std::ostringstream ss;

        // Progress
        if (s.samples_total > 0) {
            double progress = (double)s.samples_processed / s.samples_total * 100.0;
            ss << "Pos: " << std::fixed << std::setprecision(1)
               << progress << "%  ";
        } else {
            ss << "Pos: " << (s.samples_processed / 48000.0) << "s  ";
        }

        // Packet stats
        ss << "Packets: " << s.packets_total
           << "  Rate: " << std::fixed << std::setprecision(1)
           << s.packet_rate << "/min";

        return ss.str();
    }

    std::string render_status(const SignalStats& s) const {
        std::ostringstream ss;

        ss << "  Freq: " << std::fixed << std::setprecision(4)
           << s.frequency_mhz << " MHz  ";

        if (!s.is_rtl) {
            ss << "Input: WAV file";
        } else {
            ss << "Input: RTL-SDR  Gain: " << s.gain << " dB";
        }

        ss << "  [" << (s.running ? "RUNNING" : "STOPPED") << "]";

        return ss.str();
    }

private:
    std::chrono::steady_clock::time_point start_time_{
        std::chrono::steady_clock::now()
    };
};

} // namespace tui
} // namespace orbcomm

#endif // ORBCOMM_TUI_SIGNAL_PANEL_H
