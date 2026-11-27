# The libraries of TWIST Console

The engine of TWIST Console is four shared libraries: one with no
protocol, and one per protocol. **The three protocol libraries do not
know each other**: each holds its own code and the common signal
processing compiled in, and none needs another (`make test` checks
their dynamic sections). OPERA and PI4 can therefore be left out at
installation; the application then simply does without them.

| Library | Header | Prefix | Content |
|---|---|---|---|
| `libtwcore` | [`core/twcore.h`](../core/twcore.h) | `twl_` | live audio (PortAudio), transmission, keying detector, arbitration between libraries |
| `libtwist` | [`core/libtwist.h`](../core/libtwist.h) | `twt_` | TWIST S and TWIST L: decoder, encoder |
| `libopera` | [`core/twcl.h`](../core/twcl.h) | `twcl_` | the classic OPERA frame: decoder with the deep search for known callsigns, encoder |
| `libpi4` | [`core/libpi4.h`](../core/libpi4.h) | `pi4l_` | PI4: decoder, encoder |

Their common types are in [`core/twltypes.h`](../core/twltypes.h): one
settings structure, one spot structure, one transmit step structure for
the three protocols. In the code, the classic frame keeps neutral names
(`twcl_`, `TWL_PROTO_CLASSIC`); its usual name is only its display label
(`core/twnames.h`, returned by `twcl_proto_name`) and the file name of
its library.

| Platform | Files | Built by |
|---|---|---|
| Windows (MSYS2 UCRT64) | `libtwcore.dll`, `libtwist.dll`, `libopera.dll`, `libpi4.dll` | `make CC=gcc` |
| Windows (Visual Studio) | the same | CMake |
| Linux | `libtwcore.so`... | `make` or CMake |
| macOS | `libtwcore.dylib`... | `make` or CMake |

On Windows they need `libfftw3-3.dll` and `libfftw3_threads-3.dll` next
to them (and `libportaudio.dll` for `libtwcore`). FFTW's planner, one
for the whole process, is made thread-safe by the first plan of each
library (`fftw_make_planner_thread_safe`, `core/twbb.c`): decoders of
several libraries may run in several threads at once.

C#: [`bindings/csharp/LibTwist.cs`](../bindings/csharp/LibTwist.cs).

## Design

- Plain types only — `int`, `double`, `float` arrays, ANSI strings —
  and fixed-size structures without pointers, so that .NET marshals
  them directly.
- The libraries never allocate memory the caller frees: spots are
  written into the caller's array.
- One opaque handle per decoder (`twt_decoder`, `twcl_decoder`,
  `pi4l_decoder`) holds what persists between passes: the known
  callsigns, a conversion buffer, the last error message.
- Only the functions of the library's prefix are exported.
- Each library has its API version (`TWL_CORE_API_VERSION`,
  `TWT_API_VERSION`, `TWCL_API_VERSION`, `PI4L_API_VERSION`, now 1),
  bumped whenever a structure or a signature changes; the C# side
  checks them. All four give the same version of TWIST Console
  (`twl_version()`, "1.0.0-rc.1").

## API

The three protocol libraries have the same decoder functions, with
their prefix (`twt_`, `twcl_`, `pi4l_`):

| Function | Purpose |
|---|---|
| `X_api_version()`, `X_version()` | API version, version of TWIST Console |
| `X_settings_default(&s)` | default settings: the OOK frames (PI4 on request), 1500 ± 200 Hz, deep search on, whole block |
| `X_create()`, `X_destroy(d)` | decoder handle |
| `X_set_known(d, "F5ZZO,F1HDI")` | known callsigns (`known_only`; the deep search of the classic frame) |
| `X_decode(d, x, n, fs, &s, spots, max)` | one pass over a block of mono samples, for the library's protocols among `s.protocols` |
| `X_decode_recent(d, x, n, fs, &s, lookback, spots, max)` | frames that ended during the last `lookback` seconds |
| `X_last_error(d)` | message of the last error |
| `X_proto_name(proto)` | display label of one of its protocols, as printed by the decoders |

