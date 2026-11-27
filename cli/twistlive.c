/*
 * twistlive.c - Continuous decoder from the sound card, and audio
 * transmission: the receive and transmit chain of the desktop
 * application, on the command line, through the libraries of TWIST
 * Console (twcore.h, libtwist.h, twcl.h, libpi4.h) exactly as the
 * application uses them.
 *
 *   twistlive -l                         lists the audio devices
 *   twistlive [-d DEV] [options]         decodes the sound card continuously
 *   twistlive -i FILE.wav [-F] [options] same, fed from a WAV file played
 *                                        at real speed (-F: as fast as possible)
 *   twistlive -s SEQ -C CALL [-L LOC] [-o DEV]
 *                                        transmits a sequence once (audio
 *                                        method; PTT by VOX for now)
 *
 * Every PERIOD seconds (3), the last 45 s of audio are searched for the
 * frames that ended since the previous pass (*_decode_recent of each
 * library, then twl_arbitrate, with a
 * lookback a little longer than the period); a frame found by two
 * passes is printed once (same protocol and callsign, start within
 * 5 s). Each spot is printed as soon as it is decoded:
 *
 *   2026-10-28 13:42:17.3  -17  1520.1  OPERA 05 : F5ZZO
 *   date and UTC time of the frame start, SNR (dB/2500 Hz), audio
 *   frequency (Hz), protocol, callsign [locator] [* = deep search]
 */

#if defined(_WIN32) && !defined(_WIN32_WINNT)
#define _WIN32_WINNT 0x0602     /* Windows 8+: GetSystemTimePreciseAsFileTime */
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <signal.h>
#include <time.h>
#include <math.h>
#include "twcore.h"
#include "libtwist.h"
#include "twcl.h"
#include "libpi4.h"
#include "wavread.h"

#ifdef _WIN32
#include <windows.h>
static void sleep_ms(int ms) { Sleep((DWORD)ms); }
#else
static void sleep_ms(int ms)
{
    struct timespec t;
    t.tv_sec = ms / 1000;
    t.tv_nsec = (long)(ms % 1000) * 1000000L;
    nanosleep(&t, NULL);
}
#endif

#define BLOCK_S   45.0     /* audio searched by each pass                 */
#define DUP_S      5.0     /* same frame if the starts are this close     */
#define MAX_SEEN 256

static volatile sig_atomic_t quit = 0;
static void on_signal(int sig) { (void)sig; quit = 1; }

static double now_utc(void)
{
#ifdef _WIN32
    FILETIME ft;          /* 100 ns units since 1601-01-01 */
    ULARGE_INTEGER u;
    GetSystemTimePreciseAsFileTime(&ft);
    u.LowPart = ft.dwLowDateTime;
    u.HighPart = ft.dwHighDateTime;
    return (double)(u.QuadPart - 116444736000000000ULL) * 1e-7;
#else
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (double)ts.tv_sec + ts.tv_nsec * 1e-9;
#endif
}

static void fmt_utc(double t, char *out, size_t n)
{
    time_t s = (time_t)floor(t);
    struct tm tmv;
#ifdef _WIN32
    gmtime_s(&tmv, &s);
#else
    gmtime_r(&s, &tmv);
#endif
    snprintf(out, n, "%04d-%02d-%02d %02d:%02d:%04.1f", tmv.tm_year + 1900, tmv.tm_mon + 1,
             tmv.tm_mday, tmv.tm_hour, tmv.tm_min, tmv.tm_sec + (t - floor(t)));
}

/* ------------------------------------------------------------ decoding */

typedef struct {
    twt_decoder   *tw;      /* the three libraries' decoders */
    twcl_decoder  *cl;
    pi4l_decoder  *p4;
    twl_settings_t set;
    double         period;
    int            verbose;
    float         *blk;
    int            nblk;
    struct { int proto; char call[12]; double start; } seen[MAX_SEEN];
    int            nseen, nspots, npass;
    double         t_pass_max;
} rx_t;

