#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <string>
#include <fstream>
#include <iostream>
#include <iomanip>

#include "dsp/decoder.h"

using namespace orbcomm;

// ─── WAV Reader ────────────────────────────────────────────────────────────

struct WavHeader {
    char     riff[4];       // "RIFF"
    uint32_t file_size;
    char     wave[4];       // "WAVE"
    char     fmt[4];        // "fmt "
    uint32_t fmt_size;
    uint16_t audio_format;  // 1 = PCM
    uint16_t num_channels;
    uint32_t sample_rate;
    uint32_t byte_rate;
    uint16_t block_align;
    uint16_t bits_per_sample;
    char     data_id[4];    // "data"
    uint32_t data_size;
};

static_assert(sizeof(WavHeader) == 44, "WAV header must be 44 bytes");

std::vector<Sample> read_wav(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        std::cerr << "Error: cannot open " << path << "\n";
        return {};
    }

    WavHeader hdr;
    file.read(reinterpret_cast<char*>(&hdr), sizeof(hdr));

    if (std::strncmp(hdr.riff, "RIFF", 4) != 0 ||
        std::strncmp(hdr.wave, "WAVE", 4) != 0) {
        std::cerr << "Error: not a valid WAV file\n";
        return {};
    }

    if (hdr.num_channels != 1) {
        std::cerr << "Error: expected mono WAV, got "
                  << hdr.num_channels << " channels\n";
        return {};
    }

    if (hdr.bits_per_sample != 16) {
        std::cerr << "Error: expected 16-bit WAV, got "
                  << hdr.bits_per_sample << "-bit\n";
        return {};
    }

    if (hdr.sample_rate != SAMPLE_RATE) {
        std::cerr << "Warning: sample rate is " << hdr.sample_rate
                  << " Hz, expected " << SAMPLE_RATE << " Hz\n";
    }

    size_t num_samples = hdr.data_size / (hdr.bits_per_sample / 8);
    std::vector<Sample> samples(num_samples);
    file.read(reinterpret_cast<char*>(samples.data()), hdr.data_size);

    return samples;
}

// ─── Main ──────────────────────────────────────────────────────────────────

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: orbcomm_cli <input.wav>\n";
        return 1;
    }

    std::string path = argv[1];
    auto samples = read_wav(path);
    if (samples.empty()) {
        return 1;
    }

    std::cout << "Processing " << samples.size() << " samples ("
              << (double)samples.size() / SAMPLE_RATE << "s)...\n";

    Decoder decoder;
    size_t count = decoder.decode(
        samples.data(), samples.size(),
        [](const DecodedPacket& pkt) {
            std::cout << PacketParser::format(pkt) << "\n";
        }
    );

    std::cout << "\nTotal: " << count << " valid packets decoded.\n";
    return 0;
}
