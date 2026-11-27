/*
 * twist_encoder.c - TWIST v1 frame encoder. See twist_encoder.h and
 * PROTOCOL.md (twistd). The generator tables below are copied from
 * twistd's twist_tables.h (frozen: they define the protocol).
 */
#include <string.h>
#include "twist_encoder.h"

/* ---- LDPC generator rows: codeword bit j = bit (j % 64) of word j / 64 ---- */
static const uint64_t TWS_G[44][3] = {
    {0x0003ffffffffffffULL,0x0000000000000000ULL,0x0000000000000000ULL},
    {0x805470652bded630ULL,0x0000000000000eefULL,0x0000000000000000ULL},
    {0x6981b0b7c4265ab3ULL,0x00000000000014fcULL,0x0000000000000000ULL},
    {0x283138a2e6e5e698ULL,0x0000000000006008ULL,0x0000000000000000ULL},
    {0xb159f821d68f9c3dULL,0x000000000000832fULL,0x0000000000000000ULL},
    {0x90f80097068b7686ULL,0x00000000000106f1ULL,0x0000000000000000ULL},
    {0xeebd4db3ad4d7493ULL,0x0000000000020422ULL,0x0000000000000000ULL},
    {0xfb603e295ca116d8ULL,0x00000000000420ceULL,0x0000000000000000ULL},
    {0xfea13068588639e9ULL,0x000000000008032fULL,0x0000000000000000ULL},
    {0xd029e55b0027e5a1ULL,0x0000000000102754ULL,0x0000000000000000ULL},
    {0x7d4d0b5a891cb3b3ULL,0x000000000020264aULL,0x0000000000000000ULL},
    {0x57f86b0a5e2ac879ULL,0x00000000004025b3ULL,0x0000000000000000ULL},
    {0x8ba00d4f38c23364ULL,0x0000000000802097ULL,0x0000000000000000ULL},
    {0x6169d8d8881fabd5ULL,0x0000000001002129ULL,0x0000000000000000ULL},
    {0xe53dacdd4bd73d7cULL,0x00000000020004c6ULL,0x0000000000000000ULL},
    {0x1a3c1ed42de65194ULL,0x0000000004002407ULL,0x0000000000000000ULL},
    {0xdbe12116cccebaf6ULL,0x000000000800066cULL,0x0000000000000000ULL},
    {0xecadd81f4977fc2cULL,0x00000000100026e6ULL,0x0000000000000000ULL},
    {0x1c98af99e0401038ULL,0x00000000200024f8ULL,0x0000000000000000ULL},
    {0xcca5592c9d543525ULL,0x0000000040002309ULL,0x0000000000000000ULL},
    {0xe4cdeb617fbe793bULL,0x00000000800023a1ULL,0x0000000000000000ULL},
    {0x648484e6b9024530ULL,0x0000000100002607ULL,0x0000000000000000ULL},
    {0xb34c0c80f5990aceULL,0x00000002000026f0ULL,0x0000000000000000ULL},
    {0xa465e602cd5f449aULL,0x0000000400002216ULL,0x0000000000000000ULL},
    {0x83b0b6991d830275ULL,0x0000000800002780ULL,0x0000000000000000ULL},
    {0x54602159b46aa535ULL,0x0000001000000536ULL,0x0000000000000000ULL},
    {0x93f82c95e88b4a6cULL,0x000000200000264cULL,0x0000000000000000ULL},
    {0x645d54fbc6cf5367ULL,0x000000400000055cULL,0x0000000000000000ULL},
    {0x35ad6eb221408a57ULL,0x00000080000022b7ULL,0x0000000000000000ULL},
    {0x9b68ab862c8a56e7ULL,0x00000100000026e1ULL,0x0000000000000000ULL},
    {0x23c8be4dd658791fULL,0x0000020000002176ULL,0x0000000000000000ULL},
    {0x9029297fdef5bd6aULL,0x0000040000000306ULL,0x0000000000000000ULL},
    {0x0f48ae0f022864b6ULL,0x0000080000002371ULL,0x0000000000000000ULL},
    {0x8b90c56ae0b29fadULL,0x0000100000000515ULL,0x0000000000000000ULL},
    {0x34f9bee1568c9b2cULL,0x00002000000021abULL,0x0000000000000000ULL},
    {0x0c51cf5be68d23bcULL,0x00004000000022f0ULL,0x0000000000000000ULL},
    {0x9370c1a63678b027ULL,0x000080000000030fULL,0x0000000000000000ULL},
    {0x0154f55a13e81557ULL,0x00010000000000e1ULL,0x0000000000000000ULL},
    {0x9d49cc05c6ad403fULL,0x000200000000037fULL,0x0000000000000000ULL},
    {0x34a8504208132de5ULL,0x0004000000002169ULL,0x0000000000000000ULL},
    {0xe7b958c96257ed2dULL,0x00080000000021e5ULL,0x0000000000000000ULL},
    {0xdcf14040606efdb9ULL,0x00100000000007cdULL,0x0000000000000000ULL},
    {0x1e8c566515b23dc5ULL,0x00200000000000ccULL,0x0000000000000000ULL},
    {0xade132813afd4961ULL,0x004000000000248bULL,0x0000000000000000ULL},
};
static const uint64_t TWL_G[69][3] = {
    {0xeb70b56cb60f3260ULL,0x0000000004eee8f8ULL,0x0000000000000000ULL},
    {0x7c2231cf2b57dc56ULL,0x0000000008f2cae7ULL,0x0000000000000000ULL},
    {0xfb42bd95a50c7328ULL,0x00000000315be4dbULL,0x0000000000000000ULL},
    {0xe8bddc746a9b84d5ULL,0x0000000052dfb19dULL,0x0000000000000000ULL},
    {0x917dcc055c459b5eULL,0x0000000092f57451ULL,0x0000000000000000ULL},
    {0x6bb5c2d578198af5ULL,0x000000011355969fULL,0x0000000000000000ULL},
    {0xcf64c4c2922d4b5aULL,0x0000000202517d31ULL,0x0000000000000000ULL},
    {0x21ecbc46a3524503ULL,0x000000041353a717ULL,0x0000000000000000ULL},
    {0x7900130ca34df0b3ULL,0x0000000813d50cb7ULL,0x0000000000000000ULL},
    {0x293e3e6d4da85cdbULL,0x00000010111249d3ULL,0x0000000000000000ULL},
    {0x52ed5c3228df899cULL,0x00000020119e2c89ULL,0x0000000000000000ULL},
    {0x96fdf040aa57819bULL,0x00000040031b2dd3ULL,0x0000000000000000ULL},
    {0xb5114e9e7e6724bcULL,0x0000008013f8c293ULL,0x0000000000000000ULL},
    {0x8cc1d5ad79816315ULL,0x000001000142d7b4ULL,0x0000000000000000ULL},
    {0xb1def915f51b314eULL,0x00000200014eb282ULL,0x0000000000000000ULL},
    {0x606b3362aba473eaULL,0x0000040002e8703dULL,0x0000000000000000ULL},
    {0xe188992c07bd385eULL,0x000008001025badbULL,0x0000000000000000ULL},
    {0x56c0526208b9cd7cULL,0x0000100010dbaa2bULL,0x0000000000000000ULL},
    {0xc05c303e9e8d83c7ULL,0x00002000106c68baULL,0x0000000000000000ULL},
    {0xec622f47ac047d4dULL,0x0000400003544adaULL,0x0000000000000000ULL},
    {0xba0806eb0ea4c871ULL,0x0000800011731606ULL,0x0000000000000000ULL},
    {0x930216631e68d8a0ULL,0x00010000105217a6ULL,0x0000000000000000ULL},
    {0xd5a029a27bb8eac6ULL,0x0002000000dacf71ULL,0x0000000000000000ULL},
    {0xcbcd979f2b3ca838ULL,0x0004000011b0b48fULL,0x0000000000000000ULL},
    {0x7103eb2aaea4f898ULL,0x000800001049d25cULL,0x0000000000000000ULL},
    {0xaaf246a1e3bb15c1ULL,0x0010000001fde5e8ULL,0x0000000000000000ULL},
    {0x74bf66264b909a18ULL,0x0020000000ff5916ULL,0x0000000000000000ULL},
    {0x631b453198e4b2fdULL,0x0040000012f19473ULL,0x0000000000000000ULL},
    {0xb2f1ff837517f9a9ULL,0x00800000112d7414ULL,0x0000000000000000ULL},
    {0x367236991a2f0566ULL,0x0100000011e09a48ULL,0x0000000000000000ULL},
    {0x187bd27610a943f8ULL,0x0200000013a73aa7ULL,0x0000000000000000ULL},
    {0x58efa5083516b592ULL,0x040000001093a347ULL,0x0000000000000000ULL},
    {0x08bd3175dab5f4eaULL,0x0800000003846126ULL,0x0000000000000000ULL},
    {0xa6fcad1ecc11bb2bULL,0x1000000000e919a0ULL,0x0000000000000000ULL},
    {0xade0a4aef9f513a6ULL,0x20000000138bc385ULL,0x0000000000000000ULL},
    {0xf063d0e5d387dd25ULL,0x4000000003ec9746ULL,0x0000000000000000ULL},
    {0x15fc0a5c9f345f85ULL,0x8000000000bed0d9ULL,0x0000000000000000ULL},
    {0x61f3e07f16f59149ULL,0x00000000021e3c03ULL,0x0000000000000001ULL},
    {0x4c742a53992c8d30ULL,0x000000000147d279ULL,0x0000000000000002ULL},
    {0xcf1a1c44e52258eaULL,0x0000000010164ba3ULL,0x0000000000000004ULL},
    {0x782084ccf96c96b3ULL,0x00000000038aeb87ULL,0x0000000000000008ULL},
    {0x6fe40a6453d9414fULL,0x0000000011d3755fULL,0x0000000000000010ULL},
    {0x72cc5f6d6fd9f32eULL,0x00000000127494edULL,0x0000000000000020ULL},
    {0x1fa0b8a1f2b0d28dULL,0x0000000003df4de8ULL,0x0000000000000040ULL},
    {0xc53eee708fe790e6ULL,0x0000000003caddbcULL,0x0000000000000080ULL},
    {0xf10458b49bcf54f6ULL,0x0000000011fd5b96ULL,0x0000000000000100ULL},
    {0x7ec6128fb843d0e4ULL,0x0000000000f12bd6ULL,0x0000000000000200ULL},
    {0xba0a47ec4becf831ULL,0x00000000037a0896ULL,0x0000000000000400ULL},
    {0x6b87335b7873d6d7ULL,0x0000000010e40752ULL,0x0000000000000800ULL},
    {0xa5bd74dfa8128e9cULL,0x0000000011d705f7ULL,0x0000000000001000ULL},
    {0x8f8ac78b9dfb62a8ULL,0x000000000252995cULL,0x0000000000002000ULL},
    {0xaa6082a333a44f83ULL,0x0000000013d8f121ULL,0x0000000000004000ULL},
    {0xef11ab85b54f3cbaULL,0x000000001075c81bULL,0x0000000000008000ULL},
    {0x3a87c624c19ac29cULL,0x0000000002e2f8ceULL,0x0000000000010000ULL},
    {0x1233eada214b4e84ULL,0x0000000013b5e9aaULL,0x0000000000020000ULL},
    {0xd3c6fcb3409e1990ULL,0x0000000013a97c93ULL,0x0000000000040000ULL},
    {0xfe96c25643520253ULL,0x0000000012675d5cULL,0x0000000000080000ULL},
    {0x4c051b4fe74e76a2ULL,0x0000000002ec3c23ULL,0x0000000000100000ULL},
    {0xb2c71700fa7ae709ULL,0x0000000003688feaULL,0x0000000000200000ULL},
    {0xf1e5a5a922f973a4ULL,0x00000000104f9800ULL,0x0000000000400000ULL},
    {0xbb7d21b3ee806de2ULL,0x0000000003a29d0cULL,0x0000000000800000ULL},
    {0x7d0794de8b230cb1ULL,0x0000000012b85ed5ULL,0x0000000001000000ULL},
    {0x83982905b5512acaULL,0x00000000006ece13ULL,0x0000000002000000ULL},
    {0x91f7b4928b5f4bfcULL,0x00000000132c4bd1ULL,0x0000000004000000ULL},
    {0x97ef5b93ff1f33baULL,0x000000001301150eULL,0x0000000008000000ULL},
    {0x0337fd9f3f4da247ULL,0x0000000010d6aae8ULL,0x0000000010000000ULL},
    {0x07629a4be32f4f57ULL,0x0000000003f9e003ULL,0x0000000020000000ULL},
    {0xe9412a18916ccdccULL,0x0000000011c295c9ULL,0x0000000040000000ULL},
    {0x6e21101af06dd856ULL,0x0000000011c3c502ULL,0x0000000080000000ULL},
};

