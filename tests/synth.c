#include <math.h>
#include "synth.h"
#include "twcdefs.h"

static uint64_t st = 0x9E3779B97F4A7C15ull;
void synth_seed(uint64_t s) { st = s ? s : 0x9E3779B97F4A7C15ull; }
static uint64_t xr(void) { st ^= st << 13; st ^= st >> 7; st ^= st << 17; return st; }
static double unif(void) { return ((xr() >> 11) + 0.5) * (1.0 / 9007199254740992.0); }
double synth_gauss(void)
{
    static int have = 0; static double g2;
    double u, v, r;
    if (have) { have = 0; return g2; }
    u = unif(); v = unif(); r = sqrt(-2.0 * log(u));
    g2 = r * sin(2 * M_PI * v); have = 1;
    return r * cos(2 * M_PI * v);
}
double synth_amp_for_snr(double snr_db, double sigma, int fs)
{
    double pn = sigma * sigma * 2500.0 / (fs / 2.0);
    return sqrt(2.0 * pn * pow(10.0, snr_db / 10.0));
}
void synth_add_classic(double *x, long n, int fs, const unsigned char *chips, double tchip,
                     double t0, double freq, double amp, double phase, double drift, double trf)
{
    synth_add_frame(x, n, fs, chips, TWC_NCHIPS, tchip, t0, freq, amp, phase, drift, trf);
}
void synth_add_frame(double *x, long n, int fs, const unsigned char *chips, int nch, double tchip,
                     double t0, double freq, double amp, double phase, double drift, double trf)
{
    double dur = nch * tchip, tmid = t0 + dur / 2;
    long i0 = (long)floor(t0 * fs), i1 = (long)ceil((t0 + dur) * fs), i;
    double ph = phase;
    if (i0 < 0) i0 = 0;
    if (i1 > n) i1 = n;
    /* phase-continuous carrier (as a real transmitter keyed on/off) */
    for (i = 0; i < i1; i++) {
        double t = (double)i / fs, f = freq + drift * (t - tmid);
        double tt = t - t0, env = 0.0;
        long k = (long)floor(tt / tchip);
        if (i >= i0 && k >= 0 && k < nch && chips[k]) {
            env = 1.0;
            if (trf > 0) {
                double a = tt - k * tchip, b = (k + 1) * tchip - tt;
                int prev_on = k > 0 && chips[k - 1], next_on = k < nch - 1 && chips[k + 1];
                if (!prev_on && a < trf) env = 0.5 - 0.5 * cos(M_PI * a / trf);
                if (!next_on && b < trf) env = 0.5 - 0.5 * cos(M_PI * b / trf);
            }
            x[i] += amp * env * cos(ph);
        }
        ph += 2 * M_PI * f / fs;
        if (ph > 2 * M_PI) ph -= 2 * M_PI;
    }
}
void synth_add_noise(double *x, long n, double sigma)
{
    long i;
    for (i = 0; i < n; i++) x[i] += sigma * synth_gauss();
}

void synth_add_classic_spread(double *x, long n, int fs, const unsigned char *chips, double tchip,
                            double t0, double freq, double amp, double spread_hz)
{
    enum { NT = 64 };
    double fo[NT], ph[NT], a = amp / sqrt((double)NT);
    double dur = TWC_NCHIPS * tchip;
    long i0 = (long)floor(t0 * fs), i1 = (long)ceil((t0 + dur) * fs), i;
    int k;
    for (k = 0; k < NT; k++) { fo[k] = freq + spread_hz * synth_gauss(); ph[k] = 2 * M_PI * unif(); }
    if (i0 < 0) i0 = 0;
    if (i1 > n) i1 = n;
    for (i = i0; i < i1; i++) {
        double t = (double)i / fs, v = 0.0;
        long c = (long)floor((t - t0) / tchip);
        if (c < 0 || c >= TWC_NCHIPS || !chips[c]) continue;
        for (k = 0; k < NT; k++) v += cos(2 * M_PI * fo[k] * t + ph[k]);
        x[i] += a * v;
    }
}