and their encoders:

| Function | Purpose |
|---|---|
| `twt_encode(proto, call, loc, chips, max)`, `twt_frame_chips(proto)` | chips of a TWIST frame (1 = carrier on), 239 / 321 |
| `twcl_encode(call, chips, max)` | the 239 chips of a classic frame |
| `twt_frame_step`, `twcl_frame_step`, `pi4l_frame_step` | a transmit step for a frame, its symbols encoded |
| `pi4l_frame_seconds()` | 146 / 6 s |

`libtwcore`:

| Function | Purpose |
|---|---|
| `twl_api_version()`, `twl_version()`, `twl_chip_seconds()` | API version, version, 0.128 s |
| `twl_keyed(samples, n, fs, &s, window_s, z_min, out, max)` | keyed signals present now (frame in progress) |
| `twl_arbitrate(spots, n)` | spots of several libraries from the same audio: one of each pair that cannot both be real |
| `twl_tx_settings_default`, `twl_tx_create`, `twl_tx_destroy` | transmitter handle |
| `twl_tx_start(t, &s, steps, n)` | prepares a sequence of steps |
| `twl_tx_render(t, out, n)` | next n audio samples of the sequence |
| `twl_tx_stop(t)`, `twl_tx_status(t, &st)`, `twl_tx_last_error(t)` | stop, progress and PTT state, error |
| `twl_tx_set_level(t, level)` | level while playing (Tune), smoothed over 10 ms |
| `twl_tx_step_seconds(&step)` | duration of a step, -1 if it cannot be sent |
| `twl_audio_available`, `twl_audio_device_count`, `twl_audio_device_info` | live audio built in? audio devices |
| `twl_audio_create`, `twl_audio_destroy` | audio handle |
| `twl_audio_start_input`, `twl_audio_last`, `twl_audio_input_stats`, `twl_audio_stop_input` | capture into a ring buffer, newest samples, level and overruns |
| `twl_audio_push` | feeds the capture path without a sound card (file replay, tests) |
| `twl_audio_start_output`, `twl_audio_output_active`, `twl_audio_stop_output` | plays a transmit sequence |
| `twl_audio_start_output_at`, `twl_audio_output_started`, `twl_audio_output_latency` | same, the first sample at the converter at a given UTC time |

The protocol constants are `TWL_PROTO_CLASSIC` (the classic frame,
Op05, libopera), `TWL_PROTO_TWS` and `TWL_PROTO_TWL` (libtwist;
`TWL_PROTO_ALL`, the three OOK frames, is the default) and
`TWL_PROTO_PI4` (libpi4: 4-FSK, searched in its own zone
`pi4_f_center` ± `pi4_tol`, 800 ± 150 Hz by default; not in the
default: add it to `protocols`). One settings structure serves the
three libraries; each takes its own protocols and ignores the others.

## Several libraries on the same audio

The application gives each block to the libraries of the protocols
asked, in this order: the classic frame, TWIST, PI4; then
`twl_arbitrate` on all the spots, then sorts them by start. Two spots
cannot both be real when they are the same callsign in overlapping
frames (one station, one frame at a time: the first is kept) or frames
of different protocols at the same frequency overlapping by more than
half of the shorter (a classic frame and a TWIST S frame both have 239
chips: the one whose pattern explains the received chips best - the
larger `contrast` of the spot - is kept). PI4, in its own zone and
minute, is never arbitrated. It is what the single receiver of the
earlier versions did inside one pass.

Times are in seconds from the first sample of the block, frequencies
in Hz. A spot gives `call`, `loc` (TWIST L, PI4), `proto`, `deep`,
`start`, `end`, `freq`, `snr` (dB in 2500 Hz, average power over the
frame), `drift` (Hz/s) and `contrast` (OOK frames: ON/OFF chip contrast
in noise units).