/* ---- callsign: classic-frame packing, 28 bits ---- */

static int chr_code(char c)            /* space 0, A-Z 1..26, 0-9 27..36 */
{
    if (c >= '0' && c <= '9') return c - '0' + 27;
    if (c >= 'A' && c <= 'Z') return c - 'A' + 1;
    if (c == ' ') return 0;
    return -1;
}

static int pack_call(const char *in, uint32_t *v)
{
    char t[8];
    int len = 0, i, c[6];
    while (*in == ' ') in++;
    while (in[len] && in[len] != ' ' && len < 7) {
        char ch = in[len];
        if (ch >= 'a' && ch <= 'z') ch = (char)(ch - 'a' + 'A');
        t[len++] = ch;
    }
    if (len == 0 || (in[len] && in[len] != ' ')) return -1;
    /* the 3rd character must be a digit: "F5OEO" -> " F5OEO" */
    if (len < 3 || !(t[2] >= '0' && t[2] <= '9')) {
        if (len > 5) return -1;
        memmove(t + 1, t, (size_t)len);
        t[0] = ' ';
        len++;
    }
    if (len > 6) return -1;
    for (i = len; i < 6; i++) t[i] = ' ';
    for (i = 0; i < 6; i++) c[i] = chr_code(t[i]);
    if (c[0] < 0 || c[1] < 1 || c[2] < 27) return -1;
    for (i = 3; i < 6; i++) if (c[i] < 0 || c[i] > 26) return -1;
    *v = (uint32_t)c[0];
    *v = *v * 36 + (uint32_t)(c[1] - 1);
    *v = *v * 10 + (uint32_t)(c[2] - 27);
    *v = *v * 27 + (uint32_t)c[3];
    *v = *v * 27 + (uint32_t)c[4];
    *v = *v * 27 + (uint32_t)c[5];
    return 0;
}

