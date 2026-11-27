/*
 * false_alarm.c - Runs the full pipeline on pure noise and counts
 * anything decoded (every decode here is false).
 * usage: false_alarm mode_n trials seed [list_max] [z_min] [max_cand]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "twpipe.h"
#include "synth.h"
#include "twcdeep.h"

/* KNOWN="CALL1,CALL2" enables the deep search for those callsigns;
 * KNOWN_ONLY=1 restricts reports to them; DEEP_FA=x sets the target. */
static twc_known_t g_known[32];
static int g_nknown = 0;
static void setup_known(tw_params_t *p)
{
    const char *k = getenv("KNOWN");
    if (k && !g_nknown) {
        char buf[512], *tok;
        strncpy(buf, k, sizeof buf - 1); buf[sizeof buf - 1] = 0;
        for (tok = strtok(buf, ","); tok && g_nknown < 32; tok = strtok(NULL, ","))
            if (twc_known_init(tok, &g_known[g_nknown]) == 0) g_nknown++;
    }
    p->known = g_known; p->nknown = g_nknown;
    p->known_only = getenv("KNOWN_ONLY") != NULL;
    if (getenv("DEEP_FA")) p->deep_fa = atof(getenv("DEEP_FA"));
    if (getenv("NODEEP")) p->deep = 0;
    if (getenv("SPREAD")) p->spread_hz = atof(getenv("SPREAD"));
}
int main(int argc, char **argv)
{
    double n = argc > 1 ? atof(argv[1]) : 0.5;
    int trials = argc > 2 ? atoi(argv[2]) : 100;
    unsigned long seed = argc > 3 ? strtoul(argv[3], 0, 10) : 1;
    int list_max = argc > 4 ? atoi(argv[4]) : -1;
    double z_min = argc > 5 ? atof(argv[5]) : -1;
    int max_cand = argc > 6 ? atoi(argv[6]) : -1;
    int fs = 12000, t, k, nfalse = 0, ncand_tot = 0;
    long N = (long)((twc_duration(n) + 6) * fs);
    double *x = malloc(sizeof(double) * N);
    synth_seed(seed * 7777 + 1);
    for (t = 0; t < trials; t++) {
        tw_params_t p; tw_spot_t sp[8]; int ns;
        for (k = 0; k < N; k++) x[k] = 0.1 * synth_gauss();
        tw_params_default(&p); p.snr_avg = 0; /* compare with the injected carrier-on SNR */ p.mode_n = n; setup_known(&p);
        if (getenv("OSD_MARGIN")) p.osd_margin = atof(getenv("OSD_MARGIN"));
        if (list_max > 0) p.list_max = list_max;
        if (z_min >= 0) p.z_min = z_min;
        if (max_cand > 0) p.max_cand = max_cand;
        ns = tw_decode_real(x, N, fs, &p, sp, 8);
        for (k = 0; k < ns; k++) {
            printf("FALSE%s: trial %d seed %lu : %s dt=%.2f df=%.1f snr=%.1f z=%.1f rank=%d score=%.1f\n",
                   sp[k].deep ? "(deep)" : "", t, seed, sp[k].call, sp[k].dt, sp[k].df, sp[k].snr, sp[k].z, sp[k].rank, sp[k].score);
            nfalse++;
        }
        (void)ncand_tot;
    }
    printf("seed %lu: %d trials, %d false decodes\n", seed, trials, nfalse);
    free(x);
    return 0;
}
