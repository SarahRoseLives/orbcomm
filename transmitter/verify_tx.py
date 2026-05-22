#!/usr/bin/env python3
"""
Verify the Orbcomm transmitter output by FM-demodulating the IQ back to
baseband audio and running it through the decoder.

This simulates a receiver chain: HackRF TX → RTL-SDR RX → FM demod → decoder.

Usage:
    python verify_tx.py test_signal.iq
"""

import sys
import os
import struct
import wave
import math
import argparse

import numpy as np

# Allow importing from parent
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from orbcomm_decoder import decode_from_samples, format_packet


def read_iq_file(path: str) -> np.ndarray:
    """Read interleaved int8 IQ file into complex64 array."""
    raw = np.fromfile(path, dtype=np.int8)
    i = raw[0::2].astype(np.float32) / 127.0
    q = raw[1::2].astype(np.float32) / 127.0
    return i + 1j * q


def fm_demodulate(iq: np.ndarray, tx_rate: float, audio_rate: float = 48000.0) -> np.ndarray:
    """
    FM-demodulate complex IQ to real audio.

    Uses the discrete derivative of phase: audio[k] ≈ angle(iq[k] * conj(iq[k-1]))
    scaled by tx_rate / (2π * deviation) to recover the original modulation.

    Returns float64 array at audio_rate.
    """
    # Phase difference (FM discriminator)
    phase_diff = np.angle(iq[1:] * np.conj(iq[:-1]))

    # Scale: the original modulating signal had deviation applied relative to tx_rate
    # We just need to recover the audio envelope; amplitude doesn't need exact calibration
    # since the decoder only cares about sign of correlation.

    # Resample to audio rate
    num_audio = int(len(phase_diff) * audio_rate / tx_rate)
    indices = np.linspace(0, len(phase_diff) - 1, num_audio)
    audio = np.interp(indices, np.arange(len(phase_diff)), phase_diff)

    # Normalize amplitude to match the original SDPSK levels (~16000)
    peak = np.max(np.abs(audio))
    if peak > 0:
        audio = audio / peak * 16000.0

    return audio


def audio_to_wav(audio: np.ndarray, path: str, sample_rate: int = 48000):
    """Write float64 audio to 16-bit mono WAV."""
    scaled = np.clip(audio, -32767, 32767).astype(np.int16)
    with wave.open(path, "wb") as wf:
        wf.setnchannels(1)
        wf.setsampwidth(2)
        wf.setframerate(sample_rate)
        wf.writeframes(scaled.tobytes())


def main():
    parser = argparse.ArgumentParser(description="Verify Orbcomm TX by FM-demod + decode")
    parser.add_argument("iq_file", help="Path to IQ file (interleaved int8)")
    parser.add_argument("--rate", type=float, default=2e6, help="IQ sample rate (default: 2e6)")
    parser.add_argument("--audio-out", type=str, default=None, help="Save demodulated audio WAV")
    args = parser.parse_args()

    print(f"Reading {args.iq_file}...")
    iq = read_iq_file(args.iq_file)
    print(f"  IQ samples: {len(iq)} at {args.rate/1e6:.1f} Msps "
          f"({len(iq)/args.rate:.3f}s)")

    print("FM-demodulating...")
    audio = fm_demodulate(iq, tx_rate=args.rate)
    print(f"  Audio samples: {len(audio)} at 48000 Hz")

    if args.audio_out:
        audio_to_wav(audio, args.audio_out)
        print(f"  Saved audio to {args.audio_out}")

    print("Decoding...")
    samples = list(audio.astype(int))
    packets = decode_from_samples(samples, verbose=False)
    seg = ""  # suppress the "Processing..." line by capturing? Actually it goes to stdout.
    print(f"  Found {len(packets)} valid packets")

    for pkt in packets:
        print(f"  {format_packet(pkt)}")

    if not packets:
        print("  WARNING: No packets decoded. Check FM deviation and signal quality.")


if __name__ == "__main__":
    main()
