# Orbcomm Signal Decoding Protocol

Based on reverse engineering of OrbcommPlotter.exe (COAA, MFC Windows application).

## Overview

OrbcommPlotter receives and decodes Orbcomm satellite VHF downlink signals via a PC sound card.
The encoding uses SDPSK (Symmetric Differential Phase Shift Keying) at 4800 bps, received as 
baseband audio at 48000 Hz sample rate.

## Signal Processing Chain

```
Audio Input (48 kHz, 16-bit mono)
    └─> BPSK Integrate-and-Dump Demodulator (10 samples/bit)
        └─> Bit Synchronizer (correlation-based sync detection)
            └─> Packet Framer (24-byte packets)
                └─> Fletcher Checksum Verification
                    └─> Packet Type Dispatch
                        └─> Field Extraction & Display
```

## Physical Layer

### Sample Rate & Bit Rate
- Sample rate: 48000 Hz (standard PC sound card rate)
- Bit rate: 4800 bps
- Samples per bit: 48000 / 4800 = **10 samples/bit**
- Audio buffer: 48000 samples (1 second circular buffer)

### Modulation: SDPSK (Symmetric Differential Phase Shift Keying)

The Orbcomm downlink uses SDPSK where each bit period (10 samples) is divided into two 5-sample halves:
- **Bit 1**: first half positive (+A), second half negative (-A)
- **Bit 0**: first half negative (-A), second half positive (+A)

The demodulator uses a matched filter with a 10-sample correlation pattern:

```
Pattern: [-1, -1, +1, +1, +1, +1, +1, -1, -1, -1]
         [ SUB  SUB  ADD  ADD  ADD  ADD  ADD  SUB  SUB  SUB ]
```

For a bit 1 signal [+A,+A,+A,+A,+A, -A,-A,-A,-A,-A]:  correlation = +2A (> 0)
For a bit 0 signal [-A,-A,-A,-A,-A, +A,+A,+A,+A,+A]:  correlation = -2A (< 0)

The demodulator runs in three phases with different sample offsets to achieve bit
synchronization:
1. Phase 1: Skip 0x50 (80) samples through 8 iterations → 8 bits for initial sync
2. Phase 2: Skip 0x50 samples through 8 iterations → 8 more bits
3. Phase 3: Skip 0x3c0 (960) samples through 12 iterations → 12 more bits

The combined result is 24 bytes (0x18 = 192 bits).

## Packet Structure

Each Orbcomm packet is **24 bytes**:

| Byte   | Size  | Field           |
|--------|-------|-----------------|
| 0      | 1     | Message Type    |
| 1-21   | 21    | Data Payload    |
| 22-23  | 2     | Fletcher Checksum|

### Checksum Algorithm (Fletcher-8 variant)

The checksum is a Fletcher-8 variant computed over all 24 bytes. The last 2 bytes (positions 22-23)
are the checksum bytes (C1, C2) such that:

```
sum1 = 0
sum2 = 0
for each byte in packet[0..23]:
    sum1 = (sum1 + byte) mod 256
    sum2 = (sum2 + sum1) mod 256

Valid packet: BOTH sum1 == 0 AND sum2 == 0
```

The checksum bytes are computed from the 22-byte body:
```
C1 = (-sum2_body - sum1_body) mod 256
C2 = sum2_body
```

where `sum1_body` and `sum2_body` are the accumulator values after processing bytes 0-21.

## Message Types

| Type | Hex  | Description            | Format String                                               |
|------|------|------------------------|-------------------------------------------------------------|
| 0x1a | 26   | Message (segmented)    | "Message (pt. %d of %d) %05X %05X %05X %05X"               |
| 0x1b | 27   | Uplink channels        | "Uplink channels " + list of "%8.4lfMHz"                   |
| 0x1c | 28   | Downlink channels      | "Downlink channels (pt %d of %d) " + 5 channel values      |
| 0x1d | 29   | Network Control        | "Network Control %02X%02X%02X%02X%02X%02X%02X%02X%02X"    |
| 0x1e | 30   | Fill Packet            | "Fill packet %02X%02X%02X%02X%02X%02X%02X%02X%02X"        |
| 0x1f | 31   | Unknown (raw log)      | 12 hex bytes                                                |
| 0x22 | 34   | Ephemeris              | "Ephemeris FM-%02d wk. %d, %s, %02d:%02d:%02d pos. %lf %lf" |
| 0x65 | 101  | Network Announcement   | "Spacecraft FM-%02d %8.4lfMHz frame %02d"                  |

### Sync Detection
The application looks for these byte values at position 0 of a candidate packet:
0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f, 0x22, 0x65

For raw logging, the byte sequence `65 A8 F9` at the start indicates a valid raw frame.

## Detailed Packet Field Layout

