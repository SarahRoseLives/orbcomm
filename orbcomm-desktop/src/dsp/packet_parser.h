#ifndef ORBCOMM_DSP_PACKET_PARSER_H
#define ORBCOMM_DSP_PACKET_PARSER_H

#include "types.h"
#include "fletcher.h"
#include <cstring>
#include <cmath>

namespace orbcomm {

// ─── Packet Parser ─────────────────────────────────────────────────────────
//
// Parses the fields of a valid 24-byte Orbcomm packet depending on its type.

class PacketParser {
public:
    // Parse a raw 24-byte packet into a DecodedPacket structure.
    // Returns false if the packet fails checksum verification.
    static bool parse(const ByteArray& raw, uint64_t sample_offset,
                      DecodedPacket& out) {
        if (!Fletcher::verify(raw)) {
            return false;
        }

        out.raw = raw;
        out.type_byte = raw[0];
        out.timestamp = sample_offset;

        switch (static_cast<PacketType>(raw[0])) {
            case PacketType::NetworkAnnouncement:
                out.type = PacketType::NetworkAnnouncement;
                parse_network_announcement(raw, out.announcement);
                break;
            case PacketType::Ephemeris:
                out.type = PacketType::Ephemeris;
                parse_ephemeris(raw, out.ephemeris);
                break;
            case PacketType::Message:
                out.type = PacketType::Message;
                parse_message(raw, out.message);
                break;
            case PacketType::UplinkChannels:
                out.type = PacketType::UplinkChannels;
                parse_uplink_channels(raw, out.uplink_channels);
                break;
            case PacketType::DownlinkChannels:
                out.type = PacketType::DownlinkChannels;
                parse_downlink_channels(raw, out.downlink_channels);
                break;
            case PacketType::NetworkControl:
                out.type = PacketType::NetworkControl;
                parse_network_control(raw, out.network_control);
                break;
            case PacketType::FillPacket:
                out.type = PacketType::FillPacket;
                parse_fill_packet(raw, out.fill_packet);
                break;
            default:
                out.type = static_cast<PacketType>(raw[0]);
                break;
        }

        return true;
    }

    // Format a decoded packet as a human-readable string.
    static std::string format(const DecodedPacket& pkt) {
        char buf[512];

        switch (pkt.type) {
            case PacketType::NetworkAnnouncement: {
                auto& a = pkt.announcement;
                snprintf(buf, sizeof(buf),
                    "Spacecraft FM-%02d %8.4lfMHz frame %02d",
                    a.satellite_id, a.frequency_mhz, a.frame);
                break;
            }
            case PacketType::Ephemeris: {
                auto& e = pkt.ephemeris;
                snprintf(buf, sizeof(buf),
                    "Ephemeris FM-%02d wk.%d %d %02d:%02d:%02d pos. %.0f %.0f",
                    e.satellite_id, e.week, e.day,
                    e.hour, e.minute, e.second,
                    e.longitude, e.latitude);
                break;
            }
            case PacketType::Message: {
                auto& m = pkt.message;
                snprintf(buf, sizeof(buf),
                    "Message (pt. %d of %d) %05X %05X %05X %05X",
                    m.part_number, m.total_parts,
                    m.data[0], m.data[1], m.data[2], m.data[3]);
                break;
            }
            case PacketType::UplinkChannels: {
                auto& u = pkt.uplink_channels;
                int off = snprintf(buf, sizeof(buf), "Uplink channels ");
                for (size_t i = 0; i < u.frequencies_mhz.size() && off < 450; ++i) {
                    off += snprintf(buf + off, sizeof(buf) - off,
                                    "%8.4lfMHz ", u.frequencies_mhz[i]);
                }
                break;
            }
            case PacketType::DownlinkChannels: {
                auto& d = pkt.downlink_channels;
                int off = snprintf(buf, sizeof(buf),
                    "Downlink channels (pt %d of %d) ",
                    d.part_number, d.total_parts);
                for (size_t i = 0; i < d.channels.size() && off < 450; ++i) {
                    off += snprintf(buf + off, sizeof(buf) - off,
                                    "%d ", d.channels[i]);
                }
                break;
            }
            case PacketType::NetworkControl: {
                auto& n = pkt.network_control;
                int off = snprintf(buf, sizeof(buf), "Network Control ");
                for (int i = 0; i < 9; ++i) {
                    off += snprintf(buf + off, sizeof(buf) - off,
                                    "%02X", n.data[i]);
                }
                break;
            }
            case PacketType::FillPacket: {
                auto& f = pkt.fill_packet;
                int off = snprintf(buf, sizeof(buf), "Fill packet ");
                for (int i = 0; i < 9; ++i) {
                    off += snprintf(buf + off, sizeof(buf) - off,
                                    "%02X", f.data[i]);
                }
                break;
            }
            default: {
                snprintf(buf, sizeof(buf), "Unknown type 0x%02X", pkt.type_byte);
                break;
            }
        }

        return std::string(buf);
    }

private:
    // ─── Type-Specific Parsers ──────────────────────────────────────────

