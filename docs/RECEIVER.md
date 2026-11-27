# TWIST receiver — design notes

This document describes the receiver shared by all frame types
(`core/`), its options and its measured performance. It started as the
notes of the earlier classic-frame decoder (versions 1.0–1.3 of that
decoder are summarized in the history below); the tools are now
`twistd` (TWIST S and TWIST L) and `operad` (the classic frame), one
source (`cli/twistd.c`) built with each library's pipeline, and `pi4d`
(PI4). The libraries of TWIST Console are described in
[`LIBTWIST.md`](LIBTWIST.md).

Same command-line conventions as `pi4d`: read a `.wav` file, print the
callsign(s) found, one line per spot, nothing else. No dependency other
than FFTW3. Linux (gcc, `-Wall -Wextra` clean, Makefile and CMake) and
Windows (MSYS2 UCRT64 gcc, built by the CI) — see
[`WINDOWS.md`](WINDOWS.md) for Visual Studio.

## Building

```
sudo apt install libfftw3-dev
make
sudo make install          # /usr/local/bin/twistd, operad, pi4d
make test                  # self-checking suite (tests/run_tests.sh)
make tests                 # builds every test program in tests/
```

`make install PREFIX=/usr`, `make install DESTDIR=... PREFIX=...` and
`make uninstall` work as for pi4d. Windows / Visual Studio 2022: see
[`WINDOWS.md`](WINDOWS.md) (CMake + vcpkg, `vcpkg install fftw3`).

## Usage

```
twistd [options] file.wav       TWIST S, TWIST L
operad [options] file.wav       the classic frame
```

The options below are those of both, except: `-p` (twistd: `tws`,
`twl`, `all`), `-G` (twistd: OSD margin); `-m` (sub-modes OpN), `-c`
with the deep search, `-N`, `-F`, `-L`, `-M` (operad). twistd keeps
`-c` and `-k` (report only listed callsigns).

| Option | Role | Default |
|---|---|---|
| `-m MODE` | Sub-mode: `05` (or `0.5`), `1`, `2`, `4`, `8`, `16`, `32` | `05` |
| `-f FREQ` | Expected carrier frequency (Hz). With `-i`: relative to the I/Q center | 1500 (0 with `-i`) |
| `-t TOL` | Half-width of the frequency search (Hz) | 200 |
| `-d [MIN:]MAX` | Window for the frame **start** (s from the start of the file); `-d 59:` = from 59 s on | anywhere |
| `-c CALLS` | Known callsigns (comma list, or a file) — enables the **deep search** | none |
| `-k` | Report only callsigns of the `-c` list | off |
| `-N` | No deep search (use `-c` for `-k` only) | — |
| `-F FA` | Deep search: accepted false alarms per run | 1e-4 |
| `-L N` | List-decoder depth (frames checked per candidate) | 32 |
| `-M MARGIN` | Minimum lead over the frame's CRC twins (noise units, 0 = off) | 10 |
| `-G MARGIN` | TWIST: minimum lead of a frame found by OSD over the next nearest codeword (LLR units, 0 = off) | 2 |
| `-O LIMIT` | Pattern consistency: refuse blind decodes whose OFF chips hold another signal (0 = off) | 0.6 |
| `-P` | Keep callsigns failing the ITU-structure plausibility check | off |
| `-r DRIFT` | Largest drift searched (Hz/min) | ≈ ±1/T per frame |
| `-C` | SNR as carrier-on power instead of frame-average power | off |
| `-w WIDTH` | Spread mode: chip energy summed over WIDTH Hz (rain/aircraft scatter) | 0 (off) |
| `-T HHMMSS` | UTC timestamp to display | system time |
| `-i` | Input is a stereo I/Q capture (RIFF or RF64, left = I, right = Q) | off |
| `-Z SIGMA` | Wideband scan (`-t` > 1000 Hz): pre-detection threshold | 6 |
| `-S N` | Wideband scan: at most N frequencies decoded | 40 |
| `-D` | Diagnostics on stderr | off |
| `-v` / `-V` / `-h` | Build string / version / help | — |