### Type 0x65 - Network Announcement (Gateway Information)
```
Byte  0: 0x65       (message type)
Byte  1: 0xA8       (sync byte 2)  
Byte  2: 0xF9       (sync byte 3)
Byte  3:            (satellite ID, 1-36)
Byte  4:            (frequency field - if <= 0x40, add 0x100)
                    freq = (value * 0.0025) + 137.0 MHz  (approximation)
Byte  5:            (frame number, upper nibble is frame)
Bytes 6-23:         (additional data)
```
Displayed as: `"Spacecraft FM-{sat_id} {freq}MHz frame {frame}"`

### Type 0x22 - Ephemeris
```
Byte  0: 0x22       (message type)
Byte  1:            (satellite ID, bits [3:0] - must match byte 2 bits [3:0])
Byte  2:            (satellite ID, bits [7:4] = satellite ID upper)
Bytes 3-5:          (packed timestamp: week number, day, time)
Bytes 6-7:          (longitude: 20-bit signed, subtract 0x80000, shift left 4)
Bytes 8-9:          (latitude: 20-bit signed, subtract 0x80000, shift left 4)
Bytes 10-11:        (altitude: 20-bit signed)
```
Position computation:
```
x = longitude_int  (after sign-correction)
y = latitude_int   (after sign-correction)
z = altitude_int   (after sign-correction)

range = sqrt(x² + y²)
azimuth = atan2(y * factor, range * factor) * 180/pi
elevation = atan2(z * factor, sqrt(x² + y²) * factor) * 180/pi
```

Displayed as: `"Ephemeris FM-{sat_id} wk. {week}, {day}, {h}:{m}:{s} pos. {lon} {lat}"`

### Type 0x1a - Message (Segmented)
```
Byte  0: 0x1a       (message type)
Byte  1:            (total parts)
Byte  2:            (part number, nibbles)
Bytes 3-6:          (4 data fields as hex: %05X %05X %05X %05X)
```

### Type 0x1c - Downlink Channels
```
Byte  0: 0x1c       (message type)
Byte  1:            (total parts)
Byte  2:            (part number, nibbles)
Bytes 3-22:         (5 channel values, each displayed as frequency)
```

### Type 0x1d - Network Control
```
Byte  0: 0x1d       (message type)
Bytes 1-9:          (9 hex bytes, displayed as %02X%02X...)
```

### Type 0x1e - Fill Packet
```
Byte  0: 0x1e       (message type)  
Bytes 1-9:          (9 hex bytes, displayed as %02X%02X...)
```

## Time Encoding

The timestamp is decoded from packed bytes using arithmetic division:
```
minutes = value / 60
seconds = value % 60
hours = minutes / 60
days = ... etc.
```

Constants used (via multiplication):
- `/30`  = multiply by 0xC22E4507, shift right 16 + sign adjust
- `/60`  = multiply by 0x88888889, shift right 5 + sign adjust
- `/3600` = multiply by 0x91A2B3C5, shift right 11 + sign adjust

## Receiver Configuration

The application supports:
- Sound card input via waveIn API
- Selectable soundcard (Options->Audio)
- Signal polarity inversion (`[ESI + 0x2f894]`)
- View options for different packet types (toggles at offsets in class object)

## Data Logging

Two log formats:
- **Decoded log**: `orbcomm{date}.log` - human-readable decoded packets
- **Raw log**: `orbcomm_raw{date}.log` - raw hex bytes, filtered to frames starting with `65 A8 F9`

Log entries prefixed with `[{y}-{m}-{d} {H}:{M}:{S}]` timestamp.

## Memory Layout (COrbcommPlotterView)

The main view class occupies ~2MB (0x1FC454 bytes):
```
+0x44..0x47:     start/stop flags
+0x50..0x1FA4:   satellite orbital data (globe.dat, 8101 bytes)
+0x58:           soundcard option
+0x90:           circular buffer position (sample index)
+0x94..:         circular audio sample buffer (48000 * 4 = 192KB)
+0x2ee9c:        log directory path
+0x2eea4:        sample countdown timer (0x77f = 1919 = ~40ms after packet)
+0x2eed8:        view state
+0x2eedc:        log flag
+0x2eee0:        raw flag
+0x2eee4..:      decoded message history ring buffer (100 entries)
+0x2f074:        ring buffer write index (0-99)
+0x2f078..:      home latitude (double)
+0x2f080..:      home longitude (double)
+0x2f090..:      spacecraft frequency array (36 entries, doubles)
+0x2f098..:      spacecraft azimuth array
+0x2f890:        last packet timestamp
+0x2f894:        signal polarity flag
+0x2f898:        track history flag
+0x2f89c..:      message type display flags (for types 0x1a-0x22, 0x65)
+0x2f8b0:        ephemeris display flag
+0x2f8b8:        network announcement display flag
+0x2f8c4:        last satellite number
+0x1c4054..:     large initialized buffer (~230KB)
```