static int already_seen(rx_t *r, const twl_spot_t *s, double start)
{
    int i;
    for (i = 0; i < r->nseen; i++)
        if (r->seen[i].proto == s->proto && !strcmp(r->seen[i].call, s->call) &&
            fabs(r->seen[i].start - start) < DUP_S)
            return 1;
    if (r->nseen == MAX_SEEN) {                 /* forget the oldest */
        memmove(&r->seen[0], &r->seen[1], sizeof r->seen[0] * (MAX_SEEN - 1));
        r->nseen--;
    }
    r->seen[r->nseen].proto = s->proto;
    memcpy(r->seen[r->nseen].call, s->call, sizeof r->seen[0].call);
    r->seen[r->nseen].call[sizeof r->seen[0].call - 1] = '\0';
    r->seen[r->nseen].start = start;
    r->nseen++;
    return 0;
}

/* Display label of a protocol, from its library */
static const char *proto_name(int proto)
{
    return proto == TWL_PROTO_CLASSIC ? twcl_proto_name(proto) :
           proto == TWL_PROTO_PI4 ? pi4l_proto_name(proto) : twt_proto_name(proto);
}

static int cmp_start(const void *a, const void *b)
{
    double x = ((const twl_spot_t *)a)->start, y = ((const twl_spot_t *)b)->start;
    return (x > y) - (x < y);
}

/* The three libraries on the same block: the classic frame first, then
 * TWIST, then PI4 (twl_arbitrate keeps the first of the same callsign),
 * arbitrated, sorted by start. */
static int decode_all(rx_t *r, const float *x, int n, int fs, double lookback, twl_spot_t *sp, int max)
{
    int ns = 0, k;
    k = twcl_decode_recent(r->cl, x, n, fs, &r->set, lookback, sp, max);
    if (k < 0) { fprintf(stderr, "twistlive: %s\n", twcl_last_error(r->cl)); return -1; }
    ns += k;
    k = twt_decode_recent(r->tw, x, n, fs, &r->set, lookback, sp + ns, max - ns);
    if (k < 0) { fprintf(stderr, "twistlive: %s\n", twt_last_error(r->tw)); return -1; }
    ns += k;
    k = pi4l_decode_recent(r->p4, x, n, fs, &r->set, lookback, sp + ns, max - ns);
    if (k < 0) { fprintf(stderr, "twistlive: %s\n", pi4l_last_error(r->p4)); return -1; }
    ns += k;
    ns = twl_arbitrate(sp, ns);
    qsort(sp, (size_t)ns, sizeof sp[0], cmp_start);
    return ns;
}

/* One pass over the n newest samples, the last of which was captured
 * at UTC time t_end. */
static void pass(rx_t *r, const float *x, int n, int fs, double t_end)
{
    twl_spot_t sp[96];
    double t0 = t_end - (double)n / fs, c0, dt;
    int k, ns;
    c0 = (double)clock() / CLOCKS_PER_SEC;
    ns = decode_all(r, x, n, fs, r->period + 1.0, sp, 96);
    dt = (double)clock() / CLOCKS_PER_SEC - c0;
    r->npass++;
    if (dt > r->t_pass_max) r->t_pass_max = dt;
    if (ns < 0) return;
    for (k = 0; k < ns; k++) {
        char when[64];
        double start = t0 + sp[k].start;
        if (already_seen(r, &sp[k], start)) continue;
        fmt_utc(start, when, sizeof when);
        printf("%s %4.0f %7.1f  %s : %s%s%s%s\n", when, sp[k].snr, sp[k].freq,
               proto_name(sp[k].proto), sp[k].call, sp[k].loc[0] ? " " : "", sp[k].loc,
               sp[k].deep ? " *" : "");
        if (r->verbose)
            printf("      frame ended %.1f s before this pass, pass %.2f s\n", t_end - (t0 + sp[k].end), dt);
        fflush(stdout);
        r->nspots++;
    }
}

static void status_line(const rx_t *r, const twl_audio_stats_t *st)
{
    char when[64];
    fmt_utc(now_utc(), when, sizeof when);
    fprintf(stderr, "[%s] level %4.0f dBFS peak, %4.0f dBFS rms, %d Hz, %d overrun(s), "
            "%d pass(es), longest %.2f s, %d spot(s)\n", when, st->level_dbfs, st->rms_dbfs,
            st->rate, st->overruns, r->npass, r->t_pass_max, r->nspots);
}

