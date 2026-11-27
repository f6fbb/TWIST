/*
 * twaudio.c - Live audio over PortAudio (twaudio.h): capture into a
 * ring buffer of decimated mono samples, playback of a transmit
 * sequence.
 *
 * Capture path, run by the sound card callback (or twa_push):
 *   interleaved frames -> mono (mean of the channels) -> low-pass FIR
 *   (windowed sinc, cut-off 0.4 x output rate) computed only at the
 *   kept samples -> ring buffer
 * The ring buffer has ONE writer (the callback) and ONE reader (the
 * decoding thread). The writer stores the samples, then publishes the
 * new write count; the reader takes the count, copies the newest
 * samples behind it. The ring holds keep_s seconds (60 s) for passes
 * over 45 s: the 15 s in between are far more than a copy takes.
 */

#if defined(_WIN32) && (!defined(_WIN32_WINNT) || _WIN32_WINNT < 0x0602)
#undef _WIN32_WINNT
#define _WIN32_WINNT 0x0602   /* GetSystemTimePreciseAsFileTime: Windows 8 */
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdint.h>
#include "twaudio.h"
#include "twdefs.h"

#ifdef TWL_HAVE_PORTAUDIO
#include <portaudio.h>
#endif

#if defined(_MSC_VER)
#include <windows.h>
#define TWA_BARRIER() MemoryBarrier()
#else
#define TWA_BARRIER() __sync_synchronize()
#endif
#if defined(_WIN32) && !defined(_MSC_VER)
#include <windows.h>
#endif
#if !defined(_WIN32)
#include <time.h>
#endif

#ifdef TWL_HAVE_PORTAUDIO
/* UTC wall clock, s since 1970, sub-millisecond */
static double wall_now(void)
{
#if defined(_WIN32)
    FILETIME ft;
    ULARGE_INTEGER u;
    GetSystemTimePreciseAsFileTime(&ft);
    u.LowPart = ft.dwLowDateTime;
    u.HighPart = ft.dwHighDateTime;
    return (double)(u.QuadPart - 116444736000000000ULL) / 1e7;
#else
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (double)ts.tv_sec + ts.tv_nsec / 1e9;
#endif
}
#endif

#define TWA_MAX_CH 16

struct twa {
    /* capture */
    int     in_on;
    int     rate_in, dec, rate_out;   /* device rate, decimation, kept rate */
    float  *taps;                     /* FIR, ntaps                         */
    int     ntaps;
    float  *hist;                     /* last ntaps mono inputs (circular)  */
    int     hpos;
    int     phase;                    /* inputs since the last output       */
    float  *ring;
    int64_t cap;
    volatile int64_t wcount;          /* samples written (published)        */
    double  blk_peak, blk_sq;         /* level of the current ~0.1 s block  */
    int     blk_n, blk_len;
    volatile double level_dbfs, rms_dbfs;
    volatile int overruns;
    double  latency_in;
    /* playback */
    twtx_t *tx;
    int     in_ch, out_ch;
    double  start_at;                 /* UTC s for the first sample, 0: at once */
    int     out_started;              /* the sequence has begun at the DAC      */
    double  latency_out;
    volatile double dac_start;        /* UTC s the first sample reached the DAC */
#ifdef TWL_HAVE_PORTAUDIO
    PaStream *in_stream, *out_stream;
#endif
    char    err[200];
};

#ifdef TWL_HAVE_PORTAUDIO
/* PortAudio is initialized once, at first use, and terminated at exit:
 * each initialization probes every audio driver (slow, and noisy on
 * Linux). The device list is the one seen at that first use. */
static int pa_state = 0;   /* 0: not yet, 1: up, -1: failed */
static void pa_atexit(void) { Pa_Terminate(); }

static int pa_up(void)
{
    if (pa_state == 0) {
        pa_state = Pa_Initialize() == paNoError ? 1 : -1;
        if (pa_state == 1) atexit(pa_atexit);
    }
    return pa_state == 1 ? 0 : -1;
}
static void pa_down(void) { }
#endif

static void set_err(twa_t *a, const char *msg, const char *detail)
{
    if (a) snprintf(a->err, sizeof a->err, "%s%s%s", msg, detail ? ": " : "", detail ? detail : "");
}

