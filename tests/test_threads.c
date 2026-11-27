/* test_threads.c - several decoders at once, in several threads (the
 * application's tests do it; before the FFTW planner lock of core/twbb.c
 * this could end in "double free or corruption"). Each thread decodes
 * the same synthetic TWIST S frame a few times.
 * Prints PASS/FAIL; exit status 0 if every decode found the frame. */
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "twist.h"
#include "twpipe.h"
#include "synth.h"

#define NTHREADS 4
#define ROUNDS   3

static double *g_x;
static long g_n;
static int g_fs = 12000;

static void *worker(void *arg)
{
    int r, k, *ok = (int *)arg;
    for (r = 0; r < ROUNDS; r++) {
        tw_params_t p; tw_spot_t sp[8]; int ns;
        tw_params_default(&p);
        p.protocols = TW_PROTO_S;
        ns = tw_decode_real(g_x, g_n, g_fs, &p, sp, 8);
        for (k = 0; k < ns; k++) if (!strcmp(sp[k].call, "F6FBB")) { (*ok)++; break; }
    }
    return NULL;
}

int main(void)
{
    pthread_t th[NTHREADS];
    int ok[NTHREADS] = { 0 }, i, total = 0, nch = 239;
    unsigned char chips[TW_MAXCHIPS];
    double tchip = tw_tchip(0.5);
    g_n = (long)((nch * tchip + 4) * g_fs);
    g_x = calloc((size_t)g_n, sizeof(double));
    tw_encode(TW_TYPE_S, "F6FBB", NULL, chips);
    synth_seed(5);
    synth_add_frame(g_x, g_n, g_fs, chips, nch, tchip, 1.5, 1500, synth_amp_for_snr(-15, 0.1, g_fs), 0.3, 0.0, 0.005);
    synth_add_noise(g_x, g_n, 0.1);
    for (i = 0; i < NTHREADS; i++) pthread_create(&th[i], NULL, worker, &ok[i]);
    for (i = 0; i < NTHREADS; i++) { pthread_join(th[i], NULL); total += ok[i]; }
    free(g_x);
    if (total == NTHREADS * ROUNDS) { printf("PASS  %d decoders in %d threads at once\n", NTHREADS * ROUNDS, NTHREADS); return 0; }
    printf("FAIL  %d decoders in %d threads at once: %d found the frame\n", NTHREADS * ROUNDS, NTHREADS, total);
    return 1;
}