static int run_live(rx_t *r, int device)
{
    twl_audio *a = twl_audio_create();
    twl_audio_stats_t st;
    double next;
    int fs, n, k = 0;
    if (!a || twl_audio_start_input(a, device, 0, 60.0) != 0) {
        fprintf(stderr, "twistlive: %s\n", a ? twl_audio_last_error(a) : "out of memory");
        twl_audio_destroy(a);
        return 1;
    }
    twl_audio_input_stats(a, &st);
    fs = st.rate;
    r->nblk = (int)(BLOCK_S * fs);
    r->blk = (float *)malloc(sizeof(float) * (size_t)r->nblk);
    if (!r->blk) { twl_audio_destroy(a); return 1; }
    fprintf(stderr, "twistlive: capturing, decoded at %d Hz; a pass every %.0f s once 31 s are "
            "captured; Ctrl-C to stop\n", fs, r->period);
    next = now_utc() + r->period;
    while (!quit) {
        double t;
        sleep_ms(100);
        if ((t = now_utc()) < next) continue;
        next += r->period;
        if (t > next) next = t + r->period;      /* fell behind: no burst of passes */
        n = twl_audio_last(a, r->blk, r->nblk);
        twl_audio_input_stats(a, &st);
        if (n >= 31 * fs) pass(r, r->blk, n, fs, now_utc() - st.latency_s);
        if (r->verbose || ++k % 20 == 0) status_line(r, &st);
    }
    twl_audio_input_stats(a, &st);
    status_line(r, &st);
    twl_audio_destroy(a);
    free(r->blk);
    return 0;
}

/* The same chain fed from a WAV file: the samples go through the
 * capture path (mixing, decimation, ring buffer) block by block, and a
 * pass runs every PERIOD seconds of audio. Time 0 of the file is
 * t_base (UTC, from -T). */
static int run_file(rx_t *r, const char *path, int fast, double t_base)
{
    wavfile_t w;
    twl_audio *a = twl_audio_create();
    twl_audio_stats_t st;
    float *chunk;
    long pos = 0, step;
    int fs, n, i;
    if (wav_read(path, &w) != 0) { twl_audio_destroy(a); return 1; }
    if (!a || twl_audio_start_input(a, TWL_AUDIO_NONE, w.sample_rate, 60.0) != 0) {
        fprintf(stderr, "twistlive: %s\n", a ? twl_audio_last_error(a) : "out of memory");
        wav_free(&w); twl_audio_destroy(a);
        return 1;
    }
    twl_audio_input_stats(a, &st);
    fs = st.rate;
    step = (long)(r->period * w.sample_rate);
    chunk = (float *)malloc(sizeof(float) * (size_t)step);
    r->nblk = (int)(BLOCK_S * fs);
    r->blk = (float *)malloc(sizeof(float) * (size_t)r->nblk);
    if (!chunk || !r->blk) return 1;
    /* the file, then 2 periods of silence so that a frame ending at
     * the very end of the file is still reached by a pass */
    while (!quit && pos < w.nsamples + 2 * step) {
        double t0 = now_utc();
        for (i = 0; i < step; i++) chunk[i] = pos + i < w.nsamples ? (float)w.samples[pos + i] : 0.0f;
        twl_audio_push(a, chunk, (int)step, 1);
        pos += step;
        n = twl_audio_last(a, r->blk, r->nblk);
        if (n >= 31 * fs) pass(r, r->blk, n, fs, t_base + (double)pos / w.sample_rate);
        if (!fast) {
            double left = r->period - (now_utc() - t0);
            if (left > 0) sleep_ms((int)(left * 1000));
        }
    }
    twl_audio_input_stats(a, &st);
    if (r->verbose) status_line(r, &st);
    free(chunk); free(r->blk);
    wav_free(&w);
    twl_audio_destroy(a);
    return 0;
}

/* ------------------------------------------------------------ transmit */

static int ieq(const char *a, const char *b, size_t n)
{
    size_t i;
    for (i = 0; i < n; i++) if (tolower((unsigned char)a[i]) != tolower((unsigned char)b[i])) return 0;
    return 1;
}