/* ------------------------------------------------------------ devices */

int twa_available(void)
{
#ifdef TWL_HAVE_PORTAUDIO
    return 1;
#else
    return 0;
#endif
}

int twa_device_count(void)
{
#ifdef TWL_HAVE_PORTAUDIO
    int n;
    if (pa_up() != 0) return 0;
    n = Pa_GetDeviceCount();
    pa_down();
    return n < 0 ? 0 : n;
#else
    return 0;
#endif
}

int twa_device_info(int index, twl_audio_device_t *d)
{
#ifdef TWL_HAVE_PORTAUDIO
    const PaDeviceInfo *info;
    int rc = -1;
    if (!d || pa_up() != 0) return -1;
    memset(d, 0, sizeof *d);
    info = (index >= 0 && index < Pa_GetDeviceCount()) ? Pa_GetDeviceInfo(index) : NULL;
    if (info) {
        const PaHostApiInfo *h = Pa_GetHostApiInfo(info->hostApi);
        snprintf(d->name, sizeof d->name, "%s", info->name ? info->name : "");
        snprintf(d->host_api, sizeof d->host_api, "%s", h && h->name ? h->name : "");
        d->max_in = info->maxInputChannels;
        d->max_out = info->maxOutputChannels;
        d->default_rate = info->defaultSampleRate;
        d->is_default_in = index == Pa_GetDefaultInputDevice();
        d->is_default_out = index == Pa_GetDefaultOutputDevice();
        rc = 0;
    }
    pa_down();
    return rc;
#else
    (void)index; (void)d;
    return -1;
#endif
}

/* ------------------------------------------------------------ capture */

twa_t *twa_create(void)
{
    return (twa_t *)calloc(1, sizeof(twa_t));
}

/* Decimation: the largest integer factor keeping the rate >= 11025 Hz
 * (48000 -> 12000, 44100 -> 11025, 96000 -> 12000, 16000 -> 16000). */
static int choose_dec(int rate)
{
    int d = rate / 11025;
    return d < 1 ? 1 : d;
}

static int setup_capture(twa_t *a, int rate, double keep_s)
{
    int k, M;
    double fc, sum = 0.0;
    a->rate_in = rate;
    a->dec = choose_dec(rate);
    a->rate_out = rate / a->dec;
    /* FIR: 16 taps per decimation step (at least 33), Blackman window,
     * cut-off 0.4 x output rate: flat to 3 kHz, > 70 dB at the alias */
    a->ntaps = a->dec == 1 ? 1 : 16 * a->dec + 1;
    M = a->ntaps - 1;
    fc = 0.4 * a->rate_out / rate;            /* cycles per input sample */
    a->taps = (float *)malloc(sizeof(float) * (size_t)a->ntaps);
    a->hist = (float *)calloc((size_t)a->ntaps, sizeof(float));
    a->cap = (int64_t)(keep_s * a->rate_out);
    if (a->cap < a->rate_out) a->cap = a->rate_out;
    a->ring = (float *)calloc((size_t)a->cap, sizeof(float));
    if (!a->taps || !a->hist || !a->ring) return -1;
    for (k = 0; k <= M; k++) {
        double x = k - 0.5 * M, h, w;
        h = x == 0.0 ? 2.0 * fc : sin(2.0 * M_PI * fc * x) / (M_PI * x);
        w = M == 0 ? 1.0 : 0.42 - 0.5 * cos(2.0 * M_PI * k / M) + 0.08 * cos(4.0 * M_PI * k / M);
        a->taps[k] = (float)(h * w);
        sum += h * w;
    }
    for (k = 0; k <= M; k++) a->taps[k] = (float)(a->taps[k] / sum);   /* unity gain at DC */
    a->hpos = 0;
    a->phase = 0;
    a->wcount = 0;
    a->blk_len = a->rate_out / 10;
    a->blk_n = 0;
    a->blk_peak = a->blk_sq = 0.0;
    a->level_dbfs = a->rms_dbfs = -120.0;
    a->overruns = 0;
    a->latency_in = 0.0;
    return 0;
}

