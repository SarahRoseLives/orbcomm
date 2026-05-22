#!/usr/bin/env python3
"""
Orbcomm Downlink Transmitter for HackRF

Generates a simulated Orbcomm satellite downlink signal and transmits it via
HackRF. Produces narrowband FM-modulated RF at 137-138 MHz with SDPSK data
at 4800 bps -- identical to what an Orbcomm satellite broadcasts.

Requirements:
    - HackRF One hardware
    - hackrf_transfer (from hackrf-tools)
    - numpy (pip install numpy)

Usage:
    python orbcomm_tx.py [--freq 137.5e6] [--gain 20] [--repeat 10]

The transmitted signal can be received by:
    - A VHF scanner/receiver feeding audio into orbcomm_decoder.py
    - An SDR (RTL-SDR, HackRF, etc.) recording IQ, then FM-demodulated
"""

import struct
import sys
import subprocess
import argparse
import math
import os

# Find hackrf_transfer
_HACKRF_PATH = None
for _candidate in [
    r"C:\HackRF\bin\hackrf_transfer.exe",
    r"C:\Program Files\HackRF\bin\hackrf_transfer.exe",
    "hackrf_transfer",
]:
    if os.path.exists(_candidate) or _candidate == "hackrf_transfer":
        _HACKRF_PATH = _candidate
        break

# Allow importing from the parent orbcomm package
_SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
_PARENT_DIR = os.path.dirname(_SCRIPT_DIR)
if _PARENT_DIR not in sys.path:
    sys.path.insert(0, _PARENT_DIR)

from orbcomm_test_signal import (
    make_packet, build_announcement_packet, build_ephemeris_packet,
    build_fill_packet, modulate_sdpsk, bits_from_bytes,
    SAMPLES_PER_BIT as SPB,
)

try:
    import numpy as np
except ImportError:
    print("Error: numpy is required. Install with: pip install numpy")
    sys.exit(1)


# ─── Constants ───────────────────────────────────────────────────────────────

AUDIO_RATE    = 48000       # Baseband audio sample rate (Hz)
BIT_RATE      = 4800        # SDPSK bit rate (bps)
SPS           = AUDIO_RATE // BIT_RATE  # 10 samples per bit

# HackRF transmit sample rate
TX_RATE       = 2e6          # 2 Msps IQ

# FM deviation for narrowband FM (~2.5 kHz deviation in 12.5 kHz channels)
FM_DEVIATION  = 2500.0       # Hz peak deviation

# Default Orbcomm downlink frequencies (MHz)
ORBCOMM_FREQS = [
    137.250e6, 137.3125e6, 137.375e6, 137.4375e6,
    137.500e6, 137.5625e6, 137.625e6, 137.6875e6,
    137.750e6, 137.8125e6, 137.875e6, 137.9375e6,
]


# ─── Packet Sequence Builder ─────────────────────────────────────────────────

def build_packet_sequence() -> bytes:
    """Build a realistic sequence of Orbcomm packets."""
    packets = []

    for sat_id in [5, 7, 12, 18, 23]:
        freq_raw = 128 + sat_id * 3
        frame = sat_id % 15
        packets.append(build_announcement_packet(sat_id, freq_raw, frame))

    packets.append(build_ephemeris_packet(
        sat_id=7, ts=0x123456, lon=500000, lat=300000, alt=200000
    ))

    for _ in range(3):
        packets.append(build_fill_packet())

    return packets


def packets_to_baseband(packets: list[bytes], idle_bits: int = 32) -> np.ndarray:
    """
    Convert a list of packets into a continuous SDPSK baseband signal.
    """
    idle_samples = [0] * (idle_bits * SPB)
    all_bits = []
    for pkt in packets:
        all_bits.extend(bits_from_bytes(pkt))

    baseband = np.array(modulate_sdpsk(all_bits), dtype=np.float64)
    return baseband


# ─── FM Modulator ────────────────────────────────────────────────────────────

def fm_modulate(baseband: np.ndarray,
                deviation: float = FM_DEVIATION,
                audio_rate: float = AUDIO_RATE,
                tx_rate: float = TX_RATE) -> np.ndarray:
    """
    FM-modulate a baseband signal to complex IQ at the HackRF transmit rate.

    Returns:
        complex64 IQ array normalized to [-1.0, 1.0] for HackRF
    """
    num_samples = int(len(baseband) * tx_rate / audio_rate)
    indices = np.arange(num_samples) * audio_rate / tx_rate
    resampled = np.interp(indices, np.arange(len(baseband)), baseband)

    # Normalize baseband to [-1.0, 1.0] for FM modulation
    peak = np.max(np.abs(resampled))
    if peak > 0:
        resampled = resampled / peak

    # Phase accumulation for FM: phase[k] = phase[k-1] + 2*pi*dev*m[k]/tx_rate
    phase_increment = 2.0 * math.pi * deviation / tx_rate
    phase = np.cumsum(resampled * phase_increment)
    iq = np.exp(1j * phase).astype(np.complex64)

    # Normalize output
    peak = np.max(np.abs(iq))
    if peak > 0:
        iq /= peak

    return iq


# ─── HackRF Transmission ─────────────────────────────────────────────────────

