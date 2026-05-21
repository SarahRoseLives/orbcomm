#!/usr/bin/env python3
"""
Orbcomm Signal Decoder

Decodes Orbcomm satellite VHF downlink signals from raw audio.
Implements SDPSK matched-filter demodulation at 4800 bps from 48000 Hz audio.

Based on reverse engineering of OrbcommPlotter by COAA.

Usage:
    python orbcomm_decoder.py <input.wav> [--raw] [--verbose]

The input must be 48000 Hz mono 16-bit PCM WAV audio.
"""

import struct
import sys
import wave
import argparse
from datetime import datetime


# ─── Constants ───────────────────────────────────────────────────────────────

SAMPLE_RATE  = 48000
BIT_RATE     = 4800
SAMPLES_PER_BIT = SAMPLE_RATE // BIT_RATE  # 10
PACKET_BYTES = 24
PACKET_BITS  = PACKET_BYTES * 8            # 192
PACKET_SAMPLES = PACKET_BITS * SAMPLES_PER_BIT  # 1920

# SDPSK matched filter pattern: maps each sample to +1 or -1 correlation weight.
# Bit 1: [+A,+A,+A,+A,+A, -A,-A,-A,-A,-A]  → half positive, half negative
# Bit 0: [-A,-A,-A,-A,-A, +A,+A,+A,+A,+A]  → half negative, half positive
# Pattern is 10 samples: [0,0,1,1,1,1,1,0,0,0]
CORRELATION_PATTERN = [-1, -1, 1, 1, 1, 1, 1, -1, -1, -1]

# Valid sync bytes at position 0 of a packet
SYNC_BYTES = {0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f, 0x22, 0x65}

# Sync pattern for raw logging filter
SYNC_65_A8_F9 = bytes([0x65, 0xA8, 0xF9])

# ─── Fletcher Checksum ──────────────────────────────────────────────────────

def fletcher_checksum(data: bytes) -> tuple[int, int]:
    """Compute Fletcher-8 checksum. Returns (sum1, sum2)."""
    s1, s2 = 0, 0
    for b in data:
        s1 = (s1 + b) & 0xFF
        s2 = (s2 + s1) & 0xFF
    return s1, s2


def verify_packet(packet: bytes) -> bool:
    """Verify a 24-byte packet's Fletcher checksum."""
    if len(packet) != PACKET_BYTES:
        return False
    s1, s2 = fletcher_checksum(packet)
    return s1 == 0 and s2 == 0


# ─── SDPSK Demodulator ──────────────────────────────────────────────────────

def demodulate_bit(samples: list[int]) -> int:
    """Demodulate a single bit from 10 samples using SDPSK matched filter."""
    corr = sum(s * p for s, p in zip(samples, CORRELATION_PATTERN))
    return 1 if corr > 0 else 0


def demodulate_to_bits(samples: list[int]) -> list[int]:
    """Demodulate sample stream into bit stream."""
    bits = []
    for i in range(0, len(samples) - SAMPLES_PER_BIT + 1, SAMPLES_PER_BIT):
        bits.append(demodulate_bit(samples[i:i + SAMPLES_PER_BIT]))
    return bits


def bits_to_bytes(bits: list[int]) -> bytes:
    """Convert bit list to bytes (MSB first)."""
    result = bytearray()
    for i in range(0, len(bits) - 7, 8):
        byte = 0
        for j in range(8):
            byte = (byte << 1) | bits[i + j]
        result.append(byte)
    return bytes(result)


# ─── Packet Extraction ──────────────────────────────────────────────────────

def extract_packets(bits: list[int]) -> list[bytes]:
    """Scan bit stream for valid Orbcomm packets."""
    packets = []
    i = 0
    while i < len(bits) - PACKET_BITS:
        candidate = bits_to_bytes(bits[i:i + PACKET_BITS])
        if len(candidate) == PACKET_BYTES:
            if candidate[0] in SYNC_BYTES and verify_packet(candidate):
                packets.append(candidate)
                i += PACKET_BITS  # skip past this packet
                continue
        i += 1  # slide by 1 bit
    return packets


def extract_packets_sample_aligned(samples: list[int]) -> list[bytes]:
    """Try all bit-phase offsets (0..9) to find packets."""
    all_packets = []
    for offset in range(SAMPLES_PER_BIT):
        bits = demodulate_to_bits(samples[offset:])
        packets = extract_packets(bits)
        for pkt in packets:
            all_packets.append(pkt)
    return all_packets


# ─── Packet Parsers ─────────────────────────────────────────────────────────

def parse_network_announcement(pkt: bytes) -> dict:
    """Parse type 0x65 (Network Announcement / Gateway Information)."""
    sat_id = pkt[3] & 0xFF
    freq_raw = pkt[4] & 0xFF
    if freq_raw <= 0x40:
        freq_raw += 0x100
    freq_mhz = 137.0 + (freq_raw * 0.0025)  # approximate scaling
    frame = (pkt[5] >> 4) & 0x0F
    return {
        "type": "network_announcement",
        "satellite_id": sat_id,
        "frequency_mhz": round(freq_mhz, 4),
        "frame": frame,
    }


