#ifndef ORBCOMM_DSP_TYPES_H
#define ORBCOMM_DSP_TYPES_H

#include <cstdint>
#include <vector>
#include <string>
#include <array>
#include <complex>

namespace orbcomm {

// ─── Physical Layer Constants ──────────────────────────────────────────────

constexpr int    SAMPLE_RATE      = 48000;    // Hz
constexpr int    BIT_RATE         = 4800;     // bps
constexpr int    SAMPLES_PER_BIT  = SAMPLE_RATE / BIT_RATE;  // 10
constexpr int    PACKET_BYTES     = 24;
constexpr int    PACKET_BITS      = PACKET_BYTES * 8;        // 192
constexpr int    PACKET_SAMPLES   = PACKET_BITS * SAMPLES_PER_BIT;  // 1920

// SDPSK matched filter pattern
// Bit 1: [+A,+A,+A,+A,+A, -A,-A,-A,-A,-A]  (half positive, half negative)
// Bit 0: [-A,-A,-A,-A,-A, +A,+A,+A,+A,+A]  (half negative, half positive)
// Pattern weights: [-1,-1,+1,+1,+1,+1,+1,-1,-1,-1]
constexpr int CORRELATION_PATTERN[SAMPLES_PER_BIT] = {
    -1, -1, 1, 1, 1, 1, 1, -1, -1, -1
};

// Valid sync bytes at position 0 of a packet
constexpr uint8_t SYNC_BYTES[] = {
    0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f, 0x22, 0x65
};
constexpr int SYNC_BYTES_COUNT = sizeof(SYNC_BYTES) / sizeof(SYNC_BYTES[0]);

// ─── Sample Types ──────────────────────────────────────────────────────────

using Sample    = int16_t;   // Raw audio sample (16-bit PCM)
using Real      = float;     // Floating-point sample for DSP
using Complex   = std::complex<Real>;
using Bits      = std::vector<uint8_t>;   // 0 or 1
using ByteArray = std::array<uint8_t, PACKET_BYTES>;

// ─── Packet Type Enum ──────────────────────────────────────────────────────

enum class PacketType : uint8_t {
    Message               = 0x1a,
    UplinkChannels        = 0x1b,
    DownlinkChannels      = 0x1c,
    NetworkControl        = 0x1d,
    FillPacket            = 0x1e,
    Unknown1f             = 0x1f,
    Ephemeris             = 0x22,
    NetworkAnnouncement   = 0x65,
};

// ─── Decoded Packet ────────────────────────────────────────────────────────

struct EphemerisData {
    int     satellite_id;
    int     week;
    int     day;
    int     hour;
    int     minute;
    int     second;
    double  longitude;
    double  latitude;
    double  altitude;
};

struct NetworkAnnouncementData {
    int     satellite_id;
    double  frequency_mhz;
    int     frame;
};

struct MessageData {
    int     total_parts;
    int     part_number;
    std::array<int, 4> data;
};

struct UplinkChannelsData {
    std::vector<double> frequencies_mhz;
};

struct DownlinkChannelsData {
    int     total_parts;
    int     part_number;
    std::vector<int> channels;
};

struct FillPacketData {
    std::array<uint8_t, 9> data;
};

struct NetworkControlData {
    std::array<uint8_t, 9> data;
};

struct DecodedPacket {
    PacketType          type;
    uint8_t             type_byte;
    ByteArray           raw;
    uint64_t            timestamp;  // sample offset

    // Parsed fields (only one is valid depending on type)
    EphemerisData           ephemeris;
    NetworkAnnouncementData announcement;
    MessageData             message;
    UplinkChannelsData      uplink_channels;
    DownlinkChannelsData    downlink_channels;
    FillPacketData          fill_packet;
    NetworkControlData      network_control;
};

} // namespace orbcomm

#endif // ORBCOMM_DSP_TYPES_H
