#include "tui/app.h"
#include "dsp/rtl_source.h"

#include <iostream>
#include <string>

int main(int argc, char* argv[]) {
    using namespace orbcomm;

    std::string wav_path;
    RtlSource::Config rtl_cfg;

    // Parse command-line arguments
    for (int i = 1; i < argc; ++i) {
        std::string arg(argv[i]);

        if (arg == "--file" && i + 1 < argc) {
            wav_path = argv[++i];
        }
        else if (arg == "--freq" && i + 1 < argc) {
            rtl_cfg.frequency = static_cast<uint32_t>(
                std::stod(argv[++i])
            );
        }
        else if (arg == "--gain" && i + 1 < argc) {
            rtl_cfg.gain = std::stoi(argv[++i]);
        }
        else if (arg == "--rate" && i + 1 < argc) {
            rtl_cfg.sample_rate = std::stoi(argv[++i]);
        }
        else if (arg == "--device" && i + 1 < argc) {
            rtl_cfg.device_index = std::stoi(argv[++i]);
        }
        else if (arg == "--list-devices") {
#ifdef HAS_RTLSDR
            auto devs = RtlSource::list_devices();
            if (devs.empty()) {
                std::cout << "No RTL-SDR devices found.\n";
            } else {
                for (auto& d : devs) std::cout << d << "\n";
            }
#else
            std::cout << "RTL-SDR support not compiled in.\n";
#endif
            return 0;
        }
        else if (arg == "--help" || arg == "-h") {
            std::cout <<
                "Orbcomm Decoder TUI\n\n"
                "Usage:\n"
                "  orbcomm_tui [options]\n\n"
                "Options:\n"
                "  --file <path>      Decode WAV file (48kHz mono 16-bit)\n"
                "  --freq <hz>        RTL-SDR frequency (default: 137.5e6)\n"
                "  --gain <db>        RTL-SDR gain (0=auto, default: 20)\n"
                "  --rate <hz>        RTL-SDR sample rate (default: 240000)\n"
                "  --device <n>       RTL-SDR device index (default: 0)\n"
                "  --list-devices     List available RTL-SDR devices\n"
                "  --help             Show this help\n\n"
                "Keyboard controls:\n"
                "  S       Start/stop decode\n"
                "  Q/Esc   Quit\n\n"
                "Examples:\n"
                "  orbcomm_tui --freq 137.5e6 --gain 30\n"
                "  orbcomm_tui --file recording.wav\n";
            return 0;
        }
    }

    // Default: if neither WAV nor RTL frequency specified, try RTL at 137.5 MHz
    if (wav_path.empty() && rtl_cfg.frequency == 0) {
#ifdef HAS_RTLSDR
        rtl_cfg.frequency = 137500000;
        rtl_cfg.sample_rate = 240000;
        rtl_cfg.gain = 20;
#else
        std::cerr << "No input specified. Use --file <wav> or rebuild with RTL-SDR support.\n";
        return 1;
#endif
    }

    tui::App app;
    app.run(wav_path, rtl_cfg);

    return 0;
}