static void free_capture(twa_t *a)
{
    free(a->taps); free(a->hist); free(a->ring);
    a->taps = a->hist = a->ring = NULL;
    a->in_on = 0;
}

static double dbfs(double v) { return v > 1e-6 ? 20.0 * log10(v) : -120.0; }

/* The capture path: called by the sound card callback or twa_push. */
static void feed(twa_t *a, const float *x, long frames, int ch)
{
    int64_t w = a->wcount;
    long f;
    int c, k;
    for (f = 0; f < frames; f++) {
        float m = 0.0f;
        for (c = 0; c < ch; c++) m += x[f * ch + c];
        m /= (float)ch;
        a->hist[a->hpos] = m;
        a->hpos = a->hpos + 1 == a->ntaps ? 0 : a->hpos + 1;
        if (++a->phase < a->dec) continue;
        a->phase = 0;
        {
            /* y = sum taps[k] * input[newest - k]; the newest input is at hpos - 1 */
            double y = 0.0;
            int j = a->hpos;
            for (k = a->ntaps - 1; k >= 0; k--) {
                y += a->taps[k] * a->hist[j];
                j = j + 1 == a->ntaps ? 0 : j + 1;
            }
            a->ring[w % a->cap] = (float)y;
            w++;
            if (fabs(y) > a->blk_peak) a->blk_peak = fabs(y);
            a->blk_sq += y * y;
            if (++a->blk_n >= a->blk_len) {
                a->level_dbfs = dbfs(a->blk_peak);
                a->rms_dbfs = dbfs(sqrt(a->blk_sq / a->blk_n));
                a->blk_n = 0;
                a->blk_peak = a->blk_sq = 0.0;
            }
        }
    }
    TWA_BARRIER();          /* samples stored before the count is published */
    a->wcount = w;
}

#ifdef TWL_HAVE_PORTAUDIO
static int in_cb(const void *in, void *out, unsigned long frames,
                 const PaStreamCallbackTimeInfo *ti, PaStreamCallbackFlags flags, void *user)
{
    twa_t *a = (twa_t *)user;
    (void)out; (void)ti;
    if (flags & paInputOverflow) a->overruns++;
    if (in) feed(a, (const float *)in, (long)frames, a->in_ch);
    return paContinue;
}
#endif

int twa_start_input(twa_t *a, int device, int rate, double keep_s)
{
    if (!a) return -1;
    a->err[0] = '\0';
    twa_stop_input(a);
    if (keep_s <= 0.0) keep_s = 60.0;
    if (device == TWL_AUDIO_NONE) {
        if (rate < 8000 || rate > 384000) { set_err(a, "rate out of range (8000-384000 Hz)", NULL); return -1; }
        if (setup_capture(a, rate, keep_s) != 0) { free_capture(a); set_err(a, "out of memory", NULL); return -1; }
        a->in_on = 1;
        return 0;
    }
#ifdef TWL_HAVE_PORTAUDIO
    {
        PaStreamParameters p;
        const PaDeviceInfo *info;
        PaError e;
        if (pa_up() != 0) { set_err(a, "PortAudio initialization failed", NULL); return -1; }
        if (device == TWL_AUDIO_DEFAULT) device = Pa_GetDefaultInputDevice();
        info = (device >= 0 && device < Pa_GetDeviceCount()) ? Pa_GetDeviceInfo(device) : NULL;
        if (!info || info->maxInputChannels < 1) { pa_down(); set_err(a, "no such input device", NULL); return -1; }
        if (rate <= 0) rate = (int)info->defaultSampleRate;
        memset(&p, 0, sizeof p);
        p.device = device;
        p.channelCount = info->maxInputChannels >= 2 ? 2 : 1;   /* mono or stereo, mixed */
        p.sampleFormat = paFloat32;
        p.suggestedLatency = info->defaultHighInputLatency;
        if (setup_capture(a, rate, keep_s) != 0) { free_capture(a); pa_down(); set_err(a, "out of memory", NULL); return -1; }
        a->in_ch = p.channelCount;
        e = Pa_OpenStream(&a->in_stream, &p, NULL, rate, paFramesPerBufferUnspecified, paNoFlag, in_cb, a);
        if (e == paNoError) e = Pa_StartStream(a->in_stream);
        if (e != paNoError) {
            if (a->in_stream) Pa_CloseStream(a->in_stream);
            a->in_stream = NULL;
            free_capture(a);
            pa_down();
            set_err(a, "cannot open the input device", Pa_GetErrorText(e));
            return -1;
        }
        {
            const PaStreamInfo *si = Pa_GetStreamInfo(a->in_stream);
            a->latency_in = si ? si->inputLatency : 0.0;
        }
        a->in_on = 1;
        return 0;
    }
#else
    (void)rate;
    set_err(a, "built without live audio (PortAudio)", NULL);
    return -1;
#endif
}