/* "twl,carrier:10,pause:5,classic" -> steps */
static int parse_seq(const char *q, const char *call, const char *loc, twl_step_t *st, int max)
{
    int n = 0;
    while (*q) {
        size_t len = strcspn(q, ",");
        const char *colon = memchr(q, ':', len);
        size_t name = colon ? (size_t)(colon - q) : len;
        double sec = colon ? atof(colon + 1) : 0.0;
        twl_step_t *s = &st[n];
        if (n == max) return -1;
        int frame = 0, rc = 0;
        memset(s, 0, sizeof *s);
        if (name == 3 && ieq(q, "tws", 3)) { frame = 1; if (call) rc = twt_frame_step(TWL_PROTO_TWS, call, NULL, s); }
        else if (name == 3 && ieq(q, "twl", 3)) { frame = 1; if (call) rc = twt_frame_step(TWL_PROTO_TWL, call, loc, s); }
        else if ((name == 7 && ieq(q, "classic", 7)) ||
                 (name == strcspn(twcl_proto_name(TWL_PROTO_CLASSIC), " ") &&
                  ieq(q, twcl_proto_name(TWL_PROTO_CLASSIC), name)))
            { frame = 1; if (call) rc = twcl_frame_step(call, s); }
        else if (name == 7 && ieq(q, "carrier", 7)) { s->kind = TWL_STEP_CARRIER; s->seconds = sec; }
        else if (name == 5 && ieq(q, "pause", 5)) { s->kind = TWL_STEP_PAUSE; s->seconds = sec; s->ptt_hold = 1; }
        else if (name == 4 && ieq(q, "rest", 4)) { s->kind = TWL_STEP_PAUSE; s->seconds = sec; s->ptt_hold = 0; }
        else return -1;
        if (frame && !call) return -2;
        if (rc != 0) return -3;
        n++;
        q += len;
        if (*q == ',') q++;
    }
    return n;
}

static int run_tx(const char *seq, const char *call, const char *loc, int device,
                  double tone, double level)
{
    twl_step_t st[64];
    twl_tx_settings_t ts;
    twl_tx_status_t ss;
    twl_audio_device_t info;
    twl_tx *tx = twl_tx_create();
    twl_audio *a = twl_audio_create();
    int n = parse_seq(seq, call, loc, st, 64), dev = device, i, last_step = -2;

    if (n == -2) { fprintf(stderr, "twistlive: a frame needs -C CALL\n"); return 2; }
    if (n == -3) { fprintf(stderr, "twistlive: cannot encode %s %s\n", call, loc ? loc : ""); return 2; }
    if (n <= 0) { fprintf(stderr, "twistlive: bad sequence '%s'\n", seq); return 2; }
    if (dev == TWL_AUDIO_DEFAULT)
        for (i = 0; i < twl_audio_device_count(); i++)
            if (twl_audio_device_info(i, &info) == 0 && info.is_default_out) { dev = i; break; }
    twl_tx_settings_default(&ts);
    if (twl_audio_device_info(dev, &info) == 0) ts.fs = (int)info.default_rate;
    ts.tone_hz = tone;
    ts.level = level;
    if (twl_tx_start(tx, &ts, st, n) != 0) { fprintf(stderr, "twistlive: %s\n", twl_tx_last_error(tx)); return 1; }
    twl_tx_status(tx, &ss);
    fprintf(stderr, "twistlive: transmitting %d step(s), %.1f s, tone %.0f Hz at %d Hz on '%s'; "
            "Ctrl-C stops at once\n", n, ss.total, tone, ts.fs, info.name);
    if (twl_audio_start_output(a, dev, tx) != 0) {
        fprintf(stderr, "twistlive: %s\n", twl_audio_last_error(a));
        return 1;
    }
    while (twl_audio_output_active(a)) {
        twl_tx_status(tx, &ss);
        if (quit) twl_tx_stop(tx);
        if (ss.step != last_step && !ss.done) {
            const twl_step_t *s = &st[ss.step];
            fprintf(stderr, "  %5.1f s  step %d: %s%s%s\n", ss.elapsed, ss.step + 1,
                    s->kind == TWL_STEP_FRAME ? proto_name(s->proto) :
                    s->kind == TWL_STEP_CARRIER ? "carrier" : "pause",
                    s->kind == TWL_STEP_PAUSE ? (s->ptt_hold ? " (PTT held)" : " (PTT released)") : "",
                    s->kind == TWL_STEP_FRAME ? "" : "");
            last_step = ss.step;
        }
        sleep_ms(50);
    }
    twl_tx_status(tx, &ss);
    fprintf(stderr, "twistlive: %s after %.1f s\n", quit ? "stopped" : "done", ss.elapsed);
    twl_audio_destroy(a);
    twl_tx_destroy(tx);
    return 0;
}