## The continuous decoder

OPERA and TWIST are not time-synchronous: a frame can start at any
second. The application keeps the last 45 s of audio and, every 3 s,
asks for the frames that **ended** since the previous pass:

```c
twl_spot_t spots[96];
int n = twcl_decode_recent(cl, last45s, 45 * fs, fs, &settings, 3.0, spots, 32);
n += twt_decode_recent(tw, last45s, 45 * fs, fs, &settings, 3.0, spots + n, 32);
n += pi4l_decode_recent(p4, last45s, 45 * fs, fs, &settings, 3.0, spots + n, 32);
n = twl_arbitrate(spots, n);
```

The window is on the frame **end**, per protocol: an OPERA or TWIST S
frame (30.6 s) and a TWIST L frame (41.1 s) that both just ended are
found in the same pass, each with its own start window
(`end - duration`). The same window exists in `twistd` and
`operad`: `-e 42:45` on a 45 s file.

A frame that ended just before a pass is found by that pass; one that
ends a moment later, by the next one. With a pass every 3 s, a frame
is displayed between 0 and 3 s after its end, plus the pass time.

### Measured pass time

45 s of 12 kHz audio, the three OOK frames (libopera and libtwist, each
its own spectrogram), on one core of a 2.1 GHz server Xeon (cloud VM,
slower than a desktop PC): about 0.9 s with three frames ending in the
same 3 s. `tests/test_libs` prints this time on every `make test`. The time
grows with the number of frames decoded (refinement and list decoding
of each), not with the length of the block. A pass well under the 3 s
period keeps the 4 s display target of the specification; if the
testers' PCs prove too slow, the refinement search (`tw_refine`) is the
place to gain.

## Frame in progress

`twl_keyed(samples, n, fs, &settings, window_s, z_min, out, max)` looks
at the last `window_s` seconds (8 s: about 60 chips) of the samples, in
the search zone, for an on/off keyed signal: one-chip spectra every
quarter chip, the energy jumps from one chip to the next in each bin
(Manchester and TWIST frames change level often), as robust z-scores
over at least 600 Hz. A steady carrier is set apart twice: its jumps are
small for its energy (keying index), and its energy varies less than
its mean (var/mean² >= 0.85 required; noise and keying give about 1).
It returns the signals strongest first, 15 Hz apart. The application
calls it after each pass (z >= 6.5) and shows a signal seen on two
passes in a row; from the decoding thread only (FFTW).

## Transmission (audio method)

The application transmits by sending a keyed audio tone to the
transceiver's USB or line input, in SSB. A transmission is a
**sequence of steps**:

| Step | Fields |
|---|---|
| `TWL_STEP_FRAME` | its symbols (`sym`, `nsym`, `sym_s`; `spacing_hz`, `offset_hz` for a frequency-shift frame), filled by the protocol's library: `twt_frame_step(proto, call, loc, &st)`, `twcl_frame_step(call, &st)`, `pi4l_frame_step(call, loc, &st)` |
| `TWL_STEP_CARRIER` | `seconds` (0.05 to 3600) — also the Tune |
| `TWL_STEP_PAUSE` | `seconds`, `ptt_hold` (1 = PTT stays on) |
| `TWL_STEP_CW` | `text` in Morse at `wpm` (5 to 40, 0: 12) |