def parse_ephemeris(pkt: bytes) -> dict:
    """Parse type 0x22 (Ephemeris data)."""
    sat_lo = pkt[1] & 0x0F
    sat_hi = pkt[2] & 0x0F
    sat_id = sat_hi  # satellite ID from upper nibble

    # Timestamp (packed)
    # The exact decomposition uses integer arithmetic with magic constants
    t1 = pkt[3] & 0xFF
    t2 = pkt[4] & 0xFF
    t3 = pkt[5] & 0xFF
    ts = (t1 << 16) | (t2 << 8) | t3

    # Position: 20-bit signed values
    raw_lon = ((pkt[6] & 0xFF) << 12) | ((pkt[7] & 0xFF) << 4) | ((pkt[8] >> 4) & 0x0F)
    raw_lat = (((pkt[8] & 0x0F) << 16) | ((pkt[9] & 0xFF) << 8) | (pkt[10] & 0xFF)) << 4
    raw_alt = (((pkt[11] & 0xFF) << 12) | ((pkt[12] & 0xFF) << 4) | ((pkt[13] >> 4) & 0x0F))

    # Sign correction (subtract 0x80000 then shift left 4)
    lon = (raw_lon - 0x80000) << 4
    lat = (raw_lat - 0x80000) << 4
    alt = (raw_alt - 0x80000) << 4

    return {
        "type": "ephemeris",
        "satellite_id": sat_id,
        "timestamp_raw": ts,
        "longitude": lon,
        "latitude": lat,
        "altitude": alt,
    }


def parse_message(pkt: bytes) -> dict:
    """Parse type 0x1a (Segmented Message)."""
    total_parts = (pkt[1] >> 4) & 0x0F
    part_num = pkt[1] & 0x0F
    data = []
    for i in range(4):
        offset = 2 + i * 3
        val = ((pkt[offset] & 0xFF) << 8) | (pkt[offset + 1] & 0xFF)
        data.append(val)
    return {
        "type": "message",
        "total_parts": total_parts,
        "part_number": part_num,
        "data": data,
    }


def parse_uplink_channels(pkt: bytes) -> dict:
    """Parse type 0x1b (Uplink Channels)."""
    channels = []
    for i in range(3, 9):
        val = pkt[i] & 0xFF
        if val:
            freq = 137.0 + (val * 0.0025)  # approximate
            channels.append(round(freq, 4))
    return {
        "type": "uplink_channels",
        "channels": channels,
    }


def parse_downlink_channels(pkt: bytes) -> dict:
    """Parse type 0x1c (Downlink Channels)."""
    total_parts = pkt[1] & 0x0F
    part_num = (pkt[1] >> 4) & 0x0F
    channels = []
    for i in range(5):
        offset = 2 + i * 4
        if offset + 3 < PACKET_BYTES:
            val = (pkt[offset] << 24) | (pkt[offset + 1] << 16) | \
                  (pkt[offset + 2] << 8) | pkt[offset + 3]
            channels.append(val)
    return {
        "type": "downlink_channels",
        "total_parts": total_parts,
        "part_number": part_num,
        "channels": channels,
    }


def parse_network_control(pkt: bytes) -> dict:
    """Parse type 0x1d (Network Control)."""
    data = list(pkt[1:10])
    return {
        "type": "network_control",
        "data": data,
    }


def parse_fill_packet(pkt: bytes) -> dict:
    """Parse type 0x1e (Fill Packet)."""
    data = list(pkt[1:10])
    return {
        "type": "fill_packet",
        "data": data,
    }


def parse_packet(pkt: bytes) -> dict | None:
    """
    Parse an Orbcomm packet. Returns a dict with decoded fields, or None.
    """
    if not verify_packet(pkt):
        return None

    msg_type = pkt[0]

    result = {"type_byte": msg_type, "raw": pkt.hex().upper()}

    if msg_type == 0x65:
        result.update(parse_network_announcement(pkt))
    elif msg_type == 0x22:
        result.update(parse_ephemeris(pkt))
    elif msg_type == 0x1a:
        result.update(parse_message(pkt))
    elif msg_type == 0x1b:
        result.update(parse_uplink_channels(pkt))
    elif msg_type == 0x1c:
        result.update(parse_downlink_channels(pkt))
    elif msg_type == 0x1d:
        result.update(parse_network_control(pkt))
    elif msg_type == 0x1e:
        result.update(parse_fill_packet(pkt))
    else:
        result["type"] = f"unknown_0x{msg_type:02x}"

    return result


# ─── Display Formatting ─────────────────────────────────────────────────────