/* ------------------------------------------------------------ main */

static void usage(FILE *o, const char *prog)
{
    fprintf(o, "twistlive %s - continuous TWIST / %s / PI4 decoder and audio transmitter\n\n", twl_version(),
            twcl_proto_name(TWL_PROTO_CLASSIC));
    fprintf(o, "usage: %s -l                    list the audio devices\n", prog);
    fprintf(o, "       %s [-d DEV] [options]    decode the sound card continuously\n", prog);
    fprintf(o, "       %s -i FILE.wav [-F] [-T HHMMSS] [options]\n", prog);
    fprintf(o, "                               same, from a WAV file at real speed (-F: fast)\n");
    fprintf(o, "       %s -s SEQ -C CALL [-L LOC] [-o DEV] [-a HZ] [-g LEVEL]\n", prog);
    fprintf(o, "                               transmit a sequence once (audio, PTT by VOX)\n\n");
    fprintf(o, "decoding options:\n");
    fprintf(o, "  -f HZ      centre of the search (default 1500)\n");
    fprintf(o, "  -t HZ      half-width of the search (default 200)\n");
    fprintf(o, "  -p LIST    protocols: classic, tws, twl, twist, all (default all), pi4\n");
    fprintf(o, "  -c CALLS   known callsigns, comma-separated (deep search)\n");
    fprintf(o, "  -w HZ      rain scatter mode, spread in Hz\n");
    fprintf(o, "  -P SEC     period of the passes (default 3)\n");
    fprintf(o, "  -v         details: frame-to-display delay, status at each pass\n\n");
    fprintf(o, "sequence: steps separated by commas - classic, tws, twl (frames),\n");
    fprintf(o, "  carrier:SEC, pause:SEC (PTT held), rest:SEC (PTT released).\n");
    fprintf(o, "  e.g. -s twl,carrier:10,pause:5,classic -C F6FBB -L JN12AX\n");
    fprintf(o, "  -s carrier:30 is a 30 s tune. -a tone (1500 Hz), -g peak level (0.5)\n");
}