`libtwcore` knows no protocol: a frame step carries its symbols, sent
one after the other for `sym_s` each, as on/off keying (`spacing_hz`
0, symbol 1 = carrier on) or as frequency-shift keying (symbol *k* at
`tone + offset_hz + k × spacing_hz`). Any step may have its own
frequency, `tone_hz` (0: the sequence's `twl_tx_settings_t.tone_hz`);
the phase goes on continuously across the change.
`twl_tx_step_seconds(&step)` gives the duration of a step, or -1 if it
cannot be sent.

**PI4**: `pi4l_frame_step(call, loc, &st)` encodes the message `call`
(or `"/"` + `loc`), 8 characters at most of A-Z 0-9 space and `/`: 146
symbols of exactly 1/6 s (24.33 s), 4-FSK, tone *k* at
`tone_hz − 117.1875 + k × 234.375` Hz — set `tone_hz` to the PI4 nominal
frequency (OZ2M), where a PI4 beacon also sends its CW identification
and carrier. In the status, `chip` is the symbol and `nchips` 146. A PI4
beacon minute, as FbBalise sends it:

```c
twl_step_t m[3] = {{0}};
pi4l_frame_step("F5ZZO", "", &m[0]);                m[0].tone_hz = 800.0;
m[1].kind = TWL_STEP_CW; strcpy(m[1].text, "F5ZZO JN12AX"); m[1].tone_hz = 800.0;
m[2].kind = TWL_STEP_CARRIER; m[2].seconds = 18.4;  m[2].tone_hz = 800.0;
```

```c
twl_tx_settings_t s;  twl_tx_settings_default(&s);   /* 48 kHz, 1500 Hz, level 0.5, 5 ms */
twl_step_t seq[3] = {{0}};
twt_frame_step(TWL_PROTO_TWL, "F6FBB", "JN12AX", &seq[0]);
seq[1].kind = TWL_STEP_CARRIER; seq[1].seconds = 10.0;
twcl_frame_step("F6FBB", &seq[2]);
twl_tx_start(tx, &s, seq, 3);
/* in the sound card callback: */
int got = twl_tx_render(tx, buffer, frames);   /* 0 = finished */
```

- **Timing on the audio clock.** Samples are produced on demand, so
  the sound card that consumes them sets the pace. Chip *k* of a step
  starting at *T* begins at sample `round((T + k × 0.128) × fs)`,
  computed from the exact time each time: no drift over 41 s at any
  rate (at 44.1 kHz a chip is 5644.8 samples).
- **No clicks.** The tone keeps a continuous phase over the whole
  sequence; each change of level is a raised-cosine ramp of 5 ms
  starting at the chip boundary. All edges are late by the same
  2.5 ms, so chip timing and energy are unchanged.
- **PTT.** `twl_tx_status` says whether the PTT should be on now
  (frames, carriers, pauses with `ptt_hold`, the final ramp) and when
  the sequence is `done`. Keying the PTT (CAT, DTR, RTS) is the
  caller's job, as is the watchdog.
- **Stop.** `twl_tx_stop` fades out over 5 ms; the next render then
  returns the last samples and `done` is set.
- **Level.** `twl_tx_set_level` changes the amplitude while the
  sequence plays, from any thread; it moves there with a 10 ms time
  constant (no click). For the Tune: the drive is set by ear and on the
  ALC meter, and the same level serves the beacon.

`twl_tx_render` runs in the audio thread; `twl_tx_stop` and
`twl_tx_status` may be called from the interface thread.

`twistenc -w FILE.wav CALL [LOCATOR]` writes one TWIST frame with
this generator, to play into a transmitter or check with `twistd`;
`twistenc -p pi4 [-w FILE.wav] MESSAGE` a PI4 frame (its 146 symbols,
and the audio at 800 Hz, or `-f`).

## Live audio

Built in when PortAudio is found at build time (`twl_audio_available`):
WASAPI, MME and DirectSound on Windows, ALSA and PulseAudio on Linux,
CoreAudio on macOS.

**Capture.** The sound card callback mixes the channels to mono,
low-pass filters and decimates to about 12 kHz (48000 → 12000,
44100 → 11025; flat to 3 kHz, aliases rejected by more than 70 dB),
and appends to a ring buffer (60 s). Every 3 s the decoding thread
copies the last 45 s with `twl_audio_last` and runs the
`*_decode_recent` of the libraries: the sound card and the decoder never wait for
each other, and a slow pass loses no samples. `twl_audio_input_stats`
gives the peak and RMS level (dBFS, last 0.1 s), the overruns reported
by the driver, and the input latency (to date the frames).

**Playback.** `twl_audio_start_output` plays a prepared `twl_tx`
sequence; the callback renders it, so the sound card clocks the chips.
Prepare the sequence at the device's `default_rate`.

**On the second.** `twl_audio_start_output_at(a, device, tx, utc)`
opens the stream at once and plays silence until the first sample of
the sequence reaches the converter at `utc` (seconds since 1970, with
fractions). The callback knows when each buffer will be heard
(PortAudio's `outputBufferDacTime`, or the stream latency if the driver
gives no time) and starts the sequence inside the buffer, to the
sample. The output latency - from tens of milliseconds to two seconds
with PulseAudio - is therefore absorbed: through a loopback the frame
is decoded 0.03 s from the asked second, against 1.9 s late when the
stream was simply started a second ahead. Open it at least one buffer
ahead (the application: 1 s). `twl_audio_output_started` gives the UTC
time the first sample reached the converter (0 while waiting).

**Without a sound card.** `TWL_AUDIO_NONE` + `twl_audio_push` feed the
same capture path: `twistlive -i FILE.wav` replays a recording through
it, and the tests use it.

The device list is read once, when PortAudio is first used.

### twistlive

The receive and transmit chain of the application, on the command
line, through the four libraries:

```
twistlive -l                                   audio devices
twistlive -d 3 -c F5ZZO                        continuous decoding from device 3
twistlive -i rec.wav -F                        same chain, fed from a file (fast)
twistlive -s twl,carrier:10,pause:5,classic -C F6FBB -L JN12AX -o 5
                                               transmit once (PTT by VOX for now)
```

A spot is printed once, as soon as its frame is decoded:
`2026-10-28 13:42:17.3  -17  1520.1  TWIST L : F1HDI JN18CS` (UTC start,
SNR, audio frequency, protocol, callsign, locator).

## Threads

A decoder is used by one thread at a time; several decoders, of one
library or several, may run in several threads at once (the FFTW
planner is made thread-safe). The application runs all its decoding in
one worker thread, so the user interface and the audio capture are
never blocked.

## From C#

```csharp
using Twist.Native;

using var dec = new TwistDecoder();          // the libraries present, as one
dec.SetKnown("F5ZZO,F1HDI");
LibTwist.SettingsDefault(out var s);
foreach (var p in dec.DecodeRecent(last45s, 12000, s, 3.0))   // arbitrated, by start
    Console.WriteLine($"{LibTwist.ProtoName(p.Proto)} {p.Call} {p.Loc} {p.Snr:F0} dB {p.Freq:F1} Hz");
```

`LibTwist.HasClassic` and `LibTwist.HasPi4` say whether `libopera` and
`libpi4` are there; without them, `TwistDecoder` decodes the other
protocols, and a frame step of theirs cannot be sent (an error says
why). A transmit step is described by its protocol, callsign and
locator (`TwlStep.Frame`, `TwlStep.Pi4`, `TwlStep.Cw`...); the
protocol's library encodes it when the sequence starts.

`bindings/csharp/TwistDemo` is a console program doing this on a WAV
file:

```
make                                  # at the repository root
cd bindings/csharp/TwistDemo
dotnet run ../../../tests/real/f5zzo.wav
dotnet run FILE.wav -r 3              # frames ended in the last 3 s
dotnet run -e F6FBB JN12AX            # chips of a TWIST L frame
dotnet run -t seq.wav F6FBB JN12AX    # TWIST L + carrier + pause + OPERA, as audio
```

The structures in `LibTwist.cs` mirror those of `twltypes.h` field by
field: a change on one side must be made on the other, with the API
version bumped.