int twa_input_rate(const twa_t *a) { return a && a->in_on ? a->rate_out : 0; }

int twa_push(twa_t *a, const float *x, int frames, int channels)
{
    if (!a || !a->in_on || !x || frames < 0 || channels < 1 || channels > TWA_MAX_CH) return -1;
#ifdef TWL_HAVE_PORTAUDIO
    if (a->in_stream) { set_err(a, "push needs device TWL_AUDIO_NONE", NULL); return -1; }
#endif
    feed(a, x, frames, channels);
    return frames;
}

int twa_last(twa_t *a, float *out, int n)
{
    int64_t w, start, i;
    if (!a || !a->in_on || !out || n <= 0) return 0;
    w = a->wcount;
    TWA_BARRIER();          /* count read before the samples */
    if (n > w) n = (int)w;
    if (n > a->cap) n = (int)a->cap;
    start = w - n;
    for (i = 0; i < n; i++) out[i] = a->ring[(start + i) % a->cap];
    return n;
}

void twa_input_stats(const twa_t *a, twl_audio_stats_t *st)
{
    if (!st) return;
    memset(st, 0, sizeof *st);
    st->level_dbfs = st->rms_dbfs = -120.0;
    if (!a || !a->in_on) return;
    st->level_dbfs = a->level_dbfs;
    st->rms_dbfs = a->rms_dbfs;
    st->seconds = (double)a->wcount / a->rate_out;
    st->latency_s = a->latency_in;
    st->overruns = a->overruns;
    st->rate = a->rate_out;
}

void twa_stop_input(twa_t *a)
{
    if (!a || !a->in_on) return;
#ifdef TWL_HAVE_PORTAUDIO
    if (a->in_stream) {
        Pa_StopStream(a->in_stream);
        Pa_CloseStream(a->in_stream);
        a->in_stream = NULL;
        pa_down();
    }
#endif
    free_capture(a);
}

/* ------------------------------------------------------------ playback */

#ifdef TWL_HAVE_PORTAUDIO
static int out_cb(const void *in, void *out, unsigned long frames,
                  const PaStreamCallbackTimeInfo *ti, PaStreamCallbackFlags flags, void *user)
{
    twa_t *a = (twa_t *)user;
    float *o = (float *)out, mono[1024];
    unsigned long done = 0;
    int got = 1;
    (void)in; (void)flags;
    if (!a->out_started) {
        /* when the first sample of this buffer reaches the DAC, on the
         * wall clock: PortAudio's stream times when it gives them,
         * else the stream's announced latency */
        double ahead = ti && ti->outputBufferDacTime > 0.0 && ti->currentTime > 0.0
                       ? ti->outputBufferDacTime - ti->currentTime : a->latency_out;
        double dac = wall_now() + (ahead >= 0.0 && ahead < 5.0 ? ahead : a->latency_out);
        long wait = a->start_at > 0.0 ? (long)floor((a->start_at - dac) * twtx_rate(a->tx) + 0.5) : 0;
        if (wait >= (long)frames) {            /* the start is after this buffer */
            memset(o, 0, sizeof(float) * frames * (unsigned long)a->out_ch);
            return paContinue;
        }
        if (wait > 0) {
            memset(o, 0, sizeof(float) * (unsigned long)wait * (unsigned long)a->out_ch);
            done = (unsigned long)wait;
        }
        a->dac_start = dac + (double)(wait > 0 ? wait : 0) / twtx_rate(a->tx);
        a->out_started = 1;
    }
    while (done < frames) {
        int n = (int)(frames - done > 1024 ? 1024 : frames - done), k, c;
        got = twtx_render(a->tx, mono, n);         /* zero-fills past the end */
        for (k = 0; k < n; k++)
            for (c = 0; c < a->out_ch; c++) o[(done + k) * a->out_ch + c] = mono[k];
        done += n;
        if (got < n) break;
    }
    for (; done < frames; done++) {
        int c;
        for (c = 0; c < a->out_ch; c++) o[done * a->out_ch + c] = 0.0f;
    }
    /* paComplete lets the last buffer play out, then the stream stops */
    return got > 0 ? paContinue : paComplete;
}
#endif