int main(int argc, char **argv)
{
    rx_t r;
    const char *file = NULL, *seq = NULL, *call = NULL, *loc = NULL, *known = NULL;
    int list = 0, fast = 0, device = TWL_AUDIO_DEFAULT, out_dev = TWL_AUDIO_DEFAULT, i, rc;
    double t_base = -1.0, tone = 1500.0, level = 0.5;

#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);   /* device names are UTF-8 */
#endif
    memset(&r, 0, sizeof r);
    r.period = 3.0;
    twt_settings_default(&r.set);
    r.set.protocols = TWL_PROTO_ALL;                   /* PI4 on request (-p) */
    if (twl_api_version() != TWL_CORE_API_VERSION || twt_api_version() != TWT_API_VERSION ||
        twcl_api_version() != TWCL_API_VERSION || pi4l_api_version() != PI4L_API_VERSION) {
        fprintf(stderr, "twistlive: library version mismatch\n");
        return 1;
    }

    for (i = 1; i < argc; i++) {
        const char *a = argv[i], *v = NULL;
        if (a[0] != '-' || !a[1] || a[2]) { usage(stderr, argv[0]); return 2; }
        if (strchr("difptcwPTsCLoag", a[1])) {
            if (i + 1 >= argc) { fprintf(stderr, "%s: %s needs a value\n", argv[0], a); return 2; }
            v = argv[++i];
        }
        switch (a[1]) {
        case 'l': list = 1; break;
        case 'F': fast = 1; break;
        case 'v': r.verbose = 1; break;
        case 'h': usage(stdout, argv[0]); return 0;
        case 'd': device = atoi(v); break;
        case 'o': out_dev = atoi(v); break;
        case 'i': file = v; break;
        case 'f': r.set.f_center = atof(v); break;
        case 't': r.set.tol = atof(v); break;
        case 'w': r.set.spread_hz = atof(v); break;
        case 'P': r.period = atof(v); break;
        case 'c': known = v; break;
        case 's': seq = v; break;
        case 'C': call = v; break;
        case 'L': loc = v; break;
        case 'a': tone = atof(v); break;
        case 'g': level = atof(v); break;
        case 'T':
            if (strlen(v) != 6) { fprintf(stderr, "%s: -T expects HHMMSS\n", argv[0]); return 2; }
            t_base = floor(now_utc() / 86400.0) * 86400.0 + ((v[0] - '0') * 10 + (v[1] - '0')) * 3600.0 + ((v[2] - '0') * 10 + (v[3] - '0')) * 60.0 +
                     (v[4] - '0') * 10 + (v[5] - '0');
            break;
        case 'p': {
            const char *q = v;
            r.set.protocols = 0;
            while (*q) {
                size_t len = strcspn(q, ",");
                if (len == 3 && ieq(q, "tws", 3)) r.set.protocols |= TWL_PROTO_TWS;
                else if (len == 3 && ieq(q, "twl", 3)) r.set.protocols |= TWL_PROTO_TWL;
                else if (len == 5 && ieq(q, "twist", 5)) r.set.protocols |= TWL_PROTO_TWS | TWL_PROTO_TWL;
                else if (len == 3 && ieq(q, "all", 3)) r.set.protocols |= TWL_PROTO_ALL;
                else if (len == 3 && ieq(q, "pi4", 3)) r.set.protocols |= TWL_PROTO_PI4;
                else if ((len == 7 && ieq(q, "classic", 7)) ||
                         (len == strcspn(twcl_proto_name(TWL_PROTO_CLASSIC), " ") &&
                          ieq(q, twcl_proto_name(TWL_PROTO_CLASSIC), len)))
                    r.set.protocols |= TWL_PROTO_CLASSIC;
                else { fprintf(stderr, "%s: unknown protocol '%.*s'\n", argv[0], (int)len, q); return 2; }
                q += len;
                if (*q == ',') q++;
            }
            break;
        }
        default: usage(stderr, argv[0]); return 2;
        }
    }
    if (r.period < 1.0 || r.period > 30.0) { fprintf(stderr, "%s: -P between 1 and 30 s\n", argv[0]); return 2; }
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);

    if (list) {
        twl_audio_device_t d;
        int n = twl_audio_device_count();
        if (!twl_audio_available()) { fprintf(stderr, "%s: built without live audio (PortAudio)\n", argv[0]); return 1; }
        printf(" #  in out    rate  host API          name\n");
        for (i = 0; i < n; i++)
            if (twl_audio_device_info(i, &d) == 0)
                printf("%2d %3d %3d %7.0f  %-16.16s  %s%s%s\n", i, d.max_in, d.max_out, d.default_rate, d.host_api,
                       d.name, d.is_default_in ? "  [default input]" : "", d.is_default_out ? "  [default output]" : "");
        return 0;
    }
    if (seq) return run_tx(seq, call, loc, out_dev, tone, level);

    r.tw = twt_create();
    r.cl = twcl_create();
    r.p4 = pi4l_create();
    if (!r.tw || !r.cl || !r.p4) return 1;
    if (known && (twt_set_known(r.tw, known) < 0 || twcl_set_known(r.cl, known) < 0 || pi4l_set_known(r.p4, known) < 0)) {
        fprintf(stderr, "%s: too many known callsigns\n", argv[0]);
        return 2;
    }
    if (t_base < 0) t_base = floor(now_utc() / 86400.0) * 86400.0;   /* file start: 00:00:00 today */
    rc = file ? run_file(&r, file, fast, t_base) : run_live(&r, device);
    twt_destroy(r.tw);
    twcl_destroy(r.cl);
    pi4l_destroy(r.p4);
    return rc;
}
