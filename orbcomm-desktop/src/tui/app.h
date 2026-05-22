#ifndef ORBCOMM_TUI_APP_H
#define ORBCOMM_TUI_APP_H

#include "tui/dsp_worker.h"
#include "tui/packet_table.h"
#include "tui/signal_panel.h"

#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/screen.hpp>
#include <ftxui/screen/string.hpp>
#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>

#include <atomic>
#include <chrono>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace orbcomm {
namespace tui {

using namespace ftxui;

// ─── TUI Application ───────────────────────────────────────────────────────
//
// Orchestrates the FTXUI-based Orbcomm decoder dashboard.
// Main thread runs the UI event loop.  DSP runs in a background thread.

class App {
public:
    App() {
        worker_ = std::make_unique<DspWorker>();
    }

    ~App() {
        running_ = false;
        if (refresh_thread_.joinable()) {
            refresh_thread_.join();
        }
        worker_->stop();
    }

    // Run the TUI.  Blocks until the user quits.
    void run(const std::string& wav_path = "",
             RtlSource::Config rtl_config = {})
    {
        auto screen = ScreenInteractive::Fullscreen();

        // ── State ──────────────────────────────────────────────────────
        std::string status_text = "Press S to start, Q to quit";
        int scroll_offset = 0;

        // ── Components ─────────────────────────────────────────────────
        auto renderer = Renderer([&] {
            return layout(status_text, scroll_offset);
        });

        auto component = CatchEvent(renderer, [&](Event event) -> bool {
            return handle_key(event, status_text);
        });

        // ── Timer-based UI refresh ─────────────────────────────────────
        running_ = true;
        refresh_thread_ = std::thread([&] {
            while (running_) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                screen.Post([&] {
                    // Drain new packets from worker
                    auto pkts = worker_->drain_packets();
                    if (!pkts.empty()) {
                        table_.add_bulk(pkts);
                    }

                    // Prevent unbounded table growth
                    if (table_.size() > 500) {
                        // table_ auto-trims at MAX_PACKETS
                    }
                });
                screen.Post(Event::Custom);
            }
        });

        // ── Auto-start if WAV path provided ────────────────────────────
        if (!wav_path.empty()) {
            worker_->start_file(wav_path);
            panel_.reset_clock();
            status_text = "Decoding: " + wav_path;
        }
#ifdef HAS_RTLSDR
        else if (rtl_config.frequency > 0) {
            worker_->start_rtl(rtl_config);
            panel_.reset_clock();
            status_text = "RTL-SDR capture @ " +
                          std::to_string(rtl_config.frequency / 1e6) + " MHz";
        }
#endif

        screen.Loop(component);

        // Cleanup
        running_ = false;
        worker_->stop();
    }

private:
    // ── Layout ─────────────────────────────────────────────────────────

    Element layout(const std::string& status, int scroll) {
        auto& state = worker_->state();
        auto stats = panel_.compute(state);

        // Build packet lines
        auto packet_lines = table_.render_lines(30);

        Elements packet_elements;
        for (auto& line : packet_lines) {
            packet_elements.push_back(text(line));
        }

        auto packet_box = vbox(std::move(packet_elements)) |
                          border |
                          color(Color::Cyan);

        // Stats
        auto stats_bar = hbox({
            text(" " + panel_.render(stats) + " "),
            filler(),
            text(panel_.render_status(stats) + " ")
        }) | color(Color::White);

        // Help bar
        auto help = hbox({
            text(" S:Start  Q:Quit  [+/-]:Scroll  ") | dim,
            text(status) | color(Color::Yellow),
        }) | border;

        return vbox({
            text(" Orbcomm Decoder v1.0 ") | bold | center | color(Color::Cyan),
            separator(),
            packet_box | flex,
            separator(),
            stats_bar,
            help,
        });
    }

    // ── Keyboard ───────────────────────────────────────────────────────

    bool handle_key(Event event, std::string& status) {
        if (event == Event::Character('q') || event == Event::Character('Q') ||
            event == Event::Escape) {
            running_ = false;
            worker_->stop();
            return true; // handled → exit
        }

        if (event == Event::Character('s') || event == Event::Character('S')) {
            auto& state = worker_->state();
            if (state.running) {
                worker_->stop();
                status = "Stopped";
            } else {
                // Re-start with same source
                if (!state.input_path.empty()) {
                    worker_->start_file(state.input_path);
                    panel_.reset_clock();
                    status = "Decoding: " + state.input_path;
                }
#ifdef HAS_RTLSDR
                else {
                    RtlSource::Config cfg;
                    cfg.frequency = static_cast<uint32_t>(state.frequency_mhz * 1e6);
                    cfg.gain = static_cast<int>(state.gain);
                    worker_->start_rtl(cfg);
                    panel_.reset_clock();
                    status = "RTL-SDR capture @ " +
                             std::to_string(state.frequency_mhz) + " MHz";
                }
#endif
            }
            return true;
        }

        return false;
    }

    std::unique_ptr<DspWorker> worker_;
    PacketTable                table_;
    SignalPanel                panel_;
    std::atomic<bool>          running_{false};
    std::thread                refresh_thread_;
};

} // namespace tui
} // namespace orbcomm

#endif // ORBCOMM_TUI_APP_H