    static void parse_network_announcement(const ByteArray& pkt,
                                            NetworkAnnouncementData& out) {
        out.satellite_id = pkt[3] & 0xFF;
        int freq_raw = pkt[4] & 0xFF;
        if (freq_raw <= 0x40) freq_raw += 0x100;
        out.frequency_mhz = 137.0 + freq_raw * 0.0025;
        out.frame = (pkt[5] >> 4) & 0x0F;
    }

    static void parse_ephemeris(const ByteArray& pkt, EphemerisData& out) {
        // Satellite ID from upper nibble of byte 1
        out.satellite_id = pkt[1] & 0x0F;

        // Timestamp is packed in bytes 2-4
        int ts = (pkt[2] << 16) | (pkt[3] << 8) | pkt[4];

        // The original uses arithmetic with magic constants.
        // Simplified extraction: treat as seconds-of-week-ish.
        int total = ts;
        out.second = total % 60;  total /= 60;
        out.minute = total % 60;  total /= 60;
        out.hour   = total % 24;  total /= 24;
        out.day    = total % 7;
        out.week   = total / 7;

        // Position: 20-bit signed values, subtract 0x80000, shift left 4
        int raw_lon = ((pkt[5] & 0xFF) << 12) |
                      ((pkt[6] & 0xFF) << 4) |
                      ((pkt[7] >> 4) & 0x0F);
        int raw_lat = (((pkt[7] & 0x0F) << 16) |
                       ((pkt[8] & 0xFF) << 8) |
                       (pkt[9] & 0xFF)) << 4;
        int raw_alt = ((pkt[10] & 0xFF) << 12) |
                      ((pkt[11] & 0xFF) << 4) |
                      ((pkt[12] >> 4) & 0x0F);

        out.longitude = (raw_lon - 0x80000) << 4;
        out.latitude  = (raw_lat - 0x80000) << 4;
        out.altitude  = (raw_alt - 0x80000) << 4;
    }

    static void parse_message(const ByteArray& pkt, MessageData& out) {
        out.total_parts = (pkt[1] >> 4) & 0x0F;
        out.part_number = pkt[1] & 0x0F;
        for (int i = 0; i < 4; ++i) {
            int off = 2 + i * 3;
            out.data[i] = ((pkt[off] & 0xFF) << 8) | (pkt[off + 1] & 0xFF);
        }
    }

    static void parse_uplink_channels(const ByteArray& pkt,
                                       UplinkChannelsData& out) {
        for (int i = 3; i <= 8; ++i) {
            int val = pkt[i] & 0xFF;
            if (val != 0) {
                out.frequencies_mhz.push_back(137.0 + val * 0.0025);
            }
        }
    }

    static void parse_downlink_channels(const ByteArray& pkt,
                                         DownlinkChannelsData& out) {
        out.total_parts = pkt[1] & 0x0F;
        out.part_number = (pkt[1] >> 4) & 0x0F;
        for (int i = 0; i < 5; ++i) {
            int off = 2 + i * 4;
            if (off + 3 < PACKET_BYTES) {
                int val = (pkt[off] << 24) | (pkt[off + 1] << 16) |
                          (pkt[off + 2] << 8) | pkt[off + 3];
                out.channels.push_back(val);
            }
        }
    }

    static void parse_network_control(const ByteArray& pkt,
                                       NetworkControlData& out) {
        for (int i = 0; i < 9; ++i) {
            out.data[i] = pkt[i + 1];
        }
    }

    static void parse_fill_packet(const ByteArray& pkt, FillPacketData& out) {
        for (int i = 0; i < 9; ++i) {
            out.data[i] = pkt[i + 1];
        }
    }
};

} // namespace orbcomm

#endif // ORBCOMM_DSP_PACKET_PARSER_H
