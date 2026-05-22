# Orbcomm HackRF Transmitter

Simulates an Orbcomm satellite downlink using HackRF One hardware.

## Requirements

- HackRF One
- [hackrf-tools](https://github.com/greatscottgadgets/hackrf) (provides `hackrf_transfer`)
- Python 3.10+ with numpy (`pip install numpy`)

## Quick Start

```bash
# List Orbcomm downlink frequencies
python orbcomm_tx.py --list-freqs

# Transmit at default 137.5 MHz
python orbcomm_tx.py

# Transmit at a specific frequency with higher gain
python orbcomm_tx.py --freq 137.3125e6 --gain 30

# Generate IQ file without transmitting
python orbcomm_tx.py --save orbcomm_signal.iq

# Transmit a pre-generated IQ file
hackrf_transfer -t orbcomm_signal.iq -f 137500000 -s 2000000 -x 20 -R
```

## Signal Parameters

| Parameter | Value |
|-----------|-------|
| Modulation | Narrowband FM (2.5 kHz deviation) |
| Baseband | SDPSK at 4800 bps |
| Channel spacing | 12.5 kHz |
| HackRF TX rate | 2 Msps IQ (configurable) |
| Frequency range | 137-138 MHz |

## Receiving

To receive and decode the transmitted signal:

1. Use an RTL-SDR or HackRF to capture IQ at the same frequency
2. FM-demodulate the captured signal (e.g., with GQRX, SDR#, or GNU Radio)
3. Record the FM-demodulated audio as a 48 kHz mono WAV
4. Decode with `orbcomm_decoder.py`

Or use the full chain:
```bash
# Capture with RTL-SDR (requires rtl_fm)
rtl_fm -f 137.5M -s 48k -g 30 - | sox -t raw -r 48k -e signed -b 16 -c 1 - orbcomm_recording.wav

# Decode
python ../orbcomm_decoder.py orbcomm_recording.wav
```

## Common Orbcomm Frequencies

```
137.2500 MHz
137.3125 MHz
137.3750 MHz
137.4375 MHz
137.5000 MHz
137.5625 MHz
137.6250 MHz
137.6875 MHz
137.7500 MHz
137.8125 MHz
137.8750 MHz
137.9375 MHz
```