def transmit_hackrf(iq: np.ndarray,
                    freq_hz: float = 137.5e6,
                    tx_rate: float = TX_RATE,
                    gain_db: int = 20,
                    repeat: int = 1):
    """
    Stream IQ samples to hackrf_transfer for transmission.
    """
    if _HACKRF_PATH is None:
        print("Error: hackrf_transfer not found.")
        print("Install hackrf-tools or set the path in the script.")
        sys.exit(1)

    # Interleave I/Q as int8 for hackrf_transfer
    i_scaled = (iq.real * 127).astype(np.int8)
    q_scaled = (iq.imag * 127).astype(np.int8)
    interleaved = np.empty(2 * len(iq), dtype=np.int8)
    interleaved[0::2] = i_scaled
    interleaved[1::2] = q_scaled

    if repeat > 1:
        interleaved = np.tile(interleaved, repeat)

    cmd = [
        _HACKRF_PATH,
        "-t", "-",
        "-f", str(int(freq_hz)),
        "-s", str(int(tx_rate)),
        "-x", str(gain_db),
        "-a", "1",
    ]

    print(f"HackRF: {_HACKRF_PATH}")
    print(f"Transmitting at {freq_hz/1e6:.4f} MHz, {tx_rate/1e6:.1f} Msps, gain={gain_db} dB")
    print(f"Signal duration: {len(interleaved) / (2 * tx_rate):.2f}s")
    print("Press Ctrl+C to stop.")

    try:
        proc = subprocess.Popen(
            cmd,
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
        stdout, stderr = proc.communicate(input=interleaved.tobytes())
        if stderr:
            err_text = stderr.decode(errors="replace")
            # Filter out expected info messages
            for line in err_text.splitlines():
                if "error" in line.lower() or "fail" in line.lower():
                    print(f"  hackrf: {line.strip()}")
    except FileNotFoundError:
        print(f"\nError: {_HACKRF_PATH} not found.")
        sys.exit(1)
    except KeyboardInterrupt:
        proc.terminate()
        print("\nTransmission stopped.")


def save_iq_file(iq: np.ndarray, path: str, tx_rate: float = TX_RATE):
    """Save IQ samples to a file that can be transmitted later."""
    i_scaled = (iq.real * 127).astype(np.int8)
    q_scaled = (iq.imag * 127).astype(np.int8)
    interleaved = np.empty(2 * len(iq), dtype=np.int8)
    interleaved[0::2] = i_scaled
    interleaved[1::2] = q_scaled

    with open(path, "wb") as f:
        f.write(interleaved.tobytes())

    print(f"IQ file saved to {path}")
    print(f"  Transmit with: hackrf_transfer -t {path} -f <freq> -s {int(tx_rate)} -x 20")


# ─── CLI ─────────────────────────────────────────────────────────────────────

def main():
    parser = argparse.ArgumentParser(
        description="Orbcomm downlink transmitter for HackRF",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  python orbcomm_tx.py                           # transmit at 137.5 MHz
  python orbcomm_tx.py --freq 137.3125e6 --gain 30
  python orbcomm_tx.py --save test.iq            # save to file instead of transmit
  python orbcomm_tx.py --list-freqs              # list common Orbcomm freqs
        """,
    )
    parser.add_argument("--freq", type=float, default=137.5e6,
                        help="Center frequency in Hz (default: 137.5e6)")
    parser.add_argument("--gain", type=int, default=20,
                        help="TX gain 0-47 dB (default: 20)")
    parser.add_argument("--rate", type=float, default=TX_RATE,
                        help=f"TX sample rate in Hz (default: {TX_RATE/1e6:.0f}e6)")
    parser.add_argument("--repeat", type=int, default=10,
                        help="Number of times to repeat signal (default: 10)")
    parser.add_argument("--save", type=str, default=None,
                        help="Save IQ to file instead of transmitting")
    parser.add_argument("--deviation", type=float, default=FM_DEVIATION,
                        help=f"FM deviation in Hz (default: {FM_DEVIATION})")
    parser.add_argument("--idle-bits", type=int, default=64,
                        help="Silence bits between packets (default: 64)")
    parser.add_argument("--list-freqs", action="store_true",
                        help="List common Orbcomm downlink frequencies")
    args = parser.parse_args()

    if args.list_freqs:
        print("Common Orbcomm downlink frequencies:")
        for f in ORBCOMM_FREQS:
            print(f"  {f/1e6:.4f} MHz")
        return

    # Build signal
    packets = build_packet_sequence()
    print(f"Generated {len(packets)} packets:")
    for pkt in packets:
        print(f"  type=0x{pkt[0]:02X}, {pkt.hex()[:16]}...")

    baseband = packets_to_baseband(packets, idle_bits=args.idle_bits)
    print(f"Baseband: {len(baseband)} samples at {AUDIO_RATE} Hz "
          f"({len(baseband)/AUDIO_RATE:.2f}s)")

    # FM modulate to IQ at TX rate
    iq = fm_modulate(baseband, deviation=args.deviation,
                     audio_rate=AUDIO_RATE, tx_rate=args.rate)
    print(f"IQ: {len(iq)} samples at {args.rate/1e6:.1f} Msps "
          f"({len(iq)/args.rate:.3f}s)")

    if args.save:
        save_iq_file(iq, args.save, tx_rate=args.rate)
    else:
        transmit_hackrf(iq, freq_hz=args.freq, tx_rate=args.rate,
                        gain_db=args.gain, repeat=args.repeat)


if __name__ == "__main__":
    main()