int twa_start_output(twa_t *a, int device, twtx_t *tx)
{
    return twa_start_output_at(a, device, tx, 0.0);
}

int twa_start_output_at(twa_t *a, int device, twtx_t *tx, double utc)
{
    if (!a) return -1;
    a->err[0] = '\0';
    twa_stop_output(a);
    a->start_at = utc;
    a->out_started = 0;
    a->dac_start = 0.0;
    a->latency_out = 0.0;
    if (!tx || twtx_rate(tx) <= 0) { set_err(a, "no transmit sequence prepared", NULL); return -1; }
#ifdef TWL_HAVE_PORTAUDIO
    {
        PaStreamParameters p;
        const PaDeviceInfo *info;
        PaError e;
        if (pa_up() != 0) { set_err(a, "PortAudio initialization failed", NULL); return -1; }
        if (device == TWL_AUDIO_DEFAULT) device = Pa_GetDefaultOutputDevice();
        info = (device >= 0 && device < Pa_GetDeviceCount()) ? Pa_GetDeviceInfo(device) : NULL;
        if (!info || info->maxOutputChannels < 1) { pa_down(); set_err(a, "no such output device", NULL); return -1; }
        memset(&p, 0, sizeof p);
        p.device = device;
        p.channelCount = info->maxOutputChannels >= 2 ? 2 : 1;   /* same tone on both */
        p.sampleFormat = paFloat32;
        p.suggestedLatency = info->defaultHighOutputLatency;
        a->tx = tx;
        a->out_ch = p.channelCount;
        e = Pa_OpenStream(&a->out_stream, NULL, &p, twtx_rate(tx), paFramesPerBufferUnspecified,
                          paClipOff, out_cb, a);
        if (e == paNoError) {
            const PaStreamInfo *si = Pa_GetStreamInfo(a->out_stream);
            a->latency_out = si ? si->outputLatency : 0.0;
            e = Pa_StartStream(a->out_stream);
        }
        if (e != paNoError) {
            if (a->out_stream) Pa_CloseStream(a->out_stream);
            a->out_stream = NULL;
            a->tx = NULL;
            pa_down();
            set_err(a, "cannot open the output device", Pa_GetErrorText(e));
            return -1;
        }
        return 0;
    }
#else
    (void)device;
    set_err(a, "built without live audio (PortAudio)", NULL);
    return -1;
#endif
}

double twa_output_latency(const twa_t *a) { return a ? a->latency_out : 0.0; }
double twa_output_started(const twa_t *a) { return a && a->out_started ? a->dac_start : 0.0; }

int twa_output_active(const twa_t *a)
{
#ifdef TWL_HAVE_PORTAUDIO
    return a && a->out_stream && Pa_IsStreamActive(a->out_stream) == 1;
#else
    (void)a;
    return 0;
#endif
}

void twa_stop_output(twa_t *a)
{
    if (!a) return;
#ifdef TWL_HAVE_PORTAUDIO
    if (a->out_stream) {
        Pa_StopStream(a->out_stream);
        Pa_CloseStream(a->out_stream);
        a->out_stream = NULL;
        pa_down();
    }
#endif
    a->tx = NULL;
}

void twa_destroy(twa_t *a)
{
    if (!a) return;
    twa_stop_output(a);
    twa_stop_input(a);
    free(a);
}

const char *twa_error(const twa_t *a)
{
    return a ? a->err : "no audio";
}
