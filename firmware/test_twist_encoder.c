/* prints the chips of each frame given as CALL[:LOC] arguments; compare with twistenc */
#include <stdio.h>
#include <string.h>
#include "twist_encoder.h"
int main(int argc, char **argv)
{
    int a;
    for (a = 1; a < argc; a++) {
        char call[32], *loc;
        uint8_t ch[TWIST_MAX_CHIPS], pk[(TWIST_MAX_CHIPS + 7) / 8];
        int n, m, i;
        snprintf(call, sizeof call, "%s", argv[a]);
        loc = strchr(call, ':');
        if (loc) *loc++ = 0;
        n = twist_encode(call, loc, ch);
        m = twist_encode_packed(call, loc, pk);
        if (n < 0) { printf("%s: invalid\n", argv[a]); continue; }
        if (m != n) { printf("PACKED MISMATCH\n"); return 1; }
        for (i = 0; i < n; i++) if (TWIST_CHIP(pk, i) != ch[i]) { printf("PACKED MISMATCH\n"); return 1; }
        printf("%s: ", argv[a]);
        for (i = 0; i < n; i++) putchar('0' + ch[i]);
        putchar('\n');
    }
    return 0;
}
