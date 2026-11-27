/*
 * twltypes.h - The types shared by the TWIST Console libraries:
 *
 *   libtwcore  (twcore.h)   audio, transmission, keying detector
 *   libtwist   (libtwist.h) TWIST S and TWIST L frames, twt_*
 *   libopera   (twcl.h)     the classic frame, twcl_*
 *   libpi4     (libpi4.h)   PI4, pi4l_*
 *
 * The three protocol libraries do not know each other; each depends
 * on nothing but its own code and the common signal processing
 * compiled into it. They share these plain types, so that a caller
 * handles the spots of all three, and the transmit steps they encode,
 * the same way.
 *
 * Designed to be called from C# (P/Invoke) as well as from C:
 *   - plain types only: int, double, float arrays, char strings;
 *   - fixed-size structures without pointers, filled by the caller's
 *     arrays - the libraries never allocate memory the caller must
 *     free;
 *   - opaque handles for the state kept between calls.
 * See docs/LIBTWIST.md.
 *
 * Threads: a decoder is used by one thread at a time; several
 * decoders, of one library or several, may run in several threads at
 * once (FFTW's planner, not thread-safe, is made so at load time).
 *
 * Times are in seconds from the first sample of the block passed,
 * frequencies in Hz (audio).
 */

#ifndef TWLTYPES_H
#define TWLTYPES_H

#if defined(_WIN32)
#  if defined(TWL_BUILD)
#    define TWL_API __declspec(dllexport)
#  elif defined(TWL_STATIC)
#    define TWL_API
#  else
#    define TWL_API __declspec(dllimport)
#  endif
#else
#  define TWL_API __attribute__((visibility("default")))
#endif

/* Protocols (bits of twl_settings_t.protocols, twl_spot_t.proto). */
#define TWL_PROTO_CLASSIC 1  /* classic frame (Op05): callsign, 239 chips (libopera) */
#define TWL_PROTO_TWS    2   /* TWIST S: callsign, 239 chips (libtwist)            */
#define TWL_PROTO_TWL    4   /* TWIST L: callsign + locator, 321 chips (libtwist)  */
#define TWL_PROTO_ALL    7   /* the three OOK frames                               */
#define TWL_PROTO_PI4    8   /* PI4 (4-FSK, OZ2M), in its own zone:
                                pi4_f_center +/- pi4_tol (libpi4)                 */

#define TWL_MAX_CHIPS  321   /* longest OOK frame (TWIST L)                  */

/* Decoding settings - twl_settings_default() fills the defaults. */
typedef struct {
    int    protocols;    /* TWL_PROTO_* bits searched (default: the three
                            OOK frames, TWL_PROTO_ALL; PI4 on request).
                            Each library takes its own, ignores the rest */
    double f_center;     /* centre of the search, Hz (default 1500)        */
    double tol;          /* half-width of the search, Hz (default 200)     */
    double spread_hz;    /* > 0: frequency-spread signals (rain scatter),
                            energy summed over this band; 0 = off         */
    int    deep;         /* 1: deep search for the known callsigns
                            (classic frame only), default 1               */
    int    known_only;   /* 1: report only the known callsigns            */
    double end_min;      /* frame END window, s from the block start;     */
    double end_max;      /* end_max < 0: no limit (default: whole block)  */
    double pi4_f_center; /* PI4: its carrier (OZ2M convention), Hz (800)   */
    double pi4_tol;      /* PI4: half-width of its search, Hz (150)        */
} twl_settings_t;

/* One decoded frame. */
typedef struct {
    char   call[12];     /* callsign, NUL-terminated (PI4: up to 8
                            characters, "F5ZZO/P"; "" for a PI4 locator
                            message, then loc is set)                   */
    char   loc[8];       /* locator (TWIST L), else ""                     */
    int    proto;        /* TWL_PROTO_CLASSIC, _TWS, _TWL or _PI4          */
    int    deep;         /* 1: found by the deep search (known callsign)   */
    double start;        /* start of chip 0, s from the block start        */
    double end;          /* end of the last chip, s                        */
    double freq;         /* carrier frequency, Hz                          */
    double snr;          /* dB in 2500 Hz, average power over the frame    */
    double drift;        /* Hz/s                                           */
    double contrast;     /* OOK frames: (mean ON-chip - mean OFF-chip
                            energy) / noise - how well the decoded
                            pattern explains the received chips. Two
                            spots of different libraries at the same
                            frequency and time: the larger one is the
                            true frame (twl_arbitrate). PI4: 0.        */
} twl_spot_t;

/* A keyed signal present now (twl_keyed, twcore.h). */
typedef struct {
    double freq;         /* Hz, audio                                      */
    double z;            /* detector strength, robust sigmas over the zone */
} twl_keyed_t;

/* ------------------------------------------------------------------ */
/* Transmission: the audio method (keyed tone into the transceiver's  */
/* USB / line input, SSB mode)                                         */
/* ------------------------------------------------------------------ */
/*
 * A sequence of steps - frames, fixed-length carriers, pauses, CW - is
 * rendered as audio samples, block by block, by twl_tx_render(): the
 * sound card's callback asks for the next block, so the chips are
 * clocked by the sound card itself, without drift (chip k of a step
 * starting at T begins at sample round((T + k x 0.128 s) x fs)).
 *
 * The tone keeps a continuous phase; each change of level is a
 * raised-cosine ramp of ramp_s (5 ms) starting at the chip boundary:
 * no clicks. The PTT state wanted at each moment is in the status
 * (pauses may release it); keying the PTT is the caller's job.
 *
 * Threads: twl_tx_render from the audio thread; twl_tx_stop and
 * twl_tx_status may be called from another thread (status is then a
 * snapshot that may lag by one block).
 */

