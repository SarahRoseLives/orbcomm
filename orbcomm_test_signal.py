#!/usr/bin/env python3
"""
Generate a synthetic Orbcomm SDPSK test signal for decoder verification.

Creates a 48kHz 16-bit mono WAV file containing valid Orbcomm packets
modulated with SDPSK at 4800 bps.

Usage:
    python orbcomm_test_signal.py [output.wav]
"""

import struct
import wave
import sys
import os


# ─── Constants ───────────────────────────────────────────────────────────────

SAMPLE_RATE = 48000
BIT_RATE = 4800
SAMPLES_PER_BIT = SAMPLE_RATE // BIT_RATE  # 10
AMPLITUDE = 16000  # 16-bit amplitude

# ─── Fletcher Checksum ──────────────────────────────────────────────────────

def compute_checksum_bytes(data: bytes) -> bytes:
    """
    Compute Fletcher-8 checksum for payload bytes 0-21.
    Returns 2 checksum bytes (C1, C2) such that verifying the full
    24-byte packet yields (s1=0, s2=0).
    """
    s1, s2 = 0, 0
    for b in data[:22]:
        s1 = (s1 + b) & 0xFF
        s2 = (s2 + s1) & 0xFF
    c1 = (-s2 - s1) & 0xFF
    c2 = s2
    return bytes([c1, c2])


def make_packet(msg_type: int, payload: bytes) -> bytes:
    """Create a 24-byte Orbcomm packet with valid checksum."""
    assert len(payload) == 21, f"Payload must be 21 bytes, got {len(payload)}"
    body = bytes([msg_type]) + payload  # 22 bytes
    csum = compute_checksum_bytes(body)
    return body + csum  # 24 bytes


# ─── SDPSK Modulator ────────────────────────────────────────────────────────

def modulate_sdpsk(bits: list[int]) -> list[int]:
    """
    Convert bits to SDPSK baseband samples (10 samples per bit).

    SDPSK: each bit period is divided into two halves.
    Bit 1: first half positive (+A), second half negative (-A)
    Bit 0: first half negative (-A), second half positive (+A)
    """
    HALF = SAMPLES_PER_BIT // 2  # 5
    samples = []
    for bit in bits:
        if bit:
            samples.extend([AMPLITUDE] * HALF + [-AMPLITUDE] * HALF)
        else:
            samples.extend([-AMPLITUDE] * HALF + [AMPLITUDE] * HALF)
    return samples


def bits_from_bytes(data: bytes) -> list[int]:
    """Convert bytes to bit list (MSB first)."""
    bits = []
    for byte in data:
        for i in range(7, -1, -1):
            bits.append((byte >> i) & 1)
    return bits


def write_wav(path: str, samples: list[int]):
    """Write 16-bit mono PCM WAV file."""
    with wave.open(path, "wb") as wf:
        wf.setnchannels(1)
        wf.setsampwidth(2)
        wf.setframerate(SAMPLE_RATE)
        packed = struct.pack(f"<{len(samples)}h", *samples)
        wf.writeframes(packed)
    print(f"Wrote {len(samples)} samples to {path}")


# ─── Packet Builders ────────────────────────────────────────────────────────

def build_announcement_packet(sat_id: int = 5, freq_raw: int = 128, frame: int = 3) -> bytes:
    """
    Build type 0x65 (Network Announcement) packet.
    Format: 65 A8 F9 {sat_id} {freq} {frame_byte} {pad...}
    """
    payload = bytearray(21)
    payload[0] = 0xA8  # sync byte 2
    payload[1] = 0xF9  # sync byte 3
    payload[2] = sat_id & 0xFF
    payload[3] = freq_raw & 0xFF
    payload[4] = (frame << 4) & 0xFF
    # rest stays zero (padding)
    return make_packet(0x65, bytes(payload))


def build_ephemeris_packet(sat_id: int = 7,
                           ts: int = 0x123456,
                           lon: int = 500000,
                           lat: int = 300000,
                           alt: int = 200000) -> bytes:
    """
    Build type 0x22 (Ephemeris) packet.
    Position values are 20-bit signed (pre sign-correction and shift).
    """
    payload = bytearray(21)
    # Satellite ID split across bytes 0-1
    payload[0] = (sat_id & 0x0F)        # sat ID lower nibble
    payload[1] = (sat_id & 0x0F)        # sat ID upper nibble (same)
    # Timestamp (3 bytes)
    payload[2] = (ts >> 16) & 0xFF
    payload[3] = (ts >> 8) & 0xFF
    payload[4] = ts & 0xFF
    # Longitude (20-bit, pre-corrected)
    raw_lon = (lon >> 4) + 0x80000
    payload[5] = (raw_lon >> 12) & 0xFF
    payload[6] = (raw_lon >> 4) & 0xFF
    payload[7] = ((raw_lon & 0x0F) << 4)
    # Latitude
    raw_lat = (lat >> 4) + 0x80000
    payload[8] = (raw_lat >> 12) & 0xFF
    payload[9] = (raw_lat >> 4) & 0xFF
    payload[10] = ((raw_lat & 0x0F) << 4)
    # Altitude
    raw_alt = (alt >> 4) + 0x80000
    payload[11] = (raw_alt >> 12) & 0xFF
    payload[12] = (raw_alt >> 4) & 0xFF
    payload[13] = ((raw_alt & 0x0F) << 4)
    return make_packet(0x22, bytes(payload))


def build_fill_packet(data: list[int] = None) -> bytes:
    """Build type 0x1e (Fill Packet)."""
    if data is None:
        data = [0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0x11, 0x22, 0x33]
    payload = bytearray(21)
    for i, b in enumerate(data[:9]):
        payload[i] = b
    return make_packet(0x1e, bytes(payload))


# ─── Main ────────────────────────────────────────────────────────────────────

def main():
    path = sys.argv[1] if len(sys.argv) > 1 else "orbcomm_test.wav"

    # Build multiple packets for testing
    packets = [
        build_announcement_packet(sat_id=5, freq_raw=128, frame=3),
        build_ephemeris_packet(sat_id=7),
        build_fill_packet(),
        build_announcement_packet(sat_id=12, freq_raw=200, frame=1),
    ]

    # Add padding (silence) between packets and at start/end
    silence = [0] * (SAMPLES_PER_BIT * 20)  # 20 bits of silence between packets

    all_samples = list(silence)  # leading silence
    for pkt in packets:
        bits = bits_from_bytes(pkt)
        samples = modulate_sdpsk(bits)
        all_samples.extend(samples)
        all_samples.extend(silence)

    write_wav(path, all_samples)

    print(f"Generated test signal with {len(packets)} Orbcomm packets:")
    for i, pkt in enumerate(packets):
        print(f"  Packet {i+1}: type=0x{pkt[0]:02X}, {pkt.hex().upper()}")


if __name__ == "__main__":
    main()