/* ---- locator: Maidenhead, 25 bits ---- */

static char up(char c) { return (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c; }

static int pack_loc(const char *l, uint32_t *v)
{
    int n = (int)strlen(l), sub = 0;
    char a = up(l[0]), b, c, d;
    if (n != 4 && n != 6) return -1;
    b = up(l[1]); c = l[2]; d = l[3];
    if (a < 'A' || a > 'R' || b < 'A' || b > 'R' || c < '0' || c > '9' || d < '0' || d > '9') return -1;
    if (n == 6) {
        char e = up(l[4]), f = up(l[5]);
        if (e < 'A' || e > 'X' || f < 'A' || f > 'X') return -1;
        sub = 1 + (e - 'A') * 24 + (f - 'A');
    }
    *v = (uint32_t)(((((a - 'A') * 18 + (b - 'A')) * 10 + (c - '0')) * 10 + (d - '0')) * 577 + sub);
    return 0;
}

/* ---- frame ---- */

int twist_encode(const char *call, const char *loc, uint8_t chips[TWIST_MAX_CHIPS])
{
    uint8_t v[69];
    uint32_t pc, pl = 0, s;
    uint16_t crc;
    uint64_t w[3] = { 0, 0, 0 };
    int longf = loc && loc[0], nm, k, n, i;
    const uint64_t (*G)[3];

    if (pack_call(call, &pc) != 0) return -1;
    if (longf && pack_loc(loc, &pl) != 0) return -1;

    /* message bits, MSB first */
    for (i = 0; i < 28; i++) v[i] = (uint8_t)((pc >> (27 - i)) & 1u);
    nm = 28;
    if (longf) {
        for (i = 0; i < 25; i++) v[28 + i] = (uint8_t)((pl >> (24 - i)) & 1u);
        nm = 53;
    }
    /* CRC-16/CCITT over the message bits, type-specific initial value */
    crc = longf ? 0x1D0F : 0xFFFF;
    for (i = 0; i < nm; i++) {
        int fb = ((crc >> 15) & 1) ^ v[i];
        crc = (uint16_t)(crc << 1);
        if (fb) crc ^= 0x1021;
    }
    for (i = 0; i < 16; i++) v[nm + i] = (uint8_t)((crc >> (15 - i)) & 1u);
    k = nm + 16;                        /* 44 or 69 */

    /* scrambling: 20-bit LFSR, type-specific seed */
    s = longf ? 0x3C3C7u : 0x5A5A1u;
    for (i = 0; i < k; i++) {
        uint32_t b = (s ^ (s >> 2) ^ (s >> 3) ^ (s >> 5)) & 1u;
        v[i] ^= (uint8_t)(s & 1u);
        s = (s >> 1) | (b << 19);
    }

    /* LDPC: codeword = XOR of the generator rows of the 1 bits */
    G = longf ? TWL_G : TWS_G;
    n = longf ? 160 : 119;
    for (i = 0; i < k; i++)
        if (v[i]) { w[0] ^= G[i][0]; w[1] ^= G[i][1]; w[2] ^= G[i][2]; }

    /* chips: start chip ON, then Manchester 0 -> ON,OFF ; 1 -> OFF,ON */
    chips[0] = 1;
    for (i = 0; i < n; i++) {
        uint8_t bit = (uint8_t)((w[i >> 6] >> (i & 63)) & 1u);
        chips[1 + 2 * i] = (uint8_t)!bit;
        chips[2 + 2 * i] = bit;
    }
    return 2 * n + 1;
}

int twist_encode_packed(const char *call, const char *loc, uint8_t packed[(TWIST_MAX_CHIPS + 7) / 8])
{
    uint8_t chips[TWIST_MAX_CHIPS];
    int n = twist_encode(call, loc, chips), i;
    if (n < 0) return n;
    memset(packed, 0, (TWIST_MAX_CHIPS + 7) / 8);
    for (i = 0; i < n; i++) if (chips[i]) packed[i >> 3] |= (uint8_t)(0x80u >> (i & 7));
    return n;
}
