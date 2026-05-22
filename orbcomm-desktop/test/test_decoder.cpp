#include <cstdio>
#include <cstring>
#include <cassert>
#include <vector>
#include <iostream>
#include <iomanip>

#include "dsp/types.h"
#include "dsp/sdpsk_demod.h"
#include "dsp/fletcher.h"
#include "dsp/packet_framer.h"
#include "dsp/packet_parser.h"
#include "dsp/decoder.h"

using namespace orbcomm;

// ─── Helper: modulate a bit sequence to SDPSK samples ─────────────────────

std::vector<Sample> modulate_sdpsk(const std::vector<uint8_t>& bits) {
    std::vector<Sample> samples;
    for (auto bit : bits) {
        Sample first = bit ? 16000 : -16000;
        Sample second = bit ? -16000 : 16000;
        for (int i = 0; i < SAMPLES_PER_BIT / 2; ++i) samples.push_back(first);
        for (int i = 0; i < SAMPLES_PER_BIT / 2; ++i) samples.push_back(second);
    }
    return samples;
}

// ─── Helper: convert bytes to bits ─────────────────────────────────────────

std::vector<uint8_t> bytes_to_bits(const uint8_t* bytes, size_t n) {
    std::vector<uint8_t> bits;
    for (size_t i = 0; i < n; ++i) {
        for (int b = 7; b >= 0; --b) {
            bits.push_back((bytes[i] >> b) & 1);
        }
    }
    return bits;
}

// ─── Test 1: SDPSK round-trip ──────────────────────────────────────────────

void test_sdpsk_roundtrip() {
    std::cout << "Test 1: SDPSK round-trip... ";

    // Generate a known bit pattern: alternating 1s and 0s
    std::vector<uint8_t> bits = {1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0};

    // Modulate
    auto samples = modulate_sdpsk(bits);

    // Demodulate
    for (size_t i = 0; i < bits.size(); ++i) {
        int recovered = SdpskDemodulator::demodulate_bit(
            samples.data() + i * SAMPLES_PER_BIT
        );
        if (recovered != bits[i]) {
            std::cerr << "Bit mismatch at " << i << ": expected "
                      << (int)bits[i] << " got " << recovered << "\n";
            std::exit(1);
        }
    }

    std::cout << "PASSED\n";
}

// ─── Test 2: Bits to bytes ─────────────────────────────────────────────────

void test_bits_to_bytes() {
    std::cout << "Test 2: Bits to bytes... ";

    // 0x65 = 01100101
    uint8_t bits[] = {0,1,1,0,0,1,0,1};
    uint8_t byte;
    SdpskDemodulator::bits_to_bytes(bits, 8, &byte, 1);
    assert(byte == 0x65 && "Bits to bytes conversion failed");

    // 0xA8 = 10101000
    uint8_t bits2[] = {1,0,1,0,1,0,0,0};
    SdpskDemodulator::bits_to_bytes(bits2, 8, &byte, 1);
    assert(byte == 0xA8 && "Bits to bytes conversion failed");

    std::cout << "PASSED\n";
}

// ─── Test 3: Fletcher checksum ─────────────────────────────────────────────

void test_fletcher() {
    std::cout << "Test 3: Fletcher checksum... ";

    // Known valid packet: 65 A8 F9 05 80 30 ... CC 79
    ByteArray pkt = {
        0x65, 0xA8, 0xF9, 0x05, 0x80, 0x30,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0xCC, 0x79
    };
    assert(Fletcher::verify(pkt) && "Valid packet should verify");

    // Corrupt a byte
    pkt[3] = 0xFF;
    assert(!Fletcher::verify(pkt) && "Corrupted packet should fail");

    // Test checksum generation
    uint8_t body[22] = {
        0x65, 0xA8, 0xF9, 0x05, 0x80, 0x30,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00
    };
    auto [c1, c2] = Fletcher::checksum_bytes(body, 22);
    assert(c1 == 0xCC && c2 == 0x79 && "Checksum byte generation failed");

    std::cout << "PASSED\n";
}

// ─── Test 4: Sync byte detection ───────────────────────────────────────────