#define TWL_STEP_FRAME    1  /* a frame: its symbols, encoded by the protocol's
                                library (twt_frame_step, twcl_frame_step,
                                pi4l_frame_step)                          */
#define TWL_STEP_CARRIER  2  /* continuous carrier for `seconds`           */
#define TWL_STEP_PAUSE    3  /* silence for `seconds`                      */
#define TWL_STEP_CW       4  /* Morse: text, at wpm                        */

#define TWL_MAX_SYMBOLS 324  /* symbols of a frame step (TWIST L: 321)     */

/* A frame step holds its symbols, sent one after the other for sym_s
 * each: with spacing_hz = 0, on/off keying (symbol 1 = carrier on) at
 * the step's tone; with spacing_hz > 0, frequency shift keying, symbol
 * k at tone + offset_hz + k * spacing_hz (PI4: 4-FSK, offset -117.1875,
 * spacing 234.375 Hz: tone_hz is the PI4 nominal frequency, OZ2M).
 *
 * tone_hz of any step (0: the tone of the sequence, twl_tx_settings_t)
 * moves that step to its own frequency, the phase going on
 * continuously. */
typedef struct {
    int    kind;         /* TWL_STEP_*                                     */
    int    proto;        /* FRAME: its protocol (TWL_PROTO_*), for the
                            caller's display only                          */
    int    nsym;         /* FRAME: number of symbols in sym                */
    double sym_s;        /* FRAME: duration of one symbol, s (> 0)         */
    double spacing_hz;   /* FRAME: 0 = on/off keying, else FSK spacing     */
    double offset_hz;    /* FRAME FSK: frequency of symbol 0 - tone, Hz    */
    double seconds;      /* CARRIER, PAUSE: duration, 0.05 to 3600 s       */
    int    ptt_hold;     /* PAUSE: 1 = PTT stays on, 0 = released          */
    double tone_hz;      /* frequency of this step, Hz (0: the sequence's) */
    double wpm;          /* CW: words per minute, 5 to 40 (0: 12; PARIS,
                            a dot 1.2 / wpm s)                             */
    char   text[32];     /* CW: the text sent (A-Z 0-9 / and spaces);
                            FRAME: what it carries ("F5ZZO JN12AX"),
                            for display                                    */
    unsigned char sym[TWL_MAX_SYMBOLS];  /* FRAME: the symbols           */
} twl_step_t;

typedef struct {
    int    fs;           /* output sample rate, Hz (default 48000)         */
    double tone_hz;      /* audio tone, Hz (default 1500)                  */
    double level;        /* peak amplitude, 0 to 1 (default 0.5)           */
    double ramp_s;       /* rise and fall time, s (default 0.005)          */
} twl_tx_settings_t;

typedef struct {
    int    step;         /* current step (-1 when done)                    */
    int    kind;         /* its TWL_STEP_*                                 */
    int    chip;         /* symbol being sent in a frame, else -1          */
    int    nchips;       /* symbols of that frame                          */
    double elapsed;      /* s since the start of the sequence              */
    double total;        /* s, whole sequence                              */
    int    ptt;          /* 1: the PTT should be on now                    */
    int    done;         /* 1: finished or stopped - release the PTT       */
} twl_tx_status_t;


/* ------------------------------------------------------------------ */
/* Live audio (PortAudio): capture for the continuous decoder,        */
/* playback of a transmit sequence                                     */
/* ------------------------------------------------------------------ */
/*
 * Capture: channels mixed to mono, low-pass filtered and decimated to
 * about 12 kHz (48000 -> 12000, 44100 -> 11025) in the sound card
 * callback, kept in a ring buffer; twl_audio_last copies the newest
 * samples for a decoding pass. The decoding thread and the sound card
 * never wait for each other.
 *
 * Playback: the sound card callback renders a twl_tx sequence, so the
 * chips are clocked by the sound card. Prepare the sequence with
 * twl_tx_settings_t.fs = the output device's default_rate.
 *
 * Device TWL_AUDIO_NONE: capture without a sound card, fed by
 * twl_audio_push (replay of a file, tests).
 */

#define TWL_AUDIO_DEFAULT  (-1)  /* the system's default device          */
#define TWL_AUDIO_NONE     (-2)  /* no device: fed by twl_audio_push      */

typedef struct {
    char   name[128];    /* device name, UTF-8                             */
    char   host_api[32]; /* "Windows WASAPI", "MME", "ALSA"... (UTF-8)     */
    int    max_in;       /* input channels (0: output only)                */
    int    max_out;      /* output channels (0: input only)                */
    double default_rate; /* Hz                                             */
    int    is_default_in, is_default_out;
} twl_audio_device_t;

typedef struct {
    double level_dbfs;   /* peak level of the last ~0.1 s, dBFS (-120: silence) */
    double rms_dbfs;     /* RMS level of the last ~0.1 s, dBFS             */
    double seconds;      /* audio captured so far, s                       */
    double latency_s;    /* input latency reported by the driver, s        */
    int    overruns;     /* blocks lost by the sound card (should stay 0)  */
    int    rate;         /* rate of the samples given by twl_audio_last    */
} twl_audio_stats_t;

#endif /* TWLTYPES_H */