def format_packet(pkt: dict) -> str:
    """Format a decoded packet for display."""
    t = pkt.get("type", "unknown")
    ts = datetime.now().strftime("%Y-%m-%d %H:%M:%S")

    if t == "network_announcement":
        return (f"[{ts}] Spacecraft FM-{pkt['satellite_id']:02d} "
                f"{pkt['frequency_mhz']:.4f}MHz frame {pkt['frame']:02d}")

    elif t == "ephemeris":
        return (f"[{ts}] Ephemeris FM-{pkt['satellite_id']:02d} "
                f"pos. {pkt['longitude']} {pkt['latitude']}")

    elif t == "message":
        data_str = " ".join(f"{v:05X}" for v in pkt["data"])
        return (f"[{ts}] Message (pt. {pkt['part_number']} of {pkt['total_parts']}) "
                f"{data_str}")

    elif t == "uplink_channels":
        ch_str = " ".join(f"{c:.4f}MHz" for c in pkt["channels"])
        return f"[{ts}] Uplink channels {ch_str}"

    elif t == "downlink_channels":
        ch_str = " ".join(str(c) for c in pkt["channels"])
        return (f"[{ts}] Downlink channels "
                f"(pt {pkt['part_number']} of {pkt['total_parts']}) {ch_str}")

    elif t == "network_control":
        data_str = "".join(f"{b:02X}" for b in pkt["data"])
        return f"[{ts}] Network Control {data_str}"

    elif t == "fill_packet":
        data_str = "".join(f"{b:02X}" for b in pkt["data"])
        return f"[{ts}] Fill packet {data_str}"

    else:
        return f"[{ts}] Unknown type 0x{pkt['type_byte']:02X}: {pkt['raw']}"


# ─── Audio Input ────────────────────────────────────────────────────────────

def read_wav_samples(path: str) -> list[int]:
    """Read 16-bit mono PCM samples from a WAV file."""
    with wave.open(path, "rb") as wf:
        if wf.getnchannels() != 1:
            raise ValueError(f"Expected mono WAV, got {wf.getnchannels()} channels")
        if wf.getsampwidth() != 2:
            raise ValueError(f"Expected 16-bit WAV, got {wf.getsampwidth() * 8}-bit")
        sr = wf.getframerate()
        if sr != SAMPLE_RATE:
            print(f"Warning: sample rate is {sr} Hz, expected {SAMPLE_RATE} Hz. "
                  f"Resampling not implemented.")

        n_frames = wf.getnframes()
        raw = wf.readframes(n_frames)
        samples = list(struct.unpack(f"<{n_frames}h", raw))
    return samples


# ─── Main Pipeline ──────────────────────────────────────────────────────────

def decode_from_samples(samples: list[int], verbose: bool = False) -> list[dict]:
    """Full decode pipeline from raw PCM samples."""
    print(f"Processing {len(samples)} samples ({len(samples) / SAMPLE_RATE:.1f}s)...")

    all_packets = []
    seen = set()

    for offset in range(SAMPLES_PER_BIT):
        if verbose:
            print(f"  Trying bit offset {offset}...")
        bits = demodulate_to_bits(samples[offset:])
        if verbose:
            print(f"    Got {len(bits)} bits")

        # Extract packets from this bit phase, scanning every bit position
        i = 0
        while i < len(bits) - PACKET_BITS:
            candidate_bits = bits[i:i + PACKET_BITS]
            candidate = bits_to_bytes(candidate_bits)
            if len(candidate) == PACKET_BYTES:
                if candidate[0] in SYNC_BYTES and verify_packet(candidate):
                    hex_key = candidate.hex()
                    if hex_key not in seen:
                        seen.add(hex_key)
                        parsed = parse_packet(candidate)
                        if parsed:
                            all_packets.append(parsed)
                    i += PACKET_BITS  # skip past found packet
                    continue
            i += 1  # slide by 1 bit

    return all_packets


# ─── CLI ─────────────────────────────────────────────────────────────────────

def main():
    parser = argparse.ArgumentParser(
        description="Orbcomm satellite signal decoder",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  python orbcomm_decoder.py recording.wav
  python orbcomm_decoder.py recording.wav --verbose
  python orbcomm_decoder.py recording.wav --raw > packets.json
        """,
    )
    parser.add_argument("input", help="Input WAV file (48000 Hz, 16-bit mono)")
    parser.add_argument("--raw", action="store_true", help="Output raw JSON")
    parser.add_argument("--verbose", "-v", action="store_true", help="Verbose output")
    args = parser.parse_args()

    samples = read_wav_samples(args.input)
    packets = decode_from_samples(samples, verbose=args.verbose)

    if args.raw:
        import json
        print(json.dumps(packets, indent=2))
    else:
        if not packets:
            print("No valid Orbcomm packets found.")
        for pkt in packets:
            print(format_packet(pkt))

    print(f"\nTotal: {len(packets)} valid packets decoded.")


if __name__ == "__main__":
    main()
