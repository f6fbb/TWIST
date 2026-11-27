/*
 * tw_sweep.c - Monte-Carlo of the TWIST frames through the integrated
 * twistd pipeline (all protocols searched, as twistd does by default).
 * usage: tw_sweep S|L snr_from snr_to snr_step trials
 *   snr <= -90: noise only. Counts good decodes, and anything else
 *   (wrong callsign/locator/protocol) as false.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <math.h>
#include "twcdefs.h"
#include "twpipe.h"
#include "twist.h"
#include "synth.h"

static const char *calls[] = { "F6FBB","F1HDI","F5ZZO","OZ7IGY","EA5XYZ","G4JNT","DL0ABC","K1JT","9A1A","VK2ABC" };
static const char *locs[]  = { "JN18cs","JN03xh","JN12ax","JO65","IM98wl","IO90","JO40hd","FN42","JN95","QF56od" };

int main(int argc, char **argv)
{
    int fs = 12000, trials, t, type, nch;
    double s0, s1, ds, snr, tchip = tw_tchip(0.5), len;
    long N;
    double *x;
    if (argc < 6) { fprintf(stderr, "usage: tw_sweep S|L snr_from snr_to step trials\n"); return 2; }
    type = argv[1][0] == 'S' ? TW_TYPE_S : TW_TYPE_L;
    s0 = atof(argv[2]); s1 = atof(argv[3]); ds = atof(argv[4]); trials = atoi(argv[5]);
    nch = 2 * tw_ncoded(type) + 1;
    len = nch * tchip + 6.0;
    N = (long)(len * fs);
    x = malloc(sizeof(double) * N);
    synth_seed(getenv("SEED") ? strtoull(getenv("SEED"), NULL, 10) : 12345);
    printf("TWIST %s, %d chips, %d trials per point\n", type == TW_TYPE_S ? "short" : "long", nch, trials);
    for (snr = s0; ds < 0 ? snr >= s1 - 1e-9 : snr <= s1 + 1e-9; snr += ds) {
        int ok = 0, bad = 0;
        for (t = 0; t < trials; t++) {
            const char *call = calls[t % 10], *loc = locs[(t / 10 + t) % 10];
            unsigned char chips[TW_MAXCHIPS];
            double t0 = 0.5 + 3.5 * (double)((t * 7919) % 1000) / 1000.0;
            double f = 1500 + 150 * (2.0 * ((t * 104729) % 1000) / 1000.0 - 1.0);
            tw_params_t p; tw_spot_t sp[16]; int k, ns, got = 0;
            memset(x, 0, sizeof(double) * N);
            if (snr > -90) {
                tw_encode(type, call, loc, chips);
                synth_add_frame(x, N, fs, chips, nch, tchip, t0, f, synth_amp_for_snr(snr, 0.1, fs),
                                fabs(synth_gauss()) * 3, 0.0, 0.005);
            }
            synth_add_noise(x, N, 0.1);
            tw_params_default(&p); p.snr_avg = 0;
            if (getenv("OSD_MARGIN")) p.osd_margin = atof(getenv("OSD_MARGIN"));
            ns = tw_decode_real(x, N, fs, &p, sp, 16);
            for (k = 0; k < ns; k++) {
                int good = snr > -90 && !strcasecmp(sp[k].call, call) && fabs(sp[k].dt - t0) < tchip / 2 &&
                           sp[k].proto == (type == TW_TYPE_S ? TW_PROTO_S : TW_PROTO_L);
                if (good && type == TW_TYPE_L) good = !strcasecmp(sp[k].loc, loc);
                if (good && !got) { ok++; got = 1; }
                else if (!good) {
                    bad++;
                    printf("  FALSE snr=%.1f proto=%d %s %s dt=%.2f df=%.1f snr_est=%.1f\n", snr, sp[k].proto,
                           sp[k].call, sp[k].loc, sp[k].dt, sp[k].df, sp[k].snr);
                }
            }
        }
        printf("SNR %6.1f dB: %3d/%d decoded (%.0f%%), %d false\n", snr, ok, trials, 100.0 * ok / trials, bad);
        fflush(stdout);
        if (snr <= -90) break;
    }
    free(x);
    return 0;
}
