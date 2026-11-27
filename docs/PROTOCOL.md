# TWIST v1 — frame specification

TWIST keeps everything of OPERA (EA5HVK) except the coding chain:
same on/off keying (OOK), same Manchester chips, same chip duration
(0.128 s, Op05), asynchronous frames. Only the content of the frame
and its error-correcting code change: a real CRC-16 computed on the
bits, and an LDPC code over the whole message instead of OPERA's
seventeen independent [7,3] groups.

Two frame types:

| Type | Content | Message | + CRC | Coded bits | Chips | Duration (Op05) |
|---|---|---|---|---|---|---|
| **TWS** (short) | callsign | 28 bits | 44 bits | 119 | 239 | 30.6 s |
| **TWL** (long) | callsign + Maidenhead locator | 53 bits | 69 bits | 160 | 321 | 41.1 s |

`twistenc` (in this directory) prints the chips of any frame; a beacon
implementation must reproduce them exactly.

## 1. Callsign — 28 bits

Exactly OPERA's packing (`core/twcenc.c`: `tw_normalize_call`,
`tw_pack_call`):

1. Upper case. If the third character is not a digit, prepend a space
   ("F5OEO" → " F5OEO"); pad to 6 characters with spaces. Format:
   `[ A-Z0-9][A-Z0-9][0-9][ A-Z][ A-Z][ A-Z]`.
2. Character codes: space = 0, A..Z = 1..26, 0..9 = 27..36.
3. `v = c0`; `v = v*36 + (c1 - 1)`; `v = v*10 + (c2 - 27)`;
   `v = v*27 + c3`; `v = v*27 + c4`; `v = v*27 + c5`.

(Portable suffixes such as /P cannot be encoded, as in OPERA.)

## 2. Locator — 25 bits (TWL only)

Maidenhead, 4 or 6 characters: field `A-R A-R`, square `0-9 0-9`,
optional subsquare `A-X A-X` (case-insensitive).

    v = ((((F1*18 + F2)*10 + S1)*10 + S2) * 577) + sub
    F1, F2 = field letters - 'A' (0..17); S1, S2 = digits (0..9)
    sub = 0 for a 4-character locator,
          1 + (X1*24 + X2) for 6 characters (X1, X2 = subsquare letters - 'A', 0..23)

Range 0 .. 18 694 799 (< 2^25). A received value outside this range is
invalid.

## 3. Message bits and CRC-16

Bits are written MSB first:

- TWS: `m = callsign(28)` — 28 bits
- TWL: `m = callsign(28) | locator(25)` — 53 bits

CRC-16, polynomial 0x1021 (CCITT), bitwise, MSB first, no reflection,
no final XOR, computed over the message bits only:

    crc = INIT
    for each message bit b:
        fb = (crc >> 15) ^ b
        crc = (crc << 1) & 0xFFFF
        if fb: crc ^= 0x1021

INIT depends on the type, so that a frame can never check as the other
type: **TWS: 0xFFFF, TWL: 0x1D0F**.

Vector `v = m | crc(16)`: k = 44 bits (TWS) or 69 bits (TWL).

## 4. Scrambling

`v` is XORed bit by bit (v[0] first) with a type-specific sequence from
a 20-bit LFSR:

    s = 0x5A5A1 (TWS) or 0x3C3C7 (TWL)
    for i in 0 .. k-1:
        out[i] = s & 1
        b = (s ^ (s >> 2) ^ (s >> 3) ^ (s >> 5)) & 1
        s = (s >> 1) | (b << 19)

    v[i] ^= out[i]

(Avoids the all-zero codeword and decorrelates the two frame types.)

## 5. LDPC encoding

Codes: TWS (119, 44), TWL (160, 69); column weight 3, built by
progressive edge growth, then frozen in `core/twist_tables.h` — **the
tables define the protocol**.

Codeword `c` (n bits) = XOR of the generator rows `G[i]` for every
`v[i] = 1`. Row i is stored as three 64-bit words; codeword bit j is
bit `(j % 64)` of word `j / 64`. The code is systematic: `c[PIVOT[i]]
= v[i]`.

## 6. Chips

    chip 0          = 1 (carrier ON: start chip, as in OPERA)
    for i in 0 .. n-1:
        c[i] = 0  ->  chips 1+2i, 2+2i = ON, OFF
        c[i] = 1  ->  chips 1+2i, 2+2i = OFF, ON

2n + 1 chips: 239 (TWS), 321 (TWL). One chip = 0.128 s (Op05); the
carrier is ON for exactly n + 1 chips. No time synchronisation: a
frame may start at any time (as OPERA); repeat it as the beacon cycle
allows.

## Receiver (twistd)

Same receiver as twistd (spectrogram of one-chip windows, blind search
over time and frequency, refinement of start/frequency/drift, ±3-bit
alignment shifts), with the frame geometry of each type and a
code-independent search metric; then non-coherent soft values (exact
log-likelihood ratio of the two chip energies), belief propagation,
and if it fails ordered-statistics decoding (order 2); CRC-16, callsign
and locator validity, ITU-structure plausibility, and the pattern
consistency check of twistd 1.3.1 (the OFF chips must hold only noise).

Measured (`tests/tw_sweep.c`, white noise, carrier-on SNR in 2500 Hz,
50 % of frames decoded): OPERA -21.4 dB, **TWS -22.2 dB**, **TWL
-21.4 dB** (see README.md).