The input can be at **any sample rate** (8 kHz, 12 kHz, 48 kHz...),
8/16/24/32-bit PCM, mono or multi-channel (averaged). Options may be
placed before or after the file name, glued (`-f1500`) or not
(`-f 1500`) — same parser as pi4d.

### Sub-modes

One chip lasts 0.256 × N seconds for OpN; a frame is always 239
chips. Each doubling of N gives 3 dB of sensitivity and doubles the
frame length:

| Mode | Chip | Frame | Usual bands |
|---|---|---|---|
| Op05 | 0.128 s | 30.6 s | 2 m and above |
| Op1 | 0.256 s | 61.2 s | 15 m – 4 m |
| Op2 | 0.512 s | 2 min 02 | 80 m – 17 m |
| Op4 | 1.024 s | 4 min 05 | 160 m |
| Op8 | 2.048 s | 8 min 09 | 630 m |
| Op16 | 4.096 s | 16 min 19 | 2200 m |
| Op32 | 8.192 s | 32 min 38 | — |

The recording must hold the frame (see "Truncated frames" below for
the tolerance). OPERA is **not time-synchronous**: by default the
whole file is searched for a frame start.

## Output format

Same as pi4d (jt9-inspired), one line per spot, sorted by DT:

```
HHMMSS SNR   DT   DF : CALLSIGN
000000 -14  1.8 1564 : F5ZZO
```

- `SNR`: dB in 2500 Hz, **average power over the frame** — the
  convention of the official OPERA software, so that spots compare
  directly with it (checked on the ED5YAE recording: both say -20 dB).
  OPERA keys the carrier during 120 of its 239 chips, so this is the
  carrier-on power minus 3.0 dB. `-C` gives the carrier-on power
  instead (the convention of D. Estévez's measurements and of the
  sensitivity tables below). Estimated from the decoded frame
  (calibrated: bias < 0.3 dB down to the decoding threshold, standard
  deviation about 0.5 dB).
- `DT`: start of the frame (its first chip), in seconds from the start
  of the file. Can be negative if the beginning of the frame was cut
  off (see below).
- `DF`: carrier frequency in Hz (with `-i`: relative to the I/Q
  center, printed `%+8.1f` as in pi4d).
