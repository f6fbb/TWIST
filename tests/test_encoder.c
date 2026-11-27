/*
 * test_encoder.c - Bit-exact validation of twcenc.c against the
 * three reference transmissions of the F4GCB/7L1RLL sources, plus a
 * pack/unpack and CRC round trip over many callsigns.
 *
 *   gcc -O2 -I. -o test_encoder tests/test_encoder.c twcenc.c
 */
#include <stdio.h>
#include <string.h>
#include "twcenc.h"
#include "ref_vectors.h"

static int check(const char *call, const unsigned char *ref)
{
    unsigned char chips[TWC_NCHIPS];
    char norm[7];
    int i, ndiff = 0;

    if (twc_encode(call, chips, norm) != 0) {
        printf("%-8s: encode FAILED\n", call);
        return 1;
    }
    for (i = 0; i < TWC_NCHIPS; i++) if (chips[i] != ref[i]) ndiff++;
    printf("%-8s (\"%s\"): %s (%d/239 chips differ)\n", call, norm,
           ndiff ? "MISMATCH" : "bit-exact", ndiff);
    return ndiff != 0;
}

int main(void)
{
    static const char *calls[] = {
        "F6FBB", "F1HDI", "F5ZZO", "OZ7IGY", "AA1AA", "2E0ABC", "K1JT",
        "EA5XYZ", "G4JNT", "W1AW", "9A1A", "VK2ABC", "DL0ABC", "F4GCB", "Z1A"
    };
    int fail = 0;
    size_t k;

    fail |= check("AA1AA", REF_AA1AA);
    fail |= check("F5OEO", REF_F5OEO);
    fail |= check("7L1RLL", REF_7L1RLL);

    for (k = 0; k < sizeof calls / sizeof calls[0]; k++) {
        char norm[7], back[7];
        uint32_t p, p2;
        unsigned char vec[TWC_NVEC];
        if (tw_normalize_call(calls[k], norm) || tw_pack_call(norm, &p)) {
            printf("round trip %-8s: cannot encode\n", calls[k]); fail = 1; continue;
        }
        twc_build_vector(p, vec);
        if (twc_check_vector(vec, &p2) || p2 != p || tw_unpack_call(p, back) || strcmp(back, norm)) {
            printf("round trip %-8s: FAILED (\"%s\" -> \"%s\")\n", calls[k], norm, back); fail = 1;
        }
    }
    {   /* invalid ones must be refused */
        static const char *bad[] = { "F1HDI/P", "F1AB2", "ABCDEFG", "", "AB" };
        char norm[7];
        for (k = 0; k < sizeof bad / sizeof bad[0]; k++)
            if (tw_normalize_call(bad[k], norm) == 0) {
                printf("\"%s\" should have been refused (got \"%s\")\n", bad[k], norm); fail = 1;
            }
    }
    printf("%s\n", fail ? "FAIL" : "ALL OK");
    return fail;
}