void test_sync_bytes() {
    std::cout << "Test 4: Sync byte detection... ";

    assert(Fletcher::is_sync_byte(0x1a));
    assert(Fletcher::is_sync_byte(0x65));
    assert(!Fletcher::is_sync_byte(0x00));
    assert(!Fletcher::is_sync_byte(0xFF));

    std::cout << "PASSED\n";
}

// ─── Test 5: Full packet pipeline (modulate → demodulate → decode) ────────

void test_full_pipeline() {
    std::cout << "Test 5: Full pipeline... ";

    // Build a valid announcement packet manually
    uint8_t body[22] = {
        0x65, 0xA8, 0xF9, 0x05, 0x80, 0x30,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00
    };
    auto [c1, c2] = Fletcher::checksum_bytes(body, 22);

    ByteArray pkt;
    std::copy(body, body + 22, pkt.begin());
    pkt[22] = c1;
    pkt[23] = c2;

    // Convert to bits
    auto bits = bytes_to_bits(pkt.data(), PACKET_BYTES);

    // Modulate
    auto samples = modulate_sdpsk(bits);

    // Decode
    Decoder decoder;
    int count = 0;
    decoder.decode(samples.data(), samples.size(),
        [&](const DecodedPacket& pkt) {
            (void)pkt; // suppress unused warning in release builds
            assert(pkt.type == PacketType::NetworkAnnouncement);
            assert(pkt.announcement.satellite_id == 5);
            assert(pkt.announcement.frequency_mhz > 137.0 &&
                   pkt.announcement.frequency_mhz < 138.0);
            count++;
        }
    );

    assert(count >= 1 && "Should decode at least 1 packet");
    std::cout << "PASSED (" << count << " packets)\n";
}

// ─── Test 6: Known packet type parsing ─────────────────────────────────────

void test_packet_parsing() {
    std::cout << "Test 6: Packet parsing... ";

    // Announcement
    {
        ByteArray pkt = {
            0x65, 0xA8, 0xF9, 0x05, 0x80, 0x30,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0xCC, 0x79
        };
        DecodedPacket dp;
        assert(PacketParser::parse(pkt, 0, dp));
        assert(dp.type == PacketType::NetworkAnnouncement);
        assert(dp.announcement.satellite_id == 5);
        assert(dp.announcement.frame == 3);
    }

    // Ephemeris
    {
        ByteArray pkt = {
            0x22, 0x07, 0x07, 0x12, 0x34, 0x56,
            0x87, 0xA1, 0x20, 0x84, 0x93, 0xE0,
            0x83, 0x0D, 0x40, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0xFC, 0x29
        };
        DecodedPacket dp;
        assert(PacketParser::parse(pkt, 0, dp));
        assert(dp.type == PacketType::Ephemeris);
        assert(dp.ephemeris.satellite_id == 7);
    }

    // Fill packet
    {
        ByteArray pkt = {
            0x1E, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE,
            0xFF, 0x11, 0x22, 0x33, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x81, 0x00
        };
        DecodedPacket dp;
        assert(PacketParser::parse(pkt, 0, dp));
        assert(dp.type == PacketType::FillPacket);
        assert(dp.fill_packet.data[0] == 0xAA);
        assert(dp.fill_packet.data[3] == 0xDD);
    }

    std::cout << "PASSED\n";
}

// ─── Test 7: Packet formatter produces expected strings ────────────────────

void test_formatting() {
    std::cout << "Test 7: Formatting... ";

    DecodedPacket dp;
    dp.type = PacketType::NetworkAnnouncement;
    dp.announcement.satellite_id = 5;
    dp.announcement.frequency_mhz = 137.320;
    dp.announcement.frame = 3;

    std::string s = PacketParser::format(dp);
    assert(s.find("FM-05") != std::string::npos);
    assert(s.find("137.3200MHz") != std::string::npos);

    std::cout << "PASSED\n";
}

// ─── Main ──────────────────────────────────────────────────────────────────

int main() {
    std::cout << "=== Orbcomm DSP Unit Tests ===\n\n";

    test_sdpsk_roundtrip();
    test_bits_to_bytes();
    test_fletcher();
    test_sync_bytes();
    test_full_pipeline();
    test_packet_parsing();
    test_formatting();

    std::cout << "\nAll tests PASSED.\n";
    return 0;
}
