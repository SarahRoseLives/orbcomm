#ifndef ORBCOMM_TUI_PACKET_TABLE_H
#define ORBCOMM_TUI_PACKET_TABLE_H

#include "dsp/packet_parser.h"
#include <vector>
#include <string>
#include <ctime>
#include <iomanip>
#include <sstream>

namespace orbcomm {
namespace tui {

// ─── Packet Table ──────────────────────────────────────────────────────────
//
// Maintains a scrollable list of recently decoded packets and produces
// an FTXUI DOM element for rendering.

class PacketTable {
public:
    static constexpr int MAX_PACKETS = 500;

    void add(const DecodedPacket& pkt) {
        if (packets_.size() >= MAX_PACKETS) {
            packets_.erase(packets_.begin());
        }
        packets_.push_back(pkt);
    }

    void add_bulk(const std::vector<DecodedPacket>& pkts) {
        for (auto& p : pkts) add(p);
    }

    const std::vector<DecodedPacket>& packets() const { return packets_; }
    size_t size() const { return packets_.size(); }

    // Format a packet as a single compact line for display.
    static std::string format_line(const DecodedPacket& pkt) {
        // Timestamp
        auto now = std::time(nullptr);
        auto* tm = std::localtime(&now);
        char time_buf[16];
        std::strftime(time_buf, sizeof(time_buf), "%H:%M:%S", tm);

        std::ostringstream ss;
        ss << time_buf << "  ";

        switch (pkt.type) {
            case PacketType::NetworkAnnouncement: {
                auto& a = pkt.announcement;
                ss << "Spacecraft  FM-" << std::setw(2) << std::setfill('0')
                   << a.satellite_id << "  "
                   << std::fixed << std::setprecision(3)
                   << a.frequency_mhz << "MHz  fr " << a.frame;
                break;
            }
            case PacketType::Ephemeris: {
                auto& e = pkt.ephemeris;
                ss << "Ephemeris   FM-" << std::setw(2) << std::setfill('0')
                   << e.satellite_id << "  wk " << e.week
                   << "  pos " << std::fixed << std::setprecision(0)
                   << e.longitude << " " << e.latitude;
                break;
            }
            case PacketType::Message: {
                auto& m = pkt.message;
                ss << "Message     pt " << m.part_number
                   << "/" << m.total_parts << "  "
                   << std::hex << std::uppercase
                   << m.data[0] << " " << m.data[1] << " "
                   << m.data[2] << " " << m.data[3];
                break;
            }
            case PacketType::UplinkChannels: {
                ss << "Uplink      ";
                for (auto f : pkt.uplink_channels.frequencies_mhz) {
                    ss << std::fixed << std::setprecision(3) << f << "MHz ";
                }
                break;
            }
            case PacketType::DownlinkChannels: {
                auto& d = pkt.downlink_channels;
                ss << "Downlink    pt " << d.part_number << "/" << d.total_parts;
                break;
            }
            case PacketType::NetworkControl: {
                ss << "NetControl  ";
                for (auto b : pkt.network_control.data) {
                    ss << std::hex << std::uppercase << std::setw(2)
                       << std::setfill('0') << (int)b;
                }
                break;
            }
            case PacketType::FillPacket: {
                ss << "FillPacket  ";
                for (auto b : pkt.fill_packet.data) {
                    ss << std::hex << std::uppercase << std::setw(2)
                       << std::setfill('0') << (int)b;
                }
                break;
            }
            default: {
                ss << "Unknown     0x" << std::hex << (int)pkt.type_byte;
                break;
            }
        }

        return ss.str();
    }

    // Generate column-aligned header + rows for FTXUI rendering.
    std::vector<std::string> render_lines(int max_lines) const {
        std::vector<std::string> lines;
        lines.push_back("  Time      Type         Data");

        int start = std::max(0, (int)packets_.size() - max_lines);
        for (int i = start; i < (int)packets_.size(); ++i) {
            lines.push_back("  " + format_line(packets_[i]));
        }

        if (packets_.empty()) {
            lines.push_back("  (waiting for packets...)");
        }

        return lines;
    }

private:
    std::vector<DecodedPacket> packets_;
};

} // namespace tui
} // namespace orbcomm

#endif // ORBCOMM_TUI_PACKET_TABLE_H