- `CALLSIGN`: trimmed. A trailing **` *`** marks a spot found by the
  **deep search** (a known callsign's pattern detected — see below)
  rather than decoded blindly.

Nothing on stdout when nothing is found; one line on stderr and exit
status 1 — the normal outcome when no OPERA signal is present, not a
fault. `-D` adds all the diagnostics on stderr (candidates, refined
time/frequency/drift, list-decoder rank, frames rejected by the twin
check or as ambiguous, deep-search threshold and hits).

Examples:

```
$ twistd beacon.wav
104500 -14  1.8 1564 : F5ZZO

$ twistd -m 2 -f 800 -t 100 hf.wav           # Op2, carrier near 800 Hz

$ twistd -c F5ZZO,F6FBB beacon.wav            # + deep search for 2 calls
104500 -24  1.8 1564 : F5ZZO *

$ twistd -c F5ZZO -k -f 1564 -t 10 -d 1:3 beacon.wav   # known beacon,
                                              # known timing: max sensitivity
$ twistd -i -t 20000 capture_iq.wav           # +/- 20 kHz of I/Q
```

## The OPERA protocol (as implemented)

The official OPERA software is closed source. The coding chain used
here is the one published by **F4GCB** (Patrick, JUMA-TX500/136
controller) and **7L1RLL** (Rick Wakatori), both "derived from the
work of EA5HVK", distributed with **rpitx** by F5OEO
(`src/opera/opera.cpp`, `src/opera/decode_opera.cpp`):

```
callsign  ->  28 bits (6 characters: [ A-Z0-9][A-Z0-9][0-9][ A-Z]x3)
          ->  + CRC16 over those bits (16)  + 3-bit CRC over the 44  = 47
          ->  "0000" + 47 bits                                        = 51
          ->  XOR fixed pseudo-random sequence                        = 51
          ->  17 groups of 3 bits, each -> 7-bit codeword ([7,3]
              simplex code, "Walsh matrix")                           = 119
          ->  7 x 17 block interleaver                                = 119
          ->  Manchester (0 -> on,off ; 1 -> off,on), after one
              leading "on" chip                                       = 239 chips
```

`core/twcenc.c` reproduces this **bit-exact** against the three complete
reference transmissions of those sources (AA1AA, F5OEO, 7L1RLL —
`tests/test_encoder.c`), including a peculiarity: the CRC is computed
over the ASCII characters `'0'`/`'1'`, not over the bits. Callsign
format limits come from the protocol: 6 characters at most, the digit
in 3rd position (a one-letter prefix like `F1HDI` is padded to
` F1HDI`), no digit in the suffix, no `/P`.

## How the decoder works

1. **Front end** (`core/twbb.c`) — one FFT of the whole recording, keep
   only the search band, inverse FFT: a narrow complex baseband signal
   at 125·2^k Hz, chosen so that a chip is an exact power-of-two
   number of samples. Works identically for real SSB audio and I/Q;
   any input sample rate; exact time alignment (no resampling step).
2. **Spectrogram** (`core/twsync.c`) — one DFT per chip-long window
   (the matched filter of one chip), every 1/16 chip, bins of 1/(4T).
   Each bin is normalized by a robust noise estimate (median in time,
   then running median across frequency, so that a beacon "on" half
   of the time does not raise its own noise reference).
3. **Search** — OPERA has no separate sync sequence. For every start
   time and frequency, the score is the soft-decision likelihood of
   the best possible frame (each of the 17 groups takes its most
   likely codeword; group 0 is fully known, group 1 half known). Peaks
   more than 7 robust sigmas above the map median become candidates.
   Detection is **non-coherent** (energy per chip): carrier phase,
   phase noise and slow frequency wander do not matter.
4. **Refinement** — exact chip energies around each candidate:
   sample-accurate start, 1/16-bin frequency, linear drift.
5. **Bit-shift ambiguity** — a peculiarity of OPERA: shifting the
   frame by one Manchester bit (2 chips) leaves every group holding a
   valid codeword (its neighbour's), so the alignments ±2, ±4, ±6
   chips are almost as likely as the true one. They are all evaluated
   and the CRC decides.
6. **Decoding** (`core/twcdec.c`) — soft-decision maximum likelihood per
   group, then a **list decoder**: the most likely frames over all
   explored alignments (best one, then one, two or three groups swapped
   to their runner-up codeword), ranked by likelihood, checked against
   the 19 CRC bits, up to `-L` frames (default 32). Near the threshold
   most successful decodes come from this list, not from the best frame
   (it is worth about 1 dB).
7. **SNR** from the re-encoded frame: mean energy of the ON chips vs
   the OFF chips.

Truncated frames: a frame whose beginning or end lies outside the
file (up to 20 % of it) is still searched, the missing chips being
treated as erasures — useful when a recording window cuts into a
transmission. Several frames in one file — the same beacon repeating,
or different stations at different frequencies — are all reported.

## PI4 frames (libpi4, pi4d)

PI4 (PharusIgnis4, specified by Bo Hansen, OZ2M) is 4-FSK: 146 symbols
of 1/6 s (24.33 s), tones 234.375 Hz apart, a 42-bit message (8
characters) with the K=32, r=1/2 convolutional code of JT4, decoded by
Fano's sequential decoder. libpi4 uses the decoder of pi4d (same
author: `core/pi4sync.c` coherent symbol-by-symbol sync, `pi4demod.c`
symbol metrics, `pi4snr.c`, `pi4dec.c` deinterleaving and Fano;
`fano.c`/`tab.c` from WSJT-X, unmodified), called by `core/pi4pipe.c`.
The command-line `pi4d` (`cli/pi4d.c`) is pi4d itself, unchanged in its
options and output (with its I/Q scan, `core/pi4iqscan.c` and
`pi4iqmix.c`); TWIST Console uses libpi4:

- **Own zone**: the "carrier" (OZ2M convention: half a tone spacing above
  tone 0), `pi4_f_center` ± `pi4_tol` of the settings (pi4d: `-f`, `-t`),
  default 800 ± 150 Hz - the tones span about
  700 Hz, they do not fit the OOK zone.
- **Windows**: frames whose start and end fit the start and end windows,
  as the other protocols - each frame is found once by the short passes
  of the application, just after it ends (`tests/pi4_test`). In a long
  file, the strongest frame of the window is found, then the parts
  before and after it are searched again.
- **Messages**: Fano says nothing of a wrong message, so a message is
  kept only if it is a plausible callsign (the base callsign of
  `F5ZZO/P`, `EA8/F5ZZ` passing `tw_call_plausible`) or "/" and a
  locator (`/JN12AX`, the message beacons alternate with their
  callsign: the spot then has no callsign, its `loc` is set).
- **Not searched by default** (`-p all` is the three OOK frames): add it,
  `-p all,pi4`. Cost: about 0.15 s per short pass, 50 to 60 MB of
  memory while it runs.

Measured (`tests/pi4_test sweep 40`, white noise, frame start 1.0 to
2.5 s, carrier 700 to 884 Hz): 10 % at -25 dB, 28 % at -24 dB, 80 % at
-23 dB, 100 % from -22 dB; no false decode on 1500 short passes of noise
(`tests/pi4_test noise 1500`). Real recording: `tests/real/pi4_f5zzo.wav`
(F5ZZO, SNR -11 dB).

## Sensitivity (measured)

Monte-Carlo, full pipeline (search included), white Gaussian noise,
random start time, frequency and phase, 5 ms keying edges, default
options. "50 %" = the SNR at which half of the frames are decoded.

**All SNRs in this section (and in `CHANGELOG.md`) are carrier-on
powers** (`-C` convention, as in D. Estévez's measurements quoted
below). In the default output convention (average power, official
OPERA software), subtract 3 dB: e.g. the Op05 blind threshold of
-21.4 dB reads about -24.4 dB there.

| Mode | 100 % down to | 50 % | Notes |
|---|---|---|---|
| Op05, blind | -20 dB (≥ 95 %) | **-21.4 dB** | 300-600 trials per point |
| Op05, deep search, whole file and ±200 Hz | -22 dB | **-24 dB** | 60 trials per point |
| Op05, deep search, start known ±0.5 s, freq. ±8 Hz | -24 dB | **-25.5 dB** | 60 trials per point |
| Op1, blind | -22 dB | -24.3 dB | 20 trials per point |
| OpN, blind | | ≈ -21.4 - 3·log2(2N) dB | +3 dB per doubling |

For comparison, D. Estévez measured the official OPERA software at
about -19 dB (Op05), -25 dB (Op2) and -29 dB (Op4) in the same
reference bandwidth. `twistd` in blind mode is therefore about 2 dB
more sensitive on Op05; the deep search adds 2.5 to 4 dB more.

On the real F5ZZO recording (-14 dB as received), with white noise
added: blind decoding still succeeds at -17/-18 dB (3 noise draws out
of 3), fails at about -21/-22 dB, where the deep search still found it
in 1 draw out of 3 (at -23 dB estimated) — consistent with the table.

## False decodes — and why OPERA needs care there

OPERA's 19 CRC bits suggest about one chance in 500 000 for a wrong
frame to pass. That holds for random frames (pure noise: **no false
decode in 1500 noise-only files**, with the deep search enabled for
10 callsigns). But the code structure makes it **much weaker against
the errors that actually occur near the threshold**: a frame with 2,
3 or 4 wrong groups passes the CRCs far more often — e.g. changing
groups 1 and 6 together in one precise way always gives another
frame with valid CRCs. `tests/gen_twins.c` finds all such "CRC
twins" (153 patterns of 2 to 4 groups, `core/twctwins.h`). Without
precautions, about 1 decode in 120 near the threshold was false.

Four safeguards, all on by default:

1. **Twin check** (`-M`): a decoded frame must beat each of its valid
   CRC twins by a soft-metric margin (10 noise units by default). A
   true frame beats its twins easily (they differ by at least 8 coded
   bits); a false frame usually does not, since the true one is among
   its twins. Divides false decodes by about 3 (full pipeline) to 7
   (decoder alone) for a 2-4 % loss of decodes at the threshold.
2. **Ambiguity rejection**: if two different callsigns pass within the
   same list, the candidate is dropped.
3. **Plausibility** (`-P` to disable): the callsign must look like an
   amateur callsign (non-empty suffix, allocated ITU prefix block: no
   Q, no 0/1 except 1A, only B F G I K M N R W as one-letter prefixes,
   only allocated letter+digit prefixes such as A6, E7, T7...). It
   removes about half of what remains.
4. **Pattern consistency** (`-O`, since 1.3.1): the OFF chips of the
   decoded callsign's pattern must hold only noise. On a real frame
   their energy exceeds the noise by at most 0.26 noise units near the
   threshold (0.21 on the real F5ZZO recording, ~0 on strong
   synthetic signals). When the decoder works on the energy of
   *another* signal - a CW beacon's carrier, a birdie, a PI4 tone, all
   of which the wideband scan sends to the decoder because they are
   keyed - a frame that happens to pass the CRCs has its OFF chips
   full of that signal (measured 2 to 160 noise units) and an absurd
   SNR (typically -28 to -32 dB). Refused when the excess is above
   `LIMIT + 0.05 x contrast` (LIMIT = 0.6 by default; 0 disables).
   Only blind decodes are checked: the deep search's matched filter is
   already insensitive to other signals.

Measured with these (`../CHANGELOG.md`): 2 false decodes for 906 good
ones in 3000 frames between -20.5 and -23 dB, i.e. roughly **one
false decode per 400-500 decodes of signals at the very limit** — and
none on strong signals or on noise (0 in 1500 noise-only files). The
official software deals with the same weakness by checking decodes
against a callsign database; here, `-c LIST -k` does that, and with
it a false blind decode would have to be one of the listed callsigns.

To be even more conservative, `-L 1` (no list decoding) or a larger
`-M`; to squeeze the last fraction of a dB at the cost of more false
decodes, `-L 64` or `-M 0`.

### TWIST frames: the OSD margin (`-G`, since 1.6.2)

TWIST frames are decoded by belief propagation, and when it fails by
ordered-statistics decoding (OSD, order 2), which gains about 1 dB at
the threshold. But OSD always returns a codeword - the nearest one to
what was received - and then only the CRC-16 stands between noise and
a spot: one random codeword in 65 536 passes it, the 28 bits of a
callsign almost always unpack to a valid one, half of those look
plausible. Pure white noise never gave candidates above the search
threshold, so the noise-only tests counted no false decode; on a real
band (other signals, interference, AGC) many candidates reach the
decoder, every 3 s, and testers saw far more ghost TWIST spots than
OPERA ones - OPERA's twin margin (above) does the job that TWIST
lacked.

Belief propagation never converged on noise (0 in 10 227 attempts);
all the ghosts come from OSD. On noise, the nearest codeword barely
leads the next nearest one (median 0.2 to 0.4 in LLR units), a real
frame found by OSD usually by much more (median 5 to 8). Requiring a
lead of 2 (`-G 2`, the default) keeps 1.1 % of the OSD results on
noise for TWIST S (90 times fewer ghosts) and 0.01 % for TWIST L, and
71 to 82 % of the frames OSD found (`tests/tw_sweep`, 100 frames per
point: 50 % decoded at -22.0 dB for TWIST S, -21.4 dB for TWIST L,
about 0.1 dB less than without it).

It does not help against another keyed signal in the window (a frame
of the other protocol, strong): there the OSD results stand out from
their neighbours too; the pattern consistency check (`-O`) and the
arbitration between protocols handle those.

### Wideband scan — finding beacons without knowing their frequency

With a search half-width above 1000 Hz — typically an I/Q capture of a
whole beacon band, `twistd -i -t 50000 band.wav` for ±50 kHz — the
full decoder would have to try every start time at every one of tens
of thousands of frequencies. Instead, two stages:

1. **Pre-detection** (`core/twscan.c`), over the whole band: a short-time
   spectrum with one-chip windows, and in each 1/(2T)-wide bin a "jump"
   statistic — the mean change of energy from one chip to the next over
   one frame length. Manchester coding guarantees a transition in every
   pair of chips, so a keyed OPERA carrier stands out; noise gives about
   1; a steady carrier (birdie) is rejected by a keying index (mean jump
   / mean energy, well below 1 for a steady carrier). Values become
   robust z-scores across the band; detections above `-Z` (6) are kept,
   at most `-S` (40).
2. **Decoding**: the normal decoder, deep search included, on a ±20 Hz
   tile around each detection. One forward FFT of the capture serves
   all tiles.

The pre-detection is as sensitive as the decoder (measured: 50 % of
the frames decoded at -21.3 dB carrier-on over ±42 kHz, against -21.4
dB for the narrowband decoder — `tests/wide_sweep.sh`), and fast: about
5 s for a 40 s capture at 128 kS/s on a PC. Memory: about 500 MB for a
57 s capture at 128 kS/s. Other on/off keyed signals (CW identifiers of
beacons...) are detected too and simply rejected by the decoder.

### Spread mode (`-w`) — rain scatter, aircraft scatter

At 10 GHz and above, rain scatter smears the carrier over tens of Hz
with fast fading. The normal detector measures each chip in a filter
about 1/T wide (8 Hz in Op05) and loses most of such a signal. With
`-w WIDTH`, the chip energy is summed over WIDTH Hz instead (in the
spectrogram, and identically in the refinement and deep search). The
sum's noise statistics are measured on the file and rescaled so that
every threshold keeps its calibration; the SNR estimate is corrected.

Measured on simulated rain scatter (Rayleigh fading, Gaussian Doppler
spectrum): about **+4 dB** at 10 Hz RMS spread (`-w 20` to `-w 40`),
+1.5 dB at 3 Hz RMS (`-w 20`), about **-1 dB** on a clean carrier —
so leave it off for normal propagation. Choose WIDTH close to the
visible width of the smeared carrier on a waterfall. See
`CHANGELOG.md` (1.1.0) for the table. Costs more CPU (the refinement
sums several frequencies): about 1 s per Op05 file instead of 0.3 s.

### Deep search (`-c`)

When the callsigns that may be on the air are known (beacons being
monitored), each one gives an exact 239-chip pattern, and the
receiver only has to decide whether it is present: a matched filter
over the whole frame, `Z = Σ s·d / sqrt(Σ d²)` (`core/twcdeep.c`). Z is
self-normalized — with noise only, *or with any other signal however
strong*, it behaves as a standard normal variable — so the detection
threshold follows directly from the number of hypotheses tested
(time × frequency cells × callsigns) and the accepted false-alarm rate
(`-F`, default 1e-4 per run). The calibration of the effective number
of hypotheses was measured on noise (`tests/deep_calib.c`).

Consequence worth knowing: **the narrower the search, the more
sensitive**. For a beacon whose frequency and timing are known (a
GPS-locked beacon starting at a known second), `-f`/`-t`/`-d` around
them lower the threshold (Z ≈ 5.4 instead of 6.2) — about 1.5 dB.

Deep-search spots are marked ` *`, are only ever one of the listed
callsigns, and are only reported where no blind decode was made.

**Two similar callsigns next to a strong signal** (since 1.6.3). "Any
other signal" above is true for noise-like ones, not for a weak copy
of a known pattern: a strong frame leaks into the frequencies around
it (the chip window's sidelobes, about -20 log10(π·df·T) dB at df Hz,
and its keying edges), and the matched filter of a listed callsign
that shares most chips with it (F5ZAS for F5ZAL) found that copy some
tens of Hz away (testers: F5ZAS reported with every strong F5ZAL; in
`tests/deep_shadow`, 16 ghosts in 39 passes). A deep hit is now
dropped as a copy when, next to a spot that overlaps it in time
(within 20 chip bandwidths), it is no more than 3 dB above that
leakage, and that spot's own pattern, with its own timing, explains
the chip energies at the hit's frequency at least a third as well as
the hit's callsign. A real weak station there keeps its own timing and
pattern (ratio about 7, the copies 0.5 to 2.3) and is kept.

## Project layout

| File | Role |
|---|---|
| `core/twdefs.h` | Protocol constants, version |
| `core/twcenc.c/h` | Callsign packing, CRCs, full transmit chain (bit-exact) |
| `core/twbb.c/h` | FFT-domain down-conversion to complex baseband |
| `core/twsync.c/h` | Spectrogram, candidate search, refinement, SNR |
| `core/twcdec.c/h` | Soft decoding, list decoder, twin check, plausibility |
| `core/twctwins.h` | CRC twin patterns (generated by `tests/gen_twins.c`) |
| `core/twcdeep.c/h` | Deep search for known callsigns |
| `core/twscan.c/h` | Wideband pre-detection (scan of a whole band) |
| `core/twpipe.c/h` | The whole pipeline in one call |
| `twistd.c` | Command-line tool |
| `wavread.c/h`, `iqread.c/h` | WAV / I/Q (RIFF, RF64) readers, from pi4d |
| `tests/test_encoder.c` | Bit-exactness against the reference transmissions |
| `tests/gen_test_wav.c`, `tests/synth.c/h` | Synthetic recordings (SSB or I/Q, several stations, drift) |
| `tests/test_e2e.c` | One synthetic frame through the pipeline, verbose |
| `tests/snr_sweep.c` | Sensitivity, SNR bias, false decodes (env. `KNOWN=`, `NARROW=1`) |
| `tests/false_alarm.c` | Pure noise: anything decoded is false |
| `tests/deep_calib.c` | Deep-search threshold calibration |
| `tests/gen_twins.c` | Finds the CRC twin patterns |
| `tests/gen_wide.c`, `tests/wide_sweep.sh` | Synthetic wideband I/Q captures (several beacons, birdies, keyed carriers), wideband sensitivity |
| `tests/spread_sweep.c` | Rain-scatter simulation, with and without `-w` |
| `tests/real/f5zzo.wav` | Real off-air Op05 recording of F5ZZO (8 kHz), decoded by `make test` |

## Known limitations

1. **One sub-mode per run** (`-m`): run `twistd` once per mode if
   several may be present.
2. **No averaging across repetitions** of the same beacon (each frame
   is decoded on its own).
3. **Drift**: linear only, ± about one main-lobe width over the frame
   by default (`-r` to widen). Strongly non-linear drift over a long
   frame (Op8 and above) will cost sensitivity.
4. **Memory/time for slow modes**: the spectrogram covers the whole
   file. Op05 over ±200 Hz: ~0.3 s for 36 s of audio; wideband scan
   of ±50 kHz: ~5 s for 40 s at 128 kS/s. Op32 over ±200 Hz with a
   35-minute file: a few hundred MB; narrow `-t` for slow modes.
5. **Callsigns**: the protocol cannot carry portable suffixes or
   callsigns longer than 6 characters.
6. Validated on one real recording so far: more real captures
   (weak ones especially, and other sub-modes) are the most useful
   next test.

## Licence

`core/twcenc.c` is a port of GPL code (F4GCB / 7L1RLL, distributed with
rpitx under the GPL); the project as a whole is therefore distributed
under the **GNU GPL v3**. Please keep the references to EA5HVK (the
OPERA mode), F4GCB and 7L1RLL (the published coding chain) and F5OEO
(rpitx). `wavread.c`/`iqread.c` come from the pi4d project.

Sources:
[OPERA (EA5HVK)](https://rosmodem.wordpress.com/) ·
[rpitx (F5OEO)](https://github.com/F5OEO/rpitx) ·
[D. Estévez, Testing Opera sensitivity](https://destevez.net/2016/11/testing-opera-sensitivity-with-gnu-radio/) ·
[OPERA on sigidwiki](https://www.sigidwiki.com/wiki/OPERA_Beacon_and_Data)
